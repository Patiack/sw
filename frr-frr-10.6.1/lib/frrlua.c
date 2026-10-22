// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file defines the lua interface into
 * FRRouting.
 *
 * Copyright (C) 2016-2019 Cumulus Networks, Inc.
 * Donald Sharp, Quentin Young
 */

/*
 * ============================ 文件导读 ============================
 * frrlua.c —— FRR 内嵌 Lua 引擎「C 端绑定」的实现
 *
 * 一、职责
 *   实现 frrlua.h 里声明的所有 lua_pushX / lua_decodeX / lua_toX 函数，
 *   在 C 数据结构与 Lua 栈之间做双向转换，并提供日志导出与栈调试工具。
 *
 * 二、核心机制
 *   Lua 与 C 通过一个虚拟栈交互。本文件里反复出现的套路是：
 *     lua_newtable(L)            在栈顶新建一张表
 *     lua_pushXxx / lua_setfield 往表里塞字段
 *   解码时则：
 *     lua_getfield(L, idx, k)    把字段值取到栈顶
 *     lua_toXxx / lua_pop(L, 1)  读值后弹栈
 *   最后 lua_decode_xxx 末尾会再 lua_pop 一次，弹出那张表本身。
 *
 * 三、核心数据结构
 *   - lua_State         Lua 解释器状态（虚拟栈），贯穿全文。
 *   - struct prefix     FRR 前缀，编码为含 network / length / family 的表。
 *   - struct interface  接口，编码为含 name / ifindex / mtu 等的表。
 *   - struct in_addr / in6_addr / ipaddr / ethaddr / sockunion 地址类。
 *   - struct nexthop / nexthop_group  下一跳与下一跳组。
 *   - MTYPE_SCRIPT_RES 本文件新分配对象的统一内存类型。
 *
 * 四、与其他模块的关系
 *   - 上游：frrscript.c 与开启了 Lua 钩子的守护进程调用这里的函数。
 *   - 依赖 lib/prefix.h、if.h、nexthop.h、nexthop_group.h、log.h 等，
 *     把 FRR 的数据结构翻译成 Lua。
 *   - frrlua_export_logging 把 lib/log.h 的 zlog 接口暴露给 Lua。
 *
 * 五、阅读顺序
 *   先看 frrlua_table_get_integer（最简单）-> 再看 prefix 三件套 ->
 *   interface -> 各类地址 -> nexthop -> integer / longlong / string ->
 *   logging 导出 -> 末尾的 lua_table_dump / frrlua_stackdump 调试工具。
 *
 * 六、易踩的坑
 *   1. lua_toXxx 用 XCALLOC(MTYPE_SCRIPT_RES, ..) 分配，必须由调用者
 *      用 XFREE(MTYPE_SCRIPT_RES, ..) 释放，切勿与 MTYPE_TMP 混用。
 *   2. lua_decode_xxx 假定栈上 idx 处就是对应的 table，且会弹出该表；
 *      调用前不要多压东西。
 *   3. 解码全靠字段名（字符串）匹配，字段名拼错不会报错，只会读到 0。
 *   4. 整个文件被 HAVE_SCRIPTING 笼罩，未启用脚本时是空编译单元。
 * ================================================================
 */
/* FRR 统一的超级头，集中引入大量系统头与平台兼容定义，必须最先包含。 */
#include <zebra.h>

/* 仅在编译时启用脚本支持时才编译本文件的实现。 */
#ifdef HAVE_SCRIPTING

/* Lua 辅助库（table / string 等标准操作的 C 接口）。 */
#include <lauxlib.h>
/* Lua 核心头，lua_State 与栈操作 API。 */
#include <lua.h>
/* Lua 标准库注册入口（luaL_openlibs 用）。 */
#include <lualib.h>

/* 前缀类型 struct prefix 及其字符串化 prefix2str / str2prefix。 */
#include "lib/prefix.h"
/* 动态缓冲区 struct buffer，lua_table_dump 用。 */
#include "lib/buffer.h"
/* 本文件要实现的接口声明。 */
#include "lib/frrlua.h"
/* 接口结构体 struct interface。 */
#include "lib/if.h"
/* zlog 日志接口，frrlua_export_logging 用。 */
#include "lib/log.h"
/* 下一跳 struct nexthop。 */
#include "lib/nexthop.h"
/* 下一跳组 struct nexthop_group。 */
#include "lib/nexthop_group.h"

