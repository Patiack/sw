// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP4 SNMP support
 * Copyright (C) 1999, 2000 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_snmp.c —— bgpd 的 SNMP 模块总入口
 * ============================================================================
 * 【它解决什么问题】把运行中的 BGP 内部状态通过 SNMP（以 AgentX 子代理的方式接入
 *   系统的 snmpd）暴露给网管系统轮询；同时把“邻居建立 / 邻居回退”两个事件作为
 *   trap（告警）主动上报。本模块是 bgpd 的一个可加载子模块（见文件末尾
 *   FRR_MODULE_SETUP）。SNMP 未编译进来时（没有 --enable-snmp）本文件不参与构建。
 *
 * 【为什么这样设计】SNMP 只是旁路观测能力，绝不能影响 BGP 协议本身的行为，所以
 *   本模块只做两件事：(1) 注册各 MIB 的取值回调；(2) 用 hook 钩住 BGP 事件点，在
 *   事件发生时决定是否发 trap。真正取数的回调分散在三个文件：
 *     bgp_snmp_bgp4.c    —— BGP4-MIB（RFC4273，IPv4 单播）
 *     bgp_snmp_bgp4v2.c  —— BGP4V2-MIB（支持 4 字节 AS / IPv6 / 无类前缀）
 *     bgp_mplsvpn_snmp.c —— MPLS L3VPN MIB（RFC4382，VRF / RT 表）
 *   本文件只做串联与开关，不含具体 OID 取值逻辑。
 *
 * 【本文件职责】
 *   1) 三个 CLI 命令：分别开关 rfc4273 / rfc4382 / bgp4-mibv2 三套 trap，并把开关
 *      状态写进配置（config write）。
 *   2) bgpTrapEstablished / bgpTrapBackwardTransition：面向 BGP 侧的 trap 入口，
 *      内部再按开关分发给 BGP4-MIB 与 BGP4V2-MIB 两套实现。
 *   3) bgp_snmp_init：真正初始化（smux_init 后依次初始化三个子 MIB）。
 *   4) bgp_snmp_module_init：模块装载时注册 hook 与初始化回调。
 *
 * 【关键数据结构】本文件没有自己的结构体；操作的是全局 bm->options 上的位标志
 *   （BGP_OPT_TRAPS_*），以及各子 MIB 文件里的 static 变量表。
 *
 * 【关键概念：hook】FRR 用 hook（见 lib/hook.h）做事件点订阅。这里订阅了：
 *   peer_status_changed      邻居状态变化 → 发 Established 告警
 *   peer_backward_transition 邻居状态回退 → 发 BackwardTransition 告警
 *   frr_late_init            daemon 初始化后期 → 执行 SNMP 初始化
 *   bgp_snmp_traps_config_write 写配置时 → 输出 trap 开关
 *
 * 【推荐阅读顺序】先看三个 DEFPY 命令理解三套 trap 开关，再看
 *   bgp_snmp_module_init / bgp_snmp_init 理解装载与初始化时序，最后进入各子 MIB 文件。
 * ============================================================================
 */

#include <zebra.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include "if.h"
#include "log.h"
#include "prefix.h"
#include "command.h"
#include "frrevent.h"
#include "smux.h"
#include "filter.h"
#include "hook.h"
#include "libfrr.h"
#include "lib/version.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_fsm.h"
#include "bgpd/bgp_snmp.h"
#include "bgpd/bgp_snmp_bgp4.h"
#include "bgpd/bgp_snmp_bgp4v2.h"
#include "bgpd/bgp_mplsvpn_snmp.h"
#include "bgpd/bgp_snmp_clippy.c"



static int bgp_cli_snmp_traps_config_write(struct vty *vty);

/* 【命令】bgp snmp traps rfc4273：开关 RFC4273（BGP4-MIB）的 trap，默认开启。
 * 只翻转 bm->options 上的 BGP_OPT_TRAPS_RFC4273 位；是否真正发 trap 由发送侧
 * （bgp_snmp_bgp4.c 的 bgp4TrapEstablished 等）再判断。 */
