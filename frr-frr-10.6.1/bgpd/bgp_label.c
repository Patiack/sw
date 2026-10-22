// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP carrying label information
 * Copyright (C) 2013 Cumulus Networks, Inc.
 */

/*
 * ============================================================================
 * 【文件导读】bgp_label.c —— BGP 标签的编解码、共享(intern)、申请/注册与 NLRI 解析
 * ============================================================================
 * 本文件是 bgp_label.h 的实现。它围绕三件事展开：
 *
 * 1) 标签对象的 interned 共享（bgp_labels_hash_* / bgp_labels_intern /
 *    bgp_labels_unintern / bgp_labels_cmp）。与 bgp_attr 一样，内容相同的标签栈
 *    全局只保留一份，靠 refcnt 复用，避免为每条路由各复制一遍标签数组。
 *
 * 2) 本地出标签的申请/释放/注册链路（bgp_reg_dereg_for_label /
 *    bgp_reg_for_label_callback / bgp_send_fec_register_label_msg /
 *    bgp_parse_fec_update）。标签有两种来源：
 *      a. 由 zebra 的标签管理器按 FEC(转发等价类) 分配——走 ZEBRA_FEC_REGISTER；
 *      b. 由本机标签池 labelpool 分配——走 bgp_lp_get()，是异步的。
 *    SR(Segment Routing) 场景还可用 attr 里的 label_index 让 zebra 推导标签。
 *
 * 3) 报文里标签栈的编解码与 NLRI 解析（bgp_nlri_get_labels /
 *    bgp_nlri_parse_label / decode_label / encode_label*）。
 *
 * 【必须记住的线上格式】单个标签 3 字节(24bit)：20 位 label + 3 位 TC + 1 位 BOS，
 * 详见 bgp_label.h。FRR 借用 TC 区里的 0x02 位当“有效”内部标记。
 *
 * 【建议阅读顺序】
 *   bgp_labels_* (共享族) → bgp_adv_label (决定通告哪个标签) →
 *   bgp_reg_dereg_for_label → bgp_reg_for_label_callback →
 *   bgp_send_fec_register_label_msg → bgp_parse_fec_update (zebra 方向) →
 *   bgp_nlri_get_labels → bgp_nlri_parse_label (收包解析) →
 *   末尾 decode_label / encode_label* / mpls_labels2str。
 * ============================================================================
 */

#include <zebra.h>

#include "command.h"
#include "frrevent.h"
#include "prefix.h"
#include "zclient.h"
#include "stream.h"
#include "network.h"
#include "log.h"
#include "memory.h"
#include "nexthop.h"
#include "mpls.h"
#include "jhash.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_label.h"
#include "bgpd/bgp_packet.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_errors.h"

/* 指向 bgpd 与 zebra 之间的 zclient 连接；FEC 注册、标签池申请等消息都靠它发出。 */
extern struct zclient *bgp_zclient;


/* 【全局】标签对象共享哈希表：key 为标签栈内容，value 为 struct bgp_labels。
 * 由 bgp_labels_init() 建立、bgp_labels_finish() 销毁。 */
/* MPLS Labels hash routines. */
static struct hash *labels_hash;

/* 【函数】bgp_labels_hash_alloc —— hash_get 的“新建”回调：当哈希表里查不到匹配项时，
 * 由它分配并填充一个新的 struct bgp_labels。这里只拷贝标签内容，refcnt 不在此初始化
 * （由 bgp_labels_intern 负责递增）。这是 FRR 哈希表管理对象生命周期的标准套路。 */
static void *bgp_labels_hash_alloc(void *p)
{
	const struct bgp_labels *labels = p;
	struct bgp_labels *new;
	uint8_t i;

	new = XCALLOC(MTYPE_BGP_LABELS, sizeof(struct bgp_labels));

	new->num_labels = labels->num_labels;
	for (i = 0; i < labels->num_labels; i++)
		new->label[i] = labels->label[i];

	return new;
}

/* 【函数】bgp_labels_hash_key_make —— 计算哈希键：对标签数组做 jhash。
 * 注意 num_labels==0 时键取 0（空标签不进哈希，见 bgp_labels_intern）。 */
static uint32_t bgp_labels_hash_key_make(const void *p)
{
	const struct bgp_labels *labels = p;
	uint32_t key = 0;

	if (labels->num_labels)
		key = jhash(&labels->label,
			    labels->num_labels * sizeof(mpls_label_t), key);

	return key;
}

