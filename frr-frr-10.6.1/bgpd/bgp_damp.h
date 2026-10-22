// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP flap dampening
 * Copyright (C) 2001 IP Infusion Inc.
 */

/* ============================================================================
 * 【文件导读】bgp_damp.h —— BGP 路由抖动抑制（flap dampening）
 * ============================================================================
 * 【它解决什么】一条路由反复 up / down 会让 BGP 不停重算、向邻居反复通告，造成全网
 * 震荡。抖动抑制（源自 RFC2439）的思路是给每条路由记一个“惩罚分(penalty)”：每次
 * down 加分，随时间按半衰期指数衰减；分数超过 suppress 阈值就停止通告该路由
 * (suppress)，降到 reuse 阈值以下才恢复通告。分数封顶到 ceiling。
 *
 * 【核心两个结构】
 *   bgp_damp_info   : **每条路由**一份的运行态（惩罚分、抖动次数、起止时间、所属
 *                     配置、反向指针等）。
 *   bgp_damp_config : **一套 dampening 参数**（阈值、半衰期，以及由参数算出的衰减表
 *                     与复用链表等），可分别挂在 peer / peer-group / bgp 实例上。
 *   get_active_bdc_from_pi 按“peer 自身 → 所属 peer-group → bgp 实例”的优先级选出
 *   对某条路由生效的那一套配置。
 *
 * 【时间与阈值】半衰期、复用阈值、抑制阈值、最大抑制时间都可配；实现把时间离散成
 * 刻度（DELTA_T 用于衰减表、DELTA_REUSE 用于复用链表轮转），因此相关计算是近似值。
 * 默认值见 DEFAULT_* 等宏。
 *
 * 【复用链表(reuse list)】被抑制的路由按“预计可复用时间”分散挂到若干 reuse_list 桶
 * 上，定时器每次只处理当前桶（reuse_offset 轮转），从而摊平 CPU 开销——这是本模块
 * 最需要理解的数据结构技巧；尚未能入桶的挂在 no_reuse_list。
 *
 * 【阅读建议】bgp_damp_info / config → 状态宏 → 时间常量与默认值 → 函数原型组。
 *            实现在 bgp_damp.c。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_DAMP_H
#define _QUAGGA_BGP_DAMP_H

#include "bgpd/bgp_table.h"

/* 【结构】bgp_damp_info —— 单条路由的抑制状态。
 *   penalty     : 惩罚分(figure-of-merit)，大于 suppress 阈值即被抑制。
 *   flap        : 累计抖动次数。
 *   start_time  : 首次抖动时间。
 *   t_updated   : 上次惩罚分被衰减/更新的时间（算 t-diff 的基准）。
 *   suppress_time : 允许解除抑制的最早时间（配合 max_suppress_time）。
 *   config      : 反向指针，指向生效的 bgp_damp_config。
 *   path / dest : 反向指针，指回 bgp_path_info 与 bgp_dest。
 *   index       : 在 reuse_list 中的桶下标；哨兵 -1 表示在 no_reuse_list 上。
 *   lastrecord  : 最近一次记录的事件类型（UPDATE / WITHDRAW）。
 *   entry       : SLIST 挂钩，使它可挂进 reuselist。 */
/* Structure maintained on a per-route basis. */
struct bgp_damp_info {
	/* Figure-of-merit.  */
	unsigned int penalty;

	/* Number of flapping.  */
	unsigned int flap;

	/* First flap time  */
	time_t start_time;

	/* Last time penalty was updated.  */
	time_t t_updated;

	/* Time of route start to be suppressed.  */
	time_t suppress_time;

	/* Back reference to associated dampening configuration. */
	struct bgp_damp_config *config;

	/* Back reference to bgp_path_info. */
	struct bgp_path_info *path;

	/* Back reference to bgp_node. */
	struct bgp_dest *dest;

	/* Current index in the reuse_list. */
	int index;
/* 【宏】reuse_list 下标的哨兵：为 -1 表示该条目位于 no_reuse_list 而非某个复用桶。 */
#define BGP_DAMP_NO_REUSE_LIST_INDEX                                           \
	(-1) /* index for elements on no_reuse_list */

