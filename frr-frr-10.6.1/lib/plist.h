// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Prefix list functions.
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

/*
 * 文件导读：prefix-list（前缀列表）对外接口
 *
 * 前缀列表是路由策略的基础设施：按前缀（network / masklen）匹配，决定 permit 或 deny。
 * 它与 prefix.c 的分工是：prefix.c 提供“前缀本身”的运算原语（解析、掩码长度运算、
 * 包含判定、字符串互转、跨地址族统一表示），而 plist.c 用这些原语实现“列表匹配引擎”。
 * 换句话说，prefix.c 是地基，plist.c 是在地基上盖的策略匹配层。
 *
 * 本头文件只声明接口，数据结构定义在 plist_int.h，算法实现在 plist.c。
 *
 * 主要接口速览：
 *   - 生命周期：prefix_list_init / prefix_list_reset / prefix_list_get / prefix_list_delete。
 *   - 匹配入口：prefix_list_apply_ext（带 address_mode）及其便捷宏 prefix_list_apply。
 *   - 表项操作：prefix_list_entry_new / _delete / _lookup / prefix_new_seq_get。
 *   - 查询与钩子：prefix_list_lookup / _name / _afi，以及 add / delete 回调钩子。
 *   - BGP ORF 相关：prefix_bgp_orf_*（把前缀列表作为 ORF 能力下发给邻居）。
 *
 * 一个易混点：地址模式（address_mode）与普通前缀模式的区别——
 * 地址模式把传入参数当作“单个地址”处理，忽略 le / ge，只做地址是否落在前缀内。
 */

#ifndef _QUAGGA_PLIST_H
#define _QUAGGA_PLIST_H

#include <zebra.h>

#include "stream.h"
#include "vty.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 一条表项的处置动作。
 * PREFIX_DENY   ：命中即拒绝（丢弃该路由）。
 * PREFIX_PERMIT ：命中即放行（接受该路由）。
 * 匹配时按 seq 从小到大逐条尝试，第一条命中的表项决定最终结果；
 * 若全部未命中，缺省动作是 deny。
 */
enum prefix_list_type {
	PREFIX_DENY,
	PREFIX_PERMIT,
};

/*
 * 前向声明：具体定义在 plist_int.h，这里只用到指针，避免暴露内部布局。
 */
struct prefix_list;
struct prefix_list_entry;

/*
 * ORF（Outbound Route Filtering）里描述“一条前缀过滤器”的紧凑结构。
 * seq  ：序号，决定 ORF 表项的应用顺序。
 * ge/le：前缀长度约束下界 / 上界。
 * p    ：目标前缀。
 * 当 BGP 邻居需要按前缀列表做 ORF 时，用本结构把列表条目编码进 ORF 报文。
 */
struct orf_prefix {
	uint32_t seq;
	uint8_t ge;
	uint8_t le;
	struct prefix p;
};

/* Prototypes. */
/*
 * 生命周期与钩子：
 *   prefix_list_init      ：初始化前缀列表子系统（注册命令行、建全局容器），进程启动时调用一次。
 *   prefix_list_reset    ：清空所有前缀列表（如重载配置或退出前）。
 *   prefix_list_add_hook /
 *   prefix_list_delete_hook：注册回调，在列表新增 / 删除时被通知（供 BGP 等模块联动）。
 */
extern void prefix_list_init(void);
extern void prefix_list_reset(void);
extern void prefix_list_add_hook(void (*func)(struct prefix_list *plst));
extern void prefix_list_delete_hook(void (*func)(struct prefix_list *plist));

/*
 * 查询类接口：
 *   prefix_list_name  ：返回列表名字（内部指针，勿释放）。
 *   prefix_list_afi   ：返回列表所属地址族（AFI_IP / AFI_IP6 等）。
 *   prefix_list_lookup：按地址族 + 名字查找列表，找不到返回 NULL。
 */
extern const char *prefix_list_name(struct prefix_list *plist);
extern afi_t prefix_list_afi(struct prefix_list *plist);
extern struct prefix_list *prefix_list_lookup(afi_t afi, const char *name);

