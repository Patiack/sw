// SPDX-License-Identifier: GPL-2.0-or-later
/* Scripting foo
 * Copyright (C) 2020  NVIDIA Corporation
 * Quentin Young
 */
/*
 * ============================ 文件导读 ============================
 * frrscript.c —— FRR 脚本子系统（frrscript）的实现
 *
 * 一、职责
 *   实现 frrscript.h 声明的接口：管理「钩子函数名 -> 脚本名」的全局映射，
 *   创建脚本运行时对象、加载并执行 Lua 函数、把结果解码回 C，以及维护
 *   「类型名 -> 编解码器」的 codec 表。
 *
 * 二、核心数据结构
 *   - frrscript_names_hash   全局哈希，项是 frrscript_names_entry，记录每个
 *     脚本钩子函数名对应哪个脚本文件。
 *   - codec_hash             全局哈希，项是 frrscript_codec，按类型名存
 *     各 C 类型的 encoder / decoder。
 *   - frrscript_codecs_lib   内置注册的核心类型表（integer/string/prefix/
 *     interface/in_addr/in6_addr/sockunion）。
 *   - frrscript / lua_function_state  一个脚本对象及其里边的函数状态。
 *   - scriptdir              查找 .lua 文件的目录，frrscript_init 时设定。
 *
 * 三、与其他模块的关系
 *   - 依赖 frrlua.c 提供的各 lua_pushX / lua_decodeX / lua_toX。
 *   - 用 hash.h 的通用哈希，typesafe 的 frrscript_names 由头文件宏生成。
 *   - frrscript_load 里调用 frrlua_export_logging 让脚本能用 log.*。
 *   - 各守护进程在启动时 frrscript_init、register_type_codec，再 load。
 *
 * 四、阅读顺序
 *   先看名字哈希这一段（config_write / add / set / get / hash_key / cmp）
 *   -> 内置 codec 表 -> codec_hash 与 codec_alloc/free -> 函数哈希工具 ->
 *   _frrscript_call_lua（执行 + 错误检查）-> frrscript_get_result ->
 *   注册接口 -> 生命周期（new / load / delete / init / fini）。
 *
 * 五、易踩的坑
 *   1. 整个文件由条件编译包住，未启用脚本时是空编译单元。
 *   2. Lua 函数必须「恰好返回一个 table」，否则 _frrscript_call_lua 报错。
 *   3. codec 重复注册会 assert 失败（见 register_type_codec）。
 *   4. frrscript_load 失败路径会 lua_close，成功路径则把 L 交给函数状态，
 *      由 lua_function_free 负责关闭，切忌重复关闭。
 * ================================================================
 */
/* FRR 超级头，集中引入系统头与兼容定义，必须最先包含。 */
#include <zebra.h>

/* 仅在启用脚本支持时编译本文件。 */
#ifdef HAVE_SCRIPTING

/* 变参支持（_lua_decode_noop 形参里有 ...）。 */
#include <stdarg.h>
/* Lua 核心头。 */
#include <lua.h>
/* Lua 标准库注册。 */
#include <lualib.h>
/* Lua 辅助库。 */
#include <lauxlib.h>

/* 本文件要实现的接口。 */
#include "frrscript.h"
/* 各类型的 lua_push / lua_decode / lua_to。 */
#include "frrlua.h"
/* FRR 内存类型（MTYPE）。 */
#include "memory.h"
/* 通用哈希表 API（hash_create / hash_get / hash_lookup 等）。 */
#include "hash.h"
/* 日志与 zlog_backtrace。 */
#include "log.h"
/* vty 定义，config_write 用。 */
#include "lib/vty.h"


/* 定义本文件私有内存类型 MTYPE_SCRIPT（static，不导出），本文件里所有
 * XCALLOC / XSTRDUP / XFREE 都用它。 */
DEFINE_MTYPE_STATIC(LIB, SCRIPT, "Scripting");

/* 名字哈希区块：维护「脚本钩子函数名 -> 脚本名」的全局映射。 */
/*
 * Script name hash utilities
 */

/* 全局的名字表实例：所有「脚本钩子函数名 -> 脚本名」的映射都存在这里，
 * 是配置解析与运行时查脚本的枢纽。 */
struct frrscript_names_head frrscript_names_hash;

/* 无操作解码器实现（头文件里声明）：没有对应解码器时什么都不做。 */
void _lua_decode_noop(lua_State *L, ...) {}

