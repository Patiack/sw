// SPDX-License-Identifier: GPL-2.0-or-later
/* Tracing for BGP
 *
 * Copyright (C) 2020  NVIDIA Corporation
 * Quentin Young
 */

/* ============================================================================
 * 【文件导读】bgp_trace.h —— bgpd 的 LTTng 追踪点(tracepoint)定义头
 * ============================================================================
 * 【本文件解决什么问题】bgpd 想在**不改变控制流、尽量不影响性能**的前提下，把
 * 内部关键事件（收发包、状态机迁移、路由过滤、EVPN 变更、优雅重启……）暴露给
 * 外部追踪工具（LTTng / USDT 探针 / BPF……），供离线分析或在线观测。为此业界用
 * “静态追踪点(static tracepoint)”机制：编译期把探针埋进代码，运行期由内核/工具
 * 决定是否真正采集；不采集时几乎零开销。
 *
 * 【为什么长这样——关键在“一个头被读两遍”】本头用 TRACEPOINT_EVENT(...) 声明一
 * 大堆事件。LTTng 要求这类提供方(provider)头被**包含两次**：
 *   第一遍（普通包含）：把每个事件登记成“追踪点声明”。
 *   第二遍（多读包含）：由 lttng/tracepoint.h 在展开 tracepoint-event.h 时，
 *     临时定义 TRACEPOINT_HEADER_MULTI_READ 后再 include 本文件，用来生成配套的
 *     探针桩代码。
 * 这就解释了为什么第 8 行的包含卫不仅用 _BGP_TRACE_H，还带
 * `|| defined(TRACEPOINT_HEADER_MULTI_READ)` —— 普通重复包含被挡住，但“多读”
 * 那一遍会被放行；也解释了文件末尾为何先 `clang-format on` 再 `#include
 * <lttng/tracepoint-event.h>`；还解释了整段定义为何被 `#ifdef HAVE_LTTNG` 包住
 * （没装 LTTng 时本头几乎为空，编译产物里不含任何探针代码）。
 *
 * 【本文件 vs lib/trace.h】lib/trace.h 定义的是**调用侧**宏 frrtrace() /
 * frrtrace_enabled()（在编译期按后端切换：LTTng / USDT / 空）。本文件则是 bgpd
 * 自己的**事件清单/声明**：要用哪些事件、每个事件带哪些参数与字段、字段如何编码。
 * 真正的触发发生在 bgp_*.c 里的 frrtrace(n, frr_bgp, 事件名, ...)。
 *
 * 【关键概念】
 *   - provider      : 事件所属提供方，本文件统一是 frr_bgp。
 *   - TRACEPOINT_EVENT             : 声明一个事件；TP_ARGS 列参数类型/名字，
 *                                    TP_FIELDS 用 ctf_* 把参数编码进追踪记录。
 *   - TRACEPOINT_EVENT_CLASS 配 _INSTANCE : 先定义“公共字段类”，再按名字实例化出
 *                                    多个同类事件（本文件用来批量生成 open /
 *                                    keepalive / ... 等报文处理事件）。
 *   - TRACEPOINT_LOGLEVEL          : 给事件关联日志级别(TRACE_INFO / TRACE_DEBUG)。
 *   - ctf_string / ctf_integer / ctf_sequence_hex / ctf_array / ctf_integer_hex /
 *     ctf_integer_network_hex      : 字段编码助手(CTF = Common Trace Format)。
 *
 * 【本文件分段（按事件主题）】
 *   1. 报文收发与过滤：packet_process 家族、packet_read、process_update、
 *      input_filter / output_filter。
 *   2. BMP(BGP Monitoring Protocol)监控镜像：bmp_*。
 *   3. 引用计数 / 锁 调试：bgp_dest_lock/unlock、bgp_peer_lock/unlock、
 *      bgp_path_info_add/free。
 *   4. EVPN 与 EVPN 多归属(ES/ESI)：evpn_*。
 *   5. 优雅重启(Graceful Restart, GR)与 EOR：gr_*、eor_*。
 *   6. 会话状态机(FSM)与错误：session_state_change、connection_attempt、
 *      fsm_event、bgp_err_str。
 *   7. 与 zebra 交互及杂项：bgp_zebra_*、bgp_ifp_oper、bgp_redistribute_* 等。
 *   8. 更新组(update-group / update-subgroup)管理：ug_*。
 *   9. 出向更新处理(update subgroup 流水线)：upd_*。
 *
 * 【阅读建议】先读 lib/trace.h 明白 frrtrace 的三种后端展开，再回头看本头第
 * 8~27 行的“两遍读取 / 提供方注册”脚手架，最后按上面 9 个分段挑感兴趣的事件读。
 * 绝大多数事件结构雷同（TP_ARGS 列参数、TP_FIELDS 逐个 ctf_* 编码），故本注释
 * 只在各分段起始给总览并挑代表事件详解，同类事件不逐一展开。
 * ============================================================================
 */

/* 【包含卫的特殊写法】普通重复包含被 _BGP_TRACE_H 挡住；但 LTTng 的
 * tracepoint-event.h 会在定义 TRACEPOINT_HEADER_MULTI_READ 后**再次**读入本
 * 文件以生成探针桩，故这里用 `|| defined(TRACEPOINT_HEADER_MULTI_READ)` 放行
 * “多读”那一遍。 */
#if !defined(_BGP_TRACE_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define _BGP_TRACE_H

/* 【调用侧宏】frrtrace()/frrtrace_enabled() 等由 lib/trace.h 按后端选择展开；
 * bgp_*.c 里就是用它们触发本文件声明的事件。 */
#include "lib/trace.h"

/* 【整段定义的总开关】只有编译时启用了 LTTng(HAVE_LTTNG) 才登记并生成追踪点；
 * 否则本头除包含卫外几乎为空，编译产物里不含任何探针代码。 */
#ifdef HAVE_LTTNG

/* 【提供方名】本文件中所有事件都归到 provider `frr_bgp` 名下；调用侧
 * frrtrace(n, frr_bgp, <事件名>, ...) 必须与此一致。 */
#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER frr_bgp

/* 【多读时告诉 LTTng 去哪里再读一遍】被再次包含的就是本文件自身。 */
#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "bgpd/bgp_trace.h"

/* 引入 LTTng 的 tracepoint 与 CTF 字段助手(ctf_integer / ctf_string / ...)。 */
#include <lttng/tracepoint.h>

/* 【字段用到的类型】TP_FIELDS 会引用 peer / attr / stream / EVPN 等结构，故需
 * 先引入它们各自的定义。 */
#include "bgpd/bgpd.h"
#include "bgpd/bgp_attr.h"
#include "lib/stream.h"
#include "bgpd/bgp_evpn_private.h"
#include "bgpd/bgp_evpn_mh.h"


/* ================= 分段 1：报文收发与过滤 =================
 * 覆盖：报文处理(packet_process 家族)、原始报文读取(packet_read)、
 *      UPDATE 处理(process_update)、入向 / 出向策略过滤
 *      (input_filter / output_filter)。 */

/* 关闭 clang-format：本块几乎全是宏，逐行对齐的排版比自动格式化更重要。 */
/* clang-format off */

/* 【事件“类”与“实例”】packet_process 定义一个公共事件类：参数固定为
 * (peer, size)，公共字段 peer 编码成主机名字符串。随后用
 * TRACEPOINT_EVENT_INSTANCE 按不同名字批量实例化出多个“同形不同名”的事件，
 * 这样 open / keepalive / update / ... 的报文处理复用同一份字段定义。 */
TRACEPOINT_EVENT_CLASS(
	frr_bgp,
	packet_process,
	TP_ARGS(struct peer *, peer, bgp_size_t, size),
	TP_FIELDS(
		ctf_string(peer, PEER_HOSTNAME(peer))
	)
)

#define PKT_PROCESS_TRACEPOINT_INSTANCE(name)                                  \
	TRACEPOINT_EVENT_INSTANCE(                                             \
		frr_bgp, packet_process, name,                                 \
		TP_ARGS(struct peer *, peer, bgp_size_t, size))                \
	TRACEPOINT_LOGLEVEL(frr_bgp, name, TRACE_INFO)

/* 【实例化宏】把“实例化 packet_process 类 + 挂日志级别”打包成一个宏：传入 name
 * 即生成一个名为 <name> 的追踪点，例如 open_process。
 * TRACEPOINT_EVENT_INSTANCE 复用上面的 packet_process 类（参数仍为
 * struct peer *, bgp_size_t）；TRACEPOINT_LOGLEVEL 给该实例设 INFO 级别。 */
PKT_PROCESS_TRACEPOINT_INSTANCE(open_process)
PKT_PROCESS_TRACEPOINT_INSTANCE(keepalive_process)
PKT_PROCESS_TRACEPOINT_INSTANCE(update_process)
PKT_PROCESS_TRACEPOINT_INSTANCE(notification_process)
PKT_PROCESS_TRACEPOINT_INSTANCE(capability_process)
PKT_PROCESS_TRACEPOINT_INSTANCE(refresh_process)

/* 【事件】packet_read —— 从套接字读到“一个完整 BGP 报文”时上报原始字节。
 * 字段：connection 提供 peer(编码为主机名)；pkt 是报文缓冲流，用
 * ctf_sequence_hex 把 [data, data+STREAM_READABLE(pkt)) 这段**变长**字节以
 * 十六进制序列编码，便于抓包级回放。 */
