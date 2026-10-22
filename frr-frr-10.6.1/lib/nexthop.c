// SPDX-License-Identifier: GPL-2.0-or-later
/* A generic nexthop structure
 * Copyright (C) 2013 Cumulus Networks, Inc.
 */
/*
 * 文件导读：nexthop.c —— 下一跳（nexthop）对象的实现。
 *
 * 本文件实现 nexthop.h 声明的对象：分配 / 释放、比较 / 相等判定、哈希、
 * 拷贝与复制、标签栈与 SRv6 的增删、以及字符串 / JSON / VTY 输出。
 *
 * 一、比较的“分层”设计（本文件核心）
 *   _nexthop_labels_cmp   比较 MPLS 标签栈（个数 + 逐个标签）；
 *   _nexthop_srv6_cmp     比较 SRv6 seg6local 行为 / 上下文 / SID 列表；
 *   _nexthop_gateway_cmp  比较网关地址（按类型走 IPv4 / IPv6）；
 *   _nexthop_source_cmp   比较源地址；
 *   _nexthop_cmp_no_labels 比较“除标签与 SRv6 外”的字段：vrf → type →
 *                         weight（可选）→ 网关 / 接口 / 黑洞类型 →
 *                         srte_color → 源地址 → 备份索引；
 *   nexthop_cmp_internal  把上面三者按 无标签段 → 标签 → SRv6 串起来。
 *   对外暴露 nexthop_cmp（含 weight）与 nexthop_cmp_no_weight（忽略 weight）。
 *
 * 二、相等与哈希
 *   nexthop_same 系列基于 cmp 实现；nexthop_cmp_basic 是更弱的比较，只用于
 *   识别“重复 nexthop”（某条链路上同一个出口只应出现一次）。哈希函数对
 *   struct 内 _hash_begin.._hash_end 的连续块求 jhash，见 nexthop.h。
 *
 * 三、易踩的坑
 *   - 很多 cmp 函数假定 nh1 / nh2 非空，只有少数显式判 NULL；
 *   - 比较顺序即排序顺序，nexthop_group 的规范排序依赖它，改动会破坏
 *     已排序前提；
 *   - IPv4 地址必须放在清零过的 16 字节 union 中，否则哈希会读到垃圾字节。
 */

#include <zebra.h>

#include "prefix.h"
#include "table.h"
#include "memory.h"
#include "command.h"
#include "log.h"
#include "sockunion.h"
#include "linklist.h"
#include "prefix.h"
#include "nexthop.h"
#include "mpls.h"
#include "jhash.h"
#include "printfrr.h"
#include "vrf.h"
#include "nexthop_group.h"
#include "lib/json.h"

/* 内存类型注册：分别用于 nexthop 本体、MPLS 标签栈、SRv6 子结构，
 * 便于内存统计与泄漏定位（仅本文件可见）。
 */
DEFINE_MTYPE_STATIC(LIB, NEXTHOP, "Nexthop");
DEFINE_MTYPE_STATIC(LIB, NH_LABEL, "Nexthop label");
DEFINE_MTYPE_STATIC(LIB, NH_SRV6, "Nexthop srv6");

/* 比较两个 nexthop 的 MPLS 标签栈 nh_label，返回 -1 / 0 / 1（语义同 memcmp）。
 * 规则：都无标签视为相等；有标签者大于无标签者；否则先比标签个数，再逐个
 * 字节比较标签数组。不修改入参。
 */
static int _nexthop_labels_cmp(const struct nexthop *nh1,
			       const struct nexthop *nh2)
{
	const struct mpls_label_stack *nhl1 = NULL;
	const struct mpls_label_stack *nhl2 = NULL;

	nhl1 = nh1->nh_label;
	nhl2 = nh2->nh_label;

	/* No labels is a match */
	if (!nhl1 && !nhl2)
		return 0;

	if (nhl1 && !nhl2)
		return 1;

	if (nhl2 && !nhl1)
		return -1;

	if (nhl1->num_labels > nhl2->num_labels)
		return 1;

	if (nhl1->num_labels < nhl2->num_labels)
		return -1;

	return memcmp(nhl1->label, nhl2->label,
		      (nhl1->num_labels * sizeof(mpls_label_t)));
}

/* 比较两个 nexthop 的 SRv6 信息 nh_srv6，返回 -1 / 0 / 1。
 * 依次比较：是否有 SRv6 信息 → seg6local_action → seg6local_ctx →
 * SID 列表（num_segs、逐个 SID）→ encap_behavior。都无 SRv6 视为相等。
 * 副作用：无。注意 SID 个数不同直接返回 -1（不区分大小）。
 */
static int _nexthop_srv6_cmp(const struct nexthop *nh1,
			     const struct nexthop *nh2)
{
	int ret = 0;
	int i = 0;

	if (!nh1->nh_srv6 && !nh2->nh_srv6)
		return 0;

	if (nh1->nh_srv6 && !nh2->nh_srv6)
		return 1;

	if (!nh1->nh_srv6 && nh2->nh_srv6)
		return -1;

	if (nh1->nh_srv6->seg6local_action > nh2->nh_srv6->seg6local_action)
		return 1;

	if (nh2->nh_srv6->seg6local_action < nh1->nh_srv6->seg6local_action)
		return -1;

	ret = memcmp(&nh1->nh_srv6->seg6local_ctx,
		     &nh2->nh_srv6->seg6local_ctx,
		     sizeof(struct seg6local_context));
	if (ret != 0)
		return ret;

	if (!nh1->nh_srv6->seg6_segs && !nh2->nh_srv6->seg6_segs)
		return 0;

	if (!nh1->nh_srv6->seg6_segs && nh2->nh_srv6->seg6_segs)
		return -1;

	if (nh1->nh_srv6->seg6_segs && !nh2->nh_srv6->seg6_segs)
		return 1;

	if (nh1->nh_srv6->seg6_segs->num_segs !=
	    nh2->nh_srv6->seg6_segs->num_segs)
		return -1;

	for (i = 0; i < nh1->nh_srv6->seg6_segs->num_segs; i++) {
		ret = memcmp(&nh1->nh_srv6->seg6_segs->seg[i],
			     &nh2->nh_srv6->seg6_segs->seg[i],
			     sizeof(struct in6_addr));
		if (ret != 0)
			break;
	}

	if (nh1->nh_srv6->seg6_segs->encap_behavior > nh2->nh_srv6->seg6_segs->encap_behavior)
		return 1;

	if (nh1->nh_srv6->seg6_segs->encap_behavior < nh2->nh_srv6->seg6_segs->encap_behavior)
		return -1;

	return ret;
}

/* 按 nexthop 类型比较两个网关地址：IPv4 类型走 IPV4_ADDR_CMP，IPv6 类型走
 * IPV6_ADDR_CMP，IFINDEX / BLACKHOLE 类型无地址，直接返回 0。返回 cmp 值。
 */
int nexthop_g_addr_cmp(enum nexthop_types_t type, const union g_addr *addr1,
		       const union g_addr *addr2)
{
	int ret = 0;

	switch (type) {
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV4_IFINDEX:
		ret = IPV4_ADDR_CMP(&addr1->ipv4, &addr2->ipv4);
		break;
	case NEXTHOP_TYPE_IPV6:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		ret = IPV6_ADDR_CMP(&addr1->ipv6, &addr2->ipv6);
		break;
	case NEXTHOP_TYPE_IFINDEX:
	case NEXTHOP_TYPE_BLACKHOLE:
		/* No addr here */
		break;
	}

	return ret;
}

/* 比较两个 nexthop 的网关地址（gate）。用 nh1 的类型决定地址族。 */
static int _nexthop_gateway_cmp(const struct nexthop *nh1,
				const struct nexthop *nh2)
{
	return nexthop_g_addr_cmp(nh1->type, &nh1->gate, &nh2->gate);
}

/* 比较两个 nexthop 的源地址（src）。同样用 nh1 的类型决定地址族。 */
static int _nexthop_source_cmp(const struct nexthop *nh1,
			       const struct nexthop *nh2)
{
	return nexthop_g_addr_cmp(nh1->type, &nh1->src, &nh2->src);
}

