// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * 文件导读：libfrr_trace.c —— LTTng 追踪探针的“单一定义单元”
 *
 * 本文件只有 8 行，却是把 libfrr_trace.h 里声明的探针从“声明”落成真实编译产物的收尾文件。
 *
 * 背景：libfrr_trace.h 用 TRACEPOINT_EVENT 系列宏声明了大量探针（hash_get、memalloc、
 * list_add 等），但这些宏展开后必须恰好有一个翻译单元来生成真正的追踪函数体与元数据，
 * 否则会出现重复定义或未定义符号。本文件就是这个唯一的落地点。
 *
 * 两个宏的作用：
 *   TRACEPOINT_CREATE_PROBES —— 让被包含的头文件展开出探针的实现（函数定义与元数据），
 *                               而不是普通的 extern 声明。
 *   TRACEPOINT_DEFINE       —— 向 LTTng 标记“这里就是探针的最终定义点”。
 *   顺序很关键：必须先 CREATE_PROBES 再 DEFINE。
 *
 * 依赖关系：本文件必须且只能编译一次；其它 .c 文件只需 include libfrr_trace.h 来获得探针的
 * 声明与调用桩，绝不能再定义 TRACEPOINT_DEFINE。libfrr_trace.h 内部被 HAVE_LTTNG 包住，
 * 未启用 LTTng 时这些宏与探针全部为空，整个追踪系统零开销退化为空操作。
 *
 * 阅读建议：先看 libfrr_trace.h 了解探针种类与字段，再回本文件看这两个宏的先后顺序，
 * 最后结合 memory.c / hash.c / linklist.c / table.c 里的调用点理解追踪的落点。
 */
/* 展开出探针的真实实现与元数据；必须先于 TRACEPOINT_DEFINE */
#define TRACEPOINT_CREATE_PROBES
/* 标记此处为本工程所有探针的唯一最终定义点（整个工程只能出现一次） */
#define TRACEPOINT_DEFINE

#include <zebra.h>

/* 包含探针声明；配合上面的两个宏，在此展开出全部探针实现 */
#include "libfrr_trace.h"
