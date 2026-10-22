// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP nexthop scan
 * Copyright (C) 2000 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_nexthop.c —— 下一跳缓存(bnc)的数据结构层：增删查 + 地址/直连/自环
 * ============================================================================
 * 职责与定位：本文件是 bgp_nexthop.h 里那套数据结构和扫描逻辑的**实现**，与
 *   bgp_nht.c（NHT 逻辑层）配套：本文件管“结构”（bnc 红黑树、本机地址表、直连表、
 *   multiaccess 与自环判断），bgp_nht.c 管“流程”（登记、zebra 回调、扇出重算）。
 *   struct bgp_nexthop_cache(bnc) 的字段语义与 flags/change_flags 位含义见 bgp_nexthop.h。
 *
 * 关键路径（谁调用它 / 它调用谁）：
 *   - bnc_new/bnc_find/bnc_free 由 bgp_nht.c 的登记/注销逻辑调用；
 *   - bgp_connected_add/delete 由 bgp_zebra.c 在处理接口地址增删时调用；bgp_address_add/
 *     del 同理（维护本机“我有哪些地址”表）；
 *   - bgp_nexthop_self / bgp_hostroute_self / bgp_multiaccess_check_* 由 bgp_route.c、
 *     bgp_updgrp_adv.c（出向 next-hop-self 与路由回注视判断）调用；
 *   - 本文件末尾的 show 命令与 bgp_scan_* 由 bgpd 主流程初始化/展示时使用。
 *
 * 关键函数清单（建议阅读顺序）：
 *   1) bgp_nexthop_cache_compare + bnc_new/find/free —— bnc 的键(prefix,color,ifindex)
 *      与生命周期；
 *   2) bgp_connected_add/delete —— 直连路由表如何维护（决定“下一跳是否直连”的判据）；
 *   3) bgp_address_add/del —— 本机地址表（判断“下一跳是不是我”）；
 *   4) bgp_nexthop_self / bgp_hostroute_self —— 自环/回注判断（next-hop-self 场景）；
 *   5) bgp_multiaccess_check_v4/v6 —— 同一网段上是否有别的邻居（影响 next-hop-self 决策）。
 *
 * 建议阅读顺序：先读 bgp_nexthop.h 建立 bnc 概念，再按上面 1)→5) 阅读；show 相关可略读。
 * ============================================================================
 */

#include <zebra.h>

#include "command.h"
#include "frrevent.h"
#include "prefix.h"
#include "lib/json.h"
#include "zclient.h"
#include "stream.h"
#include "network.h"
#include "log.h"
#include "memory.h"
#include "hash.h"
#include "jhash.h"
#include "nexthop.h"
#include "queue.h"
#include "filter.h"
#include "printfrr.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_nexthop.h"
#include "bgpd/bgp_nht.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_damp.h"
#include "bgpd/bgp_fsm.h"
#include "bgpd/bgp_vty.h"
#include "bgpd/bgp_rd.h"
#include "bgpd/bgp_mplsvpn.h"
#include "bgpd/bgp_bfd.h"

/* 本文件专用的内存类型：本机地址表里保存的接口名字符串。 */
DEFINE_MTYPE_STATIC(BGPD, MARTIAN_STRING, "BGP Martian Addr Intf String");

/* ========== 1. bnc 的键、增删查与整表重置 ========== */
/* 【函数】bgp_nexthop_cache_compare —— bnc 红黑树的比较函数（决定整棵树的排序键）。
 * 键的顺序（重要，等价于 bnc 的唯一键）：
 *   1) srte_color        —— 同前缀不同 SR-TE color 视为不同下一跳；
 *   2) ifindex_ipv6_ll   —— 仅 IPv6 链路本地下一跳有效，用来区分“同一个 fe80:: 在不同
 *                            接口上”，这正是 bnc 键里要含 ifindex 的原因；
 *   3) prefix            —— 下一跳前缀。
 * 返回 -1/0/1 供 rbtree 使用；三者全相等即同一个 bnc。 */
int bgp_nexthop_cache_compare(const struct bgp_nexthop_cache *a,
			      const struct bgp_nexthop_cache *b)
{
	if (a->srte_color < b->srte_color)
		return -1;
	if (a->srte_color > b->srte_color)
		return 1;

	if (a->ifindex_ipv6_ll < b->ifindex_ipv6_ll)
		return -1;
	if (a->ifindex_ipv6_ll > b->ifindex_ipv6_ll)
		return 1;

	return prefix_cmp(&a->prefix, &b->prefix);
}

/* 【函数】bnc_nexthop_free —— 只释放 bnc 内部的 nexthop 链表，不动 bnc 本体。
 * 调用点：重建下一跳链表前、释放 bnc 前（bnc_free 内部也会调用）。
 * 注意：释放后要把 bnc->nexthop 置 NULL，否则会悬垂（本文件与 bgp_nht.c 均如此约定）。 */
void bnc_nexthop_free(struct bgp_nexthop_cache *bnc)
{
	nexthops_free(bnc->nexthop);
}

/* 【函数】bnc_new —— 新建一个 bnc 并入树（**不**向 zebra 登记）。
 * 参数：tree 所属下一跳表；prefix 下一跳前缀；srte_color SR-TE color；ifindex 仅 v6 LL 用。
 * 动作：XCALLOC 出零初始化对象；填键字段(prefix/ifindex_ipv6_ll/srte_color)与所属 tree；
 *   LIST_INIT 初始化 paths 链表；bgp_nexthop_cache_add 插入红黑树。
 * 说明：登记 zebra 由 NHT 逻辑层(bgp_nht.c)负责，本函数只负责“建结构”。 */
struct bgp_nexthop_cache *bnc_new(struct bgp_nexthop_cache_head *tree,
				  struct prefix *prefix, uint32_t srte_color,
				  ifindex_t ifindex)
{
	struct bgp_nexthop_cache *bnc;

	bnc = XCALLOC(MTYPE_BGP_NEXTHOP_CACHE,
		      sizeof(struct bgp_nexthop_cache));
	bnc->prefix = *prefix;
	bnc->ifindex_ipv6_ll = ifindex;
	bnc->srte_color = srte_color;
	bnc->tree = tree;
	LIST_INIT(&(bnc->paths));
	bgp_nexthop_cache_add(tree, bnc);

	return bnc;
}

/* 【函数】bnc_existing_for_prefix —— 判断同前缀是否**还有别的** bnc（忽略 color/ifindex）。
 * 用途：向 zebra 注销时的判断依据——若同前缀仍有其它 bnc（如不同 color/ifindex），
 *   不能注销 zebra 侧那条（它们可能共用同一条登记），见 unregister_zebra_rnh。
 * 返回：存在别的同前缀 bnc 返回 true；否则 false。 */
bool bnc_existing_for_prefix(struct bgp_nexthop_cache *bnc)
{
	struct bgp_nexthop_cache *bnc_tmp;

	frr_each (bgp_nexthop_cache, bnc->tree, bnc_tmp) {
		if (bnc_tmp == bnc)
			continue;
		if (prefix_cmp(&bnc->prefix, &bnc_tmp->prefix) == 0)
			return true;
	}
	return false;
}

/* 【函数】bnc_free —— 释放一个 bnc：先释放 nexthop 链表，再从树删除，最后释放本体。
 * 前置约束：调用前必须已确保 bnc->paths 为空且无 nht_info（见 bgp_unlink_nexthop_check），
 *   否则会留下指向已释放对象的悬垂路径引用。 */
void bnc_free(struct bgp_nexthop_cache *bnc)
{
	bnc_nexthop_free(bnc);
	bgp_nexthop_cache_del(bnc->tree, bnc);
	XFREE(MTYPE_BGP_NEXTHOP_CACHE, bnc);
}

/* 【函数】bnc_find —— 按完整键(prefix, srte_color, ifindex)在树里查找 bnc。
 * 实现：在栈上放一个临时 bnc，只填三个键字段，交给 bgp_nexthop_cache_find 比较查找。
 * 返回：命中的 bnc，找不到返回 NULL；tree 为 NULL 时直接返回 NULL。 */
struct bgp_nexthop_cache *bnc_find(struct bgp_nexthop_cache_head *tree,
				   struct prefix *prefix, uint32_t srte_color,
				   ifindex_t ifindex)
{
	struct bgp_nexthop_cache bnc = {};

	if (!tree)
		return NULL;

	bnc.prefix = *prefix;
	bnc.srte_color = srte_color;
	bnc.ifindex_ipv6_ll = ifindex;
	return bgp_nexthop_cache_find(tree, &bnc);
}

