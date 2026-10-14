// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libfrr overall management functions
 *
 * Copyright (C) 2016  David Lamparter for NetDEF, Inc.
 */

/*
 * 文件导读：libfrr.c —— 守护进程公共启动骨架
 *
 * 本文件是 FRR 所有守护进程共享的 main() 模板与运行期基础设施，被每个 daemon 以
 * FRR_DAEMON_INFO 加少量 hook 的方式复用。它负责：命令行解析、配置文件定位、
 * 日志与内存初始化、权限与 pid 文件处理、信号安装、zclient 连接、VTY 服务，
 * 以及主事件循环的进出。
 *
 * 结构脉络：
 *   1. 全局目录变量与各种默认路径（config_default、pidfile_default 等）。
 *   2. 命令行选项体系：struct optspec 描述一组短选项/长选项/帮助文本，
 *      opt_extend 逐组累积到 comb_optstr / comb_lo / comb_helpstr 供 getopt 使用。
 *      选项按功能分为 always / cfg / fullcli / pid / zclient / vty / user 若干组。
 *   3. frr_preinit / frr_init / frr_config_fork / frr_run 构成的启动流程。
 *   4. 生命周期 hook（frr_early_init 等到 frr_fini）的定义与触发时机。
 *   5. 内存统计、pid 文件、信号、VTY 与 zclient 相关辅助函数。
 *
 * 阅读建议：先看 libfrr.h 了解对外契约，再从上到下读本文件的启动主干，
 * 最后结合各 hook 的触发点理解模块如何插入启动流程。
 */
#include <zebra.h>

#include <signal.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/wait.h>

#include "libfrr.h"
#include "getopt.h"
#include "privs.h"
#include "vty.h"
#include "command.h"
#include "lib/version.h"
#include "lib_vty.h"
#include "log_vty.h"
#include "zclient.h"
#include "module.h"
#include "network.h"
#include "lib_errors.h"
#include "db.h"
#include "northbound_cli.h"
#include "northbound_db.h"
#include "debug.h"
#include "frrcu.h"
#include "frr_pthread.h"
#include "defaults.h"
#include "frrscript.h"
#include "systemd.h"
#include "json.h"

#include "lib/config_paths.h"

/* 启动/收尾生命周期钩子的定义点：HOOK 是顺序钩子（init 方向），
 * KOOH 是逆序钩子（fini 方向，与前面的 hook 镜像）。声明见 libfrr.h。 */
DEFINE_HOOK(frr_early_init, (struct event_loop * tm), (tm));
DEFINE_HOOK(frr_late_init, (struct event_loop * tm), (tm));
DEFINE_HOOK(frr_config_pre, (struct event_loop * tm), (tm));
DEFINE_HOOK(frr_config_post, (struct event_loop * tm), (tm));
DEFINE_KOOH(frr_early_fini, (), ());
DEFINE_KOOH(frr_fini, (), ());

/* 编译期确定的标准目录：系统配置目录为常量，运行/状态/模块/脚本目录
 * 可被 -N 命名空间等运行期方式修改。 */
const char frr_sysconfdir[] = SYSCONFDIR;
char frr_runstatedir[256] = FRR_RUNSTATE_PATH;
char frr_libstatedir[256] = FRR_LIBSTATE_PATH;
const char frr_moduledir[] = MODULE_PATH;
const char frr_scriptdir[] = SCRIPT_PATH;

/* 协议名及其带实例名的缓存，默认 NONE，启动时由 daemon 信息填充 */
char frr_protoname[256] = "NONE";
char frr_protonameinst[256] = "NONE";

/* 运行时计算出的各默认路径：配置文件、zclient socket、pid 文件、
 * 可选的 sqlite 数据库文件、VTY 路径，缓冲较大以容纳前缀与命名空间。 */
char config_default[512];
char frr_zclientpath[512];
static char pidfile_default[1024];
#ifdef HAVE_SQLITE3
static char dbfile_default[1024];
#endif
static char vtypath_default[512];

/* 运行期状态标志：frr_is_after_fork 在 frr_preinit 清除、守护化后重设；
 * debug_memstats_at_exit 控制退出是否打印内存统计；
 * nodetach_term / nodetach_daemon 记录 -d -t 相关模式；
 * startup_fds 保存平滑重启时继承的 fd 位图。 */
/* cleared in frr_preinit(), then re-set after daemonizing */
bool frr_is_after_fork = true;
bool debug_memstats_at_exit = false;
static bool nodetach_term, nodetach_daemon;
static uint64_t startup_fds;

/* 命令行选项聚合缓冲：把所有 optspec 组拼成一个短选项串 comb_optstr、
 * 一个长选项数组 comb_lo、一段合并帮助文本 comb_helpstr；comb_next_lo 是
 * 往 comb_lo 追加长选项的游标。 */
static char comb_optstr[256];
static struct option comb_lo[64];
static struct option *comb_next_lo = &comb_lo[0];
static char comb_helpstr[4096];

/* 前向声明：内存统计的初始化与收尾，在下面按需调用 */
static void frr_memory_init(struct event_loop *loop);
static void frr_memory_fini(void);

/* 一组命令行选项的描述：短选项串 optstr、帮助文本 helpstr、长选项数组 longopts；
 * 由 opt_extend 合并进全局选项表。 */
struct optspec {
	const char *optstr;
	const char *helpstr;
	const struct option *longopts;
};

/* 把一组选项并入全局选项表：拼接短选项串与帮助文本，逐个复制长选项。
 * 关键参数：os 待并入的 optspec。副作用：改动 comb_optstr / comb_helpstr / comb_lo。 */
static void opt_extend(const struct optspec *os)
{
	const struct option *lo;

	strlcat(comb_optstr, os->optstr, sizeof(comb_optstr));
	strlcat(comb_helpstr, os->helpstr, sizeof(comb_helpstr));
	for (lo = os->longopts; lo->name; lo++)
		memcpy(comb_next_lo++, lo, sizeof(*lo));
}


/* 长选项的伪短选项编号（从 1000 起），用于 getopt_long 中区分没有单字符短选项的
 * 长选项，如 --vty_socket、--log、--limit-fds 等。 */
#define OPTION_VTYSOCK   1000
#define OPTION_MODULEDIR 1002
#define OPTION_LOG       1003
#define OPTION_LOGLEVEL  1004
#define OPTION_TCLI      1005
#define OPTION_DB_FILE   1006
#define OPTION_LOGGING   1007
#define OPTION_LIMIT_FDS 1008
#define OPTION_SCRIPTDIR 1009

/* “始终可用”的选项组：帮助、版本、守护化、模块、profile、命名空间(-N)、
 * VRF、平滑重启、socket 路径、日志、fd 上限等所有守护进程通用选项。 */
static const struct option lo_always[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "version", no_argument, NULL, 'v' },
	{ "daemon", no_argument, NULL, 'd' },
	{ "module", no_argument, NULL, 'M' },
	{ "profile", required_argument, NULL, 'F' },
	{ "pathspace", required_argument, NULL, 'N' },
#ifdef HAVE_NETLINK
	{ "vrfwnetns", no_argument, NULL, 'w' },
#endif
	{ "vrfdefaultname", required_argument, NULL, 'o' },
	{ "graceful_restart", optional_argument, NULL, 'K' },
	{ "vty_socket", required_argument, NULL, OPTION_VTYSOCK },
	{ "moduledir", required_argument, NULL, OPTION_MODULEDIR },
	{ "scriptdir", required_argument, NULL, OPTION_SCRIPTDIR },
	{ "log", required_argument, NULL, OPTION_LOG },
	{ "log-level", required_argument, NULL, OPTION_LOGLEVEL },
	{ "command-log-always", no_argument, NULL, OPTION_LOGGING },
	{ "limit-fds", required_argument, NULL, OPTION_LIMIT_FDS },
	{ NULL }
};
static const struct optspec os_always = {
#ifdef HAVE_NETLINK
	"w"
#endif
	"hvdM:F:N:o:K::",
	"  -h, --help         Display this help and exit\n"
	"  -v, --version      Print program version\n"
	"  -d, --daemon       Runs in daemon mode\n"
	"  -M, --module       Load specified module\n"
	"  -F, --profile      Use specified configuration profile\n"
	"  -N, --pathspace    Insert prefix into config & socket paths\n"
#ifdef HAVE_NETLINK
	"  -w, --vrfwnetns    Use network namespaces for VRFs\n"
#endif
	"  -o, --vrfdefaultname     Set default VRF name.\n"
	"  -K, --graceful_restart   FRR starting in Graceful Restart mode, with optional route-cleanup timer\n"
	"      --vty_socket   Override vty socket path\n"
	"      --moduledir    Override modules directory\n"
	"      --scriptdir    Override scripts directory\n"
	"      --log          Set Logging to stdout, syslog, or file:<name>\n"
	"      --log-level    Set Logging Level to use, debug, info, warn, etc\n"
	"      --command-log-always Always log every command, cannot be turned off\n"
	"      --limit-fds    Limit number of fds supported\n",
	lo_always
};

