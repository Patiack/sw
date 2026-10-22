// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NetNS backend for non Linux systems
 * Copyright (C) 2018 6WIND S.A.
 */


/*
 * ============================================================================
 * netns_other.c —— 非 Linux 平台上 NS 后端的空实现（OPEN_BSD）
 * ============================================================================
 * 职责：当操作系统不是 GNU/Linux 而是 OPEN_BSD 时编译本文件，为 ns.h 中
 *   声明的全部 NS 接口提供空（stub）实现，保证跨平台链接不缺符号。
 *   所有函数都不做实际工作：查询类返回 NULL / 0 / NS_UNKNOWN，动作类为空。
 *
 * 核心数据结构：
 *   ns_tree    一个空的 RB 树头（仍按 ns.h 约定存在，与 Linux 版同名）
 *   ns_compare 仅用于满足 RB_GENERATE 的排序需求
 *
 * 与其他模块关系：
 *   ns.h 中所有 NS 接口在此都有对应桩函数；Linux 对应实现见 netns_linux.c。
 *   若要在 BSD 上真正实现 netns，需要另写后端，而非此空壳。
 *
 * 阅读顺序：
 *   ns_compare -> 各桩函数（顺序与 netns_linux.c 基本一致）。
 *
 * 易踩的坑：
 *   1) 本文件整体被 #if !defined(GNU_LINUX) && defined(OPEN_BSD) 包裹，
 *      非 OpenBSD 平台不会编译它；
 *   2) 返回值语义须与 ns.h 声明一致，例如 ns_have_netns() 返回 0 表示
 *      本平台无 netns 支持，调用方据此走默认 NS 路径；
 *   3) 这里 ns_walk_func / ns_enable 的签名与 Linux 版略有出入（参数更少），
 *      以本平台编译需求为准。
 * ============================================================================
 */

// 仅当目标平台不是 GNU/Linux 且是 OPEN_BSD 时才编译本文件；
// 其他平台的 NS 后端另有实现（Linux 见 netns_linux.c）。
#if !defined(GNU_LINUX) && defined(OPEN_BSD)
/* OPEN_BSD */

#include <zebra.h>
#include "ns.h"
#include "log.h"
#include "memory.h"

// 前置声明 ns_compare，供下方 RB_GENERATE 生成空红黑树操作使用。
static inline int ns_compare(const struct ns *ns, const struct ns *ns2);

// 与 Linux 版同名：为 ns_head 红黑树生成操作函数（此处并不真正使用）。
RB_GENERATE(ns_head, ns, entry, ns_compare)

// ns_tree —— 空的 NS 红黑树头，仅为保持与 Linux 版一致的结构占位。
static struct ns_head ns_tree = RB_INITIALIZER(&ns_tree);

// ns_compare —— 按 ns_id 比较，满足红黑树排序要求（本平台不实际使用）。
static inline int ns_compare(const struct ns *a, const struct ns *b)
{
	return (a->ns_id - b->ns_id);
}

// ns_terminate —— 空实现：非 Linux 平台无 netns 可清理。
void ns_terminate(void)
{
}

/* API to initialize NETNS managerment
 * parameter is the default ns_id
 */
// ns_init_management —— 空实现：本平台无 NS 管理需要初始化；
// 参数 ns_id 仅用于保持与 Linux 版接口一致。
void ns_init_management(ns_id_t ns_id)
{
}

/*
 * NS utilities
 */

/* Create a socket serving for the given NS
 */
// ns_socket —— 恒返回 -1，表示本平台无法在指定 NS 中创建 socket。
int ns_socket(int domain, int type, int protocol, ns_id_t ns_id)
{
	return -1;
}

/* return the path of the NETNS */
// ns_netns_pathname —— 恒返回 NULL，表示本平台无 netns 路径概念。
char *ns_netns_pathname(struct vty *vty, const char *name)
{
	return NULL;
}

/* Parse and execute a function on all the NETNS */
// ns_walk_func —— 空实现：本平台无 NS 可遍历。
void ns_walk_func(int (*func)(struct ns *))
{
}

/* API to get the NETNS name, from the ns pointer */
// ns_get_name —— 恒返回 NULL：本平台不存在具名 NS。
const char *ns_get_name(struct ns *ns)
{
	return NULL;
}

/* only called from vrf ( when removing netns from vrf)
 * or at VRF termination
 */
// ns_delete —— 空实现：本平台无 NS 需要删除。
void ns_delete(struct ns *ns)
{
}

/* return > 0 if netns is available
 * called by VRF to check netns backend is available for VRF
 */
// ns_have_netns —— 恒返回 0，表示本平台不支持 netns；调用方据此走默认 NS。
int ns_have_netns(void)
{
	return 0;
}

/* API to get context information of a NS */
// ns_info_lookup —— 恒返回 NULL：本平台无 NS 上下文可查。
void *ns_info_lookup(ns_id_t ns_id)
{
	return NULL;
}

/*
 * NS init routine
 * should be called from backendx
 */
// ns_init —— 空实现：本平台无需初始化 NS 子系统。
void ns_init(void)
{
}

/* API that can be used to change from NS */
// ns_switchback_to_initial —— 恒返回 0，表示本平台无需 / 无法切回初始命名空间。
int ns_switchback_to_initial(void)
{
	return 0;
}
// ns_switch_to_netns —— 恒返回 0，表示本平台不支持切入 netns。
int ns_switch_to_netns(const char *netns_name)
{
	return 0;
}

/*
 * NS handling routines.
 * called by modules that use NS backend
 */

/* API to search for already present NETNS */
// ns_lookup —— 恒返回 NULL：本平台无 NS 可查。
struct ns *ns_lookup(ns_id_t ns_id)
{
	return NULL;
}

// ns_lookup_name —— 恒返回 NULL：本平台无具名 NS 可查。
struct ns *ns_lookup_name(const char *name)
{
	return NULL;
}

/* API to handle NS : creation, enable, disable
 * for enable, a callback function is passed as parameter
 * the callback belongs to the module that uses NS as backend
 * upon enabling the NETNS, the upper layer is informed
 */
// ns_enable —— 恒返回 0，表示本平台无法启用 NS。
int ns_enable(struct ns *ns, int (*func)(ns_id_t, void *))
{
	return 0;
}

// ns_map_nsid_with_external —— 恒返回 NS_UNKNOWN：本平台无 ns_id 映射。
ns_id_t ns_map_nsid_with_external(ns_id_t ns_id, bool maporunmap)
{
	return NS_UNKNOWN;
}

// ns_get_created —— 恒返回 NULL：本平台不创建 NS。
struct ns *ns_get_created(struct ns *ns, char *name, ns_id_t ns_id)
{
	return NULL;
}

// ns_disable —— 空实现：本平台无 NS 需要禁用。
void ns_disable(struct ns *ns)
{
}

// 结束 OPEN_BSD 平台分支：以上均为本平台上的空（stub）实现。
#endif /* !GNU_LINUX */
