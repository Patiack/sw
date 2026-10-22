// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP-4 dump routine
 * Copyright (C) 1999 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_dump.c —— 把 BGP 报文事件流与 RIB 快照编码成 MRT 文件
 * ============================================================================
 * 【这个文件解决什么问题】bgpd 运行时有两类“可被离线回放”的信息：
 *   1) 报文事件流：邻居收 / 发的每条 UPDATE / OPEN 等报文，以及邻居状态机的
 *      跃迁(Idle→Connect→…→Established)。对应 `dump bgp all / updates`。
 *   2) RIB 快照：某一时刻本机 BGP 路由表的完整内容。对应 `dump bgp routes-mrt`。
 *   两者都被编码成 MRT(Multi-threaded Routing Toolkit, RFC 6396)格式的二进制
 *   记录，顺序追加到文件。之后可用 bgpdump / Wireshark 等工具离线解析，用于
 *   排障回溯和生成研究数据集。注意：这是**旁路功能**，dump 与否不影响路由计算
 *   与转发，只是往磁盘写字节。
 *
 * 【为什么这样设计】MRT 是“自描述 + 网络字节序”的流式格式：每条记录 = 12 字节
 *   公共头(type + subtype + 时间戳 + 长度) + 负载。因此只需一个全局输出缓冲
 *   bgp_dump_obuf，每次“复位 → 先写头部、长度留 0 占位 → 填负载 → 回填长度 →
 *   fwrite”。长度字段位置固定(偏移 8)，所以能先占位、最后用 bgp_dump_set_size
 *   回填。所有多字节整数一律网络序(大端)，由 stream_putw / stream_putl 保证。
 *
 * 【关键数据结构】
 *   struct bgp_dump      : 一份 dump 通道的运行时状态(type / 文件名 / 间隔 /
 *                          定时器 / 文件句柄)；本文件有三个全局实例，分别对应
 *                          all / updates / routes 三条独立通道，互不干扰。
 *   bgp_dump_obuf        : 全局复用的编码缓冲区(单线程事件循环里使用，无需加锁)。
 *   peer->table_dump_index : RIB 快照里给每个邻居分配的编号，见
 *                          bgp_dump_routes_index_table。
 *
 * 【两条编码路径(推荐阅读顺序)】
 *   报文路径: bgp_dump_packet(hook 回调) → bgp_dump_packet_func
 *                → bgp_dump_header + bgp_dump_common。
 *   快照路径: bgp_dump_interval_func(定时器) → bgp_dump_routes_func
 *                → bgp_dump_routes_index_table(邻居编号表)
 *                → bgp_dump_route_node_record(逐前缀记录) + bgp_dump_routes_attr。
 *   公共基础设施: bgp_dump_header / bgp_dump_set_size(记录头与长度回填)、
 *                bgp_dump_open_file(按时间戳展开文件名并打开)、
 *                bgp_dump_interval_add(定时 / 一次性调度)。
 *   配置与生命周期: dump_bgp_all / no_dump_bgp_all(DEFUN)、bgp_dump_set /
 *                bgp_dump_unset、config_write_bgp_dump、bgp_dump_init /
 *                bgp_dump_finish。
 *
 * 【与头文件分工】bgp_dump.h 定义 MRT 的 type / subtype / 头部长度等宏；
 *   bgp_dump_routes_attr(属性编码)实现在 bgp_attr.c 一侧，被本文件调用。
 * ============================================================================
 */

#include <zebra.h>
#include <sys/stat.h>

#include "log.h"
#include "stream.h"
#include "sockunion.h"
#include "command.h"
#include "prefix.h"
#include "frrevent.h"
#include "linklist.h"
#include "queue.h"
#include "memory.h"
#include "filter.h"

#include "bgpd/bgp_table.h"
#include "bgpd/bgpd.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_dump.h"
#include "bgpd/bgp_errors.h"
#include "bgpd/bgp_packet.h"

/* 【enum bgp_dump_type】CLI 层面的“dump 类别”枚举——**不是** MRT 的 type 字段。
 * 它区分用户配置的五种模式：all(BGP 全报文), all-et(全报文 + 微秒扩展头),
 * updates(仅 UPDATE), updates-et, routes-mrt(RIB 快照)。这个值在整个文件中
 * 用于挑选目标通道，并最终由 bgp_dump_header 映射为 MRT 的 type / subtype。 */
enum bgp_dump_type {
	BGP_DUMP_ALL,
	BGP_DUMP_ALL_ET,
	BGP_DUMP_UPDATES,
	BGP_DUMP_UPDATES_ET,
	BGP_DUMP_ROUTES
};

/* 【bgp_dump_type_map】把上面的枚举与 CLI 关键字字符串对照起来，供 DEFUN 用
 * strmatch 把用户输入解析成枚举；末尾 {0, NULL} 是遍历终止哨兵。 */
static const struct bgp_dump_type_map {
	enum bgp_dump_type type;
	const char *str;
} bgp_dump_type_map[] = {
	{BGP_DUMP_ALL, "all"},		 {BGP_DUMP_ALL_ET, "all-et"},
	{BGP_DUMP_UPDATES, "updates"},   {BGP_DUMP_UPDATES_ET, "updates-et"},
	{BGP_DUMP_ROUTES, "routes-mrt"}, {0, NULL},
};

/* 【enum MRT_MSG_TYPES】MRT 记录顶层 type 的取值表(MRT 规范定义)。这是一张
 * “对齐规范”的完整清单：START / DIE / PEER_DOWN 等是旧 MRT 工具的会话控制类，
 * RIP / IDRP / RIPNG / OSPF 是为其它协议预留的。
 * 关键：bgpd 实际写出时用的是 bgp_dump.h 里的宏(MSG_PROTOCOL_BGP4MP=16、
 * MSG_PROTOCOL_BGP4MP_ET=17、MSG_TABLE_DUMP_V2=13)，本枚举仅作规范对照，
 * 两者数值并**不**一一对应，读代码时别把本枚举的编号当成写出的 type 值。 */