/* 是否把日志输出到标准输出（由 --log stdout 设定） */
static bool logging_to_stdout = false; /* set when --log stdout specified */

/* 配置相关选项组：-f 指定配置文件、-C 只做配置校验(dryrun) 后退出 */
static const struct option lo_cfg[] = {
	{"config_file", required_argument, NULL, 'f'},
	{"dryrun", no_argument, NULL, 'C'},
	{NULL}};
static const struct optspec os_cfg = {
	"f:C",
	"  -f, --config_file  Set configuration file name\n"
	"  -C, --dryrun       Check configuration for validity and exit\n",
	lo_cfg};


/* 完整 CLI 相关选项组：-t 在标准输入输出上开终端会话、--tcli 使用事务型 CLI、
 * 可选 --db_file 指定 sqlite 数据库 */
static const struct option lo_fullcli[] = {
	{"terminal", no_argument, NULL, 't'},
	{"tcli", no_argument, NULL, OPTION_TCLI},
#ifdef HAVE_SQLITE3
	{"db_file", required_argument, NULL, OPTION_DB_FILE},
#endif
	{NULL}};
static const struct optspec os_fullcli = {
	"t",
	"      --tcli         Use transaction-based CLI\n"
	"  -t, --terminal     Open terminal session on stdio\n"
	"  -d -t              Daemonize after terminal session ends\n",
	lo_fullcli};


/* pid 文件选项组：-i 指定进程标识文件路径 */
static const struct option lo_pid[] = {
	{"pid_file", required_argument, NULL, 'i'},
	{NULL}};
static const struct optspec os_pid = {
	"i:",
	"  -i, --pid_file     Set process identifier file name\n",
	lo_pid};


/* zclient 选项组：-z 指定 zebra socket 路径 */
static const struct option lo_zclient[] = {
	{"socket", required_argument, NULL, 'z'},
	{NULL}};
static const struct optspec os_zclient = {
	"z:", "  -z, --socket       Set path of zebra socket\n", lo_zclient};


/* VTY 选项组：-A 绑定地址、-P 端口 */
static const struct option lo_vty[] = {
	{"vty_addr", required_argument, NULL, 'A'},
	{"vty_port", required_argument, NULL, 'P'},
	{NULL}};
static const struct optspec os_vty = {
	"A:P:",
	"  -A, --vty_addr     Set vty's bind address\n"
	"  -P, --vty_port     Set vty's port number\n",
	lo_vty};


/* 权限选项组：-u 运行用户、-g 运行组（需要 privs 支持） */
static const struct option lo_user[] = {{"user", required_argument, NULL, 'u'},
					{"group", required_argument, NULL, 'g'},
					{NULL}};
static const struct optspec os_user = {"u:g:",
				       "  -u, --user         User to run as\n"
				       "  -g, --group        Group to run as\n",
				       lo_user};

/* 把 zclient 连接地址解析进 sockaddr_storage，返回是否成功。
 * 关键参数：sa 输出地址、sa_len 输出长度、path 路径（为 NULL 时用默认 frr_zclientpath）。
 * 逻辑：若 path 以 ZAPI_TCP_PATHNAME 前缀开头则解析 TCP 形式（可带 4/6 与端口），
 *       但该分支被下面的 #if 1 强制禁用——TCP zapi 存在安全隐患，仅作开发调试用；
 *       否则按 Unix 域 socket（AF_UNIX）填充 sun_path。
 * 坑：TCP 路径即使解析成功也返回 false，调用者需注意。 */
bool frr_zclient_addr(struct sockaddr_storage *sa, socklen_t *sa_len,
		      const char *path)
{
	memset(sa, 0, sizeof(*sa));

	if (!path)
		path = frr_zclientpath;

	if (!strncmp(path, ZAPI_TCP_PATHNAME, strlen(ZAPI_TCP_PATHNAME))) {
		/* note: this functionality is disabled at bottom */
		int af;
		int port = ZEBRA_TCP_PORT;
		char *err = NULL;
		struct sockaddr_in *sin = NULL;
		struct sockaddr_in6 *sin6 = NULL;

		path += strlen(ZAPI_TCP_PATHNAME);

		switch (path[0]) {
		case '4':
			path++;
			af = AF_INET;
			break;
		case '6':
			path++;
			af = AF_INET6;
			break;
		default:
			af = AF_INET6;
			break;
		}

		switch (path[0]) {
		case '\0':
			break;
		case ':':
			path++;
			port = strtoul(path, &err, 10);
			if (*err || !*path)
				return false;
			break;
		default:
			return false;
		}

		sa->ss_family = af;
		switch (af) {
		case AF_INET:
			sin = (struct sockaddr_in *)sa;
			sin->sin_port = htons(port);
			sin->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			*sa_len = sizeof(struct sockaddr_in);
#ifdef HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
			sin->sin_len = *sa_len;
#endif
			break;
		case AF_INET6:
			sin6 = (struct sockaddr_in6 *)sa;
			sin6->sin6_port = htons(port);
			inet_pton(AF_INET6, "::1", &sin6->sin6_addr);
			*sa_len = sizeof(struct sockaddr_in6);
#ifdef SIN6_LEN
			sin6->sin6_len = *sa_len;
#endif
			break;
		}

#if 1
		/* force-disable this path, because tcp-zebra is a
		 * SECURITY ISSUE.  there are no checks at all against
		 * untrusted users on the local system connecting on TCP
		 * and injecting bogus routing data into the entire routing
		 * domain.
		 *
		 * The functionality is only left here because it may be
		 * useful during development, in order to be able to get
		 * tcpdump or wireshark watching ZAPI as TCP.  If you want
		 * to do that, flip the #if 1 above to #if 0. */
		memset(sa, 0, sizeof(*sa));
		return false;
#endif
	} else {
		/* "sun" is a #define on solaris */
		struct sockaddr_un *suna = (struct sockaddr_un *)sa;

		suna->sun_family = AF_UNIX;
		strlcpy(suna->sun_path, path, sizeof(suna->sun_path));
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
		*sa_len = suna->sun_len = SUN_LEN(suna);
#else
		*sa_len = sizeof(suna->sun_family) + strlen(suna->sun_path);
#endif /* HAVE_STRUCT_SOCKADDR_UN_SUN_LEN */
#if 0
		/* this is left here for future reference;  Linux abstract
		 * socket namespace support can be enabled by replacing
		 * above #if 0 with #ifdef GNU_LINUX.
		 *
		 * THIS IS A SECURITY ISSUE, the abstract socket namespace
		 * does not have user/group permission control on sockets.
		 * we'd need to implement SCM_CREDENTIALS support first to
		 * check that only proper users can connect to abstract
		 * sockets. (same problem as tcp-zebra, except there is a
		 * fix with SCM_CREDENTIALS.  tcp-zebra has no such fix.)
		 */
		if (suna->sun_path[0] == '@')
			suna->sun_path[0] = '\0';
#endif
	}
	return true;
}

/* 指向当前守护进程的信息结构（由 frr_preinit 传入），全文件共享的单例 */
static struct frr_daemon_info *di = NULL;