TRACEPOINT_EVENT(
	frr_bgp,
	packet_read,
	TP_ARGS(struct peer_connection *, connection, struct stream *, pkt),
	TP_FIELDS(
		ctf_string(peer, PEER_HOSTNAME(connection->peer))
		ctf_sequence_hex(uint8_t, packet, pkt->data, size_t,
				 STREAM_READABLE(pkt))
	)
)

TRACEPOINT_LOGLEVEL(frr_bgp, packet_read, TRACE_INFO)

/* 【事件】process_update —— 处理一条 UPDATE 中的单个前缀时上报。字段：peer
 * (主机名)、prefix(前缀字符串)、addpath_id(Add-Path 路径标识)、afi/safi、
 * attribute_ptr(属性块地址，十六进制编码，便于与其它事件配对)。 */
TRACEPOINT_EVENT(
	frr_bgp,
	process_update,
	TP_ARGS(struct peer *, peer, char *, pfx, uint32_t, addpath_id, afi_t,
		afi, safi_t, safi, struct attr *, attr),
	TP_FIELDS(
		ctf_string(peer, PEER_HOSTNAME(peer))
		ctf_string(prefix, pfx)
		ctf_integer(uint32_t, addpath_id, addpath_id)
		ctf_integer(afi_t, afi, afi)
		ctf_integer(safi_t, safi, safi)
		ctf_integer_hex(intptr_t, attribute_ptr, attr)
	)
)

TRACEPOINT_LOGLEVEL(frr_bgp, process_update, TRACE_INFO)

/* 【事件】input_filter —— 入向策略(route-map / filter)对某前缀的判定结果。
 * 字段：peer、prefix、afi/safi、action(result 字符串，如 "permit" / "deny")。 */
TRACEPOINT_EVENT(
	frr_bgp,
	input_filter,
	TP_ARGS(struct peer *, peer, char *, pfx, afi_t, afi, safi_t, safi,
		const char *, result),
	TP_FIELDS(
		ctf_string(peer, PEER_HOSTNAME(peer))
		ctf_string(prefix, pfx)
		ctf_integer(afi_t, afi, afi)
		ctf_integer(safi_t, safi, safi)
		ctf_string(action, result)
	)
)

TRACEPOINT_LOGLEVEL(frr_bgp, input_filter, TRACE_INFO)

/* 【事件】output_filter —— 出向策略对某前缀的判定结果，字段同 input_filter。 */
TRACEPOINT_EVENT(
	frr_bgp,
	output_filter,
	TP_ARGS(struct peer *, peer, char *, pfx, afi_t, afi, safi_t, safi,
		const char *, result),
	TP_FIELDS(
		ctf_string(peer, PEER_HOSTNAME(peer))
		ctf_string(prefix, pfx)
		ctf_integer(afi_t, afi, afi)
		ctf_integer(safi_t, safi, safi)
		ctf_string(action, result)
	)
)

TRACEPOINT_LOGLEVEL(frr_bgp, output_filter, TRACE_INFO)

/* ================= 分段 2：BMP（BGP Monitoring Protocol）监控 =================
 * BMP 是把本机 BGP 会话与路由“镜像”给监控采集器的协议。这些事件记录 BMP 侧的
 * 关键动作：镜像报文、发送 EOR(End-of-RIB)、缓存对端 OPEN、对端状态/回退、路由
 * 处理挂钩、下一跳有效性变化等，大多为调试(TRACE_DEBUG)级别。 */

/* BMP tracepoints */

/* BMP mirrors a packet to all mirror-enabled targets */
/* 【事件】bmp_mirror_packet —— 把一个报文镜像给所有启用镜像的目标。字段：
 * peer(主机名)、type(BMP 消息类型)、packet(变长十六进制字节序列)。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bmp_mirror_packet,
	TP_ARGS(struct peer *, peer, uint8_t, type, struct stream *, pkt),
	TP_FIELDS(
		ctf_string(peer, PEER_HOSTNAME(peer))
		ctf_integer(uint8_t, type, type)
		ctf_sequence_hex(uint8_t, packet, pkt->data, size_t,
				 STREAM_READABLE(pkt))
	)
)

TRACEPOINT_LOGLEVEL(frr_bgp, bmp_mirror_packet, TRACE_INFO)


/* BMP sends an EOR */
/* 【事件】bmp_eor —— BMP 向采集器发送一条 End-of-RIB。字段：afi/safi、flags、
 * peer_type_flag、bgp(实例名 name_pretty)。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bmp_eor,
	TP_ARGS(afi_t, afi, safi_t, safi, uint8_t, flags, uint8_t, peer_type_flag,
		struct bgp *, bgp),
	TP_FIELDS(
		ctf_integer(afi_t, afi, afi)
		ctf_integer(safi_t, safi, safi)
		ctf_integer(uint8_t, flags, flags)
		ctf_integer(uint8_t, peer_type_flag, peer_type_flag)
		ctf_string(bgp, bgp->name_pretty)
	)
)

TRACEPOINT_LOGLEVEL(frr_bgp, bmp_eor, TRACE_INFO)


/* BMP updates its copy of the last OPEN a peer sent */
/* 【事件】bmp_update_saved_open —— BMP 更新“最近一次对端 OPEN 报文”的副本。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bmp_update_saved_open,
	TP_ARGS(struct peer *, peer, struct stream *, pkt),
	TP_FIELDS(
		ctf_string(peer, PEER_HOSTNAME(peer))
		ctf_sequence_hex(uint8_t, packet, pkt->data, size_t,
				 STREAM_READABLE(pkt))
	)
)

TRACEPOINT_LOGLEVEL(frr_bgp, bmp_update_saved_open, TRACE_DEBUG)


/* BMP is notified of a peer status change internally */
/* 【事件】bmp_peer_status_changed —— 内部通知 BMP“某邻居状态变了”。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bmp_peer_status_changed,
	TP_ARGS(struct peer *, peer),
	TP_FIELDS(
		ctf_string(peer, PEER_HOSTNAME(peer))
	)
)

TRACEPOINT_LOGLEVEL(frr_bgp, bmp_peer_status_changed, TRACE_DEBUG)


/*
 * BMP is notified that a peer has transitioned in the opposite direction of
 * Established internally
 */
/* 【事件】bmp_peer_backward_transition —— 通知 BMP“某邻居朝远离 Established 的
 * 方向迁移了”。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bmp_peer_backward_transition,
	TP_ARGS(struct peer *, peer),
	TP_FIELDS(
		ctf_string(peer, PEER_HOSTNAME(peer))
	)
)

TRACEPOINT_LOGLEVEL(frr_bgp, bmp_peer_backward, TRACE_DEBUG)


/*
 * BMP is hooked for a route process
 */
/* 【事件】bmp_process —— BMP 挂钩到路由处理，记录前缀与是否为撤销。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bmp_process,
	TP_ARGS(struct peer *, peer, char *, pfx, afi_t,
		afi, safi_t, safi, bool, withdraw),
	TP_FIELDS(
		ctf_string(peer, PEER_HOSTNAME(peer))
		ctf_string(prefix, pfx)
		ctf_integer(afi_t, afi, afi)
		ctf_integer(safi_t, safi, safi)
		ctf_integer(bool, withdraw, withdraw)
	)
)

TRACEPOINT_LOGLEVEL(frr_bgp, bmp_process, TRACE_DEBUG)

/*
 * BMP is hooked for a nexthop tracking event
 */
/* 【事件】bmp_nht_path_valid —— BMP 挂钩到下一跳追踪(NHT)，记录某路径下一跳
 * 是否有效。字段：bgp(实例名)、prefix、path 的对端主机名、valid。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bmp_nht_path_valid,
	TP_ARGS(struct bgp *, bgp, char *, pfx, struct bgp_path_info *,
		path, bool, valid),
	TP_FIELDS(
		ctf_string(bgp, bgp->name_pretty)
		ctf_string(prefix, pfx)
		ctf_string(path, PEER_HOSTNAME(path->peer))
		ctf_integer(bool, valid, valid)
	)
)

TRACEPOINT_LOGLEVEL(frr_bgp, bmp_nht_path_valid, TRACE_DEBUG)

/* ================= 分段 3：引用计数 / 锁 调试 =================
 * BGP 用引用计数与节点锁管理内存与生命周期(前缀 dest 锁、peer 引用计数、路径项
 * bgp_path_info 的增删)。这些事件记录计数值变化及其调用点(caller)名字，用于
 * 排查“谁多持 / 少持了一次引用”这类内存与生命周期 bug。 */

/*
 * bgp_dest_lock/bgp_dest_unlock
 */
/* 【事件】bgp_dest_lock —— 对前缀节点 dest 加锁。字段：prefix、加锁后的计数
 * (count)。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bgp_dest_lock,
	TP_ARGS(struct bgp_dest *, dest),
	TP_FIELDS(
		ctf_string(prefix, bgp_dest_get_prefix_str(dest))
		ctf_integer(unsigned int, count, bgp_dest_get_lock_count(dest))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_dest_lock, TRACE_INFO)

/* 【事件】bgp_dest_unlock —— 对前缀节点 dest 解锁。字段：prefix、解锁后计数。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bgp_dest_unlock,
	TP_ARGS(struct bgp_dest *, dest),
	TP_FIELDS(
		ctf_string(prefix, bgp_dest_get_prefix_str(dest))
		ctf_integer(unsigned int, count, bgp_dest_get_lock_count(dest))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_dest_unlock, TRACE_INFO)

/*
 * peer_lock/peer_unlock
 */
