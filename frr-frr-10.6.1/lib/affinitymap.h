// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Affinity-map function.
 *
 * Copyright 2022 Hiroki Shirokura, LINE Corporation
 * Copyright 2022 Masakazu Asama
 * Copyright 2022 6WIND S.A.
 */

/*
 * 【文件导读】affinitymap.h —— “亲和映射”(Affinity Map)模块对外接口
 * =====================================================================
 * 【是什么】亲和映射把“一个字符串名字”映射到“一个位序号(bit position)”。
 * 它是 FRR 里表达“接口 / 链路属性 -> 位图某一位”的通用小工具：例如 flex-algo
 * (灵活算法) 或 SR 策略需要用命名亲和(affinity)给链路打标签时，就需要这种
 * “名字 -> 位下标”的稳定映射。名字由用户配置(如 red、blue)，位下标 0..1023。
 *
 * 【数据模型】
 *   struct affinity_map  : 单条映射(名字 + 位下标)，带 QOBJ 类型信息。
 *   struct affinity_maps : 全局容器，内部是链表 maps，并挂一个 update_hook
 *                          回调，用于在“某映射的位下标被改动”时通知订阅者
 *                          (典型订阅者是北向 YANG 层或 flex-algo 消费者)。
 *
 * 【名字长度】AFFINITY_NAME_SIZE=32，所有比较都用它做上限，比较语义是
 *   strncmp，属“前缀相等即视为同名”；名字不长于上限时也就是全名相等。
 *
 * 【两套 YANG 模块】frr_affinity_map_info 是北向数据 / 配置回调(定义于
 *   affinitymap_northbound.c)；frr_affinity_map_cli_info 只负责把配置回显到
 *   show running(定义于 affinitymap_cli.c)。
 *
 * 【易踩的坑】
 *   1) 名字是定长 32 的数组，用 strncmp 比较，没有长度参数校验；
 *   2) affinity_map_set 内部按名字查找，找到就改位下标，找不到才新建；
 *   3) update_hook 只在“已存在映射被改位下标”时触发，纯新建映射时不触发。
 * =====================================================================
 */
#ifndef _ZEBRA_AFFINITYMAP_H
#define _ZEBRA_AFFINITYMAP_H

/* 【依赖】typesafe(类型安全容器) / prefix(网络前缀) / memory(内存统计) /
 * qobj(可被 show 枚举的对象) / vty(虚拟终端) / plist(前缀列表)。 */
#include "typesafe.h"
#include "prefix.h"
#include "memory.h"
#include "qobj.h"
#include "vty.h"
#include "lib/plist.h"
#include "lib/plist_int.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 【常量】亲和映射名字的最大长度(含结尾 '\0')。所有名字比较 / 拷贝都以此为上界。 */
#define AFFINITY_NAME_SIZE 32

/* 【结构体】单条亲和映射：一个名字对应一个位下标。
 *   name         : 定长 32 字节的名字缓冲(由 affinity_map_set 用 snprintf 填充)。
 *   bit_position : 该名字对应的位序号(在管理组 / 亲和位图中充当位下标)。
 *   QOBJ_FIELDS  : 融入 FRR 的 QOBJ(可被 show 命令按类型枚举)基础设施。 */
struct affinity_map {
	char name[AFFINITY_NAME_SIZE];
	uint16_t bit_position;

	QOBJ_FIELDS;
};
/* 【宏】向 QOBJ 系统注册 affinity_map 类型；对应 .c 里的 DEFINE_QOBJ_TYPE。 */
DECLARE_QOBJ_TYPE(affinity_map);

/* 【结构体】亲和映射的全局容器(单例 affinity_map_master)。
 *   maps        : 存 struct affinity_map 的主链表。
 *   update_hook : 当“已存在映射的位下标被修改”时的回调；形参为
 *                 (名字, 旧位下标, 新位下标)，可为 NULL 表示未注册。
 *   QOBJ_FIELDS : QOBJ 基础设施字段。 */
struct affinity_maps {
	struct list *maps;

	void (*update_hook)(const char *affmap_name, uint16_t old_pos,
			    uint16_t new_pos);

	QOBJ_FIELDS;
};
/* 【宏】向 QOBJ 系统注册 affinity_maps 类型。 */
DECLARE_QOBJ_TYPE(affinity_maps);

/* 【全局只读对象】两个 YANG 模块描述符：
 *   frr_affinity_map_info     : 北向(数据 / 配置)回调集合，在 affinitymap_northbound.c 定义。
 *   frr_affinity_map_cli_info : CLI 回显回调集合，在 affinitymap_cli.c 定义。
 * 用法：由 libfrr 在启动时按名字注册，供 northbound 框架回调。 */
extern const struct frr_yang_module_info frr_affinity_map_info;
extern const struct frr_yang_module_info frr_affinity_map_cli_info;

/* 【接口：增删查】按名字操作亲和映射。
 *   affinity_map_set   : 名字已存在则改其 bit_position，不存在则新建(见 .c)。
 *   affinity_map_unset : 删除该名字的映射(不存在则静默返回)。
 *   affinity_map_get   : 按名字查映射。
 * 返回值：affinity_map_get 返回映射指针(内部持有，勿释放)、查不到返回 NULL；
 *         set / unset 无返回值。 */
void affinity_map_set(const char *name, int pos);
void affinity_map_unset(const char *name);
struct affinity_map *affinity_map_get(const char *name);

/* 【接口：触发回调】请求把 affmap_name 的位下标变更通知给已注册的 update_hook。
 * 注意：若该名字尚未存在(纯新建)，本函数直接返回、不触发回调。 */
void affinity_map_update_hook(const char *affmap_name, uint16_t new_pos);

/* 【接口：注册回调】设置全局 update_hook，用于订阅“位下标变更”事件。
 * 传入 func 可为 NULL 以注销，回调签名见 struct affinity_maps.update_hook。
 * 【易踩的坑】全局只有一个 hook 槽，后注册者覆盖前者。 */
void affinity_map_set_update_hook(void (*func)(const char *affmap_name,
					       uint16_t old_pos,
					       uint16_t new_pos));

/* 【接口：生命周期】
 *   affinity_map_init      : 进程启动时初始化本模块(挂载 CLI 命令)。
 *   affinity_map_terminate : 进程退出 / 重配时清理，释放所有映射。 */
void affinity_map_init(void);
void affinity_map_terminate(void);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_AFFINITYMAP_H */
