// SPDX-License-Identifier: GPL-2.0-or-later
/* Route filtering function.
 * Copyright (C) 1998, 1999 Kunihiro Ishiguro
 */

/*
 * 文件导读 filter.c —— 访问列表 / 前缀列表的通用底座（lib 层核心实现）
 *
 * 定位：本文件实现“规则怎么存、怎么查、怎么匹配”，是 ip access-list /
 *   ipv6 access-list / mac access-list / prefix-list 等功能的公共引擎。
 *   它不直接解析配置，也不直接生成命令——配置路径由 filter_nb.c（YANG）
 *   与 filter_cli.c（CLI）分别驱动，二者最终都调用本文件导出的函数。
 *
 * 三方分工回顾：
 *   filter.c     ：数据结构操作与匹配算法（本文件）；
 *   filter_nb.c  ：把 YANG 数据树的增删改翻译成对 access_list / filter 的操作；
 *   filter_cli.c ：命令行解析与 show 输出，同样落到本文件的 API 上。
 *
 * 本文件组织：
 *   1) 三个静态 access_master（IPv4 / IPv6 / MAC）：各地址族列表的总管；
 *   2) filter_match_* 系列：匹配算法（Cisco 通配符、zebra 前缀、sadr 源目的）；
 *   3) access_list 生命周期：new / free / insert / delete / lookup / get；
 *   4) 规则级操作、seq 序号生成、比较与显示等辅助函数。
 *
 * 关键算法提示：
 *   - Cisco 匹配用“通配符取反”技巧：先算 addr & ~addr_mask 再与规则地址比，
 *     通配符位为 1 表示不关心，取反后该位为 0，于是被“与”掉不参与比较。
 *   - zebra 匹配走 prefix_match 做前缀包含判断，exact 时还要求长度相等。
 *   - sadr 用于扩展 ACL 的「源 + 目的」二元匹配，IPv4 / IPv6 各一份。
 *
 * 坑：filter_match_* 返回 int（命中 1 / 未命中 0），与 access_list_apply
 *   返回的 enum filter_type 不是一回事，阅读调用点时要分清。
 */

#include <zebra.h>

#include "prefix.h"
#include "filter.h"
#include "memory.h"
#include "command.h"
#include "sockunion.h"
#include "buffer.h"
#include "log.h"
#include "routemap.h"
#include "libfrr.h"
#include "northbound_cli.h"
#include "json.h"

/*
 * 三个内存类型标签：ACCESS_LIST（列表本身）、ACCESS_LIST_STR（列表名字串）、
 * ACCESS_FILTER（规则条目）。用于内存统计与泄漏定位，
 * 申请 / 释放务必使用同一标签（XMALLOC / XFREE 成对）。
 */
DEFINE_MTYPE_STATIC(LIB, ACCESS_LIST, "Access List");
DEFINE_MTYPE_STATIC(LIB, ACCESS_LIST_STR, "Access List Str");
DEFINE_MTYPE_STATIC(LIB, ACCESS_FILTER, "Access Filter");

/*
 * 三个静态 master 实例：分别管理 MAC（L2VPN）/ IPv4 / IPv6 的访问列表链。
 * 每个都初始化为「空链表 + 两个空钩子」，钩子稍后由感兴趣的模块注册。
 */
/* Static structure for mac access_list's master. */
static struct access_master access_master_mac = {
	{NULL, NULL},
	NULL,
	NULL,
};

/* Static structure for IPv4 access_list's master. */
static struct access_master access_master_ipv4 = {
	{NULL, NULL},
	NULL,
	NULL,
};

/* Static structure for IPv6 access_list's master. */
static struct access_master access_master_ipv6 = {
	{NULL, NULL},
	NULL,
	NULL,
};

/*
 * 全 1 的 IPv6 掩码常量（128 位全 F），用于“全部不关心”的地址运算，
 * 避免在匹配循环里反复构造掩码。（clang-format off 是为了保持大括号排版）
 */
/* clang-format off */
static const struct in6_addr in6_mask_all = {
	.s6_addr32 = {
		0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
	}
};
/* clang-format on */

/*
 * access_master_get —— 按地址族返回对应的全局 master。
 *   afi：AFI_IP -> IPv4，AFI_IP6 -> IPv6，AFI_L2VPN -> MAC。
 * 返回：对应 master 指针；未知 AFI 返回 NULL（调用方需自行判空）。
 * 副作用：无，只是返回静态对象的地址。
 */
static struct access_master *access_master_get(afi_t afi)
{
	if (afi == AFI_IP)
		return &access_master_ipv4;
	else if (afi == AFI_IP6)
		return &access_master_ipv6;
	else if (afi == AFI_L2VPN)
		return &access_master_mac;
	return NULL;
}

/*
 * filter_new —— 分配并清零一条空的过滤规则（struct filter）。
 * 返回：新规则指针；XCALLOC 失败会直接中止进程（FRR 惯例）。
 * 注意：只分配，不挂链、不设默认动作，须由调用方填充后交给
 *   access_list_filter_add 挂入列表。
 */
/* Allocate new filter structure. */
struct filter *filter_new(void)
{
	return XCALLOC(MTYPE_ACCESS_FILTER, sizeof(struct filter));
}

/*
 * filter_free —— 释放一条规则占用的内存。
 * 只归还 struct filter 本身；其内部字段都是值类型（union 中没有堆指针），
 * 因此无需再释放其它成员。
 */
static void filter_free(struct filter *filter)
{
	XFREE(MTYPE_ACCESS_FILTER, filter);
}

/*
 * filter_type_str —— 把规则动作枚举转成可读字符串。
 * 返回："permit" / "deny" / "dynamic"；未知取值返回空串。
 * 供 show 输出与日志使用。
 */
/* Return string of filter_type. */
static const char *filter_type_str(struct filter *filter)
{
	switch (filter->type) {
	case FILTER_PERMIT:
		return "permit";
	case FILTER_DENY:
		return "deny";
	case FILTER_DYNAMIC:
		return "dynamic";
	default:
		return "";
	}
}

/*
 * filter_match_cisco_ipv4 —— 用单条 Cisco 风格规则匹配一个 IPv4 前缀 / 地址。
 * 参数：mfilter 承载规则的 struct filter；p 待匹配前缀。
 * 返回：命中 1 / 未命中 0。
 * 算法：check_addr = p 的地址 & ~通配符掩码，再与规则保存的地址比较；
 *   扩展 ACL（extended）还会把前缀长度用 masklen2ip 换算成掩码，
 *   以同样“与通配符取反”的方式与规则保存的 mask 比较（见 wtf 分支）。
 * 坑：扩展分支用的是 wtf.mask / wtf.mask_mask 字段，
 *   这是“伪前缀列表模式”复用的存储，别误当成普通掩码成员。
 */
