// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP Extended Communities Attribute
 * Copyright (C) 2000 Kunihiro Ishiguro <kunihiro@zebra.org>
 */

/* 【文件导读】 bgp_ecommunity.c —— Extended Community 属性的实现
 *
 * 一、本文件做什么
 *    实现 bgp_ecommunity.h 声明的操作：分配 / 释放、解析（线格式与字符串）、
 *    打印（三种格式）、驻留与引用计数、集合运算、各 subtype 的语义处理，以及
 *    link-bandwidth 与聚合器相关函数。扩展社区是 RT / SoO / 带宽 / Router MAC /
 *    流量动作等的统一载体，语义分支很多。阅读主线：先看 type（高序字节）决定
 *    大类，再看 subtype（低序字节）决定细节。
 *
 * 二、单元与内存布局
 *    普通扩展社区单元 8 字节，IPv6 扩展社区单元 20 字节；结构里用 unit_size
 *    区分，size 是单元个数，val 是连续单元数组。所有以 _internal 结尾的函数
 *    用参数带入 unit_size，被普通版本（8 字节）与 IPv6 版本（20 字节）复用。
 *
 * 三、驻留与引用计数
 *    ecomhash 是全局哈希表。ecommunity_intern / ecommunity_unintern 与 community
 *    系列同构：内容相同的集合进程内只存一份，用 refcnt 共享。
 *
 * 四、字符串互转
 *    ecommunity_gettoken 负责词法解析（rt / soo / nt / color 关键字，以及
 *    A.B.C.D:MN、EF:OPQR、GHJK:MN 等数字形式）；ecommunity_str2com 组装集合
 *    （返回未驻留对象）；_ecommunity_ecom2str 负责反向打印，按 subtype 分支。
 *
 * 五、返回约定（重点）
 *    - ecommunity_str 返回对象内部缓存，勿 free；
 *    - ecommunity_ecom2str / _ecommunity_ecom2str 返回新分配字符串，需用
 *      ecommunity_strfree 释放；
 *    - ecommunity_str2com / ecommunity_parse 返回未驻留对象（refcnt 为 0）。
 *
 * 六、推荐阅读顺序
 *    ecommunity_new / free -> ecommunity_add_val_internal -> uniq_sort_internal
 *    -> ecommunity_parse / dup -> intern / unintern -> ecommunity_gettoken /
 *    str2com -> _ecommunity_ecom2str（打印总入口，各 subtype 分支）-> 末尾的
 *    linkbw 与 aggregate 系列。
 */

#include <zebra.h>

#include "hash.h"
#include "memory.h"
#include "prefix.h"
#include "command.h"
#include "queue.h"
#include "filter.h"
#include "jhash.h"
#include "stream.h"

#include "lib/printfrr.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_encap_types.h"
#include "bgpd/bgp_ecommunity.h"
#include "bgpd/bgp_lcommunity.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_flowspec_private.h"
#include "bgpd/bgp_pbr.h"

/* struct used to dump the rate contained in FS set traffic-rate EC */
/* 【结构】traffic_rate：把 4 字节按 float 与字节数组两种视角访问，
 * 用于 FlowSpec traffic-rate 扩展社区中带宽的打印。
 */
union traffic_rate {
	float rate_float;
	uint8_t rate_byte[4];
};

/* Hash of community attribute. */
/* 【全局变量】ecomhash：所有已驻留扩展社区集合的全局哈希表；
 * 键用 ecommunity_hash_make、比较用 ecommunity_cmp。
 */
static struct hash *ecomhash;

/* Allocate a new ecommunities.  */
/* 【函数】ecommunity_new：分配空扩展社区，unit_size 默认 8 字节。 */
struct ecommunity *ecommunity_new(void)
{
	struct ecommunity *ecom;

	ecom = (struct ecommunity *)XCALLOC(MTYPE_ECOMMUNITY,
					    sizeof(struct ecommunity));
	ecom->unit_size = ECOMMUNITY_SIZE;
	return ecom;
}

/* 【函数】ecommunity_strfree：释放由 ecommunity_ecom2str 系列分配的字符串。 */
void ecommunity_strfree(char **s)
{
	XFREE(MTYPE_ECOMMUNITY_STR, *s);
}

/* Free ecommunities.  */
/* 【函数】ecommunity_free：释放扩展社区及其内部 val / str，并置空指针。 */
void ecommunity_free(struct ecommunity **ecom)
{
	if (!(*ecom))
		return;

	XFREE(MTYPE_ECOMMUNITY_VAL, (*ecom)->val);
	XFREE(MTYPE_ECOMMUNITY_STR, (*ecom)->str);
	XFREE(MTYPE_ECOMMUNITY, *ecom);
}

/* 【函数】ecommunity_hash_free：哈希销毁回调，释放每个集合。 */
static void ecommunity_hash_free(struct ecommunity *ecom)
{
	ecommunity_free(&ecom);
}


/* Add a new Extended Communities value to Extended Communities
   Attribute structure.  When the value is already exists in the
   structure, we don't add the value.  Newly added value is sorted by
   numerical order.  When the value is added to the structure return 1
   else return 0.
   The additional parameters 'unique' and 'overwrite' ensure a particular
   extended community (based on type and sub-type) is present only
   once and whether the new value should replace what is existing or
   not.
*/
/* 【函数】ecommunity_add_val_internal：通用插入，参数 ecom_size 决定单元长度
 * （8 或 20 字节），故同时服务普通与 IPv6 两种扩展社区。
 *   unique 为真时按 type 与 subtype 判重：同 type / subtype 已存在则不再插入，
 *     overwrite 为真时用新值覆盖旧值；
 *   unique 为假时按整单元字节判重并保持升序插入。
 * 插入位置由 ins_idx 计算，再用 memmove 腾挪。
 */
static bool ecommunity_add_val_internal(struct ecommunity *ecom,
					const void *eval,
					bool unique, bool overwrite,
					uint8_t ecom_size)
{
	uint32_t c, ins_idx;
	const struct ecommunity_val *eval4 = (struct ecommunity_val *)eval;
	const struct ecommunity_val_ipv6 *eval6 =
		(struct ecommunity_val_ipv6 *)eval;

	/* When this is fist value, just add it. */
	if (ecom->val == NULL) {
		ecom->size = 1;
		ecom->val = XMALLOC(MTYPE_ECOMMUNITY_VAL,
				    ecom_length_size(ecom, ecom_size));
		memcpy(ecom->val, eval, ecom_size);
		return true;
	}

	/* If the value already exists in the structure return 0.  */
	/* check also if the extended community itself exists. */
	c = 0;

	ins_idx = UINT32_MAX;
	for (uint8_t *p = ecom->val; c < ecom->size;
	     p += ecom_size, c++) {
		if (unique) {
			if (ecom_size == ECOMMUNITY_SIZE) {
				if (p[0] == eval4->val[0] &&
				    p[1] == eval4->val[1]) {
					if (overwrite) {
						memcpy(p, eval4->val,
						       ecom_size);
						return true;
					}
					return false;
				}
			} else {
				if (p[0] == eval6->val[0] &&
				    p[1] == eval6->val[1]) {
					if (overwrite) {
						memcpy(p, eval6->val,
						       ecom_size);
						return true;
					}
					return false;
				}
			}
		}
		int ret = memcmp(p, eval, ecom_size);
		if (ret == 0)
			return false;
		if (ret > 0) {
			if (!unique)
				break;
			if (ins_idx == UINT32_MAX)
				ins_idx = c;
		}
	}

	if (ins_idx == UINT32_MAX)
		ins_idx = c;

	/* Add the value to the structure with numerical sorting.  */
	ecom->size++;
	ecom->val = XREALLOC(MTYPE_ECOMMUNITY_VAL, ecom->val,
			 ecom_length_size(ecom, ecom_size));

	memmove(ecom->val + ((ins_idx + 1) * ecom_size),
		ecom->val + (ins_idx * ecom_size),
		(ecom->size - 1 - ins_idx) * ecom_size);
	memcpy(ecom->val + (ins_idx * ecom_size),
	       eval, ecom_size);

	return true;
}

/* Add a new Extended Communities value to Extended Communities
 * Attribute structure.  When the value is already exists in the
 * structure, we don't add the value.  Newly added value is sorted by
 * numerical order.  When the value is added to the structure return 1
 * else return 0.
 */
/* 【函数】ecommunity_add_val：8 字节单元的插入封装。 */
bool ecommunity_add_val(struct ecommunity *ecom, struct ecommunity_val *eval,
		       bool unique, bool overwrite)
{
	return ecommunity_add_val_internal(ecom, (const void *)eval, unique,
					   overwrite, ECOMMUNITY_SIZE);
}

/* 【函数】ecommunity_add_val_ipv6：20 字节单元的插入封装。 */
bool ecommunity_add_val_ipv6(struct ecommunity *ecom,
			     struct ecommunity_val_ipv6 *eval,
			     bool unique, bool overwrite)
{
	return ecommunity_add_val_internal(ecom, (const void *)eval, unique,
					   overwrite, IPV6_ECOMMUNITY_SIZE);
}

static struct ecommunity *
/* 【函数】ecommunity_uniq_sort_internal：按 ecom_size 去重排序，返回新集合
 * （沿用 disable_ieee_floating）。
 */
ecommunity_uniq_sort_internal(struct ecommunity *ecom,
			      unsigned short ecom_size)
{
	uint32_t i;
	struct ecommunity *new;
	const void *eval;

	if (!ecom)
		return NULL;

	new = ecommunity_new();
	new->unit_size = ecom_size;
	new->disable_ieee_floating = ecom->disable_ieee_floating;

	for (i = 0; i < ecom->size; i++) {
		eval = (void *)(ecom->val + (i * ecom_size));
		ecommunity_add_val_internal(new, eval, false, false, ecom_size);
	}
	return new;
}

/* This function takes pointer to Extended Communities structure then
 * create a new Extended Communities structure by uniq and sort each
 * Extended Communities value.
 */
/* 【函数】ecommunity_uniq_sort：8 字节版本的去重排序入口。 */
struct ecommunity *ecommunity_uniq_sort(struct ecommunity *ecom)
{
	return ecommunity_uniq_sort_internal(ecom, ECOMMUNITY_SIZE);
}

/* Parse Extended Communities Attribute in BGP packet.  */
/* 【函数】ecommunity_parse_internal：从报文解析。length 必须能被 size_ecom
 * 整除，否则返回 NULL；去重排序后 intern 返回。
 */
static struct ecommunity *ecommunity_parse_internal(uint8_t *pnt,
						    unsigned short length,
						    unsigned short size_ecom,
						    bool disable_ieee_floating)
{
	struct ecommunity tmp;
	struct ecommunity *new;

	/* Length check.  */
	if (length % size_ecom)
		return NULL;

	/* Prepare tmporary structure for making a new Extended Communities
	   Attribute.  */
	tmp.size = length / size_ecom;
	tmp.val = pnt;
	tmp.disable_ieee_floating = disable_ieee_floating;

	/* Create a new Extended Communities Attribute by uniq and sort each
	   Extended Communities value  */
	new = ecommunity_uniq_sort_internal(&tmp, size_ecom);

	return ecommunity_intern(new);
}