	/* Last time message type. */
	uint8_t lastrecord;
/* 【宏】上次对该路由记录的事件类型：正常更新 / 撤回，影响抖动计数与惩罚逻辑。 */
#define BGP_RECORD_UPDATE	1U
#define BGP_RECORD_WITHDRAW	2U

	afi_t afi;
	safi_t safi;

	SLIST_ENTRY(bgp_damp_info) entry;
};

/* 【类型】复用链表的头（单向链表）。config 里会建一个 reuse_list 数组，每桶一个这样的头。 */
SLIST_HEAD(reuselist, bgp_damp_info);

/* 【结构】bgp_damp_config —— 一套 dampening 参数与运行数据（可挂在 peer / peer-group
 * / bgp 实例上，见 get_active_bdc_from_pi）。
 * 可配置项：suppress_value（高于此值即抑制）、reuse_limit（低于此值即恢复）、
 *   max_suppress_time（最长抑制时间）、half_life（惩罚减半所需时间）。
 * 实现期固定项：reuse_list_size（复用桶数）、reuse_index_size（索引数组大小）。
 * 由参数计算项：ceiling（惩罚上限）、decay_rate_per_tick、decay_array_size、
 *   reuse_scale_factor、scale_factor。
 * 运行数据：decay_array（按刻度预计算的衰减系数表，查表代替实时指数运算）、
 *   reuse_index（把惩罚分映射到桶的索引表）、reuse_list（桶数组）、reuse_offset
 *   （当前轮转到的桶）、no_reuse_list（暂未能入桶的条目）、t_reuse（复用定时器）。
 *   safi / afi 标明这套配置对应的地址族。 */
/* Specified parameter set configuration. */
struct bgp_damp_config {
	/* Value over which routes suppressed.  */
	unsigned int suppress_value;

	/* Value below which suppressed routes reused.  */
	unsigned int reuse_limit;

	/* Max time a route can be suppressed.  */
	time_t max_suppress_time;

	/* Time during which accumulated penalty reduces by half.  */
	time_t half_life;

	/* Non-configurable parameters but fixed at implementation time.
	 * To change this values, init_bgp_damp() should be modified.
	 */
	unsigned int reuse_list_size;  /* Number of reuse lists */
	unsigned int reuse_index_size; /* Size of reuse index array */

	/* Non-configurable parameters.  Most of these are calculated from
	 * the configurable parameters above.
	 */
	unsigned int ceiling;		  /* Max value a penalty can attain */
	unsigned int decay_rate_per_tick; /* Calculated from half-life */
	unsigned int decay_array_size; /* Calculated using config parameters */
	unsigned int reuse_scale_factor;
	double scale_factor;

	/* Decay array per-set based. */
	double *decay_array;

	/* Reuse index array per-set based. */
	int *reuse_index;

	/* Reuse list array per-set based. */
	struct reuselist *reuse_list;
	unsigned int reuse_offset;
	safi_t safi;

	/* All dampening information which is not on reuse list.  */
	struct reuselist no_reuse_list;

	/* Reuse timer thread per-set base. */
	struct event *t_reuse;

	afi_t afi;
};

/* 【宏】对某条路由应用 dampening 时的状态判定：未使用 / 已在使用 / 已被抑制。
 * 供 bgp_damp_update 等决定是“新增一条抑制记录”“更新已有记录”还是“直接跳过”。 */
#define BGP_DAMP_NONE           0
#define BGP_DAMP_USED		1
#define BGP_DAMP_SUPPRESSED	2

/* 【宏】时间离散刻度：复用链表的轮转步长（秒）与衰减表的计算步长（秒）。 */
/* Time granularity for reuse lists */
#define DELTA_REUSE	          10

/* Time granularity for decay arrays */
#define DELTA_T 	           5

/* 【宏】默认参数与容量：初始惩罚分、默认半衰期(分钟)、默认复用/抑制阈值、复用桶数量、
 * 复用索引数组大小。改动这些会改变 dampening 的默认行为。 */
