// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP advertisement and adjacency
 * Copyright (C) 1996, 97, 98, 99, 2000 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_advertise.c —— 出/入向通告记录(adj_out / adj_in)的增删与属性聚合
 * ============================================================================
 * 职责与定位：本文件是 bgp_advertise.h 里那套数据结构的**实现**，很薄但很关键。
 *   它只做三件事：
 *     1) 管理 “属性聚合体” bgp_advertise_attr 的生命周期（哈希复用 + refcnt），
 *        使“属性相同的一批前缀”能共用一次编码——这是 update-group 打包的基础；
 *     2) 管理出向待办项 bgp_advertise 的分配/释放（含对 bgp_path_info 的引用计数）；
 *     3) 维护入向记录 bgp_adj_in（建立/更新/摘除），供 soft-reconfig 使用。
 *
 * 与谁配合：
 *   - 主要调用方是 bgp_updgrp_adv.c（bgp_adj_out_set/unset 里创建/清理 adv，
 *     见 bgp_advertise_clean_subgroup → bgp_advertise_attr_unintern）。
 *   - 哈希表 subgrp->hash 是 per-update_subgroup 的（不是全局），所以“属性复用”
 *     的范围是本更新子组内部——这点容易误解，注意。
 *   - bgp_adj_in_* 主要由 bgp_route.c 的 UPDATE 处理路径调用。
 *
 * 引用计数约定（阅读时的关键）：
 *   - bgp_advertise->pathi : 持一个 bgp_path_info 引用（bgp_advertise_free 释放）。
 *   - bgp_adj_in->peer     : 持一个 peer 引用（remove 时释放）。
 *   - bgp_advertise_attr   : refcnt = 共享该属性的待办项(adv)个数；其 attr 指针
 *                            的那一份引用由调用方 bgp_attr_intern() 时“赠送”进来。
 *   - bgp_dest 的锁        : 每建一条 adj_in 就 bgp_dest_lock_node 一次，
 *                            摘除时 unlock（注意 bgp_adj_in_remove 会把 dest 指针
 *                            作为出参传出，因为 unlock 可能释放掉这个节点）。
 *
 * 建议阅读顺序：bgp_advertise_attr_intern/unintern（先懂引用计数）
 *               → bgp_advertise_new/free → bgp_adj_in_set/remove/unset
 *               → bgp_adj_out_lookup（查询语义）。
 * ============================================================================
 */

#include <zebra.h>

#include "command.h"
#include "memory.h"
#include "prefix.h"
#include "hash.h"
#include "frrevent.h"
#include "queue.h"
#include "filter.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_advertise.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_packet.h"
#include "bgpd/bgp_fsm.h"
#include "bgpd/bgp_mplsvpn.h"
#include "bgpd/bgp_updgrp.h"

/* ========== 1. 属性聚合体 bgp_advertise_attr 的分配与哈希复用 ========== */
/* 【函数】bgp_advertise_attr_new —— 裸分配一个聚合体（未挂哈希、未计数）。
 * 只做 XCALLOC，attr/fifo/refcnt 之后由调用方或 hash_alloc 填。 */
/* BGP advertise attribute is used for pack same attribute update into
   one packet.  To do that we maintain attribute hash in struct
   peer.  */
struct bgp_advertise_attr *bgp_advertise_attr_new(void)
{
	return XCALLOC(MTYPE_BGP_ADVERTISE_ATTR,
		       sizeof(struct bgp_advertise_attr));
}

/* 【函数】bgp_advertise_attr_free —— 释放聚合体本身。
 * 先把内部 FIFO(挂着的 adv 链)收尾，再释放内存。
 * 注意：**不释放 baa->attr**（那是调用方的引用，由 unintern 负责），也不从哈希表
 * 摘除（那是 hash_release 的事）——只在 unintern 里被调用。 */
