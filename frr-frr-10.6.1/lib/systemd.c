// SPDX-License-Identifier: GPL-2.0-or-later
/* lib/systemd Code
 * Copyright (C) 2016 Cumulus Networks, Inc.
 * Donald Sharp
 */

/* ============================================================================
 * 【文件导读】systemd.c —— 通过 NOTIFY_SOCKET 向 systemd 上报状态
 * ============================================================================
 * 【做什么】systemd 用“notify 协议”让被拉起的进程回报健康状况：进程往环境变量
 * NOTIFY_SOCKET 指定的 UNIX 数据报套接字里发一封 key=value 文本，systemd 收到后
 * 据此判断 READY / STOPPING / 看门狗心跳。本文件就是这套 notify 协议的最小自实现
 * (原注释说它“功能上等价于 sd_notify()”，只是不引入 libsystemd 依赖)。
 *
 * 【为什么不用 libsystemd】FRR 要能在没有 systemd 的发行版上编译，所以自己实现，
 * 系统里没有 systemd 时环境变量为空，一切退化为空操作。
 *
 * 【关键环境变量】
 *   NOTIFY_SOCKET  : 上报套接字路径；**没它则整个文件的功能全部空转**。
 *   WATCHDOG_USEC  : systemd 要求的看门狗周期(微秒)。
 *   WATCHDOG_PID   : systemd 期望发心跳的进程号(FRR 目前忽略，见下)。
 *   JOURNAL_STREAM : 形如 "设备号:inode"，用来判断标准输出/错误是否接了 journald。
 *
 * 【阅读顺序】systemd_send_information(发信底层) → 各 send_* 封装 → getenv_int
 * → systemd_init_env(启动期解析环境变量)。
 * 【易踩的坑】所有上报都是**尽力而为**：套接字创建失败、发送失败都被静默忽略，
 * 因为这里出错也无处上报，且不能影响主业务。
 * ============================================================================
 */
#include <zebra.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "frrevent.h"
#include "systemd.h"
#include "lib_errors.h"

/* these are cleared from env so they don't "leak" into things we fork(),
 * particularly for watchfrr starting individual daemons
 *
 * watchdog_pid is currently not used since watchfrr starts forking.
 * (TODO: handle that better, somehow?)
 */
/* 【静态状态：看门狗参数】
 *   watchdog_pid  : systemd 期望发心跳的进程 PID；-1 表示“不适用/未指定”。当前
 *                   代码只在 systemd_init_env 里读进来做归一化，**并未真正使用**，
 *                   因为 watchfrr 会 fork 子进程，PID 语义在此场景下不成立(原英文
 *                   注释也留了 TODO)。
 *   watchdog_msec : 心跳周期(毫秒)。0 表示不需要心跳；>0 时 systemd_send_started
 *                   会启动周期定时器。
 * 【易踩的坑】watchdog_msec 的**单位是毫秒**，而 systemd 给的是微秒，换算在
 *   systemd_init_env 里做，别在这里再乘除一次。 */
static pid_t watchdog_pid = -1;
static intmax_t watchdog_msec;

/* not used yet, but can trigger auto-switch to journald logging */
/* 【全局标志】标准输出、标准错误是否连接到 journald。判定方法是比对 JOURNAL_STREAM
 * 里的设备号与 inode 同 fstat(STDOUT/STDERR) 是否一致——这是**唯一**可靠的判据，
 * 不能只看环境变量是否存在。
 * 【用途】原英文注释说“尚未使用，但可用于自动切到 journald 日志”。即目前只是留档，
 * 读者若在别处看到它被读取，说明该特性已被启用。 */
bool sd_stdout_is_journal;
bool sd_stderr_is_journal;

/* 【静态状态】notify 套接字路径，来自环境变量 NOTIFY_SOCKET(见 systemd_init_env)。
 * 【易踩的坑】它是 getenv 返回的**只读字符串指针**，不归本模块所有，切勿 free 或
 * 改写；为 NULL 表示当前没有 systemd，所有上报函数据此空转。 */
static char *notify_socket;

/* talk to whatever entity claims to be systemd ;)
 *
 * refer to sd_notify docs for messages systemd accepts over this socket.
 * This function should be functionally equivalent to sd_notify().
 */
