// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP4 SNMP support
 * Copyright (C) 1999, 2000 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_snmp_bgp4.c —— BGP4-MIB（RFC4273）取值实现
 * ============================================================================
 * 【它解决什么问题】把 BGP4 会话（IPv4 单播时代语义）的状态，按 MIB 定义填成一张张
 *   SNMP 表供网管 get / get_next 轮询；并在邻居进入 Established、或 FSM 回退时发 trap。
 *
 * 【Net-SNMP 集成骨架（读本文件前务必先懂这个）】
 *   本文件通过 FRR 的 smux 层注册“变量表”。每张表的每一列在 bgp_variables[] 里是一行：
 *       {列号 magic, 类型(INTEGER/IPADDRESS/COUNTER32...), 读或写(RONLY/RWRITE),
 *        回调函数, OID 段数, {OID 路径段...}}
 *   网管查询时，Net-SNMP / smux 会按 OID 找到对应的“回调函数 + magic”，再调用它：
 *       uint8_t *fn(struct variable *v, oid name[], size_t *length,
 *                   int exact, size_t *var_len, WriteMethod **write_method)
 *   - v->magic 告诉你“当前问的是这一行的哪一列”；
 *   - exact 为真表示精确 get，为假表示 get_next（需要返回“下一个”条目）；
 *   - 返回值指向数值缓冲，*var_len 给出长度；返回 NULL 表示无此实例。
 *   读任何回调时的固定顺序：先 smux_header_*（做 OID 合法性 / 长度校验）→ 按 exact
 *   定位实例 → 最后 switch(v->magic) 逐列取数。表型 MIB（如 bgpPeerTable）用
 *   smux_header_table + 一个 *_lookup 辅助函数做按索引遍历 / 查找；标量 MIB 用
 *   smux_header_generic。
 *
 * 【本文件覆盖的 MIB 对象】
 *   标量 : bgpVersion / bgpLocalAs / bgpIdentifier（bgpVersion 恒为版本 4 的位图）
 *   表   : bgpPeerTable（邻居状态 / 计数）、bgp4PathAttrTable（IPv4 路径属性）
 *   废弃 : bgpRcvdPathAttrTable / bgpPathAttrTable（返回 NULL 的占位）
 *   trap : bgpEstablished / bgpBackwardTransition
 *
 * 【与 bgp_snmp_bgp4v2.c 的差别】本文件是“老 MIB”：AS 2 字节、地址仅 IPv4、索引是
 *   点分地址。BGP4V2-MIB（另一文件）支持 4 字节 AS、IPv6、无类前缀，且区分 VRF。
 *
 * 【推荐阅读顺序】bgp_variables[] 变量表（看清列与回调的对应）→ bgpPeerTable 回调 →
 *   bgp4PathAttrTable 回调 → 末尾两个 trap 函数与 init。
 * ============================================================================
 */

#include <zebra.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include "if.h"
#include "log.h"
#include "prefix.h"
#include "command.h"
#include "frrevent.h"
#include "smux.h"
#include "filter.h"
#include "hook.h"
#include "libfrr.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_fsm.h"
#include "bgpd/bgp_snmp.h"
#include "bgpd/bgp_snmp_bgp4.h"
#include "bgpd/bgp_mplsvpn_snmp.h"

/* 【宏】声明本文件回调返回值要用的 static 缓冲（snmp_int_val 等），见 lib/smux.h。
 * 所有 SNMP_INTEGER / SNMP_IPADDRESS ... 宏都写这些缓冲并返回其地址。 */
/* Declare static local variables for convenience. */
SNMP_LOCAL_VARIABLES

/* 【OID 常量】bgp_oid：整个 bgp 子树；bgp_trap_oid：bgp.0（trap 的企业 OID 前缀）。 */
/* BGP-MIB instances. */
static oid bgp_oid[] = {BGP4MIB};
static oid bgp_trap_oid[] = {BGP4MIB, 0};

/* IP address 0.0.0.0. */
/* 【哨兵】0.0.0.0：当 peer 尚无本地 / 远端地址时用于占位返回。 */
static struct in_addr bgp_empty_addr = {.s_addr = 0};

/* 【标量】bgpVersion：返回本 daemon 支持的 BGP 版本位图。zebra / bgpd 只支持版本 4，
 * 所以返回 0x80 >> (4-1)（第 4 位置 1），长度为 1 字节的 OctetString。 */
static uint8_t *bgpVersion(struct variable *v, oid name[], size_t *length,
			   int exact, size_t *var_len,
			   WriteMethod **write_method)
{
	static uint8_t version;

	if (smux_header_generic(v, name, length, exact, var_len,
				write_method) == MATCH_FAILED)
		return NULL;

	/* Return BGP version.  Zebra bgpd only support version 4. */
	version = (0x80 >> (BGP_VERSION_4 - 1));

	/* Return octet string length 1. */
	*var_len = 1;
	return &version;
}

