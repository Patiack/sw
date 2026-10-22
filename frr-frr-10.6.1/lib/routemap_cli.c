// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Route map northbound CLI implementation.
 *
 * Copyright (C) 2019 Network Device Education Foundation, Inc. ("NetDEF")
 *                    Rafael Zalamena
 */
/* ============================================================================
 * 【学习导读】routemap_cli.c —— route-map 的“命令行前端”
 * ============================================================================
 * 本文件只负责“把人敲的 CLI 命令翻译成 YANG 数据树上的增删改”，真正的
 * route-map 匹配 / 设置逻辑在 routemap.c，YANG 模型回调在 routemap_northbound.c。
 * 三者关系：
 *
 *    用户 CLI  --> [本文件 DEFPY/DEFPY_YANG]  --> northbound(生成/提交改动)
 *                       --> routemap_northbound.c 的 callback
 *                       --> routemap.c 的 route_map_* 内部结构
 *
 * 关键机制：
 *   1) DEFPY / DEFPY_YANG 宏既生成命令字符串的解析表，又生成对应的 C 函数。
 *      名字后面带 $xxx 的是“命令行变量”，函数里可直接用同名 C 变量取到值。
 *   2) nb_cli_enqueue_change() 把一次改动（CREATE / MODIFY / DESTROY）排入
 *      队列，最后 nb_cli_apply_changes() 一次性提交并触发 YANG 回调。
 *      —— 注意：不显式调用 apply，前面的 enqueue 不会生效。
 *   3) XPath 字符串是本文件的核心“地址”。例如：
 *      /frr-route-map:lib/route-map[name='X']/entry[sequence='10']/action
 *      它精确定位到某条 route-map 的某个序号条目上的 action / match / set 节点。
 *   4) ROUTE_MAP_*_CMD_STR 是命令帮助文本（help 字符串），必须按键数量与
 *      命令模板一一对应，少一行或多一行都会导致 CLI 注册断言失败。
 *
 * 阅读建议：先看顶部的 *.c 与几个 route-map/no route-map 主命令，理解
 * enqueue+apply 套路；再顺次看 match / set 各子命令，它们都是同一模式。
 * ============================================================================
 */

#include <zebra.h>

#include "lib/command.h"
#include "lib/northbound_cli.h"
#include "lib/routemap.h"

#include "lib/routemap_cli_clippy.c"

/* 以下三个宏是 CLI 命令的帮助文本（help 字符串）。
 * 规则：每个宏里的字符串条目数量，必须与命令模板中“按空格切分的单词数”一致，
 * 一一对应；对不上会在命令注册时触发断言，进程崩溃。
 * 用反斜杠把多行拼成一个字符串字面量，便于按“参数”分条书写帮助。 */
#define ROUTE_MAP_CMD_STR \
	"Create route-map or enter route-map command mode\n" \
	"Route map tag\n"
#define ROUTE_MAP_OP_CMD_STR \
	"Route map denies set operations\n" \
	"Route map permits set operations\n"
#define ROUTE_MAP_SEQUENCE_CMD_STR \
	"Sequence to insert to/delete from existing route-map entry\n"

/* 命令：route-map RMAP_NAME <deny|permit> (sequence)
 * 作用：创建（或进入）一条 route-map 条目。name/action/sequence 三个 $变量
 *       由 DEFPY 解析后直接作为同名 C 变量可用。
 * 实现套路：按 XPath 逐层 enqueue —— 先建 route-map 容器节点，再建其下的
 *       第 sequence 号 entry 节点，最后把 action（deny/permit）写到 entry 的
 *       action 叶子。全部排好队后一次性 apply。
 * 关键点：apply 成功（CMD_SUCCESS）后才把当前视图切换到该 entry（VTY_PUSH_XPATH），
 *       这样后续的 match / set 子命令才有正确的上下文。失败则不切换。 */
