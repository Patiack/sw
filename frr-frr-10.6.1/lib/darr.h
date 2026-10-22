// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * June 23 2023, Christian Hopps <chopps@labn.net>
 *
 * Copyright (c) 2023, LabN Consulting, L.L.C.
 */
/*
 * 【文件导读】darr.h —— FRR 现代“动态数组”容器的公开接口
 *
 * 一、这个文件是干什么的
 *   darr 是 FRR 2023 年引入的动态数组（dynamic array）。它是一个可自动增长的
 *   连续内存数组，指针直接指向首元素，元素类型由宏 sizeof((A)[0]) 推断，因此
 *   可以像普通数组一样用 A[i] 访问，无需强转。绝大多数操作靠宏 + 编译器内联
 *   展开实现，本头文件是 darr 的主要 API 面。
 *
 * 二、核心数据结构
 *   每个 darr 在首元素之前隐藏一段元数据 struct darr_metadata
 *   （len 当前长度 / cap 容量 / mtype 内存类型）。数组指针 A 直接指向用户元素，
 *   元数据位于 A 的前一“格”，由 _darr_meta(A) 取回。空指针(NULL)是合法的空数组，
 *   len/cap 均按 0 处理。
 *
 * 三、与其它模块的关系
 *   依赖 memory.h 的 MTYPE 内存统计；darr.c 实现少量非宏的函数（resize /
 *   insert_n / search / in_vsprintf 等）。相比老旧的 vector，darr 类型安全、
 *   连续存储、迭代更自然，是新代码的首选容器之一。
 *
 * 四、阅读顺序
 *   先读本导读与 darr_metadata，再读基础宏 darr_cap / darr_len / darr_avail /
 *   darr_maxi / darr_lasti，然后 ensure 系列、append / push / pop、insert /
 *   remove、foreach 迭代宏，最后字符串工具宏。darr.c 里读 _msb /
 *   darr_next_count / _darr__resize / _darr__insert_n / search 系列。
 *
 * 易踩的坑：任何可能触发扩容的调用都可能移动数组，旧指针随之失效；字符串场景
 *   的“长度”指字节数，且需要额外一个 NUL 字节，别与元素个数混淆。
 */

#ifndef _FRR_DARR_H_
#define _FRR_DARR_H_

/*
 * API functions:
 * ==============
 *  - darr_append
 *  - darr_append_mt
 *  - darr_append_n
 *  - darr_append_n_mt
 *  - darr_append_nz
 *  - darr_append_nz_mt
 *  - darr_cap
 *  - darr_ensure_avail
 *  - darr_ensure_avail_mt
 *  - darr_ensure_cap
 *  - darr_ensure_cap_mt
 *  - darr_ensure_i
 *  - darr_ensure_i_mt
 *  - darr_free
 *  - darr_free_free
 *  - darr_free_func
 *  - darr_insert
 *  - darr_insert_mt
 *  - darr_insertz
 *  - darr_insertz_mt
 *  - darr_insert_n
 *  - darr_insert_n_mt
 *  - darr_insert_nz
 *  - darr_insert_nz_mt
 *  - darr_last
 *  - darr_lasti
 *  - darr_len
 *  - darr_maxi
 *  - darr_pop
 *  - darr_push
 *  - darr_pushz
 *  - darr_remove
 *  - darr_remove_n
 *  - darr_reset
 *  - darr_setlen
 *
 * Iteration
 * ---------
 *  - darr_foreach_i
 *  - darr_foreach_p
 *
 * String Utilities
 * ----------------
 *  - darr_in_strcat_tail
 *  - darr_in_strcatf, darr_in_vstrcatf
 *  - darr_in_strdup
 *  - darr_in_strdup_cap
 *  - darr_in_sprintf, darr_in_vsprintf
 *  - darr_set_strlen
 *  - darr_strdup
 *  - darr_strdup_cap
 *  - darr_strlen
 *  - darr_strlen_fixup
 *  - darr_strnul
 *  - darr_str_search
 *  - darr_str_search_ceil
 *  - darr_str_search_floor
 *  - darr_sprintf, darr_vsprintf
 */
/*
 * A few assured items
 *
 * - DAs will never have capacity 0 unless they are NULL pointers.
 */

/*
 * NOTE: valgrind by default enables a "length64" heuristic (among others) which
 * identifies "interior-pointer" 8 bytes forward of a "start-pointer" as a
 * "start-pointer". This should cause what normally would be "possibly-lost"
 * errors to instead be definite for dynamic arrays. This is b/c the header is 8 bytes
 */

#include <zebra.h>
#include <limits.h>
#include "memory.h"

DECLARE_MTYPE(DARR);
DECLARE_MTYPE(DARR_STR);

/*
 * darr_metadata —— 藏在数组首元素之前的元数据头（8 字节对齐）。
 *   len   —— 当前元素个数（字符串场景是字节数 + 1，含 NUL）。
 *   cap   —— 已分配可容纳的元素个数（含正在使用的）。
 *   mtype —— 分配该块内存所用的内存类型，供统计与释放时使用。
 * 约定：数组指针 A 指向首个元素，元数据位于 A 的前一“格”，用 _darr_meta(A) 取回。
 */
struct darr_metadata {
	uint32_t len;
	uint32_t cap;
	struct memtype *mtype;
};

/*
 * 以下三个是 darr 的非宏底层函数，实现在 darr.c：
 *   _darr__insert_n  在 at 处插入 count 个元素（可选清零），必要时扩容，返回新指针。
 *   _darr__in_sprintf / _darr__in_vsprintf  往 darr 字符串里按格式追加/写入内容。
 *   _darr__resize    把数组容量调整为至少 count 个元素，返回（可能已移动的）新指针。
 * 这些名字带双下划线，属内部接口，业务代码应使用上层宏。
 */
