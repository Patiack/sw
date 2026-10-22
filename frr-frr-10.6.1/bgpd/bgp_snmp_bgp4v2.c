// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP4V2-MIB SNMP support
 *
 * Copyright (C) 2022 Donatas Abraitis <donatas@opensourcerouting.org>
 */
/* ============================================================================
 * 【文件导读】bgp_snmp_bgp4v2.c —— BGP4V2-MIB 取值实现
 * ============================================================================
 * 【它解决什么问题】把 BGP 会话状态、以及每条 NLRI（前缀）的路径属性，按 BGP4V2-MIB
 *   定义暴露给 SNMP；相比 BGP4-MIB，它支持 4 字节 AS、IPv6、无类前缀，并区分 VRF。
 *
 * 【Net-SNMP 集成骨架】与 bgp_snmp_bgp4.c 同构：同一套回调签名
 *   (struct variable *v, oid name[], size_t *length, int exact, size_t *var_len,
 *    WriteMethod **write_method)，同一套 smux_header_table / generic 前置校验。
 *   区别在于：本 MIB 每列在 bgpv2_variables[] 里写两遍——一遍 OID 尾段为 1（IPv4 族）、
 *   一遍尾段为 2（IPv6 族）；回调里用 name[namelen-1] 判断地址族，再走 IPv4 / IPv6 分支。
 *
 * 【表与索引】
 *   bgp4V2PeerTable           : 索引 = 地址族(1/2) + 邻居地址(4 / 16 字节)
 *   bgp4V2PeerErrorsTable     : 复用邻居表索引，暴露最近错误
 *   bgp4V2PeerEventTimesTable : 复用邻居表索引，暴露时长
 *   bgp4V2NlriTable           : 索引 = AFI + SAFI + 前缀类型 + 前缀 + 前缀长度 +
 *                               邻居地址 + NlriIndex（同一前缀下多条路径用 index 区分）
 *
 * 【本文件覆盖的对象】上述四张表 + bgp4V2Established / bgp4V2BackwardTransition 两个 trap。
 *
 * 【推荐阅读顺序】peer_lookup_all_vrf / ..._next（跨 VRF 找邻居）→ bgpv2PeerTable_lookup
 *   （地址族 + 地址索引）→ 三个邻居表回调 → bgp4v2PathAttrLookup + bgp4v2PathAttrTable
 *   （NLRI 表）→ bgpv2_variables[] → 末尾 trap 与 init。
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
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_fsm.h"
#include "bgpd/bgp_snmp.h"
#include "bgpd/bgp_snmp_bgp4v2.h"

/* 【宏】声明回调返回值用的 static 缓冲（见 lib/smux.h）；SNMP_INTEGER 等宏写它们。 */
SNMP_LOCAL_VARIABLES

/* 【OID 常量】bgpv2_oid：bgp4V2 子树；bgpv2_trap_oid：bgp4V2.0（trap 前缀）。 */
static oid bgpv2_oid[] = {BGP4V2MIB};
static oid bgpv2_trap_oid[] = { BGP4V2MIB, 0 };
/* 【哨兵】0.0.0.0，地址缺失时占位返回。 */
static struct in_addr bgp_empty_addr = {};

/* 【辅助】跨所有 VRF 的所有邻居，按地址（IPv4 或 IPv6）精确查找 peer。
 * 依据 sockunion 的地址族走 AF_INET / AF_INET6 比较。 */
static struct peer *peer_lookup_all_vrf(struct ipaddr *addr)
{
	struct bgp *bgp;
	struct peer *peer;
	struct listnode *node;
	struct listnode *bgpnode;

	for (ALL_LIST_ELEMENTS_RO(bm->bgp, bgpnode, bgp)) {
		for (ALL_LIST_ELEMENTS_RO(bgp->peer, node, peer)) {
			switch (sockunion_family(&peer->connection->su)) {
			case AF_INET:
				if (IPV4_ADDR_SAME(&peer->connection->su.sin
							    .sin_addr,
						   &addr->ip._v4_addr))
					return peer;
				break;
			case AF_INET6:
				if (IPV6_ADDR_SAME(&peer->connection->su.sin6
							    .sin6_addr,
						   &addr->ip._v6_addr))
					return peer;
				break;
			default:
				break;
			}
		}
	}

	return NULL;
}

/* 【辅助】get_next：在指定地址族(family)的所有邻居里，找“地址严格大于起点”的最小者，
 * 供按地址递增遍历。offset 指向 OID 里的地址起始处，从中还原起点地址。 */
static struct peer *peer_lookup_all_vrf_next(struct ipaddr *addr, oid *offset,
					     sa_family_t family)
{
	struct bgp *bgp;
	struct peer *peer;
	struct peer *next_peer = NULL;
	struct listnode *node;
	struct listnode *bgpnode;

	for (ALL_LIST_ELEMENTS_RO(bm->bgp, bgpnode, bgp)) {
		for (ALL_LIST_ELEMENTS_RO(bgp->peer, node, peer)) {
			sa_family_t peer_family =
				sockunion_family(&peer->connection->su);

			if (peer_family != family)
				continue;

			switch (peer_family) {
			case AF_INET:
				oid2in_addr(offset, IN_ADDR_SIZE,
					    &addr->ip._v4_addr);
				if (IPV4_ADDR_CMP(&peer->connection->su.sin
							   .sin_addr,
						  &addr->ip._v4_addr) < 0 ||
				    IPV4_ADDR_SAME(&peer->connection->su.sin
							    .sin_addr,
						   &addr->ip._v4_addr))
					continue;

				if (!next_peer ||
				    IPV4_ADDR_CMP(&next_peer->connection->su.sin
							   .sin_addr,
						  &peer->connection->su.sin
							   .sin_addr) > 0)
					next_peer = peer;

				break;
			case AF_INET6:
				oid2in6_addr(offset, &addr->ip._v6_addr);
				if (IPV6_ADDR_CMP(&peer->connection->su.sin6
							   .sin6_addr,
						  &addr->ip._v6_addr) < 0 ||
				    IPV6_ADDR_SAME(&peer->connection->su.sin6
							    .sin6_addr,
						   &addr->ip._v6_addr))
					continue;

				if (!next_peer ||
				    IPV6_ADDR_CMP(&next_peer->connection->su
							   .sin6.sin6_addr,
						  &peer->connection->su.sin6
							   .sin6_addr) > 0)
					next_peer = peer;

				break;
			default:
				break;
			}
		}
	}

	if (next_peer)
		return next_peer;

	return NULL;
}

/* 【辅助】bgp4V2PeerTable 实例定位。namelen 之后：OID 末段（name[namelen-1]）是地址族
 * 标志（1=IPv4, 2=IPv6），其后是邻居地址；据此决定地址长度（4 / 16 字节）。
 * 注意 net-snmp 5.7.3 兼容 hack：当 offsetlen==0（get_next 首个请求）时把地址段清零，
 * 规避旧版本未初始化 OID 数组的问题（5.8 / 5.9 无此问题）。 */
static struct peer *bgpv2PeerTable_lookup(struct variable *v, oid name[],
					  size_t *length, int exact,
					  struct ipaddr *addr)
{
	struct peer *peer = NULL;
	size_t namelen = v ? v->namelen : BGP4V2_PEER_ENTRY_OFFSET;
	oid *offset = name + namelen;
	sa_family_t family = name[namelen - 1] == 1 ? AF_INET : AF_INET6;
	int afi_len = IN_ADDR_SIZE;
	size_t offsetlen = *length - namelen;

