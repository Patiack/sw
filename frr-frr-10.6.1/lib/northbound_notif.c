// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * December 1 2024, Christian Hopps <chopps@labn.net>
 *
 * Copyright (c) 2024, LabN Consulting, L.L.C.
 *
 */
/* ============================================================================
 * 【文件导读】northbound_notif.c —— 北向“运维状态变更”通知框架
 * ============================================================================
 * 【它在 FRR 里干什么】本文件实现操作数据(operational state)变更的通知 / 遥测框架：
 * 子系统在运行中修改了运维状态（如接口 / 邻居状态）时，调用 nb_notif_add 或
 * nb_notif_delete 登记“某条 YANG 路径被增改 / 被删除”；本框架把大量变更去重、按前缀
 * 压缩（更泛化的路径吸收更具体的路径），再交给 mgmt_be_client 批量发给外部客户端
 * （sysrepo / gNMI 等）。
 *
 * 【核心思路】不逐条立即上报，而是把待发变更按“一轮查询 / 一次发送”为单位攒成组，由
 * 定时器(timer_walk_*)稍后统一遍历实际状态再发送——因为通知应在“发送时刻”查询状态，
 * 而非在“被告知变更的时刻”，以免发出已过期的中间态。
 *
 * 【核心数据结构】
 *   struct op_change        : 一条变更（仅一条 YANG 路径，柔性数组存放）。
 *   op_changes (RB 树)      : 按路径排序的变更集合；adds / dels 两棵分别记增改与删除。
 *   struct op_changes_group : 一组待发送的变更(adds + dels)，构成队列中的一项。
 *   struct nb_notif_walk_args: 一次遍历过程中的上下文(group、当前 YANG tree)。
 *
 * 【模块关系】依赖 northbound.h（nb_oper_walk 等）、mgmt_be_client（实际发送）、
 * typesafe.h 与 darr（动态数组、队列）。对外 API：nb_notif_add / nb_notif_delete(_f)、
 * nb_op_update*(_f)、nb_notif_set_filters、nb_notif_get_state、nb_notif_init /
 * nb_notif_terminate。
 *
 * 【阅读顺序】宏与全局锁 → op_change / RB 树 → 变更压缩(__op_change_add_del 及辅助)
 * → 对外登记 API → 组队列与遍历定时器(timer_walk_*) → 初始化 / 终止。
 * ============================================================================
 */

/* FRR 与北向基础头文件：debug.h / lib_errors.h 提供调试与错误码；typesafe.h 提供
 * PREDECL_LIST 等类型安全容器宏；northbound.h 提供 nb_oper_walk 等北向接口；
 * mgmt_be_client.h 提供真正把通知发给外部客户端的发送函数。
 */
#include <zebra.h>
#include "debug.h"
#include "lib_errors.h"
#include "typesafe.h"
#include "northbound.h"
#include "mgmt_be_client.h"

/* 【宏】调试与错误日志：_dbg 走 DEBUGD 打到 nb_dbg_notif 调试开关（自动带函数名）；
 * _log_err 用 zlog_err 打错误。两者都前置 "NB_OP_CHANGE: 函数名:" 便于过滤。
 */
#define _dbg(fmt, ...)	   DEBUGD(&nb_dbg_notif, "NB_OP_CHANGE: %s: " fmt, __func__, ##__VA_ARGS__)
#define _log_err(fmt, ...) zlog_err("NB_OP_CHANGE: %s: ERROR: " fmt, __func__, ##__VA_ARGS__)

/* 【常量】遍历定时器 / 重试延迟：10 毫秒。用于把变更攒一小会儿再统一遍历发送，并在
 * 发送失败时以此间隔重试。
 */
#define NB_NOTIF_TIMER_MSEC (10) /* 10msec */

/* 【压缩语义】本框架对运维状态变更做“前缀包含”级别的去重：更泛化(更短前缀)的 add 会
 * 吸收更具体的 add、并抵消更具体的 delete（因为 add 按“替换”上报，能覆盖下挂状态）；
 * 更泛化的 delete 会吸收更具体的 delete，并把更具体的 add 转成 add-replace。下面英文
 * 注释详列了各类情形；务必记住：状态是在“发送通知时”而非“被告知变更时”查询的。
 */
/*
 * ADDS:
 * - Less specific:
 *   - Any new add will cause more specific pending adds to be dropped and equal
 *     or more specific deletes to be dropped.
 * - More specific:
 *   - Ignore any new add that is the same or more specific than an existing add.
 *   - A new add that is more specific than a delete should change the delete
 *     into an add query (since adds are reported as a replace).
 *
 * DELETES:
 * - Less specific:
 *   - Any new delete will cause more specific pending deletes to be dropped and
 *     equal or more specific adds to be dropped.
 * - More specific:
 *   - Ignore new deletes that are the same or more specific than existing
 *     deletes.
 *   - A new delete that is more specific than an add can be dropped since we
 *     use replacement methodology for the add.
 *
 * One thing we have to pay close attention to is that the state is going to be
 * queried when the notification sent, not when we are told of the change.
 */

