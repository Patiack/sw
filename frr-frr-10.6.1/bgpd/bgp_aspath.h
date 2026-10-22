// SPDX-License-Identifier: GPL-2.0-or-later
/* AS path related definitions.
 * Copyright (C) 1997, 98, 99 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_aspath.h —— AS_PATH 属性（BGP 的“路径签名”）
 * ============================================================================
 * 【AS_PATH 是什么】它是 BGP 最重要的属性之一：一条路由经过的每个 AS 都会把
 * 自己的 AS 号**前置**进去，于是接收方看到的就是“到目的地的路径”。它同时承担
 * 三种职责——① 防环（收到含本 AS 号的路由就丢弃）；② 选路（**短者优先**）；
 * ③ 策略匹配（as-path 正则、filter-list 都作用在它的字符串形式上）。
 *
 * 【它不是一维数组，而是“段的数组”】线格式里 AS_PATH = 若干「段」，
 * 每段 = 「类型 + 长度 + 若干 AS 号」。四种段类型（见下面宏）：
 *   AS_SEQUENCE / AS_SET        : 普通（非联盟）的**有序**序列 / **无序集合**。
 *       AS_SET 是聚合(aggregate)产生的：把多个来源的 AS 合并成一个集合，语义上
 *       不表达顺序，这也解释了为什么“集合算 1 跳”而不是算 N 跳。
 *   AS_CONFED_SEQUENCE / _SET   : **联盟(confederation)** 内部使用的同类结构——
 *       联盟子 AS 之间的路径，对外不体现。因此几乎所有函数都有 “confed” 版本，
 *       处理“对外可见路径”与“对外不可见的联盟路径”要分开算。
 *   这就是 `aspath_count_hops`（普通跳数，排除联盟段）与 `aspath_count_confeds`
 *   并存的原因，别把二者混用。
 *
 * 【两种 AS 宽度与 BGP_AS_TRANS】AS 号线格式原本 2 字节，后来 RFC 6793 扩到 4
 * 字节。过渡期用 23456(BGP_AS_TRANS) 占位，真正的 4 字节 AS 放在 AS4_PATH 属性里；
 * 由 `aspath_reconcile_as4` 把两者**对账合并**成最终路径。所以 `aspath_parse` 与
 * `aspath_put` 都带 `use32bit`——**同一份数据在线上有两种编码**，读写必须成对一致。
 * 本文件里还有 `aspath_has_as4`（是否含 4 字节 AS）与 asnotation 相关参数：
 * 因为 2 字节 AS 有多种“点分”写法（1.10 等），**显示格式**由 asnotation 决定，
 * 它也影响 str 字段的内容与 JSON 输出——同一个 aspath 用不同 asnotation 打印，
 * 字符串不同，正则匹配结果也可能不同，这是个隐蔽的坑。
 *
 * 【intern 机制（refcnt 的由来）】struct aspath 是**可共享对象**：内容相同的 AS
 * 路径在内存里只保留一份，由引用计数共享。所有 `aspath_*` 函数要么返回**已 intern
 * 的对象**（调用方需 unintern 或明确不持有），要么返回新对象，读实现确认——
 * 这是本文件最容易出内存问题的地方。aspath_dup/aspath_intern 的区别也在此。
 *
 * 【阅读建议】段类型宏 → struct assegment / struct aspath → aspath_parse /
 *            aspath_put（线格式）→ aspath_cmp / aspath_key_make（哈希比较）→
 *            各类 aspath_*_check（匹配与防环）→ aspath_filter_exclude 与
 *            bgp_compute_aggregate_*（策略动作）。实现在 bgp_aspath.c。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_ASPATH_H
#define _QUAGGA_BGP_ASPATH_H

#include "lib/json.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_filter.h"
#include <typesafe.h>

/* AS path segment type.  */
/* 【段类型】线格式里的 1 字节类型码（取值与 RFC 4271 一致，不能改）。
 * 1/2 是对外可见路径；3/4 是联盟内部路径，**对外不通告**——所以很多统计函数
 * 要问一句“算不算 confed 段”。 */