/* 【标量】bgpLocalAs：返回默认 BGP 实例的本地 AS 号（bgp->as）。 */
static uint8_t *bgpLocalAs(struct variable *v, oid name[], size_t *length,
			   int exact, size_t *var_len,
			   WriteMethod **write_method)
{
	struct bgp *bgp;

	if (smux_header_generic(v, name, length, exact, var_len,
				write_method) == MATCH_FAILED)
		return NULL;

	/* Get BGP structure. */
	bgp = bgp_get_default();
	if (!bgp)
		return NULL;

	return SNMP_INTEGER(bgp->as);
}

/* 【辅助】在“所有 BGP 实例的所有邻居”里，按邻居 IPv4 地址精确查找 peer。
 * 只比较 AF_INET 的邻居；返回匹配的 peer 或 NULL。 */
static struct peer *peer_lookup_addr_ipv4(struct in_addr *src)
{
	struct bgp *bgp;
	struct peer *peer;
	struct listnode *node;
	struct listnode *bgpnode;

	for (ALL_LIST_ELEMENTS_RO(bm->bgp, bgpnode, bgp)) {
		for (ALL_LIST_ELEMENTS_RO(bgp->peer, node, peer)) {
			if (sockunion_family(&peer->connection->su) != AF_INET)
				continue;

			if (sockunion2ip(&peer->connection->su) == src->s_addr)
				return peer;
		}
	}

	return NULL;
}

/* 【辅助】get_next 用：在所有 IPv4 邻居里，找出“地址严格大于 src 的最小者”，
 * 以实现按地址递增的字典序遍历。找到后把 src 更新为该地址并返回该 peer。 */
static struct peer *bgp_peer_lookup_next(struct in_addr *src)
{
	struct bgp *bgp;
	struct peer *peer;
	struct peer *next_peer = NULL;
	struct listnode *node;
	struct listnode *bgpnode;

	for (ALL_LIST_ELEMENTS_RO(bm->bgp, bgpnode, bgp)) {
		for (ALL_LIST_ELEMENTS_RO(bgp->peer, node, peer)) {
			if (sockunion_family(&peer->connection->su) != AF_INET)
				continue;
			if (ntohl(sockunion2ip(&peer->connection->su)) <=
			    ntohl(src->s_addr))
				continue;

			if (!next_peer ||
			    ntohl(sockunion2ip(&next_peer->connection->su)) >
				    ntohl(sockunion2ip(&peer->connection->su))) {
				next_peer = peer;
			}
		}
	}

	if (next_peer) {
		src->s_addr = sockunion2ip(&next_peer->connection->su);
		return next_peer;
	}

	return NULL;
}

/* 【常量】bgpPeerTable 索引前缀长度：到列号为止共 10 段，其后接 4 字节地址。
 * 即 1.3.6.1.2.1.15.3.1.<列号>.<ipv4 四段> 的前 10 段。 */
/* 1.3.6.1.2.1.15.3.1.x  = 10 */
#define PEERTAB_NAMELEN 10

/* 【辅助】bgpPeerTable 的实例定位：
 *   exact   : OID 末尾 4 字节是邻居地址，交给 peer_lookup_addr_ipv4 精确查找；
 *   get_next: OID 末尾是“起点地址”，交给 bgp_peer_lookup_next 找下一个，并把找到的
 *             地址回填进 name[]、更新 *length。返回 peer 或 NULL。 */
static struct peer *bgpPeerTable_lookup(struct variable *v, oid name[],
					size_t *length, struct in_addr *addr,
					int exact)
{
	struct peer *peer = NULL;
	size_t namelen = v ? v->namelen : PEERTAB_NAMELEN;
	int len;

	if (exact) {
		/* Check the length. */
		if (*length - namelen != sizeof(struct in_addr))
			return NULL;

		oid2in_addr(name + namelen, IN_ADDR_SIZE, addr);

		peer = peer_lookup_addr_ipv4(addr);
		return peer;
	} else {
		len = *length - namelen;
		if (len > 4)
			len = 4;

		oid2in_addr(name + namelen, len, addr);

		peer = bgp_peer_lookup_next(addr);

		if (peer == NULL)
			return NULL;

		oid_copy_in_addr(name + namelen, addr);
		*length = sizeof(struct in_addr) + namelen;

		return peer;
	}
	return NULL;
}

/* 【写回调】bgpPeerTable 若干可写列的 SET 回调（变量表里标 RWRITE 的列）。
 * Net-SNMP 对一次 SET 会分阶段回调（RESERVE / COMMIT ...），这里只在 COMMIT 阶段
 * 真正生效；action != SNMP_MSG_INTERNAL_SET_COMMIT 时直接返回 NOERROR。
 * 用 name[PEERTAB_NAMELEN-1]（OID 的列号段）判断改的是哪一列：
 *   bgpPeerAdminStatus                : stop → 投递 BGP_Stop 事件；start → 无操作
 *   bgpPeerConnectRetryInterval       : 置连接定时器标志并写 peer->connect / v_connect
 *   bgpPeerHoldTimeConfigured         : 置定时器标志并写 peer->holdtime / v_holdtime
 *   bgpPeerKeepAliveConfigured        : 置定时器标志并写 peer->keepalive / v_keepalive
 *   bgpPeerMinRouteAdvertisementInterval : 写 peer->v_routeadv
 * 疑点：此处 switch 用 OID 的列号段（name[9]），而变量表里最后两列的 v->magic 与 OID
 * 列号相差 1（见 bgp_snmp_bgp4.h），可能使 MinRouteAdvInterval 分支永不命中；
 * 仅作标注，不改动既有代码。 */
