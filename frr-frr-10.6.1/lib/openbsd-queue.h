// SPDX-License-Identifier: BSD-3-Clause
/*	$OpenBSD: queue.h,v 1.43 2015/12/28 19:38:40 millert Exp $	*/
/*	$NetBSD: queue.h,v 1.11 1996/05/16 05:17:14 mycroft Exp $	*/

/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 *	@(#)queue.h	8.5 (Berkeley) 8/20/94
 */

#ifndef _SYS_QUEUE_H_
#define	_SYS_QUEUE_H_

/*
 * 文件导读: openbsd-queue.h
 *
 * 本文件是 OpenBSD 的 BSD 许可头文件 queue.h 被 FRR 原样搬入 lib/ 的副本,
 * 用一组宏在 C 里实现五类侵入式(invasive)线性容器。
 *
 * 五类数据结构:
 *   SLIST    单链表, 仅一个前向指针, 删除任意元素需 O(n) 遍历, 适合 LIFO 或极少删除的场景;
 *   LIST     带头指针的双向链表, 节点存 le_next 与"指向上一个 next 的地址" le_prev,
 *            故任意元素可 O(1) 摘除, 但只能前向遍历;
 *   SIMPLEQ  单链尾队列, 头结构同时保存首元素和"尾 next 的地址" sqh_last, 故尾插 O(1),
 *            节点只存一个 sqe_next 省空间, 只能从头删除、前向遍历;
 *   XSIMPLEQ 与 SIMPLEQ 用法相同, 但指针与一个随机 cookie 异或后再存, 增大内存投毒的难度;
 *   TAILQ    双链尾队列, 头结构含 tqh_first 与 tqh_last, 节点含 tqe_next 和 tqe_prev,
 *            尾插与任意位置删除均 O(1), 且可双向遍历, 是 FRR 中最常用的链表。
 *
 * 核心技巧: LIST / SIMPLEQ / TAILQ 把"前驱指针"存成指向前一个节点 next 字段的地址
 * (类型是 struct type 的二级指针)。这样插入、删除不必知道所属头结构的类型, 且拖尾插入 O(1)。
 *
 * 为什么直接搬入 FRR: 这些宏在编译期展开、类型安全, 链接节点内联在用户结构体内,
 * 不额外分配内存, 且经 BSD 长期使用验证。FRR 新代码推荐用 typesafe 容器, 本文件属于
 * 历史最久、迁移优先级最低的一类(详见上方英文 NOTICE 与 lib/typesafe.h)。
 *
 * 阅读顺序: 先看 _Q_INVALIDATE 调试宏, 再依次阅读 SLIST、LIST、SIMPLEQ、XSIMPLEQ、TAILQ;
 * 每类按 HEAD / ENTRY 定义、访问宏(_FIRST / _NEXT / _FOREACH)、操作宏(_INSERT / _REMOVE / _CONCAT)展开。
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NOTICE:
 *
 * If you are reading this file in an effort to add a new queue structure
 * this is the wrong place to be using it.  Please see the typesafe
 * data structures, or ask one of the other developers.
 *
 * If you are reading this file as a way to update an existing usage
 * of this data structure, please consider just converting the data
 * structure to one of the typesafe data structures instead.  However,
 * among converting datastrucutres, the the BSD ones are the lowest
 * priority / should be converted last. They are already typesafe and
 * use inline linking nodes, so the only gain is consistency. Please
 * convert uses of linklist.h and hash.h first.
 */

/*
 * This file defines five types of data structures: singly-linked lists,
 * lists, simple queues, tail queues and XOR simple queues.
 *
 *
 * A singly-linked list is headed by a single forward pointer. The elements
 * are singly linked for minimum space and pointer manipulation overhead at
 * the expense of O(n) removal for arbitrary elements. New elements can be
 * added to the list after an existing element or at the head of the list.
 * Elements being removed from the head of the list should use the explicit
 * macro for this purpose for optimum efficiency. A singly-linked list may
 * only be traversed in the forward direction.  Singly-linked lists are ideal
 * for applications with large datasets and few or no removals or for
 * implementing a LIFO queue.
 *
 * A list is headed by a single forward pointer (or an array of forward
 * pointers for a hash table header). The elements are doubly linked
 * so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before
 * or after an existing element or at the head of the list. A list
 * may only be traversed in the forward direction.
 *
 * A simple queue is headed by a pair of pointers, one to the head of the
 * list and the other to the tail of the list. The elements are singly
 * linked to save space, so elements can only be removed from the
 * head of the list. New elements can be added to the list before or after
 * an existing element, at the head of the list, or at the end of the
 * list. A simple queue may only be traversed in the forward direction.
 *
 * A tail queue is headed by a pair of pointers, one to the head of the
 * list and the other to the tail of the list. The elements are doubly
 * linked so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before or
 * after an existing element, at the head of the list, or at the end of
 * the list. A tail queue may be traversed in either direction.
 *
 * An XOR simple queue is used in the same way as a regular simple queue.
 * The difference is that the head structure also includes a "cookie" that
 * is XOR'd with the queue pointer (first, last or next) to generate the
 * real pointer value.
 *
 * For details on the use of these macros, see the queue(3) manual page.
 */

/*
 * _Q_INVALIDATE: 调试期"毒化"指针的辅助宏。
 * 开启 QUEUE_MACRO_DEBUG, 或在内核态且开启 DIAGNOSTIC 时, 把刚被摘除的链接字段赋值为
 * (void *)-1, 使用户再访问这块已失效的链接时立即崩溃或触发断言, 便于定位 use-after-remove。
 * 默认配置下本宏展开为空, 零开销。注意依赖它的保护只在开启该宏时存在, 关闭后不再报错。
 */
#if defined(QUEUE_MACRO_DEBUG) || (defined(_KERNEL) && defined(DIAGNOSTIC))
#define _Q_INVALIDATE(a) (a) = ((void *)-1)
#else
#define _Q_INVALIDATE(a)
#endif

/*
 * SLIST —— 单链表定义区。
 * 头结构只有一个前向指针 slh_first, 每个节点只有一个 sle_next 指针, 空间开销最小。
 * 适合元素多、几乎不删除、或当作 LIFO 栈使用的场景; 删除任意元素需 O(n) 找到前驱。
 */
/*
 * Singly-linked List definitions.
 */
/*
 * SLIST_HEAD(name, type): 声明名为 name 的 SLIST 头结构, 内含指向 type 的 slh_first。
 * 直接把该宏用在结构体定义或作用域里即可; 头结构本身需用 SLIST_INIT 或 *_INITIALIZER 初始化。
 */
#define SLIST_HEAD(name, type)                                                 \
	struct name {                                                          \
		struct type *slh_first; /* first element */                    \
	}

/*
 * SLIST_HEAD_INITIALIZER(head): 头结构的静态初始化表达式, 令 slh_first 为 NULL。
 * 形如头变量定义处赋初值用于静态或全局头结构; 运行期创建的头用 SLIST_INIT 初始化。
 */
#define SLIST_HEAD_INITIALIZER(head)                                           \
	{                                                                      \
		NULL                                                           \
	}

/*
 * SLIST_ENTRY(type): 在用户结构体里嵌入的链接节点, 仅含 sle_next 指针, 指向下一个 type。
 * 一个结构体可嵌入多个不同的 SLIST_ENTRY 字段, 从而同时挂在多条单链表上。
 */
#define SLIST_ENTRY(type)                                                      \
	struct {                                                               \
		struct type *sle_next; /* next element */                      \
	}

/*
 * SLIST 访问宏: SLIST_FIRST 取首元素; SLIST_END 恒为 NULL, 作遍历哨兵;
 * SLIST_EMPTY 判断表是否为空; SLIST_NEXT(elm, field) 取 elm 在指定链接字段上的后继。
 * 都是表达式, 参数可能多次求值; 空表时 SLIST_FIRST 返回 NULL。
 */
/*
 * Singly-linked List access methods.
 */
#define	SLIST_FIRST(head)	((head)->slh_first)
#define	SLIST_END(head)		NULL
#define	SLIST_EMPTY(head)	(SLIST_FIRST(head) == SLIST_END(head))
#define	SLIST_NEXT(elm, field)	((elm)->field.sle_next)

/*
 * SLIST_FOREACH(var, head, field): 前向遍历 head, 每轮把当前元素赋给 var, 直到末尾。
 * 仅用于遍历, 循环体内不得删除当前 var(会丢失后继); 需边遍历边删除时改用 SLIST_FOREACH_SAFE。
 */
#define SLIST_FOREACH(var, head, field)                                        \
	for ((var) = SLIST_FIRST(head); (var) != SLIST_END(head);              \
	     (var) = SLIST_NEXT(var, field))

/*
 * SLIST_FOREACH_SAFE(var, head, field, tvar): 安全遍历, tvar 先缓存 var 的后继再进入循环体,
 * 因此循环体内可安全删除 var。tvar 仅作临时变量, 循环结束后其值无意义, 不要依赖。
 */
#define SLIST_FOREACH_SAFE(var, head, field, tvar)                             \
	for ((var) = SLIST_FIRST(head);                                        \
	     (var) && ((tvar) = SLIST_NEXT(var, field), 1); (var) = (tvar))

/*
 * Singly-linked List functions.
 */
/*
 * SLIST_INIT(head): 把表头置空(slh_first = SLIST_END)。运行期创建头结构后必须调用。
 */
#define SLIST_INIT(head)                                                       \
	{                                                                      \
		SLIST_FIRST(head) = SLIST_END(head);                           \
	}

/*
 * SLIST_INSERT_AFTER(slistelm, elm, field): 把 elm 插到 slistelm 之后(同一张表)。O(1)。
 * 不修改头结构, 调用者须保证 slistelm 确实属于该表。
 */
#define SLIST_INSERT_AFTER(slistelm, elm, field)                               \
	do {                                                                   \
		(elm)->field.sle_next = (slistelm)->field.sle_next;            \
		(slistelm)->field.sle_next = (elm);                            \
	} while (0)

/*
 * SLIST_INSERT_HEAD(head, elm, field): 把 elm 插到表头。O(1), 单链表唯一的高效插入点。
 */
#define SLIST_INSERT_HEAD(head, elm, field)                                    \
	do {                                                                   \
		(elm)->field.sle_next = (head)->slh_first;                     \
		(head)->slh_first = (elm);                                     \
	} while (0)

/*
 * SLIST_REMOVE_AFTER(elm, field): 摘除 elm 之后的一个元素(不释放内存)。O(1)。
 * 调用者须确保 elm 后面确有元素, 否则会解引用 NULL 崩溃。
 */
#define SLIST_REMOVE_AFTER(elm, field)                                         \
	do {                                                                   \
		(elm)->field.sle_next = (elm)->field.sle_next->field.sle_next; \
	} while (0)

/*
 * SLIST_REMOVE_HEAD(head, field): 摘除表头元素, 只前移头指针, 不释放内存。O(1)。
 * 是出栈/LIFO 取出的正确接口, 比通用 SLIST_REMOVE 快; 调用前须确保表非空。
 */
#define SLIST_REMOVE_HEAD(head, field)                                         \
	do {                                                                   \
		(head)->slh_first = (head)->slh_first->field.sle_next;         \
	} while (0)

/*
 * SLIST_REMOVE(head, elm, type, field): 从 head 中删除指定元素 elm(不释放内存)。
 * 单链表需从头遍历寻找前驱, 故为 O(n); 若已知删除的是头元素, 改用 SLIST_REMOVE_HEAD。
 * 需要 type 参数用于声明临时前驱指针; 调试模式下删除后会毒化 elm 的 next 字段。
 */
#define SLIST_REMOVE(head, elm, type, field)                                   \
	do {                                                                   \
		if ((head)->slh_first == (elm)) {                              \
			SLIST_REMOVE_HEAD((head), field);                      \
		} else {                                                       \
			struct type *curelm = (head)->slh_first;               \
                                                                               \
			while (curelm->field.sle_next != (elm))                \
				curelm = curelm->field.sle_next;               \
			curelm->field.sle_next =                               \
				curelm->field.sle_next->field.sle_next;        \
		}                                                              \
		_Q_INVALIDATE((elm)->field.sle_next);                          \
	} while (0)

/*
 * LIST —— 双向链表定义区。
 * 头结构只有一个前向指针 lh_first, 但每个节点除 le_next 外还存 le_prev(指向前驱节点 next 字段的地址)。
 * 因此插入、删除任意元素均 O(1) 且无需头结构; 只能前向遍历; 适合频繁在中间增删的场景。
 */
/*
 * List definitions.
 */
/*
 * LIST_HEAD(name, type): 声明名为 name 的 LIST 头, 内含指向首元素的 lh_first。
 * 也可用于哈希表头(其"头"就是这种前向指针组成的数组)。
 */
#define LIST_HEAD(name, type)                                                  \
	struct name {                                                          \
		struct type *lh_first; /* first element */                     \
	}

/*
 * LIST_HEAD_INITIALIZER(head): LIST 头结构的静态初始化表达式, 令 lh_first 为 NULL。
 */
#define LIST_HEAD_INITIALIZER(head)                                            \
	{                                                                      \
		NULL                                                           \
	}

/*
 * LIST_ENTRY(type): 嵌入的双向链表节点, 含 le_next(下一元素)与 le_prev(指向上一个元素 next 字段的地址)。
 * le_prev 指向的是"指向自己的那个指针", 因此删除时可直接回填前驱, 无需从头遍历。
 */
#define LIST_ENTRY(type)                                                       \
	struct {                                                               \
		struct type *le_next;  /* next element */                      \
		struct type **le_prev; /* address of previous next element */  \
	}

/*
 * LIST 访问宏: LIST_FIRST 取首元素; LIST_END 恒为 NULL 作哨兵; LIST_EMPTY 判空;
 * LIST_NEXT(elm, field) 取后继。都是表达式; 只提供前向遍历(反向需自行用 le_prev)。
 */
/*
 * List access methods.
 */
#define	LIST_FIRST(head)		((head)->lh_first)
#define	LIST_END(head)			NULL
#define	LIST_EMPTY(head)		(LIST_FIRST(head) == LIST_END(head))
#define	LIST_NEXT(elm, field)		((elm)->field.le_next)

/*
 * LIST_FOREACH(var, head, field): 前向遍历。循环体内不可删除 var; 需删除用 LIST_FOREACH_SAFE。
 */
#define LIST_FOREACH(var, head, field)                                         \
	for ((var) = LIST_FIRST(head); (var) != LIST_END(head);                \
	     (var) = LIST_NEXT(var, field))

/*
 * LIST_FOREACH_SAFE(var, head, field, tvar): 安全遍历, 先用 tvar 缓存后继, 故可安全删除 var。
 */
#define LIST_FOREACH_SAFE(var, head, field, tvar)                              \
	for ((var) = LIST_FIRST(head);                                         \
	     (var) && ((tvar) = LIST_NEXT(var, field), 1); (var) = (tvar))

/*
 * List functions.
 */
/*
 * LIST_INIT(head): 把 LIST 表头置空。运行期创建头结构后必须调用。
 */
#define LIST_INIT(head)                                                        \
	do {                                                                   \
		LIST_FIRST(head) = LIST_END(head);                             \
	} while (0)

/*
 * LIST_INSERT_AFTER(listelm, elm, field): 把 elm 插到 listelm 之后。O(1)。
 * 若 listelm 原是表尾则只是设置 elm->le_next 为 NULL; 同时维护后继的 le_prev 指向 elm。
 */
#define LIST_INSERT_AFTER(listelm, elm, field)                                 \
	do {                                                                   \
		if (((elm)->field.le_next = (listelm)->field.le_next) != NULL) \
			(listelm)->field.le_next->field.le_prev =              \
				&(elm)->field.le_next;                         \
		(listelm)->field.le_next = (elm);                              \
		(elm)->field.le_prev = &(listelm)->field.le_next;              \
	} while (0)

/*
 * LIST_INSERT_BEFORE(listelm, elm, field): 把 elm 插到 listelm 之前。O(1), 借助 listelm 的 le_prev。
 */
#define LIST_INSERT_BEFORE(listelm, elm, field)                                \
	do {                                                                   \
		(elm)->field.le_prev = (listelm)->field.le_prev;               \
		(elm)->field.le_next = (listelm);                              \
		*(listelm)->field.le_prev = (elm);                             \
		(listelm)->field.le_prev = &(elm)->field.le_next;              \
	} while (0)

/*
 * LIST_INSERT_HEAD(head, elm, field): 把 elm 插到表头。O(1), 需正确设置 elm->le_prev 指向头指针。
 */
#define LIST_INSERT_HEAD(head, elm, field)                                     \
	do {                                                                   \
		if (((elm)->field.le_next = (head)->lh_first) != NULL)         \
			(head)->lh_first->field.le_prev =                      \
				&(elm)->field.le_next;                         \
		(head)->lh_first = (elm);                                      \
		(elm)->field.le_prev = &(head)->lh_first;                      \
	} while (0)

/*
 * LIST_REMOVE(elm, field): 从所属 LIST 中摘除 elm(不释放内存)。O(1), 靠 le_prev 直接回填前驱。
 * 不需要 head 参数(节点靠 le_prev 自定位); 调用者须保证 elm 确实在链表中。调试模式毒化两个指针。
 */
#define LIST_REMOVE(elm, field)                                                \
	do {                                                                   \
		if ((elm)->field.le_next != NULL)                              \
			(elm)->field.le_next->field.le_prev =                  \
				(elm)->field.le_prev;                          \
		*(elm)->field.le_prev = (elm)->field.le_next;                  \
		_Q_INVALIDATE((elm)->field.le_prev);                           \
		_Q_INVALIDATE((elm)->field.le_next);                           \
	} while (0)

/*
 * LIST_REPLACE(elm, elm2, field): 用 elm2 顶替 elm 在链表中的位置(不释放 elm)。O(1)。
 * 常用于原地更新节点并保持前后关系不变; 调试模式下毒化被替换掉的 elm。
 */
#define LIST_REPLACE(elm, elm2, field)                                         \
	do {                                                                   \
		if (((elm2)->field.le_next = (elm)->field.le_next) != NULL)    \
			(elm2)->field.le_next->field.le_prev =                 \
				&(elm2)->field.le_next;                        \
		(elm2)->field.le_prev = (elm)->field.le_prev;                  \
		*(elm2)->field.le_prev = (elm2);                               \
		_Q_INVALIDATE((elm)->field.le_prev);                           \
		_Q_INVALIDATE((elm)->field.le_next);                           \
	} while (0)

/*
 * SIMPLEQ —— 单链尾队列定义区。
 * 头结构含 sqh_first 与 sqh_last; sqh_last 存的是"尾节点 next 字段的地址"(struct type 的二级指针),
 * 故尾插 O(1)。节点只含一个 sqe_next 省空间; 但只能从头删除, 且只能前向遍历。
 * 适合 FIFO 队列; 注意 sqh_last 是指针的地址而非元素指针, 这是本类型最容易误用的地方。
 */
/*
 * Simple queue definitions.
 */
/*
 * SIMPLEQ_HEAD(name, type): 声明名为 name 的 SIMPLEQ 头, 含 sqh_first(首元素)与 sqh_last(尾 next 的地址)。
 */
#define SIMPLEQ_HEAD(name, type)                                               \
	struct name {                                                          \
		struct type *sqh_first; /* first element */                    \
		struct type **sqh_last; /* addr of last next element */        \
	}

/*
 * SIMPLEQ_HEAD_INITIALIZER(head): 静态初始化表达式, 令 sqh_first 为 NULL, sqh_last 指向 sqh_first 自身。
 */
#define SIMPLEQ_HEAD_INITIALIZER(head)                                         \
	{                                                                      \
		NULL, &(head).sqh_first                                        \
	}

/*
 * SIMPLEQ_ENTRY(type): 嵌入的单链队列节点, 仅含 sqe_next 指针。同一结构体的多个字段可挂在多条队列上。
 */
#define SIMPLEQ_ENTRY(type)                                                    \
	struct {                                                               \
		struct type *sqe_next; /* next element */                      \
	}

/*
 * SIMPLEQ 访问宏: SIMPLEQ_FIRST 取首元素; SIMPLEQ_END 恒为 NULL 作哨兵; SIMPLEQ_EMPTY 判空;
 * SIMPLEQ_NEXT(elm, field) 取后继。都是表达式且只支持前向遍历。
 */
/*
 * Simple queue access methods.
 */
#define	SIMPLEQ_FIRST(head)	    ((head)->sqh_first)
#define	SIMPLEQ_END(head)	    NULL
#define	SIMPLEQ_EMPTY(head)	    (SIMPLEQ_FIRST(head) == SIMPLEQ_END(head))
#define	SIMPLEQ_NEXT(elm, field)    ((elm)->field.sqe_next)

/*
 * SIMPLEQ_FOREACH(var, head, field): 前向遍历。循环体内不可删除 var; 需删除用 SIMPLEQ_FOREACH_SAFE。
 */
#define SIMPLEQ_FOREACH(var, head, field)                                      \
	for ((var) = SIMPLEQ_FIRST(head); (var) != SIMPLEQ_END(head);          \
	     (var) = SIMPLEQ_NEXT(var, field))

/*
 * SIMPLEQ_FOREACH_SAFE(var, head, field, tvar): 安全遍历, 先用 tvar 缓存后继, 可安全删除 var。
 */
#define SIMPLEQ_FOREACH_SAFE(var, head, field, tvar)                           \
	for ((var) = SIMPLEQ_FIRST(head);                                      \
	     (var) && ((tvar) = SIMPLEQ_NEXT(var, field), 1); (var) = (tvar))

/*
 * Simple queue functions.
 */
/*
 * SIMPLEQ_INIT(head): 初始化头结构, sqh_first 置 NULL 且 sqh_last 指向 sqh_first。运行期头创建后必须调用。
 */
#define SIMPLEQ_INIT(head)                                                     \
	do {                                                                   \
		(head)->sqh_first = NULL;                                      \
		(head)->sqh_last = &(head)->sqh_first;                         \
	} while (0)

/*
 * SIMPLEQ_INSERT_HEAD(head, elm, field): 把 elm 插到队首。O(1)。
 * 若原队列为空, 还需把 sqh_last 改成指向 elm 的 sqe_next(即新的尾 next 地址)。
 */
#define SIMPLEQ_INSERT_HEAD(head, elm, field)                                  \
	do {                                                                   \
		if (((elm)->field.sqe_next = (head)->sqh_first) == NULL)       \
			(head)->sqh_last = &(elm)->field.sqe_next;             \
		(head)->sqh_first = (elm);                                     \
	} while (0)

/*
 * SIMPLEQ_INSERT_TAIL(head, elm, field): 把 elm 追加到队尾。O(1), 靠 sqh_last 直接改写尾 next。
 * 是队列(而非栈)的常规入队接口, 也是本类型相对 SLIST 的核心优势。
 */
#define SIMPLEQ_INSERT_TAIL(head, elm, field)                                  \
	do {                                                                   \
		(elm)->field.sqe_next = NULL;                                  \
		*(head)->sqh_last = (elm);                                     \
		(head)->sqh_last = &(elm)->field.sqe_next;                     \
	} while (0)

/*
 * SIMPLEQ_INSERT_AFTER(head, listelm, elm, field): 把 elm 插到 listelm 之后。O(1)。
 * 若 listelm 原是队尾, 需同步更新 sqh_last 指向 elm 的 sqe_next。
 */
#define SIMPLEQ_INSERT_AFTER(head, listelm, elm, field)                        \
	do {                                                                   \
		if (((elm)->field.sqe_next = (listelm)->field.sqe_next)        \
		    == NULL)                                                   \
			(head)->sqh_last = &(elm)->field.sqe_next;             \
		(listelm)->field.sqe_next = (elm);                             \
	} while (0)

/*
 * SIMPLEQ_REMOVE_HEAD(head, field): 摘除队首元素(不释放内存), 头指针前移。O(1)。
 * 若摘除后队列变空, 需把 sqh_last 复位指向 sqh_first。是出队的标准接口。
 */
#define SIMPLEQ_REMOVE_HEAD(head, field)                                       \
	do {                                                                   \
		if (((head)->sqh_first = (head)->sqh_first->field.sqe_next)    \
		    == NULL)                                                   \
			(head)->sqh_last = &(head)->sqh_first;                 \
	} while (0)

/*
 * SIMPLEQ_REMOVE_AFTER(head, elm, field): 摘除 elm 之后的一个元素(不释放内存)。O(1)。
 * 若删掉的恰是队尾, 需把 sqh_last 复位指向 elm 的 sqe_next; 调用者须确保 elm 后面确有元素。
 */
#define SIMPLEQ_REMOVE_AFTER(head, elm, field)                                 \
	do {                                                                   \
		if (((elm)->field.sqe_next =                                   \
			     (elm)->field.sqe_next->field.sqe_next)            \
		    == NULL)                                                   \
			(head)->sqh_last = &(elm)->field.sqe_next;             \
	} while (0)

/*
 * SIMPLEQ_CONCAT(head1, head2): 把 head2 整体拼到 head1 末尾, 随后清空 head2。O(1)。
 * 实现是把 head1 尾 next 指向 head2 首元素, 再让 head1 的 sqh_last 接管 head2 的 sqh_last。
 * 若 head2 为空则什么都不做; 拼接后 head2 被复位(可继续使用), 但其原有元素已归属 head1。
 */
#define SIMPLEQ_CONCAT(head1, head2)                                           \
	do {                                                                   \
		if (!SIMPLEQ_EMPTY((head2))) {                                 \
			*(head1)->sqh_last = (head2)->sqh_first;               \
			(head1)->sqh_last = (head2)->sqh_last;                 \
			SIMPLEQ_INIT((head2));                                 \
		}                                                              \
	} while (0)

/*
 * XSIMPLEQ —— 异或单链尾队列定义区。
 * 用法与 SIMPLEQ 相同, 区别在于头结构里多了一个随机 cookie, 所有存进链接字段的指针
 * 都与该 cookie 异或后再保存, 读取时再异或还原。这样即使攻击者篡改内存也难以凭空伪造出
 * 一个可用指针, 常用于内核里存放可能被内存投毒的目标。
 * 注意: cookie 是每个队列实例私有的, 故队列不可跨进程或跨实例传递。
 */
/*
 * XOR Simple queue definitions.
 */
/*
 * XSIMPLEQ_HEAD(name, type): 声明异或队列头, 含 sqx_first、sqx_last(尾 next 的地址)和 sqx_cookie。
 */
#define XSIMPLEQ_HEAD(name, type)                                              \
	struct name {                                                          \
		struct type *sqx_first; /* first element */                    \
		struct type **sqx_last; /* addr of last next element */        \
		unsigned long sqx_cookie;                                      \
	}

/*
 * XSIMPLEQ_ENTRY(type): 嵌入的异或队列节点, 仅含一个 sqx_next(存放异或后的指针)。
 */
#define XSIMPLEQ_ENTRY(type)                                                   \
	struct {                                                               \
		struct type *sqx_next; /* next element */                      \
	}

/*
 * XSIMPLEQ 访问宏: XSIMPLEQ_FIRST 取首元素(经 _XOR 还原); XSIMPLEQ_END 恒为 NULL 作哨兵;
 * XSIMPLEQ_EMPTY 判空; XSIMPLEQ_NEXT(head, elm, field) 取后继。
 * 注意 NEXT 需要 head 参数, 因为要用其中的 cookie 做异或还原。
 */
/*
 * XOR Simple queue access methods.
 */
/*
 * XSIMPLEQ_XOR(head, ptr): 把指针 ptr 与头结构的 sqx_cookie 异或, 得到实际存或取的原始值。
 * 因异或可逆, 同一运算既用于写入时混淆也用于读取时还原; 外层 __typeof 保留指针类型。
 */
#define XSIMPLEQ_XOR(head, ptr)                                                \
	((__typeof(ptr))((head)->sqx_cookie ^ (unsigned long)(ptr)))
#define	XSIMPLEQ_FIRST(head)	    XSIMPLEQ_XOR(head, ((head)->sqx_first))
#define	XSIMPLEQ_END(head)	    NULL
#define	XSIMPLEQ_EMPTY(head)	    (XSIMPLEQ_FIRST(head) == XSIMPLEQ_END(head))
#define	XSIMPLEQ_NEXT(head, elm, field)    XSIMPLEQ_XOR(head, ((elm)->field.sqx_next))

/*
 * XSIMPLEQ_FOREACH(var, head, field): 前向遍历, 因每步都需 cookie 故把 head 传入 NEXT。
 * 循环体内不可删除 var; 需删除用 XSIMPLEQ_FOREACH_SAFE。
 */
#define XSIMPLEQ_FOREACH(var, head, field)                                     \
	for ((var) = XSIMPLEQ_FIRST(head); (var) != XSIMPLEQ_END(head);        \
	     (var) = XSIMPLEQ_NEXT(head, var, field))

/*
 * XSIMPLEQ_FOREACH_SAFE(var, head, field, tvar): 安全遍历, 先用 tvar 缓存后继(经 cookie 还原), 可安全删除 var。
 */
#define XSIMPLEQ_FOREACH_SAFE(var, head, field, tvar)                          \
	for ((var) = XSIMPLEQ_FIRST(head);                                     \
	     (var) && ((tvar) = XSIMPLEQ_NEXT(head, var, field), 1);           \
	     (var) = (tvar))

/*
 * XOR Simple queue functions.
 */
/*
 * XSIMPLEQ_INIT(head): 用随机数初始化 cookie(arc4random_buf), 把首指针置为异或后的 NULL,
 * 尾 next 地址置为异或后的 &sqx_first。运行期头创建后必须调用; cookie 每次初始化都不同, 不可复制头结构。
 */
#define XSIMPLEQ_INIT(head)                                                    \
	do {                                                                   \
		arc4random_buf(&(head)->sqx_cookie,                            \
			       sizeof((head)->sqx_cookie));                    \
		(head)->sqx_first = XSIMPLEQ_XOR(head, NULL);                  \
		(head)->sqx_last = XSIMPLEQ_XOR(head, &(head)->sqx_first);     \
	} while (0)

/*
 * XSIMPLEQ_INSERT_HEAD(head, elm, field): 把 elm 插到队首。O(1)。
 * 空队列时需同步把 sqx_last 更新为异或后的 &elm->sqx_next。
 */
#define XSIMPLEQ_INSERT_HEAD(head, elm, field)                                 \
	do {                                                                   \
		if (((elm)->field.sqx_next = (head)->sqx_first)                \
		    == XSIMPLEQ_XOR(head, NULL))                               \
			(head)->sqx_last =                                     \
				XSIMPLEQ_XOR(head, &(elm)->field.sqx_next);    \
		(head)->sqx_first = XSIMPLEQ_XOR(head, (elm));                 \
	} while (0)

/*
 * XSIMPLEQ_INSERT_TAIL(head, elm, field): 把 elm 追加到队尾。O(1)。
 * 先把 elm->sqx_next 写成异或后的 NULL, 再改写原尾 next 并更新 sqx_last 为异或后的 &elm->sqx_next。
 */
#define XSIMPLEQ_INSERT_TAIL(head, elm, field)                                 \
	do {                                                                   \
		(elm)->field.sqx_next = XSIMPLEQ_XOR(head, NULL);              \
		*(XSIMPLEQ_XOR(head, (head)->sqx_last)) =                      \
			XSIMPLEQ_XOR(head, (elm));                             \
		(head)->sqx_last = XSIMPLEQ_XOR(head, &(elm)->field.sqx_next); \
	} while (0)

/*
 * XSIMPLEQ_INSERT_AFTER(head, listelm, elm, field): 把 elm 插到 listelm 之后。O(1)。
 * 若 listelm 原是队尾, 需同步更新 sqx_last。这里直接把 listelm 存的异或值复制给 elm, 不再还原,
 * 因为链中存的都是异或值, 原样传递即可保持混淆一致。
 */
#define XSIMPLEQ_INSERT_AFTER(head, listelm, elm, field)                       \
	do {                                                                   \
		if (((elm)->field.sqx_next = (listelm)->field.sqx_next)        \
		    == XSIMPLEQ_XOR(head, NULL))                               \
			(head)->sqx_last =                                     \
				XSIMPLEQ_XOR(head, &(elm)->field.sqx_next);    \
		(listelm)->field.sqx_next = XSIMPLEQ_XOR(head, (elm));         \
	} while (0)

/*
 * XSIMPLEQ_REMOVE_HEAD(head, field): 摘除队首元素(不释放内存), 头指针前移。O(1)。
 * 若摘除后队列变空, 需把 sqx_last 复位为异或后的 &sqx_first。是出队的标准接口。
 */
#define XSIMPLEQ_REMOVE_HEAD(head, field)                                      \
	do {                                                                   \
		if (((head)->sqx_first = XSIMPLEQ_XOR(head, (head)->sqx_first) \
						 ->field.sqx_next)             \
		    == XSIMPLEQ_XOR(head, NULL))                               \
			(head)->sqx_last =                                     \
				XSIMPLEQ_XOR(head, &(head)->sqx_first);        \
	} while (0)

/*
 * XSIMPLEQ_REMOVE_AFTER(head, elm, field): 摘除 elm 之后的一个元素(不释放内存)。O(1)。
 * 若删掉的恰是队尾, 需把 sqx_last 复位指向 elm 的 sqx_next; 调用者须确保 elm 后面确有元素。
 */
#define XSIMPLEQ_REMOVE_AFTER(head, elm, field)                                \
	do {                                                                   \
		if (((elm)->field.sqx_next =                                   \
			     XSIMPLEQ_XOR(head, (elm)->field.sqx_next)         \
				     ->field.sqx_next)                         \
		    == XSIMPLEQ_XOR(head, NULL))                               \
			(head)->sqx_last =                                     \
				XSIMPLEQ_XOR(head, &(elm)->field.sqx_next);    \
	} while (0)


/*
 * TAILQ —— 双链尾队列定义区, FRR 中最常用的链表。
 * 头结构含 tqh_first 与 tqh_last(尾 next 的地址); 节点含 tqe_next 与 tqe_prev(指向前驱 next 字段的地址)。
 * 因此尾插、任意位置插入删除均 O(1), 且可双向遍历。功能最全, 代价是每节点两个指针。
 * 适合需要从两端操作、频繁中间删改的场景; 只需单向且极少删除时可考虑更省空间的 SLIST / SIMPLEQ。
 */
/*
 * Tail queue definitions.
 */
/*
 * TAILQ_HEAD(name, type): 声明名为 name 的 TAILQ 头, 含 tqh_first(首元素)与 tqh_last(尾 next 的地址)。
 */
#define TAILQ_HEAD(name, type)                                                 \
	struct name {                                                          \
		struct type *tqh_first; /* first element */                    \
		struct type **tqh_last; /* addr of last next element */        \
	}

/*
 * TAILQ_HEAD_INITIALIZER(head): 静态初始化表达式, 令 tqh_first 为 NULL, tqh_last 指向 tqh_first。
 */
#define TAILQ_HEAD_INITIALIZER(head)                                           \
	{                                                                      \
		NULL, &(head).tqh_first                                        \
	}

/*
 * TAILQ_ENTRY(type): 嵌入的双链尾队列节点, 含 tqe_next(下个元素)与 tqe_prev(上个元素 next 字段的地址)。
 * tqe_prev 让删除任意元素无需头结构、无需遍历, 是 TAILQ 双向定位的基础。
 */
#define TAILQ_ENTRY(type)                                                      \
	struct {                                                               \
		struct type *tqe_next;  /* next element */                     \
		struct type **tqe_prev; /* address of previous next element */ \
	}

/*
 * TAILQ 访问宏: TAILQ_FIRST 取首元素; TAILQ_END 恒为 NULL 作哨兵; TAILQ_NEXT(elm, field) 取后继。
 * 反向取前驱用 TAILQ_PREV, 取尾元素用 TAILQ_LAST(两者都需要 headname 参数)。
 */
/*
 * Tail queue access methods.
 */
#define	TAILQ_FIRST(head)		((head)->tqh_first)
#define	TAILQ_END(head)			NULL
#define	TAILQ_NEXT(elm, field)		((elm)->field.tqe_next)
/*
 * TAILQ_LAST(head, headname): 取 TAILQ 的最后一个元素。原理: tqh_last 指向尾节点的 tqe_next 字段,
 * 把该地址按头结构类型重新定型后取其 tqh_last 再解引用, 即得尾元素。O(1)。
 * 需要 headname 参数(头结构类型名)以完成指针的重新定型。
 */
#define TAILQ_LAST(head, headname)                                             \
	(*(((struct headname *)((head)->tqh_last))->tqh_last))
/* XXX */
/*
 * TAILQ_PREV(elm, headname, field): 取 elm 的前驱。原理: elm 的 tqe_prev 指向前驱的 next 字段地址,
 * 若该地址正是头结构的 tqh_first 槽, 则按头类型重新定型后取 tqh_last 即可回退。O(1), 支持反向遍历。
 * 需要 headname 参数。源码在此宏后带有 XXX 注释, 提示该"指针重新定型"写法是已知的可读性取舍点。
 */
#define TAILQ_PREV(elm, headname, field)                                       \
	(*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))
