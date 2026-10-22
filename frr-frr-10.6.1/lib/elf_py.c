// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * fast ELF file accessor
 * Copyright (C) 2018-2020  David Lamparter for NetDEF, Inc.
 */

/* 【文件导读】elf_py.c —— 从 ELF 符号表 / 重定位表导出北向 YANG 元数据给 Python 绑定
 *
 * 职责：
 *   这是一个 C 语言写的 Python 扩展模块（模块名 _clippy），用于在构建期快速读取 ELF
 *   文件。FRR 的 clippy（YANG 代码生成器）依赖它：从编译产物里取出符号 / 重定位信息，
 *   进而把北向接口（northbound）所需的 YANG 元数据（回调函数地址等）导出给 Python 绑定。
 *   它刻意模仿 python elfutils 包的一个很小的子集，但速度远快于后者。
 *
 * 核心数据结构（都对应一个 Python 类型）：
 *   - struct elffile  -> ELFFile     ：整个 ELF 文件对象，入口点。
 *   - struct elfsect  -> ELFSection  ：单个节（section），含该节的重定位表。
 *   - struct elfreloc -> ELFReloc    ：单条重定位记录（静态 .o 与动态 .so 通用）。
 *
 * 与其他模块的关系：
 *   - gelf.h（libelf）：本文件几乎 1:1 地映射 ELF 结构，所有真实解析都交给 libelf。
 *   - clippy.h：本模块作为 _clippy 扩展的一部分被初始化（elf_py_init）。
 *   - typesafe.h / jhash.h：用哈希表按重定位偏移量索引 elfrelocs。
 *
 * 阅读顺序：
 *   1) 先看三个 struct 定义，理解对象模型。
 *   2) 看 elfrelocs 哈希（cmp / hash / get）如何按偏移检索重定位。
 *   3) 看 elfsect_add_relocations 与 elffile_add_dynreloc 如何填充重定位。
 *   4) 最后看 elffile_load（唯一真正的入口）与 elf_py_init（类型注册）。
 *
 * 易踩的坑：
 *   - 本代码仅适合读取可信 ELF（如自家构建产物），NOT SUITABLE FOR UNTRUSTED ELF。
 *   - REL（非 RELA）记录没有 addend 字段，需从指针内容自己读出来（见 add_dynreloc）。
 *   - 目标文件的重定位是"按节"的，而可执行 / 共享库是"按文件"的，两者行为被刻意统一。
 *   - 访问 ELF 中的指针时，Python 层必须检查是否有重定位，否则读到的是全 0。
 */
/* Note: this wrapper is intended to be used as build-time helper.  While
 * it should be generally correct and proper, there may be the occasional
 * memory leak or SEGV for things that haven't been well-tested.
 *     _
 *    / \    This code is NOT SUITABLE FOR UNTRUSTED ELF FILES.  It's used
 *   / ! \   in FRR to read files created by its own build.  Don't take it out
 *  /_____\  of FRR and use it to parse random ELF files you found somewhere.
 *
 * If you're working with this code (or even reading it), you really need to
 * read a bunch of the ELF specs.  There's no way around it, things in here
 * just represent pieces of ELF pretty much 1:1.  Also, readelf & objdump are
 * your friends.
 *
 * Required reading:
 *   https://refspecs.linuxfoundation.org/elf/elf.pdf
 *   https://refspecs.linuxfoundation.org/elf/x86_64-SysV-psABI.pdf
 * Recommended reading:
 *   https://github.com/ARM-software/abi-aa/releases/download/2020Q4/aaelf64.pdf
 *
 * The core ELF spec is *not* enough, you should read at least one of the
 * processor specific (psABI) docs.  They define what & how relocations work.
 * Luckily we don't need to care about the processor specifics since this only
 * does data relocations, but without looking at the psABI, some things aren't
 * quite clear.
 */

/* 本模块的 API 大致模仿 python elfutils 包的一个很小的子集（后者太慢）。 */
/* the API of this module roughly follows a very small subset of the one
 * provided by the python elfutils package, which unfortunately is painfully
 * slow.
 */

/* PY_SSIZE_T_CLEAN：要求 Python C-API 对 "y#" / "s#" 等格式统一按 Py_ssize_t 处理
 * 长度，避免旧式 int 截断问题。必须在包含 Python.h 之前定义。 */
#define PY_SSIZE_T_CLEAN

/* 引入 Python C-API、structmember（成员描述）、以及 mmap / stat / fcntl 等系统头文件。 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Python.h>
#include "structmember.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

/* Solaris 32 位平台上 libelf 要求必须关闭大文件支持，否则会报错，故在此撤销
 * _FILE_OFFSET_BITS 并强制为 32。 */
#if defined(__sun__) && (__SIZEOF_POINTER__ == 4)
/* Solaris libelf bails otherwise ... */
#undef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 32
#endif

/* libelf 的通用（generic）接口头文件；本文件的 ELF 解析几乎全部借助它。 */
#include <gelf.h>

/* FRR 内部依赖：typesafe 提供类型安全容器宏，jhash 提供哈希函数，
 * clippy.h 声明本扩展模块的初始化入口 elf_py_init()。 */
#include "typesafe.h"
#include "jhash.h"
#include "clippy.h"

/* debug：全局调试开关，由 Python 侧 elfpy_debug() 设置。 */
static bool debug;

/* debugf(...)：用法同 printf，但仅当 debug 为真时向 stderr 输出。 */
#define debugf(...)                                                            \
	do {                                                                   \
		if (debug)                                                     \
			fprintf(stderr, __VA_ARGS__);                          \
	} while (0)

/* 两个 Python 异常类型对象，在 elf_py_init() 中创建并注册到模块命名空间：
 *   ELFFormatError —— ELF 文件格式错误（继承 ValueError）；
 *   ELFAccessError —— 访问越界 / 无效索引（继承 IndexError）。 */
/* Exceptions */
static PyObject *ELFFormatError;
static PyObject *ELFAccessError;

/* refuse_new：作为三个类型的 tp_new，直接抛错禁止用户直接实例化。
 * 这些对象只能由本模块内部的方法（如 ELFFile 的加载）创建并返回。 */
/* most objects can only be created as return values from one of the methods */
static PyObject *refuse_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyErr_SetString(PyExc_ValueError,
			"cannot create instances of this type");
	return NULL;
}

/* 前置声明：elfreloc / elfsect 在哈希表定义处会被引用。
 * PREDECL_HASH(elfrelocs)：预先声明一个以重定位偏移量为键的哈希表类型，
 * 稍后用 DECLARE_HASH 正式实例化。 */
struct elfreloc;
struct elfsect;

PREDECL_HASH(elfrelocs);

/* ELFFile 与 ELFSection 刻意共享一部分行为（尤其是下标切片访问文件数据）。
 * 原因是：可重定位目标文件（.o）按节处理，而已链接的可执行 / 库按整个文件处理；
 * 让两者接口相似可以简化 Python 侧代码。 */
/* ELFFile and ELFSection intentionally share some behaviour, particularly
 * subscript[123:456] access to file data.  This is because relocatables
 * (.o files) do things section-based, but linked executables/libraries do
 * things file-based.  Having the two behave similar allows simplifying the
 * Python code.
 */

