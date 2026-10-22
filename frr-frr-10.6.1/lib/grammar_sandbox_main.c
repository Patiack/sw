// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Testing shim and API examples for the new CLI backend.
 *
 * Minimal main() to run grammar_sandbox standalone.
 * [split off grammar_sandbox.c 2017-01-23]
 * --
 * Copyright (C) 2016 Cumulus Networks, Inc.
 * Copyright (C) 2017 David Lamparter for NetDEF, Inc.
 */

// ============================================================
// 【文件导读】grammar_sandbox_main.c
// ------------------------------------------------------------
// 职责：
//   这是"命令语法沙箱"(grammar sandbox)的独立可执行入口。
//   grammar_sandbox.c 提供了 grammar parse / match / complete /
//   find-ambiguous 等调试命令的实现，但那些命令通常寄生在某
//   个守护进程或 vtysh 里；本文件提供一个最小 main()，把 FRR
//   库(命令系统 / vty / 事件循环 / northbound)整体拉起来，让开
//   发者能脱离真实守护进程、单独跑一个交互式 CLI 来调试"命令
//   图"(DFA)与新解析引擎。
//
// 核心流程：
//   1. event_master_create() 创建主事件循环；
//   2. zlog_aux_init() 初始化日志；
//   3. cmd_init() 建命令系统，lib_cmd_init()/nb_init() 装基础命令；
//   4. vty_init() 建 vty 子系统，vty_stdio() 把标准输入输出接成 vty；
//   5. 死循环 event_fetch()/event_call() 驱动事件。
//
// 与其他模块的关系：
//   - grammar_sandbox.c：被本 shim 间接初始化(经由 lib_cmd_init)，
//     提供 grammar 系列调试命令；
//   - command.c / command_match.c：真正干活的新老命令引擎；
//   - vty.c / lib_vty.c：终端会话与命令输出；
//   - event.c：事件循环，master 全局句柄由其创建。
//
// 阅读顺序：
//   main() -> vty_do_exit() 退出回调 -> 进入事件循环。
//
// 易踩的坑：
//   - master 必须在 vty_init() 之前创建，否则 vty 事件无处注册；
//   - vty_do_exit() 的 isexit 语义反直觉：为假时才真正 exit；
//   - 末尾 exit(0) 实际不可达，仅作防御式写法。
// ============================================================

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// command.h：命令系统(命令定义 / 安装 / 解析入口)。
// lib_vty.h：库级 vty 初始化入口(vty_init / vty_stdio 等)。
#include "command.h"
#include "lib_vty.h"

// vty_do_exit: vty_stdio() 注册的退出回调。
//   - 打印 "\nend.\n" 提示会话结束；
//   - isexit 为假(0) 时直接 exit(0) 结束进程；为真时返回，
//     交由调用方决定后续(例如 vtysh 场景继续另一个会话)。
//   注意：此处判断写的是 !isexit，语义与名字看似相反，实为
//   "不是显式退出请求时才立刻结束进程"。
static void vty_do_exit(int isexit)
{
	printf("\nend.\n");
	if (!isexit)
		exit(0);
}

// master: 全局主事件循环句柄。FRR 中几乎所有异步工作(定时器、
// socket 读写、工作队列)都挂在同一个 event loop 上；必须早于
// vty_init() 创建，因为 vty 会把终端读写事件注册到它上面。
struct event_loop *master;

// main: 语法沙箱的独立可执行入口。
//   event_master_create() 建事件循环后，依次初始化日志、命令系统、
//   vty、northbound，再用 vty_stdio() 把标准输入输出变成 vty 会话，
//   最后进入事件循环一直转，直到进程退出。
int main(int argc, char **argv)
{
	struct event event;

	master = event_master_create(NULL);
	// 创建一个新的主事件循环；FRR 所有异步 IO / 定时器都跑在它上面。

	zlog_aux_init("NONE: ", LOG_DEBUG);
	// 初始化日志基础设施：前缀 "NONE: "，只用到 LOG_DEBUG 及以上；
	// 在守护进程里通常由 zlog_init() 做更完整的初始化。

	/* Library inits. */
	cmd_init(1);
	// 建立命令系统。参数 1 表示这是库的顶层初始化(会创建节点/命令向量)。
	cmd_hostname_set("test");
	cmd_domainname_set("testdomainname");
	// 给命令系统一个假的 hostname / domainname，供命令提示符等使用。

	vty_init(master, true);
	// 初始化 vty 子系统，并把 vty 的读写事件挂到 master 事件循环上。
	lib_cmd_init();
	// 安装 lib 自带的命令(含 grammar_sandbox_init())。
	nb_init(master, NULL, 0, false, false);
	// 初始化 northbound(YANG 数据模型)子系统，事件循环用 master。

	vty_stdio(vty_do_exit);
	// 把标准输入输出接成一个 vty 会话，退出时回调 vty_do_exit。

	/* Fetch next active thread. */
	while (event_fetch(master, &event))
		// 取出下一个就绪事件；event_fetch 返回假表示事件循环已空/退出。
		event_call(&event);
		// 执行该事件对应的回调(命令处理、IO 读写等)。

	/* Not reached. */
	exit(0);
}
