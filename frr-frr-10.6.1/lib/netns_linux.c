// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NS functions.
 * Copyright (C) 2014 6WIND S.A.
 */

/*
 * ============================================================================
 * netns_linux.c —— Linux network namespace（netns）后端实现
 * ============================================================================
 *
 * 职责：把 FRR 的 NS 抽象落到 Linux 内核 netns 上。做法是先用
 *   open("/var/run/netns/<name>") 取得 netns 的 fd，再调用
 *   setns(fd, CLONE_NEWNET) 切换进程当前的网络命名空间，使随后创建的
 *   socket 落在该 netns 内。非 Linux 平台的空实现见 netns_other.c。
 *
 * 核心数据结构：
 *   struct ns            一个 network namespace 上下文（见 ns.h）
 *   struct ns_head       按 ns_id 索引全部 NS 的红黑树
 *   struct ns_master     四个全局钩子 new / delete / enable / disable
 *   struct ns_map_nsid   内部 ns_id 与对外 vrf_id 的映射表项
 *
 * 关键全局变量：
 *   ns_tree              全部 NS 的红黑树
 *   default_ns           默认 NS（启动即存在）
 *   ns_current_ns_fd     进程“当前切入”的 netns fd（-1 表示未切入）
 *   ns_default_ns_fd     默认 netns 的 fd，用于切回
 *   ns_debug             调试开关（当前恒为 0）
 *
 * 与其他模块关系：
 *   ns.h     对外接口声明与 struct ns / ns_id_t
 *   vrf.c    以 NS 为后端；VRF 的启用/禁用会驱动这里的 enable/disable
 *   log.h    出错时用 flog_err / flog_warn / zlog_info 记录
 *
 * 阅读顺序：
 *   ns_compare -> ns_get_created_internal -> ns_enable_internal ->
 *   ns_init / ns_init_management -> ns_socket ->
 *   ns_switch_to_netns / ns_switchback_to_initial -> ns_terminate
 *
 * 易踩的坑：
 *   1) NS 必须 enable 之后才可用；未 enable 时 ns->fd 为 -1 或 -2；
 *   2) 无 netns 能力时只有默认 NS 视为可用（其 fd == -2）；
 *   3) ns_socket() 切进目标 netns 建完 socket 后会自动切回默认 netns；
 *   4) ns_enable_internal 先置 errno = -ENOTSUP 再打日志，该 errno 并非真正的系统错误码。
 * ============================================================================
 */

#include <zebra.h>
#include <fcntl.h>

#ifdef HAVE_NETNS
#undef _GNU_SOURCE
#define _GNU_SOURCE

#include <sched.h>
#endif

/* for basename */
#include <libgen.h>

#include "if.h"
#include "ns.h"
#include "log.h"
#include "memory.h"
#include "command.h"
#include "vty.h"
#include "vrf.h"
#include "lib_errors.h"

// DEFINE_MTYPE_STATIC(LIB, NS)      —— 分配 struct ns / ns_map_nsid 用的私有内存类型
// DEFINE_MTYPE_STATIC(LIB, NS_NAME) —— 分配 NS 名字字符串用的私有内存类型
DEFINE_MTYPE_STATIC(LIB, NS, "NetNS Context");
DEFINE_MTYPE_STATIC(LIB, NS_NAME, "NetNS Name");

// 前置声明：ns_compare 供 RB 树比较用；ns_lookup_name_internal 供本文件内按名查找。
static inline int ns_compare(const struct ns *ns, const struct ns *ns2);
static struct ns *ns_lookup_name_internal(const char *name);

// RB_GENERATE(ns_head, ns, entry, ns_compare) —— 为 ns_head 红黑树生成
// 插入 / 查找 / 删除等操作，排序键为 struct ns 的 ns_id（比较函数 ns_compare）。
RB_GENERATE(ns_head, ns, entry, ns_compare)

// ns_tree —— 全部已创建 NS 的红黑树（RB 头），按 ns_id 排序。
static struct ns_head ns_tree = RB_INITIALIZER(&ns_tree);