/* 遍历名字表，把设置了脚本名的项写成配置行（这里硬编码输出成 zebra 风格
 * 的 "zebra on-rib-process script <name>"），即 show running-config 的
 * 脚本配置来源。未设脚本名的项跳过。 */
void frrscript_names_config_write(struct vty *vty)
{
	struct frrscript_names_entry *lua_script_entry;

	frr_each (frrscript_names, &frrscript_names_hash, lua_script_entry)
		if (lua_script_entry->script_name[0] != '\0')
			vty_out(vty, "zebra on-rib-process script %s\n",
				lua_script_entry->script_name);
}

/*
 * Wrapper for frrscript_names_add
 * Use this to register hook calls when a daemon starts up
 */
/* 登记一个钩子函数名：新建名字表项（只填 function_name），插入全局表。
 * 守护进程启动时用它注册自己支持的钩子。插入失败（多半重复）打 warn 并
 * 返回 1，成功返回 0。 */
int frrscript_names_add_function_name(const char *function_name)
{
	struct frrscript_names_entry *insert =
		XCALLOC(MTYPE_SCRIPT, sizeof(*insert));
	strlcpy(insert->function_name, function_name,
		sizeof(insert->function_name));

	if (frrscript_names_add(&frrscript_names_hash, insert)) {
		zlog_warn(
			"Failed to add hook call function name to script_names");
		return 1;
	}
	return 0;
}

/* 弹出并释放名字表里的所有项，清空全局表（退出时清理）。 */
void frrscript_names_destroy(void)
{
	struct frrscript_names_entry *ne;

	while ((ne = frrscript_names_pop(&frrscript_names_hash)))
		XFREE(MTYPE_SCRIPT, ne);
}

/*
 * Given a function_name, set its script_name. function_names and script_names
 * are one-to-one. Each set will wipe the previous script_name.
 * Return 0 if set was successful, else 1.
 *
 * script_name is the base name of the file, without .lua.
 */
/* 为某钩子函数名设定脚本名（一对一，后设的覆盖先前的）。先按函数名查出
 * 表项，查不到返回 1；查到就 strlcpy 写入 script_name 并返回 0。脚本名
 * 是不含 .lua 的文件基名。 */
int frrscript_names_set_script_name(const char *function_name,
				    const char *script_name)
{
	struct frrscript_names_entry lookup;

	strlcpy(lookup.function_name, function_name,
		sizeof(lookup.function_name));
	struct frrscript_names_entry *snhe =
		frrscript_names_find(&frrscript_names_hash, &lookup);
	if (!snhe)
		return 1;
	strlcpy(snhe->script_name, script_name, sizeof(snhe->script_name));
	return 0;
}

/*
 * Given a function_name, get its script_name.
 * Return NULL if function_name not found.
 *
 * script_name is the base name of the file, without .lua.
 */
/* 反查：按钩子函数名取脚本名。查不到项、或该项还没设脚本名（空串），都
 * 返回 NULL；否则返回表项内部字符串指针（勿释放）。 */
char *frrscript_names_get_script_name(const char *function_name)
{
	struct frrscript_names_entry lookup;

	strlcpy(lookup.function_name, function_name,
		sizeof(lookup.function_name));
	struct frrscript_names_entry *snhe =
		frrscript_names_find(&frrscript_names_hash, &lookup);
	if (!snhe)
		return NULL;

	if (snhe->script_name[0] == '\0')
		return NULL;

	return snhe->script_name;
}

/* 名字表的哈希函数：以 function_name 字符串算哈希值。 */
uint32_t frrscript_names_hash_key(const struct frrscript_names_entry *snhe)
{
	return string_hash_make(snhe->function_name);
}

/* 名字表的比较函数：按 function_name 比较（key 就是函数名）。 */
int frrscript_names_hash_cmp(const struct frrscript_names_entry *snhe1,
			     const struct frrscript_names_entry *snhe2)
{
	return strncmp(snhe1->function_name, snhe2->function_name,
		       sizeof(snhe1->function_name));
}

/* 编解码器区块开始。 */
/* Codecs */

/* 核心库内置的类型编解码表：把类型名映射到 lua_pushX / lua_toX 函数对。
 * encoder 需要把函数指针强转成统一的 encoder_func 签名。末尾以全零项
 * 收尾，供 frrscript_register_type_codecs 判断结束。 */
