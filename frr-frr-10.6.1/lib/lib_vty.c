// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Assorted library VTY commands
 *
 * Copyright (C) 1998 Kunihiro Ishiguro
 * Copyright (C) 2016-2017  David Lamparter for NetDEF, Inc.
 */

/* ============================================================================
 * lib_vty.c —— 公共库（lib/）中“所有守护进程通用”的一批 VTY 命令实现
 *
 * 本文件集中实现那些不属于某个具体协议、每个 FRR 守护进程都需要的命令：
 *   - show memory         显示系统分配器（glibc mallinfo2/mallinfo）统计，
 *                         以及 FRR 内部 qmem 内存池的分组统计。
 *   - show modules        列出已加载的模块及其来源路径。
 *   - frr defaults ...    读写配置时记录的 defaults profile（配置兼容用）。
 *   - frr version ...     配置由哪个版本写出（配置兼容用）。
 *   - XFRR_start_configuration / XFRR_end_configuration
 *                         标记“整份配置读取”的起止，用于统计耗时、
 *                         控制 pending（候选）配置是否允许，并在结束时做一次
 *                         待提交项（pending commit）检查。
 *
 * 关键角色：
 *   - lib_cmd_init() 是入口，由各守护进程启动时调用一次，安装上面这些命令。
 *   - mtype_memstr() 是通用格式化工具，把字节数转成人类可读字符串。
 *   - callback（struct call_back）保存配置读取的起始时刻与起止回调，
 *     具体守护进程可通过 cmd_init_config_callbacks() 挂上自定义逻辑。
 * ============================================================================
 */

#include <zebra.h>
/* malloc.h is generally obsolete, however GNU Libc mallinfo wants it. */
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#ifdef HAVE_MALLOC_MALLOC_H
#include <malloc/malloc.h>
#endif
#include <dlfcn.h>
#ifdef HAVE_LINK_H
#include <link.h>
#endif

#include "log.h"
#include "memory.h"
#include "module.h"
#include "defaults.h"
#include "lib_vty.h"
#include "northbound_cli.h"

/* Looking up memory status from vty interface. */
#include "vector.h"
#include "vty.h"
#include "command.h"

#if defined(HAVE_MALLINFO2) || defined(HAVE_MALLINFO)
/* 打印系统分配器（glibc）的内存统计。
 * 参数：vty 为输出终端。返回：恒返回 1（表示已输出）。
 * 说明：优先用 mallinfo2()（字段为 size_t，不会溢出）；退化到旧的 mallinfo()
 *       时其字段是 int，可能因数值过大翻负，故把负值强制掰回 0x7fffffff。
 * 副作用：仅向 vty 写文本，不修改任何状态。 */
static int show_memory_mallinfo(struct vty *vty)
{
	char buf[MTYPE_MEMSTR_LEN];
#if defined(HAVE_MALLINFO2)
	struct mallinfo2 minfo = mallinfo2();
#elif defined(HAVE_MALLINFO)
	struct mallinfo minfo = mallinfo();

	/* If any 'int' values have rolled over, coerce them back */
	if (minfo.arena < 0)
		minfo.arena = 0x7fffffff;
	if (minfo.hblkhd < 0)
		minfo.hblkhd = 0x7fffffff;
	if (minfo.usmblks < 0)
		minfo.usmblks = 0x7fffffff;
	if (minfo.uordblks < 0)
		minfo.uordblks = 0x7fffffff;
	if (minfo.fsmblks < 0)
		minfo.fsmblks = 0x7fffffff;
	if (minfo.fordblks < 0)
		minfo.fordblks = 0x7fffffff;
#endif

	vty_out(vty, "System allocator statistics:\n");
	vty_out(vty, "  Total heap allocated:  %s\n",
		mtype_memstr(buf, MTYPE_MEMSTR_LEN, minfo.arena));
	vty_out(vty, "  Holding block headers: %s\n",
		mtype_memstr(buf, MTYPE_MEMSTR_LEN, minfo.hblkhd));
	vty_out(vty, "  Used small blocks:     %s\n",
		mtype_memstr(buf, MTYPE_MEMSTR_LEN, minfo.usmblks));
	vty_out(vty, "  Used ordinary blocks:  %s\n",
		mtype_memstr(buf, MTYPE_MEMSTR_LEN, minfo.uordblks));
	vty_out(vty, "  Free small blocks:     %s\n",
		mtype_memstr(buf, MTYPE_MEMSTR_LEN, minfo.fsmblks));
	vty_out(vty, "  Free ordinary blocks:  %s\n",
		mtype_memstr(buf, MTYPE_MEMSTR_LEN, minfo.fordblks));
	vty_out(vty, "  Ordinary blocks:       %ld\n",
		(unsigned long)minfo.ordblks);
	vty_out(vty, "  Small blocks:          %ld\n",
		(unsigned long)minfo.smblks);
	vty_out(vty, "  Holding blocks:        %ld\n",
		(unsigned long)minfo.hblks);
	vty_out(vty, "(see system documentation for 'mallinfo' for meaning)\n");
	return 1;
}
#endif /* HAVE_MALLINFO */

