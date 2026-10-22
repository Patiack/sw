// SPDX-License-Identifier: GPL-2.0-or-later
/* Scripting foo
 * Copyright (C) 2020  NVIDIA Corporation
 * Quentin Young
 */
#ifndef __FRRSCRIPT_H__
#define __FRRSCRIPT_H__
/*
 * ============================ 文件导读 ============================
 * frrscript.h —— FRR「脚本子系统」的高层接口（frrscript）
 *
 * 一、职责
 *   如果说 frrlua 只管「单个值怎么在 C 与 Lua 栈之间搬」，本模块则负责
 *   更高层的事：按名字管理若干个 Lua 脚本文件、把脚本里的函数加载进来、
 *   用 C 参数调用它、再把脚本返回的 table 解回 C 变量。它是守护进程使用
 *   Lua 钩子的入口。
 *
 * 二、核心概念
 *   - 「脚本名」  要加载的 .lua 文件名（不含扩展名），如 "bgp_magic"。
 *   - 「函数名」  脚本里的某个 Lua 函数，如 "on_import"。
 *   - frrscript   一个脚本的运行时对象，内含「函数名 -> lua_function_state」
 *     的哈希表。
 *   - lua_function_state  单个已加载 Lua 函数的抽象：函数名 + 它所属的
 *     lua_State。
 *   - codec（编解码器）  每个 C 类型注册一对 encoder / decoder，调用时
 *     靠 _Generic 按实参类型自动挑选。
 *
 * 三、核心数据结构与宏
 *   - struct frrscript_names_entry  全局「钩子函数名 -> 脚本名」映射表项，
 *     由 typesafe hash（frrscript_names）管理。
 *   - struct frrscript_codec  类型名 + encoder + decoder。
 *   - struct frrscript_env    一次调用里 (类型名, 绑定名, 值) 三元组。
 *   - ENCODE_ARGS / DECODE_ARGS / *_WITH_STATE  用 _Generic 完成类型分派。
 *   - frrscript_call(...)  最核心的宏：查函数、清栈、压函数、编码实参、
 *     调 _frrscript_call_lua 执行、再用 decoder 把结果写回实参。
 *
 * 四、与其他模块的关系
 *   - 依赖 frrlua.h 提供的各 lua_pushX / lua_decodeX。
 *   - 用 lib/typesafe.h 的 hash 模板管理名字表与函数表。
 *   - zlog.h 打日志；实际实现见 frrscript.c。
 *   - 各守护进程（bgpd、zebra）通过注册 codec 与调用 frrscript_call 使用它。
 *
 * 五、阅读顺序
 *   先看 struct frrscript / lua_function_state 理解对象模型 -> 名字哈希相
 *   关接口 -> codec 与 frrscript_env -> frrscript_new/load/delete 生命周期
 *   -> ENCODE/DECODE 宏 -> frrscript_call -> frrscript_get_result。
 *
 * 六、易踩的坑
 *   1. 整套东西被 HAVE_SCRIPTING 包住，未开脚本时不存在。
 *   2. frrscript_call 用 GCC 语句表达式与 MAP_LISTS/PP_NARG 宏魔法实现，
 *      必须用 GCC/clang 编译。
 *   3. decoder 分配内存用 MTYPE_TMP，与 frrlua 的 MTYPE_SCRIPT_RES 不同。
 *   4. 调用前函数必须已经被 frrscript_load 加载，否则宏会报错并返回 1。
 * ================================================================
 */


/* 仅在启用脚本支持时本头文件才有内容。 */
#ifdef HAVE_SCRIPTING

/* 标准大小 / 布尔 / 定长整数类型定义。 */
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
/* 提供 MAXPATHLEN（脚本名、函数名缓冲长度）。 */
#include <sys/param.h>

/* Lua 核心头，结构体里会出现 lua_State 指针。 */
#include <lua.h>

/* FRR 断言封装。 */
#include "assert.h"
/* 编译器属性辅助宏（如 __attribute__）。 */
#include "lib/compiler.h"
/* FRR 的 typesafe 容器框架，用于 PREDECL_HASH / DECLARE_HASH 哈希模板。 */
#include "lib/typesafe.h"

/* include-what-you-use doesn't quite seem to understand _Generic, and these
 * headers are only needed for the functions used there
 */
