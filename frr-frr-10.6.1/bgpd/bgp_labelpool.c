// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BGP Label Pool - Manage label chunk allocations from zebra asynchronously
 *
 * Copyright (C) 2018 LabN Consulting, L.L.C.
 */

/*
 * ============================================================================
 * 【文件导读】bgp_labelpool.c —— 标签池：向 zebra 申请 label chunk 并按需分配
 * ============================================================================
 * 【它解决什么问题】
 *   MPLS 转发要有本机“出标签”。20 位标签空间由 zebra 的标签管理器统一掌管，bgpd
 *   不能自己乱取。做法是向 zebra 申请**一段连续标签(chunk)**，再在本地把这段标签
 *   按需分配给不同用途。本文件就是这套“本地标签池”的实现。
 *
 * 【三层数据结构（务必先理解，其余都是围绕它们的操作）】
 *   ledger(跳表) : 键 = labelid(请求者唯一标识)，值 = struct lp_lcb。
 *                  “已受理的请求账本”。哪怕标签还没分下来也会先登记，用于识别
 *                  重复请求、释放时校验归属。
 *   inuse (跳表) : 键 = 标签值，值 = labelid。“当前已分配出去的标签”，用于从标签
 *                  反查到请求者、释放时确认标签确实在用。
 *   chunks(链表) : zebra 授予的连续标签段 struct lp_chunk，每段带一个 bitfield 标记
 *                  哪些标签已分配(allocated_map)与剩余空闲数(nfree)。分配就在这些
 *                  段里找空闲位；找不到就再向 zebra 要一段。
 *
 * 【异步与状态机】
 *   与 zebra 的交互是异步的：bgp_lp_get() 里若本地池有货就立刻排队回调；否则把请求挂到
 *   requests FIFO，并向 zebra 申请 chunk；等 bgp_lp_event_chunk() 收到 chunk 后，再由
 *   bgp_sync_label_manager() 把 FIFO 里的请求逐个满足。
 *   回调不直接调用，而是投递到 callback_q(工作队列)里执行，避免在消息处理路径上重入。
 *   与 zebra 断连时允许继续用旧标签(bgp_lp_event_zebra_down 空实现)；重连时
 *   (bgp_lp_event_zebra_up) 把旧标签全部作废，通过回调(allocated=false)通知请求者，
 *   并重新为其分配，因此请求者无需自己重发申请。
 *
 * 【引用计数 / 共享】
 *   同一条路由可能被多处引用同一个标签，必须等所有使用者都不再用了才能释放
 *   (bgp_lp_release 会校验 labelid / type)。chunk 只有在整段都空闲、且没有待处理请求
 *   时，才归还 zebra 并释放。
 *
 * 【建议阅读顺序】
 *   常量与数据结构(lp_chunk / lp_lcb / lp_fifo / lp_cbq_item) → bgp_lp_init →
 *   bgp_lp_get(快 / 慢路径) → get_label_from_pool → bgp_lp_release →
 *   bgp_sync_label_manager / bgp_lp_event_chunk / bgp_lp_event_zebra_up(状态机) →
 *   show 命令 → 末尾 bgp_label_per_nexthop_* 一族。
 *   BGP_LABELPOOL_ENABLE_TESTS 包住的测试代码默认关闭，可略读。
 * ============================================================================
 */

#include <zebra.h>

#include "log.h"
#include "memory.h"
#include "stream.h"
#include "mpls.h"
#include "vty.h"
#include "linklist.h"
#include "skiplist.h"
#include "workqueue.h"
#include "mpls.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_labelpool.h"
#include "bgpd/bgp_label.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_errors.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_zebra.h"
#include "bgpd/bgp_vty.h"
#include "bgpd/bgp_rd.h"

/* 【编译开关】置 1 才会编入文件末尾的压测代码(lptest)；默认 0，生产构建不含。 */
#define BGP_LABELPOOL_ENABLE_TESTS 0

#include "bgpd/bgp_labelpool_clippy.c"


#if BGP_LABELPOOL_ENABLE_TESTS
static void lptest_init(void);
static void lptest_finish(void);
#endif

static void bgp_sync_label_manager(struct event *e);
static void lp_chunk_free(void *goner);
static void bgp_lp_release(mpls_label_t label, void *labelid, int type, bool check_type,
			   bool debug_enabled);

/*
 * Remember where pool data are kept
 */
/* 模块全局单例指针，指向 bgp_lp_init() 传入的 labelpool 实例；后续操作都作用于它。 */
static struct labelpool *lp;

/* 【关键常量】chunk 大小策略：首次申请 LP_CHUNK_SIZE_MIN=128 个，之后每次翻倍直到上限
 * LP_CHUNK_SIZE_MAX = 1<<(20-4) = 65536（即整个 20 位标签空间的 1/16）。
 * 为何不一次要很多：标签空间在本机各 FRR 进程间共享，贪心申请既浪费又挤占别人；限制在
 * 1/16 可减少浪费，代价是本进程空闲标签的查找会稍慢一点。 */
/*
 * Number of labels requested at a time from the zebra label manager.
 * We start small but double the request size each time up to a
 * maximum size.
 *
 * The label space is 20 bits which is shared with other FRR processes
 * on this host, so to avoid greedily requesting a mostly wasted chunk,
 * we limit the chunk size to 1/16 of the label space (that's the -4 bits
 * in the definition below). This limit slightly increases our cost of
 * finding free labels in our allocated chunks.
 */
#define LP_CHUNK_SIZE_MIN 128
#define LP_CHUNK_SIZE_MAX (1 << (20 - 4))

/* 【内存类型】交给 FRR 内存统计系统的类型标签，便于用 show memory 追踪各结构用量。 */
DEFINE_MTYPE_STATIC(BGPD, BGP_LABEL_CHUNK, "BGP Label Chunk");
DEFINE_MTYPE_STATIC(BGPD, BGP_LABEL_FIFO, "BGP Label FIFO item");
DEFINE_MTYPE_STATIC(BGPD, BGP_LABEL_CB, "BGP Dynamic Label Assignment");
DEFINE_MTYPE_STATIC(BGPD, BGP_LABEL_CBQ, "BGP Dynamic Label Callback");

/* 【结构】struct lp_chunk —— zebra 授予的一段连续标签 [first, last]。
 *   nfree             : 这段里尚未分配的标签个数（为 0 说明整段用满）。
 *   idx_last_allocated: 上次分配到的位下标，下次从这里往后找，避免重复扫描。
 *   allocated_map     : 位图，第 i 位为 1 表示标签 first+i 已分配。
 * 分配(get_label_from_pool)与释放(bgp_lp_release)都在这个位图上做置位 / 清位。 */
struct lp_chunk {
	uint32_t	first;
	uint32_t	last;
	uint32_t nfree;		     /* un-allocated count */
	uint32_t idx_last_allocated; /* start looking here */
	bitfield_t allocated_map;
};

/*
 * label control block
 */
/* 【结构】struct lp_lcb —— Label Control Block，记账“一个请求”的全部信息。
 *   label   : 已分配到本请求的标签；MPLS_LABEL_NONE 表示还没分配（在排队等 chunk）。
 *   type    : 用途(LP_TYPE_*)。
 *   labelid : 请求者唯一标识，作为回调句柄回传。
 *   vrf_id  : 所属 VRF，回调时用来找 bgp 实例。
 *   cbfunc  : 回调；allocated=false 表示原先的标签失效（例如 zebra 重连）。
 * ledger 跳表就是 labelid → lp_lcb 的映射。 */
struct lp_lcb {
	mpls_label_t	label;		/* MPLS_LABEL_NONE = not allocated */
	int		type;
	void		*labelid;	/* unique ID */
	vrf_id_t	vrf_id;
	/*
	 * callback for label allocation and loss
	 *
	 * allocated: false = lost
	 */
	int		(*cbfunc)(mpls_label_t label, void *lblid, bool alloc);
};

/* 【结构】struct lp_fifo —— 挂在 requests FIFO 上的元素：内嵌一份 lp_lcb 副本(快照)。
 * 用副本而非指针，是因为 FIFO 里的请求可能在被处理前就已失效，副本可避免悬空引用。 */
struct lp_fifo {
	struct lp_fifo_item fifo;
	struct lp_lcb	lcb;
};

DECLARE_LIST(lp_fifo, struct lp_fifo, fifo);

/* 【结构】struct lp_cbq_item —— 投递到 callback_q 工作队列的一项“待执行回调”。
 * 把 label、labelid、vrf_id、allocated 等参数打包，延迟到工作队列里再调用请求者的
 * cbfunc；这样分配动作与回调执行解耦，避免重入。 */
struct lp_cbq_item {
	int		(*cbfunc)(mpls_label_t label, void *lblid, bool alloc);
	int		type;
	mpls_label_t	label;
	void		*labelid;
	vrf_id_t	vrf_id;
	bool		allocated;	/* false = lost */
};

/* 【函数】lp_cbq_docallback —— 工作队列里真正执行回调的地方。要点：
 *   - 若 label==MPLS_LABEL_NONE（不应发生）则记错误并返回。
 *   - 找不到对应 bgp 实例时，如果标签是“已分配”的，仍需把它还回池里(bgp_lp_release)，
 *     否则会泄漏；然后不再回调。
 *   - allocated=true 但回调返回非 0，表示请求者拒收（常见于请求发出后配置又变了）；
 *     此时释放该标签。
 * 返回值恒为 WQ_SUCCESS。 */
