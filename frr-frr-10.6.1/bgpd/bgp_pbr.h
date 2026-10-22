// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BGP pbr
 * Copyright (C) 6WIND
 */
/*
 * 【文件导读】bgp_pbr.h —— BGP PBR (Policy Based Routing) 的接口与内部抽象定义
 *
 * 一、这个文件解决什么问题
 *   FlowSpec (RFC 5575) 允许 BGP 邻居用 NLRI 表达"匹配什么样的流量 + 对匹配到的流量做什么"。
 *   bgpd 收到这类规则后，必须把它翻译成 zebra / 内核能执行的策略路由条目
 *   (ipset 集合 + iptables 规则 + ip rule 策略)。本头文件定义这套翻译层所用的
 *   全部数据结构与对外函数原型；真正干活的是同目录下的 bgp_pbr.c。
 *
 * 二、为什么这样设计
 *   - 把"匹配"和"动作"拆成两个正交维度：match 描述流量特征，action 描述对命中流量做什么。
 *   - 规则在内部会被去重与共享：多条 FlowSpec 可能落到同一条策略条目上，
 *     因此大量结构带 unique 编号和 refcnt 引用计数，并与 zebra 侧以 unique 做请求-回执配对。
 *   - 同时服务两类来源：FlowSpec 生成的 ipsets (type = BGP_PBR_IPSET)，
 *     以及本机 ip rule 策略 (type = BGP_PBR_IPRULE)。二者共用入口但下游路径不同。
 *
 * 三、关键数据结构与推荐阅读顺序
 *   1) struct bgp_pbr_match_val    一个"匹配值 + 比较符 + 逻辑连接符"的最小单元
 *   2) struct bgp_pbr_entry_action 单条动作(重定向 / 限速 / 打标 / 丢弃等)，union 承载各动作参数
 *   3) struct bgp_pbr_entry_main   一条完整规则的临时视图：match 部分 + action 数组 + vrf/afi
 *   4) struct bgp_pbr_action       动作的归一化对象(fwmark / table_id / nexthop)，被多条规则共享
 *   5) struct bgp_pbr_rule         IP rule 策略条目，指向一个 bgp_pbr_action
 *   6) struct bgp_pbr_match        ipset 集合(匹配集合)，内部含 entry_hash
 *   7) struct bgp_pbr_match_entry  ipset 集合里的单条匹配项
 *   8) struct bgp_pbr_interface    接口策略用的红黑树节点，由 bgp_pbr_config 持有
 *
 * 四、对外主入口
 *   bgp_pbr_update_entry() 由路由(NLRI)变化驱动，本文件只声明，实现在 bgp_pbr.c。
 */
#ifndef __BGP_PBR_H__
#define __BGP_PBR_H__

#include "nexthop.h"
#include "zclient.h"

/*
 * 动作(action)维度：一条 FlowSpec 规则最多携带的动作数。
 * 每个动作由 enum bgp_pbr_action_enum 取一个值，承载在 bgp_pbr_entry_action 里。
 *   1 redirect       重定向到 VRF 或下一跳
 *   1 set dscp       改写 DSCP / 流标签
 *   1 set traffic rate 限速(rate=0 时表示丢弃)
 * 真实上限 4，为将来扩展留了余量。
 */
/* flowspec case: 0 to 3 actions maximum:
 * 1 redirect
 * 1 set dscp
 * 1 set traffic rate
 */
#define ACTIONS_MAX_NUM 4
/*
 * 动作类型；值被 bgp_pbr_entry_action.action 使用。
 *   ACTION_TRAFFICRATE   限速 / rate=0 表示丢弃(drop)
 *   ACTION_TRAFFIC_ACTION 流量动作(sample / terminate / distribute)
 *   ACTION_REDIRECT      重定向到某个 VRF
 *   ACTION_MARKING       改写 DSCP / 流标签
 *   ACTION_REDIRECT_IP   重定向到某个 IP 下一跳
 */
