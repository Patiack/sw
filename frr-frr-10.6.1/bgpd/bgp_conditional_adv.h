// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BGP Conditional advertisement
 * Copyright (C) 2020  Samsung R&D Institute India - Bangalore.
 *			Madhurilatha Kuruganti
 */

#ifndef _FRR_BGP_CONDITION_ADV_H
#define _FRR_BGP_CONDITION_ADV_H
#include <zebra.h>
#include "prefix.h"
#include "bgpd/bgp_addpath.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgpd.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_updgrp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 【文件导读】bgp_conditional_adv.h —— 条件通告（conditional advertisement）
 * ============================================================================
 * 【要解决什么】有时“是否把 A 类路由通告给某个邻居”取决于“我有没有 B 类路由”。
 * 典型场景是**多出口/备份链路**：只有当从主链路学到了默认路由（B），才把明细路由
 * （A）通告给对端；主链路断了就撤回。用 route-map 做不到这一点，因为 route-map
 * 只看单条路由的属性，不看“整张表里有没有别的东西”。于是有了条件通告。
 *
 * 【配置长什么样（FRR 上是 advertise-map 组合）】
 *     neighbor X advertise-map ADV_MAP exist-map COND_MAP
 *     neighbor X advertise-map ADV_MAP non-exist-map COND_MAP
 * 语义：把 ADV_MAP(advertise-map) 匹配到的路由通告出去，**前提是** COND_MAP
 * (condition-map) 在当前 BGP 表里能匹配到（exist）**或**一条也匹配不到
 * (non-exist)。三个 map 都是 route-map，各自职责不同：
 *   ADV_MAP  决定“发哪些”；COND_MAP 决定“有没有发的条件”；两者配合完成开关。
 *
 * 【为什么需要轮询】条件判断依赖的是**整张路由表的内容**，而路由表随时在变。
 * 实现上不能每来一条路由就重算全部邻居的条件（代价太大），所以按定时器**周期性地
 * 扫描**条件路由（见 DEFAULT_CONDITIONAL_ROUTES_POLL_TIME）。这解释了两个现象：
 *   ① 条件变化后通告的生效/撤销**不是瞬时**的，最多延迟一个轮询周期；
 *   ② 相关代码里既有事件驱动的部分，也有定时器回调的部分，别只找其中一个。
 *
 * 【生效范围限制】该特性只对 **IPv4/IPv6 单播**这类有意义的 AFI/SAFI 生效，
 * 因此 enable/disable 都带着 afi/safi——按 (邻居, afi, safi) 三元组维护状态。
 *
 * 【阅读建议】peer_advertise_map_set/unset（配置）→ bgp_conditional_adv_enable /
 *            disable（按 afi/safi 激活与清理）→ 实现在 bgp_conditional_adv.c 的
 *            轮询与判定逻辑。
 * ============================================================================
 */

/* Macro to log debug message */
/* 【宏】条件通告的调试日志开关。展开成“先查 debug 位再打印”的 do-while 惯用法：
 *   BGP_DEBUG(cond_adv, COND_ADV) 是 FRR 的两级开关（全局大类 cond_adv +
 *   具体子项 COND_ADV），只有两级都开且编译时启用了 debug 才会输出。
 *   用 do-while(0) 包裹是宏的标准写法，保证在 if/else 里当单语句使用也安全；
 *   末尾的 "" 拼接是为了让 __VA_ARGS__ 至少有一个前置字符串（避免空变参）。
 *   注意这个宏会**求值参数**——不要在参数里写有副作用的表达式。 */
#define bgp_cond_adv_debug(...)                                                \
	do {                                                                   \
		if (BGP_DEBUG(cond_adv, COND_ADV))                             \
			zlog_debug("" __VA_ARGS__);                            \
	} while (0)

/* Polling time for monitoring condition-map routes in route table */
/* 【定时器】扫描条件路由的周期（秒）。见文件头导读：条件成立与否不是事件驱动的，
 * 而是靠这个周期兜底检查。因此现场看到的“开关不动”现象，先确认是否还没到下一
 * 个周期；也正因为是轮询，周期太长会显得迟钝、太短会白耗 CPU。 */
#define DEFAULT_CONDITIONAL_ROUTES_POLL_TIME 60

/* 【函数】按 (邻居, afi, safi) 激活/关闭条件通告：
 *   enable  : 完成注册与初始化——为这个 peer_af 建立条件表、启动轮询定时器，
 *             并把“当前条件是否成立”的初次判断做掉（否则首个周期内行为未定义）。
 *   disable : 反操作，必须**成对**调用（配置被删除、邻居被删除、afi 被去激活时
 *             都要走这里），否则会留下悬挂的定时器或未清理的引用。
 * 这两个函数不直接改配置，只负责“让配置在运行态生效/失效”。 */
extern void bgp_conditional_adv_enable(struct peer *peer, afi_t afi,
				       safi_t safi);
extern void bgp_conditional_adv_disable(struct peer *peer, afi_t afi,
					safi_t safi);
/* 【函数】配置项 `neighbor ... advertise-map A <exist-map|non-exist-map> C` 的
 * set / unset。参数同时给了**名字**和**已解析的对象**（两者都要，因为名字用于
 * 回显配置、对象用于立即取用；若只给对象，配置无法原样写回；若只给名字，无法
 * 立刻生效）。所有参数都必须非空/一致，否则报错返回非 0。
 *   condition : 布尔量，区分这条命令是 **exist-map(真)** 还是 **non-exist-map(假)**
 *               —— 同一个接口承担两种语义，改动/阅读时必须盯住这个标志位，
 *               否则会把 exist 与 non-exist 装反，行为正好相反。
 * 注意 advertise-map 是可以**同时存在多条**的（分属 exist 与 non-exist 两组），
 * unset 时要精确匹配到同一条，因此参数必须与 set 时完全一致。 */
extern int peer_advertise_map_set(struct peer *peer, afi_t afi, safi_t safi,
				  const char *advertise_name,
				  struct route_map *advertise_map,
				  const char *condition_name,
				  struct route_map *condition_map,
				  bool condition);
extern int peer_advertise_map_unset(struct peer *peer, afi_t afi, safi_t safi,
				    const char *advertise_name,
				    struct route_map *advertise_map,
				    const char *condition_name,
				    struct route_map *condition_map,
				    bool condition);
#ifdef __cplusplus
}
#endif

#endif /* _FRR_BGP_CONDITION_ADV_H */
