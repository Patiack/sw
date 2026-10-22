// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * FRR filter CLI implementation.
 *
 * Copyright (C) 2019 Network Device Education Foundation, Inc. ("NetDEF")
 *                    Rafael Zalamena
 */

/*
 * 文件导读 filter_cli.c —— 访问列表 / 前缀列表的 CLI 与 YANG 命令实现
 *
 * 定位：本文件把用户敲的配置命令翻译成对 YANG 候选配置的修改（enqueue change），
 *   真正的数据落地与校验由 filter_nb.c 的北向回调完成；本文件不直接操作
 *   filter.c 里的 access_list 结构，也不做匹配运算。
 *
 * 三方分工回顾：
 *   filter.h      ：数据结构与接口原型；
 *   filter.c      ：访问列表核心（存 / 查 / 匹配）；
 *   filter_nb.c   ：YANG 回调，把配置树落到 filter.c 的 API；
 *   filter_cli.c  ：命令行（DEFPY_YANG）与 show / 比较回调（本文件）。
 *
 * 本文件主要内容：
 *   - acl_get_seq / acl_get_seq_cb ：为旧式命令自动生成序号（+5 递增）；
 *   - filter_remove_check_empty    ：删除条目后若表空且无 remark，连整表一起删；
 *   - access_list_* / no_access_list_* ：标准 / 扩展 ACL 的增删命令（IPv4 / IPv6 / MAC）；
 *   - prefix_list_*                ：前缀列表命令；
 *   - *_cmp / *_show / *_remark_show：YANG 框架要求的比较与显示回调。
 *
 * 关键约定：
 *   - 命令处理函数用 DEFPY_YANG（带 $ 变量），通过 nb_cli_enqueue_change 排队修改，
 *     最后 nb_cli_apply_changes 统一提交；中途出错返回 CMD_WARNING_CONFIG_FAILED。
 *   - 兼容性：重复条目在旧命令里被静默接受（不报错）。
 */

#include "zebra.h"
#include "northbound.h"
#include "prefix.h"

#include "lib/command.h"
#include "lib/filter.h"
#include "lib/northbound_cli.h"
#include "lib/plist.h"
#include "lib/plist_int.h"
#include "lib/printfrr.h"

#include "lib/filter_cli_clippy.c"

/*
 * 命令帮助字符串宏：把各命令共用的 help 文本集中定义，避免重复书写。
 *   ACCESS_LIST_STR / ACCESS_LIST_ZEBRA_STR : 命令关键字与列表名说明；
 *   ACCESS_LIST_SEQ_STR         : 序号参数说明（供 DEFPY 的 help 数组使用）；
 *   ACCESS_LIST_ACTION_STR      : deny / permit 说明；
 *   ACCESS_LIST_REMARK_STR / _LINE_STR : remark 及其长度说明；
 *   PREFIX_LIST_NAME_STR        : 前缀列表名说明。
 * 注意：这些字符串直接进入命令行的 help 输出，改动会影响 show / help。
 */
#define ACCESS_LIST_STR "Access list entry\n"
#define ACCESS_LIST_ZEBRA_STR "Access list name\n"
#define ACCESS_LIST_SEQ_STR                                                    \
	"Sequence number of an entry\n"                                        \
	"Sequence number\n"
#define ACCESS_LIST_ACTION_STR                                                 \
	"Specify packets to reject\n"                                          \
	"Specify packets to forward\n"
#define ACCESS_LIST_REMARK_STR "Access list entry comment\n"
#define ACCESS_LIST_REMARK_LINE_STR "Comment up to 100 characters\n"

#define PREFIX_LIST_NAME_STR "Prefix list entry name\n"

/*
 * acl_get_seq_cb —— yang_dnode_iterate 的回调：遍历条目时求最大序号。
 * 参数：dnode 当前条目节点；arg 指向调用方维护的最大值（int64_t *）。
 * 返回：YANG_ITER_CONTINUE，表示继续遍历剩余条目。
 */
/*
 * Helper function to generate a sequence number for legacy commands.
 */
static int acl_get_seq_cb(const struct lyd_node *dnode, void *arg)
{
	int64_t *seq = arg;
	int64_t cur_seq = yang_dnode_get_uint32(dnode, "sequence");

	if (cur_seq > *seq)
		*seq = cur_seq;

	return YANG_ITER_CONTINUE;
}

/*
 * acl_get_seq —— 为一条列表计算下一个未使用的序号。
 * 参数：xpath 列表节点的 XPath；is_remove 为真表示当前是删除操作。
 * 返回：max + 5；删除时即使超过 UINT32_MAX 也不报错，新增时超范围则报错返回 -1。
 * 逻辑：遍历 <xpath>/entry 求最大 sequence，再加 5。
 * 坑：返回值 -1 表示错误，调用方必须判断并返回 CMD_WARNING_CONFIG_FAILED。
 */
/**
 * Helper function that iterates over the XPath `xpath` on the candidate
 * configuration in `vty->candidate_config`.
 *
 * \param[in] vty shell context with the candidate configuration.
 * \param[in] xpath the XPath to look for the sequence leaf.
 * \returns next unused sequence number, -1 if out of range when adding.
 */
static int64_t acl_get_seq(struct vty *vty, const char *xpath, bool is_remove)
{
	int64_t seq = 0;

	yang_dnode_iterate(acl_get_seq_cb, &seq, vty->candidate_config->dnode,
			   "%s/entry", xpath);

	seq += 5;
	if (!is_remove && seq > UINT32_MAX) {
		vty_out(vty, "%% Malformed sequence value\n");
		return -1;
	}
	return seq;
}

/*
 * filter_remove_check_empty —— 删除一个条目 / remark 后，必要时删除整张空列表。
 * 参数：ftype / iptype / name 定位列表；del_seq 为要删的序号（0 表示不删条目）；
 *   del_remark 为真表示同时删除 remark。
 * 返回：nb_cli_apply_changes 的结果（命令执行状态）。
 * 逻辑：① 统计现存 entry 数；② 若待删条目存在，则排队删除并使计数减一；
 *   ③ 处理 remark（删除或探测是否存在）；④ 若计数为 0 且无 remark，
 *   则排队删除整个列表节点——这是为了兼容旧 CLI 与测试的“空表不留痕”行为。
 * 副作用：会向候选配置排队多个 DESTROY 变更并提交。
 */
/**
 * Remove main data structure filter list if there are no more entries or
 * remark. This fixes compatibility with old CLI and tests.
 */
static int filter_remove_check_empty(struct vty *vty, const char *ftype,
				     const char *iptype, const char *name,
				     uint32_t del_seq, bool del_remark)
{
	const struct lyd_node *remark_dnode = NULL;
	const struct lyd_node *entry_dnode = NULL;
	char xpath[XPATH_MAXLEN];
	uint32_t count;

	/* Count existing entries */
	count = yang_dnode_count(vty->candidate_config->dnode,
				 "/frr-filter:lib/%s-list[type='%s'][name='%s']/entry",
				 ftype, iptype, name);

	/* Check entry-to-delete actually exists */
	if (del_seq) {
		snprintf(xpath, sizeof(xpath),
			 "/frr-filter:lib/%s-list[type='%s'][name='%s']/entry[sequence='%u']",
			 ftype, iptype, name, del_seq);
		entry_dnode = yang_dnode_get(vty->candidate_config->dnode,
					     xpath);

		/* If exists, delete and don't count it, we need only remaining entries */
		if (entry_dnode) {
			nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
			count--;
		}
	}

	/* Delete the remark, or check whether it exists if we're keeping it */
	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/%s-list[type='%s'][name='%s']/remark", ftype,
		 iptype, name);
	if (del_remark)
		nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	else
		remark_dnode = yang_dnode_get(vty->candidate_config->dnode,
					      xpath);

	/* If there are no entries left and no remark, delete the whole list */
	if (count == 0 && !remark_dnode) {
		snprintf(xpath, sizeof(xpath),
			 "/frr-filter:lib/%s-list[type='%s'][name='%s']", ftype,
			 iptype, name);
		nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	}

	return nb_cli_apply_changes(vty, NULL);
}

/*
 * ===== 传统（Cisco 风格）访问列表命令 =====
 * 下面一组 DEFPY_YANG 对应 "access-list ..." 系列命令；
 * 它们按 标准 / 扩展、IPv4 / IPv6 / MAC 分别展开。
 */
/*
 * Cisco (legacy) access lists.
 */
/*
 * access_list_std —— 标准 ACL 配置命令 "access-list NAME [seq N] deny|permit ..."。
 * 语法：可跟 [host] 地址，或「地址 + 通配符」，或源任意（source-any）。
 * 逻辑：① 按参数填 acl_dup_args 并调用 acl_is_dup 查重，重复则直接成功返回；
 *   ② 未提供 seq 时用 acl_get_seq 自动生成；③ 依次 enqueue 建列表、建条目、
 *   设置 action 与地址字段；④ 最后 nb_cli_apply_changes 提交。
 * 返回：CMD_SUCCESS / CMD_WARNING_CONFIG_FAILED。
 * 坑：acl_get_seq 返回负值（超范围）时必须立即拦截返回失败。
 */
DEFPY_YANG(
	access_list_std, access_list_std_cmd,
	"access-list ACCESSLIST4_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action <[host] A.B.C.D$host|A.B.C.D$host A.B.C.D$mask>",
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"A single host address\n"
	"Address to match\n"
	"Address to match\n"
	"Wildcard bits\n")
{
	int64_t sseq;
	struct acl_dup_args ada = {};
	char xpath[XPATH_MAXLEN];
	char xpath_entry[XPATH_MAXLEN + 128];

	/*
	 * Backward compatibility: don't complain about duplicated values,
	 * just silently accept.
	 */
	ada.ada_type = "ipv4";
	ada.ada_name = name;
	ada.ada_action = action;
	if (host_str && mask_str == NULL) {
		ada.ada_xpath[0] = "./host";
		ada.ada_value[0] = host_str;
	} else if (host_str && mask_str) {
		ada.ada_xpath[0] = "./network/address";
		ada.ada_value[0] = host_str;
		ada.ada_xpath[1] = "./network/mask";
		ada.ada_value[1] = mask_str;
	} else {
		ada.ada_xpath[0] = "./source-any";
		ada.ada_value[0] = "";
	}

	if (acl_is_dup(vty->candidate_config->dnode, &ada))
		return CMD_SUCCESS;

	/*
	 * Create the access-list first, so we can generate sequence if
	 * none given (backward compatibility).
	 */
	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/access-list[type='ipv4'][name='%s']", name);
	if (seq_str == NULL) {
		/* Use XPath to find the next sequence number. */
		sseq = acl_get_seq(vty, xpath, false);
		if (sseq < 0)
			return CMD_WARNING_CONFIG_FAILED;

		snprintfrr(xpath_entry, sizeof(xpath_entry),
			   "%s/entry[sequence='%" PRId64 "']", xpath, sseq);
	} else
		snprintfrr(xpath_entry, sizeof(xpath_entry),
			   "%s/entry[sequence='%s']", xpath, seq_str);

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	nb_cli_enqueue_change(vty, xpath_entry, NB_OP_CREATE, NULL);

	nb_cli_enqueue_change(vty, "./action", NB_OP_MODIFY, action);
	if (host_str != NULL && mask_str == NULL) {
		nb_cli_enqueue_change(vty, "./host", NB_OP_MODIFY, host_str);
	} else if (host_str != NULL && mask_str != NULL) {
		nb_cli_enqueue_change(vty, "./network/address", NB_OP_MODIFY,
				      host_str);
		nb_cli_enqueue_change(vty, "./network/mask", NB_OP_MODIFY,
				      mask_str);
	} else {
		nb_cli_enqueue_change(vty, "./source-any", NB_OP_CREATE, NULL);
	}

	return nb_cli_apply_changes(vty, "%s", xpath_entry);
}

