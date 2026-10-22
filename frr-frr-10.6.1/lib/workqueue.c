// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Quagga Work Queue Support.
 *
 * Copyright (C) 2005 Sun Microsystems, Inc.
 */

/*
 * 【文件导读】workqueue.c —— 工作队列的实现
 *
 * 1. 职责：
 *    实现 workqueue.h 声明的接口。工作队列把一批数据项分多次、在多个
 *    事件回调里陆续处理，核心目的是让长的批处理让出 CPU、避免阻塞事件循环。
 *
 * 2. 核心数据结构与全局：
 *    - _work_queues / work_queues : 进程内所有工作队列的全局链表，供 CLI 遍历。
 *    - struct work_queue          : 队列本体，见 workqueue.h。
 *    - work_queue_item            : 队列节点，data 指向业务负载。
 *    每个队列内部还维护运行统计: runs / yields / cycles，用于动态调整
 *    每次回调处理多少个项(yield granularity 退避算法)。
 *
 * 3. 与其他模块的关系：
 *    - frrevent.h : event_add_event / event_add_timer_msec / event_should_yield
 *                   提供事件循环与“该让出了”判断。
 *    - memory.h   : XCALLOC / XSTRDUP / XFREE 带 MTYPE 记账。
 *    - linklist.h : list 用于登记所有队列。
 *    - command.h  : 注册 show work-queues 命令。
 *
 * 4. 推荐阅读顺序：
 *    work_queue_new -> work_queue_add -> work_queue_schedule -> work_queue_run
 *    -> work_queue_plug / unplug -> 队列项生命周期函数(item_new/free/remove)。
 *
 * 5. 易踩的坑：
 *    - workfunc / del_item_data / completion_func 都在事件循环线程被调用，
 *      禁止阻塞、睡眠或做慢操作。
 *    - work_queue_schedule 只有在“未 plug 且无已排在途事件且非空”时才排程，
 *      所以重复 add 不会重复排事件。
 *    - work_queue_run 中 WQ_REQUEUE 会 requeue 项并可能造成长循环；
 *      靠 event_should_yield 与 yield granularity 控制让出时机。
 *    - item->ran 与 spec.max_retries 配合限制重试；WQ_QUEUE_BLOCKED /
 *      WQ_REQUEUE 分支会先把 ran 减回去，避免被误判超过重试上限。
 *    - 销毁队列用 work_queue_free_and_null(双重指针)，否则易 use-after-free。
 */

#include <zebra.h>
#include "frrevent.h"
#include "memory.h"
#include "workqueue.h"
#include "linklist.h"
#include "command.h"
#include "log.h"

/*
 * 定义内存记账类型，供 XCALLOC / XFREE 标记分配来源：
 *   WORK_QUEUE      : 队列结构体本身，全局可见(导出)。
 *   WORK_QUEUE_ITEM : 队列节点，文件内静态(STATIC)。
 *   WORK_QUEUE_NAME : 队列名字符串，文件内静态。
 * 这些类型名用于 memory 命令统计，本身不产生运行逻辑。
 */
DEFINE_MTYPE(LIB, WORK_QUEUE, "Work queue");
DEFINE_MTYPE_STATIC(LIB, WORK_QUEUE_ITEM, "Work queue item");
DEFINE_MTYPE_STATIC(LIB, WORK_QUEUE_NAME, "Work queue name string");

/*
 * 全局工作队列链表：登记本进程内创建的所有 work_queue，主要供 CLI
 * (show work-queues)遍历展示，以及集中管理。
 * _work_queues 是真正的结构体存储；work_queues 只是指向它的指针，
 * 之所以用指针是为了绕开 ALL_LIST_ELEMENTS_RO 宏在某些编译器下的无害告警。
 * 访问只应在事件循环线程内进行，非线程安全。
 */
/* master list of work_queues */
static struct list _work_queues;
/* pointer primarily to avoid an otherwise harmless warning on
 * ALL_LIST_ELEMENTS_RO
 */