DEFPY_YANG_NOSH(
	route_map, route_map_cmd,
	"route-map RMAP_NAME$name <deny|permit>$action (1-65535)$sequence",
	ROUTE_MAP_CMD_STR
	ROUTE_MAP_OP_CMD_STR
	ROUTE_MAP_SEQUENCE_CMD_STR)
{
	char xpath_action[XPATH_MAXLEN + 64];
	char xpath_index[XPATH_MAXLEN + 32];
	char xpath[XPATH_MAXLEN];
	int rv;

	snprintf(xpath, sizeof(xpath),
		 "/frr-route-map:lib/route-map[name='%s']", name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintfrr(xpath_index, sizeof(xpath_index), "%s/entry[sequence='%" PRIu64 "']", xpath,
		   sequence);
	nb_cli_enqueue_change(vty, xpath_index, NB_OP_CREATE, NULL);

	snprintf(xpath_action, sizeof(xpath_action), "%s/action", xpath_index);
	nb_cli_enqueue_change(vty, xpath_action, NB_OP_MODIFY, action);

	rv = nb_cli_apply_changes(vty, NULL);
	if (rv == CMD_SUCCESS)
		VTY_PUSH_XPATH(RMAP_NODE, xpath_index);

	return rv;
}

/* 命令：no route-map RMAP_NAME
 * 作用：删除整条 route-map（连同其下所有 entry）。只按 name 定位到容器节点，
 *       发一个 NB_OP_DESTROY 即可，YANG 的销毁会级联清掉子节点。
 * 返回值：nb_cli_apply_changes 的返回值（CMD_SUCCESS / 各种错误）。 */
DEFPY_YANG(
	no_route_map_all, no_route_map_all_cmd,
	"no route-map RMAP_NAME$name",
	NO_STR
	ROUTE_MAP_CMD_STR)
{
	char xpath[XPATH_MAXLEN];

	snprintf(xpath, sizeof(xpath),
		 "/frr-route-map:lib/route-map[name='%s']", name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* 命令：no route-map RMAP_NAME <deny|permit> (sequence)
 * 作用：只删除某一条 entry（指定 sequence 的那一条），保留同名的其他条目。
 *       注意与 no_route_map_all 的区别：这里带 deny/permit 与序号。
 * 实现：XPath 精确到 entry[sequence='N']，对该节点发 NB_OP_DESTROY。
 *       命令里的 action 参数在此处并不参与 XPath 定位，仅用于语法匹配。 */
DEFPY_YANG(
	no_route_map, no_route_map_cmd,
	"no route-map RMAP_NAME$name <deny|permit>$action (1-65535)$sequence",
	NO_STR
	ROUTE_MAP_CMD_STR
	ROUTE_MAP_OP_CMD_STR
	ROUTE_MAP_SEQUENCE_CMD_STR)
{
	char xpath[XPATH_MAXLEN];

	snprintfrr(xpath, sizeof(xpath),
		   "/frr-route-map:lib/route-map[name='%s']/entry[sequence='%" PRIu64 "']", name,
		   sequence);

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* entry 节点（route-map 的某一条）的比较函数，供“按 sequence 排序输出”使用。
 * 入参：dnode1 / dnode2 为两个 entry 的 YANG 节点。
 * 返回：seq1 - seq2（<0 / 0 / >0），即按 sequence 升序。
 * 坑：是把 uint16_t 相减，差异很大时理论上可能溢出为负？此处只用于排序，
 *     相对大小关系仍然正确，故无碍。 */
int route_map_instance_cmp(const struct lyd_node *dnode1,
			   const struct lyd_node *dnode2)
{
	uint16_t seq1 = yang_dnode_get_uint16(dnode1, "sequence");
	uint16_t seq2 = yang_dnode_get_uint16(dnode2, "sequence");

	return seq1 - seq2;
}

/* 单条 entry 的“显示”回调：把一条 route-map 打印成一行配置。
 * 形如：route-map NAME permit 10
 * 入参：dnode 是某个 entry 节点；name 取自其父节点（../name），action/sequence
 *       取自本节点。show_defaults 表示是否连默认值一起显示（此处未用）。
 * 副作用：只向 vty 输出文本，不改变任何状态。 */
void route_map_instance_show(struct vty *vty, const struct lyd_node *dnode,
			     bool show_defaults)
{
	const char *name = yang_dnode_get_string(dnode, "../name");
	const char *action = yang_dnode_get_string(dnode, "action");
	const char *sequence = yang_dnode_get_string(dnode, "sequence");

	vty_out(vty, "route-map %s %s %s\n", name, action, sequence);

}

/* 一个 route-map（容器）显示完毕后的“收尾”回调：输出 exit 与分隔符 ! 。
 * 对应 show 输出中一条 route-map 结束后的换行，让配置块看起来与 vty 回显一致。 */
void route_map_instance_show_end(struct vty *vty, const struct lyd_node *dnode)
{
	vty_out(vty, "exit\n");
	vty_out(vty, "!\n");
}

/* ==== match 条件命令族（通用套路）=========================================
 * 一条 route-map entry 下有一张 match-condition 列表。每个条件表现为一个
 * 列表节点：key 是 condition（一个 identityref，指明“哪一类条件”），
 * 具体数值放在它下面的 rmap-match-condition 分支里（不同类型结构不同）。
 * 因此每个 match 命令的固定三步是：
 *   1) CREATE  ./match-condition[condition='frr-route-map:xxx']
 *   2) MODIFY  <上面路径>/rmap-match-condition/<字段> = 用户输入
 *   3) apply 提交
 * 对应的 no match 只做一步 DESTROY（删掉整个条件节点）。
 * 坑：XPath 首部的 "./" 是相对当前 entry 上下文（由进入 route-map 时的
 *     VTY_PUSH_XPATH 建立），因此这些命令只能在 route-map 视图里执行。
 * ======================================================================== */

/* match interface IFNAME：按“路由的下一跳出接口”匹配。
 * 条件标识：frr-route-map:interface；数值写在 rmap-match-condition/interface。 */
DEFPY_YANG(
	match_interface, match_interface_cmd,
	"match interface IFNAME",
	MATCH_STR
	"Match first hop interface of route\n"
	INTERFACE_STR)
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:interface']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/interface", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, ifname);

	return nb_cli_apply_changes(vty, NULL);
}

/* no match interface：删除 interface 匹配条件（整节点 DESTROY）。 */
DEFPY_YANG(
	no_match_interface, no_match_interface_cmd,
	"no match interface [IFNAME]",
	NO_STR
	MATCH_STR
	"Match first hop interface of route\n"
	INTERFACE_STR)
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:interface']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* match ip address ACCESSLIST4_NAME：按 IPv4 目的前缀匹配（ACL 列表）。
 * 条件标识：frr-route-map:ipv4-address-list；数值写到 .../list-name。 */
DEFPY_YANG(
	match_ip_address, match_ip_address_cmd,
	"match ip address ACCESSLIST4_NAME$name",
	MATCH_STR
	IP_STR
	"Match address of route\n"
	"IP Access-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv4-address-list']";
	char xpath_value[XPATH_MAXLEN + 32];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/list-name", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, name);

	return nb_cli_apply_changes(vty, NULL);
}

/* no match ip address：删除 IPv4 目的 ACL 匹配条件。 */
DEFPY_YANG(
	no_match_ip_address, no_match_ip_address_cmd,
	"no match ip address [ACCESSLIST4_NAME]",
	NO_STR
	MATCH_STR
	IP_STR
	"Match address of route\n"
	"IP Access-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv4-address-list']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* match ip address prefix-list：与上一条同类，但用 IPv4 prefix-list 而非 ACL。
 * 条件标识：frr-route-map:ipv4-prefix-list。 */
DEFPY_YANG(
	match_ip_address_prefix_list,
	match_ip_address_prefix_list_cmd,
	"match ip address prefix-list PREFIXLIST4_NAME$name",
	MATCH_STR
	IP_STR
	"Match address of route\n"
	"Match entries of prefix-lists\n"
	"IP prefix-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv4-prefix-list']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/list-name", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, name);

	return nb_cli_apply_changes(vty, NULL);
}

/* no match ip address prefix-list：删除 IPv4 prefix-list 目的匹配条件。 */
DEFPY_YANG(
	no_match_ip_address_prefix_list, no_match_ip_address_prefix_list_cmd,
	"no match ip address prefix-list [PREFIXLIST4_NAME]",
	NO_STR
	MATCH_STR
	IP_STR
	"Match address of route\n"
	"Match entries of prefix-lists\n"
	"IP prefix-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv4-prefix-list']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* match ip next-hop：按 IPv4 下一跳地址匹配（ACL）。
 * 条件标识：frr-route-map:ipv4-next-hop-list。 */
DEFPY_YANG(
	match_ip_next_hop, match_ip_next_hop_cmd,
	"match ip next-hop ACCESSLIST4_NAME$name",
	MATCH_STR
	IP_STR
	"Match next-hop address of route\n"
	"IP Access-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv4-next-hop-list']";
	char xpath_value[XPATH_MAXLEN + 32];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/list-name", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, name);

	return nb_cli_apply_changes(vty, NULL);
}

/* no match ip next-hop：删除 IPv4 下一跳 ACL 匹配条件。 */
DEFPY_YANG(
	no_match_ip_next_hop, no_match_ip_next_hop_cmd,
	"no match ip next-hop [ACCESSLIST4_NAME]",
	NO_STR
	MATCH_STR
	IP_STR
	"Match address of route\n"
	"IP Access-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv4-next-hop-list']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* match ip next-hop prefix-list：IPv4 下一跳匹配，用 prefix-list。
 * 条件标识：frr-route-map:ipv4-next-hop-prefix-list。 */
DEFPY_YANG(
	match_ip_next_hop_prefix_list,
	match_ip_next_hop_prefix_list_cmd,
	"match ip next-hop prefix-list PREFIXLIST4_NAME$name",
	MATCH_STR
	IP_STR
	"Match next-hop address of route\n"
	"Match entries of prefix-lists\n"
	"IP prefix-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv4-next-hop-prefix-list']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/list-name", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, name);

	return nb_cli_apply_changes(vty, NULL);
}

/* no match ip next-hop prefix-list：删除 IPv4 下一跳 prefix-list 匹配条件。 */
DEFPY_YANG(
	no_match_ip_next_hop_prefix_list,
	no_match_ip_next_hop_prefix_list_cmd,
	"no match ip next-hop prefix-list [PREFIXLIST4_NAME]",
	NO_STR
	MATCH_STR
	IP_STR
	"Match next-hop address of route\n"
	"Match entries of prefix-lists\n"
	"IP prefix-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv4-next-hop-prefix-list']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* match ip next-hop type blackhole：按下一跳“类型”匹配（当前仅 blackhole）。
 * 与按地址匹配不同，这里数值写在 rmap-match-condition/ipv4-next-hop-type。 */
DEFPY_YANG(
	match_ip_next_hop_type, match_ip_next_hop_type_cmd,
	"match ip next-hop type <blackhole>$type",
	MATCH_STR
	IP_STR
	"Match next-hop address of route\n"
	"Match entries by type\n"
	"Blackhole\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv4-next-hop-type']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/ipv4-next-hop-type", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, type);

	return nb_cli_apply_changes(vty, NULL);
}

/* no match ip next-hop type：删除“下一跳类型”匹配条件。 */
DEFPY_YANG(
	no_match_ip_next_hop_type, no_match_ip_next_hop_type_cmd,
	"no match ip next-hop type [<blackhole>]",
	NO_STR MATCH_STR IP_STR
	"Match next-hop address of route\n"
	"Match entries by type\n"
	"Blackhole\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv4-next-hop-type']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* match ipv6 address：按 IPv6 目的前缀匹配（ACL）。
 * 条件标识：frr-route-map:ipv6-address-list。 */
DEFPY_YANG(
	match_ipv6_address, match_ipv6_address_cmd,
	"match ipv6 address ACCESSLIST6_NAME$name",
	MATCH_STR
	IPV6_STR
	"Match IPv6 address of route\n"
	"IPv6 access-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv6-address-list']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/list-name", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, name);

	return nb_cli_apply_changes(vty, NULL);
}

/* no match ipv6 address：删除 IPv6 目的 ACL 匹配条件。 */
DEFPY_YANG(
	no_match_ipv6_address, no_match_ipv6_address_cmd,
	"no match ipv6 address [ACCESSLIST6_NAME]",
	NO_STR
	MATCH_STR
	IPV6_STR
	"Match IPv6 address of route\n"
	"IPv6 access-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv6-address-list']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* match ipv6 address prefix-list：IPv6 目的匹配，用 prefix-list。
 * 条件标识：frr-route-map:ipv6-prefix-list。 */
DEFPY_YANG(
	match_ipv6_address_prefix_list, match_ipv6_address_prefix_list_cmd,
	"match ipv6 address prefix-list PREFIXLIST6_NAME$name",
	MATCH_STR
	IPV6_STR
	"Match address of route\n"
	"Match entries of prefix-lists\n"
	"IP prefix-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv6-prefix-list']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/list-name", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, name);

	return nb_cli_apply_changes(vty, NULL);
}

/* no match ipv6 address prefix-list：删除 IPv6 目的 prefix-list 匹配条件。 */
DEFPY_YANG(
	no_match_ipv6_address_prefix_list,
	no_match_ipv6_address_prefix_list_cmd,
	"no match ipv6 address prefix-list [PREFIXLIST6_NAME]",
	NO_STR
	MATCH_STR
	IPV6_STR
	"Match address of route\n"
	"Match entries of prefix-lists\n"
	"IP prefix-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv6-prefix-list']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* match ipv6 next-hop：按 IPv6 下一跳地址匹配（ACL）。
 * 注意这里用 DEFUN_YANG（无 $变量），取用户在命令行末位输入的值要用
 * argv[argc - 1]->arg 取得最后一个参数。条件标识 ipv6-next-hop-list。 */
DEFUN_YANG (match_ipv6_next_hop,
	    match_ipv6_next_hop_cmd,
	    "match ipv6 next-hop ACCESSLIST6_NAME",
	    MATCH_STR
	    IPV6_STR
	    "Match IPv6 next-hop address of route\n"
	    "IPv6 access-list name\n")
{
	const char *xpath = "./match-condition[condition='frr-route-map:ipv6-next-hop-list']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value), "%s/rmap-match-condition/list-name", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, argv[argc - 1]->arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* no match ipv6 next-hop：删除 IPv6 下一跳 ACL 匹配条件。 */
DEFUN_YANG (no_match_ipv6_next_hop,
	    no_match_ipv6_next_hop_cmd,
	    "no match ipv6 next-hop [ACCESSLIST6_NAME]",
	    NO_STR
	    MATCH_STR
	    IPV6_STR
	    "Match IPv6 next-hop address of route\n"
	    "IPv6 access-list name\n")
{
	const char *xpath = "./match-condition[condition='frr-route-map:ipv6-next-hop-list']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* match ipv6 next-hop prefix-list：IPv6 下一跳匹配，用 prefix-list。
 * 条件标识 frr-route-map:ipv6-next-hop-prefix-list。 */
DEFUN_YANG (match_ipv6_next_hop_prefix_list,
	    match_ipv6_next_hop_prefix_list_cmd,
	    "match ipv6 next-hop prefix-list PREFIXLIST_NAME",
	    MATCH_STR
	    IPV6_STR
	    "Match IPv6 next-hop address of route\n"
	    "Match entries by prefix-list\n"
	    "IPv6 prefix-list name\n")
{
	const char *xpath = "./match-condition[condition='frr-route-map:ipv6-next-hop-prefix-list']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value), "%s/rmap-match-condition/list-name", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, argv[argc - 1]->arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* no match ipv6 next-hop prefix-list：删除 IPv6 下一跳 prefix-list 匹配条件。 */
DEFUN_YANG (no_match_ipv6_next_hop_prefix_list,
	    no_match_ipv6_next_hop_prefix_list_cmd,
	    "no match ipv6 next-hop prefix-list [PREFIXLIST_NAME]",
	    NO_STR
	    MATCH_STR
	    IPV6_STR
	    "Match IPv6 next-hop address of route\n"
	    "Match entries by prefix-list\n"
	    "IPv6 prefix-list name\n")
{
	const char *xpath = "./match-condition[condition='frr-route-map:ipv6-next-hop-prefix-list']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* match ipv6 next-hop type blackhole：按 IPv6 下一跳“类型”匹配。
 * 数值写在 rmap-match-condition/ipv6-next-hop-type。 */
DEFPY_YANG(
	match_ipv6_next_hop_type, match_ipv6_next_hop_type_cmd,
	"match ipv6 next-hop type <blackhole>$type",
	MATCH_STR IPV6_STR
	"Match next-hop address of route\n"
	"Match entries by type\n"
	"Blackhole\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv6-next-hop-type']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/ipv6-next-hop-type", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, type);

	return nb_cli_apply_changes(vty, NULL);
}

/* no match ipv6 next-hop type：删除 IPv6“下一跳类型”匹配条件。 */
DEFPY_YANG(
	no_match_ipv6_next_hop_type, no_match_ipv6_next_hop_type_cmd,
	"no match ipv6 next-hop type [<blackhole>]",
	NO_STR MATCH_STR IPV6_STR
	"Match address of route\n"
	"Match entries by type\n"
	"Blackhole\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:ipv6-next-hop-type']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* match metric (0-4294967295)：按路由度量值匹配。
 * 条件标识 frr-route-map:match-metric；值以字符串 metric_str 写入 metric 叶。
 * 坑：变量名是 metric_str 而非 metric（因为 metric 可能被用作整型名，避免冲突）。 */
DEFPY_YANG(
	match_metric, match_metric_cmd,
	"match metric (0-4294967295)$metric",
	MATCH_STR
	"Match metric of route\n"
	"Metric value\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:match-metric']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/metric", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, metric_str);

	return nb_cli_apply_changes(vty, NULL);
}

/* no match metric：删除 metric 匹配条件。 */
DEFPY_YANG(
	no_match_metric, no_match_metric_cmd,
	"no match metric [(0-4294967295)]",
	NO_STR
	MATCH_STR
	"Match metric of route\n"
	"Metric value\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:match-metric']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* match tag <untagged|(1-4294967295)>：按路由 tag 匹配。
 * 条件标识 frr-route-map:match-tag。untagged 与带值用两个互斥的 $变量：
 * untagged 为真时取 0，否则取 tagged 的值，统一写入 /rmap-match-condition/tag。
 * 坑：这里用 snprintfrr 而非 snprintf 来格式化 PRIu64（FRR 的增强版）。 */
DEFPY_YANG(
	match_tag, match_tag_cmd,
	"match tag <untagged$untagged|(1-4294967295)$tagged>",
	MATCH_STR
	"Match tag of route\n"
	"Untagged route\n"
	"Tag value\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:match-tag']";
	char xpath_value[XPATH_MAXLEN];
	char value[64];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/tag", xpath);
	snprintfrr(value, sizeof(value), "%" PRIu64, tagged ? tagged : 0);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, value);

	return nb_cli_apply_changes(vty, NULL);
}

/* no match tag：删除 tag 匹配条件。 */
DEFPY_YANG(
	no_match_tag, no_match_tag_cmd,
	"no match tag [<untagged|(1-4294967295)>]",
	NO_STR
	MATCH_STR
	"Match tag of route\n"
	"Untagged route\n"
	"Tag value\n")
{
	const char *xpath =
		"./match-condition[condition='frr-route-map:match-tag']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【显示回调】把一个 match 条件节点回显成 CLI 文本。
 * 作用：show running-config 时，对每条 match-condition 生成 “match xxx …” 一行。
 * 参数：dnode 是某个 match-condition 列表节点；其 key 叶 condition 指明条件类型。
 * 逻辑：读 condition（identityref 的名字），用一串 IS_MATCH_* 宏做“多分支派发”，
 *       每个分支按该条件自己的 XPath 取到具体值再打印。
 * 关键点：
 *   - IS_MATCH_* 是 routemap.h 里定义的字符串比较宏；不同模块（zebra / bgp /
 *     pim）的条件值落在不同命名空间前缀的子树里，如 ./rmap-match-condition/
 *     frr-bgp-route-map:xxx，注意路径里的模块前缀不能漏。
 *   - 部分条件（community / large-community / extcommunity）除列表名外还有
 *     exact-match / any 两个布尔附加项，需要再拼进输出。
 *   - peer / src-peer 这类条件可能有 IPv4 / IPv6 / interface / local 多种取值，
 *     要依次尝试各个子节点，取到第一个非空的。
 * 返回值：无（void）；只向 vty 写文本，无副作用。 */
void route_map_condition_show(struct vty *vty, const struct lyd_node *dnode,
			      bool show_defaults)
{
	const char *condition = yang_dnode_get_string(dnode, "condition");
	const struct lyd_node *ln;
	const char *acl;

	if (IS_MATCH_INTERFACE(condition)) {
		vty_out(vty, " match interface %s\n",
			yang_dnode_get_string(
				dnode, "./rmap-match-condition/interface"));
	} else if (IS_MATCH_IPv4_ADDRESS_LIST(condition)) {
		vty_out(vty, " match ip address %s\n",
			yang_dnode_get_string(
				dnode, "./rmap-match-condition/list-name"));
	} else if (IS_MATCH_IPv4_NEXTHOP_LIST(condition)) {
		vty_out(vty, " match ip next-hop %s\n",
			yang_dnode_get_string(
				dnode, "./rmap-match-condition/list-name"));
	} else if (IS_MATCH_IPv6_NEXTHOP_LIST(condition)) {
		vty_out(vty, " match ipv6 next-hop %s\n",
			yang_dnode_get_string(
				dnode, "./rmap-match-condition/list-name"));
	} else if (IS_MATCH_IPv4_PREFIX_LIST(condition)) {
		vty_out(vty, " match ip address prefix-list %s\n",
			yang_dnode_get_string(
				dnode, "./rmap-match-condition/list-name"));
	} else if (IS_MATCH_IPv4_NEXTHOP_PREFIX_LIST(condition)) {
		vty_out(vty, " match ip next-hop prefix-list %s\n",
			yang_dnode_get_string(
				dnode, "./rmap-match-condition/list-name"));
	} else if (IS_MATCH_IPv6_NEXTHOP_PREFIX_LIST(condition)) {
		vty_out(vty, " match ipv6 next-hop prefix-list %s\n",
			yang_dnode_get_string(
				dnode, "./rmap-match-condition/list-name"));
	} else if (IS_MATCH_IPv6_ADDRESS_LIST(condition)) {
		vty_out(vty, " match ipv6 address %s\n",
			yang_dnode_get_string(
				dnode, "./rmap-match-condition/list-name"));
	} else if (IS_MATCH_IPv6_PREFIX_LIST(condition)) {
		vty_out(vty, " match ipv6 address prefix-list %s\n",
			yang_dnode_get_string(
				dnode, "./rmap-match-condition/list-name"));
	} else if (IS_MATCH_IPv4_NEXTHOP_TYPE(condition)) {
		vty_out(vty, " match ip next-hop type %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/ipv4-next-hop-type"));
	} else if (IS_MATCH_ASPATH_COUNT(condition)) {
		vty_out(vty, " match as-path-count %s\n",
			yang_dnode_get_string(dnode,
					      "./rmap-match-condition/frr-bgp-route-map:as-path-count"));
	} else if (IS_MATCH_IPv6_NEXTHOP_TYPE(condition)) {
		vty_out(vty, " match ipv6 next-hop type %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/ipv6-next-hop-type"));
	} else if (IS_MATCH_METRIC(condition)) {
		vty_out(vty, " match metric %s\n",
			yang_dnode_get_string(dnode,
					      "./rmap-match-condition/metric"));
	} else if (IS_MATCH_TAG(condition)) {
		uint32_t tag =
			strtoul(yang_dnode_get_string(dnode,
						      "./rmap-match-condition/tag"),
				NULL, 10);

		if (!tag)
			vty_out(vty, " match tag untagged\n");
		else
			vty_out(vty, " match tag %u\n", tag);
	} else if (IS_MATCH_IPv4_PREFIX_LEN(condition)) {
		vty_out(vty, " match ip address prefix-len %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-zebra-route-map:ipv4-prefix-length"));
	} else if (IS_MATCH_IPv6_PREFIX_LEN(condition)) {
		vty_out(vty, " match ipv6 address prefix-len %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-zebra-route-map:ipv6-prefix-length"));
	} else if (IS_MATCH_IPv4_NH_PREFIX_LEN(condition)) {
		vty_out(vty, " match ip next-hop prefix-len %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-zebra-route-map:ipv4-prefix-length"));
	} else if (IS_MATCH_SRC_PROTO(condition) || IS_MATCH_BGP_SRC_PROTO(condition)) {
		vty_out(vty, " match source-protocol %s\n",
			yang_dnode_get_string(
				dnode,
				IS_MATCH_SRC_PROTO(condition)
					? "./rmap-match-condition/frr-zebra-route-map:source-protocol"
					: "./rmap-match-condition/frr-bgp-route-map:source-protocol"));
	} else if (IS_MATCH_SRC_INSTANCE(condition)) {
		vty_out(vty, " match source-instance %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-zebra-route-map:source-instance"));
	} else if (IS_MATCH_VPN_DATAPLANE(condition)) {
		vty_out(vty, " match vpn dataplane %s\n",
			yang_dnode_get_string(dnode,
					      "./rmap-match-condition/frr-bgp-route-map:vpn-dataplane"));
	} else if (IS_MATCH_LOCAL_PREF(condition)) {
		vty_out(vty, " match local-preference %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:local-preference"));
	} else if (IS_MATCH_ALIAS(condition)) {
		vty_out(vty, " match alias %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:alias"));
	} else if (IS_MATCH_SCRIPT(condition)) {
		vty_out(vty, " match script %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:script"));
	} else if (IS_MATCH_ORIGIN(condition)) {
		vty_out(vty, " match origin %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:origin"));
	} else if (IS_MATCH_RPKI(condition)) {
		vty_out(vty, " match rpki %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:rpki"));
	} else if (IS_MATCH_EXTCOMMUNITY_LIMIT(condition)) {
		vty_out(vty, " match extcommunity-limit %s\n",
			yang_dnode_get_string(dnode,
					      "./rmap-match-condition/frr-bgp-route-map:extcommunity-limit"));
	} else if (IS_MATCH_RPKI_EXTCOMMUNITY(condition)) {
		vty_out(vty, " match rpki-extcommunity %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:rpki-extcommunity"));
	} else if (IS_MATCH_PROBABILITY(condition)) {
		vty_out(vty, " match probability %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:probability"));
	} else if (IS_MATCH_SRC_VRF(condition)) {
		vty_out(vty, " match source-vrf %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:source-vrf"));
	} else if (IS_MATCH_PEER(condition)) {
		acl = NULL;
		if ((ln = yang_dnode_get(
			     dnode,
			     "./rmap-match-condition/frr-bgp-route-map:peer-ipv4-address"))
		    != NULL)
			acl = yang_dnode_get_string(ln, NULL);
		else if (
			(ln = yang_dnode_get(
				 dnode,
				 "./rmap-match-condition/frr-bgp-route-map:peer-ipv6-address"))
			!= NULL)
			acl = yang_dnode_get_string(ln, NULL);
		else if (
			(ln = yang_dnode_get(
				 dnode,
				 "./rmap-match-condition/frr-bgp-route-map:peer-interface"))
			!= NULL)
			acl = yang_dnode_get_string(ln, NULL);
		else if (yang_dnode_get(
				 dnode,
				 "./rmap-match-condition/frr-bgp-route-map:peer-local")
			!= NULL)
			acl = "local";

		vty_out(vty, " match peer %s\n", acl);
	} else if (IS_MATCH_SRC_PEER(condition)) {
		acl = NULL;
		ln = yang_dnode_get(dnode,
				    "./rmap-match-condition/frr-bgp-route-map:src-peer-ipv4-address");
		if (!ln)
			ln = yang_dnode_get(dnode,
					    "./rmap-match-condition/frr-bgp-route-map:src-peer-ipv6-address");
		if (!ln)
			ln = yang_dnode_get(dnode,
					    "./rmap-match-condition/frr-bgp-route-map:src-peer-interface");
		acl = yang_dnode_get_string(ln, NULL);
		vty_out(vty, " match src-peer %s\n", acl);
	} else if (IS_MATCH_AS_LIST(condition)) {
		vty_out(vty, " match as-path %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:list-name"));
	} else if (IS_MATCH_EVPN_ROUTE_TYPE(condition)) {
		vty_out(vty, " match evpn route-type %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:evpn-route-type"));
	} else if (IS_MATCH_EVPN_DEFAULT_ROUTE(condition)) {
		vty_out(vty, " match evpn default-route\n");
	} else if (IS_MATCH_EVPN_VNI(condition)) {
		vty_out(vty, " match evpn vni %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:evpn-vni"));
	} else if (IS_MATCH_EVPN_RD(condition)) {
		vty_out(vty, " match evpn rd %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:route-distinguisher"));
	} else if (IS_MATCH_MAC_LIST(condition)) {
		vty_out(vty, " match mac address %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:list-name"));
	} else if (IS_MATCH_ROUTE_SRC(condition)) {
		vty_out(vty, " match ip route-source %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:list-name"));
	} else if (IS_MATCH_ROUTE_SRC_PL(condition)) {
		vty_out(vty, " match ip route-source prefix-list %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:list-name"));
	} else if (IS_MATCH_COMMUNITY_LIMIT(condition)) {
		vty_out(vty, " match community-limit %s\n",
			yang_dnode_get_string(dnode,
					      "./rmap-match-condition/frr-bgp-route-map:community-limit"));
	} else if (IS_MATCH_COMMUNITY(condition)) {
		vty_out(vty, " match community %s",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name"));
		if (yang_dnode_get_bool(
			    dnode,
			    "./rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-exact-match"))
			vty_out(vty, " exact-match");
		if (yang_dnode_get_bool(
			    dnode,
			    "./rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-any"))
			vty_out(vty, " any");
		vty_out(vty, "\n");
	} else if (IS_MATCH_LCOMMUNITY(condition)) {
		vty_out(vty, " match large-community %s",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name"));
		if (yang_dnode_get_bool(
			    dnode,
			    "./rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-exact-match"))
			vty_out(vty, " exact-match");
		if (yang_dnode_get_bool(
			    dnode,
			    "./rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-any"))
			vty_out(vty, " any");
		vty_out(vty, "\n");
	} else if (IS_MATCH_EXTCOMMUNITY(condition)) {
		vty_out(vty, " match extcommunity %s",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name"));
		if (yang_dnode_get_bool(
			    dnode,
			    "./rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-exact-match"))
			vty_out(vty, " exact-match");
		if (yang_dnode_get_bool(dnode,
					"./rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-any"))
			vty_out(vty, " any");
		vty_out(vty, "\n");
	} else if (IS_MATCH_IPV4_NH(condition)) {
		vty_out(vty, " match ip next-hop address %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:ipv4-address"));
	} else if (IS_MATCH_IPV6_NH(condition)) {
		vty_out(vty, " match ipv6 next-hop address %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-bgp-route-map:ipv6-address"));
	} else if (IS_MATCH_IPV4_MULTICAST_SOURCE(condition)) {
		vty_out(vty, " match ip multicast-source %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-pim-route-map:ipv4-multicast-source-address"));
	} else if (IS_MATCH_IPV6_MULTICAST_SOURCE(condition)) {
		vty_out(vty, " match ipv6 multicast-source %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-pim-route-map:ipv6-multicast-source-address"));
	} else if (IS_MATCH_IPV4_MULTICAST_SOURCE_PREFIX_LIST(condition)) {
		vty_out(vty, " match ip multicast-source prefix-list %s\n",
			yang_dnode_get_string(dnode,
					      "./rmap-match-condition/frr-pim-route-map:list-name"));
	} else if (IS_MATCH_IPV6_MULTICAST_SOURCE_PREFIX_LIST(condition)) {
		vty_out(vty, " match ipv6 multicast-source prefix-list %s\n",
			yang_dnode_get_string(dnode,
					      "./rmap-match-condition/frr-pim-route-map:list-name"));
	} else if (IS_MATCH_IPV4_MULTICAST_GROUP(condition)) {
		vty_out(vty, " match ip multicast-group %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-pim-route-map:ipv4-multicast-group-address"));
	} else if (IS_MATCH_IPV6_MULTICAST_GROUP(condition)) {
		vty_out(vty, " match ipv6 multicast-group %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-pim-route-map:ipv6-multicast-group-address"));
	} else if (IS_MATCH_IPV4_MULTICAST_GROUP_PREFIX_LIST(condition)) {
		vty_out(vty, " match ip multicast-group prefix-list %s\n",
			yang_dnode_get_string(dnode,
					      "./rmap-match-condition/frr-pim-route-map:list-name"));
	} else if (IS_MATCH_IPV6_MULTICAST_GROUP_PREFIX_LIST(condition)) {
		vty_out(vty, " match ipv6 multicast-group prefix-list %s\n",
			yang_dnode_get_string(dnode,
					      "./rmap-match-condition/frr-pim-route-map:list-name"));
	} else if (IS_MATCH_MULTICAST_INTERFACE(condition)) {
		vty_out(vty, " match multicast-interface %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-match-condition/frr-pim-route-map:multicast-interface"));
	}
}

