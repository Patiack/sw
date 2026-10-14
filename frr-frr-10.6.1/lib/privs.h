// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Zebra privileges header.
 *
 * Copyright (C) 2003 Paul Jakma.
 */

/*
 * ============================================================================
 * 【文件导读】lib/privs.h —— FRR 权限降低（降权）与 capability 管理
 * ============================================================================
 * 职责：
 *   声明让 FRR 各守护进程能够"以 root 启动、初始化完成后降到普通用户
 *   （通常是 frr / quagga）运行"的接口；同时保留一组刚好够用的 Linux
 *   capability，使得降权后仍能完成绑定低端口、操作路由/网络接口等特权动作。
 *
 * 核心数据结构：
 *   struct zebra_privs_t —— 一个守护进程的降权配置：需要保留的能力数组
 *       caps_p、可继承能力 caps_i、目标 user/group/vty_group，以及两个
 *       回调 change / current_state。
 *   struct zebra_privs_refs_t —— 按线程记录的"提权引用计数"，配合互斥量
 *       解决多线程下"某线程临时提权、别的线程不该看到其影响"的竞态。
 *   struct zprivs_ids_t   —— 查询到的 uid / gid 集合（特权与非特权各一份）。
 *   zebra_capabilities_t  —— FRR 关心的 capability 枚举（SETID、BIND、
 *       NET_ADMIN、SYS_ADMIN、NET_RAW ...）。
 *
 * 与其它模块的关系：
 *   实现位于 lib/privs.c；lib/libfrr.c 在进程启动早期调用 zprivs_init。
 *   各守护进程用 frr_with_privs 宏在需要临时提权的代码块前后包一层，
 *   块结束时自动降权（利用 cleanup 属性）。
 *
 * 阅读顺序：
 *   1) 三个枚举（能力表 / 当前状态 / 操作码）；
 *   2) struct zebra_privs_t 与 zebra_privs_refs_t；
 *   3) zprivs_preinit → zprivs_init → zprivs_terminate 的生命周期；
 *   4) frr_with_privs 宏与 _zprivs_raise / _zprivs_lower。
 *
 * 易踩的坑：
 *   - 时序：必须先 zprivs_preinit 记下原始 uid / gid 与能力，再 zprivs_init
 *     真正 setuid/setgid 降权；顺序反了就再也回不去了。
 *   - frr_with_privs 的代码块无论是否成功提权都会执行（有意如此），
 *     块内不能假设一定持有特权。
 *   - 该宏借用 for 循环只跑一次，块内可用 break 提前退出并仍触发降权。
 * ============================================================================
 */

#ifndef _ZEBRA_PRIVS_H
#define _ZEBRA_PRIVS_H

#include <pthread.h>
#include <stdint.h>
#include "lib/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// zebra_capabilities_t：FRR 关心的 Linux capability 枚举。
// 降权不是简单地放弃全部特权，而是"保留这张表里列出的若干能力"。
// 各成员含义：
//   ZCAP_SETID       —— 修改自身 uid / gid（CAP_SETUID / CAP_SETGID）；
//   ZCAP_BIND        —— 绑定特权端口 < 1024（CAP_NET_BIND_SERVICE）；
//   ZCAP_NET_ADMIN   —— 配置接口 / 路由 / 地址等（CAP_NET_ADMIN）；
//   ZCAP_SYS_ADMIN   —— 系统管理如 chroot、挂载（CAP_SYS_ADMIN）；
//   ZCAP_NET_RAW     —— 原始套接字（ping / ICMP 等，CAP_NET_RAW）；
//   ZCAP_CHROOT      —— 切换到受限根目录（CAP_SYS_CHROOT）；
//   ZCAP_NICE        —— 调整调度优先级（CAP_SYS_NICE）；
//   ZCAP_PTRACE      —— 调试其它进程（CAP_SYS_PTRACE）；
//   ZCAP_DAC_OVERRIDE   —— 绕过文件读 / 写 / 执行权限检查（CAP_DAC_OVERRIDE）；
//   ZCAP_READ_SEARCH    —— 绕过文件读 / 目录搜索权限（CAP_DAC_READ_SEARCH）；
//   ZCAP_FOWNER      —— 绕过文件属主检查（CAP_FOWNER）；
//   ZCAP_IPC_LOCK    —— 锁定内存不被换出（CAP_IPC_LOCK，mlock）；
//   ZCAP_SYS_RAWIO   —— 原始 I/O 口操作（CAP_SYS_RAWIO）；
//   ZCAP_MAX         —— 哨兵，表示能力个数上限，不是真实能力。
/* list of zebra capabilities */
typedef enum {
	ZCAP_SETID,
	ZCAP_BIND,
	ZCAP_NET_ADMIN,
	ZCAP_SYS_ADMIN,
	ZCAP_NET_RAW,
	ZCAP_CHROOT,
	ZCAP_NICE,
	ZCAP_PTRACE,
	ZCAP_DAC_OVERRIDE,
	ZCAP_READ_SEARCH,
	ZCAP_FOWNER,
	ZCAP_IPC_LOCK,
	ZCAP_SYS_RAWIO,
	ZCAP_MAX
} zebra_capabilities_t;

