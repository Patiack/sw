// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016-2019 Cumulus Networks, Inc.
 * Donald Sharp, Quentin Young
 */
#ifndef __FRRLUA_H__
#define __FRRLUA_H__
/*
 * ============================ 文件导读 ============================
 * frrlua.h —— FRR 内嵌 Lua 脚本引擎的「C 语言绑定」接口头文件
 *
 * 一、这个文件干什么
 *   FRR 允许用 Lua 脚本对路由做编程式控制（例如按需改写属性、做
 *   策略判断）。要让 Lua 能操作 FRR 的核心数据结构，就必须在 C 与
 *   Lua 的虚拟栈（lua_State）之间来回搬运数据。本头文件声明了这一
 *   层「胶水」函数：把 C 对象压入 Lua 栈，或从 Lua 栈取出 C 对象。
 *
 * 二、核心概念：Lua 栈
 *   Lua 与 C 的交互全靠一个虚拟栈。C 侧调用 lua_pushXXX 把值压栈，
 *   Lua 侧再从栈上取；反过来 Lua 调 C 函数时，参数也放在栈上。所有
 *   的 lua_pushX / lua_decodeX / lua_toX 都是围绕这个栈工作。
 *
 * 三、命名约定（很重要，看名字就知道行为）
 *   lua_pushXXX      C 对象 -> Lua 值（入栈），只读，不改原对象
 *   lua_decodeXXX    从栈上 idx 处取值，填充到「调用者已备好」的 C 对象
 *   lua_toXXX        从栈上 idx 处取值，内部用 MTYPE_TMP 新分配对象并
 *                    返回，调用者负责释放（多数返回 void *）
 *
 * 四、被支持的类型
 *   prefix、interface、in_addr、in6_addr、ipaddr、ethaddr、sockunion、
 *   nexthop_group、nexthop、int（integerp）、long long（longlongp）、
 *   string（stringp）。
 *
 * 五、与其他模块的关系
 *   - 依赖 lib/memory.h 的内存类型（MTYPE 体系）。
 *   - 被 frrscript.c 以及启用脚本的守护进程（如 bgpd、zebra 的脚本
 *     钩子）包含；真正的实现体全部在 frrlua.c。
 *   - frrlua_export_logging 把 zlog 日志接口导出为 Lua 的 log.*。
 *
 * 六、阅读顺序
 *   先看下方 lua_pushstring_wrapper 感受栈操作风格 -> 再看各类型的
 *   push / decode / to 三元组 -> 最后看 frrlua_table_get_integer 与
 *   frrlua_export_logging 这类「表 / 命名空间」级操作 -> 回到 frrlua.c。
 *
 * 七、易踩的坑
 *   1. 整个头文件被 #ifdef HAVE_SCRIPTING 包住，未启用脚本时不存在。
 *   2. lua_toXXX 返回的内存属于 MTYPE_TMP，必须 XFREE(MTYPE_TMP, ..)。
 *   3. lua_tointerface 返回的接口是「游离」的，未挂进全局接口树。
 *   4. 各函数都假定 Lua 栈状态正确，越界取栈会崩溃，并不返回错误码。
 * ================================================================
 */


/* 只有编译时开启了脚本支持（configure --enable-scripting）才定义
 * HAVE_SCRIPTING；未开启时本头文件的全部内容都不会参与编译。 */
#ifdef HAVE_SCRIPTING

/* 系统网络地址头：提供 struct in_addr、struct in6_addr、sockaddr 等定义。 */
#include <netinet/in.h>

/* Lua 官方解释器公共头：定义 lua_State 以及 lua_push* / lua_to* / lua_gettop
 * 等栈操作 API，本头文件所有函数都围绕它声明的栈展开。 */
#include <lua.h>

/* FRR 自己的内存类型（MTYPE）注册体系，lua_toXXX 内部用 MTYPE_TMP 分配
 * 内存，因此需要这里的声明。 */
#include "lib/memory.h"

/* 若本头文件被 C++ 编译单元包含，则按 C 的链接约定导出符号，避免
 * C++ 的名字改编（name mangling）导致链接不到实现。 */