/* 【事件】bgp_peer_lock —— 对 peer 加引用。字段：caller(调用点名字)、peer、
 * 加后计数(peer->lock)。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bgp_peer_lock,
	TP_ARGS(struct peer *, peer,
		const char *, name),
	TP_FIELDS(
		ctf_string(caller, name)
		ctf_string(peer, PEER_HOSTNAME(peer))
		ctf_integer(unsigned int, count, peer->lock)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_peer_lock, TRACE_INFO)

/* 【事件】bgp_peer_unlock —— 对 peer 减引用。字段同 bgp_peer_lock。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bgp_peer_unlock,
	TP_ARGS(struct peer *, peer,
		const char *, name),
	TP_FIELDS(
		ctf_string(caller, name)
		ctf_string(peer, PEER_HOSTNAME(peer))
		ctf_integer(unsigned int, count, peer->lock)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_peer_unlock, TRACE_INFO)

/*
 * bgp_path_info_add/bgp_path_info_free
 */
/* 【事件】bgp_path_info_add —— 向前缀节点加入一条路径项 bgp_path_info。字段：
 * caller、prefix、peer、dest_lock(dest 锁计数)、peer_lock(peer 引用计数)。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bgp_path_info_add,
	TP_ARGS(struct bgp_dest *, dest,
		struct bgp_path_info *, bpi,
		const char *, name),
	TP_FIELDS(
		ctf_string(caller, name)
		ctf_string(prefix, bgp_dest_get_prefix_str(dest))
		ctf_string(peer, PEER_HOSTNAME(bpi->peer))
		ctf_integer(unsigned int, dest_lock,
			    bgp_dest_get_lock_count(dest))
		ctf_integer(unsigned int, peer_lock, bpi->peer->lock)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_path_info_add, TRACE_INFO)

/* 【事件】bgp_path_info_free —— 释放一条路径项。字段：caller、prefix(取自
 * bpi->net)、peer、dest_lock、peer_lock。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bgp_path_info_free,
	TP_ARGS(struct bgp_path_info *, bpi,
		const char *, name),
	TP_FIELDS(
		ctf_string(caller, name)
		ctf_string(prefix, bgp_dest_get_prefix_str(bpi->net))
		ctf_string(peer, PEER_HOSTNAME(bpi->peer))
		ctf_integer(unsigned int, dest_lock,
			    bgp_dest_get_lock_count(bpi->net))
		ctf_integer(unsigned int, peer_lock, bpi->peer->lock)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_path_info_free, TRACE_INFO)

/* ================= 分段 4：EVPN / EVPN 多归属(ES) =================
 * 这一大段是 EVPN 相关事件，可再细分：
 *   - zsend 组(evpn_*_zsend)：把本地事件“发往 zebra”时上报，涵盖 MAC/IP 路由、
 *     BUM 流量的 VTEP、ES / ESI、下一跳组(NHG)、远端 MAC(RMAC)等。
 *   - zrecv 组(evpn_*_zrecv)：从 zebra“收到”EVPN 事件时上报，如本地 VNI 增删、
 *     本地 MAC/IP 增删、本地 ES 增删等。
 *   - 本地 ES 路由(type 1/4)变更、Type-5 路由通告 / 撤销等。
 * 字段多为 ESI、VNI、VTEP 地址、MAC、VRF 等 EVPN 关键标识；事件高度同构，下面
 * 只在各组起始处给总览，不逐一展开。 */

/* 【事件】evpn_mac_ip_zsend —— 向 zebra 发送“MAC/IP 路由”相关事件。字段：
 * add(增 / 删)、vni、eth_tag、mac、ip、vtep、esi。 */