static wq_item_status lp_cbq_docallback(struct work_queue *wq, void *data)
{
	struct lp_cbq_item *lcbq = data;
	int rc;
	int debug = BGP_DEBUG(labelpool, LABELPOOL);
	struct bgp *bgp = bgp_lookup_by_vrf_id(lcbq->vrf_id);

	if (debug)
		zlog_debug("%s: calling callback with labelid=%p label=%u allocated=%d",
			__func__, lcbq->labelid, lcbq->label, lcbq->allocated);

	if (lcbq->label == MPLS_LABEL_NONE) {
		/* shouldn't happen */
		flog_err(EC_BGP_LABEL, "%s: error: label==MPLS_LABEL_NONE",
			 __func__);
		return WQ_SUCCESS;
	}

	if (!bgp) {
		/*
		 * If we can't find the BGP instance, we should still release
		 * the label back to the pool since we can't process the callback
		 */
		if (lcbq->allocated)
			bgp_lp_release(lcbq->label, lcbq->labelid, 0, false, debug);

		return WQ_SUCCESS;
	}

	rc = (*(lcbq->cbfunc))(lcbq->label, lcbq->labelid, lcbq->allocated);

	if (lcbq->allocated && rc) {
		/*
		 * Callback rejected allocation. This situation could arise
		 * if there was a label request followed by the requestor
		 * deciding it didn't need the assignment (e.g., config
		 * change) while the reply to the original request (with
		 * label) was in the work queue.
		 */
		if (debug)
			zlog_debug("%s: callback rejected allocation, releasing labelid=%p label=%u",
				__func__, lcbq->labelid, lcbq->label);

		bgp_lp_release(lcbq->label, lcbq->labelid, 0, false, debug);
	}

	return WQ_SUCCESS;
}

/* 【函数】lp_cbq_item_free —— 工作队列删除元素时释放 lp_cbq_item 内存。 */
static void lp_cbq_item_free(struct work_queue *wq, void *data)
{
	XFREE(MTYPE_BGP_LABEL_CBQ, data);
}

/* 【函数】lp_lcb_free —— ledger 跳表的元素删除回调：释放 lp_lcb 内存。 */
static void lp_lcb_free(void *goner)
{
	XFREE(MTYPE_BGP_LABEL_CB, goner);
}

/* 【函数】lp_chunk_free —— chunks 链表的元素删除回调：释放位图与 lp_chunk 自身。 */
static void lp_chunk_free(void *goner)
{
	struct lp_chunk *chunk = (struct lp_chunk *)goner;

	bf_free(chunk->allocated_map);
	XFREE(MTYPE_BGP_LABEL_CHUNK, goner);
}

/* 【函数】bgp_lp_init —— 建立标签池的各数据结构并绑定回调工作队列。
 * 这里把模块全局指针 lp 指向调用方传入的 pool；后续所有操作都作用于它。
 *   ledger / inuse : 两个跳表(skiplist)，分别以 labelid / 标签值为键。
 *   chunks         : lp_chunk 链表，删除回调为 lp_chunk_free。
 *   requests       : 请求 FIFO(侵入式链表)。
 *   callback_q     : 工作队列，workfunc=lp_cbq_docallback，del_item_data=lp_cbq_item_free，
 *                    且 max_retries=0(回调失败不重试)。
 *   next_chunksize : 初始为最小值，之后逐次翻倍。 */
void bgp_lp_init(struct event_loop *master, struct labelpool *pool)
{
	if (BGP_DEBUG(labelpool, LABELPOOL))
		zlog_debug("%s: entry", __func__);

	lp = pool;	/* Set module pointer to pool data */

	lp->ledger = skiplist_new(0, NULL, lp_lcb_free);
	lp->inuse = skiplist_new(0, NULL, NULL);
	lp->chunks = list_new();
	lp->chunks->del = lp_chunk_free;
	lp_fifo_init(&lp->requests);
	lp->callback_q = work_queue_new(master, "label callbacks");

	lp->callback_q->spec.workfunc = lp_cbq_docallback;
	lp->callback_q->spec.del_item_data = lp_cbq_item_free;
	lp->callback_q->spec.max_retries = 0;

	lp->next_chunksize = LP_CHUNK_SIZE_MIN;

#if BGP_LABELPOOL_ENABLE_TESTS
	lptest_init();
#endif
}

/* 【函数】check_bgp_lu_cb_unlock / check_bgp_lu_cb_lock —— 针对 BGP-LU 请求的特殊处理：
 * LU 请求在排队 / 回调期间需要持有对应 dest 节点的锁(bgp_dest_lock_node)，以防节点在
 * 等待标签时被删除。二者成对使用，请求作废或回调结束时解锁。 */
/* check if a label callback was for a BGP LU node, and if so, unlock it */
static void check_bgp_lu_cb_unlock(struct lp_lcb *lcb)
{
	if (lcb->type == LP_TYPE_BGP_LU)
		bgp_dest_unlock_node(lcb->labelid);
}

/* check if a label callback was for a BGP LU node, and if so, lock it */
static void check_bgp_lu_cb_lock(struct lp_lcb *lcb)
{
	if (lcb->type == LP_TYPE_BGP_LU)
		bgp_dest_lock_node(lcb->labelid);
}

/* 【函数】bgp_lp_finish —— 模块退出清理：把手上所有 chunk 归还 zebra、清空 requests
 * FIFO（顺带给其中的 LU 请求解锁）、并遍历 callback_q 里尚未执行的回调给对应 LU 节点
 * 解锁。注意“为何要手动遍历 callback_q”：工作队列的删除回调在“执行完成”时也会被调用，
 * 若在删除回调里解锁会造成重复解锁，故这里显式遍历处理（见代码中 ugh 注释）。 */
void bgp_lp_finish(void)
{
	struct lp_fifo *lf;
	struct work_queue_item *item, *titem;
	struct listnode *node;
	struct lp_chunk *chunk;

#if BGP_LABELPOOL_ENABLE_TESTS
	lptest_finish();
#endif
	if (!lp)
		return;

	skiplist_free(lp->ledger);
	lp->ledger = NULL;

	skiplist_free(lp->inuse);
	lp->inuse = NULL;

	for (ALL_LIST_ELEMENTS_RO(lp->chunks, node, chunk))
		bgp_zebra_release_label_range(chunk->first, chunk->last);

	list_delete(&lp->chunks);

	while ((lf = lp_fifo_pop(&lp->requests))) {
		check_bgp_lu_cb_unlock(&lf->lcb);
		XFREE(MTYPE_BGP_LABEL_FIFO, lf);
	}
	lp_fifo_fini(&lp->requests);

	/* we must unlock path infos for LU callbacks; but we cannot do that
	 * in the deletion callback of the workqueue, as that is also called
	 * to remove an element from the queue after it has been run, resulting
	 * in a double unlock. Hence we need to iterate over our queues and
	 * lists and manually perform the unlocking (ugh)
	 */
	STAILQ_FOREACH_SAFE (item, &lp->callback_q->items, wq, titem)
		check_bgp_lu_cb_unlock(item->data);

	work_queue_free_and_null(&lp->callback_q);

	lp = NULL;
}

/* 【函数】get_label_from_pool —— 在本池已授予的 chunks 里找一个空闲标签。
 *   - 跳过 nfree==0 的段；
 *   - 用 bf_find_next_clear_bit_wrap 从上次位置之后“绕圈”找第一个空闲位
 *     (idx_last_allocated 记录上次落点，分摊扫描成本)；
 *   - 找到后把标签值插入 inuse 跳表(键=标签，值=labelid)，再置位、更新 nfree 并返回；
 *   - 都不行则返回 MPLS_LABEL_NONE，调用方据此决定“向 zebra 再要一段”。
 * 注意：只从“已到账”的 chunk 里取，不含尚未到账的。 */
static mpls_label_t get_label_from_pool(void *labelid)
{
	struct listnode *node;
	struct lp_chunk *chunk;
	int debug = BGP_DEBUG(labelpool, LABELPOOL);

	/*
	 * Find a free label
	 */
	for (ALL_LIST_ELEMENTS_RO(lp->chunks, node, chunk)) {
		uintptr_t lbl;
		unsigned int index;

		if (debug)
			zlog_debug("%s: chunk first=%u last=%u",
				__func__, chunk->first, chunk->last);

		/*
		 * don't look in chunks with no available labels
		 */
		if (!chunk->nfree)
			continue;

		/*
		 * roll through bitfield starting where we stopped
		 * last time
		 */
		index = bf_find_next_clear_bit_wrap(
			&chunk->allocated_map, chunk->idx_last_allocated + 1,
			0);

		/*
		 * since chunk->nfree is non-zero, we should always get
		 * a valid index
		 */
		assert(index != WORD_MAX);

		lbl = chunk->first + index;
		if (skiplist_insert(lp->inuse, (void *)lbl, labelid)) {
			/* something is very wrong */
			flog_err(EC_BGP_LABEL_POOL_INSERT_FAIL,
				 "%s: unable to insert inuse label %u (id %p)", __func__,
				 (uint32_t)lbl, labelid);
			return MPLS_LABEL_NONE;
		}

		/*
		 * Success
		 */
		bf_set_bit(chunk->allocated_map, index);
		chunk->idx_last_allocated = index;
		chunk->nfree -= 1;

		return lbl;
	}

	return MPLS_LABEL_NONE;
}

/*
 * Success indicated by value of "label" field in returned LCB
 */
/* 【函数】lcb_alloc —— 分配并初始化一个 lp_lcb。其中立刻尝试 get_label_from_pool()：
 * 若分到标签则 lcb->label 非 NONE（成功）；否则为 MPLS_LABEL_NONE，表示需要排队等 chunk。
 * 即“是否已成功分配”由返回 LCB 的 label 字段表示。 */
