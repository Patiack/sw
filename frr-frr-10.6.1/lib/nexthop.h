// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Nexthop structure definition.
 * Copyright (C) 1997, 98, 99, 2001 Kunihiro Ishiguro
 * Copyright (C) 2013 Cumulus Networks, Inc.
 */

/*
 * 文件导读：nexthop.h —— FRR 转发面“下一跳”对象的公共定义。
 *
 * 本文件只定义数据结构、枚举、宏与函数原型，具体实现见 nexthop.c。
 *
 * 一、nexthop 是什么
 *   struct nexthop 描述报文离开本设备时的“下一个出口”，是路由项（route）
 *   与实际转发动作之间的桥梁。它可能携带：网关地址（gate）、本地源地址
 *   （src）、出接口索引（ifindex）、MPLS 标签栈（nh_label）、SRv6 SID 与
 *   headend 行为（nh_srv6）、权重（weight，用于 UCMP）等。
 *
 * 二、NEXTHOP_TYPE_* 语义（枚举 nexthop_types_t）
 *   NEXTHOP_TYPE_IFINDEX       仅接口，直连，无网关地址
 *   NEXTHOP_TYPE_IPV4          仅 IPv4 网关（出接口由递归解析得到）
 *   NEXTHOP_TYPE_IPV4_IFINDEX  IPv4 网关 + 指定出接口
 *   NEXTHOP_TYPE_IPV6          仅 IPv6 网关
 *   NEXTHOP_TYPE_IPV6_IFINDEX  IPv6 网关 + 指定出接口
 *   NEXTHOP_TYPE_BLACKHOLE     Null0 丢弃类（结合 enum blackhole_type）
 *
 * 三、哈希与相等判定
 *   结构体中 _hash_begin 与 _hash_end 之间的字段（type / vrf_id / ifindex /
 *   nh_label_type / gate / src / rmap_src）会被整体喂给 jhash()，用于
 *   哈希表去重；static_assert 保证这段区间没有 padding 空洞，改动字段顺序
 *   或类型会触发编译期报错。相等判定见 nexthop_same() 系列函数。
 *
 * 四、标志位（NEXTHOP_FLAG_*）
 *   描述 nexthop 的状态：ACTIVE / FIB / RECURSIVE / ONLINK / DUPLICATE 等；
 *   其中只有 ONLINK 参与哈希（NEXTHOP_FLAGS_HASHED）。
 *
 * 五、递归解析
 *   需要递归解析时设置 NEXTHOP_FLAG_RECURSIVE，解析结果挂在 resolved 链表，
 *   反向父指针为 rparent。
 *
 * 六、备份与权重
 *   主 nexthop 可挂最多 NEXTHOP_MAX_BACKUPS 个备份索引（backup_idx）；
 *   weight 用于非等价多路径（UCMP）。
 */

#ifndef _LIB_NEXTHOP_H
#define _LIB_NEXTHOP_H

#include "jhash.h"
#include "prefix.h"
#include "mpls.h"
#include "vxlan.h"
#include "srv6.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum next hop string length - gateway + ifindex */
#define NEXTHOP_STRLEN (INET6_ADDRSTRLEN + 30)

/* 统一的地址联合体：同一块内存既可视作 IPv4 地址也可视作 IPv6 地址。
 * 因为 nexthop 的 gate / src 字段会被整体哈希，放入 IPv4 时必须把
 * 16 字节全部清零（见下方注释），否则残留字节会导致哈希不一致。
 */
union g_addr {
	struct in_addr ipv4;
	struct in6_addr ipv6;
};

/* 下一跳类型：决定 nexthop 携带哪些字段、以及如何被解析与安装。
 * 编码值从 1 开始，不可随意调整，因为会影响持久化 / 比较语义。
 */