/*
 * TAILQ_EMPTY(head): 判断 TAILQ 是否为空(首元素为 NULL)。
 */
#define TAILQ_EMPTY(head) (TAILQ_FIRST(head) == TAILQ_END(head))

/*
 * TAILQ_FOREACH(var, head, field): 前向遍历。循环体内不可删除 var; 需删除用 TAILQ_FOREACH_SAFE。
 */
#define TAILQ_FOREACH(var, head, field)                                        \
	for ((var) = TAILQ_FIRST(head); (var) != TAILQ_END(head);              \
	     (var) = TAILQ_NEXT(var, field))

/*
 * TAILQ_FOREACH_SAFE(var, head, field, tvar): 安全遍历, 先用 tvar 缓存后继, 可安全删除 var。
 */
#define TAILQ_FOREACH_SAFE(var, head, field, tvar)                             \
	for ((var) = TAILQ_FIRST(head);                                        \
	     (var) != TAILQ_END(head) && ((tvar) = TAILQ_NEXT(var, field), 1); \
	     (var) = (tvar))


/*
 * TAILQ_FOREACH_REVERSE(var, head, headname, field): 从尾到头反向遍历。
 * 需要 headname 参数, 因为取尾元素(TAILQ_LAST)与取前驱(TAILQ_PREV)都要用到头结构类型。
 */
