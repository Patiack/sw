// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2016-2018  David Lamparter, for NetDEF, Inc.
 */

/*
 * 【文件导读】atomlist.c —— 无锁原子单链表与原子有序表的 CAS 实现
 *
 * 一、这个文件是干什么的
 *   实现 atomlist.h 声明的原子容器操作。核心思想：用 CPU 的原子比较交换
 *   （CAS）直接改指针，配合“低位 LOCK 标记”表示“该结点正在被删除”，从而让
 *   并发插入、删除、弹出都不需要互斥锁。属于 lock-free（但非 wait-free）。
 *
 * 二、核心数据结构
 *   复用 atomlist.h 的 struct atomlist_head（first / last / count）与
 *   struct atomlist_item（单个原子 next）。atomsort 部分用 atomsort_head /
 *   atomsort_item，语义相同但按比较函数保持有序。
 *
 * 三、与其它模块的关系
 *   仅依赖 atomlist.h 与 frratomic.h。被上层需要“无锁表”的模块调用（如
 *   路由/邻居发布）。内存序（memory_order）的选择是本文件理解的关键。
 *
 * 四、阅读顺序
 *   atomlist_add_head（最简单的头插）-> atomlist_add_tail（尾插，较复杂）
 *   -> atomlist_del_core（删除核心，理解“删链”处理）-> atomlist_del_hint
 *   -> atomlist_pop。atomsort_* 与 atomlist_* 结构几乎一一对应，可对照阅读。
 *
 * 易踩的坑：删除要求调用方独占持有该元素的引用（否则会触发断言/竞态）；
 *   早于删除开始读取的线程仍可能看到旧结点；元素结构体不可用 packed 属性。
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>

#include "atomlist.h"

/*
 * atomlist_add_head —— 原子地把 item 头插到 h 前面。
 * 步骤：count 自增 -> 置 item->next 为 NULL -> 用 CAS 把 h->first 换成 item。
 * 内存序：成功的 CAS 用 release，保证 item 及其 next 的写入在发布前完成。
 * 返回即代表插入完成（其它线程立即可见）。
 */
void atomlist_add_head(struct atomlist_head *h, struct atomlist_item *item)
{
	atomptr_t prevval;
	atomptr_t i = atomptr_i(item);

	atomic_fetch_add_explicit(&h->count, 1, memory_order_relaxed);

	/* updating ->last is possible here, but makes the code considerably
	 * more complicated... let's not.
	 */
	prevval = ATOMPTR_NULL;
	item->next = ATOMPTR_NULL;

	/* head-insert atomically
	 * release barrier: item + item->next writes must be completed
	 */
	while (!atomic_compare_exchange_weak_explicit(&h->first, &prevval, i,
				memory_order_release, memory_order_relaxed))
		atomic_store_explicit(&item->next, prevval,
				memory_order_relaxed);
}

/*
 * atomlist_add_tail —— 原子地把 item 尾插到 h 末尾（可当队列入队）。
 * 做法：先把新结点用 exchange 挂到 h->last（占住尾位），再向前扫描找到真正
 *   的尾结点，用 CAS 把它的 next 接到 item。
 * 副作用：count 自增；竞争时可能重试。返回不意味对其它线程已可见——需等到
 *   此前已开始的其它 add_tail 都完成。
 * 坑：若尾结点正被删除（带 LOCK），需从头重试；hint 会随失败被更新以减少重扫。
 */
void atomlist_add_tail(struct atomlist_head *h, struct atomlist_item *item)
{
	atomptr_t prevval = ATOMPTR_NULL;
	atomptr_t i = atomptr_i(item);
	atomptr_t hint;
	struct atomlist_item *prevptr;
	_Atomic atomptr_t *prev;

	item->next = ATOMPTR_NULL;

	atomic_fetch_add_explicit(&h->count, 1, memory_order_relaxed);

	/* place new item into ->last
	 * release: item writes completed;  acquire: DD barrier on hint
	 */
	hint = atomic_exchange_explicit(&h->last, i, memory_order_acq_rel);

	while (1) {
		if (atomptr_p(hint) == NULL)
			prev = &h->first;
		else
			prev = &atomlist_itemp(hint)->next;

		do {
			prevval = atomic_load_explicit(prev,
					memory_order_consume);
			prevptr = atomlist_itemp(prevval);
			if (prevptr == NULL)
				break;

			prev = &prevptr->next;
		} while (prevptr);

		/* last item is being deleted - start over */
		if (atomptr_l(prevval)) {
			hint = ATOMPTR_NULL;
			continue;
		}

		/* no barrier - item->next is NULL and was so in xchg above */
		if (!atomic_compare_exchange_strong_explicit(prev, &prevval, i,
					memory_order_consume,
					memory_order_consume)) {
			hint = prevval;
			continue;
		}
		break;
	}
}

