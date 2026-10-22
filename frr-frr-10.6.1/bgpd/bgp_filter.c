// SPDX-License-Identifier: GPL-2.0-or-later
/* AS path filter list.
 * Copyright (C) 1999 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_filter.c —— as-path access-list（AS 路径过滤器）的实现
 * ============================================================================
 * 【它是什么】对应配置 `bgp as-path access-list WORD permit REGEX`（还有老的
 * `ip as-path access-list` 别名）。用户写一条**正则表达式**，去匹配路由的
 * **AS_PATH 字符串形式**（如 "65001 65002 65003"），命中即按该条目的 permit
 * / deny 决定放行还是拒绝。它是 `neighbor ... filter-list` 与 route-map
 * `match as-path` 的底座。
 *
 * 【本文件的数据结构（对照 bgp_filter.h 一起看）】
 *   struct as_list_list   ：本文件私有的“列表链表头尾对”。**注意它与头文件里
 *                           同名的 as_list_list 是两个不同的类型**——头文件那个
 *                           用于 exclude_rule 的 DLIST 框架，本文件的只是把一个
 *                           as_list 链表的 head / tail 打包。名字撞车是历史遗留。
 *   struct as_list_master ：全局单例，只保存**字符串名**列表链表 str 与增删两个
 *                           hook。本文件只实现字符串名一路，**没有**数字名容器
 *                           （这点与 community-list 不同，as-path 不支持数字名）。
 *   struct as_list        ：一条命名访问列表（定义在头文件）。
 *   struct as_filter      ：列表内一条规则（定义在头文件）。
 *
 * 【核心流程】
 *   配置写入 bgp_as_path_cmd → as_list_get 取 / 建列表 → as_list_filter_add 把
 *   新规则**按 seq 升序**插进链表（同 seq 则整条替换）→ 触发 add_hook 通知依赖方。
 *   匹配 as_list_apply → 按 seq 序遍历，逐条跑正则，**首条命中即返回其 type**；
 *   全不命中返回 AS_FILTER_DENY（即“未命中默认拒绝”）。
 *
 * 【两个关键点】
 *   1. 匹配用 bgp_regexec(reg, aspath)，把 aspath 对象的**字符串形式**喂给正则
 *      引擎，所以正则里写的是 AS 号序列（如 ^65001_ ）而不是别的。
 *   2. exclude_rule 那条 DLIST 与普通规则链表**分开存放**（见头文件导读），由
 *      aspath exclude 机制借用；本文件只在配置命令与 unset 时做挂接 / 摘除。
 *
 * 【阅读建议】两个私有结构 → seq 分配 / 查重 → as_filter_make → as_list_insert
 *            → as_list_filter_add（有序插入）→ as_list_apply（匹配主循环）
 *            → config_bgp_aspath_validate（预校验）→ 两个 DEFUN → init / reset。
 * ============================================================================
 */

#include <zebra.h>

#include "command.h"
#include "log.h"
#include "memory.h"
#include "buffer.h"
#include "queue.h"
#include "filter.h"
#include "frregex_real.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_regex.h"

/* List of AS list. */
/* 【结构】as_list_list —— 本文件私有的“列表链表头尾对”。**与头文件里同名的
 * as_list_list 是两个不同的类型**：头文件那个服务于 exclude_rule 的 DLIST 框架，
 * 这个只是把 as_list 链表的 head / tail 打包，便于作为 as_list_master 的成员统一
 * 维护。名字撞车是历史遗留，读代码时按“所在文件”区分。 */
struct as_list_list {
	struct as_list *head;
	struct as_list *tail;
};

/* AS path filter master. */
/* 【结构】as_list_master —— 全局单例（见下方 as_list_master 变量）。**只维护
 * 字符串名列表**（str）——as-path 访问列表不支持数字名，这是与 community-list
 * 的重要差异。另存两个函数指针 hook：列表被增 / 删时对外通知（route-map 等依赖方
 * 借此失效重建）。hook 为 NULL 时静默跳过，所以未注册也不会崩。 */
struct as_list_master {
	/* List of access_list which name is string. */
	struct as_list_list str;

	/* Hook function which is executed when new access_list is added. */
	void (*add_hook)(char *);

	/* Hook function which is executed when access_list is deleted. */
	void (*delete_hook)(const char *);
};



/* Calculate new sequential number. */
/* 【函数】为新条目**自动分配 seq**：取当前最大 seq，向上取整到 5 的倍数再加 5
 * （即 5,10,15… 递增，如 maxseq=5 则新值=10）。这样手工与自动混用时留出插空余地。
 * 上限截到 UINT_MAX。仅在“用户没写 seq”（seqnum == ASPATH_SEQ_NUMBER_AUTO）时调用。 */