enum bgp_pbr_action_enum {
	ACTION_TRAFFICRATE = 1,
	ACTION_TRAFFIC_ACTION = 2,
	ACTION_REDIRECT = 3,
	ACTION_MARKING = 4,
	ACTION_REDIRECT_IP = 5
};

/*
 * 流量动作子标志；被 bgp_pbr_entry_action.u.za.filter 使用。
 *   SAMPLE     采样
 *   TERMINATE  终止(应用过滤器)
 *   DISTRIBUTE 分发
 */
#define TRAFFIC_ACTION_SAMPLE     (1 << 0)
#define TRAFFIC_ACTION_TERMINATE  (1 << 1)
#define TRAFFIC_ACTION_DISTRIBUTE (1 << 2)

/*
 * 比较运算符；被 bgp_pbr_match_val.compare_operator 使用。
 *   LESS_THAN    < value
 *   GREATER_THAN > value
 *   EQUAL_TO     = value
 *   EXACT_MATCH  精确匹配
 * 注意它们可以按位叠加，例如 GREATER_THAN 加 EQUAL_TO 表示"大于等于"。
 */
#define OPERATOR_COMPARE_LESS_THAN    (1<<1)
#define OPERATOR_COMPARE_GREATER_THAN (1<<2)
#define OPERATOR_COMPARE_EQUAL_TO     (1<<3)
#define OPERATOR_COMPARE_EXACT_MATCH  (1<<4)

/*
 * 一元逻辑运算符；被 bgp_pbr_match_val.unary_operator 使用，连接同一列表内的多个匹配值。
 *   UNARY_OR  OR 连接
 *   UNARY_AND AND 连接
 * 二者同时出现时表示混合连接(实现里会做特殊处理或判定为不支持)。
 */
#define OPERATOR_UNARY_OR    (1<<1)
#define OPERATOR_UNARY_AND   (1<<2)

/*
 * 单个匹配值单元：一个数值 + 一个比较运算符 + 一个逻辑连接符。
 * 例如 FlowSpec 的 "port >= 80 && port <= 90" 会被拆成两个 bgp_pbr_match_val。
 *   value            被比较的数值(端口 / 协议号 / DSCP 等，0 到 65535)
 *   compare_operator 取自 OPERATOR_COMPARE_ 系列
 *   unary_operator   取自 OPERATOR_UNARY_ 系列，决定与前一单元是 AND 还是 OR
 * 一个字段(如 src_port)最多允许 BGP_PBR_MATCH_VAL_MAX 个单元。
 */
/* struct used to store values [0;65535]
 * this can be used for port number of protocol
 */
#define BGP_PBR_MATCH_VAL_MAX 5

struct bgp_pbr_match_val {
	uint16_t value;
	uint8_t compare_operator;
	uint8_t unary_operator;
};

/*
 * Fragment 匹配标志位取值；对应 FlowSpec fragment 组件的位掩码。
 *   DONT  1 不分片(dont fragment)
 *   IS    2 是分片
 *   FIRST 4 首片
 *   LAST  8 末片
 */
#define FRAGMENT_DONT  1
#define FRAGMENT_IS    2
#define FRAGMENT_FIRST 4
#define FRAGMENT_LAST  8

/*
 * 单条动作。
 *   action 取自 enum bgp_pbr_action_enum，决定 union u 里哪个成员有效。
 *   union u 各成员与动作的对应关系：
 *     u.r   限速参数(ACTION_TRAFFICRATE)，rate 用 IEEE.754.1985 浮点编码
 *     u.za  流量动作(ACTION_TRAFFIC_ACTION)，do_sample / filter
 *     u.redirect_vrf      重定向目标 VRF(ACTION_REDIRECT)
 *     u.zr  重定向目标 IP(ACTION_REDIRECT_IP)，v4 / v6 及 duplicate 副本标志
 *     u.marking_dscp      改写目标 DSCP / 流标签(ACTION_MARKING)
 *   union 上标了 aligned(8)，保证浮点成员对齐。
 */
