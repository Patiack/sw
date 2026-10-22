// SPDX-License-Identifier: GPL-2.0-or-later
/* SNMP support
 * Copyright (C) 2012 Vincent Bernat <bernat@luffy.cx>
 */

/*
 * ========================== 文件导读 ==========================
 * 1. 职责
 *    在编译期打开 SNMP_AGENTX 时，用 NetSNMP 库实现 SNMP 子代理：
 *    初始化 AgentX 会话，把 NetSNMP 的 fd 与超时接入 FRR 事件循环，
 *    并提供 MIB 注册与 trap 发送接口。文件主体被 SNMP_AGENTX
 *    条件编译包住，未启用时整个文件内容为空。
 * 2. 核心数据结构
 *    - agentx_tm：FRR 事件循环句柄。
 *    - timeout_thr：NetSNMP 下一次超时对应的 FRR 定时器。
 *    - events：NetSNMP 待监听 fd 的链表，元素指向 struct event。
 *    - agentx_enabled：本文件定义的 hook，供 hook_call 广播启用事件。
 * 3. 与其他模块的关系
 *    - CLI 侧由 libagentx.c 装配命令，并通过 agentx_cli_enabled 等 hook
 *      回调本文件的 agentx_cli_on / agentx_cli_off。
 *    - smux.h 提供 smux_init、smux_trap 等对外符号，供各协议 daemon
 *      注册 MIB 与发送 trap。
 *    - libfrr.h 提供事件循环，xref.h 提供符号引用追踪。
 * 4. 阅读顺序
 *    agentx_events_update 是核心：它把 NetSNMP 的 fd 集合同步成 FRR
 *    事件。建议顺序：事件注册 / 超时 -> events_update -> 日志回调
 *    -> CLI 启停 -> smux 对外接口 -> 终止清理。
 * 5. 易踩的坑
 *    - events 链表里存的是 struct event 指针的指针，删除时先释放。
 *    - 两个列表同时遍历使用双指针法，注意 fd 与链表可能错位。
 *    - smux_trap 的 trapobj 用正负 namelen 区分列对象与标量对象。
 *    - smux_disable 与 smux_terminate 职责不同，前者只清标志，
 *      后者才真正释放链表。
 * ============================================================
 */
#include <zebra.h>
#include <fcntl.h>

/*
 * NetSNMP 头文件：net-snmp-config / includes 提供库基础，agent 目录
 * 提供子代理接口，large_fd_set 用于可扩展的 fd 集合。以下全部依赖
 * SNMP_AGENTX 条件编译，未启用则整段不参与编译。
 */
#ifdef SNMP_AGENTX
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
#include <net-snmp/agent/snmp_vars.h>
#include <net-snmp/library/large_fd_set.h>

#include "command.h"
#include "smux.h"
#include "memory.h"
#include "linklist.h"
#include "lib/version.h"
#include "lib_errors.h"
#include "hook.h"
#include "libfrr.h"
#include "xref.h"
#include "lib/libagentx.h"

/*
 * XREF_SETUP：登记本模块的符号引用表，供 xref 工具生成交叉引用，
 * 需放在文件作用域靠前位置。
 */
XREF_SETUP();

/*
 * 定义 agentx_enabled hook。启用 AgentX 时通过 hook_call 广播，
 * 让关心该事件的模块收到通知。
 */
DEFINE_HOOK(agentx_enabled, (), ());

/*
 * 历史遗留：此处原拟定义同名 bool，现已改为 hook，见上方定义。
 * 保留注释仅为记录演进痕迹。
 */
//bool agentx_enabled = false;

/*
 * agentx_tm：FRR 事件循环句柄，由 smux_init 注入。
 * timeout_thr：NetSNMP 下一次需要超时处理时挂的 FRR 定时器。
 * events：NetSNMP 需要监听的 fd 列表，链表元素指向 struct event 指针。
 */