/* 【函数】bgp_nexthop_cache_reset —— 清空并释放整棵 bnc 树（实例销毁时用）。
 * 实现：循环取树里第一个节点，先把它 paths 里的每条路径映射拆掉（path_nh_map false，
 *   并解掉 MPLS 标签链接），再 bnc_free，直到树空。
 * 注意：这里**主动**拆映射，是为了避免路径还指向即将释放的 bnc（悬垂）。 */
/* Reset and free all BGP nexthop cache. */
static void bgp_nexthop_cache_reset(struct bgp_nexthop_cache_head *tree)
{
	struct bgp_nexthop_cache *bnc;

	while (bgp_nexthop_cache_count(tree) > 0) {
		bnc = bgp_nexthop_cache_first(tree);

		while (!LIST_EMPTY(&(bnc->paths))) {
			struct bgp_path_info *path = LIST_FIRST(&(bnc->paths));

			bgp_mplsvpn_path_nh_label_unlink(path);
			bgp_mplsvpn_path_nh_label_bind_unlink(path);

			path_nh_map(path, bnc, false);
		}

		bnc_free(bnc);
	}
}

/* ========== 2. 本机 tunnel-ip(TIP) 地址表 ========== */
/* 【本机隧道端点地址表 bgp->tip_hash】键为 struct ipaddr，值为 struct tip_addr{addr,refcnt}。
 * 用途：PE 场景下识别“下一跳是否为本机隧道端点地址”，参与自环/路由回注判断（见
 *   bgp_nexthop_self）。refcnt 计数：同一地址可被多处引用，归零才真正移除。
 * 下面四个 static 函数是哈希表回调：alloc（建对象）/free（释放）/key_make/cmp。 */
static void *bgp_tip_hash_alloc(void *p)
{
	const struct ipaddr *val = (const struct ipaddr *)p;
	struct tip_addr *addr;

	addr = XMALLOC(MTYPE_TIP_ADDR, sizeof(struct tip_addr));
	addr->refcnt = 0;
	addr->addr = *val;

	return addr;
}

static void bgp_tip_hash_free(void *addr)
{
	XFREE(MTYPE_TIP_ADDR, addr);
}

static unsigned int bgp_tip_hash_key_make(const void *p)
{
	const struct tip_addr *addr = p;

	if (IS_IPADDR_V4(&addr->addr)) {
		return jhash_1word(addr->addr.ipaddr_v4.s_addr, 0);
	} else {
		return jhash2(addr->addr.ipaddr_v6.s6_addr32,
				array_size(addr->addr.ipaddr_v6.s6_addr32), 0);
	}
}

static bool bgp_tip_hash_cmp(const void *p1, const void *p2)
{
	const struct tip_addr *addr1 = p1;
	const struct tip_addr *addr2 = p2;

	return ipaddr_is_same(&addr1->addr, &addr2->addr);
}

/* 【函数】bgp_tip_hash_init/destroy —— 创建 / 销毁本机 TIP 哈希表（实例级）。
 * destroy 会遍历释放每个 tip_addr（hash_free 回调）。 */
void bgp_tip_hash_init(struct bgp *bgp)
{
	bgp->tip_hash = hash_create(bgp_tip_hash_key_make, bgp_tip_hash_cmp,
				    "BGP TIP hash");
}

void bgp_tip_hash_destroy(struct bgp *bgp)
{
	hash_clean_and_free(&bgp->tip_hash, bgp_tip_hash_free);
}

/* 【函数】bgp_tip_add —— 把一个本机隧道端点地址登记进 TIP 表（若已存在只加引用计数）。
 * 返回：true 表示**新增**了地址（集合发生了变化，调用方可据此采取行动）。 */
/* Add/Update Tunnel-IP entry of bgp martian next-hop table.
 *
 * Returns true only if we add a _new_ TIP so the caller knows that an
 * actionable change has occurred. If we find an existing TIP then we
 * only need to update the refcnt, since the collection of known TIPs
 * has not changed.
 */
bool bgp_tip_add(struct bgp *bgp, struct ipaddr *tip)
{
	struct tip_addr tmp;
	struct tip_addr *addr;
	bool tip_added = false;

	tmp.addr = *tip;

	addr = hash_lookup(bgp->tip_hash, &tmp);
	if (!addr) {
		addr = hash_get(bgp->tip_hash, &tmp, bgp_tip_hash_alloc);
		tip_added = true;
	}

	addr->refcnt++;

	return tip_added;
}

/* 【函数】bgp_tip_del —— 从 TIP 表移除一个引用；引用计数归零才真正删除条目。
 * 注意：可能已被 bgp_interface_down() 提前删过（addr == NULL），需判空返回。 */
void bgp_tip_del(struct bgp *bgp, struct ipaddr *tip)
{
	struct tip_addr tmp;
	struct tip_addr *addr;

	tmp.addr = *tip;

	addr = hash_lookup(bgp->tip_hash, &tmp);
	/* may have been deleted earlier by bgp_interface_down() */
	if (addr == NULL)
		return;

	addr->refcnt--;

	if (addr->refcnt == 0) {
		hash_release(bgp->tip_hash, addr);
		XFREE(MTYPE_TIP_ADDR, addr);
	}
}

/* ========== 3. 本机地址表 bgp_addr（判断“下一跳是不是我”） ========== */
/* 【结构】struct bgp_addr —— 本机一个地址（通常规整为 /32 或 /128），附带“该地址出现在
 *   哪些接口上”的接口名列表(ifp_name_list)。表键为前缀 p，值为本结构。
 * 用途：bgp_nexthop_self()/bgp_hostroute_self() 用它判断“某下一跳/前缀是否命中本机地址”，
 *   服务于路由回注与自环防护。 */
/* BGP own address structure */
struct bgp_addr {
	struct prefix p;
	struct list *ifp_name_list;
};

/* 【函数】show_address_entry —— 本机地址哈希表的遍历回调：逐条打印地址及其接口名。 */
static void show_address_entry(struct hash_bucket *bucket, void *args)
{
	struct vty *vty = (struct vty *)args;
	struct bgp_addr *addr = (struct bgp_addr *)bucket->data;
	char *name;
	struct listnode *node;
	char str[INET6_ADDRSTRLEN] = {0};

	vty_out(vty, "addr: %s, count: %d : ",
		inet_ntop(addr->p.family, &(addr->p.u.prefix),
			  str, INET6_ADDRSTRLEN),
		addr->ifp_name_list->count);

	for (ALL_LIST_ELEMENTS_RO(addr->ifp_name_list, node, name)) {
		vty_out(vty, " %s,", name);
	}

	vty_out(vty, "\n");
}

/* 【函数】bgp_nexthop_show_address_hash —— show 命令用：遍历打印本机地址表全部条目。 */
void bgp_nexthop_show_address_hash(struct vty *vty, struct bgp *bgp)
{
	hash_iterate(bgp->address_hash,
		     (void (*)(struct hash_bucket *, void *))show_address_entry,
		     vty);
}

/* 【函数】bgp_address_hash_string_del —— 接口名列表(list)的删除回调：释放接口名字符串。 */
static void bgp_address_hash_string_del(void *val)
{
	char *data = val;

	XFREE(MTYPE_MARTIAN_STRING, data);
}

/* 【函数】bgp_address_hash_alloc —— hash_get 未命中时的分配回调：新建 bgp_addr，
 *   拷贝前缀、创建接口名列表（并挂上释放回调 bgp_address_hash_string_del）。 */
static void *bgp_address_hash_alloc(void *p)
{
	struct bgp_addr *copy_addr = p;
	struct bgp_addr *addr = NULL;

	addr = XMALLOC(MTYPE_BGP_ADDR, sizeof(struct bgp_addr));
	prefix_copy(&addr->p, &copy_addr->p);

	addr->ifp_name_list = list_new();
	addr->ifp_name_list->del = bgp_address_hash_string_del;

	return addr;
}

/* 【函数】bgp_address_hash_free —— 释放一条本机地址（含其接口名列表）。 */
static void bgp_address_hash_free(void *data)
{
	struct bgp_addr *addr = data;

	list_delete(&addr->ifp_name_list);
	XFREE(MTYPE_BGP_ADDR, addr);
}

/* 【函数】bgp_address_hash_key_make / bgp_address_hash_cmp —— 本机地址表的哈希键与比较，
 *   都直接复用前缀的哈希/相等函数（prefix_hash_key / prefix_same）。 */
static unsigned int bgp_address_hash_key_make(const void *p)
{
	const struct bgp_addr *addr = p;

	return prefix_hash_key(&addr->p);
}

static bool bgp_address_hash_cmp(const void *p1, const void *p2)
{
	const struct bgp_addr *addr1 = p1;
	const struct bgp_addr *addr2 = p2;

	return prefix_same(&addr1->p, &addr2->p);
}

