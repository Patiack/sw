// SPDX-License-Identifier: GPL-2.0-or-later
/* Ethernet-VPN Attribute handling file
 * Copyright (C) 2016 6WIND
 */

/*
 * 文件导读：EVPN（以太网 VPN）相关属性的解析与辅助工具
 *
 * 本模块处理 BGP EVPN 路由属性中那些以扩展团体（extended community）形式
 * 携带的字段，以及一些 EVPN 前缀构造与比较的小工具。它不负责完整的属性
 * 编解码（那在 bgp_attr.c），而是提供若干“从 attr 的扩展团体里查出某个 EVPN
 * 子类型字段”的只读访问器。
 *
 * 主要覆盖：
 *   - ESI（以太网段标识）字符串与结构体互转、EVPN 路由信息比较；
 *   - Router MAC 扩展团体的添加与读取，以及把扩展团体里的 MAC 转成字符串；
 *   - Default Gateway、DF 选举（DF preference 与算法）、MAC 移动性序号、
 *     EVPN NA（邻居通告）标志位（router 标志与 proxy 标志）等扩展团体的查询；
 *   - 把普通 IPv4 / IPv6 前缀“伪造”成 EVPN type-5 IP 前缀。
 *
 * 与相邻模块的配合：本文件的消费者主要是 bgp_evpn.c / bgp_evpn_mh.c 以及
 * 路由处理流程；扩展团体本身的构造与遍历依赖 bgp_ecommunity.h 提供的 API。
 *
 * 读的顺序：先看 bgp_route_evpn_same 与 str2esi 等基础比较 / 转换，再看一
 * 批“从扩展团体取字段”的访问器，最后看 bgp_build_evpn_prefix 前缀构造。
 */

#include <zebra.h>

#include "command.h"
#include "filter.h"
#include "prefix.h"
#include "log.h"
#include "memory.h"
#include "stream.h"
#include "vxlan.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_attr_evpn.h"
#include "bgpd/bgp_ecommunity.h"
#include "bgpd/bgp_evpn.h"
#include "bgpd/bgp_evpn_private.h"

/*
 * 比较两条 EVPN 路由信息是否相同。若一方为空另一方非空则不同，两者皆空则
 * 视为相同；否则按 type、ESI（eth_s_id）与网关 IP（gw_ip）三项逐一比较，
 * 全部相同才返回 true。任一参数为 NULL 是被显式支持的，调用方无需先判空。
 */
bool bgp_route_evpn_same(const struct bgp_route_evpn *e1,
			 const struct bgp_route_evpn *e2)
{
	if (!e1 && e2)
		return false;
	if (!e2 && e1)
		return false;
	if (!e1 && !e2)
		return true;

	return (e1->type == e2->type &&
		!memcmp(&(e1->eth_s_id), &(e2->eth_s_id), sizeof(esi_t)) &&
		!ipaddr_cmp(&(e1->gw_ip), &(e2->gw_ip)));
}

/*
 * 向属性 attr 的扩展团体集合中添加一个 Router MAC 扩展团体（EVPN 编码、
 * ROUTERMAC 子类型），后随 6 字节 MAC。若 attr 尚无扩展团体则先新建一个，
 * 随后追加该值并刷新扩展团体的字符串缓存。无返回值，直接就地修改 attr。
 */
void bgp_add_routermac_ecom(struct attr *attr, struct ethaddr *routermac)
{
	struct ecommunity_val routermac_ecom;
	struct ecommunity *ecomm = bgp_attr_get_ecommunity(attr);

	memset(&routermac_ecom, 0, sizeof(routermac_ecom));
	routermac_ecom.val[0] = ECOMMUNITY_ENCODE_EVPN;
	routermac_ecom.val[1] = ECOMMUNITY_EVPN_SUBTYPE_ROUTERMAC;
	memcpy(&routermac_ecom.val[2], routermac->octet, ETH_ALEN);
	if (!ecomm) {
		bgp_attr_set_ecommunity(attr, ecommunity_new());
		ecomm = bgp_attr_get_ecommunity(attr);
	}
	ecommunity_add_val(ecomm, &routermac_ecom, false, false);
	ecommunity_str(ecomm);
}