static struct event_loop *agentx_tm;
static struct event *timeout_thr = NULL;
static struct list *events = NULL;

/*
 * agentx_events_update 前置声明，供 timeout 与 read 回调互相调用。
 */
static void agentx_events_update(void);

/*
 * agentx_timeout：FRR 定时器回调。驱动 NetSNMP 的超时处理、报警
 * 处理与未完成请求检查，随后同步 fd 事件集合。
 */
static void agentx_timeout(struct event *t)
{
	snmp_timeout();
	run_alarms();
	netsnmp_check_outstanding_agent_requests();
	agentx_events_update();
}

/*
 * agentx_read：某个待监听 fd 可读时的回调。先把该监听项从 events
 * 链表摘除并释放其记录，再把 fd 临时设为非阻塞，调用 snmp_read2
 * 读取并处理报文，最后恢复原阻塞标志并做下一轮事件同步。
 */
static void agentx_read(struct event *t)
{
	netsnmp_large_fd_set lfds;
	int flags, new_flags = 0;
	int nonblock = false;
	struct listnode *ln = EVENT_ARG(t);
	struct event **thr = listgetdata(ln);
	XFREE(MTYPE_TMP, thr);
	list_delete_node(events, ln);

	/* fix for non blocking socket */
	flags = fcntl(EVENT_FD(t), F_GETFL, 0);
	if (-1 == flags) {
		flog_err(EC_LIB_SYSTEM_CALL, "Failed to get FD settings fcntl: %s(%d)",
			 strerror(errno), errno);
		return;
	}

	if (flags & O_NONBLOCK)
		nonblock = true;
	else
		new_flags = fcntl(EVENT_FD(t), F_SETFL, flags | O_NONBLOCK);

	if (new_flags == -1)
		flog_err(EC_LIB_SYSTEM_CALL, "Failed to set snmp fd non blocking: %s(%d)",
			 strerror(errno), errno);

	netsnmp_large_fd_set_init(&lfds, FD_SETSIZE);
	netsnmp_large_fd_setfd(t->u.fd, &lfds);
	snmp_read2(&lfds);

	/* Reset the flag */
	if (!nonblock) {
		new_flags = fcntl(EVENT_FD(t), F_SETFL, flags);

		if (new_flags == -1)
			flog_err(
				EC_LIB_SYSTEM_CALL,
				"Failed to set snmp fd back to original settings: %s(%d)",
				strerror(errno), errno);
	}

	netsnmp_check_outstanding_agent_requests();
	agentx_events_update();
	netsnmp_large_fd_set_cleanup(&lfds);
}

/*
 * agentx_events_update：核心同步函数。用 snmp_select_info2 取得
 * NetSNMP 当前关心的 fd 集合与超时，先取消旧定时器；若需阻塞等待
 * 则挂新的超时定时器。随后用双指针法同时遍历 fd 序号与 events
 * 链表：不再被关心的 fd 取消监听并删除节点，新出现的 fd 建立监听
 * 项，fd 超过 maxfd 的残留项统一清理。
 */