static struct list *work_queues = &_work_queues;

/*
 * yield granularity 的最小值：每次检查“是否该让出 CPU”之间至少处理 1 个项。
 * granularity 必须 > 0，否则 work_queue_run 里的取模运算会除零。
 */
#define WORK_QUEUE_MIN_GRANULARITY 1

/*
 * 分配一个空的工作队列节点(仅分配，不设 data、不入队)。
 *   wq : 目标队列，仅用于断言非空(目前不参与逻辑)。
 * 返回清零后的节点；分配走 XCALLOC，失败会触发 FRR 的致命分配错误处理。
 * 内存归属：调用者负责，最终通过 work_queue_item_free 释放。
 * 注意：返回节点的 data 为 NULL，必须由调用方填充后再入队。
 */
static struct work_queue_item *work_queue_item_new(struct work_queue *wq)
{
	struct work_queue_item *item;
	assert(wq);

	item = XCALLOC(MTYPE_WORK_QUEUE_ITEM, sizeof(struct work_queue_item));

	return item;
}

/*
 * 释放一个工作队列节点(仅释放节点内存，不触碰 item->data)。
 *   item : 待释放节点，可为 NULL(XFREE 对 NULL 安全)。
 * 注意：调用前应确保该节点已从队列链表摘除，否则会造成悬空链表指针。
 * data 所指业务内存的释放由 del_item_data 回调负责，不在本函数内。
 */
static void work_queue_item_free(struct work_queue_item *item)
{
	XFREE(MTYPE_WORK_QUEUE_ITEM, item);
	return;
}

/*
 * 把节点从队列链表中摘除，并把 item_count 减一。
 *   wq   : 目标队列，断言 item_count > 0。
 *   item : 待摘除节点。
 * 副作用：修改队列计数与 STAILQ 链。不释放节点、不调用 del_item_data。
 * 注意：断言失败(计数为 0)说明调用者状态不一致，属编程错误。
 */
static inline void work_queue_item_dequeue(struct work_queue *wq,
					   struct work_queue_item *item)
{
	assert(wq->item_count > 0);

	wq->item_count--;
	STAILQ_REMOVE(&wq->items, item, work_queue_item, wq);
}

/*
 * 完整移除一个节点：先回调 del_item_data 释放业务数据，再摘链、再释放节点。
 *   wq   : 目标队列，用于取 spec.del_item_data。
 *   item : 待移除节点，断言 item 与其 data 均非空。
 * 副作用：可能调用用户回调 del_item_data(wq, item->data)，随后 dequeue 并
 * 释放节点。这是项的“正常消亡”路径(处理成功或超重试时使用)。
 * 注意：del_item_data 在事件循环线程执行，勿阻塞；若未设置该回调则跳过。
 */
static void work_queue_item_remove(struct work_queue *wq,
				   struct work_queue_item *item)
{
	assert(item && item->data);

	/* call private data deletion callback if needed */
	if (wq->spec.del_item_data)
		wq->spec.del_item_data(wq, item->data);

	work_queue_item_dequeue(wq, item);

	work_queue_item_free(item);

	return;
}

/* create new work queue */
/*
 * 创建一个工作队列并登记到全局链表。
 *   m          : 事件循环句柄，保存到 new->master。
 *   queue_name : 队列名，XSTRDUP 复制保存(MTYPE_WORK_QUEUE_NAME)。
 * 返回新队列；XCALLOC 失败会走 FRR 致命处理，通常不会返回 NULL。
 * 副作用：置 WQ_UNPLUGGED 标志(默认可用)、初始化 items、加入 work_queues，
 * 并把 granularity 设为最小值、hold/yield/retry 设为默认值。
 * 注意：此时 spec.workfunc 仍为 NULL，调用者必须在 add 前配置好。
 */
struct work_queue *work_queue_new(struct event_loop *m, const char *queue_name)
{
	struct work_queue *new;