	if (family == AF_INET6)
		afi_len = IN6_ADDR_SIZE;

	/* Somehow with net-snmp 5.7.3, every OID item in an array
	 * is uninitialized and has a max random value, let's zero it.
	 * With 5.8, 5.9, it works fine even without this hack.
	 */
	if (!offsetlen) {
		for (int i = 0; i < afi_len; i++)
			*(offset + i) = 0;
	}

	if (exact) {
		if (family == AF_INET) {
			oid2in_addr(offset, afi_len, &addr->ip._v4_addr);
			peer = peer_lookup_all_vrf(addr);
			return peer;
		} else if (family == AF_INET6) {
			oid2in6_addr(offset, &addr->ip._v6_addr);
			return peer_lookup_all_vrf(addr);
		}
	} else {
		peer = peer_lookup_all_vrf_next(addr, offset, family);
		if (peer == NULL)
			return NULL;

		switch (sockunion_family(&peer->connection->su)) {
		case AF_INET:
			oid_copy_in_addr(offset,
					 &peer->connection->su.sin.sin_addr);
			*length = afi_len + namelen;
			return peer;
		case AF_INET6:
			oid_copy_in6_addr(offset,
					  &peer->connection->su.sin6.sin6_addr);
			*length = afi_len + namelen;
			return peer;
		default:
			break;
		}
	}

	return NULL;
}

/* 【表】bgp4V2PeerTable 读取回调：逐列返回邻居信息。经 smux_header_table 与 lookup 后，
 * 按 v->magic 取数：Instance→vrf_id; AddrType→su_local / su_remote 是否 AF_INET;
 * Local / Remote Addr / Port→依地址族取 sin 或 sin6; LocalAs→peer->local_as;
 * LocalIdentifier→peer->local_id; RemoteAs→peer->as; RemoteIdentifier→peer->remote_id;
 * AdminStatus→BGP_PEER_START_SUPPRESSED 判定; State→connection->status;
 * Description→peer->desc（SNMP_STRING）。 */
static uint8_t *bgpv2PeerTable(struct variable *v, oid name[], size_t *length,
			       int exact, size_t *var_len,
			       WriteMethod **write_method)
{
	struct peer *peer;
	struct ipaddr addr = {};

	if (smux_header_table(v, name, length, exact, var_len, write_method) ==
	    MATCH_FAILED)
		return NULL;

	peer = bgpv2PeerTable_lookup(v, name, length, exact, &addr);
	if (!peer)
		return NULL;

	switch (v->magic) {
	case BGP4V2_PEER_INSTANCE:
		return SNMP_INTEGER(peer->bgp->vrf_id);
	case BGP4V2_PEER_LOCAL_ADDR_TYPE:
		if (peer->connection->su_local)
			return SNMP_INTEGER(peer->connection->su_local->sa.sa_family == AF_INET
						    ? AFI_IP
						    : AFI_IP6);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_PEER_LOCAL_ADDR:
		if (peer->connection->su_local)
			if (peer->connection->su_local->sa.sa_family == AF_INET)
				return SNMP_IPADDRESS(peer->connection->su_local->sin.sin_addr);
			else
				return SNMP_IP6ADDRESS(peer->connection->su_local->sin6.sin6_addr);
		else
			return SNMP_IPADDRESS(bgp_empty_addr);
	case BGP4V2_PEER_REMOTE_ADDR_TYPE:
		if (peer->connection->su_remote)
			return SNMP_INTEGER(peer->connection->su_remote->sa.sa_family == AF_INET
						    ? AFI_IP
						    : AFI_IP6);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_PEER_REMOTE_ADDR:
		if (peer->connection->su_remote)
			if (peer->connection->su_remote->sa.sa_family == AF_INET)
				return SNMP_IPADDRESS(peer->connection->su_remote->sin.sin_addr);
			else
				return SNMP_IP6ADDRESS(peer->connection->su_remote->sin6.sin6_addr);
		else
			return SNMP_IPADDRESS(bgp_empty_addr);
	case BGP4V2_PEER_LOCAL_PORT:
		if (peer->connection->su_local)
			if (peer->connection->su_local->sa.sa_family == AF_INET)
				return SNMP_INTEGER(ntohs(peer->connection->su_local->sin.sin_port));
			else
				return SNMP_INTEGER(
					ntohs(peer->connection->su_local->sin6.sin6_port));
		else
			return SNMP_INTEGER(0);
	case BGP4V2_PEER_LOCAL_AS:
		return SNMP_INTEGER(peer->local_as);
	case BGP4V2_PEER_LOCAL_IDENTIFIER:
		return SNMP_IPADDRESS(peer->local_id);
	case BGP4V2_PEER_REMOTE_PORT:
		if (peer->connection->su_remote)
			if (peer->connection->su_remote->sa.sa_family == AF_INET)
				return SNMP_INTEGER(
					ntohs(peer->connection->su_remote->sin.sin_port));
			else
				return SNMP_INTEGER(
					ntohs(peer->connection->su_remote->sin6.sin6_port));
		else
			return SNMP_INTEGER(0);
	case BGP4V2_PEER_REMOTE_AS:
		return SNMP_INTEGER(peer->as);
	case BGP4V2_PEER_REMOTE_IDENTIFIER:
		return SNMP_IPADDRESS(peer->remote_id);
	case BGP4V2_PEER_ADMIN_STATUS:
#define BGP_PEER_ADMIN_STATUS_HALTED 1
#define BGP_PEER_ADMIN_STATUS_RUNNING 2
		if (BGP_PEER_START_SUPPRESSED(peer))
			return SNMP_INTEGER(BGP_PEER_ADMIN_STATUS_HALTED);
		else
			return SNMP_INTEGER(BGP_PEER_ADMIN_STATUS_RUNNING);
	case BGP4V2_PEER_STATE:
		return SNMP_INTEGER(peer->connection->status);
	case BGP4V2_PEER_DESCRIPTION:
		if (peer->desc)
			return SNMP_STRING(peer->desc);
		break;
	default:
		break;
	}

	return NULL;
}

/* 【表】bgp4V2PeerErrorsTable 读取回调：暴露最近一次错误的收 / 发细节。
 * 用 peer->last_reset 区分方向：等于 PEER_DOWN_NOTIFY_RECEIVED 视为“收到方向”，
 * 否则视为“发出方向”；相应列取 notify.code / subcode、resettime、错误文本与 data。
 * 当 code==CEASE 且 subcode 为 admin-shutdown / admin-reset 时，用
 * bgp_notify_admin_message 解出人类可读文本。 */
