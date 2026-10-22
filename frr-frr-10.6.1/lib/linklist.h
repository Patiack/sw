// SPDX-License-Identifier: GPL-2.0-or-later
/* Generic linked list
 * Copyright (C) 1997, 2000 Kunihiro Ishiguro
 */

#ifndef _ZEBRA_LINKLIST_H
#define _ZEBRA_LINKLIST_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * 文件导读：linklist.h —— FRR 自研的非类型安全双向链表（Generic linked list）
 * ============================================================================
 *
 * 本头文件在 FRR 里的作用：
 *   提供一套通用的、以 void 指针存放元素的侵入式双向链表，供全项目（bgpd、
 *   zebra、ospfd 等）复用。它是 FRR 历史上最早、使用最广的容器之一，但并非
 *   类型安全——存进去的指针需要调用方自行保证类型正确。
 *
 * 核心数据结构：
 *   struct listnode —— 链表节点，含 next / prev / data 三个成员。
 *                      data 为私有成员，应通过 listgetdata() 读取。
 *   struct list     —— 链表本体，含 head / tail / count / flags / cmp / del。
 *                      head 指向首节点，tail 指向尾节点，count 记录节点个数，
 *                      cmp / del 为可选的比较回调与数据释放回调。
 *
 * 关键语义提示：
 *   1) cmp 回调返回约定：val1 小于 val2 返回 -1，相等返回 0，大于返回 1；
 *      仅 listnode_add_sort / listnode_add_sort_nodup 会用到，用于决定插入位置。
 *   2) del 回调：在删除节点或删除整条链表时被调用，用于释放用户自己的数据；
 *      强烈建议提供，否则会造成内存泄漏。
 *   3) LINKLIST_FLAG_NODE_MEM_BY_APP：置位后，listnode 的内存由应用自管，
 *      本库不负责分配与释放；此时各 add 接口传入的 val 就是 listnode 本身。
 *
 * 与其他模块的关系：
 *   仅依赖内存分配接口与 assert 断言；被几乎所有 FRR 守护进程包含。
 *   FRR 新代码建议优先使用类型安全的容器，参见文件内下方的 NOTICE 说明。
 *
 * 阅读顺序：
 *   struct listnode → struct list 及其各成员 → LINKLIST_FLAG 与内联宏
 *   （listhead / listtail / listnextnode / listgetdata 等）→ 各 extern API 原型。
 * ============================================================================
 */


/*
 * NOTICE:
 *
 * If you are reading this file in an effort to add a new list structure
 * this is the wrong place to be using it.  Please see the typesafe
 * data structures, or ask one of the other developers.
 *
 * If you are reading this file as a way to update an existing usage
 * of this data structure, please consider just converting the data
 * structure to one of the typesafe data structures instead.
 */

/*
 * 链表节点：双向链表中的单个元素容器。
 * next / prev 分别指向后驱与前驱节点（非循环链表：首节点 prev 为 NULL，
 * 尾节点 next 为 NULL）。data 为私有成员，禁止直接访问，应使用 listgetdata()
 * 读取。节点必须始终携带有效数据，向链表中加入空节点（data 为 NULL）是非法用法。
 */

/* listnodes must always contain data to be valid. Adding an empty node
 * to a list is invalid
 */
struct listnode {
	struct listnode *next;
	struct listnode *prev;

	/* private member, use getdata() to retrieve, do not access directly */
	void *data;
};

/*
 * 链表本体：管理一条双向链表的头尾指针、节点计数、行为标志与回调。
 * head / tail：首尾节点指针，空链表时两者均为 NULL。
 * count：不变量——链表中节点的个数，随增删操作维护。
 * flags：行为标志位，目前只用到下方的 LINKLIST_FLAG_NODE_MEM_BY_APP。
 * cmp：排序比较回调，供 listnode_add_sort / listnode_add_sort_nodup 使用。
 * del：节点被删除时的数据释放回调，强烈建议提供以避免内存泄漏。
 */

struct list {
	struct listnode *head;
	struct listnode *tail;

	/* invariant: count is the number of listnodes in the list */
	unsigned int count;