// default_ns       —— 默认 NS（FRR 启动即存在，永不删除）。
// ns_current_ns_fd —— 进程当前“切入”的 netns fd；-1 表示尚未切入任何 netns。
// ns_default_ns_fd —— 默认 netns 的 fd，用于 setns 切回初始命名空间。
static struct ns *default_ns;
static int ns_current_ns_fd;
static int ns_default_ns_fd;

// ns_debug —— 调试开关；当前代码在 ns_init() 里恒置 0，调试日志默认关闭。
static int ns_debug;

// struct ns_map_nsid —— 内部 ns_id 与“对外 vrf_id”之间的映射表项。
// id_entry 为红黑树挂链节点；ns_id 为内部编号；ns_id_external 为对外编号。
struct ns_map_nsid {
	RB_ENTRY(ns_map_nsid) id_entry;
	ns_id_t ns_id_external;
	ns_id_t ns_id;
};

// ns_map_compare —— ns_map_nsid 红黑树的比较函数，按内部 ns_id 升序比较。
static inline int ns_map_compare(const struct ns_map_nsid *a,
				   const struct ns_map_nsid *b)
{
	return (a->ns_id - b->ns_id);
}

// 为 ns_map_nsid 定义红黑树头类型 ns_map_nsid_head 并生成其操作函数；
// ns_map_nsid_list 是该树全局头，保存全部 ns_id 与 vrf_id 的映射。
RB_HEAD(ns_map_nsid_head, ns_map_nsid);
RB_PROTOTYPE(ns_map_nsid_head, ns_map_nsid, id_entry, ns_map_compare);
RB_GENERATE(ns_map_nsid_head, ns_map_nsid, id_entry, ns_map_compare);
static struct ns_map_nsid_head ns_map_nsid_list =
		RB_INITIALIZER(&ns_map_nsid_list);

// ns_id_external_numbering —— 对外 vrf_id 的自增计数器（默认 vrf 为 0，故从 0 起）。
static ns_id_t ns_id_external_numbering;


// 兼容老内核头文件：若未定义 CLONE_NEWNET，则手动补上其常量值
// （0x40000000，表示新建 network namespace）。
#ifndef CLONE_NEWNET
#define CLONE_NEWNET 0x40000000
/* New network namespace (lo, device, names sockets, etc) */
#endif

// 若系统未提供 HAVE_SETNS，则用一个内联 setns() 兜底：有 __NR_setns 时走
// syscall 直调，否则置 errno = EINVAL 并返回 -1。
#ifndef HAVE_SETNS
static inline int setns(int fd, int nstype)
{
#ifdef __NR_setns
	return syscall(__NR_setns, fd, nstype);
#else
	errno = EINVAL;
	return -1;
#endif
}
#endif /* !HAVE_SETNS */

#ifdef HAVE_NETNS
// have_netns_enabled —— 缓存“本机是否支持 netns”的探测结果：
// -1 未探测，1 支持，0 不支持。
static int have_netns_enabled = -1;
#endif /* HAVE_NETNS */

// have_netns —— 探测并缓存本机是否具备 netns 能力：尝试打开默认 netns 路径
// NS_DEFAULT_NAME，能打开即视为支持。结果缓存在 have_netns_enabled 中，
// 后续调用直接返回缓存值；未编译 HAVE_NETNS 时恒返回 0。
static int have_netns(void)
{
#ifdef HAVE_NETNS
	if (have_netns_enabled < 0) {
		int fd = open(NS_DEFAULT_NAME, O_RDONLY);

		if (fd < 0)
			have_netns_enabled = 0;
		else {
			have_netns_enabled = 1;
			close(fd);
		}
	}
	return have_netns_enabled;
#else
	return 0;
#endif
}

/* Holding NS hooks  */
// ns_master —— 全局 NS 钩子集合：new / delete / enable / disable 四个回调，
// 由 ns_add_hook() 注册，供 vrf 等上层在 NS 生命周期事件中分配 / 释放资源。
static struct ns_master {
	int (*ns_new_hook)(struct ns *ns);
	int (*ns_delete_hook)(struct ns *ns);
	int (*ns_enable_hook)(struct ns *ns);
	int (*ns_disable_hook)(struct ns *ns);
} ns_master = {
	0,
};

