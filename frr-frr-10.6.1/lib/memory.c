// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2015-16  David Lamparter, for NetDEF, Inc.
 */

/*
 * 文件导读：memory.c —— MTYPE 内存记账体系的实现
 *
 * 本文件实现 memory.h 声明的那套“带类型记账”的内存分配。核心思想：每次分配/释放都
 * 显式带上一个 struct memtype，于是每条分配路径都能被统计，进程退出时即可打印
 * “谁泄漏了多少、多少次”。
 *
 * 记账骨干：
 *   mg_first / mg_insert —— 所有管理组组成的全局链表头与尾部插入指针，由各组的
 *     构造函数在启动时填好（见 memory.h 的 _DEFINE_MGROUP）。
 *   mt_count_alloc / mt_count_free —— 更新某个 memtype 的计数（活跃次数、峰值、
 *     当前与累计字节），全部用原子操作，保证多线程安全。
 *   mt_checkalloc —— 统一检查 malloc 返回值，失败且 size 非 0 时调用 memory_oom。
 *
 * 对外分配接口：qmalloc/qcalloc/qrealloc/qstrdup 走 mt_checkalloc，
 * qcountfree/qfree 走 mt_count_free；它们被头文件里的 XMALLOC/XFREE 宏包装后使用。
 *
 * 退出统计：qmem_walk 遍历所有组与类型并回调；qmem_exit_walker 是给 log_memstats 用的
 * 回调，把仍有活跃分配的 MTYPE 按组打印到日志/标准错误/文件，并累计泄漏数。
 *
 * 依赖：memory.h（结构与声明）、log.h（zlog_*）、libfrr_trace.h（memalloc/memfree 探针）。
 */
#include <zebra.h>

#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#ifdef HAVE_MALLOC_NP_H
#include <malloc_np.h>
#endif
#ifdef HAVE_MALLOC_MALLOC_H
#include <malloc/malloc.h>
#endif

#include "memory.h"
#include "log.h"
#include "libfrr_trace.h"

/* 兼容层：某些平台（如 macOS）只有 malloc_size 而无 malloc_usable_size，
 * 这里把它别名过去，便于下面统一用 HAVE_MALLOC_USABLE_SIZE 判断。 */
#if defined(HAVE_MALLOC_SIZE) && !defined(HAVE_MALLOC_USABLE_SIZE)
#define malloc_usable_size(x) malloc_size(x)
#define HAVE_MALLOC_USABLE_SIZE
#endif

/* 所有管理组链表的头指针，以及用于尾部插入的二级指针 mg_insert；
 * 各管理组的构造函数会把自身追加到这里（见 memory.h 中 _DEFINE_MGROUP 生成的代码）。 */
static struct memgroup *mg_first = NULL;
struct memgroup **mg_insert = &mg_first;

/* libfrr 自身登记的管理组与类型：LIB 组下含通用临时内存 TMP、
 * 路由表临时内存 TMP_TTABLE、位图内存 BITFIELD */
DEFINE_MGROUP(LIB, "libfrr");
DEFINE_MTYPE(LIB, TMP, "Temporary memory");
DEFINE_MTYPE(LIB, TMP_TTABLE, "Temporary memory for TTABLE");
DEFINE_MTYPE(LIB, BITFIELD, "Bitfield memory");

/* 记账一次成功分配。内部：原子自增活跃次数并更新峰值 n_max；对 size 做“是否恒定”
 * 判定，一旦同一 MTYPE 出现不同大小就把 size 记为 SIZE_VAR 表示可变。
 * 若平台支持 malloc_usable_size，再累计真实字节到 total 并更新 max_size 峰值。
 * 关键参数：mt 类型、size 名义字节、ptr 实际内存指针（用于取可用量）。 */
static inline void mt_count_alloc(struct memtype *mt, size_t size, void *ptr)
{
	size_t current;
	size_t oldsize;

	current = 1 + atomic_fetch_add_explicit(&mt->n_alloc, 1,
						memory_order_relaxed);

	oldsize = atomic_load_explicit(&mt->n_max, memory_order_relaxed);
	if (current > oldsize)
		/* note that this may fail, but approximation is sufficient */
		atomic_compare_exchange_weak_explicit(&mt->n_max, &oldsize,
						      current,
						      memory_order_relaxed,
						      memory_order_relaxed);

	oldsize = atomic_load_explicit(&mt->size, memory_order_relaxed);
	if (oldsize == 0)
		oldsize = atomic_exchange_explicit(&mt->size, size,
						   memory_order_relaxed);
	if (oldsize != 0 && oldsize != size && oldsize != SIZE_VAR)
		atomic_store_explicit(&mt->size, SIZE_VAR,
				      memory_order_relaxed);

#ifdef HAVE_MALLOC_USABLE_SIZE
	size_t mallocsz = malloc_usable_size(ptr);

	current = mallocsz + atomic_fetch_add_explicit(&mt->total, mallocsz,
						       memory_order_relaxed);
	oldsize = atomic_load_explicit(&mt->max_size, memory_order_relaxed);
	if (current > oldsize)
		/* note that this may fail, but approximation is sufficient */
		atomic_compare_exchange_weak_explicit(&mt->max_size, &oldsize,
						      current,
						      memory_order_relaxed,
						      memory_order_relaxed);
#endif
}