/* 【函数】bgp_labels_hash_cmp —— 哈希表比较回调，直接复用 bgp_labels_cmp。 */
static bool bgp_labels_hash_cmp(const void *p1, const void *p2)
{
	return bgp_labels_cmp(p1, p2);
}

/* 【函数】bgp_labels_init —— 进程启动时建立标签对象的共享哈希表。 */
void bgp_labels_init(void)
{
	labels_hash = hash_create(bgp_labels_hash_key_make, bgp_labels_hash_cmp,
				  "BGP Labels hash");
}

/* 【函数】bgp_labels_free —— 供哈希表清理使用的删除回调，只负责释放内存。 */
/*
 * special for hash_clean below
 */
static void bgp_labels_free(void *labels)
{
	XFREE(MTYPE_BGP_LABELS, labels);
}

/* 【函数】bgp_labels_finish —— 进程退出时清空并释放共享哈希表。 */
void bgp_labels_finish(void)
{
	hash_clean_and_free(&labels_hash, bgp_labels_free);
}

/* 【函数】bgp_labels_intern —— 按内容查表复用标签对象（与 bgp_attr_intern 同构）：
 *   - 入参为 NULL 或 num_labels==0 时直接返回 NULL（不 intern 空标签栈）；
 *   - 否则查共享哈希表：命中则 refcnt++，未命中则由 bgp_labels_hash_alloc 建新对象，
 *     再 refcnt++。
 * 返回值是全局共享对象，调用方拿到后应视其为只读；要“改”必须新建一个再 intern。 */
struct bgp_labels *bgp_labels_intern(struct bgp_labels *labels)
{
	struct bgp_labels *find;

	if (!labels)
		return NULL;

	if (!labels->num_labels)
		/* do not intern void labels structure */
		return NULL;

	find = (struct bgp_labels *)hash_get(labels_hash, labels,
					     bgp_labels_hash_alloc);
	find->refcnt++;

	return find;
}

/* 【函数】bgp_labels_unintern —— 引用计数减一；归零则从哈希表移除并释放对象，
 * 同时把调用方指针置 NULL（所以入参是二级指针）。 */
void bgp_labels_unintern(struct bgp_labels **plabels)
{
	struct bgp_labels *labels = *plabels;
	struct bgp_labels *ret;

	if (!*plabels)
		return;

	/* Decrement labels reference. */
	labels->refcnt--;

	/* If reference becomes zero then free labels object. */
	if (labels->refcnt == 0) {
		ret = hash_release(labels_hash, labels);
		assert(ret != NULL);
		bgp_labels_free(labels);
		*plabels = NULL;
	}
}

/* 【函数】bgp_labels_cmp —— 内容比较：先比标签个数，再逐个比标签值。
 * 两个都为 NULL 视为相等（都为空）；只有一个为 NULL 则不等。
 * 既用作哈希表的等值判断，也被上层用来判断两个标签栈是否相同。 */
bool bgp_labels_cmp(const struct bgp_labels *labels1,
		    const struct bgp_labels *labels2)
{
	uint8_t i;

	if (!labels1 && !labels2)
		return true;

	if (!labels1 && labels2)
		return false;

	if (labels1 && !labels2)
		return false;

	if (labels1->num_labels != labels2->num_labels)
		return false;

	for (i = 0; i < labels1->num_labels; i++) {
		if (labels1->label[i] != labels2->label[i])
			return false;
	}

	return true;
}

/* 【函数】bgp_parse_fec_update —— 处理 zebra 主动发来的 FEC(转发等价类)更新消息：
 * 某个前缀在本机的出标签变了。流程：
 *   1) 从 zclient 输入流按 (family, prefixlen, prefix, label) 解出内容；
 *   2) 在默认 bgp 实例的 unicast RIB 里查找到对应节点 dest；
 *   3) 更新 dest->local_label：zebra 给的标签有效则编码写入并 bgp_set_valid_label()；
 *      若为 MPLS_INVALID_LABEL 则视为“隐式撤销”，清掉有效标记；
 *   4) 打上 BGP_NODE_LABEL_CHANGED 标志并触发 bgp_process 重新处理该路由。
 * 疑点：这里硬编码只查默认实例 + SAFI_UNICAST（见代码里 “hack for the bgp instance &
 * SAFI” 注释），所以只覆盖 IPv4/IPv6 单播表；多实例 / 其他 SAFI 的 FEC 更新会被丢弃。 */
