// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2017-20  David Lamparter, for NetDEF, Inc.
 */

#ifndef _FRR_XREF_H
#define _FRR_XREF_H

#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include "compiler.h"
#include "typesafe.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * xref.h —— 交叉引用（xref）元数据的声明与打点宏
 * ============================================================================
 *
 * xref 是 FRR 的一种“代码内打点”机制：用宏在日志、事件、命令、断言等位置
 * 埋入一个编译期常量 struct xref，链接器把它们收集进 xref_array 段，运行时由
 * xref.c 为其分配唯一 ID，外部工具（xrelfo.py）也可据此提取所有打点。
 *
 * 核心类型：
 *   - enum xref_type    打点类型（事件调度、日志、断言、命令 defun 等）
 *   - struct xref       只读部分：xrefdata 指针 + 类型 + 代码位置
 *   - struct xrefdata   可写部分：xref 反指针 + uid 文本 + hash 输入
 *   - struct xref_block 一个 DSO 的 xref 段起止（用于启动时遍历）
 *
 * 关键宏：
 *   - XREF_INIT   初始化一个 struct xref
 *   - XREF_LINK   把 xref 登记进 xref_array 段
 *   - XREF_SETUP  在每个 DSO 中调用一次，登记本 DSO 的 xref 段
 *   - XREF_NOTE   生成 ELF note（供外部工具读取），可关闭
 *
 * 与其它模块关系：被 module.h 的 FRR_COREMOD_SETUP 间接引用；实现见 xref.c。
 *
 * 阅读顺序：enum xref_type -> struct xref / xrefdata -> 各打点宏。
 * ============================================================================
 */

/*
 * enum xref_type - xref 打点类型，取值按功能分段编号：
 *   XREFT_NONE            无类型（占位）
 *   XREFT_EVENTSCHED      事件调度
 *   XREFT_LOGMSG          日志消息
 *   XREFT_ASSERT          断言
 *   XREFT_DEFUN           命令定义
 *   XREFT_INSTALL_ELEMENT 命令元素安装
 */
enum xref_type {
	XREFT_NONE = 0,

	XREFT_EVENTSCHED = 0x100,

	XREFT_LOGMSG = 0x200,
	XREFT_ASSERT = 0x280,

	XREFT_DEFUN = 0x300,
	XREFT_INSTALL_ELEMENT = 0x301,
};

/* struct xref is the "const" part;  struct xrefdata is the writable part. */
/*
 * 前置声明：struct xref 为只读部分，struct xrefdata 为可写部分。
 */
struct xref;
struct xrefdata;

/*
 * struct xref - 打点结构中的“只读 / 编译期常量”部分。
 * xrefdata 指向可写的关联数据，可能为 NULL（此时无 UID，无法按 UID 访问）；
 * type 为打点类型（运行时一般不需要）；line / file / func 记录代码位置。
 * 注意：类型相关的附加位通过把本结构嵌入更大的容器结构来追加，
 *       因此本结构固定为 64 位下 32 字节。
 */
struct xref {
	/* this may be NULL, depending on the type of the xref.
	 * if it is NULL, the xref has no unique ID and cannot be accessed
	 * through that mechanism.
	 */
	struct xrefdata *xrefdata;

	/* type isn't generally needed at runtime */
	enum xref_type type;

	/* code location */
	int line;
	const char *file;
	const char *func;

	/* -- 32 bytes (on 64bit) -- */

	/* type-specific bits appended by embedding this struct */
};

/*
 * 用于按 UID 索引 xrefdata 的红黑树声明（唯一键）。
 */
PREDECL_RBTREE_UNIQ(xrefdata_uid);

/*
 * struct xrefdata - 打点结构中的“可写”部分。
 * xref    指回对应的只读 struct xref（启动时由 xref_block_add 建立）
 * uid     Crockford base32 编码的唯一 ID 文本（补齐到 16 字节）
 * hashstr / hashu32 参与 UID 计算的哈希输入；hashstr 为 NULL 时不分配 UID
 * xui     挂入 xrefdata_uid 红黑树的节点
 * 说明：不存在没有 xref 的 xrefdata，但存在没有 xrefdata 的 xref。
 */