enum nexthop_types_t {
	NEXTHOP_TYPE_IFINDEX = 1,  /* Directly connected.  */
	NEXTHOP_TYPE_IPV4,	 /* IPv4 nexthop.  */
	NEXTHOP_TYPE_IPV4_IFINDEX, /* IPv4 nexthop with ifindex.  */
	NEXTHOP_TYPE_IPV6,	 /* IPv6 nexthop.  */
	NEXTHOP_TYPE_IPV6_IFINDEX, /* IPv6 nexthop with ifindex.  */
	NEXTHOP_TYPE_BLACKHOLE,    /* Null0 nexthop.  */
};

/* 黑洞（blackhole）下一跳的细分类型，配合 NEXTHOP_TYPE_BLACKHOLE 使用：
 * UNSPEC 未指定；NULL 静默丢弃；REJECT 回送 ICMP 不可达；
 * ADMINPROHIB 回送 ICMP 管理禁止。
 */
enum blackhole_type {
	BLACKHOLE_UNSPEC = 0,
	BLACKHOLE_NULL,
	BLACKHOLE_REJECT,
	BLACKHOLE_ADMINPROHIB,
};

/* 下一跳使用的封装类型；数值与 FPM（转发面管理接口）约定一致。 */
enum nh_encap_type {
	NET_VXLAN = 100, /* value copied from FPM_NH_ENCAP_VXLAN. */
};

/* 每个主 nexthop 允许关联的备份 nexthop 数量上限（固定 8 个）。 */
/* Fixed limit on the number of backup nexthops per primary nexthop */
#define NEXTHOP_MAX_BACKUPS  8

/* 备份索引取值范围上限：索引以 uint8_t 存储，故最大 255。 */
/* Backup index value is limited */
#define NEXTHOP_BACKUP_IDX_MAX 255

/* Nexthop structure. */
/* 单个 nexthop 对象。它同时被用作链表节点（next / prev）与哈希表元素。
 * 注意 _hash_begin 与 _hash_end 之间是“参与哈希的连续内存块”，
 * 详见文件导读第三节与结构体下方 static_assert。
 */
struct nexthop {
	struct nexthop *next;
	struct nexthop *prev;


	/* begin of hashed data - all fields from here onwards are given to
	 * jhash() as one consecutive chunk.  DO NOT create "padding holes".
	 * DO NOT insert pointers that need to be deep-hashed.
	 *
	 * static_assert() below needs to be updated when fields are added
	 */
	char _hash_begin[0];

	/* see above */
	enum nexthop_types_t type;

	/* What vrf is this nexthop associated with? */
	vrf_id_t vrf_id;

	/* Interface index. */
	ifindex_t ifindex;

	/* Type of label(s), if any */
	enum lsp_types_t nh_label_type;

	/* padding: keep 16 byte alignment here */

	/* Nexthop address
	 * make sure all 16 bytes for IPv6 are zeroed when putting in an IPv4
	 * address since the entire thing is hashed as-is
	 */
	union {
		union g_addr gate;
		enum blackhole_type bh_type;
	};
	union g_addr src;
	union g_addr rmap_src; /* Src is set via routemap */

	/* end of hashed data - remaining fields in this struct are not
	 * directly fed into jhash().  Most of them are actually part of the
	 * hash but have special rules or handling attached.
	 */
	char _hash_end[0];

	/* flags 字段的各比特含义（NEXTHOP_FLAG_*）：
 * ACTIVE 本跳可用；FIB 已装表；RECURSIVE 需/已递归解析；ONLINK 接口路由需
 * onlink 安装；DUPLICATE 与另一可用跳重复；RNH_FILTERED 被 rmap 过滤；
 * HAS_BACKUP 已设置备份索引；SRTE 关联 SR-TE 策略；EVPN 来自 EVPN；
 * LINKDOWN 链路 down 时仍保留不删除。
 * 只有 NEXTHOP_FLAGS_HASHED 列出的标志（目前仅 ONLINK）参与哈希去重。
 */
	/* Weight of the nexthop ( for unequal cost ECMP ) */
	uint16_t weight;