/* ==== set 动作命令族（通用套路）==========================================
 * 与 match 对称：一条 entry 下有一张 set-action 列表。key 是 action（identityref），
 * 数值放在其下的 rmap-set-action 分支。set 命令固定三步：
 *   1) CREATE  ./set-action[action='frr-route-map:xxx']
 *   2) MODIFY  <上面路径>/rmap-set-action/<字段> = 用户输入
 *   3) apply
 * no set 只做 DESTROY。注意 set-action 用单数形式（match-condition 亦然），
 * 但都是 YANG list，key 是 action / condition。
 * ======================================================================== */

/* set ip next-hop A.B.C.D：设置 IPv4 下一跳地址（不是 ACL，是具体地址）。
 * 符号 $addr 的地址字符串变量名是 addr_str（lib 约定：地址类变量加 _str）。
 * action 标识 frr-route-map:ipv4-next-hop；值写到 rmap-set-action/ipv4-address。 */
DEFPY_YANG(
	set_ip_nexthop, set_ip_nexthop_cmd,
	"set ip next-hop A.B.C.D$addr",
	SET_STR
	IP_STR
	"Next hop address\n"
	"IP address of next hop\n")
{
	const char *xpath =
		"./set-action[action='frr-route-map:ipv4-next-hop']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/ipv4-address", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, addr_str);

	return nb_cli_apply_changes(vty, NULL);
}