void *_darr__insert_n(void *a, uint at, uint count, size_t esize, bool zero, struct memtype *mt);
char *_darr__in_sprintf(char **sp, bool concat, const char *fmt, ...) PRINTFRR(3, 4);
char *_darr__in_vsprintf(char **sp, bool concat, const char *fmt, va_list ap) PRINTFRR(3, 0);
void *_darr__resize(void *a, uint count, size_t esize, struct memtype *mt);

/*
 * 内部辅助宏：从数组指针 A 推导元数据位置、元素大小与长度，并封装扩容调用。
 *   _darr_esize(A)  单个元素字节数；
 *   _darr_len(A)    直接取元数据里的 len 左值；
 *   _darr_meta(A)   由元素指针回退一格得到元数据指针；
 *   _darr_resize_mt / _darr_resize  调用 _darr__resize 并把返回指针写回 (A)。
 * 这些带下划线前缀的宏仅供本头文件内部使用。
 */
#define _darr_esize(A) sizeof((A)[0])
#define _darr_len(A)   _darr_meta(A)->len
#define _darr_meta(A)  (((struct darr_metadata *)(A)) - 1)
#define _darr_resize_mt(A, C, MT) ({ (A) = _darr__resize(A, C, _darr_esize(A), MT); })
#define _darr_resize(A, C)	  _darr_resize_mt(A, C, MTYPE_DARR)

/*
 * darr_cap(A) —— 返回当前容量（可容纳的元素个数）。
 * A 为 NULL 时返回 0。内含一段抑制 GCC -Warray-bounds 误报的 pragma，见下方英文注释。
 */
/* Get the current capacity of the array */
/* GCC 14.2 seems to trip a false-positive warning on this:
 * lib/darr.h:110:55: warning: array subscript -1 is outside array bounds of 'char[]' [-Warray-bounds=]
 *  110 | #define darr_cap(A) (((A) == NULL) ? 0 : _darr_meta(A)->cap)
 *      |                                                       ^
 * (...)
 * lib/northbound_oper.c:450:9: note: in expansion of macro 'darr_in_strdup'
 *  450 |         darr_in_strdup(xpath, xpath_in);
 *      |         ^~~~~~~~~~~~~~
 *
 * Hence the warning-suppression pragmas here :( (added 2025-05-23)
 * (and of course _Pragma gets clang-format confused...)
 */
/* clang-format off */
#define darr_cap(A)                                                                                \
	({                                                                                         \
		_Pragma("GCC diagnostic push")                                                     \
		_Pragma("GCC diagnostic ignored \"-Warray-bounds\"")                               \
		(((A) == NULL) ? 0 : _darr_meta(A)->cap);                                          \
		_Pragma("GCC diagnostic pop")                                                      \
	})
/* clang-format on */

/*
 * darr_avail(A) —— 返回当前剩余可追加空间（cap - len）。
 * A 为 NULL 时为 0。插入/追加前常用于判断是否需扩容。
 */
/* Get the current available expansion space */
#define darr_avail(A) (((A) == NULL) ? 0 : (darr_cap(A) - darr_len(A)))

/*
 * darr_maxi(A) —— 在不扩容的前提下，darr_ensure_i 能保证的最大下标（cap - 1）。
 * 返回 int；空数组为 -1。
 */
/* Get the largest possible index one can `darr_ensure_i` w/o resizing */
#define darr_maxi(A) ((int)darr_cap(A) - 1)

/*
 * darr_len(A) / darr_ilen(A) —— 当前元素个数，NULL 视为 0。
 * 前者返回无符号 uint，后者返回带符号 ssize_t（便于与 -1 比较）。
 */
/**
 * darr_len() - Get the current length of the array as a unsigned int.
 * darr_ilen() - Get the current length of the array as an int.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 *
 * Return:
 *      The current length of the array.
 */
#define darr_len(A)  (((A) == NULL) ? 0 : _darr_meta(A)->len)
#define darr_ilen(A) (((A) == NULL) ? 0 : (ssize_t)_darr_meta(A)->len)

/*
 * darr_lasti(A) —— 最后一个元素的下标；空数组返回 -1。
 * 等价于 darr_ilen(A) - 1。常用于 darr_last 之类取尾部元素的场景。
 */
/**
 * darr_lasti() - Get the last element's index.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 *
 * Return:
 *      The current last element index, or -1 for none.
 */
#define darr_lasti(A) (darr_ilen(A) - 1)

/*
 * darr_reset(A) —— 把长度置 0（逻辑清空），不释放内存、不改变 cap。
 * A 为 NULL 时无操作。清空后可复用已有缓冲区。
 */
/**
 * Set the current length of the array `A` to 0.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 */
#define darr_reset(A)                                                          \
	do {                                                                   \
		if ((A))                                                       \
			_darr_len(A) = 0;                                      \
	} while (0)

/*
 * darr_setlen(A, L) —— 直接把长度设为 L（可增可减），不分配内存。
 * 断言：A 非空（除非 L 为 0），且 cap 必须已 >= L。请先用 ensure 系列保证容量。
 */
/**
 * Set the current length of the array `A` to `L`.
 *
 * This function does *not* guarantee the memory is valid to L,
 * use `darr_ensure` or `darr_ensure_cap` for that.
 *
 * Args:
 *	A: The dynamic array, can only be NULL if (L) == 0.
 *      L: The new length of the array.
 */
#define darr_setlen(A, L)                                                      \
	do {                                                                   \
		assert((A) || !(L));                                           \
		if ((A)) {                                                     \
			/* have to cast to avoid compiler warning for "0" */   \
			assert((long long)darr_cap(A) >= (long long)(L));      \
			_darr_len(A) = (L);                                    \
		}                                                              \
	} while (0)

/*
 * darr_set_strlen(S, L) —— 字符串场景：把字符串长度设为 L，并写 NUL 终止符。
 * 结果：darr_len(S)==L+1，darr_strlen(S)==L，S[L]==0。返回指向结尾 NUL 的指针。
 * 不保证已分配 L+1 字节，须先 ensure 容量；S 不可为 NULL。
 */
