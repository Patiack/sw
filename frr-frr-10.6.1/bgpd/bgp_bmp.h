// SPDX-License-Identifier: GPL-2.0-or-later
/* BMP support.
 * Copyright (C) 2018 Yasuhiro Ohara
 * Copyright (C) 2019 David Lamparter for NetDEF, Inc.
 */

/* 【文件导读】bgp_bmp.h —— BGP Monitoring Protocol（RFC 7854 / RFC 9069）头文件
 *
 * 本文件解决什么问题：
 *   BMP 是一个"单向旁路"遥测协议。它把 BGP 邻居的状态变化、收到的路由、以及
 *   路由策略处理前后的内容，镜像给一个（或一组）外部监控服务器（monitoring
 *   station）。BMP 只读地观察 BGP，绝不参与 BGP 的选路与转发决策，因此 BMP
 *   的存在不应改变 BGP 自身的协议行为。
 *
 * 为什么这样设计：
 *   1) BMP 会话与 BGP 对等体会话完全解耦 —— 监控端不建立 BGP 邻居，它只是
 *      一个 TCP 客户端 / 服务端，被动接收二进制 BMP 消息。
 *   2) BMP 采用"生产者—队列—发送线程"模型：BGP 主线程只把待发消息压入队列
 *      （本文件描述的各种 queue 结构），真正的 socket 写由 pullwr 异步完成，
 *      避免阻塞 BGP 协议处理。
 *   3) 配置相同的 listener / active 连接会被归并到同一个 bmp_targets，使多个
 *      监控会话共享同一批队列项，节省内存与遍历开销。
 *
 * 关键数据结构（从配置到运行时）：
 *   - struct bmp_targets：一份"给谁发、发什么、发多少"的配置集合，是 BMP 的
 *     核心配置对象，隶属于某个 struct bgp 实例。它持有 listener、active 连接、
 *     被导入的其它 BGP 实例集合，以及两条路由监控队列。
 *   - struct bmp_listener / struct bmp_active：被动监听 / 主动外连两种建链方式。
 *   - struct bmp：一条已经建立（established）的 BMP 会话的运行时状态。
 *   - struct bmp_queue_entry：route monitoring 队列项，键为
 *     (bgp, afi, safi, prefix, peerid)，同一键最多一项，实现"旧消息可被新消息
 *     替换"的合并语义，队列长度受 BGP 表大小约束。
 *   - struct bmp_mirrorq：route mirroring 队列项，存放逐字节原始 BGP PDU。
 *   - struct bmp_bgp_peer / struct bmp_bgp：分别记录每个 BGP peer 保存的 OPEN
 *     报文，以及每个 BGP 实例挂载的 BMP 状态（含 mirror 队列）。
 *
 * 推荐阅读顺序：
 *   1) 先读本文件顶部的消息类型宏（BMP_TYPE_*）建立消息分类概念；
 *   2) 再读 struct bmp_targets 及其下辖的队列结构，理解"给谁发、发什么"；
 *   3) 然后进入 bgp_bmp.c：bmp_send_initiation / bmp_per_peer_hdr /
 *      bmp_peerstate 理解消息封装；
 *   4) 最后读 bmp_monitor（route monitoring）与 bmp_mirror_packet
 *      （route mirroring），对比二者的本质区别。
 */

#ifndef _BGP_BMP_H_
#define _BGP_BMP_H_

#include "zebra.h"
#include "typesafe.h"
#include "pullwr.h"
#include "qobj.h"
#include "resolver.h"

#define BMP_VERSION_3	3

#define BMP_LENGTH_POS  1

