// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP scripting foo
 * Copyright (C) 2020  NVIDIA Corporation
 * Quentin Young
 */
/* ============================================================================
 * 【文件导读】bgp_script.h —— 用 Lua 脚本扩展 BGP 的桥接头
 * ============================================================================
 * 【能力与用途】FRR 支持在配置里挂 **Lua 脚本**（如 `bgp script` 命令配合
 * frr-script 基础设施），让运维用脚本做自定义的路由处理——典型是**通告前**对
 * 属性做个性化加工（改 community、按前缀打标、算 MED 等），比 route-map 表达力
 * 更强。本文件就是“把 BGP 内部对象交给 Lua、把 Lua 返回的属性写回 BGP”的接口。
 *
 * 【编译开关】整体在 `#ifdef HAVE_SCRIPTING` 内——configure 未开 Lua 时这些符号
 * 不存在，代码里搜 lua_pushpeer 找不到定义时先确认这个开关。
 *
 * 【Lua C API 的栈约定（读这几个函数前必须懂，否则完全看不懂）】Lua 与 C 之间
 * 通过一个**虚拟栈**交换数据：每个参数/返回值都是压栈(push)或取值(to/decode)。
 * 因此：
 *   - `lua_pushpeer` / `lua_pushattr` ：把 BGP 对象封装成 Lua 的表(userdata/table)
 *     压到栈顶。push 之后**栈上多了一个值**，调用方负责平衡栈。
 *   - `lua_decode_attr(L, idx, attr)`：把栈上第 idx 个位置的 Lua 表**反向填充**到
 *     一个已存在的 C 结构体 attr 里——注意它不分配 attr，只做拷贝填充。
 *   - `lua_toattr(L, idx)`：把栈上第 idx 个位置的表**新建**成一个 C 的 attr
 *     并返回指针（与 decode 的区别就在这儿：一个填充现成的、一个新建）。
 *   ★ 这三个函数的入参 idx 既可以是**正索引**(从栈底数)也可以是**负索引**
 *     (从栈顶数，-1 表示栈顶)，这是 Lua API 的通用约定，不是本文件的特殊之处。
 *
 * 【与 lib 的关系】原注释特别提醒：`lib/frrscript.h` 对这里若干 API 有前向引用；
 * 改动本文件这些签名时，必须同步检查 lib 那边的引用，否则链接/类型不匹配。
 *
 * 【阅读建议】先读 `lib/frrscript.h` 了解整体框架，再回来看这四个函数；
 *            实现在 bgp_script.c（很短）。
 * ============================================================================
 */
#ifndef __BGP_SCRIPT__
#define __BGP_SCRIPT__

#include <zebra.h>

#ifdef HAVE_SCRIPTING

#include <lua.h>

/* 【函数】bgp_script_init —— 初始化脚本子系统：建 Lua 状态机、注册脚本里可用的
 * C 函数、加载配置里指定的脚本。由 bgpd 启动时调用一次。 */
/*
 * Initialize scripting stuff.
 */
void bgp_script_init(void);

/*
 * Note that the common lib/frrscript.h has forward-refs to some of these
 * apis; if these change, please ensure those refs are updated too.
 */

/* Forward references */
struct peer;
struct attr;

/* 【函数】把 BGP 对象压入 Lua 栈 / 从 Lua 栈取回（栈约定见文件头导读）。
 * lua_pushpeer：把 peer 暴露给 Lua——脚本里据此知道“这条路由是发给/收自哪个邻居”。
 * 只读暴露：传的是 const struct peer *，脚本不应（也无法直接）改到 C 侧状态。 */
void lua_pushpeer(lua_State *L, const struct peer *peer);

/* 【函数】lua_pushattr —— 把属性表压入 Lua 栈，让脚本读取/修改（改完由脚本返回，
 * 再由下面的 decode/toattr 写回 C 侧）。 */
void lua_pushattr(lua_State *L, const struct attr *attr);

/* 【函数】lua_decode_attr —— 把栈上第 idx 个位置的 Lua 表反向填充进**调用方提供
 * 的** attr（不新建、不释放，只拷内容）。用于“脚本返回了一个属性表，我要把它
 * 变成 C 的属性结构”。 */
void lua_decode_attr(lua_State *L, int idx, struct attr *attr);

/* 【函数】lua_toattr —— 与 lua_decode_attr 的区别：它**新建**一个 attr 并返回其
 * 指针（因此调用方要负责后续释放）。返回 void * 是为了不在头文件里绑定具体类型，
 * 使用时应立刻赋给 struct attr *。 */
void *lua_toattr(lua_State *L, int idx);

#endif /* HAVE_SCRIPTING */

#endif /* __BGP_SCRIPT__ */