/* 【内存类型】为本模块的三种对象登记 MTYPE，便于内存统计与泄漏定位：单条变更、变更组、
 * 遍历上下文。
 */
DEFINE_MTYPE_STATIC(LIB, OP_CHANGE, "NB Oper Change");
DEFINE_MTYPE_STATIC(LIB, OP_CHANGES_GROUP, "NB Oper Changes Group");
DEFINE_MTYPE_STATIC(LIB, NB_NOTIF_WALK_ARGS, "NB Notify Oper Walk");

/* 【结构】op_change —— 一条运维状态变更，即一条 YANG 路径。link 是 RB 树挂接点；
 * path 是柔性数组，紧随结构体尾部存放路径字符串（分配时按 strlen+1 一并申请）。
 */
struct op_change {
	RB_ENTRY(op_change) link;
	char path[];
};

/*
 * RB tree for op_change
 */
/* 【RB 树】op_changes —— 以路径字符串排序的变更集合（红黑树）。op_change_cmp 用
 * pathcmp 比较路径；RB_PROTOTYPE / RB_GENERATE 展开出插入、查找、遍历等操作。
 */
static int op_change_cmp(const struct op_change *e1, const struct op_change *e2);
RB_HEAD(op_changes, op_change);
RB_PROTOTYPE(op_changes, op_change, link, op_change_cmp)
RB_GENERATE(op_changes, op_change, link, op_change_cmp)

/* 【全局状态】
 *   _nb_notif_lock / nb_notif_lock : 全局变更表的互斥锁（nb_notif_lock 为 NULL 表示
 *                                    单线程模式；见 nb_notif_enable_multi_thread）。
 *   nb_notif_adds / nb_notif_dels  : 当前累积的“增改”与“删除”变更 RB 树。
 *   nb_notif_master                : 用于调度遍历定时器的事件循环。
 *   nb_notif_timer                 : 遍历定时器事件句柄。
 *   nb_notif_walk                  : 当前进行中的运维状态遍历(nb_oper_walk)句柄。
 *   nb_notif_filters               : 通知过滤器选择器(路径)动态数组。
 */
pthread_mutex_t _nb_notif_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t *nb_notif_lock;

struct op_changes nb_notif_adds = RB_INITIALIZER(&nb_notif_adds);
struct op_changes nb_notif_dels = RB_INITIALIZER(&nb_notif_dels);
struct event_loop *nb_notif_master;
struct event *nb_notif_timer;
void *nb_notif_walk;

const char **nb_notif_filters;

/*
 * We maintain a queue of change lists one entry per query and notification send
 * action
 */
/* 【结构】op_changes_group —— 队列中的一组待发送变更。item 是链表节点；adds / dels
 * 为该组的增改与删除集合；cur_changes / cur_change 是遍历游标（先发 dels 再发 adds）；
 * refer_id 用于发送初始的 notify dump（区分快照轮次）。变更组由 op_changes_queue
 * 链表串成 FIFO 队列，逐组发送。
 */
PREDECL_LIST(op_changes_queue);
struct op_changes_group {
	struct op_changes_queue_item item;
	struct op_changes adds;
	struct op_changes dels;
	struct op_changes *cur_changes; /* used when walking */
	struct op_change *cur_change;	/* "    "     " */
	uint64_t refer_id;		/* for sending initial notify dump */
};

DECLARE_LIST(op_changes_queue, struct op_changes_group, item);
static struct op_changes_queue_head op_changes_queue;

/* 【结构】nb_notif_walk_args —— 一次运维状态遍历的上下文：group 指向当前正在处理的
 * 变更组；tree 为遍历时使用的 YANG 数据树。作为 event 参数在各 timer_walk_* 间传递。
 */
struct nb_notif_walk_args {
	struct op_changes_group *group;
	struct lyd_node *tree;
};

/* 【函数原型】设置遍历定时器（实现见文件末尾）：把一条新的运维变更排入待发并安排
 * 定时器。前置声明供 __op_change_add_del 提前调用。
 */
static void nb_notif_set_walk_timer(void);


/* 【函数】pathncmp —— 比较两条 YANG 路径的前 n 个字符，返回 <0 / 0 / >0。特殊之处：
 * 单引号与双引号被视为等价（抹平 list key 引号差异）。用于“前缀是否为祖先路径”判断。
 */
static int pathncmp(const char *s1, const char *s2, size_t n)
{
	size_t i = 0;

	while (i < n && *s1 && *s2) {
		char c1 = *s1;
		char c2 = *s2;

		if ((c1 == '\'' && c2 == '\"') || (c1 == '\"' && c2 == '\'')) {
			s1++;
			s2++;
			i++;
			continue;
		}
		if (c1 != c2)
			return (unsigned char)c1 - (unsigned char)c2;
		s1++;
		s2++;
		i++;
	}
	if (i < n)
		return (unsigned char)*s1 - (unsigned char)*s2;
	return 0;
}