	new = XCALLOC(MTYPE_WORK_QUEUE, sizeof(struct work_queue));

	new->name = XSTRDUP(MTYPE_WORK_QUEUE_NAME, queue_name);
	new->master = m;
	SET_FLAG(new->flags, WQ_UNPLUGGED);

	STAILQ_INIT(&new->items);

	listnode_add(work_queues, new);

	new->cycles.granularity = WORK_QUEUE_MIN_GRANULARITY;

	/* Default values, can be overridden by caller */
	new->spec.hold = WORK_QUEUE_DEFAULT_HOLD;
	new->spec.yield = EVENT_YIELD_TIME_SLOT;
	new->spec.retry = WORK_QUEUE_DEFAULT_RETRY;

	return new;
}

/*
 * 销毁工作队列并把 *wqp 置 NULL，杜绝 use-after-free。
 *   wqp : 双重指针，指向调用者持有的队列指针。
 * 副作用：取消在途事件；对剩余项逐个调用 work_queue_item_remove
 * (因此会触发 del_item_data 释放业务数据)；从全局链表摘除；释放名字与本体。
 * 注意：从队尾(work_queue_last_item)开始清，保证遍历安全；
 * 若 wqp 为 NULL 或 *wqp 为 NULL 会解引用空指针，调用者需保证有效。
 */
void work_queue_free_and_null(struct work_queue **wqp)
{
	struct work_queue *wq = *wqp;

	event_cancel(&wq->event);

	while (!work_queue_empty(wq)) {
		struct work_queue_item *item = work_queue_last_item(wq);

		work_queue_item_remove(wq, item);
	}

	listnode_delete(work_queues, wq);

	XFREE(MTYPE_WORK_QUEUE_NAME, wq->name);
	XFREE(MTYPE_WORK_QUEUE, wq);

	*wqp = NULL;
}

/*
 * 查询队列当前是否已有排定的事件(是否被调度)。
 *   wq : 目标队列。
 * 返回 event_is_scheduled(wq->event) 的结果，true 表示有一个在途的
 * work_queue_run。无副作用；用于避免重复排程。
 */
bool work_queue_is_scheduled(struct work_queue *wq)
{
	return event_is_scheduled(wq->event);
}

/*
 * 尝试为队列排定一次运行事件。
 *   wq    : 目标队列。
 *   delay : 延迟毫秒数；0 表示作为普通事件立即排入，>0 表示用定时器。
 * 返回 1 表示成功排程，0 表示未排(队列被 plug / 已有在途事件 / 队列空)。
 * 副作用：注册事件并绑定到 wq->event；若 spec.yield 非默认则设置让步时间；
 * 定时器路径会调用 event_ignore_late_timer(迟到的定时器不再补触发)。
 * 注意：重复调用是安全的——已排程时直接返回 0，不会叠加事件。
 */
static int work_queue_schedule(struct work_queue *wq, unsigned int delay)
{
	/* if appropriate, schedule work queue thread */
	if (CHECK_FLAG(wq->flags, WQ_UNPLUGGED) && !event_is_scheduled(wq->event) &&
	    !work_queue_empty(wq)) {
		/* Schedule timer if there's a delay, otherwise just schedule
		 * as an 'event'
		 */
		if (delay > 0) {
			event_add_timer_msec(wq->master, work_queue_run, wq, delay, &wq->event);
			event_ignore_late_timer(wq->event);
		} else
			event_add_event(wq->master, work_queue_run, wq, 0, &wq->event);

		/* set thread yield time, if needed */
		if (event_is_scheduled(wq->event) && wq->spec.yield != EVENT_YIELD_TIME_SLOT)
			event_set_yield_time(wq->event, wq->spec.yield);
		return 1;
	} else
		return 0;
}

