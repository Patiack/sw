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
 * 文件导读：lib/spf_backoff.c —— SPF 退避定时器实现
 * ============================================================================
 * 本文件实现 IETF SPF 延迟算法（draft-ietf-rtgwg-backoff-algo）的状态机与
 * 定时器，向上层提供“收到拓扑事件后应延迟多久再跑 SPF”的决策。
 *
 * 三个延迟参数与两个计时器构成核心：
 *   init_delay  本安静期内第一次事件的固定延迟；
 *   short_delay 短时间内又发生事件时的短等待；
 *   long_delay  事件持续超过 timetolearn 后的长等待（抑制抖动）；
 *   holddown    用于把状态机拉回 QUIET 的静默计时；timetolearn 计时到期则
 *               进入 LONG_WAIT。
 *
 * 关键数据结构：
 *   struct spf_backoff：保存参数、状态（QUIET / SHORT_WAIT / LONG_WAIT）、
 *   两个 event（holddown 与 timetolearn）以及调试用名字与事件时间。
 *
 * 与其他模块的关系：
 *   - 依赖 frrevent 的事件定时器（event_add_timer_msec、event_cancel 等）；
 *   - 通过 DEFUN 注册 debug 命令，并在 write_config 中回显；
 *   - 被 isisd / ospfd 等 IGP 在 SPF 调度处使用。
 *
 * 易踩的坑：
 *   - schedule 在 SHORT_WAIT / LONG_WAIT 时会重置 holddown 定时器；
 *   - holddown 到期会取消 timetolearn 计时并清空 first_event_time。
 *
 * 建议阅读顺序：
 *   1) debug 开关与 backoff_debug 宏；
 *   2) 状态枚举与 spf_backoff 结构；
 *   3) state2str、new / free；
 *   4) 两个定时器回调与 schedule 状态机；
 *   5) show、DEFUN 命令、write_config、cmd_init 与访问器。
 */
#include <zebra.h>

#include "spf_backoff.h"

#include "command.h"
#include "memory.h"
#include "frrevent.h"
#include "vty.h"

/*
 * 注册本文件使用的内存类型：SPF_BACKOFF（实例本体）与 SPF_BACKOFF_NAME
 * （实例名副本）。
 */
DEFINE_MTYPE_STATIC(LIB, SPF_BACKOFF, "SPF backoff");
DEFINE_MTYPE_STATIC(LIB, SPF_BACKOFF_NAME, "SPF backoff name");

/*
 * 全局 SPF 退避调试开关，由 debug spf-delay-ietf 命令控制。
 * 下表宏 backoff_debug 在开关打开时输出 zlog_debug。
 */
static bool debug_spf_backoff = false;
/*
 * backoff_debug：条件式调试日志宏，仅当 debug_spf_backoff 为真时打印。
 * 用法同 zlog_debug，接受格式化参数。
 */
#define backoff_debug(...)                                                     \
	do {                                                                   \
		if (debug_spf_backoff)                                         \
			zlog_debug(__VA_ARGS__);                               \
	} while (0)

/*
 * enum spf_backoff_state：退避状态机的三种状态。
 * QUIET 空闲、SHORT_WAIT 短等待、LONG_WAIT 长等待。
 */
enum spf_backoff_state {
	SPF_BACKOFF_QUIET,
	SPF_BACKOFF_SHORT_WAIT,
	SPF_BACKOFF_LONG_WAIT
};

/*
 * struct spf_backoff：SPF 退避实例。
 * m 为事件循环；init/short/long/holddown/timetolearn 为算法参数（毫秒）；
 * state 为当前状态；t_holddown / t_timetolearn 为两个定时器句柄；
 * name 为实例名；first_event_time / last_event_time 供调试展示事件时间。
 */
struct spf_backoff {
	struct event_loop *m;

	/* Timers as per draft */
	long init_delay;
	long short_delay;
	long long_delay;
	long holddown;
	long timetolearn;

