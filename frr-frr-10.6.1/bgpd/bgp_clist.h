// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP Community list.
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

#ifndef _QUAGGA_BGP_CLIST_H
#define _QUAGGA_BGP_CLIST_H

#include "jhash.h"

/* ============================================================================
 * 【文件导读】bgp_clist.h —— community-list（团体过滤器）
 * ============================================================================
 * 【它是什么】route-map 里 `match community / extcommunity / large-community` 的
 * 底座，对应配置 `ip community-list WORD permit 65001:100` 这类命令（还有
 * community-list / extcommunity-list / large-community-list 三套并行命令）。
 * 每个 list 里有多条条目，条目各自带序列号与 permit / deny，**按 seq 升序匹配、
 * 第一条命中即定论**（与 route-map、as-path filter 完全同一套语义）。
 *
 * 【两个正交的维度（必须先分清楚，否则看不懂那堆宏）】
 *   维度一 · 团体的种类(MASTER)：standard community / extcommunity /
 *       large-community 三种，各自有独立的 list 命名空间与匹配函数族。
 *   维度二 · 条目的写法(style)：**standard** 与 **expanded** 两种：
 *       standard 是“列出具体团体值，要求精确出现”；
 *       expanded 是“写**正则表达式**，去匹配团体的**字符串形式**”。
 *       因此 expanded 条目的匹配依赖团体转字符串的写法（与 as-path 正则同理），
 *       是个容易踩的坑。
 *   两个维度相乘得到 6 种 style 常量（见下面 COMMUNITY_LIST_STANDARD 等）——
 *   这组常量的编号就是"种类×写法"的打包，**不是**随便排的数组下标。
 *
 * 【另一种分法：编号 vs 命名】community-list 支持
 *   `ip community-list 1 permit …`（数字名，1~500）与
 *   `ip community-list NAME permit …`（字符串名）两种写法，于是每个 master 里
 *   同时维护 **num 与 str 两张链表**，再加一张 **hash** 用于按名字快速查找
 *   （名字查找是高频操作：route-map 每次匹配都要按名字找 list）。
 *
 * 【返回值约定（容易误用）】community_list_*_match 系列返回 **bool**，语义是
 * “匹配上该 list 的规则了”——但**并不等于“该 permit”**！因为条目本身还带
 * direct(permit/deny)，所以“匹配上了一条 deny 条目”同样返回 true。判断最终
 * 放行与否必须结合调用处对 direct 的处理，或使用 exact_match / any_match 这类
 * 语义更明确的变体。读调用点时务必确认它用的是哪一种。
 *
 * 【阅读建议】几个 MASTER / style / direct 宏 → struct community_list（一个列表）
 *            → struct community_entry（一条规则）→ struct community_list_handler
 *            （三个 master）→ 成对的 set/unset（配置）→ 成组的 match 函数。
 *            实现在 bgp_clist.c。
 * ============================================================================
 */

/* Master Community-list. */
/* 【宏】三个“类别”（文件头导读里的维度一）：标准团体 / 扩展团体 / 大团体。
 * 它们被用来索引 community_list_handler 里的三个 master，所以取值 0/1/2 是
 * **数组下标语义**，不能随意调整顺序。 */
#define COMMUNITY_LIST_MASTER          0
#define EXTCOMMUNITY_LIST_MASTER       1
#define LARGE_COMMUNITY_LIST_MASTER    2

/* Community-list deny and permit.  */
/* 【宏】条目的动作。注意 **DENY 是 0**——这意味着“未初始化”的条目默认就是 deny，
 * 属于**失败即拒绝**的安全默认，但也意味着 memset 清空后忘了设 direct 会把本该
 * permit 的规则变成 deny。与 as_filter_type 的枚举也是同一个取值习惯。 */
#define COMMUNITY_DENY                 0
#define COMMUNITY_PERMIT               1

/* Number and string based community-list name.  */
/* 【宏】list 的命名方式：字符串名 / 数字名。同样作为索引/判别使用。 */
#define COMMUNITY_LIST_STRING          0
#define COMMUNITY_LIST_NUMBER          1
/* The numbered community-list (including large/ext communities)
 * have a range between 1-500.
 */
/* 【宏】数字名的上界。注意原注释说 large/ext 也共用这个 1~500 的范围，
 * 且**下界是 1 而不是 0**（0 被上面的 COMMUNITY_LIST_STRING 占用，但从命名
 * 维度看 0 恰好也表示“字符串名”，两套 0 的含义不同，不要串）。 */
