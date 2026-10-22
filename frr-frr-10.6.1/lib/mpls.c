// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * mpls functions
 *
 * Copyright (C) 2018 Cumulus Networks, Inc.
 *                    Donald Sharp
 */
#include <zebra.h>
/* ============================================================================
 * 【文件导读】mpls.c —— MPLS 标签与字符串之间的互转(公共小工具)
 * ============================================================================
 * 【为什么这俩转换要放在 lib】MPLS 标签是多个模块都要处理的公共概念：zebra 要
 * 解析配置里的 "100/200/300"、LDP 要显示标签栈、BGP VPN 也要格式化 EVPN 标签。
 * 若各写各的解析，就会出现“有的接受保留标签、有的不接受”这类不一致。于是把
 * 解析与格式化放到公共库，供全体使用。
 *
 * 【两个函数的分工与不对称】
 *   mpls_str2label : 字符串 "a/b/c" → 标签数组，返回**错误码**，做完整校验。
 *   mpls_label2str : 标签数组 → 字符串，返回缓冲区指针，**不做错误报告**。
 * 一个“严格入、可错”，一个“宽出、不错”。理解这对不对称能省去很多困惑：
 * 解析路径的防御都在前者，格式化路径基本不会失败。
 *
 * 【术语提示】MPLS 标签 0~15 是**保留标签**(RFC3032)，用户配置里不允许直接用；
 * 这正是 str2label 会返回 -2 的原因。而 label2str 的 pretty 开关则决定是否把
 * 标签渲染成“人看的名字”(如 implicit-null)还是数字。
 * ============================================================================
 */
#include <mpls.h>
#include <memory.h>

/*
 * String to label conversion, labels separated by '/'.
 *
 * @param label_str labels separated by /
 * @param num_labels number of labels; zero if conversion was unsuccessful
 * @param labels preallocated mpls_label_t array of size MPLS_MAX_LABELS; only
 *               modified if the conversion succeeded
 * @return  0 on success
 *         -1 if the string could not be parsed as integers
 *         -2 if a label was inside the reserved range (0-15)
 *         -3 if the number of labels given exceeds MPLS_MAX_LABELS
 */
/* 【函数】把 "100/200/300" 形式的字符串解析成标签数组(英文原注释见上)。
 *   label_str  : '?' 分隔的标签串(分隔符是斜杠，不是逗号!)。
 *   num_labels : **输出**实际解析出的标签个数；失败时被置 0。
 *   labels     : **输出**预分配的 mpls_label_t 数组，容量须 >= MPLS_MAX_LABELS；
 *                失败时**不被修改**(原注释明确说明，这是有意的原子性保证)。
 *   返回       : 0 成功；-1 存在非数字内容；-2 出现保留标签(0~15)；
 *                -3 标签个数超过 MPLS_MAX_LABELS。
 * 【实现要点】① 先 XSTRDUP 一份副本，因为 strsep 会**就地改写**字符串，不能碰
 *   调用方的常量串；② 解析结果先写进局部数组 pl[]，全部校验通过后才 memcpy 到
 *   调用方的 labels——这就是“失败不改输出”得以成立的原因，也是本函数值得学的
 *   一点：输出参数的写入放在最后一步。
 * 【易踩的坑】① 循环条件 `lstr && !rc`：strsep 走到末尾会把 lstr 置 NULL，
 *   循环自然结束；② “超量”判定写成 `i == MPLS_MAX_LABELS && lstr`，必须在
 *   循环**之后**单独判，因为循环因 i 触顶退出时 rc 仍为 0；③ strtoul 的结果
 *   赋给 mpls_label_t(20 bit)时未做上界检查，超出 2^20 的值会被**截断**而
 *   非报错——这一点上游代码如此，此处只作标注。 */
int mpls_str2label(const char *label_str, uint8_t *num_labels,
		   mpls_label_t *labels)
{
	char *ostr;			  // copy of label string (start)
	char *lstr;			  // copy of label string
	char *nump;			  // pointer to next segment
	char *endp;			  // end pointer
	int i;				  // for iterating label_str
	int rc;				  // return code
	mpls_label_t pl[MPLS_MAX_LABELS]; // parsed labels

	/* labels to zero until we have a successful parse */
	ostr = lstr = XSTRDUP(MTYPE_TMP, label_str);
	*num_labels = 0;
	rc = 0;

	for (i = 0; i < MPLS_MAX_LABELS && lstr && !rc; i++) {
		nump = strsep(&lstr, "/");
		pl[i] = strtoul(nump, &endp, 10);

		/* format check */
		if (*endp != '\0')
			rc = -1;
		/* validity check */
		else if (!IS_MPLS_UNRESERVED_LABEL(pl[i]))
			rc = -2;
	}

	/* excess labels */
	if (!rc && i == MPLS_MAX_LABELS && lstr)
		rc = -3;

	if (!rc) {
		*num_labels = i;
		memcpy(labels, pl, *num_labels * sizeof(mpls_label_t));
	}

	XFREE(MTYPE_TMP, ostr);

	return rc;
}

/*
 * Label to string conversion, labels in string separated by '/'.
 */
/* 【函数】把标签数组格式化成 '?' 分隔的字符串(英文原注释见上)。
 *   num_labels: 标签个数。
 *   labels    : 标签数组。
 *   buf       : **输出**调用方提供的缓冲区，函数只用不分配。
 *   len       : buf 的容量，用于内部 strlcat 的有界拼接。
 *   type      : LSP 类型，决定 EVPN 场景下是否按 VNI 解读(见下)。
 *   pretty    : 非 0 时用 label2str 输出“人类可读名”(如 implicit-null)；
 *               为 0 时输出纯数字(或 EVPN 的 VNI)。
 *   返回      : 就是传入的 buf，便于链式调用(如直接当 printf 参数)。
 * 【易踩的坑】① 唯一的副作用是把 buf 写空/写入内容，无错误报告——但注意
 *   数字分支里的 snprintf 是自己截断、EVPN 分支用的是 label2vni()，两条路径的
 *   取值来源不同，别以为 pretty 只影响“显示形式”；② 内部用 strlcat 有界拼接，
 *   buf 过小会**静默截断**而不报错，调用方应预留足够空间(常见是 BUFSIZ 级)；
 *   ③ 分隔符只加在 i != 0 之前，因此空数组(num_labels==0)时 buf 保持空串。 */
char *mpls_label2str(uint8_t num_labels, const mpls_label_t *labels, char *buf,
		     int len, enum lsp_types_t type, int pretty)
{
	char label_buf[BUFSIZ];
	int i;

	buf[0] = '\0';
	for (i = 0; i < num_labels; i++) {
		if (i != 0)
			strlcat(buf, "/", len);
		if (pretty)
			label2str(labels[i], type, label_buf,
				  sizeof(label_buf));
		else
			snprintf(label_buf, sizeof(label_buf), "%u",
				 ((type == ZEBRA_LSP_EVPN)
					  ? label2vni(&labels[i])
					  : labels[i]));

		strlcat(buf, label_buf, len);
	}

	return buf;
}
