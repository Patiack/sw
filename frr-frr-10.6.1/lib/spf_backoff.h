// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This is an implementation of the IETF SPF delay algorithm
 * as explained in draft-ietf-rtgwg-backoff-algo-04
 *
 * Created: 25-01-2017 by S. Litkowski
 *
 * Copyright (C) 2017 Orange Labs http://www.orange.com/
 * Copyright (C) 2017 by Christian Franke, Open Source Routing / NetDEF Inc.
 *
 * This file is part of FRRouting (FRR)
 */
/*
 * ============================================================================
 * 文件导读：lib/spf_backoff.h —— SPF 计算退避（backoff）定时器接口
 * ============================================================================
 * 本文件声明 IETF 定义的 SPF 延迟算法（draft-ietf-rtgwg-backoff-algo）的公共
 * 接口。目标：IGP 拓扑频繁变化时避免每个事件都立刻跑一次 SPF，而是按
 * “状态机 + 定时器” 对 SPF 计算做指数式退避调度。
 *
 * 三状态机（定义在 .c）：
 *   QUIET      空闲，刚发生过一次事件，等待 Holddown 与 TimeToLearn；
 *   SHORT_WAIT 短等待，返回 short_delay；
 *   LONG_WAIT  长等待，返回 long_delay（TimeToLearn 到期后进入）。
 *
 * 典型用法：
 *   backoff = spf_backoff_new(m, name, init, short, long, holddown, learn);
 *   delay = spf_backoff_schedule(backoff);  收到 IGP 事件时调用并得建议延迟
 *
 * 与其他模块的关系：被 isisd / ospfd 等 IGP 在 SPF 调度处使用；通过
 * spf_backoff_cmd_init 注册全局 debug 命令，spf_backoff_write_config 回显配置。
 *
 * 建议阅读顺序：
 *   1) 结构体前向声明；
 *   2) new / free 生命周期；
 *   3) schedule（核心入口）；
 *   4) show / write_config / cmd_init 与各访问器函数。
 */
#ifndef _ZEBRA_SPF_BACKOFF_H
#define _ZEBRA_SPF_BACKOFF_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 前向声明：spf_backoff 实例本体（定义在 .c，对外不透明）、事件循环与 VTY，
 * 避免头文件引入更多依赖。
 */
struct spf_backoff;
struct event_loop;
struct vty;

/*
 * spf_backoff_new：创建一个 SPF 退避实例。
 * 参数：m 事件循环；name 实例名（用于 debug 输出）；init_delay 首次事件延迟；
 *       short_delay 短等待延迟；long_delay 长等待延迟；holddown Holddown 时长；
 *       timetolearn TimeToLearn 时长。单位均为毫秒。
 * 返回新实例（初始状态 QUIET）。
 */
struct spf_backoff *spf_backoff_new(struct event_loop *m, const char *name,
				    long init_delay, long short_delay,
				    long long_delay, long holddown,
				    long timetolearn);

/*
 * spf_backoff_free：释放 SPF 退避实例（会取消其定时器并释放名字）。
 * 参数：backoff 可为 NULL（直接返回）。
 */
void spf_backoff_free(struct spf_backoff *backoff);

/* Called whenever an IGP event is received, returns how many
 * milliseconds routing table computation should be delayed */
/*
 * spf_backoff_schedule：收到 IGP 事件时调用，推进状态机并返回建议的 SPF 延迟。
 * 返回值为毫秒。首次事件返回 init_delay；短等待期返回 short_delay；
 * 长等待期返回 long_delay。
 */
long spf_backoff_schedule(struct spf_backoff *backoff);

/* Shows status of SPF backoff instance */
/*
 * spf_backoff_show：把实例状态与各定时器输出到 VTY。
 * 参数：backoff 实例；vty 输出目标；prefix 每行前缀（用于缩进）。
 */
void spf_backoff_show(struct spf_backoff *backoff, struct vty *vty,
		      const char *prefix);

/* Writes out global SPF backoff debug config */
/*
 * spf_backoff_write_config：把全局 SPF 退避调试配置写出到 VTY。
 * 返回写出的配置行数。
 */
int spf_backoff_write_config(struct vty *vty);

/* Registers global SPF backoff debug commands */
/*
 * spf_backoff_cmd_init：注册全局 SPF 退避调试命令（debug / no debug）。
 */
void spf_backoff_cmd_init(void);

/* Accessor functions for SPF backoff parameters */
/*
 * 以下访问器返回实例中各定时器参数（毫秒），供上层读取配置值：
 *   spf_backoff_init_delay   首次事件延迟；
 *   spf_backoff_short_delay  短等待延迟；
 *   spf_backoff_long_delay   长等待延迟；
 *   spf_backoff_holddown     Holddown 时长；
 *   spf_backoff_timetolearn  TimeToLearn 时长。
 */
long spf_backoff_init_delay(struct spf_backoff *backoff);
long spf_backoff_short_delay(struct spf_backoff *backoff);
long spf_backoff_long_delay(struct spf_backoff *backoff);
long spf_backoff_holddown(struct spf_backoff *backoff);
long spf_backoff_timetolearn(struct spf_backoff *backoff);

#ifdef __cplusplus
}
#endif

#endif
