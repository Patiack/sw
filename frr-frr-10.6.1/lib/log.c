// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Logging of zebra
 * Copyright (C) 1997, 1998, 1999 Kunihiro Ishiguro
 */

/*
 * 文件导读：log.c —— FRR 传统日志入口与公共辅助的实现
 *
 * 本文件实现 log.h 中声明的一批“通用日志与诊断辅助”，可看作新日志核心 zlog.c
 * 周围的一圈工具函数。它们大多与某一次具体日志输出无直接关系，而是服务于崩溃诊断、
 * 回溯、时间戳、名字查表与文本清洗等场景。
 *
 * 主要内容：
 *   1. lookup_msg：在 {键, 字符串} 表里按键查名字，是各 *_string 函数的基础。
 *   2. frr_timestamp：带亚秒精度的时间戳渲染，内部缓存同一秒的 localtime 结果。
 *   3. 崩溃 / 信号路径：zlog_signal、zlog_backtrace、zlog_backtrace_sigsafe、
 *      zlog_thread_info，全部只能使用异步信号安全的函数。
 *   4. safe_strerror、memory_oom：错误字符串兜底与内存耗尽时的致命处理。
 *   5. 名字表：DESC_ENTRY 宏生成 command_types / route_types / gr_client_cap_types
 *      等数组，配合 zroute_lookup / zebra_route_string / zserv_command_string 等使用。
 *   6. proto_name2num / proto_redistnum：协议名与 ZEBRA_ROUTE_* 编号互转。
 *   7. zlog_hexdump / zlog_sanitize：十六进制转储与不可打印字符转义。
 *
 * 阅读建议：本文件与 zlog.c 是“工具 / 辅助”与“核心引擎”的分工；一次 zlog 调用的
 * 主体流程在 zlog.c，本文件提供周边能力。
 */
#define FRR_DEFINE_DESC_TABLE

#include <zebra.h>

#ifdef HAVE_GLIBC_BACKTRACE
#include <execinfo.h>
#endif /* HAVE_GLIBC_BACKTRACE */

#include "zclient.h"
#include "log.h"
#include "memory.h"
#include "command.h"
#include "lib_errors.h"
#include "lib/hook.h"
#include "printfrr.h"
#include "frr_pthread.h"

#ifdef HAVE_LIBUNWIND
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#include <dlfcn.h>
#endif

/**
 * Looks up a message in a message list by key.
 *
 * If the message is not found, returns the provided error message.
 *
 * Terminates when it hits a struct message that's all zeros.
 *
 * @param mz the message list
 * @param kz the message key
 * @param nf the message to return if not found
 * @return the message
 */
/*
 * lookup_msg - 在消息表 mz 中按键 kz 查找对应字符串。
 * 表中以全零的 struct message 作为结束标志；找到返回其 str，找不到返回 nf
 * （nf 为 NULL 时用 "(no message found)"）。用 memcmp 与全零模板比较来判断表尾。
 */
const char *lookup_msg(const struct message *mz, int kz, const char *nf)
{
	static struct message nt = {0};
	const char *rz = nf ? nf : "(no message found)";
	const struct message *pnt;
	for (pnt = mz; memcmp(pnt, &nt, sizeof(struct message)); pnt++)
		if (pnt->key == kz) {
			rz = pnt->str ? pnt->str : rz;
			break;
		}
	return rz;
}

/* For time string format. */
/*
 * frr_timestamp - 把当前时间渲染成 "%Y/%m/%d %H:%M:%S"（可选亚秒）写入 buf。
 * 参数 timestamp_precision 为亚秒位数（0 表示不要亚秒）；buflen 为 buf 容量。
 * 返回值：写入的字符数（不含结尾空字符）。内部用 static 缓存同一秒的 strftime 结果，
 * 以避免同一秒内反复调用 localtime；亚秒部分不缓存。
 * 坑：若 buflen 不足以容纳（含亚秒），会退化为不带亚秒，或直接返回 0 并置 buf[0] 为 0。
 */
size_t frr_timestamp(int timestamp_precision, char *buf, size_t buflen)
{
	static struct {
		time_t last;
		size_t len;
		char buf[28];
	} cache;
	struct timeval clock;

	gettimeofday(&clock, NULL);

	/* first, we update the cache if the time has changed */
	if (cache.last != clock.tv_sec) {
		struct tm tm;
		cache.last = clock.tv_sec;
		localtime_r(&cache.last, &tm);
		cache.len = strftime(cache.buf, sizeof(cache.buf),
				     "%Y/%m/%d %H:%M:%S", &tm);
	}
	/* note: it's not worth caching the subsecond part, because
	   chances are that back-to-back calls are not sufficiently close
	   together
	   for the clock not to have ticked forward */

	if (buflen > cache.len) {
		memcpy(buf, cache.buf, cache.len);
		if ((timestamp_precision > 0)
		    && (buflen > cache.len + 1 + timestamp_precision)) {
			/* should we worry about locale issues? */
			static const int divisor[] = {0,   100000, 10000, 1000,
						      100, 10,     1};
			int prec;
			char *p = buf + cache.len + 1
				  + (prec = timestamp_precision);
			*p-- = '\0';
			while (prec > 6)
			/* this is unlikely to happen, but protect anyway */
			{
				*p-- = '0';
				prec--;
			}
			clock.tv_usec /= divisor[prec];
			do {
				*p-- = '0' + (clock.tv_usec % 10);
				clock.tv_usec /= 10;
			} while (--prec > 0);
			*p = '.';
			return cache.len + 1 + timestamp_precision;
		}
		buf[cache.len] = '\0';
		return cache.len;
	}
	if (buflen > 0)
		buf[0] = '\0';
	return 0;
}

