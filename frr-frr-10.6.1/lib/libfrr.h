// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libfrr overall management functions
 *
 * Copyright (C) 2016  David Lamparter for NetDEF, Inc.
 */

/*
 * 文件导读：libfrr.h —— 守护进程公共启动骨架的对外接口
 *
 * FRR 的每个守护进程（zebra、bgpd、ospf 等）都从同一个 main() 模板启动，模板的实现
 * 在 libfrr.c，而本头文件是它与各守护进程之间的契约。
 *
 * 这里定义了：
 *   FRR_DAEMON_INFO 宏 —— 每个守护进程用它填写自己的 struct frr_daemon_info，
 *     把可执行名、日志名、模块以及一系列启动参数固化下来。
 *   struct frr_daemon_info —— 描述一个守护进程的全部启动期配置（程序名、VTY 地址与端口、
 *     配置文件、pid 文件、信号表、privs、YANG 模块、CLI 模式等）。
 *   FRR_NO_* 等标志位 —— 关闭某些守护进程不适用的命令行选项。
 *   各守护进程默认端口表、版本号编解码宏，以及 frr_init / frr_run 等启动函数声明。
 *
 * 生命周期钩子：frr_early_init / frr_late_init / frr_config_pre / frr_config_post
 * 以及收尾的 frr_early_fini / frr_fini，让各模块在启动与退出的固定阶段插入逻辑。
 *
 * 与其它文件的关系：实现全部在 libfrr.c；模块加载在 module.h，事件循环在 frrevent.h，
 * 权限在 privs.h，信号在 sigevent.h。先读本文件再读 libfrr.c 更易建立整体框架。
 */
#ifndef _ZEBRA_FRR_H
#define _ZEBRA_FRR_H

#include "typesafe.h"
#include "sigevent.h"
#include "privs.h"
#include "frrevent.h"
#include "log.h"
#include "getopt.h"
#include "module.h"
#include "hook.h"
#include "northbound.h"