int bgp_parse_fec_update(void)
{
	struct stream *s;
	struct bgp_dest *dest;
	struct bgp *bgp;
	struct bgp_table *table;
	struct prefix p;
	uint32_t label;
	afi_t afi;
	safi_t safi;

	s = bgp_zclient->ibuf;

	memset(&p, 0, sizeof(p));
	p.family = stream_getw(s);
	p.prefixlen = stream_getc(s);
	stream_get(p.u.val, s, PSIZE(p.prefixlen));
	label = stream_getl(s);

	/* hack for the bgp instance & SAFI = have to send/receive it */
	afi = family2afi(p.family);
	safi = SAFI_UNICAST;
	bgp = bgp_get_default();
	if (!bgp) {
		zlog_debug("no default bgp instance");
		return -1;
	}

	table = bgp->rib[afi][safi];
	if (!table) {
		zlog_debug("no %u unicast table", p.family);
		return -1;
	}
	dest = bgp_node_lookup(table, &p);
	if (!dest) {
		zlog_debug("no node for the prefix");
		return -1;
	}

	/* treat it as implicit withdraw - the label is invalid */
	if (label == MPLS_INVALID_LABEL)
		bgp_unset_valid_label(&dest->local_label);
	else {
		dest->local_label = mpls_lse_encode(label, 0, 0, 1);
		bgp_set_valid_label(&dest->local_label);
	}
	SET_FLAG(dest->flags, BGP_NODE_LABEL_CHANGED);
	bgp_process(bgp, dest, NULL, afi, safi);
	bgp_dest_unlock_node(dest);
	return 1;
}

/* 【函数】bgp_adv_label —— 决定“向对端 to 通告时，这条路由携带哪个标签”，并把结果
 * 写进调用方提供的 labels / num_labels 缓冲。
 * 决策依据 use_local_label：
 *   - 默认使用本端标签 dest->local_label（每个前缀一个出标签）；
 *   - 但在下面两种情形改用路径自带的标签栈(pi->extra->labels)：
 *       a. 该路由从 iBGP 学来、又要转发给 iBGP(即 reflect 反射)，且对端未强制
 *          next-hop-self（PEER_FLAG_FORCE_NEXTHOP_SELF）；
 *       b. 对端配置了 PEER_FLAG_NEXTHOP_UNCHANGED（下一跳不变）。
 *   这两类情形下本端不重写标签（标签来自原始通告者），以免破坏端到端标签路径。
 * dest / pi / to 任一为空时输出 0 个标签并返回。 */
void bgp_adv_label(struct bgp_dest *dest, struct bgp_path_info *pi, struct peer *to, afi_t afi,
		   safi_t safi, mpls_label_t *labels, uint8_t *num_labels)
{
	struct peer *from;
	int reflect;
	uint8_t i;
	bool use_local_label = true;

	labels[0] = MPLS_INVALID_LABEL;
	if (!dest || !pi || !to) {
		*num_labels = 0;
		return;
	}

	from = pi->peer;
	reflect =
		((from->sort == BGP_PEER_IBGP) && (to->sort == BGP_PEER_IBGP));

	if (reflect
	    && !CHECK_FLAG(to->af_flags[afi][safi],
			   PEER_FLAG_FORCE_NEXTHOP_SELF))
		use_local_label = false;

	if (CHECK_FLAG(to->af_flags[afi][safi], PEER_FLAG_NEXTHOP_UNCHANGED))
		use_local_label = false;

	if (use_local_label) {
		*num_labels = 1;
		labels[0] = dest->local_label;
	} else {
		*num_labels = BGP_PATH_INFO_NUM_LABELS(pi);
		for (i = 0; i < *num_labels; i++)
			labels[i] = pi->extra->labels->label[i];
	}
}

/* 【函数】bgp_send_fec_register_label_msg —— 向 zebra 发送 FEC 注册/注销消息
 * (ZEBRA_FEC_REGISTER / ZEBRA_FEC_UNREGISTER)，把“前缀 → 本地出标签”的映射告诉
 * zebra，由它下发给转发面做 MPLS 转发。
 *   reg=true  注册：标签来源优先级为 label_index(若给定) > 已算出的有效标签 >
 *             都不给(交给 zebra 自行分配)。分别对应 flags 里的
 *             ZEBRA_FEC_REGISTER_LABEL_INDEX / ZEBRA_FEC_REGISTER_LABEL。
 *   reg=false 注销：清掉 BGP_NODE_REGISTERED_FOR_LABEL 标志。
 * 注意 flags 字段先占位（记录 flags_pos）后回填，且只有注册时才写新 flags。
 * 发送前先检查 socket 是否可用；并注意 SR 场景下 label_index 优先于本地标签。 */