/*
 * crash handling
 *
 * NB: only AS-Safe (async-signal) functions can be used here!
 */

/* Note: the goal here is to use only async-signal-safe functions. */
/*
 * zlog_signal - 崩溃信号处理器中调用，记录收到信号的时间、信号号、si_addr 与 PC。
 * 参数：signo 信号号，action 动作文本（如 "aborting"），siginfo_v 为 siginfo_t 指针，
 * program_counter 为出错指令地址（可为 NULL）。
 * 副作用：先经 zlog_sigsafe 输出头部，再打印信号安全回溯，最后输出线程调度信息。
 * 坑：只能用异步信号安全函数；访问 siginfo 字段依赖 siginfo_v 有效。
 */
void zlog_signal(int signo, const char *action, void *siginfo_v,
		 void *program_counter)
{
	siginfo_t *siginfo = siginfo_v;
	time_t now;
	char buf[sizeof("DEFAULT: Received signal S at T (si_addr 0xP, PC 0xP); aborting...")
		 + 100];
	struct fbuf fb = { .buf = buf, .pos = buf, .len = sizeof(buf) };

	time(&now);

	bprintfrr(&fb, "Received signal %d at %lld", signo, (long long)now);
	if (program_counter)
		bprintfrr(&fb, " (si_addr 0x%tx, PC 0x%tx)",
			  (ptrdiff_t)siginfo->si_addr,
			  (ptrdiff_t)program_counter);
	else
		bprintfrr(&fb, " (si_addr 0x%tx)",
			  (ptrdiff_t)siginfo->si_addr);
	bprintfrr(&fb, "; %s\n", action);

	zlog_sigsafe(fb.buf, fb.pos - fb.buf);

	zlog_backtrace_sigsafe(LOG_CRIT, program_counter);

	fb.pos = buf;

	struct event *tc;
	tc = pthread_getspecific(thread_current);

	if (!tc)
		bprintfrr(&fb, "no thread information available\n");
	else
		bprintfrr(&fb, "in thread %s scheduled from %s:%d %s()\n",
			  tc->xref->funcname, tc->xref->xref.file,
			  tc->xref->xref.line, tc->xref->xref.func);

	zlog_sigsafe(fb.buf, fb.pos - fb.buf);
}

/* Log a backtrace using only async-signal-safe functions.
   Needs to be enhanced to support syslog logging. */
/*
 * zlog_backtrace_sigsafe - 只用异步信号安全函数打印调用栈回溯。
 * 优先使用 libunwind（HAVE_LIBUNWIND），否则用 glibc backtrace（HAVE_GLIBC_BACKTRACE）。
 * 参数 priority 仅作语义标识，program_counter 用于在回溯中标注出错位置。
 * 坑：会写入 zlog 的文件状态，只应在进程即将退出 / abort 时调用。
 */
void zlog_backtrace_sigsafe(int priority, void *program_counter)
{
#ifdef HAVE_LIBUNWIND
	char buf[256];
	struct fbuf fb = { .buf = buf, .len = sizeof(buf) };
	unw_cursor_t cursor;
	unw_context_t uc;
	unw_word_t ip, off, sp;
	Dl_info dlinfo;

	memset(&uc, 0, sizeof(uc));
	memset(&cursor, 0, sizeof(cursor));

	unw_getcontext(&uc);
	unw_init_local(&cursor, &uc);
	while (unw_step(&cursor) > 0) {
		char name[128] = "?";

		unw_get_reg(&cursor, UNW_REG_IP, &ip);
		unw_get_reg(&cursor, UNW_REG_SP, &sp);

		if (!unw_get_proc_name(&cursor, buf, sizeof(buf), &off))
			snprintfrr(name, sizeof(name), "%s+%#lx",
				   buf, (long)off);

		fb.pos = buf;
		if (unw_is_signal_frame(&cursor))
			bprintfrr(&fb, "    ---- signal ----\n");
		bprintfrr(&fb, "%-30s %16lx %16lx", name, (long)ip, (long)sp);
		if (dladdr((void *)ip, &dlinfo))
			bprintfrr(&fb, " %s (mapped at %p)",
				  dlinfo.dli_fname, dlinfo.dli_fbase);
		bprintfrr(&fb, "\n");
		zlog_sigsafe(fb.buf, fb.pos - fb.buf);
	}
#elif defined(HAVE_GLIBC_BACKTRACE)
	void *array[64];
	int size, i;
	char buf[128];
	struct fbuf fb = { .buf = buf, .pos = buf, .len = sizeof(buf) };
	char **bt = NULL;

	size = backtrace(array, array_size(array));
	if (size <= 0 || (size_t)size > array_size(array))
		return;

	bprintfrr(&fb, "Backtrace for %d stack frames:", size);
	zlog_sigsafe(fb.pos, fb.buf - fb.pos);

	bt = backtrace_symbols(array, size);

	for (i = 0; i < size; i++) {
		fb.pos = buf;
		if (bt)
			bprintfrr(&fb, "%s", bt[i]);
		else
			bprintfrr(&fb, "[bt %d] 0x%tx", i,
				  (ptrdiff_t)(array[i]));
		zlog_sigsafe(fb.buf, fb.pos - fb.buf);
	}
	if (bt)
		free(bt);
#endif /* HAVE_STRACK_TRACE */
}