/* struct elffile（对应 Python 类型 ELFFile）：本模块的总入口对象。
 *   filename   —— 打开时传入的文件名；
 *   mmap/mmend/len —— 整文件 mmap 的基址、结束地址、长度；
 *   elf        —— libelf 句柄；
 *   ehdr,_ehdr —— ELF 头（指针 + 内部副本，_ehdr 用于承载 32/64 位与字节序转换后的拷贝）；
 *   symtab/nsym/symstridx/symdata —— 静态符号表（节、符号数、字符串表索引、数据）；
 *   sects/n_sect —— 各节对应的 Python 对象缓存数组（惰性创建）；
 *   dynrelocs  —— 动态重定位哈希表（可执行 / 共享库用）；
 *   elfclass/bigendian/has_symbols —— 32 或 64 位、字节序、是否存在符号表。 */
/* class ELFFile:
 *
 * overall entry point, instantiated by reading in an ELF file
 */
struct elffile {
	PyObject_HEAD

	char *filename;
	char *mmap, *mmend;
	size_t len;
	Elf *elf;

	/* note from here on there are several instances of
	 *
	 *   GElf_Something *x, _x;
	 *
	 * this is a pattern used by libelf's generic ELF routines; the _x
	 * field is used to create a copy of the ELF structure from the file
	 * with 32/64bit and endianness adjusted.
	 */

	GElf_Ehdr *ehdr, _ehdr;
	Elf_Scn *symtab;
	size_t nsym, symstridx;
	Elf_Data *symdata;

	PyObject **sects;
	size_t n_sect;

	struct elfrelocs_head dynrelocs;

	int elfclass;
	bool bigendian;
	bool has_symbols;
};

/* struct elfsect（对应 Python 类型 ELFSection）：单个 ELF 节。
 *   name —— 节名；ef —— 所属文件对象；
 *   _shdr/shdr —— 节头（+ 内部副本）；scn —— libelf 的节句柄；
 *   idx/len —— 节索引与节长度；
 *   relocs  —— 本节内的重定位哈希表（按偏移索引）。 */
/* class ELFSection:
 *
 * note that executables and shared libraries can have their section headers
 * removed, though in practice this is only used as an obfuscation technique.
 */
struct elfsect {
	PyObject_HEAD

	const char *name;
	struct elffile *ef;

	GElf_Shdr _shdr, *shdr;
	Elf_Scn *scn;
	unsigned long idx, len;

	struct elfrelocs_head relocs;
};

/* struct elfreloc（对应 Python 类型 ELFReloc）：一条重定位记录，静态 / 动态通用。
 *   elfrelocs_item —— 挂入哈希表的节点；
 *   es/ef —— 所属节 / 文件；
 *   _rela/rela —— 重定位项（统一转换成带 addend 的 RELA 形式）；
 *   _sym/sym/symidx/symname —— 目标符号及其名字；
 *   symvalid/unresolved/relative/st_value —— 符号是否有效 / 未解析 / 是否 REL 相对、
 *   以及符号值；这些语义在 Python docstring 中有详细说明。 */
/* class ELFReloc:
 *
 * note: relocations in object files (.o) are section-based while relocations
 * in executables and shared libraries are file-based.
 *
 * Whenever accessing something that is a pointer in the ELF file, the Python
 * code needs to check for a relocation;  if the pointer is pointing to some
 * unresolved symbol the file will generally contain 0 bytes.  The relocation
 * will tell what the pointer is actually pointing to.
 *
 * This represents both static (.o file) and dynamic (.so/exec) relocations.
 */
struct elfreloc {
	PyObject_HEAD

	struct elfrelocs_item elfrelocs_item;

	struct elfsect *es;
	struct elffile *ef;

	/* there's also old-fashioned GElf_Rel; we're converting that to
	 * GElf_Rela in elfsect_add_relocations()
	 */
	GElf_Rela _rela, *rela;
	GElf_Sym _sym, *sym;
	size_t symidx;
	const char *symname;

	/* documented below in python docstrings */
	bool symvalid, unresolved, relative;
	unsigned long long st_value;
};

/* elfreloc_cmp / elfreloc_hash：哈希表的比较 / 哈希函数前置声明。
 * DECLARE_HASH 正式实例化 elfrelocs 哈希表，元素是 struct elfreloc，
 * 节点字段 elfrelocs_item，键由比较函数定义（即重定位偏移量 r_offset）。 */
static int elfreloc_cmp(const struct elfreloc *a, const struct elfreloc *b);
static uint32_t elfreloc_hash(const struct elfreloc *reloc);

DECLARE_HASH(elfrelocs, struct elfreloc, elfrelocs_item,
	     elfreloc_cmp, elfreloc_hash);

/* 若干内部函数的前置声明：
 *   elf_find_addr     —— 按虚拟地址查找节；
 *   elffile_secbyidx  —— 取得（并缓存）第 idx 节的 Python 对象；
 *   elfreloc_getsection / elfreloc_getaddend —— ELFReloc 的取值方法。 */
static Elf_Scn *elf_find_addr(struct elffile *ef, uint64_t addr, size_t *idx);
static PyObject *elffile_secbyidx(struct elffile *w, Elf_Scn *scn, size_t idx);
static PyObject *elfreloc_getsection(PyObject *self, PyObject *args);
static PyObject *elfreloc_getaddend(PyObject *obj, void *closure);

/* --- end of declarations -------------------------------------------------- */

/*
 * class ELFReloc:
 */

/* elfreloc_doc：ELFReloc 类型的 tp_doc 文本（暴露给 Python help()）。 */
static const char elfreloc_doc[] =
	"Represents an ELF relocation record\n"
	"\n"
	"(struct elfreloc * in elf_py.c)";

/* member：构造一个 PyMemberDef 条目的便捷宏，自动填入字段名、类型、结构体偏移量
 * 与文档串（文档串末尾会自动追加 "（字段名, 类型 in elf_py.c）" 提示）。用完即 #undef。 */
#define member(name, type, doc)                                                \
	{                                                                      \
		(char *)#name, type, offsetof(struct elfreloc, name), READONLY,\
		(char *)doc "\n\n(\"" #name "\", " #type " in elf_py.c)"       \
	}
/* members_elfreloc：ELFReloc 暴露给 Python 的只读数据成员列表
 * （symname / symvalid / unresolved / relative / st_value），末尾以空条目终止。 */
static PyMemberDef members_elfreloc[] = {
	member(symname, T_STRING,
	       "Name of symbol this relocation refers to.\n"
	       "\n"
	       "Will frequently be `None` in executables and shared libraries."
	),
	member(symvalid, T_BOOL,
	       "Target symbol has a valid type, i.e. not STT_NOTYPE"),
	member(unresolved, T_BOOL,
	       "Target symbol refers to an existing section"),
	member(relative, T_BOOL,
	       "Relocation is a REL (not RELA) record and thus relative."),
	member(st_value, T_ULONGLONG,
	       "Target symbol's value, if known\n\n"
	       "Will be zero for unresolved/external symbols."),
	{}
};
#undef member

/* getset_elfreloc：ELFReloc 的计算属性（getset），目前仅 r_addend 一个。 */
static PyGetSetDef getset_elfreloc[] = {
	{ .name = (char *)"r_addend", .get = elfreloc_getaddend, .doc =
		(char *)"Relocation addend value"},
	{}
};

/* methods_elfreloc：ELFReloc 的方法表（getsection）。 */
static PyMethodDef methods_elfreloc[] = {
	{"getsection", elfreloc_getsection, METH_VARARGS,
		"Find relocation target's ELF section\n\n"
		"Args: address of relocatee (TODO: fix/remove?)\n"
		"Returns: ELFSection or None\n\n"
		"Not possible if section headers have been stripped."},
	{}
};