#define AS_SET                       1
#define AS_SEQUENCE                  2
#define AS_CONFED_SEQUENCE           3
#define AS_CONFED_SET                4

/* Private AS range defined in RFC2270.  */
/* 【私有 AS(2 字节)】64512 ~ 65535。对外发送时可被 `remove-private-AS` 剥掉，
 * 是运营商常见的清洗动作。 */
#define BGP_PRIVATE_AS_MIN       64512U
#define BGP_PRIVATE_AS_MAX UINT16_MAX

/* Private 4 byte AS range defined in RFC6996.  */
/* 【私有 AS(4 字节)】4200000000 ~ 4294967294。注意上界是 ...294 而不是
 * 全 1——全 1(4294967295) 保留用作“最大值”哨兵(BGP_AS4_MAX)，两者别搞混。 */
#define BGP_PRIVATE_AS4_MIN     4200000000U
#define BGP_PRIVATE_AS4_MAX     4294967294U

/* we leave BGP_AS_MAX as the 16bit AS MAX number.  */
/* 【几个 AS 边界值】名字相似但含义不同，是本文件高频踩坑点：
 *   BGP_AS_MAX  : 仍是 **2 字节** 的上界(65535)，尽管 AS 已扩展到 4 字节——
 *                 原注释特意强调“we leave”，即有意保留旧语义（很多旧代码
 *                 仍用它表示“2 字节 AS 的最大值”，而不是“AS 号的最大值”）。
 *   BGP_AS4_MAX : 4 字节上界 4294967295。
 *   BGP_AS_ZERO : 0 号 AS，协议上是保留/非法的，aspath_check_as_zero 专门查它。
 *   BGP_AS_TRANS: 23456，RFC 6793 的过渡占位 AS——“这里本该是个 4 字节 AS，
 *                 真值在 AS4_PATH 属性里”。对账逻辑见 aspath_reconcile_as4。 */
#define BGP_AS_ZERO		          0
#define BGP_AS_MAX UINT16_MAX
#define BGP_AS4_MAX		4294967295U
/* Transition 16Bit AS as defined by IANA */
#define BGP_AS_TRANS		 23456U

/* 【宏】判断一个 AS 号是否属于**私有**范围（2 字节段或 4 字节段任一）。
 * 注意 ASN 会被**求值两次**——传带副作用的表达式（如 ASN++）会出错；
 * 另外它不做类型检查，传负数或超界值的结果不可靠。 */
#define BGP_AS_IS_PRIVATE(ASN)                                                 \
	(((ASN) >= BGP_PRIVATE_AS_MIN && (ASN) <= BGP_PRIVATE_AS_MAX)          \
	 || ((ASN) >= BGP_PRIVATE_AS4_MIN && (ASN) <= BGP_PRIVATE_AS4_MAX))

/* AS_PATH segment data in abstracted form, no limit is placed on length */
/* 【结构】assegment —— AS_PATH 里的**一个段**。
 *   next        : 段与段串成单链表（aspath.segments 指向第一段）。
 *   as          : AS 号数组。注意是 as_t（4 字节）——即便线上是 2 字节编码，
 *                 内存里统一按 4 字节存，转换发生在 parse/put 时。
 *   length      : 本段 AS 号**个数**（不是字节数）。用 unsigned short，理论上限
 *                 65535，实际受 BGP 报文最大长度约束。
 *   type        : 上面四种段类型之一。
 * 注意“no limit is placed on length”是设计选择：解析时不做长度校验，校验交给
 * 上层(nlri/attr 检查)，所以构造数据时要自己保证合理性。 */
struct assegment {
	struct assegment *next;
	as_t *as;
	unsigned short length;
	uint8_t type;
};

