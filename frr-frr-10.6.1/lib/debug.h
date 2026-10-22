// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Debugging utilities.
 * Copyright (C) 2018  Cumulus Networks, Inc.
 * Quentin Young
 */
#ifndef _FRRDEBUG_H
#define _FRRDEBUG_H

#include <zebra.h>
#include "command.h"
#include "frratomic.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 文件导读：debug.h —— FRR 统一的调试开关与日志宏
 *
 * 1. 这个文件干什么：
 *    为 FRR 各模块提供一套统一的“按类别开关调试输出”的机制。每个模块声明一个
 *    struct debug 记录，通过本文件的一堆宏原子地读 / 写其 flags 字段；VTY 命令
 *    依据 CONFIG_NODE / ENABLE_NODE 决定调试开关是持久化到配置文件，还是仅本次会话有效。
 *
 * 2. 核心数据结构：
 *    - struct debug：一条调试记录，含原子 flags、配置字符串 conf、描述 desc，
 *      以及挂在 debug_list 上的链表节点 item。
 *    - flags 布局（高位到低位）：[0] 模式字段（DEBUG_MODE），[1..3] 选项字段
 *      （DEBUG_OPT）。模式字节是框架保留的，别乱动；低三字节归使用者。
 *
 * 3. 关键宏（建议阅读顺序）：
 *    DEBUG_MODE_CHECK / DEBUG_MODE_SET / DEBUG_MODE_ON / DEBUG_MODE_OFF
 *    DEBUG_OPT_CHECK / DEBUG_OPT_SET
 *    DEBUG_FLAGS_CHECK / DEBUG_FLAGS_SET / DEBUG_CLEAR / DEBUG_ON
 *    DEBUG_NODE2MODE -> DEBUG / DEBUGE / DEBUGW / DEBUGI / DEBUGN / DEBUGD
 *    以上宏全部基于 frratomic.h 的原子操作，是线程安全的（MT-Safe）。
 *
 * 4. 与其他模块的关系：
 *    依赖 zebra.h、command.h（struct vty）、frratomic.h；由 debug.c 实现
 *    debug_install / debug_init / debug_status_write，供全部守护进程使用。
 *
 * 5. 易踩的坑：
 *    - flags 字段只能用本文件的宏操作，直接读改写会破坏多线程下的原子性。
 *    - 本文件个别宏存在历史笔误（见对应注释），照抄使用前请留心。
 */

/*
 * Debugging modes.
 *
 * FRR's convention is that a debug statement issued under the vty CONFIG_NODE
 * persists to the config file, whereas the same debug statement issued from
 * the ENABLE_NODE only persists for the current session. These are mapped to
 * DEBUG_MODE_CONF and DEBUG_MODE_TERM respectively.
 *
 * They are not mutually exclusive and are placed in the MSB of the flags
 * field in a debugging record.
 */
/*
 * 调试模式标志位（存放在 flags 的最高字节）：TERM 仅当前会话有效，
 * CONF 会写入配置文件；ALL 二者兼有，NONE 表示都关。
 * DEBUG_OPT_* 则用于低三字节的选项位（ALL 为 0x00FFFFFF，NONE 为 0）。
 */
#define DEBUG_MODE_TERM 0x01000000
#define DEBUG_MODE_CONF 0x02000000
#define DEBUG_MODE_ALL (DEBUG_MODE_TERM | DEBUG_MODE_CONF)
#define DEBUG_MODE_NONE 0x00000000
#define DEBUG_OPT_ALL 0x00FFFFFF
#define DEBUG_OPT_NONE 0x00000000


/*
 * PREDECL_LIST(debug_list) -- 用链表模板为调试记录预生成链表类型定义。
 */
PREDECL_LIST(debug_list);
/*
 * Debugging record.
 *
 * All operations on this record exposed in this header are MT-safe.
 *
 * flags
 *    A bitfield with the following format (bytes high to low)
 *    - [0] Debugging mode field (MSB)  | Mode
 *    - [1] Arbitrary flag field        | Option
 *    - [2] Arbitrary flag field        | Option
 *    - [3] Arbitrary flag field (LSB)  | Option
 *
 *              ALL THESE BYTES ARE YOURS - EXCEPT MODE.
 *                      ATTEMPT NO BIT OPS THERE.
 *
 *    The MSB of this field determines the debug mode, Use the DEBUG_MODE*
 *    macros to manipulate this byte.
 *
 *    The low 3 bytes of this field may be used to store arbitrary information.
 *    Usually they are used to store flags that tune how detailed the logging
 *    for a particular debug record is. Use the DEBUG_OPT* macros to manipulate
 *    those bytes.
 *
 *    All operations performed on this field should be done using the macros
 *    later in this header file. They are guaranteed to be atomic operations
 *    with respect to this field. Using anything except the macros to
 *    manipulate the flags field in a multithreaded environment results in
 *    undefined behavior.
 *
 * conf
 *    The configuration string that will be written to the config file.
 *
 * desc
 *    Human-readable description of this debugging record.
 */