static int64_t bgp_alist_new_seq_get(struct as_list *list)
{
	int64_t maxseq;
	int64_t newseq;
	struct as_filter *entry;

	maxseq = 0;

	for (entry = list->head; entry; entry = entry->next) {
		if (maxseq < entry->seq)
			maxseq = entry->seq;
	}

	newseq = ((maxseq / 5) * 5) + 5;

	return (newseq > UINT_MAX) ? UINT_MAX : newseq;
}

/* Return as-list entry which has same seq number. */
/* 【函数】按 seq 精确查找已有条目：找到返回它（调用方据此做“同 seq 替换”），
 * 找不到返回 NULL。注意它比对的是 **seq** 而非规则内容——seq 在同一列表内唯一。 */
static struct as_filter *bgp_aslist_seq_check(struct as_list *list, int64_t seq)
{
	struct as_filter *entry;

	for (entry = list->head; entry; entry = entry->next)
		if (entry->seq == seq)
			return entry;

	return NULL;
}

/* as-path access-list 10 permit AS1. */

/* 【全局】唯一实例：{{NULL,NULL}, NULL, NULL} 即 str 链表为空、两个 hook 均为 NULL。
 * 全部 as_list 都挂在这唯一的 str 链表上。 */
static struct as_list_master as_list_master = {{NULL, NULL},
					       NULL,
					       NULL};

/* Allocate new AS filter. */
/* 【函数】分配并**零初始化**一条规则（XCALLOC）。零初始化意味着 type 默认 0
 * 即 AS_FILTER_DENY、seq 默认 0、reg / reg_str 为 NULL——调用方务必再逐字段填齐。
 * 内存类型标签是 MTYPE_AS_FILTER。 */
static struct as_filter *as_filter_new(void)
{
	return XCALLOC(MTYPE_AS_FILTER, sizeof(struct as_filter));
}

/* Free allocated AS filter. */
/* 【函数】释放规则：先释放编译后的正则(bgp_regex_free)、再释放正则源串、最后释放
 * 结构本身。注意 reg_str 用的是 MTYPE_AS_FILTER_STR，与结构体的 MTYPE_AS_FILTER
 * 是**两个不同的内存类型标签**，别对错类型。 */
static void as_filter_free(struct as_filter *asfilter)
{
	if (asfilter->reg)
		bgp_regex_free(asfilter->reg);
	XFREE(MTYPE_AS_FILTER_STR, asfilter->reg_str);
	XFREE(MTYPE_AS_FILTER, asfilter);
}

/* Make new AS filter. */
/* 【函数】装配一条规则：保存**已编译**的正则对象 reg 与源串 reg_str、以及动作
 * type。源串在这里被 XSTRDUP 一份（配置原文，用于 show 回显）。reg 的所有权
 * 转移给本条目，之后由 as_filter_free 负责释放——调用方不要再自己 free reg。 */
static struct as_filter *as_filter_make(struct frregex *reg, const char *reg_str,
					enum as_filter_type type)
{
	struct as_filter *asfilter;

	asfilter = as_filter_new();
	asfilter->reg = reg;
	asfilter->type = type;
	asfilter->reg_str = XSTRDUP(MTYPE_AS_FILTER_STR, reg_str);

	return asfilter;
}

/* 【函数】在列表里按**正则源串**查找规则：只比 reg_str，**不比传入的 type**。
 * 因此对同一正则同时写了 permit 与 deny 两条时，它会返回先遇到的那条。它用在
 * `no bgp as-path` 命令里（该命令还会另判动作），单纯靠它定位规则时要留意这个局限。 */
static struct as_filter *as_filter_lookup(struct as_list *aslist,
					  const char *reg_str,
					  enum as_filter_type type)
{
	struct as_filter *asfilter;

	for (asfilter = aslist->head; asfilter; asfilter = asfilter->next)
		if (strcmp(reg_str, asfilter->reg_str) == 0)
			return asfilter;
	return NULL;
}

/* 【函数】用新条目 entry **原地替换**旧条目 replace：接好前后指针（若旧条目在
 * 首 / 尾还要顺带改 list->head / list->tail），最后释放旧条目。用于“同 seq 覆盖”。 */
static void as_filter_entry_replace(struct as_list *list,
				    struct as_filter *replace,
				    struct as_filter *entry)
{
	if (replace->next) {
		entry->next = replace->next;
		replace->next->prev = entry;
	} else {
		entry->next = NULL;
		list->tail = entry;
	}

	if (replace->prev) {
		entry->prev = replace->prev;
		replace->prev->next = entry;
	} else {
		entry->prev = NULL;
		list->head = entry;
	}

	as_filter_free(replace);
}

/* 【函数】把新规则按 **seq 升序**插入列表，是“同 seq 覆盖 / 升序插入”的核心：
 *   1) 若新 seq 比尾节点还大，直接插尾部（令 point = NULL 走尾部分支）；
 *   2) 否则先 bgp_aslist_seq_check 查同 seq——命中就整条替换后 goto hook；
 *   3) 否则从头找到第一个 seq 不小于自己的点，插在它前面。
 * 无论走哪条路，末尾都调用 add_hook(aslist->name) 通知依赖方。
 * 注意 seq 相等时不是并存而是替换，这保证同一列表内 seq 唯一。 */