#define TAILQ_FOREACH_REVERSE(var, head, headname, field)                      \
	for ((var) = TAILQ_LAST(head, headname); (var) != TAILQ_END(head);     \
	     (var) = TAILQ_PREV(var, headname, field))

/*
 * TAILQ_FOREACH_REVERSE_SAFE(var, head, headname, field, tvar): 反向安全遍历, 先用 tvar 缓存前驱,
 * 故循环体内可安全删除 var。同样需要 headname 参数。
 */
#define TAILQ_FOREACH_REVERSE_SAFE(var, head, headname, field, tvar)           \
	for ((var) = TAILQ_LAST(head, headname);                               \
	     (var) != TAILQ_END(head)                                          \
	     && ((tvar) = TAILQ_PREV(var, headname, field), 1);                \
	     (var) = (tvar))

/*
 * Tail queue functions.
 */
/*
 * TAILQ_INIT(head): 初始化头结构, tqh_first 置 NULL 且 tqh_last 指向 tqh_first。运行期头创建后必须调用。
 */
#define TAILQ_INIT(head)                                                       \
	do {                                                                   \
		(head)->tqh_first = NULL;                                      \
		(head)->tqh_last = &(head)->tqh_first;                         \
	} while (0)

/*
 * TAILQ_INSERT_HEAD(head, elm, field): 把 elm 插到队首。O(1)。
 * 若原队列为空, 还要把 tqh_last 指向 elm 的 tqe_next(即新的尾 next 地址)。
 */