// zebra_privs_current_t：进程当前的权限状态，供 current_state 回调返回。
//   ZPRIVS_LOWERED —— 当前为低权（普通用户）；
//   ZPRIVS_RAISED  —— 当前为高权（root 或持有能力）；
//   ZPRIVS_UNKNOWN —— 状态未知。
typedef enum {
	ZPRIVS_LOWERED,
	ZPRIVS_RAISED,
	ZPRIVS_UNKNOWN,
} zebra_privs_current_t;

// zebra_privs_ops_t：对 change 回调的操作码。
//   ZPRIVS_RAISE —— 提权；
//   ZPRIVS_LOWER —— 降权。
typedef enum {
	ZPRIVS_RAISE,
	ZPRIVS_LOWER,
} zebra_privs_ops_t;

// struct zebra_privs_refs_t：按线程记录"提权引用计数"。
//   entry   —— STAILQ 链表节点（挂在 zebra_privs_t.thread_refs 上）；
//   tid     —— 所属线程 id；
//   refcount—— 该线程当前提权了几次（嵌套提权时需要计数）；
//   raised_in_funcname —— 记录首次提权所在函数名，便于调试定位泄漏。
struct zebra_privs_refs_t {
	STAILQ_ENTRY(zebra_privs_refs_t) entry;
	pthread_t tid;
	uint32_t refcount;
	const char *raised_in_funcname;
};

// struct zebra_privs_t：一个守护进程的降权配置，由调用方（各守护进程）填充后
// 交给 zprivs_init。各字段：
//   caps_p / caps_i / cap_num_p / cap_num_i —— 需要保留的能力数组、可继承的能力
//       数组，及二者元素个数；
//   mutex / process_refs / thread_refs —— 进程级与线程级的提权计数与互斥量，
//       用于多线程进程中安全地临时提权；
//   user / group / vty_group —— 降权后要切换到的用户、组，以及 vty socket 的属组；
//   change / current_state —— 由本文件外提供的回调，执行实际提降权与状态查询。
struct zebra_privs_t {
	zebra_capabilities_t *caps_p; /* caps required for operation */
	zebra_capabilities_t *caps_i; /* caps to allow inheritance of */
	int cap_num_p;		      /* number of caps in arrays */
	int cap_num_i;

	/* Mutex and counter used to avoid race conditions in multi-threaded
	 * processes. If privs status is process-wide, we need to
	 * control changes to the privilege status among threads.
	 * If privs changes are per-thread, we need to be able to
	 * manage that too.
	 */
	pthread_mutex_t mutex;
	struct zebra_privs_refs_t process_refs;

	STAILQ_HEAD(thread_refs_q, zebra_privs_refs_t) thread_refs;

	const char *user; /* user and group to run as */
	const char *group;
	const char *vty_group; /* group to chown vty socket to */
	/* methods */
	int (*change)(zebra_privs_ops_t); /* change privileges, 0 on success */
	zebra_privs_current_t (*current_state)(
		void); /* current privilege state */
};

// struct zprivs_ids_t：zprivs_get_ids 查询到的 uid / gid 集合。
//   uid_priv / uid_normal —— 特权用户（root）与普通用户的 uid；
//   gid_priv / gid_normal —— 特权组与普通组的 gid；
//   gid_vty               —— vty 组的 gid（用于给 vty socket 设属组）；
//   约定 -1 表示未定义 / 未知。
struct zprivs_ids_t {
	/* -1 is undefined */
	uid_t uid_priv;   /* privileged uid */
	uid_t uid_normal; /* normal uid */
	gid_t gid_priv;   /* privileged uid */
	gid_t gid_normal; /* normal uid */
	gid_t gid_vty;    /* vty gid */
};

