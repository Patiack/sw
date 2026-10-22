// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Administrative-group library (RFC3630, RFC5305, RFC5329, RFC7308)
 *
 * Copyright 2022 Hiroki Shirokura, LINE Corporation
 * Copyright 2022 Masakazu Asama
 * Copyright 2022 6WIND S.A.
 */

#ifndef _FRR_ADMIN_GROUP_H
#define _FRR_ADMIN_GROUP_H

/* ============================================================================
 * 【文件导读】admin_group.h —— 链路管理组(Administrative Group)的位图抽象
 * ============================================================================
 * 【是什么】IGP 里每条链路可以带一串“管理组”位(俗称 link color / affinity)，
 * 用于 TE 选路与约束判断。传统形式是 32 位(RFC3630 的 Admin Group、RFC5305 的
 * IS-IS 扩展)，后来被扩展成“变长位图”(RFC7308 的 Extended Admin Group，理论
 * 上 1024 位)。所以本模块要同时表达“短的 32 位”和“长的千位图”。
 *
 * 【为什么用位图而不是 uint32_t】因为要统一两种长度。设计上让 bitmap 为**空**
 * 时代表“标准 32 位形式”，非空时代表扩展形式——由此产生了一组看似多余的
 * API(如 admin_group_bulk_set/get_offset、explicit zero 的允许与禁止)，它们全
 * 都是为“在两种表示之间平滑切换、且能区分‘未设置’与‘显式全 0’”服务的。
 *
 * 【术语】FAD 是 Flexible Algorithm Definition(灵活算法定义)，其中的管理组
 * 约束要与链路的实际管理组做匹配，于是有了文件末尾的 match_any / match_all。
 *
 * 【阅读顺序】两个容量宏 → struct admin_group → 打印/比较/拷贝等基础操作
 * → get/set/unset 与 bulk 操作 → explicit zero 开关 → 末尾两个匹配函数。
 * ============================================================================
 */
#include "zebra.h"
#include "memory.h"
#include "bitfield.h"

/* 【常量】admin_group_string() 输出缓冲的建议上限(字符数)。位图最长 1024 位，
 * 按“每 4 位一个十六进制字符 + 分隔符”估算，2048 足够；调用方按此大小开栈数组。 */
#define ADMIN_GROUP_PRINT_MAX_SIZE 2048
/* 【常量】扩展管理组支持的最大位序号(RFC7308：最多 1024 位)。set/get 的 pos
 * 必须小于它，否则是越界——但**本头文件不做校验**，越界由底层 bitfield 承担。 */
#define EXT_ADMIN_GROUP_MAX_POSITIONS 1024

/* 【结构体】管理组位图(仅一个成员，但类型选择有讲究)。
 *   bitmap: 变长位图句柄。**关键语义**：位图为“空”时，表示本条管理组是传统的
 *           32 位标准形式；一旦有内容就按扩展位图处理。这个“空=标准”的约定是
 *           全文件所有分支判断的根源，读实现时务必记住。 */
struct admin_group {
	bitfield_t bitmap;
};

/* 【接口组：格式化】三者都是“写进调用方给的 out 缓冲并返回 out”。
 *   admin_group_string          : 按 indent 缩进输出完整位图描述。
 *   admin_group_standard_print  : 把**裸的 32 位**标准管理组直接格式化
 *                                 (输入是 uint32_t，不是 struct admin_group)。
 *   admin_group_print           : 输出 struct admin_group，内部按“标准/扩展”
 *                                 自动选择写法，是日常最该用的那个。
 * 【易踩的坑】三个函数都不返回长度、也不报告截断；sz/缓冲区不足时会静默截断，
 *   调用方应使用 ADMIN_GROUP_PRINT_MAX_SIZE 开缓冲。 */
char *admin_group_string(char *out, size_t sz, int indent,
			 const struct admin_group *ag);
char *admin_group_standard_print(char *out, int indent, uint32_t bitmap);
char *admin_group_print(char *out, int indent, const struct admin_group *ag);
/* 【接口组：基础运算】比较 / 拷贝 / 建 / 拆。
 *   admin_group_cmp  : 判两管理组是否**相等**(注意返回 bool 而非三态)。
 *   admin_group_copy : 深拷贝(dst 已有内容会被覆盖，需先 init)。
 *   admin_group_init / term : 生命周期；term 释放位图，之后需重新 init 才能再用。
 * 【易踩的坑】copy 不做 dst 的旧资源释放，重复 copy 到同一个 dst 会泄漏。 */
