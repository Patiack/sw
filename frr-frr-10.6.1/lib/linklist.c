// SPDX-License-Identifier: GPL-2.0-or-later
/* Generic linked list routine.
 * Copyright (C) 1997, 2000 Kunihiro Ishiguro
 */

/* ============================================================================
 * 【文件导读】linklist.c —— FRR 自研的非类型安全双向链表
 * ============================================================================
 * 【它是什么】这是全 FRR(以及整个项目)最常用的基础容器之一，实现了
 * generic doubly-linked list。所谓“非类型安全”，是指链表里存的是 void *data，
 * 编译期完全不知道真实类型，存/取都由调用方自己保证类型正确——性能高、灵活，
 * 但用错类型编译器不会拦你。
 *
 * 【核心数据结构(定义在 linklist.h)】
 *   struct list     { head, tail, count, flags, cmp, del } —— 链表自身；
 *   struct listnode { next, prev, data }                   —— 单个节点。
 * 其中：
 *   cmp  : 排序插入(listnode_add_sort)时使用的比较回调，约定见下；
 *   del  : 删除节点时释放 data 的回调，可为 NULL(表示 data 不归链表管)；
 *   flags: 目前主要用到 LINKLIST_FLAG_NODE_MEM_BY_APP。
 *
 * 【cmp 回调约定】cmp(a, b) 返回 <0 表示 a 应排在 b 之前，返回 0 表示两者
 * “相等”。在 _nodup 版本里，返回 0 会被当作重复项直接丢弃。
 *
 * 【del 回调约定】del(data) 只负责释放 data 指向的对象；listnode 结构体本身
 * 由链表内部释放(见 listnode_free)。顺序是“先 del 数据、再释放节点”。
 *
 * 【LINKLIST_FLAG_NODE_MEM_BY_APP 是什么】默认情况下 listnode 结构体由链表
 * 自己 XCALLOC；置此标志后，节点结构体的内存改由**应用自己管理**，此时传给
 * listnode_add 等函数的 val 参数“就是 listnode 本身”(而不再是 data)。理解
 * 这一点是看懂本文件里各处 if (flags & ...) 分支的关键：listnode_new / free
 * 会因此有意跳过分配与释放。
 *
 * 【两个核心宏】LISTNODE_ATTACH / LISTNODE_DETACH 只做指针接挂与摘除，
 * 不释放任何内存(既不释放 data，也不释放 node)。列表增删的基本动作都由它们
 * 或与之等价的展开代码组成。
 *
 * 【阅读顺序】LISTNODE_ATTACH / DETACH 宏 → list_new/free → listnode_new/free
 * (注意应用托管分支) → listnode_add 系列 → listnode_delete 系列 → list_sort。
 * ============================================================================
 */
#include <zebra.h>
#include <stdlib.h>

#include "linklist.h"
#include "memory.h"
#include "libfrr_trace.h"

/* 【内存记账类型】LINK_LIST 用于 struct list 壳，LINK_NODE 用于 struct listnode
 * 节点。DEFINE_MTYPE_STATIC 表示这两个类型标签仅在本文件内可见——它们只服务
 * 于本文件的 XCALLOC/XFREE，用于内存统计与泄漏定位，不对外暴露。 */
DEFINE_MTYPE_STATIC(LIB, LINK_LIST, "Link List");
DEFINE_MTYPE_STATIC(LIB, LINK_NODE, "Link Node");

/* these *do not* cleanup list nodes and referenced data, as the functions
 * do - these macros simply {de,at}tach a listnode from/to a list.
 */

/* 【宏】LISTNODE_ATTACH(L, N)：把节点 N 追加到链表 L 的**尾部**，只改指针，
 * 不分配/释放任何内存。
 * 【前提】N 已存在(通常刚由 listnode_new 建好)且尚未挂在任何链表中。
 * 【做什么】N->prev 指向原尾节点；若表为空则 N 同时成为 head，否则挂到原
 * tail 的 next 之后；更新 tail 与 count。
 * 【易踩的坑】它不检查 N 是否已在链表中，重复 attach 会造成自环 / 计数错乱；
 * 它也不设置 N->data(那是 listnode_new 的职责)。 */