// lib_privs：libfrr 自身（而非某个守护进程）使用的降权配置全局指针。
extern struct zebra_privs_t *lib_privs;

/* initialise zebra privileges */
// zprivs_preinit：降权"预初始化"。必须在真正的 setuid/setgid 之前调用，
// 目的是先记下当前（通常是 root 的）uid / gid 以及进程已具备的 capability，
// 并把 zebra_privs_t 里的字段准备好。此阶段并不改变进程权限。
extern void zprivs_preinit(struct zebra_privs_t *zprivs);
// zprivs_init：真正执行降权——切换到 zprivs 里配置的 user / group，
// 并只保留 caps_p / caps_i 列出的能力。必须紧跟在 zprivs_preinit 之后、
// 且只能在启动的适当时机调用一次。
extern void zprivs_init(struct zebra_privs_t *zprivs);
/* drop all and terminate privileges */
// zprivs_terminate：撤销一切权限、清理资源（程序退出路径使用）。
extern void zprivs_terminate(struct zebra_privs_t *zprivs);
/* query for runtime uid's and gid's, eg vty needs this */
// zprivs_get_ids：查询运行期的 uid / gid 集合，例如 vty 需要知道 gid_vty。
extern void zprivs_get_ids(struct zprivs_ids_t *ids);

/*
 * Wrapper around zprivs, to be used as:
 *   frr_with_privs(&privs) {
 *     ... code ...
 *     if (error)
 *       break;         -- break can be used to get out of the block
 *     ... code ...
 *   }
 *
 * The argument to frr_with_privs() can be NULL to leave privileges as-is
 * (mostly useful for conditional privilege-raising, i.e.:)
 *   frr_with_privs(cond ? &privs : NULL) {}
 *
 * NB: The code block is always executed, regardless of whether privileges
 * could be raised or not, or whether NULL was given or not.  This is fully
 * intentional;  the user may have configured some RBAC or similar that we
 * are not aware of, but that allows our code to proceed without privileges.
 *
 * The point of this wrapper is to prevent accidental bugs where privileges
 * are elevated but then not dropped.  This can happen when, for example, a
 * "return", "goto" or "break" in the middle of the elevated-privilege code
 * skips past the privilege dropping call.
 *
 * The macro below uses variable cleanup to drop privileges as soon as the
 * code block is left in any way (and thus the _privs variable goes out of
 * scope.)  _once is just a trick to run the loop exactly once.
 */
// _zprivs_raise / _zprivs_lower：frr_with_privs 宏背后的两个内部函数，正常代码
// 不应直接调用它们。raise 记录并临时提升权限，lower 释放一次提权（配合 cleanup 自动执行）。
extern struct zebra_privs_t *_zprivs_raise(struct zebra_privs_t *privs,
					   const char *funcname);
extern void _zprivs_lower(struct zebra_privs_t **privs);

// 宏 frr_with_privs：临时提权的作用域包装器。用法：
//     frr_with_privs(&privs) {
//         ...需要特权的代码...
//         if (error) break;   // 用 break 也能安全跳出并自动降权
//         ...
//     }
// 参数可为 NULL，表示维持当前权限不变（用于"按条件提权"：frr_with_privs(cond ? &privs : NULL)）。
// 关键点：无论提权成功与否、传没传 NULL，这个代码块都会被执行（有意为之，
// 因为外部 RBAC 等配置可能已允许无特权运行）；其核心价值是防止"提权后中途
// return / goto / break 漏掉降权"这类 bug。实现上用 for 循环只跑一次，并借
// cleanup(_zprivs_lower) 在 _privs 变量离开作用域时自动降权。
#define frr_with_privs(privs)                                               \
	for (struct zebra_privs_t *_once = NULL,                               \
				  *_privs __attribute__(                       \
					  (unused, cleanup(_zprivs_lower))) =  \
					  _zprivs_raise(privs, __func__);      \
	     _once == NULL; _once = (void *)1)

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_PRIVS_H */
