// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * VRF related header.
 * Copyright (C) 2014 6WIND S.A.
 */

#ifndef _ZEBRA_VRF_H
#define _ZEBRA_VRF_H

/* ============================================================================
 * 【文件导读】vrf.h —— VRF(Virtual Routing and Forwarding)实例接口
 * ============================================================================
 * 【这个文件干什么】声明 FRR 中“虚拟路由转发实例”的表示与全套管理接口。每个
 * VRF 是一张独立的转发表；接口、路由、协议邻居都归属于某个 VRF。VRF 有两种
 * 后端：VRF-lite(内核 VRF 设备加 table_id)与 netns(网络命名空间)。
 *
 * 【核心数据结构 struct vrf】
 *   vrf_id     —— VRF 标识(默认 VRF 为 0，未知为 VRF_UNKNOWN)；
 *   name       —— 名字(默认后端下默认名为 "default")；
 *   status     —— 位标志：VRF_ACTIVE(内核已 up)与 VRF_CONFIGURED(有 FRR 配置)；
 *   ifaces_by_name / ifaces_by_index —— 两个接口索引树；
 *   info       —— 使用者数据；
 *   data       —— 来自内核/OS 的数据(table_id、netns 名)；
 *   ns_ctxt    —— 指回命名空间上下文。
 * 所有 VRF 同时挂在两棵树里：vrfs_by_id 与 vrfs_by_name，分别按 ID、名字查找。
 *
 * 【生命周期】create(新建) → enable(可用) → disable(不可用) → delete(销毁)，
 * 由 vrf_master 里的四个钩子回调转达给各协议模块(由 vrf_init 传入)。
 *
 * 【关键 API 分组】
 *   查找：vrf_lookup_by_id / vrf_lookup_by_name / vrf_get / vrf_update
 *   生命周期：vrf_init / vrf_enable / vrf_disable / vrf_delete / vrf_terminate
 *   网络操作：vrf_socket / vrf_bind / vrf_getaddrinfo / vrf_ioctl
 *   位图：vrf_bitmap_init / _set / _unset / _check / _free
 *   后端：vrf_configure_backend / vrf_get_backend / vrf_is_backend_netns
 *
 * 【与其他模块的关系】依赖 ns.h(netns 后端)、if.h(接口)与 YANG 北向(frr_vrf_info)。
 * 【阅读顺序】宏与 struct vrf → 全局树 → 查找/生命周期接口 → 网络操作 → 后端
 * 与 CLI 接口。
 * ============================================================================
 */
#include "openbsd-tree.h"
#include "linklist.h"
#include "qobj.h"
#include "vty.h"
#include "ns.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 【宏】特殊的 VRF ID，表示“未知/无效”的 VRF(注意与默认 VRF 0 不同)。 */
/* The default VRF ID */
#define VRF_UNKNOWN UINT32_MAX

/* 【平台补齐】若内核头未定义 IFLA_VRF__*，这里本地定义 VRF netlink 属性枚举，
 * 使代码在缺少对应内核头时也能编译。 */
/* Pending: May need to refine this. */
#ifndef IFLA_VRF_MAX
enum { IFLA_VRF_UNSPEC, IFLA_VRF_TABLE, __IFLA_VRF_MAX };

#define IFLA_VRF_MAX (__IFLA_VRF_MAX - 1)
#endif

/* 【宏】VRF 名字与命名空间名字的最大长度(不含结尾 NUL)。 */
#define VRF_NAMSIZ      36
#define NS_NAMSIZ 36

/* 【宏】VRF 相关命令的帮助字符串(CLI 文档文本)。 */
/*
 * The command strings
 */
#define VRF_CMD_HELP_STR    "Specify the VRF\nThe VRF name\n"
#define VRF_ALL_CMD_HELP_STR    "Specify the VRF\nAll VRFs\n"
#define VRF_FULL_CMD_HELP_STR   "Specify the VRF\nThe VRF name\nAll VRFs\n"

