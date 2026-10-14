// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Zebra privileges.
 *
 * Copyright (C) 2003 Paul Jakma.
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * ============================================================================
 * 【文件导读】lib/privs.c —— 从 root 降权到 frr 用户并保留 capability
 * ============================================================================
 * 职责：
 *   实现 privs.h 声明的降权机制。核心：守护进程以 root 启动，做完需要
 *   特权的初始化（绑定低端口、建 socket、读配置等）后，把进程身份降到
 *   普通用户（如 frr），并按需保留一小撮 Linux capability，使后续某些
 *   操作仍能成功；需要时再临时"提权 - 做完 - 降权"。
 *
 * 核心数据结构：
 *   zprivs_state（struct _zprivs_t） —— 本文件私有的全局状态：目标 uid/gid、
 *       保存的 uid、vty 组 gid，以及能力集合的存储（pset_t / cap_t）。
 *   zprivs_null_state —— "不做任何降权"模式下 current_state 的返回值。
 *   cap_map[ZCAP_MAX] —— 把 FRR 抽象能力（ZCAP_*）映射到 Linux 具体能力
 *       （CAP_*）的静态表，每项是一组 system_caps。
 *   注意 zprivs_state 与 cap_map 都是文件级 static，不对外暴露。
 *
 * 与其它模块的关系：
 *   - 由 libfrr.c 在启动早期按 zprivs_preinit → zprivs_init 顺序调用；
 *   - 各守护进程通过 privs.h 的 frr_with_privs 宏间接调用 _zprivs_raise /
 *     _zprivs_lower；
 *   - Linux capability 依赖 sys/capability.h 与 sys/prctl.h，由编译期宏
 *     HAVE_CAPABILITIES / HAVE_LCAPS 控制；否则走 uid 版（seteuid）。
 *
 * 阅读顺序：
 *   1) 顶部的 privs_per_process 与能力类型 typedef（pset_t / pstorage_t）；
 *   2) zprivs_state、cap_map 与 zcaps2sys；
 *   3) capability 路径：zprivs_change_caps / zprivs_state_caps /
 *      zprivs_caps_init / zprivs_caps_terminate；
 *   4) uid 路径：zprivs_change_uid / zprivs_state_uid 与 null 路径；
 *   5) 提权引用计数：get_privs_refs / _zprivs_raise / _zprivs_lower；
 *   6) 生命周期：zprivs_preinit / zprivs_init / zprivs_terminate / zprivs_get_ids。
 *
 * 易踩的坑：
 *   - privs_per_process：不同能力 API 有的是进程级、有的是线程级，决定引用
 *     计数放在 process_refs 还是 thread_refs，改这里要同时理解两者的并发语义。
 *   - HAVE_LCAPS 未定义时会直接 #error，本文件实质只支持 Linux capability。
 *   - zprivs_preinit 只记录信息、不降权；真正 setuid/setgid 在 zprivs_init；
 *     顺序错了将无法再提权。
 *   - 多处错误处理直接 fprintf(stderr) + exit，因为此刻 vty / 日志尚未就绪。
 * ============================================================================
 */
#include <zebra.h>

#include <pwd.h>
#include <grp.h>

#ifdef HAVE_LCAPS
#include <sys/capability.h>
#include <sys/prctl.h>
#endif /* HAVE_LCAPS */

#include "log.h"
#include "privs.h"
#include "memory.h"
#include "frr_pthread.h"
#include "lib_errors.h"
#include "lib/queue.h"

// 定义本文件私有的内存类型 PRIVS，供 XCALLOC / XFREE(MTYPE_PRIVS, ...) 记账使用。
DEFINE_MTYPE_STATIC(LIB, PRIVS, "Privilege information");

/*
 * Different capabilities/privileges apis have different characteristics: some
 * are process-wide, and some are per-thread.
 */
// privs_per_process：本编译单元的能力/权限 API 是"进程级"还是"线程级"的判别。
// Linux capability 是线程级的，故在 HAVE_LCAPS 下为 false（注释里写 = false）；
// 其它情况按进程级处理（true）。这个常量决定提权引用计数放在 process_refs
// 还是 thread_refs，进而影响并发行为。
#ifdef HAVE_CAPABILITIES
#ifdef HAVE_LCAPS
static const bool privs_per_process;  /* = false */
#else
static const bool privs_per_process = true;
#endif /* HAVE_LCAPS */
#else /* HAVE_CAPABILITIES */
static const bool privs_per_process = true;
#endif

#ifdef HAVE_CAPABILITIES