static struct lp_lcb *lcb_alloc(int type, void *labelid, vrf_id_t vrf_id,
				int (*cbfunc)(mpls_label_t label, void *labelid, bool allocated))
{
	/*
	 * Set up label control block
	 */
	struct lp_lcb *new = XCALLOC(MTYPE_BGP_LABEL_CB,
		sizeof(struct lp_lcb));

	new->label = get_label_from_pool(labelid);
	new->type = type;
	new->labelid = labelid;
	new->vrf_id = vrf_id;
	new->cbfunc = cbfunc;

	return new;
}

/*
 * Callers who need labels must supply a type, labelid, and callback.
 * The type is a value defined in bgp_labelpool.h (add types as needed).
 * The callback is for asynchronous notification of label allocation.
 * The labelid is passed as an argument to the callback. It should be unique
 * to the requested label instance.
 *
 * If zebra is not connected, callbacks with labels will be delayed
 * until connection is established. If zebra connection is lost after
 * labels have been assigned, existing assignments via this labelpool
 * module will continue until reconnection.
 *
 * When connection to zebra is reestablished, previous label assignments
 * will be invalidated (via callbacks having the "allocated" parameter unset)
 * and new labels will be automatically reassigned by this labelpool module
 * (that is, a requestor does not need to call bgp_lp_get() again if it is
 * notified via callback that its label has been lost: it will eventually
 * get another callback with a new label assignment).
 *
 * The callback function should return 0 to accept the allocation
 * and non-zero to refuse it. The callback function return value is
 * ignored for invalidations (i.e., when the "allocated" parameter is false)
 *
 * Prior requests for a given labelid are detected so that requests and
 * assignments are not duplicated.
 */
/* 【函数】bgp_lp_get —— 申请标签的主入口(异步)。执行流程：
 *   1) 先在 ledger 里按 labelid 查是否已受理：已存在 → requested=1（重复请求，
 *      不重复分配）；不存在 → lcb_alloc() 造一个 lp_lcb(内部会尝试从本地池取标签)
 *      并插入 ledger。
 *   2) 若 lcb->label != NONE：本地池已满足（或这是已满足过的重复请求）→ 打包一个
 *      lp_cbq_item 投入 callback_q 立即回调(allocated=true)，返回。
 *   3) 否则(没取到标签)：若为重复请求直接返回；否则进入**慢路径**——把请求加入
 *      requests FIFO，并在待处理请求数超过已申请量时向 zebra 再申请 next_chunksize
 *      个标签(并把下次块大小翻倍、封顶)，最后挂定时器稍后 bgp_sync_label_manager。
 * 【快 / 慢路径】快路径=本地有货立即回调；慢路径=登记排队，等 zebra 给 chunk。 */
void bgp_lp_get(int type, void *labelid, vrf_id_t vrf_id,
		int (*cbfunc)(mpls_label_t label, void *labelid, bool allocated))
{
	struct lp_lcb *lcb;
	int requested = 0;
	int debug = BGP_DEBUG(labelpool, LABELPOOL);

	if (debug)
		zlog_debug("%s: labelid=%p", __func__, labelid);

	/*
	 * Have we seen this request before?
	 */
	if (!skiplist_search(lp->ledger, labelid, (void **)&lcb)) {
		requested = 1;
	} else {
		lcb = lcb_alloc(type, labelid, vrf_id, cbfunc);
		if (debug)
			zlog_debug("%s: inserting lcb=%p label=%u",
				__func__, lcb, lcb->label);
		int rc = skiplist_insert(lp->ledger, labelid, lcb);

		if (rc) {
			/* shouldn't happen */
			flog_err(EC_BGP_LABEL,
				 "%s: can't insert new LCB into ledger list",
				 __func__);
			XFREE(MTYPE_BGP_LABEL_CB, lcb);
			return;
		}
	}

	if (lcb->label != MPLS_LABEL_NONE) {
		/*
		 * Fast path: we filled the request from local pool (or
		 * this is a duplicate request that we filled already).
		 * Enqueue response work item with new label.
		 */
		struct lp_cbq_item *q;

		q = XCALLOC(MTYPE_BGP_LABEL_CBQ, sizeof(struct lp_cbq_item));

		q->cbfunc = lcb->cbfunc;
		q->type = lcb->type;
		q->label = lcb->label;
		q->labelid = lcb->labelid;
		q->vrf_id = lcb->vrf_id;
		q->allocated = true;

		/* if this is a LU request, lock node before queueing */
		check_bgp_lu_cb_lock(lcb);

		work_queue_add(lp->callback_q, q);

		return;
	}

	if (requested)
		return;

	if (debug)
		zlog_debug("%s: slow path. lcb=%p label=%u",
			__func__, lcb, lcb->label);

	/*
	 * Slow path: we are out of labels in the local pool,
	 * so remember the request and also get another chunk from
	 * the label manager.
	 *
	 * We track number of outstanding label requests: don't
	 * need to get a chunk for each one.
	 */

	struct lp_fifo *lf = XCALLOC(MTYPE_BGP_LABEL_FIFO,
		sizeof(struct lp_fifo));

	lf->lcb = *lcb;
	/* if this is a LU request, lock node before queueing */
	check_bgp_lu_cb_lock(lcb);

	lp_fifo_add_tail(&lp->requests, lf);

	if (lp_fifo_count(&lp->requests) > lp->pending_count) {
		if (!bgp_zebra_request_label_range(MPLS_LABEL_BASE_ANY,
						   lp->next_chunksize, true))
			return;

		lp->pending_count += lp->next_chunksize;
		if ((lp->next_chunksize << 1) <= LP_CHUNK_SIZE_MAX)
			lp->next_chunksize <<= 1;
	}

	event_add_timer(bm->master, bgp_sync_label_manager, NULL, 1,
			&bm->t_bgp_sync_label_manager);
}

/* Label release logic - releases label from skiplists and chunk bitfield */
/* 【函数】bgp_lp_release —— 标签释放的公共实现，带多重校验以防误释放：
 *   1) 在 inuse 里查该标签，且必须匹配 labelid（同一标签不能被别人释放）；
 *   2) 在 ledger 里查该 labelid 的 lcb；普通释放(check_type=true)时还要求 label 与
 *      type 都与 lcb 记录一致，否则直接返回、不释放；
 *   3) 通过后从 ledger / inuse 摘除，并在所属 chunk 的位图里清位、nfree++；
 *   4) 若整段 chunk 已全空、且 requests FIFO 也空了，就把这段 chunk 归还 zebra、
 *      从 chunks 删除并释放内存，同时把 next_chunksize 复位为最小值。
 * 注意：释放后不会主动去满足 FIFO 里等待的请求（留待下次 sync）。 */
static void bgp_lp_release(mpls_label_t label, void *labelid, int type, bool check_type,
			   bool debug_enabled)
{
	struct listnode *node;
	struct lp_chunk *chunk;
	uintptr_t lbl = label;
	void *found_labelid;
	struct lp_lcb *lcb;
	bool deallocated = false;
	bool found = false;

	/*
	 * Find the label in the skiplists and validate it matches
	 */
	if (!skiplist_search(lp->inuse, (void *)lbl, &found_labelid) && labelid == found_labelid) {
		if (!skiplist_search(lp->ledger, labelid, (void **)&lcb)) {
			/* Additional validation for normal release */
			if (check_type && (label != lcb->label || type != lcb->type))
				return;

			if (label == lcb->label) {
				skiplist_delete(lp->ledger, labelid, NULL);
				found = true;
			}
		}
		skiplist_delete(lp->inuse, (void *)lbl, NULL);

		/*
		 * Find the chunk this label belongs to and
		 * deallocate the label in the chunk's bitfield
		 */
		for (ALL_LIST_ELEMENTS_RO(lp->chunks, node, chunk)) {
			uint32_t index;

			if ((label < chunk->first) || (label > chunk->last))
				continue;

			index = label - chunk->first;
			if (bf_test_index(chunk->allocated_map, index)) {
				bf_release_index(chunk->allocated_map, index);
				chunk->nfree += 1;
				deallocated = true;
				if (debug_enabled)
					zlog_debug("%s: released label %u from chunk, nfree now %u",
						   __func__, label, chunk->nfree);
			}
			break;
		}

		if (!deallocated && debug_enabled) {
			zlog_debug("%s: warning: could not find chunk for label %u", __func__,
				   label);
		}

		/*
		 * Handle chunk deletion for normal release
		 */
		if (found && deallocated && check_type &&
		    chunk->nfree == chunk->last - chunk->first + 1 &&
		    lp_fifo_count(&lp->requests) == 0) {
			bgp_zebra_release_label_range(chunk->first, chunk->last);
			list_delete_node(lp->chunks, node);
			lp_chunk_free(chunk);
			lp->next_chunksize = LP_CHUNK_SIZE_MIN;
		}
	}
}

/*
 * Public typed release api
 */
/* 【函数】typed release 包装 —— 把“释放”按用途固化 type，并顺手清理请求者状态：
 *   bgp_lu_lp_release     : BGP-LU，释放后清 dest 的 local_label 有效位。
 *   bgp_nh_lp_release     : 每下一跳标签(内部使用)。
 *   bgp_vpn_lp_release    : VRF 出标签，释放后把 policy->tovpn_label 置 NONE。
 *   bgp_vpn_nh_lp_release : L3VPN 每下一跳绑定标签。
 * 都调用 bgp_lp_release(...) 且 check_type=true，确保类型匹配。 */
void bgp_lu_lp_release(struct bgp_dest *dest, mpls_label_t label)
{
	bgp_lp_release(label, dest, LP_TYPE_BGP_LU, true, false);
	bgp_unset_valid_label(&dest->local_label);
}

