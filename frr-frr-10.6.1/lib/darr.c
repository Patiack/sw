// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * June 23 2023, Christian Hopps <chopps@labn.net>
 *
 * Copyright (c) 2023, LabN Consulting, L.L.C.
 *
 */
/*
 * 【文件导读】darr.c —— darr 动态数组中不便于用宏表达的一批函数实现
 *
 * 一、这个文件是干什么的
 *   darr 的绝大多数 API 是在 darr.h 里以宏形式展开的，本文件只放“必须写函数体”
 *   的少量实现：容量增长策略、内存重分配、批量插入、有序数组二分查找，以及格式化
 *   字符串写入 darr 的辅助函数。
 *
 * 二、核心数据结构
 *   仍是 darr.h 里的 struct darr_metadata（len / cap / mtype），位于元素区之前。
 *   本文件所有函数都通过 _darr_meta 取回它来读写 len / cap。
 *
 * 三、与其它模块的关系
 *   依赖 memory.h（XREALLOC / XCALLOC / MTYPE）、printfrr.h（vsnprintfrr 格式化）。
 *   上层 darr.h 的宏转调到本文件：_darr__resize、_darr__insert_n、
 *   _darr__in_sprintf / _darr__in_vsprintf。
 *
 * 四、阅读顺序
 *   _msb / darr_next_count（容量取整为 2 的幂）-> darr_size -> _darr__resize
 *   -> _darr__insert_n -> 字符串 _darr__in_vsprintf / _darr__in_sprintf
 *   -> _darr_search_floor / _darr_search / _darr_search_ceil -> darr_strings_cmp。
 *
 * 易踩的坑：扩容可能移动内存并会清零“新增长出来的”尾部；capacity 恒为非 0
 *   （除非指针为 NULL）。字符串长度语义是“字节数 + 1”，含结尾 NUL。
 */

#include <zebra.h>
#include "darr.h"
#include "memory.h"
#include "printfrr.h"

/*
 * 两个内存类型标签：MTYPE_DARR 用于普通动态数组，MTYPE_DARR_STR 用于
 * darr 字符串（char 数组）。供内存统计与泄漏排查使用。
 */
DEFINE_MTYPE(LIB, DARR, "Dynamic Array");
DEFINE_MTYPE(LIB, DARR_STR, "Dynamic Array String");

/*
 * _msb —— 返回 count 中最高有效位（most significant bit）的位号，从 0 计。
 * 例：count=1 返回 0，count=8 返回 3，count=9 返回 3（最高位仍是 1000b）。
 * count 为 0 时返回 0（循环不执行，初值 msb=0）。仅内部使用。
 */
static uint _msb(uint count)
{
	uint bit = 0;
	int msb = 0;

	while (count) {
		if (count & 1)
			msb = bit;
		count >>= 1;
		bit += 1;
	}
	return msb;
}

/*
 * darr_next_count —— 根据请求的元素数 count 计算“下一次分配”的容量。
 * 策略：把 count 向上取整为 2 的幂（避免频繁 realloc）；元素较大（>8 字节）
 *   且只需 1 个时，容量就取 1 当作单个指针处理。小元素（<8 字节）最少给 8 个。
 * 参数 count：用户请求的元素数；esize：单个元素字节数。
 * 返回：分配容量（>= count）。仅内部使用。
 */
static uint darr_next_count(uint count, size_t esize)
{
	uint ncount;

	if (esize > sizeof(long long) && count == 1)
		/* treat like a pointer */
		ncount = 1;
	else {
		uint msb = _msb(count);

		ncount = 1ull << msb;
		/* if the users count wasn't a pow2 make it the next pow2. */
		if (ncount != count) {
			assert(ncount < count);
			ncount <<= 1;
			if (esize < sizeof(long long) && ncount < 8)
				ncount = 8;
		}
	}
	return ncount;
}

/*
 * darr_size —— 计算容纳 count 个元素所需的整块内存字节数。
 * 等于 count 个元素空间再加上隐藏的 darr_metadata 头大小。仅内部使用。
 */