	/* State machine */
	enum spf_backoff_state state;
	struct event *t_holddown;
	struct event *t_timetolearn;

	/* For debugging */
	char *name;
	struct timeval first_event_time;
	struct timeval last_event_time;
};

/*
 * spf_backoff_state2str：状态枚举转字符串（QUIET / SHORT_WAIT / LONG_WAIT）。
 * 未知状态返回 "???"。
 */
static const char *spf_backoff_state2str(enum spf_backoff_state state)
{
	switch (state) {
	case SPF_BACKOFF_QUIET:
		return "QUIET";
	case SPF_BACKOFF_SHORT_WAIT:
		return "SHORT_WAIT";
	case SPF_BACKOFF_LONG_WAIT:
		return "LONG_WAIT";
	}
	return "???";
}

/*
 * spf_backoff_new：分配并初始化 SPF 退避实例。
 * 参数：m 事件循环；name 实例名（XSTRDUP 复制）；其余为各延迟 / 时长参数。
 * 返回新实例，初始状态 QUIET。
 */
struct spf_backoff *spf_backoff_new(struct event_loop *m, const char *name,
				    long init_delay, long short_delay,
				    long long_delay, long holddown,
				    long timetolearn)
{
	struct spf_backoff *rv;

	rv = XCALLOC(MTYPE_SPF_BACKOFF, sizeof(*rv));
	rv->m = m;

	rv->init_delay = init_delay;
	rv->short_delay = short_delay;
	rv->long_delay = long_delay;
	rv->holddown = holddown;
	rv->timetolearn = timetolearn;

	rv->state = SPF_BACKOFF_QUIET;

	rv->name = XSTRDUP(MTYPE_SPF_BACKOFF_NAME, name);
	return rv;
}

/*
 * spf_backoff_free：释放 SPF 退避实例。
 * 参数：backoff 可为 NULL（直接返回）。
 * 副作用：取消两个定时器，释放名字副本，再释放实例本体。
 */
void spf_backoff_free(struct spf_backoff *backoff)
{
	if (!backoff)
		return;

	event_cancel(&backoff->t_holddown);
	event_cancel(&backoff->t_timetolearn);
	XFREE(MTYPE_SPF_BACKOFF_NAME, backoff->name);

	XFREE(MTYPE_SPF_BACKOFF, backoff);
}

/*
 * spf_backoff_timetolearn_elapsed：TimeToLearn 计时到期回调。
 * 参数：event 携带 spf_backoff 实例。
 * 副作用：状态切到 LONG_WAIT（表示事件已持续足够久，改用长延迟抑制抖动）。
 */
static void spf_backoff_timetolearn_elapsed(struct event *event)
{
	struct spf_backoff *backoff = EVENT_ARG(event);

	backoff->state = SPF_BACKOFF_LONG_WAIT;
	backoff_debug("SPF Back-off(%s) TIMETOLEARN elapsed, move to state %s",
		      backoff->name, spf_backoff_state2str(backoff->state));
}

/*
 * spf_backoff_holddown_elapsed：Holddown 计时到期回调。
 * 参数：event 携带 spf_backoff 实例。
 * 副作用：取消 TimeToLearn 计时、清空首次事件时间、状态回到 QUIET
 * （表示已安静足够久，下一次事件重新按 init_delay 起算）。
 */
static void spf_backoff_holddown_elapsed(struct event *event)
{
	struct spf_backoff *backoff = EVENT_ARG(event);

	event_cancel(&backoff->t_timetolearn);
	timerclear(&backoff->first_event_time);
	backoff->state = SPF_BACKOFF_QUIET;
	backoff_debug("SPF Back-off(%s) HOLDDOWN elapsed, move to state %s",
		      backoff->name, spf_backoff_state2str(backoff->state));
}