/* qmem_walk() 的回调：按内存组（memgroup）逐组、逐类型（memtype）打印统计。
 * 参数：arg 实为 struct vty *；mg 为当前内存组；mt 为一个内存类型，
 *       当 mt 为 NULL 时表示“刚进入该组”，此时打印组标题与列头。
 * 返回：恒返回 0，表示继续遍历。
 * 坑：列头与数据行都用条件宏 TSTR / TARG 控制是否带 total / max_size 列，
 *     仅当系统支持 malloc_usable_size 时才打印这两列。 */
static int qmem_walker(void *arg, struct memgroup *mg, struct memtype *mt)
{
	struct vty *vty = arg;
	if (!mt) {
		vty_out(vty, "--- qmem %s ---\n", mg->name);
		vty_out(vty, "%-30s: %8s %-8s%s %8s %9s\n",
			"Type", "Current#", "  Size",
#ifdef HAVE_MALLOC_USABLE_SIZE
			"     Total",
#else
			"",
#endif
			"Max#",
#ifdef HAVE_MALLOC_USABLE_SIZE
			"MaxBytes"
#else
			""
#endif
			);
	} else {
		if (mt->n_max != 0) {
			char size[32];
			snprintf(size, sizeof(size), "%6zu", mt->size);
#ifdef HAVE_MALLOC_USABLE_SIZE
#define TSTR " %9zu"
#define TARG , mt->total
#define TARG2 , mt->max_size
#else
#define TSTR ""
#define TARG
#define TARG2
#endif
			vty_out(vty, "%-30s: %8zu %-8s"TSTR" %8zu"TSTR"\n",
				mt->name,
				mt->n_alloc,
				mt->size == 0 ? ""
					      : mt->size == SIZE_VAR
							? "variable"
							: size
				TARG,
				mt->n_max
				TARG2);
		}
	}
	return 0;
}


/* VTY 命令 "show memory"。
 * 先（若平台支持）打印系统分配器统计，再打印 FRR 自身的 qmem 内存池统计。
 * 注意：用的是 DEFUN_NOSH，即本命令的处理函数不自动携带 vty 参数解析包装外的
 *       额外行为；返回值恒为 CMD_SUCCESS。 */
DEFUN_NOSH (show_memory,
	    show_memory_cmd,
	    "show memory",
	    "Show running system information\n"
	    "Memory statistics\n")
{
#ifdef HAVE_MALLINFO
	show_memory_mallinfo(vty);
#endif /* HAVE_MALLINFO */

	qmem_walk(qmem_walker, vty);
	return CMD_SUCCESS;
}

/* VTY 命令 "show modules"：列出已加载模块的名字、版本、描述。
 * 若模块是动态加载（plug->dl_handle 非空），再尽力打印其来源路径：
 *   优先用 dlinfo(RTLD_DI_ORIGIN) 取目录 + RTLD_DI_LINKMAP 取文件名；
 *   依次退化到 load_name 或仅打印 origin。
 * 这些 dlinfo 能力都由编译期宏（HAVE_DLINFO_ORIGIN / HAVE_DLINFO_LINKMAP）把关，
 * 不同平台输出格式不同。返回 CMD_SUCCESS。 */
DEFUN_NOSH (show_modules,
	    show_modules_cmd,
	    "show modules",
	    "Show running system information\n"
	    "Loaded modules\n")
{
	struct frrmod_runtime *plug = frrmod_list;

	vty_out(vty, "%-12s %-25s %s\n\n", "Module Name", "Version",
		"Description");
	while (plug) {
		const struct frrmod_info *i = plug->info;

		vty_out(vty, "%-12s %-25s %s\n", i->name, i->version,
			i->description);
		if (plug->dl_handle) {
#ifdef HAVE_DLINFO_ORIGIN
			char origin[MAXPATHLEN] = "";
			dlinfo(plug->dl_handle, RTLD_DI_ORIGIN, &origin);
#ifdef HAVE_DLINFO_LINKMAP
			const char *name;
			struct link_map *lm = NULL;
			dlinfo(plug->dl_handle, RTLD_DI_LINKMAP, &lm);
			if (lm) {
				name = strrchr(lm->l_name, '/');
				name = name ? name + 1 : lm->l_name;
				vty_out(vty, "\tfrom: %s/%s\n", origin, name);
			}
#else
			vty_out(vty, "\tfrom: %s \n", origin, plug->load_name);
#endif
#else
			vty_out(vty, "\tfrom: %s\n", plug->load_name);
#endif
		}
		plug = plug->next;
	}

	vty_out(vty, "pid: %u\n", (uint32_t)(getpid()));

	return CMD_SUCCESS;
}