/*
 * 把节点插入队列尾部并让 item_count 自增。与 dequeue 互为逆操作。
 *   wq   : 目标队列。
 *   item : 待入队节点，其 data 应已填好。
 * 副作用：修改 STAILQ 链与计数。不分配、不回调。
 */
static inline void work_queue_item_enqueue(struct work_queue *wq,
					   struct work_queue_item *item)
{
	STAILQ_INSERT_TAIL(&wq->items, item, wq);
	wq->item_count++;
}

/*
 * 对外接口：把一个数据项加入队列并触发可能的排程。
 *   wq   : 目标队列，断言非空；其 spec.workfunc 必须已配置。
 *   data : 不透明负载，保存到新节点的 data 字段。
 * 副作用：分配节点、入队、然后按 spec.hold 延迟尝试排程(work_queue_schedule)。
 * 注意：若队列已被 plug 或已有在途事件，这里不会真正排新事件，
 * 由后续 unplug / run 收尾时再调度；data 内存归调用者所有。
 */
void work_queue_add(struct work_queue *wq, void *data)
{
	struct work_queue_item *item;

	assert(wq);

	item = work_queue_item_new(wq);

	item->data = data;
	work_queue_item_enqueue(wq, item);

	work_queue_schedule(wq, wq->spec.hold);

	return;
}

/*
 * 把节点移到队列尾部(先摘除再尾插)，用于 WQ_REQUEUE 的“稍后再处理”。
 *   wq   : 目标队列。
 *   item : 待重排节点。
 * 副作用：dequeue + enqueue，计数先减后加净不变。
 * 注意：这是原地重排，节点与其 data 均不释放；若处理函数一直返回
 * WQ_REQUEUE，可能反复循环，靠 max_retries 与让步机制收敛。
 */
static void work_queue_item_requeue(struct work_queue *wq,
				    struct work_queue_item *item)
{
	work_queue_item_dequeue(wq, item);

	/* attach to end of list */
	work_queue_item_enqueue(wq, item);
}

/*
 * CLI 命令 “show work-queues” 的实现。
 * 遍历全局 work_queues 链表，逐行打印每个队列的: 是否被 plug(P 标记)、
 * 待处理项数、hold 值、总运行次数、让步次数、周期统计(最佳/粒度/总计/平均)
 * 以及队列名。第一个字符为 'P' 表示该队列已被 plug(暂停)。
 * 副作用：仅写 vty 输出。返回 CMD_SUCCESS。
 * 注意：展示专用，读取队列内部字段；平均值为 cycles.total / runs，
 * runs 为 0 时输出 0 以避免除零。
 */
DEFUN (show_work_queues,
       show_work_queues_cmd,
       "show work-queues",
       SHOW_STR
       "Work Queue information\n")
{
	struct listnode *node;
	struct work_queue *wq;

	vty_out(vty, "%c %8s %5s %8s %8s %21s\n", ' ', "List", "(ms) ",
		"Q. Runs", "Yields", "Cycle Counts   ");
	vty_out(vty, "%c %8s %5s %8s %8s %7s %6s %8s %6s %s\n", 'P', "Items",
		"Hold", "Total", "Total", "Best", "Gran.", "Total", "Avg.",
		"Name");

	for (ALL_LIST_ELEMENTS_RO(work_queues, node, wq)) {
		vty_out(vty, "%c %8d %5d %8ld %8ld %7d %6d %8ld %6u %s\n",
			(CHECK_FLAG(wq->flags, WQ_UNPLUGGED) ? ' ' : 'P'),
			work_queue_item_count(wq), wq->spec.hold, wq->runs,
			wq->yields, wq->cycles.best, wq->cycles.granularity,
			wq->cycles.total,
			(wq->runs) ? (unsigned int)(wq->cycles.total / wq->runs)
				   : 0,
			wq->name);
	}

	return CMD_SUCCESS;
}

/*
 * 注册工作队列相关的 CLI 命令。
 * 把 show_work_queues_cmd 安装到 VIEW_NODE，使普通视图即可查看队列状态。
 * 通常在守护进程启动初始化阶段调用一次。
 */