/* sort out some generic internal types for:
 *
 * privilege values (cap_value_t, priv_t) 	-> pvalue_t
 * privilege set (..., priv_set_t) 		-> pset_t
 * privilege working storage (cap_t, ...) 	-> pstorage_t
 *
 * values we think of as numeric (they're ints really, but we dont know)
 * sets are mostly opaque, to hold a set of privileges, related in some way.
 * storage binds together a set of sets we're interested in.
 * (in reality: cap_value_t and priv_t are ints)
 */
#ifdef HAVE_LCAPS
/* Linux doesn't have a 'set' type: a set of related privileges */
// struct _pset：Linux 没有现成的能力集合类型，这里自造一个。
//   num  —— caps 数组的元素个数；
//   caps —— 指向 cap_value_t 数组（一组相关能力）。
struct _pset {
	int num;
	cap_value_t *caps;
};
// 统一各种能力 API 的抽象类型：
//   pvalue_t   —— 单个能力值（对应 cap_value_t）；
//   pset_t     —— 一组能力（对应自造的 struct _pset）；
//   pstorage_t —— 能力工作存储（对应 cap_t）。
typedef cap_value_t pvalue_t;
typedef struct _pset pset_t;
typedef cap_t pstorage_t;

#else /* no LCAPS */
#error "HAVE_CAPABILITIES defined, but neither LCAPS nor Solaris Capabilties!"
#endif /* HAVE_LCAPS */
#endif /* HAVE_CAPABILITIES */

/* the default NULL state we report is RAISED, but could be LOWERED if
 * zprivs_terminate is called and the NULL handler is installed.
 */
// zprivs_null_state：在"NULL 处理"（即不真正降权）模式下 current_state 回调
// 返回的状态。默认 RAISED；当 zprivs_terminate 安装了 NULL 处理器后置为 LOWERED。
static zebra_privs_current_t zprivs_null_state = ZPRIVS_RAISED;

/* internal privileges state */
// zprivs_state：本文件私有的全局权限状态（单例）。
//   caps / syscaps_p / syscaps_i —— 能力的系统存储、请求保留的 permitted 能力集、
//       请求保留的 inheritable 能力集（仅在 HAVE_CAPABILITIES 下存在）；
//   zuid / zsuid —— 降权后要切换到的 uid、保存的原始 uid；
//   zgid / vtygrp —— 降权后的 gid、vty socket 的 gid。
static struct _zprivs_t {
#ifdef HAVE_CAPABILITIES
	pstorage_t caps;   /* working storage        */
	pset_t *syscaps_p; /* system-type requested permitted caps    */
	pset_t *syscaps_i; /* system-type requested inheritable caps  */
#endif			   /* HAVE_CAPABILITIES */
	uid_t zuid,	/* uid to run as            */
		zsuid;     /* saved uid                */
	gid_t zgid;	/* gid to run as            */
	gid_t vtygrp;      /* gid for vty sockets      */
} zprivs_state;

// 下列函数由 zprivs_state 自身状态驱动、通常不直接被外部调用，
// 而是被安装为 zebra_privs_t 的 change / current_state 回调：
//   zprivs_change_caps / zprivs_state_caps —— 基于 capability 的提降权与状态查询；
//   zprivs_change_uid  / zprivs_state_uid  —— 基于 seteuid 的提降权与状态查询；
//   zprivs_change_null / zprivs_state_null —— 什么都不做的空实现。
/* externally exported but not directly accessed functions */
#ifdef HAVE_CAPABILITIES
int zprivs_change_caps(zebra_privs_ops_t);
zebra_privs_current_t zprivs_state_caps(void);
#endif /* HAVE_CAPABILITIES */
int zprivs_change_uid(zebra_privs_ops_t);
zebra_privs_current_t zprivs_state_uid(void);
int zprivs_change_null(zebra_privs_ops_t);
zebra_privs_current_t zprivs_state_null(void);

#ifdef HAVE_CAPABILITIES
/* internal capability API */
// 内部 capability API 前置声明：
//   zcaps2sys            —— 把 FRR 抽象能力数组转换为系统能力集；
//   zprivs_caps_init     —— 初始化 capability（降权、设 permitted/inheritable）；
//   zprivs_caps_terminate—— 清除并释放所有 capability。
static pset_t *zcaps2sys(zebra_capabilities_t *, int);
static void zprivs_caps_init(struct zebra_privs_t *);
static void zprivs_caps_terminate(void);