/* 定义 MTYPE_SCRIPT_RES：本文件用 lua_toXxx 新分配对象时统一走这个内存
 * 类型，便于内存统计与泄漏排查。头文件里是 DECLARE，这里是 DEFINE。 */
DEFINE_MTYPE(LIB, SCRIPT_RES, "Scripting results");

/* 本文件按「分组」组织：先通用便利函数，再各类型编解码，最后是日志导出
 * 与调试工具；每组前都有一段英文小标题注释。 */
/* Lua stuff */

/* 便捷函数区：让 C 与 Lua 栈之间的数据搬运更省事，先看这里最直观。 */
/*
 * FRR convenience functions.
 *
 * This section has convenience functions used to make interacting with the Lua
 * stack easier.
 */

/* 从栈顶那张 table 中按 key 取整数。做法是：把 key 压栈，用 lua_gettable
 * 以「栈中 -2 的表 + 栈顶的 key」做索引，取到的值就到栈顶，转成 int 后
 * 弹掉。注意调用前栈顶必须是被查询的 table。 */
int frrlua_table_get_integer(lua_State *L, const char *key)
{
	int result;

	lua_pushstring(L, key);
	lua_gettable(L, -2);

	result = lua_tointeger(L, -1);
	lua_pop(L, 1);

	return result;
}

/* 编解码函数区：每种 FRR 类型都提供 1 个编码（lua_pushX）+ 2 个解码
 * （lua_decodeX 写入已有对象 / lua_toX 新分配对象）函数。 */
/*
 * This section has functions that convert internal FRR datatypes into Lua
 * datatypes: one encoder function and two decoder functions for each type.
 *
 */

/* 把 struct prefix 编码成一张 Lua 表压栈，字段有：
 *   network  形如 "10.0.0.0/8" 的字符串（prefix2str 生成）
 *   length   前缀长度 prefixlen
 *   family   地址族（AF_INET / AF_INET6）
 * 注意表本身留在栈顶，由调用者决定何时弹出。 */
void lua_pushprefix(lua_State *L, const struct prefix *prefix)
{
	char buffer[PREFIX_STRLEN];

	lua_newtable(L);
	lua_pushstring(L, prefix2str(prefix, buffer, PREFIX_STRLEN));
	lua_setfield(L, -2, "network");
	lua_pushinteger(L, prefix->prefixlen);
	lua_setfield(L, -2, "length");
	lua_pushinteger(L, prefix->family);
	lua_setfield(L, -2, "family");
}

/* 反向：从栈上 idx 处的表解出 prefix，填充到 *prefix。只读 network 字段
 * 并用 str2prefix 解析（length / family 由它自行推出）。取字段后弹掉字段
 * 值，最后再弹掉整张表（见末尾 lua_pop）。 */
void lua_decode_prefix(lua_State *L, int idx, struct prefix *prefix)
{
	lua_getfield(L, idx, "network");
	(void)str2prefix(lua_tostring(L, -1), prefix);
	lua_pop(L, 1);
	/* pop the table */
	lua_pop(L, 1);
}

/* 便捷封装：新分配一个 prefix（MTYPE_SCRIPT_RES）后交给 lua_decode_prefix
 * 填充并返回。调用者用完必须 XFREE(MTYPE_SCRIPT_RES, ..)。 */
void *lua_toprefix(lua_State *L, int idx)
{
	struct prefix *p = XCALLOC(MTYPE_SCRIPT_RES, sizeof(struct prefix));
	lua_decode_prefix(L, idx, p);
	return p;
}

/* 把 struct interface 编成一张 Lua 表压栈，字段包括 name / ifindex /
 * status / flags / metric / speed / mtu / mtu6 / bandwidth / link_ifindex
 * / linklayer_type，供脚本读取接口的各项属性。 */