/* 【函数】pathcmp —— 比较两条完整 YANG 路径，返回 <0 / 0 / >0；同样把单引号与双引号
 * 视为等价。作为 RB 树的排序键。
 */
static int pathcmp(const char *s1, const char *s2)
{
	while (*s1 && *s2) {
		char c1 = *s1;
		char c2 = *s2;

		if ((c1 == '\'' && c2 == '\"') || (c1 == '\"' && c2 == '\'')) {
			s1++;
			s2++;
			continue;
		}
		if (c1 != c2)
			return (unsigned char)c1 - (unsigned char)c2;
		s1++;
		s2++;
	}
	return (unsigned char)*s1 - (unsigned char)*s2;
}


/* 【函数】op_change_cmp —— RB 树比较器，转调 pathcmp 比较两条变更的路径。 */
static int op_change_cmp(const struct op_change *e1, const struct op_change *e2)
{
	return pathcmp(e1->path, e2->path);
}

/* 【函数】op_change_alloc —— 分配一条 op_change：按路径长度(含结尾 NUL)在结构体尾部
 * 的柔性数组里直接存放路径串。返回新对象（尚未挂入任何树）。
 */
static struct op_change *op_change_alloc(const char *path)
{
	struct op_change *note;
	size_t ssize = strlen(path) + 1;

	note = XMALLOC(MTYPE_OP_CHANGE, sizeof(*note) + ssize);
	memset(note, 0, sizeof(*note));
	strlcpy(note->path, path, ssize);

	return note;
}

/* 【函数】op_change_free —— 释放一条 op_change（调用前应先从树中移除）。 */
static void op_change_free(struct op_change *note)
{
	XFREE(MTYPE_OP_CHANGE, note);
}

/* 【函数】__op_changes_group_push —— 把当前累积的 nb_notif_adds / nb_notif_dels 整体
 * 挪进一个新的变更组并挂到队列尾，随后把这两棵树重置为空；若当前没有任何变更则返回
 * NULL。refer_id 随组保存（发送初始快照 dump 时用）。调用者必须已持有锁。
 */
/**
 * __op_changes_group_push() - Save the current set of changes on the queue.
 * @refer_id: sets refer_id usually 0.
 *
 * This function will save the current set of changes on the queue and
 * initialize a new set of changes.
 *
 * The lock must be held during this call.
 */
static struct op_changes_group *__op_changes_group_push(uint64_t refer_id)
{
	struct op_changes_group *changes;

	if (RB_EMPTY(op_changes, &nb_notif_adds) && RB_EMPTY(op_changes, &nb_notif_dels))
		return NULL;

	_dbg("pushing current oper changes onto queue");

	changes = XCALLOC(MTYPE_OP_CHANGES_GROUP, sizeof(*changes));
	changes->adds = nb_notif_adds;
	changes->dels = nb_notif_dels;
	changes->refer_id = refer_id;
	op_changes_queue_add_tail(&op_changes_queue, changes);

	RB_INIT(op_changes, &nb_notif_adds);
	RB_INIT(op_changes, &nb_notif_dels);

	return changes;
}

/* 【函数】op_changes_group_free —— 释放一个变更组：遍历其 adds / dels 释放全部
 * op_change，再释放组本身。
 */
static void op_changes_group_free(struct op_changes_group *group)
{
	struct op_change *e, *next;

	RB_FOREACH_SAFE (e, op_changes, &group->adds, next) {
		RB_REMOVE(op_changes, &group->adds, e);
		op_change_free(e);
	}
	RB_FOREACH_SAFE (e, op_changes, &group->dels, next) {
		RB_REMOVE(op_changes, &group->dels, e);
		op_change_free(e);
	}
	XFREE(MTYPE_OP_CHANGES_GROUP, group);
}

/* 【函数】__find_less_specific —— 在有序集合 head 中找出 note 路径的“更泛化(更短前缀)”
 * 祖先项，找到则返回，否则返回 NULL。实现：用 RB_NFIND 取“等于或更具体”的项，再无
 * 条件取前驱；若为空则取 RB_MAX（最大项）。因为相等项必被 RB_NFIND 命中并被前驱跳过，
 * 故此处断言二者长度不相等。返回的项路径是 note 路径的严格前缀。
 */
static struct op_change *__find_less_specific(struct op_changes *head, struct op_change *note)
{
	struct op_change *e;
	size_t plen;

	/*
	 * RB_NFIND finds equal or greater (more specific) than the key,
	 * so the previous node will be a less specific or no match that
	 * sorts earlier. We want to find when we are a more specific
	 * match.
	 */
	e = RB_NFIND(op_changes, head, note);
	if (e)
		e = RB_PREV(op_changes, e);
	else
		e = RB_MAX(op_changes, head);
	if (!e)
		return NULL;
	plen = strlen(e->path);
	if (pathncmp(e->path, note->path, plen))
		return NULL;
	/* equal would have been returned from RB_NFIND() then we went RB_PREV */
	assert(strlen(note->path) != plen);
	return e;
}