/**
 * Set the string length of the array `S` to `L`, and NUL
 * terminate the string at L. The dynamic array length will be `L` + 1.
 *
 * Thus after calling:
 *
 *    darr_len(S) == L + 1
 *    darr_strlen(S) == L
 *    S[L] == 0
 *
 * This function does *not* guarantee the `L` + 1 memory is allocated to
 * the array, use `darr_ensure` or `*_cap` functions for that.
 *
 * Args:
 *	S: The dynamic array, cannot be NULL.
 *      L: The new str length of the array, will set
 *
 * Return:
 *      A pointer to the end of S (i.e., pointing to the NUL byte).
 */
#define darr_set_strlen(S, L)                                                  \
	({                                                                     \
		assert((S));                                                   \
		/* have to cast to avoid compiler warning for "0" */           \
		assert((long long)darr_cap(S) >= (long long)(L));              \
		_darr_len(S) = (L) + 1;                                        \
		*darr_last(S) = 0;                                             \
		darr_last(S);                                                  \
	})

/*
 * darr_free(A) —— 释放整个动态数组占用的内存，并把 A 置 NULL。
 * 只释放数组本身，不释放元素内部分配的内容。A 为 NULL 时无操作。
 */
/**
 * Free memory allocated for the dynamic array `A`
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 */

#define darr_free(A)                                                                               \
	do {                                                                                       \
		if ((A)) {                                                                         \
			struct darr_metadata *_d__meta = _darr_meta(A);                            \
			XFREE(_d__meta->mtype, _d__meta);                                          \
			(A) = NULL;                                                                \
		}                                                                                  \
	} while (0)

/*
 * darr_free_free(A) —— 先对每个非空元素当作 darr 释放，再释放数组本身。
 * 适用于“数组里装的又是 darr”的二层结构；普通元素请用 darr_free。
 */
/**
 * Free memory allocated for the dynamic array `A`, calling `darr_free` for
 * each element of the array first.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 */
#define darr_free_free(A)                                                                          \
	do {                                                                                       \
		for (uint _d__i = 0; _d__i < darr_len(A); _d__i++)                                 \
			if ((A)[_d__i]) {                                                          \
				struct darr_metadata *_d__meta = _darr_meta((A)[_d__i]);           \
				XFREE(_d__meta->mtype, _d__meta);                                  \
			}                                                                          \
		darr_free(A);                                                                      \
	} while (0)

/*
 * darr_free_func(A, F) —— 先对每个元素调用函数 F，再释放数组本身。
 * F 用于释放元素内部分配的资源（字符串、子结构等）。
 */
/**
 * Free memory allocated for the dynamic array `A`, calling `F` routine
 * for each element of the array first.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 *	F: The function to call for each element.
 */

#define darr_free_func(A, F)                                                                       \
	do {                                                                                       \
		for (uint _d__i = 0; _d__i < darr_len(A); _d__i++) {                               \
			F((A)[_d__i]);                                                             \
		}                                                                                  \
		darr_free(A);                                                                      \
	} while (0)

/**
 * Make sure that there is room in the dynamic array `A` to add `C` elements.
 *
 * Available space is `darr_cap(a) - darr_len(a)`.
 *
 * The value `A` may be changed as a result of this call in which case any
 * pointers into the previous memory block are no longer valid. The `A` value
 * is guaranteed not to change if there is sufficient capacity in the array.
 *
 * Args:
 *	A: (IN/OUT) the dynamic array, can be NULL.
 *	S: Amount of free space to guarantee.
 *
 * Return:
 *      A pointer to the (possibly moved) array.
 */
/*
 * darr_ensure_avail_mt(A, S, MT) / darr_ensure_avail(A, S) —— 保证还有 S 个空位。
 * S 指“剩余可用槽数”。必要时按 (cap + 缺口) 扩容，返回（可能移动的）A。
 * 副作用：可能重新分配 → A 变化时其旧指针失效。
 */
#define darr_ensure_avail_mt(A, S, MT)                                                             \
	({                                                                                         \
		ssize_t _dea_need = (ssize_t)(S) - (ssize_t)(darr_cap(A) - darr_len(A));           \
		if (_dea_need > 0)                                                                 \
			_darr_resize_mt((A), darr_cap(A) + _dea_need, MT);                         \
		(A);                                                                               \
	})
#define darr_ensure_avail(A, S) darr_ensure_avail_mt(A, S, MTYPE_DARR)

/**
 * Make sure that there is room in the dynamic array `A` for `C` elements.
 *
 * The value `A` may be changed as a result of this call in which case any
 * pointers into the previous memory block are no longer valid. The `A` value
 * is guaranteed not to change if there is sufficient capacity in the array.
 *
 * The exception to the no-change rule is if @C is passed as 0, it will be
 * considered 1 so that an array is always allocated if currently NULL,
 * i.e., @A will never be NULL after a call to darr_ensure_cap_mt()
 *
 * Args:
 *	A: (IN/OUT) the dynamic array, can be NULL.
 *	C: Total capacity to guarantee.
 *
 * Return:
 *      A pointer to the (possibly moved) array.
 */
/*
 * darr_ensure_cap_mt(A, C, MT) / darr_ensure_cap(A, C) —— 保证总容量 >= C。
 * 与 ensure_avail 的区别：C 是“总容量”而非剩余空位。C 传 0 时按 1 处理，
 * 因此调用后 A 一定非 NULL（会自动建立数组）。可能缩/扩并返回新指针。
 */
#define darr_ensure_cap_mt(A, C, MT)                                                               \
	({                                                                                         \
		/* Cast to avoid warning when C == 0 */                                            \
		uint _dec_c = (C) > 0 ? (C) : 1;                                                   \
		if ((size_t)darr_cap(A) < _dec_c)                                                  \
			_darr_resize_mt((A), _dec_c, MT);                                          \
		(A);                                                                               \
	})
