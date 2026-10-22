// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * FRR filter northbound implementation.
 *
 * Copyright (C) 2019 Network Device Education Foundation, Inc. ("NetDEF")
 *                    Rafael Zalamena
 */

/*
 * 文件导读 filter_nb.c —— 访问列表 / 前缀列表的北向接口（YANG）实现
 *
 * 定位：本文件是「配置数据树 -> 内存结构」的落地层。filter_cli.c 负责把命令
 *   写进候选配置，本文件的回调再把候选配置 apply 到运行配置，并同步更新
 *   filter.c / plist.c 里的 C 数据结构；它不解析命令行，也不做匹配运算。
 *
 * 三方分工回顾：
 *   filter.h      ：数据结构与原型；
 *   filter.c      ：访问列表核心（存 / 查 / 匹配）；
 *   filter_nb.c   ：YANG 回调（validate / create / destroy / modify / cli_show），
 *                   把配置树落到 filter.c 的 access_list 及 plist.c 的 prefix_list；
 *   filter_cli.c  ：命令行（DEFPY_YANG）与 show / 比较回调。
 *
 * 本文件主要内容：
 *   - prefix_list_length_validate            ：校验 ge / le 长度约束；
 *   - acl_is_dup / acl_cisco_is_dup / acl_zebra_is_dup ：查重逻辑；
 *   - *_create / *_destroy / *_modify        ：各类节点的增删改回调；
 *   - frr_filter_info                        ：YANG 模块回调表（见文件末尾）。
 *
 * 关键约定：
 *   - 多数回调在 args->event != NB_EV_APPLY 时直接返回 NB_OK，即只在 apply
 *     阶段真正改动内存结构；校验只在 validate 阶段做。
 *   - nb_running_get_entry 用于从数据节点找回对应的 C 结构。
 *   - cli_show 回调负责把运行配置反向打印成命令行文本。
 */

#include "zebra.h"

#include "lib/northbound.h"
#include "lib/prefix.h"
#include "lib/printfrr.h"

#include "lib/filter.h"
#include "lib/plist.h"
#include "lib/plist_int.h"
#include "lib/routemap.h"

/*
 * prefix_list_length_validate —— 校验前缀列表条目的 ge / le 长度约束。
 * 参数：args 含待校验节点及错误信息缓冲区。
 * 返回：NB_OK；不满足则写 args->errmsg 并返回 NB_ERR_VALIDATION。
 * 三条规则都需满足：前缀长度 <= le、前缀长度 <= ge、ge <= le。
 * 坑：le / ge 可能不存在，取值前必须先用 yang_dnode_exists 判断。
 */
static enum nb_error prefix_list_length_validate(struct nb_cb_modify_args *args)
{
	int type = yang_dnode_get_enum(args->dnode, "../../type");
	const char *xpath_le = NULL, *xpath_ge = NULL;
	struct prefix p;
	uint8_t le, ge;

	if (type == YPLT_IPV4) {
		yang_dnode_get_prefix(&p, args->dnode, "../ipv4-prefix");
		xpath_le = "../ipv4-prefix-length-lesser-or-equal";
		xpath_ge = "../ipv4-prefix-length-greater-or-equal";
	} else {
		yang_dnode_get_prefix(&p, args->dnode, "../ipv6-prefix");
		xpath_le = "../ipv6-prefix-length-lesser-or-equal";
		xpath_ge = "../ipv6-prefix-length-greater-or-equal";
	}

	/*
	 * Check rule:
	 * prefix length <= le.
	 */
	if (yang_dnode_exists(args->dnode, xpath_le)) {
		le = yang_dnode_get_uint8(args->dnode, "%s", xpath_le);
		if (p.prefixlen > le)
			goto log_and_fail;
	}

	/*
	 * Check rule:
	 * prefix length <= ge.
	 */
	if (yang_dnode_exists(args->dnode, xpath_ge)) {
		ge = yang_dnode_get_uint8(args->dnode, "%s", xpath_ge);
		if (p.prefixlen > ge)
			goto log_and_fail;
	}

	/*
	 * Check rule:
	 * ge <= le.
	 */
	if (yang_dnode_exists(args->dnode, xpath_le)
	    && yang_dnode_exists(args->dnode, xpath_ge)) {
		le = yang_dnode_get_uint8(args->dnode, "%s", xpath_le);
		ge = yang_dnode_get_uint8(args->dnode, "%s", xpath_ge);
		if (ge > le)
			goto log_and_fail;
	}

	return NB_OK;

log_and_fail:
	snprintfrr(
		args->errmsg, args->errmsg_len,
		"Invalid prefix range for %pFX: Make sure that mask length <= ge <= le",
		&p);
	return NB_ERR_VALIDATION;
}

/*
 * prefix_list_entry_set_empty —— 把一个前缀列表条目重置为“空值”：
 *   any=false、prefix 清零、ge=le=0。
 * 参数：ple 待重置的条目；无返回值。
 */
/**
 * Sets prefix list entry to blank value.
 *
 * \param[out] ple prefix list entry to modify.
 */
static void prefix_list_entry_set_empty(struct prefix_list_entry *ple)
{
	ple->any = false;
	memset(&ple->prefix, 0, sizeof(ple->prefix));
	ple->ge = 0;
	ple->le = 0;
}

/*
 * prefix_list_nb_validate_v4_af_type —— 校验给定列表节点的 type 是否为 IPv4。
 * 返回：NB_OK；若不是 YPLT_IPV4，写入错误信息并返回 NB_ERR_VALIDATION。
 * 用途：供 YANG must 表达式引用，防止 v4 / v6 前缀混用。
 */
static int
prefix_list_nb_validate_v4_af_type(const struct lyd_node *plist_dnode,
				   char *errmsg, size_t errmsg_len)
{
	int af_type;

	af_type = yang_dnode_get_enum(plist_dnode, "type");
	if (af_type != YPLT_IPV4) {
		snprintf(errmsg, errmsg_len,
			 "prefix-list type %u is mismatched.", af_type);
		return NB_ERR_VALIDATION;
	}

	return NB_OK;
}

/*
 * prefix_list_nb_validate_v6_af_type —— 校验给定列表节点的 type 是否为 IPv6。
 * 返回：NB_OK；否则写入错误信息并返回 NB_ERR_VALIDATION。
 */
static int
prefix_list_nb_validate_v6_af_type(const struct lyd_node *plist_dnode,
				   char *errmsg, size_t errmsg_len)
{
	int af_type;

	af_type = yang_dnode_get_enum(plist_dnode, "type");
	if (af_type != YPLT_IPV6) {
		snprintf(errmsg, errmsg_len,
			 "prefix-list type %u is mismatched.", af_type);
		return NB_ERR_VALIDATION;
	}

	return NB_OK;
}

/*
 * lib_prefix_list_entry_prefix_length_greater_or_equal_modify
 *   —— ge（最小长度）叶子的 modify 回调。
 * 逻辑：仅 NB_EV_APPLY 阶段生效；取回对应 prefix_list_entry，调用
 *   prefix_list_entry_update_start 启动更新，然后写入 ple->ge。
 * 返回：NB_OK。
 */
static int lib_prefix_list_entry_prefix_length_greater_or_equal_modify(
	struct nb_cb_modify_args *args)
{
	struct prefix_list_entry *ple;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	ple = nb_running_get_entry(args->dnode, NULL, true);

	/* Start prefix entry update procedure. */
	prefix_list_entry_update_start(ple);

	ple->ge = yang_dnode_get_uint8(args->dnode, NULL);

	return NB_OK;
}

/*
 * lib_prefix_list_entry_prefix_length_lesser_or_equal_modify
 *   —— le（最大长度）叶子的 modify 回调：启动更新后写入 ple->le（流程同 ge）。
 * 返回：NB_OK。
 */
static int lib_prefix_list_entry_prefix_length_lesser_or_equal_modify(
	struct nb_cb_modify_args *args)
{
	struct prefix_list_entry *ple;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	ple = nb_running_get_entry(args->dnode, NULL, true);

	/* Start prefix entry update procedure. */
	prefix_list_entry_update_start(ple);

	ple->le = yang_dnode_get_uint8(args->dnode, NULL);

	return NB_OK;
}

/*
 * lib_prefix_list_entry_prefix_length_greater_or_equal_destroy
 *   —— ge 叶子的 destroy 回调：把 ple->ge 复位为 0，并启动条目更新。
 * 返回：NB_OK。
 */
static int lib_prefix_list_entry_prefix_length_greater_or_equal_destroy(
	struct nb_cb_destroy_args *args)
{
	struct prefix_list_entry *ple;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	ple = nb_running_get_entry(args->dnode, NULL, true);

	/* Start prefix entry update procedure. */
	prefix_list_entry_update_start(ple);

	ple->ge = 0;

	return NB_OK;
}

/*
 * lib_prefix_list_entry_prefix_length_lesser_or_equal_destroy
 *   —— le 叶子的 destroy 回调：把 ple->le 复位为 0，并启动条目更新。
 * 返回：NB_OK。
 */
static int lib_prefix_list_entry_prefix_length_lesser_or_equal_destroy(
	struct nb_cb_destroy_args *args)
{
	struct prefix_list_entry *ple;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	ple = nb_running_get_entry(args->dnode, NULL, true);

	/* Start prefix entry update procedure. */
	prefix_list_entry_update_start(ple);

	ple->le = 0;

	return NB_OK;
}

/*
 * cisco_unset_addr_mask —— 把一条 Cisco 规则的地址 / 掩码复位为“未设置”，
 *   等价于 0.0.0.0/32（地址全 0，通配符全 0 即 host 全 0）。
 * 参数：addr / mask 分别为地址与通配符掩码，就地修改；无返回值。
 */
/**
 * Unsets the cisco style rule for addresses so it becomes disabled (the
 * equivalent of setting: `0.0.0.0/32`).
 *
 * \param addr address part.
 * \param mask mask part.
 */
static void cisco_unset_addr_mask(struct in_addr *addr, struct in_addr *mask)
{
	addr->s_addr = INADDR_ANY;
	mask->s_addr = CISCO_BIN_HOST_WILDCARD_MASK;
}