/* BMP 消息类型（BMP common header 的 type 字段）：
 *   ROUTE_MONITORING  —— 路由监控：把路由以"加工后"的形式上报，可来自策略前
 *                        （pre-policy）、策略后（post-policy）或本地 RIB
 *                        （loc-rib）。同一前缀的过时更新可被丢弃。
 *   STATISTICS_REPORT —— 统计上报：周期性汇报各 peer 的计数（被拒前缀、环路、
 *                        Adj-RIB-In / Loc-RIB 规模等）。
 *   PEER_DOWN / PEER_UP —— 邻居状态通知：携带 per-peer header，UP 时还附带
 *                        本端 / 对端 OPEN 报文。
 *   INITIATION        —— 会话初始化：携带 sysName / sysDescr 等本端信息。
 *   TERMINATION       —— 会话结束（本实现未主动发送）。
 *   ROUTE_MIRRORING   —— 逐字节镜像：把收到的原始 BGP PDU 原样转发，不经过策略，
 *                        也不做按键去重。
 */
/* BMP message types */
#define BMP_TYPE_ROUTE_MONITORING       0
#define BMP_TYPE_STATISTICS_REPORT      1
#define BMP_TYPE_PEER_DOWN_NOTIFICATION 2
#define BMP_TYPE_PEER_UP_NOTIFICATION   3
#define BMP_TYPE_INITIATION             4
#define BMP_TYPE_TERMINATION            5
#define BMP_TYPE_ROUTE_MIRRORING        6

#define BMP_READ_BUFSIZ	1024

/* bmp->state：BMP 会话自身的状态机（与 BGP 的 FSM 无关）。
 *   BMP_None   —— 未初始化（值为 0，状态机跳过了 1）。
 *   BMP_PeerUp —— 会话已建立，正在发送 peer up / VRF up 等初始消息。
 *   BMP_Run    —— 进入正常发送阶段（route monitoring / mirroring / sync）。
 */
/* bmp->state */
#define BMP_None        0
#define BMP_PeerUp      2
#define BMP_Run         3

/* This one is for BMP Route Monitoring messages, i.e. delivering updates
 * in somewhat processed (as opposed to fully raw, see mirroring below) form.
 * RFC explicitly says that we can skip old updates if we haven't sent them out
 * yet and another newer update for the same prefix arrives.
 *
 * So, at most one of these can exist for each (bgp, afi, safi, prefix, peerid)
 * tuple; if some prefix is "re-added" to the queue, the existing entry is
 * instead moved to the end of the queue.  This ensures that the queue size is
 * bounded by the BGP table size.
 *
 * bmp_qlist is the queue itself while bmp_rbtree is used to efficiently check
 * whether a tuple is already on the list.  The queue is maintained per
 * bmp_target.
 *
 * refcount = number of "struct bmp *" whose queue position is before this
 * entry, i.e. number of BMP sessions where we still want to send this out.
 * Decremented on send so we know when we're done with an entry (i.e. this
 * always happens from the front of the queue.)
 */

/* 【route monitoring 队列】DLIST 双向链表 + RBTREE 红黑树共同实现同一键去重的
 * 发送队列：
 *   - bmp_qlist：按到达顺序排队，是真正的发送顺序；
 *   - bmp_rbtree：按键快速查找"该条目是否已在队列中"。
 * bmp_queue_entry 就是队列元素，键为 (bgp, afi, safi, prefix, peerid)；EVPN / VPN
 * 场景下还带上 RD（结构体里的 rd 字段）一并作为键。
 */
PREDECL_DLIST(bmp_qlist);
PREDECL_RBTREE_UNIQ(bmp_rbtree);

struct bmp_queue_entry {
	struct bmp_qlist_item bli;
	struct bmp_rbtree_item bhi;

	struct prefix p;
	uint64_t peerid;
	afi_t afi;
	safi_t safi;

	size_t refcount;

	/* initialized only for L2VPN/EVPN (S)AFIs */
	struct prefix_rd rd;
};

/* This is for BMP Route Mirroring, which feeds fully raw BGP PDUs out to BMP
 * receivers.  So, this goes directly off packet RX/TX handling instead of
 * grabbing bits from tables.
 *
 * There is *one* queue for each "struct bgp *" where we throw everything on,
 * with a size limit.  Refcount works the same as for monitoring above.
 */