/* Map of Quagga abstract capabilities to system capabilities */
static struct {
	int num;
	pvalue_t *system_caps;
// cap_map：把 FRR 抽象能力 ZCAP_* 映射为 Linux 具体能力 CAP_* 的静态表。
// 数组下标即 ZCAP_*，每项给出该抽象能力对应的一组系统能力：
//   num          —— 该组内 system_caps 的个数；
//   system_caps  —— 指向 pvalue_t（cap_value_t）数组。
// 例如 ZCAP_SETID 需要 CAP_SETGID 与 CAP_SETUID 两者，故 num = 2。
} cap_map[ZCAP_MAX] = {
#ifdef HAVE_LCAPS /* Quagga -> Linux capabilities mappings */
		[ZCAP_SETID] =
			{
				2, (pvalue_t[]){CAP_SETGID, CAP_SETUID},
			},
		[ZCAP_BIND] =
			{
				1, (pvalue_t[]){CAP_NET_BIND_SERVICE},
			},
		[ZCAP_NET_ADMIN] =
			{
				1, (pvalue_t[]){CAP_NET_ADMIN},
			},
		[ZCAP_NET_RAW] =
			{
				1, (pvalue_t[]){CAP_NET_RAW},
			},
		[ZCAP_CHROOT] =
			{
				1,
				(pvalue_t[]){
					CAP_SYS_CHROOT,
				},
			},
		[ZCAP_NICE] =
			{
				1, (pvalue_t[]){CAP_SYS_NICE},
			},
		[ZCAP_PTRACE] =
			{
				1, (pvalue_t[]){CAP_SYS_PTRACE},
			},
		[ZCAP_DAC_OVERRIDE] =
			{
				1, (pvalue_t[]){CAP_DAC_OVERRIDE},
			},
		[ZCAP_READ_SEARCH] =
			{
				1, (pvalue_t[]){CAP_DAC_READ_SEARCH},
			},
		[ZCAP_SYS_ADMIN] =
			{
				1, (pvalue_t[]){CAP_SYS_ADMIN},
			},
		[ZCAP_FOWNER] =
			{
				1, (pvalue_t[]){CAP_FOWNER},
			},
		[ZCAP_IPC_LOCK] =
			{
				1, (pvalue_t[]){CAP_IPC_LOCK},
			},
		[ZCAP_SYS_RAWIO] =
			{
				1, (pvalue_t[]){CAP_SYS_RAWIO},
			},
#endif /* HAVE_LCAPS */
};

#ifdef HAVE_LCAPS
/* Linux forms of capabilities methods */
/* convert zebras privileges to system capabilities */
// zcaps2sys（static）：把调用方给出的抽象能力数组 zcaps 展开为系统能力集。
// 先把每个抽象能力对应的系统能力个数累加，据此分配 pset_t 与 caps 数组，
// 再逐个拷入，最后设 num 为实际个数。num 为 0 或分配失败返回 NULL。
static pset_t *zcaps2sys(zebra_capabilities_t *zcaps, int num)
{
	pset_t *syscaps;
	int i, j = 0, count = 0;

	if (!num)
		return NULL;

	/* first count up how many system caps we have */
	for (i = 0; i < num; i++)
		count += cap_map[zcaps[i]].num;

	if ((syscaps = XCALLOC(MTYPE_PRIVS, sizeof(pset_t))) == NULL) {
		fprintf(stderr, "%s: could not allocate syscaps!", __func__);
		return NULL;
	}

	syscaps->caps = XCALLOC(MTYPE_PRIVS, (sizeof(pvalue_t) * count));

	if (!syscaps->caps) {
		fprintf(stderr, "%s: could not XCALLOC caps!", __func__);
		return NULL;
	}

	/* copy the capabilities over */
	count = 0;
	for (i = 0; i < num; i++)
		for (j = 0; j < cap_map[zcaps[i]].num; j++)
			syscaps->caps[count++] =
				cap_map[zcaps[i]].system_caps[j];

	/* iterations above should be exact same as previous count, obviously..
	 */
	syscaps->num = count;

	return syscaps;
}

/* set or clear the effective capabilities to/from permitted */
// zprivs_change_caps：把 permitted 能力集在 CAP_EFFECTIVE 上开启或清除，
// 从而"提权 / 降权"（capability 模型里 effective 位决定当前是否生效）。
// 若未配置任何能力（syscaps_p / caps 为空）则直接返回 0，视为成功。
// op 非 RAISE / LOWER 返回 -1。
int zprivs_change_caps(zebra_privs_ops_t op)
{
	cap_flag_value_t cflag;

	/* Called without valid caps - just return. Not every daemon needs
	 * privs.
	 */
	if (zprivs_state.syscaps_p == NULL || zprivs_state.caps == NULL)
		return 0;

	if (op == ZPRIVS_RAISE)
		cflag = CAP_SET;
	else if (op == ZPRIVS_LOWER)
		cflag = CAP_CLEAR;
	else
		return -1;

	if (!cap_set_flag(zprivs_state.caps, CAP_EFFECTIVE,
			  zprivs_state.syscaps_p->num,
			  zprivs_state.syscaps_p->caps, cflag))
		return cap_set_proc(zprivs_state.caps);
	return -1;
}

