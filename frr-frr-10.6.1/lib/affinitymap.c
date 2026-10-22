// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Affinity map function.
 *
 * Copyright 2022 Hiroki Shirokura, LINE Corporation
 * Copyright 2022 Masakazu Asama
 * Copyright 2022 6WIND S.A.
 */

/*
 * 【文件导读】affinitymap.c —— 亲和映射(名字 -> 位序号)的核心实现
 * =====================================================================
 * 【职责】维护全局单例 affinity_map_master，提供“按名字增删查映射”以及
 *   “位下标变更回调”的触发与注册逻辑。数据模型见 affinitymap.h。
 *
 * 【内部结构】映射存在一条无序链表里，查找是 O(n) 线性 strncmp 比较；
 *   对本模块预期的映射规模(几十条)足够。
 *
 * 【两个关键不变量】
 *   1) 名字匹配一律用 strncmp(..., AFFINITY_NAME_SIZE)，即定长前缀比较；
 *   2) update_hook 只在“已存在映射”的位下标被改时触发，纯新建不触发。
 *
 * 【内存】映射对象统一用 MTYPE_AFFINITY_MAP 统计，由 affinity_map_free 释放。
 *
 * 【阅读顺序】内存类型 / QOBJ 定义 -> 全局实例 -> free 辅助 -> set / unset / get
 *   -> update_hook 触发与注册 -> terminate。
 * =====================================================================
 */
#include <zebra.h>

#include "linklist.h"
#include "memory.h"
#include "command.h"
#include "vector.h"
#include "prefix.h"
#include "vty.h"
#include "affinitymap.h"
#include "command.h"
#include "log.h"
#include "hash.h"
#include "libfrr.h"
#include "lib_errors.h"
#include "table.h"
#include "json.h"
#include "jhash.h"

/* 【内存类型】为亲和映射对象注册独立的内存统计标签 "Affinity map"，
 * 便于 show memory 区分其分配量；STATIC 表示仅本文件使用。 */
DEFINE_MTYPE_STATIC(LIB, AFFINITY_MAP, "Affinity map");

/* 【QOBJ 定义】与头文件里的 DECLARE_QOBJ_TYPE 配对，实例化 QOBJ 元数据。 */
DEFINE_QOBJ_TYPE(affinity_maps);
DEFINE_QOBJ_TYPE(affinity_map);

/* 【全局单例】亲和映射总容器；maps 惰性初始化(首次 set 时才建链表)，
 * update_hook 初始为 NULL(无人订阅)。 */
struct affinity_maps affinity_map_master = {NULL, NULL};

/* 【辅助函数】释放一条映射对象(内部调用，外部不应持有该指针)。
 * 参数 map : 要释放的节点。
 * 返回：无。副作用：把内存归还到 MTYPE_AFFINITY_MAP 统计；
 *   注意它**不**负责把节点从 master.maps 链表中摘除。 */
static void affinity_map_free(struct affinity_map *map)
{
	XFREE(MTYPE_AFFINITY_MAP, map);
}

/* 【核心函数】按名字设置(或新建)一条亲和映射的位下标。
 * 参数 name : 映射名字(比较上限 AFFINITY_NAME_SIZE)。
 * 参数 pos  : 位下标；此处只存不校验范围(合法范围由配置层 / YANG 约束保证)。
 * 返回：无。
 * 副作用：1) 首次调用惰性创建 master.maps 链表；2) 名字已存在时只改 bit_position
 *   后直接返回(此路径不触发回调——回调需调用方另行调用 affinity_map_update_hook)。
 * 易踩的坑：找不到就 XCALLOC 新建，名字用 snprintf 截断到定长；不检查位下标冲突。 */
void affinity_map_set(const char *name, int pos)
{
	struct listnode *node;
	struct affinity_map *map;

	if (!affinity_map_master.maps)
		affinity_map_master.maps = list_new();

	for (ALL_LIST_ELEMENTS_RO(affinity_map_master.maps, node, map)) {
		if (strncmp(name, map->name, AFFINITY_NAME_SIZE) != 0)
			continue;
		map->bit_position = pos;
		return;
	}

	map = XCALLOC(MTYPE_AFFINITY_MAP, sizeof(*map));
	map->bit_position = pos;
	snprintf(map->name, sizeof(map->name), "%s", name);
	listnode_add(affinity_map_master.maps, map);
}

/* 【核心函数】按名字删除一条亲和映射。
 * 参数 name : 要删除的映射名字。
 * 返回：无。找不到则静默返回。
 * 副作用：遍历用 ALL_LIST_ELEMENTS(带安全后继 nnode)，因为循环体内会删除当前节点；
 *   删除后立即 free 并返回。 */
void affinity_map_unset(const char *name)
{
	struct listnode *node, *nnode;
	struct affinity_map *map;

	if (!affinity_map_master.maps)
		return;

	for (ALL_LIST_ELEMENTS(affinity_map_master.maps, node, nnode, map)) {
		if (strncmp(name, map->name, AFFINITY_NAME_SIZE) != 0)
			continue;
		listnode_delete(affinity_map_master.maps, map);
		affinity_map_free(map);
		return;
	}
}

/* 【核心函数】按名字查映射(只读遍历，不会创建)。
 * 参数 name : 映射名字。
 * 返回：命中的 struct affinity_map 指针(内部持有)；未命中或链表未建返回 NULL。 */
struct affinity_map *affinity_map_get(const char *name)
{
	struct listnode *node;
	struct affinity_map *map;

	if (!affinity_map_master.maps)
		return NULL;

	for (ALL_LIST_ELEMENTS_RO(affinity_map_master.maps, node, map))
		if (strncmp(name, map->name, AFFINITY_NAME_SIZE) == 0)
			return map;
	return NULL;
}

/* 【核心函数】把“位下标变更”通知给已注册的 update_hook。
 * 参数 affmap_name : 发生变更的映射名字。
 * 参数 new_pos     : 新的位下标。
 * 返回：无。副作用：调用 master.update_hook(名字, 旧位下标, 新位下标)。
 * 逻辑要点：无 hook 直接返回；映射不存在(属于新建)也直接返回，即“只对已存在的映射
 *   报告变更”。旧位下标取自当前 map->bit_position。 */
void affinity_map_update_hook(const char *affmap_name, uint16_t new_pos)
{
	struct affinity_map *map;

	if (!affinity_map_master.update_hook)
		return;

	map = affinity_map_get(affmap_name);

	if (!map)
		/* Affinity-map creation */
		return;

	(*affinity_map_master.update_hook)(affmap_name, map->bit_position,
					   new_pos);
}

/* 【核心函数】注册 / 注销全局位下标变更回调。
 * 参数 func : 回调函数指针；传 NULL 即注销。
 * 返回：无。副作用：覆盖 master.update_hook(全局单槽，后设置者胜)。 */
void affinity_map_set_update_hook(void (*func)(const char *affmap_name,
					       uint16_t old_pos,
					       uint16_t new_pos))
{
	affinity_map_master.update_hook = func;
}

/* 【清理函数】释放所有映射节点(用于进程退出或模块重配)。
 * 返回：无。副作用：遍历并 free 每条映射(同样带 nnode 安全遍历)。
 * 注意：本函数不把 master.maps 置 NULL，也不释放链表本身；
 *   若之后继续使用本模块，需留意枚举器 / 链表状态。 */
void affinity_map_terminate(void)
{
	struct affinity_map *map;
	struct listnode *node, *nnode;

	for (ALL_LIST_ELEMENTS(affinity_map_master.maps, node, nnode, map))
		affinity_map_free(map);
}