void bgp_advertise_attr_free(struct bgp_advertise_attr *baa)
{
	bgp_advertise_attr_fifo_fini(&baa->fifo);

	XFREE(MTYPE_BGP_ADVERTISE_ATTR, baa);
}

/* 【函数】bgp_advertise_attr_hash_alloc —— hash_get 未命中时的回调：真正建对象。
 * 参数 p 是调用方在栈上构造的临时 ref（只填了 attr 字段），这里照抄其 attr、
 * 初始化 FIFO，返回新对象交给哈希表持有。
 * 注意：此处**不对 attr 再加引用**——那份引用由调用方（bgp_adj_out_set 里
 * bgp_attr_intern 出来的 attr_new）“赠送”，baa 负责在 unintern 时归还，
 * 所以每个 baa 恰好持有一份 attr 引用。 */
static void *bgp_advertise_attr_hash_alloc(void *p)
{
	struct bgp_advertise_attr *ref = (struct bgp_advertise_attr *)p;
	struct bgp_advertise_attr *baa;

	baa = bgp_advertise_attr_new();
	baa->attr = ref->attr;

	bgp_advertise_attr_fifo_init(&baa->fifo);

	return baa;
}

/* 【函数】bgp_advertise_attr_hash_key —— 哈希键。
 * 直接借用属性自身的哈希函数 attrhash_key_make(baa->attr)：因为“同一个 interned
 * attr”就代表“同一份属性”，复用它既能保证键一致，也省去自造哈希。 */
unsigned int bgp_advertise_attr_hash_key(const void *p)
{
	const struct bgp_advertise_attr *baa = p;

	return attrhash_key_make(baa->attr);
}

/* 【函数】bgp_advertise_attr_hash_cmp —— 哈希比较。
 * 同样借用 attrhash_cmp(attr, attr) 做属性等价判断；等价 ⇒ 两 baa 可合并。
 * 由 key/cmp 都建立在 interned attr 之上可知：本哈希表实际是“按属性去重”。 */
bool bgp_advertise_attr_hash_cmp(const void *p1, const void *p2)
{
	const struct bgp_advertise_attr *baa1 = p1;
	const struct bgp_advertise_attr *baa2 = p2;

	return attrhash_cmp(baa1->attr, baa2->attr);
}

/* ========== 2. 待办项 bgp_advertise 的分配/释放/挂链 ========== */
/* 【函数】bgp_advertise_new —— 裸分配一条待办项（未填 dest/pathi/baa/adj）。
 * 调用方 bgp_adj_out_set() 负责随后填字段并把它挂到 baa 的 FIFO 上。 */
/* BGP update and withdraw information is stored in BGP advertise
   structure.  This structure is referred from BGP adjacency
   information.  */
struct bgp_advertise *bgp_advertise_new(void)
{
	return XCALLOC(MTYPE_BGP_ADVERTISE, sizeof(struct bgp_advertise));
}

/* 【函数】bgp_advertise_free —— 释放一条待办项。
 * 关键：若还持有 pathi，必须先 bgp_path_info_unlock 归还引用，否则该路径对象
 * 泄漏（引用计数不归零）。注意它**不**负责从 baa FIFO / subgroup FIFO 摘链，
 * 摘链由调用方先做（bgp_advertise_delete / bgp_adv_fifo_del）。 */
void bgp_advertise_free(struct bgp_advertise *adv)
{
	if (adv->pathi)
		/* bgp_advertise bgp_path_info reference */
		bgp_path_info_unlock(adv->pathi);
	XFREE(MTYPE_BGP_ADVERTISE, adv);
}

/* 【函数】bgp_advertise_add —— 把待办项追加到“同属性”聚合体 baa 的 FIFO 尾部。
 * 这条链就是组包时“一批同属性前缀”的枚举来源（见 bgp_updgrp_packet.c 打包逻辑）。 */
void bgp_advertise_add(struct bgp_advertise_attr *baa,
		       struct bgp_advertise *adv)
{
	bgp_advertise_attr_fifo_add_tail(&baa->fifo, adv);
}