// 前置声明 ns_is_enabled()，供下方 ns_enable_internal() 等调用。
static int ns_is_enabled(struct ns *ns);

// ns_compare —— struct ns 红黑树的比较函数，按 ns_id 升序比较。
static inline int ns_compare(const struct ns *a, const struct ns *b)
{
	return (a->ns_id - b->ns_id);
}

/* Look up a NS by identifier. */
// ns_lookup_internal —— 按内部 ns_id 在 ns_tree 中查找 NS，找不到返回 NULL。
static struct ns *ns_lookup_internal(ns_id_t ns_id)
{
	struct ns ns;

	ns.ns_id = ns_id;
	return RB_FIND(ns_head, &ns_tree, &ns);
}

/* Look up a NS by name */
// ns_lookup_name_internal —— 按名字线性遍历 ns_tree 查找 NS，名字匹配时
// 返回该 NS，否则返回 NULL。
static struct ns *ns_lookup_name_internal(const char *name)
{
	struct ns *ns = NULL;

	RB_FOREACH (ns, ns_head, &ns_tree) {
		if (ns->name != NULL) {
			if (strcmp(name, ns->name) == 0)
				return ns;
		}
	}
	return NULL;
}

// ns_get_created_internal —— 取得或创建一个 NS：
//   1) 先按 ns_id 或 name 尝试查找已有 NS；
//   2) 找不到则分配并插入 ns_tree（fd 初始化为 -1，标记 created）；
//   3) 若传入的 ns_id 与已有不同，则摘除后按新 ns_id 重新插入；
//   4) 仅对“新建”的 NS 触发调试日志与 ns_new_hook 回调。
static struct ns *ns_get_created_internal(struct ns *ns, char *name,
					  ns_id_t ns_id)
{
	int created = 0;
	/*
	 * Initialize interfaces.
	 */
	if (!ns && !name && ns_id != NS_UNKNOWN)
		ns = ns_lookup_internal(ns_id);
	if (!ns && name)
		ns = ns_lookup_name_internal(name);
	if (!ns) {
		ns = XCALLOC(MTYPE_NS, sizeof(struct ns));
		ns->ns_id = ns_id;
		if (name)
			ns->name = XSTRDUP(MTYPE_NS_NAME, name);
		ns->fd = -1;
		RB_INSERT(ns_head, &ns_tree, ns);
		created = 1;
	}
	if (ns_id != ns->ns_id) {
		RB_REMOVE(ns_head, &ns_tree, ns);
		ns->ns_id = ns_id;
		RB_INSERT(ns_head, &ns_tree, ns);
	}
	if (!created)
		return ns;
	if (ns_debug) {
		if (ns->ns_id != NS_UNKNOWN)
			zlog_info("NS %u is created.", ns->ns_id);
		else
			zlog_info("NS %s is created.", ns->name);
	}
	if (ns_master.ns_new_hook)
		(*ns_master.ns_new_hook)(ns);
	return ns;
}

/*
 * Enable a NS - that is, let the NS be ready to use.
 * The NS_ENABLE_HOOK callback will be called to inform
 * that they can allocate resources in this NS.
 *
 * RETURN: 1 - enabled successfully; otherwise, 0.
 */