void lua_pushinterface(lua_State *L, const struct interface *ifp)
{
	lua_newtable(L);
	lua_pushstring(L, ifp->name);
	lua_setfield(L, -2, "name");
	lua_pushinteger(L, ifp->ifindex);
	lua_setfield(L, -2, "ifindex");
	lua_pushinteger(L, ifp->status);
	lua_setfield(L, -2, "status");
	lua_pushinteger(L, ifp->flags);
	lua_setfield(L, -2, "flags");
	lua_pushinteger(L, ifp->metric);
	lua_setfield(L, -2, "metric");
	lua_pushinteger(L, ifp->speed);
	lua_setfield(L, -2, "speed");
	lua_pushinteger(L, ifp->mtu);
	lua_setfield(L, -2, "mtu");
	lua_pushinteger(L, ifp->mtu6);
	lua_setfield(L, -2, "mtu6");
	lua_pushinteger(L, ifp->bandwidth);
	lua_setfield(L, -2, "bandwidth");
	lua_pushinteger(L, ifp->link_ifindex);
	lua_setfield(L, -2, "link_ifindex");
	lua_pushinteger(L, ifp->ll_type);
	lua_setfield(L, -2, "linklayer_type");
}

/* 反向：从栈上 idx 处的表逐字段解码进 *ifp。每读一个字段就 getfield
 * 取值、tointeger 转换、pop 弹栈，最后再 pop 掉整表。字符串 name 用
 * strlcpy 做边界安全拷贝。字段名必须与 lua_pushinterface 严格一致。 */
void lua_decode_interface(lua_State *L, int idx, struct interface *ifp)
{
	lua_getfield(L, idx, "name");
	strlcpy(ifp->name, lua_tostring(L, -1), sizeof(ifp->name));
	lua_pop(L, 1);
	lua_getfield(L, idx, "ifindex");
	ifp->ifindex = lua_tointeger(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, idx, "status");
	ifp->status = lua_tointeger(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, idx, "flags");
	ifp->flags = lua_tointeger(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, idx, "metric");
	ifp->metric = lua_tointeger(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, idx, "speed");
	ifp->speed = lua_tointeger(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, idx, "mtu");
	ifp->mtu = lua_tointeger(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, idx, "mtu6");
	ifp->mtu6 = lua_tointeger(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, idx, "bandwidth");
	ifp->bandwidth = lua_tointeger(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, idx, "link_ifindex");
	ifp->link_ifindex = lua_tointeger(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, idx, "linklayer_type");
	ifp->ll_type = lua_tointeger(L, -1);
	lua_pop(L, 1);
	/* pop the table */
	lua_pop(L, 1);
}
/* 便捷封装：新分配 interface（MTYPE_SCRIPT_RES）并解码填充后返回。注意
 * 这是游离快照，未挂进全局接口树，调用者负责释放。 */
void *lua_tointerface(lua_State *L, int idx)
{
	struct interface *ifp =
		XCALLOC(MTYPE_SCRIPT_RES, sizeof(struct interface));

	lua_decode_interface(L, idx, ifp);
	return ifp;
}

/* 把 IPv4 地址编成表压栈，含两个字段：value = 32 位原始整数，string =
 * 点分十进制文本（inet_ntop 生成），方便 Lua 侧按需取用。 */
void lua_pushinaddr(lua_State *L, const struct in_addr *addr)
{
	char buf[INET_ADDRSTRLEN];

	inet_ntop(AF_INET, addr, buf, sizeof(buf));

	lua_newtable(L);
	lua_pushinteger(L, addr->s_addr);
	lua_setfield(L, -2, "value");
	lua_pushstring(L, buf);
	lua_setfield(L, -2, "string");
}

/* 反向：取 value 字段（原始 32 位整数）还原 IPv4 地址，最后 pop 掉整表。 */
void lua_decode_inaddr(lua_State *L, int idx, struct in_addr *inaddr)
{
	lua_getfield(L, idx, "value");
	inaddr->s_addr = lua_tointeger(L, -1);
	lua_pop(L, 1);
	/* pop the table */
	lua_pop(L, 1);
}

/* 新分配 in_addr（MTYPE_SCRIPT_RES）并解码返回，调用者负责释放。 */
void *lua_toinaddr(lua_State *L, int idx)
{
	struct in_addr *inaddr =
		XCALLOC(MTYPE_SCRIPT_RES, sizeof(struct in_addr));
	lua_decode_inaddr(L, idx, inaddr);
	return inaddr;
}


/* 把 IPv6 地址编成表压栈：value 是 16 字节原始数据（lua_pushlstring 存
 * 二进制），string 是冒号十六进制文本。注意 raw 数据超过 32 位，不能塞
 * 进 lua_pushinteger。 */