/* 【函数】bgp_advertise_delete —— 从 baa 的 FIFO 摘除一条待办项（只摘链，不释放）。
 * 典型调用：bgp_advertise_clean_subgroup() 在撤换某前缀的待办项时先摘链。 */
void bgp_advertise_delete(struct bgp_advertise_attr *baa,
			  struct bgp_advertise *adv)
{
	bgp_advertise_attr_fifo_del(&baa->fifo, adv);
}

/* ========== 3. 属性聚合体的 intern / unintern（引用计数核心） ========== */
/* 【函数】bgp_advertise_attr_intern —— 按属性在内 subgrp->hash 中“查或建”聚合体，
 * 并把 refcnt 加一。返回的 baa 由调用方持有（存入 adv->baa）。
 *
 * 调用约定（重要）：传入的 attr **必须已经是 interned 的**（源码注释 "The attr is
 * already intern'ed"），caller 在 bgp_adj_out_set() 里先用 bgp_attr_intern() 得到
 * attr_new 再传进来。命中已有 baa 时，本次调用只是 refcnt++，**不会**再多持一份
 * attr 引用——所以每个 baa 始终只对应“一份” attr 引用。 */
struct bgp_advertise_attr *bgp_advertise_attr_intern(struct hash *hash,
						     struct attr *attr)
{
	struct bgp_advertise_attr ref;
	struct bgp_advertise_attr *baa;

	/* The attr is already intern'ed */
	ref.attr = attr;
	baa = (struct bgp_advertise_attr *)hash_get(
		hash, &ref, bgp_advertise_attr_hash_alloc);
	baa->refcnt++;

	return baa;
}

/* 【函数】bgp_advertise_attr_unintern —— 归还一次引用（每次摘除一个 adv 调用一次）。
 * 语义：refcnt 先减一；若仍有人用则只归还属性引用，否则从哈希表摘除并释放对象。
 *
 * 注意（存疑点，未改动代码，仅供阅读时留意）：refcnt>0 的分支会立即调用
 * bgp_attr_unintern(&baa->attr)。而 baa 只持有一份 attr 引用（见 intern 的约定），
 * 此时 baa 仍留在哈希表里、adv->baa 仍被其它待办项引用，却已经把身下那份属性引用
 * 还掉了。也就是说“一个 baa 被多个 adv 共享”时，第一次摘除就把属性引用释放了。
 * 由于 bgp_attr_unintern() 仅在 refcount 归零时才把指针置 NULL，运行期未必立刻暴露，
 * 但这份账目与 intern 时的约定并不对称，值得实测确认（例如同一属性下挂 2 个前缀，
 * 摘除其一后观察 attrhash 计数与 baa->attr 的引用计数）。 */
void bgp_advertise_attr_unintern(struct hash *hash,
				 struct bgp_advertise_attr *baa)
{
	if (baa->refcnt)
		baa->refcnt--;

	if (baa->refcnt && baa->attr)
		bgp_attr_unintern(&baa->attr);
	else {
		if (baa->attr) {
			hash_release(hash, baa);
			bgp_attr_unintern(&baa->attr);
		}
		bgp_advertise_attr_free(baa);
	}
}

