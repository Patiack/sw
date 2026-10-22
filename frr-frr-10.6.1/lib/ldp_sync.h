// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Defines and structures common to LDP-Sync for OSPFv2 and OSPFv3 and ISIS
 * Copyright (C) 2020 Volta Networks, Inc.
 */

/*
 * ============================================================================
 * 文件导读：lib/ldp_sync.h —— IGP 与 LDP 同步（LDP-IGP Sync）公共定义
 * ============================================================================
 * LDP-IGP Sync 的目的：在 IGP 邻居已建立但 LDP 会话尚未就绪时，先把该链路的
 * IGP 度量抬高（或抑制通告），等 LDP 完成标签分发后再恢复，避免流量在标签
 * 未就绪时被黑洞。本文件给出 OSPFv2 / OSPFv3 / IS-IS 共用的标志、状态、结构
 * 与少量接口声明。
 *
 * 关键概念：
 *   flags（LDP_SYNC_FLAG_...）   是否启用、是否配置了 Holddown、是否接口级
 *                                配置、是否已改动 ISIS 接口度量；
 *   enabled（LDP_IGP_SYNC_...）  默认 与 已启用；
 *   state（LDP_IGP_SYNC_STATE_...） 不需要同步 / 需要但未就绪 / 需要且已就绪。
 *
 * 核心结构：
 *   struct ldp_sync_info      每个接口 / 全局的同步状态与控制；
 *   struct ldp_sync_info_cmd  命令解析用的轻量视图；
 *   struct ldp_igp_sync_...   ZAPI 通告消息体（向 ldpd 通知同步开始 / 请求）。
 *
 * 与其他模块的关系：被 ospfd / ospf6d / isisd 引用；部分结构同时是 ZAPI 报文
 * 体，放在 lib 中以保证各进程定义一致。
 *
 * 建议阅读顺序：
 *   1) 标志与状态宏；
 *   2) struct ldp_sync_info；
 *   3) 接口函数声明；
 *   4) ZAPI 消息结构体。
 */
#ifndef _LIBLDPSYNC_H
#define _LIBLDPSYNC_H

#ifdef __cplusplus
extern "C" {
#endif

/* LDP-IGP Sync values */
/*
 * LDP_SYNC_FLAG_*：ldp_sync_info.flags 的位定义，描述启用范围与附加状态。
 */
#define LDP_SYNC_FLAG_ENABLE     (1 << 0) /* LDP-SYNC enabled */
#define LDP_SYNC_FLAG_HOLDDOWN   (1 << 1) /* Holddown timer enabled */
#define LDP_SYNC_FLAG_IF_CONFIG  (1 << 2) /* LDP-SYNC enabled on interface */
#define LDP_SYNC_FLAG_SET_METRIC (1 << 3) /* Metric has been set on ISIS intf */

/*
 * LDP_IGP_SYNC_*：ldp_sync_info.enabled 的取值，表示是否启用同步。
 */
#define LDP_IGP_SYNC_DEFAULT        0
#define LDP_IGP_SYNC_ENABLED        1

/*
 * LDP_IGP_SYNC_STATE_*：ldp_sync_info.state 的取值，描述同步运行状态。
 *   NOT_REQUIRED        无需同步（LDP 已就绪或未启用）；
 *   REQUIRED_NOT_UP     需要同步但尚未就绪（期间抑制 / 抬高度量）；
 *   REQUIRED_UP         需要同步且已就绪。
 */
#define LDP_IGP_SYNC_STATE_NOT_REQUIRED     0
#define LDP_IGP_SYNC_STATE_REQUIRED_NOT_UP  1
#define LDP_IGP_SYNC_STATE_REQUIRED_UP      2

/*
 * LDP_IGP_SYNC_HOLDDOWN_DEFAULT：Holddown 定时器缺省值（0 表示未启用）。
 */
#define LDP_IGP_SYNC_HOLDDOWN_DEFAULT 0

/* LDP-IGP Sync structures */
/*
 * struct ldp_sync_info_cmd：命令解析 / 传参时用的轻量结构。
 * flags 为 LDP_SYNC_FLAG_* 组合；holddown 为定时器值（毫秒）。
 */
struct ldp_sync_info_cmd {
	uint16_t flags;
	uint16_t holddown;       /* timer value */
};

/*
 * struct ldp_sync_info：LDP-IGP 同步的运行时状态（接口级或全局）。
 * flags 指示配置范围与附加位；enabled 是否启用；state 运行状态；
 * holddown 定时器时长；t_holddown 定时器句柄；metric[2] 保存 ISIS 接口原度量
 * （两个度量级别），用于同步期间抬高、就绪后恢复。
 */
struct ldp_sync_info {
	uint16_t flags;          /* indicate if set on interface or globally */
	uint8_t enabled;         /* enabled */
	uint8_t state;           /* running state */
	uint16_t holddown;       /* timer value */
	struct event *t_holddown; /* holddown timer*/
	uint32_t metric[2];      /* isis interface metric */
};

/* Prototypes. */
/*
 * ldp_sync_info_create：分配并初始化一个 ldp_sync_info（默认未启用、状态
 * NOT_REQUIRED、无 holddown 定时器）。返回新对象。
 */
extern struct ldp_sync_info *ldp_sync_info_create(void);
/*
 * ldp_sync_if_is_enabled：判断该接口是否处于“已启用同步且尚未就绪”的状态。
 * 返回 true 表示正在同步抑制中（需要抬高度量 / 抑制通告）。
 */
extern bool ldp_sync_if_is_enabled(struct ldp_sync_info *ldp_sync_info);
/*
 * ldp_sync_if_down：停止该接口的 LDP-SYNC。
 * 参数：ldp_sync_info 同步信息（可为 NULL）。
 * 返回 true 表示确实执行了停止（原本已启用）；否则 false。
 * 副作用：取消 holddown 定时器；把状态从 REQUIRED_UP 回落为 REQUIRED_NOT_UP。
 */
extern bool ldp_sync_if_down(struct ldp_sync_info *ldp_sync_info);
/*
 * ldp_sync_info_free：释放 ldp_sync_info 并把调用者指针置空。
 * 参数：ldp_sync_info 指向指针的指针（可指向 NULL）。
 */
extern void ldp_sync_info_free(struct ldp_sync_info **ldp_sync_info);

/*
 * struct ldp_igp_sync_announce：ZAPI 消息体，IGP 通告自身协议号，用于与 ldpd
 * 协商 LDP-IGP 同步。proto 为路由协议标识。
 */
struct ldp_igp_sync_announce {
	int proto;
};

/*
 * struct ldp_igp_sync_if_state：ZAPI 消息体，报告某接口的同步状态。
 * ifindex 接口索引；sync_start 为 true 表示同步开始（进行中）。
 */
struct ldp_igp_sync_if_state {
	ifindex_t ifindex;
	bool sync_start;
};

/*
 * struct ldp_igp_sync_if_state_req：ZAPI 消息体，向某协议请求指定接口的同步
 * 状态。proto 协议号；ifindex 接口索引；name 接口名（IFNAMSIZ）。
 */
struct ldp_igp_sync_if_state_req {
	int proto;
	ifindex_t ifindex;
	char name[IFNAMSIZ];
};

#ifdef __cplusplus
}
#endif

#endif /* _LIBLDPSYNC_H */