/* 记账一次释放。发送 memfree 追踪探针，断言计数非零后原子自减 n_alloc，
 * 支持时再按实际可用量减少 total。关键参数：mt 类型、ptr 被释放指针。 */
static inline void mt_count_free(struct memtype *mt, void *ptr)
{
	frrtrace(2, frr_libfrr, memfree, mt, ptr);

	assert(mt->n_alloc);
	atomic_fetch_sub_explicit(&mt->n_alloc, 1, memory_order_relaxed);

#ifdef HAVE_MALLOC_USABLE_SIZE
	size_t mallocsz = malloc_usable_size(ptr);

	atomic_fetch_sub_explicit(&mt->total, mallocsz, memory_order_relaxed);
#endif
}

/* 分配结果的统一入口：发送 memalloc 追踪探针；若 ptr 为 NULL 且 size 非 0，
 * 判定为内存耗尽并调用 memory_oom（不返回）；否则记账并返回 ptr。
 * 注意 size 为 0 时 malloc 允许返回 NULL，此时直接返回 NULL 不算错误。 */
static inline void *mt_checkalloc(struct memtype *mt, void *ptr, size_t size)
{
	frrtrace(3, frr_libfrr, memalloc, mt, ptr, size);

	if (__builtin_expect(ptr == NULL, 0)) {
		if (size) {
			/* malloc(0) is allowed to return NULL */
			memory_oom(size, mt->name);
		}
		return NULL;
	}
	mt_count_alloc(mt, size, ptr);
	return ptr;
}

/* 按类型分配内存：malloc 后交给 mt_checkalloc 检查与记账，返回指针 */
void *qmalloc(struct memtype *mt, size_t size)
{
	return mt_checkalloc(mt, malloc(size), size);
}

/* 按类型分配并清零：calloc(size, 1) 后交给 mt_checkalloc；size 为元素总大小 */
void *qcalloc(struct memtype *mt, size_t size)
{
	return mt_checkalloc(mt, calloc(size, 1), size);
}

/* 按类型重分配：若 ptr 非空先按原类型减计数，再 realloc（ptr 为空则退化为 malloc），
 * 交给 mt_checkalloc 重新记账。返回新指针。 */
void *qrealloc(struct memtype *mt, void *ptr, size_t size)
{
	if (ptr)
		mt_count_free(mt, ptr);
	return mt_checkalloc(mt, ptr ? realloc(ptr, size) : malloc(size), size);
}

/* 按类型复制字符串：str 非空时 strdup，记账 size 为 strlen+1；str 为 NULL 返回 NULL */
void *qstrdup(struct memtype *mt, const char *str)
{
	return str ? mt_checkalloc(mt, strdup(str), strlen(str) + 1) : NULL;
}

/* 只做记账减法、不真正释放内存：用于内存由其它路径释放（如交给第三方库）的场景。
 * 参数：mt 类型、ptr 指针；ptr 为 NULL 时什么也不做。 */
void qcountfree(struct memtype *mt, void *ptr)
{
	if (ptr)
		mt_count_free(mt, ptr);
}

/* 真正释放内存：先按类型减计数（ptr 非空时），再调用 free(ptr)。
 * 注意即使 ptr 为 NULL 也会调用 free(NULL)，这是安全的。 */
void qfree(struct memtype *mt, void *ptr)
{
	if (ptr)
		mt_count_free(mt, ptr);
	free(ptr);
}

/* 遍历所有管理组与其下的所有 MTYPE，逐个回调 func。
 * 每个组先以 (mg, NULL) 回调一次以打印组标题，再逐个 MTYPE 回调。
 * 返回：回调返回非 0 时立即返回该值，全部走完返回 0。 */
int qmem_walk(qmem_walk_fn *func, void *arg)
{
	struct memgroup *mg;
	struct memtype *mt;
	int rv;

	for (mg = mg_first; mg; mg = mg->next) {
		if ((rv = func(arg, mg, NULL)))
			return rv;
		for (mt = mg->types; mt; mt = mt->next)
			if ((rv = func(arg, mg, mt)))
				return rv;
	}
	return 0;
}

