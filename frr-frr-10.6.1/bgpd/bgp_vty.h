// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP VTY interface.
 * Copyright (C) 1996, 97, 98, 99, 2000 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_vty.h —— bgpd CLI(VTY)层的对外声明：命令字符串宏 + 辅助函数原型
 * ============================================================================
 * 【职责与定位】本文件是 bgpd 命令行层的**头文件**，本身几乎没有可执行代码，
 *   只提供三类东西给 bgp_vty.c(以及少数其它 bgpd 文件)使用：
 *     1) 一组 **命令语法/帮助字符串宏**(BGP_AFI_CMD_STR、BGP_INSTANCE_HELP_STR 等)，
 *        它们被 DEFUN/DEFPY 宏的第二个/第三个参数直接拼接使用——把命令词法集中
 *        定义，避免同一段命令串在多处手写而产生分歧。改这些宏会**同时**改变所有
 *        引用它的命令的语法与 help 输出，务必谨慎。
 *     2) 一组 **配置默认值档案**(FRR_CFG_DEFAULT_*)，为 keepalive/holdtime/
 *        connect-retry 等提供“按 profile(如 datacenter) 覆盖”的默认值。
 *     3) 一组 **辅助函数原型**(bgp_vty_return、peer_and_group_lookup_vty、
 *        argv_find_and_parse_afi/safi 等)，供各个命令处理函数复用参数解析与
 *        错误回显逻辑。
 *
 * 【核心概念】
 *   - AFI/SAFI：地址族(ipv4/ipv6)与子地址族(unicast/multicast/vpn/labeled-unicast/
 *     flowspec)。命令里大量出现 "<ipv4|ipv6> <unicast|...>"，就是把 AFI 与 SAFI
 *     的文字形式提供出来。
 *   - Graceful Restart(GR) 报文宏族(VTY_BGP_GR_*、SHOW_GR_HEADER)：用于在配置
 *     命令里检测“是否存在启用了 GR 的邻居”，并据此决定要不要向 zebra 通告 GR
 *     能力。这是本文件里最容易写错的一组宏(见下)。
 *
 * 【本文件在主流程中的位置】被 bgp_vty.c 包含；bgp_vty.c 里的 DEFUN 命令用这里的
 *   字符串宏拼命令，命令处理函数调用这里的解析/回显辅助函数。bgp_vty_init() 是
 *   bgpd 启动时注册所有命令的总入口(实现在 .c 中)。
 *
 * 【易错点 / 阅读建议】
 *   - 宏里对 AFI/SAFI 的 help 字符串是把单个 help 重复 N 次(BGP_AF_STR 等)，改动
 *     help 数量必须与命令词数对齐，否则 CLI 的上下文帮助会错位。
 *   - VTY_BGP_GR_* 宏含控制流与副作用(会真的调用 bgp_zebra_send_capabilities())，
 *     在 do/while(0) 中运行；阅读时要留意它们**不是纯字符串宏**。
 *   建议阅读顺序：命令字符串宏 → FRR_CFG_DEFAULT_* 默认值 → GR 宏族 → 函数原型。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_VTY_H
#define _QUAGGA_BGP_VTY_H

#include "bgpd/bgpd.h"
#include "stream.h"
struct bgp;

/* 【配置默认值档案】FRR_CFG_DEFAULT_ULONG 定义“某个可配置项的默认取值”。
 * 它按 <版本号/配置档案> 从上到下**首个匹配**的条目生效：
 *   - 当处于 "datacenter" profile 时，keepalive=3 / holdtime=9 / connect-retry=10
 *     （收敛更快，适配数据中心）；
 *   - 否则回退到统一默认值 BGP_DEFAULT_*。最后一条无 match 条件的即为兜底默认。
 * 这里只是“声明默认来源”，真正的读取发生在命令未显式配置时。 */