/* 启动第一步：在正式解析命令行前保存 daemon 信息并做基础初始化。
 * 关键动作：记录 di、清除 frr_is_after_fork、从 argv[0] 取程序名、按是否 profiling 设置 umask、
 *   初始化早期日志参数、按 flags 组合要启用的选项组、计算 config/pid/zclient/db 默认路径、
 *   设置协议名与 CLI 模式默认值，并扫描记录启动时已打开的 fd（startup_fds）。
 * 关键参数：daemon 守护进程信息、argc/argv 命令行。
 * 坑：必须在使用 frr_getopt 之前调用；它会改动全局默认路径缓冲。 */
void frr_preinit(struct frr_daemon_info *daemon, int argc, char **argv)
{
	di = daemon;
	frr_is_after_fork = false;

	/* basename(), opencoded. */
	char *p = strrchr(argv[0], '/');
	di->progname = p ? p + 1 : argv[0];

	if (!getenv("GCOV_PREFIX"))
		umask(0027);
	else {
		/* If we are profiling use a more generous umask */
		umask(0002);
	}

	log_args_init(daemon->early_logging);

	opt_extend(&os_always);
	if (!(di->flags & FRR_NO_SPLIT_CONFIG))
		opt_extend(&os_cfg);
	if (!(di->flags & FRR_LIMITED_CLI))
		opt_extend(&os_fullcli);
	if (!(di->flags & FRR_NO_PID))
		opt_extend(&os_pid);
	if (!(di->flags & FRR_NO_PRIVSEP))
		opt_extend(&os_user);
	if (!(di->flags & FRR_NO_ZCLIENT))
		opt_extend(&os_zclient);
	if (!(di->flags & FRR_NO_TCPVTY))
		opt_extend(&os_vty);
	if (di->flags & FRR_DETACH_LATER)
		nodetach_daemon = true;

	snprintf(config_default, sizeof(config_default), "%s/%s.conf",
		 frr_sysconfdir, di->name);
	snprintf(pidfile_default, sizeof(pidfile_default), "%s/%s.pid",
		 frr_runstatedir, di->name);
	snprintf(frr_zclientpath, sizeof(frr_zclientpath), ZAPI_SOCK_NAME);
#ifdef HAVE_SQLITE3
	snprintf(dbfile_default, sizeof(dbfile_default), "%s/%s.db",
		 frr_libstatedir, di->name);
#endif

	strlcpy(frr_protoname, di->logname, sizeof(frr_protoname));
	strlcpy(frr_protonameinst, di->logname, sizeof(frr_protonameinst));

	di->cli_mode = FRR_CLI_CLASSIC;
	di->graceful_restart = false;
	di->gr_cleanup_time = 0;

	/* we may be starting with extra FDs open for whatever purpose,
	 * e.g. logging, some module, etc.  Recording them here allows later
	 * checking whether an fd is valid for such extension purposes,
	 * without this we could end up e.g. logging to a BGP session fd.
	 */
	startup_fds = 0;
	for (int i = 0; i < 64; i++) {
		struct stat st;

		if (fstat(i, &st))
			continue;
		if (S_ISDIR(st.st_mode) || S_ISBLK(st.st_mode))
			continue;

		startup_fds |= UINT64_C(0x1) << (uint64_t)i;
	}

	/* note this doesn't do anything, it just grabs state, so doing it
	 * early in _preinit is perfect.
	 */
	systemd_init_env();
}

/* 判断 fd 是否为进程启动时就已打开的 fd（记录在 startup_fds 位图里）。
 * 用于区分“本进程自己该管的 fd”和“继承来的 fd”，避免误用（如往 BGP 会话 fd 写日志）。 */
bool frr_is_startup_fd(int fd)
{
	return !!(startup_fds & (UINT64_C(0x1) << (uint64_t)fd));
}

/* 供守护进程在解析前追加自定义选项：构造一个临时 optspec 并并入全局选项表。
 * 关键参数：optstr 短选项串、longopts 长选项数组、helpstr 帮助文本。 */
void frr_opt_add(const char *optstr, const struct option *longopts,
		 const char *helpstr)
{
	const struct optspec main_opts = {optstr, helpstr, longopts};
	opt_extend(&main_opts);
}

/* 打印用法与帮助后退出（不返回）。status 非 0 时先向 stderr 打印 Invalid options.。
 * 优先用 di->printhelp，否则用 proghelp / copyright / comb_helpstr 拼出用法信息。 */
void frr_help_exit(int status)
{
	FILE *target = status ? stderr : stdout;

	if (status != 0)
		fprintf(stderr, "Invalid options.\n\n");

	if (di->printhelp)
		di->printhelp(target);
	else
		fprintf(target, "Usage: %s [OPTION...]\n\n%s%s%s\n\n%s",
			di->progname, di->proghelp, di->copyright ? "\n\n" : "",
			di->copyright ? di->copyright : "", comb_helpstr);
	fprintf(target, "\nReport bugs to %s\n", FRR_BUG_ADDRESS);
	exit(status);
}

/* 模块选项 -M 的链表节点：把每个 -M 指定的模块名按出现顺序串起来。 */
struct option_chain {
	struct option_chain *next;
	const char *arg;
};

/* modules 链表头与尾部插入游标 modnext；errors 累计命令行解析中的错误次数。 */
static struct option_chain *modules = NULL, **modnext = &modules;
static int errors = 0;

/* getopt 的分发函数：处理每个已解析选项（由 frr_getopt 在循环中调用）。
 * 覆盖 -h/-v/-d/-M/-F/-i/-f/-N/-w/-o/-K/-C/-t/-z/-A/-P 以及 --vty_socket、
 * --moduledir、--scriptdir、--db_file 等长选项，设置 di 中对应字段。
 * 返回：0 表示继续；返回 1 表示该选项被 flags 禁用，应视为“不支持”交上层处理。
 * 坑：很多选项在对应 FRR_NO_* 标志置位时直接返回 1；重复指定某些选项会 errors++。 */