/*
 * Typed release api
 */
static void bgp_nh_lp_release(struct bgp_label_per_nexthop_cache *blnc,
			      mpls_label_t label)
{
	bgp_lp_release(label, blnc, LP_TYPE_NEXTHOP, true, false);
}

/*
 * Public typed release api
 */
void bgp_vpn_lp_release(struct vpn_policy *policy, mpls_label_t label)
{
	bgp_lp_release(label, policy, LP_TYPE_VRF, true, false);
	policy->tovpn_label = MPLS_LABEL_NONE;
}

/*
 * Public typed release api
 */
void bgp_vpn_nh_lp_release(struct bgp_mplsvpn_nh_label_bind_cache *bmnc,
			   mpls_label_t label)
{
	bgp_lp_release(label, bmnc, LP_TYPE_BGP_L3VPN_BIND, true, false);
}

/* 【函数】bgp_sync_label_manager —— 定时器驱动：把 requests FIFO 里排队的请求尽量用本地
 * 池满足。逐个取出请求处理：
 *   - ledger 里已查不到 → 请求已失效，跳过（顺带给 LU 解锁）；
 *   - lcb->label 已非 NONE → 已经分到标签，跳过（顺带给 LU 解锁）；
 *   - 尝试 get_label_from_pool：分到就投递回调；分不到说明本池又空了，就把该请求放回
 *     FIFO 尾部、重挂定时器并跳出（等 zebra 给新 chunk）。
 * 本质是“FIFO 请求 × 本地 chunk 空闲位”的分配循环。 */
static void bgp_sync_label_manager(struct event *e)
{
	int debug = BGP_DEBUG(labelpool, LABELPOOL);
	struct lp_fifo *lf;

	while ((lf = lp_fifo_pop(&lp->requests))) {
		struct lp_lcb *lcb;
		void *labelid = lf->lcb.labelid;

		if (skiplist_search(lp->ledger, labelid, (void **)&lcb)) {
			/* request no longer in effect */

			if (debug) {
				zlog_debug("%s: labelid %p: request no longer in effect",
					   __func__, labelid);
			}
			/* if this was a BGP_LU request, unlock node
			 */
			check_bgp_lu_cb_unlock(lcb);
			goto finishedrequest;
		}

		/* have LCB */
		if (lcb->label != MPLS_LABEL_NONE) {
			/* request already has a label */
			if (debug) {
				zlog_debug("%s: labelid %p: request already has a label: %u=0x%x, lcb=%p",
					   __func__, labelid, lcb->label,
					   lcb->label, lcb);
			}
			/* if this was a BGP_LU request, unlock node
			 */
			check_bgp_lu_cb_unlock(lcb);

			goto finishedrequest;
		}

		lcb->label = get_label_from_pool(lcb->labelid);

		if (lcb->label == MPLS_LABEL_NONE) {
			/*
			 * Out of labels in local pool, await next chunk
			 */
			if (debug) {
				zlog_debug("%s: out of labels, await more",
					   __func__);
			}

			lp_fifo_add_tail(&lp->requests, lf);
			event_add_timer(bm->master, bgp_sync_label_manager,
					NULL, 1, &bm->t_bgp_sync_label_manager);
			break;
		}

		/*
		 * we filled the request from local pool.
		 * Enqueue response work item with new label.
		 */
		struct lp_cbq_item *q = XCALLOC(MTYPE_BGP_LABEL_CBQ,
						sizeof(struct lp_cbq_item));

		q->cbfunc = lcb->cbfunc;
		q->type = lcb->type;
		q->label = lcb->label;
		q->labelid = lcb->labelid;
		q->vrf_id = lcb->vrf_id;
		q->allocated = true;

		if (debug)
			zlog_debug("%s: assigning label %u to labelid %p",
				   __func__, q->label, q->labelid);

		work_queue_add(lp->callback_q, q);

finishedrequest:
		XFREE(MTYPE_BGP_LABEL_FIFO, lf);
	}
}

/* 【函数】bgp_lp_event_chunk —— zebra 授予了一段连续标签 [first,last]。把它做成一个
 * lp_chunk：建位图、nfree=整段长度，并**插入 chunks 链表头部**（新段通常更大，放头部
 * 让它先被扫描，提高分配命中率），最后从 pending_count 里扣掉这段长度。
 * 若 first>last（非法）则记错误返回。 */
void bgp_lp_event_chunk(uint32_t first, uint32_t last)
{
	struct lp_chunk *chunk;
	uint32_t labelcount;

	if (last < first) {
		flog_err(EC_BGP_LABEL,
			 "%s: zebra label chunk invalid: first=%u, last=%u",
			 __func__, first, last);
		return;
	}

	chunk = XCALLOC(MTYPE_BGP_LABEL_CHUNK, sizeof(struct lp_chunk));

	labelcount = last - first + 1;

	chunk->first = first;
	chunk->last = last;
	chunk->nfree = labelcount;
	bf_init(chunk->allocated_map, labelcount);

	/*
	 * Optimize for allocation by adding the new (presumably larger)
	 * chunk at the head of the list so it is examined first.
	 */
	listnode_add_head(lp->chunks, chunk);

	lp->pending_count -= labelcount;
}

/*
 * continue using allocated labels until zebra returns
 */
/* 【函数】bgp_lp_event_zebra_down —— 与 zebra 断连。策略是“继续使用已分配的标签”，
 * 所以这里是空实现(rats)；真正的作废与重分配推迟到重连(bgp_lp_event_zebra_up)时做。 */
void bgp_lp_event_zebra_down(void)
{
	/* rats. */
}

/*
 * Inform owners of previously-allocated labels that their labels
 * are not valid. Request chunk from zebra large enough to satisfy
 * previously-allocated labels plus any outstanding requests.
 */
/* 【函数】bgp_lp_event_zebra_up —— 与 zebra 重连后的重建流程：
 *   1) reconnect_count++；
 *   2) 估算需要的标签数 = FIFO 中请求数 + 当前 inuse 数；按需抬高 next_chunksize，
 *      向上取整成整段后向 zebra 一次性申请；
 *   3) 作废当前所有 chunk（旧标签不再有效）；
 *   4) 遍历 inuse 里每个标签：给请求者发一个 allocated=false 的失效回调，把 lcb->label
 *      清成 NONE，并把该请求重新放回 requests FIFO（等重新分配）；随后清空 inuse；
 *   5) 挂定时器稍后 bgp_sync_label_manager 做实际分配。
 * 这样请求者无需自己重发申请，重连后会自动先收到“失效”、再收到“新标签”回调。 */
void bgp_lp_event_zebra_up(void)
{
	unsigned int labels_needed;
	unsigned int chunks_needed;
	void *labelid;
	struct lp_lcb *lcb;

	lp->reconnect_count++;
	/*
	 * Get label chunk allocation request dispatched to zebra
	 */
	labels_needed = lp_fifo_count(&lp->requests) +
		skiplist_count(lp->inuse);

	if (labels_needed > lp->next_chunksize) {
		while ((lp->next_chunksize < labels_needed) &&
		       (lp->next_chunksize << 1 <= LP_CHUNK_SIZE_MAX))

			lp->next_chunksize <<= 1;
	}

	/* round up */
	chunks_needed = (labels_needed + lp->next_chunksize - 1) / lp->next_chunksize;
	labels_needed = chunks_needed * lp->next_chunksize;

	/*
	 * Invalidate current list of chunks
	 */
	list_delete_all_node(lp->chunks);

	if (labels_needed && !bgp_zebra_request_label_range(MPLS_LABEL_BASE_ANY,
							    labels_needed, true))
		return;
	lp->pending_count += labels_needed;

	/*
	 * Invalidate any existing labels and requeue them as requests
	 */
	while (!skiplist_first(lp->inuse, NULL, &labelid)) {

		/*
		 * Get LCB
		 */
		if (!skiplist_search(lp->ledger, labelid, (void **)&lcb)) {

			if (lcb->label != MPLS_LABEL_NONE) {
				/*
				 * invalidate
				 */
				struct lp_cbq_item *q;

				q = XCALLOC(MTYPE_BGP_LABEL_CBQ,
					sizeof(struct lp_cbq_item));
				q->cbfunc = lcb->cbfunc;
				q->type = lcb->type;
				q->label = lcb->label;
				q->labelid = lcb->labelid;
				q->vrf_id = lcb->vrf_id;
				q->allocated = false;
				check_bgp_lu_cb_lock(lcb);
				work_queue_add(lp->callback_q, q);

				lcb->label = MPLS_LABEL_NONE;
			}

			/*
			 * request queue
			 */
			struct lp_fifo *lf = XCALLOC(MTYPE_BGP_LABEL_FIFO,
				sizeof(struct lp_fifo));

			lf->lcb = *lcb;
			check_bgp_lu_cb_lock(lcb);
			lp_fifo_add_tail(&lp->requests, lf);
		}

		skiplist_delete_first(lp->inuse);
	}

	event_add_timer(bm->master, bgp_sync_label_manager, NULL, 1,
			&bm->t_bgp_sync_label_manager);
}

/* 【命令】show bgp labelpool summary —— 打印台账概况：ledger / inuse / requests 各自
 * 条目数、chunk 数、pending、重连次数；支持 json 输出。 */