/*
 * 把形如 AA:BB:CC:DD:EE:FF:GG:HH:II:JJ 的字符串解析为 10 字节的 ESI。
 * 解析成功（恰好 10 段）返回 true，否则 false。若 id 为 NULL，则只做格式
 * 校验、不写结果，便于“仅校验字符串合法性”的场景。str 为 NULL 返回 false。
 */
/* converts to an esi
 * returns 1 on success, 0 otherwise
 * format accepted: AA:BB:CC:DD:EE:FF:GG:HH:II:JJ
 * if id is null, check only is done
 */
bool str2esi(const char *str, esi_t *id)
{
	unsigned int a[ESI_BYTES];
	int i;

	if (!str)
		return false;
	if (sscanf(str, "%2x:%2x:%2x:%2x:%2x:%2x:%2x:%2x:%2x:%2x", a + 0, a + 1,
		   a + 2, a + 3, a + 4, a + 5, a + 6, a + 7, a + 8, a + 9)
	    != ESI_BYTES) {
		/* error in incoming str length */
		return false;
	}
	/* valid mac address */
	if (!id)
		return true;
	for (i = 0; i < ESI_BYTES; ++i)
		id->val[i] = a[i] & 0xff;
	return true;
}

/*
 * 从扩展团体值的指针中取出其中的 MAC 部分并转为字符串。ecom_mac 指向扩展
 * 团体值的起始处，跳过前 2 字节（type 与 sub_type）后把其后当作 ethaddr
 * 交给 prefix_mac2str 格式化。返回值来自 prefix_mac2str（静态缓冲区）。
 */
char *ecom_mac2str(char *ecom_mac)
{
	char *en;

	en = ecom_mac;
	en += 2;

	return prefix_mac2str((struct ethaddr *)en, NULL, 0);
}

/*
 * 从属性 attr 的扩展团体中取出 Router MAC。遍历所有扩展团体，找到 EVPN 编码
 * 且 ROUTERMAC 子类型的那一个，把其后的 6 字节 MAC 拷入 rmac 并返回 true；
 * 未找到返回 false（attr 无扩展团体或为空时也返回 false）。找到即返回，不
 * 继续扫描后续同类型团体。
 */
/* Fetch router-mac from extended community */
bool bgp_attr_rmac(struct attr *attr, struct ethaddr *rmac)
{
	uint32_t i = 0;
	struct ecommunity *ecom;

	ecom = bgp_attr_get_ecommunity(attr);
	if (!ecom || !ecom->size)
		return false;

	/* If there is a router mac extended community, set RMAC in attr */
	for (i = 0; i < ecom->size; i++) {
		uint8_t *pnt = NULL;
		uint8_t type = 0;
		uint8_t sub_type = 0;

		pnt = (ecom->val + (i * ECOMMUNITY_SIZE));
		type = *pnt++;
		sub_type = *pnt++;

		if (!(type == ECOMMUNITY_ENCODE_EVPN
		      && sub_type == ECOMMUNITY_EVPN_SUBTYPE_ROUTERMAC))
			continue;

		memcpy(rmac, pnt, ETH_ALEN);
		return true;
	}
	return false;
}

/*
 * 判断属性 attr 是否带有 Default Gateway 扩展团体，并据此设置或清除 attr 上
 * 的 ATTR_EVPN_FLAG_DEFAULT_GW 标志。函数先清除旧标志，再遍历扩展团体，遇到
 * 不透明编码（OPAQUE）、DEF_GW 子类型的团体即置位并结束。无返回值，就地更新标志。
 */
/*
 * Determine whether 'attr' contains default gw extended community
 */
void bgp_attr_default_gw(struct attr *attr)
{
	struct ecommunity *ecom;
	uint32_t i;

	/* Reset default-gw flag */
	UNSET_FLAG(attr->evpn_flags, ATTR_EVPN_FLAG_DEFAULT_GW);

	ecom = bgp_attr_get_ecommunity(attr);
	if (!ecom || !ecom->size)
		return;

	/* Look for a default gw extended community */
	for (i = 0; i < ecom->size; i++) {
		uint8_t *pnt;
		uint8_t type, sub_type;

		pnt = (ecom->val + (i * ECOMMUNITY_SIZE));
		type = *pnt++;
		sub_type = *pnt++;

		if ((type == ECOMMUNITY_ENCODE_OPAQUE
		     && sub_type == ECOMMUNITY_EVPN_SUBTYPE_DEF_GW)) {
			SET_FLAG(attr->evpn_flags, ATTR_EVPN_FLAG_DEFAULT_GW);
			break;
		}
	}
}

