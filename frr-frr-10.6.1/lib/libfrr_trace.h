// SPDX-License-Identifier: GPL-2.0-or-later
/* Tracing
 *
 * Copyright (C) 2020  NVIDIA Corporation
 * Quentin Young
 */

/*
 * 文件导读：libfrr_trace.h —— FRR 公共库的 LTTng 追踪探针清单
 *
 * 本头文件用 LTTng 的 TRACEPOINT_EVENT 系列宏，声明了 libfrr 中被追踪的关键事件：
 *   - 哈希表：hash_get / hash_insert / hash_release
 *   - 事件循环：由事件类 thread_operation 派生出 schedule_timer / schedule_event /
 *     schedule_read / schedule_write / event_cancel / event_cancel_async / event_call
 *   - 线程：frr_pthread_run / frr_pthread_stop
 *   - 内存：memalloc / memfree
 *   - 链表：list_add / list_remove / list_delete_node / list_sort
 *   - 路由表：route_node_get
 *
 * 结构说明：
 *   TRACEPOINT_PROVIDER 定义为 frr_libfrr，是探针提供者的名字；
 *   TRACEPOINT_EVENT 定义一个带字段（TP_FIELDS）的事件，字段用 ctf_string /
 *   ctf_integer / ctf_integer_hex 描述，便于追踪工具按类型解析；
 *   TRACEPOINT_EVENT_CLASS 加 TRACEPOINT_EVENT_INSTANCE 用于“同一套参数、多个事件名”的复用，
 *   thread_operation 就是这样一个类，配合 THREAD_OPERATION_TRACEPOINT_INSTANCE 批量生成实例。
 *
 * 条件编译：整段内容被 HAVE_LTTNG 包住。未启用 LTTng 时所有宏退化为空，
 * 调用点也随之消失，运行时零开销。
 *
 * 与其它文件的关系：本文件只负责声明，真正的实现定义在 libfrr_trace.c。
 * 调用点分布在 memory.c（memalloc / memfree）、hash.c、linklist.c、table.c、
 * frrevent.c（事件循环）等处，是观察 FRR 运行时行为的窗口。
 */
/* 头文件保护：LTTng 要求本头文件可被多次读取以展开事件定义，
 * 因此除常规的一次性保护外，还额外允许 TRACEPOINT_HEADER_MULTI_READ 触发的重入包含。 */
#if !defined(_LIBFRR_TRACE_H_) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define _LIBFRR_TRACE_H_

/* trace.h 在未启用 LTTng 时提供空宏定义，保证调用点无需条件编译即可编译通过 */
#include "trace.h"

/* 以下整段仅在编译时启用了 LTTng 才生效；否则本头文件几乎为空 */
#ifdef HAVE_LTTNG

#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER frr_libfrr

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "./libfrr_trace.h"

#include <lttng/tracepoint.h>

#include "hash.h"
#include "frrevent.h"
#include "memory.h"
#include "linklist.h"
#include "table.h"

/* clang-format off */

/* 探针：哈希表查表 hash_get。记录表名、桶数量、元素总数与查找的 data 指针 */
TRACEPOINT_EVENT(
	frr_libfrr,
	hash_get,
	TP_ARGS(struct hash *, hash, void *, data),
	TP_FIELDS(
		ctf_string(name, hash->name ? hash->name : "(unnamed)")
		ctf_integer(unsigned int, index_size, hash->size)
		ctf_integer(unsigned long, item_count, hash->count)
		ctf_integer_hex(intptr_t, data_ptr, data)
	)
)

TRACEPOINT_LOGLEVEL(frr_libfrr, hash_get, TRACE_INFO)

/* 探针：哈希插入 hash_insert，相较查表额外记录 key 值 */
TRACEPOINT_EVENT(
	frr_libfrr,
	hash_insert,
	TP_ARGS(struct hash *, hash, void *, data, unsigned int, key),
	TP_FIELDS(
		ctf_string(name, hash->name ? hash->name : "(unnamed)")
		ctf_integer(unsigned int, key, hash->size)
		ctf_integer(unsigned int, index_size, hash->size)
		ctf_integer(unsigned long, item_count, hash->count)
		ctf_integer_hex(intptr_t, data_ptr, data)
	)
)

TRACEPOINT_LOGLEVEL(frr_libfrr, hash_insert, TRACE_INFO)

/* 探针：哈希释放 hash_release，记录被移除的元素 released_item */
TRACEPOINT_EVENT(
	frr_libfrr,
	hash_release,
	TP_ARGS(struct hash *, hash, void *, data, void *, released_item),
	TP_FIELDS(
		ctf_string(name, hash->name ? hash->name : "(unnamed)")
		ctf_integer(unsigned int, index_size, hash->size)
		ctf_integer(unsigned long, item_count, hash->count)
		ctf_integer_hex(intptr_t, data_ptr, data)
		ctf_integer_hex(intptr_t, released_item, data)
	)
)

TRACEPOINT_LOGLEVEL(frr_libfrr, hash_release, TRACE_INFO)

/* 事件循环探针共用的参数表：循环名、函数名、调度来源文件与行号、
 * event 指针、fd、event 值、参数指针、定时器时长 */
#define THREAD_SCHEDULE_ARGS                                                   \
	TP_ARGS(const char *, loopname, const char *, funcname,        \
		const char *, schedfrom, int, fromln, struct event **,        \
		thread_ptr, int, fd, int, val, void *, arg, long, time)