/* List node attach macro.  */
#define LISTNODE_ATTACH(L, N)                                                  \
	do {                                                                   \
		(N)->prev = (L)->tail;                                         \
		(N)->next = NULL;                                              \
		if ((L)->head == NULL)                                         \
			(L)->head = (N);                                       \
		else                                                           \
			(L)->tail->next = (N);                                 \
		(L)->tail = (N);                                               \
		(L)->count++;                                                  \
	} while (0)

/* 【宏】LISTNODE_DETACH(L, N)：把节点 N 从链表 L 中**摘除**，只改指针，
 * 不释放 N 也不释放 N->data。
 * 【做什么】让 N 的前驱、后继互相接上；若 N 恰是头 / 尾则相应更新 head/tail；
 * count 减一。
 * 【易踩的坑】摘除后 N 仍保留着旧的 prev/next 值，不会被清空——若之后还要
 * 复用这个 N，必须重新设置它的 prev/next。 */
/* List node detach macro.  */
#define LISTNODE_DETACH(L, N)                                                  \
	do {                                                                   \
		if ((N)->prev)                                                 \
			(N)->prev->next = (N)->next;                           \
		else                                                           \
			(L)->head = (N)->next;                                 \
		if ((N)->next)                                                 \
			(N)->next->prev = (N)->prev;                           \
		else                                                           \
			(L)->tail = (N)->prev;                                 \
		(L)->count--;                                                  \
	} while (0)

/* 【函数】list_new —— 分配并清零一个新的空链表壳。
 *   返回: struct list*，其 head/tail/count/flags/cmp/del 全为 0 / NULL。
 * 【要点】只建“壳”，不预先分配任何节点。用 XCALLOC 保证全部字段初始为 0，
 * 所以新表默认无 cmp、无 del、flags 无任何标志位。
 * 【易踩的坑】创建后必须用 list_delete() 成对销毁，否则壳内存泄漏。 */
struct list *list_new(void)
{
	return XCALLOC(MTYPE_LINK_LIST, sizeof(struct list));
}

/* 【函数】list_free_internal —— 仅释放链表壳本身(静态内部函数)。
 * 【易踩的坑】它**不清理任何节点**：调用前必须确保表已空(节点已全部释放)，
 * 否则残留在表中的节点连同它们的 data 就一起泄漏了。壳释放的公共入口是
 * list_delete()，后者会先删光所有节点再调用本函数。参数由调用方保证非 NULL。 */
/* Free list. */
static void list_free_internal(struct list *l)
{
	XFREE(MTYPE_LINK_LIST, l);
}


/* 【函数】listnode_new —— 创建一个 listnode 并关联数据 val(内部专用)。
 *   参数: list 决定采用哪种内存策略；val 通常是 data。
 *   返回: 新建(或就地取用)的 listnode*。
 * 【关键分支——应用托管内存】若 list->flags 含 LINKLIST_FLAG_NODE_MEM_BY_APP，
 * 则**不分配节点**，直接把传入的 val 当成 listnode 使用(此时 val 本身就是
 * listnode*)，只把它的 prev/next 清零，data 由应用自己填写。
 * 否则 XCALLOC 一个节点，并把 val 存入 node->data。
 * 【易踩的坑】托管模式下 val 与 data 的角色完全反转，调用者必须保证 val 指向
 * 的内存足以容纳 struct listnode，且其生命周期由自己管理。 */
/* Allocate new listnode.  Internal use only. */
static struct listnode *listnode_new(struct list *list, void *val)
{
	struct listnode *node;

	/* if listnode memory is managed by the app then the val
	 * passed in is the listnode
	 */
	if (list->flags & LINKLIST_FLAG_NODE_MEM_BY_APP) {
		node = val;
		node->prev = node->next = NULL;
	} else {
		node = XCALLOC(MTYPE_LINK_NODE, sizeof(struct listnode));
		node->data = val;
	}
	return node;
}

/* 【函数】listnode_free —— 释放节点结构体(内部专用)。
 * 【分支】托管模式下(list->flags & LINKLIST_FLAG_NODE_MEM_BY_APP)**什么都不做**，
 * 节点内存归应用所有，链表无权释放。
 * 【易踩的坑】它只释放 listnode 结构体，**不释放 node->data**——data 由
 * list->del 回调负责释放(见 list_delete_all_node / list_delete_node 的调用点)。 */
/* Free listnode. */
static void listnode_free(struct list *list, struct listnode *node)
{
	if (!(list->flags & LINKLIST_FLAG_NODE_MEM_BY_APP))
		XFREE(MTYPE_LINK_NODE, node);
}

