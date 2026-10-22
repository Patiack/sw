// SPDX-License-Identifier: BSD-3-Clause
/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 *	@(#)queue.h	8.5 (Berkeley) 8/20/94
 * $FreeBSD$
 */

#ifndef _SYS_QUEUE_H_
#define	_SYS_QUEUE_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * 文件导读：FreeBSD 原版 queue.h（被 FRR 整体引入）
 * ============================================================================
 *
 * 1. 这是什么
 *    FreeBSD 的 BSD-3-Clause 授权队列宏头文件，定义了四类线性数据结构。
 *    FRR 把它原样搬进 lib/，上层通过 lib/queue.h 间接包含它（非 OpenBSD
 *    平台）。引入动机：类型安全、链接节点内联在元素里（零额外内存分配）、
 *    经过 BSD 数十年生产验证、且是纯头文件的宏实现，没有链接期依赖。
 *
 * 2. 四类结构（决定该用哪个）
 *    SLIST  单链表      只有前向指针；省内存、只可前向遍历；删任意元素
 *                       O(n)；适合元素多、几乎不删、或实现 LIFO 栈。
 *    STAILQ 单链尾队列  头尾都存指针；头尾都能 O(1) 入队；只前向遍历；
 *                       适合元素多、少删除、或实现 FIFO 队列。
 *    LIST   双向链表    元素带 le_prev 反向指针；任意元素 O(1) 删除；
 *                       但链表本身只能前向遍历。
 *    TAILQ  双链尾队列  元素双向链接且头带尾指针；双端 O(1)、可双向
 *                       遍历、可 O(1) 删除任意元素；功能最全。
 *
 * 3. 命名约定（每类都有一套同后缀宏）
 *    _HEAD / _HEAD_INITIALIZER / _ENTRY / _INIT / _EMPTY / _FIRST / _NEXT
 *    / _LAST / _FOREACH / _FOREACH_SAFE / _INSERT_HEAD / _INSERT_AFTER
 *    / _INSERT_TAIL / _REMOVE / _SWAP 等。文件上方的英文总表已列出各结构
 *    支持哪些后缀，可对照阅读。参数 field 一律指「元素内那个 ENTRY 成员的
 *    成员名」；type / field 写错会算错地址，务必核对。
 *
 * 4. 调试设施
 *    定义 QUEUE_MACRO_DEBUG 时，QMD_TRACE_HEAD / QMD_TRACE_ELEM 会记录
 *    头 / 元素最近两次被改的文件名与行号（struct qm_trace），TRASHIT 会把
 *    删除后的链接指针写成哨兵值便于崩溃定位。不定义时这些宏全部展开为空，
 *    零运行时开销。
 *
 * 5. 阅读顺序
 *    自上而下按 SLIST -> STAILQ -> LIST -> TAILQ 四节读，每节先看 _HEAD /
 *    _ENTRY / _INIT，再看 _INSERT_* / _REMOVE* / _FOREACH*。TAILQ 功能
 *    最全，放最后作为「集大成」理解。
 * ============================================================================
 */

/*
 * This file defines four types of data structures: singly-linked lists,
 * singly-linked tail queues, lists and tail queues.
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
 * A singly-linked tail queue is headed by a pair of pointers, one to the
 * head of the list and the other to the tail of the list. The elements are
 * singly linked for minimum space and pointer manipulation overhead at the
 * expense of O(n) removal for arbitrary elements. New elements can be added
 * to the list after an existing element, at the head of the list, or at the
 * end of the list. Elements being removed from the head of the tail queue
 * should use the explicit macro for this purpose for optimum efficiency.
 * A singly-linked tail queue may only be traversed in the forward direction.
 * Singly-linked tail queues are ideal for applications with large datasets
 * and few or no removals or for implementing a FIFO queue.
 *
 * A list is headed by a single forward pointer (or an array of forward
 * pointers for a hash table header). The elements are doubly linked
 * so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before
 * or after an existing element or at the head of the list. A list
 * may only be traversed in the forward direction.
 *
 * A tail queue is headed by a pair of pointers, one to the head of the
 * list and the other to the tail of the list. The elements are doubly
 * linked so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before or
 * after an existing element, at the head of the list, or at the end of
 * the list. A tail queue may be traversed in either direction.
 *
 * For details on the use of these macros, see the queue(3) manual page.
 *
 *
 *				SLIST	LIST	STAILQ	TAILQ
 * _HEAD			+	+	+	+
 * _HEAD_INITIALIZER		+	+	+	+
 * _ENTRY			+	+	+	+
 * _INIT			+	+	+	+
 * _EMPTY			+	+	+	+
 * _FIRST			+	+	+	+
 * _NEXT			+	+	+	+
 * _PREV			-	-	-	+
 * _LAST			-	-	+	+
 * _FOREACH			+	+	+	+
 * _FOREACH_SAFE		+	+	+	+
 * _FOREACH_REVERSE		-	-	-	+
 * _FOREACH_REVERSE_SAFE	-	-	-	+
 * _INSERT_HEAD			+	+	+	+
 * _INSERT_BEFORE		-	+	-	+
 * _INSERT_AFTER		+	+	+	+
 * _INSERT_TAIL			-	-	+	+
 * _CONCAT			-	-	+	+
 * _REMOVE_AFTER		+	-	+	-
 * _REMOVE_HEAD			+	-	+	-
 * _REMOVE			+	+	+	+
 * _SWAP			+	+	+	+
 *
 */
#ifdef QUEUE_MACRO_DEBUG
/* Store the last 2 places the queue element or head was altered */
/*
 * struct qm_trace：仅在定义 QUEUE_MACRO_DEBUG 时启用的调试结构，记录某个
 * 队列头 / 元素最近两次被修改的位置（文件名与行号），便于排查「链表被谁
 * 改坏」。由各 _INIT / _INSERT* / _REMOVE* / _SWAP 宏内的 QMD_TRACE_* 更新。
 */
struct qm_trace {
	char *lastfile;
	int lastline;
	char *prevfile;
	int prevline;
};

/*
 * TRACEBUF：在队列头 / 元素结构里插入一个 struct qm_trace trace 成员。
 * 仅在 QUEUE_MACRO_DEBUG 下展开，否则为空宏，不占任何空间。
 */
#define	TRACEBUF	struct qm_trace trace;
/*
 * TRASHIT(x)：调试时把已删除 / 已释放的链接指针赋为 (void *)-1 哨兵值，
 * 使后续误用该指针时立即崩溃以便定位；非调试构建为空宏。
 */
#define	TRASHIT(x)	do {(x) = (void *)-1;} while (0)
/*
 * QMD_SAVELINK(name, link)：调试时把某链接字段的地址存入局部变量 name，
 * 供随后的 TRASHIT 在删除后把它清成哨兵值；非调试构建为空宏（不留变量）。
 */
#define	QMD_SAVELINK(name, link)	void **name = (void *)&(link)

/*
 * QMD_TRACE_HEAD(head)：调试时把 head 本次修改的位置（__FILE__ / __LINE__）
 * 写入其 trace，旧记录压入 prev；非调试构建为空宏。
 */
#define QMD_TRACE_HEAD(head)                                                   \
	do {                                                                   \
		(head)->trace.prevline = (head)->trace.lastline;               \
		(head)->trace.prevfile = (head)->trace.lastfile;               \
		(head)->trace.lastline = __LINE__;                             \
		(head)->trace.lastfile = __FILE__;                             \
	} while (0)

/*
 * QMD_TRACE_ELEM(elem)：调试时把元素链接节点 elem 本次修改的位置记入其
 * trace，旧记录压入 prev；非调试构建为空宏。
 */
#define QMD_TRACE_ELEM(elem)                                                   \
	do {                                                                   \
		(elem)->trace.prevline = (elem)->trace.lastline;               \
		(elem)->trace.prevfile = (elem)->trace.lastfile;               \
		(elem)->trace.lastline = __LINE__;                             \
		(elem)->trace.lastfile = __FILE__;                             \
	} while (0)

/*
 * 非调试构建分支：上面这些 QMD_TRACE_* / TRACEBUF / TRASHIT / QMD_SAVELINK
 * 全部退化为空定义，既不产生代码也不产生数据，故生产环境使用零开销。
 */
#else
#define	QMD_TRACE_ELEM(elem)
#define	QMD_TRACE_HEAD(head)
#define	QMD_SAVELINK(name, link)
#define	TRACEBUF
#define	TRASHIT(x)
#endif /* QUEUE_MACRO_DEBUG */

/*
 * Singly-linked List declarations.
 */
/*
 * 中文小结：本节定义单链表的「头结构」与「元素链接节点」。单链表只有前向
 * 指针，最省内存，但删除任意元素需 O(n) 且只能前向遍历。
 */
/*
 * SLIST_HEAD(name, type)：定义单链表头，只含一个前向指针 slh_first。
 * name 是头结构体名，type 是元素类型名。适合元素多、几乎不删、或做 LIFO。
 */
#define SLIST_HEAD(name, type)                                                 \
	struct name {                                                          \
		struct type *slh_first; /* first element */                    \
	}

/*
 * SLIST_HEAD_INITIALIZER(head)：单链表头的静态初始化器，把首指针置 NULL。
 * 只能用在全局 / 静态变量的定义处；运行时初始化请用 SLIST_INIT。
 */
#define SLIST_HEAD_INITIALIZER(head)                                           \
	{                                                                      \
		NULL                                                           \
	}

/*
 * SLIST_ENTRY(type)：嵌入元素中的链接节点，仅含一个 next 指针 sle_next。
 * 同一元素若同时挂在多条链表上，需为每条链表各用一个 SLIST_ENTRY 成员。
 */
#define SLIST_ENTRY(type)                                                      \
	struct {                                                               \
		struct type *sle_next; /* next element */                      \
	}

/*
 * Singly-linked List functions.
 */
/*
 * 中文小结：本节是单链表的全部操作宏。头部插入 / 头部删除为 O(1)，按元素
 * 删除平均 O(n)。若需要频繁 O(1) 删除任意元素，请改用 LIST 或 TAILQ。
 */
/*
 * SLIST_EMPTY(head)：判断单链表是否为空，非 0 表示空。
 */
#define	SLIST_EMPTY(head)	((head)->slh_first == NULL)

/*
 * SLIST_FIRST(head)：返回单链表首元素指针，空链表时为 NULL。
 */
#define	SLIST_FIRST(head)	((head)->slh_first)

/*
 * SLIST_FOREACH(var, head, field)：正序逐个遍历，var 是循环变量。field 指
 * 元素内 SLIST_ENTRY 成员的成员名。遍历中删除当前元素不安全，需边遍历边删
 * 请改用 SLIST_FOREACH_SAFE。
 */
#define SLIST_FOREACH(var, head, field)                                        \
	for ((var) = SLIST_FIRST((head)); (var);                               \
	     (var) = SLIST_NEXT((var), field))

/*
 * SLIST_FOREACH_SAFE(var, head, field, tvar)：可删除元素的安全遍历。tvar 是
 * 调用方提供的临时变量，先记住下一元素再处理 var，故循环体内可安全删除当前
 * var 指向的元素。
 */
#define SLIST_FOREACH_SAFE(var, head, field, tvar)                             \
	for ((var) = SLIST_FIRST((head));                                      \
	     (var) && ((tvar) = SLIST_NEXT((var), field), 1); (var) = (tvar))

/*
 * SLIST_FOREACH_PREVPTR(var, varp, head, field)：遍历时同时给出「指向当前
 * 元素指针的指针」varp，便于在循环体内原地修改链接（如删除或替换当前节点），
 * 避免为删除再从头找前驱。var 仍是当前元素。
 */
#define SLIST_FOREACH_PREVPTR(var, varp, head, field)                          \
	for ((varp) = &SLIST_FIRST((head)); ((var) = *(varp)) != NULL;         \
	     (varp) = &SLIST_NEXT((var), field))

/*
 * SLIST_INIT(head)：把一个单链表头初始化为空链表（运行时调用）。
 */
#define SLIST_INIT(head)                                                       \
	do {                                                                   \
		SLIST_FIRST((head)) = NULL;                                    \
	} while (0)

/*
 * SLIST_INSERT_AFTER(slistelm, elm, field)：把 elm 插到 slistelm 之后。
 * 要求 slistelm 已在链表中。注意：本宏无 head 参数。
 */
#define SLIST_INSERT_AFTER(slistelm, elm, field)                               \
	do {                                                                   \
		SLIST_NEXT((elm), field) = SLIST_NEXT((slistelm), field);      \
		SLIST_NEXT((slistelm), field) = (elm);                         \
	} while (0)

/*
 * SLIST_INSERT_HEAD(head, elm, field)：把 elm 插到链表头部，O(1)。
 */
#define SLIST_INSERT_HEAD(head, elm, field)                                    \
	do {                                                                   \
		SLIST_NEXT((elm), field) = SLIST_FIRST((head));                \
		SLIST_FIRST((head)) = (elm);                                   \
	} while (0)

/*
 * SLIST_NEXT(elm, field)：返回 elm 的后继元素指针，链尾为 NULL。
 */
#define	SLIST_NEXT(elm, field)	((elm)->field.sle_next)

/*
 * SLIST_REMOVE(head, elm, type, field)：从链表中删除元素 elm。elm 是首元素
 * 时走 O(1) 的 SLIST_REMOVE_HEAD；否则需 O(n) 线性查找其前驱再删。type 必须
 * 是元素类型名（用于声明临时前驱变量）。
 */
#define SLIST_REMOVE(head, elm, type, field)                                   \
	do {                                                                   \
		QMD_SAVELINK(oldnext, (elm)->field.sle_next);                  \
		if (SLIST_FIRST((head)) == (elm)) {                            \
			SLIST_REMOVE_HEAD((head), field);                      \
		} else {                                                       \
			struct type *curelm = SLIST_FIRST((head));             \
			while (SLIST_NEXT(curelm, field) != (elm))             \
				curelm = SLIST_NEXT(curelm, field);            \
			SLIST_REMOVE_AFTER(curelm, field);                     \
		}                                                              \
		TRASHIT(*oldnext);                                             \
	} while (0)

/*
 * SLIST_REMOVE_AFTER(elm, field)：删除 elm 的后继元素（该后继必须存在）。
 * 无 head 参数，O(1)。
 */
#define SLIST_REMOVE_AFTER(elm, field)                                         \
	do {                                                                   \
		SLIST_NEXT(elm, field) =                                       \
			SLIST_NEXT(SLIST_NEXT(elm, field), field);             \
	} while (0)

/*
 * SLIST_REMOVE_HEAD(head, field)：删除链表首元素，O(1)。删除首元素时应优先
 * 用它而不是 SLIST_REMOVE，以免线性查找。
 */
#define SLIST_REMOVE_HEAD(head, field)                                         \
	do {                                                                   \
		SLIST_FIRST((head)) = SLIST_NEXT(SLIST_FIRST((head)), field);  \
	} while (0)

/*
 * SLIST_SWAP(head1, head2, type)：交换两个同类单链表的全部内容。
 */
#define SLIST_SWAP(head1, head2, type)                                         \
	do {                                                                   \
		struct type *swap_first = SLIST_FIRST(head1);                  \
		SLIST_FIRST(head1) = SLIST_FIRST(head2);                       \
		SLIST_FIRST(head2) = swap_first;                               \
	} while (0)

/*
 * Singly-linked Tail queue declarations.
 */
/*
 * 中文小结：STAILQ = 单链尾队列。比 SLIST 多存一个「尾指针」，使头尾入队
 * 都是 O(1)，是「元素多、少删除、要 FIFO」场景的首选。
 */
/*
 * STAILQ_HEAD(name, type)：定义单链尾队列头，含首指针 stqh_first 和「指向
 * 末尾 next 字段的地址」stqh_last。头尾都能 O(1) 操作，适合实现 FIFO。
 */
#define STAILQ_HEAD(name, type)                                                \
	struct name {                                                          \
		struct type *stqh_first; /* first element */                   \
		struct type **stqh_last; /* addr of last next element */       \
	}

/*
 * STAILQ_HEAD_INITIALIZER(head)：尾队列头的静态初始化器（首为 NULL，尾指向
 * 首字段）。仅用于全局 / 静态变量，运行时用 STAILQ_INIT。
 */
#define STAILQ_HEAD_INITIALIZER(head)                                          \
	{                                                                      \
		NULL, &(head).stqh_first                                       \
	}

/*
 * STAILQ_ENTRY(type)：嵌入元素中的链接节点，仅含一个 next 指针 stqe_next。
 */
#define STAILQ_ENTRY(type)                                                     \
	struct {                                                               \
		struct type *stqe_next; /* next element */                     \
	}

/*
 * Singly-linked Tail queue functions.
 */
/*
 * 中文小结：本节是 STAILQ 的全部操作宏。相较 SLIST 多了 INSERT_TAIL / LAST
 * / CONCAT，尾插入与拼接为 O(1)；删除任意元素仍为 O(n)。
 */
/*
 * STAILQ_CONCAT(head1, head2)：把 head2 整体接到 head1 尾部并清空 head2，
 * O(1)。要求两者为同一类型的尾队列。
 */
#define STAILQ_CONCAT(head1, head2)                                            \
	do {                                                                   \
		if (!STAILQ_EMPTY((head2))) {                                  \
			*(head1)->stqh_last = (head2)->stqh_first;             \
			(head1)->stqh_last = (head2)->stqh_last;               \
			STAILQ_INIT((head2));                                  \
		}                                                              \
	} while (0)

/*
 * STAILQ_EMPTY(head)：判断尾队列是否为空，非 0 表示空。
 */
#define	STAILQ_EMPTY(head)	((head)->stqh_first == NULL)

/*
 * STAILQ_FIRST(head)：返回队首元素指针，空队列时为 NULL。
 */
#define	STAILQ_FIRST(head)	((head)->stqh_first)

/*
 * STAILQ_FOREACH(var, head, field)：正序逐个遍历。删除当前元素不安全，需
 * 边遍历边删请用 STAILQ_FOREACH_SAFE。
 */
#define STAILQ_FOREACH(var, head, field)                                       \
	for ((var) = STAILQ_FIRST((head)); (var);                              \
	     (var) = STAILQ_NEXT((var), field))


/*
 * STAILQ_FOREACH_SAFE(var, head, field, tvar)：可删除元素的安全遍历。tvar 为
 * 调用方提供的临时变量，先记下一元素再处理 var。
 */
#define STAILQ_FOREACH_SAFE(var, head, field, tvar)                            \
	for ((var) = STAILQ_FIRST((head));                                     \
	     (var) && ((tvar) = STAILQ_NEXT((var), field), 1); (var) = (tvar))

/*
 * STAILQ_INIT(head)：把尾队列初始化为空（首为 NULL，尾指回首字段）。
 */
#define STAILQ_INIT(head)                                                      \
	do {                                                                   \
		STAILQ_FIRST((head)) = NULL;                                   \
		(head)->stqh_last = &STAILQ_FIRST((head));                     \
	} while (0)

/*
 * STAILQ_INSERT_AFTER(head, tqelm, elm, field)：把 elm 插到 tqelm 之后。
 * tqelm 必须已在队列中；若插到末尾会同步更新尾指针。
 */
#define STAILQ_INSERT_AFTER(head, tqelm, elm, field)                           \
	do {                                                                   \
		if ((STAILQ_NEXT((elm), field) = STAILQ_NEXT((tqelm), field))  \
		    == NULL)                                                   \
			(head)->stqh_last = &STAILQ_NEXT((elm), field);        \
		STAILQ_NEXT((tqelm), field) = (elm);                           \
	} while (0)

/*
 * STAILQ_INSERT_HEAD(head, elm, field)：把 elm 插到队首；若原队列为空会同步
 * 更新尾指针。
 */
#define STAILQ_INSERT_HEAD(head, elm, field)                                   \
	do {                                                                   \
		if ((STAILQ_NEXT((elm), field) = STAILQ_FIRST((head)))         \
		    == NULL)                                                   \
			(head)->stqh_last = &STAILQ_NEXT((elm), field);        \
		STAILQ_FIRST((head)) = (elm);                                  \
	} while (0)

/*
 * STAILQ_INSERT_TAIL(head, elm, field)：把 elm 插到队尾，O(1)。尾队列相对
 * 普通单链表的主要优势即在此。
 */
#define STAILQ_INSERT_TAIL(head, elm, field)                                   \
	do {                                                                   \
		STAILQ_NEXT((elm), field) = NULL;                              \
		*(head)->stqh_last = (elm);                                    \
		(head)->stqh_last = &STAILQ_NEXT((elm), field);                \
	} while (0)

/*
 * STAILQ_LAST(head, type, field)：返回队尾元素指针，空队列返回 NULL。由尾
 * 指针 stqh_last 减去 field 相对 type 的偏移还原地址，type / field 必须正确。
 */
#define STAILQ_LAST(head, type, field)                                         \
	(STAILQ_EMPTY((head))                                                  \
		 ? NULL                                                        \
		 : ((struct type *)(void *)((char *)((head)->stqh_last)        \
					    - offsetof(struct type, field))))

/*
 * STAILQ_NEXT(elm, field)：返回 elm 的后继元素指针，末尾为 NULL。
 */
#define	STAILQ_NEXT(elm, field)	((elm)->field.stqe_next)

/*
 * STAILQ_REMOVE(head, elm, type, field)：从队列中删除元素 elm。elm 是队首时
 * O(1)，否则 O(n) 找前驱再删。type 为元素类型名。
 */
#define STAILQ_REMOVE(head, elm, type, field)                                  \
	do {                                                                   \
		QMD_SAVELINK(oldnext, (elm)->field.stqe_next);                 \
		if (STAILQ_FIRST((head)) == (elm)) {                           \
			STAILQ_REMOVE_HEAD((head), field);                     \
		} else {                                                       \
			struct type *curelm = STAILQ_FIRST((head));            \
			while (STAILQ_NEXT(curelm, field) != (elm))            \
				curelm = STAILQ_NEXT(curelm, field);           \
			STAILQ_REMOVE_AFTER(head, curelm, field);              \
		}                                                              \
		TRASHIT(*oldnext);                                             \
	} while (0)

/*
 * STAILQ_REMOVE_AFTER(head, elm, field)：删除 elm 的后继元素；若删掉的是尾
 * 元素会同步更新尾指针。
 */
#define STAILQ_REMOVE_AFTER(head, elm, field)                                  \
	do {                                                                   \
		if ((STAILQ_NEXT(elm, field) =                                 \
			     STAILQ_NEXT(STAILQ_NEXT(elm, field), field))      \
		    == NULL)                                                   \
			(head)->stqh_last = &STAILQ_NEXT((elm), field);        \
	} while (0)

/*
 * STAILQ_REMOVE_HEAD(head, field)：删除队首元素，O(1)；若删空会同步更新尾
 * 指针。
 */
#define STAILQ_REMOVE_HEAD(head, field)                                        \
	do {                                                                   \
		if ((STAILQ_FIRST((head)) =                                    \
			     STAILQ_NEXT(STAILQ_FIRST((head)), field))         \
		    == NULL)                                                   \
			(head)->stqh_last = &STAILQ_FIRST((head));             \
	} while (0)

/*
 * STAILQ_SWAP(head1, head2, type)：交换两个同类尾队列的全部内容；交换后若
 * 某个头变空，会把其尾指针指回自身首字段以便复用。
 */
#define STAILQ_SWAP(head1, head2, type)                                        \
	do {                                                                   \
		struct type *swap_first = STAILQ_FIRST(head1);                 \
		struct type **swap_last = (head1)->stqh_last;                  \
		STAILQ_FIRST(head1) = STAILQ_FIRST(head2);                     \
		(head1)->stqh_last = (head2)->stqh_last;                       \
		STAILQ_FIRST(head2) = swap_first;                              \
		(head2)->stqh_last = swap_last;                                \
		if (STAILQ_EMPTY(head1))                                       \
			(head1)->stqh_last = &STAILQ_FIRST(head1);             \
		if (STAILQ_EMPTY(head2))                                       \
			(head2)->stqh_last = &STAILQ_FIRST(head2);             \
	} while (0)


/*
 * List declarations.
 */
/*
 * 中文小结：LIST 是双向链表。元素带 le_prev 反向指针，因此删除任意元素为
 * O(1)，但链表本身仍只能前向遍历（没有 _LAST / _FOREACH_REVERSE）。
 */
/*
 * LIST_HEAD(name, type)：定义双向链表头，只含一个前向指针 lh_first。元素是
 * 双向链接，故任意元素都能 O(1) 删除；但链表本身只能前向遍历。
 */
#define LIST_HEAD(name, type)                                                  \
	struct name {                                                          \
		struct type *lh_first; /* first element */                     \
	}

/*
 * LIST_HEAD_INITIALIZER(head)：双向链表头的静态初始化器（首指针置 NULL）。
 * 仅用于全局 / 静态变量，运行时用 LIST_INIT。
 */
#define LIST_HEAD_INITIALIZER(head)                                            \
	{                                                                      \
		NULL                                                           \
	}

/*
 * LIST_ENTRY(type)：嵌入元素的链接节点，含 next 指针与 le_prev ——后者是
 * 「指向前驱 next 字段地址」的指针，正因如此删除任意元素无需遍历找前驱。
 */
#define LIST_ENTRY(type)                                                       \
	struct {                                                               \
		struct type *le_next;  /* next element */                      \
		struct type **le_prev; /* address of previous next element */  \
	}

/*
 * List functions.
 */
/*
 * 中文小结：本节是 LIST 的全部操作宏。INSERT_BEFORE / REMOVE 借助 le_prev
 * 做到 O(1)，这是它相对 SLIST 的最大价值；代价是每元素多一个反向指针。
 */

#if (defined(_KERNEL) && defined(INVARIANTS))
/*
 * QMD_LIST_CHECK_HEAD(head, field)：仅在「内核态且开 INVARIANTS」时生效的
 * 一致性校验，检查头与首元素的双向链接自洽；其它情况展开为空，零开销。
 */
#define QMD_LIST_CHECK_HEAD(head, field)                                       \
	do {                                                                   \
		if (LIST_FIRST((head)) != NULL                                 \
		    && LIST_FIRST((head))->field.le_prev                       \
			       != &LIST_FIRST((head)))                         \
			panic("Bad list head %p first->prev != head", (head)); \
	} while (0)

/*
 * QMD_LIST_CHECK_NEXT(elm, field)：调试下校验 elm 与其后继的双向链接自洽；
 * 非调试构建展开为空。
 */
#define QMD_LIST_CHECK_NEXT(elm, field)                                        \
	do {                                                                   \
		if (LIST_NEXT((elm), field) != NULL                            \
		    && LIST_NEXT((elm), field)->field.le_prev                  \
			       != &((elm)->field.le_next))                     \
			panic("Bad link elm %p next->prev != elm", (elm));     \
	} while (0)

/*
 * QMD_LIST_CHECK_PREV(elm, field)：调试下校验 elm 与其前驱的双向链接自洽；
 * 非调试构建展开为空。
 */
#define QMD_LIST_CHECK_PREV(elm, field)                                        \
	do {                                                                   \
		if (*(elm)->field.le_prev != (elm))                            \
			panic("Bad link elm %p prev->next != elm", (elm));     \
	} while (0)
/*
 * 非调试构建分支：LIST 的三个 QMD_LIST_CHECK_* 一致性检查宏退化为空，
 * 运行时零开销。
 */
#else
#define	QMD_LIST_CHECK_HEAD(head, field)
#define	QMD_LIST_CHECK_NEXT(elm, field)
#define	QMD_LIST_CHECK_PREV(elm, field)
#endif /* (_KERNEL && INVARIANTS) */

/*
 * LIST_EMPTY(head)：判断双向链表是否为空，非 0 表示空。
 */
#define	LIST_EMPTY(head)	((head)->lh_first == NULL)

/*
 * LIST_FIRST(head)：返回首元素指针，空链表时为 NULL。
 */
#define	LIST_FIRST(head)	((head)->lh_first)

/*
 * LIST_FOREACH(var, head, field)：正序逐个遍历。删除当前元素不安全，需边
 * 遍历边删请用 LIST_FOREACH_SAFE。LIST 没有反向遍历。
 */
#define LIST_FOREACH(var, head, field)                                         \
	for ((var) = LIST_FIRST((head)); (var); (var) = LIST_NEXT((var), field))

/*
 * LIST_FOREACH_SAFE(var, head, field, tvar)：可删除元素的安全遍历。tvar 为
 * 调用方提供的临时变量，先记下一元素再处理 var。
 */
#define LIST_FOREACH_SAFE(var, head, field, tvar)                              \
	for ((var) = LIST_FIRST((head));                                       \
	     (var) && ((tvar) = LIST_NEXT((var), field), 1); (var) = (tvar))

/*
 * LIST_INIT(head)：把双向链表头初始化为空。
 */
#define LIST_INIT(head)                                                        \
	do {                                                                   \
		LIST_FIRST((head)) = NULL;                                     \
	} while (0)

/*
 * LIST_INSERT_AFTER(listelm, elm, field)：把 elm 插到 listelm 之后。listelm
 * 必须已在链表中；宏会同步维护后继元素的 le_prev 反向指针。
 */
#define LIST_INSERT_AFTER(listelm, elm, field)                                 \
	do {                                                                   \
		QMD_LIST_CHECK_NEXT(listelm, field);                           \
		if ((LIST_NEXT((elm), field) = LIST_NEXT((listelm), field))    \
		    != NULL)                                                   \
			LIST_NEXT((listelm), field)->field.le_prev =           \
				&LIST_NEXT((elm), field);                      \
		LIST_NEXT((listelm), field) = (elm);                           \
		(elm)->field.le_prev = &LIST_NEXT((listelm), field);           \
	} while (0)

/*
 * LIST_INSERT_BEFORE(listelm, elm, field)：把 elm 插到 listelm 之前。借助
 * listelm 的 le_prev，无需从头遍历即可 O(1) 完成。这是 SLIST 做不到的。
 */
#define LIST_INSERT_BEFORE(listelm, elm, field)                                \
	do {                                                                   \
		QMD_LIST_CHECK_PREV(listelm, field);                           \
		(elm)->field.le_prev = (listelm)->field.le_prev;               \
		LIST_NEXT((elm), field) = (listelm);                           \
		*(listelm)->field.le_prev = (elm);                             \
		(listelm)->field.le_prev = &LIST_NEXT((elm), field);           \
	} while (0)

/*
 * LIST_INSERT_HEAD(head, elm, field)：把 elm 插到链表头，O(1)。
 */
#define LIST_INSERT_HEAD(head, elm, field)                                     \
	do {                                                                   \
		QMD_LIST_CHECK_HEAD((head), field);                            \
		if ((LIST_NEXT((elm), field) = LIST_FIRST((head))) != NULL)    \
			LIST_FIRST((head))->field.le_prev =                    \
				&LIST_NEXT((elm), field);                      \
		LIST_FIRST((head)) = (elm);                                    \
		(elm)->field.le_prev = &LIST_FIRST((head));                    \
	} while (0)

/*
 * LIST_NEXT(elm, field)：返回 elm 的后继元素指针，末尾为 NULL。
 */
#define	LIST_NEXT(elm, field)	((elm)->field.le_next)

/*
 * LIST_REMOVE(elm, field)：从链表中删除元素 elm，O(1)（无需找前驱）。注意它
 * 不带 head 参数，仅靠元素自身的 le_prev 完成，故 elm 必须确实在链表中。
 */
#define LIST_REMOVE(elm, field)                                                \
	do {                                                                   \
		QMD_SAVELINK(oldnext, (elm)->field.le_next);                   \
		QMD_SAVELINK(oldprev, (elm)->field.le_prev);                   \
		QMD_LIST_CHECK_NEXT(elm, field);                               \
		QMD_LIST_CHECK_PREV(elm, field);                               \
		if (LIST_NEXT((elm), field) != NULL)                           \
			LIST_NEXT((elm), field)->field.le_prev =               \
				(elm)->field.le_prev;                          \
		*(elm)->field.le_prev = LIST_NEXT((elm), field);               \
		TRASHIT(*oldnext);                                             \
		TRASHIT(*oldprev);                                             \
	} while (0)

/*
 * LIST_SWAP(head1, head2, type, field)：交换两个同类双向链表的全部内容，并
 * 修正各自新首元素的 le_prev 使其指回各自的头。
 */
#define LIST_SWAP(head1, head2, type, field)                                   \
	do {                                                                   \
		struct type *swap_tmp = LIST_FIRST((head1));                   \
		LIST_FIRST((head1)) = LIST_FIRST((head2));                     \
		LIST_FIRST((head2)) = swap_tmp;                                \
		if ((swap_tmp = LIST_FIRST((head1))) != NULL)                  \
			swap_tmp->field.le_prev = &LIST_FIRST((head1));        \
		if ((swap_tmp = LIST_FIRST((head2))) != NULL)                  \
			swap_tmp->field.le_prev = &LIST_FIRST((head2));        \
	} while (0)

/*
 * Tail queue declarations.
 */
/*
 * 中文小结：TAILQ 是双链尾队列，功能最全：头尾 O(1) 操作、可双向遍历、可
 * O(1) 删除任意元素。头里既存首指针 tqh_first 也存尾指针 tqh_last。
 */
/*
 * TAILQ_HEAD(name, type)：定义双链尾队列头，含首指针 tqh_first 与指向末尾
 * next 字段的地址 tqh_last。可双端 O(1) 操作、可双向遍历，功能最全。
 */
#define TAILQ_HEAD(name, type)                                                 \
	struct name {                                                          \
		struct type *tqh_first; /* first element */                    \
		struct type **tqh_last; /* addr of last next element */        \
		TRACEBUF                                                       \
	}

/*
 * TAILQ_HEAD_INITIALIZER(head)：双链尾队列头的静态初始化器。仅用于全局 /
 * 静态变量，运行时用 TAILQ_INIT。
 */
#define TAILQ_HEAD_INITIALIZER(head)                                           \
	{                                                                      \
		NULL, &(head).tqh_first                                        \
	}

/*
 * TAILQ_ENTRY(type)：嵌入元素的链接节点，含 next 与 prev 两个指针，因此
 * TAILQ 元素可双向遍历、也能 O(1) 删除任意元素。
 */
#define TAILQ_ENTRY(type)                                                      \
	struct {                                                               \
		struct type *tqe_next;  /* next element */                     \
		struct type **tqe_prev; /* address of previous next element */ \
		TRACEBUF                                                       \
	}

/*
 * Tail queue functions.
 */
/*
 * 中文小结：本节是 TAILQ 的全部操作宏，含它独有的 _FOREACH_REVERSE(_SAFE)
 * / _LAST / _PREV。理解了 TAILQ，前三类结构基本也就顺带理解了。
 */
#if (defined(_KERNEL) && defined(INVARIANTS))
/*
 * QMD_TAILQ_CHECK_HEAD(head, field)：仅内核 INVARIANTS 调试下生效，校验尾队
 * 列头与首元素的双向链接自洽；非调试构建展开为空。
 */
#define QMD_TAILQ_CHECK_HEAD(head, field)                                      \
	do {                                                                   \
		if (!TAILQ_EMPTY(head)                                         \
		    && TAILQ_FIRST((head))->field.tqe_prev                     \
			       != &TAILQ_FIRST((head)))                        \
			panic("Bad tailq head %p first->prev != head",         \
			      (head));                                         \
	} while (0)

/*
 * QMD_TAILQ_CHECK_TAIL(head, field)：调试下校验尾指针 tqh_last 所指的 next
 * 字段应为 NULL（确认它确实指向末尾）；非调试构建展开为空。
 */
#define QMD_TAILQ_CHECK_TAIL(head, field)                                      \
	do {                                                                   \
		if (*(head)->tqh_last != NULL)                                 \
			panic("Bad tailq NEXT(%p->tqh_last) != NULL", (head)); \
	} while (0)

/*
 * QMD_TAILQ_CHECK_NEXT(elm, field)：调试下校验 elm 与后继的双向链接自洽；
 * 非调试构建展开为空。
 */
#define QMD_TAILQ_CHECK_NEXT(elm, field)                                       \
	do {                                                                   \
		if (TAILQ_NEXT((elm), field) != NULL                           \
		    && TAILQ_NEXT((elm), field)->field.tqe_prev                \
			       != &((elm)->field.tqe_next))                    \
			panic("Bad link elm %p next->prev != elm", (elm));     \
	} while (0)

/*
 * QMD_TAILQ_CHECK_PREV(elm, field)：调试下校验 elm 与前驱的双向链接自洽；
 * 非调试构建展开为空。
 */
#define QMD_TAILQ_CHECK_PREV(elm, field)                                       \
	do {                                                                   \
		if (*(elm)->field.tqe_prev != (elm))                           \
			panic("Bad link elm %p prev->next != elm", (elm));     \
	} while (0)
/*
 * 非调试构建分支：TAILQ 的四个 QMD_TAILQ_CHECK_* 一致性检查宏退化为空，
 * 运行时零开销。
 */
#else
#define	QMD_TAILQ_CHECK_HEAD(head, field)
#define	QMD_TAILQ_CHECK_TAIL(head, headname)
#define	QMD_TAILQ_CHECK_NEXT(elm, field)
#define	QMD_TAILQ_CHECK_PREV(elm, field)
#endif /* (_KERNEL && INVARIANTS) */

/*
 * TAILQ_CONCAT(head1, head2, field)：把 head2 接到 head1 尾部并清空 head2。
 * 要求为同一类型；会修正接过来的首元素的 prev 指针。注意比 STAILQ_CONCAT
 * 多一个 field 参数。
 */
#define TAILQ_CONCAT(head1, head2, field)                                      \
	do {                                                                   \
		if (!TAILQ_EMPTY(head2)) {                                     \
			*(head1)->tqh_last = (head2)->tqh_first;               \
			(head2)->tqh_first->field.tqe_prev =                   \
				(head1)->tqh_last;                             \
			(head1)->tqh_last = (head2)->tqh_last;                 \
			TAILQ_INIT((head2));                                   \
			QMD_TRACE_HEAD(head1);                                 \
			QMD_TRACE_HEAD(head2);                                 \
		}                                                              \
	} while (0)

/*
 * TAILQ_EMPTY(head)：判断尾队列是否为空，非 0 表示空。
 */
#define	TAILQ_EMPTY(head)	((head)->tqh_first == NULL)

/*
 * TAILQ_FIRST(head)：返回队首元素指针，空队列时为 NULL。
 */
#define	TAILQ_FIRST(head)	((head)->tqh_first)

/*
 * TAILQ_FOREACH(var, head, field)：正序逐个遍历。删除当前元素不安全，需删
 * 请用 TAILQ_FOREACH_SAFE。
 */
#define TAILQ_FOREACH(var, head, field)                                        \
	for ((var) = TAILQ_FIRST((head)); (var);                               \
	     (var) = TAILQ_NEXT((var), field))

/*
 * TAILQ_FOREACH_SAFE(var, head, field, tvar)：可删除元素的安全正序遍历。
 * tvar 为调用方提供的临时变量，先记下一元素再处理 var。
 */
#define TAILQ_FOREACH_SAFE(var, head, field, tvar)                             \
	for ((var) = TAILQ_FIRST((head));                                      \
	     (var) && ((tvar) = TAILQ_NEXT((var), field), 1); (var) = (tvar))

/*
 * TAILQ_FOREACH_REVERSE(var, head, headname, field)：从队尾反向遍历到队首。
 * 需额外传入头结构体名 headname（用于还原尾 / 前驱指针类型）。这是 TAILQ
 * 独有的能力，前三类结构都没有反向遍历。
 */
#define TAILQ_FOREACH_REVERSE(var, head, headname, field)                      \
	for ((var) = TAILQ_LAST((head), headname); (var);                      \
	     (var) = TAILQ_PREV((var), headname, field))

/*
 * TAILQ_FOREACH_REVERSE_SAFE(var, head, headname, field, tvar)：可删除元素的
 * 安全反向遍历。headname 为头结构体名，tvar 为调用方提供的临时变量。
 */
#define TAILQ_FOREACH_REVERSE_SAFE(var, head, headname, field, tvar)           \
	for ((var) = TAILQ_LAST((head), headname);                             \
	     (var) && ((tvar) = TAILQ_PREV((var), headname, field), 1);        \
	     (var) = (tvar))

/*
 * TAILQ_INIT(head)：把尾队列初始化为空（首为 NULL，尾指回首字段）。
 */
#define TAILQ_INIT(head)                                                       \
	do {                                                                   \
		TAILQ_FIRST((head)) = NULL;                                    \
		(head)->tqh_last = &TAILQ_FIRST((head));                       \
		QMD_TRACE_HEAD(head);                                          \
	} while (0)

/*
 * TAILQ_INSERT_AFTER(head, listelm, elm, field)：把 elm 插到 listelm 之后。
 * listelm 必须已在队列中；若插到末尾会更新头的尾指针。
 */
#define TAILQ_INSERT_AFTER(head, listelm, elm, field)                          \
	do {                                                                   \
		QMD_TAILQ_CHECK_NEXT(listelm, field);                          \
		if ((TAILQ_NEXT((elm), field) = TAILQ_NEXT((listelm), field))  \
		    != NULL)                                                   \
			TAILQ_NEXT((elm), field)->field.tqe_prev =             \
				&TAILQ_NEXT((elm), field);                     \
		else {                                                         \
			(head)->tqh_last = &TAILQ_NEXT((elm), field);          \
			QMD_TRACE_HEAD(head);                                  \
		}                                                              \
		TAILQ_NEXT((listelm), field) = (elm);                          \
		(elm)->field.tqe_prev = &TAILQ_NEXT((listelm), field);         \
		QMD_TRACE_ELEM(&(elm)->field);                                 \
		QMD_TRACE_ELEM(&listelm->field);                               \
	} while (0)

/*
 * TAILQ_INSERT_BEFORE(listelm, elm, field)：把 elm 插到 listelm 之前，借助
 * listelm 的 prev 指针 O(1) 完成。无 head 参数。
 */
#define TAILQ_INSERT_BEFORE(listelm, elm, field)                               \
	do {                                                                   \
		QMD_TAILQ_CHECK_PREV(listelm, field);                          \
		(elm)->field.tqe_prev = (listelm)->field.tqe_prev;             \
		TAILQ_NEXT((elm), field) = (listelm);                          \
		*(listelm)->field.tqe_prev = (elm);                            \
		(listelm)->field.tqe_prev = &TAILQ_NEXT((elm), field);         \
		QMD_TRACE_ELEM(&(elm)->field);                                 \
		QMD_TRACE_ELEM(&listelm->field);                               \
	} while (0)

/*
 * TAILQ_INSERT_HEAD(head, elm, field)：把 elm 插到队首；若原队列为空会同步
 * 更新尾指针。
 */
#define TAILQ_INSERT_HEAD(head, elm, field)                                    \
	do {                                                                   \
		QMD_TAILQ_CHECK_HEAD(head, field);                             \
		if ((TAILQ_NEXT((elm), field) = TAILQ_FIRST((head))) != NULL)  \
			TAILQ_FIRST((head))->field.tqe_prev =                  \
				&TAILQ_NEXT((elm), field);                     \
		else                                                           \
			(head)->tqh_last = &TAILQ_NEXT((elm), field);          \
		TAILQ_FIRST((head)) = (elm);                                   \
		(elm)->field.tqe_prev = &TAILQ_FIRST((head));                  \
		QMD_TRACE_HEAD(head);                                          \
		QMD_TRACE_ELEM(&(elm)->field);                                 \
	} while (0)

/*
 * TAILQ_INSERT_TAIL(head, elm, field)：把 elm 插到队尾，O(1)。
 */
#define TAILQ_INSERT_TAIL(head, elm, field)                                    \
	do {                                                                   \
		QMD_TAILQ_CHECK_TAIL(head, field);                             \
		TAILQ_NEXT((elm), field) = NULL;                               \
		(elm)->field.tqe_prev = (head)->tqh_last;                      \
		*(head)->tqh_last = (elm);                                     \
		(head)->tqh_last = &TAILQ_NEXT((elm), field);                  \
		QMD_TRACE_HEAD(head);                                          \
		QMD_TRACE_ELEM(&(elm)->field);                                 \
	} while (0)

/*
 * TAILQ_LAST(head, headname)：返回队尾元素指针，空队列时为 NULL。实现是把
 * tqh_last 强转为头类型指针再取其 tqh_last；headname 必须是头结构体名且与
 * head 真实类型一致，否则会取到错误地址。
 */
#define TAILQ_LAST(head, headname)                                             \
	(*(((struct headname *)((head)->tqh_last))->tqh_last))

/*
 * TAILQ_NEXT(elm, field)：返回 elm 的后继元素指针，末尾为 NULL。
 */
#define	TAILQ_NEXT(elm, field) ((elm)->field.tqe_next)

/*
 * TAILQ_PREV(elm, headname, field)：返回 elm 的前驱元素指针。实现是把 elm 的
 * prev 指针强转为头类型再取 tqh_last；headname 是头结构体名。这是 TAILQ 能
 * 反向遍历的关键。
 */
#define TAILQ_PREV(elm, headname, field)                                       \
	(*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))