FRR_CFG_DEFAULT_ULONG(BGP_KEEPALIVE,
	{ .val_ulong = 3, .match_profile = "datacenter", },
	{ .val_ulong = BGP_DEFAULT_KEEPALIVE },
);
FRR_CFG_DEFAULT_ULONG(BGP_HOLDTIME,
	{ .val_ulong = 9, .match_profile = "datacenter", },
	{ .val_ulong = BGP_DEFAULT_HOLDTIME },
);
FRR_CFG_DEFAULT_ULONG(BGP_CONNECT_RETRY,
	{ .val_ulong = 10, .match_profile = "datacenter", },
	{ .val_ulong = BGP_DEFAULT_CONNECT_RETRY },
);

/* 【实例名占位宏】BGP 视图命令可带一个可选的实例名(VRF 名)。
 *   BGP_INSTANCE_HELP_STR     : 显示单个实例名时用；
 *   BGP_INSTANCE_ALL_HELP_STR : 允许 "all" 通配(用于 show/clear 全部实例)时用。 */
#define BGP_INSTANCE_HELP_STR "BGP view\nBGP VRF\nView/VRF name\n"
#define BGP_INSTANCE_ALL_HELP_STR "BGP view\nBGP VRF\nAll Views/VRFs\n"

/* 【AFI/SAFI 命令词宏】BGP_AFI_CMD_STR 是 AFI 的可选令牌词法，注意 AFI 的 help
 * 字符串由 BGP_AF_STR **重复两次**拼出——因为 "<ipv4|ipv6>" 的每个候选都要一段
 * help，重复次数必须与候选个数一致，否则 CLI 上下文帮助会错位。
 *   BGP_AFI_CMD_STR    : AFI 令牌 "<ipv4|ipv6>"。
 *   BGP_SAFI_CMD_STR   : 基础 SAFI 令牌 "<unicast|multicast|vpn>"。
 *   BGP_AFI_SAFI_CMD_STR : 把 AFI 与 SAFI 拼成一段(中间用空格分隔)。
 *   BGP_SAFI_WITH_LABEL_* : 扩展 SAFI，额外含 labeled-unicast(BGP-LU)与 flowspec。 */
#define BGP_AFI_CMD_STR         "<ipv4|ipv6>"
#define BGP_AFI_HELP_STR BGP_AF_STR BGP_AF_STR
#define BGP_SAFI_CMD_STR        "<unicast|multicast|vpn>"
#define BGP_SAFI_HELP_STR                                                      \
	BGP_AF_MODIFIER_STR BGP_AF_MODIFIER_STR BGP_AF_MODIFIER_STR
#define BGP_AFI_SAFI_CMD_STR    BGP_AFI_CMD_STR" "BGP_SAFI_CMD_STR
#define BGP_AFI_SAFI_HELP_STR   BGP_AFI_HELP_STR BGP_SAFI_HELP_STR

#define BGP_SAFI_WITH_LABEL_CMD_STR  "<unicast|multicast|vpn|labeled-unicast|flowspec>"
#define BGP_SAFI_WITH_LABEL_HELP_STR                                           \
	BGP_AF_MODIFIER_STR BGP_AF_MODIFIER_STR BGP_AF_MODIFIER_STR            \
		BGP_AF_MODIFIER_STR BGP_AF_MODIFIER_STR

/* 【self-originate 关键字】用于 show 命令限定的可选项：只显示本机自己产生的路由。 */
#define BGP_SELF_ORIG_CMD_STR       "self-originate"
#define BGP_SELF_ORIG_HELP_STR      "Display only self-originated routes\n"

/* 【show 输出表头宏】
 *   SHOW_GR_HEADER                : 显示邻居 GR 状态前的“符号图例”(GR 状态图例，含 * 等符号)。
 *   BGP_SHOW_SUMMARY_HEADER_ALL   : 标准 neighbour summary 表头。
 *   ..._ALL_WIDE                  : 宽表头(多出 LocalAS 列)。
 *   ..._FAILED                    : show bgp neighbor failed/失败重连统计的表头。
 * 这些是纯显示字符串，改动只影响输出外观，不影响逻辑。 */