/* BGP write methods. */
static int write_bgpPeerTable(int action, uint8_t *var_val,
			      uint8_t var_val_type, size_t var_val_len,
			      uint8_t *statP, oid *name, size_t length)
{
	struct in_addr addr;
	struct peer *peer;
	struct peer_connection *connection;
	long intval;

	if (var_val_type != ASN_INTEGER) {
		return SNMP_ERR_WRONGTYPE;
	}
	if (var_val_len != sizeof(long)) {
		return SNMP_ERR_WRONGLENGTH;
	}

	intval = *(long *)var_val;

	memset(&addr, 0, sizeof(addr));

	peer = bgpPeerTable_lookup(NULL, name, &length, &addr, 1);
	if (!peer)
		return SNMP_ERR_NOSUCHNAME;

	connection = peer->connection;

	if (action != SNMP_MSG_INTERNAL_SET_COMMIT)
		return SNMP_ERR_NOERROR;

	zlog_info("%s: SNMP write .%ld = %ld", peer->host,
		  (long)name[PEERTAB_NAMELEN - 1], intval);

	switch (name[PEERTAB_NAMELEN - 1]) {
	case BGPPEERADMINSTATUS:
#define BGP_PeerAdmin_stop 1
#define BGP_PeerAdmin_start 2
		/* When the peer is established,   */
		if (intval == BGP_PeerAdmin_stop)
			BGP_EVENT_ADD(connection, BGP_Stop);
		else if (intval == BGP_PeerAdmin_start)
			; /* Do nothing. */
		else
			return SNMP_ERR_NOSUCHNAME;
		break;
	case BGPPEERCONNECTRETRYINTERVAL:
		peer_flag_set(peer, PEER_FLAG_TIMER_CONNECT);
		peer->connect = intval;
		peer->v_connect = intval;
		break;
	case BGPPEERHOLDTIMECONFIGURED:
		peer_flag_set(peer, PEER_FLAG_TIMER);
		peer->holdtime = intval;
		peer->v_holdtime = intval;
		break;
	case BGPPEERKEEPALIVECONFIGURED:
		peer_flag_set(peer, PEER_FLAG_TIMER);
		peer->keepalive = intval;
		peer->v_keepalive = intval;
		break;
	case BGPPEERMINROUTEADVERTISEMENTINTERVAL:
		peer->v_routeadv = intval;
		break;
	}
	return SNMP_ERR_NOERROR;
}

/* 【表】bgpPeerTable 读取回调：逐列取邻居状态 / 计数。先 smux_header_table 做表校验，
 * 再 bgpPeerTable_lookup 定位实例，最后 switch(v->magic) 按列返回。各列取数来源：
 *   Identifier→peer->remote_id; State→connection->status; AdminStatus→SHUTDOWN 标志;
 *   Local / Remote Addr / Port→connection->su_local / su_remote; RemoteAs→peer->as;
 *   In / OutUpdates→原子计数 update_in / update_out; TotalMessages→PEER_TOTAL_RX / TX;
 *   LastError→notify.code / subcode（2 字节）; FsmEstablishedTime→monotime()-uptime;
 *   ConnectRetry / HoldTime / KeepAlive→peer->v_*（当前值）或 peer->*（配置值，
 *     依据 PEER_FLAG_TIMER 决定取哪个）; InUpdateElapsedTime→monotime()-update_time。
 * 标 RWRITE 的列同时设置 *write_method = write_bgpPeerTable 以支持 SET。 */
