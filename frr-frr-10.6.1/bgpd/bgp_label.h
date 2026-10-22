// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP carrying Label information
 * Copyright (C) 2013 Cumulus Networks, Inc.
 */
/* ============================================================================
 * 【文件导读】bgp_label.h —— BGP 携带的 MPLS 标签：编码/内部标记/共享/注册
 * ============================================================================
 * 职责与定位：BGP 有几种 SAFI 会在 NLRI 里**随前缀一起携带 MPLS 标签**——
 *   SAFI_LABELED_UNICAST(BGP-LU, RFC3107/8277)、SAFI_MPLS_VPN(VPNv4/v6, RFC4364)、
 *   SAFI_EVPN(携带 VNI)。本文件定义这些标签的内部表示、线上编解码、有效性标记、
 *   共享(intern)与向标签管理器注册的接口。
 *
 * 【线上编码（必须记住，本文件所有位操作都基于它）】
 *   MPLS 标签在报文里是 **3 字节(24 bit)**：
 *       [ 20 bit Label | 3 bit TC(EXP) | 1 bit BOS ]
 *   加上前缀长度字段后，“前缀长度”字段的单位是 **bit** 而非字节，所以一个
 *   0 长度前缀也能塞下 floor(256/24)=10 个标签 —— 这就是 BGP_MAX_LABELS=10 的来历。
 *   对应关系（本文件用 uint8_t* 视角操作）：
 *       t[0] = 高 8 位(label 高 8 位)，t[1] = 次 8 位，
 *       t[2] = 低 8 位：bit3..1 = TC，bit0 = BOS。
 *
 * 【一个内部约定/坑：0x02 这一位】
 *   FRR 不使用 TC/EXP 字段，于是**借用 t[2] 里的 0x02 位**作为“本端已确认该标签
 *   有效”的标记（bgp_set_valid_label/bgp_is_valid_label）。它不是协议语义位，
 *   但因为它落在线上传输的 24bit 内，收到对端报文时该位可能已经被置上——所以
 *   bgp_is_withdraw_label() 对 t[2] 同时接受 0x00 与 0x02 两种取值。
 *   读这几个 inline 函数时，先分清“协议位(BOS/TC)”与“FRR 内部标记位”。
 *
 * 【生命周期】bgp_labels 与 attr 一样走 interned 共享：内容相同(标签序列一致)
 *   的 bgp_labels 全局只留一份，靠 refcnt 复用；比较用 bgp_labels_cmp。
 *
 * 建议阅读顺序：BGP_MAX_LABELS 常量 → struct bgp_labels → bgp_labels_* intern 族
 *               → bgp_reg_dereg_for_label(向标签管理器注册) → 末尾一组 inline 位操作。
 * ============================================================================
 */

#ifndef _BGP_LABEL_H
#define _BGP_LABEL_H

/* 一个 MPLS 标签在报文里占 3 字节(24 bit)，见文件头导读的位布局。 */
#define BGP_LABEL_BYTES 3
/* 特殊“标签”取值 0xFFFFFFFE：用于防止 VRF 间泄漏——当某条路由不该被某 VRF
 * 使用时，用这个不可能出现在真实标签栈里的值做显式标记。
 * （真实标签 20 bit，0xFFFFFFFE 远超其范围，故不会与正常标签混淆。） */
#define BGP_PREVENT_VRF_2_VRF_LEAK 0xFFFFFFFE

struct bgp_dest;
struct bgp_path_info;
struct peer;

/* 【中文补充：为何上限恰好是 10】NLRI 里“前缀长度”字段只有 1 字节(最大 255)，
 * 且它的单位是 **bit**（前缀 bit 数 + 整个标签栈的 bit 数）。每个标签 24 bit，
 * 故最多容纳 floor(256/24) = 10 个标签——即使前缀本身长度为 0 也是如此。
 * 因此 BGP-LU 在协议层面就**不可能**携带超过 10 个标签；这个常量不是随意取的。 */