#define darr_ensure_cap(A, C) darr_ensure_cap_mt(A, C, MTYPE_DARR)

/**
 * Return a pointer to the (I)th element of array `A`, making sure there is
 * room for the element.
 *
 * If the array length is less than `I + 1` then the length is set to `I + 1`.
 *
 * The value `A` may be changed as a result of this call in which case any
 * pointers into the previous memory block are no longer valid. The `A` value
 * is guaranteed not to change if there is sufficient capacity in the array.
 *
 * Args:
 *
 *	A: (IN/OUT) the dynamic array, can be NULL.
 *	I: the index to guarantee memory exists for
 *
 * Return:
 *      A pointer to the (I)th element in `A`
 */
/*
 * darr_ensure_i_mt(A, I, MT) / darr_ensure_i(A, I) —— 返回下标 I 处的元素指针。
 * 保证该下标可写；若 I+1 超过当前长度，则清零补足并把长度设为 I+1。
 * 断言 I 在 [0, INT_MAX]。可能扩容并移动 A，旧指针失效。
 */
#define darr_ensure_i_mt(A, I, MT)                                                                \
	({                                                                                        \
		assert((int)(I) >= 0 && (uint)(I) <= INT_MAX);                                    \
		int _d__i = (int)(I);                                                             \
		if (_d__i > darr_maxi(A))                                                         \
			_darr_resize_mt((A), _d__i + 1, MT);                                      \
		assert((A) != NULL);                                                              \
		if ((uint)_d__i + 1 > _darr_len(A)) {                                             \
			memset(&(A)[darr_len(A)], 0, (_d__i + 1 - darr_len(A)) * _darr_esize(A)); \
			_darr_len(A) = _d__i + 1;                                                 \
		}                                                                                 \
		&(A)[_d__i];                                                                      \
	})
#define darr_ensure_i(A, I) darr_ensure_i_mt(A, I, MTYPE_DARR)

/*
 * _darr_insert_n(A, I, N, Z, MT) —— 内部插入助手：在 I 处插入 N 个元素。
 * Z 为真时把新元素清零。返回指向首个被插入元素的指针。
 * 实现在 darr.c 的 _darr__insert_n；上层 darr_insert* 宏都转调它。
 */
#define _darr_insert_n(A, I, N, Z, MT)                                                             \
	({                                                                                         \
		uint _ins_i = (I);                                                                 \
		uint _ins_n = (N);                                                                 \
		(A) = _darr__insert_n(A, _ins_i, _ins_n, _darr_esize(A), Z, MT);                   \
		&(A)[_ins_i];                                                                      \
	})
/**
 * Insert N uninitialized elements in the array at index `I`.
 *
 * Previous elements from `I` are shifted right by `N`. Array length is
 * increased by `N`.
 *
 * The value `A` may be changed as a result of this call in which case any
 * pointers into the previous memory block are no longer valid. The `A` value
 * is guaranteed not to change if there is sufficient capacity in the array.
 *
 * The `z` variant zeros new elements.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 *
 * Return:
 *      A pointer to the first inserted element in the array.
 */
/*
 * darr_insert_n / darr_insert_nz（及其 _mt 变体）—— 在 I 处插入 N 个元素，
 * 原有 I 及之后的元素右移 N 个位置，长度加 N。z 变体把新元素清零。
 * 返回指向首个新元素的指针；可能扩容。
 */
#define darr_insert_n(A, I, N)	       _darr_insert_n(A, I, N, false, MTYPE_DARR)
#define darr_insert_n_mt(A, I, N, MT)  _darr_insert_n(A, I, N, false, MT)
#define darr_insert_nz(A, I, N)	       _darr_insert_n(A, I, N, true, MTYPE_DARR)
#define darr_insert_nz_mt(A, I, N, MT) _darr_insert_n(A, I, N, true, MT)

/**
 * Insert an uninitialized element in the array at index `I`.
 *
 * Previous elements from `I` are shifted right by 1. Array length is
 * increased by 1.
 *
 * The value `A` may be changed as a result of this call in which case any
 * pointers into the previous memory block are no longer valid. The `A` value
 * is guaranteed not to change if there is sufficient capacity in the array.
 *
 * The `z` variant zeros the new element.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 *
 * Return:
 *      A pointer to the element in the array.
 */
/*
 * darr_insert / darr_insertz（及其 _mt 变体）—— 在 I 处插入 1 个元素，长度加 1；
 * 原有 I 及之后的元素右移一格。z 变体清零新元素。返回指向新元素的指针。
 */
#define darr_insert(A, I)	  _darr_insert_n(A, I, 1, false, MTYPE_DARR)
#define darr_insert_mt(A, I, MT)  _darr_insert_n(A, I, 1, false, MT)
#define darr_insertz(A, I)	  _darr_insert_n(A, I, 1, true, MTYPE_DARR)
#define darr_insertz_mt(A, I, MT) _darr_insert_n(A, I, 1, true, MT)

/**
 * Remove `N` elements from the array starting at index `I`.
 *
 * Elements from `I` + `N` are shifted left by `N`. Array length is reduced by
 * `N`.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 */
/*
 * darr_remove_n(A, I, N) —— 从下标 I 起删除 N 个元素，其后的元素左移 N 位，
 * 长度减 N。若 I+N 超出末尾，则直接把长度截断为 I。A 为 NULL 或空数组时无操作。
 */
#define darr_remove_n(A, I, N)                                                                     \
	do {                                                                                       \
		uint _d__i = (I);                                                                  \
		uint _d__n = (N);                                                                  \
		uint _d__len = darr_len(A);                                                        \
		if (!_d__len)                                                                      \
			break;                                                                     \
		else if (_d__i + _d__n < _d__len) {                                                \
			memmove(&(A)[_d__i], &(A)[_d__i + _d__n],                                  \
				_darr_esize(A) * (_d__len - (_d__i + _d__n)));                     \
			_darr_len(A) = _d__len - _d__n;                                            \
		} else                                                                             \
			_darr_len(A) = _d__i;                                                      \
	} while (0)

