// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2021  David Lamparter, for NetDEF, Inc.
 */

/*
 * 文件导读（zlog_5424.h）
 *
 * 本头文件声明 FRR 新一代 syslog 日志目标（zlog 5424 backend）的公共接口。
 * 具体实现位于 zlog_5424.c：把一条已经格式化好的日志消息，按 RFC5424（或
 * RFC3164、本地格式、journald 等）封装后，投递到一个"目标"（destination）。
 *
 * 关键概念：
 *  - enum zlog_5424_dst：目标类型，可为空(NONE)、文件描述符(FD)、文件(FILE)、
 *    命名管道(FIFO)、Unix 域套接字(UNIX)。
 *  - enum zlog_5424_format：输出格式，决定封装成哪种 syslog 风格。
 *  - struct zlog_cfg_5424：一份"目标配置"，既含用户可配项（facility、
 *    最低优先级、是否输出版本/位置/uid/错误分类/参数等关键字、时间戳标志、
 *    格式、目标类型、文件名/权限/属主），也含运行期内部字段（当前后端指针、
 *    重连事件、退避计数、套接字地址等，这些不要手工设置）。
 *  - struct zlog_cfg_5424_user：面向用户/CLI 的包装，带一个 name、绑定的
 *    环境变量，以及在 show config 时回写的 uid/gid 字符串等。
 *
 * 使用流程：zlog_5424_init 初始化 -> zlog_5424_apply_meta 应用元数据 ->
 * zlog_5424_apply_dst 打开/切换目标（会顺带做一次轮转并再次 apply_meta）->
 * 运行期可用 zlog_5424_rotate（响应 SIGHUP 轮转）-> zlog_5424_state 查询状态
 * -> zlog_5424_fini 收尾（keepopen=true 表示关机时只摘除配置、日志继续输出）。
 *
 * 注意：这些配置结构不使用 malloc/free，允许作为静态全局对象使用。
 */
#ifndef _FRR_ZLOG_5424_H
#define _FRR_ZLOG_5424_H

#include <sys/stat.h>

#include "lib/typesafe.h"
#include "zlog.h"
#include "zlog_targets.h"
#include "qobj.h"

struct event;
struct event_loop;

/*
 * 日志投递目标的类型。NONE=0 可用于临时禁用一个目标而不删除其配置；
 * 其余依次为文件描述符、文件、命名管道(FIFO)、Unix 域套接字。
 * ZLOG_5424_DST_LAST 是编译期可用的"最后一个枚举值"，便于做范围判断。
 */
enum zlog_5424_dst {
	/* can be used to disable a target temporarily */
	ZLOG_5424_DST_NONE = 0,

	ZLOG_5424_DST_FD,
	ZLOG_5424_DST_FILE,
	ZLOG_5424_DST_FIFO,
	ZLOG_5424_DST_UNIX,

#define ZLOG_5424_DST_LAST ZLOG_5424_DST_UNIX
};

/*
 * 日志输出格式。5424=RFC5424（可带结构化数据）；3164=RFC3164（较老的 BSD
 * syslog 格式，无结构化数据）；LOCAL=FRR 自己的本地格式；JOURNALD=systemd
 * 日志（journald）格式。ZLOG_FMT_LAST 便于遍历 / 范围判断。
 */
enum zlog_5424_format {
	ZLOG_FMT_5424 = 0,
	ZLOG_FMT_3164,
	ZLOG_FMT_LOCAL,
	ZLOG_FMT_JOURNALD,

#define ZLOG_FMT_LAST ZLOG_FMT_JOURNALD
};

/* actual RCU'd logging backend */
/* 实际的日志后端对象，受 RCU 保护；本头文件里只做前向声明 */
struct zlt_5424;

/*
 * 一份 5424 目标的完整配置（既承载用户配置，也承载运行期内部状态）。
 *  - active：当前生效的 RCU 后端指针，由 5424 代码维护，勿手工设置。
 *  - cfg_mtx：保护配置读写的互斥锁。
 *  - facility/prio_min：syslog facility 与最低记录优先级。
 *  - kw_version/kw_location/kw_uid/kw_ec/kw_args：是否输出版本、位置
 *    (源文件与行号)、唯一标识(uid/xref)、错误分类(EC)、格式化参数等关键字。
 *  - ts_flags：时间戳输出开关（精度 / 是否带时区等）。
 *  - fmt：输出格式（见 enum zlog_5424_format）。
 *  - dst：当前目标类型（见 enum zlog_5424_dst）。
 *  - fd：预打开的文件描述符（注意：并非真正用于记录日志的那个 fd）。
 *  - file_nocreate：目标文件不存在时是否禁止创建。
 *  - filename/file_mode/file_uid/file_gid：目标文件路径、权限、属主属组
 *    （uid/gid 为 -1 表示不修改）。
 *  - 其余字段（master、t_reconnect、reconn_backoff、reconn_backoff_cur、
 *    reconn_backoff_max、sock_type、sa、sa_len）由 5424 代码内部使用与更新，
 *    属于套接字重连状态，不是配置项，勿设置。
 */
