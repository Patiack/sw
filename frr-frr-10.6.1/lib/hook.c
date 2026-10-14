// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2016  David Lamparter, for NetDEF, Inc.
 */

/* ============================================================================
 * 【文件导读】hook.c —— FRR 的“钩子(hook)”机制实现：可插拔的回调链表
 * ============================================================================
 * 【钩子解决什么问题】lib 里的框架代码经常需要在若干“扩展点”上让**别的模块**
 * 插进来做事，而框架本身不该知道有哪些模块。典型例子：bgpd 需要“VRF 删除时”
 * 做一些清理，但 VRF 模块在 lib 里、bgpd 在别处。钩子就是这块解耦板：
 *   框架侧用 DEFINE_HOOK 定义“有这么个钩子”；
 *   模块侧用 hook_register 把自己的函数挂上去；
 *   框架在合适的时机用 hook_call 顺序触发所有已注册函数。
 *
 * 【三个宏的分工(配合 hook.h 看)】
 *   DECLARE_HOOK / DEFINE_HOOK : 声明 / 定义钩子本体(全局变量 + 注册与调用函数)。
 *   hook_register()            : 把回调挂上，本文件 _hook_register() 是它的底层。
 *   hook_call()                : 遍历并调用，定义在 hook.h。
 *   DECLARE_KOOH/DEFINE_KOOH   : 与 HOOK 完全等价，只是名字倒写用作“逆序调用”的
 *                                视觉提示(hook.h 原文明确说明)。
 *
 * 【本文件只做两件事】注册、注销。因为这两个操作直接碰内部链表，所以对上层
 * 宏隐藏起来(__下划线前缀)，由 hook.h 里的宏负责拼参数。
 *
 * 【核心设计：栈上 vs 堆上的条目】hookent 有两种生存方式——
 *   - 注册者若在编译期就知道自己要挂几个钩子，可用静态数组(传入 stackent)，
 *     此时 `struct hookent` 是**静态存储**，ent_on_heap 为 false；
 *   - 否则本文件用 XCALLOC 现分配，ent_on_heap 为 true。
 * 注销时按 ent_on_heap 决定是 XFREE 还是 memset 归零(保留静态条目地址可复用)。
 * 这就是下面两个函数都把 ent_on_heap 当作分水岭的原因，也是理解“为什么
 * memset 而不是 free”的关键。
 *
 * 【阅读顺序】DEFINE_MTYPE_STATIC(记账类型) → _hook_register(插入 + 排序)
 * → _hook_unregister(摘链 + 按需释放)。
 * ============================================================================
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "memory.h"
#include "hook.h"

DEFINE_MTYPE_STATIC(LIB, HOOK_ENTRY, "Hook entry");

/* 【函数】把一个回调注册进钩子(按优先级插入到链表中)。
 *   hook     : 目标钩子(由 DEFINE_HOOK 生成的全局变量)。
 *   stackent : 调用方提供的**预分配条目**(通常是静态数组元素)，可为未使用状态；
 *              当它还被占用时本函数会改走堆分配。传 NULL 是不允许的。
 *   funcptr  : 要注册的回调函数指针。
 *   arg      : 传给回调的固定参数(has_arg 为假时忽略)。
 *   has_arg  : 回调是否需要参数——注意它是**函数签名的一部分**，注销时要按
 *              三元组 (funcptr, arg, has_arg) 精确匹配，不能只看函数指针。
 *   module   : 注册者所属模块，用于(未完成的)按模块批量注销/内省。
 *   funcname : 函数名字符串，仅用于报错与内省。
 *   priority : 排序键，数值越小越先执行(见下方插入逻辑)。
 * 【插入逻辑是重点，两个细节容易读错】
 *   ① 若 stackent 尚未使用(!ent_used)，**就地复用**它；否则另分配一个堆条目
 *      并标记 ent_on_heap——这让“注册一个静态条目却重复注册”不至于互相覆盖。
 *   ② 遍历找插入点时的比较方向由 hook->reverse 决定：
 *        reverse == false : 找到第一个 priority >= 自己的位置，即**升序**(小的在前)；
 *        reverse == true  : 找到第一个 priority <  自己的位置，即**降序**。
 *      这正是“逆序钩子”的实现方式——同一个 priority 语义，仅靠链表方向反转。
 *      KOOH 之所以在名字上做文章，就是为了提醒读者“这个钩子会 reverse”。
 * 【易踩的坑】① stackent 为 NULL 时解引用即崩溃，宏必须保证传入有效地址；
 *   ② 同一回调重复注册会被插入两次(本函数不做去重)，注销时也只摘掉第一个匹配项；
 *   ③ ent_used 一旦为真就不再看 stackent 内容，因此静态条目被“回收重用”是
 *      注销路径 memset 的结果，二者必须配对理解。 */
void _hook_register(struct hook *hook, struct hookent *stackent, void *funcptr,
		    void *arg, bool has_arg, struct frrmod_runtime *module,
		    const char *funcname, int priority)
{
	struct hookent *he, **pos;

	if (!stackent->ent_used)
		he = stackent;
	else {
		he = XCALLOC(MTYPE_HOOK_ENTRY, sizeof(*he));
		he->ent_on_heap = true;
	}
	he->ent_used = true;
	he->hookfn = funcptr;
	he->hookarg = arg;
	he->has_arg = has_arg;
	he->module = module;
	he->fnname = funcname;
	he->priority = priority;

	for (pos = &hook->entries; *pos; pos = &(*pos)->next)
		if (hook->reverse ? (*pos)->priority < priority
				  : (*pos)->priority >= priority)
			break;

	he->next = *pos;
	*pos = he;
}

/* 【函数】把先前注册的回调从钩子上摘掉。
 *   hook/funcptr/arg/has_arg: **必须与注册时完全一致的三元组**才能匹配到。
 *   返回: 无。找不到匹配项时**静默返回**，不报错(调用方需自己保证配对)。
 * 【要点】使用了二级指针 prev 来做“摘链”：命中后 *prev = he->next 即把节点从
 * 链上摘除，无需处理前驱为空的特例——这是链表删除的惯用写法。
 * 【易踩的坑】① 只摘除**第一个**匹配项；若同一回调被注册了多次，要调用同样
 *   次数才能全部清除；② 释放策略按 ent_on_heap 分流——堆条目 XFREE 归还，
 *   静态条目 memset 归零并把 ent_used 一并清掉，从而**可以被后续注册复用**；
 *   ③ 本函数不检查钩子是否正在被 hook_call 遍历(无锁)，因此**不要**在钩子
 *   回调内部注销自己所在的钩子，否则遍历中的链表会断裂。 */
void _hook_unregister(struct hook *hook, void *funcptr, void *arg, bool has_arg)
{
	struct hookent *he, **prev;

	for (prev = &hook->entries; (he = *prev) != NULL; prev = &he->next)
		if (he->hookfn == funcptr && he->hookarg == arg
		    && he->has_arg == has_arg) {
			*prev = he->next;
			if (he->ent_on_heap)
				XFREE(MTYPE_HOOK_ENTRY, he);
			else
				memset(he, 0, sizeof(*he));
			break;
		}
}
