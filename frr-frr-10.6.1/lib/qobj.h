// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2015-16  David Lamparter, for NetDEF, Inc.
 */

/*
 * 【文件导读】qobj.h —— 通用对象标识（QOBJ）框架的对外接口
 *
 * 一、职责
 *   QOBJ（Quagga OBJect）为库内任意一种"对象"（接口、路由表、route-map、
 *   nexthop-group 等）分配一个全局唯一的 64 位 ID（nid），并维护一张线程安全
 *   的注册表，可用该 ID 反查对象指针。它是 FRR 里跨模块、跨线程引用对象时使用
 *   的"句柄"机制，避免调用方直接长期持有裸指针。
 *
 * 二、核心数据结构
 *   struct qobj_node     —— 嵌入宿主结构体里的"锚点"（用 QOBJ_FIELDS 宏声明）。
 *   struct qobj_nodetype —— 每种对象类型一份的全局常量，记录锚点在宿主结构体内
 *                           的偏移，用于由锚点反推出宿主对象首地址。
 *   qobj_nodes 哈希表     —— 以 nid 为键的全局表，真正定义在 qobj.c 中。
 *
 * 三、与其他模块的关系
 *   依赖 typesafe.h（类型安全哈希宏）。qobj.c 实现哈希表与读写锁。
 *   使用者在对象 _create 末尾调用 QOBJ_REG，在 _destroy 开头调用 QOBJ_UNREG，
 *   并用 QOBJ_GET_TYPESAFE / QOBJ_ID 做类型安全的反查与取 ID。
 *
 * 四、推荐阅读顺序
 *   先理解 RESERVED_SPACE_STRUCT 与 qobj_nodetype 的布局约束，再看 qobj_node、
 *   QOBJ_FIELDS，然后是 QOBJ_REG / QOBJ_UNREG，最后是查询宏与 init/finish。
 *   具体实现请配合 qobj.c 阅读。
 *
 * 五、易踩的坑
 *   1) qobj_node 必须作为宿主结构体的成员，且只能用 offsetof 计算偏移的
 *      DEFINE_QOBJ_TYPE 系列宏来声明类型；类型比较是"指针相等"，不可复制类型对象。
 *   2) qobj_get 在并发场景几乎从不安全：拿到指针后对象可能已被其他线程释放；
 *      类型校验要用 qobj_get_typed / QOBJ_GET_TYPESAFE（类型在锁内比较）。
 *   3) QOBJ_ID_0SAFE 是 GCC 语句表达式，对 NULL 指针安全；QOBJ_ID 则不行。
 *   4) DECLARE_QOBJ_TYPE 与 DEFINE_QOBJ_TYPE 必须成对，否则链接期缺符号。
 */

#ifndef _QOBJ_H
#define _QOBJ_H

#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>

#include "typesafe.h"

