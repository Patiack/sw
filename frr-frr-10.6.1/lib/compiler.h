// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2015-2017  David Lamparter, for NetDEF, Inc.
 */

/*
 * 文件导读（lib/compiler.h）
 *
 * 本文件集中封装编译器相关的可移植性工具，是 FRR 最底层的头文件之一，
 * 被 zebra.h 首先引入，几乎所有源文件都会间接用到。
 *
 * 主要提供：
 *   - 编译器 / 语言标准的编译期校验：要求 C 使用 -std=gnu11（C11）及以上，
 *     C++ 使用 C++11 及以上；不满足则直接 #error。
 *   - 属性宏包装：把 GCC / Clang 的 __attribute__ 包装成 _CONSTRUCTOR、
 *     _DESTRUCTOR、_DEPRECATED、_RET_NONNULL、_ALLOC_SIZE、fallthrough 等，
 *     并针对不同编译器版本做兼容与降级。
 *   - 符号可见性宏：DSO_PUBLIC / DSO_SELF / DSO_LOCAL。
 *   - 宏编程工具：可变参数宏分发（MACRO_VARIANT）、按参数重复（MACRO_REPEAT）、
 *     按参数列表重复（MAP_LISTS）、参数计数（PP_NARG）、字符串化（CPP_STR）。
 *   - 常用工具宏：MAX / MIN / numcmp、offsetof、container_of（及 const 安全
 *     变体）、container_of_null、array_size。
 *   - printf 格式校验与 int64_t 处理：有 frr-format 插件时直接支持 %Ld /
 *     %pI4 等；否则把 int64_t 重定义为 long long 以配合 -Wformat。
 *   - 分支预测宏 likely / unlikely、数据段宏 _DATA_SECTION、FRR_NORETURN。
 *
 * 阅读顺序：语言标准检查 -> 属性宏（按编译器分支）-> 可见性宏 -> 宏编程
 * 工具 -> MAX / MIN / container_of -> printfrr 格式处理 -> likely 等辅助宏。
 */

#ifndef _FRR_COMPILER_H
#define _FRR_COMPILER_H

#ifdef __cplusplus
extern "C" {
#endif

/* 编译期语言标准检查：C++ 需 C++11 及以上，C 需 C11 及以上，否则报错。
 * C++ 下额外把 _Static_assert 映射到 static_assert。 */
#ifdef __cplusplus
# if __cplusplus < 201103L
#  error FRRouting headers must be compiled in C++11 mode or newer
# endif
/* C++ defines static_assert(), but not _Static_assert().  C defines
 * _Static_assert() and has static_assert() in <assert.h>.  However, we mess
 * with assert() in zassert.h so let's not include <assert.h> here.
 */
# define _Static_assert static_assert
#else
# if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#  error FRRouting must be compiled with min. -std=gnu11 (GNU ISO C11 dialect)
# endif
#endif

/* 函数属性宏，用法如 void prototype(void) __attribute__((_CONSTRUCTOR(100)));
 * 下面按编译器分别定义各属性的可用版本与降级形式。 */
/* function attributes, use like
 *   void prototype(void) __attribute__((_CONSTRUCTOR(100)));
 */
/* Clang 分支：按 clang 版本探测可用属性，并用 __has_builtin 探测内建函数。 */
#if defined(__clang__)
#if __clang_major__ > 3 || (__clang_major__ == 3 && __clang_minor__ >= 5)
#  define _RET_NONNULL    , returns_nonnull
#endif
#if __has_attribute(fallthrough) && !defined(__cplusplus)
#  define fallthrough __attribute__((fallthrough));
#endif
# define _CONSTRUCTOR(x)  constructor(x)
# define _DEPRECATED(x) deprecated(x)
# if __has_builtin(assume)
#  define assume(x) __builtin_assume(x)
# endif
/* GCC 分支：同样按 GCC 版本探测属性；旧版本对 __has_attribute 做兜底。 */
#elif defined(__GNUC__)
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 9)
#  define _RET_NONNULL    , returns_nonnull
#endif
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3)
#  define _CONSTRUCTOR(x) constructor(x)
#  define _DESTRUCTOR(x)  destructor(x)
#  define _ALLOC_SIZE(x)  alloc_size(x)
#endif
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)
#  define _DEPRECATED(x) deprecated(x)
#  define assume(x) do { if (!(x)) __builtin_unreachable(); } while (0)
#endif
#if __GNUC__ < 5
#  define __has_attribute(x) 0
#endif
#if __GNUC__ >= 7 && !defined(__cplusplus)
#  define fallthrough __attribute__((fallthrough));
#endif
#endif