#define DEFAULT_PENALTY         1000

#define DEFAULT_HALF_LIFE         15
#define DEFAULT_REUSE 	       	 750
#define DEFAULT_SUPPRESS 	2000

#define REUSE_LIST_SIZE          256
#define REUSE_ARRAY_SIZE        1024

/* 【函数】接口分组说明：
 *   get_active_bdc_from_pi   : 按优先级选出对某路由生效的配置（peer → 组 → bgp）。
 *   bgp_damp_enable/disable  : 在 **bgp 实例级** 启用/关闭 dampening（half 半衰期、
 *                              reuse/suppress 阈值、max 最大抑制时间）。
 *   bgp_damp_withdraw/update : 路由撤回/更新时喂给 dampening，触发惩罚分累加与判定。
 *   bgp_damp_info_free/clean : 释放单条抑制记录 / 清理某实例在某 afi,safi 的全部记录。
 *   bgp_damp_config_clean    : 释放一套配置内部分配的数组。
 *   bgp_damp_decay           : 按时间差算衰减后的惩罚分。
 *   bgp_config_write_damp / bgp_damp_info_vty / bgp_damp_reuse_time_vty /
 *   bgp_show_dampening_parameters : 配置回显与 show 输出。
 *   bgp_peer_damp_enable/disable / bgp_config_write_peer_damp /
 *   bgp_show_peer_dampening_parameters : **peer 级**（或 peer-group 级）dampening 的
 *                            启用、打印与回显。
 * 注意 bgp_damp_enable 与 bgp_peer_damp_enable 面向不同挂载点（实例级 vs 邻居级）。 */
extern struct bgp_damp_config *get_active_bdc_from_pi(struct bgp_path_info *pi,
						      afi_t afi, safi_t safi);
extern int bgp_damp_enable(struct bgp *bgp, afi_t afi, safi_t safi, time_t half,
			   unsigned int reuse, unsigned int suppress,
			   time_t max);
extern int bgp_damp_disable(struct bgp *bgp, afi_t afi, safi_t safi);
extern int bgp_damp_withdraw(struct bgp_path_info *path, struct bgp_dest *dest,
			     afi_t afi, safi_t safi, int attr_change);
extern int bgp_damp_update(struct bgp_path_info *path, struct bgp_dest *dest,
			   afi_t afi, safi_t saff);
extern void bgp_damp_info_free(struct bgp_damp_info *bdi,
			       struct reuselist *list, int withdraw);
extern void bgp_damp_info_clean(struct bgp *bgp, struct bgp_damp_config *bdc,
				afi_t afi, safi_t safi);
extern void bgp_damp_config_clean(struct bgp_damp_config *bdc);
extern int bgp_damp_decay(time_t tdiff, int penalty,
			  struct bgp_damp_config *bdc);
extern void bgp_config_write_damp(struct vty *vty, struct bgp *bgp, afi_t afi,
				  safi_t safi);
extern void bgp_damp_info_vty(struct vty *vty, struct bgp *bgp,
			      struct bgp_path_info *path, afi_t afi,
			      safi_t safi, json_object *json_path);
extern const char *bgp_damp_reuse_time_vty(struct vty *vty,
					   struct bgp_path_info *path,
					   char *timebuf, size_t len, afi_t afi,
					   safi_t safi, bool use_json,
					   json_object *json);
extern int bgp_show_dampening_parameters(struct vty *vty, afi_t afi,
					 safi_t safi, uint16_t show_flags);
extern void bgp_peer_damp_enable(struct peer *peer, afi_t afi, safi_t safi,
				 time_t half, unsigned int reuse,
				 unsigned int suppress, time_t max);
extern void bgp_peer_damp_disable(struct peer *peer, afi_t afi, safi_t safi);
extern void bgp_config_write_peer_damp(struct vty *vty, struct peer *peer,
				       afi_t afi, safi_t safi);
extern void bgp_show_peer_dampening_parameters(struct vty *vty,
					       struct peer *peer, afi_t afi,
					       safi_t safi, bool use_json);

#endif /* _QUAGGA_BGP_DAMP_H */