/* elfreloc_cmp：按重定位偏移量 r_offset 比较两条记录，供哈希表使用。 */
static int elfreloc_cmp(const struct elfreloc *a, const struct elfreloc *b)
{
	if (a->rela->r_offset < b->rela->r_offset)
		return -1;
	if (a->rela->r_offset > b->rela->r_offset)
		return 1;
	return 0;
}

/* elfreloc_hash：对 r_offset 做 jhash，得到哈希值（固定种子）。 */
static uint32_t elfreloc_hash(const struct elfreloc *reloc)
{
	return jhash(&reloc->rela->r_offset, sizeof(reloc->rela->r_offset),
		     0xc9a2b7f4);
}

/* elfrelocs_get：在给定哈希表中按偏移量 offset 查找重定位记录。
 * 用栈上临时 dummy 对象的 r_offset 作为查找键。 */
static struct elfreloc *elfrelocs_get(struct elfrelocs_head *head,
				      GElf_Addr offset)
{
	struct elfreloc dummy;

	dummy.rela = &dummy._rela;
	dummy.rela->r_offset = offset;
	return elfrelocs_find(head, &dummy);
}

/* elfreloc_getsection：给定一个地址，返回它落在哪个 ELF 节（ELFSection 或 None）。
 * 若目标符号已知则直接按符号的 st_shndx 定位节；否则用地址（配合 addend 与 relative
 * 语义）去文件里反查节。 */
static PyObject *elfreloc_getsection(PyObject *self, PyObject *args)
{
	struct elfreloc *w = (struct elfreloc *)self;
	long data;

	if (!PyArg_ParseTuple(args, "k", &data))
		return NULL;

	if (!w->es)
		Py_RETURN_NONE;

	if (!w->symvalid || w->symidx == 0) {
		size_t idx = 0;
		Elf_Scn *scn;

		data = (w->relative ? data : 0) + w->rela->r_addend;
		scn = elf_find_addr(w->es->ef, data, &idx);
		if (!scn)
			Py_RETURN_NONE;
		return elffile_secbyidx(w->es->ef, scn, idx);
	}
	return elffile_secbyidx(w->es->ef, NULL, w->sym->st_shndx);
}

/* elfreloc_getaddend：r_addend 属性的 getter，返回重定位加数。 */
static PyObject *elfreloc_getaddend(PyObject *obj, void *closure)
{
	struct elfreloc *w = (struct elfreloc *)obj;

	return Py_BuildValue("K", (unsigned long long)w->rela->r_addend);
}

/* elfreloc_repr：对象的 repr() 文本，形如 <ELFReloc @偏移 符号+加数>。 */
static PyObject *elfreloc_repr(PyObject *arg)
{
	struct elfreloc *w = (struct elfreloc *)arg;

	return PyUnicode_FromFormat("<ELFReloc @%lu %s+%lu>",
				    (unsigned long)w->rela->r_offset,
				    (w->symname && w->symname[0]) ? w->symname
						: "[0]",
				    (unsigned long)w->rela->r_addend);
}

/* elfreloc_free：对象销毁回调。当前为空实现——重定位记录本身不持有需释放的资源，
 * 数据指向的都是 mmap 区域。 */
static void elfreloc_free(void *arg)
{
	struct elfreloc *w = arg;

	(void)w;
}

/* typeobj_elfreloc：ELFReloc 的 Python 类型对象定义，绑定文档、成员、方法与属性。 */
static PyTypeObject typeobj_elfreloc = {
	PyVarObject_HEAD_INIT(NULL, 0).tp_name = "_clippy.ELFReloc",
	.tp_basicsize = sizeof(struct elfreloc),
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_doc = elfreloc_doc,
	.tp_new = refuse_new,
	.tp_free = elfreloc_free,
	.tp_repr = elfreloc_repr,
	.tp_members = members_elfreloc,
	.tp_methods = methods_elfreloc,
	.tp_getset = getset_elfreloc,
};

/*
 * class ELFSection:
 */

/* elfsect_doc：ELFSection 类型的 tp_doc 文本。 */
static const char elfsect_doc[] =
	"Represents an ELF section\n"
	"\n"
	"To access section contents, use subscript notation, e.g.\n"
	"  section[123:456]\n"
	"To read null terminated C strings, replace the end with str:\n"
	"  section[123:str]\n\n"
	"(struct elfsect * in elf_py.c)";

/* elfsect_getaddr：节虚拟地址（sh_addr）属性的 getter 前置声明。 */
static PyObject *elfsect_getaddr(PyObject *self, void *closure);

/* member：与 ELFReloc 中的同名宏作用相同，此处为 ELFSection 的字段生成成员条目。 */
#define member(name, type, doc)                                                \
	{                                                                      \
		(char *)#name, type, offsetof(struct elfsect, name), READONLY, \
		(char *)doc "\n\n(\"" #name "\", " #type " in elf_py.c)"       \
	}
/* members_elfsect：ELFSection 暴露给 Python 的只读成员（name / idx / len）。 */
static PyMemberDef members_elfsect[] = {
	member(name, T_STRING,
	       "Section name, e.g. \".text\""),
	member(idx, T_ULONG,
	       "Section index in file"),
	member(len, T_ULONG,
	       "Section length in bytes"),
	{},
};
#undef member

/* getset_elfsect：ELFSection 的计算属性（getset），目前仅 sh_addr。 */
static PyGetSetDef getset_elfsect[] = {
	{ .name = (char *)"sh_addr", .get = elfsect_getaddr, .doc =
		(char *)"Section virtual address (mapped program view)"},
	{}
};

/* elfsect_getaddr：返回节的虚拟地址（sh_addr）。 */
static PyObject *elfsect_getaddr(PyObject *self, void *closure)
{
	struct elfsect *w = (struct elfsect *)self;

	return Py_BuildValue("K", (unsigned long long)w->shdr->sh_addr);
}


/* elfsect_getreloc：按节内偏移量查找该处的重定位记录（返回 ELFReloc 或 None）。
 * 查找时把节偏移换算成虚拟地址（偏移 + sh_addr）作为键。 */
static PyObject *elfsect_getreloc(PyObject *self, PyObject *args)
{
	struct elfsect *w = (struct elfsect *)self;
	struct elfreloc *relw;
	unsigned long offs;
	PyObject *ret;

	if (!PyArg_ParseTuple(args, "k", &offs))
		return NULL;

	relw = elfrelocs_get(&w->relocs, offs + w->shdr->sh_addr);
	if (!relw)
		Py_RETURN_NONE;

	ret = (PyObject *)relw;
	Py_INCREF(ret);
	return ret;
}

/* methods_elfsect：ELFSection 的方法表（getreloc）。 */
static PyMethodDef methods_elfsect[] = {
	{"getreloc", elfsect_getreloc, METH_VARARGS,
		"Check for / get relocation at offset into section\n\n"
		"Args: byte offset into section to check\n"
		"Returns: ELFReloc or None"},
	{}
};

/* elfsect_subscript：实现节对象的下标访问 section[start:stop] 或 section[start:str]。
 * 前一种返回指定区间的原始字节；后一种（stop 为 str 类型）读取以 NUL 结尾的 C 字符串。
 * 越界会抛出 ELFAccessError，非法切片抛出 IndexError。 */