#define COMMUNITY_LIST_NUMBER_MAX 500

/* 【宏】省略序列号时的哨兵值，表示“自动分配”。与 as-path filter 的
 * ASPATH_SEQ_NUMBER_AUTO 是同一套路（值都是 -1），所以 seq 字段类型是 int64_t。 */
#define COMMUNITY_SEQ_NUMBER_AUTO     -1

/* Community-list entry types.  */
/* 【宏】条目的“种类 × 写法”六种组合（文件头导读的维度二）。命名规律：
 *   前缀 COMMUNITY_LIST_ / EXTCOMMUNITY_LIST_ / LARGE_COMMUNITY_LIST_ 表示种类；
 *   后缀 _STANDARD / _EXPANDED 表示写法。
 *   它们是**扁平编号 0~5**，可作为数组下标，但注意 0 是“standard community 的
 *   standard 写法”——0 在这里是**有效值**而不是“未设置”，用 0 判空会出错。 */
#define COMMUNITY_LIST_STANDARD        0 /* Standard community-list.  */
#define COMMUNITY_LIST_EXPANDED        1 /* Expanded community-list.  */
#define EXTCOMMUNITY_LIST_STANDARD     2 /* Standard extcommunity-list.  */
#define EXTCOMMUNITY_LIST_EXPANDED     3 /* Expanded extcommunity-list.  */
#define LARGE_COMMUNITY_LIST_STANDARD  4 /* Standard Large community-list.  */
#define LARGE_COMMUNITY_LIST_EXPANDED  5 /* Expanded Large community-list.  */

/* Community-list.  */
/* 【结构】community_list —— **一个**命名的团体过滤器（对应配置里一条
 * `ip community-list WORD …` 的 WORD）。它不是单条规则，而是规则的集合。 */
struct community_list {
	/* Name of the community-list.  */
	char *name;

	/* Stored hash value of name, to further speed up hash operations */
	uint32_t name_hash;

	/* String or number.  */
	int sort;

	/* Link to upper list.  */
	struct community_list_list *parent;

	/* Linked list for other community-list.  */
	struct community_list *next;
	struct community_list *prev;

	/* Community-list entry in this community-list.  */
	struct community_entry *head;
	struct community_entry *tail;
};

/* Each entry in community-list.  */
/* 【结构】community_entry —— 列表里的**一条规则**（`… permit 65001:100` 这一行）。
 *   next / prev  : 同一列表内的规则链表（**顺序即 seq 顺序**，见下）。
 *   direct       : 动作，取 COMMUNITY_PERMIT / COMMUNITY_DENY（不是 bool！）。
 *   style        : 写法（standard / expanded）——决定 u 里用哪种团体结构、以及
 *                  匹配时走“值比较”还是“正则”。
 *   seq          : 匹配次序（升序，先命中先定论），可为 -1(AUTO)。
 *   u            : 三种团体结构体的 union——**用 style 判别该取哪一个**。
 *                  为什么用 union：一条规则只可能是三种之一，共享内存更省。
 *                  访问时**必须先看 style**，取错了读到的是别种团体的内存布局，
 *                  编译器不会报错（这正是 union 的固有风险）。
 *   config       : 用户写的那段配置原文，用于 `show running-config` 原样回显。
 *   reg          : expanded 写法下编译出来的正则对象（standard 写法时不用）。
 *                  与 config 的关系同 as-path filter：config 用于回显、reg 用于匹配。
 * 注意结构里**同时存在** config 与 reg、以及 union 的三个成员，这些都是“按
 * style 二选一”的冗余字段，不是每个都有效。 */
struct community_entry {
	struct community_entry *next;
	struct community_entry *prev;

	/* Permit or deny.  */
	uint8_t direct;

	/* Standard or expanded.  */
	uint8_t style;

	/* Sequence number. */
	int64_t seq;

	/* Community structure.  */
	union {
		struct community *com;
		struct ecommunity *ecom;
		struct lcommunity *lcom;
	} u;

	/* Configuration string.  */
	char *config;

	/* Expanded community-list regular expression.  */
	struct frregex *reg;
};

/* Linked list of community-list.  */
/* 【结构】community_list_list —— 一个列表链表的头尾对。被 master 用两次：一次是
 * “数字名列表的链表”、一次是“字符串名列表的链表”，因此**同一个类型出现两份**。 */
struct community_list_list {
	struct community_list *head;
	struct community_list *tail;
};