static uint8_t *bgpv2PeerErrorsTable(struct variable *v, oid name[],
				     size_t *length, int exact, size_t *var_len,
				     WriteMethod **write_method)
{
	struct peer *peer;
	struct ipaddr addr = {};

	if (smux_header_table(v, name, length, exact, var_len, write_method) ==
	    MATCH_FAILED)
		return NULL;

	peer = bgpv2PeerTable_lookup(v, name, length, exact, &addr);
	if (!peer)
		return NULL;

	switch (v->magic) {
	case BGP4V2_PEER_LAST_ERROR_CODE_RECEIVED:
		if (peer->last_reset == PEER_DOWN_NOTIFY_RECEIVED)
			return SNMP_INTEGER(peer->notify.code);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_PEER_LAST_ERROR_SUBCODE_RECEIVED:
		if (peer->last_reset == PEER_DOWN_NOTIFY_RECEIVED)
			return SNMP_INTEGER(peer->notify.subcode);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_PEER_LAST_ERROR_RECEIVED_TIME:
		if (peer->last_reset == PEER_DOWN_NOTIFY_RECEIVED)
			return SNMP_INTEGER(peer->resettime);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_PEER_LAST_ERROR_RECEIVED_TEXT:
		if (peer->last_reset == PEER_DOWN_NOTIFY_RECEIVED) {
			struct bgp_notify notify = peer->notify;
			char msg_buf[255];
			const char *msg_str = NULL;

			if (notify.code == BGP_NOTIFY_CEASE &&
			    (notify.subcode ==
				     BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN ||
			     notify.subcode == BGP_NOTIFY_CEASE_ADMIN_RESET)) {
				msg_str = bgp_notify_admin_message(
					msg_buf, sizeof(msg_buf),
					(uint8_t *)notify.data, notify.length);
				return SNMP_STRING(msg_str);
			}
		}
		return SNMP_STRING("");
	case BGP4V2_PEER_LAST_ERROR_RECEIVED_DATA:
		if (peer->last_reset == PEER_DOWN_NOTIFY_RECEIVED &&
		    peer->notify.data)
			return SNMP_STRING(peer->notify.data);
		else
			return SNMP_STRING("");
	case BGP4V2_PEER_LAST_ERROR_CODE_SENT:
		if (peer->last_reset != PEER_DOWN_NOTIFY_RECEIVED)
			return SNMP_INTEGER(peer->notify.code);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_PEER_LAST_ERROR_SUBCODE_SENT:
		if (peer->last_reset != PEER_DOWN_NOTIFY_RECEIVED)
			return SNMP_INTEGER(peer->notify.subcode);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_PEER_LAST_ERROR_SENT_TIME:
		if (peer->last_reset != PEER_DOWN_NOTIFY_RECEIVED)
			return SNMP_INTEGER(peer->resettime);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_PEER_LAST_ERROR_SENT_TEXT:
		if (peer->last_reset == PEER_DOWN_NOTIFY_SEND ||
		    peer->last_reset == PEER_DOWN_RTT_SHUTDOWN ||
		    peer->last_reset == PEER_DOWN_USER_SHUTDOWN) {
			struct bgp_notify notify = peer->notify;
			char msg_buf[255];
			const char *msg_str = NULL;

			if (notify.code == BGP_NOTIFY_CEASE &&
			    (notify.subcode ==
				     BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN ||
			     notify.subcode == BGP_NOTIFY_CEASE_ADMIN_RESET)) {
				msg_str = bgp_notify_admin_message(
					msg_buf, sizeof(msg_buf),
					(uint8_t *)notify.data, notify.length);
				return SNMP_STRING(msg_str);
			}
		}
		return SNMP_STRING("");
	case BGP4V2_PEER_LAST_ERROR_SENT_DATA:
		if ((peer->last_reset == PEER_DOWN_NOTIFY_SEND ||
		     peer->last_reset == PEER_DOWN_RTT_SHUTDOWN ||
		     peer->last_reset == PEER_DOWN_USER_SHUTDOWN) &&
		    peer->notify.data)
			return SNMP_STRING(peer->notify.data);
		else
			return SNMP_STRING("");
	default:
		break;
	}

	return NULL;
}

/* 【表】bgp4V2PeerEventTimesTable 读取回调：Established 时长、距上次 UPDATE 时长，
 * 均为 monotime(NULL) 减内部时间戳（uptime / update_time），时间戳为 0 时返回 0。 */
static uint8_t *bgpv2PeerEventTimesTable(struct variable *v, oid name[],
					 size_t *length, int exact,
					 size_t *var_len,
					 WriteMethod **write_method)
{
	struct peer *peer;
	struct ipaddr addr = {};

	if (smux_header_table(v, name, length, exact, var_len, write_method) ==
	    MATCH_FAILED)
		return NULL;

	peer = bgpv2PeerTable_lookup(v, name, length, exact, &addr);
	if (!peer)
		return NULL;

	switch (v->magic) {
	case BGP4V2_PEER_FSM_ESTABLISHED_TIME:
		if (!peer->uptime)
			return SNMP_INTEGER(0);
		else
			return SNMP_INTEGER(monotime(NULL) - peer->uptime);
	case BGP4V2_PEER_PEER_IN_UPDATES_ELAPSED_TIME:
		if (!peer->update_time)
			return SNMP_INTEGER(0);
		else
			return SNMP_INTEGER(monotime(NULL) - peer->update_time);
	default:
		break;
	}

	return NULL;
}

/* 【辅助】bgp4V2NlriTable 实例定位与遍历。OID 索引段包含：AFI（在 name[namelen-1]）、
 * SAFI、前缀类型、前缀、前缀长度、邻居地址类型、邻居地址、NlriIndex。
 *   exact   : 逐段解析并找到指定 (前缀, 邻居, index) 的 path；
 *   get_next: 在同一前缀的多条 path 里按 peer 地址与 index 选“下一个”，没有则用
 *             bgp_route_next 走到下一个前缀，并把索引回填进 name[] 与 *length。
 * 地址用 union ipaddr paddr 承载，family / min_family 记录选中的地址族。 */
static struct bgp_path_info *
bgp4v2PathAttrLookup(struct variable *v, oid name[], size_t *length,
		     struct bgp *bgp, struct prefix *addr, int exact)
{
	oid *offset;
	int offsetlen;
	struct bgp_path_info *path, *min;
	struct bgp_dest *dest;
	union sockunion su;
	struct ipaddr paddr = {};
	size_t namelen = v ? v->namelen : BGP4V2_NLRI_ENTRY_OFFSET;
	sa_family_t family;
	sa_family_t min_family = 0; /* family of the selected min path */
	afi_t afi;
	safi_t safi;
	size_t afi_len;
	long prefix_type = 0;
	long peer_addr_type = 0;
	long nrli_index = 1;
	long cur_index = 0;

	/* Bgp4V2AddressFamilyIdentifierTC limited to IPv6 */
	if (name[namelen - 1] > IANA_AFI_IPV6)
		return NULL;
	afi = afi_iana2int(name[namelen - 1]);
	afi_len = afi == AFI_IP ? IN_ADDR_SIZE : IN6_ADDR_SIZE;
	assert(IS_VALID_AFI(afi));

#define BGP_NLRI_ENTRY_OFFSET namelen
#define BGP4V2_NLRI_V4_V4_OFFSET IN_ADDR_SIZE + IN_ADDR_SIZE + 5
#define BGP4V2_NLRI_V4_V6_OFFSET IN_ADDR_SIZE + IN6_ADDR_SIZE + 5
#define BGP4V2_NLRI_V6_V6_OFFSET IN6_ADDR_SIZE + IN6_ADDR_SIZE + 5


	sockunion_init(&su);