static PyObject *elfsect_subscript(PyObject *self, PyObject *key)
{
	Py_ssize_t start, stop, step, sllen;
	struct elfsect *w = (struct elfsect *)self;
	PySliceObject *slice;
	unsigned long offs, len = ~0UL;

	if (!PySlice_Check(key)) {
		PyErr_SetString(PyExc_IndexError,
				"ELFSection subscript must be slice");
		return NULL;
	}
	slice = (PySliceObject *)key;
	if (PyLong_Check(slice->stop)) {
		if (PySlice_GetIndicesEx(key, w->shdr->sh_size,
					 &start, &stop, &step, &sllen))
			return NULL;

		if (step != 1) {
			PyErr_SetString(PyExc_IndexError,
					"ELFSection subscript slice step must be 1");
			return NULL;
		}
		if ((GElf_Xword)stop > w->shdr->sh_size) {
			PyErr_Format(ELFAccessError,
				     "access (%lu) beyond end of section %lu/%s (%lu)",
				     stop, w->idx, w->name, w->shdr->sh_size);
			return NULL;
		}

		offs = start;
		len = sllen;
	} else {
		if (slice->stop != (void *)&PyUnicode_Type
		    || !PyLong_Check(slice->start)) {
			PyErr_SetString(PyExc_IndexError, "invalid slice");
			return NULL;
		}

		offs = PyLong_AsUnsignedLongLong(slice->start);
		len = ~0UL;
	}

	offs += w->shdr->sh_offset;
	if (offs > w->ef->len) {
		PyErr_Format(ELFAccessError,
			     "access (%lu) beyond end of file (%lu)",
			     offs, w->ef->len);
		return NULL;
	}
	if (len == ~0UL)
		len = strnlen(w->ef->mmap + offs, w->ef->len - offs);

	Py_ssize_t pylen = len;

#if PY_MAJOR_VERSION >= 3
	return Py_BuildValue("y#", w->ef->mmap + offs, pylen);
#else
	return Py_BuildValue("s#", w->ef->mmap + offs, pylen);
#endif
}

/* mp_elfsect：把 elfsect_subscript 注册为 ELFSection 的映射协议（下标）实现。 */
static PyMappingMethods mp_elfsect = {
	.mp_subscript = elfsect_subscript,
};

/* elfsect_free：ELFSection 的销毁回调，当前为空实现（数据归属于 ELFFile 的 mmap）。 */
static void elfsect_free(void *arg)
{
	struct elfsect *w = arg;

	(void)w;
}

/* elfsect_repr：对象的 repr() 文本，形如 <ELFSection 节名>。 */
static PyObject *elfsect_repr(PyObject *arg)
{
	struct elfsect *w = (struct elfsect *)arg;

	return PyUnicode_FromFormat("<ELFSection %s>", w->name);
}

/* typeobj_elfsect：ELFSection 的 Python 类型对象定义。 */
static PyTypeObject typeobj_elfsect = {
	PyVarObject_HEAD_INIT(NULL, 0).tp_name = "_clippy.ELFSection",
	.tp_basicsize = sizeof(struct elfsect),
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_doc = elfsect_doc,
	.tp_new = refuse_new,
	.tp_free = elfsect_free,
	.tp_repr = elfsect_repr,
	.tp_as_mapping = &mp_elfsect,
	.tp_members = members_elfsect,
	.tp_methods = methods_elfsect,
	.tp_getset = getset_elfsect,
};

/* elfsect_add_relocations：把一个重定位节（.rel / .rela）中的所有重定位项读入
 * 目标节的 relocs 哈希表。REL 记录没有 addend，会自行补 0 并标记 relative；
 * 只有落在本节地址范围内的项才被收录。 */
static void elfsect_add_relocations(struct elfsect *w, Elf_Scn *rel,
				    GElf_Shdr *relhdr)
{
	size_t i, entries;
	Elf_Scn *symtab = elf_getscn(w->ef->elf, relhdr->sh_link);
	GElf_Shdr _symhdr, *symhdr = gelf_getshdr(symtab, &_symhdr);
	Elf_Data *symdata = elf_getdata(symtab, NULL);
	Elf_Data *reldata = elf_getdata(rel, NULL);

	entries = relhdr->sh_size / relhdr->sh_entsize;
	for (i = 0; i < entries; i++) {
		struct elfreloc *relw;
		size_t symidx;
		GElf_Rela *rela;
		GElf_Sym *sym;

		relw = (struct elfreloc *)typeobj_elfreloc.tp_alloc(
				&typeobj_elfreloc, 0);
		relw->es = w;

		if (relhdr->sh_type == SHT_REL) {
			GElf_Rel _rel, *erel;

			erel = gelf_getrel(reldata, i, &_rel);
			relw->rela = &relw->_rela;
			relw->rela->r_offset = erel->r_offset;
			relw->rela->r_info = erel->r_info;
			relw->rela->r_addend = 0;
			relw->relative = true;
		} else
			relw->rela = gelf_getrela(reldata, i, &relw->_rela);

		rela = relw->rela;
		if (rela->r_offset < w->shdr->sh_addr
		    || rela->r_offset >= w->shdr->sh_addr + w->shdr->sh_size)
			continue;

		symidx = relw->symidx = GELF_R_SYM(rela->r_info);
		sym = relw->sym = gelf_getsym(symdata, symidx, &relw->_sym);
		if (sym) {
			relw->symname = elf_strptr(w->ef->elf, symhdr->sh_link,
						   sym->st_name);
			relw->symvalid = GELF_ST_TYPE(sym->st_info)
					!= STT_NOTYPE;
			relw->unresolved = sym->st_shndx == SHN_UNDEF;
			relw->st_value = sym->st_value;
		} else {
			relw->symname = NULL;
			relw->symvalid = false;
			relw->unresolved = false;
			relw->st_value = 0;
		}

		debugf("reloc @ %016llx sym %5llu %016llx %s\n",
		       (long long)rela->r_offset, (unsigned long long)symidx,
		       (long long)rela->r_addend, relw->symname);

		elfrelocs_add(&w->relocs, relw);
	}
}

/* 以下是 ELFFile 与 ELFSection 之间的绑定 / 加载代码：
 * elfsect_wrap 包装一个节对象，elf_find_section / elf_find_addr 用于按名 / 按地址查找节。 */
/*
 * bindings & loading code between ELFFile and ELFSection
 */

/* elfsect_wrap：创建一个 ELFSection 对象，填充节头信息，并扫描文件中所有指向该节的
 * 重定位节、把其重定位项收集进来。 */
static PyObject *elfsect_wrap(struct elffile *ef, Elf_Scn *scn, size_t idx,
			      const char *name)
{
	struct elfsect *w;
	size_t i;

	w = (struct elfsect *)typeobj_elfsect.tp_alloc(&typeobj_elfsect, 0);
	if (!w)
		return NULL;

	w->name = name;
	w->ef = ef;
	w->scn = scn;
	w->shdr = gelf_getshdr(scn, &w->_shdr);
	w->len = w->shdr->sh_size;
	w->idx = idx;
	elfrelocs_init(&w->relocs);

	for (i = 0; i < ef->ehdr->e_shnum; i++) {
		Elf_Scn *escn = elf_getscn(ef->elf, i);
		GElf_Shdr _shdr, *shdr = gelf_getshdr(escn, &_shdr);

		if (shdr->sh_type != SHT_RELA && shdr->sh_type != SHT_REL)
			continue;
		if (shdr->sh_info && shdr->sh_info != idx)
			continue;
		elfsect_add_relocations(w, escn, shdr);
	}

	return (PyObject *)w;
}