/*
 * _acl_is_dup —— yang_dnode_iterate 的回调：判断某条目是否与查询参数重复。
 * 参数：dnode 当前条目；arg 为 struct acl_dup_args。
 * 返回：命中则填 ada_found / ada_seq 并返回 YANG_ITER_STOP；
 *   否则 YANG_ITER_CONTINUE 继续遍历。
 * 逻辑：先跳过调用方自身；再比 action；最后逐项比对 ada_xpath / ada_value，
 *   任一类型不符或取值不同即视为不重复。
 * 坑：ada_xpath 中出现 NULL 表示没有更多待比值，比较循环随之结束。
 */
static int _acl_is_dup(const struct lyd_node *dnode, void *arg)
{
	struct acl_dup_args *ada = arg;
	int idx;

	/* This entry is the caller, so skip it. */
	if (ada->ada_entry_dnode
	    && ada->ada_entry_dnode == dnode)
		return YANG_ITER_CONTINUE;

	if (strcmp(yang_dnode_get_string(dnode, "action"), ada->ada_action))
		return YANG_ITER_CONTINUE;

	/* Check if all values match. */
	for (idx = 0; idx < ADA_MAX_VALUES; idx++) {
		/* No more values. */
		if (ada->ada_xpath[idx] == NULL)
			break;

		/* Not same type, just skip it. */
		if (!yang_dnode_exists(dnode, ada->ada_xpath[idx]))
			return YANG_ITER_CONTINUE;

		/* Check if different value. */
		if (strcmp(yang_dnode_get_string(dnode, "%s",
						 ada->ada_xpath[idx]),
			   ada->ada_value[idx]))
			return YANG_ITER_CONTINUE;
	}

	ada->ada_found = true;
	ada->ada_seq = yang_dnode_get_uint32(dnode, "sequence");

	return YANG_ITER_STOP;
}

/*
 * acl_is_dup —— 对外查重入口：在指定列表的所有 entry 中查找与 ada 描述
 *   完全相同的条目。
 * 参数：dnode 用于定位候选配置；ada 查重参数（类型 / 名字 / 动作 / 取值）。
 * 返回：是否找到重复项（并回填 ada->ada_seq）。
 * 逻辑：先复位 ada_found，再用 _acl_is_dup 遍历 <type>/<name> 下的 entry。
 */
bool acl_is_dup(const struct lyd_node *dnode, struct acl_dup_args *ada)
{
	ada->ada_found = false;

	yang_dnode_iterate(
		_acl_is_dup, ada, dnode,
		"/frr-filter:lib/access-list[type='%s'][name='%s']/entry",
		ada->ada_type, ada->ada_name);

	return ada->ada_found;
}

/*
 * acl_cisco_is_dup —— 判断一条 Cisco 风格规则是否与列表中已有条目重复。
 * 参数：dnode 为触发点（其父 entry 才是要检查的条目）。
 * 返回：是否重复。
 * 逻辑：把 entry 下所有“已存在”的 cisco 相关字段（host / network / any /
 *   destination-* 等）收集成 xpath + value 列表，填入 acl_dup_args，
 *   再调用 acl_is_dup 比对。
 * 坑：ada_entry_dnode 设为自身 entry，供 _acl_is_dup 跳过自己。
 */
static bool acl_cisco_is_dup(const struct lyd_node *dnode)
{
	const struct lyd_node *entry_dnode =
		yang_dnode_get_parent(dnode, "entry");
	struct acl_dup_args ada = {};
	int idx = 0, arg_idx = 0;
	static const char *cisco_entries[] = {
		"./host",
		"./network/address",
		"./network/mask",
		"./source-any",
		"./destination-host",
		"./destination-network/address",
		"./destination-network/mask",
		"./destination-any",
		NULL
	};

	/* Initialize. */
	ada.ada_type = "ipv4";
	ada.ada_name = yang_dnode_get_string(entry_dnode, "../name");
	ada.ada_action = yang_dnode_get_string(entry_dnode, "action");
	ada.ada_entry_dnode = entry_dnode;

	/* Load all values/XPaths. */
	while (cisco_entries[idx] != NULL) {
		if (!yang_dnode_exists(entry_dnode, cisco_entries[idx])) {
			idx++;
			continue;
		}

		ada.ada_xpath[arg_idx] = cisco_entries[idx];
		ada.ada_value[arg_idx] = yang_dnode_get_string(
			entry_dnode, "%s", cisco_entries[idx]);
		arg_idx++;
		idx++;
	}

	return acl_is_dup(entry_dnode, &ada);
}

/*
 * acl_zebra_is_dup —— 判断一条 zebra 风格规则是否重复。
 * 参数：dnode 触发点；type 列表类型（IPv4 / IPv6 / MAC）。
 * 返回：是否重复。
 * 逻辑：按 type 选定 ada_type 字符串，再把存在的 zebra 字段
 *   （ipv4-prefix / ipv6-prefix / mac / any 等）收集进 acl_dup_args，
 *   最后调用 acl_is_dup 比对。
 */
static bool acl_zebra_is_dup(const struct lyd_node *dnode,
			     enum yang_access_list_type type)
{
	const struct lyd_node *entry_dnode =
		yang_dnode_get_parent(dnode, "entry");
	struct acl_dup_args ada = {};
	int idx = 0, arg_idx = 0;
	static const char *zebra_entries[] = {
		"./ipv4-prefix",
		"./ipv4-exact-match",
		"./ipv6-prefix",
		"./ipv6-exact-match",
		"./mac",
		"./any",
		NULL
	};

	/* Initialize. */
	switch (type) {
	case YALT_IPV4:
		ada.ada_type = "ipv4";
		break;
	case YALT_IPV6:
		ada.ada_type = "ipv6";
		break;
	case YALT_MAC:
		ada.ada_type = "mac";
		break;
	}
	ada.ada_name = yang_dnode_get_string(entry_dnode, "../name");
	ada.ada_action = yang_dnode_get_string(entry_dnode, "action");
	ada.ada_entry_dnode = entry_dnode;

	/* Load all values/XPaths. */
	while (zebra_entries[idx] != NULL) {
		if (!yang_dnode_exists(entry_dnode, zebra_entries[idx])) {
			idx++;
			continue;
		}

		ada.ada_xpath[arg_idx] = zebra_entries[idx];
		ada.ada_value[arg_idx] = yang_dnode_get_string(
			entry_dnode, "%s", zebra_entries[idx]);
		arg_idx++;
		idx++;
	}

	return acl_is_dup(entry_dnode, &ada);
}

/*
 * plist_dnode_to_prefix —— 从一段前缀列表 entry 节点提取匹配要素。
 * 参数（出参）：any 是否 any；p 前缀；ge / le 长度约束。
 * 逻辑：先默认 any=false、ge=le=0；若存在 any 叶子则置 any=true 直接返回；
 *   否则按 type（IPv4 / IPv6）读对应前缀与 ge / le 字段。
 */
static void plist_dnode_to_prefix(const struct lyd_node *dnode, bool *any,
				  struct prefix *p, int *ge, int *le)
{
	*any = false;
	*ge = 0;
	*le = 0;

	if (yang_dnode_exists(dnode, "any")) {
		*any = true;
		return;
	}

	switch (yang_dnode_get_enum(dnode, "../type")) {
	case YPLT_IPV4:
		yang_dnode_get_prefix(p, dnode, "ipv4-prefix");
		if (yang_dnode_exists(dnode,
				      "./ipv4-prefix-length-greater-or-equal"))
			*ge = yang_dnode_get_uint8(
				dnode, "./ipv4-prefix-length-greater-or-equal");
		if (yang_dnode_exists(dnode,
				      "./ipv4-prefix-length-lesser-or-equal"))
			*le = yang_dnode_get_uint8(
				dnode, "./ipv4-prefix-length-lesser-or-equal");
		break;
	case YPLT_IPV6:
		yang_dnode_get_prefix(p, dnode, "ipv6-prefix");
		if (yang_dnode_exists(dnode,
				      "./ipv6-prefix-length-greater-or-equal"))
			*ge = yang_dnode_get_uint8(
				dnode, "./ipv6-prefix-length-greater-or-equal");
		if (yang_dnode_exists(dnode,
				      "./ipv6-prefix-length-lesser-or-equal"))
			*le = yang_dnode_get_uint8(
				dnode, "./ipv6-prefix-length-lesser-or-equal");
		break;
	}
}

/*
 * _plist_is_dup —— yang_dnode_iterate 回调：判断某前缀列表条目是否与查询参数重复。
 * 参数：dnode 当前条目；arg 为 struct plist_dup_args。
 * 返回：命中则填 pda_found / pda_seq 并 YANG_ITER_STOP；否则 YANG_ITER_CONTINUE。
 * 逻辑：跳过调用方自身；比 action；用 plist_dnode_to_prefix 取出条目要素，
 *   any 条目要求对方也是 any，否则要求前缀相同且 ge / le 都相等。
 */
static int _plist_is_dup(const struct lyd_node *dnode, void *arg)
{
	struct plist_dup_args *pda = arg;
	struct prefix p = {};
	int ge, le;
	bool any;

	/* This entry is the caller, so skip it. */
	if (pda->pda_entry_dnode
	    && pda->pda_entry_dnode == dnode)
		return YANG_ITER_CONTINUE;

	if (strcmp(yang_dnode_get_string(dnode, "action"), pda->pda_action))
		return YANG_ITER_CONTINUE;

	plist_dnode_to_prefix(dnode, &any, &p, &ge, &le);

	if (pda->any) {
		if (!any)
			return YANG_ITER_CONTINUE;
	} else {
		if (!prefix_same(&pda->prefix, &p) || pda->ge != ge
		    || pda->le != le)
			return YANG_ITER_CONTINUE;
	}

	pda->pda_found = true;
	pda->pda_seq = yang_dnode_get_uint32(dnode, "sequence");

	return YANG_ITER_STOP;
}

/*
 * plist_is_dup —— 对外查重入口：在指定前缀列表中查找与 pda 描述相同的条目。
 * 参数：dnode 定位候选配置；pda 查重参数（类型 / 名字 / 动作 / 前缀 / ge / le）。
 * 返回：是否找到重复项（并回填 pda->pda_seq）。
 */