/* Maximum number of labels we can process or send with a prefix. We
 * support 10 for MPLS (BGP-LU) and need only 2 for EVPN-VxLAN.
 *
 * According to RFC 3107/RFC 8277 label stack is distributed in NLRI, length is given in
 * one byte in bits! (prefix length + whole number of label stack bits), so even if
 * prefix is 0 length, (each label takes 3 bytes) floor(256/24) = 10
 *
 * It is impossible to pass more than 10 labels by BGP-LU
 */
#define BGP_MAX_LABELS 10

/* 【结构】bgp_labels —— 一组标签栈 + 引用计数，参与 interned 共享。
 *   label[]    : 标签栈，按**栈顶在前**的顺序存放（发报文时按此顺序编码）。
 *   num_labels : 实际有效的标签个数。
 *   refcnt     : 共享计数（>1 时该对象被多方引用，务必视为只读，不可就地修改）。
 * 为什么不能就地改：一旦 intern 共享，同内容的对象全局唯一；就地改会让所有
 * 引用者一起被改。要“改”就得新建一个 bgp_labels 再 intern，替换引用。
 * EVPN-VxLAN 场景这里的 label 装的是 VNI。 */
/* MPLS label(s) - VNI(s) for EVPN-VxLAN  */
struct bgp_labels {
	mpls_label_t label[BGP_MAX_LABELS];
	uint8_t num_labels;

	unsigned long refcnt;
};

/* 【标签共享族】与 attr 的 intern 机制同构：
 *   bgp_labels_init/finish : 进程启动建共享哈希表 / 退出销毁。
 *   bgp_labels_intern      : 按内容查表复用，refcnt++，返回共享对象。
 *   bgp_labels_unintern    : refcnt--，归零则从表移除并释放，并把调用方指针置 NULL
 *                            （故传二级指针）。
 *   bgp_labels_cmp         : 内容比较（个数 + 逐个标签），供哈希与去重判断。
 *   bgp_labels_same        : 直接比较两个裸数组(不是 bgp_labels 对象)，用于
 *                            比较“现有标签”与“新算出的标签”是否一致。 */
extern void bgp_labels_init(void);
extern void bgp_labels_finish(void);
extern struct bgp_labels *bgp_labels_intern(struct bgp_labels *labels);
extern void bgp_labels_unintern(struct bgp_labels **plabels);
extern bool bgp_labels_cmp(const struct bgp_labels *labels1,
			   const struct bgp_labels *labels2);

/* 【标签申请链路：bgpd → label 管理器(zebra/ldpd) → bgpd】
 *   bgp_reg_dereg_for_label(dest, pi, true)  : 为某条路径申请一个本地出标签。
 *       标签管理器的分配是**异步**的：这里只是发起请求，真正的结果通过下面的
 *       callback 回来。dest/pi 用来标识“这个标签是给谁的”。
 *   bgp_reg_for_label_callback(new_label, labelid, allocated) : 标签分配完成的
 *       回调。allocated=false 表示申请失败/被回收，需要相应处理（该路由不能
 *       继续作为带标签路由通告）。
 *   bgp_parse_fec_update() : 处理来自 zebra 的 FEC(转发等价类)更新消息——对端
 *       把某 FEC 映射到了新标签，需要更新本地对应的标签映射。
 * 这条链路是 BGP-LU / VPN 场景下“标签从哪来”的答案。 */
extern int bgp_reg_for_label_callback(mpls_label_t new_label, void *labelid,
				    bool allocated);
extern void bgp_reg_dereg_for_label(struct bgp_dest *dest,
				    struct bgp_path_info *pi, bool reg);
extern int bgp_parse_fec_update(void);
/* 【函数】bgp_adv_label —— 把 labels/num_labels 这组标签随路由 dest(路径 pi)
 * 通告给对端 to。相比直接写属性，这里会处理“标签是否需要重新申请 / 是否已就绪”
 * 等情况，必要时先登记再等回调。 */
extern void bgp_adv_label(struct bgp_dest *dest, struct bgp_path_info *pi, struct peer *to,
			  afi_t afi, safi_t safi, mpls_label_t *labels, uint8_t *num_labels);

