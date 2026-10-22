// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Affinity map northbound CLI implementation.
 *
 * Copyright 2022 Hiroki Shirokura, LINE Corporation
 * Copyright 2022 Masakazu Asama
 * Copyright 2022 6WIND S.A.
 */

/*
 * 【文件导读】affinitymap_cli.c —— 亲和映射的 VTY / CLI 层
 * =====================================================================
 * 【职责】提供 affinity-map 命令的解析与配置下发，以及 show running 时的回显。
 *   真正的存储 / 回调逻辑在 affinitymap.c；本文件只做“命令行 <-> YANG 数据”的桥接。
 *
 * 【关键机制：北向 CLI(northbound CLI)】
 *   DEFPY_YANG_NOSH 生成的命令体并不直接改内存，而是通过 nb_cli_enqueue_change()
 *   把一条 XPath 变更排入队列，再由 nb_cli_apply_changes() 统一提交到 YANG
 *   datastore；提交时会回调 affinitymap_northbound.c 里的 create / destroy /
 *   modify 回调，最终才落到 affinity_map_set / unset。这种“间接”是理解本文件的钥匙。
 *
 * 【两条命令】
 *   affinity-map NAME bit-position (0-1023)  : 新建 / 修改映射(对应 value 的 MODIFY)。
 *   no affinity-map NAME [bit-position ...]  : 删除整个映射容器(DESTROY)。
 *
 * 【位号上限】CLI 里字面写死 (0-1023)，注释也说 max 是
 *   EXT_ADMIN_GROUP_MAX_POSITIONS - 1(=1023)；改动上限需同步改这里与管理组的常量。
 *
 * 【回显】cli_show_affinity_map 被 northbound 框架在生成配置文本时调用，负责把
 *   datastore 里的 affinity-map 打印回 "affinity-map NAME bit-position N" 形式。
 * =====================================================================
 */
#include <zebra.h>

#include "lib/command.h"
#include "lib/northbound_cli.h"
#include "lib/affinitymap.h"
/* 【生成头】affinitymap_cli_clippy.c 由 clippy 工具根据下方 DEFPY 自动生成，
 * 内含命令字符串表 / 可选值表等；不手工编辑。 */
#include "lib/affinitymap_cli_clippy.c"

/* max value is EXT_ADMIN_GROUP_MAX_POSITIONS - 1 */
/* 【命令】affinity-map NAME bit-position (0-1023)
 * 作用：新建或修改一条亲和映射的位下标。
 * 实现：把 XPath /frr-affinity-map:lib/affinity-maps/affinity-map[name='NAME']/value
 *   以 NB_OP_MODIFY 入队，再统一 apply；真正落库由北向回调完成。
 * 参数：$name 映射名；$position_str 位下标字符串(0-1023，CLI 已做范围校验)。
 * 返回：nb_cli_apply_changes 的结果(CMD_SUCCESS / CMD_WARNING 等)。
 * 【易踩的坑】这里传的是 position_str(字符串)而非数值，交由 YANG 层解析。 */
DEFPY_YANG_NOSH(affinity_map, affinity_map_cmd,
		"affinity-map NAME$name bit-position (0-1023)$position",
		"Affinity map configuration\n"
		"Affinity attribute name\n"
		"Bit position for affinity attribute value\n"
		"Bit position\n")
{
	char xpathr[XPATH_MAXLEN];

	snprintf(
		xpathr, sizeof(xpathr),
		"/frr-affinity-map:lib/affinity-maps/affinity-map[name='%s']/value",
		name);
	nb_cli_enqueue_change(vty, xpathr, NB_OP_MODIFY, position_str);
	return nb_cli_apply_changes(vty, NULL);
}

/* max value is EXT_ADMIN_GROUP_MAX_POSITIONS - 1 */
/* 【命令】no affinity-map NAME [bit-position (0-1023)]
 * 作用：删除整条亲和映射(不管是否带 bit-position，都删整个容器)。
 * 实现：对 XPath /frr-affinity-map:lib/affinity-maps/affinity-map[name='NAME']
 *   以 NB_OP_DESTROY 入队。
 * 参数：$name 映射名；$position 可选且被忽略(保留以便与正向命令对称)。
 * 返回：nb_cli_apply_changes 的结果。
 * 【易踩的坑】带不带 bit-position 行为一致——都是删整个映射。 */
DEFPY_YANG_NOSH(no_affinity_map, no_affinity_map_cmd,
		"no affinity-map NAME$name [bit-position (0-1023)$position]",
		NO_STR
		"Affinity map configuration\n"
		"Affinity attribute name\n"
		"Bit position for affinity attribute value\n"
		"Bit position\n")
{
	char xpathr[XPATH_MAXLEN];

	snprintf(xpathr, sizeof(xpathr),
		 "/frr-affinity-map:lib/affinity-maps/affinity-map[name='%s']",
		 name);
	nb_cli_enqueue_change(vty, xpathr, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【回调】把一条 affinity-map 节点回显成配置文本。
 * 参数 vty           : 输出终端。
 * 参数 dnode         : 当前节点(取自 YANG datastore)。
 * 参数 show_defaults : 未使用(已标注 __attribute__((__unused__)))。
 * 返回：无，直接 vty_out。
 * 副作用：从 dnode 取下 name 与 value(位下标) 打印。 */
static void cli_show_affinity_map(struct vty *vty, const struct lyd_node *dnode,
			   bool show_defaults __attribute__((__unused__)))
{
	vty_out(vty, "affinity-map %s bit-position %u\n",
		yang_dnode_get_string(dnode, "name"),
		yang_dnode_get_uint16(dnode, "value"));
}

/* 【YANG 模块描述符】frr-affinity-map 的 CLI 回显回调表。
 *   .ignore_cfg_cbs = true : 表示本模块不注册常规配置回调(配置由 northbound 文件负责)，
 *     此处只提供 cli_show，用于 show running。
 *   .nodes : 绑定 /frr-affinity-map:lib/affinity-maps/affinity-map 的回显函数。 */
const struct frr_yang_module_info frr_affinity_map_cli_info = {
	.name = "frr-affinity-map",
	.ignore_cfg_cbs = true,
	.nodes = {
		{
			.xpath = "/frr-affinity-map:lib/affinity-maps/affinity-map",
			.cbs.cli_show = cli_show_affinity_map,
		},
		{
			.xpath = NULL,
		},
	}
};

/* Initialization of affinity map vector. */
/* 【初始化】在 CONFIG 节点安装两条 CLI 命令(模块的 init 入口)。
 * 返回：无。副作用：install_element 注册 affinity_map_cmd 与 no_affinity_map_cmd。
 * 注意：与之配对的 affinity_map_terminate 在 affinitymap.c 中实现。 */
void affinity_map_init(void)
{
	/* CLI commands. */
	install_element(CONFIG_NODE, &affinity_map_cmd);
	install_element(CONFIG_NODE, &no_affinity_map_cmd);
}