/* elf_find_section：按节名在文件中查找节，可选地通过 idx 返回其索引；未找到返回 NULL。 */
static Elf_Scn *elf_find_section(struct elffile *ef, const char *name,
		size_t *idx)
{
	size_t i;
	const char *secname;

	for (i = 0; i < ef->ehdr->e_shnum; i++) {
		Elf_Scn *scn = elf_getscn(ef->elf, i);
		GElf_Shdr _shdr, *shdr = gelf_getshdr(scn, &_shdr);

		secname = elf_strptr(ef->elf, ef->ehdr->e_shstrndx,
				     shdr->sh_name);
		if (strcmp(secname, name))
			continue;
		if (idx)
			*idx = i;
		return scn;
	}
	return NULL;
}

/* elf_find_addr：按虚拟地址 addr 查找所属的节（跳过 TLS 节，因为其地址无意义）。 */
static Elf_Scn *elf_find_addr(struct elffile *ef, uint64_t addr, size_t *idx)
{
	size_t i;

	for (i = 0; i < ef->ehdr->e_shnum; i++) {
		Elf_Scn *scn = elf_getscn(ef->elf, i);
		GElf_Shdr _shdr, *shdr = gelf_getshdr(scn, &_shdr);

		/* virtual address is kinda meaningless for TLS sections */
		if (shdr->sh_flags & SHF_TLS)
			continue;
		if (addr < shdr->sh_addr ||
		    addr >= shdr->sh_addr + shdr->sh_size)
			continue;

		if (idx)
			*idx = i;
		return scn;
	}
	return NULL;
}

/*
 * class ELFFile:
 */

/* elffile_doc：ELFFile 类型的 tp_doc 文本。 */
static const char elffile_doc[] =
	"Represents an ELF file\n"
	"\n"
	"Args: filename to load\n"
	"\n"
	"To access raw file contents, use subscript notation, e.g.\n"
	"  file[123:456]\n"
	"To read null terminated C strings, replace the end with str:\n"
	"  file[123:str]\n\n"
	"(struct elffile * in elf_py.c)";


/* member：与前面两处同名宏作用相同，此处为 ELFFile 的字段生成成员条目。 */
#define member(name, type, doc)                                                \
	{                                                                      \
		(char *)#name, type, offsetof(struct elffile, name), READONLY, \
		(char *)doc "\n\n(\"" #name "\", " #type " in elf_py.c)"       \
	}
/* members_elffile：ELFFile 暴露给 Python 的只读成员
 * （filename / elfclass / bigendian / has_symbols）。 */
static PyMemberDef members_elffile[] = {
	member(filename, T_STRING,
	       "Original file name as given when opening"),
	member(elfclass, T_INT,
	       "ELF class (architecture bit size)\n\n"
	       "Either 32 or 64, straight integer."),
	member(bigendian, T_BOOL,
	       "ELF file is big-endian\n\n"
	       "All internal ELF structures are automatically converted."),
	member(has_symbols, T_BOOL,
	       "A symbol section is present\n\n"
	       "Note: only refers to .symtab/SHT_SYMTAB section, not DT_SYMTAB"
	),
	{},
};
#undef member

/* elffile_secbyidx：返回第 idx 个节对应的 ELFSection Python 对象（惰性创建并缓存到
 * w->sects 数组里）；越界或无该节时返回 None。 */
static PyObject *elffile_secbyidx(struct elffile *w, Elf_Scn *scn, size_t idx)
{
	const char *name;
	PyObject *ret;

	if (!scn)
		scn = elf_getscn(w->elf, idx);
	if (!scn || idx >= w->n_sect)
		Py_RETURN_NONE;

	if (!w->sects[idx]) {
		GElf_Shdr _shdr, *shdr = gelf_getshdr(scn, &_shdr);

		name = elf_strptr(w->elf, w->ehdr->e_shstrndx, shdr->sh_name);
		w->sects[idx] = elfsect_wrap(w, scn, idx, name);
	}

	ret = w->sects[idx];
	Py_INCREF(ret);
	return ret;
}

/* elffile_get_section：按节名返回 ELFSection。 */
static PyObject *elffile_get_section(PyObject *self, PyObject *args)
{
	const char *name;
	struct elffile *w = (struct elffile *)self;
	Elf_Scn *scn;
	size_t idx = 0;

	if (!PyArg_ParseTuple(args, "s", &name))
		return NULL;

	scn = elf_find_section(w, name, &idx);
	return elffile_secbyidx(w, scn, idx);
}

/* elffile_get_section_addr：按虚拟地址返回其所属的 ELFSection。 */
static PyObject *elffile_get_section_addr(PyObject *self, PyObject *args)
{
	unsigned long long addr;
	struct elffile *w = (struct elffile *)self;
	Elf_Scn *scn;
	size_t idx = 0;

	if (!PyArg_ParseTuple(args, "K", &addr))
		return NULL;

	scn = elf_find_addr(w, addr, &idx);
	return elffile_secbyidx(w, scn, idx);
}

/* elffile_get_section_idx：按节索引返回 ELFSection。 */
static PyObject *elffile_get_section_idx(PyObject *self, PyObject *args)
{
	unsigned long long idx;
	struct elffile *w = (struct elffile *)self;

	if (!PyArg_ParseTuple(args, "K", &idx))
		return NULL;

	return elffile_secbyidx(w, NULL, idx);
}

/* elffile_get_symbol：按名字在静态符号表中查找符号，返回
 * （符号名, 符号值, 所属 ELFSection 或 None）三元组；未找到返回 None。 */
static PyObject *elffile_get_symbol(PyObject *self, PyObject *args)
{
	const char *name, *symname;
	struct elffile *w = (struct elffile *)self;
	GElf_Sym _sym, *sym;
	size_t i;

	if (!PyArg_ParseTuple(args, "s", &name))
		return NULL;

	for (i = 0; i < w->nsym; i++) {
		sym = gelf_getsym(w->symdata, i, &_sym);
		if (sym->st_name == 0)
			continue;
		symname = elf_strptr(w->elf, w->symstridx, sym->st_name);
		if (strcmp(symname, name))
			continue;

		PyObject *pysect;
		Elf_Scn *scn = elf_getscn(w->elf, sym->st_shndx);

		if (scn)
			pysect = elffile_secbyidx(w, scn, sym->st_shndx);
		else {
			pysect = Py_None;
			Py_INCREF(pysect);
		}
		return Py_BuildValue("sKN", symname,
				(unsigned long long)sym->st_value, pysect);
	}
	Py_RETURN_NONE;
}

/* elffile_getreloc：在文件的动态重定位表中按地址查找 ELFReloc。 */
static PyObject *elffile_getreloc(PyObject *self, PyObject *args)
{
	struct elffile *w = (struct elffile *)self;
	struct elfreloc *relw;
	unsigned long offs;
	PyObject *ret;

	if (!PyArg_ParseTuple(args, "k", &offs))
		return NULL;

	relw = elfrelocs_get(&w->dynrelocs, offs);
	if (!relw)
		Py_RETURN_NONE;

	ret = (PyObject *)relw;
	Py_INCREF(ret);
	return ret;
}

/* elffile_find_note：在 PT_NOTE 段中查找指定 owner 名与 4 字节类型 ID 的 note 条目，
 * 返回其数据段对应的切片（start, stop），可作为下标访问文件；未找到或无相关支持时返回 None。 */