/* 【函数】ecommunity_parse：8 字节版本解析入口。 */
struct ecommunity *ecommunity_parse(uint8_t *pnt, unsigned short length,
				    bool disable_ieee_floating)
{
	return ecommunity_parse_internal(pnt, length, ECOMMUNITY_SIZE,
					 disable_ieee_floating);
}

/* 【函数】ecommunity_parse_ipv6：20 字节版本解析入口。 */
struct ecommunity *ecommunity_parse_ipv6(uint8_t *pnt, unsigned short length)
{
	return ecommunity_parse_internal(pnt, length, IPV6_ECOMMUNITY_SIZE,
					 false);
}

/* Duplicate the Extended Communities Attribute structure.  */
/* 【函数】ecommunity_dup：深拷贝（含 unit_size），refcnt 归零，不驻留。 */
struct ecommunity *ecommunity_dup(struct ecommunity *ecom)
{
	struct ecommunity *new;

	new = XCALLOC(MTYPE_ECOMMUNITY, sizeof(struct ecommunity));
	new->size = ecom->size;
	new->unit_size = ecom->unit_size;
	if (new->size) {
		new->val = XMALLOC(MTYPE_ECOMMUNITY_VAL,
				   ecom->size * ecom->unit_size);
		memcpy(new->val, ecom->val,
		       (size_t)ecom->size * (size_t)ecom->unit_size);
	} else
		new->val = NULL;
	return new;
}

/* Return string representation of ecommunities attribute. */
/* 【函数】ecommunity_str：返回 ecom->str（内部缓存），必要时用 DISPLAY 格式
 * 惰性生成；调用者不得 free。空指针返回字面量 "(null)"。
 */
const char *ecommunity_str(struct ecommunity *ecom)
{
	if (!ecom)
		return "(null)";

	if (!ecom->str)
		ecom->str =
			ecommunity_ecom2str(ecom, ECOMMUNITY_FORMAT_DISPLAY, 0);
	return ecom->str;
}

/* Merge two Extended Communities Attribute structure.  */
/* 【函数】ecommunity_merge：把 ecom2 追加到 ecom1 末尾（原地，按 ecom1 的
 * unit_size 复制）。要求两者 unit_size 一致。
 */
struct ecommunity *ecommunity_merge(struct ecommunity *ecom1,
				    struct ecommunity *ecom2)
{
	ecom1->val = XREALLOC(MTYPE_ECOMMUNITY_VAL, ecom1->val,
			      (size_t)(ecom1->size + ecom2->size)
				      * (size_t)ecom1->unit_size);

	memcpy(ecom1->val + (ecom1->size * ecom1->unit_size), ecom2->val,
	       (size_t)ecom2->size * (size_t)ecom1->unit_size);
	ecom1->size += ecom2->size;

	return ecom1;
}

/* Intern Extended Communities Attribute.  */
/* 【函数】ecommunity_intern：驻留。命中已存者则释放传入对象、返回已有者并
 * refcnt 加一；首个快照会生成 DISPLAY 字符串。
 */
struct ecommunity *ecommunity_intern(struct ecommunity *ecom)
{
	struct ecommunity *find;

	assert(ecom->refcnt == 0);
	find = (struct ecommunity *)hash_get(ecomhash, ecom, hash_alloc_intern);
	if (find != ecom)
		ecommunity_free(&ecom);

	find->refcnt++;

	if (!find->str)
		find->str =
			ecommunity_ecom2str(find, ECOMMUNITY_FORMAT_DISPLAY, 0);

	return find;
}

/* Unintern Extended Communities Attribute.  */
/* 【函数】ecommunity_unintern：refcnt 减一，减到 0 从哈希摘除并释放，指针置 NULL。 */
void ecommunity_unintern(struct ecommunity **ecom)
{
	struct ecommunity *ret;

	if (!*ecom)
		return;

	if ((*ecom)->refcnt)
		(*ecom)->refcnt--;

	/* Pull off from hash.  */
	if ((*ecom)->refcnt == 0) {
		/* Extended community must be in the hash.  */
		ret = (struct ecommunity *)hash_release(ecomhash, *ecom);
		assert(ret != NULL);

		ecommunity_free(ecom);
	}
}

/* Utinity function to make hash key.  */
/* 【函数】ecommunity_hash_make：对 val 的全部字节做 jhash 得到哈希键。 */
unsigned int ecommunity_hash_make(const void *arg)
{
	const struct ecommunity *ecom = arg;
	int size = ecom->size * ecom->unit_size;

	return jhash(ecom->val, size, 0x564321ab);
}

/* Compare two Extended Communities Attribute structure.  */
/* 【函数】ecommunity_cmp：全等比较，先比 unit_size 再比 size 与字节。哈希 key 回调。 */
bool ecommunity_cmp(const void *arg1, const void *arg2)
{
	const struct ecommunity *ecom1 = arg1;
	const struct ecommunity *ecom2 = arg2;

	if (ecom1 == NULL && ecom2 == NULL)
		return true;

	if (ecom1 == NULL || ecom2 == NULL)
		return false;

	if (ecom1->unit_size != ecom2->unit_size)
		return false;

	return (ecom1->size == ecom2->size
		&& memcmp(ecom1->val, ecom2->val, ecom1->size *
			  ecom1->unit_size) == 0);
}

/* 【函数】ecommunity_color_str：把 color 扩展社区打印为 "Color:CX:值"，
 * CX 由 CO 位决定。
 */
static void ecommunity_color_str(char *buf, size_t bufsz, uint8_t *ptr)
{
	/*
	 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *  | 0x03         | Sub-Type(0x0b) | CO|    Flags                  |
	 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *  |                          Color Value                          |
	 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *  https://datatracker.ietf.org/doc/rfc9256/, Section 8.8.1
	 *  The CO bits can have 4 different values: 00 01 10 11
	 */
	uint32_t colorid;
	uint8_t color_type;
	/* get the color type */
	ptr++;
	color_type = (*ptr) >> 6;

	memcpy(&colorid, ptr + 2, 4);
	colorid = ntohl(colorid);
	snprintf(buf, bufsz, "Color:%d%d:%d", (color_type & 0x2) >> 1, color_type & 0x1, colorid);
}

/* Initialize Extended Comminities related hash. */
/* 【函数】ecommunity_init：创建全局哈希表 ecomhash。 */
void ecommunity_init(void)
{
	ecomhash = hash_create(ecommunity_hash_make, ecommunity_cmp,
			       "BGP ecommunity hash");
}

/* 【函数】ecommunity_finish：清空并销毁全局哈希表。 */
void ecommunity_finish(void)
{
	hash_clean_and_free(&ecomhash, (void (*)(void *))ecommunity_hash_free);
}

/* Extended Communities token enum. */
/* 【枚举】ecommunity_token：字符串解析的词法类型（rt / nt / soo / color /
 * 数字值 / IPv6 rt6 / IPv6 值 val6 / unknown）。
 */
enum ecommunity_token {
	ecommunity_token_unknown = 0,
	ecommunity_token_rt,
	ecommunity_token_nt,
	ecommunity_token_soo,
	ecommunity_token_color,
	ecommunity_token_val,
	ecommunity_token_rt6,
	ecommunity_token_val6,
};

/* 【函数】ecommunity_origin_validation_state2str：校验状态枚举转显示字符串。 */
static const char *ecommunity_origin_validation_state2str(
	enum ecommunity_origin_validation_states state)
{
	switch (state) {
	case ECOMMUNITY_ORIGIN_VALIDATION_STATE_VALID:
		return "valid";
	case ECOMMUNITY_ORIGIN_VALIDATION_STATE_NOTFOUND:
		return "not-found";
	case ECOMMUNITY_ORIGIN_VALIDATION_STATE_INVALID:
		return "invalid";
	case ECOMMUNITY_ORIGIN_VALIDATION_STATE_NOTUSED:
		return "not-used";
	}

	return "ERROR";
}

/* 【函数】ecommunity_origin_validation_state_str：打印为 "OVS:valid" 等。
 * 上面英文注释给出了该扩展社区的位布局。
 */
static void ecommunity_origin_validation_state_str(char *buf, size_t bufsz,
						   uint8_t *ptr)
{
	/* Origin Validation State is encoded in the last octet
	 *
	 * 0                   1                   2                   3
	 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |       0x43    |      0x00     |             Reserved          |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |                    Reserved                   |validationstate|
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 */
	uint8_t state = *(ptr + ECOMMUNITY_SIZE - 3);

	snprintf(buf, bufsz, "OVS:%s",
		 ecommunity_origin_validation_state2str(state));

	(void)ptr; /* consume value */
}

/* 【函数】ecommunity_node_target_match：判断集合中是否存在 IP 编码的 node target
 * 且其目标 ID 等于 local_id。
 */
bool ecommunity_node_target_match(struct ecommunity *ecom,
				  struct in_addr *local_id)
{
	uint32_t i;
	bool match = false;

	if (!ecom || !ecom->size)
		return NULL;

	for (i = 0; i < ecom->size; i++) {
		const uint8_t *pnt;
		uint8_t type, sub_type;

		pnt = (ecom->val + (i * ECOMMUNITY_SIZE));
		type = *pnt++;
		sub_type = *pnt++;

		if (type == ECOMMUNITY_ENCODE_IP &&
		    sub_type == ECOMMUNITY_NODE_TARGET) {
			/* Node Target ID is encoded as A.B.C.D:0 */
			if (IPV4_ADDR_SAME((struct in_addr *)pnt, local_id))
				match = true;
			(void)pnt;
		}
	}

	return match;
}

/* 【函数】ecommunity_node_target_str：打印 node target；DISPLAY 格式为
 * "NT:x.x.x.x"，community-list 格式为 "nt x.x.x.x:0"。
 */
static void ecommunity_node_target_str(char *buf, size_t bufsz, uint8_t *ptr,
				       int format)
{
	/*
	 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *  | 0x01 or 0x41 | Sub-Type(0x09) |    Target BGP Identifier      |
	 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *  | Target BGP Identifier (cont.) |           Reserved            |
	 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 */
	struct in_addr node_id = {};

	IPV4_ADDR_COPY(&node_id, (struct in_addr *)ptr);


	snprintfrr(buf, bufsz, "%s%pI4%s",
		   format == ECOMMUNITY_FORMAT_COMMUNITY_LIST ? "nt " : "NT:",
		   &node_id,
		   format == ECOMMUNITY_FORMAT_COMMUNITY_LIST ? ":0" : "");

	(void)ptr; /* consume value */
}

/* 【函数】ecommunity_encode_internal：按 type 与 sub_type 把参数编码为扩展社区，
 * 是各类 encode_* 的分发器。先做取值范围校验（AS 不能超过 BGP_AS_MAX、值不能
 * 超过 UINT16_MAX 等），再根据 type 调用对应编码器。
 */
