// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Logging - VTY library
 * Copyright (C) 2019 Cumulus Networks, Inc.
 *                    Stephen Worley
 */

/*
 * 文件导读（log_vty.h）
 *
 * 日志相关 VTY（CLI）命令的公共接口。这里声明的是"命令注册 + 配置读写 +
 * 若干日志级别 / facility 辅助函数 + 全局日志状态"的入口，具体实现分布在
 * log_vty.c 以及各守护进程共用的日志 CLI 代码中。
 *
 * 关键内容：
 *  - log_cmd_init / log_config_write：注册日志命令、把日志配置写回 show run。
 *  - log_level_match / log_lev2sev / facility_match / facility_name：日志级别
 *    与 facility 的名称与数值互转。
 *  - zlog_rotate 钩子：SIGHUP 时触发，通知各日志目标做轮转。
 *  - zlog_cli_show 钩子：让各日志目标把自己的配置追加到 show config 输出。
 *  - 一批全局状态变量（当前 stdout / syslog 级别、命令行级别、以及各种日志
 *    目标对象），供实现侧共享。
 */
#ifndef __LOG_VTY_H__
#define __LOG_VTY_H__

#include "lib/hook.h"

#ifdef __cplusplus
extern "C" {
#endif

/* VTY 结构体前向声明（避免把 vty.h 拉进本头文件） */
struct vty;

/* 注册 log 相关的 VTY 命令（在各守护进程的 vty 初始化中调用） */
extern void log_cmd_init(void);
/* 把当前日志配置写入 show running-config 的输出 */
extern void log_config_write(struct vty *vty);
/* 由日志级别名字（如 debugging / informational）取得对应级别值；
 * 无法识别时返回负值。 */
extern int log_level_match(const char *s);
/* 展示 syslog 相关日志配置（供 show 命令使用） */
extern void log_show_syslog(struct vty *vty);
/* 把内部日志级别名转换为 syslog 的严重性（severity）名字 */
extern const char *log_lev2sev(const char *level);

/* 由 facility 名字取得对应数值；无法识别时返回负值 */
extern int facility_match(const char *str);
/* 由 facility 数值取得对应名字 */
extern const char *facility_name(int facility);

/* 声明 zlog_rotate 钩子：无参数无返回值。日志目标可注册回调以响应轮转，
 * 由 zlog_rotate() 统一触发。 */
DECLARE_HOOK(zlog_rotate, (), ());
extern void zlog_rotate(void);

/* 声明 zlog_cli_show 钩子（参数为 struct vty *）：日志目标可实现它，把自身
 * 配置追加到 show config 的输出中。 */
DECLARE_HOOK(zlog_cli_show, (struct vty * vty), (vty));

/* YANG 模型中的默认日志级别 */
/* Default logging level in the YANG model */
extern const int log_default_lvl;

/*
 * 内部日志状态（供实现侧共享，不要手工修改）：
 *  - logmsgs_with_persist_bt：带持久化 backtrace 的日志消息计数（诊断用）。
 *  - log_config_stdout_lvl / log_config_syslog_lvl：来自配置文件的
 *    stdout / syslog 日志级别。
 *  - log_cmdline_stdout_lvl / log_cmdline_syslog_lvl：来自命令行的
 *    stdout / syslog 日志级别（命令行设置优先于配置文件）。
 *  - zt_file / zt_filterfile / zt_stdout_file / zt_stdout_journald：各日志
 *    目标对象（普通日志文件、过滤文件、stdout 文件、stdout journald）。
 *  - zt_file_cmdline：来自命令行的日志文件目标。
 *  - stdout_journald_in_use：stdout 是否正被 journald 目标占用。
 */
/* Internal logging state */
extern uint logmsgs_with_persist_bt;
extern int log_config_stdout_lvl;
extern int log_config_syslog_lvl;
extern int log_cmdline_stdout_lvl;
extern int log_cmdline_syslog_lvl;
extern struct zlog_cfg_file zt_file_cmdline;
extern struct zlog_cfg_file zt_file;
extern struct zlog_cfg_filterfile zt_filterfile;
extern struct zlog_cfg_file zt_stdout_file;
extern struct zlog_cfg_5424 zt_stdout_journald;
extern bool stdout_journald_in_use;

/* 根据当前配置，把 stdout 日志目标的记录级别应用到实际目标上 */
void log_stdout_apply_level(void);
/* 清除所有来自命令行的日志目标（命令行标志仅在启动阶段生效） */
void clear_cmdline_targets(void);

/* 日志 CLI 的 YANG 模块信息，供北向接口 / YANG 框架使用 */
extern struct frr_yang_module_info frr_logging_cli_info;

#ifdef __cplusplus
}
#endif

#endif /* __LOG_VTY_H__ */