/* Visual Studio / VSCode 的 IntelliSense 会误报 constructor 属性带参，这里
 * 抑制该告警编号。 */
#ifdef __INTELLISENSE__
/*
 * Fix Visual Studio Code error: attribute "constructor" does not take
 * arguments.
 *
 * Caused by the macro `DEFINE_MTYPE_ATTR` in `lib/memory.h`.
 */
#pragma diag_suppress 1094
#endif /* __INTELISENSE__ */

/* 优化相关属性：_OPTIMIZE_HOT 标记热函数，_OPTIMIZE_O3 指定 O3 优化，
 * OPTIMIZE 为二者组合。属性不可用时退化为空。 */
#if __has_attribute(hot)
#  define _OPTIMIZE_HOT __attribute__((hot))
#else
#  define _OPTIMIZE_HOT
#endif
#if __has_attribute(optimize)
#  define _OPTIMIZE_O3 __attribute__((optimize("3")))
#else
#  define _OPTIMIZE_O3
#endif
#define OPTIMIZE _OPTIMIZE_O3 _OPTIMIZE_HOT

/* 符号可见性：模块代码依赖 GCC 的 visibility 扩展，非 GCC 或过旧版本直接
 * 报错。DSO_PUBLIC 对外可见，DSO_SELF 保护可见（可被本模块覆盖），
 * DSO_LOCAL 隐藏（仅模块内可见）。 */
#if !defined(__GNUC__)
#error module code needs GCC visibility extensions
#elif __GNUC__ < 4
#error module code needs GCC visibility extensions
#else
# define DSO_PUBLIC __attribute__ ((visibility ("default")))
# define DSO_SELF   __attribute__ ((visibility ("protected")))
# define DSO_LOCAL  __attribute__ ((visibility ("hidden")))
#endif

/* Solaris 因链接器限制不支持带优先级的构造 / 析构函数，故取消其带参形式。 */
#ifdef __sun
/* Solaris doesn't do constructor priorities due to linker restrictions */
#undef _CONSTRUCTOR
#undef _DESTRUCTOR
#endif

/* 属性宏的兜底定义：上面分支未定义的属性在此降级为空或基础形式。 */
/* fallback versions */
#ifndef _RET_NONNULL
# define _RET_NONNULL
#endif
#ifndef _CONSTRUCTOR
# define _CONSTRUCTOR(x) constructor
#endif
#ifndef _DESTRUCTOR
# define _DESTRUCTOR(x) destructor
#endif
#ifndef _ALLOC_SIZE
# define _ALLOC_SIZE(x)
#endif
#if !defined(fallthrough) && !defined(__cplusplus)
#define fallthrough
#endif
#ifndef _DEPRECATED
#define _DEPRECATED(x) deprecated
#endif
#ifndef assume
#define assume(x)
#endif

/* Coverity 静态分析下，把 assume 换成 __coverity_panic__() 以消除误报。 */
#ifdef __COVERITY__
/* __coverity_panic__() is named a bit poorly, it's essentially the same as
 * __builtin_unreachable().  Used to eliminate false positives.
 */
#undef assume
#define assume(x) do { if (!(x)) __coverity_panic__(); } while (0)
#endif

/* 供宏内部定义的辅助函数使用：static inline 且标记 unused，避免告警。 */
/* for helper functions defined inside macros */
#define macro_inline	static inline __attribute__((unused))
#define macro_pure	static inline __attribute__((unused, pure))