void workqueue_cmd_init(void)
{
	install_element(VIEW_NODE, &show_work_queues_cmd);
}

/* 'plug' a queue: Stop it from being scheduled,
 * ie: prevent the queue from draining.
 */
/*
 * 暂停队列：撤销在途事件并清除 WQ_UNPLUGGED 标志，使其不再被排程 / 排水。
 *   wq : 目标队列。
 * 副作用：event_cancel 取消当前事件；之后 work_queue_schedule 会因标志
 * 未置位而拒绝排程，直到 work_queue_unplug。
 * 注意：已入队的项会保留，等 unplug 后再处理。
 */
void work_queue_plug(struct work_queue *wq)
{
	event_cancel(&wq->event);

	UNSET_FLAG(wq->flags, WQ_UNPLUGGED);
}

/* unplug queue, schedule it again, if appropriate
 * Ie: Allow the queue to be drained again
 */
/*
 * 恢复队列：置位 WQ_UNPLUGGED 并尝试重新排程(按 spec.hold 延迟)。
 *   wq : 目标队列。
 * 副作用：SET_FLAG 后调用 work_queue_schedule。若没有待处理项或已有在途
 * 事件，则本次不会产生新事件。
 * 注意：与 plug 成对使用，务必确保最终会被 unplug，否则队列永久冻结。
 */
void work_queue_unplug(struct work_queue *wq)
{
	SET_FLAG(wq->flags, WQ_UNPLUGGED);

	/* if thread isnt already waiting, add one */
	work_queue_schedule(wq, wq->spec.hold);
}

/* timer thread to process a work queue
 * will reschedule itself if required,
 * otherwise work_queue_item_add
 */
/*
 * 工作队列的运行引擎，作为事件回调被调用一次即处理一批项。
 *   event : 事件对象，其 arg(通过 EVENT_ARG 取)指向对应的 work_queue。
 * 流程要点：
 *   1) 校正 granularity(>0)；用 STAILQ_FOREACH_SAFE 安全遍历(允许删除当前项)。
 *   2) 若某节点 ran 已超过 spec.max_retries，则直接移除(放弃)。
 *   3) 调用 workfunc(wq, item->data)；注意内层 do/while 会在
 *      ran < max_retries 时反复重试同一项——这是“立即重试”语义。
 *   4) 依据返回值：WQ_QUEUE_BLOCKED 回退 ran 并跳到 stats(整队稍后再来)；
 *      WQ_REQUEUE 回退 ran 并把项移到队尾；其余(含 WQ_SUCCESS)移除该项。
 *   5) 每个完成的 cycle 检查 event_should_yield，若该让步就置 yielded 跳到 stats。
 *   6) stats 处根据是否让出、cycles 与 granularity 的关系动态调整粒度，
 *      形成带迟滞(hysteresis)的自适应批处理。
 *   7) 若队列仍未空，则按 retry(blocked)或 0(否则)重新排程；若已空则调用
 *      spec.completion_func(若设置)。
 * 副作用：大量——执行用户 workfunc、修改队列结构、更新统计、重排事件。
 * 注意：全程运行在事件循环线程；workfunc 内严禁阻塞。
 */