/* 【函数】bgp_address_init/destroy —— 创建 / 销毁本机地址哈希表（实例级）。
 * 由 bgp_scan_init/bgp_scan_finish 成对调用。 */
void bgp_address_init(struct bgp *bgp)
{
	bgp->address_hash =
		hash_create(bgp_address_hash_key_make, bgp_address_hash_cmp,
				"BGP Connected Address Hash");
}

void bgp_address_destroy(struct bgp *bgp)
{
	hash_clean_and_free(&bgp->address_hash, bgp_address_hash_free);
}

/* 【函数】bgp_address_add —— 记录“本机在接口 ifc 上有地址 p”。
 * 关键：不论 p 是网段还是单地址，都规整为**主机前缀**（IPv4 → /32，IPv6 → /128）后
 *   入表，因为判断“下一跳是不是我”时比的是具体地址。若该接口名尚未在列表里则追加。
 * 调用时机：zebra 报告接口地址新增时（经 bgp_connected_add 再进来）。 */
static void bgp_address_add(struct bgp *bgp, struct connected *ifc,
			    struct prefix *p)
{
	struct bgp_addr tmp;
	struct bgp_addr *addr;
	struct listnode *node;
	char *name;

	tmp.p = *p;

	if (tmp.p.family == AF_INET)
		tmp.p.prefixlen = IPV4_MAX_BITLEN;
	else if (tmp.p.family == AF_INET6)
		tmp.p.prefixlen = IPV6_MAX_BITLEN;

	addr = hash_get(bgp->address_hash, &tmp, bgp_address_hash_alloc);

	for (ALL_LIST_ELEMENTS_RO(addr->ifp_name_list, node, name)) {
		if (strcmp(ifc->ifp->name, name) == 0)
			break;
	}
	if (!node) {
		name = XSTRDUP(MTYPE_MARTIAN_STRING, ifc->ifp->name);
		listnode_add(addr->ifp_name_list, name);
	}
}

/* 【函数】bgp_address_del —— 反操作：从地址 p 的接口名列表里移除 ifc->ifp->name。
 * 同样先规整为主机前缀；若列表因此变空，则整条地址从表里删除并释放。 */
static void bgp_address_del(struct bgp *bgp, struct connected *ifc,
			    struct prefix *p)
{
	struct bgp_addr tmp;
	struct bgp_addr *addr;
	struct listnode *node;
	char *name;

	tmp.p = *p;

	if (tmp.p.family == AF_INET)
		tmp.p.prefixlen = IPV4_MAX_BITLEN;
	else if (tmp.p.family == AF_INET6)
		tmp.p.prefixlen = IPV6_MAX_BITLEN;

	addr = hash_lookup(bgp->address_hash, &tmp);
	/* may have been deleted earlier by bgp_interface_down() */
	if (addr == NULL)
		return;

	for (ALL_LIST_ELEMENTS_RO(addr->ifp_name_list, node, name)) {
		if (strcmp(ifc->ifp->name, name) == 0)
			break;
	}

	if (node) {
		list_delete_node(addr->ifp_name_list, node);
		XFREE(MTYPE_MARTIAN_STRING, name);
	}

	if (addr->ifp_name_list->count == 0) {
		hash_release(bgp->address_hash, addr);
		list_delete(&addr->ifp_name_list);
		XFREE(MTYPE_BGP_ADDR, addr);
	}
}


/* ========== 4. 直连路由表 bgp->connected_table（判断“下一跳是否直连”） ========== */
/* 【结构】struct bgp_connected_ref —— 挂在 bgp->connected_table 某节点上的引用计数包装。
 *   一个直连网段节点被几个接口/地址引用，就计几次；归零才释放。
 * 直连表用途：eBGP 单跳必须直连的合法性校验、决定是否走 connected 下一跳解析，
 *   也服务于 multiaccess 判断（同一网段是否有别的邻居）。 */
struct bgp_connected_ref {
	unsigned int refcnt;
};

/* 【函数】bgp_connected_add —— zebra 报告某接口上新增一个直连地址时，维护直连表。
 * 动作：
 *   - IPv4：掩码后跳过 0.0.0.0/0(any)；把地址加入本机地址表；在 connected_table 对应
 *     节点上 refcnt++（无则由 XCALLOC 建包装）；
 *   - IPv6：掩码后跳过 unspecified 与 **链路本地**（LL 不当作直连网段，v6 LL 由接口
 *     事件单独驱动）；同样加入地址表并在 connected_table 记 refcnt；
 *   - 最后遍历所有 peer：刷新 BFD 源地址；对“接口型未编号邻居”若尚未 established，
 *     视情况重启会话（接口事件驱动未编号 BGP 建立）。
 * 说明：本函数由 bgp_zebra.c 在处理接口地址新增消息时调用。 */
void bgp_connected_add(struct bgp *bgp, struct connected *ifc)
{
	struct prefix p;
	struct prefix *addr;
	struct bgp_dest *dest;
	struct bgp_connected_ref *bc;
	struct listnode *node, *nnode;
	struct peer *peer;
	struct peer_connection *connection;

	addr = ifc->address;

	p = *(CONNECTED_PREFIX(ifc));
	if (addr->family == AF_INET) {
		apply_mask_ipv4((struct prefix_ipv4 *)&p);

		if (prefix_ipv4_any((struct prefix_ipv4 *)&p))
			return;

		bgp_address_add(bgp, ifc, addr);

		dest = bgp_node_get(bgp->connected_table[AFI_IP], &p);
		bc = bgp_dest_get_bgp_connected_ref_info(dest);
		if (bc)
			bc->refcnt++;
		else {
			bc = XCALLOC(MTYPE_BGP_CONN,
				     sizeof(struct bgp_connected_ref));
			bc->refcnt = 1;
			bgp_dest_set_bgp_connected_ref_info(dest, bc);
		}

	/* IPv6 直连：跳过 unspecified(::) 与链路本地(fe80::/10)——后者不参与直连网段判断，
	 * 由 v6 LL 的接口事件机制单独处理。 */
	} else if (addr->family == AF_INET6) {
		apply_mask_ipv6((struct prefix_ipv6 *)&p);

		if (IN6_IS_ADDR_UNSPECIFIED(&p.u.prefix6))
			return;

		if (IN6_IS_ADDR_LINKLOCAL(&p.u.prefix6))
			return;

		bgp_address_add(bgp, ifc, addr);

		dest = bgp_node_get(bgp->connected_table[AFI_IP6], &p);

		bc = bgp_dest_get_bgp_connected_ref_info(dest);
		if (bc)
			bc->refcnt++;
		else {
			bc = XCALLOC(MTYPE_BGP_CONN,
				     sizeof(struct bgp_connected_ref));
			bc->refcnt = 1;
			bgp_dest_set_bgp_connected_ref_info(dest, bc);
		}
	}

	/* 遍历所有 peer：刷新 BFD 会话源信息；若是“接口型未编号邻居”且未 established，
	 * 触发一次 BGP_Stop/BGP_Start，使未编号会话随接口地址出现而重新建立。 */
	/*
	 * Iterate over all the peers and attempt to set the bfd session
	 * data and if it's a bgp unnumbered get her flowing if necessary
	 */
	for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {
		bgp_peer_bfd_update_source(peer);
		if (peer->conf_if && (strcmp(peer->conf_if, ifc->ifp->name) == 0) &&
		    !peer_established(peer->connection) &&
		    !CHECK_FLAG(peer->flags, PEER_FLAG_IFPEER_V6ONLY)) {
			connection = peer->connection;
			if (peer_active(connection) == BGP_PEER_ACTIVE)
				BGP_EVENT_ADD(connection, BGP_Stop);
			BGP_EVENT_ADD(connection, BGP_Start);
		}
	}
}

/* 【函数】bgp_connected_delete —— bgp_connected_add 的反操作：接口地址删除时维护直连表。
 * 同样跳过 IPv4 any 与 IPv6 unspecified/LL；从地址表移除；在 connected_table 对应节点
 * 上 refcnt--，归零则释放包装。末尾两次 bgp_dest_unlock_node 归还该节点的引用
 * （一次对应 add 时长期持有的引用，一次对应本次 lookup 的引用）。 */