/* 【函数】__drop_eq_or_more_specific —— 从 next 起，在有序集合 head 中删除所有“路径以
 * path(长度 plen) 为前缀”的项（即等于或比 path 更具体者），直到遇到前缀不再匹配为止。
 * 用于新变更吸收掉同前缀的既有变更。
 */
static void __drop_eq_or_more_specific(struct op_changes *head, const char *path, int plen,
				       struct op_change *next)
{
	struct op_change *e;

	for (e = next; e != NULL; e = next) {
		/* if the prefix no longer matches we are done */
		if (pathncmp(path, e->path, plen))
			break;
		_dbg("dropping more specific %s: %s", head == &nb_notif_adds ? "add" : "delete",
		     e->path);
		next = RB_NEXT(op_changes, e);
		RB_REMOVE(op_changes, head, e);
		op_change_free(e);
	}
}

/* 【函数】__op_change_add_del —— 变更压缩的核心。把一条 path 登记到 this_head(add 或
 * delete)：1) 若已被更泛化的同类变更覆盖，则直接丢弃；2) 若存在更泛化的“另一类”变更，
 * 按规则处理——本条是 delete 时保持原样丢弃，本条是 add 时把那条更泛化的 delete 转成
 * add（递归）；3) 否则插入本树，并删除所有被本条覆盖的同前缀 / 更具体的同类与异类变更。
 * 最后安排遍历定时器。this_head / other_head 的取值决定是 add 还是 delete 语义。
 */
static void __op_change_add_del(const char *path, struct op_changes *this_head,
				struct op_changes *other_head)
{
	/* find out if this has been subsumed or will subsume */

	const char *op = this_head == &nb_notif_adds ? "add" : "delete";
	struct op_change *note = op_change_alloc(path);
	struct op_change *next, *e;
	int plen;

	_dbg("processing oper %s change path: %s", op, path);

	/*
	 * See if we are already covered by a more general `op`.
	 */
	e = __find_less_specific(this_head, note);
	if (e) {
		_dbg("%s path already covered by: %s", op, e->path);
		op_change_free(note);
		return;
	}

	/*
	 * Handle having a less-specific `other op`.
	 */
	e = __find_less_specific(other_head, note);
	if (e) {
		if (this_head == &nb_notif_dels) {
			/*
			 * If we have a less-specific add then drop this
			 * more-specific delete as the add-replace will remove
			 * this missing state.
			 */
			_dbg("delete path already covered add-replace: %s", e->path);
		} else {
			/*
			 * If we have a less-specific delete, convert the delete
			 * to an add, and drop this more-specific add. The new
			 * less-specific add will pick up the more specific add
			 * during the walk and as adds are processed as replaces
			 * any other existing state that was to be deleted will
			 * still be deleted (unless it also returns) by the replace.
			 */
			_dbg("add covered, converting covering delete to add-replace: %s", e->path);
			RB_REMOVE(op_changes, other_head, e);
			__op_change_add_del(e->path, &nb_notif_adds, &nb_notif_dels);
			op_change_free(e);
		}
		op_change_free(note);
		return;
	}

	e = RB_INSERT(op_changes, this_head, note);
	if (e) {
		_dbg("path already in %s tree: %s", op, path);
		op_change_free(note);
		return;
	}

	_dbg("scanning for subsumed or subsuming: %s", path);

	plen = strlen(path);

	next = RB_NEXT(op_changes, note);
	__drop_eq_or_more_specific(this_head, path, plen, next);

	/* Drop exact match or more specific `other op` */
	next = RB_NFIND(op_changes, other_head, note);
	__drop_eq_or_more_specific(other_head, path, plen, next);

	nb_notif_set_walk_timer();
}

/* 【函数】nb_notif_add —— 对外入口：登记一条路径为“增改”。加锁后交给
 * __op_change_add_del(adds 树, dels 树)。nb_notif_lock 为 NULL 时不加锁（单线程）。
 */
void nb_notif_add(const char *path)
{
	if (nb_notif_lock)
		pthread_mutex_lock(nb_notif_lock);

	__op_change_add_del(path, &nb_notif_adds, &nb_notif_dels);

	if (nb_notif_lock)
		pthread_mutex_unlock(nb_notif_lock);
}

/* 【函数】nb_notif_addf —— nb_notif_add 的 printf 风格变体：用 darr_vsprintf 拼出绝对
 * 路径（必须以 '/' 起始，否则断言失败）再调用 nb_notif_add，随后释放临时串。
 */