static int filter_match_cisco_ipv4(struct filter *mfilter, const struct prefix *p)
{
	struct filter_cisco *filter;
	struct in_addr mask;
	uint32_t check_addr;
	uint32_t check_mask;

	filter = &mfilter->u.cfilter;
	check_addr = p->u.prefix4.s_addr & ~filter->addr_mask.s_addr;

	if (filter->extended) {
		masklen2ip(p->prefixlen, &mask);
		check_mask = mask.s_addr & ~filter->wtf.mask_mask.s_addr;

		if (memcmp(&check_addr, &filter->addr.s_addr, IPV4_MAX_BYTELEN) == 0 &&
		    memcmp(&check_mask, &filter->wtf.mask.s_addr, IPV4_MAX_BYTELEN) == 0)
			return 1;
	} else if (memcmp(&check_addr, &filter->addr.s_addr, IPV4_MAX_BYTELEN)
		   == 0)
		return 1;

	return 0;
}

/*
 * filter_match_cisco_ipv6 —— 用单条 Cisco 风格规则匹配一个 IPv6 前缀。
 * 参数 / 返回：同 filter_match_cisco_ipv4（命中 1 / 未命中 0）。
 * 算法：对地址的每个 32 位字做 addr & ~src_mask；扩展模式下再对换算出的
 *   掩码做 mask & ~dst_mask，分别与规则保存的 src / dst 比较。
 * 坑：IPv6 版本用的是 sadr6.src / sadr6.src_mask / sadr6.dst 字段，
 *   与 IPv4 版本的 addr / wtf 完全不同，移植代码时不要张冠李戴。
 */
static int filter_match_cisco_ipv6(struct filter *mfilter, const struct prefix *p)
{
	struct filter_cisco *filter;
	struct in6_addr mask;
	struct in6_addr check_addr;
	struct in6_addr check_mask;
	size_t i;

	filter = &mfilter->u.cfilter;
	for (i = 0; i < sizeof(check_addr) / 4; i++)
		check_addr.s6_addr32[i] = p->u.prefix6.s6_addr32[i] &
					  ~filter->sadr6.src_mask.s6_addr32[i];

	if (filter->extended) {
		masklen2ip6(p->prefixlen, &mask);
		for (i = 0; i < sizeof(check_mask) / 4; i++)
			check_mask.s6_addr32[i] = mask.s6_addr32[i] &
						  ~filter->sadr6.dst_mask.s6_addr32[i];

		if (memcmp(&check_addr, &filter->sadr6.src, IPV6_MAX_BYTELEN) == 0 &&
		    memcmp(&check_mask, &filter->sadr6.dst, IPV6_MAX_BYTELEN) == 0)
			return 1;
	} else if (memcmp(&check_addr, &filter->sadr6.src, IPV6_MAX_BYTELEN) == 0)
		return 1;

	return 0;
}

/*
 * filter_match_cisco —— 按 ipv6 标志分派到 IPv4 / IPv6 的具体匹配实现。
 * 返回：命中 1 / 未命中 0。
 */
/* If filter match to the prefix then return 1. */
static inline int filter_match_cisco(struct filter *mfilter, const struct prefix *p)
{
	return (mfilter->u.cfilter.ipv6) ? filter_match_cisco_ipv6(mfilter, p)
					 : filter_match_cisco_ipv4(mfilter, p);
}

/*
 * filter_match_zebra —— 用单条 zebra（前缀列表）风格规则匹配前缀。
 * 返回：命中 1 / 未命中 0。
 * 逻辑：先要求 family 相同；exact 时前缀长度也必须相同，
 *   最后统一用 prefix_match 做前缀包含判断（更长的前缀也算命中）。
 */
/* If filter match to the prefix then return 1. */
static int filter_match_zebra(struct filter *mfilter, const struct prefix *p)
{
	struct filter_zebra *filter = NULL;

	filter = &mfilter->u.zfilter;

	if (filter->prefix.family == p->family) {
		if (filter->exact) {
			if (filter->prefix.prefixlen == p->prefixlen)
				return prefix_match(&filter->prefix, p);
			else
				return 0;
		} else
			return prefix_match(&filter->prefix, p);
	} else
		return 0;
}

/*
 * filter_match_cisco_sadr4 —— 扩展 ACL 的「源 + 目的」IPv4 二元匹配。
 * 参数：src / dst 分别待匹配的源、目的前缀。
 * 返回：命中 1 / 未命中 0。
 * 逻辑：扩展规则对 src 和 dst 都做“与通配符取反后比较”，两者都相等才命中；
 *   非扩展规则退化为只匹配 dst（与标准 ACL 行为保持一致）。
 */
static int filter_match_cisco_sadr4(struct filter *mfilter, const struct prefix *src,
				    const struct prefix *dst)
{
	struct filter_cisco *filter;

	filter = &mfilter->u.cfilter;

	if (filter->extended) {
		in_addr_t src_m = src->u.prefix4.s_addr;
		in_addr_t dst_m = dst->u.prefix4.s_addr;

		src_m &= ~filter->sadr.src_mask.s_addr;
		dst_m &= ~filter->sadr.dst_mask.s_addr;

		if (src_m == filter->sadr.src.s_addr && dst_m == filter->sadr.dst.s_addr)
			return 1;
	} else {
		in_addr_t check_addr;

		check_addr = dst->u.prefix4.s_addr & ~filter->addr_mask.s_addr;
		if (check_addr == filter->addr.s_addr)
			return 1;
	}

	return 0;
}

/*
 * filter_match_cisco_sadr6 —— 扩展 ACL 的「源 + 目的」IPv6 二元匹配。
 * 参数 / 返回：同 sadr4（命中 1 / 未命中 0）。
 * 逻辑：源、目的各按 32 位字与通配符掩码取反后比较；
 *   非扩展规则只用 dst 与规则保存的 src 字段比较。
 * 坑：非扩展分支借用 sadr6.src 当作目的地址来比，属历史实现约定。
 */
