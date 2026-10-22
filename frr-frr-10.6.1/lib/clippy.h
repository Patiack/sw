// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * clippy (CLI preparator in python)
 * Copyright (C) 2016-2017  David Lamparter for NetDEF, Inc.
 */

#ifndef _FRR_CLIPPY_H
#define _FRR_CLIPPY_H

/* ============================================================================
 * 【文件导读】clippy.h —— CLI 文档生成器 “clippy” 的 Python 嵌入式接口
 * ============================================================================
 * 【clippy 是干什么的】FRR 的命令文档(clippy 源文件里那种 `CLIPPY(...)` 注解)
 * 要在**构建期**被解析、生成 man 手册与 HTML 文档。解析器本身是用 Python 写的，
 * 但被编译进一个 C 程序里运行——本头文件就是这两侧之间的接口声明。
 * 【注意名字的来源】原注释 "clippy (CLI preparator in python)" 说明它是
 * “CLI 预处理器”，与那个著名办公助手只是个巧合的重名。
 *
 * 【三个入口的分工】
 *   clippy_parse      : Python 侧调用，处理一个 clippy 单元(通常是源文件片段)。
 *   command_py_init   : Python 模块初始化钩子(PyMODINIT_FUNC)，注册本 C 扩展。
 *   elf_py_init       : ELF 解析辅助模块的初始化(供 clippy 读取可执行文件里的
 *                      命令定义/符号信息)。
 * 前两者属于“把 clippy 跑起来”，后者属于“给 clippy 提供额外数据源”。
 *
 * 【把 Python.h 放在公共头里的代价】凡是 include 本头文件的编译单元都会牵连
 * Python 开发头文件，编译慢且要求 Python 环境；因此本头文件的**包含范围很窄**，
 * 只在构建期工具(如 clippy、command_py.c、elf_py.c)里使用，**不要**在守护进程
 * 的公共路径上引用它。
 * ============================================================================
 */
#include <stdbool.h>
#include <Python.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 【接口】处理一个 clippy 解析单元。self/args 是 Python C-API 的固定调用约定
 * (self 为模块对象，args 为参数元组)；返回新的 Python 对象，失败返回 NULL 并
 * 置 Python 异常。**调用方必须按 Python C-API 规则处理引用计数**。 */
extern PyObject *clippy_parse(PyObject *self, PyObject *args);
/* 【接口】Python 模块初始化函数，按 CPython 约定无参数、返回模块对象。
 * 注意它用的是 PyMODINIT_FUNC(而不是普通的 extern)，由 Python 头文件展开成
 * “正确的导出符号与调用约定”，**手写声明容易搞错，应保留这个宏**。 */
extern PyMODINIT_FUNC command_py_init(void);
/* 【接口】初始化 elf 辅助 Python 模块。pymod 为已创建的 Python 模块对象；
 * 返回 true 表示初始化成功。失败时**不抛 Python 异常**，由调用方自行报错。 */
extern bool elf_py_init(PyObject *pymod);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_CLIPPY_H */