void nb_notif_addf(const char *path, ...)
{
	char *abs_path;
	va_list ap;

	va_start(ap, path);
	abs_path = darr_vsprintf(path, ap);
	va_end(ap);

	assert(abs_path[0] == '/');
	nb_notif_add(abs_path);
	darr_free(abs_path);
}


/* 【函数】nb_notif_delete —— 对外入口：登记一条路径为“删除”。加锁后交给
 * __op_change_add_del(dels 树, adds 树)，两类树的角色与 nb_notif_add 相反。
 */
void nb_notif_delete(const char *path)
{
	if (nb_notif_lock)
		pthread_mutex_lock(nb_notif_lock);

	__op_change_add_del(path, &nb_notif_dels, &nb_notif_adds);

	if (nb_notif_lock)
		pthread_mutex_unlock(nb_notif_lock);
}

/* 【函数】nb_notif_deletef —— nb_notif_delete 的 printf 风格变体（拼出绝对路径后调用）。 */
void nb_notif_deletef(const char *path, ...)
{
	char *abs_path;
	va_list ap;

	va_start(ap, path);
	abs_path = darr_vsprintf(path, ap);
	va_end(ap);

	assert(abs_path[0] == '/');
	nb_notif_delete(abs_path);
	darr_free(abs_path);
}


/* ---------------------------------------------- */
/* User functions to update and delete oper state */
/* ---------------------------------------------- */

/* 【函数】nb_op_update —— 先经 yang_state_new(tree, path, value) 在操作状态里增改数据，
 * 再对该路径登记“增改”通知。path 若非 '/' 起始，则用 lyd_path 取得新建节点的绝对路径。
 * 返回新建的 dnode（其生命周期由传入的 tree 管理）。
 */
struct lyd_node *nb_op_update(struct lyd_node *tree, const char *path, const char *value)
{
	struct lyd_node *dnode;
	const char *abs_path = NULL;

	_dbg("updating path: %s with value: %s", path, value);

	dnode = yang_state_new(tree, path, value);

	if (path[0] == '/')
		abs_path = path;
	else
		abs_path = lyd_path(dnode, LYD_PATH_STD, NULL, 0);

	nb_notif_add(abs_path);

	if (abs_path != path)
		free((char *)abs_path);

	return dnode;
}

/* 【函数】nb_op_update_delete —— 先经 yang_state_delete(tree, path) 删除数据，再登记
 * “删除”通知。path 可为绝对或相对：非 '/' 起始时，以 tree 的绝对路径为前缀拼出绝对
 * 路径（内部区分静态串与动态数组，以便按正确方式释放）。
 */
void nb_op_update_delete(struct lyd_node *tree, const char *path)
{
	char *abs_path = NULL;

	_dbg("deleting path: %s", path);

	if (path && path[0] == '/')
		abs_path = (char *)path;
	else {
		assert(tree);
		abs_path = lyd_path(tree, LYD_PATH_STD, NULL, 0);
		assert(abs_path);
		if (path) {
			char *tmp = darr_strdup(abs_path);

			free(abs_path);
			abs_path = tmp;
			if (*darr_last(abs_path) != '/')
				darr_in_strcat(abs_path, "/");
			assert(abs_path); /* silence bad CLANG NULL warning */
			darr_in_strcat(abs_path, path);
		}
	}

	yang_state_delete(tree, path);

	nb_notif_delete(abs_path);

	if (abs_path != path) {
		if (path)
			darr_free(abs_path);
		else
			free(abs_path);
	}
}

PRINTFRR(2, 0)
/* 【函数】nb_op_update_vpathf —— nb_op_update 的 va_list 版：用 darr_vsprintf 由
 * path_fmt 与 ap 生成路径。PRINTFRR(2, 0) 提示编译器把第 2 个参数(path_fmt)当格式串
 * 做校验。返回新建的 dnode。
 */
struct lyd_node *nb_op_update_vpathf(struct lyd_node *tree, const char *path_fmt, const char *value,
				     va_list ap)
{
	struct lyd_node *dnode;
	char *path;

	path = darr_vsprintf(path_fmt, ap);
	dnode = nb_op_update(tree, path, value);
	darr_free(path);

	return dnode;
}

/* 【函数】nb_op_update_pathf —— nb_op_update_vpathf 的可变参数包装。 */
struct lyd_node *nb_op_update_pathf(struct lyd_node *tree, const char *path_fmt, const char *value,
				    ...)
{
	struct lyd_node *dnode;
	va_list ap;

	va_start(ap, value);
	dnode = nb_op_update_vpathf(tree, path_fmt, value, ap);
	va_end(ap);

	return dnode;
}

PRINTFRR(2, 0)
/* 【函数】nb_op_update_delete_vpathf —— nb_op_update_delete 的 va_list 版：路径由
 * path_fmt 与 ap 生成。
 */
void nb_op_update_delete_vpathf(struct lyd_node *tree, const char *path_fmt, va_list ap)
{
	char *path;

	path = darr_vsprintf(path_fmt, ap);
	nb_op_update_delete(tree, path);
	darr_free(path);
}