static void bgp_send_fec_register_label_msg(struct bgp_dest *dest, bool reg,
					    uint32_t label_index)
{
	struct stream *s;
	int command;
	const struct prefix *p;
	uint16_t flags = 0;
	size_t flags_pos = 0;
	mpls_label_t *local_label = &(dest->local_label);
	uint32_t ttl = 0;
	uint32_t bos = 0;
	uint32_t exp = 0;
	mpls_label_t label = MPLS_INVALID_LABEL;
	bool have_label_to_reg;

	mpls_lse_decode(*local_label, &label, &ttl, &exp, &bos);

	have_label_to_reg = bgp_is_valid_label(local_label) &&
			    label != MPLS_LABEL_IMPLICIT_NULL;

	p = bgp_dest_get_prefix(dest);

	/* Check socket. */
	if (!bgp_zclient || bgp_zclient->sock < 0)
		return;

	if (BGP_DEBUG(labelpool, LABELPOOL))
		zlog_debug("%s: FEC %sregister %pBD label_index=%u label=%u",
			   __func__, reg ? "" : "un", dest, label_index, label);
	/* If the route node has a local_label assigned or the
	 * path node has an MPLS SR label index allowing zebra to
	 * derive the label, proceed with registration. */
	s = bgp_zclient->obuf;
	stream_reset(s);
	command = (reg) ? ZEBRA_FEC_REGISTER : ZEBRA_FEC_UNREGISTER;
	zclient_create_header(s, command, VRF_DEFAULT);
	flags_pos = stream_get_endp(s); /* save position of 'flags' */
	stream_putw(s, flags);		/* initial flags */
	stream_putw(s, PREFIX_FAMILY(p));
	stream_put_prefix(s, p);
	if (reg) {
		/* label index takes precedence over auto-assigned label. */
		if (label_index != 0) {
			SET_FLAG(flags, ZEBRA_FEC_REGISTER_LABEL_INDEX);
			stream_putl(s, label_index);
		} else if (have_label_to_reg) {
			SET_FLAG(flags, ZEBRA_FEC_REGISTER_LABEL);
			stream_putl(s, label);
		}
		SET_FLAG(dest->flags, BGP_NODE_REGISTERED_FOR_LABEL);
	} else
		UNSET_FLAG(dest->flags, BGP_NODE_REGISTERED_FOR_LABEL);

	/* Set length and flags */
	stream_putw_at(s, 0, stream_get_endp(s));

	/*
	 * We only need to write new flags if this is a register
	 */
	if (reg)
		stream_putw_at(s, flags_pos, flags);

	zclient_send_message(bgp_zclient);
}

/**
 * This is passed as the callback function to bgp_labelpool.c:bgp_lp_get()
 * by bgp_reg_dereg_for_label() when a label needs to be obtained from
 * label pool.
 * Note that it will reject the allocated label if a label index is found,
 * because the label index supposes predictable labels
 */
/* 【函数】bgp_reg_for_label_callback —— 由标签池(bgp_labelpool)在标签分配完成后回调
 * （在 bgp_reg_dereg_for_label 里通过 bgp_lp_get 注册）。要点：
 *   - 若此时路由已不再需要标签（BGP_NODE_LABEL_REQUESTED 已清），拒绝这次分配：
 *     返回 -1 并解锁节点，标签池会回收该标签；因为 label_index 可能已接管。
 *   - allocated=false 表示原先的标签失效：先把 local_label 置为隐式空标签
 *     (MPLS_LABEL_IMPLICIT_NULL)，等新标签到来时再替换。
 *   - 否则用新标签编码写回 local_label 并标为有效，最后重新登记 FEC。 */
int bgp_reg_for_label_callback(mpls_label_t new_label, void *labelid,
			       bool allocated)
{
	struct bgp_dest *dest;

	dest = labelid;

	/*
	 * if the route had been removed or the request has gone then reject
	 * the allocated label. The requesting code will have done what is
	 * required to allocate the correct label
	 */
	if (!CHECK_FLAG(dest->flags, BGP_NODE_LABEL_REQUESTED)) {
		bgp_dest_unlock_node(dest);
		return -1;
	}

	dest = bgp_dest_unlock_node(dest);
	assert(dest);

	if (BGP_DEBUG(labelpool, LABELPOOL))
		zlog_debug("%s: FEC %pBD label=%u, allocated=%d", __func__,
			   dest, new_label, allocated);

	if (!allocated) {
		/*
		 * previously-allocated label is now invalid, set to implicit
		 * null until new label arrives
		 */
		if (CHECK_FLAG(dest->flags, BGP_NODE_REGISTERED_FOR_LABEL)) {
			UNSET_FLAG(dest->flags, BGP_NODE_LABEL_REQUESTED);
			dest->local_label = mpls_lse_encode(
				MPLS_LABEL_IMPLICIT_NULL, 0, 0, 1);
			bgp_set_valid_label(&dest->local_label);
		}
	}

	dest->local_label = mpls_lse_encode(new_label, 0, 0, 1);
	bgp_set_valid_label(&dest->local_label);

	/*
	 * Get back to registering the FEC
	 */
	bgp_send_fec_register_label_msg(dest, true, 0);

	return 0;
}