/* 比较两个 nexthop 中“除标签与 SRv6 外”的字段，返回 -1 / 0 / 1。
 * 比较顺序：vrf_id → type → weight（仅当 use_weight）→ 按类型的网关 / 接口 /
 * 黑洞类型 → srte_color → 源地址 → 是否带备份（HAS_BACKUP）→ 备份个数与索引。
 * use_ifindex 为 false 时，IPV4_IFINDEX / IPV6_IFINDEX 类型不比较 ifindex。
 * 副作用：无。这是规范排序的主比较体，改动顺序会影响所有依赖方。
 */
static int _nexthop_cmp_no_labels(const struct nexthop *next1,
				  const struct nexthop *next2, bool use_weight, bool use_ifindex)
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

	if (use_weight) {
		if (next1->weight < next2->weight)
			return -1;

		if (next1->weight > next2->weight)
			return 1;
	}

	switch (next1->type) {
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV6:
		ret = _nexthop_gateway_cmp(next1, next2);
		if (ret != 0)
			return ret;
		break;
	case NEXTHOP_TYPE_IPV4_IFINDEX:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		ret = _nexthop_gateway_cmp(next1, next2);
		if (ret != 0)
			return ret;
		if (!use_ifindex)
			break;
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

	ret = _nexthop_source_cmp(next1, next2);
	if (ret != 0)
		goto done;

	if (!CHECK_FLAG(next1->flags, NEXTHOP_FLAG_HAS_BACKUP) &&
	    !CHECK_FLAG(next2->flags, NEXTHOP_FLAG_HAS_BACKUP))
		return 0;

	if (!CHECK_FLAG(next1->flags, NEXTHOP_FLAG_HAS_BACKUP) &&
	    CHECK_FLAG(next2->flags, NEXTHOP_FLAG_HAS_BACKUP))
		return -1;

	if (CHECK_FLAG(next1->flags, NEXTHOP_FLAG_HAS_BACKUP) &&
	    !CHECK_FLAG(next2->flags, NEXTHOP_FLAG_HAS_BACKUP))
		return 1;

	if (next1->backup_num == 0 && next2->backup_num == 0)
		goto done;

	if (next1->backup_num < next2->backup_num)
		return -1;

	if (next1->backup_num > next2->backup_num)
		return 1;

	ret = memcmp(next1->backup_idx,
		     next2->backup_idx, next1->backup_num);

done:
	return ret;
}

/* 完整比较的统一入口：先比“无标签段”，再比标签栈，最后比 SRv6 信息。
 * use_weight / use_ifindex 透传给 _nexthop_cmp_no_labels。返回 cmp 值。
 */
static int nexthop_cmp_internal(const struct nexthop *next1,
				const struct nexthop *next2, bool use_weight, bool use_ifindex)
{
	int ret = 0;

	ret = _nexthop_cmp_no_labels(next1, next2, use_weight, use_ifindex);
	if (ret != 0)
		return ret;

	ret = _nexthop_labels_cmp(next1, next2);
	if (ret != 0)
		return ret;

	ret = _nexthop_srv6_cmp(next1, next2);

	return ret;
}

/* 完整比较（含 weight 与 ifindex）。返回 -1 / 0 / 1。 */
int nexthop_cmp(const struct nexthop *next1, const struct nexthop *next2)
{
	return nexthop_cmp_internal(next1, next2, true, true);
}

/* 完整比较但忽略 weight（其它字段照比）。返回 -1 / 0 / 1。 */
int nexthop_cmp_no_weight(const struct nexthop *next1,
			  const struct nexthop *next2)
{
	return nexthop_cmp_internal(next1, next2, false, true);
}

/* 受限比较：只比 vrf_id、type、网关 / 接口等基本键，用于识别重复 nexthop。
 * 返回 -1 / 0 / 1。与 nexthop_cmp 不同，它忽略 weight、源地址、SRv6、
 * srte_color、备份等，且显式支持 nh1 / nh2 为 NULL 的情形。
 */
/*
 * More-limited comparison function used to detect duplicate
 * nexthops. This is used in places where we don't need the full
 * comparison of 'nexthop_cmp()'.
 */
int nexthop_cmp_basic(const struct nexthop *nh1,
		      const struct nexthop *nh2)
{
	int ret = 0;
	const struct mpls_label_stack *nhl1 = NULL;
	const struct mpls_label_stack *nhl2 = NULL;

	if (nh1 == NULL && nh2 == NULL)
		return 0;

	if (nh1 && !nh2)
		return 1;

	if (!nh1 && nh2)
		return -1;

	if (nh1->vrf_id < nh2->vrf_id)
		return -1;

	if (nh1->vrf_id > nh2->vrf_id)
		return 1;

	if (nh1->type < nh2->type)
		return -1;

	if (nh1->type > nh2->type)
		return 1;

	if (nh1->weight < nh2->weight)
		return -1;

	if (nh1->weight > nh2->weight)
		return 1;

	switch (nh1->type) {
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV6:
		ret = nexthop_g_addr_cmp(nh1->type, &nh1->gate, &nh2->gate);
		if (ret != 0)
			return ret;
		break;
	case NEXTHOP_TYPE_IPV4_IFINDEX:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		ret = nexthop_g_addr_cmp(nh1->type, &nh1->gate, &nh2->gate);
		if (ret != 0)
			return ret;
		fallthrough;
	case NEXTHOP_TYPE_IFINDEX:
		if (nh1->ifindex < nh2->ifindex)
			return -1;

		if (nh1->ifindex > nh2->ifindex)
			return 1;
		break;
	case NEXTHOP_TYPE_BLACKHOLE:
		if (nh1->bh_type < nh2->bh_type)
			return -1;

		if (nh1->bh_type > nh2->bh_type)
			return 1;
		break;
	}

	/* Compare source addr */
	ret = nexthop_g_addr_cmp(nh1->type, &nh1->src, &nh2->src);
	if (ret != 0)
		goto done;

	nhl1 = nh1->nh_label;
	nhl2 = nh2->nh_label;

	/* No labels is a match */
	if (!nhl1 && !nhl2)
		return 0;

	if (nhl1 && !nhl2)
		return 1;

	if (nhl2 && !nhl1)
		return -1;

	if (nhl1->num_labels > nhl2->num_labels)
		return 1;

	if (nhl1->num_labels < nhl2->num_labels)
		return -1;

	ret = memcmp(nhl1->label, nhl2->label,
		     (nhl1->num_labels * sizeof(mpls_label_t)));

done:
	return ret;
}

/* 把 nexthop 类型枚举转成人类可读字符串（查表 desc）。调用者需保证 nh_type
 * 落在合法范围内，否则会数组越界。
 */
/*
 * nexthop_type_to_str
 */
const char *nexthop_type_to_str(enum nexthop_types_t nh_type)
{
	static const char *const desc[] = {
		"none",		 "Directly connected",
		"IPv4 nexthop",  "IPv4 nexthop with ifindex",
		"IPv6 nexthop",  "IPv6 nexthop with ifindex",
		"Null0 nexthop",
	};

	return desc[nh_type];
}

/* 判断两个 nexthop 的标签栈是否一致，一致返回 true。内部复用
 * _nexthop_labels_cmp。
 */
/*
 * Check if the labels match for the 2 nexthops specified.
 */
bool nexthop_labels_match(const struct nexthop *nh1, const struct nexthop *nh2)
{
	if (_nexthop_labels_cmp(nh1, nh2) != 0)
		return false;

	return true;
}

/* 分配并零初始化一个 nexthop（XCALLOC，失败即断言 / 终止）。特别注意：
 * weight 默认置 1，因为 Linux 内核在 netlink 上会给权重加 1，这里统一以 1
 * 为基准，避免在 netlink 代码里特殊处理默认权重。
 */
struct nexthop *nexthop_new(void)
{
	struct nexthop *nh;

	nh = XCALLOC(MTYPE_NEXTHOP, sizeof(struct nexthop));

	/*
	 * Default the weight to 1 here for all nexthops.
	 * The linux kernel does some weird stuff with adding +1 to
	 * all nexthop weights it gets over netlink.
	 * To handle this, just default everything to 1 right from
	 * the beginning so we don't have to special case
	 * default weights in the linux netlink code.
	 *
	 * 1 should be a valid on all platforms anyway.
	 */
	nh->weight = 1;

	return nh;
}