// zprivs_state_caps：查询当前能力状态——只要任一请求的能力处于 effective（CAP_SET）
// 就认为 RAISED，否则 LOWERED；cap_get_flag 出错返回 UNKNOWN。
// 前置条件：必须已配置能力，否则 assert / exit。
zebra_privs_current_t zprivs_state_caps(void)
{
	int i;
	cap_flag_value_t val;

	/* should be no possibility of being called without valid caps */
	assert(zprivs_state.syscaps_p && zprivs_state.caps);
	if (!(zprivs_state.syscaps_p && zprivs_state.caps))
		exit(1);

	for (i = 0; i < zprivs_state.syscaps_p->num; i++) {
		if (cap_get_flag(zprivs_state.caps,
				 zprivs_state.syscaps_p->caps[i], CAP_EFFECTIVE,
				 &val)) {
			flog_err(
				EC_LIB_SYSTEM_CALL,
				"zprivs_state_caps: could not cap_get_flag, %s",
				safe_strerror(errno));
			return ZPRIVS_UNKNOWN;
		}
		if (val == CAP_SET)
			return ZPRIVS_RAISED;
	}
	return ZPRIVS_LOWERED;
}

/** Release private cap state if allocated. */
// zprivs_state_free_caps（static）：释放 zprivs_state 中 capability 相关的堆内存：
// 先释放 syscaps_p / syscaps_i 内部的 caps 数组与结构本身，再 cap_free 工作存储 caps，
// 并把 caps 置 NULL。用于重复 init 前清理旧状态。
static void zprivs_state_free_caps(void)
{
	if (zprivs_state.syscaps_p) {
		if (zprivs_state.syscaps_p->num)
			XFREE(MTYPE_PRIVS, zprivs_state.syscaps_p->caps);

		XFREE(MTYPE_PRIVS, zprivs_state.syscaps_p);
	}

	if (zprivs_state.syscaps_i) {
		if (zprivs_state.syscaps_i->num)
			XFREE(MTYPE_PRIVS, zprivs_state.syscaps_i->caps);

		XFREE(MTYPE_PRIVS, zprivs_state.syscaps_i);
	}

	if (zprivs_state.caps) {
		cap_free(zprivs_state.caps);
		zprivs_state.caps = NULL;
	}
}

// zprivs_caps_init（static）：capability 路径的初始化核心。步骤：
//   1) 释放旧的 cap 状态；
//   2) zcaps2sys 把请求的能力转成系统能力集；
//   3) prctl(PR_SET_KEEPCAPS, 1) 告诉内核"换 uid 时保留能力"；
//   4) 若目标 uid 与当前不同，setreuid 切到目标 uid；
//   5) cap_init / cap_clear 建并清空能力集；
//   6) 设置 permitted 与 inheritable 能力；
//   7) cap_set_proc 应用（effective 暂清空，用到时再 raise），失败则打印现状并 exit；
//   8) 把 change / current_state 回调装到 zprivs 上。
static void zprivs_caps_init(struct zebra_privs_t *zprivs)
{
	/* Release allocated zcaps if this function was called before. */
	zprivs_state_free_caps();

	zprivs_state.syscaps_p = zcaps2sys(zprivs->caps_p, zprivs->cap_num_p);
	zprivs_state.syscaps_i = zcaps2sys(zprivs->caps_i, zprivs->cap_num_i);

	/* Tell kernel we want caps maintained across uid changes */
	if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) == -1) {
		fprintf(stderr,
			"privs_init: could not set PR_SET_KEEPCAPS, %s\n",
			safe_strerror(errno));
		exit(1);
	}

	/* we have caps, we have no need to ever change back the original user
	 */
	/* only change uid if we don't have the correct one */
	if ((zprivs_state.zuid) && (zprivs_state.zsuid != zprivs_state.zuid)) {
		if (setreuid(zprivs_state.zuid, zprivs_state.zuid)) {
			fprintf(stderr,
				"zprivs_init (cap): could not setreuid, %s\n",
				safe_strerror(errno));
			exit(1);
		}
	}

	if (!(zprivs_state.caps = cap_init())) {
		fprintf(stderr, "privs_init: failed to cap_init, %s\n",
			safe_strerror(errno));
		exit(1);
	}

	if (cap_clear(zprivs_state.caps)) {
		fprintf(stderr, "privs_init: failed to cap_clear, %s\n",
			safe_strerror(errno));
		exit(1);
	}

	/* set permitted caps, if any */
	if (zprivs_state.syscaps_p && zprivs_state.syscaps_p->num) {
		cap_set_flag(zprivs_state.caps, CAP_PERMITTED,
			     zprivs_state.syscaps_p->num,
			     zprivs_state.syscaps_p->caps, CAP_SET);
	}

	/* set inheritable caps, if any */
	if (zprivs_state.syscaps_i && zprivs_state.syscaps_i->num) {
		cap_set_flag(zprivs_state.caps, CAP_INHERITABLE,
			     zprivs_state.syscaps_i->num,
			     zprivs_state.syscaps_i->caps, CAP_SET);
	}

	/* apply caps. CAP_EFFECTIVE is cleared. we'll raise the caps as
	 * and when, and only when, they are needed.
	 */
	if (cap_set_proc(zprivs_state.caps)) {
		cap_t current_caps;
		char *current_caps_text = NULL;
		char *wanted_caps_text = NULL;

		fprintf(stderr, "privs_init: initial cap_set_proc failed: %s\n",
			safe_strerror(errno));

		current_caps = cap_get_proc();
		if (current_caps) {
			current_caps_text = cap_to_text(current_caps, NULL);
			cap_free(current_caps);
		}

		wanted_caps_text = cap_to_text(zprivs_state.caps, NULL);
		fprintf(stderr, "Wanted caps: %s\n",
			wanted_caps_text ? wanted_caps_text : "???");
		fprintf(stderr, "Have   caps: %s\n",
			current_caps_text ? current_caps_text : "???");
		if (current_caps_text)
			cap_free(current_caps_text);
		if (wanted_caps_text)
			cap_free(wanted_caps_text);

		exit(1);
	}

	/* set methods for the caller to use */
	zprivs->change = zprivs_change_caps;
	zprivs->current_state = zprivs_state_caps;
}