struct frrscript_codec frrscript_codecs_lib[] = {
	{.typename = "integer",
	 .encoder = (encoder_func)lua_pushintegerp,
	 .decoder = lua_tointegerp},
	{.typename = "string",
	 .encoder = (encoder_func)lua_pushstring_wrapper,
	 .decoder = lua_tostringp},
	{.typename = "prefix",
	 .encoder = (encoder_func)lua_pushprefix,
	 .decoder = lua_toprefix},
	{.typename = "interface",
	 .encoder = (encoder_func)lua_pushinterface,
	 .decoder = lua_tointerface},
	{.typename = "in_addr",
	 .encoder = (encoder_func)lua_pushinaddr,
	 .decoder = lua_toinaddr},
	{.typename = "in6_addr",
	 .encoder = (encoder_func)lua_pushin6addr,
	 .decoder = lua_toin6addr},
	{.typename = "sockunion",
	 .encoder = (encoder_func)lua_pushsockunion,
	 .decoder = lua_tosockunion},
	{}};

/* 类型编解码器区块：运行时所有已注册 codec 的哈希表与脚本目录。 */
/* Type codecs */

/* 全局 codec 哈希：键是类型名，值是 frrscript_codec；各守护进程可往里
 * 追加自己类型（如 peer、attr）的编解码器。 */
struct hash *codec_hash;
/* 存放 .lua 文件的目录，由 frrscript_init 设定。 */
char scriptdir[MAXPATHLEN];

/* codec 哈希的取键函数：用 typename 字符串算哈希。 */
static unsigned int codec_hash_key(const void *data)
{
	const struct frrscript_codec *c = data;

	return string_hash_make(c->typename);
}

/* codec 哈希的比较函数：两个 codec 的 typename 相等即视为同一个 key。 */
static bool codec_hash_cmp(const void *d1, const void *d2)
{
	const struct frrscript_codec *e1 = d1;
	const struct frrscript_codec *e2 = d2;

	return strmatch(e1->typename, e2->typename);
}

/* hash_get 的分配回调：把传入的临时 codec 深拷贝一份存进哈希（typename
 * 用 XSTRDUP 复制，避免指向栈上临时数据）。 */
static void *codec_alloc(void *arg)
{
	struct frrscript_codec *tmp = arg;

	struct frrscript_codec *e =
		XCALLOC(MTYPE_SCRIPT, sizeof(struct frrscript_codec));
	e->typename = XSTRDUP(MTYPE_SCRIPT, tmp->typename);
	e->encoder = tmp->encoder;
	e->decoder = tmp->decoder;

	return e;
}

/* 哈希销毁回调：先释放复制出来的 typename（它被存成 const char *，这里
 * 用局部非 const 变量做强制转换后 XFREE，故变量名带 workaround 字样），
 * 再释放 codec 结构本身。 */
static void codec_free(void *data)
{
	struct frrscript_codec *c = data;
	char *constworkaroundandihateit = (char *)c->typename;

	XFREE(MTYPE_SCRIPT, constworkaroundandihateit);
	XFREE(MTYPE_SCRIPT, c);
}

/* Lua 函数哈希工具区块：管理「函数名 -> lua_function_state」的表。 */
/* Lua function hash utils */

/* 函数哈希取键：用函数名算哈希。 */
unsigned int lua_function_hash_key(const void *data)
{
	const struct lua_function_state *lfs = data;

	return string_hash_make(lfs->name);
}

/* 函数哈希比较：函数名相等即同一 key。 */
bool lua_function_hash_cmp(const void *d1, const void *d2)
{
	const struct lua_function_state *lfs1 = d1;
	const struct lua_function_state *lfs2 = d2;

	return strmatch(lfs1->name, lfs2->name);
}

/* 分配回调：新分配一个 lua_function_state，直接借用临时项里的 name 指针
 * 与 lua_State（不复制，name 通常来自 fs 内部常量字符串）。 */
void *lua_function_alloc(void *arg)
{
	struct lua_function_state *tmp = arg;
	struct lua_function_state *lfs =
		XCALLOC(MTYPE_SCRIPT, sizeof(struct lua_function_state));

	lfs->name = tmp->name;
	lfs->L = tmp->L;
	return lfs;
}

/* 销毁回调：关闭该函数持有的 Lua 状态并释放结构。lua_State 的关闭责任
 * 就落在这里——注意只有成功加载的 L 才进了哈希表，失败路径另有关闭。 */
static void lua_function_free(void *data)
{
	struct lua_function_state *lfs = data;

	lua_close(lfs->L);
	XFREE(MTYPE_SCRIPT, lfs);
}

/* 内部 API 区块：真正执行 Lua、取结果等核心逻辑。 */
/* internal frrscript APIs */