/* 释放单个 nexthop：先释放其标签栈、SRv6 seg6local 与 SID 列表，再递归释放
 * 其 resolved 递归链，最后释放结构体本身。调用后指针失效。
 */
/* Free nexthop. */
void nexthop_free(struct nexthop *nexthop)
{
	nexthop_del_labels(nexthop);
	nexthop_del_srv6_seg6local(nexthop);
	nexthop_del_srv6_seg6(nexthop);
	if (nexthop->resolved)
		nexthops_free(nexthop->resolved);
	XFREE(MTYPE_NEXTHOP, nexthop);
}

/* 释放整条 nexthop 链表：先保存 next 再释放当前节点，避免释放后解引用。 */
/* Frees a list of nexthops */
void nexthops_free(struct nexthop *nexthop)
{
	struct nexthop *nh, *next;

	for (nh = nexthop; nh; nh = next) {
		next = nh->next;
		nexthop_free(nh);
	}
}

/* 忽略 ifindex 的相等判定。NULL 规则：一方为空返回 false；同一指针视为
 * 相等；否则走 nexthop_cmp_internal（use_ifindex=false）。
 */
bool nexthop_same_no_ifindex(const struct nexthop *nh1, const struct nexthop *nh2)
{
	if (nh1 && !nh2)
		return false;

	if (!nh1 && nh2)
		return false;

	if (nh1 == nh2)
		return true;

	return nexthop_cmp_internal(nh1, nh2, true, false);
}

/* 完整相等判定（含 weight、ifindex、标签、SRv6）。NULL 规则同上；
 * 使用完整 nexthop_cmp。
 */
bool nexthop_same(const struct nexthop *nh1, const struct nexthop *nh2)
{
	if (nh1 && !nh2)
		return false;

	if (!nh1 && nh2)
		return false;

	if (nh1 == nh2)
		return true;

	if (nexthop_cmp(nh1, nh2) != 0)
		return false;

	return true;
}

/* 忽略标签栈的相等判定：只比“无标签段”（weight 与 ifindex 均参与）。 */
bool nexthop_same_no_labels(const struct nexthop *nh1,
			    const struct nexthop *nh2)
{
	if (nh1 && !nh2)
		return false;

	if (!nh1 && nh2)
		return false;

	if (nh1 == nh2)
		return true;

	if (_nexthop_cmp_no_labels(nh1, nh2, true, true) != 0)
		return false;

	return true;
}

/* 忽略 weight 的相等判定（其余字段照比）。NULL 规则与上面一致。 */
bool nexthop_same_no_weight(const struct nexthop *nh1, const struct nexthop *nh2)
{
	if (nh1 == nh2)
		return true;

	if (!nh1 || !nh2)
		return false;

	return (nexthop_cmp_no_weight(nh1, nh2) == 0);
}

/* 构造直连型 nexthop（NEXTHOP_TYPE_IFINDEX）：仅设置 ifindex 与 vrf_id。
 * 以下 nexthop_from_* 系列都基于 nexthop_new()，失败由 XCALLOC 保证不返回。
 */
/*
 * Allocate a new nexthop object and initialize it from various args.
 */
struct nexthop *nexthop_from_ifindex(ifindex_t ifindex, vrf_id_t vrf_id)
{
	struct nexthop *nexthop;

	nexthop = nexthop_new();
	nexthop->type = NEXTHOP_TYPE_IFINDEX;
	nexthop->ifindex = ifindex;
	nexthop->vrf_id = vrf_id;

	return nexthop;
}

/* 构造 IPv4 网关型 nexthop（NEXTHOP_TYPE_IPV4）：设置网关 ipv4、vrf_id；
 * src 非空时一并设置源地址。注意 gate union 已整体清零，IPv4 只填低 4 字节。
 */
struct nexthop *nexthop_from_ipv4(const struct in_addr *ipv4,
				  const struct in_addr *src,
				  vrf_id_t vrf_id)
{
	struct nexthop *nexthop;

	nexthop = nexthop_new();
	nexthop->type = NEXTHOP_TYPE_IPV4;
	nexthop->vrf_id = vrf_id;
	nexthop->gate.ipv4 = *ipv4;
	if (src)
		nexthop->src.ipv4 = *src;

	return nexthop;
}

/* 构造 IPv4 网关 + 出接口型 nexthop（NEXTHOP_TYPE_IPV4_IFINDEX）：在
 * nexthop_from_ipv4 基础上额外设置 ifindex。
 */
struct nexthop *nexthop_from_ipv4_ifindex(const struct in_addr *ipv4,
					  const struct in_addr *src,
					  ifindex_t ifindex, vrf_id_t vrf_id)
{
	struct nexthop *nexthop;

	nexthop = nexthop_new();
	nexthop->type = NEXTHOP_TYPE_IPV4_IFINDEX;
	nexthop->vrf_id = vrf_id;
	nexthop->gate.ipv4 = *ipv4;
	if (src)
		nexthop->src.ipv4 = *src;
	nexthop->ifindex = ifindex;

	return nexthop;
}

/* 构造 IPv6 网关型 nexthop（NEXTHOP_TYPE_IPV6）：设置网关 ipv6 与 vrf_id，
 * 出接口留待递归解析。
 */
struct nexthop *nexthop_from_ipv6(const struct in6_addr *ipv6,
				  vrf_id_t vrf_id)
{
	struct nexthop *nexthop;

	nexthop = nexthop_new();
	nexthop->vrf_id = vrf_id;
	nexthop->type = NEXTHOP_TYPE_IPV6;
	nexthop->gate.ipv6 = *ipv6;

	return nexthop;
}

/* 构造 IPv6 网关 + 出接口型 nexthop（NEXTHOP_TYPE_IPV6_IFINDEX）。 */
struct nexthop *nexthop_from_ipv6_ifindex(const struct in6_addr *ipv6,
					  ifindex_t ifindex, vrf_id_t vrf_id)
{
	struct nexthop *nexthop;

	nexthop = nexthop_new();
	nexthop->vrf_id = vrf_id;
	nexthop->type = NEXTHOP_TYPE_IPV6_IFINDEX;
	nexthop->gate.ipv6 = *ipv6;
	nexthop->ifindex = ifindex;

	return nexthop;
}

/* 构造黑洞型 nexthop（NEXTHOP_TYPE_BLACKHOLE）：设置 bh_type 与 vrf_id。
 * bh_type 与 gate 共用同一段 union 内存。
 */
struct nexthop *nexthop_from_blackhole(enum blackhole_type bh_type,
				       vrf_id_t nh_vrf_id)
{
	struct nexthop *nexthop;

	nexthop = nexthop_new();
	nexthop->vrf_id = nh_vrf_id;
	nexthop->type = NEXTHOP_TYPE_BLACKHOLE;
	nexthop->bh_type = bh_type;

	return nexthop;
}

/* 给 nexthop 设置标签栈。num_labels 为 0 时直接返回；会先释放已有标签避免
 * 泄漏；标签数超过 MPLS_MAX_LABELS 会被截断；同时记录标签类型 ltype。
 * 分配的内存由 nexthop_del_labels / nexthop_free 负责回收。
 */
/* Update nexthop with label information. */
void nexthop_add_labels(struct nexthop *nexthop, enum lsp_types_t ltype,
			uint8_t num_labels, const mpls_label_t *labels)
{
	struct mpls_label_stack *nh_label;
	int i;

	if (num_labels == 0)
		return;

	/* Free existing labels if present to prevent memory leak */
	if (nexthop->nh_label != NULL) {
		XFREE(MTYPE_NH_LABEL, nexthop->nh_label);
		nexthop->nh_label = NULL;
	}

	/* Enforce limit on label stack size */
	if (num_labels > MPLS_MAX_LABELS)
		num_labels = MPLS_MAX_LABELS;

	nexthop->nh_label_type = ltype;

	nh_label = XCALLOC(MTYPE_NH_LABEL,
			   sizeof(struct mpls_label_stack)
				   + num_labels * sizeof(mpls_label_t));
	nh_label->num_labels = num_labels;
	for (i = 0; i < num_labels; i++)
		nh_label->label[i] = *(labels + i);
	nexthop->nh_label = nh_label;
}

/* 释放 nexthop 的标签栈并把标签类型复位为 ZEBRA_LSP_NONE。可安全重复调用
 * （XFREE 对 NULL 安全）。
 */