void bgp_connected_delete(struct bgp *bgp, struct connected *ifc)
{
	struct prefix p;
	struct prefix *addr;
	struct bgp_dest *dest = NULL;
	struct bgp_connected_ref *bc;

	addr = ifc->address;

	p = *(CONNECTED_PREFIX(ifc));
	apply_mask(&p);
	if (addr->family == AF_INET) {
		if (prefix_ipv4_any((struct prefix_ipv4 *)&p))
			return;

		bgp_address_del(bgp, ifc, addr);

		dest = bgp_node_lookup(bgp->connected_table[AFI_IP], &p);
	} else if (addr->family == AF_INET6) {
		if (IN6_IS_ADDR_UNSPECIFIED(&p.u.prefix6))
			return;

		if (IN6_IS_ADDR_LINKLOCAL(&p.u.prefix6))
			return;

		bgp_address_del(bgp, ifc, addr);

		dest = bgp_node_lookup(bgp->connected_table[AFI_IP6], &p);
	}

	if (!dest)
		return;

	bc = bgp_dest_get_bgp_connected_ref_info(dest);
	bc->refcnt--;
	if (bc->refcnt == 0) {
		XFREE(MTYPE_BGP_CONN, bc);
		bgp_dest_set_bgp_connected_ref_info(dest, NULL);
	}

	dest = bgp_dest_unlock_node(dest);
	assert(dest);
	bgp_dest_unlock_node(dest);
}

/* 【函数】bgp_connected_cleanup —— route_table 的 cleanup 回调：节点被删除时释放其
 *   直连引用包装(bgp_connected_ref)，避免泄漏。由 bgp_scan_finish 挂到 connected_table。 */
static void bgp_connected_cleanup(struct route_table *table,
				  struct route_node *rn)
{
	struct bgp_connected_ref *bc;
	struct bgp_dest *bn = bgp_dest_from_rnode(rn);

	bc = bgp_dest_get_bgp_connected_ref_info(bn);
	if (!bc)
		return;

	XFREE(MTYPE_BGP_CONN, bc);
	bgp_dest_set_bgp_connected_ref_info(bn, NULL);
}

/* ========== 5. 自环 / 路由回注判断 ========== */
/* 【函数】bgp_nexthop_self —— 判断“这条路由的下一跳是否指向我本机地址”（next-hop-self）。
 * 用途：服务于**路由回注**——本机自己通告出去的路由又被自己收到时不应再算/再下发，
 *   否则会形成回环或错误选路。
 * 判据：
 *   1) 按路由类型确定地址族：BGP 静态路由用传入 afi；否则由属性里的下一跳类型
 *      (BGP_ATTR_NEXTHOP_AFI_IP6)决定；
 *   2) 由 attr 构造出实际下一跳地址 tmp_addr（静态用 net 前缀，否则用 attr->nexthop /
 *      mp_nexthop_global_in / mp_nexthop_global），查本机地址表 address_hash；
 *   3) IPv4 时若隧道端点表 tip_hash 非空，再用 attr 构造隧道端点地址查 tip_hash
 *      （PE 隧道端点的自环识别）。
 * 返回：命中本机地址返回 true，否则 false。 */
bool bgp_nexthop_self(struct bgp *bgp, afi_t afi, uint8_t type,
		      uint8_t sub_type, struct attr *attr,
		      struct bgp_dest *dest)
{
	uint8_t new_afi = afi == AFI_IP ? AF_INET : AF_INET6;
	struct bgp_addr tmp_addr = {{0}}, *addr = NULL;
	struct tip_addr tmp_tip, *tip = NULL;
	const struct prefix *p = bgp_dest_get_prefix(dest);
	bool is_bgp_static_route =
		((type == ZEBRA_ROUTE_BGP) && (sub_type == BGP_ROUTE_STATIC))
			? true
			: false;

	if (!is_bgp_static_route)
		new_afi = BGP_ATTR_NEXTHOP_AFI_IP6(attr) ? AF_INET6 : AF_INET;

	tmp_addr.p.family = new_afi;
	switch (new_afi) {
	case AF_INET:
		if (is_bgp_static_route) {
			tmp_addr.p.u.prefix4 = p->u.prefix4;
			tmp_addr.p.prefixlen = p->prefixlen;
		} else {
			/* Here we need to find out which nexthop to be used*/
			if (bgp_attr_exists(attr, BGP_ATTR_NEXT_HOP)) {
				tmp_addr.p.u.prefix4 = attr->nexthop;
				tmp_addr.p.prefixlen = IPV4_MAX_BITLEN;
			} else if ((attr->mp_nexthop_len) &&
				   ((attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV4) ||
				    (attr->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV4))) {
				tmp_addr.p.u.prefix4 =
					attr->mp_nexthop_global_in;
				tmp_addr.p.prefixlen = IPV4_MAX_BITLEN;
			} else
				return false;
		}
		break;
	case AF_INET6:
		if (is_bgp_static_route) {
			tmp_addr.p.u.prefix6 = p->u.prefix6;
			tmp_addr.p.prefixlen = p->prefixlen;
		} else {
			tmp_addr.p.u.prefix6 = attr->mp_nexthop_global;
			tmp_addr.p.prefixlen = IPV6_MAX_BITLEN;
		}
		break;
	default:
		break;
	}

	/* 命中本机地址表：下一跳就是我 → 属于自环/回注。 */
	addr = hash_lookup(bgp->address_hash, &tmp_addr);
	if (addr)
		return true;

	/* IPv4 且存在隧道端点(TIP)时，再用 attr 构造端点地址查 TIP 表（PE 场景）。 */
	if (new_afi == AF_INET && hashcount(bgp->tip_hash)) {
		memset(&tmp_tip, 0, sizeof(tmp_tip));
		SET_IPADDR_V4(&tmp_tip.addr);
		IPV4_ADDR_COPY(&tmp_tip.addr.ipaddr_v4, &attr->nexthop);

		if (bgp_attr_exists(attr, BGP_ATTR_NEXT_HOP)) {
			IPV4_ADDR_COPY(&tmp_tip.addr.ipaddr_v4, &attr->nexthop);
		} else if ((attr->mp_nexthop_len) &&
			   ((attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV4) ||
			    (attr->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV4))) {
			IPV4_ADDR_COPY(&tmp_tip.addr.ipaddr_v4, &attr->mp_nexthop_global_in);
		} else if ((attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV6_GLOBAL) ||
			   (attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL) ||
			   (attr->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV6_GLOBAL) ||
			   (attr->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV6_GLOBAL_AND_LL)) {
			SET_IPADDR_V6(&tmp_tip.addr);
			IPV6_ADDR_COPY(&tmp_tip.addr.ipaddr_v6, &attr->mp_nexthop_global);
		}

		tip = hash_lookup(bgp->tip_hash, &tmp_tip);
		if (tip)
			return true;
	}

	return false;
}

/* 【函数】bgp_hostroute_self —— 判断前缀 p 是否命中本机地址（/32 或 /128 主机路由）。
 * 实现：直接用 p 查本机地址表 address_hash（表里的键都是主机前缀）。同样是回注/自环
 *   防护的一部分。返回命中为 true。 */
bool bgp_hostroute_self(struct bgp *bgp, const struct prefix *p)
{
	struct bgp_addr tmp;

	tmp.p = *p;
	return hash_lookup(bgp->address_hash, &tmp) ? true : false;
}

/* ========== 6. multiaccess（同一网段是否有别的邻居）判断 ========== */
/* 【核心问题】该下一跳所在的网段上，除了我以外是否还有别的 BGP 邻居？若是（广播/共享
 *   网段），本端就不能把下一跳改成自己，也会影响 next-hop-self 决策与路由回注。
 * 【本函数】bgp_multiaccess_check_v4 —— 判断 IPv4 下一跳与 peer 的对端地址是否落在本机
 *   同一直连网段（connected_table 里同一节点）。
 * 实现：分别用 nexthop 与 peer 地址在 connected_table[AFI_IP] 里做 bgp_node_match，
 *   两节点相同即同一网段 → true；任一未命中 → false。注意节点用完要 unlock。 */
bool bgp_multiaccess_check_v4(struct in_addr nexthop, struct peer *peer)
{
	struct bgp_dest *dest1;
	struct bgp_dest *dest2;
	struct prefix p;
	int ret;

	p.family = AF_INET;
	p.prefixlen = IPV4_MAX_BITLEN;
	p.u.prefix4 = nexthop;

	dest1 = bgp_node_match(peer->bgp->connected_table[AFI_IP], &p);
	if (!dest1)
		return false;

	p.family = AF_INET;
	p.prefixlen = IPV4_MAX_BITLEN;
	p.u.prefix4 = peer->connection->su.sin.sin_addr;

	dest2 = bgp_node_match(peer->bgp->connected_table[AFI_IP], &p);
	if (!dest2) {
		bgp_dest_unlock_node(dest1);
		return false;
	}

	ret = (dest1 == dest2);

	bgp_dest_unlock_node(dest1);
	bgp_dest_unlock_node(dest2);

	return ret;
}

/* 【函数】bgp_multiaccess_check_v6 —— bgp_multiaccess_check_v4 的 IPv6 版本，
 *   逻辑一致，只是在本机 IPv6 直连表(connected_table[AFI_IP6])里匹配。
 *   （注意 IPv6 侧链路本地下一跳对应的直连网段通常不在 connected_table 中。） */