static void as_list_filter_add(struct as_list *aslist,
			       struct as_filter *asfilter)
{
	struct as_filter *point;
	struct as_filter *replace;

	if (aslist->tail && asfilter->seq > aslist->tail->seq)
		point = NULL;
	else {
		replace = bgp_aslist_seq_check(aslist, asfilter->seq);
		if (replace) {
			as_filter_entry_replace(aslist, replace, asfilter);
			goto hook;
		}

		/* Check insert point. */
		for (point = aslist->head; point; point = point->next)
			if (point->seq >= asfilter->seq)
				break;
	}

	asfilter->next = point;

	if (point) {
		if (point->prev)
			point->prev->next = asfilter;
		else
			aslist->head = asfilter;

		asfilter->prev = point->prev;
		point->prev = asfilter;
	} else {
		if (aslist->tail)
			aslist->tail->next = asfilter;
		else
			aslist->head = asfilter;

		asfilter->prev = aslist->tail;
		aslist->tail = asfilter;
	}

hook:
	/* Run hook function. */
	if (as_list_master.add_hook)
		(*as_list_master.add_hook)(aslist->name);
}

/* Lookup as_list from list of as_list by name. */
/* 【函数】按名字线性查找列表（公开函数，供别处引用列表时用）。找不到返回 NULL。
 * 注意它**不会创建**——与 as_list_get 的区别正在于此。name 为 NULL 直接返回 NULL。 */
struct as_list *as_list_lookup(const char *name)
{
	struct as_list *aslist;

	if (name == NULL)
		return NULL;

	for (aslist = as_list_master.str.head; aslist; aslist = aslist->next)
		if (strcmp(aslist->name, name) == 0)
			return aslist;
	return NULL;
}

/* 【函数】分配一个空列表（零初始化）。注意 name 用 MTYPE_AS_STR，而结构体本身
 * 用 MTYPE_AS_LIST，两个内存类型标签不同（见 as_list_free）。 */
static struct as_list *as_list_new(void)
{
	return XCALLOC(MTYPE_AS_LIST, sizeof(struct as_list));
}

static void as_list_free(struct as_list *aslist)
{

	XFREE (MTYPE_AS_STR, aslist->name);
	XFREE (MTYPE_AS_LIST, aslist);
}

/* Insert new AS list to list of as_list.  Each as_list is sorted by
   the name. */
/* 【函数】新建并**按名字字典序**插入全局 str 链表（用 strcmp 定序）。名字有序
 * 只为回显配置时顺序稳定、便于阅读，与匹配无关（匹配靠 seq）。四种插入位置分别
 * 处理：空表 / 尾部 / 头部 / 中间，逐一维护 prev / next / head / tail。
 * 注意这里**不做重名检查**——进入前调用方 as_list_get 已先 lookup 过。 */
static struct as_list *as_list_insert(const char *name)
{
	struct as_list *aslist;
	struct as_list *point;
	struct as_list_list *list;

	/* Allocate new access_list and copy given name. */
	aslist = as_list_new();
	aslist->name = XSTRDUP(MTYPE_AS_STR, name);
	assert(aslist->name);

	/* Set access_list to string list. */
	list = &as_list_master.str;

	/* Set point to insertion point. */
	for (point = list->head; point; point = point->next)
		if (strcmp(point->name, name) >= 0)
			break;

	/* In case of this is the first element of master. */
	if (list->head == NULL) {
		list->head = list->tail = aslist;
		return aslist;
	}

	/* In case of insertion is made at the tail of access_list. */
	if (point == NULL) {
		aslist->prev = list->tail;
		list->tail->next = aslist;
		list->tail = aslist;
		return aslist;
	}

	/* In case of insertion is made at the head of access_list. */
	if (point == list->head) {
		aslist->next = list->head;
		list->head->prev = aslist;
		list->head = aslist;
		return aslist;
	}

	/* Insertion is made at middle of the access_list. */
	aslist->next = point;
	aslist->prev = point->prev;

	if (point->prev)
		point->prev->next = aslist;
	point->prev = aslist;

	return aslist;
}

/* 【函数】取或建：先 as_list_lookup，没有就 as_list_insert。这是配置命令的落点：
 * “首次引用名字即隐式创建列表”。 */
static struct as_list *as_list_get(const char *name)
{
	struct as_list *aslist;

	aslist = as_list_lookup(name);
	if (aslist == NULL)
		aslist = as_list_insert(name);

	return aslist;
}

/* 【函数】把动作枚举转成 "permit" / "deny" 字符串，供 show 与写配置回显。
 * 未知值返回空串。 */
static const char *filter_type_str(enum as_filter_type type)
{
	switch (type) {
	case AS_FILTER_PERMIT:
		return "permit";
	case AS_FILTER_DENY:
		return "deny";
	default:
		return "";
	}
}