/* Free label information of nexthop, if present. */
void nexthop_del_labels(struct nexthop *nexthop)
{
	XFREE(MTYPE_NH_LABEL, nexthop->nh_label);
	nexthop->nh_label_type = ZEBRA_LSP_NONE;
}

/* 用 new_stack 替换 nexthop 的标签栈：标签数超限会截断；仅当新栈更长时才
 * 重新分配并扩容（XREALLOC），否则复用原缓冲区。最后把标签逐个拷入。
 * 若 nexthop 当前没有标签栈（nh_label 为 NULL）会直接解引用崩溃，属调用者
 * 需保证的前提。
 */
void nexthop_change_labels(struct nexthop *nexthop, struct mpls_label_stack *new_stack)
{
	struct mpls_label_stack *nh_label_tmp;
	uint32_t i;

	/* Enforce limit on label stack size */
	if (new_stack->num_labels > MPLS_MAX_LABELS)
		new_stack->num_labels = MPLS_MAX_LABELS;

	/* Resize the array to accommodate the new label stack */
	if (new_stack->num_labels > nexthop->nh_label->num_labels) {
		nh_label_tmp = XREALLOC(MTYPE_NH_LABEL, nexthop->nh_label,
					sizeof(struct mpls_label_stack) +
						new_stack->num_labels * sizeof(mpls_label_t));
		if (nh_label_tmp) {
			nexthop->nh_label = nh_label_tmp;
			nexthop->nh_label->num_labels = new_stack->num_labels;
		} else
			new_stack->num_labels = nexthop->nh_label->num_labels;
	}

	/* Copy the label stack into the array */
	for (i = 0; i < new_stack->num_labels; i++)
		nexthop->nh_label->label[i] = new_stack->label[i];
}

/* 设置 SRv6 seg6local 端点行为：action 为动作码（UNSPEC 时直接返回），
 * ctx 为其上下文（整块拷贝）。必要时先分配 nh_srv6。
 */
void nexthop_add_srv6_seg6local(struct nexthop *nexthop, uint32_t action,
				const struct seg6local_context *ctx)
{
	if (action == ZEBRA_SEG6_LOCAL_ACTION_UNSPEC)
		return;

	if (!nexthop->nh_srv6)
		nexthop->nh_srv6 = XCALLOC(MTYPE_NH_SRV6,
					   sizeof(struct nexthop_srv6));

	nexthop->nh_srv6->seg6local_action = action;
	nexthop->nh_srv6->seg6local_ctx = *ctx;
}

/* 清除 seg6local 行为：把 action 复位为 UNSPEC；若无 SID 列表或 SID 全零，
 * 一并释放 seg6_segs；若 seg6_segs 已为空则释放整个 nh_srv6。无副作用时的
 * 前提：nh_srv6 为空则直接返回。
 */
void nexthop_del_srv6_seg6local(struct nexthop *nexthop)
{
	if (!nexthop->nh_srv6)
		return;

	if (nexthop->nh_srv6->seg6local_action == ZEBRA_SEG6_LOCAL_ACTION_UNSPEC)
		return;

	nexthop->nh_srv6->seg6local_action = ZEBRA_SEG6_LOCAL_ACTION_UNSPEC;

	if (nexthop->nh_srv6->seg6_segs &&
	    (nexthop->nh_srv6->seg6_segs->num_segs == 0 ||
	     sid_zero(nexthop->nh_srv6->seg6_segs)))
		XFREE(MTYPE_NH_SRV6, nexthop->nh_srv6->seg6_segs);

	if (nexthop->nh_srv6->seg6_segs == NULL)
		XFREE(MTYPE_NH_SRV6, nexthop->nh_srv6);
}

/* 设置 SRv6 封装的 SID 列表：segs 为 SID 数组（为空直接返回），num_segs 个，
 * 超过 SRV6_MAX_SIDS 会截断；记录 encap_behavior。必要时分配 nh_srv6 与
 * seg6_segs。注意：已存在 seg6_segs 时复用其缓冲，可能容量不足——需靠调用
 * 者保证一致的使用方式。
 */
void nexthop_add_srv6_seg6(struct nexthop *nexthop, const struct in6_addr *segs, int num_segs,
			   enum srv6_headend_behavior encap_behavior)
{
	int i;

	if (!segs)
		return;

	if (!nexthop->nh_srv6)
		nexthop->nh_srv6 = XCALLOC(MTYPE_NH_SRV6,
					   sizeof(struct nexthop_srv6));

	/* Enforce limit on srv6 seg stack size */
	if (num_segs > SRV6_MAX_SIDS)
		num_segs = SRV6_MAX_SIDS;

	if (!nexthop->nh_srv6->seg6_segs) {
		nexthop->nh_srv6->seg6_segs =
			XCALLOC(MTYPE_NH_SRV6,
				sizeof(struct seg6_seg_stack) +
					num_segs * sizeof(struct in6_addr));
	}

	nexthop->nh_srv6->seg6_segs->num_segs = num_segs;

	for (i = 0; i < num_segs; i++)
		memcpy(&nexthop->nh_srv6->seg6_segs->seg[i], &segs[i],
		       sizeof(struct in6_addr));

	nexthop->nh_srv6->seg6_segs->encap_behavior = encap_behavior;
}

/* 清除 SRv6 的 SID 列表信息：仅在“无 seg6local 行为但存在 SID 列表”时清零
 * SID 并释放列表；随后释放 nh_srv6 本体。若存在 seg6local 行为，则只清列表、
 * 保留 nh_srv6（由 del_srv6_seg6local 负责其余清理）。
 */
void nexthop_del_srv6_seg6(struct nexthop *nexthop)
{
	if (!nexthop->nh_srv6)
		return;

	if (nexthop->nh_srv6->seg6local_action == ZEBRA_SEG6_LOCAL_ACTION_UNSPEC &&
			nexthop->nh_srv6->seg6_segs) {
		memset(nexthop->nh_srv6->seg6_segs->seg, 0,
		       sizeof(struct in6_addr) *
			       nexthop->nh_srv6->seg6_segs->num_segs);
		XFREE(MTYPE_NH_SRV6, nexthop->nh_srv6->seg6_segs);
	}
	XFREE(MTYPE_NH_SRV6, nexthop->nh_srv6);
}

/* 把 nexthop 格式化为可读字符串写入 str（容量 size），返回 str。
 * 形式随类型不同：接口型 “if N (A/I)”、地址型 “addr if N (A/I)”、
 * 黑洞型 “blackhole”。括号内 A 表示 ACTIVE，I 表示非活动。调用者需保证
 * str 足够大（建议 NEXTHOP_STRLEN）。
 */
const char *nexthop2str(const struct nexthop *nexthop, char *str, int size)
{
	switch (nexthop->type) {
	case NEXTHOP_TYPE_IFINDEX:
		snprintf(str, size, "if %u (%s)", nexthop->ifindex,
			 CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_ACTIVE) ? "A" : "I");
		break;
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV4_IFINDEX:
		snprintfrr(str, size, "%pI4 if %u (%s)", &nexthop->gate.ipv4, nexthop->ifindex,
			   CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_ACTIVE) ? "A" : "I");
		break;
	case NEXTHOP_TYPE_IPV6:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		snprintfrr(str, size, "%pI6 if %u (%s)", &nexthop->gate.ipv6, nexthop->ifindex,
			   CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_ACTIVE) ? "A" : "I");
		break;
	case NEXTHOP_TYPE_BLACKHOLE:
		snprintf(str, size, "blackhole");
		break;
	}

	return str;
}

/* ALL_NEXTHOPS 的步进函数：实现“先序遍历”式遍历，左子树为 resolved（递归
 * 解析链），右子树为 next（同级链）。若当前跳带 RECURSIVE 标志，下一步进入
 * resolved；否则沿 next 走；next 到头则沿 rparent 向上找第一个还有 next 的
 * 祖先。返回下一个 nexthop，遍历结束返回 NULL。
 */