	if (exact) {
		if (*length - namelen != BGP4V2_NLRI_V4_V4_OFFSET &&
		    *length - namelen != BGP4V2_NLRI_V4_V6_OFFSET &&
		    *length - namelen != BGP4V2_NLRI_V6_V6_OFFSET)
			return NULL;

		/* Set OID offset for prefix type */
		offset = name + namelen;

		/* Bgp4V2SubsequentAddressFamilyIdentifierTC  */
		/* limited to Labeled unicast */
		if (*offset > IANA_SAFI_LABELED_UNICAST)
			return NULL;
		safi = safi_iana2int(*offset);
		offset++;

		/* get bgp4V2NlriPrefixType */
		prefix_type = *offset;
		offset++;

		/* get bgp4V2NlriPrefix */
		if (prefix_type == IANA_AFI_IPV4) {
			oid2in_addr(offset, IN_ADDR_SIZE, &addr->u.prefix4);
			addr->family = AF_INET;
			offset += IN_ADDR_SIZE;
		} else if (prefix_type == IANA_AFI_IPV6) {
			oid2in6_addr(offset, &addr->u.prefix6);
			addr->family = AF_INET6;
			offset += IN6_ADDR_SIZE;
		}

		/* get bgp4V2NlriPrefixLen */
		addr->prefixlen = *offset;
		offset++;

		/* get bgp4V2PeerRemoteAddrType */
		peer_addr_type = *offset;
		if (peer_addr_type == IANA_AFI_IPV4)
			family = AF_INET;
		else
			family = AF_INET6;
		offset++;

		/* Peer address */
		su.sin.sin_family = family;

		/* get bgp4V2PeerRemoteAddr*/
		if (family == AF_INET) {
			oid2in_addr(offset, IN_ADDR_SIZE, &su.sin.sin_addr);
			offset += IN_ADDR_SIZE;
		} else {
			oid2in6_addr(offset, &su.sin6.sin6_addr);
			offset += IN6_ADDR_SIZE;
		}

		/* bgp4V2NlriIndex */
		nrli_index = *offset;
		offset++;

		/* Lookup node */
		dest = bgp_node_lookup(bgp->rib[afi][safi], addr);
		if (dest) {
			for (path = bgp_dest_get_bgp_path_info(dest); path;
			     path = path->next)
				if (sockunion_same(&path->peer->connection->su,
						   &su)) {
					cur_index++;
					if (cur_index == nrli_index)
						return path;
				}

			bgp_dest_unlock_node(dest);
		}

		return NULL;
	}

	/* Set OID offset for prefix type */
	offset = name + namelen;
	offsetlen = *length - namelen;

	if (offsetlen == 0) {
		dest = bgp_table_top(bgp->rib[afi][SAFI_UNICAST]);
	} else {

		/* bgp4V2NlriAfi  is already get  */
		/* it is comming from the name parameter */

		/* get bgp4V2NlriSafi */
		/* Bgp4V2SubsequentAddressFamilyIdentifierTC */
		/* limited to Labeled unicast */
		if (*offset > IANA_SAFI_LABELED_UNICAST)
			return NULL;
		safi = safi_iana2int(*offset);
		offset++;

		/* get bgp4V2NlriPrefixType */
		prefix_type = *offset;
		offset++;
		/* get bgp4V2NlriPrefix */
		if (prefix_type == IANA_AFI_IPV4) {
			oid2in_addr(offset, IN_ADDR_SIZE, &addr->u.prefix4);
			addr->family = AF_INET;
			offset += IN_ADDR_SIZE;
			offsetlen -= IN_ADDR_SIZE;
		} else if (prefix_type == IANA_AFI_IPV6) {
			oid2in6_addr(offset, &addr->u.prefix6);
			addr->family = AF_INET6;
			offset += IN6_ADDR_SIZE;
			offsetlen -= IN6_ADDR_SIZE;
		}

		/* get bgp4V2NlriPrefixLen */
		if (offsetlen > 0)
			addr->prefixlen = *offset;
		else
			addr->prefixlen = afi_len * 8;

		offset++;
		offsetlen--;

		/* get node */
		dest = bgp_node_lookup(bgp->rib[afi][safi], addr);
	}

	if (!dest)
		return NULL;

	if (offsetlen > 0) {
		/* get bgp4V2PeerRemoteAddrType */
		peer_addr_type = *offset;
		if (peer_addr_type == IANA_AFI_IPV4)
			family = AF_INET;
		else
			family = AF_INET6;
		offset++;

		if (family == AF_INET) {
			oid2in_addr(offset, IN_ADDR_SIZE, &paddr.ip._v4_addr);
			offset += IN_ADDR_SIZE;
		} else {
			oid2in6_addr(offset, &paddr.ip._v6_addr);
			offset += IN6_ADDR_SIZE;
		}
		/* get bgp4V2NlriIndex	*/
		nrli_index = *offset;
		offset++;

	} else {
		/* default case  start with ipv4*/
		if (afi == AFI_IP)
			family = AF_INET;
		else
			family = AF_INET6;
		memset(&paddr.ip, 0, sizeof(paddr.ip));
		nrli_index = 1;
	}

	do {
		min = NULL;
		min_family = 0;
		cur_index = 0;

		for (path = bgp_dest_get_bgp_path_info(dest); path;
		     path = path->next) {
			sa_family_t path_family =
				sockunion_family(&path->peer->connection->su);
			/* the next addr must be > to the current */
			if (path_family < family)
				continue;

			if (family == AF_INET &&
			    IPV4_ADDR_CMP(&paddr.ip._v4_addr,
					  &path->peer->connection->su.sin
						   .sin_addr) > 0)
				continue;
			else if (family == AF_INET6 &&
				 IPV6_ADDR_CMP(&paddr.ip._v6_addr,
					       &path->peer->connection->su.sin6
							.sin6_addr) > 0)
				continue;

			if (family == AF_INET &&
			    IPV4_ADDR_CMP(&paddr.ip._v4_addr,
					  &path->peer->connection->su.sin
						   .sin_addr) == 0) {
				if (cur_index == nrli_index) {
					min = path;
					min_family = family;
					nrli_index++;
					break;
				}
				cur_index++;
				continue;
			} else if (family == AF_INET6 &&
				   IPV6_ADDR_CMP(&paddr.ip._v6_addr,
						 &path->peer->connection->su
							  .sin6.sin6_addr) == 0) {
				if (cur_index == nrli_index) {
					min = path;
					min_family = family;
					nrli_index++;
					break;
				}
				cur_index++;
				continue;
			}

			/* first valid path its the  min peer addr*/
			if (!min) {
				min = path;
				min_family = path_family;
				continue;
			}

			/* consider path < min */
			if (path_family < min_family) {
				min = path;
				min_family = path_family;
				continue;
			}

			if (path_family == AF_INET
			    && IPV4_ADDR_CMP(&path->peer->connection->su.sin.sin_addr,
					     &min->peer->connection->su.sin.sin_addr)
				       < 0) {
				min = path;
				min_family = path_family;

			} else if (path_family == AF_INET6
				   && IPV6_ADDR_CMP(
					      &path->peer->connection->su.sin6.sin6_addr,
					      &min->peer->connection->su.sin6.sin6_addr)
					      < 0) {
				min = path;
				min_family = path_family;
			}
		}

		if (min) {
			const struct prefix *rn_p = bgp_dest_get_prefix(dest);

			offset = name + namelen;

			/* encode bgp4V2NlriSafi*/
			*offset = SAFI_UNICAST;
			offset++;
			/* encode bgp4V2NlriPrefixType into index*/
			/* encode  bgp4V2NlriPrefix into index */
			if (rn_p->family == AF_INET) {
				*offset = IANA_AFI_IPV4;
				offset++;
				oid_copy_in_addr(offset, &rn_p->u.prefix4);
				offset += IN_ADDR_SIZE;
			} else {
				*offset = IANA_AFI_IPV6;
				offset++;
				oid_copy_in6_addr(offset, &rn_p->u.prefix6);
				offset += IN6_ADDR_SIZE;
			}
			/* encode bgp4V2NlriPrefixLen into index*/
			*offset = rn_p->prefixlen;
			offset++;

			/* Encode bgp4V2PeerRemoteAddrType */
			/* Encode bgp4V2PeerRemoteAddr */
			if (min_family == AF_INET) {
				*offset = IANA_AFI_IPV4;
				offset++;
				oid_copy_in_addr(offset,
						 &min->peer->connection->su.sin.sin_addr);
				offset += IN_ADDR_SIZE;
				addr->u.prefix4 = rn_p->u.prefix4;
			} else {
				*offset = IANA_AFI_IPV6;
				offset++;
				oid_copy_in6_addr(
					offset, &min->peer->connection->su.sin6.sin6_addr);
				offset += IN6_ADDR_SIZE;
				addr->u.prefix6 = rn_p->u.prefix6;
			}

			/* Encode bgp4V2NlriIndex*/

			*offset = nrli_index;
			offset++;

			*length = offset - name;

			addr->prefixlen = rn_p->prefixlen;
			addr->family = rn_p->family;

			bgp_dest_unlock_node(dest);

			return min;
		}

		memset(&paddr.ip, 0, sizeof(paddr.ip));
		nrli_index = 1;

	} while ((dest = bgp_route_next(dest)));