/* VTY 命令 "frr defaults PROFILE..."：设置本次配置所用的 defaults profile。
 * PROFILE 支持多段（PROFILE...），用 argv_concat 拼成一个字符串。
 * 若 profile 在当前版本中不存在，仅打印告警并返回 CMD_WARNING，
 * 但仍会把它记录下来（便于把旧配置原文保留下来）。
 * 注意：argv_concat 返回的内存需用 XFREE(MTYPE_TMP, ...) 释放。 */
DEFUN (frr_defaults,
       frr_defaults_cmd,
       "frr defaults PROFILE...",
       "FRRouting global parameters\n"
       "set of configuration defaults used\n"
       "profile string\n")
{
	char *profile = argv_concat(argv, argc, 2);
	int rv = CMD_SUCCESS;

	if (!frr_defaults_profile_valid(profile)) {
		vty_out(vty, "%% WARNING: profile %s is not known in this version\n",
			profile);
		rv = CMD_WARNING;
	}
	frr_defaults_profile_set(profile);
	XFREE(MTYPE_TMP, profile);
	return rv;
}

/* VTY 命令 "frr version VERSION..."：记录写出本份配置的 FRR 版本号。
 * VERSION 支持多段（VERSION...），用 argv_concat 拼接。
 * 返回 CMD_SUCCESS；同样需 XFREE 释放 argv_concat 分配的内存。 */
DEFUN (frr_version,
       frr_version_cmd,
       "frr version VERSION...",
       "FRRouting global parameters\n"
       "version configuration was written by\n"
       "version string\n")
{
	char *version = argv_concat(argv, argc, 2);

	frr_defaults_version_set(version);
	XFREE(MTYPE_TMP, version);
	return CMD_SUCCESS;
}

/* 配置读取回调登记表（全局单例 callback）。
 * 字段：
 *   readin_time   —— 本次配置读取的起始时刻，由 start_config 记录，
 *                    end_config 用它做差算出总耗时。
 *   start_config  —— 可选：配置读取开始时的用户回调。
 *   end_config    —— 可选：配置读取结束时的用户回调。
 * 两个回调由 cmd_init_config_callbacks() 注入，守护进程可按需挂钩。 */
static struct call_back {
	time_t readin_time;

	void (*start_config)(struct vty *vty);
	void (*end_config)(struct vty *vty);
} callback;


/* 内部命令 "XFRR_start_configuration"：标记整份配置读取的开始。
 * 记录当前单调时间为起始时刻，并把 vty->pending_allowed 置 1，
 * 表示在配置读取期间允许积累候选（pending）配置项。
 * 若守护进程注册了 start_config 回调，则在此调用。
 * 该命令由配置解析器在读取配置头部时自动执行，用户一般无需手敲。 */
DEFUN_NOSH(start_config, start_config_cmd, "XFRR_start_configuration",
	   "The Beginning of Configuration\n")
{
	callback.readin_time = monotime(NULL);

	vty->pending_allowed = 1;

	if (callback.start_config)
		(*callback.start_config)(vty);

	return CMD_SUCCESS;
}

/* 内部命令 "XFRR_end_configuration"：标记整份配置读取的结束。
 * 流程：算出读取耗时并转成可读字符串；把 vty->pending_allowed 清 0；
 * 调用 nb_cli_pending_commit_check() 做一次待提交项检查（返回码即本命令返回码）；
 * 打日志，最后触发守护进程注册的 end_config 回调。
 * 返回：nb_cli_pending_commit_check() 的结果（可能是 CMD_WARNING 等）。
 * 坑：pending_allowed 在此清 0，退出配置节点时还会再清一次（见英文注释）。 */