/*
 * Iteration step for ALL_NEXTHOPS macro:
 * This is the tricky part. Check if `nexthop' has
 * NEXTHOP_FLAG_RECURSIVE set. If yes, this implies that `nexthop' has
 * at least one nexthop attached to `nexthop->resolved', which will be
 * the next one.
 *
 * If NEXTHOP_FLAG_RECURSIVE is not set, `nexthop' will progress in its
 * current chain. In case its current chain end is reached, it will move
 * upwards in the recursion levels and progress there. Whenever a step
 * forward in a chain is done, recursion will be checked again.
 * In a nustshell, it's equivalent to a pre-traversal order assuming that
 * left branch is 'resolved' and right branch is 'next':
 * https://en.wikipedia.org/wiki/Tree_traversal#/media/File:Sorted_binary_tree_preorder.svg
 */
struct nexthop *nexthop_next(const struct nexthop *nexthop)
{
	if (CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_RECURSIVE))
		return nexthop->resolved;

	if (nexthop->next)
		return nexthop->next;

	for (struct nexthop *par = nexthop->rparent; par; par = par->rparent)
		if (par->next)
			return par->next;

	return NULL;
}

/* 遍历步进的封装：nexthop_resolution 为真时等价于 nexthop_next（会下钻递归
 * 链），为假时只在当前 next 链上前进，不进入 resolved。
 */
struct nexthop *nexthop_next_resolution(const struct nexthop *nexthop,
					bool nexthop_resolution)
{
	if (nexthop_resolution)
		return nexthop_next(nexthop);
	/* no resolution attempt */
	return nexthop->next;
}

/* 返回遍历树中下一个“已解析且 active”的 nexthop：从 nexthop_next 开始，
 * 跳过仍是 RECURSIVE 或非 ACTIVE 的节点。找不到返回 NULL。
 */
/* Return the next nexthop in the tree that is resolved and active */
struct nexthop *nexthop_next_active_resolved(const struct nexthop *nexthop)
{
	struct nexthop *next = nexthop_next(nexthop);

	while (next
	       && (CHECK_FLAG(next->flags, NEXTHOP_FLAG_RECURSIVE)
		   || !CHECK_FLAG(next->flags, NEXTHOP_FLAG_ACTIVE)))
		next = nexthop_next(next);

	return next;
}

/* 计算 nexthop 的递归层级：沿 rparent 一路向上计数，顶层为 0。 */
unsigned int nexthop_level(const struct nexthop *nexthop)
{
	unsigned int rv = 0;

	for (const struct nexthop *par = nexthop->rparent;
	     par; par = par->rparent)
		rv++;

	return rv;
}

/* 计算 nexthop 的哈希值（种子常量 0x45afe398）。哈希内容依次为：
 *   1) _hash_begin.._hash_end 连续块（type / vrf_id / ifindex / 地址等）；
 *   2) flags 中参与哈希的位（NEXTHOP_FLAGS_HASHED，目前仅 ONLINK）；
 *   3) 标签栈（标签个数不同自然导致哈希不同，故不单独哈希 num_labels）；
 *   4) 备份索引（仅当 HAS_BACKUP）；
 *   5) SRv6：seg6local 动作与上下文，或 SID 列表与 encap_behavior。
 * 副作用：无。返回 32 位哈希。
 */
uint32_t nexthop_hash(const struct nexthop *nexthop)
{
	uint32_t key = 0x45afe398;

	/* type, vrf, ifindex, ip addresses - see nexthop.h */
	key = _nexthop_hash_bytes(nexthop, key);

	key = jhash_1word(nexthop->flags & NEXTHOP_FLAGS_HASHED, key);

	if (nexthop->nh_label) {
		const struct mpls_label_stack *ls = nexthop->nh_label;

		/* num_labels itself isn't useful to hash, if the number of
		 * labels is different, the hash value will change just due to
		 * that already.
		 */
		key = jhash(ls->label, sizeof(ls->label[0]) * ls->num_labels, key);
	}

	/* Include backup nexthops, if present */
	if (CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_HAS_BACKUP)) {
		int backups = nexthop->backup_num;

		key = jhash(nexthop->backup_idx, sizeof(nexthop->backup_idx[0]) * backups, key);
	}

	if (nexthop->nh_srv6) {
		int segs_num = 0;
		int i = 0;

		if (nexthop->nh_srv6->seg6local_action !=
		    ZEBRA_SEG6_LOCAL_ACTION_UNSPEC) {
			key = jhash_1word(nexthop->nh_srv6->seg6local_action,
					  key);
			key = jhash(&nexthop->nh_srv6->seg6local_ctx,
				    sizeof(nexthop->nh_srv6->seg6local_ctx),
				    key);
			if (nexthop->nh_srv6->seg6_segs)
				key = jhash(&nexthop->nh_srv6->seg6_segs->seg[0],
					    sizeof(struct in6_addr), key);
		} else {
			if (nexthop->nh_srv6->seg6_segs) {
				segs_num = nexthop->nh_srv6->seg6_segs->num_segs;
				while (segs_num >= 1) {
					key = jhash(&nexthop->nh_srv6->seg6_segs
							    ->seg[i],
						    sizeof(struct in6_addr),
						    key);
					segs_num -= 1;
					i += 1;
				}
				key = jhash_1word(nexthop->nh_srv6->seg6_segs->encap_behavior, key);
			}
		}
	}

	return key;
}

/* 把 nexthop 的非递归字段拷贝到已分配的 copy：vrf_id、ifindex、type、
 * flags、weight、备份（含 assert 校验）、srte_color、gate / src / rmap_src /
 * rmac、rparent，以及标签栈与 SRv6（通过 add 系列函数深拷贝）。
 * 不拷贝 resolved / next 链。copy 需已由 nexthop_new 分配。
 */
void nexthop_copy_no_recurse(struct nexthop *copy,
			     const struct nexthop *nexthop,
			     struct nexthop *rparent)
{
	copy->vrf_id = nexthop->vrf_id;
	copy->ifindex = nexthop->ifindex;
	copy->type = nexthop->type;
	copy->flags = nexthop->flags;
	copy->weight = nexthop->weight;

	assert(nexthop->backup_num < NEXTHOP_MAX_BACKUPS);
	copy->backup_num = nexthop->backup_num;
	if (copy->backup_num > 0)
		memcpy(copy->backup_idx, nexthop->backup_idx, copy->backup_num);

	copy->srte_color = nexthop->srte_color;
	memcpy(&copy->gate, &nexthop->gate, sizeof(nexthop->gate));
	memcpy(&copy->src, &nexthop->src, sizeof(nexthop->src));
	memcpy(&copy->rmap_src, &nexthop->rmap_src, sizeof(nexthop->rmap_src));
	memcpy(&copy->rmac, &nexthop->rmac, sizeof(nexthop->rmac));
	copy->rparent = rparent;
	if (nexthop->nh_label)
		nexthop_add_labels(copy, nexthop->nh_label_type,
				   nexthop->nh_label->num_labels,
				   &nexthop->nh_label->label[0]);

	if (nexthop->nh_srv6) {
		if (nexthop->nh_srv6->seg6local_action !=
		    ZEBRA_SEG6_LOCAL_ACTION_UNSPEC)
			nexthop_add_srv6_seg6local(copy,
				nexthop->nh_srv6->seg6local_action,
				&nexthop->nh_srv6->seg6local_ctx);
		if (nexthop->nh_srv6->seg6_segs &&
		    nexthop->nh_srv6->seg6_segs->num_segs &&
		    !sid_zero(nexthop->nh_srv6->seg6_segs))
			nexthop_add_srv6_seg6(copy, &nexthop->nh_srv6->seg6_segs->seg[0],
					      nexthop->nh_srv6->seg6_segs->num_segs,
					      nexthop->nh_srv6->seg6_segs->encap_behavior);
	}
}

/* 完整拷贝（含递归）：先 copy_no_recurse，若带 RECURSIVE 标志则用
 * copy_nexthops 递归复制 resolved 链（因为解析结果可能是一整个组）。
 */
void nexthop_copy(struct nexthop *copy, const struct nexthop *nexthop,
		  struct nexthop *rparent)
{
	nexthop_copy_no_recurse(copy, nexthop, rparent);

	/* Bit of a special case here, we need to handle the case
	 * of a nexthop resolving to a group. Hence, we need to
	 * use a nexthop_group API.
	 */
	if (CHECK_FLAG(copy->flags, NEXTHOP_FLAG_RECURSIVE))
		copy_nexthops(&copy->resolved, nexthop->resolved, copy);
}