/* 【route mirroring 队列】bmp_mirrorq 直接保存一段原始 BGP PDU 字节（柔性数组成员
 * data[0] 保存 len 字节）。它是"逐字节镜像"的载体：与上面的 monitoring 队列不同，
 * 这里不做按键合并 / 去重，收到的每个报文都各自入队。每个 struct bgp 实例拥有
 * 一条 mirrorq（见 struct bmp_bgp），并受 mirror_qsizelimit 限制。
 */
PREDECL_LIST(bmp_mirrorq);

struct bmp_mirrorq {
	struct bmp_mirrorq_item bmi;

	size_t refcount;
	uint64_t peerid;
	struct timeval tv;

	size_t len;
	uint8_t data[0];
};

enum bmp_afi_state {
	BMP_AFI_INACTIVE = 0,
	BMP_AFI_NEEDSYNC,
	BMP_AFI_SYNC,
	BMP_AFI_LIVE,
};

PREDECL_LIST(bmp_session);

struct bmp_active;
struct bmp_targets;

/* struct bmp：一条已经建立的 BMP 会话的运行时状态。
 * state 字段即上面的 BMP_PeerUp / BMP_Run；afistate 记录每个 (afi,safi) 的同步进度
 * （见 enum bmp_afi_state）。locrib_queuepos / queuepos / mirrorpos 是本会话在三条
 * 共享队列中的"读游标"，必须与队列项的 refcount 同步维护。
 */
/* an established BMP session to a peer */
struct bmp {
	struct bmp_session_item bsi;
	struct bmp_targets *targets;
	struct bmp_active *active;

	int socket;
	char remote[SU_ADDRSTRLEN + 6];
	struct event *t_read;

	struct pullwr *pullwr;

	int state;

	/* queue positions must remain synced with refcounts in the items.
	 * Whenever appending a queue item, we need to know the correct number
	 * of "struct bmp *" that want it, and when moving these positions
	 * ahead we need to make sure that refcount is decremented.  Also, on
	 * disconnects we need to walk the queue and drop our reference.
	 */
	struct bmp_queue_entry *locrib_queuepos;
	struct bmp_queue_entry *queuepos;
	struct bmp_mirrorq *mirrorpos;
	bool mirror_lost;

	/* enum BMP_AFI_* */
	uint8_t afistate[AFI_MAX][SAFI_MAX];

	/* counters for the various BMP packet types */
	uint64_t cnt_update, cnt_mirror;
	/* number of times this peer wasn't fast enough in consuming the
	 * mirror queue
	 */
	uint64_t cnt_mirror_overruns;
	struct timeval t_up;

	/* synchronization / startup works by repeatedly finding the next
	 * table entry, the sync* fields note down what we sent last
	 */
	struct prefix syncpos;
	struct bgp_dest *syncrdpos;
	uint64_t syncpeerid;
	afi_t syncafi;
	safi_t syncsafi;
	struct bgp *sync_bgp;
};

/* config & state for an active outbound connection.  When the connection
 * succeeds, "bmp" is set up.
 */

/* struct bmp_active：主动外连（我作为客户端去连监控服务器）的配置与状态。
 * 通过 resolver 解析 hostname 得到一组地址 addrs[]，逐个尝试连接；成功后建立
 * struct bmp 会话。minretry / maxretry 控制重连退避。
 */
PREDECL_SORTLIST_UNIQ(bmp_actives);

#define BMP_DFLT_MINRETRY	30000
#define BMP_DFLT_MAXRETRY	720000

struct bmp_active {
	struct bmp_actives_item bai;
	struct bmp_targets *targets;
	struct bmp *bmp;

	char *hostname;
	int port;
	unsigned minretry, maxretry;
	char *ifsrc;
	union sockunion addrsrc;

	struct resolver_query resq;

	unsigned curretry;
	unsigned addrpos, addrtotal;
	union sockunion addrs[8];
	int socket;
	const char *last_err;
	struct event *t_timer, *t_read, *t_write;
};