/*
 * TAILQ_REMOVE(head, elm, field)：从队列中删除元素 elm，O(1)。若 elm 是尾元素
 * 会同步更新头的尾指针；正因如此它比 LIST_REMOVE 多一个 head 参数。
 */
#define TAILQ_REMOVE(head, elm, field)                                         \
	do {                                                                   \
		QMD_SAVELINK(oldnext, (elm)->field.tqe_next);                  \
		QMD_SAVELINK(oldprev, (elm)->field.tqe_prev);                  \
		QMD_TAILQ_CHECK_NEXT(elm, field);                              \
		QMD_TAILQ_CHECK_PREV(elm, field);                              \
		if ((TAILQ_NEXT((elm), field)) != NULL)                        \
			TAILQ_NEXT((elm), field)->field.tqe_prev =             \
				(elm)->field.tqe_prev;                         \
		else {                                                         \
			(head)->tqh_last = (elm)->field.tqe_prev;              \
			QMD_TRACE_HEAD(head);                                  \
		}                                                              \
		*(elm)->field.tqe_prev = TAILQ_NEXT((elm), field);             \
		TRASHIT(*oldnext);                                             \
		TRASHIT(*oldprev);                                             \
		QMD_TRACE_ELEM(&(elm)->field);                                 \
	} while (0)

/*
 * TAILQ_SWAP(head1, head2, type, field)：交换两个同类尾队列的全部内容，并修
 * 正各自新首元素的 prev 或头的尾指针，使其自洽。
 */
#define TAILQ_SWAP(head1, head2, type, field)                                  \
	do {                                                                   \
		struct type *swap_first = (head1)->tqh_first;                  \
		struct type **swap_last = (head1)->tqh_last;                   \
		(head1)->tqh_first = (head2)->tqh_first;                       \
		(head1)->tqh_last = (head2)->tqh_last;                         \
		(head2)->tqh_first = swap_first;                               \
		(head2)->tqh_last = swap_last;                                 \
		if ((swap_first = (head1)->tqh_first) != NULL)                 \
			swap_first->field.tqe_prev = &(head1)->tqh_first;      \
		else                                                           \
			(head1)->tqh_last = &(head1)->tqh_first;               \
		if ((swap_first = (head2)->tqh_first) != NULL)                 \
			swap_first->field.tqe_prev = &(head2)->tqh_first;      \
		else                                                           \
			(head2)->tqh_last = &(head2)->tqh_first;               \
	} while (0)

#ifdef __cplusplus
}
#endif

#endif /* !_SYS_QUEUE_H_ */
