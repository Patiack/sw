// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lists and queues implementations
 */

#ifndef _FRR_QUEUE_H
#define _FRR_QUEUE_H

/*
 * ============================================================================
 * 文件导读：FRR 队列适配层（queue.h）
 * ============================================================================
 *
 * 1. 这个文件干什么
 *    它是 FRR 对「四类 BSD 线性表宏」的统一入口。上层代码只 #include
 *    <queue.h>，无需关心底层跑在哪个操作系统上。
 *
 * 2. 平台分派逻辑
 *    - OpenBSD：包含 openbsd-queue.h，并把 STAILQ 系列宏别名映射到
 *      SIMPLEQ 系列（OpenBSD 把单链尾队列叫 SIMPLEQ，语义等价）。
 *    - 其他平台（Linux / FreeBSD / macOS 等）：包含 freebsd-queue.h，
 *      即 FreeBSD 原版 queue.h，直接提供 SLIST / LIST / STAILQ / TAILQ。
 *
 * 3. 核心数据结构
 *    全部来自被包含的头文件，本文件不定义新结构：
 *      SLIST  单链表      只向前遍历，删除任意元素 O(n)，适合 LIFO
 *      LIST   双向链表    任意元素 O(1) 删除，只向前遍历
 *      STAILQ 单链尾队列  头尾都能 O(1) 入队，向前遍历，适合 FIFO
 *      TAILQ  双链尾队列  双端 O(1) 操作、可双向遍历，功能最全
 *
 * 4. 本文件额外的两个东西
 *    - STAILQ 到 SIMPLEQ 的宏映射（仅 OpenBSD 分支）。
 *    - TAILQ_POP_FIRST：取出并删除队首元素的 GNU 语句表达式辅助宏。
 *
 * 5. 与其他模块的关系
 *    lib/ 内大量模块（hash、workqueue、各种表与资源池等）都用这些宏组织
 *    对象链表。freebsd-queue.h 被整体搬进 FRR，是为了获得类型安全、无需
 *    额外内存分配的内联链接节点，以及 BSD 长期验证过的成熟实现。
 *
 * 6. 建议阅读顺序
 *    先读 freebsd-queue.h 里 SLIST -> LIST -> STAILQ -> TAILQ 四节，
 *    理解每类宏的语义后，再回来看本文件的平台适配与 TAILQ_POP_FIRST。
 * ============================================================================
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 平台适配入口：仅当在 OpenBSD 上、且外部尚未定义 STAILQ_HEAD 时才走
 * OpenBSD 分支。多加 !defined(STAILQ_HEAD) 是为了兼容已有 STAILQ 定义的
 * 环境，避免宏重复定义与语义冲突。非 OpenBSD 平台直接包含 FreeBSD 原版。
 */
#if defined(__OpenBSD__) && !defined(STAILQ_HEAD)
#include "openbsd-queue.h"

/* Try to map FreeBSD implementation to OpenBSD one. */
/*
 * STAILQ_HEAD：定义单链尾队列的「头」结构体（首指针 + 指向尾 next 的地址）。
 * OpenBSD 上等价于 SIMPLEQ_HEAD；name 为头类型名，type 为元素类型名。
 */
#define STAILQ_HEAD(name, type)				SIMPLEQ_HEAD(name, type)
/*
 * STAILQ_HEAD_INITIALIZER：头结构的静态初始化器，用于全局 / 静态变量。
 * OpenBSD 上等价于 SIMPLEQ_HEAD_INITIALIZER，把首指针与尾指针置空。
 */
#define STAILQ_HEAD_INITIALIZER(head)			SIMPLEQ_HEAD_INITIALIZER(head)
/*
 * STAILQ_ENTRY：嵌入到元素类型中的链接节点（只含一个 next 指针）。
 * 同一元素若要同时挂在多种结构上，需为每种结构各定义一个 ENTRY 成员。
 * OpenBSD 上等价于 SIMPLEQ_ENTRY。
 */
#define STAILQ_ENTRY(entry)				SIMPLEQ_ENTRY(entry)

/*
 * STAILQ_CONCAT(head1, head2)：把 head2 整体接到 head1 的尾部。
 * 要求两者是同一 type 的尾队列；操作后 head2 被置为空队列。
 * OpenBSD 上等价于 SIMPLEQ_CONCAT。
 */