// IWYU pragma: begin_keep
/* 各类型的 lua_push / lua_decode 由 frrlua.h 声明；这些仅被下方 _Generic
 * 用到的函数引用，IWYU 对 _Generic 理解不到位，故显式保留这两行。 */
#include "lib/frrlua.h"
#include "lib/zlog.h"
// IWYU pragma: end_keep

/* C++ 兼容：按 C 链接约定导出。 */
#ifdef __cplusplus
extern "C" {
#endif

/* vty 命令输出结构的前置声明（frrscript_names_config_write 用）。 */
struct vty;

/* Forward declarations */
/* zebra 数据面上下文的编解码由 zebra 侧提供，这里只前置声明并 extern
 * 引用，避免 lib 依赖 zebra 的实现。 */
struct zebra_dplane_ctx;
extern void lua_pushzebra_dplane_ctx(lua_State *L,
				     const struct zebra_dplane_ctx *ctx);
extern void lua_decode_zebra_dplane_ctx(lua_State *L, int idx,
					struct zebra_dplane_ctx *ctx);
/* bgp 侧的 peer / attr 编解码同样由 bgpd 提供。 */
/* From bgp */
struct peer;
struct attr;
void lua_pushpeer(lua_State *L, const struct peer *peer);
void lua_pushattr(lua_State *L, const struct attr *attr);
void lua_decode_attr(lua_State *L, int idx, struct attr *attr);

/*
 * Script name hash
 */
/* typesafe 哈希模板的前置声明，稍后用 DECLARE_HASH 生成具体类型。 */
PREDECL_HASH(frrscript_names);

/* 名字表的一项：把「Lua 钩子函数名」映射到「它所在的脚本名」。配置阶段
 * 用它解析出用户为某钩子挂的脚本。 */
struct frrscript_names_entry {
	/* Name of a Lua hook call */
	char function_name[MAXPATHLEN];

	/* Lua script in which to look for it */
	char script_name[MAXPATHLEN];

	struct frrscript_names_item item;
};

/* 全局的名字表头部实例，保存所有已挂接的钩子函数名字项。 */
extern struct frrscript_names_head frrscript_names_hash;

/* 把名字表配置（钩子函数 -> 脚本名）写进 vty 输出，即 show running-config
 * 里那一部分配置的来源。 */
extern void frrscript_names_config_write(struct vty *vty);

/* 名字表的两大基石：比较函数（判断两项是否同一个 key）与哈希函数（由
 * 项算出桶号）。二者由 DECLARE_HASH 绑定到模板。 */
int frrscript_names_hash_cmp(const struct frrscript_names_entry *snhe1,
			     const struct frrscript_names_entry *snhe2);
uint32_t frrscript_names_hash_key(const struct frrscript_names_entry *snhe);

/* 用哈希模板生成 frrscript_names 相关的增删查函数；item 指明项内的链接
 * 字段名，末尾两个是上面的比较 / 哈希函数。 */
DECLARE_HASH(frrscript_names, struct frrscript_names_entry, item,
	     frrscript_names_hash_cmp, frrscript_names_hash_key);

/* 向名字表登记一个钩子函数名（配置里出现新钩子时调用）。 */
int frrscript_names_add_function_name(const char *function_name);
/* 销毁整个名字表，退出时清理。 */
void frrscript_names_destroy(void);
/* 为已登记的钩子函数指定它所在的脚本名。 */
int frrscript_names_set_script_name(const char *function_name,
				    const char *script_name);
/* 反查某钩子函数对应的脚本名，找不到返回 NULL。 */
char *frrscript_names_get_script_name(const char *function_name);

/* 编码器函数指针：把 C 值压入 Lua 栈（void * 是泛化后的值）。 */
typedef void (*encoder_func)(lua_State *, const void *);
/* 解码器函数指针：从栈上 idx 取出值，新分配并返回（MTYPE_TMP）。 */
typedef void *(*decoder_func)(lua_State *, int);

/* 一个类型的编解码器登记项：typename 是用户自定义的类型标签（如 "peer"），
 * 配上该类型的编码 / 解码函数。frrscript_register_type_codec 用它建表。 */
struct frrscript_codec {
	const char *typename;
	encoder_func encoder;
	decoder_func decoder;
};

/* 单个已加载 Lua 函数的抽象：函数名，以及它所在的 lua_State。哈希表以
 * 该结构为值、以 name 为 key（见 lua_function_hash_* 系列）。 */
struct lua_function_state {
	const char *name;
	lua_State *L;
};

/* 一个脚本的运行时对象：名字 + 「函数名 -> lua_function_state」的哈希表。
 * 由 frrscript_new 创建、frrscript_delete 销毁。 */
struct frrscript {
	/* Script name */
	char *name;

	/* Hash of Lua function name to Lua function state */
	struct hash *lua_function_hash;
};


/*
 * Hash related functions for lua_function_hash
 */

/* 函数名哈希表的三件套：分配一项、算 key、比较两项相等。 */
void *lua_function_alloc(void *arg);

unsigned int lua_function_hash_key(const void *data);

bool lua_function_hash_cmp(const void *d1, const void *d2);

/* 一次脚本调用里向 Lua 传入 / 从 Lua 取回的一个绑定：类型名 + 绑定名 +
 * 值指针。调用宏据此把 C 变量与 Lua 变量对应起来。 */
struct frrscript_env {
	/* Value type */
	const char *typename;

	/* Binding name */
	const char *name;

	/* Value */
	const void *val;
};

/*
 * Create new struct frrscript for a Lua script.
 * This will hold the states for the Lua functions in this script.
 *
 * scriptname
 *     Name of the Lua script file, without the .lua
 */
/* 新建一个脚本对象，用于后续加载该脚本里的 Lua 函数（文件名不含 .lua）。 */
struct frrscript *frrscript_new(const char *scriptname);

/*
 * Load a function into frrscript, run callback if any
 */
/* 从脚本里加载一个具名函数到 fs；加载成功后可反复用 frrscript_call 调用。
 * load_cb 非空时在加载后回调（供模块做初始化）。 */
int frrscript_load(struct frrscript *fs, const char *function_name,
		   int (*load_cb)(struct frrscript *));

/*
 * Delete Lua function states and frrscript
 */
/* 删除脚本对象：销毁其中所有函数状态及 Lua 状态，释放 frrscript 本身。 */
void frrscript_delete(struct frrscript *fs);

/*
 * Register a Lua codec for a type.
 *
 * tname
 *    Name of type; e.g., "peer", "ospf_interface", etc. Chosen at will.
 *
 * codec(s)
 *    Function pointer to codec struct. Encoder function should push a Lua
 *    table representing the passed argument - which will have the C type
 *    associated with the chosen 'tname' to the provided stack. The decoder
 *    function should pop a value from the top of the stack and return a heap
 *    chunk containing that value. Allocations should be made with MTYPE_TMP.
 *
 *    If using the plural function variant, pass a NULL-terminated array.
 *
 */
/* 注册一个类型编解码器；复数版本接收以 NULL 结尾的数组，一次注册一批。 */
void frrscript_register_type_codec(struct frrscript_codec *codec);
void frrscript_register_type_codecs(struct frrscript_codec *codecs);

/*
 * Initialize scripting subsystem. Call this before anything else.
 *
 * scriptdir
 *    Directory in which to look for scripts
 */
/* 初始化脚本子系统，必须在其它脚本接口之前调用；scriptdir 指定到哪找
 * .lua 文件。 */
void frrscript_init(const char *scriptdir);

/*
 * On shutdown clean up memory associated with the scripting subsystem
 */
/* 关闭时清理脚本子系统占用的内存与全局状态。 */
void frrscript_fini(void);

/*
 * This macro is mapped to every (name, value) in frrscript_call,
 * so this in turn maps them onto their encoders
 */
/* 把 frrscript_call 里的每个 (名, 值) 参数映射到对应编码器：取出该函数
 * 所属 Lua 状态 lfs->L，交给类型分派宏 ENCODE_ARGS_WITH_STATE。 */
#define ENCODE_ARGS(name, value) ENCODE_ARGS_WITH_STATE(lfs->L, (value))

/*
 * This macro is also mapped to every (name, value) in frrscript_call, but
 * not every value can be mapped to its decoder - only those that appear
 * in the returned table will. To find out if they appear in the returned
 * table, first pop the value and check if its nil. Only call the decoder
 * if non-nil.
 *
 * At the end, the only thing left on the stack should be the
 * returned table.
 */
/* 与编码相反：对每个 (名, 值) 参数，从返回表里按 name 取值。若该字段为
 * nil 说明脚本没改它，跳过；否则调用对应解码器写回实参。每步后断言栈上
 * 只留下「返回的那张表」一层，保证栈平衡。 */
#define DECODE_ARGS(name, value)                                               \
	do {                                                                   \
		lua_getfield(lfs->L, 1, (name));                               \
		if (lua_isnil(lfs->L, 2)) {                                    \
			lua_pop(lfs->L, 1);                                    \
		} else {                                                       \
			DECODE_ARGS_WITH_STATE(lfs->L, (value));               \
		}                                                              \
		assert(lua_gettop(lfs->L) == 1);                               \
	} while (0)

/*
 * Noop function. Used below where we need a noop decoder for any type.
 */
/* 无操作的占位解码器：某些类型没有注册解码器时，_Generic 的 default 分支
 * 落到它身上，什么都不做（等价于「该值只出不进」）。 */
void _lua_decode_noop(lua_State *L, ...);

/*
 * Maps the type of value to its encoder/decoder.
 * Add new mappings here.
 *
 * L
 *    Lua state
 * scriptdir
 *    Directory in which to look for scripts
 */
/* 类型分派核心：这是一个巨大的 _Generic 表达式，按实参 value 的 C 类型
 * 挑选对应的 lua_pushXxx。新增可传类型时，在此加一条映射即可。注意
 * int / long long 直接映射到 lua_pushinteger，指针类型映射到各自 push。 */
#define ENCODE_ARGS_WITH_STATE(L, value)                                       \
	_Generic((value), \
int : lua_pushinteger,                                          \
int * : lua_pushintegerp,                                       \
long long : lua_pushinteger,                                    \
long long * : lua_pushlonglongp,                                \
struct prefix * : lua_pushprefix,                               \
struct interface * : lua_pushinterface,                         \
struct in_addr * : lua_pushinaddr,                              \
struct in6_addr * : lua_pushin6addr,                            \
union sockunion * : lua_pushsockunion,                          \
char * : lua_pushstring_wrapper,                                \
struct attr * : lua_pushattr,                                   \
struct peer * : lua_pushpeer,                                   \
const struct prefix * : lua_pushprefix,                         \
const struct ipaddr * : lua_pushipaddr,                         \
const struct ethaddr * : lua_pushethaddr,                       \
const struct nexthop_group * : lua_pushnexthop_group,           \
const struct nexthop * : lua_pushnexthop,                       \
struct zebra_dplane_ctx * : lua_pushzebra_dplane_ctx            \
)((L), (value))

/* 解码侧的类型分派 _Generic：按 value 类型挑 lua_decodeXxx；未列出的类型
 * 落到 default 的 _lua_decode_noop。注意解码器都从栈顶 -1 取一个值。 */
#define DECODE_ARGS_WITH_STATE(L, value)                                       \
	_Generic((value), \
int * : lua_decode_integerp,                                    \
long long * : lua_decode_longlongp,                             \
struct prefix * : lua_decode_prefix,                            \
struct interface * : lua_decode_interface,                      \
struct in_addr * : lua_decode_inaddr,                           \
struct in6_addr * : lua_decode_in6addr,                         \
union sockunion * : lua_decode_sockunion,                       \
char * : lua_decode_stringp,                                    \
struct attr * : lua_decode_attr,                                \
default : _lua_decode_noop                                      \
)((L), -1, (value))

/*
 * Call Lua function state (abstraction for a single Lua function)
 *
 * lfs
 *    The Lua function to call; this should have been loaded in by
 *    frrscript_load(). nargs Number of arguments the function accepts
 *
 * Returns:
 *    0 if the script ran successfully, nonzero otherwise.
 */
/* 真正执行 Lua 函数：入参 lfs 是已加载的函数状态，nargs 是压入栈的参数
 * 个数（即被编码的实参数量）。请求栈容量后调用并检查返回值，成功返回 0。 */
int _frrscript_call_lua(struct lua_function_state *lfs, int nargs);

/*
 * Wrapper for calling Lua function state.
 *
 * The Lua function name (f) to run should have already been checked by
 * frrscript_load. So this wrapper will:
 * 1) Find the Lua function state, which contains the Lua state
 * 2) Clear the Lua state (there may be leftovers items from previous call)
 * 3) Push the Lua function (f)
 * 4) Map frrscript_call arguments onto their encoder and decoders, push those
 * 5) Call _frrscript_call_lua (Lua execution takes place)
 * 6) Write back to frrscript_call arguments using their decoders
 *
 * This wrapper can be called multiple times (after one frrscript_load).
 *
 * fs
 *    The struct frrscript in which the Lua fuction was loaded into
 * f
 *    Name of the Lua function.
 *
 * Returns:
 *    0 if the script ran successfully, nonzero otherwise.
 */
/* 调用 Lua 函数的顶层宏（GCC 语句表达式实现）。给定脚本对象 fs 与函数名 f
 * 以及若干 (名, 值) 实参，流程为：
 *   1) 在 fs 的函数表里查 lfs，查不到则 zlog_err 并返回 1；
 *   2) lua_settop(lfs->L, 0) 清栈，lua_getglobal 压入目标函数；
 *   3) MAP_LISTS(ENCODE_ARGS, ...) 把每个实参编码压栈；
 *   4) _frrscript_call_lua 执行脚本，非零则报错返回 1；
 *   5) 成功则 MAP_LISTS(DECODE_ARGS, ...) 把脚本改过的字段写回实参，返回 0。
 * 参数按「名字, 变量」成对给出，名字同时用作 Lua 侧字段名与解码键。 */
#define frrscript_call(fs, f, ...)                                                                                                                                 \
	({                                                                                                                                                         \
		struct lua_function_state lookup = {.name = (f)};                                                                                                  \
		struct lua_function_state *lfs;                                                                                                                    \
		lfs = hash_lookup((fs)->lua_function_hash, &lookup);                                                                                               \
		lfs == NULL ? ({                                                                                                                                   \
			zlog_err(                                                                                                                                  \
				"frrscript: '%s.lua': '%s': tried to call this function but it was not loaded",                                                    \
				(fs)->name, (f));                                                                                                                  \
			1;                                                                                                                                         \
		})                                                                                                                                                 \
			    : ({                                                                                                                                   \
				      lua_settop(lfs->L, 0);                                                                                                       \
				      lua_getglobal(lfs->L, f);                                                                                                    \
				      MAP_LISTS(ENCODE_ARGS, ##__VA_ARGS__);                                                                                       \
				      _frrscript_call_lua(                                                                                                         \
					      lfs, PP_NARG(__VA_ARGS__));                                                                                          \
			      }) != 0                                                                                                                              \
				      ? ({                                                                                                                         \
						zlog_err(                                                                                                          \
							"frrscript: '%s.lua': '%s': this function called but returned non-zero exit code. No variables modified.", \
							(fs)->name, (f));                                                                                          \
						1;                                                                                                                 \
					})                                                                                                                         \
				      : ({                                                                                                                         \
						MAP_LISTS(DECODE_ARGS,                                                                                             \
							  ##__VA_ARGS__);                                                                                          \
						0;                                                                                                                 \
					});                                                                                                                        \
	})

/*
 * Get result from finished function
 *
 * fs
 *    The script. This script must have been run already.
 * function_name
 *    Name of the Lua function.
 * name
 *    Name of the result.
 *    This will be used as a string key to retrieve from the table that the
 *    Lua function returns.
 *    The name here should *not* appear in frrscript_call.
 * lua_to
 *    Function pointer to a lua_to decoder function.
 *    This function should allocate and decode a value from the Lua state.
 *
 * Returns:
 *    A pointer to the decoded value from the Lua state, or NULL if no such
 *    value.
 */
/* 从函数执行后的返回 table 里，按 name 取一个值并用 lua_to 解码。lua_to
 * 通常传 lua_toprefix / lua_tostringp 这类「新分配并返回」的解码器；取出
 * 的值由调用者负责释放。找不到对应字段时返回 NULL。 */
void *frrscript_get_result(struct frrscript *fs, const char *function_name,
			   const char *name,
			   void *(*lua_to)(lua_State *L, int idx));

#ifdef __cplusplus
}
/* 结束 extern "C" 块。 */
#endif /* __cplusplus */

/* 结束 HAVE_SCRIPTING 条件块。 */
#endif /* HAVE_SCRIPTING */

/* 结束多重包含保护 __FRRSCRIPT_H__。 */
#endif /* __FRRSCRIPT_H__ */
