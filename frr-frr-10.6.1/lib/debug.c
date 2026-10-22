// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Debugging utilities.
 * Copyright (C) 2018  Cumulus Networks, Inc.
 * Quentin Young
 */
/* ============================================================================
 * 【文件导读】debug.c —— 通用的 “debug all” / debug 状态显示与配置回写
 * ============================================================================
 * 【它和 debug.h 的分工】debug.h 提供每个模块声明自己调试项所需的宏
 * (DEBUG_MODE_* 位、DEFINE_MTYPE 之类的静态模板)；本文件提供**所有模块共享**
 * 的那一小块框架：一个全局的 debug 链表、一条 "debug all" 命令、以及把当前
 * 调试状态既显示给人看(vty)又回写进配置(config_write)的两个函数。
 *
 * 【核心数据结构】debug_head 是一个统一链表，每个模块在启动时调 debug_install()
 * 把自己的 `struct debug` 挂进来。此后：
 *   - "debug all"       : 遍历链表，**在当前位置的 CLI 节点对应的位上**全部置位；
 *   - debug_status_write: 遍历链表，把“还开着”的项打印成 `show debugging` 风格；
 *   - config_write_debug: 遍历链表，把“已写进配置”的项原样回写配置文件。
 * 三个动作都只是“遍历 + 看位”，本文件因此非常短。
 *
 * 【关键概念：一个 debug 项有“两套位”】
 *   DEBUG_MODE_ALL  : 运行时状态位(现在真的在打日志吗)。
 *   DEBUG_MODE_CONF : 配置位(这条 debug 是否已被写进配置文件)。
 * 二者为什么要分开：`debug all` 会把运行时位全部打开，但**不应**把几十条
 * debug 配置都写进配置文件(那样配置会爆炸)。所以“调试中”与“被配置”是两件事，
 * 回写时只看 CONF 位。这也解释了 DEFUN_NOSH 的出现——它让这条命令不出现在
 * 自动生成的 CLI 帮助里(典型用法)。
 *
 * 【阅读顺序】全局链表与 DECLARE_LIST → "debug all" 命令 → 两个遍历函数
 * → debug_node(配置节点) → install/init(注册入口)。
 * ============================================================================
 */
#include <zebra.h>
#include "typesafe.h"
#include "debug.h"
#include "command.h"

/* 【全局状态】所有已注册 debug 项的链表头。用 typesafe 的 DECLARE_LIST 生成
 * 类型安全的链表操作(见下方 DECLARE_LIST)，因此这里只声明头结点，不写操作代码。 */
static struct debug_list_head debug_head;

/* 【类型安全链表】把 struct debug 的 item 成员实例化为 debug_list 这一族
 * 类型安全的链表 API(debug_list_add_tail / debug_list_init / frr_each 等)。
 * 【易踩的坑】参数顺序是 (名字, 元素类型, 链表成员名)，成员名必须与
 * struct debug 里那个 struct debug_list_item 字段的名字**逐字一致**(这里是
 * item)，写错会得到难以理解的编译错误。 */
DECLARE_LIST(debug_list, struct debug, item);

/* All code in this section should be reentrant and MT-safe */

/* 【命令】"debug all" / "no debug all"：把当前 CLI 节点对应的调试位整体打开
 * 或关闭。
 * 【命令串有两处要点】
 *   ① `[no]` 是可选的 "no " 前缀——同一个 DEFUN 同时承担开关两个方向，
 *      靠下面判断 argv[0] 的文本来分辨，而不是写两条命令。
 *   ② `NO_STR DEBUG_STR` 是帮助文本的拼接宏，等价于直接写相应字符串常量。
 * 【DEFUN_NOSH 的特殊之处】带 _NOSH 后缀表示这条命令**不进入 CLI 补全与帮助**，
 *   属于“内部/隐藏命令”——通常因为它牵扯面太大，不想让用户随手补全出来。
 * 【关键映射：vty->node → 调试位】DEBUG_NODE2MODE(vty->node) 把“我在哪个 CLI
 *   节点敲的命令”翻译成一个 mode 掩码。也就是说 `debug all` 在 config-if 下与
 *   在 enable 下**影响的调试类别不同**——这正是它按节点区分 bit 的原因。
 * 【遍历里的两步】
 *   DEBUG_MODE_SET(debug, mode, set) : 把 mode 对应的位按 set 置/清。
 *   DEBUG_MODE_CHECK(debug, DEBUG_MODE_ALL) : 检查该 debug 项是否还有任何位为开。
 *   若全关则 DEBUG_CLEAR(debug) 清掉残留的 options(原注释：不保留选项)。
 * 【易踩的坑】这条命令**不是幂等的语义**那么简单：`debug all` 打开的是“与当前
 *   节点相关”的位，不是“所有模块的所有位”。要全部打开需在相应节点分别执行——
 *   这是读代码时最容易误解的一点。 */