#define TAILQ_INSERT_HEAD(head, elm, field)                                    \
	do {                                                                   \
		if (((elm)->field.tqe_next = (head)->tqh_first) != NULL)       \
			(head)->tqh_first->field.tqe_prev =                    \
				&(elm)->field.tqe_next;                        \
		else                                                           \
			(head)->tqh_last = &(elm)->field.tqe_next;             \
		(head)->tqh_first = (elm);                                     \
		(elm)->field.tqe_prev = &(head)->tqh_first;                    \
	} while (0)

/*
 * TAILQ_INSERT_TAIL(head, elm, field): 把 elm 追加到队尾。O(1), 靠 tqh_last 直接改写尾 next 并更新之。
 */
#define TAILQ_INSERT_TAIL(head, elm, field)                                    \
	do {                                                                   \
		(elm)->field.tqe_next = NULL;                                  \
		(elm)->field.tqe_prev = (head)->tqh_last;                      \
		*(head)->tqh_last = (elm);                                     \
		(head)->tqh_last = &(elm)->field.tqe_next;                     \
	} while (0)

/*
 * TAILQ_INSERT_AFTER(head, listelm, elm, field): 把 elm 插到 listelm 之后。O(1)。
 * 若 listelm 原是队尾, 需同步更新 tqh_last; 并维护后继元素的 tqe_prev 指向 elm 的 tqe_next。
 */