#define SHOW_GR_HEADER \
	"Codes: GR - Graceful Restart," \
	" * -  Inheriting Global GR Config,\n" \
	"       Restart - GR Mode-Restarting," \
	" Helper - GR Mode-Helper,\n" \
	"       Disable - GR Mode-Disable.\n\n"

#define BGP_SHOW_SUMMARY_HEADER_ALL                                            \
	"V         AS   MsgRcvd   MsgSent   TblVer  InQ OutQ  Up/Down State/PfxRcd   PfxSnt Desc\n"
#define BGP_SHOW_SUMMARY_HEADER_ALL_WIDE                                       \
	"V         AS    LocalAS   MsgRcvd   MsgSent   TblVer  InQ OutQ  Up/Down State/PfxRcd   PfxSnt Desc\n"
#define BGP_SHOW_SUMMARY_HEADER_FAILED "EstdCnt DropCnt ResetTime Reason\n"

/* 【show 邻居 GR 能力】把“本地模式 / 远端模式 / 标志 / 时间 / per-AFI-SAFI 信息”
 * 依次打印到 vty(或 json)。注意宏体里第 5 个调用写的是 use_json 而非参数 json，
 * 与前面几个调用不一致——疑点：可能是笔误(应统一用形参)，但已在多处沿用，改动
 * 需谨慎确认（待确认）。 */
#define BGP_SHOW_PEER_GR_CAPABILITY(vty, p, json)                                                 \
	do {                                                                                      \
		bgp_show_neighbor_graceful_restart_local_mode(vty, p, json);                      \
		bgp_show_neighbor_graceful_restart_remote_mode(vty, p, json);                     \
		bgp_show_neighnor_graceful_restart_flags(vty, p, json);                           \
		bgp_show_neighbor_graceful_restart_time(vty, p, json);                            \
		bgp_show_peer_gr_info_afi_safi(vty, p, use_json, json);                           \
	} while (0)

/* 【GR 检测宏族：判断“有没有启用 GR 的邻居”，据此同步 GR 能力给 zebra】
 * 背景：BGP 的 GR 能力是否需要启用，取决于**是否存在**启用了 GR 的邻居。
 *   VTY_BGP_GR_DEFINE_LOOP_VARIABLE : 只是声明遍历邻居列表所需的循环变量。
 *   VTY_BGP_GR_ROUTER_DETECT        : 遍历 peer 列表，只要发现任一 peer 带
 *                                     PEER_FLAG_GRACEFUL_RESTART，就把 gr_router_detected 置 true。
 *   VTY_SEND_BGP_GR_CAPABILITY_TO_ZEBRA : 根据检测结果与当前已下发状态，仅在
 *                                     “需要变而尚未变”时调用 bgp_zebra_send_capabilities()
 *                                     真正通知 zebra；失败则把 _ret 置 BGP_ERR_INVALID_VALUE。
 *   VTY_BGP_GR_ROUTER_DETECT_AND_SEND_CAPABILITY_TO_ZEBRA : 上面两步的合并版
 *                                     (自带局部变量，无需外部先声明)。
 * 注意这些宏**有副作用**(会真的发送 zebra 消息)，不是纯字符串宏。 */
#define VTY_BGP_GR_DEFINE_LOOP_VARIABLE                                        \
	struct peer *peer_loop = NULL;                                         \
	struct listnode *node = NULL;                                          \
	struct listnode *nnode = NULL;                                         \
	bool gr_router_detected = false