static PyObject *elffile_find_note(PyObject *self, PyObject *args)
{
#if defined(HAVE_GELF_GETNOTE) && defined(HAVE_ELF_GETDATA_RAWCHUNK)
	const char *owner;
	const uint8_t *ids;
	GElf_Word id;
	struct elffile *w = (struct elffile *)self;
	size_t i;

	if (!PyArg_ParseTuple(args, "ss", &owner, &ids))
		return NULL;

	if (strlen((char *)ids) != 4) {
		PyErr_SetString(PyExc_ValueError,
				"ELF note ID must be exactly 4-byte string");
		return NULL;
	}
	if (w->bigendian)
		id = (ids[0] << 24) | (ids[1] << 16) | (ids[2] << 8) | ids[3];
	else
		id = (ids[3] << 24) | (ids[2] << 16) | (ids[1] << 8) | ids[0];

	for (i = 0; i < w->ehdr->e_phnum; i++) {
		GElf_Phdr _phdr, *phdr = gelf_getphdr(w->elf, i, &_phdr);
		Elf_Data *notedata;
		size_t offset;

		if (phdr->p_type != PT_NOTE)
			continue;

		notedata = elf_getdata_rawchunk(w->elf, phdr->p_offset,
						phdr->p_filesz, ELF_T_NHDR);

		GElf_Nhdr nhdr[1];
		size_t nameoffs, dataoffs;

		offset = 0;
		while ((offset = gelf_getnote(notedata, offset, nhdr,
					      &nameoffs, &dataoffs))) {
			if (phdr->p_offset + nameoffs >= w->len)
				continue;

			const char *name = w->mmap + phdr->p_offset + nameoffs;

			if (strcmp(name, owner))
				continue;
			if (id != nhdr->n_type)
				continue;

			PyObject *s, *e;

			s = PyLong_FromUnsignedLongLong(
				phdr->p_vaddr + dataoffs);
			e = PyLong_FromUnsignedLongLong(
				phdr->p_vaddr + dataoffs + nhdr->n_descsz);
			return PySlice_New(s, e, NULL);
		}
	}
#endif
	Py_RETURN_NONE;
}

#ifdef HAVE_ELF_GETDATA_RAWCHUNK
/* elffile_virt2file：把虚拟地址 virt 转换成文件内偏移 offs（借助 PT_LOAD 段头）。
 * 若地址落在某个段的 memsz 内但超出 filesz（即属于 .bss 之类无文件内容的区域）则返回 false。
 * 仅在 HAVE_ELF_GETDATA_RAWCHUNK 可用时编译。 */
static bool elffile_virt2file(struct elffile *w, GElf_Addr virt,
			      GElf_Addr *offs)
{
	*offs = 0;

	for (size_t i = 0; i < w->ehdr->e_phnum; i++) {
		GElf_Phdr _phdr, *phdr = gelf_getphdr(w->elf, i, &_phdr);

		if (phdr->p_type != PT_LOAD)
			continue;

		if (virt < phdr->p_vaddr
		    || virt >= phdr->p_vaddr + phdr->p_memsz)
			continue;

		if (virt >= phdr->p_vaddr + phdr->p_filesz)
			return false;

		*offs = virt - phdr->p_vaddr + phdr->p_offset;
		return true;
	}

	return false;
}
#endif /* HAVE_ELF_GETDATA_RAWCHUNK */

/* elffile_subscript：实现 file[start:stop] 或 file[start:str] 的下标访问。
 * 它是"按文件"而非"按节"的访问方式，用虚拟地址定位到 PT_LOAD 段再取字节；
 * stop 为 str 类型时读取以 NUL 结尾的 C 字符串。 */
static PyObject *elffile_subscript(PyObject *self, PyObject *key)
{
	Py_ssize_t start, stop, step;
	PySliceObject *slice;
	struct elffile *w = (struct elffile *)self;
	bool str = false;

	if (!PySlice_Check(key)) {
		PyErr_SetString(PyExc_IndexError,
				"ELFFile subscript must be slice");
		return NULL;
	}
	slice = (PySliceObject *)key;
	stop = -1;
	step = 1;
	if (PyLong_Check(slice->stop)) {
		start = PyLong_AsSsize_t(slice->start);
		if (PyErr_Occurred())
			return NULL;
		if (slice->stop != Py_None) {
			stop = PyLong_AsSsize_t(slice->stop);
			if (PyErr_Occurred())
				return NULL;
		}
		if (slice->step != Py_None) {
			step = PyLong_AsSsize_t(slice->step);
			if (PyErr_Occurred())
				return NULL;
		}
	} else {
		if (slice->stop != (void *)&PyUnicode_Type
		    || !PyLong_Check(slice->start)) {
			PyErr_SetString(PyExc_IndexError, "invalid slice");
			return NULL;
		}

		str = true;
		start = PyLong_AsUnsignedLongLong(slice->start);
	}
	if (step != 1) {
		PyErr_SetString(PyExc_IndexError,
				"ELFFile subscript slice step must be 1");
		return NULL;
	}

	GElf_Addr xstart = start, xstop = stop;

	for (size_t i = 0; i < w->ehdr->e_phnum; i++) {
		GElf_Phdr _phdr, *phdr = gelf_getphdr(w->elf, i, &_phdr);

		if (phdr->p_type != PT_LOAD)
			continue;

		if (xstart < phdr->p_vaddr
		    || xstart >= phdr->p_vaddr + phdr->p_memsz)
			continue;
		if (!str && (xstop < phdr->p_vaddr
		    || xstop > phdr->p_vaddr + phdr->p_memsz)) {
			PyErr_Format(ELFAccessError,
				     "access (%llu) beyond end of program header (%llu)",
				     (long long)xstop,
				     (long long)(phdr->p_vaddr +
						 phdr->p_memsz));
			return NULL;
		}

		xstart = xstart - phdr->p_vaddr + phdr->p_offset;

		if (str)
			xstop = strlen(w->mmap + xstart);
		else
			xstop = xstop - phdr->p_vaddr + phdr->p_offset;

		Py_ssize_t pylen = xstop - xstart;

#if PY_MAJOR_VERSION >= 3
		return Py_BuildValue("y#", w->mmap + xstart, pylen);
#else
		return Py_BuildValue("s#", w->mmap + xstart, pylen);
#endif
	};

	return PyErr_Format(ELFAccessError,
			    "virtual address (%llu) not found in program headers",
			    (long long)start);
}

/* methods_elffile：ELFFile 的方法表
 * （find_note / getreloc / get_symbol / get_section / get_section_addr / get_section_idx）。 */
static PyMethodDef methods_elffile[] = {
	{"find_note", elffile_find_note, METH_VARARGS,
		"find specific note entry"},
	{"getreloc", elffile_getreloc, METH_VARARGS,
		"find relocation"},
	{"get_symbol", elffile_get_symbol, METH_VARARGS,
		"find symbol by name"},
	{"get_section", elffile_get_section, METH_VARARGS,
		"find section by name"},
	{"get_section_addr", elffile_get_section_addr, METH_VARARGS,
		"find section by address"},
	{"get_section_idx", elffile_get_section_idx, METH_VARARGS,
		"find section by index"},
	{}
};

static PyObject *elffile_load(PyTypeObject *type, PyObject *args,
			     PyObject *kwds);

/* elffile_free：ELFFile 的销毁回调。释放 libelf 句柄、解除 mmap 映射、释放文件名副本。 */
static void elffile_free(void *arg)
{
	struct elffile *w = arg;

	elf_end(w->elf);
	munmap(w->mmap, w->len);
	free(w->filename);
}