/* 若宏以函数定义结尾，用它强制调用处补分号（借静态断言触发编译错误）。 */
/* if the macro ends with a function definition */
#define MACRO_REQUIRE_SEMICOLON() \
	_Static_assert(1, "please add a semicolon after this macro")

/* 可变参数宏分发工具，用法：
 *   #define V_0()  ...
 *   #define V_1(x) ...
 *   #define V(...) MACRO_VARIANT(V, ##__VA_ARGS__)(__VA_ARGS__) */
/* variadic macros, use like:
 * #define V_0()  ...
 * #define V_1(x) ...
 * #define V(...) MACRO_VARIANT(V, ##__VA_ARGS__)(__VA_ARGS__)
 */
/* 参数计数选择器：把额外参数补进前面槽位后，第 N 个形参即被选中的名字。 */
#define _MACRO_VARIANT(A0,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10, N, ...) N

/* 两级拼接宏：_CONCAT 先展开参数再交给 _CONCAT2 做真正的 token 粘接。 */
#define _CONCAT2(a, b) a ## b
#define _CONCAT(a, b) _CONCAT2(a,b)

/* 依据参数个数，把 NAME 与后缀 _0 / _1 / ... 拼接，选中对应的变体宏。 */
#define MACRO_VARIANT(NAME, ...) \
	_CONCAT(NAME, _MACRO_VARIANT(0, ##__VA_ARGS__, \
			_10, _9, _8, _7, _6, _5, _4, _3, _2, _1, _0))

/* 借 __COUNTER__ 生成唯一名字，常用于同一作用域内多次展开的宏。 */
#define NAMECTR(name) _CONCAT(name, __COUNTER__)

/* 按参数逐个重复展开的宏，用法：
 *   #define PERARG(n) ...n...
 *   #define FOO(...) MACRO_REPEAT(PERARG, ##__VA_ARGS__) */
/* per-arg repeat macros, use like:
 * #define PERARG(n) ...n...
 * #define FOO(...) MACRO_REPEAT(PERARG, ##__VA_ARGS__)
 */

/* 各参数个数对应的展开体：对每个参数套用 NAME，依次拼接。 */
#define _MACRO_REPEAT_0(NAME)
#define _MACRO_REPEAT_1(NAME, A1) \
	NAME(A1)
#define _MACRO_REPEAT_2(NAME, A1, A2) \
	NAME(A1) NAME(A2)
#define _MACRO_REPEAT_3(NAME, A1, A2, A3) \
	NAME(A1) NAME(A2) NAME(A3)
#define _MACRO_REPEAT_4(NAME, A1, A2, A3, A4) \
	NAME(A1) NAME(A2) NAME(A3) NAME(A4)
#define _MACRO_REPEAT_5(NAME, A1, A2, A3, A4, A5) \
	NAME(A1) NAME(A2) NAME(A3) NAME(A4) NAME(A5)
#define _MACRO_REPEAT_6(NAME, A1, A2, A3, A4, A5, A6) \
	NAME(A1) NAME(A2) NAME(A3) NAME(A4) NAME(A5) NAME(A6)
#define _MACRO_REPEAT_7(NAME, A1, A2, A3, A4, A5, A6, A7) \
	NAME(A1) NAME(A2) NAME(A3) NAME(A4) NAME(A5) NAME(A6) NAME(A7)
#define _MACRO_REPEAT_8(NAME, A1, A2, A3, A4, A5, A6, A7, A8) \
	NAME(A1) NAME(A2) NAME(A3) NAME(A4) NAME(A5) NAME(A6) NAME(A7) NAME(A8)