#define VTY_BGP_GR_ROUTER_DETECT(_bgp, _peer, _peer_list)                      \
	do {                                                                   \
		for (ALL_LIST_ELEMENTS(_peer_list, node, nnode, peer_loop)) {  \
			if (CHECK_FLAG(peer_loop->flags,                       \
				       PEER_FLAG_GRACEFUL_RESTART))            \
				gr_router_detected = true;                     \
		}                                                              \
	} while (0)


#define VTY_SEND_BGP_GR_CAPABILITY_TO_ZEBRA(_bgp, _ret)                        \
	do {                                                                   \
		if (gr_router_detected                                         \
		    && _bgp->present_zebra_gr_state == ZEBRA_GR_DISABLE) {     \
			if (bgp_zebra_send_capabilities(_bgp, false))          \
				_ret = BGP_ERR_INVALID_VALUE;                  \
		} else if (!gr_router_detected                                 \
			   && _bgp->present_zebra_gr_state                     \
				      == ZEBRA_GR_ENABLE) {                    \
			if (bgp_zebra_send_capabilities(_bgp, true))           \
				_ret = BGP_ERR_INVALID_VALUE;                  \
		}                                                              \
	} while (0)

#define VTY_BGP_GR_ROUTER_DETECT_AND_SEND_CAPABILITY_TO_ZEBRA(_bgp,             \
							      _peer_list, _ret) \
	do {                                                                    \
		struct peer *peer_loop;                                         \
		bool gr_router_detected = false;                                \
		struct listnode *node = { 0 };                                  \
		struct listnode *nnode = { 0 };                                 \
		for (ALL_LIST_ELEMENTS(_peer_list, node, nnode, peer_loop)) {   \
			if (CHECK_FLAG(peer_loop->flags,                        \
				       PEER_FLAG_GRACEFUL_RESTART))             \
				gr_router_detected = true;                      \
		}                                                               \
		if (gr_router_detected &&                                       \
		    _bgp->present_zebra_gr_state == ZEBRA_GR_DISABLE) {         \
			if (bgp_zebra_send_capabilities(_bgp, false))           \
				_ret = BGP_ERR_INVALID_VALUE;                   \
		} else if (!gr_router_detected &&                               \
			   _bgp->present_zebra_gr_state == ZEBRA_GR_ENABLE) {   \
			if (bgp_zebra_send_capabilities(_bgp, true))            \
				_ret = BGP_ERR_INVALID_VALUE;                   \
		}                                                               \
	} while (0)


/* 【EOR 输出宏】EOR = End-of-RIB，表示对端已把该 AFI/SAFI 的全部路由发完。
 *   PRINT_EOR      : 文本模式打印 "Yes"/"No"。
 *   PRINT_EOR_JSON : JSON 模式在 json_endofrib_status 对象里加布尔字段
 *                    endOfRibSentAfterUpdate。
 * 两个宏依赖外部的变量名(vty / json_endofrib_status)，因此只能在相应命名环境里用。 */
#define PRINT_EOR(_eor_flag)                                                   \
	do {                                                                   \
		if (eor_flag)                                                  \
			vty_out(vty, "Yes\n");                                 \
		else                                                           \
			vty_out(vty, "No\n");                                  \
	} while (0)

#define PRINT_EOR_JSON(_eor_flag)                                              \
	do {                                                                   \
		if (eor_flag)                                                  \
			json_object_boolean_true_add(                          \
				json_endofrib_status,                          \
				"endOfRibSentAfterUpdate");                    \
		else                                                           \
			json_object_boolean_false_add(                         \
				json_endofrib_status,                          \
				"endOfRibSentAfterUpdate");                    \
	} while (0)

/* 【函数】bgp_clear_soft_in —— 对本地已存的入向路由做一次“软清(soft-in)”，即
 * 在不重置 BGP 会话的前提下，重新应用入向策略(基于 bgp_adj_in 保存的原始属性)。
 * afi/safi 指定作用范围。 */
extern void bgp_clear_soft_in(struct bgp *bgp, afi_t afi, safi_t safi);