	uint16_t flags;
#define NEXTHOP_FLAG_ACTIVE     (1 << 0) /* This nexthop is alive. */
#define NEXTHOP_FLAG_FIB        (1 << 1) /* FIB nexthop. */
#define NEXTHOP_FLAG_RECURSIVE  (1 << 2) /* Recursive nexthop. */
#define NEXTHOP_FLAG_ONLINK     (1 << 3) /* Nexthop should be installed
					  * onlink.
					  */
#define NEXTHOP_FLAG_DUPLICATE  (1 << 4) /* nexthop duplicates another
					  * active one
					  */
#define NEXTHOP_FLAG_RNH_FILTERED  (1 << 5) /* rmap filtered, used by rnh */
#define NEXTHOP_FLAG_HAS_BACKUP (1 << 6)    /* Backup nexthop index is set */
#define NEXTHOP_FLAG_SRTE       (1 << 7) /* SR-TE color used for BGP traffic */
#define NEXTHOP_FLAG_EVPN       (1 << 8) /* nexthop is EVPN */
#define NEXTHOP_FLAG_LINKDOWN   (1 << 9) /* is not removed on link down */

	/* which flags are part of nexthop_hash().  Should probably be split
	 * off into a separate field...
	 */
#define NEXTHOP_FLAGS_HASHED NEXTHOP_FLAG_ONLINK

/* 判定“本跳真正可用”：既置了 ACTIVE，又不是重复项（未被 DUPLICATE 标记）。 */
#define NEXTHOP_IS_ACTIVE(flags)                                               \
	(CHECK_FLAG(flags, NEXTHOP_FLAG_ACTIVE)                                \
	 && !CHECK_FLAG(flags, NEXTHOP_FLAG_DUPLICATE))

	/* resolved 指向本跳经递归解析后得到的下一跳链表；rparent 指回发起
	 * 递归的父跳，便于从解析结果反查顶层。
	 */
	/* Nexthops obtained by recursive resolution.
	 *
	 * If the nexthop struct needs to be resolved recursively,
	 * NEXTHOP_FLAG_RECURSIVE will be set in flags and the nexthops
	 * obtained by recursive resolution will be added to `resolved'.
	 */
	struct nexthop *resolved;
	/* Recursive parent */
	struct nexthop *rparent;

	/* Label(s) associated with this nexthop. */
	struct mpls_label_stack *nh_label;

	/* Count and index of corresponding backup nexthop(s) in a backup list;
	 * only meaningful if the HAS_BACKUP flag is set.
	 */
	uint8_t backup_num;
	uint8_t backup_idx[NEXTHOP_MAX_BACKUPS];

	/* 封装信息：目前仅用于 VXLAN，nh_encap.vni 记录目标 VNI。 */
	/* Encapsulation information. */
	enum nh_encap_type nh_encap_type;
	union {
		vni_t vni;
	} nh_encap;

	/* EVPN router's MAC.
	 * Don't support multiple RMAC from the same VTEP yet, so it's not
	 * included in hash key.
	 */
	struct ethaddr rmac;

	/* SR-TE color used for matching SR-TE policies */
	uint32_t srte_color;

	/* SRv6 相关信息（SID 列表 / seg6local 行为等），为空表示无 SRv6。
	 * 该指针指向的子结构不参与哈希键，比较时需单独处理。
	 */
	/* SRv6 information */
	struct nexthop_srv6 *nh_srv6;
};

/* 编译期校验：_hash_begin.._hash_end 区间的大小必须恰好等于所列各哈希字段
 * 大小之和。若新增 / 调整字段导致结构体出现 padding 空洞，这里会编译失败，
 * 提示需要在恰当位置插入 _pad 字段，从而保证哈希连续块不会读到脏数据。
 */
/* all hashed fields (including padding, if it is necessary to add) need to
 * be listed in the static_assert below
 */

/* 取成员大小的辅助宏，仅供下面的 static_assert 使用，用完即 #undef。 */
#define S(field) sizeof(((struct nexthop *)NULL)->field)