struct xrefdata {
	/* pointer back to the const part;  this will be initialized at
	 * program startup by xref_block_add().  (Creating structs with
	 * cyclic pointers to each other is not easily possible for
	 * function-scoped static variables.)
	 *
	 * There is no xrefdata w/o xref, but there are xref w/o xrefdata.
	 */
	const struct xref *xref;

	/* base32(crockford) of unique ID.  not all bytes are used, but
	 * let's pad to 16 for simplicity
	 */
	char uid[16];

	/* hash/uid input
	 * if hashstr is NULL, no UID is assigned/calculated.  Use macro
	 * string concatenation if multiple values need to be fed in.
	 * (This is here to not make the UID calculation independent of
	 * xref type.)
	 */
	const char *hashstr;
	uint32_t hashu32[2];

	/* -- 32 bytes (on 64bit) -- */
	struct xrefdata_uid_item xui;
};

/*
 * xrefdata_uid_cmp - xrefdata 红黑树的比较函数：按 uid 字符串唯一。
 */
static inline int xrefdata_uid_cmp(const struct xrefdata *a,
				   const struct xrefdata *b)
{
	return strcmp(a->uid, b->uid);
}

/*
 * 实例化 xrefdata_uid 类型：元素 struct xrefdata，挂接字段 xui。
 */
DECLARE_RBTREE_UNIQ(xrefdata_uid, struct xrefdata, xui, xrefdata_uid_cmp);
/*
 * 全局的 UID 索引树（定义在 xref.c）。
 */
extern struct xrefdata_uid_head xrefdata_uid;

/* linker "magic" is used to create an array of pointers to struct xref.
 * the result is a contiguous block of pointers, each pointing to an xref
 * somewhere in the code.  The linker gives us start and end pointers, we
 * stuff those into the struct below and hook up a constructor to run at
 * program startup with the struct passed.
 *
 * Placing the xrefs themselves into an array doesn't work because they'd
 * need to be constant size, but we're embedding struct xref into other
 * container structs with extra data.  Also this means that external code
 * (like the python xref dumper) can safely ignore extra data at the end of
 * xrefs without needing to account for size in iterating the array.
 *
 * If you're curious, this is also how __attribute__((constructor)) (and
 * destructor) are implemented - there are 2 arrays, ".init_array" and
 * ".fini_array", containing function pointers.  The magic turns out to be
 * quite mundane, actually ;)
 *
 * The slightly tricky bit is that this is a per-object (i.e. per shared
 * library & daemon) thing and we need a bit of help (in XREF_SETUP) to
 * initialize correctly.
 */

/*
 * struct xref_block - 一个 DSO 的 xref 段描述。
 * start / stop 指向 xref_array 段的首尾（来自链接器符号），
 * next 用于把这些块串成链表，供启动时统一遍历。
 */
struct xref_block {
	struct xref_block *next;
	const struct xref * const *start;
	const struct xref * const *stop;
};

/*
 * xref_blocks - 所有已登记 xref_block 的链表头（定义在 xref.c）。
 */
extern struct xref_block *xref_blocks;
/*
 * xref_block_add - 登记一个 xref_block 并为其内部所有 xref 分配 UID。
 */
extern void xref_block_add(struct xref_block *block);
/*
 * xref_gcc_workaround - 供无法用链接器段收集 xref 的编译器在运行时登记单个 xref。
 */
extern void xref_gcc_workaround(const struct xref *xref);

/* the actual symbols that the linker provides for us.  Note these are
 * _symbols_ referring to the actual section start/end, i.e. they are very
 * much NOT _pointers_, rather the symbol *value* is the pointer.  Declaring
 * them as size-1 arrays is the "best" / "right" thing.
 */
/*
 * 链接器为本 DSO 提供的 xref_array 段起止符号：
 * 它们是“符号地址”而非指针，故声明成长度为 1 的数组（取数组名即地址）。
 * DSO_LOCAL 表示仅本 DSO 可见。
 */
extern const struct xref * const __start_xref_array[1] DSO_LOCAL;
extern const struct xref * const __stop_xref_array[1] DSO_LOCAL;