bool bgp_multiaccess_check_v6(struct in6_addr nexthop, struct peer *peer)
{
	struct bgp_dest *dest1;
	struct bgp_dest *dest2;
	struct prefix p;
	int ret;

	p.family = AF_INET6;
	p.prefixlen = IPV6_MAX_BITLEN;
	p.u.prefix6 = nexthop;

	dest1 = bgp_node_match(peer->bgp->connected_table[AFI_IP6], &p);
	if (!dest1)
		return false;

	p.family = AF_INET6;
	p.prefixlen = IPV6_MAX_BITLEN;
	p.u.prefix6 = peer->connection->su.sin6.sin6_addr;

	dest2 = bgp_node_match(peer->bgp->connected_table[AFI_IP6], &p);
	if (!dest2) {
		bgp_dest_unlock_node(dest1);
		return false;
	}

	ret = (dest1 == dest2);

	bgp_dest_unlock_node(dest1);
	bgp_dest_unlock_node(dest2);

	return ret;
}

/* 【函数】bgp_subgrp_multiaccess_check_v6 —— 在“一个更新子组(update_subgroup)”范围内
 *   判断是否 multiaccess。
 * 语义差异：不是只比某一个 peer，而是遍历该子组内**所有** peer（排除 exclude，通常
 *   是路由来源/source of route），只要有一个与本下一跳落在同一网段，就认为对“这一组
 *   要发给的 peer”而言是 multiaccess。
 * 返回：存在同网段成员 → true；否则 false。 */
bool bgp_subgrp_multiaccess_check_v6(struct in6_addr nexthop,
				     struct update_subgroup *subgrp,
				     struct peer *exclude)
{
	struct bgp_dest *dest1 = NULL, *dest2 = NULL;
	struct peer_af *paf = NULL;
	struct prefix p = {0}, np = {0};
	struct bgp *bgp = NULL;

	np.family = AF_INET6;
	np.prefixlen = IPV6_MAX_BITLEN;
	np.u.prefix6 = nexthop;

	p.family = AF_INET;
	p.prefixlen = IPV6_MAX_BITLEN;

	bgp = SUBGRP_INST(subgrp);
	dest1 = bgp_node_match(bgp->connected_table[AFI_IP6], &np);
	if (!dest1)
		return false;

	SUBGRP_FOREACH_PEER (subgrp, paf) {
		/* Skip peer we're told to exclude - e.g., source of route. */
		if (paf->peer == exclude)
			continue;

		p.u.prefix6 = paf->peer->connection->su.sin6.sin6_addr;
		dest2 = bgp_node_match(bgp->connected_table[AFI_IP6], &p);
		if (dest1 == dest2) {
			bgp_dest_unlock_node(dest1);
			bgp_dest_unlock_node(dest2);
			return true;
		}

		if (dest2)
			bgp_dest_unlock_node(dest2);
	}

	bgp_dest_unlock_node(dest1);
	return false;
}

/* 【函数】bgp_subgrp_multiaccess_check_v4 —— bgp_subgrp_multiaccess_check_v6 的 IPv4 版本。 */
bool bgp_subgrp_multiaccess_check_v4(struct in_addr nexthop,
				     struct update_subgroup *subgrp,
				     struct peer *exclude)
{
	struct bgp_dest *dest1, *dest2;
	struct peer_af *paf;
	struct prefix p, np;
	struct bgp *bgp;

	np.family = AF_INET;
	np.prefixlen = IPV4_MAX_BITLEN;
	np.u.prefix4 = nexthop;

	p.family = AF_INET;
	p.prefixlen = IPV4_MAX_BITLEN;

	bgp = SUBGRP_INST(subgrp);
	dest1 = bgp_node_match(bgp->connected_table[AFI_IP], &np);
	if (!dest1)
		return false;

	SUBGRP_FOREACH_PEER (subgrp, paf) {
		/* Skip peer we're told to exclude - e.g., source of route. */
		if (paf->peer == exclude)
			continue;

		p.u.prefix4 = paf->peer->connection->su.sin.sin_addr;

		dest2 = bgp_node_match(bgp->connected_table[AFI_IP], &p);
		if (dest1 == dest2) {
			bgp_dest_unlock_node(dest1);
			bgp_dest_unlock_node(dest2);
			return true;
		}

		if (dest2)
			bgp_dest_unlock_node(dest2);
	}

	bgp_dest_unlock_node(dest1);
	return false;
}

/* ========== 7. show 命令：下一跳缓存 / import 检查表的展示 ========== */
/* 【函数】bgp_show_bgp_path_info_flags —— 把 path->flags 的各标志位导出为 JSON 布尔，
 *   供 `show bgp nexthop ... json` 使用。非 JSON 模式由调用方另行文本输出。 */
static void bgp_show_bgp_path_info_flags(uint32_t flags, json_object *json)
{
	json_object *json_flags = NULL;

	if (!json)
		return;

	json_flags = json_object_new_object();
	json_object_boolean_add(json_flags, "igpChanged",
				CHECK_FLAG(flags, BGP_PATH_IGP_CHANGED));
	json_object_boolean_add(json_flags, "damped",
				CHECK_FLAG(flags, BGP_PATH_DAMPED));
	json_object_boolean_add(json_flags, "history",
				CHECK_FLAG(flags, BGP_PATH_HISTORY));
	json_object_boolean_add(json_flags, "bestpath",
				CHECK_FLAG(flags, BGP_PATH_SELECTED));
	json_object_boolean_add(json_flags, "valid",
				CHECK_FLAG(flags, BGP_PATH_VALID));
	json_object_boolean_add(json_flags, "attrChanged",
				CHECK_FLAG(flags, BGP_PATH_ATTR_CHANGED));
	json_object_boolean_add(json_flags, "deterministicMedCheck",
				CHECK_FLAG(flags, BGP_PATH_DMED_CHECK));
	json_object_boolean_add(json_flags, "deterministicMedSelected",
				CHECK_FLAG(flags, BGP_PATH_DMED_SELECTED));
	json_object_boolean_add(json_flags, "stale",
				CHECK_FLAG(flags, BGP_PATH_STALE));
	json_object_boolean_add(json_flags, "removed",
				CHECK_FLAG(flags, BGP_PATH_REMOVED));
	json_object_boolean_add(json_flags, "counted",
				CHECK_FLAG(flags, BGP_PATH_COUNTED));
	json_object_boolean_add(json_flags, "multipath",
				CHECK_FLAG(flags, BGP_PATH_MULTIPATH));
	json_object_boolean_add(json_flags, "multipathChanged",
				CHECK_FLAG(flags, BGP_PATH_MULTIPATH_CHG));
	json_object_boolean_add(json_flags, "ribAttributeChanged",
				CHECK_FLAG(flags, BGP_PATH_RIB_ATTR_CHG));
	json_object_boolean_add(json_flags, "nexthopSelf",
				CHECK_FLAG(flags, BGP_PATH_ANNC_NH_SELF));
	json_object_boolean_add(json_flags, "linkBandwidthChanged",
				CHECK_FLAG(flags, BGP_PATH_LINK_BW_CHG));
	json_object_boolean_add(json_flags, "acceptOwn",
				CHECK_FLAG(flags, BGP_PATH_ACCEPT_OWN));
	json_object_object_add(json, "flags", json_flags);
}

/* 【函数】bgp_show_nexthop_paths —— 打印某个 bnc 所依赖的所有路径（遍历 bnc->paths），
 *   即“哪些路由在用这个下一跳”。这正是 NHT 多对多关系在展示层的体现。 */