/* ========== 4. 出向查询：bgp_adj_out_lookup ========== */
/* 【函数】bgp_adj_out_lookup —— 判断“本端是否已把该前缀通告给了该 peer”。
 *
 * 参数：peer 目标邻居；dest 前缀；addpath_tx_id 指定的 Add-Path 路径 ID
 *       （0 表示不限定路径）。
 * 返回：true = 该 peer 所属 subgroup 上已有对该前缀的通告记录。
 * 用途：源头去重（如 network 语句重下发、策略重算后避免重复通告）、以及决定
 *       撤回是否需要真正发出去。
 *
 * 实现要点（读的时候容易漏）：
 *  - dest->adj_out 是【按 subgroup 索引】的红黑树，而本函数要按 peer 找，所以
 *    外层遍历 adj_out，内层用 SUBGRP_FOREACH_PEER 展开该 subgroup 的所有 peer，
 *    命中 paf->peer == peer 才算找到。**一个 peer 只属于一个 subgroup**，
 *    因此命中即可返回，无需继续。
 *  - Add-Path 的匹配条件：只有该 peer 在 enable-tx 方向真的启用了 addpath
 *    （bgp_addpath_encode_tx）且调用方传了非 0 的 addpath_tx_id 时，才按
 *    adj->addpath_tx_id 精确匹配；否则忽略路径 ID（即“任一条路径存在即可”）。
 *    这个条件判断的顺序容易写反，注意 addpath_capable && addpath_tx_id 同时成立。
 *  - 返回值取的是 adj->adv（有待办项）优先、否则 adj->attr（已通告过的属性）：
 *    两者有一个非空就等于“通告过”。其中 adv 分支还要求 adv->baa 非空。 */
bool bgp_adj_out_lookup(struct peer *peer, struct bgp_dest *dest,
			uint32_t addpath_tx_id)
{
	struct bgp_adj_out *adj;
	struct peer_af *paf;
	afi_t afi;
	safi_t safi;
	bool addpath_capable;

	RB_FOREACH (adj, bgp_adj_out_rb, &dest->adj_out)
		SUBGRP_FOREACH_PEER (adj->subgroup, paf)
			if (paf->peer == peer) {
				afi = SUBGRP_AFI(adj->subgroup);
				safi = SUBGRP_SAFI(adj->subgroup);
				addpath_capable =
					bgp_addpath_encode_tx(peer, afi, safi);

				/* Match on a specific addpath_tx_id if we are
				 * using addpath for
				 * this
				 * peer and if an addpath_tx_id was specified */
				if (addpath_capable && addpath_tx_id
				    && adj->addpath_tx_id != addpath_tx_id)
					continue;

				return (adj->adv
						? (adj->adv->baa ? true : false)
						: (adj->attr ? true : false));
			}

	return false;
}


/* ========== 5. 入向记录 bgp_adj_in 的建立 / 更新 / 摘除 ========== */
/* 【函数】bgp_adj_in_set —— 记录“peer 通告了(或再次通告了)前缀 dest”。
 *
 * 调用时机：处理对端 UPDATE 时，凡本端存储下来(new/new-addpath/best 等)的路径，
 *           都会建一条入向记录（受 soft-reconfig / addpath 配置影响）。
 * 语义：一条记录 = (peer, addpath_rx_id) 唯一。
 *   - 已存在同 (peer, addpath_id) 的记录 → **就地更新**：属性/标签变了才换来换去
 *     （先 unintern 旧的再 intern 新的，保持 interned 对象只读、不就地改）；
 *     属性与标签都没变则直接 return，避免无谓的引用计数抖动。
 *   - 不存在 → 新建并头插到 dest->adj_in 链表。
 *
 * 引用计数副作用（漏掉会泄漏/提前释放）：
 *   - peer_lock(peer)：该记录持有一个 peer 引用，由 bgp_adj_in_remove 归还。
 *   - bgp_attr_intern / bgp_labels_intern：持有属性与标签引用，remove 时归还。
 *   - bgp_dest_lock_node(dest)：每建一条记录就把前缀节点锁加一；因为同 (peer,id)
 *     只会有一条，所以“记录数 = 锁增量”，remove 时一一对应解锁。
 *   - peer->stat_pfx_adj_rib_in++：show 命令里的 Adj-RIB-In 计数。 */