#ifdef __cplusplus
extern "C" {
#endif

/* reserve a specific amount of bytes for a struct, which can grow up to
 * that size (or be dummy'd out if not needed)
 *
 * note the padding's array size will be an error if it gets negative or zero;
 * this is intentional to prevent the struct from growing beyond the allocated
 * space.
 */
/*
 * 【宏】RESERVED_SPACE_STRUCT(name, fieldname, size)（C 版本）
 *   目的：在结构体里为将来可能扩展的成员 fieldname 预留 size 字节空间。
 *   原理：声明一个匿名 struct，内含真正的 fieldname 成员和一个长度恰为
 *         (size - sizeof(struct name)) 的 padding 数组。将来若 struct name
 *         长得超过 size，数组长度变成负值或零，编译即报错——这是有意设计的
 *         上限保护，防止结构体尺寸无节制增长破坏 ABI。
 *   参数：name      预留成员的类型名（struct 标签）；
 *         fieldname 成员名；
 *         size      预留的总字节数。
 *   坑：size 必须不小于 sizeof(struct name)，否则数组长度为负而报错。
 */
#ifndef __cplusplus
#define RESERVED_SPACE_STRUCT(name, fieldname, size)                           \
	struct {                                                               \
		struct name fieldname;                                         \
		char padding##fieldname[size - sizeof(struct name)];           \
	};
/*
 * 【宏】RESERVED_SPACE_STRUCT(name, fieldname, size)（C++ 版本）
 *   与 C 版本语义相同，仅因 C++ 不允许此处的匿名 struct 用法，改为把真正的
 *   成员与 padding 数组直接摊平成结构体的两个成员，效果一致。
 */
#else
#define RESERVED_SPACE_STRUCT(name, fieldname, size)                           \
	struct name fieldname;                                                 \
	char padding##fieldname[size - sizeof(struct name)];
#endif

/* don't need struct definitions for these here.  code actually using
 * these needs to define the struct *before* including this header.
 * HAVE_QOBJ_xxx should be defined to +1 in that case, like this:
 *
 * #if defined(HAVE_QOBJ_NODETYPE_CLI) && HAVE_QOBJ_NODETYPE_CLI < 0
 * #error include files are in wrong order
 * #else
 * #define HAVE_QOBJ_NODETYPE_CLI 1
 * struct qobj_nodetype_cli { ... }
 * #endif
 */
/*
 * 【占位结构体】qobj_nodetype_cli（HAVE_QOBJ_NODETYPE_CLI 未定义时）
 *   这是给 struct qobj_nodetype 预留 cli 扩展区用的占位类型。真正要使用 CLI
 *   扩展的编译单元必须在包含本头文件之前就定义好 struct qobj_nodetype_cli，
 *   并把 HAVE_QOBJ_NODETYPE_CLI 置为 +1；否则此处退化为一个仅含 dummy 的空壳，
 *   让 RESERVED_SPACE_STRUCT 仍能算出预留区大小。包含顺序写反会触发头文件上方
 *   示例中所示的 #error。
 */
#ifndef HAVE_QOBJ_NODETYPE_CLI
#define HAVE_QOBJ_NODETYPE_CLI -1
struct qobj_nodetype_cli {
	int dummy;
};
#endif

/*
 * 【占位结构体】qobj_nodetype_capnp（HAVE_QOBJ_NODETYPE_CAPNP 未定义时）
 *   与 qobj_nodetype_cli 同理，用于给 capnp 序列化扩展预留空间；未启用时退化
 *   为只含 dummy 的占位类型。使用方需保证先定义结构体再包含本头文件。
 */
#ifndef HAVE_QOBJ_NODETYPE_CAPNP
#define HAVE_QOBJ_NODETYPE_CAPNP -1
struct qobj_nodetype_capnp {
	int dummy;
};
#endif

#include "typesafe.h"

/* each different kind of object will have a global variable of this type,
 * which can be used by various other pieces to store type-related bits.
 * type equality can be tested as pointer equality. (cf. QOBJ_GET_TYPESAFE)
 */
/*
 * 【结构体】struct qobj_nodetype —— 每种对象类型一份的全局元数据
 *   node_member_offset：宿主结构体中 qobj_node 成员的字节偏移（由 offsetof 得到）。
 *     反查时用"锚点地址 - node_member_offset"即可还原宿主对象首地址。
 *   cli / capnp：两段各 256 字节的预留扩展区，借 RESERVED_SPACE_STRUCT 保证
 *     结构体尺寸固定，未来加字段不会破坏 ABI。
 *   每种对象类型对应一个全局常量 qobj_t_<structname>（见 DEFINE_QOBJ_TYPE）。
 *   类型相等性判断就是"指向本结构体的指针相等"，因此类型对象不可被复制。
 *   坑：node_member_offset 必须与宿主结构体真实布局一致；改动成员顺序后若不
 *       重新编译，用 (char *)node - offset 会算出错误地址。
 */
struct qobj_nodetype {
	ptrdiff_t node_member_offset;
	RESERVED_SPACE_STRUCT(qobj_nodetype_cli, cli, 256)
	RESERVED_SPACE_STRUCT(qobj_nodetype_capnp, capnp, 256)
};

/*
 * 【宏】PREDECL_HASH(qobj_nodes)
 *   前置声明一个名为 qobj_nodes 的类型安全哈希表：生成 qobj_nodes_head、
 *   qobj_nodes_item 等类型（供 struct qobj_node 内嵌 item 使用），以及
 *   _init / _add / _del / _find / _pop 等函数原型。真正的实现由 qobj.c 中的
 *   DECLARE_HASH 展开生成，此处只需前向声明，故用 PREDECL。
 */
PREDECL_HASH(qobj_nodes);

/* anchor to be embedded somewhere in the object's struct */
/*
 * 【结构体】struct qobj_node —— 嵌入宿主的"对象锚点"
 *   nid      ：全局唯一 64 位对象 ID，注册时由弱随机数生成；0 被保留为"无效"。
 *   nodehash ：把本锚点挂进全局哈希表 qobj_nodes 的链节（由 PREDECL_HASH 提供）。
 *   type     ：指向类型元数据 qobj_nodetype，用于反推宿主地址与校验类型。
 *   用法：宿主结构体里放一个 QOBJ_FIELDS 成员即可，无需手工初始化，
 *         QOBJ_REG 会负责填充各字段。
 */
struct qobj_node {
	uint64_t nid;
	struct qobj_nodes_item nodehash;
	const struct qobj_nodetype *type;
};

/*
 * 【宏】QOBJ_FIELDS
 *   展开为 "struct qobj_node qobj_node;" 成员。把该宏写进宿主结构体，即可让
 *   对象具备 QOBJ 能力。成员名固定为 qobj_node，供 DEFINE_QOBJ_TYPE 用
 *   offsetof(struct X, qobj_node) 计算偏移。
 */
#define QOBJ_FIELDS struct qobj_node qobj_node

/* call these at the end of any _create function (QOBJ_REG)
 * and beginning of any _destroy function (QOBJ_UNREG) */
/*
 * 【宏】QOBJ_REG(n, structname) / QOBJ_UNREG(n)
 *   QOBJ_REG  ：在对象 _create 函数末尾调用，把对象 n 的锚点注册进全局表，并
 *               关联类型全局变量 qobj_t_<structname>。
 *   QOBJ_UNREG：在对象 _destroy 函数开头调用，先注销再释放，避免反查悬挂。
 *   注意：structname 必须是宿主结构体的标签名，用来拼出类型变量名。
 */
#define QOBJ_REG(n, structname) qobj_reg(&n->qobj_node, &qobj_t_##structname)
#define QOBJ_UNREG(n) qobj_unreg(&n->qobj_node)

/* internals - should not be directly used without a good reason
 *
 * note: qobj_get is essentially never safe to use in MT context because
 * the object could be deleted by another thread -- and worse, it could be
 * of the "wrong" type and deleted.
 *
 * with qobj_get_typed, the type check is done under lock, which means that
 * it can be used as long as another lock prevents the deletion of objects
 * of the expected type.
 *
 * in the long this may need another touch, e.g. built-in per-object locking.
 */
/*
 * 【函数声明】QOBJ 注册表操作（内部接口，无充分理由请勿直接调用）
 *   qobj_reg(node, type)    ：为锚点分配 nid 并插入全局表（需写锁）。
 *   qobj_unreg(node)        ：从全局表删除锚点（需写锁）。
 *   qobj_get(id)            ：按 ID 反查锚点；并发下不安全（见下）。
 *   qobj_get_typed(id,type) ：按 ID 反查并校验类型，返回宿主对象指针或 NULL；
 *                             类型检查在持锁期间完成，配合外部锁可安全使用。
 */
void qobj_reg(struct qobj_node *node, const struct qobj_nodetype *type);
void qobj_unreg(struct qobj_node *node);
struct qobj_node *qobj_get(uint64_t id);
void *qobj_get_typed(uint64_t id, const struct qobj_nodetype *type);

/* type declarations */
/*
 * 【宏】DECLARE_QOBJ_TYPE(structname)
 *   声明（extern）类型元数据全局变量 qobj_t_<structname>。通常放在头文件里，
 *   让其他编译单元引用该类型。需与 DEFINE_QOBJ_TYPE 成对使用。
 */
#define DECLARE_QOBJ_TYPE(structname)                                          \
	extern const struct qobj_nodetype qobj_t_##structname                  \
	/* end */
/*
 * 【宏】DEFINE_QOBJ_TYPE(structname)
 *   定义类型元数据全局常量，自动填入 node_member_offset =
 *   offsetof(struct structname, qobj_node)。每个使用 QOBJ 的结构体都要有一份。
 *   坑：类型对象是 const 全局量，只能用指针相等比较类型，切勿按值拷贝。
 */
#define DEFINE_QOBJ_TYPE(structname)                                           \
	const struct qobj_nodetype qobj_t_##structname = {                     \
		.node_member_offset =                                          \
			(ptrdiff_t)offsetof(struct structname, qobj_node)}     \
	/* end */