/*
 * atomlist_del_core —— 单链表删除的内部核心（del_hint 与 pop 共用）。
 * 参数：h 表头；item 已带 LOCK 标记、待摘除的结点；hint 前驱指针缓存（可 NULL）；
 *       next 为 item 摘除后其前驱应指向的后继值（调用方已存好）。
 * 做法：先把 h->last 的悬挂引用清掉（若尾结点就是 item），count 自减；再从
 *   hint 或表头扫到 item 的前驱，用 CAS 把前驱 next 改成 next。为保持
 *   lock-free，会顺带“代其它线程完成删除”。
 * 返回：无（void）；若发现已有其它线程删掉了 item 则直接返回。
 */
static void atomlist_del_core(struct atomlist_head *h,
			      struct atomlist_item *item,
			      _Atomic atomptr_t *hint,
			      atomptr_t next)
{
	_Atomic atomptr_t *prev = hint ? hint : &h->first, *upd;
	atomptr_t prevval, updval;
	struct atomlist_item *prevptr;

	/* drop us off "last" if needed.  no r/w to barrier. */
	prevval = atomptr_i(item);
	atomic_compare_exchange_strong_explicit(&h->last, &prevval,
			ATOMPTR_NULL,
			memory_order_relaxed, memory_order_relaxed);

	atomic_fetch_sub_explicit(&h->count, 1, memory_order_relaxed);

	/* the following code should be identical (except sort<>list) to
	 * atomsort_del_hint()
	 */
	while (1) {
		upd = NULL;
		updval = ATOMPTR_LOCK;

		do {
			prevval = atomic_load_explicit(prev,
					memory_order_consume);

			/* track the beginning of a chain of deleted items
			 * this is necessary to make this lock-free; we can
			 * complete deletions started by other threads.
			 */
			if (!atomptr_l(prevval)) {
				updval = prevval;
				upd = prev;
			}

			prevptr = atomlist_itemp(prevval);
			if (prevptr == item)
				break;

			prev = &prevptr->next;
		} while (prevptr);

		if (prevptr != item)
			/* another thread completed our deletion */
			return;

		if (!upd || atomptr_l(updval)) {
			/* failed to find non-deleted predecessor...
			 * have to try again
			 */
			prev = &h->first;
			continue;
		}

		if (!atomic_compare_exchange_strong_explicit(upd, &updval,
					next, memory_order_consume,
					memory_order_consume)) {
			/* prev doesn't point to item anymore, something
			 * was inserted.  continue at same position forward.
			 */
			continue;
		}
		break;
	}
}

/*
 * atomlist_del_hint —— 删除链表中的 item，hint 为前驱指针缓存（可 NULL）。
 * 步骤：先对 item->next 做 fetch_or(LOCK) 打删除标记（同时取得其后继），
 *   断言此前未被打标记（否则是同一元素的删除竞态）；再调 atomlist_del_core。
 * 前置条件：调用方独占持有该元素引用。
 */
void atomlist_del_hint(struct atomlist_head *h, struct atomlist_item *item,
		_Atomic atomptr_t *hint)
{
	atomptr_t next;

	/* mark ourselves in-delete - full barrier */
	next = atomic_fetch_or_explicit(&item->next, ATOMPTR_LOCK,
				memory_order_acquire);
	assert(!atomptr_l(next));	/* delete race on same item */

	atomlist_del_core(h, item, hint, next);
}

/*
 * atomlist_pop —— 从链表头弹出一个结点并返回；空表返回 NULL。
 * 做法：读 h->first，对候选结点的 next 打 LOCK 标记；若发现该结点已被别的
 *   pop/del 抢先标记，就顺延到下一个节点继续尝试；标记成功即调
 *   atomlist_del_core 完成摘除并返回。
 * 注意：与所有删除一致，更早开始读取的线程可能仍持有该结点指针。
 */
struct atomlist_item *atomlist_pop(struct atomlist_head *h)
{
	struct atomlist_item *item;
	atomptr_t next;

	/* grab head of the list - and remember it in replval for the
	 * actual delete below.  No matter what, the head of the list is
	 * where we start deleting because either it's our item, or it's
	 * some delete-marked items and then our item.
	 */
	next = atomic_load_explicit(&h->first, memory_order_consume);

	do {
		item = atomlist_itemp(next);
		if (!item)
			return NULL;

		/* try to mark deletion */
		next = atomic_fetch_or_explicit(&item->next, ATOMPTR_LOCK,
					memory_order_acquire);

	} while (atomptr_l(next));
	/* if loop is taken: delete race on same item (another pop or del)
	 * => proceed to next item
	 * if loop exited here: we have our item selected and marked
	 */
	atomlist_del_core(h, item, &h->first, next);
	return item;
}

/*
 * atomsort_add —— 按 cmpfn 把 item 插入有序表 h 的正确排序位置（无锁）。
 * 做法：从表头扫描找到第一个“比 item 大”的位置 prev，把 item->next 指向
 *   prevval 后再 CAS 挂入；扫描中若遇到相等（cmpval==0）说明是唯一键表且
 *   已存在，直接返回该已存在结点。
 * 返回：成功返回 NULL（注意：与头文件注释相反，本实现成功返回 NULL，
 *   失败/重复时返回已存在结点，请以实际代码为准）；count 自增。
 */