static int ecommunity_encode_internal(uint8_t type, uint8_t sub_type,
				      int trans, as_t as,
				      struct in_addr *ip,
				      struct in6_addr *ip6,
				      uint32_t val,
				      void *eval_ptr)
{
	struct ecommunity_val *eval = (struct ecommunity_val *)eval_ptr;
	struct ecommunity_val_ipv6 *eval6 =
		(struct ecommunity_val_ipv6 *)eval_ptr;

	assert(eval);
	if (type == ECOMMUNITY_ENCODE_AS) {
		if (as > BGP_AS_MAX)
			return -1;
	} else if (type == ECOMMUNITY_ENCODE_IP
		   || type == ECOMMUNITY_ENCODE_AS4) {
		if (val > UINT16_MAX)
			return -1;
	} else if (type == ECOMMUNITY_ENCODE_TRANS_EXP &&
		   sub_type == ECOMMUNITY_FLOWSPEC_REDIRECT_IPV6 &&
		   (!ip6 || val > UINT16_MAX)) {
		return -1;
	}

	/* Fill in the values. */
	eval->val[0] = type;
	if (!trans)
		SET_FLAG(eval->val[0], ECOMMUNITY_FLAG_NON_TRANSITIVE);
	eval->val[1] = sub_type;
	if (type == ECOMMUNITY_ENCODE_AS) {
		encode_route_target_as(as, val, eval, trans);
	} else if (type == ECOMMUNITY_ENCODE_IP) {
		if (sub_type == ECOMMUNITY_NODE_TARGET)
			encode_node_target(ip, eval, trans);
		else
			encode_route_target_ip(ip, val, eval, trans);
	} else if (type == ECOMMUNITY_ENCODE_TRANS_EXP &&
		   sub_type == ECOMMUNITY_FLOWSPEC_REDIRECT_IPV6) {
		memcpy(&eval6->val[2], ip6, sizeof(struct in6_addr));
		eval6->val[18] = (val >> 8) & 0xff;
		eval6->val[19] = val & 0xff;
	} else if (type == ECOMMUNITY_ENCODE_OPAQUE &&
		   sub_type == ECOMMUNITY_COLOR) {
		encode_color(val, as, eval);
	} else {
		encode_route_target_as4(as, val, eval, trans);
	}

	return 0;
}

/*
 * Encode BGP extended community from passed values. Supports types
 * defined in RFC 4360 and well-known sub-types.
 */
/* 【函数】ecommunity_encode：IPv4 场景的编码封装（8 字节单元）。 */
static int ecommunity_encode(uint8_t type, uint8_t sub_type, int trans, as_t as,
			     struct in_addr ip, uint32_t val,
			     struct ecommunity_val *eval)
{
	return ecommunity_encode_internal(type, sub_type, trans, as,
					  &ip, NULL, val, (void *)eval);
}

/* Get next Extended Communities token from the string. */
/* 【函数】ecommunity_gettoken：扩展社区字符串词法解析，本文件最复杂的一个函数。
 * 先识别关键字节 rt / rt6 / nt / soo / color；否则按数字形式解析，支持：
 *   A.B.C.D:MN（IPv4）、EF:OPQR（2 字节 AS 与 4 字节值）、
 *   GHJK:MN（4 字节 AS 与 2 字节值）、IPv6:MN（仅 rt6）。
 * 通过 dot / separator 计数区分 IP 与 AS，据此选择编码类型；color 还额外解析
 * CO 位。出错则把 token 置为 unknown。
 */
static const char *ecommunity_gettoken(const char *str, void *eval_ptr,
				       enum ecommunity_token *token, int type)
{
	int ret;
	int dot = 0;
	int digit = 0;
	int separator = 0;
	const char *p = str;
	char *endptr;
	struct in_addr ip;
	struct in6_addr ip6;
	as_t as = 0;
	uint32_t val = 0;
	uint32_t val_color = 0;
	uint8_t ecomm_type = 0;
	uint8_t sub_type = 0;
	char buf[INET_ADDRSTRLEN + 1];
	struct ecommunity_val *eval = (struct ecommunity_val *)eval_ptr;
	uint64_t tmp_as = 0;
	static const char *str_color = "color";
	const char *ptr_color;
	bool val_color_set = false;

	/* Skip white space. */
	while (isspace((unsigned char)*p)) {
		p++;
		str++;
	}

	/* Check the end of the line. */
	if (*p == '\0')
		return NULL;

	/* "rt", "nt", "soo", and "color" keyword parse. */
	/* "rt" */
	if (tolower((unsigned char)*p) == 'r') {
		p++;
		if (tolower((unsigned char)*p) == 't') {
			p++;
			if (*p != '\0' && tolower((int)*p) == '6')
				*token = ecommunity_token_rt6;
			else
				*token = ecommunity_token_rt;
			return p;
		}
		if (isspace((unsigned char)*p) || *p == '\0') {
			*token = ecommunity_token_rt;
			return p;
		}
		goto error;
	}

	/* "nt" */
	if (tolower((unsigned char)*p) == 'n') {
		p++;
		if (tolower((unsigned char)*p) == 't') {
			p++;
			*token = ecommunity_token_nt;
			return p;
		}
		if (isspace((unsigned char)*p) || *p == '\0') {
			*token = ecommunity_token_nt;
			return p;
		}
		goto error;
	}

	/* "soo" */
	if (tolower((unsigned char)*p) == 's') {
		p++;
		if (tolower((unsigned char)*p) == 'o') {
			p++;
			if (tolower((unsigned char)*p) == 'o') {
				p++;
				*token = ecommunity_token_soo;
				return p;
			}
			if (isspace((unsigned char)*p) || *p == '\0') {
				*token = ecommunity_token_soo;
				return p;
			}
			goto error;
		}
		if (isspace((unsigned char)*p) || *p == '\0') {
			*token = ecommunity_token_soo;
			return p;
		}
		goto error;
	}

	/* "color" */
	if (tolower((unsigned char)*p) == 'c') {
		ptr_color = &str_color[0];
		for (unsigned int i = 0; i < 5; i++) {
			if (tolower((unsigned char)*p) != *ptr_color)
				break;

			p++;
			ptr_color++;
		}
		if (isspace((unsigned char)*p) || *p == '\0') {
			*token = ecommunity_token_color;
			return p;
		}
		goto error;
	}
	/* What a mess, there are several possibilities:
	 *
	 * a) A.B.C.D:MN
	 * b) EF:OPQR
	 * c) GHJK:MN
	 * d) <IPV6>:MN (only with rt6)
	 *
	 * A.B.C.D: Four Byte IP
	 * EF:      Two byte ASN
	 * GHJK:    Four-byte ASN
	 * MN:      Two byte value
	 * OPQR:    Four byte value
	 *
	 */
	/* IPv6 case : look for last ':' */
	if (*token == ecommunity_token_rt6 ||
	    *token == ecommunity_token_val6) {
		char *limit;

		limit = endptr = strrchr(p, ':');
		if (!endptr)
			goto error;

		endptr++;
		errno = 0;
		tmp_as = strtoul(endptr, &endptr, 10);
		/* 'unsigned long' is a uint64 on 64-bit
		 * systems, and uint32 on 32-bit systems. So for
		 * 64-bit we can just directly check the value
		 * against BGP_AS4_MAX/UINT32_MAX, and for
		 * 32-bit we can check for errno (set to ERANGE
		 * upon overflow).
		 */
		if (*endptr != '\0' || tmp_as == BGP_AS4_MAX || errno)
			goto error;
		as = (as_t)tmp_as;

		memcpy(buf, p, (limit - p));
		buf[limit - p] = '\0';
		ret = inet_pton(AF_INET6, buf, &ip6);
		if (ret == 0)
			goto error;

		ecomm_type = ECOMMUNITY_ENCODE_TRANS_EXP;
		if (ecommunity_encode_internal(ecomm_type,
					ECOMMUNITY_FLOWSPEC_REDIRECT_IPV6,
					1, 0, NULL, &ip6, as, eval_ptr))
			goto error;

		*token = ecommunity_token_val6;
		while (isdigit((int)*p) || *p == ':' || *p == '.') {
			p++;
		}
		return p;
	}
	while (isdigit((unsigned char)*p) || *p == ':' || *p == '.') {
		if (*p == ':') {
			if (separator)
				goto error;

			separator = 1;
			digit = 0;

			if ((p - str) > INET_ADDRSTRLEN)
				goto error;
			memset(buf, 0, INET_ADDRSTRLEN + 1);
			memcpy(buf, str, p - str);

			if (dot == 3) {
				/* Parsing A.B.C.D in:
				 * A.B.C.D:MN
				 */
				ret = inet_aton(buf, &ip);
				if (ret == 0)
					goto error;
			} else if (dot == 1) {
				/* Parsing A.B AS number in:
				 * A.B:MN
				 */
				if (!asn_str2asn(buf, &as))
					goto error;
			} else if (type == ECOMMUNITY_COLOR) {
				/* If extcommunity is color, only support 00/01/10/11, max value is 3 */
				/* color value */
				as = strtoul(buf, &endptr, 2);
				if (*endptr != '\0' || as > 3)
					goto error;
				val_color = 0;
			} else {
				/* Parsing A AS number in A:MN */
				errno = 0;
				tmp_as = strtoul(buf, &endptr, 10);
				/* 'unsigned long' is a uint64 on 64-bit
				 * systems, and uint32 on 32-bit systems. So for
				 * 64-bit we can just directly check the value
				 * against BGP_AS4_MAX/UINT32_MAX, and for
				 * 32-bit we can check for errno (set to ERANGE
				 * upon overflow).
				 */
				if (*endptr != '\0' || tmp_as > BGP_AS4_MAX ||
				    errno)
					goto error;
				if (*token == ecommunity_token_color && tmp_as > 3)
					goto error;
				as = (as_t)tmp_as;
			}
		} else if (*p == '.') {
			if (separator)
				goto error;
			/* either IP or AS format */
			dot++;
			if (dot > 1)
				ecomm_type = ECOMMUNITY_ENCODE_IP;
			if (dot >= 4)
				goto error;
		} else {
			digit = 1;

			/* We're past the IP/ASN part,
			 * or we have a color
			 */
			if (separator) {
				val *= 10;
				val += (*p - '0');
				val_color_set = false;
			} else {
				val_color *= 10;
				val_color += (*p - '0');
				val_color_set = true;
			}
		}
		p++;
	}

	/* Low digit part must be there. */
	if (!digit && (!separator || !val_color_set))
		goto error;

	if (ecomm_type != ECOMMUNITY_ENCODE_IP) {
		/* Encode result into extended community for AS format or color.  */
		if (as > BGP_AS_MAX)
			ecomm_type = ECOMMUNITY_ENCODE_AS4;
		else if (type == ECOMMUNITY_COLOR) {
			ecomm_type = ECOMMUNITY_ENCODE_OPAQUE;
			sub_type = ECOMMUNITY_COLOR;
			if (val_color) {
				val = val_color;
				as = 1;
			}
		} else if (as > 0)
			ecomm_type = ECOMMUNITY_ENCODE_AS;
	}
	if (ecommunity_encode(ecomm_type, sub_type, 1, as, ip, val, eval))
		goto error;
	*token = ecommunity_token_val;
	return p;

error:
	*token = ecommunity_token_unknown;
	return p;
}