DEFPY(bgp_snmp_traps_rfc4273, bgp_snmp_traps_rfc4273_cmd,
      "[no$no] bgp snmp traps rfc4273",
      NO_STR BGP_STR
      "Configure BGP SNMP\n"
      "Configure SNMP traps for BGP\n"
      "Configure use of rfc4273 SNMP traps for BGP\n")
{
	if (no) {
		UNSET_FLAG(bm->options, BGP_OPT_TRAPS_RFC4273);
		return CMD_SUCCESS;
	}
	SET_FLAG(bm->options, BGP_OPT_TRAPS_RFC4273);
	return CMD_SUCCESS;
}

/* 【命令】bgp snmp traps rfc4382：开关 RFC4382（MPLS L3VPN MIB）相关 trap，默认开。
 * 对应 bgp_mplsvpn_snmp.c 里 VRF up / down 的告警。 */
DEFPY(bgp_snmp_traps_rfc4382, bgp_snmp_traps_rfc4382_cmd,
      "[no$no] bgp snmp traps rfc4382",
      NO_STR BGP_STR
      "Configure BGP SNMP\n"
      "Configure SNMP traps for BGP\n"
      "Configure use of rfc4382 SNMP traps for BGP\n")
{
	if (no) {
		UNSET_FLAG(bm->options, BGP_OPT_TRAPS_RFC4382);
		return CMD_SUCCESS;
	}
	SET_FLAG(bm->options, BGP_OPT_TRAPS_RFC4382);
	return CMD_SUCCESS;
}

/* 【命令】bgp snmp traps bgp4-mibv2：开关 BGP4V2-MIB 的 trap，默认关闭。
 * 对应 bgp_snmp_bgp4v2.c。BGP4V2-MIB 支持 4 字节 AS、IPv6、无类前缀。 */
DEFPY(bgp_snmp_traps_bgp4_mibv2, bgp_snmp_traps_bgp4_mibv2_cmd,
      "[no$no] bgp snmp traps bgp4-mibv2",
      NO_STR BGP_STR
      "Configure BGP SNMP\n"
      "Configure SNMP traps for BGP\n"
      "Configure use of BGP4-MIBv2 SNMP traps for BGP\n")
{
	if (no) {
		UNSET_FLAG(bm->options, BGP_OPT_TRAPS_BGP4MIBV2);
		return CMD_SUCCESS;
	}
	SET_FLAG(bm->options, BGP_OPT_TRAPS_BGP4MIBV2);
	return CMD_SUCCESS;
}

/* 【函数】bgp_snmp_traps_init：把三个 trap 开关命令安装到 CONFIG_NODE，并设定默认值。
 * 默认：rfc4273 开、rfc4382 开、bgp4-mibv2 关（见下面“disabled by default”注释）。
 * 由 bgp_snmp_init 在 frr_late_init 阶段调用一次。 */
static void bgp_snmp_traps_init(void)
{
	install_element(CONFIG_NODE, &bgp_snmp_traps_rfc4273_cmd);
	install_element(CONFIG_NODE, &bgp_snmp_traps_bgp4_mibv2_cmd);
	install_element(CONFIG_NODE, &bgp_snmp_traps_rfc4382_cmd);

	SET_FLAG(bm->options, BGP_OPT_TRAPS_RFC4273);
	/* BGP4MIBv2 traps are disabled by default */

	SET_FLAG(bm->options, BGP_OPT_TRAPS_RFC4382);
}

/* 【函数】bgp_cli_snmp_traps_config_write：把三个 trap 开关的“非默认”状态写入配置。
 * 返回写入的行数（write），供上层判断是否需要输出该配置段。
 * 注意默认开启的两项在“关闭”时写 "no ..."；默认关闭的 bgp4-mibv2 在“开启”时才写。 */