/* 【函数】bgp_nlri_parse_label —— 解析 BGP-LU / VPN 的 NLRI：
 * 从 packet 里按“前缀长度(bit) → 标签栈 → 前缀”的顺序取出，逐条交给
 * bgp_update/bgp_withdraw 处理（与 bgp_nlri_parse_ipv4 同层次的入口）。 */
extern int bgp_nlri_parse_label(struct peer *peer, struct attr *attr,
				struct bgp_nlri *packet);
/* 【编解码】线上 3 字节(24bit) ←→ 内部 mpls_label_t。
 *   decode_label  : 收到报文后把线上格式转成本地可比较的数值（含大小端调整）。
 *   encode_label  : 发送前把内部数值写成线上 3 字节（BOS=0）。
 *   encode_label_bos : 同上但显式指定 BOS 位——多标签栈时**只有栈底那个**标签
 *      置 BOS=1，其余为 0；写错会让对端无法正确解析栈深。
 *   注意这两个方向不是简单的 memcpy，别用赋值代替。 */
extern uint32_t decode_label(mpls_label_t *label);
extern void encode_label_bos(mpls_label_t label_in, mpls_label_t *label_out, bool bos);
extern void encode_label(mpls_label_t label_in, mpls_label_t *label_out);
/* 【函数】bgp_labels_same —— 比较两个**裸标签数组**是否完全一致（个数 + 逐个值）。
 * 用途：判断“现存的标签”与“本次算出来的标签”是否相同；相同就不必重新 intern /
 * 重新通告（避免无谓的 UPDATE 与标签申请）。注意入参不是 bgp_labels 对象。 */
extern bool bgp_labels_same(const mpls_label_t *tbl_a,
			    const uint8_t num_labels_a,
			    const mpls_label_t *tbl_b,
			    const uint8_t num_labels_b);
/* 【函数】bgp_labels_is_implicit_null —— 判断路径携带的标签是否为隐式空标签
 * (implicit-null, 标签值 3)。隐式空标签意味着“对端应弹出标签后按 IP 转发”，
 * 在决定是否下发标签动作 / 是否 PHP 时需要区别对待。 */
extern bool bgp_labels_is_implicit_null(struct bgp_path_info *pi);
/* 【函数】mpls_labels2str —— 把标签栈打印成 "prefix标签1/标签2/.../标签N" 形式，
 * 供 show 命令显示。label 值已是解码后的本地数值（不是线上 3 字节）。 */
/* Write labels to str of format "prefixLABEL1/LABEL2/.../LABELN", prefix may be NULL */
extern char *mpls_labels2str(mpls_label_t *labels, uint8_t num_labels, const char *prefix,
			     char *buf, int size);

/* 【inline 位操作组总览】以下函数都以 uint8_t* 视角操作“单个标签的 3 字节”：
 *   协议语义位: BOS = t[2] & 0x01
 *   内部标记位: VALID = t[2] & 0x02 （FRR 借用 TC 区域的一位，见文件头导读）
 * 读这些函数时务必先分清是哪一类位。 */

/* 【函数】bgp_labeled_safi —— 该 SAFI 的 NLRI 里是否**可能**带标签。
 * 注意语义是“可能”而非“必然”（见函数内注释）：EVPN 的路由并非条条都带标签，
 * 所以不能用它来断言“一定有标签”，只能用来决定“要不要按带标签的格式去解析”。 */
static inline int bgp_labeled_safi(safi_t safi)
{
	/* NOTE: This API really says a label (tag) MAY be present. Not all EVPN
	 * routes will have a label.
	 */
	if ((safi == SAFI_LABELED_UNICAST) || (safi == SAFI_MPLS_VPN)
	    || (safi == SAFI_EVPN))
		return 1;
	return 0;
}

