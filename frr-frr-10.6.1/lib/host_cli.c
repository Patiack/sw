// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * June 12 2025, Christian Hopps <chopps@labn.net>
 *
 * Copyright (c) 2025, LabN Consulting, L.L.C.
 *
 */
/*
 * ============================================================
 * 【文件导读】host_cli.c —— 把“宿主(host)”配置暴露给 vty / CLI
 * ============================================================
 *
 * 一、职责
 *   为 FRR 的全局“宿主”配置提供命令行入口。这里只有一条配置：
 *   allow-reserved-ranges，用于允许使用 IPv4 Class E(240.0.0.0/4)
 *   保留地址空间。命令本身不直接改数据结构，而是通过北向接口
 *   (northbound / YANG)把变更入队并应用。
 *
 * 二、核心机制
 *   - DEFPY 宏定义命令 allow_reserved_ranges_cmd，绑定 YANG 路径
 *     /frr-host:host/allow-reserved-ranges。
 *   - frr_host_cli_info：本模块的 YANG 模块注册信息，把
 *     cli_show 回调挂到对应 xpath 上，使 show running-config 能
 *     正确回显配置。
 *   - host_cli_init：把命令安装到 CONFIG_NODE。
 *
 * 三、与其他模块的关系
 *   - 依赖 northbound_cli.h 的 nb_cli_enqueue_change / nb_cli_apply_changes。
 *   - host_cli_clippy.c 是 clippy 工具为 DEFPY 生成的参数解析代码。
 *   - 实际取值由 frr-host YANG 模型与 mgmtd 北向层承载。
 *
 * 四、阅读顺序
 *   DEFPY 命令(输入) -> cli_show 回调(输出回显) -> 模块信息表
 *   -> host_cli_init(注册入口)。
 *
 * 五、易踩的坑
 *   - 命令只改 YANG 路径，不直接写内存；逻辑真正落在北向层。
 *   - ignore_cfg_cbs=true 表示本模块不走传统 cfg 回调，全由
 *     northbound 统一处理。
 *   - allow-reserved-ranges 影响全局路由表对保留网段的接受度。
 * ============================================================
 */
#include <zebra.h>
#include "command.h"
#include "northbound.h"
#include "northbound_cli.h"
#include "vty.h"

#include "lib/host_cli_clippy.c"

/*
 * DEFPY: allow-reserved-ranges 命令。
 * 语法 "[no] allow-reserved-ranges"；no 前缀由 no 变量体现。
 * 行为：把 /frr-host:host/allow-reserved-ranges 置为 "true" / "false"
 * 入队后立即应用。NO_STR 提供 "no" 关键字文档字符串。
 */
DEFPY(allow_reserved_ranges,
      allow_reserved_ranges_cmd,
      "[no] allow-reserved-ranges",
      NO_STR
      "Allow using IPv4 (Class E) reserved IP space\n")
{
	nb_cli_enqueue_change(vty, "/frr-host:host/allow-reserved-ranges", NB_OP_MODIFY,
			      no ? "false" : "true");
	nb_cli_apply_changes(vty, NULL);

	return CMD_SUCCESS;
}

/*
 * cli_show 回调：show running-config 时回显本项配置。
 * enable 为真输出 "allow-reserved-ranges"，否则在 show_defaults
 * 模式下输出 "no allow-reserved-ranges"。
 */
static void host_allow_reserved_ranges_cli_write(struct vty *vty, const struct lyd_node *dnode,
						 bool show_defaults)
{
	bool enable = yang_dnode_get_bool(dnode, NULL);

	if (enable)
		vty_out(vty, "allow-reserved-ranges\n");
	else if (show_defaults)
		vty_out(vty, "no allow-reserved-ranges\n");
}

/* clang-format off */
/*
 * frr_host_cli_info：本 CLI 模块对应的 YANG 模块信息。
 * name="frr-host"；ignore_cfg_cbs 跳过旧式 cfg 回调；
 * nodes 把 cli_show 回调绑定到 allow-reserved-ranges 的 xpath，
 * 以 NULL 结尾的哨兵项表示节点表结束。
 */
const struct frr_yang_module_info frr_host_cli_info = {
	.name = "frr-host",
	.ignore_cfg_cbs = true,
	.nodes = {
		{ .xpath = "/frr-host:host/allow-reserved-ranges",
		  .cbs.cli_show = host_allow_reserved_ranges_cli_write
		},
		{ .xpath = NULL },
	}
};
/* clang-format on */

/* 模块初始化：把 allow-reserved-ranges 命令安装到 CONFIG_NODE */
void host_cli_init(void)
{
	install_element(CONFIG_NODE, &allow_reserved_ranges_cmd);
}