static int frr_opt(int opt)
{
	static int vty_port_set = 0;
	static int vty_addr_set = 0;
	struct option_chain *oc;
	struct log_arg *log_arg;
	size_t arg_len;
	char *err;

	switch (opt) {
	case 'h':
		frr_help_exit(0);
	case 'v':
		print_version(di->progname);
		exit(0);
		break;
	case 'd':
		di->daemon_mode = true;
		break;
	case 'M':
		oc = XMALLOC(MTYPE_TMP, sizeof(*oc));
		oc->arg = optarg;
		oc->next = NULL;
		*modnext = oc;
		modnext = &oc->next;
		break;
	case 'F':
		if (!frr_defaults_profile_valid(optarg)) {
			const char **p;
			FILE *ofd = stderr;

			if (!strcmp(optarg, "help"))
				ofd = stdout;
			else
				fprintf(stderr,
					"The \"%s\" configuration profile is not valid for this FRR version.\n",
					optarg);

			fprintf(ofd, "Available profiles are:\n");
			for (p = frr_defaults_profiles; *p; p++)
				fprintf(ofd, "%s%s\n",
					strcmp(*p, DFLT_NAME) ? "   " : " * ",
					*p);

			if (ofd == stdout)
				exit(0);
			fprintf(ofd, "\n");
			errors++;
			break;
		}
		frr_defaults_profile_set(optarg);
		break;
	case 'i':
		if (di->flags & FRR_NO_PID)
			return 1;
		di->pid_file = optarg;
		break;
	case 'f':
		if (di->flags & FRR_NO_SPLIT_CONFIG)
			return 1;
		di->config_file = optarg;
		break;
	case 'N':
		if (di->pathspace) {
			fprintf(stderr,
				"-N/--pathspace option specified more than once!\n");
			errors++;
			break;
		}
		if (di->zpathspace)
			fprintf(stderr,
				"-N option overridden by -z for zebra named socket path\n");

		if (strchr(optarg, '/') || strchr(optarg, '.')) {
			fprintf(stderr,
				"slashes or dots are not permitted in the --pathspace option.\n");
			errors++;
			break;
		}
		di->pathspace = optarg;

		snprintf(frr_runstatedir, sizeof(frr_runstatedir),
			 FRR_RUNSTATE_PATH "/%s", di->pathspace);
		snprintf(frr_libstatedir, sizeof(frr_libstatedir),
			 FRR_LIBSTATE_PATH "/%s", di->pathspace);
		snprintf(pidfile_default, sizeof(pidfile_default), "%s/%s.pid",
			 frr_runstatedir, di->name);
		if (!di->zpathspace)
			snprintf(frr_zclientpath, sizeof(frr_zclientpath),
				 ZAPI_SOCK_NAME);
		break;
#ifdef HAVE_NETLINK
	case 'w':
		vrf_configure_backend(VRF_BACKEND_NETNS);
		break;
#endif
	case 'o':
		vrf_set_default_name(optarg);
		break;
#ifdef HAVE_SQLITE3
	case OPTION_DB_FILE:
		if (di->flags & FRR_NO_PID)
			return 1;
		di->db_file = optarg;
		break;
#endif
	case 'K':
		di->graceful_restart = true;
		if (optarg)
			di->gr_cleanup_time = atoi(optarg);
		break;
	case 'C':
		if (di->flags & FRR_NO_SPLIT_CONFIG)
			return 1;
		di->dryrun = true;
		break;
	case 't':
		if (di->flags & FRR_LIMITED_CLI)
			return 1;
		di->terminal = true;
		break;
	case 'z':
		di->zpathspace = true;
		if (di->pathspace)
			fprintf(stderr,
				"-z option overrides -N option for zebra named socket path\n");
		if (di->flags & FRR_NO_ZCLIENT)
			return 1;
		strlcpy(frr_zclientpath, optarg, sizeof(frr_zclientpath));
		break;
	case 'A':
		if (di->flags & FRR_NO_TCPVTY)
			return 1;
		if (vty_addr_set) {
			fprintf(stderr,
				"-A option specified more than once!\n");
			errors++;
			break;
		}
		vty_addr_set = 1;
		di->vty_addr = optarg;
		break;
	case 'P':
		if (di->flags & FRR_NO_TCPVTY)
			return 1;
		if (vty_port_set) {
			fprintf(stderr,
				"-P option specified more than once!\n");
			errors++;
			break;
		}
		vty_port_set = 1;
		di->vty_port = strtoul(optarg, &err, 0);
		if (*err || !*optarg) {
			fprintf(stderr,
				"invalid port number \"%s\" for -P option\n",
				optarg);
			errors++;
			break;
		}
		break;
	case OPTION_VTYSOCK:
		if (di->vty_sock_path) {
			fprintf(stderr,
				"--vty_socket option specified more than once!\n");
			errors++;
			break;
		}
		di->vty_sock_path = optarg;
		break;
	case OPTION_MODULEDIR:
		if (di->module_path) {
			fprintf(stderr,
				"----moduledir option specified more than once!\n");
			errors++;
			break;
		}
		di->module_path = optarg;
		break;
	case OPTION_SCRIPTDIR:
		if (di->script_path) {
			fprintf(stderr, "--scriptdir option specified more than once!\n");
			errors++;
			break;
		}
		di->script_path = optarg;
		break;
	case OPTION_TCLI:
		di->cli_mode = FRR_CLI_TRANSACTIONAL;
		break;
	case 'u':
		if (di->flags & FRR_NO_PRIVSEP)
			return 1;
		di->privs->user = optarg;
		break;
	case 'g':
		if (di->flags & FRR_NO_PRIVSEP)
			return 1;
		di->privs->group = optarg;
		break;
	case OPTION_LOG:
		arg_len = strlen(optarg) + 1;
		log_arg = XCALLOC(MTYPE_TMP, sizeof(*log_arg) + arg_len);
		memcpy(log_arg->target, optarg, arg_len);
		log_args_add_tail(di->early_logging, log_arg);
		break;
	case OPTION_LOGLEVEL:
		di->early_loglevel = optarg;
		break;
	case OPTION_LOGGING:
		di->log_always = true;
		break;
	case OPTION_LIMIT_FDS:
		di->limit_fds = strtoul(optarg, &err, 0);
		break;
	default:
		return 1;
	}
	return 0;
}

/* 命令行解析循环：先用 comb_next_lo->name=NULL 给长选项数组收尾，再反复调
 * getopt_long 并分发给 frr_opt；遇到 frr_opt 返回非 0 或解析结束(-1) 时退出循环。
 * 返回：最后得到的选项码（-1 表示解析结束）；若结束时有解析错误则调用 frr_help_exit(1)。
 * 副作用：*longindex 传出命中的长选项下标。 */
int frr_getopt(int argc, char *const argv[], int *longindex)
{
	int opt;
	int lidx;

	comb_next_lo->name = NULL;

	do {
		opt = getopt_long(argc, argv, comb_optstr, comb_lo, &lidx);
		if (frr_opt(opt))
			break;
	} while (opt != -1);

	if (opt == -1 && errors)
		frr_help_exit(1);
	if (longindex)
		*longindex = lidx;
	return opt;
}

/* 创建目录并按 frrvty 组的需求设置权限与属主。
 * 关键参数：path 目录路径；strip 为 true 时只创建其父目录（去掉最后一段）。
 * 做法：临时 umask(0022) 后 mkdir 0755，再 chown 给 zprivs 中的普通用户/组。
 * 坑：目录已存在(EEXIST)时直接返回以保留用户自定义权限；失败只记日志不中断。 */
static void frr_mkdir(const char *path, bool strip)
{
	char buf[256];
	mode_t prev;
	int ret;
	struct zprivs_ids_t ids;

	if (strip) {
		char *slash = strrchr(path, '/');
		size_t plen;
		if (!slash)
			return;
		plen = slash - path;
		if (plen > sizeof(buf) - 1)
			return;
		memcpy(buf, path, plen);
		buf[plen] = '\0';
		path = buf;
	}

	/* o+rx (..5) is needed for the frrvty group to work properly;
	 * without it, users in the frrvty group can't access the vty sockets.
	 */
	prev = umask(0022);
	ret = mkdir(path, 0755);
	umask(prev);

	if (ret != 0) {
		/* if EEXIST, return without touching the permissions,
		 * so user-set custom permissions are left in place
		 */
		if (errno == EEXIST)
			return;

		flog_err(EC_LIB_SYSTEM_CALL, "failed to mkdir \"%s\": %s", path,
			 strerror(errno));
		return;
	}

	zprivs_get_ids(&ids);
	if (chown(path, ids.uid_normal, ids.gid_normal))
		flog_err(EC_LIB_SYSTEM_CALL, "failed to chown \"%s\": %s", path,
			 strerror(errno));
}

/* 模块加载器用的错误输出回调：cookie 是前缀字符串，把“前缀: 错误信息”打到 stderr */
static void _err_print(const void *cookie, const char *errstr)
{
	const char *prefix = (const char *)cookie;

	fprintf(stderr, "%s: %s\n", prefix, errstr);
}

/* 全局主事件循环指针，由 frr_init 创建、frr_run 使用 */
static struct event_loop *master;
/* 守护进程初始化的核心：在 frr_preinit/frr_getopt 之后调用，返回主事件循环 master。
 * 主要步骤：设置随机种子、应用默认配置(profile)、按实例与命名空间重算 config/pid/db 路径、
 *   初始化 privs 与日志（消费早期日志目标）、解析 zclient 地址、按需 mkdir 各运行目录、
 *   加载命令行 -M 指定的模块、初始化 privs 与事件循环、安装信号、触发 frr_early_init 钩子，
 *   再初始化 CLI/VTY/调试/线程/脚本/日志引用/北向接口(northbound) 与内存统计。
 * 返回：主事件循环指针。坑：多处直接 exit(1) 而非返回错误；调用顺序对外部模块有约束。 */
struct event_loop *frr_init(void)
{
	struct option_chain *oc;
	struct log_arg *log_arg;
	struct frrmod_runtime *module;
	struct zprivs_ids_t ids;
	char p_instance[16] = "", p_pathspace[256] = "";
	const char *dir;

	dir = di->module_path ? di->module_path : frr_moduledir;

	srandom(time(NULL));
	frr_defaults_apply();

	if (di->instance) {
		snprintf(frr_protonameinst, sizeof(frr_protonameinst), "%s[%u]",
			 di->logname, di->instance);
		snprintf(p_instance, sizeof(p_instance), "-%d", di->instance);
	}
	if (di->pathspace)
		snprintf(p_pathspace, sizeof(p_pathspace), "%s/",
			 di->pathspace);