/* 【函数】bgp_reg_dereg_for_label —— 申请/注销“某路由在本机的出标签”的对外总入口，
 * 也就是 bgp_label.h 里 bgp_register/unregister_for_label 的真正实现。
 *   reg=true  申请：
 *       - 若 attr 带 Prefix-SID 且 label_index 有效，则让 zebra 按 label_index 推导
 *         标签（label_index 优先，可获得可预测标签），并清掉 LABEL_REQUESTED 标志；
 *       - 否则若当前还没有可用标签，就打上 BGP_NODE_LABEL_REQUESTED 标志、调用
 *         bgp_lp_get() 异步向标签池申请，随即 return——真正的 FEC 注册留到
 *         bgp_reg_for_label_callback 里做（保证不会带着无效标签去注册 FEC）。
 *   reg=false 注销：清标志并释放标签池中的标签(bgp_lu_lp_release)。
 * 若无需走标签池（已有标签或走 label_index 路径），直接发 FEC 注册消息。 */
void bgp_reg_dereg_for_label(struct bgp_dest *dest, struct bgp_path_info *pi,
			     bool reg)
{
	bool with_label_index = false;
	const struct prefix *p;
	bool have_label_to_reg;
	uint32_t ttl = 0;
	uint32_t bos = 0;
	uint32_t exp = 0;
	mpls_label_t label = MPLS_INVALID_LABEL;

	mpls_lse_decode(dest->local_label, &label, &ttl, &exp, &bos);

	have_label_to_reg = bgp_is_valid_label(&dest->local_label) &&
			    label != MPLS_LABEL_IMPLICIT_NULL;

	p = bgp_dest_get_prefix(dest);

	if (BGP_DEBUG(labelpool, LABELPOOL))
		zlog_debug("%s: %pFX: %s ", __func__, p,
			   (reg ? "reg" : "dereg"));

	if (reg) {
		assert(pi);
		/*
		 * Determine if we will let zebra should derive label from
		 * label index instead of bgpd requesting from label pool
		 */
		if (bgp_attr_exists(pi->attr, BGP_ATTR_PREFIX_SID) &&
		    pi->attr->label_index != BGP_INVALID_LABEL_INDEX) {
			with_label_index = true;
			UNSET_FLAG(dest->flags, BGP_NODE_LABEL_REQUESTED);
		} else {
			/*
			 * If no label has been registered -- assume any label
			 * from label pool will do. This means that label index
			 * always takes precedence over auto-assigned labels.
			 */
			if (!have_label_to_reg) {
				SET_FLAG(dest->flags, BGP_NODE_LABEL_REQUESTED);
				struct bgp_table *table;

				if (BGP_DEBUG(labelpool, LABELPOOL))
					zlog_debug(
						"%s: Requesting label from LP for %pFX",
						__func__, p);
				/* bgp_reg_for_label_callback() will deal with
				 * fec registration when it gets a label from
				 * the pool. This means we'll never register
				 * FECs withoutvalid labels.
				 */
				table = bgp_dest_table(dest);

				bgp_lp_get(LP_TYPE_BGP_LU, dest, table->bgp->vrf_id,
					   bgp_reg_for_label_callback);
				return;
			}
		}
	} else {
		UNSET_FLAG(dest->flags, BGP_NODE_LABEL_REQUESTED);
		bgp_lu_lp_release(dest, label);
	}

	bgp_send_fec_register_label_msg(
		dest, reg, with_label_index ? pi->attr->label_index : 0);
}