/*
 * spf_backoff_schedule：状态机核心入口，收到 IGP 事件时调用。
 * 参数：backoff 实例。
 * 返回建议的 SPF 延迟（毫秒）：QUIET->init_delay，SHORT_WAIT->short_delay，
 * LONG_WAIT->long_delay。
 * 副作用：QUIET 时启动 timetolearn 与 holddown 两个定时器并记录首次事件时间；
 * SHORT/LONG_WAIT 时重置 holddown 定时器；每次更新 last_event_time。
 */
long spf_backoff_schedule(struct spf_backoff *backoff)
{
	long rv = 0;
	struct timeval now;

	gettimeofday(&now, NULL);

	backoff_debug("SPF Back-off(%s) schedule called in state %s",
		      backoff->name, spf_backoff_state2str(backoff->state));

	backoff->last_event_time = now;

	switch (backoff->state) {
	case SPF_BACKOFF_QUIET:
		backoff->state = SPF_BACKOFF_SHORT_WAIT;
		event_add_timer_msec(
			backoff->m, spf_backoff_timetolearn_elapsed, backoff,
			backoff->timetolearn, &backoff->t_timetolearn);
		event_add_timer_msec(backoff->m, spf_backoff_holddown_elapsed,
				     backoff, backoff->holddown,
				     &backoff->t_holddown);
		backoff->first_event_time = now;
		rv = backoff->init_delay;
		break;
	case SPF_BACKOFF_SHORT_WAIT:
	case SPF_BACKOFF_LONG_WAIT:
		event_cancel(&backoff->t_holddown);
		event_add_timer_msec(backoff->m, spf_backoff_holddown_elapsed,
				     backoff, backoff->holddown,
				     &backoff->t_holddown);
		if (backoff->state == SPF_BACKOFF_SHORT_WAIT)
			rv = backoff->short_delay;
		else
			rv = backoff->long_delay;
		break;
	}

	backoff_debug(
		"SPF Back-off(%s) changed state to %s and returned %ld delay",
		backoff->name, spf_backoff_state2str(backoff->state), rv);
	return rv;
}

/*
 * timeval_format：把 timeval 格式化为可读本地时间字符串。
 * 参数：tv 待格式化的时间。
 * 返回静态缓冲区中的字符串；若 tv 为零则返回 "(never)"，格式化失败返回 "???"。
 * 注意：使用静态缓冲区且非线程安全，仅供即时打印。
 */
static const char *timeval_format(struct timeval *tv)
{
	struct tm tm_store;
	struct tm *tm;
	static char timebuf[256];

	if (!tv->tv_sec && !tv->tv_usec)
		return "(never)";

	tm = localtime_r(&tv->tv_sec, &tm_store);
	if (!tm
	    || strftime(timebuf, sizeof(timebuf), "%Z %a %Y-%m-%d %H:%M:%S", tm)
		       == 0) {
		return "???";
	}

	size_t offset = strlen(timebuf);
	snprintf(timebuf + offset, sizeof(timebuf) - offset, ".%ld",
		 (long int)tv->tv_usec);

	return timebuf;
}

/*
 * spf_backoff_show：把实例状态、各延时参数、两个定时器剩余时间与首次 / 最近
 * 事件时间输出到 VTY。
 * 参数：backoff 实例；vty 输出目标；prefix 每行前缀（供调用者缩进）。
 */