	return NULL;
}

/* 【表】bgp4V2NlriTable 读取回调：逐列返回一条 path 的路径属性。先取默认实例 bgp，
 * bgp4v2PathAttrLookup 定位 path，再用 AF_FOREACH 找到该 peer 的 peer_af 取 AFI / SAFI；
 * 然后按 v->magic 取数：Index / AFI / SAFI / PrefixType / Prefix / PrefixLen / Best /
 * CalcLocalPref / Origin / NextHop(AddrType) / LocalPref / MED / AtomicAggregate /
 * Aggregator* / AsPath* 等。多数属性先用 bgp_attr_exists 判断是否存在，不存在返回 0。 */
static uint8_t *bgp4v2PathAttrTable(struct variable *v, oid name[],
				    size_t *length, int exact, size_t *var_len,
				    WriteMethod **write_method)
{
	struct bgp *bgp;
	struct bgp_path_info *path;
	struct peer_af *paf = NULL;
	struct prefix addr = {};
	const struct prefix *prefix = NULL;
	enum bgp_af_index index;

	bgp = bgp_get_default();
	if (!bgp)
		return NULL;

	if (smux_header_table(v, name, length, exact, var_len, write_method) ==
	    MATCH_FAILED)
		return NULL;

	path = bgp4v2PathAttrLookup(v, name, length, bgp, &addr, exact);
	if (!path)
		return NULL;

	prefix = bgp_dest_get_prefix(path->net);

	AF_FOREACH (index) {
		paf = path->peer->peer_af_array[index];
		if (paf)
			break;
	}

	switch (v->magic) {
	case BGP4V2_NLRI_INDEX:
		return SNMP_INTEGER(0);
	case BGP4V2_NLRI_AFI:
		if (paf)
			return SNMP_INTEGER(paf->afi);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_NLRI_SAFI:
		if (paf)
			return SNMP_INTEGER(paf->safi);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_NLRI_PREFIX_TYPE:
		if (paf)
			return SNMP_INTEGER(paf->afi);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_NLRI_PREFIX:
		if (prefix->family == AF_INET6)
			return SNMP_IP6ADDRESS(prefix->u.prefix6);
		else
			return SNMP_IPADDRESS(prefix->u.prefix4);
	case BGP4V2_NLRI_PREFIX_LEN:
		return SNMP_INTEGER(prefix->prefixlen);
	case BGP4V2_NLRI_BEST:
		if (CHECK_FLAG(path->flags, BGP_PATH_SELECTED))
			return SNMP_INTEGER(1);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_NLRI_CALC_LOCAL_PREF:
		if (bgp_attr_exists(path->attr, BGP_ATTR_LOCAL_PREF))
			return SNMP_INTEGER(path->attr->local_pref);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_NLRI_ORIGIN:
		switch (path->attr->origin) {
		case BGP_ORIGIN_IGP:
			return SNMP_INTEGER(1);
		case BGP_ORIGIN_EGP:
			return SNMP_INTEGER(2);
		case BGP_ORIGIN_INCOMPLETE:
			return SNMP_INTEGER(3);
		default:
			return SNMP_INTEGER(0);
		}
	case BGP4V2_NLRI_NEXT_HOP_ADDR_TYPE:
		switch (path->attr->mp_nexthop_len) {
		case BGP_ATTR_NHLEN_IPV4:
			return SNMP_INTEGER(1);
		case BGP_ATTR_NHLEN_IPV6_GLOBAL:
			return SNMP_INTEGER(2);
		case BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL:
			if (CHECK_FLAG(path->attr->nh_flags,
				       BGP_ATTR_NH_MP_PREFER_GLOBAL))
				return SNMP_INTEGER(2);
			else
				return SNMP_INTEGER(4);
		default:
			return SNMP_INTEGER(1);
		}
	case BGP4V2_NLRI_NEXT_HOP_ADDR:
		switch (path->attr->mp_nexthop_len) {
		case BGP_ATTR_NHLEN_IPV4:
			return SNMP_IPADDRESS(path->attr->mp_nexthop_global_in);
		case BGP_ATTR_NHLEN_IPV6_GLOBAL:
			return SNMP_IP6ADDRESS(path->attr->mp_nexthop_global);
		case BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL:
			if (CHECK_FLAG(path->attr->nh_flags,
				       BGP_ATTR_NH_MP_PREFER_GLOBAL))
				return SNMP_IP6ADDRESS(
					path->attr->mp_nexthop_global);
			else
				return SNMP_IP6ADDRESS(
					path->attr->mp_nexthop_local);
		default:
			return SNMP_IPADDRESS(path->attr->nexthop);
		}
		break;
	case BGP4V2_NLRI_LINK_LOCAL_NEXT_HOP_ADDR_TYPE:
	case BGP4V2_NLRI_LINK_LOCAL_NEXT_HOP_ADDR:
		/* Not properly defined in specification what should be here. */
		break;
	case BGP4V2_NLRI_LOCAL_PREF_PRESENT:
		if (bgp_attr_exists(path->attr, BGP_ATTR_LOCAL_PREF))
			return SNMP_INTEGER(1);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_NLRI_LOCAL_PREF:
		if (bgp_attr_exists(path->attr, BGP_ATTR_LOCAL_PREF))
			return SNMP_INTEGER(path->attr->local_pref);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_NLRI_MED_PRESENT:
		if (bgp_attr_exists(path->attr, BGP_ATTR_MULTI_EXIT_DISC))
			return SNMP_INTEGER(1);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_NLRI_MED:
		if (bgp_attr_exists(path->attr, BGP_ATTR_MULTI_EXIT_DISC))
			return SNMP_INTEGER(path->attr->med);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_NLRI_ATOMIC_AGGREGATE:
		if (bgp_attr_exists(path->attr, BGP_ATTR_ATOMIC_AGGREGATE))
			return SNMP_INTEGER(1);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_NLRI_AGGREGATOR_PRESENT:
		if (bgp_attr_exists(path->attr, BGP_ATTR_AGGREGATOR))
			return SNMP_INTEGER(1);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_NLRI_AGGREGATOR_AS:
		if (bgp_attr_exists(path->attr, BGP_ATTR_AGGREGATOR))
			return SNMP_INTEGER(path->attr->aggregator_as);
		else
			return SNMP_INTEGER(0);
	case BGP4V2_NLRI_AGGREGATOR_ADDR:
		if (bgp_attr_exists(path->attr, BGP_ATTR_AGGREGATOR))
			return SNMP_IPADDRESS(path->attr->aggregator_addr);
		else
			return SNMP_IPADDRESS(bgp_empty_addr);
	case BGP4V2_NLRI_AS_PATH_CALC_LENGTH:
		return SNMP_INTEGER((path->attr->aspath && path->attr->aspath->segments)
					    ? path->attr->aspath->segments->length
					    : 0);
	case BGP4V2_NLRI_AS_PATH:
		return aspath_snmp_pathseg(path->attr->aspath, var_len);
	case BGP4V2_NLRI_PATH_ATTR_UNKNOWN:
		*var_len = 0;
		return NULL;
	}
	return NULL;
}