/* 分配一个新 nexthop 并拷贝非递归字段，返回新对象。 */
struct nexthop *nexthop_dup_no_recurse(const struct nexthop *nexthop,
				       struct nexthop *rparent)
{
	struct nexthop *new = nexthop_new();

	nexthop_copy_no_recurse(new, nexthop, rparent);
	return new;
}

/* 分配一个新 nexthop 并完整拷贝（含递归），返回新对象。 */
struct nexthop *nexthop_dup(const struct nexthop *nexthop,
			    struct nexthop *rparent)
{
	struct nexthop *new = nexthop_new();

	nexthop_copy(new, nexthop, rparent);
	return new;
}

/* 解析逗号分隔的备份索引串进 backups 数组（容量 NEXTHOP_MAX_BACKUPS）。
 * *num_backups 回填成功解析的个数。成功返回 0，解析失败返回负值。
 * 错误情形：段非纯数字、空段、值大于 255、段数超过上限。内部会复制字符串
 * （strsep 具破坏性），用完即释放。
 */
/*
 * Parse one or more backup index values, as comma-separated numbers,
 * into caller's array of uint8_ts. The array must be NEXTHOP_MAX_BACKUPS
 * in size. Mails back the number of values converted, and returns 0 on
 * success, <0 if an error in parsing.
 */
int nexthop_str2backups(const char *str, int *num_backups,
			uint8_t *backups)
{
	char *ostr;			  /* copy of string (start) */
	char *lstr;			  /* working copy of string */
	char *nump;			  /* pointer to next segment */
	char *endp;			  /* end pointer */
	int i, ret;
	uint8_t tmp[NEXTHOP_MAX_BACKUPS];
	uint32_t lval;

	/* Copy incoming string; the parse is destructive */
	lstr = ostr = XSTRDUP(MTYPE_TMP, str);
	*num_backups = 0;
	ret = 0;

	for (i = 0; i < NEXTHOP_MAX_BACKUPS && lstr; i++) {
		nump = strsep(&lstr, ",");
		lval = strtoul(nump, &endp, 10);

		/* Format check */
		if (*endp != '\0') {
			ret = -1;
			break;
		}

		/* Empty value */
		if (endp == nump) {
			ret = -1;
			break;
		}

		/* Limit to one octet */
		if (lval > 255) {
			ret = -1;
			break;
		}

		tmp[i] = lval;
	}

	/* Excess values */
	if (ret == 0 && i == NEXTHOP_MAX_BACKUPS && lstr)
		ret = -1;

	if (ret == 0) {
		*num_backups = i;
		memcpy(backups, tmp, i);
	}

	XFREE(MTYPE_TMP, ostr);

	return ret;
}

/* printfrr 的 %pNHs 扩展：把 nexthop 追加写入 fbuf，返回写入长度。 */
ssize_t printfrr_nhs(struct fbuf *buf, const struct nexthop *nexthop)
{
	ssize_t ret = 0;

	if (!nexthop)
		return bputs(buf, "(null)");

	switch (nexthop->type) {
	case NEXTHOP_TYPE_IFINDEX:
		ret += bprintfrr(buf, "if %u", nexthop->ifindex);
		break;
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV4_IFINDEX:
		ret += bprintfrr(buf, "%pI4 if %u", &nexthop->gate.ipv4,
				 nexthop->ifindex);
		break;
	case NEXTHOP_TYPE_IPV6:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		ret += bprintfrr(buf, "%pI6 if %u", &nexthop->gate.ipv6,
				 nexthop->ifindex);
		break;
	case NEXTHOP_TYPE_BLACKHOLE:
		ret += bputs(buf, "blackhole");
		break;
	}

	ret += bprintfrr(buf, " vrfid %u", nexthop->vrf_id);
	return ret;
}

/* printfrr 的 %pNH 家族扩展实现。printfrr_nh 是注册的回调，按 ea->fmt 的
 * 后续字符选择打印风格：v（含 vv）、s、c 后接 g（网关）或 i（接口名）。
 * 下方英文块列出各风格对应的输出示例。返回写入长度，未知格式返回 -1。
 */
/*
 * nexthop printing variants:
 *	%pNHvv
 *		via 1.2.3.4
 *		via 1.2.3.4, eth0
 *		is directly connected, eth0
 *		unreachable (blackhole)
 *	%pNHv
 *		1.2.3.4
 *		1.2.3.4, via eth0
 *		directly connected, eth0
 *		unreachable (blackhole)
 *	%pNHs
 *		nexthop2str()
 *	%pNHcg
 *		1.2.3.4
 *		(0-length if no IP address present)
 *	%pNHci
 *		eth0
 *		(0-length if no interface present)
 */
/* 把 "NH" 扩展名与 printfrr_nh 回调绑定（构造期自动注册）。 */
printfrr_ext_autoreg_p("NH", printfrr_nh);
static ssize_t printfrr_nh(struct fbuf *buf, struct printfrr_eargs *ea,
			   const void *ptr)
{
	const struct nexthop *nexthop = ptr;
	bool do_ifi = false;
	const char *v_is = "", *v_via = "", *v_viaif = "via ";
	ssize_t ret = 0;

	switch (*ea->fmt) {
	case 'v':
		ea->fmt++;
		if (*ea->fmt == 'v') {
			v_is = "is ";
			v_via = "via ";
			v_viaif = "";
			ea->fmt++;
		}

		if (!nexthop)
			return bputs(buf, "(null)");

		switch (nexthop->type) {
		case NEXTHOP_TYPE_IPV4:
		case NEXTHOP_TYPE_IPV4_IFINDEX:
			ret += bprintfrr(buf, "%s%pI4", v_via,
					 &nexthop->gate.ipv4);
			do_ifi = true;
			break;
		case NEXTHOP_TYPE_IPV6:
		case NEXTHOP_TYPE_IPV6_IFINDEX:
			ret += bprintfrr(buf, "%s%pI6", v_via,
					 &nexthop->gate.ipv6);
			do_ifi = true;
			break;
		case NEXTHOP_TYPE_IFINDEX:
			ret += bprintfrr(buf, "%sdirectly connected, %s", v_is,
					 ifindex2ifname(nexthop->ifindex,
							nexthop->vrf_id));
			break;
		case NEXTHOP_TYPE_BLACKHOLE:
			ret += bputs(buf, "unreachable");

			switch (nexthop->bh_type) {
			case BLACKHOLE_REJECT:
				ret += bputs(buf, " (ICMP unreachable)");
				break;
			case BLACKHOLE_ADMINPROHIB:
				ret += bputs(buf, " (ICMP admin-prohibited)");
				break;
			case BLACKHOLE_NULL:
				ret += bputs(buf, " (blackhole)");
				break;
			case BLACKHOLE_UNSPEC:
				break;
			}
			break;
		}
		if (do_ifi && nexthop->ifindex)
			ret += bprintfrr(buf, ", %s%s", v_viaif,
					 ifindex2ifname(nexthop->ifindex,
							nexthop->vrf_id));

		return ret;
	case 's':
		ea->fmt++;

		ret += printfrr_nhs(buf, nexthop);
		return ret;
	case 'c':
		ea->fmt++;
		if (*ea->fmt == 'g') {
			ea->fmt++;
			if (!nexthop)
				return bputs(buf, "(null)");
			switch (nexthop->type) {
			case NEXTHOP_TYPE_IPV4:
			case NEXTHOP_TYPE_IPV4_IFINDEX:
				ret += bprintfrr(buf, "%pI4",
						 &nexthop->gate.ipv4);
				break;
			case NEXTHOP_TYPE_IPV6:
			case NEXTHOP_TYPE_IPV6_IFINDEX:
				ret += bprintfrr(buf, "%pI6",
						 &nexthop->gate.ipv6);
				break;
			case NEXTHOP_TYPE_IFINDEX:
			case NEXTHOP_TYPE_BLACKHOLE:
				break;
			}
		} else if (*ea->fmt == 'i') {
			ea->fmt++;
			if (!nexthop)
				return bputs(buf, "(null)");
			switch (nexthop->type) {
			case NEXTHOP_TYPE_IFINDEX:
				ret += bprintfrr(
					buf, "%s",
					ifindex2ifname(nexthop->ifindex,
						       nexthop->vrf_id));
				break;
			case NEXTHOP_TYPE_IPV4:
			case NEXTHOP_TYPE_IPV4_IFINDEX:
			case NEXTHOP_TYPE_IPV6:
			case NEXTHOP_TYPE_IPV6_IFINDEX:
				if (nexthop->ifindex)
					ret += bprintfrr(
						buf, "%s",
						ifindex2ifname(
							nexthop->ifindex,
							nexthop->vrf_id));
				break;
			case NEXTHOP_TYPE_BLACKHOLE:
				break;
			}
		}
		return ret;
	}
	return -1;
}

