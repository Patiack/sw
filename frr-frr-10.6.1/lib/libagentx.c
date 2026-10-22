// SPDX-License-Identifier: GPL-2.0-or-later
/* SNMP cli support
 * Copyright (C) 2024 Donald Sharp <sharpd@nvidia.com> NVIDIA Corporation
 */
/*
 * ========================== 文件导读 ==========================
 * 1. 职责
 *    实现 SNMP AgentX 的 CLI 装配层：定义并注册两个 hook，维护全局
 *    启用标志，安装 smux 配置节点，提供 agentx / no agentx 命令。
 * 2. 核心数据结构
 *    - agentx_enabled：全局 bool，功能启用状态。
 *    - agentx_node：struct cmd_node，配置写出命令的载体。
 *    - agentx_cli_enabled / agentx_cli_disabled：hook，衔接 agentx.c
 *      中真正的启停逻辑。
 * 3. 与其他模块的关系
 *    - 对外契约声明在 libagentx.h，各守护进程调用 libagentx_init 装配。
 *    - 真正的 NetSNMP 子代理实现在 agentx.c，hook 是两层之间的桥。
 *    - 依赖 hook.h 机制，实现按需加载 / 解耦。
 * 4. 阅读顺序
 *    按文件顺序读：hook 定义 -> 全局变量 -> 配置写出 -> 命令节点
 *    -> DEFUN 命令 -> 初始化函数。
 * 5. 易踩的坑
 *    - 命令里先判断 hook_have_hooks，模块没加载时直接返回成功。
 *    - no agentx 无论如何都会打印不可禁用提示，并依赖 hook 返回值。
 *    - 节点名沿用历史名称 smux，与 AgentX 是同一套机制。
 * ============================================================
 */
#include <zebra.h>

#include "lib/hook.h"
#include "lib/libagentx.h"
#include "command.h"

/*
 * 定义两个 hook 的存储实体。DECLARE_HOOK 只做声明，这里才真正分配
 * 全局 hook 表；agentx.c 的 smux_init 会向它们注册实现函数。
 */
DEFINE_HOOK(agentx_cli_enabled, (), ());
DEFINE_HOOK(agentx_cli_disabled, (), ());

/*
 * 全局启用标志的真实定义。libagentx.h 中的 extern 声明指向这里，
 * agentx.c 也通过它判断子代理是否已启动。
 */
bool agentx_enabled;

/* AgentX node. */
/*
 * config_write_agentx：配置写出回调。启用状态下写出 agentx 一行，
 * 使配置能被 show running-config 与写文件保存。始终返回 1。
 */
static int config_write_agentx(struct vty *vty)
{
	if (agentx_enabled)
		vty_out(vty, "agentx\n");
	return 1;
}

/*
 * agentx_node：配置节点描述。node 使用 SMUX_NODE，name 为 smux，
 * prompt 为空表示该节点不进入子视图，config_write 指向写出回调。
 */
static struct cmd_node agentx_node = {
	.name = "smux",
	.node = SMUX_NODE,
	.prompt = "",
	.config_write = config_write_agentx,
};

/*
 * agentx enable 命令。若没有任何模块注册 agentx_cli_enabled hook，
 * 说明 AgentX 模块未加载，仅记录一条提示日志后返回成功；否则调用
 * hook 链真正启用子代理。
 */
DEFUN(agentx_enable, agentx_enable_cmd, "agentx",
      "SNMP AgentX protocol settings\n")
{
	if (!hook_have_hooks(agentx_cli_enabled)) {
		zlog_info(
			"agentx specified but the agentx Module is not loaded, is this intentional?");

		return CMD_SUCCESS;
	}

	hook_call(agentx_cli_enabled);

	return CMD_SUCCESS;
}

/*
 * no agentx 命令。该功能不支持运行时关闭，先打印提示，再调用
 * agentx_cli_disabled hook；hook 调用失败时返回配置失败码。
 */
DEFUN(no_agentx, no_agentx_cmd, "no agentx",
      NO_STR "SNMP AgentX protocol settings\n")
{
	vty_out(vty, "SNMP AgentX support cannot be disabled once enabled\n");
	if (!hook_call(agentx_cli_disabled))
		return CMD_WARNING_CONFIG_FAILED;

	return CMD_SUCCESS;
}

/*
 * libagentx_init：模块初始化入口。复位启用标志，安装配置节点，
 * 并在 CONFIG_NODE 下安装 agentx 与 no agentx 两条命令。
 */
void libagentx_init(void)
{
	agentx_enabled = false;

	install_node(&agentx_node);
	install_element(CONFIG_NODE, &agentx_enable_cmd);
	install_element(CONFIG_NODE, &no_agentx_cmd);
}