/*
 * no_access_list_std —— 删除标准 ACL 条目的命令 "no access-list ..."。
 * 逻辑：若用户显式给了 seq，直接按序号删；否则用 acl_dup_args + acl_is_dup
 *   反查该匹配条目对应的序号，查不到则报错返回。
 * 返回：CMD_SUCCESS / CMD_WARNING_CONFIG_FAILED；实际删除交给
 *   filter_remove_check_empty（可能顺带删掉空表）。
 */
DEFPY_YANG(
	no_access_list_std, no_access_list_std_cmd,
	"no access-list ACCESSLIST4_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action <[host] A.B.C.D$host|A.B.C.D$host A.B.C.D$mask>",
	NO_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"A single host address\n"
	"Address to match\n"
	"Address to match\n"
	"Wildcard bits\n")
{
	int64_t sseq;
	struct acl_dup_args ada = {};

	/* If the user provided sequence number, then just go for it. */
	if (seq_str != NULL)
		return filter_remove_check_empty(vty, "access", "ipv4", name,
						 seq, false);

	/* Otherwise, to keep compatibility, we need to figure it out. */
	ada.ada_type = "ipv4";
	ada.ada_name = name;
	ada.ada_action = action;
	if (host_str && mask_str == NULL) {
		ada.ada_xpath[0] = "./host";
		ada.ada_value[0] = host_str;
	} else if (host_str && mask_str) {
		ada.ada_xpath[0] = "./network/address";
		ada.ada_value[0] = host_str;
		ada.ada_xpath[1] = "./network/mask";
		ada.ada_value[1] = mask_str;
	} else {
		ada.ada_xpath[0] = "./source-any";
		ada.ada_value[0] = "";
	}

	if (acl_is_dup(vty->candidate_config->dnode, &ada))
		sseq = ada.ada_seq;
	else
		return CMD_WARNING_CONFIG_FAILED;

	return filter_remove_check_empty(vty, "access", "ipv4", name, sseq,
					 false);
}

/*
 * access_list_ext —— 扩展 ACL 配置命令 "access-list NAME ... permit ip SRC DST"。
 * 语法：源与目的各可写「地址 + 掩码 / host 地址 / any」。
 * 逻辑：与标准命令类似，但用 idx 依次把源、目的字段填入 acl_dup_args 做查重，
 *   随后 enqueue 建条目并逐项设置源 / 目的字段，最后统一提交。
 * 返回：CMD_SUCCESS / CMD_WARNING_CONFIG_FAILED。
 * 坑：源 / 目的涉及多个字段，拼 XPath 与维护 idx 时要格外小心。
 */
DEFPY_YANG(
	access_list_ext, access_list_ext_cmd,
	"access-list ACCESSLIST4_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action ip <A.B.C.D$src A.B.C.D$src_mask|host A.B.C.D$src|any> <A.B.C.D$dst A.B.C.D$dst_mask|host A.B.C.D$dst|any>",
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"IPv4 address\n"
	"Source address to match\n"
	"Source address mask to apply\n"
	"Single source host\n"
	"Source address to match\n"
	"Any source host\n"
	"Destination address to match\n"
	"Destination address mask to apply\n"
	"Single destination host\n"
	"Destination address to match\n"
	"Any destination host\n")
{
	int idx = 0;
	int64_t sseq;
	struct acl_dup_args ada = {};
	char xpath[XPATH_MAXLEN];
	char xpath_entry[XPATH_MAXLEN + 128];

	/*
	 * Backward compatibility: don't complain about duplicated values,
	 * just silently accept.
	 */
	ada.ada_type = "ipv4";
	ada.ada_name = name;
	ada.ada_action = action;
	if (src_str && src_mask_str == NULL) {
		ada.ada_xpath[idx] = "./host";
		ada.ada_value[idx] = src_str;
		idx++;
	} else if (src_str && src_mask_str) {
		ada.ada_xpath[idx] = "./network/address";
		ada.ada_value[idx] = src_str;
		idx++;
		ada.ada_xpath[idx] = "./network/mask";
		ada.ada_value[idx] = src_mask_str;
		idx++;
	} else {
		ada.ada_xpath[idx] = "./source-any";
		ada.ada_value[idx] = "";
		idx++;
	}

	if (dst_str && dst_mask_str == NULL) {
		ada.ada_xpath[idx] = "./destination-host";
		ada.ada_value[idx] = dst_str;
		idx++;
	} else if (dst_str && dst_mask_str) {
		ada.ada_xpath[idx] = "./destination-network/address";
		ada.ada_value[idx] = dst_str;
		idx++;
		ada.ada_xpath[idx] = "./destination-network/mask";
		ada.ada_value[idx] = dst_mask_str;
		idx++;
	} else {
		ada.ada_xpath[idx] = "./destination-any";
		ada.ada_value[idx] = "";
		idx++;
	}

	if (acl_is_dup(vty->candidate_config->dnode, &ada))
		return CMD_SUCCESS;

	/*
	 * Create the access-list first, so we can generate sequence if
	 * none given (backward compatibility).
	 */
	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/access-list[type='ipv4'][name='%s']", name);
	if (seq_str == NULL) {
		/* Use XPath to find the next sequence number. */
		sseq = acl_get_seq(vty, xpath, false);
		if (sseq < 0)
			return CMD_WARNING_CONFIG_FAILED;

		snprintfrr(xpath_entry, sizeof(xpath_entry),
			   "%s/entry[sequence='%" PRId64 "']", xpath, sseq);
	} else
		snprintfrr(xpath_entry, sizeof(xpath_entry),
			   "%s/entry[sequence='%s']", xpath, seq_str);

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	nb_cli_enqueue_change(vty, xpath_entry, NB_OP_CREATE, NULL);

	nb_cli_enqueue_change(vty, "./action", NB_OP_MODIFY, action);
	if (src_str != NULL && src_mask_str == NULL) {
		nb_cli_enqueue_change(vty, "./host", NB_OP_MODIFY, src_str);
	} else if (src_str != NULL && src_mask_str != NULL) {
		nb_cli_enqueue_change(vty, "./network/address", NB_OP_MODIFY,
				      src_str);
		nb_cli_enqueue_change(vty, "./network/mask", NB_OP_MODIFY,
				      src_mask_str);
	} else {
		nb_cli_enqueue_change(vty, "./source-any", NB_OP_CREATE, NULL);
	}

	if (dst_str != NULL && dst_mask_str == NULL) {
		nb_cli_enqueue_change(vty, "./destination-host", NB_OP_MODIFY,
				      dst_str);
	} else if (dst_str != NULL && dst_mask_str != NULL) {
		nb_cli_enqueue_change(vty, "./destination-network/address",
				      NB_OP_MODIFY, dst_str);
		nb_cli_enqueue_change(vty, "./destination-network/mask",
				      NB_OP_MODIFY, dst_mask_str);
	} else {
		nb_cli_enqueue_change(vty, "./destination-any", NB_OP_CREATE,
				      NULL);
	}

	return nb_cli_apply_changes(vty, "%s", xpath_entry);
}

/*
 * no_access_list_ext —— 删除扩展 ACL 条目的命令 "no access-list ... ip ..."。
 * 逻辑：给了 seq 就直接按序号删；否则用源 + 目的的字段组合查重反查序号，
 *   查不到则报错返回。
 * 返回：CMD_SUCCESS / CMD_WARNING_CONFIG_FAILED。
 */
DEFPY_YANG(
	no_access_list_ext, no_access_list_ext_cmd,
	"no access-list ACCESSLIST4_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action ip <A.B.C.D$src A.B.C.D$src_mask|host A.B.C.D$src|any> <A.B.C.D$dst A.B.C.D$dst_mask|host A.B.C.D$dst|any>",
	NO_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"Any Internet Protocol\n"
	"Source address to match\n"
	"Source address mask to apply\n"
	"Single source host\n"
	"Source address to match\n"
	"Any source host\n"
	"Destination address to match\n"
	"Destination address mask to apply\n"
	"Single destination host\n"
	"Destination address to match\n"
	"Any destination host\n")
{
	int idx = 0;
	int64_t sseq;
	struct acl_dup_args ada = {};

	/* If the user provided sequence number, then just go for it. */
	if (seq_str != NULL)
		return filter_remove_check_empty(vty, "access", "ipv4", name,
						 seq, false);

	/* Otherwise, to keep compatibility, we need to figure it out. */
	ada.ada_type = "ipv4";
	ada.ada_name = name;
	ada.ada_action = action;
	if (src_str && src_mask_str == NULL) {
		ada.ada_xpath[idx] = "./host";
		ada.ada_value[idx] = src_str;
		idx++;
	} else if (src_str && src_mask_str) {
		ada.ada_xpath[idx] = "./network/address";
		ada.ada_value[idx] = src_str;
		idx++;
		ada.ada_xpath[idx] = "./network/mask";
		ada.ada_value[idx] = src_mask_str;
		idx++;
	} else {
		ada.ada_xpath[idx] = "./source-any";
		ada.ada_value[idx] = "";
		idx++;
	}

	if (dst_str && dst_mask_str == NULL) {
		ada.ada_xpath[idx] = "./destination-host";
		ada.ada_value[idx] = dst_str;
		idx++;
	} else if (dst_str && dst_mask_str) {
		ada.ada_xpath[idx] = "./destination-network/address";
		ada.ada_value[idx] = dst_str;
		idx++;
		ada.ada_xpath[idx] = "./destination-network/mask";
		ada.ada_value[idx] = dst_mask_str;
		idx++;
	} else {
		ada.ada_xpath[idx] = "./destination-any";
		ada.ada_value[idx] = "";
		idx++;
	}

	if (acl_is_dup(vty->candidate_config->dnode, &ada))
		sseq = ada.ada_seq;
	else
		return CMD_WARNING_CONFIG_FAILED;

	return filter_remove_check_empty(vty, "access", "ipv4", name, sseq,
					 false);
}

/*
 * access_list_v6_std —— IPv6 标准 ACL 命令 "ipv6 access-list NAME ... "。
 * 说明：IPv6 标准 ACL 只关心目的（dst），可写 host / 地址 + 掩码 / 源任意。
 * 逻辑：与 IPv4 标准版本一致——先查重，未给 seq 时用 acl_get_seq 生成。
 * 坑：本函数未检查 acl_get_seq 的负返回值（历史实现），与 IPv4 版本不同。
 */
