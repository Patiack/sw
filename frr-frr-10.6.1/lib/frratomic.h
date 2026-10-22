// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2015-16  David Lamparter, for NetDEF, Inc.
 */

/*
 * 文件导读（lib/frratomic.h）
 *
 * 本文件是 FRR 对 C11 原子操作头 <stdatomic.h> 的一层极薄封装。
 * FRR 主体用 C 编写，直接使用 C11 的 atomic_* 系列类型与 memory_order；
 * 但部分代码（测试、C++ 绑定库等）需用 C++ 编译，而 C++ 的原子设施位
 * 于 <atomic> 且命名在 std 命名空间下，二者接口并不兼容。
 *
 * 本文件职责：在 C++ 编译时，把 std:: 下的原子类型与内存序枚举引入全
 * 局命名空间，并补齐 C11 风格的类型别名（atomic_bool、atomic_size_t、
 * atomic_uint_fast32_t、atomic_uintptr_t），使同一份源码在 C 与 C++ 下
 * 都能使用统一的原子操作名称。
 *
 * 核心内容：
 *   - __cplusplus 分支：包含 <atomic>，using 引入 std::atomic_int 与各
 *     memory_order，并 typedef 出 C 风格别名。
 *   - 否则分支：直接包含 <stdatomic.h>。
 *
 * 阅读顺序：先看顶部宏名与版权，再看 __cplusplus 条件编译的两条分支。
 * 本文件不含运行期逻辑，纯类型 / 宏声明。
 */

#ifndef _FRRATOMIC_H
#define _FRRATOMIC_H

/* C++ 兼容分支：C++ 无 <stdatomic.h>，改用 <atomic> 并把名字引入全局。 */
/* C++ compatibility */
#ifdef __cplusplus
#include <stdint.h>
#include <atomic>
/* 把 std 命名空间下的整型原子类型与内存序枚举引入全局，便于 C / C++
 * 共用同一套名字。 */
using std::atomic_int;
using std::memory_order;
using std::memory_order_relaxed;
using std::memory_order_acquire;
using std::memory_order_release;
using std::memory_order_acq_rel;
using std::memory_order_consume;
using std::memory_order_seq_cst;

/* C11 风格类型别名：用 std::atomic<T> 实例化出与 C 侧同名的原子类型。 */
typedef std::atomic<bool>		atomic_bool;
typedef std::atomic<size_t>		atomic_size_t;
typedef std::atomic<uint_fast32_t>	atomic_uint_fast32_t;
typedef std::atomic<uintptr_t>		atomic_uintptr_t;

/* C 编译分支：直接用标准 C11 原子头。 */
#else
#include <stdatomic.h>
#endif

#endif /* _FRRATOMIC_H */