static uint8_t *bgpPeerTable(struct variable *v, oid name[], size_t *length,
			     int exact, size_t *var_len,
			     WriteMethod **write_method)
{
	static struct in_addr addr;
	struct peer *peer;
	uint32_t ui, uo;

	if (smux_header_table(v, name, length, exact, var_len, write_method) ==
	    MATCH_FAILED)
		return NULL;
	memset(&addr, 0, sizeof(addr));

	peer = bgpPeerTable_lookup(v, name, length, &addr, exact);
	if (!peer)
		return NULL;

	switch (v->magic) {
	case BGPPEERIDENTIFIER:
		return SNMP_IPADDRESS(peer->remote_id);
	case BGPPEERSTATE:
		return SNMP_INTEGER(peer->connection->status);
	case BGPPEERADMINSTATUS:
		*write_method = write_bgpPeerTable;
#define BGP_PeerAdmin_stop 1
#define BGP_PeerAdmin_start 2
		if (CHECK_FLAG(peer->flags, PEER_FLAG_SHUTDOWN))
			return SNMP_INTEGER(BGP_PeerAdmin_stop);
		else
			return SNMP_INTEGER(BGP_PeerAdmin_start);
	case BGPPEERNEGOTIATEDVERSION:
		return SNMP_INTEGER(BGP_VERSION_4);
	case BGPPEERLOCALADDR:
		if (peer->connection->su_local)
			return SNMP_IPADDRESS(peer->connection->su_local->sin.sin_addr);
		else
			return SNMP_IPADDRESS(bgp_empty_addr);
	case BGPPEERLOCALPORT:
		if (peer->connection->su_local)
			return SNMP_INTEGER(ntohs(peer->connection->su_local->sin.sin_port));
		else
			return SNMP_INTEGER(0);
	case BGPPEERREMOTEADDR:
		if (peer->connection->su_remote)
			return SNMP_IPADDRESS(peer->connection->su_remote->sin.sin_addr);
		else
			return SNMP_IPADDRESS(bgp_empty_addr);
	case BGPPEERREMOTEPORT:
		if (peer->connection->su_remote)
			return SNMP_INTEGER(ntohs(peer->connection->su_remote->sin.sin_port));
		else
			return SNMP_INTEGER(0);
	case BGPPEERREMOTEAS:
		return SNMP_INTEGER(peer->as);
	case BGPPEERINUPDATES:
		ui = atomic_load_explicit(&peer->update_in,
					  memory_order_relaxed);
		return SNMP_INTEGER(ui);
	case BGPPEEROUTUPDATES:
		uo = atomic_load_explicit(&peer->update_out,
					  memory_order_relaxed);
		return SNMP_INTEGER(uo);
	case BGPPEERINTOTALMESSAGES:
		return SNMP_INTEGER(PEER_TOTAL_RX(peer));
	case BGPPEEROUTTOTALMESSAGES:
		return SNMP_INTEGER(PEER_TOTAL_TX(peer));
	case BGPPEERLASTERROR: {
		static uint8_t lasterror[2];
		lasterror[0] = peer->notify.code;
		lasterror[1] = peer->notify.subcode;
		*var_len = 2;
		return (uint8_t *)&lasterror;
	}
	case BGPPEERFSMESTABLISHEDTRANSITIONS:
		return SNMP_INTEGER(peer->established);
	case BGPPEERFSMESTABLISHEDTIME:
		if (peer->uptime == 0)
			return SNMP_INTEGER(0);
		else
			return SNMP_INTEGER(monotime(NULL) - peer->uptime);
	case BGPPEERCONNECTRETRYINTERVAL:
		*write_method = write_bgpPeerTable;
		return SNMP_INTEGER(peer->v_connect);
	case BGPPEERHOLDTIME:
		return SNMP_INTEGER(peer->v_holdtime);
	case BGPPEERKEEPALIVE:
		return SNMP_INTEGER(peer->v_keepalive);
	case BGPPEERHOLDTIMECONFIGURED:
		*write_method = write_bgpPeerTable;
		if (CHECK_FLAG(peer->flags, PEER_FLAG_TIMER))
			return SNMP_INTEGER(peer->holdtime);
		else
			return SNMP_INTEGER(peer->v_holdtime);
	case BGPPEERKEEPALIVECONFIGURED:
		*write_method = write_bgpPeerTable;
		if (CHECK_FLAG(peer->flags, PEER_FLAG_TIMER))
			return SNMP_INTEGER(peer->keepalive);
		else
			return SNMP_INTEGER(peer->v_keepalive);
	case BGPPEERMINROUTEADVERTISEMENTINTERVAL:
		*write_method = write_bgpPeerTable;
		return SNMP_INTEGER(peer->v_routeadv);
	case BGPPEERINUPDATEELAPSEDTIME:
		if (peer->update_time == 0)
			return SNMP_INTEGER(0);
		else
			return SNMP_INTEGER(monotime(NULL) - peer->update_time);
	default:
		return NULL;
	}
	return NULL;
}

/* 【标量】bgpIdentifier：返回默认实例的 BGP Router ID（bgp->router_id）。 */
static uint8_t *bgpIdentifier(struct variable *v, oid name[], size_t *length,
			      int exact, size_t *var_len,
			      WriteMethod **write_method)
{
	struct bgp *bgp;

	if (smux_header_generic(v, name, length, exact, var_len,
				write_method) == MATCH_FAILED)
		return NULL;

	bgp = bgp_get_default();
	if (!bgp)
		return NULL;

	return SNMP_IPADDRESS(bgp->router_id);
}

/* 【废弃表】bgpPathAttrTable 的回调：BGP3 时代遗留，功能已被 bgp4PathAttrTable 取代，
 * 永远返回 NULL（MIB 中仍保留这些列以兼容）。 */