static int filter_match_cisco_sadr6(struct filter *mfilter, const struct prefix *src,
				    const struct prefix *dst)
{
	struct filter_cisco *filter;
	size_t i;

	filter = &mfilter->u.cfilter;

	if (filter->extended) {
		struct in6_addr src_m = src->u.prefix6;
		struct in6_addr dst_m = dst->u.prefix6;
		struct in6_addr fsrc_m = filter->sadr6.src;
		struct in6_addr fdst_m = filter->sadr6.dst;

		for (i = 0; i < sizeof(src_m) / 4; i++) {
			src_m.s6_addr32[i] &= ~filter->sadr6.src_mask.s6_addr32[i];
			dst_m.s6_addr32[i] &= ~filter->sadr6.dst_mask.s6_addr32[i];
			fsrc_m.s6_addr32[i] &= ~filter->sadr6.src_mask.s6_addr32[i];
			fdst_m.s6_addr32[i] &= ~filter->sadr6.dst_mask.s6_addr32[i];
		}

		if (memcmp(&src_m, &fsrc_m, sizeof(src_m)) == 0 &&
		    memcmp(&dst_m, &fdst_m, sizeof(dst_m)) == 0)
			return 1;
	} else {
		struct in6_addr check_addr = dst->u.prefix6;
		struct in6_addr filter_addr = filter->sadr6.src;

		for (i = 0; i < sizeof(check_addr) / 4; i++) {
			check_addr.s6_addr32[i] &= ~filter->sadr6.src_mask.s6_addr32[i];
			filter_addr.s6_addr32[i] &= ~filter->sadr6.src_mask.s6_addr32[i];
		}

		if (memcmp(&check_addr, &filter_addr, sizeof(check_addr)) == 0)
			return 1;
	}

	return 0;
}

/*
 * filter_match_cisco_sadr —— 按 ipv6 标志分派到 sadr6 / sadr4。
 */
/* If filter match to the prefix then return 1. */
static inline int filter_match_cisco_sadr(struct filter *mfilter, const struct prefix *src,
					  const struct prefix *dst)
{
	return mfilter->u.cfilter.ipv6 ? filter_match_cisco_sadr6(mfilter, src, dst)
				       : filter_match_cisco_sadr4(mfilter, src, dst);
}

/*
 * filter_match_zebra_sadr —— zebra 风格版本的「源 + 目的」匹配。
 * 说明：src 被忽略，实际只对 dst 做前缀匹配（见函数内 src ignored here 注释）。
 * 返回：命中 1 / 未命中 0。
 */
/* If filter match to the prefix then return 1. */
static int filter_match_zebra_sadr(struct filter *mfilter, const struct prefix *src,
				   const struct prefix *dst)
{
	struct filter_zebra *filter = NULL;
	/* src ignored here */
	const struct prefix *p = dst;

	filter = &mfilter->u.zfilter;

	if (filter->prefix.family == p->family) {
		if (filter->exact) {
			if (filter->prefix.prefixlen == p->prefixlen)
				return prefix_match(&filter->prefix, p);
			else
				return 0;
		} else
			return prefix_match(&filter->prefix, p);
	} else
		return 0;
}

/*
 * access_list_new —— 分配并清零一条空的访问列表结构。
 * 只是分配，不插入链表、不设名字；名字与钩子由调用方填充。
 */
/* Allocate new access list structure. */
static struct access_list *access_list_new(void)
{
	return XCALLOC(MTYPE_ACCESS_LIST, sizeof(struct access_list));
}

/*
 * access_list_free —— 归还访问列表结构体本身的内存。
 * 不负责其规则链与名字串的释放（那些由 access_list_delete 处理）。
 */
/* Free allocated access_list. */
static void access_list_free(struct access_list *access)
{
	XFREE(MTYPE_ACCESS_LIST, access);
}

/*
 * access_list_delete —— 从 master 链中摘除一条访问列表并彻底释放。
 * 步骤：① 逐个 filter_free 清空规则链；② 从 master->str 双向链断开；
 *   ③ 通知 route-map 依赖已删除（RMAP_EVENT_FILTER_DELETED）；
 *   ④ 调用 master->delete_hook 让下游清理对该 ACL 的引用；
 *   ⑤ 释放 name / remark 与结构体本身。
 * 副作用：触发 route_map_notify_dependencies 与 delete_hook。
 * 注意：name 与 remark 用不同 MTYPE 分配，释放时别用错标签。
 */
/* Delete access_list from access_master and free it. */
void access_list_delete(struct access_list *access)
{
	struct filter *filter;
	struct filter *next;
	struct access_list_list *list;
	struct access_master *master;

	for (filter = access->head; filter; filter = next) {
		next = filter->next;
		filter_free(filter);
	}

	master = access->master;

	list = &master->str;

	if (access->next)
		access->next->prev = access->prev;
	else
		list->tail = access->prev;

	if (access->prev)
		access->prev->next = access->next;
	else
		list->head = access->next;

	route_map_notify_dependencies(access->name, RMAP_EVENT_FILTER_DELETED);

	if (master->delete_hook)
		master->delete_hook(access);

	XFREE(MTYPE_ACCESS_LIST_STR, access->name);

	XFREE(MTYPE_TMP, access->remark);

	access_list_free(access);
}

/*
 * access_list_insert —— 新建一条访问列表并按名字有序插入该 AFI 的链表。
 * 参数：afi 地址族；name 列表名（内部 XSTRDUP 保存）。
 * 返回：新插入的列表指针；master 不存在时返回 NULL。
 * 逻辑：先 access_list_new 建空表，再在 master->str 中按名字比较找插入点，
 *   分别处理「空表 / 尾部 / 头部 / 中间」四种情形，保持链表按名升序。
 */
/* Insert new access list to list of access_list.  Each access_list
   is sorted by the name. */
static struct access_list *access_list_insert(afi_t afi, const char *name)
{
	struct access_list *access;
	struct access_list *point;
	struct access_list_list *alist;
	struct access_master *master;

	master = access_master_get(afi);
	if (master == NULL)
		return NULL;

	/* Allocate new access_list and copy given name. */
	access = access_list_new();
	access->name = XSTRDUP(MTYPE_ACCESS_LIST_STR, name);
	access->master = master;

	/* Set access_list to string list. */
	alist = &master->str;

	/* Set point to insertion point. */
	for (point = alist->head; point; point = point->next)
		if (strcmp(point->name, name) >= 0)
			break;

	/* In case of this is the first element of master. */
	if (alist->head == NULL) {
		alist->head = alist->tail = access;
		return access;
	}