	/*
	 * flags：链表行为标志位。当前仅定义 LINKLIST_FLAG_NODE_MEM_BY_APP，
	 * 传参、内存归属语义均受其影响。
	 */
	uint8_t flags;
/*
 * 标志：listnode 的内存由应用自行管理，本库不负责其分配与释放。
 * 置位后，所有 add 接口传入的 val 参数即被视为 listnode 指针本身；
 * 相应地，删除节点 / 删除链表时本库也不会释放该 listnode 的内存。
 */
/* Indicates that listnode memory is managed by the application and
 * doesn't need to be freed by this library via listnode_delete etc.
 */
#define LINKLIST_FLAG_NODE_MEM_BY_APP (1 << 0)

	/*
	 * cmp：排序比较回调。返回 -1 表示 val1 小于 val2，返回 0 表示两者相等，
	 * 返回 1 表示 val1 大于 val2。仅用于 listnode_add_sort /
	 * listnode_add_sort_nodup 决定插入位置；为 NULL 时排序插入退化为从尾部追加。
	 * 用于排序时须构成全序关系，否则插入位置不可预期。
	 */
	/*
	 * Returns -1 if val1 < val2, 0 if equal?, 1 if val1 > val2.
	 * Used as definition of sorted for listnode_add_sort
	 */
	int (*cmp)(void *val1, void *val2);

	/*
	 * del：删除节点或整条链表时回调，用于释放用户持有的数据。强烈建议提供，
	 * 否则数据对象不会被释放而泄漏。注意 list_delete_node() 不会触发该回调。
	 */
	/* callback to free user-owned data when listnode is deleted. supplying
	 * this callback is very much encouraged!
	 */
	void (*del)(void *val);
};

/*
 * 一组常用的内联访问宏。带 _unchecked 后缀的版本假定参数 X 非 NULL，直接解引用，
 * 由调用方保证合法性；不带后缀的版本先判空，X 为 NULL 时返回 NULL。
 *   listnextnode / listhead / listtail：NULL 安全版，取后驱 / 头 / 尾。
 *   listnextnode_unchecked / listhead_unchecked / listtail_unchecked：不判空版。
 *   listcount：取节点数（直接读 count，不解引用 head/tail，无 NULL 风险）。
 *   list_isempty：head 与 tail 同时为 NULL 视为空链表。
 */
#define listnextnode(X) ((X) ? ((X)->next) : NULL)
#define listnextnode_unchecked(X) ((X)->next)
#define listhead(X) ((X) ? ((X)->head) : NULL)
#define listhead_unchecked(X) ((X)->head)
#define listtail(X) ((X) ? ((X)->tail) : NULL)
#define listtail_unchecked(X) ((X)->tail)
#define listcount(X) ((X)->count)
#define list_isempty(X) ((X)->head == NULL && (X)->tail == NULL)
/*
 * listgetdata：返回节点携带的数据指针，是读取 node->data 的推荐方式。
 * 要求 X 与 X->data 均非 NULL，否则触发 assert 断言失败（非 NULL 安全）。
 */
/* return X->data only if X and X->data are not NULL */
static inline void *listgetdata(const struct listnode *X)
{
	assert((X != NULL) && ((X)->data != NULL));
	return X->data;
}
/*
 * listset_app_node_mem：置位 MEM_BY_APP 标志，声明“node 内存由应用管理”，
 *   置位后本库不再分配与释放 node，各 add 接口传入的实参即为 listnode 自身。
 * listnode_init：把 val 直接写入节点的 data 字段，用于就地初始化一个 node。
 */
/* App is going to manage listnode memory */
#define listset_app_node_mem(X) ((X)->flags |= LINKLIST_FLAG_NODE_MEM_BY_APP)
#define listnode_init(X, val) ((X)->data = (val))

/*
 * list_new：分配并返回一条空链表。
 * 返回：新链表指针（head / tail 初始化为 NULL，count 为 0，flags 为 0）。
 * 默认未设置 cmp / del，调用方需要时自行赋值。失败时通常不会返回 NULL
 * （分配失败即终止），调用方一般无需判空。
 */