/* 【宏】frr-vrf YANG 模型中 VRF 列表的 XPath，及其带名字键的模板(%s 为名字)。 */
#define FRR_VRF_XPATH "/frr-vrf:lib/vrf"
#define FRR_VRF_KEY_XPATH "/frr-vrf:lib/vrf[name='%s']"

/* 【结构】承载 OS/内核侧传递给各守护进程的 VRF 数据：
 * l.table_id(内核表号)与 l.netns_name(命名空间名)。用 union 以便将来扩展。 */
/*
 * Pass some OS specific data up through
 * to the daemons
 */
struct vrf_data {
	union {
		struct {
			uint32_t table_id;
			char netns_name[NS_NAMSIZ];
		} l;
	};
};

/* 【核心结构】一个 VRF 实例。成员含义见各字段注释；同一对象同时参与
 * id_entry 与 name_entry 两棵红黑树。 */
struct vrf {
	/* 两棵红黑树的链接字段：分别按 ID、名字索引同一对象。 */
	RB_ENTRY(vrf) id_entry, name_entry;

	/* VRF 标识，通常与 vector 索引一致。 */
	/* Identifier, same as the vector index */
	vrf_id_t vrf_id;

	/* VRF 名字(末尾保留 NUL)。 */
	/* Name */
	char name[VRF_NAMSIZ + 1];

	/* Zebra 内部状态位：VRF_ACTIVE 与 VRF_CONFIGURED，见下方宏。 */
	/* Zebra internal VRF status */
	uint8_t status;
/* 【位标志】status 的取值：VRF_ACTIVE 表示内核侧已 up；VRF_CONFIGURED 表示
 * 该 VRF 已有 FRR 用户配置。两者正交，可同时置位。 */
#define VRF_ACTIVE     (1 << 0) /* VRF is up in kernel */
#define VRF_CONFIGURED (1 << 1) /* VRF has some FRR configuration */

	/* 属于本 VRF 的接口，按名字索引。 */
	/* Interfaces belonging to this VRF */
	struct if_name_head ifaces_by_name;
	/* 属于本 VRF 的接口，按 ifindex 索引。 */
	struct if_index_head ifaces_by_index;

	/* 使用者自定义数据。 */
	/* User data */
	void *info;

	/* 来自内核的数据(table_id、netns 名)。 */
	/* The table_id from the kernel */
	struct vrf_data data;

	/* 回指命名空间上下文。 */
	/* Back pointer to namespace context */
	void *ns_ctxt;

	/* QOBJ 引用计数/自省的字段展开宏。 */
	QOBJ_FIELDS;
};
/* 以 vrf_id / vrf->name 为键的两棵红黑树头部类型与原型声明。 */
RB_HEAD(vrf_id_head, vrf);
RB_PROTOTYPE(vrf_id_head, vrf, id_entry, vrf_id_compare)
RB_HEAD(vrf_name_head, vrf);
RB_PROTOTYPE(vrf_name_head, vrf, name_entry, vrf_name_compare)
/* 声明 vrf 为可 QOBJ 管理的类型(供内存/引用计数调试)。 */
DECLARE_QOBJ_TYPE(vrf);

/* 【枚举】VRF 后端类型：VRF_BACKEND_VRF_LITE(内核 VRF 设备与表) 或
 * VRF_BACKEND_NETNS(网络命名空间)。 */
/* Allow VRF with netns as backend */
enum vrf_backend_type {
	VRF_BACKEND_VRF_LITE,
	VRF_BACKEND_NETNS,
};

/* 【全局树】所有 VRF 的按 ID、按名字索引的红黑树头(定义在 vrf.c)。 */
extern struct vrf_id_head vrfs_by_id;
extern struct vrf_name_head vrfs_by_name;

/* 【查找接口】按 ID 或名字查到 vrf；vrf_get 找不到则创建；vrf_update 按新 ID
 * 更新已有 VRF 或创建；vrf_id_to_name 把 ID 转为可读名字。 */