/* Labels must have BGP_MAX_LABELS elements */
/* 【函数】bgp_nlri_get_labels —— 从一段 NLRI 数据里按 3 字节一步解析出标签栈。
 *   pnt / plen : 指向“标签 + 前缀”的这段数据及其比特长度；
 *   labels     : 输出缓冲，调用方必须保证至少 BGP_MAX_LABELS 个元素；
 *   num_labels_pnt : 输出实际解析到的标签个数。
 * 逻辑：
 *   - 每读一个标签，memcpy 到 labels[num_labels]，并 bgp_set_valid_label()（打内部有效标记）；
 *   - 遇到“撤销标签”或 BOS=1（栈底）就停止；
 *   - 最多只保留前 BGP_MAX_LABELS 个：超出的读入局部变量丢弃，并把最后一个保留标签
 *     强制置 BOS（把栈截断到合法深度）；
 *   - 若整段读完都没见到 BOS / 撤销标签，判定为非法标签栈，*num_labels_pnt=0 并返回 0。
 * 返回值为消耗的比特数 llen；调用方用 prefixlen-llen 得到真实前缀长度。
 * 注释：labels 必须够大是硬约束——本函数不检查 labels 容量。 */
static int bgp_nlri_get_labels(struct peer *peer, uint8_t *pnt, uint8_t plen, mpls_label_t *labels,
			       uint8_t *num_labels_pnt)
{
	uint8_t *data = pnt;
	uint8_t *lim = pnt + plen;
	uint8_t llen = 0;
	uint8_t label_depth = 0;
	uint8_t num_labels = 0;
	mpls_label_t label = MPLS_INVALID_LABEL;
	mpls_label_t *label_pnt = &label;

	if (plen < BGP_LABEL_BYTES)
		return 0;

	for (; (data + BGP_LABEL_BYTES) <= lim; data += BGP_LABEL_BYTES) {
		/*
		 * Support only BGP_MAX_LABELS, read rest to local variable and
		 * discard, shouldn't be possible - see comment to BGP_MAX_LABELS
		 */
		if (num_labels < BGP_MAX_LABELS)
			label_pnt = &labels[num_labels++];
		else
			label_pnt = &label;

		memcpy(label_pnt, data, BGP_LABEL_BYTES);
		llen += BGP_LABEL_BYTES;

		bgp_set_valid_label(label_pnt);

		label_depth += 1;

		if (bgp_is_withdraw_label(label_pnt) || label_bos(label_pnt))
			break;
	}

	*num_labels_pnt = num_labels;

	if (label_depth > BGP_MAX_LABELS) {
		label_set_bos(&labels[num_labels - 1]);
		zlog_info("%pBP rcvd UPDATE with label stack %d deep, using only first %d labels",
			  peer, label_depth, num_labels);
	}

	if (!(bgp_is_withdraw_label(label_pnt) || label_bos(label_pnt))) {
		flog_err(EC_BGP_INVALID_LABEL_STACK,
			 "%pBP rcvd UPDATE with invalid label stack - no bottom of stack", peer);
		*num_labels_pnt = 0;
		return 0;
	}

	return llen;
}

/* 【函数】bgp_nlri_parse_label —— BGP-LU / MPLS-VPN 等“带标签 NLRI”的收包解析入口
 * （与 bgp_nlri_parse_ipv4 同层次）。对每条 NLRI：
 *   1) 若该 peer/afi/safi 启用了 add-path，先跳过并取出 4 字节 addpath_id；
 *   2) 读 1 字节前缀长度 prefixlen；psize=PSIZE(prefixlen) 是“标签+前缀”总字节数；
 *   3) 调 bgp_nlri_get_labels 解出标签栈，得到消耗的比特数 llen；
 *   4) 真实前缀长度 = prefixlen - llen（比特）；从 pnt+llen 处拷贝前缀字节；
 *   5) 对 LABELED_UNICAST 做合法性检查：拒绝 IPv4 组播、IPv6 链路本地 / 组播地址
 *      （RFC4271：语义不合法的前缀本地记日志并忽略）；
 *   6) attr 非空走 bgp_update（更新），否则走 bgp_withdraw（撤销）。
 * 循环末尾校验恰好读完（pnt==lim），否则报“数据剩余”错误。返回解析结果码。 */
int bgp_nlri_parse_label(struct peer *peer, struct attr *attr,
			 struct bgp_nlri *packet)
{
	uint8_t *pnt;
	uint8_t *lim;
	struct prefix p;
	int psize = 0;
	int prefixlen;
	afi_t afi;
	safi_t safi;
	bool addpath_capable;
	uint32_t addpath_id;
	mpls_label_t labels[MPLS_MAX_LABELS] = { MPLS_INVALID_LABEL };
	uint8_t num_labels = 0;
	uint8_t llen;

	pnt = packet->nlri;
	lim = pnt + packet->length;
	afi = packet->afi;
	safi = packet->safi;
	addpath_id = 0;

	addpath_capable = bgp_addpath_encode_rx(peer, afi, safi);