/* 依据参数个数选择对应的 _MACRO_REPEAT_n 展开。 */
#define MACRO_REPEAT(NAME, ...) \
	MACRO_VARIANT(_MACRO_REPEAT, ##__VA_ARGS__)(NAME, ##__VA_ARGS__)

/* 按“参数列表”重复的宏，用法：
 *   #define foo(...) MAP_LISTS(F, ##__VA_ARGS__)
 * 其中 F 的元数等于每个参数列表的元素个数，例如 MAP_LISTS(f, (a, b), (c, d))
 * 展开为 f(a, b); f(c, d)。 */
/* per-arglist repeat macro, use like this:
 * #define foo(...) MAP_LISTS(F, ##__VA_ARGS__)
 * where F is a n-ary function where n is the number of args in each arglist.
 * e.g.: MAP_LISTS(f, (a, b), (c, d))
 * expands to: f(a, b); f(c, d)
 */

/* 展开辅助宏：把参数原样展开，用于阻断二次宏展开的副作用。 */
#define ESC(...) __VA_ARGS__
#define MAP_LISTS(M, ...)                                                      \
	_CONCAT(_MAP_LISTS_, PP_NARG(__VA_ARGS__))(M, ##__VA_ARGS__)
#define _MAP_LISTS_0(M)
#define _MAP_LISTS_1(M, _1) ESC(M _1)
#define _MAP_LISTS_2(M, _1, _2) ESC(M _1; M _2)
#define _MAP_LISTS_3(M, _1, _2, _3) ESC(M _1; M _2; M _3)
#define _MAP_LISTS_4(M, _1, _2, _3, _4) ESC(M _1; M _2; M _3; M _4)
#define _MAP_LISTS_5(M, _1, _2, _3, _4, _5) ESC(M _1; M _2; M _3; M _4; M _5)
#define _MAP_LISTS_6(M, _1, _2, _3, _4, _5, _6)                                \
	ESC(M _1; M _2; M _3; M _4; M _5; M _6)
#define _MAP_LISTS_7(M, _1, _2, _3, _4, _5, _6, _7)                            \
	ESC(M _1; M _2; M _3; M _4; M _5; M _6; M _7)
#define _MAP_LISTS_8(M, _1, _2, _3, _4, _5, _6, _7, _8)                        \
	ESC(M _1; M _2; M _3; M _4; M _5; M _6; M _7; M _8)

/* 宏告警工具：在宏内容里写 CPP_WARN("...") 即可在使用处产生编译告警。 */
/*
 * for warnings on macros, put in the macro content like this:
 *   #define MACRO BLA CPP_WARN("MACRO has been deprecated")
 */
/* 字符串化宏。 */
#define CPP_STR(X) #X

/* 告警 / 提示实现，按编译器分派：ICC 用 message，较新的 GCC / Clang 用
 * warning / message 原语，其余编译器退化为空。 */
#if defined(__ICC)
#define CPP_NOTICE(text) _Pragma(CPP_STR(message __FILE__ ": " text))
#define CPP_WARN(text) CPP_NOTICE(text)

#elif (defined(__GNUC__)                                                       \
       && (__GNUC__ >= 5 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)))           \
	|| (defined(__clang__)                                                 \
	    && (__clang_major__ >= 4                                           \
		|| (__clang_major__ == 3 && __clang_minor__ >= 5)))
#define CPP_WARN(text) _Pragma(CPP_STR(GCC warning text))
#define CPP_NOTICE(text) _Pragma(CPP_STR(message text))

#else
#define CPP_WARN(text)
#define CPP_NOTICE(text)
#endif

/* MAX / MIN 并非普遍定义，但很实用；注意 glibc 的 sys/param.h 已有 MIN 定义，
 * 故此处先 undef 再重定义。二者均用 typeof 保证只求值一次并带类型。 */
/* MAX / MIN are not commonly defined, but useful */
/* note: glibc sys/param.h has #define MIN(a,b) (((a)<(b))?(a):(b)) */
/* 若已存在 MAX / MIN 定义则先取消，避免与 glibc 版本冲突。 */
#ifdef MAX
#undef MAX
#endif
/* 取较大值：把两个操作数各存临时变量后比较，避免重复求值副作用。 */
#define MAX(a, b)                                                              \
	({                                                                     \
		typeof(a) _max_a = (a);                                        \
		typeof(b) _max_b = (b);                                        \
		_max_a > _max_b ? _max_a : _max_b;                             \
	})