/* 【函数】listnode_add —— 把 val 追加到链表**尾部**，返回新节点。
 *   参数: list 目标链表；val 要存入的数据(非 NULL，内部有 assert)。
 *   返回: 新建的 listnode*。
 * 【做什么】先 frrtrace 记一笔跟踪，再用 listnode_new 建节点，然后把它挂到
 * tail 上(逻辑与 LISTNODE_ATTACH 宏等价，这里是手写展开)。
 * 【易踩的坑】O(1) 尾插，但不做任何去重 / 排序；托管模式下 val 必须是一个
 * 可当 listnode 使用的对象。 */
struct listnode *listnode_add(struct list *list, void *val)
{
	frrtrace(2, frr_libfrr, list_add, list, val);

	struct listnode *node;

	assert(val != NULL);

	node = listnode_new(list, val);

	node->prev = list->tail;

	if (list->head == NULL)
		list->head = node;
	else
		list->tail->next = node;
	list->tail = node;

	list->count++;

	return node;
}

/* 【函数】listnode_add_head —— 把 val 插到链表**头部**，O(1)。
 *   参数: list 目标链表；val 数据(非 NULL，有 assert)。
 * 【做什么】listnode_new 建节点后挂到 head 之前(成为新 head)；空表时 head 与
 * tail 同时指向该节点。
 * 【易踩的坑】同样不做去重 / 排序。空表分支必须同时设 tail，否则 tail 会悬空。 */
void listnode_add_head(struct list *list, void *val)
{
	struct listnode *node;

	assert(val != NULL);

	node = listnode_new(list, val);

	node->next = list->head;

	if (list->head == NULL) {
		list->head = node;
		list->tail = node;
	} else
		list->head->prev = node;
	list->head = node;

	list->count++;
}

/* 【函数】listnode_add_sort_nodup —— 有序插入且**去重**。
 *   参数: list 目标链表(应已设置 cmp)；val 数据(非 NULL，有 assert)。
 *   返回: 真正插入返回 true；发现相等元素(未插入)返回 false。
 * 【做什么】托管模式下先取出 n->data 作为比较基准(data 变量)。若设置了 cmp，
 * 从表头向后扫描：遇到第一个 cmp(data, n->data) < 0 的位置，把新节点插到它
 * 前面并返回 true；扫描途中若某次 cmp 返回 0，说明已存在相同元素，**丢弃本次
 * 插入**并返回 false；一直走到表尾都没找到位置，就追加到末尾。
 * 【易踩的坑】去重完全依赖“cmp 返回 0”这一约定，所以 cmp 必须满足相等即返回 0；
 * 比较时 data 与 n->data 的先后顺序要一致，否则排序结果与去重行为都会变。 */
bool listnode_add_sort_nodup(struct list *list, void *val)
{
	struct listnode *n;
	struct listnode *new;
	int ret;
	void *data;

	assert(val != NULL);

	if (list->flags & LINKLIST_FLAG_NODE_MEM_BY_APP) {
		n = val;
		data = n->data;
	} else {
		data = val;
	}

	if (list->cmp) {
		for (n = list->head; n; n = n->next) {
			ret = (*list->cmp)(data, n->data);
			if (ret < 0) {
				new = listnode_new(list, val);

				new->next = n;
				new->prev = n->prev;

				if (n->prev)
					n->prev->next = new;
				else
					list->head = new;
				n->prev = new;
				list->count++;
				return true;
			}
			/* found duplicate return false */
			if (ret == 0)
				return false;
		}
	}

	new = listnode_new(list, val);

	LISTNODE_ATTACH(list, new);

	return true;
}

/* 【函数】list_dup —— 浅拷贝一个链表(复制壳与节点链，不复制 data)。
 *   参数: list 源链表(非 NULL，有 assert)。
 *   返回: 新链表，节点顺序与源一致，data 指针与源指向同一批对象。
 * 【做什么】新壳继承源的 cmp 与 del，再按顺序把每个 data 用 listnode_add 逐个
 * 追加进新表。
 * 【易踩的坑】这是**浅拷贝**：新旧链表共享同一批 data，销毁其一(若 del 会释放
 * data)会让另一个悬空 / 二次释放——复制后必须明确 data 的所有权归属。另外
 * flags 标志位不会被复制。 */
struct list *list_dup(struct list *list)
{
	struct list *dup;
	struct listnode *node;
	void *data;