#ifdef __cplusplus
extern "C" {
#endif

/* zserv（zclient）API 的 Unix socket 路径模板：<frr_runstatedir>/zserv.api */
#define ZAPI_SOCK_NAME "%s/zserv.api", frr_runstatedir

/* 下面这些位标志用于关闭某些守护进程不适用的命令行选项。
 * 例如 FRR_NO_PRIVSEP 关掉 -u/-g，FRR_NO_TCPVTY 关掉 TCP 方式的 VTY，
 * FRR_NO_PID 关掉 pid 文件，FRR_NO_ZCLIENT 关掉 zclient 连接；
 * FRR_DETACH_LATER 让守护进程 -d 后延迟与父进程分离，直到调用 frr_detach()；
 * FRR_MANUAL_VTY_START 让 frr_run() 不自动起 VTY 监听；
 * FRR_LOAD_YANG_LIBRARY 让 libyang 实现 ietf-yang-library（通常只有 mgmtd 用）；
 * FRR_MGMTD_BACKEND 标记本进程是 mgmtd 的后端。 */
/* The following options disable specific command line options that
 * are not applicable for a particular daemon.
 */
#define FRR_NO_PRIVSEP		(1 << 0)
#define FRR_NO_TCPVTY		(1 << 1)
#define FRR_LIMITED_CLI		(1 << 2)
#define FRR_NO_SPLIT_CONFIG	(1 << 3)
#define FRR_NO_PID		(1 << 4)
#define FRR_NO_CFG_PID_DRY	(FRR_NO_PID | FRR_NO_SPLIT_CONFIG)
#define FRR_NO_ZCLIENT		(1 << 5)
/* If FRR_DETACH_LATER is used, the daemon will keep its parent running
 * until frr_detach() is called.  Normally "somedaemon -d" returns once the
 * main event loop is reached in the daemon;  use this for extra startup bits.
 *
 * Does nothing if -d isn't used.
 */
#define FRR_DETACH_LATER	(1 << 6)
/* If FRR_MANUAL_VTY_START is used, frr_run() will not automatically start
 * listening on for vty connection (either TCP or Unix socket based). The daemon
 * is responsible for calling frr_vty_serv() itself.
 */
#define FRR_MANUAL_VTY_START (1 << 7)
/* If FRR_LOAD_YANG_LIBRARY is set then libyang will be told to load and
 * implement it's internal ietf-yang-library implementation. This should
 * normally only be done from mgmtd.
 */
#define FRR_LOAD_YANG_LIBRARY (1 << 8)
/* Set FRR_MGMTD_BACKEND to indicate that this daemon is a backend
 * for mgmtd. This will affect various initializations for CLI handling
 * etc.
 */
#define FRR_MGMTD_BACKEND (1 << 9)

/* 早期日志目标的链表：在正式日志系统就绪前，命令行 -M/--log 指定的日志目标
 * 先挂到这里，稍后统一初始化。itm 是链表节点，target 为柔性数组存放目标串。 */
PREDECL_DLIST(log_args);
struct log_arg {
	struct log_args_item itm;

	char target[0];
};
DECLARE_DLIST(log_args, struct log_arg, itm);

/* 各守护进程默认端口登记表。
 * 多数是 VTY 端口（2601 起），也有别用途的：ZEBRA_TCP_PORT 是 zapi 的 TCP 端口
 * （出于安全默认禁用），OSPF_API_SYNC_PORT 是 ospfapi，FPM_DEFAULT_PORT 是 zebra FPM。
 * 见下方宏与各守护进程的一一对应关系。 */
/* Registry of daemons' port defaults. Many of these are VTY ports; some
 * daemons have default ports for other features such as ospfapi, or zebra's
 * FPM.
 */

/* default zebra TCP port for zapi; this is currently disabled for security
 * reasons.
 */
#define ZEBRA_TCP_PORT 2600

#define ZEBRA_VTY_PORT 2601
#define RIP_VTY_PORT 2602
#define RIPNG_VTY_PORT 2603
#define OSPF_VTY_PORT 2604
#define BGP_VTY_PORT 2605
#define OSPF6_VTY_PORT 2606

/* Default API server port to accept connection request from client-side.
 * This value could be overridden by "ospfapi" entry in "/etc/services".
 */
#define OSPF_API_SYNC_PORT 2607

#define ISISD_VTY_PORT 2608
#define BABEL_VTY_PORT 2609
#define NHRP_VTY_PORT 2610
#define PIMD_VTY_PORT 2611
#define LDP_VTY_PORT 2612
#define EIGRP_VTY_PORT 2613
#define SHARP_VTY_PORT 2614
#define PBR_VTY_PORT 2615
#define STATIC_VTY_PORT 2616
#define BFDD_VTY_PORT 2617
#define FABRICD_VTY_PORT 2618
#define VRRP_VTY_PORT 2619

/* default port for FPM connections */
#define FPM_DEFAULT_PORT 2620

#define PATH_VTY_PORT 2621
#define PIM6D_VTY_PORT 2622
#define MGMTD_VTY_PORT 2623
/* Registry of daemons' port defaults */

/* CLI 会话模式：CLASSIC 为传统的即时生效；TRANSACTIONAL 为配置事务模式
 * （先改候选配置、提交后生效），用于支持基于 YANG 的两阶段配置。 */
enum frr_cli_mode {
	FRR_CLI_CLASSIC = 0,
	FRR_CLI_TRANSACTIONAL,
};

/* 描述一个守护进程的全部启动期信息，由 FRR_DAEMON_INFO 宏填写。
 * 主要字段：flags 为 FRR_NO_* 等标志；progname/name/logname 为各种名字；
 * vty_addr/vty_port/vty_sock_path 为 VTY 监听参数；config_file/pid_file 等为路径；
 * signals/n_signals 为信号处理表；privs 为权限设置；yang_modules 为 YANG 模块集；
 * cli_mode 为 CLI 模式；此外还有 dryrun、daemon_mode、graceful_restart 等运行开关。 */
struct frr_daemon_info {
	unsigned flags;

	const char *progname;
	const char *name;
	const char *logname;
	unsigned short instance;
	struct frrmod_runtime *module;

	char *vty_addr;
	int vty_port;
	char *vty_sock_path;
	bool dryrun;
	bool daemon_mode;
	bool terminal;
	bool graceful_restart;
	int gr_cleanup_time;
	enum frr_cli_mode cli_mode;

	struct event *read_in;
	const char *config_file;
	const char *backup_config_file;
	const char *pid_file;
#ifdef HAVE_SQLITE3
	const char *db_file;
#endif
	const char *vty_path;
	const char *module_path;
	const char *script_path;
	char **state_paths;

	const char *pathspace;
	bool zpathspace;

	struct log_args_head early_logging[1];
	const char *early_loglevel;

	const char *proghelp;
	void (*printhelp)(FILE *target);
	const char *copyright;
	char startinfo[128];

	struct frr_signal_t *signals;
	size_t n_signals;

	struct zebra_privs_t *privs;

	const struct frr_yang_module_info *const *yang_modules;
	size_t n_yang_modules;

	bool log_always;

	/* Optional upper limit on the number of fds used in select/poll */
	uint32_t limit_fds;
};

/* 定义一个守护进程信息结构 execname##_di，并设置核心模块信息。
 * 参数：execname 可执行名（如 zebra），同时用作 pid / 配置文件基名；
 *       constname 源码级名字（如 ZEBRA），主要用作日志 ID；
 *       其余变参透传到结构体初始化，用于覆盖默认字段。
 * 坑：每个守护进程只能有一个；execname 还会被用作模块名。 */
/* execname is the daemon's executable (and pidfile and configfile) name,
 * i.e. "zebra" or "bgpd"
 * constname is the daemons source-level name, primarily for the logging ID,
 * i.e. "ZEBRA" or "BGP"
 *
 * note that this macro is also a latch-on point for other changes (e.g.
 * upcoming module support) that need to place some per-daemon things.  Each
 * daemon should have one of these.
 */
#define FRR_DAEMON_INFO(execname, constname, ...)                              \
	static struct frr_daemon_info execname##_di = {.name = #execname,      \
						       .logname = #constname,  \
						       .module = THIS_MODULE,  \
						       __VA_ARGS__};           \
	FRR_COREMOD_SETUP(.name = #execname,                                   \
			  .description = #execname " daemon",                  \
			  .version = FRR_VERSION, );                           \
	MACRO_REQUIRE_SEMICOLON() /* end */