/* AS path may be include some AsSegments.  */
/* 【结构】aspath —— 完整的 AS_PATH 属性（**intern 共享对象**，见文件头导读）。
 *   refcnt  : 引用计数。共享的前提——内容相同只存一份，谁用谁加 1。
 *   segments: 段链表的头。空路径(本地起源)时为空。
 *   json    : 缓存的 JSON 表示（惰性生成）。**缓存意味着 asnotation 变化后必须
 *             失效重建**，改这块逻辑时要留意。
 *   str / str_len : AS 路径的**字符串形式**，既用于 vty 显示，也用于 **as-path
 *             正则匹配**（filter-list 拿它去跑 regexec）。所以这个字符串不是
 *             纯装饰，改了会影响策略匹配结果。str_len 是长度缓存，避免每次 strlen。
 *   count   : 缓存“跳数/元素数”之类的统计值（具体口径读实现），用于快速比较。
 *   asnotation: 生成 str 时采用的 2 字节 AS 写法（点分 / 点分加 / 纯数字）。
 *             它被**存进对象**正是因为 str 是缓存——不知道当初的写法就无法
 *             判断缓存是否还有效。 */
struct aspath {
	/* Reference count to this aspath.  */
	unsigned long refcnt;

	/* segment data */
	struct assegment *segments;

	/* AS path as a json object */
	json_object *json;

	/* String expression of AS path.  This string is used by vty output
	   and AS path regular expression match.  */
	char *str;
	unsigned short str_len;
	uint32_t count;

	/* AS notation used by string expression of AS path */
	enum asnotation_mode asnotation;
};

/* 【长度】AS 路径字符串的**初始**缓冲长度(常见路径的合理起点)，实现会在
 * 需要时扩容——不要理解成硬上限。 */
#define ASPATH_STR_DEFAULT_LEN 32

/* `set as-path exclude ASn' */
/* 【结构】aspath_exclude —— 支撑 `set as-path exclude` 这条 route-map 动作。
 *   exclude_list     : 类型安全 DLIST 的挂载项（下面 DECLARE_DLIST 声明用），
 *                      使这个结构能被放进 as_list 的 exclude_rule 链表。
 *   aspath           : 要排除的那条 AS 路径（可能是显式写的，或由 acl 算出来）。
 *   exclude_all      : 语义开关，表示“排除全部”——此时不看具体 AS 号，
 *                      整个路径都被排除；与逐条比对是两条不同代码路径。
 *   exclude_aspath_acl_name : 用户写的 as-path access-list **名字**（配置原文，
 *                      用于回显）。
 *   exclude_aspath_acl      : 上述名字解析出来的**对象**。名字与对象都要留：
 *                      前者回显、后者匹配；列表被删时对象会失效，需要失效处理
 *                      （见 as_exclude_set_orphan / remove_orphan 这对函数，
 *                      "orphan" 即“宿主列表已不在”的悬空状态）。 */
struct aspath_exclude {
	struct as_list_list_item exclude_list;
	struct aspath *aspath;
	bool exclude_all;
	char *exclude_aspath_acl_name;
	struct as_list *exclude_aspath_acl;
};
DECLARE_DLIST(as_list_list, struct aspath_exclude, exclude_list);


/* Prototypes. */
/* 【函数】子系统生命周期：初始化/销毁 AS 路径的 intern 表（哈希表）。
 * 这两个必须与其他所有 aspath_* 调用有先后顺序——先 init，进程退出前 finish。 */
extern void aspath_init(void);
extern void aspath_finish(void);
/* 【函数】aspath_parse —— 从**线格式字节流**解析出 AS_PATH。
 *   s / length : 数据流与可用长度（必须给长度，段的边界全靠它）。
 *   use32bit   : 决定按 2 字节还是 4 字节解释每个 AS 号（见文件头导读的两宽度问题）。
 *   asnotation : 决定生成 str 时的写法，会**存进对象**（因为 str 被缓存）。
 * 返回的对象是 intern 过的还是新建的，读实现确认后再决定是否 unintern。 */
extern struct aspath *aspath_parse(struct stream *s, size_t length,
				   int use32bit,
				   enum asnotation_mode asnotation);

/* 【函数】aspath_dup —— **深拷贝**一份 AS 路径（新的段、新的字符串），
 * 与 aspath_intern（共享同一份）语义完全相反。想在副本上改而不影响原对象时用 dup。 */