/* 判断是否为黑洞 nexthop：仅看 type 是否等于 NEXTHOP_TYPE_BLACKHOLE。 */
bool nexthop_is_blackhole(const struct nexthop *nh)
{
	return nh->type == NEXTHOP_TYPE_BLACKHOLE;
}

/* 把一个 nexthop 渲染进已存在的 json 对象 json_nexthop 中，调用者负责该
 * json 对象的分配与释放。输出内容随类型不同：地址 / 接口、flags、active /
 * onLink / linkDown / recursive、备份索引、源 / rmap 源地址、标签（EVPN 时
 * 转成 VNI）、weight、srteColor、seg6local 与 seg6 SID 等；display_vrfid
 * 为真时附带 vrf 名，rn_family 用于 IPv4 前缀 + IPv6 下一跳的特例。
 */
/*
 * Render a nexthop into a json object; the caller allocates and owns
 * the json object memory.
 */
void nexthop_json_helper(json_object *json_nexthop,
			 const struct nexthop *nexthop, bool display_vrfid,
			 uint8_t rn_family)
{
	json_object *json_labels = NULL;
	json_object *json_backups = NULL;
	json_object *json_seg6local = NULL;
	json_object *json_seg6local_context = NULL;
	json_object *json_srv6_sid_structure = NULL;
	json_object *json_seg6 = NULL;
	json_object *json_segs = NULL;
	int i;

	json_object_int_add(json_nexthop, "flags", nexthop->flags);

	if (CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_DUPLICATE))
		json_object_boolean_true_add(json_nexthop, "duplicate");

	if (CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_FIB))
		json_object_boolean_true_add(json_nexthop, "fib");

	switch (nexthop->type) {
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV4_IFINDEX:
		json_object_string_addf(json_nexthop, "ip", "%pI4",
					&nexthop->gate.ipv4);
		json_object_string_add(json_nexthop, "afi", "ipv4");

		if (nexthop->ifindex) {
			json_object_int_add(json_nexthop, "interfaceIndex",
					    nexthop->ifindex);
			json_object_string_add(json_nexthop, "interfaceName",
					       ifindex2ifname(nexthop->ifindex,
							      nexthop->vrf_id));
		}
		break;
	case NEXTHOP_TYPE_IPV6:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		json_object_string_addf(json_nexthop, "ip", "%pI6",
					&nexthop->gate.ipv6);
		json_object_string_add(json_nexthop, "afi", "ipv6");

		if (nexthop->ifindex) {
			json_object_int_add(json_nexthop, "interfaceIndex",
					    nexthop->ifindex);
			json_object_string_add(json_nexthop, "interfaceName",
					       ifindex2ifname(nexthop->ifindex,
							      nexthop->vrf_id));
		}
		break;

	case NEXTHOP_TYPE_IFINDEX:
		json_object_boolean_true_add(json_nexthop, "directlyConnected");
		json_object_int_add(json_nexthop, "interfaceIndex",
				    nexthop->ifindex);
		json_object_string_add(json_nexthop, "interfaceName",
				       ifindex2ifname(nexthop->ifindex,
						      nexthop->vrf_id));
		break;
	case NEXTHOP_TYPE_BLACKHOLE:
		json_object_boolean_true_add(json_nexthop, "unreachable");
		switch (nexthop->bh_type) {
		case BLACKHOLE_REJECT:
			json_object_boolean_true_add(json_nexthop, "reject");
			break;
		case BLACKHOLE_ADMINPROHIB:
			json_object_boolean_true_add(json_nexthop,
						     "adminProhibited");
			break;
		case BLACKHOLE_NULL:
			json_object_boolean_true_add(json_nexthop, "blackhole");
			break;
		case BLACKHOLE_UNSPEC:
			break;
		}
		break;
	}

	/* This nexthop is a resolver for the parent nexthop.
	 * Set resolver flag for better clarity and delimiter
	 * in flat list of nexthops in json.
	 */
	if (nexthop->rparent)
		json_object_boolean_true_add(json_nexthop, "resolver");

	if (display_vrfid)
		json_object_string_add(json_nexthop, "vrf",
				       vrf_id_to_name(nexthop->vrf_id));
	if (CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_DUPLICATE))
		json_object_boolean_true_add(json_nexthop, "duplicate");

	if (CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_ACTIVE))
		json_object_boolean_true_add(json_nexthop, "active");

	if (CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_ONLINK))
		json_object_boolean_true_add(json_nexthop, "onLink");

	if (CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_LINKDOWN))
		json_object_boolean_true_add(json_nexthop, "linkDown");

	if (CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_RECURSIVE))
		json_object_boolean_true_add(json_nexthop, "recursive");

	if (CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_HAS_BACKUP)) {
		json_backups = json_object_new_array();
		for (i = 0; i < nexthop->backup_num; i++) {
			json_object_array_add(json_backups,
					      json_object_new_int(
						      nexthop->backup_idx[i]));
		}

		json_object_object_add(json_nexthop, "backupIndex",
				       json_backups);
	}

	switch (nexthop->type) {
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV4_IFINDEX:
		if (nexthop->rmap_src.ipv4.s_addr)
			json_object_string_addf(json_nexthop, "rmapSource",
						"%pI4", &nexthop->rmap_src.ipv4);
		else if (nexthop->src.ipv4.s_addr)
			json_object_string_addf(json_nexthop, "source", "%pI4",
						&nexthop->src.ipv4);
		break;
	case NEXTHOP_TYPE_IPV6:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		/* Allow for 5549 ipv4 prefix with ipv6 nexthop */
		if (rn_family == AF_INET && nexthop->rmap_src.ipv4.s_addr)
			json_object_string_addf(json_nexthop, "rmapSource",
						"%pI4", &nexthop->rmap_src.ipv4);
		else if (!IPV6_ADDR_SAME(&nexthop->rmap_src.ipv6, &in6addr_any))
			json_object_string_addf(json_nexthop, "rmapSource",
						"%pI6", &nexthop->rmap_src.ipv6);
		else if (!IPV6_ADDR_SAME(&nexthop->src.ipv6, &in6addr_any))
			json_object_string_addf(json_nexthop, "source", "%pI6",
						&nexthop->src.ipv6);
		break;
	case NEXTHOP_TYPE_IFINDEX:
	case NEXTHOP_TYPE_BLACKHOLE:
		break;
	}

	if (nexthop->nh_label && nexthop->nh_label->num_labels) {
		json_labels = json_object_new_array();

		for (int label_index = 0;
		     label_index < nexthop->nh_label->num_labels; label_index++)
			json_object_array_add(
				json_labels,
				json_object_new_int((
					(nexthop->nh_label_type == ZEBRA_LSP_EVPN)
						? label2vni(
							  &nexthop->nh_label->label
								   [label_index])
						: nexthop->nh_label
							  ->label[label_index])));

		json_object_object_add(json_nexthop, "labels", json_labels);
	}

	if (nexthop->weight)
		json_object_int_add(json_nexthop, "weight", nexthop->weight);

	if (nexthop->srte_color)
		json_object_int_add(json_nexthop, "srteColor",
				    nexthop->srte_color);

	if (nexthop->nh_srv6) {
		json_seg6local = json_object_new_object();
		json_object_string_add(json_seg6local, "action",
				       seg6local_action2str_with_next_csid(
					       nexthop->nh_srv6->seg6local_action,
					       seg6local_has_next_csid(
						       &nexthop->nh_srv6->seg6local_ctx)));
		json_seg6local_context = json_object_new_object();
		json_object_object_add(json_nexthop, "seg6local",
				       json_seg6local);

		seg6local_context2json(&nexthop->nh_srv6->seg6local_ctx,
				       nexthop->nh_srv6->seg6local_action,
				       json_seg6local_context);
		json_object_object_add(json_nexthop, "seg6localContext",
				       json_seg6local_context);

		json_srv6_sid_structure = json_object_new_object();
		srv6_sid_structure2json(&nexthop->nh_srv6->seg6local_ctx, json_srv6_sid_structure);
		json_object_object_add(json_seg6local, "sidStructure", json_srv6_sid_structure);

		if (nexthop->nh_srv6->seg6_segs &&
		    nexthop->nh_srv6->seg6_segs->num_segs == 1) {
			json_seg6 = json_object_new_object();
			json_object_string_addf(json_seg6, "segs", "%pI6",
						&nexthop->nh_srv6->seg6_segs
							 ->seg[0]);
			json_object_object_add(json_nexthop, "seg6", json_seg6);
			json_object_string_add(json_nexthop, "srv6EncapBehavior",
					       srv6_headend_behavior2str(nexthop->nh_srv6->seg6_segs
										 ->encap_behavior,
									 false));
		} else {
			if (nexthop->nh_srv6->seg6_segs) {
				json_segs = json_object_new_array();
				for (int seg_idx = 0;
				     seg_idx <
				     nexthop->nh_srv6->seg6_segs->num_segs;
				     seg_idx++)
					json_object_array_add(
						json_segs,
						json_object_new_stringf(
							"%pI6",
							&nexthop->nh_srv6
								 ->seg6_segs
								 ->seg[seg_idx]));
				json_object_object_add(json_nexthop, "seg6",
						       json_segs);
				json_object_string_add(json_nexthop, "srv6EncapBehavior",
						       srv6_headend_behavior2str(nexthop->nh_srv6
											 ->seg6_segs
											 ->encap_behavior,
										 false));
			}
		}
	}
}