	/* In case of insertion is made at the tail of access_list. */
	if (point == NULL) {
		access->prev = alist->tail;
		alist->tail->next = access;
		alist->tail = access;
		return access;
	}

	/* In case of insertion is made at the head of access_list. */
	if (point == alist->head) {
		access->next = alist->head;
		alist->head->prev = access;
		alist->head = access;
		return access;
	}

	/* Insertion is made at middle of the access_list. */
	access->next = point;
	access->prev = point->prev;

	if (point->prev)
		point->prev->next = access;
	point->prev = access;

	return access;
}

/*
 * access_list_lookup —— 按名字在指定 AFI 的列表中查找。
 * 返回：匹配的列表指针；name 为 NULL / master 不存在 / 未找到，均返回 NULL。
 * 副作用：无，纯只读遍历。
 */
/* Lookup access_list from list of access_list by name. */
struct access_list *access_list_lookup(afi_t afi, const char *name)
{
	struct access_list *access;
	struct access_master *master;

	if (name == NULL)
		return NULL;

	master = access_master_get(afi);
	if (master == NULL)
		return NULL;

	for (access = master->str.head; access; access = access->next)
		if (strcmp(access->name, name) == 0)
			return access;

	return NULL;
}

/*
 * access_list_get —— 查找访问列表，找不到就新建。
 * 返回：已存在或新建的列表指针（写路径），等价于 lookup 失败则 insert。
 * 注意：这是配置增量的常用入口，会真实修改 master 链表。
 */
/* Get access list from list of access_list.  If there isn't matched
   access_list create new one and return it. */
struct access_list *access_list_get(afi_t afi, const char *name)
{
	struct access_list *access;

	access = access_list_lookup(afi, name);
	if (access == NULL)
		access = access_list_insert(afi, name);
	return access;
}

/*
 * access_list_apply —— 用整张访问列表匹配一个对象（通常为 struct prefix）。
 * 参数：access 列表；object 待匹配对象，函数内按 struct prefix 解读。
 * 返回：命中第一条规则的动作（permit / deny）；未命中或 access 为 NULL
 *   都返回 FILTER_DENY（隐式拒绝）。
 * 逻辑：从头遍历规则，cisco 规则走 filter_match_cisco，
 *   zebra 规则走 filter_match_zebra，谁先命中就返回谁的 type（首条命中生效）。
 * 坑：object 必须是合法的 struct prefix，否则按类型解释会读错内存。
 */
/* Apply access list to object (which should be struct prefix *). */
enum filter_type access_list_apply(struct access_list *access,
				   const void *object)
{
	struct filter *filter;
	const struct prefix *p = (const struct prefix *)object;

	if (access == NULL)
		return FILTER_DENY;

	for (filter = access->head; filter; filter = filter->next) {
		if (filter->cisco) {
			if (filter_match_cisco(filter, p))
				return filter->type;
		} else {
			if (filter_match_zebra(filter, p))
				return filter->type;
		}
	}

	return FILTER_DENY;
}

/*
 * access_list_apply_sadr —— 用整张列表按「源 + 目的」匹配（扩展 ACL 用）。
 * 参数：src / dst 源与目的前缀；match 为可选出参，命中时回填命中规则指针。
 * 返回：命中动作（permit / deny）；未命中或 access 为 NULL 返回 FILTER_DENY。
 * 逻辑：逐条规则，cisco 走 filter_match_cisco_sadr，zebra 走
 *   filter_match_zebra_sadr（后者只关心 dst），首条命中即返回。
 */
enum filter_type access_list_apply_sadr(struct access_list *access, union prefixconstptr src,
					union prefixconstptr dst, struct filter **match)
{
	struct filter *filter;

	if (access == NULL)
		return FILTER_DENY;

	for (filter = access->head; filter; filter = filter->next) {
		if (filter->cisco) {
			if (filter_match_cisco_sadr(filter, src.p, dst.p)) {
				if (match)
					*match = filter;

				return filter->type;
			}
		} else {
			if (filter_match_zebra_sadr(filter, src.p, dst.p)) {
				if (match)
					*match = filter;

				return filter->type;
			}
		}
	}

	return FILTER_DENY;
}

/*
 * access_list_add_hook —— 给三个 master 统一注册“新增列表”回调。
 * 注意：同一个回调会同时注册到 IPv4 / IPv6 / MAC 三个 master，
 *   回调内部需按 access->master 自行区分地址族。
 */
/* Add hook function. */
void access_list_add_hook(void (*func)(struct access_list *access))
{
	access_master_ipv4.add_hook = func;
	access_master_ipv6.add_hook = func;
	access_master_mac.add_hook = func;
}

/*
 * access_list_delete_hook —— 给三个 master 统一注册“删除列表”回调。
 * 与 add_hook 一样同时作用于 IPv4 / IPv6 / MAC 三个地址族。
 */
/* Delete hook function. */
void access_list_delete_hook(void (*func)(struct access_list *access))
{
	access_master_ipv4.delete_hook = func;
	access_master_ipv6.delete_hook = func;
	access_master_mac.delete_hook = func;
}

/*
 * filter_new_seq_get —— 为一条列表计算下一个可用序号。
 * 返回：当前最大序号“向上取整到 5 的倍数再加 5”；超过 UINT_MAX 时封顶 UINT_MAX。
 * 逻辑：遍历求出 maxseq，令 newseq = (maxseq / 5) * 5 + 5，即按 5 递增分配，
 *   给手工插入序号留出空档。
 */
/* Calculate new sequential number. */
int64_t filter_new_seq_get(struct access_list *access)
{
	int64_t maxseq;
	int64_t newseq;
	struct filter *filter;

	maxseq = 0;

	for (filter = access->head; filter; filter = filter->next) {
		if (maxseq < filter->seq)
			maxseq = filter->seq;
	}

	newseq = ((maxseq / 5) * 5) + 5;

	return (newseq > UINT_MAX) ? UINT_MAX : newseq;
}

/*
 * filter_seq_check —— 查找列表中序号等于 seq 的规则。
 * 返回：命中的规则指针；没有则 NULL。
 * 用途：插入新规则时若序号冲突，用它定位旧规则以便先删后插。
 */
/* Return access list entry which has same seq number. */
static struct filter *filter_seq_check(struct access_list *access,
						  int64_t seq)
{
	struct filter *filter;

	for (filter = access->head; filter; filter = filter->next)
		if (filter->seq == seq)
			return filter;
	return NULL;
}