DEFUN_NOSH(end_config, end_config_cmd, "XFRR_end_configuration",
	   "The End of Configuration\n")
{
	time_t readin_time;
	char readin_time_str[MONOTIME_STRLEN];
	int ret;

	readin_time = monotime(NULL);
	readin_time -= callback.readin_time;

	frrtime_to_interval(readin_time, readin_time_str,
			    sizeof(readin_time_str));

	/* This is also getting cleared in the config node exit */
	vty->pending_allowed = 0;
	ret = nb_cli_pending_commit_check(vty);

	zlog_info("Configuration Read in Took: %s", readin_time_str);
	zlog_debug("%s: VTY:%p, pending SET-CFG: %u", __func__, vty,
		   (uint32_t)vty->mgmt_num_pending_setcfg);

	if (callback.end_config)
		(*callback.end_config)(vty);

	return ret;
}

/* 注册配置读取的起止回调。
 * 参数：start_config_cb / end_config_cb，分别对应读取开始与结束时的回调（可为 NULL）。
 * 返回：无。副作用：写入全局 callback 结构。 */
void cmd_init_config_callbacks(void (*start_config_cb)(struct vty *vty),
			       void (*end_config_cb)(struct vty *vty))
{
	callback.start_config = start_config_cb;
	callback.end_config = end_config_cb;
}


/* PROFILE 令牌的补全函数：把内置的 defaults profile 列表填进补全候选集。
 * 参数：comps 为待填充的补全 vector；token 为当前正在补全的令牌（此处未用）。
 * 说明：每个候选都用 XSTRDUP(MTYPE_COMPLETION, ...) 复制一份，由补全框架负责释放。
 * token 未使用，是命令变量处理器固定回调签名的要求。 */
static void defaults_autocomplete(vector comps, struct cmd_token *token)
{
	const char **p;

	for (p = frr_defaults_profiles; *p; p++)
		vector_set(comps, XSTRDUP(MTYPE_COMPLETION, *p));
}

/* 命令变量处理器表：把命令里的 PROFILE 令牌绑定到补全函数。
 * 约定：数组以“空终止项”（.completions = NULL）结尾，注册时逐项扫描。
 * 通过 cmd_variable_handler_register() 生效。 */
static const struct cmd_variable_handler default_var_handlers[] = {
	{.tokenname = "PROFILE", .completions = defaults_autocomplete},
	{.completions = NULL},
};

/* 库级命令初始化入口，各守护进程启动时调用一次。
 * 做两件事：注册命令变量（PROFILE）补全处理器；把本文件定义的命令安装到
 * 相应节点（CONFIG_NODE 装 frr defaults / frr version / 配置起止标记，
 * VIEW_NODE 装 show memory / show modules）。
 * 参数：无。返回：无。副作用：修改全局命令树，重复调用会重复安装。 */
void lib_cmd_init(void)
{
	cmd_variable_handler_register(default_var_handlers);

	install_element(CONFIG_NODE, &frr_defaults_cmd);
	install_element(CONFIG_NODE, &frr_version_cmd);

	install_element(VIEW_NODE, &show_memory_cmd);
	install_element(VIEW_NODE, &show_modules_cmd);

	install_element(CONFIG_NODE, &start_config_cmd);
	install_element(CONFIG_NODE, &end_config_cmd);
}

/* Stats querying from users */
/* 把字节数格式化为人类可读字符串。
 * 参数：buf / len 为输出缓冲及长度；bytes 为待格式化字节数。
 * 返回：恒非 NULL，可能指向 buf，也可能指向常量（"0 bytes"/"1 byte"）。
 * 逻辑：按 2^30 / 2^20 / 2^10 右移得到 G / M / K 的粗值；当数量级超过 10 时，
 *       看被截掉的最高位是否为 1 做四舍五入；否则直接按原始字节数打印。
 * 注意：返回 buf 时内容由 snprintf 写入，len 不足会被截断。 */
/* Return a pointer to a human friendly string describing
 * the byte count passed in. E.g:
 * "0 bytes", "2048 bytes", "110kB", "500MiB", "11GiB", etc.
 * Up to 4 significant figures will be given.
 * The pointer returned may be NULL (indicating an error)
 * or point to the given buffer, or point to static storage.
 */
const char *mtype_memstr(char *buf, size_t len, unsigned long bytes)
{
	unsigned int g, m, k;

	/* easy cases */
	if (!bytes)
		return "0 bytes";
	if (bytes == 1)
		return "1 byte";

	g = bytes >> 30;
	m = bytes >> 20;
	k = bytes >> 10;

	if (g > 10) {
		if (bytes & (1 << 29))
			g++;
		snprintf(buf, len, "%d GB", g);
	} else if (m > 10) {
		if (bytes & (1 << 19))
			m++;
		snprintf(buf, len, "%d MiB", m);
	} else if (k > 10) {
		if (bytes & (1 << 9))
			k++;
		snprintf(buf, len, "%d KiB", k);
	} else
		snprintf(buf, len, "%ld bytes", bytes);

	return buf;
}