/*
 * zlog_backtrace - 打印调用栈回溯，允许使用非异步信号安全的函数（可分配内存等）。
 * 同样优先 libunwind，其次 glibc backtrace，都没有时打印一行提示。
 * 参数 priority 决定这些行以什么优先级记录。
 */
void zlog_backtrace(int priority)
{
#ifdef HAVE_LIBUNWIND
	char buf[100];
	unw_cursor_t cursor = {};
	unw_context_t uc;
	unw_word_t ip, off, sp;
	Dl_info dlinfo;

	unw_getcontext(&uc);
	unw_init_local(&cursor, &uc);
	zlog(priority, "Backtrace:");
	while (unw_step(&cursor) > 0) {
		char name[128] = "?";

		unw_get_reg(&cursor, UNW_REG_IP, &ip);
		unw_get_reg(&cursor, UNW_REG_SP, &sp);

		if (unw_is_signal_frame(&cursor))
			zlog(priority, "    ---- signal ----");

		if (!unw_get_proc_name(&cursor, buf, sizeof(buf), &off))
			snprintf(name, sizeof(name), "%s+%#lx",
				buf, (long)off);

		if (dladdr((void *)ip, &dlinfo))
			zlog(priority, "%-30s %16lx %16lx %s (mapped at %p)",
				name, (long)ip, (long)sp,
				dlinfo.dli_fname, dlinfo.dli_fbase);
		else
			zlog(priority, "%-30s %16lx %16lx",
				name, (long)ip, (long)sp);
	}
#elif defined(HAVE_GLIBC_BACKTRACE)
	void *array[20];
	int size, i;
	char **strings;

	size = backtrace(array, array_size(array));
	if (size <= 0 || (size_t)size > array_size(array)) {
		flog_err_sys(
			EC_LIB_SYSTEM_CALL,
			"Cannot get backtrace, returned invalid # of frames %d (valid range is between 1 and %lu)",
			size, (unsigned long)(array_size(array)));
		return;
	}
	zlog(priority, "Backtrace for %d stack frames:", size);
	if (!(strings = backtrace_symbols(array, size))) {
		flog_err_sys(EC_LIB_SYSTEM_CALL,
			     "Cannot get backtrace symbols (out of memory?)");
		for (i = 0; i < size; i++)
			zlog(priority, "[bt %d] %p", i, array[i]);
	} else {
		for (i = 0; i < size; i++)
			zlog(priority, "[bt %d] %s", i, strings[i]);
		free(strings);
	}
#else /* !HAVE_GLIBC_BACKTRACE && !HAVE_LIBUNWIND */
	zlog(priority, "No backtrace available on this platform.");
#endif
}

/*
 * zlog_thread_info - 打印“当前线程从哪个函数 / 文件 / 行调度而来”的信息。
 * 通过 thread_current 线程特定数据取当前 event；取不到则提示信息不可用。
 * 参数 log_level 为这些行的日志优先级。
 */
void zlog_thread_info(int log_level)
{
	struct event *tc;
	tc = pthread_getspecific(thread_current);

	if (tc)
		zlog(log_level,
		     "Current thread function %s, scheduled from file %s, line %u in %s()",
		     tc->xref->funcname, tc->xref->xref.file,
		     tc->xref->xref.line, tc->xref->xref.func);
	else
		zlog(log_level, "Current thread not known/applicable");
}

/*
 * memory_oom - 内存分配失败（OOM）时的致命处理。
 * 以 LOG_CRIT 打印失败对象与字节数，打印回溯与内存统计，然后 abort()。
 * 参数 size 为失败的字节数，name 为对象类型名。
 * 坑：本函数不返回，一定会终止进程。
 */
void memory_oom(size_t size, const char *name)
{
	zlog(LOG_CRIT,
	     "out of memory: failed to allocate %zu bytes for %s object",
	     size, name);
	zlog_backtrace(LOG_CRIT);
	log_memstats(zlog_progname, true);
	abort();
}