/* frr_preinit：解析命令行前的初始化，处理 --version / --help 等通用选项并保存 daemon 信息。
 * frr_opt_add：注册守护进程额外的命令行选项（短选项串、longopts、帮助文本）。
 * frr_getopt：包装 getopt_long，返回下一个选项并传出 longindex。 */
extern void frr_preinit(struct frr_daemon_info *daemon, int argc, char **argv);
extern void frr_opt_add(const char *optstr, const struct option *longopts,
			const char *helpstr);
extern int frr_getopt(int argc, char *const argv[], int *longindex);

/* 打印帮助并退出进程（FRR_NORETURN 表示不会返回）；status 为退出码 */
extern FRR_NORETURN void frr_help_exit(int status);

/* frr_init：初始化 libfrr 与守护进程，返回主事件循环 master。
 * frr_get_progname：返回程序名；frr_get_cli_mode：返回 CLI 模式；
 * frr_get_fd_limit：返回 select/poll 使用的 fd 上限；
 * frr_is_startup_fd：判断 fd 是否为启动期传入的（用于平滑重启）；
 * frr_is_daemon：判断是否运行在守护进程模式。 */
extern struct event_loop *frr_init(void);
extern const char *frr_get_progname(void);
extern enum frr_cli_mode frr_get_cli_mode(void);
extern uint32_t frr_get_fd_limit(void);
extern bool frr_is_startup_fd(int fd);
extern bool frr_is_daemon(void);
/* 生命周期钩子，调用顺序严格如下：
 * frr_early_init -> frr_late_init ->（fork 发生在 late_init 与 config_pre 之间）
 * -> frr_config_pre -> frr_config_post。各模块用它们插入启动逻辑。 */
/* call order of these hooks is as ordered here */
DECLARE_HOOK(frr_early_init, (struct event_loop * tm), (tm));
DECLARE_HOOK(frr_late_init, (struct event_loop * tm), (tm));
/* fork() happens between late_init and config_pre */
DECLARE_HOOK(frr_config_pre, (struct event_loop * tm), (tm));
DECLARE_HOOK(frr_config_post, (struct event_loop * tm), (tm));

/* 完成配置加载与 fork 的切换：一般在 frr_init 后调用，触发上述配置相关钩子并 fork 到后台 */
extern void frr_config_fork(void);

/* frr_run：进入主事件循环（通常在配置加载完毕后调用），直到进程退出。
 * frr_detach：按需把守护进程与父进程分离（配合 FRR_DETACH_LATER）。
 * frr_vty_serv_start/stop：启动或停止 VTY 监听（TCP 与 Unix socket）。 */
extern void frr_run(struct event_loop *master);
extern void frr_detach(void);
extern void frr_vty_serv_start(bool check_detach);
extern void frr_vty_serv_stop(void);

/* 计算 zclient 连接地址（Unix socket 或 TCP），填入 sockaddr_storage。
 * 参数：sa 输出地址、sa_len 输出长度、path 指定路径；返回是否成功。 */
extern bool frr_zclient_addr(struct sockaddr_storage *sa, socklen_t *sa_len,
			     const char *path);