static_assert(
	offsetof(struct nexthop, _hash_end) - offsetof(struct nexthop, _hash_begin) ==
		S(type) + S(vrf_id) + S(ifindex) + S(nh_label_type) + S(gate) + S(src) + S(rmap_src),
	"struct nexthop contains padding, this can break things. insert _pad fields at appropriate places");

#undef S

/* 对哈希连续块求 jhash 的内联辅助函数。把 _hash_begin 起始、长度为
 * (_hash_end - _hash_begin) 的一段字节喂给 jhash()；用负偏移锚定的写法让
 * 关心哈希实现的人一眼看清算的是什么。
 */
/* this is here to show exactly what is meant by the comments above about
 * the hashing
 */
static inline uint32_t _nexthop_hash_bytes(const struct nexthop *nh, uint32_t seed)
{
	return jhash(&nh->_hash_begin,
		     offsetof(struct nexthop, _hash_end) - offsetof(struct nexthop, _hash_begin),
		     seed);
}

/* 把 new 接到 to 之后：设置 to->next = new、new->prev = to，并把 new->next
 * 置空。注意它只是接一个节点，不会遍历到链表尾部——调用者需自行确保 to
 * 就是当前尾节点。也不做任何内存分配。
 */
/* Utility to append one nexthop to another. */
#define NEXTHOP_APPEND(to, new)           \
	do {                              \
		(to)->next = (new);       \
		(new)->prev = (to);       \
		(new)->next = NULL;       \
	} while (0)

/* 分配并清零一个新的 nexthop（堆分配），返回其指针。失败时返回 NULL。 */
struct nexthop *nexthop_new(void);

/* 释放单个 nexthop（含其内部标签栈、SRv6 等子对象）。 */
void nexthop_free(struct nexthop *nexthop);
/* 释放整条 nexthop 链表（从 nexthop 开始沿 next 逐个释放）。 */
void nexthops_free(struct nexthop *nexthop);

/* 给 nexthop 设置 MPLS 标签栈，标签类型 ltype（如 LSP_LDP / LSP_BGP 等），
 * 标签个数 num_labels，标签数组 labels。会覆盖原有栈。
 */
void nexthop_add_labels(struct nexthop *nexthop, enum lsp_types_t ltype,
			uint8_t num_labels, const mpls_label_t *labels);
/* 清空 nexthop 的标签栈并释放其内存。 */
void nexthop_del_labels(struct nexthop *nexthop);
/* 用新栈替换旧栈（比先 del 再 add 更省事的封装）。 */
void nexthop_change_labels(struct nexthop *nexthop, struct mpls_label_stack *new_stack);

/* 设置 SRv6 seg6local 端点行为：action 为 seg6local 动作码，
 * ctx 提供该动作的上下文（如 SID、接口、表等）。
 */
void nexthop_add_srv6_seg6local(struct nexthop *nexthop, uint32_t action,
				const struct seg6local_context *ctx);
/* 清除 nexthop 上的 seg6local 信息。 */
void nexthop_del_srv6_seg6local(struct nexthop *nexthop);
/* 设置 SRv6 封装：seg 为 SID 列表，num_segs 为 SID 个数，
 * encap_behavior 指定 headend 行为（如 H.Encaps / L2Encaps 等）。
 */
void nexthop_add_srv6_seg6(struct nexthop *nexthop, const struct in6_addr *seg, int num_segs,
			   enum srv6_headend_behavior encap_behavior);
/* 清除 nexthop 上的 SRv6 SID 列表信息。 */
void nexthop_del_srv6_seg6(struct nexthop *nexthop);

/*
 * Allocate a new nexthop object and initialize it from various args.
 */
/* 以下 nexthop_from_* 系列：按参数构造并返回一个新的 nexthop 对象
 * （内部会设置对应的 NEXTHOP_TYPE_* 与各字段）。
 */
/* 直连型：仅给 ifindex 与 vrf，无网关地址。 */
struct nexthop *nexthop_from_ifindex(ifindex_t ifindex, vrf_id_t vrf_id);
/* IPv4 网关型：ipv4 为网关，src 为源地址，vrf_id 为所属 VRF。 */
struct nexthop *nexthop_from_ipv4(const struct in_addr *ipv4,
				  const struct in_addr *src,
				  vrf_id_t vrf_id);