static void bgp_show_nexthop_paths(struct vty *vty, struct bgp *bgp,
				   struct bgp_nexthop_cache *bnc,
				   json_object *json)
{
	struct bgp_dest *dest;
	struct bgp_path_info *path;
	afi_t afi;
	safi_t safi;
	struct bgp_table *table;
	struct bgp *bgp_path;
	json_object *paths = NULL;
	json_object *json_path = NULL;

	if (json)
		paths = json_object_new_array();
	else
		vty_out(vty, "  Paths:\n");
	LIST_FOREACH (path, &(bnc->paths), nh_thread) {
		dest = path->net;
		assert(dest && bgp_dest_table(dest));
		afi = family2afi(bgp_dest_get_prefix(dest)->family);
		table = bgp_dest_table(dest);
		safi = table->safi;
		bgp_path = table->bgp;


		if (json) {
			json_path = json_object_new_object();
			json_object_string_add(json_path, "afi", afi2str(afi));
			json_object_string_add(json_path, "safi",
					       safi2str(safi));
			json_object_string_addf(json_path, "prefix", "%pBD",
						dest);
			if (dest->pdest)
				json_object_string_addf(
					json_path, "rd",
					BGP_RD_AS_FORMAT(bgp->asnotation),
					(struct prefix_rd *)bgp_dest_get_prefix(
						dest->pdest));
			json_object_string_add(
				json_path, "vrf",
				vrf_id_to_name(bgp_path->vrf_id));
			bgp_show_bgp_path_info_flags(path->flags, json_path);
			json_object_array_add(paths, json_path);
			continue;
		}
		if (dest->pdest) {
			vty_out(vty, "    %d/%d %pBD RD ", afi, safi, dest);
			vty_out(vty, BGP_RD_AS_FORMAT(bgp->asnotation),
				(struct prefix_rd *)bgp_dest_get_prefix(
					dest->pdest));
			vty_out(vty, " %s flags 0x%x\n", bgp_path->name_pretty,
				path->flags);
		} else
			vty_out(vty, "    %d/%d %pBD %s flags 0x%x\n",
				afi, safi, dest, bgp_path->name_pretty, path->flags);
	}
	if (json)
		json_object_object_add(json, "paths", paths);
}

/* 【函数】bgp_show_nexthops_detail —— 打印某 bnc 解析出的下一跳明细：按类型(IPV4/IPV6/
 *   IFINDEX/黑洞等)输出地址与接口，文本与 JSON 双模式。 */
static void bgp_show_nexthops_detail(struct vty *vty, struct bgp *bgp,
				     struct bgp_nexthop_cache *bnc,
				     json_object *json)
{
	struct nexthop *nexthop;
	json_object *json_gates = NULL;
	json_object *json_gate = NULL;

	if (json)
		json_gates = json_object_new_array();
	for (nexthop = bnc->nexthop; nexthop; nexthop = nexthop->next) {
		if (json) {
			json_gate = json_object_new_object();
			switch (nexthop->type) {
			case NEXTHOP_TYPE_IPV6:
				json_object_string_addf(json_gate, "ip", "%pI6",
							&nexthop->gate.ipv6);
				break;
			case NEXTHOP_TYPE_IPV6_IFINDEX:
				json_object_string_addf(json_gate, "ip", "%pI6",
							&nexthop->gate.ipv6);
				json_object_string_add(
					json_gate, "interfaceName",
					ifindex2ifname(
						bnc->ifindex_ipv6_ll
							? bnc->ifindex_ipv6_ll
							: nexthop->ifindex,
						bgp->vrf_id));
				json_object_int_add(json_gate, "ifindex",
						    bnc->ifindex_ipv6_ll ? bnc->ifindex_ipv6_ll
									 : nexthop->ifindex);
				break;
			case NEXTHOP_TYPE_IPV4:
				json_object_string_addf(json_gate, "ip", "%pI4",
							&nexthop->gate.ipv4);
				break;
			case NEXTHOP_TYPE_IFINDEX:
				json_object_string_add(
					json_gate, "interfaceName",
					ifindex2ifname(
						bnc->ifindex_ipv6_ll
							? bnc->ifindex_ipv6_ll
							: nexthop->ifindex,
						bgp->vrf_id));
				json_object_int_add(json_gate, "ifindex",
						    bnc->ifindex_ipv6_ll ? bnc->ifindex_ipv6_ll
									 : nexthop->ifindex);
				break;
			case NEXTHOP_TYPE_IPV4_IFINDEX:
				json_object_string_addf(json_gate, "ip", "%pI4",
							&nexthop->gate.ipv4);
				json_object_string_add(
					json_gate, "interfaceName",
					ifindex2ifname(
						bnc->ifindex_ipv6_ll
							? bnc->ifindex_ipv6_ll
							: nexthop->ifindex,
						bgp->vrf_id));
				json_object_int_add(json_gate, "ifindex",
						    bnc->ifindex_ipv6_ll ? bnc->ifindex_ipv6_ll
									 : nexthop->ifindex);
				break;
			case NEXTHOP_TYPE_BLACKHOLE:
				json_object_boolean_true_add(json_gate,
							     "unreachable");
				switch (nexthop->bh_type) {
				case BLACKHOLE_REJECT:
					json_object_boolean_true_add(json_gate,
								     "reject");
					break;
				case BLACKHOLE_ADMINPROHIB:
					json_object_boolean_true_add(
						json_gate, "adminProhibited");
					break;
				case BLACKHOLE_NULL:
					json_object_boolean_true_add(
						json_gate, "blackhole");
					break;
				case BLACKHOLE_UNSPEC:
					break;
				}
				break;
			default:
				break;
			}
			json_object_array_add(json_gates, json_gate);
			continue;
		}
		switch (nexthop->type) {
		case NEXTHOP_TYPE_IPV6:
		case NEXTHOP_TYPE_IPV6_IFINDEX:
			vty_out(vty, "  gate %pI6", &nexthop->gate.ipv6);
			if (nexthop->type == NEXTHOP_TYPE_IPV6_IFINDEX &&
			    bnc->ifindex_ipv6_ll)
				vty_out(vty, ", if %s, ifindex %d\n",
					ifindex2ifname(bnc->ifindex_ipv6_ll, bgp->vrf_id),
					bnc->ifindex_ipv6_ll);
			else if (nexthop->ifindex)
				vty_out(vty, ", if %s, ifindex %d\n",
					ifindex2ifname(nexthop->ifindex, bgp->vrf_id),
					nexthop->ifindex);
			else
				vty_out(vty, "\n");
			break;
		case NEXTHOP_TYPE_IPV4:
		case NEXTHOP_TYPE_IPV4_IFINDEX:
			vty_out(vty, "  gate %pI4", &nexthop->gate.ipv4);
			if (nexthop->type == NEXTHOP_TYPE_IPV4_IFINDEX &&
			    bnc->ifindex_ipv6_ll)
				vty_out(vty, ", if %s, ifindex %d\n",
					ifindex2ifname(bnc->ifindex_ipv6_ll, bgp->vrf_id),
					bnc->ifindex_ipv6_ll);
			else if (nexthop->ifindex)
				vty_out(vty, ", if %s, ifindex %d\n",
					ifindex2ifname(nexthop->ifindex, bgp->vrf_id),
					nexthop->ifindex);
			else
				vty_out(vty, "\n");
			break;
		case NEXTHOP_TYPE_IFINDEX:
			vty_out(vty, "  if %s, ifindex %d\n",
				ifindex2ifname(bnc->ifindex_ipv6_ll ? bnc->ifindex_ipv6_ll
								    : nexthop->ifindex,
					       bgp->vrf_id),
				bnc->ifindex_ipv6_ll ? bnc->ifindex_ipv6_ll : nexthop->ifindex);
			break;
		case NEXTHOP_TYPE_BLACKHOLE:
			vty_out(vty, "  blackhole\n");
			break;
		default:
			vty_out(vty, "  invalid nexthop type %u\n",
				nexthop->type);
		}
	}
	if (json)
		json_object_object_add(json, "nexthops", json_gates);
}

/* 【函数】bgp_show_nexthop —— 打印单个 bnc 的状态，是 show 的核心输出函数。
 * 三种状态分支：
 *   VALID             → "valid [IGP metric ..] #paths .."（可含 peer、EVPN 网关 IP、解析前缀）；
 *   EVPN_INCOMPLETE   → "overlay index unresolved ..."（L3 可达但 MAC/IP 未解析）；
 *   其它              → "invalid ..."（并提示 Must be Connected / Is not Registered）。
 * detail 为真时再调用 bgp_show_nexthops_detail / bgp_show_nexthop_paths 展开明细。 */