// zprivs_caps_terminate（static）：清空所有能力并生效（此后能力永久消失），
// 再释放相关内存。caps 为空则直接返回。
static void zprivs_caps_terminate(void)
{
	/* Clear all capabilities, if we have any. */
	if (zprivs_state.caps)
		cap_clear(zprivs_state.caps);
	else
		return;

	/* and boom, capabilities are gone forever */
	if (cap_set_proc(zprivs_state.caps)) {
		fprintf(stderr, "privs_terminate: cap_set_proc failed, %s",
			safe_strerror(errno));
		exit(1);
	}

	zprivs_state_free_caps();
}
#else /* !HAVE_LCAPS */
#error "no Linux capabilities, dazed and confused..."
#endif /* HAVE_LCAPS */
#endif /* HAVE_CAPABILITIES */

// zprivs_change_uid：非 capability 平台上的提降权——用 seteuid 在
// 保存 uid（zsuid）与目标 uid（zuid）之间切换。目标与保存一致则无需动作。
int zprivs_change_uid(zebra_privs_ops_t op)
{
	if (zprivs_state.zsuid == zprivs_state.zuid)
		return 0;
	if (op == ZPRIVS_RAISE)
		return seteuid(zprivs_state.zsuid);
	else if (op == ZPRIVS_LOWER)
		return seteuid(zprivs_state.zuid);
	else
		return -1;
}

// zprivs_state_uid：用 euid 与目标 uid 比较判断当前权限——相等即 LOWERED，
// 否则 RAISED。
zebra_privs_current_t zprivs_state_uid(void)
{
	return ((zprivs_state.zuid == geteuid()) ? ZPRIVS_LOWERED
						 : ZPRIVS_RAISED);
}

// zprivs_change_null：空操作（不做任何提降权），恒返回 0。
// 用于"该守护进程不需要特权"或尚未配置能力的情形。
int zprivs_change_null(zebra_privs_ops_t op)
{
	return 0;
}

// zprivs_state_null：返回 zprivs_null_state（默认 RAISED，terminate 后为 LOWERED）。
zebra_privs_current_t zprivs_state_null(void)
{
	return zprivs_null_state;
}

