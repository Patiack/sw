// SPDX-License-Identifier: GPL-2.0-or-later
/* C-Ares integration to Quagga mainloop
 * Copyright (c) 2014-2015 Timo Teräs
 */

/*
 * ============================================================
 * 文件导读：resolver.c —— 把 c-ares 异步 DNS 解析接进 FRR 事件循环
 * ============================================================
 *
 * 这个文件在 FRR 里干什么
 *   同步的 getaddrinfo 会阻塞主线程，导致协议超时。本文件用 c-ares 库
 *   做异步 DNS，并把 c-ares 管理的 socket 事件与超时事件桥接到 FRR 的
 *   event_loop（frrevent）。核心是“FD 管家”逻辑：c-ares 告诉我们哪些 FD
 *   可读 / 可写，我们据此在 FRR 事件循环里注册 / 注销对应事件，并在事件
 *   触发时回调 ares_process_fd 推进解析。
 *
 * 代码组织（自顶向下阅读）
 *   1. resolver_state（全局单例 state）与 struct resolver_fd（每个被
 *      c-ares 使用的 FD 的读写事件记录）。
 *   2. FD 哈希表：resolver_fd_get / _drop_maybe 负责按需建 / 删记录。
 *   3. 事件回调：resolver_cb_timeout、resolver_cb_socket_readable、
 *      resolver_cb_socket_writable 三个，分别驱动 c-ares 推进与重算超时。
 *   4. ares_socket_cb：c-ares 的 socket 状态回调，注册 / 注销 FD 事件。
 *   5. ares_address_cb：解析完成回调（新旧 c-ares API 各一版），把结果
 *      转成 union sockunion 数组后交还调用方回调。
 *   6. resolver_cb_literal：IP 字面量快捷路径的延迟回调。
 *   7. resolver_resolve：对外主入口，处理字面量 / VRF 切换 / 发起解析。
 *   8. debug resolver 命令、resolver_init / resolver_terminate。
 *
 * 核心数据结构
 *   struct resolver_state —— c-ares channel + 主事件循环 + 超时事件。
 *   struct resolver_fd    —— fd 对应的 t_read / t_write 事件与所属 state。
 *   union sockunion       —— 回调交还的解析结果地址（见 sockunion.h）。
 *
 * 关键坑（务必理解）
 *   在 resolver_cb_socket_readable / writable 中，必须“先重新注册事件、
 *   再调 ares_process_fd”。因为 ares_process_fd 可能级联触发
 *   event_cancel 与 resolver_fd_drop_maybe，把 resfd 释放掉；先保存所需
 *   信息、先注册，才能避免 use-after-free。源码中的 "ordering important"
 *   注释强调的正是这一点。
 *
 * 与其他模块的关系
 *   依赖 c-ares（ares.h）、typesafe.h（PREDECL_HASH / DECLARE_HASH 哈希
 *   表宏）、jhash.h、frrevent.h（事件）、vrf.h（vrf_switch_to_netns，
 *   在目标 VRF 命名空间内解析）、command.h / xref.h（命令注册）。
 *
 * 阅读顺序建议
 *   先读 FD 管家（resolver_fd_get / _drop_maybe）与三个事件回调，理解与
 *   事件循环的咬合；再读 ares_socket_cb 与 ares_address_cb；最后读
 *   resolver_resolve 主流程。
 * ============================================================
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ares.h>
#include <ares_version.h>

#include "typesafe.h"
#include "jhash.h"
#include "frrevent.h"
#include "lib_errors.h"
#include "resolver.h"
#include "command.h"
#include "xref.h"
#include "vrf.h"

/*
 * 初始化 FRR 的 xref（交叉引用）子系统，用于追踪函数 / 数据被引用情况，
 * 便于链接期 / 运行期检查。每个使用 XREF 的编译单元需调用一次。
 */
XREF_SETUP();

/*
 * 解析器全局状态（单例）。
 *   channel —— c-ares 的解析通道句柄，所有查询都经它发起。
 *   master  —— FRR 主事件循环，用于注册 c-ares 的 socket / 超时事件。
 *   timeout —— 当前的“下一次 c-ares 超时”定时事件句柄。
 */