DEFPY_YANG(
	access_list_v6_std, access_list_v6_std_cmd,
	"ipv6 access-list ACCESSLIST6_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action <X:X::X:X$dst X:X::X:X$dst_mask|[host] X:X::X:X$dst>",
	IPV6_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"Destination address to match\n"
	"Destination address mask to apply\n"
	"Single destination host\n"
	"Destination address to match\n")
{
	int idx = 0;
	int64_t sseq;
	struct acl_dup_args ada = {};
	char xpath[XPATH_MAXLEN];
	char xpath_entry[XPATH_MAXLEN + 128];

	/*
	 * Backward compatibility: don't complain about duplicated values,
	 * just silently accept.
	 */
	ada.ada_type = "ipv6";
	ada.ada_name = name;
	ada.ada_action = action;
	if (dst_str && dst_mask_str == NULL) {
		ada.ada_xpath[idx] = "./ipv6-host";
		ada.ada_value[idx] = dst_str;
		idx++;
	} else if (dst_str && dst_mask_str) {
		ada.ada_xpath[idx] = "./ipv6-network/address";
		ada.ada_value[idx] = dst_str;
		idx++;
		ada.ada_xpath[idx] = "./ipv6-network/mask";
		ada.ada_value[idx] = dst_mask_str;
		idx++;
	} else {
		ada.ada_xpath[idx] = "./ipv6-source-any";
		ada.ada_value[idx] = "";
		idx++;
	}

	if (acl_is_dup(vty->candidate_config->dnode, &ada))
		return CMD_SUCCESS;

	/*
	 * Create the access-list first, so we can generate sequence if
	 * none given (backward compatibility).
	 */
	snprintf(xpath, sizeof(xpath), "/frr-filter:lib/access-list[type='ipv6'][name='%s']", name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	if (seq_str == NULL) {
		/* Use XPath to find the next sequence number. */
		sseq = acl_get_seq(vty, xpath, false);
		snprintfrr(xpath_entry, sizeof(xpath_entry), "%s/entry[sequence='%" PRId64 "']",
			   xpath, sseq);
	} else
		snprintfrr(xpath_entry, sizeof(xpath_entry), "%s/entry[sequence='%s']", xpath,
			   seq_str);

	nb_cli_enqueue_change(vty, xpath_entry, NB_OP_CREATE, NULL);

	nb_cli_enqueue_change(vty, "./action", NB_OP_MODIFY, action);
	if (dst_str != NULL && dst_mask_str == NULL) {
		nb_cli_enqueue_change(vty, "./ipv6-host", NB_OP_MODIFY, dst_str);
	} else if (dst_str != NULL && dst_mask_str != NULL) {
		nb_cli_enqueue_change(vty, "./ipv6-network/address", NB_OP_MODIFY, dst_str);
		nb_cli_enqueue_change(vty, "./ipv6-network/mask", NB_OP_MODIFY, dst_mask_str);
	} else {
		nb_cli_enqueue_change(vty, "./ipv6-source-any", NB_OP_CREATE, NULL);
	}

	return nb_cli_apply_changes(vty, "%s", xpath_entry);
}

/*
 * no_access_list_v6_std —— 删除 IPv6 标准 ACL 条目 "no ipv6 access-list ..."。
 * 逻辑同 IPv4 版本：给了 seq 直接删，否则查重反查序号；查不到返回失败。
 */
DEFPY_YANG(
	no_access_list_v6_std, no_access_list_v6_std_cmd,
	"no ipv6 access-list ACCESSLIST6_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action <X:X::X:X$dst X:X::X:X$dst_mask|[host] X:X::X:X$dst>",
	NO_STR
	IPV6_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"Destination address to match\n"
	"Destination address mask to apply\n"
	"Single destination host\n"
	"Destination address to match\n")
{
	int idx = 0;
	int64_t sseq;
	struct acl_dup_args ada = {};

	/* If the user provided sequence number, then just go for it. */
	if (seq_str != NULL)
		return filter_remove_check_empty(vty, "access", "ipv6", name, seq, false);

	/* Otherwise, to keep compatibility, we need to figure it out. */
	ada.ada_type = "ipv6";
	ada.ada_name = name;
	ada.ada_action = action;
	if (dst_str && dst_mask_str == NULL) {
		ada.ada_xpath[idx] = "./ipv6-host";
		ada.ada_value[idx] = dst_str;
		idx++;
	} else if (dst_str && dst_mask_str) {
		ada.ada_xpath[idx] = "./ipv6-network/address";
		ada.ada_value[idx] = dst_str;
		idx++;
		ada.ada_xpath[idx] = "./ipv6-network/mask";
		ada.ada_value[idx] = dst_mask_str;
		idx++;
	} else {
		ada.ada_xpath[idx] = "./ipv6-source-any";
		ada.ada_value[idx] = "";
		idx++;
	}

	if (acl_is_dup(vty->candidate_config->dnode, &ada))
		sseq = ada.ada_seq;
	else
		return CMD_WARNING_CONFIG_FAILED;

	return filter_remove_check_empty(vty, "access", "ipv6", name, sseq, false);
}

/*
 * access_list_v6_ext —— IPv6 扩展 ACL 命令 "ipv6 access-list NAME ... ipv6 SRC DST"。
 * 语法：源、目的各可为「地址 + 掩码 / host / any」。
 * 逻辑：先查重，再逐项 enqueue 建条目并设置源 / 目的字段，最后统一提交。
 */
DEFPY_YANG(
	access_list_v6_ext, access_list_v6_ext_cmd,
	"ipv6 access-list ACCESSLIST6_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action ipv6 <X:X::X:X$src X:X::X:X$src_mask|host X:X::X:X$src|any> <X:X::X:X$dst X:X::X:X$dst_mask|host X:X::X:X$dst|any>",
	IPV6_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"IPv6 address\n"
	"Source address to match\n"
	"Source address mask to apply\n"
	"Single source host\n"
	"Source address to match\n"
	"Any source host\n"
	"Destination address to match\n"
	"Destination address mask to apply\n"
	"Single destination host\n"
	"Destination address to match\n"
	"Any destination host\n")
{
	int idx = 0;
	int64_t sseq;
	struct acl_dup_args ada = {};
	char xpath[XPATH_MAXLEN];
	char xpath_entry[XPATH_MAXLEN + 128];

	/*
	 * Backward compatibility: don't complain about duplicated values,
	 * just silently accept.
	 */
	ada.ada_type = "ipv6";
	ada.ada_name = name;
	ada.ada_action = action;
	if (src_str && src_mask_str == NULL) {
		ada.ada_xpath[idx] = "./ipv6-host";
		ada.ada_value[idx] = src_str;
		idx++;
	} else if (src_str && src_mask_str) {
		ada.ada_xpath[idx] = "./ipv6-network/address";
		ada.ada_value[idx] = src_str;
		idx++;
		ada.ada_xpath[idx] = "./ipv6-network/mask";
		ada.ada_value[idx] = src_mask_str;
		idx++;
	} else {
		ada.ada_xpath[idx] = "./ipv6-source-any";
		ada.ada_value[idx] = "";
		idx++;
	}

	if (dst_str && dst_mask_str == NULL) {
		ada.ada_xpath[idx] = "./ipv6-destination-host";
		ada.ada_value[idx] = dst_str;
		idx++;
	} else if (dst_str && dst_mask_str) {
		ada.ada_xpath[idx] = "./ipv6-destination-network/address";
		ada.ada_value[idx] = dst_str;
		idx++;
		ada.ada_xpath[idx] = "./ipv6-destination-network/mask";
		ada.ada_value[idx] = dst_mask_str;
		idx++;
	} else {
		ada.ada_xpath[idx] = "./ipv6-destination-any";
		ada.ada_value[idx] = "";
		idx++;
	}

	if (acl_is_dup(vty->candidate_config->dnode, &ada))
		return CMD_SUCCESS;

	/*
	 * Create the access-list first, so we can generate sequence if
	 * none given (backward compatibility).
	 */
	snprintf(xpath, sizeof(xpath), "/frr-filter:lib/access-list[type='ipv6'][name='%s']", name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	if (seq_str == NULL) {
		/* Use XPath to find the next sequence number. */
		sseq = acl_get_seq(vty, xpath, false);
		snprintfrr(xpath_entry, sizeof(xpath_entry), "%s/entry[sequence='%" PRId64 "']",
			   xpath, sseq);
	} else
		snprintfrr(xpath_entry, sizeof(xpath_entry), "%s/entry[sequence='%s']", xpath,
			   seq_str);

	nb_cli_enqueue_change(vty, xpath_entry, NB_OP_CREATE, NULL);

	nb_cli_enqueue_change(vty, "./action", NB_OP_MODIFY, action);
	if (src_str != NULL && src_mask_str == NULL) {
		nb_cli_enqueue_change(vty, "./ipv6-host", NB_OP_MODIFY, src_str);
	} else if (src_str != NULL && src_mask_str != NULL) {
		nb_cli_enqueue_change(vty, "./ipv6-network/address", NB_OP_MODIFY, src_str);
		nb_cli_enqueue_change(vty, "./ipv6-network/mask", NB_OP_MODIFY, src_mask_str);
	} else {
		nb_cli_enqueue_change(vty, "./ipv6-source-any", NB_OP_CREATE, NULL);
	}

	if (dst_str != NULL && dst_mask_str == NULL) {
		nb_cli_enqueue_change(vty, "./ipv6-destination-host", NB_OP_MODIFY, dst_str);
	} else if (dst_str != NULL && dst_mask_str != NULL) {
		nb_cli_enqueue_change(vty, "./ipv6-destination-network/address", NB_OP_MODIFY,
				      dst_str);
		nb_cli_enqueue_change(vty, "./ipv6-destination-network/mask", NB_OP_MODIFY,
				      dst_mask_str);
	} else {
		nb_cli_enqueue_change(vty, "./ipv6-destination-any", NB_OP_CREATE, NULL);
	}

	return nb_cli_apply_changes(vty, "%s", xpath_entry);
}

/*
 * no_access_list_v6_ext —— 删除 IPv6 扩展 ACL 条目
 * "no ipv6 access-list ... ipv6 SRC DST"。
 * 逻辑：给了 seq 直接删，否则用源 + 目的字段查重反查序号；查不到返回失败。
 */
DEFPY_YANG(
	no_access_list_v6_ext, no_access_list_v6_ext_cmd,
	"no ipv6 access-list ACCESSLIST6_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action ipv6 <X:X::X:X$src X:X::X:X$src_mask|host X:X::X:X$src|any> <X:X::X:X$dst X:X::X:X$dst_mask|host X:X::X:X$dst|any>",
	NO_STR
	IPV6_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"IPv6 address\n"
	"Source address to match\n"
	"Source address mask to apply\n"
	"Single source host\n"
	"Source address to match\n"
	"Any source host\n"
	"Destination address to match\n"
	"Destination address mask to apply\n"
	"Single destination host\n"
	"Destination address to match\n"
	"Any destination host\n")
{
	int idx = 0;
	int64_t sseq;
	struct acl_dup_args ada = {};

	/* If the user provided sequence number, then just go for it. */
	if (seq_str != NULL)
		return filter_remove_check_empty(vty, "access", "ipv6", name, seq, false);

	/* Otherwise, to keep compatibility, we need to figure it out. */
	ada.ada_type = "ipv6";
	ada.ada_name = name;
	ada.ada_action = action;
	if (src_str && src_mask_str == NULL) {
		ada.ada_xpath[idx] = "./ipv6-host";
		ada.ada_value[idx] = src_str;
		idx++;
	} else if (src_str && src_mask_str) {
		ada.ada_xpath[idx] = "./ipv6-network/address";
		ada.ada_value[idx] = src_str;
		idx++;
		ada.ada_xpath[idx] = "./ipv6-network/mask";
		ada.ada_value[idx] = src_mask_str;
		idx++;
	} else {
		ada.ada_xpath[idx] = "./ipv6-source-any";
		ada.ada_value[idx] = "";
		idx++;
	}

	if (dst_str && dst_mask_str == NULL) {
		ada.ada_xpath[idx] = "./ipv6-destination-host";
		ada.ada_value[idx] = dst_str;
		idx++;
	} else if (dst_str && dst_mask_str) {
		ada.ada_xpath[idx] = "./ipv6-destination-network/address";
		ada.ada_value[idx] = dst_str;
		idx++;
		ada.ada_xpath[idx] = "./ipv6-destination-network/mask";
		ada.ada_value[idx] = dst_mask_str;
		idx++;
	} else {
		ada.ada_xpath[idx] = "./ipv6-destination-any";
		ada.ada_value[idx] = "";
		idx++;
	}

	if (acl_is_dup(vty->candidate_config->dnode, &ada))
		sseq = ada.ada_seq;
	else
		return CMD_WARNING_CONFIG_FAILED;

	return filter_remove_check_empty(vty, "access", "ipv6", name, sseq, false);
}

/*
 * ===== Zebra（前缀列表式）访问列表命令 =====
 * 下面命令用 "A.B.C.D/M [exact-match]" 表达匹配，落到 YANG 里即 zebra 风格条目。
 */
/*
 * Zebra access lists.
 */
/*
 * access_list —— zebra 风格 IPv4 访问列表命令
 * "access-list NAME ... deny|permit A.B.C.D/M [exact-match]|any"。
 * 逻辑：① 用 ipv4-prefix（含 exact 则加 ipv4-exact-match）或 any 填 acl_dup_args 查重；
 *   ② 未给 seq 时用 acl_get_seq 生成（此处含负值检查）；③ enqueue 建列表与条目，
 *   设置 action 及 ipv4-prefix / ipv4-exact-match（或 any）；④ 提交。
 * 返回：CMD_SUCCESS / CMD_WARNING_CONFIG_FAILED。
 * 注意：这类条目在 filter.c 走 zebra 分支匹配（前缀包含，exact 时要求等长）。
 */
DEFPY_YANG(
	access_list, access_list_cmd,
	"access-list ACCESSLIST4_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action <A.B.C.D/M$prefix [exact-match$exact]|any>",
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"Prefix to match. e.g. 10.0.0.0/8\n"
	"Exact match of the prefixes\n"
	"Match any IPv4\n")
{
	int64_t sseq;
	struct acl_dup_args ada = {};
	char xpath[XPATH_MAXLEN];
	char xpath_entry[XPATH_MAXLEN + 128];

	/*
	 * Backward compatibility: don't complain about duplicated values,
	 * just silently accept.
	 */
	ada.ada_type = "ipv4";
	ada.ada_name = name;
	ada.ada_action = action;

	if (prefix_str) {
		ada.ada_xpath[0] = "./ipv4-prefix";
		ada.ada_value[0] = prefix_str;
		if (exact) {
			ada.ada_xpath[1] = "./ipv4-exact-match";
			ada.ada_value[1] = "true";
		}
	} else {
		ada.ada_xpath[0] = "./any";
		ada.ada_value[0] = "";
	}

	if (acl_is_dup(vty->candidate_config->dnode, &ada))
		return CMD_SUCCESS;

	/*
	 * Create the access-list first, so we can generate sequence if
	 * none given (backward compatibility).
	 */
	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/access-list[type='ipv4'][name='%s']", name);
	if (seq_str == NULL) {
		/* Use XPath to find the next sequence number. */
		sseq = acl_get_seq(vty, xpath, false);
		if (sseq < 0)
			return CMD_WARNING_CONFIG_FAILED;

		snprintfrr(xpath_entry, sizeof(xpath_entry),
			   "%s/entry[sequence='%" PRId64 "']", xpath, sseq);
	} else
		snprintfrr(xpath_entry, sizeof(xpath_entry),
			   "%s/entry[sequence='%s']", xpath, seq_str);

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	nb_cli_enqueue_change(vty, xpath_entry, NB_OP_CREATE, NULL);

	nb_cli_enqueue_change(vty, "./action", NB_OP_MODIFY, action);
	if (prefix_str != NULL) {
		nb_cli_enqueue_change(vty, "./ipv4-prefix", NB_OP_MODIFY,
				      prefix_str);
		nb_cli_enqueue_change(vty, "./ipv4-exact-match", NB_OP_MODIFY,
				      exact ? "true" : "false");
	} else {
		nb_cli_enqueue_change(vty, "./any", NB_OP_CREATE, NULL);
	}

	return nb_cli_apply_changes(vty, "%s", xpath_entry);
}

/*
 * no_access_list —— 删除 zebra 风格 IPv4 访问列表条目
 * "no access-list NAME ... deny|permit A.B.C.D/M|any"。
 * 逻辑同其它 no 命令：给了 seq 直接删，否则查重反查序号，
 *   最后交给 filter_remove_check_empty（空表则一并删除）。
 */
DEFPY_YANG(
	no_access_list, no_access_list_cmd,
	"no access-list ACCESSLIST4_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action <A.B.C.D/M$prefix [exact-match$exact]|any>",
	NO_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"Prefix to match. e.g. 10.0.0.0/8\n"
	"Exact match of the prefixes\n"
	"Match any IPv4\n")
{
	int64_t sseq;
	struct acl_dup_args ada = {};

	/* If the user provided sequence number, then just go for it. */
	if (seq_str != NULL)
		return filter_remove_check_empty(vty, "access", "ipv4", name,
						 seq, false);

	/* Otherwise, to keep compatibility, we need to figure it out. */
	ada.ada_type = "ipv4";
	ada.ada_name = name;
	ada.ada_action = action;

	if (prefix_str) {
		ada.ada_xpath[0] = "./ipv4-prefix";
		ada.ada_value[0] = prefix_str;
		if (exact) {
			ada.ada_xpath[1] = "./ipv4-exact-match";
			ada.ada_value[1] = "true";
		}
	} else {
		ada.ada_xpath[0] = "./any";
		ada.ada_value[0] = "";
	}

	if (acl_is_dup(vty->candidate_config->dnode, &ada))
		sseq = ada.ada_seq;
	else
		return CMD_WARNING_CONFIG_FAILED;

	return filter_remove_check_empty(vty, "access", "ipv4", name, sseq,
					 false);
}

/*
 * no_access_list_all —— "no access-list NAME"：删除整张列表。
 * 逻辑：直接对 access-list 节点排队 NB_OP_DESTROY 并提交，无需逐条删除。
 */
DEFPY_YANG(
	no_access_list_all, no_access_list_all_cmd,
	"no access-list ACCESSLIST4_NAME$name",
	NO_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR)
{
	char xpath[XPATH_MAXLEN];

	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/access-list[type='ipv4'][name='%s']", name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/*
 * access_list_remark —— "access-list NAME remark LINE..."：设置列表备注。
 * 逻辑：先 CREATE 列表节点（保证其存在），把 LINE... 拼成一个 remark 字符串，
 *   enqueue 到 ./remark，提交后立即 XFREE 该临时字符串。
 * 返回：nb_cli_apply_changes 的结果。
 * 坑：remark 用 MTYPE_TMP 分配，必须在本函数内释放，不能遗漏。
 */
DEFPY_YANG(
	access_list_remark, access_list_remark_cmd,
	"access-list ACCESSLIST4_NAME$name remark LINE...",
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_REMARK_STR
	ACCESS_LIST_REMARK_LINE_STR)
{
	int rv;
	char *remark;
	char xpath[XPATH_MAXLEN];

	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/access-list[type='ipv4'][name='%s']", name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	remark = argv_concat(argv, argc, 3);
	nb_cli_enqueue_change(vty, "./remark", NB_OP_CREATE, remark);
	rv = nb_cli_apply_changes(vty, "%s", xpath);
	XFREE(MTYPE_TMP, remark);

	return rv;
}

/*
 * no_access_list_remark —— "no access-list NAME remark"：删除备注。
 * 逻辑：调用 filter_remove_check_empty，del_seq=0 表示不删条目、
 *   del_remark=true 表示删 remark（若删后表空则整表一并删除）。
 */
DEFPY_YANG(
	no_access_list_remark, no_access_list_remark_cmd,
	"no access-list ACCESSLIST4_NAME$name remark",
	NO_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_REMARK_STR)
{
	return filter_remove_check_empty(vty, "access", "ipv4", name, 0, true);
}

/*
 * ALIAS no_access_list_remark_line —— 为 no_access_list_remark 增加一个带
 *   "remark LINE..." 文本的别名写法，两种输入效果相同（都删除备注）。
 */
ALIAS(
	no_access_list_remark, no_access_list_remark_line_cmd,
	"no access-list ACCESSLIST4_NAME$name remark LINE...",
	NO_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_REMARK_STR
	ACCESS_LIST_REMARK_LINE_STR)

/*
 * ipv6_access_list —— zebra 风格 IPv6 访问列表命令
 * "ipv6 access-list NAME ... deny|permit X:X::X:X/M [exact-match]|any"。
 * 逻辑：与 IPv4 zebra 版本一致，字段换成 ipv6-prefix / ipv6-exact-match。
 * 返回：CMD_SUCCESS / CMD_WARNING_CONFIG_FAILED。
 */
DEFPY_YANG(
	ipv6_access_list, ipv6_access_list_cmd,
	"ipv6 access-list ACCESSLIST6_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action <X:X::X:X/M$prefix [exact-match$exact]|any>",
	IPV6_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"IPv6 prefix\n"
	"Exact match of the prefixes\n"
	"Match any IPv6\n")
{
	int64_t sseq;
	struct acl_dup_args ada = {};
	char xpath[XPATH_MAXLEN];
	char xpath_entry[XPATH_MAXLEN + 128];

	/*
	 * Backward compatibility: don't complain about duplicated values,
	 * just silently accept.
	 */
	ada.ada_type = "ipv6";
	ada.ada_name = name;
	ada.ada_action = action;

	if (prefix_str) {
		ada.ada_xpath[0] = "./ipv6-prefix";
		ada.ada_value[0] = prefix_str;
		if (exact) {
			ada.ada_xpath[1] = "./ipv6-exact-match";
			ada.ada_value[1] = "true";
		}
	} else {
		ada.ada_xpath[0] = "./any";
		ada.ada_value[0] = "";
	}

	if (acl_is_dup(vty->candidate_config->dnode, &ada))
		return CMD_SUCCESS;

	/*
	 * Create the access-list first, so we can generate sequence if
	 * none given (backward compatibility).
	 */
	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/access-list[type='ipv6'][name='%s']", name);
	if (seq_str == NULL) {
		/* Use XPath to find the next sequence number. */
		sseq = acl_get_seq(vty, xpath, false);
		if (sseq < 0)
			return CMD_WARNING_CONFIG_FAILED;

		snprintfrr(xpath_entry, sizeof(xpath_entry),
			   "%s/entry[sequence='%" PRId64 "']", xpath, sseq);
	} else
		snprintfrr(xpath_entry, sizeof(xpath_entry),
			   "%s/entry[sequence='%s']", xpath, seq_str);

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	nb_cli_enqueue_change(vty, xpath_entry, NB_OP_CREATE, NULL);

	nb_cli_enqueue_change(vty, "./action", NB_OP_MODIFY, action);
	if (prefix_str != NULL) {
		nb_cli_enqueue_change(vty, "./ipv6-prefix", NB_OP_MODIFY,
				      prefix_str);
		nb_cli_enqueue_change(vty, "./ipv6-exact-match", NB_OP_MODIFY,
				      exact ? "true" : "false");
	} else {
		nb_cli_enqueue_change(vty, "./any", NB_OP_CREATE, NULL);
	}

	return nb_cli_apply_changes(vty, "%s", xpath_entry);
}

/*
 * no_ipv6_access_list —— 删除 zebra 风格 IPv6 列表条目
 * "no ipv6 access-list NAME ... deny|permit X:X::X:X/M|any"。
 * 逻辑：给了 seq 直接删，否则查重反查序号，再交给 filter_remove_check_empty。
 */
DEFPY_YANG(
	no_ipv6_access_list, no_ipv6_access_list_cmd,
	"no ipv6 access-list ACCESSLIST6_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action <X:X::X:X/M$prefix [exact-match$exact]|any>",
	NO_STR
	IPV6_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"IPv6 prefix\n"
	"Exact match of the prefixes\n"
	"Match any IPv6\n")
{
	int64_t sseq;
	struct acl_dup_args ada = {};

	/* If the user provided sequence number, then just go for it. */
	if (seq_str != NULL)
		return filter_remove_check_empty(vty, "access", "ipv6", name,
						 seq, false);

	/* Otherwise, to keep compatibility, we need to figure it out. */
	ada.ada_type = "ipv6";
	ada.ada_name = name;
	ada.ada_action = action;

	if (prefix_str) {
		ada.ada_xpath[0] = "./ipv6-prefix";
		ada.ada_value[0] = prefix_str;
		if (exact) {
			ada.ada_xpath[1] = "./ipv6-exact-match";
			ada.ada_value[1] = "true";
		}
	} else {
		ada.ada_xpath[0] = "./any";
		ada.ada_value[0] = "";
	}

	if (acl_is_dup(vty->candidate_config->dnode, &ada))
		sseq = ada.ada_seq;
	else
		return CMD_WARNING_CONFIG_FAILED;

	return filter_remove_check_empty(vty, "access", "ipv6", name, sseq,
					 false);
}

/*
 * no_ipv6_access_list_all —— "no ipv6 access-list NAME"：删除整张 IPv6 列表。
 * 逻辑：直接对列表节点排队 NB_OP_DESTROY 并提交。
 */
DEFPY_YANG(
	no_ipv6_access_list_all, no_ipv6_access_list_all_cmd,
	"no ipv6 access-list ACCESSLIST6_NAME$name",
	NO_STR
	IPV6_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR)
{
	char xpath[XPATH_MAXLEN];

	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/access-list[type='ipv6'][name='%s']", name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/*
 * ipv6_access_list_remark —— "ipv6 access-list NAME remark LINE..."：设置备注。
 * 逻辑同 IPv4 版本，只是 XPath 类型为 ipv6、LINE 的起始下标为 4。
 * 坑：remark 用 MTYPE_TMP 分配，需在本函数内释放。
 */
DEFPY_YANG(
	ipv6_access_list_remark, ipv6_access_list_remark_cmd,
	"ipv6 access-list ACCESSLIST6_NAME$name remark LINE...",
	IPV6_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_REMARK_STR
	ACCESS_LIST_REMARK_LINE_STR)
{
	int rv;
	char *remark;
	char xpath[XPATH_MAXLEN];

	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/access-list[type='ipv6'][name='%s']", name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	remark = argv_concat(argv, argc, 4);
	nb_cli_enqueue_change(vty, "./remark", NB_OP_CREATE, remark);
	rv = nb_cli_apply_changes(vty, "%s", xpath);
	XFREE(MTYPE_TMP, remark);

	return rv;
}

/*
 * no_ipv6_access_list_remark —— "no ipv6 access-list NAME remark"：删除备注。
 * 逻辑同 IPv4：调用 filter_remove_check_empty 且 del_remark=true。
 */
DEFPY_YANG(
	no_ipv6_access_list_remark, no_ipv6_access_list_remark_cmd,
	"no ipv6 access-list ACCESSLIST6_NAME$name remark",
	NO_STR
	IPV6_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_REMARK_STR)
{
	return filter_remove_check_empty(vty, "access", "ipv6", name, 0, true);
}

/*
 * ALIAS no_ipv6_access_list_remark_line —— 为 IPv6 删除备注命令增加带 LINE... 的别名。
 */
ALIAS(
	no_ipv6_access_list_remark, no_ipv6_access_list_remark_line_cmd,
	"no ipv6 access-list ACCESSLIST6_NAME$name remark LINE...",
	NO_STR
	IPV6_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_REMARK_STR
	ACCESS_LIST_REMARK_LINE_STR)

/*
 * mac_access_list —— MAC 访问列表命令
 * "mac access-list NAME ... deny|permit X:X:X:X:X:X|any"。
 * 逻辑：以 ./mac 或 ./any 字段填充 acl_dup_args 查重；未给 seq 时用 acl_get_seq
 *   生成（含负值检查）；然后 enqueue 建列表与条目，设置 action 与 mac / any 并提交。
 * 返回：CMD_SUCCESS / CMD_WARNING_CONFIG_FAILED。
 */
DEFPY_YANG(
	mac_access_list, mac_access_list_cmd,
	"mac access-list ACCESSLIST_MAC_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action <X:X:X:X:X:X$mac|any>",
	MAC_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"MAC address\n"
	"Match any MAC address\n")
{
	int64_t sseq;
	struct acl_dup_args ada = {};
	char xpath[XPATH_MAXLEN];
	char xpath_entry[XPATH_MAXLEN + 128];

	/*
	 * Backward compatibility: don't complain about duplicated values,
	 * just silently accept.
	 */
	ada.ada_type = "mac";
	ada.ada_name = name;
	ada.ada_action = action;

	if (mac_str) {
		ada.ada_xpath[0] = "./mac";
		ada.ada_value[0] = mac_str;
	} else {
		ada.ada_xpath[0] = "./any";
		ada.ada_value[0] = "";
	}

	if (acl_is_dup(vty->candidate_config->dnode, &ada))
		return CMD_SUCCESS;

	/*
	 * Create the access-list first, so we can generate sequence if
	 * none given (backward compatibility).
	 */
	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/access-list[type='mac'][name='%s']", name);
	if (seq_str == NULL) {
		/* Use XPath to find the next sequence number. */
		sseq = acl_get_seq(vty, xpath, false);
		if (sseq < 0)
			return CMD_WARNING_CONFIG_FAILED;

		snprintfrr(xpath_entry, sizeof(xpath_entry),
			   "%s/entry[sequence='%" PRId64 "']", xpath, sseq);
	} else
		snprintfrr(xpath_entry, sizeof(xpath_entry),
			   "%s/entry[sequence='%s']", xpath, seq_str);

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	nb_cli_enqueue_change(vty, xpath_entry, NB_OP_CREATE, NULL);

	nb_cli_enqueue_change(vty, "./action", NB_OP_MODIFY, action);
	if (mac_str != NULL) {
		nb_cli_enqueue_change(vty, "./mac", NB_OP_MODIFY, mac_str);
	} else {
		nb_cli_enqueue_change(vty, "./any", NB_OP_CREATE, NULL);
	}

	return nb_cli_apply_changes(vty, "%s", xpath_entry);
}

/*
 * no_mac_access_list —— 删除 MAC 访问列表条目
 * "no mac access-list NAME ... deny|permit MAC|any"。
 * 逻辑同其它 no 命令：给了 seq 直接删，否则查重反查序号。
 */
DEFPY_YANG(
	no_mac_access_list, no_mac_access_list_cmd,
	"no mac access-list ACCESSLIST_MAC_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action <X:X:X:X:X:X$mac|any>",
	NO_STR
	MAC_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"MAC address\n"
	"Match any MAC address\n")
{
	int64_t sseq;
	struct acl_dup_args ada = {};

	/* If the user provided sequence number, then just go for it. */
	if (seq_str != NULL)
		return filter_remove_check_empty(vty, "access", "mac", name,
						 seq, false);

	/* Otherwise, to keep compatibility, we need to figure it out. */
	ada.ada_type = "mac";
	ada.ada_name = name;
	ada.ada_action = action;

	if (mac_str) {
		ada.ada_xpath[0] = "./mac";
		ada.ada_value[0] = mac_str;
	} else {
		ada.ada_xpath[0] = "./any";
		ada.ada_value[0] = "";
	}

	if (acl_is_dup(vty->candidate_config->dnode, &ada))
		sseq = ada.ada_seq;
	else
		return CMD_WARNING_CONFIG_FAILED;

	return filter_remove_check_empty(vty, "access", "mac", name, sseq,
					 false);
}

/*
 * no_mac_access_list_all —— "no mac access-list NAME"：删除整张 MAC 列表。
 */
DEFPY_YANG(
	no_mac_access_list_all, no_mac_access_list_all_cmd,
	"no mac access-list ACCESSLIST_MAC_NAME$name",
	NO_STR
	MAC_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR)
{
	char xpath[XPATH_MAXLEN];

	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/access-list[type='mac'][name='%s']", name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/*
 * mac_access_list_remark —— "mac access-list NAME remark LINE..."：设置 MAC 列表备注。
 * 逻辑同 IPv4 / IPv6 版本，只是类型为 mac。remark 需在本函数内释放。
 */
DEFPY_YANG(
	mac_access_list_remark, mac_access_list_remark_cmd,
	"mac access-list ACCESSLIST_MAC_NAME$name remark LINE...",
	MAC_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_REMARK_STR
	ACCESS_LIST_REMARK_LINE_STR)
{
	int rv;
	char *remark;
	char xpath[XPATH_MAXLEN];

	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/access-list[type='mac'][name='%s']", name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	remark = argv_concat(argv, argc, 4);
	nb_cli_enqueue_change(vty, "./remark", NB_OP_CREATE, remark);
	rv = nb_cli_apply_changes(vty, "%s", xpath);
	XFREE(MTYPE_TMP, remark);

	return rv;
}

/*
 * no_mac_access_list_remark —— "no mac access-list NAME remark"：删除 MAC 列表备注。
 * 逻辑同 IPv4 / IPv6：filter_remove_check_empty 且 del_remark=true。
 */
DEFPY_YANG(
	no_mac_access_list_remark, no_mac_access_list_remark_cmd,
	"no mac access-list ACCESSLIST_MAC_NAME$name remark",
	NO_STR
	MAC_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_REMARK_STR)
{
	return filter_remove_check_empty(vty, "access", "mac", name, 0, true);
}

/*
 * ALIAS no_mac_access_list_remark_line —— 为 MAC 删除备注命令增加带 LINE... 的别名。
 */
ALIAS(
	no_mac_access_list_remark, no_mac_access_list_remark_line_cmd,
	"no mac access-list ACCESSLIST_MAC_NAME$name remark LINE...",
	NO_STR
	MAC_STR
	ACCESS_LIST_STR
	ACCESS_LIST_ZEBRA_STR
	ACCESS_LIST_REMARK_STR
	ACCESS_LIST_REMARK_LINE_STR)

/*
 * access_list_cmp —— YANG 条目的排序比较回调（显示时用于按序排列）。
 * 参数：dnode1 / dnode2 两个条目节点。
 * 返回：seq1 - seq2 的差值（<0 表示前者应排在前）。
 * 说明：直接用无符号序号相减，相等得 0，足以表达排序关系。
 */
int access_list_cmp(const struct lyd_node *dnode1,
		    const struct lyd_node *dnode2)
{
	uint32_t seq1 = yang_dnode_get_uint32(dnode1, "sequence");
	uint32_t seq2 = yang_dnode_get_uint32(dnode2, "sequence");

	return seq1 - seq2;
}

/*
 * access_list_show —— 把一条访问列表条目渲染成配置文本（YANG 显示回调）。
 * 参数：dnode 当前 entry 节点；show_defaults 本函数未使用。
 * 逻辑：先读 type（IPv4 / IPv6 / MAC）决定地址表现形式，再打印
 *   "access-list NAME seq N action"；随后按风格分流——
 *   cisco 标准 / 扩展（区分 host / network / any，扩展再输出目的部分），
 *   或 zebra 风格（前缀 + 可选 exact-match，any 则输出 any）。
 * 注意：MAC 用 type==2 判断（历史写法），且不打印掩码。
 */
void access_list_show(struct vty *vty, const struct lyd_node *dnode,
		      bool show_defaults)
{
	int type = yang_dnode_get_enum(dnode, "../type");
	struct prefix p;
	bool is_any;
	bool is_exact = false;
	bool cisco_style = false;
	bool cisco_extended = false;
	struct in_addr addr, mask;
	struct in6_addr addr6, mask6;
	char macstr[PREFIX2STR_BUFFER];

	is_any = yang_dnode_exists(dnode, "any");
	switch (type) {
	case YALT_IPV4:
		if (is_any)
			break;

		if (yang_dnode_exists(dnode, "host")
		    || yang_dnode_exists(dnode, "network/address")
		    || yang_dnode_exists(dnode, "source-any")) {
			cisco_style = true;
			if (yang_dnode_exists(dnode, "destination-host")
			    || yang_dnode_exists(
				    dnode, "./destination-network/address")
			    || yang_dnode_exists(dnode, "destination-any"))
				cisco_extended = true;
		} else {
			yang_dnode_get_prefix(&p, dnode, "ipv4-prefix");
			is_exact = yang_dnode_get_bool(dnode,
						       "./ipv4-exact-match");
		}
		break;
	case YALT_IPV6: /* ipv6 */
		vty_out(vty, "ipv6 ");
		if (is_any)
			break;

		if (yang_dnode_exists(dnode, "./ipv6-host") ||
		    yang_dnode_exists(dnode, "./ipv6-network/address") ||
		    yang_dnode_exists(dnode, "./ipv6-source-any")) {
			cisco_style = true;
			if (yang_dnode_exists(dnode, "./ipv6-destination-host") ||
			    yang_dnode_exists(dnode, "./ipv6-destination-network/address") ||
			    yang_dnode_exists(dnode, "./ipv6-destination-any"))
				cisco_extended = true;
		} else {
			yang_dnode_get_prefix(&p, dnode, "./ipv6-prefix");
			is_exact = yang_dnode_get_bool(dnode, "./ipv6-exact-match");
		}
		break;
	case YALT_MAC: /* mac */
		vty_out(vty, "mac ");
		if (is_any)
			break;

		yang_dnode_get_prefix(&p, dnode, "mac");
		break;
	}

	vty_out(vty, "access-list %s seq %s %s",
		yang_dnode_get_string(dnode, "../name"),
		yang_dnode_get_string(dnode, "sequence"),
		yang_dnode_get_string(dnode, "action"));

	/* Handle Cisco style access lists. */
	if (cisco_style) {
		if (cisco_extended) {
			if (type == YALT_IPV4)
				vty_out(vty, " ip");
			else
				vty_out(vty, " ipv6");
		}

		if (yang_dnode_exists(dnode, "network")) {
			yang_dnode_get_ipv4(&addr, dnode, "network/address");
			yang_dnode_get_ipv4(&mask, dnode, "network/mask");
			vty_out(vty, " %pI4 %pI4", &addr, &mask);
		} else if (yang_dnode_exists(dnode, "host")) {
			if (cisco_extended)
				vty_out(vty, " host");

			vty_out(vty, " %s",
				yang_dnode_get_string(dnode, "host"));
		} else if (yang_dnode_exists(dnode, "source-any"))
			vty_out(vty, " any");

		if (yang_dnode_exists(dnode, "./ipv6-network")) {
			yang_dnode_get_ipv6(&addr6, dnode, "./ipv6-network/address");
			yang_dnode_get_ipv6(&mask6, dnode, "./ipv6-network/mask");
			vty_out(vty, " %pI6 %pI6", &addr6, &mask6);
		} else if (yang_dnode_exists(dnode, "./ipv6-host")) {
			if (cisco_extended)
				vty_out(vty, " host");

			vty_out(vty, " %s", yang_dnode_get_string(dnode, "./ipv6-host"));
		} else if (yang_dnode_exists(dnode, "./ipv6-source-any"))
			vty_out(vty, " any");

		/* Not extended, exit earlier. */
		if (!cisco_extended) {
			vty_out(vty, "\n");
			return;
		}

		/* Handle destination address. */
		if (yang_dnode_exists(dnode, "destination-network")) {
			yang_dnode_get_ipv4(&addr, dnode,
					    "./destination-network/address");
			yang_dnode_get_ipv4(&mask, dnode,
					    "./destination-network/mask");
			vty_out(vty, " %pI4 %pI4", &addr, &mask);
		} else if (yang_dnode_exists(dnode, "destination-host"))
			vty_out(vty, " host %s",
				yang_dnode_get_string(dnode,
						      "./destination-host"));
		else if (yang_dnode_exists(dnode, "destination-any"))
			vty_out(vty, " any");

		if (yang_dnode_exists(dnode, "./ipv6-destination-network")) {
			yang_dnode_get_ipv6(&addr6, dnode, "./ipv6-destination-network/address");
			yang_dnode_get_ipv6(&mask6, dnode, "./ipv6-destination-network/mask");
			vty_out(vty, " %pI6 %pI6", &addr6, &mask6);
		} else if (yang_dnode_exists(dnode, "./ipv6-destination-host"))
			vty_out(vty, " host %s",
				yang_dnode_get_string(dnode, "./ipv6-destination-host"));
		else if (yang_dnode_exists(dnode, "./ipv6-destination-any"))
			vty_out(vty, " any");

		vty_out(vty, "\n");
		return;
	}

	/* Zebra style access list. */
	if (!is_any) {
		/* If type is MAC don't show '/mask'. */
		if (type == 2 /* mac */) {
			prefix_mac2str(&p.u.prefix_eth, macstr, sizeof(macstr));
			vty_out(vty, " %s", macstr);
		} else
			vty_out(vty, " %pFX", &p);
	} else
		vty_out(vty, " any");

	if (is_exact)
		vty_out(vty, " exact-match");

	vty_out(vty, "\n");
}

/*
 * access_list_remark_show —— 渲染列表的 remark（YANG 显示回调）。
 * 逻辑：按 type 先输出可选前缀（ipv6 / mac），再输出
 *   "access-list NAME remark <文本>"。
 */
void access_list_remark_show(struct vty *vty, const struct lyd_node *dnode,
			     bool show_defaults)
{
	int type = yang_dnode_get_enum(dnode, "../type");

	switch (type) {
	case YALT_IPV4:
		break;
	case YALT_IPV6:
		vty_out(vty, "ipv6 ");
		break;
	case YALT_MAC:
		vty_out(vty, "mac ");
		break;
	}

	vty_out(vty, "access-list %s remark %s\n",
		yang_dnode_get_string(dnode, "../name"),
		yang_dnode_get_string(dnode, NULL));
}

/*
 * ===== 前缀列表（prefix-list）命令 =====
 * 下面一组命令对应 "ip / ipv6 prefix-list ..."，同样落到 frr-filter 的
 * prefix-list 子树。
 */
/*
 * Prefix lists.
 */

/*
 * plist_remove —— 通用前缀列表条目删除辅助。
 * 参数：iptype 地址族；name 列表名；seq 序号（0 表示按内容反查）；
 *   action 动作；prefix 前缀（NULL 表示 any）；ge / le 长度约束。
 * 返回：CMD_SUCCESS / CMD_WARNING_CONFIG_FAILED。
 * 逻辑：给了 seq 直接删；否则构造 plist_dup_args 并调用 plist_is_dup 反查序号，
 *   再交给 filter_remove_check_empty。
 */
static int plist_remove(struct vty *vty, const char *iptype, const char *name,
			uint32_t seq, const char *action,
			union prefixconstptr prefix, int ge, int le)
{
	int64_t sseq;
	struct plist_dup_args pda = {};

	/* If the user provided sequence number, then just go for it. */
	if (seq != 0)
		return filter_remove_check_empty(vty, "prefix", iptype, name,
						 seq, false);

	/* Otherwise, to keep compatibility, we need to figure it out. */
	pda.pda_type = iptype;
	pda.pda_name = name;
	pda.pda_action = action;
	if (prefix.p) {
		prefix_copy(&pda.prefix, prefix);
		apply_mask(&pda.prefix);
		pda.ge = ge;
		pda.le = le;
	} else {
		pda.any = true;
	}

	if (plist_is_dup(vty->candidate_config->dnode, &pda))
		sseq = pda.pda_seq;
	else
		return CMD_WARNING_CONFIG_FAILED;

	return filter_remove_check_empty(vty, "prefix", iptype, name, sseq,
					 false);
}

/*
 * ip_prefix_list —— IPv4 前缀列表配置命令
 * "ip prefix-list NAME [seq N] deny|permit any|A.B.C.D/M [ge X|le Y]"。
 * 逻辑：① 填 plist_dup_args；② 未给 seq 时先查重（重复则复用其序号），
 *   否则用 acl_get_seq 生成（含负值检查）；③ enqueue 建列表与条目，
 *   设置 action、ipv4-prefix 及 ge / le（未提供则 DESTROY 旧值）或 any；④ 提交。
 * 返回：CMD_SUCCESS / CMD_WARNING_CONFIG_FAILED。
 * 坑：修改前缀条目时会显式 DESTROY 未再给出的 ge / le，防止旧约束残留。
 */
DEFPY_YANG(
	ip_prefix_list, ip_prefix_list_cmd,
	"ip prefix-list PREFIXLIST4_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action <any|A.B.C.D/M$prefix [{ge (0-32)$ge|le (0-32)$le}]>",
	IP_STR
	PREFIX_LIST_STR
	PREFIX_LIST_NAME_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"Any prefix match.  Same as \"0.0.0.0/0 le 32\"\n"
	"IP prefix <network>/<length>, e.g., 35.0.0.0/8\n"
	"Minimum prefix length to be matched\n"
	"Minimum prefix length\n"
	"Maximum prefix length to be matched\n"
	"Maximum prefix length\n")
{
	int64_t sseq;
	struct plist_dup_args pda = {};
	char xpath[XPATH_MAXLEN];
	char xpath_entry[XPATH_MAXLEN + 128];

	/*
	 * Backward compatibility: don't complain about duplicated values,
	 * just silently accept.
	 */
	pda.pda_type = "ipv4";
	pda.pda_name = name;
	pda.pda_action = action;
	if (prefix_str) {
		prefix_copy(&pda.prefix, prefix);
		pda.ge = ge;
		pda.le = le;
	} else {
		pda.any = true;
	}

	/*
	 * Create the prefix-list first, so we can generate sequence if
	 * none given (backward compatibility).
	 */
	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/prefix-list[type='ipv4'][name='%s']", name);
	if (seq_str == NULL) {
		if (plist_is_dup(vty->candidate_config->dnode, &pda))
			sseq = pda.pda_seq;
		else {
			/* Use XPath to find the next sequence number. */
			sseq = acl_get_seq(vty, xpath, false);
			if (sseq < 0)
				return CMD_WARNING_CONFIG_FAILED;
		}

		snprintfrr(xpath_entry, sizeof(xpath_entry),
			   "%s/entry[sequence='%" PRId64 "']", xpath, sseq);
	} else
		snprintfrr(xpath_entry, sizeof(xpath_entry),
			   "%s/entry[sequence='%s']", xpath, seq_str);

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	nb_cli_enqueue_change(vty, xpath_entry, NB_OP_CREATE, NULL);

	nb_cli_enqueue_change(vty, "./action", NB_OP_MODIFY, action);
	if (prefix_str != NULL) {
		nb_cli_enqueue_change(vty, "./ipv4-prefix", NB_OP_MODIFY,
				      prefix_str);

		if (ge_str) {
			nb_cli_enqueue_change(
				vty, "./ipv4-prefix-length-greater-or-equal",
				NB_OP_MODIFY, ge_str);
		} else {
			/*
			 * Remove old ge if not being modified
			 */
			nb_cli_enqueue_change(
				vty, "./ipv4-prefix-length-greater-or-equal",
				NB_OP_DESTROY, NULL);
		}

		if (le_str) {
			nb_cli_enqueue_change(
				vty, "./ipv4-prefix-length-lesser-or-equal",
				NB_OP_MODIFY, le_str);
		} else {
			/*
			 * Remove old le if not being modified
			 */
			nb_cli_enqueue_change(
				vty, "./ipv4-prefix-length-lesser-or-equal",
				NB_OP_DESTROY, NULL);
		}
		nb_cli_enqueue_change(vty, "./any", NB_OP_DESTROY, NULL);
	} else {
		nb_cli_enqueue_change(vty, "./any", NB_OP_CREATE, NULL);
	}

	return nb_cli_apply_changes(vty, "%s", xpath_entry);
}

/*
 * no_ip_prefix_list —— 删除 IPv4 前缀列表条目（可按内容匹配）。
 * 逻辑：委托 plist_remove 统一处理（prefix_str 决定是否带前缀匹配）。
 */
DEFPY_YANG(
	no_ip_prefix_list, no_ip_prefix_list_cmd,
	"no ip prefix-list PREFIXLIST4_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action <any|A.B.C.D/M$prefix [{ge (0-32)|le (0-32)}]>",
	NO_STR
	IP_STR
	PREFIX_LIST_STR
	PREFIX_LIST_NAME_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"Any prefix match.  Same as \"0.0.0.0/0 le 32\"\n"
	"IP prefix <network>/<length>, e.g., 35.0.0.0/8\n"
	"Minimum prefix length to be matched\n"
	"Minimum prefix length\n"
	"Maximum prefix length to be matched\n"
	"Maximum prefix length\n")
{
	return plist_remove(vty, "ipv4", name, seq, action,
			    prefix_str ? prefix : NULL, ge, le);
}

/*
 * no_ip_prefix_list_seq —— 按序号删除前缀列表条目
 * "no ip prefix-list NAME seq N"，直接走 plist_remove 的 seq 分支。
 */
DEFPY_YANG(
	no_ip_prefix_list_seq, no_ip_prefix_list_seq_cmd,
	"no ip prefix-list PREFIXLIST4_NAME$name seq (1-4294967295)$seq",
	NO_STR
	IP_STR
	PREFIX_LIST_STR
	PREFIX_LIST_NAME_STR
	ACCESS_LIST_SEQ_STR)
{
	return plist_remove(vty, "ipv4", name, seq, NULL, NULL, 0, 0);
}

/*
 * no_ip_prefix_list_all —— "no ip prefix-list NAME"：删除整张 IPv4 前缀列表。
 */
DEFPY_YANG(
	no_ip_prefix_list_all, no_ip_prefix_list_all_cmd,
	"no ip prefix-list PREFIXLIST4_NAME$name",
	NO_STR
	IP_STR
	PREFIX_LIST_STR
	PREFIX_LIST_NAME_STR)
{
	char xpath[XPATH_MAXLEN];

	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/prefix-list[type='ipv4'][name='%s']", name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/*
 * ip_prefix_list_remark —— "ip prefix-list NAME description LINE..."：设置描述。
 * 逻辑同 ACL remark：CREATE 列表节点、拼接 LINE 为 remark、提交后释放临时串。
 * 坑：命令关键字是 description，但落到 YANG 的字段仍是 ./remark。
 */
DEFPY_YANG(
	ip_prefix_list_remark, ip_prefix_list_remark_cmd,
	"ip prefix-list PREFIXLIST4_NAME$name description LINE...",
	IP_STR
	PREFIX_LIST_STR
	PREFIX_LIST_NAME_STR
	ACCESS_LIST_REMARK_STR
	ACCESS_LIST_REMARK_LINE_STR)
{
	int rv;
	char *remark;
	char xpath[XPATH_MAXLEN];

	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/prefix-list[type='ipv4'][name='%s']", name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	remark = argv_concat(argv, argc, 4);
	nb_cli_enqueue_change(vty, "./remark", NB_OP_CREATE, remark);
	rv = nb_cli_apply_changes(vty, "%s", xpath);
	XFREE(MTYPE_TMP, remark);

	return rv;
}

/*
 * no_ip_prefix_list_remark —— "no ip prefix-list NAME description"：删除描述。
 * 逻辑：filter_remove_check_empty 且 del_remark=true（若删后表空则一并删除）。
 */
DEFPY_YANG(
	no_ip_prefix_list_remark, no_ip_prefix_list_remark_cmd,
	"no ip prefix-list PREFIXLIST4_NAME$name description",
	NO_STR
	IP_STR
	PREFIX_LIST_STR
	PREFIX_LIST_NAME_STR
	ACCESS_LIST_REMARK_STR)
{
	return filter_remove_check_empty(vty, "prefix", "ipv4", name, 0, true);
}

/*
 * ALIAS no_ip_prefix_list_remark_line —— 为上一条删除描述命令增加带 LINE... 的别名。
 */
ALIAS(
	no_ip_prefix_list_remark, no_ip_prefix_list_remark_line_cmd,
	"no ip prefix-list PREFIXLIST4_NAME$name description LINE...",
	NO_STR
	IP_STR
	PREFIX_LIST_STR
	PREFIX_LIST_NAME_STR
	ACCESS_LIST_REMARK_STR
	ACCESS_LIST_REMARK_LINE_STR)

/*
 * ipv6_prefix_list —— IPv6 前缀列表配置命令
 * "ipv6 prefix-list NAME [seq N] deny|permit any|X:X::X:X/M [ge X|le Y]"。
 * 逻辑：与 IPv4 前缀列表一致，字段换成 ipv6-prefix 与 ipv6-prefix-length-*
 *   （同样含负值检查，以及未再给出 ge / le 时的 DESTROY）。
 * 返回：CMD_SUCCESS / CMD_WARNING_CONFIG_FAILED。
 */
DEFPY_YANG(
	ipv6_prefix_list, ipv6_prefix_list_cmd,
	"ipv6 prefix-list PREFIXLIST6_NAME$name [seq (1-4294967295)] <deny|permit>$action <any|X:X::X:X/M$prefix [{ge (0-128)$ge|le (0-128)$le}]>",
	IPV6_STR
	PREFIX_LIST_STR
	PREFIX_LIST_NAME_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"Any prefix match.  Same as \"::0/0 le 128\"\n"
	"IPv6 prefix <network>/<length>, e.g., 3ffe::/16\n"
	"Maximum prefix length to be matched\n"
	"Maximum prefix length\n"
	"Minimum prefix length to be matched\n"
	"Minimum prefix length\n")
{
	int64_t sseq;
	struct plist_dup_args pda = {};
	char xpath[XPATH_MAXLEN];
	char xpath_entry[XPATH_MAXLEN + 128];

	/*
	 * Backward compatibility: don't complain about duplicated values,
	 * just silently accept.
	 */
	pda.pda_type = "ipv6";
	pda.pda_name = name;
	pda.pda_action = action;
	if (prefix_str) {
		prefix_copy(&pda.prefix, prefix);
		pda.ge = ge;
		pda.le = le;
	} else {
		pda.any = true;
	}

	/*
	 * Create the prefix-list first, so we can generate sequence if
	 * none given (backward compatibility).
	 */
	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/prefix-list[type='ipv6'][name='%s']", name);
	if (seq_str == NULL) {
		if (plist_is_dup(vty->candidate_config->dnode, &pda))
			sseq = pda.pda_seq;
		else {
			/* Use XPath to find the next sequence number. */
			sseq = acl_get_seq(vty, xpath, false);
			if (sseq < 0)
				return CMD_WARNING_CONFIG_FAILED;
		}

		snprintfrr(xpath_entry, sizeof(xpath_entry),
			   "%s/entry[sequence='%" PRId64 "']", xpath, sseq);
	} else
		snprintfrr(xpath_entry, sizeof(xpath_entry),
			   "%s/entry[sequence='%s']", xpath, seq_str);

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	nb_cli_enqueue_change(vty, xpath_entry, NB_OP_CREATE, NULL);

	nb_cli_enqueue_change(vty, "./action", NB_OP_MODIFY, action);
	if (prefix_str != NULL) {
		nb_cli_enqueue_change(vty, "./ipv6-prefix", NB_OP_MODIFY,
				      prefix_str);

		if (ge_str) {
			nb_cli_enqueue_change(
				vty, "./ipv6-prefix-length-greater-or-equal",
				NB_OP_MODIFY, ge_str);
		} else {
			/*
			 * Remove old ge if not being modified
			 */
			nb_cli_enqueue_change(
				vty, "./ipv6-prefix-length-greater-or-equal",
				NB_OP_DESTROY, NULL);
		}

		if (le_str) {
			nb_cli_enqueue_change(
				vty, "./ipv6-prefix-length-lesser-or-equal",
				NB_OP_MODIFY, le_str);
		} else {
			/*
			 * Remove old le if not being modified
			 */
			nb_cli_enqueue_change(
				vty, "./ipv6-prefix-length-lesser-or-equal",
				NB_OP_DESTROY, NULL);
		}
		nb_cli_enqueue_change(vty, "./any", NB_OP_DESTROY, NULL);
	} else {
		nb_cli_enqueue_change(vty, "./any", NB_OP_CREATE, NULL);
	}

	return nb_cli_apply_changes(vty, "%s", xpath_entry);
}

/*
 * no_ipv6_prefix_list —— 删除 IPv6 前缀列表条目（可带前缀匹配）。
 * 逻辑：委托 plist_remove(vty, "ipv6", ...) 统一处理。
 */
DEFPY_YANG(
	no_ipv6_prefix_list, no_ipv6_prefix_list_cmd,
	"no ipv6 prefix-list PREFIXLIST6_NAME$name [seq (1-4294967295)$seq] <deny|permit>$action <any|X:X::X:X/M$prefix [{ge (0-128)$ge|le (0-128)$le}]>",
	NO_STR
	IPV6_STR
	PREFIX_LIST_STR
	PREFIX_LIST_NAME_STR
	ACCESS_LIST_SEQ_STR
	ACCESS_LIST_ACTION_STR
	"Any prefix match.  Same as \"::0/0 le 128\"\n"
	"IPv6 prefix <network>/<length>, e.g., 3ffe::/16\n"
	"Maximum prefix length to be matched\n"
	"Maximum prefix length\n"
	"Minimum prefix length to be matched\n"
	"Minimum prefix length\n")
{
	return plist_remove(vty, "ipv6", name, seq, action,
			    prefix_str ? prefix : NULL, ge, le);
}

/*
 * no_ipv6_prefix_list_seq —— 按序号删除 IPv6 前缀列表条目
 * "no ipv6 prefix-list NAME seq N"。
 */
DEFPY_YANG(
	no_ipv6_prefix_list_seq, no_ipv6_prefix_list_seq_cmd,
	"no ipv6 prefix-list PREFIXLIST6_NAME$name seq (1-4294967295)$seq",
	NO_STR
	IPV6_STR
	PREFIX_LIST_STR
	PREFIX_LIST_NAME_STR
	ACCESS_LIST_SEQ_STR)
{
	return plist_remove(vty, "ipv6", name, seq, NULL, NULL, 0, 0);
}

/*
 * no_ipv6_prefix_list_all —— "no ipv6 prefix-list NAME"：删除整张 IPv6 前缀列表。
 */
DEFPY_YANG(
	no_ipv6_prefix_list_all, no_ipv6_prefix_list_all_cmd,
	"no ipv6 prefix-list PREFIXLIST6_NAME$name",
	NO_STR
	IPV6_STR
	PREFIX_LIST_STR
	PREFIX_LIST_NAME_STR)
{
	char xpath[XPATH_MAXLEN];

	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/prefix-list[type='ipv6'][name='%s']", name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/*
 * ipv6_prefix_list_remark —— "ipv6 prefix-list NAME description LINE..."：设置描述。
 * 逻辑同 IPv4 版本，类型为 ipv6，LINE 起始下标为 4，临时串需释放。
 */
DEFPY_YANG(
	ipv6_prefix_list_remark, ipv6_prefix_list_remark_cmd,
	"ipv6 prefix-list PREFIXLIST6_NAME$name description LINE...",
	IPV6_STR
	PREFIX_LIST_STR
	PREFIX_LIST_NAME_STR
	ACCESS_LIST_REMARK_STR
	ACCESS_LIST_REMARK_LINE_STR)
{
	int rv;
	char *remark;
	char xpath[XPATH_MAXLEN];

	snprintf(xpath, sizeof(xpath),
		 "/frr-filter:lib/prefix-list[type='ipv6'][name='%s']", name);
	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	remark = argv_concat(argv, argc, 4);
	nb_cli_enqueue_change(vty, "./remark", NB_OP_CREATE, remark);
	rv = nb_cli_apply_changes(vty, "%s", xpath);
	XFREE(MTYPE_TMP, remark);

	return rv;
}

/*
 * no_ipv6_prefix_list_remark —— "no ipv6 prefix-list NAME description"：删除描述。
 * 逻辑：filter_remove_check_empty 且 del_remark=true。
 */
DEFPY_YANG(
	no_ipv6_prefix_list_remark, no_ipv6_prefix_list_remark_cmd,
	"no ipv6 prefix-list PREFIXLIST6_NAME$name description",
	NO_STR
	IPV6_STR
	PREFIX_LIST_STR
	PREFIX_LIST_NAME_STR
	ACCESS_LIST_REMARK_STR)
{
	return filter_remove_check_empty(vty, "prefix", "ipv6", name, 0, true);
}

/*
 * ALIAS no_ipv6_prefix_list_remark_line —— 为 IPv6 删除描述命令增加带 LINE... 的别名。
 */
ALIAS(
	no_ipv6_prefix_list_remark, no_ipv6_prefix_list_remark_line_cmd,
	"no ipv6 prefix-list PREFIXLIST6_NAME$name description LINE...",
	NO_STR
	IPV6_STR
	PREFIX_LIST_STR
	PREFIX_LIST_NAME_STR
	ACCESS_LIST_REMARK_STR
	ACCESS_LIST_REMARK_LINE_STR)

/*
 * prefix_list_cmp —— 前缀列表条目的排序比较回调，按 sequence 升序。
 * 返回：seq1 - seq2。
 */
int prefix_list_cmp(const struct lyd_node *dnode1,
		    const struct lyd_node *dnode2)
{
	uint32_t seq1 = yang_dnode_get_uint32(dnode1, "sequence");
	uint32_t seq2 = yang_dnode_get_uint32(dnode2, "sequence");

	return seq1 - seq2;
}

/*
 * prefix_list_show —— 渲染一条前缀列表条目（YANG 显示回调）。
 * 逻辑：读 type（IPv4 / IPv6）决定前缀来源与 ge / le 字段名；先打印
 *   "prefix-list NAME seq N action"；any 直接收尾，否则输出前缀及可选 ge / le。
 */
void prefix_list_show(struct vty *vty, const struct lyd_node *dnode,
		      bool show_defaults)
{
	int type = yang_dnode_get_enum(dnode, "../type");
	const char *ge_str = NULL, *le_str = NULL;
	bool is_any;
	struct prefix p;

	is_any = yang_dnode_exists(dnode, "any");
	switch (type) {
	case YPLT_IPV4:
		if (!is_any)
			yang_dnode_get_prefix(&p, dnode, "ipv4-prefix");
		if (yang_dnode_exists(dnode,
				      "./ipv4-prefix-length-greater-or-equal"))
			ge_str = yang_dnode_get_string(
				dnode, "./ipv4-prefix-length-greater-or-equal");
		if (yang_dnode_exists(dnode,
				      "./ipv4-prefix-length-lesser-or-equal"))
			le_str = yang_dnode_get_string(
				dnode, "./ipv4-prefix-length-lesser-or-equal");

		vty_out(vty, "ip ");
		break;
	case YPLT_IPV6:
		if (!is_any)
			yang_dnode_get_prefix(&p, dnode, "ipv6-prefix");
		if (yang_dnode_exists(dnode,
				      "./ipv6-prefix-length-greater-or-equal"))
			ge_str = yang_dnode_get_string(
				dnode, "./ipv6-prefix-length-greater-or-equal");
		if (yang_dnode_exists(dnode,
				      "./ipv6-prefix-length-lesser-or-equal"))
			le_str = yang_dnode_get_string(
				dnode, "./ipv6-prefix-length-lesser-or-equal");

		vty_out(vty, "ipv6 ");
		break;
	}

	vty_out(vty, "prefix-list %s seq %s %s",
		yang_dnode_get_string(dnode, "../name"),
		yang_dnode_get_string(dnode, "sequence"),
		yang_dnode_get_string(dnode, "action"));

	if (is_any) {
		vty_out(vty, " any\n");
		return;
	}

	vty_out(vty, " %pFX", &p);
	if (ge_str)
		vty_out(vty, " ge %s", ge_str);
	if (le_str)
		vty_out(vty, " le %s", le_str);

	vty_out(vty, "\n");
}

/*
 * prefix_list_remark_show —— 渲染前缀列表的 description（YANG 显示回调）。
 * 逻辑：按 type 输出 "ip " / "ipv6 "，再输出
 *   "prefix-list NAME description <文本>"。
 */
void prefix_list_remark_show(struct vty *vty, const struct lyd_node *dnode,
			     bool show_defaults)
{
	int type = yang_dnode_get_enum(dnode, "../type");

	switch (type) {
	case YPLT_IPV4:
		vty_out(vty, "ip ");
		break;
	case YPLT_IPV6:
		vty_out(vty, "ipv6 ");
		break;
	}

	vty_out(vty, "prefix-list %s description %s\n",
		yang_dnode_get_string(dnode, "../name"),
		yang_dnode_get_string(dnode, NULL));
}

/*
 * filter_cli_init —— 注册本模块全部配置命令（均挂在 CONFIG_NODE 下）。
 * 分组安装：传统 Cisco 风格 ACL -> zebra 风格 ACL（IPv4 / IPv6 / MAC）
 *   -> 前缀列表（IPv4 / IPv6），每类都含 remark / 描述与整表删除命令。
 * 说明：由 access_list_init_new 在非后端模式下调用（后端不装 config 命令）。
 */
void filter_cli_init(void)
{
	/* access-list cisco-style (legacy). */
	install_element(CONFIG_NODE, &access_list_std_cmd);
	install_element(CONFIG_NODE, &no_access_list_std_cmd);
	install_element(CONFIG_NODE, &access_list_ext_cmd);
	install_element(CONFIG_NODE, &no_access_list_ext_cmd);
	install_element(CONFIG_NODE, &access_list_v6_std_cmd);
	install_element(CONFIG_NODE, &no_access_list_v6_std_cmd);
	install_element(CONFIG_NODE, &access_list_v6_ext_cmd);
	install_element(CONFIG_NODE, &no_access_list_v6_ext_cmd);

	/* access-list zebra-style. */
	install_element(CONFIG_NODE, &access_list_cmd);
	install_element(CONFIG_NODE, &no_access_list_cmd);
	install_element(CONFIG_NODE, &no_access_list_all_cmd);
	install_element(CONFIG_NODE, &access_list_remark_cmd);
	install_element(CONFIG_NODE, &no_access_list_remark_cmd);
	install_element(CONFIG_NODE, &no_access_list_remark_line_cmd);

	install_element(CONFIG_NODE, &ipv6_access_list_cmd);
	install_element(CONFIG_NODE, &no_ipv6_access_list_cmd);
	install_element(CONFIG_NODE, &no_ipv6_access_list_all_cmd);
	install_element(CONFIG_NODE, &ipv6_access_list_remark_cmd);
	install_element(CONFIG_NODE, &no_ipv6_access_list_remark_cmd);
	install_element(CONFIG_NODE, &no_ipv6_access_list_remark_line_cmd);

	install_element(CONFIG_NODE, &mac_access_list_cmd);
	install_element(CONFIG_NODE, &no_mac_access_list_cmd);
	install_element(CONFIG_NODE, &no_mac_access_list_all_cmd);
	install_element(CONFIG_NODE, &mac_access_list_remark_cmd);
	install_element(CONFIG_NODE, &no_mac_access_list_remark_cmd);
	install_element(CONFIG_NODE, &no_mac_access_list_remark_line_cmd);

	/* prefix lists. */
	install_element(CONFIG_NODE, &ip_prefix_list_cmd);
	install_element(CONFIG_NODE, &no_ip_prefix_list_cmd);
	install_element(CONFIG_NODE, &no_ip_prefix_list_seq_cmd);
	install_element(CONFIG_NODE, &no_ip_prefix_list_all_cmd);
	install_element(CONFIG_NODE, &ip_prefix_list_remark_cmd);
	install_element(CONFIG_NODE, &no_ip_prefix_list_remark_cmd);
	install_element(CONFIG_NODE, &no_ip_prefix_list_remark_line_cmd);

	install_element(CONFIG_NODE, &ipv6_prefix_list_cmd);
	install_element(CONFIG_NODE, &no_ipv6_prefix_list_cmd);
	install_element(CONFIG_NODE, &no_ipv6_prefix_list_seq_cmd);
	install_element(CONFIG_NODE, &no_ipv6_prefix_list_all_cmd);
	install_element(CONFIG_NODE, &ipv6_prefix_list_remark_cmd);
	install_element(CONFIG_NODE, &no_ipv6_prefix_list_remark_cmd);
	install_element(CONFIG_NODE, &no_ipv6_prefix_list_remark_line_cmd);
}