/* 【函数】ecommunity_str2com_internal：循环 gettoken 组装集合。
 * keyword_included 表示字符串自带 "rt" / "soo" 等关键字（extcommunity-list 场景），
 * 否则需由参数 type 指定语义（route-map 场景）。返回未驻留对象。
 */
static struct ecommunity *ecommunity_str2com_internal(const char *str, int type,
						      int keyword_included,
						      bool is_ipv6_extcomm)
{
	struct ecommunity *ecom = NULL;
	enum ecommunity_token token = ecommunity_token_unknown;
	struct ecommunity_val_ipv6 eval;
	int keyword = 0;

	if (is_ipv6_extcomm)
		token = ecommunity_token_rt6;
	while ((str = ecommunity_gettoken(str, (void *)&eval, &token, type))) {
		switch (token) {
		case ecommunity_token_rt:
		case ecommunity_token_nt:
		case ecommunity_token_rt6:
		case ecommunity_token_soo:
		case ecommunity_token_color:
			if (!keyword_included || keyword) {
				if (ecom)
					ecommunity_free(&ecom);
				return NULL;
			}
			keyword = 1;

			if (token == ecommunity_token_rt ||
			    token == ecommunity_token_rt6)
				type = ECOMMUNITY_ROUTE_TARGET;
			if (token == ecommunity_token_soo)
				type = ECOMMUNITY_SITE_ORIGIN;
			if (token == ecommunity_token_nt)
				type = ECOMMUNITY_NODE_TARGET;
			if (token == ecommunity_token_color)
				type = ECOMMUNITY_COLOR;
			break;
		case ecommunity_token_val:
			if (keyword_included) {
				if (!keyword) {
					ecommunity_free(&ecom);
					return NULL;
				}
				keyword = 0;
			}
			if (ecom == NULL)
				ecom = ecommunity_new();
			eval.val[1] = type;
			ecommunity_add_val_internal(ecom, (void *)&eval,
						    false, false,
						    ecom->unit_size);
			break;
		case ecommunity_token_val6:
			if (keyword_included) {
				if (!keyword) {
					ecommunity_free(&ecom);
					return NULL;
				}
				keyword = 0;
			}
			if (ecom == NULL)
				ecom = ecommunity_new();
			ecom->unit_size = IPV6_ECOMMUNITY_SIZE;
			eval.val[1] = type;
			ecommunity_add_val_internal(ecom, (void *)&eval, false, false,
						    ecom->unit_size);
			break;
		case ecommunity_token_unknown:
			if (ecom)
				ecommunity_free(&ecom);
			return NULL;
		}
	}
	return ecom;
}

/* Convert string to extended community attribute.
 *
 * When type is already known, please specify both str and type.  str
 * should not include keyword such as "rt" and "soo".  Type is
 * ECOMMUNITY_ROUTE_TARGET or ECOMMUNITY_SITE_ORIGIN.
 * keyword_included should be zero.
 *
 * For example route-map's "set extcommunity" command case:
 *
 * "rt 100:1 100:2 100:3"        -> str = "100:1 100:2 100:3"
 *				    type = ECOMMUNITY_ROUTE_TARGET
 *				    keyword_included = 0
 *
 * "soo 100:1"                   -> str = "100:1"
 *				    type = ECOMMUNITY_SITE_ORIGIN
 *				    keyword_included = 0
 *
 * When string includes keyword for each extended community value.
 * Please specify keyword_included as non-zero value.
 *
 * For example standard extcommunity-list case:
 *
 * "rt 100:1 rt 100:2 soo 100:1" -> str = "rt 100:1 rt 100:2 soo 100:1"
 *				    type = 0
 *				    keyword_include = 1
 */
/* 【函数】ecommunity_str2com：普通（8 字节）字符串转集合入口。上面的英文注释
 * 给出了 route-map 与 extcommunity-list 两类调用示例。
 */
struct ecommunity *ecommunity_str2com(const char *str, int type,
				      int keyword_included)
{
	return ecommunity_str2com_internal(str, type,
					   keyword_included, false);
}

/* 【函数】ecommunity_str2com_ipv6：IPv6（20 字节）字符串转集合入口。 */
struct ecommunity *ecommunity_str2com_ipv6(const char *str, int type,
					   int keyword_included)
{
	return ecommunity_str2com_internal(str, type,
					   keyword_included, true);
}

/* 【函数】ecommunity_rt_soo_str_internal：把 RT / SoO 打印成字符串。按 format
 * 决定前缀（"rt " / "soo "、"RT:" / "SoO:" 或空）；按 type 决定取 AS 还是 IP；
 * 当 ecom_size 为 20 时按 IPv6 地址取值。
 */
static int ecommunity_rt_soo_str_internal(char *buf, size_t bufsz,
					  const uint8_t *pnt, int type,
					  int sub_type, int format,
					  unsigned short ecom_size)
{
	int len = 0;
	const char *prefix;
	char buf_local[INET6_ADDRSTRLEN];

	/* For parse Extended Community attribute tuple. */
	struct ecommunity_as eas;
	struct ecommunity_ip eip;
	struct ecommunity_ip6 eip6;

	/* Determine prefix for string, if any. */
	switch (format) {
	case ECOMMUNITY_FORMAT_COMMUNITY_LIST:
		prefix = (sub_type == ECOMMUNITY_ROUTE_TARGET ? "rt " : "soo ");
		break;
	case ECOMMUNITY_FORMAT_DISPLAY:
		prefix = (sub_type == ECOMMUNITY_ROUTE_TARGET ? "RT:" : "SoO:");
		break;
	case ECOMMUNITY_FORMAT_ROUTE_MAP:
		prefix = "";
		break;
	default:
		prefix = "";
		break;
	}

	/* Put string into buffer.  */
	if (type == ECOMMUNITY_ENCODE_AS4) {
		pnt = ptr_get_be32(pnt, &eas.as);
		eas.val = (*pnt++ << 8);
		eas.val |= (*pnt++);

		len = snprintf(buf, bufsz, "%s%u:%u", prefix, eas.as, eas.val);
	} else if (type == ECOMMUNITY_ENCODE_AS) {
		if (ecom_size == ECOMMUNITY_SIZE) {
			eas.as = (*pnt++ << 8);
			eas.as |= (*pnt++);
			pnt = ptr_get_be32(pnt, &eas.val);

			len = snprintf(buf, bufsz, "%s%u:%u", prefix, eas.as,
				       eas.val);
		} else {
			/* this is an IPv6 ext community
			 * first 16 bytes stands for IPv6 address
			 */
			memcpy(&eip6.ip, pnt, 16);
			pnt += 16;
			eip6.val = (*pnt++ << 8);
			eip6.val |= (*pnt++);

			inet_ntop(AF_INET6, &eip6.ip, buf_local,
				  sizeof(buf_local));
			len = snprintf(buf, bufsz, "%s%s:%u", prefix,
				       buf_local, eip6.val);
		}
	} else if (type == ECOMMUNITY_ENCODE_IP) {
		memcpy(&eip.ip, pnt, 4);
		pnt += 4;
		eip.val = (*pnt++ << 8);
		eip.val |= (*pnt++);

		len = snprintfrr(buf, bufsz, "%s%pI4:%u", prefix, &eip.ip,
				 eip.val);
	}

	/* consume value */
	(void)pnt;

	return len;
}

/* 【函数】ecommunity_rt_soo_str：8 字节版本的 RT / SoO 打印封装。 */
static int ecommunity_rt_soo_str(char *buf, size_t bufsz, const uint8_t *pnt,
				 int type, int sub_type, int format)
{
	return ecommunity_rt_soo_str_internal(buf, bufsz, pnt, type,
					      sub_type, format,
					      ECOMMUNITY_SIZE);
}

/* Helper function to convert IEEE-754 Floating Point to uint32 */
/* 【函数】ieee_float_uint32_to_uint32：把 IEEE-754 单精度浮点的位模式还原为整数
 * （与 bgp_ecommunity.h 中的 uint32_to_ieee_float_uint32 相反）。
 */
static uint32_t ieee_float_uint32_to_uint32(uint32_t u)
{
	union {
		float r;
		uint32_t d;
	} f = {.d = u};

	return (uint32_t)f.r;
}

/* 【函数】ecommunity_lb_str：打印 link-bandwidth（subtype 0x04），
 * 形如 "LB:AS:字节数 (N.NNN Gbps)"；disable_ieee_floating 决定带宽按整数还是
 * 浮点解释，并按量级选择 Gbps / Mbps / Kbps / bps 单位。
 */
static int ecommunity_lb_str(char *buf, size_t bufsz, const uint8_t *pnt,
			     bool disable_ieee_floating)
{
	int len = 0;
	as_t as;
	uint32_t bw_tmp, bw;
	char bps_buf[20] = {0};

	as = (*pnt++ << 8);
	as |= (*pnt++);
	(void)ptr_get_be32(pnt, &bw_tmp);

	bw = disable_ieee_floating ? bw_tmp
				   : ieee_float_uint32_to_uint32(bw_tmp);

	if (bw >= ONE_GBPS_BYTES)
		snprintf(bps_buf, sizeof(bps_buf), "%.3f Gbps",
			 (float)(bw / ONE_GBPS_BYTES));
	else if (bw >= ONE_MBPS_BYTES)
		snprintf(bps_buf, sizeof(bps_buf), "%.3f Mbps",
			 (float)(bw / ONE_MBPS_BYTES));
	else if (bw >= ONE_KBPS_BYTES)
		snprintf(bps_buf, sizeof(bps_buf), "%.3f Kbps",
			 (float)(bw / ONE_KBPS_BYTES));
	else
		snprintf(bps_buf, sizeof(bps_buf), "%u bps", bw * 8);

	len = snprintf(buf, bufsz, "LB:%u:%u (%s)", as, bw, bps_buf);
	return len;
}

/* 【函数】ipv6_ecommunity_lb_str：打印 extended-link-bandwidth（20 字节，
 * 8 字节带宽与 4 字节 AS）。
 */
static int ipv6_ecommunity_lb_str(char *buf, size_t bufsz, const uint8_t *pnt,
				  size_t length)
{
	int len = 0;
	as_t as = 0;
	uint64_t bw = 0;
	char bps_buf[20] = { 0 };

	if (length < IPV6_ECOMMUNITY_SIZE)
		goto done;

	pnt += 2; /* Reserved */
	pnt = ptr_get_be64(pnt, &bw);
	(void)ptr_get_be32(pnt, &as);

	if (bw >= ONE_GBPS_BYTES)
		snprintf(bps_buf, sizeof(bps_buf), "%.3f Gbps",
			 (float)(bw / ONE_GBPS_BYTES));
	else if (bw >= ONE_MBPS_BYTES)
		snprintf(bps_buf, sizeof(bps_buf), "%.3f Mbps",
			 (float)(bw / ONE_MBPS_BYTES));
	else if (bw >= ONE_KBPS_BYTES)
		snprintf(bps_buf, sizeof(bps_buf), "%.3f Kbps",
			 (float)(bw / ONE_KBPS_BYTES));
	else
		snprintfrr(bps_buf, sizeof(bps_buf), "%" PRIu64 " bps", bw * 8);

done:
	len = snprintfrr(buf, bufsz, "LB:%u:%" PRIu64 " (%s)", as, bw, bps_buf);
	return len;
}

