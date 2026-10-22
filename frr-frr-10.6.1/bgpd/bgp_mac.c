// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BGPd - Mac hash code
 * Copyright (C) 2018 Cumulus Networks, Inc.
 *               Donald Sharp
 */

/*
 * 文件导读：BGP 本机 MAC 哈希表（self mac hash）
 *
 * 本模块维护一张“本机（本设备 SVI 等接口）拥有的 MAC 地址”到接口名列表的
 * 哈希表。EVPN 场景下，来自对端的 type-2（MAC/IP）路由若其 MAC 属于本机
 * 本地 MAC，或其对端 router-mac 指向本机，就需要被重新处理 / 通告；本表正是
 * 用来快速判定“某 MAC 是否为本机所有”。当本机某接口的 MAC 发生变化（学到一个
 * 新 MAC、或 MAC 在接口间迁移、或接口消失）时，会触发对全部 EVPN 表的重新扫描
 * 并重发受影响的路由。
 *
 * 关键数据结构：
 *   - struct bgp_self_mac：一条记录，含 6 字节 MAC（macaddr）与拥有该 MAC 的
 *     接口名列表（ifp_list）。
 *   - bm->self_mac_hash：全局哈希表，键为 MAC，值为 struct bgp_self_mac。
 *   - struct bgp_mac_find_internal：遍历哈希表时用的辅助结构，携带目标接口名
 *     与找到的结果。
 *
 * 与相邻模块的配合：
 *   - 接口事件（bgp_mac_add_mac_entry / bgp_mac_del_mac_entry）由接口管理代码
 *     在 ifp 增删 / MAC 变化时调用；
 *   - 重新扫描依赖 bgp_update / bgp_soft_reconfig_in 把受影响路由重新发送；
 *   - 判定查询 bgp_mac_exist / bgp_mac_entry_exists 供 EVPN 路由处理流程使用。
 *
 * 读的顺序：先看哈希表的键 / 比较 / 分配 / 释放，再看查找与重扫流程，最后看
 * 增删入口 bgp_mac_add_mac_entry / bgp_mac_del_mac_entry 与查询、展示函数。
 */

#include <zebra.h>

#include <jhash.h>
#include <hash.h>
#include <prefix.h>
#include <memory.h>

#include "bgpd/bgpd.h"
#include "bgpd/bgp_mac.h"
#include "bgpd/bgp_memory.h"
#include "bgpd/bgp_label.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_packet.h"
#include "bgpd/bgp_rd.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_evpn_private.h"
#include "bgpd/bgp_trace.h"

/* 分配类型：self mac 哈希表记录本身。 */
DEFINE_MTYPE_STATIC(BGPD, BSM, "Mac Hash Entry");
/* 分配类型：记录内保存的接口名字符串。 */
DEFINE_MTYPE_STATIC(BGPD, BSM_STRING, "Mac Hash Entry Intf String");

/*
 * 本机 MAC 哈希表的一条记录：一个 MAC 地址，以及拥有该 MAC 的所有接口名
 * 列表（同一个 MAC 可能出现在多个接口上）。
 */
struct bgp_self_mac {
	struct ethaddr macaddr;
	struct list *ifp_list;
};

/*
 * 哈希函数：以记录中的 6 字节 MAC 为键，用 jhash 算出一个哈希值。
 */
static unsigned int bgp_mac_hash_key_make(const void *data)
{
	const struct bgp_self_mac *bsm = data;

	return jhash(&bsm->macaddr, ETH_ALEN, 0xa5a5dead);
}

/*
 * 哈希比较函数：两记录的 MAC 完全相等则视为相同（true），否则 false。
 */
static bool bgp_mac_hash_cmp(const void *d1, const void *d2)
{
	const struct bgp_self_mac *bsm1 = d1;
	const struct bgp_self_mac *bsm2 = d2;

	if (memcmp(&bsm1->macaddr, &bsm2->macaddr, ETH_ALEN) == 0)
		return true;

	return false;
}

/*
 * 创建全局 self mac 哈希表，注册上面的键函数与比较函数。bgpd 启动时调用一次。
 */
void bgp_mac_init(void)
{
	bm->self_mac_hash = hash_create(bgp_mac_hash_key_make, bgp_mac_hash_cmp,
					"BGP MAC Hash");
}