#ifndef HAVE_GETGROUPLIST
/* Solaris 11 has no getgrouplist() */
// getgrouplist（static，仅在没有系统实现时编译）：Solaris 11 等平台缺失本函数，
// 这里给出替代实现——把用户所属的各个组 gid 填入 groups 数组（不超过 *ngroups），
// 并回写实际组数到 *ngroups；空间不足时返回 -1。
static int getgrouplist(const char *user, gid_t group, gid_t *groups,
			int *ngroups)
{
	struct group *grp;
	size_t usridx;
	int pos = 0, ret;

	if (pos < *ngroups)
		groups[pos] = group;
	pos++;

	setgrent();
	while ((grp = getgrent())) {
		if (grp->gr_gid == group)
			continue;
		for (usridx = 0; grp->gr_mem[usridx] != NULL; usridx++)
			if (!strcmp(grp->gr_mem[usridx], user)) {
				if (pos < *ngroups)
					groups[pos] = grp->gr_gid;
				pos++;
				break;
			}
	}
	endgrent();

	ret = (pos <= *ngroups) ? pos : -1;
	*ngroups = pos;
	return ret;
}
#endif /* HAVE_GETGROUPLIST */

/*
 * Helper function that locates a refcounting object to use: a process-wide
 * object or a per-pthread object.
 */
// get_privs_refs（static）：定位（必要时创建）用于引用计数的对象。
// 进程级权限用 privs->process_refs；线程级权限则在 privs->thread_refs 链表中
// 按当前线程 tid 查找，找不到就 XCALLOC 一个并插入表尾。
static struct zebra_privs_refs_t *get_privs_refs(struct zebra_privs_t *privs)
{
	struct zebra_privs_refs_t *temp, *refs = NULL;
	pthread_t tid;

	if (privs_per_process)
		refs = &(privs->process_refs);
	else {
		/* Locate - or create - the object for the current pthread. */
		tid = pthread_self();

		STAILQ_FOREACH(temp, &(privs->thread_refs), entry) {
			if (pthread_equal(temp->tid, tid)) {
				refs = temp;
				break;
			}
		}

		/* Need to create a new refcounting object. */
		if (refs == NULL) {
			refs = XCALLOC(MTYPE_PRIVS,
				       sizeof(struct zebra_privs_refs_t));
			refs->tid = tid;
			STAILQ_INSERT_TAIL(&(privs->thread_refs), refs, entry);
		}
	}

	return refs;
}

// _zprivs_raise：frr_with_privs 的"提权"入口（供宏内部调用，非公开 API）。
// privs 为 NULL 直接返回 NULL。在互斥量保护下取得引用计数对象，refcount 从 0
// 变 1 时才真正调用 change(ZPRIVS_RAISE) 提权（即支持嵌套/多线程共享一次提权），
// 并记录首次提权所在函数名；期间保存并恢复 errno。
struct zebra_privs_t *_zprivs_raise(struct zebra_privs_t *privs,
				    const char *funcname)
{
	int save_errno = errno;
	struct zebra_privs_refs_t *refs;

	if (!privs)
		return NULL;

	/*
	 * Serialize 'raise' operations; particularly important for
	 * OSes where privs are process-wide.
	 */
	frr_with_mutex (&(privs->mutex)) {
		/* Locate ref-counting object to use */
		refs = get_privs_refs(privs);

		if (++(refs->refcount) == 1) {
			errno = 0;
			if (privs->change(ZPRIVS_RAISE)) {
				zlog_err("%s: Failed to raise privileges (%s)",
					 funcname, safe_strerror(errno));
			}
			errno = save_errno;
			refs->raised_in_funcname = funcname;
		}
	}

	return privs;
}

// _zprivs_lower：frr_with_privs 的"降权"入口，通常由 cleanup 属性自动触发。
// 在互斥量保护下把引用计数减一，减到 0 时才调用 change(ZPRIVS_LOWER) 真正降权；
// 最后把调用方句柄置 NULL。参数是二级指针，以便置空。
void _zprivs_lower(struct zebra_privs_t **privs)
{
	int save_errno = errno;
	struct zebra_privs_refs_t *refs;

	if (!*privs)
		return;

	/* Serialize 'lower privs' operation - particularly important
	 * when OS privs are process-wide.
	 */
	frr_with_mutex (&(*privs)->mutex) {
		refs = get_privs_refs(*privs);

		if (--(refs->refcount) == 0) {
			errno = 0;
			if ((*privs)->change(ZPRIVS_LOWER)) {
				zlog_err("%s: Failed to lower privileges (%s)",
					 refs->raised_in_funcname,
					 safe_strerror(errno));
			}
			errno = save_errno;
			refs->raised_in_funcname = NULL;
		}
	}

	*privs = NULL;
}