/* 【函数】nb_op_update_delete_pathf —— nb_op_update_delete_vpathf 的可变参数包装。 */
void nb_op_update_delete_pathf(struct lyd_node *tree, const char *path_fmt, ...)
{
	va_list ap;

	va_start(ap, path_fmt);
	nb_op_update_delete_vpathf(tree, path_fmt, ap);
	va_end(ap);
}


PRINTFRR(3, 0)
/* 【函数】nb_op_vupdatef —— nb_op_update 的变体：value 由 val_fmt 加 va_list 生成。
 * PRINTFRR(3, 0) 说明被校验的格式串是第 3 个参数(val_fmt)。返回新建的 dnode。
 */
struct lyd_node *nb_op_vupdatef(struct lyd_node *tree, const char *path, const char *val_fmt,
				va_list ap)
{
	struct lyd_node *dnode;
	char *value;

	value = darr_vsprintf(val_fmt, ap);
	dnode = nb_op_update(tree, path, value);
	darr_free(value);

	return dnode;
}


/* 【函数】nb_op_updatef —— nb_op_vupdatef 的可变参数包装。 */
struct lyd_node *nb_op_updatef(struct lyd_node *tree, const char *path, const char *val_fmt, ...)
{
	struct lyd_node *dnode;
	va_list ap;

	va_start(ap, val_fmt);
	dnode = nb_op_vupdatef(tree, path, val_fmt, ap);
	va_end(ap);

	return dnode;
}

/* 【函数】op_changes_group_next —— 从队列取出下一个变更组并设定遍历游标：若队列为空，
 * 先把当前累积的变更压成一个组再取。取到后游标先指向 dels（先删后增），若 dels 为空再
 * 指向 adds。返回组，或 NULL 表示暂无可发内容。加锁保护队列操作。
 */
static struct op_changes_group *op_changes_group_next(void)
{
	struct op_changes_group *group;

	if (nb_notif_lock)
		pthread_mutex_lock(nb_notif_lock);

	group = op_changes_queue_pop(&op_changes_queue);
	if (!group) {
		__op_changes_group_push(0);
		group = op_changes_queue_pop(&op_changes_queue);
	}

	if (nb_notif_lock)
		pthread_mutex_unlock(nb_notif_lock);

	if (!group)
		return NULL;

	group->cur_changes = &group->dels;
	group->cur_change = RB_MIN(op_changes, group->cur_changes);
	if (!group->cur_change) {
		group->cur_changes = &group->adds;
		group->cur_change = RB_MIN(op_changes, group->cur_changes);
		assert(group->cur_change);
	}
	return group;
}

/* ---------------------------- */
/* Query for changes and notify */
/* ---------------------------- */

/* 【函数原型】遍历状态机的三个回调：timer_walk_continue 推进遍历、timer_walk_abort
 * 失败重试、timer_walk_done 收尾释放。前置声明供前面的 oper_walk_done 调用。
 */
static void timer_walk_abort(struct nb_notif_walk_args *args);
static void timer_walk_continue(struct event *event);
static void timer_walk_done(struct nb_notif_walk_args *args);

/* 【函数】__next_change —— 返回当前变更在同组内的下一个，并把“当前变更”从所属树移除
 * 并释放（这样一旦失败重试，能从头重新处理）。返回下一个，或 NULL 表示本类已处理完。
 */
static struct op_change *__next_change(struct op_changes_group *group)
{
	struct op_change *next = RB_NEXT(op_changes, group->cur_change);

	/* Remove and free current so retry works */
	RB_REMOVE(op_changes, group->cur_changes, group->cur_change);
	op_change_free(group->cur_change);
	return next;
}

/* 【函数】__next_group —— 结束当前组（释放之），再取下一个组。返回下一组或 NULL。 */
static struct op_changes_group *__next_group(struct op_changes_group *group)
{
	_dbg("done with oper-path collection for group");
	op_changes_group_free(group);
	return op_changes_group_next();
}

/* 【函数】oper_walk_done —— nb_oper_walk 的回调：对某条路径收集完实际运维状态后调用。
 * 依据 ret：NB_ERR_NOT_FOUND 视为成功（无该状态）；NB_OK 时经 mgmt_be_client 发送
 * “增改(替换)”通知；其它错误则记录并中止本次遍历(转重试)。发送后推进到下一条变更，
 * 必要时切到下一组；最后安排下一次遍历（先让其它事件有机会运行）。返回处理结果。
 * 注意：yield 时不允许成批发送（此处断言 ret != NB_YIELD），以保证任一 patch 都是完整
 * 的一次编辑。
 */
static enum nb_error oper_walk_done(const struct lyd_node *tree, void *arg, enum nb_error ret)
{
	struct nb_notif_walk_args *args = arg;
	struct op_changes_group *group = args->group;
	const char *path = group->cur_change->path;