/* Wrapper around strerror to handle case where it returns NULL. */
/*
 * safe_strerror - 包裹 strerror，保证不返回 NULL。
 * 参数 errnum 为错误码；strerror 返回 NULL 时用 "Unknown error" 兜底。
 */
const char *safe_strerror(int errnum)
{
	const char *s = strerror(errnum);
	return (s != NULL) ? s : "Unknown error";
}

/*
 * DESC_ENTRY 宏与名字表：
 *   DESC_ENTRY(T) 生成形如 { T, "T", '\0' } 的表项，成员依次为 type、string、chr。
 *   command_types 以 zserv 命令号作下标，把命令枚举映射为名字字符串，
 *   供 zserv_command_string() 使用；新增 zserv 命令时必须在此补一行。
 */
#define DESC_ENTRY(T) [(T)] = { (T), (#T), '\0' }
static const struct zebra_desc_table command_types[] = {
	DESC_ENTRY(ZEBRA_INTERFACE_ADD),
	DESC_ENTRY(ZEBRA_INTERFACE_DELETE),
	DESC_ENTRY(ZEBRA_INTERFACE_ADDRESS_ADD),
	DESC_ENTRY(ZEBRA_INTERFACE_ADDRESS_DELETE),
	DESC_ENTRY(ZEBRA_INTERFACE_UP),
	DESC_ENTRY(ZEBRA_INTERFACE_DOWN),
	DESC_ENTRY(ZEBRA_INTERFACE_SET_MASTER),
	DESC_ENTRY(ZEBRA_INTERFACE_SET_PROTODOWN),
	DESC_ENTRY(ZEBRA_ROUTE_ADD),
	DESC_ENTRY(ZEBRA_ROUTE_DELETE),
	DESC_ENTRY(ZEBRA_ROUTE_NOTIFY_OWNER),
	DESC_ENTRY(ZEBRA_REDISTRIBUTE_ADD),
	DESC_ENTRY(ZEBRA_REDISTRIBUTE_DELETE),
	DESC_ENTRY(ZEBRA_REDISTRIBUTE_DEFAULT_ADD),
	DESC_ENTRY(ZEBRA_REDISTRIBUTE_DEFAULT_DELETE),
	DESC_ENTRY(ZEBRA_ROUTER_ID_ADD),
	DESC_ENTRY(ZEBRA_ROUTER_ID_DELETE),
	DESC_ENTRY(ZEBRA_ROUTER_ID_UPDATE),
	DESC_ENTRY(ZEBRA_HELLO),
	DESC_ENTRY(ZEBRA_CAPABILITIES),
	DESC_ENTRY(ZEBRA_NEXTHOP_REGISTER),
	DESC_ENTRY(ZEBRA_NEXTHOP_UNREGISTER),
	DESC_ENTRY(ZEBRA_NEXTHOP_UPDATE),
	DESC_ENTRY(ZEBRA_INTERFACE_NBR_ADDRESS_ADD),
	DESC_ENTRY(ZEBRA_INTERFACE_NBR_ADDRESS_DELETE),
	DESC_ENTRY(ZEBRA_INTERFACE_BFD_DEST_UPDATE),
	DESC_ENTRY(ZEBRA_BFD_DEST_REGISTER),
	DESC_ENTRY(ZEBRA_BFD_DEST_DEREGISTER),
	DESC_ENTRY(ZEBRA_BFD_DEST_UPDATE),
	DESC_ENTRY(ZEBRA_BFD_DEST_REPLAY),
	DESC_ENTRY(ZEBRA_REDISTRIBUTE_ROUTE_ADD),
	DESC_ENTRY(ZEBRA_REDISTRIBUTE_ROUTE_DEL),
	DESC_ENTRY(ZEBRA_VRF_ADD),
	DESC_ENTRY(ZEBRA_VRF_DELETE),
	DESC_ENTRY(ZEBRA_VRF_LABEL),
	DESC_ENTRY(ZEBRA_BFD_CLIENT_REGISTER),
	DESC_ENTRY(ZEBRA_BFD_CLIENT_DEREGISTER),
	DESC_ENTRY(ZEBRA_INTERFACE_ENABLE_RADV),
	DESC_ENTRY(ZEBRA_INTERFACE_DISABLE_RADV),
	DESC_ENTRY(ZEBRA_NEXTHOP_LOOKUP),
	DESC_ENTRY(ZEBRA_INTERFACE_LINK_PARAMS),
	DESC_ENTRY(ZEBRA_MPLS_LABELS_ADD),
	DESC_ENTRY(ZEBRA_MPLS_LABELS_DELETE),
	DESC_ENTRY(ZEBRA_MPLS_LABELS_REPLACE),
	DESC_ENTRY(ZEBRA_SR_POLICY_SET),
	DESC_ENTRY(ZEBRA_SR_POLICY_DELETE),
	DESC_ENTRY(ZEBRA_SR_POLICY_NOTIFY_STATUS),
	DESC_ENTRY(ZEBRA_IPMR_ROUTE_STATS),
	DESC_ENTRY(ZEBRA_LABEL_MANAGER_CONNECT),
	DESC_ENTRY(ZEBRA_LABEL_MANAGER_CONNECT_ASYNC),
	DESC_ENTRY(ZEBRA_GET_LABEL_CHUNK),
	DESC_ENTRY(ZEBRA_RELEASE_LABEL_CHUNK),
	DESC_ENTRY(ZEBRA_FEC_REGISTER),
	DESC_ENTRY(ZEBRA_FEC_UNREGISTER),
	DESC_ENTRY(ZEBRA_FEC_UPDATE),
	DESC_ENTRY(ZEBRA_ADVERTISE_DEFAULT_GW),
	DESC_ENTRY(ZEBRA_ADVERTISE_SVI_MACIP),
	DESC_ENTRY(ZEBRA_ADVERTISE_SUBNET),
	DESC_ENTRY(ZEBRA_ADVERTISE_ALL_VNI),
	DESC_ENTRY(ZEBRA_LOCAL_ES_ADD),
	DESC_ENTRY(ZEBRA_LOCAL_ES_DEL),
	DESC_ENTRY(ZEBRA_REMOTE_ES_VTEP_ADD),
	DESC_ENTRY(ZEBRA_REMOTE_ES_VTEP_DEL),
	DESC_ENTRY(ZEBRA_LOCAL_ES_EVI_ADD),
	DESC_ENTRY(ZEBRA_LOCAL_ES_EVI_DEL),
	DESC_ENTRY(ZEBRA_VNI_ADD),
	DESC_ENTRY(ZEBRA_VNI_DEL),
	DESC_ENTRY(ZEBRA_L3VNI_ADD),
	DESC_ENTRY(ZEBRA_L3VNI_DEL),
	DESC_ENTRY(ZEBRA_REMOTE_VTEP_ADD),
	DESC_ENTRY(ZEBRA_REMOTE_VTEP_DEL),
	DESC_ENTRY(ZEBRA_MACIP_ADD),
	DESC_ENTRY(ZEBRA_MACIP_DEL),
	DESC_ENTRY(ZEBRA_IP_PREFIX_ROUTE_ADD),
	DESC_ENTRY(ZEBRA_IP_PREFIX_ROUTE_DEL),
	DESC_ENTRY(ZEBRA_REMOTE_MACIP_ADD),
	DESC_ENTRY(ZEBRA_REMOTE_MACIP_DEL),
	DESC_ENTRY(ZEBRA_DUPLICATE_ADDR_DETECTION),
	DESC_ENTRY(ZEBRA_PW_ADD),
	DESC_ENTRY(ZEBRA_PW_DELETE),
	DESC_ENTRY(ZEBRA_PW_SET),
	DESC_ENTRY(ZEBRA_PW_UNSET),
	DESC_ENTRY(ZEBRA_PW_STATUS_UPDATE),
	DESC_ENTRY(ZEBRA_RULE_ADD),
	DESC_ENTRY(ZEBRA_RULE_DELETE),
	DESC_ENTRY(ZEBRA_RULE_NOTIFY_OWNER),
	DESC_ENTRY(ZEBRA_TABLE_MANAGER_CONNECT),
	DESC_ENTRY(ZEBRA_GET_TABLE_CHUNK),
	DESC_ENTRY(ZEBRA_RELEASE_TABLE_CHUNK),
	DESC_ENTRY(ZEBRA_IPSET_CREATE),
	DESC_ENTRY(ZEBRA_IPSET_DESTROY),
	DESC_ENTRY(ZEBRA_IPSET_ENTRY_ADD),
	DESC_ENTRY(ZEBRA_IPSET_ENTRY_DELETE),
	DESC_ENTRY(ZEBRA_IPSET_NOTIFY_OWNER),
	DESC_ENTRY(ZEBRA_IPSET_ENTRY_NOTIFY_OWNER),
	DESC_ENTRY(ZEBRA_IPTABLE_ADD),
	DESC_ENTRY(ZEBRA_IPTABLE_DELETE),
	DESC_ENTRY(ZEBRA_IPTABLE_NOTIFY_OWNER),
	DESC_ENTRY(ZEBRA_VXLAN_FLOOD_CONTROL),
	DESC_ENTRY(ZEBRA_VXLAN_SG_ADD),
	DESC_ENTRY(ZEBRA_VXLAN_SG_DEL),
	DESC_ENTRY(ZEBRA_VXLAN_SG_REPLAY),
	DESC_ENTRY(ZEBRA_MLAG_PROCESS_UP),
	DESC_ENTRY(ZEBRA_MLAG_PROCESS_DOWN),
	DESC_ENTRY(ZEBRA_MLAG_CLIENT_REGISTER),
	DESC_ENTRY(ZEBRA_MLAG_CLIENT_UNREGISTER),
	DESC_ENTRY(ZEBRA_MLAG_FORWARD_MSG),
	DESC_ENTRY(ZEBRA_NHG_ADD),
	DESC_ENTRY(ZEBRA_NHG_DEL),
	DESC_ENTRY(ZEBRA_NHG_NOTIFY_OWNER),
	DESC_ENTRY(ZEBRA_EVPN_REMOTE_NH_ADD),
	DESC_ENTRY(ZEBRA_EVPN_REMOTE_NH_DEL),
	DESC_ENTRY(ZEBRA_SRV6_LOCATOR_ADD),
	DESC_ENTRY(ZEBRA_SRV6_LOCATOR_DELETE),
	DESC_ENTRY(ZEBRA_SRV6_MANAGER_GET_LOCATOR_CHUNK),
	DESC_ENTRY(ZEBRA_SRV6_MANAGER_RELEASE_LOCATOR_CHUNK),
	DESC_ENTRY(ZEBRA_SRV6_MANAGER_GET_LOCATOR),
	DESC_ENTRY(ZEBRA_SRV6_MANAGER_GET_SRV6_SID),
	DESC_ENTRY(ZEBRA_SRV6_MANAGER_RELEASE_SRV6_SID),
	DESC_ENTRY(ZEBRA_ERROR),
	DESC_ENTRY(ZEBRA_CLIENT_CAPABILITIES),
	DESC_ENTRY(ZEBRA_OPAQUE_MESSAGE),
	DESC_ENTRY(ZEBRA_OPAQUE_REGISTER),
	DESC_ENTRY(ZEBRA_OPAQUE_UNREGISTER),
	DESC_ENTRY(ZEBRA_NEIGH_DISCOVER),
	DESC_ENTRY(ZEBRA_ROUTE_NOTIFY_REQUEST),
	DESC_ENTRY(ZEBRA_CLIENT_CLOSE_NOTIFY),
	DESC_ENTRY(ZEBRA_NEIGH_ADDED),
	DESC_ENTRY(ZEBRA_NEIGH_REMOVED),
	DESC_ENTRY(ZEBRA_NEIGH_GET),
	DESC_ENTRY(ZEBRA_NEIGH_REGISTER),
	DESC_ENTRY(ZEBRA_NEIGH_UNREGISTER),
	DESC_ENTRY(ZEBRA_NEIGH_IP_ADD),
	DESC_ENTRY(ZEBRA_NEIGH_IP_DEL),
	DESC_ENTRY(ZEBRA_CONFIGURE_ARP),
	DESC_ENTRY(ZEBRA_GRE_GET),
	DESC_ENTRY(ZEBRA_GRE_UPDATE),
	DESC_ENTRY(ZEBRA_GRE_SOURCE_SET),
	DESC_ENTRY(ZEBRA_TC_QDISC_INSTALL),
	DESC_ENTRY(ZEBRA_TC_QDISC_UNINSTALL),
	DESC_ENTRY(ZEBRA_TC_CLASS_ADD),
	DESC_ENTRY(ZEBRA_TC_CLASS_DELETE),
	DESC_ENTRY(ZEBRA_TC_FILTER_ADD),
	DESC_ENTRY(ZEBRA_TC_FILTER_DELETE),
	DESC_ENTRY(ZEBRA_OPAQUE_NOTIFY),
	DESC_ENTRY(ZEBRA_SRV6_SID_NOTIFY)
};
#undef DESC_ENTRY