/*
 * 从属性 attr 的 DF 选举（DF election）扩展团体中取出 DF 优先级，并把算法
 * 通过出参 alg 返回。返回值即 DF preference；未找到对应团体或 attr 无扩展
 * 团体时返回 0。alg 默认先置为 EVPN_MH_DF_ALG_SERVICE_CARVING（服务切割
 * 算法），找到团体后再按载荷中的算法位更新。找到即结束，不处理多个团体。
 */
/*
 * Fetch and return the DF preference and algorithm from
 * DF election extended community, if present, else 0.
 */
uint16_t bgp_attr_df_pref_from_ec(struct attr *attr, uint8_t *alg)
{
	struct ecommunity *ecom;
	uint32_t i;
	uint16_t df_pref = 0;

	*alg = EVPN_MH_DF_ALG_SERVICE_CARVING;
	ecom = bgp_attr_get_ecommunity(attr);
	if (!ecom || !ecom->size)
		return 0;

	for (i = 0; i < ecom->size; i++) {
		uint8_t *pnt;
		uint8_t type, sub_type;

		pnt = (ecom->val + (i * ECOMMUNITY_SIZE));
		type = *pnt++;
		sub_type = *pnt++;
		if (!(type == ECOMMUNITY_ENCODE_EVPN
		      && sub_type == ECOMMUNITY_EVPN_SUBTYPE_DF_ELECTION))
			continue;

		*alg = (*pnt++) & ECOMMUNITY_EVPN_SUBTYPE_DF_ALG_BITS;

		pnt += 3;
		pnt = ptr_get_be16(pnt, &df_pref);
		(void)pnt; /* consume value */
		break;
	}

	return df_pref;
}

/*
 * 从属性 attr 的 MAC 移动性（MAC Mobility）扩展团体中取出序号并返回；
 * 未找到或 attr 无扩展团体时返回 0。遍历中先读 flags，若带 STICKY 标志则
 * 设置 attr 上的 ATTR_EVPN_FLAG_STICKY，否则清除该标志；随后跳过 1 字节并
 * 读取 4 字节序号返回。找到第一个即返回（RFC 对同路由多个该团体的处理未明确，
 * 此处遇到第一个就结束）。无扩展团体时不会改动 STICKY 标志。
 */
/*
 * Fetch and return the sequence number from MAC Mobility extended
 * community, if present, else 0.
 */
uint32_t bgp_attr_mac_mobility_seqnum(struct attr *attr)
{
	struct ecommunity *ecom;
	uint32_t i;
	uint8_t flags = 0;

	ecom = bgp_attr_get_ecommunity(attr);
	if (!ecom || !ecom->size)
		return 0;

	/* If there is a MAC Mobility extended community, return its
	 * sequence number.
	 * TODO: RFC is silent on handling of multiple MAC mobility extended
	 * communities for the same route. We will bail out upon the first
	 * one.
	 */
	for (i = 0; i < ecom->size; i++) {
		const uint8_t *pnt;
		uint8_t type, sub_type;
		uint32_t seq_num;

		pnt = (ecom->val + (i * ECOMMUNITY_SIZE));
		type = *pnt++;
		sub_type = *pnt++;
		if (!(type == ECOMMUNITY_ENCODE_EVPN
		      && sub_type == ECOMMUNITY_EVPN_SUBTYPE_MACMOBILITY))
			continue;
		flags = *pnt++;

		if (CHECK_FLAG(flags,
			       ECOMMUNITY_EVPN_SUBTYPE_MACMOBILITY_FLAG_STICKY))
			SET_FLAG(attr->evpn_flags, ATTR_EVPN_FLAG_STICKY);
		else
			UNSET_FLAG(attr->evpn_flags, ATTR_EVPN_FLAG_STICKY);

		pnt++;
		pnt = ptr_get_be32(pnt, &seq_num);
		(void)pnt; /* consume value */
		return seq_num;
	}

	return 0;
}