/* 【函数】发送一条 notify 消息的底层实现(功能上等价于 sd_notify)。
 *   参数 info : 形如 "READY=1" / "STOPPING=1" / "STATUS=xxx" / "WATCHDOG=1" 的
 *               一行文本，具体合法取值见 sd_notify 文档。
 * 【实现要点】
 *   ① 每次都新建一个 AF_UNIX **数据报** 套接字并发完即关——开销可接受，换来的是
 *      无需维护长连接状态。
 *   ② 路径以 '@' 开头时改写为 '\0'，这是适配 Linux **抽象命名空间**套接字的约定。
 *   ③ notify_socket 为 NULL(systemd 不存在)时直接返回，不发也不报错。
 * 【易踩的坑】sendto 的失败被**刻意忽略**(原注释“nothing we can do”)；因此调用方
 *   无法得知消息是否真的送达，不要依赖它做流程控制。 */
static void systemd_send_information(const char *info)
{
	int sock;
	struct sockaddr_un sun;

	if (!notify_socket)
		return;

	sock = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sock < 0)
		return;

	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, notify_socket, sizeof(sun.sun_path));

	/* linux abstract unix socket namespace */
	if (sun.sun_path[0] == '@')
		sun.sun_path[0] = '\0';

	/* nothing we can do if this errors out... */
	(void)sendto(sock, info, strlen(info), 0, (struct sockaddr *)&sun,
		     sizeof(sun));

	close(sock);
}

/* 【函数】通知 systemd“本进程即将停止”：先清空 STATUS 行，再置 STOPPING=1。
 * 【顺序的用意】先发 "STATUS=" 是**清掉上一次的状态文本**，避免 systemctl status
 *   里还挂着过期信息；再发 STOPPING=1 让 systemd 知道这是主动停止，不触发重启策略。 */
void systemd_send_stopping(void)
{
	systemd_send_information("STATUS=");
	systemd_send_information("STOPPING=1");
}

/* 【静态状态】心跳定时器所挂的事件循环，由 systemd_send_started 填入。
 * 【为何要存起来】心跳是“发一次、再预约下一次”的自续定时器，回调里重新注册时必须
 * 再次用到这个 event_loop，所以缓存一份。 */
static struct event_loop *systemd_master = NULL;

/* 【函数】看门狗心跳的定时器回调：发一次 WATCHDOG=1，然后把自己**重新预约**到
 * watchdog_msec 毫秒之后(自续定时器)。
 *   参数 t : 触发本次的 event 对象，本回调不使用。
 * 【易踩的坑】① 若忘记在回调里重新 add，心跳只发一次，systemd 会判定进程卡死并
 *   强杀；② assert(watchdog_msec > 0) 说明本函数**只能**在已确认需要心跳时进入，
 *   调用方的 watchdog_msec > 0 判断不是可有可无的。 */
static void systemd_send_watchdog(struct event *t)
{
	systemd_send_information("WATCHDOG=1");

	assert(watchdog_msec > 0);
	event_add_timer_msec(systemd_master, systemd_send_watchdog, NULL,
			     watchdog_msec, NULL);
}

/* 【函数】通知 systemd“启动完成(READY=1)”，并在需要时启动看门狗心跳。
 *   参数 m : 承载心跳定时器的事件循环，**不能为 NULL**(有 assert)。
 * 【副作用】把 m 缓存到 systemd_master，随后每 watchdog_msec 毫秒发一次心跳。
 * 【易踩的坑】只有 watchdog_msec > 0 时才起心跳——这个值由 systemd_init_env 依据
 *   环境变量 WATCHDOG_USEC 算出，没配就不需要心跳。 */
void systemd_send_started(struct event_loop *m)
{
	assert(m != NULL);

	systemd_master = m;

	systemd_send_information("READY=1");
	if (watchdog_msec > 0)
		systemd_send_watchdog(NULL);
}

/* 【函数】上报一句人类可读的状态文本，即 systemctl status 里看到的那一行。
 *   参数 status : 状态文本；不能为 NULL。
 * 【实现】拼成 "STATUS=<文本>" 再发送，**不要**自己带 "STATUS=" 前缀。
 * 【易踩的坑】缓冲区固定 1024 字节，超长状态会被 snprintf **静默截断**；这不是
 *   安全边界，别把敏感信息或大段文本塞进来。 */