/* 【trap 载荷】bgp4V2Established / BackwardTransition 两个 trap 的对象列表。
 * 每条 {namelen, {OID 段}}：前 3 段 {1,2,1} 或 {1,3,1} 定位到表，末段 1 / 2 指定
 * IPv4 / IPv6 实例。Backward 列表比 Established 多带“最近错误”的三列。 */
/* BGP V2 Traps. */
static struct trap_object bgpv2TrapEstListv4[] = {
	{ 6, { 1, 2, 1, BGP4V2_PEER_STATE, 1, 1 } },
	{ 6, { 1, 2, 1, BGP4V2_PEER_LOCAL_PORT, 1, 1 } },
	{ 6, { 1, 2, 1, BGP4V2_PEER_REMOTE_PORT, 1, 1 } }
};

static struct trap_object bgpv2TrapEstListv6[] = {
	{ 6, { 1, 2, 1, BGP4V2_PEER_STATE, 1, 2 } },
	{ 6, { 1, 2, 1, BGP4V2_PEER_LOCAL_PORT, 1, 2 } },
	{ 6, { 1, 2, 1, BGP4V2_PEER_REMOTE_PORT, 1, 2 } }
};

static struct trap_object bgpv2TrapBackListv4[] = {
	{ 6, { 1, 2, 1, BGP4V2_PEER_STATE, 1, 1 } },
	{ 6, { 1, 2, 1, BGP4V2_PEER_LOCAL_PORT, 1, 1 } },
	{ 6, { 1, 2, 1, BGP4V2_PEER_REMOTE_PORT, 1, 1 } },
	{ 6, { 1, 3, 1, BGP4V2_PEER_LAST_ERROR_CODE_RECEIVED, 1, 1 } },
	{ 6, { 1, 3, 1, BGP4V2_PEER_LAST_ERROR_SUBCODE_RECEIVED, 1, 1 } },
	{ 6, { 1, 3, 1, BGP4V2_PEER_LAST_ERROR_RECEIVED_TEXT, 1, 1 } }
};

static struct trap_object bgpv2TrapBackListv6[] = {
	{ 6, { 1, 2, 1, BGP4V2_PEER_STATE, 1, 2 } },
	{ 6, { 1, 2, 1, BGP4V2_PEER_LOCAL_PORT, 1, 2 } },
	{ 6, { 1, 2, 1, BGP4V2_PEER_REMOTE_PORT, 1, 2 } },
	{ 6, { 1, 3, 1, BGP4V2_PEER_LAST_ERROR_CODE_RECEIVED, 1, 2 } },
	{ 6, { 1, 3, 1, BGP4V2_PEER_LAST_ERROR_SUBCODE_RECEIVED, 1, 2 } },
	{ 6, { 1, 3, 1, BGP4V2_PEER_LAST_ERROR_RECEIVED_TEXT, 1, 2 } }
};

/* 【变量表】BGP4V2-MIB 注册表：每行 {magic, 类型, 读写, 回调, OID 段数, {OID 段}}。
 * 重要模式：同一列写两遍——相邻两行 OID 段仅末段不同（1=IPv4, 2=IPv6），因为 MIB 把
 * 地址族编进了索引。magic 与各回调里 switch(v->magic) 用的常量一致。 */