/* 守护进程状态的加载与保存：load 从磁盘读出 json 状态对象，save 把状态写回，
 * 用于重启后恢复内部状态。 */
struct json_object;
extern struct json_object *frr_daemon_state_load(void);
extern void frr_daemon_state_save(struct json_object **statep);

/* 收尾钩子：frr_early_fini 在协议守护进程自身关闭之前触发（与 frr_late_init 对应），
 * frr_fini 在守护进程完成自身清理之后触发。 */
/* these two are before the protocol daemon does its own shutdown
 * it's named this way being the counterpart to frr_late_init */
DECLARE_KOOH(frr_early_fini, (), ());
extern void frr_early_fini(void);
/* and these two are after the daemon did its own cleanup */
DECLARE_KOOH(frr_fini, (), ());
extern void frr_fini(void);

/* 默认配置文件路径与 zclient 路径缓冲（长度 512） */
extern char config_default[512];
extern char frr_zclientpath[512];

/* 各标准目录（构建时由 config_paths.h 决定）：
 * frr_sysconfdir 配置目录（一般不随 -N 加命名空间后缀，末尾带斜杠）；
 * frr_runstatedir 易失运行目录（含 -N 命名空间），放 pid 与 socket；
 * frr_libstatedir 持久状态目录（含 -N 命名空间）；
 * frr_moduledir 模块目录；frr_scriptdir Lua 脚本目录。 */
/* refer to lib/config_paths.h (generated during ./configure) for build config
 * values of the following:
 */

/* sysconfdir is generally /etc/frr/, some BSDs may use /usr/local/etc/frr/.
 * Will NOT include "pathspace" (namespace) suffix from -N. (libfrr.c handles
 * pathspace'ing config files.)  Has a slash at the end for "historical"
 * reasons.
 */
extern const char frr_sysconfdir[];

/* runstatedir is *ephemeral* across reboots.  It may either be a ramdisk,
 * or be wiped during boot.  Use only for pid files, sockets, and the like,
 * not state.  Commonly /run/frr or /var/run/frr.
 * Will include "pathspace" (namespace) suffix from -N.
 */
extern char frr_runstatedir[256];

/* libstatedir is *persistent*.  It's the place to put state like sequence
 * numbers or databases.  Commonly /var/lib/frr.
 * Will include "pathspace" (namespace) suffix from -N.
 */
extern char frr_libstatedir[256];

/* moduledir is something along the lines of /usr/lib/frr/modules or
 * /usr/lib/x86_64-linux-gnu/frr/modules.  It is not guaranteed to be a
 * subdirectory of the directory that the daemon binaries reside in.  (e.g.
 * the "x86_64-linux-gnu" component will be absent from daemon paths.)
 */
extern const char frr_moduledir[];

/* scriptdir is for Lua scripts, generally ${frr_sysconfdir}/scripts */
extern const char frr_scriptdir[];

/* frr_protoname / frr_protonameinst：协议名与带实例的协议名；
 * frr_is_after_fork：在“本该 fork 的位置”始终置位，即使实际未 fork。 */
extern char frr_protoname[];
extern char frr_protonameinst[];
/* always set in the spot where we *would* fork even if we don't do so */
extern bool frr_is_after_fork;

/* 是否在退出时打印内存统计（受命令行与调试配置控制） */
extern bool debug_memstats_at_exit;

/* 版本号编解码：MAKE_FRRVERSION 把主/次/修订号打包进一个 32 位整数
 * （主 8 位、次 16 位、修订 8 位）；MAJOR/MINOR/SUB_FRRVERSION 从中取出各段。 */
/*
 * Version numbering: MAJOR (8) | MINOR (16) | SUB (8)
 */
#define MAKE_FRRVERSION(maj, min, sub)                                         \
	((((maj) & 0xff) << 24) | (((min) & 0xffff) << 8) | ((sub) & 0xff))
#define MAJOR_FRRVERSION(v) (((v) >> 24) & 0xff)
#define MINOR_FRRVERSION(v) (((v) >> 8) & 0xffff)
#define SUB_FRRVERSION(v)  ((v) & 0xff)

/* 把打包后的版本号格式化成可读字符串写入 buf（长度 buflen），返回 buf */
const char *frr_vers2str(uint32_t version, char *buf, int buflen);

/* 退出流程：先刷新待发送的缓冲，再按 status 退出 */
void frr_exit_with_buffer_flush(int status);

/* 配置空闲内存的归还速率（单位 MB/秒），让长期运行的进程把不用的内存还给系统 */
/* Config for free-memory release: rate in MB/sec */
void frr_mem_release_config(uint32_t rate);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_FRR_H */