/* 【函数】删除整个列表：先逐个释放内部规则，再从全局 str 链表上摘除自己（注意
 * 首 / 尾迁移），最后释放自身。它**不调用 delete_hook**——hook 由调用方（配置
 * 命令或 as_list_filter_delete）在合适时机统一调用，避免重复通知。 */
static void as_list_delete(struct as_list *aslist)
{
	struct as_list_list *list;
	struct as_filter *filter, *next;

	for (filter = aslist->head; filter; filter = next) {
		next = filter->next;
		as_filter_free(filter);
	}

	list = &as_list_master.str;

	if (aslist->next)
		aslist->next->prev = aslist->prev;
	else
		list->tail = aslist->prev;

	if (aslist->prev)
		aslist->prev->next = aslist->next;
	else
		list->head = aslist->next;

	as_list_free(aslist);
}

/* 【函数】判断列表是否已无规则（head 与 tail 同时为空）。删规则后据此决定是否
 * 连带把整个列表也删掉。 */
static bool as_list_empty(struct as_list *aslist)
{
	return aslist->head == NULL && aslist->tail == NULL;
}

/* 【函数】从列表删除单条规则：先摘链、释放规则；若删空则顺带 as_list_delete 整表；
 * 最后**总是**触发 delete_hook。名字先 XSTRDUP 备份，因为规则删空后 aslist
 * 可能已被释放，届时 aslist->name 不可再访问。 */
static void as_list_filter_delete(struct as_list *aslist,
				  struct as_filter *asfilter)
{
	char *name = XSTRDUP(MTYPE_AS_STR, aslist->name);

	if (asfilter->next)
		asfilter->next->prev = asfilter->prev;
	else
		aslist->tail = asfilter->prev;

	if (asfilter->prev)
		asfilter->prev->next = asfilter->next;
	else
		aslist->head = asfilter->next;

	as_filter_free(asfilter);

	/* If access_list becomes empty delete it from access_master. */
	if (as_list_empty(aslist))
		as_list_delete(aslist);

	/* Run hook function. */
	if (as_list_master.delete_hook)
		(*as_list_master.delete_hook)(name);
	XFREE(MTYPE_AS_STR, name);
}

/* 【函数】单条规则的匹配：把 aspath 喂给编译好的正则，返回值非 REG_NOMATCH 即
 * 视为命中。一行封装，真正匹配由 bgp_regexec 完成（见 bgp_regex.c）。 */
static bool as_filter_match(struct as_filter *asfilter, struct aspath *aspath)
{
	return bgp_regexec(asfilter->reg, aspath) != REG_NOMATCH;
}

/* Apply AS path filter to AS. */
/* 【函数·匹配主循环】按链表顺序（即 seq 升序）逐条跑正则，
 * **第一条命中即返回该条目的 type（permit 或 deny）**——首条命中定论。
 *   - aslist == NULL（配置引用了不存在的列表）→ 直接返回 AS_FILTER_DENY；
 *   - 全部未命中 → 同样返回 AS_FILTER_DENY。
 * 即“未命中默认拒绝”。参数 object 声明为 void *，此处按调用约定转成 aspath。 */
enum as_filter_type as_list_apply(struct as_list *aslist, void *object)
{
	struct as_filter *asfilter;
	struct aspath *aspath;

	aspath = (struct aspath *)object;

	if (aslist == NULL)
		return AS_FILTER_DENY;

	for (asfilter = aslist->head; asfilter; asfilter = asfilter->next) {
		if (as_filter_match(asfilter, aspath))
			return asfilter->type;
	}
	return AS_FILTER_DENY;
}

/* Add hook function. */
/* 【函数】注册“列表新增规则时”的回调（由上层调用，如策略模块借此在列表变动后
 * 重建依赖它的结构）。只保存函数指针，替换掉旧的。 */
void as_list_add_hook(void (*func)(char *))
{
	as_list_master.add_hook = func;
}

/* Delete hook function. */
/* 【函数】注册“列表被删除时”的回调。回调参数是**列表名（char *）而非对象指针**，
 * 故回调里通常需自行 lookup 或只做失效标记（见头文件说明）。 */
void as_list_delete_hook(void (*func)(const char *))
{
	as_list_master.delete_hook = func;
}

/* 【函数】查重：若列表里已有**动作(type)与正则源串(reg_str)都相同**的条目则返回
 * true，配置命令据此丢弃新条目（避免同一规则重复插入）。只比 type + reg_str，
 * **不比 seq**。 */
static bool as_list_dup_check(struct as_list *aslist, struct as_filter *new)
{
	struct as_filter *asfilter;

	for (asfilter = aslist->head; asfilter; asfilter = asfilter->next) {
		if (asfilter->type == new->type
		    && strcmp(asfilter->reg_str, new->reg_str) == 0)
			return true;
	}
	return false;
}