/* 【函数】ecommunity_has_route_target：判断集合中是否含 RT（在 AS / IP / AS4
 * 三种编码空间里找 subtype 为 ECOMMUNITY_ROUTE_TARGET 的单元）。
 */
bool ecommunity_has_route_target(struct ecommunity *ecom)
{
	uint32_t i;
	uint8_t *pnt;
	uint8_t type = 0;
	uint8_t sub_type = 0;

	if (!ecom)
		return false;
	for (i = 0; i < ecom->size; i++) {
		/* Retrieve value field */
		pnt = ecom->val + (i * ecom->unit_size);

		/* High-order octet is the type */
		type = *pnt++;

		if (type == ECOMMUNITY_ENCODE_AS ||
		    type == ECOMMUNITY_ENCODE_IP ||
		    type == ECOMMUNITY_ENCODE_AS4) {
			/* Low-order octet of type. */
			sub_type = *pnt++;
			if (sub_type == ECOMMUNITY_ROUTE_TARGET)
				return true;
		}
	}
	return false;
}

/* Convert extended community attribute to string.
 * Due to historical reason of industry standard implementation, there
 * are three types of format:
 *
 * route-map set extcommunity format:
 *     "rt 100:1 100:2soo 100:3"
 *
 * extcommunity-list:
 *     "rt 100:1 rt 100:2 soo 100:3"
 *
 * show bgp:
 *     "RT:100:1 RT:100:2 SoO:100:3"
 *
 * For each format please use below definition for format:
 *     ECOMMUNITY_FORMAT_ROUTE_MAP
 *     ECOMMUNITY_FORMAT_COMMUNITY_LIST
 *     ECOMMUNITY_FORMAT_DISPLAY
 *
 * Filter is added to display only ECOMMUNITY_ROUTE_TARGET in some cases.
 * 0 value displays all.
 * Index is a unsigned integer value, and stands for the extended community list entry
 * to display when value is not -1.
 */
/* 【函数】_ecommunity_ecom2str：打印扩展社区集合的总入口，也是本文件最长、
 * 分支最多的函数。逐单元处理：type 决定大类，subtype 决定细节，拼成可读文本。
 *   filter 为 ECOMMUNITY_ROUTE_TARGET 时跳过 opaque 与 EVPN；
 *   index 不为 -1 时只打印第 index 个单元。
 * 返回新分配字符串（需用 ecommunity_strfree 释放）。
 * 主要 subtype 分支：RT / SoO、link-bandwidth、node target、color、EVPN 的
 * Router MAC / MAC mobility / ND / ES-Import RT / ESI label / DF / L2 attr、
 * FlowSpec 的 redirect / action / rate / marking，以及 OVS 等。
 */
static char *_ecommunity_ecom2str(struct ecommunity *ecom, int format, int filter, int index)
{
	uint32_t i;
	uint8_t *pnt;
	uint8_t type = 0;
	uint8_t sub_type = 0;
	int str_size;
	char *str_buf;

	if (!ecom || ecom->size == 0)
		return XCALLOC(MTYPE_ECOMMUNITY_STR, 1);

	/* ecom strlen + space + null term */
	str_size = (ecom->size * (ECOMMUNITY_STRLEN + 1)) + 1;
	str_buf = XCALLOC(MTYPE_ECOMMUNITY_STR, str_size);

	char encbuf[128];

	for (i = 0; i < ecom->size; i++) {
		bool unk_ecom = false;
		memset(encbuf, 0x00, sizeof(encbuf));

		if (index != -1 && (uint32_t)index != i)
			continue;
		/* Space between each value.  */
		if (index == -1 && i > 0)
			strlcat(str_buf, " ", str_size);

		/* Retrieve value field */
		pnt = ecom->val + (i * ecom->unit_size);

		uint8_t *data = pnt;
		uint8_t *end = data + ecom->unit_size;
		size_t len = end - data;

		/* Sanity check for extended communities lenght, to avoid
		 * overrun when dealing with bits, e.g. ptr_get_be64().
		 */
		if (len < ecom->unit_size) {
			unk_ecom = true;
			goto unknown;
		}

		/* High-order octet is the type */
		type = *pnt++;

		if (type == ECOMMUNITY_ENCODE_AS || type == ECOMMUNITY_ENCODE_IP
		    || type == ECOMMUNITY_ENCODE_AS4) {
			/* Low-order octet of type. */
			sub_type = *pnt++;
			if (sub_type != ECOMMUNITY_ROUTE_TARGET
			    && sub_type != ECOMMUNITY_SITE_ORIGIN) {
				if (sub_type ==
				    ECOMMUNITY_FLOWSPEC_REDIRECT_IPV4 &&
				    type == ECOMMUNITY_ENCODE_IP) {
					struct in_addr *ipv4 =
						(struct in_addr *)pnt;
					snprintfrr(encbuf, sizeof(encbuf),
						   "NH:%pI4:%d", ipv4, pnt[5]);
				} else if (sub_type ==
					   ECOMMUNITY_LINK_BANDWIDTH &&
					   type == ECOMMUNITY_ENCODE_AS) {
					ecommunity_lb_str(
						encbuf, sizeof(encbuf), pnt,
						ecom->disable_ieee_floating);
				} else if (sub_type ==
						   ECOMMUNITY_EXTENDED_LINK_BANDWIDTH &&
					   type == ECOMMUNITY_ENCODE_AS4) {
					ipv6_ecommunity_lb_str(encbuf,
							       sizeof(encbuf),
							       pnt, len);
				} else if (sub_type == ECOMMUNITY_NODE_TARGET &&
					   type == ECOMMUNITY_ENCODE_IP) {
					ecommunity_node_target_str(
						encbuf, sizeof(encbuf), pnt,
						format);
				} else
					unk_ecom = true;
			} else {
				ecommunity_rt_soo_str(encbuf, sizeof(encbuf),
						      pnt, type, sub_type,
						      format);
			}
		} else if (type == ECOMMUNITY_ENCODE_OPAQUE) {
			if (filter == ECOMMUNITY_ROUTE_TARGET)
				continue;
			if (*pnt == ECOMMUNITY_OPAQUE_SUBTYPE_ENCAP) {
				bgp_encap_types tunneltype;

				if (ecommunity_tunnel_type(ecom, i, &tunneltype))
					snprintf(encbuf, sizeof(encbuf), "ET:%d", tunneltype);
			} else if (*pnt == ECOMMUNITY_EVPN_SUBTYPE_DEF_GW) {
				strlcpy(encbuf, "Default Gateway",
					sizeof(encbuf));
			} else if (*pnt == ECOMMUNITY_COLOR) {
				ecommunity_color_str(encbuf, sizeof(encbuf),
						     pnt);
			} else {
				unk_ecom = true;
			}
		} else if (type == ECOMMUNITY_ENCODE_EVPN) {
			if (filter == ECOMMUNITY_ROUTE_TARGET)
				continue;
			if (*pnt == ECOMMUNITY_EVPN_SUBTYPE_ROUTERMAC) {
				struct ethaddr rmac;
				pnt++;
				memcpy(&rmac, pnt, ETH_ALEN);

				snprintf(encbuf, sizeof(encbuf),
					 "Rmac:%02x:%02x:%02x:%02x:%02x:%02x",
					 (uint8_t)rmac.octet[0],
					 (uint8_t)rmac.octet[1],
					 (uint8_t)rmac.octet[2],
					 (uint8_t)rmac.octet[3],
					 (uint8_t)rmac.octet[4],
					 (uint8_t)rmac.octet[5]);
			} else if (*pnt
				   == ECOMMUNITY_EVPN_SUBTYPE_MACMOBILITY) {
				uint32_t seqnum;
				uint8_t flags = *++pnt;

				memcpy(&seqnum, pnt + 2, 4);
				seqnum = ntohl(seqnum);

				snprintf(encbuf, sizeof(encbuf), "MM:%u",
					 seqnum);

				if (CHECK_FLAG(
					    flags,
					    ECOMMUNITY_EVPN_SUBTYPE_MACMOBILITY_FLAG_STICKY))
					strlcat(encbuf, ", sticky MAC",
						sizeof(encbuf));
			} else if (*pnt == ECOMMUNITY_EVPN_SUBTYPE_ND) {
				uint8_t flags = *++pnt;

				if (CHECK_FLAG(
					    flags,
					    ECOMMUNITY_EVPN_SUBTYPE_ND_ROUTER_FLAG))
					strlcpy(encbuf, "ND:Router Flag",
						sizeof(encbuf));
				if (CHECK_FLAG(
					    flags,
					    ECOMMUNITY_EVPN_SUBTYPE_PROXY_FLAG))
					strlcpy(encbuf, "ND:Proxy",
						sizeof(encbuf));
			} else if (*pnt
				   == ECOMMUNITY_EVPN_SUBTYPE_ES_IMPORT_RT) {
				struct ethaddr mac;

				pnt++;
				memcpy(&mac, pnt, ETH_ALEN);
				snprintf(encbuf,
					sizeof(encbuf),
					"ES-Import-Rt:%02x:%02x:%02x:%02x:%02x:%02x",
					(uint8_t)mac.octet[0],
					(uint8_t)mac.octet[1],
					(uint8_t)mac.octet[2],
					(uint8_t)mac.octet[3],
					(uint8_t)mac.octet[4],
					(uint8_t)mac.octet[5]);
			} else if (*pnt
				   == ECOMMUNITY_EVPN_SUBTYPE_ESI_LABEL) {
				uint8_t flags = *++pnt;

				snprintf(encbuf, sizeof(encbuf),
					 "ESI-label-Rt:%s",
					 CHECK_FLAG(flags,
						    ECOMMUNITY_EVPN_SUBTYPE_ESI_SA_FLAG)
						 ? "SA"
						 : "AA");
			} else if (*pnt
				   == ECOMMUNITY_EVPN_SUBTYPE_DF_ELECTION) {
				uint8_t alg;
				uint16_t pref;
				uint16_t bmap;

				alg = *(pnt + 1);
				memcpy(&bmap, pnt + 2, 2);
				bmap = ntohs(bmap);
				memcpy(&pref, pnt + 5, 2);
				pref = ntohs(pref);

				if (bmap)
					snprintf(
						encbuf, sizeof(encbuf),
						"DF: (alg: %u, bmap: 0x%x pref: %u)",
						alg, bmap, pref);
				else
					snprintf(encbuf, sizeof(encbuf),
						 "DF: (alg: %u, pref: %u)", alg,
						 pref);
			} else if (*pnt == ECOMMUNITY_EVPN_SUBTYPE_LAYER2_ATTR) {
				uint16_t flags, l2mtu;

				++pnt;
				memcpy(&flags, pnt, 2);
				++pnt;
				++pnt;

				memcpy(&l2mtu, pnt, 2);

				snprintf(encbuf, sizeof(encbuf),
					 "L2: P flag:%c, B Flag %c, C word %c, MTU %d",
					 CHECK_FLAG(flags,
						    ECOMMUNITY_EVPN_SUBTYPE_LAYER2_ATTR_PRIMARY_PE_FLAG)
						 ? 'Y'
						 : 'N',
					 CHECK_FLAG(flags,
						    ECOMMUNITY_EVPN_SUBTYPE_LAYER2_ATTR_BACKUP_PE_FLAG)
						 ? 'Y'
						 : 'N',
					 CHECK_FLAG(flags,
						    ECOMMUNITY_EVPN_SUBTYPE_LAYER2_ATTR_CONTROL_WORD_FLAG)
						 ? 'Y'
						 : 'N',
					 l2mtu);
			} else
				unk_ecom = true;
		} else if (type == ECOMMUNITY_ENCODE_REDIRECT_IP_NH) {
			sub_type = *pnt++;
			if (sub_type == ECOMMUNITY_REDIRECT_IP_NH) {
				snprintf(encbuf, sizeof(encbuf),
					 "FS:redirect IP 0x%x", *(pnt + 5));
			} else
				unk_ecom = true;
		} else if (type == ECOMMUNITY_ENCODE_TRANS_EXP ||
			   type == ECOMMUNITY_EXTENDED_COMMUNITY_PART_2 ||
			   type == ECOMMUNITY_EXTENDED_COMMUNITY_PART_3) {
			sub_type = *pnt++;

			if (sub_type == ECOMMUNITY_ROUTE_TARGET) {
				char buf[ECOMMUNITY_STRLEN];

				memset(buf, 0, sizeof(buf));
				ecommunity_rt_soo_str_internal(
					buf, sizeof(buf), (const uint8_t *)pnt,
					CHECK_FLAG(type,
						   ~ECOMMUNITY_ENCODE_TRANS_EXP),
					ECOMMUNITY_ROUTE_TARGET, format,
					ecom->unit_size);
				snprintf(encbuf, sizeof(encbuf), "%s", buf);
			} else if (sub_type ==
				   ECOMMUNITY_FLOWSPEC_REDIRECT_IPV6) {
				char buf[64];

				memset(buf, 0, sizeof(buf));
				ecommunity_rt_soo_str_internal(
					buf, sizeof(buf), (const uint8_t *)pnt,
					CHECK_FLAG(type,
						   ~ECOMMUNITY_ENCODE_TRANS_EXP),
					ECOMMUNITY_ROUTE_TARGET,
					ECOMMUNITY_FORMAT_DISPLAY,
					ecom->unit_size);
				snprintf(encbuf, sizeof(encbuf),
					 "FS:redirect VRF %s", buf);
			} else if (sub_type == ECOMMUNITY_REDIRECT_VRF) {
				char buf[16];

				memset(buf, 0, sizeof(buf));
				ecommunity_rt_soo_str(
					buf, sizeof(buf), (const uint8_t *)pnt,
					CHECK_FLAG(type,
						   ~ECOMMUNITY_ENCODE_TRANS_EXP),
					ECOMMUNITY_ROUTE_TARGET,
					ECOMMUNITY_FORMAT_DISPLAY);
				snprintf(encbuf, sizeof(encbuf),
					 "FS:redirect VRF %s", buf);
				snprintf(encbuf, sizeof(encbuf),
					 "FS:redirect VRF %s", buf);
			} else if (type != ECOMMUNITY_ENCODE_TRANS_EXP)
				unk_ecom = true;
			else if (sub_type == ECOMMUNITY_TRAFFIC_ACTION) {
				char action[64];

				if (*(pnt+3) ==
				    1 << FLOWSPEC_TRAFFIC_ACTION_TERMINAL)
					strlcpy(action, "terminate (apply)",
						sizeof(action));
				else
					strlcpy(action, "eval stops",
						sizeof(action));

				if (*(pnt+3) ==
				    1 << FLOWSPEC_TRAFFIC_ACTION_SAMPLE)
					strlcat(action, ", sample",
						sizeof(action));


				snprintf(encbuf, sizeof(encbuf), "FS:action %s",
					 action);
			} else if (sub_type == ECOMMUNITY_TRAFFIC_RATE) {
				union traffic_rate rate;

				rate.rate_byte[3] = *(pnt + 2);
				rate.rate_byte[2] = *(pnt + 3);
				rate.rate_byte[1] = *(pnt + 4);
				rate.rate_byte[0] = *(pnt + 5);
				snprintf(encbuf, sizeof(encbuf), "FS:rate %f",
					 rate.rate_float);
			} else if (sub_type == ECOMMUNITY_TRAFFIC_MARKING) {
				snprintf(encbuf, sizeof(encbuf),
					 "FS:marking %u", *(pnt + 5));
			} else
				unk_ecom = true;
		} else if (CHECK_FLAG(type, ECOMMUNITY_FLAG_NON_TRANSITIVE) ||
			   type == ECOMMUNITY_ENCODE_OPAQUE_NON_TRANS) {
			sub_type = *pnt++;
			if (sub_type == ECOMMUNITY_ORIGIN_VALIDATION_STATE)
				ecommunity_origin_validation_state_str(encbuf, sizeof(encbuf), pnt);
			else if (sub_type == ECOMMUNITY_LINK_BANDWIDTH)
				ecommunity_lb_str(encbuf, sizeof(encbuf), pnt,
						  ecom->disable_ieee_floating);
			else if (sub_type == ECOMMUNITY_EXTENDED_LINK_BANDWIDTH)
				ipv6_ecommunity_lb_str(encbuf, sizeof(encbuf),
						       pnt, len);
			else if (sub_type == ECOMMUNITY_OPAQUE_SUBTYPE_COLOR) {
				uint32_t color;
				/* get the color type */
				uint8_t color_type = (*pnt) >> 6;
				memcpy(&color, pnt + 2, 4);
				color = ntohl(color);
				snprintf(encbuf, sizeof(encbuf), "Color:%d%d:%u",
					 (color_type & 0x2) >> 1, color_type & 0x1, color);
			} else
				unk_ecom = true;
		} else if (type == ECOMMUNITY_ENCODE_IP_NON_TRANS) {
			sub_type = *pnt++;
			if (sub_type == ECOMMUNITY_NODE_TARGET)
				ecommunity_node_target_str(
					encbuf, sizeof(encbuf), pnt, format);
			else
				unk_ecom = true;
		} else {
			sub_type = *pnt++;
			unk_ecom = true;
		}

unknown:
		if (unk_ecom)
			snprintf(encbuf, sizeof(encbuf), "UNK:%d, %d", type,
				 sub_type);

		int r = strlcat(str_buf, encbuf, str_size);

		assert(r < str_size);
	}

	return str_buf;
}