// ns_enable_internal —— 启用一个 NS，使其可用：
//   - 若无 netns 能力，置 fd = -2 并置 errno = -ENOTSUP（仅默认 NS 可用）；
//   - 否则 open(ns->name) 打开对应 netns 得到 fd；
//   - 成功后回调 func(ns_id, vrf_ctxt)，再触发 ns_enable_hook。
// 返回 1 表示启用成功，0 表示失败。
static int ns_enable_internal(struct ns *ns, void (*func)(ns_id_t, void *))
{
	if (!ns_is_enabled(ns)) {
		if (have_netns()) {
			ns->fd = open(ns->name, O_RDONLY);
		} else {
			ns->fd = -2;
			/* Remember ns_enable_hook has been called */
			errno = -ENOTSUP;
		}

		if (!ns_is_enabled(ns)) {
			flog_err_sys(EC_LIB_SYSTEM_CALL,
				     "Can not enable NS %u: %s!", ns->ns_id,
				     safe_strerror(errno));
			return 0;
		}

		/* Non default NS. leave */
		if (ns->ns_id == NS_UNKNOWN) {
			flog_err(EC_LIB_NS,
				 "Can not enable NS %s %u: Invalid NSID",
				 ns->name, ns->ns_id);
			return 0;
		}
		if (func)
			func(ns->ns_id, (void *)ns->vrf_ctxt);
		if (ns_debug) {
			if (have_netns())
				zlog_info("NS %u is associated with NETNS %s.",
					  ns->ns_id, ns->name);
			zlog_info("NS %u is enabled.", ns->ns_id);
		}
		/* zebra first receives NS enable event,
		 * then VRF enable event
		 */
		if (ns_master.ns_enable_hook)
			(*ns_master.ns_enable_hook)(ns);
	}

	return 1;
}

/*
 * Check whether the NS is enabled - that is, whether the NS
 * is ready to allocate resources. Currently there's only one
 * type of resource: socket.
 */
// ns_is_enabled —— 判断 NS 是否已启用（即可分配资源，当前资源仅 socket）。
//   有 netns 能力时：NS 非空且 fd >= 0 即视为启用；
//   无 netns 能力时：仅默认 NS（fd == -2 且 ns_id == NS_DEFAULT）算启用。
static int ns_is_enabled(struct ns *ns)
{
	if (have_netns())
		return ns && ns->fd >= 0;
	else
		return ns && ns->fd == -2 && ns->ns_id == NS_DEFAULT;
}

/*
 * Disable a NS - that is, let the NS be unusable.
 * The NS_DELETE_HOOK callback will be called to inform
 * that they must release the resources in the NS.
 */
// ns_disable_internal —— 禁用一个 NS，使其不可用：先触发 ns_disable_hook
// 通知上层释放资源，再关闭 netns fd（若有效）并置为 -1。
static void ns_disable_internal(struct ns *ns)
{
	if (ns_is_enabled(ns)) {
		if (ns_debug)
			zlog_info("NS %u is to be disabled.", ns->ns_id);

		if (ns_master.ns_disable_hook)
			(*ns_master.ns_disable_hook)(ns);

		if (have_netns() && ns->fd >= 0)
			close(ns->fd);

		ns->fd = -1;
	}
}

/* VRF list existence check by name. */
// ns_map_nsid_lookup_by_nsid —— 在 ns_map_nsid_list 中按内部 ns_id 查找
// 映射表项，供 ns_map_nsid_with_external() 使用。
static struct ns_map_nsid *ns_map_nsid_lookup_by_nsid(ns_id_t ns_id)
{
	struct ns_map_nsid ns_map;

	ns_map.ns_id = ns_id;
	return RB_FIND(ns_map_nsid_head, &ns_map_nsid_list, &ns_map);
}

// ns_map_nsid_with_external —— 维护 ns_id 与对外 vrf_id 的映射：
//   map 为 false 时，若已有映射则摘除并返回其对外编号（解除映射）；
//   map 为 true 时，已有映射直接返回，否则新建一项，对外编号取自
//   自增计数器 ns_id_external_numbering。
ns_id_t ns_map_nsid_with_external(ns_id_t ns_id, bool map)
{
	struct ns_map_nsid *ns_map;
	vrf_id_t ns_id_external;

	ns_map = ns_map_nsid_lookup_by_nsid(ns_id);
	if (ns_map && !map) {
		ns_id_external = ns_map->ns_id_external;
		RB_REMOVE(ns_map_nsid_head, &ns_map_nsid_list, ns_map);
		return ns_id_external;
	}
	if (ns_map)
		return ns_map->ns_id_external;
	ns_map = XCALLOC(MTYPE_NS, sizeof(struct ns_map_nsid));
	/* increase vrf_id
	 * default vrf is the first one : 0
	 */
	ns_map->ns_id_external = ns_id_external_numbering++;
	ns_map->ns_id = ns_id;
	RB_INSERT(ns_map_nsid_head, &ns_map_nsid_list, ns_map);
	return ns_map->ns_id_external;
}

