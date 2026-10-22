// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * VRF Internal Header
 * Copyright (C) 2017 Cumulus Networks, Inc.
 *                    Donald Sharp
 */
#ifndef __LIB_VRF_PRIVATE_H__
#define __LIB_VRF_PRIVATE_H__

/* ============================================================================
 * 【文件导读】vrf_int.h —— VRF 生命周期的“内部”开关接口
 * ============================================================================
 * 【为什么叫 internal，又为什么只有两个函数】VRF 的“打开/关闭”不能由任意模块
 * 随意触发，否则协议侧可能看到不存在或尚未就绪的 VRF。真正合适的开关时机只有
 * 两处(文件头的英文注释把这一点讲得很直白)：
 *   zebra/if_netlink.c : 内核新增/删除 VRF 设备时，从 OS 进入 Zebra；
 *   lib/zclient.c      : Zebra 把这一变化转发给各守护进程时。
 * 因此这两个函数被单独放进这个“私有”头文件；其它模块请走公开的 lib/vrf.h 接口。
 * 【阅读顺序】先看文件头那段英文注释理解设计理由，再看下面两个函数的中文块。 */
#include "vrf.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * These functions should only be called by:
 * zebra/if_netlink.c -> The interface from OS into Zebra
 * lib/zclient.c -> The interface from Zebra to each daemon
 *
 * Why you ask?  Well because these are the turn on/off
 * functions and the only place we can really turn a
 * vrf on properly is in the call up from the os -> zebra
 * and the pass through of this informatoin from zebra -> protocols
 */

/*
 * vrf_enable
 *
 * Given a newly running vrf enable it to be used
 * by interested routing protocols
 */
/* 【函数】启用一个刚建好的 VRF，使其可被路由协议使用。
 *   参数 vrf : 指向 VRF 实例(其名字、路由表号、netns 已就绪)。
 *   返回     : 0 表示成功；非 0 表示失败(重名、资源不足等)。
 * 【副作用】会广播 VRF 启用事件，唤醒已注册的协议侧回调；协议随后可能在该 VRF 内
 *   建表并下发路由。
 * 【易踩的坑】调用前必须保证内核侧的 VRF 设备已就绪，否则协议会对着一个并不存在
 *   的名字工作。 */
extern int vrf_enable(struct vrf *vrf);

/*
 * vrf_delete
 *
 * Given a vrf that is being deleted, delete it
 * from interested parties
 */
/* 【函数】删除一个 VRF，并通知所有相关方清理。
 * 【副作用】触发协议侧的清理回调(撤销该 VRF 内的路由与邻居等)，随后释放 VRF
 *   实例。函数返回后 vrf 指针**不再有效**，不要继续引用。
 * 【易踩的坑】只应由前述两处(内核事件、Zebra 转发)调用；在协议自身逻辑里调用会
 *   造成清理顺序错乱。 */
extern void vrf_delete(struct vrf *vrf);

#ifdef __cplusplus
}
#endif

#endif