/*
 * access_list_filter_delete —— 从指定列表中摘除并释放一条规则。
 * 步骤：从双向链断开该规则 -> filter_free -> 通知 route-map 依赖变化
 *   -> 调用 master->delete_hook。
 * 副作用：触发 route_map_notify_dependencies 与 delete_hook。
 */
/* Delete filter from specified access_list.  If there is hook
   function execute it. */
void access_list_filter_delete(struct access_list *access,
			       struct filter *filter)
{
	struct access_master *master;

	master = access->master;

	if (filter->next)
		filter->next->prev = filter->prev;
	else
		access->tail = filter->prev;

	if (filter->prev)
		filter->prev->next = filter->next;
	else
		access->head = filter->next;

	filter_free(filter);

	route_map_notify_dependencies(access->name, RMAP_EVENT_FILTER_DELETED);
	/* Run hook function. */
	if (master->delete_hook)
		(*master->delete_hook)(access);
}

/*
 * access_list_filter_add —— 把一条规则加入指定访问列表（按 seq 有序）。
 * 逻辑：seq 为 -1 时自动分配（filter_new_seq_get）；若已存在相同 seq 的规则，
 *   先 access_list_filter_delete 掉旧规则；然后在链表中找到第一个 seq 更大的
 *   位置插入，保持整表按序号升序。
 * 副作用：可能触发一次 access_list_filter_delete。
 * 坑：自动分配按 5 递增，注意与现有手工序号的关系。
 */
/* Add new filter to the end of specified access_list. */
void access_list_filter_add(struct access_list *access,
			    struct filter *filter)
{
	struct filter *replace;
	struct filter *point;

	/* Automatic assignment of seq no. */
	if (filter->seq == -1)
		filter->seq = filter_new_seq_get(access);

	if (access->tail && filter->seq > access->tail->seq)
		point = NULL;
	else {
		/* Is there any same seq access list filter? */
		replace = filter_seq_check(access, filter->seq);
		if (replace)
			access_list_filter_delete(access, replace);

		/* Check insert point. */
		for (point = access->head; point; point = point->next)
			if (point->seq >= filter->seq)
				break;
	}

	/* In case of this is the first element of the list. */
	filter->next = point;

	if (point) {
		if (point->prev)
			point->prev->next = filter;
		else
			access->head = filter;

		filter->prev = point->prev;
		point->prev = filter;
	} else {
		if (access->tail)
			access->tail->next = filter;
		else
			access->head = filter;

		filter->prev = access->tail;
		access->tail = filter;
	}
}

/*
 * access_list_filter_update —— 规则增删后同步外部依赖。
 * 逻辑：若 master 注册了 add_hook 就调用它，随后通知 route-map
 *   该 ACL 已被修改（RMAP_EVENT_FILTER_ADDED），促使相关路由重算。
 * 副作用：触发 add_hook 与 route_map_notify_dependencies。
 */
void access_list_filter_update(struct access_list *access)
{
	/* Run hook function. */
	if (access->master->add_hook)
		(*access->master->add_hook)(access);
	route_map_notify_dependencies(access->name, RMAP_EVENT_FILTER_ADDED);
}

/*
 * 下面两条注释块是历史遗留的说明文字（原样保留、未改动）：
 * 上一条解释 permit / deny / dynamic 三种动作的含义，
 * 下一条解释地址匹配的三种书写形式（具体地址串 / any / host）。
 */

/*
  deny    Specify packets to reject
  permit  Specify packets to forward
  dynamic ?
*/

/*
  Hostname or A.B.C.D  Address to match
  any                  Any source host
  host                 A single host address
*/

/*
 * 两个静态渲染函数的前置声明：分别负责 zebra 风格与 cisco 风格的
 * 单条规则输出（函数体定义在本文件后面）。
 */
static void config_write_access_zebra(struct vty *, struct filter *,
				      json_object *);
static void config_write_access_cisco(struct vty *, struct filter *,
				      json_object *);

/*
 * filter_type2str —— 返回规则的类型标识字符串，用于 show 表头。
 * 返回：cisco 扩展规则 "Extended"；cisco 标准规则 "Standard"；否则 "Zebra"。
 * 注意：它与 filter_type_str（返回 permit / deny）用途不同，别混淆。
 */
static const char *filter_type2str(struct filter *filter)
{
	if (filter->cisco) {
		if (filter->u.cfilter.extended)
			return "Extended";
		else
			return "Standard";
	} else
		return "Zebra";
}

/*
 * filter_show —— 显示访问列表内容（show xxx access-list 的实现核心）。
 * 参数：name 为 NULL 时显示该 AFI 全部列表，否则只显示同名列表；
 *   afi 指定地址族；use_json 决定输出文本还是 JSON。
 * 返回：vty_json 的结果（JSON 模式用于收尾输出）。
 * 逻辑：遍历 master 的列表，每张表首次出现时打印表头（类型 + 名字），
 *   之后逐条打印 seq / 动作 / 匹配条件；cisco 标准 / 扩展与 zebra 分别走
 *   config_write_access_* 渲染。该 AFI 无列表且 JSON 模式时输出空对象。
 * 坑：filter 指针被无条件指向 cfilter，仅在非 zebra 分支真正使用。
 */