/* Master structure of community-list and extcommunity-list.  */
/* 【结构】community_list_master —— **一类**团体的全部列表容器（见文件头导读：
 * 编号容器 + 命名容器 + 哈希索引三件套）。
 *   num  : 数字名（1~500）的列表链表。为什么还要链表——回显配置时要有稳定顺序。
 *   str  : 字符串名的列表链表。
 *   hash : 按名字的哈希索引，用于**查找**（链表负责顺序、哈希负责速度，两者是
 *          同一批数据的两种组织，节点是同一批对象）。
 * 注意 num 与 str 是**两个独立的命名空间**：名叫 "1" 的字符串列表与数字列表 1
 * 是不同的东西，查找时必须带上 master 与 sort 才能定位对。 */
struct community_list_master {
	struct community_list_list num;
	struct community_list_list str;
	struct hash *hash;
};

/* Community-list handler.  community_list_init() returns this
   structure as handler.  */
/* 【结构】community_list_handler —— 全局句柄，**把三类团体的 master 打包在一起**。
 * 初始化函数返回的就是它。所有 set/unset/lookup 的第一个参数都是它（或它的某个
 * master），这种“句柄传递”风格让同一套代码能服务三类团体，代价是调用点必须
 * 传对那一个。 */
struct community_list_handler {
	/* Community-list.  */
	struct community_list_master community_list;

	/* Extended community-list.  */
	struct community_list_master extcommunity_list;

	/* Large community-list.  */
	struct community_list_master lcommunity_list;
};

/* Error code of community-list.  */
/* 【错误码】配置阶段的失败原因，注意**全为负数**（-1/-2/-3），所以判断写法是
 * `< 0` 而不是 `!= 0`；同时 0 保留给“成功”。这三个值的含义从名字可读：
 *   值格式非法 / 与已有 standard 条目冲突 / 与已有 expanded 条目冲突。
 * 最后一条值得注意：在同一 list 里**混用 standard 与 expanded 写法是不允许的**，
 * 配置时就被拦下（所以才有这两个 conflict 错误码）。 */
#define COMMUNITY_LIST_ERR_MALFORMED_VAL     -1
#define COMMUNITY_LIST_ERR_STANDARD_CONFLICT -2
#define COMMUNITY_LIST_ERR_EXPANDED_CONFLICT -3
/* Handler.  */
/* 【全局】bgpd 进程唯一的 community-list 句柄（三类团体的总入口）。 */
extern struct community_list_handler *bgp_clist;

/* Prototypes.  */
/* 【函数】生命周期：init 建好三类团体的容器并**返回句柄**（调用方需把它保存，
 * 通常会赋给上面的 bgp_clist）；terminate 全部释放。 */
extern struct community_list_handler *community_list_init(void);
extern void community_list_terminate(struct community_list_handler *ch);

/* 【函数】三种团体的配置 set/unset（三套并行，形状完全一样，所以只讲一遍）：
 *   ch     : 句柄；name : 列表名（数字或字符串）；str : 本条规则的内容
 *            （standard 写具体团体值，expanded 写正则源串）。
 *   seq    : 序列号**字符串**形式——因为要区分“用户没写”与“写了 0”，所以传
 *            字符串而不是整数（未写时传 NULL，由实现按 AUTO 处理）。
 *   direct : permit / deny。
 *   style  : standard / expanded。
 * 注意 set 返回 int **错误码**（见上面的 -1/-2/-3），调用方应当检查；而 unset
 * 返回 void，**失败是无提示的**（删了一条不存在的规则不会报错），排障时要知道
 * 这一点。三套函数的差别只在于操作哪个 master。
 * lcommunity_list_valid 是配置**校验**辅助：判断给定的 large community 字符串
 * 在指定 style 下是否合法，供 CLI 在真正写入前拒绝坏输入。 */
extern int community_list_set(struct community_list_handler *ch,
			      const char *name, const char *str,
			      const char *seq, int direct, int style);
extern void community_list_unset(struct community_list_handler *ch,
				 const char *name, const char *str,
				 const char *seq, int direct, int style);
extern int extcommunity_list_set(struct community_list_handler *ch,
				 const char *name, const char *str,
				 const char *seq, int direct, int style);
extern void extcommunity_list_unset(struct community_list_handler *ch,
				    const char *name, const char *str,
				    const char *seq, int direct, int style);
