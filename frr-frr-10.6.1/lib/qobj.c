// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2015-16  David Lamparter, for NetDEF, Inc.
 *
 * This file is part of Quagga
 */

/*
 * 【文件导读】qobj.c —— QOBJ 全局注册表与 64 位对象 ID 的实现
 *
 * 一、职责
 *   实现 qobj.h 声明的注册表：为对象锚点分配唯一 nid，维护 "nid -> 锚点" 的
 *   全局哈希表，并提供按 ID 反查（含类型校验）的接口。支撑 FRR 各模块用 64 位
 *   ID 而非裸指针来互相引用对象。
 *
 * 二、核心数据结构
 *   nodes_lock ：一把 pthread 读写锁，保护全局表 nodes。注册 / 注销取写锁，
 *                查询取读锁，从而形成"多读单写"的并发模型。
 *   nodes      ：DECLARE_HASH(qobj_nodes, ...) 生成的全局哈希表头，键为 nid。
 *
 * 三、与其他模块的关系
 *   顶部 include 了 frrevent.h、memory.h、hash.h、log.h、jhash.h、network.h。
 *   依赖 frr_weak_random() 产生 nid，依赖 typesafe.h 的哈希宏。对象侧通过
 *   qobj.h 里的 QOBJ_REG / QOBJ_UNREG 宏间接调用本文件的 qobj_reg / qobj_unreg。
 *
 * 四、推荐阅读顺序
 *   先看 qobj_hash / qobj_cmp 两个回调与 DECLARE_HASH，理解哈希表如何工作；再看
 *   nodes_lock / nodes 两个全局量；然后按 qobj_reg、qobj_unreg、qobj_get、
 *   qobj_get_typed、qobj_init、qobj_finish 的顺序阅读。
 *
 * 五、易踩的坑
 *   1) nid 用弱随机数生成并被强制非 0 且不重复（循环重试）；0 恒表示"无效 ID"。
 *   2) qobj_get 返回后锁即释放，对象可能被别的线程删掉——并发下应改用
 *      qobj_get_typed，且其类型比较在锁内完成。
 *   3) qobj_init 必须在任何使用前调用；qobj_finish 会销毁锁，之后不可再用。
 *   4) qobj_get_typed 特意在持读锁期间完成类型判断，源码注释说明了这是为了避免
 *      "删掉的其实是另一种类型对象"的竞态。
 */

#include <zebra.h>

#include "frrevent.h"
#include "memory.h"
#include "hash.h"
#include "log.h"
#include "qobj.h"
#include "jhash.h"
#include "network.h"

/*
 * 【函数】qobj_hash(node)
 *   哈希函数：把 node->nid 直接截断成 uint32_t 作为哈希值。
 *   参数 node：待求哈希的锚点。返回 32 位哈希值。
 *   注意：nid 的高 32 位被丢弃，分布质量最终由 typesafe 哈希层再处理。
 */
static uint32_t qobj_hash(const struct qobj_node *node)
{
	return (uint32_t)node->nid;
}

/*
 * 【函数】qobj_cmp(na, nb)
 *   比较两个锚点的 nid，返回 -1 / 0 / 1，供哈希表做键相等判断。
 *   只有 nid 完全相等才视为同一对象。命中冲突桶时由该回调精确判等。
 */
static int qobj_cmp(const struct qobj_node *na, const struct qobj_node *nb)
{
	if (na->nid < nb->nid)
		return -1;
	if (na->nid > nb->nid)
		return 1;
	return 0;
}

/*
 * 【宏】DECLARE_HASH(qobj_nodes, struct qobj_node, nodehash, qobj_cmp, qobj_hash)
 *   实例化类型安全的哈希表 qobj_nodes，把 qobj.h 里的 PREDECL_HASH 声明补齐为
 *   实现，生成 qobj_nodes_init / _add / _del / _find / _pop 等函数。
 *   参数依次为：表名、元素类型、元素内的链节成员名、比较回调、哈希回调。
 *   这里生成的是带链节 nodehash 的普通哈希表。
 */
DECLARE_HASH(qobj_nodes, struct qobj_node, nodehash,
			qobj_cmp, qobj_hash);

/*
 * 【全局变量】nodes_lock
 *   pthread 读写锁，保护 nodes 表。注册 / 注销取写锁，查询取读锁，实现多读单写。
 *   在 qobj_init 中初始化，在 qobj_finish 中销毁。
 * 【全局变量】nodes
 *   全局哈希表头（键为 nid）。所有已注册对象的锚点都挂在这里；初始为空。
 */
static pthread_rwlock_t nodes_lock;
static struct qobj_nodes_head nodes = { };