/*
 * 哈希表元素释放回调：先删除该记录的接口名列表（其成员字符串由列表的 del
 * 回调释放），再释放记录本身。
 */
static void bgp_mac_hash_free(void *data)
{
	struct bgp_self_mac *bsm = data;

	if (bsm->ifp_list)
		list_delete(&bsm->ifp_list);

	XFREE(MTYPE_BSM, bsm);
}

/*
 * 清空并销毁全局 self mac 哈希表（逐元素调用 bgp_mac_hash_free 再释放表）。
 * bgpd 退出时调用。
 */
void bgp_mac_finish(void)
{
	hash_clean_and_free(&bm->self_mac_hash, bgp_mac_hash_free);
}

/*
 * 接口名列表的元素释放回调：释放一个接口名字符串。
 */
static void bgp_mac_hash_interface_string_del(void *val)
{
	char *data = val;

	XFREE(MTYPE_BSM_STRING, data);
}

/*
 * 哈希表元素分配回调：为一次 hash_get 新建记录。只拷贝 MAC 地址，并初始化
 * 空的接口名列表，列表的 del 回调设为上面的字符串释放函数。
 */
static void *bgp_mac_hash_alloc(void *p)
{
	const struct bgp_self_mac *orig = p;
	struct bgp_self_mac *bsm;

	bsm = XCALLOC(MTYPE_BSM, sizeof(struct bgp_self_mac));
	memcpy(&bsm->macaddr, &orig->macaddr, ETH_ALEN);

	bsm->ifp_list = list_new();
	bsm->ifp_list->del = bgp_mac_hash_interface_string_del;

	return bsm;
}

/*
 * 查找辅助结构：携带要查的接口名，并回填命中的记录（bsm）。
 */
struct bgp_mac_find_internal {
	struct bgp_self_mac *bsm;
	const char *ifname;
};

/*
 * 哈希遍历回调：检查当前桶的记录是否在接口名列表里含目标接口名，命中则把
 * 记录写入 bmfi->bsm 并立即返回（提前结束遍历）。
 */
static void bgp_mac_find_ifp_internal(struct hash_bucket *bucket, void *arg)
{
	struct bgp_mac_find_internal *bmfi = arg;
	struct bgp_self_mac *bsm = bucket->data;
	struct listnode *node;
	char *name;

	for (ALL_LIST_ELEMENTS_RO(bsm->ifp_list, node, name)) {
		if (strcmp(name, bmfi->ifname) == 0) {
			bmfi->bsm = bsm;
			return;
		}
	}
}

/*
 * 按接口名在哈希表中查找拥有该接口的记录（反向查找：由接口名找 MAC 记录）。
 * 遍历全表，命中返回记录指针，未命中返回 NULL。
 */
static struct bgp_self_mac *bgp_mac_find_interface_name(const char *ifname)
{
	struct bgp_mac_find_internal bmfi;

	bmfi.bsm = NULL;
	bmfi.ifname = ifname;
	hash_iterate(bm->self_mac_hash, bgp_mac_find_ifp_internal, &bmfi);

	return bmfi.bsm;
}

/*
 * 对单个 EVPN 表做重扫：对指定对端 peer，把 MAC 匹配 macaddr（type-2 路由的
 * mac 一致）或其 router-mac（attr->rmac）等于 macaddr 的路由重新发送一次。
 * 流程：外层按 RD 表遍历，内层按前缀遍历，找到该 peer 对应的路径；已被标记
 * BGP_PATH_REMOVED 的路径跳过（仅在调试题下打印并埋点）；命中则用 bgp_update
 * 重发该路由。用于本机 MAC 发生变化后刷新向该对端通告的内容。
 */
static void bgp_process_mac_rescan_table(struct bgp *bgp, struct peer *peer,
					 struct bgp_table *table,
					 struct ethaddr *macaddr)
{
	struct bgp_dest *pdest, *dest;
	struct bgp_path_info *pi;
	uint8_t num_labels;
	mpls_label_t *label_pnt;