extern int lcommunity_list_set(struct community_list_handler *ch,
			       const char *name, const char *str,
			       const char *seq, int direct, int style);
extern bool lcommunity_list_valid(const char *community, int style);
extern void lcommunity_list_unset(struct community_list_handler *ch,
				  const char *name, const char *str,
				  const char *seq, int direct, int style);

/* 【函数】两个查找，分工不同：
 *   community_list_master_lookup : 按 master 编号取出**三类之一**的容器
 *        （返回的是 handler 内的地址，不要释放）。
 *   community_list_lookup        : 在指定 master 里按名字找**具体的 list**。
 *        注意它额外要一个 `name_hash` 参数——是为了避免每次都重算哈希（调用点
 *        通常已经有这个值），如果传错哈希会**查不到**（哈希表按值找桶），这是个
 *        不容易发现的错误，务必用 bgp_clist_hash_key 算出的值。 */
extern struct community_list_master *
community_list_master_lookup(struct community_list_handler *ch, int master);

extern struct community_list *
community_list_lookup(struct community_list_handler *c, const char *name,
		      uint32_t name_hash, int master);

/* 【函数】匹配函数族（三套 × 四种语义）。**四种语义的差别是本文件最需要分清的地方**：
 *   ..._match        : 常规匹配——判断该团体的**整体**是否命中列表规则。返回值
 *                      同时体现了条目里的 permit / deny 结论（因此它常被直接当成
 *                      “放行 / 拒绝”的答案）。
 *   ..._exact_match  : **全等**匹配——要求传入的团体集合与列表条目**完全一致**
 *                      （多一个少一个都不算），而不是“包含其中某个”。
 *                      典型用途是精确匹配某组团体，防止误伤。
 *   ..._any_match    : **任一命中**即返回 true——只要传入团体里任意一个出现在
 *                      列表中就成立，适合“有没有打这个标”这类快速判断。
 *   ..._match_delete : 不是查询而是**变换**：返回一个**新的团体集合**，把命中的
 *                      那些团体删掉。用于 route-map 的 `set comm-list ... delete`
 *                      ——“把列表里列出的这些团体从路由上摘掉”。
 *                      注意它**返回新对象**，调用方要负责归属（原注释未说明，
 *                      以实现为准），且它是本组里唯一会新分配内存的。
 * 三套（community / ecommunity / lcommunity）同名同形，只是操作的团体类型不同；
 * 用错套类型编译期就会报错，相对安全；真正的风险在于**选错语义**（match 与
 * exact_match、any_match 的混淆），它们的签名往往只差一个词。 */
extern bool community_list_match(struct community *com,
				 struct community_list *list);
extern bool ecommunity_list_match(struct ecommunity *ecom,
				  struct community_list *list);
extern bool lcommunity_list_match(struct lcommunity *lcom,
				  struct community_list *list);
extern bool community_list_exact_match(struct community *com,
				       struct community_list *list);
extern bool ecommunity_list_exact_match(struct ecommunity *com, struct community_list *list);
extern bool lcommunity_list_exact_match(struct lcommunity *lcom,
					struct community_list *list);
extern bool community_list_any_match(struct community *com,
				     struct community_list *list);
extern struct community *
community_list_match_delete(struct community *com, struct community_list *list);
extern bool lcommunity_list_any_match(struct lcommunity *lcom,
				      struct community_list *list);
extern bool ecommunity_list_any_match(struct ecommunity *ecom, struct community_list *list);
extern struct lcommunity *
lcommunity_list_match_delete(struct lcommunity *lcom,
			     struct community_list *list);
extern struct ecommunity *
ecommunity_list_match_delete(struct ecommunity *ecom,
			     struct community_list *list);

/* 【函数】计算列表名的哈希值（供 community_list_lookup 复用）。用 jhash 且带一个
 * **固定的魔数种子 0xdeadbeaf**——种子固定是为了让同一进程内结果稳定；这也意味着
 * 跨进程/跨版本的哈希值不可比（不要把它持久化或与其他实现对比）。
 * 内联放在头文件里是为了让调用点随手可用，属于性能考量。 */
static inline uint32_t bgp_clist_hash_key(char *name)
{
	return jhash(name, strlen(name), 0xdeadbeaf);
}

/* 【函数】把已配置的 community-list 名字注册为 CLI 补全候选（Tab 提示）。
 * 列表增删后需重新调用刷新。 */
extern void bgp_community_list_command_completion_setup(void);

#endif /* _QUAGGA_BGP_CLIST_H */