static void agentx_events_update(void)
{
	int maxfd = 0;
	int block = 1;
	struct timeval timeout = {.tv_sec = 0, .tv_usec = 0};
	netsnmp_large_fd_set lfds;
	struct listnode *ln;
	struct event **thr;
	int fd, thr_fd;

	event_cancel(&timeout_thr);

	netsnmp_large_fd_set_init(&lfds, FD_SETSIZE);
	snmp_select_info2(&maxfd, &lfds, &timeout, &block);

	if (!block) {
		event_add_timer_tv(agentx_tm, agentx_timeout, NULL, &timeout,
				   &timeout_thr);
	}

	ln = listhead(events);
	thr = ln ? listgetdata(ln) : NULL;
	thr_fd = thr ? EVENT_FD(*thr) : -1;

	/* "two-pointer" / two-list simultaneous iteration
	 * ln/thr/thr_fd point to the next existing event listener to hit while
	 * fd counts to catch up */
	for (fd = 0; fd < maxfd; fd++) {
		/* caught up */
		if (thr_fd == fd) {
			struct listnode *nextln = listnextnode(ln);
			if (!netsnmp_large_fd_is_set(fd, &lfds)) {
				event_cancel(thr);
				XFREE(MTYPE_TMP, thr);
				list_delete_node(events, ln);
			}
			ln = nextln;
			thr = ln ? listgetdata(ln) : NULL;
			thr_fd = thr ? EVENT_FD(*thr) : -1;
		}
		/* need listener, but haven't hit one where it would be */
		else if (netsnmp_large_fd_is_set(fd, &lfds)) {
			struct listnode *newln;

			thr = XCALLOC(MTYPE_TMP, sizeof(struct event *));
			newln = listnode_add_before(events, ln, thr);
			event_add_read(agentx_tm, agentx_read, newln, fd, thr);
		}
	}

	/* leftover event listeners at this point have fd > maxfd, delete them
	 */
	while (ln) {
		struct listnode *nextln = listnextnode(ln);
		thr = listgetdata(ln);
		event_cancel(thr);
		XFREE(MTYPE_TMP, thr);
		list_delete_node(events, ln);
		ln = nextln;
	}
	netsnmp_large_fd_set_cleanup(&lfds);
}

/* Logging NetSNMP messages */
/*
 * agentx_log_callback：把 NetSNMP 的日志回调桥接到 FRR 日志。按
 * snmp_log_message 的优先级映射到 flog_err / flog_warn / zlog_*，
 * 并去掉消息尾部换行。
 */
static int agentx_log_callback(int major, int minor, void *serverarg,
			       void *clientarg)
{
	struct snmp_log_message *slm = (struct snmp_log_message *)serverarg;
	char *msg = XSTRDUP(MTYPE_TMP, slm->msg);
	if (msg)
		msg[strlen(msg) - 1] = '\0';
	switch (slm->priority) {
	case LOG_EMERG:
		flog_err(EC_LIB_SNMP, "snmp[emerg]: %s", msg ? msg : slm->msg);
		break;
	case LOG_ALERT:
		flog_err(EC_LIB_SNMP, "snmp[alert]: %s", msg ? msg : slm->msg);
		break;
	case LOG_CRIT:
		flog_err(EC_LIB_SNMP, "snmp[crit]: %s", msg ? msg : slm->msg);
		break;
	case LOG_ERR:
		flog_err(EC_LIB_SNMP, "snmp[err]: %s", msg ? msg : slm->msg);
		break;
	case LOG_WARNING:
		flog_warn(EC_LIB_SNMP, "snmp[warning]: %s",
			  msg ? msg : slm->msg);
		break;
	case LOG_NOTICE:
		zlog_notice("snmp[notice]: %s", msg ? msg : slm->msg);
		break;
	case LOG_INFO:
		zlog_info("snmp[info]: %s", msg ? msg : slm->msg);
		break;
	case LOG_DEBUG:
		zlog_debug("snmp[debug]: %s", msg ? msg : slm->msg);
		break;
	}
	XFREE(MTYPE_TMP, msg);
	return SNMP_ERR_NOERROR;
}

/*
 * agentx_cli_on：agentx 命令的 hook 实现。若尚未启用，则初始化
 * NetSNMP、创建 events 链表、建立初始监听、置位标志并广播
 * agentx_enabled hook。返回 1 表示成功。
 */
static int agentx_cli_on(void)
{
	if (!agentx_enabled) {
		init_snmp(FRR_SMUX_NAME);
		events = list_new();
		agentx_events_update();
		agentx_enabled = true;
		hook_call(agentx_enabled);
	}

	return 1;
}

/*
 * agentx_cli_off：no agentx 命令的 hook 实现。已启用时返回 0 表示
 * 拒绝禁用，未启用时返回 1。
 */
