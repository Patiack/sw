// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Timer Wheel
 * Copyright (C) 2016 Cumulus Networks, Inc.
 * Donald Sharp
 */
/*
 * 【文件导读】wheel.c —— 定时轮(timer wheel)实现
 *
 * 1. 职责：
 *    实现 wheel.h 的接口。用固定数量的槽把时间分段，把大量周期性任务
 *    聚合到少数定时器事件上，显著减少事件循环里的定时事件数量。
 *
 * 2. 核心数据结构：
 *    - struct timer_wheel(见 wheel.h)：槽位数组 wheel_slot_lists、
 *      当前槽 curr_slot、每格时长 nexttime、跳槽数 slots_to_skip。
 *    - 两种 MTYPE：TIMER_WHEEL(轮子本体)、TIMER_WHEEL_LIST(槽 list)。
 *
 * 3. 与其他模块的关系：
 *    - frrevent.h : event_add_timer_msec / event_cancel 驱动轮子前进。
 *    - linklist.h : 每个槽是一个 list。
 *    - memory.h   : XCALLOC / XFREE 带 MTYPE 记账。
 *
 * 4. 推荐阅读顺序：
 *    wheel_init -> wheel_timer_thread(核心滴答逻辑) -> wheel_add_item
 *    -> wheel_remove_item -> wheel_delete。
 *
 * 5. 易踩的坑：
 *    - wheel_timer_thread 中 curr_slot 用 long long / unsigned long long 混合
 *      计算后再对 slots 取模，注意类型与取模范围。
 *    - slot_key 在 add / remove 时各算一次，必须对同一 item 返回相同结果，
 *      否则会加到 A 槽却从 B 槽删，导致删不掉或误删。
 *    - slot_run 在事件循环线程执行，禁止阻塞。
 *    - wheel_delete 只释放槽 list 结构，不回调释放残留项，删除前应清空槽。
 *    - debug_timer_wheel 是进程内静态开关，仅控制日志。
 */
#include "zebra.h"
#include "linklist.h"
#include "frrevent.h"
#include "memory.h"
#include "wheel.h"
#include "log.h"

/*
 * 定义两类内存记账类型：
 *   TIMER_WHEEL      : 轮子结构体本体。
 *   TIMER_WHEEL_LIST : 每个槽使用的 list 对象。
 * 均为文件内静态(STATIC)，仅用于 memory 统计。
 */
DEFINE_MTYPE_STATIC(LIB, TIMER_WHEEL, "Timer Wheel");
DEFINE_MTYPE_STATIC(LIB, TIMER_WHEEL_LIST, "Timer Wheel Slot List");

/*
 * 调试开关：非 0 时在加/删项与滴答时打印 zlog_debug。
 * 目前无外部 setter，默认 0(关闭)，仅作开发期排查用。
 */
static int debug_timer_wheel = 0;

/*
 * 定时轮驱动回调的前向声明(定义紧随其后)。
 * 提前声明是因为 wheel_init 里注册该回调时它尚未定义。
 */
static void wheel_timer_thread(struct event *t);

/*
 * 定时轮的驱动回调：轮子每次“滴答”时被事件循环调用。
 *   t : 事件对象，EVENT_ARG 取回所属 timer_wheel。
 * 流程：
 *   1) curr_slot 前进 slots_to_skip 格，再对 slots 取模得到本轮处理槽；
 *   2) 若开启调试则打印该槽内项数；
 *   3) 用 ALL_LIST_ELEMENTS 遍历该槽，对每项调用 slot_run(允许回调中删项)；
 *   4) 向后探测连续空槽，累加 slots_to_skip，以便跳过空槽减少唤醒；
 *   5) 按 nexttime * slots_to_skip 毫秒重新注册定时器，形成下一次滴答。
 * 副作用：执行用户 slot_run；更新 curr_slot / slots_to_skip / timer。
 * 注意：slot_run 在事件循环线程执行，禁止阻塞。
 */
static void wheel_timer_thread(struct event *t)
{
	struct listnode *node, *nextnode;
	unsigned long long curr_slot;
	unsigned int slots_to_skip = 1;
	struct timer_wheel *wheel;
	void *data;

	wheel = EVENT_ARG(t);

	wheel->curr_slot += wheel->slots_to_skip;

	curr_slot = wheel->curr_slot % wheel->slots;

	if (debug_timer_wheel)
		zlog_debug("%s: Wheel Slot: %lld(%lld) count: %d", __func__,
			   wheel->curr_slot, curr_slot,
			   listcount(wheel->wheel_slot_lists[curr_slot]));

	for (ALL_LIST_ELEMENTS(wheel->wheel_slot_lists[curr_slot], node,
			       nextnode, data))
		(*wheel->slot_run)(data);

	while (list_isempty(wheel->wheel_slot_lists[(curr_slot + slots_to_skip)
						    % wheel->slots])
	       && (curr_slot + slots_to_skip) % wheel->slots != curr_slot)
		slots_to_skip++;

	wheel->slots_to_skip = slots_to_skip;
	event_add_timer_msec(wheel->master, wheel_timer_thread, wheel,
			     wheel->nexttime * slots_to_skip, &wheel->timer);
}