/* mp_elffile：把 elffile_subscript 注册为 ELFFile 的下标（映射协议）实现。 */
static PyMappingMethods mp_elffile = {
	.mp_subscript = elffile_subscript,
};

/* typeobj_elffile：ELFFile 的 Python 类型对象定义；注意 tp_new 指向 elffile_load，
 * 也就是说用 ELFFile(filename) 构造对象时会直接走加载逻辑。 */
static PyTypeObject typeobj_elffile = {
	PyVarObject_HEAD_INIT(NULL, 0).tp_name = "_clippy.ELFFile",
	.tp_basicsize = sizeof(struct elffile),
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_doc = elffile_doc,
	.tp_new = elffile_load,
	.tp_free = elffile_free,
	.tp_as_mapping = &mp_elffile,
	.tp_members = members_elffile,
	.tp_methods = methods_elffile,
};

#ifdef HAVE_ELF_GETDATA_RAWCHUNK
/* elfdata_strptr：从一块 Elf_Data 的 offset 处取以 NUL 结尾的字符串指针；
 * 若越界或没有 NUL 终止符则返回 NULL。仅在 HAVE_ELF_GETDATA_RAWCHUNK 可用时编译。 */
static char *elfdata_strptr(Elf_Data *data, size_t offset)
{
	char *p;

	if (offset >= data->d_size)
		return NULL;

	p = (char *)data->d_buf + offset;
	if (strnlen(p, data->d_size - offset) >= data->d_size - offset)
		return NULL;

	return p;
}

/* elffile_add_dynreloc：把动态重定位段（.rel.dyn / .rela.dyn）的条目读入 w->dynrelocs。
 * typ 指定是 REL 还是 RELA。对 REL 记录，需要按 virt2file 找到指针所在位置、读出其内容
 * 作为 addend（注意 libelf 会做字节序转换但不做位宽转换，故对大端小尺寸情形有特殊处理）。 */
static void elffile_add_dynreloc(struct elffile *w, Elf_Data *reldata,
				 size_t entries, Elf_Data *symdata,
				 Elf_Data *strdata, Elf_Type typ)
{
	size_t i;

	for (i = 0; i < entries; i++) {
		struct elfreloc *relw;
		size_t symidx;
		GElf_Rela *rela;
		GElf_Sym *sym;
		GElf_Addr rel_offs = 0;

		relw = (struct elfreloc *)typeobj_elfreloc.tp_alloc(
				&typeobj_elfreloc, 0);
		relw->ef = w;

		if (typ == ELF_T_REL) {
			GElf_Rel _rel, *rel;
			GElf_Addr offs;

			rel = gelf_getrel(reldata, i, &_rel);
			relw->rela = &relw->_rela;
			relw->rela->r_offset = rel->r_offset;
			relw->rela->r_info = rel->r_info;
			relw->rela->r_addend = 0;
			relw->relative = true;

			/* REL uses the pointer contents itself instead of the
			 * RELA addend field :( ... theoretically this could
			 * be some weird platform specific encoding, but since
			 * we only care about data relocations it should
			 * always be a pointer...
			 */
			if (elffile_virt2file(w, rel->r_offset, &offs)) {
				Elf_Data *ptr;

				/* NB: this endian-converts! */
				ptr = elf_getdata_rawchunk(w->elf, offs,
							   w->elfclass / 8,
							   ELF_T_ADDR);

				if (ptr) {
					char *dst = (char *)&rel_offs;

					/* sigh.  it endian-converts.  but
					 * doesn't size-convert.
					 */
					if (BYTE_ORDER == BIG_ENDIAN &&
					    ptr->d_size < sizeof(rel_offs))
						dst += sizeof(rel_offs) -
						       ptr->d_size;

					memcpy(dst, ptr->d_buf, ptr->d_size);

					relw->relative = false;
					relw->rela->r_addend = rel_offs;
				}
			}
		} else
			relw->rela = gelf_getrela(reldata, i, &relw->_rela);

		rela = relw->rela;
		symidx = relw->symidx = GELF_R_SYM(rela->r_info);
		sym = relw->sym = gelf_getsym(symdata, symidx, &relw->_sym);
		if (sym) {
			if (strdata)
				relw->symname = elfdata_strptr(strdata,
							       sym->st_name);
			relw->symvalid = GELF_ST_TYPE(sym->st_info)
					!= STT_NOTYPE;
			relw->unresolved = sym->st_shndx == SHN_UNDEF;
			relw->st_value = sym->st_value;
		} else {
			relw->symname = NULL;
			relw->symvalid = false;
			relw->unresolved = false;
			relw->st_value = 0;
		}

		if (typ == ELF_T_RELA)
			debugf("dynrela @ %016llx sym %5llu %016llx %s\n",
			       (long long)rela->r_offset,
			       (unsigned long long)symidx,
			       (long long)rela->r_addend, relw->symname);
		else
			debugf("dynrel @ %016llx sym %5llu (%016llx) %s\n",
			       (long long)rela->r_offset,
			       (unsigned long long)symidx,
			       (unsigned long long)rel_offs, relw->symname);

		elfrelocs_add(&w->dynrelocs, relw);
	}

}
#endif /* HAVE_ELF_GETDATA_RAWCHUNK */

/* elffile_load：本模块真正（也是唯一）的入口点，作为 ELFFile 的 tp_new。
 * 流程：打开并 fstat 文件 -> mmap -> 校验 ELF 魔数与类 / 字节序 -> elf_memory 建句柄 ->
 * 读 ELF 头 -> 定位静态符号表 -> 解析 PT_DYNAMIC 中的动态重定位 -> 为节对象数组分配内存。 */
/* primary (only, really) entry point to anything in this module */
static PyObject *elffile_load(PyTypeObject *type, PyObject *args,
			      PyObject *kwds)
{
	const char *filename;
	static const char * const kwnames[] = {"filename", NULL};
	struct elffile *w;
	struct stat st;
	int fd, err;

	w = (struct elffile *)typeobj_elffile.tp_alloc(&typeobj_elffile, 0);
	if (!w)
		return NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", (char **)kwnames,
					 &filename))
		return NULL;

	w->filename = strdup(filename);
	fd = open(filename, O_RDONLY | O_NOCTTY);
	if (fd < 0 || fstat(fd, &st)) {
		PyErr_SetFromErrnoWithFilename(PyExc_OSError, filename);
		if (fd >= 0)
			close(fd);
		goto out;
	}
	w->len = st.st_size;
	w->mmap = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (!w->mmap) {
		PyErr_SetFromErrnoWithFilename(PyExc_IOError, filename);
		close(fd);
		goto out;
	}
	close(fd);
	w->mmend = w->mmap + st.st_size;

	if (w->len < EI_NIDENT || memcmp(w->mmap, ELFMAG, SELFMAG)) {
		PyErr_SetString(ELFFormatError, "invalid ELF signature");
		goto out;
	}

	switch (w->mmap[EI_CLASS]) {
	case ELFCLASS32:
		w->elfclass = 32;
		break;
	case ELFCLASS64:
		w->elfclass = 64;
		break;
	default:
		PyErr_SetString(ELFFormatError, "invalid ELF class");
		goto out;
	}
	switch (w->mmap[EI_DATA]) {
	case ELFDATA2LSB:
		w->bigendian = false;
		break;
	case ELFDATA2MSB:
		w->bigendian = true;
		break;
	default:
		PyErr_SetString(ELFFormatError, "invalid ELF byte order");
		goto out;
	}

	w->elf = elf_memory(w->mmap, w->len);
	if (!w->elf)
		goto out_elferr;
	w->ehdr = gelf_getehdr(w->elf, &w->_ehdr);
	if (!w->ehdr)
		goto out_elferr;

	for (size_t i = 0; i < w->ehdr->e_shnum; i++) {
		Elf_Scn *scn = elf_getscn(w->elf, i);
		GElf_Shdr _shdr, *shdr = gelf_getshdr(scn, &_shdr);

		if (shdr->sh_type == SHT_SYMTAB) {
			w->symtab = scn;
			w->nsym = shdr->sh_size / shdr->sh_entsize;
			w->symdata = elf_getdata(scn, NULL);
			w->symstridx = shdr->sh_link;
			break;
		}
	}
	w->has_symbols = w->symtab && w->symstridx;
	elfrelocs_init(&w->dynrelocs);