static size_t darr_size(uint count, size_t esize)
{
	return count * esize + sizeof(struct darr_metadata);
}

/*
 * _darr__in_vsprintf —— 把 va_list ap 按格式 fmt 写入 darr 字符串 *sp。
 * 参数：sp 指向 darr 字符串指针（可为 NULL，会自动分配）；concat 为真时追加，
 *       否则先 darr_reset 覆盖重写。
 * 返回：*sp（可能已重新分配，指针值以返回/出参为准）。
 * 副作用：可能扩容并把 sp 内容整体移动；用 vsnprintfrr 先试探长度，写不下再扩容重试。
 */
char *_darr__in_vsprintf(char **sp, bool concat, const char *fmt, va_list ap)
{
	size_t inlen = concat ? darr_strlen(*sp) : 0;
	size_t capcount = strlen(fmt) + MIN(inlen + 64, 128);
	ssize_t len;
	va_list ap_copy;

	darr_ensure_cap(*sp, capcount);

	if (!concat)
		darr_reset(*sp);

	/* code below counts on having a NUL terminated string */
	if (darr_len(*sp) == 0)
		*darr_append(*sp) = 0;
again:
	va_copy(ap_copy, ap);
	len = vsnprintfrr(darr_last(*sp), darr_avail(*sp) + 1, fmt, ap_copy);
	va_end(ap_copy);
	if (len < 0)
		darr_in_strcat(*sp, fmt);
	else if ((size_t)len <= darr_avail(*sp))
		_darr_len(*sp) += len;
	else {
		darr_ensure_cap(*sp, darr_len(*sp) + (size_t)len);
		goto again;
	}
	return *sp;
}

/*
 * _darr__in_sprintf —— _darr__in_vsprintf 的可变参数包装版本。
 * 用 va_start/va_end 处理 ... 后转调 vsprintf 实现。返回 *sp。
 */
char *_darr__in_sprintf(char **sp, bool concat, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)_darr__in_vsprintf(sp, concat, fmt, ap);
	va_end(ap);
	return *sp;
}


/*
 * _darr__resize —— 把 darr 的容量调整为至少容纳 count 个元素（实际按 pow2 取整）。
 * 参数：a 数组指针（可为 NULL）；count 请求元素数；esize 元素大小；mtype 内存类型。
 * 返回：指向（可能已移动的）数组首元素的指针，旧的 a 在移动后失效。
 * 副作用：XREALLOC 或 XCALLOC；若是新分配则记录 mtype；容量增大时把新增区间清零。
 * 注意：新分配时 (void *)(dm + 1) 即越过元数据头之后的首元素地址。
 */
void *_darr__resize(void *a, uint count, size_t esize, struct memtype *mtype)
{
	uint ncount = darr_next_count(count, esize);
	size_t osz = (a == NULL) ? 0 : darr_size(darr_cap(a), esize);
	size_t sz = darr_size(ncount, esize);
	struct darr_metadata *dm;

	if (a) {
		dm = XREALLOC(_darr_meta(a)->mtype, _darr_meta(a), sz);
		if (sz > osz)
			memset((char *)dm + osz, 0, sz - osz);
	} else {
		dm = XCALLOC(mtype, sz);
		dm->mtype = mtype;
	}
	dm->cap = ncount;
	return (void *)(dm + 1);
}


/*
 * _darr__insert_n —— 在 darr 的下标 at 处插入 count 个元素（可选清零）。
 * 参数：a 数组（可为 NULL）；at 插入位置；count 插入个数；esize 元素大小；
 *       zero 为真时把新元素清零；mtype 内存类型。
 * 返回：指向（可能已移动的）数组首元素的指针。
 * 副作用：可能扩容；把 at 及之后的已有元素整体右移 count 个位置；长度改为 nlen。
 * 坑：at >= 当前长度时按“末尾追加”处理（新长度 = at + count）。
 */