/*
 * 依据编译器能力定义 xref_array_attr：把 xref 指针放进名为 "xref_array" 的
 * 段中，并标记为 used 防止被优化掉。对开启 AddressSanitizer 的 clang 额外
 * 关闭每项之间的 redzone，以免破坏数组布局。
 */
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
/* no redzone around each of the xref_p please, we're building an array out
 * of variables here.  kinda breaks things if there's redzones between each
 * array item.
 */
#ifdef __clang__
/* GCC doesn't allow no_sanitize on variables, and warns about it :( */
#define xref_array_attr used, section("xref_array"), no_sanitize("address")
#endif
#endif
#endif
#ifndef xref_array_attr
#define xref_array_attr used, section("xref_array")
#endif

/* this macro is invoked once for each standalone DSO through
 *   FRR_MODULE_SETUP  \
 *                      }-> FRR_COREMOD_SETUP -> XREF_SETUP
 *   FRR_DAEMON_INFO   /
 */
/*
 * XREF_SETUP - 在每个独立 DSO 中调用一次，完成 xref 段的登记。
 * 它生成一个哨兵 xref 与指向它的指针（保证段非空），并注册构造函数
 * _xref_init()：在程序启动时构造本 DSO 的 xref_block 并调用 xref_block_add。
 * 展开位置由 FRR_COREMOD_SETUP / FRR_DAEMON_INFO 等宏触发。
 */
#define XREF_SETUP()                                                           \
	static const struct xref _dummy_xref = {                               \
			/* .xrefdata = */ NULL,                                \
			/* .type = */ XREFT_NONE,                              \
			/* .line = */ __LINE__,                                \
			/* .file = */ __FILE__,                                \
			/* .func = */ "dummy",                                 \
	};                                                                     \
	static const struct xref * const _dummy_xref_p                         \
			__attribute__((xref_array_attr)) = &_dummy_xref;       \
	static void __attribute__((used, _CONSTRUCTOR(1100)))                  \
			_xref_init(void) {                                     \
		static struct xref_block _xref_block = {                       \
			.next = NULL,                                          \
			.start = __start_xref_array,                           \
			.stop = __stop_xref_array,                             \
		};                                                             \
		xref_block_add(&_xref_block);                                  \
	}                                                                      \
	asm(XREF_NOTE);                                                        \
	MACRO_REQUIRE_SEMICOLON() /* end */

/* the following blurb emits an ELF note indicating start and end of the xref
 * array in the binary.  This is technically the "correct" entry point for
 * external tools reading xrefs out of an ELF shared library or executable.
 *
 * right now, the extraction tools use the section header for "xref_array"
 * instead; however, section headers are technically not necessarily preserved
 * for fully linked libraries or executables.  (In practice they are only
 * stripped by obfuscation tools.)
 *
 * conversely, for reading xrefs out of a single relocatable object file (e.g.
 * bar.o), section headers are the right thing to look at since the note is
 * only emitted for the final binary once.
 *
 * FRR itself does not need this note to operate correctly, so if you have
 * some build issue with it just add -DFRR_XREF_NO_NOTE to your build flags
 * to disable it.
 */
/*
 * 关闭 ELF note 时（定义 FRR_XREF_NO_NOTE）或在 MIPS64 上，XREF_NOTE 为空；
 * 否则走下面的 #else 分支生成描述 xref 段起止的 ELF note。
 */
#if defined(FRR_XREF_NO_NOTE) || defined(__mips64)
#define XREF_NOTE ""

/* mips64 note:  MIPS64 (regardless of endianness, both mips64 & mips64el)
 * does not have a 64-bit PC-relative relocation type.  Unfortunately, a
 * 64-bit PC-relative relocation is exactly what the below asm magic emits.
 * Therefore, the xref ELF note is permanently disabled on MIPS64.
 *
 * For some context, refer to https://reviews.llvm.org/D80390
 *
 * As noted above, xref extraction still works through the section header
 * path, so no functionality is lost.
 */
#else

/*
 * 按指针宽度选择 ELF note 中的指针字节数与汇编伪指令（.long / .quad）。
 */