void spf_backoff_show(struct spf_backoff *backoff, struct vty *vty,
		      const char *prefix)
{
	vty_out(vty, "%sCurrent state:     %s\n", prefix,
		spf_backoff_state2str(backoff->state));
	vty_out(vty, "%sInit timer:        %ld msec\n", prefix,
		backoff->init_delay);
	vty_out(vty, "%sShort timer:       %ld msec\n", prefix,
		backoff->short_delay);
	vty_out(vty, "%sLong timer:        %ld msec\n", prefix,
		backoff->long_delay);
	vty_out(vty, "%sHolddown timer:    %ld msec\n", prefix,
		backoff->holddown);
	if (backoff->t_holddown) {
		struct timeval remain = event_timer_remain(backoff->t_holddown);

		vty_out(vty, "%s                   Still runs for %lld msec\n",
			prefix,
			(long long)remain.tv_sec * 1000
				+ remain.tv_usec / 1000);
	} else {
		vty_out(vty, "%s                   Inactive\n", prefix);
	}

	vty_out(vty, "%sTimeToLearn timer: %ld msec\n", prefix,
		backoff->timetolearn);
	if (backoff->t_timetolearn) {
		struct timeval remain =
			event_timer_remain(backoff->t_timetolearn);
		vty_out(vty, "%s                   Still runs for %lld msec\n",
			prefix,
			(long long)remain.tv_sec * 1000
				+ remain.tv_usec / 1000);
	} else {
		vty_out(vty, "%s                   Inactive\n", prefix);
	}

	vty_out(vty, "%sFirst event:       %s\n", prefix,
		timeval_format(&backoff->first_event_time));
	vty_out(vty, "%sLast event:        %s\n", prefix,
		timeval_format(&backoff->last_event_time));
}

/*
 * DEFUN spf_backoff_debug：命令 "debug spf-delay-ietf"。
 * 打开全局 SPF 退避调试开关，始终返回 CMD_SUCCESS。
 */
DEFUN(spf_backoff_debug,
      spf_backoff_debug_cmd,
      "debug spf-delay-ietf",
      DEBUG_STR
      "SPF Back-off Debugging\n")
{
	debug_spf_backoff = true;
	return CMD_SUCCESS;
}

/*
 * DEFUN no_spf_backoff_debug：命令 "no debug spf-delay-ietf"。
 * 关闭全局 SPF 退避调试开关，始终返回 CMD_SUCCESS。
 */
DEFUN(no_spf_backoff_debug,
      no_spf_backoff_debug_cmd,
      "no debug spf-delay-ietf",
      NO_STR
      DEBUG_STR
      "SPF Back-off Debugging\n")
{
	debug_spf_backoff = false;
	return CMD_SUCCESS;
}

/*
 * spf_backoff_write_config：把全局 SPF 退避调试配置写出到 VTY。
 * 参数：vty 输出目标。
 * 返回写出的行数（开启 debug 时为 1）。
 */
int spf_backoff_write_config(struct vty *vty)
{
	int written = 0;

	if (debug_spf_backoff) {
		vty_out(vty, "debug spf-delay-ietf\n");
		written++;
	}

	return written;
}

/*
 * spf_backoff_cmd_init：在 ENABLE 与 CONFIG 节点注册 debug / no debug
 * 两条命令。由守护进程启动时调用。
 */
void spf_backoff_cmd_init(void)
{
	install_element(ENABLE_NODE, &spf_backoff_debug_cmd);
	install_element(CONFIG_NODE, &spf_backoff_debug_cmd);
	install_element(ENABLE_NODE, &no_spf_backoff_debug_cmd);
	install_element(CONFIG_NODE, &no_spf_backoff_debug_cmd);
}

/*
 * spf_backoff_init_delay：返回首次事件延迟 init_delay（毫秒）。
 */
long spf_backoff_init_delay(struct spf_backoff *backoff)
{
	return backoff->init_delay;
}

/*
 * spf_backoff_short_delay：返回短等待延迟 short_delay（毫秒）。
 */
long spf_backoff_short_delay(struct spf_backoff *backoff)
{
	return backoff->short_delay;
}

/*
 * spf_backoff_long_delay：返回长等待延迟 long_delay（毫秒）。
 */
long spf_backoff_long_delay(struct spf_backoff *backoff)
{
	return backoff->long_delay;
}

/*
 * spf_backoff_holddown：返回 Holddown 时长（毫秒）。
 */
long spf_backoff_holddown(struct spf_backoff *backoff)
{
	return backoff->holddown;
}

/*
 * spf_backoff_timetolearn：返回 TimeToLearn 时长（毫秒）。
 */
long spf_backoff_timetolearn(struct spf_backoff *backoff)
{
	return backoff->timetolearn;
}