void bgp_adj_in_set(struct bgp_dest *dest, struct peer *peer, struct attr *attr,
		    uint32_t addpath_id, struct bgp_labels *labels)
{
	struct bgp_adj_in *adj;

	for (adj = dest->adj_in; adj; adj = adj->next) {
		if (adj->peer == peer && adj->addpath_rx_id == addpath_id) {
			if (!attrhash_cmp(adj->attr, attr)) {
				bgp_attr_unintern(&adj->attr);
				adj->attr = bgp_attr_intern(attr);
			}
			if (!bgp_labels_cmp(adj->labels, labels)) {
				bgp_labels_unintern(&adj->labels);
				adj->labels = bgp_labels_intern(labels);
			}
			return;
		}
	}
	adj = XCALLOC(MTYPE_BGP_ADJ_IN, sizeof(struct bgp_adj_in));
	adj->peer = peer_lock(peer); /* adj_in peer reference */
	adj->attr = bgp_attr_intern(attr);
	adj->uptime = monotime(NULL);
	adj->addpath_rx_id = addpath_id;
	adj->labels = bgp_labels_intern(labels);
	BGP_ADJ_IN_ADD(dest, adj);
	peer->stat_pfx_adj_rib_in++;
	bgp_dest_lock_node(dest);
}

/* 【函数】bgp_adj_in_remove —— 摘除一条入向记录并释放其全部引用（set 的逆操作）。
 *
 * 参数用 **dest（二级指针）的原因（重点，容易看不懂）：末尾
 * bgp_dest_unlock_node() 会把 dest 的锁减一，**若锁归零该节点会被释放**，
 * 函数必须把“可能已失效”的新指针回写给调用方，避免调用方拿着悬垂指针继续用。
 * 因此调用方（bgp_adj_in_unset）每次摘除后都要 assert(*dest) 重新取用。
 *
 * 释放顺序：属性/标签 unintern → 统计减一 → 链表摘钩 → dest 解锁 → peer 解锁 → 释放本体。
 * 其中 peer->stat_pfx_adj_rib_in-- 与 set 里的 ++ 严格配对。 */
void bgp_adj_in_remove(struct bgp_dest **dest, struct bgp_adj_in *bai)
{
	bgp_attr_unintern(&bai->attr);
	bgp_labels_unintern(&bai->labels);
	if (bai->peer)
		bai->peer->stat_pfx_adj_rib_in--;
	BGP_ADJ_IN_DEL(*dest, bai);
	*dest = bgp_dest_unlock_node(*dest);
	peer_unlock(bai->peer); /* adj_in peer reference */
	XFREE(MTYPE_BGP_ADJ_IN, bai);
}

/* 【函数】bgp_adj_in_unset —— 按 (peer, addpath_id) 删除该前缀上的入向记录。
 *
 * 调用时机：收到该 peer 对该前缀的撤回(WITHDRAW)、或 peer 关闭/重置时清理。
 * 返回：true = 前缀上原本存在 adj_in 链表（不代表一定删掉了匹配项，只表示表非空）；
 *       false = 本来就没有任何入向记录。
 *
 * 实现要点：
 *  - 先保存 adj_next 再删除当前节点：bgp_adj_in_remove 会 free 掉 adj，不能再用
 *    adj->next。这是最典型的“删链表节点要先存后继”写法。
 *  - 循环尾部 assert(*dest)：因为 remove 可能释放并回写 dest 指针，这里做防御性
 *    校验（发布版 ASSERT 被关掉，调试版会捕捉到异常）。
 *  - 注意：匹配项可能不止一条吗？addpath_id 限定了唯一性，但同一 peer 若未启用
 *    addpath（addpath_id 恒等）也不会有重复，故实际最多删一条；写成 while 是为了
 *    兼容历史上可能存在的重复项。 */
bool bgp_adj_in_unset(struct bgp_dest **dest, struct peer *peer,
		      uint32_t addpath_id)
{
	struct bgp_adj_in *adj;
	struct bgp_adj_in *adj_next;

	adj = (*dest)->adj_in;

	if (!adj)
		return false;

	while (adj) {
		adj_next = adj->next;

		if (adj->peer == peer && adj->addpath_rx_id == addpath_id)
			bgp_adj_in_remove(dest, adj);

		adj = adj_next;

		assert(*dest);
	}

	return true;
}