static const struct zebra_desc_table unknown = {0, "unknown", '?'};

/*
 * zroute_lookup - 按路由类型编号在 route_types 表中查出对应描述项。
 * 正常情况下 route_types 以 type 值为下标，可直接命中；为稳健起见还会线性搜索，
 * 若发现表“乱序”会告警但仍返回；越界或查不到时告警并返回 unknown 兜底项。
 * 参数 zroute 为路由类型编号；返回指向表中项（或 unknown）的只读指针。
 */
static const struct zebra_desc_table *zroute_lookup(unsigned int zroute)
{
	unsigned int i;

	if (zroute >= array_size(route_types)) {
		flog_err(EC_LIB_DEVELOPMENT, "unknown zebra route type: %u",
			 zroute);
		return &unknown;
	}
	if (zroute == route_types[zroute].type)
		return &route_types[zroute];
	for (i = 0; i < array_size(route_types); i++) {
		if (zroute == route_types[i].type) {
			zlog_warn(
				"internal error: route type table out of order while searching for %u, please notify developers",
				zroute);
			return &route_types[i];
		}
	}
	flog_err(EC_LIB_DEVELOPMENT,
		 "internal error: cannot find route type %u in table!", zroute);
	return &unknown;
}

/*
 * zebra_route_string - 返回路由类型编号对应的名字字符串（经 zroute_lookup）。
 */