/*
 * prefix_list_apply_which_prefix
 *
 * Allow calling function to learn which prefix
 * caused the DENY or PERMIT.
 *
 * If no pointer is sent in, do not return anything.
 * If it is a empty plist return a NULL pointer.
 *
 * address_mode = the "prefix" being passed in is really an address, match
 * regardless of prefix length (i.e. ge/le are ignored.)  prefix->prefixlen
 * must be /32.
 */
/*
 * 核心匹配入口 prefix_list_apply_ext（上面的英文注释已说明 matches 回填与 address_mode 语义）：
 *   返回值       ：命中即为 PREFIX_PERMIT 或 PREFIX_DENY；未命中一律返回 PREFIX_DENY（缺省拒绝）。
 *   参数 plist   ：要匹配的列表；参数 prefix 用 union prefixconstptr 传，可兼容 v4 / v6。
 *   参数 matches ：若非 NULL，回填“是哪条表项导致的结果”，便于调试与统计。
 *   address_mode ：true 时把传入前缀当作单个地址，忽略 le / ge，只判断地址是否落在前缀内。
 * 副作用：会递增命中表项的 hitcnt 计数。
 */
extern enum prefix_list_type
prefix_list_apply_ext(struct prefix_list *plist,
		      const struct prefix_list_entry **matches,
		      union prefixconstptr prefix,
		      bool address_mode);
/*
 * 便捷宏：普通前缀匹配，不要匹配表项回填（matches 传 NULL），address_mode 为 false。
 */
#define prefix_list_apply(A, B) \
	prefix_list_apply_ext((A), NULL, (B), false)

/*
 * BGP ORF（出方向路由过滤）相关接口：
 *   prefix_bgp_orf_lookup     ：按地址族 + 名字查 ORF 用的列表。
 *   prefix_bgp_orf_entry      ：把列表条目按 permit / deny 标志编码写入报文字节流，返回该流。
 *   prefix_bgp_orf_set        ：从收到的 ORF 条目设置 / 清除一条本地过滤器。
 *   prefix_bgp_orf_remove_all ：清空某地址族下全部 ORF 过滤器。
 *   prefix_bgp_show_prefix_list：把列表内容输出到 vty（支持 JSON），用于 show 命令。
 */
extern struct prefix_list *prefix_bgp_orf_lookup(afi_t afi, const char *name);
extern struct stream *prefix_bgp_orf_entry(struct stream *s, struct prefix_list *plist,
					   uint8_t init_flag, uint8_t permit_flag,
					   uint8_t deny_flag);
extern int prefix_bgp_orf_set(char *name, afi_t afi, struct orf_prefix *orfp, int permit, int set);
extern void prefix_bgp_orf_remove_all(afi_t afi, char *name);
extern int prefix_bgp_show_prefix_list(struct vty *vty, afi_t afi, char *name,
				       bool use_json);

/*
 * 列表对象的增删与序号：
 *   prefix_list_get      ：取得（不存在则按 orf 标志创建）指定地址族下的列表，返回其指针。
 *   prefix_list_delete   ：删除整个列表及其全部表项。
 *   prefix_new_seq_get   ：生成一个新的可用 seq（通常取当前最大序号 + 5）。
 */
extern struct prefix_list *prefix_list_get(afi_t afi, int orf,
					   const char *name);
extern void prefix_list_delete(struct prefix_list *plist);
extern int64_t prefix_new_seq_get(struct prefix_list *plist);

/*
 * 表项（entry）操作：
 *   prefix_list_entry_new   ：分配并清零一条新表项。
 *   prefix_list_entry_delete：从列表中移除一条表项；update_list 非 0 时同时更新匹配结构。
 *   prefix_list_entry_lookup：按 (prefix, type, seq, le, ge) 精确查找已存在的表项，用于去重。
 */
extern struct prefix_list_entry *prefix_list_entry_new(void);
extern void prefix_list_entry_delete(struct prefix_list *plist,
				     struct prefix_list_entry *pentry,
				     int update_list);
extern struct prefix_list_entry *
prefix_list_entry_lookup(struct prefix_list *plist, struct prefix *prefix,
			 enum prefix_list_type type, int64_t seq, int le,
			 int ge);

#ifdef __cplusplus
}
#endif

#endif /* _QUAGGA_PLIST_H */