/**
 * Remove the `I`th element from the array.
 *
 * Previous elements from `I` + 1 are shifted left by 1, Array length is reduced
 * by 1.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 */
/*
 * darr_remove(A, I) —— 删除下标 I 的单个元素，其后元素左移一位，长度减 1。
 * 等价于 darr_remove_n(A, I, 1)。
 */
#define darr_remove(A, I) darr_remove_n(A, I, 1)


/*
 * _darr_append_n(A, N, Z, MT) —— 内部追加助手：在末尾扩展 N 个元素并返回首地址。
 * Z 为真时把新元素清零。先 ensure 容量再改长度，无插入的搬移开销。
 */
#define _darr_append_n(A, N, Z, MT)                                                                \
	({                                                                                         \
		uint _da_len = darr_len(A);                                                        \
		darr_ensure_cap_mt(A, _da_len + (N), MT);                                          \
		_darr_len(A) = _da_len + (N);                                                      \
		if (Z)                                                                             \
			memset(&(A)[_da_len], 0, (N)*_darr_esize(A));                              \
		&(A)[_da_len];                                                                     \
	})
/**
 * Extending the array's length by N.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 *
 * The `z` variant zeros new elements.
 *
 * Return:
 *	A pointer to the first of the added elements at the end of the array.
 */
/*
 * darr_append_n / darr_append_nz（及其 _mt 变体）—— 在末尾扩展 N 个元素，长度加 N。
 * z 变体清零新元素。返回指向首个新增元素的指针。可能扩容。
 */
#define darr_append_n(A, N)	    _darr_append_n(A, N, false, MTYPE_DARR)
#define darr_append_n_mt(A, N, MT)  _darr_append_n(A, N, false, MT)
#define darr_append_nz(A, N)	    _darr_append_n(A, N, true, MTYPE_DARR)
#define darr_append_nz_mt(A, N, MT) _darr_append_n(A, N, true, MT)

/**
 * Extending the array's length by 1.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 *
 * The `z` variant zeros the new element.
 *
 * Return:
 *	A pointer to the new element at the end of the array.
 */
/*
 * darr_append / darr_appendz（及其 _mt 变体）—— 在末尾扩展 1 个元素，返回其指针。
 * z 变体清零该元素。注意：darr_append 返回的是指针（为了赋值），空数组时会分配。
 */
#define darr_append(A)	       _darr_append_n(A, 1, false, MTYPE_DARR)
#define darr_append_mt(A, MT)  _darr_append_n(A, 1, false, MT)
#define darr_appendz(A)	       _darr_append_n(A, 1, true, MTYPE_DARR)
#define darr_appendz_mt(A, MT) _darr_append_n(A, 1, true, MT)

/**
 * Append an element `E` onto the array `A`, extending it's length by 1.
 *
 * The `z` variant zeros the new element.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 *
 * Return:
 *	A pointer to the element in the array.
 */
/*
 * darr_push / darr_push_mt(A, E) —— 追加一个元素并把值 E 写入，长度加 1。
 * darr_pushz / darr_pushz_mt(A) —— 追加并清零，返回新元素指针（不赋值）。
 * darr_push 返回赋值结果（元素本身），不是指针。
 */
#define darr_push(A, E)	       (*darr_append(A) = (E))
#define darr_push_mt(A, E, MT) (*darr_append_mt(A, MT) = (E))
#define darr_pushz(A)	       (darr_appendz(A))
#define darr_pushz_mt(A, MT)   (darr_appendz_mt(A, MT))

/*
 * darr_push_uniq_mt(A, E, MT) —— 仅当 E 不在数组中时才追加，常用指针去重。
 * 用线性查找，O(n)；命中即结束（不追加）。
 */
#define darr_push_uniq_mt(A, E, MT)                                                                \
	do {                                                                                       \
		uint _dpu_i;                                                                       \
                                                                                                   \
		darr_foreach_i (A, _dpu_i) {                                                       \
			if ((A)[_dpu_i] == (E))                                                    \
				break;                                                             \
		}                                                                                  \
		if (_dpu_i == darr_len(A))                                                         \
			(*darr_append_mt(A, MT) = (E));                                            \
	} while (0)

/**
 * darr_push_uniq() - Append element if not present.
 * @A: The dynamic array, can be NULL.
 * @E: The element to push onto the array if missing.
 *
 * Append an element `E` onto the array `A`, extending it's length by 1. This is
 * particularly useful for arrays of pointers.
 */
/*
 * darr_push_uniq(A, E) —— 去重追加的默认内存类型版本（MTYPE_DARR）。
 */
#define darr_push_uniq(A, E) darr_push_uniq_mt(A, E, MTYPE_DARR)


/**
 * Pop the last `N` elements from the array decrementing the length by `N`.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 */
/*
 * darr_pop_n(A, N) —— 从末尾弹出 N 个元素（长度减 N），不释放内存。
 * 若 N 大于等于当前长度，则整体置空（darr_reset）。A 为 NULL 时注意分支。
 */
#define darr_pop_n(A, N)                                                       \
	do {                                                                   \
		if ((A) && (N) >= _darr_len(A))                                \
			darr_reset(A);                                         \
		else                                                           \
			_darr_len(A) -= (N);                                   \
	} while (0)


/**
 * Pop the last element from the array decrementing the length by 1.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 *
 * Return:
 *      The element just popped.
 */
/*
 * darr_pop(A) —— 弹出并返回最后一个元素的值（拷贝）。
 * 断言数组非空（长度 > 0）；先把长度减 1，再取原末尾元素（不触发扩容/搬移）。
 */