/*
 * 创建并初始化定时轮。
 *   master   : 事件循环句柄。
 *   period   : 轮子转一圈的秒数。
 *   slots    : 槽位数。
 *   slot_key : 计算项所属槽位的哈希回调。
 *   slot_run : 处理槽内单项的回调。
 *   run_name : 调试用的运行名(当前实现未使用)。
 * 返回新轮子；XCALLOC 失败走 FRR 致命处理。
 * 副作用：分配轮子与各槽 list，把 nexttime 设为 period / slots(每格毫秒)，
 * 并注册首个驱动定时器。
 * 注意：period / slots 是整数除，若 period < slots 会得 0，可能导致高频滴答；
 * slot_key 返回值需落在 [0, slots)。
 */
struct timer_wheel *wheel_init(struct event_loop *master, int period,
			       size_t slots,
			       unsigned int (*slot_key)(const void *),
			       void (*slot_run)(void *), const char *run_name)
{
	struct timer_wheel *wheel;
	size_t i;

	wheel = XCALLOC(MTYPE_TIMER_WHEEL, sizeof(struct timer_wheel));

	wheel->slot_key = slot_key;
	wheel->slot_run = slot_run;

	wheel->period = period;
	wheel->slots = slots;
	wheel->curr_slot = 0;
	wheel->master = master;
	wheel->nexttime = period / slots;

	wheel->wheel_slot_lists = XCALLOC(MTYPE_TIMER_WHEEL_LIST,
					  slots * sizeof(struct list *));
	for (i = 0; i < slots; i++)
		wheel->wheel_slot_lists[i] = list_new();

	event_add_timer_msec(wheel->master, wheel_timer_thread, wheel,
			     wheel->nexttime, &wheel->timer);

	return wheel;
}

/*
 * 删除定时轮。
 *   wheel : 目标轮子。
 * 副作用：逐个 list_delete 释放各槽 list，取消定时器，再释放槽数组与轮子本体。
 * 注意：仅释放 list 结构，不会对槽内残留项触发任何回调或释放其内存，
 * 调用者应先确保轮子已空、项已取出或自行销毁。
 */
void wheel_delete(struct timer_wheel *wheel)
{
	int i;

	for (i = 0; i < wheel->slots; i++) {
		list_delete(&wheel->wheel_slot_lists[i]);
	}

	event_cancel(&wheel->timer);
	XFREE(MTYPE_TIMER_WHEEL_LIST, wheel->wheel_slot_lists);
	XFREE(MTYPE_TIMER_WHEEL, wheel);
}

/*
 * 把一项加入定时轮。
 *   wheel : 目标轮子。
 *   item  : 不透明数据，最终交给 slot_run。
 * 返回 0(恒成功)。
 * 副作用：用 slot_key 算出槽下标(slot % slots)，把 item 追加到该槽 list；
 * 若开启调试则打印。可能因新项落入先前空槽而影响下次跳槽(需等下一轮滴答)。
 * 注意：同一 item 重复加入会重复出现；item 内存归调用者。
 */
int wheel_add_item(struct timer_wheel *wheel, void *item)
{
	long long slot;

	slot = (*wheel->slot_key)(item);

	if (debug_timer_wheel)
		zlog_debug("%s: Inserting %p: %lld %lld", __func__, item, slot,
			   slot % wheel->slots);
	listnode_add(wheel->wheel_slot_lists[slot % wheel->slots], item);

	return 0;
}

/*
 * 从定时轮移除一项。
 *   wheel : 目标轮子。
 *   item  : 待移除项。
 * 返回 0(恒成功；即使 item 不在槽中，listnode_delete 也不报错)。
 * 副作用：用 slot_key 算出槽下标，从该槽 list 删除 item；调试时打印。
 * 注意：item 的内存不在此释放；若 slot_key 对同一 item 返回不一致，
 * 会在错误槽里查找而删不掉，务必保证 slot_key 稳定。
 */
int wheel_remove_item(struct timer_wheel *wheel, void *item)
{
	long long slot;

	slot = (*wheel->slot_key)(item);

	if (debug_timer_wheel)
		zlog_debug("%s: Removing %p: %lld %lld", __func__, item, slot,
			   slot % wheel->slots);
	listnode_delete(wheel->wheel_slot_lists[slot % wheel->slots], item);

	return 0;
}