extern struct aspath *aspath_dup(struct aspath *aspath);
/* 【函数】聚合与前置：
 *   aspath_aggregate : 把两条路径**合并**成聚合路由的路径——按 RFC 4271 的规则
 *                      找公共前缀，去掉的差异部分要变成 AS_SET（这正是 AS_SET
 *                      的由来）。参数顺序影响结果，读实现确认语义。
 *   aspath_prepend    : 把 as2 前置到 as1 前面。**AS 前置**（本地往外发时重复
 *                      自己的 AS 号）就是靠它实现，也是 route-map `set as-path
 *                      prepend` 的底座。 */
extern struct aspath *aspath_aggregate(struct aspath *as1, struct aspath *as2);
extern struct aspath *aspath_prepend(struct aspath *as1, struct aspath *as2);
/* 【函数】`set as-path exclude` 的支撑（配合 struct aspath_exclude）：
 *   as_exclude_set_orphan / remove_orphan : 标记/取消标记“宿主 as-path 列表
 *        已被删、本配置项成了孤儿”。孤儿状态必须保留配置以便回显，但不能再
 *        参与匹配——这就是要有 orphan 这套机制的原因。
 *   as_exclude_lookup_orphan : 按 acl 名字在孤儿集合里查找。
 * 三个函数都由配置增删路径调用，普通数据路径用不到。 */
extern void as_exclude_set_orphan(struct aspath_exclude *ase);
extern void as_exclude_remove_orphan(struct aspath_exclude *ase);
extern struct aspath_exclude *as_exclude_lookup_orphan(const char *acl_name);
/* 【函数】从一条路径里**剔除**指定的 AS：
 *   aspath_filter_exclude     : 按给定的 exclude 路径剔除其中出现的 AS。
 *   aspath_filter_exclude_all : 剔除全部（等价于把路径清空），对应 exclude_all 置位。
 *   aspath_filter_exclude_acl : 按 as-path access-list 决定剔除哪些 AS（更灵活）。
 * 三者都**返回新的 aspath**（不改动入参 source），返回值的归属（intern 与否）
 * 读实现确认。 */
extern struct aspath *aspath_filter_exclude(struct aspath *source,
					    struct aspath *exclude_list);
extern struct aspath *aspath_filter_exclude_all(struct aspath *source);
extern struct aspath *aspath_filter_exclude_acl(struct aspath *source,
						struct as_list *acl_list);
/* 【函数】在路径上**添加** AS 号：
 *   aspath_add_seq_n       : 添加 num 次（num>1 就是一次做多跳前置，比循环快）。
 *   aspath_add_seq         : 添加 1 次（普通前置）。
 *   aspath_add_confed_seq  : 添加的是**联盟序列**段(AS_CONFED_SEQUENCE)——只用于
 *                            联盟内部，绝不能对外通告，别与前者用错。
 * 都返回新的 aspath，不改入参。 */
extern struct aspath *aspath_add_seq_n(struct aspath *aspath, as_t asno,
				       unsigned num);
extern struct aspath *aspath_add_seq(struct aspath *aspath, as_t asno);
extern struct aspath *aspath_add_confed_seq(struct aspath *aspath, as_t asno);
/* 【函数】aspath_cmp —— 判断两条路径**内容是否完全相同**（段类型、长度、每个
 * AS 号逐一比较），是哈希表/去重的基础。入参是 void * 因为要当哈希回调用。
 * 返回 true 表示相同。 */
extern bool aspath_cmp(const void *as1, const void *as2);
/* 【函数】按**最左（第一个）AS** 比较——不是比整条路径，而是问“这两条路径的
 * 起始 AS 是否相同”。为什么重要：BGP 选路里的“来自同一邻居(peer)”判定、
 * 以及 `bgp bestpath` 的若干比较规则都基于最左 AS。
 *   aspath_cmp_left        : 比较最左的**普通** AS。
 *   aspath_cmp_left_confed : 比较最左的**联盟** AS（联盟内部场景专用）。
 * 两者都不能替代 aspath_cmp（整体比较），用途完全不同。 */
extern bool aspath_cmp_left(const struct aspath *aspath1,
			    const struct aspath *aspath2);
extern bool aspath_cmp_left_confed(const struct aspath *as1,
				   const struct aspath *as2);