	snprintf(config_default, sizeof(config_default), "%s%s%s%s.conf",
		 frr_sysconfdir, p_pathspace, di->name, p_instance);
	snprintf(pidfile_default, sizeof(pidfile_default), "%s/%s%s.pid",
		 frr_runstatedir, di->name, p_instance);
#ifdef HAVE_SQLITE3
	snprintf(dbfile_default, sizeof(dbfile_default), "%s/%s%s.db", frr_libstatedir, di->name,
		 p_instance);
#endif

	zprivs_preinit(di->privs);
	zprivs_get_ids(&ids);

	zlog_init(di->progname, di->logname, di->instance,
		  ids.uid_normal, ids.gid_normal);

	while ((log_arg = log_args_pop(di->early_logging))) {
		command_setup_early_logging(log_arg->target,
					    di->early_loglevel);
		/* this is a bit of a hack,
		   but need to notice when
		   the target is stdout */
		if (strcmp(log_arg->target, "stdout") == 0)
			logging_to_stdout = true;
		XFREE(MTYPE_TMP, log_arg);
	}

	if (!frr_zclient_addr(&zclient_addr, &zclient_addr_len,
			      frr_zclientpath)) {
		fprintf(stderr, "Invalid zserv socket path: %s\n",
			frr_zclientpath);
		exit(1);
	}

	/* don't mkdir these as root... */
	if (!(di->flags & FRR_NO_PRIVSEP)) {
		frr_mkdir(frr_libstatedir, false);
		if (!di->pid_file || !di->vty_path)
			frr_mkdir(frr_runstatedir, false);
		if (di->pid_file)
			frr_mkdir(di->pid_file, true);
		if (di->vty_path)
			frr_mkdir(di->vty_path, true);
	}

	frrmod_init(di->module);
	while (modules) {
		modules = (oc = modules)->next;
		module = frrmod_load(oc->arg, dir, _err_print, __func__);
		if (!module)
			exit(1);
		XFREE(MTYPE_TMP, oc);
	}

	zprivs_init(di->privs);

	master = event_master_create(NULL);
	signal_init(master, di->n_signals, di->signals);
	hook_call(frr_early_init, master);

#ifdef HAVE_SQLITE3
	if (!di->db_file)
		di->db_file = dbfile_default;
	db_init("%s", di->db_file);
#endif

	if (di->flags & FRR_LIMITED_CLI)
		cmd_init(-1);
	else {
		cmd_init(1);
		if (!(di->flags & FRR_MGMTD_BACKEND)) {
			host_cli_init();
			log_cli_init();
		}
	}

	vty_init(master, di->log_always);
	lib_cmd_init();
	debug_init();

	frr_pthread_init();
#ifdef HAVE_SCRIPTING
	frrscript_init(di->script_path ? di->script_path : frr_scriptdir);
#endif

	log_ref_init();
	log_ref_vty_init();
	lib_error_init();

	nb_init(master, di->yang_modules, di->n_yang_modules, true,
		(di->flags & FRR_LOAD_YANG_LIBRARY) != 0);
	if (nb_db_init() != NB_OK)
		flog_warn(EC_LIB_NB_DATABASE,
			  "%s: failed to initialize northbound database",
			  __func__);

	frr_memory_init(master);

	return master;
}

/* 返回当前进程程序名；di 未初始化时返回 NULL */
const char *frr_get_progname(void)
{
	return di ? di->progname : NULL;
}

/* 返回当前 CLI 模式；di 未初始化时默认 CLASSIC */
enum frr_cli_mode frr_get_cli_mode(void)
{
	return di ? di->cli_mode : FRR_CLI_CLASSIC;
}

/* 返回 select/poll 使用的 fd 上限（来自 --limit-fds）；未设置时为 0 */
uint32_t frr_get_fd_limit(void)
{
	return di ? di->limit_fds : 0;
}

/* 记录最近收到的信号编号，供 frr_daemon_wait 的信号循环判断 */
static int rcvd_signal = 0;

/* 信号处理函数：仅把信号编号存入 rcvd_signal。poll 被信号中断后由主循环读取处理，
 * 因此这里不做任何复杂动作（信号处理中应尽量可重入、低风险）。 */
static void rcv_signal(int signum)
{
	rcvd_signal = signum;
	/* poll() is interrupted by the signal; handled below */
}

/* 父进程在守护化后等待子进程进入主循环的“握手”等待函数（FRR_NORETURN，必然退出）。
 * 做法：屏蔽并捕获 SIGTSTP/SIGQUIT/SIGINT，用 poll/ppoll 等监听与子进程的 socketpair；
 *   收到 TSTP 时把终端挂起信号转发给子进程并等待其就绪、收到 INT/QUIT 时转发字符；
 *   子进程成功进入主循环并关闭 socket 后，父进程收到可读事件并退出(0)；
 *   若子进程启动失败则打印错误并 exit(1)。
 * 关键参数：fd 为与子进程通信的父端 socket。
 * 坑：无 ppoll/pollts 时退化为“有竞态”的 poll 加 sigprocmask 实现（仅用于老系统）。 */
static FRR_NORETURN void frr_daemon_wait(int fd)
{
	struct pollfd pfd[1];
	int ret;
	pid_t exitpid;
	int exitstat;
	sigset_t sigs, prevsigs;

	sigemptyset(&sigs);
	sigaddset(&sigs, SIGTSTP);
	sigaddset(&sigs, SIGQUIT);
	sigaddset(&sigs, SIGINT);
	sigprocmask(SIG_BLOCK, &sigs, &prevsigs);

	struct sigaction sa = {
		.sa_handler = rcv_signal, .sa_flags = SA_RESETHAND,
	};
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTSTP, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	do {
		char buf[1];
		ssize_t nrecv;

		pfd[0].fd = fd;
		pfd[0].events = POLLIN;

		rcvd_signal = 0;

#if defined(HAVE_PPOLL)
		ret = ppoll(pfd, 1, NULL, &prevsigs);
#elif defined(HAVE_POLLTS)
		ret = pollts(pfd, 1, NULL, &prevsigs);
#else
		/* racy -- only used on FreeBSD 9 */
		sigset_t tmpsigs;
		sigprocmask(SIG_SETMASK, &prevsigs, &tmpsigs);
		ret = poll(pfd, 1, -1);
		sigprocmask(SIG_SETMASK, &tmpsigs, NULL);
#endif
		if (ret < 0 && errno != EINTR && errno != EAGAIN) {
			perror("poll()");
			exit(1);
		}
		switch (rcvd_signal) {
		case SIGTSTP:
			send(fd, "S", 1, 0);
			do {
				nrecv = recv(fd, buf, sizeof(buf), 0);
			} while (nrecv == -1
				 && (errno == EINTR || errno == EAGAIN));

			raise(SIGTSTP);
			sigaction(SIGTSTP, &sa, NULL);
			send(fd, "R", 1, 0);
			break;
		case SIGINT:
			send(fd, "I", 1, 0);
			break;
		case SIGQUIT:
			send(fd, "Q", 1, 0);
			break;
		}
	} while (ret <= 0);

	exitpid = waitpid(-1, &exitstat, WNOHANG);
	if (exitpid == 0)
		/* child successfully went to main loop & closed socket */
		exit(0);

	/* child failed one way or another ... */
	if (WIFEXITED(exitstat) && WEXITSTATUS(exitstat) == 0)
		/* can happen in --terminal case if exit is fast enough */
		(void)0;
	else if (WIFEXITED(exitstat))
		fprintf(stderr, "%s failed to start, exited %d\n", di->name,
			WEXITSTATUS(exitstat));
	else if (WIFSIGNALED(exitstat))
		fprintf(stderr, "%s crashed in startup, signal %d\n", di->name,
			WTERMSIG(exitstat));
	else
		fprintf(stderr, "%s failed to start, unknown problem\n",
			di->name);
	exit(1);
}

/* 子进程侧保存的守护化控制 socket（-1 表示无）；frr_check_detach 会按需关闭它 */
static int daemon_ctl_sock = -1;