/* config & state for passive / listening sockets */
/* struct bmp_listener：被动监听（我作为服务端等待监控服务器连入）的配置与状态。 */
PREDECL_SORTLIST_UNIQ(bmp_listeners);

struct bmp_listener {
	struct bmp_listeners_item bli;

	struct bmp_targets *targets;

	union sockunion addr;
	int port;

	struct event *t_accept;
	int sock;
};

/* config for imported bgp instances */
PREDECL_SORTLIST_UNIQ(bmp_imported_bgps);

/* bmp_targets - plural since it may contain multiple bmp_listener &
 * bmp_active items.  If they have the same config, BMP session should be
 * put in the same targets since that's a bit more effective.
 */
/* struct bmp_targets：BMP 的核心配置对象，回答"给谁发、发多少、发什么"。
 *   - listeners / actives：两组建链对象（被动 / 主动）；
 *   - afimon[afi][safi]：本 target 在每种 AFI/SAFI 上要发哪些监控内容，由
 *     BMP_MON_PREPOLICY / BMP_MON_POSTPOLICY / BMP_MON_LOC_RIB 位掩码组合；
 *   - mirror：是否发送逐字节镜像；
 *   - imported_bgps：额外要镜像进来的其它 BGP 实例（VRF / view）；
 *   - updhash / updlist 与 locupdhash / locupdlist：两条 monitoring 队列
 *     （普通策略前 / 后队列，以及 loc-rib 专用队列）。
 */
PREDECL_SORTLIST_UNIQ(bmp_targets);

struct bmp_targets {
	struct bmp_targets_item bti;

	struct bmp_bgp *bmpbgp;
	struct bgp *bgp;
	bool bgp_request_sync[AFI_MAX][SAFI_MAX];
	char *name;

	struct bmp_listeners_head listeners;

	char *acl_name;
	char *acl6_name;
#define BMP_STAT_DEFAULT_TIMER	60000
	int stat_msec;

	/* only supporting:
	 * - IPv4 / unicast & multicast & VPN
	 * - IPv6 / unicast & multicast & VPN
	 * - L2VPN / EVPN
	 */
#define BMP_MON_PREPOLICY	(1 << 0)
#define BMP_MON_POSTPOLICY	(1 << 1)
#define BMP_MON_LOC_RIB (1 << 2)

	uint8_t afimon[AFI_MAX][SAFI_MAX];
	bool mirror;

	struct bmp_actives_head actives;

	struct event *t_stats;
	struct bmp_session_head sessions;

	struct bmp_rbtree_head updhash;
	struct bmp_qlist_head updlist;

	struct bmp_rbtree_head locupdhash;
	struct bmp_qlist_head locupdlist;

	struct bmp_imported_bgps_head imported_bgps;

	uint64_t cnt_accept, cnt_aclrefused;

	bool stats_send_experimental;

	QOBJ_FIELDS;
};
DECLARE_QOBJ_TYPE(bmp_targets);

/* per struct peer * data.  Lookup by peer->qobj_node.nid, created on demand,
 * deleted in peer_backward hook. */
/* struct bmp_bgp_peer：每个 BGP peer 的 BMP 附属数据，按 peer 的 qobj nid 索引。
 * 保存该 peer 收发的 OPEN 报文副本（open_rx / open_tx），用于在 peer up 通知中
 * 回填本端 / 对端 OPEN。
 */
PREDECL_HASH(bmp_peerh);

struct bmp_bgp_peer {
	struct bmp_peerh_item bpi;

	uint64_t peerid;
	/* struct peer *peer; */

	uint8_t *open_rx;
	size_t open_rx_len;

	uint8_t *open_tx;
	size_t open_tx_len;
};

/* per struct bgp * data */
/* struct bmp_bgp：每个 BGP 实例的 BMP 状态，按 struct bgp 指针索引。持有该实例
 * 下挂的全部 targets，以及该实例的一条 route mirroring 队列（mirrorq）及其容量
 * 统计 / 限额。
 */
