// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP scripting foo
 * Copyright (C) 2020  NVIDIA Corporation
 * Quentin Young
 */

/* ============================================================================
 * 【文件导读】bgp_script.c —— BGP 对象与 Lua 脚本之间的桥接（编解码器注册）
 * ============================================================================
 * 【本文件解决什么问题】FRR 的脚本子系统(lib/frrscript.c)允许在配置里挂 Lua
 * 脚本处理路由。要让它能读写 BGP 的内部对象，必须提供“把 C 对象编码成 Lua 表”
 * 和“把 Lua 表解码回 C 对象”两类函数，并按类型名注册进 frrscript 的编码表。
 * 本文件就是 bgpd 侧的这一组实现，暴露 peer 与 attr 两种类型。
 *
 * 【四个函数 + 栈约定】Lua 与 C 通过虚拟栈交换数据（详见 bgp_script.h 导读）：
 *   - lua_pushpeer : 把 struct peer 的关键字段打包成 Lua 表压栈（只读）。
 *   - lua_pushattr : 把 struct attr 打包成 Lua 表压栈。
 *   - lua_decode_attr : 把栈上第 idx 个位置的 Lua 表**填充进调用方给的结构体**。
 *   - lua_toattr     : 新建一个 attr 并返回其指针（调用方负责释放）。
 * ★ 注意 lua_* 系列里用 -1/-2 这类**负索引**表示相对栈顶的位置（Lua 约定）：
 *   例如 lua_setfield(L, -2, "x") 的语义是“把栈顶的值设为栈顶下方那个表的字段 x”。
 *
 * 【★ 关于 lua_decode_attr 末尾那次 lua_pop】末尾多出的一次 lua_pop(L, 1) 不是笔误，
 * 而是**解码器契约**：框架 frrscript_get_result() 先 lua_getfield 把待解码的值压到
 * 栈上再调用 decoder，要求 decoder 处理完把栈恢复到只剩返回表（其内有 assert
 * lua_gettop == 1）。这次 pop 就是消费掉框架压上的那个值表。
 *
 * 【内存归属】lua_toattr 用 XCALLOC(MTYPE_TMP,...) 新建 attr，归调用方释放；
 * lua_decode_attr 不分配。lua_pushpeer/pushattr 只把已存在对象的内容压栈，不转移所有权。
 *
 * 【注册表】frrscript_codecs_bgpd[] 把类型名映射到 encoder/decoder，最后由
 * bgp_script_init() 交给 frrscript_register_type_codecs() 完成注册。
 *
 * 【编译开关】整个文件体在 #ifdef HAVE_SCRIPTING 内；未开脚本功能时为空。
 *
 * 【阅读建议】先读 bgp_script.h -> 看 frrscript_codecs_bgpd[] 了解注册结构 ->
 *            再逐个看四个函数。
 * ============================================================================
 */

#include <zebra.h>

#ifdef HAVE_SCRIPTING

#include "bgpd.h"
#include "bgp_script.h"
#include "bgp_debug.h"
#include "bgp_aspath.h"
#include "frratomic.h"
#include "frrscript.h"

/* 【函数】lua_pushpeer —— 把 peer 的信息压成一个 Lua 表。
 * 顶层字段：remote_as / local_as / remote_id / local_id / state / description /
 * uptime / last_readtime / last_resettime / local_address / remote_address /
 * capabilities / flags / password；另含两个子表：
 *   timers = { configured = {hold,keepalive,connect,route_advertisement},
 *              negotiated = {hold,keepalive,connect,route_advertisement} }
 *            （configured 是本端配置值，negotiated 是协商出来的值）
 *   stats  = { open_in/out、update_in/out/time、keepalive_in/out、notify_in/out、
 *              refresh_in/out、dynamic_cap_in/out、times_established/times_dropped }
 * 其中计数器用 atomic_load_explicit(..., memory_order_relaxed) 读取——peer 统计量
 * 是原子变量，这里只做展示，无需严格内存序。整个函数只读 peer，不改其状态。
 * 用 lua_pushsockunion / lua_pushinaddr 把地址压成 Lua 侧可识别的对象。 */
