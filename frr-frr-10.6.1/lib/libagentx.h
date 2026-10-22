// SPDX-License-Identifier: GPL-2.0-or-later
/* SNMP cli support
 * Copyright (C) 2024 Donald Sharp <sharpd@nvidia.com> NVIDIA Corporation
 */
/*
 * ======================== 文件导读 ========================
 * 1. 职责
 *    本头文件是 SNMP AgentX 功能的对外契约层，只暴露最小的 CLI 接口：
 *    一个初始化函数、一个全局启用标志、两个 CLI 钩子。真正的 NetSNMP
 *    子代理逻辑在 agentx.c 中，CLI 装配逻辑在 libagentx.c 中。
 * 2. 核心数据结构
 *    - agentx_enabled：全局布尔量，表示 AgentX 是否已启用。
 *    - agentx_cli_enabled / agentx_cli_disabled：两个 hook 声明，
 *      分别由 agentx 命令 / no agentx 命令触发。
 * 3. 与其他模块的关系
 *    - libagentx.c 定义并注册这两个 hook，并初始化全局标志。
 *    - agentx.c 在 SNMP_AGENTX 宏打开时提供 NetSNMP 子代理实现，
 *      并通过 smux_enabled 等接口回读状态。
 *    - 各守护进程调用 libagentx_init 安装配置节点与命令。
 * 4. 阅读顺序
 *    先本文件看契约，再 libagentx.c 看 CLI 落地，最后 agentx.c 看
 *    NetSNMP 事件循环交互。
 * 5. 易踩的坑
 *    - agentx_enabled 在此仅为 extern 声明，定义在 libagentx.c。
 *    - hook 需先 DEFINE 再 REGISTER，且仅在模块加载后可用；否则启用
 *      命令只会打印 Module is not loaded 的提示而不生效。
 *    - 该功能启用后不支持通过 CLI 关闭。
 * ========================================================
 */
#ifndef __LIBAGENTX_H__
#define __LIBAGENTX_H__

/*
 * libagentx_init：安装 agentx 配置节点与 agentx / no agentx 两条命令，
 *   并复位全局启用标志。由各守护进程在其初始化阶段调用。
 * agentx_enabled：全局启用开关，真实定义在 libagentx.c，
 *   此处仅作 extern 声明供其他模块读取。
 */
extern void libagentx_init(void);
extern bool agentx_enabled;

/*
 * agentx_cli_enabled：配置 agentx 命令时应调用的 hook 集合。
 *   若当前没有任何实现注册该 hook，说明 AgentX 模块未加载。
 * agentx_cli_disabled：配置 no agentx 命令时应调用的 hook 集合。
 *   返回值用于判断禁用是否被接受。
 */
DECLARE_HOOK(agentx_cli_enabled, (), ());
DECLARE_HOOK(agentx_cli_disabled, (), ());

#endif