static uint8_t *bgpRcvdPathAttrTable(struct variable *v, oid name[],
				     size_t *length, int exact, size_t *var_len,
				     WriteMethod **write_method)
{
	/* Received Path Attribute Table.  This table contains, one entry
	   per path to a network, path attributes received from all peers
	   running BGP version 3 or less.  This table is obsolete, having
	   been replaced in functionality with the bgp4PathAttrTable.  */
	return NULL;
}

/* 【辅助】bgp4PathAttrTable 的实例定位与遍历。索引由三段构成：
 *   <前缀 4 字节><前缀长度 1 字节><邻居地址 4 字节>（BGP_PATHATTR_ENTRY_OFFSET）。
 *   exact   : 解析出前缀与邻居，在 AFI_IP / SAFI_UNICAST 路由表里找到该 dest，再在
 *             该前缀的多条 path 中挑出 peer 地址匹配的那条；
 *   get_next: 从起点前缀 / 邻居出发找“下一个”：同一 dest 内选 peer 地址大于当前的
 *             最小者，否则用 bgp_route_next 走到下一个前缀，并回填 name[] 与 *length。
 * 返回 bgp_path_info 或 NULL。 */
static struct bgp_path_info *bgp4PathAttrLookup(struct variable *v, oid name[],
						size_t *length, struct bgp *bgp,
						struct prefix_ipv4 *addr,
						int exact)
{
	oid *offset;
	int offsetlen;
	struct bgp_path_info *path;
	struct bgp_path_info *min;
	struct bgp_dest *dest;
	union sockunion su;
	unsigned int len;
	struct in_addr paddr;

	sockunion_init(&su);

#define BGP_PATHATTR_ENTRY_OFFSET (IN_ADDR_SIZE + 1 + IN_ADDR_SIZE)

	if (exact) {
		if (*length - v->namelen != BGP_PATHATTR_ENTRY_OFFSET)
			return NULL;

		/* Set OID offset for prefix. */
		offset = name + v->namelen;
		oid2in_addr(offset, IN_ADDR_SIZE, &addr->prefix);
		offset += IN_ADDR_SIZE;

		/* Prefix length. */
		addr->prefixlen = *offset;
		offset++;

		/* Peer address. */
		su.sin.sin_family = AF_INET;
		oid2in_addr(offset, IN_ADDR_SIZE, &su.sin.sin_addr);

		/* Lookup node. */
		dest = bgp_node_lookup(bgp->rib[AFI_IP][SAFI_UNICAST],
				       (struct prefix *)addr);
		if (dest) {
			for (path = bgp_dest_get_bgp_path_info(dest); path;
			     path = path->next)
				if (sockunion_same(&path->peer->connection->su,
						   &su))
					return path;

			bgp_dest_unlock_node(dest);
		}
	} else {
		offset = name + v->namelen;
		offsetlen = *length - v->namelen;
		len = offsetlen;

		if (offsetlen == 0)
			dest = bgp_table_top(bgp->rib[AFI_IP][SAFI_UNICAST]);
		else {
			if (len > IN_ADDR_SIZE)
				len = IN_ADDR_SIZE;

			oid2in_addr(offset, len, &addr->prefix);

			offset += IN_ADDR_SIZE;
			offsetlen -= IN_ADDR_SIZE;

			if (offsetlen > 0)
				addr->prefixlen = *offset;
			else
				addr->prefixlen = len * 8;

			dest = bgp_node_get(bgp->rib[AFI_IP][SAFI_UNICAST],
					    (struct prefix *)addr);

			offset++;
			offsetlen--;
		}

		if (offsetlen > 0) {
			len = offsetlen;
			if (len > IN_ADDR_SIZE)
				len = IN_ADDR_SIZE;

			oid2in_addr(offset, len, &paddr);
		} else
			paddr.s_addr = INADDR_ANY;

		if (!dest)
			return NULL;

		do {
			min = NULL;

			for (path = bgp_dest_get_bgp_path_info(dest); path;
			     path = path->next) {
				if (path->peer->connection->su.sin.sin_family ==
					    AF_INET &&
				    ntohl(paddr.s_addr) <
					    ntohl(path->peer->connection->su.sin
							  .sin_addr.s_addr)) {
					if (min) {
						if (ntohl(path->peer->connection
								  ->su.sin
								  .sin_addr
								  .s_addr) <
						    ntohl(min->peer->connection
								  ->su.sin
								  .sin_addr
								  .s_addr))
							min = path;
					} else
						min = path;
				}
			}

			if (min) {
				const struct prefix *rn_p =
					bgp_dest_get_prefix(dest);

				*length =
					v->namelen + BGP_PATHATTR_ENTRY_OFFSET;

				offset = name + v->namelen;
				oid_copy_in_addr(offset, &rn_p->u.prefix4);
				offset += IN_ADDR_SIZE;
				*offset = rn_p->prefixlen;
				offset++;
				oid_copy_in_addr(offset,
						 &min->peer->connection->su.sin
							  .sin_addr);
				addr->prefix = rn_p->u.prefix4;
				addr->prefixlen = rn_p->prefixlen;

				bgp_dest_unlock_node(dest);

				return min;
			}

			paddr.s_addr = INADDR_ANY;
		} while ((dest = bgp_route_next(dest)) != NULL);
	}
	return NULL;
}