/*
 * 检查属性 attr 的 EVPN NA（邻居通告）扩展团体，据其标志位更新状态：若带
 * ROUTER 标志则设置 attr 上的 ATTR_EVPN_FLAG_ROUTER；若带 PROXY 标志则把
 * 出参 proxy 置为 true。函数无返回值，就地修改 attr 与 proxy。
 * 注意 proxy 只在命中该团体且带对应标志时才被写入，调用方需自行初始化。
 */
/*
 * return true if attr contains router flag extended community
 */
void bgp_attr_evpn_na_flag(struct attr *attr, bool *proxy)
{
	struct ecommunity *ecom;
	uint32_t i;
	uint8_t val;

	ecom = bgp_attr_get_ecommunity(attr);
	if (!ecom || !ecom->size)
		return;

	/* If there is a evpn na extendd community set router_flag */
	for (i = 0; i < ecom->size; i++) {
		uint8_t *pnt;
		uint8_t type, sub_type;

		pnt = (ecom->val + (i * ECOMMUNITY_SIZE));
		type = *pnt++;
		sub_type = *pnt++;

		if (type == ECOMMUNITY_ENCODE_EVPN &&
		    sub_type == ECOMMUNITY_EVPN_SUBTYPE_ND) {
			val = *pnt++;

			if (CHECK_FLAG(val,
				       ECOMMUNITY_EVPN_SUBTYPE_ND_ROUTER_FLAG))
				SET_FLAG(attr->evpn_flags,
					 ATTR_EVPN_FLAG_ROUTER);

			if (CHECK_FLAG(val, ECOMMUNITY_EVPN_SUBTYPE_PROXY_FLAG))
				*proxy = true;

			break;
		}
	}
}

/*
 * 把普通 IPv4 / IPv6 前缀 dst（就地改造）伪造为 EVPN 前缀，目前仅支持
 * type-5（BGP_EVPN_IP_PREFIX_ROUTE）：填入 eth_tag、前缀长度与 IP 地址，
 * 并按地址族设置 EVPN 前缀整体长度。成功返回 0；dst 为空、family 为 0 或
 * evpn_type 非 type-5 时返回 -1（此时 dst 可能已被清零，调用方需谨慎）。
 * 易踩的坑：函数会先清空 dst 再重填，原前缀内容在此过程中被丢弃。
 */
/* dst prefix must be AF_INET or AF_INET6 prefix, to forge EVPN prefix */
extern int bgp_build_evpn_prefix(int evpn_type, uint32_t eth_tag,
				 struct prefix *dst)
{
	struct evpn_addr *p_evpn_p;
	struct prefix p2;
	struct prefix *src = &p2;

	if (!dst || dst->family == 0)
		return -1;
	/* store initial prefix in src */
	prefix_copy(src, dst);
	memset(dst, 0, sizeof(struct prefix));
	p_evpn_p = &(dst->u.prefix_evpn);
	dst->family = AF_EVPN;
	p_evpn_p->route_type = evpn_type;
	if (evpn_type == BGP_EVPN_IP_PREFIX_ROUTE) {
		p_evpn_p->prefix_addr.eth_tag = eth_tag;
		p_evpn_p->prefix_addr.ip_prefix_length = p2.prefixlen;
		if (src->family == AF_INET) {
			SET_IPADDR_V4(&p_evpn_p->prefix_addr.ip);
			memcpy(&p_evpn_p->prefix_addr.ip.ipaddr_v4,
			       &src->u.prefix4,
			       sizeof(struct in_addr));
			dst->prefixlen = (uint16_t)PREFIX_LEN_ROUTE_TYPE_5_IPV4;
		} else {
			SET_IPADDR_V6(&p_evpn_p->prefix_addr.ip);
			memcpy(&p_evpn_p->prefix_addr.ip.ipaddr_v6,
			       &src->u.prefix6,
			       sizeof(struct in6_addr));
			dst->prefixlen = (uint16_t)PREFIX_LEN_ROUTE_TYPE_5_IPV6;
		}
	} else
		return -1;
	return 0;
}