// ns_get_created —— 对外的“取得或创建 NS”接口，转发到内部实现。
struct ns *ns_get_created(struct ns *ns, char *name, ns_id_t ns_id)
{
	return ns_get_created_internal(ns, name, ns_id);
}

// ns_have_netns —— 对外暴露本机是否支持 netns（转发 have_netns()）。
int ns_have_netns(void)
{
	return have_netns();
}

/* Delete a NS. This is called in ns_terminate(). */
// ns_delete —— 删除一个 NS：先 disable 释放资源，再触发 ns_delete_hook，
// 最后从 ns_tree 摘除并释放名字字符串与结构体本身。主要调度入口是 ns_terminate()。
void ns_delete(struct ns *ns)
{
	if (ns_debug)
		zlog_info("NS %u is to be deleted.", ns->ns_id);

	ns_disable(ns);

	if (ns_master.ns_delete_hook)
		(*ns_master.ns_delete_hook)(ns);

	/*
	 * I'm not entirely sure if the vrf->iflist
	 * needs to be moved into here or not.
	 */
	// if_terminate (&ns->iflist);

	RB_REMOVE(ns_head, &ns_tree, ns);
	XFREE(MTYPE_NS_NAME, ns->name);

	XFREE(MTYPE_NS, ns);
}

/* Look up the data pointer of the specified VRF. */
// ns_info_lookup —— 按 ns_id 查找 NS 并返回其 info 字段（自定义上下文指针），
// 找不到返回 NULL。
void *ns_info_lookup(ns_id_t ns_id)
{
	struct ns *ns = ns_lookup_internal(ns_id);

	return ns ? ns->info : NULL;
}

/* Look up a NS by name */
// ns_lookup_name —— 对外按名字查找 NS，转发到内部实现。
struct ns *ns_lookup_name(const char *name)
{
	return ns_lookup_name_internal(name);
}

// ns_enable —— 对外启用 NS 接口，转发到 ns_enable_internal()。
int ns_enable(struct ns *ns, void (*func)(ns_id_t, void *))
{
	return ns_enable_internal(ns, func);
}

// ns_disable —— 对外禁用 NS 接口，转发到 ns_disable_internal()。
void ns_disable(struct ns *ns)
{
	ns_disable_internal(ns);
}

// ns_lookup —— 对外按 ns_id 查找 NS，转发到内部实现。
struct ns *ns_lookup(ns_id_t ns_id)
{
	return ns_lookup_internal(ns_id);
}

// ns_walk_func —— 遍历 ns_tree 中每个 NS 并调用回调 func；回调返回
// NS_WALK_STOP 时提前结束遍历。param_in / param_out 用于透传数据。
void ns_walk_func(int (*func)(struct ns *,
			      void *param_in,
			      void **param_out),
		  void *param_in,
		  void **param_out)
{
	struct ns *ns = NULL;
	int ret;

	RB_FOREACH (ns, ns_head, &ns_tree) {
		ret = func(ns, param_in, param_out);
		if (ret == NS_WALK_STOP)
			return;
	}
}

// ns_get_name —— 返回 NS 的名字（ns 为空时返回 NULL）。
const char *ns_get_name(struct ns *ns)
{
	if (!ns)
		return NULL;
	return ns->name;
}

/* Add a NS hook. Please add hooks before calling ns_init(). */
// ns_add_hook —— 注册 NS 生命周期钩子：type 取 NS_NEW_HOOK / NS_DELETE_HOOK /
// NS_ENABLE_HOOK / NS_DISABLE_HOOK 之一，分别写入 ns_master 对应字段。
// 注意：必须在 ns_init() 之前调用。
void ns_add_hook(int type, int (*func)(struct ns *))
{
	switch (type) {
	case NS_NEW_HOOK:
		ns_master.ns_new_hook = func;
		break;
	case NS_DELETE_HOOK:
		ns_master.ns_delete_hook = func;
		break;
	case NS_ENABLE_HOOK:
		ns_master.ns_enable_hook = func;
		break;
	case NS_DISABLE_HOOK:
		ns_master.ns_disable_hook = func;
		break;
	default:
		break;
	}
}