enum MRT_MSG_TYPES {
	MSG_NULL,
	MSG_START,		  /* sender is starting up */
	MSG_DIE,		  /* receiver should shut down */
	MSG_I_AM_DEAD,		  /* sender is shutting down */
	MSG_PEER_DOWN,		  /* sender's peer is down */
	MSG_PROTOCOL_BGP,	 /* msg is a BGP packet */
	MSG_PROTOCOL_RIP,	 /* msg is a RIP packet */
	MSG_PROTOCOL_IDRP,	/* msg is an IDRP packet */
	MSG_PROTOCOL_RIPNG,       /* msg is a RIPNG packet */
	MSG_PROTOCOL_BGP4PLUS,    /* msg is a BGP4+ packet */
	MSG_PROTOCOL_BGP4PLUS_01, /* msg is a BGP4+ (draft 01) packet */
	MSG_PROTOCOL_OSPF,	/* msg is an OSPF packet */
	MSG_TABLE_DUMP,		  /* routing table dump */
	MSG_TABLE_DUMP_V2	 /* routing table dump, version 2 */
};

/* 【struct bgp_dump】一份 dump 通道的运行时状态。
 *   type         : 本通道的 CLI 类别(all / all-et / updates / updates-et /
 *                  routes-mrt)，决定 bgp_dump_header 写出的 MRT type / subtype。
 *   filename     : 用户配置的输出路径(可含 strftime 时间格式，如 %Y%m%d)。
 *   fp           : 当前打开的文件句柄；NULL 表示未打开(此时不写)。
 *   interval     : 周期(秒)；0 表示“一次性 dump”。
 *   interval_str : 原始间隔字符串(如 "1m")，用于回显配置。
 *   t_interval   : 触发本次 / 下次 dump 的定时器事件。
 * 本文件有三个全局实例：bgp_dump_all / bgp_dump_updates / bgp_dump_routes。 */
struct bgp_dump {
	enum bgp_dump_type type;

	char *filename;

	FILE *fp;

	unsigned int interval;

	char *interval_str;

	struct event *t_interval;
};

/* 前置声明：unset 清理配置，interval_func 是定时器回调(定义见下)。 */
static int bgp_dump_unset(struct bgp_dump *bgp_dump);
static void bgp_dump_interval_func(struct event *);

/* ========== 全局状态：编码缓冲 + 三个独立的 dump 通道 ========== */
/* bgp_dump_obuf : 全局复用的 MRT 编码缓冲，所有记录都先编到这里再 fwrite。
 *   大小按“最大 BGP 报文 + 溢出余量”预留，保证单条报文记录能整体容纳；
 *   单线程事件循环串行使用，无需加锁。
 * bgp_dump_all / bgp_dump_updates / bgp_dump_routes : 三个通道各自的运行时状态，
 *   分别对应 CLI 的 all(全报文 + 状态变化) / updates(仅 UPDATE) / routes(RIB 快照)，
 *   互不干扰，可同时启用。 */
/* BGP packet dump output buffer. */
struct stream *bgp_dump_obuf;

/* BGP dump structure for 'dump bgp all' */
struct bgp_dump bgp_dump_all;

/* BGP dump structure for 'dump bgp updates' */
struct bgp_dump bgp_dump_updates;

/* BGP dump structure for 'dump bgp routes' */
struct bgp_dump bgp_dump_routes;

/* 【函数】bgp_dump_open_file —— 展开文件名并按当前时间打开输出文件。
 * 细节：
 *   - filename 被当作 strftime 模板展开(支持 %Y%m%d 之类)，所以周期 dump 每次
 *     都能生成带时间戳的新文件；若首字符是目录分隔符则视为绝对路径直接用。
 *   - 相对路径会拼上配置态当前目录 vty_get_cwd()。
 *   - 先关掉旧句柄再以 "w"(截断)打开。
 *   - 临时收紧 umask，使新文件权限受 LOGFILE_MASK 约束，open 后立即恢复。
 * 返回有效 FILE* 表示成功；失败返回 NULL(调用方据此跳过本次写入)。 */
static FILE *bgp_dump_open_file(struct bgp_dump *bgp_dump)
{
	int ret;
	time_t clock;
	struct tm tm;
	char fullpath[MAXPATHLEN];
	char realpath[MAXPATHLEN];
	mode_t oldumask;

	time(&clock);
	localtime_r(&clock, &tm);

	/* 相对路径：拼上配置态当前目录后作为 strftime 模板展开 */
	if (bgp_dump->filename[0] != DIRECTORY_SEP) {
		snprintf(fullpath, sizeof(fullpath), "%s/%s", vty_get_cwd(),
			 bgp_dump->filename);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
		/* user supplied date/time format string */
		ret = strftime(realpath, MAXPATHLEN, fullpath, &tm);
	} else
		ret = strftime(realpath, MAXPATHLEN, bgp_dump->filename, &tm);
#pragma GCC diagnostic pop

	if (ret == 0) {
		flog_warn(EC_BGP_DUMP, "%s: strftime error", __func__);
		return NULL;
	}

	if (bgp_dump->fp)
		fclose(bgp_dump->fp);


	/* 临时收紧 umask，限制新文件权限；open 完立刻恢复 */
	oldumask = umask(0777 & ~LOGFILE_MASK);
	bgp_dump->fp = fopen(realpath, "w");

	if (bgp_dump->fp == NULL) {
		flog_warn(EC_BGP_DUMP, "%s: %s: %s", __func__, realpath,
			  strerror(errno));
		umask(oldumask);
		return NULL;
	}
	umask(oldumask);

	return bgp_dump->fp;
}

/* 【函数】bgp_dump_interval_add —— 调度下一次 dump。
 * interval > 0：周期任务。若 interval 能整除一天(86400)，则把首次触发对齐到
 *   “今天内下一个整齐边界”: 触发在 interval - (当天已过秒数 % interval) 秒后，
 *   使 dump 落在可预测时刻(midnight 起算)；否则从当前起 interval 秒后触发。
 *   对齐用的 secs_into_day 是“今天已经过了多少秒”。
 * interval == 0：一次性任务，立即执行(event_add_event，delay=0)，不影响已排定的
 *   周期 dump。
 * 结果都记录到 bgp_dump->t_interval，供 bgp_dump_unset 取消。 */