void systemd_send_status(const char *status)
{
	char buffer[1024];

	snprintf(buffer, sizeof(buffer), "STATUS=%s", status);
	systemd_send_information(buffer);
}

/* 【函数】读取一个整型环境变量(仅在 systemd_init_env 内部使用)。
 *   参数 varname : 变量名；dflt : 变量不存在或格式非法时的默认值。
 *   返回        : 解析出的整数值，或 dflt。
 * 【实现要点】strtoimax 的基数传 0，因此支持 "0x" 十六进制与八进制前缀——这与
 *   systemd 传参的写法一致。
 * 【易踩的坑】判非法用的是 **err(未消费的第一个字符) 与 val(是否为空串)**，
 *   而不是 errno；这正是“整串必须被完整解析”的写法。 */
static intmax_t getenv_int(const char *varname, intmax_t dflt)
{
	char *val, *err;
	intmax_t intval;

	val = getenv(varname);
	if (!val)
		return dflt;

	intval = strtoimax(val, &err, 0);
	if (*err || !*val)
		return dflt;
	return intval;
}

/* 【函数】启动期解析 systemd 相关的环境变量，并**清理**它们以免泄漏给子进程。
 * 【流程】
 *   ① 读 NOTIFY_SOCKET；没读到就**整体不做任何事**(没有 systemd)。
 *   ② 读 WATCHDOG_PID(归一化为 -1 或 >0)。
 *   ③ 读 WATCHDOG_USEC：以微秒为单位，先做整除折算成毫秒——
 *      注意代码里除以的是 3000 而不是 1000，因为原注释写明 systemd 给的是
 *      **deadline**(截止时刻)而非周期，需要再除以 3 留出安全余量，共 3000；
 *      且当值小于 3000 微秒时视为不可用，会打一条错误日志并把 watchdog_msec 置 0。
 *   ④ 读 JOURNAL_STREAM(格式 "设备号:inode")，与 fstat(1) / fstat(2) 的实际
 *      设备号、inode 逐一比对，命中则把对应标志置 true。
 *   ⑤ 最后 unsetenv 掉 WATCHDOG_* ——原英文注释说明这是为了**不让它们泄漏进 fork
 *      出来的子进程**(尤其 watchfrr 拉起的各守护进程)，否则子进程会误以为自己也
 *      该发心跳。
 * 【易踩的坑】① 本函数应在进程极早期、日志系统就绪之后调用一次；② 它是**一次性**
 *   的，重复调用会让第二次读不到已被清理的变量；③ sscanf 的 %n 与长度校验用来保证
 *   整串被完整解析，否则 "123:456garbage" 之类会被错误接受。 */
void systemd_init_env(void)
{
	char *tmp;
	uintmax_t dev, ino;
	int len;
	struct stat st;

	notify_socket = getenv("NOTIFY_SOCKET");

	/* no point in setting up watchdog w/o notify socket */
	if (notify_socket) {
		intmax_t watchdog_usec;

		watchdog_pid = getenv_int("WATCHDOG_PID", -1);
		if (watchdog_pid <= 0)
			watchdog_pid = -1;

		/* note this is the deadline, hence the divide by 3 */
		watchdog_usec = getenv_int("WATCHDOG_USEC", 0);
		if (watchdog_usec >= 3000)
			watchdog_msec = watchdog_usec / 3000;
		else {
			if (watchdog_usec != 0)
				flog_err(
					EC_LIB_UNAVAILABLE,
					"systemd expects a %jd microsecond watchdog timer, but FRR only supports millisecond resolution!",
					watchdog_usec);
			watchdog_msec = 0;
		}
	}

	tmp = getenv("JOURNAL_STREAM");
	if (tmp && sscanf(tmp, "%ju:%ju%n", &dev, &ino, &len) == 2
	    && (size_t)len == strlen(tmp)) {
		if (fstat(1, &st) == 0 && st.st_dev == (dev_t)dev
		    && st.st_ino == (ino_t)ino)
			sd_stdout_is_journal = true;
		if (fstat(2, &st) == 0 && st.st_dev == (dev_t)dev
		    && st.st_ino == (ino_t)ino)
			sd_stderr_is_journal = true;
	}

	/* these should *not* be passed to any other process we start */
	unsetenv("WATCHDOG_PID");
	unsetenv("WATCHDOG_USEC");
}
