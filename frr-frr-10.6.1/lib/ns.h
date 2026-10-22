// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NS related header.
 * Copyright (C) 2014 6WIND S.A.
 */

#ifndef _ZEBRA_NS_H
#define _ZEBRA_NS_H

/* ============================================================================
 * 【文件导读】ns.h —— 网络命名空间(NETNS)模块的头文件
 * ============================================================================
 * 【这个文件干什么】声明 FRR 对 Linux 网络命名空间(netns)的抽象与管理接口。
 * netns 是“VRF 后端”的一种：当 FRR 以 netns 模式运行时，每个 VRF 对应一个
 * 独立网络命名空间，接口、路由表、socket 都在该命名空间内彼此隔离。
 *
 * 【核心数据结构 struct ns】代表一个已登记的网络命名空间：
 *   ns_id                —— 外部可见 ID(常与 vector 索引一致)；
 *   internal_ns_id       —— 内部映射 ID；
 *   relative_default_ns  —— 默认 netns 的相对 NSID；
 *   name                 —— 名字；
 *   fd                   —— 打开该 netns 得到的文件描述符(用于 setns 切换)；
 *   iflist               —— 该命名空间下的接口链表；
 *   vrf_ctxt             —— 回指所属 VRF；
 *   info                 —— 使用者自定义数据。
 * 所有 ns 通过 RB 树(ns_head)按 ns_id 索引管理。
 *
 * 【关键 API 分类】
 *   初始化/销毁：ns_init / ns_terminate / ns_init_management
 *   查找：ns_lookup / ns_lookup_name / ns_info_lookup
 *   生命周期：ns_get_created / ns_enable / ns_disable / ns_delete
 *   切换：ns_switch_to_netns / ns_switchback_to_initial
 *   工具：ns_socket / ns_netns_pathname / ns_walk_func / ns_have_netns /
 *         ns_map_nsid_with_external
 *   钩子：ns_add_hook(NS_NEW / DELETE / ENABLE / DISABLE_HOOK)
 *
 * 【与其他模块的关系】本头文件只声明，实现见 ns.c(按平台后端区分)；vrf.h 依
 * 赖它来承载 VRF 的 netns 后端。
 * 【阅读顺序】ns_id_t 与宏 → struct ns → 钩子 → 各 API 声明。
 * ============================================================================
 */
#include "openbsd-tree.h"
#include "linklist.h"
#include "vty.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 【类型别名】网络命名空间的 ID 类型：32 位无符号整数。 */
typedef uint32_t ns_id_t;

/* 【宏】保留的特殊 NS ID，表示“未知/无效”的命名空间。 */
/* the default NS ID */
#define NS_UNKNOWN UINT32_MAX

/* 【宏】Linux 下网络命名空间的挂载目录，netns 文件通常位于该目录下。 */
/* Default netns directory (Linux) */
#define NS_RUN_DIR         "/var/run/netns"

/* 【宏】默认命名空间的名字或路径：支持 netns 时指向 /proc/self/ns/net，
 * 否则退化为字符串 "default-netns"。 */
#ifdef HAVE_NETNS
#define NS_DEFAULT_NAME    "/proc/self/ns/net"
#else  /* !HAVE_NETNS */
#define NS_DEFAULT_NAME    "default-netns"
#endif /* HAVE_NETNS */

/* 【核心结构】描述一个网络命名空间及其关联的接口、VRF 上下文与用户数据。
 * 成员含义见各字段上方注释。 */
struct ns {
	/* 红黑树链接字段：把本 ns 挂进全局 ns_head 树。 */
	RB_ENTRY(ns) entry;

	/* 外部标识：网络命名空间的 NSID，通常与 vector 索引一致。 */
	/* Identifier, same as the vector index */
	ns_id_t ns_id;

	/* 内部标识：映射到内核 NSID 值上，用于后端交互。 */
	/* Identifier, mapped on the NSID value */
	ns_id_t internal_ns_id;

	/* 默认 netns 的 NSID 在该本地 netns 中的相对值。 */
	/* Identifier, value of NSID of default netns,
	 * relative value in that local netns
	 */
	ns_id_t relative_default_ns;

	/* 命名空间的名字。 */
	/* Name */
	char *name;

	/* 已打开该命名空间的文件描述符，setns 切换时使用。 */
	/* File descriptor */
	int fd;

	/* 属于本命名空间的接口主链表。 */
	/* Master list of interfaces belonging to this NS */
	struct list *iflist;

	/* 回指关联的 VRF 上下文(用 void* 以避免头文件循环依赖)。 */
	/* Back Pointer to VRF */
	void *vrf_ctxt;

	/* 使用者自定义数据。 */
	/* User data */
	void *info;
};
/* 以 ns_id 为键的红黑树头部类型与原型声明(比较函数 ns_compare 由 ns.c 提供)。 */
RB_HEAD(ns_head, ns);
RB_PROTOTYPE(ns_head, ns, entry, ns_compare)

/*
 * API for managing NETNS. eg from zebra daemon
 * one want to manage the list of NETNS, etc...
 */

/*
 * NS hooks
 */

/* 【钩子类型】命名空间生命周期事件，供各模块注册回调(ns_add_hook)感知。 */
#define NS_NEW_HOOK        0   /* a new netns is just created */
#define NS_DELETE_HOOK     1   /* a netns is to be deleted */
#define NS_ENABLE_HOOK     2   /* a netns is ready to use */
#define NS_DISABLE_HOOK    3   /* a netns is to be unusable */