DEFUN_NOSH (debug_all,
	    debug_all_cmd,
	    "[no] debug all",
	    NO_STR DEBUG_STR
	    "Toggle all debugging output\n")
{
	struct debug *debug;
	bool set = !strmatch(argv[0]->text, "no");
	uint32_t mode = DEBUG_NODE2MODE(vty->node);

	frr_each (debug_list, &debug_head, debug) {
		DEBUG_MODE_SET(debug, mode, set);

		/* If all modes have been turned off, don't preserve options. */
		if (!DEBUG_MODE_CHECK(debug, DEBUG_MODE_ALL))
			DEBUG_CLEAR(debug);
	}

	return CMD_SUCCESS;
}

/* ------------------------------------------------------------------------- */

/* 【函数】打印当前处于开启状态的调试项(供 show 类命令使用)。
 *   vty: 输出目标终端。
 * 【要点】判据是 DEBUG_MODE_ALL(**运行时**位)而非 CONF 位——所以它反映的是
 *   此刻真实的日志行为。输出格式 `  <desc> debugging is on` 是 FRR 用户熟悉的
 *   固定样式；改这里的缩进/措辞会与其它 show 输出风格不一致，需谨慎。
 * 【易踩的坑】desc 为 NULL 的 debug 项会打印 "(null)"；按约定各模块注册时必须
 *   提供 desc(见 debug.h 的用法约束)。 */
void debug_status_write(struct vty *vty)
{
	struct debug *debug;

	frr_each (debug_list, &debug_head, debug) {
		if (DEBUG_MODE_CHECK(debug, DEBUG_MODE_ALL))
			vty_out(vty, "  %s debugging is on\n", debug->desc);
	}
}

/* 【函数】把调试配置回写进配置文件(show running-config 的一部分)。
 *   vty: 输出目标(通常指向配置缓冲区)。
 *   返回: 恒为 0。
 * 【要点】判据是 DEBUG_MODE_CONF(**配置**位)——与上面 debug_status_write 用的是
 *   另一套位，这是理解本文件的关键区别(见文件导读)。
 * 【为什么是“原样打印 debug->conf”】con 字段是该模块预先准备好的完整命令文本
 *   (如 "debug bgp updates")，此处不做任何加工，直接输出——把“命令文本怎么拼”
 *   的责任留在各模块，本文件只负责遍历与判定。
 * 【易踩的坑】con 为 NULL 会让 vty_out 的 %s 打印 "(null)"，从而污染配置文件；
 *   各模块注册时若只设了位没设 conf 就会踩到。 */
static int config_write_debug(struct vty *vty)
{
	struct debug *debug;

	frr_each (debug_list, &debug_head, debug) {
		if (DEBUG_MODE_CHECK(debug, DEBUG_MODE_CONF))
			vty_out(vty, "%s\n", debug->conf);
	}

	return 0;
}

/* 【命令节点】FRR 的 CLI 节点(配置模式)声明。
 *   .name        : 节点名，出现在帮助/错误信息里。
 *   .node        : 节点枚举值 LIB_DEBUG_NODE，必须与 command.h 的 node_type 中
 *                  同名项一致(它是节点表的索引)。
 *   .prompt      : prompt 为空串表示“进入该节点后提示符不变”(不显示专属前缀)。
 *   .config_write : 该节点的配置回写函数——正是上面那个 config_write_debug。
 * 【易踩的坑】install_node() 内部通常只做“把节点挂进全局表”，不会失败；但
 *   .node 与 .name 若与别处冲突(LIB_DEBUG_NODE 被复用)，行为会很隐蔽。 */
static struct cmd_node debug_node = {
	.name = "debug",
	.node = LIB_DEBUG_NODE,
	.prompt = "",
	.config_write = config_write_debug,
};

/* 【函数】把一个模块的 debug 结构注册进全局链表。
 *   debug: 该模块的 `struct debug`(通常由 debug.h 的宏在编译期生成静态实例)。
 * 【要点】只是**尾插**进 debug_head，不校验重复、不设初始位——也就是说注册
 *   之后所有调试默认是关的，直到用户敲 debug 命令。
 * 【调用时机】各模块启动早期调用一次。若模块忘了调用，它的 debug 项对
 *   "debug all" 与 show 命令都不可见(但该模块自己的调试开关仍可能由自己的
 *   命令直接控制)——这是排查“为什么 debug all 管不到某模块”的常见原因。 */
void debug_install(struct debug *debug)
{
	debug_list_add_tail(&debug_head, debug);
}

/* 【函数】初始化调试子系统。由守护进程启动流程调用**一次**。
 * 【三步】① 初始化链表头；② 安装 debug_node(配置节点)；③ 在 ENABLE 与 CONFIG
 *   两个节点都安装 "debug all" 命令。
 * 【为什么同时装到两个节点】ENABLE(特权 exec)下可以直接 `debug all`，CONFIG
 *   (全局配置)下则作为配置动作——两者共用同一个 DEFUN，靠 vty->node 区分作用域
 *   (见 debug_all 的 DEBUG_NODE2MODE)。
 * 【易踩的坑】必须在 debug_install() 之前调用——否则链表未初始化就把模块挂进去，
 *   链表头的初始化会把它冲掉。这个顺序错误在自定义启动流程里偶有发生。 */
void debug_init(void)
{
	debug_list_init(&debug_head);

	install_node(&debug_node);

	install_element(ENABLE_NODE, &debug_all_cmd);
	install_element(CONFIG_NODE, &debug_all_cmd);
}