DEFUN(show_bgp_labelpool_summary, show_bgp_labelpool_summary_cmd,
      "show bgp labelpool summary [json]",
      SHOW_STR BGP_STR
      "BGP Labelpool information\n"
      "BGP Labelpool summary\n" JSON_STR)
{
	bool uj = use_json(argc, argv);
	json_object *json = NULL;

	if (!lp) {
		if (uj)
			vty_out(vty, "{}\n");
		else
			vty_out(vty, "No existing BGP labelpool\n");
		return (CMD_WARNING);
	}

	if (uj) {
		json = json_object_new_object();
		json_object_int_add(json, "ledger", skiplist_count(lp->ledger));
		json_object_int_add(json, "inUse", skiplist_count(lp->inuse));
		json_object_int_add(json, "requests",
				    lp_fifo_count(&lp->requests));
		json_object_int_add(json, "labelChunks", listcount(lp->chunks));
		json_object_int_add(json, "pending", lp->pending_count);
		json_object_int_add(json, "reconnects", lp->reconnect_count);
		vty_json(vty, json);
	} else {
		vty_out(vty, "Labelpool Summary\n");
		vty_out(vty, "-----------------\n");
		vty_out(vty, "%-13s %d\n",
			"Ledger:", skiplist_count(lp->ledger));
		vty_out(vty, "%-13s %d\n", "InUse:", skiplist_count(lp->inuse));
		vty_out(vty, "%-13s %zu\n",
			"Requests:", lp_fifo_count(&lp->requests));
		vty_out(vty, "%-13s %d\n",
			"LabelChunks:", listcount(lp->chunks));
		vty_out(vty, "%-13s %d\n", "Pending:", lp->pending_count);
		vty_out(vty, "%-13s %d\n", "Reconnects:", lp->reconnect_count);
	}
	return CMD_SUCCESS;
}

/* 【命令】show bgp labelpool ledger —— 遍历 ledger，按 type 打印每个请求对应的前缀与
 * 标签；BGP-LU 请求若已不再标记为 BGP_NODE_LABEL_REQUESTED 则显示 INVALID。支持 json。 */
DEFUN(show_bgp_labelpool_ledger, show_bgp_labelpool_ledger_cmd,
      "show bgp labelpool ledger [json]",
      SHOW_STR BGP_STR
      "BGP Labelpool information\n"
      "BGP Labelpool ledger\n" JSON_STR)
{
	bool uj = use_json(argc, argv);
	json_object *json = NULL, *json_elem = NULL;
	struct lp_lcb *lcb = NULL;
	struct bgp_dest *dest;
	void *cursor = NULL;
	const struct prefix *p;
	int rc, count;

	if (!lp) {
		if (uj)
			vty_out(vty, "{}\n");
		else
			vty_out(vty, "No existing BGP labelpool\n");
		return (CMD_WARNING);
	}

	if (uj) {
		count = skiplist_count(lp->ledger);
		if (!count) {
			vty_out(vty, "{}\n");
			return CMD_SUCCESS;
		}
		json = json_object_new_array();
	} else {
		vty_out(vty, "Prefix                Label\n");
		vty_out(vty, "---------------------------\n");
	}

	for (rc = skiplist_next(lp->ledger, (void **)&dest, (void **)&lcb,
				&cursor);
	     !rc; rc = skiplist_next(lp->ledger, (void **)&dest, (void **)&lcb,
				     &cursor)) {
		if (uj) {
			json_elem = json_object_new_object();
			json_object_array_add(json, json_elem);
		}
		switch (lcb->type) {
		case LP_TYPE_BGP_LU:
			if (!CHECK_FLAG(dest->flags, BGP_NODE_LABEL_REQUESTED))
				if (uj) {
					json_object_string_add(
						json_elem, "prefix", "INVALID");
					json_object_int_add(json_elem, "label",
							    lcb->label);
				} else
					vty_out(vty, "%-18s         %u\n",
						"INVALID", lcb->label);
			else {
				p = bgp_dest_get_prefix(dest);
				if (uj) {
					json_object_string_addf(
						json_elem, "prefix", "%pFX", p);
					json_object_int_add(json_elem, "label",
							    lcb->label);
				} else
					vty_out(vty, "%-18pFX    %u\n", p,
						lcb->label);
			}
			break;
		case LP_TYPE_VRF:
			if (uj) {
				json_object_string_add(json_elem, "prefix",
						       "VRF");
				json_object_int_add(json_elem, "label",
						    lcb->label);
			} else
				vty_out(vty, "%-18s         %u\n", "VRF",
					lcb->label);

			break;
		case LP_TYPE_NEXTHOP:
			if (uj) {
				json_object_string_add(json_elem, "prefix",
						       "nexthop");
				json_object_int_add(json_elem, "label",
						    lcb->label);
			} else
				vty_out(vty, "%-18s         %u\n", "nexthop",
					lcb->label);
			break;
		case LP_TYPE_BGP_L3VPN_BIND:
			if (uj) {
				json_object_string_add(json_elem, "prefix",
						       "l3vpn-bind");
				json_object_int_add(json_elem, "label",
						    lcb->label);
			} else
				vty_out(vty, "%-18s         %u\n", "l3vpn-bind",
					lcb->label);
			break;
		}
	}
	if (uj)
		vty_json(vty, json);
	return CMD_SUCCESS;
}

/* 【命令】show bgp labelpool inuse —— 遍历 inuse（已分配标签），反查 ledger 得到 type，
 * 打印前缀 / 标签。支持 json。
 * 疑点（仅标注，未改动）：本命令开头有两处重复的 “if (!lp)” 检查（其一冗余）；且遍历
 * 循环的“推进”用的是 lp->ledger 而非 lp->inuse（见下方 for 的第三段），两跳表键不同，
 * 疑似笔误，实际行为存疑。 */
DEFUN(show_bgp_labelpool_inuse, show_bgp_labelpool_inuse_cmd,
      "show bgp labelpool inuse [json]",
      SHOW_STR BGP_STR
      "BGP Labelpool information\n"
      "BGP Labelpool inuse\n" JSON_STR)
{
	bool uj = use_json(argc, argv);
	json_object *json = NULL, *json_elem = NULL;
	struct bgp_dest *dest;
	mpls_label_t label;
	struct lp_lcb *lcb;
	void *cursor = NULL;
	const struct prefix *p;
	int rc, count;

	if (!lp) {
		vty_out(vty, "No existing BGP labelpool\n");
		return (CMD_WARNING);
	}
	if (!lp) {
		if (uj)
			vty_out(vty, "{}\n");
		else
			vty_out(vty, "No existing BGP labelpool\n");
		return (CMD_WARNING);
	}

	if (uj) {
		count = skiplist_count(lp->inuse);
		if (!count) {
			vty_out(vty, "{}\n");
			return CMD_SUCCESS;
		}
		json = json_object_new_array();
	} else {
		vty_out(vty, "Prefix                Label\n");
		vty_out(vty, "---------------------------\n");
	}
	for (rc = skiplist_next(lp->inuse, (void **)&label, (void **)&dest,
				&cursor);
	     !rc; rc = skiplist_next(lp->ledger, (void **)&label,
				     (void **)&dest, &cursor)) {
		if (skiplist_search(lp->ledger, dest, (void **)&lcb))
			continue;

		if (uj) {
			json_elem = json_object_new_object();
			json_object_array_add(json, json_elem);
		}

		switch (lcb->type) {
		case LP_TYPE_BGP_LU:
			if (!CHECK_FLAG(dest->flags, BGP_NODE_LABEL_REQUESTED))
				if (uj) {
					json_object_string_add(
						json_elem, "prefix", "INVALID");
					json_object_int_add(json_elem, "label",
							    label);
				} else
					vty_out(vty, "INVALID         %u\n",
						label);
			else {
				p = bgp_dest_get_prefix(dest);
				if (uj) {
					json_object_string_addf(
						json_elem, "prefix", "%pFX", p);
					json_object_int_add(json_elem, "label",
							    label);
				} else
					vty_out(vty, "%-18pFX    %u\n", p,
						label);
			}
			break;
		case LP_TYPE_VRF:
			if (uj) {
				json_object_string_add(json_elem, "prefix",
						       "VRF");
				json_object_int_add(json_elem, "label", label);
			} else
				vty_out(vty, "%-18s         %u\n", "VRF",
					label);
			break;
		case LP_TYPE_NEXTHOP:
			if (uj) {
				json_object_string_add(json_elem, "prefix",
						       "nexthop");
				json_object_int_add(json_elem, "label", label);
			} else
				vty_out(vty, "%-18s         %u\n", "nexthop",
					label);
			break;
		case LP_TYPE_BGP_L3VPN_BIND:
			if (uj) {
				json_object_string_add(json_elem, "prefix",
						       "l3vpn-bind");
				json_object_int_add(json_elem, "label", label);
			} else
				vty_out(vty, "%-18s         %u\n", "l3vpn-bind",
					label);
			break;
		}
	}
	if (uj)
		vty_json(vty, json);
	return CMD_SUCCESS;
}

/* 【命令】show bgp labelpool requests —— 遍历 requests FIFO，打印每个等待分配请求的
 * 前缀 / 类型。用 lp_fifo_next_safe 先取下一个再处理，防止遍历中被改动。支持 json。 */