#define darr_pop(A)                                                                                \
	({                                                                                         \
		uint _d__poplen = _darr_len(A);                                                    \
		assert(_d__poplen);                                                                \
		darr_remove(A, _d__poplen - 1);                                                    \
		/* count on fact that we don't resize */                                           \
		(A)[_d__poplen - 1];                                                               \
	})

/**
 * Return the address at the end of the array -- useful for iterating
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 *
 * Return:
 *	The address of the end of the array (past the last elment) or NULL
 *	if `A` is NULL.
 */
/*
 * darr_end(A) —— 返回数组末尾之后的地址（尾后指针），便于与 for 循环配合。
 * A 为 NULL 时结果为 NULL + 0，即 NULL。
 */
#define darr_end(A) ((A) + darr_len(A))

/**
 * darr_last() - Get a pointer to the last element of the array.
 * darr_strnul() - Get a pointer to the NUL byte of the darr string or NULL.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 *
 * Return:
 *      A pointer to the last element of the array or NULL if the array is
 *      empty.
 */
/*
 * darr_last(A) / darr_strnul(S) —— 返回最后一个元素的指针；空数组返回 NULL。
 * 对字符串而言，darr_strnul 即指向结尾 NUL 字节的位置。
 */
#define darr_last(A)                                                                               \
	({                                                                                         \
		uint _d__len = darr_len(A);                                                        \
		((_d__len > 0) ? &(A)[_d__len - 1] : NULL);                                        \
	})
#define darr_strnul(S) darr_last(S)

/**
 * darr_in_sprintf() - sprintf into D.
 *
 * Args:
 *      D: The destination darr, D's value may be NULL.
 *      F: The format string
 *      ...: variable arguments for format string.
 *
 * Return:
 *	The dynamic_array D with the new string content.
 */
/*
 * darr_in_sprintf(D, F, ...) —— 把格式化结果写入 darr 字符串 D（覆盖原内容）。
 * D 可为 NULL（会自动创建）。实现转调 darr.c 的 _darr__in_sprintf。
 */
#define darr_in_sprintf(D, F, ...) _darr__in_sprintf(&(D), 0, F, __VA_ARGS__)


/**
 * darr_in_strcat() - concat a string into a darr string.
 *
 * Args:
 *      D: The destination darr, D's value may be NULL.
 *      S: The string to concat onto D.
 *
 * Return:
 *	The dynamic_array D with the new string content.
 */
/*
 * darr_in_strcat(D, S) —— 把字符串 S 追加到 darr 字符串 D 末尾。
 * D 可为 NULL（自动创建）。保证分配 len + slen + 1 字节；返回 D 自身。
 */
#define darr_in_strcat(D, S)                                                                       \
	({                                                                                         \
		const char *_d__S = (S);                                                           \
		uint _d__dlen = darr_strlen(D);                                                    \
		uint _d__slen = strlen(_d__S);                                                     \
		darr_ensure_cap_mt(D, _d__dlen + _d__slen + 1, MTYPE_DARR_STR);                    \
		if (darr_len(D) == 0)                                                              \
			*darr_append(D) = 0;                                                       \
		memcpy(&(D)[darr_strlen(D)], _d__S, _d__slen + 1);                                 \
		_darr_len(D) += _d__slen;                                                          \
		(D);                                                                               \
	})

/**
 * darr_in_strcatf() - concat a formatted string into a darr string.
 *
 * Args:
 *      D: The destination darr, D's value may be NULL.
 *      F: The format string to concat onto D after adding arguments.
 *    ...: The arguments for the format string.
 * Return:
 *	The dynamic_array D with the new string content.
 */
/*
 * darr_in_strcatf(D, F, ...) —— 把格式化字符串“追加”到 darr 字符串 D（不覆盖）。
 * 与 darr_in_sprintf 的区别是 concat 参数为 true。
 */
#define darr_in_strcatf(D, F, ...) _darr__in_sprintf(&(D), true, (F), __VA_ARGS__)

/**
 * darr_in_strcat_tail() - copies end of one darr str to another.
 *
 * This is a rather specialized function, it takes 2 darr's, a destination and a
 * source. If the source is not longer than the destination nothing is done.
 * Otherwise the characters in the source that lie beyond the length of the dest
 * are added to the dest. No checking is done to make sure the common prefix
 * matches. For example:
 *
 *     D: "/foo"
 *     S: "/foo/bar"
 *  -> D: "/foo/bar"
 *
 *     perhaps surprising results:
 *     D: "/foo"
 *     S: "/zoo/bar"
 *  -> D: "/foo/bar"
 *
 * Args:
 *      D: The destination darr, D's value may be NULL.
 *      S: The string to copy the tail from.
 *
 * Return:
 *	The dynamic_array D with the extended string content.
 */
/*
 * darr_in_strcat_tail(D, S) —— 把 S 中“超出 D 当前长度”的那一段尾巴追加到 D。
 * 不校验公共前缀是否一致（见上方英文示例，可能有意外结果）。S 不长于 D 时无操作。
 */
#define darr_in_strcat_tail(D, S)                                                                  \
	({                                                                                         \
		const char *_d__S = (S);                                                           \
		int _d__dsize, _d__ssize, _d__extra;                                               \
                                                                                                   \
		if (darr_len(D) == 0)                                                              \
			*darr_append(D) = 0;                                                       \
		_d__dsize = darr_ilen(D);                                                          \
		_d__ssize = darr_ilen(_d__S);                                                      \
		_d__extra = _d__ssize - _d__dsize;                                                 \
		if (_d__extra > 0) {                                                               \
			darr_ensure_cap_mt(D, (uint)_d__ssize, MTYPE_DARR_STR);                    \
			memcpy(darr_last(D), _d__S + _d__dsize - 1, _d__extra + 1);                \
			_darr_len(D) += _d__extra;                                                 \
		}                                                                                  \
		(D);                                                                               \
	})

