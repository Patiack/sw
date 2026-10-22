// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Quagga Work Queues.
 *
 * Copyright (C) 2005 Sun Microsystems, Inc.
 */

#ifndef _QUAGGA_WORK_QUEUE_H
#define _QUAGGA_WORK_QUEUE_H

/*
 * 【文件导读】workqueue.h —— 工作队列(work queue)公共接口
 *
 * 1. 这个文件干什么：
 *    定义 FRR 事件驱动框架里的“工作队列”抽象。工作队列把一个函数(workfunc)
 *    反复作用到一批待处理数据项(items)上，用于把批量任务分摊到多个事件
 *    回调里执行，避免一次回调长时间占用事件循环(thread / event loop)。
 *
 * 2. 核心数据结构：
 *    - struct work_queue      : 队列本体，含 items 链表、计数与 spec 配置。
 *    - struct work_queue_item : 单个待处理项，data 为不透明指针。
 *    - wq_item_status         : workfunc 的返回值，决定项是完成还是重排。
 *    - spec                   : 用户必须填写的回调 / 参数区。
 *
 * 3. 与其他模块的关系：
 *    - 依赖 memory.h 的 MTYPE 记账与 queue.h 的 STAILQ 单向尾队列。
 *    - 每个队列挂在一个 struct event_loop 上，用其中的 event 触发
 *      work_queue_run()。
 *    - thread.c / command.c 会直接调用 work_queue_run 等内部入口。
 *
 * 4. 推荐阅读顺序：
 *    先看本头文件的 spec 与枚举 -> 再看 workqueue.c 的 work_queue_run
 *    -> 最后看 work_queue_add / plug / unplug 的调度细节。
 *
 * 5. 易踩的坑：
 *    - workfunc 在事件循环上下文被调用，不能阻塞、不能睡眠。
 *    - wq 的 spec 必须在 work_queue_add 之前填好，否则回调为 NULL。
 *    - WQ_REQUEUE 会把项重新排到队尾，注意避免无限循环(有 max_retries)。
 *    - struct work_queue 里 spec 之后的部分对调用者应视为不透明。
 */

#include "memory.h"
#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 声明一个内存记账类型 WORK_QUEUE。
 * FRR 用 MTYPE 对各类分配做统计，workqueue.c 里 xmalloc / xcalloc 都带此类型，
 * 便于 memory 命令查看内存占用。真正的定义由 DECLARE_MTYPE 宏展开，
 * 使用方只需包含本头文件即可引用该类型。
 */
DECLARE_MTYPE(WORK_QUEUE);

/*
 * 队列首次被调度时的默认“保持(hold)”时间，单位毫秒。
 * 目的是把刚加入的项攒一小会儿再统一处理，减少事件回调次数。
 * 0 表示不延迟、立即运行。
 */
/* Hold time for the initial schedule of a queue run, in  millisec */
#define WORK_QUEUE_DEFAULT_HOLD 50

/*
 * 队列处于 blocked / retry later 状态时的默认重试间隔。
 * 注意默认值是 0，表示不做额外惩罚性延迟，由上层自行设置 spec.retry。
 */
/* Retry for queue that is 'blocked' or 'retry later' */
#define WORK_QUEUE_DEFAULT_RETRY 0

/*
 * workfunc(工作函数)与项错误处理器的返回值语义：
 *   WQ_SUCCESS      : 该项处理成功，从队列中移除。
 *   WQ_REQUEUE      : 把该项重新排回队尾，继续处理后续项。
 *   WQ_QUEUE_BLOCKED: 当前无法处理(如资源忙碌)，队列整体暂停；
 *                     与 RETRY_LATER 类似，但不单独惩罚某个项。
 * 使用 WQ_QUEUE_BLOCKED 会让整个队列推迟，注意别造成长期饥饿。
 */
/* action value, for use by item processor and item error handlers */
typedef enum {
	WQ_SUCCESS = 0,
	WQ_REQUEUE,	  /* requeue item, continue processing work queue */
	WQ_QUEUE_BLOCKED, /* Queue cant be processed at this time.
			   * Similar to WQ_RETRY_LATER, but doesn't penalise
			   * the particular item.. */
} wq_item_status;