	/* we don't send batches when yielding as we need completed edit in any patch */
	assert(ret != NB_YIELD);

	if (ret == NB_ERR_NOT_FOUND) {
		_dbg("Path not found while walking oper tree: %s", path);
		ret = NB_OK;
	} else if (ret != NB_OK) {
error:
		_log_err("Error notifying for datastore path: %s: %s", path, nb_err_name(ret));

		timer_walk_abort(args);
		goto done;
	} else {
		_dbg("Done with oper-path collection for path: %s", path);

		/* Send the add (replace) notification */
		if (mgmt_be_send_ds_replace_notification(path, tree, group->refer_id)) {
			_log_err("Error sending notification message for path: %s", path);
			ret = NB_ERR;
			goto error;
		}
	}

	/*
	 * Advance to next change.
	 */

	group->cur_change = __next_change(group);
	if (!group->cur_change) {
		args->group = __next_group(group);
		if (!args->group) {
			timer_walk_done(args);
			goto done;
		}
	}

	/* Run next walk after giving other events a shot to run */
	event_add_timer_msec(nb_notif_master, timer_walk_continue, args, 0, &nb_notif_timer);
done:
	/* Done with current walk and scheduled next one if there is more */
	nb_notif_walk = NULL;

	return ret;
}

/* 【函数】nb_notify_delete_changes —— 遍历当前组 dels 树里的每条变更，逐条发送“删除”
 * 通知并顺带移除释放。返回 0 成功、1 发送失败（交由上层重试）。
 */
static int nb_notify_delete_changes(struct nb_notif_walk_args *args)
{
	struct op_changes_group *group = args->group;

	group->cur_change = RB_MIN(op_changes, group->cur_changes);
	while (group->cur_change) {
		if (mgmt_be_send_ds_delete_notification(group->cur_change->path)) {
			_log_err("Error sending delete notification message for path: %s",
				 group->cur_change->path);
			return 1;
		}
		group->cur_change = __next_change(group);
	}
	return 0;
}

/* 【函数】timer_walk_continue —— 遍历推进器（定时器回调）：先对当前组发送所有 delete
 * 通知；dels 发完后转到 adds，取第一条 add 变更发起一次 nb_oper_walk（真正收集状态后
 * 由 oper_walk_done 发送）。若无 add 则切到下一组继续。
 */
static void timer_walk_continue(struct event *event)
{
	struct nb_notif_walk_args *args = EVENT_ARG(event);
	struct op_changes_group *group = args->group;
	const char *path;
	int ret;

	/*
	 * Notify about deletes until we have add changes to collect.
	 */
	while (group->cur_changes == &group->dels) {
		ret = nb_notify_delete_changes(args);
		if (ret) {
			timer_walk_abort(args);
			return;
		}

		/* after deletes advance to adds */
		group->cur_changes = &group->adds;
		group->cur_change = RB_MIN(op_changes, group->cur_changes);
		if (group->cur_change)
			break;

		args->group = __next_group(group);
		if (!args->group) {
			timer_walk_done(args);
			return;
		}
		group = args->group;
	}

	path = group->cur_change->path;
	_dbg("starting next oper-path replace walk for path: %s", path);
	nb_notif_walk = nb_oper_walk(path, NULL, 0, false, NULL, NULL, oper_walk_done, args);
}

/* 【函数】timer_walk_start —— 定时器启动回调：取下一个变更组，为其分配遍历上下文
 * (nb_notif_walk_args) 并挂到 event 参数上，然后进入 timer_walk_continue 开始处理。
 */
static void timer_walk_start(struct event *event)
{
	struct op_changes_group *group;
	struct nb_notif_walk_args *args;

	_dbg("oper-state change notification timer fires");

	group = op_changes_group_next();
	if (!group) {
		_dbg("no oper changes to notify");
		return;
	}

	args = XCALLOC(MTYPE_NB_NOTIF_WALK_ARGS, sizeof(*args));
	args->group = group;

	EVENT_ARG(event) = args;
	timer_walk_continue(event);
}

/* 【函数】timer_walk_abort —— 发送失败时的处理：记日志并安排重试定时器，在
 * NB_NOTIF_TIMER_MSEC 毫秒后再调 timer_walk_continue。遍历上下文 args 保留以便复用。
 */
static void timer_walk_abort(struct nb_notif_walk_args *args)
{
	_dbg("Failed notifying datastore changes, will retry");

	_dbg("oper-state notify setting retry timer to fire in: %d msec ", NB_NOTIF_TIMER_MSEC);
	event_add_timer_msec(nb_notif_master, timer_walk_continue, args, NB_NOTIF_TIMER_MSEC,
			     &nb_notif_timer);
}

/* 【函数】timer_walk_done —— 全部变更发送完毕：断言当前已无待处理组，释放遍历上下文。 */
static void timer_walk_done(struct nb_notif_walk_args *args)
{
	_dbg("Finished notifying for all datastore changes");
	assert(!args->group);
	XFREE(MTYPE_NB_NOTIF_WALK_ARGS, args);
}

