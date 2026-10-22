// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Route filtering function.
 * Copyright (C) 1998 Kunihiro Ishiguro
 */

/*
 * 文件导读 filter.h —— 路由过滤框架的公共数据结构头文件（lib 层）
 *
 * 定位：本文件只描述“过滤规则长什么样”，是 lib 路由过滤能力的对外声明；
 *   它本身不含匹配逻辑，也不含配置回调与命令实现。四者分工如下：
 *     - filter.h    ：数据结构 + 函数原型（本文件）；
 *     - filter.c    ：访问列表核心实现：创建 / 删除 / 查找 / 匹配；
 *     - filter_nb.c ：北向接口（YANG）回调，把 YANG 配置树落到本文件的结构；
 *     - filter_cli.c：CLI 命令、show 输出、比较回调。
 *
 * 关键数据结构层次（由小到大）：
 *     struct filter        单条规则（一个 permit / deny 条目）；
 *     struct access_list   一条命名访问列表，由若干 struct filter 组成；
 *     struct access_master 某地址族（AFI）下所有访问列表的总管。
 *
 * filter 用 union 承载两种风格的数据：
 *     filter_cisco  传统 Cisco 风格 ACL（host / any / 通配符掩码）；
 *     filter_zebra  前缀列表（prefix-list）风格，用 struct prefix 表达。
 *
 * 重要提醒：Cisco 通配符（wildcard）与子网掩码语义相反——
 *   通配符某位为 0 表示“该位必须相等”，为 1 表示“不关心”。
 *   阅读 filter.c 的匹配代码时务必区分 mask 与 wildcard。
 *
 * 本文件同时导出三组 YANG 相关枚举，供 filter_nb.c 在回调中使用。
 */

#ifndef _ZEBRA_FILTER_H
#define _ZEBRA_FILTER_H

#include "if.h"
#include "prefix.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ACL_NAMSIZ —— 访问列表 / 前缀列表名字的最大长度（含结尾 '\0'）。
 * 名字存放在 access_list->name（strdup 而来），超长输入在配置阶段被拒。
 * 注意它与 YANG 模型里的名字长度约束需保持一致，否则 NB 能配、CLI 不能配。
 */
/* Maximum ACL name length */
#define ACL_NAMSIZ                128

/*
 * Cisco 风格「主机」与「任意」通配符掩码字符串常量。
 * Cisco ACL 用的是通配符（wildcard）而非子网掩码：
 *   某位为 0 表示要求该位相等，为 1 表示该位不关心。
 *   host 对应全 0 通配符（精确匹配单个地址）；
 *   any  对应全 1 通配符（匹配所有地址）。
 * 下面四个宏分别给出字符串形式与二进制形式（BIN 版便于直接参与按位运算）。
 */
/** Cisco host wildcard mask. */
#define CISCO_HOST_WILDCARD_MASK  "0.0.0.0"
/** Cisco host wildcard binary mask. */
#define CISCO_BIN_HOST_WILDCARD_MASK INADDR_ANY

/** Cisco any wildcard mask. */
#define CISCO_ANY_WILDCARD_MASK   "255.255.255.255"
/** Cisco binary any wildcard mask. */
#define CISCO_BIN_ANY_WILDCARD_MASK INADDR_NONE

/*
 * 过滤方向：IN 作用于入方向，OUT 作用于出方向，MAX 作为相关数组的维度上界。
 * 很多模块会写成 filter[FILTER_MAX] 之类，用方向当数组下标，故顺序不可改。
 */
/* Filter direction.  */
#define FILTER_IN                 0
#define FILTER_OUT                1
#define FILTER_MAX                2

/*
 * 单条规则的动作：FILTER_DENY（拒绝）、FILTER_PERMIT（允许）、
 * FILTER_DYNAMIC（动态，主要供 BGP 等场景使用）。
 *
 * 返回约定：access_list_apply 系列最终只会返回 FILTER_PERMIT 或 FILTER_DENY；
 *   若整表遍历完都没有命中，约定返回 FILTER_DENY（即隐式拒绝 / implicit deny）。
 *   列表指针为 NULL 时同样返回 FILTER_DENY。
 */
/* Filter type is made by `permit', `deny' and `dynamic'. */
enum filter_type { FILTER_DENY, FILTER_PERMIT, FILTER_DYNAMIC };