/* 【show 邻居时的标志位】位掩码，用于 show bgp neighbor 系列筛选要展示的信息。
 * 【Peer show flags】
 * 【Value of 0 means show all information】 */
/* Peer show flags */
/* Value of 0 means show all information */
#define VTY_BGP_PEER_SHOW_GR_INFO (1 << 0)

/* 【对外接口原型总览】下面这些是 bgp_vty.c 里实现、供其它 bgpd 文件调用的函数：
 *   bgp_vty_init                : bgpd 启动时注册所有 CLI 命令的总入口。
 *   community_alias_vty         : 注册 community alias(团体属性别名)相关命令。
 *   get_afi_safi_str            : AFI/SAFI 转显示字符串(for_json 决定风格)。
 *   bgp_get_vty                 : 按 vty 输入(AS/实例名/类型/AS 表示法)取得或创建 bgp 实例。
 *   bgp_config_write_update_delay / wpkt_quanta / rpkt_quanta / listen /
 *     coalesce_time             : 向 running-config 写回各类 bgp 参数(供 config write)。
 *   bgp_vty_return              : 把 bgp_create 错误码转成给用户的 vty 报错文本。
 *   bgp_config_inprocess        : 是否正处在配置事务处理中(用于抑制部分副作用)。
 *   peer_and_group_lookup_vty   : 按字符串查邻居(先 peer 后 peer-group)。
 *   bgp_vty_afi_from_str / _safi_from_str : 命令行字符串 → afi/safi 枚举。
 *   argv_find_and_parse_afi / _safi : 在 argv 中定位并解析 AFI/SAFI 令牌。
 *   bgp_vty_find_and_parse_afi_safi_bgp : 上两者的合并版，一次性解析 afi+safi+bgp 实例。
 *   bgp_vty_find_and_parse_bgp  : 只解析 bgp 实例。
 *   bgp_show_summary_vty        : show bgp summary 的实现入口。
 *   peergroup_flag_check        : 检查(并含 peer-group 继承语义的)邻居标志位。 */
/* 【函数原型】bgp_vty_init —— bgpd 启动时注册全部 CLI 命令的总入口，实现在 .c 中。 */
extern void bgp_vty_init(void);
/* 【函数原型】community_alias_vty —— 注册 community alias(团体属性别名)相关命令。 */
extern void community_alias_vty(void);
/* 【函数原型】get_afi_safi_str —— 把 afi/safi 枚举转成显示用字符串；
 * for_json 为真时采用 JSON 风格(大小写/下划线写法)的文本，返回静态字符串不需释放。 */
extern const char *get_afi_safi_str(afi_t afi, safi_t safi, bool for_json);
/* 【函数原型】bgp_get_vty —— 依据 vty 输入取得或创建 bgp 实例。
 * as/name/inst_type 描述目标实例(默认视图/命名视图/VRF)，as_pretty 与 asnotation
 * 决定 AS 数字的文本表示法；成功返回 CMD_SUCCESS 并回填 *bgp，失败返回错误码。 */
extern int bgp_get_vty(struct bgp **bgp, as_t *as, const char *name,
		       enum bgp_instance_type inst_type, const char *as_pretty,
		       enum asnotation_mode asnotation);
/* 【函数原型】一组 config-write 辅助函数：向 running-config 逐项写回 bgp 全局参数
 * (update-delay / 读写包 quanta / listen / coalesce-time)，供 bgp_config_write() 调用；
 * vty 为输出目标，bgp 为待写实例，无返回值。 */
extern void bgp_config_write_update_delay(struct vty *vty, struct bgp *bgp);
extern void bgp_config_write_wpkt_quanta(struct vty *vty, struct bgp *bgp);
extern void bgp_config_write_rpkt_quanta(struct vty *vty, struct bgp *bgp);
extern void bgp_config_write_listen(struct vty *vty, struct bgp *bgp);
extern void bgp_config_write_coalesce_time(struct vty *vty, struct bgp *bgp);
/* 【函数原型】bgp_vty_return —— 把 bgp_create 类错误码转成给用户的 vty 报错文本，
 * 返回 CMD_WARNING(调用者据此终止当前命令处理)等。 */