	for (pdest = bgp_table_top(table); pdest;
	     pdest = bgp_route_next(pdest)) {
		struct bgp_table *sub = pdest->info;
		const struct prefix *pdest_p = bgp_dest_get_prefix(pdest);

		if (!sub)
			continue;

		for (dest = bgp_table_top(sub); dest;
		     dest = bgp_route_next(dest)) {
			bool dest_affected;
			const struct prefix *p = bgp_dest_get_prefix(dest);
			struct prefix_evpn *pevpn = (struct prefix_evpn *)dest;
			struct prefix_rd prd;

			if (pevpn->family == AF_EVPN
			    && pevpn->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE
			    && memcmp(&p->u.prefix_evpn.macip_addr.mac, macaddr,
				      ETH_ALEN)
				       == 0)
				dest_affected = true;
			else
				dest_affected = false;

			for (pi = dest->info; pi; pi = pi->next) {
				if (pi->peer == peer)
					break;
			}

			if (!pi)
				continue;

			/*
			 * If the mac address is not the same then
			 * we don't care and since we are looking
			 */
			if ((memcmp(&pi->attr->rmac, macaddr, ETH_ALEN) != 0)
			    && !dest_affected)
				continue;

			num_labels = BGP_PATH_INFO_NUM_LABELS(pi);
			label_pnt = num_labels ? &pi->extra->labels->label[0]
					       : NULL;

			prd.family = AF_UNSPEC;
			prd.prefixlen = 64;
			memcpy(&prd.val, pdest_p->u.val, 8);

			if (CHECK_FLAG(pi->flags, BGP_PATH_REMOVED)) {
				char pfx_buf[BGP_PRD_PATH_STRLEN] = { 0 };

				bgp_debug_rdpfxpath2str(AFI_L2VPN, SAFI_EVPN, &prd, p, label_pnt,
							num_labels, pi->addpath_rx_id ? 1 : 0,
							pi->addpath_rx_id, NULL, pfx_buf,
							sizeof(pfx_buf));

				if (bgp_debug_update(peer, p, NULL, 1))
					zlog_debug("%s skip update of %s marked as removed",
						   peer->host, pfx_buf);

				frrtrace(2, frr_bgp, upd_skip_update_of_removed_prefix, peer->host,
					 pfx_buf);

				continue;
			}

			bgp_update(peer, p, pi->addpath_rx_id, pi->attr,
				   AFI_L2VPN, SAFI_EVPN, ZEBRA_ROUTE_BGP,
				   BGP_ROUTE_NORMAL, &prd, label_pnt, num_labels,
				   1, bgp_attr_get_evpn_overlay(pi->attr));
		}
	}
}

/*
 * 对某实例的 EVPN 表（AFI_L2VPN / SAFI_EVPN）做重扫：遍历其所有对端，跳过
 * 邻居组、未建立会话、未激活该地址族的对端；若该对端开启了 inbound soft-reconfig，
 * 则优先走 bgp_soft_reconfig_in 让其在入方向重算，否则直接调用
 * bgp_process_mac_rescan_table 重发受影响路由。
 */
static void bgp_mac_rescan_evpn_table(struct bgp *bgp, struct ethaddr *macaddr)
{
	struct listnode *node;
	struct peer *peer;
	safi_t safi;
	afi_t afi;

	afi = AFI_L2VPN;
	safi = SAFI_EVPN;
	for (ALL_LIST_ELEMENTS_RO(bgp->peer, node, peer)) {

		if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
			continue;

		if (!peer_established(peer->connection))
			continue;

		if (!peer->afc[afi][safi])
			continue;

		if (bgp_debug_update(peer, NULL, NULL, 1))
			zlog_debug(
				"Processing EVPN MAC interface change on peer %s %s",
				peer->host,
				CHECK_FLAG(peer->af_flags[afi][safi],
					   PEER_FLAG_SOFT_RECONFIG)
					? "(inbound, soft-reconfig)"
					: "");

		if (!bgp_soft_reconfig_in(peer, afi, safi)) {
			struct bgp_table *table = bgp->rib[afi][safi];

			frrtrace(2, frr_bgp, upd_mac_rescan_evpn_table, peer->host, 0);

			bgp_process_mac_rescan_table(bgp, peer, table, macaddr);
		}
	}
}