/*
 * struct filter_cisco —— Cisco 风格 ACL 单条规则的匹配数据（IPv4 / IPv6 共用）。
 *   extended : 是否为扩展 ACL（可同时匹配源与目的，见 sadr / sadr6）；
 *   ipv6     : 该条目使用 IPv6 地址结构。
 * 联合体按匹配模式复用同一段内存：
 *   匿名结构 (addr / addr_mask)   标准 ACL 的源地址匹配；
 *   addr6 / addr6_mask            标准 ACL 的 IPv6 源地址匹配；
 *   wtf（“伪前缀列表模式”）       把 ACL 当 prefix-list 用时，额外带掩码；
 *   sadr / sadr6                  扩展 ACL 的源 / 目的地址匹配。
 * 坑：这些成员是匿名的，直接写 f->u.cfilter.addr 访问，不要当成嵌套字段。
 */
struct filter_cisco {
	/* Cisco access-list */
	int extended;
	/* Use IPv6 */
	bool ipv6;

	union {
		/* basic */
		struct {
			struct in_addr addr;
			struct in_addr addr_mask;
		};
		/* basic IPv6 */
		struct {
			struct in6_addr addr6;
			struct in6_addr addr6_mask;
		};
		/* "pseudo-prefix-list-mode", aka WTF mode */
		struct {
			struct in_addr addr;
			struct in_addr addr_mask;
			struct in_addr mask;
			struct in_addr mask_mask;
		} wtf;
		/* source / destination address matching */
		struct {
			struct in_addr src;
			struct in_addr src_mask;
			struct in_addr dst;
			struct in_addr dst_mask;
		} sadr;
		/* source /destination IPv6 address matching */
		struct {
			struct in6_addr src;
			struct in6_addr src_mask;
			struct in6_addr dst;
			struct in6_addr dst_mask;
		} sadr6;
	};
};

/*
 * struct filter_zebra —— 前缀列表（prefix-list）风格的匹配数据。
 *   exact  : 非 0 表示要求精确匹配（只匹配该前缀本身，不接受更长前缀）；
 *   prefix : 前缀本体，匹配时还会结合列表项的 ge / le 长度约束一起判断。
 * 说明：zebra 风格的条目由 filter.c 中的前缀匹配例程消费。
 */
struct filter_zebra {
	/* If this filter is "exact" match then this flag is set. */
	int exact;

	/* Prefix information. */
	struct prefix prefix;
};

/* Forward declaration of access-list struct. */
struct access_list;

/*
 * struct filter —— 访问列表中的「一条规则」，同时是双向链表的节点。
 *   next / prev : 在所属 access_list 内的双向链；
 *   acl         : 反向指针，指回父访问列表；
 *   type        : 动作（permit / deny / dynamic）；
 *   seq         : 序号，即 CLI 里 seq N 的 N，用于排序与按序插入；
 *   cisco       : 非 0 表示 Cisco 风格规则，从而决定 union 取哪一支；
 *   u.cfilter / u.zfilter : 两种风格各自的数据载体。
 * 生命周期：由 filter_new() 分配，挂入 access_list 后交由链表管理。
 */
/* Filter element of access list */
struct filter {
	/* For doubly linked list. */
	struct filter *next;
	struct filter *prev;

	/* Parent access-list pointer. */
	struct access_list *acl;

	/* Filter type information. */
	enum filter_type type;

	/* Sequence number */
	int64_t seq;

	/* Cisco access-list */
	int cisco;

	union {
		struct filter_cisco cfilter;
		struct filter_zebra zfilter;
	} u;
};

/*
 * struct access_list —— 一条命名的访问列表 / 前缀列表。
 *   name / remark : 列表名与用户备注（remark）；
 *   master        : 指向所属地址族的总管（access_master），提供 add / delete 钩子；
 *   next / prev   : 在同一 AFI 的列表链中串联；
 *   head / tail   : 规则（struct filter）双向链表的头尾。
 * 每个 AFI（IPv4 / IPv6 / MAC）各有一个 access_master 与各自的列表链。
 */
/* Access list */
struct access_list {
	char *name;
	char *remark;

	struct access_master *master;

	struct access_list *next;
	struct access_list *prev;

	struct filter *head;
	struct filter *tail;
};