	assert(list);

	dup = list_new();
	dup->cmp = list->cmp;
	dup->del = list->del;
	for (ALL_LIST_ELEMENTS_RO(list, node, data))
		listnode_add(dup, data);

	return dup;
}

/* 【函数】listnode_add_sort —— 有序插入(允许重复)。
 *   参数: list 目标链表(应已设置 cmp)；val 数据(非 NULL，有 assert)。
 * 【做什么】listnode_new 建节点后，用 node->data 回填 val(托管模式下这一步才
 * 取到真正的数据)；若设置了 cmp，从头扫描，在第一个 cmp(val, n->data) < 0 的
 * 位置插入并返回；否则追加到尾部。
 * 【易踩的坑】与 _nodup 不同，这里**不去重**，相等元素也会被追加(可能破坏
 * 严格有序，但保持插入稳定性)。cmp 为 NULL 时退化为单纯尾插(根本不排序)。 */
void listnode_add_sort(struct list *list, void *val)
{
	struct listnode *n;
	struct listnode *new;

	assert(val != NULL);

	new = listnode_new(list, val);
	val = new->data;

	if (list->cmp) {
		for (n = list->head; n; n = n->next) {
			if ((*list->cmp)(val, n->data) < 0) {
				new->next = n;
				new->prev = n->prev;

				if (n->prev)
					n->prev->next = new;
				else
					list->head = new;
				n->prev = new;
				list->count++;
				return;
			}
		}
	}

	new->prev = list->tail;

	if (list->tail)
		list->tail->next = new;
	else
		list->head = new;

	list->tail = new;
	list->count++;
}

/* 【函数】listnode_add_after —— 在指定节点 pp **之后**插入 val。
 *   参数: list 目标链表；pp 参照节点(为 NULL 表示插到最前面即新的 head)；
 *         val 数据(非 NULL，有 assert)。
 *   返回: 新建的 listnode*。
 * 【做什么】listnode_new 建节点后把它接到 pp 的后面；pp 为 NULL 时走头插分支。
 * 【易踩的坑】pp 必须确实属于 list(此处不校验)；不排序、不去重。 */
struct listnode *listnode_add_after(struct list *list, struct listnode *pp,
				    void *val)
{
	struct listnode *nn;

	assert(val != NULL);

	nn = listnode_new(list, val);

	if (pp == NULL) {
		if (list->head)
			list->head->prev = nn;
		else
			list->tail = nn;

		nn->next = list->head;
		nn->prev = pp;

		list->head = nn;
	} else {
		if (pp->next)
			pp->next->prev = nn;
		else
			list->tail = nn;

		nn->next = pp->next;
		nn->prev = pp;

		pp->next = nn;
	}
	list->count++;
	return nn;
}

/* 【函数】listnode_add_before —— 在指定节点 pp **之前**插入 val。
 *   参数: list 目标链表；pp 参照节点(为 NULL 表示插到最末尾即新的 tail)；
 *         val 数据(非 NULL，有 assert)。
 *   返回: 新建的 listnode*。
 * 【做什么】listnode_new 建节点后把它接到 pp 的前面；pp 为 NULL 时走尾插分支。
 * 【易踩的坑】pp 必须确实属于 list(此处不校验)；不排序、不去重。 */
struct listnode *listnode_add_before(struct list *list, struct listnode *pp,
				     void *val)
{
	struct listnode *nn;

	assert(val != NULL);

	nn = listnode_new(list, val);

	if (pp == NULL) {
		if (list->tail)
			list->tail->next = nn;
		else
			list->head = nn;

		nn->prev = list->tail;
		nn->next = pp;

		list->tail = nn;
	} else {
		if (pp->prev)
			pp->prev->next = nn;
		else
			list->head = nn;

		nn->prev = pp->prev;
		nn->next = pp;

		pp->prev = nn;
	}
	list->count++;
	return nn;
}

/* 【函数】listnode_move_to_tail —— 把已在表中的节点 n 移动到链表尾部。
 * 【做什么】先 LISTNODE_DETACH 摘除，再 LISTNODE_ATTACH 追加到尾部；count 在
 * 两次操作里先减后加，净变化为 0。
 * 【易踩的坑】n 必须确实在 l 中(两个宏都不做校验，否则会破坏链表结构)。
 * 只是移动节点位置，不触碰 data，是 O(1) 操作。常用于 LRU / 优先级调整。 */