extern struct vrf *vrf_lookup_by_id(vrf_id_t);
extern struct vrf *vrf_lookup_by_name(const char *);
extern struct vrf *vrf_get(vrf_id_t vrf_id, const char *name);
extern struct vrf *vrf_update(vrf_id_t new_vrf_id, const char *name);
extern const char *vrf_id_to_name(vrf_id_t vrf_id);

/* 【宏】安全取 VRF 名字：V 为空时回退到 "Unknown"，用于日志打印。 */
#define VRF_LOGNAME(V) V ? V->name : "Unknown"

/* 【内联工具】在 CLI/vty 上下文里按名字解析出 vrf_id。
 *   vty      : 命令上下文，用于输出错误提示。
 *   pvrfid   : 输出参数，写回解析到的 vrf_id。
 *   name     : VRF 名字。
 *   use_json : true 时错误以空 JSON 对象输出，否则输出文本提示。
 *   返回     : 成功 true；VRF 不存在或未激活时 false(且已向 vty 输出提示)。 */
/* Utility for lookups in a cli/vty context */
static inline bool vrf_get_id(struct vty *vty, vrf_id_t *pvrfid, const char *name,
			      bool use_json)
{
	struct vrf *_vrf;

	if (!(_vrf = vrf_lookup_by_name(name))) {
		if (use_json)
			vty_out(vty, "{}\n");
		else
			vty_out(vty, "%% VRF %s not found\n", name);

		return false;
	}
	if (_vrf->vrf_id == VRF_UNKNOWN) {
		if (use_json)
			vty_out(vty, "{}\n");
		else
			vty_out(vty, "%% VRF %s not active\n", name);

		return false;
	}
	*pvrfid = _vrf->vrf_id;

	return true;
}

/* 【内联】VRF 是否已启用(VRF_ACTIVE 位置位)。空指针安全，返回 0 或非 0。 */
/*
 * Check whether the VRF is enabled.
 */
static inline int vrf_is_enabled(struct vrf *vrf)
{
	return vrf && CHECK_FLAG(vrf->status, VRF_ACTIVE);
}

/* 【内联】VRF 是否有 FRR 用户配置(VRF_CONFIGURED 位置位)。 */
/* check if the vrf is user configured */
static inline int vrf_is_user_cfged(struct vrf *vrf)
{
	return vrf && CHECK_FLAG(vrf->status, VRF_CONFIGURED);
}

/* 【内联】统计本 VRF 下真实接口的数量，跳过与 VRF 同名的 l3mdev 设备。 */
static inline uint32_t vrf_interface_count(struct vrf *vrf)
{
	uint32_t count = 0;
	struct interface *ifp;

	RB_FOREACH (ifp, if_name_head, &vrf->ifaces_by_name) {
		/* skip the l3mdev */
		if (strncmp(ifp->name, vrf->name, VRF_NAMSIZ) == 0)
			continue;
		count++;
	}
	return count;
}

/*
 * Utilities to obtain the user data
 */

/* 【查数据】返回指定 VRF 的 info 用户数据指针；VRF 不存在时返回 NULL。 */
/* Look up the data pointer of the specified VRF. */
extern void *vrf_info_lookup(vrf_id_t);

/*
 * VRF bit-map: maintaining flags, one bit per VRF ID
 */
/* 【类型与宏】VRF 位图：按 VRF 标记某位是否置位(内部用哈希表实现 bool)，
 * VRF_BITMAP_NULL 表示空位图。 */
typedef void *vrf_bitmap_t;
#define VRF_BITMAP_NULL     NULL

/* 【位图操作】init 初始为空；set / unset 置位或清位；check 查询某 VRF 是否置位；
 * free 释放整张位图。 */
extern void vrf_bitmap_init(vrf_bitmap_t *pbmap);
extern void vrf_bitmap_free(vrf_bitmap_t *pbmap);
extern void vrf_bitmap_set(vrf_bitmap_t *pbmap, vrf_id_t vrf_id);
extern void vrf_bitmap_unset(vrf_bitmap_t *pbmap, vrf_id_t vrf_id);
extern int vrf_bitmap_check(vrf_bitmap_t *pbmap, vrf_id_t vrf_id);