/*
 * 【函数】qobj_reg(node, type)
 *   把对象锚点 node 注册进全局表，并记录其类型 type。
 *   流程：先写 node->type；在写锁保护下循环用两个 frr_weak_random() 拼出 64 位
 *   nid，直到非 0 且不与已有对象冲突；随后插入表并解锁。
 *   参数 node：待注册的锚点（通常来自宿主对象的 qobj_node 成员）；
 *        type：类型元数据（qobj_t_<structname>）。
 *   副作用：填充 node->nid / node->type，调用方无需自行初始化。
 *   返回：无。
 *   坑：应在对象 _create 末尾、对象已可安全访问时调用；写锁作用域为整张全局表，
 *       高并发注册会有短暂串行化。
 */
void qobj_reg(struct qobj_node *node, const struct qobj_nodetype *type)
{
	node->type = type;
	pthread_rwlock_wrlock(&nodes_lock);
	do {
		node->nid = (uint64_t)frr_weak_random();
		node->nid ^= (uint64_t)frr_weak_random() << 32;
	} while (!node->nid || qobj_nodes_find(&nodes, node));
	qobj_nodes_add(&nodes, node);
	pthread_rwlock_unlock(&nodes_lock);
}

/*
 * 【函数】qobj_unreg(node)
 *   在写锁保护下把锚点 node 从全局表删除。
 *   应在对象 _destroy 开头、释放内存之前调用，避免其他线程反查到悬空指针。
 *   参数 node：已注册的锚点。返回：无。
 *   坑：对未注册或重复注销的 node 调用属未定义行为；本函数不释放宿主对象。
 */
void qobj_unreg(struct qobj_node *node)
{
	pthread_rwlock_wrlock(&nodes_lock);
	qobj_nodes_del(&nodes, node);
	pthread_rwlock_unlock(&nodes_lock);
}

/*
 * 【函数】qobj_get(id)
 *   按 ID 反查锚点：构造一个仅含 nid 的临时锚点 dummy，在读锁下查表；命中返回
 *   指针，否则返回 NULL。
 *   参数 id：对象 ID。返回：锚点指针（qobj_node *，非宿主对象指针）或 NULL。
 *   坑：并发下不安全——返回后读锁已释放，对象可能被删除；且不做类型校验。
 *       仅在单线程或外部已有更强保护时使用，多线程请改用 qobj_get_typed。
 */
struct qobj_node *qobj_get(uint64_t id)
{
	struct qobj_node dummy = {.nid = id}, *rv;
	pthread_rwlock_rdlock(&nodes_lock);
	rv = qobj_nodes_find(&nodes, &dummy);
	pthread_rwlock_unlock(&nodes_lock);
	return rv;
}

/*
 * 【函数】qobj_get_typed(id, type)
 *   按 ID 反查并做类型校验，返回宿主对象首地址或 NULL。
 *   在读锁保护下：先查表得到锚点，再比较 node->type 是否等于传入的 type；相等时
 *   用 (char *)node - type->node_member_offset 反推出宿主对象指针。
 *   参数 id：对象 ID；type：期望的类型元数据。返回：校验通过返回宿主对象指针，
 *   未找到或类型不符返回 NULL。
 *   坑：源码注释特别指出——类型判断必须在持锁期间完成，否则可能与"另一种类型
 *       对象被删除"发生竞态；调用方仍需用外部锁保证对象在取用期间不被释放。
 */
void *qobj_get_typed(uint64_t id, const struct qobj_nodetype *type)
{
	struct qobj_node dummy = {.nid = id};
	struct qobj_node *node;
	void *rv;

	pthread_rwlock_rdlock(&nodes_lock);
	node = qobj_nodes_find(&nodes, &dummy);

	/* note: we explicitly hold the lock until after we have checked the
	 * type.
	 * if the caller holds a lock that for example prevents the deletion of
	 * route-maps, we can still race against a delete of something that
	 * isn't
	 * a route-map. */
	if (!node || node->type != type)
		rv = NULL;
	else
		rv = (char *)node - node->type->node_member_offset;

	pthread_rwlock_unlock(&nodes_lock);
	return rv;
}

/*
 * 【函数】qobj_init(void)
 *   初始化读写锁 nodes_lock 并初始化全局哈希表 nodes。
 *   应在进程 / 模块启动、任何注册或查询之前调用。返回：无。
 *   坑：重复初始化属未定义行为；未初始化就使用会导致对未初始化锁加锁。
 */
void qobj_init(void)
{
	pthread_rwlock_init(&nodes_lock, NULL);
	qobj_nodes_init(&nodes);
}

/*
 * 【函数】qobj_finish(void)
 *   关闭时清理：循环弹出并删除表中所有锚点，最后销毁读写锁。
 *   返回：无。
 *   坑：只释放注册表结构，不负责释放宿主对象本身；调用后不得再使用任何 QOBJ
 *       接口（锁已销毁）。
 */
void qobj_finish(void)
{
	struct qobj_node *node;
	while ((node = qobj_nodes_pop(&nodes)))
		qobj_nodes_del(&nodes, node);
	pthread_rwlock_destroy(&nodes_lock);
}