const char *zebra_route_string(unsigned int zroute)
{
	return zroute_lookup(zroute)->string;
}

/*
 * zebra_route_char - 返回路由类型编号对应的单字符（经 zroute_lookup）。
 */
char zebra_route_char(unsigned int zroute)
{
	return zroute_lookup(zroute)->chr;
}

/*
 * zserv_command_string - 返回 zserv 命令编号对应的名字字符串。
 * 越界时告警并返回 "unknown"。
 */
const char *zserv_command_string(unsigned int command)
{
	if (command >= array_size(command_types)) {
		flog_err(EC_LIB_DEVELOPMENT, "unknown zserv command type: %u",
			 command);
		return unknown.string;
	}
	return command_types[command].string;
}

/*
 * gr_client_cap_types - 把 GR（Graceful Restart）客户端能力枚举映射为名字字符串，
 * 供 zserv_gr_client_cap_string() 使用；DESC_ENTRY 用法同上。
 */
#define DESC_ENTRY(T) [(T)] = {(T), (#T), '\0'}
static const struct zebra_desc_table gr_client_cap_types[] = {
	DESC_ENTRY(ZEBRA_CLIENT_GR_CAPABILITIES),
	DESC_ENTRY(ZEBRA_CLIENT_ROUTE_UPDATE_COMPLETE),
	DESC_ENTRY(ZEBRA_CLIENT_ROUTE_UPDATE_PENDING),
	DESC_ENTRY(ZEBRA_CLIENT_GR_DISABLE),
	DESC_ENTRY(ZEBRA_CLIENT_RIB_STALE_TIME),
};
#undef DESC_ENTRY