/*
 * 对所有 BGP 实例的 EVPN 表做重扫：遍历全部实例，只对确实存在 EVPN 表的实例
 * 调用 bgp_mac_rescan_evpn_table。本机 MAC 变化时从这里开始重扫。
 */
static void bgp_mac_rescan_all_evpn_tables(struct ethaddr *macaddr)
{
	struct listnode *node;
	struct bgp *bgp;

	for (ALL_LIST_ELEMENTS_RO(bm->bgp, node, bgp)) {
		struct bgp_table *table = bgp->rib[AFI_L2VPN][SAFI_EVPN];

		if (table)
			bgp_mac_rescan_evpn_table(bgp, macaddr);
	}
}

/*
 * 从某记录 bsm 的接口名列表中移除接口 ifname。先定位节点并删除、释放名字串；
 * 若移除后该记录的接口列表已空，说明这个 MAC 在本机已不再使用，于是释放整条
 * 记录，并以 macaddr 为参数触发一次全 EVPN 表重扫（让依赖该本机 MAC 的路由
 * 更新）。注意 macaddr 是调用方先拷贝出来的，避免释放后悬垂。
 */
static void bgp_mac_remove_ifp_internal(struct bgp_self_mac *bsm, char *ifname,
					struct ethaddr *macaddr)
{
	struct listnode *node = NULL;
	char *name;

	for (ALL_LIST_ELEMENTS_RO(bsm->ifp_list, node, name)) {
		if (strcmp(name, ifname) == 0)
			break;
	}

	if (node) {
		list_delete_node(bsm->ifp_list, node);
		XFREE(MTYPE_BSM_STRING, name);
	}

	if (bsm->ifp_list->count == 0) {
		struct ethaddr mac = *macaddr;

		hash_release(bm->self_mac_hash, bsm);
		list_delete(&bsm->ifp_list);
		XFREE(MTYPE_BSM, bsm);

		bgp_mac_rescan_all_evpn_tables(&mac);
	}
}

/*
 * 新增 / 更新一条本机 MAC 记录（接口 ifp 的硬件地址）。以接口 MAC 查表：
 * 不存在则新建记录（mac_added 置真，表示需要重扫）。随后处理“MAC 迁移”：
 * 若该接口原先关联到别的记录（old_bsm），先从旧记录摘除该接口。若本记录列表
 * 原为空则直接加入；若 old_bsm 就是本记录说明地址未变，直接返回。
 * 仅当产生新记录时，才触发一次全 EVPN 表重扫（已有 MAC 换接口不改变本机
 * MAC 状态，无需重扫）。
 */
/* Add/Update entry of the 'bgp mac hash' table.
 * A rescan of the EVPN tables is only needed if
 * a new hash bucket is allocated.
 * Learning an existing mac on a new interface (or
 * having an existing mac move from one interface to
 * another) does not result in changes to self mac
 * state, so we shouldn't trigger a rescan.
 */
void bgp_mac_add_mac_entry(struct interface *ifp)
{
	struct bgp_self_mac lookup;
	struct bgp_self_mac *bsm;
	struct bgp_self_mac *old_bsm;
	char *ifname;
	bool mac_added = false;

	memcpy(&lookup.macaddr, &ifp->hw_addr, ETH_ALEN);
	bsm = hash_lookup(bm->self_mac_hash, &lookup);
	if (!bsm) {
		bsm = hash_get(bm->self_mac_hash, &lookup, bgp_mac_hash_alloc);
		/* mac is new, rescan needs to be triggered */
		mac_added = true;
	}

	/*
	 * Does this happen to be a move
	 */
	old_bsm = bgp_mac_find_interface_name(ifp->name);
	ifname = XSTRDUP(MTYPE_BSM_STRING, ifp->name);

	if (bsm->ifp_list->count == 0) {

		listnode_add(bsm->ifp_list, ifname);
		if (old_bsm)
			bgp_mac_remove_ifp_internal(old_bsm, ifname,
						    &old_bsm->macaddr);
	} else {
		/*
		 * If old mac address is the same as the new,
		 * then there is nothing to do here
		 */
		if (old_bsm == bsm) {
			XFREE(MTYPE_BSM_STRING, ifname);
			return;
		}

		if (old_bsm)
			bgp_mac_remove_ifp_internal(old_bsm, ifp->name,
						    &old_bsm->macaddr);

		listnode_add(bsm->ifp_list, ifname);
	}

	if (mac_added)
		bgp_mac_rescan_all_evpn_tables(&bsm->macaddr);
}