void lua_pushin6addr(lua_State *L, const struct in6_addr *addr)
{
	char buf[INET6_ADDRSTRLEN];

	inet_ntop(AF_INET6, addr, buf, sizeof(buf));

	lua_newtable(L);
	lua_pushlstring(L, (const char *)addr->s6_addr, 16);
	lua_setfield(L, -2, "value");
	lua_pushstring(L, buf);
	lua_setfield(L, -2, "string");
}

/* 反向：取 string 字段用 inet_pton 解析回 IPv6（未用 value 原始字节，
 * 与 push 侧不对称但等价），最后 pop 掉整表。 */
void lua_decode_in6addr(lua_State *L, int idx, struct in6_addr *in6addr)
{
	lua_getfield(L, idx, "string");
	inet_pton(AF_INET6, lua_tostring(L, -1), in6addr);
	lua_pop(L, 1);
	/* pop the table */
	lua_pop(L, 1);
}

/* 新分配 in6_addr（MTYPE_SCRIPT_RES）并解码返回，调用者负责释放。 */
void *lua_toin6addr(lua_State *L, int idx)
{
	struct in6_addr *in6addr =
		XCALLOC(MTYPE_SCRIPT_RES, sizeof(struct in6_addr));
	lua_decode_in6addr(L, idx, in6addr);
	return in6addr;
}

/* 统一地址 struct ipaddr 可按 v4/v6 承载；这里用 IS_IPADDR_V4 判断实际
 * 族，分派到对应的 push 函数，脚本侧无需关心底层是哪种。 */
void lua_pushipaddr(lua_State *L, const struct ipaddr *addr)
{
	if (IS_IPADDR_V4(addr))
		lua_pushinaddr(L, &addr->ipaddr_v4);
	else
		lua_pushin6addr(L, &addr->ipaddr_v6);
}

/* 把 MAC 地址编成表压栈，只有一个 octet 字段（把 6 字节当作整数读出）。
 * 注意：这里取的是首元素的值，不是完整 6 字节数组，仅够做简单比较。 */
void lua_pushethaddr(lua_State *L, const struct ethaddr *addr)
{
	lua_newtable(L);
	lua_pushinteger(L, *(addr->octet));
	lua_setfield(L, -2, "octet");
}

/* 把套接字地址 sockunion 编成表压栈：value 是变长原始地址字节（v4 为
 * 4 字节、v6 为 16 字节，长度由 sockunion_get_addrlen 给出），string 是
 * 文本形式（sockunion2str，形如 "1.2.3.4" 或 "[::1]"）。 */
void lua_pushsockunion(lua_State *L, const union sockunion *su)
{
	char buf[SU_ADDRSTRLEN];

	sockunion2str(su, buf, sizeof(buf));

	lua_newtable(L);
	lua_pushlstring(L, (const char *)sockunion_get_addr(su),
			sockunion_get_addrlen(su));
	lua_setfield(L, -2, "value");
	lua_pushstring(L, buf);
	lua_setfield(L, -2, "string");
}

/* 反向：取 string 字段用 str2sockunion 解析；解析失败会打一条 zlog_err
 * 但仍继续（su 内容可能不可靠）。最后 pop 掉整表。 */
void lua_decode_sockunion(lua_State *L, int idx, union sockunion *su)
{
	lua_getfield(L, idx, "string");
	if (str2sockunion(lua_tostring(L, -1), su) < 0)
		zlog_err("Lua hook call: Failed to decode sockunion");

	lua_pop(L, 1);
	/* pop the table */
	lua_pop(L, 1);
}

/* 新分配 sockunion（MTYPE_SCRIPT_RES）并解码返回，调用者负责释放。 */
void *lua_tosockunion(lua_State *L, int idx)
{
	union sockunion *su =
		XCALLOC(MTYPE_SCRIPT_RES, sizeof(union sockunion));

	lua_decode_sockunion(L, idx, su);
	return su;
}

/* 把 int 直接压栈（不带表包装），最轻量的一种编码。 */
void lua_pushintegerp(lua_State *L, const int *num)
{
	lua_pushinteger(L, *num);
}

/* 从栈上 idx 处读一个数，用 lua_tonumberx 同时拿到「是否为数字」标志
 * isnum；随后 pop，并用 assert 保证确实取到了数字（类型不符会在此断言）。 */