struct atomsort_item *atomsort_add(struct atomsort_head *h,
		struct atomsort_item *item, int (*cmpfn)(
			const struct atomsort_item *,
			const struct atomsort_item *))
{
	_Atomic atomptr_t *prev;
	atomptr_t prevval;
	atomptr_t i = atomptr_i(item);
	struct atomsort_item *previtem;
	int cmpval;

	do {
		prev = &h->first;

		do {
			prevval = atomic_load_explicit(prev,
					memory_order_acquire);
			previtem = atomptr_p(prevval);

			if (!previtem || (cmpval = cmpfn(previtem, item)) > 0)
				break;
			if (cmpval == 0)
				return previtem;

			prev = &previtem->next;
		} while (1);

		if (atomptr_l(prevval))
			continue;

		item->next = prevval;
		if (atomic_compare_exchange_strong_explicit(prev, &prevval, i,
				memory_order_release, memory_order_relaxed))
			break;
	} while (1);

	atomic_fetch_add_explicit(&h->count, 1, memory_order_relaxed);
	return NULL;
}

/*
 * atomsort_del_core —— 有序表删除的内部核心（与 atomlist_del_core 逻辑一致）。
 * 参数：h 表头；item 已带 LOCK 标记的待删结点；hint 前驱缓存（可 NULL）；
 *       next item 摘除后前驱应指向的后继值。
 * 差异：有序表没有 last 指针，故无需清理尾引用；其余扫描/代删流程与单链表相同。
 * 返回：无；若已有其它线程完成删除则直接返回。
 */
static void atomsort_del_core(struct atomsort_head *h,
		struct atomsort_item *item, _Atomic atomptr_t *hint,
		atomptr_t next)
{
	_Atomic atomptr_t *prev = hint ? hint : &h->first, *upd;
	atomptr_t prevval, updval;
	struct atomsort_item *prevptr;

	atomic_fetch_sub_explicit(&h->count, 1, memory_order_relaxed);

	/* the following code should be identical (except sort<>list) to
	 * atomlist_del_core()
	 */
	while (1) {
		upd = NULL;
		updval = ATOMPTR_LOCK;

		do {
			prevval = atomic_load_explicit(prev,
					memory_order_consume);

			/* track the beginning of a chain of deleted items
			 * this is necessary to make this lock-free; we can
			 * complete deletions started by other threads.
			 */
			if (!atomptr_l(prevval)) {
				updval = prevval;
				upd = prev;
			}

			prevptr = atomsort_itemp(prevval);
			if (prevptr == item)
				break;

			prev = &prevptr->next;
		} while (prevptr);

		if (prevptr != item)
			/* another thread completed our deletion */
			return;

		if (!upd || atomptr_l(updval)) {
			/* failed to find non-deleted predecessor...
			 * have to try again
			 */
			prev = &h->first;
			continue;
		}

		if (!atomic_compare_exchange_strong_explicit(upd, &updval,
					next, memory_order_relaxed,
					memory_order_relaxed)) {
			/* prev doesn't point to item anymore, something
			 * was inserted.  continue at same position forward.
			 */
			continue;
		}
		break;
	}
}

/*
 * atomsort_del_hint —— 删除有序表中的 item，hint 为前驱指针缓存（可 NULL）。
 * 步骤：对 item->next 做 fetch_or(LOCK) 打删除标记（memory_order_seq_cst），
 *   断言此前未被打标记；再调 atomsort_del_core。
 * 前置条件：调用方独占持有该元素引用。
 */
void atomsort_del_hint(struct atomsort_head *h, struct atomsort_item *item,
		_Atomic atomptr_t *hint)
{
	atomptr_t next;

	/* mark ourselves in-delete - full barrier */
	next = atomic_fetch_or_explicit(&item->next, ATOMPTR_LOCK,
				memory_order_seq_cst);
	assert(!atomptr_l(next));	/* delete race on same item */

	atomsort_del_core(h, item, hint, next);
}

/*
 * atomsort_pop —— 弹出有序表 h 的第一个（最小）结点并返回；空表返回 NULL。
 * 流程与 atomlist_pop 相同（读表头 -> 打 LOCK 标记 -> del_core 摘除），
 * 只是删除核心换成 atomsort_del_core。
 */
struct atomsort_item *atomsort_pop(struct atomsort_head *h)
{
	struct atomsort_item *item;
	atomptr_t next;

	/* grab head of the list - and remember it in replval for the
	 * actual delete below.  No matter what, the head of the list is
	 * where we start deleting because either it's our item, or it's
	 * some delete-marked items and then our item.
	 */
	next = atomic_load_explicit(&h->first, memory_order_consume);

	do {
		item = atomsort_itemp(next);
		if (!item)
			return NULL;

		/* try to mark deletion */
		next = atomic_fetch_or_explicit(&item->next, ATOMPTR_LOCK,
					memory_order_acquire);

	} while (atomptr_l(next));
	/* if loop is taken: delete race on same item (another pop or del)
	 * => proceed to next item
	 * if loop exited here: we have our item selected and marked
	 */
	atomsort_del_core(h, item, &h->first, next);
	return item;
}