static struct variable bgpv2_variables[] = {
	/* bgp4V2PeerEntry */
	{BGP4V2_PEER_INSTANCE,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_INSTANCE, 1, 1}},
	{BGP4V2_PEER_INSTANCE,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_INSTANCE, 1, 2}},
	{BGP4V2_PEER_LOCAL_ADDR_TYPE,
	 ASN_INTEGER,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_LOCAL_ADDR_TYPE, 1, 1}},
	{BGP4V2_PEER_LOCAL_ADDR_TYPE,
	 ASN_INTEGER,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_LOCAL_ADDR_TYPE, 1, 2}},
	{BGP4V2_PEER_LOCAL_ADDR,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_LOCAL_ADDR, 1, 1}},
	{BGP4V2_PEER_LOCAL_ADDR,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_LOCAL_ADDR, 1, 2}},
	{BGP4V2_PEER_REMOTE_ADDR_TYPE,
	 ASN_INTEGER,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_REMOTE_ADDR_TYPE, 1, 1}},
	{BGP4V2_PEER_REMOTE_ADDR_TYPE,
	 ASN_INTEGER,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_REMOTE_ADDR_TYPE, 1, 2}},
	{BGP4V2_PEER_REMOTE_ADDR,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_REMOTE_ADDR, 1, 1}},
	{BGP4V2_PEER_REMOTE_ADDR,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_REMOTE_ADDR, 1, 2}},
	{BGP4V2_PEER_LOCAL_PORT,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_LOCAL_PORT, 1, 1}},
	{BGP4V2_PEER_LOCAL_PORT,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_LOCAL_PORT, 1, 2}},
	{BGP4V2_PEER_LOCAL_AS,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_LOCAL_AS, 1, 1}},
	{BGP4V2_PEER_LOCAL_AS,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_LOCAL_AS, 1, 2}},
	{BGP4V2_PEER_LOCAL_IDENTIFIER,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_LOCAL_IDENTIFIER, 1, 1}},
	{BGP4V2_PEER_LOCAL_IDENTIFIER,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_LOCAL_IDENTIFIER, 1, 2}},
	{BGP4V2_PEER_REMOTE_PORT,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_REMOTE_PORT, 1, 1}},
	{BGP4V2_PEER_REMOTE_PORT,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_REMOTE_PORT, 1, 2}},
	{BGP4V2_PEER_REMOTE_AS,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_REMOTE_AS, 1, 1}},
	{BGP4V2_PEER_REMOTE_AS,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_REMOTE_AS, 1, 2}},
	{BGP4V2_PEER_REMOTE_IDENTIFIER,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_REMOTE_IDENTIFIER, 1, 1}},
	{BGP4V2_PEER_REMOTE_IDENTIFIER,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_REMOTE_IDENTIFIER, 1, 2}},
	{BGP4V2_PEER_ADMIN_STATUS,
	 ASN_INTEGER,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_ADMIN_STATUS, 1, 1}},
	{BGP4V2_PEER_ADMIN_STATUS,
	 ASN_INTEGER,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_ADMIN_STATUS, 1, 2}},
	{BGP4V2_PEER_STATE,
	 ASN_INTEGER,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_STATE, 1, 1}},
	{BGP4V2_PEER_STATE,
	 ASN_INTEGER,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_STATE, 1, 2}},
	{BGP4V2_PEER_DESCRIPTION,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_DESCRIPTION, 1, 1}},
	{BGP4V2_PEER_DESCRIPTION,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerTable,
	 6,
	 {1, 2, 1, BGP4V2_PEER_DESCRIPTION, 1, 2}},
	/* bgp4V2PeerErrorsEntry */
	{BGP4V2_PEER_LAST_ERROR_CODE_RECEIVED,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_CODE_RECEIVED, 1, 1}},
	{BGP4V2_PEER_LAST_ERROR_CODE_RECEIVED,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_CODE_RECEIVED, 1, 2}},
	{BGP4V2_PEER_LAST_ERROR_SUBCODE_RECEIVED,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_SUBCODE_RECEIVED, 1, 1}},
	{BGP4V2_PEER_LAST_ERROR_SUBCODE_RECEIVED,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_SUBCODE_RECEIVED, 1, 2}},
	{BGP4V2_PEER_LAST_ERROR_RECEIVED_TIME,
	 ASN_TIMETICKS,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_RECEIVED_TIME, 1, 1}},
	{BGP4V2_PEER_LAST_ERROR_RECEIVED_TIME,
	 ASN_TIMETICKS,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_RECEIVED_TIME, 1, 2}},
	{BGP4V2_PEER_LAST_ERROR_RECEIVED_TEXT,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_RECEIVED_TEXT, 1, 1}},
	{BGP4V2_PEER_LAST_ERROR_RECEIVED_TEXT,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_RECEIVED_TEXT, 1, 2}},
	{BGP4V2_PEER_LAST_ERROR_RECEIVED_DATA,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_RECEIVED_DATA, 1, 1}},
	{BGP4V2_PEER_LAST_ERROR_RECEIVED_DATA,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_RECEIVED_DATA, 1, 2}},
	{BGP4V2_PEER_LAST_ERROR_CODE_SENT,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_CODE_SENT, 1, 1}},
	{BGP4V2_PEER_LAST_ERROR_CODE_SENT,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_CODE_SENT, 1, 2}},
	{BGP4V2_PEER_LAST_ERROR_SUBCODE_SENT,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_SUBCODE_SENT, 1, 1}},
	{BGP4V2_PEER_LAST_ERROR_SUBCODE_SENT,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_SUBCODE_SENT, 1, 2}},
	{BGP4V2_PEER_LAST_ERROR_SENT_TIME,
	 ASN_TIMETICKS,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_SENT_TIME, 1, 1}},
	{BGP4V2_PEER_LAST_ERROR_SENT_TIME,
	 ASN_TIMETICKS,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_SENT_TIME, 1, 2}},
	{BGP4V2_PEER_LAST_ERROR_SENT_TEXT,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_SENT_TEXT, 1, 1}},
	{BGP4V2_PEER_LAST_ERROR_SENT_TEXT,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_SENT_TEXT, 1, 2}},
	{BGP4V2_PEER_LAST_ERROR_SENT_DATA,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_SENT_DATA, 1, 1}},
	{BGP4V2_PEER_LAST_ERROR_SENT_DATA,
	 ASN_OCTET_STR,
	 RONLY,
	 bgpv2PeerErrorsTable,
	 6,
	 {1, 3, 1, BGP4V2_PEER_LAST_ERROR_SENT_DATA, 1, 2}},
	/* bgp4V2PeerEventTimesEntry */
	{BGP4V2_PEER_FSM_ESTABLISHED_TIME,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerEventTimesTable,
	 6,
	 {1, 4, 1, BGP4V2_PEER_FSM_ESTABLISHED_TIME, 1, 1}},
	{BGP4V2_PEER_FSM_ESTABLISHED_TIME,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerEventTimesTable,
	 6,
	 {1, 4, 1, BGP4V2_PEER_FSM_ESTABLISHED_TIME, 1, 2}},
	{BGP4V2_PEER_PEER_IN_UPDATES_ELAPSED_TIME,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerEventTimesTable,
	 6,
	 {1, 4, 1, BGP4V2_PEER_PEER_IN_UPDATES_ELAPSED_TIME, 1, 1}},
	{BGP4V2_PEER_PEER_IN_UPDATES_ELAPSED_TIME,
	 ASN_UNSIGNED,
	 RONLY,
	 bgpv2PeerEventTimesTable,
	 6,
	 {1, 4, 1, BGP4V2_PEER_PEER_IN_UPDATES_ELAPSED_TIME, 1, 2}},
	/* bgp4V2NlriTable */
	{BGP4V2_NLRI_INDEX,
	 ASN_UNSIGNED,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_INDEX, 1, 1}},
	{BGP4V2_NLRI_INDEX,
	 ASN_UNSIGNED,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_INDEX, 1, 2}},
	{BGP4V2_NLRI_AFI,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_AFI, 1, 1}},
	{BGP4V2_NLRI_AFI,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_AFI, 1, 2}},
	{BGP4V2_NLRI_SAFI,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_SAFI, 1, 1}},
	{BGP4V2_NLRI_SAFI,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_SAFI, 1, 2}},
	{BGP4V2_NLRI_PREFIX_TYPE,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_PREFIX_TYPE, 1, 1}},
	{BGP4V2_NLRI_PREFIX_TYPE,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_PREFIX_TYPE, 1, 2}},
	{BGP4V2_NLRI_PREFIX,
	 ASN_OCTET_STR,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_PREFIX, 1, 1}},
	{BGP4V2_NLRI_PREFIX,
	 ASN_OCTET_STR,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_PREFIX, 1, 2}},
	{BGP4V2_NLRI_PREFIX_LEN,
	 ASN_UNSIGNED,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_PREFIX_LEN, 1, 1}},
	{BGP4V2_NLRI_PREFIX_LEN,
	 ASN_UNSIGNED,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_PREFIX_LEN, 1, 2}},
	{BGP4V2_NLRI_BEST,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_BEST, 1, 1}},
	{BGP4V2_NLRI_BEST,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_BEST, 1, 2}},
	{BGP4V2_NLRI_CALC_LOCAL_PREF,
	 ASN_UNSIGNED,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_CALC_LOCAL_PREF, 1, 1}},
	{BGP4V2_NLRI_CALC_LOCAL_PREF,
	 ASN_UNSIGNED,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_CALC_LOCAL_PREF, 1, 2}},
	{BGP4V2_NLRI_ORIGIN,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_ORIGIN, 1, 1}},
	{BGP4V2_NLRI_ORIGIN,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_ORIGIN, 1, 2}},
	{BGP4V2_NLRI_NEXT_HOP_ADDR_TYPE,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_NEXT_HOP_ADDR_TYPE, 1, 1}},
	{BGP4V2_NLRI_NEXT_HOP_ADDR_TYPE,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_NEXT_HOP_ADDR_TYPE, 1, 2}},
	{BGP4V2_NLRI_NEXT_HOP_ADDR,
	 ASN_OCTET_STR,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_NEXT_HOP_ADDR, 1, 1}},
	{BGP4V2_NLRI_NEXT_HOP_ADDR,
	 ASN_OCTET_STR,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_NEXT_HOP_ADDR, 1, 2}},
	{BGP4V2_NLRI_LINK_LOCAL_NEXT_HOP_ADDR_TYPE,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_LINK_LOCAL_NEXT_HOP_ADDR_TYPE, 1, 1}},
	{BGP4V2_NLRI_LINK_LOCAL_NEXT_HOP_ADDR_TYPE,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_LINK_LOCAL_NEXT_HOP_ADDR_TYPE, 1, 2}},
	{BGP4V2_NLRI_LINK_LOCAL_NEXT_HOP_ADDR,
	 ASN_OCTET_STR,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_LINK_LOCAL_NEXT_HOP_ADDR, 1, 1}},
	{BGP4V2_NLRI_LINK_LOCAL_NEXT_HOP_ADDR,
	 ASN_OCTET_STR,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_LINK_LOCAL_NEXT_HOP_ADDR, 1, 2}},
	{BGP4V2_NLRI_LOCAL_PREF_PRESENT,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_LOCAL_PREF_PRESENT, 1, 1}},
	{BGP4V2_NLRI_LOCAL_PREF_PRESENT,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_LOCAL_PREF_PRESENT, 1, 2}},
	{BGP4V2_NLRI_LOCAL_PREF,
	 ASN_UNSIGNED,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_LOCAL_PREF, 1, 1}},
	{BGP4V2_NLRI_LOCAL_PREF,
	 ASN_UNSIGNED,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_LOCAL_PREF, 1, 2}},
	{BGP4V2_NLRI_MED_PRESENT,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_MED_PRESENT, 1, 1}},
	{BGP4V2_NLRI_MED_PRESENT,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_MED_PRESENT, 1, 2}},
	{BGP4V2_NLRI_MED,
	 ASN_UNSIGNED,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_MED, 1, 1}},
	{BGP4V2_NLRI_MED,
	 ASN_UNSIGNED,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_MED, 1, 2}},
	{BGP4V2_NLRI_ATOMIC_AGGREGATE,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_ATOMIC_AGGREGATE, 1, 1}},
	{BGP4V2_NLRI_ATOMIC_AGGREGATE,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_ATOMIC_AGGREGATE, 1, 2}},
	{BGP4V2_NLRI_AGGREGATOR_PRESENT,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_AGGREGATOR_PRESENT, 1, 1}},
	{BGP4V2_NLRI_AGGREGATOR_PRESENT,
	 ASN_INTEGER,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_AGGREGATOR_PRESENT, 1, 2}},
	{BGP4V2_NLRI_AGGREGATOR_AS,
	 ASN_UNSIGNED,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_AGGREGATOR_AS, 1, 1}},
	{BGP4V2_NLRI_AGGREGATOR_AS,
	 ASN_UNSIGNED,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_AGGREGATOR_AS, 1, 2}},
	{BGP4V2_NLRI_AGGREGATOR_ADDR,
	 ASN_OCTET_STR,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_AGGREGATOR_ADDR, 1, 1}},
	{BGP4V2_NLRI_AGGREGATOR_ADDR,
	 ASN_OCTET_STR,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_AGGREGATOR_ADDR, 1, 2}},
	{BGP4V2_NLRI_AS_PATH_CALC_LENGTH,
	 ASN_UNSIGNED,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_AS_PATH_CALC_LENGTH, 1, 1}},
	{BGP4V2_NLRI_AS_PATH_CALC_LENGTH,
	 ASN_UNSIGNED,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_AS_PATH_CALC_LENGTH, 1, 2}},
	{BGP4V2_NLRI_AS_PATH_STRING,
	 ASN_OCTET_STR,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_AS_PATH_STRING, 1, 1}},
	{BGP4V2_NLRI_AS_PATH_STRING,
	 ASN_OCTET_STR,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_AS_PATH_STRING, 1, 2}},
	{BGP4V2_NLRI_AS_PATH,
	 ASN_OCTET_STR,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_AS_PATH, 1, 1}},
	{BGP4V2_NLRI_AS_PATH,
	 ASN_OCTET_STR,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_AS_PATH, 1, 2}},
	{BGP4V2_NLRI_PATH_ATTR_UNKNOWN,
	 ASN_OCTET_STR,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_PATH_ATTR_UNKNOWN, 1, 1}},
	{BGP4V2_NLRI_PATH_ATTR_UNKNOWN,
	 ASN_OCTET_STR,
	 RONLY,
	 bgp4v2PathAttrTable,
	 6,
	 {1, 9, 1, BGP4V2_NLRI_PATH_ATTR_UNKNOWN, 1, 2}},
};