DEFUN(show_bgp_labelpool_requests, show_bgp_labelpool_requests_cmd,
      "show bgp labelpool requests [json]",
      SHOW_STR BGP_STR
      "BGP Labelpool information\n"
      "BGP Labelpool requests\n" JSON_STR)
{
	bool uj = use_json(argc, argv);
	json_object *json = NULL, *json_elem = NULL;
	struct bgp_dest *dest;
	const struct prefix *p;
	struct lp_fifo *item, *next;
	int count;

	if (!lp) {
		if (uj)
			vty_out(vty, "{}\n");
		else
			vty_out(vty, "No existing BGP labelpool\n");
		return (CMD_WARNING);
	}

	if (uj) {
		count = lp_fifo_count(&lp->requests);
		if (!count) {
			vty_out(vty, "{}\n");
			return CMD_SUCCESS;
		}
		json = json_object_new_array();
	} else {
		vty_out(vty, "Prefix         \n");
		vty_out(vty, "----------------\n");
	}

	for (item = lp_fifo_first(&lp->requests); item; item = next) {
		next = lp_fifo_next_safe(&lp->requests, item);
		dest = item->lcb.labelid;
		if (uj) {
			json_elem = json_object_new_object();
			json_object_array_add(json, json_elem);
		}
		switch (item->lcb.type) {
		case LP_TYPE_BGP_LU:
			if (!CHECK_FLAG(dest->flags,
					BGP_NODE_LABEL_REQUESTED)) {
				if (uj)
					json_object_string_add(
						json_elem, "prefix", "INVALID");
				else
					vty_out(vty, "INVALID\n");
			} else {
				p = bgp_dest_get_prefix(dest);
				if (uj)
					json_object_string_addf(
						json_elem, "prefix", "%pFX", p);
				else
					vty_out(vty, "%-18pFX\n", p);
			}
			break;
		case LP_TYPE_VRF:
			if (uj)
				json_object_string_add(json_elem, "prefix",
						       "VRF");
			else
				vty_out(vty, "VRF\n");
			break;
		case LP_TYPE_NEXTHOP:
			if (uj)
				json_object_string_add(json_elem, "prefix",
						       "nexthop");
			else
				vty_out(vty, "Nexthop\n");
			break;
		case LP_TYPE_BGP_L3VPN_BIND:
			if (uj)
				json_object_string_add(json_elem, "prefix",
						       "l3vpn-bind");
			else
				vty_out(vty, "L3VPN-BIND\n");
			break;
		}
	}
	if (uj)
		vty_json(vty, json);
	return CMD_SUCCESS;
}

/* 【命令】show bgp labelpool chunks —— 打印每段 chunk 的 first / last / size / nfree。支持 json。 */
DEFUN(show_bgp_labelpool_chunks, show_bgp_labelpool_chunks_cmd,
      "show bgp labelpool chunks [json]",
      SHOW_STR BGP_STR
      "BGP Labelpool information\n"
      "BGP Labelpool chunks\n" JSON_STR)
{
	bool uj = use_json(argc, argv);
	json_object *json = NULL, *json_elem;
	struct listnode *node;
	struct lp_chunk *chunk;
	int count;

	if (!lp) {
		if (uj)
			vty_out(vty, "{}\n");
		else
			vty_out(vty, "No existing BGP labelpool\n");
		return (CMD_WARNING);
	}

	if (uj) {
		count = listcount(lp->chunks);
		if (!count) {
			vty_out(vty, "{}\n");
			return CMD_SUCCESS;
		}
		json = json_object_new_array();
	} else {
		vty_out(vty, "%10s %10s %10s %10s\n", "First", "Last", "Size",
			"nfree");
		vty_out(vty, "-------------------------------------------\n");
	}

	for (ALL_LIST_ELEMENTS_RO(lp->chunks, node, chunk)) {
		uint32_t size;

		size = chunk->last - chunk->first + 1;

		if (uj) {
			json_elem = json_object_new_object();
			json_object_array_add(json, json_elem);
			json_object_int_add(json_elem, "first", chunk->first);
			json_object_int_add(json_elem, "last", chunk->last);
			json_object_int_add(json_elem, "size", size);
			json_object_int_add(json_elem, "numberFree",
					    chunk->nfree);
		} else
			vty_out(vty, "%10u %10u %10u %10u\n", chunk->first,
				chunk->last, size, chunk->nfree);
	}
	if (uj)
		vty_json(vty, json);
	return CMD_SUCCESS;
}

/* 【函数】show_bgp_nexthop_label_afi —— 打印某 AFI 下“每下一跳标签缓存”树的内容：
 * 每个下一跳的标签、引用路径数(path_count)、出接口、上次更新时间；detail 时再逐条列出
 * 使用它的路径（含 RD / flags）。 */
static void show_bgp_nexthop_label_afi(struct vty *vty, afi_t afi,
				       struct bgp *bgp, bool detail)
{
	struct bgp_label_per_nexthop_cache_head *tree;
	struct bgp_label_per_nexthop_cache *iter;
	safi_t safi;
	void *src;
	char buf[PREFIX2STR_BUFFER];
	char labelstr[MPLS_LABEL_STRLEN];
	struct bgp_dest *dest;
	struct bgp_path_info *path;
	struct bgp *bgp_path;
	struct bgp_table *table;

	vty_out(vty, "Current BGP label nexthop cache for %s, VRF %s\n",
		afi2str(afi), bgp->name_pretty);

	tree = &bgp->mpls_labels_per_nexthop[afi];
	frr_each (bgp_label_per_nexthop_cache, tree, iter) {
		if (afi2family(afi) == AF_INET)
			src = (void *)&iter->nexthop.u.prefix4;
		else
			src = (void *)&iter->nexthop.u.prefix6;

		vty_out(vty, " %s, label %s #paths %u\n",
			inet_ntop(afi2family(afi), src, buf, sizeof(buf)),
			mpls_label2str(1, &iter->label, labelstr,
				       sizeof(labelstr), 0, true),
			iter->path_count);
		if (iter->nh)
			vty_out(vty, "  if %s\n",
				ifindex2ifname(iter->nh->ifindex,
					       iter->nh->vrf_id));
		vty_out(vty, "  Last update: %s", time_to_string(iter->last_update, buf));
		if (!detail)
			continue;
		vty_out(vty, "  Paths:\n");
		LIST_FOREACH (path, &(iter->paths),
			      mplsvpn.blnc.label_nh_thread) {
			dest = path->net;
			table = bgp_dest_table(dest);
			assert(dest && table);
			afi = family2afi(bgp_dest_get_prefix(dest)->family);
			safi = table->safi;
			bgp_path = table->bgp;

			if (dest->pdest) {
				vty_out(vty, "    %d/%d %pBD RD ", afi, safi,
					dest);

				vty_out(vty, BGP_RD_AS_FORMAT(bgp->asnotation),
					(struct prefix_rd *)bgp_dest_get_prefix(
						dest->pdest));
				vty_out(vty, " %s flags 0x%x\n",
					bgp_path->name_pretty, path->flags);
			} else
				vty_out(vty, "    %d/%d %pBD %s flags 0x%x\n",
					afi, safi, dest, bgp_path->name_pretty,
					path->flags);
		}
	}
}

/* 【命令】show bgp [<view|vrf> VIEWVRFNAME] label-nexthop [detail] —— 按 VRF（缺省用默认
 * 实例）分别对 AFI_IP / AFI_IP6 调用上面的打印函数。 */
DEFPY(show_bgp_nexthop_label, show_bgp_nexthop_label_cmd,
      "show bgp [<view|vrf> VIEWVRFNAME] label-nexthop [detail]",
      SHOW_STR BGP_STR BGP_INSTANCE_HELP_STR
      "BGP label per-nexthop table\n"
      "Show detailed information\n")
{
	int idx = 0;
	char *vrf = NULL;
	struct bgp *bgp;
	bool detail = false;
	int afi;

	if (argv_find(argv, argc, "vrf", &idx)) {
		vrf = argv[++idx]->arg;
		bgp = bgp_lookup_by_name(vrf);
	} else
		bgp = bgp_get_default();

	if (!bgp)
		return CMD_SUCCESS;

	if (argv_find(argv, argc, "detail", &idx))
		detail = true;

	for (afi = AFI_IP; afi <= AFI_IP6; afi++)
		show_bgp_nexthop_label_afi(vty, afi, bgp, detail);
	return CMD_SUCCESS;
}

#if BGP_LABELPOOL_ENABLE_TESTS
/*------------------------------------------------------------------------
 *			Testing code start
 *------------------------------------------------------------------------*/

/* 【测试区开始】以下代码由 BGP_LABELPOOL_ENABLE_TESTS 控制，默认关闭（宏为 0），生产
 * 构建不会包含。用途是压测标签池的分配 / 释放：批量申请标签、记录时间戳，统计吞吐。
 * 无相关需求可直接略读，跳到 bgp_lp_vty_init。 */
DEFINE_MTYPE_STATIC(BGPD, LABELPOOL_TEST, "Label pool test");

#define LPT_STAT_INSERT_FAIL 0
#define LPT_STAT_DELETE_FAIL 1
#define LPT_STAT_ALLOCATED 2
#define LPT_STAT_DEALLOCATED 3
#define LPT_STAT_MAX 4

const char *lpt_counter_names[] = {
	"sl insert failures",
	"sl delete failures",
	"labels allocated",
	"labels deallocated",
};

static uint8_t lpt_generation;
static bool lpt_inprogress;
static struct skiplist *lp_tests;
static unsigned int lpt_test_cb_tcb_lookup_fails;
static unsigned int lpt_release_tcb_lookup_fails;
static unsigned int lpt_test_event_tcb_lookup_fails;
static unsigned int lpt_stop_tcb_lookup_fails;

struct lp_test {
	uint8_t generation;
	unsigned int request_maximum;
	unsigned int request_blocksize;
	uintptr_t request_count; /* match type of labelid */
	int label_type;
	struct skiplist *labels;
	struct timeval starttime;
	struct skiplist *timestamps_alloc;
	struct skiplist *timestamps_dealloc;
	struct event *event_thread;
	unsigned int counter[LPT_STAT_MAX];
};

/* test parameters */
#define LPT_MAX_COUNT 500000  /* get this many labels in all */
#define LPT_BLKSIZE 10000     /* this many at a time, then yield */
#define LPT_TS_INTERVAL 10000 /* timestamp every this many labels */