int bgp_cli_snmp_traps_config_write(struct vty *vty)
{
	int write = 0;

	if (!CHECK_FLAG(bm->options, BGP_OPT_TRAPS_RFC4273)) {
		vty_out(vty, "no bgp snmp traps rfc4273\n");
		write++;
	}
	if (CHECK_FLAG(bm->options, BGP_OPT_TRAPS_BGP4MIBV2)) {
		vty_out(vty, "bgp snmp traps bgp4-mibv2\n");
		write++;
	}
	if (!CHECK_FLAG(bm->options, BGP_OPT_TRAPS_RFC4382)) {
		vty_out(vty, "no bgp snmp traps rfc4382\n");
		write++;
	}

	return write;
}

/* 【函数】bgpTrapEstablished：邻居进入 Established 时被 hook(peer_status_changed) 调用。
 * 按开关把事件分发给 BGP4-MIB(bgp4TrapEstablished) 与 BGP4V2-MIB(bgpv2TrapEstablished)。
 * 返回 0——发 trap 是尽力而为，不影响 BGP 会话。 */
int bgpTrapEstablished(struct peer *peer)
{
	if (CHECK_FLAG(bm->options, BGP_OPT_TRAPS_RFC4273))
		bgp4TrapEstablished(peer);

	if (CHECK_FLAG(bm->options, BGP_OPT_TRAPS_BGP4MIBV2))
		bgpv2TrapEstablished(peer);

	return 0;
}

/* 【函数】bgpTrapBackwardTransition：FSM 向后迁移时被 hook(peer_backward_transition) 调用。
 * 同样按开关分发给两套 MIB 的 BackwardTransition 实现。 */
int bgpTrapBackwardTransition(struct peer *peer)
{
	if (CHECK_FLAG(bm->options, BGP_OPT_TRAPS_RFC4273))
		bgp4TrapBackwardTransition(peer);

	if (CHECK_FLAG(bm->options, BGP_OPT_TRAPS_BGP4MIBV2))
		bgpv2TrapBackwardTransition(peer);

	return 0;
}

/* 【函数】bgp_snmp_init：SNMP 子系统真正初始化（在 frr_late_init 阶段执行）。
 * 顺序：smux_init（建立与 snmpd 的 AgentX 通道并挂到事件循环）→ 初始化 trap 开关
 * → 依次注册 BGP4-MIB、BGP4V2-MIB、MPLS L3VPN MIB 三套变量表。 */
static int bgp_snmp_init(struct event_loop *tm)
{
	smux_init(tm);
	bgp_snmp_traps_init();
	bgp_snmp_bgp4_init(tm);
	bgp_snmp_bgp4v2_init(tm);
	bgp_mpls_l3vpn_module_init();
	return 0;
}

/* 【函数】bgp_snmp_module_init：模块装载入口（被 FRR_MODULE_SETUP 引用）。
 * 只做 hook 注册，把上面的函数挂到对应事件点；真正的初始化推迟到 frr_late_init。 */
static int bgp_snmp_module_init(void)
{
	hook_register(peer_status_changed, bgpTrapEstablished);
	hook_register(peer_backward_transition, bgpTrapBackwardTransition);
	hook_register(frr_late_init, bgp_snmp_init);
	hook_register(bgp_snmp_traps_config_write,
		      bgp_cli_snmp_traps_config_write);
	return 0;
}

/* 【模块声明】FRR 动态模块脚手架：声明本模块名称、版本、描述与初始化函数。
 * 名字 "bgpd_snmp" 即“bgpd 的 AgentX SNMP 模块”；.init 指向 bgp_snmp_module_init。 */
FRR_MODULE_SETUP(.name = "bgpd_snmp", .version = FRR_VERSION,
		 .description = "bgpd AgentX SNMP module",
		 .init = bgp_snmp_module_init,
);