static int agentx_cli_off(void)
{
	if (!agentx_enabled)
		return 1;
	return 0;
}

/*
 * smux_disable：进程早期结束流程注册的清理回调，仅复位启用标志，
 * 不释放 events 链表。始终返回 0。
 */
static int smux_disable(void)
{
	agentx_enabled = false;

	return 0;
}

/*
 * smux_enabled：对外查询当前 AgentX 是否启用，直接返回全局标志。
 */
bool smux_enabled(void)
{
	return agentx_enabled;
}

/*
 * smux_init：模块初始化。记录事件循环，注册两个 CLI hook，配置
 * NetSNMP 子代理与日志回调，init_agent 建立子代理会话，并在
 * frr_early_fini 时注册 smux_disable。
 */
void smux_init(struct event_loop *tm)
{
	agentx_tm = tm;

	hook_register(agentx_cli_enabled, agentx_cli_on);
	hook_register(agentx_cli_disabled, agentx_cli_off);

	netsnmp_enable_subagent();
	snmp_disable_log();
	snmp_enable_calllog();
	snmp_register_callback(SNMP_CALLBACK_LIBRARY, SNMP_CALLBACK_LOGGING,
			       agentx_log_callback, NULL);
	init_agent(FRR_SMUX_NAME);

	hook_register(frr_early_fini, smux_disable);
}

/*
 * smux_agentx_enable：不经 CLI 直接启用 AgentX，逻辑与 agentx_cli_on
 * 类似，但不广播 agentx_enabled hook。
 */
void smux_agentx_enable(void)
{
	if (!agentx_enabled) {
		init_snmp(FRR_SMUX_NAME);
		events = list_new();
		agentx_events_update();
		agentx_enabled = true;
	}
}

/*
 * smux_register_mib：封装 NetSNMP 的 register_mib，供协议模块注册
 * MIB 子树与变量处理函数。参数含义与 register_mib 一致。
 */
void smux_register_mib(const char *descr, struct variable *var, size_t width,
		       int num, oid name[], size_t namelen)
{
	register_mib(descr, var, width, num, name, namelen);
}

/*
 * smux_trap：单索引 trap 的便捷封装。把单个索引拷贝成多索引结构，
 * 再转交 smux_trap_multi_index 处理，保持旧有调用方语义不变。
 */
void smux_trap(struct variable *vp, size_t vp_len, const oid *ename,
	       size_t enamelen, const oid *name, size_t namelen,
	       const oid *iname, size_t inamelen,
	       const struct trap_object *trapobj, size_t trapobjlen,
	       uint8_t sptrap)
{
	struct index_oid trap_index[1];

	/* copy the single index into the multi-index format */
	oid_copy(trap_index[0].indexname, iname, inamelen);
	trap_index[0].indexlen = inamelen;

	smux_trap_multi_index(vp, vp_len, ename, enamelen, name, namelen,
			      trap_index, array_size(trap_index), trapobj,
			      trapobjlen, sptrap);
}

/*
 * smux_trap_multi_index：构造并发送 SNMPv2 trap。先填充 snmpTrapOID，
 * 再逐个 trap 绑定对象拼出完整 OID，必要时按索引区分不同对象，
 * 在 MIB 注册表中找到 findVar 取值后加入变量链表，最后 send_v2trap
 * 并释放链表、刷新事件集合。index_len 为 1 时所有对象共用同一索引，
 * 否则按对象序号取各自索引。
 */