/* 把当前进程守护化：建一对 socketpair，fork 出子进程；子进程 setsid 成为会话首进程，
 * 保存控制 socket 后返回继续启动；父进程关闭子端并进入 frr_daemon_wait 等待握手。 */
static void frr_daemonize(void)
{
	int fds[2];
	pid_t pid;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds)) {
		perror("socketpair() for daemon control");
		exit(1);
	}
	set_cloexec(fds[0]);
	set_cloexec(fds[1]);

	pid = fork();
	if (pid < 0) {
		perror("fork()");
		exit(1);
	}
	if (pid == 0) {
		/* child */
		close(fds[0]);
		if (setsid() < 0) {
			perror("setsid()");
			exit(1);
		}

		daemon_ctl_sock = fds[1];
		return;
	}

	close(fds[1]);
	frr_daemon_wait(fds[0]);
}

/* 读取配置文件的回调（作为事件在事件循环启动后执行），并触发 config_pre/post 钩子。
 * 做法：先 hook_call(frr_config_pre)；读主配置（失败则尝试 backup_config_file 并恢复原配置）；
 *   事务型 CLI 下自动提交候选配置；最后 hook_call(frr_config_post)。
 * 参数：t 为事件对象（此处不用其内容）。
 * 坑：放在事件循环里执行是为了让集成配置(integrated config)与非集成配置的时序一致。 */
/*
 * Why is this a thread?
 *
 * The read in of config for integrated config happens *after*
 * thread execution starts( because it is passed in via a vtysh -b -n )
 * While if you are not using integrated config we want the ability
 * to read the config in after thread execution starts, so that
 * we can match this behavior.
 */
static void frr_config_read_in(struct event *t)
{
	hook_call(frr_config_pre, master);

	if (!vty_read_config(vty_shared_candidate_config, di->config_file,
			     config_default)
	    && di->backup_config_file) {
		char *orig = XSTRDUP(MTYPE_TMP, host_config_get());

		zlog_info("Attempting to read backup config file: %s specified",
			  di->backup_config_file);
		vty_read_config(vty_shared_candidate_config,
				di->backup_config_file, config_default);

		host_config_set(orig);
		XFREE(MTYPE_TMP, orig);
	}

	/*
	 * Automatically commit the candidate configuration after
	 * reading the configuration file.
	 */
	if (frr_get_cli_mode() == FRR_CLI_TRANSACTIONAL) {
		struct nb_context context = {};
		char errmsg[BUFSIZ] = {0};
		int ret;

		context.client = NB_CLIENT_CLI;
		ret = nb_candidate_commit(context, vty_shared_candidate_config,
					  true, "Read configuration file", NULL,
					  errmsg, sizeof(errmsg));
		if (ret != NB_OK && ret != NB_ERR_NO_CHANGES)
			zlog_err(
				"%s: failed to read configuration file: %s (%s)",
				__func__, nb_err_name(ret), errmsg);
	}

	hook_call(frr_config_post, master);
}

/* 完成“读配置 + 守护化 + 写 pid”的启动切换。
 * 步骤：hook_call(frr_late_init)；非 SPLIT_CONFIG 时（dryrun 则立即读配置并退出，
 *   否则安排 frr_config_read_in 事件）；守护模式或终端模式下调用 frr_daemonize；
 *   置 frr_is_after_fork；补默认 pid 文件并写 pid；初始化 zlog 线程缓冲。 */
void frr_config_fork(void)
{
	hook_call(frr_late_init, master);

	if (!(di->flags & FRR_NO_SPLIT_CONFIG)) {
		/* Don't start execution if we are in dry-run mode */
		if (di->dryrun) {
			frr_config_read_in(NULL);
			exit(0);
		}

		event_add_event(master, frr_config_read_in, NULL, 0,
				&di->read_in);
	}

	if (di->daemon_mode || di->terminal)
		frr_daemonize();

	frr_is_after_fork = true;

	if (!di->pid_file)
		di->pid_file = pidfile_default;
	pid_output(di->pid_file);
	zlog_tls_buffer_init();
}

/* 按需释放守护化控制 socket：若处于“保持附着”(nodetach_term/nodetach_daemon) 状态则不动，
 * 否则关闭并置 -1，表示不再需要与父进程的握手通道。 */
static void frr_check_detach(void)
{
	if (nodetach_term || nodetach_daemon)
		return;

	if (daemon_ctl_sock != -1)
		close(daemon_ctl_sock);
	daemon_ctl_sock = -1;
}

/* 启动 VTY 服务：在 Unix socket（或可选 TCP）上监听，供 vtysh 等连接。
 * 关键参数：check_detach 为真时先调用 frr_check_detach 处理守护化控制 socket。 */
void frr_vty_serv_start(bool check_detach)
{
	/* allow explicit override of vty_path in the future
	 * (not currently set anywhere) */
	if (!di->vty_path) {
		const char *dir;
		char defvtydir[256];

		snprintf(defvtydir, sizeof(defvtydir), "%s", frr_runstatedir);

		dir = di->vty_sock_path ? di->vty_sock_path : defvtydir;

		if (di->instance)
			snprintf(vtypath_default, sizeof(vtypath_default),
				 "%s/%s-%d.vty", dir, di->name, di->instance);
		else
			snprintf(vtypath_default, sizeof(vtypath_default),
				 "%s/%s.vty", dir, di->name);

		di->vty_path = vtypath_default;
	}

	vty_serv_start(di->vty_addr, di->vty_port, di->vty_path);

	if (check_detach)
		frr_check_detach();
}

/* 停止 VTY 服务：调用 vty_serv_stop 关闭监听，并删除对应的 vty socket 文件 */
void frr_vty_serv_stop(void)
{
	vty_serv_stop();

	if (di->vty_path)
		unlink(di->vty_path);
}

/* 终端会话(-t)结束时的回调。若未开启守护模式或 isexit 为真，则打印退出信息并（非主动退出时）
 * 触发 SIGINT；否则打印 daemonizing 并把 tty 上的 stdin/stdout/stderr 重定向到 /dev/null
 * （当 stdio 是 tty 且 stdout 未被 --log stdout 占用时）。
 * 关键参数：isexit 表示是否为正常退出路径。 */
static void frr_terminal_close(int isexit)
{
	int nullfd;

	nodetach_term = false;
	frr_check_detach();

	if (!di->daemon_mode || isexit) {
		printf("\n%s exiting\n", di->name);
		if (!isexit)
			raise(SIGINT);
		return;
	} else {
		printf("\n%s daemonizing\n", di->name);
		fflush(stdout);
	}

	nullfd = open("/dev/null", O_RDONLY | O_NOCTTY);
	if (nullfd == -1) {
		flog_err_sys(EC_LIB_SYSTEM_CALL,
			     "%s: failed to open /dev/null: %s", __func__,
			     safe_strerror(errno));
	} else {
		int fd;
		/*
		 * only redirect stdin, stdout, stderr to null when a tty also
		 * don't redirect when stdout is set with --log stdout
		 */
		for (fd = 2; fd >= 0; fd--)
			if (isatty(fd) &&
			    (fd != STDOUT_FILENO || !logging_to_stdout))
				dup2(nullfd, fd);
		close(nullfd);
	}
}

/* 守护化控制 socket 的可读事件对象（在 frr_run 里注册），保存以便管理 */
static struct event *daemon_ctl_thread = NULL;

/* 处理来自父进程(守护化握手 socket)的控制字节：S=挂起(SIGTSTP)、R=恢复、I=SIGINT 退出、
 * Q=SIGQUIT 关闭终端。处理完重新注册可读事件，保持持续监听（goto out 亦重新注册）。
 * 关键参数：t 为事件对象（此处未用）。 */