/*
 * 删除接口 ifp 对应的本机 MAC 记录。按接口 MAC 查表，未找到则直接返回；
 * 找到则从该记录的接口名列表中移除本接口（若移除后列表为空，记录会被释放
 * 并触发重扫，见 bgp_mac_remove_ifp_internal）。
 */
void bgp_mac_del_mac_entry(struct interface *ifp)
{
	struct bgp_self_mac lookup;
	struct bgp_self_mac *bsm;

	memcpy(&lookup.macaddr, &ifp->hw_addr, ETH_ALEN);
	bsm = hash_lookup(bm->self_mac_hash, &lookup);
	if (!bsm)
		return;

	/*
	 * Write code to allow old mac address to no-longer
	 * win if we happen to have received it from a peer.
	 */
	bgp_mac_remove_ifp_internal(bsm, ifp->name, &bsm->macaddr);
}

/*
 * 判断给定 MAC 是否为本机某个接口（如 SVI）所拥有。全零 MAC 直接返回 false。
 * 否则在哈希表中查找，找到返回 true，否则 false。典型用途：判定 EVPN 更新
 * 中的 router-mac 属性是否指向本机。
 */
/* This API checks MAC address against any of local
 * assigned (SVIs) MAC address.
 * An example: router-mac attribute in any of evpn update
 * requires to compare against local mac.
 */
bool bgp_mac_exist(const struct ethaddr *mac)
{
	struct bgp_self_mac lookup;
	struct bgp_self_mac *bsm;
	static uint8_t tmp [ETHER_ADDR_STRLEN] = {0};

	if (memcmp(mac, &tmp, ETH_ALEN) == 0)
		return false;

	memcpy(&lookup.macaddr, mac, ETH_ALEN);
	bsm = hash_lookup(bm->self_mac_hash, &lookup);
	if (!bsm)
		return false;

	return true;
}

/*
 * 判断一个 EVPN 前缀是否为“MAC 属于本机”的 type-2 路由：先要求前缀族为
 * AF_EVPN 且路由类型为 BGP_EVPN_MAC_IP_ROUTE，再用其中的 MAC 调用
 * bgp_mac_exist 判定。任一条件不满足返回 false。
 */
/* This API checks EVPN type-2 prefix and compares
 * mac against any of local assigned (SVIs) MAC
 * address.
 */
bool bgp_mac_entry_exists(const struct prefix *p)
{
	const struct prefix_evpn *pevpn = (const struct prefix_evpn *)p;

	if (pevpn->family != AF_EVPN)
		return false;

	if (pevpn->prefix.route_type != BGP_EVPN_MAC_IP_ROUTE)
		return false;

	return bgp_mac_exist(&p->u.prefix_evpn.macip_addr.mac);
}

/*
 * 哈希遍历回调：向 vty 打印一条记录，格式为“Mac Address: <MAC> <接口名...>”。
 * arg 为要输出到的 vty 上下文。
 */
static void bgp_mac_show_mac_entry(struct hash_bucket *bucket, void *arg)
{
	struct vty *vty = arg;
	struct bgp_self_mac *bsm = bucket->data;
	struct listnode *node;
	char *name;
	char buf_mac[ETHER_ADDR_STRLEN];

	vty_out(vty, "Mac Address: %s ",
		prefix_mac2str(&bsm->macaddr, buf_mac, sizeof(buf_mac)));

	for (ALL_LIST_ELEMENTS_RO(bsm->ifp_list, node, name))
		vty_out(vty, "%s ", name);

	vty_out(vty, "\n");
}

/*
 * 在 vty 上展示整张本机 MAC 哈希表（供 show 命令调用）。逐条经
 * bgp_mac_show_mac_entry 打印。
 */
void bgp_mac_dump_table(struct vty *vty)
{
	hash_iterate(bm->self_mac_hash, bgp_mac_show_mac_entry, vty);
}