void lua_decode_integerp(lua_State *L, int idx, int *num)
{
	int isnum;
	*num = lua_tonumberx(L, idx, &isnum);
	lua_pop(L, 1);
	assert(isnum);
}

/* 新分配 int（MTYPE_SCRIPT_RES）并解码返回，调用者负责释放。 */
void *lua_tointegerp(lua_State *L, int idx)
{
	int *num = XCALLOC(MTYPE_SCRIPT_RES, sizeof(int));

	lua_decode_integerp(L, idx, num);
	return num;
}

/* 把单条下一跳编成表压栈。公共字段：vrf_id / ifindex / type / flags /
 * weight / backup_num / backup_idx / nh_label_type / nh_encap_type /
 * srte_color。再按 type 追加类型相关字段：黑洞路由带 bh_type；IPv4/IPv6
 * 网关分别用 lua_pushinaddr / lua_pushin6addr 编成嵌套的 gate 表；VXLAN
 * 封装带 vni。 */
void lua_pushnexthop(lua_State *L, const struct nexthop *nexthop)
{
	lua_newtable(L);
	lua_pushinteger(L, nexthop->vrf_id);
	lua_setfield(L, -2, "vrf_id");
	lua_pushinteger(L, nexthop->ifindex);
	lua_setfield(L, -2, "ifindex");
	lua_pushinteger(L, nexthop->type);
	lua_setfield(L, -2, "type");
	lua_pushinteger(L, nexthop->flags);
	lua_setfield(L, -2, "flags");
	if (nexthop->type == NEXTHOP_TYPE_BLACKHOLE) {
		lua_pushinteger(L, nexthop->bh_type);
		lua_setfield(L, -2, "bh_type");
	} else if (nexthop->type == NEXTHOP_TYPE_IPV4) {
		lua_pushinaddr(L, &nexthop->gate.ipv4);
		lua_setfield(L, -2, "gate");
	} else if (nexthop->type == NEXTHOP_TYPE_IPV6) {
		lua_pushin6addr(L, &nexthop->gate.ipv6);
		lua_setfield(L, -2, "gate");
	}
	lua_pushinteger(L, nexthop->nh_label_type);
	lua_setfield(L, -2, "nh_label_type");
	lua_pushinteger(L, nexthop->weight);
	lua_setfield(L, -2, "weight");
	lua_pushinteger(L, nexthop->backup_num);
	lua_setfield(L, -2, "backup_num");
	lua_pushinteger(L, *(nexthop->backup_idx));
	lua_setfield(L, -2, "backup_idx");
	if (nexthop->nh_encap_type == NET_VXLAN) {
		lua_pushinteger(L, nexthop->nh_encap.vni);
		lua_setfield(L, -2, "vni");
	}
	lua_pushinteger(L, nexthop->nh_encap_type);
	lua_setfield(L, -2, "nh_encap_type");
	lua_pushinteger(L, nexthop->srte_color);
	lua_setfield(L, -2, "srte_color");
}

/* 把整个下一跳组编成表压栈：用 ALL_NEXTHOPS_PTR 遍历链表，每条 nexthop
 * 递归编成子表后 lua_seti 以 1 起始的整数下标写进数组，最终得到 Lua 的
 * 数组风格 table。 */
void lua_pushnexthop_group(lua_State *L, const struct nexthop_group *ng)
{
	lua_newtable(L);
	struct nexthop *nexthop;
	int i = 1;

	for (ALL_NEXTHOPS_PTR(ng, nexthop)) {
		lua_pushnexthop(L, nexthop);
		lua_seti(L, -2, i);
		i++;
	}
}

/* 把 long long 压栈。Lua 的 integer 在 64 位构建下就是 64 位，可直接
 * 承载，无需拆成高低位。 */
void lua_pushlonglongp(lua_State *L, const long long *num)
{
	/* lua library function; this can take a long long */
	lua_pushinteger(L, *num);
}

/* 从栈上 idx 处读一个数并转为 long long；与 decode_integerp 同样用
 * lua_tonumberx 校验类型后 assert，最后 pop。 */
void lua_decode_longlongp(lua_State *L, int idx, long long *num)
{
	int isnum;
	*num = lua_tonumberx(L, idx, &isnum);
	lua_pop(L, 1);
	assert(isnum);
}