static void frr_daemon_ctl(struct event *t)
{
	char buf[1];
	ssize_t nr;

	nr = recv(daemon_ctl_sock, buf, sizeof(buf), 0);
	if (nr < 0 && (errno == EINTR || errno == EAGAIN))
		goto out;
	if (nr <= 0)
		return;

	switch (buf[0]) {
	case 'S': /* SIGTSTP */
		vty_stdio_suspend();
		if (send(daemon_ctl_sock, "s", 1, 0) < 0)
			zlog_err("%s send(\"s\") error (SIGTSTP propagation)",
				 (di && di->name ? di->name : ""));
		break;
	case 'R': /* SIGTCNT [implicit] */
		vty_stdio_resume();
		break;
	case 'I': /* SIGINT */
		di->daemon_mode = false;
		raise(SIGINT);
		break;
	case 'Q': /* SIGQUIT */
		di->daemon_mode = true;
		vty_stdio_close();
		break;
	}

out:
	event_add_read(master, frr_daemon_ctl, NULL, daemon_ctl_sock,
		       &daemon_ctl_thread);
}

/* 与 FRR_DETACH_LATER 配套：清除 nodetach_daemon 并释放守护化控制 socket，正式与父进程分离 */
void frr_detach(void)
{
	nodetach_daemon = false;
	frr_check_detach();
}

/* 进入主事件循环，是守护进程启动的最后一步（通常配置已就绪）。
 * 步骤：非 MANUAL_VTY_START 时启动 VTY 服务；打印启动日志；终端模式下开 vty_stdio 并监听
 *   守护化控制 socket，守护模式下把 tty 重定向到 /dev/null 并释放控制 socket；
 *   结束启动期日志后进入 event_fetch/event_call 循环，直到进程退出。
 * 关键参数：loop 主事件循环（即 frr_init 返回的 master）。 */
void frr_run(struct event_loop *loop)
{
	char instanceinfo[64] = "";
	struct event thread;

	if (!(di->flags & FRR_MANUAL_VTY_START))
		frr_vty_serv_start(false);

	if (di->instance)
		snprintf(instanceinfo, sizeof(instanceinfo), "instance %u ",
			 di->instance);

	zlog_notice("%s %s starting: %svty@%d%s", di->name, FRR_VERSION,
		    instanceinfo, di->vty_port, di->startinfo);

	if (di->terminal) {
		nodetach_term = true;

		vty_stdio(frr_terminal_close);
		if (daemon_ctl_sock != -1) {
			set_nonblocking(daemon_ctl_sock);
			event_add_read(loop, frr_daemon_ctl, NULL,
				       daemon_ctl_sock, &daemon_ctl_thread);
		}
	} else if (di->daemon_mode) {
		int nullfd = open("/dev/null", O_RDONLY | O_NOCTTY);
		if (nullfd == -1) {
			flog_err_sys(EC_LIB_SYSTEM_CALL,
				     "%s: failed to open /dev/null: %s",
				     __func__, safe_strerror(errno));
		} else {
			int fd;
			/*
			 * only redirect stdin, stdout, stderr to null when a
			 * tty also don't redirect when stdout is set with --log
			 * stdout
			 */
			for (fd = 2; fd >= 0; fd--)
				if (isatty(fd) &&
				    (fd != STDOUT_FILENO || !logging_to_stdout))
					dup2(nullfd, fd);
			close(nullfd);
		}

		if (!(di->flags & FRR_MANUAL_VTY_START))
			frr_check_detach();
	}

	/* end fixed stderr startup logging */
	zlog_startup_end();

	while (event_fetch(loop, &thread))
		event_call(&thread);
}

/* 触发 frr_early_fini 逆序钩子：在协议守护进程自身关闭之前执行 */
void frr_early_fini(void)
{
	hook_call(frr_early_fini);
}

/* 进程收尾：触发 frr_fini 钩子，然后依次拆掉内存统计、VTY、命令行、北向接口、YANG、
 * 可选数据库、日志引用、脚本、线程、privs、信号、主事件循环与日志线程缓冲，
 * 最后（按开关）打印内存统计。
 * 坑：zlog_fini 被刻意放在 if (0) 中禁用，让 zlog 一直存活到 exit()，代价是内存泄漏，
 *   这些泄漏由 DEFINE_MGROUP_ACTIVEATEXIT 标记并在退出统计中排除。 */
void frr_fini(void)
{
	hook_call(frr_fini);

	frr_memory_fini();

	vty_terminate();
	cmd_terminate();
	nb_terminate();
	yang_terminate();
#ifdef HAVE_SQLITE3
	db_close();
#endif
	log_ref_fini();

#ifdef HAVE_SCRIPTING
	frrscript_fini();
#endif
	frr_pthread_finish();
	zprivs_terminate(di->privs);
	/* signal_init -> nothing needed */
	event_master_free(master);
	master = NULL;
	zlog_tls_buffer_fini();

	if (0) {
		/* this is intentionally disabled.  zlog remains running until
		 * exit(), so even the very last item done during shutdown can
		 * have its zlog() messages written out.
		 *
		 * Yes this causes memory leaks.  They are explicitly marked
		 * with DEFINE_MGROUP_ACTIVEATEXIT, which is only used for
		 * log target memory allocations, and excluded from leak
		 * reporting at shutdown.  This is strongly preferable over
		 * just discarding error messages at shutdown.
		 */
		zlog_fini();
	}

	/* frrmod_init -> nothing needed / hooks */
	rcu_shutdown();

	frrmod_terminate();

	log_memstats(di->name, debug_memstats_at_exit);
	zlog_tmpdir_fini();
}

/* 从磁盘加载守护进程状态 JSON：按 di->state_paths 顺序尝试，返回第一个成功读到的对象。
 * 返回：解析出的 json 对象；若无任何文件命中则返回空对象。
 * 坑：di->state_paths 未配置时会 assert 失败；返回对象由调用者释放。 */
struct json_object *frr_daemon_state_load(void)
{
	struct json_object *state;
	char **state_path;

	assertf(di->state_paths,
		"CODE BUG: daemon trying to load state, but no state path in frr_daemon_info");

	for (state_path = di->state_paths; *state_path; state_path++) {
		state = json_object_from_file(*state_path);
		if (state)
			return state;
	}

	return json_object_new_object();
}

/* 把守护进程状态 JSON 写入磁盘（原子写：先写临时文件再改名）。
 * 关键参数：statep 指向状态 json 对象的指针（保存后可能被释放或置空）。
 * 坑：写文件的思路与 command.c 的 file_write_config 类似但不同
 *   （此处用唯一临时名并保留旧配置备份）。 */
/* cross-reference file_write_config() in command.c
 * the code there is similar but not identical (configs use a unique temporary
 * name for writing and keep a backup of the previous config.)
 */