/**
 * darr_in_strdup_cap() - duplicate the string into a darr reserving capacity.
 * darr_in_strdup() - duplicate the string into a darr.
 *
 * Args:
 *      D: The destination darr, D's value may be NULL.
 *      S: The string to duplicate.
 *      C: The capacity to reserve.
 *
 * Return:
 *	The dynamic_array D with the duplicated string.
 */
/*
 * darr_in_strdup_cap(D, S, C) —— 把字符串 S 复制进 D（覆盖原内容），并至少预留 C 容量。
 * 先 reset 再按 max(C, strlen(S)+1) 申请，长度置为复制后的字节数。
 */
#define darr_in_strdup_cap(D, S, C)                                                                \
	({                                                                                         \
		const char *_d__S = (S);                                                           \
		size_t _d__size = strlen(_d__S) + 1;                                               \
		darr_reset(D);                                                                     \
		darr_ensure_cap_mt(D, ((size_t)(C) > _d__size) ? (size_t)(C) : _d__size,           \
				   MTYPE_DARR_STR);                                                \
		strlcpy(D, _d__S, darr_cap(D));                                                    \
		darr_setlen((D), (size_t)_d__size);                                                \
		(D);                                                                               \
	})
/*
 * darr_in_strdup(D, S) —— 复制字符串 S 进 D，预留容量参数取 1（即只要够放即可）。
 */
#define darr_in_strdup(D, S) darr_in_strdup_cap(D, S, 1)

/**
 * darr_in_vsprintf() - vsprintf into D.
 *
 * Args:
 *      D: The destination darr, D's value may be NULL.
 *      F: The format string
 *      A: Varargs
 *
 * Return:
 *	The dynamic_array D with the new string content.
 */
/*
 * darr_in_vsprintf(D, F, A) —— 用 va_list A 把格式串 F 写入 D（覆盖原内容）。
 * 供已有 va_list 的包装函数使用；普通调用用 darr_in_sprintf。
 */
#define darr_in_vsprintf(D, F, A) _darr__in_vsprintf(&(D), 0, F, A)

/**
 * darr_in_vstrcatf() - concat a formatted string into a darr string.
 *
 * Args:
 *      D: The destination darr, D's value may be NULL.
 *      F: The format string to concat onto D after adding arguments.
 *      A: Varargs
 *
 * Return:
 *	The dynamic_array D with the new string content.
 */
/*
 * darr_in_vstrcatf(D, F, A) —— 用 va_list A 把格式串 F“追加”到 D（不覆盖）。
 * 注意：此宏调用的是 __darr__in_vsprintf（双下划线开头），疑为原代码笔误，
 *       可能无法链接；使用前请确认当前版本是否已修正。
 */
#define darr_in_vstrcatf(D, F, A) __darr__in_vsprintf(&(D), true, (F), (A))

/**
 * darr_sprintf() - sprintf into a new dynamic array.
 *
 * Args:
 *      F: The format string
 *      ...: variable arguments for format string.
 *
 * Return:
 *	A char * dynamic_array with the new string content.
 */
/*
 * darr_sprintf(F, ...) —— 新建一个 darr 字符串并写入格式化结果，返回它。
 * 内部先声明 NULL 再 _darr__in_sprintf，因此首次会自动分配。
 */
#define darr_sprintf(F, ...)                                                                       \
	({                                                                                         \
		char *_d__d = NULL;                                                                \
		_darr__in_sprintf(&_d__d, false, F, __VA_ARGS__);                                  \
		_d__d;                                                                             \
	})

/**
 * darr_strdup_cap() - duplicate the string reserving capacity.
 * darr_strdup() - duplicate the string into a dynamic array.
 *
 * Args:
 *      S: The string to duplicate.
 *      C: The capacity to reserve.
 *
 * Return:
 *	The dynamic_array with the duplicated string.
 */
/*
 * darr_strdup_cap(S, C) —— 新建 darr 并复制字符串 S，至少预留 C 容量。返回新 darr。
 * darr_strdup(S) 是其 C 取 0（只保证够放）的便捷版本。
 */
#define darr_strdup_cap(S, C)                                                                      \
	({                                                                                         \
		const char *_d__S = (S);                                                           \
		size_t _d__size = strlen(_d__S) + 1;                                               \
		char *_d__s = NULL;                                                                \
		/* Cast to ssize_t to avoid warning when C == 0 */                                 \
		darr_ensure_cap_mt(_d__s,                                                          \
				   ((ssize_t)(C) > (ssize_t)_d__size) ? (size_t)(C) : _d__size,    \
				   MTYPE_DARR_STR);                                                \
		strlcpy(_d__s, _d__S, darr_cap(_d__s));                                            \
		darr_setlen(_d__s, (size_t)_d__size);                                              \
		_d__s;                                                                             \
	})
/*
 * darr_strdup(S) —— 新建 darr 并复制字符串 S（容量只保证够放），返回新 darr。
 */
#define darr_strdup(S) darr_strdup_cap(S, 0)

/**
 * darr_strlen() - get the length of the NUL terminated string in a darr.
 *
 * Args:
 *      S: The string to measure, value may be NULL.
 *
 * Return:
 *	The length of the NUL terminated string in @S
 */
/*
 * darr_strlen(S) —— 返回 darr 字符串的有效字节数（不含结尾 NUL）。
 * 实现：取 darr_len 减 1（len 含 NUL）。空/S 为 NULL 时返回 0；
 * 断言 S 的 len-1 处确实是 NUL 字节。
 */
#define darr_strlen(S)                                                                             \
	({                                                                                         \
		uint _d__size = darr_len(S);                                                       \
		if (_d__size)                                                                      \
			_d__size -= 1;                                                             \
		assert(!(S) || ((char *)(S))[_d__size] == 0);                                      \
		_d__size;                                                                          \
	})

/**
 * Fixup darr_len (and thus darr_strlen) for `S` based on its strlen(S)
 * (i.e., scan for NUL byte). The dynamic array length will be set to strlen(S) + 1.
 *
 * Args:
 *	S: The dynamic array with a NUL terminated string, cannot be NULL.
 *
 * Return:
 *      The calculated strlen() value.
 */