void *_darr__insert_n(void *a, uint at, uint count, size_t esize, bool zero, struct memtype *mtype)
{
	struct darr_metadata *dm;
	uint olen, nlen;

	if (!a)
		a = _darr__resize(NULL, at + count, esize, mtype);
	dm = (struct darr_metadata *)a - 1;
	olen = dm->len;

	// at == 1
	// count == 100
	// olen == 2

	/* see if the user is expanding first using `at` */
	if (at >= olen)
		nlen = at + count;
	else
		nlen = olen + count;

	if (nlen > dm->cap) {
		a = _darr__resize(a, nlen, esize, mtype);
		dm = (struct darr_metadata *)a - 1;
	}

#define _a_at(i) ((char *)a + ((i)*esize))
	if (at < olen)
		memmove(_a_at(at + count), _a_at(at), esize * (olen - at));

	dm->len = nlen;

	if (zero) {
		if (at >= olen) {
			at -= olen;
			count += olen;
		}
		memset(_a_at(at), 0, esize * count);
	}

	return a;
#undef _a_at
}

/*
 * _darr_search_floor —— 在有序数组中二分查找 key 的“下界”下标。
 * 参数：a 数组（可为 NULL）；esize 元素大小；key 关键字；equal 出参（是否精确命中）；
 *       cmpf 比较函数，为 NULL 时用 memcmp 按 esize 比较。
 * 返回：若命中，返回该元素下标且 *equal=true；否则返回小于 key 的最大元素下标，
 *       若无更小元素则返回 -1（空数组/NULL 也返回 -1）。
 * 前提：数组已按 cmpf 升序排序。
 */
int _darr_search_floor(const void *a, size_t esize, const void *key, bool *equal,
		       darr_search_cmpf cmpf)
{
	struct darr_metadata *dm;

	if (equal)
		*equal = false;

	if (!a)
		return -1;

	dm = (struct darr_metadata *)a - 1;

	int len = dm->len;
	int low = 0, high = len - 1;
	int floor = -1;

#define _a_at(i) ((void *)((char *)a + ((i)*esize)))
	while (low <= high) {
		int mid = low + (high - low) / 2;
		int cmp;

		if (cmpf)
			cmp = cmpf(_a_at(mid), key);
		else
			cmp = memcmp(_a_at(mid), key, esize);

		if (!cmp) {
			if (equal)
				*equal = true;
			assert(mid >= -1);
			return mid;
		} else if (cmp < 0) {
			floor = mid;
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}

	assert(floor >= -1);
	return floor;
#undef _a_at
}

/*
 * _darr_search —— 在有序数组中精确查找 key，返回下标；未命中返回 -1。
 * 基于 _darr_search_floor 实现：只有 *equal 为真才认为是命中。
 */
int _darr_search(const void *a, size_t esize, const void *key, darr_search_cmpf cmpf)
{
	bool equal;
	int i;

	i = _darr_search_floor(a, esize, key, &equal, cmpf);
	if (!equal)
		return -1;
	return i;
}

/*
 * _darr_search_ceil —— 在有序数组中查找 key 的“上界”下标（大于等于 key 的最小下标）。
 * 参数与 _darr_search_floor 相同。
 * 返回：命中时返回该下标（*equal 为真）；否则返回 floor+1；都为空时返回 0（可作插入位）。
 * 注意返回值类型是 uint，无元素时返回 0 而非 -1。
 */
uint _darr_search_ceil(const void *a, size_t esize, const void *key, bool *equal,
		       darr_search_cmpf cmpf)
{
	int i;

	i = _darr_search_floor(a, esize, key, equal, cmpf);
	assert(i >= -1);
	if (*equal)
		return (uint)i;
	return (uint)(i + 1);
}

/*
 * darr_strings_cmp —— 面向 darr 字符串指针数组的比较函数，供 darr_str_search* 使用。
 * 参数：a 指向数组中的元素（即一个 char 指针的地址），key 为待比较字符串。
 * 返回：strcmp(*a, key) 的结果（<0 / 0 / >0）。
 */
int darr_strings_cmp(const char **a, const char *key)
{
	return strcmp(*a, key);
}