/* no set ip next-hop：删除 IPv4 下一跳动作。 */
DEFPY_YANG(
	no_set_ip_nexthop, no_set_ip_nexthop_cmd,
	"no set ip next-hop [A.B.C.D]",
	NO_STR
	SET_STR
	IP_STR
	"Next hop address\n"
	"IP address of next hop\n")
{
	const char *xpath =
		"./set-action[action='frr-route-map:ipv4-next-hop']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* set ipv6 next-hop local X:X::X:X：设置 IPv6 下一跳的“本地”地址。
 * 命令里 local 是语法关键词（区别于 peer 地址），并未体现在 XPath 里。
 * action 标识 frr-route-map:ipv6-next-hop；值写到 rmap-set-action/ipv6-address。 */
DEFPY_YANG(
	set_ipv6_nexthop_local, set_ipv6_nexthop_local_cmd,
	"set ipv6 next-hop local X:X::X:X$addr",
	SET_STR
	IPV6_STR
	"IPv6 next-hop address\n"
	"IPv6 local address\n"
	"IPv6 address of next hop\n")
{
	const char *xpath =
		"./set-action[action='frr-route-map:ipv6-next-hop']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/ipv6-address", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, addr_str);

	return nb_cli_apply_changes(vty, NULL);
}

/* no set ipv6 next-hop local：删除 IPv6 下一跳本地地址动作。 */
DEFPY_YANG(
	no_set_ipv6_nexthop_local, no_set_ipv6_nexthop_local_cmd,
	"no set ipv6 next-hop local [X:X::X:X]",
	NO_STR
	SET_STR
	IPV6_STR
	"IPv6 next-hop address\n"
	"IPv6 local address\n"
	"IPv6 address of next hop\n")
{
	const char *xpath =
		"./set-action[action='frr-route-map:ipv6-next-hop']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* set metric <值|+值|-值|rtt|+rtt|-rtt|igp|aigp>：设置度量值，形态很多。
 * action 标识统一是 frr-route-map:set-metric，但依据用户所选形态，落到
 * rmap-set-action 下不同的叶子：
 *   rtt / igp / aigp / +rtt / -rtt —— 对应的布尔叶写 "true"；
 *   '+值' / '-值' —— 写 add-metric / subtract-metric，值取去掉正负号后的数字；
 *   普通数值 —— 写 value。
 * 坑：metric_str 是带符号字符串；对 '+值' / '-值' 用 ++metric_str 跳过头一个
 * 符号字符再存，所以这里会就地修改该字符串（只用于本次拼接，安全）。 */
DEFPY_YANG(
	set_metric, set_metric_cmd,
	"set metric <(-4294967295-4294967295)$metric|rtt$rtt|+rtt$artt|-rtt$srtt|igp$igp|aigp$aigp>",
	SET_STR
	"Metric value for destination routing protocol\n"
	"Metric value (use +/- for additions or subtractions)\n"
	"Assign round trip time\n"
	"Add round trip time\n"
	"Subtract round trip time\n"
	"Metric value from IGP protocol\n"
	"Metric value from AIGP (Accumulated IGP)\n")
{
	const char *xpath = "./set-action[action='frr-route-map:set-metric']";
	char xpath_value[XPATH_MAXLEN];
	char value[64];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	if (rtt) {
		snprintf(xpath_value, sizeof(xpath_value),
			 "%s/rmap-set-action/use-round-trip-time", xpath);
		snprintf(value, sizeof(value), "true");
	} else if (igp) {
		snprintf(xpath_value, sizeof(xpath_value), "%s/rmap-set-action/use-igp", xpath);
		snprintf(value, sizeof(value), "true");
	} else if (aigp) {
		snprintf(xpath_value, sizeof(xpath_value), "%s/rmap-set-action/use-aigp", xpath);
		snprintf(value, sizeof(value), "true");
	} else if (artt) {
		snprintf(xpath_value, sizeof(xpath_value),
			 "%s/rmap-set-action/add-round-trip-time", xpath);
		snprintf(value, sizeof(value), "true");
	} else if (srtt) {
		snprintf(xpath_value, sizeof(xpath_value),
			 "%s/rmap-set-action/subtract-round-trip-time", xpath);
		snprintf(value, sizeof(value), "true");
	} else if (metric_str && metric_str[0] == '+') {
		snprintf(xpath_value, sizeof(xpath_value),
			 "%s/rmap-set-action/add-metric", xpath);
		snprintf(value, sizeof(value), "%s", ++metric_str);
	} else if (metric_str && metric_str[0] == '-') {
		snprintf(xpath_value, sizeof(xpath_value),
			 "%s/rmap-set-action/subtract-metric", xpath);
		snprintf(value, sizeof(value), "%s", ++metric_str);
	} else {
		snprintf(xpath_value, sizeof(xpath_value),
			 "%s/rmap-set-action/value", xpath);
		snprintf(value, sizeof(value), "%s", metric_str);
	}
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, value);

	return nb_cli_apply_changes(vty, NULL);
}

