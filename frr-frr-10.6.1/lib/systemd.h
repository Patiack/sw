// SPDX-License-Identifier: GPL-2.0-or-later
/* lib/systemd Code
 * Copyright (C) 2016 Cumulus Networks, Inc.
 * Donald Sharp
 */

/* ============================================================================
 * 【文件导读】systemd.h —— 与 systemd 打交道的薄封装
 * ============================================================================
 * 【定位】FRR 守护进程通常由 systemd 拉起。宿主要求守护进程按约定上报状态(启动
 * 完成、正在停止、周期性健康心跳)。本文件是这层封装的声明。
 *
 * 【最重要的设计点(原英文注释也强调了)】若当前系统没有使用 systemd，本文件的
 * 所有函数都退化为**空操作**。因此调用方**无需**先探测有没有 systemd，直接调用
 * 即可；把“有没有 systemd”的判断完全收拢在实现里。
 *
 * 【两个全局布尔】sd_stdout_is_journal / sd_stderr_is_journal 表示“标准输出、
 * 标准错误是否连接到了 journald”。FRR 用它决定是否需要自己把日志送进 journald，
 * 避免重复采集。
 *
 * 【函数一览】send_stopping(即将停止) / send_started(启动完成并起心跳) /
 * send_status(上报一句可读状态) / init_env(从环境变量取启动信息)。
 * ============================================================================
 */
#ifdef __cplusplus
extern "C" {
#endif

/* fd 1/2 connected to journald? */
/* 【全局标志】由启动早期的环境探测(见 systemd_init_env)填充，表示标准输出、
 * 标准错误是否直连 journald。用途是判断日志是否已被 journald 采集，以免重复处理。
 * 【易踩的坑】它们在进程生命周期内是**只读**的稳定值，外部不要改写；读取前应确保
 * 启动探测已经跑过，否则会读到默认的 false。 */
extern bool sd_stdout_is_journal;
extern bool sd_stderr_is_journal;

/*
 * Wrapper functions to systemd calls.
 *
 * Design point is that if systemd is not being used on this system
 * then these functions becomes a no-op.
 */
/* 【函数】通知 systemd“本进程即将停止”。
 * 【时机】应在优雅退出的早期调用，让 systemd 区分“主动停止”与“异常崩溃”，
 *   从而不误触重启/告警策略。
 * 【易踩的坑】无 systemd 时为空操作，无需额外判断。 */
void systemd_send_stopping(void);

/*
 *  master - The struct event_loop * to use to schedule ourself
 *  the_process - Should we send watchdog if we are not the requested
 *                process?
 */
/* 【函数】通知 systemd“启动完成”，并随后周期性地发送看门狗(健康)心跳。
 *   master : 承载心跳定时器的 event_loop。心跳必须挂在事件循环上，才能随主循环
 *            一起被调度。
 * 【易踩的坑】看门狗若漏发，systemd 会判定进程卡死并强杀之；所以本调用应在主事件
 *   循环就绪后**尽早**执行。无 systemd 时为空操作。 */
void systemd_send_started(struct event_loop *master);

/*
 * status - A status string to send to systemd
 */
/* 【函数】向 systemd 上报一句人类可读的状态(即 systemctl status 里显示的那一行)。
 *   参数 status : 状态文本，需为有效的以结尾符终止的字符串。
 * 【定位】纯观测用途，不承载任何控制语义，失败也不影响业务。无 systemd 时为空操作。 */
void systemd_send_status(const char *status);

/*
 * grab startup state from env vars
 */
/* 【函数】从环境变量中读取启动期信息(是否连接 journald、看门狗超时等)，填充上面
 * 两个全局标志。
 * 【时机】应在进程**极早期**、日志系统初始化之前调用一次。无 systemd 时为空操作。 */
void systemd_init_env(void);

#ifdef __cplusplus
}
#endif