/* 若已存在 MIN 定义则先取消。 */
#ifdef MIN
#undef MIN
#endif
/* 取较小值，语义同 MAX。 */
#define MIN(a, b)                                                              \
	({                                                                     \
		typeof(a) _min_a = (a);                                        \
		typeof(b) _min_b = (b);                                        \
		_min_a < _min_b ? _min_a : _min_b;                             \
	})

/* 数值比较：返回 -1 / 0 / 1，可用于排序回调，避免直接相减溢出。 */
#define numcmp(a, b)                                                           \
	({                                                                     \
		typeof(a) _cmp_a = (a);                                        \
		typeof(b) _cmp_b = (b);                                        \
		(_cmp_a < _cmp_b) ? -1 : ((_cmp_a > _cmp_b) ? 1 : 0);          \
	})

/* offsetof：优先用编译器内建，否则用经典的以空指针取成员地址手法。 */
#ifndef offsetof
#ifdef __compiler_offsetof
#define offsetof(TYPE, MEMBER) __compiler_offsetof(TYPE,MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif
#endif

/* container_of 可能被其它头定义，先取消再重定义。 */
#ifdef container_of
#undef container_of
#endif

/* C 版 container_of：由成员地址反推外层结构体地址。用编译器内建判断指针
 * 是否 const，从而在不需要额外提示的情况下保留 const 限定，避免剥掉
 * const 的编译告警。 */
#if !(defined(__cplusplus) || defined(test__cplusplus))
/* this variant of container_of() retains 'const' on pointers without needing
 * to be told to do so.  The following will all work without warning:
 *
 * struct member *p;
 * const struct member *cp;
 *
 * const struct cont *x = container_of(cp, struct cont, member);
 * const struct cont *x = container_of(cp, const struct cont, member);
 * const struct cont *x = container_of(p,  struct cont, member);
 * const struct cont *x = container_of(p,  const struct cont, member);
 * struct cont *x       = container_of(p,  struct cont, member);
 *
 * but the following will generate warnings about stripping const:
 *
 * struct cont *x       = container_of(cp, struct cont, member);
 * struct cont *x       = container_of(cp, const struct cont, member);
 * struct cont *x       = container_of(p,  const struct cont, member);
 */
#define container_of(ptr, type, member)                                        \
	(__builtin_choose_expr(                                                \
		__builtin_types_compatible_p(typeof(&((type *)0)->member),     \
			typeof(ptr))                                           \
		    ||  __builtin_types_compatible_p(void *, typeof(ptr)),     \
		({                                                             \
			typeof(((type *)0)->member) *__mptr = (void *)(ptr);   \
			(type *)((char *)__mptr - offsetof(type, member));     \
		}),                                                            \
		({                                                             \
			typeof(((const type *)0)->member) *__mptr = (ptr);     \
			(const type *)((const char *)__mptr -                  \
					offsetof(type, member));               \
		})                                                             \
	))
#else
/* C++ 版 container_of：当前 C++ 编译器不支持上面那些内建，故不做 const 检查。 */
/* current C++ compilers don't have the builtins used above; so this version
 * of the macro doesn't do the const check. */
#define container_of(ptr, type, member)                                        \
		({                                                             \
			const typeof(((type *)0)->member) *__mptr = (ptr);     \
			(type *)((char *)__mptr - offsetof(type, member));     \
		})
#endif

/* container_of 的空安全版本：ptr 为空时返回 NULL，否则正常反推。 */
#define container_of_null(ptr, type, member)                                   \
	({                                                                     \
		typeof(ptr) _tmp = (ptr);                                      \
		_tmp ? container_of(_tmp, type, member) : NULL;                \
	})

/* 计算数组元素个数（仅对真正的数组有效，对指针无效）。 */
#define array_size(ar) (sizeof(ar) / sizeof(ar[0]))

/* 下面这组宏用于统计传给“类函数宏”的可变参数个数（最多 63 个），
 * PP_NARG(...) 即结果。实现方式是把计数序列倒序拼在参数后，取第 N 个。 */