#ifdef __cplusplus
extern "C" {
#endif

/* 前置声明：本头文件只使用这些类型的「指针」，无需含其完整定义，
 * 这样能减少头文件相互包含与编译依赖，保持接口层轻量。 */
struct interface;
struct prefix;
struct ipaddr;
struct ethaddr;
union sockunion;
struct nexthop_group;
struct nexthop;
struct buffer;

/* 声明一个 FRR 内存类型 SCRIPT_RES，用于脚本执行结果等对象的分配统计
 * 与泄漏追踪；对应的 DEFINE_MTYPE 实现位于源文件侧。 */
DECLARE_MTYPE(SCRIPT_RES);

/*
 * gcc-10 is complaining about the wrapper function
 * not being compatible with lua_pushstring returning
 * a char *.  Let's wrapper it here to make our life
 * easier
 */
/* 内联包装：把 C 字符串压入 Lua 栈。见上方英文说明——包装是为了屏蔽
 * 不同 Lua 版本中 lua_pushstring 返回类型差异引起的编译告警。 */
static inline void lua_pushstring_wrapper(lua_State *L, const char *str)
{
	(void)lua_pushstring(L, str);
}

/*
 * Converts a prefix to a Lua value and pushes it on the stack.
 */
/* 把 FRR 的 struct prefix 压入 Lua 栈（转成一个 Lua table，含 family /
 * prefixlen / 地址等字段），供 Lua 侧读取。只读，不改动 prefix。 */
void lua_pushprefix(lua_State *L, const struct prefix *prefix);

/* 从栈上 idx 处的 Lua table 解码出一个 prefix，写入调用者已备好的
 * *prefix；不做内存分配，适合栈上临时对象或已存在的字段。 */
void lua_decode_prefix(lua_State *L, int idx, struct prefix *prefix);

/*
 * Converts the Lua value at idx to a prefix.
 *
 * Returns:
 *    struct prefix allocated with MTYPE_TMP
 */
/* 从栈上 idx 处的值新分配一个 prefix（MTYPE_TMP）并返回；调用者需自行
 * 释放，否则每调用一次就泄漏一块内存。 */
void *lua_toprefix(lua_State *L, int idx);

/*
 * Converts an interface to a Lua value and pushes it on the stack.
 */
/* 把 struct interface 转成 Lua 值压栈（通常是含 name / index 等字段的
 * table），供脚本读取接口信息。 */
void lua_pushinterface(lua_State *L, const struct interface *ifp);

/* 从栈上 idx 处解码接口信息，填充进调用者提供的 *ifp。 */
void lua_decode_interface(lua_State *L, int idx, struct interface *ifp);

/*
 * Converts the Lua value at idx to an interface.
 *
 * Returns:
 *    struct interface allocated with MTYPE_TMP. This interface is not hooked
 *    to anything, nor is it inserted in the global interface tree.
 */
/* 从栈上 idx 处新分配一个 struct interface（MTYPE_TMP）并返回。注意：
 * 返回的接口是「游离」的，既未与任何东西挂钩，也未被插入全局接口树，
 * 仅是一份数据快照。 */
void *lua_tointerface(lua_State *L, int idx);

/*
 * Converts an in_addr to a Lua value and pushes it on the stack.
 */
/* 把 IPv4 地址 struct in_addr 压入 Lua 栈（以字符串或整型形式）。 */
void lua_pushinaddr(lua_State *L, const struct in_addr *addr);

/* 从栈上 idx 处解码出 IPv4 地址，写入调用者提供的 *addr。 */
void lua_decode_inaddr(lua_State *L, int idx, struct in_addr *addr);

/*
 * Converts the Lua value at idx to an in_addr.
 *
 * Returns:
 *    struct in_addr allocated with MTYPE_TMP.
 */
/* 从栈上 idx 处新分配一个 struct in_addr（MTYPE_TMP）并返回，调用者
 * 负责释放。 */
void *lua_toinaddr(lua_State *L, int idx);

/*
 * Converts an in6_addr to a Lua value and pushes it on the stack.
 */
/* 把 IPv6 地址 struct in6_addr 压入 Lua 栈。 */
void lua_pushin6addr(lua_State *L, const struct in6_addr *addr);

/* 从栈上 idx 处解码出 IPv6 地址，写入调用者提供的 *addr。 */
void lua_decode_in6addr(lua_State *L, int idx, struct in6_addr *addr);

/* 把统合地址类型 struct ipaddr（可承载 v4 或 v6）压入 Lua 栈。 */
void lua_pushipaddr(lua_State *L, const struct ipaddr *addr);

/* 把以太网 MAC 地址 struct ethaddr 压入 Lua 栈。 */
void lua_pushethaddr(lua_State *L, const struct ethaddr *addr);

/*
 * Converts the Lua value at idx to an in6_addr.
 *
 * Returns:
 *    struct in6_addr allocated with MTYPE_TMP.
 */
/* 从栈上 idx 处新分配一个 struct in6_addr（MTYPE_TMP）并返回，调用者
 * 负责释放。 */
void *lua_toin6addr(lua_State *L, int idx);

/*
 * Converts a sockunion to a Lua value and pushes it on the stack.
 */
/* 把套接字地址 union sockunion（含地址族与端口）压入 Lua 栈。 */
void lua_pushsockunion(lua_State *L, const union sockunion *su);

/* 从栈上 idx 处解码出 sockunion，写入调用者提供的 *su。 */
void lua_decode_sockunion(lua_State *L, int idx, union sockunion *su);

/*
 * Converts the Lua value at idx to a sockunion.
 *
 * Returns:
 *    sockunion allocated with MTYPE_TMP.
 */
/* 从栈上 idx 处新分配一个 union sockunion（MTYPE_TMP）并返回，调用者
 * 负责释放。 */
void *lua_tosockunion(lua_State *L, int idx);

/* 把下一跳组 struct nexthop_group 压入 Lua 栈（通常是一个数组/列表
 * 形式的 table，每个元素是一条 nexthop）。 */
void lua_pushnexthop_group(lua_State *L, const struct nexthop_group *ng);

/* 把单条下一跳 struct nexthop 压入 Lua 栈（含网关、出口接口、类型等）。 */
void lua_pushnexthop(lua_State *L, const struct nexthop *nexthop);

/*
 * Converts an int to a Lua value and pushes it on the stack.
 */
/* 把 C 的 int 压入 Lua 栈。带 p 后缀的版本统一以「指针」为参数，便于
 * 与其它 lua_pushXxx 的接口风格保持一致。 */
void lua_pushintegerp(lua_State *L, const int *num);

/* 从栈上 idx 处解码出一个 int，写入调用者提供的 *num。 */
void lua_decode_integerp(lua_State *L, int idx, int *num);

/*
 * Converts the Lua value at idx to an int.
 *
 * Returns:
 *    int allocated with MTYPE_TMP.
 */
/* 从栈上 idx 处新分配一个 int（MTYPE_TMP）并返回，调用者负责释放。 */
void *lua_tointegerp(lua_State *L, int idx);

/*
 * Converts a long long to a Lua value and pushes it on the stack.
 */
/* 把 C 的 long long 压入 Lua 栈（用于 64 位计数，如带宽/字节数）。 */
void lua_pushlonglongp(lua_State *L, const long long *num);

/* 从栈上 idx 处解码出一个 long long，写入调用者提供的 *num。 */
void lua_decode_longlongp(lua_State *L, int idx, long long *num);

/*
 * Converts the Lua value at idx to a long long.
 *
 * Returns:
 *    long long allocated with MTYPE_TMP.
 */
/* 从栈上 idx 处新分配一个 long long（MTYPE_TMP）并返回，调用者负责释放。 */
void *lua_tolonglongp(lua_State *L, int idx);

/* 从栈上 idx 处解码出字符串，拷贝进调用者提供的缓冲区 *str。调用者
 * 需保证缓冲区足够大（调用方通常先查 strlen）。 */
void lua_decode_stringp(lua_State *L, int idx, char *str);

/*
 * Pop string.
 *
 * Sets *string to a copy of the string at the top of the stack. The copy is
 * allocated with MTYPE_TMP and the caller is responsible for freeing it.
 */
/* 取出栈上 idx 处的字符串，用 MTYPE_TMP 拷一份返回；调用者负责释放。 */
void *lua_tostringp(lua_State *L, int idx);

/*
 * Retrieve an integer from table on the top of the stack.
 *
 * key
 *    Key of string value in table
 */
/* 从「栈顶那个 table」里取出名为 key 的整数值；常用于把 FRR 传入的
 * 参数 table 拆成各个字段。 */
int frrlua_table_get_integer(lua_State *L, const char *key);

/*
 * Exports a new table containing bindings to FRR zlog functions into the
 * global namespace.
 *
 * From Lua, these functions may be accessed as:
 *
 * - log.debug()
 * - log.info()
 * - log.warn()
 * - log.error()
 *
 * They take a single string argument.
 */
/* 在 Lua 全局命名空间里建一个 log 表，内含 debug / info / warn / error
 * 四个函数，它们各自转调 FRR 的 zlog 对应级别；脚本里即可写
 * log.info("...") 输出到 FRR 日志。 */
void frrlua_export_logging(lua_State *L);

/* A helper fuction that dumps the Lua stack */
/* 调试辅助：把 Lua 栈上 index 处的值（含嵌套 table）递归格式化输出到
 * buf，level 表示缩进层级。用于排查 Lua/C 数据搬运错误。 */
void lua_table_dump(lua_State *L, int index, struct buffer *buf, int level);

/*
 * Dump Lua stack to a string.
 *
 * Return value must be freed with XFREE(MTYPE_TMP, ...);
 */
/* 把整个 Lua 栈内容导成一个可读字符串返回，供调试打印；返回的内存
 * 需用 XFREE(MTYPE_TMP, ...) 释放。 */
char *frrlua_stackdump(lua_State *L);

/* 结束上面的 extern "C" 块。 */
#ifdef __cplusplus
}
#endif

/* 结束 HAVE_SCRIPTING 条件块：未开启脚本时上面所有声明都被跳过。 */
#endif /* HAVE_SCRIPTING */

/* 结束多重包含保护 __FRRLUA_H__。 */
#endif /* __FRRLUA_H__ */