void work_queue_run(struct event *event)
{
	struct work_queue *wq;
	struct work_queue_item *item, *titem;
	wq_item_status ret = WQ_SUCCESS;
	unsigned int cycles = 0;
	char yielded = 0;

	wq = EVENT_ARG(event);

	assert(wq);

	/* calculate cycle granularity:
	 * list iteration == 1 run
	 * listnode processing == 1 cycle
	 * granularity == # cycles between checks whether we should yield.
	 *
	 * granularity should be > 0, and can increase slowly after each run to
	 * provide some hysteris, but not past cycles.best or 2*cycles.
	 *
	 * Best: starts low, can only increase
	 *
	 * Granularity: starts at WORK_QUEUE_MIN_GRANULARITY, can be decreased
	 *              if we run to end of time slot, can increase otherwise
	 *              by a small factor.
	 *
	 * We could use just the average and save some work, however we want to
	 * be
	 * able to adjust quickly to CPU pressure. Average wont shift much if
	 * daemon has been running a long time.
	 */
	if (wq->cycles.granularity == 0)
		wq->cycles.granularity = WORK_QUEUE_MIN_GRANULARITY;

	STAILQ_FOREACH_SAFE (item, &wq->items, wq, titem) {
		assert(item->data);

		/* dont run items which are past their allowed retries */
		if (item->ran > wq->spec.max_retries) {
			work_queue_item_remove(wq, item);
			continue;
		}

		/* run and take care of items that want to be retried
		 * immediately */
		do {
			ret = wq->spec.workfunc(wq, item->data);
			item->ran++;
		} while (item->ran < wq->spec.max_retries);

		switch (ret) {
		case WQ_QUEUE_BLOCKED: {
			/* decrement item->ran again, cause this isn't an item
			 * specific error, and retry later
			 */
			item->ran--;
			goto stats;
		}
		case WQ_REQUEUE: {
			item->ran--;
			work_queue_item_requeue(wq, item);
			/* If a single node is being used with a meta-queue
			 * (e.g., zebra),
			 * update the next node as we don't want to exit the
			 * event and
			 * reschedule it after every node. By definition,
			 * WQ_REQUEUE is
			 * meant to continue the processing; the yield logic
			 * will kick in
			 * to terminate the event when time has exceeded.
			 */
			if (titem == NULL)
				titem = item;
			break;
		}
		case WQ_SUCCESS:
		default: {
			work_queue_item_remove(wq, item);
			break;
		}
		}

		/* completed cycle */
		cycles++;

		/* test if we should yield */
		if (!(cycles % wq->cycles.granularity) &&
		    event_should_yield(event)) {
			yielded = 1;
			goto stats;
		}
	}

stats:

/*
 * 粒度调整的迟滞系数：当 cycles 远超 granularity 时成倍放大粒度(快升)，
 * 略超时只小幅增加(慢加)，从而在 CPU 压力变化时避免剧烈震荡。
 * 仅在本函数 stats 标签段内使用，函数结尾 #undef。
 */
#define WQ_HYSTERESIS_FACTOR 4

	/* we yielded, check whether granularity should be reduced */
	if (yielded && (cycles < wq->cycles.granularity)) {
		wq->cycles.granularity =
			((cycles > 0) ? cycles : WORK_QUEUE_MIN_GRANULARITY);
	}
	/* otherwise, should granularity increase? */
	else if (cycles >= (wq->cycles.granularity)) {
		if (cycles > wq->cycles.best)
			wq->cycles.best = cycles;

		/* along with yielded check, provides hysteresis for granularity
		 */
		if (cycles > (wq->cycles.granularity * WQ_HYSTERESIS_FACTOR
			      * WQ_HYSTERESIS_FACTOR))
			wq->cycles.granularity *=
				WQ_HYSTERESIS_FACTOR; /* quick ramp-up */
		else if (cycles
			 > (wq->cycles.granularity * WQ_HYSTERESIS_FACTOR))
			wq->cycles.granularity += WQ_HYSTERESIS_FACTOR;
	}
#undef WQ_HYSTERIS_FACTOR

	wq->runs++;
	wq->cycles.total += cycles;
	if (yielded)
		wq->yields++;

	/* Is the queue done yet? If it is, call the completion callback. */
	if (!work_queue_empty(wq)) {
		if (ret == WQ_QUEUE_BLOCKED)
			work_queue_schedule(wq, wq->spec.retry);
		else
			work_queue_schedule(wq, 0);

	} else if (wq->spec.completion_func)
		wq->spec.completion_func(wq);
}
