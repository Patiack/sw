// SPDX-License-Identifier: GPL-2.0-or-later
/* Tracing macros
 *
 * Wraps tracepoint macros for different tracing systems to allow switching
 * between them at compile time.
 *
 * This should not be included directly by source files wishing to provide
 * tracepoints. Instead, write a header that defines LTTng tracepoints and
 * which includes this header, and include your new header in your source. USDT
 * probes do not need tracepoint definitions, but are less capable than LTTng
 * tracepoints.
 *
 * Copyright (C) 2020  NVIDIA Corporation
 * Quentin Young
 */

/*
 * 文件导读（lib/trace.h）
 *
 * 本文件把不同跟踪系统（LTTng 与 USDT）的探针宏统一包装成 frrtrace 系列
 * 宏，使上层源码写一次即可在编译期切换后端，或在未开启跟踪时退化为空
 * 操作、零开销。
 *
 * 三个核心宏：
 *   - frrtrace(nargs, provider, name, ...)：定义 / 触发一个跟踪点。nargs
 *     为参数个数，因为 USDT 用 DTRACE_PROBEn 系列宏，必须按个数展开。
 *   - frrtrace_enabled(provider, name)：探针是否启用；LTTng 映射为
 *     tracepoint_enabled()，USDT 恒为 true。
 *   - frrtracelog(level, msg, ...)：仅用于 zlog 核心，把日志转发给 LTTng，
 *     其它情况下展开为空。
 *
 * 使用方法：不要在源文件里直接包含本头。应新建一个头文件定义 LTTng
 * tracepoint，并让它包含本文件；再在源文件中包含你的新头。USDT 探针无需
 * 预定义 tracepoint，但能力弱于 LTTng。
 *
 * 阅读顺序：先看三段预处理器分支（HAVE_LTTNG / HAVE_USDT / 其它），理解
 * 同一组宏在三种构建配置下分别展开成什么。
 */

#ifndef _TRACE_H_
#define _TRACE_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

/*
 * Provided here:
 * - frrtrace(n, provider, name, ...args...)
 * - frrtrace_enabled(provider, name)
 * - frrtracelog(level, msg, ...)
 *
 * Use frrtrace() to define tracepoints. n is the number of arguments; this is
 * needed because USDT probe definitions use DTRACE_PROBEn macros, so the
 * number of args must be passed in order to expand the correct macro.
 *
 * frrtrace_enabled() maps to tracepoint_enabled() under LTTng and is always
 * true when using USDT. In the future it could be mapped to USDT semaphores
 * but this is not implemented at present.
 *
 * frrtracelog() maps to tracelog() under LTTng and should only be used in zlog
 * core code, to propagate zlog messages to LTTng. It expands to nothing
 * otherwise.
 */

/* LTTng 后端：直接转发到原生的 tracepoint 宏，功能最完整。 */
#if defined(HAVE_LTTNG)

#define frrtrace(nargs, provider, name, ...) \
	tracepoint(provider, name, ## __VA_ARGS__)
#define frrtrace_enabled(...) tracepoint_enabled(__VA_ARGS__)
#define frrtracelog(...) tracelog(__VA_ARGS__)

/* USDT 后端：用 DTRACE_PROBEn 宏展开探针；enabled 恒为 true，
 * tracelog 无对应实现故展开为空。 */
#elif defined(HAVE_USDT)

#include "sys/sdt.h"

#define frrtrace(nargs, provider, name, ...) \
	DTRACE_PROBE##nargs(provider, name, ## __VA_ARGS__)
#define frrtrace_enabled(...) true
#define frrtracelog(...)

/* 未开启任何跟踪：全部退化为可被优化掉的空操作。 */
#else

#define frrtrace(nargs, provider, name, ...) (void)0
#define frrtrace_enabled(...) false
#define frrtracelog(...) (void)0

#endif

#endif /* _TRACE_H_ */