void listnode_move_to_tail(struct list *l, struct listnode *n)
{
	LISTNODE_DETACH(l, n);
	LISTNODE_ATTACH(l, n);
}

/* 【函数】listnode_delete —— 按 data 在链表中查找并删除对应节点。
 *   参数: list 目标链表；val 要匹配的 data 指针(按**指针相等**匹配，非内容)。
 * 【做什么】frrtrace 后调用 listnode_lookup 找到节点，若找到则交给
 * list_delete_node 摘除并释放。
 * 【易踩的坑】匹配是“指针相等”比较(见 listnode_lookup)，不是 memcmp / strcmp；
 * 找不到则静默返回、什么都不做。 */
void listnode_delete(struct list *list, const void *val)
{
	frrtrace(2, frr_libfrr, list_remove, list, val);

	struct listnode *node = listnode_lookup(list, val);

	if (node)
		list_delete_node(list, node);
}

/* 【函数】listnode_head —— 返回链表头节点里的 data(不摘除节点)。
 *   参数: list 目标链表(非 NULL，有 assert)。
 *   返回: 头节点的 node->data；空表返回 NULL。
 * 【易踩的坑】只读取、不改变链表；返回的是 data 本体的指针，而不是 listnode。 */
void *listnode_head(struct list *list)
{
	struct listnode *node;

	assert(list);
	node = list->head;

	if (node)
		return node->data;
	return NULL;
}

/* 【函数】list_delete_all_node —— 清空链表中的所有节点，但保留链表壳。
 *   参数: list 目标链表(非 NULL，有 assert)。
 * 【做什么】遍历链表，对每个节点：先用 *list->del(node->data) 释放数据
 * (代码先解引用判空，只有设置了 del 才真正调用)，再用 listnode_free 释放节点；
 * 循环用 next 预先保存后继，避免释放后访问悬空指针。最后把 head/tail 置 NULL、
 * count 归 0。
 * 【易踩的坑】del 可为 NULL，此时只释放节点、data 交给调用方自己处理，否则
 * 会数据泄漏。 */
void list_delete_all_node(struct list *list)
{
	struct listnode *node;
	struct listnode *next;

	assert(list);
	for (node = list->head; node; node = next) {
		next = node->next;
		if (*list->del)
			(*list->del)(node->data);
		listnode_free(list, node);
	}
	list->head = list->tail = NULL;
	list->count = 0;
}

/* 【函数】list_delete —— 彻底销毁链表：先清空所有节点，再释放链表壳。
 *   参数: list 指向链表指针的指针；销毁后会被置为 NULL(即 *list = NULL)。
 * 【做什么】assert *list 非 NULL → list_delete_all_node 清空节点 →
 * list_free_internal 释放壳 → *list = NULL。
 * 【易踩的坑】传入的是**二级指针**，函数会把调用方持有的指针也置空以防范悬空；
 * 但若别处还保存着同一链表指针的副本，它们不会被自动置空。 */
void list_delete(struct list **list)
{
	assert(*list);
	list_delete_all_node(*list);
	list_free_internal(*list);
	*list = NULL;
}

/* 【函数】listnode_lookup —— 按 data 指针在链表中查找节点。
 *   参数: list 目标链表(非 NULL，有 assert)；data 要匹配的指针。
 *   返回: 命中的 listnode*；未找到返回 NULL。
 * 【做什么】用 listhead / listnextnode / listgetdata 这套宏从表头逐节点比较。
 * 【易踩的坑】比较的是**指针本身**是否相等，而非它指向的内容；同一份内容的不同
 * 副本不会被匹配。 */
struct listnode *listnode_lookup(struct list *list, const void *data)
{
	struct listnode *node;

	assert(list);
	for (node = listhead(list); node; node = listnextnode(node))
		if (data == listgetdata(node))
			return node;
	return NULL;
}

/* 【函数】listnode_lookup_nocheck —— listnode_lookup 的“免断言”容错版本。
 *   参数: list 允许为 NULL；data 要匹配的指针。
 *   返回: 命中节点或 NULL；list 为 NULL 时直接返回 NULL。
 * 【易踩的坑】相比 listnode_lookup 多了一层 list 判空，调用方无需先 assert；
 * 其余行为完全一致，仍然是“指针相等”比较。 */