#if __SIZEOF_POINTER__ == 4
#define _NOTE_2PTRSIZE	"8"
#define _NOTE_PTR	".long"
#elif __SIZEOF_POINTER__ == 8
#define _NOTE_2PTRSIZE	"16"
#define _NOTE_PTR	".quad"
#else
#error unsupported pointer size
#endif

/*
 * asmspecial - 汇编注释符：ARM 上为 "%"，其余架构为 "@"。
 */
#ifdef __arm__
# define asmspecial "%"
#else
# define asmspecial "@"
#endif

/*
 * XREF_NOTE - 生成名为 _frr_xref_note 的 ELF note（.note.FRR 段），记录
 * __start_xref_array 与 __stop_xref_array 的位置，便于外部工具在段表信息
 * 不完整时定位 xref 段。
 */
#define XREF_NOTE                                                              \
	""                                                                 "\n"\
	"	.type _frr_xref_note," asmspecial "object"                 "\n"\
	"	.pushsection .note.FRR,\"a\"," asmspecial "note"           "\n"\
	"	.p2align 2"                                                "\n"\
	"_frr_xref_note:"                                                  "\n"\
	"	.long	9"                                                 "\n"\
	"	.long	" _NOTE_2PTRSIZE                                   "\n"\
	"	.ascii	\"XREF\""                                          "\n"\
	"	.ascii	\"FRRouting\\0\\0\\0\""                            "\n"\
	"	" _NOTE_PTR "	__start_xref_array-."                      "\n"\
	"	" _NOTE_PTR "	__stop_xref_array-."                       "\n"\
	"	.size _frr_xref_note, .-_frr_xref_note"                    "\n"\
	"	.popsection"                                               "\n"\
	""                                                                 "\n"\
	/* end */
#endif

/* emit the array entry / pointer to xref */
/*
 * XREF_LINK - 把一个 struct xref 登记进 xref_array 段：生成指向它的静态指针，
 * 并用 xref_array_attr 放进段中。
 * clang（或纯 C）走下面一条；GCC 加 C++ 因编译器 bug（GCC PR41091）走
 * _xref_p 包装方案。
 */
#if defined(__clang__) || !defined(__cplusplus)
#define XREF_LINK(dst)                                                         \
	static const struct xref * const NAMECTR(xref_p_)                      \
			__attribute__((xref_array_attr))                       \
		= &(dst)                                                       \
	/* end */

#else /* GCC && C++ */
/* workaround for GCC bug 41091 (dated 2009), added in 2021...
 *
 * this breaks extraction of xrefs with xrelfo.py (because the xref_array
 * entry will be missing), but provides full runtime functionality.  To get
 * the proper list of xrefs from C++ code, build with clang...
 */
/*
 * struct _xref_p - GCC C++ 下的替代方案：用一个带构造函数的包装对象，
 * 在其构造函数里调用 xref_gcc_workaround 完成运行时登记。
 */
struct _xref_p {
	const struct xref * const ptr;

	_xref_p(const struct xref *_ptr) : ptr(_ptr)
	{
		xref_gcc_workaround(_ptr);
	}
};

#define XREF_LINK(dst)                                                         \
	static const struct _xref_p __attribute__((used))                      \
			NAMECTR(xref_p_)(&(dst))                               \
	/* end */
#endif

/* initializer for a "struct xref" */
/*
 * XREF_INIT - struct xref 的初始化器：填入 xrefdata、类型、行号、文件、函数名。
 */
#define XREF_INIT(type_, xrefdata_, func_)                                     \
	{                                                                      \
		/* .xrefdata = */ (xrefdata_),                                 \
		/* .type = */ (type_),                                         \
		/* .line = */ __LINE__,                                        \
		/* .file = */ __FILE__,                                        \
		/* .func = */ func_,                                           \
	}                                                                      \
	/* end */

/* use with XREF_INIT when outside of a function, i.e. no __func__ */
/*
 * XREF_NO_FUNC - 在函数外（无 __func__）使用 XREF_INIT 时替代函数名的占位串。
 */
#define XREF_NO_FUNC	"<global>"

#ifdef __cplusplus
}
#endif

#endif /* _FRR_XREF_H */