/* 【函数】ecommunity_ecom2str：打印全部单元。返回新分配字符串。 */
char *ecommunity_ecom2str(struct ecommunity *ecom, int format, int filter)
{
	return _ecommunity_ecom2str(ecom, format, filter, -1);
}

/* 【函数】ecommunity_ecom2str_one：只打印第 number 个单元。返回新分配字符串。 */
char *ecommunity_ecom2str_one(struct ecommunity *ecom, int format, int number)
{
	return _ecommunity_ecom2str(ecom, format, 0, number);
}

/* 【函数】ecommunity_include_one：判断某单个单元 ptr 是否在集合中。 */
bool ecommunity_include_one(struct ecommunity *ecom, uint8_t *ptr)
{
	uint32_t i;
	uint8_t *ecom_ptr;

	for (i = 0; i < ecom->size; i++) {
		ecom_ptr = ecom->val + (i * ecom->unit_size);
		if (memcmp(ptr, ecom_ptr, ecom->unit_size) == 0)
			return true;
	}
	return false;
}

/* 【函数】ecommunity_include：判断 e1 与 e2 是否有任意一个单元相同（交集非空）。 */
bool ecommunity_include(struct ecommunity *e1, struct ecommunity *e2)
{
	uint32_t i, j;

	if (!e1 || !e2)
		return false;
	for (i = 0; i < e1->size; ++i) {
		for (j = 0; j < e2->size; ++j) {
			if (!memcmp(e1->val + (i * e1->unit_size),
				    e2->val + (j * e2->unit_size),
				    e1->unit_size))
				return true;
		}
	}
	return false;
}

/* 【函数】ecommunity_match：判断 ecom2 是否为 ecom1 的子集。归并式遍历实现。 */
bool ecommunity_match(const struct ecommunity *ecom1,
		      const struct ecommunity *ecom2)
{
	uint32_t i = 0;
	uint32_t j = 0;

	if (ecom1 == NULL && ecom2 == NULL)
		return true;

	if (ecom1 == NULL || ecom2 == NULL)
		return false;

	if (ecom1->size < ecom2->size)
		return false;

	/* Every community on com2 needs to be on com1 for this to match */
	while (i < ecom1->size && j < ecom2->size) {
		if (memcmp(ecom1->val + i * ecom1->unit_size,
			   ecom2->val + j * ecom2->unit_size,
			   ecom2->unit_size)
		    == 0)
			j++;
		i++;
	}

	if (j == ecom2->size)
		return true;
	else
		return false;
}

/* return last occurrence of color */
/* it will be the greatest color value */
/* 【函数】ecommunity_select_color：返回集合中最后一个 color 的值（英文注释说即
 * 最大 color 值）。
 */
extern uint32_t ecommunity_select_color(const struct ecommunity *ecom)
{

	uint32_t aux_color = 0;
	uint8_t *p;
	uint32_t c = 0;

	/* If the value already exists in the structure return 0.  */

	for (p = ecom->val; c < ecom->size; p += ecom->unit_size, c++) {
		if (p == NULL)
			break;

		if (p[0] == ECOMMUNITY_ENCODE_OPAQUE &&
		    p[1] == ECOMMUNITY_COLOR)
			ptr_get_be32((const uint8_t *)&p[4], &aux_color);
	}
	return aux_color;
}


/* return first occurrence of type */
/* 【函数】ecommunity_lookup：返回第一个 type 与 subtype 匹配的单元指针，
 * 找不到返回 NULL。
 */
extern struct ecommunity_val *ecommunity_lookup(const struct ecommunity *ecom,
						uint8_t type, uint8_t subtype)
{
	uint8_t *p;
	uint32_t c;

	/* If the value already exists in the structure return 0.  */
	c = 0;
	for (p = ecom->val; c < ecom->size; p += ecom->unit_size, c++) {
		if (p == NULL) {
			continue;
		}
		if (p[0] == type && p[1] == subtype)
			return (struct ecommunity_val *)p;
	}
	return NULL;
}

/* remove ext. community matching type and subtype
 * return 1 on success ( removed ), 0 otherwise (not present)
 */