#define STAILQ_CONCAT(head1, head2)			SIMPLEQ_CONCAT(head1, head2)
/*
 * STAILQ_EMPTY(head)：判断队列是否为空，非 0 表示空。
 * OpenBSD 上等价于 SIMPLEQ_EMPTY。
 */
#define STAILQ_EMPTY(head)				SIMPLEQ_EMPTY(head)
/*
 * STAILQ_FIRST(head)：返回队首元素指针，空队列时为 NULL。
 * OpenBSD 上等价于 SIMPLEQ_FIRST。
 */
#define STAILQ_FIRST(head)				SIMPLEQ_FIRST(head)
/*
 * STAILQ_FOREACH(var, head, field)：正向遍历队列，var 依次指向每个元素。
 * field 是元素内的 STAILQ_ENTRY 成员名。遍历过程中不可删除当前元素，
 * 需要在遍历时删除请改用 _FOREACH_SAFE。OpenBSD 对应 SIMPLEQ_FOREACH。
 */
#define STAILQ_FOREACH(var, head, field)		SIMPLEQ_FOREACH(var, head, field)
/*
 * STAILQ_FOREACH_SAFE(var, head, field, tvar)：可安全删除的安全遍历。
 * tvar 是调用方提供的临时变量，先记住下一元素再处理 var，因此循环体内
 * 可以 STAILQ_REMOVE 掉当前 var。OpenBSD 对应 SIMPLEQ_FOREACH_SAFE。
 */
#define STAILQ_FOREACH_SAFE(var, head, field, tvar)	SIMPLEQ_FOREACH_SAFE(var, head, field, tvar)
/*
 * STAILQ_INIT(head)：把队列初始化为空（首指针为 NULL，尾指针指回首指针）。
 * OpenBSD 上等价于 SIMPLEQ_INIT。
 */
#define STAILQ_INIT(head)				SIMPLEQ_INIT(head)
/*
 * STAILQ_INSERT_AFTER(head, tqelm, elm, field)：把 elm 插到 tqelm 之后。
 * tqelm 必须已在该队列中；若插在末尾会同步更新尾指针。
 * OpenBSD 上等价于 SIMPLEQ_INSERT_AFTER。
 */
#define STAILQ_INSERT_AFTER(head, tqelm, elm, field)	SIMPLEQ_INSERT_AFTER(head, tqelm, elm, field)
/*
 * STAILQ_INSERT_HEAD(head, elm, field)：把 elm 插到队首。
 * 若原来是空队列，会同步更新尾指针。OpenBSD 对应 SIMPLEQ_INSERT_HEAD。
 */
#define STAILQ_INSERT_HEAD(head, elm, field)		SIMPLEQ_INSERT_HEAD(head, elm, field)
/*
 * STAILQ_INSERT_TAIL(head, elm, field)：把 elm 插到队尾，O(1)。
 * 这是尾队列相对普通单链表的主要优势，适合实现 FIFO。
 * OpenBSD 上等价于 SIMPLEQ_INSERT_TAIL。
 */
#define STAILQ_INSERT_TAIL(head, elm, field)		SIMPLEQ_INSERT_TAIL(head, elm, field)
/*
 * STAILQ_LAST(head, type, field)：返回队尾元素指针，空队列时返回 NULL。
 * 实现上由尾指针 sqh_last 减去 field 相对 type 的偏移还原出元素地址，
 * 因此必须传入正确的 type 与 field，否则地址会算错。
 */
#define STAILQ_LAST(head, type, field)                                         \
	(SIMPLEQ_EMPTY((head))                                                 \
		 ? NULL                                                        \
		 : ((struct type *)(void *)((char *)((head)->sqh_last)         \
					    - offsetof(struct type, field))))
/*
 * STAILQ_NEXT(elm, field)：返回 elm 的后继元素指针，末尾为 NULL。
 * OpenBSD 上等价于 SIMPLEQ_NEXT。
 */
#define STAILQ_NEXT(elm, field)				SIMPLEQ_NEXT(elm, field)
/*
 * STAILQ_REMOVE(head, elm, type, field)：从队列中删除元素 elm。
 * elm 是队首时走 O(1) 的 REMOVE_HEAD；否则需 O(n) 线性查找前驱再删。
 * OpenBSD 上用 SIMPLEQ 原语重写以保持等价语义。
 */