void frr_daemon_state_save(struct json_object **statep)
{
	struct json_object *state = *statep;
	char *state_path, *slash, *temp_name, **other;
	size_t name_len, json_len;
	const char *json_str;
	int dirfd, fd;

	assertf(di->state_paths,
		"CODE BUG: daemon trying to save state, but no state path in frr_daemon_info");

	state_path = di->state_paths[0];
	json_str = json_object_to_json_string_ext(state,
						  JSON_C_TO_STRING_PRETTY);
	json_len = strlen(json_str);

	/* To correctly fsync() and ensure we have either consistent old state
	 * or consistent new state but no fs-damage garbage inbetween, we need
	 * to work with a directory fd.  If we need that anyway we might as
	 * well use the dirfd with openat() & co in fd-relative operations.
	 */

	slash = strrchr(state_path, '/');
	if (slash) {
		char *state_dir;

		state_dir = XSTRDUP(MTYPE_TMP, state_path);
		state_dir[slash - state_path] = '\0';
		dirfd = open(state_dir, O_DIRECTORY | O_RDONLY);
		XFREE(MTYPE_TMP, state_dir);

		if (dirfd < 0) {
			zlog_err("failed to open directory %pSQq for saving daemon state: %m",
				 state_dir);
			return;
		}

		/* skip to file name */
		slash++;
	} else {
		dirfd = open(".", O_DIRECTORY | O_RDONLY);
		if (dirfd < 0) {
			zlog_err(
				"failed to open current directory for saving daemon state: %m");
			return;
		}

		/* file name = path */
		slash = state_path;
	}

	/* unlike saving configs, a temporary unique filename is unhelpful
	 * here as it might litter files on limited write-heavy storage
	 * (think switch with small NOR flash for frequently written data.)
	 *
	 * => always use filename with .sav suffix, worst case it litters one
	 * file.
	 */
	name_len = strlen(slash);
	temp_name = XMALLOC(MTYPE_TMP, name_len + 5);
	memcpy(temp_name, slash, name_len);
	memcpy(temp_name + name_len, ".sav", 5);

	/* state file is always 0600, it's by and for FRR itself only */
	fd = openat(dirfd, temp_name, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		zlog_err("failed to open temporary daemon state save file for %pSQq: %m",
			 state_path);
		goto out_closedir_free;
	}

	while (json_len) {
		ssize_t nwr = write(fd, json_str, json_len);

		if (nwr <= 0) {
			zlog_err("failed to write temporary daemon state to %pSQq: %m",
				 state_path);

			close(fd);
			unlinkat(dirfd, temp_name, 0);
			goto out_closedir_free;
		}

		json_str += nwr;
		json_len -= nwr;
	}

	/* fsync is theoretically implicit in close(), but... */
	if (fsync(fd) < 0)
		zlog_warn("fsync for daemon state %pSQq failed: %m", state_path);
	close(fd);

	/* this is the *actual* fsync that ensures we're consistent.  The
	 * file fsync only syncs the inode, but not the directory entry
	 * referring to it.
	 */
	if (fsync(dirfd) < 0)
		zlog_warn("directory fsync for daemon state %pSQq failed: %m",
			  state_path);

	/* atomic, hopefully. */
	if (renameat(dirfd, temp_name, dirfd, slash) < 0) {
		zlog_err("renaming daemon state %pSQq to %pSQq failed: %m",
			 temp_name, state_path);
		/* no unlink here, give the user a chance to investigate */
		goto out_closedir_free;
	}

	/* and the rename needs to be synced too */
	if (fsync(dirfd) < 0)
		zlog_warn("directory fsync for daemon state %pSQq failed after rename: %m",
			  state_path);

	/* daemon may specify other deprecated paths to load from; since we
	 * just saved successfully we should delete those.
	 */
	for (other = di->state_paths + 1; *other; other++) {
		if (unlink(*other) == 0)
			continue;
		if (errno == ENOENT || errno == ENOTDIR)
			continue;

		zlog_warn("failed to remove deprecated daemon state file %pSQq: %m",
			  *other);
	}

out_closedir_free:
	XFREE(MTYPE_TMP, temp_name);
	close(dirfd);

	json_object_free(state);
	*statep = NULL;
}

#ifdef INTERP
static const char interp[]
	__attribute__((section(".interp"), used)) = INTERP;
#endif
/* libfrr.so 的“可执行入口”：打印 FRR 全名、版本、版权、git 信息与编译配置，然后 _exit(0)。
 * 关键：此入口跳过 libc 初始化，因此只能调用极少数函数（这里只用 write 与 _exit）。
 * 用途：让 libfrr.so 也能被直接执行以查看版本。 */
/*
 * executable entry point for libfrr.so
 *
 * note that libc initialization is skipped for this so the set of functions
 * that can be called is rather limited
 */
extern void _libfrr_version(void)
	__attribute__((visibility("hidden"), noreturn));
void _libfrr_version(void)
{
	const char banner[] =
		FRR_FULL_NAME " " FRR_VERSION ".\n"
		FRR_COPYRIGHT GIT_INFO "\n"
#ifdef ENABLE_VERSION_BUILD_CONFIG
		"configured with:\n    " FRR_CONFIG_ARGS "\n"
#endif
	;
	write(1, banner, sizeof(banner) - 1);
	_exit(0);
}

/* 把打包版本号渲染成 主.次.修订 形式写入 buf（长度 buflen），返回 buf */
/* Render simple version tuple to string */
const char *frr_vers2str(uint32_t version, char *buf, int buflen)
{
	snprintf(buf, buflen, "%d.%d.%d", MAJOR_FRRVERSION(version),
		 MINOR_FRRVERSION(version), SUB_FRRVERSION(version));

	return buf;
}

/* 判断当前是否已作为守护进程初始化（di 非空即视为是） */
bool frr_is_daemon(void)
{
	if (di)
		return true;

	return false;
}

/* 先刷新 zlog 的线程缓冲再退出（FRR_NORETURN，不返回），status 为退出码 */
FRR_NORETURN void frr_exit_with_buffer_flush(int status)
{
	zlog_tls_buffer_flush();
	exit(status);
}

/* tcmalloc 归还空闲内存的专有代码：用周期定时器把 tcmalloc 缓存的空闲页还给操作系统 */
/*
 * Special code for tcmalloc mem lib, releasing free mem back to the OS.
 */
static struct event *t_mem_release_event;

#ifdef HAVE_TCMALLOC

/* tcmalloc 归还内存的相关常量：默认速率 1 MB/秒、触发阈值约 20MB、检查周期 10 秒 */
#define FRR_MEM_RELEASE_MB_DEFAULT 1 /* In MB/sec */
#define FRR_MEM_RELEASE_THRESHOLD  20 * 1024 * 1024
#define FRR_MEM_RELEASE_TIMEOUT    10 /* Seconds */

/* 运行期可调的 tcmalloc 归还速率(MB/秒)与检查周期(秒) */
static uint32_t mem_release_mb = FRR_MEM_RELEASE_MB_DEFAULT;
static uint32_t mem_release_timeout = FRR_MEM_RELEASE_TIMEOUT;

#include "gperftools/tcmalloc.h"
#include "gperftools/malloc_extension_c.h"

/* Timer handler to release free mem */
/* tcmalloc 定时回调：重新安排下一次触发；读取 pageheap 空闲字节，若超过阈值则按速率
 * 释放一部分内存归还系统。关键参数：event 定时事件对象。 */
static void tcmalloc_timer(struct event *event)
{
	size_t release, sval;

	if (mem_release_timeout == 0)
		return;

	/* Schedule again */
	event_add_timer(master, tcmalloc_timer, NULL, mem_release_timeout,
				&t_mem_release_event);

	MallocExtension_GetNumericProperty("tcmalloc.pageheap_free_bytes", &sval);

	/* Check mem stats, release mem if we're over the threshold */
	if (sval > FRR_MEM_RELEASE_THRESHOLD) {
		release = mem_release_mb * 1024 * 1024 * mem_release_timeout;
		MallocExtension_ReleaseToSystem(release);
	}
}

/* 处理内存归还速率配置变更：速率变化时取消旧定时器；速率为正则按新速率重新安排。
 * 关键参数：rate 期望速率(MB/秒)。 */
/* Handle config changes */
void frr_mem_release_config(uint32_t rate)
{
	if (rate != mem_release_mb)
		event_cancel(&t_mem_release_event);

	mem_release_mb = rate;
	if (mem_release_mb > 0)
		/* Schedule */
		event_add_timer(master, tcmalloc_timer, NULL, mem_release_timeout,
				&t_mem_release_event);
}

/* tcmalloc 库初始化钩子：若速率被禁用则直接返回，否则安排周期性的 tcmalloc_timer。
 * 返回 0 表示成功。关键参数：loop 事件循环。 */
static int frr_tcmalloc_lib_init(struct event_loop *loop)
{
	/* Using tcmalloc memory-release functions: configure periodic
	 * task to release free memory back to the OS.
	 */

	/* Disabled (via config?) */
	if (mem_release_mb == 0)
		return 0;

	event_add_timer(master, tcmalloc_timer, NULL, mem_release_timeout,
			&t_mem_release_event);

	return 0;
}

#else
/* tcmalloc 未启用时的空实现：速率配置被忽略。 */
/* No-op */
void frr_mem_release_config(uint32_t rate)
{
	/* No-op */
}

#endif /* HAVE_TCMALLOC */

/* 初始化与平台相关的内存功能：启用 tcmalloc 时注册 frr_config_post 回调以启动归还定时器。 */
/* Init memory-related functions for some platforms */
static void frr_memory_init(struct event_loop *loop)
{
#ifdef HAVE_TCMALLOC
	/* Register for post-config callback */
	hook_register(frr_config_post, frr_tcmalloc_lib_init);
#endif
}

/* 收尾与平台相关的内存功能：取消 tcmalloc 归还定时器（未启用时该事件为空，无害）。 */
/* De-init memory-related functions for some platforms */
static void frr_memory_fini(void)
{
	event_cancel(&t_mem_release_event);
}