/* 【函数】移除路径里的**联盟段**——对外发送前通常要剥掉联盟路径（联盟是对内
 * 的私有结构）。返回新的 aspath。 */
extern struct aspath *aspath_delete_confed_seq(struct aspath *aspath);
/* 【函数】空路径：
 *   aspath_empty      : 按指定 asnotation 造一个**新的**空路径对象。
 *   aspath_empty_get  : 取**共享的**那个空路径单例（省内存；也因此**不要释放**
 *                       它，通常返回的是 intern 表内的对象）。
 * 本地起源的路由（network 命令发布的）就带空 AS 路径。 */
extern struct aspath *aspath_empty(enum asnotation_mode asnotation);
extern struct aspath *aspath_empty_get(void);
/* 【函数】从**字符串**（配置里写的 AS 路径）解析出 aspath 对象，是 CLI 侧的
 * 入口，与 aspath_parse（线格式入口）相对。asnotation 影响 2 字节 AS 的解析。 */
extern struct aspath *aspath_str2aspath(const char *str,
					enum asnotation_mode asnotation);
/* 【函数】aspath_str_update —— 重建缓存字符串 str（可选是否同时重建 json）。
 * **凡是改动过段内容的代码，都必须调用它刷新缓存**，否则 str 会与实际路径不一致，
 * 而 str 正是 as-path 正则匹配的依据——这会导致“策略匹配结果与显示不符”的
 * 隐蔽 bug。这是本文件里最需要记住的一致性约束之一。 */
extern void aspath_str_update(struct aspath *as, bool make_json);
/* 【函数】内存管理三件套，语义差别是本文件最容易出错的地方：
 *   aspath_free    : **无条件释放**（不理会引用计数）。只应在确认无人共享时用。
 *   aspath_intern  : 把对象放进 intern 表并返回**表内那一份**——若表里已有等价
 *                    对象，则释放入参并返回已有对象（因此返回值可能与入参不同！）
 *                    调用方必须用返回值，不能继续用原指针。
 *   aspath_unintern: 引用计数递减，减到 0 才真正释放；入参是**二级指针**，因为
 *                    释放后要把调用方的指针置空，防止悬空。
 * 记住这句：**拿到 aspath_* 的返回值就重新赋值给变量**。 */
extern void aspath_free(struct aspath *aspath);
extern struct aspath *aspath_intern(struct aspath *aspath);
extern void aspath_unintern(struct aspath **aspath);
/* 【函数】打印：aspath_print 返回一个**内部静态/复用缓冲**的字符串（不能释放、
 * 不能跨调用保存）；print_vty 直接输出到 vty。print_all_vty 打印 intern 表里
 * 全部路径（排障用）。 */
extern const char *aspath_print(struct aspath *aspath);
extern void aspath_print_vty(struct vty *vty, struct aspath *aspath);
extern void aspath_print_all_vty(struct vty *vty);
/* 【函数】aspath_key_make —— intern 哈希表的**哈希函数**，与 aspath_cmp 配对
 * （哈希找桶 + 逐个比较确认）。改哈希算法时必须同时改二者，否则表会查不到。 */
extern unsigned int aspath_key_make(const void *p);
/* 【函数】取路径两端的 AS：first = 最左边那个（我的对端 AS），last = 最右边
 * （路由起源 AS）。注意返回值是 unsigned int，路径为空时的返回值读实现确认，
 * **不要假设是 0**。 */
extern unsigned int aspath_get_first_as(struct aspath *aspath);
extern unsigned int aspath_get_last_as(struct aspath *aspath);
/* 【函数】防环检查：如果路径里（或联盟路径里）出现了给定的 AS 号，说明会成环，
 * 返回非 0。aspath_loop_check 只查普通段，aspath_loop_check_confed 查联盟段——
 * 联盟内部有单独的环检测需求，所以分成两个。 */
extern int aspath_loop_check(struct aspath *aspath, as_t asno);
extern int aspath_loop_check_confed(struct aspath *aspath, as_t asno);
/* 【函数】判断路径里**是否含私有 AS**（范围见上面的宏）。用于
 * `remove-private-AS` 决定是否有东西可删。 */