struct resolver_state {
	ares_channel channel;
	struct event_loop *master;
	struct event *timeout;
};

/*
 * 全局单例状态与调试开关。resolver_debug 由 "debug resolver" 命令控制，
 * 打开后打印解析过程中的调试日志。
 */
static struct resolver_state state;
static bool resolver_debug;

/* a FD doesn't necessarily map 1:1 to a request;  we could be talking to
 * multiple caches simultaneously, to see which responds fastest.
 * Theoretically we could also be using the same fd for multiple lookups,
 * but the c-ares API guarantees an n:1 mapping for fd => channel.
 *
 * Either way c-ares makes that decision and we just need to deal with
 * whatever FDs it gives us.
 */

/*
 * 定义 ARES_FD 内存类型（记在 LIB 模块下），用于给每个 resolver_fd
 * 记录做内存记账。PREDECL_HASH 声明一个名为 resolver_fds 的哈希表类型
 * （由 typesafe.h 宏生成），键为下面的 struct resolver_fd。
 */
DEFINE_MTYPE_STATIC(LIB, ARES_FD, "c-ares (DNS) file descriptor information");
PREDECL_HASH(resolver_fds);

/*
 * 一个被 c-ares 使用的文件描述符在本地的记录。
 *   itm    —— 哈希表链表节点（由 typesafe.h 哈希宏使用）。
 *   fd     —— 对应的 c-ares socket 文件描述符。
 *   state  —— 所属解析器全局状态。
 *   t_read / t_write —— 注册在 FRR 事件循环上的读 / 写事件句柄（可为 NULL）。
 * 作用：把 c-ares 的 socket 可读 / 可写事件桥接到 FRR 的事件机制。
 */
struct resolver_fd {
	struct resolver_fds_item itm;

	int fd;
	struct resolver_state *state;
	struct event *t_read, *t_write;
};

/*
 * 哈希表比较函数：按 fd 数值比较（numcmp 返回 -1 / 0 / 1），用于查找。
 */
static int resolver_fd_cmp(const struct resolver_fd *a,
			   const struct resolver_fd *b)
{
	return numcmp(a->fd, b->fd);
}

/*
 * 哈希表哈希函数：对 fd 做 jhash_1word，固定种子 0xacd04c9e。
 */
static uint32_t resolver_fd_hash(const struct resolver_fd *item)
{
	return jhash_1word(item->fd, 0xacd04c9e);
}

/*
 * 实例化 resolver_fds 哈希表类型（比较 / 哈希函数已给出）；随后声明全局
 * 哈希表实例 resfds（单元素数组，INIT_HASH 初始化）。
 */
DECLARE_HASH(resolver_fds, struct resolver_fd, itm, resolver_fd_cmp,
	     resolver_fd_hash);

/*
 * 全局 FD 记录表。写作长度为 1 的数组是为了让 “resfds” 能像指针一样
 * 传给哈希表 API（这是 FRR 里常见的 typesafe 容器惯用法）。
 */
static struct resolver_fds_head resfds[1] = {INIT_HASH(resfds[0])};

/*
 * 按 fd 查找 FD 记录；不存在且 newstate 非 NULL 时新建并加入哈希表。
 *   fd       —— 目标描述符。
 *   newstate —— 若需要新建记录，则用它作为 state；为 NULL 表示“只查不建”。
 * 返回值：找到 / 新建的记录指针；未找到且不允许新建时返回 NULL。
 * 副作用：可能分配记录并注册进 resfds；调试开启时打印日志。
 */
static struct resolver_fd *resolver_fd_get(int fd,
					   struct resolver_state *newstate)
{
	struct resolver_fd ref = {.fd = fd}, *res;

	res = resolver_fds_find(resfds, &ref);
	if (!res && newstate) {
		res = XCALLOC(MTYPE_ARES_FD, sizeof(*res));
		res->fd = fd;
		res->state = newstate;
		resolver_fds_add(resfds, res);

		if (resolver_debug)
			zlog_debug("c-ares registered FD %d", fd);
	}
	return res;
}