/* 【函数·预校验】在真正写入前，用**字符白名单**挡住明显非法的正则：允许集为
 * "1234567890_^|[,{}() ]$*+.?-\\"。strspn 统计从头连续属于白名单的字符个数，
 * 等于串全长才算通过。注意这是**粗筛而非语法校验**——它不保证正则语法正确，
 * 只挡掉含非法字符的输入；真正的合法性仍由 bgp_regcomp 能否编译决定（两者互补）。 */
bool config_bgp_aspath_validate(const char *regstr)
{
	char valid_chars[] = "1234567890_^|[,{}() ]$*+.?-\\";

	if (strspn(regstr, valid_chars) == strlen(regstr))
		return true;
	return false;
}

/* 【命令·DEFUN bgp_as_path_cmd】顶层配置命令 "bgp as-path access-list" 的实现体，
 * 用于**新增一条** AS-path 过滤条目。流程：解析过滤器名、可选 seq、动作(deny/permit)
 * 与正则表达式 LINE；先 bgp_regcomp 编译正则，再用 config_bgp_aspath_validate 做字符
 * 白名单粗筛，任一失败都释放 regstr 并返回 CMD_WARNING_CONFIG_FAILED。
 * 通过后 as_filter_make 造出 asfilter 并挂到 as_list_get(名字) 上：seq 未指定则用
 * bgp_alist_new_seq_get 自动分配；as_list_dup_check 命中则直接释放新条目（不重复插入）。
 * 侧作用：还会把 aslist->exclude_rule 初始化，并把此前处于 orphan 的 aspath 排除规则
 * （as_exclude_lookup_orphan）重新挂回本表并设置反向指针 exclude_aspath_acl。
 * 返回 CMD_SUCCESS；易错点：seq 自动分配与 orphan 回挂都发生在插入之后。 */
DEFUN(as_path, bgp_as_path_cmd,
      "bgp as-path access-list AS_PATH_FILTER_NAME [seq (0-4294967295)] <deny|permit> LINE...",
      BGP_STR
      "BGP autonomous system path filter\n"
      "Specify an access list name\n"
      "Regular expression access list name\n"
      "Sequence number of an entry\n"
      "Sequence number\n"
      "Specify packets to reject\n"
      "Specify packets to forward\n"
      "A regular-expression (1234567890_^|[,{}() ]$*+.?-\\) to match the BGP AS paths\n")
{
	int idx = 0;
	enum as_filter_type type;
	struct as_filter *asfilter;
	struct as_list *aslist;
	struct aspath_exclude *ase;
	struct frregex *regex;
	char *regstr;
	int64_t seqnum = ASPATH_SEQ_NUMBER_AUTO;

	/* Retrieve access list name */
	argv_find(argv, argc, "AS_PATH_FILTER_NAME", &idx);
	char *alname = argv[idx]->arg;

	if (argv_find(argv, argc, "(0-4294967295)", &idx))
		seqnum = (int64_t)atol(argv[idx]->arg);

	/* Check the filter type. */
	type = argv_find(argv, argc, "deny", &idx) ? AS_FILTER_DENY
						   : AS_FILTER_PERMIT;

	/* Check AS path regex. */
	argv_find(argv, argc, "LINE", &idx);
	regstr = argv_concat(argv, argc, idx);

	regex = bgp_regcomp(regstr);
	if (!regex) {
		vty_out(vty, "can't compile regexp %s\n", regstr);
		XFREE(MTYPE_TMP, regstr);
		return CMD_WARNING_CONFIG_FAILED;
	}

	if (!config_bgp_aspath_validate(regstr)) {
		vty_out(vty, "Invalid character in as-path access-list %s\n",
			regstr);
		XFREE(MTYPE_TMP, regstr);
		return CMD_WARNING_CONFIG_FAILED;
	}

	asfilter = as_filter_make(regex, regstr, type);

	XFREE(MTYPE_TMP, regstr);

	/* Install new filter to the access_list. */
	aslist = as_list_get(alname);

	if (seqnum == ASPATH_SEQ_NUMBER_AUTO)
		seqnum = bgp_alist_new_seq_get(aslist);

	asfilter->seq = seqnum;

	/* Duplicate insertion check. */;
	if (as_list_dup_check(aslist, asfilter))
		as_filter_free(asfilter);
	else
		as_list_filter_add(aslist, asfilter);

	/* init the exclude rule list*/
	as_list_list_init(&aslist->exclude_rule);

	/* get aspath orphan exclude that are using this acl */
	ase = as_exclude_lookup_orphan(alname);
	if (ase) {
		as_list_list_add_head(&aslist->exclude_rule, ase);
		/* set reverse pointer */
		ase->exclude_aspath_acl = aslist;
		/* set list of aspath excludes using that acl */
		while ((ase = as_exclude_lookup_orphan(alname))) {
			as_list_list_add_head(&aslist->exclude_rule, ase);
			ase->exclude_aspath_acl = aslist;
		}
	}

	return CMD_SUCCESS;
}