#define TAILQ_INSERT_AFTER(head, listelm, elm, field)                          \
	do {                                                                   \
		if (((elm)->field.tqe_next = (listelm)->field.tqe_next)        \
		    != NULL)                                                   \
			(elm)->field.tqe_next->field.tqe_prev =                \
				&(elm)->field.tqe_next;                        \
		else                                                           \
			(head)->tqh_last = &(elm)->field.tqe_next;             \
		(listelm)->field.tqe_next = (elm);                             \
		(elm)->field.tqe_prev = &(listelm)->field.tqe_next;            \
	} while (0)

/*
 * TAILQ_INSERT_BEFORE(listelm, elm, field): 把 elm 插到 listelm 之前。O(1), 借助 listelm 的 tqe_prev。
 * 插入后需把 listelm 的 tqe_prev 更新为 elm 的 tqe_next。
 */
#define TAILQ_INSERT_BEFORE(listelm, elm, field)                               \
	do {                                                                   \
		(elm)->field.tqe_prev = (listelm)->field.tqe_prev;             \
		(elm)->field.tqe_next = (listelm);                             \
		*(listelm)->field.tqe_prev = (elm);                            \
		(listelm)->field.tqe_prev = &(elm)->field.tqe_next;            \
	} while (0)

/*
 * TAILQ_REMOVE(head, elm, field): 从所属 TAILQ 中摘除 elm(不释放内存)。O(1), 靠 tqe_prev 直接回填前驱。
 * 若 elm 是尾元素, 还需回写 head->tqh_last 指向 elm 的 tqe_prev。调试模式下毒化 elm 的两个指针。
 */