struct bgp_pbr_entry_action {
	/* used to store enum bgp_pbr_action_enum enumerate */
	uint8_t action;
	union {
		union {
			uint8_t rate_info[4]; /* IEEE.754.1985 */
			float rate;
		} r __attribute__((aligned(8)));
		struct _pbr_action {
			uint8_t do_sample;
			uint8_t filter;
		} za;
		vrf_id_t redirect_vrf;
		struct _pbr_redirect_ip {
			struct in_addr redirect_ip_v4;
			struct in6_addr redirect_ip_v6;
			uint8_t duplicate;
		} zr;
		uint8_t marking_dscp;
	} u __attribute__((aligned(8)));
};

/*
 * 一条规则的临时完整视图，是"外部输入 -> 内部结构"的中间层。
 * 由 bgp_pbr_build_and_validate_entry() 填充后交给 bgp_pbr_handle_entry() 消费。
 *
 * 分层：
 *   match 部分  —— 描述"匹配什么流量"：
 *     type            规则来源：BGP_PBR_IPSET(FlowSpec) 或 BGP_PBR_IPRULE(本机 ip rule)
 *     match_bitmask   哪些字段被匹配(src/dst 前缀、协议、端口、tcpflags 等各占一位)
 *     match_*_num     对应 *_[] 数组里实际有效的元素个数
 *     src/dst_prefix  源 / 目的前缀；src/dst_prefix_offset 为该实现暂不支持的偏移
 *     各类 *_[]       使用 bgp_pbr_match_val 表达的匹配值数组
 *   action 部分 —— 描述"对匹配到的流量做什么"：
 *     action_num      动作个数
 *     actions[]       动作数组，每个 bgp_pbr_entry_action
 *   vrf_id / afi / safi 规则所处地址族与 VRF。
 *
 * 注意 match_bitmask_iprule 是 ip rule 专属的 src/dst 存在位，与 match_bitmask 分开。
 */
/* BGP Policy Route structure */
struct bgp_pbr_entry_main {
#define BGP_PBR_UNDEFINED	0
#define BGP_PBR_IPSET		1
#define BGP_PBR_IPRULE		2
	uint8_t type;

	/*
	 * This is an enum but we are going to treat it as a uint8_t
	 * for purpose of encoding/decoding
	 */
	afi_t afi;
	safi_t safi;

#define PREFIX_SRC_PRESENT (1 << 0)
#define PREFIX_DST_PRESENT (1 << 1)
	uint8_t match_bitmask_iprule;
	uint8_t match_bitmask;

	uint8_t match_src_port_num;
	uint8_t match_dst_port_num;
	uint8_t match_port_num;
	uint8_t match_protocol_num;
	uint8_t match_icmp_type_num;
	uint8_t match_icmp_code_num;
	uint8_t match_packet_length_num;
	uint8_t match_dscp_num;
	uint8_t match_tcpflags_num;
	uint8_t match_fragment_num;
	uint8_t match_flowlabel_num;

	struct prefix src_prefix;
	struct prefix dst_prefix;
	uint8_t src_prefix_offset;
	uint8_t dst_prefix_offset;

#define PROTOCOL_UDP 17
#define PROTOCOL_TCP 6
#define PROTOCOL_ICMP 1
#define PROTOCOL_ICMPV6 58
	struct bgp_pbr_match_val protocol[BGP_PBR_MATCH_VAL_MAX];
	struct bgp_pbr_match_val src_port[BGP_PBR_MATCH_VAL_MAX];
	struct bgp_pbr_match_val dst_port[BGP_PBR_MATCH_VAL_MAX];
	struct bgp_pbr_match_val port[BGP_PBR_MATCH_VAL_MAX];
	struct bgp_pbr_match_val icmp_type[BGP_PBR_MATCH_VAL_MAX];
	struct bgp_pbr_match_val icmp_code[BGP_PBR_MATCH_VAL_MAX];
	struct bgp_pbr_match_val packet_length[BGP_PBR_MATCH_VAL_MAX];
	struct bgp_pbr_match_val dscp[BGP_PBR_MATCH_VAL_MAX];
	struct bgp_pbr_match_val flow_label[BGP_PBR_MATCH_VAL_MAX];