/* 【测试】test_cb —— 压测用回调：allocated 时把 labelid → label 记入 tcb->labels，并按
 * 间隔打时间戳；失效(allocated=false)时从记录中删除。labelid 高位是 generation，用于
 * 找到对应的测试。 */
static int test_cb(mpls_label_t label, void *labelid, bool allocated)
{
	uintptr_t generation;
	struct lp_test *tcb;

	generation = ((uintptr_t)labelid >> 24) & 0xff;

	if (skiplist_search(lp_tests, (void *)generation, (void **)&tcb)) {

		/* couldn't find current test in progress */
		++lpt_test_cb_tcb_lookup_fails;
		return -1; /* reject allocation */
	}

	if (allocated) {
		++tcb->counter[LPT_STAT_ALLOCATED];
		if (!(tcb->counter[LPT_STAT_ALLOCATED] % LPT_TS_INTERVAL)) {
			uintptr_t time_ms;

			time_ms = monotime_since(&tcb->starttime, NULL) / 1000;
			skiplist_insert(tcb->timestamps_alloc,
					(void *)(uintptr_t)tcb
						->counter[LPT_STAT_ALLOCATED],
					(void *)time_ms);
		}
		if (skiplist_insert(tcb->labels, labelid,
				    (void *)(uintptr_t)label)) {
			++tcb->counter[LPT_STAT_INSERT_FAIL];
			return -1;
		}
	} else {
		++tcb->counter[LPT_STAT_DEALLOCATED];
		if (!(tcb->counter[LPT_STAT_DEALLOCATED] % LPT_TS_INTERVAL)) {
			uintptr_t time_ms;

			time_ms = monotime_since(&tcb->starttime, NULL) / 1000;
			skiplist_insert(tcb->timestamps_dealloc,
					(void *)(uintptr_t)tcb
						->counter[LPT_STAT_ALLOCATED],
					(void *)time_ms);
		}
		if (skiplist_delete(tcb->labels, labelid, 0)) {
			++tcb->counter[LPT_STAT_DELETE_FAIL];
			return -1;
		}
	}
	return 0;
}

/* 【测试】labelpool_test_event_handler —— 分批发起标签申请(每轮 request_blocksize 个)，
 * 直到达到 request_maximum；构造 32 位 labelid = generation<<24 | count。 */
static void labelpool_test_event_handler(struct event *event)
{
	struct lp_test *tcb;

	if (skiplist_search(lp_tests, (void *)(uintptr_t)(lpt_generation),
			    (void **)&tcb)) {

		/* couldn't find current test in progress */
		++lpt_test_event_tcb_lookup_fails;
		return;
	}

	/*
	 * request a bunch of labels
	 */
	for (unsigned int i = 0; (i < tcb->request_blocksize) &&
				 (tcb->request_count < tcb->request_maximum);
	     ++i) {

		uintptr_t id;

		++tcb->request_count;

		/*
		 * construct 32-bit id from request_count and generation
		 */
		id = ((uintptr_t)tcb->generation << 24) |
		     (tcb->request_count & 0x00ffffff);
		bgp_lp_get(LP_TYPE_VRF, (void *)id, test_cb);
	}

	if (tcb->request_count < tcb->request_maximum)
		thread_add_event(bm->master, labelpool_test_event_handler, NULL,
				 0, &tcb->event_thread);
}

/* 【测试】lptest_stop —— 停止当前压测：取消其事件线程并清 lpt_inprogress。 */
static void lptest_stop(void)
{
	struct lp_test *tcb;

	if (!lpt_inprogress)
		return;

	if (skiplist_search(lp_tests, (void *)(uintptr_t)(lpt_generation),
			    (void **)&tcb)) {

		/* couldn't find current test in progress */
		++lpt_stop_tcb_lookup_fails;
		return;
	}

	if (tcb->event_thread)
		event_cancel(&tcb->event_thread);

	lpt_inprogress = false;
}

/* 【测试】lptest_start —— 启动一次压测：检查是否有测试在跑、generation 位宽是否够，
 * 然后建 tcb 并投递首轮事件。 */
static int lptest_start(struct vty *vty)
{
	struct lp_test *tcb;

	if (lpt_inprogress) {
		vty_out(vty, "test already in progress\n");
		return -1;
	}

	if (skiplist_count(lp_tests) >=
	    (1 << (8 * sizeof(lpt_generation))) - 1) {
		/*
		 * Too many test runs
		 */
		vty_out(vty, "too many tests: clear first\n");
		return -1;
	}

	/*
	 * We pack the generation and request number into the labelid;
	 * make sure they fit.
	 */
	unsigned int n1 = LPT_MAX_COUNT;
	unsigned int sh = 0;
	unsigned int label_bits;

	label_bits = 8 * (sizeof(tcb->request_count) - sizeof(lpt_generation));

	/* n1 should be same type as tcb->request_maximum */
	assert(sizeof(n1) == sizeof(tcb->request_maximum));

	while (n1 >>= 1)
		++sh;
	sh += 1; /* number of bits needed to hold LPT_MAX_COUNT */

	if (sh > label_bits) {
		vty_out(vty,
			"Sorry, test iteration count too big on this platform (LPT_MAX_COUNT %u, need %u bits, but label_bits is only %u)\n",
			LPT_MAX_COUNT, sh, label_bits);
		return -1;
	}

	lpt_inprogress = true;
	++lpt_generation;

	tcb = XCALLOC(MTYPE_LABELPOOL_TEST, sizeof(*tcb));

	tcb->generation = lpt_generation;
	tcb->label_type = LP_TYPE_VRF;
	tcb->request_maximum = LPT_MAX_COUNT;
	tcb->request_blocksize = LPT_BLKSIZE;
	tcb->labels = skiplist_new(0, NULL, NULL);
	tcb->timestamps_alloc = skiplist_new(0, NULL, NULL);
	tcb->timestamps_dealloc = skiplist_new(0, NULL, NULL);
	thread_add_event(bm->master, labelpool_test_event_handler, NULL, 0,
			 &tcb->event_thread);
	monotime(&tcb->starttime);

	skiplist_insert(lp_tests, (void *)(uintptr_t)tcb->generation, tcb);
	return 0;
}

/* 【测试命令】debug bgp lptest {start|show|stop|release|clear} —— 启动 / 查看 / 停止 /
 * 按比例释放 / 清理压测（仅测试构建可用）。 */
DEFPY(start_labelpool_perf_test, start_labelpool_perf_test_cmd,
      "debug bgp lptest start",
      DEBUG_STR BGP_STR
      "label pool test\n"
      "start\n")
{
	lptest_start(vty);
	return CMD_SUCCESS;
}

/* 【测试】lptest_print_stats —— 打印计数(插入失败 / 删除失败 / 分配 / 释放)与时间戳表。 */
static void lptest_print_stats(struct vty *vty, struct lp_test *tcb)
{
	unsigned int i;

	vty_out(vty, "Global Lookup Failures in test_cb: %5u\n",
		lpt_test_cb_tcb_lookup_fails);
	vty_out(vty, "Global Lookup Failures in release: %5u\n",
		lpt_release_tcb_lookup_fails);
	vty_out(vty, "Global Lookup Failures in event:   %5u\n",
		lpt_test_event_tcb_lookup_fails);
	vty_out(vty, "Global Lookup Failures in stop:    %5u\n",
		lpt_stop_tcb_lookup_fails);
	vty_out(vty, "\n");

	if (!tcb) {
		if (skiplist_search(lp_tests, (void *)(uintptr_t)lpt_generation,
				    (void **)&tcb)) {
			vty_out(vty, "Error: can't find test %u\n",
				lpt_generation);
			return;
		}
	}

	vty_out(vty, "Test Generation %u:\n", tcb->generation);

	vty_out(vty, "Counter   Value\n");
	for (i = 0; i < LPT_STAT_MAX; ++i) {
		vty_out(vty, "%20s: %10u\n", lpt_counter_names[i],
			tcb->counter[i]);
	}
	vty_out(vty, "\n");

	if (tcb->timestamps_alloc) {
		void *Key;
		void *Value;
		void *cursor;

		float elapsed;

		vty_out(vty, "%10s %10s\n", "Count", "Seconds");

		cursor = NULL;
		while (!skiplist_next(tcb->timestamps_alloc, &Key, &Value,
				      &cursor)) {

			elapsed = ((float)(uintptr_t)Value) / 1000;

			vty_out(vty, "%10llu %10.3f\n",
				(unsigned long long)(uintptr_t)Key, elapsed);
		}
		vty_out(vty, "\n");
	}
}

DEFPY(show_labelpool_perf_test, show_labelpool_perf_test_cmd,
      "debug bgp lptest show",
      DEBUG_STR BGP_STR
      "label pool test\n"
      "show\n")
{

	if (lp_tests) {
		void *Key;
		void *Value;
		void *cursor;

		cursor = NULL;
		while (!skiplist_next(lp_tests, &Key, &Value, &cursor)) {
			lptest_print_stats(vty, (struct lp_test *)Value);
		}
	} else {
		vty_out(vty, "no test results\n");
	}
	return CMD_SUCCESS;
}

DEFPY(stop_labelpool_perf_test, stop_labelpool_perf_test_cmd,
      "debug bgp lptest stop",
      DEBUG_STR BGP_STR
      "label pool test\n"
      "stop\n")
{

	if (lpt_inprogress) {
		lptest_stop();
		lptest_print_stats(vty, NULL);
	} else {
		vty_out(vty, "no test in progress\n");
	}
	return CMD_SUCCESS;
}