static int bgp_dump_interval_add(struct bgp_dump *bgp_dump, int interval)
{
	int secs_into_day;
	time_t t;
	struct tm tm;

	if (interval > 0) {
		/* Periodic dump every interval seconds */
		if ((interval < 86400) && ((86400 % interval) == 0)) {
			/* Dump at predictable times: if a day has a whole
			 * number of
			 * intervals, dump every interval seconds starting from
			 * midnight
			 */
			(void)time(&t);
			localtime_r(&t, &tm);
			secs_into_day = tm.tm_sec + 60 * tm.tm_min
					+ 60 * 60 * tm.tm_hour;
			interval = interval
				   - secs_into_day % interval; /* always > 0 */
		}
		event_add_timer(bm->master, bgp_dump_interval_func, bgp_dump,
				interval, &bgp_dump->t_interval);
	} else {
		/* One-off dump: execute immediately, don't affect any scheduled
		 * dumps */
		event_add_event(bm->master, bgp_dump_interval_func, bgp_dump, 0,
				&bgp_dump->t_interval);
	}

	return 0;
}

/* 【函数】bgp_dump_header —— 写 MRT 记录公共头(12 字节)以及可选的 ET 微秒字段。
 * 布局(网络序)：4 字节秒时间戳 | 2 字节 type | 2 字节 subtype | 4 字节长度 |
 *   [仅 ET: 4 字节微秒]。
 * 长度字段先写 0 占位，负载写完后由 bgp_dump_set_size 回填(位置固定在偏移 8)。
 * 若本通道是 *_ET 且 type 为 BGP4MP(16)，则升级为 BGP4MP_ET(17) 并多写微秒字段。
 * 注意：即使带 ET，长度基准仍从公共头之后算起，微秒字段计入长度(RFC6396)。 */
/* Dump common header. */
static void bgp_dump_header(struct stream *obuf, int type, int subtype,
			    int dump_type)
{
	struct timeval clock;
	long msecs;
	time_t secs;

	if ((dump_type == BGP_DUMP_ALL_ET || dump_type == BGP_DUMP_UPDATES_ET)
	    && type == MSG_PROTOCOL_BGP4MP)
		type = MSG_PROTOCOL_BGP4MP_ET;

	/* 取当前时间：秒进公共头；微秒仅在 ET 通道写入 */
	gettimeofday(&clock, NULL);

	secs = clock.tv_sec;
	msecs = clock.tv_usec;

	/* Put dump packet header. */
	stream_putl(obuf, frr_time_t_to_uint32_t(secs));
	stream_putw(obuf, type);
	stream_putw(obuf, subtype);
	stream_putl(obuf, 0); /* len */

	/* Adding microseconds for the MRT Extended Header */
	if (type == MSG_PROTOCOL_BGP4MP_ET)
		stream_putl(obuf, msecs);
}

/* 【函数】bgp_dump_set_size —— 回填记录长度到公共头偏移 8 处。
 * 写入值 = 当前缓冲末尾位置 - BGP_DUMP_HEADER_SIZE，即“负载长度”(不含公共头)。
 * 即使 ET 多写了 4 字节微秒，长度基准仍是 12，故微秒计入长度(RFC6396 2011)。 */
static void bgp_dump_set_size(struct stream *s, int type)
{
	/*
	 * The BGP_DUMP_HEADER_SIZE stay at 12 event when ET:
	 * "The Microsecond Timestamp is included in the computation
	 *  of the Length field value." (RFC6396 2011)
	 */
	stream_putl_at(s, 8, stream_get_endp(s) - BGP_DUMP_HEADER_SIZE);
}

/* 【函数】bgp_dump_routes_index_table —— 写 RIB 快照开头的 PEER_INDEX_TABLE。
 * TABLE_DUMP_V2 快照要求先给出“邻居编号表”，后续每条前缀记录只引用编号，避免
 * 重复写整段邻居 IP。内容：
 *   - Collector BGP ID(本机 router_id)。
 *   - View 名(VRF 名；无则长度 0)。
 *   - 邻居数(实际邻居 + 1 个本地伪邻居)。
 *   - 邻居条目：每条的“类型字节” = 地址族位(IP / IP6) + AS 宽度位(AS4) 的组合；
 *     随后是 BGP ID、IP 地址、ASN。
 * 索引 0 是本地伪邻居(0.0.0.0 / AS0)，代表本机产生的路由。
 * 遍历中把每个邻居的编号存入 peer->table_dump_index，供后续前缀记录引用。
 * 注意：本函数把 IPv4 / IPv6 邻居一次全写入，故每次快照只调用一次(见调用方)。 */