/* no set metric：删除 set-metric 动作（无论之前是哪种形态，整体删除）。 */
DEFPY_YANG(
	no_set_metric, no_set_metric_cmd,
	"no set metric [OPTVAL]",
	NO_STR
	SET_STR
	"Metric value for destination routing protocol\n"
	"Metric value\n")
{
	const char *xpath = "./set-action[action='frr-route-map:set-metric']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* set min-metric (0-4294967295)：设置“最小度量”下限。
 * action 标识 frr-route-map:set-min-metric；值写到 rmap-set-action/min-metric。 */
DEFPY_YANG(set_min_metric, set_min_metric_cmd,
	   "set min-metric <(0-4294967295)$metric>",
	   SET_STR
	   "Minimum metric value for destination routing protocol\n"
	   "Minimum metric value\n")
{
	const char *xpath =
		"./set-action[action='frr-route-map:set-min-metric']";
	char xpath_value[XPATH_MAXLEN];
	char value[64];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/min-metric", xpath);
	snprintf(value, sizeof(value), "%s", metric_str);

	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, value);

	return nb_cli_apply_changes(vty, NULL);
}

/* no set min-metric：删除最小度量动作。 */
DEFPY_YANG(no_set_min_metric, no_set_min_metric_cmd,
	   "no set min-metric [(0-4294967295)]",
	   NO_STR SET_STR
	   "Minimum metric value for destination routing protocol\n"
	   "Minimum metric value\n")
{
	const char *xpath =
		"./set-action[action='frr-route-map:set-min-metric']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* set max-metric (0-4294967295)：设置“最大度量”上限。
 * action 标识 frr-route-map:set-max-metric；值写到 rmap-set-action/max-metric。 */
DEFPY_YANG(set_max_metric, set_max_metric_cmd,
	   "set max-metric <(0-4294967295)$metric>",
	   SET_STR
	   "Maximum metric value for destination routing protocol\n"
	   "Miximum metric value\n")
{
	const char *xpath =
		"./set-action[action='frr-route-map:set-max-metric']";
	char xpath_value[XPATH_MAXLEN];
	char value[64];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/max-metric", xpath);
	snprintf(value, sizeof(value), "%s", metric_str);

	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, value);

	return nb_cli_apply_changes(vty, NULL);
}

/* no set max-metric：删除最大度量动作。 */
DEFPY_YANG(no_set_max_metric, no_set_max_metric_cmd,
	   "no set max-metric [(0-4294967295)]",
	   NO_STR SET_STR
	   "Maximum Metric value for destination routing protocol\n"
	   "Maximum metric value\n")
{
	const char *xpath =
		"./set-action[action='frr-route-map:set-max-metric']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* set tag <untagged|(1-4294967295)>：设置 route tag。
 * action 标识 frr-route-map:set-tag；untagged 取 0，否则取 tagged，写入 tag 叶。
 * 与 match tag 同构：untagged/tagged 两个互斥 $变量合一成最终数值。 */
DEFPY_YANG(
	set_tag, set_tag_cmd,
	"set tag <untagged$untagged|(1-4294967295)$tagged>",
	SET_STR
	"Tag value for routing protocol\n"
	"Untagged route\n"
	"Tag value\n")
{
	const char *xpath = "./set-action[action='frr-route-map:set-tag']";
	char xpath_value[XPATH_MAXLEN];
	char value[64];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value), "%s/rmap-set-action/tag",
		 xpath);
	snprintfrr(value, sizeof(value), "%" PRIu64, tagged ? tagged : 0);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, value);

	return nb_cli_apply_changes(vty, NULL);
}