/* 【表】bgp4PathAttrTable 读取回调：把一条 path 的 BGP 属性逐列返回。先取默认实例的
 * bgp，再 bgp4PathAttrLookup 定位 path，然后 switch(v->magic)：
 *   Peer / Prefix / PrefixLen→索引里的值; Origin→attr->origin;
 *   ASPathSegment→aspath_snmp_pathseg; NextHop→attr->nexthop; MultiExitDisc→attr->med;
 *   LocalPref→attr->local_pref; AggregatorAS / Addr→attr->aggregator_*;
 *   AtomicAggregate / CalcLocalPref→常量 1 / -1（本表不真正计算，取固定值）;
 *   Best→BGP_PATH_SELECTED 标志; Unknown→长度 0。 */
static uint8_t *bgp4PathAttrTable(struct variable *v, oid name[],
				  size_t *length, int exact, size_t *var_len,
				  WriteMethod **write_method)
{
	struct bgp *bgp;
	struct bgp_path_info *path;
	struct prefix_ipv4 addr;

	bgp = bgp_get_default();
	if (!bgp)
		return NULL;

	if (smux_header_table(v, name, length, exact, var_len, write_method) ==
	    MATCH_FAILED)
		return NULL;
	memset(&addr, 0, sizeof(addr));

	path = bgp4PathAttrLookup(v, name, length, bgp, &addr, exact);
	if (!path)
		return NULL;

	switch (v->magic) {
	case BGP4PATHATTRPEER: /* 1 */
		return SNMP_IPADDRESS(path->peer->connection->su.sin.sin_addr);
	case BGP4PATHATTRIPADDRPREFIXLEN: /* 2 */
		return SNMP_INTEGER(addr.prefixlen);
	case BGP4PATHATTRIPADDRPREFIX: /* 3 */
		return SNMP_IPADDRESS(addr.prefix);
	case BGP4PATHATTRORIGIN: /* 4 */
		return SNMP_INTEGER(path->attr->origin);
	case BGP4PATHATTRASPATHSEGMENT: /* 5 */
		return aspath_snmp_pathseg(path->attr->aspath, var_len);
	case BGP4PATHATTRNEXTHOP: /* 6 */
		return SNMP_IPADDRESS(path->attr->nexthop);
	case BGP4PATHATTRMULTIEXITDISC: /* 7 */
		return SNMP_INTEGER(path->attr->med);
	case BGP4PATHATTRLOCALPREF: /* 8 */
		return SNMP_INTEGER(path->attr->local_pref);
	case BGP4PATHATTRATOMICAGGREGATE: /* 9 */
		return SNMP_INTEGER(1);
	case BGP4PATHATTRAGGREGATORAS: /* 10 */
		return SNMP_INTEGER(path->attr->aggregator_as);
	case BGP4PATHATTRAGGREGATORADDR: /* 11 */
		return SNMP_IPADDRESS(path->attr->aggregator_addr);
	case BGP4PATHATTRCALCLOCALPREF: /* 12 */
		return SNMP_INTEGER(-1);
	case BGP4PATHATTRBEST: /* 13 */
#define BGP4_PathAttrBest_false 1
#define BGP4_PathAttrBest_true 2
		if (CHECK_FLAG(path->flags, BGP_PATH_SELECTED))
			return SNMP_INTEGER(BGP4_PathAttrBest_true);
		else
			return SNMP_INTEGER(BGP4_PathAttrBest_false);
	case BGP4PATHATTRUNKNOWN: /* 14 */
		*var_len = 0;
		return NULL;
	}
	return NULL;
}

/* 【trap 载荷】发 trap 时附带的对象列表：{namelen, {OID 段}}。namelen=3 表示 OID 段
 * 为 {3,1,col}（相对 bgp_oid 前缀），即选中 bgpPeerTable 的三列：
 * RemoteAddr / LastError / State。 */
/* BGP Traps. */
static struct trap_object bgpTrapList[] = {{3, {3, 1, BGPPEERREMOTEADDR}},
					   {3, {3, 1, BGPPEERLASTERROR}},
					   {3, {3, 1, BGPPEERSTATE}}};

/* 【变量表】BGP4-MIB 的完整注册表：每行 {列号, 类型, 读写, 回调, OID 段数, {OID 段}}。
 * 类型宏见 bgp_snmp.h（INTEGER/IPADDRESS/COUNTER32/GAUGE32/OCTET_STRING...）。
 * RWRITE 表示该列可 SET（会走对应回调设置的 write_method）。 */
