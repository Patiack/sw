// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Timer Wheel
 * Copyright (C) 2016 Cumulus Networks, Inc.
 * Donald Sharp
 */
#ifndef __WHEEL_H__
#define __WHEEL_H__

/*
 * 【文件导读】wheel.h —— 定时轮(timer wheel)公共接口
 *
 * 1. 这个文件干什么：
 *    定义一个“定时轮”抽象，用来把海量周期性任务聚合成少量事件。做法是把
 *    时间切成固定数量的槽(slots)，每个项按 slot_key 落到某个槽里；
 *    轮子每隔 period / slots 秒前进一格，并对当前槽内的所有项执行 slot_run。
 *
 * 2. 核心数据结构：
 *    struct timer_wheel : 轮子本体，含槽位数组 wheel_slot_lists、当前槽
 *    下标 curr_slot、驱动 event，以及两个回调 slot_key / slot_run。
 *
 * 3. 与其他模块的关系：
 *    - 依赖 event_loop(master) 注册驱动定时器。
 *    - 依赖 list 为每个槽维护项列表(实现见 wheel.c)。
 *    - 典型使用方：需要给大量对端定期发包 / 超时检查的协议(如 hello)。
 *
 * 4. 推荐阅读顺序：
 *    先看 struct timer_wheel 字段 -> wheel_init -> wheel_add_item
 *    -> wheel_remove_item -> wheel_delete。
 *
 * 5. 易踩的坑：
 *    - slot_run / slot_key 在事件循环线程被调用，禁止阻塞。
 *    - slot_key 返回值必须稳定且落在 [0, slots)，否则会越界访问槽数组。
 *    - wheel_delete 不会回调释放槽内残留项，删除前应确保槽已空。
 *    - 项一旦加入轮子，其归属与运行时机由轮子决定，勿自行修改其 slot 归属。
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 定时轮(timer wheel)结构：把大量周期性事件聚合成少量事件。
 *   master           : 事件循环句柄，用于注册驱动定时器。
 *   slots            : 轮子上的槽位数。
 *   curr_slot        : 当前正在处理的槽位下标。
 *   period           : 轮子转一整圈的秒数(每隔 period / slots 秒前进一格)。
 *   nexttime         : 下一次触发的时间戳。
 *   slots_to_skip    : 为跳过空槽而在一次唤醒中可前进的槽数。
 *   wheel_slot_lists : 每个槽位一个 list，存放该槽内的项。
 *   timer            : 驱动轮子前进的事件句柄。
 *   slot_key         : 计算某项应放入哪个槽的哈希函数。
 *   slot_run         : 对槽内每项执行的函数。
 * 典型用途：给海量邻居批量发送 keepalive，用少数定时器替代成百上千个独立事件。
 */
struct timer_wheel {
	struct event_loop *master;
	int slots;
	long long curr_slot;
	unsigned int period;
	unsigned int nexttime;
	unsigned int slots_to_skip;

	struct list **wheel_slot_lists;
	struct event *timer;
	/*
	 * Key to determine what slot the item belongs in
	 */
	unsigned int (*slot_key)(const void *destroy);

	void (*slot_run)(void *data);
};

/*
 * Creates a timer wheel
 *
 * master - Thread master structure for the process
 * period - The Time in seconds that the timer wheel will
 *          take before it starts issuing commands again
 *          for items in each slot
 * slots  - The number of slots to have in this particular
 *          timer wheel
 * slot_key - A hashing function of some sort that will allow
 *            the timer wheel to put items into individual slots
 * slot_run - The function to run over each item in a particular slot
 *
 * Creates a timer wheel that will wake up 'slots' times over the entire
 * wheel.  Each time the timer wheel wakes up it will iterate through
 * and run the slot_run function for each item stored in that particular
 * slot.
 *
 * The timer code is 'intelligent' in that it notices if anything is
 * in a particular slot and can schedule the next timer to skip
 * the empty slot.
 *
 * The general purpose of a timer wheel is to reduce events in a system.
 * A perfect example of usage for this is say hello packets that need
 * to be sent out to all your neighbors.  Suppose a large routing protocol
 * has to send keepalive packets every Y seconds to each of it's peers.
 * At scale we can have a very large number of peers, X.
 * This means that we will have X timing events every Y seconds.
 * If you replace these events with a timer wheel that has Z slots
 * you will have at most Y/Z timer events if each slot has a work item
 * in it.
 *
 * When X is large the number of events in a system can quickly escalate
 * and cause significant amount of time handling thread events instead
 * of running your code.
 */
/*
 * 创建并初始化一个定时轮。
 *   master   : 事件循环句柄。
 *   period   : 轮子转一圈的秒数。
 *   slots    : 槽位数。
 *   slot_key : 计算项所属槽位的回调(哈希)。
 *   slot_run : 处理槽内单项的回调。
 *   run_name : 内部定时事件的调试名。
 * 返回新轮子；分配失败按 FRR 约定走致命处理(通常不返回 NULL)。
 * 副作用：分配轮子与各槽 list，注册首个驱动定时器。
 * 约定：slot_key 返回值必须稳定且落在 [0, slots) 范围内，否则越界。
 */
struct timer_wheel *wheel_init(struct event_loop *master, int period,
			       size_t slots,
			       unsigned int (*slot_key)(const void *),
			       void (*slot_run)(void *), const char *run_name);

/*
 * Delete the specified timer wheel created
 */
/*
 * 删除定时轮：取消其定时器并释放全部槽 list 与轮子本体。
 *   wheel : 目标轮子。
 * 副作用：不对槽内残留项调用任何回调(仅释放 list 结构)，调用者需自行处理
 * 项的生命周期，建议先保证轮子已空再删除。
 */
void wheel_delete(struct timer_wheel *wheel);

/*
 * wheel - The Timer wheel being modified
 * item - The generic data structure that will be handed
 *        to the slot_run function.
 *
 * Add item to a slot setup by the slot_key,
 * possibly change next time pop.
 */
/*
 * 把一项加入定时轮中由 slot_key 决定的槽位。
 *   wheel : 目标轮子。
 *   item  : 不透明数据，后续交给 slot_run 处理。
 * 返回 0 表示成功(本实现中失败情形有限)。
 * 副作用：把 item 追加到对应槽 list，并可能调整下一次触发的定时器。
 * 注意：item 的内存归调用者，轮子只保存指针。
 */
int wheel_add_item(struct timer_wheel *wheel, void *item);

/*
 * wheel - The Timer wheel being modified.
 * item - The item to remove from one of the slots in
 *        the timer wheel.
 *
 * Remove a item to a slot setup by the slot_key,
 * possibly change next time pop.
 */
/*
 * 从定时轮中移除一项。
 *   wheel : 目标轮子。
 *   item  : 待移除项，按其 slot_key 定位所在槽。
 * 返回 0 表示成功。
 * 副作用：从对应槽 list 中删除该项，并可能调整下一次触发时间。
 * 注意：item 的内存不在此释放，仍归调用者所有。
 */
int wheel_remove_item(struct timer_wheel *wheel, void *item);

#ifdef __cplusplus
}
#endif

#endif