/*
 * Create a new linked list.
 *
 * Returns:
 *    the created linked list
 */
extern struct list *list_new(void);

/*
 * listnode_add：把 data 作为新节点追加到链表尾部。O(1)。
 * 返回：新创建的 listnode 指针。
 * 注意：若已置位 MEM_BY_APP，则传入的 data 必须是 listnode 自身。
 */
/*
 * Add a new element to the tail of a list.
 *
 * Runtime is O(1).
 *
 * list
 *    list to operate on
 *
 * data
 *    element to add
 */
extern struct listnode *listnode_add(struct list *list, void *data);

/*
 * listnode_add_head：把 data 作为新节点插入链表头部。O(1)。
 * 无返回值。若置位 MEM_BY_APP，则 data 即 listnode 本身。
 */
/*
 * Add a new element to the beginning of a list.
 *
 * Runtime is O(1).
 *
 * list
 *    list to operate on
 *
 * data
 *    If MEM_BY_APP is set this is listnode. Otherwise it is element to add.
 */
extern void listnode_add_head(struct list *list, void *data);

/*
 * listnode_add_sort：按 list->cmp 的插入排序规则把 val 插入到有序链表。O(N)。
 * 若 list->cmp 为 NULL，则等价于 listnode_add（追加到尾部）。
 * 若置位 MEM_BY_APP，则 val 即 listnode 本身。
 * 注意：依赖 cmp 返回 -1 / 0 / 1 的全序关系，链表须保持有序。
 */
/*
 * Insert a new element into a list with insertion sort.
 *
 * If list->cmp is set, this function is used to determine the position to
 * insert the new element. If it is not set, this function is equivalent to
 * listnode_add.
 *
 * Runtime is O(N).
 *
 * list
 *    list to operate on
 *
 * val
 *    If MEM_BY_APP is set this is listnode. Otherwise it is element to add.
 */
extern void listnode_add_sort(struct list *list, void *val);

/*
 * listnode_add_after：在节点 pp 之后插入 data。O(1)。
 * 返回：承载新数据的 listnode 指针。
 * 若置位 MEM_BY_APP，则 data 即 listnode 本身。
 */
/*
 * Insert a new element into a list after another element.
 *
 * Runtime is O(1).
 *
 * list
 *    list to operate on
 *
 * pp
 *    listnode to insert after
 *
 * data
 *    If MEM_BY_APP is set this is listnode. Otherwise it is element to add.
 *
 * Returns:
 *    pointer to newly created listnode that contains the inserted data
 */
extern struct listnode *listnode_add_after(struct list *list,
					   struct listnode *pp, void *data);

/*
 * listnode_add_before：在节点 pp 之前插入 data。O(1)。
 * 返回：承载新数据的 listnode 指针。
 * 若置位 MEM_BY_APP，则 data 即 listnode 本身。
 */
/*
 * Insert a new element into a list before another element.
 *
 * Runtime is O(1).
 *
 * list
 *    list to operate on
 *
 * pp
 *    listnode to insert before
 *
 * data
 *    If MEM_BY_APP is set this is listnode. Otherwise it is element to add.
 *
 * Returns:
 *    pointer to newly created listnode that contains the inserted data
 */
extern struct listnode *listnode_add_before(struct list *list,
					    struct listnode *pp, void *data);

/*
 * listnode_move_to_tail：把已存在的节点 node 移动到链表尾部。O(1)。
 * 仅调整指针，不涉及内存分配与释放；常用于 LRU 等顺序维护场景。
 */
/*
 * Move a node to the tail of a list.
 *
 * Runtime is O(1).
 *
 * list
 *    list to operate on
 *
 * node
 *    node to move to tail
 */
extern void listnode_move_to_tail(struct list *list, struct listnode *node);

/*
 * listnode_delete：按数据指针 data 查找并删除对应节点。O(N)。
 * 找到后会回调 list->del（若已设置）释放用户数据。data 用 const 修饰，
 * 表示仅用于比较、不修改。未找到时静默返回。
 */