#define TAILQ_REMOVE(head, elm, field)                                         \
	do {                                                                   \
		if (((elm)->field.tqe_next) != NULL)                           \
			(elm)->field.tqe_next->field.tqe_prev =                \
				(elm)->field.tqe_prev;                         \
		else                                                           \
			(head)->tqh_last = (elm)->field.tqe_prev;              \
		*(elm)->field.tqe_prev = (elm)->field.tqe_next;                \
		_Q_INVALIDATE((elm)->field.tqe_prev);                          \
		_Q_INVALIDATE((elm)->field.tqe_next);                          \
	} while (0)

/*
 * TAILQ_REPLACE(head, elm, elm2, field): 用 elm2 顶替 elm 在队列中的位置(不释放 elm)。O(1)。
 * 需处理 elm 是尾元素时 tqh_last 的更新; 调试模式下毒化被替换掉的 elm。
 */
#define TAILQ_REPLACE(head, elm, elm2, field)                                  \
	do {                                                                   \
		if (((elm2)->field.tqe_next = (elm)->field.tqe_next) != NULL)  \
			(elm2)->field.tqe_next->field.tqe_prev =               \
				&(elm2)->field.tqe_next;                       \
		else                                                           \
			(head)->tqh_last = &(elm2)->field.tqe_next;            \
		(elm2)->field.tqe_prev = (elm)->field.tqe_prev;                \
		*(elm2)->field.tqe_prev = (elm2);                              \
		_Q_INVALIDATE((elm)->field.tqe_prev);                          \
		_Q_INVALIDATE((elm)->field.tqe_next);                          \
	} while (0)

/*
 * TAILQ_CONCAT(head1, head2, field): 把 head2 整体拼到 head1 末尾, 随后清空 head2。O(1)。
 * 把 head1 尾 next 指向 head2 首元素并回填其 tqe_prev, 再让 head1 的 tqh_last 接管 head2 的 tqh_last。
 * 若 head2 为空则什么都不做; 拼接后 head2 被复位(可继续使用), 其原有元素已归属 head1。
 */
#define TAILQ_CONCAT(head1, head2, field)                                      \
	do {                                                                   \
		if (!TAILQ_EMPTY(head2)) {                                     \
			*(head1)->tqh_last = (head2)->tqh_first;               \
			(head2)->tqh_first->field.tqe_prev =                   \
				(head1)->tqh_last;                             \
			(head1)->tqh_last = (head2)->tqh_last;                 \
			TAILQ_INIT((head2));                                   \
		}                                                              \
	} while (0)

#ifdef __cplusplus
}
#endif

#endif /* !_SYS_QUEUE_H_ */