void lua_pushpeer(lua_State *L, const struct peer *peer)
{
	lua_newtable(L);
	lua_pushinteger(L, peer->as);
	lua_setfield(L, -2, "remote_as");
	lua_pushinteger(L, peer->local_as);
	lua_setfield(L, -2, "local_as");
	lua_pushinaddr(L, &peer->remote_id);
	lua_setfield(L, -2, "remote_id");
	lua_pushinaddr(L, &peer->local_id);
	lua_setfield(L, -2, "local_id");
	lua_pushstring(L, lookup_msg(bgp_status_msg, peer->connection->status,
				     NULL));
	lua_setfield(L, -2, "state");
	lua_pushstring(L, peer->desc ? peer->desc : "");
	lua_setfield(L, -2, "description");
	lua_pushinteger(L, peer->uptime);
	lua_setfield(L, -2, "uptime");
	lua_pushinteger(L, peer->readtime);
	lua_setfield(L, -2, "last_readtime");
	lua_pushinteger(L, peer->resettime);
	lua_setfield(L, -2, "last_resettime");
	lua_pushsockunion(L, peer->connection->su_local);
	lua_setfield(L, -2, "local_address");
	lua_pushsockunion(L, peer->connection->su_remote);
	lua_setfield(L, -2, "remote_address");
	lua_pushinteger(L, peer->cap);
	lua_setfield(L, -2, "capabilities");
	lua_pushinteger(L, peer->flags);
	lua_setfield(L, -2, "flags");
	lua_pushstring(L, peer->password ? peer->password : "");
	lua_setfield(L, -2, "password");

	/* Nested tables here */
	lua_newtable(L);
	{
		lua_newtable(L);
		{
			lua_pushinteger(L, peer->holdtime);
			lua_setfield(L, -2, "hold");
			lua_pushinteger(L, peer->keepalive);
			lua_setfield(L, -2, "keepalive");
			lua_pushinteger(L, peer->connect);
			lua_setfield(L, -2, "connect");
			lua_pushinteger(L, peer->routeadv);
			lua_setfield(L, -2, "route_advertisement");
		}
		lua_setfield(L, -2, "configured");

		lua_newtable(L);
		{
			lua_pushinteger(L, peer->v_holdtime);
			lua_setfield(L, -2, "hold");
			lua_pushinteger(L, peer->v_keepalive);
			lua_setfield(L, -2, "keepalive");
			lua_pushinteger(L, peer->v_connect);
			lua_setfield(L, -2, "connect");
			lua_pushinteger(L, peer->v_routeadv);
			lua_setfield(L, -2, "route_advertisement");
		}
		lua_setfield(L, -2, "negotiated");
	}
	lua_setfield(L, -2, "timers");

	lua_newtable(L);
	{
		lua_pushinteger(L, atomic_load_explicit(&peer->open_in,
							memory_order_relaxed));
		lua_setfield(L, -2, "open_in");
		lua_pushinteger(L, atomic_load_explicit(&peer->open_out,
							memory_order_relaxed));
		lua_setfield(L, -2, "open_out");
		lua_pushinteger(L, atomic_load_explicit(&peer->update_in,
							memory_order_relaxed));
		lua_setfield(L, -2, "update_in");
		lua_pushinteger(L, atomic_load_explicit(&peer->update_out,
							memory_order_relaxed));
		lua_setfield(L, -2, "update_out");
		lua_pushinteger(L, atomic_load_explicit(&peer->update_time,
							memory_order_relaxed));
		lua_setfield(L, -2, "update_time");
		lua_pushinteger(L, atomic_load_explicit(&peer->keepalive_in,
							memory_order_relaxed));
		lua_setfield(L, -2, "keepalive_in");
		lua_pushinteger(L, atomic_load_explicit(&peer->keepalive_out,
							memory_order_relaxed));
		lua_setfield(L, -2, "keepalive_out");
		lua_pushinteger(L, atomic_load_explicit(&peer->notify_in,
							memory_order_relaxed));
		lua_setfield(L, -2, "notify_in");
		lua_pushinteger(L, atomic_load_explicit(&peer->notify_out,
							memory_order_relaxed));
		lua_setfield(L, -2, "notify_out");
		lua_pushinteger(L, atomic_load_explicit(&peer->refresh_in,
							memory_order_relaxed));
		lua_setfield(L, -2, "refresh_in");
		lua_pushinteger(L, atomic_load_explicit(&peer->refresh_out,
							memory_order_relaxed));
		lua_setfield(L, -2, "refresh_out");
		lua_pushinteger(L, atomic_load_explicit(&peer->dynamic_cap_in,
							memory_order_relaxed));
		lua_setfield(L, -2, "dynamic_cap_in");
		lua_pushinteger(L, atomic_load_explicit(&peer->dynamic_cap_out,
							memory_order_relaxed));
		lua_setfield(L, -2, "dynamic_cap_out");
		lua_pushinteger(L, peer->established);
		lua_setfield(L, -2, "times_established");
		lua_pushinteger(L, peer->dropped);
		lua_setfield(L, -2, "times_dropped");
	}
	lua_setfield(L, -2, "stats");
}