	struct bgp_pbr_match_val tcpflags[BGP_PBR_MATCH_VAL_MAX];
	struct bgp_pbr_match_val fragment[BGP_PBR_MATCH_VAL_MAX];

	uint16_t action_num;
	struct bgp_pbr_entry_action actions[ACTIONS_MAX_NUM];

	vrf_id_t vrf_id;
};

/*
 * 接口策略节点：记录一个被 PBR 策略引用的接口名，挂在红黑树里按名字查找。
 * id_entry 为 RB 树挂接点，name 为接口名(如 eth0)。
 */
struct bgp_pbr_interface {
	RB_ENTRY(bgp_pbr_interface) id_entry;
	char name[IFNAMSIZ];
};

RB_HEAD(bgp_pbr_interface_head, bgp_pbr_interface);
RB_PROTOTYPE(bgp_pbr_interface_head, bgp_pbr_interface, id_entry,
	     bgp_pbr_interface_compare);

extern int bgp_pbr_interface_compare(const struct bgp_pbr_interface *a,
				     const struct bgp_pbr_interface *b);

/*
 * BGP 实例上的 PBR 全局配置。
 *   ifaces_by_name_ipv4 / ifaces_by_name_ipv6 两个接口红黑树，分别对应 v4 / v6 策略。
 *   pbr_interface_any_ipv4 / pbr_interface_any_ipv6 表示是否接受"任意接口"(通配)。
 */
struct bgp_pbr_config {
	struct bgp_pbr_interface_head ifaces_by_name_ipv4;
	bool pbr_interface_any_ipv4;
	struct bgp_pbr_interface_head ifaces_by_name_ipv6;
	bool pbr_interface_any_ipv6;
};

/*
 * IP rule 策略条目(对应内核 ip rule / ip route)，是 BGP_PBR_IPRULE 类型规则的下游形态。
 *   flags               MATCH_IP_SRC_SET / MATCH_IP_DST_SET 等，指示 src/dst 是否参与匹配
 *   src / dst           匹配用的源 / 目的前缀
 *   action              指向共享的 bgp_pbr_action(决定动作)
 *   vrf_id              所属 VRF
 *   unique              唯一编号，用于与 zebra 的请求-回执配对
 *   priority            内核 ip rule 的优先级
 *   installed           是否已下发到 zebra / 内核
 *   install_in_progress 是否正在下发中，防止重复
 *   path                反向指回触发它的 bgp_path_info(用于撤销时解链)
 */
struct bgp_pbr_rule {
	uint32_t flags;
	struct prefix src;
	struct prefix dst;
	struct bgp_pbr_action *action;
	vrf_id_t vrf_id;
	uint32_t unique;
	uint32_t priority;
	bool installed;
	bool install_in_progress;
	void *path;
};

/*
 * 一个 ipset 集合(匹配集合)，是 FlowSpec 规则的下游形态之一。
 * 同一个集合内可以有多个匹配项(bgp_pbr_match_entry)，用 entry_hash 组织。
 *   ipset_name   集合名(下发到 zebra 时使用)
 *   type         集合类型，取值见 enum ipset_type(如 IPSET_NET / IPSET_NET_PORT 等)
 *   flags        MATCH_* 位掩码，说明哪些匹配字段有效
 *   family       地址族(AF_INET / AF_INET6)
 *   各种匹配字段 pkt_len / tcp_flags / dscp / fragment / protocol / flow_label 等
 *   vrf_id       所属 VRF
 *   unique       与 zebra 做 ipset 创建事务配对的编号
 *   unique2      与 zebra 做 iptable 添加事务配对的编号
 *   installed                集合是否已创建
 *   install_in_progress      创建是否进行中
 *   installed_in_iptable     相关 iptables 规则是否已添加
 *   install_iptable_in_progress iptables 添加是否进行中
 *   entry_hash   本集合内匹配项的哈希表
 *   action       本集合关联的动作(引用一个 bgp_pbr_action)
 */