static void bgp_show_nexthop(struct vty *vty, struct bgp *bgp, struct bgp_nexthop_cache *bnc,
			     bool detail, bool uj)
{
	char buf[PREFIX2STR_BUFFER];
	time_t tbuf;
	char timebuf[32];
	struct peer *peer;
	json_object *json_last_update = NULL;
	json_object *json_nexthop = NULL;

	peer = (struct peer *)bnc->nht_info;

	if (uj)
		json_nexthop = json_object_new_object();
	if (bnc->srte_color) {
		if (uj)
			json_object_int_add(json_nexthop, "srteColor",
					    bnc->srte_color);
		else
			vty_out(vty, " SR-TE color %u -", bnc->srte_color);
	}
	inet_ntop(bnc->prefix.family, &bnc->prefix.u.prefix, buf, sizeof(buf));
	if (CHECK_FLAG(bnc->flags, BGP_NEXTHOP_VALID)) {
		if (uj) {
			json_object_boolean_true_add(json_nexthop, "valid");
			json_object_boolean_true_add(json_nexthop, "complete");
			json_object_int_add(json_nexthop, "igpMetric",
					    bnc->metric);
			json_object_int_add(json_nexthop, "pathCount",
					    bnc->path_count);
			if (peer)
				json_object_string_add(json_nexthop, "peer",
						       peer->host);
			if (bnc->is_evpn_gwip_nexthop)
				json_object_boolean_true_add(json_nexthop,
							     "isEvpnGatewayIp");
			json_object_string_addf(json_nexthop, "resolvedPrefix", "%pFX",
						&bnc->resolved_prefix);
		} else {
			vty_out(vty, " %s valid [IGP metric %d], #paths %d",
				buf, bnc->metric, bnc->path_count);
			if (peer)
				vty_out(vty, ", peer %s", peer->host);
			if (bnc->is_evpn_gwip_nexthop)
				vty_out(vty, " EVPN Gateway IP");
			vty_out(vty, "\n  Resolved prefix %pFX",
				&bnc->resolved_prefix);
			vty_out(vty, "\n");
		}
		bgp_show_nexthops_detail(vty, bgp, bnc, json_nexthop);
	} else if (CHECK_FLAG(bnc->flags, BGP_NEXTHOP_EVPN_INCOMPLETE)) {
		if (uj) {
			json_object_boolean_true_add(json_nexthop, "valid");
			json_object_boolean_false_add(json_nexthop, "complete");
			json_object_int_add(json_nexthop, "igpMetric",
					    bnc->metric);
			json_object_int_add(json_nexthop, "pathCount",
					    bnc->path_count);
			if (bnc->is_evpn_gwip_nexthop)
				json_object_boolean_true_add(json_nexthop,
							     "isEvpnGatewayIp");
		} else {
			vty_out(vty,
				" %s overlay index unresolved [IGP metric %d], #paths %d",
				buf, bnc->metric, bnc->path_count);
			if (bnc->is_evpn_gwip_nexthop)
				vty_out(vty, " EVPN Gateway IP");
			vty_out(vty, "\n");
		}
		bgp_show_nexthops_detail(vty, bgp, bnc, json_nexthop);
	} else {
		if (uj) {
			json_object_boolean_false_add(json_nexthop, "valid");
			json_object_boolean_false_add(json_nexthop, "complete");
			json_object_int_add(json_nexthop, "pathCount",
					    bnc->path_count);
			if (peer)
				json_object_string_add(json_nexthop, "peer",
						       peer->host);
			if (bnc->is_evpn_gwip_nexthop)
				json_object_boolean_true_add(json_nexthop,
							     "isEvpnGatewayIp");
			if (CHECK_FLAG(bnc->flags, BGP_NEXTHOP_CONNECTED))
				json_object_boolean_false_add(json_nexthop,
							      "isConnected");
			if (!CHECK_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED))
				json_object_boolean_false_add(json_nexthop,
							      "isRegistered");
		} else {
			vty_out(vty, " %s invalid, #paths %d", buf,
				bnc->path_count);
			if (peer)
				vty_out(vty, ", peer %s", peer->host);
			if (bnc->is_evpn_gwip_nexthop)
				vty_out(vty, " EVPN Gateway IP");
			vty_out(vty, "\n");
			if (CHECK_FLAG(bnc->flags, BGP_NEXTHOP_CONNECTED))
				vty_out(vty, "  Must be Connected\n");
			if (!CHECK_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED))
				vty_out(vty, "  Is not Registered\n");
		}
	}
	tbuf = time(NULL) - (monotime(NULL) - bnc->last_update);
	if (uj) {
		if (detail) {
			json_last_update = json_object_new_object();
			json_object_int_add(json_last_update, "epoch", tbuf);
			json_object_string_add(json_last_update, "string",
					       time_to_string_json(bnc->last_update, timebuf));
			json_object_object_add(json_nexthop, "lastUpdate",
					       json_last_update);
		} else {
			json_object_int_add(json_nexthop, "lastUpdate", tbuf);
		}
	} else {
		vty_out(vty, "  Last update: %s", time_to_string(bnc->last_update, timebuf));
	}

	/* show paths dependent on nexthop, if needed. */
	if (detail)
		bgp_show_nexthop_paths(vty, bgp, bnc, json_nexthop);

	if (uj) {
		vty_out(vty, "\"%s\":", buf);
		vty_json_no_pretty(vty, json_nexthop);
	}
}

/* 【函数】bgp_show_nexthops —— 遍历一张表(import 检查表或下一跳缓存表)并逐条打印。
 * afi 指定为 AFI_IP/AFI_IP6 时只列该族；为 AFI_UNSPEC 时两族都列。 */
static void bgp_show_nexthops(struct vty *vty, struct bgp *bgp, bool import_table, bool uj,
			      afi_t afi, bool detail)
{
	struct bgp_nexthop_cache *bnc;
	struct bgp_nexthop_cache_head(*tree)[AFI_MAX];
	bool found = false;
	bool firstafi = true;
	bool firstnh = true;

	if (!uj) {
		if (import_table)
			vty_out(vty, "Current BGP import check cache:\n");
		else
			vty_out(vty, "Current BGP nexthop cache:\n");
	}
	if (import_table)
		tree = &bgp->import_check_table;
	else
		tree = &bgp->nexthop_cache_table;

	if (afi == AFI_IP || afi == AFI_IP6) {
		if (uj)
			vty_out(vty, "%s:{", (afi == AFI_IP) ? "\"ipv4\"" : "\"ipv6\"");
		frr_each (bgp_nexthop_cache, &(*tree)[afi], bnc) {
			if (uj)
				vty_out(vty, "%s", firstnh ? "" : ",");
			bgp_show_nexthop(vty, bgp, bnc, detail, uj);
			found = true;
			firstnh = false;
		}
		if (found && uj)
			vty_out(vty, "}");
		return;
	}

	for (afi = AFI_IP; afi < AFI_MAX; afi++) {
		if (afi != AFI_IP && afi != AFI_IP6)
			continue;
		if (uj)
			vty_out(vty, "%s%s:{", firstafi ? "" : ",",
				(afi == AFI_IP) ? "\"ipv4\"" : "\"ipv6\"");
		firstafi = false;
		firstnh = true;
		frr_each (bgp_nexthop_cache, &(*tree)[afi], bnc) {
			if (uj)
				vty_out(vty, "%s", firstnh ? "" : ",");
			bgp_show_nexthop(vty, bgp, bnc, detail, uj);
			firstnh = false;
		}

		if (uj)
			vty_out(vty, "}");
	}
}

/* 【函数】show_ip_bgp_nexthop_table —— show 命令的公共入口：按 VRF 名取 bgp 实例，
 *   若给定 nhopip_str 则只输出该下一跳的条目，否则输出整张表（可选 detail/json）。
 * 返回 CMD_WARNING 表示实例不存在或地址非法。 */
static int show_ip_bgp_nexthop_table(struct vty *vty, const char *name, const char *nhopip_str,
				     bool import_table, bool uj, afi_t afi, bool detail)
{
	struct bgp *bgp;

	if (name && !strmatch(name, VRF_DEFAULT_NAME))
		bgp = bgp_lookup_by_name(name);
	else
		bgp = bgp_get_default();
	if (!bgp) {
		if (!uj)
			vty_out(vty, "%% No such BGP instance exist\n");
		return CMD_WARNING;
	}

	if (nhopip_str) {
		struct prefix nhop;
		struct bgp_nexthop_cache_head (*tree)[AFI_MAX];
		struct bgp_nexthop_cache *bnc;
		bool found = false;

		if (!str2prefix(nhopip_str, &nhop)) {
			if (!uj)
				vty_out(vty, "nexthop address is malformed\n");
			return CMD_WARNING;
		}
		tree = import_table ? &bgp->import_check_table
				    : &bgp->nexthop_cache_table;
		if (uj)
			vty_out(vty, "%s:{",
				(family2afi(nhop.family) == AFI_IP) ? "\"ipv4\"" : "\"ipv6\"");
		frr_each (bgp_nexthop_cache, &(*tree)[family2afi(nhop.family)],
			  bnc) {
			if (prefix_cmp(&bnc->prefix, &nhop))
				continue;
			bgp_show_nexthop(vty, bgp, bnc, true, uj);
			found = true;
		}
		if (!found && !uj)
			vty_out(vty, "nexthop %s does not have entry\n",
				nhopip_str);

		if (uj)
			vty_out(vty, "}");
	} else
		bgp_show_nexthops(vty, bgp, import_table, uj, afi, detail);

	return CMD_SUCCESS;
}