/*
 * darr_strlen_fixup(S) —— 用 strlen(S) 扫描 NUL 位置来校正 darr_len（设为字节数+1）。
 * 适用于外部直接改写过内容的场景。S 不可为 NULL；返回校正后的 strlen 值。
 */
#define darr_strlen_fixup(S)                                                                       \
	({                                                                                         \
		_darr_len(S) = strlen(S) + 1;                                                      \
		darr_strlen(S);                                                                    \
	})

/**
 * darr_vsprintf() - vsprintf into a new dynamic array.
 *
 * Args:
 *      F: The format string
 *      A: Varargs
 *
 * Return:
 *	The dynamic_array D with the new string content.
 */
/*
 * darr_vsprintf(F, A) —— 新建 darr 字符串并用 va_list A 格式化写入，返回它。
 * 与 darr_sprintf 类似，但接收已展开的 va_list。
 */
#define darr_vsprintf(F, A)                                                    \
	({                                                                     \
		char *d = NULL;                                                \
		darr_in_vsprintf(d, F, A);                                     \
		d;                                                             \
	})

/*
 * darr_search_{floor,ceil}() functions - search for key in sorted arrays
 */
/*
 * darr_search_cmpf —— 有序数组查找的比较函数类型。
 *   ep  指向数组中的某元素，key 为待查关键字。
 *   返回 <0 / 0 / >0 表示 ep 小于 / 等于 / 大于 key（与 strcmp/qsort 一致）。
 * darr_strings_cmp —— 面向字符串指针数组的现成比较函数（键为 const char *)。
 * _darr_search / _ceil / _floor —— 底层二分查找，实现在 darr.c：
 *   精确查找返回下标或 -1；floor/ceil 返回最接近的下界/上界下标，并可回传是否相等。
 */
typedef int (*darr_search_cmpf)(const void *ep, const void *key);
extern int darr_strings_cmp(const char **a, const char *key);
extern int _darr_search(const void *a, size_t esize, const void *key, darr_search_cmpf cmpf);
extern uint _darr_search_ceil(const void *a, size_t esize, const void *key, bool *equal,
			      darr_search_cmpf cmpf);
extern int _darr_search_floor(const void *a, size_t esize, const void *key, bool *equal,
			      darr_search_cmpf cmpf);

/**
 * darr_str_search() - Find exact key in array of strings.
 *
 * Args:
 *	A: array of string pointers
 *	K: key string
 *
 * Return:
 *	The index of the string which matches the key or -1 for no match.
 */
/*
 * darr_str_search(A, K) —— 在有序字符串指针数组 A 中精确查找 K，返回下标或 -1。
 * 前提：A 已按 strcmp 排序；使用 darr_strings_cmp 比较。
 */
#define darr_str_search(A, K)                                                                      \
	_darr_search((A), _darr_esize(A), (K), (darr_search_cmpf)darr_strings_cmp)

/**
 * darr_str_search_ceil() - Find least elm greater than or equal to the key
 *
 * Args:
 *	A: array of string pointers
 *	K: key string
 *	E: Ptr to bool, set to true if element matching key is found
 *
 * Return:
 *	The index of the least element that is greater than or equal to the @K
 *	string. @E is set to true if equal otherwise false. The return value can
 *	be passed directly to darr_insert().
 */
/*
 * darr_str_search_ceil(A, K, E) —— 有序字符串数组中找“大于等于 K”的最小元素下标。
 * E 为出参 bool：命中相等元素时为 true，否则 false。返回值可直接交给 darr_insert。
 */
#define darr_str_search_ceil(A, K, E)                                                              \
	_darr_search_ceil((A), _darr_esize(A), (K), (E), (darr_search_cmpf)darr_strings_cmp)

/**
 * darr_str_search_floor() - Find greatest elm less than or equal to the key
 *
 * Args:
 *	A: array of string pointers
 *	K: key string
 *	E: Ptr to bool, set to true if element matching key is found
 *
 * Return:
 *	The index of the greatest element that is less than or equal to the @K
 *	string. @E is set to true if equal otherwise false. If used with
 *	darr_insert() then the index should be passed +1 because darr_insert()
 *	inserts *before* the given index. If no element is less than or equal to
 *	the key then -1 is returned.
 */
/*
 * darr_str_search_floor(A, K, E) —— 有序字符串数组中找“小于等于 K”的最大元素下标。
 * E 为出参 bool：命中相等时为 true，否则 false。无 <=K 的元素时返回 -1。
 * 若配合 darr_insert 使用，传入下标需 +1（因为 insert 插在下标之前）。
 */
#define darr_str_search_floor(A, K, E)                                                             \
	_darr_search_floor((A), _darr_esize(A), (K), (E), (darr_search_cmpf)darr_strings_cmp)

/**
 * Iterate over array `A` using a pointer to each element in `P`.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 *	P: A variable with the same type as A used as the iterator.
 */
/*
 * darr_foreach_p(A, P) —— 用同类型指针 P 遍历数组 A 的每个元素（P 指向元素本体）。
 * A 可为 NULL（此时循环不执行）。适合需要元素地址或连续访问的场景。
 */
#define darr_foreach_p(A, P) for ((P) = (A); (P) < darr_end(A); (P)++)

/**
 * Iterate over array `A`s indices.
 *
 * Args:
 *	A: The dynamic array, can be NULL.
 *	I: A uint variable to store the current element index in.
 */
/*
 * darr_foreach_i(A, I) —— 用 uint 变量 I 遍历数组 A 的下标（0 .. len-1）。
 * A 可为 NULL（此时循环不执行）。遍历中若增删元素需小心失效。
 */
#define darr_foreach_i(A, I) for ((I) = 0; (I) < darr_len(A); (I)++)

#endif /* _FRR_DARR_H_ */