struct bgp_pbr_match {
	char ipset_name[ZEBRA_IPSET_NAME_SIZE];

	/* mapped on enum ipset_type
	 */
	uint32_t type;

	uint32_t flags;
	uint8_t family;

	uint16_t pkt_len_min;
	uint16_t pkt_len_max;
	uint16_t tcp_flags;
	uint16_t tcp_mask_flags;
	uint8_t dscp_value;
	uint8_t fragment;
	uint8_t protocol;
	uint16_t flow_label;

	vrf_id_t vrf_id;

	/* unique identifier for ipset create transaction
	 */
	uint32_t unique;

	/* unique identifier for iptable add transaction
	 */
	uint32_t unique2;

	bool installed;
	bool install_in_progress;

	bool installed_in_iptable;
	bool install_iptable_in_progress;

	struct hash *entry_hash;

	struct bgp_pbr_action *action;

};

/*
 * ipset 集合内的一条具体匹配项(例如某个 src/dst 前缀加端口区间)。
 *   backpointer                     反指所属的 bgp_pbr_match
 *   unique                          与 zebra 做 ipset 条目添加 / 删除配对的编号
 *   src / dst                       源 / 目的前缀
 *   src_port_min/max dst_port_min/max 源 / 目的端口区间
 *   proto                           协议号
 *   path                            反指触发它的 bgp_path_info
 *   installed / install_in_progress 是否已下发 / 是否下发中
 */
struct bgp_pbr_match_entry {
	struct bgp_pbr_match *backpointer;

	uint32_t unique;

	struct prefix src;
	struct prefix dst;

	uint16_t src_port_min;
	uint16_t src_port_max;
	uint16_t dst_port_min;
	uint16_t dst_port_max;
	uint8_t proto;

	void *path;

	bool installed;
	bool install_in_progress;
};

/*
 * 动作的归一化对象：把"重定向 / 丢弃 / 走某张表"抽象成一个可被多条规则共享的目标。
 * 这是引用计数(refcnt)的核心所在——多条 bgp_pbr_rule 或 bgp_pbr_match 可以指向同一个 action。
 *   unique      唯一编号
 *   fwmark      内核 fwmark(用于标记后查表)
 *   table_id    内核路由表 id(重定向到 VRF 时可能直接用 VRF 的表 id)
 *   rate        限速值
 *   vrf_id      源 VRF(=0 表示默认表)
 *   nh          下一跳信息，或丢弃信息(nh 含目标 vrf_id)
 *   installed / install_in_progress 是否已下发 / 下发中
 *   refcnt      引用计数：归零时才真正回收(delete)
 *   bgp         所属 BGP 实例
 *   afi         地址族
 */
struct bgp_pbr_action {

	/*
	 * The Unique identifier of this specific pbrms
	 */
	uint32_t unique;

	uint32_t fwmark;

	uint32_t table_id;

	float rate;

	/*
	 * nexthop information, or drop information
	 * contains src vrf_id and nh contains dest vrf_id
	 */
	vrf_id_t vrf_id;
	struct nexthop nh;

	bool installed;
	bool install_in_progress;
	uint32_t refcnt;
	struct bgp *bgp;
	afi_t afi;
};

/*
 * 若干"按 unique 查找"的辅助函数：先在对应 BGP 实例的哈希表里遍历，
 * 找到唯一编号匹配的对象后返回。unique 为 0 视为无效，直接返回 NULL。
 *   bgp_pbr_rule_lookup            按 unique 找 ip rule 条目
 *   bgp_pbr_action_rule_lookup     按 unique 找 action
 *   bgp_pbr_match_ipset_lookup     按 unique 找 ipset 集合(用 unique 字段)
 *   bgp_pbr_match_ipset_entry_lookup 先按集合名找到集合，再按 unique 找集合内条目
 *   bgp_pbr_match_iptable_lookup   按 unique 找 iptable 相关的集合(用 unique2 字段)
 */