/* 【函数】nb_notif_set_walk_timer —— 若已有遍历在进行、或定时器已排好，则直接返回；
 * 否则安排 timer_walk_start 在 NB_NOTIF_TIMER_MSEC 毫秒后触发（把本轮变更先攒一会儿，
 * 合并后再统一发送）。
 */
static void nb_notif_set_walk_timer(void)
{
	if (nb_notif_walk) {
		_dbg("oper-state walk already in progress.");
		return;
	}
	if (event_is_scheduled(nb_notif_timer)) {
		_dbg("oper-state notification timer already set.");
		return;
	}

	_dbg("oper-state notification setting timer to fire in: %d msec ", NB_NOTIF_TIMER_MSEC);
	event_add_timer_msec(nb_notif_master, timer_walk_start, NULL, NB_NOTIF_TIMER_MSEC,
			     &nb_notif_timer);
}

/* 【函数】nb_notif_set_filters —— 设置 / 合并通知过滤器(选择器)列表。replace 为真时先
 * 清空旧列表；随后把 selectors 去重后按升序并入 nb_notif_filters。注意：入参 selectors
 * 数组及其元素的所有权会被接管并释放（重复项就地 free，整体最后 darr_free）。
 */
void nb_notif_set_filters(const char **selectors, bool replace)
{
	// struct nb_node **np, **nb_nodes;
	const char **csp;
	bool exists;
	int before;

	_dbg("existing notif_filters: %pSAd", nb_notif_filters);

	if (replace)
		darr_free_free(nb_notif_filters);

	/* Add in sorted, eliminating duplicates */
	darr_foreach_p (selectors, csp) {
		if (!darr_len(nb_notif_filters)) {
			*darr_append(nb_notif_filters) = *csp;
			continue;
		}
		exists = false;
		before = darr_str_search_ceil(nb_notif_filters, *csp, &exists);
		if (exists)
			darr_free(*csp);
		else
			*darr_insert(nb_notif_filters, before) = *csp;
	}

	_dbg("new notif_filters: %pSAd", nb_notif_filters);

	darr_free(selectors);
}

/* 【函数】nb_notif_get_state —— 请求当前运维状态的完整快照：先把已有变更压成一组，
 * 再为每个 selector 登记一条 add（触发对其下挂状态的遍历发送），最后把新变更压成一个
 * 带 refer_id 的组。加锁执行；结束释放 selectors。
 */
void nb_notif_get_state(const char **selectors, uint64_t refer_id)
{
	uint i;

	if (nb_notif_lock)
		pthread_mutex_lock(nb_notif_lock);

	/* push any current changes */
	__op_changes_group_push(0);

	/* add "add" notifications for each selector */
	darr_foreach_i (selectors, i)
		__op_change_add_del(selectors[i], &nb_notif_adds, &nb_notif_dels);

	/* push new "changes" */
	__op_changes_group_push(refer_id);

	if (nb_notif_lock)
		pthread_mutex_unlock(nb_notif_lock);

	darr_free_free(selectors);
}

/* 【函数】nb_notif_enable_multi_thread —— 打开多线程模式：把 nb_notif_lock 指向全局
 * 互斥锁，此后所有变更登记 / 遍历均加锁。应在进入多线程前调用一次。
 */
void nb_notif_enable_multi_thread(void)
{
	nb_notif_lock = &_nb_notif_lock;
}

/* 【函数】nb_notif_init —— 初始化：记录事件循环 nb_notif_master 并初始化变更组队列。
 * 进程启动时调用一次。
 */
void nb_notif_init(struct event_loop *tm)
{
	nb_notif_master = tm;
	op_changes_queue_init(&op_changes_queue);
}

/* 【函数】nb_notif_terminate —— 清理：取消定时器，结束进行中的遍历并释放其上下文，
 * 释放队列中剩余的变更组，最后释放过滤器数组。进程退出时调用。
 */
void nb_notif_terminate(void)
{
	struct nb_notif_walk_args *args = nb_notif_timer ? EVENT_ARG(nb_notif_timer) : NULL;
	struct op_changes_group *group;

	_dbg("terminating: timer: %p timer arg: %p walk %p", nb_notif_timer, args, nb_notif_walk);

	event_cancel(&nb_notif_timer);

	if (nb_notif_walk) {
		/* Grab walk args from walk if active. */
		args = nb_oper_walk_finish_arg(nb_notif_walk);
		nb_oper_cancel_walk(nb_notif_walk);
		nb_notif_walk = NULL;
	}
	if (args) {
		op_changes_group_free(args->group);
		XFREE(MTYPE_NB_NOTIF_WALK_ARGS, args);
	}

	while ((group = op_changes_group_next()))
		op_changes_group_free(group);

	darr_free_free(nb_notif_filters);
}