/*
 * struct access_list_list —— 访问列表链表的头尾封装，纯粹便于头插 / 尾插。
 */
/* List of access_list. */
struct access_list_list {
	struct access_list *head;
	struct access_list *tail;
};

/*
 * struct access_master —— 一个地址族（AFI）下所有访问列表的总管。
 *   str         : 名字到列表的链表（当前实现均以字符串命名）；
 *   add_hook    : 新增列表时的回调，其它模块借此感知“出现了新 ACL”；
 *   delete_hook : 删除列表时的回调，用于清理引用了该 ACL 的下游状态。
 * 设计意图：用钩子把“列表增删”的副作用与 filter.c 解耦，
 *   使核心实现不必知道有哪些模块在引用访问列表。
 */
/* Master structure of access_list. */
struct access_master {
	/* List of access_list which name is string. */
	struct access_list_list str;

	/* Hook function which is executed when new access_list is added. */
	void (*add_hook)(struct access_list *);

	/* Hook function which is executed when access_list is deleted. */
	void (*delete_hook)(struct access_list *);
};


/*
 * 访问列表（access-list）对外接口总览：
 *   access_list_init / access_list_init_new : 初始化（new 版用于独立后端模式）；
 *   access_list_reset                       : 清空全部列表（重启配置 / 退出时）；
 *   access_list_add_hook / delete_hook      : 注册列表增删钩子；
 *   access_list_lookup                      : 按 AFI + 名字查找（找不到返回 NULL）；
 *   access_list_apply                       : 用整表匹配一个对象（IP / 前缀 / 字符串）；
 *   access_list_apply_sadr                  : 扩展 ACL 按「源 + 目的」匹配；
 *   access_list_get                         : 查找，找不到则创建（写路径）；
 *   access_list_delete                      : 删除整表；
 *   filter_new                              : 分配一条空规则；
 *   access_list_filter_add / delete / update: 规则级增删与内部整理；
 *   filter_new_seq_get                      : 生成下一个可用序号。
 * 注意：apply 系列返回 enum filter_type（PERMIT / DENY）。
 */
/* Prototypes for access-list. */
extern void access_list_init(void);
extern void access_list_init_new(bool in_backend);
extern void access_list_reset(void);
extern void access_list_add_hook(void (*func)(struct access_list *));
extern void access_list_delete_hook(void (*func)(struct access_list *));
extern struct access_list *access_list_lookup(afi_t afi, const char *name);
extern enum filter_type access_list_apply(struct access_list *access,
					  const void *object);
extern enum filter_type access_list_apply_sadr(struct access_list *access, union prefixconstptr src,
					       union prefixconstptr dst, struct filter **match);

struct access_list *access_list_get(afi_t afi, const char *name);
void access_list_delete(struct access_list *access);
struct filter *filter_new(void);
void access_list_filter_add(struct access_list *access, struct filter *filter);
void access_list_filter_delete(struct access_list *access,
			       struct filter *filter);
void access_list_filter_update(struct access_list *access);
int64_t filter_new_seq_get(struct access_list *access);

extern const struct frr_yang_module_info frr_filter_info;
extern const struct frr_yang_module_info frr_filter_cli_info;


/*
 * 以下三个枚举是 filter_nb.c 与 YANG 模型之间的“类型 / 动作”约定值，
 * 用于在 C 结构里表达 YANG leaf 的取值：
 *   yang_access_list_type   : 访问列表类型（IPv4 / IPv6 / MAC）；
 *   yang_prefix_list_type   : 前缀列表类型（IPv4 / IPv6）；
 *   yang_prefix_list_action : 前缀列表动作（deny / permit）。
 * 注意：这些是内部约定数值，并不等于 YANG 侧字符串本身；
 *   "ipv4" / "mac" 等字符串到数值的映射在 filter_nb.c 的辅助函数里完成。
 */
/* filter_nb.c */
enum yang_access_list_type {
	YALT_IPV4 = 0,
	YALT_IPV6 = 1,
	YALT_MAC = 2,
};

enum yang_prefix_list_type {
	YPLT_IPV4 = 0,
	YPLT_IPV6 = 1,
};

enum yang_prefix_list_action {
	YPLA_DENY = 0,
	YPLA_PERMIT = 1,
};

