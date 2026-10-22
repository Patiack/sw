// SPDX-License-Identifier: GPL-2.0-or-later
/* Ethernet-VPN Packet and vty Processing File
 * Copyright (C) 2016 6WIND
 * Copyright (C) 2017 Cumulus Networks, Inc.
 */
/* ============================================================================
 * 【文件导读】bgp_evpn.c —— EVPN 子系统的主体实现(路由编解码/导入导出/本地学习/下发内核)
 * ============================================================================
 * 【职责与定位】
 *   本文件是 EVPN 的“中枢实现”，体量很大，几乎涵盖除多归属(bgp_evpn_mh.c)与
 *   命令层(bgp_evpn_vty.c)之外的全部逻辑。可粗分为：
 *     1) 内部哈希/列表的键比较与 RT 映射(import_rt / vrf_import_rt，
 *        remote_ip_hash，vni_svi_hash)；
 *     2) NLRI 编解码(编码 bgp_evpn_encode_prefix / 解析 bgp_nlri_parse_evpn)；
 *     3) 路由导入导出(按 RT 把 EVPN 路由落到 VNI/VRF，Type-5 的导出)；
 *     4) 本地事件处理(来自 zebra 的本地 MAC/IP、L2VNI、L3VNI 增删)；
 *     5) 路由选择与向 zebra 下发/撤销(evpn_zebra_install / uninstall)；
 *     6) VNI/VRF 的生命周期、RD/RT 的自动派生与配置联动的辅助函数。
 *
 * 【核心数据结构 / 关键概念】(详见 bgp_evpn_private.h 与 bgp_evpn.h)
 *   - struct bgpevpn：一个 L2 VNI(MAC-VRF)的运行时对象，含 mac_table /
 *     ip_table / remote_ip_hash / RD / RT 列表等；存于 bgp->vni_hash。
 *   - 三种“RT 映射”：import_rt_hash(IRT: 一个 RT → 多个 VNI)、
 *     vrf_import_rt_hash(一个 RT → 多个 VRF)、以及每个 VRF 上的 RT 列表。
 *     这是“收到路由该导入到哪些 VNI/VRF”的索引。
 *   - EVPN 路由类型：Type-1 EAD / Type-2 MAC-IP / Type-3 IMET / Type-4 ES /
 *     Type-5 IP-Prefix。路由键 = (RD, 类型相关字段)，标签里装的是 VNI。
 *   - RD / RT / ESI / Ethernet Tag / SoO / MAC Mobility / RouterMAC 等扩展团体
 *     (编码器见 bgp_evpn_private.h 的 encode_xxx_extcomm 族)。
 *
 * 【本文件在主流程中的位置】
 *   上游：bgp_packet.c 收到 UPDATE → bgp_nlri_parse_evpn(本文件)；
 *         bgp_zebra.c 收到本地事件 → bgp_evpn_local_* (本文件)。
 *   下游：本文件把结果下发 zebra(evpn_zebra_install/uninstall)，并维护
 *         bgp->vni_hash 与各 VRF 的导入结果(VNI 的 mac/ip 表)。
 *   多归属 ES/DF/别名：委托给 bgp_evpn_mh.c。
 *
 * 【易错点 / 阅读建议】
 *   1) 区分“全局 EVPN 表”(默认实例)与“某 VNI 的 mac/ip 表”：同名概念在不同
 *      表里语义不同(见 bgp_evpn_private.h 的 global_* 与 vni_* 节点函数)。
 *   2) 导入判定会同时看 RT 匹配、SoO(防环)和 martian(过滤)——顺序与条件相互
 *      影响，读 bgp_evpn_import_route 时需一起看。
 *   3) 引用计数：bgpevpn 被多处引用(bgp->vni_hash、VRF 的 l2vnis、ES-EVI 等)，
 *      释放走 bgp_evpn_free 或 QOBJ/引用计数，别直接 XFREE。
 *   建议阅读顺序：先读顶部哈希键/RT 映射小函数 → is_route_injectable_*
 *     → NLRI 编解码 → 导入导出主流程 → 本地事件 → zebra 下发。
 *   (本批注释覆盖到文件开头的若干辅助/主函数，见文末说明。)
 * ============================================================================
 */

#include <zebra.h>

#include "command.h"
#include "filter.h"
#include "prefix.h"
#include "log.h"
#include "memory.h"
#include "stream.h"
#include "hash.h"
#include "jhash.h"
#include "zclient.h"

#include "lib/printfrr.h"

#include "bgpd/bgp_attr_evpn.h"
#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_mplsvpn.h"
#include "bgpd/bgp_label.h"
#include "bgpd/bgp_evpn.h"
#include "bgpd/bgp_evpn_private.h"
#include "bgpd/bgp_evpn_mh.h"
#include "bgpd/bgp_ecommunity.h"
#include "bgpd/bgp_encap_types.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_errors.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_zebra.h"
#include "bgpd/bgp_nexthop.h"
#include "bgpd/bgp_addpath.h"
#include "bgpd/bgp_mac.h"
#include "bgpd/bgp_vty.h"
#include "bgpd/bgp_nht.h"
#include "bgpd/bgp_trace.h"
#include "bgpd/bgp_mpath.h"
#include "bgpd/bgp_packet.h"

/* 【定义与外部声明】注册 QOBJ 类型与内存类型(用于对象生命周期、内存统计/追踪)。
 * bgpevpn 定义见 bgp_evpn_private.h；bgp_evpn_es 定义见 bgp_evpn_mh.h。 */
/*
 * Definitions and external declarations.
 */
DEFINE_QOBJ_TYPE(bgpevpn);
DEFINE_QOBJ_TYPE(bgp_evpn_es);

/* BGP_EVPN_INFO: struct bgp_evpn_info 的内存类型；
 * VRF_ROUTE_TARGET: struct vrf_route_target(L3 RT 包装)的内存类型。 */
DEFINE_MTYPE_STATIC(BGPD, BGP_EVPN_INFO, "BGP EVPN instance information");
DEFINE_MTYPE_STATIC(BGPD, VRF_ROUTE_TARGET, "L3 Route Target");

/* 【静态函数前向声明】本文件内部使用的 static 函数，分组为：
 * remote_ip_hash 系列(以网关 IP 为键做递归查表)、vni_svi_hash 系列
 * (VNI 与 SVI 的哈希链接)、remote_ip 下一跳处理系列、addpath id 计算。 */
/*
 * Static function declarations
 */
static void bgp_evpn_remote_ip_hash_init(struct bgpevpn *evpn);
static void bgp_evpn_remote_ip_hash_destroy(struct bgpevpn *evpn);
static void bgp_evpn_remote_ip_hash_add(struct bgpevpn *vpn,
					struct bgp_path_info *pi);
static void bgp_evpn_remote_ip_hash_del(struct bgpevpn *vpn,
					struct bgp_path_info *pi);
static void bgp_evpn_remote_ip_hash_iterate(struct bgpevpn *vpn,
					    void (*func)(struct hash_bucket *,
							 void *),
					    void *arg);
static void bgp_evpn_link_to_vni_svi_hash(struct bgp *bgp, struct bgpevpn *vpn);
static void bgp_evpn_unlink_from_vni_svi_hash(struct bgp *bgp,
					      struct bgpevpn *vpn);
static unsigned int vni_svi_hash_key_make(const void *p);
static bool vni_svi_hash_cmp(const void *p1, const void *p2);
static void bgp_evpn_remote_ip_process_nexthops(struct bgpevpn *vpn,
						struct ipaddr *addr,
						bool resolve);
static void bgp_evpn_remote_ip_hash_link_nexthop(struct hash_bucket *bucket,
						 void *args);
static void bgp_evpn_remote_ip_hash_unlink_nexthop(struct hash_bucket *bucket,
						   void *args);
/* 【静态变量】zero_vtep_ip —— 一个全 0 的 IPv4 地址常量，用作“无/空 VTEP IP”
 * 的占位(避免到处构造临时结构)。 */
static struct ipaddr zero_vtep_ip = {
	.ipa_type = IPADDR_V4,
	.ip = {
		._v4_addr = {0},
	}
};

static uint32_t bgp_evpn_addpath_id_for_path(const struct bgp *bgp, const struct bgp_path_info *pi,
					     afi_t afi);

/* 【函数】vxlan_flood_control_str —— 把 BUM 泛洪控制枚举转成可读字符串，
 * 供 show/日志使用：Flooding(头端复制)、Disabled(禁用)、PIM-SM、Inherit Global
 * (继承全局)；其它值返回 “unknown”。 */
static const char *vxlan_flood_control_str(enum vxlan_flood_control flood_ctrl)
{
	switch (flood_ctrl) {
	case VXLAN_FLOOD_HEAD_END_REPL:
		return "Flooding";
	case VXLAN_FLOOD_DISABLED:
		return "Disabled";
	case VXLAN_FLOOD_PIM_SM:
		return "PIM-SM";
	case VXLAN_FLOOD_INHERIT_GLOBAL:
		return "Inherit Global";
	default:
		return "unknown";
	}
}

/* 【私有函数区】以下为本文件内部使用的 static 函数。 */
/*
 * Private functions.
 */

/* 【函数】vni_hash_key_make —— VNI 哈希(bgp->vni_hash)的键生成：直接对 vpn->vni
 * 做 jhash_1word。 */
/*
 * Make vni hash key.
 */
static unsigned int vni_hash_key_make(const void *p)
{
	const struct bgpevpn *vpn = p;
	return (jhash_1word(vpn->vni, 0));
}

/* 【函数】vni_hash_cmp —— VNI 哈希的相等比较：两 bgpevpn 的 vni 号相同即相等。 */
/*
 * Comparison function for vni hash
 */
static bool vni_hash_cmp(const void *p1, const void *p2)
{
	const struct bgpevpn *vpn1 = p1;
	const struct bgpevpn *vpn2 = p2;

	return vpn1->vni == vpn2->vni;
}

/* 【函数】vni_list_cmp —— VNI 链表排序比较：按 vni 号升序(供 list 的 sorted 操作)。 */
int vni_list_cmp(void *p1, void *p2)
{
	const struct bgpevpn *vpn1 = p1;
	const struct bgpevpn *vpn2 = p2;

	return vpn1->vni - vpn2->vni;
}

/* 【函数】vrf_import_rt_hash_key_make —— VRF 导入 RT 哈希
 * (bgp_evpn->vrf_import_rt_hash)的键生成：对 RT 的 8 字节 val 做 jhash。 */
/*
 * Make vrf import route target hash key.
 */
static unsigned int vrf_import_rt_hash_key_make(const void *p)
{
	const struct vrf_irt_node *irt = p;
	const uint8_t *pnt = irt->rt.val;

	return jhash(pnt, 8, 0x5abc1234);
}

/* 【函数】vrf_import_rt_hash_cmp —— VRF 导入 RT 哈希的相等比较：RT 的 8 字节相同。 */
/*
 * Comparison function for vrf import rt hash
 */
static bool vrf_import_rt_hash_cmp(const void *p1, const void *p2)
{
	const struct vrf_irt_node *irt1 = p1;
	const struct vrf_irt_node *irt2 = p2;

	return (memcmp(irt1->rt.val, irt2->rt.val, ECOMMUNITY_SIZE) == 0);
}

/* 【函数】vrf_import_rt_new —— 新建一个 vrf_irt_node 并挂入
 * bgp_evpn->vrf_import_rt_hash。先取默认 EVPN 实例(不存在则报错返回 NULL)；
 * 分配、填 rt、建空 vrfs 列表，最后 hash_get 插入。 */
/*
 * Create a new vrf import_rt in evpn instance
 */
static struct vrf_irt_node *vrf_import_rt_new(struct ecommunity_val *rt)
{
	struct bgp *bgp_evpn = NULL;
	struct vrf_irt_node *irt;

	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn) {
		flog_err(EC_BGP_NO_DFLT,
			 "vrf import rt new - evpn instance not created yet");
		return NULL;
	}

	irt = XCALLOC(MTYPE_BGP_EVPN_VRF_IMPORT_RT,
		      sizeof(struct vrf_irt_node));

	irt->rt = *rt;
	irt->vrfs = list_new();

	/* Add to hash */
	(void)hash_get(bgp_evpn->vrf_import_rt_hash, irt, hash_alloc_intern);

	return irt;
}

/* 【函数】vrf_import_rt_free —— 从哈希摘除并释放一个 vrf_irt_node(含其 vrfs 列表)。
 * 需要默认 EVPN 实例存在以拿到哈希表。 */
/*
 * Free the vrf import rt node
 */
static void vrf_import_rt_free(struct vrf_irt_node *irt)
{
	struct bgp *bgp_evpn = NULL;

	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn) {
		flog_err(EC_BGP_NO_DFLT,
			 "vrf import rt free - evpn instance not created yet");
		return;
	}

	hash_release(bgp_evpn->vrf_import_rt_hash, irt);
	list_delete(&irt->vrfs);
	XFREE(MTYPE_BGP_EVPN_VRF_IMPORT_RT, irt);
}

/* 【函数】hash_vrf_import_rt_free —— 哈希销毁时的元素回调：仅释放节点内存
 * (不摘除、不释放列表——应由释放流程先行处理)。 */
static void hash_vrf_import_rt_free(struct vrf_irt_node *irt)
{
	XFREE(MTYPE_BGP_EVPN_VRF_IMPORT_RT, irt);
}

/* 【函数】lookup_vrf_import_rt —— 按 RT 在 bgp_evpn->vrf_import_rt_hash 中查找
 * vrf_irt_node(即“哪些 VRF 会导入带此 RT 的路由”)。用栈上临时结构做键。 */
/*
 * Function to lookup Import RT node - used to map a RT to set of
 * VNIs importing routes with that RT.
 */
static struct vrf_irt_node *lookup_vrf_import_rt(struct ecommunity_val *rt)
{
	struct bgp *bgp_evpn = NULL;
	struct vrf_irt_node *irt;
	struct vrf_irt_node tmp;

	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn) {
		flog_err(
			EC_BGP_NO_DFLT,
			"vrf import rt lookup - evpn instance not created yet");
		return NULL;
	}

	memset(&tmp, 0, sizeof(tmp));
	memcpy(&tmp.rt, rt, ECOMMUNITY_SIZE);
	irt = hash_lookup(bgp_evpn->vrf_import_rt_hash, &tmp);
	return irt;
}

/* 【函数】is_vrf_present_in_irt_vrfs —— 该 VRF 是否已在某 RT 的“导入 VRF 列表”
 * 中(指针比较)。返回 1/0。 */
/*
 * Is specified VRF present on the RT's list of "importing" VRFs?
 */
static int is_vrf_present_in_irt_vrfs(struct list *vrfs, struct bgp *bgp_vrf)
{
	struct listnode *node = NULL, *nnode = NULL;
	struct bgp *tmp_bgp_vrf = NULL;

	for (ALL_LIST_ELEMENTS(vrfs, node, nnode, tmp_bgp_vrf)) {
		if (tmp_bgp_vrf == bgp_vrf)
			return 1;
	}
	return 0;
}

/* 【函数】import_rt_hash_key_make —— VNI 导入 RT 哈希(bgp->import_rt_hash)的
 * 键生成：对 RT 的 8 字节 val 做 jhash。 */
/*
 * Make import route target hash key.
 */
static unsigned int import_rt_hash_key_make(const void *p)
{
	const struct irt_node *irt = p;
	const uint8_t *pnt = irt->rt.val;

	return jhash(pnt, 8, 0xdeadbeef);
}

/* 【函数】import_rt_hash_cmp —— VNI 导入 RT 哈希的相等比较：RT 的 8 字节相同。 */
/*
 * Comparison function for import rt hash
 */
static bool import_rt_hash_cmp(const void *p1, const void *p2)
{
	const struct irt_node *irt1 = p1;
	const struct irt_node *irt2 = p2;

	return (memcmp(irt1->rt.val, irt2->rt.val, ECOMMUNITY_SIZE) == 0);
}

/* 【函数】import_rt_new —— 新建一个 irt_node 并挂入 bgp->import_rt_hash。
 * 分配、填 rt、建空 vnis 列表，最后 hash_get 插入。 */
/*
 * Create a new import_rt
 */
static struct irt_node *import_rt_new(struct bgp *bgp,
				      struct ecommunity_val *rt)
{
	struct irt_node *irt;

	irt = XCALLOC(MTYPE_BGP_EVPN_IMPORT_RT, sizeof(struct irt_node));

	irt->rt = *rt;
	irt->vnis = list_new();

	/* Add to hash */
	(void)hash_get(bgp->import_rt_hash, irt, hash_alloc_intern);

	return irt;
}

/* 【函数】import_rt_free —— 从哈希摘除并释放一个 irt_node(含其 vnis 列表)。 */
/*
 * Free the import rt node
 */
static void import_rt_free(struct bgp *bgp, struct irt_node *irt)
{
	hash_release(bgp->import_rt_hash, irt);
	list_delete(&irt->vnis);
	XFREE(MTYPE_BGP_EVPN_IMPORT_RT, irt);
}

/* 【函数】hash_import_rt_free —— 哈希销毁时的元素回调：仅释放节点内存。 */
static void hash_import_rt_free(struct irt_node *irt)
{
	XFREE(MTYPE_BGP_EVPN_IMPORT_RT, irt);
}

/* 【函数】lookup_import_rt —— 按 RT 在 bgp->import_rt_hash 中查找 irt_node
 * (即“哪些 VNI 会导入带此 RT 的路由”)。 */
/*
 * Function to lookup Import RT node - used to map a RT to set of
 * VNIs importing routes with that RT.
 */
static struct irt_node *lookup_import_rt(struct bgp *bgp,
					 struct ecommunity_val *rt)
{
	struct irt_node *irt;
	struct irt_node tmp;

	memset(&tmp, 0, sizeof(tmp));
	memcpy(&tmp.rt, rt, ECOMMUNITY_SIZE);
	irt = hash_lookup(bgp->import_rt_hash, &tmp);
	return irt;
}

/* 【函数】is_vni_present_in_irt_vnis —— 该 VNI 是否已在某 RT 的“导入 VNI 列表”
 * 中(指针比较)。返回 1/0。 */
/*
 * Is specified VNI present on the RT's list of "importing" VNIs?
 */
static int is_vni_present_in_irt_vnis(struct list *vnis, struct bgpevpn *vpn)
{
	struct listnode *node, *nnode;
	struct bgpevpn *tmp_vpn;

	for (ALL_LIST_ELEMENTS(vnis, node, nnode, tmp_vpn)) {
		if (tmp_vpn == vpn)
			return 1;
	}

	return 0;
}

/* 【函数】is_route_injectable_into_evpn_non_supp —— 判断某路径能否注入 EVPN
 * (不考虑聚合抑制)：非导入路由、或虽是导入但其根父路由不在 L2VPN/EVPN 表中
 * (即不是从 EVPN 导来的)，都视为可注入；反之(从 EVPN 导入的路由)不可注入，
 * 避免环回。返回 true/false。 */
/* Flag if the route is injectable into EVPN.
 * This would be following category:
 * Non-imported route,
 * Non-EVPN imported route,
 */
bool is_route_injectable_into_evpn_non_supp(struct bgp_path_info *pi)
{
	struct bgp_path_info *parent_pi;
	struct bgp_table *table;
	struct bgp_dest *dest;

	if (pi->sub_type != BGP_ROUTE_IMPORTED || !pi->extra ||
	    !pi->extra->vrfleak || !pi->extra->vrfleak->parent)
		return true;

        parent_pi = (struct bgp_path_info *)pi->extra->vrfleak->parent;
        dest = parent_pi->net;
        if (!dest)
		return true;
        table = bgp_dest_table(dest);
        if (table &&
            table->afi == AFI_L2VPN &&
            table->safi == SAFI_EVPN)
                return false;

        return true;
}

/* 【函数】is_route_injectable_into_evpn —— 在上一个函数基础上再排除“聚合抑制
 * 的路由”(bgp_path_suppressed)，即：非导入、非 EVPN 导入、且非聚合抑制才可注入。
 * 命中抑制时会打一条 trace 便于排查。 */
/* Flag if the route is injectable into EVPN.
 * This would be following category:
 * Non-imported route,
 * Non-EVPN imported route,
 * Non Aggregate suppressed route.
 */
bool is_route_injectable_into_evpn(struct bgp_path_info *pi)
{
	/* do not import aggr suppressed routes */
	if (bgp_path_suppressed(pi)) {
		frrtrace(2, frr_bgp, evpn_ignore_suppress_route, pi->net, pi->peer);
		return false;
	}

	return is_route_injectable_into_evpn_non_supp(pi);
}

/* 【函数】bgp_evpn_route_target_cmp —— 比较两个 RT(ecommunity)：NULL 视为最小，
 * 都有 str 时按 strcmp；返回 <0/0/>0。用于 list 的排序/查找。 */
/*
 * Compare Route Targets.
 */
int bgp_evpn_route_target_cmp(struct ecommunity *ecom1,
			      struct ecommunity *ecom2)
{
	if (ecom1 && !ecom2)
		return -1;

	if (!ecom1 && ecom2)
		return 1;

	if (!ecom1 && !ecom2)
		return 0;

	if (ecom1->str && !ecom2->str)
		return -1;

	if (!ecom1->str && ecom2->str)
		return 1;

	if (!ecom1->str && !ecom2->str)
		return 0;

	return strcmp(ecom1->str, ecom2->str);
}

/* 【函数】evpn_vrf_route_target_cmp —— L3 RT 包装的排序比较：委托
 * bgp_evpn_route_target_cmp 比较各自的 ecom。 */
/*
 * Compare L3 Route Targets.
 */
static int evpn_vrf_route_target_cmp(struct vrf_route_target *rt1,
				     struct vrf_route_target *rt2)
{
	return bgp_evpn_route_target_cmp(rt1->ecom, rt2->ecom);
}

/* 【函数】bgp_evpn_xxport_delete_ecomm —— 释放一个用于导入/导出的 ecommunity
 * (list 的元素释放回调)。 */
void bgp_evpn_xxport_delete_ecomm(void *val)
{
	struct ecommunity *ecomm = val;
	ecommunity_free(&ecomm);
}

/* 【函数】evpn_vrf_rt_del —— 释放一个 L3 RT 包装(先释放其 ecom，再释放结构)。 */
/*
 * Delete l3 Route Target.
 */
static void evpn_vrf_rt_del(void *val)
{
	struct vrf_route_target *l3rt = val;

	ecommunity_free(&l3rt->ecom);

	XFREE(MTYPE_VRF_ROUTE_TARGET, l3rt);
}

/* 【函数】evpn_vrf_rt_new —— 分配一个 L3 RT 包装并挂上给定 ecom(移交所有权)。 */
/*
 * Allocate a new l3 Route Target.
 */
static struct vrf_route_target *evpn_vrf_rt_new(struct ecommunity *ecom)
{
	struct vrf_route_target *l3rt;

	l3rt = XCALLOC(MTYPE_VRF_ROUTE_TARGET, sizeof(struct vrf_route_target));

	l3rt->ecom = ecom;

	return l3rt;
}

/* 【函数】mask_ecom_global_admin —— 把某 RT 扩展团体的 global-admin 字段屏蔽，
 * 仅保留 local-admin 字段写入 dst。用于“按 local-admin 匹配”的 RT 比较(如自动
 * 派生 RT 的宽松匹配)。 */
/*
 * Mask off global-admin field of specified extended community (RT),
 * just retain the local-admin field.
 */
static inline void mask_ecom_global_admin(struct ecommunity_val *dst,
					  const struct ecommunity_val *src)
{
	uint8_t type;

	type = src->val[0];
	dst->val[0] = 0;
	if (type == ECOMMUNITY_ENCODE_AS) {
		dst->val[2] = dst->val[3] = 0;
	} else if (type == ECOMMUNITY_ENCODE_AS4
		   || type == ECOMMUNITY_ENCODE_IP) {
		dst->val[2] = dst->val[3] = 0;
		dst->val[4] = dst->val[5] = 0;
	}
}

/* 【函数】vrf_rt2ecom_val —— 取 L3 RT 包装里第 iter 个扩展团体值写入 to_eval；
 * 若该 RT 带 AUTO(自动)或 WILD(通配)标志，则只保留 local-admin(屏蔽 global-admin)，
 * 以便用 (L3VNI) 作 RT 时兼容任意 ASN。 */
/*
 * Converts the RT to Ecommunity Value and adjusts masking based
 * on flags set for RT.
 */
static void vrf_rt2ecom_val(struct ecommunity_val *to_eval,
			    const struct vrf_route_target *l3rt, int iter)
{
	const struct ecommunity_val *eval;

	eval = (const struct ecommunity_val *)(l3rt->ecom->val +
					       (iter * ECOMMUNITY_SIZE));
	/* If using "automatic" or "wildcard *" RT,
	 * we only care about the local-admin sub-field.
	 * This is to facilitate using L3VNI(VRF-VNI)
	 * as the RT for EBGP peering too and simplify
	 * configurations by allowing any ASN via '*'.
	 */
	memcpy(to_eval, eval, ECOMMUNITY_SIZE);

	if (CHECK_FLAG(l3rt->flags, BGP_VRF_RT_AUTO) ||
	    CHECK_FLAG(l3rt->flags, BGP_VRF_RT_WILD))
		mask_ecom_global_admin(to_eval, eval);
}

/* 【函数】map_vrf_to_rt —— 把某 L3 RT 的一个包装映射到指定 VRF：对该包装的每个
 * 扩展团体值取 local-admin(见 vrf_rt2ecom_val)，查/建 vrf_irt_node，并把 VRF 加入
 * 其 vrfs 列表(已映射则跳过)。 */
/*
 * Map one RT to specified VRF.
 * bgp_vrf = BGP vrf instance
 */
static void map_vrf_to_rt(struct bgp *bgp_vrf, struct vrf_route_target *l3rt)
{
	uint32_t i = 0;

	for (i = 0; i < l3rt->ecom->size; i++) {
		struct vrf_irt_node *irt = NULL;
		struct ecommunity_val eval_tmp;

		/* Adjust masking for value */
		vrf_rt2ecom_val(&eval_tmp, l3rt, i);

		irt = lookup_vrf_import_rt(&eval_tmp);

		if (irt && is_vrf_present_in_irt_vrfs(irt->vrfs, bgp_vrf))
			return; /* Already mapped. */

		if (!irt)
			irt = vrf_import_rt_new(&eval_tmp);

		/* Add VRF to the list for this RT. */
		listnode_add(irt->vrfs, bgp_vrf);
	}
}

/* 【函数】unmap_vrf_from_rt —— map_vrf_to_rt 的反操作：把 VRF 从各 RT 的 vrfs 列表
 * 中删除；若某 RT 已无任何 VRF，则释放该 vrf_irt_node。 */
/*
 * Unmap specified VRF from specified RT. If there are no other
 * VRFs for this RT, then the RT hash is deleted.
 * bgp_vrf: BGP VRF specific instance
 */
static void unmap_vrf_from_rt(struct bgp *bgp_vrf,
			      struct vrf_route_target *l3rt)
{
	uint32_t i;

	for (i = 0; i < l3rt->ecom->size; i++) {
		struct vrf_irt_node *irt;
		struct ecommunity_val eval_tmp;

		/* Adjust masking for value */
		vrf_rt2ecom_val(&eval_tmp, l3rt, i);

		irt = lookup_vrf_import_rt(&eval_tmp);

		if (!irt)
			return; /* Not mapped */

		/* Delete VRF from list for this RT. */
		listnode_delete(irt->vrfs, bgp_vrf);

		if (!listnode_head(irt->vrfs))
			vrf_import_rt_free(irt);
	}
}

/* 【函数】map_vni_to_rt —— 把某 RT 扩展到指定 VNI：若 VNI 的 Import RT 不是用户
 * 配置的(is_import_rt_configured 为假)，则屏蔽 global-admin(自动 RT 语义)；查/建
 * irt_node 并把 VNI 加入其 vnis 列表(已映射则跳过)。 */
/*
 * Map one RT to specified VNI.
 */
static void map_vni_to_rt(struct bgp *bgp, struct bgpevpn *vpn,
			  struct ecommunity_val *eval)
{
	struct irt_node *irt;
	struct ecommunity_val eval_tmp;

	/* If using "automatic" RT, we only care about the local-admin
	 * sub-field.
	 * This is to facilitate using VNI as the RT for EBGP peering too.
	 */
	memcpy(&eval_tmp, eval, ECOMMUNITY_SIZE);
	if (!is_import_rt_configured(vpn))
		mask_ecom_global_admin(&eval_tmp, eval);

	irt = lookup_import_rt(bgp, &eval_tmp);
	if (irt)
		if (is_vni_present_in_irt_vnis(irt->vnis, vpn))
			/* Already mapped. */
			return;

	if (!irt)
		irt = import_rt_new(bgp, &eval_tmp);

	/* Add VNI to the hash list for this RT. */
	listnode_add(irt->vnis, vpn);
}

/* 【函数】unmap_vni_from_rt —— map_vni_to_rt 的反操作：把 VNI 从 irt_node 的 vnis
 * 列表删除；若已空则释放该 irt_node。 */
/*
 * Unmap specified VNI from specified RT. If there are no other
 * VNIs for this RT, then the RT hash is deleted.
 */
static void unmap_vni_from_rt(struct bgp *bgp, struct bgpevpn *vpn,
			      struct irt_node *irt)
{
	/* Delete VNI from hash list for this RT. */
	listnode_delete(irt->vnis, vpn);
	if (!listnode_head(irt->vnis)) {
		import_rt_free(bgp, irt);
	}
}

/* 【函数】bgp_evpn_get_rmac_nexthop —— 为 Type-2 路径的属性确定路由器 MAC(rmac)
 * 与下一跳：若为 SVI-IP 且开启 advertise-pip 且为 anycast MAC，则用系统(pip)RMAC
 * 与 PIP IP 作下一跳；否则用 VRF 的 rmac。非 Type-2 或无 VRF 时清零 rmac 返回。 */
static void bgp_evpn_get_rmac_nexthop(struct bgpevpn *vpn,
				      const struct prefix_evpn *p,
				      struct attr *attr, uint8_t flags)
{
	struct bgp *bgp_vrf = vpn->bgp_vrf;

	memset(&attr->rmac, 0, sizeof(struct ethaddr));
	if (!bgp_vrf)
		return;

	if (p->prefix.route_type != BGP_EVPN_MAC_IP_ROUTE)
		return;

	/* Copy sys (pip) RMAC and PIP IP as nexthop
	 * in case of route is self MAC-IP,
	 * advertise-pip and advertise-svi-ip features
	 * are enabled.
	 * Otherwise, for all host MAC-IP route's
	 * copy anycast RMAC.
	 */
	if (CHECK_FLAG(flags, BGP_EVPN_MACIP_TYPE_SVI_IP)
	    && bgp_vrf->evpn_info->advertise_pip &&
	    bgp_vrf->evpn_info->is_anycast_mac) {
		/* copy sys rmac */
		memcpy(&attr->rmac, &bgp_vrf->evpn_info->pip_rmac,
		       ETH_ALEN);
		attr->nexthop = bgp_vrf->evpn_info->pip_ip.ipaddr_v4;
		attr->mp_nexthop_global_in = bgp_vrf->evpn_info->pip_ip.ipaddr_v4;
	} else
		memcpy(&attr->rmac, &bgp_vrf->rmac, ETH_ALEN);
}

/* 【函数】form_auto_rt —— 自动派生 RT(形如 AS:VNI)：若开启 RFC8365 风格
 * (advertise_autort_rfc8365)则对 vni 置 EVPN_AUTORT_VXLAN 位；用 AS 低 16 位与 vni
 * 编码成一个 RT，按 is_l3 组装为 L3 包装或普通 ecommunity，并加入 rtl(避免重复)。
 * 只取 AS 低 16 位即可保证“同一 AS 内各路由器对同一 VNI 得到相同且跨 VNI 唯一的 RT”。 */
/*
 * Create RT extended community automatically from passed information:
 * of the form AS:VNI.
 * NOTE: We use only the lower 16 bits of the AS. This is sufficient as
 * the need is to get a RT value that will be unique across different
 * VNIs but the same across routers (in the same AS) for a particular
 * VNI.
 */
static void form_auto_rt(struct bgp *bgp, vni_t vni, struct list *rtl,
			 bool is_l3)
{
	struct ecommunity_val eval;
	struct ecommunity *ecomadd;
	struct ecommunity *ecom;
	struct vrf_route_target *l3rt;
	struct vrf_route_target *newrt;
	bool ecom_found = false;
	struct listnode *node;

	if (bgp->advertise_autort_rfc8365)
		SET_FLAG(vni, EVPN_AUTORT_VXLAN);
	encode_route_target_as((bgp->as & 0xFFFF), vni, &eval, true);

	ecomadd = ecommunity_new();
	ecommunity_add_val(ecomadd, &eval, false, false);

	if (is_l3) {
		for (ALL_LIST_ELEMENTS_RO(rtl, node, l3rt))
			if (ecommunity_cmp(ecomadd, l3rt->ecom)) {
				ecom_found = true;
				break;
			}
	} else {
		for (ALL_LIST_ELEMENTS_RO(rtl, node, ecom))
			if (ecommunity_cmp(ecomadd, ecom)) {
				ecom_found = true;
				break;
			}
	}

	if (!ecom_found) {
		if (is_l3) {
			newrt = evpn_vrf_rt_new(ecomadd);
			/* Label it as autoderived */
			SET_FLAG(newrt->flags, BGP_VRF_RT_AUTO);
			listnode_add_sort(rtl, newrt);
		} else
			listnode_add_sort(rtl, ecomadd);
	} else
		ecommunity_free(&ecomadd);
}

/* 【函数】derive_rd_rt_for_vni —— 为某 VNI 自动派生 RD 与 Import/Export RT，
 * 在 VNI 创建时调用(依次调用三个 derive 辅助)。 */
/*
 * Derive RD and RT for a VNI automatically. Invoked at the time of
 * creation of a VNI.
 */
static void derive_rd_rt_for_vni(struct bgp *bgp, struct bgpevpn *vpn)
{
	bgp_evpn_derive_auto_rd(bgp, vpn);
	bgp_evpn_derive_auto_rt_import(bgp, vpn);
	bgp_evpn_derive_auto_rt_export(bgp, vpn);
}

/* 【函数】evpn_convert_nexthop_to_ipv6 —— 把 IPv4 下一跳(远端 VTEP IP)转换为
 * IPv4-mapped IPv6 形式(填 mp_nexthop_global 并设 mp_nexthop_len)。已是 IPv6
 * 下一跳则直接返回。 */
/*
 * Convert nexthop (remote VTEP IP) into an IPv6 address.
 */
static void evpn_convert_nexthop_to_ipv6(struct attr *attr)
{
	if (BGP_ATTR_NEXTHOP_AFI_IP6(attr))
		return;
	ipv4_to_ipv4_mapped_ipv6(&attr->mp_nexthop_global, attr->nexthop);
	attr->mp_nexthop_len = IPV6_MAX_BYTELEN;
}

/* 【函数】bgp_evpn_global_node_get —— 在全局 EVPN 表中获取(dest)节点。
 * 全局表的前缀与 VNI 表不同：Type-1 需去掉 VTEP-IP(见 evpn_type1_prefix_global_copy)；
 * Type-2 需带上 MAC/IP(从 local_pi 借)以构成全局键。再调 bgp_afi_node_get 建节点。 */
/*
 * Wrapper for node get in global table.
 */
struct bgp_dest *bgp_evpn_global_node_get(struct bgp_table *table, afi_t afi,
					  safi_t safi,
					  const struct prefix_evpn *evp,
					  struct prefix_rd *prd,
					  const struct bgp_path_info *local_pi)
{
	struct prefix_evpn global_p = {};

	if (evp->prefix.route_type == BGP_EVPN_AD_ROUTE) {
		/* prefix in the global table doesn't include the VTEP-IP so
		 * we need to create a different copy of the prefix
		 */
		evpn_type1_prefix_global_copy(&global_p, evp);
		evp = &global_p;
	} else if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE &&
		   local_pi) {
		/*
		 * prefix in the global table needs MAC/IP, ensure they are
		 * present, using one's from local table's path_info.
		 */
		if (is_evpn_prefix_ipaddr_none(evp)) {
			/* VNI MAC -> Global */
			evpn_type2_prefix_global_copy(
				&global_p, evp, NULL /* mac */,
				evpn_type2_path_info_get_ip(local_pi));
		} else {
			/* VNI IP -> Global */
			evpn_type2_prefix_global_copy(
				&global_p, evp,
				evpn_type2_path_info_get_mac(local_pi),
				NULL /* ip */);
		}

		evp = &global_p;
	}
	return bgp_afi_node_get(table, afi, safi, (struct prefix *)evp, prd);
}

/* 【函数】bgp_evpn_global_node_lookup —— 与 bgp_evpn_global_node_get 同逻辑，但仅
 * 查找(不创建)，前缀归一同上。 */
/*
 * Wrapper for node lookup in global table.
 */
struct bgp_dest *bgp_evpn_global_node_lookup(
	struct bgp_table *table, safi_t safi, const struct prefix_evpn *evp,
	struct prefix_rd *prd, const struct bgp_path_info *local_pi)
{
	struct prefix_evpn global_p = {};

	if (evp->prefix.route_type == BGP_EVPN_AD_ROUTE) {
		/* prefix in the global table doesn't include the VTEP-IP so
		 * we need to create a different copy of the prefix
		 */
		evpn_type1_prefix_global_copy(&global_p, evp);
		evp = &global_p;
	} else if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE &&
		   local_pi) {
		/*
		 * prefix in the global table needs MAC/IP, ensure they are
		 * present, using one's from local table's path_info.
		 */
		if (is_evpn_prefix_ipaddr_none(evp)) {
			/* VNI MAC -> Global */
			evpn_type2_prefix_global_copy(
				&global_p, evp, NULL /* mac */,
				evpn_type2_path_info_get_ip(local_pi));
		} else {
			/* VNI IP -> Global */
			evpn_type2_prefix_global_copy(
				&global_p, evp,
				evpn_type2_path_info_get_mac(local_pi),
				NULL /* ip */);
		}

		evp = &global_p;
	}
	return bgp_safi_node_lookup(table, safi, (struct prefix *)evp, prd);
}

/* 【函数】bgp_evpn_vni_ip_node_get —— 在某 VNI 的 IP 表中获取节点。
 * IP 表前缀与全局表不同：Type-1 需补回 VTEP-IP(从 parent_pi->attr 取)；
 * Type-2 必须是带 IP 的(仅 MAC 的断言失败)，并去掉 MAC 只留 IP。 */
/*
 * Wrapper for node get in VNI IP table.
 */
struct bgp_dest *bgp_evpn_vni_ip_node_get(struct bgp_table *const table,
					  const struct prefix_evpn *evp,
					  const struct bgp_path_info *parent_pi)
{
	struct prefix_evpn vni_p = {};

	if (evp->prefix.route_type == BGP_EVPN_AD_ROUTE && parent_pi) {
		/* prefix in the global table doesn't include the VTEP-IP so
		 * we need to create a different copy for the VNI
		 */
		evpn_type1_prefix_vni_ip_copy(&vni_p, evp, parent_pi->attr);
		evp = &vni_p;
	} else if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) {
		/* Only MAC-IP should go into this table, not mac-only */
		assert(is_evpn_prefix_ipaddr_none(evp) == false);

		/*
		 * prefix in the vni IP table doesn't include MAC so
		 * we need to create a different copy of the prefix.
		 */
		evpn_type2_prefix_vni_ip_copy(&vni_p, evp);
		evp = &vni_p;
	}
	return bgp_node_get(table, (struct prefix *)evp);
}

/* 【函数】bgp_evpn_vni_ip_node_lookup —— 与 bgp_evpn_vni_ip_node_get 同逻辑，但仅
 * 查找(不创建)。 */
/*
 * Wrapper for node lookup in VNI IP table.
 */
struct bgp_dest *
bgp_evpn_vni_ip_node_lookup(const struct bgp_table *const table,
			    const struct prefix_evpn *evp,
			    const struct bgp_path_info *parent_pi)
{
	struct prefix_evpn vni_p = {};

	if (evp->prefix.route_type == BGP_EVPN_AD_ROUTE && parent_pi) {
		/* prefix in the global table doesn't include the VTEP-IP so
		 * we need to create a different copy for the VNI
		 */
		evpn_type1_prefix_vni_ip_copy(&vni_p, evp, parent_pi->attr);
		evp = &vni_p;
	} else if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) {
		/* Only MAC-IP should go into this table, not mac-only */
		assert(is_evpn_prefix_ipaddr_none(evp) == false);

		/*
		 * prefix in the vni IP table doesn't include MAC so
		 * we need to create a different copy of the prefix.
		 */
		evpn_type2_prefix_vni_ip_copy(&vni_p, evp);
		evp = &vni_p;
	}
	return bgp_node_lookup(table, (struct prefix *)evp);
}

/* 【函数】bgp_evpn_vni_mac_node_get —— 在某 VNI 的 MAC 表中获取节点。表中只允许
 * Type-2(断言)，且键需去掉 IP(仅保留 MAC，见 evpn_type2_prefix_vni_mac_copy)。 */
/*
 * Wrapper for node get in VNI MAC table.
 */
struct bgp_dest *
bgp_evpn_vni_mac_node_get(struct bgp_table *const table,
			  const struct prefix_evpn *evp,
			  const struct bgp_path_info *parent_pi)
{
	struct prefix_evpn vni_p = {};

	/* Only type-2 should ever go into this table */
	assert(evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE);

	/*
	 * prefix in the vni MAC table doesn't include IP so
	 * we need to create a different copy of the prefix.
	 */
	evpn_type2_prefix_vni_mac_copy(&vni_p, evp);
	evp = &vni_p;
	return bgp_node_get(table, (struct prefix *)evp);
}

/* 【函数】bgp_evpn_vni_mac_node_lookup —— 与 bgp_evpn_vni_mac_node_get 同逻辑，仅查找。 */
/*
 * Wrapper for node lookup in VNI MAC table.
 */
struct bgp_dest *
bgp_evpn_vni_mac_node_lookup(const struct bgp_table *const table,
			     const struct prefix_evpn *evp,
			     const struct bgp_path_info *parent_pi)
{
	struct prefix_evpn vni_p = {};

	/* Only type-2 should ever go into this table */
	assert(evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE);

	/*
	 * prefix in the vni MAC table doesn't include IP so
	 * we need to create a different copy of the prefix.
	 */
	evpn_type2_prefix_vni_mac_copy(&vni_p, evp);
	evp = &vni_p;
	return bgp_node_lookup(table, (struct prefix *)evp);
}

/* 【函数】bgp_evpn_vni_node_get —— 按前缀类型在 VNI 的两张表间分派：仅 MAC 的
 * Type-2 走 mac_table，其余(带 IP 的 Type-2、Type-5)走 ip_table。 */
/*
 * Wrapper for node get in both VNI tables.
 */
struct bgp_dest *bgp_evpn_vni_node_get(struct bgpevpn *vpn,
				       const struct prefix_evpn *p,
				       const struct bgp_path_info *parent_pi)
{
	if ((p->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) &&
	    (is_evpn_prefix_ipaddr_none(p) == true))
		return bgp_evpn_vni_mac_node_get(vpn->mac_table, p, parent_pi);

	return bgp_evpn_vni_ip_node_get(vpn->ip_table, p, parent_pi);
}

/* 【函数】bgp_evpn_vni_node_lookup —— 与 bgp_evpn_vni_node_get 同分派逻辑，仅查找。 */
/*
 * Wrapper for node lookup in both VNI tables.
 */
struct bgp_dest *bgp_evpn_vni_node_lookup(const struct bgpevpn *vpn,
					  const struct prefix_evpn *p,
					  const struct bgp_path_info *parent_pi)
{
	if ((p->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) &&
	    (is_evpn_prefix_ipaddr_none(p) == true))
		return bgp_evpn_vni_mac_node_lookup(vpn->mac_table, p,
						    parent_pi);

	return bgp_evpn_vni_ip_node_lookup(vpn->ip_table, p, parent_pi);
}

/* 【函数】bgp_zebra_send_remote_macip —— 向 zebra 发送“新增/删除远端 MAC-IP”消息
 * (add 决定 ZEBRA_REMOTE_MACIP_ADD/DEL)。报文含 VNI、MAC、IP(或其长度=0)、远端
 * VTEP IP(若 ESI 有效则改为发全 0 的 zero_remote_vtep_ip)，add 时再附 flags/seq/ESI。
 * 前置条件：zclient 存在且 bgp 实例对 zebra 已知，否则直接返回 SUCCESS(视为无需发)。
 * 返回 zclient_send_status。 */
/*
 * Add (update) or delete MACIP from zebra.
 */
static enum zclient_send_status bgp_zebra_send_remote_macip(
	struct bgp *bgp, struct bgpevpn *vpn, const struct prefix_evpn *p,
	const struct ethaddr *mac, struct ipaddr *remote_vtep_ip, int add,
	uint8_t flags, uint32_t seq, esi_t *esi)
{
	struct stream *s;
	uint16_t ipa_len;
	static struct ipaddr zero_remote_vtep_ip = { .ipa_type = IPADDR_V4, .ipaddr_v4 = { INADDR_ANY } };
	bool esi_valid;

	/* Check socket. */
	if (!bgp_zclient || bgp_zclient->sock < 0) {
		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug("%s: No zclient or zclient->sock exists",
				   __func__);
		return ZCLIENT_SEND_SUCCESS;
	}

	/* Don't try to register if Zebra doesn't know of this instance. */
	if (!IS_BGP_INST_KNOWN_TO_ZEBRA(bgp)) {
		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug(
				"%s: No zebra instance to talk to, not installing remote macip",
				__func__);
		return ZCLIENT_SEND_SUCCESS;
	}

	if (!esi)
		esi = zero_esi;
	s = bgp_zclient->obuf;
	stream_reset(s);

	zclient_create_header(
		s, add ? ZEBRA_REMOTE_MACIP_ADD : ZEBRA_REMOTE_MACIP_DEL,
		bgp->vrf_id);
	stream_putl(s, vpn ? vpn->vni : 0);

	if (mac) /* Mac Addr */
		stream_put(s, &mac->octet, ETH_ALEN);
	else
		stream_put(s, &p->prefix.macip_addr.mac.octet, ETH_ALEN);

	/* IP address length and IP address, if any. */
	if (is_evpn_prefix_ipaddr_none(p))
		stream_putw(s, 0);
	else {
		ipa_len = is_evpn_prefix_ipaddr_v4(p) ? IPV4_MAX_BYTELEN
						      : IPV6_MAX_BYTELEN;
		stream_putw(s, ipa_len);
		stream_put(s, &p->prefix.macip_addr.ip.ip.addr, ipa_len);
	}
	/* If the ESI is valid that becomes the nexthop; tape out the
	 * VTEP-IP for that case
	 */
	if (bgp_evpn_is_esi_valid(esi)) {
		esi_valid = true;
		stream_put_ipaddr(s, &zero_remote_vtep_ip);
	} else {
		esi_valid = false;
		stream_put_ipaddr(s, remote_vtep_ip);
	}

	/* TX flags - MAC sticky status and/or gateway mac */
	/* Also TX the sequence number of the best route. */
	if (add) {
		stream_putc(s, flags);
		stream_putl(s, seq);
		stream_put(s, esi, sizeof(esi_t));
	}

	stream_putw_at(s, 0, stream_get_endp(s));

	if (bgp_debug_zebra(NULL)) {
		char esi_buf[ESI_STR_LEN];

		if (esi_valid)
			esi_to_str(esi, esi_buf, sizeof(esi_buf));
		else
			snprintf(esi_buf, sizeof(esi_buf), "-");
		zlog_debug(
			"Tx %s MACIP, VNI %u MAC %pEA IP %pIA flags 0x%x seq %u remote VTEP %pIA esi %s",
			add ? "ADD" : "DEL", (vpn ? vpn->vni : 0),
			(mac ? mac : &p->prefix.macip_addr.mac),
			&p->prefix.macip_addr.ip, flags, seq, remote_vtep_ip,
			esi_buf);
	}

	frrtrace(5, frr_bgp, evpn_mac_ip_zsend, add, vpn, p, remote_vtep_ip,
		 esi);

	return zclient_send_message(bgp_zclient);
}

/* 【函数】bgp_zebra_send_remote_vtep —— 向 zebra 发送“新增/删除远端 VTEP”消息
 * (add 决定 ADD/DEL)，对应 Type-3(IMET)学到的远端 VTEP。报文含 VNI、VTEP IP
 * (取 imet_addr.ip)与 flood_control。前置条件同上一个函数。 */
/*
 * Add (update) or delete remote VTEP from zebra.
 */
static enum zclient_send_status
bgp_zebra_send_remote_vtep(struct bgp *bgp, struct bgpevpn *vpn,
			   const struct prefix_evpn *p, int flood_control,
			   int add)
{
	struct stream *s;

	/* Check socket. */
	if (!bgp_zclient || bgp_zclient->sock < 0) {
		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug("%s: No zclient or zclient->sock exists",
				   __func__);
		return ZCLIENT_SEND_SUCCESS;
	}

	/* Don't try to register if Zebra doesn't know of this instance. */
	if (!IS_BGP_INST_KNOWN_TO_ZEBRA(bgp)) {
		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug(
				"%s: No zebra instance to talk to, not installing remote vtep",
				__func__);
		return ZCLIENT_SEND_SUCCESS;
	}

	s = bgp_zclient->obuf;
	stream_reset(s);

	zclient_create_header(
		s, add ? ZEBRA_REMOTE_VTEP_ADD : ZEBRA_REMOTE_VTEP_DEL,
		bgp->vrf_id);
	stream_putl(s, vpn ? vpn->vni : 0);
	stream_put_ipaddr(s, &p->prefix.imet_addr.ip);
	stream_putl(s, flood_control);

	stream_putw_at(s, 0, stream_get_endp(s));

	if (bgp_debug_zebra(NULL))
		zlog_debug("Tx %s Remote VTEP, VNI %u (flood control %d) remote VTEP %pIA",
			   add ? "ADD" : "DEL", (vpn ? vpn->vni : 0), flood_control,
			   &p->prefix.imet_addr.ip);

	frrtrace(3, frr_bgp, evpn_bum_vtep_zsend, add, vpn, p);

	return zclient_send_message(bgp_zclient);
}

/* 【函数】build_evpn_type5_route_extcomm —— 为 Type-5(IP Prefix)路由组装扩展团体：
 * 先加 Encap(VXLAN)，再并入 L3VNI/VRF 的 Export RT 链，最后若非零 MAC 则加
 * Router MAC(rmac)扩展团体。注意 ecommunity_merge 的返回值都回填到 attr。 */
/*
 * Build extended communities for EVPN prefix route.
 */
static void build_evpn_type5_route_extcomm(struct bgp *bgp_vrf,
					   struct attr *attr)
{
	struct ecommunity ecom_encap;
	struct ecommunity_val eval;
	struct ecommunity_val eval_rmac;
	bgp_encap_types tnl_type;
	struct listnode *node, *nnode;
	struct vrf_route_target *l3rt;
	struct ecommunity *old_ecom;
	struct ecommunity *ecom;
	struct list *vrf_export_rtl = NULL;

	/* Encap */
	tnl_type = BGP_ENCAP_TYPE_VXLAN;
	memset(&ecom_encap, 0, sizeof(ecom_encap));
	encode_encap_extcomm(tnl_type, &eval);
	ecom_encap.size = 1;
	ecom_encap.unit_size = ECOMMUNITY_SIZE;
	ecom_encap.val = (uint8_t *)eval.val;

	/* Add Encap */
	if (bgp_attr_get_ecommunity(attr)) {
		old_ecom = bgp_attr_get_ecommunity(attr);
		ecom = ecommunity_merge(ecommunity_dup(old_ecom), &ecom_encap);
		if (!old_ecom->refcnt)
			ecommunity_free(&old_ecom);
	} else
		ecom = ecommunity_dup(&ecom_encap);
	bgp_attr_set_ecommunity(attr, ecom);
	attr->encap_tunneltype = tnl_type;

	/* Add the export RTs for L3VNI/VRF */
	vrf_export_rtl = bgp_vrf->vrf_export_rtl;
	for (ALL_LIST_ELEMENTS(vrf_export_rtl, node, nnode, l3rt))
		bgp_attr_set_ecommunity(
			attr, ecommunity_merge(bgp_attr_get_ecommunity(attr),
					       l3rt->ecom));

	/* add the router mac extended community */
	if (!is_zero_mac(&attr->rmac)) {
		encode_rmac_extcomm(&eval_rmac, &attr->rmac);
		ecommunity_add_val(bgp_attr_get_ecommunity(attr), &eval_rmac,
				   true, true);
	}
}

/* 【函数】build_evpn_route_extcomm —— 为 Type-2/Type-3 路由组装扩展团体：
 * 所有路由都加 Encap + L2VNI 的 Export RT；add_l3_ecomm 为真时再加 L3VNI 的
 * Export RT 与 Router MAC；另外按 attr->evpn_flags 视情况加 MAC Mobility(sticky)、
 * 默认网关扩展团体，以及按 ROUTER 标志或代理标志加 ND 标志扩展团体。
 * add_l3_ecomm 由调用方决定(如仅对非 link-local 的 Type-2 加三层团体)。 */
/*
 * Build extended communities for EVPN route.
 * This function is applicable for type-2 and type-3 routes. The layer-2 RT
 * and ENCAP extended communities are applicable for all routes.
 * The default gateway extended community and MAC mobility (sticky) extended
 * community are added as needed based on passed settings - only for type-2
 * routes. Likewise, the layer-3 RT and Router MAC extended communities are
 * added, if present, based on passed settings - only for non-link-local
 * type-2 routes.
 */
static void build_evpn_route_extcomm(struct bgpevpn *vpn, struct attr *attr,
				     int add_l3_ecomm,
				     struct ecommunity *macvrf_soo)
{
	struct ecommunity ecom_encap;
	struct ecommunity ecom_sticky;
	struct ecommunity ecom_default_gw;
	struct ecommunity ecom_na;
	struct ecommunity_val eval;
	struct ecommunity_val eval_sticky;
	struct ecommunity_val eval_default_gw;
	struct ecommunity_val eval_rmac;
	struct ecommunity_val eval_na;
	bool proxy;

	bgp_encap_types tnl_type;
	struct listnode *node, *nnode;
	struct ecommunity *ecom;
	struct vrf_route_target *l3rt;
	uint32_t seqnum;
	struct list *vrf_export_rtl = NULL;

	/* Encap */
	tnl_type = BGP_ENCAP_TYPE_VXLAN;
	memset(&ecom_encap, 0, sizeof(ecom_encap));
	encode_encap_extcomm(tnl_type, &eval);
	ecom_encap.size = 1;
	ecom_encap.unit_size = ECOMMUNITY_SIZE;
	ecom_encap.val = (uint8_t *)eval.val;

	/* Add Encap */
	bgp_attr_set_ecommunity(attr, ecommunity_dup(&ecom_encap));
	attr->encap_tunneltype = tnl_type;

	/* Add the export RTs for L2VNI */
	for (ALL_LIST_ELEMENTS(vpn->export_rtl, node, nnode, ecom))
		bgp_attr_set_ecommunity(
			attr,
			ecommunity_merge(bgp_attr_get_ecommunity(attr), ecom));

	/* Add the export RTs for L3VNI if told to - caller determines
	 * when this should be done.
	 */
	if (add_l3_ecomm) {
		vrf_export_rtl = bgpevpn_get_vrf_export_rtl(vpn);
		if (vrf_export_rtl && !list_isempty(vrf_export_rtl)) {
			for (ALL_LIST_ELEMENTS(vrf_export_rtl, node, nnode,
					       l3rt))
				bgp_attr_set_ecommunity(
					attr,
					ecommunity_merge(
						bgp_attr_get_ecommunity(attr),
						l3rt->ecom));
		}
	}

	/* Add MAC mobility (sticky) if needed. */
	if (CHECK_FLAG(attr->evpn_flags, ATTR_EVPN_FLAG_STICKY)) {
		seqnum = 0;
		encode_mac_mobility_extcomm(1, seqnum, &eval_sticky);
		ecom_sticky.size = 1;
		ecom_sticky.unit_size = ECOMMUNITY_SIZE;
		ecom_sticky.val = (uint8_t *)eval_sticky.val;
		bgp_attr_set_ecommunity(
			attr, ecommunity_merge(bgp_attr_get_ecommunity(attr),
					       &ecom_sticky));
	}

	/* Add RMAC, if told to. */
	if (add_l3_ecomm) {
		encode_rmac_extcomm(&eval_rmac, &attr->rmac);
		ecommunity_add_val(bgp_attr_get_ecommunity(attr), &eval_rmac,
				   true, true);
	}

	/* Add default gateway, if needed. */
	if (CHECK_FLAG(attr->evpn_flags, ATTR_EVPN_FLAG_DEFAULT_GW)) {
		encode_default_gw_extcomm(&eval_default_gw);
		ecom_default_gw.size = 1;
		ecom_default_gw.unit_size = ECOMMUNITY_SIZE;
		ecom_default_gw.val = (uint8_t *)eval_default_gw.val;
		bgp_attr_set_ecommunity(
			attr, ecommunity_merge(bgp_attr_get_ecommunity(attr),
					       &ecom_default_gw));
	}

	proxy = !!(attr->es_flags & ATTR_ES_PROXY_ADVERT);
	if (CHECK_FLAG(attr->evpn_flags, ATTR_EVPN_FLAG_ROUTER) || proxy) {
		encode_na_flag_extcomm(&eval_na,
				       CHECK_FLAG(attr->evpn_flags,
						  ATTR_EVPN_FLAG_ROUTER),
				       proxy);
		ecom_na.size = 1;
		ecom_na.unit_size = ECOMMUNITY_SIZE;
		ecom_na.val = (uint8_t *)eval_na.val;
		bgp_attr_set_ecommunity(
			attr, ecommunity_merge(bgp_attr_get_ecommunity(attr),
					       &ecom_na));
	}

	/* Add MAC-VRF SoO, if configured */
	if (macvrf_soo)
		bgp_attr_set_ecommunity(
			attr, ecommunity_merge(attr->ecommunity, macvrf_soo));
}

/*
 * add_mac_mobility_to_attr -- 向路由属性中写入 MAC 移动性扩展团体(MM, MAC Mobility)。
 *
 * 职责：把序号 seq_num 编码为 MM 扩展团体并挂到 attr 上；若 attr 上已存在 MM 扩展
 *       团体则就地覆盖(而非追加)，保证同一属性里 MM 只有一份。
 * 参数：seq_num  MAC 移动序号(越大表示越新，用于 MAC 迁移时的择优)。
 *       attr     目标路由属性，函数通过 bgp_attr_set_ecommunity 更新其 ecommunity。
 * 返回：无(直接修改 attr)。
 * 关键分支：遍历 ecomm 找 type == ECOMMUNITY_ENCODE_EVPN 且 sub_type == MACMOBILITY
 *       的成员；命中则 memcpy 覆盖，未命中则构造单元素临时 ecommunity 后 merge 或 dup。
 * 易错点：覆盖分支用 ecomm->unit_size 作为拷贝长度(而非固定 sizeof(struct ecommunity_val))，
 *       依赖 unit_size 恒等于 ECOMMUNITY_SIZE，阅读时留意二者语义一致性。
 * 调用时机：本地或远端 MAC-IP 路由宣告前，为携带 MM 扩展团体而调用。
 */
/*
 * Add MAC mobility extended community to attribute.
 */
static void add_mac_mobility_to_attr(uint32_t seq_num, struct attr *attr)
{
	struct ecommunity ecom_tmp;
	struct ecommunity_val eval;
	uint8_t *ecom_val_ptr;
	uint32_t i;
	uint8_t *pnt;
	int type = 0;
	int sub_type = 0;
	struct ecommunity *ecomm = bgp_attr_get_ecommunity(attr);

	/* Build MM */
	encode_mac_mobility_extcomm(0, seq_num, &eval);

	/* Find current MM ecommunity */
	ecom_val_ptr = NULL;

	if (ecomm) {
		for (i = 0; i < ecomm->size; i++) {
			pnt = ecomm->val + (i * ecomm->unit_size);
			type = *pnt++;
			sub_type = *pnt++;

			if (type == ECOMMUNITY_ENCODE_EVPN
			    && sub_type
				       == ECOMMUNITY_EVPN_SUBTYPE_MACMOBILITY) {
				ecom_val_ptr =
					(ecomm->val + (i * ecomm->unit_size));
				break;
			}
		}
	}

	/* Update the existing MM ecommunity */
	if (ecom_val_ptr) {
		memcpy(ecom_val_ptr, eval.val, sizeof(char) * ecomm->unit_size);
	}
	/* Add MM to existing */
	else {
		memset(&ecom_tmp, 0, sizeof(ecom_tmp));
		ecom_tmp.size = 1;
		ecom_tmp.unit_size = ECOMMUNITY_SIZE;
		ecom_tmp.val = (uint8_t *)eval.val;

		if (ecomm)
			bgp_attr_set_ecommunity(
				attr, ecommunity_merge(ecomm, &ecom_tmp));
		else
			bgp_attr_set_ecommunity(attr,
						ecommunity_dup(&ecom_tmp));
	}
}

/*
 * evpn_zebra_install -- 把一条 EVPN 路由的最佳路径下发到 zebra。
 *
 * 职责：按路由类型分派到不同的 zebra 下发接口：
 *       - Type-2 (MAC/IP)：组装 ZEBRA_MACIP_TYPE_xxx flags 与 MM 序号，调用
 *         bgp_zebra_send_remote_macip，把 MAC 或 MAC-IP 表项送内核(编程 VXLAN FDB / 邻接)。
 *       - Type-1 (EAD)：调用 bgp_evpn_remote_es_evi_add，交由 MH 模块处理。
 *       - Type-3 (IMET) 等其余类型：按 PIM / 承载隧道的 pmsi_tnl_type 计算 flood_control，
 *         调用 bgp_zebra_send_remote_vtep 通告远端 VTEP(BUM 泛洪控制)。
 * 参数：bgp  实例；vpn  该路由所属 L2 VNI(bgpevpn)；p  路由前缀(prefix_evpn)；
 *       pi   被选中的路径信息。
 * 返回：zebra 发送状态(zclient_send_status)。
 * 关键分支：
 *   - pi->sub_type == BGP_ROUTE_IMPORTED(远端 / 导入路由)：按属性里的 STICKY / DEFAULT_GW /
 *     ROUTER 标志置相应 flags；若为本地 ES 的同步路径再叠加 SYNC_PATH 与 PROXY_ADVERT。
 *   - 否则(本地生成路由)：只有带 SYNC 属性的才下发，sync 场景还要取对端最高 MM 序号、
 *     并可能按对端 ROUTER / ACTIVE 标志补 ROUTER 或 PROXY_ADVERT。
 * 易错点：MAC 单播更新(前缀内 IP 为 none)时 MAC 参数传 NULL，只有 MAC-IP 更新才传 MAC；
 *       nhfamily 由属性的 mp_nexthop_len 推导，缺省 vtep_ip 为全零。
 * 调用时机：EVPN 最佳路径变化后由 evpn_route_select_install 等回调触发。
 * 依据：flags 语义来自 zebra.h 的 ZEBRA_MACIP_TYPE_* 定义。
 */
/* Install EVPN route into zebra. */
enum zclient_send_status evpn_zebra_install(struct bgp *bgp, struct bgpevpn *vpn,
					    const struct prefix_evpn *p,
					    struct bgp_path_info *pi)
{
	uint8_t flags;
	int flood_control = VXLAN_FLOOD_DISABLED;
	uint32_t seq;
	enum zclient_send_status ret = ZCLIENT_SEND_SUCCESS;
	struct ipaddr vtep_ip = { .ipa_type = IPADDR_V4, .ipaddr_v4 = { INADDR_ANY } };

	if (p->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) {
		flags = 0;

		if (pi->sub_type == BGP_ROUTE_IMPORTED) {
			if (CHECK_FLAG(pi->attr->evpn_flags,
				       ATTR_EVPN_FLAG_STICKY))
				SET_FLAG(flags, ZEBRA_MACIP_TYPE_STICKY);
			if (CHECK_FLAG(pi->attr->evpn_flags,
				       ATTR_EVPN_FLAG_DEFAULT_GW))
				SET_FLAG(flags, ZEBRA_MACIP_TYPE_GW);
			if (is_evpn_prefix_ipaddr_v6(p) &&
			    CHECK_FLAG(pi->attr->evpn_flags,
				       ATTR_EVPN_FLAG_ROUTER))
				SET_FLAG(flags, ZEBRA_MACIP_TYPE_ROUTER_FLAG);

			seq = mac_mobility_seqnum(pi->attr);
			/* If local ES notify zebra that this is a sync path.
			 * ES is non-local while in LACP Bypass.
			 */
			if (bgp_evpn_attr_is_local_es(pi->attr)) {
				SET_FLAG(flags, ZEBRA_MACIP_TYPE_SYNC_PATH);
				if (bgp_evpn_attr_is_proxy(pi->attr))
					SET_FLAG(flags,
						ZEBRA_MACIP_TYPE_PROXY_ADVERT);
			}
		} else {
			if (!bgp_evpn_attr_is_sync(pi->attr))
				return 0;

			/* If local ES notify zebra that this is a sync path.
			 * ES is non-local while in LACP Bypass.
			 */
			if (bgp_evpn_attr_is_local_es(pi->attr)) {
				/* if a local path is being turned around and
				 * sent to zebra it is because it is a sync path
				 * on a local ES
				 */
				SET_FLAG(flags, ZEBRA_MACIP_TYPE_SYNC_PATH);
				/* supply the highest peer seq number to zebra
				 * for MM seq syncing
				 */
				seq = bgp_evpn_attr_get_sync_seq(pi->attr);
				/* if any of the paths from the peer have the
				 * ROUTER flag set install the local entry as a
				 * router entry
				 */
				if (is_evpn_prefix_ipaddr_v6(p) &&
				    CHECK_FLAG(pi->attr->es_flags, ATTR_ES_PEER_ROUTER))
					SET_FLAG(flags, ZEBRA_MACIP_TYPE_ROUTER_FLAG);

				if (!CHECK_FLAG(pi->attr->es_flags, ATTR_ES_PEER_ACTIVE))
					SET_FLAG(flags, ZEBRA_MACIP_TYPE_PROXY_ADVERT);
			} else
				seq = mac_mobility_seqnum(pi->attr);
		}

		uint8_t nhfamily = NEXTHOP_FAMILY(pi->attr->mp_nexthop_len);

		switch (nhfamily) {
		case AF_INET:
			SET_IPADDR_V4(&vtep_ip);
			vtep_ip.ipaddr_v4 = pi->attr->mp_nexthop_global_in;
			break;
		case AF_INET6:
			SET_IPADDR_V6(&vtep_ip);
			IPV6_ADDR_COPY(&vtep_ip.ipaddr_v6, &pi->attr->mp_nexthop_global);
			break;
		}

		ret = bgp_zebra_send_remote_macip(
			bgp, vpn, p,
			(is_evpn_prefix_ipaddr_none(p)
				 ? NULL /* MAC update */
				 : evpn_type2_path_info_get_mac(
					   pi) /* MAC-IP update */),
			&vtep_ip, 1, flags, seq,
			bgp_evpn_attr_get_esi(pi->attr));
	} else if (p->prefix.route_type == BGP_EVPN_AD_ROUTE) {
		ret = bgp_evpn_remote_es_evi_add(bgp, vpn, p, pi);
	} else {
		switch (bgp_attr_get_pmsi_tnl_type(pi->attr)) {
		case PMSI_TNLTYPE_INGR_REPL:
			flood_control = VXLAN_FLOOD_HEAD_END_REPL;
			break;

		case PMSI_TNLTYPE_PIM_SM:
			flood_control = VXLAN_FLOOD_PIM_SM;
			break;

		case PMSI_TNLTYPE_NO_INFO:
		case PMSI_TNLTYPE_RSVP_TE_P2MP:
		case PMSI_TNLTYPE_MLDP_P2MP:
		case PMSI_TNLTYPE_PIM_SSM:
		case PMSI_TNLTYPE_PIM_BIDIR:
		case PMSI_TNLTYPE_MLDP_MP2MP:
			flood_control = VXLAN_FLOOD_DISABLED;
			break;
		}

		ret = bgp_zebra_send_remote_vtep(bgp, vpn, p, flood_control, 1);
	}

	return ret;
}

/*
 * evpn_zebra_uninstall -- 把一条 EVPN 路由从 zebra 撤销。
 *
 * 职责：与 evpn_zebra_install 相反。按类型分派：
 *       - Type-2 (MAC/IP)：调用 bgp_zebra_send_remote_macip 且 installed=0 撤销；
 *         若 is_sync 为真则 VTEP 用零地址(zero_vtep_ip)，表示这是同步路径的撤销。
 *       - Type-1 (EAD)：调用 bgp_evpn_remote_es_evi_del。
 *       - 其余类型：bgp_zebra_send_remote_vtep(installed=0)。
 * 参数：bgp / vpn / p / pi 同上；is_sync 表示是否为 ES 同步路径的撤销(决定 VTEP 地址)。
 * 返回：zebra 发送状态。
 * 易错点：vtep_ip 仅在 Type-2 分支内被赋值，也只在 Type-2 分支被使用，逻辑自洽；
 *       非 Type-2 分支不读 vtep_ip。撤销时以 NULL 作为扩展团体参数。
 * 调用时机：最佳路径被替换 / 删除、路由撤销时。
 */
/* Uninstall EVPN route from zebra. */
enum zclient_send_status evpn_zebra_uninstall(struct bgp *bgp,
					      struct bgpevpn *vpn,
					      const struct prefix_evpn *p,
					      struct bgp_path_info *pi,
					      bool is_sync)
{
	enum zclient_send_status ret = ZCLIENT_SEND_SUCCESS;
	struct ipaddr vtep_ip;

	if (p->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) {
		uint8_t nhfamily = NEXTHOP_FAMILY(pi->attr->mp_nexthop_len);

		switch (nhfamily) {
		case AF_INET:
			SET_IPADDR_V4(&vtep_ip);
			vtep_ip.ipaddr_v4 = pi->attr->mp_nexthop_global_in;
			break;
		case AF_INET6:
			SET_IPADDR_V6(&vtep_ip);
			IPV6_ADDR_COPY(&vtep_ip.ipaddr_v6, &pi->attr->mp_nexthop_global);
			break;
		}
	}

	if (p->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE)
		ret = bgp_zebra_send_remote_macip(
			bgp, vpn, p,
			(is_evpn_prefix_ipaddr_none(p)
				 ? NULL /* MAC update */
				 : evpn_type2_path_info_get_mac(
					   pi) /* MAC-IP update */),
			(is_sync ? &zero_vtep_ip : &vtep_ip), 0, 0, 0,
			NULL);
	else if (p->prefix.route_type == BGP_EVPN_AD_ROUTE)
		ret = bgp_evpn_remote_es_evi_del(bgp, vpn, p, pi);
	else
		ret = bgp_zebra_send_remote_vtep(bgp, vpn, p,
						 VXLAN_FLOOD_DISABLED, 0);

	return ret;
}

/*
 * evpn_delete_old_local_route -- 因 MAC 移动性，旧的"本地"最佳路由需要清除。
 *
 * 背景：某 MAC-IP 的本地路由被一条更优的"远端"路由顶替时(典型为 MAC 迁移到别的 PE)，
 *       旧的本地路由必须从本地 VNI 表和全局 EVPN 表中删除，并向对端撤销。
 * 职责：1) 在全局 EVPN 表(两级树 RD 级 / 前缀级，类似 L3VPN 路由)中定位对应节点；
 *       2) 用 delete_evpn_route_entry 删除该表项，并用 bgp_process 调度，由全局表向对端
 *          发出 withdraw；
 *       3) 在 VNI 表里把 old_local 标记为待删除(bgp_path_info_mark_for_delete)，
 *          具体摘除由调用者完成。
 * 参数：bgp / vpn / dest(VNI 表节点) / old_local(旧本地路径) / new_select(顶替它的新路径，可空)。
 * 返回：无。
 * 易错点：本函数只做"标记删除"，VNI 表项的最终摘除交给调用者，避免在遍历中改动链表。
 * 调用时机：evpn_route_select_install 中发现新最佳路径来自远端时。
 */
/*
 * Due to MAC mobility, the prior "local" best route has been supplanted
 * by a "remote" best route. The prior route has to be deleted and withdrawn
 * from peers.
 */
static void evpn_delete_old_local_route(struct bgp *bgp, struct bgpevpn *vpn,
					struct bgp_dest *dest,
					struct bgp_path_info *old_local,
					struct bgp_path_info *new_select)
{
	struct bgp_dest *global_dest;
	struct bgp_path_info *pi;
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;

	if (BGP_DEBUG(evpn_mh, EVPN_MH_RT)) {
		char esi_buf[ESI_STR_LEN];
		char esi_buf2[ESI_STR_LEN];
		struct prefix_evpn *evp =
			(struct prefix_evpn *)bgp_dest_get_prefix(dest);

		zlog_debug("local path deleted %pFX es %s; new-path-es %s", evp,
			   esi_to_str(&old_local->attr->esi, esi_buf,
				      sizeof(esi_buf)),
			   new_select ? esi_to_str(&new_select->attr->esi,
						   esi_buf2, sizeof(esi_buf2))
				      : "");
	}

	/* Locate route node in the global EVPN routing table. Note that
	 * this table is a 2-level tree (RD-level + Prefix-level) similar to
	 * L3VPN routes.
	 */
	global_dest = bgp_evpn_global_node_lookup(
		bgp->rib[afi][safi], safi,
		(const struct prefix_evpn *)bgp_dest_get_prefix(dest),
		&vpn->prd, old_local);
	if (global_dest) {
		/* Delete route entry in the global EVPN table. */
		pi = delete_evpn_route_entry(bgp, afi, safi, global_dest, NULL, 0);

		/* Schedule for processing - withdraws to peers happen from
		 * this table.
		 */
		if (pi)
			bgp_process(bgp, global_dest, pi, afi, safi);
		bgp_dest_unlock_node(global_dest);
	}

	/* Delete route entry in the VNI route table, caller to remove. */
	bgp_path_info_mark_for_delete(dest, old_local);
}

/*
 * evpn_route_select_install -- 对一条 EVPN 路由计算最佳路径，并视情况把最佳路径安装或
 *                              更新到 zebra。
 *
 * 职责：1) 若 dest 置了 BGP_NODE_SELECT_DEFER(如 GR 期间)则直接返回，不入 workqueue；
 *       2) 把传入的 pi 提到路径链表头部(置 BGP_PATH_UNSORTED)，再做最佳路径选择；
 *       3) 比较选择前后：
 *          - 最佳路径未变：对远端导入路由或带 SYNC 属性的本地路由，若属性相对 zebra 有
 *            变化则重新下发(evpn_zebra_install 或 bgp_zebra_route_install)；
 *          - 最佳路径改变：bump version、切换 SELECTED 标志，随后对新最佳路径下发；若新
 *            最佳是远端导入且旧最佳是本地静态(self 生成)，说明发生了 MAC 迁移，调用
 *            evpn_delete_old_local_route 隐式删除并撤销旧本地路由；否则对旧最佳(远端导入)
 *            执行撤销。
 *       4) 清路由变更标志、回收已 REMOVED 的旧最佳路径。
 * 参数：bgp / vpn(本地 EAD-ES 路由时可为 NULL) / dest(VNI 表节点) / pi(本次处理的路径)。
 * 返回：0 或某次 zebra 下发的发送状态(仅当走了 evpn_zebra_install / evpn_zebra_uninstall
 *       直连分支)。
 * 易错点：BGP_FLAG_DELETE_IN_PROGRESS 或 VNI down 或 GR 处于 BGP_GR_SKIP_BP 时，绕过
 *       bgp_zebra_route_install 的排队逻辑，直接调用 evpn_zebra_install / evpn_zebra_uninstall。
 * 调用时机：EVPN 路由新增 / 变更 / 删除后，由路由处理流程触发。
 */
/*
 * Calculate the best path for an EVPN route. Install/update best path in zebra,
 * if appropriate.
 * Note: vpn is NULL for local EAD-ES routes.
 */
int evpn_route_select_install(struct bgp *bgp, struct bgpevpn *vpn,
			      struct bgp_dest *dest, struct bgp_path_info *pi)
{
	struct bgp_path_info *old_select, *new_select, *first;
	struct bgp_path_info_pair old_and_new;
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;
	int ret = 0;

	/* If the flag BGP_NODE_SELECT_DEFER is set, do not add route to
	 * the workqueue
	 */
	if (CHECK_FLAG(dest->flags, BGP_NODE_SELECT_DEFER)) {
		if (BGP_DEBUG(graceful_restart, GRACEFUL_RESTART))
			zlog_debug("%s: SELECT_DEFER flag set for EVPN route %pBD, dest %p",
				   bgp->name_pretty, dest, dest);

		return ret;
	}

	first = bgp_dest_get_bgp_path_info(dest);
	SET_FLAG(pi->flags, BGP_PATH_UNSORTED);
	if (pi != first) {
		if (pi->next)
			pi->next->prev = pi->prev;
		if (pi->prev)
			pi->prev->next = pi->next;

		if (first)
			first->prev = pi;
		pi->next = first;
		pi->prev = NULL;
		bgp_dest_set_bgp_path_info(dest, pi);
	}

	/* Compute the best path. */
	bgp_best_selection(bgp, dest, &bgp->maxpaths[afi][safi], &old_and_new,
			   afi, safi);
	old_select = old_and_new.old;
	new_select = old_and_new.new;

	/* If the best path hasn't changed - see if there is still something to
	 * update to zebra RIB.
	 * Remote routes and SYNC route (i.e. local routes with
	 * SYNCED_FROM_PEER flag) need to updated to zebra on any attr
	 * change.
	 */
	if (old_select && old_select == new_select
	    && old_select->type == ZEBRA_ROUTE_BGP
	    && (old_select->sub_type == BGP_ROUTE_IMPORTED ||
			bgp_evpn_attr_is_sync(old_select->attr))
	    && !CHECK_FLAG(dest->flags, BGP_NODE_USER_CLEAR)
	    && !CHECK_FLAG(old_select->flags, BGP_PATH_ATTR_CHANGED)
	    && !bgp_addpath_is_addpath_used(&bgp->tx_addpath, afi, safi)) {
		if (bgp_zebra_has_route_changed(old_select)) {
			/* BP is disabled when  BGP instance is being deleted or
			 * GR is in progress.
			 */
			if (CHECK_FLAG(bgp->flags, BGP_FLAG_DELETE_IN_PROGRESS) ||
			    CHECK_FLAG(bgp->gr_info[afi][safi].flags, BGP_GR_SKIP_BP))
				ret = evpn_zebra_install(bgp, vpn,
							 (const struct prefix_evpn *)
								 bgp_dest_get_prefix(dest),
							 old_select);
			else
				bgp_zebra_route_install(dest, old_select, bgp,
							true, vpn, false);
		}

		UNSET_FLAG(old_select->flags, BGP_PATH_MULTIPATH_CHG);
		UNSET_FLAG(old_select->flags, BGP_PATH_LINK_BW_CHG);
		bgp_zebra_clear_route_change_flags(dest);
		return ret;
	}

	/* If the user did a "clear" this flag will be set */
	UNSET_FLAG(dest->flags, BGP_NODE_USER_CLEAR);

	/* bestpath has changed; update relevant fields and install or uninstall
	 * into the zebra RIB.
	 */
	if (old_select || new_select)
		bgp_bump_version(dest);

	if (old_select)
		bgp_path_info_unset_flag(dest, old_select, BGP_PATH_SELECTED);
	if (new_select) {
		bgp_path_info_set_flag(dest, new_select, BGP_PATH_SELECTED);
		bgp_path_info_unset_flag(dest, new_select,
					 BGP_PATH_ATTR_CHANGED);
		UNSET_FLAG(new_select->flags, BGP_PATH_MULTIPATH_CHG);
		UNSET_FLAG(new_select->flags, BGP_PATH_LINK_BW_CHG);
	}

	/* a local entry with the SYNC flag also results in a MAC-IP update
	 * to zebra
	 */
	if (new_select && new_select->type == ZEBRA_ROUTE_BGP
	    && (new_select->sub_type == BGP_ROUTE_IMPORTED ||
			bgp_evpn_attr_is_sync(new_select->attr))) {
		if (CHECK_FLAG(bgp->flags, BGP_FLAG_DELETE_IN_PROGRESS) ||
		    CHECK_FLAG(bgp->gr_info[afi][safi].flags, BGP_GR_SKIP_BP))
			ret = evpn_zebra_install(bgp, vpn,
						 (const struct prefix_evpn *)bgp_dest_get_prefix(
							 dest),
						 new_select);
		else
			bgp_zebra_route_install(dest, new_select, bgp, true,
						vpn, false);

		/* If an old best existed and it was a "local" route, the only
		 * reason
		 * it would be supplanted is due to MAC mobility procedures. So,
		 * we
		 * need to do an implicit delete and withdraw that route from
		 * peers.
		 */
		if (new_select->sub_type == BGP_ROUTE_IMPORTED &&
				old_select && old_select->peer == bgp->peer_self
				&& old_select->type == ZEBRA_ROUTE_BGP
				&& old_select->sub_type == BGP_ROUTE_STATIC
				&& vpn)
			evpn_delete_old_local_route(bgp, vpn, dest,
					old_select, new_select);
	} else {
		if (old_select && old_select->type == ZEBRA_ROUTE_BGP &&
		    old_select->sub_type == BGP_ROUTE_IMPORTED) {
			if (CHECK_FLAG(bgp->flags, BGP_FLAG_DELETE_IN_PROGRESS) ||
			    CHECK_FLAG(bgp->flags, BGP_FLAG_VNI_DOWN) ||
			    CHECK_FLAG(bgp->gr_info[afi][safi].flags, BGP_GR_SKIP_BP))
				ret = evpn_zebra_uninstall(bgp, vpn,
							   (const struct prefix_evpn *)
								   bgp_dest_get_prefix(dest),
							   old_select, false);
			else
				bgp_zebra_route_install(dest, old_select, bgp,
							false, vpn, false);
		}
	}

	/* Clear any route change flags. */
	bgp_zebra_clear_route_change_flags(dest);

	/* Reap old select bgp_path_info, if it has been removed */
	if (old_select && CHECK_FLAG(old_select->flags, BGP_PATH_REMOVED))
		bgp_path_info_reap(dest, old_select);

	return ret;
}

/*
 * bgp_evpn_route_get_local_path -- 在 dest 的路径链表中查找"本地"且 addpath_rx_id 匹配的路径。
 *
 * 参数：bgp / dest / addpath_id(期望的接收侧 add-path id)。
 * 返回：命中的 bgp_path_info，未找到返回 NULL。
 * 依据：本地性由 bgp_evpn_is_path_local 判定。
 */
static struct bgp_path_info *bgp_evpn_route_get_local_path(struct bgp *bgp, struct bgp_dest *dest,
							   uint32_t addpath_id)
{
	struct bgp_path_info *pi = NULL;

	for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next) {
		if (bgp_evpn_is_path_local(bgp, pi) && pi->addpath_rx_id == addpath_id)
			return pi;
	}

	return NULL;
}

/*
 * update_evpn_type5_route_entry -- 在 EVPN 全局表中新建或更新一条 Type-5 路由表项。
 *
 * 职责：Type-5(IP Prefix)路由从 VRF 侧 originate 到 EVPN 侧时，写入 EVPN 全局表。
 *       先在 dest 上按 type5_originator 找到对应的本地路径(local_pi)：
 *       - 不存在：新建 bgp_path_info(BGP_ROUTE_STATIC，peer_self)，若源 VRF 与 EVPN 实例的
 *         AS 不同则给 aspath 追加源 VRF 的 AS，intern 属性，挂上 L3-VNI 标签(Label)，
 *         设置 addpath_rx_id，加入 dest，*route_changed = 1。
 *       - 已存在：用 attrhash_cmp 比较属性；不同则更新 originator、在 AS 不同时重算 aspath、
 *         重新 intern 属性、置 ATTR_CHANGED、必要时 restore、替换属性并刷新 uptime 与
 *         addpath_rx_id，*route_changed = 1。
 * 参数：bgp_evpn(EVPN 实例) / bgp_vrf(来源 VRF) / afi,safi / dest(全局表节点) /
 *       originator(来源路径) / attr(源属性) / 出参 route_changed / 出参 entry(本次表项) /
 *       addpath_id。
 * 返回：恒为 0。
 * 易错点：local_pi 的定位只按 type5_originator 匹配(前缀已由 dest 唯一确定)；
 *       属性变更时若 originator 被替换会打 warn(视为异常情况)。
 * 依据：extra->evpn 需先经 bgp_evpn_path_info_extra_get 分配。
 */
static int update_evpn_type5_route_entry(struct bgp *bgp_evpn, struct bgp *bgp_vrf, afi_t afi,
					 safi_t safi, struct bgp_dest *dest,
					 struct bgp_path_info *originator, struct attr *attr,
					 int *route_changed, struct bgp_path_info **entry,
					 uint32_t addpath_id)
{
	struct attr *attr_new = NULL;
	struct bgp_path_info *pi = NULL;
	struct bgp_labels bgp_labels = {};
	struct bgp_path_info *local_pi = NULL;
	struct bgp_path_info *tmp_pi = NULL;
	struct aspath *new_aspath;
	struct attr static_attr = { 0 };

	*route_changed = 0;

	for (local_pi = bgp_dest_get_bgp_path_info(dest); local_pi; local_pi = local_pi->next) {
		if (!bgp_evpn_is_path_local(bgp_evpn, local_pi))
			continue;

		if (local_pi->extra->evpn->type5_originator == originator)
			break;
	}

	static_attr = *attr;

	/*
	 * create a new route entry if one doesn't exist.
	 * Otherwise see if route attr has changed
	 */
	if (!local_pi) {

		/* route has changed as this is the first entry */
		*route_changed = 1;

		/*
		 * if the asn values are different, copy the as of
		 * source vrf to the target entry
		 */
		if (bgp_vrf->as != bgp_evpn->as) {
			new_aspath = aspath_dup(static_attr.aspath);
			new_aspath = aspath_add_seq(new_aspath, bgp_vrf->as);
			static_attr.aspath = new_aspath;
		}

		/* Add (or update) attribute to hash. */
		attr_new = bgp_attr_intern(&static_attr);
		bgp_attr_flush(&static_attr);

		/* create the route info from attribute */
		pi = info_make(ZEBRA_ROUTE_BGP, BGP_ROUTE_STATIC, 0,
			       bgp_evpn->peer_self, attr_new, dest);
		SET_FLAG(pi->flags, BGP_PATH_VALID);
		if (local_pi)
			SET_FLAG(pi->flags, BGP_PATH_MULTIPATH);

		/* Type-5 routes advertise the L3-VNI */
		bgp_evpn_path_info_extra_get(pi);
		pi->extra->evpn->type5_originator = originator;
		vni2label(bgp_vrf->l3vni, &bgp_labels.label[0]);
		bgp_labels.num_labels = 1;
		if (!bgp_path_info_labels_same(pi, &bgp_labels.label[0],
					       bgp_labels.num_labels)) {
			bgp_labels_unintern(&pi->extra->labels);
			pi->extra->labels = bgp_labels_intern(&bgp_labels);
		}

		/* handle addpath: use the original route's tx id as our rx id */
		pi->addpath_rx_id = addpath_id;

		/* add the route entry to route node*/
		bgp_path_info_add(dest, pi);
		*entry = pi;
	} else {
		tmp_pi = local_pi;
		if (!attrhash_cmp(tmp_pi->attr, attr)) {
			if (originator != local_pi->extra->evpn->type5_originator)
				zlog_warn(
					"Changing the originator of a type5 route, this is not right");

			local_pi->extra->evpn->type5_originator = originator;

			/* attribute changed */
			*route_changed = 1;

			/* if the asn values are different, copy the asn of
			 * source vrf to the target (evpn) vrf entry.
			 */
			if (bgp_vrf->as != bgp_evpn->as) {
				new_aspath = aspath_dup(static_attr.aspath);
				new_aspath = aspath_add_seq(new_aspath, bgp_vrf->as);
				static_attr.aspath = new_aspath;
			}
			/* The attribute has changed. */
			/* Add (or update) attribute to hash. */
			attr_new = bgp_attr_intern(&static_attr);
			bgp_attr_flush(&static_attr);
			bgp_path_info_set_flag(dest, tmp_pi,
					       BGP_PATH_ATTR_CHANGED);

			/* Restore route, if needed. */
			if (CHECK_FLAG(tmp_pi->flags, BGP_PATH_REMOVED))
				bgp_path_info_restore(dest, tmp_pi);

			/* Unintern existing, set to new. */
			bgp_attr_unintern(&tmp_pi->attr);
			tmp_pi->attr = attr_new;
			tmp_pi->uptime = monotime(NULL);
			tmp_pi->addpath_rx_id = addpath_id;
		}
		*entry = local_pi;
	}
	return 0;
}

/*
 * update_evpn_type5_route -- Type-5 路由的统一入口：构造属性并在 EVPN 全局表新建 / 更新表项。
 *
 * 职责：1) 取 EVPN 实例(无则直接返回 0)；
 *       2) 以源属性(src_attr)为基础，若为空则以 VRF 默认属性(bgp_attr_default_set)构造；
 *       3) 调用 bgp_evpn_fill_rmac_nh_to_attr 填写 RMAC 与下一跳(PIP / anycast 逻辑)；
 *       4) 按 ADV IPv6 / IPv4 unicast gateway-ip 的配置，必要时把源下一跳写入 overlay
 *          gateway-ip(bgp_route_evpn)；
 *       5) build_evpn_type5_route_extcomm 生成 RT 与 encap 扩展团体；
 *       6) 在 EVPN 全局表取 / 建节点(assert 节点非空)，调用 update_evpn_type5_route_entry
 *          写入表项，route_changed 时 bgp_process 调度并解锁节点；
 *       7) 若属性是本地新构造的(无 src_attr)则 uninten 临时 aspath。
 * 参数：bgp_vrf(来源 VRF) / originator / evp(前缀) / src_attr(可空) / src_afi,src_safi /
 *       addpath_id。
 * 返回：恒为 0。
 * 易错点：dest 处使用 assert(dest)，依赖 bgp_evpn_global_node_get 一定成功；
 *       仅当 src_attr 为空时才需自行 aspath_unintern 释放临时 aspath。
 * 依据：OVERLAY_INDEX_GATEWAY_IP 表示下一跳走网关 IP(GW IP 特性)。
 */
/* update evpn type-5 route entry */
static int update_evpn_type5_route(struct bgp *bgp_vrf, struct bgp_path_info *originator,
				   struct prefix_evpn *evp, struct attr *src_attr, afi_t src_afi,
				   safi_t src_safi, uint32_t addpath_id)
{
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;
	struct attr attr;
	struct bgp_dest *dest = NULL;
	struct bgp *bgp_evpn = NULL;
	int route_changed = 0;
	struct bgp_path_info *pi = NULL;
	struct ipaddr vtep_ip;

	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn)
		return 0;

	/* Build path attribute for this route - use the source attr, if
	 * present, else treat as locally originated.
	 */
	if (src_attr)
		attr = *src_attr;
	else {
		memset(&attr, 0, sizeof(attr));
		bgp_attr_default_set(&attr, bgp_vrf, BGP_ORIGIN_IGP);
	}

	/* Advertise Primary IP (PIP) is enabled, send individual
	 * IP (default instance router-id) as nexthop.
	 * PIP is disabled or vrr interface is not present
	 * use anycast-IP as nexthop and anycast RMAC.
	 */
	bgp_evpn_fill_rmac_nh_to_attr(bgp_vrf, &attr, evp, &vtep_ip);

	if (bgp_debug_zebra(NULL))
		zlog_debug("VRF %s type-5 route evp %pFX RMAC %pEA nexthop %pI4 mp_nexthop %pI6 orig vtep %pIA",
			   vrf_id_to_name(bgp_vrf->vrf_id), evp, &attr.rmac, &attr.nexthop,
			   &attr.mp_nexthop_global, &bgp_vrf->originator_ip);

	frrtrace(4, frr_bgp, evpn_advertise_type5, bgp_vrf->vrf_id, evp, &attr.rmac, &vtep_ip);

	if (src_afi == AFI_IP6 &&
	    CHECK_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN],
		       BGP_L2VPN_EVPN_ADV_IPV6_UNICAST_GW_IP)) {
		if (src_attr &&
		    !IN6_IS_ADDR_UNSPECIFIED(&src_attr->mp_nexthop_global)) {
			struct bgp_route_evpn *bre =
				XCALLOC(MTYPE_BGP_EVPN_OVERLAY,
					sizeof(struct bgp_route_evpn));

			bre->type = OVERLAY_INDEX_GATEWAY_IP;
			SET_IPADDR_V6(&bre->gw_ip);
			memcpy(&bre->gw_ip.ipaddr_v6,
			       &src_attr->mp_nexthop_global,
			       sizeof(struct in6_addr));
			bgp_attr_set_evpn_overlay(&attr, bre);
		}
	} else if (src_afi == AFI_IP &&
		   CHECK_FLAG(bgp_vrf->af_flags[AFI_L2VPN][SAFI_EVPN],
			      BGP_L2VPN_EVPN_ADV_IPV4_UNICAST_GW_IP)) {
		if (src_attr && src_attr->nexthop.s_addr != 0) {
			struct bgp_route_evpn *bre =
				XCALLOC(MTYPE_BGP_EVPN_OVERLAY,
					sizeof(struct bgp_route_evpn));

			bre->type = OVERLAY_INDEX_GATEWAY_IP;
			SET_IPADDR_V4(&bre->gw_ip);
			memcpy(&bre->gw_ip.ipaddr_v4, &src_attr->nexthop,
			       sizeof(struct in_addr));
			bgp_attr_set_evpn_overlay(&attr, bre);
		}
	}

	/* Setup RT and encap extended community */
	build_evpn_type5_route_extcomm(bgp_vrf, &attr);

	/* get the route node in global table */
	dest = bgp_evpn_global_node_get(bgp_evpn->rib[afi][safi], afi, safi,
					evp, &bgp_vrf->vrf_prd, NULL);
	assert(dest);

	/* create or update the route entry within the route node */
	update_evpn_type5_route_entry(bgp_evpn, bgp_vrf, afi, safi, dest, originator, &attr,
				      &route_changed, &pi, addpath_id);

	/* schedule for processing and unlock node */
	if (route_changed) {
		bgp_process(bgp_evpn, dest, pi, afi, safi);
		bgp_dest_unlock_node(dest);
	}

	/* uninten temporary */
	if (!src_attr)
		aspath_unintern(&attr.aspath);
	return 0;
}

/*
 * bgp_evpn_get_sync_info -- 遍历 dest 上的远端导入路径，提取"ES 同步"所需的对端信息。
 *
 * 背景：对于带有效 ESI 的本地 Type-2 路径，本地 PE 需要感知同一 ES 上对端 PE 通告的
 *       同一 MAC/IP(即 sync path)，用于 MM 序号同步与 proxy 通告。
 * 职责：1) 若传入 mac 且前缀为 MAC-IP(含 IP)，则开启 MAC 比较(纯 MAC 路由不需要)；
 *       2) 遍历路径，跳过非远端导入(BGP_ROUTE_IMPORTED)或非 VALID 的路径，用
 *          bgp_evpn_path_info_cmp 选出最优的另一条非本地路径 second_best_path；
 *       3) 若无此路径直接返回；否则比较其 ESI 是否与本地相同(同 ES 才算 sync)：
 *          - 对端 MM 序号小于本地 loc_seq 则返回(本地更新，无需同步)；
 *          - 若对端是 proxy 通告则置 proxy_from_peer，否则置 active_on_peer；
 *          - 对端带 ROUTER 标志则置 peer_router；
 *          - 用较大的 MM 序号更新 *max_sync_seq。
 * 参数：bgp / esi(本地路径 ESI) / dest / loc_seq(本地 MM 序号) /
 *       出参 max_sync_seq, active_on_peer, peer_router, proxy_from_peer / mac(可空)。
 * 返回：无(通过出参回传)。
 * 依据：注释"local path can only be present as best path"说明本地路径只会是 best，
 *       故这里遍历到的所有非本地路径即为可选 sync 源。
 */
static void bgp_evpn_get_sync_info(struct bgp *bgp, esi_t *esi,
				   struct bgp_dest *dest, uint32_t loc_seq,
				   uint32_t *max_sync_seq, bool *active_on_peer,
				   bool *peer_router, bool *proxy_from_peer,
				   const struct ethaddr *mac)
{
	struct bgp_path_info *tmp_pi;
	struct bgp_path_info *second_best_path = NULL;
	uint32_t tmp_mm_seq = 0;
	esi_t *tmp_esi;
	int paths_eq;
	struct ethaddr *tmp_mac;
	bool mac_cmp = false;
	struct prefix_evpn *evp = (struct prefix_evpn *)&dest->rn->p;


	/* mac comparison is not needed for MAC-only routes */
	if (mac && !is_evpn_prefix_ipaddr_none(evp))
		mac_cmp = true;

	/* find the best non-local path. a local path can only be present
	 * as best path
	 */
	for (tmp_pi = bgp_dest_get_bgp_path_info(dest); tmp_pi;
	     tmp_pi = tmp_pi->next) {
		if (tmp_pi->sub_type != BGP_ROUTE_IMPORTED ||
			!CHECK_FLAG(tmp_pi->flags, BGP_PATH_VALID))
			continue;

		/* ignore paths that have a different mac */
		if (mac_cmp) {
			tmp_mac = evpn_type2_path_info_get_mac(tmp_pi);
			if (memcmp(mac, tmp_mac, sizeof(*mac)))
				continue;
		}

		if (bgp_evpn_path_info_cmp(bgp, tmp_pi, second_best_path,
					   &paths_eq, false))
			second_best_path = tmp_pi;
	}

	if (!second_best_path)
		return;

	tmp_esi = bgp_evpn_attr_get_esi(second_best_path->attr);
	/* if this has the same ES destination as the local path
	 * it is a sync path
	 */
	if (!memcmp(esi, tmp_esi, sizeof(esi_t))) {
		tmp_mm_seq = mac_mobility_seqnum(second_best_path->attr);
		if (tmp_mm_seq < loc_seq)
			return;

		/* we have a non-proxy path from the ES peer.  */
		if (second_best_path->attr->es_flags &
					ATTR_ES_PROXY_ADVERT) {
			*proxy_from_peer = true;
		} else {
			*active_on_peer = true;
		}

		if (CHECK_FLAG(second_best_path->attr->evpn_flags,
			       ATTR_EVPN_FLAG_ROUTER))
			*peer_router = true;

		/* we use both proxy and non-proxy imports to
		 * determine the max sync sequence
		 */
		if (tmp_mm_seq > *max_sync_seq)
			*max_sync_seq = tmp_mm_seq;
	}
}

/*
 * update_evpn_route_entry_sync_info -- 把 dest 上各非最佳路径的 sync 信息"冒泡"到本地路径的属性。
 *
 * 背景：本地路径在 VNI 路由表中只会是 best path，需要把同 ES 对端的 sync 信息(MM 序号、
 *       active / proxy / router 标志)汇聚到本地路径的 attr 上，供 MM 序号同步与 proxy 通告使用。
 * 职责：1) 仅对 Type-2 (MAC_IP_ROUTE) 生效，其他类型直接返回；
 *       2) 取 attr 的 ESI，若 ESI 有效且 setup_sync 为真，则调用 bgp_evpn_get_sync_info 取回
 *          max_sync_seq 以及 active / proxy / router 标志，写入 attr->mm_sync_seqnum 并
 *          相应地置 / 清 es_flags 的 PEER_ACTIVE / PEER_PROXY / PEER_ROUTER；
 *       3) 若 ESI 无效，则清零 mm_sync_seqnum 并清掉 PEER_ACTIVE / PEER_PROXY。
 * 参数：bgp / dest / attr / loc_seq / setup_sync / mac。
 * 返回：无。
 * 依据：es_flags 位名为 ATTR_ES_PEER_ACTIVE / PEER_PROXY / PEER_ROUTER。
 */
/* Bubble up sync-info from all paths (non-best) to the local-path.
 * This is need for MM sequence number syncing and proxy advertisement.
 * Note: The local path can only exist as a best path in the
 * VPN route table. It will take precedence over all sync paths.
 */
static void update_evpn_route_entry_sync_info(struct bgp *bgp,
					      struct bgp_dest *dest,
					      struct attr *attr,
					      uint32_t loc_seq, bool setup_sync,
					      const struct ethaddr *mac)
{
	esi_t *esi;
	struct prefix_evpn *evp =
		(struct prefix_evpn *)bgp_dest_get_prefix(dest);

	if (evp->prefix.route_type != BGP_EVPN_MAC_IP_ROUTE)
		return;

	esi = bgp_evpn_attr_get_esi(attr);
	if (bgp_evpn_is_esi_valid(esi)) {
		if (setup_sync) {
			uint32_t max_sync_seq = 0;
			bool active_on_peer = false;
			bool peer_router = false;
			bool proxy_from_peer = false;

			bgp_evpn_get_sync_info(bgp, esi, dest, loc_seq,
					       &max_sync_seq, &active_on_peer,
					       &peer_router, &proxy_from_peer,
					       mac);
			attr->mm_sync_seqnum = max_sync_seq;
			if (active_on_peer)
				SET_FLAG(attr->es_flags, ATTR_ES_PEER_ACTIVE);
			else
				UNSET_FLAG(attr->es_flags, ATTR_ES_PEER_ACTIVE);
			if (proxy_from_peer)
				SET_FLAG(attr->es_flags, ATTR_ES_PEER_PROXY);
			else
				UNSET_FLAG(attr->es_flags, ATTR_ES_PEER_PROXY);
			if (peer_router)
				SET_FLAG(attr->es_flags, ATTR_ES_PEER_ROUTER);
			else
				UNSET_FLAG(attr->es_flags, ATTR_ES_PEER_ROUTER);

			if (BGP_DEBUG(evpn_mh, EVPN_MH_RT)) {
				char esi_buf[ESI_STR_LEN];

				zlog_debug("setup sync info for %pFX es %s max_seq %d %s%s%s",
					   evp,
					   esi_to_str(esi, esi_buf,
						      sizeof(esi_buf)),
					   max_sync_seq,
					   CHECK_FLAG(attr->es_flags,
						      ATTR_ES_PEER_ACTIVE)
						   ? "peer-active "
						   : "",
					   CHECK_FLAG(attr->es_flags,
						      ATTR_ES_PEER_PROXY)
						   ? "peer-proxy "
						   : "",
					   CHECK_FLAG(attr->es_flags,
						      ATTR_ES_PEER_ROUTER)
						   ? "peer-router "
						   : "");
			}
		}
	} else {
		attr->mm_sync_seqnum = 0;
		UNSET_FLAG(attr->es_flags, ATTR_ES_PEER_ACTIVE);
		UNSET_FLAG(attr->es_flags, ATTR_ES_PEER_PROXY);
	}
}

/*
 * bgp_evpn_is_macip_with_l3vni -- 判断是否为携带有效 L3 信息的 Type-2 MAC-IP 路由。
 *
 * 条件：路由类型为 Type-2，且 IP 地址为 IPv4 或非 link-local 的 IPv6，同时 VNI 配置了
 *       L3-VNI(VNI_FLAG_USE_TWO_LABELS)且能取到 L3-VNI。
 * 返回：满足上述全部条件为 true。
 * 用途：决定 Type-2 路由是否额外携带第二个标签(L3-VNI)与 L3 属性(RMAC、L3 RT)。
 */
/*
 * Check if the route is a type-2 MAC-IP route with a valid global address
 * (IPv4 or non-link-local IPv6) and the VPN has a valid L3 VNI configured.
 */
static inline bool bgp_evpn_is_macip_with_l3vni(struct bgpevpn *vpn, const struct prefix_evpn *p)
{
	return p->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE &&
	       (is_evpn_prefix_ipaddr_v4(p) ||
		(is_evpn_prefix_ipaddr_v6(p) &&
		 !IN6_IS_ADDR_LINKLOCAL(&p->prefix.macip_addr.ip.ipaddr_v6))) &&
	       CHECK_FLAG(vpn->flags, VNI_FLAG_USE_TWO_LABELS) && bgpevpn_get_l3vni(vpn);
}

/*
 * bgp_evpn_route_add_l3_attrs_ok -- 判断是否应给该路由追加 L3 属性(RMAC、L3 RT 等)。
 *
 * 条件：本身是带 L3 的 Type-2 MAC-IP(bgp_evpn_is_macip_with_l3vni)，且所在 ESI 允许
 *       添加 L3 属性(bgp_evpn_es_add_l3_attrs_ok)。
 * 返回：允许为 true。
 * 依据：对 ES 场景额外做一次检查，避免对不适用的 ES 路由挂 L3 属性。
 */
/*
 * While adding l3-attrs such as rmac and l3 RTs we need an added check for ES
 * as well along with checking for mac-ip with l3vni.
 */
static inline bool bgp_evpn_route_add_l3_attrs_ok(struct bgpevpn *vpn, const struct prefix_evpn *p,
						  esi_t *esi)
{
	return bgp_evpn_is_macip_with_l3vni(vpn, p) && bgp_evpn_es_add_l3_attrs_ok(esi);
}

/*
 * update_evpn_route_entry -- 在 VNI 路由表或全局 EVPN 表中新建 / 更新一条本地的 EVPN 路由表项。
 *
 * 职责：定位 dest 上的本地路径 local_pi(bgp_evpn_route_get_local_path)：
 *       - 若无 local_pi 且 add 为 0，则无事可做直接返回 0；
 *       - 记下旧路径是否 sync(old_is_sync 出参)；
 *       - 先调用 update_evpn_route_entry_sync_info 冒泡 sync 信息；
 *       - 非 GW 且 seq 非零则把 MM 扩展团体写入属性；
 *       - 不存在本地路径则新建 bgp_path_info(BGP_ROUTE_STATIC，peer_self)，挂 VNI 标签
 *         (必要时第二标签 L3-VNI)，设置 type-2 的 MAC / IP 附加信息与 SVI 标记；
 *         已存在则用 attrhash_cmp 判定属性是否变化：变化才重算标签、置 ATTR_CHANGED、
 *         restore、替换属性并刷新 uptime，否则 route_change = 0；
 *       - route_change 且 vpn_rt 且为 Type-2 时，把该路径与目的 ES 建立关联
 *         (bgp_evpn_path_es_link)。
 * 参数：bgp / vpn / afi,safi / dest / attr / mac(可空) / ip(可空) / add(0 表示不新建) /
 *       出参 pi / flags / seq / vpn_rt(是否 VNI 表) / 出参 old_is_sync(可空)。
 * 返回：route_change(1 表示新建或属性变化，0 表示无变化)。
 * 易错点：本地路径定位固定用 addpath_id = 0；
 *       属性变化判定要求路径未被 REMOVED 且 attrhash_cmp 相等才视为无变化。
 */
/*
 * Create or update EVPN route entry. This could be in the VNI route tables
 * or the global route table.
 */
static int update_evpn_route_entry(struct bgp *bgp, struct bgpevpn *vpn,
				   afi_t afi, safi_t safi,
				   struct bgp_dest *dest, struct attr *attr,
				   const struct ethaddr *mac,
				   const struct ipaddr *ip, int add,
				   struct bgp_path_info **pi, uint8_t flags,
				   uint32_t seq, bool vpn_rt, bool *old_is_sync)
{
	struct bgp_path_info *tmp_pi;
	struct bgp_path_info *local_pi;
	struct attr *attr_new;
	struct attr local_attr;
	struct bgp_labels bgp_labels = {};
	int route_change = 1;
	const struct prefix_evpn *evp;

	*pi = NULL;
	evp = (const struct prefix_evpn *)bgp_dest_get_prefix(dest);

	/* See if this is an update of an existing route, or a new add. */
	local_pi = bgp_evpn_route_get_local_path(bgp, dest, 0);

	/* If route doesn't exist already, create a new one, if told to.
	 * Otherwise act based on whether the attributes of the route have
	 * changed or not.
	 */
	if (!local_pi && !add)
		return 0;

	if (old_is_sync && local_pi)
		*old_is_sync = bgp_evpn_attr_is_sync(local_pi->attr);

	/* if a local path is being added with a non-zero esi look
	 * for SYNC paths from ES peers and bubble up the sync-info
	 */
	update_evpn_route_entry_sync_info(bgp, dest, attr, seq, vpn_rt, mac);

	/* For non-GW MACs, update MAC mobility seq number, if needed. */
	if (seq && !CHECK_FLAG(flags, ZEBRA_MACIP_TYPE_GW))
		add_mac_mobility_to_attr(seq, attr);

	if (!local_pi) {
		local_attr = *attr;

		/* Extract MAC mobility sequence number, if any. */
		local_attr.mm_seqnum = bgp_attr_mac_mobility_seqnum(&local_attr);

		/* Add (or update) attribute to hash. */
		attr_new = bgp_attr_intern(&local_attr);

		/* Create new route with its attribute. */
		tmp_pi = info_make(ZEBRA_ROUTE_BGP, BGP_ROUTE_STATIC, 0,
				   bgp->peer_self, attr_new, dest);
		SET_FLAG(tmp_pi->flags, BGP_PATH_VALID);
		bgp_path_info_extra_get(tmp_pi);

		/* The VNI goes into the 'label' field of the route */
		vni2label(vpn->vni, &bgp_labels.label[0]);
		bgp_labels.num_labels = 1;

		/* Type-2 routes may carry a second VNI - the L3-VNI.
		 * Only attach second label if we are advertising two labels for
		 * type-2 routes.
		 */
		if (bgp_evpn_is_macip_with_l3vni(vpn, evp)) {
			vni_t l3vni;

			l3vni = bgpevpn_get_l3vni(vpn);
			if (l3vni) {
				vni2label(l3vni, &bgp_labels.label[1]);
				bgp_labels.num_labels++;
			}
		}

		if (!bgp_path_info_labels_same(tmp_pi, &bgp_labels.label[0],
					       bgp_labels.num_labels)) {
			bgp_labels_unintern(&tmp_pi->extra->labels);
			tmp_pi->extra->labels = bgp_labels_intern(&bgp_labels);
		}

		if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) {
			if (mac)
				evpn_type2_path_info_set_mac(tmp_pi, *mac);
			else if (ip)
				evpn_type2_path_info_set_ip(tmp_pi, *ip);
		}

		/* Mark route as self type-2 route */
		if (flags && CHECK_FLAG(flags, ZEBRA_MACIP_TYPE_SVI_IP))
			tmp_pi->extra->evpn->af_flags =
				BGP_EVPN_MACIP_TYPE_SVI_IP;
		bgp_path_info_add(dest, tmp_pi);
	} else {
		tmp_pi = local_pi;
		if (!CHECK_FLAG(tmp_pi->flags, BGP_PATH_REMOVED) && attrhash_cmp(tmp_pi->attr, attr))
			route_change = 0;
		else {
			/*
			 * The attributes have changed, type-2 routes needs to
			 * be advertised with right labels.
			 */
			vni2label(vpn->vni, &bgp_labels.label[0]);
			bgp_labels.num_labels = 1;
			if (bgp_evpn_is_macip_with_l3vni(vpn, evp)) {
				vni_t l3vni;

				l3vni = bgpevpn_get_l3vni(vpn);
				if (l3vni) {
					vni2label(l3vni, &bgp_labels.label[1]);
					bgp_labels.num_labels++;
				}
			}
			if (!bgp_path_info_labels_same(tmp_pi,
						       &bgp_labels.label[0],
						       bgp_labels.num_labels)) {
				bgp_labels_unintern(&tmp_pi->extra->labels);
				tmp_pi->extra->labels =
					bgp_labels_intern(&bgp_labels);
			}

			if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) {
				if (mac)
					evpn_type2_path_info_set_mac(tmp_pi,
								     *mac);
				else if (ip)
					evpn_type2_path_info_set_ip(tmp_pi,
								    *ip);
			}

			/* The attribute has changed. */
			/* Add (or update) attribute to hash. */
			local_attr = *attr;
			bgp_path_info_set_flag(dest, tmp_pi,
					       BGP_PATH_ATTR_CHANGED);

			/* Extract MAC mobility sequence number, if any. */
			local_attr.mm_seqnum =
				bgp_attr_mac_mobility_seqnum(&local_attr);

			attr_new = bgp_attr_intern(&local_attr);

			/* Restore route, if needed. */
			if (CHECK_FLAG(tmp_pi->flags, BGP_PATH_REMOVED))
				bgp_path_info_restore(dest, tmp_pi);

			/* Unintern existing, set to new. */
			bgp_attr_unintern(&tmp_pi->attr);
			tmp_pi->attr = attr_new;
			tmp_pi->uptime = monotime(NULL);
		}
	}

	/* local MAC-IP routes in the VNI table are linked to
	 * the destination ES
	 */
	if (route_change && vpn_rt
	    && (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE))
		bgp_evpn_path_es_link(tmp_pi, vpn->vni,
				      bgp_evpn_attr_get_esi(tmp_pi->attr));

	/* Return back the route entry. */
	*pi = tmp_pi;
	return route_change;
}

/*
 * evpn_zebra_reinstall_best_route -- 找到 dest 上的 SELECTED 路径并重新下发到 zebra。
 *
 * 职责：遍历路径找到置 BGP_PATH_SELECTED 的最佳路径；若它是远端导入或带 SYNC 属性的
 *       本地路由，则重新下发(删除进行中直连 evpn_zebra_install，否则走
 *       bgp_zebra_route_install)。
 * 用途：本地非最佳路径被驱逐后，需要把真正的最佳远端路径重新装回 zebra。
 */
static void evpn_zebra_reinstall_best_route(struct bgp *bgp,
					    struct bgpevpn *vpn,
					    struct bgp_dest *dest)
{
	struct bgp_path_info *tmp_ri;
	struct bgp_path_info *curr_select = NULL;

	for (tmp_ri = bgp_dest_get_bgp_path_info(dest); tmp_ri;
	     tmp_ri = tmp_ri->next) {
		if (CHECK_FLAG(tmp_ri->flags, BGP_PATH_SELECTED)) {
			curr_select = tmp_ri;
			break;
		}
	}

	if (curr_select && curr_select->type == ZEBRA_ROUTE_BGP &&
	    (curr_select->sub_type == BGP_ROUTE_IMPORTED ||
	     bgp_evpn_attr_is_sync(curr_select->attr))) {
		if (CHECK_FLAG(bgp->flags, BGP_FLAG_DELETE_IN_PROGRESS))
			evpn_zebra_install(bgp, vpn,
					   (const struct prefix_evpn *)
						   bgp_dest_get_prefix(dest),
					   curr_select);
		else
			bgp_zebra_route_install(dest, curr_select, bgp, true,
						vpn, false);
	}
}

/*
 * evpn_cleanup_local_non_best_route -- 本地路径未成为最佳路径时，驱逐它并让 zebra 重新装回最佳远端路径。
 *
 * 背景：本地路径通常应作为最佳；但当 bgp 侧添加与 zebra 侧添加交叉(竞态)时，本地路径可能
 *       从未被选为最佳，需要额外处理，避免 bgp 注入并保留一条非最佳的本地路径。
 * 职责：1) 打印调试日志；
 *       2) evpn_delete_old_local_route 删除该本地路径(向对端撤销)；
 *       3) evpn_zebra_reinstall_best_route 让 zebra 重新添加最佳远端路径；
 *       4) bgp_path_info_reap 回收该本地路径。
 * 返回：reap 之后新的路径链表头对应的 dest(供调用者重新锁定使用)。
 * 依据：注释明确说明这是竞态场景下的补偿处理。
 */
/*
 * If the local route was not selected evict it and tell zebra to re-add
 * the best remote dest.
 *
 * Typically a local path added by zebra is expected to be selected as
 * best. In which case when a remote path wins as best (later)
 * evpn_route_select_install itself evicts the older-local-best path.
 *
 * However if bgp's add and zebra's add cross paths (race condition) it
 * is possible that the local path is no longer the "older" best path.
 * It is a path that was never designated as best and hence requires
 * additional handling to prevent bgp from injecting and holding on to a
 * non-best local path.
 */
static struct bgp_dest *
evpn_cleanup_local_non_best_route(struct bgp *bgp, struct bgpevpn *vpn,
				  struct bgp_dest *dest,
				  struct bgp_path_info *local_pi)
{
	/* local path was not picked as the winner; kick it out */
	if (bgp_debug_zebra(NULL))
		zlog_debug("evicting local evpn prefix %pBD as remote won",
			   dest);

	evpn_delete_old_local_route(bgp, vpn, dest, local_pi, NULL);

	/* tell zebra to re-add the best remote path */
	evpn_zebra_reinstall_best_route(bgp, vpn, dest);

	return bgp_path_info_reap(dest, local_pi);
}

/*
 * update_evpn_route -- 为指定的 VNI 新建 / 更新一条本地 EVPN 路由，并调度处理。
 *
 * 职责：1) 以默认属性为基础，把 vpn->originator_ip 作为下一跳；
 *       2) 按 flags 映射到 evpn_flags / es_flags(STICKY / DEFAULT_GW / ROUTER / PROXY_ADVERT)；
 *       3) 若 esi 有效则记录 ESI，并按"是否本地且非 bypass"置 ATTR_ES_IS_LOCAL；
 *       4) Type-3 (IMET) 设置 PMSI 隧道类型为 INGR_REPL(头端复制)；
 *       5) Type-2 调用 bgp_evpn_get_rmac_nexthop 填 RMAC 与下一跳；
 *       6) 记录 VNI 标签；按需追加 L3 属性(add_l3_attrs)与 SoO；
 *       7) 在 VNI 表取 / 建节点(无 RD)并调用 update_evpn_route_entry 建立 / 更新表项；
 *       8) 锁定 pi 后调用 evpn_route_select_install 做选路；若本地路径未被选中则调用
 *          evpn_cleanup_local_non_best_route 驱逐并重装远端；若仍是最佳但由 sync 变非 sync，
 *          则通知 zebra 撤销 sync 路径；
 *       9) route_change 时把路由导出到全局表(RD 级 + 前缀级两级树)并 bgp_process 调度，
 *          由全局表向对端宣告。
 * 参数：bgp / vpn / p(前缀) / flags(zebra MACIP 类型位) / seq(MM 序号) / esi(可空)。
 * 返回：恒为 0。
 * 易错点：纯 MAC 路由(mac_only)时 MAC 传 NULL；pi 用 lock / unlock 保护，避免被
 *       evpn_route_select_install 释放。
 */
/*
 * Create or update EVPN route (of type based on prefix) for specified VNI
 * and schedule for processing.
 */
static int update_evpn_route(struct bgp *bgp, struct bgpevpn *vpn,
			     struct prefix_evpn *p, uint8_t flags,
			     uint32_t seq, esi_t *esi)
{
	struct bgp_dest *dest;
	struct attr attr;
	struct attr *attr_new;
	bool add_l3_attrs = false;
	struct bgp_path_info *pi;
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;
	int route_change;
	bool old_is_sync = false;
	bool mac_only = false;
	struct ecommunity *macvrf_soo = NULL;

	memset(&attr, 0, sizeof(attr));

	/* Build path-attribute for this route. */
	bgp_attr_default_set(&attr, bgp, BGP_ORIGIN_IGP);
	bgp_evpn_vtep_ip_to_attr_nh(&vpn->originator_ip, &attr);
	if (CHECK_FLAG(flags, ZEBRA_MACIP_TYPE_STICKY))
		SET_FLAG(attr.evpn_flags, ATTR_EVPN_FLAG_STICKY);
	if (CHECK_FLAG(flags, ZEBRA_MACIP_TYPE_GW))
		SET_FLAG(attr.evpn_flags, ATTR_EVPN_FLAG_DEFAULT_GW);
	if (CHECK_FLAG(flags, ZEBRA_MACIP_TYPE_ROUTER_FLAG))
		SET_FLAG(attr.evpn_flags, ATTR_EVPN_FLAG_ROUTER);
	if (CHECK_FLAG(flags, ZEBRA_MACIP_TYPE_PROXY_ADVERT))
		SET_FLAG(attr.es_flags, ATTR_ES_PROXY_ADVERT);

	if (esi && bgp_evpn_is_esi_valid(esi)) {
		memcpy(&attr.esi, esi, sizeof(esi_t));
		/* ES should not be marked local if ESI is in bypass */
		if (bgp_evpn_is_esi_local_and_non_bypass(esi))
			SET_FLAG(attr.es_flags, ATTR_ES_IS_LOCAL);
		else
			UNSET_FLAG(attr.es_flags, ATTR_ES_IS_LOCAL);
	}

	/* PMSI is only needed for type-3 routes */
	if (p->prefix.route_type == BGP_EVPN_IMET_ROUTE) {
		SET_FLAG(attr.flag, ATTR_FLAG_BIT(BGP_ATTR_PMSI_TUNNEL));
		bgp_attr_set_pmsi_tnl_type(&attr, PMSI_TNLTYPE_INGR_REPL);
	}

	/* router mac is only needed for type-2 routes here. */
	if (p->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) {
		uint8_t af_flags = 0;

		if (CHECK_FLAG(flags, ZEBRA_MACIP_TYPE_SVI_IP))
			SET_FLAG(af_flags, BGP_EVPN_MACIP_TYPE_SVI_IP);

		bgp_evpn_get_rmac_nexthop(vpn, p, &attr, af_flags);
	}

	if (bgp_debug_zebra(NULL)) {
		char buf3[ESI_STR_LEN];

		zlog_debug(
			"VRF %s vni %u type-%u route evp %pFX RMAC %pEA nexthop %pIA esi %s",
			vpn->bgp_vrf ? vrf_id_to_name(vpn->bgp_vrf->vrf_id)
				     : "None",
			vpn->vni, p->prefix.route_type, p, &attr.rmac,
			&vpn->originator_ip,
			esi_to_str(esi, buf3, sizeof(buf3)));
	}

	vni2label(vpn->vni, &(attr.label));

	/* Include L3 VNI related attributes (RTs, RMAC and MPLS Label2)
	 * for type-2 routes, if they're IPv4 or IPv6 global addresses and
	 * we're advertising L3VNI with these routes.
	 */
	add_l3_attrs = bgp_evpn_route_add_l3_attrs_ok(vpn, p,
						      CHECK_FLAG(attr.es_flags, ATTR_ES_IS_LOCAL)
							      ? &attr.esi
							      : NULL);

	if (bgp->evpn_info)
		macvrf_soo = bgp->evpn_info->soo;

	/* Set up extended community. */
	build_evpn_route_extcomm(vpn, &attr, add_l3_attrs, macvrf_soo);

	/* First, create (or fetch) route node within the VNI.
	 * NOTE: There is no RD here.
	 */
	dest = bgp_evpn_vni_node_get(vpn, p, NULL);

	if ((p->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) &&
	    (is_evpn_prefix_ipaddr_none(p) == true))
		mac_only = true;

	/* Create or update route entry. */
	route_change = update_evpn_route_entry(
		bgp, vpn, afi, safi, dest, &attr,
		(mac_only ? NULL : &p->prefix.macip_addr.mac), NULL /* ip */, 1,
		&pi, flags, seq, true /* setup_sync */, &old_is_sync);
	assert(pi);
	attr_new = pi->attr;

	/* lock ri to prevent freeing in evpn_route_select_install */
	bgp_path_info_lock(pi);

	/* Perform route selection. Normally, the local route in the
	 * VNI is expected to win and be the best route. However, if
	 * there is a race condition where a host moved from local to
	 * remote and the remote route was received in BGP just prior
	 * to the local MACIP notification from zebra, the remote
	 * route would win, and we should evict the defunct local route
	 * and (re)install the remote route into zebra.
	 */
	evpn_route_select_install(bgp, vpn, dest, pi);
	/*
	 * If the new local route was not selected evict it and tell zebra
	 * to re-add the best remote dest. BGP doesn't retain non-best local
	 * routes.
	 */
	if (CHECK_FLAG(pi->flags, BGP_PATH_REMOVED)) {
		route_change = 0;
	} else {
		if (!CHECK_FLAG(pi->flags, BGP_PATH_SELECTED)) {
			route_change = 0;
			dest = evpn_cleanup_local_non_best_route(bgp, vpn, dest,
								 pi);
		} else {
			bool new_is_sync;

			/* If the local path already existed and is still the
			 * best path we need to also check if it transitioned
			 * from being a sync path to a non-sync path. If it
			 * it did we need to notify zebra that the sync-path
			 * has been removed.
			 */
			new_is_sync = bgp_evpn_attr_is_sync(pi->attr);
			if (!new_is_sync && old_is_sync) {
				if (CHECK_FLAG(bgp->flags,
					       BGP_FLAG_DELETE_IN_PROGRESS))
					evpn_zebra_uninstall(bgp, vpn, p, pi,
							     true);
				else
					bgp_zebra_route_install(dest, pi, bgp,
								false, vpn,
								true);
			}
		}
	}
	bgp_path_info_unlock(pi);

	if (dest)
		bgp_dest_unlock_node(dest);

	/* If this is a new route or some attribute has changed, export the
	 * route to the global table. The route will be advertised to peers
	 * from there. Note that this table is a 2-level tree (RD-level +
	 * Prefix-level) similar to L3VPN routes.
	 */
	if (route_change) {
		struct bgp_path_info *global_pi;

		dest = bgp_evpn_global_node_get(bgp->rib[afi][safi], afi, safi,
						p, &vpn->prd, NULL);
		update_evpn_route_entry(
			bgp, vpn, afi, safi, dest, attr_new, NULL /* mac */,
			NULL /* ip */, 1, &global_pi, flags, seq,
			false /* setup_sync */, NULL /* old_is_sync */);

		/* Schedule for processing and unlock node. */
		bgp_process(bgp, dest, global_pi, afi, safi);
		bgp_dest_unlock_node(dest);
	}

	/* Unintern temporary. */
	aspath_unintern(&attr.aspath);

	return 0;
}

/*
 * delete_evpn_route_entry -- 删除(标记)一条 EVPN 路由表项，可用于 VNI / ESI 表或全局表。
 *
 * 职责：给定 originator 时按其 type5_originator 匹配本地路径(Type-5 用)；否则用
 *       bgp_evpn_route_get_local_path 按 addpath_id 找本地路径(Type-2 等用)；
 *       找到后 bgp_path_info_mark_for_delete 标记删除。
 * 参数：bgp / afi,safi / dest / originator(可空) / addpath_id。
 * 返回：被标记的 bgp_path_info，未找到返回 NULL。
 * 易错点：只做"标记"，真正的移除在对端撤销与选路流程中完成。
 */
/*
 * Delete EVPN route entry.
 * The entry can be in ESI/VNI table or the global table.
 */
struct bgp_path_info *delete_evpn_route_entry(struct bgp *bgp, afi_t afi, safi_t safi,
					      struct bgp_dest *dest,
					      const struct bgp_path_info *originator,
					      uint32_t addpath_id)
{
	struct bgp_path_info *pi = NULL;

	/* Now, find matching route. */
	if (originator) {
		for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next) {
			if (!bgp_evpn_is_path_local(bgp, pi))
				continue;

			if (pi->extra->evpn->type5_originator == originator)
				break;
		}
	} else
		pi = bgp_evpn_route_get_local_path(bgp, dest, addpath_id);

	/* Mark route for delete. */
	if (pi)
		bgp_path_info_mark_for_delete(dest, pi);

	return pi;
}

/*
 * delete_evpn_type5_route -- 删除 EVPN 全局表中某 VRF 的 Type-5 路由并调度向对端撤销。
 *
 * 职责：取 EVPN 实例(无则返回)；用 vrf_prd 在全局表定位节点(不存在则返回)；
 *       调用 delete_evpn_route_entry 按 originator / addpath_id 标记删除该 Type-5 表项，
 *       若有删除则 bgp_process 调度，最后解锁节点。
 * 参数：bgp_vrf(来源 VRF) / originator / evp / addpath_id。
 * 返回：恒为 0。
 * 依据：frrtrace evpn_withdraw_type5 用于观测 Type-5 撤销。
 */
/* Delete EVPN type5 route */
static int delete_evpn_type5_route(struct bgp *bgp_vrf, const struct bgp_path_info *originator,
				   struct prefix_evpn *evp, uint32_t addpath_id)
{
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;
	struct bgp_dest *dest = NULL;
	struct bgp_path_info *pi = NULL;
	struct bgp *bgp_evpn = NULL; /* evpn bgp instance */

	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn)
		return 0;

	/* locate the global route entry for this type-5 prefix */
	dest = bgp_evpn_global_node_lookup(bgp_evpn->rib[afi][safi], safi, evp,
					   &bgp_vrf->vrf_prd, NULL);
	if (!dest)
		return 0;

	frrtrace(2, frr_bgp, evpn_withdraw_type5, bgp_vrf->vrf_id, evp);

	pi = delete_evpn_route_entry(bgp_evpn, afi, safi, dest, originator, addpath_id);
	if (pi)
		bgp_process(bgp_evpn, dest, pi, afi, safi);
	bgp_dest_unlock_node(dest);
	return 0;
}

/*
 * delete_evpn_route -- 删除指定 VNI 的一条 EVPN 路由(VNI 表 + 全局表)并调度处理。
 *
 * 职责：1) 在 VNI 表定位节点(无则无需处理直接返回)；
 *       2) 在全局 EVPN 表(RD 级 + 前缀级两级树)用 vpn->prd 定位节点，删除其表项并
 *          bgp_process 调度，由全局表向对端撤销，然后解锁全局节点；
 *       3) 删除 VNI 表项，标记删除并调用 evpn_route_select_install 触发选路(可能触发
 *          zebra 撤销)；
 *       4) 解锁 VNI 节点。
 * 参数：bgp / vpn / p。
 * 返回：恒为 0。
 * 易错点：VNI 表路由无 RD；全局表才带 vpn->prd。
 */
/*
 * Delete EVPN route (of type based on prefix) for specified VNI and
 * schedule for processing.
 */
static int delete_evpn_route(struct bgp *bgp, struct bgpevpn *vpn,
			     struct prefix_evpn *p)
{
	struct bgp_dest *dest, *global_dest;
	struct bgp_path_info *pi;
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;

	/* First, locate the route node within the VNI. If it doesn't exist,
	 * there
	 * is nothing further to do.
	 * NOTE: There is no RD here.
	 */
	dest = bgp_evpn_vni_node_lookup(vpn, p, NULL);
	if (!dest)
		return 0;

	/* Next, locate route node in the global EVPN routing table. Note that
	 * this table is a 2-level tree (RD-level + Prefix-level) similar to
	 * L3VPN routes.
	 */
	global_dest = bgp_evpn_global_node_lookup(bgp->rib[afi][safi], safi, p,
						  &vpn->prd, NULL);
	if (global_dest) {
		/* Delete route entry in the global EVPN table. */
		pi = delete_evpn_route_entry(bgp, afi, safi, global_dest, NULL, 0);

		/* Schedule for processing - withdraws to peers happen from
		 * this table.
		 */
		if (pi)
			bgp_process(bgp, global_dest, pi, afi, safi);
		bgp_dest_unlock_node(global_dest);
	}

	/* Delete route entry in the VNI route table. This can just be removed.
	 */
	pi = delete_evpn_route_entry(bgp, afi, safi, dest, NULL, 0);
	if (pi) {
		bgp_path_info_mark_for_delete(dest, pi);
		evpn_route_select_install(bgp, vpn, dest, pi);
	}

	/* dest should still exist due to locking make coverity happy */
	assert(dest);
	bgp_dest_unlock_node(dest);

	return 0;
}

/*
 * bgp_evpn_update_type2_route_entry -- 重新评估并刷新一条本地 Type-2 路由(如 Tunnel / 下一跳 IP
 *                                     变化、ES 本地 / 非本地状态变化时)。
 *
 * 职责：1) 路径已 REMOVED 则直接返回；
 *       2) 把 VNI 表中的 MAC / IP 补全为全局前缀(evpn_type2_prefix_global_copy)：
 *          纯 MAC 前缀补 IP，含 IP 前缀补 MAC；
 *       3) 按 local_pi 重建属性：默认属性 + originator_ip 下一跳；继承 evpn_flags / es_flags；
 *          若为 DEFAULT_GW 且 IPv6 则补 ROUTER 标志；拷贝 ESI 并按"本地且非 bypass"重设
 *          ATTR_ES_IS_LOCAL；再取 RMAC / 下一跳、VNI 标签、L3 属性、SoO 与扩展团体；
 *       4) 调用 update_evpn_route_entry(add = 0，即只更新不新建)刷新表项；
 *       5) evpn_route_select_install 选路，未选中则驱逐并重装远端；仍是最佳但由 sync 变
 *          非 sync 则通知 zebra 撤销 sync 路径；
 *       6) route_change 时同步刷新全局表并 bgp_process 调度，最后 uninten 临时 aspath。
 * 参数：bgp / vpn / dest(VNI 表节点) / local_pi(本地路径) / caller(日志标识)。
 * 返回：无。
 * 易错点：这里 update_evpn_route_entry 的 add 传 0，因此本地路径必须已存在才更新；
 *       pi 同样用 lock / unlock 保护。
 */
void bgp_evpn_update_type2_route_entry(struct bgp *bgp, struct bgpevpn *vpn,
				       struct bgp_dest *dest,
				       struct bgp_path_info *local_pi,
				       const char *caller)
{
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;
	struct bgp_path_info *pi;
	struct attr attr;
	struct attr *attr_new;
	uint32_t seq;
	bool add_l3_attrs = false;
	struct bgp_dest *global_dest;
	struct bgp_path_info *global_pi;
	struct prefix_evpn evp;
	int route_change;
	bool old_is_sync = false;
	struct ecommunity *macvrf_soo = NULL;

	if (CHECK_FLAG(local_pi->flags, BGP_PATH_REMOVED))
		return;

	/*
	 * VNI table MAC-IP prefixes don't have MAC so make sure it's set from
	 * path info here.
	 */
	if (is_evpn_prefix_ipaddr_none((struct prefix_evpn *)&dest->rn->p)) {
		/* VNI MAC -> Global */
		evpn_type2_prefix_global_copy(
			&evp, (struct prefix_evpn *)&dest->rn->p, NULL /* mac */,
			evpn_type2_path_info_get_ip(local_pi));
	} else {
		/* VNI IP -> Global */
		evpn_type2_prefix_global_copy(
			&evp, (struct prefix_evpn *)&dest->rn->p,
			evpn_type2_path_info_get_mac(local_pi), NULL /* ip */);
	}

	/*
	 * Build attribute per local route as the MAC mobility and
	 * some other values could differ for different routes. The
	 * attributes will be shared in the hash table.
	 */
	bgp_attr_default_set(&attr, bgp, BGP_ORIGIN_IGP);
	bgp_evpn_vtep_ip_to_attr_nh(&vpn->originator_ip, &attr);
	attr.evpn_flags = local_pi->attr->evpn_flags;
	attr.es_flags = local_pi->attr->es_flags;
	if (CHECK_FLAG(local_pi->attr->evpn_flags, ATTR_EVPN_FLAG_DEFAULT_GW)) {
		SET_FLAG(attr.evpn_flags, ATTR_EVPN_FLAG_DEFAULT_GW);
		if (is_evpn_prefix_ipaddr_v6(&evp))
			SET_FLAG(attr.evpn_flags, ATTR_EVPN_FLAG_ROUTER);
	}
	memcpy(&attr.esi, &local_pi->attr->esi, sizeof(esi_t));
	/*
	 * We are evaluating a change in local/non-local status.
	 * The es_flags need to align with the local ES status
	 */
	if (bgp_evpn_is_esi_local_and_non_bypass(&attr.esi))
		SET_FLAG(attr.es_flags, ATTR_ES_IS_LOCAL);
	else
		UNSET_FLAG(attr.es_flags, ATTR_ES_IS_LOCAL);

	bgp_evpn_get_rmac_nexthop(vpn, &evp, &attr, local_pi->extra->evpn->af_flags);
	vni2label(vpn->vni, &(attr.label));
	/* Add L3 VNI RTs and RMAC for non IPv6 link-local if
	 * using L3 VNI for type-2 routes also.
	 */
	add_l3_attrs = bgp_evpn_route_add_l3_attrs_ok(vpn, &evp,
						      CHECK_FLAG(attr.es_flags, ATTR_ES_IS_LOCAL)
							      ? &attr.esi
							      : NULL);

	if (bgp->evpn_info)
		macvrf_soo = bgp->evpn_info->soo;

	/* Set up extended community. */
	build_evpn_route_extcomm(vpn, &attr, add_l3_attrs, macvrf_soo);
	seq = mac_mobility_seqnum(local_pi->attr);

	if (bgp_debug_zebra(NULL)) {
		char buf3[ESI_STR_LEN];

		zlog_debug(
			"VRF %s vni %u evp %pFX RMAC %pEA nexthop %pI4 esi %s esf 0x%x from %s",
			vpn->bgp_vrf ? vrf_id_to_name(vpn->bgp_vrf->vrf_id)
				     : " ",
			vpn->vni, &evp, &attr.rmac, &attr.mp_nexthop_global_in,
			esi_to_str(&attr.esi, buf3, sizeof(buf3)),
			attr.es_flags, caller);
	}

	/* Update the route entry. */
	route_change = update_evpn_route_entry(
		bgp, vpn, afi, safi, dest, &attr, NULL /* mac */, NULL /* ip */,
		0, &pi, 0, seq, true /* setup_sync */, &old_is_sync);

	assert(pi);
	attr_new = pi->attr;
	/* lock ri to prevent freeing in evpn_route_select_install */
	bgp_path_info_lock(pi);

	/* Perform route selection. Normally, the local route in the
	 * VNI is expected to win and be the best route. However,
	 * under peculiar situations (e.g., tunnel (next hop) IP change
	 * that causes best selection to be based on next hop), a
	 * remote route could win. If the local route is the best,
	 * ensure it is updated in the global EVPN route table and
	 * advertised to peers; otherwise, ensure it is evicted and
	 * (re)install the remote route into zebra.
	 */
	evpn_route_select_install(bgp, vpn, dest, pi);

	if (CHECK_FLAG(pi->flags, BGP_PATH_REMOVED)) {
		route_change = 0;
	} else {
		if (!CHECK_FLAG(pi->flags, BGP_PATH_SELECTED)) {
			route_change = 0;
			evpn_cleanup_local_non_best_route(bgp, vpn, dest, pi);
		} else {
			bool new_is_sync;

			/* If the local path already existed and is still the
			 * best path we need to also check if it transitioned
			 * from being a sync path to a non-sync path. If it
			 * it did we need to notify zebra that the sync-path
			 * has been removed.
			 */
			new_is_sync = bgp_evpn_attr_is_sync(pi->attr);
			if (!new_is_sync && old_is_sync) {
				if (CHECK_FLAG(bgp->flags,
					       BGP_FLAG_DELETE_IN_PROGRESS))
					(void)evpn_zebra_uninstall(bgp, vpn,
								   &evp, pi,
								   true);
				else
					bgp_zebra_route_install(dest, pi, bgp,
								false, vpn,
								true);
			}
		}
	}


	/* unlock pi */
	bgp_path_info_unlock(pi);

	if (route_change) {
		/* Update route in global routing table. */
		global_dest = bgp_evpn_global_node_get(
			bgp->rib[afi][safi], afi, safi, &evp, &vpn->prd, NULL);
		assert(global_dest);
		update_evpn_route_entry(
			bgp, vpn, afi, safi, global_dest, attr_new,
			NULL /* mac */, NULL /* ip */, 0, &global_pi, 0,
			mac_mobility_seqnum(attr_new), false /* setup_sync */,
			NULL /* old_is_sync */);

		/* Schedule for processing and unlock node. */
		bgp_process(bgp, global_dest, global_pi, afi, safi);
		bgp_dest_unlock_node(global_dest);
	}

	/* Unintern temporary. */
	aspath_unintern(&attr.aspath);
}

/*
 * update_type2_route -- 对 dest 上的一条本地 Type-2 路由执行刷新。
 *
 * 职责：仅处理 Type-2 前缀；取本地路径，存在则调用 bgp_evpn_update_type2_route_entry 刷新。
 * 参数：bgp / vpn / dest。
 * 返回：无。
 */
static void update_type2_route(struct bgp *bgp, struct bgpevpn *vpn,
			       struct bgp_dest *dest)
{
	struct bgp_path_info *tmp_pi;

	const struct prefix_evpn *evp =
		(const struct prefix_evpn *)bgp_dest_get_prefix(dest);

	if (evp->prefix.route_type != BGP_EVPN_MAC_IP_ROUTE)
		return;

	/* Identify local route. */
	tmp_pi = bgp_evpn_route_get_local_path(bgp, dest, 0);
	if (!tmp_pi)
		return;

	bgp_evpn_update_type2_route_entry(bgp, vpn, dest, tmp_pi, __func__);
}

/*
 * update_all_type2_routes -- 刷新某 VNI 下所有的本地 Type-2(MAC / IP)路由，使其向对端重新宣告。
 *
 * 职责：遍历 vpn->mac_table 与 vpn->ip_table，对每个节点调用 update_type2_route。
 * 参数：bgp / vpn。
 * 返回：无。
 * 触发场景：VNI 的 RMAC / L3-VNI / 属性等发生变化时，需要把已有 Type-2 路由全部重刷一遍。
 */
/*
 * Update all type-2 (MACIP) local routes for this VNI - these should also
 * be scheduled for advertise to peers.
 */
static void update_all_type2_routes(struct bgp *bgp, struct bgpevpn *vpn)
{
	struct bgp_dest *dest;

	/* Walk this VNI's route MAC & IP table and update local type-2
	 * routes. For any routes updated, update corresponding entry in the
	 * global table too.
	 */
	for (dest = bgp_table_top(vpn->mac_table); dest;
	     dest = bgp_route_next(dest))
		update_type2_route(bgp, vpn, dest);

	for (dest = bgp_table_top(vpn->ip_table); dest;
	     dest = bgp_route_next(dest))
		update_type2_route(bgp, vpn, dest);
}

/*
 * delete_global_type2_routes -- 删除某 VNI 在全局表中所有的本地 Type-2 路由并调度向对端撤销。
 *
 * 职责：按 vpn->prd 在全局表定位 RD 节点(RD 级)，取 RD 下的前缀级表，遍历其中 Type-2
 *       前缀，逐一 delete_evpn_route_entry 标记删除并 bgp_process 调度，最后解锁 RD 节点。
 * 参数：bgp / vpn。
 * 返回：无。
 * 依据：全局表为 RD 级 + 前缀级两级结构，需先定位 RD 节点再遍历前缀。
 */
/*
 * Delete all type-2 (MACIP) local routes for this VNI - only from the
 * global routing table. These are also scheduled for withdraw from peers.
 */
static void delete_global_type2_routes(struct bgp *bgp, struct bgpevpn *vpn)
{
	afi_t afi;
	safi_t safi;
	struct bgp_dest *rddest, *dest;
	struct bgp_table *table;
	struct bgp_path_info *pi;

	afi = AFI_L2VPN;
	safi = SAFI_EVPN;

	rddest = bgp_node_lookup(bgp->rib[afi][safi],
				 (struct prefix *)&vpn->prd);
	if (rddest) {
		table = bgp_dest_get_bgp_table_info(rddest);
		for (dest = bgp_table_top(table); dest;
		     dest = bgp_route_next(dest)) {
			const struct prefix_evpn *evp =
				(const struct prefix_evpn *)bgp_dest_get_prefix(
					dest);

			if (evp->prefix.route_type != BGP_EVPN_MAC_IP_ROUTE)
				continue;

			pi = delete_evpn_route_entry(bgp, afi, safi, dest, NULL, 0);
			if (pi)
				bgp_process(bgp, dest, pi, afi, safi);
		}

		/* Unlock RD node. */
		bgp_dest_unlock_node(rddest);
	}
}

/*
 * delete_vni_type2_route -- 删除 VNI 表中一条本地 Type-2 路由并立即回收，返回新的 dest。
 *
 * 职责：仅处理 Type-2 前缀；标记删除该本地路径后用 bgp_path_info_reap 立即回收
 *       (VNI 本地表可直接删)，返回 reap 后的 dest 供遍历继续使用。
 * 参数：bgp / dest。
 * 返回：处理后的 dest(非 Type-2 时原样返回)。
 */
static struct bgp_dest *delete_vni_type2_route(struct bgp *bgp,
					       struct bgp_dest *dest)
{
	struct bgp_path_info *pi;
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;

	const struct prefix_evpn *evp =
		(const struct prefix_evpn *)bgp_dest_get_prefix(dest);

	if (evp->prefix.route_type != BGP_EVPN_MAC_IP_ROUTE)
		return dest;

	pi = delete_evpn_route_entry(bgp, afi, safi, dest, NULL, 0);

	/* Route entry in local table gets deleted immediately. */
	if (pi)
		dest = bgp_path_info_reap(dest, pi);

	return dest;
}

/*
 * delete_vni_type2_routes -- 删除某 VNI 的 MAC 表和 IP 表中所有本地 Type-2 路由。
 *
 * 职责：分别遍历 vpn->mac_table 与 vpn->ip_table，对每个节点调用 delete_vni_type2_route；
 *       由于 reap 会改变遍历节点，故用返回值更新 dest。
 * 参数：bgp / vpn。
 * 返回：无。
 */
static void delete_vni_type2_routes(struct bgp *bgp, struct bgpevpn *vpn)
{
	struct bgp_dest *dest;

	/* Next, walk this VNI's MAC & IP route table and delete local type-2
	 * routes.
	 */
	for (dest = bgp_table_top(vpn->mac_table); dest;
	     dest = bgp_route_next(dest)) {
		dest = delete_vni_type2_route(bgp, dest);
		assert(dest);
	}

	for (dest = bgp_table_top(vpn->ip_table); dest;
	     dest = bgp_route_next(dest)) {
		dest = delete_vni_type2_route(bgp, dest);
		assert(dest);
	}
}

/*
 * delete_all_type2_routes -- 删除某 VNI 的所有本地 Type-2 路由(全局表 + VNI 表)。
 *
 * 职责：先 delete_global_type2_routes 处理全局表(会向对端撤销)，再 delete_vni_type2_routes
 *       处理 VNI 本地表。
 * 参数：bgp / vpn。
 * 返回：无。
 */
/*
 * Delete all type-2 (MACIP) local routes for this VNI - from the global
 * table as well as the per-VNI route table.
 */
static void delete_all_type2_routes(struct bgp *bgp, struct bgpevpn *vpn)
{
	/* First, walk the global route table for this VNI's type-2 local
	 * routes.
	 * EVPN routes are a 2-level table, first get the RD table.
	 */
	delete_global_type2_routes(bgp, vpn);
	delete_vni_type2_routes(bgp, vpn);
}

/*
 * delete_all_vni_routes -- 删除某 VNI 的 MAC / IP 表中所有路由(含远端路由)。
 *
 * 职责：遍历 mac_table 与 ip_table，对每个路径：先 bgp_evpn_remote_ip_hash_del(仅 MAC 表)
 *       从远端 IP 哈希中移除，再 mark_for_delete 并 reap；由于 reap 改变遍历，故用 nextpi
 *       预先记录后继、dest 用返回值更新。
 * 参数：bgp / vpn。
 * 返回：无。
 * 依据：MAC 表侧需要清理 remote_ip_hash 引用，IP 表侧不需要。
 */
/*
 * Delete all routes in the per-VNI route table.
 */
static void delete_all_vni_routes(struct bgp *bgp, struct bgpevpn *vpn)
{
	struct bgp_dest *dest;
	struct bgp_path_info *pi, *nextpi;

	/* Walk this VNI's MAC & IP route table and delete all routes. */
	for (dest = bgp_table_top(vpn->mac_table); dest;
	     dest = bgp_route_next(dest)) {
		for (pi = bgp_dest_get_bgp_path_info(dest);
		     (pi != NULL) && (nextpi = pi->next, 1); pi = nextpi) {
			bgp_evpn_remote_ip_hash_del(vpn, pi);
			bgp_path_info_mark_for_delete(dest, pi);
			dest = bgp_path_info_reap(dest, pi);

			assert(dest);
		}
	}

	for (dest = bgp_table_top(vpn->ip_table); dest;
	     dest = bgp_route_next(dest)) {
		for (pi = bgp_dest_get_bgp_path_info(dest);
		     (pi != NULL) && (nextpi = pi->next, 1); pi = nextpi) {
			bgp_path_info_mark_for_delete(dest, pi);
			dest = bgp_path_info_reap(dest, pi);

			assert(dest);
		}
	}
}

/*
 * bgp_evpn_vni_flood_mode_get -- 计算某 VNI 生效的 BUM 泛洪模式。
 *
 * 规则(按优先级)：
 *   1) 若 VNI 自设泛洪模式且与全局不同，采用 VNI 模式；
 *   2) 若全局泛洪被关闭(VXLAN_FLOOD_DISABLED)，返回关闭；
 *   3) 若配置了组播组地址(mcast_grp 非 0)，采用 PIM-SM；
 *   4) 否则默认为头端复制(HEAD_END_REPL)。
 * 参数：bgp(全局泛洪模式 bgp->vxlan_flood_ctrl) / vpn(VNI 泛洪模式 vpn->vxlan_flood_ctrl、
 *       组播组 vpn->mcast_grp)。
 * 返回：生效的 VXLAN_FLOOD_* 模式。
 */
/* BUM traffic flood mode per-l2-vni */
static int bgp_evpn_vni_flood_mode_get(struct bgp *bgp,
					struct bgpevpn *vpn)
{
	if (bgp_debug_zebra(NULL))
		zlog_debug("VRF %s vni %u flood mode %d (global flood mode %d)",
			   vpn->bgp_vrf ? vrf_id_to_name(vpn->bgp_vrf->vrf_id) : "UNKNOWN",
			   vpn->vni, vpn->vxlan_flood_ctrl, bgp->vxlan_flood_ctrl);

	/* If per-VNI flood mode is set and differs from global mode,
	 * use per-VNI mode.
	 */
	if (vpn->vxlan_flood_ctrl != VXLAN_FLOOD_INHERIT_GLOBAL &&
	    vpn->vxlan_flood_ctrl != bgp->vxlan_flood_ctrl)
		return vpn->vxlan_flood_ctrl;

	/* if flooding has been globally disabled per-vni mode is
	 * not relevant
	 */
	if (bgp->vxlan_flood_ctrl == VXLAN_FLOOD_DISABLED)
		return VXLAN_FLOOD_DISABLED;

	/* if mcast group ip has been specified we use a PIM-SM MDT */
	if (vpn->mcast_grp.s_addr != INADDR_ANY)
		return VXLAN_FLOOD_PIM_SM;

	/* default is ingress replication */
	return VXLAN_FLOOD_HEAD_END_REPL;
}

/*
 * update_routes_for_vni -- 更新并重新宣告某 VNI 的本地路由。
 *
 * 触发：VNI 的 export RT 变化或隧道 IP(tunnel IP)变化时调用；这类场景需要同时更新
 *       VNI 表与全局表中的路由(因属性变化)。
 * 职责：1) update_type1_routes_for_evi 刷新 Type-1(EAD)路由；
 *       2) 若生效泛洪模式为头端复制(HEAD_END_REPL)，构建并 update_evpn_route 通告
 *          Type-3(IMET)路由(每个 VNI 仅一条)；
 *       3) update_all_type2_routes 刷新全部本地 Type-2(MAC-IP)路由。
 * 参数：bgp / vpn。
 * 返回：0 或 update_evpn_route 的返回值。
 */
/*
 * Update (and advertise) local routes for a VNI. Invoked upon the VNI
 * export RT getting modified or change to tunnel IP. Note that these
 * situations need the route in the per-VNI table as well as the global
 * table to be updated (as attributes change).
 */
int update_routes_for_vni(struct bgp *bgp, struct bgpevpn *vpn)
{
	int ret;
	struct prefix_evpn p;

	update_type1_routes_for_evi(bgp, vpn);

	/* Update and advertise the type-3 route (only one) followed by the
	 * locally learnt type-2 routes (MACIP) - for this VNI.
	 *
	 * RT-3 only if doing head-end replication
	 */
	if (bgp_evpn_vni_flood_mode_get(bgp, vpn)
				== VXLAN_FLOOD_HEAD_END_REPL) {
		build_evpn_type3_prefix(&p, &vpn->originator_ip);
		ret = update_evpn_route(bgp, vpn, &p, 0, 0, NULL);
		if (ret)
			return ret;
	}

	update_all_type2_routes(bgp, vpn);
	return 0;
}

/*
 * update_routes_for_vni_hash -- hash_iterate 回调：对哈希桶中的 VNI 执行 update_routes_for_vni。
 *
 * 参数：bucket(hash_bucket) / bgp。
 * 返回：无。
 * 用途：对 bgp->vni_hash 中所有 VNI 批量刷新路由。
 */
/* Update Type-2/3 Routes for L2VNI.
 * Called by hash_iterate()
 */
static void update_routes_for_vni_hash(struct hash_bucket *bucket,
				       struct bgp *bgp)
{
	struct bgpevpn *vpn;

	if (!bucket)
		return;

	vpn = (struct bgpevpn *)bucket->data;
	update_routes_for_vni(bgp, vpn);
}

/*
 * delete_routes_for_vni -- 删除并撤销某 VNI 的本地路由(全局表 + VNI 表)，另清空 VNI 表其余路由。
 *
 * 触发：VNI 被删除或 EVPN(advertise-all-vni)被关闭时。
 * 职责：1) delete_all_type2_routes 删除并撤销本地 Type-2 路由；
 *       2) 构建 Type-3 前缀，置 BGP_FLAG_VNI_DOWN 后 delete_evpn_route 删除 Type-3，再清标志；
 *       3) delete_all_vni_routes 清空 VNI 表其余路由。
 * 参数：bgp / vpn。
 * 返回：0 或 delete_evpn_route 的返回值。
 * 说明：注释描述的是 zebra 侧新前缀 FIFO 列表尚未下发时 VNI 下线的场景，需要立即向
 *       zebra 撤销并弹出 za_vpn 指向该 VNI 的 dest，避免悬空引用。
 */
/*
 * Delete (and withdraw) local routes for specified VNI from the global
 * table and per-VNI table. After this, remove all other routes from
 * the per-VNI table. Invoked upon the VNI being deleted or EVPN
 * (advertise-all-vni) being disabled.
 */
static int delete_routes_for_vni(struct bgp *bgp, struct bgpevpn *vpn)
{
	int ret;
	struct prefix_evpn p;

	/* Delete and withdraw locally learnt type-2 routes (MACIP)
	 * followed by type-3 routes (only one) - for this VNI.
	 */
	delete_all_type2_routes(bgp, vpn);

	build_evpn_type3_prefix(&p, &vpn->originator_ip);

	/*
	 * To handle the following scenario:
	 *  - Say, the new zebra announce fifo list has few vni Evpn prefixes yet
	 *    to be sent to zebra.
	 *  - At this point if we have triggers like "no advertise-all-vni" or
	 *    "networking restart", where a vni is going down.
	 *
	 * Perform the below
	 *    1) send withdraw routes to zebra immediately in case it is installed.
	 *    2) before we blow up the vni table, we need to walk the list and
	 *       pop all the dest whose za_vpn points to this vni.
	 */
	SET_FLAG(bgp->flags, BGP_FLAG_VNI_DOWN);
	ret = delete_evpn_route(bgp, vpn, &p);
	UNSET_FLAG(bgp->flags, BGP_FLAG_VNI_DOWN);
	if (ret)
		return ret;

	/* Delete all routes from the per-VNI table. */
	delete_all_vni_routes(bgp, vpn);
	return 0;
}

/*
 * bgp_evpn_mcast_grp_change -- 处理某 VNI 的 BUM 组播地址变化。
 *
 * 职责：更新 vpn->mcast_grp；若 VNI 活跃(is_vni_live)则构建 Type-3 前缀并删除原 Type-3 路由。
 * 说明：新 Type-3 会在隧道 IP 更新后按新的泛洪模式(若为头端复制)重新生成。
 * 参数：bgp / vpn / mcast_grp(新组播组)。
 * 返回：恒为 0。
 */
/*
 * There is a flood mcast IP address change. Update the mcast-grp and
 * remove the type-3 route if any. A new type-3 route will be generated
 * post tunnel_ip update if the new flood mode is head-end-replication.
 */
static int bgp_evpn_mcast_grp_change(struct bgp *bgp, struct bgpevpn *vpn,
		struct in_addr mcast_grp)
{
	struct prefix_evpn p;

	vpn->mcast_grp = mcast_grp;

	if (is_vni_live(vpn)) {
		build_evpn_type3_prefix(&p, &vpn->originator_ip);
		delete_evpn_route(bgp, vpn, &p);
	}

	return 0;
}

/*
 * handle_tunnel_ip_change -- 处理 VNI / VRF 的隧道端点 IP(VTEP-IP)变化。
 *
 * 职责：1) 依 L3VNI(bgp_vrf 非空用 vrf->originator_ip)或 L2VNI 取旧 VTEP IP；相同则返回；
 *       2) L2VNI 且非活跃时只更新 originator_ip 后返回(L3VNI 立即更新，不提前返回)；
 *       3) 更新 tunnel-ip 哈希：bgp_tip_del 旧 IP，bgp_tip_add 新 IP；若新 IP 新增成功则
 *          按新的 martian 下一跳重新过滤 EVPN 路由(bgp_filter_evpn_routes_upon_martian_change)；
 *       4) L2VNI：构建 Type-3 前缀并删除旧 Type-3(因 originator IP 是 key 的一部分)，
 *          再更新 originator_ip；L3VNI：直接更新 vrf->originator_ip。
 * 参数：bgp_vrf(可空，L3VNI 时非空) / bgp_evpn / vpn / originator_ip(新 IP)。
 * 返回：无。
 * 说明：路由的重新宣告在其他流程中完成。
 */
/*
 * If there is a tunnel endpoint IP address (VTEP-IP) change for this VNI.
     - Deletes tip_hash entry for old VTEP-IP
     - Adds tip_hash entry/refcount for new VTEP-IP
     - Deletes prior type-3 route for L2VNI (if needed)
     - Updates originator_ip
 * Note: Route re-advertisement happens elsewhere after other processing
 * other changes.
 */
static void handle_tunnel_ip_change(struct bgp *bgp_vrf, struct bgp *bgp_evpn,
				    struct bgpevpn *vpn,
				    struct ipaddr *originator_ip)
{
	struct prefix_evpn p;
	struct ipaddr old_vtep_ip;

	if (bgp_vrf) /* L3VNI */
		old_vtep_ip = bgp_vrf->originator_ip;
	else /* L2VNI */
		old_vtep_ip = vpn->originator_ip;

	/* TIP didn't change, nothing to do */
	if (ipaddr_is_same(&old_vtep_ip, originator_ip))
		return;

	/* If L2VNI is not live, we only need to update the originator_ip.
	 * L3VNIs are updated immediately, so we can't bail out early.
	 */
	if (!bgp_vrf && !is_vni_live(vpn)) {
		vpn->originator_ip = *originator_ip;
		return;
	}

	/* Update the tunnel-ip hash */
	bgp_tip_del(bgp_evpn, &old_vtep_ip);
	if (bgp_tip_add(bgp_evpn, originator_ip))
		/* The originator_ip was not already present in the
		 * bgp martian next-hop table as a tunnel-ip, so we
		 * need to go back and filter routes matching the new
		 * martian next-hop.
		 */
		bgp_filter_evpn_routes_upon_martian_change(bgp_evpn,
							   BGP_MARTIAN_TUN_IP);

	if (!bgp_vrf) {
		/* Need to withdraw type-3 route as the originator IP is part
		 * of the key.
		 */
		build_evpn_type3_prefix(&p, &vpn->originator_ip);
		delete_evpn_route(bgp_evpn, vpn, &p);

		vpn->originator_ip = *originator_ip;
	} else
		bgp_vrf->originator_ip = *originator_ip;

	return;
}

/*
 * bgp_create_evpn_bgp_path_info -- 为 EVPN 路由导入到 VRF 而创建一条 BGP_ROUTE_IMPORTED 路径。
 *
 * 职责：intern 属性；用 info_make 创建 BGP_ROUTE_IMPORTED 路径(peer / type 继承 parent_pi)，
 *       置 VALID 并分配 vrfleak 扩展；把 parent_pi 记录在 extra->vrfleak->parent(并 lock
 *       parent 及其 net 节点)；继承 igpmetric 与 MPLS 标签(若有)；把路径加入 dest。
 * 参数：parent_pi(来源 EVPN 路径) / dest(VRF 表节点) / attr。
 * 返回：新建的 bgp_path_info。
 * 依据：vrfleak->parent 用于后续在 VRF 表中反查来源路径。
 */
static struct bgp_path_info *
bgp_create_evpn_bgp_path_info(struct bgp_path_info *parent_pi,
			      struct bgp_dest *dest, struct attr *attr)
{
	struct attr *attr_new;
	struct bgp_path_info *pi;

	/* Add (or update) attribute to hash. */
	attr_new = bgp_attr_intern(attr);

	/* Create new route with its attribute. */
	pi = info_make(parent_pi->type, BGP_ROUTE_IMPORTED, 0, parent_pi->peer,
		       attr_new, dest);
	SET_FLAG(pi->flags, BGP_PATH_VALID);
	bgp_path_info_extra_get(pi);
	if (!pi->extra->vrfleak)
		pi->extra->vrfleak =
			XCALLOC(MTYPE_BGP_ROUTE_EXTRA_VRFLEAK,
				sizeof(struct bgp_path_info_extra_vrfleak));
	pi->extra->vrfleak->parent = bgp_path_info_lock(parent_pi);
	bgp_dest_lock_node((struct bgp_dest *)parent_pi->net);
	if (parent_pi->extra)
		pi->extra->igpmetric = parent_pi->extra->igpmetric;

	if (BGP_PATH_INFO_NUM_LABELS(parent_pi))
		pi->extra->labels = bgp_labels_intern(parent_pi->extra->labels);

	bgp_path_info_add(dest, pi);

	return pi;
}

/*
 * bgp_evpn_filter_ecommunity -- ecommunity_filter 回调：从 EVPN 导入 VRF 的路由中剥离特定扩展团体。
 *
 * 依据 draft-ietf-bess-evpn-ipvpn-interworking-13，需剥离：
 *   a. BGP Encapsulation 扩展团体；
 *   b. Route Target 扩展团体；
 *   c. 所有 EVPN 类型的扩展团体。
 * 返回：true 表示保留该团体，false 表示剥离。
 * 实现：按扩展团体的类型 / 子类型判断——AS / IP / AS4 编码且子类型为 RT 的返回 false；
 *       OPAQUE 编码且子类型为 ENCAP 的返回 false；EVPN 编码的一律返回 false。
 */
/*
 * According to draft-ietf-bess-evpn-ipvpn-interworking-13, strip the following
 * extended communities for VRF routes imported from EVPN.
 *
 *   a. BGP Encapsulation extended communities.
 *   b. Route Target extended communities.
 *   c. All the extended communities of type EVPN.
 */
static bool bgp_evpn_filter_ecommunity(uint8_t *val, uint8_t size, void *arg)
{
	switch (val[0]) {
	case ECOMMUNITY_ENCODE_AS:
	case ECOMMUNITY_ENCODE_IP:
	case ECOMMUNITY_ENCODE_AS4:
		if (val[1] == ECOMMUNITY_ROUTE_TARGET)
			return false;
		break;
	case ECOMMUNITY_ENCODE_OPAQUE:
		if (val[1] == ECOMMUNITY_OPAQUE_SUBTYPE_ENCAP)
			return false;
		break;
	case ECOMMUNITY_ENCODE_EVPN:
		return false;
	}
	return true;
}

/*
 * install_evpn_route_entry_in_vrf -- 把一条 EVPN 路由导入安装到 VRF 路由表并触发选路。
 *
 * 职责：1) 由 EVPN 前缀还原出普通前缀(ip_prefix_from_evpn_prefix)；
 *       2) 按 IPv4 / IPv6 选择 VRF 的 AFI / SAFI 并取 / 建 dest(无 RD)；
 *       3) 由 parent_pi 复制属性并处理下一跳：若带 gateway-ip overlay，则用 gw_ip 作下一跳
 *          (GW IP 特性)；否则 IPv6 走 evpn_convert_nexthop_to_ipv6，IPv4 用 mp_nexthop_global_in；
 *       4) 按 ES L3-NHG 配置置 ATTR_ES_L3_NHG_USE / ACTIVE；
 *       5) 用 bgp_evpn_filter_ecommunity 过滤扩展团体；
 *       6) 查找已有的对应导入路径(vrfleak->parent == parent_pi)：无则新建；有且属性未变
 *          则直接返回；属性变化则 re-intern、restore、标记 IGP_CHANGED、置 ATTR_CHANGED、
 *          替换属性并刷新 uptime；
 *       7) gateway-ip 下一跳需解析(bgp_find_or_add_nexthop 决定 VALID)，否则按导入设置
 *          ANNC_NH_SELF；关联 EVPN 下一跳、聚合计数、bgp_process 选路、vpn_leak 泄漏处理，
 *          最后解锁 dest。
 * 参数：bgp_vrf / evp(EVPN 前缀) / parent_pi。
 * 返回：0 或 -1(VRF 未知)。
 * 依据：EVPN 路由下一跳只有 IPv4(远端 VTEP)，IPv6 主机 / 前缀需转成 IPv4-mapped 以复用后续逻辑。
 */
/*
 * Install route entry into the VRF routing table and invoke route selection.
 */
static int install_evpn_route_entry_in_vrf(struct bgp *bgp_vrf,
					   const struct prefix_evpn *evp,
					   struct bgp_path_info *parent_pi)
{
	struct bgp_dest *dest;
	struct bgp_path_info *pi;
	struct attr attr;
	struct attr *attr_new;
	int ret = 0;
	struct prefix p;
	struct prefix *pp = &p;
	afi_t afi = 0;
	safi_t safi = 0;
	bool new_pi = false;
	bool use_l3nhg = false;
	bool is_l3nhg_active = false;
	char buf1[INET6_ADDRSTRLEN];
	struct bgp_route_evpn *bre;
	struct ecommunity *ecom;

	memset(pp, 0, sizeof(struct prefix));
	ip_prefix_from_evpn_prefix(evp, pp);

	if (bgp_debug_zebra(NULL))
		zlog_debug(
			"vrf %s: import evpn prefix %pFX parent %p flags 0x%x",
			vrf_id_to_name(bgp_vrf->vrf_id), evp, parent_pi,
			parent_pi->flags);

	if (bgp_vrf->vrf_id == VRF_UNKNOWN)
		return -1;

	/* Create (or fetch) route within the VRF. */
	/* NOTE: There is no RD here. */
	if (is_evpn_prefix_ipaddr_v4(evp)) {
		afi = AFI_IP;
		safi = SAFI_UNICAST;
		dest = bgp_node_get(bgp_vrf->rib[afi][safi], pp);
	} else if (is_evpn_prefix_ipaddr_v6(evp)) {
		afi = AFI_IP6;
		safi = SAFI_UNICAST;
		dest = bgp_node_get(bgp_vrf->rib[afi][safi], pp);
	} else
		return 0;

	/* EVPN routes currently only support a IPv4 next hop which corresponds
	 * to the remote VTEP. When importing into a VRF, if it is IPv6 host
	 * or prefix route, we have to convert the next hop to an IPv4-mapped
	 * address for the rest of the code to flow through. In the case of IPv4,
	 * make sure to set the flag for next hop attribute.
	 */
	attr = *parent_pi->attr;
	bre = bgp_attr_get_evpn_overlay(&attr);
	if (bre && bre->type == OVERLAY_INDEX_GATEWAY_IP) {
		/*
		 * If gateway IP overlay index is specified in the NLRI of
		 * EVPN RT-5, this gateway IP should be used as the nexthop
		 * for the prefix in the VRF
		 */
		if (bgp_debug_zebra(NULL)) {
			zlog_debug("Install gateway IP %s as nexthop for prefix %pFX in vrf %s",
				   inet_ntop(pp->family, &bre->gw_ip, buf1,
					     sizeof(buf1)),
				   pp, vrf_id_to_name(bgp_vrf->vrf_id));
		}

		if (afi == AFI_IP6) {
			memcpy(&attr.mp_nexthop_global, &bre->gw_ip.ipaddr_v6,
			       sizeof(struct in6_addr));
			attr.mp_nexthop_len = IPV6_MAX_BYTELEN;
		} else {
			attr.nexthop = bre->gw_ip.ipaddr_v4;
			SET_FLAG(attr.flag, ATTR_FLAG_BIT(BGP_ATTR_NEXT_HOP));
		}
	} else {
		if (afi == AFI_IP6)
			evpn_convert_nexthop_to_ipv6(&attr);
		else {
			attr.nexthop = attr.mp_nexthop_global_in;
			SET_FLAG(attr.flag, ATTR_FLAG_BIT(BGP_ATTR_NEXT_HOP));
		}
	}

	bgp_evpn_es_vrf_use_nhg(bgp_vrf, &parent_pi->attr->esi, &use_l3nhg,
				&is_l3nhg_active, NULL);
	if (use_l3nhg)
		SET_FLAG(attr.es_flags, ATTR_ES_L3_NHG_USE);
	if (is_l3nhg_active)
		SET_FLAG(attr.es_flags, ATTR_ES_L3_NHG_ACTIVE);

	ecom = ecommunity_filter(bgp_attr_get_ecommunity(&attr), bgp_evpn_filter_ecommunity, NULL);
	bgp_attr_set_ecommunity(&attr, ecom);

	/* Check if route entry is already present. */
	for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next)
		if (pi->extra && pi->extra->vrfleak &&
		    (struct bgp_path_info *)pi->extra->vrfleak->parent ==
			    parent_pi)
			break;

	if (!pi) {
		pi = bgp_create_evpn_bgp_path_info(parent_pi, dest, &attr);
		new_pi = true;
	} else {
		if (!CHECK_FLAG(pi->flags, BGP_PATH_REMOVED) && attrhash_cmp(pi->attr, &attr)) {
			bgp_dest_unlock_node(dest);
			return 0;
		}
		/* The attribute has changed. */
		/* Add (or update) attribute to hash. */
		attr_new = bgp_attr_intern(&attr);

		/* Restore route, if needed. */
		if (CHECK_FLAG(pi->flags, BGP_PATH_REMOVED))
			bgp_path_info_restore(dest, pi);

		/* Mark if nexthop has changed. */
		if ((afi == AFI_IP
		     && !IPV4_ADDR_SAME(&pi->attr->nexthop, &attr_new->nexthop))
		    || (afi == AFI_IP6
			&& !IPV6_ADDR_SAME(&pi->attr->mp_nexthop_global,
					   &attr_new->mp_nexthop_global)))
			SET_FLAG(pi->flags, BGP_PATH_IGP_CHANGED);

		bgp_path_info_set_flag(dest, pi, BGP_PATH_ATTR_CHANGED);
		/* Unintern existing, set to new. */
		bgp_attr_unintern(&pi->attr);
		pi->attr = attr_new;
		pi->uptime = monotime(NULL);
	}

	bgp_dest_set_defer_flag(dest, false);

	/* Gateway IP nexthop should be resolved */
	if (bre && bre->type == OVERLAY_INDEX_GATEWAY_IP) {
		if (bgp_find_or_add_nexthop(bgp_vrf, bgp_vrf, afi, safi, pi, NULL, 0, NULL, NULL))
			bgp_path_info_set_flag(dest, pi, BGP_PATH_VALID);
		else {
			if (BGP_DEBUG(nht, NHT)) {
				inet_ntop(pp->family, &bre->gw_ip, buf1,
					  sizeof(buf1));
				zlog_debug("%s: gateway IP NH unresolved",
					   buf1);
			}
			bgp_path_info_unset_flag(dest, pi, BGP_PATH_VALID);
		}
	} else {
		/* as it is an importation, change nexthop */
		bgp_path_info_set_flag(dest, pi, BGP_PATH_ANNC_NH_SELF);
	}

	/* Link path to evpn nexthop */
	bgp_evpn_path_nh_add(bgp_vrf, pi);

	bgp_aggregate_increment(bgp_vrf, bgp_dest_get_prefix(dest), pi, afi,
				safi);

	/* Perform route selection and update zebra, if required. */
	bgp_process(bgp_vrf, dest, pi, afi, safi);

	/* Process for route leaking. */
	vpn_leak_from_vrf_update(bgp_get_default(), bgp_vrf, pi);

	if (bgp_debug_zebra(NULL)) {
		struct ipaddr nhip = {};

		if (pi->net->rn->p.family == AF_INET6) {
			SET_IPADDR_V6(&nhip);
			IPV6_ADDR_COPY(&nhip.ipaddr_v6, &pi->attr->mp_nexthop_global);
		} else {
			SET_IPADDR_V4(&nhip);
			IPV4_ADDR_COPY(&nhip.ipaddr_v4, &pi->attr->nexthop);
		}
		zlog_debug("... %s pi %s dest %p (l %d) pi %p (l %d, f 0x%x) nh %pIA",
			   new_pi ? "new" : "update",
			   bgp_vrf->name_pretty, dest,
			   bgp_dest_get_lock_count(dest), pi, pi->lock,
			   pi->flags, &nhip);
	}

	bgp_dest_unlock_node(dest);

	return ret;
}

/*
 * install_evpn_route_entry_in_vni_common -- VNI 路由表(MAC 表 / IP 表)安装与选路的公共处理。
 *
 * 职责：在 dest 上查找由 parent_pi 导入的路径(vrfleak->parent == parent_pi)：
 *       - 不存在：用 bgp_create_evpn_bgp_path_info 新建，并按 Type-2 前缀补 MAC 或 IP 附加
 *         信息(is_evpn_type2_dest_ipaddr_none 决定补 IP 还是 MAC)；
 *       - 已存在：属性未变且非 REMOVED 则直接返回(由调用者解锁 dest)；否则 re-intern、
 *         restore、比较下一跳(mp_nexthop_len / IPv6 global 与 link-local / IPv4 两个位置)
 *         标记 IGP_CHANGED；若本地 ES 状态变化或 ESI 变化则置 ATTR_CHANGED；最后替换属性并
 *         刷新 uptime。
 *       随后清 dest 的 defer 标志，把路径加入远端 IP 哈希，调用 evpn_route_select_install
 *       选路；若最佳是带 ES 的本地路径，则在远端路径增删后更新其 sync 信息
 *       (bgp_evpn_update_type2_route_entry)。
 * 参数：bgp / vpn / p / dest / parent_pi。
 * 返回：evpn_route_select_install 的返回值(或属性未变时的 0)。
 * 依据：dest 的解锁由上层 install_evpn_route_entry_in_vni_mac / _ip 负责。
 */
/*
 * Common handling for vni route tables install/selection.
 */
static int install_evpn_route_entry_in_vni_common(
	struct bgp *bgp, struct bgpevpn *vpn, const struct prefix_evpn *p,
	struct bgp_dest *dest, struct bgp_path_info *parent_pi)
{
	struct bgp_path_info *pi;
	struct bgp_path_info *local_pi;
	struct attr *attr_new;
	int ret;
	bool old_local_es = false;
	bool new_local_es;

	/* Check if route entry is already present. */
	for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next)
		if (pi->extra && pi->extra->vrfleak &&
		    (struct bgp_path_info *)pi->extra->vrfleak->parent ==
			    parent_pi)
			break;

	if (!pi) {
		/* Create an info */
		pi = bgp_create_evpn_bgp_path_info(parent_pi, dest,
						    parent_pi->attr);

		if (p->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) {
			if (is_evpn_type2_dest_ipaddr_none(dest))
				evpn_type2_path_info_set_ip(
					pi, p->prefix.macip_addr.ip);
			else
				evpn_type2_path_info_set_mac(
					pi, p->prefix.macip_addr.mac);
		}

		new_local_es = bgp_evpn_attr_is_local_es(pi->attr);
	} else {
		/* Return early if attributes haven't changed
		 * and dest isn't flagged for removal.
		 * dest will be unlocked by either
		 * install_evpn_route_entry_in_vni_mac() or
		 * install_evpn_route_entry_in_vni_ip()
		 */
		if (!CHECK_FLAG(pi->flags, BGP_PATH_REMOVED) &&
		    attrhash_cmp(pi->attr, parent_pi->attr))
			return 0;
		/* The attribute has changed. */
		/* Add (or update) attribute to hash. */
		attr_new = bgp_attr_intern(parent_pi->attr);

		/* Restore route, if needed. */
		if (CHECK_FLAG(pi->flags, BGP_PATH_REMOVED))
			bgp_path_info_restore(dest, pi);

		/* Mark if nexthop has changed. */
		if (pi->attr->mp_nexthop_len != attr_new->mp_nexthop_len) {
			/* Nexthop address family changed */
			SET_FLAG(pi->flags, BGP_PATH_IGP_CHANGED);
		} else if (BGP_ATTR_MP_NEXTHOP_LEN_IP6(pi->attr)) {
			/* IPv6 nexthop */
			if (!IPV6_ADDR_SAME(&pi->attr->mp_nexthop_global,
					    &attr_new->mp_nexthop_global))
				SET_FLAG(pi->flags, BGP_PATH_IGP_CHANGED);
			/* Also check link-local nexthop if present */
			else if ((pi->attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL ||
				  pi->attr->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV6_GLOBAL_AND_LL) &&
				 !IPV6_ADDR_SAME(&pi->attr->mp_nexthop_local,
						 &attr_new->mp_nexthop_local))
				SET_FLAG(pi->flags, BGP_PATH_IGP_CHANGED);
		} else if (pi->attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV4 ||
			   pi->attr->mp_nexthop_len == BGP_ATTR_NHLEN_VPNV4) {
			/* IPv4 nexthop in mp_nexthop_global_in */
			if (!IPV4_ADDR_SAME(&pi->attr->mp_nexthop_global_in,
					    &attr_new->mp_nexthop_global_in))
				SET_FLAG(pi->flags, BGP_PATH_IGP_CHANGED);
		} else {
			/* IPv4 nexthop in nexthop field */
			if (!IPV4_ADDR_SAME(&pi->attr->nexthop, &attr_new->nexthop))
				SET_FLAG(pi->flags, BGP_PATH_IGP_CHANGED);
		}

		old_local_es = bgp_evpn_attr_is_local_es(pi->attr);
		new_local_es = bgp_evpn_attr_is_local_es(attr_new);
		/* If ESI is different or if its type has changed we
		 * need to reinstall the path in zebra
		 */
		if ((old_local_es != new_local_es)
		    || memcmp(&pi->attr->esi, &attr_new->esi,
			      sizeof(attr_new->esi))) {

			if (BGP_DEBUG(evpn_mh, EVPN_MH_RT))
				zlog_debug("VNI %d path %pFX chg to %s es",
					   vpn->vni, &pi->net->rn->p,
					   new_local_es ? "local" : "non-local");
			bgp_path_info_set_flag(dest, pi, BGP_PATH_ATTR_CHANGED);
		}

		/* Unintern existing, set to new. */
		bgp_attr_unintern(&pi->attr);
		pi->attr = attr_new;
		pi->uptime = monotime(NULL);
	}

	bgp_dest_set_defer_flag(dest, false);

	/* Add this route to remote IP hashtable */
	bgp_evpn_remote_ip_hash_add(vpn, pi);

	/* Perform route selection and update zebra, if required. */
	ret = evpn_route_select_install(bgp, vpn, dest, pi);

	/* if the best path is a local path with a non-zero ES
	 * sync info against the local path may need to be updated
	 * when a remote path is added/updated (including changes
	 * from sync-path to remote-path)
	 */
	local_pi = bgp_evpn_route_get_local_path(bgp, dest, 0);
	if (local_pi && (old_local_es || new_local_es))
		bgp_evpn_update_type2_route_entry(bgp, vpn, dest, local_pi,
						  __func__);

	return ret;
}

/*
 * uninstall_evpn_route_entry_in_vni_common -- VNI 路由表卸载与选路的公共处理。
 *
 * 职责：在 dest 上查找由 parent_pi 导入的路径，未找到返回 0；否则从远端 IP 哈希删除该路径、
 *       标记删除，调用 evpn_route_select_install 选路(可能触发 zebra 撤销)；若最佳路径是带
 *       ES 的本地路径，则因远端路径被删需要刷新其 sync 信息
 *       (bgp_evpn_update_type2_route_entry)。
 * 参数：bgp / vpn / p / dest / parent_pi。
 * 返回：evpn_route_select_install 的返回值(或未找到时的 0)。
 */
/*
 * Common handling for vni route tables uninstall/selection.
 */
static int uninstall_evpn_route_entry_in_vni_common(
	struct bgp *bgp, struct bgpevpn *vpn, const struct prefix_evpn *p,
	struct bgp_dest *dest, struct bgp_path_info *parent_pi)
{
	struct bgp_path_info *pi;
	struct bgp_path_info *local_pi;
	int ret;

	/* Find matching route entry. */
	for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next)
		if (pi->extra && pi->extra->vrfleak &&
		    (struct bgp_path_info *)pi->extra->vrfleak->parent ==
			    parent_pi)
			break;

	if (!pi)
		return 0;

	bgp_evpn_remote_ip_hash_del(vpn, pi);

	/* Mark entry for deletion */
	bgp_path_info_mark_for_delete(dest, pi);

	/* Perform route selection and update zebra, if required. */
	ret = evpn_route_select_install(bgp, vpn, dest, pi);

	/* if the best path is a local path with a non-zero ES
	 * sync info against the local path may need to be updated
	 * when a remote path is deleted
	 */
	local_pi = bgp_evpn_route_get_local_path(bgp, dest, 0);
	if (local_pi && bgp_evpn_attr_is_local_es(local_pi->attr))
		bgp_evpn_update_type2_route_entry(bgp, vpn, dest, local_pi,
						  __func__);

	return ret;
}

/*
 * install_evpn_route_entry_in_vni_ip -- 把 EVPN 路由安装到 VNI 的 IP 表并选路。
 *
 * 职责：跳过纯 MAC 的 Type-2 路由；在 vpn->ip_table 取 / 建节点后调用
 *       install_evpn_route_entry_in_vni_common，最后解锁节点。
 * 参数：bgp / vpn / p / parent_pi。
 * 返回：安装 / 选路结果。
 */
/*
 * Install route entry into VNI IP table and invoke route selection.
 */
static int install_evpn_route_entry_in_vni_ip(struct bgp *bgp,
					      struct bgpevpn *vpn,
					      const struct prefix_evpn *p,
					      struct bgp_path_info *parent_pi)
{
	int ret;
	struct bgp_dest *dest;

	/* Ignore MAC Only Type-2 */
	if ((p->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) &&
	    (is_evpn_prefix_ipaddr_none(p) == true))
		return 0;

	/* Create (or fetch) route within the VNI IP table. */
	dest = bgp_evpn_vni_ip_node_get(vpn->ip_table, p, parent_pi);

	ret = install_evpn_route_entry_in_vni_common(bgp, vpn, p, dest,
						     parent_pi);

	bgp_dest_unlock_node(dest);

	return ret;
}

/*
 * install_evpn_route_entry_in_vni_mac -- 把 EVPN 路由安装到 VNI 的 MAC 表并选路。
 *
 * 职责：仅处理 Type-2 路由；在 vpn->mac_table 取 / 建节点后调用
 *       install_evpn_route_entry_in_vni_common，最后解锁节点。
 * 参数：bgp / vpn / p / parent_pi。
 * 返回：安装 / 选路结果。
 */
/*
 * Install route entry into VNI MAC table and invoke route selection.
 */
static int install_evpn_route_entry_in_vni_mac(struct bgp *bgp,
					       struct bgpevpn *vpn,
					       const struct prefix_evpn *p,
					       struct bgp_path_info *parent_pi)
{
	int ret;
	struct bgp_dest *dest;

	/* Only type-2 routes go into this table */
	if (p->prefix.route_type != BGP_EVPN_MAC_IP_ROUTE)
		return 0;

	/* Create (or fetch) route within the VNI MAC table. */
	dest = bgp_evpn_vni_mac_node_get(vpn->mac_table, p, parent_pi);

	ret = install_evpn_route_entry_in_vni_common(bgp, vpn, p, dest,
						     parent_pi);

	bgp_dest_unlock_node(dest);

	return ret;
}

/*
 * uninstall_evpn_route_entry_in_vni_ip -- 从 VNI 的 IP 表卸载 EVPN 路由并选路。
 *
 * 职责：跳过纯 MAC 的 Type-2 路由；在 vpn->ip_table 定位节点(无则返回)后调用
 *       uninstall_evpn_route_entry_in_vni_common，最后解锁节点。
 * 参数：bgp / vpn / p / parent_pi。
 * 返回：卸载 / 选路结果。
 */
/*
 * Uninstall route entry from VNI IP table and invoke route selection.
 */
static int uninstall_evpn_route_entry_in_vni_ip(struct bgp *bgp,
						struct bgpevpn *vpn,
						const struct prefix_evpn *p,
						struct bgp_path_info *parent_pi)
{
	int ret;
	struct bgp_dest *dest;

	/* Ignore MAC Only Type-2 */
	if ((p->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) &&
	    (is_evpn_prefix_ipaddr_none(p) == true))
		return 0;

	/* Locate route within the VNI IP table. */
	dest = bgp_evpn_vni_ip_node_lookup(vpn->ip_table, p, parent_pi);
	if (!dest)
		return 0;

	ret = uninstall_evpn_route_entry_in_vni_common(bgp, vpn, p, dest,
						       parent_pi);

	bgp_dest_unlock_node(dest);

	return ret;
}

/*
 * uninstall_evpn_route_entry_in_vni_mac -- 从 VNI 的 MAC 表卸载 EVPN 路由并选路。
 *
 * 职责：仅处理 Type-2 路由；在 vpn->mac_table 定位节点(无则返回)后调用
 *       uninstall_evpn_route_entry_in_vni_common，最后解锁节点。
 * 参数：bgp / vpn / p / parent_pi。
 * 返回：卸载 / 选路结果。
 * 注：函数上方英文注释写作 VNI IP table，但函数体使用的是 vpn->mac_table，阅读以函数体为准。
 */
/*
 * Uninstall route entry from VNI IP table and invoke route selection.
 */
static int
uninstall_evpn_route_entry_in_vni_mac(struct bgp *bgp, struct bgpevpn *vpn,
				      const struct prefix_evpn *p,
				      struct bgp_path_info *parent_pi)
{
	int ret;
	struct bgp_dest *dest;

	/* Only type-2 routes go into this table */
	if (p->prefix.route_type != BGP_EVPN_MAC_IP_ROUTE)
		return 0;

	/* Locate route within the VNI MAC table. */
	dest = bgp_evpn_vni_mac_node_lookup(vpn->mac_table, p, parent_pi);
	if (!dest)
		return 0;

	ret = uninstall_evpn_route_entry_in_vni_common(bgp, vpn, p, dest,
						       parent_pi);

	bgp_dest_unlock_node(dest);

	return ret;
}
/*
 * uninstall_evpn_route_entry_in_vrf -- 从 VRF 路由表卸载一条导入的 EVPN 路由并向 zebra 反映。
 *
 * 职责：1) 由 EVPN 前缀还原普通前缀；2) 按 IPv4 / IPv6 在 VRF 表中定位节点(无则返回)；
 *       3) 查找由 parent_pi 导入的路径(无则返回)；4) vpn_leak 撤销、聚合计数递减；
 *       5) 置 BGP_NODE_PROCESS_CLEAR 强制处理，标记删除，解除与 EVPN 下一跳的关联，
 *          bgp_process 选路并解锁节点。
 * 参数：bgp_vrf / evp / parent_pi。
 * 返回：0(ret 恒为 0)。
 */
/*
 * Uninstall route entry from the VRF routing table and send message
 * to zebra, if appropriate.
 */
int uninstall_evpn_route_entry_in_vrf(struct bgp *bgp_vrf, const struct prefix_evpn *evp,
				      struct bgp_path_info *parent_pi)
{
	struct bgp_dest *dest;
	struct bgp_path_info *pi;
	int ret = 0;
	struct prefix p;
	struct prefix *pp = &p;
	afi_t afi = 0;
	safi_t safi = 0;

	memset(pp, 0, sizeof(struct prefix));
	ip_prefix_from_evpn_prefix(evp, pp);

	if (bgp_debug_zebra(NULL))
		zlog_debug(
			"vrf %s: unimport evpn prefix %pFX parent %p flags 0x%x",
			vrf_id_to_name(bgp_vrf->vrf_id), evp, parent_pi,
			parent_pi->flags);

	/* Locate route within the VRF. */
	/* NOTE: There is no RD here. */
	if (is_evpn_prefix_ipaddr_v4(evp)) {
		afi = AFI_IP;
		safi = SAFI_UNICAST;
		dest = bgp_node_lookup(bgp_vrf->rib[afi][safi], pp);
	} else {
		afi = AFI_IP6;
		safi = SAFI_UNICAST;
		dest = bgp_node_lookup(bgp_vrf->rib[afi][safi], pp);
	}

	if (!dest)
		return 0;

	/* Find matching route entry. */
	for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next)
		if (pi->extra && pi->extra->vrfleak &&
		    (struct bgp_path_info *)pi->extra->vrfleak->parent ==
			    parent_pi)
			break;

	if (!pi) {
		bgp_dest_unlock_node(dest);
		return 0;
	}

	if (bgp_debug_zebra(NULL)) {
		struct ipaddr nhip = {};

		if (pi->net->rn->p.family == AF_INET6) {
			SET_IPADDR_V6(&nhip);
			IPV6_ADDR_COPY(&nhip.ipaddr_v6, &pi->attr->mp_nexthop_global);
		} else {
			SET_IPADDR_V4(&nhip);
			IPV4_ADDR_COPY(&nhip.ipaddr_v4, &pi->attr->nexthop);
		}

		zlog_debug("... delete pi %s dest %p (l %d) pi %p (l %d, f 0x%x) nh %pIA",
			   bgp_vrf->name_pretty, dest,
			   bgp_dest_get_lock_count(dest), pi, pi->lock,
			   pi->flags, &nhip);
	}

	/* Process for route leaking. */
	vpn_leak_from_vrf_withdraw(bgp_get_default(), bgp_vrf, pi);

	bgp_aggregate_decrement(bgp_vrf, bgp_dest_get_prefix(dest), pi, afi,
				safi);

	/* Force deletion */
	SET_FLAG(dest->flags, BGP_NODE_PROCESS_CLEAR);

	/* Mark entry for deletion */
	bgp_path_info_mark_for_delete(dest, pi);

	/* Unlink path to evpn nexthop */
	bgp_evpn_path_nh_del(bgp_vrf, pi);

	/* Perform route selection and update zebra, if required. */
	bgp_process(bgp_vrf, dest, pi, afi, safi);

	/* Unlock route node. */
	bgp_dest_unlock_node(dest);

	return ret;
}

/*
 * install_evpn_route_entry -- 把 EVPN 路由安装到 VNI 的 MAC 表与 IP 表。
 *
 * 职责：按顺序调用 install_evpn_route_entry_in_vni_mac 与 ..._in_vni_ip，
 *       任一步失败则记录 EC_BGP_EVPN_FAIL 并返回错误码。
 * 参数：bgp / vpn / p / parent_pi。
 * 返回：0 表示成功，非 0 表示失败。
 */
/*
 * Install route entry into the VNI routing tables.
 */
static int install_evpn_route_entry(struct bgp *bgp, struct bgpevpn *vpn,
				    const struct prefix_evpn *p,
				    struct bgp_path_info *parent_pi)
{
	int ret = 0;
	char prefix_str[PREFIX2STR_BUFFER] = { 0 };
	struct prefix tmp;

	if (bgp_debug_update(parent_pi->peer, NULL, NULL, 1) || bgp_debug_zebra(NULL))
		zlog_debug(
			"%s (%u): Installing EVPN %pFX route in VNI %u IP/MAC table",
			vrf_id_to_name(bgp->vrf_id), bgp->vrf_id, p, vpn->vni);

	tmp.family = p->family;
	tmp.prefixlen = p->prefixlen;
	tmp.u.prefix_evpn = p->prefix;
	prefix2str(&tmp, prefix_str, sizeof(prefix_str));
	frrtrace(4, frr_bgp, upd_evpn_route_entry, 1, bgp->vrf_id, prefix_str, vpn->vni);

	ret = install_evpn_route_entry_in_vni_mac(bgp, vpn, p, parent_pi);

	if (ret) {
		flog_err(
			EC_BGP_EVPN_FAIL,
			"%s (%u): Failed to install EVPN %pFX route in VNI %u MAC table",
			vrf_id_to_name(bgp->vrf_id), bgp->vrf_id, p, vpn->vni);

		return ret;
	}

	ret = install_evpn_route_entry_in_vni_ip(bgp, vpn, p, parent_pi);

	if (ret) {
		flog_err(
			EC_BGP_EVPN_FAIL,
			"%s (%u): Failed to install EVPN %pFX route in VNI %u IP table",
			vrf_id_to_name(bgp->vrf_id), bgp->vrf_id, p, vpn->vni);

		return ret;
	}

	return ret;
}

/*
 * uninstall_evpn_route_entry -- 从 VNI 的 IP 表与 MAC 表卸载 EVPN 路由。
 *
 * 职责：按顺序调用 uninstall_evpn_route_entry_in_vni_ip 与 ..._in_vni_mac，
 *       任一步失败则记录 EC_BGP_EVPN_FAIL 并返回错误码。
 * 参数：bgp / vpn / p / parent_pi。
 * 返回：0 表示成功，非 0 表示失败。
 */
/*
 * Uninstall route entry from the VNI routing tables.
 */
static int uninstall_evpn_route_entry(struct bgp *bgp, struct bgpevpn *vpn,
				      const struct prefix_evpn *p,
				      struct bgp_path_info *parent_pi)
{
	int ret = 0;
	char prefix_str[PREFIX2STR_BUFFER] = { 0 };
	struct prefix tmp;

	if (bgp_debug_update(parent_pi->peer, NULL, NULL, 1))
		zlog_debug(
			"%s (%u): Uninstalling EVPN %pFX route from VNI %u IP/MAC table",
			vrf_id_to_name(bgp->vrf_id), bgp->vrf_id, p, vpn->vni);

	tmp.family = p->family;
	tmp.prefixlen = p->prefixlen;
	tmp.u.prefix_evpn = p->prefix;
	prefix2str(&tmp, prefix_str, sizeof(prefix_str));
	frrtrace(4, frr_bgp, upd_evpn_route_entry, 0, bgp->vrf_id, prefix_str, vpn->vni);

	ret = uninstall_evpn_route_entry_in_vni_ip(bgp, vpn, p, parent_pi);

	if (ret) {
		flog_err(
			EC_BGP_EVPN_FAIL,
			"%s (%u): Failed to uninstall EVPN %pFX route from VNI %u IP table",
			vrf_id_to_name(bgp->vrf_id), bgp->vrf_id, p, vpn->vni);

		return ret;
	}

	ret = uninstall_evpn_route_entry_in_vni_mac(bgp, vpn, p, parent_pi);

	if (ret) {
		flog_err(
			EC_BGP_EVPN_FAIL,
			"%s (%u): Failed to uninstall EVPN %pFX route from VNI %u MAC table",
			vrf_id_to_name(bgp->vrf_id), bgp->vrf_id, p, vpn->vni);

		return ret;
	}

	return ret;
}

/*
 * is_route_matching_for_vrf -- 判断一条路由是否应导入到指定的 VRF(RT 匹配)。
 *
 * 职责：要求属性含扩展团体；遍历每个 RT 扩展团体：先精确匹配 VRF 的 import RT
 *       (lookup_vrf_import_rt)；再对 AS / AS4 / IP 编码的 RT 做"掩掉 global-admin、
 *       只看 local-admin"的非精确匹配(mask_ecom_global_admin)——便于 EBGP 对端也用 VNI 作 RT；
 *       任一匹配且该 VRF 在该 import RT 的 VRF 列表中(is_vrf_present_in_irt_vrfs)即返回 1。
 * 参数：bgp_vrf / pi。
 * 返回：1 匹配，0 不匹配。
 */
/*
 * Given a route entry and a VRF, see if this route entry should be
 * imported into the VRF i.e., RTs match + Site-of-Origin check passes.
 */
static int is_route_matching_for_vrf(struct bgp *bgp_vrf,
				     struct bgp_path_info *pi)
{
	struct attr *attr = pi->attr;
	struct ecommunity *ecom;
	uint32_t i;

	assert(attr);
	/* Route should have valid RT to be even considered. */
	if (!bgp_attr_exists(attr, BGP_ATTR_EXT_COMMUNITIES))
		return 0;

	ecom = bgp_attr_get_ecommunity(attr);
	if (!ecom || !ecom->size)
		return 0;

	/* For each extended community RT, see if it matches this VNI. If any RT
	 * matches, we're done.
	 */
	for (i = 0; i < ecom->size; i++) {
		uint8_t *pnt;
		uint8_t type, sub_type;
		struct ecommunity_val *eval;
		struct ecommunity_val eval_tmp;
		struct vrf_irt_node *irt;

		/* Only deal with RTs */
		pnt = (ecom->val + (i * ecom->unit_size));
		eval = (struct ecommunity_val *)(ecom->val
						 + (i * ecom->unit_size));
		type = *pnt++;
		sub_type = *pnt++;
		if (sub_type != ECOMMUNITY_ROUTE_TARGET)
			continue;

		/* See if this RT matches specified VNIs import RTs */
		irt = lookup_vrf_import_rt(eval);
		if (irt)
			if (is_vrf_present_in_irt_vrfs(irt->vrfs, bgp_vrf))
				return 1;

		/* Also check for non-exact match. In this, we mask out the AS
		 * and
		 * only check on the local-admin sub-field. This is to
		 * facilitate using
		 * VNI as the RT for EBGP peering too.
		 */
		irt = NULL;
		if (type == ECOMMUNITY_ENCODE_AS
		    || type == ECOMMUNITY_ENCODE_AS4
		    || type == ECOMMUNITY_ENCODE_IP) {
			memcpy(&eval_tmp, eval, ecom->unit_size);
			mask_ecom_global_admin(&eval_tmp, eval);
			irt = lookup_vrf_import_rt(&eval_tmp);
		}
		if (irt)
			if (is_vrf_present_in_irt_vrfs(irt->vrfs, bgp_vrf))
				return 1;
	}

	return 0;
}

/*
 * is_route_matching_for_vni -- 判断一条路由是否应导入到指定 VNI(逻辑同 is_route_matching_for_vrf，
 *                              但作用于 VNI 的 import RT)。
 *
 * 职责：遍历 RT 扩展团体，精确(或非精确)匹配 lookup_import_rt(bgp, ...) 得到的 irt_node，
 *       若 VNI 在该 import RT 的 VNI 列表中(is_vni_present_in_irt_vnis)则返回 1。
 * 参数：bgp / vpn / pi。
 * 返回：1 匹配，0 不匹配。
 */
/*
 * Given a route entry and a VNI, see if this route entry should be
 * imported into the VNI i.e., RTs match.
 */
static int is_route_matching_for_vni(struct bgp *bgp, struct bgpevpn *vpn,
				     struct bgp_path_info *pi)
{
	struct attr *attr = pi->attr;
	struct ecommunity *ecom;
	uint32_t i;

	assert(attr);
	/* Route should have valid RT to be even considered. */
	if (!bgp_attr_exists(attr, BGP_ATTR_EXT_COMMUNITIES))
		return 0;

	ecom = bgp_attr_get_ecommunity(attr);
	if (!ecom || !ecom->size)
		return 0;

	/* For each extended community RT, see if it matches this VNI. If any RT
	 * matches, we're done.
	 */
	for (i = 0; i < ecom->size; i++) {
		uint8_t *pnt;
		uint8_t type, sub_type;
		struct ecommunity_val *eval;
		struct ecommunity_val eval_tmp;
		struct irt_node *irt;

		/* Only deal with RTs */
		pnt = (ecom->val + (i * ecom->unit_size));
		eval = (struct ecommunity_val *)(ecom->val
						 + (i * ecom->unit_size));
		type = *pnt++;
		sub_type = *pnt++;
		if (sub_type != ECOMMUNITY_ROUTE_TARGET)
			continue;

		/* See if this RT matches specified VNIs import RTs */
		irt = lookup_import_rt(bgp, eval);
		if (irt)
			if (is_vni_present_in_irt_vnis(irt->vnis, vpn))
				return 1;

		/* Also check for non-exact match. In this, we mask out the AS
		 * and
		 * only check on the local-admin sub-field. This is to
		 * facilitate using
		 * VNI as the RT for EBGP peering too.
		 */
		irt = NULL;
		if (type == ECOMMUNITY_ENCODE_AS
		    || type == ECOMMUNITY_ENCODE_AS4
		    || type == ECOMMUNITY_ENCODE_IP) {
			memcpy(&eval_tmp, eval, ecom->unit_size);
			mask_ecom_global_admin(&eval_tmp, eval);
			irt = lookup_import_rt(bgp, &eval_tmp);
		}
		if (irt)
			if (is_vni_present_in_irt_vnis(irt->vnis, vpn))
				return 1;
	}

	return 0;
}

/*
 * bgp_evpn_route_matches_macvrf_soo -- 判断 EVPN 路由是否带有本机 MAC-VRF 的 SoO(用于过滤自环)。
 *
 * 职责：仅对 Type-2 / Type-3 路由做判断(其他类型不盖 MAC-VRF SoO，无需过滤)；取 EVPN 实例的
 *       SoO，调用 route_matches_soo 判断是否命中；命中则(可选日志)返回 true。
 * 参数：pi / evp。
 * 返回：true 表示该路由来自本地 MAC-VRF(应跳过导入)，false 表示不命中。
 */
static bool bgp_evpn_route_matches_macvrf_soo(struct bgp_path_info *pi,
					      const struct prefix_evpn *evp)
{
	struct bgp *bgp_evpn = bgp_get_evpn();
	struct ecommunity *macvrf_soo;
	bool ret = false;

	if (!bgp_evpn || !bgp_evpn->evpn_info)
		return false;

	/* We only stamp the mac-vrf soo on routes from our local L2VNI.
	 * No need to filter additional EVPN routes that originated outside
	 * the MAC-VRF/L2VNI.
	 */
	if (evp->prefix.route_type != BGP_EVPN_MAC_IP_ROUTE &&
	    evp->prefix.route_type != BGP_EVPN_IMET_ROUTE)
		return false;

	macvrf_soo = bgp_evpn->evpn_info->soo;
	ret = route_matches_soo(pi, macvrf_soo);

	if (ret && bgp_debug_zebra(NULL)) {
		char *ecom_str;

		ecom_str = ecommunity_ecom2str(macvrf_soo,
					       ECOMMUNITY_FORMAT_ROUTE_MAP, 0);
		zlog_debug(
			"import of evpn prefix %pFX skipped, local mac-vrf soo %s",
			evp, ecom_str);
		ecommunity_strfree(&ecom_str);
	}

	return ret;
}

/*
 * bgp_evpn_route_rmac_self_check -- 检查 EVPN 路由携带的 RMAC 是否与本 VRF 的 RMAC 相同。
 *
 * 背景：EVPN 路由可能在 L3-VNI 起来之前就学到，安装前需做 RMAC 自检，避免把指向本机的
 *       路由装进 VRF 表和 bridge 表；等 SVI 带 MAC 起来并触发 bgp_mac_rescan_all_evpn_tables
 *       后该路由会从全局表移除。
 * 职责：比较 bgp_vrf->rmac 与属性 rmac；相同则(仅 debug / trace 开启时格式化日志并发出
 *       frrtrace upd_prefix_denied_due_to_self_mac)返回 1，否则返回 0。
 * 参数：bgp_vrf / evp / pi。
 * 返回：1 表示命中本机 RMAC(应拒绝安装)，0 表示正常。
 */
/* This API will scan evpn routes for checking attribute's rmac
 * matches with bgp instance router mac. It avoid installing
 * route into bgp vrf table and remote rmac in bridge table.
 */
static int bgp_evpn_route_rmac_self_check(struct bgp *bgp_vrf,
					  const struct prefix_evpn *evp,
					  struct bgp_path_info *pi)
{
	/* evpn route could have learnt prior to L3vni has come up,
	 * perform rmac check before installing route and
	 * remote router mac.
	 * The route will be removed from global bgp table once
	 * SVI comes up with MAC and stored in hash, triggers
	 * bgp_mac_rescan_all_evpn_tables.
	 */
	if (memcmp(&bgp_vrf->rmac, &pi->attr->rmac, ETH_ALEN) == 0) {
		/* Only do expensive string formatting if debug or trace is enabled. */
		if (bgp_debug_update(pi->peer, NULL, NULL, 1) ||
		    frrtrace_enabled(frr_bgp, upd_prefix_denied_due_to_self_mac)) {
			char prefix_str[PREFIX2STR_BUFFER] = { 0 };
			char attr_str[BUFSIZ] = { 0 };
			struct prefix tmp;

			bgp_dump_attr(pi->attr, attr_str, sizeof(attr_str));

			if (bgp_debug_update(pi->peer, NULL, NULL, 1))
				zlog_debug("%s: bgp %u prefix %pFX with attr %s - DENIED due to self mac",
					   __func__, bgp_vrf->vrf_id, evp, attr_str);

			tmp.family = evp->family;
			tmp.prefixlen = evp->prefixlen;
			tmp.u.prefix_evpn = evp->prefix;
			prefix2str(&tmp, prefix_str, sizeof(prefix_str));
			frrtrace(3, frr_bgp, upd_prefix_denied_due_to_self_mac, bgp_vrf->vrf_id,
				 prefix_str, attr_str);
		}

		return 1;
	}

	return 0;
}

/*
 * bgp_evpn_skip_vrf_import_of_local_es -- 判断是否跳过对本机 ES 上主机的 VRF 导入。
 *
 * 职责：仅对 Type-2 路由；若该路由指向本机 ES(bgp_evpn_attr_is_local_es)则返回 true
 *       (不导入本地直连主机)，否则 false。
 * 参数：bgp_vrf / evp / pi / install(仅用于日志区分 import / unimport)。
 * 返回：true 跳过，false 不跳过。
 */
/* don't import hosts that are locally attached */
bool bgp_evpn_skip_vrf_import_of_local_es(struct bgp *bgp_vrf, const struct prefix_evpn *evp,
					  struct bgp_path_info *pi, int install)
{
	esi_t *esi;

	if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) {
		esi = bgp_evpn_attr_get_esi(pi->attr);

		/* Don't import routes that point to a local destination */
		if (bgp_evpn_attr_is_local_es(pi->attr)) {
			if (BGP_DEBUG(evpn_mh, EVPN_MH_RT)) {
				char esi_buf[ESI_STR_LEN];

				zlog_debug(
					"vrf %s of evpn prefix %pFX skipped, local es %s",
					install ? "import" : "unimport", evp,
					esi_to_str(esi, esi_buf,
						   sizeof(esi_buf)));
			}
			return true;
		}
	}
	return false;
}

/*
 * bgp_evpn_route_entry_install_if_vrf_match -- 若 RT 匹配则把 mac-ip 路由安装 / 卸载到指定 VRF。
 *
 * 职责：1) 仅处理 VALID 的 BGP_ROUTE_NORMAL 远端路由；
 *       2) is_route_matching_for_vrf 判定 RT 匹配；
 *       3) 匹配后先做 RMAC 自检(bgp_evpn_route_rmac_self_check)；
 *       4) 导入时再检查本机 ES 主机(bgp_evpn_skip_vrf_import_of_local_es)与 MAC-VRF SoO；
 *       5) 通过则调用 install / uninstall_evpn_route_entry_in_vrf，失败记录错误。
 * 参数：bgp_vrf / pi / install(1 安装，0 卸载)。
 * 返回：0 或 install / uninstall 的结果(失败会返回非 0)。
 */
/*
 * Install or uninstall a mac-ip route in the provided vrf if
 * there is a rt match
 */
int bgp_evpn_route_entry_install_if_vrf_match(struct bgp *bgp_vrf,
					      struct bgp_path_info *pi,
					      int install)
{
	int ret = 0;
	const struct prefix_evpn *evp =
		(const struct prefix_evpn *)bgp_dest_get_prefix(pi->net);

	/* Consider "valid" remote routes applicable for
	 * this VRF.
	 */
	if (!(CHECK_FLAG(pi->flags, BGP_PATH_VALID) && pi->type == ZEBRA_ROUTE_BGP &&
	      pi->sub_type == BGP_ROUTE_NORMAL))
		return 0;

	if (is_route_matching_for_vrf(bgp_vrf, pi)) {
		if (bgp_evpn_route_rmac_self_check(bgp_vrf, evp, pi))
			return 0;

		/* don't import hosts that are locally attached */
		if (install && (bgp_evpn_skip_vrf_import_of_local_es(
					bgp_vrf, evp, pi, install) ||
				bgp_evpn_route_matches_macvrf_soo(pi, evp)))
			return 0;

		if (install)
			ret = install_evpn_route_entry_in_vrf(bgp_vrf, evp, pi);
		else
			ret = uninstall_evpn_route_entry_in_vrf(bgp_vrf, evp,
								pi);

		if (ret)
			flog_err(EC_BGP_EVPN_FAIL,
				 "Failed to %s EVPN %pFX route in VRF %s",
				 install ? "install" : "uninstall", evp,
				 vrf_id_to_name(bgp_vrf->vrf_id));
	}

	return ret;
}

/*
 * install_uninstall_routes_for_vrf -- 遍历全局 EVPN 表，把匹配的 mac-ip / type-5 路由批量
 *                                     安装 / 卸载到指定 VRF。
 *
 * 职责：取 EVPN 实例(无则返回 -1)；遍历全局表(RD 级)下每个前缀节点(前缀级)，跳过非
 *       Type-2 / Type-5 或无 IP 的路由；对节点上每条路径调用
 *       bgp_evpn_route_entry_install_if_vrf_match，失败时解锁已持有的节点并提前返回错误。
 * 参数：bgp_vrf / install。
 * 返回：0 成功，-1(无 EVPN 实例)或某次失败的错误码。
 */
/*
 * Install or uninstall mac-ip routes are appropriate for this
 * particular VRF.
 */
static int install_uninstall_routes_for_vrf(struct bgp *bgp_vrf, bool install)
{
	afi_t afi;
	safi_t safi;
	struct bgp_dest *rd_dest, *dest;
	struct bgp_table *table;
	struct bgp_path_info *pi;
	int ret;
	struct bgp *bgp_evpn = NULL;

	afi = AFI_L2VPN;
	safi = SAFI_EVPN;
	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn)
		return -1;

	/* Walk entire global routing table and evaluate routes which could be
	 * imported into this VRF. Note that we need to loop through all global
	 * routes to determine which route matches the import rt on vrf
	 */
	for (rd_dest = bgp_table_top(bgp_evpn->rib[afi][safi]); rd_dest;
	     rd_dest = bgp_route_next(rd_dest)) {
		table = bgp_dest_get_bgp_table_info(rd_dest);
		if (!table)
			continue;

		for (dest = bgp_table_top(table); dest;
		     dest = bgp_route_next(dest)) {
			const struct prefix_evpn *evp =
				(const struct prefix_evpn *)bgp_dest_get_prefix(
					dest);

			/* if not mac-ip route skip this route */
			if (!(evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE ||
			      evp->prefix.route_type == BGP_EVPN_IP_PREFIX_ROUTE))
				continue;

			/* if not a mac+ip route skip this route */
			if (!(is_evpn_prefix_ipaddr_v4(evp) || is_evpn_prefix_ipaddr_v6(evp)))
				continue;

			for (pi = bgp_dest_get_bgp_path_info(dest); pi;
			     pi = pi->next) {
				ret = bgp_evpn_route_entry_install_if_vrf_match(bgp_vrf, pi,
										install);
				if (ret) {
					bgp_dest_unlock_node(rd_dest);
					bgp_dest_unlock_node(dest);
					return ret;
				}
			}
		}
	}

	return 0;
}

/*
 * install_evpn_remote_route_per_l2vni -- 通过 zebra L2-VNI 队列(fifo)把一条远端 EVPN 路由
 *                                        安装到 RT 匹配的各个 L2 VNI。
 *
 * 说明：bm->zebra_l2_vni_head 是 zebra 通告、待处理的 L2 VNI 链表；单次最多处理
 *       BGP_PROC_L2VNI_LIMIT(10)个，避免一次处理过多。
 * 职责：遍历队列(先取下一个再处理，允许处理后删除当前项)，跳过 RT 不匹配的 VNI，对匹配者调用
 *       install_evpn_route_entry；失败则记录错误、从队列中删除该 VNI 并返回错误码。
 * 参数：bgp / pi / evp。
 * 返回：0 成功，非 0 失败。
 */
#define BGP_PROC_L2VNI_LIMIT 10
static int install_evpn_remote_route_per_l2vni(struct bgp *bgp, struct bgp_path_info *pi,
					       const struct prefix_evpn *evp)
{
	int ret = 0;
	uint8_t vni_iter = 0;
	struct bgpevpn *t_vpn = NULL;
	struct bgpevpn *t_vpn_next = NULL;

	for (t_vpn = zebra_l2_vni_first(&bm->zebra_l2_vni_head);
	     t_vpn && vni_iter < BGP_PROC_L2VNI_LIMIT; t_vpn = t_vpn_next) {
		t_vpn_next = zebra_l2_vni_next(&bm->zebra_l2_vni_head, t_vpn);
		vni_iter++;
		/*
		 * Skip install/uninstall if the route entry is not needed to
		 * be imported into the VNI i.e. RTs dont match
		 */
		if (!is_route_matching_for_vni(bgp, t_vpn, pi))
			continue;

		ret = install_evpn_route_entry(bgp, t_vpn, evp, pi);

		if (ret) {
			flog_err(EC_BGP_EVPN_FAIL,
				 "%u: Failed to install EVPN %s route in VNI %u during BP",
				 bgp->vrf_id, bgp_evpn_route_type_str[evp->prefix.route_type].str,
				 t_vpn->vni);
			zebra_l2_vni_del(&bm->zebra_l2_vni_head, t_vpn);

			return ret;
		}
	}

	return 0;
}

/*
 * install_uninstall_routes_for_vni -- 遍历全局 EVPN 表，把匹配的路由批量安装 / 卸载到指定 VNI。
 *
 * 职责：bgp 为空时切换到 walk_fifo 模式(用 zebra L2 VNI 队列，bgp 取 EVPN 实例)；遍历全局表
 *       (RD 级 + 前缀级两级)，只处理 Type-1(AD) / Type-2(MAC-IP) / Type-3(IMET)，跳过
 *       VALID 之外或非 BGP_ROUTE_NORMAL 的路径(导入时还跳过命中 MAC-VRF SoO 的)；
 *       - walk_fifo 模式：对每条路径调用 install_evpn_remote_route_per_l2vni；
 *       - 否则：再判 is_route_matching_for_vni，调用 install / uninstall_evpn_route_entry；
 *       失败则解锁节点并返回错误码。walk_fifo 模式最后从队列弹出至多 BGP_PROC_L2VNI_LIMIT 个
 *       VNI 并清其 VNI_FLAG_ADD。
 * 参数：bgp(可空) / vpn(非 fifo 模式使用) / install。
 * 返回：0 成功，-1(无 EVPN 实例)或失败错误码。
 */
/*
 * Install or uninstall routes of specified type that are appropriate for this
 * particular VNI.
 */
int install_uninstall_routes_for_vni(struct bgp *bgp, struct bgpevpn *vpn, bool install)
{
	afi_t afi;
	safi_t safi;
	struct bgp_dest *rd_dest, *dest;
	struct bgp_table *table;
	struct bgp_path_info *pi;
	int ret = 0;
	uint8_t count = 0;
	bool walk_fifo = false;

	afi = AFI_L2VPN;
	safi = SAFI_EVPN;

	if (!bgp) {
		walk_fifo = true;
		bgp = bgp_get_evpn();
		if (!bgp) {
			zlog_warn("%s: No BGP EVPN instance found...", __func__);

			return -1;
		}
	}

	if (BGP_DEBUG(zebra, ZEBRA))
		zlog_debug("%s: Total %u L2VNI VPNs pending to be processed for remote route installation",
			   __func__, (uint32_t)zebra_l2_vni_count(&bm->zebra_l2_vni_head));
	/*
	 * Walk entire global routing table and evaluate routes which could be
	 * imported into this VPN. Note that we cannot just look at the routes
	 * for the VNI's RD - remote routes applicable for this VNI could have
	 * any RD.
	 * Note: EVPN routes are a 2-level table.
	 */
	for (rd_dest = bgp_table_top(bgp->rib[afi][safi]); rd_dest;
	     rd_dest = bgp_route_next(rd_dest)) {
		table = bgp_dest_get_bgp_table_info(rd_dest);
		if (!table)
			continue;

		for (dest = bgp_table_top(table); dest;
		     dest = bgp_route_next(dest)) {
			const struct prefix_evpn *evp =
				(const struct prefix_evpn *)bgp_dest_get_prefix(
					dest);

			/* Proceed only for AD, MAC_IP and IMET routes */
			switch (evp->prefix.route_type) {
			case BGP_EVPN_AD_ROUTE:
			case BGP_EVPN_MAC_IP_ROUTE:
			case BGP_EVPN_IMET_ROUTE:
				break;
			case BGP_EVPN_ES_ROUTE:
			case BGP_EVPN_IP_PREFIX_ROUTE:
				continue;
			}

			for (pi = bgp_dest_get_bgp_path_info(dest); pi;
			     pi = pi->next) {
				/*
				 * Skip install/uninstall if
				 * - Not a valid remote routes
				 * - Install & evpn route matchesi macvrf SOO
				 */
				if (!(CHECK_FLAG(pi->flags, BGP_PATH_VALID) &&
				      pi->type == ZEBRA_ROUTE_BGP &&
				      pi->sub_type == BGP_ROUTE_NORMAL) ||
				    (install && bgp_evpn_route_matches_macvrf_soo(pi, evp)))
					continue;

				if (walk_fifo) {
					ret = install_evpn_remote_route_per_l2vni(bgp, pi, evp);
					if (ret) {
						bgp_dest_unlock_node(rd_dest);
						bgp_dest_unlock_node(dest);
						return ret;
					}
				} else {
					/*
					 * Skip install/uninstall if the route
					 * entry is not needed to be imported
					 * into the VNI i.e. RTs dont match
					 */
					if (!is_route_matching_for_vni(bgp, vpn, pi))
						continue;

					if (install)
						ret = install_evpn_route_entry(bgp, vpn, evp, pi);
					else
						ret = uninstall_evpn_route_entry(bgp, vpn, evp, pi);

					if (ret) {
						flog_err(EC_BGP_EVPN_FAIL,
							 "%u: Failed to %s EVPN %s route in VNI %u",
							 bgp->vrf_id,
							 install ? "install" : "uninstall",
							 bgp_evpn_route_type_str[evp->prefix.route_type]
								 .str,
							 vpn->vni);

						bgp_dest_unlock_node(rd_dest);
						bgp_dest_unlock_node(dest);
						return ret;
					}
				}
			}
		}
	}

	if (walk_fifo) {
		while (count < BGP_PROC_L2VNI_LIMIT) {
			vpn = zebra_l2_vni_pop(&bm->zebra_l2_vni_head);
			if (!vpn)
				return 0;

			UNSET_FLAG(vpn->flags, VNI_FLAG_ADD);
			count++;
		}
	}

	return 0;
}

/*
 * install_routes_for_vrf -- 安装该 VRF 适用的既有远端路由(install_uninstall_routes_for_vrf 的封装)。
 *
 * 触发：L3-VNI add 或 L3-VNI import RT 变化时。
 * 返回：install_uninstall_routes_for_vrf 的返回值。
 */
/* Install any existing remote routes applicable for this VRF into VRF RIB. This
 * is invoked upon l3vni-add or l3vni import rt change
 */
static int install_routes_for_vrf(struct bgp *bgp_vrf)
{
	return install_uninstall_routes_for_vrf(bgp_vrf, true);
}

/*
 * install_routes_for_vni -- 安装该 VNI 适用的既有远端路由(install_uninstall_routes_for_vni 的封装)。
 *
 * 触发：VNI 变为 live 或其 import RT 变化时。
 * 返回：install_uninstall_routes_for_vni 的返回值。
 * 说明：先装 Type-3，再装 Type-2。
 */
/*
 * Install any existing remote routes applicable for this VNI into its
 * routing table. This is invoked when a VNI becomes "live" or its Import
 * RT is changed.
 */
static int install_routes_for_vni(struct bgp *bgp, struct bgpevpn *vpn)
{
	/*
	 * Install type-3 routes followed by type-2 routes - the ones applicable
	 * for this VNI.
	 */
	return install_uninstall_routes_for_vni(bgp, vpn, true);
}

/*
 * uninstall_routes_for_vrf -- 卸载该 VRF 的远端路由(install_uninstall_routes_for_vrf 的封装)。
 * 返回：install_uninstall_routes_for_vrf 的返回值。
 */
/* uninstall routes from l3vni vrf. */
static int uninstall_routes_for_vrf(struct bgp *bgp_vrf)
{
	return install_uninstall_routes_for_vrf(bgp_vrf, false);
}

/*
 * uninstall_routes_for_vni -- 卸载该 VNI 的远端路由(install_uninstall_routes_for_vni 的封装)。
 *
 * 说明：先卸 Type-2，再卸 Type-3。
 * 触发：例如 import RT 变化时。
 * 返回：install_uninstall_routes_for_vni 的返回值。
 */
/*
 * Uninstall any existing remote routes for this VNI. One scenario in which
 * this is invoked is upon an import RT change.
 */
static int uninstall_routes_for_vni(struct bgp *bgp, struct bgpevpn *vpn)
{
	/*
	 * Uninstall type-2 routes followed by type-3 routes - the ones
	 * applicable for this VNI.
	 */
	return install_uninstall_routes_for_vni(bgp, vpn, false);
}

/*
 * install_uninstall_route_in_vrfs -- 把一条路由批量安装 / 卸载到一组匹配的 VRF(列表)。
 *
 * 职责：仅处理 Type-2 / Type-5(且 Type-2 需含 IP)；遍历 VRF 列表，导入时先跳过本机 ES 主机
 *       (bgp_evpn_skip_vrf_import_of_local_es)，再调用 install / uninstall_evpn_route_entry_in_vrf；
 *       失败记录错误并返回。
 * 参数：bgp_def / afi,safi / evp / pi / vrfs / install。
 * 返回：0 或失败错误码。
 */
/*
 * Install or uninstall route in matching VRFs (list).
 */
static int install_uninstall_route_in_vrfs(struct bgp *bgp_def, afi_t afi,
					   safi_t safi, struct prefix_evpn *evp,
					   struct bgp_path_info *pi,
					   struct list *vrfs, int install)
{
	struct bgp *bgp_vrf;
	struct listnode *node, *nnode;

	/* Only type-2/type-5 routes go into a VRF */
	if (!(evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE
	      || evp->prefix.route_type == BGP_EVPN_IP_PREFIX_ROUTE))
		return 0;

	/* if it is type-2 route and not a mac+ip route skip this route */
	if ((evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE)
	    && !(is_evpn_prefix_ipaddr_v4(evp)
		 || is_evpn_prefix_ipaddr_v6(evp)))
		return 0;

	for (ALL_LIST_ELEMENTS(vrfs, node, nnode, bgp_vrf)) {
		int ret;

		/* don't import hosts that are locally attached */
		if (install && bgp_evpn_skip_vrf_import_of_local_es(
				       bgp_vrf, evp, pi, install))
			return 0;

		if (install)
			ret = install_evpn_route_entry_in_vrf(bgp_vrf, evp, pi);
		else
			ret = uninstall_evpn_route_entry_in_vrf(bgp_vrf, evp,
								pi);

		if (ret) {
			flog_err(EC_BGP_EVPN_FAIL,
				 "%u: Failed to %s prefix %pFX in VRF %s",
				 bgp_def->vrf_id,
				 install ? "install" : "uninstall", evp,
				 vrf_id_to_name(bgp_vrf->vrf_id));
			return ret;
		}
	}

	return 0;
}

/*
 * install_uninstall_route_in_vnis -- 把一条路由批量安装 / 卸载到一组匹配的 VNI(列表)。
 *
 * 职责：遍历 VNI 列表，跳过非 live 的 VNI，其余调用 install / uninstall_evpn_route_entry；
 *       失败记录错误并返回(日志区分 MACIP / IMET)。
 * 参数：bgp / afi,safi / evp / pi / vnis / install。
 * 返回：0 或失败错误码。
 */
/*
 * Install or uninstall route in matching VNIs (list).
 */
static int install_uninstall_route_in_vnis(struct bgp *bgp, afi_t afi,
					   safi_t safi, struct prefix_evpn *evp,
					   struct bgp_path_info *pi,
					   struct list *vnis, int install)
{
	struct bgpevpn *vpn;
	struct listnode *node, *nnode;

	for (ALL_LIST_ELEMENTS(vnis, node, nnode, vpn)) {
		int ret;

		if (!is_vni_live(vpn))
			continue;

		if (install)
			ret = install_evpn_route_entry(bgp, vpn, evp, pi);
		else
			ret = uninstall_evpn_route_entry(bgp, vpn, evp, pi);

		if (ret) {
			flog_err(EC_BGP_EVPN_FAIL,
				 "%u: Failed to %s EVPN %s route in VNI %u",
				 bgp->vrf_id, install ? "install" : "uninstall",
				 evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE
					 ? "MACIP"
					 : "IMET",
				 vpn->vni);
			return ret;
		}
	}

	return 0;
}

/*
 * bgp_evpn_install_uninstall_table -- 依据路由携带的 RT，把一条 EVPN 路由安装 / 卸载到相应
 *                                      的 VNI / VRF / ES 表。
 *
 * 职责：1) 仅支持 Type-1 至 Type-5；
 *       2) 无扩展团体则直接返回；EAD(Type-1)在全局表不含 VTEP-IP，需生成 VNI 副本
 *          (evpn_type1_prefix_vni_ip_copy)；导入时先做 MAC-VRF SoO 过滤；
 *       3) 遍历每个 RT 扩展团体：
 *          - Type-2 的全局路由建立到目的 ES 的关联(bgp_evpn_path_es_link)；
 *          - AD / IMET / MACIP 导入 VNI 表(in_vni_rt 时 lookup_import_rt)，MACIP / Type-5
 *            导入 VRF 表(in_vrf_rt 时 lookup_vrf_import_rt)；
 *          - 再做 AS / AS4 / IP 编码的非精确匹配(mask_ecom_global_admin)，重复上述导入；
 *          - ES 路由(Type-4)按整个 ESI 匹配本机 ES(bgp_evpn_es_find + is_es_local)，交
 *            bgp_evpn_es_route_install_uninstall 处理。
 * 参数：bgp / afi,safi / p / pi / import(1 导入 0 卸载) / in_vni_rt / in_vrf_rt
 *       (是否处理 VNI / VRF 表)。
 * 返回：0 成功(或无可处理)，-1(有扩展团体属性但 ecom 为空)。
 */
/*
 * Install or uninstall route for appropriate VNIs/ESIs.
 */
static int bgp_evpn_install_uninstall_table(struct bgp *bgp, afi_t afi,
					    safi_t safi, const struct prefix *p,
					    struct bgp_path_info *pi,
					    int import, bool in_vni_rt,
					    bool in_vrf_rt)
{
	struct prefix_evpn *evp = (struct prefix_evpn *)p;
	struct attr *attr = pi->attr;
	struct ecommunity *ecom;
	uint32_t i;
	struct prefix_evpn ad_evp;

	assert(attr);

	/* Only EVPN route-types 1-5 are supported currently */
	if (!(evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE
	      || evp->prefix.route_type == BGP_EVPN_IMET_ROUTE
	      || evp->prefix.route_type == BGP_EVPN_ES_ROUTE
	      || evp->prefix.route_type == BGP_EVPN_AD_ROUTE
	      || evp->prefix.route_type == BGP_EVPN_IP_PREFIX_ROUTE))
		return 0;

	/* If we don't have Route Target, nothing much to do. */
	if (!bgp_attr_exists(attr, BGP_ATTR_EXT_COMMUNITIES))
		return 0;

	/* EAD prefix in the global table doesn't include the VTEP-IP so
	 * we need to create a different copy for the VNI
	 */
	if (evp->prefix.route_type == BGP_EVPN_AD_ROUTE)
		evp = evpn_type1_prefix_vni_ip_copy(&ad_evp, evp, attr);

	ecom = bgp_attr_get_ecommunity(attr);
	if (!ecom || !ecom->size)
		return -1;

	/* Filter routes carrying a Site-of-Origin that matches our
	 * local MAC-VRF SoO.
	 */
	if (import && bgp_evpn_route_matches_macvrf_soo(pi, evp))
		return 0;

	/* An EVPN route belongs to a VNI or a VRF or an ESI based on the RTs
	 * attached to the route */
	for (i = 0; i < ecom->size; i++) {
		uint8_t *pnt;
		uint8_t type, sub_type;
		struct ecommunity_val *eval;
		struct ecommunity_val eval_tmp;
		struct irt_node *irt;	 /* import rt for l2vni */
		struct vrf_irt_node *vrf_irt; /* import rt for l3vni */
		struct bgp_evpn_es *es;

		/* Only deal with RTs */
		pnt = (ecom->val + (i * ecom->unit_size));
		eval = (struct ecommunity_val *)(ecom->val
						 + (i * ecom->unit_size));
		type = *pnt++;
		sub_type = *pnt++;
		if (sub_type != ECOMMUNITY_ROUTE_TARGET)
			continue;

		/* non-local MAC-IP routes in the global route table are linked
		 * to the destination ES
		 */
		if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE)
			bgp_evpn_path_es_link(pi, 0,
					      bgp_evpn_attr_get_esi(pi->attr));

		/*
		 * AD/IMET routes (type-1/3) are imported into VNI table.
		 * MACIP routes (type-2) are imported into VNI and VRF tables.
		 * Prefix routes (type 5) are imported into VRF table.
		 */
		if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE ||
		    evp->prefix.route_type == BGP_EVPN_IMET_ROUTE ||
		    evp->prefix.route_type == BGP_EVPN_AD_ROUTE ||
		    evp->prefix.route_type == BGP_EVPN_IP_PREFIX_ROUTE) {
			if (evp->prefix.route_type != BGP_EVPN_IP_PREFIX_ROUTE) {
				irt = in_vni_rt ? lookup_import_rt(bgp, eval) : NULL;
				if (irt)
					install_uninstall_route_in_vnis(bgp, afi, safi, evp, pi,
									irt->vnis, import);
			}

			if (evp->prefix.route_type != BGP_EVPN_AD_ROUTE &&
			    evp->prefix.route_type != BGP_EVPN_IMET_ROUTE) {
				vrf_irt = in_vrf_rt ? lookup_vrf_import_rt(eval) : NULL;
				if (vrf_irt)
					install_uninstall_route_in_vrfs(bgp, afi, safi, evp, pi,
									vrf_irt->vrfs, import);
			}

			/* Also check for non-exact match.
			 * In this, we mask out the AS and
			 * only check on the local-admin sub-field.
			 * This is to facilitate using
			 * VNI as the RT for EBGP peering too.
			 */
			irt = NULL;
			vrf_irt = NULL;
			if (type == ECOMMUNITY_ENCODE_AS
			    || type == ECOMMUNITY_ENCODE_AS4
			    || type == ECOMMUNITY_ENCODE_IP) {
				memcpy(&eval_tmp, eval, ecom->unit_size);
				mask_ecom_global_admin(&eval_tmp, eval);
				if (in_vni_rt)
					irt = lookup_import_rt(bgp, &eval_tmp);
				if (in_vrf_rt)
					vrf_irt =
						lookup_vrf_import_rt(&eval_tmp);
			}

			if (irt)
				install_uninstall_route_in_vnis(
					bgp, afi, safi, evp, pi, irt->vnis,
					import);
			if (vrf_irt)
				install_uninstall_route_in_vrfs(
					bgp, afi, safi, evp, pi, vrf_irt->vrfs,
					import);
		}

		/* es route is imported into the es table */
		if (evp->prefix.route_type == BGP_EVPN_ES_ROUTE) {

			/* we will match based on the entire esi to avoid
			 * import of an es route for esi2 into esi1
			 */
			es = bgp_evpn_es_find(&evp->prefix.es_addr.esi);
			if (es && bgp_evpn_is_es_local(es))
				bgp_evpn_es_route_install_uninstall(
					bgp, es, afi, safi, evp, pi, import);
		}
	}

	return 0;
}

/*
 * install_uninstall_evpn_route -- 对一条 EVPN 路由做全量安装 / 卸载(VNI 表与 VRF 表都处理)。
 *
 * 职责：以 in_vni_rt = true、in_vrf_rt = true 调用 bgp_evpn_install_uninstall_table。
 * 返回：bgp_evpn_install_uninstall_table 的返回值。
 */
/*
 * Install or uninstall route for appropriate VNIs/ESIs.
 */
static int install_uninstall_evpn_route(struct bgp *bgp, afi_t afi, safi_t safi,
					const struct prefix *p,
					struct bgp_path_info *pi, int import)
{
	return bgp_evpn_install_uninstall_table(bgp, afi, safi, p, pi, import,
						true, true);
}

/*
 * bgp_evpn_import_type2_route -- 导入 / 撤销导入一条 Type-2 路由(供 MH 等模块调用)。
 *
 * 职责：取 EVPN 实例(无则返回)，对其全局表前缀调用 install_uninstall_evpn_route 处理 import。
 * 参数：pi / import。
 * 返回：无。
 */
void bgp_evpn_import_type2_route(struct bgp_path_info *pi, int import)
{
	struct bgp *bgp_evpn;

	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn)
		return;

	install_uninstall_evpn_route(bgp_evpn, AFI_L2VPN, SAFI_EVPN,
				     &pi->net->rn->p, pi, import);
}

/*
 * delete_withdraw_vrf_routes -- 删除并撤销某 VRF 表内全部 IPv4 / IPv6 路由(以 Type-5 形式)。
 *
 * 做什么：若该 VRF 配置了 EVPN 默认路由发起，则先删除并撤销对应地址族的默认路由；
 *         若启用了 Type-5 最优路径 / 多路径发布，则撤销该地址族全部 Type-5 路由。
 *
 * 参数：bgp_vrf 为 VRF 对应的 BGP 实例。
 * 返回：无。副作用：从全局 EVPN 表移除相应 Type-5 路由，进而向对端撤销。
 * 坑：默认路由与普通路由走不同分支，两个地址族都要处理，别只做一半。
 */
/*
 * delete and withdraw all ipv4 and ipv6 routes in the vrf table as type-5
 * routes
 */
static void delete_withdraw_vrf_routes(struct bgp *bgp_vrf)
{
	/* Delete ipv4 default route and withdraw from peers */
	if (evpn_default_originate_set(bgp_vrf, AFI_IP, SAFI_UNICAST))
		bgp_evpn_install_uninstall_default_route(bgp_vrf, AFI_IP, SAFI_UNICAST, NULL,
							 false);

	/* delete all ipv4 routes and withdraw from peers */
	if (advertise_type5_routes_bestpath(bgp_vrf, AFI_IP) ||
	    advertise_type5_routes_multipath(bgp_vrf, AFI_IP))
		bgp_evpn_withdraw_type5_routes(bgp_vrf, AFI_IP, SAFI_UNICAST);

	/* Delete ipv6 default route and withdraw from peers */
	if (evpn_default_originate_set(bgp_vrf, AFI_IP6, SAFI_UNICAST))
		bgp_evpn_install_uninstall_default_route(bgp_vrf, AFI_IP6, SAFI_UNICAST, NULL,
							 false);

	/* delete all ipv6 routes and withdraw from peers */
	if (advertise_type5_routes_bestpath(bgp_vrf, AFI_IP6) ||
	    advertise_type5_routes_multipath(bgp_vrf, AFI_IP6))
		bgp_evpn_withdraw_type5_routes(bgp_vrf, AFI_IP6, SAFI_UNICAST);
}

/*
 * update_advertise_vrf_routes -- 更新并发布某 VRF 表内全部 IPv4 / IPv6 路由(以 Type-5 形式)。
 *
 * 做什么：先取全局 EVPN 实例，没有则直接返回；再判断该 VRF 是否有活跃的 L3VNI，
 *         没有则无事可做。随后按地址族发布普通 Type-5 路由、更新默认路由。
 * 参数：bgp_vrf 为 VRF 对应的 BGP 实例。
 * 返回：无。副作用：在全局 EVPN 表创建 / 更新 Type-5 路由并调度向对端发布。
 * 坑：必须先确认 l3vni 存活，否则会把不该发布的 VRF 路由发出去。
 */
/*
 * update and advertise all ipv4 and ipv6 routes in thr vrf table as type-5
 * routes
 */
void update_advertise_vrf_routes(struct bgp *bgp_vrf)
{
	struct bgp *bgp_evpn = NULL; /* EVPN bgp instance */

	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn)
		return;

	if (!is_l3vni_live(bgp_vrf))
		return; /* Nothing to do if no l3vni */

	/* update all ipv4 routes */
	if (advertise_type5_routes_bestpath(bgp_vrf, AFI_IP) ||
	    advertise_type5_routes_multipath(bgp_vrf, AFI_IP))
		bgp_evpn_advertise_type5_routes(bgp_vrf, AFI_IP, SAFI_UNICAST);

	/* update ipv4 default route and withdraw from peers */
	if (evpn_default_originate_set(bgp_vrf, AFI_IP, SAFI_UNICAST))
		bgp_evpn_install_uninstall_default_route(bgp_vrf, AFI_IP, SAFI_UNICAST, NULL, true);

	/* update all ipv6 routes */
	if (advertise_type5_routes_bestpath(bgp_vrf, AFI_IP6) ||
	    advertise_type5_routes_multipath(bgp_vrf, AFI_IP6))
		bgp_evpn_advertise_type5_routes(bgp_vrf, AFI_IP6, SAFI_UNICAST);

	/* update ipv6 default route and withdraw from peers */
	if (evpn_default_originate_set(bgp_vrf, AFI_IP6, SAFI_UNICAST))
		bgp_evpn_install_uninstall_default_route(bgp_vrf, AFI_IP6, SAFI_UNICAST, NULL,
							 true);
}

/*
 * update_router_id_vrf -- VRF 的 router-id 变化后，重新发布其 Type-5 路由。
 *
 * 做什么：若该 VRF 的 RD 是显式配置的就跳过(无需跟随 router-id 重算)；
 *         否则依据新 router-id 推导自动 RD，再调用 update_advertise_vrf_routes。
 * 参数：bgp_vrf 为 VRF 对应的 BGP 实例。
 * 返回：无。副作用：更新全局表中该 VRF 的 Type-5 路由并重新发布。
 * 坑：仅在 VRF 表中已有路由上做处理，不新建路由。
 */
/*
 * update and advertise local routes for a VRF as type-5 routes.
 * This is invoked upon RD change for a VRF. Note that the processing is only
 * done in the global route table using the routes which already exist in the
 * VRF routing table
 */
static void update_router_id_vrf(struct bgp *bgp_vrf)
{
	/* skip if the RD is configured */
	if (is_vrf_rd_configured(bgp_vrf))
		return;

	/* derive the RD for the VRF based on new router-id */
	bgp_evpn_derive_auto_rd_for_vrf(bgp_vrf);

	/* update advertise ipv4|ipv6 routes as type-5 routes */
	update_advertise_vrf_routes(bgp_vrf);
}

/*
 * withdraw_router_id_vrf -- VRF 的 router-id 变化前，撤销其旧的 Type-5 路由。
 *
 * 做什么：若该 VRF 的 RD 是显式配置的就跳过；否则调用 delete_withdraw_vrf_routes
 *         删除并撤销对应全部 Type-5 路由(旧 RD 下的)。
 * 参数：bgp_vrf 为 VRF 对应的 BGP 实例。
 * 返回：无。副作用：从全局表删除旧 RD 的 Type-5 路由并向对端撤销。
 * 坑：这是"先撤后发"的撤步骤，必须在更新 router-id 之前调用。
 */
/*
 * Delete and withdraw all type-5 routes  for the RD corresponding to VRF.
 * This is invoked upon VRF RD change. The processing is done only from global
 * table.
 */
static void withdraw_router_id_vrf(struct bgp *bgp_vrf)
{
	/* skip if the RD is configured */
	if (is_vrf_rd_configured(bgp_vrf))
		return;

	/* delete/withdraw ipv4|ipv6 routes as type-5 routes */
	delete_withdraw_vrf_routes(bgp_vrf);
}

/*
 * update_advertise_vni_route -- 依据某 VNI 表节点，在全局表中创建 / 更新对应 EVPN 路由。
 *
 * 做什么：只处理 Type-1(AD)与 Type-2(MAC-IP)路由；取该 dest 的本地路径 pi，
 *         用其属性 attr 在全局表(带 vpn->prd 作为 RD)生成全局节点 global_dest。
 *         Type-2 走 update_evpn_route_entry；Type-1 走 bgp_evpn_mh_route_update。
 *         最后调用 bgp_process 调度处理并解锁节点。
 * 参数：bgp 全局实例；vpn 所属 VNI；dest 为 per-VNI 表中的路由节点。
 * 返回：无。副作用：在全局表增改路由并调度发布。
 * 坑：VNI 表的 MAC-IP 前缀不带 MAC，需从 pi 补回 MAC 或 IP 再拷贝到全局前缀。
 */
static void update_advertise_vni_route(struct bgp *bgp, struct bgpevpn *vpn,
				       struct bgp_dest *dest)
{
	struct bgp_dest *global_dest;
	struct bgp_path_info *pi, *global_pi;
	struct attr *attr;
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;

	struct prefix_evpn tmp_evp;
	const struct prefix_evpn *evp =
		(const struct prefix_evpn *)bgp_dest_get_prefix(dest);

	/*
	 * We have already processed type-3 routes.
	 * Process only type-1 and type-2 routes here.
	 */
	if (evp->prefix.route_type != BGP_EVPN_MAC_IP_ROUTE &&
	    evp->prefix.route_type != BGP_EVPN_AD_ROUTE)
		return;

	pi = bgp_evpn_route_get_local_path(bgp, dest, 0);
	if (!pi)
		return;

	/*
	 * VNI table MAC-IP prefixes don't have MAC so make sure it's
	 * set from path info here.
	 */
	if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) {
		if (is_evpn_prefix_ipaddr_none(evp)) {
			/* VNI MAC -> Global */
			evpn_type2_prefix_global_copy(
				&tmp_evp, evp, NULL /* mac */,
				evpn_type2_path_info_get_ip(pi));
		} else {
			/* VNI IP -> Global */
			evpn_type2_prefix_global_copy(
				&tmp_evp, evp, evpn_type2_path_info_get_mac(pi),
				NULL /* ip */);
		}
	} else {
		memcpy(&tmp_evp, evp, sizeof(tmp_evp));
	}

	/* Create route in global routing table using this route entry's
	 * attribute.
	 */
	attr = pi->attr;
	global_dest = bgp_evpn_global_node_get(bgp->rib[afi][safi], afi, safi,
					       &tmp_evp, &vpn->prd, NULL);
	assert(global_dest);

	if (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE) {
		/* Type-2 route */
		update_evpn_route_entry(
			bgp, vpn, afi, safi, global_dest, attr, NULL /* mac */,
			NULL /* ip */, 1, &global_pi, 0,
			mac_mobility_seqnum(attr), false /* setup_sync */,
			NULL /* old_is_sync */);
	} else {
		/* Type-1 route */
		struct bgp_evpn_es *es;
		int route_changed = 0;

		es = bgp_evpn_es_find(&evp->prefix.ead_addr.esi);
		bgp_evpn_mh_route_update(bgp, es, vpn, afi, safi, global_dest,
					 attr, &global_pi, &route_changed);
	}

	/* Schedule for processing and unlock node. */
	bgp_process(bgp, global_dest, global_pi, afi, safi);
	bgp_dest_unlock_node(global_dest);
}

/*
 * update_advertise_vni_routes -- VNI 的 router-id / RD 变化后，重新发布其本地路由。
 *
 * 做什么：若该 VNI 处于头端复制(head-end replication)泛洪模式，则为其重建 Type-3 路由
 *         并在全局表创建 / 更新；随后遍历该 VNI 的 mac_table 与 ip_table，
 *         对每个节点调用 update_advertise_vni_route，把本地路由镜像到全局表。
 * 参数：bgp 全局实例；vpn 目标 VNI。
 * 返回：无。副作用：在全局表创建 / 更新该 VNI 的 Type-3 与 Type-2 / Type-1 路由并发布。
 * 坑：Type-3 仅在头端复制模式下生成；遍历过程中会同时处理 MAC 表与 IP 表。
 */
/*
 * Update and advertise local routes for a VNI. Invoked upon router-id/RD
 * change. Note that the processing is done only on the global route table
 * using routes that already exist in the per-VNI table.
 */
static void update_advertise_vni_routes(struct bgp *bgp, struct bgpevpn *vpn)
{
	struct prefix_evpn p;
	struct bgp_dest *dest, *global_dest;
	struct bgp_path_info *pi;
	struct attr *attr;
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;

	/* Locate type-3 route for VNI in the per-VNI table and use its
	 * attributes to create and advertise the type-3 route for this VNI
	 * in the global table.
	 *
	 * RT-3 only if doing head-end replication
	 */
	if (bgp_evpn_vni_flood_mode_get(bgp, vpn)
				== VXLAN_FLOOD_HEAD_END_REPL) {
		build_evpn_type3_prefix(&p, &vpn->originator_ip);
		dest = bgp_evpn_vni_node_lookup(vpn, &p, NULL);
		if (!dest) /* unexpected */
			return;
		pi = bgp_evpn_route_get_local_path(bgp, dest, 0);
		if (!pi) {
			bgp_dest_unlock_node(dest);
			return;
		}

		attr = pi->attr;

		global_dest = bgp_evpn_global_node_get(
			bgp->rib[afi][safi], afi, safi, &p, &vpn->prd, NULL);
		update_evpn_route_entry(
			bgp, vpn, afi, safi, global_dest, attr, NULL /* mac */,
			NULL /* ip */, 1, &pi, 0, mac_mobility_seqnum(attr),
			false /* setup_sync */, NULL /* old_is_sync */);

		/* Schedule for processing and unlock node. */
		bgp_process(bgp, global_dest, pi, afi, safi);
		bgp_dest_unlock_node(global_dest);
	}

	/* Now, walk this VNI's MAC & IP route table and use the route and its
	 * attribute to create and schedule route in global table.
	 */
	for (dest = bgp_table_top(vpn->mac_table); dest;
	     dest = bgp_route_next(dest))
		update_advertise_vni_route(bgp, vpn, dest);

	for (dest = bgp_table_top(vpn->ip_table); dest;
	     dest = bgp_route_next(dest))
		update_advertise_vni_route(bgp, vpn, dest);
}

/*
 * delete_withdraw_vni_routes -- 从全局表删除(并撤销)某 VNI 的本地路由。
 *
 * 做什么：先删除该 VNI 本地学到的 Type-2(MACIP)全局路由(delete_global_type2_routes)；
 *         再按 originator_ip 构造 Type-3 前缀，在全局表找节点并删除其路由项、
 *         调度 bgp_process 触发向对端撤销、解锁；最后删除该 VNI 的 Type-1 EAD-EVI 路由。
 * 参数：bgp 全局实例；vpn 目标 VNI。
 * 返回：固定返回 0。
 * 坑：本函数只动全局表，per-VNI 表保留路由以便用新 router-id 重新发布。
 */
/*
 * Delete (and withdraw) local routes for a VNI - only from the global
 * table. Invoked upon router-id change.
 */
static int delete_withdraw_vni_routes(struct bgp *bgp, struct bgpevpn *vpn)
{
	struct prefix_evpn p;
	struct bgp_dest *global_dest;
	struct bgp_path_info *pi;
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;

	/* Delete and withdraw locally learnt type-2 routes (MACIP)
	 * for this VNI - from the global table.
	 */
	delete_global_type2_routes(bgp, vpn);

	/* Remove type-3 route for this VNI from global table. */
	build_evpn_type3_prefix(&p, &vpn->originator_ip);
	global_dest = bgp_evpn_global_node_lookup(bgp->rib[afi][safi], safi, &p,
						  &vpn->prd, NULL);
	if (global_dest) {
		/* Delete route entry in the global EVPN table. */
		pi = delete_evpn_route_entry(bgp, afi, safi, global_dest, NULL, 0);

		/* Schedule for processing - withdraws to peers happen from
		 * this table.
		 */
		if (pi)
			bgp_process(bgp, global_dest, pi, afi, safi);
		bgp_dest_unlock_node(global_dest);
	}


	delete_global_ead_evi_routes(bgp, vpn);
	return 0;
}

/*
 * update_router_id_vni -- hash 遍历回调：对单个 VNI 处理 router-id 变更后的重发布。
 *
 * 做什么：从 bucket 取出 VNI；若其 RD 已显式配置则跳过；否则按新 router-id
 *         推导自动 RD，再调用 update_advertise_vni_routes 重建 / 发布本地路由。
 * 参数：bucket 为 VNI 哈希桶；bgp 全局实例。
 * 返回：无。供 hash 遍历(bgp_evpn_handle_router_id_update)使用。
 * 坑：显式配置 RD 的 VNI 必须跳过，否则会错误改动其 RD。
 */
/*
 * Handle router-id change. Update and advertise local routes corresponding
 * to this VNI from peers. Note that this is invoked after updating the
 * router-id. The routes in the per-VNI table are used to create routes in
 * the global table and schedule them.
 */
static void update_router_id_vni(struct hash_bucket *bucket, struct bgp *bgp)
{
	struct bgpevpn *vpn = (struct bgpevpn *)bucket->data;

	/* Skip VNIs with configured RD. */
	if (is_rd_configured(vpn))
		return;

	bgp_evpn_derive_auto_rd(bgp, vpn);
	update_advertise_vni_routes(bgp, vpn);
}

/*
 * withdraw_router_id_vni -- hash 遍历回调：对单个 VNI 处理 router-id 变更前的撤销。
 *
 * 做什么：从 bucket 取出 VNI；若其 RD 已显式配置则跳过；否则调用
 *         delete_withdraw_vni_routes 从全局表删除其所有本地路由并撤销。
 * 参数：bucket 为 VNI 哈希桶；bgp 全局实例。
 * 返回：无。供 hash 遍历使用，须在更新 router-id 之前调用。
 * 坑：per-VNI 表路由需保留，供稍后用新 router-id 重新发布。
 */
/*
 * Handle router-id change. Delete and withdraw local routes corresponding
 * to this VNI from peers. Note that this is invoked prior to updating
 * the router-id and is done only on the global route table, the routes
 * are needed in the per-VNI table to re-advertise with new router id.
 */
static void withdraw_router_id_vni(struct hash_bucket *bucket, struct bgp *bgp)
{
	struct bgpevpn *vpn = (struct bgpevpn *)bucket->data;

	/* Skip VNIs with configured RD. */
	if (is_rd_configured(vpn))
		return;

	delete_withdraw_vni_routes(bgp, vpn);
}

/*
 * advertise_withdraw_type3 -- hash 遍历回调：按泛洪模式发布 / 撤销 VNI 的 Type-3 路由。
 *
 * 做什么：取出 VNI，未存活(live)则跳过；记录 BUM 泛洪处理日志，并把泛洪模式
 *         同步给 zebra(bgp_zebra_vxlan_flood_control)；随后若为新头端复制模式，
 *         构造 Type-3 前缀并 update_evpn_route 创建 / 发布；若为禁用模式，
 *         构造 Type-3 前缀并 delete_evpn_route 删除。
 * 参数：bucket 为 VNI 哈希桶；data 实际是 struct bgp *。
 * 返回：无。副作用：按泛洪模式变化增删 Type-3 路由并同步 zebra。
 * 坑：data 是无类型指针，需按其真实类型 struct bgp * 使用。
 */
static void advertise_withdraw_type3(struct hash_bucket *bucket, void *data)
{
	struct bgpevpn *vpn = bucket->data;
	struct bgp *bgp = data;
	struct prefix_evpn p;
	int flood_control;

	if (!vpn || !is_vni_live(vpn))
		return;

	zlog_info("L2VPN EVPN BUM handling for VNI %u is %s", vpn->vni,
		  vxlan_flood_control_str(vpn->vxlan_flood_ctrl));

	bgp_zebra_vxlan_flood_control(bgp, vpn);

	flood_control = bgp_evpn_vni_flood_mode_get(bgp, vpn);

	/* Create RT-3 for a VNI and schedule for processing and advertisement.
	 * This is invoked upon flooding mode changing to head-end replication.
	 */
	if (flood_control == VXLAN_FLOOD_HEAD_END_REPL) {
		build_evpn_type3_prefix(&p, &vpn->originator_ip);
		if (update_evpn_route(bgp, vpn, &p, 0, 0, NULL))
			flog_err(EC_BGP_EVPN_ROUTE_CREATE,
				 "Type3 route creation failure for VNI %u", vpn->vni);
	} else if (flood_control == VXLAN_FLOOD_DISABLED) {
		/* Delete RT-3 for a VNI and schedule for processing and withdrawal.
		 * This is invoked upon flooding mode changing to drop BUM packets.
		 */
		build_evpn_type3_prefix(&p, &vpn->originator_ip);
		delete_evpn_route(bgp, vpn, &p);
	}
}

/*
 * process_type2_route -- 解析收到的 EVPN Type-2(MAC / IP)NLRI，做发布或撤销。
 *
 * 做什么：按报文长度与字段布局逐字段解析：RD(8) / ESI(10) / EthTag(4) /
 *         MAC 长度(1) / MAC(6) / IP 长度(1) / IP(0 或 4 或 16) /
 *         MPLS 标签(VNI，可能 1 或 2 个)。ESI 若为本地点且非旁路，则置
 *         ATTR_ES_IS_LOCAL 标志。attr 非空则 bgp_update，否则 bgp_withdraw。
 * 参数：peer 源对端；afi / safi；attr 为属性(撤销时为 NULL)；pfx / psize 为
 *       NLRI 原始字节与长度；addpath_id 为 add-path 标识。
 * 返回：成功 0；长度或字段非法返回 -1。
 * 坑：IP 长度只有 0 / 32 / 128 合法，且要与 psize 相互印证；STREAM_GET 系列
 *     失败会跳转到 stream_failure 标签统一报错并释放。
 */
/*
 * Process received EVPN type-2 route (advertise or withdraw).
 */
static int process_type2_route(struct peer *peer, afi_t afi, safi_t safi,
			       struct attr *attr, uint8_t *pfx, int psize,
			       uint32_t addpath_id)
{
	struct prefix_rd prd;
	struct prefix_evpn p = {};
	uint8_t ipaddr_len;
	uint8_t macaddr_len;
	/* holds the VNI(s) as in packet */
	mpls_label_t label[BGP_MAX_LABELS] = {};
	uint8_t num_labels = 0;
	uint32_t eth_tag;
	int ret = 0;

	/* Type-2 route should be either 33, 37 or 49 bytes or an
	 * additional 3 bytes if there is a second label (VNI):
	 * RD (8), ESI (10), Eth Tag (4), MAC Addr Len (1),
	 * MAC Addr (6), IP len (1), IP (0, 4 or 16),
	 * MPLS Lbl1 (3), MPLS Lbl2 (0 or 3)
	 */
	if (psize != 33 && psize != 37 && psize != 49 && psize != 36
	    && psize != 40 && psize != 52) {
		flog_err(EC_BGP_EVPN_ROUTE_INVALID,
			 "%u:%s - Rx EVPN Type-2 NLRI with invalid length %d",
			 peer->bgp->vrf_id, peer->host, psize);
		return -1;
	}

	struct stream *pkt = stream_new(psize);
	stream_put(pkt, pfx, psize);

	/* Make prefix_rd */
	prd.family = AF_UNSPEC;
	prd.prefixlen = 64;

	STREAM_GET(&prd.val, pkt, 8);

	/* Make EVPN prefix. */
	p.family = AF_EVPN;
	p.prefixlen = EVPN_ROUTE_PREFIXLEN;
	p.prefix.route_type = BGP_EVPN_MAC_IP_ROUTE;

	/* Copy Ethernet Seg Identifier */
	if (attr) {
		STREAM_GET(&attr->esi, pkt, sizeof(esi_t));

		if (bgp_evpn_is_esi_local_and_non_bypass(&attr->esi))
			SET_FLAG(attr->es_flags, ATTR_ES_IS_LOCAL);
		else
			UNSET_FLAG(attr->es_flags, ATTR_ES_IS_LOCAL);
	} else {
		STREAM_FORWARD_GETP(pkt, sizeof(esi_t));
	}

	/* Copy Ethernet Tag */
	STREAM_GET(&eth_tag, pkt, 4);
	p.prefix.macip_addr.eth_tag = ntohl(eth_tag);

	/* Get the MAC Addr len */
	STREAM_GETC(pkt, macaddr_len);

	/* Get the MAC Addr */
	if (macaddr_len == (ETH_ALEN * 8)) {
		STREAM_GET(&p.prefix.macip_addr.mac.octet, pkt, ETH_ALEN);
	} else {
		flog_err(
			EC_BGP_EVPN_ROUTE_INVALID,
			"%u:%s - Rx EVPN Type-2 NLRI with unsupported MAC address length %d",
			peer->bgp->vrf_id, peer->host, macaddr_len);
		goto fail;
	}


	/* Get the IP. */
	STREAM_GETC(pkt, ipaddr_len);

	if (ipaddr_len != 0 && ipaddr_len != IPV4_MAX_BITLEN
	    && ipaddr_len != IPV6_MAX_BITLEN) {
		flog_err(
			EC_BGP_EVPN_ROUTE_INVALID,
			"%u:%s - Rx EVPN Type-2 NLRI with unsupported IP address length %d",
			peer->bgp->vrf_id, peer->host, ipaddr_len);
		goto fail;
	}

	/* Validate ipaddr_len against the NLRI length */
	if ((psize != 33 + (ipaddr_len / 8)) && (psize != 36 + (ipaddr_len / 8))) {
		flog_err(EC_BGP_EVPN_ROUTE_INVALID,
			 "%u:%s - Rx EVPN Type-2 NLRI with invalid IP address length %d",
			 peer->bgp->vrf_id, peer->host, ipaddr_len);
		goto fail;
	}

	if (ipaddr_len) {
		ipaddr_len /= 8; /* Convert to bytes. */
		p.prefix.macip_addr.ip.ipa_type = (ipaddr_len == IPV4_MAX_BYTELEN)
					       ? IPADDR_V4
					       : IPADDR_V6;
		STREAM_GET(&p.prefix.macip_addr.ip.ip.addr, pkt, ipaddr_len);
	}

	/* Get the VNI(s). Stored as bytes here. */
	STREAM_GET(&label[0], pkt, BGP_LABEL_BYTES);
	num_labels++;

	/* Do we have a second VNI? */
	if (STREAM_READABLE(pkt)) {
		num_labels++;
		STREAM_GET(&label[1], pkt, BGP_LABEL_BYTES);
	}

	/* Process the route. */
	if (attr)
		bgp_update(peer, (struct prefix *)&p, addpath_id, attr, afi,
			   safi, ZEBRA_ROUTE_BGP, BGP_ROUTE_NORMAL, &prd,
			   &label[0], num_labels, 0, NULL);
	else
		bgp_withdraw(peer, (struct prefix *)&p, addpath_id, afi, safi,
			     ZEBRA_ROUTE_BGP, BGP_ROUTE_NORMAL, &prd, &label[0],
			     num_labels);
	goto done;

fail:
stream_failure:
	flog_err(EC_BGP_EVPN_ROUTE_INVALID,
		 "%u:%s - Rx EVPN Type-2 NLRI - corrupt, discarding",
		 peer->bgp->vrf_id, peer->host);
	ret = -1;
done:
	stream_free(pkt);
	return ret;
}

/*
 * process_type3_route -- 解析收到的 EVPN Type-3(IMET)NLRI，做发布或撤销。
 *
 * 做什么：按布局解析 RD(8) / EthTag(4) / IP 长度(1) / IP(4 或 16)，合法长度为
 *         17 或 29 字节。若带 PMSI 隧道属性且类型不是 IR / PIM-SM，仅记录告警。
 *         构造 IMET 前缀后，attr 非空则 bgp_update，否则 bgp_withdraw。
 * 参数：peer 源对端；afi / safi；attr(撤销时为 NULL)；pfx / psize；addpath_id。
 * 返回：成功 0；长度或 IP 长度非法返回 -1。
 * 坑：ipaddr_len 必须换算为 13 + len/8 才与 psize 相等，校验不通过直接丢弃。
 */
/*
 * Process received EVPN type-3 route (advertise or withdraw).
 */
static int process_type3_route(struct peer *peer, afi_t afi, safi_t safi,
			       struct attr *attr, uint8_t *pfx, int psize,
			       uint32_t addpath_id)
{
	struct prefix_rd prd;
	struct prefix_evpn p;
	uint8_t ipaddr_len;
	uint32_t eth_tag;

	/* Type-3 route should be either 17 or 29 bytes: RD (8), Eth Tag (4),
	 * IP len (1) and IP (4 or 16).
	 */
	if (psize != 17 && psize != 29) {
		flog_err(EC_BGP_EVPN_ROUTE_INVALID,
			 "%u:%s - Rx EVPN Type-3 NLRI with invalid length %d",
			 peer->bgp->vrf_id, peer->host, psize);
		return -1;
	}

	/* If PMSI is present, log if it is anything other than IR.
	 * Note: We just simply ignore the values as it is not clear if
	 * doing anything else is better.
	 */
	if (attr && bgp_attr_exists(attr, BGP_ATTR_PMSI_TUNNEL)) {
		enum pta_type pmsi_tnl_type = bgp_attr_get_pmsi_tnl_type(attr);

		if (pmsi_tnl_type != PMSI_TNLTYPE_INGR_REPL
		    && pmsi_tnl_type != PMSI_TNLTYPE_PIM_SM) {
			flog_warn(
				EC_BGP_EVPN_PMSI_PRESENT,
				"%u:%s - Rx EVPN Type-3 NLRI with unsupported PTA %d",
				peer->bgp->vrf_id, peer->host, pmsi_tnl_type);
		}
	}

	/* Make prefix_rd */
	prd.family = AF_UNSPEC;
	prd.prefixlen = 64;
	memcpy(&prd.val, pfx, 8);
	pfx += 8;

	/* Make EVPN prefix. */
	memset(&p, 0, sizeof(p));
	p.family = AF_EVPN;
	p.prefixlen = EVPN_ROUTE_PREFIXLEN;
	p.prefix.route_type = BGP_EVPN_IMET_ROUTE;

	/* Copy Ethernet Tag */
	memcpy(&eth_tag, pfx, 4);
	p.prefix.imet_addr.eth_tag = ntohl(eth_tag);
	pfx += 4;

	/* Get the IP. */
	ipaddr_len = *pfx++;

	/* Validate */
	if (psize != 13 + (ipaddr_len / 8)) {
		flog_err(EC_BGP_EVPN_ROUTE_INVALID,
			 "%u:%s - Rx EVPN Type-3 NLRI with invalid IP address length %d",
			 peer->bgp->vrf_id, peer->host, ipaddr_len);
		return -1;
	}

	if (ipaddr_len == IPV4_MAX_BITLEN) {
		SET_IPADDR_V4(&p.prefix.imet_addr.ip);
		memcpy(&p.prefix.imet_addr.ip.ip.addr, pfx, IPV4_MAX_BYTELEN);
	} else if (ipaddr_len == IPV6_MAX_BITLEN) {
		SET_IPADDR_V6(&p.prefix.imet_addr.ip);
		IPV6_ADDR_COPY(&p.prefix.imet_addr.ip.ipaddr_v6, pfx);
	} else {
		flog_err(
			EC_BGP_EVPN_ROUTE_INVALID,
			"%u:%s - Rx EVPN Type-3 NLRI with unsupported IP address length %d",
			peer->bgp->vrf_id, peer->host, ipaddr_len);
		return -1;
	}

	/* Process the route. */
	if (attr)
		bgp_update(peer, (struct prefix *)&p, addpath_id, attr, afi,
			   safi, ZEBRA_ROUTE_BGP, BGP_ROUTE_NORMAL, &prd, NULL,
			   0, 0, NULL);
	else
		bgp_withdraw(peer, (struct prefix *)&p, addpath_id, afi, safi,
			     ZEBRA_ROUTE_BGP, BGP_ROUTE_NORMAL, &prd, NULL, 0);
	return 0;
}

/*
 * process_type5_route -- 解析收到的 EVPN Type-5(IP 前缀)NLRI，做发布或撤销。
 *
 * 做什么：按布局解析 RD(8) / ESI(10) / EthTag(4) / IP 长度(1) / IP(4 或 16) /
 *         网关 IP(4 或 16) / VNI(3)，合法长度 34 或 58。分配 bgp_route_evpn
 *         叠加信息 evpn；据 ESI / 网关是否有效设置 OVERLAY_INDEX_ESI 或
 *         OVERLAY_INDEX_GATEWAY_IP。校验非法组合(ESI 与网关同时非零、四元组
 *         全零、组播 / 广播 RMAC)，非法则标记 is_valid_update=false。
 * 参数：peer 源对端；afi / safi；attr(撤销时为 NULL)；pfx / psize；addpath_id。
 * 返回：固定返回 0；非法时按撤回处理并释放 evpn。
 * 坑：非法的 update 会被当作 withdraw 处理并打出告警，属预期行为而非错误。
 */
/*
 * Process received EVPN type-5 route (advertise or withdraw).
 */
static int process_type5_route(struct peer *peer, afi_t afi, safi_t safi,
			       struct attr *attr, uint8_t *pfx, int psize,
			       uint32_t addpath_id)
{
	struct prefix_rd prd;
	struct prefix_evpn p;
	struct bgp_route_evpn *evpn = XCALLOC(MTYPE_BGP_EVPN_OVERLAY,
					      sizeof(struct bgp_route_evpn));
	uint8_t ippfx_len;
	uint32_t eth_tag;
	mpls_label_t label; /* holds the VNI as in the packet */
	bool is_valid_update = true;

	/* Type-5 route should be 34 or 58 bytes:
	 * RD (8), ESI (10), Eth Tag (4), IP len (1), IP (4 or 16),
	 * GW (4 or 16) and VNI (3).
	 * Note that the IP and GW should both be IPv4 or both IPv6.
	 */
	if (psize != 34 && psize != 58) {
		flog_err(EC_BGP_EVPN_ROUTE_INVALID,
			 "%u:%s - Rx EVPN Type-5 NLRI with invalid length %d",
			 peer->bgp->vrf_id, peer->host, psize);
		evpn_overlay_free(evpn);
		return -1;
	}

	/* Make prefix_rd */
	prd.family = AF_UNSPEC;
	prd.prefixlen = 64;
	memcpy(&prd.val, pfx, 8);
	pfx += 8;

	/* Make EVPN prefix. */
	memset(&p, 0, sizeof(p));
	p.family = AF_EVPN;
	p.prefixlen = EVPN_ROUTE_PREFIXLEN;
	p.prefix.route_type = BGP_EVPN_IP_PREFIX_ROUTE;

	/* Fetch ESI overlay index */
	if (attr)
		memcpy(&evpn->eth_s_id, pfx, sizeof(esi_t));
	pfx += ESI_BYTES;

	/* Fetch Ethernet Tag. */
	memcpy(&eth_tag, pfx, 4);
	p.prefix.prefix_addr.eth_tag = ntohl(eth_tag);
	pfx += 4;

	/* Fetch IP prefix length. */
	ippfx_len = *pfx++;
	if (ippfx_len > IPV6_MAX_BITLEN) {
		flog_err(
			EC_BGP_EVPN_ROUTE_INVALID,
			"%u:%s - Rx EVPN Type-5 NLRI with invalid IP Prefix length %d",
			peer->bgp->vrf_id, peer->host, ippfx_len);
		evpn_overlay_free(evpn);
		return -1;
	}
	p.prefix.prefix_addr.ip_prefix_length = ippfx_len;

	/* Determine IPv4 or IPv6 prefix */
	/* Since the address and GW are from the same family, this just becomes
	 * a simple check on the total size.
	 */
	if (psize == 34) {
		SET_IPADDR_V4(&p.prefix.prefix_addr.ip);
		memcpy(&p.prefix.prefix_addr.ip.ipaddr_v4, pfx, 4);
		pfx += 4;
		SET_IPADDR_V4(&evpn->gw_ip);
		memcpy(&evpn->gw_ip.ipaddr_v4, pfx, 4);
		pfx += 4;
	} else {
		SET_IPADDR_V6(&p.prefix.prefix_addr.ip);
		memcpy(&p.prefix.prefix_addr.ip.ipaddr_v6, pfx,
		       IPV6_MAX_BYTELEN);
		pfx += IPV6_MAX_BYTELEN;
		SET_IPADDR_V6(&evpn->gw_ip);
		memcpy(&evpn->gw_ip.ipaddr_v6, pfx, IPV6_MAX_BYTELEN);
		pfx += IPV6_MAX_BYTELEN;
	}

	/* Get the VNI (in MPLS label field). Stored as bytes here. */
	memset(&label, 0, sizeof(label));
	memcpy(&label, pfx, BGP_LABEL_BYTES);

	/*
	 * If in future, we are required to access additional fields,
	 * we MUST increment pfx by BGP_LABEL_BYTES in before reading the next
	 * field
	 */

	/*
	 * An update containing a non-zero gateway IP and a non-zero ESI
	 * at the same time is should be treated as withdraw
	 */
	if (bgp_evpn_is_esi_valid(&evpn->eth_s_id) &&
	    !ipaddr_is_zero(&evpn->gw_ip)) {
		flog_err(EC_BGP_EVPN_ROUTE_INVALID,
			 "%s - Rx EVPN Type-5 ESI and gateway-IP both non-zero.",
			 peer->host);
		is_valid_update = false;
	} else if (bgp_evpn_is_esi_valid(&evpn->eth_s_id))
		evpn->type = OVERLAY_INDEX_ESI;
	else if (!ipaddr_is_zero(&evpn->gw_ip))
		evpn->type = OVERLAY_INDEX_GATEWAY_IP;
	if (attr) {
		if (is_zero_mac(&attr->rmac) &&
		    !bgp_evpn_is_esi_valid(&evpn->eth_s_id) &&
		    ipaddr_is_zero(&evpn->gw_ip) && label == 0) {
			flog_err(EC_BGP_EVPN_ROUTE_INVALID,
				 "%s - Rx EVPN Type-5 ESI, gateway-IP, RMAC and label all zero",
				 peer->host);
			is_valid_update = false;
		}

		if (is_mcast_mac(&attr->rmac) || is_bcast_mac(&attr->rmac))
			is_valid_update = false;
	}

	/* Process the route. */
	if (attr && is_valid_update)
		bgp_update(peer, (struct prefix *)&p, addpath_id, attr, afi,
			   safi, ZEBRA_ROUTE_BGP, BGP_ROUTE_NORMAL, &prd,
			   &label, 1, 0, evpn);
	else {
		if (!is_valid_update) {
			char attr_str[BUFSIZ] = {0};

			bgp_dump_attr(attr, attr_str, BUFSIZ);
			zlog_warn(
				"Invalid update from peer %s vrf %u prefix %pFX attr %s - treat as withdraw",
				peer->hostname, peer->bgp->vrf_id, &p,
				attr_str);
		}
		bgp_withdraw(peer, (struct prefix *)&p, addpath_id, afi, safi,
			     ZEBRA_ROUTE_BGP, BGP_ROUTE_NORMAL, &prd, &label, 1);
		evpn_overlay_free(evpn);
	}

	return 0;
}

/*
 * evpn_mpattr_encode_type5 -- 把一条 EVPN Type-5 路由编码进发送流。
 *
 * 做什么：仅当 p->family == AF_EVPN 才编码。先写总长度，再依次写 RD、
 *         ESI(属性为 OVERLAY_INDEX_ESI 时取 attr->esi，否则写全零)、
 *         EthTag、IP 前缀长度、IP 前缀、网关 IP(属性为 OVERLAY_INDEX_GATEWAY_IP
 *         时取 bre->gw_ip，否则写零)、最后 3 字节 VNI 标签。
 * 参数：s 目标流；p EVPN 前缀；prd RD；label / num_labels 标签与个数；attr 属性。
 * 返回：无。
 * 坑：IP 与网关 IP 必须同为 IPv4 或同为 IPv6，len 据此取 8 或 32。
 */
static void evpn_mpattr_encode_type5(struct stream *s, const struct prefix *p,
				     const struct prefix_rd *prd,
				     mpls_label_t *label, uint8_t num_labels,
				     struct attr *attr)
{
	int len;
	char temp[16];
	const struct evpn_addr *p_evpn_p;
	struct bgp_route_evpn *bre = NULL;

	memset(&temp, 0, sizeof(temp));
	if (p->family != AF_EVPN)
		return;
	p_evpn_p = &(p->u.prefix_evpn);

	if (attr)
		bre = bgp_attr_get_evpn_overlay(attr);

	/* len denites the total len of IP and GW-IP in the route
	   IP and GW-IP have to be both ipv4 or ipv6
	 */
	if (IS_IPADDR_V4(&p_evpn_p->prefix_addr.ip))
		len = 8; /* IP and GWIP are both ipv4 */
	else
		len = 32; /* IP and GWIP are both ipv6 */
	/* Prefix contains RD, ESI, EthTag, IP length, IP, GWIP and VNI */
	stream_putc(s, 8 + 10 + 4 + 1 + len + 3);
	stream_put(s, prd->val, 8);
	if (attr && bre && bre->type == OVERLAY_INDEX_ESI)
		stream_put(s, &attr->esi, sizeof(esi_t));
	else
		stream_put(s, 0, sizeof(esi_t));
	stream_putl(s, p_evpn_p->prefix_addr.eth_tag);
	stream_putc(s, p_evpn_p->prefix_addr.ip_prefix_length);
	if (IS_IPADDR_V4(&p_evpn_p->prefix_addr.ip))
		stream_put_ipv4(s, p_evpn_p->prefix_addr.ip.ipaddr_v4.s_addr);
	else
		stream_put(s, &p_evpn_p->prefix_addr.ip.ipaddr_v6, 16);
	if (attr && bre && bre->type == OVERLAY_INDEX_GATEWAY_IP) {
		if (IS_IPADDR_V4(&p_evpn_p->prefix_addr.ip))
			stream_put_ipv4(s, bre->gw_ip.ipaddr_v4.s_addr);
		else
			stream_put(s, &(bre->gw_ip.ipaddr_v6), 16);
	} else {
		if (IS_IPADDR_V4(&p_evpn_p->prefix_addr.ip))
			stream_put_ipv4(s, 0);
		else
			stream_put(s, &temp, 16);
	}

	if (num_labels)
		stream_put(s, label, 3);
	else
		stream_put3(s, 0);
}

/*
 * bgp_zebra_evpn_pop_items_from_announce_fifo -- 从 zebra 通告 FIFO 中弹出属于某 VNI
 * 的全部待处理 VPN 路由条目。
 *
 * 做什么：遍历 bm->zebra_announce_head 链表，凡是 za_vpn 指向该 VNI 的节点，
 *         从链表删除并对其 za_bgp_pi 与 dest 做解锁。
 * 参数：vpn 目标 VNI。
 * 返回：无。副作用：清空该 VNI 在 FIFO 中的待通告项，避免悬空引用。
 * 坑：'no advertise-all-vni' 与 L2 VNI 删除时必须调用，否则 FIFO 里残留项会
 *     指向已释放的 VNI。
 */
/*
 * In cases such as 'no advertise-all-vni' and L2 VNI DELETE, we need to
 * pop all the VPN routes present in the bgp_zebra_announce FIFO yet to
 * be processed regardless of VNI is configured or not.
 *
 * NOTE: NO need to pop the VPN routes in two cases
 *  1) In free_vni_entry
 *     - Called by bgp_free()->bgp_evpn_cleanup().
 *     - Since bgp_delete is called before bgp_free and we pop all the dest
 *       pertaining to bgp under delete.
 *  2) evpn_delete_vni() when user configures "no vni" since the withdraw
 *     of all routes happen in normal cycle.
 */
void bgp_zebra_evpn_pop_items_from_announce_fifo(struct bgpevpn *vpn)
{
	struct bgp_dest *dest = NULL;
	struct bgp_dest *dest_next = NULL;

	for (dest = zebra_announce_first(&bm->zebra_announce_head); dest; dest = dest_next) {
		dest_next = zebra_announce_next(&bm->zebra_announce_head, dest);
		if (dest->za_vpn == vpn) {
			zebra_announce_del(&bm->zebra_announce_head, dest);
			bgp_path_info_unlock(dest->za_bgp_pi);
			bgp_dest_unlock_node(dest);
		}
	}
}
/*
 * cleanup_vni_on_disable -- hash 遍历回调：EVPN 被禁用(如 no advertise-all-vni)时清理单个 VNI。
 *
 * 做什么：删除该 VNI 的路由并调度处理；清除 LIVE 标志；从 zebra 通告 FIFO 弹出
 *         残留项；若该 VNI 未被显式配置(且已无引用)则释放它。
 * 参数：bucket 为 VNI 哈希桶；bgp 全局实例。
 * 返回：无。
 * 坑：需先清 FIFO 再判断释放，避免释放后 FIFO 残留悬空指针。
 */
/*
 * Cleanup specific VNI upon EVPN (advertise-all-vni) being disabled.
 */
static void cleanup_vni_on_disable(struct hash_bucket *bucket, struct bgp *bgp)
{
	struct bgpevpn *vpn = (struct bgpevpn *)bucket->data;

	/* Remove EVPN routes and schedule for processing. */
	delete_routes_for_vni(bgp, vpn);

	/* Clear "live" flag and see if hash needs to be freed. */
	UNSET_FLAG(vpn->flags, VNI_FLAG_LIVE);
	/* Pop items from bgp_zebra_announce FIFO for any VPN routes pending*/
	bgp_zebra_evpn_pop_items_from_announce_fifo(vpn);
	if (!is_vni_configured(vpn))
		bgp_evpn_free(bgp, vpn);
}

/*
 * free_vni_entry -- hash 遍历回调：清理阶段删除 VNI 全部路由并释放 VNI 项。
 *
 * 做什么：取出 bucket 中的 VNI，调用 delete_all_vni_routes 删除其所有路由，
 *         再 bgp_evpn_free 释放。
 * 参数：bucket 为 VNI 哈希桶；bgp 全局实例。
 * 返回：无。
 * 坑：这是最终释放路径，需保证已无其它引用，否则会悬空。
 */
/*
 * Free a VNI entry; iterator function called during cleanup.
 */
static void free_vni_entry(struct hash_bucket *bucket, struct bgp *bgp)
{
	struct bgpevpn *vpn = (struct bgpevpn *)bucket->data;

	delete_all_vni_routes(bgp, vpn);
	bgp_evpn_free(bgp, vpn);
}

/*
 * evpn_auto_rt_import_add_for_vrf -- 为 VRF(L3VNI)生成自动 import RT 并建立映射。
 *
 * 做什么：用 form_auto_rt 依据 l3vni 推导自动 import RT 加入 vrf_import_rtl；
 *         取全局 EVPN 实例，存在则 bgp_evpn_map_vrf_to_its_rts 建立 RT 到 VRF 的映射。
 * 参数：bgp_vrf 为 VRF 对应的 BGP 实例。
 * 返回：无。
 * 坑：无全局 EVPN 实例时只生成 RT、跳过映射。
 */
/*
 * Derive AUTO import RT for BGP VRF - L3VNI
 */
static void evpn_auto_rt_import_add_for_vrf(struct bgp *bgp_vrf)
{
	struct bgp *bgp_evpn = NULL;

	form_auto_rt(bgp_vrf, bgp_vrf->l3vni, bgp_vrf->vrf_import_rtl, true);

	/* Map RT to VRF */
	bgp_evpn = bgp_get_evpn();

	if (!bgp_evpn)
		return;

	bgp_evpn_map_vrf_to_its_rts(bgp_vrf);
}

/*
 * evpn_auto_rt_import_delete_for_vrf -- 删除 VRF(L3VNI)的自动 import RT。
 *
 * 做什么：调用 evpn_rt_delete_auto，从 vrf_import_rtl 中移除该 VRF 依据 l3vni
 *         自动推导出的 import RT。
 * 参数：bgp_vrf 为 VRF 对应的 BGP 实例。
 * 返回：无。
 * 坑：只删自动生成的 RT，显式配置的 RT 不受影响。
 */
/*
 * Delete AUTO import RT from BGP VRF - L3VNI
 */
static void evpn_auto_rt_import_delete_for_vrf(struct bgp *bgp_vrf)
{
	evpn_rt_delete_auto(bgp_vrf, bgp_vrf->l3vni, bgp_vrf->vrf_import_rtl,
			    true);
}

/*
 * evpn_auto_rt_export_add_for_vrf -- 为 VRF(L3VNI)生成自动 export RT。
 *
 * 做什么：用 form_auto_rt 依据 l3vni 推导自动 export RT 加入 vrf_export_rtl。
 * 参数：bgp_vrf 为 VRF 对应的 BGP 实例。
 * 返回：无。
 * 坑：export RT 只影响本地对外发布，无需建立 RT 到 VRF 的映射。
 */
/*
 * Derive AUTO export RT for BGP VRF - L3VNI
 */
static void evpn_auto_rt_export_add_for_vrf(struct bgp *bgp_vrf)
{
	form_auto_rt(bgp_vrf, bgp_vrf->l3vni, bgp_vrf->vrf_export_rtl, true);
}

/*
 * evpn_auto_rt_export_delete_for_vrf -- 删除 VRF(L3VNI)的自动 export RT。
 *
 * 做什么：调用 evpn_rt_delete_auto，从 vrf_export_rtl 中移除依据 l3vni 自动推导的 export RT。
 * 参数：bgp_vrf 为 VRF 对应的 BGP 实例。
 * 返回：无。
 * 坑：只删自动生成的 RT，显式配置的 RT 不受影响。
 */
/*
 * Delete AUTO export RT from BGP VRF - L3VNI
 */
static void evpn_auto_rt_export_delete_for_vrf(struct bgp *bgp_vrf)
{
	evpn_rt_delete_auto(bgp_vrf, bgp_vrf->l3vni, bgp_vrf->vrf_export_rtl,
			    true);
}

/*
 * bgp_evpn_handle_export_rt_change_for_vrf -- VRF 的 export RT 变化后，重新发布其路由。
 *
 * 做什么：取全局 EVPN 实例，不存在则返回；调用 update_advertise_vrf_routes 重发
 *         该 VRF 的全部 Type-5 路由；再遍历该 VRF 的 l2vnis 列表，对每个 VNI
 *         调用 update_routes_for_vni 重发其 Type-2 路由。
 * 参数：bgp_vrf 为 VRF 对应的 BGP 实例。
 * 返回：无。
 * 坑：需同时刷新 Type-5(VRF 级)与 Type-2(依附的 L2VNI 级)两类路由。
 */
static void bgp_evpn_handle_export_rt_change_for_vrf(struct bgp *bgp_vrf)
{
	struct bgp *bgp_evpn = NULL;
	struct listnode *node = NULL;
	struct bgpevpn *vpn = NULL;

	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn)
		return;

	/* update all type-5 routes */
	update_advertise_vrf_routes(bgp_vrf);

	/* update all type-2 routes */
	for (ALL_LIST_ELEMENTS_RO(bgp_vrf->l2vnis, node, vpn))
		update_routes_for_vni(bgp_evpn, vpn);
}

/*
 * update_autort_vni -- hash 遍历回调：处理单个 VNI 的 autort(自动 RT)变化。
 *
 * 做什么：若 import RT 非显式配置，则先卸载该 VNI 路由、解除 RT 映射、清空
 *         import_rtl、重新推导自动 import RT，若 VNI 存活再重新安装路由；
 *         若 export RT 非显式配置，则清空 export_rtl、重新推导自动 export RT，
 *         若 VNI 存活则触发 export RT 变化处理。
 * 参数：bucket 为 VNI 哈希桶；bgp 全局实例。
 * 返回：无。
 * 坑：import / export 两个分支相互独立，仅对"自动推导"的 RT 生效。
 */
/*
 * Handle autort change for a given VNI.
 */
static void update_autort_vni(struct hash_bucket *bucket, struct bgp *bgp)
{
	struct bgpevpn *vpn = bucket->data;

	if (!is_import_rt_configured(vpn)) {
		if (is_vni_live(vpn))
			bgp_evpn_uninstall_routes(bgp, vpn);
		bgp_evpn_unmap_vni_from_its_rts(bgp, vpn);
		list_delete_all_node(vpn->import_rtl);
		bgp_evpn_derive_auto_rt_import(bgp, vpn);
		if (is_vni_live(vpn))
			bgp_evpn_install_routes(bgp, vpn);
	}
	if (!is_export_rt_configured(vpn)) {
		list_delete_all_node(vpn->export_rtl);
		bgp_evpn_derive_auto_rt_export(bgp, vpn);
		if (is_vni_live(vpn))
			bgp_evpn_handle_export_rt_change(bgp, vpn);
	}
}

/*
 * update_autort_l3vni -- 处理 L3VNI(VRF)的 autort(自动 RT)变化。
 *
 * 做什么：若 import 与 export RT 均为显式配置则直接返回。对非配置的一侧：
 *         import 侧先卸载该 VRF 路由、解除映射、删旧自动 RT、清空表、再重建自动 RT；
 *         export 侧清空表、删旧自动 RT、重建自动 RT，若 L3VNI 存活则重建映射。
 *         最后若 L3VNI 存活，重发 Type-5 路由并把该 L3VNI 的远端路由装回 VRF。
 * 参数：bgp 为 VRF 对应的 BGP 实例。
 * 返回：无。
 * 坑：两侧判定相互独立；L3VNI 不存活时只重建 RT、不做路由发布 / 安装。
 */
/*
 * Handle autort change for L3VNI.
 */
static void update_autort_l3vni(struct bgp *bgp)
{
	if ((CHECK_FLAG(bgp->vrf_flags, BGP_VRF_IMPORT_RT_CFGD))
	    && (CHECK_FLAG(bgp->vrf_flags, BGP_VRF_EXPORT_RT_CFGD)))
		return;

	if (!CHECK_FLAG(bgp->vrf_flags, BGP_VRF_IMPORT_RT_CFGD)) {
		if (is_l3vni_live(bgp))
			uninstall_routes_for_vrf(bgp);

		/* Cleanup the RT to VRF mapping */
		bgp_evpn_unmap_vrf_from_its_rts(bgp);

		/* Remove auto generated RT */
		evpn_auto_rt_import_delete_for_vrf(bgp);

		list_delete_all_node(bgp->vrf_import_rtl);

		/* Map auto derive or configured RTs */
		evpn_auto_rt_import_add_for_vrf(bgp);
	}

	if (!CHECK_FLAG(bgp->vrf_flags, BGP_VRF_EXPORT_RT_CFGD)) {
		list_delete_all_node(bgp->vrf_export_rtl);

		evpn_auto_rt_export_delete_for_vrf(bgp);

		evpn_auto_rt_export_add_for_vrf(bgp);

		if (is_l3vni_live(bgp))
			bgp_evpn_map_vrf_to_its_rts(bgp);
	}

	if (!is_l3vni_live(bgp))
		return;

	/* advertise type-5 routes if needed */
	update_advertise_vrf_routes(bgp);

	/* install all remote routes belonging to this l3vni
	 * into corresponding vrf
	 */
	install_routes_for_vrf(bgp);
}

/*
 * Public functions.
 */

/*
 * bgp_evpn_withdraw_type5_route -- 撤销单个 IP 前缀对应的 Type-5 路由。
 *
 * 做什么：由 IP 前缀构造 Type-5 EVPN 前缀，调用 delete_evpn_type5_route 删除；
 *         失败则打错误日志。
 * 参数：bgp_vrf VRF 实例；originator 原始路径信息；p IP 前缀；afi / safi；addpath_id。
 * 返回：无。
 * 坑：删除失败只记日志，不改变控制流。
 */
/* withdraw type-5 route corresponding to ip prefix */
void bgp_evpn_withdraw_type5_route(struct bgp *bgp_vrf, const struct bgp_path_info *originator,
				   const struct prefix *p, afi_t afi, safi_t safi,
				   uint32_t addpath_id)
{
	int ret = 0;
	struct prefix_evpn evp;

	build_type5_prefix_from_ip_prefix(&evp, p);
	ret = delete_evpn_type5_route(bgp_vrf, originator, &evp, addpath_id);
	if (ret)
		flog_err(
			EC_BGP_EVPN_ROUTE_DELETE,
			"%u failed to delete type-5 route for prefix %pFX in vrf %s",
			bgp_vrf->vrf_id, p, vrf_id_to_name(bgp_vrf->vrf_id));
}

/*
 * bgp_evpn_withdraw_type5_routes -- 撤销某地址族下全部可注入 EVPN 的 Type-5 路由。
 *
 * 做什么：遍历该 VRF 的 afi / safi 路由表；对每个 dest 的路径，仅处理
 *         is_route_injectable_into_evpn 为真的路径，计算 addpath_id 后调用
 *         bgp_evpn_withdraw_type5_route 撤销；若为 bestpath 模式则每个 dest 只撤一条。
 * 参数：bgp_vrf VRF 实例；afi / safi 地址族。
 * 返回：无。
 * 坑：bestpath 模式下撤完首条即 break，避免多路径模式下重复撤销。
 */
/* withdraw all type-5 routes for an address family */
void bgp_evpn_withdraw_type5_routes(struct bgp *bgp_vrf, afi_t afi, safi_t safi)
{
	struct bgp_table *table = NULL;
	struct bgp_dest *dest = NULL;
	struct bgp_path_info *pi;
	uint32_t addpath_id;

	table = bgp_vrf->rib[afi][safi];
	for (dest = bgp_table_top(table); dest; dest = bgp_route_next(dest)) {
		/* Only care about "selected" and "multipath" routes. Also
		 * ensure that these are routes that are injectable into EVPN.
		 */
		for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next) {
			if (!is_route_injectable_into_evpn(pi))
				continue;
			addpath_id = bgp_evpn_addpath_id_for_path(bgp_vrf, pi, afi);
			bgp_evpn_withdraw_type5_route(bgp_vrf, pi, bgp_dest_get_prefix(dest), afi,
						      safi, addpath_id);

			if (advertise_type5_routes_bestpath(bgp_vrf, afi))
				break;
		}
	}
}

/*
 * bgp_evpn_install_uninstall_default_route -- 安装 / 卸载 EVPN 默认路由(默认网关通告)。
 *
 * 做什么：构造默认前缀 0.0.0.0/0(地址族由 afi 决定)；add 为真则调用
 *         bgp_evpn_advertise_type5_route 以 Type-5 形式通告默认路由，
 *         为假则调用 bgp_evpn_withdraw_type5_route 撤销。
 * 参数：bgp_vrf VRF 实例；afi / safi；originator；add 表示安装或卸载。
 * 返回：无。
 * 坑：默认路由以全零前缀表示，addpath_id 传 0。
 */
/*
 * evpn - enable advertisement of default g/w
 */
void bgp_evpn_install_uninstall_default_route(struct bgp *bgp_vrf, afi_t afi, safi_t safi,
					      struct bgp_path_info *originator, bool add)
{
	struct prefix ip_prefix;

	/* form the default prefix 0.0.0.0/0 */
	memset(&ip_prefix, 0, sizeof(ip_prefix));
	ip_prefix.family = afi2family(afi);

	if (add)
		bgp_evpn_advertise_type5_route(bgp_vrf, originator, &ip_prefix, NULL, afi, safi, 0);
	else
		bgp_evpn_withdraw_type5_route(bgp_vrf, originator, &ip_prefix, afi, safi, 0);
}


/*
 * bgp_evpn_advertise_type5_route -- 把单个 IP 前缀作为 Type-5 路由通告。
 *
 * 做什么：由 IP 前缀构造 Type-5 EVPN 前缀，调用 update_evpn_type5_route 创建 / 更新；
 *         失败则打错误日志。
 * 参数：bgp_vrf VRF 实例；originator 原始路径；p IP 前缀；src_attr 源属性
 *       (本地子网通告时为 NULL)；afi / safi；addpath_id。
 * 返回：无。
 * 坑：afi / safi / src_attr 需与源 IP 前缀(通常是最优路径)一致。
 */
/*
 * Advertise IP prefix as type-5 route. The afi/safi and src_attr passed
 * to this function correspond to those of the source IP prefix (best
 * path in the case of the attr. In the case of a local prefix (when we
 * are advertising local subnets), the src_attr will be NULL.
 */
void bgp_evpn_advertise_type5_route(struct bgp *bgp_vrf, struct bgp_path_info *originator,
				    const struct prefix *p, struct attr *src_attr, afi_t afi,
				    safi_t safi, uint32_t addpath_id)
{
	int ret = 0;
	struct prefix_evpn evp;

	build_type5_prefix_from_ip_prefix(&evp, p);
	ret = update_evpn_type5_route(bgp_vrf, originator, &evp, src_attr, afi, safi, addpath_id);
	if (ret)
		flog_err(EC_BGP_EVPN_ROUTE_CREATE,
			 "%u: Failed to create type-5 route for prefix %pFX",
			 bgp_vrf->vrf_id, p);
}

/*
 * bgp_evpn_advertise_type5_routes -- 把某地址族(IPv4 / IPv6 unicast)的全部前缀注入 EVPN(Type-5)。
 *
 * 做什么：遍历该 VRF 的 afi / safi 路由表；对每个 dest 的路径，跳过不可注入 EVPN 的、
 *         以及既非 SELECTED 又非 MULTIPATH 的路径；对其余路径调用
 *         bgp_evpn_export_type5_route 注入；bestpath 模式下每个 dest 只处理一条。
 * 参数：bgp_vrf VRF 实例；afi / safi 地址族。
 * 返回：无。
 * 坑：通告启用时调用；只有被选中的路由才注入，避免把备用路径也发出去。
 */
/* Inject all prefixes of a particular address-family (currently, IPv4 or
 * IPv6 unicast) into EVPN as type-5 routes. This is invoked when the
 * advertisement is enabled.
 */
void bgp_evpn_advertise_type5_routes(struct bgp *bgp_vrf, afi_t afi,
				     safi_t safi)
{
	struct bgp_table *table = NULL;
	struct bgp_dest *dest = NULL;
	struct bgp_path_info *pi;

	table = bgp_vrf->rib[afi][safi];
	for (dest = bgp_table_top(table); dest; dest = bgp_route_next(dest)) {
		/* Need to identify the "selected" route entry to use its
		 * attribute. Also, ensure that the route is injectable
		 * into EVPN.
		 */
		for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next) {
			if (!is_route_injectable_into_evpn(pi))
				continue;

			if (!CHECK_FLAG(pi->flags, BGP_PATH_SELECTED) &&
			    !CHECK_FLAG(pi->flags, BGP_PATH_MULTIPATH))
				continue;

			bgp_evpn_export_type5_route(bgp_vrf, dest, pi, afi, safi);

			if (advertise_type5_routes_bestpath(bgp_vrf, afi))
				break;
		}
	}
}

/*
 * rt_list_remove_node -- 从 RT 链表中删除与给定 ecommunity 匹配的节点。
 *
 * 做什么：is_l3 为真时链表元素是 vrf_route_target，用 ecommunity_match 匹配后
 *         evpn_vrf_rt_del；为假时元素就是 ecommunity，匹配后 ecommunity_free。
 *         命中后跳出并把该节点从链表删除。
 * 参数：rt_list 目标链表；ecomdel 待删的 RT；is_l3 是否为 L3(VRF)RT 链表。
 * 返回：无。
 * 坑：只删第一个匹配项；两种链表元素类型不同，释放函数也不同。
 */
static void rt_list_remove_node(struct list *rt_list,
				struct ecommunity *ecomdel, bool is_l3)
{
	struct listnode *node = NULL, *nnode = NULL, *node_to_del = NULL;
	struct vrf_route_target *l3rt = NULL;
	struct ecommunity *ecom = NULL;

	if (is_l3) {
		for (ALL_LIST_ELEMENTS(rt_list, node, nnode, l3rt)) {
			if (ecommunity_match(l3rt->ecom, ecomdel)) {
				evpn_vrf_rt_del(l3rt);
				node_to_del = node;
				break;
			}
		}
	} else {
		for (ALL_LIST_ELEMENTS(rt_list, node, nnode, ecom)) {
			if (ecommunity_match(ecom, ecomdel)) {
				ecommunity_free(&ecom);
				node_to_del = node;
				break;
			}
		}
	}


	if (node_to_del)
		list_delete_node(rt_list, node_to_del);
}

/*
 * evpn_rt_delete_auto -- 删除由 AS + VNI 自动推导出的 Route Target。
 *
 * 做什么：若启用了 RFC8365 自动 RT，则给 vni 置 EVPN_AUTORT_VXLAN 标志；
 *         用 encode_route_target_as 依据本地 AS(低 16 位)与 vni 生成 RT 值，
 *         构造临时 ecommunity，调用 rt_list_remove_node 从 rtl 移除该 RT。
 * 参数：bgp 实例；vni；rtl 目标 RT 链表；is_l3 是否为 L3 (VRF) 链表。
 * 返回：无。
 * 坑：AS 取低 16 位，与自动 RT 的生成规则必须一致。
 */
void evpn_rt_delete_auto(struct bgp *bgp, vni_t vni, struct list *rtl,
			 bool is_l3)
{
	struct ecommunity *ecom_auto;
	struct ecommunity_val eval;

	if (bgp->advertise_autort_rfc8365)
		SET_FLAG(vni, EVPN_AUTORT_VXLAN);

	encode_route_target_as((bgp->as & 0xFFFF), vni, &eval, true);

	ecom_auto = ecommunity_new();
	ecommunity_add_val(ecom_auto, &eval, false, false);

	rt_list_remove_node(rtl, ecom_auto, is_l3);

	ecommunity_free(&ecom_auto);
}

/*
 * evpn_vrf_rt_routes_map -- 把 VRF 映射到其 RT 并安装匹配的新 RT 路由。
 *
 * 做什么：若该 VRF 的 L3VNI 存活，则调用 bgp_evpn_map_vrf_to_its_rts 建立 RT 映射，
 *         再 install_routes_for_vrf 把匹配新 RT 的远端路由安装进 VRF。
 * 参数：bgp_vrf VRF 实例。
 * 返回：无。
 * 坑：L3VNI 不存活时不做任何映射 / 安装。
 */
static void evpn_vrf_rt_routes_map(struct bgp *bgp_vrf)
{
	/* map VRFs to its RTs and install routes matching this new RT */
	if (is_l3vni_live(bgp_vrf)) {
		bgp_evpn_map_vrf_to_its_rts(bgp_vrf);
		install_routes_for_vrf(bgp_vrf);
	}
}

/*
 * evpn_vrf_rt_routes_unmap -- 解除 VRF 与其 RT 的关联并卸载相关路由。
 *
 * 做什么：若该 VRF 的 L3VNI 存活，先 uninstall_routes_for_vrf 从 VRF 卸载远端路由；
 *         再 bgp_evpn_unmap_vrf_from_its_rts 清除 RT 到 VRF 的映射。
 * 参数：bgp_vrf VRF 实例。
 * 返回：无。
 * 坑：卸载路由只在 L3VNI 存活时做，但 RT 映射清理无条件执行。
 */
static void evpn_vrf_rt_routes_unmap(struct bgp *bgp_vrf)
{
	/* uninstall routes from vrf */
	if (is_l3vni_live(bgp_vrf))
		uninstall_routes_for_vrf(bgp_vrf);

	/* Cleanup the RT to VRF mapping */
	bgp_evpn_unmap_vrf_from_its_rts(bgp_vrf);
}

/*
 * rt_list_has_cfgd_rt -- 判断 RT 链表中是否还存在显式配置的(非自动)RT。
 *
 * 做什么：遍历链表，只要发现一个未置 BGP_VRF_RT_AUTO 标志的 vrf_route_target
 *         即返回 true。
 * 参数：rt_list 目标链表。
 * 返回：存在显式配置 RT 返回 true，否则 false。
 * 坑：用于决定是否可清除 BGP_VRF_IMPORT_RT_CFGD 等标志。
 */
static bool rt_list_has_cfgd_rt(struct list *rt_list)
{
	struct listnode *node = NULL, *nnode = NULL;
	struct vrf_route_target *l3rt = NULL;

	for (ALL_LIST_ELEMENTS(rt_list, node, nnode, l3rt)) {
		if (!CHECK_FLAG(l3rt->flags, BGP_VRF_RT_AUTO))
			return true;
	}

	return false;
}

/*
 * unconfigure_import_rt_for_vrf_fini -- 取消 import RT 配置后的收尾：必要时回退到自动 RT。
 *
 * 做什么：若无 import RT 链表则返回；若 L3VNI 不存活则返回；若链表已空，
 *         说明删掉了最后一个 RT，回退为自动生成 import RT。
 * 参数：bgp_vrf VRF 实例。
 * 返回：无。
 * 坑：仅在 L3VNI 存活且表为空时才补回自动 RT。
 */
static void unconfigure_import_rt_for_vrf_fini(struct bgp *bgp_vrf)
{
	if (!bgp_vrf->vrf_import_rtl)
		return; /* this should never fail */

	if (!is_l3vni_live(bgp_vrf))
		return; /* Nothing to do if no vni */

	/* fall back to auto-generated RT if this was the last RT */
	if (list_isempty(bgp_vrf->vrf_import_rtl))
		evpn_auto_rt_import_add_for_vrf(bgp_vrf);
}

/*
 * unconfigure_export_rt_for_vrf_fini -- 取消 export RT 配置后的收尾：必要时回退到自动 RT。
 *
 * 做什么：若无 export RT 链表则返回；若 L3VNI 不存活则返回；若链表已空则回退为
 *         自动生成 export RT；最后调用 bgp_evpn_handle_export_rt_change_for_vrf
 *         依据新 RT 重新发布路由。
 * 参数：bgp_vrf VRF 实例。
 * 返回：无。
 * 坑：export RT 变化必须触发重发布，否则对端收不到更新。
 */
static void unconfigure_export_rt_for_vrf_fini(struct bgp *bgp_vrf)
{

	if (!bgp_vrf->vrf_export_rtl)
		return; /* this should never fail */

	if (!is_l3vni_live(bgp_vrf))
		return; /* Nothing to do if no vni */

	/* fall back to auto-generated RT if this was the last RT */
	if (list_isempty(bgp_vrf->vrf_export_rtl))
		evpn_auto_rt_export_add_for_vrf(bgp_vrf);

	bgp_evpn_handle_export_rt_change_for_vrf(bgp_vrf);
}

/*
 * bgp_evpn_configure_import_rt_for_vrf -- 为 VRF 增加一条显式配置的 import RT。
 *
 * 做什么：以 ecomadd 新建 vrf_route_target，is_wildcard 时置 BGP_VRF_RT_WILD；
 *         先 unmap 解除当前映射；若未启用自动 import RT 则删除已有自动 RT；
 *         把新 RT 有序加入 vrf_import_rtl；置 BGP_VRF_IMPORT_RT_CFGD 标志；
 *         最后重新 map 建立映射并安装匹配路由。
 * 参数：bgp_vrf VRF 实例；ecomadd 新 RT；is_wildcard 是否通配匹配。
 * 返回：无。
 * 坑：加 RT 前需先 unmap，避免旧映射残留。
 */
void bgp_evpn_configure_import_rt_for_vrf(struct bgp *bgp_vrf,
					  struct ecommunity *ecomadd,
					  bool is_wildcard)
{
	struct vrf_route_target *newrt;

	newrt = evpn_vrf_rt_new(ecomadd);

	if (is_wildcard)
		SET_FLAG(newrt->flags, BGP_VRF_RT_WILD);

	evpn_vrf_rt_routes_unmap(bgp_vrf);

	/* Remove auto generated RT if not configured */
	if (!CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_IMPORT_AUTO_RT_CFGD))
		evpn_auto_rt_import_delete_for_vrf(bgp_vrf);

	/* Add the newly configured RT to RT list */
	listnode_add_sort(bgp_vrf->vrf_import_rtl, newrt);

	SET_FLAG(bgp_vrf->vrf_flags, BGP_VRF_IMPORT_RT_CFGD);

	evpn_vrf_rt_routes_map(bgp_vrf);
}

/*
 * bgp_evpn_configure_import_auto_rt_for_vrf -- 为 VRF 启用自动 import RT。
 *
 * 做什么：若已置 BGP_VRF_IMPORT_AUTO_RT_CFGD 则直接返回；否则置该标志；
 *         若 L3VNI 尚未存活则先返回(待 VNI 就绪再补 RT)；随后 unmap、
 *         evpn_auto_rt_import_add_for_vrf 生成自动 RT、再重新 map。
 * 参数：bgp_vrf VRF 实例。
 * 返回：无。
 * 坑：L3VNI 未就绪时只置标志、不加 RT，后续由 VNI 事件补齐。
 */
void bgp_evpn_configure_import_auto_rt_for_vrf(struct bgp *bgp_vrf)
{
	if (CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_IMPORT_AUTO_RT_CFGD))
		return; /* Already configured */

	SET_FLAG(bgp_vrf->vrf_flags, BGP_VRF_IMPORT_AUTO_RT_CFGD);

	if (!is_l3vni_live(bgp_vrf))
		return; /* Wait for VNI before adding rts */

	evpn_vrf_rt_routes_unmap(bgp_vrf);

	evpn_auto_rt_import_add_for_vrf(bgp_vrf);

	evpn_vrf_rt_routes_map(bgp_vrf);
}

/*
 * bgp_evpn_unconfigure_import_rt_for_vrf -- 删除 VRF 的一条显式配置 import RT。
 *
 * 做什么：若未置 BGP_VRF_IMPORT_RT_CFGD 则直接返回；否则先 unmap，随后从
 *         vrf_import_rtl 移除与 ecomdel 匹配的节点；若表中已无显式 RT 则清
 *         BGP_VRF_IMPORT_RT_CFGD 标志；调用 fini 收尾(必要时回退自动 RT)；
 *         最后重新 map。
 * 参数：bgp_vrf VRF 实例；ecomdel 待删 RT。
 * 返回：无。
 * 坑：删除后要重新 map，才能按剩余 RT 调整安装的路由。
 */
void bgp_evpn_unconfigure_import_rt_for_vrf(struct bgp *bgp_vrf,
					    struct ecommunity *ecomdel)
{
	if (!CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_IMPORT_RT_CFGD))
		return; /* Already un-configured */

	evpn_vrf_rt_routes_unmap(bgp_vrf);

	/* Remove rt */
	rt_list_remove_node(bgp_vrf->vrf_import_rtl, ecomdel, true);

	if (!rt_list_has_cfgd_rt(bgp_vrf->vrf_import_rtl))
		UNSET_FLAG(bgp_vrf->vrf_flags, BGP_VRF_IMPORT_RT_CFGD);

	unconfigure_import_rt_for_vrf_fini(bgp_vrf);

	evpn_vrf_rt_routes_map(bgp_vrf);
}

/*
 * bgp_evpn_unconfigure_import_auto_rt_for_vrf -- 关闭 VRF 的自动 import RT。
 *
 * 做什么：若未置 BGP_VRF_IMPORT_AUTO_RT_CFGD 则直接返回；否则 unmap 解除映射、
 *         删除自动生成的 import RT、清除该标志、fini 收尾，最后重新 map。
 * 参数：bgp_vrf VRF 实例。
 * 返回：无。
 * 坑：关闭后若表中仍有显式 RT，收尾逻辑的行为需与其余 RT 协同。
 */
void bgp_evpn_unconfigure_import_auto_rt_for_vrf(struct bgp *bgp_vrf)
{
	if (!CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_IMPORT_AUTO_RT_CFGD))
		return; /* Already un-configured */

	evpn_vrf_rt_routes_unmap(bgp_vrf);

	/* remove auto-generated RT */
	evpn_auto_rt_import_delete_for_vrf(bgp_vrf);

	UNSET_FLAG(bgp_vrf->vrf_flags, BGP_VRF_IMPORT_AUTO_RT_CFGD);

	unconfigure_import_rt_for_vrf_fini(bgp_vrf);

	evpn_vrf_rt_routes_map(bgp_vrf);
}

/*
 * bgp_evpn_configure_export_rt_for_vrf -- 为 VRF 增加一条显式配置的 export RT。
 *
 * 做什么：以 ecomadd 新建 vrf_route_target；若未启用自动 export RT 则先删除已有
 *         自动 export RT；把新 RT 有序加入 vrf_export_rtl；置 BGP_VRF_EXPORT_RT_CFGD；
 *         若 L3VNI 存活则触发 export RT 变化处理(重发布)。
 * 参数：bgp_vrf VRF 实例；ecomadd 新 RT。
 * 返回：无。
 * 坑：export RT 变化会引起重发布，故须在 L3VNI 存活时调用 handle。
 */
void bgp_evpn_configure_export_rt_for_vrf(struct bgp *bgp_vrf,
					  struct ecommunity *ecomadd)
{
	struct vrf_route_target *newrt;

	newrt = evpn_vrf_rt_new(ecomadd);

	/* Remove auto generated RT if not configured */
	if (!CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_EXPORT_AUTO_RT_CFGD))
		evpn_auto_rt_export_delete_for_vrf(bgp_vrf);

	/* Add the new RT to the RT list */
	listnode_add_sort(bgp_vrf->vrf_export_rtl, newrt);

	SET_FLAG(bgp_vrf->vrf_flags, BGP_VRF_EXPORT_RT_CFGD);

	if (is_l3vni_live(bgp_vrf))
		bgp_evpn_handle_export_rt_change_for_vrf(bgp_vrf);
}

/*
 * bgp_evpn_configure_export_auto_rt_for_vrf -- 启用 VRF 的自动 export RT。
 *
 * 做什么：若已置 BGP_VRF_EXPORT_AUTO_RT_CFGD 则返回；否则置该标志；若 L3VNI
 *         未就绪则先返回(待 VNI 就绪再补)；随后生成自动 export RT 并触发 export
 *         RT 变化处理。
 * 参数：bgp_vrf VRF 实例。
 * 返回：无。
 * 坑：L3VNI 未就绪时只置标志、不加 RT。
 */
void bgp_evpn_configure_export_auto_rt_for_vrf(struct bgp *bgp_vrf)
{
	if (CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_EXPORT_AUTO_RT_CFGD))
		return; /* Already configured */

	SET_FLAG(bgp_vrf->vrf_flags, BGP_VRF_EXPORT_AUTO_RT_CFGD);

	if (!is_l3vni_live(bgp_vrf))
		return; /* Wait for VNI before adding rts */

	evpn_auto_rt_export_add_for_vrf(bgp_vrf);

	bgp_evpn_handle_export_rt_change_for_vrf(bgp_vrf);
}

/*
 * bgp_evpn_unconfigure_export_rt_for_vrf -- 删除 VRF 的一条显式配置 export RT。
 *
 * 做什么：若未置 BGP_VRF_EXPORT_RT_CFGD 则返回；否则从 vrf_export_rtl 移除匹配
 *         节点；若表中已无显式 RT 则清 BGP_VRF_EXPORT_RT_CFGD；最后调用 fini 收尾。
 * 参数：bgp_vrf VRF 实例；ecomdel 待删 RT。
 * 返回：无。
 * 坑：export RT 变更由 fini 统一触发重发布。
 */
void bgp_evpn_unconfigure_export_rt_for_vrf(struct bgp *bgp_vrf,
					    struct ecommunity *ecomdel)
{
	if (!CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_EXPORT_RT_CFGD))
		return; /* Already un-configured */

	/* Remove rt */
	rt_list_remove_node(bgp_vrf->vrf_export_rtl, ecomdel, true);

	if (!rt_list_has_cfgd_rt(bgp_vrf->vrf_export_rtl))
		UNSET_FLAG(bgp_vrf->vrf_flags, BGP_VRF_EXPORT_RT_CFGD);

	unconfigure_export_rt_for_vrf_fini(bgp_vrf);
}

/*
 * bgp_evpn_unconfigure_export_auto_rt_for_vrf -- 关闭 VRF 的自动 export RT。
 *
 * 做什么：若未置 BGP_VRF_EXPORT_AUTO_RT_CFGD 则返回；否则删除自动生成的 export RT、
 *         清除该标志、调用 fini 收尾。
 * 参数：bgp_vrf VRF 实例。
 * 返回：无。
 * 坑：与配置路径对称，收尾时可能补回自动 RT 或触发重发布。
 */
void bgp_evpn_unconfigure_export_auto_rt_for_vrf(struct bgp *bgp_vrf)
{
	if (!CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_EXPORT_AUTO_RT_CFGD))
		return; /* Already un-configured */

	/* remove auto-generated RT */
	evpn_auto_rt_export_delete_for_vrf(bgp_vrf);

	UNSET_FLAG(bgp_vrf->vrf_flags, BGP_VRF_EXPORT_AUTO_RT_CFGD);

	unconfigure_export_rt_for_vrf_fini(bgp_vrf);
}

/*
 * bgp_evpn_handle_router_id_update -- 处理 BGP router-id 变更(变更处理器会调用两次)。
 *
 * 做什么：withdraw 为真(变更前)：withdraw_router_id_vrf 撤销该 VRF 的 Type-5 路由，
 *         再 hash_iterate 对全部 L2VNI 调用 withdraw_router_id_vni 撤销其 Type-2 / Type-3；
 *         若为默认实例，还按需重置各 VRF 的 pip_ip。withdraw 为假(变更后)：为默认实例
 *         重新指派 pip / router-id 并重发对应 VRF 路由，再 update_router_id_vrf 与
 *         hash_iterate update_router_id_vni 用新 RD 重新发布。
 * 参数：bgp 实例；withdraw 区分变更前(撤销)与变更后(重发布)。
 * 返回：无。
 * 坑：必须先撤后发，保证对端先收到撤销再收到新 router-id 下的通告。
 */
/*
 * Handle change to BGP router id. This is invoked twice by the change
 * handler, first before the router id has been changed and then after
 * the router id has been changed. The first invocation will result in
 * local routes for all VNIs/VRF being deleted and withdrawn and the next
 * will result in the routes being re-advertised.
 */
void bgp_evpn_handle_router_id_update(struct bgp *bgp, int withdraw)
{
	struct listnode *node;
	struct bgp *bgp_vrf;

	if (withdraw) {

		/* delete and withdraw all the type-5 routes
		   stored in the global table for this vrf
		 */
		withdraw_router_id_vrf(bgp);

		/* delete all the VNI routes (type-2/type-3) routes for all the
		 * L2-VNIs
		 */
		hash_iterate(bgp->vnihash,
			     (void (*)(struct hash_bucket *,
				       void *))withdraw_router_id_vni,
			     bgp);

		if (bgp->inst_type == BGP_INSTANCE_TYPE_DEFAULT) {
			for (ALL_LIST_ELEMENTS_RO(bm->bgp, node, bgp_vrf)) {
				/* advertise pip is enabled,
				 * bgp instance L3VNI VTEP-IP is IPv4
				 * advertise pip IP is not user configured.
				 */
				if (bgp_vrf->evpn_info->advertise_pip &&
				    IS_IPADDR_V4(&bgp_vrf->originator_ip) &&
				    (bgp_vrf->evpn_info->pip_ip_static.ipaddr_v4.s_addr ==
				     INADDR_ANY)) {
					bgp_vrf->evpn_info->pip_ip.ipaddr_v4.s_addr = INADDR_ANY;
				}
			}
		}
	} else {

		/* Assign new default instance router-id */
		if (bgp->inst_type == BGP_INSTANCE_TYPE_DEFAULT) {
			for (ALL_LIST_ELEMENTS_RO(bm->bgp, node, bgp_vrf)) {
				/* advertise pip is enabled,
				 * bgp instance L3VNI VTEP-IP is IPv4
				 * advertise pip IP is not user configured.
				 * assign the bgp default router-id as pip IP.
				 */
				if (bgp_vrf->evpn_info->advertise_pip &&
				    IS_IPADDR_V4(&bgp_vrf->originator_ip) &&
				    (bgp_vrf->evpn_info->pip_ip_static.ipaddr_v4.s_addr ==
				     INADDR_ANY)) {
					SET_IPADDR_V4(&bgp_vrf->evpn_info->pip_ip);
					bgp_vrf->evpn_info->pip_ip.ipaddr_v4 = bgp->router_id;
					/* advertise type-5 routes with
					 * new nexthop
					 */
					update_advertise_vrf_routes(bgp_vrf);
				}
			}
		}

		/* advertise all routes in the vrf as type-5 routes with the new
		 * RD
		 */
		update_router_id_vrf(bgp);

		/* advertise all the VNI routes (type-2/type-3) routes with the
		 * new RD
		 */
		hash_iterate(bgp->vnihash,
			     (void (*)(struct hash_bucket *,
				       void *))update_router_id_vni,
			     bgp);
	}
}

/*
 * bgp_evpn_handle_autort_change -- 处理自动 RT 算法变化，更新并重发本地路由。
 *
 * 做什么：hash_iterate 对全部 VNI 调用 update_autort_vni 处理各自自动 RT 变化；
 *         若本实例有 l3vni，再调用 update_autort_l3vni 处理 L3VNI(VRF)侧。
 * 参数：bgp 实例。
 * 返回：无。
 * 坑：需同时覆盖 L2VNI(逐 VNI)与 L3VNI(VRF)两条处理路径。
 */
/*
 * Handle change to auto-RT algorithm - update and advertise local routes.
 */
void bgp_evpn_handle_autort_change(struct bgp *bgp)
{
	hash_iterate(bgp->vnihash,
		     (void (*)(struct hash_bucket *,
			       void*))update_autort_vni,
		     bgp);
	if (bgp->l3vni)
		update_autort_l3vni(bgp);
}

/*
 * struct vni_gr_walk -- 优雅重启(GR)延迟选路遍历 VNI 时携带的上下文。
 * 字段：bgp 目标实例；cnt 已处理的延迟路径计数(用于限制单批处理量)。
 * 用于 bgp_evpn_handle_deferred_bestpath_for_vnis 在各 VNI 间传递进度。
 */
struct vni_gr_walk {
	struct bgp *bgp;
	uint16_t cnt;
};

/*
 * bgp_deferred_path_selection -- 遍历表中标记为延迟选路的前缀并完成其 bestpath 计算。
 *
 * 做什么：从表顶遍历，只要 gr_deferred 非零且 cnt 未达 BGP_MAX_BEST_ROUTE_SELECT，
 *         对置了 BGP_NODE_SELECT_DEFER 的 dest：清标志、gr_deferred 减一；evpn_select
 *         为真时把所有路径置 UNSORTED 并走 evpn_route_select_install，否则走
 *         bgp_process_main_one；cnt 递增。若提前停表则解锁当前 dest。
 * 参数：bgp 实例；afi / safi；table 待遍历表；cnt 起始计数；vpn(EVPN 选路用)；
 *       evpn_select 是否走 EVPN 专用选路。
 * 返回：处理后的累计计数 cnt。
 * 坑：只有被显式标记延迟的节点才会被处理；提前退出必须解锁 dest 防泄漏。
 */
/*
 * Iterate over all the deferred prefixes in this table
 * and calculate the bestpath.
 */
uint16_t bgp_deferred_path_selection(struct bgp *bgp, afi_t afi, safi_t safi,
				     struct bgp_table *table, uint16_t cnt, struct bgpevpn *vpn,
				     bool evpn_select)
{
	struct bgp_dest *dest = NULL;

	for (dest = bgp_table_top(table);
	     dest && bgp->gr_info[afi][safi].gr_deferred != 0 && cnt < BGP_MAX_BEST_ROUTE_SELECT;
	     dest = bgp_route_next(dest)) {
		if (!CHECK_FLAG(dest->flags, BGP_NODE_SELECT_DEFER))
			continue;

		UNSET_FLAG(dest->flags, BGP_NODE_SELECT_DEFER);
		bgp->gr_info[afi][safi].gr_deferred--;

		if (evpn_select) {
			struct bgp_path_info *pi = bgp_dest_get_bgp_path_info(dest);

			/*
			 * Mark them all as unsorted and just pass
			 * the first one in to do work on.  Clear
			 * everything since at this point it is
			 * unknown what was or was not done for
			 * all the deferred paths
			 */
			while (pi) {
				SET_FLAG(pi->flags, BGP_PATH_UNSORTED);
				pi = pi->next;
			}

			evpn_route_select_install(bgp, vpn, dest, bgp_dest_get_bgp_path_info(dest));
		} else
			bgp_process_main_one(bgp, dest, afi, safi);

		cnt++;
	}

	/* If iteration stopped before the entire table was traversed then the
	 * node needs to be unlocked.
	 */
	if (dest) {
		bgp_dest_unlock_node(dest);
		dest = NULL;
	}

	return cnt;
}

/*
 * bgp_evpn_handle_deferred_bestpath_per_vni -- hash 遍历回调：对单个 VNI 完成延迟 bestpath 选路。
 *
 * 做什么：取出 VNI 与上下文 ctx；必要时打印调试日志；若已无延迟路径或已达单批上限
 *         则返回；否则对该 VNI 的 mac_table 与 ip_table 分别调用
 *         bgp_deferred_path_selection 并把处理数累加到 ctx->cnt。
 * 参数：bucket 为 VNI 哈希桶；arg 为 struct vni_gr_walk *。
 * 返回：无。
 * 坑：arg 需按 struct vni_gr_walk * 解引用；两张表都要遍历。
 */
static void bgp_evpn_handle_deferred_bestpath_per_vni(struct hash_bucket *bucket, void *arg)
{
	struct bgpevpn *vpn = bucket->data;
	struct vni_gr_walk *ctx = arg;
	struct bgp *bgp = ctx->bgp;
	afi_t afi = AFI_L2VPN;
	safi_t safi = SAFI_EVPN;

	/*
	 * Now, walk this VNI's MAC & IP route table and do deferred bestpath
	 * selection
	 */
	if (BGP_DEBUG(graceful_restart, GRACEFUL_RESTART))
		zlog_debug("%s (%u): GR walking IP and MAC table for VNI %u. Deferred paths %d, batch cnt %d",
			   vrf_id_to_name(bgp->vrf_id), bgp->vrf_id, vpn->vni,
			   bgp->gr_info[afi][safi].gr_deferred, ctx->cnt);

	if (!bgp->gr_info[afi][safi].gr_deferred || ctx->cnt >= BGP_MAX_BEST_ROUTE_SELECT)
		return;

	ctx->cnt += bgp_deferred_path_selection(bgp, afi, safi, vpn->mac_table, ctx->cnt, vpn,
						true);
	ctx->cnt += bgp_deferred_path_selection(bgp, afi, safi, vpn->ip_table, ctx->cnt, vpn, true);
}

/*
 * bgp_evpn_handle_deferred_bestpath_for_vnis -- 对所有 VNI 触发延迟 bestpath 选路。
 *
 * 做什么：用入参 bgp 与 cnt 初始化 struct vni_gr_walk 上下文，hash_iterate 遍历
 *         全部 VNI，逐个调用 bgp_evpn_handle_deferred_bestpath_per_vni。
 * 参数：bgp 实例；cnt 起始计数(承接前一批进度)。
 * 返回：无。
 * 坑：cnt 由调用方带入场，可在多批之间延续，避免单次处理过多延迟路径。
 */
void bgp_evpn_handle_deferred_bestpath_for_vnis(struct bgp *bgp, uint16_t cnt)
{
	struct vni_gr_walk ctx;

	ctx.bgp = bgp;
	ctx.cnt = cnt;

	hash_iterate(bgp->vnihash,
		     (void (*)(struct hash_bucket *,
			       void *))bgp_evpn_handle_deferred_bestpath_per_vni,
		     &ctx);
}
/*
 * bgp_evpn_handle_export_rt_change -- 处理 export RT 变化后某 VNI 本地路由的重发。
 *
 * 做什么：直接转调 update_routes_for_vni(bgp, vpn)，由其完成该 VNI 路由的更新发布。
 * 参数：bgp 实例；vpn 目标 VNI。
 * 返回：update_routes_for_vni 的返回值。
 * 坑：本函数只是薄封装，真正逻辑在 update_routes_for_vni。
 */
/*
 * Handle change to export RT - update and advertise local routes.
 */
int bgp_evpn_handle_export_rt_change(struct bgp *bgp, struct bgpevpn *vpn)
{
	return update_routes_for_vni(bgp, vpn);
}

/*
 * bgp_evpn_handle_vrf_rd_change -- 处理 VRF 的 RD 变化(撤销旧 / 发布新 Type-5 路由)。
 *
 * 做什么：withdraw 为真时调用 delete_withdraw_vrf_routes 删除并撤销该 VRF 的
 *         Type-5 路由；为假时调用 update_advertise_vrf_routes 以新 RD 重新发布。
 * 参数：bgp_vrf VRF 实例；withdraw 区分撤销与重发布。
 * 返回：无。
 * 坑：同样遵循先撤后发，避免新旧 RD 路由并存。
 */
void bgp_evpn_handle_vrf_rd_change(struct bgp *bgp_vrf, int withdraw)
{
	if (withdraw)
		delete_withdraw_vrf_routes(bgp_vrf);
	else
		update_advertise_vrf_routes(bgp_vrf);
}

/*
 * bgp_evpn_handle_rd_change -- 处理某 VNI 的 RD 变化(撤销旧 / 发布新本地路由)。
 *
 * 做什么：withdraw 为真时调用 delete_withdraw_vni_routes 删除并撤销该 VNI 的
 *         本地路由；为假时调用 update_advertise_vni_routes 以新 RD 重新发布。
 * 参数：bgp 实例；vpn 目标 VNI；withdraw 区分撤销与重发布。
 * 返回：无。
 * 坑：变更处理器会调用两次，先撤后发，确保对端不残留旧 RD 路由。
 */
/*
 * Handle change to RD. This is invoked twice by the change handler,
 * first before the RD has been changed and then after the RD has
 * been changed. The first invocation will result in local routes
 * of this VNI being deleted and withdrawn and the next will result
 * in the routes being re-advertised.
 */
void bgp_evpn_handle_rd_change(struct bgp *bgp, struct bgpevpn *vpn,
			       int withdraw)
{
	if (withdraw)
		delete_withdraw_vni_routes(bgp, vpn);
	else
		update_advertise_vni_routes(bgp, vpn);
}

/*
 * bgp_evpn_handle_global_macvrf_soo_change -- 处理全局 MAC-VRF Site-of-Origin(SoO)变化。
 *
 * 做什么：保存旧 SoO；若新旧相同则释放新 SoO 并返回；否则设置新 SoO，
 *         先 unimport 匹配新 SoO 的路由(bgp_filter_evpn_routes_upon_martian_change)，
 *         再 reimport 匹配旧 SoO 且不匹配新 SoO 的路由
 *         (bgp_reimport_evpn_routes_upon_martian_change)；随后 hash_iterate 对全部
 *         L2VNI 调用 update_routes_for_vni_hash 更新本地起源路由并重发；最后释放旧 SoO。
 * 参数：bgp 实例；new_soo 新 SoO。
 * 返回：无。
 * 坑：新旧相同的短路分支会接管并释放 new_soo，属预期，调用方不要再释放它。
 */
/* "mac-vrf soo" vty handler
 * Handle change to the global MAC-VRF Site-of-Origin:
 *   - Unimport routes with new SoO from VNI/VRF
 *   - Import routes with old SoO into VNI/VRF
 *   - Update SoO on local VNI routes + re-advertise
 */
void bgp_evpn_handle_global_macvrf_soo_change(struct bgp *bgp,
					      struct ecommunity *new_soo)
{
	struct ecommunity *old_soo;

	old_soo = bgp->evpn_info->soo;

	/* cleanup and bail out if old_soo == new_soo */
	if (ecommunity_match(old_soo, new_soo)) {
		ecommunity_free(&new_soo);
		return;
	}

	/* set new_soo */
	bgp->evpn_info->soo = new_soo;

	/* Unimport routes matching the new_soo */
	bgp_filter_evpn_routes_upon_martian_change(bgp, BGP_MARTIAN_SOO);

	/* Reimport routes with old_soo and !new_soo.
	 */
	bgp_reimport_evpn_routes_upon_martian_change(
		bgp, BGP_MARTIAN_SOO, (void *)old_soo, (void *)new_soo);

	/* Update locally originated routes for all L2VNIs */
	hash_iterate(bgp->vnihash,
		     (void (*)(struct hash_bucket *,
			       void *))update_routes_for_vni_hash,
		     bgp);

	/* clear old_soo */
	ecommunity_free(&old_soo);
}

/*
 * bgp_evpn_install_routes -- 为某 VNI 安装路由(import RT 变化时调用)。
 *
 * 做什么：直接转调 install_routes_for_vni(bgp, vpn)。
 * 参数：bgp 实例；vpn 目标 VNI。
 * 返回：install_routes_for_vni 的返回值。
 * 坑：薄封装，实际安装逻辑在 install_routes_for_vni。
 */
/*
 * Install routes for this VNI. Invoked upon change to Import RT.
 */
int bgp_evpn_install_routes(struct bgp *bgp, struct bgpevpn *vpn)
{
	return install_routes_for_vni(bgp, vpn);
}

/*
 * bgp_evpn_uninstall_routes -- 卸载某 VNI 已安装的全部路由(import RT 变化时调用)。
 *
 * 做什么：直接转调 uninstall_routes_for_vni(bgp, vpn)。
 * 参数：bgp 实例；vpn 目标 VNI。
 * 返回：uninstall_routes_for_vni 的返回值。
 * 坑：薄封装，实际卸载逻辑在 uninstall_routes_for_vni。
 */
/*
 * Uninstall all routes installed for this VNI. Invoked upon change
 * to Import RT.
 */
int bgp_evpn_uninstall_routes(struct bgp *bgp, struct bgpevpn *vpn)
{
	return uninstall_routes_for_vni(bgp, vpn);
}

/*
 * bgp_evpn_label2str -- 把标签(VNI)数组转成可读字符串写入 buf。
 *
 * 做什么：把 label[0] 转为 vni1；若 num_labels 为 2，再把 label[1] 转为 vni2，
 *         按 "vni1/vni2" 格式写；否则只写 "vni1"。返回 buf 便于链式使用。
 * 参数：label 标签数组；num_labels 个数；buf / len 目标缓冲。
 * 返回：写入的 buf。
 * 坑：当前硬编码最多支持 2 个 VNI，多于 2 个不会被表示。
 */
/*
 * TODO: Hardcoded for a maximum of 2 VNIs right now
 */
char *bgp_evpn_label2str(mpls_label_t *label, uint8_t num_labels, char *buf,
			 int len)
{
	vni_t vni1, vni2;

	vni1 = label2vni(label);
	if (num_labels == 2) {
		vni2 = label2vni(label + 1);
		snprintf(buf, len, "%u/%u", vni1, vni2);
	} else
		snprintf(buf, len, "%u", vni1);
	return buf;
}

/*
 * bgp_evpn_route2json -- 把一条 EVPN 路由转换为 JSON 对象(便于 show 输出)。
 *
 * 做什么：json 为空直接返回；先写 routeType，再按类型分支：MAC-IP 写 ethTag /
 *         macLen / mac(有 IP 时再写 ipLen / ip)；IMET 写 ethTag / ipLen / ip；
 *         IP-PREFIX 写 ethTag / ipLen / ip。
 * 参数：p EVPN 前缀；json 目标 JSON 对象。
 * 返回：无。
 * 坑：此处未用 prefix2str，输出字段布局与通用前缀转字符串不同。
 */
/*
 * Function to convert evpn route to json format.
 * NOTE: We don't use prefix2str as the output here is a bit different.
 */
void bgp_evpn_route2json(const struct prefix_evpn *p, json_object *json)
{
	char buf1[ETHER_ADDR_STRLEN];
	char buf2[PREFIX2STR_BUFFER];
	uint8_t family;
	uint8_t prefixlen;

	if (!json)
		return;

	json_object_int_add(json, "routeType", p->prefix.route_type);

	switch (p->prefix.route_type) {
	case BGP_EVPN_MAC_IP_ROUTE:
		json_object_int_add(json, "ethTag",
			p->prefix.macip_addr.eth_tag);
		json_object_int_add(json, "macLen", 8 * ETH_ALEN);
		json_object_string_add(json, "mac",
			prefix_mac2str(&p->prefix.macip_addr.mac, buf1,
			sizeof(buf1)));

		if (!is_evpn_prefix_ipaddr_none(p)) {
			family = is_evpn_prefix_ipaddr_v4(p) ? AF_INET :
				AF_INET6;
			prefixlen = (family == AF_INET) ?
				IPV4_MAX_BITLEN : IPV6_MAX_BITLEN;
			inet_ntop(family, &p->prefix.macip_addr.ip.ip.addr,
				buf2, PREFIX2STR_BUFFER);
			json_object_int_add(json, "ipLen", prefixlen);
			json_object_string_add(json, "ip", buf2);
		}
	break;

	case BGP_EVPN_IMET_ROUTE:
		json_object_int_add(json, "ethTag",
			p->prefix.imet_addr.eth_tag);
		family = is_evpn_prefix_ipaddr_v4(p) ? AF_INET : AF_INET6;
		prefixlen = (family == AF_INET) ?  IPV4_MAX_BITLEN :
			IPV6_MAX_BITLEN;
		inet_ntop(family, &p->prefix.imet_addr.ip.ip.addr, buf2,
			PREFIX2STR_BUFFER);
		json_object_int_add(json, "ipLen", prefixlen);
		json_object_string_add(json, "ip", buf2);
	break;

	case BGP_EVPN_IP_PREFIX_ROUTE:
		json_object_int_add(json, "ethTag",
			p->prefix.prefix_addr.eth_tag);
		family = is_evpn_prefix_ipaddr_v4(p) ? AF_INET : AF_INET6;
		inet_ntop(family, &p->prefix.prefix_addr.ip.ip.addr,
			  buf2, sizeof(buf2));
		json_object_int_add(json, "ipLen",
				    p->prefix.prefix_addr.ip_prefix_length);
		json_object_string_add(json, "ip", buf2);
	break;

	default:
	break;
	}
}

/*
 * bgp_evpn_encode_prefix -- 把一条 EVPN 前缀编码进 UPDATE 报文(MP_REACH)。
 *
 * 做什么：addpath 能力开启时先写 addpath_tx_id；写 route_type；按类型分支：
 *         Type-2(MAC-IP) 写 RD / ESI / EthTag / MAC 与长度 / IP 长度与 IP / 1 或 2 个 VNI；
 *         Type-3(IMET) 写 RD / EthTag / 起源 IP；
 *         Type-4(ES) 写 RD / ESI / 起源 IP；Type-1(AD) 写 RD / ESI / EthTag / VNI；
 *         Type-5 委托 evpn_mpattr_encode_type5。
 * 参数：s 目标流；p 前缀；prd RD；label / num_labels 标签与个数；attr 属性；
 *       addpath_capable 是否支持 add-path；addpath_tx_id 发送侧 add-path 标识。
 * 返回：无。
 * 坑：Type-2 有 IP 且标签数为 2 时才追加第二个(L3VNI)标签。
 */
/*
 * Encode EVPN prefix in Update (MP_REACH)
 */
void bgp_evpn_encode_prefix(struct stream *s, const struct prefix *p,
			    const struct prefix_rd *prd, mpls_label_t *label,
			    uint8_t num_labels, struct attr *attr,
			    bool addpath_capable, uint32_t addpath_tx_id)
{
	struct prefix_evpn *evp = (struct prefix_evpn *)p;
	int len, ipa_len = 0;

	if (addpath_capable)
		stream_putl(s, addpath_tx_id);

	/* Route type */
	stream_putc(s, evp->prefix.route_type);

	switch (evp->prefix.route_type) {
	case BGP_EVPN_MAC_IP_ROUTE:
		if (is_evpn_prefix_ipaddr_v4(evp))
			ipa_len = IPV4_MAX_BYTELEN;
		else if (is_evpn_prefix_ipaddr_v6(evp))
			ipa_len = IPV6_MAX_BYTELEN;
		/* RD, ESI, EthTag, MAC+len, IP len, [IP], 1 VNI */
		len = 8 + 10 + 4 + 1 + 6 + 1 + ipa_len + 3;
		if (ipa_len && num_labels > 1) /* There are 2 VNIs */
			len += 3;
		stream_putc(s, len);
		stream_put(s, prd->val, 8);   /* RD */
		if (attr)
			stream_put(s, &attr->esi, ESI_BYTES);
		else
			stream_put(s, 0, 10);
		stream_putl(s, evp->prefix.macip_addr.eth_tag);	/* Ethernet Tag ID */
		stream_putc(s, 8 * ETH_ALEN); /* Mac Addr Len - bits */
		stream_put(s, evp->prefix.macip_addr.mac.octet, 6); /* Mac Addr */
		stream_putc(s, 8 * ipa_len); /* IP address Length */
		if (ipa_len) /* IP */
			stream_put(s, &evp->prefix.macip_addr.ip.ip.addr,
				   ipa_len);
		/* 1st label is the L2 VNI */
		stream_put(s, label, BGP_LABEL_BYTES);
		/* Include 2nd label (L3 VNI) if advertising MAC+IP */
		if (ipa_len && num_labels > 1)
			stream_put(s, label + 1, BGP_LABEL_BYTES);
		break;

	case BGP_EVPN_IMET_ROUTE: {
		uint8_t orig_ip_bits = 0;
		/* RFC-7432 IMET NLRI 13 = RD (8) + Ethtag (4) + IP length (1) */
		uint8_t total_bytes = 13; /* Fixed part excluding Originator IP */

		/* If Originator IP, add bytes to sizes */
		if (IS_IPADDR_V4(&evp->prefix.imet_addr.ip)) {
			orig_ip_bits = IPV4_MAX_BITLEN;
			total_bytes += IPV4_MAX_BYTELEN; /* V4 Originator IP */
		} else if (IS_IPADDR_V6(&evp->prefix.imet_addr.ip)) {
			orig_ip_bits = IPV6_MAX_BITLEN;
			total_bytes += IPV6_MAX_BYTELEN; /* V6 Originator IP */
		}

		stream_putc(s, total_bytes);
		stream_put(s, prd->val, 8);		       /* RD */
		stream_putl(s, evp->prefix.imet_addr.eth_tag); /* Ethernet Tag ID */

		stream_putc(s, orig_ip_bits); /* Originator IP address Length - bits */
		if (orig_ip_bits)
			stream_put(s, &evp->prefix.imet_addr.ip.ip, orig_ip_bits / 8);

		break;
	}

	case BGP_EVPN_ES_ROUTE: {
		uint8_t ipaddr_bits = 0;
		/* RFC-7432 ES NLRI 19 = RD (8) + ESI (10) + IP length (1) */
		uint8_t total_bytes = 19; /* Fixed independent of IP family */

		if (IS_IPADDR_V4(&evp->prefix.es_addr.ip)) {
			ipaddr_bits = IPV4_MAX_BITLEN;
			total_bytes += IPV4_MAX_BYTELEN; /* V4 Originator IP */
		} else if (IS_IPADDR_V6(&evp->prefix.es_addr.ip)) {
			ipaddr_bits = IPV6_MAX_BITLEN;
			total_bytes += IPV6_MAX_BYTELEN; /* V6 Originator IP */
		}

		stream_putc(s, total_bytes);
		stream_put(s, prd->val, 8); /* RD */
		stream_put(s, evp->prefix.es_addr.esi.val, 10); /* ESI */
		stream_putc(s, ipaddr_bits); /* Originator IP address Length - bits */
		/* VTEP IP */
		if (ipaddr_bits)
			stream_put(s, &evp->prefix.es_addr.ip.ip, ipaddr_bits / 8);
		else
			flog_err(EC_BGP_EVPN_ROUTE_CREATE,
				 "evpn ES route %pFX created with ip field as empty", evp);

		break;
	}

	case BGP_EVPN_AD_ROUTE:
		/* RD, ESI, EthTag, 1 VNI */
		len = RD_BYTES + ESI_BYTES + EVPN_ETH_TAG_BYTES + BGP_LABEL_BYTES;
		stream_putc(s, len);
		stream_put(s, prd->val, RD_BYTES); /* RD */
		stream_put(s, evp->prefix.ead_addr.esi.val, ESI_BYTES); /* ESI */
		stream_putl(s, evp->prefix.ead_addr.eth_tag); /* Ethernet Tag */
		stream_put(s, label, BGP_LABEL_BYTES);
		break;

	case BGP_EVPN_IP_PREFIX_ROUTE:
		evpn_mpattr_encode_type5(s, p, prd, label, num_labels, attr);
		break;

	default:
		break;
	}
}

/*
 * bgp_nlri_parse_evpn -- 解析 MP_REACH / MP_UNREACH 中的 EVPN NLRI(可能含多条)。
 *
 * 做什么：从 packet->nlri 遍历到 lim；支持 add-path 时先读 addpath_id；每条以
 *         type + length 开头，做溢出检查后按 rtype 分派：Type-2 走 process_type2_route、
 *         Type-3 走 process_type3_route、Type-4 走 bgp_evpn_type4_route_process、
 *         Type-1 走 bgp_evpn_type1_route_process、Type-5 走 process_type5_route。
 * 参数：peer 源对端；attr 属性(撤销时为 NULL)；packet 承载 NLRI；withdraw 是否撤销。
 * 返回：BGP_NLRI_PARSE_OK；出错返回对应 BGP_NLRI_PARSE_ERROR_* 码。
 * 坑：withdraw 为真时把 attr 置 NULL 交给子解析函数；循环结束必须 pnt == lim 才合法。
 */
int bgp_nlri_parse_evpn(struct peer *peer, struct attr *attr,
			struct bgp_nlri *packet, bool withdraw)
{
	uint8_t *pnt;
	uint8_t *lim;
	afi_t afi;
	safi_t safi;
	uint32_t addpath_id;
	bool addpath_capable;
	int psize = 0;
	uint8_t rtype;
	struct prefix p;

	/* Start processing the NLRI - there may be multiple in the MP_REACH */
	pnt = packet->nlri;
	lim = pnt + packet->length;
	afi = packet->afi;
	safi = packet->safi;
	addpath_id = 0;

	addpath_capable = bgp_addpath_encode_rx(peer, afi, safi);

	for (; pnt < lim; pnt += psize) {
		/* Clear prefix structure. */
		memset(&p, 0, sizeof(p));

		/* Deal with path-id if AddPath is supported. */
		if (addpath_capable) {
			/* When packet overflow occurs return immediately. */
			if (pnt + BGP_ADDPATH_ID_LEN > lim)
				return BGP_NLRI_PARSE_ERROR_PACKET_OVERFLOW;

			memcpy(&addpath_id, pnt, BGP_ADDPATH_ID_LEN);
			addpath_id = ntohl(addpath_id);
			pnt += BGP_ADDPATH_ID_LEN;
		}

		/* All EVPN NLRI types start with type and length. */
		if (pnt + 2 > lim)
			return BGP_NLRI_PARSE_ERROR_EVPN_MISSING_TYPE;

		rtype = *pnt++;
		psize = *pnt++;

		/* When packet overflow occur return immediately. */
		if (pnt + psize > lim)
			return BGP_NLRI_PARSE_ERROR_PACKET_OVERFLOW;

		switch (rtype) {
		case BGP_EVPN_MAC_IP_ROUTE:
			if (process_type2_route(peer, afi, safi,
						withdraw ? NULL : attr, pnt,
						psize, addpath_id)) {
				flog_err(
					EC_BGP_EVPN_FAIL,
					"%u:%s - Error in processing EVPN type-2 NLRI size %d",
					peer->bgp->vrf_id, peer->host, psize);
				return BGP_NLRI_PARSE_ERROR_EVPN_TYPE2_SIZE;
			}
			break;

		case BGP_EVPN_IMET_ROUTE:
			if (process_type3_route(peer, afi, safi,
						withdraw ? NULL : attr, pnt,
						psize, addpath_id)) {
				flog_err(
					EC_BGP_PKT_PROCESS,
					"%u:%s - Error in processing EVPN type-3 NLRI size %d",
					peer->bgp->vrf_id, peer->host, psize);
				return BGP_NLRI_PARSE_ERROR_EVPN_TYPE3_SIZE;
			}
			break;

		case BGP_EVPN_ES_ROUTE:
			if (bgp_evpn_type4_route_process(peer, afi, safi,
						withdraw ? NULL : attr, pnt,
						psize, addpath_id)) {
				flog_err(
					EC_BGP_PKT_PROCESS,
					"%u:%s - Error in processing EVPN type-4 NLRI size %d",
					peer->bgp->vrf_id, peer->host, psize);
				return BGP_NLRI_PARSE_ERROR_EVPN_TYPE4_SIZE;
			}
			break;

		case BGP_EVPN_AD_ROUTE:
			if (bgp_evpn_type1_route_process(peer, afi, safi,
						withdraw ? NULL : attr, pnt,
						psize, addpath_id)) {
				flog_err(
					EC_BGP_PKT_PROCESS,
					"%u:%s - Error in processing EVPN type-1 NLRI size %d",
					peer->bgp->vrf_id, peer->host, psize);
				return BGP_NLRI_PARSE_ERROR_EVPN_TYPE1_SIZE;
			}
			break;

		case BGP_EVPN_IP_PREFIX_ROUTE:
			if (process_type5_route(peer, afi, safi,
						withdraw ? NULL : attr, pnt,
						psize, addpath_id)) {
				flog_err(
					EC_BGP_PKT_PROCESS,
					"%u:%s - Error in processing EVPN type-5 NLRI size %d",
					peer->bgp->vrf_id, peer->host, psize);
				return BGP_NLRI_PARSE_ERROR_EVPN_TYPE5_SIZE;
			}
			break;

		default:
			break;
		}
	}

	/* Packet length consistency check. */
	if (pnt != lim)
		return BGP_NLRI_PARSE_ERROR_PACKET_LENGTH;

	return BGP_NLRI_PARSE_OK;
}

/*
 * bgp_evpn_map_vrf_to_its_rts -- 把 VRF(配置或自动推导)的 RT 映射到该 VRF。
 *
 * 做什么：遍历 bgp_vrf->vrf_import_rtl，对每个 vrf_route_target 调用 map_vrf_to_rt
 *         建立 RT 到 VRF 的映射(路由处理时会用到该映射)。
 * 参数：bgp_vrf 具体 BGP VRF 实例。
 * 返回：无。
 * 坑：只处理 import RT 链表；映射用于后续按 RT 导入路由。
 */
/*
 * Map the RTs (configured or automatically derived) of a VRF to the VRF.
 * The mapping will be used during route processing.
 * bgp_vrf: specific bgp vrf instance on which RT is configured
 */
void bgp_evpn_map_vrf_to_its_rts(struct bgp *bgp_vrf)
{
	struct listnode *node, *nnode;
	struct vrf_route_target *l3rt;

	for (ALL_LIST_ELEMENTS(bgp_vrf->vrf_import_rtl, node, nnode, l3rt))
		map_vrf_to_rt(bgp_vrf, l3rt);
}

/*
 * bgp_evpn_unmap_vrf_from_its_rts -- 解除 VRF(配置或自动推导)RT 到该 VRF 的映射。
 *
 * 做什么：遍历 bgp_vrf->vrf_import_rtl，对每个 vrf_route_target 调用
 *         unmap_vrf_from_rt 清除映射。
 * 参数：bgp_vrf 具体 BGP VRF 实例。
 * 返回：无。
 * 坑：与 map_vrf_to_its_rts 对称，用于 RT 变更或卸载时回收映射。
 */
/*
 * Unmap the RTs (configured or automatically derived) of a VRF from the VRF.
 */
void bgp_evpn_unmap_vrf_from_its_rts(struct bgp *bgp_vrf)
{
	struct listnode *node, *nnode;
	struct vrf_route_target *l3rt;

	for (ALL_LIST_ELEMENTS(bgp_vrf->vrf_import_rtl, node, nnode, l3rt))
		unmap_vrf_from_rt(bgp_vrf, l3rt);
}

/*
 * bgp_evpn_map_vni_to_its_rts -- 把 VNI(配置或自动推导)的 RT 映射到该 VNI。
 *
 * 做什么：遍历 vpn->import_rtl 中的每个 ecommunity；对其中每个 RT 值(按
 *         ECOMMUNITY_SIZE 步进)调用 map_vni_to_rt 建立 RT 到 VNI 的映射。
 * 参数：bgp 实例；vpn 目标 VNI。
 * 返回：无。
 * 坑：一个 ecommunity 可能含多个 RT 值，需逐个映射。
 */
/*
 * Map the RTs (configured or automatically derived) of a VNI to the VNI.
 * The mapping will be used during route processing.
 */
void bgp_evpn_map_vni_to_its_rts(struct bgp *bgp, struct bgpevpn *vpn)
{
	uint32_t i;
	struct ecommunity_val *eval;
	struct listnode *node, *nnode;
	struct ecommunity *ecom;

	for (ALL_LIST_ELEMENTS(vpn->import_rtl, node, nnode, ecom)) {
		for (i = 0; i < ecom->size; i++) {
			eval = (struct ecommunity_val *)(ecom->val
							 + (i
							    * ECOMMUNITY_SIZE));
			map_vni_to_rt(bgp, vpn, eval);
		}
	}
}

/*
 * bgp_evpn_unmap_vni_from_its_rts -- 解除 VNI(配置或自动推导)RT 到该 VNI 的映射。
 *
 * 做什么：遍历 vpn->import_rtl 的每个 ecommunity 及其中每个 RT 值；若为自动 RT，
 *         先用 mask_ecom_global_admin 掩掉全局管理字段(只比较本地管理子字段)，
 *         再用 lookup_import_rt 找到对应 irt，随后 unmap_vni_from_rt 解除映射。
 * 参数：bgp 实例；vpn 目标 VNI。
 * 返回：无。
 * 坑：自动 RT(未显式配置 import RT)必须先掩码再查找，否则映射键不一致。
 */
/*
 * Unmap the RTs (configured or automatically derived) of a VNI from the VNI.
 */
void bgp_evpn_unmap_vni_from_its_rts(struct bgp *bgp, struct bgpevpn *vpn)
{
	uint32_t i;
	struct ecommunity_val *eval;
	struct listnode *node, *nnode;
	struct ecommunity *ecom;

	for (ALL_LIST_ELEMENTS(vpn->import_rtl, node, nnode, ecom)) {
		for (i = 0; i < ecom->size; i++) {
			struct irt_node *irt;
			struct ecommunity_val eval_tmp;

			eval = (struct ecommunity_val *)(ecom->val
							 + (i
							    * ECOMMUNITY_SIZE));
			/* If using "automatic" RT, we only care about the
			 * local-admin sub-field.
			 * This is to facilitate using VNI as the RT for EBGP
			 * peering too.
			 */
			memcpy(&eval_tmp, eval, ECOMMUNITY_SIZE);
			if (!is_import_rt_configured(vpn))
				mask_ecom_global_admin(&eval_tmp, eval);

			irt = lookup_import_rt(bgp, &eval_tmp);
			if (irt)
				unmap_vni_from_rt(bgp, vpn, irt);
		}
	}
}

/*
 * bgp_evpn_derive_auto_rt_import -- 为 VNI 自动推导 import RT 并建立到 VNI 的映射。
 *
 * 做什么：用 form_auto_rt 依据 vni 生成自动 import RT 加入 import_rtl；清除
 *         VNI_FLAG_IMPRT_CFGD(表示非显式配置)；再 bgp_evpn_map_vni_to_its_rts 建映射。
 * 参数：bgp 实例；vpn 目标 VNI。
 * 返回：无。
 * 坑：生成后必须建映射，否则按 RT 导入路由时匹配不到该 VNI。
 */
/*
 * Derive Import RT automatically for VNI and map VNI to RT.
 * The mapping will be used during route processing.
 */
void bgp_evpn_derive_auto_rt_import(struct bgp *bgp, struct bgpevpn *vpn)
{
	form_auto_rt(bgp, vpn->vni, vpn->import_rtl, false);
	UNSET_FLAG(vpn->flags, VNI_FLAG_IMPRT_CFGD);

	/* Map RT to VNI */
	bgp_evpn_map_vni_to_its_rts(bgp, vpn);
}

/*
 * bgp_evpn_derive_auto_rt_export -- 为 VNI 自动推导 export RT。
 *
 * 做什么：用 form_auto_rt 依据 vni 生成自动 export RT 加入 export_rtl；清除
 *         VNI_FLAG_EXPRT_CFGD。
 * 参数：bgp 实例；vpn 目标 VNI。
 * 返回：无。
 * 坑：export RT 只影响本地对外发布，无需建立映射。
 */
/*
 * Derive Export RT automatically for VNI.
 */
void bgp_evpn_derive_auto_rt_export(struct bgp *bgp, struct bgpevpn *vpn)
{
	form_auto_rt(bgp, vpn->vni, vpn->export_rtl, false);
	UNSET_FLAG(vpn->flags, VNI_FLAG_EXPRT_CFGD);
}

/*
 * bgp_evpn_derive_auto_rd_for_vrf -- 为 VRF 自动推导 RD，形如 RouterId:unique-id。
 *
 * 做什么：若该 VRF 的 RD 已显式配置则返回；否则用 form_auto_rd 以 router_id 与
 *         vrf_rd_id 生成 RD 写入 bgp->vrf_prd。
 * 参数：bgp 为 VRF 对应的 BGP 实例。
 * 返回：无。
 * 坑：显式配置 RD 的 VRF 不覆盖，保证用户配置优先。
 */
/*
 * Derive RD automatically for VNI using passed information - it
 * is of the form RouterId:unique-id-for-vni.
 */
void bgp_evpn_derive_auto_rd_for_vrf(struct bgp *bgp)
{
	if (is_vrf_rd_configured(bgp))
		return;

	form_auto_rd(bgp->router_id, bgp->vrf_rd_id, &bgp->vrf_prd);
}

/*
 * bgp_evpn_derive_auto_rd -- 为 VNI 自动推导 RD，形如 RouterId:unique-id-for-vni。
 *
 * 做什么：把 vpn->prd 置为 AF_UNSPEC / 64 位；用 "routerid:rd_id" 格式化后经
 *         str2prefix_rd 解析进 vpn->prd；若已有 prd_pretty 则释放；清除 VNI_FLAG_RD_CFGD。
 * 参数：bgp 实例；vpn 目标 VNI。
 * 返回：无。
 * 坑：rd_id 每个 VNI 唯一，由其决定 RD 的 unique-id 部分。
 */
/*
 * Derive RD automatically for VNI using passed information - it
 * is of the form RouterId:unique-id-for-vni.
 */
void bgp_evpn_derive_auto_rd(struct bgp *bgp, struct bgpevpn *vpn)
{
	char buf[BGP_EVPN_PREFIX_RD_LEN];

	vpn->prd.family = AF_UNSPEC;
	vpn->prd.prefixlen = 64;
	snprintfrr(buf, sizeof(buf), "%pI4:%hu", &bgp->router_id, vpn->rd_id);
	(void)str2prefix_rd(buf, &vpn->prd);
	if (vpn->prd_pretty)
		XFREE(MTYPE_BGP_NAME, vpn->prd_pretty);
	UNSET_FLAG(vpn->flags, VNI_FLAG_RD_CFGD);
}

/*
 * bgp_evpn_lookup_l3vni_l2vni_table -- 判断某 VNI 是否已作为某个 VRF 的 L3VNI。
 *
 * 做什么：遍历全部 BGP 实例，若存在 bgp_vrf->l3vni == vni 则返回 true。
 * 参数：vni 待查 VNI。
 * 返回：命中返回 true，否则 false。
 * 坑：只查 L3VNI 字段，不检查 VNI 是否为 L2VNI。
 */
/*
 * Lookup L3-VNI
 */
bool bgp_evpn_lookup_l3vni_l2vni_table(vni_t vni)
{
	struct list *inst = bm->bgp;
	struct listnode *node;
	struct bgp *bgp_vrf;

	for (ALL_LIST_ELEMENTS_RO(inst, node, bgp_vrf)) {
		if (bgp_vrf->l3vni == vni)
			return true;
	}

	return false;
}

/*
 * bgp_evpn_lookup_vni -- 在实例的 VNI 哈希表中查找指定 VNI。
 *
 * 做什么：构造只含 vni 的临时 struct bgpevpn 作为键，hash_lookup 查找并返回。
 * 参数：bgp 实例；vni 待查 VNI。
 * 返回：找到返回 bgpevpn 指针，否则 NULL。
 * 坑：hash 比较只依赖 vni 字段，故临时结构只填 vni 即可。
 */
/*
 * Lookup VNI.
 */
struct bgpevpn *bgp_evpn_lookup_vni(struct bgp *bgp, vni_t vni)
{
	struct bgpevpn *vpn;
	struct bgpevpn tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.vni = vni;
	vpn = hash_lookup(bgp->vnihash, &tmp);
	return vpn;
}

/*
 * bgp_evpn_new -- 新建一个 VNI(由配置或 zebra 通知触发)。
 *
 * 做什么：分配 struct bgpevpn，填入 vni / originator_ip / tenant_vrf_id /
 *         mcast_grp / svi_ifindex，泛洪模式默认继承全局；初始化 import / export RT 链表
 *         及比较器与析构器；分配 rd_id 并推导 RD / RT；初始化 ip_table 与 mac_table；
 *         加入 vnihash；初始化 remote-ip 哈希、链接 SVI 哈希、链接到 L3VNI、初始化 ES，
 *         最后注册 QOBJ。
 * 参数：bgp 实例；vni；originator_ip；tenant_vrf_id；mcast_grp；svi_ifindex。
 * 返回：新建的 bgpevpn 指针。
 * 坑：新建时会立即推导默认 RD / RT 并挂入哈希，调用方通常需再按需设置 live 标志。
 */
/*
 * Create a new vpn - invoked upon configuration or zebra notification.
 */
struct bgpevpn *bgp_evpn_new(struct bgp *bgp, vni_t vni,
		struct ipaddr *originator_ip,
		vrf_id_t tenant_vrf_id,
		struct in_addr mcast_grp,
		ifindex_t svi_ifindex)
{
	struct bgpevpn *vpn;

	vpn = XCALLOC(MTYPE_BGP_EVPN, sizeof(struct bgpevpn));

	/* Set values - RD and RT set to defaults. */
	vpn->vni = vni;
	vpn->originator_ip = *originator_ip;
	vpn->tenant_vrf_id = tenant_vrf_id;
	vpn->mcast_grp = mcast_grp;
	vpn->svi_ifindex = svi_ifindex;
	vpn->vxlan_flood_ctrl = VXLAN_FLOOD_INHERIT_GLOBAL;

	/* Initialize route-target import and export lists */
	vpn->import_rtl = list_new();
	vpn->import_rtl->cmp =
		(int (*)(void *, void *))bgp_evpn_route_target_cmp;
	vpn->import_rtl->del = bgp_evpn_xxport_delete_ecomm;
	vpn->export_rtl = list_new();
	vpn->export_rtl->cmp =
		(int (*)(void *, void *))bgp_evpn_route_target_cmp;
	vpn->export_rtl->del = bgp_evpn_xxport_delete_ecomm;
	bf_assign_index(bm->rd_idspace, vpn->rd_id);
	derive_rd_rt_for_vni(bgp, vpn);

	/* Initialize EVPN route tables. */
	vpn->ip_table = bgp_table_init(bgp, AFI_L2VPN, SAFI_EVPN);
	vpn->mac_table = bgp_table_init(bgp, AFI_L2VPN, SAFI_EVPN);

	/* Add to hash */
	(void)hash_get(bgp->vnihash, vpn, hash_alloc_intern);

	bgp_evpn_remote_ip_hash_init(vpn);
	bgp_evpn_link_to_vni_svi_hash(bgp, vpn);

	/* add to l2vni list on corresponding vrf */
	bgpevpn_link_to_l3vni(vpn);

	bgp_evpn_vni_es_init(vpn);

	QOBJ_REG(vpn, bgpevpn);
	return vpn;
}

/*
 * bgp_evpn_free -- 释放一个 VNI(zebra 通知、配置删除、advertise-all-vni 关闭等场景调用)。
 *
 * 做什么：销毁 remote-ip 哈希、清理 ES、从 L3VNI 解链、解锁 ip_table 与 mac_table、
 *         解除 RT 映射、删除 import / export RT 链表、释放 rd_id、从 vni_svi_hash
 *         与 vnihash 移除、释放 prd_pretty、注销 QOBJ、最后释放本体。
 * 参数：bgp 实例；vpn 目标 VNI。
 * 返回：无。
 * 坑：本函数只释放内存，调用方须先完成路由撤销等必要动作，否则会留下悬空引用。
 */
/*
 * Free a given VPN - called in multiple scenarios such as zebra
 * notification, configuration being deleted, advertise-all-vni disabled etc.
 * This just frees appropriate memory, caller should have taken other
 * needed actions.
 */
void bgp_evpn_free(struct bgp *bgp, struct bgpevpn *vpn)
{
	bgp_evpn_remote_ip_hash_destroy(vpn);
	bgp_evpn_vni_es_cleanup(vpn);
	bgpevpn_unlink_from_l3vni(vpn);
	bgp_table_unlock(vpn->ip_table);
	bgp_table_unlock(vpn->mac_table);
	bgp_evpn_unmap_vni_from_its_rts(bgp, vpn);
	list_delete(&vpn->import_rtl);
	list_delete(&vpn->export_rtl);
	bf_release_index(bm->rd_idspace, vpn->rd_id);
	hash_release(bgp->vni_svi_hash, vpn);
	hash_release(bgp->vnihash, vpn);
	if (vpn->prd_pretty)
		XFREE(MTYPE_BGP_NAME, vpn->prd_pretty);
	QOBJ_UNREG(vpn);
	XFREE(MTYPE_BGP_EVPN, vpn);
}

/*
 * hash_evpn_free -- 哈希表条目释放回调：直接释放 struct bgpevpn 本体。
 * 参数：vpn 待释放的 VNI。
 * 返回：无。
 * 坑：仅做 XFREE，不处理 VNI 内部资源(那些由 bgp_evpn_free 负责)。
 */
static void hash_evpn_free(struct bgpevpn *vpn)
{
	XFREE(MTYPE_BGP_EVPN, vpn);
}

/*
 * bgp_evpn_import_route -- 把全局表中的 EVPN 路由导入到 VNI / VRF / ESI。
 *
 * 做什么：以 import=1 调用 install_uninstall_evpn_route。
 * 参数：bgp 实例；afi / safi；p 前缀；pi 路径信息。
 * 返回：install_uninstall_evpn_route 的返回值。
 * 坑：薄封装，实际导入逻辑在 install_uninstall_evpn_route。
 */
/*
 * Import evpn route from global table to VNI/VRF/ESI.
 */
int bgp_evpn_import_route(struct bgp *bgp, afi_t afi, safi_t safi,
			  const struct prefix *p, struct bgp_path_info *pi)
{
	return install_uninstall_evpn_route(bgp, afi, safi, p, pi, 1);
}

/*
 * bgp_evpn_unimport_route -- 把 EVPN 路由从 VNI / VRF / ESI 中撤销导入。
 *
 * 做什么：以 import=0 调用 install_uninstall_evpn_route。
 * 参数：bgp 实例；afi / safi；p 前缀；pi 路径信息。
 * 返回：install_uninstall_evpn_route 的返回值。
 * 坑：与 import 路径共用同一函数，靠 import 取值区分导入 / 撤销。
 */
/*
 * Unimport evpn route from VNI/VRF/ESI.
 */
int bgp_evpn_unimport_route(struct bgp *bgp, afi_t afi, safi_t safi,
			    const struct prefix *p, struct bgp_path_info *pi)
{
	return install_uninstall_evpn_route(bgp, afi, safi, p, pi, 0);
}

/*
 * bgp_evpn_export_type5_route -- 把一条 IPv4 / IPv6 unicast 路由从 VRF 导出为全局 Type-5。
 *
 * 做什么：先 bgp_addpath_update_ids 补齐 add-path 发送 ID(可能在选路后才生成)；
 *         计算 addpath_id；若该地址族未配置通告 route-map，直接用 pi->attr 通告；
 *         否则复制属性到临时 attr、装填临时 path_info、套用 route-map：被拒则冲刷
 *         临时属性并返回，否则用结果属性通告。
 * 参数：bgp VRF 实例；dest 源节点；pi 源路径；afi / safi 地址族。
 * 返回：无。
 * 坑：add-path 情况下须强制补齐 TX ID，否则对端撤回时无法对应。
 */
/*
 * Export IPv[46] unicast route from VRF to global table
 */
void bgp_evpn_export_type5_route(struct bgp *bgp, struct bgp_dest *dest, struct bgp_path_info *pi,
				 afi_t afi, safi_t safi)
{
	const struct prefix *prefix = bgp_dest_get_prefix(dest);
	route_map_result_t ret;
	struct bgp_path_info tmp_pi;
	struct bgp_path_info_extra tmp_pie;
	struct attr tmp_attr;
	uint32_t addpath_id;

	/* For addpath we need to have valid TX addpath IDs when exporting to
	 * EVPN (well, at least the ID for ALL strategy). Those will be used as
	 * the RX ID in the EVPN paths, helping identifying them for withdraws.
	 * Such IDs are populated after bestpath selection, which is a bummer
	 * for us. Force populate those.
	 */
	bgp_addpath_update_ids(bgp, dest, afi, safi);

	addpath_id = bgp_evpn_addpath_id_for_path(bgp, pi, afi);
	if (!bgp->adv_cmd_rmap[afi][safi].map) {
		bgp_evpn_advertise_type5_route(bgp, pi, prefix, pi->attr, afi, safi, addpath_id);
		return;
	}

	tmp_attr = *pi->attr;

	/* Fill temp path_info */
	prep_for_rmap_apply(&tmp_pi, &tmp_pie, dest, pi, pi->peer, NULL, &tmp_attr);
	RESET_FLAG(tmp_attr.rmap_change_flags);

	ret = route_map_apply(bgp->adv_cmd_rmap[afi][safi].map, prefix, &tmp_pi);
	if (ret == RMAP_DENYMATCH) {
		bgp_attr_flush(&tmp_attr);
		return;
	}
	bgp_evpn_advertise_type5_route(bgp, pi, prefix, &tmp_attr, afi, safi, addpath_id);
}

/*
 * bgp_evpn_unexport_type5_route -- 撤销一条从 VRF 导出到全局的 Type-5 路由。
 *
 * 做什么：计算 addpath_id 后，调用 bgp_evpn_withdraw_type5_route 撤销该前缀。
 * 参数：bgp VRF 实例；dest 源节点；pi 源路径；afi / safi 地址族。
 * 返回：无。
 * 坑：需与导出时使用相同的 addpath_id 才能准确撤回。
 */
/*
 * Unexport IPv[46] unicast route from VRF to global table
 */
void bgp_evpn_unexport_type5_route(struct bgp *bgp, const struct bgp_dest *dest,
				   const struct bgp_path_info *pi, afi_t afi, safi_t safi)
{
	const struct prefix *prefix = bgp_dest_get_prefix(dest);
	uint32_t addpath_id;

	addpath_id = bgp_evpn_addpath_id_for_path(bgp, pi, afi);
	bgp_evpn_withdraw_type5_route(bgp, pi, prefix, afi, safi, addpath_id);
}

/*
 * bgp_reimport_evpn_routes_upon_macvrf_soo_change -- 重新导入此前因携带自身 MAC-VRF SoO
 * 而被丢弃的 EVPN 路由。
 *
 * 做什么：遍历全局 EVPN rib —— 它是两级表(外层 RD，内层 prefix_evpn)；只处理
 *         Type-2(MAC-IP)与 Type-3(IMET)；对每条从对端学到且 VALID 的路径，判断是否
 *         命中旧 SoO 且不命中新 SoO，命中则调用 bgp_evpn_import_route 重新导入。
 * 参数：bgp 实例；old_soo 旧 SoO；new_soo 新 SoO。
 * 返回：无。
 * 坑：远端路由 RD 任意，必须遍历全部外层 RD；只对 MAC-IP / IMET 做 SoO 判断。
 */
/* Refresh previously-discarded EVPN routes carrying "self" MAC-VRF SoO.
 * Walk global EVPN rib + import remote routes with old_soo && !new_soo.
 */
void bgp_reimport_evpn_routes_upon_macvrf_soo_change(struct bgp *bgp,
						     struct ecommunity *old_soo,
						     struct ecommunity *new_soo)
{
	afi_t afi;
	safi_t safi;
	struct bgp_dest *rd_dest, *dest;
	struct bgp_table *table;
	struct bgp_path_info *pi;

	afi = AFI_L2VPN;
	safi = SAFI_EVPN;

	/* EVPN routes are a 2-level table: outer=prefix_rd, inner=prefix_evpn.
	 * A remote route could have any RD, so we need to walk them all.
	 */
	for (rd_dest = bgp_table_top(bgp->rib[afi][safi]); rd_dest;
	     rd_dest = bgp_route_next(rd_dest)) {
		table = bgp_dest_get_bgp_table_info(rd_dest);
		if (!table)
			continue;

		for (dest = bgp_table_top(table); dest;
		     dest = bgp_route_next(dest)) {
			const struct prefix *p;
			struct prefix_evpn *evp;

			p = bgp_dest_get_prefix(dest);
			evp = (struct prefix_evpn *)p;

			/* On export we only add MAC-VRF SoO to RT-2/3, so we
			 * can skip evaluation of other RTs.
			 */
			if (evp->prefix.route_type != BGP_EVPN_MAC_IP_ROUTE &&
			    evp->prefix.route_type != BGP_EVPN_IMET_ROUTE)
				continue;

			for (pi = bgp_dest_get_bgp_path_info(dest); pi;
			     pi = pi->next) {
				bool old_soo_fnd = false;
				bool new_soo_fnd = false;

				/* Only consider routes learned from peers */
				if (!(pi->type == ZEBRA_ROUTE_BGP &&
				      pi->sub_type == BGP_ROUTE_NORMAL))
					continue;

				if (!CHECK_FLAG(pi->flags, BGP_PATH_VALID))
					continue;

				old_soo_fnd = route_matches_soo(pi, old_soo);
				new_soo_fnd = route_matches_soo(pi, new_soo);

				if (old_soo_fnd && !new_soo_fnd) {
					if (bgp_debug_update(pi->peer, p, NULL,
							     1)) {
						char attr_str[BUFSIZ] = {0};

						bgp_dump_attr(pi->attr,
							      attr_str, BUFSIZ);

						zlog_debug(
							"mac-vrf soo changed: evaluating reimport of prefix %pBD with attr %s",
							dest, attr_str);
					}

					bgp_evpn_import_route(bgp, afi, safi, p,
							      pi);
				}
			}
		}
	}
}

/* Filter learned (!local) EVPN routes carrying "self" attributes.
 * Walk the Global EVPN loc-rib unimporting martian routes from the appropriate
 * L2VNIs (MAC-VRFs) / L3VNIs (IP-VRFs), and deleting them from the Global
 * loc-rib when applicable (based on martian_type).
 * This function is the handler for new martian entries, which is triggered by
 * events occurring on the local system,
 * e.g.
 * - New VTEP-IP
 *   + bgp_zebra_process_local_vni
 *   + bgp_zebra_process_local_l3vni
 * - New MAC-VRF Site-of-Origin
 *   + bgp_evpn_handle_global_macvrf_soo_change
 * This will likely be extended in the future to cover these events too:
 * - New Interface IP
 *   + bgp_interface_address_add
 * - New Interface MAC
 *   + bgp_ifp_up
 *   + bgp_ifp_create
 * - New RMAC
 *   + bgp_zebra_process_local_l3vni
 */
/*
 * bgp_evpn_log_martian_discard -- 记录 / 追踪一条因 Martian 被丢弃的 EVPN 路由。
 *
 * 做什么：仅当更新调试或对应 trace 开启时才做开销较大的字符串格式化；转储属性、
 *         把前缀转字符串；调试开启时打 zlog，并触发 frrtrace 记录丢弃原因。
 * 参数：bgp 实例；pi 被丢弃路径；dest 节点；p 前缀；martian_type Martian 类型。
 * 返回：无。
 * 坑：前置的调试 / trace 判断用于避免无谓的格式化开销。
 */
static void bgp_evpn_log_martian_discard(struct bgp *bgp, struct bgp_path_info *pi,
					 struct bgp_dest *dest, const struct prefix *p,
					 enum bgp_martian_type martian_type)
{
	/* Only do expensive string formatting if debug or trace is enabled. */
	if (!bgp_debug_update(pi->peer, p, NULL, 1) &&
	    !frrtrace_enabled(frr_bgp, upd_attr_discarded_due_to_martian))
		return;

	char attr_str[BUFSIZ] = { 0 };
	char prefix_str[PREFIX2STR_BUFFER] = { 0 };

	bgp_dump_attr(pi->attr, attr_str, sizeof(attr_str));
	prefix2str(p, prefix_str, sizeof(prefix_str));

	if (bgp_debug_update(pi->peer, p, NULL, 1))
		zlog_debug("%u: prefix %pBD with attr %s - DISCARDED due to Martian/%s",
			   bgp->vrf_id, dest, attr_str, bgp_martian_type2str(martian_type));

	frrtrace(4, frr_bgp, upd_attr_discarded_due_to_martian, bgp->vrf_id, prefix_str, attr_str,
		 bgp_martian_type2str(martian_type));
}

/*
 * bgp_filter_evpn_routes_upon_martian_change -- 过滤携带"自身"属性的已学到 EVPN 路由。
 *
 * 做什么：遍历全局 EVPN loc-rib(两级表：外层 RD，内层 prefix_evpn)；对每条从对端
 *         学到且有效(NORMAL 子类型)的路径，按 martian_type 判断是否受影响：
 *         TUN_IP 用 bgp_nexthop_self，SOO 用 route_matches_soo(macvrf_soo)，
 *         其余类型暂不处理。受影响的路径先记丢弃日志，再 bgp_evpn_unimport_route
 *         从 L2VNI / VRF 撤销导入；TUN_IP 类型还额外从全局 loc-rib 删除(bgp_rib_remove)。
 * 参数：bgp 实例；martian_type Martian 类型。
 * 返回：无。
 * 坑：SOO 路由仅从 L2VNI / VRF 撤销导入但保留在全局 loc-rib；TUN_IP(自身 IP / MAC)
 *     路由还会从全局 loc-rib 删除，二者处理不同(代码注释明确标注后续待统一)。
 */
void bgp_filter_evpn_routes_upon_martian_change(
	struct bgp *bgp, enum bgp_martian_type martian_type)
{
	afi_t afi;
	safi_t safi;
	struct bgp_dest *rd_dest, *dest;
	struct bgp_table *table;
	struct bgp_path_info *pi;
	struct ecommunity *macvrf_soo;

	afi = AFI_L2VPN;
	safi = SAFI_EVPN;
	macvrf_soo = bgp->evpn_info->soo;

	/* EVPN routes are a 2-level table: outer=prefix_rd, inner=prefix_evpn.
	 * A remote route could have any RD, so we need to walk them all.
	 */
	for (rd_dest = bgp_table_top(bgp->rib[afi][safi]); rd_dest;
	     rd_dest = bgp_route_next(rd_dest)) {
		table = bgp_dest_get_bgp_table_info(rd_dest);
		if (!table)
			continue;

		for (dest = bgp_table_top(table); dest;
		     dest = bgp_route_next(dest)) {
			struct bgp_path_info *next;

			for (pi = bgp_dest_get_bgp_path_info(dest);
			     (pi != NULL) && (next = pi->next, 1); pi = next) {
				bool affected = false;
				const struct prefix *p;

				/* Only consider routes learned from peers */
				if (!(pi->type == ZEBRA_ROUTE_BGP
				      && pi->sub_type == BGP_ROUTE_NORMAL))
					continue;

				p = bgp_dest_get_prefix(dest);

				switch (martian_type) {
				case BGP_MARTIAN_TUN_IP:
					affected = bgp_nexthop_self(
						bgp, afi, pi->type,
						pi->sub_type, pi->attr, dest);
					break;
				case BGP_MARTIAN_SOO:
					affected = route_matches_soo(
						pi, macvrf_soo);
					break;
				case BGP_MARTIAN_IF_IP:
				case BGP_MARTIAN_IF_MAC:
				case BGP_MARTIAN_RMAC:
					break;
				}

				if (affected) {
					bgp_evpn_log_martian_discard(bgp, pi, dest, p,
								     martian_type);

					bgp_evpn_unimport_route(bgp, afi, safi,
								p, pi);

					/* For now, retain existing handling of
					 * tip_hash updates: (Self SoO routes
					 * are unimported from L2VNI/VRF but
					 *  retained in global loc-rib, but Self
					 * IP/MAC routes are also deleted from
					 * global loc-rib).
					 * TODO: use consistent handling for all
					 * martian types
					 */
					if (martian_type == BGP_MARTIAN_TUN_IP)
						bgp_rib_remove(dest, pi,
							       pi->peer, afi,
							       safi);
				}
			}
		}
	}
}

/* Refresh previously-discarded EVPN routes carrying "self" attributes.
 * This function is the handler for deleted martian entries, which is triggered
 * by events occurring on the local system,
 * e.g.
 * - Del MAC-VRF Site-of-Origin
 *   + bgp_evpn_handle_global_macvrf_soo_change
 * This will likely be extended in the future to cover these events too:
 * - Del VTEP-IP
 *   + bgp_zebra_process_local_vni
 *   + bgp_zebra_process_local_l3vni
 * - Del Interface IP
 *   + bgp_interface_address_delete
 * - Del Interface MAC
 *   + bgp_ifp_down
 *   + bgp_ifp_destroy
 * - Del RMAC
 *   + bgp_zebra_process_local_l3vni
 */
/*
 * bgp_reimport_evpn_routes_upon_martian_change -- Martian 条目消失后重新学习此前被丢弃的 EVPN 路由。
 *
 * 做什么：martian_type 为 SOO 时，取 old_martian / new_martian 作为 SoO，若旧 SoO
 *         存在则调用 bgp_reimport_evpn_routes_upon_macvrf_soo_change 重导入，然后返回。
 *         其余类型(TIP / IP / MAC / RMAC)因路由已从全局 loc-rib 删除，需靠
 *         soft-reconfig / route-refresh 重新学习：遍历对端，跳过 group 与非 Established，
 *         对启用了 soft-reconfig 的对端调 bgp_soft_reconfig_in，否则发送 route-refresh。
 * 参数：bgp 实例；martian_type;old_martian / new_martian 旧新 Martian 值(SoO 时为 ecommunity)。
 * 返回：无。
 * 坑：SoO 与其它 Martian 类型的恢复机制不同，前者靠本地 loc-rib 重导入，后者靠对端重发。
 */
void bgp_reimport_evpn_routes_upon_martian_change(
	struct bgp *bgp, enum bgp_martian_type martian_type, void *old_martian,
	void *new_martian)
{
	struct listnode *node;
	struct peer *peer;
	safi_t safi;
	afi_t afi;
	struct ecommunity *old_soo, *new_soo;

	afi = AFI_L2VPN;
	safi = SAFI_EVPN;

	/* Self-SoO routes are held in the global EVPN loc-rib, so we can
	 * reimport routes w/o triggering soft-reconfig/route-refresh.
	 */
	if (martian_type == BGP_MARTIAN_SOO) {
		old_soo = (struct ecommunity *)old_martian;
		new_soo = (struct ecommunity *)new_martian;

		/* If !old_soo, then we can skip the reimport because we
		 * wouldn't have filtered anything via the self-SoO import check
		 */
		if (old_martian)
			bgp_reimport_evpn_routes_upon_macvrf_soo_change(
				bgp, old_soo, new_soo);

		return;
	}

	/* Self-TIP/IP/MAC/RMAC routes are deleted from the global EVPN
	 * loc-rib, so we need to re-learn the routes via soft-reconfig/
	 * route-refresh.
	 */
	for (ALL_LIST_ELEMENTS_RO(bgp->peer, node, peer)) {

		if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
			continue;

		if (peer->connection->status != Established)
			continue;

		if (CHECK_FLAG(peer->af_flags[afi][safi],
			       PEER_FLAG_SOFT_RECONFIG)) {
			if (bgp_debug_update(peer, NULL, NULL, 1))
				zlog_debug(
					"Processing EVPN Martian/%s change on peer %s (inbound, soft-reconfig)",
					bgp_martian_type2str(martian_type),
					peer->host);

			frrtrace(3, frr_bgp, upd_evpn_martian_change, peer->host,
				 bgp_martian_type2str(martian_type), 1);

			bgp_soft_reconfig_in(peer, afi, safi);
		} else {
			if (bgp_debug_update(peer, NULL, NULL, 1))
				zlog_debug(
					"Processing EVPN Martian/%s change on peer %s",
					bgp_martian_type2str(martian_type),
					peer->host);

			frrtrace(3, frr_bgp, upd_evpn_martian_change, peer->host,
				 bgp_martian_type2str(martian_type), 0);

			bgp_route_refresh_send(peer, afi, safi, 0,
					       REFRESH_IMMEDIATE, 0,
					       BGP_ROUTE_REFRESH_NORMAL);
		}
	}
}

/*
 * bgp_evpn_local_macip_del -- 处理本地 MACIP 删除(zebra 通知)。
 *
 * 做什么：查 VNI，未找到或未存活则告警并返回 -1；构造 Type-2 前缀；若 state 为
 *         ACTIVE 则 delete_evpn_route 删除该 Type-2 路由并调度处理；否则(非活跃)
 *         重新安装当前远端最优路径(evpn_zebra_reinstall_best_route)并解锁节点。
 * 参数：bgp 实例；vni；mac / ip；state 邻居状态。
 * 返回：成功 0；VNI 无效返回 -1。
 * 坑：state 非 ACTIVE 时并非删除路由，而是回退到远端最优路径。
 */
/*
 * Handle del of a local MACIP.
 */
int bgp_evpn_local_macip_del(struct bgp *bgp, vni_t vni, struct ethaddr *mac,
			     struct ipaddr *ip, int state)
{
	struct bgpevpn *vpn;
	struct prefix_evpn p;
	struct bgp_dest *dest;

	/* Lookup VNI hash - should exist. */
	vpn = bgp_evpn_lookup_vni(bgp, vni);
	if (!vpn || !is_vni_live(vpn)) {
		flog_warn(EC_BGP_EVPN_VPN_VNI,
			  "%u: VNI hash entry for VNI %u %s at MACIP DEL",
			  bgp->vrf_id, vni, vpn ? "not live" : "not found");
		return -1;
	}

	build_evpn_type2_prefix(&p, mac, ip);
	if (state == ZEBRA_NEIGH_ACTIVE) {
		/* Remove EVPN type-2 route and schedule for processing. */
		delete_evpn_route(bgp, vpn, &p);
	} else {
		/* Re-instate the current remote best path if any */
		dest = bgp_evpn_vni_node_lookup(vpn, &p, NULL);
		if (dest) {
			evpn_zebra_reinstall_best_route(bgp, vpn, dest);
			bgp_dest_unlock_node(dest);
		}
	}

	return 0;
}

/*
 * bgp_evpn_local_macip_add -- 处理本地 MACIP 新增(zebra 通知)。
 *
 * 做什么：查 VNI，未找到或未存活则告警并返回 -1；构造 Type-2 前缀并调用
 *         update_evpn_route 创建 / 更新路由，失败则打错误日志并返回 -1。
 * 参数：bgp 实例；vni；mac / ip；flags(如 sticky gateway)；seq 移动序列号；esi。
 * 返回：成功 0；VNI 无效或创建失败返回 -1。
 * 坑：flags 中的 ZEBRA_MACIP_TYPE_STICKY 会影响日志措辞，但路由创建统一走 update_evpn_route。
 */
/*
 * Handle add of a local MACIP.
 */
int bgp_evpn_local_macip_add(struct bgp *bgp, vni_t vni, struct ethaddr *mac,
		struct ipaddr *ip, uint8_t flags, uint32_t seq, esi_t *esi)
{
	struct bgpevpn *vpn;
	struct prefix_evpn p;

	/* Lookup VNI hash - should exist. */
	vpn = bgp_evpn_lookup_vni(bgp, vni);
	if (!vpn || !is_vni_live(vpn)) {
		flog_warn(EC_BGP_EVPN_VPN_VNI,
			  "%u: VNI hash entry for VNI %u %s at MACIP ADD",
			  bgp->vrf_id, vni, vpn ? "not live" : "not found");
		return -1;
	}

	/* Create EVPN type-2 route and schedule for processing. */
	build_evpn_type2_prefix(&p, mac, ip);
	if (update_evpn_route(bgp, vpn, &p, flags, seq, esi)) {
		flog_err(
			EC_BGP_EVPN_ROUTE_CREATE,
			"%u:Failed to create Type-2 route, VNI %u %s MAC %pEA IP %pIA (flags: 0x%x)",
			bgp->vrf_id, vpn->vni,
			CHECK_FLAG(flags, ZEBRA_MACIP_TYPE_STICKY)
				? "sticky gateway"
				: "",
			mac, ip, flags);
		return -1;
	}

	return 0;
}

/*
 * link_l2vni_hash_to_l3vni -- hash 遍历回调：把属于某 VRF 的 L2VNI 链接到对应 L3VNI。
 *
 * 做什么：取全局 EVPN 实例(断言非空)；若该 VNI 的 tenant_vrf_id 与目标 VRF 的
 *         vrf_id 相同，则调用 bgpevpn_link_to_l3vni 建立 L2VNI 到 L3VNI 的关联。
 * 参数：bucket 为 VNI 哈希桶；bgp_vrf 目标 VRF 实例。
 * 返回：无。
 * 坑：只有 tenant_vrf_id 匹配的 VNI 才链接，避免误连其它 VRF 的 VNI。
 */
static void link_l2vni_hash_to_l3vni(struct hash_bucket *bucket,
				     struct bgp *bgp_vrf)
{
	struct bgpevpn *vpn = (struct bgpevpn *)bucket->data;
	struct bgp *bgp_evpn = NULL;

	bgp_evpn = bgp_get_evpn();
	assert(bgp_evpn);

	if (vpn->tenant_vrf_id == bgp_vrf->vrf_id)
		bgpevpn_link_to_l3vni(vpn);
}

/*
 * bgp_evpn_local_l3vni_add -- 处理本地 L3VNI 新增(zebra 通知)。
 *
 * 做什么：取全局 EVPN 实例(缺失或正在删除则报错返回 -1)；取 AS；按 vrf_id 查找
 *         或自动创建 VRF 实例并标记 BGP_VRF_AUTO；关联 l3vni / svi_ifindex /
 *         anycast 标志；处理 VTEP-IP 变化；用 VRR MAC 作 rmac、SVI MAC 作 sys MAC，
 *         未配置时用 SVI MAC 作 pip MAC；v6 VTEP 时从 loopback 取 pip；据 filter
 *         设置 BGP_VRF_L3VNI_PREFIX_ROUTES_ONLY，否则给各 L2VNI 置双标签标志并更新
 *         Type-2 路由；映射 / 推导自动 RT 与 RD；hash 遍历链接各 L2VNI；非 filter 时
 *         更新 Type-2 路由；最后重发 Type-5 并安装该 L3VNI 的远端路由。
 * 参数：l3vni；vrf_id；svi_rmac 系统 RMAC；vrr_rmac 任播 RMAC；originator_ip VTEP-IP；
 *       filter 是否仅用于前缀路由；svi_ifindex；is_anycast_mac。
 * 返回：成功 0；实例缺失 / AS 或类型不匹配返回 -1。
 * 坑：VRF 可能是自动创建的，需正确置 BGP_VRF_AUTO 以便后续删除。
 */
int bgp_evpn_local_l3vni_add(vni_t l3vni, vrf_id_t vrf_id,
			     struct ethaddr *svi_rmac,
			     struct ethaddr *vrr_rmac,
			     struct ipaddr *originator_ip, int filter,
			     ifindex_t svi_ifindex,
			     bool is_anycast_mac)
{
	struct bgp *bgp_vrf = NULL; /* bgp VRF instance */
	struct bgp *bgp_evpn = NULL; /* EVPN bgp instance */
	struct listnode *node = NULL;
	struct bgpevpn *vpn = NULL;
	as_t as = 0;

	/* get the EVPN instance - required to get the AS number for VRF
	 * auto-creatio
	 */
	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn) {
		flog_err(
			EC_BGP_NO_DFLT,
			"Cannot process L3VNI  %u ADD - EVPN BGP instance not yet created",
			l3vni);
		return -1;
	}

	if (CHECK_FLAG(bgp_evpn->flags, BGP_FLAG_DELETE_IN_PROGRESS)) {
		flog_err(EC_BGP_NO_DFLT,
			  "Cannot process L3VNI %u ADD - EVPN BGP instance is shutting down",
			  l3vni);
		return -1;
	}

	as = bgp_evpn->as;

	/* if the BGP vrf instance doesn't exist - create one */
	bgp_vrf = bgp_lookup_by_vrf_id(vrf_id);
	if (!bgp_vrf) {

		int ret = 0;

		ret = bgp_get_vty(&bgp_vrf, &as, vrf_id_to_name(vrf_id),
				  vrf_id == VRF_DEFAULT
					  ? BGP_INSTANCE_TYPE_DEFAULT
					  : BGP_INSTANCE_TYPE_VRF,
				  NULL, ASNOTATION_UNDEFINED);
		switch (ret) {
		case BGP_ERR_AS_MISMATCH:
			flog_err(EC_BGP_EVPN_AS_MISMATCH,
				 "BGP instance is already running; AS is %s",
				 bgp_vrf->as_pretty);
			return -1;
		case BGP_ERR_INSTANCE_MISMATCH:
			flog_err(EC_BGP_EVPN_INSTANCE_MISMATCH,
				 "BGP instance type mismatch");
			return -1;
		}

		/* mark as auto created */
		SET_FLAG(bgp_vrf->vrf_flags, BGP_VRF_AUTO);
	}

	/* associate the vrf with l3vni and related parameters */
	bgp_vrf->l3vni = l3vni;
	bgp_vrf->l3vni_svi_ifindex = svi_ifindex;
	bgp_vrf->evpn_info->is_anycast_mac = is_anycast_mac;

	/* Update tip_hash of the EVPN underlay BGP instance (bgp_evpn)
	 * if the VTEP-IP (originator_ip) has changed
	 */
	handle_tunnel_ip_change(bgp_vrf, bgp_evpn, vpn, originator_ip);

	/* copy anycast MAC from VRR MAC */
	memcpy(&bgp_vrf->rmac, vrr_rmac, ETH_ALEN);
	/* copy sys RMAC from SVI MAC */
	memcpy(&bgp_vrf->evpn_info->pip_rmac_zebra, svi_rmac, ETH_ALEN);
	/* PIP user configured mac is not present use svi mac as sys mac */
	if (is_zero_mac(&bgp_vrf->evpn_info->pip_rmac_static))
		memcpy(&bgp_vrf->evpn_info->pip_rmac, svi_rmac, ETH_ALEN);
	/* for v6 vtep_ip assign lo primary v6 address as pip,
	 * for v4 vtep_ip bgp instance router-id as pip in bgp_evpn_init.
	 */
	if (IS_IPADDR_V6(&bgp_vrf->originator_ip)) {
		struct interface *ifp;
		struct in6_addr addr;

		ifp = if_get_vrf_loopback(VRF_DEFAULT);
		if (ifp && if_get_ipv6_global(ifp, &addr)) {
			if (bgp_debug_zebra(NULL))
				zlog_debug("%s vni %u ifp %s addr %pI6 copy as pip", __func__,
					   bgp_vrf->l3vni, ifp->name, &addr);
			SET_IPADDR_V6(&bgp_vrf->evpn_info->pip_ip);
			IPV6_ADDR_COPY(&bgp_vrf->evpn_info->pip_ip.ipaddr_v6, &addr);
		} else if (ifp)
			if (bgp_debug_zebra(NULL))
				zlog_debug("%s vni %u ifp %s v6 addr not found, skip pip assignment",
					   __func__, bgp_vrf->l3vni, ifp->name);
	}

	if (bgp_debug_zebra(NULL))
		zlog_debug("VRF %s vni %u pip %s IP %pIA RMAC %pEA sys RMAC %pEA static RMAC %pEA is_anycast_mac %s",
			   vrf_id_to_name(bgp_vrf->vrf_id), bgp_vrf->l3vni,
			   bgp_vrf->evpn_info->advertise_pip ? "enable" : "disable",
			   &bgp_vrf->evpn_info->pip_ip, &bgp_vrf->rmac,
			   &bgp_vrf->evpn_info->pip_rmac, &bgp_vrf->evpn_info->pip_rmac_static,
			   is_anycast_mac ? "Enable" : "Disable");

	/* set the right filter - are we using l3vni only for prefix routes? */
	if (filter) {
		SET_FLAG(bgp_vrf->vrf_flags, BGP_VRF_L3VNI_PREFIX_ROUTES_ONLY);

		/*
		 * VNI_FLAG_USE_TWO_LABELS flag for linked L2VNIs should not be
		 * set before linking vrf to L3VNI. Thus, no need to clear
		 * that explicitly.
		 */
	} else {
		UNSET_FLAG(bgp_vrf->vrf_flags,
			   BGP_VRF_L3VNI_PREFIX_ROUTES_ONLY);

		for (ALL_LIST_ELEMENTS_RO(bgp_vrf->l2vnis, node, vpn)) {
			if (!CHECK_FLAG(vpn->flags, VNI_FLAG_USE_TWO_LABELS)) {

				/*
				 * If we are flapping VNI_FLAG_USE_TWO_LABELS
				 * flag, update all MACIP routes in this VNI
				 */
				SET_FLAG(vpn->flags, VNI_FLAG_USE_TWO_LABELS);
				update_all_type2_routes(bgp_evpn, vpn);
			}
		}
	}

	/* Map auto derive or configured RTs */
	if (!CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_IMPORT_RT_CFGD) ||
	    CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_IMPORT_AUTO_RT_CFGD))
		evpn_auto_rt_import_add_for_vrf(bgp_vrf);
	else
		bgp_evpn_map_vrf_to_its_rts(bgp_vrf);

	if (!CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_EXPORT_RT_CFGD) ||
	    CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_EXPORT_AUTO_RT_CFGD))
		evpn_auto_rt_export_add_for_vrf(bgp_vrf);

	/* auto derive RD */
	bgp_evpn_derive_auto_rd_for_vrf(bgp_vrf);

	/* link all corresponding l2vnis */
	hash_iterate(bgp_evpn->vnihash,
		     (void (*)(struct hash_bucket *,
			       void *))link_l2vni_hash_to_l3vni,
		     bgp_vrf);

	/* Only update all corresponding type-2 routes if we are advertising two
	 * labels along with type-2 routes
	 */
	if (!filter)
		for (ALL_LIST_ELEMENTS_RO(bgp_vrf->l2vnis, node, vpn))
			update_routes_for_vni(bgp_evpn, vpn);

	/* advertise type-5 routes if needed */
	update_advertise_vrf_routes(bgp_vrf);

	/* install all remote routes belonging to this l3vni into corresponding
	 * vrf */
	install_routes_for_vrf(bgp_vrf);

	return 0;
}

/*
 * bgp_evpn_local_l3vni_del -- 处理本地 L3VNI 删除(zebra 通知)。
 *
 * 做什么：按 vrf_id 查 VRF 实例(缺失报错)；取全局 EVPN 实例(缺失或正在删除报错)；
 *         卸载该 VRF 远端路由并删除 / 撤销其全部 Type-5 路由；从 EVPN underlay 的
 *         tip_hash 删除 VTEP-IP；清空 l3vni、rmac、pip_rmac_zebra 及(非静态时的)pip_rmac；
 *         移除 / 解映射 import、export 自动 RT；非 prefix-routes-only 时清除各 L2VNI
 *         的双标签标志并更新 Type-2 路由；把指向本实例的 L2VNI 全部解链；清除
 *         BGP_VRF_L3VNI_PREFIX_ROUTES_ONLY；若为 BGP_VRF_AUTO 自动创建则删除该实例。
 * 参数：l3vni；vrf_id。
 * 返回：成功 0；实例缺失返回 -1。
 * 坑：即便 VRF 是 BGP_VRF_AUTO，也须先手工卸载远端路由，否则路径计数不会被递减。
 */
int bgp_evpn_local_l3vni_del(vni_t l3vni, vrf_id_t vrf_id)
{
	struct bgp *bgp_vrf = NULL;  /* bgp vrf instance */
	struct bgp *bgp_evpn = NULL; /* EVPN bgp instance */
	struct listnode *node = NULL;
	struct listnode *next = NULL;
	struct bgpevpn *vpn = NULL;

	bgp_vrf = bgp_lookup_by_vrf_id(vrf_id);
	if (!bgp_vrf) {
		flog_err(EC_BGP_NO_DFLT,
			 "Cannot process L3VNI %u Del - Could not find BGP instance", l3vni);
		return -1;
	}

	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn) {
		flog_err(EC_BGP_NO_DFLT,
			 "Cannot process L3VNI %u Del - Could not find EVPN BGP instance", l3vni);
		return -1;
	}

	if (CHECK_FLAG(bgp_evpn->flags, BGP_FLAG_DELETE_IN_PROGRESS)) {
		flog_err(EC_BGP_NO_DFLT,
			 "Cannot process L3VNI %u ADD - EVPN BGP instance is shutting down", l3vni);
		return -1;
	}

	/* Remove remote routes from BGT VRF even if BGP_VRF_AUTO is configured,
	 * bgp_delete would not remove/decrement bgp_path_info of the ip_prefix
	 * routes. This will uninstalling the routes from zebra and decremnt the
	 * bgp info count.
	 */
	uninstall_routes_for_vrf(bgp_vrf);

	/* delete/withdraw all type-5 routes */
	delete_withdraw_vrf_routes(bgp_vrf);

	/* Tunnel is no longer active.
	 * Delete VTEP-IP from EVPN underlay's tip_hash.
	 */
	bgp_tip_del(bgp_evpn, &bgp_vrf->originator_ip);

	/* remove the l3vni from vrf instance */
	bgp_vrf->l3vni = 0;

	/* remove the Rmac from the BGP vrf */
	memset(&bgp_vrf->rmac, 0, sizeof(struct ethaddr));
	memset(&bgp_vrf->evpn_info->pip_rmac_zebra, 0, ETH_ALEN);
	if (is_zero_mac(&bgp_vrf->evpn_info->pip_rmac_static) &&
	    !is_zero_mac(&bgp_vrf->evpn_info->pip_rmac))
		memset(&bgp_vrf->evpn_info->pip_rmac, 0, ETH_ALEN);

	/* remove default import RT or Unmap non-default import RT */
	if (!list_isempty(bgp_vrf->vrf_import_rtl)) {
		bgp_evpn_unmap_vrf_from_its_rts(bgp_vrf);
		if (!CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_IMPORT_RT_CFGD))
			list_delete_all_node(bgp_vrf->vrf_import_rtl);
	}

	/* remove default export RT */
	if (!list_isempty(bgp_vrf->vrf_export_rtl) &&
	    !CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_EXPORT_RT_CFGD)) {
		list_delete_all_node(bgp_vrf->vrf_export_rtl);
	}

	/* update all corresponding local mac-ip routes */
	if (!CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_L3VNI_PREFIX_ROUTES_ONLY)) {
		for (ALL_LIST_ELEMENTS_RO(bgp_vrf->l2vnis, node, vpn)) {
			UNSET_FLAG(vpn->flags, VNI_FLAG_USE_TWO_LABELS);
			update_routes_for_vni(bgp_evpn, vpn);
		}
	}

	/* If any L2VNIs point to this instance, unlink them. */
	for (ALL_LIST_ELEMENTS(bgp_vrf->l2vnis, node, next, vpn))
		bgpevpn_unlink_from_l3vni(vpn);

	UNSET_FLAG(bgp_vrf->vrf_flags, BGP_VRF_L3VNI_PREFIX_ROUTES_ONLY);

	/* Delete the instance if it was autocreated */
	if (CHECK_FLAG(bgp_vrf->vrf_flags, BGP_VRF_AUTO))
		bgp_delete(bgp_vrf);

	return 0;
}

/*
 * bgp_evpn_l2vni_remote_route_processing -- 把 L2VNI 加入延迟处理队列以安装远端路由。
 *
 * 做什么：若该 VNI 未置 VNI_FLAG_ADD，则挂入 bm->zebra_l2_vni_head 的 FIFO 并置标志；
 *         随后据 FIFO 中 VNI 数量决定调度策略：FIFO 非空时用 10ms 延时定时器调度
 *         bgp_zebra_process_remote_routes_for_l2vni，否则立即调度该处理器。
 * 参数：vpn 目标 L2VNI。
 * 返回：无。
 * 坑：zebra 批量下发 L2VNI 时逐个遍历全局表开销巨大，故改为队列 + 分批处理；
 *     已处理过的 VNI 再次到来时立即调度，避免无谓等待。
 */
static void bgp_evpn_l2vni_remote_route_processing(struct bgpevpn *vpn)
{
	/*
	 * Anytime BGP gets a Bulk of L2 VNIs ADD/UPD from zebra,
	 *  - Walking the entire global routing table per VNI is very expensive.
	 *  - The next read (say of another VNI ADD/UPD) from the socket does
	 *    not proceed unless this walk is complete.
	 *  This results in huge output buffer FIFO growth spiking up the
	 *  memory in zebra.
	 *
	 * To avoid this, idea is to hookup the VPN off the struct bgp_master
	 * and maintain a VPN FIFO list which is processed later on, where we
	 * walk a chunk of VPNs and do the remote route install.
	 */
	if (!CHECK_FLAG(vpn->flags, VNI_FLAG_ADD)) {
		zebra_l2_vni_add_tail(&bm->zebra_l2_vni_head, vpn);
		SET_FLAG(vpn->flags, VNI_FLAG_ADD);
	}

	if (BGP_DEBUG(zebra, ZEBRA))
		zlog_debug("Scheduling L2VNI ADD to be processed later for VNI %u", vpn->vni);

	/*
	 * If there are no VNI's in the bgp VPN FIFO list i.e. an update
	 * for an already processed VNI comes in, schedule the remote
	 * route install immediately.
	 *
	 * In all other cases, it is ok to schedule the remote route install
	 * after a small sleep. This is to give benefit of doubt in case more
	 * L2VNI ADD events come.
	 */
	if (zebra_l2_vni_count(&bm->zebra_l2_vni_head))
		event_add_timer_msec(bm->master, bgp_zebra_process_remote_routes_for_l2vni, NULL,
				     10, &bm->t_bgp_zebra_l2_vni);
	else
		event_add_event(bm->master, bgp_zebra_process_remote_routes_for_l2vni, NULL, 0,
				&bm->t_bgp_zebra_l2_vni);
}

/*
 * bgp_evpn_instance_down -- BGP 实例 down 时清理 EVPN 遗留状态。
 *
 * 做什么：若该实例带有 l3vni，则调用 bgp_evpn_local_l3vni_del 删除残留的本地 L3VNI。
 * 参数：bgp 实例。
 * 返回：无。
 * 坑：仅处理 L3VNI 残留，不做整表清理。
 */
/*
 * When bgp instance goes down also clean up what might have been left over
 * from evpn.
 */
void bgp_evpn_instance_down(struct bgp *bgp)
{
	/* If we have a stale local vni, delete it */
	if (bgp->l3vni)
		bgp_evpn_local_l3vni_del(bgp->l3vni, bgp->vrf_id);
}

/*
 * bgp_evpn_local_vni_del -- 处理本地 VNI 删除(zebra 通知)。
 *
 * 做什么：查 VNI，未找到直接返回 0；清除 VNI_FLAG_ADD 并把该 VNI 从 zebra L2VNI
 *         FIFO 移除；随后删除该 VNI 的全部本地路由并调度处理(以便向对端撤销)。
 * 参数：bgp 实例；vni。
 * 返回：成功 0(含 VNI 不存在的情形)。
 * 坑：VNI 不存在返回 0 而非错误，属幂等处理。
 */
/*
 * Handle del of a local VNI.
 */
int bgp_evpn_local_vni_del(struct bgp *bgp, vni_t vni)
{
	struct bgpevpn *vpn;

	/* Locate VNI hash */
	vpn = bgp_evpn_lookup_vni(bgp, vni);
	if (!vpn)
		return 0;

	/* Remove the VPN from the bgp VPN FIFO (if exists) */
	UNSET_FLAG(vpn->flags, VNI_FLAG_ADD);
	zebra_l2_vni_del(&bm->zebra_l2_vni_head, vpn);

	/* Remove all local EVPN routes and schedule for processing (to
	 * withdraw from peers).
	 */
	delete_routes_for_vni(bgp, vpn);

	bgp_evpn_unlink_from_vni_svi_hash(bgp, vpn);

	vpn->svi_ifindex = 0;
	/* Tunnel is no longer active.
	 * Delete VTEP-IP from EVPN underlay's tip_hash.
	 */
	bgp_tip_del(bgp, &vpn->originator_ip);

	/* Clear "live" flag and see if hash needs to be freed. */
	UNSET_FLAG(vpn->flags, VNI_FLAG_LIVE);
	/* Pop items from bgp_zebra_announce FIFO for any VPN routes pending*/
	bgp_zebra_evpn_pop_items_from_announce_fifo(vpn);
	if (!is_vni_configured(vpn))
		bgp_evpn_free(bgp, vpn);

	return 0;
}

/*
 * bgp_evpn_local_vni_add -- 处理本地 VNI 的新增或更新(zebra 通知)。
 *
 * 做什么：查 VNI；若已存在且 originator_ip / mcast_grp / tenant_vrf_id / svi_ifindex
 *         均未变化则直接返回 0(视为无关注变化)。否则分别处理：多播组变化、
 *         SVI 变化(重解 / 重绑网关 IP 下一跳)、tenant VRF 变化(重解 l3vni 关联)、
 *         隧道端点 IP 变化；若 VNI 存活则更新其路由。VNI 不存在则由 bgp_evpn_new 创建。
 *         随后若尚非存活：置 VNI_FLAG_LIVE；bgp_tip_add 加入隧道 IP(命中则过滤
 *         Martian TUN_IP 路由)；头端复制模式下创建 Type-3 路由；把 gw / svi macip
 *         通告设置下发给 zebra；最后把该 VNI 加入远端路由延迟处理队列。
 * 参数：bgp 实例；vni；originator_ip VTEP-IP；tenant_vrf_id；mcast_grp；svi_ifindex。
 * 返回：无关注变化或成功返回 0；Type-3 创建失败返回 -1。
 * 坑：这是 VNI 生命周期的核心入口，多处状态变化都要在此同步。
 */
/*
 * Handle add (or update) of a local VNI. The VNI changes we care
 * about are for the local-tunnel-ip and the (tenant) VRF.
 */
int bgp_evpn_local_vni_add(struct bgp *bgp, vni_t vni,
			   struct ipaddr *originator_ip,
			   vrf_id_t tenant_vrf_id,
			   struct in_addr mcast_grp,
			   ifindex_t svi_ifindex)
{
	struct bgpevpn *vpn;
	struct prefix_evpn p;
	struct bgp *bgp_evpn = bgp_get_evpn();

	/* Lookup VNI. If present and no change, exit. */
	vpn = bgp_evpn_lookup_vni(bgp, vni);
	if (vpn) {

		if (is_vni_live(vpn)
		    && ipaddr_is_same(&vpn->originator_ip, originator_ip)
		    && IPV4_ADDR_SAME(&vpn->mcast_grp, &mcast_grp)
		    && vpn->tenant_vrf_id == tenant_vrf_id
		    && vpn->svi_ifindex == svi_ifindex)
			/* Probably some other param has changed that we don't
			 * care about.
			 */
			return 0;

		bgp_evpn_mcast_grp_change(bgp, vpn, mcast_grp);

		if (vpn->svi_ifindex != svi_ifindex) {

			/*
			 * Unresolve all the gateway IP nexthops for this VNI
			 * for old SVI
			 */
			bgp_evpn_remote_ip_hash_iterate(
				vpn,
				(void (*)(struct hash_bucket *, void *))
					bgp_evpn_remote_ip_hash_unlink_nexthop,
				vpn);
			bgp_evpn_unlink_from_vni_svi_hash(bgp, vpn);
			vpn->svi_ifindex = svi_ifindex;
			bgp_evpn_link_to_vni_svi_hash(bgp, vpn);

			/*
			 * Resolve all the gateway IP nexthops for this VNI
			 * for new SVI
			 */
			bgp_evpn_remote_ip_hash_iterate(
				vpn,
				(void (*)(struct hash_bucket *, void *))
					bgp_evpn_remote_ip_hash_link_nexthop,
				vpn);
		}

		/* Update tenant_vrf_id if it has changed. */
		if (vpn->tenant_vrf_id != tenant_vrf_id) {

			/*
			 * Unresolve all the gateway IP nexthops for this VNI
			 * in old tenant vrf
			 */
			bgp_evpn_remote_ip_hash_iterate(
				vpn,
				(void (*)(struct hash_bucket *, void *))
					bgp_evpn_remote_ip_hash_unlink_nexthop,
				vpn);
			bgpevpn_unlink_from_l3vni(vpn);
			vpn->tenant_vrf_id = tenant_vrf_id;
			bgpevpn_link_to_l3vni(vpn);

			/*
			 * Resolve all the gateway IP nexthops for this VNI
			 * in new tenant vrf
			 */
			bgp_evpn_remote_ip_hash_iterate(
				vpn,
				(void (*)(struct hash_bucket *, void *))
					bgp_evpn_remote_ip_hash_link_nexthop,
				vpn);
		}

		/* If tunnel endpoint IP has changed, update (and delete prior
		 * type-3 route, if needed.)
		 */
		handle_tunnel_ip_change(NULL, bgp, vpn, originator_ip);

		/* Update all routes with new endpoint IP and/or export RT
		 * for VRFs
		 */
		if (is_vni_live(vpn))
			update_routes_for_vni(bgp, vpn);
	} else {
		/* Create or update as appropriate. */
		vpn = bgp_evpn_new(bgp, vni, originator_ip, tenant_vrf_id,
				   mcast_grp, svi_ifindex);
	}

	/* if the VNI is live already, there is nothing more to do */
	if (is_vni_live(vpn))
		return 0;

	/* Mark as "live" */
	SET_FLAG(vpn->flags, VNI_FLAG_LIVE);

	/* Tunnel is newly active.
	 * Add TIP to tip_hash of the EVPN underlay instance (bgp_get_evpn()).
	 */
	if (bgp_tip_add(bgp, originator_ip))
		/* The originator_ip was not already present in the
		 * bgp martian next-hop table as a tunnel-ip, so we
		 * need to go back and filter routes matching the new
		 * martian next-hop.
		 */
		bgp_filter_evpn_routes_upon_martian_change(bgp_evpn,
							   BGP_MARTIAN_TUN_IP);

	/*
	 * Create EVPN type-3 route and schedule for processing.
	 *
	 * RT-3 only if doing head-end replication
	 */
	if (bgp_evpn_vni_flood_mode_get(bgp, vpn)
			== VXLAN_FLOOD_HEAD_END_REPL) {
		build_evpn_type3_prefix(&p, &vpn->originator_ip);
		if (update_evpn_route(bgp, vpn, &p, 0, 0, NULL)) {
			flog_err(EC_BGP_EVPN_ROUTE_CREATE,
				 "%u: Type3 route creation failure for VNI %u",
				 bgp->vrf_id, vni);
			return -1;
		}
	}

	/* If we are advertising gateway mac-ip
	   It needs to be conveyed again to zebra */
	bgp_zebra_advertise_gw_macip(bgp, vpn->advertise_gw_macip, vpn->vni);

	/* advertise svi mac-ip knob to zebra */
	bgp_zebra_advertise_svi_macip(bgp, vpn->advertise_svi_macip, vpn->vni);

	bgp_evpn_l2vni_remote_route_processing(vpn);

	return 0;
}

/*
 * bgp_evpn_flood_control_change -- 处理 BUM 泛洪处理设置的变化。
 *
 * 做什么：hash_iterate 遍历 vnihash，对每个 VNI 调用 advertise_withdraw_type3，
 *         由后者按新泛洪模式发布或撤销该 VNI 的 Type-3 路由并同步 zebra。
 * 参数：bgp 实例。
 * 返回：无。
 * 坑：头端复制模式需发布 RT-3，丢弃 BUM 模式需撤销 RT-3，均由回调统一处理。
 */
/*
 * Handle change in setting for BUM handling. The supported values
 * are head-end replication and dropping all BUM packets. Any change
 * should be registered with zebra. Also, if doing head-end replication,
 * need to advertise local VNIs as EVPN RT-3 whereas, if BUM packets are
 * to be dropped, the RT-3s must be withdrawn.
 */
void bgp_evpn_flood_control_change(struct bgp *bgp)
{
	hash_iterate(bgp->vnihash, advertise_withdraw_type3, bgp);
}

/*
 * bgp_evpn_cleanup_on_disable -- 关闭 EVPN(如 no advertise-all-vni)时清理信息并撤销路由。
 *
 * 做什么：先清空本实例在 bm->zebra_l2_vni_head 中的 L2VNI FIFO(逐个弹出并清
 *         VNI_FLAG_ADD)；再 hash_iterate 对 vnihash 调用 cleanup_vni_on_disable
 *         逐个清理 VNI 并删除 / 撤销其路由。
 * 参数：bgp 实例。
 * 返回：无。
 * 坑：必须先清 FIFO 再逐个清理，避免残留项指向已释放的 VNI。
 */
/*
 * Cleanup EVPN information on disable - Need to delete and withdraw
 * EVPN routes from peers.
 */
void bgp_evpn_cleanup_on_disable(struct bgp *bgp)
{
	struct bgpevpn *vpn = NULL;
	uint32_t vni_count = zebra_l2_vni_count(&bm->zebra_l2_vni_head);

	/* Cleanup VNI FIFO list from this bgp instance */
	while (vni_count) {
		vpn = zebra_l2_vni_pop(&bm->zebra_l2_vni_head);
		UNSET_FLAG(vpn->flags, VNI_FLAG_ADD);
		vni_count--;
	}

	hash_iterate(bgp->vnihash, (void (*)(struct hash_bucket *, void *))cleanup_vni_on_disable,
		     bgp);
}

/*
 * bgp_evpn_cleanup -- 清理 EVPN 全部信息(bgpd 退出或默认 BGP 实例释放时调用)。
 *
 * 做什么：hash_iterate 对 vnihash 调用 free_vni_entry 释放全部 VNI；依次
 *         hash_clean_and_free 释放 import_rt_hash / vrf_import_rt_hash /
 *         vni_svi_hash / vnihash；list_delete 释放 vrf_import_rtl / vrf_export_rtl /
 *         l2vnis；释放 evpn_info(含 soo)；释放 vrf_prd_pretty。
 * 参数：bgp 实例。
 * 返回：无。
 * 坑：这是实例级终态清理，需保证之前的引用都已解除。
 */
/*
 * Cleanup EVPN information - invoked at the time of bgpd exit or when the
 * BGP instance (default) is being freed.
 */
void bgp_evpn_cleanup(struct bgp *bgp)
{
	hash_iterate(bgp->vnihash,
		     (void (*)(struct hash_bucket *, void *))free_vni_entry,
		     bgp);

	hash_clean_and_free(&bgp->import_rt_hash,
			    (void (*)(void *))hash_import_rt_free);

	hash_clean_and_free(&bgp->vrf_import_rt_hash,
			    (void (*)(void *))hash_vrf_import_rt_free);

	hash_clean_and_free(&bgp->vni_svi_hash,
			    (void (*)(void *))hash_evpn_free);

	/*
	 * Why is the vnihash freed at the top of this function and
	 * then deleted here?
	 */
	hash_clean_and_free(&bgp->vnihash, NULL);

	list_delete(&bgp->vrf_import_rtl);
	list_delete(&bgp->vrf_export_rtl);
	list_delete(&bgp->l2vnis);

	if (bgp->evpn_info) {
		ecommunity_free(&bgp->evpn_info->soo);
		XFREE(MTYPE_BGP_EVPN_INFO, bgp->evpn_info);
	}

	if (bgp->vrf_prd_pretty)
		XFREE(MTYPE_BGP_NAME, bgp->vrf_prd_pretty);
}

/*
 * bgp_evpn_init -- 初始化 EVPN：创建 VNI 哈希表与 RT 到 VNI 的哈希表等。
 *
 * 做什么：创建 vnihash / vni_svi_hash / import_rt_hash / vrf_import_rt_hash 四张
 *         哈希表；创建 vrf_import_rtl / vrf_export_rtl(含比较器与析构器)与 l2vnis
 *         链表；分配 evpn_info 并设重复地址检测(DAD)默认值(开启、最大移动 5 次、
 *         检测 180s、仅告警、冻结关闭)并同步 zebra；对 VRF 实例默认开启 PIP 且以
 *         默认实例 router-id 作 pip；默认 BUM 处理为头端复制；最后 bgp_evpn_nh_init。
 * 参数：bgp 实例。
 * 返回：无。
 * 坑：DAD 默认是 warning-only，且 VRF 实例默认启用 PIP。
 */
/*
 * Initialization for EVPN
 * Create
 *  VNI hash table
 *  hash for RT to VNI
 */
void bgp_evpn_init(struct bgp *bgp)
{
	bgp->vnihash =
		hash_create(vni_hash_key_make, vni_hash_cmp, "BGP VNI Hash");
	bgp->vni_svi_hash =
		hash_create(vni_svi_hash_key_make, vni_svi_hash_cmp,
			    "BGP VNI hash based on SVI ifindex");
	bgp->import_rt_hash =
		hash_create(import_rt_hash_key_make, import_rt_hash_cmp,
			    "BGP Import RT Hash");
	bgp->vrf_import_rt_hash =
		hash_create(vrf_import_rt_hash_key_make, vrf_import_rt_hash_cmp,
			    "BGP VRF Import RT Hash");
	bgp->vrf_import_rtl = list_new();
	bgp->vrf_import_rtl->cmp =
		(int (*)(void *, void *))evpn_vrf_route_target_cmp;
	bgp->vrf_import_rtl->del = evpn_vrf_rt_del;
	bgp->vrf_export_rtl = list_new();
	bgp->vrf_export_rtl->cmp =
		(int (*)(void *, void *))evpn_vrf_route_target_cmp;
	bgp->vrf_export_rtl->del = evpn_vrf_rt_del;
	bgp->l2vnis = list_new();
	bgp->l2vnis->cmp = vni_list_cmp;
	bgp->evpn_info =
		XCALLOC(MTYPE_BGP_EVPN_INFO, sizeof(struct bgp_evpn_info));
	/* By default Duplicate Address Dection is enabled.
	 * Max-moves (N) 5, detection time (M) 180
	 * default action is warning-only
	 * freeze action permanently freezes address,
	 * and freeze time (auto-recovery) is disabled.
	 */
	if (bgp->evpn_info) {
		bgp->evpn_info->dup_addr_detect = true;
		bgp->evpn_info->dad_time = EVPN_DAD_DEFAULT_TIME;
		bgp->evpn_info->dad_max_moves = EVPN_DAD_DEFAULT_MAX_MOVES;
		bgp->evpn_info->dad_freeze = false;
		bgp->evpn_info->dad_freeze_time = 0;
		/* Initialize zebra vxlan */
		bgp_zebra_dup_addr_detection(bgp);
		/* Enable PIP feature by default for bgp vrf instance */
		if (bgp->inst_type == BGP_INSTANCE_TYPE_VRF) {
			struct bgp *bgp_default;

			bgp->evpn_info->advertise_pip = true;
			bgp_default = bgp_get_default();
			if (bgp_default) {
				SET_IPADDR_V4(&bgp->evpn_info->pip_ip);
				bgp->evpn_info->pip_ip.ipaddr_v4 = bgp_default->router_id;
			}
		}
	}

	/* Default BUM handling is to do head-end replication. */
	bgp->vxlan_flood_ctrl = VXLAN_FLOOD_HEAD_END_REPL;

	bgp_evpn_nh_init(bgp);
}

/*
 * bgp_evpn_vrf_delete -- 删除 VRF 时清理其 EVPN 相关资源。
 *
 * 做什么：解除该 VRF 与其 RT 的映射(bgp_evpn_unmap_vrf_from_its_rts)，
 *         并调用 bgp_evpn_nh_finish 释放 EVPN 下一跳跟踪相关资源。
 * 参数：bgp_vrf VRF 实例。
 * 返回：无。
 * 坑：与初始化时的 bgp_evpn_nh_init 对称，须成对调用。
 */
void bgp_evpn_vrf_delete(struct bgp *bgp_vrf)
{
	bgp_evpn_unmap_vrf_from_its_rts(bgp_vrf);
	bgp_evpn_nh_finish(bgp_vrf);
}

/*
 * bgp_evpn_get_type5_prefixlen -- 取 Type-5 EVPN 路由内所携带 IP 前缀的前缀长度。
 *
 * 做什么：若 pfx 为空或族非 AF_EVPN 返回 0；若路由类型不是 IP-PREFIX 返回 0；
 *         否则返回 prefix_addr.ip_prefix_length。
 * 参数：pfx 前缀。
 * 返回：Type-5 前缀长度；不适用时返回 0。
 * 坑：对非 Type-5 路由一律返回 0，调用方需自行区分"长度为 0"与"不适用"。
 */
/*
 * Get the prefixlen of the ip prefix carried within the type5 evpn route.
 */
int bgp_evpn_get_type5_prefixlen(const struct prefix *pfx)
{
	struct prefix_evpn *evp = (struct prefix_evpn *)pfx;

	if (!pfx || pfx->family != AF_EVPN)
		return 0;

	if (evp->prefix.route_type != BGP_EVPN_IP_PREFIX_ROUTE)
		return 0;

	return evp->prefix.prefix_addr.ip_prefix_length;
}

/*
 * bgp_evpn_is_prefix_nht_supported -- 判断该 EVPN 前缀是否需要注册下一跳跟踪(NHT)。
 *
 * 做什么：仅当 pfx 非空、族为 AF_EVPN 且路由类型为 Type-1(AD) / Type-2(MAC-IP) /
 *         Type-3(IMET) / Type-4(ES) / Type-5(IP-PREFIX)时返回 true，否则 false。
 * 参数：pfx 前缀。
 * 返回：需要 NHT 返回 true，否则 false。
 * 坑：EVPN 路由只有下一跳可达才应置 VALID 并被导入 / 通告，此判断决定是否跟踪。
 */
/*
 * Should we register nexthop for this EVPN prefix for nexthop tracking?
 */
bool bgp_evpn_is_prefix_nht_supported(const struct prefix *pfx)
{
	struct prefix_evpn *evp = (struct prefix_evpn *)pfx;

	/*
	 * EVPN routes should be marked as valid only if the nexthop is
	 * reachable. Only if this happens, the route should be imported
	 * (into VNI or VRF routing tables) and/or advertised.
	 * Note: This is currently applied for EVPN type-1, type-2,
	 * type-3, type-4 and type-5 routes.
	 * It may be tweaked later on for other routes, or
	 * even removed completely when all routes are handled.
	 */
	if (pfx && pfx->family == AF_EVPN
	    && (evp->prefix.route_type == BGP_EVPN_MAC_IP_ROUTE
		|| evp->prefix.route_type == BGP_EVPN_AD_ROUTE
		|| evp->prefix.route_type == BGP_EVPN_ES_ROUTE
		|| evp->prefix.route_type == BGP_EVPN_IMET_ROUTE
		|| evp->prefix.route_type == BGP_EVPN_IP_PREFIX_ROUTE))
		return true;

	return false;
}

/*
 * bgp_evpn_remote_ip_hash_alloc -- 远端 IP 哈希表的分配回调。
 * 做什么：分配 struct evpn_remote_ip 并拷贝键内容，初始化其 macip_path_list 链表。
 * 参数：p 指向作为键的 evpn_remote_ip。
 * 返回：新分配的 evpn_remote_ip 指针。
 * 坑：分配时即建好链表，释放时需对应 list_delete。
 */
static void *bgp_evpn_remote_ip_hash_alloc(void *p)
{
	const struct evpn_remote_ip *key = (const struct evpn_remote_ip *)p;
	struct evpn_remote_ip *ip;

	ip = XMALLOC(MTYPE_EVPN_REMOTE_IP, sizeof(struct evpn_remote_ip));
	*ip = *key;
	ip->macip_path_list = list_new();

	return ip;
}

/*
 * bgp_evpn_remote_ip_hash_key_make -- 远端 IP 哈希表的键生成函数。
 * 做什么：IPv4 用 jhash_1word 对 v4 地址散列；IPv6 用 jhash2 对 v6 地址字数组散列。
 * 参数：p 指向 evpn_remote_ip。
 * 返回：哈希值。
 * 坑：v4 / v6 走不同散列路径，需与 cmp 逻辑保持一致。
 */
static unsigned int bgp_evpn_remote_ip_hash_key_make(const void *p)
{
	const struct evpn_remote_ip *ip = p;
	const struct ipaddr *addr = &ip->addr;

	if (IS_IPADDR_V4(addr))
		return jhash_1word(addr->ipaddr_v4.s_addr, 0);

	return jhash2(addr->ipaddr_v6.s6_addr32,
		      array_size(addr->ipaddr_v6.s6_addr32), 0);
}

/*
 * bgp_evpn_remote_ip_hash_cmp -- 远端 IP 哈希表的键比较函数。
 * 做什么：用 ipaddr_cmp 比较两个 evpn_remote_ip 的 addr，相等返回 true。
 * 参数：p1 / p2 待比较的两个 evpn_remote_ip。
 * 返回：地址相同返回 true，否则 false。
 * 坑：比较的是 addr 字段(可 v4 可 v6)，与键生成函数对应。
 */
static bool bgp_evpn_remote_ip_hash_cmp(const void *p1, const void *p2)
{
	const struct evpn_remote_ip *ip1 = p1;
	const struct evpn_remote_ip *ip2 = p2;

	return !ipaddr_cmp(&ip1->addr, &ip2->addr);
}

/*
 * bgp_evpn_remote_ip_hash_init -- 初始化 VNI 的远端 IP 哈希表。
 * 做什么：仅当 evpn_resolve_overlay_index() 开启时才创建 vpn->remote_ip_hash。
 * 参数：vpn 目标 VNI。
 * 返回：无。
 * 坑：功能关闭时不建表，后续访问需先判断 remote_ip_hash 是否为空。
 */
static void bgp_evpn_remote_ip_hash_init(struct bgpevpn *vpn)
{
	if (!evpn_resolve_overlay_index())
		return;

	vpn->remote_ip_hash = hash_create(bgp_evpn_remote_ip_hash_key_make,
					  bgp_evpn_remote_ip_hash_cmp,
					  "BGP EVPN remote IP hash");
}

/*
 * bgp_evpn_remote_ip_hash_free -- 远端 IP 哈希表条目释放回调。
 * 做什么：对该 IP 调用 bgp_evpn_remote_ip_process_nexthops(unlink)；删除其
 *         macip_path_list 链表；从哈希表 release 并释放本体。
 * 参数：bucket 哈希桶；args 为 struct bgpevpn *。
 * 返回：无。
 * 坑：释放前需先解链下一跳，避免残留跟踪引用。
 */
static void bgp_evpn_remote_ip_hash_free(struct hash_bucket *bucket, void *args)
{
	struct evpn_remote_ip *ip = (struct evpn_remote_ip *)bucket->data;
	struct bgpevpn *vpn = (struct bgpevpn *)args;

	bgp_evpn_remote_ip_process_nexthops(vpn, &ip->addr, false);

	list_delete(&ip->macip_path_list);

	hash_release(vpn->remote_ip_hash, ip);
	XFREE(MTYPE_EVPN_REMOTE_IP, ip);
}

/*
 * bgp_evpn_remote_ip_hash_destroy -- 销毁 VNI 的远端 IP 哈希表。
 * 做什么：功能关闭或表为空则返回；否则 hash_iterate 逐个调用 free 回调，再 hash_free
 *         并置 vpn->remote_ip_hash 为 NULL。
 * 参数：vpn 目标 VNI。
 * 返回：无。
 * 坑：释放后必须把指针置 NULL，防止后续误用。
 */
static void bgp_evpn_remote_ip_hash_destroy(struct bgpevpn *vpn)
{
	if (!evpn_resolve_overlay_index() || vpn->remote_ip_hash == NULL)
		return;

	hash_iterate(vpn->remote_ip_hash,
	(void (*)(struct hash_bucket *, void *))bgp_evpn_remote_ip_hash_free,
	vpn);

	hash_free(vpn->remote_ip_hash);
	vpn->remote_ip_hash = NULL;
}

/*
 * bgp_evpn_remote_ip_hash_add -- 把一条远端 MAC / IP 路由加入远端 IP 哈希表。
 * 做什么：功能关闭则返回；仅处理从对端导入(BGP 且 IMPORTED 子类型)且 VALID 的
 *         Type-2 路由(含 IP)；以路由中的 IP 为键查找，已有条目则把 pi 追加到其
 *         macip_path_list，否则新建条目后再解链下一跳。
 * 参数：vpn 目标 VNI；pi 远端路径。
 * 返回：无。
 * 坑：仅 MAC-IP 且带 IP 的路由才纳入；重复加入时会先去重。
 */
/* Add a remote MAC/IP route to hash table */
static void bgp_evpn_remote_ip_hash_add(struct bgpevpn *vpn,
					struct bgp_path_info *pi)
{
	struct evpn_remote_ip tmp;
	struct evpn_remote_ip *ip;
	struct prefix_evpn *evp;

	if (!evpn_resolve_overlay_index())
		return;

	if (pi->type != ZEBRA_ROUTE_BGP || pi->sub_type != BGP_ROUTE_IMPORTED
	    || !CHECK_FLAG(pi->flags, BGP_PATH_VALID))
		return;

	evp = (struct prefix_evpn *)&pi->net->rn->p;

	if (evp->family != AF_EVPN
	    || evp->prefix.route_type != BGP_EVPN_MAC_IP_ROUTE
	    || is_evpn_prefix_ipaddr_none(evp))
		return;

	tmp.addr = evp->prefix.macip_addr.ip;
	ip = hash_lookup(vpn->remote_ip_hash, &tmp);
	if (ip) {
		if (listnode_lookup(ip->macip_path_list, pi) != NULL)
			return;
		(void)listnode_add(ip->macip_path_list, pi);
		return;
	}

	ip = hash_get(vpn->remote_ip_hash, &tmp, bgp_evpn_remote_ip_hash_alloc);
	(void)listnode_add(ip->macip_path_list, pi);

	bgp_evpn_remote_ip_process_nexthops(vpn, &ip->addr, true);
}

/*
 * bgp_evpn_remote_ip_hash_del -- 从远端 IP 哈希表中删除一条远端 MAC / IP 路由。
 * 做什么：功能关闭或无对应 IP 条目则返回；否则从该 IP 的 macip_path_list 删除 pi，
 *         若链表已空则解链下一跳、release 该条目并释放本体。
 * 参数：vpn 目标 VNI；pi 远端路径。
 * 返回：无。
 * 坑：仅当其 macip_path_list 为空时才释放 IP 条目，多个 pi 共享同一 IP 条目。
 */
/* Delete a remote MAC/IP route from hash table */
static void bgp_evpn_remote_ip_hash_del(struct bgpevpn *vpn,
					struct bgp_path_info *pi)
{
	struct evpn_remote_ip tmp;
	struct evpn_remote_ip *ip;
	struct prefix_evpn *evp;

	if (!evpn_resolve_overlay_index())
		return;

	evp = (struct prefix_evpn *)&pi->net->rn->p;

	if (evp->family != AF_EVPN
	    || evp->prefix.route_type != BGP_EVPN_MAC_IP_ROUTE
	    || is_evpn_prefix_ipaddr_none(evp))
		return;

	tmp.addr = evp->prefix.macip_addr.ip;
	ip = hash_lookup(vpn->remote_ip_hash, &tmp);
	if (ip == NULL)
		return;

	listnode_delete(ip->macip_path_list, pi);

	if (ip->macip_path_list->count == 0) {
		bgp_evpn_remote_ip_process_nexthops(vpn, &ip->addr, false);
		hash_release(vpn->remote_ip_hash, ip);
		list_delete(&ip->macip_path_list);
		XFREE(MTYPE_EVPN_REMOTE_IP, ip);
	}
}

/*
 * bgp_evpn_remote_ip_hash_iterate -- 遍历 VNI 的远端 IP 哈希表并执行回调。
 * 做什么：功能关闭则返回；否则对 vpn->remote_ip_hash 调用 hash_iterate(func, arg)。
 * 参数：vpn 目标 VNI；func 回调；arg 回调参数。
 * 返回：无。
 * 坑：功能未开启时静默跳过遍历。
 */
static void bgp_evpn_remote_ip_hash_iterate(struct bgpevpn *vpn,
					    void (*func)(struct hash_bucket *,
							 void *),
					    void *arg)
{
	if (!evpn_resolve_overlay_index())
		return;

	hash_iterate(vpn->remote_ip_hash, func, arg);
}

/*
 * show_remote_ip_entry -- vty 输出回调：打印单个远端 IP 及其关联的 MAC / IP 路由。
 * 做什么：以 args 为 struct vty *，输出该远端 IP，再遍历其 macip_path_list 打印每条路由前缀。
 * 参数：bucket 哈希桶(数据为 evpn_remote_ip)；args 为 struct vty *。
 * 返回：无。
 * 坑：args 是 vty 而非 VNI，注意与其它遍历回调区分。
 */
static void show_remote_ip_entry(struct hash_bucket *bucket, void *args)
{
	char buf[INET6_ADDRSTRLEN];
	struct listnode *node = NULL;
	struct bgp_path_info *pi = NULL;
	struct vty *vty = (struct vty *)args;
	struct evpn_remote_ip *ip = (struct evpn_remote_ip *)bucket->data;

	vty_out(vty, "  Remote IP: %s\n",
		ipaddr2str(&ip->addr, buf, sizeof(buf)));
	vty_out(vty, "      Linked MAC/IP routes:\n");
	for (ALL_LIST_ELEMENTS_RO(ip->macip_path_list, node, pi))
		vty_out(vty, "        %pFX\n", &pi->net->rn->p);
}

/*
 * bgp_evpn_show_remote_ip_hash -- vty 输出回调：打印某 VNI 的远端 IP 哈希表内容。
 * 做什么：以 bucket 数据为 VNI、args 为 vty，先打印 VNI 号，再遍历其远端 IP 哈希表
 *         调用 show_remote_ip_entry，最后打印空行。
 * 参数：bucket VNI 哈希桶；args 为 struct vty *。
 * 返回：无。
 * 坑：这是对 vnihash 遍历时的回调，内部又对远端 IP 哈希再遍历一次。
 */
void bgp_evpn_show_remote_ip_hash(struct hash_bucket *bucket, void *args)
{
	struct bgpevpn *vpn = (struct bgpevpn *)bucket->data;
	struct vty *vty = (struct vty *)args;

	vty_out(vty, "VNI: %u\n", vpn->vni);
	bgp_evpn_remote_ip_hash_iterate(
		vpn,
		(void (*)(struct hash_bucket *, void *))show_remote_ip_entry,
		vty);
	vty_out(vty, "\n");
}

/*
 * bgp_evpn_remote_ip_hash_link_nexthop -- 遍历回调：为远端 IP 建立下一跳跟踪。
 * 做什么：取出 bucket 中的 evpn_remote_ip 与 args 中的 VNI，对该 IP 调用
 *         bgp_evpn_remote_ip_process_nexthops(resolve=true)。
 * 参数：bucket 哈希桶(数据为 evpn_remote_ip)；args 为 struct bgpevpn *。
 * 返回：无。
 * 坑：SVI 或 tenant VRF 变化后对全部远端 IP 重新解链。
 */
static void bgp_evpn_remote_ip_hash_link_nexthop(struct hash_bucket *bucket,
						 void *args)
{
	struct evpn_remote_ip *ip = (struct evpn_remote_ip *)bucket->data;
	struct bgpevpn *vpn = (struct bgpevpn *)args;

	bgp_evpn_remote_ip_process_nexthops(vpn, &ip->addr, true);
}

/*
 * bgp_evpn_remote_ip_hash_unlink_nexthop -- 遍历回调：解除远端 IP 的下一跳跟踪。
 * 做什么：取出 bucket 中的 evpn_remote_ip 与 args 中的 VNI，对该 IP 调用
 *         bgp_evpn_remote_ip_process_nexthops(resolve=false)。
 * 参数：bucket 哈希桶(数据为 evpn_remote_ip)；args 为 struct bgpevpn *。
 * 返回：无。
 * 坑：与 link_nexthop 相反，用于 SVI / VRF 变化前的解绑。
 */
static void bgp_evpn_remote_ip_hash_unlink_nexthop(struct hash_bucket *bucket,
						   void *args)
{
	struct evpn_remote_ip *ip = (struct evpn_remote_ip *)bucket->data;
	struct bgpevpn *vpn = (struct bgpevpn *)args;

	bgp_evpn_remote_ip_process_nexthops(vpn, &ip->addr, false);
}

/*
 * vni_svi_hash_key_make -- 基于 SVI ifindex 的 VNI 哈希表键生成函数。
 * 做什么：对 vpn->svi_ifindex 做 jhash_1word。
 * 参数：p 指向 struct bgpevpn。
 * 返回：哈希值。
 * 坑：该表以 SVI ifindex 为键，一个 SVI 对应一个 VNI。
 */
static unsigned int vni_svi_hash_key_make(const void *p)
{
	const struct bgpevpn *vpn = p;

	return jhash_1word(vpn->svi_ifindex, 0);
}

/*
 * vni_svi_hash_cmp -- 基于 SVI ifindex 的 VNI 哈希表键比较函数。
 * 做什么：比较两个 bgpevpn 的 svi_ifindex 是否相等。
 * 参数：p1 / p2 待比较的两个 bgpevpn。
 * 返回：svi_ifindex 相同返回 true，否则 false。
 * 坑：仅比较 svi_ifindex，与键生成函数对应。
 */
static bool vni_svi_hash_cmp(const void *p1, const void *p2)
{
	const struct bgpevpn *vpn1 = p1;
	const struct bgpevpn *vpn2 = p2;

	return (vpn1->svi_ifindex == vpn2->svi_ifindex);
}

/*
 * bgp_evpn_vni_svi_hash_lookup -- 按 SVI ifindex 查找 VNI。
 * 做什么：构造只填 svi_ifindex 的临时 bgpevpn 作键，在 bgp->vni_svi_hash 中查找。
 * 参数：bgp 实例；svi SVI 接口索引。
 * 返回：找到返回 bgpevpn 指针，否则 NULL。
 * 坑：临时结构只需填 svi_ifindex，与比较函数一致。
 */
static struct bgpevpn *bgp_evpn_vni_svi_hash_lookup(struct bgp *bgp,
						    ifindex_t svi)
{
	struct bgpevpn *vpn;
	struct bgpevpn tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.svi_ifindex = svi;
	vpn = hash_lookup(bgp->vni_svi_hash, &tmp);
	return vpn;
}

/*
 * bgp_evpn_link_to_vni_svi_hash -- 把 VNI 加入基于 SVI ifindex 的哈希表。
 * 做什么：若 vpn->svi_ifindex 为 0 则返回；否则 hash_get 把 vpn 挂入 bgp->vni_svi_hash。
 * 参数：bgp 实例；vpn 目标 VNI。
 * 返回：无。
 * 坑：svi_ifindex 为 0 表示无 SVI，不入表。
 */
static void bgp_evpn_link_to_vni_svi_hash(struct bgp *bgp, struct bgpevpn *vpn)
{
	if (vpn->svi_ifindex == 0)
		return;

	(void)hash_get(bgp->vni_svi_hash, vpn, hash_alloc_intern);
}

/*
 * bgp_evpn_unlink_from_vni_svi_hash -- 把 VNI 从基于 SVI ifindex 的哈希表移除。
 * 做什么：若 vpn->svi_ifindex 为 0 则返回；否则 hash_release 从 bgp->vni_svi_hash 移除。
 * 参数：bgp 实例；vpn 目标 VNI。
 * 返回：无。
 * 坑：与 link 对称，SVI 变化时先 unlink 再改 ifindex 再 link。
 */
static void bgp_evpn_unlink_from_vni_svi_hash(struct bgp *bgp,
					      struct bgpevpn *vpn)
{
	if (vpn->svi_ifindex == 0)
		return;

	hash_release(bgp->vni_svi_hash, vpn);
}

/*
 * bgp_evpn_show_vni_svi_hash -- vty 输出回调：打印 SVI ifindex 与对应 VNI 号。
 * 做什么：以 bucket 数据为 VNI、args 为 vty，输出一行 "SVI: %u VNI: %u"。
 * 参数：bucket VNI 哈希桶；args 为 struct vty *。
 * 返回：无。
 * 坑：这是 vni_svi_hash 遍历的回调，展示 SVI 到 VNI 的映射关系。
 */
void bgp_evpn_show_vni_svi_hash(struct hash_bucket *bucket, void *args)
{
	struct bgpevpn *evpn = (struct bgpevpn *)bucket->data;
	struct vty *vty = (struct vty *)args;

	vty_out(vty, "SVI: %u VNI: %u\n", evpn->svi_ifindex, evpn->vni);
}

/*
 * bgp_evpn_is_gateway_ip_resolved -- 判断网关 IP 型 overlay 下一跳是否已解析。
 *
 * 做什么：针对下一跳为"网关 IP overlay index"的 bgp_nexthop_cache 条目，检查该网关
 *         IP 是否存在于其 SVI 对应 EVI 的远端 IP 哈希表中(即是否收到该网关 IP 的
 *         远端 MAC / IP 路由)。依次校验：overlay index 解析功能开启、bnc 下一跳与
 *         ifindex、EVPN 实例存在、按 ifindex 找到 VNI、vpn->bgp_vrf 与 bnc->bgp 一致、
 *         由 bnc->prefix 构造地址后在 remote_ip_hash 中命中。
 * 参数：bnc 下一跳缓存条目。
 * 返回：已解析返回 true，否则 false。
 * 坑：任一前提不满足即返回 false，只有确实收到对应远端 MAC / IP 才算解析成功。
 */
/*
 * This function is called for a bgp_nexthop_cache entry when the nexthop is
 * gateway IP overlay index.
 * This function returns true if there is a remote MAC/IP route for the gateway
 * IP in the EVI of the nexthop SVI.
 */
bool bgp_evpn_is_gateway_ip_resolved(struct bgp_nexthop_cache *bnc)
{
	struct bgp *bgp_evpn = NULL;
	struct bgpevpn *vpn = NULL;
	struct evpn_remote_ip tmp;
	struct prefix *p;

	if (!evpn_resolve_overlay_index())
		return false;

	if (!bnc->nexthop || bnc->nexthop->ifindex == 0)
		return false;

	bgp_evpn = bgp_get_evpn();
	if (!bgp_evpn)
		return false;

	/*
	 * Gateway IP is resolved by nht over SVI interface.
	 * Use this SVI to find corresponding EVI(L2 context)
	 */
	vpn = bgp_evpn_vni_svi_hash_lookup(bgp_evpn, bnc->nexthop->ifindex);
	if (!vpn)
		return false;

	if (vpn->bgp_vrf != bnc->bgp)
		return false;

	/*
	 * Check if the gateway IP is present in the EVI remote_ip_hash table
	 * which stores all the remote IP addresses received via MAC/IP routes
	 * in this EVI
	 */
	memset(&tmp, 0, sizeof(tmp));

	p = &bnc->prefix;
	if (p->family == AF_INET) {
		tmp.addr.ipa_type = IPADDR_V4;
		memcpy(&(tmp.addr.ipaddr_v4), &(p->u.prefix4),
		       sizeof(struct in_addr));
	} else if (p->family == AF_INET6) {
		tmp.addr.ipa_type = IPADDR_V6;
		memcpy(&(tmp.addr.ipaddr_v6), &(p->u.prefix6),
		       sizeof(struct in6_addr));
	} else
		return false;

	if (hash_lookup(vpn->remote_ip_hash, &tmp) == NULL)
		return false;

	return true;
}

/*
 * bgp_evpn_remote_ip_process_nexthops -- MAC / IP 路由增删时解析 / 反解析相应下一跳。
 *
 * 做什么：若 VNI 无关联 VRF 或无 SVI 则返回；按 addr 的地址族构造 /32 或 /128 前缀；
 *         在该 VRF 的 nexthop_cache_table[afi] 中查找 bnc；须为 EVPN 网关 IP 下一跳且
 *         ifindex 与 SVI 匹配，否则返回。resolve 为真且 INCOMPLETE 时置 VALID、标记
 *         MACIP_CHANGED 并 evaluate_paths；为假且 VALID 时置 INCOMPLETE、标记并
 *         evaluate_paths。
 * 参数：vpn 目标 VNI；addr 网关 IP；resolve 解析(true)或反解析(false)。
 * 返回：无。
 * 坑：只有 evpn_gwip_nexthop 且 SVI 一致的条目才受本函数影响。
 */
/* Resolve/Unresolve nexthops when a MAC/IP route is added/deleted */
static void bgp_evpn_remote_ip_process_nexthops(struct bgpevpn *vpn,
						struct ipaddr *addr,
						bool resolve)
{
	afi_t afi;
	struct prefix p;
	struct bgp_nexthop_cache *bnc;
	struct bgp_nexthop_cache_head *tree = NULL;

	if (!vpn->bgp_vrf || vpn->svi_ifindex == 0)
		return;

	memset(&p, 0, sizeof(p));

	if (addr->ipa_type == IPADDR_V4) {
		afi = AFI_IP;
		p.family = AF_INET;
		memcpy(&(p.u.prefix4), &(addr->ipaddr_v4),
		       sizeof(struct in_addr));
		p.prefixlen = IPV4_MAX_BITLEN;
	} else if (addr->ipa_type == IPADDR_V6) {
		afi = AFI_IP6;
		p.family = AF_INET6;
		memcpy(&(p.u.prefix6), &(addr->ipaddr_v6),
		       sizeof(struct in6_addr));
		p.prefixlen = IPV6_MAX_BITLEN;
	} else
		return;

	tree = &vpn->bgp_vrf->nexthop_cache_table[afi];
	bnc = bnc_find(tree, &p, 0, 0);

	if (!bnc || !bnc->is_evpn_gwip_nexthop)
		return;

	if (!bnc->nexthop || bnc->nexthop->ifindex != vpn->svi_ifindex)
		return;

	if (BGP_DEBUG(nht, NHT))
		zlog_debug("%s(%u): vni %u mac/ip %s for NH %pFX",
			   vpn->bgp_vrf->name_pretty, vpn->tenant_vrf_id,
			   vpn->vni, (resolve ? "add" : "delete"),
			   &bnc->prefix);

	/*
	 * MAC/IP route or SVI or tenant vrf being added to EVI.
	 * Set nexthop as valid only if it is already L3 reachable
	 */
	if (resolve && CHECK_FLAG(bnc->flags, BGP_NEXTHOP_EVPN_INCOMPLETE)) {
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_EVPN_INCOMPLETE);
		SET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
		SET_FLAG(bnc->change_flags, BGP_NEXTHOP_MACIP_CHANGED);
		evaluate_paths(bnc);
	}

	 /* MAC/IP route or SVI or tenant vrf being deleted from EVI */
	if (!resolve && CHECK_FLAG(bnc->flags, BGP_NEXTHOP_VALID)) {
		UNSET_FLAG(bnc->flags, BGP_NEXTHOP_VALID);
		SET_FLAG(bnc->flags, BGP_NEXTHOP_EVPN_INCOMPLETE);
		SET_FLAG(bnc->change_flags, BGP_NEXTHOP_MACIP_CHANGED);
		evaluate_paths(bnc);
	}
}

/*
 * bgp_evpn_handle_resolve_overlay_index_set -- hash 遍历回调：开启 overlay index 解析时建索引。
 * 做什么：为 bucket 中的 VNI 初始化远端 IP 哈希表；遍历其 ip_table 的每个节点与每条
 *         路径 pi，调用 bgp_evpn_remote_ip_hash_add 把远端 MAC / IP 路由登记进去。
 * 参数：bucket VNI 哈希桶；arg 未使用。
 * 返回：无。
 * 坑：功能开关从关到开时调用，需把已有路由全部补入索引。
 */
void bgp_evpn_handle_resolve_overlay_index_set(struct hash_bucket *bucket,
					       void *arg)
{
	struct bgpevpn *vpn = (struct bgpevpn *)bucket->data;
	struct bgp_dest *dest;
	struct bgp_path_info *pi;

	bgp_evpn_remote_ip_hash_init(vpn);

	for (dest = bgp_table_top(vpn->ip_table); dest;
	     dest = bgp_route_next(dest))
		for (pi = bgp_dest_get_bgp_path_info(dest); pi; pi = pi->next)
			bgp_evpn_remote_ip_hash_add(vpn, pi);
}

/*
 * bgp_evpn_handle_resolve_overlay_index_unset -- hash 遍历回调：关闭 overlay index 解析时销毁索引。
 * 做什么：对 bucket 中的 VNI 调用 bgp_evpn_remote_ip_hash_destroy 销毁其远端 IP 哈希表。
 * 参数：bucket VNI 哈希桶；arg 未使用。
 * 返回：无。
 * 坑：与 set 回调成对，功能从开到关时清理索引避免残留。
 */
void bgp_evpn_handle_resolve_overlay_index_unset(struct hash_bucket *bucket,
						 void *arg)
{
	struct bgpevpn *vpn = (struct bgpevpn *)bucket->data;

	bgp_evpn_remote_ip_hash_destroy(vpn);
}

/*
 * bgp_evpn_path_info_labels_get_l3vni -- 取路径标签栈中承载 L3VNI 的那个标签。
 *
 * 做什么：labels 为空或 num_labels 为 0 返回 NULL；否则返回最后一个标签
 *         (&labels[num_labels - 1])。
 * 参数：labels 标签数组；num_labels 标签个数。
 * 返回：指向 L3VNI 标签的指针，或 NULL。
 * 坑：L3VNI 恒为最后一个标签，Type-5 只有 1 个标签、Type-2 有 2 个。
 */
/*
 * Helper function for getting the correct label index for l3vni.
 *
 * Returns the label with the l3vni of the path's label stack.
 *
 * L3vni is always last label. Type5 will only
 * have one label, Type2 will have two.
 *
 */
mpls_label_t *bgp_evpn_path_info_labels_get_l3vni(mpls_label_t *labels,
						  uint8_t num_labels)
{
	if (!labels)
		return NULL;

	if (!num_labels)
		return NULL;

	return &labels[num_labels - 1];
}

/*
 * bgp_evpn_path_info_get_l3vni -- 从路径的标签栈解析出其 L3VNI。
 *
 * 做什么：路径无标签则返回 0；否则取最后一个标签(即 L3VNI)经 label2vni 转换后返回。
 * 参数：pi 路径信息。
 * 返回：L3VNI；无标签返回 0。
 * 坑：依赖"最后一个标签即 L3VNI"的约定。
 */
/*
 * Returns the l3vni of the path converted from the label stack.
 */
vni_t bgp_evpn_path_info_get_l3vni(const struct bgp_path_info *pi)
{
	if (!BGP_PATH_INFO_NUM_LABELS(pi))
		return 0;

	return label2vni(
		bgp_evpn_path_info_labels_get_l3vni(pi->extra->labels->label,
						    pi->extra->labels
							    ->num_labels));
}

/*
 * bgp_evpn_path_is_dvni -- 判断该路径的 L3VNI 是否与本 VRF 的 L3VNI 不一致(即为 DVNI)。
 *
 * 做什么：取路径的 L3VNI；若其大于 0 且不等于 bgp_vrf->l3vni 则返回 true，否则 false。
 * 参数：bgp_vrf VRF 实例；pi 路径信息。
 * 返回：L3VNI 与 VRF 不符返回 true，否则 false。
 * 坑：L3VNI 为 0(无标签)时不视为 DVNI。
 */
/*
 * Returns true if the l3vni of any of this path doesn't match vrf's l3vni.
 */
static bool bgp_evpn_path_is_dvni(const struct bgp *bgp_vrf,
				  const struct bgp_path_info *pi)
{
	vni_t vni = 0;

	vni = bgp_evpn_path_info_get_l3vni(pi);

	if ((vni > 0) && (vni != bgp_vrf->l3vni))
		return true;

	return false;
}

/*
 * bgp_evpn_mpath_has_dvni -- 判断多路径(mpath)链中是否存在 DVNI 成员。
 *
 * 做什么：沿 mpath 链遍历，只要有一条路径为 DVNI(bgp_evpn_path_is_dvni)即返回 true。
 * 参数：bgp_vrf VRF 实例；mpinfo 多路径链首。
 * 返回：存在 DVNI 返回 true，否则 false。
 * 坑：只要有一路 L3VNI 不匹配就算命中，用于避免把含 DVNI 的多路径当普通多路径处理。
 */
/*
 * Returns true if the l3vni of any of the mpath's doesn't match vrf's l3vni.
 */
bool bgp_evpn_mpath_has_dvni(const struct bgp *bgp_vrf,
			     struct bgp_path_info *mpinfo)
{
	for (; mpinfo; mpinfo = bgp_path_info_mpath_next(mpinfo)) {
		if (bgp_evpn_path_is_dvni(bgp_vrf, mpinfo))
			return true;
	}

	return false;
}


/*
 * bgp_evpn_fill_rmac_nh_to_attr -- 用 VRF 的 L3VNI VTEP-IP 填充 attr 的 RMAC 与下一跳。
 *
 * 做什么：bgp_vrf 或 attr 为空则返回。PIP 未启用或非 anycast MAC 时：用 anycast rmac
 *         与 originator_ip 作下一跳(按 v4 / v6 填 mp_nexthop)，并把 vtep_ip 设为
 *         originator_ip。否则(PIP 且 anycast)：用系统 rmac(pip_rmac)，下一跳取
 *         pip_ip(v4 / v6)，并在 PIP 未配置时打调试日志；vtep_ip 设为 pip_ip。
 * 参数：bgp_vrf VRF 实例；attr 待填充属性；evp 前缀(仅日志用)；vtep_ip 输出 VTEP-IP。
 * 返回：无。
 * 坑：两条分支的 rmac 与下一跳来源不同(anycast 用 rmac/originator_ip，PIP 用 pip_rmac/pip_ip)。
 */
/*
 * From tenant vrf instance's L3VNI source VTEP_IP fill V4 or V6
 * version of attr's nexthop field from PIP.
 */
void bgp_evpn_fill_rmac_nh_to_attr(struct bgp *bgp_vrf, struct attr *attr, struct prefix_evpn *evp,
				   struct ipaddr *vtep_ip)
{
	if (!bgp_vrf || !attr)
		return;
	/* Advertise Primary IP (PIP) is enabled, send individual
	 * IP (default instance router-id) as nexthop.
	 * PIP is disabled or vrr interface is not present
	 * use anycast-IP as nexthop and anycast RMAC.
	 */
	if (!bgp_vrf->evpn_info->advertise_pip || (!bgp_vrf->evpn_info->is_anycast_mac)) {
		memcpy(&attr->rmac, &bgp_vrf->rmac, ETH_ALEN);
		if (IS_IPADDR_V4(&bgp_vrf->originator_ip)) {
			attr->nexthop = bgp_vrf->originator_ip.ipaddr_v4;
			attr->mp_nexthop_global_in = bgp_vrf->originator_ip.ipaddr_v4;
			attr->mp_nexthop_len = BGP_ATTR_NHLEN_IPV4;
		} else {
			IPV6_ADDR_COPY(&attr->mp_nexthop_global, &bgp_vrf->originator_ip.ipaddr_v6);
			attr->mp_nexthop_len = BGP_ATTR_NHLEN_IPV6_GLOBAL;
		}
		if (vtep_ip)
			*vtep_ip = bgp_vrf->originator_ip;
	} else {
		/* copy sys rmac */
		memcpy(&attr->rmac, &bgp_vrf->evpn_info->pip_rmac, ETH_ALEN);
		/* L3VNI VTEP-IP is IPv4 copy v4 PIP IP, otherwise copy
		 * v6 PIP IP for nexthop path attribute
		 */
		if (vtep_ip)
			*vtep_ip = bgp_vrf->evpn_info->pip_ip;

		if (IS_IPADDR_V4(&bgp_vrf->originator_ip)) {
			attr->mp_nexthop_len = BGP_ATTR_NHLEN_IPV4;
			if (bgp_vrf->evpn_info->pip_ip.ipaddr_v4.s_addr != INADDR_ANY) {
				attr->nexthop = bgp_vrf->evpn_info->pip_ip.ipaddr_v4;
				attr->mp_nexthop_global_in = bgp_vrf->evpn_info->pip_ip.ipaddr_v4;
			} else if (bgp_vrf->evpn_info->pip_ip.ipaddr_v4.s_addr == INADDR_ANY) {
				if (bgp_debug_zebra(NULL))
					zlog_debug("VRF %s evp %pFX advertise-pip primary ip is not configured",
						   vrf_id_to_name(bgp_vrf->vrf_id), evp);
			}
		} else if (IS_IPADDR_V6(&bgp_vrf->originator_ip)) {
			attr->mp_nexthop_len = BGP_ATTR_NHLEN_IPV6_GLOBAL;
			if (!IN6_IS_ADDR_UNSPECIFIED(&bgp_vrf->evpn_info->pip_ip.ipaddr_v6)) {
				IPV6_ADDR_COPY(&attr->mp_nexthop_global,
					       &bgp_vrf->evpn_info->pip_ip.ipaddr_v6);
				if (bgp_debug_zebra(NULL))
					zlog_debug("%s ipv6 vtep, pip %pI6 address as nexthop",
						   __func__, &bgp_vrf->evpn_info->pip_ip.ipaddr_v6);
			} else if (IN6_IS_ADDR_UNSPECIFIED(&bgp_vrf->evpn_info->pip_ip.ipaddr_v6)) {
				if (bgp_debug_zebra(NULL))
					zlog_debug("VRF %s evp %pFX advertise-pip primary ip is not configured",
						   vrf_id_to_name(bgp_vrf->vrf_id), evp);
			}
		}
	}
}

/*
 * bgp_aggr_supp_withdraw_from_evpn -- 聚合地址生效时，从 EVPN 撤销(不再导入)被抑制的明细路由。
 *
 * 做什么：无 EVPN 实例或未启用 Type-5(bestpath / multipath)发布则返回。遍历聚合表，
 *         对每个有聚合信息的聚合前缀 aggr_p，取其在全局表对应节点为 top，沿
 *         bgp_route_next_until 遍历其下所有节点；跳过前缀长度不大于聚合前缀的节点；
 *         对每条路径：跳过聚合来源(BGP_ROUTE_AGGREGATE)、跳过未被抑制
 *         (bgp_path_suppressed 为假)、跳过不可注入 EVPN 的，然后计算 addpath_id
 *         并调用 bgp_evpn_withdraw_type5_route 撤销该明细的 Type-5 路由。
 * 参数：bgp 实例；afi / safi 地址族。
 * 返回：无。
 * 坑：只针对"被聚合抑制"的明细路由撤销，且 originator 传 NULL。
 */
/* Upon aggregate set trigger unimport suppressed routes
 * from EVPN
 */
void bgp_aggr_supp_withdraw_from_evpn(struct bgp *bgp, afi_t afi, safi_t safi)
{
	struct bgp_dest *agg_dest, *dest, *top;
	const struct prefix *aggr_p;
	struct bgp_aggregate *bgp_aggregate;
	struct bgp_table *table;
	struct bgp_path_info *pi;
	uint32_t addpath_id;

	if (!bgp_get_evpn() || !(advertise_type5_routes_bestpath(bgp, afi) ||
				 advertise_type5_routes_multipath(bgp, afi)))
		return;

	/* Aggregate-address table walk. */
	table = bgp->rib[afi][safi];
	for (agg_dest = bgp_table_top(bgp->aggregate[afi][safi]); agg_dest;
	     agg_dest = bgp_route_next(agg_dest)) {
		bgp_aggregate = bgp_dest_get_bgp_aggregate_info(agg_dest);

		if (bgp_aggregate == NULL)
			continue;

		aggr_p = bgp_dest_get_prefix(agg_dest);

		/* Look all nodes below the aggregate prefix in
		 * global AFI/SAFI table (IPv4/IPv6).
		 * Trigger withdrawal (this will be Type-5 routes only)
		 * from EVPN Global table.
		 */
		top = bgp_node_get(table, aggr_p);
		for (dest = bgp_node_get(table, aggr_p); dest;
		     dest = bgp_route_next_until(dest, top)) {
			const struct prefix *dest_p = bgp_dest_get_prefix(dest);

			if (dest_p->prefixlen <= aggr_p->prefixlen)
				continue;

			for (pi = bgp_dest_get_bgp_path_info(dest); pi;
			     pi = pi->next) {
				if (pi->sub_type == BGP_ROUTE_AGGREGATE)
					continue;

				/* Only Suppressed route remove from EVPN */
				if (!bgp_path_suppressed(pi))
					continue;

				if (BGP_DEBUG(zebra, ZEBRA))
					zlog_debug("%s aggregated %pFX remove suppressed route %pFX",
						   __func__, aggr_p, dest_p);

				if (!is_route_injectable_into_evpn_non_supp(pi))
					continue;

				addpath_id = bgp_evpn_addpath_id_for_path(bgp, pi, afi);
				bgp_evpn_withdraw_type5_route(bgp, NULL, dest_p, afi, safi,
							      addpath_id);
			}
		}
	}
}

/*
 * bgp_evpn_addpath_id_for_path -- 取一条路径用于 EVPN 导出时的 add-path 发送 ID。
 *
 * 做什么：bgp 或 pi 为空、afi 非 AFI_IP / AFI_IP6、或未启用 Type-5 多路径发布时均返回 0；
 *         否则返回该路径针对 BGP_ADDPATH_ALL 策略的 addpath_tx_id。
 * 参数：bgp 实例；pi 路径信息；afi 地址族。
 * 返回：add-path 发送 ID；不适用时返回 0。
 * 坑：仅在多路径发布场景有意义，其它情况一律 0。
 */
static uint32_t bgp_evpn_addpath_id_for_path(const struct bgp *bgp, const struct bgp_path_info *pi,
					     afi_t afi)
{
	if (!bgp || !pi)
		return 0;
	if (afi != AFI_IP && afi != AFI_IP6)
		return 0;
	if (!advertise_type5_routes_multipath(bgp, afi))
		return 0;

	return pi->tx_addpath.addpath_tx_id[BGP_ADDPATH_ALL];
}