/* 【函数】ecommunity_strip：删除所有匹配 type 与 subtype 的单元。
 * 保留其余单元原顺序并重建 val 数组；全部匹配时直接清空。返回是否有删除。
 */
bool ecommunity_strip(struct ecommunity *ecom, uint8_t type,
		      uint8_t subtype)
{
	uint8_t *p, *q, *new;
	uint32_t c, found = 0;
	/* When this is fist value, just add it.  */
	if (ecom == NULL || ecom->val == NULL)
		return false;

	/* Check if any existing ext community matches. */
	/* Certain extended communities like the Route Target can be present
	 * multiple times, handle that.
	 */
	c = 0;
	for (p = ecom->val; c < ecom->size; p += ecom->unit_size, c++) {
		if (p[0] == type && p[1] == subtype)
			found++;
	}
	/* If no matching ext community exists, return. */
	if (found == 0)
		return false;

	/* Handle the case where everything needs to be stripped. */
	if (found == ecom->size) {
		XFREE(MTYPE_ECOMMUNITY_VAL, ecom->val);
		ecom->size = 0;
		return true;
	}

	/* Strip matching ext community(ies). */
	new = XMALLOC(MTYPE_ECOMMUNITY_VAL,
		      (ecom->size - found) * ecom->unit_size);
	q = new;
	for (c = 0, p = ecom->val; c < ecom->size; c++, p += ecom->unit_size) {
		if (!(p[0] == type && p[1] == subtype)) {
			memcpy(q, p, ecom->unit_size);
			q += ecom->unit_size;
		}
	}
	XFREE(MTYPE_ECOMMUNITY_VAL, ecom->val);
	ecom->val = new;
	ecom->size -= found;
	return true;
}

/* 【函数】ecommunity_non_transitive：判断某 type 字节是否表示非传递社区。 */
static bool ecommunity_non_transitive(uint8_t type)
{
	return (CHECK_FLAG(type, ECOMMUNITY_FLAG_NON_TRANSITIVE) ||
		type == ECOMMUNITY_ENCODE_IP_NON_TRANS ||
		type == ECOMMUNITY_ENCODE_OPAQUE_NON_TRANS);
}

/* Delete all non-transitive extended communities */
/* 【函数】ecommunity_strip_non_transitive：删除所有非传递扩展社区。 */
bool ecommunity_strip_non_transitive(struct ecommunity *ecom)
{
	uint8_t *p, *q, *new;
	uint32_t c, found = 0;

	if (!ecom || !ecom->val)
		return false;

	/* Certain extended communities like the Route Target can be present
	 * multiple times, handle that.
	 */
	c = 0;
	for (p = ecom->val; c < ecom->size; p += ecom->unit_size, c++)
		if (ecommunity_non_transitive(*p))
			found++;

	if (!found)
		return false;

	/* Handle the case where everything needs to be stripped. */
	if (found == ecom->size) {
		XFREE(MTYPE_ECOMMUNITY_VAL, ecom->val);
		ecom->size = 0;
		return true;
	}

	/* Strip extended communities with non-transitive flag set */
	new = XMALLOC(MTYPE_ECOMMUNITY_VAL, (ecom->size - found) * ecom->unit_size);
	q = new;
	for (c = 0, p = ecom->val; c < ecom->size; c++, p += ecom->unit_size) {
		if (!ecommunity_non_transitive(*p)) {
			memcpy(q, p, ecom->unit_size);
			q += ecom->unit_size;
		}
	}

	XFREE(MTYPE_ECOMMUNITY_VAL, ecom->val);
	ecom->val = new;
	ecom->size -= found;

	return true;
}

/*
 * Filter source Extended Communities Attribute. May return NULL if the result
 * is empty, or the source ecommunity, if not modified.
 */
/* 【函数】ecommunity_filter：用回调 filter 过滤单元，保留回调为真者。
 * 结果为空返回 NULL；未发生改动（全部保留）时返回原对象 ecom；否则返回新对象。
 */
struct ecommunity *ecommunity_filter(struct ecommunity *ecom,
				     bool (*filter)(uint8_t *, uint8_t, void *), void *arg)
{
	struct ecommunity *new;
	uint8_t *new_val, *p, *q;
	uint32_t c, new_size;

	if (!ecom || !ecom->size)
		return NULL;

	new_val = XMALLOC(MTYPE_ECOMMUNITY_VAL, ecom->size * ecom->unit_size);
	new_size = 0;
	for (c = 0, p = ecom->val, q = new_val; c < ecom->size; c++, p += ecom->unit_size) {
		if (filter(p, ecom->unit_size, arg)) {
			memcpy(q, p, ecom->unit_size);
			q += ecom->unit_size;
			new_size++;
		}
	}
	if (!new_size) {
		XFREE(MTYPE_ECOMMUNITY_VAL, new_val);
		return NULL;
	}
	if (new_size == ecom->size) {
		XFREE(MTYPE_ECOMMUNITY_VAL, new_val);
		return ecom;
	}

	new = XCALLOC(MTYPE_ECOMMUNITY, sizeof(struct ecommunity));
	new->size = new_size;
	new->unit_size = ecom->unit_size;
	new->val = new_val;

	return new;
}

/*
 * Remove specified extended community value from extended community.
 * Returns 1 if value was present (and hence, removed), 0 otherwise.
 */
/* 【函数】ecommunity_del_val：删除与 eval 相同的一个单元。返回是否删除。 */
bool ecommunity_del_val(struct ecommunity *ecom, struct ecommunity_val *eval)
{
	uint8_t *p;
	uint32_t c, found = 0;

	/* Make sure specified value exists. */
	if (ecom == NULL || ecom->val == NULL)
		return false;
	c = 0;
	for (p = ecom->val; c < ecom->size; p += ecom->unit_size, c++) {
		if (!memcmp(p, eval->val, ecom->unit_size)) {
			found = 1;
			break;
		}
	}
	if (found == 0)
		return false;

	/* Delete the selected value */
	ecom->size--;
	if (ecom->size) {
		p = XMALLOC(MTYPE_ECOMMUNITY_VAL, ecom->size * ecom->unit_size);
		if (c != 0)
			memcpy(p, ecom->val, c * ecom->unit_size);
		if ((ecom->size - c) != 0)
			memcpy(p + (c)*ecom->unit_size,
			       ecom->val + (c + 1) * ecom->unit_size,
			       (ecom->size - c) * ecom->unit_size);
		XFREE(MTYPE_ECOMMUNITY_VAL, ecom->val);
		ecom->val = p;
	} else
		XFREE(MTYPE_ECOMMUNITY_VAL, ecom->val);

	return true;
}

/* 【函数】ecommunity_fill_pbr_action：把 FlowSpec 扩展社区翻译为 PBR 动作
 * （traffic-rate / action / marking / redirect VRF 或 IP）。redirect VRF 需外部
 * 函数处理，这里返回 0 表示“交由上层”；无法识别返回 -1。
 */
int ecommunity_fill_pbr_action(struct ecommunity_val *ecom_eval,
			       struct bgp_pbr_entry_action *api,
			       afi_t afi)
{
	if (ecom_eval->val[1] == ECOMMUNITY_TRAFFIC_RATE) {
		api->action = ACTION_TRAFFICRATE;
		api->u.r.rate_info[3] = ecom_eval->val[4];
		api->u.r.rate_info[2] = ecom_eval->val[5];
		api->u.r.rate_info[1] = ecom_eval->val[6];
		api->u.r.rate_info[0] = ecom_eval->val[7];
	} else if (ecom_eval->val[1] == ECOMMUNITY_TRAFFIC_ACTION) {
		api->action = ACTION_TRAFFIC_ACTION;
		/* else distribute code is set by default */
		if (CHECK_FLAG(ecom_eval->val[5],
			       (1 << FLOWSPEC_TRAFFIC_ACTION_TERMINAL)))
			SET_FLAG(api->u.za.filter, TRAFFIC_ACTION_TERMINATE);
		else
			SET_FLAG(api->u.za.filter, TRAFFIC_ACTION_DISTRIBUTE);
		if (ecom_eval->val[5] == 1 << FLOWSPEC_TRAFFIC_ACTION_SAMPLE)
			SET_FLAG(api->u.za.filter, TRAFFIC_ACTION_SAMPLE);

	} else if (ecom_eval->val[1] == ECOMMUNITY_TRAFFIC_MARKING) {
		api->action = ACTION_MARKING;
		api->u.marking_dscp = ecom_eval->val[7];
	} else if (ecom_eval->val[1] == ECOMMUNITY_REDIRECT_VRF) {
		/* must use external function */
		return 0;
	} else if (ecom_eval->val[1] == ECOMMUNITY_REDIRECT_IP_NH &&
		   afi == AFI_IP) {
		/* see draft-ietf-idr-flowspec-redirect-ip-02
		 * Q1: how come a ext. community can host ipv6 address
		 * Q2 : from cisco documentation:
		 * Announces the reachability of one or more flowspec NLRI.
		 * When a BGP speaker receives an UPDATE message with the
		 * redirect-to-IP extended community, it is expected to
		 * create a traffic filtering rule for every flow-spec
		 * NLRI in the message that has this path as its best
		 * path. The filter entry matches the IP packets
		 * described in the NLRI field and redirects them or
		 * copies them towards the IPv4 or IPv6 address specified
		 * in the 'Network Address of Next- Hop'
		 * field of the associated MP_REACH_NLRI.
		 */
		struct ecommunity_ip *ip_ecom =
			(struct ecommunity_ip *)&ecom_eval->val[2];

		api->u.zr.redirect_ip_v4 = ip_ecom->ip;
	} else
		return -1;
	return 0;
}

/* 【函数】bgp_aggr_ecommunity_lookup：在聚合器的 ecommunity 哈希中查找。 */
static struct ecommunity *bgp_aggr_ecommunity_lookup(
						struct bgp_aggregate *aggregate,
						struct ecommunity *ecommunity)
{
	return hash_lookup(aggregate->ecommunity_hash, ecommunity);
}

/* 【函数】bgp_aggr_ecommunty_hash_alloc：哈希插入分配回调，返回深拷贝。
 * 注：函数名 ecommunty 少一个 i，属原始拼写笔误，未改动。
 */
static void *bgp_aggr_ecommunty_hash_alloc(void *p)
{
	struct ecommunity *ref = (struct ecommunity *)p;
	struct ecommunity *ecommunity = NULL;

	ecommunity = ecommunity_dup(ref);
	return ecommunity;
}

/* 【函数】bgp_aggr_ecommunity_prepare：哈希遍历回调，合并进聚合器。 */
static void bgp_aggr_ecommunity_prepare(struct hash_bucket *hb, void *arg)
{
	struct ecommunity *hb_ecommunity = hb->data;
	struct ecommunity **aggr_ecommunity = arg;

	if (*aggr_ecommunity)
		*aggr_ecommunity = ecommunity_merge(*aggr_ecommunity,
						    hb_ecommunity);
	else
		*aggr_ecommunity = ecommunity_dup(hb_ecommunity);
}