TRACEPOINT_EVENT(
	frr_bgp,
	evpn_mac_ip_zsend,
	TP_ARGS(int, add, struct bgpevpn *, vpn,
		const struct prefix_evpn *, pfx,
		struct ipaddr *, vtep, esi_t *, esi),
	TP_FIELDS(
		ctf_string(action, add ? "add" : "del")
		ctf_integer(vni_t, vni, (vpn ? vpn->vni : 0))
		ctf_integer(uint32_t, eth_tag, &pfx->prefix.macip_addr.eth_tag)
		ctf_array(unsigned char, mac, &pfx->prefix.macip_addr.mac,
			sizeof(struct ethaddr))
		ctf_array(unsigned char, ip, &pfx->prefix.macip_addr.ip,
			sizeof(struct ipaddr))
		ctf_array(unsigned char, vtep, vtep, sizeof(struct ipaddr))
		ctf_array(unsigned char, esi, esi, sizeof(esi_t))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_mac_ip_zsend, TRACE_INFO)

/* 【事件】evpn_bum_vtep_zsend —— 向 zebra 发送 BUM 流量的 VTEP 列表变更。 */
TRACEPOINT_EVENT(
	frr_bgp,
	evpn_bum_vtep_zsend,
	TP_ARGS(int, add, struct bgpevpn *, vpn,
		const struct prefix_evpn *, pfx),
	TP_FIELDS(
		ctf_string(action, add ? "add" : "del")
		ctf_integer(vni_t, vni, (vpn ? vpn->vni : 0))
		ctf_array(unsigned char, vtep, &pfx->prefix.imet_addr.ip, sizeof(struct ipaddr))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_bum_vtep_zsend, TRACE_INFO)

/* 以下 evpn_mh_* 组：EVPN 多归属(Multi-Homing)相关，涉及 ES(Ethernet Segment)、
 * ESI、ES 的 VTEP、下一跳组(NHG)、下一跳(NH)、远端 MAC(RMAC)等。此处先是若干
 * zsend(发往 zebra)事件。 */
TRACEPOINT_EVENT(
	frr_bgp,
	evpn_mh_vtep_zsend,
	TP_ARGS(bool, add, struct bgp_evpn_es *, es,
		struct bgp_evpn_es_vtep *, es_vtep),
	TP_FIELDS(
		ctf_string(action, add ? "add" : "del")
		ctf_string(esi, es->esi_str)
		ctf_array(unsigned char, vtep, &es_vtep->vtep_ip, sizeof(struct ipaddr))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_mh_vtep_zsend, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_mh_nhg_zsend,
	TP_ARGS(bool, add, bool, type_v4, uint32_t, nhg_id,
		struct bgp_evpn_es_vrf *, es_vrf),
	TP_FIELDS(
		ctf_string(action, add ? "add" : "del")
		ctf_string(type, type_v4 ? "v4" : "v6")
		ctf_integer(unsigned int, nhg, nhg_id)
		ctf_string(esi, es_vrf->es->esi_str)
		ctf_integer(int, vrf, es_vrf->bgp_vrf->vrf_id)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_mh_nhg_zsend, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_mh_nh_zsend,
	TP_ARGS(uint32_t, nhg_id, struct bgp_evpn_es_vtep *, vtep,
		struct bgp_evpn_es_vrf *, es_vrf),
	TP_FIELDS(
		ctf_integer(unsigned int, nhg, nhg_id)
		ctf_array(unsigned char, vtep, &vtep->vtep_ip, sizeof(struct ipaddr))
		ctf_integer(int, svi, es_vrf->bgp_vrf->l3vni_svi_ifindex)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_mh_nh_zsend, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_mh_nh_rmac_zsend,
	TP_ARGS(bool, add, struct bgp_evpn_nh *, nh),
	TP_FIELDS(
		ctf_string(action, add ? "add" : "del")
		ctf_integer(int, vrf, nh->bgp_vrf->vrf_id)
		ctf_string(nh, nh->nh_str)
		ctf_array(unsigned char, rmac, &nh->rmac,
			sizeof(struct ethaddr))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_nh_rmac_zsend, TRACE_INFO)

/* 以下 *_zrecv 组：从 zebra 收到的本地 EVPN 事件，包括本地 ES 增删、本地 ES 的
 * EVI 增删、ES-EVI 的 VTEP 增删、EAD 路由变更等。 */
TRACEPOINT_EVENT(
	frr_bgp,
	evpn_mh_local_es_add_zrecv,
	TP_ARGS(esi_t *, esi, struct ipaddr *, vtep,
		uint8_t, active, uint8_t, bypass, uint16_t, df_pref),
	TP_FIELDS(
		ctf_array(unsigned char, esi, esi, sizeof(esi_t))
		ctf_array(unsigned char, vtep, vtep, sizeof(struct ipaddr))
		ctf_integer(uint8_t, active, active)
		ctf_integer(uint8_t, bypass, bypass)
		ctf_integer(uint16_t, df_pref, df_pref)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_mh_local_es_add_zrecv, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_mh_local_es_del_zrecv,
	TP_ARGS(esi_t *, esi),
	TP_FIELDS(
		ctf_array(unsigned char, esi, esi, sizeof(esi_t))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_mh_local_es_del_zrecv, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_mh_local_es_evi_add_zrecv,
	TP_ARGS(esi_t *, esi, vni_t, vni),
	TP_FIELDS(
		ctf_array(unsigned char, esi, esi, sizeof(esi_t))
		ctf_integer(vni_t, vni, vni)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_mh_local_es_evi_add_zrecv, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_mh_local_es_evi_del_zrecv,
	TP_ARGS(esi_t *, esi, vni_t, vni),
	TP_FIELDS(
		ctf_array(unsigned char, esi, esi, sizeof(esi_t))
		ctf_integer(vni_t, vni, vni)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_mh_local_es_evi_del_zrecv, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_mh_es_evi_vtep_add,
	TP_ARGS(esi_t *, esi, vni_t, vni, struct ipaddr *, vtep,
		uint8_t, ead_es),
	TP_FIELDS(
		ctf_array(unsigned char, esi, esi, sizeof(esi_t))
		ctf_integer(vni_t, vni, vni)
		ctf_array(unsigned char, vtep, vtep, sizeof(struct ipaddr))
		ctf_integer(uint8_t, ead_es, ead_es)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_mh_es_evi_vtep_add, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_mh_es_evi_vtep_del,
	TP_ARGS(esi_t *, esi, vni_t, vni, struct ipaddr *, vtep,
		uint8_t, ead_es),
	TP_FIELDS(
		ctf_array(unsigned char, esi, esi, sizeof(esi_t))
		ctf_integer(vni_t, vni, vni)
		ctf_array(unsigned char, vtep, vtep, sizeof(struct ipaddr))
		ctf_integer(uint8_t, ead_es, ead_es)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_mh_es_evi_vtep_del, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_mh_local_ead_es_evi_route_upd,
	TP_ARGS(esi_t *, esi, vni_t, vni,
		uint8_t, route_type,
		struct ipaddr *, vtep),
	TP_FIELDS(
		ctf_array(unsigned char, esi, esi, sizeof(esi_t))
		ctf_integer(vni_t, vni, vni)
		ctf_integer(uint8_t, route_type, route_type)
		ctf_array(unsigned char, vtep, vtep, sizeof(struct ipaddr))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_mh_local_ead_es_evi_route_upd, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_mh_local_ead_es_evi_route_del,
	TP_ARGS(esi_t *, esi, vni_t, vni,
		uint8_t, route_type,
		struct ipaddr *, vtep),
	TP_FIELDS(
		ctf_array(unsigned char, esi, esi, sizeof(esi_t))
		ctf_integer(vni_t, vni, vni)
		ctf_integer(uint8_t, route_type, route_type)
		ctf_array(unsigned char, vtep, vtep, sizeof(struct ipaddr))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_mh_local_ead_es_evi_route_del, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_local_vni_add_zrecv,
	TP_ARGS(vni_t, vni, struct ipaddr *, vtep, vrf_id_t, vrf,
			struct in_addr, mc_grp),
	TP_FIELDS(
		ctf_integer(vni_t, vni, vni)
		ctf_array(unsigned char, vtep, vtep, sizeof(struct ipaddr))
		ctf_integer_network_hex(unsigned int, mc_grp,
			mc_grp.s_addr)
		ctf_integer(int, vrf, vrf)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_local_vni_add_zrecv, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_local_vni_del_zrecv,
	TP_ARGS(vni_t, vni),
	TP_FIELDS(
		ctf_integer(vni_t, vni, vni)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_local_vni_del_zrecv, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_local_macip_add_zrecv,
	TP_ARGS(vni_t, vni, struct ethaddr *, mac,
		struct ipaddr *, ip, uint32_t, flags,
		uint32_t, seqnum, esi_t *, esi),
	TP_FIELDS(
		ctf_integer(vni_t, vni, vni)
		ctf_array(unsigned char, mac, mac,
			sizeof(struct ethaddr))
		ctf_array(unsigned char, ip, ip,
			sizeof(struct ipaddr))
		ctf_integer(uint32_t, flags, flags)
		ctf_integer(uint32_t, seq, seqnum)
		ctf_array(unsigned char, esi, esi, sizeof(esi_t))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_local_macip_add_zrecv, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_local_macip_del_zrecv,
	TP_ARGS(vni_t, vni, struct ethaddr *, mac, struct ipaddr *, ip,
			int, state),
	TP_FIELDS(
		ctf_integer(vni_t, vni, vni)
		ctf_array(unsigned char, mac, mac,
			sizeof(struct ethaddr))
		ctf_array(unsigned char, ip, ip,
			sizeof(struct ipaddr))
		ctf_integer(int, state, state)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_local_macip_del_zrecv, TRACE_INFO)

/* 【事件】evpn_advertise_type5 / evpn_withdraw_type5 —— EVPN Type-5(IP 前缀)路由
 * 的通告与撤销。字段：vrf_id、ip / prefix、rmac、vtep。 */
TRACEPOINT_EVENT(
	frr_bgp,
	evpn_advertise_type5,
	TP_ARGS(vrf_id_t, vrf, const struct prefix_evpn *, pfx,
		struct ethaddr *, rmac, struct ipaddr *, vtep),
	TP_FIELDS(
		ctf_integer(int, vrf_id, vrf)
		ctf_array(unsigned char, ip, &pfx->prefix.prefix_addr.ip,
			sizeof(struct ipaddr))
		ctf_array(unsigned char, rmac, rmac,
			sizeof(struct ethaddr))
		ctf_array(unsigned char, vtep, vtep, sizeof(struct ipaddr))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_advertise_type5, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_withdraw_type5,
	TP_ARGS(vrf_id_t, vrf, const struct prefix_evpn *, pfx),
	TP_FIELDS(
		ctf_integer(int, vrf_id, vrf)
		ctf_array(unsigned char, ip, &pfx->prefix.prefix_addr.ip,
			sizeof(struct ipaddr))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_withdraw_type5, TRACE_INFO)

/* 【事件】evpn_local_l3vni_add_zrecv / _del_zrecv —— 本地 L3VNI 增删事件(来自
 * zebra)。字段含 vni、vrf、SVI RMAC、VRR RMAC、VTEP、filter、svi_ifindex、
 * anycast_mac 等。 */
TRACEPOINT_EVENT(
	frr_bgp,
	evpn_local_l3vni_add_zrecv,
	TP_ARGS(vni_t, vni, vrf_id_t, vrf,
			struct ethaddr *, svi_rmac,
			struct ethaddr *, vrr_rmac, int, filter,
			struct ipaddr *, vtep, int, svi_ifindex,
			bool, anycast_mac),
	TP_FIELDS(
		ctf_integer(vni_t, vni, vni)
		ctf_integer(int, vrf, vrf)
		ctf_array(unsigned char, svi_rmac, svi_rmac,
			sizeof(struct ethaddr))
		ctf_array(unsigned char, vrr_rmac, vrr_rmac,
			sizeof(struct ethaddr))
		ctf_array(unsigned char, vtep, vtep, sizeof(struct ipaddr))
		ctf_integer(int, filter, filter)
		ctf_integer(int, svi_ifindex, svi_ifindex)
		ctf_string(anycast_mac, anycast_mac ? "y" : "n")
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_local_l3vni_add_zrecv, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	evpn_local_l3vni_del_zrecv,
	TP_ARGS(vni_t, vni, vrf_id_t, vrf),
	TP_FIELDS(
		ctf_integer(vni_t, vni, vni)
		ctf_integer(int, vrf, vrf)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_local_l3vni_del_zrecv, TRACE_INFO)

/* ================= 分段 5：优雅重启(Graceful Restart, GR)与 EOR =================
 * 这一大段记录 GR 状态机与 End-of-RIB 交互，用于分析重启期间的收敛行为。很多
 * 事件带一个 location 字段(loc)，它是一个**枚举编号**，标注代码中触发该事件的
 * 具体位置——上面的英文注释已逐号列出 loc 的含义(如 Loc 1/2/...)，分析追踪时把
 * loc 与该注释对照即可定位触发点。覆盖：推迟计时器(gr_deferral_timer_*)、EOR
 * 收集(gr_eors / eor_*)、GR 中的路由选择推迟 / 继续(gr_*deferred_path_selection)、
 * 对端能力与状态(gr_peer_up_* / gr_send_*capability / gr_bgp_state)等。 */

/*
 * Loc 1 - gr_tier1_deferral_timer_start,
 * Loc 2 - gr_tier2_deferral_timer_start,
 */
/* 【事件】gr_deferral_timer_start —— 启动 GR 推迟计时器(等待对端重连并收齐路由)。
 * 字段：bgp_instance、afi/safi、defer_time、location(触发位置编号)。 */
TRACEPOINT_EVENT(
	frr_bgp,
	gr_deferral_timer_start,
	TP_ARGS(char *, bgp_name, uint8_t, afi, uint8_t, safi,
		uint32_t, defer_time, uint8_t, loc),
	TP_FIELDS(ctf_string(bgp_instance, bgp_name)
		ctf_integer(uint8_t, afi, afi)
		ctf_integer(uint8_t, safi, safi)
		ctf_integer(uint32_t, defer_time, defer_time)
		ctf_integer(uint8_t, location, loc)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, gr_deferral_timer_start, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	gr_deferral_timer_expiry,
	TP_ARGS(char *, bgp_name, bool, tier2, uint8_t, afi, uint8_t, safi,
		uint32_t, deferred_rt_cnt),
	TP_FIELDS(ctf_string(bgp_instance, bgp_name)
		ctf_string(gr_tier, tier2 ? "2" : "1")
		ctf_integer(uint8_t, afi, afi)
		ctf_integer(uint8_t, safi, safi)
		ctf_integer(uint32_t, deferred_routes, deferred_rt_cnt)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, gr_deferral_timer_expiry, TRACE_INFO)

/*
 * Loc1: gr_check_all_eors
 * Loc2: gr_all_directly_connected_eors_rcvd
 * Loc3: gr_all_multihop_eors_not_rcvd
 * Loc4: gr_all_eors_rcvd
 * Loc5: gr_no_multihop_eors_pending
 * Loc6: gr_eor_rcvd_check_path_select
 * Loc7: gr_do_deferred_path_selection
 */
/* 【事件】gr_eors —— GR 收集 / 检查 EOR 过程中的各阶段上报。字段：bgp_instance、
 * afi/safi、location；上面的英文注释列出 Loc1~Loc7 各对应哪一步(检查全部 EOR、
 * 直连 / 多跳 EOR 收齐与否、开始做推迟的路由选择等)。 */
TRACEPOINT_EVENT(
	frr_bgp,
	gr_eors,
	TP_ARGS(char *, bgp_name, uint8_t, afi, uint8_t, safi, uint8_t, loc),
	TP_FIELDS(ctf_string(bgp_instance, bgp_name)
		ctf_integer(uint8_t, afi, afi)
		ctf_integer(uint8_t, safi, safi)
		ctf_integer(uint8_t, location, loc)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, gr_eors, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	gr_update_complete,
	TP_ARGS(char *, bgp_name, uint8_t, afi, uint8_t, safi),
	TP_FIELDS(ctf_string(bgp_instance, bgp_name)
		ctf_integer(uint8_t, afi, afi)
		ctf_integer(uint8_t, safi, safi)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, gr_update_complete, TRACE_INFO)

/*
 * Loc1: gr_eor_awaited_from
 * Loc2: gr_eor_ignore
 * Loc3: gr_multihop_eor_awaited
 * Loc4: gr_eor_ignore_after_tier1_timer_expiry
 * Loc5: gr_directly_connected_eor_awaited
 */
/* 【事件】gr_eor_peer —— 针对**单个对端**记录 EOR 的等待 / 忽略状态。字段：
 * bgp_instance、afi/safi、peer、location(含义见上方英文注释 Loc1~Loc5)。 */
TRACEPOINT_EVENT(
	frr_bgp,
	gr_eor_peer,
	TP_ARGS(char *, bgp_name, uint8_t, afi, uint8_t, safi,
		char *, peer_name, uint8_t, loc),
	TP_FIELDS(ctf_string(bgp_instance, bgp_name)
		ctf_integer(uint8_t, afi, afi)
		ctf_integer(uint8_t, safi, safi)
		ctf_string(peer, peer_name)
		ctf_integer(uint8_t, location, loc)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, gr_eor_peer, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	gr_start_deferred_path_selection,
	TP_ARGS(char *, bgp_name, uint8_t, afi, uint8_t, safi,
		uint32_t, deferred_rt_cnt),
	TP_FIELDS(ctf_string(bgp_instance, bgp_name)
		ctf_integer(uint8_t, afi, afi)
		ctf_integer(uint8_t, safi, safi)
		ctf_integer(uint32_t, deferred_routes, deferred_rt_cnt)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, gr_start_deferred_path_selection, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	gr_peer_up_ignore,
	TP_ARGS(char *, bgp_name, char *, peer_host,
		uint32_t, peer_cap, uint64_t, peer_flags),
	TP_FIELDS(ctf_string(bgp_instance, bgp_name)
		ctf_string(peer, peer_host)
		ctf_integer(uint32_t, capability, peer_cap)
		ctf_integer(uint64_t, peer_flags, peer_flags)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, gr_peer_up_ignore, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	gr_send_rbit_capability,
	TP_ARGS(char *, bgp_name, char *, peer_host,
		uint32_t, restart_time, bool, restart),
	TP_FIELDS(ctf_string(bgp_instance, bgp_name)
		ctf_string(peer, peer_host)
		ctf_integer(uint32_t, restart_time, restart_time)
		ctf_integer(bool, R_bit, restart)

	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, gr_send_rbit_capability, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	gr_send_fbit_capability,
	TP_ARGS(char *, bgp_name, char *, peer_host,
		uint8_t, afi, uint8_t, safi, bool, f_bit),
	TP_FIELDS(ctf_string(bgp_instance, bgp_name)
		ctf_string(peer, peer_host)
		ctf_integer(uint8_t, afi, afi)
		ctf_integer(uint8_t, safi, safi)
		ctf_integer(bool, F_bit, f_bit)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, gr_send_fbit_capability, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	gr_continue_deferred_path_selection,
	TP_ARGS(char *, bgp_name, uint8_t, afi, uint8_t, safi,
		uint32_t, deferred_rt_remain),
	TP_FIELDS(ctf_string(bgp_instance, bgp_name)
		ctf_integer(uint8_t, afi, afi)
		ctf_integer(uint8_t, safi, safi)
		ctf_integer(uint32_t, remaining_routes, deferred_rt_remain)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, gr_continue_deferred_path_selection, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	gr_send_capabilities,
	TP_ARGS(char *, bgp_name, uint32_t, vrf_id, bool, disable),
	TP_FIELDS(ctf_string(bgp_instance, bgp_name)
		ctf_integer(uint32_t, vrf_id, vrf_id)
		ctf_integer(bool, disable, disable)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, gr_send_capabilities, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	gr_zebra_update,
	TP_ARGS(char *, bgp_name, uint8_t, afi, uint8_t, safi, const char *, type),
	TP_FIELDS(ctf_string(bgp_instance, bgp_name)
		ctf_integer(uint8_t, afi, afi)
		ctf_integer(uint8_t, safi, safi)
		ctf_string(type, type)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, gr_zebra_update, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	eor_send,
	TP_ARGS(char *, bgp_name, uint8_t, afi, uint8_t, safi,
		char *, peer_name),
	TP_FIELDS(ctf_string(bgp_instance, bgp_name)
		ctf_integer(uint8_t, afi, afi)
		ctf_integer(uint8_t, safi, safi)
		ctf_string(peer, peer_name)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, eor_send, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	eor_received,
	TP_ARGS(char *, bgp_name, uint8_t, afi, uint8_t, safi,
		char *, peer_name),
	TP_FIELDS(ctf_string(bgp_instance, bgp_name)
		ctf_integer(uint8_t, afi, afi)
		ctf_integer(uint8_t, safi, safi)
		ctf_string(peer, peer_name)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, eor_received, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	gr_bgp_state,
	TP_ARGS(char *, bgp_name, bool, all_peers_admin_down, bool, bgp_in_gr,
		uint8_t, global_gr_mode, bool, gr_cfgd_at_nbr_lvl),
	TP_FIELDS(ctf_string(bgp_instance, bgp_name)
		ctf_integer(bool, all_peers_admin_down, all_peers_admin_down)
		ctf_integer(bool, bgp_in_gr, bgp_in_gr)
		ctf_integer(uint8_t, global_gr_mode, global_gr_mode)
		ctf_integer(bool, gr_cfgd_at_nbr_lvl, gr_cfgd_at_nbr_lvl)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, gr_bgp_state, TRACE_INFO)

/* ================= 分段 6：会话状态机(FSM)与错误 =================
 * BGP 状态机与连接的观测点：状态迁移、连接尝试、FSM 事件、错误字符串相关。 */

/* 【事件】session_state_change —— 邻居状态机发生迁移。字段：peer、location
 * (触发位置编号)、old_status/new_status(迁移前后状态)、event(触发事件)、
 * vrf_id、fd(套接字)、established_peers(当前处于 Established 的对端数)。 */
TRACEPOINT_EVENT(
	frr_bgp,
	session_state_change,
	TP_ARGS(struct peer *, peer, uint8_t, location),
	TP_FIELDS(
		ctf_string(peer, PEER_HOSTNAME(peer))
		ctf_integer(uint8_t, location, location)
		ctf_integer(enum bgp_fsm_status, old_status, peer->connection->ostatus)
		ctf_integer(enum bgp_fsm_status, new_status, peer->connection->status)
		ctf_integer(enum bgp_fsm_events, event, peer->cur_event)
		ctf_integer(uint32_t, vrf_id, peer->bgp->vrf_id)
		ctf_integer(int, fd, peer->connection->fd)
		ctf_integer(uint32_t, established_peers, peer->bgp->established_peers)
	)
)

TRACEPOINT_LOGLEVEL(frr_bgp, session_state_change, TRACE_INFO)

/* 【事件】connection_attempt —— 发起(或结束)一次连接尝试。字段：peer、status
 * (尝试结果)、current_status(当前 FSM 状态)、vrf_id、fd。 */
TRACEPOINT_EVENT(
	frr_bgp,
	connection_attempt,
	TP_ARGS(struct peer *, peer, int, status),
	TP_FIELDS(
		ctf_string(peer, PEER_HOSTNAME(peer))
		ctf_integer(int, status, status)
		ctf_integer(enum bgp_fsm_status, current_status, peer->connection->status)
		ctf_integer(uint32_t, vrf_id, peer->bgp->vrf_id)
		ctf_integer(int, fd, peer->connection->fd)
	)
)

TRACEPOINT_LOGLEVEL(frr_bgp, connection_attempt, TRACE_INFO)

/* 【事件】fsm_event —— FSM 处理一个事件时的快照。字段：peer、event、
 * current_status、next_status、fd、vrf_id。 */
TRACEPOINT_EVENT(
	frr_bgp,
	fsm_event,
	TP_ARGS(struct peer *, peer, enum bgp_fsm_events, event, enum bgp_fsm_status, current_status, enum bgp_fsm_status, next_status, int, fd),
	TP_FIELDS(
		ctf_string(peer, PEER_HOSTNAME(peer))
		ctf_integer(enum bgp_fsm_events, event, event)
		ctf_integer(enum bgp_fsm_status, current_status, current_status)
		ctf_integer(enum bgp_fsm_status, next_status, next_status)
		ctf_integer(int, fd, fd)
		ctf_integer(uint32_t, vrf_id, peer->bgp->vrf_id)
	)
)

TRACEPOINT_LOGLEVEL(frr_bgp, fsm_event, TRACE_INFO)

/* 【事件】bgp_err_str —— 生成 BGP 错误字符串时上报。字段：peer_host、
 * peer_flags、location。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bgp_err_str,
	TP_ARGS(char *, peer_host,
	  uint64_t, peer_flags,
	  uint8_t, location),
	TP_FIELDS(
		ctf_string(peer, peer_host)
		ctf_integer(uint64_t, peer_flags, peer_flags)
		ctf_integer(uint8_t, location, location)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_err_str, TRACE_INFO)

/* ================= 分段 7：与 zebra 交互 及 杂项 =================
 * bgpd 需从 zebra 获取接口 / 地址 / 路由 / EVPN 等变更(名字里 *_zrecv 表示“从
 * zebra 收到”)，也需把状态同步给 zebra。这一组记录这些交互，以及接口操作
 * (bgp_ifp_oper)、重分发(redistribute)等。 */

/* 【事件】evpn_ignore_suppress_route —— EVPN 中因抑制(suppress)而忽略某路由。
 * 字段：prefix、peer。 */
TRACEPOINT_EVENT(
	frr_bgp,
	evpn_ignore_suppress_route,
	TP_ARGS(struct bgp_dest *, dest, struct peer *, peer),
	TP_FIELDS(
		ctf_string(prefix, bgp_dest_get_prefix_str(dest))
		ctf_string(peer, PEER_HOSTNAME(peer))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, evpn_ignore_suppress_route, TRACE_INFO)

/* 以下 bgp_zebra_* 组：bgpd 与 zebra 的各类交互(本地 IP 前缀、VXLAN flood 控制、
 * 路由通知归属、EVPN 通告类型、重复地址检测 DAD、RA 操作等)。字段趋于一致：
 * vrf_id 加若干业务标识。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bgp_zebra_process_local_ip_prefix_zrecv,
	TP_ARGS(struct prefix *, prefix, int, cmd, vrf_id_t, vrf_id),
	TP_FIELDS(
		ctf_array(unsigned char, prefix, prefix, sizeof(struct prefix))
		ctf_integer(int, cmd, cmd)
		ctf_integer(vrf_id_t, vrf_id, vrf_id)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_zebra_process_local_ip_prefix_zrecv, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	bgp_zebra_vxlan_flood_control,
	TP_ARGS(struct bgp *, bgp, enum vxlan_flood_control, flood_ctrl),
	TP_FIELDS(
		ctf_integer(vrf_id_t, vrf_id, bgp->vrf_id)
		ctf_integer(enum vxlan_flood_control, flood_ctrl, flood_ctrl)
		ctf_integer(uint8_t, flood_enabled, flood_ctrl == VXLAN_FLOOD_HEAD_END_REPL)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_zebra_vxlan_flood_control, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	bgp_zebra_route_notify_owner,
	TP_ARGS(enum zapi_route_notify_owner, route_status, struct bgp_dest *, dest, struct prefix *, prefix),
	TP_FIELDS(
		ctf_integer(enum zapi_route_notify_owner, route_status, route_status)
		ctf_integer(uint32_t, dest_flags, dest->flags)
		ctf_array(unsigned char, prefix, prefix, sizeof(struct prefix))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_zebra_route_notify_owner, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	bgp_zebra_evpn_advertise_type,
	TP_ARGS(struct bgp *, bgp, int, advertise, vni_t, vni, uint8_t, location),
	TP_FIELDS(
		ctf_integer(vrf_id_t, vrf_id, bgp->vrf_id)
		ctf_integer(int, advertise, advertise)
		ctf_integer(vni_t, vni, vni)
		ctf_integer(uint8_t, location, location)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_zebra_evpn_advertise_type, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	bgp_zebra_dup_addr_detection,
	TP_ARGS(struct bgp *, bgp),
	TP_FIELDS(
		ctf_integer(vrf_id_t, vrf_id, bgp->vrf_id)
		ctf_integer(bool, dup_addr_detect, bgp->evpn_info->dup_addr_detect)
		ctf_integer(uint32_t, dad_max_moves, bgp->evpn_info->dad_max_moves)
		ctf_integer(uint32_t, dad_time, bgp->evpn_info->dad_time)
		ctf_integer(bool, dad_freeze, bgp->evpn_info->dad_freeze)
		ctf_integer(uint32_t, dad_freeze_time, bgp->evpn_info->dad_freeze_time)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_zebra_dup_addr_detection, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	bgp_zebra_radv_operation,
	TP_ARGS(uint8_t, location, vrf_id_t, vrf_id, const char *, peer_host),
	TP_FIELDS(
		ctf_integer(uint8_t, location, location)
		ctf_integer(vrf_id_t, vrf_id, vrf_id)
		ctf_string(peer_host, peer_host)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_zebra_radv_operation, TRACE_INFO)

/* 【事件】bgp_ifp_oper —— 接口 UP/DOWN 等操作。字段：vrf_id、interface 名、
 * location。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bgp_ifp_oper,
	TP_ARGS(struct interface *, ifp, uint8_t, loc),
	TP_FIELDS(
		ctf_integer(vrf_id_t, vrf_id, ifp->vrf->vrf_id)
		ctf_string(interface, ifp->name)
		ctf_integer(uint8_t, location, loc)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_ifp_oper, TRACE_INFO)

/* 【事件】bgp_redistribute_add_zrecv / _delete_zrecv —— 从其它协议重分发进来
 * 的路由增删。字段：vrf_id、prefix、ifindex、nexthop 类型、distance、blackhole
 * 类型、metric、type(来源协议)、instance、tag 等。 */
TRACEPOINT_EVENT(
	frr_bgp,
	bgp_redistribute_add_zrecv,
	TP_ARGS(struct bgp *, bgp, struct prefix *, pfx, ifindex_t, ifindex,
		enum nexthop_types_t, nhtype, uint8_t, distance,
		enum blackhole_type, bhtype, uint32_t, metric,
		uint8_t, type,
		unsigned short, instance,
		route_tag_t, tag),
	TP_FIELDS(
		ctf_integer(uint32_t, vrf_id, bgp->vrf_id)
		ctf_array(unsigned char, prefix, pfx, sizeof(struct prefix))
		ctf_integer(ifindex_t, ifindex, ifindex)
		ctf_integer(enum nexthop_types_t, nhtype, nhtype)
		ctf_integer(uint8_t, distance, distance)
		ctf_integer(enum blackhole_type, bhtype, bhtype)
		ctf_integer(uint32_t, metric, metric)
		ctf_integer(uint8_t, type, type)
		ctf_integer(unsigned short, instance, instance)
		ctf_integer(route_tag_t, tag, tag)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_redistribute_add_zrecv, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	bgp_redistribute_delete_zrecv,
	TP_ARGS(struct bgp *, bgp, struct prefix *, pfx, uint8_t, type,
		unsigned short, instance),
	TP_FIELDS(
		ctf_integer(uint32_t, vrf_id, bgp->vrf_id)
		ctf_array(unsigned char, prefix, pfx, sizeof(struct prefix))
		ctf_integer(uint8_t, type, type)
		ctf_integer(unsigned short, instance, instance)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, bgp_redistribute_delete_zrecv, TRACE_INFO)

/* 【事件】interface_address_oper_zrecv —— 接口地址变更(来自 zebra)。字段：
 * vrf_id、ifname、address、location。 */
TRACEPOINT_EVENT(
	frr_bgp,
	interface_address_oper_zrecv,
	TP_ARGS(vrf_id_t, vrf_id, char *, name,
		struct prefix *, address,
		uint8_t, loc),
	TP_FIELDS(
		ctf_integer(int, vrf_id, vrf_id)
		ctf_string(ifname, name)
		ctf_array(unsigned char, address, address, sizeof(struct prefix))
		ctf_integer(uint8_t, location, loc)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, interface_address_oper_zrecv, TRACE_INFO)

/* 【事件】router_id_update_zrecv —— 收到 router-id 更新。字段：vrf_id、
 * router_id(以 prefix 编码)。 */
TRACEPOINT_EVENT(
	frr_bgp,
	router_id_update_zrecv,
	TP_ARGS(vrf_id_t, vrf_id, struct prefix *, router_id),
	TP_FIELDS(
		ctf_integer(int, vrf_id, vrf_id)
		ctf_array(unsigned char, router_id, router_id, sizeof(struct prefix))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, router_id_update_zrecv, TRACE_INFO)

/* ================= 分段 8：更新组 / 更新子组(update-group)管理 =================
 * BGP 把“出向策略相同、能力相同”的邻居归入同一 update-group，又进一步分出
 * subgroup 以便一次性打包出向更新，显著降低编码与内存开销。这一组事件记录更新
 * 组 / 子组的创建删除、成员增减、合并(merge)、拆分(split)等拓扑变化，便于分析
 * 出向更新效率与分组抖动。字段多为 updgrp_id / subgroup_id / peer 及其 afi /
 * safi / vrf 等标识。 */

/* 【事件】ug_bgp_aggregate_install —— 为聚合路由安装出向更新。字段：prefix、
 * afi/safi、origin。 */
TRACEPOINT_EVENT(
	frr_bgp,
	ug_bgp_aggregate_install,
	TP_ARGS(const struct prefix *, prefix, uint8_t, afi, uint8_t, safi,
		uint8_t, origin),
	TP_FIELDS(
		ctf_array(unsigned char, prefix, prefix, sizeof(struct prefix))
		ctf_integer(uint8_t, afi, afi)
		ctf_integer(uint8_t, safi, safi)
		ctf_integer(uint8_t, origin, origin)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, ug_bgp_aggregate_install, TRACE_INFO)

/* 以下 ug_* 组：update-group / subgroup 的拓扑与成员变化(创建删除、增删 peer、
 * 合并、按 peer 拆分)。字段：operation、updgrp_id、subgroup_id、peer 等。 */
TRACEPOINT_EVENT(
	frr_bgp,
	ug_create_delete,
	TP_ARGS(uint8_t, operation, uint64_t, updgrp_id),
	TP_FIELDS(
		ctf_integer(uint8_t, operation, operation)
		ctf_integer(uint64_t, updgrp_id, updgrp_id)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, ug_create_delete, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	ug_subgroup_create_delete,
	TP_ARGS(uint8_t, operation, uint64_t, updgrp_id, uint64_t, subgroup_id),
	TP_FIELDS(
		ctf_integer(uint8_t, operation, operation)
		ctf_integer(uint64_t, updgrp_id, updgrp_id)
		ctf_integer(uint64_t, subgroup_id, subgroup_id)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, ug_subgroup_create_delete, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	ug_subgroup_add_remove_peer,
	TP_ARGS(uint8_t, operation, char *, peer_host, uint8_t, pafi, uint8_t, psafi, uint32_t, pafid,
		uint64_t, subgroup_id, uint32_t, peer_count),
	TP_FIELDS(
		ctf_integer(uint8_t, operation, operation)
		ctf_string(peer, peer_host)
		ctf_integer(uint8_t, pafi, pafi)
		ctf_integer(uint8_t, psafi, psafi)
		ctf_integer(uint32_t, pafid, pafid)
		ctf_integer(uint64_t, subgroup_id, subgroup_id)
		ctf_integer(uint32_t, peer_count, peer_count)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, ug_subgroup_add_remove_peer, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	ug_subgroup_merge,
	TP_ARGS(uint64_t, updgrp_id, uint64_t, subgroup_id, uint32_t, peer_count,
		uint64_t, target_updgrp_id, uint64_t, target_subgroup_id, const char *, reason),
	TP_FIELDS(
		ctf_integer(uint64_t, updgrp_id, updgrp_id)
		ctf_integer(uint64_t, subgroup_id, subgroup_id)
		ctf_integer(uint32_t, peer_count, peer_count)
		ctf_integer(uint64_t, target_updgrp_id, target_updgrp_id)
		ctf_integer(uint64_t, target_subgroup_id, target_subgroup_id)
		ctf_string(reason, reason)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, ug_subgroup_merge, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	ug_subgroup_split_peer,
	TP_ARGS(uint64_t, old_updgrp_id, uint64_t, old_subgroup_id, uint32_t, old_peer_count,
		char *, peer_host, uint64_t, new_updgrp_id, uint64_t, new_subgroup_id),
	TP_FIELDS(
		ctf_integer(uint64_t, old_updgrp_id, old_updgrp_id)
		ctf_integer(uint64_t, old_subgroup_id, old_subgroup_id)
		ctf_integer(uint32_t, old_peer_count, old_peer_count)
		ctf_string(peer, peer_host)
		ctf_integer(uint64_t, new_updgrp_id, new_updgrp_id)
		ctf_integer(uint64_t, new_subgroup_id, new_subgroup_id)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, ug_subgroup_split_peer, TRACE_INFO)

/* ================= 分段 9：出向更新处理(update subgroup 流水线) =================
 * 这一大段是 BGP 生成 / 发送 UPDATE 的主流水线埋点，事件名以 upd_ 开头。涵盖：
 *   - 通告路由：对端 / 子组通告(upd_peer_af_announce_route*)、全部路由通告、按
 *     策略变化触发通告、合并(coalesce)计时器到期触发通告等；
 *   - 过滤与拒绝：未识别 AFI/SAFI、未知属性、格式错误属性、因“铁钉(martian)”
 *     丢弃、自 MAC 拒绝、无标签前缀、AS 环路过滤、出向 route-map 过滤等；
 *   - EVPN：EVPN 表项安装、EVPN 表重扫等；
 *   - 出向报文细节：发送 MP_UNREACH、发送撤销明细、default-originate 的发送 /
 *     撤销等。
 * 字段多为 updgrp_id / subgroup_id / peer / prefix / afi / safi 及少量业务标识。
 * 下面按子主题给若干分组说明，同类事件不逐一展开。 */

/* 【事件】upd_peer_af_announce_route —— 开始向某对端的某(afi,safi)通告路由。
 * 字段：updgrp_id、subgrp_id、peer 主机名。带 _combined 后缀的版本额外带
 * peer_count(合并通告的对端数)。 */
TRACEPOINT_EVENT(
	frr_bgp,
	upd_peer_af_announce_route,
	TP_ARGS(uint64_t, updgrp_id, uint64_t, subgrp_id, char *, peer_host),
	TP_FIELDS(
		ctf_integer(uint64_t, updgrp_id, updgrp_id)
		ctf_integer(uint64_t, subgrp_id, subgrp_id)
		ctf_string(peer, peer_host)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_peer_af_announce_route, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_peer_af_announce_route_combined,
	TP_ARGS(uint64_t, updgrp_id, uint64_t, subgrp_id, char *, peer_host, uint32_t, peer_count),
	TP_FIELDS(
		ctf_integer(uint64_t, updgrp_id, updgrp_id)
		ctf_integer(uint64_t, subgrp_id, subgrp_id)
		ctf_string(peer, peer_host)
		ctf_integer(uint32_t, peer_count, peer_count)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_peer_af_announce_route_combined, TRACE_INFO)

/* 【事件】upd_ignoring_non_ll_nexthop —— 忽略非链路本地(link-local)下一跳。
 * 字段：peer、nh_global、nh_local(两种下一跳的字符串表示)。相邻的
 * upd_rmac_is_self_mac / upd_attr_type_unsupported 分别记录“RMAC 等于自身 MAC”
 * 与“遇到不支持的属性类型”。 */
TRACEPOINT_EVENT(
	frr_bgp,
	upd_ignoring_non_ll_nexthop,
	TP_ARGS(char *, peer_host, const char *, nh_global, const char *, nh_local),
	TP_FIELDS(
		ctf_string(peer, peer_host)
		ctf_string(nh_global, nh_global)
		ctf_string(nh_local, nh_local)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_ignoring_non_ll_nexthop, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_rmac_is_self_mac,
	TP_ARGS(struct ethaddr *, rmac),
	TP_FIELDS(
		ctf_array(unsigned char, rmac, rmac, sizeof(struct ethaddr))
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_rmac_is_self_mac, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_attr_type_unsupported,
	TP_ARGS(uint8_t, loc, char *, peer_host, uint8_t, type),
	TP_FIELDS(
		ctf_integer(uint8_t, attr, loc)
		ctf_string(peer, peer_host)
		ctf_integer(uint8_t, type, type)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_attr_type_unsupported, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_unknown_attr_rcvd,
	TP_ARGS(char *, peer_host, uint8_t, type, uint16_t, len),
	TP_FIELDS(
		ctf_string(peer, peer_host)
		ctf_integer(uint8_t, type, type)
		ctf_integer(uint16_t, length, len)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_unknown_attr_rcvd, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_mp_unrecognized_afi_safi,
	TP_ARGS(char *, peer_host, const char *, afi, const char *, safi, uint8_t, loc),
	TP_FIELDS(
		ctf_string(peer, peer_host)
		ctf_string(afi, afi)
		ctf_string(safi, safi)
		ctf_integer(uint8_t, loc, loc)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_mp_unrecognized_afi_safi, TRACE_INFO)

/* 以下 upd_* 组：EVPN 与“铁钉(martian)”相关。如 upd_evpn_route_entry(EVPN 表项
 * 安装 / 卸载)、upd_prefix_denied_due_to_self_mac(因自 MAC 拒绝)、
 * upd_malformed_attr(格式错误属性)、upd_attr_discarded_due_to_martian /
 * upd_evpn_martian_change(因铁钉丢弃 / 变更)、upd_mac_rescan_evpn_table
 * (重扫 EVPN 表)等。 */
TRACEPOINT_EVENT(
	frr_bgp,
	upd_evpn_route_entry,
	TP_ARGS(uint8_t, install, uint32_t, vrf_id, char *, pfx, uint32_t, vni),
	TP_FIELDS(
		ctf_integer(uint8_t, install, install)
		ctf_integer(uint32_t, vrf_id, vrf_id)
		ctf_string(prefix, pfx)
		ctf_integer(uint32_t, vni, vni)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_evpn_route_entry, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_prefix_denied_due_to_self_mac,
	TP_ARGS(uint32_t, vrf_id, char *, pfx, char *, attr),
	TP_FIELDS(
		ctf_integer(uint32_t, vrf_id, vrf_id)
		ctf_string(prefix, pfx)
		ctf_string(attr, attr)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_prefix_denied_due_to_self_mac, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_malformed_attr,
	TP_ARGS(char *, peer, char *, attr),
	TP_FIELDS(
		ctf_string(peer, peer)
		ctf_string(attr, attr)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_malformed_attr, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_attr_discarded_due_to_martian,
	TP_ARGS(uint32_t, vrf_id, char *, pfx, char *, attr, const char *, martian),
	TP_FIELDS(
		ctf_integer(uint32_t, vrf_id, vrf_id)
		ctf_string(prefix, pfx)
		ctf_string(attr, attr)
		ctf_string(martian_type, martian)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_attr_discarded_due_to_martian, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_evpn_martian_change,
	TP_ARGS(char *, peer, const char *, martian, uint8_t, soft_in),
	TP_FIELDS(
		ctf_string(peer, peer)
		ctf_string(martian_type, martian)
		ctf_integer(uint32_t, soft_reconfig_in, soft_in)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_evpn_martian_change, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_skip_update_of_removed_prefix,
	TP_ARGS(char *, peer, char *, pfx),
	TP_FIELDS(
		ctf_string(peer, peer)
		ctf_string(prefix, pfx)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_skip_update_of_removed_prefix, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_mac_rescan_evpn_table,
	TP_ARGS(char *, peer, uint8_t, soft_in),
	TP_FIELDS(
		ctf_string(peer, peer)
		ctf_integer(uint32_t, soft_reconfig_in, soft_in)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_mac_rescan_evpn_table, TRACE_INFO)

/* 以下 upd_* 组：出向路由在打包前被各种原因过滤 / 拒绝。例如
 * upd_prefix_filtered_no_label(前缀无标签)、upd_comm_filter_check_failed(团体
 * 属性过滤检查失败)、upd_prefix_filtered_due_to(因 loc 所指原因过滤)、
 * upd_as_path_loop_filter(AS 环路过滤)、upd_outbound_route_map_filter(出向
 * route-map 过滤)等。字段：peer、prefix 及少量定位 / 业务字段。 */
TRACEPOINT_EVENT(
	frr_bgp,
	upd_prefix_filtered_no_label,
	TP_ARGS(uint64_t, updgrp_id, uint64_t, subgrp_id, char *, pfx),
	TP_FIELDS(
		ctf_integer(uint64_t, updgrp_id, updgrp_id)
		ctf_integer(uint64_t, subgrp_id, subgrp_id)
		ctf_string(prefix, pfx)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_prefix_filtered_no_label, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_comm_filter_check_failed,
	TP_ARGS(char *, pfx),
	TP_FIELDS(
		ctf_string(prefix, pfx)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_comm_filter_check_failed, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_prefix_filtered_due_to,
	TP_ARGS(uint8_t, loc, char *, peer, char *, pfx),
	TP_FIELDS(
		ctf_integer(uint64_t, location, loc)
		ctf_string(peer, peer)
		ctf_string(prefix, pfx)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_prefix_filtered_due_to, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_as_path_loop_filter,
	TP_ARGS(char *, peer, char *, pfx, uint32_t, as, uint8_t, confed),
	TP_FIELDS(
		ctf_string(peer, peer)
		ctf_string(prefix, pfx)
		ctf_integer(uint32_t, as, as)
		ctf_integer(uint8_t, confed, confed)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_as_path_loop_filter, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_outbound_route_map_filter,
	TP_ARGS(char *, peer, char *, pfx, char *, routemap),
	TP_FIELDS(
		ctf_string(peer, peer)
		ctf_string(prefix, pfx)
		ctf_string(routemap, routemap)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_outbound_route_map_filter, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_announce_nh_self,
	TP_ARGS(const char *, family),
	TP_FIELDS(
		ctf_string(family, family)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_announce_nh_self, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_extcomm_change,
	TP_ARGS(const char *, existing, const char *, new),
	TP_FIELDS(
		ctf_string(existing_extcomm, existing)
		ctf_string(new_extcomm, new)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_extcomm_change, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_announce_route_on_policy_change,
	TP_ARGS(uint8_t, default_rt, uint64_t, updgrp_id, uint64_t, subgrp_id, const char *, policy, uint8_t, policy_type),
	TP_FIELDS(
		ctf_integer(uint8_t, default_route, default_rt)
		ctf_integer(uint64_t, updgrp_id, updgrp_id)
		ctf_integer(uint64_t, subgrp_id, subgrp_id)
		ctf_string(policy, policy)
		ctf_integer(uint8_t, policy_type, policy_type)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_announce_route_on_policy_change, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_announce_route_on_coalesce_timer_expiry,
	TP_ARGS(uint64_t, updgrp_id, uint64_t, subgrp_id, uint32_t, coalesce_time),
	TP_FIELDS(
		ctf_integer(uint64_t, updgrp_id, updgrp_id)
		ctf_integer(uint64_t, subgrp_id, subgrp_id)
		ctf_integer(uint32_t, coalesce_time_ms, coalesce_time)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_announce_route_on_coalesce_timer_expiry, TRACE_INFO)

/* 以下 upd_* 组：出向报文发送细节。upd_announce_all_routes(通告全部路由)、
 * upd_send_mp_unreach(发送 MP_UNREACH)、upd_send_withdraw_details(发送撤销明细)、
 * upd_send_update_default_originate / upd_send_withdraw_default_originate
 * (default-originate 的发送与撤销)。字段：updgrp_id、subgrp_id 及报文 / 计数等。 */
TRACEPOINT_EVENT(
	frr_bgp,
	upd_announce_all_routes,
	TP_ARGS(uint64_t, updgrp_id, uint64_t, subgrp_id),
	TP_FIELDS(
		ctf_integer(uint64_t, updgrp_id, updgrp_id)
		ctf_integer(uint64_t, subgrp_id, subgrp_id)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_announce_all_routes, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_send_mp_unreach,
	TP_ARGS(uint64_t, updgrp_id, uint64_t, subgrp_id, const char *, afi, const char *, safi),
	TP_FIELDS(
		ctf_integer(uint64_t, updgrp_id, updgrp_id)
		ctf_integer(uint64_t, subgrp_id, subgrp_id)
		ctf_string(afi, afi)
		ctf_string(safi, safi)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_send_mp_unreach, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_send_withdraw_details,
	TP_ARGS(uint64_t, updgrp_id, uint64_t, subgrp_id, uint32_t, length, uint32_t, num_pfx),
	TP_FIELDS(
		ctf_integer(uint64_t, updgrp_id, updgrp_id)
		ctf_integer(uint64_t, subgrp_id, subgrp_id)
		ctf_integer(uint32_t, length, length)
		ctf_integer(uint32_t, prefix_count, num_pfx)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_send_withdraw_details, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_send_update_default_originate,
	TP_ARGS(uint64_t, updgrp_id, uint64_t, subgrp_id, char *, tx_buf, char *, attr_str),
	TP_FIELDS(
		ctf_integer(uint64_t, updgrp_id, updgrp_id)
		ctf_integer(uint64_t, subgrp_id, subgrp_id)
		ctf_string(tx_buf, tx_buf)
		ctf_string(attr, attr_str)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_send_update_default_originate, TRACE_INFO)

TRACEPOINT_EVENT(
	frr_bgp,
	upd_send_withdraw_default_originate,
	TP_ARGS(uint64_t, updgrp_id, uint64_t, subgrp_id, char *, tx_buf),
	TP_FIELDS(
		ctf_integer(uint64_t, updgrp_id, updgrp_id)
		ctf_integer(uint64_t, subgrp_id, subgrp_id)
		ctf_string(tx_buf, tx_buf)
	)
)
TRACEPOINT_LOGLEVEL(frr_bgp, upd_send_withdraw_default_originate, TRACE_INFO)

/* 恢复 clang-format：下面的头包含交给自动格式化。 */
/* clang-format on */

/* 【多读机制收尾】LTTng 用这个头在“多读”遍里展开所有 TRACEPOINT_EVENT，生成与
 * 各事件配套的探针桩。它内部会定义 TRACEPOINT_HEADER_MULTI_READ 并再次 include
 * 本文件(即上面 TRACEPOINT_INCLUDE 所指的 "bgpd/bgp_trace.h")，故本头必须在
 * 末尾包含它。 */
#include <lttng/tracepoint-event.h>

/* HAVE_LTTNG 总开关结束。 */
#endif /* HAVE_LTTNG */

/* 【包含卫结束】 */
#endif /* _BGP_TRACE_H */
