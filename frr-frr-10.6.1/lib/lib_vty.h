// SPDX-License-Identifier: GPL-2.0-or-later
/* Memory management routine
 * Copyright (C) 1998 Kunihiro Ishiguro
 */

/* ============================================================================
 * lib_vty.h —— 公共库 lib_vty.c 的对外接口声明
 *
 * 本头文件只对外暴露两件事：
 *   1) lib_cmd_init()：各 FRR 守护进程在启动时调用的初始化钩子，由它注册一批
 *      所有守护进程通用的 VTY 命令（frr defaults、frr version，
 *      以及配置读取起止标记 XFRR_start_configuration / XFRR_end_configuration）。
 *   2) mtype_memstr()：把字节数格式化成人类可读的字符串（如 110kB、11GiB），
 *      供 show memory 一类命令输出使用。
 *
 * 阅读提示：
 *   - 头文件本身不含逻辑，真正的实现都在同目录的 lib_vty.c。
 *   - MTYPE_MEMSTR_LEN 是调用 mtype_memstr() 时推荐的缓冲区长度；该函数可能
 *     直接就地写入传入缓冲，长度不足会被 snprintf 截断。
 *   - 本文件用 extern "C" 包裹，便于 C++ 代码链接。
 * ============================================================================
 */

#ifndef _ZEBRA_LIB_VTY_H
#define _ZEBRA_LIB_VTY_H

#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 库级 VTY 命令初始化入口。
 * 由各守护进程在启动阶段调用一次，注册所有守护进程共用的命令节点
 * （frr defaults / frr version / 配置读写起止标记等）。
 * 参数：无。返回：无。副作用：向命令树安装多个 cmd_element，并注册变量补全。 */
extern void lib_cmd_init(void);

/* 把字节数转成人类友好的字符串。
 * 参数：buf / len 为调用方提供的输出缓冲区及其长度；bytes 为待格式化的字节数。
 * 返回：可能返回 buf 本身，也可能返回常量字符串（如 "0 bytes"）或静态存储，
 *       因此调用方不要修改返回值，也不要假设它长期有效（后续调用可能覆盖）。
 * 格式：最多给出 4 位有效数字，如 "2048 bytes"、"110 KiB"、"11 GiB"。 */
/* Human friendly string for given byte count */
#define MTYPE_MEMSTR_LEN 20
extern const char *mtype_memstr(char *buf, size_t len, unsigned long bytes);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_LIB_VTY_H */
