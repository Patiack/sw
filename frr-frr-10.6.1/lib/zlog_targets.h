// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2015-19  David Lamparter, for NetDEF, Inc.
 */

/*
 * 文件导读：zlog_targets.h —— 内置日志目标的配置结构声明
 *
 * 本头文件声明 zlog 核心所用“内置目标”的配置对象与操作接口。目标（target）是
 * 日志消息的最终落点，本文件涉及的三种内置目标为：
 *   - 文件目标：可同时激活多个，由 struct zlog_cfg_file 配置，字段含文件路径、fd、
 *     优先级阈值 prio_min、时间戳亚秒位 ts_subsec，以及是否记录优先级 / 严重级。
 *   - 控制台目标：实际通过 zlog_fd 写入某个 fd（如 stderr），复用文件目标的渲染逻辑。
 *   - syslog 目标：全局唯一，用 facility 与 prio_min 控制，禁用时把 prio_min 设为 ZLOG_DISABLED。
 *
 * struct zlog_cfg_file 通过函数族修改：zlog_file_init / fini 做生命周期管理，
 * zlog_file_set_other 应用 prio_min 等“其它”配置，zlog_file_set_filename / set_fd
 * 切换输出位置，zlog_file_rotate 做轮转。配置改动由 cfg_mtx 加锁保护。
 *
 * 阅读建议：结合 zlog_targets.c 看配置对象如何被实例化成 struct zlog_target 挂到目标链上。
 */
#ifndef _FRR_ZLOG_TARGETS_H
#define _FRR_ZLOG_TARGETS_H

#include <pthread.h>

#include "zlog.h"

#ifdef __cplusplus
extern "C" {
#endif

/* multiple file log targets can be active */

struct zlt_fd;

/*
 * struct zlog_cfg_file - 文件类日志目标的配置与运行时状态。
 * active 指向当前生效的 zlt_fd 目标实例；cfg_mtx 保护配置项并发修改。
 * prio_min 为最低优先级阈值；ts_subsec 为时间戳亚秒位数；
 * record_priority / record_severity 控制是否在每条记录里附带优先级 / 严重级。
 * filename 与 fd 是输出位置，请通过 zlog_file_set_filename / set_fd 修改；
 * zlog_wrap 允许调用方包裹底层写函数（例如额外加工），可为 NULL。
 */
struct zlog_cfg_file {
	struct zlt_fd *active;

	pthread_mutex_t cfg_mtx;

	/* call zlog_file_set_other() to apply these */
	int prio_min;
	char ts_subsec;
	bool record_priority;
	bool record_severity;

	/* call zlog_file_set_filename/fd() to change this */
	char *filename;
	int fd;

	void (*zlog_wrap)(struct zlog_target *zt, struct zlog_msg *msgs[],
			  size_t nmsgs);
};

/*
 * zlog_file_init - 初始化文件目标配置对象 zcf（清零并设置默认值）。
 * zlog_file_fini - 反初始化，关闭 fd 并释放相关资源。
 */
extern void zlog_file_init(struct zlog_cfg_file *zcf);
extern void zlog_file_fini(struct zlog_cfg_file *zcf);

/*
 * zlog_file_set_other - 把 zcf 中 prio_min / ts_subsec / record_* 等“其它”字段
 *   应用到当前活跃的文件目标上（目标实例与配置分开保存，需显式同步）。
 * zlog_file_set_filename - 让 zcf 输出到名为 name 的文件；成功返回 true。
 * zlog_file_set_fd - 让 zcf 输出到已打开的 fd；成功返回 true。
 * zlog_file_rotate - 触发该文件目标轮转；成功返回 true。
 */
extern void zlog_file_set_other(struct zlog_cfg_file *zcf);
extern bool zlog_file_set_filename(struct zlog_cfg_file *zcf, const char *name);
extern bool zlog_file_set_fd(struct zlog_cfg_file *zcf, int fd);
extern bool zlog_file_rotate(struct zlog_cfg_file *zcf);

/*
 * zlog_fd - 通用 fd 写入函数，作为目标的 logfn 直接把多个 zlog_msg 渲染并写入某 fd。
 * 控制台目标即复用本函数（例如写入 STDERR_FILENO）；参数 nmsgs 为消息个数。
 */
extern void zlog_fd(struct zlog_target *zt, struct zlog_msg *msgs[],
		    size_t nmsgs);

/* syslog is always limited to one target */

/*
 * zlog_syslog_set_facility / zlog_syslog_get_facility - 设置 / 读取 syslog 的 facility。
 */
extern void zlog_syslog_set_facility(int facility);
extern int zlog_syslog_get_facility(void);

/* use ZLOG_DISABLED to disable */
/*
 * zlog_syslog_set_prio_min / zlog_syslog_get_prio_min - 设置 / 读取 syslog 目标的最低优先级。
 * 传入 ZLOG_DISABLED 即可在不删除目标的前提下停用 syslog 输出。
 */
extern void zlog_syslog_set_prio_min(int prio_min);
extern int zlog_syslog_get_prio_min(void);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_ZLOG_TARGETS_H */
