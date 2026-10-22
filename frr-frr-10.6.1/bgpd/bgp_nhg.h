// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP Nexthop Group Support
 * Copyright (C) 2023 NVIDIA Corporation
 * Copyright (C) 2023 6WIND
 */
/* ============================================================================
 * 【文件导读】bgp_nhg.h —— BGP 的 L3 下一跳组(nexthop group) ID 分配
 * ============================================================================
 * 【背景】传统 ECMP 是把“一组下一跳”直接展开成多条相同的路由/nexthop 下发；
 * 现代内核与硬件则支持 **nexthop object**：先用一个 ID 定义“一个下一跳组”，
 * 再由多条路由引用这个 ID。好处是共享同一组下一跳的大量路由只需存一份组信息，
 * 组变化时改一处即可。
 *
 * 【本文件的职责】只做**ID 的分配与回收**（外加初始化/收尾）。真正的组内容
 * （组里有哪些下一跳）与下发由 lib 的 nexthop_group 设施与 bgp_nhg.c 配合完成。
 * 因此这是一个非常薄的接口层。
 *
 * 【为什么 ID 需要池化管理】内核侧 nexthop 的 ID 是有限的、且一旦释放可能被
 * 立刻复用；若 BGP 侧忘记归还就会“泄漏”ID 最终分配失败。所以配对使用
 * `bgp_nhg_id_alloc()` 与 `bgp_nhg_id_free()`，不要只 alloc 不 free。
 *
 * 【阅读建议】先看四个原型 → 再看 bgp_nhg.c（很短）确认 ID 池用的是哪个 lib 设施
 *            → 然后看 bgp_zebra.c 里怎么把 nhg id 编进下发的路由。
 * ============================================================================
 */

#ifndef _BGP_NHG_H
#define _BGP_NHG_H

#include "nexthop_group.h"

/* APIs for setting up and allocating L3 nexthop group ids */
/* 【函数】
 *   bgp_nhg_id_alloc  : 申请一个下一跳组 ID（返回 0 通常表示失败/无效，以实现为准）。
 *   bgp_nhg_id_free   : 归还 ID —— 必须与 alloc 配对，否则 ID 池泄漏。
 *   bgp_nhg_init      : 进程启动时初始化 ID 池（bgp_main.c 调用一次）。
 *   bgp_nhg_finish    : 进程退出时销毁 ID 池。
 * 注意前两个是“运行期高频”调用（每建立/拆除一个下一跳组各一次），后两个是
 * “进程级一次性”调用。 */
extern uint32_t bgp_nhg_id_alloc(void);
extern void bgp_nhg_id_free(uint32_t nhg_id);
extern void bgp_nhg_init(void);
void bgp_nhg_finish(void);

#endif /* _BGP_NHG_H */