bool plist_is_dup(const struct lyd_node *dnode, struct plist_dup_args *pda)
{
	pda->pda_found = false;

	yang_dnode_iterate(
		_plist_is_dup, pda, dnode,
		"/frr-filter:lib/prefix-list[type='%s'][name='%s']/entry",
		pda->pda_type, pda->pda_name);

	return pda->pda_found;
}

/*
 * XPath: /frr-filter:lib/access-list
 */
/*
 * lib_access_list_create —— /frr-filter:lib/access-list 的 create 回调。
 * 逻辑：仅 NB_EV_APPLY 生效；按 type 选 AFI，用 access_list_get 取回或新建
 *   对应 access_list，并把它绑定到数据节点（nb_running_set_entry）。
 * 返回：NB_OK。
 * 注意：这里才真正在 filter.c 中创建内存里的访问列表。
 */
static int lib_access_list_create(struct nb_cb_create_args *args)
{
	struct access_list *acl = NULL;
	const char *acl_name;
	int type;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	type = yang_dnode_get_enum(args->dnode, "type");
	acl_name = yang_dnode_get_string(args->dnode, "name");

	switch (type) {
	case YALT_IPV4:
		acl = access_list_get(AFI_IP, acl_name);
		break;
	case YALT_IPV6:
		acl = access_list_get(AFI_IP6, acl_name);
		break;
	case YALT_MAC:
		acl = access_list_get(AFI_L2VPN, acl_name);
		break;
	}

	nb_running_set_entry(args->dnode, acl);

	return NB_OK;
}

/*
 * lib_access_list_destroy —— access-list 的 destroy 回调。
 * 逻辑：仅 NB_EV_APPLY 生效；从数据节点取回并解绑 access_list，
 *   调用 access_list_delete 彻底释放。
 * 返回：NB_OK。
 */