static struct variable bgp_variables[] = {
	/* BGP version. */
	{BGPVERSION, OCTET_STRING, RONLY, bgpVersion, 1, {1}},
	/* BGP local AS. */
	{BGPLOCALAS, INTEGER, RONLY, bgpLocalAs, 1, {2}},
	/* BGP peer table. */
	{BGPPEERIDENTIFIER, IPADDRESS, RONLY, bgpPeerTable, 3, {3, 1, 1}},
	{BGPPEERSTATE, INTEGER, RONLY, bgpPeerTable, 3, {3, 1, 2}},
	{BGPPEERADMINSTATUS, INTEGER, RWRITE, bgpPeerTable, 3, {3, 1, 3}},
	{BGPPEERNEGOTIATEDVERSION,
	 INTEGER32,
	 RONLY,
	 bgpPeerTable,
	 3,
	 {3, 1, 4}},
	{BGPPEERLOCALADDR, IPADDRESS, RONLY, bgpPeerTable, 3, {3, 1, 5}},
	{BGPPEERLOCALPORT, INTEGER, RONLY, bgpPeerTable, 3, {3, 1, 6}},
	{BGPPEERREMOTEADDR, IPADDRESS, RONLY, bgpPeerTable, 3, {3, 1, 7}},
	{BGPPEERREMOTEPORT, INTEGER, RONLY, bgpPeerTable, 3, {3, 1, 8}},
	{BGPPEERREMOTEAS, INTEGER, RONLY, bgpPeerTable, 3, {3, 1, 9}},
	{BGPPEERINUPDATES, COUNTER32, RONLY, bgpPeerTable, 3, {3, 1, 10}},
	{BGPPEEROUTUPDATES, COUNTER32, RONLY, bgpPeerTable, 3, {3, 1, 11}},
	{BGPPEERINTOTALMESSAGES, COUNTER32, RONLY, bgpPeerTable, 3, {3, 1, 12}},
	{BGPPEEROUTTOTALMESSAGES,
	 COUNTER32,
	 RONLY,
	 bgpPeerTable,
	 3,
	 {3, 1, 13}},
	{BGPPEERLASTERROR, OCTET_STRING, RONLY, bgpPeerTable, 3, {3, 1, 14}},
	{BGPPEERFSMESTABLISHEDTRANSITIONS,
	 COUNTER32,
	 RONLY,
	 bgpPeerTable,
	 3,
	 {3, 1, 15}},
	{BGPPEERFSMESTABLISHEDTIME,
	 GAUGE32,
	 RONLY,
	 bgpPeerTable,
	 3,
	 {3, 1, 16}},
	{BGPPEERCONNECTRETRYINTERVAL,
	 INTEGER,
	 RWRITE,
	 bgpPeerTable,
	 3,
	 {3, 1, 17}},
	{BGPPEERHOLDTIME, INTEGER, RONLY, bgpPeerTable, 3, {3, 1, 18}},
	{BGPPEERKEEPALIVE, INTEGER, RONLY, bgpPeerTable, 3, {3, 1, 19}},
	{BGPPEERHOLDTIMECONFIGURED,
	 INTEGER,
	 RWRITE,
	 bgpPeerTable,
	 3,
	 {3, 1, 20}},
	{BGPPEERKEEPALIVECONFIGURED,
	 INTEGER,
	 RWRITE,
	 bgpPeerTable,
	 3,
	 {3, 1, 21}},
	{BGPPEERMINROUTEADVERTISEMENTINTERVAL,
	 INTEGER,
	 RWRITE,
	 bgpPeerTable,
	 3,
	 {3, 1, 23}},
	{BGPPEERINUPDATEELAPSEDTIME,
	 GAUGE32,
	 RONLY,
	 bgpPeerTable,
	 3,
	 {3, 1, 24}},
	/* BGP identifier. */
	{BGPIDENTIFIER, IPADDRESS, RONLY, bgpIdentifier, 1, {4}},
	/* BGP received path attribute table. */
	{BGPPATHATTRPEER, IPADDRESS, RONLY, bgpRcvdPathAttrTable, 3, {5, 1, 1}},
	{BGPPATHATTRDESTNETWORK,
	 IPADDRESS,
	 RONLY,
	 bgpRcvdPathAttrTable,
	 3,
	 {5, 1, 2}},
	{BGPPATHATTRORIGIN, INTEGER, RONLY, bgpRcvdPathAttrTable, 3, {5, 1, 3}},
	{BGPPATHATTRASPATH,
	 OCTET_STRING,
	 RONLY,
	 bgpRcvdPathAttrTable,
	 3,
	 {5, 1, 4}},
	{BGPPATHATTRNEXTHOP,
	 IPADDRESS,
	 RONLY,
	 bgpRcvdPathAttrTable,
	 3,
	 {5, 1, 5}},
	{BGPPATHATTRINTERASMETRIC,
	 INTEGER32,
	 RONLY,
	 bgpRcvdPathAttrTable,
	 3,
	 {5, 1, 6}},
	/* BGP-4 received path attribute table. */
	{BGP4PATHATTRPEER, IPADDRESS, RONLY, bgp4PathAttrTable, 3, {6, 1, 1}},
	{BGP4PATHATTRIPADDRPREFIXLEN,
	 INTEGER,
	 RONLY,
	 bgp4PathAttrTable,
	 3,
	 {6, 1, 2}},
	{BGP4PATHATTRIPADDRPREFIX,
	 IPADDRESS,
	 RONLY,
	 bgp4PathAttrTable,
	 3,
	 {6, 1, 3}},
	{BGP4PATHATTRORIGIN, INTEGER, RONLY, bgp4PathAttrTable, 3, {6, 1, 4}},
	{BGP4PATHATTRASPATHSEGMENT,
	 OCTET_STRING,
	 RONLY,
	 bgp4PathAttrTable,
	 3,
	 {6, 1, 5}},
	{BGP4PATHATTRNEXTHOP,
	 IPADDRESS,
	 RONLY,
	 bgp4PathAttrTable,
	 3,
	 {6, 1, 6}},
	{BGP4PATHATTRMULTIEXITDISC,
	 INTEGER,
	 RONLY,
	 bgp4PathAttrTable,
	 3,
	 {6, 1, 7}},
	{BGP4PATHATTRLOCALPREF,
	 INTEGER,
	 RONLY,
	 bgp4PathAttrTable,
	 3,
	 {6, 1, 8}},
	{BGP4PATHATTRATOMICAGGREGATE,
	 INTEGER,
	 RONLY,
	 bgp4PathAttrTable,
	 3,
	 {6, 1, 9}},
	{BGP4PATHATTRAGGREGATORAS,
	 INTEGER,
	 RONLY,
	 bgp4PathAttrTable,
	 3,
	 {6, 1, 10}},
	{BGP4PATHATTRAGGREGATORADDR,
	 IPADDRESS,
	 RONLY,
	 bgp4PathAttrTable,
	 3,
	 {6, 1, 11}},
	{BGP4PATHATTRCALCLOCALPREF,
	 INTEGER,
	 RONLY,
	 bgp4PathAttrTable,
	 3,
	 {6, 1, 12}},
	{BGP4PATHATTRBEST, INTEGER, RONLY, bgp4PathAttrTable, 3, {6, 1, 13}},
	{BGP4PATHATTRUNKNOWN,
	 OCTET_STRING,
	 RONLY,
	 bgp4PathAttrTable,
	 3,
	 {6, 1, 14}},
};