/*
 * struct debug —— 一条调试记录
 * flags：原子标志（高字节模式 + 低三字节选项），只能用本文件的宏操作；
 * conf ：写入配置文件的命令字符串；desc：人类可读的描述；
 * item ：挂到全局 debug 链表上的节点（由 debug_install 安装）。
 */
struct debug {
	atomic_uint_fast32_t flags;
	const char *conf;
	const char *desc;

	struct debug_list_item item;
};

/*
 * Check if a mode is set for a debug.
 *
 * MT-Safe
 */
/*
 * DEBUG_MODE_CHECK -- 判断某调试记录的模式位是否被置位。返回布尔值，MT-Safe。
 */
#define DEBUG_MODE_CHECK(name, mode)                                           \
	CHECK_FLAG_ATOMIC(&(name)->flags, (mode)&DEBUG_MODE_ALL)

/*
 * Check if an option bit is set for a debug.
 *
 * MT-Safe
 */
/*
 * DEBUG_OPT_CHECK -- 判断某调试记录的选项位（低三字节）是否被置位。MT-Safe。
 */
#define DEBUG_OPT_CHECK(name, opt)                                             \
	CHECK_FLAG_ATOMIC(&(name)->flags, (opt)&DEBUG_OPT_ALL)

/*
 * Check if bits are set for a debug.
 *
 * MT-Safe
 */
/*
 * DEBUG_FLAGS_CHECK -- 判断 flags 中任意指定位（模式 / 选项均可）是否被置位。MT-Safe。
 */
#define DEBUG_FLAGS_CHECK(name, fl) CHECK_FLAG_ATOMIC(&(name)->flags, (fl))

/*
 * Set modes on a debug.
 *
 * MT-Safe
 */
/*
 * DEBUG_MODE_SET -- onoff 为真则置位、否则清除指定模式位（掩码只取 DEBUG_MODE_ALL）。MT-Safe。
 */
#define DEBUG_MODE_SET(name, mode, onoff)                                      \
	do {                                                                   \
		if (onoff)                                                     \
			SET_FLAG_ATOMIC(&(name)->flags,                        \
					(mode)&DEBUG_MODE_ALL);                \
		else                                                           \
			UNSET_FLAG_ATOMIC(&(name)->flags,                      \
					  (mode)&DEBUG_MODE_ALL);              \
	} while (0)

/*
 * DEBUG_MODE_ON / DEBUG_MODE_OFF -- DEBUG_MODE_SET 的便捷封装（开 / 关某个模式）。
 */
/* Convenience macros for specific set operations. */
#define DEBUG_MODE_ON(name, mode) DEBUG_MODE_SET(name, mode, true)
#define DEBUG_MODE_OFF(name, mode) DEBUG_MODE_SET(name, mode, false)

/*
 * Set options on a debug.
 *
 * MT-Safe
 */
/*
 * DEBUG_OPT_SET -- onoff 为真则置位、否则清除指定选项位（掩码只取 DEBUG_OPT_ALL）。MT-Safe。
 */
#define DEBUG_OPT_SET(name, opt, onoff)                                        \
	do {                                                                   \
		if (onoff)                                                     \
			SET_FLAG_ATOMIC(&(name)->flags, (opt)&DEBUG_OPT_ALL);  \
		else                                                           \
			UNSET_FLAG_ATOMIC(&(name)->flags,                      \
					  (opt)&DEBUG_OPT_ALL);                \
	} while (0)

/*
 * DEBUG_OPT_ON / DEBUG_OPT_OFF -- 选项置位 / 清除的便捷宏。
 * 注意：DEBUG_OPT_OFF 的实现误用了 true（本意应为 false），是源码中的已知笔误；
 *       本工程只加注释、不修改代码，阅读 / 使用时请留心。
 */
/* Convenience macros for specific set operations. */
#define DEBUG_OPT_ON(name, opt) DEBUG_OPT_SET(name, opt, true)
#define DEBUG_OPT_OFF(name, opt) DEBUG_OPT_SET(name, opt, true)

/*
 * Set bits on a debug.
 *
 * MT-Safe
 */