/*
 * zserv_gr_client_cap_string - 返回 GR 客户端能力 zcc 对应的名字字符串。
 * 越界时告警并返回 "unknown"。
 */
const char *zserv_gr_client_cap_string(uint32_t zcc)
{
	if (zcc >= array_size(gr_client_cap_types)) {
		flog_err(EC_LIB_DEVELOPMENT, "unknown zserv command type: %u",
			 zcc);
		return unknown.string;
	}
	return gr_client_cap_types[zcc].string;
}

/*
 * proto_name2num - 用不区分大小写的比较，把协议名翻译成 ZEBRA_ROUTE_* 编号。
 * 参数 s 为协议名；遍历 route_types 表匹配 string 字段；找不到返回 -1。
 */
int proto_name2num(const char *s)
{
	unsigned i;

	for (i = 0; i < array_size(route_types); ++i)
		if (strcasecmp(s, route_types[i].string) == 0)
			return route_types[i].type;
	return -1;
}

/*
 * proto_redistnum - 把 redistribute 命令里的协议名翻成编号，支持缩写并可按地址族限定。
 * 参数 afi 为地址族（AFI_IP / AFI_IP6），s 为协议名（如 "ospf"、"ospf6"）。
 * s 为 NULL 或无法识别时返回 ZEBRA_ROUTE_ERROR。
 * 坑：同一名字在 IPv4 / IPv6 下可能对应不同编号（如 rip 与 ripng、ospf 与 ospf6）。
 */
uint8_t proto_redistnum(int afi, const char *s)
{
	if (!s)
		return ZEBRA_ROUTE_ERROR;

	if (afi == AFI_IP) {
		if (strmatch(s, "kernel"))
			return ZEBRA_ROUTE_KERNEL;
		else if (strmatch(s, "connected"))
			return ZEBRA_ROUTE_CONNECT;
		else if (strmatch(s, "local"))
			return ZEBRA_ROUTE_LOCAL;
		else if (strmatch(s, "static"))
			return ZEBRA_ROUTE_STATIC;
		else if (strmatch(s, "rip"))
			return ZEBRA_ROUTE_RIP;
		else if (strmatch(s, "eigrp"))
			return ZEBRA_ROUTE_EIGRP;
		else if (strmatch(s, "ospf"))
			return ZEBRA_ROUTE_OSPF;
		else if (strmatch(s, "isis"))
			return ZEBRA_ROUTE_ISIS;
		else if (strmatch(s, "bgp"))
			return ZEBRA_ROUTE_BGP;
		else if (strmatch(s, "table"))
			return ZEBRA_ROUTE_TABLE;
		else if (strmatch(s, "vnc"))
			return ZEBRA_ROUTE_VNC;
		else if (strmatch(s, "vnc-direct"))
			return ZEBRA_ROUTE_VNC_DIRECT;
		else if (strmatch(s, "nhrp"))
			return ZEBRA_ROUTE_NHRP;
		else if (strmatch(s, "babel"))
			return ZEBRA_ROUTE_BABEL;
		else if (strmatch(s, "sharp"))
			return ZEBRA_ROUTE_SHARP;
		else if (strmatch(s, "openfabric"))
			return ZEBRA_ROUTE_OPENFABRIC;
		else if (strmatch(s, "table-direct"))
			return ZEBRA_ROUTE_TABLE_DIRECT;
	}
	if (afi == AFI_IP6) {
		if (strmatch(s, "kernel"))
			return ZEBRA_ROUTE_KERNEL;
		else if (strmatch(s, "connected"))
			return ZEBRA_ROUTE_CONNECT;
		else if (strmatch(s, "local"))
			return ZEBRA_ROUTE_LOCAL;
		else if (strmatch(s, "static"))
			return ZEBRA_ROUTE_STATIC;
		else if (strmatch(s, "ripng"))
			return ZEBRA_ROUTE_RIPNG;
		else if (strmatch(s, "ospf6"))
			return ZEBRA_ROUTE_OSPF6;
		else if (strmatch(s, "isis"))
			return ZEBRA_ROUTE_ISIS;
		else if (strmatch(s, "bgp"))
			return ZEBRA_ROUTE_BGP;
		else if (strmatch(s, "table"))
			return ZEBRA_ROUTE_TABLE;
		else if (strmatch(s, "vnc"))
			return ZEBRA_ROUTE_VNC;
		else if (strmatch(s, "vnc-direct"))
			return ZEBRA_ROUTE_VNC_DIRECT;
		else if (strmatch(s, "nhrp"))
			return ZEBRA_ROUTE_NHRP;
		else if (strmatch(s, "babel"))
			return ZEBRA_ROUTE_BABEL;
		else if (strmatch(s, "sharp"))
			return ZEBRA_ROUTE_SHARP;
		else if (strmatch(s, "openfabric"))
			return ZEBRA_ROUTE_OPENFABRIC;
		else if (strmatch(s, "table-direct"))
			return ZEBRA_ROUTE_TABLE_DIRECT;
	}
	return ZEBRA_ROUTE_ERROR;
}