/* 【函数】lua_pushattr —— 把 attr 打包成一个 Lua 表，字段为：
 * metric(=med) / ifindex(=nh_ifindex) / aspath(=aspath->str 文本) / localpref。
 * 只暴露这四项，是脚本加工属性时最常改的部分。 */
void lua_pushattr(lua_State *L, const struct attr *attr)
{
	lua_newtable(L);
	lua_pushinteger(L, attr->med);
	lua_setfield(L, -2, "metric");
	lua_pushinteger(L, attr->nh_ifindex);
	lua_setfield(L, -2, "ifindex");
	lua_pushstring(L, attr->aspath->str);
	lua_setfield(L, -2, "aspath");
	lua_pushinteger(L, attr->local_pref);
	lua_setfield(L, -2, "localpref");
}

/* 【函数】lua_decode_attr —— 从栈上 idx 位置的 Lua 表读取上述字段，写回调用方
 * 提供的 attr（**不新建**）。对 aspath 字段用 aspath_str2aspath 把字符串转回
 * AS_PATH 结构，字符集按当前 asnotation 解析。
 * ★ 每读一个字段都是一次 lua_getfield（压栈）+ lua_pop（出栈）的成对操作；末尾
 *   多出的那次 lua_pop(L, 1) 用于消费框架压上的值表（见文件头【关于末尾 lua_pop】）。
 * 注意：本函数对 attr 的其它字段不做保留/清零，调用方应传入已初始化（通常已清零）
 * 的结构体，否则未赋值字段会是脏值。 */
void lua_decode_attr(lua_State *L, int idx, struct attr *attr)
{
	lua_getfield(L, idx, "metric");
	attr->med = lua_tointeger(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, idx, "ifindex");
	attr->nh_ifindex = lua_tointeger(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, idx, "aspath");
	attr->aspath = aspath_str2aspath(lua_tostring(L, -1),
					 bgp_get_asnotation(NULL));
	lua_pop(L, 1);
	lua_getfield(L, idx, "localpref");
	attr->local_pref = lua_tointeger(L, -1);
	lua_pop(L, 1);
	lua_pop(L, 1);
}

/* 【函数】lua_toattr —— 新建一个 attr（MTYPE_TMP）并调用 lua_decode_attr 填充，
 * 返回其指针。返回 void * 以避免头文件绑定具体类型，调用方应立刻赋给 struct attr *。
 * ★ 调用方负责释放这个 attr（以及其中的 aspath，如适用）。它是注册表里 "attr"
 *   类型所用的 decoder（见下方 frrscript_codecs_bgpd）。 */
void *lua_toattr(lua_State *L, int idx)
{
	struct attr *attr = XCALLOC(MTYPE_TMP, sizeof(struct attr));

	lua_decode_attr(L, idx, attr);
	return attr;
}

/* 【注册表】bgpd 提供给脚本子系统使用的类型编解码器：
 *   "peer" -> encoder=lua_pushpeer，decoder=NULL（脚本只读 peer，不回写）
 *   "attr" -> encoder=lua_pushattr，decoder=lua_toattr
 * 末尾的 {} 是终止哨兵（全零项），供 frrscript_register_type_codecs 遍历时判断结束。 */
struct frrscript_codec frrscript_codecs_bgpd[] = {
	{.typename = "peer",
	 .encoder = (encoder_func)lua_pushpeer,
	 .decoder = NULL},
	{.typename = "attr",
	 .encoder = (encoder_func)lua_pushattr,
	 .decoder = lua_toattr},
	{}};

/* 【函数】bgp_script_init —— 把上面的编解码表注册进 frrscript 子系统。
 * 由 bgpd 初始化时调用一次（见 bgp_main.c）。 */
void bgp_script_init(void)
{
	frrscript_register_type_codecs(frrscript_codecs_bgpd);
}

#endif /* HAVE_SCRIPTING */