/* show access-list command. */
static int filter_show(struct vty *vty, const char *name, afi_t afi,
		       bool use_json)
{
	struct access_list *access;
	struct access_master *master;
	struct filter *mfilter;
	struct filter_cisco *filter;
	bool first;
	json_object *json = NULL;

	master = access_master_get(afi);
	if (master == NULL) {
		if (use_json)
			vty_out(vty, "{}\n");
		return 0;
	}

	if (use_json)
		json = json_object_new_object();
	else
		vty_out(vty, "%s:\n", frr_protoname);

	for (access = master->str.head; access; access = access->next) {
		json_object *json_acl = NULL;
		json_object *json_rules = NULL;

		if (name && strcmp(access->name, name) != 0)
			continue;

		first = true;

		for (mfilter = access->head; mfilter; mfilter = mfilter->next) {
			json_object *json_rule = NULL;

			filter = &mfilter->u.cfilter;

			if (first) {
				const char *type = filter_type2str(mfilter);

				if (json) {
					json_acl = json_object_new_object();
					json_object_object_add(json,
							       access->name,
							       json_acl);

					json_object_string_add(json_acl, "type",
							       type);
					json_object_string_add(json_acl,
							       "addressFamily",
							       afi2str(afi));
					json_rules = json_object_new_array();
					json_object_object_add(
						json_acl, "rules", json_rules);
				} else {
					vty_out(vty, "%s %s access list %s\n",
						type,
						(afi == AFI_IP)
							? ("IP")
							: ((afi == AFI_IP6)
								   ? ("IPv6 ")
								   : ("MAC ")),
						access->name);
				}

				first = false;
			}

			if (json) {
				json_rule = json_object_new_object();
				json_object_array_add(json_rules, json_rule);

				json_object_int_add(json_rule, "sequenceNumber",
						    mfilter->seq);
				json_object_string_add(
					json_rule, "filterType",
					filter_type_str(mfilter));
			} else {
				vty_out(vty, "    seq %" PRId64, mfilter->seq);
				vty_out(vty, " %s%s", filter_type_str(mfilter),
					mfilter->type == FILTER_DENY ? "  "
								     : "");
			}

			if (!mfilter->cisco)
				config_write_access_zebra(vty, mfilter,
							  json_rule);
			else if (filter->extended)
				config_write_access_cisco(vty, mfilter,
							  json_rule);
			else {
				if (json) {
					json_object_string_addf(
						json_rule, "address", "%pI4",
						&filter->addr);
					json_object_string_addf(
						json_rule, "mask", "%pI4",
						&filter->addr_mask);
				} else {
					if (filter->addr_mask.s_addr
					    == 0xffffffff)
						vty_out(vty, " any\n");
					else {
						vty_out(vty, " %pI4",
							&filter->addr);
						if (filter->addr_mask.s_addr
						    != INADDR_ANY)
							vty_out(vty,
								", wildcard bits %pI4",
								&filter->addr_mask);
						vty_out(vty, "\n");
					}
				}
			}
		}
	}

	return vty_json(vty, json);
}

/*
 * show_mac_access_list —— "show mac access-list"：列出全部 MAC 访问列表。
 * 直接调用 filter_show（非 JSON），afi = AFI_L2VPN。
 */
/* show MAC access list - this only has MAC filters for now*/
DEFUN (show_mac_access_list,
       show_mac_access_list_cmd,
       "show mac access-list",
       SHOW_STR
       "mac access lists\n"
       "List mac access lists\n")
{
	return filter_show(vty, NULL, AFI_L2VPN, false);
}

/*
 * show_mac_access_list_name —— "show mac access-list NAME"：只看指定 MAC 列表。
 * 列表名取自 argv[3]。
 */
DEFUN (show_mac_access_list_name,
       show_mac_access_list_name_cmd,
       "show mac access-list ACCESSLIST_MAC_NAME",
       SHOW_STR
       "mac access lists\n"
       "List mac access lists\n"
       "mac address\n")
{
	return filter_show(vty, argv[3]->arg, AFI_L2VPN, false);
}

/*
 * show_ip_access_list —— "show ip access-list [json]"：列出全部 IPv4 访问列表。
 * 通过 use_json(argc, argv) 判断是否输出 JSON。
 */
DEFUN_NOSH (show_ip_access_list,
       show_ip_access_list_cmd,
       "show ip access-list [json]",
       SHOW_STR
       IP_STR
       "List IP access lists\n"
       JSON_STR)
{
	bool uj = use_json(argc, argv);
	return filter_show(vty, NULL, AFI_IP, uj);
}

/*
 * show_ip_access_list_name —— "show ip access-list NAME [json]"：只看指定 IPv4 列表。
 * 名字取自 argv[3]。
 */
DEFUN_NOSH (show_ip_access_list_name,
       show_ip_access_list_name_cmd,
       "show ip access-list ACCESSLIST4_NAME [json]",
       SHOW_STR
       IP_STR
       "List IP access lists\n"
       "IP access-list name\n"
       JSON_STR)
{
	bool uj = use_json(argc, argv);
	int idx_acl = 3;
	return filter_show(vty, argv[idx_acl]->arg, AFI_IP, uj);
}

/*
 * show_ipv6_access_list —— "show ipv6 access-list [json]"：列出全部 IPv6 访问列表。
 */
DEFUN_NOSH (show_ipv6_access_list,
       show_ipv6_access_list_cmd,
       "show ipv6 access-list [json]",
       SHOW_STR
       IPV6_STR
       "List IPv6 access lists\n"
       JSON_STR)
{
	bool uj = use_json(argc, argv);
	return filter_show(vty, NULL, AFI_IP6, uj);
}

/*
 * show_ipv6_access_list_name —— "show ipv6 access-list NAME [json]"：只看指定 IPv6 列表。
 * 名字取自 argv[3]。
 */
DEFUN_NOSH (show_ipv6_access_list_name,
       show_ipv6_access_list_name_cmd,
       "show ipv6 access-list ACCESSLIST6_NAME [json]",
       SHOW_STR
       IPV6_STR
       "List IPv6 access lists\n"
       "IPv6 access-list name\n"
       JSON_STR)
{
	bool uj = use_json(argc, argv);
	int idx_word = 3;
	return filter_show(vty, argv[idx_word]->arg, AFI_IP6, uj);
}

/*
 * config_write_access_cisco —— 渲染一条 cisco 风格（扩展）规则的匹配条件。
 * 参数：json 非空则写 JSON 字段，否则打印文本。
 * 逻辑：IPv6 规则用 sadr6 的源 / 目的地址与掩码；IPv4 规则用 wtf.addr /
 *   wtf.addr_mask 作源，wtf.mask / wtf.mask_mask 作目的。
 *   文本模式下按“any / host x / 地址+通配符”三种形式择一输出。
 * 说明：仅处理 extended 的 cisco 规则，标准规则由 filter_show 内联输出。
 * 坑：IPv4 分支借用了 wtf 字段来存源 / 目的，命名有误导性。
 */