// zprivs_preinit：降权预初始化。在任何 setuid 之前调用，只做记录不改权限：
//   - 初始化互斥量与引用计数结构；
//   - 解析 vty_group 得到 gid_vty（允许失败，失败置 -1）；
//   - 若未配置 user/group/能力，装 null 回调直接返回（无需特权）；
//   - 用 getpwnam / getgrnam 解析目标 user / group，记下 uid / gid 到 zprivs_state。
// 出错（用户/组不存在）直接 fprintf + exit，因为此刻日志系统尚未可用。
void zprivs_preinit(struct zebra_privs_t *zprivs)
{
	struct passwd *pwentry = NULL;
	struct group *grentry = NULL;

	if (!zprivs) {
		fprintf(stderr, "zprivs_init: called with NULL arg!\n");
		exit(1);
	}

	pthread_mutex_init(&(zprivs->mutex), NULL);
	zprivs->process_refs.refcount = 0;
	zprivs->process_refs.raised_in_funcname = NULL;
	STAILQ_INIT(&zprivs->thread_refs);

	if (zprivs->vty_group) {
		/* in a "NULL" setup, this is allowed to fail too, but still
		 * try. */
		if ((grentry = getgrnam(zprivs->vty_group)))
			zprivs_state.vtygrp = grentry->gr_gid;
		else
			zprivs_state.vtygrp = (gid_t)-1;
	}

	/* NULL privs */
	if (!(zprivs->user || zprivs->group || zprivs->cap_num_p
	      || zprivs->cap_num_i)) {
		zprivs->change = zprivs_change_null;
		zprivs->current_state = zprivs_state_null;
		return;
	}

	if (zprivs->user) {
		if ((pwentry = getpwnam(zprivs->user)) == NULL) {
			/* cant use log.h here as it depends on vty */
			fprintf(stderr,
				"privs_init: could not lookup user %s\n",
				zprivs->user);
			exit(1);
		}

		zprivs_state.zuid = pwentry->pw_uid;
		zprivs_state.zgid = pwentry->pw_gid;
	}

	grentry = NULL;

	if (zprivs->group) {
		if ((grentry = getgrnam(zprivs->group)) == NULL) {
			fprintf(stderr,
				"privs_init: could not lookup group %s\n",
				zprivs->group);
			exit(1);
		}

		zprivs_state.zgid = grentry->gr_gid;
	}
}

// lib_privs：libfrr 自身的降权配置全局指针（在 zprivs_init 里赋值，terminate 里清空）。
struct zebra_privs_t *lib_privs;

// zprivs_init：真正执行降权的核心函数，必须紧跟 zprivs_preinit 之后调用。
// 主要动作：
//   1) 未配置特权则直接返回；
//   2) 记录 lib_privs；
//   3) 若指定了 user，用 getgrouplist 取出其附加组；
//   4) 若指定 vty_group，确认用户在 vty 组内并把它加入附加组；
//   5) 保存当前 euid 到 zsuid；
//   6) 仅在需要换 uid 时 setgroups 设置附加组、setregid 换 gid（永久）；
//   7) HAVE_CAPABILITIES 下调 zprivs_caps_init（其中会 setreuid 并设能力），
//      并保证 change 回调非空；否则走 uid 版 seteuid 路径。
// 任何关键系统调用失败都直接 fprintf + exit。
void zprivs_init(struct zebra_privs_t *zprivs)
{
	gid_t groups[NGROUPS_MAX] = {};
	int i, ngroups = 0;
	int found = 0;

	/* NULL privs */
	if (!(zprivs->user || zprivs->group || zprivs->cap_num_p
	      || zprivs->cap_num_i))
		return;

	lib_privs = zprivs;

	if (zprivs->user) {
		ngroups = array_size(groups);
		if (getgrouplist(zprivs->user, zprivs_state.zgid, groups,
				 &ngroups)
		    < 0) {
			/* cant use log.h here as it depends on vty */
			fprintf(stderr,
				"privs_init: could not getgrouplist for user %s\n",
				zprivs->user);
			exit(1);
		}
	}

	if (zprivs->vty_group)
	/* Add the vty_group to the supplementary groups so it can be chowned to
	   */
	{
		if (zprivs_state.vtygrp == (gid_t)-1) {
			fprintf(stderr,
				"privs_init: could not lookup vty group %s\n",
				zprivs->vty_group);
			exit(1);
		}

		for (i = 0; i < ngroups; i++)
			if (groups[i] == zprivs_state.vtygrp) {
				found++;
				break;
			}

		if (!found) {
			fprintf(stderr,
				"privs_init: user(%s) is not part of vty group specified(%s)\n",
				zprivs->user, zprivs->vty_group);
			exit(1);
		}
		if (i >= ngroups && ngroups < (int)array_size(groups)) {
			groups[i] = zprivs_state.vtygrp;
		}
	}

	zprivs_state.zsuid = geteuid(); /* initial uid */
	/* add groups only if we changed uid - otherwise skip */
	if ((ngroups) && (zprivs_state.zsuid != zprivs_state.zuid)) {
		if (setgroups(ngroups, groups)) {
			fprintf(stderr, "privs_init: could not setgroups, %s\n",
				safe_strerror(errno));
			exit(1);
		}
	}

	/* change gid only if we changed uid - otherwise skip */
	if ((zprivs_state.zgid) && (zprivs_state.zsuid != zprivs_state.zuid)) {
		/* change group now, forever. uid we do later */
		if (setregid(zprivs_state.zgid, zprivs_state.zgid)) {
			fprintf(stderr, "zprivs_init: could not setregid, %s\n",
				safe_strerror(errno));
			exit(1);
		}
	}

#ifdef HAVE_CAPABILITIES
	zprivs_caps_init(zprivs);

	/*
	 * If we have initialized the system with no requested
	 * capabilities, change will not have been set
	 * to anything by zprivs_caps_init, As such
	 * we should make sure that when we attempt
	 * to raize privileges that we actually have
	 * a do nothing function to call instead of a
	 * crash :).
	 */
	if (!zprivs->change)
		zprivs->change = zprivs_change_null;

#else  /* !HAVE_CAPABILITIES */
	/* we dont have caps. we'll need to maintain rid and saved uid
	 * and change euid back to saved uid (who we presume has all necessary
	 * privileges) whenever we are asked to raise our privileges.
	 *
	 * This is not worth that much security wise, but all we can do.
	 */
	zprivs_state.zsuid = geteuid();
	/* only change uid if we don't have the correct one */
	if ((zprivs_state.zuid) && (zprivs_state.zsuid != zprivs_state.zuid)) {
		if (setreuid(-1, zprivs_state.zuid)) {
			fprintf(stderr,
				"privs_init (uid): could not setreuid, %s\n",
				safe_strerror(errno));
			exit(1);
		}
	}

	zprivs->change = zprivs_change_uid;
	zprivs->current_state = zprivs_state_uid;
#endif /* HAVE_CAPABILITIES */
}