/* 【函数】bgp_show_all_instances_nexthops_vty —— 遍历所有 bgp 实例，逐个打印其下一跳表。 */
static void bgp_show_all_instances_nexthops_vty(struct vty *vty, bool uj, afi_t afi, bool detail)
{
	struct listnode *node, *nnode;
	struct bgp *bgp;
	const char *inst_name;
	bool firstinst = true;

	for (ALL_LIST_ELEMENTS(bm->bgp, node, nnode, bgp)) {
		inst_name = (bgp->inst_type == BGP_INSTANCE_TYPE_DEFAULT)
				    ? VRF_DEFAULT_NAME
				    : bgp->name;
		if (uj)
			vty_out(vty, "%s\"%s\":{", firstinst ? "" : ",", inst_name);

		else
			vty_out(vty, "\nInstance %s:\n", inst_name);

		bgp_show_nexthops(vty, bgp, false, uj, afi, detail);
		firstinst = false;
		if (uj)
			vty_out(vty, "}");
	}
}

#include "bgpd/bgp_nexthop_clippy.c"

/* 【命令】show [ip] bgp [vrf X] nexthop [...] —— 展示下一跳缓存表（可 detail/json）。 */
DEFPY (show_ip_bgp_nexthop,
       show_ip_bgp_nexthop_cmd,
       "show [ip] bgp [<view|vrf> VIEWVRFNAME$vrf] nexthop [<A.B.C.D|X:X::X:X>$nhop] [<ipv4$afi [A.B.C.D$nhop]|ipv6$afi [X:X::X:X$nhop]>] [detail$detail] [json$uj]",
       SHOW_STR
       IP_STR
       BGP_STR
       BGP_INSTANCE_HELP_STR
       "BGP nexthop table\n"
       "IPv4 nexthop address\n"
       "IPv6 nexthop address\n"
       "BGP nexthop IPv4 table\n"
       "IPv4 nexthop address\n"
       "BGP nexthop IPv6 table\n"
       "IPv6 nexthop address\n"
       "Show detailed information\n"
       JSON_STR)
{
	int rc = 0;
	afi_t afiz = AFI_UNSPEC;

	if (uj)
		vty_out(vty, "{\n");

	if (afi)
		afiz = bgp_vty_afi_from_str(afi);

	rc = show_ip_bgp_nexthop_table(vty, vrf, nhop_str, false, uj, afiz, detail);

	if (uj)
		vty_out(vty, "}\n");

	return rc;
}

/* 【命令】show [ip] bgp [vrf X] import-check-table —— 展示 import 检查表。 */
DEFPY (show_ip_bgp_import_check,
       show_ip_bgp_import_check_cmd,
       "show [ip] bgp [<view|vrf> VIEWVRFNAME$vrf] import-check-table [detail$detail] [json$uj]",
       SHOW_STR
       IP_STR
       BGP_STR
       BGP_INSTANCE_HELP_STR
       "BGP import check table\n"
       "Show detailed information\n"
       JSON_STR)
{
	int rc = 0;

	if (uj)
		vty_out(vty, "{\n");

	rc = show_ip_bgp_nexthop_table(vty, vrf, NULL, true, uj, AFI_UNSPEC, detail);

	if (uj)
		vty_out(vty, "}\n");

	return rc;
}

/* 【命令】show [ip] bgp vrf all nexthop —— 展示**所有实例**的下一跳表。 */
DEFPY (show_ip_bgp_instance_all_nexthop,
       show_ip_bgp_instance_all_nexthop_cmd,
       "show [ip] bgp <view|vrf> all nexthop [<ipv4|ipv6>$afi] [detail$detail] [json$uj]",
       SHOW_STR
       IP_STR
       BGP_STR
       BGP_INSTANCE_ALL_HELP_STR
       "BGP nexthop table\n"
       "BGP IPv4 nexthop table\n"
       "BGP IPv6 nexthop table\n"
       "Show detailed information\n"
       JSON_STR)
{
	afi_t afiz = AFI_UNSPEC;

	if (uj)
		vty_out(vty, "{");

	if (afi)
		afiz = bgp_vty_afi_from_str(afi);

	bgp_show_all_instances_nexthops_vty(vty, uj, afiz, detail);

	if (uj)
		vty_out(vty, "}");

	return CMD_SUCCESS;
}

/* ========== 8. 扫描层初始化/销毁与 flags 格式化 ========== */
/* 【函数】bgp_scan_init —— 每个 bgp 实例创建时调用：初始化本文件维护的数据结构。
 *   - IPv4/IPv6 各自的 nexthop_cache_table 与 import_check_table（bnc 红黑树）初始化；
 *   - connected_table[afi]（直连表，SAFI_UNICAST）创建。
 * 历史：早期 FRR 靠定期“扫描”下一跳，故函数名沿用 scan；现为 zebra 事件驱动。 */
void bgp_scan_init(struct bgp *bgp)
{
	afi_t afi;

	for (afi = AFI_IP; afi < AFI_MAX; afi++) {
		bgp_nexthop_cache_init(&bgp->nexthop_cache_table[afi]);
		bgp_nexthop_cache_init(&bgp->import_check_table[afi]);
		bgp->connected_table[afi] = bgp_table_init(bgp, afi,
			SAFI_UNICAST);
	}
}

/* 【函数】bgp_scan_vty_init —— 进程级只调用一次：把本文件的 show 命令注册进 vty。 */
void bgp_scan_vty_init(void)
{
	install_element(VIEW_NODE, &show_ip_bgp_nexthop_cmd);
	install_element(VIEW_NODE, &show_ip_bgp_import_check_cmd);
	install_element(VIEW_NODE, &show_ip_bgp_instance_all_nexthop_cmd);
}

/* 【函数】bgp_scan_finish —— 实例销毁时调用：重置两张 bnc 表（连同其路径映射一起清），
 *   并给 connected_table 挂上 cleanup 回调后解锁释放该表。 */
void bgp_scan_finish(struct bgp *bgp)
{
	afi_t afi;

	for (afi = AFI_IP; afi < AFI_MAX; afi++) {
		/* Only the current one needs to be reset. */
		bgp_nexthop_cache_reset(&bgp->nexthop_cache_table[afi]);
		bgp_nexthop_cache_reset(&bgp->import_check_table[afi]);

		bgp->connected_table[afi]->route_table->cleanup =
			bgp_connected_cleanup;
		bgp_table_unlock(bgp->connected_table[afi]);
		bgp->connected_table[afi] = NULL;
	}
}

/* 【函数】bgp_nexthop_dump_bnc_flags —— 把 bnc->flags 拼成可读字符串（show/日志用），
 *   例如 "Valid Reg Conn Static ... "。buf 至少 BNC_FLAG_DUMP_SIZE 字节。 */
char *bgp_nexthop_dump_bnc_flags(struct bgp_nexthop_cache *bnc, char *buf,
				 size_t len)
{
	if (bnc->flags == 0) {
		snprintfrr(buf, len, "None ");
		return buf;
	}

	snprintfrr(buf, len, "%s%s%s%s%s%s%s",
		   CHECK_FLAG(bnc->flags, BGP_NEXTHOP_VALID) ? "Valid " : "",
		   CHECK_FLAG(bnc->flags, BGP_NEXTHOP_REGISTERED) ? "Reg " : "",
		   CHECK_FLAG(bnc->flags, BGP_NEXTHOP_CONNECTED) ? "Conn " : "",
		   CHECK_FLAG(bnc->flags, BGP_NEXTHOP_PEER_NOTIFIED) ? "Notify "
								     : "",
		   CHECK_FLAG(bnc->flags, BGP_STATIC_ROUTE) ? "Static " : "",
		   CHECK_FLAG(bnc->flags, BGP_STATIC_ROUTE_EXACT_MATCH)
			   ? "Static Exact "
			   : "",
		   CHECK_FLAG(bnc->flags, BGP_NEXTHOP_LABELED_VALID)
			   ? "Label Valid "
			   : "");

	return buf;
}

/* 【函数】bgp_nexthop_dump_bnc_change_flags —— 把 bnc->change_flags 拼成可读字符串，
 *   例如 "Changed Metric "。
 * 【易误解点】下面的提前返回判断的是 bnc->flags（状态位）而不是 change_flags（变化位），
 *   属既有写法；阅读时勿据此推断 change_flags 与 flags 同义。 */
char *bgp_nexthop_dump_bnc_change_flags(struct bgp_nexthop_cache *bnc,
					char *buf, size_t len)
{
	if (bnc->flags == 0) {
		snprintfrr(buf, len, "None ");
		return buf;
	}

	snprintfrr(buf, len, "%s%s",
		   CHECK_FLAG(bnc->change_flags, BGP_NEXTHOP_CHANGED) ? "Changed " : "",
		   CHECK_FLAG(bnc->change_flags, BGP_NEXTHOP_METRIC_CHANGED) ? "Metric " : "");

	return buf;
}
