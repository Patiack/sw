// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * June 13 2025, Christian Hopps <chopps@labn.net>
 *
 * Copyright (c) 2025, LabN Consulting, L.L.C.
 *
 */
/* ============================================================================
 * 【文件导读】host_nb.h —— frr-host 的 YANG 模块信息与全局 host 的外部声明
 * ============================================================================
 * 【只有两个 extern，别小看它们】
 *   frr_host_nb_info : 模块的节点/回调表(定义在 host_nb.c)，各 daemon 在
 *                      注册北向模块时要引用的就是它。
 *   host             : **全局唯一的 host 实例**，真正的"定义"在 lib 的某处
 *                      (command.c 附近)，这里是给别处用的外部声明。
 * 把 frr_host_nb_info 做成 extern 而不是 static，是因为需要在多个编译单元
 * (各 daemon 或 lib 的注册流程)中被引用，说明它的可见性是有意放开的。
 *
 * 【注意】本文件 `#include "command.h"` 只是为了拿到 `struct host` 的完整
 * 定义；外部声明 host 变量时若只有不完整类型也能编译，但要访问其成员就必须
 * 有完整定义。
 * ============================================================================
 */
#include <zebra.h>
#include "command.h"

/* 【外部声明】frr-host 的 YANG 模块注册信息，用于向 northbound 框架注册本模块。 */
extern const struct frr_yang_module_info frr_host_nb_info;
/* 【外部声明】全局 host 配置结构体，其完整定义见 command.h。 */
extern struct host host;