extern bool aspath_private_as_check(struct aspath *aspath);
/* 【函数】AS 号**替换**族（route-map `set as-path replace` 的底座），都返回
 * 新的 aspath，不改入参：
 *   aspath_replace_regex_asn   : 按 as-path access-list 的正则**匹配**，把匹配到
 *                                的 AS 号换成 our_asn（最灵活，也最慢）。
 *   aspath_replace_specific_asn: 只把 target_asn 换成 our_asn。
 *   aspath_replace_all_asn     : 把所有 AS 号都换成 our_asn（等价于把路径“抹平”,
 *                                慎用——会破坏防环信息）。
 * 注意“替换”与“剔除”是两回事（剔除见 aspath_filter_exclude_*）。 */
extern struct aspath *aspath_replace_regex_asn(struct aspath *aspath,
					       struct as_list *acl_list,
					       as_t our_asn);
extern struct aspath *aspath_replace_specific_asn(struct aspath *aspath,
						  as_t target_asn,
						  as_t our_asn);
extern struct aspath *aspath_replace_all_asn(struct aspath *aspath,
					     as_t our_asn);
/* 【函数】私有 AS 的处理（对外发送时的清洗动作）：
 *   aspath_replace_private_asns : 把私有 AS **替换**成 asn（通常换成自己的 AS，
 *                                 保留跳数信息）。
 *   aspath_remove_private_asns  : 把私有 AS **删除**（路径变短，影响选路）。
 *   两者都拿 peer_asn 做判断依据——因为“对端也是私有 AS 的邻居”时不应清洗，
 *   清洗规则与对端身份有关，这是容易忽略的前提。 */
extern struct aspath *aspath_replace_private_asns(struct aspath *aspath,
						  as_t asn, as_t peer_asn);
extern struct aspath *aspath_remove_private_asns(struct aspath *aspath,
						 as_t peer_asn);
/* 【函数】按位置/结构做判断：
 *   aspath_firstas_check      : 最左 AS 是否等于 asno（`neighbor ... 对端 AS 校验`
 *                               与 route-map `match as-path` 之外的常见判断）。
 *   aspath_confed_check       : 路径里**是否含**联盟段。
 *   aspath_left_confed_check  : 路径**最左**是否是联盟段（联盟边界判断）。
 *   含联盟与最左是联盟，这两个语义不同，别混用。 */
extern bool aspath_firstas_check(struct aspath *aspath, as_t asno);
extern bool aspath_confed_check(struct aspath *aspath);
extern bool aspath_left_confed_check(struct aspath *aspath);
/* 【函数】统计：aspath_count 返回 intern 表里的路径**总数**（无参数，是全局统计，
 * 与“某条路径的跳数”完全无关，别被名字误导）。 */
extern unsigned long aspath_count(void);
/* 【函数】aspath_count_hops —— 算“跳数”：只数**普通段**，联盟段不计入。
 * 之所以强调，是因为它直接决定 BGP 选路的“AS 路径长度”，算错就会选错路。
 * 特别地，AS_SET 通常按 **1 跳**计（集合不代表顺序）。 */
extern unsigned int aspath_count_hops(const struct aspath *aspath);
/* 【函数】格式合法性检查：
 *   aspath_check_as_sets : 是否含 AS_SET（聚合过的路径通常含它；某些策略会据此过滤）。
 *   aspath_check_as_zero : 是否含 0 号 AS（协议上非法，收到即为异常，RFC 7606 的
 *                          处理分支会用到）。 */
extern bool aspath_check_as_sets(struct aspath *aspath);
extern bool aspath_check_as_zero(struct aspath *aspath);
/* 【函数】其他度量：
 *   aspath_count_confeds : 数**联盟段**里的 AS 数（与 count_hops 的统计口径互补）。
 *   aspath_size          : 路径的“元素个数”口径（具体定义读实现，常用于属性长度估算）。
 *   aspath_highest       : 路径里**最大**的 AS 号。
 *   aspath_leftmost      : 最左 AS（与 aspath_get_first_as 相近但**返回类型/空路径
 *                          行为可能不同**，读实现确认，别随手互相替换）。 */