/*
 * Delete an element from a list.
 *
 * Runtime is O(N).
 *
 * list
 *    list to operate on
 *
 * data
 *    data to insert into list
 */
extern void listnode_delete(struct list *list, const void *data);

/*
 * listnode_lookup：查找存储 data 的节点。
 * 返回：找到则返回 listnode 指针，否则返回 NULL。
 */
/*
 * Find the listnode corresponding to an element in a list.
 *
 * list
 *    list to operate on
 *
 * data
 *    data to search for
 *
 * Returns:
 *    pointer to listnode storing the given data if found, NULL otherwise
 */
extern struct listnode *listnode_lookup(struct list *list, const void *data);

/*
 * listnode_head：返回链表头节点携带的数据。
 * 返回：头节点 data；空链表返回 NULL。注意返回的是 data 而非 listnode。
 */
/*
 * Retrieve the element at the head of a list.
 *
 * list
 *    list to operate on
 *
 * Returns:
 *    data at head of list, or NULL if list is empty
 */
extern void *listnode_head(struct list *list);

/*
 * list_sort：对链表就地排序，使用快排（不稳定排序）。
 * cmp 为 quicksort 风格的比较函数，参数是“指向 void 指针的指针”，需解引用
 * 一次取得真正的数据指针；返回小于 / 等于 / 大于 0 分别表示前者小于 / 等于 /
 * 大于后者。排序后 head / tail / count 会随之更新。
 */
/*
 * Sort a list in place.
 *
 * The sorting algorithm used is quicksort. Runtimes are equivalent to those of
 * quicksort plus N. The sort is not stable.
 *
 * For portability reasons, the comparison function takes a pointer to pointer
 * to void. This pointer should be dereferenced to get the actual data pointer.
 * It is always safe to do this.
 *
 * list
 *    list to sort
 *
 * cmp
 *    comparison function for quicksort. Should return less than, equal to or
 *    greater than zero if the first argument is less than, equal to or greater
 *    than the second argument.
 */
extern void list_sort(struct list *list,
		      int (*cmp)(const void **, const void **));

/*
 * list_to_array：把链表元素依次填入预分配的指针数组。
 * 从链表头开始，直到链表末尾或数组写满（arrlen 个）为止。
 * 返回：传入的 arr 指针。
 */
/*
 * Convert a list to an array of void pointers.
 *
 * Starts from the list head and ends either on the last node of the list or
 * when the provided array cannot store any more elements.
 *
 * list
 *    list to convert
 *
 * arr
 *    Pre-allocated array of void *
 *
 * arrlen
 *    Number of elements in arr
 *
 * Returns:
 *    arr
 */
void **list_to_array(struct list *list, void **arr, size_t arrlen);

/*
 * list_delete：删除整条链表，并把调用方的指针置空（*plist = NULL）。
 * 若设置了 list->del，会对每个数据元素回调一次。
 * 参数是“指针的指针”，便于删除后自动置空，避免悬垂指针。
 */
/*
 * Delete a list and NULL its pointer.
 *
 * If non-null, list->del is called with each data element.
 *
 * plist
 *    pointer to list pointer; this will be set to NULL after the list has been
 *    deleted
 */
extern void list_delete(struct list **plist);

/*
 * list_delete_all_node：清空链表中的所有节点，但保留链表本体。O(N)。
 * 若设置了 list->del，会对每个数据元素回调一次。清空后链表仍可继续使用。
 */
/*
 * Delete all nodes from a list without deleting the list itself.
 *
 * If non-null, list->del is called with each data element.
 *
 * list
 *    list to operate on
 */
extern void list_delete_all_node(struct list *list);

/*
 * list_delete_node：删除指定节点 node。O(1)。
 * 注意：不会调用 list->del，调用方须自行处理该节点数据的释放。
 */
/*
 * Delete a node from a list.
 *
 * list->del is not called with the data associated with the node.
 *
 * Runtime is O(1).
 *
 * list
 *    list to operate on
 *
 * node
 *    the node to delete
 */
extern void list_delete_node(struct list *list, struct listnode *node);