PREDECL_HASH(bmp_bgph);

#define BMP_PEER_DOWN_NO_RELEVANT_EVENT_CODE 0x00

enum bmp_vrf_state {
	vrf_state_down = -1,
	vrf_state_unknown = 0,
	vrf_state_up = 1,
};

struct bmp_imported_bgp {
	struct bmp_imported_bgps_item bib;
	struct bmp_targets *targets;
	char *name;
	enum bmp_vrf_state vrf_state;
	bool bgp_request_sync[AFI_MAX][SAFI_MAX];
};

struct bmp_bgp {
	struct bmp_bgph_item bbi;

	struct bgp *bgp;

	enum bmp_vrf_state vrf_state;

	struct bmp_targets_head targets;

	struct bmp_mirrorq_head mirrorq;
	size_t mirror_qsize, mirror_qsizemax;

	size_t mirror_qsizelimit;
};

extern bool bmp_bgp_update_vrf_status(enum bmp_vrf_state *vrf_state, struct bgp *bgp,
				      enum bmp_vrf_state force);

/* peer down 通知的"原因码"（RFC 7854 第 10.8 节 + RFC 9069 第 8.4 节）：指明该
 * peer 为何 down，供监控端还原邻居震荡原因。
 *   BMP_PEERDOWN_LOCAL_NOTIFY  —— 本端已发 NOTIFICATION 后 down；
 *   BMP_PEERDOWN_LOCAL_FSM     —— 本端 FSM 迁移导致 down；
 *   BMP_PEERDOWN_REMOTE_NOTIFY —— 收到远端 NOTIFICATION；
 *   BMP_PEERDOWN_REMOTE_CLOSE  —— 远端直接关闭 TCP；
 *   BMP_PEERDOWN_ENDMONITOR    —— 监控策略结束；
 *   BMP_PEERDOWN_LOCAL_TLV     —— 本端原因以 TLV 给出（RFC 9069）。
 */
enum {
	/* RFC7854 - 10.8 */
	BMP_PEERDOWN_LOCAL_NOTIFY = 1,
	BMP_PEERDOWN_LOCAL_FSM = 2,
	BMP_PEERDOWN_REMOTE_NOTIFY = 3,
	BMP_PEERDOWN_REMOTE_CLOSE = 4,
	BMP_PEERDOWN_ENDMONITOR = 5,
	/* RFC9069 - 8.4 */
	BMP_PEERDOWN_LOCAL_TLV = 6,
};

/* statistics report 的统计类型码（部分）。前若干项来自 RFC 7854 第 10.9 节，
 * 末尾 BMP_STATS_FRR_NH_INVALID 是 FRR 的私有 / 实验扩展值。
 */
enum {
	BMP_STATS_PFX_REJECTED          = 0,
	BMP_STATS_PFX_DUP_ADV           = 1,
	BMP_STATS_PFX_DUP_WITHDRAW      = 2,
	BMP_STATS_UPD_LOOP_CLUSTER      = 3,
	BMP_STATS_UPD_LOOP_ASPATH       = 4,
	BMP_STATS_UPD_LOOP_ORIGINATOR   = 5,
	BMP_STATS_UPD_LOOP_CONFED       = 6,
	BMP_STATS_SIZE_ADJ_RIB_IN       = 7,
	BMP_STATS_SIZE_LOC_RIB          = 8,
	BMP_STATS_SIZE_ADJ_RIB_IN_SAFI  = 9,
	BMP_STATS_SIZE_LOC_RIB_IN_SAFI  = 10,
	BMP_STATS_UPD_7606_WITHDRAW     = 11,
	BMP_STATS_PFX_7606_WITHDRAW     = 12,
	BMP_STATS_UPD_DUP               = 13,
	BMP_STATS_FRR_NH_INVALID        = 65531,
};

DECLARE_MGROUP(BMP);

#endif /*_BGP_BMP_H_*/