static void config_write_access_cisco(struct vty *vty, struct filter *mfilter,
				      json_object *json)
{
	struct filter_cisco *filter;

	filter = &mfilter->u.cfilter;

	if (json) {
		json_object_boolean_add(json, "extended", !!filter->extended);
		if (filter->ipv6) {
			json_object_string_addf(json, "sourceAddress", "%pI6", &filter->sadr6.src);
			json_object_string_addf(json, "sourceMask", "%pI6",
						&filter->sadr6.src_mask);
			json_object_string_addf(json, "destinationAddress", "%pI6",
						&filter->sadr6.dst);
			json_object_string_addf(json, "destinationMask", "%pI6",
						&filter->sadr6.dst_mask);
		} else {
			json_object_string_addf(json, "sourceAddress", "%pI4", &filter->addr);
			json_object_string_addf(json, "sourceMask", "%pI4", &filter->addr_mask);
			json_object_string_addf(json, "destinationAddress", "%pI4",
						&filter->wtf.mask);
			json_object_string_addf(json, "destinationMask", "%pI4",
						&filter->wtf.mask_mask);
		}
	} else {
		if (filter->ipv6) {
			vty_out(vty, " ipv6");
			if (memcmp(&filter->sadr6.src, &in6_mask_all, sizeof(filter->sadr6.src)) ==
			    0)
				vty_out(vty, " any");
			else if (memcmp(&filter->sadr6.src_mask, &in6addr_any,
					sizeof(filter->sadr6.src_mask)) == 0)
				vty_out(vty, " host %pI6", &filter->sadr6.src);
			else {
				vty_out(vty, " %pI6", &filter->sadr6.src);
				vty_out(vty, " %pI6", &filter->sadr6.src_mask);
			}

			if (memcmp(&filter->sadr6.dst_mask, &in6_mask_all,
				   sizeof(filter->sadr6.dst_mask)) == 0)
				vty_out(vty, " any");
			else if (memcmp(&filter->sadr6.dst_mask, &in6addr_any,
					sizeof(filter->sadr6.dst_mask)) == 0)
				vty_out(vty, " host %pI6", &filter->sadr6.dst);
			else {
				vty_out(vty, " %pI6", &filter->sadr6.dst);
				vty_out(vty, " %pI6", &filter->sadr6.dst_mask);
			}
		} else {
			vty_out(vty, " ip");
			if (filter->wtf.addr_mask.s_addr == 0xffffffff)
				vty_out(vty, " any");
			else if (filter->wtf.addr_mask.s_addr == INADDR_ANY)
				vty_out(vty, " host %pI4", &filter->wtf.addr);
			else {
				vty_out(vty, " %pI4", &filter->wtf.addr);
				vty_out(vty, " %pI4", &filter->wtf.addr_mask);
			}

			if (filter->wtf.mask_mask.s_addr == 0xffffffff)
				vty_out(vty, " any");
			else if (filter->wtf.mask_mask.s_addr == INADDR_ANY)
				vty_out(vty, " host %pI4", &filter->wtf.mask);
			else {
				vty_out(vty, " %pI4", &filter->wtf.mask);
				vty_out(vty, " %pI4", &filter->wtf.mask_mask);
			}
		}
		vty_out(vty, "\n");
	}
}

/*
 * config_write_access_zebra —— 渲染一条 zebra（前缀列表）风格规则的匹配条件。
 * 参数：json 非空则写 JSON（prefix / exact-match），否则打印文本。
 * 逻辑：prefixlen 为 0 且非 exact 时输出 " any"；IPv4 / IPv6 前缀用 %pFX，
 *   exact 时追加 " exact-match"；以太网（MAC）前缀单独处理。
 * 说明：与 config_write_access_cisco 并列，由 filter_show 按规则类型选择。
 */
static void config_write_access_zebra(struct vty *vty, struct filter *mfilter,
				      json_object *json)
{
	struct filter_zebra *filter;
	struct prefix *p;
	char buf[BUFSIZ];

	filter = &mfilter->u.zfilter;
	p = &filter->prefix;

	if (json) {
		json_object_string_addf(json, "prefix", "%pFX", p);
		json_object_boolean_add(json, "exact-match", !!filter->exact);
	} else {
		if (p->prefixlen == 0 && !filter->exact)
			vty_out(vty, " any");
		else if (p->family == AF_INET6 || p->family == AF_INET)
			vty_out(vty, " %pFX%s", p,
				filter->exact ? " exact-match" : "");
		else if (p->family == AF_ETHERNET) {
			if (p->prefixlen == 0)
				vty_out(vty, " any");
			else
				vty_out(vty, " %s",
					prefix_mac2str(&(p->u.prefix_eth), buf,
						       sizeof(buf)));
		}

		vty_out(vty, "\n");
	}
}

/*
 * access_mac_node —— MAC 访问列表的 CLI 节点定义（空提示符、无 config_write）。
 * 仅用于把 show mac access-list 等命令挂到该节点下。
 */
static struct cmd_node access_mac_node = {
	.name = "MAC access list",
	.node = ACCESS_MAC_NODE,
	.prompt = "",
};

/*
 * access_list_reset_mac —— 清空所有 MAC 访问列表。
 * 逻辑：遍历 master 的列表逐个 access_list_delete（它会摘链），
 *   因此先用 next 保存后继；结束后断言链表确实已清空。
 */
static void access_list_reset_mac(void)
{
	struct access_list *access;
	struct access_list *next;
	struct access_master *master;

	master = access_master_get(AFI_L2VPN);
	if (master == NULL)
		return;

	for (access = master->str.head; access; access = next) {
		next = access->next;
		access_list_delete(access);
	}

	assert(master->str.head == NULL);
	assert(master->str.tail == NULL);
}

/*
 * access_list_init_mac —— 注册 MAC 访问列表的节点与命令。
 * 在 ENABLE_NODE 下挂 show mac access-list 及带名字的版本。
 */
/* Install vty related command. */
static void access_list_init_mac(void)
{
	install_node(&access_mac_node);

	install_element(ENABLE_NODE, &show_mac_access_list_cmd);
	install_element(ENABLE_NODE, &show_mac_access_list_name_cmd);
}

/*
 * access_node —— IPv4 访问列表的 CLI 节点，注册了 config_write 回调。
 * config_write_access 负责把运行配置里 frr-filter:lib 子树展开成 CLI。
 */
/* Access-list node. */
static int config_write_access(struct vty *vty);
static struct cmd_node access_node = {
	.name = "ipv4 access list",
	.node = ACCESS_NODE,
	.prompt = "",
	.config_write = config_write_access,
};

/*
 * config_write_access —— ACCESS_NODE 的配置写回调（node->config_write）。
 * 逻辑：从运行配置数据树取 /frr-filter:lib 子树，存在则用
 *   nb_cli_show_dnode_cmds 展开为 CLI 文本；返回是否写出内容（written）。
 * 说明：实际命令文本由 YANG 显示回调（filter_cli.c）决定，本函数只负责触发。
 */