/*
 * 若某个 FD 记录已无读 / 写事件（两者都为 NULL），说明它不再需要被监听，
 * 就把它从哈希表删除并释放。
 *   resfd —— 待检查的记录。
 * 无返回值。副作用：可能删除并 XFREE 该记录（调用后不得再引用 resfd）。
 */
static void resolver_fd_drop_maybe(struct resolver_fd *resfd)
{
	if (resfd->t_read || resfd->t_write)
		return;

	if (resolver_debug)
		zlog_debug("c-ares unregistered FD %d", resfd->fd);

	resolver_fds_del(resfds, resfd);
	XFREE(MTYPE_ARES_FD, resfd);
}

/* end of FD housekeeping */

/*
 * 前向声明：重算 c-ares 的下一次超时并（重）注册对应定时事件。
 * 定义见下方同名函数。
 */
static void resolver_update_timeouts(struct resolver_state *r);

/*
 * 超时事件回调：c-ares 的定时器到点后触发。
 * 用 ARES_SOCKET_BAD / ARES_SOCKET_BAD 调用 ares_process_fd，让 c-ares
 * 处理超时（重传等），随后重算下一次超时。
 */
static void resolver_cb_timeout(struct event *t)
{
	struct resolver_state *r = EVENT_ARG(t);

	ares_process_fd(r->channel, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
	resolver_update_timeouts(r);
}

/*
 * 某个 c-ares socket 可读时的回调。
 * 先重新注册读事件（保持持续监听），再调 ares_process_fd 仅传读 fd 让
 * c-ares 处理，最后重算超时。
 * 关键坑（见函数内英文注释）：必须先注册、后处理——因为 ares_process_fd
 * 可能级联调用 event_cancel 与 resolver_fd_drop_maybe 把 resfd 释放，
 * 顺序颠倒会导致 use-after-free。
 */
static void resolver_cb_socket_readable(struct event *t)
{
	struct resolver_fd *resfd = EVENT_ARG(t);
	struct resolver_state *r = resfd->state;

	event_add_read(r->master, resolver_cb_socket_readable, resfd, resfd->fd,
		       &resfd->t_read);
	/* ^ ordering important:
	 * ares_process_fd may transitively call event_cancel(&resfd->t_read)
	 * combined with resolver_fd_drop_maybe, so resfd may be free'd after!
	 */
	ares_process_fd(r->channel, resfd->fd, ARES_SOCKET_BAD);
	resolver_update_timeouts(r);
}

/*
 * 某个 c-ares socket 可写时的回调。
 * 逻辑与 resolver_cb_socket_readable 对称：先重新注册写事件，再调
 * ares_process_fd 仅传写 fd，最后重算超时。
 * 同样必须“先注册、后处理”，原因见函数内英文注释（避免 resfd 被级联
 * 释放后仍被使用）。
 */
static void resolver_cb_socket_writable(struct event *t)
{
	struct resolver_fd *resfd = EVENT_ARG(t);
	struct resolver_state *r = resfd->state;

	event_add_write(r->master, resolver_cb_socket_writable, resfd,
			resfd->fd, &resfd->t_write);
	/* ^ ordering important:
	 * ares_process_fd may transitively call event_cancel(&resfd->t_write)
	 * combined with resolver_fd_drop_maybe, so resfd may be free'd after!
	 */
	ares_process_fd(r->channel, ARES_SOCKET_BAD, resfd->fd);
	resolver_update_timeouts(r);
}

/*
 * 重算并（重）注册 c-ares 的下一次超时事件。
 *   r —— 解析器状态。
 * 流程：先取消旧超时事件，向 ares_timeout 询问下一次超时时刻（相对值），
 *   有则换算成毫秒注册新的定时事件。
 * 无返回值。注意：负责保证任意时刻至多一个超时事件存在。
 */
static void resolver_update_timeouts(struct resolver_state *r)
{
	struct timeval *tv, tvbuf;

	event_cancel(&r->timeout);
	tv = ares_timeout(r->channel, NULL, &tvbuf);
	if (tv) {
		unsigned int timeoutms = tv->tv_sec * 1000 + tv->tv_usec / 1000;

		event_add_timer_msec(r->master, resolver_cb_timeout, r,
				     timeoutms, &r->timeout);
	}
}

/*
 * c-ares 的 socket 状态回调：当某个 fd 的“关注可读 / 可写”状态变化时被
 * c-ares 调用，我们据此在 FRR 事件循环里增加或取消对应事件。
 *   data     —— 注册时给的上下文（此处是 &state）。
 *   fd       —— 目标描述符。
 *   readable / writable —— c-ares 是否希望关注可读 / 可写。
 * 流程：取 / 建 FD 记录；按 readable / writable 增删 t_read / t_write；
 *   最后若两者都为空则释放记录。
 * 注意：assert(resfd->state == r) 校验记录归属，防止 fd 被多个 channel
 *   复用导致串台。
 */
static void ares_socket_cb(void *data, ares_socket_t fd, int readable,
			   int writable)
{
	struct resolver_state *r = (struct resolver_state *)data;
	struct resolver_fd *resfd;

	resfd = resolver_fd_get(fd, (readable || writable) ? r : NULL);
	if (!resfd)
		return;

	assert(resfd->state == r);

	if (!readable)
		event_cancel(&resfd->t_read);
	else if (!resfd->t_read)
		event_add_read(r->master, resolver_cb_socket_readable, resfd,
			       fd, &resfd->t_read);

	if (!writable)
		event_cancel(&resfd->t_write);
	else if (!resfd->t_write)
		event_add_write(r->master, resolver_cb_socket_writable, resfd,
				fd, &resfd->t_write);

	resolver_fd_drop_maybe(resfd);
}

#if (ARES_VERSION >= 0x011c00)
/*
 * 解析完成回调（新版 c-ares，ARES_VERSION >= 0x011c00，用
 * ares_getaddrinfo / ares_addrinfo）。
 *   arg      —— 发起时传入的 struct resolver_query *。
 *   status   —— c-ares 状态码，ARES_SUCCESS 表示成功。
 *   result   —— 结果链表；失败时可能为 NULL。
 * 行为：取出并清空 query->callback（保证只回调一次）；失败时以
 *   错误串 + n=-1 + NULL 回调；成功时把最多 16 个结果转成 union sockunion
 *   数组后以 n=实际个数回调，最后释放 result。
 * 注意：结果上限 16（addr 数组大小），超出部分被忽略。
 */
static void ares_address_cb(void *arg, int status, int timeouts,
			    struct ares_addrinfo *result)
{
	struct resolver_query *query = (struct resolver_query *)arg;
	union sockunion addr[16];
	void (*callback)(struct resolver_query *q, const char *err, int ret,
			 union sockunion *s);
	size_t i;
	struct ares_addrinfo_node *node;

	callback = query->callback;
	query->callback = NULL;

	if (status != ARES_SUCCESS) {
		if (resolver_debug)
			zlog_debug("[%p] Resolving failed (%s)",
				   query, ares_strerror(status));

		callback(query, ares_strerror(status), -1, NULL);
		if (result)
			ares_freeaddrinfo(result);
		return;
	}


	node = result->nodes;
	for (i = 0; i < array_size(addr) && node; i++) {
		memset(&addr[i], 0, sizeof(addr[i]));
		if (node->ai_addrlen <= sizeof(addr[i]))
			memcpy(&addr[i], node->ai_addr, node->ai_addrlen);
		node = node->ai_next;
	}

	if (resolver_debug)
		zlog_debug("[%p] Resolved with %d results", query, (int)i);

	callback(query, NULL, i, &addr[0]);
	ares_freeaddrinfo(result);
}
#else
/*
 * 解析完成回调（旧版 c-ares，ARES_VERSION < 0x011c00，用
 * ares_gethostbyname / struct hostent）。
 *   arg    —— struct resolver_query *。
 *   status —— c-ares 状态码。
 *   he     —— 主机信息，含 h_addrtype 与 h_addr_list。
 * 行为：与新版一致地只回调一次；失败以错误串 + n=-1 + NULL 回调；成功时
 *   把 h_addr_list 中最多 16 个地址按 h_addrtype（v4 / v6）填入 sockunion
 *   数组后回调。
 * 注意：结果上限 16。
 */
static void ares_address_cb(void *arg, int status, int timeouts,
			    struct hostent *he)
{
	struct resolver_query *query = (struct resolver_query *)arg;
	union sockunion addr[16];
	void (*callback)(struct resolver_query *, const char *, int,
			 union sockunion *);
	size_t i;

	callback = query->callback;
	query->callback = NULL;

	if (status != ARES_SUCCESS) {
		if (resolver_debug)
			zlog_debug("[%p] Resolving failed (%s)",
				   query, ares_strerror(status));

		callback(query, ares_strerror(status), -1, NULL);
		return;
	}

	for (i = 0; i < array_size(addr) && he->h_addr_list[i] != NULL; i++) {
		memset(&addr[i], 0, sizeof(addr[i]));
		addr[i].sa.sa_family = he->h_addrtype;
		switch (he->h_addrtype) {
		case AF_INET:
			memcpy(&addr[i].sin.sin_addr,
			       (uint8_t *)he->h_addr_list[i], he->h_length);
			break;
		case AF_INET6:
			memcpy(&addr[i].sin6.sin6_addr,
			       (uint8_t *)he->h_addr_list[i], he->h_length);
			break;
		}
	}

	if (resolver_debug)
		zlog_debug("[%p] Resolved with %d results", query, (int)i);

	callback(query, NULL, i, &addr[0]);
}

#endif

/*
 * IP 字面量快捷路径的延迟回调。当 resolver_resolve 发现 hostname 本身就是
 * IP 时，不立即回调，而是注册一个 0ms 定时器，最终由本函数在事件循环
 * 里回调，以与真正的名称解析保持一致的异步时序。
 * 行为：取出并清空 query->callback，然后用 literal_addr（n=1）回调一次。
 */
static void resolver_cb_literal(struct event *t)
{
	struct resolver_query *query = EVENT_ARG(t);
	void (*callback)(struct resolver_query *, const char *, int,
			 union sockunion *);

	callback = query->callback;
	query->callback = NULL;

	callback(query, NULL, 1, &query->literal_addr);
}

/*
 * 发起异步名称解析（对外主入口）。
 *   query    —— 调用方查询对象；若 query->callback 非空表示上一次查询
 *               尚未完成，本次会被拒绝（打印错误）。
 *   af       —— 期望地址族。
 *   vrf_id   —— 在哪个 VRF / 网络命名空间内解析。
 *   hostname —— 主机名；NULL 直接返回。
 *   callback —— 完成回调。
 * 行为流程：
 *   1. 记录回调到 query，清空 literal_cb。
 *   2. 用 str2sockunion 尝试解析为 IP 字面量；若是，走 resolver_cb_literal
 *      的 0ms 定时器延迟回调（保持异步一致性），直接返回。
 *   3. 否则先 vrf_switch_to_netns 切到目标命名空间，调用 c-ares 发起解析
 *      （新版 ares_getaddrinfo / 旧版 ares_gethostbyname），再
 *      vrf_switchback_to_initial 切回，最后重算超时。
 * 返回 void。注意：真正结果经 ares_address_cb 异步返回；切 VRF 失败会
 *   直接返回（回调不会被触发，调用方需容忍）。
 */
void resolver_resolve(struct resolver_query *query, int af, vrf_id_t vrf_id,
		      const char *hostname,
		      void (*callback)(struct resolver_query *, const char *,
				       int, union sockunion *))
{
	int ret;
#if (ARES_VERSION >= 0x011c00)
	struct ares_addrinfo_hints hints = {
		.ai_flags = 0,
		.ai_family = af,
		.ai_socktype = 0, /* any of SOCK_STREAM or SOCK_DGRAM */
		.ai_protocol = 0  /* any protocol */
	};
#endif

	if (hostname == NULL)
		return;

	if (query->callback != NULL) {
		flog_err(
			EC_LIB_RESOLVER,
			"Trying to resolve '%s', but previous query was not finished yet",
			hostname);
		return;
	}

	query->callback = callback;
	query->literal_cb = NULL;

	ret = str2sockunion(hostname, &query->literal_addr);
	if (ret == 0) {
		if (resolver_debug)
			zlog_debug("[%p] Resolving '%s' (IP literal)",
				   query, hostname);

		/* for consistency with proper name lookup, don't call the
		 * callback immediately; defer to thread loop
		 */
		event_add_timer_msec(state.master, resolver_cb_literal, query,
				     0, &query->literal_cb);
		return;
	}

	if (resolver_debug)
		zlog_debug("[%p] Resolving '%s'", query, hostname);

	ret = vrf_switch_to_netns(vrf_id);
	if (ret < 0) {
		flog_err_sys(EC_LIB_SOCKET, "%s: Can't switch to VRF %u (%s)",
			     __func__, vrf_id, safe_strerror(errno));
		return;
	}

#if (ARES_VERSION >= 0x011c00)
	ares_getaddrinfo(state.channel, hostname, NULL, &hints, ares_address_cb,
			 query);
#else
	ares_gethostbyname(state.channel, hostname, af, ares_address_cb, query);
#endif
	ret = vrf_switchback_to_initial();
	if (ret < 0)
		flog_err_sys(EC_LIB_SOCKET,
			     "%s: Can't switchback from VRF %u (%s)", __func__,
			     vrf_id, safe_strerror(errno));
	resolver_update_timeouts(&state);
}

/*
 * 命令 "debug resolver" / "no debug resolver" 的实现。
 * 参数：argc == 2 表示带 "no"（关闭），否则开启。返回值：CMD_SUCCESS。
 * 副作用：修改全局 resolver_debug。
 */
DEFUN(debug_resolver,
      debug_resolver_cmd,
      "[no] debug resolver",
      NO_STR
      DEBUG_STR
      "Debug DNS resolver actions\n")
{
	resolver_debug = (argc == 2);
	return CMD_SUCCESS;
}

/*
 * 前向声明：把 debug resolver 状态写入配置（show running-config 用）。
 */
static int resolver_config_write_debug(struct vty *vty);
/*
 * resolver 的配置节点定义：节点类型 RESOLVER_DEBUG_NODE，无子提示符，
 * 写配置时回调 resolver_config_write_debug。install_node 时需要它。
 */
static struct cmd_node resolver_debug_node = {
	.name = "resolver debug",
	.node = RESOLVER_DEBUG_NODE,
	.prompt = "",
	.config_write = resolver_config_write_debug,
};

/*
 * 写配置回调：若调试已开启，则输出一行 "debug resolver"。
 *   vty —— 输出目标。
 * 返回值：恒为 1（表示节点参与配置输出）。
 */
static int resolver_config_write_debug(struct vty *vty)
{
	if (resolver_debug)
		vty_out(vty, "debug resolver\n");
	return 1;
}


/*
 * 初始化解析子系统。
 *   tm —— FRR 主事件循环，保存到 state.master。
 * 流程：设置 c-ares 选项（sock_state_cb 回调及其数据、超时 2 秒、重试 3
 *   次），调用 ares_init_options 建立 channel；再注册 "debug resolver"
 *   命令到 CONFIG / ENABLE 节点。
 * 返回 void。副作用：建立全局 state.channel，必须在事件循环就绪后调用。
 */
void resolver_init(struct event_loop *tm)
{
	struct ares_options ares_opts;

	state.master = tm;

	ares_opts = (struct ares_options){
		.sock_state_cb = &ares_socket_cb,
		.sock_state_cb_data = &state,
		.timeout = 2,
		.tries = 3,
	};

	ares_init_options(&state.channel, &ares_opts,
			  ARES_OPT_SOCK_STATE_CB | ARES_OPT_TIMEOUT
				  | ARES_OPT_TRIES);

	install_node(&resolver_debug_node);
	install_element(CONFIG_NODE, &debug_resolver_cmd);
	install_element(ENABLE_NODE, &debug_resolver_cmd);
}

/*
 * 关闭解析子系统：销毁 c-ares channel，释放相关资源。
 * 无参数、无返回值。注意：调用后不应再发起解析；本函数是文件最后一个
 *   函数。
 */
void resolver_terminate(void)
{
	ares_destroy(state.channel);
}