/* 【trap】bgp4TrapEstablished：邻居刚进 Established 时发 bgpEstablished trap。
 * 只在 ostatus==OpenConfirm 且当前已 Established 时发（确保是“刚进入”那一刻）。
 * index 为邻居地址（4 段 OID），smux_trap 会把它接到 trap 载荷对象的索引上。 */
int bgp4TrapEstablished(struct peer *peer)
{
	int ret;
	struct in_addr addr;
	oid index[sizeof(oid) * IN_ADDR_SIZE];
	struct peer_connection *connection = peer->connection;

	/* Check if this peer just went to Established */
	if ((connection->ostatus != OpenConfirm) ||
	    !(peer_established(connection)))
		return 0;

	ret = inet_aton(peer->host, &addr);
	if (ret == 0)
		return 0;

	oid_copy_in_addr(index, &addr);

	smux_trap(bgp_variables, array_size(bgp_variables), bgp_trap_oid,
		  array_size(bgp_trap_oid), bgp_oid,
		  sizeof(bgp_oid) / sizeof(oid), index, IN_ADDR_SIZE,
		  bgpTrapList, array_size(bgpTrapList), BGPESTABLISHED);
	return 0;
}

/* 【trap】bgp4TrapBackwardTransition：FSM 回退时发 bgpBackwardTransition trap。
 * 与上面类似，但不需要“刚进入”判断（回退事件本身就是触发点）。 */
int bgp4TrapBackwardTransition(struct peer *peer)
{
	int ret;
	struct in_addr addr;
	oid index[sizeof(oid) * IN_ADDR_SIZE];

	ret = inet_aton(peer->host, &addr);
	if (ret == 0)
		return 0;

	oid_copy_in_addr(index, &addr);

	smux_trap(bgp_variables, array_size(bgp_variables), bgp_trap_oid,
		  array_size(bgp_trap_oid), bgp_oid,
		  sizeof(bgp_oid) / sizeof(oid), index, IN_ADDR_SIZE,
		  bgpTrapList, array_size(bgpTrapList), BGPBACKWARDTRANSITION);
	return 0;
}

/* 【初始化】把 bgp_variables[] 注册为 MIB 描述 "mibII/bgp"，基 OID 为 bgp_oid。
 * REGISTER_MIB 展开为 smux_register_mib(...)，见 lib/smux.h。 */
int bgp_snmp_bgp4_init(struct event_loop *tm)
{
	REGISTER_MIB("mibII/bgp", bgp_variables, variable, bgp_oid);
	return 0;
}