static void bgp_dump_routes_index_table(struct bgp *bgp)
{
	struct peer *peer;
	struct listnode *node;
	uint16_t peerno = 1;
	struct stream *obuf;

	obuf = bgp_dump_obuf;
	stream_reset(obuf);

	/* MRT header */
	bgp_dump_header(obuf, MSG_TABLE_DUMP_V2, TABLE_DUMP_V2_PEER_INDEX_TABLE,
			BGP_DUMP_ROUTES);

	/* Collector BGP ID */
	stream_put_in_addr(obuf, &bgp->router_id);

	/* View name */
	if (bgp->name_pretty) {
		stream_putw(obuf, strlen(bgp->name_pretty));
		stream_put(obuf, bgp->name_pretty, strlen(bgp->name_pretty));
	} else {
		stream_putw(obuf, 0);
	}

	/* Peer count ( plus one extra internal peer ) */
	stream_putw(obuf, listcount(bgp->peer) + 1);

	/* Populate fake peer at index 0, for locally originated routes */
	/* Peer type (IPv4) */
	stream_putc(obuf,
		    TABLE_DUMP_V2_PEER_INDEX_TABLE_AS4
			    + TABLE_DUMP_V2_PEER_INDEX_TABLE_IP);
	/* Peer BGP ID (0.0.0.0) */
	stream_putl(obuf, 0);
	/* Peer IP address (0.0.0.0) */
	stream_putl(obuf, 0);
	/* Peer ASN (0) */
	stream_putl(obuf, 0);

	/* Walk down all peers */
	/* 逐个邻居写编号条目，并把编号记进 peer->table_dump_index */
	for (ALL_LIST_ELEMENTS_RO(bgp->peer, node, peer)) {
		int family = sockunion_family(&peer->connection->su);

		/* Peer's type */
		if (family == AF_INET) {
			stream_putc(
				obuf,
				TABLE_DUMP_V2_PEER_INDEX_TABLE_AS4
					+ TABLE_DUMP_V2_PEER_INDEX_TABLE_IP);
		} else if (family == AF_INET6) {
			stream_putc(
				obuf,
				TABLE_DUMP_V2_PEER_INDEX_TABLE_AS4
					+ TABLE_DUMP_V2_PEER_INDEX_TABLE_IP6);
		}

		/* Peer's BGP ID */
		stream_put_in_addr(obuf, &peer->remote_id);

		/* Peer's IP address */
		if (family == AF_INET) {
			stream_put_in_addr(obuf,
					   &peer->connection->su.sin.sin_addr);
		} else if (family == AF_INET6) {
			stream_write(obuf,
				     (uint8_t *)&peer->connection->su.sin6
					     .sin6_addr,
				     IPV6_MAX_BYTELEN);
		}

		/* Peer's AS number. */
		/* Note that, as this is an AS4 compliant quagga, the RIB is
		 * always AS4 */
		stream_putl(obuf, peer->as);

		/* Store the peer number for this peer */
		/* 记住本邻居的编号(从 1 起)，后面 RIB 记录用它引用 */
		peer->table_dump_index = peerno;
		peerno++;
	}

	bgp_dump_set_size(obuf, MSG_TABLE_DUMP_V2);

	fwrite(STREAM_DATA(obuf), stream_get_endp(obuf), 1, bgp_dump_routes.fp);
	fflush(bgp_dump_routes.fp);
}

/* 【函数】bgp_dump_route_node_record —— 编码“一个前缀”的一条 RIB 记录，返回下一个
 * 待处理的 path(可能因记录超长而提前截断)。
 * 记录结构(TABLE_DUMP_V2 RIB 记录)：
 *   公共头 | 4 字节序列号 | 1 字节前缀长度 | 前缀字节 |
 *   2 字节 entry 数 | 每条 entry{ 2 字节 peer 索引 | 4 字节 originated 时间 |
 *   [Add-Path: 4 字节 path-id] | 路径属性 }。
 * 细节：
 *   - subtype 依 afi(AFI_IP / AFI_IP6) 与是否 Add-Path 从四个 TABLE_DUMP_V2 宏中选。
 *   - 前缀只写有效位、按字节向上取整((prefixlen + 7) / 8)。
 *   - entry 数位置先写 0 占位(记下 sizep)，循环结束后回填真实条数。
 *   - 若累计长度超过“标准报文上限 + 两段头”，回退到本 entry 之前(endp)并跳出，
 *     即把剩余 path 留到下一次调用，避免单条记录过大。
 * 返回：本次“未写入”的 path 指针(供调用方 while 继续处理)。 */
static struct bgp_path_info *
bgp_dump_route_node_record(int afi, struct bgp_dest *dest,
			   struct bgp_path_info *path, unsigned int seq)
{
	struct stream *obuf;
	size_t sizep;
	size_t endp;
	bool addpath_capable;
	const struct prefix *p = bgp_dest_get_prefix(dest);

	obuf = bgp_dump_obuf;
	stream_reset(obuf);

	/* 该邻居在此 afi 是否使用 Add-Path 接收编码：决定 subtype 与是否写 path-id */
	addpath_capable = bgp_addpath_encode_rx(path->peer, afi, SAFI_UNICAST);

	/* MRT header */
	if (afi == AFI_IP && addpath_capable)
		bgp_dump_header(obuf, MSG_TABLE_DUMP_V2,
				TABLE_DUMP_V2_RIB_IPV4_UNICAST_ADDPATH,
				BGP_DUMP_ROUTES);
	else if (afi == AFI_IP)
		bgp_dump_header(obuf, MSG_TABLE_DUMP_V2,
				TABLE_DUMP_V2_RIB_IPV4_UNICAST,
				BGP_DUMP_ROUTES);
	else if (afi == AFI_IP6 && addpath_capable)
		bgp_dump_header(obuf, MSG_TABLE_DUMP_V2,
				TABLE_DUMP_V2_RIB_IPV6_UNICAST_ADDPATH,
				BGP_DUMP_ROUTES);
	else if (afi == AFI_IP6)
		bgp_dump_header(obuf, MSG_TABLE_DUMP_V2,
				TABLE_DUMP_V2_RIB_IPV6_UNICAST,
				BGP_DUMP_ROUTES);

	/* Sequence number */
	stream_putl(obuf, seq);

	/* Prefix length */
	stream_putc(obuf, p->prefixlen);

	/* Prefix */
	if (afi == AFI_IP) {
		/* We'll dump only the useful bits (those not 0), but have to
		 * align on 8 bits */
		stream_write(obuf, (uint8_t *)&p->u.prefix4,
			     (p->prefixlen + 7) / 8);
	} else if (afi == AFI_IP6) {
		/* We'll dump only the useful bits (those not 0), but have to
		 * align on 8 bits */
		stream_write(obuf, (uint8_t *)&p->u.prefix6,
			     (p->prefixlen + 7) / 8);
	}

	/* Save where we are now, so we can overwrite the entry count later */
	sizep = stream_get_endp(obuf);

	/* Entry count */
	uint16_t entry_count = 0;

	/* Entry count, note that this is overwritten later */
	stream_putw(obuf, 0);

	endp = stream_get_endp(obuf);
	/* 逐条 path(同一前缀可能有多个来源)写成一个 entry */
	for (; path; path = path->next) {
		size_t cur_endp;

		/* Peer index */
		stream_putw(obuf, path->peer->table_dump_index);

		/* Originated */
		stream_putl(obuf, time(NULL) - (monotime(NULL) - path->uptime));

		/*Path Identifier*/
		if (addpath_capable) {
			stream_putl(obuf, path->addpath_rx_id);
		}

		/* Dump attribute. */
		/* Skip prefix & AFI/SAFI for MP_NLRI */
		bgp_dump_routes_attr(obuf, path, p);

		/* 检查本 entry 后是否已超过记录大小上限，超过则回退并留待下次 */
		cur_endp = stream_get_endp(obuf);
		if (cur_endp > BGP_STANDARD_MESSAGE_MAX_PACKET_SIZE
				       + BGP_DUMP_MSG_HEADER
				       + BGP_DUMP_HEADER_SIZE) {
			stream_set_endp(obuf, endp);
			break;
		}

		entry_count++;
		endp = cur_endp;
	}