/* 【命令·DEFUN no_bgp_as_path_cmd】"no bgp as-path access-list" 的实现，用于**删除单条**
 * AS-path 过滤条目（需给出名、动作、正则以精确定位）。先 as_list_lookup 找表，找不到报错；
 * 再解析 permit/deny 动作，缺动作报错；随后 config_bgp_aspath_validate 粗筛 + bgp_regcomp
 * 编译正则，编译完立刻 bgp_regex_free（这里只用它做合法性验证，不长期持有）。
 * as_filter_lookup 按 reg_str + type 定位条目，找不到报错。
 * 侧作用：删除前把 aslist->exclude_rule 里所有规则弹出并 as_exclude_set_orphan 置为孤儿
 * （因该表条目变了，排除规则需重挂），随后 as_list_list_fini 销毁该规则链表，
 * 最后 as_list_filter_delete 真正摘除条目。返回 CMD_SUCCESS 或 CMD_WARNING_CONFIG_FAILED。 */
DEFUN(no_as_path, no_bgp_as_path_cmd,
      "no bgp as-path access-list AS_PATH_FILTER_NAME [seq (0-4294967295)] <deny|permit> LINE...",
      NO_STR
      BGP_STR
      "BGP autonomous system path filter\n"
      "Specify an access list name\n"
      "Regular expression access list name\n"
      "Sequence number of an entry\n"
      "Sequence number\n"
      "Specify packets to reject\n"
      "Specify packets to forward\n"
      "A regular-expression (1234567890_^|[,{}() ]$*+.?-\\) to match the BGP AS paths\n")
{
	int idx = 0;
	enum as_filter_type type;
	struct as_filter *asfilter;
	struct as_list *aslist;
	struct aspath_exclude *ase;
	char *regstr;
	struct frregex *regex;

	char *aslistname =
		argv_find(argv, argc, "AS_PATH_FILTER_NAME", &idx) ? argv[idx]->arg : NULL;

	/* Lookup AS list from AS path list. */
	aslist = as_list_lookup(aslistname);
	if (aslist == NULL) {
		vty_out(vty, "bgp as-path access-list %s doesn't exist\n",
			aslistname);
		return CMD_WARNING_CONFIG_FAILED;
	}

	/* Check the filter type. */
	if (argv_find(argv, argc, "permit", &idx))
		type = AS_FILTER_PERMIT;
	else if (argv_find(argv, argc, "deny", &idx))
		type = AS_FILTER_DENY;
	else {
		vty_out(vty, "filter type must be [permit|deny]\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	/* Compile AS path. */
	argv_find(argv, argc, "LINE", &idx);
	regstr = argv_concat(argv, argc, idx);

	if (!config_bgp_aspath_validate(regstr)) {
		vty_out(vty, "Invalid character in as-path access-list %s\n",
			regstr);
		return CMD_WARNING_CONFIG_FAILED;
	}

	regex = bgp_regcomp(regstr);
	if (!regex) {
		vty_out(vty, "can't compile regexp %s\n", regstr);
		XFREE(MTYPE_TMP, regstr);
		return CMD_WARNING_CONFIG_FAILED;
	}

	/* Lookup asfilter. */
	asfilter = as_filter_lookup(aslist, regstr, type);

	bgp_regex_free(regex);

	if (asfilter == NULL) {
		vty_out(vty, "Regex entered %s does not exist\n", regstr);
		XFREE(MTYPE_TMP, regstr);
		return CMD_WARNING_CONFIG_FAILED;
	}

	XFREE(MTYPE_TMP, regstr);

	/* put aspath exclude list into orphan */
	if (as_list_list_count(&aslist->exclude_rule))
		while ((ase = as_list_list_pop(&aslist->exclude_rule)))
			as_exclude_set_orphan(ase);

	as_list_list_fini(&aslist->exclude_rule);
	as_list_filter_delete(aslist, asfilter);

	return CMD_SUCCESS;
}

/* 【命令·DEFUN no_bgp_as_path_all_cmd】"no bgp as-path access-list NAME" 的实现，
 * 用于**整表删除**某条 AS-path 过滤表。as_list_lookup 找到整表后直接 as_list_delete
 * 释放（连带其下所有条目）。侧作用：删除后若注册了 as_list_master.delete_hook 回调
 * （见 as_list_delete_hook），则调用它通知使用者该表已消失，避免悬空引用。
 * 找不到表返回 CMD_WARNING_CONFIG_FAILED，否则 CMD_SUCCESS。 */
DEFUN (no_as_path_all,
       no_bgp_as_path_all_cmd,
       "no bgp as-path access-list AS_PATH_FILTER_NAME",
       NO_STR
       BGP_STR
       "BGP autonomous system path filter\n"
       "Specify an access list name\n"
       "Regular expression access list name\n")
{
	int idx_word = 4;
	struct as_list *aslist;

	aslist = as_list_lookup(argv[idx_word]->arg);
	if (aslist == NULL) {
		vty_out(vty, "bgp as-path access-list %s doesn't exist\n",
			argv[idx_word]->arg);
		return CMD_WARNING_CONFIG_FAILED;
	}

	as_list_delete(aslist);

	/* Run hook function. */
	if (as_list_master.delete_hook)
		(*as_list_master.delete_hook)(argv[idx_word]->arg);

	return CMD_SUCCESS;
}

/* 【函数·显示】把单张 as_list 的内容输出到 vty，或（json 非空时）输出为 JSON。
 * json 模式：新建数组挂到 json 下以列表名為 key，每条条目生成对象，含 sequenceNumber、
 * type（filter_type_str 转 permit / deny）、regExp 三个字段。
 * 文本模式：先打印 "AS path access list NAME"，再逐条打印 "    type reg_str"。
 * 无返回值；只读遍历，不改动链表。 */
static void as_list_show(struct vty *vty, struct as_list *aslist,
			 json_object *json)
{
	struct as_filter *asfilter;
	json_object *json_aslist = NULL;

	if (json) {
		json_aslist = json_object_new_array();
		json_object_object_add(json, aslist->name, json_aslist);
	} else
		vty_out(vty, "AS path access list %s\n", aslist->name);

	for (asfilter = aslist->head; asfilter; asfilter = asfilter->next) {
		if (json) {
			json_object *json_asfilter = json_object_new_object();

			json_object_int_add(json_asfilter, "sequenceNumber",
					    asfilter->seq);
			json_object_string_add(json_asfilter, "type",
					       filter_type_str(asfilter->type));
			json_object_string_add(json_asfilter, "regExp",
					       asfilter->reg_str);

			json_object_array_add(json_aslist, json_asfilter);
		} else
			vty_out(vty, "    %s %s\n",
				filter_type_str(asfilter->type),
				asfilter->reg_str);
	}
}

/* 【函数·显示】遍历 as_list_master.str 中的**所有** AS-path 过滤表，逐张调
 * as_list_show 输出。用于 "show ... as-path-access-list"（不带名字）的整表展示。
 * 无返回值；json 透传给 as_list_show 决定输出形式。 */
static void as_list_show_all(struct vty *vty, json_object *json)
{
	struct as_list *aslist;

	for (aslist = as_list_master.str.head; aslist; aslist = aslist->next)
		as_list_show(vty, aslist, json);
}

/* 【命令·DEFUN show_bgp_as_path_access_list_cmd】"show bgp as-path-access-list NAME [json]"
 * 的实现：展示**指定名字**的 AS-path 过滤表。用 use_json 决定是否输出 JSON；
 * as_list_lookup 找到则调 as_list_show 打印，找不到则静默（不报错，直接返回成功）。
 * json 模式末尾 vty_json 统一输出。返回 CMD_SUCCESS。 */
DEFUN (show_as_path_access_list,
       show_bgp_as_path_access_list_cmd,
       "show bgp as-path-access-list AS_PATH_FILTER_NAME [json]",
       SHOW_STR
       BGP_STR
       "List AS path access lists\n"
       "AS path access list name\n"
       JSON_STR)
{
	int idx_word = 3;
	struct as_list *aslist;
	bool uj = use_json(argc, argv);
	json_object *json = NULL;

	if (uj)
		json = json_object_new_object();

	aslist = as_list_lookup(argv[idx_word]->arg);
	if (aslist)
		as_list_show(vty, aslist, json);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令·ALIAS show_ip_as_path_access_list_cmd】把上面的 show bgp as-path-access-list
 * 命令再注册一个 "show ip as-path-access-list" 别名（历史兼容写法），实现体复用
 * show_as_path_access_list。仅多一条命令入口，逻辑与主命令完全一致。 */
ALIAS (show_as_path_access_list,
       show_ip_as_path_access_list_cmd,
       "show ip as-path-access-list AS_PATH_FILTER_NAME [json]",
       SHOW_STR
       IP_STR
       "List AS path access lists\n"
       "AS path access list name\n"
       JSON_STR)

/* 【命令·DEFUN show_bgp_as_path_access_list_all_cmd】"show bgp as-path-access-list [json]"
 * 的实现：展示**全部** AS-path 过滤表，调 as_list_show_all 遍历输出。use_json 决定
 * 是否 JSON 化，末尾 vty_json 输出。返回 CMD_SUCCESS。 */
DEFUN (show_as_path_access_list_all,
       show_bgp_as_path_access_list_all_cmd,
       "show bgp as-path-access-list [json]",
       SHOW_STR
       BGP_STR
       "List AS path access lists\n"
       JSON_STR)
{
	bool uj = use_json(argc, argv);
	json_object *json = NULL;

	if (uj)
		json = json_object_new_object();

	as_list_show_all(vty, json);

	if (uj)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令·ALIAS show_ip_as_path_access_list_all_cmd】为全表展示命令注册别名
 * "show ip as-path-access-list [json]"，实现复用 show_as_path_access_list_all。 */
ALIAS (show_as_path_access_list_all,
       show_ip_as_path_access_list_all_cmd,
       "show ip as-path-access-list [json]",
       SHOW_STR
       IP_STR
       "List AS path access lists\n"
       JSON_STR)

/* 【函数·配置回写】把所有 AS-path 过滤表按配置语法逐行写回 vty（保存配置/显示运行配置时
 * 调用）。双重循环遍历 as_list_master.str 与每张表下的条目，输出形如
 * "bgp as-path access-list NAME seq N TYPE REGSTR" 的行。
 * 返回值为写出的行数（write 计数），无表时返回 0。注意：这是只读遍历，不改动状态。 */
static int config_write_as_list(struct vty *vty)
{
	struct as_list *aslist;
	struct as_filter *asfilter;
	int write = 0;

	for (aslist = as_list_master.str.head; aslist; aslist = aslist->next)
		for (asfilter = aslist->head; asfilter;
		     asfilter = asfilter->next) {
			vty_out(vty,
				"bgp as-path access-list %s seq %" PRId64
				" %s %s\n",
				aslist->name, asfilter->seq,
				filter_type_str(asfilter->type),
				asfilter->reg_str);
			write++;
		}
	return write;
}

/* 【声明+数据结构】config_write_as_list 的前置声明，供紧随其后的 cmd_node 引用；
 * 结构体 as_list_node 是 AS_PATH access-list 节点（AS_LIST_NODE）的注册信息：
 * name 为 "as list"、prompt 为空、config_write 指向 config_write_as_list。
 * 在 bgp_filter_init 里经 install_node 注册，使该节点可承载配置并参与配置回写。 */
static int config_write_as_list(struct vty *vty);
static struct cmd_node as_list_node = {
	.name = "as list",
	.node = AS_LIST_NODE,
	.prompt = "",
	.config_write = config_write_as_list,
};

/* 【函数·补齐】AS_PATH_FILTER_NAME 令牌的命令行补全回调：遍历所有 AS-path 过滤表，
 * 把每个表名 XSTRDUP 后 vector_set 进 comps，供 vty 补全候选。
 * 无返回值；候选串按 MTYPE_COMPLETION 分配，释放由补全框架负责。 */
static void bgp_aspath_filter_cmd_completion(vector comps,
					     struct cmd_token *token)
{
	struct as_list *aslist;

	for (aslist = as_list_master.str.head; aslist; aslist = aslist->next)
		vector_set(comps, XSTRDUP(MTYPE_COMPLETION, aslist->name));
}

/* 【数据结构】命令行变量处理器表：把令牌名 AS_PATH_FILTER_NAME 绑定到
 * bgp_aspath_filter_cmd_completion 补全函数，末尾以 {.completions = NULL} 作结束哨兵。
 * 在 bgp_filter_init 里经 cmd_variable_handler_register 注册。 */
static const struct cmd_variable_handler aspath_filter_handlers[] = {
	{.tokenname = "AS_PATH_FILTER_NAME",
	 .completions = bgp_aspath_filter_cmd_completion},
	{.completions = NULL}};

/* 【函数·模块初始化】bgp 模块启动时调用，注册 AS-path 过滤相关命令：
 * install_node 注册 as_list_node；在 CONFIG_NODE 装入 bgp / no / delete-all 三条配置命令，
 * 在 VIEW_NODE 装入四条 show 命令；最后注册 aspath_filter_handlers 补全处理器。无返回值。 */
/* Register functions. */
void bgp_filter_init(void)
{
	install_node(&as_list_node);

	install_element(CONFIG_NODE, &bgp_as_path_cmd);
	install_element(CONFIG_NODE, &no_bgp_as_path_cmd);
	install_element(CONFIG_NODE, &no_bgp_as_path_all_cmd);

	install_element(VIEW_NODE, &show_bgp_as_path_access_list_cmd);
	install_element(VIEW_NODE, &show_ip_as_path_access_list_cmd);
	install_element(VIEW_NODE, &show_bgp_as_path_access_list_all_cmd);
	install_element(VIEW_NODE, &show_ip_as_path_access_list_all_cmd);

	cmd_variable_handler_register(aspath_filter_handlers);
}

/* 【函数·模块复位】进程退出/复位时调用，释放所有 AS-path 过滤表。
 * 用 next 暂存后继后逐个 as_list_delete，避免遍历中释放当前节点导致悬空。
 * 末尾用 assert 断言 as_list_master.str 的头尾都已清空，确保内存全部回收干净。无返回值。 */
void bgp_filter_reset(void)
{
	struct as_list *aslist;
	struct as_list *next;

	for (aslist = as_list_master.str.head; aslist; aslist = next) {
		next = aslist->next;
		as_list_delete(aslist);
	}

	assert(as_list_master.str.head == NULL);
	assert(as_list_master.str.tail == NULL);
}