/* 事件类 thread_operation：定义一套字段布局，供下面多个事件实例复用 */
TRACEPOINT_EVENT_CLASS(
	frr_libfrr,
	thread_operation,
	THREAD_SCHEDULE_ARGS,
	TP_FIELDS(
		ctf_string(threadmaster_name, loopname)
		ctf_string(function_name, funcname ? funcname : "(unknown function)")
		ctf_string(scheduled_from, schedfrom ? schedfrom : "(unknown file)")
		ctf_integer(int, scheduled_on_line, fromln)
		ctf_integer_hex(intptr_t, thread_addr, thread_ptr ? *thread_ptr : NULL)
		ctf_integer(int, file_descriptor, fd)
		ctf_integer(int, event_value, val)
		ctf_integer_hex(intptr_t, argument_ptr, arg)
		ctf_integer(long, timer, time)
	)
)

/* 宏：为一个已有事件类生成指定名字的实例，并设置日志级别为 TRACE_INFO */
#define THREAD_OPERATION_TRACEPOINT_INSTANCE(name)                             \
	TRACEPOINT_EVENT_INSTANCE(frr_libfrr, thread_operation, name,          \
				  THREAD_SCHEDULE_ARGS)                        \
	TRACEPOINT_LOGLEVEL(frr_libfrr, name, TRACE_INFO)

/* 批量实例化 7 个事件循环探针：定时器 / 事件 / 读 / 写 / 取消 / 异步取消 / 立即调用 */
THREAD_OPERATION_TRACEPOINT_INSTANCE(schedule_timer)
THREAD_OPERATION_TRACEPOINT_INSTANCE(schedule_event)
THREAD_OPERATION_TRACEPOINT_INSTANCE(schedule_read)
THREAD_OPERATION_TRACEPOINT_INSTANCE(schedule_write)
THREAD_OPERATION_TRACEPOINT_INSTANCE(event_cancel)
THREAD_OPERATION_TRACEPOINT_INSTANCE(event_cancel_async)
THREAD_OPERATION_TRACEPOINT_INSTANCE(event_call)

/* 探针：FRR 线程（frr_pthread）启动 */
TRACEPOINT_EVENT(
	frr_libfrr,
	frr_pthread_run,
	TP_ARGS(
		char *, name
	),
	TP_FIELDS(
		ctf_string(frr_pthread_name, name)
	)
)

/* 探针：FRR 线程停止 */
TRACEPOINT_EVENT(
	frr_libfrr,
	frr_pthread_stop,
	TP_ARGS(
		char *, name
	),
	TP_FIELDS(
		ctf_string(frr_pthread_name, name)
	)
)

/* 探针：内存分配，记录 MTYPE 名称、大小与返回指针 */
TRACEPOINT_EVENT(
	frr_libfrr,
	memalloc,
	TP_ARGS(
		struct memtype *, mt, void *, ptr, size_t, size
	),
	TP_FIELDS(
		ctf_string(memtype, mt->name)
		ctf_integer(size_t, size, size)
		ctf_integer_hex(intptr_t, ptr, ptr)
	)
)

/* 探针：内存释放，记录 MTYPE 名称与指针 */
TRACEPOINT_EVENT(
	frr_libfrr,
	memfree,
	TP_ARGS(
		struct memtype *, mt, void *, ptr
	),
	TP_FIELDS(
		ctf_string(memtype, mt->name)
		ctf_integer_hex(intptr_t, ptr, ptr)
	)
)

/* 探针：链表插入，记录链表指针、插入后元素数与元素指针 */
TRACEPOINT_EVENT(
	frr_libfrr,
	list_add,
	TP_ARGS(
		struct list *, list, const void *, ptr
	),
	TP_FIELDS(
		ctf_integer_hex(intptr_t, list, list)
		ctf_integer(unsigned int, count, list->count)
		ctf_integer_hex(intptr_t, ptr, ptr)
	)
)

/* 探针：链表移除 */
TRACEPOINT_EVENT(
	frr_libfrr,
	list_remove,
	TP_ARGS(
		struct list *, list, const void *, ptr
	),
	TP_FIELDS(
		ctf_integer_hex(intptr_t, list, list)
		ctf_integer(unsigned int, count, list->count)
		ctf_integer_hex(intptr_t, ptr, ptr)
	)
)

/* 探针：链表节点删除 */
TRACEPOINT_EVENT(
	frr_libfrr,
	list_delete_node,
	TP_ARGS(
		struct list *, list, const void *, node
	),
	TP_FIELDS(
		ctf_integer_hex(intptr_t, list, list)
		ctf_integer(unsigned int, count, list->count)
		ctf_integer_hex(intptr_t, node, node)
	)
)

/* 探针：链表排序，只记录链表指针与元素数 */
TRACEPOINT_EVENT(
	frr_libfrr,
	list_sort,
	TP_ARGS(
		struct list *, list
	),
	TP_FIELDS(
		ctf_integer_hex(intptr_t, list, list)
		ctf_integer(unsigned int, count, list->count)
	)
)

/* 探针：路由表节点获取，记录表指针与前缀字符串 */
TRACEPOINT_EVENT(
	frr_libfrr,
	route_node_get,
	TP_ARGS(
		struct route_table *, table, char *, prefix
	),
	TP_FIELDS(
		ctf_integer_hex(intptr_t, table, table)
		ctf_string(prefix, prefix)
	)
)

/* clang-format on */

#include <lttng/tracepoint-event.h>
#include <lttng/tracelog.h>

#endif /* HAVE_LTTNG */

#endif /* _LIBFRR_TRACE_H_ */