/*
 * zlog_hexdump - 以每行 8 字节的十六进制加 ASCII 形式转储 mem 处 len 字节内存。
 * 每行用一条 zlog_debug 打印；len 为 0 时只打印一行“零长度”提示。
 * 参数 mem 为起始地址，len 为字节数。
 */
void zlog_hexdump(const void *mem, size_t len)
{
	char line[64];
	const uint8_t *src = mem;
	const uint8_t *end = src + len;

	if (len == 0) {
		zlog_debug("%016lx: (zero length / no data)", (long)src);
		return;
	}

	while (src < end) {
		struct fbuf fb = {
			.buf = line,
			.pos = line,
			.len = sizeof(line),
		};
		const uint8_t *lineend = src + 8;
		unsigned line_bytes = 0;

		bprintfrr(&fb, "%016lx: ", (long)src);

		while (src < lineend && src < end) {
			bprintfrr(&fb, "%02x ", *src++);
			line_bytes++;
		}
		if (line_bytes < 8)
			bprintfrr(&fb, "%*s", (8 - line_bytes) * 3, "");

		src -= line_bytes;
		while (src < lineend && src < end && fb.pos < fb.buf + fb.len) {
			uint8_t byte = *src++;

			if (isprint(byte))
				*fb.pos++ = byte;
			else
				*fb.pos++ = '.';
		}

		zlog_debug("%.*s", (int)(fb.pos - fb.buf), fb.buf);
	}
}

/*
 * There are historical reasons why time since the epoch is 32
 * bit.  Since we need the backwards compatabilty, let's convert
 * the time since the epoch(a time_t value ) to a uint32_t
 * We know that this is going to eventually never work right
 * but there is not much we can do since we cannot go
 * backwards(HA!) to fix poorly choosen data types
 */
/*
 * frr_time_t_to_uint32_t - 把 time_t 截断为 uint32_t（历史遗留的 32 位时间格式）。
 * 参数 value 为输入时间；超过 UINT32_MAX 时返回 UINT32_MAX，否则原样返回。
 * 坑：这是有损转换，2038 之后语义会出问题，仅为兼容旧格式而保留。
 */
uint32_t frr_time_t_to_uint32_t(time_t value)
{
	if (value > (time_t)UINT32_MAX)
		return UINT32_MAX;
	return (uint32_t)value;
}

/*
 * zlog_sanitize - 把可能含不可打印字符的输入清洗成可安全打印的文本。
 * 参数：输出缓冲 buf、容量 bufsz、输入 in、输入长度 inlen。
 * 把换行 / 回车 / 制表转成对应两字符转义，其他控制字符、双引号及 >=127 的字节转成 \xHH。
 * 返回值：buf。坑：为避免写出半截转义序列，剩余空间不足 5 字节即停止。
 */
const char *zlog_sanitize(char *buf, size_t bufsz, const void *in, size_t inlen)
{
	const char *inbuf = in;
	char *pos = buf, *end = buf + bufsz;
	const char *iend = inbuf + inlen;

	memset(buf, 0, bufsz);
	for (; inbuf < iend; inbuf++) {
		/* don't write partial escape sequence */
		if (end - pos < 5)
			break;

		if (*inbuf == '\n')
			snprintf(pos, end - pos, "\\n");
		else if (*inbuf == '\r')
			snprintf(pos, end - pos, "\\r");
		else if (*inbuf == '\t')
			snprintf(pos, end - pos, "\\t");
		else if (*inbuf < ' ' || *inbuf == '"' || *inbuf >= 127)
			snprintf(pos, end - pos, "\\x%02hhx", *inbuf);
		else
			*pos = *inbuf;

		pos += strlen(pos);
	}
	return buf;
}