/*
 * 【宏】DEFINE_QOBJ_TYPE_INIT(structname, ...)
 *   与 DEFINE_QOBJ_TYPE 相同，但允许通过 __VA_ARGS__ 追加初始化字段
 *   （例如为预留扩展区 cli / capnp 填初值）。末尾的逗号已处理好，追加内容直接
 *   跟在其后即可。
 */
#define DEFINE_QOBJ_TYPE_INIT(structname, ...)                                 \
	const struct qobj_nodetype qobj_t_##structname = {                     \
		.node_member_offset =                                          \
			(ptrdiff_t)offsetof(struct structname, qobj_node),     \
		__VA_ARGS__}                                                   \
	/* end */

/* ID dereference with typecheck.
 * will return NULL if id not found or wrong type. */
/*
 * 【宏】QOBJ_GET_TYPESAFE(id, structname)
 *   类型安全的 ID 反查：等价于 (struct structname *)qobj_get_typed(
 *   id, &qobj_t_<structname>)。找不到对象或类型不符时返回 NULL。并发下推荐的
 *   取值方式。返回的是宿主对象指针，调用者需自行保证其在持锁范围内不被释放。
 */
#define QOBJ_GET_TYPESAFE(id, structname)                                      \
	((struct structname *)qobj_get_typed((id), &qobj_t_##structname))

/*
 * 【宏】QOBJ_ID(ptr)
 *   取出对象 ptr 的 qobj_node.nid。ptr 必须非空，否则解引用会崩溃。
 */
#define QOBJ_ID(ptr) ((ptr)->qobj_node.nid)
/*
 * 【宏】QOBJ_ID_0SAFE(ptr)
 *   对 NULL 安全的取 ID：ptr 为 NULL 时返回 0，否则返回其 nid。用 GCC 语句
 *   表达式把 ptr 只求值一次，避免副作用被重复执行。仅在 GCC / Clang 下可用。
 */
#define QOBJ_ID_0SAFE(ptr)                                                     \
	({                                                                     \
		typeof(ptr) _ptr = (ptr);                                      \
		_ptr ? _ptr->qobj_node.nid : 0ULL;                             \
	})

/*
 * 【函数声明】qobj_init(void) / qobj_finish(void)
 *   qobj_init  ：进程 / 模块启动时调用，初始化读写锁与全局哈希表。
 *   qobj_finish：关闭时调用，弹空并销毁哈希表与锁。
 *   必须在任何 QOBJ_REG / 查询之前完成 qobj_init，顺序错误会导致对未初始化的
 *   锁执行加锁。qobj_finish 之后不得再使用任何 QOBJ 接口。
 */
void qobj_init(void);
void qobj_finish(void);

#ifdef __cplusplus
}
#endif

#endif /* _QOBJ_H */