static int config_write_access(struct vty *vty)
{
	struct lyd_node *dnode;
	int written = 0;

	dnode = yang_dnode_get(running_config->dnode, "/frr-filter:lib");
	if (dnode) {
		nb_cli_show_dnode_cmds(vty, dnode, false);
		written = 1;
	}

	return written;
}

/*
 * access_list_reset_ipv4 —— 清空所有 IPv4 访问列表（做法同 reset_mac）。
 */
static void access_list_reset_ipv4(void)
{
	struct access_list *access;
	struct access_list *next;
	struct access_master *master;

	master = access_master_get(AFI_IP);
	if (master == NULL)
		return;

	for (access = master->str.head; access; access = next) {
		next = access->next;
		access_list_delete(access);
	}

	assert(master->str.head == NULL);
	assert(master->str.tail == NULL);
}

/*
 * access_list_init_ipv4 —— 注册 IPv4 访问列表的节点与命令（ACCESS_NODE）。
 */
/* Install vty related command. */
static void access_list_init_ipv4(void)
{
	install_node(&access_node);

	install_element(ENABLE_NODE, &show_ip_access_list_cmd);
	install_element(ENABLE_NODE, &show_ip_access_list_name_cmd);
}

/*
 * access_list_autocomplete_afi —— 把指定 AFI 下所有列表名加入补全候选表。
 * 参数：comps 补全向量（用 vector_set 追加）；token 当前命令记号（未使用）。
 * 说明：补全项是 XSTRDUP 出的副本，之后交给补全框架释放。
 */
static void access_list_autocomplete_afi(afi_t afi, vector comps,
					 struct cmd_token *token)
{
	struct access_list *access;
	struct access_list *next;
	struct access_master *master;

	master = access_master_get(afi);
	if (master == NULL)
		return;

	for (access = master->str.head; access; access = next) {
		next = access->next;
		vector_set(comps, XSTRDUP(MTYPE_COMPLETION, access->name));
	}
}

/*
 * access_ipv6_node —— IPv6 访问列表的 CLI 节点定义（无 config_write）。
 */
static struct cmd_node access_ipv6_node = {
	.name = "ipv6 access list",
	.node = ACCESS_IPV6_NODE,
	.prompt = "",
};

/*
 * access_list_autocomplete —— ACCESSLIST_NAME 的补全：聚合 IPv4 / IPv6 / MAC。
 */
static void access_list_autocomplete(vector comps, struct cmd_token *token)
{
	access_list_autocomplete_afi(AFI_IP, comps, token);
	access_list_autocomplete_afi(AFI_IP6, comps, token);
	access_list_autocomplete_afi(AFI_L2VPN, comps, token);
}

/*
 * access_list4_autocomplete —— ACCESSLIST4_NAME 的补全：仅 IPv4 列表。
 */
static void access_list4_autocomplete(vector comps, struct cmd_token *token)
{
	access_list_autocomplete_afi(AFI_IP, comps, token);
}

/*
 * access_list6_autocomplete —— ACCESSLIST6_NAME 的补全：仅 IPv6 列表。
 */
static void access_list6_autocomplete(vector comps, struct cmd_token *token)
{
	access_list_autocomplete_afi(AFI_IP6, comps, token);
}

/*
 * access_list_mac_autocomplete —— ACCESSLIST_MAC_NAME 的补全：仅 MAC 列表。
 */
static void access_list_mac_autocomplete(vector comps, struct cmd_token *token)
{
	access_list_autocomplete_afi(AFI_L2VPN, comps, token);
}

/*
 * access_list_handlers —— CLI 变量补全处理器表，把各 token 名映射到补全函数。
 * 末尾 {.completions = NULL} 是结束哨兵，不可省略。
 */
static const struct cmd_variable_handler access_list_handlers[] = {
	{.tokenname = "ACCESSLIST_NAME",
	 .completions = access_list_autocomplete},
	{.tokenname = "ACCESSLIST4_NAME",
	 .completions = access_list4_autocomplete},
	{.tokenname = "ACCESSLIST6_NAME",
	 .completions = access_list6_autocomplete},
	{.tokenname = "ACCESSLIST_MAC_NAME",
	 .completions = access_list_mac_autocomplete},
	{.completions = NULL}};

/*
 * access_list_reset_ipv6 —— 清空所有 IPv6 访问列表（做法同 reset_mac / reset_ipv4）。
 */
static void access_list_reset_ipv6(void)
{
	struct access_list *access;
	struct access_list *next;
	struct access_master *master;

	master = access_master_get(AFI_IP6);
	if (master == NULL)
		return;

	for (access = master->str.head; access; access = next) {
		next = access->next;
		access_list_delete(access);
	}

	assert(master->str.head == NULL);
	assert(master->str.tail == NULL);
}

/*
 * access_list_init_ipv6 —— 注册 IPv6 访问列表的节点与命令。
 */
static void access_list_init_ipv6(void)
{
	install_node(&access_ipv6_node);

	install_element(ENABLE_NODE, &show_ipv6_access_list_cmd);
	install_element(ENABLE_NODE, &show_ipv6_access_list_name_cmd);
}

/*
 * access_list_init_new —— 访问列表子系统的总初始化。
 * 参数：in_backend 为真表示运行在独立后端进程，此时不装载配置类命令，
 *   只保留 show 类命令（后端不处理 config 命令）。
 * 逻辑：注册补全处理器 -> 初始化 IPv4 / IPv6 / MAC -> 非后端时再调用
 *   filter_cli_init 装载 YANG 配置命令。
 */
void access_list_init_new(bool in_backend)
{
	cmd_variable_handler_register(access_list_handlers);

	access_list_init_ipv4();
	access_list_init_ipv6();
	access_list_init_mac();

	if (!in_backend) {
		/* we do not want to handle config commands in the backend */
		filter_cli_init();
	}
}

/*
 * access_list_init —— 对外默认初始化入口，等价于 access_list_init_new(false)。
 */
void access_list_init(void)
{
	access_list_init_new(false);
}

/*
 * access_list_reset —— 对外统一的重置入口：依次清空 IPv4 / IPv6 / MAC
 *   三个地址族下的所有访问列表（内部调用各 reset_* helper）。
 * 常用于进程退出或配置整体重载。
 */
void access_list_reset(void)
{
	access_list_reset_ipv4();
	access_list_reset_ipv6();
	access_list_reset_mac();
}