/* 把 nexthop 以 CLI 文本形式输出到 vty。先按类型打印“via / is directly
 * connected / unreachable”，再依次追加 vrf、inactive / onlink / linkdown /
 * recursive / dup 标志，随后是 rmap 源 / 源地址、SR-TE color、MPLS 标签、
 * seg6local 与 seg6 SID、weight、备份索引。rn_family 用于 IPv4 前缀 + IPv6
 * 下一跳的特例。无返回值，输出直接写 vty。
 */
/*
 * Helper for nexthop output
 */
void nexthop_vty_helper(struct vty *vty, const struct nexthop *nexthop,
			bool display_vrfid, uint8_t rn_family)
{
	char buf[MPLS_LABEL_STRLEN];
	char seg_buf[SRV6_SEG_STRLEN];
	struct seg6_segs segs;
	uint8_t i;

	switch (nexthop->type) {
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV4_IFINDEX:
		vty_out(vty, " via %pI4", &nexthop->gate.ipv4);
		if (nexthop->ifindex)
			vty_out(vty, ", %s",
				ifindex2ifname(nexthop->ifindex,
					       nexthop->vrf_id));
		break;
	case NEXTHOP_TYPE_IPV6:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		vty_out(vty, " via %s",
			inet_ntop(AF_INET6, &nexthop->gate.ipv6, buf,
				  sizeof(buf)));
		if (nexthop->ifindex)
			vty_out(vty, ", %s",
				ifindex2ifname(nexthop->ifindex,
					       nexthop->vrf_id));
		break;

	case NEXTHOP_TYPE_IFINDEX:
		vty_out(vty, " is directly connected, %s",
			ifindex2ifname(nexthop->ifindex, nexthop->vrf_id));
		break;
	case NEXTHOP_TYPE_BLACKHOLE:
		vty_out(vty, " unreachable");
		switch (nexthop->bh_type) {
		case BLACKHOLE_REJECT:
			vty_out(vty, " (ICMP unreachable)");
			break;
		case BLACKHOLE_ADMINPROHIB:
			vty_out(vty, " (ICMP admin-prohibited)");
			break;
		case BLACKHOLE_NULL:
			vty_out(vty, " (blackhole)");
			break;
		case BLACKHOLE_UNSPEC:
			break;
		}
		break;
	}

	if (display_vrfid)
		vty_out(vty, " (vrf %s)", vrf_id_to_name(nexthop->vrf_id));

	if (!CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_ACTIVE))
		vty_out(vty, " inactive");

	if (CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_ONLINK))
		vty_out(vty, " onlink");

	if (CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_LINKDOWN))
		vty_out(vty, " linkdown");

	if (CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_RECURSIVE))
		vty_out(vty, " (recursive)");

	if (CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_DUPLICATE))
		vty_out(vty, " (dup)");

	switch (nexthop->type) {
	case NEXTHOP_TYPE_IPV4:
	case NEXTHOP_TYPE_IPV4_IFINDEX:
		if (nexthop->rmap_src.ipv4.s_addr)
			vty_out(vty, ", rmapsrc %pI4", &nexthop->rmap_src.ipv4);
		else if (nexthop->src.ipv4.s_addr)
			vty_out(vty, ", src %pI4", &nexthop->src.ipv4);
		break;
	case NEXTHOP_TYPE_IPV6:
	case NEXTHOP_TYPE_IPV6_IFINDEX:
		/* Allow for 5549 ipv4 prefix with ipv6 nexthop */
		if (rn_family == AF_INET && nexthop->rmap_src.ipv4.s_addr)
			vty_out(vty, ", rmapsrc %pI4", &nexthop->rmap_src.ipv4);
		else if (!IPV6_ADDR_SAME(&nexthop->rmap_src.ipv6, &in6addr_any))
			vty_out(vty, ", rmapsrc %pI6", &nexthop->rmap_src.ipv6);
		else if (!IPV6_ADDR_SAME(&nexthop->src.ipv6, &in6addr_any))
			vty_out(vty, ", src %pI6", &nexthop->src.ipv6);
		break;
	case NEXTHOP_TYPE_IFINDEX:
	case NEXTHOP_TYPE_BLACKHOLE:
		break;
	}

	/* SR-TE information */
	if (nexthop->srte_color)
		vty_out(vty, ", SR-TE color %u", nexthop->srte_color);

	/* Label information */
	if (nexthop->nh_label && nexthop->nh_label->num_labels) {
		vty_out(vty, ", label %s",
			mpls_label2str(nexthop->nh_label->num_labels,
				       nexthop->nh_label->label, buf,
				       sizeof(buf), nexthop->nh_label_type, 1));
	}

	if (nexthop->nh_srv6) {
		seg6local_context2str(buf, sizeof(buf),
				      &nexthop->nh_srv6->seg6local_ctx,
				      nexthop->nh_srv6->seg6local_action);
		if (nexthop->nh_srv6->seg6local_action !=
		    ZEBRA_SEG6_LOCAL_ACTION_UNSPEC)
			vty_out(vty, ", seg6local %s%s%s",
				seg6local_action2str_with_next_csid(nexthop->nh_srv6->seg6local_action,
								    seg6local_has_next_csid(
									    &nexthop->nh_srv6
										     ->seg6local_ctx)),
				buf[0] == '\0' ? "" : " ", buf);
		if (nexthop->nh_srv6->seg6_segs &&
		    IPV6_ADDR_CMP(&nexthop->nh_srv6->seg6_segs->seg[0],
				  &in6addr_any)) {
			segs.num_segs = nexthop->nh_srv6->seg6_segs->num_segs;
			for (i = 0; i < segs.num_segs; i++)
				memcpy(&segs.segs[i],
				       &nexthop->nh_srv6->seg6_segs->seg[i],
				       sizeof(struct in6_addr));
			snprintf_seg6_segs(seg_buf, SRV6_SEG_STRLEN, &segs);
			vty_out(vty, ", seg6 %s", seg_buf);
			if (nexthop->nh_srv6->seg6_segs->encap_behavior !=
			    SRV6_HEADEND_BEHAVIOR_H_ENCAPS)
				vty_out(vty, ", encap behavior %s",
					srv6_headend_behavior2str(nexthop->nh_srv6->seg6_segs
									  ->encap_behavior,
								  false));
		}
	}

	if (nexthop->weight)
		vty_out(vty, ", weight %u", nexthop->weight);

	if (CHECK_FLAG(nexthop->flags, NEXTHOP_FLAG_HAS_BACKUP)) {
		vty_out(vty, ", backup %d", nexthop->backup_idx[0]);

		for (i = 1; i < nexthop->backup_num; i++)
			vty_out(vty, ",%d", nexthop->backup_idx[i]);
	}
}