/* no set tag：删除 tag 设置动作。 */
DEFPY_YANG(
	no_set_tag, no_set_tag_cmd,
	"no set tag [<untagged|(1-4294967295)>]",
	NO_STR
	SET_STR
	"Tag value for routing protocol\n"
	"Untagged route\n"
	"Tag value\n")
{
	const char *xpath = "./set-action[action='frr-route-map:set-tag']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* set sr-te color (1-4294967295)：设置与 SR-TE 策略绑定的 color。
 * action 标识 frr-route-map:set-sr-te-color；值写到 rmap-set-action/policy。
 * 注意这里用 DEFUN_YANG，取值要靠 argv_find 按“(1-4294967295)”这段 token
 * 找到参数位置 idx，再取 argv[idx]->arg；找不到则 arg 为 NULL。 */
DEFUN_YANG (set_srte_color,
	    set_srte_color_cmd,
	    "set sr-te color (1-4294967295)",
	    SET_STR
	    SRTE_STR
	    SRTE_COLOR_STR
	    "Color of the SR-TE Policies to match with\n")
{
	const char *xpath =
		"./set-action[action='frr-route-map:set-sr-te-color']";
	char xpath_value[XPATH_MAXLEN];
	int idx = 0;

	char *arg = argv_find(argv, argc, "(1-4294967295)", &idx)
		? argv[idx]->arg
		: NULL;

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/policy", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* no set sr-te color：删除 SR-TE color 设置动作。 */
DEFUN_YANG (no_set_srte_color,
	    no_set_srte_color_cmd,
	    "no set sr-te color [(1-4294967295)]",
	    NO_STR
	    SET_STR
	    SRTE_STR
	    SRTE_COLOR_STR
	    "Color of the SR-TE Policies to match with\n")
{
	const char *xpath =
		"./set-action[action='frr-route-map:set-sr-te-color']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}


/* 【显示回调】把一个 set 动作节点回显成 CLI 文本（与 route_map_condition_show
 * 对称）。dnode 是某个 set-action 列表节点，其 key 叶 action 指明动作类型。
 * 同样用 IS_SET_* 宏做多分支派发，再从该动作对应的 rmap-set-action 子树
 * （可能带 frr-bgp-route-map: 等模块前缀）取值打印。
 * 较复杂的几类内部还有二级/三级判断：
 *   - set metric：先看 use-round-trip-time / use-igp / use-aigp 等布尔叶，
 *     再看 add/subtract-metric，最后才是普通 value。
 *   - community / extcommunity 类：有 exact-match / any 附加项要拼。
 *   - src / dst 地址、as-path 等：可能取多个候选子节点中第一个存在的。
 * 返回值：无；只写 vty。 */
void route_map_action_show(struct vty *vty, const struct lyd_node *dnode,
			   bool show_defaults)
{
	const char *action = yang_dnode_get_string(dnode, "action");
	const struct lyd_node *ln;
	const char *acl;

	if (IS_SET_IPv4_NH(action)) {
		vty_out(vty, " set ip next-hop %s\n",
			yang_dnode_get_string(
				dnode, "./rmap-set-action/ipv4-address"));
	} else if (IS_SET_IPv6_NH(action)) {
		vty_out(vty, " set ipv6 next-hop local %s\n",
			yang_dnode_get_string(
				dnode, "./rmap-set-action/ipv6-address"));
	} else if (IS_SET_METRIC(action)) {
		if (yang_dnode_get(dnode,
				   "./rmap-set-action/use-round-trip-time")) {
			vty_out(vty, " set metric rtt\n");
		} else if (yang_dnode_get(dnode, "./rmap-set-action/use-igp")) {
			vty_out(vty, " set metric igp\n");
		} else if (yang_dnode_get(dnode, "./rmap-set-action/use-aigp")) {
			vty_out(vty, " set metric aigp\n");
		} else if (yang_dnode_get(dnode, "./rmap-set-action/add-round-trip-time")) {
			vty_out(vty, " set metric +rtt\n");
		} else if (yang_dnode_get(dnode, "./rmap-set-action/subtract-round-trip-time")) {
			vty_out(vty, " set metric -rtt\n");
		} else if (yang_dnode_get(dnode, "./rmap-set-action/add-metric")) {
			vty_out(vty, " set metric +%s\n",
				yang_dnode_get_string(
					dnode, "./rmap-set-action/add-metric"));
		} else if (yang_dnode_get(dnode, "./rmap-set-action/subtract-metric")) {
			vty_out(vty, " set metric -%s\n",
				yang_dnode_get_string(
					dnode,
					"./rmap-set-action/subtract-metric"));
		} else {
			vty_out(vty, " set metric %s\n",
				yang_dnode_get_string(
					dnode, "./rmap-set-action/value"));
		}
	} else if (IS_SET_MIN_METRIC(action)) {
		vty_out(vty, " set min-metric %s\n",
			yang_dnode_get_string(dnode,
					      "./rmap-set-action/min-metric"));
	} else if (IS_SET_MAX_METRIC(action)) {
		vty_out(vty, " set max-metric %s\n",
			yang_dnode_get_string(dnode,
					      "./rmap-set-action/max-metric"));
	} else if (IS_SET_TAG(action)) {
		uint32_t tag =
			strtoul(yang_dnode_get_string(dnode,
						      "rmap-set-action/tag"),
				NULL, 10);

		if (!tag)
			vty_out(vty, " set tag untagged\n");
		else
			vty_out(vty, " set tag %u\n", tag);
	} else if (IS_SET_SR_TE_COLOR(action)) {
		vty_out(vty, " set sr-te color %s\n",
			yang_dnode_get_string(dnode,
					      "./rmap-set-action/policy"));
	} else if (IS_SET_SRC(action)) {
		if (yang_dnode_exists(
			    dnode,
			    "./rmap-set-action/frr-zebra-route-map:ipv4-src-address"))
			vty_out(vty, " set src %s\n",
				yang_dnode_get_string(
					dnode,
					"./rmap-set-action/frr-zebra-route-map:ipv4-src-address"));
		else
			vty_out(vty, " set src %s\n",
				yang_dnode_get_string(
					dnode,
					"./rmap-set-action/frr-zebra-route-map:ipv6-src-address"));
	} else if (IS_SET_METRIC_TYPE(action)) {
		vty_out(vty, " set metric-type %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-ospf-route-map:metric-type"));
	} else if (IS_SET_FORWARDING_ADDR(action)) {
		vty_out(vty, " set forwarding-address %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-ospf6-route-map:ipv6-address"));
	} else if (IS_SET_WEIGHT(action)) {
		vty_out(vty, " set weight %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:weight"));
	} else if (IS_SET_TABLE(action)) {
		vty_out(vty, " set table %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:table"));
	} else if (IS_SET_LOCAL_PREF(action)) {
		vty_out(vty, " set local-preference %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:local-pref"));
	} else if (IS_SET_LABEL_INDEX(action)) {
		vty_out(vty, " set label-index %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:label-index"));
	} else if (IS_SET_DISTANCE(action)) {
		vty_out(vty, " set distance %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:distance"));
	} else if (IS_SET_ORIGIN(action)) {
		vty_out(vty, " set origin %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:origin"));
	} else if (IS_SET_ATOMIC_AGGREGATE(action)) {
		vty_out(vty, " set atomic-aggregate\n");
	} else if (IS_SET_AIGP_METRIC(action)) {
		vty_out(vty, " set aigp-metric %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:aigp-metric"));
	} else if (IS_SET_ORIGINATOR_ID(action)) {
		vty_out(vty, " set originator-id %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:originator-id"));
	} else if (IS_SET_COMM_LIST_DEL(action)) {
		acl = NULL;
		if ((ln = yang_dnode_get(
				 dnode,
				 "./rmap-set-action/frr-bgp-route-map:comm-list-name"))
			!= NULL)
			acl = yang_dnode_get_string(ln, NULL);

		assert(acl);

		vty_out(vty, " set comm-list %s delete\n", acl);
	} else if (IS_SET_LCOMM_LIST_DEL(action)) {
		acl = NULL;
		if ((ln = yang_dnode_get(
				 dnode,
				 "./rmap-set-action/frr-bgp-route-map:comm-list-name"))
			!= NULL)
			acl = yang_dnode_get_string(ln, NULL);

		assert(acl);

		vty_out(vty, " set large-comm-list %s delete\n", acl);
	} else if (IS_SET_EXTCOMM_LIST_DEL(action)) {
		acl = NULL;
		ln = yang_dnode_get(dnode, "rmap-set-action/frr-bgp-route-map:comm-list-name");

		if (ln)
			acl = yang_dnode_get_string(ln, NULL);

		assert(acl);

		vty_out(vty, " set extended-comm-list %s delete\n", acl);
	} else if (IS_SET_LCOMMUNITY(action)) {
		if (yang_dnode_exists(
			    dnode,
			    "./rmap-set-action/frr-bgp-route-map:large-community-string"))
			vty_out(vty, " set large-community %s\n",
				yang_dnode_get_string(
					dnode,
					"./rmap-set-action/frr-bgp-route-map:large-community-string"));
		else {
			if (true
			    == yang_dnode_get_bool(
				    dnode,
				    "./rmap-set-action/frr-bgp-route-map:large-community-none"))
				vty_out(vty, " set large-community none\n");
		}
	} else if (IS_SET_COMMUNITY(action)) {
		if (yang_dnode_exists(
			    dnode,
			    "./rmap-set-action/frr-bgp-route-map:community-string"))
			vty_out(vty, " set community %s\n",
				yang_dnode_get_string(
					dnode,
					"./rmap-set-action/frr-bgp-route-map:community-string"));
		else {
			if (true
			    == yang_dnode_get_bool(
				    dnode,
				    "./rmap-set-action/frr-bgp-route-map:community-none"))
				vty_out(vty, " set community none\n");
		}
	} else if (IS_SET_EXTCOMMUNITY_RT(action)) {
		vty_out(vty, " set extcommunity rt %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:extcommunity-rt"));
	} else if (IS_SET_EXTCOMMUNITY_NT(action)) {
		vty_out(vty, " set extcommunity nt %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:extcommunity-nt"));
	} else if (IS_SET_EXTCOMMUNITY_SOO(action)) {
		vty_out(vty, " set extcommunity soo %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:extcommunity-soo"));
	} else if (IS_SET_EXTCOMMUNITY_LB(action)) {
		enum ecommunity_lb_type lb_type;
		char str[VTY_BUFSIZ];
		uint32_t bandwidth;

		lb_type = yang_dnode_get_enum(
			dnode,
			"./rmap-set-action/frr-bgp-route-map:extcommunity-lb/lb-type");
		switch (lb_type) {
		case EXPLICIT_BANDWIDTH:
			bandwidth = yang_dnode_get_uint32(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:extcommunity-lb/bandwidth");
			snprintf(str, sizeof(str), "%d", bandwidth);
			break;
		case CUMULATIVE_BANDWIDTH:
			snprintf(str, sizeof(str), "%s", "cumulative");
			break;
		case COMPUTED_BANDWIDTH:
			snprintf(str, sizeof(str), "%s", "num-multipaths");
		}

		if (yang_dnode_get_bool(
			    dnode,
			    "./rmap-set-action/frr-bgp-route-map:extcommunity-lb/two-octet-as-specific"))
			strlcat(str, " non-transitive", sizeof(str));

		vty_out(vty, " set extcommunity bandwidth %s\n", str);
	} else if (IS_SET_EXTCOMMUNITY_COLOR(action)) {
		vty_out(vty, " set extcommunity color %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:extcommunity-color"));
	} else if (IS_SET_EXTCOMMUNITY_NONE(action)) {
		if (yang_dnode_get_bool(
			    dnode,
			    "./rmap-set-action/frr-bgp-route-map:extcommunity-none"))
			vty_out(vty, " set extcommunity none\n");
	} else if (IS_SET_AGGREGATOR(action)) {
		vty_out(vty, " set aggregator as %s %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:aggregator/aggregator-asn"),
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:aggregator/aggregator-address"));
	} else if (IS_SET_AS_EXCLUDE(action)) {
		vty_out(vty, " set as-path exclude %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:exclude-as-path"));
	} else if (IS_SET_AS_REPLACE(action)) {
		vty_out(vty, " set as-path replace %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:replace-as-path"));
	} else if (IS_SET_AS_PREPEND(action)) {
		if (yang_dnode_exists(
			    dnode,
			    "./rmap-set-action/frr-bgp-route-map:prepend-as-path"))
			vty_out(vty, " set as-path prepend %s\n",
				yang_dnode_get_string(
					dnode,
					"./rmap-set-action/frr-bgp-route-map:prepend-as-path"));
		else {
			vty_out(vty, " set as-path prepend last-as %u\n",
				yang_dnode_get_uint8(
					dnode,
					"./rmap-set-action/frr-bgp-route-map:last-as"));
		}
	} else if (IS_SET_IPV6_NH_GLOBAL(action)) {
		vty_out(vty, " set ipv6 next-hop global %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:ipv6-address"));
	} else if (IS_SET_IPV6_VPN_NH(action)) {
		vty_out(vty, " set ipv6 vpn next-hop %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:ipv6-address"));
	} else if (IS_SET_IPV6_PEER_ADDR(action)) {
		if (true
		    == yang_dnode_get_bool(
			    dnode,
			    "./rmap-set-action/frr-bgp-route-map:preference"))
			vty_out(vty, " set ipv6 next-hop peer-address\n");
	} else if (IS_SET_IPV6_PREFER_GLOBAL(action)) {
		if (true
		    == yang_dnode_get_bool(
			    dnode,
			    "./rmap-set-action/frr-bgp-route-map:preference"))
			vty_out(vty, " set ipv6 next-hop prefer-global\n");
	} else if (IS_SET_IPV4_VPN_NH(action)) {
		vty_out(vty, " set ipv4 vpn next-hop %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:ipv4-address"));
	} else if (IS_SET_BGP_IPV4_NH(action)) {
		vty_out(vty, " set ip next-hop %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:ipv4-nexthop"));
	} else if (IS_SET_BGP_EVPN_GATEWAY_IP_IPV4(action)) {
		vty_out(vty, " set evpn gateway-ip ipv4 %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:evpn-gateway-ip-ipv4"));
	} else if (IS_SET_BGP_EVPN_GATEWAY_IP_IPV6(action)) {
		vty_out(vty, " set evpn gateway-ip ipv6 %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:evpn-gateway-ip-ipv6"));
	} else if (IS_SET_BGP_L3VPN_NEXTHOP_ENCAPSULATION(action)) {
		vty_out(vty, " set l3vpn next-hop encapsulation %s\n",
			yang_dnode_get_string(
				dnode,
				"./rmap-set-action/frr-bgp-route-map:l3vpn-nexthop-encapsulation"));
	}
}