int smux_trap_multi_index(struct variable *vp, size_t vp_len, const oid *ename,
			  size_t enamelen, const oid *name, size_t namelen,
			  struct index_oid *iname, size_t index_len,
			  const struct trap_object *trapobj, size_t trapobjlen,
			  uint8_t sptrap)
{
	oid objid_snmptrap[] = {1, 3, 6, 1, 6, 3, 1, 1, 4, 1, 0};
	size_t objid_snmptrap_len = sizeof(objid_snmptrap) / sizeof(oid);
	oid notification_oid[MAX_OID_LEN];
	size_t notification_oid_len;
	unsigned int i;

	netsnmp_variable_list *notification_vars = NULL;
	if (!agentx_enabled)
		return 0;

	/* snmpTrapOID */
	oid_copy(notification_oid, ename, enamelen);
	notification_oid[enamelen] = sptrap;
	notification_oid_len = enamelen + 1;
	snmp_varlist_add_variable(&notification_vars, objid_snmptrap,
				  objid_snmptrap_len, ASN_OBJECT_ID,
				  (uint8_t *)notification_oid,
				  notification_oid_len * sizeof(oid));

	/* Provided bindings */
	for (i = 0; i < trapobjlen; i++) {
		unsigned int j;
		oid oid[MAX_OID_LEN];
		size_t oid_len, onamelen;
		uint8_t *val;
		size_t val_len;
		WriteMethod *wm = NULL;
		struct variable cvp;
		unsigned int iindex;
		/*
		 * this allows the behaviour of smux_trap with a singe index
		 * for all objects to be maintained whilst allowing traps which
		 * have different indices per object to be supported
		 */
		iindex = (index_len == 1) ? 0 : i;

		/* Make OID. */
		if (trapobj[i].namelen > 0) {
			/* Columnar object */
			onamelen = trapobj[i].namelen;
			oid_copy(oid, name, namelen);
			oid_copy(oid + namelen, trapobj[i].name, onamelen);
			oid_copy(oid + namelen + onamelen,
				 iname[iindex].indexname,
				 iname[iindex].indexlen);
			oid_len = namelen + onamelen + iname[iindex].indexlen;
		} else {
			/* Scalar object */
			onamelen = trapobj[i].namelen * (-1);
			oid_copy(oid, name, namelen);
			oid_copy(oid + namelen, trapobj[i].name, onamelen);
			oid[onamelen + namelen] = 0;
			oid_len = namelen + onamelen + 1;
		}

		/* Locate the appropriate function and type in the MIB registry.
		 */
		for (j = 0; j < vp_len; j++) {
			if (oid_compare(trapobj[i].name, onamelen, vp[j].name,
					vp[j].namelen)
			    != 0)
				continue;
			/* We found the appropriate variable in the MIB
			 * registry. */
			oid_copy(cvp.name, name, namelen);
			oid_copy(cvp.name + namelen, vp[j].name, vp[j].namelen);
			cvp.namelen = namelen + vp[j].namelen;
			cvp.type = vp[j].type;
			cvp.magic = vp[j].magic;
			cvp.acl = vp[j].acl;
			cvp.findVar = vp[j].findVar;

			/* Grab the result. */
			val = cvp.findVar(&cvp, oid, &oid_len, 1, &val_len,
					  &wm);
			if (!val)
				break;
			snmp_varlist_add_variable(&notification_vars, oid,
						  oid_len, vp[j].type, val,
						  val_len);
			break;
		}
	}


	send_v2trap(notification_vars);
	snmp_free_varbind(notification_vars);
	agentx_events_update();
	return 1;
}

/*
 * smux_events_update：对外暴露的事件同步入口，转调
 * agentx_events_update。
 */
void smux_events_update(void)
{
	agentx_events_update();
}

/*
 * smux_events_delete_thread：events 链表销毁时的元素清理回调，
 * 释放单个监听记录占用的内存。
 */
static void smux_events_delete_thread(void *arg)
{
	XFREE(MTYPE_TMP, arg);
}

/*
 * smux_terminate：终止清理。设置链表元素删除回调后删除整个 events
 * 链表，释放所有残留监听项。
 */
void smux_terminate(void)
{
	if (events) {
		events->del = smux_events_delete_thread;
		list_delete(&events);
	}
}
/*
 * 以上实现仅在启用 SNMP_AGENTX 时编译，否则本文件内容为空。
 */
#endif /* SNMP_AGENTX */