static int lib_access_list_destroy(struct nb_cb_destroy_args *args)
{
	struct access_list *acl;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	acl = nb_running_unset_entry(args->dnode);
	access_list_delete(acl);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/remark
 */
/*
 * lib_access_list_remark_modify —— access-list/remark 的 modify 回调。
 * 逻辑：仅 NB_EV_APPLY 生效；先释放旧 remark，再 XSTRDUP 新值存入 acl->remark。
 * 返回：NB_OK。
 * 注意：remark 用 MTYPE_TMP 分配，其生命周期须与 acl->remark 一致。
 */
static int lib_access_list_remark_modify(struct nb_cb_modify_args *args)
{
	struct access_list *acl;
	const char *remark;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	acl = nb_running_get_entry(args->dnode, NULL, true);
	if (acl->remark)
		XFREE(MTYPE_TMP, acl->remark);

	remark = yang_dnode_get_string(args->dnode, NULL);
	acl->remark = XSTRDUP(MTYPE_TMP, remark);

	return NB_OK;
}

/*
 * lib_access_list_remark_destroy —— access-list/remark 的 destroy 回调：
 *   释放 acl->remark（XFREE 宏会把指针一并置为 NULL）。
 * 返回：NB_OK。
 */
static int
lib_access_list_remark_destroy(struct nb_cb_destroy_args *args)
{
	struct access_list *acl;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	acl = nb_running_get_entry(args->dnode, NULL, true);
	if (acl->remark)
		XFREE(MTYPE_TMP, acl->remark);

	return NB_OK;
}


/*
 * XPath: /frr-filter:lib/access-list/entry
 */
/*
 * lib_access_list_entry_create —— access-list/entry 的 create 回调。
 * 逻辑：仅 NB_EV_APPLY 生效；filter_new 新建一条规则，设 seq、回指 acl，
 *   用 access_list_filter_add 挂入列表，并把规则绑定到该 entry 数据节点。
 * 返回：NB_OK。
 */
static int lib_access_list_entry_create(struct nb_cb_create_args *args)
{
	struct access_list *acl;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = filter_new();
	f->seq = yang_dnode_get_uint32(args->dnode, "sequence");

	acl = nb_running_get_entry(args->dnode, NULL, true);
	f->acl = acl;
	access_list_filter_add(acl, f);
	nb_running_set_entry(args->dnode, f);

	return NB_OK;
}

/*
 * lib_access_list_entry_destroy —— access-list/entry 的 destroy 回调。
 * 逻辑：取回并解绑规则 f，由 f->acl 找回列表，调用 access_list_filter_delete
 *   把规则摘除并释放。
 * 返回：NB_OK。
 */
static int lib_access_list_entry_destroy(struct nb_cb_destroy_args *args)
{
	struct access_list *acl;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_unset_entry(args->dnode);
	acl = f->acl;
	access_list_filter_delete(acl, f);

	return NB_OK;
}

/*
 * lib_access_list_entry_apply_finish —— entry 的 apply-finish 回调。
 * 逻辑：整条 entry 修改完成后调用 access_list_filter_update，触发钩子与
 *   route-map 依赖通知，令下游重新计算。
 * 用途：把“一次配置事务完成”的收尾动作与逐字段的内存更新解耦。
 */
static void
lib_access_list_entry_apply_finish(struct nb_cb_apply_finish_args *args)
{
	struct filter *f;

	f = nb_running_get_entry(args->dnode, NULL, true);
	access_list_filter_update(f->acl);
}

/*
 * XPath: /frr-filter:lib/access-list/entry/action
 */
/*
 * lib_access_list_entry_action_modify —— entry/action 的 modify 回调。
 * 逻辑：仅 NB_EV_APPLY 生效；读字符串，若为 "permit" 则 f->type = FILTER_PERMIT，
 *   否则 f->type = FILTER_DENY。
 * 返回：NB_OK。
 * 坑：除 "permit" 外一律按 deny 处理，勿把 dynamic 等取值误算。
 */
static int
lib_access_list_entry_action_modify(struct nb_cb_modify_args *args)
{
	const char *filter_type;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	filter_type = yang_dnode_get_string(args->dnode, NULL);
	if (strcmp(filter_type, "permit") == 0)
		f->type = FILTER_PERMIT;
	else
		f->type = FILTER_DENY;

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/ipv4-prefix
 */
/*
 * lib_access_list_entry_ipv4_prefix_modify —— entry/ipv4-prefix 的 modify 回调。
 * 逻辑：① NB_EV_VALIDATE 阶段用 acl_zebra_is_dup 查重，重复则报错返回；
 *   ② NB_EV_APPLY 阶段把规则标记为 zebra 风格（f->cisco=0），
 *   并把前缀读入 f->u.zfilter.prefix。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int
lib_access_list_entry_ipv4_prefix_modify(struct nb_cb_modify_args *args)
{
	struct filter_zebra *fz;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_zebra_is_dup(
			    args->dnode,
			    yang_dnode_get_enum(args->dnode, "../../type"))) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	f->cisco = 0;
	fz = &f->u.zfilter;
	yang_dnode_get_prefix(&fz->prefix, args->dnode, NULL);

	return NB_OK;
}

/*
 * lib_access_list_entry_ipv4_prefix_destroy —— entry/ipv4-prefix 的 destroy 回调：
 *   把 f->u.zfilter.prefix 清零（规则本身不删除，仅清空前缀）。
 * 返回：NB_OK。
 */
static int
lib_access_list_entry_ipv4_prefix_destroy(struct nb_cb_destroy_args *args)
{
	struct filter_zebra *fz;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fz = &f->u.zfilter;
	memset(&fz->prefix, 0, sizeof(fz->prefix));

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/ipv4-exact-match
 */
/*
 * lib_access_list_entry_ipv4_exact_match_modify —— entry/ipv4-exact-match 的 modify 回调。
 * 逻辑：VALIDATE 阶段用 acl_zebra_is_dup 查重；APPLY 阶段把布尔值写入
 *   f->u.zfilter.exact。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int
lib_access_list_entry_ipv4_exact_match_modify(struct nb_cb_modify_args *args)
{
	struct filter_zebra *fz;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_zebra_is_dup(
			    args->dnode,
			    yang_dnode_get_enum(args->dnode, "../../type"))) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fz = &f->u.zfilter;
	fz->exact = yang_dnode_get_bool(args->dnode, NULL);

	return NB_OK;
}

/*
 * lib_access_list_entry_ipv4_exact_match_destroy —— entry/ipv4-exact-match 的 destroy 回调：
 *   把 f->u.zfilter.exact 复位为 0。
 * 返回：NB_OK。
 */
static int
lib_access_list_entry_ipv4_exact_match_destroy(struct nb_cb_destroy_args *args)
{
	struct filter_zebra *fz;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fz = &f->u.zfilter;
	fz->exact = 0;

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/host
 */
/*
 * lib_access_list_entry_host_modify —— entry/host 的 modify 回调（标准 ACL 单主机）。
 * 逻辑：VALIDATE 查重；APPLY 置 f->cisco=1、ipv6=false，写 fc->addr，
 *   并把通配符掩码设为 host 全 0（要求精确匹配）。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int
lib_access_list_entry_host_modify(struct nb_cb_modify_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	f->cisco = 1;
	fc = &f->u.cfilter;
	fc->ipv6 = false;
	yang_dnode_get_ipv4(&fc->addr, args->dnode, NULL);
	fc->addr_mask.s_addr = CISCO_BIN_HOST_WILDCARD_MASK;

	return NB_OK;
}

/*
 * lib_access_list_entry_host_destroy —— entry/host 的 destroy 回调（标准 ACL 单主机）。
 * 逻辑：复位地址与掩码（cisco_unset_addr_mask），回到“未设置”状态。
 * 返回：NB_OK。
 */
static int
lib_access_list_entry_host_destroy(struct nb_cb_destroy_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = false;
	cisco_unset_addr_mask(&fc->addr, &fc->addr_mask);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/network
 */
/*
 * lib_access_list_entry_network_create —— entry/network 容器节点的 create 回调。
 * 本节点自身无数据，实际处理都在子节点（address / mask）回调中完成，
 *   故这里直接返回 NB_OK。
 */
static int lib_access_list_entry_network_create(struct nb_cb_create_args *args)
{
	/* Nothing to do here, everything is done in children callbacks */
	return NB_OK;
}

/*
 * lib_access_list_entry_network_destroy —— entry/network 的 destroy 回调：
 *   复位标准 ACL 的地址 / 掩码（cisco_unset_addr_mask）。
 * 返回：NB_OK。
 */
static int lib_access_list_entry_network_destroy(struct nb_cb_destroy_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	cisco_unset_addr_mask(&fc->addr, &fc->addr_mask);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/network/address
 */
/*
 * lib_access_list_entry_network_address_modify —— entry/network/address 的 modify 回调。
 * 逻辑：VALIDATE 查重；APPLY 置 cisco=1、ipv6=false，把地址写入 fc->addr。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int
lib_access_list_entry_network_address_modify(struct nb_cb_modify_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	f->cisco = 1;
	fc = &f->u.cfilter;
	fc->ipv6 = false;
	yang_dnode_get_ipv4(&fc->addr, args->dnode, NULL);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/network/mask
 */
/*
 * lib_access_list_entry_network_mask_modify —— entry/network/mask 的 modify 回调。
 * 逻辑：VALIDATE 查重；APPLY 把通配符掩码写入 fc->addr_mask（置 cisco、ipv6=false）。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int
lib_access_list_entry_network_mask_modify(struct nb_cb_modify_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	f->cisco = 1;
	fc = &f->u.cfilter;
	fc->ipv6 = false;
	yang_dnode_get_ipv4(&fc->addr_mask, args->dnode, NULL);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/source-any
 */
/*
 * lib_access_list_entry_source_any_create —— entry/source-any 的 create 回调。
 * 逻辑：VALIDATE 查重；APPLY 置 cisco=1、地址 INADDR_ANY、掩码为 any 通配符
 *   （CISCO_BIN_ANY_WILDCARD_MASK），表示匹配任意源。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int
lib_access_list_entry_source_any_create(struct nb_cb_create_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	f->cisco = 1;
	fc = &f->u.cfilter;
	fc->ipv6 = false;
	fc->addr.s_addr = INADDR_ANY;
	fc->addr_mask.s_addr = CISCO_BIN_ANY_WILDCARD_MASK;

	return NB_OK;
}

/*
 * lib_access_list_entry_source_any_destroy —— entry/source-any 的 destroy 回调：
 *   复位地址 / 掩码为未设置（cisco_unset_addr_mask）。
 * 返回：NB_OK。
 */
static int
lib_access_list_entry_source_any_destroy(struct nb_cb_destroy_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = false;
	cisco_unset_addr_mask(&fc->addr, &fc->addr_mask);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/destination-host
 */
/*
 * lib_access_list_entry_destination_host_modify —— entry/destination-host 的 modify 回调
 *   （扩展 ACL 的目的单主机）。
 * 逻辑：VALIDATE 查重；APPLY 置 extended=1，把目的地址写入 fc->wtf.mask，
 *   通配符掩码设为 host 全 0。
 * 坑：IPv4 扩展的目的地址借用了 wtf.mask 字段存储。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_access_list_entry_destination_host_modify(
	struct nb_cb_modify_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = false;
	fc->extended = 1;
	yang_dnode_get_ipv4(&fc->wtf.mask, args->dnode, NULL);
	fc->wtf.mask_mask.s_addr = CISCO_BIN_HOST_WILDCARD_MASK;

	return NB_OK;
}

/*
 * lib_access_list_entry_destination_host_destroy —— entry/destination-host 的 destroy 回调：
 *   置 extended=0 并复位目的地址 / 掩码。
 * 返回：NB_OK。
 */
static int lib_access_list_entry_destination_host_destroy(
	struct nb_cb_destroy_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = false;
	fc->extended = 0;
	cisco_unset_addr_mask(&fc->wtf.mask, &fc->wtf.mask_mask);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/destination-network
 */
/*
 * lib_access_list_entry_destination_network_create —— entry/destination-network
 *   容器节点的 create 回调，无自身数据，交由子节点回调处理。
 */
static int
lib_access_list_entry_destination_network_create(struct nb_cb_create_args *args)
{
	/* Nothing to do here, everything is done in children callbacks */
	return NB_OK;
}

/*
 * lib_access_list_entry_destination_network_destroy —— entry/destination-network 的 destroy 回调：
 *   置 extended=0 并复位目的地址 / 掩码。
 * 返回：NB_OK。
 */
static int lib_access_list_entry_destination_network_destroy(
	struct nb_cb_destroy_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->extended = 0;
	cisco_unset_addr_mask(&fc->wtf.mask, &fc->wtf.mask_mask);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/destination-network/address
 */
/*
 * lib_access_list_entry_destination_network_address_modify
 *   —— entry/destination-network/address 的 modify 回调。
 * 逻辑：VALIDATE 查重；APPLY 置 extended=1，把目的网络地址写入 fc->wtf.mask。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_access_list_entry_destination_network_address_modify(
	struct nb_cb_modify_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = false;
	fc->extended = 1;
	yang_dnode_get_ipv4(&fc->wtf.mask, args->dnode, NULL);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/destination-network/mask
 */
/*
 * lib_access_list_entry_destination_network_mask_modify
 *   —— entry/destination-network/mask 的 modify 回调：
 *   置 extended=1，把目的通配符掩码写入 fc->wtf.mask_mask。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_access_list_entry_destination_network_mask_modify(
	struct nb_cb_modify_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = false;
	fc->extended = 1;
	yang_dnode_get_ipv4(&fc->wtf.mask_mask, args->dnode, NULL);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/destination-any
 */
/*
 * lib_access_list_entry_destination_any_create —— entry/destination-any 的 create 回调：
 *   置 extended=1，目的地址 INADDR_ANY、掩码 any 通配符（匹配任意目的）。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_access_list_entry_destination_any_create(
	struct nb_cb_create_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = false;
	fc->extended = 1;
	fc->wtf.mask.s_addr = INADDR_ANY;
	fc->wtf.mask_mask.s_addr = CISCO_BIN_ANY_WILDCARD_MASK;

	return NB_OK;
}

/*
 * lib_access_list_entry_destination_any_destroy —— entry/destination-any 的 destroy 回调：
 *   置 extended=0 并复位目的地址 / 掩码。
 * 返回：NB_OK。
 */
static int lib_access_list_entry_destination_any_destroy(
	struct nb_cb_destroy_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = false;
	fc->extended = 0;
	cisco_unset_addr_mask(&fc->wtf.mask, &fc->wtf.mask_mask);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/ipv6-host
 */
/*
 * lib_access_list_entry_ipv6_host_modify —— entry/ipv6-host 的 modify 回调（IPv6 单主机）。
 * 逻辑：VALIDATE 查重；APPLY 置 cisco=1、ipv6=true，写 fc->addr6，掩码清零。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_access_list_entry_ipv6_host_modify(struct nb_cb_modify_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	f->cisco = 1;
	fc = &f->u.cfilter;
	fc->ipv6 = true;
	yang_dnode_get_ipv6(&fc->addr6, args->dnode, NULL);
	memset(&fc->addr6_mask, 0, sizeof(fc->addr6_mask));

	return NB_OK;
}

/*
 * lib_access_list_entry_ipv6_host_destroy —— entry/ipv6-host 的 destroy 回调：
 *   置 ipv6=true 并把地址 addr6 清零（随后掩码亦复位）。
 * 返回：NB_OK。
 */
static int lib_access_list_entry_ipv6_host_destroy(struct nb_cb_destroy_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = true;
	memset(&fc->addr6, 0, sizeof(fc->addr6));
	memset(&fc->addr6_mask, 0, sizeof(fc->addr6_mask));

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/ipv6-network
 */
/*
 * lib_access_list_entry_ipv6_network_create —— entry/ipv6-network 容器 create 回调，
 *   无自身数据，交由子节点回调处理。
 */
static int lib_access_list_entry_ipv6_network_create(struct nb_cb_create_args *args)
{
	/* Nothing to do here, everything is done in children callbacks */
	return NB_OK;
}

/*
 * lib_access_list_entry_ipv6_network_destroy —— entry/ipv6-network 的 destroy 回调：
 *   置 ipv6=true、extended=0，并把 addr6 / addr6_mask 清零。
 * 返回：NB_OK。
 * 注意：此处 memset 长度传的是 sizeof(addr6_mask)，对 addr6 的清理是等长复用。
 */
static int lib_access_list_entry_ipv6_network_destroy(struct nb_cb_destroy_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = true;
	fc->extended = 0;
	memset(&fc->addr6, 0, sizeof(fc->addr6_mask));
	memset(&fc->addr6_mask, 0, sizeof(fc->addr6_mask));

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/ipv6-network/address
 */
/*
 * lib_access_list_entry_ipv6_network_address_modify —— entry/ipv6-network/address 的 modify 回调。
 * 逻辑：VALIDATE 阶段用 acl_cisco_is_dup 查重；APPLY 阶段置 cisco=1、ipv6=true，
 *   把节点上的 IPv6 地址写入 fc->addr6。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_access_list_entry_ipv6_network_address_modify(struct nb_cb_modify_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	f->cisco = 1;
	fc = &f->u.cfilter;
	fc->ipv6 = true;
	yang_dnode_get_ipv6(&fc->addr6, args->dnode, NULL);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/network/mask
 */
/*
 * lib_access_list_entry_ipv6_network_mask_modify —— entry/ipv6-network/mask 的 modify 回调
 *   （函数名沿用旧 XPath 命名，注释块声明的 XPath 为 .../network/mask）。
 * 逻辑：VALIDATE 查重；APPLY 置 cisco=1、ipv6=true，把掩码写入 fc->addr6_mask。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_access_list_entry_ipv6_network_mask_modify(struct nb_cb_modify_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	f->cisco = 1;
	fc = &f->u.cfilter;
	fc->ipv6 = true;
	yang_dnode_get_ipv6(&fc->addr6_mask, args->dnode, NULL);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/ipv6-source-any
 */
/*
 * lib_access_list_entry_ipv6_source_any_create —— entry/ipv6-source-any 的 create 回调。
 * 逻辑：VALIDATE 查重；APPLY 置 cisco=1、ipv6=true，源地址 addr6 清零、
 *   源掩码 addr6_mask 全 0xFF（IPv6 中 wildcard 全 1 表示“任意源”）。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_access_list_entry_ipv6_source_any_create(struct nb_cb_create_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	f->cisco = 1;
	fc = &f->u.cfilter;
	fc->ipv6 = true;
	memset(&fc->addr6, 0, sizeof(fc->addr6));
	memset(&fc->addr6_mask, 0xFF, sizeof(fc->addr6_mask));

	return NB_OK;
}

/*
 * lib_access_list_entry_ipv6_source_any_destroy —— entry/ipv6-source-any 的 destroy 回调：
 *   置 ipv6=true，并把 addr6 / addr6_mask 清零。
 * 返回：NB_OK。
 */
static int lib_access_list_entry_ipv6_source_any_destroy(struct nb_cb_destroy_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = true;
	memset(&fc->addr6, 0, sizeof(fc->addr6));
	memset(&fc->addr6_mask, 0, sizeof(fc->addr6_mask));

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/ipv6-destination-host
 */
/*
 * lib_access_list_entry_ipv6_destination_host_modify —— entry/ipv6-destination-host 的
 *   modify 回调（扩展 ACL 单一目的主机）。
 * 逻辑：VALIDATE 查重；APPLY 置 ipv6=true、extended=1，把目的地址写入 fc->sadr6.dst，
 *   并把目的掩码 dst_mask 清零（精确匹配单主机）。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_access_list_entry_ipv6_destination_host_modify(struct nb_cb_modify_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = true;
	fc->extended = 1;
	yang_dnode_get_ipv6(&fc->sadr6.dst, args->dnode, NULL);
	memset(&fc->sadr6.dst_mask, 0, sizeof(fc->sadr6.dst_mask));

	return NB_OK;
}

/*
 * lib_access_list_entry_ipv6_destination_host_destroy —— entry/ipv6-destination-host 的
 *   destroy 回调：置 ipv6=true、extended=0，并清空 sadr6.dst / sadr6.dst_mask。
 * 返回：NB_OK。
 */
static int lib_access_list_entry_ipv6_destination_host_destroy(struct nb_cb_destroy_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = true;
	fc->extended = 0;
	memset(&fc->sadr6.dst, 0, sizeof(fc->sadr6.dst));
	memset(&fc->sadr6.dst_mask, 0, sizeof(fc->sadr6.dst_mask));

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/ipv6-destination-network
 */
/*
 * lib_access_list_entry_ipv6_destination_network_create —— entry/ipv6-destination-network
 *   容器 create 回调，无自身数据，交由子节点回调处理。
 */
static int lib_access_list_entry_ipv6_destination_network_create(struct nb_cb_create_args *args)
{
	/* Nothing to do here, everything is done in children callbacks */
	return NB_OK;
}

/*
 * lib_access_list_entry_ipv6_destination_network_destroy —— entry/ipv6-destination-network 的
 *   destroy 回调：置 ipv6=true、extended=1，并清空 addr6 / addr6_mask。
 * 返回：NB_OK。
 */
static int lib_access_list_entry_ipv6_destination_network_destroy(struct nb_cb_destroy_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = true;
	fc->extended = 1;
	memset(&fc->addr6, 0, sizeof(fc->addr6_mask));
	memset(&fc->addr6_mask, 0, sizeof(fc->addr6_mask));

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/ipv6-destination-network/address
 */
/*
 * lib_access_list_entry_ipv6_destination_network_address_modify
 *   —— entry/ipv6-destination-network/address 的 modify 回调。
 * 逻辑：VALIDATE 查重；APPLY 置 ipv6=true、extended=1，
 *   把目的网络地址写入 fc->sadr6.dst。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int
lib_access_list_entry_ipv6_destination_network_address_modify(struct nb_cb_modify_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = true;
	fc->extended = 1;
	yang_dnode_get_ipv6(&fc->sadr6.dst, args->dnode, NULL);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/ipv6-destination-network/mask
 */
/*
 * lib_access_list_entry_ipv6_destination_network_mask_modify
 *   —— entry/ipv6-destination-network/mask 的 modify 回调。
 * 逻辑：VALIDATE 查重；APPLY 置 ipv6=true、extended=1，
 *   把目的网络掩码写入 fc->sadr6.dst_mask。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_access_list_entry_ipv6_destination_network_mask_modify(struct nb_cb_modify_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = true;
	fc->extended = 1;
	yang_dnode_get_ipv6(&fc->sadr6.dst_mask, args->dnode, NULL);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/ipv6-destination-any
 */
/*
 * lib_access_list_entry_ipv6_destination_any_create —— entry/ipv6-destination-any 的 create 回调。
 * 逻辑：VALIDATE 查重；APPLY 置 ipv6=true、extended=1，目的地址清零、
 *   目的掩码 dst_mask 全 0xFF（匹配任意 IPv6 目的）。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_access_list_entry_ipv6_destination_any_create(struct nb_cb_create_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_cisco_is_dup(args->dnode)) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = true;
	fc->extended = 1;
	memset(&fc->sadr6.dst, 0, sizeof(fc->sadr6.dst));
	memset(&fc->sadr6.dst_mask, 0xFF, sizeof(fc->sadr6.dst_mask));

	return NB_OK;
}

/*
 * lib_access_list_entry_ipv6_destination_any_destroy —— entry/ipv6-destination-any 的 destroy 回调：
 *   置 ipv6=true、extended=0，并清空 sadr6.dst / sadr6.dst_mask。
 * 返回：NB_OK。
 */
static int lib_access_list_entry_ipv6_destination_any_destroy(struct nb_cb_destroy_args *args)
{
	struct filter_cisco *fc;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fc = &f->u.cfilter;
	fc->ipv6 = true;
	fc->extended = 0;
	memset(&fc->sadr6.dst, 0, sizeof(fc->sadr6.dst));
	memset(&fc->sadr6.dst_mask, 0, sizeof(fc->sadr6.dst_mask));

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/access-list/entry/any
 */
/*
 * lib_access_list_entry_any_create —— entry/any 的 create 回调（zebra 风格匹配任意）。
 * 逻辑：VALIDATE 用 acl_zebra_is_dup 查重（读 ../../type 取列表类型）；
 *   APPLY 置 f->cisco=0，清空 fz->prefix，并按 ../../type 设 prefix.family
 *   （YALT_IPV4→AF_INET / YALT_IPV6→AF_INET6 / YALT_MAC→AF_ETHERNET），
 *   配合前缀长度 0 表示“匹配任意”。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_access_list_entry_any_create(struct nb_cb_create_args *args)
{
	struct filter_zebra *fz;
	struct filter *f;
	int type;

	/* Don't allow duplicated values. */
	if (args->event == NB_EV_VALIDATE) {
		if (acl_zebra_is_dup(
			    args->dnode,
			    yang_dnode_get_enum(args->dnode, "../../type"))) {
			snprintfrr(args->errmsg, args->errmsg_len,
				   "duplicated access list value: %s",
				   yang_dnode_get_string(args->dnode, NULL));
			return NB_ERR_VALIDATION;
		}
		return NB_OK;
	}

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	f->cisco = 0;
	fz = &f->u.zfilter;
	memset(&fz->prefix, 0, sizeof(fz->prefix));

	type = yang_dnode_get_enum(args->dnode, "../../type");
	switch (type) {
	case YALT_IPV4:
		fz->prefix.family = AF_INET;
		break;
	case YALT_IPV6:
		fz->prefix.family = AF_INET6;
		break;
	case YALT_MAC:
		fz->prefix.family = AF_ETHERNET;
		break;
	}

	return NB_OK;
}

/*
 * lib_access_list_entry_any_destroy —— entry/any 的 destroy 回调：
 *   把 f->u.zfilter.prefix.family 复位为 AF_UNSPEC。
 * 返回：NB_OK。
 */
static int lib_access_list_entry_any_destroy(struct nb_cb_destroy_args *args)
{
	struct filter_zebra *fz;
	struct filter *f;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	f = nb_running_get_entry(args->dnode, NULL, true);
	fz = &f->u.zfilter;
	fz->prefix.family = AF_UNSPEC;

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/prefix-list
 */
/*
 * lib_prefix_list_create —— /frr-filter:lib/prefix-list 的 create 回调。
 * 逻辑：仅 NB_EV_APPLY 生效；按 type 取 AFI（0=ipv4→AFI_IP，1=ipv6→AFI_IP6），
 *   用 prefix_list_get 取回或新建 prefix_list，再用 nb_running_set_entry 绑定到 dnode。
 * 返回：NB_OK。
 * 注意：此处直接比较裸 0 / 1，与枚举 YPLT_IPV4 / YPLT_IPV6 取值一致。
 */
static int lib_prefix_list_create(struct nb_cb_create_args *args)
{
	struct prefix_list *pl = NULL;
	const char *name;
	int type;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	type = yang_dnode_get_enum(args->dnode, "type");
	name = yang_dnode_get_string(args->dnode, "name");
	switch (type) {
	case 0: /* ipv4 */
		pl = prefix_list_get(AFI_IP, 0, name);
		break;
	case 1: /* ipv6 */
		pl = prefix_list_get(AFI_IP6, 0, name);
		break;
	}

	nb_running_set_entry(args->dnode, pl);

	return NB_OK;
}

/*
 * lib_prefix_list_destroy —— /frr-filter:lib/prefix-list 的 destroy 回调。
 * 逻辑：APPLY 阶段 nb_running_unset_entry 解绑 dnode，再 prefix_list_delete
 *   删除整条前缀列表。
 * 返回：NB_OK。
 */
static int lib_prefix_list_destroy(struct nb_cb_destroy_args *args)
{
	struct prefix_list *pl;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	pl = nb_running_unset_entry(args->dnode);
	prefix_list_delete(pl);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/prefix-list/remark
 */
/*
 * lib_prefix_list_remark_modify —— /frr-filter:lib/prefix-list/remark 的 modify 回调。
 * 逻辑：APPLY 阶段先 XFREE 旧的 pl->desc，再把新 remark 字符串 XSTRDUP 存入 pl->desc。
 * 返回：NB_OK。
 * 注意：desc 使用 MTYPE_TMP 内存类型。
 */
static int lib_prefix_list_remark_modify(struct nb_cb_modify_args *args)
{
	struct prefix_list *pl;
	const char *remark;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	pl = nb_running_get_entry(args->dnode, NULL, true);
	if (pl->desc)
		XFREE(MTYPE_TMP, pl->desc);

	remark = yang_dnode_get_string(args->dnode, NULL);
	pl->desc = XSTRDUP(MTYPE_TMP, remark);

	return NB_OK;
}

/*
 * lib_prefix_list_remark_destroy —— /frr-filter:lib/prefix-list/remark 的 destroy 回调：
 *   APPLY 阶段释放 pl->desc（XFREE），表示该前缀列表不再有备注。
 * 返回：NB_OK。
 */
static int lib_prefix_list_remark_destroy(struct nb_cb_destroy_args *args)
{
	struct prefix_list *pl;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	pl = nb_running_get_entry(args->dnode, NULL, true);
	if (pl->desc)
		XFREE(MTYPE_TMP, pl->desc);

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/prefix-list/entry
 */
/*
 * lib_prefix_list_entry_create —— /frr-filter:lib/prefix-list/entry 的 create 回调。
 * 逻辑：APPLY 阶段取父 prefix-list，prefix_list_entry_new 新建条目，
 *   设 ple->pl 与 ple->seq（读 sequence 叶子），调用 prefix_list_entry_set_empty 初始化，
 *   最后 nb_running_set_entry 绑定 dnode。
 * 返回：NB_OK。
 * 说明：条目各匹配字段由后续子回调（action / ipv4-prefix 等）填充。
 */
static int lib_prefix_list_entry_create(struct nb_cb_create_args *args)
{
	struct prefix_list_entry *ple;
	struct prefix_list *pl;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	pl = nb_running_get_entry(args->dnode, NULL, true);
	ple = prefix_list_entry_new();
	ple->pl = pl;
	ple->seq = yang_dnode_get_uint32(args->dnode, "sequence");
	prefix_list_entry_set_empty(ple);
	nb_running_set_entry(args->dnode, ple);

	return NB_OK;
}

/*
 * lib_prefix_list_entry_destroy —— /frr-filter:lib/prefix-list/entry 的 destroy 回调。
 * 逻辑：APPLY 阶段 nb_running_unset_entry 解绑；若条目已 installed 到内核
 *   （调用 prefix_list_entry_delete2 做删除），否则直接 prefix_list_entry_free。
 * 返回：NB_OK。
 */
static int lib_prefix_list_entry_destroy(struct nb_cb_destroy_args *args)
{
	struct prefix_list_entry *ple;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	ple = nb_running_unset_entry(args->dnode);
	if (ple->installed)
		prefix_list_entry_delete2(ple);
	else
		prefix_list_entry_free(ple);

	return NB_OK;
}

/*
 * lib_prefix_list_entry_apply_finish —— /frr-filter:lib/prefix-list/entry 的 apply_finish 回调。
 * 逻辑：取回条目后调用 prefix_list_entry_update_finish，收尾一次前缀列表条目更新。
 *   update 流程由各子回调里的 prefix_list_entry_update_start 启动，
 *   即使被多个子回调多次调用 start，也会因 ple->installed 保护而只真正执行一次。
 * 返回：无（void）。
 */
static void
lib_prefix_list_entry_apply_finish(struct nb_cb_apply_finish_args *args)
{
	struct prefix_list_entry *ple;

	ple = nb_running_get_entry(args->dnode, NULL, true);

	/*
	 * Finish prefix entry update procedure. The procedure is started in
	 * children callbacks. `prefix_list_entry_update_start` can be called
	 * multiple times if multiple children are modified, but it is actually
	 * executed only once because of the protection by `ple->installed`.
	 */
	prefix_list_entry_update_finish(ple);
}

/*
 * XPath: /frr-filter:lib/prefix-list/entry/action
 */
/*
 * lib_prefix_list_entry_action_modify —— /frr-filter:lib/prefix-list/entry/action 的 modify 回调。
 * 逻辑：APPLY 阶段先调 prefix_list_entry_update_start 启动更新，
 *   再读动作枚举：YPLA_PERMIT → ple->type = PREFIX_PERMIT，否则 PREFIX_DENY。
 * 返回：NB_OK。
 */
static int lib_prefix_list_entry_action_modify(struct nb_cb_modify_args *args)
{
	struct prefix_list_entry *ple;
	int action_type;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	ple = nb_running_get_entry(args->dnode, NULL, true);

	/* Start prefix entry update procedure. */
	prefix_list_entry_update_start(ple);

	action_type = yang_dnode_get_enum(args->dnode, NULL);
	if (action_type == YPLA_PERMIT)
		ple->type = PREFIX_PERMIT;
	else
		ple->type = PREFIX_DENY;

	return NB_OK;
}

/*
 * lib_prefix_list_entry_prefix_modify —— 前缀列表条目「前缀」字段的通用 modify 实现，
 *   被 ipv4-prefix / ipv6-prefix 两个 YANG 叶子回调复用。
 * 逻辑：APPLY 阶段 update_start；读前缀到 ple->prefix；
 *   再用 apply_mask 归一化网络地址，若与原值不同则打印 zlog_info 并纠正，
 *   保证存储的始终是“网络地址 + 掩码”形式。
 * 返回：NB_OK。
 * 坑：用户可填入带主机位的地址，这里静默修正为按掩码归零后的网络地址。
 */
static int lib_prefix_list_entry_prefix_modify(struct nb_cb_modify_args *args)
{
	struct prefix_list_entry *ple;
	struct prefix p;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	ple = nb_running_get_entry(args->dnode, NULL, true);

	/* Start prefix entry update procedure. */
	prefix_list_entry_update_start(ple);

	yang_dnode_get_prefix(&ple->prefix, args->dnode, NULL);

	/* Apply mask and correct original address if necessary. */
	prefix_copy(&p, &ple->prefix);
	apply_mask(&p);
	if (!prefix_same(&ple->prefix, &p)) {
		zlog_info("%s: bad network %pFX correcting it to %pFX",
			  __func__, &ple->prefix, &p);
		prefix_copy(&ple->prefix, &p);
	}

	return NB_OK;
}

/*
 * lib_prefix_list_entry_prefix_destroy —— 前缀列表条目「前缀」字段的通用 destroy 实现，
 *   被 ipv4-prefix / ipv6-prefix 两个 YANG 叶子回调复用。
 * 逻辑：APPLY 阶段 update_start，然后把 ple->prefix 清零。
 * 返回：NB_OK。
 */
static int lib_prefix_list_entry_prefix_destroy(struct nb_cb_destroy_args *args)
{
	struct prefix_list_entry *ple;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	ple = nb_running_get_entry(args->dnode, NULL, true);

	/* Start prefix entry update procedure. */
	prefix_list_entry_update_start(ple);

	memset(&ple->prefix, 0, sizeof(ple->prefix));

	return NB_OK;
}

/*
 * XPath: /frr-filter:lib/prefix-list/entry/ipv4-prefix
 */
/*
 * lib_prefix_list_entry_ipv4_prefix_modify —— /frr-filter:lib/prefix-list/entry/ipv4-prefix 的 modify 回调。
 * 逻辑：VALIDATE 阶段取父 prefix-list 节点，用 prefix_list_nb_validate_v4_af_type
 *   校验该列表确为 IPv4 类型；APPLY 委托通用实现 lib_prefix_list_entry_prefix_modify。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int
lib_prefix_list_entry_ipv4_prefix_modify(struct nb_cb_modify_args *args)
{
	if (args->event == NB_EV_VALIDATE) {
		const struct lyd_node *plist_dnode =
			yang_dnode_get_parent(args->dnode, "prefix-list");

		return prefix_list_nb_validate_v4_af_type(
			plist_dnode, args->errmsg, args->errmsg_len);
	}

	return lib_prefix_list_entry_prefix_modify(args);
}

/*
 * lib_prefix_list_entry_ipv4_prefix_destroy —— /frr-filter:lib/prefix-list/entry/ipv4-prefix 的 destroy 回调：
 *   非 APPLY 事件直接返回 NB_OK，否则委托 lib_prefix_list_entry_prefix_destroy。
 * 返回：NB_OK。
 */
static int
lib_prefix_list_entry_ipv4_prefix_destroy(struct nb_cb_destroy_args *args)
{

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	return lib_prefix_list_entry_prefix_destroy(args);
}

/*
 * XPath: /frr-filter:lib/prefix-list/entry/ipv6-prefix
 */
/*
 * lib_prefix_list_entry_ipv6_prefix_modify —— /frr-filter:lib/prefix-list/entry/ipv6-prefix 的 modify 回调。
 * 逻辑：VALIDATE 阶段用 prefix_list_nb_validate_v6_af_type 校验所属列表为 IPv6 类型；
 *   APPLY 委托通用实现 lib_prefix_list_entry_prefix_modify。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int
lib_prefix_list_entry_ipv6_prefix_modify(struct nb_cb_modify_args *args)
{

	if (args->event == NB_EV_VALIDATE) {
		const struct lyd_node *plist_dnode =
			yang_dnode_get_parent(args->dnode, "prefix-list");

		return prefix_list_nb_validate_v6_af_type(
			plist_dnode, args->errmsg, args->errmsg_len);
	}

	return lib_prefix_list_entry_prefix_modify(args);
}

/*
 * lib_prefix_list_entry_ipv6_prefix_destroy —— /frr-filter:lib/prefix-list/entry/ipv6-prefix 的 destroy 回调：
 *   非 APPLY 直接返回，否则委托 lib_prefix_list_entry_prefix_destroy。
 * 返回：NB_OK。
 */
static int
lib_prefix_list_entry_ipv6_prefix_destroy(struct nb_cb_destroy_args *args)
{

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	return lib_prefix_list_entry_prefix_destroy(args);
}

/*
 * XPath: /frr-filter:lib/prefix-list/entry/ipv4-prefix-length-greater-or-equal
 */
/*
 * lib_prefix_list_entry_ipv4_prefix_length_greater_or_equal_modify
 *   —— /frr-filter:lib/prefix-list/entry/ipv4-prefix-length-greater-or-equal 的 modify 回调。
 * 逻辑：VALIDATE 先调 prefix_list_length_validate 校验 ge / le 组合是否合法，
 *   再取父 prefix-list 校验其确为 IPv4 类型；APPLY 委托通用实现写入 ple->ge。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_prefix_list_entry_ipv4_prefix_length_greater_or_equal_modify(
	struct nb_cb_modify_args *args)
{
	if (args->event == NB_EV_VALIDATE
	    && prefix_list_length_validate(args) != NB_OK)
		return NB_ERR_VALIDATION;

	if (args->event == NB_EV_VALIDATE) {
		const struct lyd_node *plist_dnode =
			yang_dnode_get_parent(args->dnode, "prefix-list");

		return prefix_list_nb_validate_v4_af_type(
			plist_dnode, args->errmsg, args->errmsg_len);
	}

	return lib_prefix_list_entry_prefix_length_greater_or_equal_modify(
		args);
}

/*
 * lib_prefix_list_entry_ipv4_prefix_length_greater_or_equal_destroy
 *   —— ipv4-prefix-length-greater-or-equal 的 destroy 回调：
 *   VALIDATE 校验所属列表为 IPv4 类型，否则委托通用实现（把 ple->ge 复位为 0）。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_prefix_list_entry_ipv4_prefix_length_greater_or_equal_destroy(
	struct nb_cb_destroy_args *args)
{
	if (args->event == NB_EV_VALIDATE) {
		const struct lyd_node *plist_dnode =
			yang_dnode_get_parent(args->dnode, "prefix-list");

		return prefix_list_nb_validate_v4_af_type(
			plist_dnode, args->errmsg, args->errmsg_len);
	}

	return lib_prefix_list_entry_prefix_length_greater_or_equal_destroy(
		args);
}

/*
 * XPath: /frr-filter:lib/prefix-list/entry/ipv4-prefix-length-lesser-or-equal
 */
/*
 * lib_prefix_list_entry_ipv4_prefix_length_lesser_or_equal_modify
 *   —— /frr-filter:lib/prefix-list/entry/ipv4-prefix-length-lesser-or-equal 的 modify 回调。
 * 逻辑：VALIDATE 校验 ge / le 组合与 IPv4 列表类型；APPLY 委托通用实现写入 ple->le。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_prefix_list_entry_ipv4_prefix_length_lesser_or_equal_modify(
	struct nb_cb_modify_args *args)
{
	if (args->event == NB_EV_VALIDATE
	    && prefix_list_length_validate(args) != NB_OK)
		return NB_ERR_VALIDATION;

	if (args->event == NB_EV_VALIDATE) {
		const struct lyd_node *plist_dnode =
			yang_dnode_get_parent(args->dnode, "prefix-list");

		return prefix_list_nb_validate_v4_af_type(
			plist_dnode, args->errmsg, args->errmsg_len);
	}

	return lib_prefix_list_entry_prefix_length_lesser_or_equal_modify(
		args);
}

/*
 * lib_prefix_list_entry_ipv4_prefix_length_lesser_or_equal_destroy
 *   —— ipv4-prefix-length-lesser-or-equal 的 destroy 回调：
 *   VALIDATE 校验 IPv4 列表类型，否则委托通用实现（把 ple->le 复位为 0）。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_prefix_list_entry_ipv4_prefix_length_lesser_or_equal_destroy(
	struct nb_cb_destroy_args *args)
{
	if (args->event == NB_EV_VALIDATE) {
		const struct lyd_node *plist_dnode =
			yang_dnode_get_parent(args->dnode, "prefix-list");

		return prefix_list_nb_validate_v4_af_type(
			plist_dnode, args->errmsg, args->errmsg_len);
	}

	return lib_prefix_list_entry_prefix_length_lesser_or_equal_destroy(
		args);
}

/*
 * XPath: /frr-filter:lib/prefix-list/entry/ipv6-prefix-length-greater-or-equal
 */
/*
 * lib_prefix_list_entry_ipv6_prefix_length_greater_or_equal_modify
 *   —— /frr-filter:lib/prefix-list/entry/ipv6-prefix-length-greater-or-equal 的 modify 回调。
 * 逻辑：VALIDATE 校验 ge / le 组合并确认列表为 IPv6 类型；APPLY 委托通用实现写入 ple->ge。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_prefix_list_entry_ipv6_prefix_length_greater_or_equal_modify(
	struct nb_cb_modify_args *args)
{
	if (args->event == NB_EV_VALIDATE
	    && prefix_list_length_validate(args) != NB_OK)
		return NB_ERR_VALIDATION;

	if (args->event == NB_EV_VALIDATE) {
		const struct lyd_node *plist_dnode =
			yang_dnode_get_parent(args->dnode, "prefix-list");

		return prefix_list_nb_validate_v6_af_type(
			plist_dnode, args->errmsg, args->errmsg_len);
	}

	return lib_prefix_list_entry_prefix_length_greater_or_equal_modify(
		args);
}

/*
 * lib_prefix_list_entry_ipv6_prefix_length_greater_or_equal_destroy
 *   —— ipv6-prefix-length-greater-or-equal 的 destroy 回调：
 *   VALIDATE 校验 IPv6 列表类型，否则委托通用实现（把 ple->ge 复位为 0）。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_prefix_list_entry_ipv6_prefix_length_greater_or_equal_destroy(
	struct nb_cb_destroy_args *args)
{
	if (args->event == NB_EV_VALIDATE) {
		const struct lyd_node *plist_dnode =
			yang_dnode_get_parent(args->dnode, "prefix-list");

		return prefix_list_nb_validate_v6_af_type(
			plist_dnode, args->errmsg, args->errmsg_len);
	}

	return lib_prefix_list_entry_prefix_length_greater_or_equal_destroy(
		args);
}

/*
 * XPath: /frr-filter:lib/prefix-list/entry/ipv6-prefix-length-lesser-or-equal
 */
/*
 * lib_prefix_list_entry_ipv6_prefix_length_lesser_or_equal_modify
 *   —— /frr-filter:lib/prefix-list/entry/ipv6-prefix-length-lesser-or-equal 的 modify 回调。
 * 逻辑：VALIDATE 校验 ge / le 组合与 IPv6 列表类型；APPLY 委托通用实现写入 ple->le。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_prefix_list_entry_ipv6_prefix_length_lesser_or_equal_modify(
	struct nb_cb_modify_args *args)
{
	if (args->event == NB_EV_VALIDATE
	    && prefix_list_length_validate(args) != NB_OK)
		return NB_ERR_VALIDATION;

	if (args->event == NB_EV_VALIDATE) {
		const struct lyd_node *plist_dnode =
			yang_dnode_get_parent(args->dnode, "prefix-list");

		return prefix_list_nb_validate_v6_af_type(
			plist_dnode, args->errmsg, args->errmsg_len);
	}

	return lib_prefix_list_entry_prefix_length_lesser_or_equal_modify(
		args);
}

/*
 * lib_prefix_list_entry_ipv6_prefix_length_lesser_or_equal_destroy
 *   —— ipv6-prefix-length-lesser-or-equal 的 destroy 回调：
 *   VALIDATE 校验 IPv6 列表类型，否则委托通用实现（把 ple->le 复位为 0）。
 * 返回：NB_OK / NB_ERR_VALIDATION。
 */
static int lib_prefix_list_entry_ipv6_prefix_length_lesser_or_equal_destroy(
	struct nb_cb_destroy_args *args)
{
	if (args->event == NB_EV_VALIDATE) {
		const struct lyd_node *plist_dnode =
			yang_dnode_get_parent(args->dnode, "prefix-list");

		return prefix_list_nb_validate_v6_af_type(
			plist_dnode, args->errmsg, args->errmsg_len);
	}

	return lib_prefix_list_entry_prefix_length_lesser_or_equal_destroy(
		args);
}

/*
 * XPath: /frr-filter:lib/prefix-list/entry/any
 */
/*
 * lib_prefix_list_entry_any_create —— /frr-filter:lib/prefix-list/entry/any 的 create 回调
 *   （prefix-list 条目匹配任意前缀，对应 `prefix any`）。
 * 逻辑：VALIDATE 直接放行（能走到这里即合法）；APPLY 取回条目并 update_start，
 *   置 ple->any=true，清空前缀，并按 ../../type 设 family 与 ge / le
 *   （YPLT_IPV4: AF_INET、ge=0、le=IPV4_MAX_BITLEN；YPLT_IPV6: AF_INET6、ge=0、le=IPV6_MAX_BITLEN）。
 * 返回：NB_OK。
 */
static int lib_prefix_list_entry_any_create(struct nb_cb_create_args *args)
{
	struct prefix_list_entry *ple;
	int type;

	/*
	 * If we have gotten to this point, it's legal
	 */
	if (args->event == NB_EV_VALIDATE)
		return NB_OK;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	ple = nb_running_get_entry(args->dnode, NULL, true);

	/* Start prefix entry update procedure. */
	prefix_list_entry_update_start(ple);

	ple->any = true;

	/* Fill prefix struct from scratch. */
	memset(&ple->prefix, 0, sizeof(ple->prefix));

	type = yang_dnode_get_enum(args->dnode, "../../type");
	switch (type) {
	case YPLT_IPV4:
		ple->prefix.family = AF_INET;
		ple->ge = 0;
		ple->le = IPV4_MAX_BITLEN;
		break;
	case YPLT_IPV6:
		ple->prefix.family = AF_INET6;
		ple->ge = 0;
		ple->le = IPV6_MAX_BITLEN;
		break;
	}

	return NB_OK;
}

/*
 * lib_prefix_list_entry_any_destroy —— /frr-filter:lib/prefix-list/entry/any 的 destroy 回调：
 *   APPLY 阶段 update_start 后把 ple->any 置回 false。
 * 返回：NB_OK。
 */
static int lib_prefix_list_entry_any_destroy(struct nb_cb_destroy_args *args)
{
	struct prefix_list_entry *ple;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	ple = nb_running_get_entry(args->dnode, NULL, true);

	/* Start prefix entry update procedure. */
	prefix_list_entry_update_start(ple);

	ple->any = false;

	return NB_OK;
}

/*
 * frr_filter_info —— 本模块的 YANG 回调注册表（YANG 模块 "frr-filter"）。
 * 作用：把 YANG 模型中的每个节点 xpath 映射到 filter_nb.c 中的回调函数；
 *   北向框架在校验 / 提交候选配置时据此回调，完成配置校验与运行态落库。
 * 关键字段：
 *   .name  —— YANG 模块名 "frr-filter"；
 *   .nodes —— 节点数组，每项含 .xpath，以及 .cbs（回调集合）。
 * 回调类型含义：
 *   create / destroy      —— 列表或容器节点的建立与删除；
 *   modify                —— 叶子值变更；
 *   apply_finish          —— 子树提交完成后收尾（如触发前缀条目更新）；
 *   cli_show / cli_cmp    —— 由 filter_cli.c 提供，负责 show 输出与排序比较。
 * 阅读建议：先看 access-list 与 entry 主表，再看 entry 下各类匹配叶子，
 *   最后看 prefix-list 系列与结尾的 frr_filter_cli_info。
 * 注意：xpath 字符串必须与 YANG 模型严格一致，错一个字符回调就不会被触发；
 *   上方的 clang-format off 指令是为了保持条目的多行排版，勿随意格式化。
 */
/* clang-format off */
const struct frr_yang_module_info frr_filter_info = {
	.name = "frr-filter",
	.nodes = {
		{
			.xpath = "/frr-filter:lib/access-list",
			.cbs = {
				.create = lib_access_list_create,
				.destroy = lib_access_list_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/remark",
			.cbs = {
				.modify = lib_access_list_remark_modify,
				.destroy = lib_access_list_remark_destroy,
				.cli_show = access_list_remark_show,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry",
			.cbs = {
				.create = lib_access_list_entry_create,
				.destroy = lib_access_list_entry_destroy,
				.apply_finish = lib_access_list_entry_apply_finish,
				.cli_cmp = access_list_cmp,
				.cli_show = access_list_show,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/action",
			.cbs = {
				.modify = lib_access_list_entry_action_modify,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/ipv4-prefix",
			.cbs = {
				.modify = lib_access_list_entry_ipv4_prefix_modify,
				.destroy = lib_access_list_entry_ipv4_prefix_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/ipv4-exact-match",
			.cbs = {
				.modify = lib_access_list_entry_ipv4_exact_match_modify,
				.destroy = lib_access_list_entry_ipv4_exact_match_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/host",
			.cbs = {
				.modify = lib_access_list_entry_host_modify,
				.destroy = lib_access_list_entry_host_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/network",
			.cbs = {
				.create = lib_access_list_entry_network_create,
				.destroy = lib_access_list_entry_network_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/network/address",
			.cbs = {
				.modify = lib_access_list_entry_network_address_modify,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/network/mask",
			.cbs = {
				.modify = lib_access_list_entry_network_mask_modify,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/source-any",
			.cbs = {
				.create = lib_access_list_entry_source_any_create,
				.destroy = lib_access_list_entry_source_any_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/destination-host",
			.cbs = {
				.modify = lib_access_list_entry_destination_host_modify,
				.destroy = lib_access_list_entry_destination_host_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/destination-network",
			.cbs = {
				.create = lib_access_list_entry_destination_network_create,
				.destroy = lib_access_list_entry_destination_network_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/destination-network/address",
			.cbs = {
				.modify = lib_access_list_entry_destination_network_address_modify,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/destination-network/mask",
			.cbs = {
				.modify = lib_access_list_entry_destination_network_mask_modify,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/destination-any",
			.cbs = {
				.create = lib_access_list_entry_destination_any_create,
				.destroy = lib_access_list_entry_destination_any_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/ipv6-prefix",
			.cbs = {
				.modify = lib_access_list_entry_ipv4_prefix_modify,
				.destroy = lib_access_list_entry_ipv4_prefix_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/ipv6-exact-match",
			.cbs = {
				.modify = lib_access_list_entry_ipv4_exact_match_modify,
				.destroy = lib_access_list_entry_ipv4_exact_match_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/ipv6-host",
			.cbs = {
				.modify = lib_access_list_entry_ipv6_host_modify,
				.destroy = lib_access_list_entry_ipv6_host_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/ipv6-network",
			.cbs = {
				.create = lib_access_list_entry_ipv6_network_create,
				.destroy = lib_access_list_entry_ipv6_network_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/ipv6-network/address",
			.cbs = {
				.modify = lib_access_list_entry_ipv6_network_address_modify,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/ipv6-network/mask",
			.cbs = {
				.modify = lib_access_list_entry_ipv6_network_mask_modify,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/ipv6-source-any",
			.cbs = {
				.create = lib_access_list_entry_ipv6_source_any_create,
				.destroy = lib_access_list_entry_ipv6_source_any_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/ipv6-destination-host",
			.cbs = {
				.modify = lib_access_list_entry_ipv6_destination_host_modify,
				.destroy = lib_access_list_entry_ipv6_destination_host_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/ipv6-destination-network",
			.cbs = {
				.create = lib_access_list_entry_ipv6_destination_network_create,
				.destroy = lib_access_list_entry_ipv6_destination_network_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/ipv6-destination-network/address",
			.cbs = {
				.modify = lib_access_list_entry_ipv6_destination_network_address_modify,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/ipv6-destination-network/mask",
			.cbs = {
				.modify = lib_access_list_entry_ipv6_destination_network_mask_modify,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/ipv6-destination-any",
			.cbs = {
				.create = lib_access_list_entry_ipv6_destination_any_create,
				.destroy = lib_access_list_entry_ipv6_destination_any_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/mac",
			.cbs = {
				.modify = lib_access_list_entry_ipv4_prefix_modify,
				.destroy = lib_access_list_entry_ipv4_prefix_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry/any",
			.cbs = {
				.create = lib_access_list_entry_any_create,
				.destroy = lib_access_list_entry_any_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/prefix-list",
			.cbs = {
				.create = lib_prefix_list_create,
				.destroy = lib_prefix_list_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/prefix-list/remark",
			.cbs = {
				.modify = lib_prefix_list_remark_modify,
				.destroy = lib_prefix_list_remark_destroy,
				.cli_show = prefix_list_remark_show,
			}
		},
		{
			.xpath = "/frr-filter:lib/prefix-list/entry",
			.cbs = {
				.create = lib_prefix_list_entry_create,
				.destroy = lib_prefix_list_entry_destroy,
				.apply_finish = lib_prefix_list_entry_apply_finish,
				.cli_cmp = prefix_list_cmp,
				.cli_show = prefix_list_show,
			}
		},
		{
			.xpath = "/frr-filter:lib/prefix-list/entry/action",
			.cbs = {
				.modify = lib_prefix_list_entry_action_modify,
			}
		},
		{
			.xpath = "/frr-filter:lib/prefix-list/entry/ipv4-prefix",
			.cbs = {
				.modify = lib_prefix_list_entry_ipv4_prefix_modify,
				.destroy = lib_prefix_list_entry_ipv4_prefix_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/prefix-list/entry/ipv4-prefix-length-greater-or-equal",
			.cbs = {
				.modify = lib_prefix_list_entry_ipv4_prefix_length_greater_or_equal_modify,
				.destroy = lib_prefix_list_entry_ipv4_prefix_length_greater_or_equal_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/prefix-list/entry/ipv4-prefix-length-lesser-or-equal",
			.cbs = {
				.modify = lib_prefix_list_entry_ipv4_prefix_length_lesser_or_equal_modify,
				.destroy = lib_prefix_list_entry_ipv4_prefix_length_lesser_or_equal_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/prefix-list/entry/ipv6-prefix",
			.cbs = {
				.modify = lib_prefix_list_entry_ipv6_prefix_modify,
				.destroy = lib_prefix_list_entry_ipv6_prefix_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/prefix-list/entry/ipv6-prefix-length-greater-or-equal",
			.cbs = {
				.modify = lib_prefix_list_entry_ipv6_prefix_length_greater_or_equal_modify,
				.destroy = lib_prefix_list_entry_ipv6_prefix_length_greater_or_equal_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/prefix-list/entry/ipv6-prefix-length-lesser-or-equal",
			.cbs = {
				.modify = lib_prefix_list_entry_ipv6_prefix_length_lesser_or_equal_modify,
				.destroy = lib_prefix_list_entry_ipv6_prefix_length_lesser_or_equal_destroy,
			}
		},
		{
			.xpath = "/frr-filter:lib/prefix-list/entry/any",
			.cbs = {
				.create = lib_prefix_list_entry_any_create,
				.destroy = lib_prefix_list_entry_any_destroy,
			}
		},
		{
			.xpath = NULL,
		},
	}
};

/*
 * frr_filter_cli_info —— 本模块「仅 CLI 展示」的 YANG 回调注册表（模块名同为 "frr-filter"）。
 * 作用：与 frr_filter_info 的区别在于它只注册 cli_show / cli_cmp，
 *   用于配置回显与条目排序；ignore_cfg_cbs = true 表示这里不处理配置增删改，
 *   避免同一模块被注册两套配置回调。回调实现在 filter_cli.c。
 * 注意：remark 与 entry 两类节点分别注册 remark_show / show 与 cmp，
 *   供 `show running-config` 与 `show ... access-list / prefix-list` 复用。
 */
const struct frr_yang_module_info frr_filter_cli_info = {
	.name = "frr-filter",
	.ignore_cfg_cbs = true,
	.nodes = {
		{
			.xpath = "/frr-filter:lib/access-list/remark",
			.cbs.cli_show = access_list_remark_show,
		},
		{
			.xpath = "/frr-filter:lib/access-list/entry",
			.cbs = {
				.cli_cmp = access_list_cmp,
				.cli_show = access_list_show,
			}
		},
		{
			.xpath = "/frr-filter:lib/prefix-list/remark",
			.cbs.cli_show = prefix_list_remark_show,
		},
		{
			.xpath = "/frr-filter:lib/prefix-list/entry",
			.cbs = {
				.cli_cmp = prefix_list_cmp,
				.cli_show = prefix_list_show,
			}
		},
		{
			.xpath = NULL,
		},
	}
};