/* 【初始化】初始化 VRF 子系统：初始化 ns、登记四个生命周期钩子、创建并启用
 * 默认 VRF(失败则致命退出)、注册 CLI 变量补全。
 *   四个参数 create/enable/disable/destroy 分别是新建/启用/停用/销毁的回调，
 *   由本模块在各时机调用，从而把 VRF 事件分发给各协议模块。
 * 【易踩的坑】必须在读配置前尽早调用；默认 VRF 创建或启用失败会 exit(1)。 */
/*
 * VRF initializer/destructor
 *
 * create -> Called back when a new VRF is created.  This
 *           can be either through these 3 options:
 *           1) CLI mentions a vrf before OS knows about it
 *           2) OS calls zebra and we create the vrf from OS
 *              callback
 *           3) zebra calls individual protocols to notify
 *              about the new vrf
 *
 * enable -> Called back when a VRF is actually usable from
 *           an OS perspective ( 2 and 3 above )
 *
 * disable -> Called back when a VRF is being deleted from
 *            the system ( 2 and 3 ) above
 *
 * delete -> Called back when a vrf is being deleted from
 *           the system ( 2 and 3 ) above.
 */
extern void vrf_init(int (*create)(struct vrf *vrf),
		     int (*enable)(struct vrf *vrf),
		     int (*disable)(struct vrf *vrf),
		     int (*destroy)(struct vrf *vrf));

/* 【遍历】先遍历所有非默认 VRF，最后再处理默认 VRF，逐个调用 fnc。
 * vrf_iter_func 是回调类型，入参为每个 vrf。 */
/*
 * Iterate over custom VRFs and round up by processing the default VRF.
 */
typedef void (*vrf_iter_func)(struct vrf *vrf);
extern void vrf_iterate(vrf_iter_func fnc);

/* 【终止】协议关闭时调用，遍历并清理所有 VRF。 */
/*
 * Call vrf_terminate when the protocol is being shutdown
 */
extern void vrf_terminate(void);

/*
 * Utilities to create networks objects,
 * or call network operations
 */

/* 【建 socket】创建与 VRF 关联的 socket 的封装。netns 后端下在命名空间内建
 * socket；VRF-lite 下等价于 socket() 再按需 bind 到 VRF 设备。
 *   name 为使 VRF-lite 下绑定到的 VRF 设备名(可 NULL)。
 * 【易踩的坑】成功返回 fd，失败返回负值；fd 由调用方负责 close。 */
/*
 * Create a new socket associated with a VRF.
 *
 * This is a wrapper that ensures correct behavior when using namespace VRFs.
 * In the namespace case, the socket is created within the namespace. In the
 * non-namespace case, this is equivalent to socket().
 *
 * If name is provided, this is provided to vrf_bind() to bind the socket to
 * the VRF. This is only relevant when using VRF-lite.
 *
 * Summary:
 * - Namespace: pass vrf_id but not name
 * - VRF-lite: pass vrf_id and name of VRF device to bind to
 * - VRF-lite, no binding: pass vrf_id but not name, or just use socket()
 */
extern int vrf_socket(int domain, int type, int protocol, vrf_id_t vrf_id,
		      const char *name);

/* 【建 socket】按 sockunion 的地址族创建与 VRF 关联的 socket，其余语义同
 * vrf_socket。 */
extern int vrf_sockunion_socket(const union sockunion *su, vrf_id_t vrf_id,
				const char *name);

/* 【绑定】把 fd 绑到指定 VRF(必要时绑到其中某接口)。
 *   ifname 为 NULL 或等于 VRF 名时绑到 VRF 设备；否则绑到该 VRF 内的接口。
 *   返回 0 成功、-1 失败。
 * 【易踩的坑】netns 后端与默认 VRF 下无需绑定，直接返回 0；实际绑定靠
 * SO_BINDTODEVICE。 */