/* Some insane macros to count number of varargs to a functionlike macro */
/* 参数选择器：取第 63 个形参 N，即为实际参数个数。 */
#define PP_ARG_N( \
          _1,  _2,  _3,  _4,  _5,  _6,  _7,  _8,  _9, _10, \
         _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, \
         _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, \
         _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, \
         _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, \
         _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, \
         _61, _62, _63, N, ...) N

/* 倒序计数序列 63..0，供 PP_NARG 拼接计数。 */
#define PP_RSEQ_N()                                        \
         62, 61, 60,                                       \
         59, 58, 57, 56, 55, 54, 53, 52, 51, 50,           \
         49, 48, 47, 46, 45, 44, 43, 42, 41, 40,           \
         39, 38, 37, 36, 35, 34, 33, 32, 31, 30,           \
         29, 28, 27, 26, 25, 24, 23, 22, 21, 20,           \
         19, 18, 17, 16, 15, 14, 13, 12, 11, 10,           \
          9,  8,  7,  6,  5,  4,  3,  2,  1,  0

/* 参数计数入口：在参数后追加计数序列，交给 PP_ARG_N 选中个数。 */
#define PP_NARG_(...) PP_ARG_N(__VA_ARGS__)
#define PP_NARG(...)     PP_NARG_(_, ##__VA_ARGS__, PP_RSEQ_N())


/* 关于 printfrr 与格式校验的说明：printfrr 支持用 "%Ld" 表示 int64_t，但
 * GCC / Clang 认为 "%Ld" 是 long long，而 64 位系统上 int64_t 多为 long，
 * 二者不一致。若启用 frr-format 编译器插件（_FRR_ATTRIBUTE_PRINTFRR），
 * 则由插件接管格式校验，什么都不用做；否则为让 -Wformat 仍可用，简单粗暴
 * 地把 int64_t 重定义为 long long（世界暂时这样，用下面的静态断言兜底）。 */
/* sigh. this is so ugly, it overflows and wraps to being nice again.
 *
 * printfrr() supports "%Ld" for <int64_t>, whatever that is typedef'd to.
 * However, gcc & clang think that "%Ld" is <long long>, which doesn't quite
 * match up since int64_t is <long> on a lot of 64-bit systems.
 *
 * If we have _FRR_ATTRIBUTE_PRINTFRR, we loaded a compiler plugin that
 * replaces the whole format checking bits with a custom version that
 * understands "%Ld" (along with "%pI4" and co.), so we don't need to do
 * anything.
 *
 * If we don't have that attribute...  we still want -Wformat to work.  So,
 * this is the "f*ck it" approach and we just redefine int64_t to always be
 * <long long>.  This should work until such a time that <long long> is
 * something else (e.g. 128-bit integer)...  let's just guard against that
 * with the _Static_assert below and work with the world we have right now,
 * where <long long> is always 64-bit.
 */

/* 这两个头需先包含，才能在后面“覆盖”其中的 int64_t / PRI*64 定义。 */
/* these need to be included before any of the following, so we can
 * "overwrite" things.
 */
#include <stdint.h>
#include <inttypes.h>

/* 启用 frr-format 插件：用其自定义的 frr_format 属性做格式校验，并把
 * PRIu64 / PRId64 / PRIx64 重定义为 %Lu / %Ld / %Lx。 */
#ifdef _FRR_ATTRIBUTE_PRINTFRR
#define PRINTFRR(a, b) __attribute__((frr_format("frr_printf", a, b)))

#undef PRIu64
#undef PRId64
#undef PRIx64
#define PRIu64 "Lu"
#define PRId64 "Ld"
#define PRIx64 "Lx"

/* 未启用插件：退回标准 format 属性（NetBSD 用 gnu_syslog 变体）。 */
#else /* !_FRR_ATTRIBUTE_PRINTFRR */
#ifdef __NetBSD__
#define PRINTFRR(a, b) __attribute__((format(gnu_syslog, a, b)))
#else
#define PRINTFRR(a, b) __attribute__((format(printf, a, b)))
#endif