/*
 * Add a specific hook ns module.
 * @param1: hook type
 * @param2: the callback function
 *          - param 1: the NS ID
 *          - param 2: the address of the user data pointer (the user data
 *                     can be stored in or freed from there)
 */
/* 【注册钩子】给指定类型(type 取 NS_*_HOOK)注册一个回调，回调参数为新 ns。 */
extern void ns_add_hook(int type, int (*)(struct ns *));


/*
 * NS initializer/destructor
 */

/* 【销毁】关闭并释放命名空间管理模块，供进程退出时调用。 */
extern void ns_terminate(void);

/* API to initialize NETNS managerment
 * parameter is the default ns_id
 */
/* 【初始化管理】初始化 netns 管理层。
 *   ns_id           : 默认命名空间的 ns_id。
 *   internal_ns_idx : 默认命名空间对应的内部索引。 */
extern void ns_init_management(ns_id_t ns_id, ns_id_t internal_ns_idx);


/*
 * NS utilities
 */

/* Create a socket serving for the given NS
 */
/* 【建 socket】在指定命名空间内创建 socket(必要时先切入该 netns 再切回)。
 * 返回新建的 fd，失败为负值。 */
int ns_socket(int domain, int type, int protocol, ns_id_t ns_id);

/* return the path of the NETNS */
/* 【路径】根据名字得到 netns 文件路径(通常落在 NS_RUN_DIR 下)。
 * vty 仅用于报错输出，可为 NULL。返回内部使用的路径串。 */
extern char *ns_netns_pathname(struct vty *vty, const char *name);

/* Parse and execute a function on all the NETNS */
/* 【遍历】对全部命名空间逐个调用 func；回调返回 NS_WALK_STOP 可提前终止、
 * NS_WALK_CONTINUE 继续。param_in 为输入参数，param_out 供回调回传结果。 */
#define NS_WALK_CONTINUE 0
#define NS_WALK_STOP 1

extern void ns_walk_func(int (*func)(struct ns *ns,
				     void *param_in,
				     void **param_out),
			 void *param_in,
			 void **param_out);

/* API to get the NETNS name, from the ns pointer */
/* 【取名字】返回给定 ns 的名字(只读，勿释放)。 */
extern const char *ns_get_name(struct ns *ns);

/* only called from vrf ( when removing netns from vrf)
 * or at VRF termination
 */
/* 【删除】释放一个命名空间对象(仅由 vrf 移除 netns 或 VRF 终止时调用)。 */
extern void ns_delete(struct ns *ns);

/* return > 0 if netns is available
 * called by VRF to check netns backend is available for VRF
 */
/* 【能力探测】运行环境支持 netns 时返回大于 0，否则返回 0。
 * 供 VRF 判断能否以 netns 作为后端。 */
extern int ns_have_netns(void);

/* API to get context information of a NS */
/* 【查上下文】按 ns_id 取出该命名空间的 info 用户数据；找不到返回 NULL。 */
extern void *ns_info_lookup(ns_id_t ns_id);

/* API to map internal ns id value with
 * user friendly ns id external value
 */
/* 【ID 映射】在内部 ns id 与用户可见的外部 ns id 之间做转换。
 * map 为 true 表示内部转外部，false 表示外部转内部；返回映射后的 ID。 */
extern ns_id_t ns_map_nsid_with_external(ns_id_t ns_id, bool map);

/*
 * NS init routine
 * should be called from backendx
 */
/* 【模块初始化】命名空间模块的初始化入口，通常由后端模块调用。 */
extern void ns_init(void);

/* 【宏】默认命名空间的 ns_id 取值 0。 */
#define NS_DEFAULT 0

/* API that can be used to change from NS */
/* 【切换】回到初始(默认)命名空间；返回 0 成功、负值失败。 */
extern int ns_switchback_to_initial(void);
/* 【切换】切入名为 netns_name 的命名空间；返回 0 成功、负值失败。 */
extern int ns_switch_to_netns(const char *netns_name);

/*
 * NS handling routines.
 * called by modules that use NS backend
 */

/* API to search for already present NETNS */
/* 【查找】按 ns_id 或名字查找已登记的命名空间，找不到返回 NULL。 */
extern struct ns *ns_lookup(ns_id_t ns_id);
extern struct ns *ns_lookup_name(const char *name);

/* API to handle NS : creation, enable, disable
 * for enable, a callback function is passed as parameter
 * the callback belongs to the module that uses NS as backend
 * upon enabling the NETNS, the upper layer is informed
 */
/* 【启用】使一个命名空间可用；func 为该模块的上层回调，在启用成功后会以
 * (ns_id, 用户数据)被调用。返回 0 成功、负值失败。 */
extern int ns_enable(struct ns *ns, void (*func)(ns_id_t, void *));
/* 【取得或创建】按 ns、name 或 ns_id 查找已有命名空间，没有则新建一个并
 * 返回其指针。 */
extern struct ns *ns_get_created(struct ns *ns, char *name, ns_id_t ns_id);
/* 【绝对化】把相对 ns_id 结合其所在链路的 NSID，换算成绝对的 ns_id。 */
extern ns_id_t ns_id_get_absolute(ns_id_t ns_id_reference, ns_id_t link_nsid);
/* 【停用】使命名空间不可用，并释放相关资源。 */
extern void ns_disable(struct ns *ns);
/* 【取默认】返回默认命名空间对象。 */
extern struct ns *ns_get_default(void);

#ifdef __cplusplus
}
#endif

#endif /*_ZEBRA_NS_H*/