/*
 * 工作队列中的单个数据项。
 *   wq.next : STAILQ 链指针，由队列维护，调用者不要动。
 *   data    : 不透明负载，指向调用者自定义结构。
 *   ran     : 该项已被执行的次数，用于配合 spec.max_retries 做重试上限判断。
 * 调用者通常把 item 结构内嵌进自己的业务结构里并作为 data 传回。
 */
/* A single work queue item, unsurprisingly */
struct work_queue_item {
	STAILQ_ENTRY(work_queue_item) wq;
	void *data;	 /* opaque data */
	unsigned short ran; /* # of times item has been run */
};

/*
 * work_queue.flags 中的位标志：队列处于“已拔出插头(未 plug)”状态，可以排水。
 * 置位表示可用；被 work_queue_plug 清除表示暂停处理。
 */
#define WQ_UNPLUGGED	(1 << 0) /* available for draining */

/*
 * 工作队列主结构。
 * 字段分为三段：
 *   - master / event / name : 只读信息，事件循环句柄、当前活动 event、队列名。
 *   - spec                  : 使用者必须在使用前填写的配置(回调、重试、时间)。
 *   - items 及其后          : 队列内部状态，调用者应视为不透明，勿直接改。
 * spec 内各字段含义见下；items 为 STAILQ 单向尾队列。
 */
struct work_queue {
	/* Everything but the specification struct is private
	 * the following may be read
	 */
	struct event_loop *master;    /* thread master */
	struct event *event;	      /* thread, if one is active */
	char *name;		      /* work queue name */

	/* Specification for this work queue.
	 * Public, must be set before use by caller. May be modified at will.
	 */
	struct {
		/* optional opaque user data, global to the queue. */
		void *data;

		/* work function to process items with:
		 * First argument is the workqueue queue.
		 * Second argument is the item data
		 */
		wq_item_status (*workfunc)(struct work_queue *wq, void *data);

		/* callback to delete user specific item data */
		void (*del_item_data)(struct work_queue *wq, void *data);

		/* completion callback, called when queue is emptied, optional
		 */
		void (*completion_func)(struct work_queue *wq);

		/* max number of retries to make for item that errors */
		unsigned int max_retries;

		unsigned int hold; /* hold time for first run, in ms */

		unsigned long
			yield; /* yield time in us for associated thread */

		uint32_t retry; /* Optional retry timeout if queue is blocked */
	} spec;

	/* remaining fields should be opaque to users */
	STAILQ_HEAD(work_queue_items, work_queue_item)
	items;		      /* queue item list */
	int item_count;       /* queued items */
	unsigned long runs;   /* runs count */
	unsigned long yields; /* yields count */

	struct {
		unsigned int best;
		unsigned int granularity;
		unsigned long total;
	} cycles; /* cycle counts */

	/* private state */
	uint16_t flags; /* user set flag */
};

/* User API */

/*
 * 返回当前队列中待处理项的数量(读取 item_count)。
 * 内联，无副作用；注意它反映的是“已入队未处理”的项数。
 */
static inline int work_queue_item_count(struct work_queue *wq)
{
	return wq->item_count;
}

/*
 * 判断队列是否为空(项数为 0)。
 * 内联，无副作用；返回 true 表示没有待处理项。
 */
static inline bool work_queue_empty(struct work_queue *wq)
{
	return (wq->item_count == 0) ? true : false;
}

/*
 * 返回队列尾部的项(最后加入的项)，用 STAILQ_LAST 实现。
 * 队列为空时返回 NULL；调用者需自行判空。
 */
static inline struct work_queue_item *
work_queue_last_item(struct work_queue *wq)
{
	return STAILQ_LAST(&wq->items, work_queue_item, wq);
}

/* create a new work queue, of given name.
 * user must fill in the spec of the returned work queue before adding
 * anything to it
 */