	/* Overwrite the entry count, now that we know the right number */
	stream_putw_at(obuf, sizep, entry_count);

	bgp_dump_set_size(obuf, MSG_TABLE_DUMP_V2);
	fwrite(STREAM_DATA(obuf), stream_get_endp(obuf), 1, bgp_dump_routes.fp);

	return path;
}


/* 【函数】bgp_dump_routes_func —— 把某 afi 的 BGP RIB 全量写成一个快照。
 * 参数 first_run 为真时先写 PEER_INDEX_TABLE(该表含 IPv4 与 IPv6 邻居，故整次
 *   快照只在第一次调用时写一次)；seq 是跨 afi 延续的序列号(入参 = 上次返回值)，
 *   返回值更新 seq，保证 IPv4 与 IPv6 记录序列号连续。
 * 注意此函数可能运行在独立的子进程里(见主进程调用处)。
 * 无默认 bgp 实例、或文件未打开时直接返回(不写)。 */
/* Runs under child process. */
static unsigned int bgp_dump_routes_func(int afi, int first_run,
					 unsigned int seq)
{
	struct bgp_path_info *path;
	struct bgp_dest *dest;
	struct bgp *bgp;
	struct bgp_table *table;

	bgp = bgp_get_default();
	if (!bgp)
		return seq;

	if (bgp_dump_routes.fp == NULL)
		return seq;

	/* Note that bgp_dump_routes_index_table will do ipv4 and ipv6 peers,
	   so this should only be done on the first call to
	   bgp_dump_routes_func.
	   ( this function will be called once for ipv4 and once for ipv6 ) */
	if (first_run)
		bgp_dump_routes_index_table(bgp);

	/* Walk down each BGP route. */
	/* 取该 afi 的单播 RIB，逐节点遍历 */
	table = bgp->rib[afi][SAFI_UNICAST];

	for (dest = bgp_table_top(table); dest; dest = bgp_route_next(dest)) {
		/* 取出该前缀的第一条 path，逐条编码(同前缀可能有多条) */
		path = bgp_dest_get_bgp_path_info(dest);
		while (path) {
			path = bgp_dump_route_node_record(afi, dest, path, seq);
			seq++;
		}
	}

	fflush(bgp_dump_routes.fp);

	return seq;
}

/* 【函数】bgp_dump_interval_func —— dump 定时器到期回调(周期或一次性)。
 * 流程：
 *   1) 打开 / 重开文件(周期 dump 会按当前时间生成新文件名)。
 *   2) 若是 routes 通道：调用 RIB 快照函数(AFI_IP 先、first_run=1 写邻居编号表；
 *      再 AFI_IP6、first_run=0)，写完立即关闭文件——RIB 快照没有“保持打开”的必要。
 *   3) 若配置了 interval，重新排下次定时器(周期续跑)。
 * 注意：即使文件打开失败也会继续重排定时器，避免一次失败就永久停止 dump。 */
static void bgp_dump_interval_func(struct event *t)
{
	struct bgp_dump *bgp_dump;
	bgp_dump = EVENT_ARG(t);

	/* Reschedule dump even if file couldn't be opened this time... */
	if (bgp_dump_open_file(bgp_dump) != NULL) {
		/* In case of bgp_dump_routes, we need special route dump
		 * function. */
		if (bgp_dump->type == BGP_DUMP_ROUTES) {
			unsigned int seq = bgp_dump_routes_func(AFI_IP, 1, 0);
			bgp_dump_routes_func(AFI_IP6, 0, seq);
			/* Close the file now. For a RIB dump there's no point
			 * in leaving
			 * it open until the next scheduled dump starts. */
			fclose(bgp_dump->fp);
			bgp_dump->fp = NULL;
		}
	}

	/* if interval is set reschedule */
	if (bgp_dump->interval > 0)
		bgp_dump_interval_add(bgp_dump, bgp_dump->interval);
}

/* 【函数】bgp_dump_common —— 写 BGP4MP 报文 / 状态记录的“公共部分”：双方 AS 号、
 * 接口索引、地址族、源 / 目的 IP。
 * forceas4 为真、或邻居已协商 AS4 能力(PEER_CAP_AS4_RCV) 时用 4 字节 AS，否则用
 * 2 字节 AS。随后按邻居地址族(AF_INET / AF_INET6)分别写接口索引、地址族、
 * 目的地址、源地址(无 su_local 时写全 0 的 empty)。 */