struct listnode *listnode_lookup_nocheck(struct list *list, void *data)
{
	if (!list)
		return NULL;
	return listnode_lookup(list, data);
}

/* 【函数】list_delete_node —— 已知节点时，把它从链表中摘除并释放。
 *   参数: list 目标链表；node 待删除的节点。
 * 【做什么】frrtrace 后，按 LISTNODE_DETACH 的逻辑把前驱 / 后继接上并更新
 * head/tail、count 减一，最后调用 listnode_free 释放节点结构体。
 * 【易踩的坑】它**不调用 del 回调、也不释放 node->data**(区别于
 * list_delete_all_node)——data 需由调用方自行处理，否则数据泄漏。node 必须确实
 * 属于 list(此处不校验)。 */
void list_delete_node(struct list *list, struct listnode *node)
{
	frrtrace(2, frr_libfrr, list_delete_node, list, node);

	if (node->prev)
		node->prev->next = node->next;
	else
		list->head = node->next;
	if (node->next)
		node->next->prev = node->prev;
	else
		list->tail = node->prev;
	list->count--;
	listnode_free(list, node);
}

/* 【函数】list_sort —— 用一个比较函数对整条链表重新排序。
 *   参数: list 目标链表；cmp 比较回调，形参是 void** 是为了贴合 qsort 风格。
 * 【做什么】把所有 data 收集进一个临时数组 → 逐个从链表中摘除节点 → 用 qsort
 * 对数组排序 → 再按新顺序把 data 依次 listnode_add 回链表 → 释放临时数组。
 * 【易踩的坑】① 排序过程中链表被清空再重建，节点对象是**全新分配**的，外部若
 * 缓存了旧的 listnode* 会失效；② cmp 的签名 void** (元素是 void* 的数组)与链表
 * 自身的 cmp(void*, void*) 不同，切勿混用；③ 复杂度 O(n log n)，会临时占用
 * O(n) 内存(MTYPE_TMP)，且要求 list 非空。 */
void list_sort(struct list *list, int (*cmp)(const void **, const void **))
{
	frrtrace(1, frr_libfrr, list_sort, list);

	struct listnode *ln, *nn;
	int i = -1;
	void *data;
	size_t n = list->count;
	void **items;
	int (*realcmp)(const void *, const void *) =
		(int (*)(const void *, const void *))cmp;

	if (!n)
		return;

	items = XCALLOC(MTYPE_TMP, (sizeof(void *)) * n);

	for (ALL_LIST_ELEMENTS(list, ln, nn, data)) {
		items[++i] = data;
		list_delete_node(list, ln);
	}

	qsort(items, n, sizeof(void *), realcmp);

	for (unsigned int j = 0; j < n; ++j)
		listnode_add(list, items[j]);

	XFREE(MTYPE_TMP, items);
}

/* 【函数】listnode_add_force —— 懒创建：链表不存在就先建好，再追加元素。
 *   参数: list 指向链表指针的指针(*list 可为 NULL，函数会自动补建)；val 数据。
 *   返回: 新建的 listnode*。
 * 【做什么】若 *list == NULL 就先 list_new()，然后 listnode_add 追加。
 * 【易踩的坑】入参是**二级指针**，新链表会写回调用方；适用于“首次插入时才
 * 创建链表”的场景。仍要求 val 非 NULL。 */
struct listnode *listnode_add_force(struct list **list, void *val)
{
	if (*list == NULL)
		*list = list_new();
	return listnode_add(*list, val);
}

/* 【函数】list_to_array —— 把链表中的 data 指针按序拷贝到调用方提供的数组。
 *   参数: list 源链表；arr 目标数组；arrlen 数组可容纳的元素个数。
 *   返回: 传入的 arr 本身(便于链式使用)。
 * 【做什么】只读遍历(ALL_LIST_ELEMENTS_RO)，逐个写入 arr，写满 arrlen 即停止。
 * 【易踩的坑】数组容量由调用方负责保证(通常先用 listcount() 申请)；写入的是
 * data 指针的**浅拷贝**，与链表共享同一批对象；本函数不修改链表。 */
void **list_to_array(struct list *list, void **arr, size_t arrlen)
{
	struct listnode *ln;
	void *vp;
	size_t idx = 0;

	for (ALL_LIST_ELEMENTS_RO(list, ln, vp)) {
		arr[idx++] = vp;
		if (idx == arrlen)
			break;
	}

	return arr;
}