extern unsigned int aspath_count_confeds(struct aspath *aspath);
extern unsigned int aspath_size(struct aspath *aspath);
extern as_t aspath_highest(struct aspath *aspath);
extern as_t aspath_leftmost(struct aspath *aspath);
/* 【函数】aspath_put —— 把 AS_PATH **编码成线格式**写入 stream，是 aspath_parse
 * 的逆操作。use32bit 决定用 2 字节还是 4 字节 AS 编码，**必须与对端协商能力一致**
 * （否则对端解析错位）。返回写入的字节数，用于回填属性长度。 */
extern size_t aspath_put(struct stream *s, struct aspath *aspath, int use32bit);

/* 【函数】BGP_AS_TRANS(23456) 过渡期的**对账**（RFC 6793）：
 *   aspath_reconcile_as4 : 把“AS_PATH 里那些 23456 占位”用 **AS4_PATH** 里的真值
 *       替换掉，得到真实路径。为什么需要——老邻居只发 2 字节 AS_PATH，4 字节 AS
 *       被塞进 AS4_PATH 属性；两者要按 AS 号个数对齐合并，否则路径就错了。
 *   aspath_has_as4       : 判断路径里是否含 4 字节 AS（决定是否需要发 AS4_PATH）。
 * 这两个是“AS4 平滑迁移”的核心，读实现时注意长度不一致时的裁剪规则。 */
extern struct aspath *aspath_reconcile_as4(struct aspath *aspath,
					   struct aspath *as4path);
extern bool aspath_has_as4(struct aspath *aspath);

/* For SNMP BGP4PATHATTRASPATHSEGMENT, might be useful for debug */
/* 【函数】把 AS_PATH 编码成 SNMP MIB(BGP4PATHATTRASPATHSEGMENT) 需要的字节序列。
 * varlen 是**出参**，回填编码后的长度。返回值指向的内存的归属读实现确认
 * （大概率是调用方需释放或复用缓冲）。 */
extern uint8_t *aspath_snmp_pathseg(struct aspath *aspath, size_t *varlen);

/* 【函数】聚合(aggregate)相关的 AS 路径统计——目的是给聚合路由算出一条**最合适
 * 的 AS_PATH**（在多个被聚合成员之间取公共部分 / 最长 / 最短等口径）。
 *   bgp_compute_aggregate_aspath / _hash : 把一条成员路径的信息并进聚合统计。
 *   bgp_compute_aggregate_aspath_val     : 结算，算出最终用于聚合路由的路径。
 *   bgp_remove_aspath_from_aggregate(_hash) : 成员撤销时从统计里摘除。
 * 这一族的难点是“**加进来**与**拿出去**必须对称”，否则聚合路径会随着成员增删
 * 缓慢漂移——那类 bug 极难复现，读代码时重点看摘除逻辑是否覆盖了所有分支。 */
extern void bgp_compute_aggregate_aspath(struct bgp_aggregate *aggregate,
					 struct aspath *aspath);

extern void bgp_compute_aggregate_aspath_hash(struct bgp_aggregate *aggregate,
					      struct aspath *aspath);
extern void bgp_compute_aggregate_aspath_val(struct bgp_aggregate *aggregate);
extern void bgp_remove_aspath_from_aggregate(struct bgp_aggregate *aggregate,
					     struct aspath *aspath);
extern void bgp_remove_aspath_from_aggregate_hash(
						struct bgp_aggregate *aggregate,
						struct aspath *aspath);

/* 【函数】聚合体的清理回调（`void *arg` 是为了当通用释放回调用）。 */
extern void bgp_aggr_aspath_remove(void *arg);
/* 【函数】aspath_delete_as_set_seq —— 把路径里的 **AS_SET 段转成 AS_SEQUENCE**
 * （或等价地移除集合语义）。之所以需要：有些对端/策略不支持 AS_SET，必须“展开”
 * 成序列才能发出去。返回新的 aspath。 */
extern struct aspath *aspath_delete_as_set_seq(struct aspath *aspath);

#endif /* _QUAGGA_BGP_ASPATH_H */