/* frr-format 插件目前只支持 C，故 C++ 下不做这些替换（也可能影响 C++）。
 * 具体做法：把 int64_t / uint64_t 换成 _int64_t / _uint64_t（即 long long），
 * 以便与 GCC / Clang 对 PRI*64 的预期一致。 */
/* frr-format plugin is C-only for now, so no point in doing these shenanigans
 * for C++...  (also they can break some C++ stuff...)
 */
#ifndef __cplusplus
/* these should be typedefs, but might also be #define */
#ifdef uint64_t
#undef uint64_t
#endif
#ifdef int64_t
#undef int64_t
#endif

/* can't overwrite the typedef, but we can replace int64_t with _int64_t */
typedef unsigned long long _uint64_t;
#define uint64_t _uint64_t
typedef signed long long _int64_t;
#define int64_t _int64_t

/* if this breaks, 128-bit machines may have entered reality (or <long long>
 * is something weird)
 */
_Static_assert(sizeof(_uint64_t) == 8 && sizeof(_int64_t) == 8,
	       "nobody expects the spanish intquisition");

/* since we redefined int64_t, we also need to redefine PRI*64 */
#undef PRIu64
#undef PRId64
#undef PRIx64
#define PRIu64 "llu"
#define PRId64 "lld"
#define PRIx64 "llx"

#endif /* !__cplusplus */
#endif /* !_FRR_ATTRIBUTE_PRINTFRR */

/* 透明联合体（transparent union）辅助：让接受多种指针类型的函数在调用处
 * 免写强制转换，从而保留类型安全（否则调用方强转任意指针类型都会通过）。 */
/* helper to get type safety/avoid casts on calls
 * (w/o this, functions accepting all prefix types need casts on the caller
 * side, which strips type safety since the cast will accept any pointer
 * type.)
 */
/* C 版：uniontype 定义一个成员指针，TRANSPARENT_UNION 为透明联合体属性。
 * C++ 版不依赖透明联合体，而是用构造函数赋值（见 #else 分支）。 */
#ifndef __cplusplus
#define uniontype(uname, typename, fieldname) typename *fieldname;
#define TRANSPARENT_UNION __attribute__((transparent_union))
#else
#define uniontype(uname, typename, fieldname)                                  \
	typename *fieldname;                                                   \
	uname(typename *x)                                                     \
	{                                                                      \
		this->fieldname = x;                                           \
	}
#define TRANSPARENT_UNION
#endif

/* IntelliSense 会把透明联合体参数误判为类型不兼容，这里抑制对应告警。 */
#ifdef __INTELLISENSE__
/*
 * Fix Visual Studio Code error: argument of type "struct prefix *" is
 * incompatible with parameter of type "union prefixptr".
 *
 * This is caused by all functions having the transparent unions in the
 * prototype. Example: `prefixptr` and `prefixconstptr` from `lib/prefix.h`.
 */
#pragma diag_suppress 167
#endif /* __INTELISENSE__ */

/* 分支预测提示：likely / unlikely 借 __builtin_expect 影响代码布局；不支持的
 * 编译器退化为对表达式取两次逻辑非。 */
#if defined(__GNUC__) && (__GNUC__ >= 3)
#define likely(_x) __builtin_expect(!!(_x), 1)
#define unlikely(_x) __builtin_expect(!!(_x), 0)
#else
#define likely(_x) !!(_x)
#define unlikely(_x) !!(_x)
#endif

/* _DATA_SECTION：把符号放入指定数据段；macOS 用双下划线段名，其余用 .data.
 * 前缀。 */
#ifdef __MACH__
#define _DATA_SECTION(name) __attribute__((section("__DATA," name)))
#else
#define _DATA_SECTION(name) __attribute__((section(".data." name)))
#endif

/* 标记函数不会返回（noreturn）。 */
/* Wrapper for the 'noreturn' metadata */
#define FRR_NORETURN __attribute__((noreturn))

#ifdef __cplusplus
}
#endif

#endif /* _FRR_COMPILER_H */