bool admin_group_cmp(const struct admin_group *ag1,
		     const struct admin_group *ag2);
void admin_group_copy(struct admin_group *dst, const struct admin_group *src);
void admin_group_init(struct admin_group *ag);
void admin_group_term(struct admin_group *ag);
/* 【接口组：按位取值/置位】以“位序号 pos”为索引的读写。
 *   admin_group_get_offset : 由**字节偏移** oct_offset 取出该处的一整字(uint32)，
 *                            用于把扩展位图与标准 32 位格式互相换算(易错点)。
 *   admin_group_set/unset  : 置位 / 清位；unset 在“标准形式”下同样有效。
 *   admin_group_get        : 读某位，返回 0/1(用 int 而非 bool，便于直接运算)。
 *   admin_group_bulk_set   : 在字节偏移 oct_offset 处**整字**写入 bitmap，
 *                            是一次性铺 32 位的快捷路径(注意 oct_offset 是字节
 *                            偏移而不是位序号，与 set/unset 的 pos 单位不同!)。 */
uint32_t admin_group_get_offset(const struct admin_group *ag,
				size_t oct_offset);
void admin_group_set(struct admin_group *ag, size_t pos);
void admin_group_unset(struct admin_group *ag, size_t pos);
int admin_group_get(const struct admin_group *ag, size_t pos);
void admin_group_bulk_set(struct admin_group *ag, uint32_t bitmap,
			  size_t oct_offset);
/* 【接口组：容量与清零】
 *   admin_group_size      : 以**字节**计的位图大小。
 *   admin_group_nb_words  : 以**字(32 位)**计的个数；内部换算关系是
 *                           nb_words == (size + 3) / 4 —— 算缓冲/遍历时用它，
 *                           别拿 size 当字数用。
 *   admin_group_clear     : 清空所有位。
 *   admin_group_zero      : 判“是否所有位都是 0”(内容层面)。
 *   admin_group_explicit_zero : 判“是否被**显式**标记为全 0”——与上一个的区别
 *                           在于“从没设过”和“明确设为全 0”在语义上不同(例如
 *                           配置需不需要回写)。 */
size_t admin_group_size(const struct admin_group *ag);
size_t admin_group_nb_words(const struct admin_group *ag);
void admin_group_clear(struct admin_group *ag);
bool admin_group_zero(const struct admin_group *ag);
bool admin_group_explicit_zero(const struct admin_group *ag);
/* 【接口组：显式全 0 的开关】允许 / 禁止把“全 0”当作一个**有效取值**保存。
 * 典型用途是 YANG 配置校验：某些场景下 all-zero 是非法的，需被拒。 */
void admin_group_allow_explicit_zero(struct admin_group *ag);
void admin_group_disallow_explicit_zero(struct admin_group *ag);

/* 【接口组：约束匹配】判断“本条 FAD 的管理组约束”是否被**链路**满足。
 *   fad_ag       : 灵活算法定义里的约束管理组(要匹配的目标)。
 *   link_std_ag  : 链路的标准 32 位管理组；**是指针，为 NULL 表示链路未配置**。
 *   link_ag       : 链路的扩展管理组。
 *   match_any    : 约束与链路**有任意一位相交**即算满足。
 *   match_all    : 约束的**每一位**都必须被链路覆盖才算满足。
 * 【易踩的坑】link_std_ag 为 NULL 时两个函数的处理方式不同(一个视为无约束满足、
 * 另一个视为不匹配)，调用前需明确语义；这也是把标准与扩展两种表示分开传参的
 * 副作用——读实现时要同时看三条输入。 */
bool admin_group_match_any(const struct admin_group *fad_ag,
			   const uint32_t *link_std_ag,
			   const struct admin_group *link_ag);
bool admin_group_match_all(const struct admin_group *fad_ag,
			   const uint32_t *link_std_ag,
			   const struct admin_group *link_ag);

#endif /* _FRR_ADMIN_GROUP_H */