/*
 * listnode_add_sort_nodup：按 cmp 做插入排序，但不插入重复元素
 * （cmp 返回 0 即判为重复）。假定输入链表已有序；若乱序，会在探测到插入位置
 * 之前一路比较查重。若 list->cmp 为 NULL，则等价于 listnode_add。
 * 返回：插入成功返回 true，发现重复未插入返回 false。
 * 若置位 MEM_BY_APP，则 val 即 listnode 本身。
 */
/*
 * Insert a new element into a list with insertion sort if there is no
 * duplicate element present in the list. This assumes the input list is
 * sorted. If unsorted, it will check for duplicate until it finds out
 * the position to do insertion sort with the unsorted list.
 *
 * If list->cmp is set, this function is used to determine the position to
 * insert the new element. If it is not set, this function is equivalent to
 * listnode_add. duplicate element is determined by cmp function returning 0.
 *
 * Runtime is O(N).
 *
 * list
 *    list to operate on
 *
 * val
 *    If MEM_BY_APP is set this is listnode. Otherwise it is element to add.
 */

extern bool listnode_add_sort_nodup(struct list *list, void *val);

/*
 * list_dup：复制链表结构，对每个元素做浅拷贝（新链表节点共享原数据指针）。
 * 返回：复制出的新链表指针。
 * 注意：数据本身不被复制，两个链表会指向同一批数据对象。
 */
/*
 * Duplicate the specified list, creating a shallow copy of each of its
 * elements.
 *
 * list
 *    list to duplicate
 *
 * Returns:
 *    the duplicated list
 */
extern struct list *list_dup(struct list *list);

/*
 * ALL_LIST_ELEMENTS：遍历宏，展开为 for 的三段式。
 * 用法：for (ALL_LIST_ELEMENTS(list, node, nextnode, data)) { ... }
 * 遍历中可安全删除当前 node——因为已提前把 nextnode 缓存下来。
 * 变量 node / nextnode 为 struct listnode *，data 为解引用后的元素指针。
 */
/* List iteration macro.
 * Usage: for (ALL_LIST_ELEMENTS (...) { ... }
 * It is safe to delete the listnode using this macro.
 */
#define ALL_LIST_ELEMENTS(list, node, nextnode, data)                          \
	(node) = listhead(list), ((data) = NULL);                              \
	(node) != NULL                                                         \
		&& ((data) = static_cast(data, listgetdata(node)),             \
		    (nextnode) = node->next, 1);                               \
	(node) = (nextnode), ((data) = NULL)

/*
 * ALL_LIST_ELEMENTS_RO：只读遍历宏，不缓存 nextnode，开销更小。
 * 用法同 ALL_LIST_ELEMENTS。
 * 注意：仅当“一眼可见”循环体内不会删除 node 时才可使用，否则会有野指针风险。
 */
/* read-only list iteration macro.
 * Usage: as per ALL_LIST_ELEMENTS, but not safe to delete the listnode Only
 * use this macro when it is *immediately obvious* the listnode is not
 * deleted in the body of the loop. Does not have forward-reference overhead
 * of previous macro.
 */
#define ALL_LIST_ELEMENTS_RO(list, node, data)                                 \
	(node) = listhead(list), ((data) = NULL);                              \
	(node) != NULL && ((data) = static_cast(data, listgetdata(node)), 1);  \
	(node) = listnextnode(node), ((data) = NULL)

/*
 * listnode_lookup_nocheck：查找存储 data 的节点的快速版本，省去额外检查。
 * 返回：找到则返回 listnode 指针，否则返回 NULL。
 */
extern struct listnode *listnode_lookup_nocheck(struct list *list, void *data);

/*
 * listnode_add_force：若 *list 非 NULL 就直接向其添加节点；否则先新建一条链表
 * 写回 *list，再添加节点。
 * 返回：新创建的 listnode 指针。
 * 常用于“懒初始化”一条可选链表。
 */
/*
 * Add a node to *list, if non-NULL. Otherwise, allocate a new list, mail
 * it back in *list, and add a new node.
 *
 * Return: the new node.
 */
extern struct listnode *listnode_add_force(struct list **list, void *val);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_LINKLIST_H */