/* IPv4 网关 + 出接口型。 */
struct nexthop *nexthop_from_ipv4_ifindex(const struct in_addr *ipv4,
					  const struct in_addr *src,
					  ifindex_t ifindex, vrf_id_t vrf_id);
/* IPv6 网关型（出接口由递归解析得到）。 */
struct nexthop *nexthop_from_ipv6(const struct in6_addr *ipv6,
				  vrf_id_t vrf_id);
/* IPv6 网关 + 出接口型。 */
struct nexthop *nexthop_from_ipv6_ifindex(const struct in6_addr *ipv6,
					  ifindex_t ifindex, vrf_id_t vrf_id);
/* 黑洞型：bh_type 指定丢弃行为，nh_vrf_id 指定 VRF。 */
struct nexthop *nexthop_from_blackhole(enum blackhole_type bh_type,
				       vrf_id_t nh_vrf_id);

/*
 * Hash a nexthop. Suitable for use with hash tables.
 *
 * Please double check the code on what is included in the hash, there was
 * documentation here but it got outdated and the only thing worse than no
 * doc is incorrect doc.
 */
/* 计算 nexthop 的哈希值，供哈希表 / 查找使用（对 _hash_begin.._hash_end
 * 连续块求 jhash）。注意实际参与哈希的字段是有限的，勿凭直觉假设。
 */
uint32_t nexthop_hash(const struct nexthop *nexthop);

/* 以下为“相等 / 比较”判定族，返回值语义统一：相等返回 true，或 cmp 返回
 * 负数 / 0 / 正数（表示 nh1 小于 / 等于 / 大于 nh2）。
 * nexthop_same            完整比较（含接口、标签、权重等全部相关字段）。
 * nexthop_same_no_ifindex 忽略 ifindex。
 * nexthop_same_no_labels  忽略标签栈。
 * nexthop_same_no_weight  忽略权重。
 * nexthop_cmp             完整排序比较；nexthop_cmp_no_weight 忽略权重。
 * nexthop_g_addr_cmp      按类型比较两个网关地址（区分 IPv4 / IPv6）。
 * 注意：这些函数都不修改入参，且 nh1 / nh2 可为 NULL 时行为各异，见实现。
 */
extern bool nexthop_same(const struct nexthop *nh1, const struct nexthop *nh2);
extern bool nexthop_same_no_ifindex(const struct nexthop *nh1, const struct nexthop *nh2);
extern bool nexthop_same_no_labels(const struct nexthop *nh1,
				   const struct nexthop *nh2);
extern bool nexthop_same_no_weight(const struct nexthop *nh1, const struct nexthop *nh2);
extern int nexthop_cmp(const struct nexthop *nh1, const struct nexthop *nh2);
extern int nexthop_cmp_no_weight(const struct nexthop *nh1,
				 const struct nexthop *nh2);
extern int nexthop_g_addr_cmp(enum nexthop_types_t type,
			      const union g_addr *addr1,
			      const union g_addr *addr2);

/* More-limited comparison function used to detect duplicate nexthops.
 * Returns -1, 0, 1
 */
/* 受限比较：用于识别“重复 nexthop”，返回 -1 / 0 / 1。只比较最基本的键
 * （类型、网关、接口等），不涉及标签、权重、递归等，便于去重。
 */
int nexthop_cmp_basic(const struct nexthop *nh1, const struct nexthop *nh2);

/* 把 nexthop 类型枚举转成可读字符串。 */
extern const char *nexthop_type_to_str(enum nexthop_types_t nh_type);
/* 判断两个 nexthop 的标签栈是否一致（类型与标签逐个比较）。 */
extern bool nexthop_labels_match(const struct nexthop *nh1,
				 const struct nexthop *nh2);

/* 把 nexthop 格式化到调用者提供的缓冲区 str（大小 size），返回 str。
 * 返回的字符串形如“网关 + 接口 + 标签”等，供日志 / 显示使用。
 */
