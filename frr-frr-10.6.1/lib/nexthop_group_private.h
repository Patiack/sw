// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Nexthop Group Private Functions.
 * Copyright (C) 2019 Cumulus Networks, Inc.
 *                    Stephen Worley
 */

/**
 * These functions should only be used internally for nexthop groups
 * and in certain special cases. Please use `lib/nexthop_group.h` for
 * any general nexthop_group api needs.
 */

#ifndef __NEXTHOP_GROUP_PRIVATE__
#define __NEXTHOP_GROUP_PRIVATE__

/* ============================================================================
 * 【文件导读】nexthop_group_private.h —— nexthop 组的“内部/危险”接口
 * ============================================================================
 * 【为什么要单独一个 private 头】nexthop 组有一批**只改链表、不做任何一致性
 * 维护**的底层操作(如把节点插进链、从链上摘下来)。它们很快，但会跳过计数、
 * 跳过与 zebra 的同步、也不发通知——误用会直接造成 nexthop 组的计数与实际
 * 节点数不一致(这类 bug 极难查)。于是把这类函数集中到本头文件，并在文件里
 * 写明“别乱用”。
 *
 * 【命名约定：前导下划线】本文件两个函数都以 `_` 开头(如 _nexthop_add)，这是
 * FRR 里“内部接口”的惯例标记——看到下划线前缀就该先去找公开版本
 * (nexthop_group.h 里的 nexthop_add / nexthop_del)而不是直接调用它。
 * 【原注释的措辞值得注意】"should only be used internally for nexthop groups
 * and in certain special cases" —— 也就是说**即使内部**也只在明确知道“这一层
 * 的维护我已经自己做了”时才用，属于少数派用法。
 * ============================================================================
 */
#include <nexthop_group.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 【内部接口】把 nexthop 挂到 *target 指向的链表上(不更新任何计数/状态)。
 *   target : 指向链表头的**二级指针**——之所以不直接传链表头，是因为要处理
 *            “*target 为 NULL(空链)”的情形；同时函数可能需要修改头指针本身。
 *   nexthop: 待插入的节点。
 * 【易踩的坑】① 不做去重，同一 nexthop 插两次就会在链上出现两个节点；
 *   ② 不校验 nexthop 是否已属于别的组，跨组共用节点会引发重复释放；
 *   ③ 调用方若还依赖组的计数/标志位，必须自己同步更新。 */
void _nexthop_add(struct nexthop **target, struct nexthop *nexthop);
/* 【内部接口】从 nhg 指向的组里摘掉指定 nexthop(同样不维护计数/状态)。
 *   nhg    : 目标 nexthop 组(需要结构体完整定义，故本头文件包含 nexthop_group.h)。
 *   nexthop: 要摘除的节点；**不在链上时是静默无操作**，不报错。
 * 【易踩的坑】只摘链、不释放(释放由调用方决定)——这与“删除”在用户语义上
 *   的差别很大，务必分清 _nexthop_del 与公开的删除接口。 */
void _nexthop_del(struct nexthop_group *nhg, struct nexthop *nexthop);

#ifdef __cplusplus
}
#endif

#endif /* __NEXTHOP_GROUP_PRIVATE__ */