/* Dump common information. */
static void bgp_dump_common(struct stream *obuf, struct peer *peer,
			    int forceas4)
{
	char empty[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	/* Source AS number and Destination AS number. */
	if (forceas4 || CHECK_FLAG(peer->cap, PEER_CAP_AS4_RCV)) {
		stream_putl(obuf, peer->as);
		stream_putl(obuf, peer->local_as);
	} else {
		stream_putw(obuf, peer->as);
		stream_putw(obuf, peer->local_as);
	}

	/* 按邻居地址族写接口索引、地址族与双方地址(IPv4 / IPv6 分支) */
	if (peer->connection->su.sa.sa_family == AF_INET) {
		stream_putw(obuf, peer->ifp ? peer->ifp->ifindex : 0);
		stream_putw(obuf, AFI_IP);

		stream_put(obuf, &peer->connection->su.sin.sin_addr,
			   IPV4_MAX_BYTELEN);

		if (peer->connection->su_local)
			stream_put(obuf, &peer->connection->su_local->sin.sin_addr,
				   IPV4_MAX_BYTELEN);
		else
			stream_put(obuf, empty, IPV4_MAX_BYTELEN);
	} else if (peer->connection->su.sa.sa_family == AF_INET6) {
		/* Interface Index and Address family. */
		stream_putw(obuf, peer->ifp ? peer->ifp->ifindex : 0);
		stream_putw(obuf, AFI_IP6);

		/* Source IP Address and Destination IP Address. */
		stream_put(obuf, &peer->connection->su.sin6.sin6_addr,
			   IPV6_MAX_BYTELEN);

		if (peer->connection->su_local)
			stream_put(obuf, &peer->connection->su_local->sin6.sin6_addr,
				   IPV6_MAX_BYTELEN);
		else
			stream_put(obuf, empty, IPV6_MAX_BYTELEN);
	}
}

/* 【函数】bgp_dump_state —— 邻居状态机变化时记录一条 BGP4MP 状态变更记录。
 * 由 peer_status_changed hook 触发(见 bgp_dump_init)；仅当 all 通道已打开时才写。
 * 记录 = 公共头(type=BGP4MP, subtype=STATE_CHANGE_AS4) + 公共部分 + 旧状态 +
 * 新状态。AS 强制为 4 字节(forceas4=1，见 bgp_dump_common)。 */
/* Dump BGP status change. */
int bgp_dump_state(struct peer *peer)
{
	struct stream *obuf;

	/* If dump file pointer is disabled return immediately. */
	if (bgp_dump_all.fp == NULL)
		return 0;

	/* Make dump stream. */
	obuf = bgp_dump_obuf;
	stream_reset(obuf);

	bgp_dump_header(obuf, MSG_PROTOCOL_BGP4MP, BGP4MP_STATE_CHANGE_AS4,
			bgp_dump_all.type);
	bgp_dump_common(obuf, peer, 1); /* force this in as4speak*/

	/* 旧状态与新状态(各 2 字节，网络序) */
	stream_putw(obuf, peer->connection->ostatus);
	stream_putw(obuf, peer->connection->status);

	/* Set length. */
	bgp_dump_set_size(obuf, MSG_PROTOCOL_BGP4MP);

	/* Write to the stream. */
	fwrite(STREAM_DATA(obuf), stream_get_endp(obuf), 1, bgp_dump_all.fp);
	fflush(bgp_dump_all.fp);
	return 0;
}

/* 【函数】bgp_dump_packet_func —— 把一条 BGP 报文编码成 BGP4MP 报文记录，写入某个
 * dump 通道。
 * subtype 选择矩阵：是否 AS4(邻居协商了 PEER_CAP_AS4_RCV) × 是否 Add-Path
 *   (按地址族查 bgp_addpath_encode_rx) → BGP4MP_MESSAGE / _AS4 / _ADDPATH /
 *   _AS4_ADDPATH。
 * 记录 = 公共头 + 公共部分 + 原始报文字节。 */
static void bgp_dump_packet_func(struct bgp_dump *bgp_dump, struct peer *peer,
				 struct stream *packet)
{
	struct stream *obuf;
	bool addpath_capable = false;
	/* If dump file pointer is disabled return immediately. */
	if (bgp_dump->fp == NULL)
		return;
	/* 按地址族判断本连接是否启用了 Add-Path 接收编码 */
	if (peer->connection->su.sa.sa_family == AF_INET) {
		addpath_capable =
			bgp_addpath_encode_rx(peer, AFI_IP, SAFI_UNICAST);
	} else if (peer->connection->su.sa.sa_family == AF_INET6) {
		addpath_capable =
			bgp_addpath_encode_rx(peer, AFI_IP6, SAFI_UNICAST);
	}

	/* Make dump stream. */
	obuf = bgp_dump_obuf;
	stream_reset(obuf);

	/* Dump header and common part. */
	/* 依据 AS4 与 Add-Path 两个维度挑选 subtype */
	if (CHECK_FLAG(peer->cap, PEER_CAP_AS4_RCV) && addpath_capable) {
		bgp_dump_header(obuf, MSG_PROTOCOL_BGP4MP,
				BGP4MP_MESSAGE_AS4_ADDPATH, bgp_dump->type);
	} else if (CHECK_FLAG(peer->cap, PEER_CAP_AS4_RCV)) {
		bgp_dump_header(obuf, MSG_PROTOCOL_BGP4MP, BGP4MP_MESSAGE_AS4,
				bgp_dump->type);
	} else if (addpath_capable) {
		bgp_dump_header(obuf, MSG_PROTOCOL_BGP4MP,
				BGP4MP_MESSAGE_ADDPATH, bgp_dump->type);
	} else {
		bgp_dump_header(obuf, MSG_PROTOCOL_BGP4MP, BGP4MP_MESSAGE,
				bgp_dump->type);
	}
	bgp_dump_common(obuf, peer, 0);

	/* Packet contents. */
	stream_put(obuf, STREAM_DATA(packet), stream_get_endp(packet));

	/* Set length. */
	bgp_dump_set_size(obuf, MSG_PROTOCOL_BGP4MP);

	/* Write to the stream. */
	fwrite(STREAM_DATA(obuf), stream_get_endp(obuf), 1, bgp_dump->fp);
	fflush(bgp_dump->fp);
}

/* 【函数】bgp_dump_packet —— bgp_packet_dump hook 的回调，BGP 收到 / 发出报文时调用。
 * 先写 all 通道；若报文是 UPDATE(BGP_MSG_UPDATE)，再额外写 updates 通道——两条
 * 通道可对同一报文各写一份。type 是 BGP 报文类型。 */
/* Called from bgp_packet.c when BGP packet is received. */
static int bgp_dump_packet(struct peer *peer, uint8_t type, bgp_size_t size,
		struct stream *packet)
{
	/* bgp_dump_all. */
	bgp_dump_packet_func(&bgp_dump_all, peer, packet);

	/* bgp_dump_updates. */
	if (type == BGP_MSG_UPDATE)
		bgp_dump_packet_func(&bgp_dump_updates, peer, packet);
	return 0;
}

/* 【函数】bgp_dump_parse_time —— 解析 CLI 的间隔字符串为秒数。
 * 支持形如 "1H30M"(时用 H / h、分用 M / m)，数字按位累积；H 与 M 各只允许出现
 * 一次(重复或顺序错误返回 0)。末尾无单位的数字按秒处理(返回 total + time)。
 * 返回 0 表示格式非法，调用方据此报 "Malformed interval string"。 */
static unsigned int bgp_dump_parse_time(const char *str)
{
	int i;
	int len;
	int seen_h;
	int seen_m;
	int time;
	unsigned int total;

	time = 0;
	total = 0;
	seen_h = 0;
	seen_m = 0;
	len = strlen(str);

	/* 逐字符扫描：数字累积成 time；遇 H / h 折算为小时并入 total，
	 * 遇 M / m 折算为分钟并入 total；非法字符或重复单位则返回 0 */
	for (i = 0; i < len; i++) {
		if (isdigit((unsigned char)str[i])) {
			time *= 10;
			time += str[i] - '0';
		} else if (str[i] == 'H' || str[i] == 'h') {
			if (seen_h)
				return 0;
			if (seen_m)
				return 0;
			total += time * 60 * 60;
			time = 0;
			seen_h = 1;
		} else if (str[i] == 'M' || str[i] == 'm') {
			if (seen_m)
				return 0;
			total += time * 60;
			time = 0;
			seen_m = 1;
		} else
			return 0;
	}
	return total + time;
}

/* 【函数】bgp_dump_set —— 处理 `dump bgp ...` 配置命令：写配置并启动调度。
 * 幂等：若文件名与类型都与现有配置相同、且间隔字符串也相同，则直接成功返回
 * (避免重复下发命令时重复挂定时器)。
 * 否则先 bgp_dump_unset 清旧配置，再解析间隔(非法则报 "Malformed interval
 * string" 并失败)、记录 type / interval / filename，最后排定时器并立刻打开一次
 * 文件(这样即使间隔是 0 的一次性 dump 也会马上执行)。 */
static int bgp_dump_set(struct vty *vty, struct bgp_dump *bgp_dump,
			enum bgp_dump_type type, const char *path,
			const char *interval_str)
{
	unsigned int interval;

	/* Don't schedule duplicate dumps if the dump command is given twice */
	if (bgp_dump->filename && strcmp(path, bgp_dump->filename) == 0
	    && type == bgp_dump->type) {
		if (interval_str) {
			if (bgp_dump->interval_str
			    && strcmp(bgp_dump->interval_str, interval_str)
				       == 0)
				return CMD_SUCCESS;
		} else {
			if (!bgp_dump->interval_str)
				return CMD_SUCCESS;
		}
	}

	/* Removing previous config */
	bgp_dump_unset(bgp_dump);

	if (interval_str) {
		/* Check interval string. */
		/* 解析间隔字符串；非法(得 0)则拒绝并报错返回 */
		interval = bgp_dump_parse_time(interval_str);
		if (interval == 0) {
			vty_out(vty, "Malformed interval string\n");
			return CMD_WARNING_CONFIG_FAILED;
		}

		/* Setting interval string */
		bgp_dump->interval_str =
			XSTRDUP(MTYPE_BGP_DUMP_STR, interval_str);
	} else {
		interval = 0;
	}

	/* Set type. */
	bgp_dump->type = type;

	/* Set interval */
	bgp_dump->interval = interval;

	/* Set file name. */
	bgp_dump->filename = XSTRDUP(MTYPE_BGP_DUMP_STR, path);

	/* Create interval thread. */
	bgp_dump_interval_add(bgp_dump, interval);

	/* This should be called when interval is expired. */
	bgp_dump_open_file(bgp_dump);

	return CMD_SUCCESS;
}

/* 【函数】bgp_dump_unset —— 清除某个 dump 通道的全部运行时状态：
 * 释放文件名、关闭文件、取消定时器、清零间隔、释放间隔字符串。
 * 由 bgp_dump_set(改配置前)与 bgp_dump_finish(退出时)调用。 */
static int bgp_dump_unset(struct bgp_dump *bgp_dump)
{
	/* Removing file name. */
	XFREE(MTYPE_BGP_DUMP_STR, bgp_dump->filename);

	/* Closing file. */
	if (bgp_dump->fp) {
		fclose(bgp_dump->fp);
		bgp_dump->fp = NULL;
	}

	/* Removing interval event. */
	event_cancel(&bgp_dump->t_interval);

	bgp_dump->interval = 0;

	/* Removing interval string. */
	XFREE(MTYPE_BGP_DUMP_STR, bgp_dump->interval_str);

	return CMD_SUCCESS;
}

/* 【DEFUN】dump_bgp_all —— `dump bgp <all|all-et|updates|updates-et|routes-mrt>
 * PATH [INTERVAL]` 命令实现。
 * 用 bgp_dump_type_map 把关键字解析成枚举，再据枚举选到对应的全局 bgp_dump 实例，
 * 取出可选间隔参数，交给 bgp_dump_set 落地。 */
DEFUN (dump_bgp_all,
       dump_bgp_all_cmd,
       "dump bgp <all|all-et|updates|updates-et|routes-mrt> PATH [INTERVAL]",
       "Dump packet\n"
       "BGP packet dump\n"
       "Dump all BGP packets\nDump all BGP packets (Extended Timestamp Header)\n"
       "Dump BGP updates only\nDump BGP updates only (Extended Timestamp Header)\n"
       "Dump whole BGP routing table\n"
       "Output filename\n"
       "Interval of output\n")
{
	int idx_dump_routes = 2;
	int idx_path = 3;
	int idx_interval = 4;
	int bgp_dump_type = 0;
	const char *interval = NULL;
	struct bgp_dump *bgp_dump_struct = NULL;
	const struct bgp_dump_type_map *map = NULL;

	for (map = bgp_dump_type_map; map->str; map++)
		if (strmatch(argv[idx_dump_routes]->text, map->str))
			bgp_dump_type = map->type;

	/* 按类别挑选目标通道(all / updates / routes) */
	switch (bgp_dump_type) {
	case BGP_DUMP_ALL:
	case BGP_DUMP_ALL_ET:
		bgp_dump_struct = &bgp_dump_all;
		break;
	case BGP_DUMP_UPDATES:
	case BGP_DUMP_UPDATES_ET:
		bgp_dump_struct = &bgp_dump_updates;
		break;
	case BGP_DUMP_ROUTES:
	default:
		bgp_dump_struct = &bgp_dump_routes;
		break;
	}

	/* When an interval is given */
	if (argc == idx_interval + 1)
		interval = argv[idx_interval]->arg;

	return bgp_dump_set(vty, bgp_dump_struct, bgp_dump_type,
			    argv[idx_path]->arg, interval);
}

/* 【DEFUN】no_dump_bgp_all —— `no dump bgp ...` 命令实现：解析类别后调用
 * bgp_dump_unset 清除对应通道。命令中的路径 / 间隔参数这里不参与匹配，仅用于
 * 命令行补全与语法一致。 */
DEFUN (no_dump_bgp_all,
       no_dump_bgp_all_cmd,
       "no dump bgp <all|all-et|updates|updates-et|routes-mrt> [PATH [INTERVAL]]",
       NO_STR
       "Stop dump packet\n"
       "Stop BGP packet dump\n"
       "Stop dump process all\n"
       "Stop dump process all-et\n"
       "Stop dump process updates\n"
       "Stop dump process updates-et\n"
       "Stop dump process route-mrt\n"
       "Output filename\n"
       "Interval of output\n")
{
	int idx_dump_routes = 3;
	int bgp_dump_type = 0;
	const struct bgp_dump_type_map *map = NULL;
	struct bgp_dump *bgp_dump_struct = NULL;

	for (map = bgp_dump_type_map; map->str; map++)
		if (strmatch(argv[idx_dump_routes]->text, map->str))
			bgp_dump_type = map->type;

	switch (bgp_dump_type) {
	case BGP_DUMP_ALL:
	case BGP_DUMP_ALL_ET:
		bgp_dump_struct = &bgp_dump_all;
		break;
	case BGP_DUMP_UPDATES:
	case BGP_DUMP_UPDATES_ET:
		bgp_dump_struct = &bgp_dump_updates;
		break;
	case BGP_DUMP_ROUTES:
	default:
		bgp_dump_struct = &bgp_dump_routes;
		break;
	}

	return bgp_dump_unset(bgp_dump_struct);
}

static int config_write_bgp_dump(struct vty *vty);
/* 【配置节点】DUMP_NODE：dump 相关配置的写回节点；config_write 用于
 * `show running-config` 时回显已配置的 dump 命令。 */
/* BGP node structure. */
static struct cmd_node bgp_dump_node = {
	.name = "dump",
	.node = DUMP_NODE,
	.prompt = "",
	.config_write = config_write_bgp_dump,
};

/* 【函数】config_write_bgp_dump —— 把三个通道中已配置的 dump 命令回显到
 * running-config(仅在 filename 已设置时输出)。 */
static int config_write_bgp_dump(struct vty *vty)
{
	/* all 通道：类型为 all / all-et，含间隔则一并回显 */
	if (bgp_dump_all.filename) {
		const char *type_str = "all";
		if (bgp_dump_all.type == BGP_DUMP_ALL_ET)
			type_str = "all-et";

		if (bgp_dump_all.interval_str)
			vty_out(vty, "dump bgp %s %s %s\n", type_str,
				bgp_dump_all.filename,
				bgp_dump_all.interval_str);
		else
			vty_out(vty, "dump bgp %s %s\n", type_str,
				bgp_dump_all.filename);
	}
	/* updates 通道：类型为 updates / updates-et */
	if (bgp_dump_updates.filename) {
		const char *type_str = "updates";
		if (bgp_dump_updates.type == BGP_DUMP_UPDATES_ET)
			type_str = "updates-et";

		if (bgp_dump_updates.interval_str)
			vty_out(vty, "dump bgp %s %s %s\n", type_str,
				bgp_dump_updates.filename,
				bgp_dump_updates.interval_str);
		else
			vty_out(vty, "dump bgp %s %s\n", type_str,
				bgp_dump_updates.filename);
	}
	/* routes-mrt 通道(无 et 变体) */
	if (bgp_dump_routes.filename) {
		if (bgp_dump_routes.interval_str)
			vty_out(vty, "dump bgp routes-mrt %s %s\n",
				bgp_dump_routes.filename,
				bgp_dump_routes.interval_str);
		else
			vty_out(vty, "dump bgp routes-mrt %s\n",
				bgp_dump_routes.filename);
	}
	return 0;
}

/* 【函数】bgp_dump_init —— dump 功能的启动初始化(进程启动时调用一次)。
 * 清零三个通道、分配全局编码缓冲、注册 DUMP_NODE 与 dump / no dump 命令，
 * 并注册两个 hook：bgp_packet_dump→bgp_dump_packet(报文事件)、
 * peer_status_changed→bgp_dump_state(状态变化)。 */
/* Initialize BGP packet dump functionality. */
void bgp_dump_init(void)
{
	memset(&bgp_dump_all, 0, sizeof(bgp_dump_all));
	memset(&bgp_dump_updates, 0, sizeof(bgp_dump_updates));
	memset(&bgp_dump_routes, 0, sizeof(bgp_dump_routes));

	bgp_dump_obuf =
		stream_new(BGP_MAX_PACKET_SIZE + BGP_MAX_PACKET_SIZE_OVERFLOW);

	install_node(&bgp_dump_node);

	install_element(CONFIG_NODE, &dump_bgp_all_cmd);
	install_element(CONFIG_NODE, &no_dump_bgp_all_cmd);

	hook_register(bgp_packet_dump, bgp_dump_packet);
	hook_register(peer_status_changed, bgp_dump_state);
}

/* 【函数】bgp_dump_finish —— 进程退出时的收尾：清空三个通道(关文件、取消定时器)、
 * 释放编码缓冲、注销两个 hook。与 bgp_dump_init 对称。 */
void bgp_dump_finish(void)
{
	bgp_dump_unset(&bgp_dump_all);
	bgp_dump_unset(&bgp_dump_updates);
	bgp_dump_unset(&bgp_dump_routes);

	stream_free(bgp_dump_obuf);
	bgp_dump_obuf = NULL;
	hook_unregister(bgp_packet_dump, bgp_dump_packet);
	hook_unregister(peer_status_changed, bgp_dump_state);
}