/*
 * NS realization with NETNS
 */

// ns_netns_pathname —— 把用户输入的 NS 名解析成实际路径：
//   - 绝对路径直接 realpath；
//   - 相对名则拼成 NS_RUN_DIR/name 再 realpath；
//   - 解析失败或 basename 超过 NS_NAMSIZ 时报错并返回 NULL。
// vty 非空时错误经 vty_out 输出，否则走 flog_warn。
char *ns_netns_pathname(struct vty *vty, const char *name)
{
	static char pathname[PATH_MAX];
	char *result;
	char *check_base;

	if (name[0] == '/') /* absolute pathname */
		result = realpath(name, pathname);
	else {
		/* relevant pathname */
		char tmp_name[PATH_MAX];

		snprintf(tmp_name, sizeof(tmp_name), "%s/%s", NS_RUN_DIR, name);
		result = realpath(tmp_name, pathname);
	}

	if (!result) {
		if (vty)
			vty_out(vty, "Invalid pathname for %s: %s\n",
				pathname,
				safe_strerror(errno));
		else
			flog_warn(EC_LIB_LINUX_NS,
				  "Invalid pathname for %s: %s", pathname,
				  safe_strerror(errno));
		return NULL;
	}
	check_base = basename(pathname);
	if (check_base != NULL && strlen(check_base) + 1 > NS_NAMSIZ) {
		if (vty)
			vty_out(vty, "NS name (%s) invalid: too long (>%d)\n",
				check_base, NS_NAMSIZ - 1);
		else
			flog_warn(EC_LIB_LINUX_NS,
				  "NS name (%s) invalid: too long (>%d)",
				  check_base, NS_NAMSIZ - 1);
		return NULL;
	}
	return pathname;
}

// ns_init —— NS 模块初始化（借助静态标志实现幂等，只做一次）：
//   打开默认 netns 路径并缓存 ns_default_ns_fd，初始化 ns_current_ns_fd = -1；
//   若本机无 netns 能力则 ns_default_ns_fd = -1 且 default_ns 置 NULL。
void ns_init(void)
{
	static int ns_initialised;

	ns_debug = 0;
	/* silently return as initialisation done */
	if (ns_initialised == 1)
		return;
	errno = 0;
	if (have_netns())
		ns_default_ns_fd = open(NS_DEFAULT_NAME, O_RDONLY);
	else {
		ns_default_ns_fd = -1;
		default_ns = NULL;
	}
	ns_current_ns_fd = -1;
	ns_initialised = 1;
}

/* Initialize NS module. */
// ns_init_management —— 初始化 NS 管理：
//   调用 ns_init() 后创建默认 NS（default_ns），打开并保存其 fd，
//   记录 internal_ns_id 与默认名字，最后 enable 默认 NS。
//   任一步失败（创建或 enable）即 flog_err 并 exit(1)。
void ns_init_management(ns_id_t default_ns_id, ns_id_t internal_ns)
{
	int fd;

	ns_init();
	default_ns = ns_get_created_internal(NULL, NULL, default_ns_id);
	if (!default_ns) {
		flog_err(EC_LIB_NS, "%s: failed to create the default NS!",
			 __func__);
		exit(1);
	}
	if (have_netns()) {
		fd = open(NS_DEFAULT_NAME, O_RDONLY);
		default_ns->fd = fd;
	}
	default_ns->internal_ns_id = internal_ns;

	/* Set the default NS name. */
	default_ns->name = XSTRDUP(MTYPE_NS_NAME, NS_DEFAULT_NAME);
	if (ns_debug)
		zlog_info("%s: default NSID is %u", __func__,
			  default_ns->ns_id);

	/* Enable the default NS. */
	if (!ns_enable(default_ns, NULL)) {
		flog_err(EC_LIB_NS, "%s: failed to enable the default NS!",
			 __func__);
		exit(1);
	}
}