/* 【函数】bgp_aggr_ecommunity_remove：释放聚合器持有的 ecommunity。 */
void bgp_aggr_ecommunity_remove(void *arg)
{
	struct ecommunity *ecommunity = arg;

	ecommunity_free(&ecommunity);
}

/* 【函数】bgp_compute_aggregate_ecommunity：计入聚合器并重算汇总。 */
void bgp_compute_aggregate_ecommunity(struct bgp_aggregate *aggregate,
				      struct ecommunity *ecommunity)
{
	bgp_compute_aggregate_ecommunity_hash(aggregate, ecommunity);
	bgp_compute_aggregate_ecommunity_val(aggregate);
}


/* 【函数】bgp_compute_aggregate_ecommunity_hash：按需建哈希；已在则 refcnt 加一，
 * 否则插入深拷贝并加一。
 */
void bgp_compute_aggregate_ecommunity_hash(struct bgp_aggregate *aggregate,
					   struct ecommunity *ecommunity)
{
	struct ecommunity *aggr_ecommunity = NULL;

	if ((aggregate == NULL) || (ecommunity == NULL))
		return;

	/* Create hash if not already created.
	 */
	if (aggregate->ecommunity_hash == NULL)
		aggregate->ecommunity_hash = hash_create(
					ecommunity_hash_make, ecommunity_cmp,
					"BGP Aggregator ecommunity hash");

	aggr_ecommunity = bgp_aggr_ecommunity_lookup(aggregate, ecommunity);
	if (aggr_ecommunity == NULL) {
		/* Insert ecommunity into hash.
		 */
		aggr_ecommunity = hash_get(aggregate->ecommunity_hash,
					   ecommunity,
					   bgp_aggr_ecommunty_hash_alloc);
	}

	/* Increment reference counter.
	 */
	aggr_ecommunity->refcnt++;
}

/* 【函数】bgp_compute_aggregate_ecommunity_val：合并哈希中全部集合，去重排序后
 * 存入 aggregate->ecommunity。
 */
void bgp_compute_aggregate_ecommunity_val(struct bgp_aggregate *aggregate)
{
	struct ecommunity *ecommerge = NULL;

	if (aggregate == NULL)
		return;

	/* Re-compute aggregate's ecommunity.
	 */
	if (aggregate->ecommunity)
		ecommunity_free(&aggregate->ecommunity);
	if (aggregate->ecommunity_hash
	    && aggregate->ecommunity_hash->count) {
		hash_iterate(aggregate->ecommunity_hash,
			     bgp_aggr_ecommunity_prepare,
			     &aggregate->ecommunity);
		ecommerge = aggregate->ecommunity;
		aggregate->ecommunity = ecommunity_uniq_sort(ecommerge);
		if (ecommerge)
			ecommunity_free(&ecommerge);
	}
}

/* 【函数】bgp_remove_ecommunity_from_aggregate：减记引用；归零则摘除释放并重算汇总。 */
void bgp_remove_ecommunity_from_aggregate(struct bgp_aggregate *aggregate,
					  struct ecommunity *ecommunity)
{
	struct ecommunity *aggr_ecommunity = NULL;
	struct ecommunity *ret_ecomm = NULL;

	if ((!aggregate)
	    || (!aggregate->ecommunity_hash)
	    || (!ecommunity))
		return;

	/* Look-up the ecommunity in the hash.
	 */
	aggr_ecommunity = bgp_aggr_ecommunity_lookup(aggregate, ecommunity);
	if (aggr_ecommunity) {
		aggr_ecommunity->refcnt--;

		if (aggr_ecommunity->refcnt == 0) {
			ret_ecomm = hash_release(aggregate->ecommunity_hash,
						 aggr_ecommunity);
			ecommunity_free(&ret_ecomm);
			bgp_compute_aggregate_ecommunity_val(aggregate);
		}
	}
}

/* 【函数】bgp_remove_ecomm_from_aggregate_hash：仅减记引用（归零则释放），不重算汇总。 */
void bgp_remove_ecomm_from_aggregate_hash(struct bgp_aggregate *aggregate,
					  struct ecommunity *ecommunity)
{

	struct ecommunity *aggr_ecommunity = NULL;
	struct ecommunity *ret_ecomm = NULL;

	if ((!aggregate)
	    || (!aggregate->ecommunity_hash)
	    || (!ecommunity))
		return;

	/* Look-up the ecommunity in the hash.
	 */
	aggr_ecommunity = bgp_aggr_ecommunity_lookup(aggregate, ecommunity);
	if (aggr_ecommunity) {
		aggr_ecommunity->refcnt--;

		if (aggr_ecommunity->refcnt == 0) {
			ret_ecomm = hash_release(aggregate->ecommunity_hash,
						 aggr_ecommunity);
			ecommunity_free(&ret_ecomm);
		}
	}
}

/* 【函数】ecommunity_add_origin_validation_state：为集合设置或更新 OVS 扩展社区。
 * 有旧集合时复制后覆盖（unique 与 overwrite 均为真）；无旧集合时以单个 OVS
 * 单元构造新集合。
 */
struct ecommunity *
ecommunity_add_origin_validation_state(enum rpki_states rpki_state,
				       struct ecommunity *old)
{
	struct ecommunity *new = NULL;
	struct ecommunity ovs_ecomm = {0};
	struct ecommunity_val ovs_eval;

	encode_origin_validation_state(rpki_state, &ovs_eval);

	if (old) {
		new = ecommunity_dup(old);
		ecommunity_add_val(new, &ovs_eval, true, true);
		if (!old->refcnt)
			ecommunity_free(&old);
	} else {
		ovs_ecomm.size = 1;
		ovs_ecomm.unit_size = ECOMMUNITY_SIZE;
		ovs_ecomm.val = (uint8_t *)&ovs_eval.val;
		new = ecommunity_dup(&ovs_ecomm);
	}

	return new;
}

/*
 * return the BGP link bandwidth extended community, if present;
 * the actual bandwidth is returned via param
 */
/* 【函数】ecommunity_linkbw_present：查找集合中的 link-bandwidth 单元，把带宽写入
 * bw 并返回该单元的指针（找不到返回 NULL）。支持 8 字节与 20 字节两种编码。
 */
const uint8_t *ecommunity_linkbw_present(struct ecommunity *ecom, uint64_t *bw)
{
	const uint8_t *data;
	uint32_t i;

	if (bw)
		*bw = 0;

	if (!ecom || !ecom->size)
		return NULL;

	for (i = 0; i < ecom->size; i++) {
		const uint8_t *pnt;
		uint8_t type, sub_type;

		data = pnt = (ecom->val + (i * ecom->unit_size));
		type = *pnt++;
		sub_type = *pnt++;

		const uint8_t *end = data + ecom->unit_size;
		size_t len = end - data;

		/* Sanity check for extended communities length, to avoid
		 * overrun when dealing with bits, e.g. ptr_get_be64().
		 */
		if (len < ecom->unit_size)
			return NULL;

		if (CHECK_FLAG(type, ~ECOMMUNITY_FLAG_NON_TRANSITIVE) == ECOMMUNITY_ENCODE_AS &&
		    sub_type == ECOMMUNITY_LINK_BANDWIDTH) {
			uint32_t bwval;

			pnt += 2; /* bandwidth is encoded as AS:val */
			pnt = ptr_get_be32(pnt, &bwval);
			(void)pnt; /* consume value */
			if (bw)
				*bw = (uint64_t)(ecom->disable_ieee_floating
							 ? bwval
							 : ieee_float_uint32_to_uint32(
								   bwval));
			return data;
		} else if (CHECK_FLAG(type, ~ECOMMUNITY_FLAG_NON_TRANSITIVE) ==
				   ECOMMUNITY_ENCODE_AS4 &&
			   sub_type == ECOMMUNITY_EXTENDED_LINK_BANDWIDTH) {
			uint64_t bwval;

			if (len < IPV6_ECOMMUNITY_SIZE)
				return NULL;

			pnt += 2; /* Reserved */
			pnt = ptr_get_be64(pnt, &bwval);
			(void)pnt;

			if (bw)
				*bw = bwval;

			return data;
		}
	}

	return NULL;
}


/* 【函数】ecommunity_replace_linkbw：把集合中传递性的 link-bandwidth 替换为新的
 * 累计带宽 cum_bw（非传递的、或本来没有 link-bandwidth 的保持不变）。
 * extended 为真时用 20 字节编码；AS 超过 2 字节时用 BGP_AS_TRANS 占位。
 */
struct ecommunity *ecommunity_replace_linkbw(as_t as, struct ecommunity *ecom,
					     uint64_t cum_bw,
					     bool disable_ieee_floating,
					     bool extended)
{
	struct ecommunity *new;
	const uint8_t *eval;
	uint8_t type;
	uint64_t cur_bw;

	/* Nothing to replace if link-bandwidth doesn't exist or
	 * is non-transitive - just return existing extcommunity.
	 */
	new = ecom;
	if (!ecom || !ecom->size)
		return new;

	eval = ecommunity_linkbw_present(ecom, &cur_bw);
	if (!eval)
		return new;

	type = *eval;
	if (CHECK_FLAG(type, ECOMMUNITY_FLAG_NON_TRANSITIVE))
		return new;

	/* Transitive link-bandwidth exists, replace with the passed
	 * (cumulative) bandwidth value. We need to create a new
	 * extcommunity for this - refer to AS-Path replace function
	 * for reference.
	 */
	if (cum_bw > 0xFFFFFFFF)
		cum_bw = 0xFFFFFFFF;

	if (extended) {
		struct ecommunity_val_ipv6 lb_eval;

		encode_lb_extended_extcomm(as, cum_bw, false, &lb_eval);
		new = ecommunity_dup(ecom);
		ecommunity_add_val_ipv6(new, &lb_eval, true, true);
	} else {
		struct ecommunity_val lb_eval;

		encode_lb_extcomm(as > BGP_AS_MAX ? BGP_AS_TRANS : as, cum_bw,
				  false, &lb_eval, disable_ieee_floating);
		new = ecommunity_dup(ecom);
		ecommunity_add_val(new, &lb_eval, true, true);
	}

	return new;
}

/* 【函数】soo_in_ecom：判断 ecom 是否含 SoO 且与 soo 匹配（要求存在 SoO，
 * 且 ecom 包含 soo 的单元）。
 */
bool soo_in_ecom(struct ecommunity *ecom, struct ecommunity *soo)
{
	if (ecom && soo) {
		if ((ecommunity_lookup(ecom, ECOMMUNITY_ENCODE_AS,
				       ECOMMUNITY_SITE_ORIGIN) ||
		     ecommunity_lookup(ecom, ECOMMUNITY_ENCODE_AS4,
				       ECOMMUNITY_SITE_ORIGIN) ||
		     ecommunity_lookup(ecom, ECOMMUNITY_ENCODE_IP,
				       ECOMMUNITY_SITE_ORIGIN)) &&
		    ecommunity_include(ecom, soo))
			return true;
	}
	return false;
}