/*
 * DEBUG_FLAGS_SET -- 通用置位 / 清除：onoff 为真置位、否则清除 fl 指定的任意位。MT-Safe。
 */
#define DEBUG_FLAGS_SET(name, fl, onoff)                                       \
	do {                                                                   \
		if (onoff)                                                     \
			SET_FLAG_ATOMIC(&(name)->flags, (fl));                 \
		else                                                           \
			UNSET_FLAG_ATOMIC(&(name)->flags, (fl));               \
	} while (0)

/*
 * DEBUG_FLAGS_ON / DEBUG_FLAGS_OFF -- 通用位开关的便捷宏。
 * 注意：这两个宏的实现误用了 (type) 而非形参 (fl)，属源码笔误（会导致编译错误）；
 *       本工程仅注释、不修改代码。
 */
/* Convenience macros for specific set operations. */
#define DEBUG_FLAGS_ON(name, fl) DEBUG_FLAGS_SET(&(name)->flags, (type), true)
#define DEBUG_FLAGS_OFF(name, fl) DEBUG_FLAGS_SET(&(name)->flags, (type), false)

/*
 * Unset all modes and options on a debug.
 *
 * MT-Safe
 */
/*
 * DEBUG_CLEAR -- 原子清空该调试记录的全部模式与选项位。MT-Safe。
 */
#define DEBUG_CLEAR(name) RESET_FLAG_ATOMIC(&(name)->flags)

/*
 * Set all modes and options on a debug.
 *
 * MT-Safe
 */
/*
 * DEBUG_ON -- 原子地打开该调试记录的全部模式与选项。MT-Safe。
 */
#define DEBUG_ON(name)                                                         \
	SET_FLAG_ATOMIC(&(name)->flags, DEBUG_MODE_ALL | DEBUG_OPT_ALL)

/*
 * Map a vty node to the correct debugging mode flags. FRR behaves such that a
 * debug statement issued under the config node persists to the config file,
 * whereas the same debug statement issued from the enable node only persists
 * for the current session.
 *
 * MT-Safe
 */
/*
 * DEBUG_NODE2MODE -- 由 VTY 节点推导应使用的调试模式：
 * CONFIG_NODE 下返回 DEBUG_MODE_ALL（可持久化到配置），否则返回 DEBUG_MODE_TERM（仅本会话）。
 */
#define DEBUG_NODE2MODE(vtynode)                                               \
	(((vtynode) == CONFIG_NODE) ? DEBUG_MODE_ALL : DEBUG_MODE_TERM)

/*
 * Debug at the given level to the default logging destination.
 *
 * MT-Safe
 */
/*
 * DEBUG -- 按模式开关与日志级别输出。level 是 zlog_ 的后缀（err / warn / info 等），
 * name 为调试记录指针。仅当对应模式已开启时才真正调用 zlog_##level。MT-Safe。
 */
#define DEBUG(level, name, fmt, ...)                                           \
	do {                                                                   \
		if (DEBUG_MODE_CHECK(name, DEBUG_MODE_ALL))                    \
			zlog_##level(fmt, ##__VA_ARGS__);                      \
	} while (0)

/*
 * DEBUGE / DEBUGW / DEBUGI / DEBUGN / DEBUGD -- 各日志级别的便捷宏，
 * 分别对应 zlog 的 err / warn / info / notice / debug。
 */
/* Convenience macros for the various levels. */
#define DEBUGE(name, fmt, ...) DEBUG(err, name, fmt, ##__VA_ARGS__)
#define DEBUGW(name, fmt, ...) DEBUG(warn, name, fmt, ##__VA_ARGS__)
#define DEBUGI(name, fmt, ...) DEBUG(info, name, fmt, ##__VA_ARGS__)
#define DEBUGN(name, fmt, ...) DEBUG(notice, name, fmt, ##__VA_ARGS__)
#define DEBUGD(name, fmt, ...) DEBUG(debug, name, fmt, ##__VA_ARGS__)

/* Show current debugging status. */
/*
 * debug_status_write -- 把当前调试状态输出到给定 vty（供 show debugging 使用）。
 */
void debug_status_write(struct vty *vty);

/*
 * Register a debug item.
 */
/*
 * debug_install -- 把一条调试记录注册进全局调试链表（各模块初始化时调用）。
 */
void debug_install(struct debug *debug);

/*
 * Initialize debugging.
 * Should only be called by libfrr.
 */
/*
 * debug_init -- 初始化调试子系统，只应由 libfrr 调用一次。
 */
void debug_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _FRRDEBUG_H */