	for (; pnt < lim; pnt += psize) {
		/* Clear prefix structure. */
		memset(&p, 0, sizeof(p));

		if (addpath_capable) {

			/* When packet overflow occurs return immediately. */
			if (pnt + BGP_ADDPATH_ID_LEN > lim)
				return BGP_NLRI_PARSE_ERROR_PACKET_OVERFLOW;

			memcpy(&addpath_id, pnt, BGP_ADDPATH_ID_LEN);
			addpath_id = ntohl(addpath_id);
			pnt += BGP_ADDPATH_ID_LEN;

			if (pnt >= lim)
				return BGP_NLRI_PARSE_ERROR_PACKET_OVERFLOW;
		}

		/* Fetch prefix length. */
		prefixlen = *pnt++;
		p.family = afi2family(packet->afi);
		psize = PSIZE(prefixlen);

		/* sanity check against packet data */
		if ((pnt + psize) > lim) {
			flog_err(
				EC_BGP_UPDATE_RCV,
				"%s [Error] Update packet error / L-U (prefix length %d exceeds packet size %u)",
				peer->host, prefixlen, (uint)(lim - pnt));
			return BGP_NLRI_PARSE_ERROR_PACKET_OVERFLOW;
		}

		/* Fill in the labels */
		llen = bgp_nlri_get_labels(peer, pnt, psize, labels, &num_labels);
		if (llen == 0) {
			flog_err(
				EC_BGP_UPDATE_RCV,
				"%s [Error] Update packet error (wrong label length 0)",
				peer->host);
			return BGP_NLRI_PARSE_ERROR_LABEL_LENGTH;
		}

		/* There needs to be at least one label */
		if (prefixlen < 24) {
			flog_err(EC_BGP_UPDATE_RCV,
				 "%s [Error] Update packet error (wrong label length %d)",
				 peer->host, prefixlen);
			return BGP_NLRI_PARSE_ERROR_LABEL_LENGTH;
		}

		p.prefixlen = prefixlen - BSIZE(llen);

		if ((afi == AFI_IP && p.prefixlen > IPV4_MAX_BITLEN)
		    || (afi == AFI_IP6 && p.prefixlen > IPV6_MAX_BITLEN))
			return BGP_NLRI_PARSE_ERROR_PREFIX_LENGTH;

		/* Fetch prefix from NLRI packet */
		memcpy(&p.u.prefix, pnt + llen, psize - llen);

		/* Check address. */
		if (afi == AFI_IP && safi == SAFI_LABELED_UNICAST) {
			if (IN_CLASSD(ntohl(p.u.prefix4.s_addr))) {
				/* From RFC4271 Section 6.3:
				 *
				 * If a prefix in the NLRI field is semantically
				 * incorrect
				 * (e.g., an unexpected multicast IP address),
				 * an error SHOULD
				 * be logged locally, and the prefix SHOULD be
				 * ignored.
				  */
				flog_err(
					EC_BGP_UPDATE_RCV,
					"%s: IPv4 labeled-unicast NLRI is multicast address %pI4, ignoring",
					peer->host, &p.u.prefix4);
				continue;
			}
		}

		/* Check address. */
		if (afi == AFI_IP6 && safi == SAFI_LABELED_UNICAST) {
			if (IN6_IS_ADDR_LINKLOCAL(&p.u.prefix6)) {
				flog_err(
					EC_BGP_UPDATE_RCV,
					"%s: IPv6 labeled-unicast NLRI is link-local address %pI6, ignoring",
					peer->host, &p.u.prefix6);

				continue;
			}

			if (IN6_IS_ADDR_MULTICAST(&p.u.prefix6)) {
				flog_err(
					EC_BGP_UPDATE_RCV,
					"%s: IPv6 unicast NLRI is multicast address %pI6, ignoring",
					peer->host, &p.u.prefix6);

				continue;
			}
		}

		if (attr) {
			bgp_update(peer, &p, addpath_id, attr, packet->afi, safi, ZEBRA_ROUTE_BGP,
				   BGP_ROUTE_NORMAL, NULL, labels, num_labels, 0, NULL);
		} else {
			bgp_withdraw(peer, &p, addpath_id, packet->afi, SAFI_UNICAST,
				     ZEBRA_ROUTE_BGP, BGP_ROUTE_NORMAL, NULL, labels, num_labels);
		}
	}

	/* Packet length consistency check. */
	if (pnt != lim) {
		flog_err(
			EC_BGP_UPDATE_RCV,
			"%s [Error] Update packet error / L-U (%td data remaining after parsing)",
			peer->host, lim - pnt);
		return BGP_NLRI_PARSE_ERROR_PACKET_LENGTH;
	}

