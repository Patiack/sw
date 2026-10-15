// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2019-22  David Lamparter, for NetDEF, Inc.
 */

/*
 * 文件导读（zlog_live.h）
 *
 * 本头文件声明"实时日志流"目标（zlog live）。相对于把日志写进文件或
 * syslog，zlog_live 会把每条日志打包成"二进制头 + 原文"的形式，推送给
 * 当前已连接的客户端（典型场景：vtysh 的 `log immediate`，让用户在终端里
 * 实时看到守护进程的日志）。
 *
 * 关键概念：
 *  - struct zlog_live_hdr：每条消息前部的头部，由固定字段 + 可变长度数组
 *    组成，描述时间戳、进程/线程 id、丢失计数、优先级、文本长度、唯一标识、
 *    错误分类、以及 printf 各格式化参数位置（供客户端按原文重放格式化）。
 *    头部之后紧跟消息正文，未来还可在正文之后附加键值元数据。
 *  - struct zlog_live_cfg：每进程持有一份，只含一个 target 指针；为空(NULL)
 *    即表示未启用实时日志流。
 *
 * 接口：zlog_live_open 创建监听并开启；zlog_live_open_fd 用一个已存在的 fd
 * 开启；zlog_live_close 关闭并释放；zlog_live_disown 放弃所有权（不关闭）。
 * zlog_live_is_null 用于快速判断配置是否为空。
 */
#ifndef _FRR_ZLOG_LIVE_H
#define _FRR_ZLOG_LIVE_H

#include "printfrr.h"

/*
 * 实时日志流每条消息的头部。除固定字段外，尾部 argpos[] 是可变长度数组
 * （记录 printf 各格式化参数在原字符串中的位置），因此整个头部的长度由
 * hdrlen 指明（hdrlen 即正文起始偏移）。消费者可跳过前 texthdrlen 字节的
 * 前缀文本，直接使用 uid / ec 等原始字段。
 */
struct zlog_live_hdr {
	/* timestamp (CLOCK_REALTIME) */
	uint64_t ts_sec;
	uint32_t ts_nsec;

	/* length of zlog_live_hdr, including variable length bits and
	 * possible future extensions - aka start of text
	 */
	uint32_t hdrlen;

	/* process & thread ID, meaning depends on OS */
	int64_t pid;
	int64_t tid;

	/* number of lost messages due to best-effort non-blocking mode */
	uint32_t lost_msgs;
	/* syslog priority value */
	uint32_t prio;
	/* flags: currently unused */
	uint32_t flags;
	/* length of message text - extra data (e.g. future key/value metadata)
	 * may follow after it
	 */
	uint32_t textlen;
	/* length of "[XXXXX-XXXXX][EC 0] " header; consumer may want to skip
	 * over it if using the raw values below.  Note that this text may be
	 * absent depending on "log error-category" and "log unique-id"
	 * settings
	 */
	uint32_t texthdrlen;

	/* xref unique identifier, "XXXXX-XXXXX\0" = 12 bytes */
	char uid[12];
	/* EC value */
	uint32_t ec;

	/* timestamp sub-second precision (digits, 0-9) */
	uint8_t ts_subsec;
	uint8_t _pad[3];

	/* recorded printf formatting argument positions (variable length) */
	uint32_t n_argpos;
	struct fmt_outpos argpos[0];
};

/* 实时日志流目标对象，仅前向声明；实现在 zlog_live.c */
struct zlt_live;

/*
 * 每进程一份的实时日志流配置，极其简单：只持有一个目标指针。
 * target == NULL 表示未启用（见 zlog_live_is_null）。
 */
struct zlog_live_cfg {
	struct zlt_live *target;

	/* nothing else here */
};

/* 打开实时日志流目标：创建 Unix 套接字监听并开始接收客户端连接。
 * prio_min 为最低记录优先级；other_fd 若非 NULL，会回填一个额外的 fd
 * （例如供 event loop 监听）。 */
extern void zlog_live_open(struct zlog_live_cfg *cfg, int prio_min,
			   int *other_fd);
/* 用一个已存在的 fd 打开实时日志流目标（调用方已自行创建好套接字等）。 */
extern void zlog_live_open_fd(struct zlog_live_cfg *cfg, int prio_min, int fd);

/* 判断实时日志流配置是否为空（未启用）：即 target 指针是否为 NULL。 */
static inline bool zlog_live_is_null(struct zlog_live_cfg *cfg)
{
	return cfg->target == NULL;
}

/* 关闭实时日志流目标并释放其资源。 */
extern void zlog_live_close(struct zlog_live_cfg *cfg);
/* 放弃对目标的所有权但不关闭它（例如把控制权移交给其它组件 / 进程）。 */
extern void zlog_live_disown(struct zlog_live_cfg *cfg);

#endif /* _FRR_ZLOG_5424_H */