extern int bgp_vty_return(struct vty *vty, enum bgp_create_error_code ret);
/* 【函数原型】bgp_config_inprocess —— 返回是否正处在配置事务处理中(用于抑制部分副作用)。 */
extern bool bgp_config_inprocess(void);
/* 【函数原型】peer_and_group_lookup_vty —— 按字符串先查 peer 再查 peer-group；
 * 找不到(或存在歧义)时向 vty 报错并返回 NULL。 */
extern struct peer *peer_and_group_lookup_vty(struct vty *vty,
					      const char *peer_str);

/* 【函数原型】bgp_vty_afi_from_str —— 命令行字符串转 afi 枚举；无法识别返回 AFI_UNSPEC。 */
extern afi_t bgp_vty_afi_from_str(const char *afi_str);

/* 【函数原型】bgp_vty_safi_from_str —— 命令行字符串转 safi 枚举；无法识别返回 SAFI_UNSPEC。 */
extern safi_t bgp_vty_safi_from_str(const char *safi_str);

/* 【函数原型】argv_find_and_parse_afi —— 在 argv 中定位 AFI 令牌并解析；
 * 命中则回填 *index(令牌下标)与 *afi，返回 1，未命中返回 0。 */
extern int argv_find_and_parse_afi(struct cmd_token **argv, int argc,
				   int *index, afi_t *afi);

/* 【函数原型】argv_find_and_parse_safi —— 在 argv 中定位 SAFI 令牌并解析；
 * 命中则回填 *index 与 *safi，返回 1，未命中返回 0。 */
extern int argv_find_and_parse_safi(struct cmd_token **argv, int argc,
				    int *index, safi_t *safi);

/* 【函数原型】bgp_vty_find_and_parse_afi_safi_bgp —— 上面两个解析函数的合并版：
 * 一次性解析 afi / safi 并取得对应的 bgp 实例；use_json 决定出错报错的风格。 */
extern int bgp_vty_find_and_parse_afi_safi_bgp(struct vty *vty,
					       struct cmd_token **argv,
					       int argc, int *idx, afi_t *afi,
					       safi_t *safi, struct bgp **bgp,
					       bool use_json);
/* 【函数原型】bgp_vty_find_and_parse_bgp —— 只解析 bgp 实例(不涉及 afi / safi)。 */
int bgp_vty_find_and_parse_bgp(struct vty *vty, struct cmd_token **argv,
			       int argc, struct bgp **bgp, bool use_json);
/* 【函数原型】bgp_show_summary_vty —— show bgp summary 的实现入口；
 * name 指定视图/VRF，neighbor 可只显示单个邻居，show_flags 控制展示细节。 */
extern int bgp_show_summary_vty(struct vty *vty, const char *name, afi_t afi,
				safi_t safi, const char *neighbor,
				enum peer_asn_type as_type, as_t as,
				uint16_t show_flags);
/* 【函数原型】peergroup_flag_check —— 检查邻居是否带某全局标志，并做 peer-group
 * 继承语义处理(成员未显式设置时看所属群组)。 */
extern bool peergroup_flag_check(struct peer *peer, uint64_t flag);
/* 【函数原型】peergroup_af_flag_check —— 上一个函数的 per-AFI-SAFI 版本，
 * 按 afi / safi 分别判定邻居(或其群组)是否带某标志。 */
extern bool peergroup_af_flag_check(struct peer *peer, afi_t afi, safi_t safi,
				    uint64_t flag);

/* 【文件收尾】以下为头文件保护宏的 #endif，本文件所有声明到此结束。 */
#endif /* _QUAGGA_BGP_VTY_H */