/* 【函数】bgp_is_withdraw_label —— 判断这 3 字节是否表示“撤销标签”。
 * 撤销时发送方填的是全 0（或 0x80 0x00 0x00）。判据逐字节列出：
 *   pkt[0] ∈ {0x80, 0x00}，pkt[1] == 0x00，pkt[2] ∈ {0x00, 0x02}。
 * 为什么 pkt[2] 要额外容忍 0x02：本端可能已经对“撤销标签”调用过
 * bgp_set_valid_label()，把内部标记位置上了；若不兼容这个位，自己产生的
 * 撤销标签就认不出来了。这是内部标记位渗透到判据里的典型例子。 */
static inline int bgp_is_withdraw_label(mpls_label_t *label)
{
	uint8_t *pkt = (uint8_t *)label;

	/* The check on pkt[2] for 0x00 or 0x02 is in case bgp_set_valid_label()
	 * was called on the withdraw label */
	if (((pkt[0] == 0x80) || (pkt[0] == 0x00)) && (pkt[1] == 0x00)
	    && ((pkt[2] == 0x00) || (pkt[2] == 0x02)))
		return 1;
	return 0;
}

/* 【函数】bgp_is_valid_label —— 检查内部的“标签有效”标记位(0x02)。
 * 返回的是**该位的值**(0 或 0x02)，不是布尔 0/1——虽然当条件用没问题，但若
 * 直接当布尔输出（如打印）会看到 2，别被误导。 */
static inline int bgp_is_valid_label(const mpls_label_t *label)
{
	uint8_t *t = (uint8_t *)label;
	if (!t)
		return 0;
	return (t[2] & 0x02);
}

/* 【函数】bgp_set_valid_label / bgp_unset_valid_label —— 置/清内部的“标签有效”
 * 标记位(0x02)。典型用法：从报文中解出标签后置上以确认“这个值我们已经校验并
 * 接受”；之后靠 bgp_is_valid_label 快速判断，无需重新解析。
 * 成对使用，unset 用于需要把标签退回“未确认”状态时。 */
static inline void bgp_set_valid_label(mpls_label_t *label)
{
	uint8_t *t = (uint8_t *)label;
	if (t)
		t[2] |= 0x02;
}

static inline void bgp_unset_valid_label(mpls_label_t *label)
{
	uint8_t *t = (uint8_t *)label;
	if (t)
		t[2] &= ~0x02;
}

/* 【便捷包装】bgp_register/unregister_for_label —— 只是把 bool 参数写死成
 * true/false 的两个薄封装，提高可读性。真正的实现在 bgp_label.c 的
 * bgp_reg_dereg_for_label()。 */
static inline void bgp_register_for_label(struct bgp_dest *dest,
					  struct bgp_path_info *pi)
{
	bgp_reg_dereg_for_label(dest, pi, true);
}

/* 【函数】bgp_unregister_for_label —— 注销 dest 上的标签申请（pi 传 NULL，
 * 因为注销是按 dest 维度做的）。路由被删除/不再需要出标签时必须调用，否则
 * 标签池会泄漏。 */
static inline void bgp_unregister_for_label(struct bgp_dest *dest)
{
	bgp_reg_dereg_for_label(dest, NULL, false);
}

/* 【中文补充：BOS 的语义】BOS = Bottom Of Stack，栈底标志。一个标签栈里
 * 只有**最底层**(最后一个)那个标签的 BOS 位是 1，其余为 0。收端据此知道栈
 * 到哪里结束。`label_bos` 返回的是该位的原始值(0 或 1)，不是布尔。 */
/* Return BOS value of label stream */
static inline uint8_t label_bos(mpls_label_t *label)
{
	uint8_t *t = (uint8_t *)label;
	return (t[2] & 0x01);
};

/* 【注意】与 bgp_set_valid_label 一样只置位不清位——没有“label_unset_bos”。
 * 因此只有在确定该标签就是栈底时才调用；多标签场景中若对中间标签误用，会
 * 让对端提前认为标签栈结束。 */
/* Set BOS to 1 */
static inline void label_set_bos(mpls_label_t *label)
{
	uint8_t *t = (uint8_t *)label;

	if (t)
		t[2] |= 0x01;
}

#endif /* _BGP_LABEL_H */
