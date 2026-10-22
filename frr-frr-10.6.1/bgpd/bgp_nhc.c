// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025, Donatas Abraitis <donatas@opensourcerouting.org>
 */

/*
 * 文件导读：BGP 下一跳能力（Next Hop Capability, NHC）TLV 链表工具
 *
 * 本模块为“下一跳能力”属性提供一组 TLV（类型-长度-值）链表的构造、追加、
 * 查找、释放辅助函数，另含一个针对 NNHN（Next-Next Hop Network）TLV 的
 * 计数工具。上层属性解析 / 编解码代码负责构造与读取这些 TLV，本文件只做
 * 通用链表操作，不承担任何协议语义判断。
 *
 * 关键数据结构：struct bgp_nhc 持有一条 TLV 单链表（tlvs）及其累计长度
 * （tlvs_length）；struct bgp_nhc_tlv 为单个 TLV，含 code、length、value
 * 以及指向下一项的 next 指针。
 *
 * 读的顺序：先看 tlv_new 的构造，再看 tlv_add 的尾部追加，然后是
 * tlv_find 查找、tlv_free / tlvs_free 释放，最后是 nnhn_count 计数。
 */

#include "bgp_nhc.h"

/*
 * 把一个 TLV 追加到 NHC 的 TLV 单链表尾部。若 tlv 为空直接返回；
 * 链表为空则作为首元素，否则遍历到表尾再挂接。新增后同步累加
 * nhc->tlvs_length，累加值等于 TLV 负载长度加上固定的 TLV 头长度。
 * 注意：本函数不拷贝 TLV，链表接管传入对象的所有权，后续由 tlvs_free 释放。
 */
void bgp_nhc_tlv_add(struct bgp_nhc *nhc, struct bgp_nhc_tlv *tlv)
{
	struct bgp_nhc_tlv *last;

	if (!tlv)
		return;

	for (last = nhc->tlvs; last && last->next; last = last->next)
		;

	if (last)
		last->next = tlv;
	else
		nhc->tlvs = tlv;

	nhc->tlvs_length += tlv->length + BGP_NHC_TLV_MIN_LEN;
}

/*
 * 新建一个 TLV：分配结构体（预留了 IPv4 最大字节数的内联空间）并填入
 * code 与 length，再单独分配 length 字节的 value 缓冲区，把 value 拷入。
 * 返回新对象的所有权给调用方；调用方需用 bgp_nhc_tlv_free 释放。
 * 注意此处不做长度合法性校验，也假定 value 至少 length 字节。
 */
struct bgp_nhc_tlv *bgp_nhc_tlv_new(uint16_t code, uint16_t length, const void *value)
{
	struct bgp_nhc_tlv *tlv;

	tlv = XCALLOC(MTYPE_BGP_NHC_TLV, sizeof(struct bgp_nhc_tlv) + IPV4_MAX_BYTELEN);
	tlv->code = code;
	tlv->length = length;
	tlv->value = XCALLOC(MTYPE_BGP_NHC_TLV_VAL, length);

	memcpy(tlv->value, value, length);

	return tlv;
}

/*
 * 在 NHC 的 TLV 链表中查找第一个 code 匹配的 TLV，找到即返回其指针，
 * 否则返回 NULL；nhc 为 NULL 时同样返回 NULL。仅返回链表内既有节点，
 * 不新建也不转移所有权。
 */
struct bgp_nhc_tlv *bgp_nhc_tlv_find(struct bgp_nhc *nhc, uint16_t code)
{
	struct bgp_nhc_tlv *tlv = NULL;

	if (!nhc)
		return tlv;

	for (tlv = nhc->tlvs; tlv; tlv = tlv->next) {
		if (tlv->code == code)
			return tlv;
	}

	return NULL;
}

/*
 * 释放单个 TLV：先释放其 value 缓冲区（非空时），再释放结构体本身。
 * 传入 NULL 时安全返回。此函数只处理当前节点，不递归释放 next 指向的节点。
 */
void bgp_nhc_tlv_free(struct bgp_nhc_tlv *tlv)
{
	if (!tlv)
		return;

	if (tlv->value)
		XFREE(MTYPE_BGP_NHC_TLV_VAL, tlv->value);

	XFREE(MTYPE_BGP_NHC_TLV, tlv);
}

/*
 * 释放整条 TLV 链表：循环摘下头节点，先把 next 保存下来再释放当前节点，
 * 避免释放后访问悬垂指针。传入 NULL 时安全返回。
 */
void bgp_nhc_tlvs_free(struct bgp_nhc_tlv *tlv)
{
	struct bgp_nhc_tlv *next;

	while (tlv) {
		next = tlv->next;
		bgp_nhc_tlv_free(tlv);
		tlv = next;
	}
}

/*
 * 释放一个 NHC 对象：先释放其内部整条 TLV 链表，再释放 NHC 结构体本身。
 * 传入 NULL 时安全返回。
 */
void bgp_nhc_free(struct bgp_nhc *nhc)
{
	if (!nhc)
		return;

	bgp_nhc_tlvs_free(nhc->tlvs);
	XFREE(MTYPE_BGP_NHC, nhc);
}

/*
 * 统计 NNHN（Next-Next Hop Network）TLV 中的下一跳个数。遍历链表找到
 * code 为 BGP_ATTR_NHC_TLV_NNHN 的 TLV，按其负载长度除以单个 BGP
 * Identifier 的 4 字节得到总个数；个数小于等于 1 时返回 0。
 * 返回值是“下一个下一跳”的个数，即总个数减一，因为第一个是下一跳本身的
 * BGP Identifier，这里只关心其后的下一跳。未找到对应 TLV 时返回 0。
 */
uint64_t bgp_nhc_nnhn_count(struct bgp_nhc *nhc)
{
	uint64_t count = 0;
	struct bgp_nhc_tlv *tlv;

	for (tlv = nhc->tlvs; tlv; tlv = tlv->next) {
		if (tlv->code == BGP_ATTR_NHC_TLV_NNHN) {
			/* BGP Identifier is always 4-bytes (yet...) */
			count = tlv->length / IPV4_MAX_BYTELEN;
			if (count <= 1)
				return 0;

			/* -1 is to exclude the next-hop BGP ID.
			 * We care only about Next-next hops here.
			 */
			return count - 1;
		}
	}

	return 0;
}