/*
 * Binds a socket to an interface (ifname) in a VRF (vrf_id).
 *
 * If ifname is NULL or is equal to the VRF name then bind to a VRF device.
 * Otherwise, bind to the specified interface in the specified VRF.
 *
 * Returns 0 on success and -1 on failure.
 */
extern int vrf_bind(vrf_id_t vrf_id, int fd, const char *ifname);

/* 【VRF 操作】在目标命名空间内执行 getaddrinfo 或 ioctl，执行完再切回初始
 * 命名空间；netns 后端下才真正切换。返回各自底层调用的返回值。 */
/* VRF ioctl operations */
extern int vrf_getaddrinfo(const char *node, const char *service,
			   const struct addrinfo *hints, struct addrinfo **res,
			   vrf_id_t vrf_id);

extern int vrf_ioctl(vrf_id_t vrf_id, int d, unsigned long request, char *args);

/* 【宏】默认 VRF 的 ID 固定为 0。 */
/* The default VRF ID */
#define VRF_DEFAULT 0

/* 【默认名】设置默认 VRF 的名字；只能在启动、读配置之前设置一次。 */
/* Must be called only during startup, before config is read */
extern void vrf_set_default_name(const char *default_name);

/* 【取默认名】返回当前默认 VRF 名字。
 * VRF_DEFAULT_NAME 宏即其简写，故默认名可被 vrf_set_default_name 改变。 */
extern const char *vrf_get_default_name(void);
#define VRF_DEFAULT_NAME    vrf_get_default_name()

/* 【切换】netns 后端下切入某 VRF 的命名空间 / 切回初始命名空间。 */
/* VRF switch from NETNS */
extern int vrf_switch_to_netns(vrf_id_t vrf_id);
extern int vrf_switchback_to_initial(void);

/*
 * VRF backend routines
 * should be called from zebra only
 */

/* 【CLI 初始化】注册 vrf / no vrf / exit-vrf 等命令，并把 VRF 节点的配置回写
 * 函数设为 writefunc(用于 show running-config 输出)。 */
/* VRF vty command initialisation
 */
extern void vrf_cmd_init(int (*writefunc)(struct vty *vty));

/* 【CLI 初始化】安装 VRF 调试命令(debug vrf 与 no debug vrf)。 */
/* VRF vty debugging
 */
extern void vrf_install_commands(void);

/*
 * VRF utilities
 */

/* 【后端配置】设置或读取 VRF 后端类型(VRF-lite 或 netns)，以及判断当前是否
 * 为 netns 后端。 */
/*
 * API for configuring VRF backend
 */
extern void vrf_configure_backend(enum vrf_backend_type backend);
extern enum vrf_backend_type vrf_get_backend(void);
extern bool vrf_is_backend_netns(void);

/* 【内部生命周期】enable/disable 切换 VRF 的可用状态(会改变 VRF_ACTIVE 位并
 * 触发对应钩子)；delete 按需停用并释放 VRF——但若有用户配置或有接口仍归其
 * 名下，则只解除 ID 映射而不真正释放。
 * 【易踩的坑】delete 是幂等式的“尽力释放”，不等于强制销毁所有引用。 */
/* used internally to enable or disable VRF.
 * Notify a change in the VRF ID of the VRF
 */
extern void vrf_disable(struct vrf *vrf);
extern int vrf_enable(struct vrf *vrf);
extern void vrf_delete(struct vrf *vrf);

/* 【对外符号】vrf_notify_oper_changes 由属主(zebra)设置，决定是否发出 VRF 状态
 * 变更通知；frr_vrf_info / frr_vrf_cli_info 是 frr-vrf 模块的 YANG 回调表。 */
extern bool vrf_notify_oper_changes;
extern const struct frr_yang_module_info frr_vrf_info;
extern const struct frr_yang_module_info frr_vrf_cli_info;

#ifdef __cplusplus
}
#endif

#endif /*_ZEBRA_VRF_H*/