struct zlog_cfg_5424 {
	struct zlt_5424 *active;

	pthread_mutex_t cfg_mtx;

	/* general settings for all dsts */
	int facility;
	int prio_min;
	bool kw_version;
	bool kw_location;
	bool kw_uid;
	bool kw_ec;
	bool kw_args;

	uint32_t ts_flags;

	enum zlog_5424_format fmt;

	/* destination specifics */
	enum zlog_5424_dst dst;

	/* pre-opened FD.  not the actual fd we log to */
	int fd;

	/* file, fifo, unix */
	bool file_nocreate;

	const char *filename;
	mode_t file_mode;
	/* -1 = no change */
	uid_t file_uid;
	gid_t file_gid;

	/* remaining fields are internally used & updated by the 5424
	 * code - *not* config.  don't set these.
	 */

	/* sockets only - read handler to reconnect on errors */
	struct event_loop *master;
	struct event *t_reconnect;
	unsigned int reconn_backoff, reconn_backoff_cur, reconn_backoff_max;
	int sock_type;
	struct sockaddr_storage sa;
	socklen_t sa_len;
};

/* these don't do malloc/free to allow using a static global */
/* 初始化一份 zlog_cfg_5424 配置：设置默认值、清零内部状态。
 * 不做 malloc/free，便于把配置对象作为静态全局变量使用。 */
extern void zlog_5424_init(struct zlog_cfg_5424 *zcf);

/* keepopen = true => for shutdown, just zap the config, keep logging */
/* 结束并摘除一份配置。keepopen=true 用于关机场景：只清空配置、日志仍继续
 * 输出到原目标（避免关机瞬间丢日志）。 */
extern void zlog_5424_fini(struct zlog_cfg_5424 *zcf, bool keepopen);

/* apply metadata/config changes */
/* 应用元数据 / 配置变更（例如 facility、关键字开关、时间戳标志、格式等）。
 * 返回 true 表示应用成功。 */
extern bool zlog_5424_apply_meta(struct zlog_cfg_5424 *zcf);

/* apply changes requiring (re-)opening the destination
 *
 * also does log cycling/rotate & applies _meta at the same time
 */
/* 应用需要（重新）打开目标的变更：会一并执行日志轮转/切割，并在最后再次
 * 调用 apply_meta。返回 true 表示成功。 */
extern bool zlog_5424_apply_dst(struct zlog_cfg_5424 *zcf);

/* SIGHUP log rotation */
/* 响应 SIGHUP 的日志轮转：重新打开文件 / 重连套接字等。返回 true 表示成功。 */
extern bool zlog_5424_rotate(struct zlog_cfg_5424 *zcf);

/* 查询运行状态。通过输出参数回填：累计丢失的消息数(lost_msgs)、最近一次
 * 错误的 errno(last_errno)、该 errno 是否为"过期的历史值"(stale_errno)、
 * 以及错误发生的时间戳(err_ts)。输出参数由调用方提供。 */
extern void zlog_5424_state(struct zlog_cfg_5424 *zcf, size_t *lost_msgs,
			    int *last_errno, bool *stale_errno,
			    struct timeval *err_ts);

/* this is the dynamically allocated "variant" */
/* 声明一个按唯一键组织的红黑树类型 targets，用于按名字索引 5424 配置用户对象 */
PREDECL_RBTREE_UNIQ(targets);

/*
 * 面向用户 / CLI 的 5424 配置包装对象（动态分配）。
 *  - targets_item：把自己挂进 targets 红黑树的节点。
 *  - name：目标名（CLI 里用它来引用）。
 *  - cfg：内嵌的实际配置结构。
 *  - envvar：绑定的环境变量名（可据环境变量切换目标）。
 *  - filename：非 const 副本，始终与 cfg.filename 相同。
 *  - file_user/file_group：uid/gid 的字符串形式，用于 show config 回写。
 *  - reconf_dst/reconf_meta：标记"目标 / 元数据需要重新应用"。
 *  - unix_special：Unix 域套接字的特殊处理标志。
 *  - QOBJ_FIELDS：QObject 元数据，用于多进程共享 / 管理。
 */
struct zlog_cfg_5424_user {
	struct targets_item targets_item;
	char *name;

	struct zlog_cfg_5424 cfg;

	char *envvar;

	/* non-const, always same as cfg.filename */
	char *filename;

	/* uid/gid strings to write back out in show config */
	char *file_user;
	char *file_group;

	bool reconf_dst;
	bool reconf_meta;

	int unix_special;

	QOBJ_FIELDS;
};

/* 为 zlog_cfg_5424_user 注册 QObject 类型（配合结构体里的 QOBJ_FIELDS 使用） */
DECLARE_QOBJ_TYPE(zlog_cfg_5424_user);

/* 注册 5424 日志相关的 VTY 配置命令（命令定义在 zlog_5424_cli.c） */
extern void log_5424_cmd_init(void);

#endif /* _FRR_ZLOG_5424_H */