extern const char *nexthop2str(const struct nexthop *nexthop,
			       char *str, int size);
/* 沿链取下“下一个” nexthop：会先走 resolved 链（若有），再走 next 链。 */
extern struct nexthop *nexthop_next(const struct nexthop *nexthop);
/* 与 nexthop_next 类似，但可指定是否把递归解析链也算进去。 */
extern struct nexthop *nexthop_next_resolution(const struct nexthop *nexthop,
					       bool nexthop_resolution);
/* 递归链中下一个“可用（active）且已解析”的 nexthop。 */
extern struct nexthop *
nexthop_next_active_resolved(const struct nexthop *nexthop);
/* 计算 nexthop 所在递归层级（顶层为 0，逐层深入递增）。 */
extern unsigned int nexthop_level(const struct nexthop *nexthop);
/* Copies to an already allocated nexthop struct */
/* 把 nexthop 的内容拷贝到已分配的 copy 中，含递归信息；rparent 指定拷贝
 * 后的递归父指针。不分配内存。
 */
extern void nexthop_copy(struct nexthop *copy, const struct nexthop *nexthop,
			 struct nexthop *rparent);
/* 同上，但不拷贝递归（resolved / rparent）信息。 */
/* Copies to an already allocated nexthop struct, not including recurse info */
extern void nexthop_copy_no_recurse(struct nexthop *copy,
				    const struct nexthop *nexthop,
				    struct nexthop *rparent);
/* 复制一个 nexthop 并返回新分配的对象（含递归信息）。失败返回 NULL。 */
/* Duplicates a nexthop and returns the newly allocated nexthop */
extern struct nexthop *nexthop_dup(const struct nexthop *nexthop,
				   struct nexthop *rparent);
/* 复制一个 nexthop 并返回新分配的对象，但不含递归信息。 */
/* Duplicates a nexthop and returns the newly allocated nexthop */
extern struct nexthop *nexthop_dup_no_recurse(const struct nexthop *nexthop,
					      struct nexthop *rparent);

/* 判断该 nexthop 是否为黑洞型（三跳 / 四跳黑洞均算）。 */
/* Is this nexthop a blackhole? */
extern bool nexthop_is_blackhole(const struct nexthop *nh);

/*
 * Parse one or more backup index values, as comma-separated numbers,
 * into caller's array of uint8_ts. The array must be NEXTHOP_MAX_BACKUPS
 * in size. Mails back the number of values converted, and returns 0 on
 * success, <0 if an error in parsing.
 */
/* 把逗号分隔的备份索引字符串（如 “1,2,3”）解析进 backups 数组，
 * 数组大小必须为 NEXTHOP_MAX_BACKUPS。*num_backups 回填解析出的个数；
 * 成功返回 0，解析出错返回负值。
 */
int nexthop_str2backups(const char *str, int *num_backups,
			uint8_t *backups);

/* 把 nexthop 以 JSON 形式写入 json_nexthop；display_vrfid 控制是否输出
 * VRF 字段，rn_family 控制地址族相关的显示。无返回值。
 */
void nexthop_json_helper(struct json_object *json_nexthop,
			 const struct nexthop *nexthop, bool display_vrfid,
			 uint8_t rn_family);
/* 把 nexthop 以 CLI 文本形式输出到 vty；参数含义同 nexthop_json_helper。 */
void nexthop_vty_helper(struct vty *vty, const struct nexthop *nexthop,
			bool display_vrfid, uint8_t rn_family);

#ifdef _FRR_ATTRIBUTE_PRINTFRR
#pragma FRR printfrr_ext "%pNH"  (struct nexthop *)
#endif

/* printfrr 的 %pNHs 格式串回调：把 nexthop 追加写入 fbuf，返回写入长度。 */
ssize_t printfrr_nhs(struct fbuf *buf, const struct nexthop *nh);
#ifdef __cplusplus
}
#endif

#endif /*_LIB_NEXTHOP_H */