/* 新分配 long long（MTYPE_SCRIPT_RES）并解码返回，调用者负责释放。 */
void *lua_tolonglongp(lua_State *L, int idx)
{
	long long *num = XCALLOC(MTYPE_SCRIPT_RES, sizeof(long long));

	lua_decode_longlongp(L, idx, num);
	return num;
}

/* 从栈上 idx 处取字符串，strlcpy 拷进调用者缓冲区 *str，随后 pop。
 * 注意拷贝长度用 strlen(str) + 1，即假定调用者已把 str 初始化到目标大小，
 * 是个隐含契约。 */
void lua_decode_stringp(lua_State *L, int idx, char *str)
{
	strlcpy(str, lua_tostring(L, idx), strlen(str) + 1);
	lua_pop(L, 1);
}

/* 取出栈上 idx 处字符串，用 XSTRDUP(MTYPE_SCRIPT_RES) 复制一份返回；
 * 调用者负责 XFREE(MTYPE_SCRIPT_RES, ..)。 */
void *lua_tostringp(lua_State *L, int idx)
{
	char *string = XSTRDUP(MTYPE_SCRIPT_RES, lua_tostring(L, idx));

	return string;
}

/* 日志导出区：把 FRR 的 zlog 各日志级别包装成 Lua 可直接调用的 log.* */
/*
 * Logging.
 *
 * Lua-compatible wrappers for FRR logging functions.
 */
/* 所有 log.* 函数的公共适配层：校验调用方恰好传了 1 个参数（否则 assert
 * 失败），并返回该参数（字符串）。真正的日志级别由各 frrlua_log_xxx 决定。 */
static const char *frrlua_log_thunk(lua_State *L)
{
	int nargs;

	nargs = lua_gettop(L);
	assert(nargs == 1);

	return lua_tostring(L, 1);
}

/* log.trace：不做普通日志，而是把整个 Lua 栈 dump 出来打到 debug，用于
 * 脚本调试时观察栈内容。 */
static int frrlua_log_trace(lua_State *L)
{
	zlog_debug("%s", frrlua_stackdump(L));
	return 0;
}

/* log.debug：转调 zlog_debug 输出脚本给的字符串。 */
static int frrlua_log_debug(lua_State *L)
{
	zlog_debug("%s", frrlua_log_thunk(L));
	return 0;
}

/* log.info：转调 zlog_info。 */
static int frrlua_log_info(lua_State *L)
{
	zlog_info("%s", frrlua_log_thunk(L));
	return 0;
}

/* log.notice：转调 zlog_notice。 */
static int frrlua_log_notice(lua_State *L)
{
	zlog_notice("%s", frrlua_log_thunk(L));
	return 0;
}

/* log.warn：转调 zlog_warn。 */
static int frrlua_log_warn(lua_State *L)
{
	zlog_warn("%s", frrlua_log_thunk(L));
	return 0;
}

/* log.error：转调 zlog_err。 */
static int frrlua_log_error(lua_State *L)
{
	zlog_err("%s", frrlua_log_thunk(L));
	return 0;
}

/* 注册表：把 Lua 里看到的函数名映射到 C 实现，末尾必须以空项收尾（{ }）,
 * 供 luaL_setfuncs 一次性批量注册。 */
static const luaL_Reg log_funcs[] = {
	{ "trace", frrlua_log_trace },
	{ "debug", frrlua_log_debug },
	{ "info", frrlua_log_info },
	{ "notice", frrlua_log_notice },
	{ "warn", frrlua_log_warn },
	{ "error", frrlua_log_error },
	{},
};

/* 建一张新表，用 log_funcs 把六个日志函数灌进去，再以全局名 log 挂到
 * Lua 环境里，于是脚本可直接写 log.info("...") 等。 */
void frrlua_export_logging(lua_State *L)
{
	lua_newtable(L);
	luaL_setfuncs(L, log_funcs, 0);
	lua_setglobal(L, "log");
}

/* 调试工具区：把 Lua 栈 / table 转成可读字符串，排查脚本问题时用。 */
/*
 * Debugging.
 */