extern struct bgp_pbr_rule *bgp_pbr_rule_lookup(vrf_id_t vrf_id,
						uint32_t unique);

extern struct bgp_pbr_action *bgp_pbr_action_rule_lookup(vrf_id_t vrf_id,
							 uint32_t unique);

extern struct bgp_pbr_match *bgp_pbr_match_ipset_lookup(vrf_id_t vrf_id,
							uint32_t unique);

extern struct bgp_pbr_match_entry *bgp_pbr_match_ipset_entry_lookup(
					    vrf_id_t vrf_id, char *name,
					    uint32_t unique);
extern struct bgp_pbr_match *bgp_pbr_match_iptable_lookup(vrf_id_t vrf_id,
							  uint32_t unique);

extern void bgp_pbr_cleanup(struct bgp *bgp);
extern void bgp_pbr_init(struct bgp *bgp);

/*
 * 哈希表的 key / equal 回调：每条规则、每个 action、每个集合、每个集合条目各有一套。
 * 之所以要把"匹配特征"参与哈希，是为了让内容相同的对象在表里天然去重、可被共享。
 *   *_hash_key    计算哈希值(jhash)
 *   *_hash_equal  判等；只有全部关键字段都相等才算同一条目
 */
extern uint32_t bgp_pbr_rule_hash_key(const void *arg);
extern bool bgp_pbr_rule_hash_equal(const void *arg1,
				   const void *arg2);
extern uint32_t bgp_pbr_action_hash_key(const void *arg);
extern bool bgp_pbr_action_hash_equal(const void *arg1,
				     const void *arg2);
extern uint32_t bgp_pbr_match_entry_hash_key(const void *arg);
extern bool bgp_pbr_match_entry_hash_equal(const void *arg1,
					  const void *arg2);
extern uint32_t bgp_pbr_match_hash_key(const void *arg);
extern bool bgp_pbr_match_hash_equal(const void *arg1,
				    const void *arg2);

void bgp_pbr_print_policy_route(struct bgp_pbr_entry_main *api);

/*
 * 外部驱动入口：当某条 FlowSpec NLRI(p)对应的最优路径发生变化时被调用。
 * nlri_update 为真表示新增 / 更新(install)，为假表示撤销(remove)。
 * 函数内部会构建 bgp_pbr_entry_main 并转交 bgp_pbr_handle_entry()。
 */
struct bgp_path_info;
extern void bgp_pbr_update_entry(struct bgp *bgp, const struct prefix *p,
				 struct bgp_path_info *new_select, afi_t afi,
				 safi_t safi, bool nlri_update);

/*
 * PBR 辅助函数。
 *   bgp_pbr_reset          把某个 afi 的接口策略清空、恢复默认状态
 *   bgp_pbr_interface_lookup 在给定红黑树中按接口名查找
 *   pbr_interface_lookup   在全局 v4 接口树中按名字查找(供外部使用)
 */
/* bgp pbr utilities */
extern struct bgp_pbr_interface *pbr_interface_lookup(const char *name);
extern void bgp_pbr_reset(struct bgp *bgp, afi_t afi);
extern struct bgp_pbr_interface *bgp_pbr_interface_lookup(const char *name,
				   struct bgp_pbr_interface_head *head);

/*
 * 从 FlowSpec 前缀 p 和路径 path 里解析出匹配(match)与动作(action)，
 * 填充到 api，并做合法性校验。成功返回 0，失败(或含不支持组合)返回负值。
 */
extern int bgp_pbr_build_and_validate_entry(const struct prefix *p,
					    struct bgp_path_info *path,
					    struct bgp_pbr_entry_main *api);
#endif /* __BGP_PBR_H__ */