/* 执行 Lua 函数的核心：此时栈上已压入目标函数和 nargs 个参数。
 * lua_pcall(L, nargs, 1, 0) 表示「消耗 nargs 个参数、期望 1 个返回值、
 * 不用错误处理函数」。随后按返回码分类打日志（运行/内存/错误处理/GC/
 * 未知），出错就弹掉错误对象并返回；成功则校验栈上恰好只剩 1 个值，
 * 且它必须是 table，否则报错。返回 LUA_OK(0) 表示成功。 */
int _frrscript_call_lua(struct lua_function_state *lfs, int nargs)
{

	int ret;
	ret = lua_pcall(lfs->L, nargs, 1, 0);

	switch (ret) {
	case LUA_OK:
		break;
	case LUA_ERRRUN:
		zlog_err("Lua hook call '%s' : runtime error: %s", lfs->name,
			 lua_tostring(lfs->L, -1));
		break;
	case LUA_ERRMEM:
		zlog_err("Lua hook call '%s' : memory error: %s", lfs->name,
			 lua_tostring(lfs->L, -1));
		break;
	case LUA_ERRERR:
		zlog_err("Lua hook call '%s' : error handler error: %s",
			 lfs->name, lua_tostring(lfs->L, -1));
		break;
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM <= 503
	case LUA_ERRGCMM:
		zlog_err("Lua hook call '%s' : garbage collector error: %s",
			 lfs->name, lua_tostring(lfs->L, -1));
		break;
#endif
	default:
		zlog_err("Lua hook call '%s' : unknown error: %s", lfs->name,
			 lua_tostring(lfs->L, -1));
		break;
	}

	if (ret != LUA_OK) {
		lua_pop(lfs->L, 1);
		goto done;
	}

	if (lua_gettop(lfs->L) != 1) {
		zlog_err(
			"Lua hook call '%s': Lua function should return only 1 result",
			lfs->name);
		ret = 1;
		goto done;
	}

	if (lua_istable(lfs->L, 1) != 1) {
		zlog_err(
			"Lua hook call '%s': Lua function should return a Lua table",
			lfs->name);
		ret = 1;
	}

done:
	/* LUA_OK is 0, so we can just return lua_pcall's result directly */
	return ret;
}

/* 从上次执行完的 Lua 函数返回表里取一个具名结果并解码。前置条件：栈上
 * 只剩那张返回表（用 assert 校验）。按 name 取字段，nil 则告警返回 NULL；
 * 否则用调用者给的 lua_to 解码器（如 lua_toprefix）解码（栈下标 2 即取到
 * 的字段值），再断言栈恢复原状后返回。 */
void *frrscript_get_result(struct frrscript *fs, const char *function_name,
			   const char *name,
			   void *(*lua_to)(lua_State *L, int idx))
{
	void *p;
	struct lua_function_state *lfs;
	struct lua_function_state lookup = {.name = function_name};

	lfs = hash_lookup(fs->lua_function_hash, &lookup);

	if (lfs == NULL)
		return NULL;

	/* At this point, the Lua state should have only the returned table.
	 * We will then search the table for the key/value we're interested in.
	 * Then if the value is present (i.e. non-nil), call the lua_to*
	 * decoder.
	 */
	assert(lua_gettop(lfs->L) == 1);
	assert(lua_istable(lfs->L, -1) == 1);
	lua_getfield(lfs->L, -1, name);
	if (lua_isnil(lfs->L, -1)) {
		lua_pop(lfs->L, 1);
		zlog_warn(
			"frrscript: '%s.lua': '%s': tried to decode '%s' as result but failed",
			fs->name, function_name, name);
		return NULL;
	}
	p = lua_to(lfs->L, 2);

	/* At the end, the Lua state should be same as it was at the start
	 * i.e. containing solely the returned table.
	 */
	assert(lua_gettop(lfs->L) == 1);
	assert(lua_istable(lfs->L, -1) == 1);

	return p;
}

/* 注册单个类型编解码器：先在 codec_hash 里查是否已存在，若已注册则打
 * backtrace 并 assert 失败（禁止重复注册）；否则 hash_get 用 codec_alloc
 * 深拷贝存入。 */
void frrscript_register_type_codec(struct frrscript_codec *codec)
{
	struct frrscript_codec c = *codec;

	if (hash_lookup(codec_hash, &c)) {
		zlog_backtrace(LOG_ERR);
		assert(!"Type codec double-registered.");
	}

	(void)hash_get(codec_hash, &c, codec_alloc);
}