// zprivs_terminate：进程退出时彻底终止权限。
//   清空 lib_privs；无 privs 结构则直接退出；
//   HAVE_CAPABILITIES 下调用 zprivs_caps_terminate 清光能力，否则用 setreuid 回到目标 uid；
//   释放所有 thread_refs 引用计数对象；
//   把回调装成 null，并把 zprivs_null_state 置为 LOWERED。
void zprivs_terminate(struct zebra_privs_t *zprivs)
{
	struct zebra_privs_refs_t *refs;

	lib_privs = NULL;

	if (!zprivs) {
		fprintf(stderr, "%s: no privs struct given, terminating",
			__func__);
		exit(0);
	}

#ifdef HAVE_CAPABILITIES
	if (zprivs->user || zprivs->group || zprivs->cap_num_p
	    || zprivs->cap_num_i)
		zprivs_caps_terminate();
#else  /* !HAVE_CAPABILITIES */
	/* only change uid if we don't have the correct one */
	if ((zprivs_state.zuid) && (zprivs_state.zsuid != zprivs_state.zuid)) {
		if (setreuid(zprivs_state.zuid, zprivs_state.zuid)) {
			fprintf(stderr,
				"privs_terminate: could not setreuid, %s",
				safe_strerror(errno));
			exit(1);
		}
	}
#endif /* HAVE_LCAPS */

	while ((refs = STAILQ_FIRST(&(zprivs->thread_refs))) != NULL) {
		STAILQ_REMOVE_HEAD(&(zprivs->thread_refs), entry);
		XFREE(MTYPE_PRIVS, refs);
	}

	zprivs->change = zprivs_change_null;
	zprivs->current_state = zprivs_state_null;
	zprivs_null_state = ZPRIVS_LOWERED;
	return;
}

// zprivs_get_ids：把运行期 uid / gid 集合填给调用方（如 vty 需要 gid_vty）。
// uid_priv 取当前 getuid；其余正常 uid / gid 来自 zprivs_state，未设置则填 -1。
void zprivs_get_ids(struct zprivs_ids_t *ids)
{

	ids->uid_priv = getuid();
	(zprivs_state.zuid) ? (ids->uid_normal = zprivs_state.zuid)
			    : (ids->uid_normal = (uid_t)-1);
	(zprivs_state.zgid) ? (ids->gid_normal = zprivs_state.zgid)
			    : (ids->gid_normal = (uid_t)-1);
	(zprivs_state.vtygrp) ? (ids->gid_vty = zprivs_state.vtygrp)
			      : (ids->gid_vty = (uid_t)-1);

	return;
}
