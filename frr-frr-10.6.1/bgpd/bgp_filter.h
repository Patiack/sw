// SPDX-License-Identifier: GPL-2.0-or-later
/* AS path filter list.
 * Copyright (C) 1999 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_filter.h —— as-path access-list（AS 路径过滤器）
 * ============================================================================
 * 【它是什么】对应配置里的 `ip as-path access-list WORD permit REGEX`。用户写的是
 * **正则表达式**，去匹配路由的 **AS_PATH 的字符串形式**（如 "65001 65002 65003"），
 * 匹配上就按本条目的 permit / deny 生效。它是 bgp filter-list 与 route-map 里
 * `match as-path` 的底座，属于“按路由来源 AS 做策略”的常用手段。
 *
 * 【两层数据结构】一个 as_list = 一条命名的访问列表；其内部由若干 as_filter
 * 条目组成，每条 = 一个序列号 seq + 一个动作(permit / deny) + 一个正则。
 * 匹配时按 **seq 升序**逐条尝试，**第一条命中即定胜负**——这点与 route-map 一致，
 * 所以 seq 的编排决定了最终策略，写正则顺序无意义。
 *
 * 【两个易混淆的名字】
 *   exclude_rule : as_list 里那条 PREDECL_DLIST 声明的链表。它承载“排除规则”，
 *       与普通条目分开存放（普通条目用 head / tail 手工链表，排除规则用类型安全的
 *       DLIST 宏），两者**不要混在一起遍历**。
 *   ASPATH_SEQ_NUMBER_AUTO : 配置里省略 seq 时的**自动序列号**哨兵值(-1)——
 *       表示“由实现分配”。因此不能把它当成一个真实序号去比较大小，必须先转换。
 *
 * 【正则引擎】用 struct frregex（FRR 对 POSIX regex 的封装，见 lib 里的 frregex），
 * 同时保存**正则源串 reg_str**：只存编译后的对象无法回显配置，二者必须配对。
 *
 * 【阅读建议】struct as_filter → struct as_list → as_list_apply（匹配主循环）
 *            → 实现在 bgp_filter.c。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_FILTER_H
#define _QUAGGA_BGP_FILTER_H

#include <typesafe.h>

struct frregex;

/* 【宏】省略序列号时的哨兵值，表示“请自动分配”(见文件头导读)。
 * 注意它是 -1，不是 0——0 可以是合法用户序号，不能拿 0 当“未指定”。 */
#define ASPATH_SEQ_NUMBER_AUTO -1

/* 【枚举】单条过滤条目的动作。只有 permit / deny 两态（不像 route-map 还有
 * 继续匹配之类的语义），因此 as_list_apply 的结果能直接当布尔策略用。
 * 顺序上 DENY=0 在前，若代码里靠 0/1 做判断要留意别把“默认值 0”误当 permit。 */
enum as_filter_type { AS_FILTER_DENY, AS_FILTER_PERMIT };

/* Element of AS path filter. */
/* 【结构】as_filter —— 访问列表中的**一条**规则。
 *   next / prev : 组成普通条目链表的双向指针（由 as_list 手工维护，不是类型安全
 *                 DLIST——与下面 exclude_rule 用的 DLIST 形成对比）。
 *   type        : 本条动作 permit / deny。
 *   reg / reg_str : 编译后的正则对象与它的**源字符串**。两者必须同时存在：
 *                 reg 用于匹配（快），reg_str 用于 show 回显配置（可读）。
 *                 只改其一会出现“回显与实际匹配不一致”的诡异现象。
 *   seq         : 本条在列表中的**匹配次序**（升序，先命中先定论）。类型是 int64_t
 *                 且可为 -1(AUTO)，所以不要用无符号类型或 int 存它。 */
struct as_filter {
	struct as_filter *next;
	struct as_filter *prev;

	enum as_filter_type type;

	struct frregex *reg;
	char *reg_str;

	/* Sequence number. */
	int64_t seq;
};

/* 【宏】为一个叫 as_list_list 的类型安全双链表“预先声明”其头/项类型。
 * 这个宏不产生代码，只是让下面的 struct as_list 能使用 as_list_list_head。 */
PREDECL_DLIST(as_list_list);
/* AS path filter list. */
/* 【结构】as_list —— 一个**命名**的 as-path 访问列表（对应配置里一个 WORD）。
 *   name       : 列表名，配置引用（如 filter-list 或 match as-path）时按键名查找。
 *   next / prev: 把所有 as_list 串成全局链表，便于遍历与回显 running-config。
 *   head / tail: 普通规则(as_filter)链表的首尾。**插入时要注意 seq 排序**——
 *                链表按 seq 有序，新条目可能插在中间而不是尾部。
 *   exclude_rule: 上述“排除规则”专用链表，与 head / tail 那串**分开存放**。
 *                语义上它描述“AS 路径的哪些变化需要排除”，走的是 DLIST 框架，
 *                因此遍历方式与普通规则不同，别混用。 */
struct as_list {
	char *name;

	struct as_list *next;
	struct as_list *prev;

	struct as_filter *head;
	struct as_filter *tail;

	/* Changes in AS path */
	struct as_list_list_head exclude_rule;
};


/* 【函数】生命周期：
 *   bgp_filter_init  : 建立本模块的内部状态（如 AS 路径字符串的复用缓冲）。
 *   bgp_filter_reset : 清空所有访问列表——进程重启 / 配置重载时用，注意它会把
 *                     配置一并丢掉，只应由框架在适当时机调用。 */
extern void bgp_filter_init(void);
extern void bgp_filter_reset(void);

/* 【函数】as_list_apply —— 核心：拿一条路由的 AS 路径去跑这个列表，返回最终动作。
 * 实现会按 seq 升序逐条匹配正则，**第一条命中即返回**；若全部未命中，返回值由
 * 实现约定（通常是 deny 或 permit，读实现确认，不要凭直觉假设）。
 * object 是 void * —— 之所以不写成 struct aspath *，是为了让同一个匹配函数也能
 * 被别的调用方复用（传入的对象由调用方与实现约定），C 语言下牺牲了类型安全。 */
extern enum as_filter_type as_list_apply(struct as_list *aslist, void *object);

/* 【函数】按名字查列表 / 注册增删回调 / 校验正则：
 *   as_list_lookup        : 用名字找 as_list，找不到返回 NULL（配置引用了不存在的
 *                           列表时靠这个报错，不会自动创建）。
 *   as_list_add_hook      : 注册“列表被创建”时的回调——上层（如 bgp_vty / 策略模块）
 *                           借此在列表变动后重建依赖它的过滤结构。
 *   as_list_delete_hook   : 注册“列表被删除”时的回调，同样用于失效通知。
 *                           回调参数是**名字**（char *），不是对象指针——所以回调里
 *                           通常还要自己再 lookup 一次，或只做失效标记。
 *   config_bgp_aspath_validate : 配置时先校验用户写的正则能否编译，避免把坏正则
 *                           装进运行态；返回 true 表示可用。 */
extern struct as_list *as_list_lookup(const char *);
extern void as_list_add_hook(void (*func)(char *));
extern void as_list_delete_hook(void (*func)(const char *));
extern bool config_bgp_aspath_validate(const char *regstr);

#endif /* _QUAGGA_BGP_FILTER_H */