/* on-match next：本 entry 命中后不终止，继续处理下一条 entry。
 * 实现：把当前 entry 的 ./exit-policy 叶子设为 "next"（相对路径，靠当前
 * route-map 视图上下文解析）。该叶子是枚举型 exit-policy（见下文 show）。 */
DEFPY_YANG(
	rmap_onmatch_next, rmap_onmatch_next_cmd,
	"on-match next",
	"Exit policy on matches\n"
	"Next clause\n")
{
	nb_cli_enqueue_change(vty, "./exit-policy", NB_OP_MODIFY, "next");

	return nb_cli_apply_changes(vty, NULL);
}

/* no on-match next：删除 exit-policy 设置，恢复默认（permit-or-deny）行为。 */
DEFPY_YANG(
	no_rmap_onmatch_next,
	no_rmap_onmatch_next_cmd,
	"no on-match next",
	NO_STR
	"Exit policy on matches\n"
	"Next clause\n")
{
	nb_cli_enqueue_change(vty, "./exit-policy", NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* on-match goto N：本 entry 命中后跳到序号为 N 的 entry 继续处理。
 * 实现：exit-policy 设为 "goto"，并把目标序号写入 ./goto-value。
 * $rm_num 对应的字符串变量是 rm_num_str。两步都 enqueue 后统一 apply。 */
DEFPY_YANG(
	rmap_onmatch_goto, rmap_onmatch_goto_cmd,
	"on-match goto (1-65535)$rm_num",
	"Exit policy on matches\n"
	"Goto Clause number\n"
	"Number\n")
{
	nb_cli_enqueue_change(vty, "./exit-policy", NB_OP_MODIFY, "goto");
	nb_cli_enqueue_change(vty, "./goto-value", NB_OP_MODIFY, rm_num_str);

	return nb_cli_apply_changes(vty, NULL);
}

/* no on-match goto：删除 exit-policy（goto-value 作为其配套叶子随之一并清除）。 */
DEFPY_YANG(
	no_rmap_onmatch_goto, no_rmap_onmatch_goto_cmd,
	"no on-match goto [(1-65535)$rm_num]",
	NO_STR
	"Exit policy on matches\n"
	"Goto Clause number\n"
	"Number\n")
{
	nb_cli_enqueue_change(vty, "./exit-policy", NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* 兼容别名：Cisco / GNU Zebra 风格。ALIAS_YANG 不新建逻辑，只把已有函数
 * rmap_onmatch_goto 再挂上另一条命令字串 continue。因此 continue 与
 * on-match goto 完全等价（同一实现、同一数据）。 */
/* Cisco/GNU Zebra compatibility aliases */
ALIAS_YANG(
	rmap_onmatch_goto, rmap_continue_cmd,
	"continue (1-65535)$rm_num",
	"Continue on a different entry within the route-map\n"
	"Route-map entry sequence number\n")

/* 兼容别名：no continue 等价于 no on-match goto（同一实现）。 */
ALIAS_YANG(
	no_rmap_onmatch_goto, no_rmap_continue_cmd,
	"no continue [(1-65535)]",
	NO_STR
	"Continue on a different entry within the route-map\n"
	"Route-map entry sequence number\n")

/* 【显示回调】把 exit-policy 节点回显成 CLI。
 * exit-policy 是枚举：0=permit-or-deny(默认，什么都不打印) / 1=next / 2=goto。
 * goto 时再从同级 ../goto-value 取目标序号一并打印。
 * 入参 dnode 即 exit-policy 叶子本身，故 yang_dnode_get_enum(dnode, NULL)
 * 直接取它自己的值（第二参数 NULL 表示“就是本节点”）。 */
void route_map_exit_policy_show(struct vty *vty, const struct lyd_node *dnode,
				bool show_defaults)
{
	int exit_policy = yang_dnode_get_enum(dnode, NULL);

	switch (exit_policy) {
	case 0: /* permit-or-deny */
		/* NOTHING: default option. */
		break;
	case 1: /* next */
		vty_out(vty, " on-match next\n");
		break;
	case 2: /* goto */
		vty_out(vty, " on-match goto %s\n",
			yang_dnode_get_string(dnode, "../goto-value"));
		break;
	}
}

/* call NAME：本 entry 匹配+设置后，转去调用另一条 route-map（嵌套）。 */
DEFPY_YANG(
	rmap_call, rmap_call_cmd,
	"call WORD$name",
	"Jump to another Route-Map after match+set\n"
	"Target route-map name\n")
{
	nb_cli_enqueue_change(vty, "./call", NB_OP_MODIFY, name);

	return nb_cli_apply_changes(vty, NULL);
}

/* no call：删除 call 设置。 */
DEFPY_YANG(
	no_rmap_call, no_rmap_call_cmd,
	"no call [NAME]",
	NO_STR
	"Jump to another Route-Map after match+set\n"
	"Target route-map name\n")
{
	nb_cli_enqueue_change(vty, "./call", NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【显示回调】回显 call 目标名称。dnode 即 call 叶子，get_string(dnode, NULL) 取自身值。 */
void route_map_call_show(struct vty *vty, const struct lyd_node *dnode,
			 bool show_defaults)
{
	vty_out(vty, " call %s\n", yang_dnode_get_string(dnode, NULL));
}

/* description LINE...：给 route-map（entry）加一段中文/任意文本描述。
 * 关键：LINE... 会匹配后续全部单词，用 argv_concat(argv, argc, 1) 把从第 1 个
 * 参数起的词拼回一个完整字符串（含空格）。写完后必须 XFREE 释放临时串，否则泄漏。
 * 返回值：转发 nb_cli_apply_changes 的结果（参数解析失败时可能非 SUCCESS）。 */
DEFPY_YANG(
	rmap_description, rmap_description_cmd,
	"description LINE...",
	"Route-map comment\n"
	"Comment describing this route-map rule\n")
{
	char *desc;
	int rv;

	desc = argv_concat(argv, argc, 1);
	nb_cli_enqueue_change(vty, "./description", NB_OP_MODIFY, desc);
	rv = nb_cli_apply_changes(vty, NULL);
	XFREE(MTYPE_TMP, desc);

	return rv;
}

/* no description：删除描述文本。 */
DEFUN_YANG (no_rmap_description,
       no_rmap_description_cmd,
       "no description",
       NO_STR
       "Route-map comment\n")
{
	nb_cli_enqueue_change(vty, "./description", NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【显示回调】回显 description 一行（dnode 即 description 叶子自身值）。 */
void route_map_description_show(struct vty *vty, const struct lyd_node *dnode,
				bool show_defaults)
{
	vty_out(vty, " description %s\n", yang_dnode_get_string(dnode, NULL));
}

/* [no] route-map NAME optimization：开启/关闭 route-map 的优化（如索引加速）。
 * 一条命令同时承载 set / unset：靠可选的 [no] 前缀，$no 变量据此为真/假。
 * 注意这里 xpath 被 snprintf 写了两次（先定位 route-map 容器并 CREATE，
 * 再切到其 optimization-disabled 叶子并 MODIFY），因为要复用同一个缓冲区。
 * 传值：no 为真写 "true"（禁用优化），否则写 "false"（启用优化）。 */
DEFPY_YANG(
	route_map_optimization, route_map_optimization_cmd,
	"[no] route-map RMAP_NAME$name optimization",
	NO_STR
	ROUTE_MAP_CMD_STR
	"Configure route-map optimization\n")
{
	char xpath[XPATH_MAXLEN];

	snprintf(xpath, sizeof(xpath),
		 "/frr-route-map:lib/route-map[name='%s']", name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(
		xpath, sizeof(xpath),
		"/frr-route-map:lib/route-map[name='%s']/optimization-disabled",
		name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_MODIFY, no ? "true" : "false");

	return nb_cli_apply_changes(vty, NULL);
}

/* 【显示回调】回显 optimization 开关状态：优化被禁用时输出 “no route-map X optimization”。
 * name 取自父节点 ../name；disabled 是本叶子的布尔值。 */
void route_map_optimization_disabled_show(struct vty *vty,
					  const struct lyd_node *dnode,
					  bool show_defaults)
{
	const char *name = yang_dnode_get_string(dnode, "../name");
	const bool disabled = yang_dnode_get_bool(dnode, NULL);

	vty_out(vty, "%sroute-map %s optimization\n", disabled ? "no " : "",
		name);
}

/* 配置写出回调：show running-config 里生成整段 route-map 配置。
 * 从运行配置树里取出 /frr-route-map:lib 子树，交给 nb_cli_show_dnode_cmds()
 * 遍历，由各节点的 show 回调（route_map_instance_show / condition / action 等）
 * 逐行打印。有内容则返回 1（已写），否则 0。 */
static int route_map_config_write(struct vty *vty)
{
	const struct lyd_node *dnode;
	int written = 0;

	dnode = yang_dnode_get(running_config->dnode,
			       "/frr-route-map:lib");
	if (dnode) {
		nb_cli_show_dnode_cmds(vty, dnode, false);
		written = 1;
	}

	return written;
}

/* 定义 route-map 专用 CLI 节点（config-route-map）：进入该节点即进入某条
 * route-map entry 的编辑视图。parent_node=CONFIG_NODE 表示它挂在全局配置下；
 * RMAP_NODE 是节点编号常量；config_write 指向上面的写出函数。 */
/* Route map node structure. */
static int route_map_config_write(struct vty *vty);
static struct cmd_node rmap_node = {
	.name = "routemap",
	.node = RMAP_NODE,
	.parent_node = CONFIG_NODE,
	.prompt = "%s(config-route-map)# ",
	.config_write = route_map_config_write,
};

/* Tab 补全回调：把当前所有 route-map 的名字加入补全候选。
 * 遍历 route_map_master 全局链表的 head，对每个 name 用 XSTRDUP 复制一份
 * （补全框架会接管该内存），再 vector_set 放入候选向量 comps。 */
static void rmap_autocomplete(vector comps, struct cmd_token *token)
{
	struct route_map *map;

	for (map = route_map_master.head; map; map = map->next)
		vector_set(comps, XSTRDUP(MTYPE_COMPLETION, map->name));
}

/* 变量处理器表：把命令模板里出现的变量名 / token 名绑定到补全回调。
 * varname 匹配 $变量名；tokenname 匹配形如 RMAP_NAME 的 token。
 * 末尾必须以一个 .completions = NULL 的哨兵项结束，install 时据此判断表尾。 */
static const struct cmd_variable_handler rmap_var_handlers[] = {
	{.varname = "route_map", .completions = rmap_autocomplete},
	{.tokenname = "ROUTEMAP_NAME", .completions = rmap_autocomplete},
	{.tokenname = "RMAP_NAME", .completions = rmap_autocomplete},
	{.completions = NULL}
};

/* 模块初始化入口：注册补全处理器，并安装 CLI 节点与所有命令。
 * 只有 install 过的命令才能在 vty 上被解析到。注意区分安装到哪个节点：
 *   - route-map 主命令 / no … / optimization 装在 CONFIG_NODE（全局视图）；
 *   - on-match / call / description / match / set 装在 RMAP_NODE（route-map 视图）。
 * 由 libfrr 在启动时调用一次。 */
void route_map_cli_init(void)
{
	/* Auto complete handler. */
	cmd_variable_handler_register(rmap_var_handlers);

	/* CLI commands. */
	install_node(&rmap_node);
	install_default(RMAP_NODE);
	install_element(CONFIG_NODE, &route_map_cmd);
	install_element(CONFIG_NODE, &no_route_map_cmd);
	install_element(CONFIG_NODE, &no_route_map_all_cmd);
	install_element(CONFIG_NODE, &route_map_optimization_cmd);

	/* Install the on-match stuff */
	install_element(RMAP_NODE, &rmap_onmatch_next_cmd);
	install_element(RMAP_NODE, &no_rmap_onmatch_next_cmd);
	install_element(RMAP_NODE, &rmap_onmatch_goto_cmd);
	install_element(RMAP_NODE, &no_rmap_onmatch_goto_cmd);
	install_element(RMAP_NODE, &rmap_continue_cmd);
	install_element(RMAP_NODE, &no_rmap_continue_cmd);

	/* Install the call stuff. */
	install_element(RMAP_NODE, &rmap_call_cmd);
	install_element(RMAP_NODE, &no_rmap_call_cmd);

	/* Install description commands. */
	install_element(RMAP_NODE, &rmap_description_cmd);
	install_element(RMAP_NODE, &no_rmap_description_cmd);

	/* Install 'match' commands. */
	install_element(RMAP_NODE, &match_interface_cmd);
	install_element(RMAP_NODE, &no_match_interface_cmd);

	install_element(RMAP_NODE, &match_ip_address_cmd);
	install_element(RMAP_NODE, &no_match_ip_address_cmd);

	install_element(RMAP_NODE, &match_ip_address_prefix_list_cmd);
	install_element(RMAP_NODE, &no_match_ip_address_prefix_list_cmd);

	install_element(RMAP_NODE, &match_ip_next_hop_cmd);
	install_element(RMAP_NODE, &no_match_ip_next_hop_cmd);

	install_element(RMAP_NODE, &match_ip_next_hop_prefix_list_cmd);
	install_element(RMAP_NODE, &no_match_ip_next_hop_prefix_list_cmd);

	install_element(RMAP_NODE, &match_ip_next_hop_type_cmd);
	install_element(RMAP_NODE, &no_match_ip_next_hop_type_cmd);

	install_element(RMAP_NODE, &match_ipv6_address_cmd);
	install_element(RMAP_NODE, &no_match_ipv6_address_cmd);

	install_element(RMAP_NODE, &match_ipv6_address_prefix_list_cmd);
	install_element(RMAP_NODE, &no_match_ipv6_address_prefix_list_cmd);

	install_element(RMAP_NODE, &match_ipv6_next_hop_type_cmd);
	install_element(RMAP_NODE, &no_match_ipv6_next_hop_type_cmd);

	install_element(RMAP_NODE, &match_ipv6_next_hop_cmd);
	install_element(RMAP_NODE, &match_ipv6_next_hop_prefix_list_cmd);
	install_element(RMAP_NODE, &no_match_ipv6_next_hop_cmd);
	install_element(RMAP_NODE, &no_match_ipv6_next_hop_prefix_list_cmd);

	install_element(RMAP_NODE, &match_metric_cmd);
	install_element(RMAP_NODE, &no_match_metric_cmd);

	install_element(RMAP_NODE, &match_tag_cmd);
	install_element(RMAP_NODE, &no_match_tag_cmd);

	/* Install 'set' commands. */
	install_element(RMAP_NODE, &set_ip_nexthop_cmd);
	install_element(RMAP_NODE, &no_set_ip_nexthop_cmd);

	install_element(RMAP_NODE, &set_ipv6_nexthop_local_cmd);
	install_element(RMAP_NODE, &no_set_ipv6_nexthop_local_cmd);

	install_element(RMAP_NODE, &set_metric_cmd);
	install_element(RMAP_NODE, &no_set_metric_cmd);

	install_element(RMAP_NODE, &set_min_metric_cmd);
	install_element(RMAP_NODE, &no_set_min_metric_cmd);

	install_element(RMAP_NODE, &set_max_metric_cmd);
	install_element(RMAP_NODE, &no_set_max_metric_cmd);

	install_element(RMAP_NODE, &set_tag_cmd);
	install_element(RMAP_NODE, &no_set_tag_cmd);

	install_element(RMAP_NODE, &set_srte_color_cmd);
	install_element(RMAP_NODE, &no_set_srte_color_cmd);
}