/*
 * 创建一个指定名字的工作队列。
 *   m          : 事件循环(thread master)，队列将绑定到它。
 *   queue_name : 队列名，会被复制保存，用于调试与 CLI 展示。
 * 返回新队列指针；失败时可能返回 NULL。
 * 副作用：注册 MTYPE_WORK_QUEUE 内存类型、初始化 items 与 flags，
 * 并把 spec 各字段置 0 / NULL。**调用者必须先在返回的 wq->spec 里填好
 * workfunc 等回调，才能 work_queue_add**，否则运行时会因空指针崩溃。
 * 内存归属：由调用者负责，最终用 work_queue_free_and_null 释放。
 */
extern struct work_queue *work_queue_new(struct event_loop *m,
					 const char *queue_name);

/* destroy work queue */
/*
 * The usage of work_queue_free is being transitioned to pass
 * in the double pointer to remove use after free's.
 */
/*
 * 销毁工作队列并把调用者的指针置空，避免 use-after-free。
 *   wqp : 指向队列指针的指针(双重指针)。
 * 副作用：释放队列名、清空并(按 del_item_data)释放剩余项、注销 event 等。
 * 约定：所有剩余项的 del_item_data 回调会被逐项调用。
 * 注意：使用后 *wqp 变为 NULL，重复调用是安全的。
 */
extern void work_queue_free_and_null(struct work_queue **wqp);

/* Add the supplied data as an item onto the workqueue */
/*
 * 把一个数据项加入队列尾部。
 *   wq   : 目标队列，其 spec.workfunc 必须已设置。
 *   item : 不透明负载指针，交由 workfunc 处理。
 * 副作用：分配一个 work_queue_item 节点挂进 items，item_count 自增，
 * 并在必要时调度 work_queue_run 事件。若队列已空且未阻塞，会立即安排运行。
 * 内存归属：节点由队列管理，data 指向的内容仍归调用者。
 * 注意：若 workfunc 缺失，调度时会崩溃或报错，务必先配置 spec。
 */
extern void work_queue_add(struct work_queue *wq, void *item);

/* plug the queue, ie prevent it from being drained / processed */
/*
 * “拔出插头”：暂停队列处理，防止其被排空。
 *   wq : 目标队列。
 * 副作用：清除 WQ_UNPLUGGED 标志。之后即使有项也不会运行 workfunc，
 * 直到 work_queue_unplug 恢复。用于临时冻结队列。
 */
extern void work_queue_plug(struct work_queue *wq);
/* unplug the queue, allow it to be drained again */
/*
 * “插回插头”：恢复队列处理。
 *   wq : 目标队列。
 * 副作用：置位 WQ_UNPLUGGED，并在有积压项时重新调度运行。
 */
extern void work_queue_unplug(struct work_queue *wq);

/*
 * 查询队列当前是否已有排定的运行事件(是否被调度)。
 *   wq : 目标队列。
 * 返回 true 表示已有一个待触发的 work_queue_run，false 表示没有。
 * 供外部模块判断是否需要重复调度。
 */
bool work_queue_is_scheduled(struct work_queue *wq);

/* Helpers, exported for thread.c and command.c */
/*
 * 队列的实际执行引擎，由事件回调驱动。
 *   event : 携带队列指针的事件对象(event 的 arg 指向 work_queue)。
 * 副作用：循环弹出并执行 workfunc；依据返回值决定移除 / 重排 / 阻塞；
 * 处理周期统计(cycles)、yields，并在必要时重新排定下一次事件。
 * 约定：若队列被 plug 或无项，直接返回不处理。
 * 注意：这是 workfunc 被调用的上下文，属事件循环线程，禁止阻塞。
 * 本函数导出给 thread.c 与 command.c 使用。
 */
extern void work_queue_run(struct event *event);

/* Function to initialize the workqueue cli */
/*
 * 初始化工作队列相关的 CLI 命令(用于调试 / 查看队列状态)。
 * 通常在进程启动阶段调用一次，幂等性由调用方保证。
 */
extern void workqueue_cmd_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _QUAGGA_WORK_QUEUE_H */