/* 【trap】bgpv2TrapEstablished：仅当 BGP_OPT_TRAPS_BGP4MIBV2 开启时才发。
 * 同样要求“刚进入 Established”；按邻居地址族选择 IPv4 / IPv6 的载荷列表
 * （bgpv2TrapEstListv4 / v6）与对应索引长度（4 / 16 字节）。 */
int bgpv2TrapEstablished(struct peer *peer)
{
	oid index[sizeof(oid) * IN6_ADDR_SIZE];
	size_t length;

	if (!CHECK_FLAG(bm->options, BGP_OPT_TRAPS_BGP4MIBV2))
		return 0;

	/* Check if this peer just went to Established */
	if ((peer->connection->ostatus != OpenConfirm) ||
	    !(peer_established(peer->connection)))
		return 0;

	switch (sockunion_family(&peer->connection->su)) {
	case AF_INET:
		oid_copy_in_addr(index, &peer->connection->su.sin.sin_addr);
		length = IN_ADDR_SIZE;
		smux_trap(bgpv2_variables, array_size(bgpv2_variables),
			  bgpv2_trap_oid, array_size(bgpv2_trap_oid), bgpv2_oid,
			  sizeof(bgpv2_oid) / sizeof(oid), index, length,
			  bgpv2TrapEstListv4, array_size(bgpv2TrapEstListv4),
			  BGP4V2ESTABLISHED);
		break;
	case AF_INET6:
		oid_copy_in6_addr(index, &peer->connection->su.sin6.sin6_addr);
		length = IN6_ADDR_SIZE;
		smux_trap(bgpv2_variables, array_size(bgpv2_variables),
			  bgpv2_trap_oid, array_size(bgpv2_trap_oid), bgpv2_oid,
			  sizeof(bgpv2_oid) / sizeof(oid), index, length,
			  bgpv2TrapEstListv6, array_size(bgpv2TrapEstListv6),
			  BGP4V2ESTABLISHED);
		break;
	default:
		break;
	}

	return 0;
}

/* 【trap】bgpv2TrapBackwardTransition：FSM 回退时发；同样按地址族选 v4 / v6 载荷列表。 */
int bgpv2TrapBackwardTransition(struct peer *peer)
{
	oid index[sizeof(oid) * IN6_ADDR_SIZE];
	size_t length;

	if (!CHECK_FLAG(bm->options, BGP_OPT_TRAPS_BGP4MIBV2))
		return 0;

	switch (sockunion_family(&peer->connection->su)) {
	case AF_INET:
		oid_copy_in_addr(index, &peer->connection->su.sin.sin_addr);
		length = IN_ADDR_SIZE;
		smux_trap(bgpv2_variables, array_size(bgpv2_variables),
			  bgpv2_trap_oid, array_size(bgpv2_trap_oid), bgpv2_oid,
			  sizeof(bgpv2_oid) / sizeof(oid), index, length,
			  bgpv2TrapBackListv4, array_size(bgpv2TrapBackListv4),
			  BGP4V2BACKWARDTRANSITION);
		break;
	case AF_INET6:
		oid_copy_in6_addr(index, &peer->connection->su.sin6.sin6_addr);
		length = IN6_ADDR_SIZE;
		smux_trap(bgpv2_variables, array_size(bgpv2_variables),
			  bgpv2_trap_oid, array_size(bgpv2_trap_oid), bgpv2_oid,
			  sizeof(bgpv2_oid) / sizeof(oid), index, length,
			  bgpv2TrapBackListv6, array_size(bgpv2TrapBackListv6),
			  BGP4V2BACKWARDTRANSITION);
		break;
	default:
		break;
	}

	return 0;
}

/* 【初始化】把 bgpv2_variables[] 注册为 MIB "mibII/bgpv2"，基 OID 为 bgpv2_oid。 */
int bgp_snmp_bgp4v2_init(struct event_loop *tm)
{
	REGISTER_MIB("mibII/bgpv2", bgpv2_variables, variable, bgpv2_oid);
	return 0;
}