/*
 * struct acl_dup_args —— 访问列表查重（duplicate）检查的入参 / 出参打包。
 * 检查“候选配置里是否已存在同类同动作、且各匹配字段完全相同的重复条目”。
 *   ada_type / ada_name   : 目标列表的类型与名字；
 *   ada_action            : 条目动作（permit / deny）；
 *   ada_xpath / ada_value : 若干叶子（如源 / 目的 / 掩码）的 XPath 与期望取值，
 *                           最多 ADA_MAX_VALUES 组，逐项比对是否完全一致；
 *   ada_found / ada_seq   : 输出，是否命中重复项及其序号；
 *   ada_entry_dnode       : 可选，调用方若已知 entry 的 dnode 可传入复用。
 * 消费方为 acl_is_dup()。
 */
struct acl_dup_args {
	/** Access list type ("ipv4", "ipv6" or "mac"). */
	const char *ada_type;
	/** Access list name. */
	const char *ada_name;

	/** Entry action. */
	const char *ada_action;

#define ADA_MAX_VALUES 4
	/** Entry XPath for value. */
	const char *ada_xpath[ADA_MAX_VALUES];
	/** Entry value to match. */
	const char *ada_value[ADA_MAX_VALUES];

	/** Duplicated entry found in list? */
	bool ada_found;

	/** Sequence number of the found entry */
	int64_t ada_seq;

	/** (Optional) Already existing `dnode`. */
	const struct lyd_node *ada_entry_dnode;
};

/**
 * Check for duplicated entries using the candidate configuration.
 *
 * \param vty so we can get the candidate config.
 * \param ada the arguments to check.
 */
bool acl_is_dup(const struct lyd_node *dnode, struct acl_dup_args *ada);

/*
 * struct plist_dup_args —— 前缀列表查重（duplicate）检查的入参 / 出参打包。
 *   pda_type / pda_name : 目标列表的类型与名字；
 *   pda_action          : 动作（deny / permit）；
 *   any                 : 是否匹配任意前缀（对应 prefix any）；
 *   prefix / ge / le    : 前缀本体及 ge / le 长度约束；
 *   pda_found / pda_seq : 输出，是否命中重复项及其序号；
 *   pda_entry_dnode     : 可选，已有的 entry dnode。
 * 消费方为 plist_is_dup()。
 */
struct plist_dup_args {
	/** Access list type ("ipv4" or "ipv6"). */
	const char *pda_type;
	/** Access list name. */
	const char *pda_name;

	/** Entry action. */
	const char *pda_action;

	bool any;
	struct prefix prefix;
	int ge;
	int le;

	/** Duplicated entry found in list? */
	bool pda_found;

	/** Sequence number of the found entry */
	int64_t pda_seq;

	/** (Optional) Already existing `dnode`. */
	const struct lyd_node *pda_entry_dnode;
};

/**
 * Check for duplicated entries using the candidate configuration.
 *
 * \param vty so we can get the candidate config.
 * \param pda the arguments to check.
 */
bool plist_is_dup(const struct lyd_node *dnode, struct plist_dup_args *pda);

/*
 * 以下声明由 filter_cli.c 提供，被 YANG 框架在这些原型处引用：
 *   access_list_cmp / prefix_list_cmp : 条目排序比较回调；
 *   access_list_show / prefix_list_show : 显示整表内容；
 *   access_list_remark_show / prefix_list_remark_show : 仅显示 remark；
 *   filter_cli_init                   : 注册本模块的 CLI 命令。
 */
/* filter_cli.c */
struct lyd_node;
struct vty;

extern int access_list_cmp(const struct lyd_node *dnode1,
			   const struct lyd_node *dnode2);
extern void access_list_show(struct vty *vty, const struct lyd_node *dnode,
			     bool show_defaults);
extern void access_list_remark_show(struct vty *vty,
				    const struct lyd_node *dnode,
				    bool show_defaults);
extern int prefix_list_cmp(const struct lyd_node *dnode1,
			   const struct lyd_node *dnode2);
extern void prefix_list_show(struct vty *vty, const struct lyd_node *dnode,
			     bool show_defaults);
extern void prefix_list_remark_show(struct vty *vty,
				    const struct lyd_node *dnode,
				    bool show_defaults);

void filter_cli_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_FILTER_H */