	return BGP_NLRI_PARSE_OK;
}

/* 【函数】decode_label —— 把线上 3 字节标签解成本地 20 位数值。
 * 位拼接：byte0 左移 12、byte1 左移 4、byte2 取高 4 位(bit7..4)再右移 4。
 * 结果只含 20 位 label，不含 TC / BOS。与 encode_label_bos 互为逆运算。
 * 注意这不是简单 memcpy，别用赋值代替。 */
uint32_t decode_label(mpls_label_t *label_pnt)
{
	uint32_t l;
	uint8_t *pnt = (uint8_t *)label_pnt;

	l = ((uint32_t)*pnt++ << 12);
	l |= (uint32_t)*pnt++ << 4;
	l |= (uint32_t)((*pnt & 0xf0) >> 4);
	return l;
}

/* 【函数】encode_label_bos —— 把 20 位 label 编成线上 3 字节，BOS 位由参数显式给定。
 * 特例：label==BGP_PREVENT_VRF_2_VRF_LEAK 时原样写入整个 mpls_label_t（见 bgp_label.h
 * 说明，它是防 VRF 间泄漏的哨兵值，不走正常位布局）。
 * 正常编码：byte0=label>>12，byte1=label>>4，byte2=(label<<4)|bos。 */
void encode_label_bos(mpls_label_t label, mpls_label_t *label_pnt, bool bos)
{
	uint8_t *pnt = (uint8_t *)label_pnt;

	if (pnt == NULL)
		return;
	if (label == BGP_PREVENT_VRF_2_VRF_LEAK) {
		*label_pnt = label;
		return;
	}
	*pnt++ = (label >> 12) & 0xff;
	*pnt++ = (label >> 4) & 0xff;
	*pnt++ = ((label << 4) + (bos ? 1 : 0)) & 0xff;
}

/* 【函数】encode_label —— 便捷包装：固定 BOS=1（适用只有一个标签、它即栈底的情况）。 */
void encode_label(mpls_label_t label, mpls_label_t *label_pnt)
{
	encode_label_bos(label, label_pnt, true);
}

/* 【函数】bgp_labels_same —— 比较两个**裸标签数组**是否完全一致（个数 + 逐个值）。
 * 用途：判断“本次算出的标签”与“已有的标签”是否相同；相同就无需重新 intern /
 * 重新通告，可省掉无谓的 UPDATE 与标签申请。注意入参不是 bgp_labels 对象。 */
bool bgp_labels_same(const mpls_label_t *tbl_a, const uint8_t num_labels_a,
		     const mpls_label_t *tbl_b, const uint8_t num_labels_b)
{
	uint32_t i;

	if (num_labels_a != num_labels_b)
		return false;
	if (num_labels_a == 0)
		return true;

	for (i = 0; i < num_labels_a; i++) {
		if (tbl_a[i] != tbl_b[i])
			return false;
	}
	return true;
}

/* 【函数】bgp_labels_is_implicit_null —— 判断路径携带的标签栈是否恰为单个“隐式空标签”
 * (implicit-null，值 3)。要求 num_labels==1，再解码核对。隐式空标签意味着对端应弹出
 * 标签后按 IP 转发，在决定是否下发标签动作 / 是否 PHP 时需要区别对待。 */
bool bgp_labels_is_implicit_null(struct bgp_path_info *pi)
{
	if (BGP_PATH_INFO_NUM_LABELS(pi) != 1)
		return false;
	if (decode_label(&pi->extra->labels->label[0]) == MPLS_LABEL_IMPLICIT_NULL)
		return true;
	return false;
}

/* 【函数】mpls_labels2str —— 把标签栈格式化成 "前缀标签1/标签2/.../标签N" 字符串，
 * 供 show 命令显示。prefix 可为 NULL；标签值经 decode_label 转成本地数值(非线上字节)。
 * 返回 buf 本身，便于链式使用。 */
char *mpls_labels2str(mpls_label_t *labels, uint8_t num_labels, const char *prefix, char *buf,
		      int size)
{
	uint32_t label_value;
	uint32_t len = 0;
	uint8_t i;

	buf[0] = '\0';
	if (!num_labels)
		return buf;

	if (prefix) {
		strlcat(buf + len, prefix, size - len);
		len = strlen(buf);
	}

	label_value = decode_label(&labels[0]);
	len += snprintf(buf + len, size - len, "%u", label_value);

	for (i = 1; i < num_labels; i++) {
		label_value = decode_label(&labels[i]);
		len += snprintf(buf + len, size - len, "/%u", label_value);
	}

	return buf;
}