DEFPY(clear_labelpool_perf_test, clear_labelpool_perf_test_cmd,
      "debug bgp lptest clear",
      DEBUG_STR BGP_STR
      "label pool test\n"
      "clear\n")
{

	if (lpt_inprogress) {
		lptest_stop();
	}
	if (lp_tests) {
		while (!skiplist_first(lp_tests, NULL, NULL))
			/* del function of skiplist cleans up tcbs */
			skiplist_delete_first(lp_tests);
	}
	return CMD_SUCCESS;
}

/*
 * With the "release" command, we can release labels at intervals through
 * the ID space. Thus we can to exercise the bitfield-wrapping behavior
 * of the allocator in a subsequent test.
 */
/* clang-format off */
/* 【测试命令】debug bgp lptest release test GENERATION every N —— 按 1/N 的比例释放某次
 * 测试记录里的标签，用来打散标签占用、在后续测试里触发分配器位图的“绕圈”行为。 */
DEFPY(release_labelpool_perf_test, release_labelpool_perf_test_cmd,
      "debug bgp lptest release test GENERATION$generation every (1-5)$every_nth",
      DEBUG_STR
      BGP_STR
      "label pool test\n"
      "release labels\n"
      "\"test\"\n"
      "test number\n"
      "\"every\"\n"
      "label fraction denominator\n")
{
	/* clang-format on */

	unsigned long testnum;
	char *end;
	struct lp_test *tcb;

	testnum = strtoul(generation, &end, 0);
	if (*end) {
		vty_out(vty, "Invalid test number: \"%s\"\n", generation);
		return CMD_SUCCESS;
	}
	if (lpt_inprogress && (testnum == lpt_generation)) {
		vty_out(vty,
			"Error: Test %lu is still in progress (stop first)\n",
			testnum);
		return CMD_SUCCESS;
	}

	if (skiplist_search(lp_tests, (void *)(uintptr_t)testnum,
			    (void **)&tcb)) {

		/* couldn't find current test in progress */
		vty_out(vty, "Error: Can't look up test number: \"%lu\"\n",
			testnum);
		++lpt_release_tcb_lookup_fails;
		return CMD_SUCCESS;
	}

	void *Key, *cKey;
	void *Value, *cValue;
	void *cursor;
	unsigned int iteration;
	int rc;

	cursor = NULL;
	iteration = 0;
	rc = skiplist_next(tcb->labels, &Key, &Value, &cursor);

	while (!rc) {
		cKey = Key;
		cValue = Value;

		/* find next item before we delete this one */
		rc = skiplist_next(tcb->labels, &Key, &Value, &cursor);

		if (!(iteration % every_nth)) {
			bgp_lp_release(tcb->label_type, cKey,
				       (mpls_label_t)(uintptr_t)cValue);
			skiplist_delete(tcb->labels, cKey, NULL);
			++tcb->counter[LPT_STAT_DEALLOCATED];
		}
		++iteration;
	}

	return CMD_SUCCESS;
}

/* 【测试】lptest_delete —— lp_tests 跳表的删除回调：释放一次测试的全部记录、时间戳，
 * 取消其事件线程并释放 tcb。 */
static void lptest_delete(void *val)
{
	struct lp_test *tcb = (struct lp_test *)val;
	void *Key;
	void *Value;
	void *cursor;

	if (tcb->labels) {
		cursor = NULL;
		while (!skiplist_next(tcb->labels, &Key, &Value, &cursor))
			bgp_lp_release(tcb->label_type, Key,
				       (mpls_label_t)(uintptr_t)Value);
		skiplist_free(tcb->labels);
		tcb->labels = NULL;
	}
	if (tcb->timestamps_alloc) {
		cursor = NULL;
		skiplist_free(tcb->timestamps_alloc);
		tcb->timestamps_alloc = NULL;
	}

	if (tcb->timestamps_dealloc) {
		cursor = NULL;
		skiplist_free(tcb->timestamps_dealloc);
		tcb->timestamps_dealloc = NULL;
	}

	if (tcb->event_thread)
		event_cancel(&tcb->event_thread);

	memset(tcb, 0, sizeof(*tcb));

	XFREE(MTYPE_LABELPOOL_TEST, tcb);
}

/* 【测试】lptest_init —— 建立 lp_tests 跳表（键=generation，值=lp_test）。 */
static void lptest_init(void)
{
	lp_tests = skiplist_new(0, NULL, lptest_delete);
}

/* 【测试】lptest_finish —— 销毁 lp_tests 跳表。 */
static void lptest_finish(void)
{
	if (lp_tests) {
		skiplist_free(lp_tests);
		lp_tests = NULL;
	}
}

/*------------------------------------------------------------------------
 *			Testing code end
 *------------------------------------------------------------------------*/
#endif /* BGP_LABELPOOL_ENABLE_TESTS */

/* 【函数】bgp_lp_vty_init —— 在 VIEW 节点安装各 show bgp labelpool ... 命令；若开启测试
 * 宏，再安装 ENABLE 节点下的 lptest 调试命令。 */
void bgp_lp_vty_init(void)
{
	install_element(VIEW_NODE, &show_bgp_labelpool_summary_cmd);
	install_element(VIEW_NODE, &show_bgp_labelpool_ledger_cmd);
	install_element(VIEW_NODE, &show_bgp_labelpool_inuse_cmd);
	install_element(VIEW_NODE, &show_bgp_labelpool_requests_cmd);
	install_element(VIEW_NODE, &show_bgp_labelpool_chunks_cmd);

#if BGP_LABELPOOL_ENABLE_TESTS
	install_element(ENABLE_NODE, &start_labelpool_perf_test_cmd);
	install_element(ENABLE_NODE, &show_labelpool_perf_test_cmd);
	install_element(ENABLE_NODE, &stop_labelpool_perf_test_cmd);
	install_element(ENABLE_NODE, &release_labelpool_perf_test_cmd);
	install_element(ENABLE_NODE, &clear_labelpool_perf_test_cmd);
#endif /* BGP_LABELPOOL_ENABLE_TESTS */
}

/* 【内存类型】每下一跳标签缓存的内存类型标签（用于内存统计）。 */
DEFINE_MTYPE_STATIC(BGPD, LABEL_PER_NEXTHOP_CACHE,
		    "BGP Label Per Nexthop entry");

/* The nexthops values are compared to
 * find in the tree the appropriate cache entry
 */
/* 【函数】bgp_label_per_nexthop_cache_cmp —— RB-tree 比较：按 nexthop 前缀比较。 */
int bgp_label_per_nexthop_cache_cmp(const struct bgp_label_per_nexthop_cache *a,
				    const struct bgp_label_per_nexthop_cache *b)
{
	return prefix_cmp(&a->nexthop, &b->nexthop);
}

struct bgp_label_per_nexthop_cache *
/* 【函数】bgp_label_per_nexthop_new —— 新建一个每下一跳标签缓存：label 初始化为
 * MPLS_INVALID_LABEL、复制 nexthop、初始化 paths 链表，然后挂入给定树。 */
bgp_label_per_nexthop_new(struct bgp_label_per_nexthop_cache_head *tree,
			  struct prefix *nexthop)
{
	struct bgp_label_per_nexthop_cache *blnc;

	blnc = XCALLOC(MTYPE_LABEL_PER_NEXTHOP_CACHE,
		       sizeof(struct bgp_label_per_nexthop_cache));
	blnc->tree = tree;
	blnc->label = MPLS_INVALID_LABEL;
	prefix_copy(&blnc->nexthop, nexthop);
	LIST_INIT(&(blnc->paths));
	bgp_label_per_nexthop_cache_add(tree, blnc);

	return blnc;
}

struct bgp_label_per_nexthop_cache *
/* 【函数】bgp_label_per_nexthop_find —— 按 nexthop 在树中查找；tree 为空返回 NULL。
 * 用栈上的临时 blnc 只填 nexthop 作为查找键。 */
bgp_label_per_nexthop_find(struct bgp_label_per_nexthop_cache_head *tree,
			   struct prefix *nexthop)
{
	struct bgp_label_per_nexthop_cache blnc = {};

	if (!tree)
		return NULL;

	memcpy(&blnc.nexthop, nexthop, sizeof(struct prefix));
	return bgp_label_per_nexthop_cache_find(tree, &blnc);
}

/* 【函数】bgp_label_per_nexthop_free —— 释放一个缓存：若持有标签，先通知 zebra 删除该
 * 标签(ZEBRA_MPLS_LABELS_DELETE)并归还标签池；再从树中摘除、释放 nh 与自身。
 * 疑点：这里直接取 blnc->nh->ifindex，未检查 nh 是否为 NULL，若 nh 为空会解引用空指针。 */
void bgp_label_per_nexthop_free(struct bgp_label_per_nexthop_cache *blnc)
{
	if (blnc->label != MPLS_INVALID_LABEL) {
		bgp_zebra_send_nexthop_label(ZEBRA_MPLS_LABELS_DELETE,
					     blnc->label, blnc->nh->ifindex,
					     blnc->nh->vrf_id, ZEBRA_LSP_BGP,
					     &blnc->nexthop, 0, NULL);
		bgp_nh_lp_release(blnc, blnc->label);
	}
	bgp_label_per_nexthop_cache_del(blnc->tree, blnc);
	if (blnc->nh)
		nexthop_free(blnc->nh);
	blnc->nh = NULL;
	XFREE(MTYPE_LABEL_PER_NEXTHOP_CACHE, blnc);
}

/* 【函数】bgp_label_per_nexthop_init —— 安装 show bgp ... label-nexthop 命令。 */
void bgp_label_per_nexthop_init(void)
{
	install_element(VIEW_NODE, &show_bgp_nexthop_label_cmd);
}