/* 递归地把栈上 index 处的值格式化进 buf。用 lua_next 遍历 table 的每个
 * 键值对：先按 key 类型（字符串/数字）输出键，再按 value 类型分别处理
 * 字符串、布尔、数字，遇到嵌套 table 就递归（level + 1 控制缩进）。
 * 遍历期间 lua_pop(1) 弹出值、保留键，是 lua_next 的标准用法。 */
void lua_table_dump(lua_State *L, int index, struct buffer *buf, int level)
{
	char tmpbuf[64] = {};

	lua_pushnil(L);

	while (lua_next(L, index) != 0) {
		int key_type;
		int value_type;

		for (int i = 0; i < level; i++)
			buffer_putstr(buf, "  ");

		key_type = lua_type(L, -2);
		if (key_type == LUA_TSTRING) {
			const char *key = lua_tostring(L, -2);

			buffer_putstr(buf, key);
			buffer_putstr(buf, ": ");
		} else if (key_type == LUA_TNUMBER) {
			snprintf(tmpbuf, sizeof(tmpbuf), "%g",
				 lua_tonumber(L, -2));
			buffer_putstr(buf, tmpbuf);
			buffer_putstr(buf, ": ");
		}

		value_type = lua_type(L, -1);
		switch (value_type) {
		case LUA_TSTRING:
			snprintf(tmpbuf, sizeof(tmpbuf), "\"%s\"\n",
				 lua_tostring(L, -1));
			buffer_putstr(buf, tmpbuf);
			break;
		case LUA_TBOOLEAN:
			snprintf(tmpbuf, sizeof(tmpbuf), "%s\n",
				 lua_toboolean(L, -1) ? "true" : "false");
			buffer_putstr(buf, tmpbuf);
			break;
		case LUA_TNUMBER:
			snprintf(tmpbuf, sizeof(tmpbuf), "%g\n",
				 lua_tonumber(L, -1));
			buffer_putstr(buf, tmpbuf);
			break;
		case LUA_TTABLE:
			buffer_putstr(buf, "{\n");
			lua_table_dump(L, lua_gettop(L), buf, level + 1);
			for (int i = 0; i < level; i++)
				buffer_putstr(buf, "  ");
			buffer_putstr(buf, "}\n");
			break;
		default:
			snprintf(tmpbuf, sizeof(tmpbuf), "%s\n",
				 lua_typename(L, value_type));
			buffer_putstr(buf, tmpbuf);
			break;
		}

		lua_pop(L, 1);
	}
}

/* 把整个 Lua 栈（从下标 1 到栈顶）格式化成一个字符串返回：逐元素按类型
 * 打印，遇到 table 调 lua_table_dump。返回串用 MTYPE_TMP 复制，调用者
 * 需 XFREE(MTYPE_TMP, ..)。主要给 log.trace 和人工调试用。 */
char *frrlua_stackdump(lua_State *L)
{
	int top = lua_gettop(L);

	char tmpbuf[64];
	struct buffer *buf = buffer_new(4098);

	for (int i = 1; i <= top; i++) {
		int t = lua_type(L, i);

		switch (t) {
		case LUA_TSTRING: /* strings */
			snprintf(tmpbuf, sizeof(tmpbuf), "\"%s\"\n",
				 lua_tostring(L, i));
			buffer_putstr(buf, tmpbuf);
			break;
		case LUA_TBOOLEAN: /* booleans */
			snprintf(tmpbuf, sizeof(tmpbuf), "%s\n",
				 lua_toboolean(L, i) ? "true" : "false");
			buffer_putstr(buf, tmpbuf);
			break;
		case LUA_TNUMBER: /* numbers */
			snprintf(tmpbuf, sizeof(tmpbuf), "%g\n",
				 lua_tonumber(L, i));
			buffer_putstr(buf, tmpbuf);
			break;
		case LUA_TTABLE: /* tables */
			buffer_putstr(buf, "{\n");
			lua_table_dump(L, i, buf, 1);
			buffer_putstr(buf, "}\n");
			break;
		default: /* other values */
			snprintf(tmpbuf, sizeof(tmpbuf), "%s\n",
				 lua_typename(L, t));
			buffer_putstr(buf, tmpbuf);
			break;
		}
	}

	char *result = XSTRDUP(MTYPE_TMP, buffer_getstr(buf));

	buffer_free(buf);

	return result;
}

/* 结束 HAVE_SCRIPTING 条件编译块，本文件到此结束。 */
#endif /* HAVE_SCRIPTING */