/* Terminate NS module. */
// ns_terminate —— 终止 NS 模块：反复删除 ns_tree 中的 NS 直到清空，
// 并释放 ns_map_nsid_list 中所有映射表项。
void ns_terminate(void)
{
	struct ns *ns;
	struct ns_map_nsid *ns_map;

	while (!RB_EMPTY(ns_head, &ns_tree)) {
		ns = RB_ROOT(ns_head, &ns_tree);

		ns_delete(ns);
	}

	while (!RB_EMPTY(ns_map_nsid_head, &ns_map_nsid_list)) {
		ns_map = RB_ROOT(ns_map_nsid_head, &ns_map_nsid_list);
		RB_REMOVE(ns_map_nsid_head, &ns_map_nsid_list, ns_map);
		XFREE(MTYPE_NS, ns_map);
	}
}

// ns_switch_to_netns —— 把当前进程切进名字为 name 的 netns：
//   open(name) 得到 fd，setns(fd, CLONE_NEWNET) 完成切换，
//   记录 ns_current_ns_fd 后关闭 fd。name 为空或无默认 netns 时返回 -1。
int ns_switch_to_netns(const char *name)
{
	int ret;
	int fd;

	if (name == NULL)
		return -1;
	if (ns_default_ns_fd == -1)
		return -1;
	fd = open(name, O_RDONLY);
	if (fd == -1) {
		errno = EINVAL;
		return -1;
	}
	ret = setns(fd, CLONE_NEWNET);
	ns_current_ns_fd = fd;
	close(fd);
	return ret;
}

/* returns 1 if switch() was not called before
 * return status of setns() otherwise
 */
// ns_switchback_to_initial —— 从当前 netns 切回默认（初始）netns：
//   若此前切入过（ns_current_ns_fd != -1），setns 回 ns_default_ns_fd 并
//   重置 ns_current_ns_fd；返回 setns 结果。若从未切入则返回 1（静默忽略）。
int ns_switchback_to_initial(void)
{
	if (ns_current_ns_fd != -1 && ns_default_ns_fd != -1) {
		int ret;

		ret = setns(ns_default_ns_fd, CLONE_NEWNET);
		ns_current_ns_fd = -1;
		return ret;
	}
	/* silently ignore if setns() is not called */
	return 1;
}

/* Create a socket for the NS. */
// ns_socket —— 在指定 NS 中创建 socket：
//   - NS 不存在或未启用时置 errno = EINVAL 返回 -1；
//   - 有 netns 能力且目标非默认 NS 时，先 setns 进目标 netns，创建 socket
//     后再 setns 切回默认 netns，并把 ns_current_ns_fd 记为 ns_id；
//   - 无 netns 能力时直接创建 socket。
int ns_socket(int domain, int type, int protocol, ns_id_t ns_id)
{
	struct ns *ns = ns_lookup(ns_id);
	int ret;

	if (!ns || !ns_is_enabled(ns)) {
		errno = EINVAL;
		return -1;
	}
	if (have_netns()) {
		ret = (ns_id != NS_DEFAULT) ? setns(ns->fd, CLONE_NEWNET) : 0;
		if (ret >= 0) {
			ret = socket(domain, type, protocol);
			if (ns_id != NS_DEFAULT) {
				setns(ns_lookup(NS_DEFAULT)->fd, CLONE_NEWNET);
				ns_current_ns_fd = ns_id;
			}
		}
	} else
		ret = socket(domain, type, protocol);

	return ret;
}

/* if relative link_nsid matches default netns,
 * then return default absolute netns value
 * otherwise, return NS_UNKNOWN
 */
// ns_id_get_absolute —— 若 ns_id_reference 对应 NS 的 relative_default_ns
// 恰等于 link_nsid，则返回默认 NS 的绝对 ns_id；否则返回 NS_UNKNOWN。
ns_id_t ns_id_get_absolute(ns_id_t ns_id_reference, ns_id_t link_nsid)
{
	struct ns *ns;

	ns = ns_lookup(ns_id_reference);
	if (!ns)
		return NS_UNKNOWN;

	if (ns->relative_default_ns != link_nsid)
		return NS_UNKNOWN;

	ns = ns_get_default();
	assert(ns);
	return ns->ns_id;
}

// ns_get_default —— 返回默认 NS 指针。
struct ns *ns_get_default(void)
{
	return default_ns;
}