#define STAILQ_REMOVE(head, elm, type, field)                                  \
	do {                                                                   \
		if (SIMPLEQ_FIRST((head)) == (elm)) {                          \
			SIMPLEQ_REMOVE_HEAD((head), field);                    \
		} else {                                                       \
			struct type *curelm = SIMPLEQ_FIRST((head));           \
			while (SIMPLEQ_NEXT(curelm, field) != (elm))           \
				curelm = SIMPLEQ_NEXT(curelm, field);          \
			SIMPLEQ_REMOVE_AFTER(head, curelm, field);             \
		}                                                              \
	} while (0)
/*
 * STAILQ_REMOVE_AFTER(head, elm, field)：删除 elm 的后继元素。
 * 若删掉的是尾元素会同步更新尾指针。OpenBSD 对应 SIMPLEQ_REMOVE_AFTER。
 */
#define STAILQ_REMOVE_AFTER(head, elm, field)		SIMPLEQ_REMOVE_AFTER(head, elm, field)
/*
 * STAILQ_REMOVE_HEAD(head, field)：删除队首元素，O(1)。
 * 若删至空队列会同步更新尾指针。OpenBSD 对应 SIMPLEQ_REMOVE_HEAD。
 */
#define STAILQ_REMOVE_HEAD(head, field)			SIMPLEQ_REMOVE_HEAD(head, field)
/*
 * STAILQ_SWAP(head1, head2, type)：交换两个同类型尾队列的全部内容。
 * 交换后若某个头变为空队列，会把其尾指针重新指回自身的首指针以便复用。
 */
#define STAILQ_SWAP(head1, head2, type)                                        \
	do {                                                                   \
		struct type *swap_first = STAILQ_FIRST(head1);                 \
		struct type **swap_last = (head1)->sqh_last;                   \
		STAILQ_FIRST(head1) = STAILQ_FIRST(head2);                     \
		(head1)->sqh_last = (head2)->sqh_last;                         \
		STAILQ_FIRST(head2) = swap_first;                              \
		(head2)->sqh_last = swap_last;                                 \
		if (STAILQ_EMPTY(head1))                                       \
			(head1)->sqh_last = &STAILQ_FIRST(head1);              \
		if (STAILQ_EMPTY(head2))                                       \
			(head2)->sqh_last = &STAILQ_FIRST(head2);              \
	} while (0)
#else
/*
 * 非 OpenBSD 平台的落点：包含 FreeBSD 原版 queue.h，直接获得
 * SLIST / LIST / STAILQ / TAILQ 四套宏，无需任何别名映射。
 */
#include "freebsd-queue.h"
#endif /* defined(__OpenBSD__) && !defined(STAILQ_HEAD) */

/*
 * TAILQ_POP_FIRST(head, field)：取出并删除队首元素，返回该元素指针。
 * 用 GNU 语句表达式「({ ... })」实现，整体值是块内最后一个表达式 _elm；
 * 队列为空时返回 NULL。它一次性修正新队首的 tqe_prev 或头的 tqh_last，
 * 因此比「先 TAILQ_FIRST 再 TAILQ_REMOVE」更简洁、且不会漏更新尾指针。
 * 外层 #ifndef 保护：若底层已提供同名宏则不重复定义。
 * 注意：这是 GNU 扩展，非标准 C，换用非 GCC / Clang 编译器会编译失败。
 */
#ifndef TAILQ_POP_FIRST
#define TAILQ_POP_FIRST(head, field)                                           \
	({  typeof((head)->tqh_first) _elm = TAILQ_FIRST(head);                \
	    if (_elm) {                                                        \
		if ((TAILQ_NEXT((_elm), field)) != NULL)                       \
			TAILQ_NEXT((_elm), field)->field.tqe_prev =            \
				&TAILQ_FIRST(head);                            \
		else                                                           \
			(head)->tqh_last = &TAILQ_FIRST(head);                 \
		TAILQ_FIRST(head) = TAILQ_NEXT((_elm), field);                 \
	}; _elm; })
#endif

#ifdef __cplusplus
}
#endif

#endif /* _FRR_QUEUE_H */