/* 批量注册：遍历以 typename == NULL 结尾的数组，逐个调用单数版本。 */
void frrscript_register_type_codecs(struct frrscript_codec *codecs)
{
	for (int i = 0; codecs[i].typename != NULL; i++)
		frrscript_register_type_codec(&codecs[i]);
}

/* 新建脚本对象：分配 frrscript，复制脚本名，并创建一个空的函数名哈希表
 * （key 用 lua_function_hash_key，比较用 lua_function_hash_cmp）。 */
struct frrscript *frrscript_new(const char *name)
{
	struct frrscript *fs = XCALLOC(MTYPE_SCRIPT, sizeof(struct frrscript));

	fs->name = XSTRDUP(MTYPE_SCRIPT, name);
	fs->lua_function_hash =
		hash_create(lua_function_hash_key, lua_function_hash_cmp,
			    "Lua function state hash");
	return fs;
}

/* 加载脚本并定位其中某个函数。步骤：
 *   1) luaL_newstate 建新 Lua 状态，luaL_openlibs 打开标准库；
 *   2) frrlua_export_logging 注入 log.* 日志接口；
 *   3) 拼出 scriptdir/name.lua 路径（过长则报错）；
 *   4) luaL_dofile 执行脚本文件，出错则报错；
 *   5) 从全局表取该 function_name 并确认是函数，否则报错；取完弹掉；
 *   6) 若有 load_cb 则回调，非零则报错；
 *   7) 把 {函数名, L} 存进哈希表，成功返回 0。
 * 任一步失败都跳到 fail 关闭 L 并返回 1（成功时 L 归哈希表所有）。 */
int frrscript_load(struct frrscript *fs, const char *function_name,
		   int (*load_cb)(struct frrscript *))
{

	/* Set up the Lua script */
	lua_State *L = luaL_newstate();

	/* Load basic built-in Lua functions, e.g. ipairs, string, etc. */
	luaL_openlibs(L);

	frrlua_export_logging(L);

	char script_name[MAXPATHLEN];

	if (snprintf(script_name, sizeof(script_name), "%s/%s.lua", scriptdir,
		     fs->name)
	    >= (int)sizeof(script_name)) {
		zlog_err("frrscript: path to script %s/%s.lua is too long",
			 scriptdir, fs->name);
		goto fail;
	}

	if (luaL_dofile(L, script_name) != 0) {
		zlog_err("frrscript: failed loading script '%s': error: %s",
			 script_name, lua_tostring(L, -1));
		goto fail;
	}

	/* To check the Lua function, we get it from the global table */
	lua_getglobal(L, function_name);
	if (lua_isfunction(L, lua_gettop(L)) == 0) {
		zlog_err("frrscript: loaded script '%s' but %s not found",
			 script_name, function_name);
		goto fail;
	}
	/* Then pop the function (frrscript_call will push it when it needs it)
	 */
	lua_pop(L, 1);

	if (load_cb && (*load_cb)(fs) != 0) {
		zlog_err(
			"frrscript: '%s': %s: loaded but callback returned non-zero exit code",
			script_name, function_name);
		goto fail;
	}

	/* Add the Lua function state to frrscript */
	struct lua_function_state key = {.name = function_name, .L = L};

	(void)hash_get(fs->lua_function_hash, &key, lua_function_alloc);

	return 0;
fail:
	lua_close(L);
	return 1;
}

/* 删除脚本对象：清空并释放函数哈希表（每个项经 lua_function_free 关掉
 * 其 Lua 状态），再释放脚本名与 fs 本身。 */
void frrscript_delete(struct frrscript *fs)
{
	hash_clean_and_free(&fs->lua_function_hash, lua_function_free);
	XFREE(MTYPE_SCRIPT, fs->name);
	XFREE(MTYPE_SCRIPT, fs);
}

/* 初始化脚本子系统：建 codec 哈希表、记录脚本目录、并注册核心库类型。
 * 必须在其它脚本接口之前、且只调用一次。 */
void frrscript_init(const char *sd)
{
	codec_hash = hash_create(codec_hash_key, codec_hash_cmp,
				 "Lua type encoders");

	strlcpy(scriptdir, sd, sizeof(scriptdir));

	/* Register core library types */
	frrscript_register_type_codecs(frrscript_codecs_lib);
}

/* 关闭子系统：清空释放 codec 哈希（经 codec_free），并销毁名字表。 */
void frrscript_fini(void)
{
	hash_clean_and_free(&codec_hash, codec_free);

	frrscript_names_destroy();
}
/* 结束 HAVE_SCRIPTING 条件编译块，本文件到此结束。 */
#endif /* HAVE_SCRIPTING */