#ifdef HAVE_ELF_GETDATA_RAWCHUNK
	for (size_t i = 0; i < w->ehdr->e_phnum; i++) {
		GElf_Phdr _phdr, *phdr = gelf_getphdr(w->elf, i, &_phdr);

		if (phdr->p_type != PT_DYNAMIC)
			continue;

		Elf_Data *dyndata = elf_getdata_rawchunk(w->elf,
				phdr->p_offset, phdr->p_filesz, ELF_T_DYN);

		GElf_Addr dynrela = 0, dynrel = 0, symtab = 0, strtab = 0;
		size_t dynrelasz = 0, dynrelaent = 0;
		size_t dynrelsz = 0, dynrelent = 0;
		size_t strsz = 0;
		GElf_Dyn _dyn, *dyn;

		for (size_t j = 0;; j++) {
			dyn = gelf_getdyn(dyndata, j, &_dyn);

			if (dyn->d_tag == DT_NULL)
				break;

			switch (dyn->d_tag) {
			case DT_SYMTAB:
				symtab = dyn->d_un.d_ptr;
				break;

			case DT_STRTAB:
				strtab = dyn->d_un.d_ptr;
				break;
			case DT_STRSZ:
				strsz = dyn->d_un.d_val;
				break;

			case DT_RELA:
				dynrela = dyn->d_un.d_ptr;
				break;
			case DT_RELASZ:
				dynrelasz = dyn->d_un.d_val;
				break;
			case DT_RELAENT:
				dynrelaent = dyn->d_un.d_val;
				break;

			case DT_REL:
				dynrel = dyn->d_un.d_ptr;
				break;
			case DT_RELSZ:
				dynrelsz = dyn->d_un.d_val;
				break;
			case DT_RELENT:
				dynrelent = dyn->d_un.d_val;
				break;
			}
		}

		GElf_Addr offset;
		Elf_Data *symdata = NULL, *strdata = NULL;

		if (elffile_virt2file(w, symtab, &offset))
			symdata = elf_getdata_rawchunk(w->elf, offset,
						       w->len - offset,
						       ELF_T_SYM);
		if (elffile_virt2file(w, strtab, &offset))
			strdata = elf_getdata_rawchunk(w->elf, offset,
						       strsz, ELF_T_BYTE);

		size_t c;

		if (dynrela && dynrelasz && dynrelaent
		    && elffile_virt2file(w, dynrela, &offset)) {
			Elf_Data *reladata = NULL;

			debugf("dynrela @%llx/%llx+%llx\n", (long long)dynrela,
			       (long long)offset, (long long)dynrelasz);

			reladata = elf_getdata_rawchunk(w->elf, offset,
							dynrelasz, ELF_T_RELA);

			c = dynrelasz / dynrelaent;
			elffile_add_dynreloc(w, reladata, c, symdata, strdata,
					     ELF_T_RELA);
		}

		if (dynrel && dynrelsz && dynrelent
		    && elffile_virt2file(w, dynrel, &offset)) {
			Elf_Data *reldata = NULL;

			debugf("dynrel @%llx/%llx+%llx\n", (long long)dynrel,
			       (long long)offset, (long long)dynrelsz);

			reldata = elf_getdata_rawchunk(w->elf, offset, dynrelsz,
						       ELF_T_REL);

			c = dynrelsz / dynrelent;
			elffile_add_dynreloc(w, reldata, c, symdata, strdata,
					     ELF_T_REL);
		}
	}
#endif
	if (w->ehdr->e_shnum > 0)
		w->sects = calloc(w->ehdr->e_shnum, sizeof(PyObject *));
	w->n_sect = w->ehdr->e_shnum;

	return (PyObject *)w;

out_elferr:
	err = elf_errno();

	PyErr_Format(ELFFormatError, "libelf error %d: %s",
		     err, elf_errmsg(err));
out:
	if (w->elf)
		elf_end(w->elf);
	free(w->filename);
	return NULL;
}

/* elfpy_debug：模块级函数，按布尔参数开关全局调试输出（设置 debug 变量）。 */
static PyObject *elfpy_debug(PyObject *self, PyObject *args)
{
	int arg;

	if (!PyArg_ParseTuple(args, "p", &arg))
		return NULL;

	debug = arg;

	Py_RETURN_NONE;
}

/* methods_elfpy：模块级方法表，目前只有 elfpy_debug 一个。 */
static PyMethodDef methods_elfpy[] = {
	{"elfpy_debug", elfpy_debug, METH_VARARGS, "switch debuging on/off"},
	{}
};

/* elf_py_init：模块初始化入口（由 clippy.h 声明、被扩展模块调用）。
 * 完成三件事：让三个自定义类型就绪、初始化 libelf 版本、把类型 / 异常 / 方法注册进模块。 */
bool elf_py_init(PyObject *pymod)
{
	if (PyType_Ready(&typeobj_elffile) < 0)
		return false;
	if (PyType_Ready(&typeobj_elfsect) < 0)
		return false;
	if (PyType_Ready(&typeobj_elfreloc) < 0)
		return false;
	if (elf_version(EV_CURRENT) == EV_NONE)
		return false;

#if PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 5
	PyModule_AddFunctions(pymod, methods_elfpy);
#else
	(void)methods_elfpy;
#endif

#if defined(HAVE_GELF_GETNOTE) && defined(HAVE_ELF_GETDATA_RAWCHUNK)
	PyObject *elf_notes = Py_True;
#else
	PyObject *elf_notes = Py_False;
#endif
	Py_INCREF(elf_notes);
	if (PyModule_AddObject(pymod, "elf_notes", elf_notes))
		Py_DECREF(elf_notes);

	ELFFormatError = PyErr_NewException("_clippy.ELFFormatError",
					    PyExc_ValueError, NULL);
	PyModule_AddObject(pymod, "ELFFormatError", ELFFormatError);
	ELFAccessError = PyErr_NewException("_clippy.ELFAccessError",
					    PyExc_IndexError, NULL);
	PyModule_AddObject(pymod, "ELFAccessError", ELFAccessError);

	Py_INCREF(&typeobj_elffile);
	PyModule_AddObject(pymod, "ELFFile", (PyObject *)&typeobj_elffile);
	Py_INCREF(&typeobj_elfsect);
	PyModule_AddObject(pymod, "ELFSection", (PyObject *)&typeobj_elfsect);
	Py_INCREF(&typeobj_elfreloc);
	PyModule_AddObject(pymod, "ELFReloc", (PyObject *)&typeobj_elfreloc);
	return true;
}