/* 退出统计用参数集合：daemon_name 守护进程名；do_log/do_file/do_stderr 三个输出开关；
 * error 累计发现泄漏的 MTYPE 数；fp 泄漏转储文件句柄；last_mg 用于组标题去重。 */
struct exit_dump_args {
	const char *daemon_name;
	bool do_log;
	bool do_file;
	bool do_stderr;
	int error;
	FILE *fp;
	struct memgroup *last_mg;
};

/* 惰性打开内存统计转储文件（只需一次）。文件名为
 * /tmp/frr-memstats-<daemon>-<pid>-<time>；打开失败则关闭 do_file 以免反复重试。
 * 关键参数：eda 携带 daemon_name 与 fp 等状态。 */
static void qmem_exit_fopen(struct exit_dump_args *eda)
{
	char filename[128];

	if (eda->fp || !eda->do_file || !eda->daemon_name)
		return;

	snprintf(filename, sizeof(filename), "/tmp/frr-memstats-%s-%llu-%llu", eda->daemon_name,
		 (unsigned long long)getpid(), (unsigned long long)time(NULL));
	eda->fp = fopen(filename, "w");

	if (!eda->fp) {
		zlog_err("failed to open memstats dump file %pSQq: %m", filename);
		/* don't try opening file over and over again */
		eda->do_file = false;
	}
}

/* log_memstats 的遍历回调：对每个仍有活跃分配的 MTYPE 做汇报。
 * 若所属组 active_at_exit 为真，说明是预期内的活跃分配，只 zlog_debug 且不计入 error；
 * 否则视为泄漏，累加 error，并按开关输出到日志/标准错误/文件（组标题每组建一次）。
 * 参数：arg 为 struct exit_dump_args；mt 为 NULL 的组标题回调直接返回 0。 */
static int qmem_exit_walker(void *arg, struct memgroup *mg, struct memtype *mt)
{
	struct exit_dump_args *eda = arg;
	const char *prefix = eda->daemon_name ?: "NONE";
	char size[32];

	if (!mt)
		/* iterator calls mg=X, mt=NULL first */
		return 0;

	if (!mt->n_alloc)
		return 0;

	if (mt->size != SIZE_VAR)
		snprintf(size, sizeof(size), "%10zu", mt->size);
	else
		snprintf(size, sizeof(size), "(variably sized)");

	if (mg->active_at_exit) {
		/* not an error - this memgroup has allocations remain active
		 * at exit.  Only printed to zlog_debug.
		 */
		if (!eda->do_log)
			return 0;

		if (eda->last_mg != mg) {
			zlog_debug("showing active allocations in memory group %s (not an error)",
				   mg->name);
			eda->last_mg = mg;
		}
		zlog_debug("memstats:  %-30s: %6zu * %s", mt->name, mt->n_alloc, size);
		return 0;
	}

	eda->error++;
	if (eda->do_file)
		qmem_exit_fopen(eda);

	if (eda->last_mg != mg) {
		if (eda->do_log)
			zlog_warn("showing active allocations in memory group %s", mg->name);
		if (eda->do_stderr)
			fprintf(stderr, "%s: showing active allocations in memory group %s\n",
				prefix, mg->name);
		if (eda->fp)
			fprintf(eda->fp, "%s: showing active allocations in memory group %s\n",
				prefix, mg->name);
		eda->last_mg = mg;
	}

	if (eda->do_log)
		zlog_warn("memstats:  %-30s: %6zu * %s", mt->name, mt->n_alloc, size);
	if (eda->do_stderr)
		fprintf(stderr, "%s: memstats:  %-30s: %6zu * %s\n", prefix, mt->name, mt->n_alloc,
			size);
	if (eda->fp)
		fprintf(eda->fp, "%s: memstats:  %-30s: %6zu * %s\n", prefix, mt->name, mt->n_alloc,
			size);
	return 0;
}

/* 汇总并打印内存统计：初始化参数（enabled 同时控制日志与文件输出，非交互 stderr 也输出），
 * 调 qmem_walk 触发 qmem_exit_walker，关闭文件句柄，最后若有泄漏则告警。
 * 返回：泄漏的 MTYPE 数量（eda.error）。 */
int log_memstats(const char *daemon_name, bool enabled)
{
	struct exit_dump_args eda = {
		.daemon_name = daemon_name,
		.do_log = enabled,
		.do_file = enabled,
		.do_stderr = enabled || !isatty(STDERR_FILENO),
		.error = 0,
	};

	qmem_walk(qmem_exit_walker, &eda);
	if (eda.fp)
		fclose(eda.fp);
	if (eda.error && eda.do_log)
		zlog_warn("exiting with %d leaked MTYPEs", eda.error);
	return eda.error;
}
