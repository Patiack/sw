// SPDX-License-Identifier: GPL-2.0-or-later
/* Route map function of bgpd.
 * Copyright (C) 1998, 1999 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_routemap.c —— BGP 专属的 route-map match / set 规则实现
 * ============================================================================
 * 【职责与定位】
 *   route-map 是 FRR 的通用路由策略引擎，框架本体在 lib/routemap.c（条目 entry、
 *   序号、permit / deny 动作、一串 match 子句 + 一串 set 子句的求值逻辑都在那里）。
 *   本文件**不实现框架**，它只向框架注册“BGP 这一侧”认识的 match / set 类型，
 *   并用命令把用户配置解析成框架里的规则对象。
 *   因此阅读时要把两件事分开：
 *     - 框架语义（什么时候调 rule->func、命中/未命中怎么走）看 lib/routemap.c；
 *     - “某个 match / set 具体匹配/设置 BGP 的哪个属性”看本文件。
 *
 * 【本文件的三类内容】
 *   1) 规则实现的样板三件套（几乎每个 match / set 都有一组）：
 *        xxx_compile(arg)  —— 把命令行的一个字符串参数编译成规则对象（void *rule）；
 *        xxx_free(rule)    —— 释放 compile 出来的规则对象；
 *        route_xxx_cmd     —— 一个 struct route_map_rule_cmd，把这套回调绑定到一个
 *                             规则名（如 "ip address"）上，交给框架查表。
 *      void *rule 是“弱类型”的：框架只当成不透明指针，真实类型由 compile 决定，
 *      func 里再强转回来（如 route_match_ip_address 里转成 struct prefix_list *）。
 *   2) match 求值函数：形如 enum route_map_cmd_result_t func(void *rule,
 *        const struct prefix *prefix, struct route_map_object_t type, void *object)。
 *      object 在常规路由场景下是 struct bgp_path_info *，即“正在被策略评估的那条 BGP
 *      路径/路由”。返回值通常是 RMAP_MATCH / RMAP_NOMATCH（见 lib/routemap.h）。
 *   3) DEFUN / DEFPY 命令：解析 CLI 词法，调用 route_map_add_match / add_set /
 *      delete_match / delete_set 等把配置落到对应 route-map 的条目上，并（多数情况下）
 *      触发 route_map_notify_dependencies / apply 让新策略生效。
 *
 * 【核心数据结构 / 关键概念】
 *   - struct rmap_value：把 "set metric 100" / "set metric +50" 这类“数值 + 加/减/
 *     直接设”的通用取值封装起来，由 route_value_compile / route_value_adjust 支撑，
 *     被多个 set 规则复用（metric、local-preference、weight、tag、as-path 等）。
 *   - struct rmap_community：match community / large-community / extcommunity 里
 *     承载“一个社区列表名 + 精确/包含匹配标志”的结构。
 *   - struct bgp_match_peer_compiled：match peer 预编译的 IP / 名字 / 类型信息。
 *   - 大量 match 规则直接复用其他模块的已有对象：prefix_list、bgp_aspath、
 *     community / ecommunity / lcommunity 的列表等——本文件通常只做“查找 + 引用”。
 *
 * 【本文件在主流程中的位置】
 *   上游：FRR 启动时 bgp_route_map_init()（见本文件末尾）把所有 BGP 规则注册进框架，
 *         并把一批 DEFUN / DEFPY 命令挂到 VTY；用户敲命令时调用本文件的命令回调。
 *   下游：策略真正生效发生在别处——bgp_route.c 等在选路/下发时对每个 bgp_path_info
 *         调用 route_map_apply()，框架再回到本文件注册的 match / set 回调。
 *   规则对象之间还通过 route_map_notify_dependencies() 建立“依赖通知”：
 *   当被引用的对象（prefix-list、community-list、as-path-list 等）变化时，
 *   依赖它的 route-map 会被通知去重算。
 *
 * 【易错点 / 阅读建议】
 *   - compile 里返回 NULL 表示“参数非法，放弃该规则”；free 必须能容忍 NULL。
 *   - 很多 match 函数对除 0 敏感：除法/取模前先判断分母非 0，否则崩溃。
 *   - set 规则常带“加/减”语义，注意 route_value_adjust 里的溢出与边界处理。
 *   - 建议阅读顺序：文件开头的 RMAP_VALUE_* / RMAP_VALUE_TYPE_* 宏
 *     → route_value_compile / route_value_adjust（通用取值）
 *     → route_match_ip_address 一整套（match 样板）
 *     → 某个 set 规则（set 样板）
 *     → 末尾 bgp_route_map_init（全局注册总表，最好的索引）。
 * ============================================================================
 */

#include <zebra.h>

#include "prefix.h"
#include "filter.h"
#include "routemap.h"
#include "command.h"
#include "linklist.h"
#include "plist.h"
#include "memory.h"
#include "log.h"
#include "frrlua.h"
#include "frrscript.h"
#include "buffer.h"
#include "sockunion.h"
#include "hash.h"
#include "queue.h"
#include "frrstr.h"
#include "network.h"
#include "lib/northbound_cli.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_packet.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_zebra.h"
#include "bgpd/bgp_regex.h"
#include "bgpd/bgp_community.h"
#include "bgpd/bgp_community_alias.h"
#include "bgpd/bgp_clist.h"
#include "bgpd/bgp_filter.h"
#include "bgpd/bgp_mplsvpn.h"
#include "bgpd/bgp_ecommunity.h"
#include "bgpd/bgp_lcommunity.h"
#include "bgpd/bgp_vty.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_evpn.h"
#include "bgpd/bgp_evpn_private.h"
#include "bgpd/bgp_evpn_vty.h"
#include "bgpd/bgp_mplsvpn.h"
#include "bgpd/bgp_pbr.h"
#include "bgpd/bgp_flowspec_util.h"
#include "bgpd/bgp_encap_types.h"
#include "bgpd/bgp_mpath.h"
#include "bgpd/bgp_script.h"
#include "bgpd/bgp_encap_types.h"
#include "bgpd/bgp_errors.h"
#ifdef ENABLE_BGP_VNC
#include "bgpd/rfapi/bgp_rfapi_cfg.h"
#endif

#include "bgpd/bgp_routemap_clippy.c"

/* Memo of route-map commands.

o Cisco route-map

 match as-path          :  Done
       community        :  Done
       interface        :  Done
       ip address       :  Done
       ip next-hop      :  Done
       ip route-source  :  Done
       ip prefix-list   :  Done
       ipv6 address     :  Done
       ipv6 next-hop    :  Done
       ipv6 route-source:  (This will not be implemented by bgpd)
       ipv6 prefix-list :  Done
       length           :  (This will not be implemented by bgpd)
       metric           :  Done
       route-type       :  (This will not be implemented by bgpd)
       tag              :  Done
       local-preference :  Done

 set  as-path prepend   :  Done
      as-path tag       :  Not yet
      automatic-tag     :  (This will not be implemented by bgpd)
      community         :  Done
      large-community   :  Done
      large-comm-list   :  Done
      comm-list         :  Not yet
      dampning          :  Not yet
      default           :  (This will not be implemented by bgpd)
      interface         :  (This will not be implemented by bgpd)
      ip default        :  (This will not be implemented by bgpd)
      ip next-hop       :  Done
      ip precedence     :  (This will not be implemented by bgpd)
      ip tos            :  (This will not be implemented by bgpd)
      level             :  (This will not be implemented by bgpd)
      local-preference  :  Done
      metric            :  Done
      metric-type       :  Not yet
      origin            :  Done
      tag               :  Done
      weight            :  Done
      table             :  Done

o Local extensions

  set ipv6 next-hop global: Done
  set ipv6 next-hop prefer-global: Done
  set ipv6 next-hop local : Done
  set as-path exclude     : Done

*/

/* 【中文补充：数值型 set 规则的通用取值抽象】
 * 许多 set 规则（metric、local-preference、weight、tag 等）都形如
 *   set metric 100      直接设  = RMAP_VALUE_SET
 *   set metric +50      在现值上加 = RMAP_VALUE_ADD
 *   set metric -50      在现值上减 = RMAP_VALUE_SUB
 * 而且号数还可以不是字面量，而是“取自某个动态来源”——下面三个 TYPE 宏
 * 就是这些动态来源的编号（0 号是“字面量”，由 route_value_compile 里 var 默认值 0 表示）：
 *   - RTT  : 用 peer->rtt（到该 peer 的 RTT）
 *   - IGP  : 用 bpi->extra->igpmetric（该路径的 IGP 度量）
 *   - AIGP : 用 bpi->attr->aigp_metric（该路径属性的 AIGP 度量）
 * 这套抽象由 route_value_compile / route_value_adjust 两个函数统一服务。
 */
/* generic value manipulation to be shared in multiple rules */

/* 【宏】set 动作三态：直接设 / 加 / 减。对应命令里 arg 前导字符
 * 无 / '+' / '-' 三种写法。 */
#define RMAP_VALUE_SET 0
#define RMAP_VALUE_ADD 1
#define RMAP_VALUE_SUB 2

/* 【宏】数值来源类型编号。注意 0 号（字面量）没有宏，因为它由 struct
 * rmap_value.variable 的默认初值 0 隐式表示。 */
#define RMAP_VALUE_TYPE_RTT  1
#define RMAP_VALUE_TYPE_IGP  2
#define RMAP_VALUE_TYPE_AIGP 3

/* 【结构】rmap_value —— “一次 set 动作”的编译结果。
 * 字段：
 *   action   —— 三态动作之一（SET / ADD / SUB）。
 *   variable —— 数值来源类型：0=字面量，否则为上面三个 TYPE 宏之一。
 *              当 variable 非 0 时，value 字段被忽略。
 *   value    —— 当 variable==0 时使用的字面量数值。 */
struct rmap_value {
	uint8_t action;
	uint8_t variable;
	uint32_t value;
};

/* 【函数】route_value_match —— match 侧用：判断给定 value 是否等于规则里的字面量。
 * 参数：rv=编译好的 rmap_value；value=被检查的实际数值。
 * 返回：相等返回 RMAP_MATCH，否则 RMAP_NOMATCH。
 * 关键：**只有 variable==0（即字面量规则）才参与比较**；若规则用的是 rtt/igp/aigp
 * 这类动态来源，此处一律返回 NOMATCH（动态来源不用于 match 语义）。 */
static int route_value_match(struct rmap_value *rv, uint32_t value)
{
	if (rv->variable == 0 && value == rv->value)
		return RMAP_MATCH;

	return RMAP_NOMATCH;
}

/* 【函数】route_value_adjust —— set 侧用：按动作把规则数值应用到 current 上，返回新值。
 * 参数：rv=编译好的 rmap_value；current=该属性当前值；bpi=当前路径（用于取动态来源）。
 * 返回：计算后的新数值。
 * 流程：先按 variable 决定“基准数值 value”（字面量直接取 rv->value；rtt/igp/aigp
 *       分别从 peer/path/attr 取）；再按 action 计算：
 *   - ADD：做饱和加法（若会超过 UINT32_MAX 就钳到 UINT32_MAX，避免回绕）。
 *   - SUB：做饱和减法（若 current<=value 则归 0，避免下溢回绕）。
 *   - SET（default）：直接返回基准 value。
 * 易错点：ADD/SUB 的溢出处理是刻意饱和而非回绕，改动前需谨慎。 */
static uint32_t route_value_adjust(struct rmap_value *rv, uint32_t current,
				   struct bgp_path_info *bpi)
{
	uint32_t value;
	struct peer *peer = bpi->peer;

	switch (rv->variable) {
	case RMAP_VALUE_TYPE_RTT:
		value = peer->rtt;
		break;
	case RMAP_VALUE_TYPE_IGP:
		value = bpi->extra ? bpi->extra->igpmetric : 0;
		break;
	case RMAP_VALUE_TYPE_AIGP:
		value = MIN(bpi->attr->aigp_metric, UINT32_MAX);
		break;
	default:
		value = rv->value;
		break;
	}

	switch (rv->action) {
	case RMAP_VALUE_ADD:
		if (current > UINT32_MAX - value)
			return UINT32_MAX;
		return current + value;
	case RMAP_VALUE_SUB:
		if (current <= value)
			return 0;
		return current - value;
	default:
		return value;
	}
}

/* 【函数】route_value_compile —— 把一个命令行参数(arg)解析成 rmap_value 规则对象。
 * 参数：arg 形如 "100"、"+50"、"-50"、"rtt"、"igp"、"aigp"（可能带前导 +/-）。
 * 返回：新分配的 struct rmap_value *；参数非法返回 NULL（框架据此放弃该 set 子句）。
 * 流程：
 *   1) 看首字符是否有 '+'/'-'，决定 action（默认 SET）。
 *   2) 若剩余是纯数字(all_digit)：用 strtoul 解析；严格校验（空串、尾部残留、
 *      errno、超过 UINT32_MAX 任一命中即非法）→ 这一步是防注入/溢出的关键。
 *   3) 否则尝试匹配关键字 rtt / igp / aigp 设置 dynamic 来源；都不匹配 → NULL。
 *   4) XMALLOC 出对象并填 action/variable/value。
 * 注意：'/' 之类非数字非关键字一律走到最后的 else 返回 NULL。 */
static void *route_value_compile(const char *arg)
{
	uint8_t action = RMAP_VALUE_SET, var = 0;
	unsigned long larg = 0;
	char *endptr = NULL;
	struct rmap_value *rv;

	if (arg[0] == '+') {
		action = RMAP_VALUE_ADD;
		arg++;
	} else if (arg[0] == '-') {
		action = RMAP_VALUE_SUB;
		arg++;
	}

	if (all_digit(arg)) {
		errno = 0;
		larg = strtoul(arg, &endptr, 10);
		if (*arg == 0 || *endptr != 0 || errno || larg > UINT32_MAX)
			return NULL;
	} else if (strmatch(arg, "rtt")) {
		var = RMAP_VALUE_TYPE_RTT;
	} else if (strmatch(arg, "igp")) {
		var = RMAP_VALUE_TYPE_IGP;
	} else if (strmatch(arg, "aigp")) {
		var = RMAP_VALUE_TYPE_AIGP;
	} else {
		return NULL;
	}

	rv = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct rmap_value));

	rv->action = action;
	rv->variable = var;
	rv->value = larg;
	return rv;
}

/* 【函数】route_value_free —— 释放 route_value_compile 生成的 rmap_value。
 * 注意 XFREE 能安全处理 NULL（框架可能对 NULL rule 调用 free）。 */
static void route_value_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充：as-path 对象的编译/释放】
 * 下面两个函数是 as-path 类规则的通用“规则对象”构造与析构：规则对象就是一条
 * 已解析好的 struct aspath（内部走 interned 复用，见 bgp_aspath.c），多个 set /
 * match 规则（如 set as-path prepend、match as-path 等）共用这一对函数。 */
/* generic as path object to be shared in multiple rules */

/* 【函数】route_aspath_compile —— 把参数解析成 aspath 对象。
 * 参数：arg=as-path 字面（如 "65001 65002"）。
 * 返回：aspath_str2aspath 解析出的 struct aspath *；解析失败返回 NULL。
 * 说明：bgp_get_asnotation(NULL) 取当前 AS 记法（asplain / asdot）用于解析。 */
static void *route_aspath_compile(const char *arg)
{
	struct aspath *aspath;

	aspath = aspath_str2aspath(arg, bgp_get_asnotation(NULL));
	if (!aspath)
		return NULL;
	return aspath;
}

/* 【函数】route_aspath_free —— 释放 route_aspath_compile 产出的 aspath 对象。 */
static void route_aspath_free(void *rule)
{
	struct aspath *aspath = rule;
	aspath_free(aspath);
}

/* 【结构】bgp_match_peer_compiled —— match peer / match src-peer 的编译结果。
 * 字段：
 *   interface —— 当参数不是 IP 地址而是名字时，存对端接口名(conf_if)或 peer-group 名；
 *                为 NULL 表示参数被当作 IP 地址解析进了 su。
 *   su        —— 参数解析出的对端地址（sockunion）。命令里写 "local"/"0.0.0.0"
 *                会得到全零地址，作为“本地/自身生成的路由”这一特例的标记。
 * 说明：本规则按“要么按接口/组名匹配，要么按 IP 匹配”二分——见 route_match_peer。 */
struct bgp_match_peer_compiled {
	char *interface;
	union sockunion su;
};

/* 【中文补充：match peer 规则】
 * 语法：match peer (A.B.C.D|X:X::X:X|WORD|local)
 * 语义：判断“该路由所属/来自的 peer”是否等于规则指定的对端。命中规则即返回
 *       RMAP_MATCH。
 * 三分支（见 route_match_peer）：
 *   1) pc->interface 非空（参数是名字）：则要求路由的 peer 是本接口(conf_if)或
 *      属于同名 peer-group。
 *   2) su 为全零地址（命令写 "local"/"0.0.0.0"）：这是“本端自己产生/引入”的路由
 *      特例——只认 NETWORK / REDISTRIBUTE / AGGREGATE / DEFAULT 四种来源标记命中的
 *      peer。
 *   3) 否则按 IP 地址比：若 peer 是 peer-group 则遍历组内所有成员逐一比地址。
 * 注意：object 在此路径下总是被当作 struct bgp_path_info * 使用。 */
/* 'match peer (A.B.C.D|X:X::X:X|WORD)' */

/* Compares the peer specified in the 'match peer' clause with the peer
    received in bgp_path_info->peer. If it is the same, or if the peer structure
    received is a peer_group containing it, returns RMAP_MATCH. */
static enum route_map_cmd_result_t
route_match_peer(void *rule, const struct prefix *prefix, void *object)
{
	struct bgp_match_peer_compiled *pc;
	union sockunion *su;
	union sockunion su_def = {
		.sin = {.sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY}};
	struct peer_group *group;
	struct peer *peer;
	struct listnode *node, *nnode;

	pc = rule;
	su = &pc->su;
	peer = ((struct bgp_path_info *)object)->peer;

	if (pc->interface) {
		if (!peer->conf_if && !peer->group)
			return RMAP_NOMATCH;

		if (peer->conf_if && strcmp(peer->conf_if, pc->interface) == 0)
			return RMAP_MATCH;

		if (peer->group &&
		    strcmp(peer->group->name, pc->interface) == 0)
			return RMAP_MATCH;

		return RMAP_NOMATCH;
	}

	/* If su='0.0.0.0' (command 'match peer local'), and it's a
	   NETWORK,
	   REDISTRIBUTE, AGGREGATE-ADDRESS or DEFAULT_GENERATED route
	   => return RMAP_MATCH
	*/
	if (sockunion_same(su, &su_def)) {
		int ret;
		if (CHECK_FLAG(peer->rmap_type, PEER_RMAP_TYPE_NETWORK)
		    || CHECK_FLAG(peer->rmap_type, PEER_RMAP_TYPE_REDISTRIBUTE)
		    || CHECK_FLAG(peer->rmap_type, PEER_RMAP_TYPE_AGGREGATE)
		    || CHECK_FLAG(peer->rmap_type, PEER_RMAP_TYPE_DEFAULT))
			ret = RMAP_MATCH;
		else
			ret = RMAP_NOMATCH;
		return ret;
	}

	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		if (sockunion_same(su, &peer->connection->su))
			return RMAP_MATCH;

		return RMAP_NOMATCH;
	} else {
		group = peer->group;
		for (ALL_LIST_ELEMENTS(group->peer, node, nnode, peer)) {
			if (sockunion_same(su, &peer->connection->su))
				return RMAP_MATCH;
		}
		return RMAP_NOMATCH;
	}

	return RMAP_NOMATCH;
}

/* 【函数】route_match_peer_compile —— 编译 match peer / match src-peer 的参数。
 * 参数：arg=对端地址、接口/组名，或关键字 "local"。
 * 返回：struct bgp_match_peer_compiled *（永不返回 NULL）。
 * 逻辑：把 "local" 特判成 "0.0.0.0" 后尝试 str2sockunion 解析；
 *   - 解析成功：只填 su，interface 留空（走“按地址比”分支）。
 *   - 解析失败（ret<0）：说明是名字，XSTRDUP 存进 interface（走“按接口/组名比”分支）。
 * 注意：此处不校验名字合法性，名字正确性留到运行时匹配时体现。 */
static void *route_match_peer_compile(const char *arg)
{
	struct bgp_match_peer_compiled *pc;
	int ret;

	pc = XCALLOC(MTYPE_ROUTE_MAP_COMPILED,
		     sizeof(struct bgp_match_peer_compiled));

	ret = str2sockunion(strcmp(arg, "local") ? arg : "0.0.0.0", &pc->su);
	if (ret < 0) {
		pc->interface = XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
		return pc;
	}

	return pc;
}

/* 【中文补充】route_match_peer_free：释放 match peer 的编译对象。
 * 先释放可能存在的 interface 字符串（为 NULL 时 XFREE 安全），再释放结构体本身。 */
/* Free route map's compiled `ip address' value. */
static void route_match_peer_free(void *rule)
{
	struct bgp_match_peer_compiled *pc = rule;

	XFREE(MTYPE_ROUTE_MAP_COMPILED, pc->interface);

	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_peer_cmd：把规则名 "peer" 与上面三个回调绑定，
 * 交给框架注册/查表。字段顺序为 {名字, match回调, compile, free}。 */
/* Route map commands for ip address matching. */
static const struct route_map_rule_cmd route_match_peer_cmd = {
	"peer",
	route_match_peer,
	route_match_peer_compile,
	route_match_peer_free
};

/* 【中文补充：match src-peer 规则】
 * 与 match peer 几乎相同，唯一区别是它比较的是“路由的来源对端”bpi->from，
 * 而非“当前持有的对端”bpi->peer。若 from 为空则回退到 bpi->peer（这在入方向
 * 使用 match src-peer 时较常见）。注意：这里**没有** match peer 里那种 “local/
 * 全零地址 + rmap_type” 的特例分支。 */
static enum route_map_cmd_result_t route_match_src_peer(void *rule, const struct prefix *prefix,
							void *object)
{
	struct bgp_match_peer_compiled *pc;
	union sockunion *su;
	struct peer_group *group;
	struct peer *peer;
	struct listnode *node, *nnode;
	struct bgp_path_info *bpi;

	pc = rule;
	su = &pc->su;
	bpi = object;
	peer = bpi->from;

	/* Fallback to destination (current) peer. This is mostly
	 * happens if `match src-peer ...` is used at incoming direction.
	 */
	if (!peer)
		peer = bpi->peer;

	if (!peer)
		return RMAP_NOMATCH;

	if (pc->interface) {
		if (!peer->conf_if && !peer->group)
			return RMAP_NOMATCH;

		if (peer->conf_if && strcmp(peer->conf_if, pc->interface) == 0)
			return RMAP_MATCH;

		if (peer->group && strcmp(peer->group->name, pc->interface) == 0)
			return RMAP_MATCH;

		return RMAP_NOMATCH;
	}

	if (!CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP)) {
		if (sockunion_same(su, &peer->connection->su))
			return RMAP_MATCH;

		return RMAP_NOMATCH;
	}

	group = peer->group;
	for (ALL_LIST_ELEMENTS(group->peer, node, nnode, peer)) {
		if (sockunion_same(su, &peer->connection->su))
			return RMAP_MATCH;
	}

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_src_peer_cmd：规则名 "src-peer"，复用 match peer 的
 * compile / free 实现（两者参数格式完全一致），仅 match 回调不同。 */
static const struct route_map_rule_cmd route_match_src_peer_cmd = {
	"src-peer",
	route_match_src_peer,
	route_match_peer_compile,
	route_match_peer_free
};

/* 【中文补充：Lua 脚本规则（可选特性 HAVE_SCRIPTING）】
 * FRR 支持用 Lua 脚本实现自定义 route-map match 逻辑。下面这段只在编译期启用
 * 脚本支持时才存在。
 * enum frrlua_rm_status：Lua 脚本返回给 C 侧的四态结果码，语义如下：
 *   LUA_RM_FAILURE(0)        —— 脚本执行失败，等价于 deny（不匹配）。
 *   LUA_RM_NOMATCH(1)        —— 未命中。
 *   LUA_RM_MATCH(2)          —— 命中且未改动数据。
 *   LUA_RM_MATCH_AND_CHANGE(3) —— 命中且脚本修改了属性，需要让上层感知变化。 */
#ifdef HAVE_SCRIPTING

enum frrlua_rm_status {
	/*
	 * Script function run failure.  This will translate into a deny
	 */
	LUA_RM_FAILURE = 0,
	/*
	 * No Match was found for the route map function
	 */
	LUA_RM_NOMATCH,
	/*
	 * Match was found but no changes were made to the incoming data.
	 */
	LUA_RM_MATCH,
	/*
	 * Match was found and data was modified, so figure out what changed
	 */
	LUA_RM_MATCH_AND_CHANGE,
};

/* 【函数】route_match_script —— 调用 Lua 脚本执行 match 判断（HAVE_SCRIPTING）。
 * 参数：rule=脚本名（compile 里存的字符串）；prefix=被匹配前缀；object=bgp_path_info。
 * 返回：RMAP_MATCH / RMAP_NOMATCH。
 * 流程：
 *   1) frrscript_new + frrscript_load 载入脚本和其中的 route_match 函数；失败即 NOMATCH。
 *   2) 复制一份属性(newattr)传给脚本，脚本通过 action 返回值表达四态结果。
 *   3) 若脚本返回 MATCH_AND_CHANGE：把脚本改过的 med / local_pref 回写到 path->attr
 *      （local_pref 变化时会置上 BGP_ATTR_LOCAL_PREF 标志位）。
 *   4) 释放 action 结果内存与脚本句柄。
 * 注意：脚本可修改属性，是本文件里少数会“带副作用改属性”的 match 实现。 */
static enum route_map_cmd_result_t
route_match_script(void *rule, const struct prefix *prefix, void *object)
{
	const char *scriptname = rule;
	const char *routematch_function = "route_match";
	struct bgp_path_info *path = (struct bgp_path_info *)object;

	struct frrscript *fs = frrscript_new(scriptname);

	if (frrscript_load(fs, routematch_function, NULL)) {
		flog_err(EC_BGP_ROUTE_MAP_SCRIPT,
			 "Issue loading script or function; defaulting to no match");
		return RMAP_NOMATCH;
	}

	struct attr newattr = *path->attr;

	int result = frrscript_call(
		fs, routematch_function, ("prefix", prefix),
		("attributes", &newattr), ("peer", path->peer),
		("RM_FAILURE", LUA_RM_FAILURE), ("RM_NOMATCH", LUA_RM_NOMATCH),
		("RM_MATCH", LUA_RM_MATCH),
		("RM_MATCH_AND_CHANGE", LUA_RM_MATCH_AND_CHANGE));

	if (result) {
		flog_err(EC_BGP_ROUTE_MAP_SCRIPT,
			 "Issue running script rule; defaulting to no match");
		return RMAP_NOMATCH;
	}

	int *action = frrscript_get_result(fs, routematch_function, "action", lua_tointegerp);

	int status = RMAP_NOMATCH;

	switch (*action) {
	case LUA_RM_FAILURE:
		flog_err(EC_BGP_ROUTE_MAP_SCRIPT,
			 "Executing route-map match script '%s' failed; defaulting to no match",
			 scriptname);
		status = RMAP_NOMATCH;
		break;
	case LUA_RM_NOMATCH:
		status = RMAP_NOMATCH;
		break;
	case LUA_RM_MATCH_AND_CHANGE:
		status = RMAP_MATCH;
		zlog_debug("Updating attribute based on script's values");

		uint32_t locpref = 0;

		path->attr->med = newattr.med;

		if (bgp_attr_exists(path->attr, BGP_ATTR_LOCAL_PREF))
			locpref = path->attr->local_pref;
		if (locpref != newattr.local_pref) {
			bgp_attr_set(path->attr, BGP_ATTR_LOCAL_PREF);
			path->attr->local_pref = newattr.local_pref;
		}
		break;
	case LUA_RM_MATCH:
		status = RMAP_MATCH;
		break;
	}

	XFREE(MTYPE_SCRIPT_RES, action);

	frrscript_delete(fs);

	return status;
}

/* 【函数】route_match_script_compile —— 规则对象就是脚本名字符串的一份拷贝。 */
static void *route_match_script_compile(const char *arg)
{
	char *scriptname;

	scriptname = XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);

	return scriptname;
}

/* 【函数】route_match_script_free —— 释放脚本名字符串。 */
static void route_match_script_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_script_cmd：规则名 "script"（仅脚本特性启用时注册）。 */
static const struct route_map_rule_cmd route_match_script_cmd = {
	"script",
	route_match_script,
	route_match_script_compile,
	route_match_script_free
};

#endif /* HAVE_SCRIPTING */

/* 【中文补充：match ip address 规则族】
 * 语法：match ip address ACCESS-LIST-NAME
 * 语义：用一条 IPv4 标准/扩展 ACL 匹配“路由前缀本身”。规则对象就是 ACL 名字符串，
 *       运行时才去做 access_list_lookup（所以 ACL 可后建，见 compile 的警告）。
 * 返回：ACL 判定为 DENY 则 NOMATCH，否则 MATCH；非 IPv4 前缀直接 NOMATCH。
 * 阅读提示：本文件里大量 “*_prefix_list” 系列的 match 只处理前缀，而 ip address
 * 用的是 access-list，两者别混淆。 */
/* `match ip address IP_ACCESS_LIST' */

/* Match function should return 1 if match is success else return
   zero. */
static enum route_map_cmd_result_t
route_match_ip_address(void *rule, const struct prefix *prefix, void *object)
{
	struct access_list *alist;

	if (prefix->family == AF_INET) {
		alist = access_list_lookup(AFI_IP, (char *)rule);
		if (alist == NULL) {
			if (unlikely(CHECK_FLAG(rmap_debug,
						DEBUG_ROUTEMAP_DETAIL)))
				zlog_debug(
					"%s: Access-List Specified: %s does not exist defaulting to NO_MATCH",
					__func__, (char *)rule);
			return RMAP_NOMATCH;
		}

		return (access_list_apply(alist, prefix) == FILTER_DENY
				? RMAP_NOMATCH
				: RMAP_MATCH);
	}
	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_ip_address_compile：只存 ACL 名字；若此刻 ACL 尚不存在
 * 仅给出 warning（不拒绝），因为运行时才查表，后续创建 ACL 后即可生效。
 * 这与某些“必须立即存在”的规则不同，属刻意设计。 */
/* Route map `ip address' match statement.  `arg' should be
   access-list name. */
static void *route_match_ip_address_compile(const char *arg)
{
	struct access_list *alist;

	alist = access_list_lookup(AFI_IP, arg);
	if (!alist)
		zlog_warn(
			"Access List specified %s does not exist yet, default will be NO_MATCH until it is created",
			arg);
	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_match_ip_address_free：释放 ACL 名字符串。 */
/* Free route map's compiled `ip address' value. */
static void route_match_ip_address_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_ip_address_cmd：规则名 "ip address"。 */
/* Route map commands for ip address matching. */
static const struct route_map_rule_cmd route_match_ip_address_cmd = {
	"ip address",
	route_match_ip_address,
	route_match_ip_address_compile,
	route_match_ip_address_free
};

/* 【中文补充：match ip next-hop 规则】
 * 语法：match ip next-hop ACCESS-LIST-NAME
 * 语义：用 ACL 匹配“该路由的 BGP next-hop 地址”（attr->nexthop，取 /32 全长度，
 *       所以 ACL 里通常按 host 匹配）。ACL 不存在或判定 DENY 均 NOMATCH。
 * 注意：这里只取 IPv4 的 nexthop 字段，不提 MP_REACH 的全局下一跳。 */
/* `match ip next-hop <IP_ADDRESS_ACCESS_LIST_NAME>' */

/* Match function return 1 if match is success else return zero. */
static enum route_map_cmd_result_t
route_match_ip_next_hop(void *rule, const struct prefix *prefix, void *object)
{
	struct access_list *alist;
	struct bgp_path_info *path;
	struct prefix_ipv4 p;

	if (prefix->family == AF_INET) {
		path = object;
		p.family = AF_INET;
		p.prefix = path->attr->nexthop;
		p.prefixlen = IPV4_MAX_BITLEN;

		alist = access_list_lookup(AFI_IP, (char *)rule);
		if (alist == NULL) {
			if (unlikely(CHECK_FLAG(rmap_debug,
						DEBUG_ROUTEMAP_DETAIL)))
				zlog_debug(
					"%s: Access-List Specified: %s does not exist defaulting to NO_MATCH",
					__func__, (char *)rule);

			return RMAP_NOMATCH;
		}

		return (access_list_apply(alist, &p) == FILTER_DENY
				? RMAP_NOMATCH
				: RMAP_MATCH);
	}
	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_ip_next_hop_compile：规则对象就是 ACL 名字符串。
 * 注意与 ip address 不同，这里 compile 不查表、不告警。 */
/* Route map `ip next-hop' match statement. `arg' is
   access-list name. */
static void *route_match_ip_next_hop_compile(const char *arg)
{
	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_match_ip_next_hop_free：释放 ACL 名字符串。 */
/* Free route map's compiled `ip address' value. */
static void route_match_ip_next_hop_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_ip_next_hop_cmd：规则名 "ip next-hop"。 */
/* Route map commands for ip next-hop matching. */
static const struct route_map_rule_cmd route_match_ip_next_hop_cmd = {
	"ip next-hop",
	route_match_ip_next_hop,
	route_match_ip_next_hop_compile,
	route_match_ip_next_hop_free
};

/* 【中文补充：match ip route-source 规则】
 * 语法：match ip route-source ACCESS-LIST-NAME
 * 语义：用 ACL 匹配“该路由的来源 peer 地址”（peer->connection->su 的 IPv4 地址，
 *       同样按 /32 匹配）。与 ip next-hop 的区别在于比的是“谁通告来的”，而非
 *       “下一跳是谁”。peer 为空或非 IPv4 连接则 NOMATCH。 */
/* `match ip route-source ACCESS-LIST' */

/* Match function return 1 if match is success else return zero. */
static enum route_map_cmd_result_t
route_match_ip_route_source(void *rule, const struct prefix *pfx, void *object)
{
	struct access_list *alist;
	struct bgp_path_info *path;
	struct peer *peer;
	struct prefix_ipv4 p;

	if (pfx->family == AF_INET) {
		path = object;
		peer = path->peer;

		if (!peer || sockunion_family(&peer->connection->su) != AF_INET)
			return RMAP_NOMATCH;

		p.family = AF_INET;
		p.prefix = peer->connection->su.sin.sin_addr;
		p.prefixlen = IPV4_MAX_BITLEN;

		alist = access_list_lookup(AFI_IP, (char *)rule);
		if (alist == NULL) {
			if (unlikely(CHECK_FLAG(rmap_debug,
						DEBUG_ROUTEMAP_DETAIL)))
				zlog_debug(
					"%s: Access-List Specified: %s does not exist defaulting to NO_MATCH",
					__func__, (char *)rule);

			return RMAP_NOMATCH;
		}

		return (access_list_apply(alist, &p) == FILTER_DENY
				? RMAP_NOMATCH
				: RMAP_MATCH);
	}
	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_ip_route_source_compile：规则对象为 ACL 名字符串。 */
/* Route map `ip route-source' match statement. `arg' is
   access-list name. */
static void *route_match_ip_route_source_compile(const char *arg)
{
	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_match_ip_route_source_free：释放 ACL 名字符串。 */
/* Free route map's compiled `ip address' value. */
static void route_match_ip_route_source_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_ip_route_source_cmd：规则名 "ip route-source"。 */
/* Route map commands for ip route-source matching. */
static const struct route_map_rule_cmd route_match_ip_route_source_cmd = {
	"ip route-source",
	route_match_ip_route_source,
	route_match_ip_route_source_compile,
	route_match_ip_route_source_free
};

/* 【函数】route_match_prefix_list_flowspec —— 用 prefix-list 匹配 flowspec 规则。
 * 参数：afi=地址族（IP/IPv6）；plist=前缀列表；p=待匹配的 flowspec 前缀。
 * 返回：MATCH / NOMATCH。
 * 逻辑：flowspec 的“前缀”其实是一条嵌套的规则表达式，本函数先把它解析成
 *       bgp_pbr_entry_main(api)，再按优先级取“目的前缀”或“源前缀”交给
 *       prefix_list_apply 判断。目的优先于源。
 * 注意：family 与 afi 不一致、解析失败(ret<0)、或既无 src 也无 dst 均 NOMATCH。 */
static enum route_map_cmd_result_t
route_match_prefix_list_flowspec(afi_t afi, struct prefix_list *plist,
				 const struct prefix *p)
{
	int ret;
	struct bgp_pbr_entry_main api;

	memset(&api, 0, sizeof(api));

	if (family2afi(p->u.prefix_flowspec.family) != afi)
		return RMAP_NOMATCH;

	/* extract match from flowspec entries */
	ret = bgp_flowspec_match_rules_fill(
					    (uint8_t *)p->u.prefix_flowspec.ptr,
					    p->u.prefix_flowspec.prefixlen, &api,
					    afi);
	if (ret < 0)
		return RMAP_NOMATCH;
	if (CHECK_FLAG(api.match_bitmask, PREFIX_DST_PRESENT) ||
	    CHECK_FLAG(api.match_bitmask_iprule, PREFIX_DST_PRESENT)) {
		if (family2afi((&api.dst_prefix)->family) != afi)
			return RMAP_NOMATCH;
		return prefix_list_apply(plist, &api.dst_prefix) == PREFIX_DENY
			? RMAP_NOMATCH
			: RMAP_MATCH;
	} else if (CHECK_FLAG(api.match_bitmask, PREFIX_SRC_PRESENT) ||
		   CHECK_FLAG(api.match_bitmask_iprule, PREFIX_SRC_PRESENT)) {
		if (family2afi((&api.src_prefix)->family) != afi)
			return RMAP_NOMATCH;
		return (prefix_list_apply(plist, &api.src_prefix) == PREFIX_DENY
			? RMAP_NOMATCH
			: RMAP_MATCH);
	}
	return RMAP_NOMATCH;
}

/* 【函数】route_match_prefix_list_evpn —— 用 prefix-list 匹配 EVPN 前缀。
 * EVPN 的 prefix 是特殊的 EVPN 类型，先经 evpn_prefix2prefix 转成普通 IP 前缀
 * （获得可比较的 family/prefixlen），再交给 prefix_list_apply。转失败即 NOMATCH。 */
static enum route_map_cmd_result_t
route_match_prefix_list_evpn(afi_t afi, struct prefix_list *plist,
			     const struct prefix *p)
{
	/* Convert to match a general plist */
	struct prefix new;

	if (evpn_prefix2prefix(p, &new))
		return RMAP_NOMATCH;

	return (prefix_list_apply(plist, &new) == PREFIX_DENY ? RMAP_NOMATCH
							      : RMAP_MATCH);
}

/* 【函数】route_match_address_prefix_list —— “ip/ipv6 address prefix-list”的公共实现。
 * 参数：rule=prefix-list 名；afi=IP 还是 IPv6；prefix=被匹配前缀；object 未用。
 * 逻辑：先按 afi 查找 prefix-list（不存在→按调试等级打印→NOMATCH）；
 *       然后按前缀 family 分派：FLOWSPEC 走 route_match_prefix_list_flowspec，
 *       EVPN 走 route_match_prefix_list_evpn，其余直接 prefix_list_apply。
 * 返回：PREFIX_DENY→NOMATCH，否则 MATCH。 */
static enum route_map_cmd_result_t
route_match_address_prefix_list(void *rule, afi_t afi,
				const struct prefix *prefix, void *object)
{
	struct prefix_list *plist;

	plist = prefix_list_lookup(afi, (char *)rule);
	if (plist == NULL) {
		if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP_DETAIL)))
			zlog_debug("%s: Prefix List %s (%s) specified does not exist defaulting to NO_MATCH",
				   __func__, (char *)rule, afi2str(afi));
		return RMAP_NOMATCH;
	}

	if (prefix->family == AF_FLOWSPEC)
		return route_match_prefix_list_flowspec(afi, plist,
							prefix);

	else if (prefix->family == AF_EVPN)
		return route_match_prefix_list_evpn(afi, plist, prefix);

	return (prefix_list_apply(plist, prefix) == PREFIX_DENY ? RMAP_NOMATCH
								: RMAP_MATCH);
}

/* 【中文补充：match ip address prefix-list 规则】
 * 语法：match ip address prefix-list PREFIX-LIST-NAME
 * 语义：用 IPv4 前缀列表匹配“路由前缀本身”（prefix_list 版，非 access-list）。
 * 说明：只是个薄包装，直接转调 route_match_address_prefix_list(AFI_IP,...)。
 * compile/free 仅存/释放前缀列表名。 */
static enum route_map_cmd_result_t
route_match_ip_address_prefix_list(void *rule, const struct prefix *prefix,
				   void *object)
{
	return route_match_address_prefix_list(rule, AFI_IP, prefix, object);
}

/* 【中文补充】route_match_ip_address_prefix_list_compile：存前缀列表名。 */
static void *route_match_ip_address_prefix_list_compile(const char *arg)
{
	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_match_ip_address_prefix_list_free：释放前缀列表名。 */
static void route_match_ip_address_prefix_list_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_ip_address_prefix_list_cmd：规则名 "ip address prefix-list"。 */
static const struct route_map_rule_cmd
		route_match_ip_address_prefix_list_cmd = {
	"ip address prefix-list",
	route_match_ip_address_prefix_list,
	route_match_ip_address_prefix_list_compile,
	route_match_ip_address_prefix_list_free
};

/* 【中文补充：match ip next-hop prefix-list 规则】
 * 语法：match ip next-hop prefix-list PREFIX-LIST-NAME
 * 语义：用 IPv4 前缀列表匹配“该路由下一跳地址”（按 /32 全长度提交）。
 * 与 ip next-hop(ACL) 结构对称，唯一区别是用 prefix-list 而非 access-list。 */
/* `match ip next-hop prefix-list PREFIX_LIST' */

static enum route_map_cmd_result_t
route_match_ip_next_hop_prefix_list(void *rule, const struct prefix *prefix,
				    void *object)
{
	struct prefix_list *plist;
	struct bgp_path_info *path;
	struct prefix_ipv4 p;

	if (prefix->family == AF_INET) {
		path = object;
		p.family = AF_INET;
		p.prefix = path->attr->nexthop;
		p.prefixlen = IPV4_MAX_BITLEN;

		plist = prefix_list_lookup(AFI_IP, (char *)rule);
		if (plist == NULL) {
			if (unlikely(CHECK_FLAG(rmap_debug,
						DEBUG_ROUTEMAP_DETAIL)))
				zlog_debug(
					"%s: Prefix List %s specified does not exist defaulting to NO_MATCH",
					__func__, (char *)rule);
			return RMAP_NOMATCH;
		}

		return (prefix_list_apply(plist, &p) == PREFIX_DENY
				? RMAP_NOMATCH
				: RMAP_MATCH);
	}
	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_ip_next_hop_prefix_list_compile：存前缀列表名。 */
static void *route_match_ip_next_hop_prefix_list_compile(const char *arg)
{
	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_match_ip_next_hop_prefix_list_free：释放前缀列表名。 */
static void route_match_ip_next_hop_prefix_list_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_ip_next_hop_prefix_list_cmd：规则名 "ip next-hop prefix-list"。 */
static const struct route_map_rule_cmd
		route_match_ip_next_hop_prefix_list_cmd = {
	"ip next-hop prefix-list",
	route_match_ip_next_hop_prefix_list,
	route_match_ip_next_hop_prefix_list_compile,
	route_match_ip_next_hop_prefix_list_free
};

/* 【中文补充：match ipv6 next-hop prefix-list 规则】
 * 语法：match ipv6 next-hop prefix-list PREFIX-LIST-NAME
 * 语义：用 IPv6 前缀列表匹配“该路由的 IPv6 下一跳”。先在 mp_nexthop_global 上判；
 *       若未命中且属性里同时带 global 与 link-local(mp_nexthop_len==GLOBAL_AND_LL)，
 *       再在 mp_nexthop_local 上判一次。两者任一命中即 MATCH。 */
/* `match ipv6 next-hop prefix-list PREFIXLIST_NAME' */
static enum route_map_cmd_result_t
route_match_ipv6_next_hop_prefix_list(void *rule, const struct prefix *prefix,
				      void *object)
{
	struct prefix_list *plist;
	struct bgp_path_info *path;
	struct prefix_ipv6 p;

	if (prefix->family == AF_INET6) {
		path = object;
		p.family = AF_INET6;
		p.prefix = path->attr->mp_nexthop_global;
		p.prefixlen = IPV6_MAX_BITLEN;

		plist = prefix_list_lookup(AFI_IP6, (char *)rule);
		if (!plist) {
			if (unlikely(CHECK_FLAG(rmap_debug,
						DEBUG_ROUTEMAP_DETAIL)))
				zlog_debug(
					"%s: Prefix List %s specified does not exist defaulting to NO_MATCH",
					__func__, (char *)rule);
			return RMAP_NOMATCH;
		}

		if (prefix_list_apply(plist, &p) == PREFIX_PERMIT)
			return RMAP_MATCH;

		if (path->attr->mp_nexthop_len
		    == BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL) {
			p.prefix = path->attr->mp_nexthop_local;
			if (prefix_list_apply(plist, &p) == PREFIX_PERMIT)
				return RMAP_MATCH;
		}
	}

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_ipv6_next_hop_prefix_list_compile：存前缀列表名。 */
static void *route_match_ipv6_next_hop_prefix_list_compile(const char *arg)
{
	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_match_ipv6_next_hop_prefix_list_free：释放前缀列表名。 */
static void route_match_ipv6_next_hop_prefix_list_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_ipv6_next_hop_prefix_list_cmd：规则名 "ipv6 next-hop prefix-list"。 */
static const struct route_map_rule_cmd
		route_match_ipv6_next_hop_prefix_list_cmd = {
	"ipv6 next-hop prefix-list",
	route_match_ipv6_next_hop_prefix_list,
	route_match_ipv6_next_hop_prefix_list_compile,
	route_match_ipv6_next_hop_prefix_list_free
};

/* 【中文补充：match ip next-hop type blackhole 规则】
 * 语法：match ip next-hop type blackhole
 * 语义：识别“黑洞路由”——下一跳地址为 0.0.0.0(INADDR_ANY) 且下一跳接口索引
 *       (nh_ifindex) 也无法解析时判为黑洞并 MATCH。典型用于匹配内核/静态的
 *       `ip route add blackhole ...` 路由。
 * 注意：compile 收到参数只是原样存下（字符 'blackhole'），match 逻辑并不依赖它。 */
/* `match ip next-hop type <blackhole>' */

static enum route_map_cmd_result_t
route_match_ip_next_hop_type(void *rule, const struct prefix *prefix,
			     void *object)
{
	struct bgp_path_info *path;

	if (prefix->family == AF_INET) {
		path = (struct bgp_path_info *)object;
		if (!path)
			return RMAP_NOMATCH;

		/* If nexthop interface's index can't be resolved and nexthop is
		   set to any address then mark it as type `blackhole`.
		   This logic works for matching kernel/static routes like:
		   `ip route add blackhole 10.0.0.1`. */
		if (path->attr->nexthop.s_addr == INADDR_ANY
		    && !path->attr->nh_ifindex)
			return RMAP_MATCH;
	}
	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_ip_next_hop_type_compile：存 "blackhole" 字符串。 */
static void *route_match_ip_next_hop_type_compile(const char *arg)
{
	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_match_ip_next_hop_type_free：释放字符串。 */
static void route_match_ip_next_hop_type_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_ip_next_hop_type_cmd：规则名 "ip next-hop type"。 */
static const struct route_map_rule_cmd
		route_match_ip_next_hop_type_cmd = {
	"ip next-hop type",
	route_match_ip_next_hop_type,
	route_match_ip_next_hop_type_compile,
	route_match_ip_next_hop_type_free
};

/* 【中文补充：match source-protocol 规则】
 * 语法：match source-protocol PROTOCOL-NAME
 * 语义：匹配“这条路由的来源协议”（bpi->type）。规则对象是一个 int 协议号，
 *       由 proto_name2num 把名字（如 static/connected/kernel/ospf）转成编号。
 * 注意：这是**严格相等**匹配（== 而非位运算），故一次只能匹配一种协议。
 *       协议名非法时 proto_name2num 返 0（ZEBRA_ROUTE_SYSTEM），需留意。 */
/* `match source-protocol` */
static enum route_map_cmd_result_t
route_match_source_protocol(void *rule, const struct prefix *prefix,
			    void *object)
{
	struct bgp_path_info *path = object;
	int *protocol = rule;

	if (!path)
		return RMAP_NOMATCH;

	if (path->type == *protocol)
		return RMAP_MATCH;

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_source_protocol_compile：把协议名转成 int 号并保存。
 * 分配一个 int 作为规则对象（弱类型指针）。 */
static void *route_match_source_protocol_compile(const char *arg)
{
	int *protocol;

	protocol = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(*protocol));
	*protocol = proto_name2num(arg);

	return protocol;
}

/* 【中文补充】route_match_source_protocol_free：释放那个 int。 */
static void route_match_source_protocol_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_source_protocol_cmd：规则名 "source-protocol"。 */
static const struct route_map_rule_cmd route_match_source_protocol_cmd = {
	"source-protocol",
	route_match_source_protocol,
	route_match_source_protocol_compile,
	route_match_source_protocol_free
};

/* 【中文补充：match ip route-source prefix-list 规则】
 * 语法：match ip route-source prefix-list PREFIX-LIST-NAME
 * 语义：用 IPv4 前缀列表匹配“该路由来源 peer 的地址”（peer->connection->su，
 *       按 /32）。与 ip route-source(ACL) 对称，仅换成 prefix-list。 */

/* `match ip route-source prefix-list PREFIX_LIST' */

static enum route_map_cmd_result_t
route_match_ip_route_source_prefix_list(void *rule, const struct prefix *prefix,
					void *object)
{
	struct prefix_list *plist;
	struct bgp_path_info *path;
	struct peer *peer;
	struct prefix_ipv4 p;

	if (prefix->family == AF_INET) {
		path = object;
		peer = path->peer;

		if (!peer || sockunion_family(&peer->connection->su) != AF_INET)
			return RMAP_NOMATCH;

		p.family = AF_INET;
		p.prefix = peer->connection->su.sin.sin_addr;
		p.prefixlen = IPV4_MAX_BITLEN;

		plist = prefix_list_lookup(AFI_IP, (char *)rule);
		if (plist == NULL) {
			if (unlikely(CHECK_FLAG(rmap_debug,
						DEBUG_ROUTEMAP_DETAIL)))
				zlog_debug(
					"%s: Prefix List %s specified does not exist defaulting to NO_MATCH",
					__func__, (char *)rule);
			return RMAP_NOMATCH;
		}

		return (prefix_list_apply(plist, &p) == PREFIX_DENY
				? RMAP_NOMATCH
				: RMAP_MATCH);
	}
	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_ip_route_source_prefix_list_compile：存前缀列表名。 */
static void *route_match_ip_route_source_prefix_list_compile(const char *arg)
{
	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_match_ip_route_source_prefix_list_free：释放前缀列表名。 */
static void route_match_ip_route_source_prefix_list_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_ip_route_source_prefix_list_cmd：规则名 "ip route-source prefix-list"。 */
static const struct route_map_rule_cmd
		route_match_ip_route_source_prefix_list_cmd = {
	"ip route-source prefix-list",
	route_match_ip_route_source_prefix_list,
	route_match_ip_route_source_prefix_list_compile,
	route_match_ip_route_source_prefix_list_free
};

/* 【中文补充：match evpn default-route 规则】
 * 语法：match evpn default-route
 * 语义：判断被匹配的 EVPN 前缀是否为“默认路由”（is_evpn_prefix_default）。
 * 特点：该规则**没有参数**，也没有 compile/free（cmd 里二者为 NULL）。 */

/* `match evpn default-route' */

/* Match function should return 1 if match is success else 0 */
static enum route_map_cmd_result_t
route_match_evpn_default_route(void *rule, const struct prefix *p, void *object)
{
	if (is_evpn_prefix_default(p))
		return RMAP_MATCH;

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_evpn_default_route_cmd：规则名 "evpn default-route"，
 * compile/free 均为 NULL（无参数）。 */
/* Route map commands for default-route matching. */
static const struct route_map_rule_cmd
		route_match_evpn_default_route_cmd = {
	"evpn default-route",
	route_match_evpn_default_route,
	NULL,
	NULL
};

/* 【中文补充：match mac address 规则】
 * 语法：match mac address MAC-ACCESS-LIST-NAME
 * 语义：用 L2VPN 族的 ACL 匹配 EVPN MAC/IP 路由里的**MAC 地址**（构造出
 *       AF_ETHERNET 前缀，长度 48 bit）。只处理 EVPN MAC-IP 路由
 *       (BGP_EVPN_MAC_IP_ROUTE)，其它 EVPN 路由类型直接 NOMATCH。 */

/* `match mac address MAC_ACCESS_LIST' */

/* Match function should return 1 if match is success else return
   zero. */
static enum route_map_cmd_result_t
route_match_mac_address(void *rule, const struct prefix *prefix, void *object)
{
	struct access_list *alist;
	struct prefix p;

	alist = access_list_lookup(AFI_L2VPN, (char *)rule);
	if (alist == NULL) {
		if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP_DETAIL)))
			zlog_debug(
				"%s: Access-List Specified: %s does not exist defaulting to NO_MATCH",
				__func__, (char *)rule);

		return RMAP_NOMATCH;
	}
	if (prefix->u.prefix_evpn.route_type != BGP_EVPN_MAC_IP_ROUTE) {
		if (unlikely(CHECK_FLAG(rmap_debug, DEBUG_ROUTEMAP_DETAIL)))
			zlog_debug(
				"%s: Prefix %pFX is not a EVPN MAC IP ROUTE defaulting to NO_MATCH",
				__func__, prefix);
		return RMAP_NOMATCH;
	}

	p.family = AF_ETHERNET;
	p.prefixlen = ETH_ALEN * 8;
	p.u.prefix_eth = prefix->u.prefix_evpn.macip_addr.mac;

	return (access_list_apply(alist, &p) == FILTER_DENY ? RMAP_NOMATCH
							    : RMAP_MATCH);
}

/* 【中文补充】route_match_mac_address_compile：存 ACL 名字符串。 */
/* Route map `mac address' match statement.  `arg' should be
   access-list name. */
static void *route_match_mac_address_compile(const char *arg)
{
	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_match_mac_address_free：释放 ACL 名字符串。 */
/* Free route map's compiled `ip address' value. */
static void route_match_mac_address_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_mac_address_cmd：规则名 "mac address"。 */
/* Route map commands for mac address matching. */
static const struct route_map_rule_cmd route_match_mac_address_cmd = {
	"mac address",
	route_match_mac_address,
	route_match_mac_address_compile,
	route_match_mac_address_free
};

/* 【中文补充：match evpn vni 规则】
 * 语法：match evpn vni VNI
 * 语义：匹配 EVPN 路由所携带的 VNI（从该路径的 MPLS/VXLAN 标签栈中反解出 label2vni）。
 * 【RMAP_NOOP 三态之一——本规则特有的“跳过检查”语义】
 *   框架的 match 并不是只有命中/未命中两态；本规则在“不适用”时返回 RMAP_NOOP，
 *   表示这条 match 子句本次不参与判定（既不命中也不否决）。
 *   返回 NOOP 的两种情况：
 *     1) 隧道类型不是 VXLAN(encap_tunneltype != BGP_ENCAP_TYPE_VXLAN)；
 *     2) 路由类型不是 type-1(AD) / type-2(MAC-IP) / type-5(IP-PREFIX)——因为它们
 *        不带 VNI 标签。
 *   其余情况遍历标签栈(最多 BGP_MAX_LABELS 个)逐个比对，命中即 MATCH，否则 NOMATCH。
 */
/*
 * Match function returns:
 * ...RMAP_MATCH if match is found.
 * ...RMAP_NOMATCH if match is not found.
 * ...RMAP_NOOP to ignore this match check.
 */
static enum route_map_cmd_result_t
route_match_vni(void *rule, const struct prefix *prefix, void *object)
{
	vni_t vni = 0;
	unsigned int label_cnt;
	struct bgp_path_info *path = NULL;
	struct prefix_evpn *evp = (struct prefix_evpn *) prefix;

	vni = *((vni_t *)rule);
	path = (struct bgp_path_info *)object;

	/*
	 * This rmap filter is valid for vxlan tunnel type only.
	 * For any other tunnel type, return noop to ignore
	 * this check.
	 */
	if (path->attr->encap_tunneltype != BGP_ENCAP_TYPE_VXLAN)
		return RMAP_NOOP;

	/*
	 * Apply filter to type 1, 2, 5 routes only.
	 * Other route types do not have vni label.
	 */
	if (evp
	    && (evp->prefix.route_type != BGP_EVPN_AD_ROUTE
		&& evp->prefix.route_type != BGP_EVPN_MAC_IP_ROUTE
		&& evp->prefix.route_type != BGP_EVPN_IP_PREFIX_ROUTE))
		return RMAP_NOOP;

	for (label_cnt = 0; label_cnt < BGP_MAX_LABELS &&
			    label_cnt < BGP_PATH_INFO_NUM_LABELS(path);
	     label_cnt++) {
		if (vni == label2vni(&path->extra->labels->label[label_cnt]))
			return RMAP_MATCH;
	}

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_vni_compile：把 VNI 字符串转成 vni_t 保存；
 * strtoul 后若尾部还有残留字符(*end != '\0')则判为非法返回 NULL。 */
/* Route map `vni' match statement. */
static void *route_match_vni_compile(const char *arg)
{
	vni_t *vni = NULL;
	char *end = NULL;

	vni = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(vni_t));

	*vni = strtoul(arg, &end, 10);
	if (*end != '\0') {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, vni);
		return NULL;
	}

	return vni;
}

/* 【中文补充】route_match_vni_free：释放 vni_t。 */
/* Free route map's compiled `vni' value. */
static void route_match_vni_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_evpn_vni_cmd：规则名 "evpn vni"。 */
/* Route map commands for vni matching. */
static const struct route_map_rule_cmd route_match_evpn_vni_cmd = {
	"evpn vni",
	route_match_vni,
	route_match_vni_compile,
	route_match_vni_free
};

/* 【中文补充：match evpn route-type 规则】
 * 语法：match evpn route-type (ad|macip|multicast|es|prefix...)  实际以 arg 前两
 *       个字母区分：ea→AD(type-1)、ma→MAC-IP(type-2)、mu→IMET/multicast(type-3)、
 *       es→ES(type-4)，其余一律当作 IP-PREFIX(type-5)。
 * 语义：把参数编译成一个 route_type 字节，match 时与 EVPN 前缀里的 route_type 严格相等比较。
 * 注意：用 strncmp(arg,"xx",2) 只比前两字母，属宽松前缀匹配（这是刻意的简写命令）。 */
/* `match evpn route-type' */

/* Match function should return 1 if match is success else return
   zero. */
static enum route_map_cmd_result_t
route_match_evpn_route_type(void *rule, const struct prefix *pfx, void *object)
{
	uint8_t route_type = 0;

	route_type = *((uint8_t *)rule);

	if (route_type == pfx->u.prefix_evpn.route_type)
		return RMAP_MATCH;

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_evpn_route_type_compile：把 route-type 参数压成 1 字节枚举。 */
/* Route map `route-type' match statement. */
static void *route_match_evpn_route_type_compile(const char *arg)
{
	uint8_t *route_type = NULL;

	route_type = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(uint8_t));

	if (strncmp(arg, "ea", 2) == 0)
		*route_type = BGP_EVPN_AD_ROUTE;
	else if (strncmp(arg, "ma", 2) == 0)
		*route_type = BGP_EVPN_MAC_IP_ROUTE;
	else if (strncmp(arg, "mu", 2) == 0)
		*route_type = BGP_EVPN_IMET_ROUTE;
	else if (strncmp(arg, "es", 2) == 0)
		*route_type = BGP_EVPN_ES_ROUTE;
	else
		*route_type = BGP_EVPN_IP_PREFIX_ROUTE;

	return route_type;
}

/* 【中文补充】route_match_evpn_route_type_free：释放 1 字节枚举。 */
/* Free route map's compiled `route-type' value. */
static void route_match_evpn_route_type_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_evpn_route_type_cmd：规则名 "evpn route-type"。 */
/* Route map commands for evpn route-type  matching. */
static const struct route_map_rule_cmd route_match_evpn_route_type_cmd = {
	"evpn route-type",
	route_match_evpn_route_type,
	route_match_evpn_route_type_compile,
	route_match_evpn_route_type_free
};

/* 【中文补充：match evpn rd 规则】
 * 语法：match evpn rd ROUTE-DISTINGUISHER
 * 语义：匹配 EVPN 路由的目的节点 RD（Route Distinguisher）。规则对象是编译好的
 *       struct prefix_rd；运行时从 bgp_dest_get_prefix(path->net->pdest) 取路由
 *       的 RD，用 memcmp 比较 ECOMMUNITY_SIZE(8) 字节是否相等。
 * 注意：只对 AF_EVPN 前缀有效；path->net 或 pdest 为空(路由已销毁等)时 NOMATCH。 */
/* `match rd' */

/* Match function should return 1 if match is success else return zero. */
static enum route_map_cmd_result_t
route_match_rd(void *rule, const struct prefix *prefix, void *object)
{
	struct prefix_rd *prd_rule = NULL;
	const struct prefix_rd *prd_route = NULL;
	struct bgp_path_info *path = NULL;

	if (prefix->family != AF_EVPN)
		return RMAP_NOMATCH;

	prd_rule = (struct prefix_rd *)rule;
	path = (struct bgp_path_info *)object;

	if (path->net == NULL || path->net->pdest == NULL)
		return RMAP_NOMATCH;

	prd_route = (struct prefix_rd *)bgp_dest_get_prefix(path->net->pdest);
	if (memcmp(prd_route->val, prd_rule->val, ECOMMUNITY_SIZE) == 0)
		return RMAP_MATCH;

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_rd_compile：用 str2prefix_rd 把 RD 字符串解析成 prefix_rd；
 * 解析失败即释放并返回 NULL。 */
/* Route map `rd' match statement. */
static void *route_match_rd_compile(const char *arg)
{
	struct prefix_rd *prd;
	int ret;

	prd = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct prefix_rd));

	ret = str2prefix_rd(arg, prd);
	if (!ret) {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, prd);
		return NULL;
	}

	return prd;
}

/* 【中文补充】route_match_rd_free：释放 prefix_rd。 */
/* Free route map's compiled `rd' value. */
static void route_match_rd_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_evpn_rd_cmd：规则名 "evpn rd"。 */
/* Route map commands for rd matching. */
static const struct route_map_rule_cmd route_match_evpn_rd_cmd = {
	"evpn rd",
	route_match_rd,
	route_match_rd_compile,
	route_match_rd_free
};

/* 【中文补充：match community-limit 规则】
 * 语法：match community-limit N
 * 语义：判断该路由携带的**标准 community 个数**是否 <= N，是则 MATCH。
 *       规则对象是编译好的 uint16_t 上限值。
 * 注意：只统计标准 community(bgp_attr_get_community)，不含 large/ext community。 */
/* `match community-limit' */

/* Match function should return :
 * - RMAP_MATCH if the bgp update community list count
 * is less or equal to the configured limit.
 * - RMAP_NOMATCH if the community list count is greater than the
 * configured limit.
 */
static enum route_map_cmd_result_t
route_match_community_limit(void *rule, const struct prefix *prefix, void *object)
{
	struct bgp_path_info *path = NULL;
	struct community *picomm = NULL;
	uint16_t count = 0;
	uint16_t *limit_rule = rule;

	path = (struct bgp_path_info *)object;

	picomm = bgp_attr_get_community(path->attr);
	if (picomm)
		count = picomm->size;

	if (count <= *limit_rule)
		return RMAP_MATCH;

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_community_limit_compile：解析上限值存入 uint16_t；有残留字符则 NULL。 */
/* Route map `community-limit' match statement. */
static void *route_match_community_limit_compile(const char *arg)
{
	uint16_t *limit = NULL;
	char *end = NULL;

	limit = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(uint16_t));
	*limit = strtoul(arg, &end, 10);
	if (*end != '\0') {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, limit);
		return NULL;
	}
	return limit;
}

/* 【中文补充】route_match_community_limit_free：释放上限值。 */
/* Free route map's compiled `community-limit' value. */
static void route_match_community_limit_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_community_limit_cmd：规则名 "community-limit"。 */
/* Route map commands for community limit matching. */
static const struct route_map_rule_cmd route_match_community_limit_cmd = {
	"community-limit", route_match_community_limit,
	route_match_community_limit_compile, route_match_community_limit_free
};

/* 【中文补充：match extcommunity-limit 规则】
 * 语法：match extcommunity-limit N
 * 语义：判断该路由携带的**扩展 community 总数**是否 <= N。
 * 注意：这里把 RT 类 ext-community(bgp_attr_get_ecommunity) 与 IPv6 类
 *       ext-community(bgp_attr_get_ipv6_ecommunity) 的个数**相加**后再比较，
 *       与 community-limit 只数一种不同。 */
/* `match extcommunity-limit' */

/* Match function should return :
 * - RMAP_MATCH if the bgp update extcommunity list count
 * is less or equal to the configured limit.
 * - RMAP_NOMATCH if the extcommunity list count is greater than the
 * configured limit.
 */
static enum route_map_cmd_result_t
route_match_extcommunity_limit(void *rule, const struct prefix *prefix, void *object)
{
	struct bgp_path_info *path = NULL;
	struct ecommunity *piextcomm = NULL, *pi6extcomm = NULL;
	uint16_t count = 0;
	uint16_t *limit_rule = rule;

	path = (struct bgp_path_info *)object;

	piextcomm = bgp_attr_get_ecommunity(path->attr);
	if (piextcomm)
		count = piextcomm->size;

	pi6extcomm = bgp_attr_get_ipv6_ecommunity(path->attr);
	if (pi6extcomm)
		count += pi6extcomm->size;

	if (count <= *limit_rule)
		return RMAP_MATCH;

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_extcommunity_limit_compile：解析上限值存入 uint16_t。 */
/* Route map `extcommunity-limit' match statement. */
static void *route_match_extcommunity_limit_compile(const char *arg)
{
	uint16_t *limit = NULL;
	char *end = NULL;

	limit = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(uint16_t));
	*limit = strtoul(arg, &end, 10);
	if (*end != '\0') {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, limit);
		return NULL;
	}
	return limit;
}

/* 【中文补充】route_match_extcommunity_limit_free：释放上限值。 */
/* Free route map's compiled `community-limit' value. */
static void route_match_extcommunity_limit_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_extcommunity_limit_cmd：规则名 "extcommunity-limit"。 */
/* Route map commands for community limit matching. */
static const struct route_map_rule_cmd route_match_extcommunity_limit_cmd = {
	"extcommunity-limit", route_match_extcommunity_limit,
	route_match_extcommunity_limit_compile, route_match_extcommunity_limit_free
};

/* 【函数】route_set_evpn_gateway_ip —— set 侧规则：给 EVPN type-5 前缀设置网关 IP。
 * 参数：rule=编译好的 struct ipaddr（网关地址）；prefix=被处理前缀；object=bgp_path_info。
 * 返回：**恒返回 RMAP_OKAY**——它是个 set 规则，用返回值表示“执行成功”，不做命中判断。
 * 逻辑：只对 AF_EVPN 且 route_type==BGP_EVPN_IP_PREFIX_ROUTE(type-5) 的前缀生效；
 *       还要求网关地址家族与 EVPN 前缀里的 IP 家族一致（v4 对 4 字节、v6 对 16 字节），
 *       否则原样返回不改。命中时分配一个 bgp_route_evpn(overlay)，标成
 *       OVERLAY_INDEX_GATEWAY_IP，填入 gw_ip，并挂到 path->attr 的 EVPN overlay 上。
 * 易错点：bre 一旦 XCALLOC 出来就不会在此函数里释放——无论后面哪条提前 return，
 *         它都交给了 bgp_attr_set_evpn_overlay 或成为泄漏疑点（见下方行内疑似注释）。
 *         另外提前返回分支不会释放 bre，是阅读时需注意的地方。 */
static enum route_map_cmd_result_t
route_set_evpn_gateway_ip(void *rule, const struct prefix *prefix, void *object)
{
	struct ipaddr *gw_ip = rule;
	struct bgp_path_info *path;
	struct prefix_evpn *evp;
	struct bgp_route_evpn *bre = XCALLOC(MTYPE_BGP_EVPN_OVERLAY,
					     sizeof(struct bgp_route_evpn));

	if (prefix->family != AF_EVPN)
		return RMAP_OKAY;

	evp = (struct prefix_evpn *)prefix;
	if (evp->prefix.route_type != BGP_EVPN_IP_PREFIX_ROUTE)
		return RMAP_OKAY;

	if ((is_evpn_prefix_ipaddr_v4(evp) && IPADDRSZ(gw_ip) != 4)
	    || (is_evpn_prefix_ipaddr_v6(evp) && IPADDRSZ(gw_ip) != 16))
		return RMAP_OKAY;

	path = object;

	/* Set gateway-ip value. */
	bre->type = OVERLAY_INDEX_GATEWAY_IP;
	bre->gw_ip = *gw_ip;
	bgp_attr_set_evpn_overlay(path->attr, bre);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_evpn_gateway_ip_compile：把参数 IP 字符串用 str2ipaddr 解析
 * 成 struct ipaddr 作为规则对象；失败则释放并返回 NULL。 */
/*
 * Route map `evpn gateway-ip' compile function.
 * Given string is converted to struct ipaddr structure
 */
static void *route_set_evpn_gateway_ip_compile(const char *arg)
{
	struct ipaddr *gw_ip = NULL;
	int ret;

	gw_ip = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct ipaddr));

	ret = str2ipaddr(arg, gw_ip);
	if (ret < 0) {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, gw_ip);
		return NULL;
	}
	return gw_ip;
}

/* 【中文补充】route_set_evpn_gateway_ip_free：释放 struct ipaddr。 */
/* Free route map's compiled `evpn gateway_ip' value. */
static void route_set_evpn_gateway_ip_free(void *rule)
{
	struct ipaddr *gw_ip = rule;

	XFREE(MTYPE_ROUTE_MAP_COMPILED, gw_ip);
}

/* 【中文补充】route_set_evpn_gateway_ip_ipv4_cmd：规则名 "evpn gateway-ip ipv4"，
 * 注意它不是 static 的（对外可见，供别处直接引用）。 */
/* Route map commands for set evpn gateway-ip ipv4. */
struct route_map_rule_cmd route_set_evpn_gateway_ip_ipv4_cmd = {
	"evpn gateway-ip ipv4", route_set_evpn_gateway_ip,
	route_set_evpn_gateway_ip_compile, route_set_evpn_gateway_ip_free};

/* 【中文补充】route_set_evpn_gateway_ip_ipv6_cmd：规则名 "evpn gateway-ip ipv6"，
 * 同样非 static。ipv4/ipv6 共用一套回调，靠 match 时校验地址家族区分。 */
/* Route map commands for set evpn gateway-ip ipv6. */
struct route_map_rule_cmd route_set_evpn_gateway_ip_ipv6_cmd = {
	"evpn gateway-ip ipv6", route_set_evpn_gateway_ip,
	route_set_evpn_gateway_ip_compile, route_set_evpn_gateway_ip_free};

/* 【中文补充：match source-vrf 规则（VRF route leak 场景）】
 * 语法：match source-vrf VRF-NAME
 * 语义：VRF 路由泄漏(route leak)时，匹配“该路由来源于哪个 VRF”。
 * 逻辑：规则对象是 VRF 名字符串(compile 里 XSTRDUP)。
 *   - 名字为 "n/a" 视为不匹配（NOMATCH）；
 *   - 要求 path->extra->vrfleak->bgp_orig 存在，否则 NOMATCH；
 *   - 比较 vrf_id_to_name(bgp_orig->vrf_id) 与规则名是否相等（strncmp VRF_NAMSIZ）。
 * 注意：用 strncmp 限定 VRF_NAMSIZ 长度比较，注意 VRF 名字长度上限语义。 */
/* Route map commands for VRF route leak with source vrf matching */
static enum route_map_cmd_result_t
route_match_vrl_source_vrf(void *rule, const struct prefix *prefix,
			   void *object)
{
	struct bgp_path_info *path;
	char *vrf_name;

	vrf_name = rule;
	path = (struct bgp_path_info *)object;

	if (strncmp(vrf_name, "n/a", VRF_NAMSIZ) == 0)
		return RMAP_NOMATCH;

	if (path->extra == NULL || path->extra->vrfleak == NULL ||
	    path->extra->vrfleak->bgp_orig == NULL)
		return RMAP_NOMATCH;

	if (strncmp(vrf_name,
		    vrf_id_to_name(path->extra->vrfleak->bgp_orig->vrf_id),
		    VRF_NAMSIZ) == 0)
		return RMAP_MATCH;

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_vrl_source_vrf_compile：把 VRF 名字存一份拷贝。 */
static void *route_match_vrl_source_vrf_compile(const char *arg)
{
	uint8_t *vrf_name = NULL;

	vrf_name = XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);

	return vrf_name;
}

/* 【中文补充】route_match_vrl_source_vrf_free：释放 VRF 名字符串。 */
/* Free route map's compiled `route-type' value. */
static void route_match_vrl_source_vrf_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_vrl_source_vrf_cmd：规则名 "source-vrf"。 */
static const struct route_map_rule_cmd route_match_vrl_source_vrf_cmd = {
	"source-vrf",
	route_match_vrl_source_vrf,
	route_match_vrl_source_vrf_compile,
	route_match_vrl_source_vrf_free
};

/* 【中文补充：match alias 规则】
 * 语法：match alias COMMUNITY-ALIAS
 * 语义：把路由携带的 community 字符串按空格拆开，逐个查“community 别名”表，
 *       只要有一个 community 能解析出等于给定别名的，即 MATCH。
 * 依赖 bgp_community_alias 模块；规则对象是别名字符串。
 * 注意：算法先把 community 串 split 再逐个 bgp_community_alias_find——见下方实现。 */
/* `match alias` */
static enum route_map_cmd_result_t
route_match_alias(void *rule, const struct prefix *prefix, void *object)
{
	char *alias = rule;
	struct bgp_path_info *path = object;
	char **communities;
	int num;
	bool found;

	if (bgp_attr_get_community(path->attr)) {
		found = false;
		frrstr_split(bgp_attr_get_community(path->attr)->str, " ",
			     &communities, &num);
		for (int i = 0; i < num; i++) {
			const char *com2alias =
				bgp_community2alias(communities[i]);
			if (!found && strcmp(alias, com2alias) == 0)
				found = true;
			XFREE(MTYPE_TMP, communities[i]);
		}
		XFREE(MTYPE_TMP, communities);
		if (found)
			return RMAP_MATCH;
	}

	if (bgp_attr_get_lcommunity(path->attr)) {
		found = false;
		frrstr_split(bgp_attr_get_lcommunity(path->attr)->str, " ",
			     &communities, &num);
		for (int i = 0; i < num; i++) {
			const char *com2alias =
				bgp_community2alias(communities[i]);
			if (!found && strcmp(alias, com2alias) == 0)
				found = true;
			XFREE(MTYPE_TMP, communities[i]);
		}
		XFREE(MTYPE_TMP, communities);
		if (found)
			return RMAP_MATCH;
	}

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_alias_compile：存别名字符串。 */
static void *route_match_alias_compile(const char *arg)
{

	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_match_alias_free：释放别名字符串。 */
static void route_match_alias_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_alias_cmd：规则名 "alias"。 */
static const struct route_map_rule_cmd route_match_alias_cmd = {
	"alias", route_match_alias, route_match_alias_compile,
	route_match_alias_free};

/* 【中文补充：match local-preference 规则】
 * 语法：match local-preference LOCAL-PREF
 * 语义：判断路由的本地优先级(path->attr->local_pref)是否等于给定值（严格相等）。
 * 规则对象是编译好的 uint32_t。注意本规则**不支持 + / - 语义**（那属于 set）。 */
/* `match local-preference LOCAL-PREF' */

/* Match function return 1 if match is success else return zero. */
static enum route_map_cmd_result_t
route_match_local_pref(void *rule, const struct prefix *prefix, void *object)
{
	uint32_t *local_pref;
	struct bgp_path_info *path;

	local_pref = rule;
	path = object;

	if (path->attr->local_pref == *local_pref)
		return RMAP_MATCH;
	else
		return RMAP_NOMATCH;
}

/* 【中文补充】route_match_local_pref_compile：用 strtoul 解析并校验（残留字符/errno/
 * 超 UINT32_MAX 任一命中即 NULL），存入 uint32_t。 */
/*
 * Route map `match local-preference' match statement.
 * `arg' is local-pref value
 */
static void *route_match_local_pref_compile(const char *arg)
{
	uint32_t *local_pref;
	char *endptr = NULL;
	unsigned long tmpval;

	errno = 0;
	tmpval = strtoul(arg, &endptr, 10);
	if (*endptr != '\0' || errno || tmpval > UINT32_MAX)
		return NULL;

	local_pref = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(uint32_t));

	*local_pref = tmpval;
	return local_pref;
}

/* 【中文补充】route_match_local_pref_free：释放 uint32_t 规则值。 */
/* Free route map's compiled `match local-preference' value. */
static void route_match_local_pref_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_local_pref_cmd：规则名 "local-preference"。 */
/* Route map commands for metric matching. */
static const struct route_map_rule_cmd route_match_local_pref_cmd = {
	"local-preference",
	route_match_local_pref,
	route_match_local_pref_compile,
	route_match_local_pref_free
};

/* 【中文补充：match metric 规则】
 * 语法：match metric METRIC
 * 语义：判断路由 MED(path->attr->med) 是否等于给定值。
 * 复用通用取值机制：compile/free 直接用 route_value_compile / route_value_free，
 * match 时调用 route_value_match——因此 rtt/igp/aigp 这类动态来源在 match 里不生效
 * （route_value_match 只认字面量）。 */
/* `match metric METRIC' */

/* Match function return 1 if match is success else return zero. */
static enum route_map_cmd_result_t
route_match_metric(void *rule, const struct prefix *prefix, void *object)
{
	struct rmap_value *rv;
	struct bgp_path_info *path;

	rv = rule;
	path = object;
	return route_value_match(rv, path->attr->med);
}

/* 【中文补充】route_match_metric_cmd：规则名 "metric"，compile/free 复用通用取值函数。 */
/* Route map commands for metric matching. */
static const struct route_map_rule_cmd route_match_metric_cmd = {
	"metric",
	route_match_metric,
	route_value_compile,
	route_value_free,
};

/* 【中文补充：match as-path 规则】
 * 语法：match as-path AS-PATH-ACCESS-LIST-NAME
 * 语义：用一条 as-path 过滤器列表(as_list)匹配路由的 AS_PATH。
 * 规则对象是列表名字符串；运行时 as_list_lookup 查表（不存在即 NOMATCH），
 * 再用 as_list_apply 判定；结果为 AS_FILTER_DENY 则 NOMATCH，否则 MATCH。 */
/* `match as-path ASPATH' */

/* Match function for as-path match.  I assume given object is */
static enum route_map_cmd_result_t
route_match_aspath(void *rule, const struct prefix *prefix, void *object)
{

	struct as_list *as_list;
	struct bgp_path_info *path;

	as_list = as_list_lookup((char *)rule);
	if (as_list == NULL)
		return RMAP_NOMATCH;

	path = object;

	/* Perform match. */
	return ((as_list_apply(as_list, path->attr->aspath) == AS_FILTER_DENY)
			? RMAP_NOMATCH
			: RMAP_MATCH);
}

/* 【中文补充】route_match_aspath_compile：存 as-path 列表名。 */
/* Compile function for as-path match. */
static void *route_match_aspath_compile(const char *arg)
{
	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_match_aspath_free：释放列表名。 */
/* Compile function for as-path match. */
static void route_match_aspath_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_aspath_cmd：规则名 "as-path"。 */
/* Route map commands for aspath matching. */
static const struct route_map_rule_cmd route_match_aspath_cmd = {
	"as-path",
	route_match_aspath,
	route_match_aspath_compile,
	route_match_aspath_free
};

/* 【中文补充：match as-path-count 规则】
 * 语法：match as-path-count N
 * 语义：判断路由 AS_PATH 里的 AS 个数(path->attr->aspath->count)是否 <= N。
 * 规则对象是 uint16_t 上限。注意：这里直接引用 aspath->count 字段（收到 update 时
 * 预先算好的 AS_SEQUENCE 计数），不重新遍历。 */
/* `match as-path-count' */

/* Match function should return :
 * - RMAP_MATCH if the bgp update aspath  count
 * is less or equal to the configured limit.
 * - RMAP_NOMATCH if the aspath count is greater than the
 * configured limit.
 */
static enum route_map_cmd_result_t route_match_aspath_count(void *rule, const struct prefix *prefix,
							    void *object)
{
	struct bgp_path_info *path = object;
	uint16_t count = 0;
	uint16_t *limit_rule = rule;

	count = path->attr->aspath->count;

	if (count <= *limit_rule)
		return RMAP_MATCH;

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_aspath_count_compile：解析上限存入 uint16_t。 */
/* Route map `as-path-count' match statement. */
static void *route_match_aspath_count_compile(const char *arg)
{
	uint16_t *count = NULL;
	char *end = NULL;

	count = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(uint16_t));
	*count = strtoul(arg, &end, 10);
	if (*end != '\0') {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, count);
		return NULL;
	}
	return count;
}

/* 【中文补充】route_match_aspath_count_free：释放上限值。 */
/* Free route map's compiled `as-path-count' value. */
static void route_match_aspath_count_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_aspath_count_cmd：规则名 "as-path-count"。 */
/* Route map commands for as-path-count matching. */
static const struct route_map_rule_cmd route_match_aspath_count_cmd = {
	"as-path-count", route_match_aspath_count, route_match_aspath_count_compile,
	route_match_aspath_count_free
};

/* 【中文补充：match community / large-community / extcommunity 共用的规则对象】
 * 语法（以 community 为例）：match community NAME [exact-match]
 * struct rmap_community 字段：
 *   name      —— 社区列表(community-list)的名字。
 *   name_hash —— name 的哈希键（bgp_clist_hash_key 预计算），用于在全局
 *                community-list 哈希表里快速查找，避免每次 match 重复算哈希。
 *   exact     —— 精确匹配标志（命令后缀 "exact-match" 置位），语义见下。
 *   any       —— 任一匹配标志（后接其它后缀时置位）。
 * 匹配语义三态（见 route_match_community）：
 *   exact=true  → 路由社区集合与列表**完全相等**才命中(community_list_exact_match)；
 *   any=true    → 路由只要含列表中**任意一条**即命中；且此时“路由根本没有社区”
 *                 会被判为 RMAP_OKAY（放行，不否决）；
 *   both=false  → 默认“包含”匹配(community_list_match)。
 * 注意：名字与后缀之间靠 strchr(arg,' ') 切分——后缀首字符 'e' 视为 exact，否则 any。 */
/* `match community COMMUNIY' */
struct rmap_community {
	char *name;
	uint32_t name_hash;
	bool exact;
	bool any;
};

/* 【函数】route_match_community —— 用 community-list 匹配路由的标准 community。
 * 参数：rule=struct rmap_community；prefix 未用；object=bgp_path_info。
 * 逻辑：先按 (name,name_hash) 在 bgp_clist 里查 master 列表（不存在→NOMATCH）；
 *       再按 exact / any / 默认三种语义分派（详见上面结构注释）。
 * 返回：MATCH / NOMATCH / OKAY（仅 any 且路由无 community 时）三态。 */
/* Match function for community match. */
static enum route_map_cmd_result_t
route_match_community(void *rule, const struct prefix *prefix, void *object)
{
	struct community_list *list;
	struct bgp_path_info *path;
	struct rmap_community *rcom = rule;

	path = object;
	rcom = rule;

	list = community_list_lookup(bgp_clist, rcom->name, rcom->name_hash,
				     COMMUNITY_LIST_MASTER);
	if (!list)
		return RMAP_NOMATCH;

	if (rcom->exact) {
		if (community_list_exact_match(
			    bgp_attr_get_community(path->attr), list))
			return RMAP_MATCH;
	} else if (rcom->any) {
		if (!bgp_attr_get_community(path->attr))
			return RMAP_OKAY;
		if (community_list_any_match(bgp_attr_get_community(path->attr),
					     list))
			return RMAP_MATCH;
	} else {
		if (community_list_match(bgp_attr_get_community(path->attr),
					 list))
			return RMAP_MATCH;
	}

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_community_compile：解析 "NAME [exact-match|any]"，见结构注释。
 * 注意：只认后缀首字母（'e'→exact，其它→any），这是依赖命令词法保证简写的做法。 */
/* Compile function for community match. */
static void *route_match_community_compile(const char *arg)
{
	struct rmap_community *rcom;
	int len;
	char *p;

	rcom = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct rmap_community));

	p = strchr(arg, ' ');
	if (p) {
		len = p - arg;
		rcom->name = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, len + 1);
		memcpy(rcom->name, arg, len);
		p++;
		if (*p == 'e')
			rcom->exact = true;
		else
			rcom->any = true;
	} else {
		rcom->name = XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
		rcom->exact = false;
		rcom->any = false;
	}

	rcom->name_hash = bgp_clist_hash_key(rcom->name);
	return rcom;
}

/* 【中文补充】route_match_community_free：先释放 name，再释放结构体。 */
/* Compile function for community match. */
static void route_match_community_free(void *rule)
{
	struct rmap_community *rcom = rule;

	XFREE(MTYPE_ROUTE_MAP_COMPILED, rcom->name);
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rcom);
}

/* 【中文补充】route_match_get_community_key：给框架的“依赖通知”提供规则键。
 * 当 route-map 引用了某个社区列表时，框架据此把该 route-map 登记为对该列表名的
 * “依赖者”；列表内容变化时通知相关 route-map 重算。返回的就是列表名(name)。
 * 说明：exact/any 等后缀不影响键，键始终只是列表名。 */
/*
 * In routemap processing there is a need to add the
 * name as a rule_key in the dependency table. Routemap
 * lib is unaware of rule_key when exact-match clause
 * is in use. routemap lib uses the compiled output to
 * get the rule_key value.
 */
static void *route_match_get_community_key(void *rule)
{
	struct rmap_community *rcom;

	rcom = rule;
	return rcom->name;
}


/* 【中文补充】route_match_community_cmd：规则名 "community"。注意它比前面的规则多
 * 一个字段——第 5 个是 get_key 回调（用于依赖表），见 struct route_map_rule_cmd。 */
/* Route map commands for community matching. */
static const struct route_map_rule_cmd route_match_community_cmd = {
	"community",
	route_match_community,
	route_match_community_compile,
	route_match_community_free,
	route_match_get_community_key
};

/* 【函数】route_match_lcommunity —— 用 large-community-list 匹配路由的 large-community。
 * 结构、三态语义(exact / any / 默认)与 route_match_community 完全对称，区别：
 *   - 查表类型是 LARGE_COMMUNITY_LIST_MASTER；
 *   - 取属性用 bgp_attr_get_lcommunity；
 *   - 调用 lcommunity_list_exact_match / any_match / match。
 * 规则对象同为 struct rmap_community。 */
/* Match function for lcommunity match. */
static enum route_map_cmd_result_t
route_match_lcommunity(void *rule, const struct prefix *prefix, void *object)
{
	struct community_list *list;
	struct bgp_path_info *path;
	struct rmap_community *rcom = rule;

	path = object;

	list = community_list_lookup(bgp_clist, rcom->name, rcom->name_hash,
				     LARGE_COMMUNITY_LIST_MASTER);
	if (!list)
		return RMAP_NOMATCH;

	if (rcom->exact) {
		if (lcommunity_list_exact_match(
			    bgp_attr_get_lcommunity(path->attr), list))
			return RMAP_MATCH;
	} else if (rcom->any) {
		if (!bgp_attr_get_lcommunity(path->attr))
			return RMAP_OKAY;
		if (lcommunity_list_any_match(bgp_attr_get_lcommunity(path->attr),
					      list))
			return RMAP_MATCH;
	} else {
		if (lcommunity_list_match(bgp_attr_get_lcommunity(path->attr),
					  list))
			return RMAP_MATCH;
	}

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_lcommunity_compile：解析 large-community 列表名与后缀，
 * 逻辑与 route_match_community_compile 完全一致。 */
/* Compile function for community match. */
static void *route_match_lcommunity_compile(const char *arg)
{
	struct rmap_community *rcom;
	int len;
	char *p;

	rcom = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct rmap_community));

	p = strchr(arg, ' ');
	if (p) {
		len = p - arg;
		rcom->name = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, len + 1);
		memcpy(rcom->name, arg, len);
		p++;
		if (*p == 'e')
			rcom->exact = true;
		else
			rcom->any = true;
	} else {
		rcom->name = XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
		rcom->exact = false;
		rcom->any = false;
	}

	rcom->name_hash = bgp_clist_hash_key(rcom->name);
	return rcom;
}

/* 【中文补充】route_match_lcommunity_free：释放 name 与结构体。 */
/* Compile function for community match. */
static void route_match_lcommunity_free(void *rule)
{
	struct rmap_community *rcom = rule;

	XFREE(MTYPE_ROUTE_MAP_COMPILED, rcom->name);
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rcom);
}

/* 【中文补充】route_match_lcommunity_cmd：规则名 "large-community"，同样带 get_key 回调。 */
/* Route map commands for community matching. */
static const struct route_map_rule_cmd route_match_lcommunity_cmd = {
	"large-community",
	route_match_lcommunity,
	route_match_lcommunity_compile,
	route_match_lcommunity_free,
	route_match_get_community_key
};


/* 【函数】route_match_ecommunity —— 用 extcommunity-list 匹配路由的扩展 community。
 * 与 community/lcommunity 结构对称，区别：查表类型 EXTCOMMUNITY_LIST_MASTER、
 * 取属性用 bgp_attr_get_ecommunity、调用 ecommunity_list_exact/any/match。
 * 注意：这里只匹配普通(非 IPv6)extcommunity；IPv6 类另有其属性字段。 */
/* Match function for extcommunity match. */
static enum route_map_cmd_result_t
route_match_ecommunity(void *rule, const struct prefix *prefix, void *object)
{
	struct community_list *list;
	struct bgp_path_info *path;
	struct rmap_community *rcom = rule;

	path = object;

	list = community_list_lookup(bgp_clist, rcom->name, rcom->name_hash,
				     EXTCOMMUNITY_LIST_MASTER);
	if (!list)
		return RMAP_NOMATCH;

	if (rcom->exact) {
		if (ecommunity_list_exact_match(bgp_attr_get_ecommunity(path->attr), list))
			return RMAP_MATCH;
	} else if (rcom->any) {
		if (!bgp_attr_get_ecommunity(path->attr))
			return RMAP_OKAY;
		if (ecommunity_list_any_match(bgp_attr_get_ecommunity(path->attr), list))
			return RMAP_MATCH;
	} else {
		if (ecommunity_list_match(bgp_attr_get_ecommunity(path->attr), list))
			return RMAP_MATCH;
	}

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_ecommunity_compile：解析 extcommunity 列表名与后缀，逻辑同上。 */
/* Compile function for extcommunity match. */
static void *route_match_ecommunity_compile(const char *arg)
{
	struct rmap_community *rcom;
	int len;
	char *p;

	rcom = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct rmap_community));

	p = strchr(arg, ' ');
	if (p) {
		len = p - arg;
		rcom->name = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, len + 1);
		memcpy(rcom->name, arg, len);
		p++;
		if (*p == 'e')
			rcom->exact = true;
		else
			rcom->any = true;
	} else {
		rcom->name = XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
		rcom->exact = false;
		rcom->any = false;
	}

	rcom->name_hash = bgp_clist_hash_key(rcom->name);
	return rcom;
}

/* 【中文补充】route_match_ecommunity_free：释放 name 与结构体。 */
/* Compile function for extcommunity match. */
static void route_match_ecommunity_free(void *rule)
{
	struct rmap_community *rcom = rule;

	XFREE(MTYPE_ROUTE_MAP_COMPILED, rcom->name);
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rcom);
}

/* 【中文补充】route_match_ecommunity_cmd：规则名 "extcommunity"。注意它**没有**
 * get_key 回调（与 community/large-community 不同）。 */
/* Route map commands for community matching. */
static const struct route_map_rule_cmd route_match_ecommunity_cmd = {
	"extcommunity",
	route_match_ecommunity,
	route_match_ecommunity_compile,
	route_match_ecommunity_free
};

/* 【中文补充】旧的 match nlri / set nlri 命令已被 address-family 取代，这里只留注释。 */
/* `match nlri` and `set nlri` are replaced by `address-family ipv4`
   and `address-family vpnv4'.  */

/* 【中文补充：match origin 规则】
 * 语法：match origin (igp|egp|incomplete)
 * 语义：判断路由的 ORIGIN 属性(path->attr->origin)是否等于给定值。
 * 规则对象是 1 字节的 origin 编码：igp→0、egp→1、其它(incomplete)→2（对应 RFC 4271
 * 的 ORIGIN 取值 IGP=0/EGP=1/INCOMPLETE=2）。 */
/* `match origin' */
static enum route_map_cmd_result_t
route_match_origin(void *rule, const struct prefix *prefix, void *object)
{
	uint8_t *origin;
	struct bgp_path_info *path;

	origin = rule;
	path = object;

	if (path->attr->origin == *origin)
		return RMAP_MATCH;

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_origin_compile：把 igp/egp/incomplete 压成 1 字节。 */
static void *route_match_origin_compile(const char *arg)
{
	uint8_t *origin;

	origin = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(uint8_t));

	if (strcmp(arg, "igp") == 0)
		*origin = 0;
	else if (strcmp(arg, "egp") == 0)
		*origin = 1;
	else
		*origin = 2;

	return origin;
}

/* 【中文补充】route_match_origin_free：释放 1 字节 origin。 */
/* Free route map's compiled `ip address' value. */
static void route_match_origin_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_origin_cmd：规则名 "origin"。 */
/* Route map commands for origin matching. */
static const struct route_map_rule_cmd route_match_origin_cmd = {
	"origin",
	route_match_origin,
	route_match_origin_compile,
	route_match_origin_free
};

/* 【中文补充：match probability 规则】
 * 语法：match probability PERCENT
 * 语义：按概率命中该条目——用 frr_weak_random() 取一个 [0,RAND_MAX] 的随机数，
 *       若小于阈值则 MATCH。规则对象是编译好的 long 阈值。
 * 边界：PERCENT=0 时阈值 0（永不命中，随机数不会 <0）；PERCENT=100 时阈值取 RAND_MAX
 *       （恒命中）；其余阈值 = RAND_MAX/100*perc（注意整数运算的取舍）。
 * 注意：这是**带随机性**的 match，会随每次判断结果变化，调试时勿期待确定性。 */
/* match probability  { */

static enum route_map_cmd_result_t
route_match_probability(void *rule, const struct prefix *prefix, void *object)
{
	long r = frr_weak_random();

	switch (*(long *)rule) {
	case 0:
		break;
	case RAND_MAX:
		return RMAP_MATCH;
	default:
		if (r < *(long *)rule) {
			return RMAP_MATCH;
		}
	}

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_probability_compile：atoi 百分比并按上述规则换算阈值。 */
static void *route_match_probability_compile(const char *arg)
{
	long *lobule;
	unsigned perc;

	perc = atoi(arg);
	lobule = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(long));

	switch (perc) {
	case 0:
		*lobule = 0;
		break;
	case 100:
		*lobule = RAND_MAX;
		break;
	default:
		*lobule = RAND_MAX / 100 * perc;
	}

	return lobule;
}

/* 【中文补充】route_match_probability_free：释放阈值。 */
static void route_match_probability_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_probability_cmd：规则名 "probability"。 */
static const struct route_map_rule_cmd route_match_probability_cmd = {
	"probability",
	route_match_probability,
	route_match_probability_compile,
	route_match_probability_free
};

/* 【中文补充：match interface 规则】
 * 语法：match interface IFNAME
 * 语义：判断路由的下一跳出接口是否为指定接口——先按名字在 peer 所属 BGP 实例的 VRF
 *       里查 interface，再比较其 ifindex 与 path->attr->nh_ifindex。
 * 注意：路径/peer/bgp 任一为空都 NOMATCH；接口名不要求事先存在（运行时查）。 */
/* `match interface IFNAME' */
/* Match function should return 1 if match is success else return
   zero. */
static enum route_map_cmd_result_t
route_match_interface(void *rule, const struct prefix *prefix, void *object)
{
	struct interface *ifp;
	struct bgp_path_info *path;

	path = object;

	if (!path || !path->peer || !path->peer->bgp)
		return RMAP_NOMATCH;

	ifp = if_lookup_by_name((char *)rule, path->peer->bgp->vrf_id);

	if (ifp == NULL || ifp->ifindex != path->attr->nh_ifindex)
		return RMAP_NOMATCH;

	return RMAP_MATCH;
}

/* 【中文补充】route_match_interface_compile：存接口名字符串。 */
/* Route map `interface' match statement.  `arg' should be
   interface name. */
static void *route_match_interface_compile(const char *arg)
{
	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_match_interface_free：释放接口名。 */
/* Free route map's compiled `interface' value. */
static void route_match_interface_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_interface_cmd：规则名 "interface"。 */
/* Route map commands for ip address matching. */
static const struct route_map_rule_cmd route_match_interface_cmd = {
	"interface",
	route_match_interface,
	route_match_interface_compile,
	route_match_interface_free
};

/* } */

/* 【中文补充：match tag 规则】
 * 语法：match tag TAG
 * 语义：判断路由的 tag(path->attr->tag) 是否等于给定值（严格相等）。
 * 注意：compile/free 复用 lib 的 route_map_rule_tag_compile / route_map_rule_tag_free
 *       （不是本文件自带的），因为 tag 解析在通用层已实现。 */
/* `set ip next-hop IP_ADDRESS' */

/* Match function return 1 if match is success else return zero. */
static enum route_map_cmd_result_t
route_match_tag(void *rule, const struct prefix *prefix, void *object)
{
	route_tag_t *tag;
	struct bgp_path_info *path;

	tag = rule;
	path = object;

	return ((path->attr->tag == *tag) ? RMAP_MATCH : RMAP_NOMATCH);
}


/* 【中文补充】route_match_tag_cmd：规则名 "tag"，复用 lib 的 tag 编译/释放。 */
/* Route map commands for tag matching. */
static const struct route_map_rule_cmd route_match_tag_cmd = {
	"tag",
	route_match_tag,
	route_map_rule_tag_compile,
	route_map_rule_tag_free,
};

/* 【函数】route_set_srte_color —— set 侧规则：给路由设置 SR-TE 的 color 值。
 * 参数：rule=编译好的 uint32_t color；object=bgp_path_info。
 * 返回：恒返回 RMAP_OKAY（set 规则，不判命中）。
 * 逻辑：直接把 path->attr->srte_color 设成给定值。 */
static enum route_map_cmd_result_t
route_set_srte_color(void *rule, const struct prefix *prefix, void *object)
{
	uint32_t *srte_color = rule;
	struct bgp_path_info *path;

	path = object;

	path->attr->srte_color = *srte_color;

	return RMAP_OKAY;
}

/* 【中文补充】route_set_srte_color_compile：atoi 解析 color 存入 uint32_t。 */
/* Route map `sr-te color' compile function */
static void *route_set_srte_color_compile(const char *arg)
{
	uint32_t *color;

	color = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(uint32_t));
	*color = atoi(arg);

	return color;
}

/* 【中文补充】route_set_srte_color_free：释放 color。 */
/* Free route map's compiled `sr-te color' value. */
static void route_set_srte_color_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_set_srte_color_cmd：规则名 "sr-te color"，非 static（对外可见）。 */
/* Route map commands for sr-te color set. */
struct route_map_rule_cmd route_set_srte_color_cmd = {
	"sr-te color", route_set_srte_color, route_set_srte_color_compile,
	route_set_srte_color_free};

/* 【中文补充：set ip next-hop 的编译结果结构】
 * struct rmap_ip_nexthop_set 字段：
 *   address      —— 当命令给出具体 IPv4 地址时，指向该地址；否则为 NULL。
 *   peer_address —— 命令为 "peer-address" 时置 1：下一跳设成对端地址。
 *   unchanged    —— 命令为 "unchanged" 时置 1：保持下一跳不变（只打标记位）。
 * 三者互斥，见 route_set_ip_nexthop_compile 的解析。 */
/* Set nexthop to object.  object must be pointer to struct attr. */
struct rmap_ip_nexthop_set {
	struct in_addr *address;
	int peer_address;
	int unchanged;
};

/* 【函数】route_set_ip_nexthop —— set 侧规则：设置 IPv4 下一跳（set ip next-hop）。
 * 参数：rule=struct rmap_ip_nexthop_set；object=bgp_path_info。
 * 返回：恒 RMAP_OKAY（set 规则）。
 * 三分支（按 rins 的标志）：
 *   unchanged    —— 只置 BATTR_RMAP_NEXTHOP_UNCHANGED 标记位，不改地址（表示“保持原样”）。
 *   peer_address —— 下一跳设为对端地址：
 *       · 入方向(PEER_RMAP_TYPE_IN)且有对端 v4 地址：直接写 attr->nexthop 并置
 *         BGP_ATTR_NEXT_HOP；
 *       · 出方向(PEER_RMAP_TYPE_OUT)：置 BATTR_RMAP_NEXTHOP_PEER_ADDRESS 标记，
 *         地址清零，实际改写留到组包阶段(packet rewrite)完成。
 *   默认(=具体地址) —— 写 attr->nexthop、置 BATTR_RMAP_IPV4_NHOP_CHANGED，并同步给
 *       MP-BGP(MPLS VPN) 用的 mp_nexthop_global_in / mp_nexthop_len。
 * 注意：入方向分支里 peer->connection->su_remote 可能为空——代码已先判非空再解引用。 */
static enum route_map_cmd_result_t
route_set_ip_nexthop(void *rule, const struct prefix *prefix, void *object)
{
	struct rmap_ip_nexthop_set *rins = rule;
	struct bgp_path_info *path;
	struct peer *peer;

	if (prefix->family == AF_INET6)
		return RMAP_OKAY;

	path = object;
	peer = path->peer;

	if (rins->unchanged) {
		SET_FLAG(path->attr->rmap_change_flags,
			 BATTR_RMAP_NEXTHOP_UNCHANGED);
	} else if (rins->peer_address) {
		if ((CHECK_FLAG(peer->rmap_type, PEER_RMAP_TYPE_IN)) &&
		    peer->connection->su_remote &&
		    sockunion_family(peer->connection->su_remote) == AF_INET) {
			path->attr->nexthop.s_addr = sockunion2ip(peer->connection->su_remote);
			bgp_attr_set(path->attr, BGP_ATTR_NEXT_HOP);
		} else if (CHECK_FLAG(peer->rmap_type, PEER_RMAP_TYPE_OUT)) {
			/* The next hop value will be set as part of
			 * packet rewrite.  Set the flags here to indicate
			 * that rewrite needs to be done.
			 * Also, clear the value.
			 */
			SET_FLAG(path->attr->rmap_change_flags,
				 BATTR_RMAP_NEXTHOP_PEER_ADDRESS);
			path->attr->nexthop.s_addr = INADDR_ANY;
		}
	} else {
		/* Set next hop value. */
		bgp_attr_set(path->attr, BGP_ATTR_NEXT_HOP);
		path->attr->nexthop = *rins->address;
		SET_FLAG(path->attr->rmap_change_flags,
			 BATTR_RMAP_IPV4_NHOP_CHANGED);
		/* case for MP-BGP : MPLS VPN */
		path->attr->mp_nexthop_global_in = *rins->address;
		path->attr->mp_nexthop_len = sizeof(*rins->address);
	}

	return RMAP_OKAY;
}

/* 【中文补充】route_set_ip_nexthop_compile：解析参数三态——
 * "peer-address"→peer_address=1；"unchanged"→unchanged=1；否则按 inet_aton 解析成
 * 具体 IPv4 地址（失败释放并返回 NULL）。三个标志填入 rmap_ip_nexthop_set。 */
/* Route map `ip nexthop' compile function.  Given string is converted
   to struct in_addr structure. */
static void *route_set_ip_nexthop_compile(const char *arg)
{
	struct rmap_ip_nexthop_set *rins;
	struct in_addr *address = NULL;
	int peer_address = 0;
	int unchanged = 0;
	int ret;

	if (strcmp(arg, "peer-address") == 0)
		peer_address = 1;
	else if (strcmp(arg, "unchanged") == 0)
		unchanged = 1;
	else {
		address = XMALLOC(MTYPE_ROUTE_MAP_COMPILED,
				  sizeof(struct in_addr));
		ret = inet_aton(arg, address);

		if (ret == 0) {
			XFREE(MTYPE_ROUTE_MAP_COMPILED, address);
			return NULL;
		}
	}

	rins = XCALLOC(MTYPE_ROUTE_MAP_COMPILED,
		       sizeof(struct rmap_ip_nexthop_set));

	rins->address = address;
	rins->peer_address = peer_address;
	rins->unchanged = unchanged;

	return rins;
}

/* 【中文补充】route_set_ip_nexthop_free：先释放 address（为 NULL 也安全），再释放结构体。 */
/* Free route map's compiled `ip nexthop' value. */
static void route_set_ip_nexthop_free(void *rule)
{
	struct rmap_ip_nexthop_set *rins = rule;

	XFREE(MTYPE_ROUTE_MAP_COMPILED, rins->address);

	XFREE(MTYPE_ROUTE_MAP_COMPILED, rins);
}

/* 【中文补充】route_set_ip_nexthop_cmd：规则名 "ip next-hop"。 */
/* Route map commands for ip nexthop set. */
static const struct route_map_rule_cmd route_set_ip_nexthop_cmd = {
	"ip next-hop",
	route_set_ip_nexthop,
	route_set_ip_nexthop_compile,
	route_set_ip_nexthop_free
};

/* 【中文补充：set l3vpn next-hop encapsulation 规则】
 * 语法：set l3vpn next-hop encapsulation (gre|gretap)
 * 语义：给 L3VPN 路由的下一跳标记“允许的隧道封装类型”。规则对象仅含一个
 *       zebra_iftype 字段（ZEBRA_IF_GRE 或 ZEBRA_IF_GRETAP）。
 * 运行：match 时按类型置 path->attr->rmap_change_flags 的
 *       BATTR_RMAP_L3VPN_ACCEPT_GRE / _GRETAP 位，恒返回 RMAP_OKAY。 */
/* `set l3vpn next-hop encapsulation l3vpn gre' */

/* Set nexthop to object */
struct rmap_l3vpn_nexthop_encapsulation_set {
	enum zebra_iftype zif_type;
};

/* 【函数】route_set_l3vpn_nexthop_encapsulation —— set 侧规则实现，见上方规则注释。
 * 只置 rmap_change_flags 的封装接受位，不直接改下一跳地址。 */
static enum route_map_cmd_result_t
route_set_l3vpn_nexthop_encapsulation(void *rule, const struct prefix *prefix,
				      void *object)
{
	struct rmap_l3vpn_nexthop_encapsulation_set *rins = rule;
	struct bgp_path_info *path;

	path = object;

	if (rins->zif_type == ZEBRA_IF_GRE)
		SET_FLAG(path->attr->rmap_change_flags, BATTR_RMAP_L3VPN_ACCEPT_GRE);
	if (rins->zif_type == ZEBRA_IF_GRETAP)
		SET_FLAG(path->attr->rmap_change_flags, BATTR_RMAP_L3VPN_ACCEPT_GRETAP);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_l3vpn_nexthop_encapsulation_compile：参数为 "gretap" 则
 * ZEBRA_IF_GRETAP，否则(含 "gre")一律 ZEBRA_IF_GRE（IPv4/IPv6 由同一关键字覆盖）。 */
/* Route map `l3vpn nexthop encapsulation' compile function. */
static void *route_set_l3vpn_nexthop_encapsulation_compile(const char *arg)
{
	struct rmap_l3vpn_nexthop_encapsulation_set *rins;

	rins = XCALLOC(MTYPE_ROUTE_MAP_COMPILED,
		       sizeof(struct rmap_l3vpn_nexthop_encapsulation_set));

	/* GRE IPv4 and IPv6 modes are covered under gre keyword
	 * GRETAP IPv4 and IPv6 modes are covered under gretap keyword
	 */
	if (strmatch(arg, "gretap"))
		rins->zif_type = ZEBRA_IF_GRETAP;
	else
		rins->zif_type = ZEBRA_IF_GRE;

	return rins;
}

/* 【中文补充】route_set_l3vpn_nexthop_encapsulation_free：释放结构体。 */
/* Free route map's compiled `ip nexthop' value. */
static void route_set_l3vpn_nexthop_encapsulation_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_set_l3vpn_nexthop_encapsulation_cmd：规则名 "l3vpn next-hop encapsulation"。 */
/* Route map commands for l3vpn next-hop encapsulation set. */
static const struct route_map_rule_cmd
	route_set_l3vpn_nexthop_encapsulation_cmd = {
		"l3vpn next-hop encapsulation",
		route_set_l3vpn_nexthop_encapsulation,
		route_set_l3vpn_nexthop_encapsulation_compile,
		route_set_l3vpn_nexthop_encapsulation_free};

/* 【中文补充：set local-preference 规则】
 * 语法：set local-preference LOCAL-PREF（支持 +N / -N 语义，走 route_value_*）
 * 语义：设置本地优先级。基准值 locpref 的选取：
 *   先取 peer 所属 BGP 的默认 local_pref(bgp->default_local_pref)；
 *   若该路由已带 LOCAL_PREF 属性，则用其现值作为基准（以便 +/- 累加）。
 * 然后 route_value_adjust 计算新值，写回 attr->local_pref，并置 BGP_ATTR_LOCAL_PREF。 */

/* `set local-preference LOCAL_PREF' */

/* Set local preference. */
static enum route_map_cmd_result_t
route_set_local_pref(void *rule, const struct prefix *prefix, void *object)
{
	struct rmap_value *rv;
	struct bgp_path_info *path;
	uint32_t locpref = 0;

	/* Fetch routemap's rule information. */
	rv = rule;
	path = object;
	locpref = path->peer->bgp->default_local_pref;

	/* Set local preference value. */
	if (path->attr->local_pref)
		locpref = path->attr->local_pref;

	bgp_attr_set(path->attr, BGP_ATTR_LOCAL_PREF);
	path->attr->local_pref = route_value_adjust(rv, locpref, path);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_local_pref_cmd：规则名 "local-preference"，compile/free 复用通用取值。 */
/* Set local preference rule structure. */
static const struct route_map_rule_cmd route_set_local_pref_cmd = {
	"local-preference",
	route_set_local_pref,
	route_value_compile,
	route_value_free,
};

/* 【中文补充：set weight 规则】
 * 语法：set weight WEIGHT（支持 +N / -N，走 route_value_*）
 * 语义：设置 BGP 权值(attr->weight)。基准值固定传 0（weight 是本地私有、不累加现值），
 *       由 route_value_adjust 按动作算出结果。 */

/* `set weight WEIGHT' */

/* 【中文补充】route_set_weight：把 weight 设为 route_value_adjust(rv, 0, path) 的结果。 */
/* Set weight. */
static enum route_map_cmd_result_t
route_set_weight(void *rule, const struct prefix *prefix, void *object)
{
	struct rmap_value *rv;
	struct bgp_path_info *path;

	/* Fetch routemap's rule information. */
	rv = rule;
	path = object;

	/* Set weight value. */
	path->attr->weight = route_value_adjust(rv, 0, path);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_weight_cmd：规则名 "weight"。 */
/* Set local preference rule structure. */
static const struct route_map_rule_cmd route_set_weight_cmd = {
	"weight",
	route_set_weight,
	route_value_compile,
	route_value_free,
};

/* 【中文补充：set distance 规则】
 * 语法：set distance DISTANCE
 * 语义：设置管理距离(attr->distance)。注意此处直接取 rv->value（**不走
 *       route_value_adjust**），因此即使命令写了 +N / -N 也只当字面量用——这是
 *       本规则与其它数值型 set 的差异。 */

/* `set distance DISTANCE */
static enum route_map_cmd_result_t
route_set_distance(void *rule, const struct prefix *prefix, void *object)
{
	struct bgp_path_info *path = object;
	struct rmap_value *rv = rule;

	path->attr->distance = rv->value;

	return RMAP_OKAY;
}

/* 【中文补充】route_set_distance_cmd：规则名 "distance"。 */
/* set distance rule structure */
static const struct route_map_rule_cmd route_set_distance_cmd = {
	"distance",
	route_set_distance,
	route_value_compile,
	route_value_free,
};

/* 【中文补充：set metric 规则】
 * 语法：set metric METRIC（支持 +N / -N，走 route_value_*）
 * 语义：设置 MED(attr->med)。基准值选该路由已有的 MED（若带 MULTI_EXIT_DISC 属性），
 *       否则 0，再由 route_value_adjust 计算；最后用 bgp_attr_set_med 写回（会顺带
 *       处理属性存在性）。 */
/* `set metric METRIC' */

/* 【中文补充】route_set_metric：见上方规则注释。 */
/* Set metric to attribute. */
static enum route_map_cmd_result_t
route_set_metric(void *rule, const struct prefix *prefix, void *object)
{
	struct rmap_value *rv;
	struct bgp_path_info *path;
	uint32_t med = 0;

	/* Fetch routemap's rule information. */
	rv = rule;
	path = object;

	if (bgp_attr_exists(path->attr, BGP_ATTR_MULTI_EXIT_DISC))
		med = path->attr->med;

	bgp_attr_set_med(path->attr, route_value_adjust(rv, med, path));

	return RMAP_OKAY;
}

/* 【中文补充】route_set_metric_cmd：规则名 "metric"。 */
/* Set metric rule structure. */
static const struct route_map_rule_cmd route_set_metric_cmd = {
	"metric",
	route_set_metric,
	route_value_compile,
	route_value_free,
};

/* 【中文补充：set table 规则】
 * 语法：set table (1-4294967295)
 * 语义：把路由挂到指定内核路由表(attr->rmap_table_id)。与 distance 一样直接取
 *       rv->value，不累加。 */

/* `set table (1-4294967295)' */

/* 【中文补充】route_set_table_id：设置 rmap_table_id。 */
static enum route_map_cmd_result_t
route_set_table_id(void *rule, const struct prefix *prefix,

		   void *object)
{
	struct rmap_value *rv;
	struct bgp_path_info *path;

	/* Fetch routemap's rule information. */
	rv = rule;
	path = object;

	path->attr->rmap_table_id = rv->value;

	return RMAP_OKAY;
}

/* 【中文补充】route_set_table_id_cmd：规则名 "table"。 */
/* Set table_id rule structure. */
static const struct route_map_rule_cmd route_set_table_id_cmd = {
	"table",
	route_set_table_id,
	route_value_compile,
	route_value_free
};

/* 【中文补充：set as-path prepend 规则】
 * 语法：set as-path prepend AS-PATH 或 set as-path prepend last-as (1-10)
 * 语义：在 AS_PATH 前面追加 AS 号。
 * 【规则对象的重载技巧——阅读要点】
 *   compile 有两种返回：当命令是 "last-as N" 时返回整数 N(1..10) 本身（当作指针
 *   存进 void*，靠 (uintptr_t)rule > 10 来区分）；否则返回一条编译好的 aspath 对象。
 *   因此 match 里用 `(uintptr_t)rule > 10` 判断是哪一种：
 *     · >10（aspath 指针）：调 aspath_prepend 把整条 aspath 追加到最前；
 *     · <=10（last-as 计数）：取当前 AS_PATH 最左 AS(aspath_leftmost)，用
 *       aspath_add_seq_n 把它重复 N 次追加。
 *   refcnt 语义：若现有 aspath 被多个属性共享(refcnt 非 0)则先 aspath_dup 出私有副本，
 *   否则可直接原地改。
 * 注意：这依赖“小整数不可能是合法堆指针”的假设（>10 阈值即为此）。 */

/* `set as-path prepend ASPATH' */

/* For AS path prepend mechanism. */
static enum route_map_cmd_result_t
route_set_aspath_prepend(void *rule, const struct prefix *prefix, void *object)
{
	struct aspath *aspath;
	struct aspath *new;
	struct bgp_path_info *path;

	path = object;

	if (path->attr->aspath->refcnt)
		new = aspath_dup(path->attr->aspath);
	else
		new = path->attr->aspath;

	if ((uintptr_t)rule > 10) {
		aspath = rule;
		aspath_prepend(aspath, new);
	} else {
		as_t as = aspath_leftmost(new);
		if (as)
			new = aspath_add_seq_n(new, as, (uintptr_t)rule);
	}

	path->attr->aspath = new;

	return RMAP_OKAY;
}

/* 【中文补充】route_set_aspath_prepend_compile：先试 "last-as N"（N∈1..10，返回整数当
 * 指针）；否则交给 route_aspath_compile 得到 aspath 对象。 */
static void *route_set_aspath_prepend_compile(const char *arg)
{
	unsigned int num;

	if (sscanf(arg, "last-as %u", &num) == 1 && num > 0 && num <= 10)
		return (void *)(uintptr_t)num;

	return route_aspath_compile(arg);
}

/* 【中文补充】route_set_aspath_prepend_free：仅当 rule 是 aspath 指针(>10)才释放，
 * 小整数(last-as 计数)不释放——与 compile 的重载约定对应。 */
static void route_set_aspath_prepend_free(void *rule)
{
	if ((uintptr_t)rule > 10)
		route_aspath_free(rule);
}


/* 【中文补充】route_set_aspath_prepend_cmd：规则名 "as-path prepend"。 */
/* Set as-path prepend rule structure. */
static const struct route_map_rule_cmd route_set_aspath_prepend_cmd = {
	"as-path prepend",
	route_set_aspath_prepend,
	route_set_aspath_prepend_compile,
	route_set_aspath_prepend_free,
};

/* 【函数】route_aspath_exclude_compile —— 编译 set as-path exclude 的参数。
 * 参数三种形态：
 *   "all"                     → exclude_all=true（排除全部 AS）；
 *   "as-path-access-list NAME"→ 取出列表名，查 as_list；存在则挂到该列表的
 *                                exclude_rule 链上，不存在则登记为 orphan（孤儿，
 *                                列表后建时再认领）；
 *   其余                      → 当作 AS 号串，str2aspath 存进 ase->aspath。
 * 返回：struct aspath_exclude *。 */
static void *route_aspath_exclude_compile(const char *arg)
{
	struct aspath_exclude *ase;
	struct as_list *aux_aslist;
	const char *str = arg;
	static const char asp_acl[] = "as-path-access-list";

	ase = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct aspath_exclude));
	if (strmatch(str, "all"))
		ase->exclude_all = true;
	else if (!strncmp(str, asp_acl, strlen(asp_acl))) {
		str += strlen(asp_acl);
		while (*str == ' ')
			str++;
		ase->exclude_aspath_acl_name = XSTRDUP(MTYPE_TMP, str);
		aux_aslist = as_list_lookup(str);
		if (!aux_aslist)
			/* new orphan filter */
			as_exclude_set_orphan(ase);
		else
			as_list_list_add_head(&aux_aslist->exclude_rule, ase);

		ase->exclude_aspath_acl = aux_aslist;
	} else
		ase->aspath = aspath_str2aspath(str, bgp_get_asnotation(NULL));

	return ase;
}

/* 【中文补充】route_aspath_exclude_free：按归属关系摘引用——若挂在某个 as_list 上
 * 就从链表删除；若是 orphan 则按 orphan 方式移除；最后释放内部 aspath 与列表名。 */
static void route_aspath_exclude_free(void *rule)
{
	struct aspath_exclude *ase = rule;
	struct as_list *acl;

	/* manage references to that rule*/
	if (ase->exclude_aspath_acl) {
		acl = ase->exclude_aspath_acl;
		as_list_list_del(&acl->exclude_rule, ase);
	} else if (ase->exclude_aspath_acl_name) {
		/* no ref to acl, this aspath exclude is orphan */
		as_exclude_remove_orphan(ase);
	}

	aspath_free(ase->aspath);
	if (ase->exclude_aspath_acl_name)
		XFREE(MTYPE_TMP, ase->exclude_aspath_acl_name);
	XFREE(MTYPE_ROUTE_MAP_COMPILED, ase);
}

/* 【函数】route_set_aspath_exclude —— 从 AS_PATH 中剔除指定 AS。
 * 参数：rule=struct aspath_exclude；object=bgp_path_info。
 * 返回：正常 RMAP_OKAY；非 EBGP peer 时打警告并返回 RMAP_NOOP（表示“本规则不适用，
 *       跳过”——注意这是 NOOP 而非 NOMATCH，不会否决该条目）。
 * 逻辑：按 ase 的三种配置分派——具体 aspath / exclude_all / 按 aspath-acl。
 * refcnt 处理同 prepend：共享则先 dup。
 * 注意：若三者都没设置(else 分支)，会 aspath_free(new_path) 把副本丢掉、不改 AS_PATH。 */
/* For ASN exclude mechanism.
 * Iterate over ASns requested and filter them from the given AS_PATH one by
 * one.
 * Make a deep copy of existing AS_PATH, but for the first ASn only.
 */
static enum route_map_cmd_result_t
route_set_aspath_exclude(void *rule, const struct prefix *dummy, void *object)
{
	struct aspath *new_path;
	struct bgp_path_info *path;
	struct aspath_exclude *ase = rule;

	path = object;

	if (path->peer->sort != BGP_PEER_EBGP) {
		zlog_warn(
			"`set as-path exclude` is supported only for EBGP peers");
		return RMAP_NOOP;
	}

	if (path->attr->aspath->refcnt)
		new_path = aspath_dup(path->attr->aspath);
	else
		new_path = path->attr->aspath;

	if (ase->aspath)
		path->attr->aspath =
			aspath_filter_exclude(new_path, ase->aspath);
	else if (ase->exclude_all)
		path->attr->aspath = aspath_filter_exclude_all(new_path);
	else if (ase->exclude_aspath_acl)
		path->attr->aspath =
			aspath_filter_exclude_acl(new_path,
						  ase->exclude_aspath_acl);
	else
		aspath_free(new_path);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_aspath_exclude_cmd：规则名 "as-path exclude"。 */
/* Set ASn exclude rule structure. */
static const struct route_map_rule_cmd route_set_aspath_exclude_cmd = {
	"as-path exclude",
	route_set_aspath_exclude,
	route_aspath_exclude_compile,
	route_aspath_exclude_free,
};

/* 【中文补充：set as-path replace 规则】
 * 语法：set as-path replace (ASN|any|as-path-access-list NAME) [ASN]
 * 语义：把 AS_PATH 中匹配的 AS 替换成新 ASN。
 * 规则对象只是原始参数字符串；真正的解析在 match 时进行（因为替换目标 ASN 可能
 * 依赖运行时状态：configured_asn 默认取 peer 的 change_local_as / local_as）。
 * 三种模式：
 *   - 有 as-path-access-list：用 ACL 筛出待替换的 AS，调 aspath_replace_regex_asn；
 *   - 关键字 "any"：替换所有 AS，调 aspath_replace_all_asn；
 *   - 具体 ASN：替换指定 AS，调 aspath_replace_specific_asn。
 * 返回：失败走 end_ko 返回 RMAP_NOOP（跳过），成功 RMAP_OKAY。
 * 注意：非 EBGP peer 直接 NOOP；解析失败(非法 AS/列表名)也 NOOP。 */
/* `set as-path replace AS-PATH` */
static void *route_aspath_replace_compile(const char *arg)
{
	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_aspath_replace_free：释放参数字符串。 */
static void route_aspath_replace_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_set_aspath_replace：见上方规则注释，解析与替换都在此处完成。 */
static enum route_map_cmd_result_t
route_set_aspath_replace(void *rule, const struct prefix *dummy, void *object)
{
	struct aspath *aspath_new;
	const char *replace = rule;
	struct bgp_path_info *path = object;
	as_t replace_asn = 0;
	as_t configured_asn;
	char *buf;
	char src_asn[ASN_STRING_MAX_SIZE];
	char *acl_list_name = NULL;
	uint32_t acl_list_name_len = 0;
	char *buf_acl_name = NULL;
	static const char asp_acl[] = "as-path-access-list";
	struct as_list *aspath_acl = NULL;

	if (path->peer->sort != BGP_PEER_EBGP) {
		zlog_warn(
			"`set as-path replace` is supported only for EBGP peers");
		goto end_ko;
	}

	buf = strchr(replace, ' ');
	if (!buf) {
		configured_asn = path->peer->change_local_as
					 ? path->peer->change_local_as
					 : path->peer->local_as;
	} else if (!strncmp(replace, asp_acl, strlen(asp_acl))) {
		/* its as-path-acl-list command get the access list name */
		while (*buf == ' ')
			buf++;
		buf_acl_name = buf;
		buf = strchr(buf_acl_name, ' ');
		if (buf)
			acl_list_name_len = buf - buf_acl_name;
		else
			acl_list_name_len = strlen(buf_acl_name);

		buf_acl_name[acl_list_name_len] = 0;
		/* get the acl-list */
		aspath_acl = as_list_lookup(buf_acl_name);
		if (!aspath_acl) {
			zlog_warn("`set as-path replace`, invalid as-path-access-list name: %s",
				  buf_acl_name);
			goto end_ko;
		}
		acl_list_name = XSTRDUP(MTYPE_TMP, buf_acl_name);
		buf_acl_name[acl_list_name_len] = ' ';

		if (!buf) {
			configured_asn = path->peer->change_local_as
						 ? path->peer->change_local_as
						 : path->peer->local_as;
		} else {
			while (*buf == ' ')
				buf++;
			/* get the configured asn */
			if (!asn_str2asn(buf, &configured_asn)) {
				zlog_warn(
					"`set as-path replace`, invalid configured AS %s",
					buf);
				goto end_ko;
			}
		}

		replace = buf;

	} else {
		memcpy(src_asn, replace, (size_t)(buf - replace));
		src_asn[(size_t)(buf - replace)] = '\0';
		replace = src_asn;
		buf++;
		if (!asn_str2asn(buf, &configured_asn)) {
			zlog_warn(
				"`set as-path replace`, invalid configured AS %s",
				buf);
			goto end_ko;
		}
	}

	if (replace && !strmatch(replace, "any") &&
	    !asn_str2asn(replace, &replace_asn)) {
		zlog_warn("`set as-path replace`, invalid AS %s", replace);
		goto end_ko;
	}

	if (path->attr->aspath->refcnt)
		aspath_new = aspath_dup(path->attr->aspath);
	else
		aspath_new = path->attr->aspath;

	if (aspath_acl) {
		path->attr->aspath = aspath_replace_regex_asn(aspath_new,
							      aspath_acl,
							      configured_asn);
	} else if (replace && strmatch(replace, "any")) {
		path->attr->aspath =
			aspath_replace_all_asn(aspath_new, configured_asn);
	} else {
		path->attr->aspath = aspath_replace_specific_asn(
			aspath_new, replace_asn, configured_asn);
	}
	aspath_free(aspath_new);


	if (acl_list_name)
		XFREE(MTYPE_TMP, acl_list_name);
	return RMAP_OKAY;

end_ko:
	if (acl_list_name)
		XFREE(MTYPE_TMP, acl_list_name);
	return RMAP_NOOP;

}

/* 【中文补充】route_set_aspath_replace_cmd：规则名 "as-path replace"。 */
static const struct route_map_rule_cmd route_set_aspath_replace_cmd = {
	"as-path replace",
	route_set_aspath_replace,
	route_aspath_replace_compile,
	route_aspath_replace_free,
};

/* 【中文补充：set community 的规则对象】
 * struct rmap_com_set 字段：
 *   com      —— 编译好的 community（新社区集合），"none" 时为 NULL。
 *   additive —— 命令带 "additive" 关键字：新社区**合并**到现有社区上，而非替换。
 *   none     —— 命令为 "none"：清空该路由的全部社区。
 * 三者语义见 route_set_community。"none" 与 "additive" 互斥。 */
/* `set community COMMUNITY' */
struct rmap_com_set {
	struct community *com;
	int additive;
	int none;
};

/* 【函数】route_set_community —— set 侧：设置/合并/清空标准 community。
 * 参数：rule=struct rmap_com_set；object=bgp_path_info。
 * 返回：恒 RMAP_OKAY。
 * 三分支：
 *   none     —— 用 bgp_attr_set_community(attr, NULL) 清空；若旧值未被 intern
 *               (refcnt==0)则释放，避免引用丢失（见下面 HACK 注释）。
 *   additive —— 若已有旧社区，把新社区合并进旧社区再 uniq_sort 去重；否则复制新社区。
 *   默认      —— 直接 community_dup(rcs->com) 覆盖。
 * 注意：本节多次出现“old 未 intern 就手释放”的 HACK，是历史遗留的属性缓存问题。 */
/* For community set mechanism. */
static enum route_map_cmd_result_t
route_set_community(void *rule, const struct prefix *prefix, void *object)
{
	struct rmap_com_set *rcs;
	struct bgp_path_info *path;
	struct attr *attr;
	struct community *new = NULL;
	struct community *old;
	struct community *merge;

	rcs = rule;
	path = object;
	attr = path->attr;
	old = bgp_attr_get_community(attr);

	/* "none" case.  */
	if (rcs->none) {
		bgp_attr_set_community(attr, NULL);
		/* See the longer comment down below. */
		if (old && old->refcnt == 0)
			community_free(&old);
		return RMAP_OKAY;
	}

	/* "additive" case.  */
	if (rcs->additive && old) {
		merge = community_merge(community_dup(old), rcs->com);

		new = community_uniq_sort(merge);
		community_free(&merge);
	} else
		new = community_dup(rcs->com);

	/* HACK: if the old community is not intern'd,
	 * we should free it here, or all reference to it may be
	 * lost.
	 * Really need to cleanup attribute caching sometime.
	 */
	if (old && old->refcnt == 0)
		community_free(&old);

	/* will be interned by caller if required */
	bgp_attr_set_community(attr, new);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_community_compile：解析 "none" / 带 "additive" 的社区串。
 * 技巧：先在串里找 "additive" 子串，把其前一个空格临时切成 '\0' 只解析社区部分，
 * 解析完再把空格还原（因为 community_str2com 不能识别 additive）。 */
/* Compile function for set community. */
static void *route_set_community_compile(const char *arg)
{
	struct rmap_com_set *rcs;
	struct community *com = NULL;
	char *sp;
	int additive = 0;
	int none = 0;

	if (strcmp(arg, "none") == 0)
		none = 1;
	else {
		sp = strstr(arg, "additive");

		if (sp && sp > arg) {
			/* "additive" keyword is included.  */
			additive = 1;
			*(sp - 1) = '\0';
		}

		com = community_str2com(arg);

		if (additive)
			*(sp - 1) = ' ';

		if (!com)
			return NULL;
	}

	rcs = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct rmap_com_set));
	rcs->com = com;
	rcs->additive = additive;
	rcs->none = none;

	return rcs;
}

/* 【中文补充】route_set_community_free：释放社区(none 时 com 为 NULL，跳过)与结构体。 */
/* Free function for set community. */
static void route_set_community_free(void *rule)
{
	struct rmap_com_set *rcs = rule;

	if (rcs->com)
		community_free(&rcs->com);
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rcs);
}

/* 【中文补充】route_set_community_cmd：规则名 "community"。 */
/* Set community rule structure. */
static const struct route_map_rule_cmd route_set_community_cmd = {
	"community",
	route_set_community,
	route_set_community_compile,
	route_set_community_free,
};

/* 【中文补充：set large-community 的规则对象】
 * 与 rmap_com_set 完全对称，字段名 & 语义一致，只是类型换成 lcommunity：
 *   lcom / additive / none。见 route_set_lcommunity。 */
/* `set community COMMUNITY' */
struct rmap_lcom_set {
	struct lcommunity *lcom;
	int additive;
	int none;
};


/* 【函数】route_set_lcommunity —— set 侧：设置/合并/清空 large-community。
 * 与 route_set_community 完全对称（none / additive / 默认三分支），仅类型换成
 * lcommunity。恒返回 RMAP_OKAY。 */
/* For lcommunity set mechanism. */
static enum route_map_cmd_result_t
route_set_lcommunity(void *rule, const struct prefix *prefix, void *object)
{
	struct rmap_lcom_set *rcs;
	struct bgp_path_info *path;
	struct attr *attr;
	struct lcommunity *new = NULL;
	struct lcommunity *old;
	struct lcommunity *merge;

	rcs = rule;
	path = object;
	attr = path->attr;
	old = bgp_attr_get_lcommunity(attr);

	/* "none" case.  */
	if (rcs->none) {
		bgp_attr_set_lcommunity(attr, NULL);

		/* See the longer comment down below. */
		if (old && old->refcnt == 0)
			lcommunity_free(&old);
		return RMAP_OKAY;
	}

	if (rcs->additive && old) {
		merge = lcommunity_merge(lcommunity_dup(old), rcs->lcom);

		new = lcommunity_uniq_sort(merge);
		lcommunity_free(&merge);
	} else
		new = lcommunity_dup(rcs->lcom);

	/* HACK: if the old large-community is not intern'd,
	 * we should free it here, or all reference to it may be
	 * lost.
	 * Really need to cleanup attribute caching sometime.
	 */
	if (old && old->refcnt == 0)
		lcommunity_free(&old);

	/* will be intern()'d or attr_flush()'d by bgp_update_main() */
	bgp_attr_set_lcommunity(attr, new);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_lcommunity_compile：同 route_set_community_compile，
 * 用 lcommunity_str2com 解析。注意 XCALLOC 用的是 sizeof(struct rmap_com_set)
 * （疑点：与字段所用的 rmap_lcom_set 不同名，但两结构体字节布局一致，内存够用，
 *  属可读性隐患，功能上无碍——待确认是否刻意）。 */
/* Compile function for set community. */
static void *route_set_lcommunity_compile(const char *arg)
{
	struct rmap_lcom_set *rcs;
	struct lcommunity *lcom = NULL;
	char *sp;
	int additive = 0;
	int none = 0;

	if (strcmp(arg, "none") == 0)
		none = 1;
	else {
		sp = strstr(arg, "additive");

		if (sp && sp > arg) {
			/* "additive" keyword is included.  */
			additive = 1;
			*(sp - 1) = '\0';
		}

		lcom = lcommunity_str2com(arg);

		if (additive)
			*(sp - 1) = ' ';

		if (!lcom)
			return NULL;
	}

	rcs = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct rmap_com_set));
	rcs->lcom = lcom;
	rcs->additive = additive;
	rcs->none = none;

	return rcs;
}

/* 【中文补充】route_set_lcommunity_free：释放 large-community 与结构体。 */
/* Free function for set lcommunity. */
static void route_set_lcommunity_free(void *rule)
{
	struct rmap_lcom_set *rcs = rule;

	if (rcs->lcom) {
		lcommunity_free(&rcs->lcom);
	}
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rcs);
}

/* 【中文补充】route_set_lcommunity_cmd：规则名 "large-community"。 */
/* Set community rule structure. */
static const struct route_map_rule_cmd route_set_lcommunity_cmd = {
	"large-community",
	route_set_lcommunity,
	route_set_lcommunity_compile,
	route_set_lcommunity_free,
};

/* 【中文补充：set large-comm-list delete 规则】
 * 语法：set large-comm-list NAME delete
 * 语义：从路由的 large-community 里**删除**与指定 large-community-list 匹配的项。
 * 规则对象复用 struct rmap_community（只用 name/name_hash 两个字段）。
 * 逻辑：查 LARGE_COMMUNITY_LIST_MASTER 列表；对 old 做 dup 后用
 *       lcommunity_list_match_delete 过滤出 merge，再 uniq_sort 得 new；
 *       若结果为空则整段清空(bgp_attr_set_lcommunity NULL)并释放 new。
 * 同样带“old 未 intern 则手释放”的 HACK；恒返回 RMAP_OKAY。
 * 注意：命令里的 "delete" 尾部关键字由命令词法吃掉，compile 只取 splits[0] 作列表名。 */
/* `set large-comm-list (<1-99>|<100-500>|WORD) delete' */

/* For large community set mechanism. */
static enum route_map_cmd_result_t
route_set_lcommunity_delete(void *rule, const struct prefix *pfx, void *object)
{
	struct community_list *list;
	struct lcommunity *merge;
	struct lcommunity *new;
	struct lcommunity *old;
	struct bgp_path_info *path;
	struct rmap_community *rcom = rule;

	if (!rcom)
		return RMAP_OKAY;

	path = object;
	list = community_list_lookup(bgp_clist, rcom->name, rcom->name_hash,
				     LARGE_COMMUNITY_LIST_MASTER);
	old = bgp_attr_get_lcommunity(path->attr);

	if (list && old) {
		merge = lcommunity_list_match_delete(lcommunity_dup(old), list);
		new = lcommunity_uniq_sort(merge);
		lcommunity_free(&merge);

		/* HACK: if the old community is not intern'd,
		 * we should free it here, or all reference to it may be
		 * lost.
		 * Really need to cleanup attribute caching sometime.
		 */
		if (old->refcnt == 0)
			lcommunity_free(&old);

		if (new->size == 0) {
			bgp_attr_set_lcommunity(path->attr, NULL);
			lcommunity_free(&new);
		} else {
			bgp_attr_set_lcommunity(path->attr, new);
		}
	}

	return RMAP_OKAY;
}

/* 【中文补充】route_set_lcommunity_delete_compile：把参数按空格切分，取第 0 段作为
 * 列表名，其余丢弃（"delete" 无需保留）；填好 name 与 name_hash。 */
/* Compile function for set lcommunity. */
static void *route_set_lcommunity_delete_compile(const char *arg)
{
	struct rmap_community *rcom;
	char **splits = NULL;
	int num = 0;

	frrstr_split(arg, " ", &splits, &num);

	if (splits == NULL)
		return NULL;

	rcom = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct rmap_community));
	rcom->name = XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, splits[0]);
	rcom->name_hash = bgp_clist_hash_key(rcom->name);

	for (int i = 0; i < num; i++)
		XFREE(MTYPE_TMP, splits[i]);
	XFREE(MTYPE_TMP, splits);

	return rcom;
}

/* 【中文补充】route_set_lcommunity_delete_free：释放 name 与结构体。 */
/* Free function for set lcommunity. */
static void route_set_lcommunity_delete_free(void *rule)
{
	struct rmap_community *rcom = rule;

	XFREE(MTYPE_ROUTE_MAP_COMPILED, rcom->name);
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rcom);
}

/* 【中文补充】route_set_lcommunity_delete_cmd：规则名 "large-comm-list"。 */
/* Set lcommunity rule structure. */
static const struct route_map_rule_cmd route_set_lcommunity_delete_cmd = {
	"large-comm-list",
	route_set_lcommunity_delete,
	route_set_lcommunity_delete_compile,
	route_set_lcommunity_delete_free,
};


/* 【中文补充：set comm-list delete 规则】
 * 语法：set comm-list NAME delete
 * 语义：从路由的标准 community 里删除与指定 community-list 匹配的项。
 * 与 route_set_lcommunity_delete 结构完全对称，换成 community /
 * COMMUNITY_LIST_MASTER / community_list_match_delete。恒返回 RMAP_OKAY。 */
/* `set comm-list (<1-99>|<100-500>|WORD) delete' */

/* For community set mechanism. */
static enum route_map_cmd_result_t
route_set_community_delete(void *rule, const struct prefix *prefix,
			   void *object)
{
	struct community_list *list;
	struct community *merge;
	struct community *new;
	struct community *old;
	struct bgp_path_info *path;
	struct rmap_community *rcom = rule;

	if (!rcom)
		return RMAP_OKAY;

	path = object;
	list = community_list_lookup(bgp_clist, rcom->name, rcom->name_hash,
				     COMMUNITY_LIST_MASTER);
	old = bgp_attr_get_community(path->attr);

	if (list && old) {
		merge = community_list_match_delete(community_dup(old), list);
		new = community_uniq_sort(merge);
		community_free(&merge);

		/* HACK: if the old community is not intern'd,
		 * we should free it here, or all reference to it may be
		 * lost.
		 * Really need to cleanup attribute caching sometime.
		 */
		if (old->refcnt == 0)
			community_free(&old);

		if (new->size == 0) {
			bgp_attr_set_community(path->attr, NULL);
			community_free(&new);
		} else {
			bgp_attr_set_community(path->attr, new);
		}
	}

	return RMAP_OKAY;
}

/* 【中文补充】route_set_community_delete_compile：切分取列表名，逻辑同 lcommunity_delete。 */
/* Compile function for set community. */
static void *route_set_community_delete_compile(const char *arg)
{
	struct rmap_community *rcom;
	char **splits = NULL;
	int num = 0;

	frrstr_split(arg, " ", &splits, &num);

	if (splits == NULL)
		return NULL;

	rcom = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct rmap_community));
	rcom->name = XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, splits[0]);
	rcom->name_hash = bgp_clist_hash_key(rcom->name);

	for (int i = 0; i < num; i++)
		XFREE(MTYPE_TMP, splits[i]);
	XFREE(MTYPE_TMP, splits);

	return rcom;
}

/* 【中文补充】route_set_community_delete_free：释放 name 与结构体。 */
/* Free function for set community. */
static void route_set_community_delete_free(void *rule)
{
	struct rmap_community *rcom = rule;

	XFREE(MTYPE_ROUTE_MAP_COMPILED, rcom->name);
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rcom);
}

/* 【中文补充】route_set_community_delete_cmd：规则名 "comm-list"。 */
/* Set community rule structure. */
static const struct route_map_rule_cmd route_set_community_delete_cmd = {
	"comm-list",
	route_set_community_delete,
	route_set_community_delete_compile,
	route_set_community_delete_free,
};

/* 【中文补充：set extcomm-list delete 规则】
 * 语法：set extcomm-list NAME delete
 * 语义：从路由的扩展 community 里删除与指定 extcommunity-list 匹配的项。
 * 结构同前面两个 delete 规则，换成 ecommunity / EXTCOMMUNITY_LIST_MASTER。 */
/* `set extcomm-list (<1-99>|<100-500>|WORD) delete' */
static enum route_map_cmd_result_t
route_set_ecommunity_delete(void *rule, const struct prefix *prefix,
			   void *object)
{
	struct community_list *list;
	struct ecommunity *merge;
	struct ecommunity *new;
	struct ecommunity *old;
	struct bgp_path_info *path;
	struct rmap_community *rcom = rule;

	if (!rcom)
		return RMAP_OKAY;

	path = object;
	list = community_list_lookup(bgp_clist, rcom->name, rcom->name_hash,
				     EXTCOMMUNITY_LIST_MASTER);
	old = bgp_attr_get_ecommunity(path->attr);
	if (list && old) {
		merge = ecommunity_list_match_delete(ecommunity_dup(old), list);
		new = ecommunity_uniq_sort(merge);
		ecommunity_free(&merge);

		/* HACK: if the old community is not intern'd,
		 * we should free it here, or all reference to it may be
		 * lost.
		 * Really need to cleanup attribute caching sometime.
		 */
		if (old->refcnt == 0)
			ecommunity_free(&old);

		if (new->size == 0) {
			bgp_attr_set_ecommunity(path->attr, NULL);
			ecommunity_free(&new);
		} else {
			bgp_attr_set_ecommunity(path->attr, new);
		}
	}

	return RMAP_OKAY;
}

/* 【中文补充】route_set_ecommunity_delete_compile：切分取列表名(与前面 delete 同)。 */
static void *route_set_ecommunity_delete_compile(const char *arg)
{
	struct rmap_community *rcom;
	char **splits;
	int num;

	frrstr_split(arg, " ", &splits, &num);

	rcom = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct rmap_community));
	rcom->name = XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, splits[0]);
	rcom->name_hash = bgp_clist_hash_key(rcom->name);

	for (int i = 0; i < num; i++)
		XFREE(MTYPE_TMP, splits[i]);
	XFREE(MTYPE_TMP, splits);

	return rcom;
}

/* 【中文补充】route_set_ecommunity_delete_free：释放 name 与结构体。 */
static void route_set_ecommunity_delete_free(void *rule)
{
	struct rmap_community *rcom = rule;

	XFREE(MTYPE_ROUTE_MAP_COMPILED, rcom->name);
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rcom);
}

/* 【中文补充】route_set_ecommunity_delete_cmd：规则名 "extended-comm-list"。 */
static const struct route_map_rule_cmd route_set_ecommunity_delete_cmd = {
	"extended-comm-list",
	route_set_ecommunity_delete,
	route_set_ecommunity_delete_compile,
	route_set_ecommunity_delete_free,
};

/* 【中文补充：set extcommunity rt / soo 的规则对象】
 * struct rmap_ecom_set 字段：
 *   ecom —— 编译好的电子社区(ecommunity)。
 *   none —— 命令为 "none"：清空扩展社区。
 * 说明：本规则被 set extcommunity rt 与 set extcommunity soo（RT/SOO）共用，
 *       恒为“增量(additive)”语义（见函数内注释）。 */
/* `set extcommunity rt COMMUNITY' */

struct rmap_ecom_set {
	struct ecommunity *ecom;
	bool none;
};

/* 【函数】route_set_ecommunity —— set 侧：设置(合并)扩展 community，被 rt/soo 共用。
 * 返回：恒 RMAP_OKAY。
 * 逻辑：none 直接清空；否则**始终按 additive 处理**（注释明确假设扩展社区为增量语义）：
 *       已有旧值则 ecommunity_merge 合并，无旧值则直接 dup。
 * 注意：old_ecom->refcnt 判断——refcnt=1 表示被别处(bgp_update_receive)持有，
 *       =0 表示是上一条 route-map 语句设的、需在此释放，避免泄漏。 */
/* For community set mechanism.  Used by _rt and _soo. */
static enum route_map_cmd_result_t
route_set_ecommunity(void *rule, const struct prefix *prefix, void *object)
{
	struct rmap_ecom_set *rcs;
	struct ecommunity *new_ecom;
	struct ecommunity *old_ecom;
	struct bgp_path_info *path;
	struct attr *attr;

	rcs = rule;
	path = object;
	attr = path->attr;

	if (rcs->none) {
		bgp_attr_set_ecommunity(attr, NULL);
		return RMAP_OKAY;
	}

	if (!rcs->ecom)
		return RMAP_OKAY;

	/* We assume additive for Extended Community. */
	old_ecom = bgp_attr_get_ecommunity(path->attr);

	if (old_ecom) {
		new_ecom =
			ecommunity_merge(ecommunity_dup(old_ecom), rcs->ecom);

		/* old_ecom->refcnt = 1 => owned elsewhere, e.g.
		 * bgp_update_receive()
		 *         ->refcnt = 0 => set by a previous route-map
		 * statement */
		if (!old_ecom->refcnt)
			ecommunity_free(&old_ecom);
	} else
		new_ecom = ecommunity_dup(rcs->ecom);

	/* will be intern()'d or attr_flush()'d by bgp_update_main() */
	bgp_attr_set_ecommunity(path->attr, new_ecom);

	return RMAP_OKAY;
}

/* 【中文补充：set extcommunity none 规则】参数为 "none" 则置 none 标志（清空扩展社区）；
 * 该 compile 也服务 "extcommunity" 这个通用规则名。 */
/* 【中文补充】route_set_ecommunity_none_compile：解析 "none" 关键字。 */
static void *route_set_ecommunity_none_compile(const char *arg)
{
	struct rmap_ecom_set *rcs;
	bool none = false;

	if (strncmp(arg, "none", 4) == 0)
		none = true;

	rcs = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct rmap_ecom_set));
	rcs->ecom = NULL;
	rcs->none = none;

	return rcs;
}

/* 【中文补充】route_set_ecommunity_rt_compile：按 route-target 类型解析电子社区，
 * ecommunity_intern 复用后存入。解析失败即 NULL。 */
static void *route_set_ecommunity_rt_compile(const char *arg)
{
	struct rmap_ecom_set *rcs;
	struct ecommunity *ecom;

	ecom = ecommunity_str2com(arg, ECOMMUNITY_ROUTE_TARGET, 0);
	if (!ecom)
		return NULL;

	rcs = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct rmap_ecom_set));
	rcs->ecom = ecommunity_intern(ecom);
	rcs->none = false;

	return rcs;
}

/* 【中文补充】route_set_ecommunity_free：unintern（而非直接释放）电子社区，再释放结构体。
 * 被 rt / soo / nt / color 等规则共用。 */
/* Free function for set community.  Used by _rt and _soo */
static void route_set_ecommunity_free(void *rule)
{
	struct rmap_ecom_set *rcs = rule;

	if (rcs->ecom)
		ecommunity_unintern(&rcs->ecom);

	XFREE(MTYPE_ROUTE_MAP_COMPILED, rcs);
}

/* 【中文补充】route_set_ecommunity_none_cmd：规则名 "extcommunity"（none 变体）。 */
static const struct route_map_rule_cmd route_set_ecommunity_none_cmd = {
	"extcommunity",
	route_set_ecommunity,
	route_set_ecommunity_none_compile,
	route_set_ecommunity_free,
};

/* 【中文补充】route_set_ecommunity_rt_cmd：规则名 "extcommunity rt"。 */
/* Set community rule structure. */
static const struct route_map_rule_cmd route_set_ecommunity_rt_cmd = {
	"extcommunity rt",
	route_set_ecommunity,
	route_set_ecommunity_rt_compile,
	route_set_ecommunity_free,
};

/* 【中文补充：set extcommunity soo 规则】*/
/* `set extcommunity soo COMMUNITY' */

/* 【中文补充】route_set_ecommunity_soo_compile：按 Site-of-Origin 类型解析电子社区。 */
/* Compile function for set community. */
static void *route_set_ecommunity_soo_compile(const char *arg)
{
	struct rmap_ecom_set *rcs;
	struct ecommunity *ecom;

	ecom = ecommunity_str2com(arg, ECOMMUNITY_SITE_ORIGIN, 0);
	if (!ecom)
		return NULL;

	rcs = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct rmap_ecom_set));
	rcs->ecom = ecommunity_intern(ecom);
	rcs->none = false;

	return rcs;
}

/* 【中文补充】route_set_ecommunity_soo_cmd：规则名 "extcommunity soo"。 */
/* Set community rule structure. */
static const struct route_map_rule_cmd route_set_ecommunity_soo_cmd = {
	"extcommunity soo",
	route_set_ecommunity,
	route_set_ecommunity_soo_compile,
	route_set_ecommunity_free,
};

/* 【中文补充】route_set_ecommunity_nt_compile：按 Node-Target 类型解析电子社区。 */
static void *route_set_ecommunity_nt_compile(const char *arg)
{
	struct rmap_ecom_set *rcs;
	struct ecommunity *ecom;

	ecom = ecommunity_str2com(arg, ECOMMUNITY_NODE_TARGET, 0);
	if (!ecom)
		return NULL;

	rcs = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct rmap_ecom_set));
	rcs->ecom = ecommunity_intern(ecom);
	rcs->none = false;

	return rcs;
}

/* 【中文补充】route_set_ecommunity_nt_cmd：规则名 "extcommunity nt"。 */
static const struct route_map_rule_cmd route_set_ecommunity_nt_cmd = {
	"extcommunity nt",
	route_set_ecommunity,
	route_set_ecommunity_nt_compile,
	route_set_ecommunity_free,
};

/* 【中文补充：set extcommunity bandwidth 的规则对象】
 * struct rmap_ecomm_lb_set 字段：
 *   lb_type   —— 带宽来源，取下面三个宏之一：
 *                VALUE(1)     给定固定带宽值 bw；
 *                CUMUL(2)     取该前缀所有多路径带宽之和(bgp_path_info_mpath_cumbw)；
 *                NUM_MPATH(3) 参考带宽 × 多路径条数。
 *   non_trans —— 命令带 "non-transitive" 后缀：生成非传递的链路带宽社区。
 *   bw        —— VALUE 模式下的带宽(Mbps)。
 * 说明：只作用于 PEER_FLAG_EXTENDED_LINK_BANDWIDTH 的 peer，据此决定编码成 IPv6
 *       扩展社区还是普通扩展社区。 */
/* `set extcommunity bandwidth' */

struct rmap_ecomm_lb_set {
	uint8_t lb_type;
#define RMAP_ECOMM_LB_SET_VALUE 1
#define RMAP_ECOMM_LB_SET_CUMUL 2
#define RMAP_ECOMM_LB_SET_NUM_MPATH 3
	bool non_trans;
	uint64_t bw;
};

/* 【函数】route_set_ecommunity_lb —— set 侧：把链路带宽编码进扩展社区。
 * 返回：RMAP_OKAY（改属性）；peer/bgp 缺失返回 RMAP_ERROR。
 * 流程：按 lb_type 算出 bw_bytes；
 *   - VALUE：bw * 1e6 / 8（Mbps→字节/秒）；
 *   - CUMUL：仅最佳路径(BGP_PATH_SELECTED)处理，取多路径累计带宽；
 *   - NUM_MPATH：仅最佳路径，= lb_ref_bw * 1e6/8 * 多路径条数。
 * 再按 peer 是否启用 EXTENDED_LINK_BANDWIDTH 选择 IPv6 扩展社区或普通扩展社区编码，
 * 合并进现有属性（旧的未 intern 则释放）。最后置 BATTR_RMAP_LINK_BW_SET 标记。
 * 注意：AS 超过 BGP_AS_MAX 时用 BGP_AS_TRANS(23456) 占位。 */
static enum route_map_cmd_result_t
route_set_ecommunity_lb(void *rule, const struct prefix *prefix, void *object)
{
	struct rmap_ecomm_lb_set *rels = rule;
	struct bgp_path_info *path;
	struct peer *peer;
	struct ecommunity ecom_lb = {0};
	uint64_t bw_bytes = 0;
	uint16_t mpath_count = 0;
	struct ecommunity *new_ecom;
	struct ecommunity *old_ecom;
	as_t as;

	path = object;
	peer = path->peer;
	if (!peer || !peer->bgp)
		return RMAP_ERROR;

	/* Build link bandwidth extended community */
	as = (peer->bgp->as > BGP_AS_MAX) ? BGP_AS_TRANS : peer->bgp->as;
	if (rels->lb_type == RMAP_ECOMM_LB_SET_VALUE) {
		bw_bytes = (rels->bw * 1000 * 1000) / 8;
	} else if (rels->lb_type == RMAP_ECOMM_LB_SET_CUMUL) {
		/* process this only for the best path. */
		if (!CHECK_FLAG(path->flags, BGP_PATH_SELECTED))
			return RMAP_OKAY;

		bw_bytes = bgp_path_info_mpath_cumbw(path->net);
		if (!bw_bytes)
			return RMAP_OKAY;

	} else if (rels->lb_type == RMAP_ECOMM_LB_SET_NUM_MPATH) {

		/* process this only for the best path. */
		if (!CHECK_FLAG(path->flags, BGP_PATH_SELECTED))
			return RMAP_OKAY;

		bw_bytes = (peer->bgp->lb_ref_bw * 1000 * 1000) / 8;
		mpath_count = bgp_path_info_mpath_count(path->net);
		bw_bytes *= mpath_count;
	}

	if (CHECK_FLAG(peer->flags, PEER_FLAG_EXTENDED_LINK_BANDWIDTH)) {
		struct ecommunity_val_ipv6 lb_eval;

		encode_lb_extended_extcomm(as, bw_bytes, rels->non_trans,
					   &lb_eval);

		old_ecom = bgp_attr_get_ipv6_ecommunity(path->attr);
		if (old_ecom) {
			new_ecom = ecommunity_dup(old_ecom);
			ecommunity_add_val_ipv6(new_ecom, &lb_eval, true, true);
			if (!old_ecom->refcnt)
				ecommunity_free(&old_ecom);
		} else {
			ecom_lb.size = 1;
			ecom_lb.unit_size = IPV6_ECOMMUNITY_SIZE;
			ecom_lb.val = (uint8_t *)lb_eval.val;
			new_ecom = ecommunity_dup(&ecom_lb);
		}

		bgp_attr_set_ipv6_ecommunity(path->attr, new_ecom);
	} else {
		struct ecommunity_val lb_eval;

		encode_lb_extcomm(as, bw_bytes, rels->non_trans, &lb_eval,
				  CHECK_FLAG(peer->flags,
					     PEER_FLAG_DISABLE_LINK_BW_ENCODING_IEEE));

		old_ecom = bgp_attr_get_ecommunity(path->attr);
		if (old_ecom) {
			new_ecom = ecommunity_dup(old_ecom);
			ecommunity_add_val(new_ecom, &lb_eval, true, true);
			if (!old_ecom->refcnt)
				ecommunity_free(&old_ecom);
		} else {
			ecom_lb.size = 1;
			ecom_lb.unit_size = ECOMMUNITY_SIZE;
			ecom_lb.val = (uint8_t *)lb_eval.val;
			new_ecom = ecommunity_dup(&ecom_lb);
		}

		bgp_attr_set_ecommunity(path->attr, new_ecom);
	}

	/* Mark that route-map has set link bandwidth; used in attribute
	 * setting decisions.
	 */
	SET_FLAG(path->attr->rmap_change_flags, BATTR_RMAP_LINK_BW_SET);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_ecommunity_lb_compile：解析 "N" / "cumulative" /
 * "num-multipaths" [non-transitive]。有空格则把首段当带宽值、置 non_trans=true。
 * 非关键字时按十进制解析带宽，有残留字符即 NULL。 */
static void *route_set_ecommunity_lb_compile(const char *arg)
{
	struct rmap_ecomm_lb_set *rels;
	uint8_t lb_type;
	uint64_t bw = 0;
	char bw_str[40] = {0};
	char *p, *str;
	bool non_trans = false;

	str = (char *)arg;
	p = strchr(arg, ' ');
	if (p) {
		int len;

		len = p - arg;
		memcpy(bw_str, arg, len);
		non_trans = true;
		str = bw_str;
	}

	if (strcmp(str, "cumulative") == 0)
		lb_type = RMAP_ECOMM_LB_SET_CUMUL;
	else if (strcmp(str, "num-multipaths") == 0)
		lb_type = RMAP_ECOMM_LB_SET_NUM_MPATH;
	else {
		char *end = NULL;

		bw = strtoul(str, &end, 10);
		if (*end != '\0')
			return NULL;
		lb_type = RMAP_ECOMM_LB_SET_VALUE;
	}

	rels = XCALLOC(MTYPE_ROUTE_MAP_COMPILED,
		       sizeof(struct rmap_ecomm_lb_set));
	rels->lb_type = lb_type;
	rels->bw = bw;
	rels->non_trans = non_trans;

	return rels;
}

/* 【中文补充】route_set_ecommunity_color —— set extcommunity color 的 match 回调。
 * 直接复用 route_set_ecommunity 的实现，仅把返回值统一成 RMAP_OKAY。
 * 规则对象仍是 rmap_ecom_set（由 color_compile 生成）。 */
static enum route_map_cmd_result_t
route_set_ecommunity_color(void *rule, const struct prefix *prefix,
			   void *object)
{
	route_set_ecommunity(rule, prefix, object);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_ecommunity_color_compile：按 COLOR 类型解析电子社区。 */
static void *route_set_ecommunity_color_compile(const char *arg)
{
	struct rmap_ecom_set *rcs;
	struct ecommunity *ecom;

	ecom = ecommunity_str2com(arg, ECOMMUNITY_COLOR, 0);
	if (!ecom)
		return NULL;

	rcs = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct rmap_ecom_set));
	rcs->ecom = ecommunity_intern(ecom);
	rcs->none = false;

	return rcs;
}

/* 【中文补充】route_set_ecommunity_color_cmd：规则名 "extcommunity color"。 */
static const struct route_map_rule_cmd route_set_ecommunity_color_cmd = {
	"extcommunity color",
	route_set_ecommunity_color,
	route_set_ecommunity_color_compile,
	route_set_ecommunity_free,
};


/* 【中文补充】route_set_ecommunity_lb_free：释放 rmap_ecomm_lb_set。 */
static void route_set_ecommunity_lb_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_set_ecommunity_lb_cmd：规则名 "extcommunity bandwidth"，非 static。 */
/* Set community rule structure. */
struct route_map_rule_cmd route_set_ecommunity_lb_cmd = {
	"extcommunity bandwidth",
	route_set_ecommunity_lb,
	route_set_ecommunity_lb_compile,
	route_set_ecommunity_lb_free,
};

/* 【中文补充：set origin 规则】
 * 语法：set origin (igp|egp|incomplete)
 * 语义：设置 ORIGIN 属性；规则对象是 1 字节 origin（BGP_ORIGIN_IGP/EGP/INCOMPLETE）。 */

/* `set origin ORIGIN' */

/* For origin set. */
static enum route_map_cmd_result_t
route_set_origin(void *rule, const struct prefix *prefix, void *object)
{
	uint8_t *origin;
	struct bgp_path_info *path;

	origin = rule;
	path = object;

	path->attr->origin = *origin;

	return RMAP_OKAY;
}

/* 【中文补充】route_set_origin_compile：把 igp/egp/incomplete 映射为 BGP_ORIGIN_* 常量。 */
/* Compile function for origin set. */
static void *route_set_origin_compile(const char *arg)
{
	uint8_t *origin;

	origin = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(uint8_t));

	if (strcmp(arg, "igp") == 0)
		*origin = BGP_ORIGIN_IGP;
	else if (strcmp(arg, "egp") == 0)
		*origin = BGP_ORIGIN_EGP;
	else
		*origin = BGP_ORIGIN_INCOMPLETE;

	return origin;
}

/* 【中文补充】route_set_origin_free：释放 1 字节 origin。 */
/* Compile function for origin set. */
static void route_set_origin_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_set_origin_cmd：规则名 "origin"。 */
/* Set origin rule structure. */
static const struct route_map_rule_cmd route_set_origin_cmd = {
	"origin",
	route_set_origin,
	route_set_origin_compile,
	route_set_origin_free,
};

/* 【中文补充：set atomic-aggregate 规则】
 * 语法：set atomic-aggregate
 * 语义：置 ATOMIC_AGGREGATE 属性位。**无参数**：compile 只返回一个非 NULL 常量
 *       (void*)1 占位，free 直接 return 什么都不做。 */

/* `set atomic-aggregate' */

/* For atomic aggregate set. */
static enum route_map_cmd_result_t
route_set_atomic_aggregate(void *rule, const struct prefix *pfx, void *object)
{
	struct bgp_path_info *path;

	path = object;
	bgp_attr_set(path->attr, BGP_ATTR_ATOMIC_AGGREGATE);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_atomic_aggregate_compile：无参数，返回占位指针 (void*)1。 */
/* Compile function for atomic aggregate. */
static void *route_set_atomic_aggregate_compile(const char *arg)
{
	return (void *)1;
}

/* 【中文补充】route_set_atomic_aggregate_free：无资源需释放（占位指针不是堆内存）。 */
/* Compile function for atomic aggregate. */
static void route_set_atomic_aggregate_free(void *rule)
{
	return;
}

/* 【中文补充】route_set_atomic_aggregate_cmd：规则名 "atomic-aggregate"。 */
/* Set atomic aggregate rule structure. */
static const struct route_map_rule_cmd route_set_atomic_aggregate_cmd = {
	"atomic-aggregate",
	route_set_atomic_aggregate,
	route_set_atomic_aggregate_compile,
	route_set_atomic_aggregate_free,
};

/* 【中文补充：set aigp-metric 规则】
 * 语法：set aigp-metric (AIGP-METRIC|igp-metric)
 * 语义：设置 AIGP 度量。参数为关键字 "igp-metric" 时取动态来源——有 nexthop 就用
 *       path->nexthop->metric，否则用 MED(attr->med)；否则按数字解析。
 * 最后 bgp_attr_set_aigp_metric 写回。规则对象是参数字符串。 */
/* AIGP TLV Metric */
static enum route_map_cmd_result_t
route_set_aigp_metric(void *rule, const struct prefix *pfx, void *object)
{
	const char *aigp_metric = rule;
	struct bgp_path_info *path = object;
	uint32_t aigp;

	/* Note: the metric is stored as MED for a locally redistributed. */
	if (strmatch(aigp_metric, "igp-metric"))
		aigp = path->nexthop ? path->nexthop->metric : path->attr->med;
	else
		aigp = atoi(aigp_metric);

	bgp_attr_set_aigp_metric(path->attr, aigp);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_aigp_metric_compile：存参数字符串。 */
static void *route_set_aigp_metric_compile(const char *arg)
{
	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_set_aigp_metric_free：释放字符串。 */
static void route_set_aigp_metric_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_set_aigp_metric_cmd：规则名 "aigp-metric"。 */
static const struct route_map_rule_cmd route_set_aigp_metric_cmd = {
	"aigp-metric",
	route_set_aigp_metric,
	route_set_aigp_metric_compile,
	route_set_aigp_metric_free,
};

/* 【中文补充：set aggregator as 规则】
 * 语法：set aggregator as AS A.B.C.D
 * 语义：设置 AGGREGATOR 属性（as + 地址）。规则对象 struct aggregator{as,address}。
 * 注意：compile 用 "%s %s" 一次性取两个 token 再分别解析，任一非法即 NULL。 */
/* `set aggregator as AS A.B.C.D' */
struct aggregator {
	as_t as;
	struct in_addr address;
};

/* 【中文补充】route_set_aggregator_as：写 aggregator_as / aggregator_addr 并置属性位。 */
static enum route_map_cmd_result_t
route_set_aggregator_as(void *rule, const struct prefix *prefix, void *object)
{
	struct bgp_path_info *path;
	struct aggregator *aggregator;

	path = object;
	aggregator = rule;

	path->attr->aggregator_as = aggregator->as;
	path->attr->aggregator_addr = aggregator->address;
	bgp_attr_set(path->attr, BGP_ATTR_AGGREGATOR);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_aggregator_as_compile：解析 "AS 地址" 两个字段。 */
static void *route_set_aggregator_as_compile(const char *arg)
{
	struct aggregator *aggregator;
	char as[10];
	char address[20];
	int ret;

	aggregator =
		XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct aggregator));
	if (sscanf(arg, "%s %s", as, address) != 2) {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, aggregator);
		return NULL;
	}

	aggregator->as = strtoul(as, NULL, 10);
	ret = inet_aton(address, &aggregator->address);
	if (ret == 0) {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, aggregator);
		return NULL;
	}
	return aggregator;
}

/* 【中文补充】route_set_aggregator_as_free：释放 struct aggregator。 */
static void route_set_aggregator_as_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_set_aggregator_as_cmd：规则名 "aggregator as"。 */
static const struct route_map_rule_cmd route_set_aggregator_as_cmd = {
	"aggregator as",
	route_set_aggregator_as,
	route_set_aggregator_as_compile,
	route_set_aggregator_as_free,
};

/* 【中文补充：set tag 规则】语法：set tag TAG；设置 attr->tag。
 * compile/free 复用 lib 的 route_map_rule_tag_*。 */
/* Set tag to object. object must be pointer to struct bgp_path_info */
static enum route_map_cmd_result_t
route_set_tag(void *rule, const struct prefix *prefix, void *object)
{
	route_tag_t *tag;
	struct bgp_path_info *path;

	tag = rule;
	path = object;

	/* Set tag value */
	path->attr->tag = *tag;

	return RMAP_OKAY;
}

/* 【中文补充】route_set_tag_cmd：规则名 "tag"。 */
/* Route map commands for tag set. */
static const struct route_map_rule_cmd route_set_tag_cmd = {
	"tag",
	route_set_tag,
	route_map_rule_tag_compile,
	route_map_rule_tag_free,
};

/* 【中文补充：set label-index 规则】语法：set label-index N；
 * 设置 prefix-SID 的 label-index。注意只有非 0 才写入并置 BGP_ATTR_PREFIX_SID 位。 */
/* Set label-index to object. object must be pointer to struct bgp_path_info */
static enum route_map_cmd_result_t
route_set_label_index(void *rule, const struct prefix *prefix, void *object)
{
	struct rmap_value *rv;
	struct bgp_path_info *path;
	uint32_t label_index;

	/* Fetch routemap's rule information. */
	rv = rule;
	path = object;

	/* Set label-index value. */
	label_index = rv->value;
	if (label_index) {
		path->attr->label_index = label_index;
		bgp_attr_set(path->attr, BGP_ATTR_PREFIX_SID);
	}

	return RMAP_OKAY;
}

/* 【中文补充】route_set_label_index_cmd：规则名 "label-index"，compile/free 复用通用取值。 */
/* Route map commands for label-index set. */
static const struct route_map_rule_cmd route_set_label_index_cmd = {
	"label-index",
	route_set_label_index,
	route_value_compile,
	route_value_free,
};

/* 【中文补充：match ipv6 address 规则】
 * 语法：match ipv6 address ACCESS-LIST-NAME
 * 语义：用 IPv6 ACL 匹配路由前缀（AF_INET6）。与 match ip address 对称（v4 版）。 */

/* `match ipv6 address IP_ACCESS_LIST' */

static enum route_map_cmd_result_t
route_match_ipv6_address(void *rule, const struct prefix *prefix, void *object)
{
	struct access_list *alist;

	if (prefix->family == AF_INET6) {
		alist = access_list_lookup(AFI_IP6, (char *)rule);
		if (alist == NULL) {
			if (unlikely(CHECK_FLAG(rmap_debug,
						DEBUG_ROUTEMAP_DETAIL)))
				zlog_debug(
					"%s: Access-List Specified: %s does not exist defaulting to NO_MATCH",
					__func__, (char *)rule);

			return RMAP_NOMATCH;
		}

		return (access_list_apply(alist, prefix) == FILTER_DENY
				? RMAP_NOMATCH
				: RMAP_MATCH);
	}
	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_ipv6_address_compile：存 ACL 名，不存在仅告警(可后建)。 */
static void *route_match_ipv6_address_compile(const char *arg)
{
	struct access_list *alist;

	alist = access_list_lookup(AFI_IP6, arg);
	if (!alist)
		zlog_warn(
			"Access List specified %s does not exist yet, default will be NO_MATCH until it is created",
			arg);

	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_match_ipv6_address_free：释放 ACL 名。 */
static void route_match_ipv6_address_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_ipv6_address_cmd：规则名 "ipv6 address"。 */
/* Route map commands for ip address matching. */
static const struct route_map_rule_cmd route_match_ipv6_address_cmd = {
	"ipv6 address",
	route_match_ipv6_address,
	route_match_ipv6_address_compile,
	route_match_ipv6_address_free
};

/* 【中文补充：match ipv6 next-hop 规则（按 ACL）】
 * 语法：match ipv6 next-hop ACCESS-LIST-NAME
 * 语义：用 IPv6 ACL 匹配下一跳（先 mp_nexthop_global，未中且带 link-local 再判
 *       mp_nexthop_local）。与 v4 的 ip next-hop(ACL) 结构对称。 */
/* `match ipv6 next-hop ACCESSLIST6_NAME' */
static enum route_map_cmd_result_t
route_match_ipv6_next_hop(void *rule, const struct prefix *prefix, void *object)
{
	struct bgp_path_info *path;
	struct access_list *alist;
	struct prefix_ipv6 p;

	if (prefix->family == AF_INET6) {
		path = object;
		p.family = AF_INET6;
		p.prefix = path->attr->mp_nexthop_global;
		p.prefixlen = IPV6_MAX_BITLEN;

		alist = access_list_lookup(AFI_IP6, (char *)rule);
		if (!alist) {
			if (unlikely(CHECK_FLAG(rmap_debug,
						DEBUG_ROUTEMAP_DETAIL)))
				zlog_debug(
					"%s: Access-List Specified: %s does not exist defaulting to NO_MATCH",
					__func__, (char *)rule);

			return RMAP_NOMATCH;
		}

		if (access_list_apply(alist, &p) == FILTER_PERMIT)
			return RMAP_MATCH;

		if (path->attr->mp_nexthop_len
		    == BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL) {
			p.prefix = path->attr->mp_nexthop_local;
			if (access_list_apply(alist, &p) == FILTER_PERMIT)
				return RMAP_MATCH;
		}
	}

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_ipv6_next_hop_compile：存 ACL 名。 */
static void *route_match_ipv6_next_hop_compile(const char *arg)
{
	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_match_ipv6_next_hop_free：释放 ACL 名。 */
static void route_match_ipv6_next_hop_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_ipv6_next_hop_cmd：规则名 "ipv6 next-hop"。 */
static const struct route_map_rule_cmd route_match_ipv6_next_hop_cmd = {
	"ipv6 next-hop",
	route_match_ipv6_next_hop,
	route_match_ipv6_next_hop_compile,
	route_match_ipv6_next_hop_free
};

/* 【中文补充：match ipv6 next-hop address 规则（按具体地址）】
 * 语法：match ipv6 next-hop address X:X::X:X
 * 语义：直接比较下一跳 IPv6 地址是否等于给定地址（global 或 link-local 任一同即命中）。
 * 规则对象是 struct in6_addr。与上面“按 ACL”的 ipv6 next-hop 是两种不同规则名。 */

/* `match ipv6 next-hop IP_ADDRESS' */

static enum route_map_cmd_result_t
route_match_ipv6_next_hop_address(void *rule, const struct prefix *prefix,
				  void *object)
{
	struct in6_addr *addr = rule;
	struct bgp_path_info *path;

	path = object;

	if (IPV6_ADDR_SAME(&path->attr->mp_nexthop_global, addr))
		return RMAP_MATCH;

	if (path->attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL
	    && IPV6_ADDR_SAME(&path->attr->mp_nexthop_local, rule))
		return RMAP_MATCH;

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_ipv6_next_hop_address_compile：inet_pton 解析 IPv6 地址。 */
static void *route_match_ipv6_next_hop_address_compile(const char *arg)
{
	struct in6_addr *address;
	int ret;

	address = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct in6_addr));

	ret = inet_pton(AF_INET6, arg, address);
	if (!ret) {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, address);
		return NULL;
	}

	return address;
}

/* 【中文补充】route_match_ipv6_next_hop_address_free：释放地址。 */
static void route_match_ipv6_next_hop_address_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_ipv6_next_hop_address_cmd：规则名 "ipv6 next-hop address"。 */
static const struct route_map_rule_cmd route_match_ipv6_next_hop_address_cmd = {
	"ipv6 next-hop address",
	route_match_ipv6_next_hop_address,
	route_match_ipv6_next_hop_address_compile,
	route_match_ipv6_next_hop_address_free
};

/* 【中文补充：match ip next-hop address 规则（v4，按具体地址）】
 * 语法：match ip next-hop address A.B.C.D
 * 语义：比较 IPv4 下一跳——既看 attr->nexthop，也在 MP 场景下看
 *       mp_nexthop_global_in(长度为 IPV4 时)。规则对象 struct in_addr。 */

/* `match ip next-hop address IP_ADDRESS' */

static enum route_map_cmd_result_t
route_match_ipv4_next_hop(void *rule, const struct prefix *prefix, void *object)
{
	struct in_addr *addr = rule;
	struct bgp_path_info *path;

	path = object;

	if (path->attr->nexthop.s_addr == addr->s_addr
	    || (path->attr->mp_nexthop_len == BGP_ATTR_NHLEN_IPV4
		&& IPV4_ADDR_SAME(&path->attr->mp_nexthop_global_in, addr)))
		return RMAP_MATCH;

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_ipv4_next_hop_compile：inet_pton 解析 IPv4 地址。 */
static void *route_match_ipv4_next_hop_compile(const char *arg)
{
	struct in_addr *address;
	int ret;

	address = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct in_addr));

	ret = inet_pton(AF_INET, arg, address);
	if (!ret) {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, address);
		return NULL;
	}

	return address;
}

/* 【中文补充】route_match_ipv4_next_hop_free：释放地址。 */
static void route_match_ipv4_next_hop_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_ipv4_next_hop_cmd：规则名 "ip next-hop address"。 */
static const struct route_map_rule_cmd route_match_ipv4_next_hop_cmd = {
	"ip next-hop address",
	route_match_ipv4_next_hop,
	route_match_ipv4_next_hop_compile,
	route_match_ipv4_next_hop_free
};

/* 【中文补充：match ipv6 address prefix-list 规则】IPv6 前缀列表匹配前缀本身，
 * 薄包装转调 route_match_address_prefix_list(AFI_IP6,...)。 */
/* `match ipv6 address prefix-list PREFIX_LIST' */

static enum route_map_cmd_result_t
route_match_ipv6_address_prefix_list(void *rule, const struct prefix *prefix,
				     void *object)
{
	return route_match_address_prefix_list(rule, AFI_IP6, prefix, object);
}

/* 【中文补充】route_match_ipv6_address_prefix_list_compile：存前缀列表名。 */
static void *route_match_ipv6_address_prefix_list_compile(const char *arg)
{
	return XSTRDUP(MTYPE_ROUTE_MAP_COMPILED, arg);
}

/* 【中文补充】route_match_ipv6_address_prefix_list_free：释放前缀列表名。 */
static void route_match_ipv6_address_prefix_list_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_ipv6_address_prefix_list_cmd：规则名 "ipv6 address prefix-list"。 */
static const struct route_map_rule_cmd
		route_match_ipv6_address_prefix_list_cmd = {
	"ipv6 address prefix-list",
	route_match_ipv6_address_prefix_list,
	route_match_ipv6_address_prefix_list_compile,
	route_match_ipv6_address_prefix_list_free
};

/* 【中文补充：match ipv6 next-hop type 规则】
 * 语法：match ipv6 next-hop type blackhole
 * 语义：v6 版黑洞匹配——下一跳为全零(::0)且无下一跳接口索引(nh_ifindex)即命中。
 * 注意：规则对象固定解析成 "::0"（compile 忽略 arg 内容，硬编码全零地址）。 */

/* `match ipv6 next-hop type <TYPE>' */

static enum route_map_cmd_result_t
route_match_ipv6_next_hop_type(void *rule, const struct prefix *prefix,
			       void *object)
{
	struct bgp_path_info *path;
	struct in6_addr *addr = rule;

	if (prefix->family == AF_INET6) {
		path = (struct bgp_path_info *)object;
		if (!path)
			return RMAP_NOMATCH;

		if (IPV6_ADDR_SAME(&path->attr->mp_nexthop_global, addr)
		    && !path->attr->nh_ifindex)
			return RMAP_MATCH;
	}

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_ipv6_next_hop_type_compile：把规则值固定设为 ::0。 */
static void *route_match_ipv6_next_hop_type_compile(const char *arg)
{
	struct in6_addr *address;
	int ret;

	address = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct in6_addr));

	ret = inet_pton(AF_INET6, "::0", address);
	if (!ret) {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, address);
		return NULL;
	}

	return address;
}

/* 【中文补充】route_match_ipv6_next_hop_type_free：释放地址。 */
static void route_match_ipv6_next_hop_type_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_match_ipv6_next_hop_type_cmd：规则名 "ipv6 next-hop type"。 */
static const struct route_map_rule_cmd
		route_match_ipv6_next_hop_type_cmd = {
	"ipv6 next-hop type",
	route_match_ipv6_next_hop_type,
	route_match_ipv6_next_hop_type_compile,
	route_match_ipv6_next_hop_type_free
};

/* 【中文补充：set ipv6 next-hop global 规则】
 * 语法：set ipv6 next-hop global X:X::X:X
 * 语义：设置 IPv6 全局下一跳。写 mp_nexthop_global；若原长度为 0 则补成
 *       BGP_ATTR_NHLEN_IPV6_GLOBAL；置 BATTR_RMAP_IPV6_GLOBAL_NHOP_CHANGED。 */

/* `set ipv6 nexthop global IP_ADDRESS' */

/* Set nexthop to object.  object must be pointer to struct attr. */
static enum route_map_cmd_result_t
route_set_ipv6_nexthop_global(void *rule, const struct prefix *p, void *object)
{
	struct in6_addr *address;
	struct bgp_path_info *path;

	/* Fetch routemap's rule information. */
	address = rule;
	path = object;

	/* Set next hop value. */
	path->attr->mp_nexthop_global = *address;

	/* Set nexthop length. */
	if (path->attr->mp_nexthop_len == 0)
		path->attr->mp_nexthop_len = BGP_ATTR_NHLEN_IPV6_GLOBAL;

	SET_FLAG(path->attr->rmap_change_flags,
		 BATTR_RMAP_IPV6_GLOBAL_NHOP_CHANGED);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_ipv6_nexthop_global_compile：inet_pton 解析 IPv6 地址。 */
/* Route map `ip next-hop' compile function.  Given string is converted
   to struct in_addr structure. */
static void *route_set_ipv6_nexthop_global_compile(const char *arg)
{
	int ret;
	struct in6_addr *address;

	address = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct in6_addr));

	ret = inet_pton(AF_INET6, arg, address);

	if (ret == 0) {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, address);
		return NULL;
	}

	return address;
}

/* 【中文补充】route_set_ipv6_nexthop_global_free：释放地址。 */
/* Free route map's compiled `ip next-hop' value. */
static void route_set_ipv6_nexthop_global_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_set_ipv6_nexthop_global_cmd：规则名 "ipv6 next-hop global"。 */
/* Route map commands for ip nexthop set. */
static const struct route_map_rule_cmd
		route_set_ipv6_nexthop_global_cmd = {
	"ipv6 next-hop global",
	route_set_ipv6_nexthop_global,
	route_set_ipv6_nexthop_global_compile,
	route_set_ipv6_nexthop_global_free
};

/* 【中文补充：set ipv6 next-hop prefer-global 规则】
 * 语义：控制多路径下一跳中“优先使用全局地址”。**无参数**：
 *   入方向(PEER_RMAP_TYPE_IN) 时置 BGP_ATTR_NH_MP_PREFER_GLOBAL；
 *   否则取消该位。两种情况都置 BATTR_RMAP_IPV6_PREFER_GLOBAL_CHANGED。 */
/* Set next-hop preference value. */
static enum route_map_cmd_result_t
route_set_ipv6_nexthop_prefer_global(void *rule, const struct prefix *prefix,
				     void *object)
{
	struct bgp_path_info *path;
	struct peer *peer;

	/* Fetch routemap's rule information. */
	path = object;
	peer = path->peer;

	if (CHECK_FLAG(peer->rmap_type, PEER_RMAP_TYPE_IN)) {
		/* Set next hop preference to global */
		SET_FLAG(path->attr->nh_flags, BGP_ATTR_NH_MP_PREFER_GLOBAL);
		SET_FLAG(path->attr->rmap_change_flags,
			 BATTR_RMAP_IPV6_PREFER_GLOBAL_CHANGED);
	} else {
		UNSET_FLAG(path->attr->nh_flags, BGP_ATTR_NH_MP_PREFER_GLOBAL);
		SET_FLAG(path->attr->rmap_change_flags,
			 BATTR_RMAP_IPV6_PREFER_GLOBAL_CHANGED);
	}

	return RMAP_OKAY;
}

/* 【中文补充】route_set_ipv6_nexthop_prefer_global_compile：无参数命令，
 * 仅分配一个占位 int(值为 1)；规则值本身不参与运算。 */
static void *route_set_ipv6_nexthop_prefer_global_compile(const char *arg)
{
	int *rins = NULL;

	rins = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(int));
	*rins = 1;

	return rins;
}

/* 【中文补充】route_set_ipv6_nexthop_prefer_global_free：释放占位 int。 */
/* Free route map's compiled `ip next-hop' value. */
static void route_set_ipv6_nexthop_prefer_global_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_set_ipv6_nexthop_prefer_global_cmd：规则名 "ipv6 next-hop prefer-global"。 */
/* Route map commands for ip nexthop set preferred. */
static const struct route_map_rule_cmd
		route_set_ipv6_nexthop_prefer_global_cmd = {
	"ipv6 next-hop prefer-global",
	route_set_ipv6_nexthop_prefer_global,
	route_set_ipv6_nexthop_prefer_global_compile,
	route_set_ipv6_nexthop_prefer_global_free
};

/* 【中文补充：set ipv6 next-hop local 规则】
 * 语法：set ipv6 next-hop local X:X::X:X
 * 语义：设置 IPv6 本地链路(link-local)下一跳 mp_nexthop_local。
 *   长度调整分两种情况：VPN/ENCAP/EVPN 表用 BGP_ATTR_NHLEN_VPNV6_GLOBAL_AND_LL，
 *   普通表用 BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL（均含全局 + 本地两段）。
 *   若路径无 net/bgp 表则直接 RMAP_OKAY 不做修改。
 *   结束置 BATTR_RMAP_IPV6_LL_NHOP_CHANGED。 */

/* 【中文补充】route_set_ipv6_nexthop_local：set ipv6 next-hop local 的执行体。
 * 作用：把规则里编译好的 IPv6 链路本地地址写入 path->attr->mp_nexthop_local。
 * 参数：rule 为编译后的 struct in6_addr 指针；object 为 struct bgp_path_info。
 * 副作用：按表的 safi 选择下一跳长度(VPN / ENCAP / EVPN 用 VPNV6_GLOBAL_AND_LL，
 *   其余用 IPV6_GLOBAL_AND_LL)，并置 BATTR_RMAP_IPV6_LL_NHOP_CHANGED 触发重新宣告。
 * 坑：dest 或 table 为空时直接返回 RMAP_OKAY 不做任何修改；本函数影响选路与宣告内容。 */
/* `set ipv6 nexthop local IP_ADDRESS' */

/* Set nexthop to object.  object must be pointer to struct attr. */
static enum route_map_cmd_result_t
route_set_ipv6_nexthop_local(void *rule, const struct prefix *p, void *object)
{
	struct in6_addr *address;
	struct bgp_path_info *path;
	struct bgp_dest *dest;
	struct bgp_table *table = NULL;

	/* Fetch routemap's rule information. */
	address = rule;
	path = object;
	dest = path->net;

	if (!dest)
		return RMAP_OKAY;

	table = bgp_dest_table(dest);
	if (!table)
		return RMAP_OKAY;

	/* Set next hop value. */
	path->attr->mp_nexthop_local = *address;

	/* Set nexthop length. */
	if (table->safi == SAFI_MPLS_VPN || table->safi == SAFI_ENCAP ||
	    table->safi == SAFI_EVPN)
		path->attr->mp_nexthop_len = BGP_ATTR_NHLEN_VPNV6_GLOBAL_AND_LL;
	else
		path->attr->mp_nexthop_len = BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL;

	SET_FLAG(path->attr->rmap_change_flags,
		 BATTR_RMAP_IPV6_LL_NHOP_CHANGED);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_ipv6_nexthop_local_compile：用 inet_pton(AF_INET6) 把参数字符串
 * 解析成 struct in6_addr，内存取自 MTYPE_ROUTE_MAP_COMPILED。
 * 返回：成功返回地址指针，解析失败释放内存并返回 NULL。 */
/* Route map `ip nexthop' compile function.  Given string is converted
   to struct in_addr structure. */
static void *route_set_ipv6_nexthop_local_compile(const char *arg)
{
	int ret;
	struct in6_addr *address;

	address = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct in6_addr));

	ret = inet_pton(AF_INET6, arg, address);

	if (ret == 0) {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, address);
		return NULL;
	}

	return address;
}

/* 【中文补充】route_set_ipv6_nexthop_local_free：释放编译期分配的 in6_addr 内存。 */
/* Free route map's compiled `ip nexthop' value. */
static void route_set_ipv6_nexthop_local_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_set_ipv6_nexthop_local_cmd：把规则名 "ipv6 next-hop local" 绑定到
 * 上述 apply / compile / free 三个回调。 */
/* Route map commands for ip nexthop set. */
static const struct route_map_rule_cmd
		route_set_ipv6_nexthop_local_cmd = {
	"ipv6 next-hop local",
	route_set_ipv6_nexthop_local,
	route_set_ipv6_nexthop_local_compile,
	route_set_ipv6_nexthop_local_free
};

/* 【中文补充】route_set_ipv6_nexthop_peer：set ipv6 next-hop peer-address 的执行体。
 * 入方向：取对端连接地址 su_remote，link-local 写入 mp_nexthop_local(长度 IPV6_GLOBAL_AND_LL)，
 *   全局地址写入 mp_nexthop_global(长度 0 时设为 IPV6_GLOBAL)。
 * 出方向：不直接改地址，只置 BATTR_RMAP_NEXTHOP_PEER_ADDRESS 标志并清空 global / local，
 *   真正下一跳在报文改写时确定。
 * 坑：仅当 rmap_type 为 IN 且对端为 AF_INET6 时入方向才生效。影响选路，需重新宣告。 */
/* `set ipv6 nexthop peer-address' */

/* Set nexthop to object.  object must be pointer to struct attr. */
static enum route_map_cmd_result_t
route_set_ipv6_nexthop_peer(void *rule, const struct prefix *pfx, void *object)
{
	struct in6_addr peer_address;
	struct bgp_path_info *path;
	struct peer *peer;

	/* Fetch routemap's rule information. */
	path = object;
	peer = path->peer;

	if ((CHECK_FLAG(peer->rmap_type, PEER_RMAP_TYPE_IN)) && peer->connection->su_remote &&
	    sockunion_family(peer->connection->su_remote) == AF_INET6) {
		peer_address = peer->connection->su_remote->sin6.sin6_addr;
		/* Set next hop value and length in attribute. */
		if (IN6_IS_ADDR_LINKLOCAL(&peer_address)) {
			path->attr->mp_nexthop_local = peer_address;
			if (path->attr->mp_nexthop_len
			    != BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL)
				path->attr->mp_nexthop_len =
					BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL;
		} else {
			path->attr->mp_nexthop_global = peer_address;
			if (path->attr->mp_nexthop_len == 0)
				path->attr->mp_nexthop_len =
					BGP_ATTR_NHLEN_IPV6_GLOBAL;
		}
	} else if (CHECK_FLAG(peer->rmap_type, PEER_RMAP_TYPE_OUT)) {
		/* The next hop value will be set as part of packet
		 * rewrite.
		 * Set the flags here to indicate that rewrite needs to
		 * be done.
		 * Also, clear the value - we clear both global and
		 * link-local
		 * nexthops, whether we send one or both is determined
		 * elsewhere.
		 */
		SET_FLAG(path->attr->rmap_change_flags,
			 BATTR_RMAP_NEXTHOP_PEER_ADDRESS);
		/* clear next hop value. */
		memset(&(path->attr->mp_nexthop_global), 0,
		       sizeof(struct in6_addr));
		memset(&(path->attr->mp_nexthop_local), 0,
		       sizeof(struct in6_addr));
	}

	return RMAP_OKAY;
}

/* 【中文补充】route_set_ipv6_nexthop_peer_compile：无参数，仅分配一个占位 int(值为 1)；
 * 规则值本身不参与运算。 */
/* Route map `ip next-hop' compile function.  Given string is converted
   to struct in_addr structure. */
static void *route_set_ipv6_nexthop_peer_compile(const char *arg)
{
	int *rins = NULL;

	rins = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(int));
	*rins = 1;

	return rins;
}

/* 【中文补充】route_set_ipv6_nexthop_peer_free：释放占位 int 内存。 */
/* Free route map's compiled `ip next-hop' value. */
static void route_set_ipv6_nexthop_peer_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_set_ipv6_nexthop_peer_cmd：绑定规则名 "ipv6 next-hop peer-address"。 */
/* Route map commands for ip nexthop set. */
static const struct route_map_rule_cmd route_set_ipv6_nexthop_peer_cmd = {
	"ipv6 next-hop peer-address",
	route_set_ipv6_nexthop_peer,
	route_set_ipv6_nexthop_peer_compile,
	route_set_ipv6_nexthop_peer_free
};

/* 【中文补充】route_set_vpnv4_nexthop：set ipv4 vpn next-hop 的执行体(L3VPN)。
 * 把 IPv4 地址写入 path->attr->mp_nexthop_global_in，长度置 BGP_ATTR_NHLEN_IPV4，
 * 并置 BATTR_RMAP_VPNV4_NHOP_CHANGED。影响 VPN 路由下一跳，需重新宣告。 */
/* `set ipv4 vpn next-hop A.B.C.D' */

static enum route_map_cmd_result_t
route_set_vpnv4_nexthop(void *rule, const struct prefix *prefix, void *object)
{
	struct in_addr *address;
	struct bgp_path_info *path;

	/* Fetch routemap's rule information. */
	address = rule;
	path = object;

	/* Set next hop value. */
	path->attr->mp_nexthop_global_in = *address;
	path->attr->mp_nexthop_len = BGP_ATTR_NHLEN_IPV4;

	SET_FLAG(path->attr->rmap_change_flags, BATTR_RMAP_VPNV4_NHOP_CHANGED);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_vpnv4_nexthop_compile：用 inet_aton 解析 IPv4 地址到
 * MTYPE_ROUTE_MAP_COMPILED；失败时释放内存返回 NULL。 */
static void *route_set_vpnv4_nexthop_compile(const char *arg)
{
	int ret;
	struct in_addr *address;

	address = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct in_addr));

	ret = inet_aton(arg, address);

	if (ret == 0) {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, address);
		return NULL;
	}

	return address;
}

/* 【中文补充】route_set_vpnv6_nexthop：set ipv6 vpn next-hop 的执行体(L3VPN)。
 * 把 IPv6 全局地址 memcpy 到 mp_nexthop_global，长度置 BGP_ATTR_NHLEN_VPNV6_GLOBAL，
 * 并置 BATTR_RMAP_VPNV6_GLOBAL_NHOP_CHANGED。影响选路，需重新宣告。 */
/* `set ipv6 vpn next-hop A.B.C.D' */

static enum route_map_cmd_result_t
route_set_vpnv6_nexthop(void *rule, const struct prefix *prefix, void *object)
{
	struct in6_addr *address;
	struct bgp_path_info *path;

	/* Fetch routemap's rule information. */
	address = rule;
	path = object;

	/* Set next hop value. */
	memcpy(&path->attr->mp_nexthop_global, address,
	       sizeof(struct in6_addr));
	path->attr->mp_nexthop_len = BGP_ATTR_NHLEN_VPNV6_GLOBAL;

	SET_FLAG(path->attr->rmap_change_flags,
		 BATTR_RMAP_VPNV6_GLOBAL_NHOP_CHANGED);

	return RMAP_OKAY;
}

/* 【中文补充】route_set_vpnv6_nexthop_compile：用 inet_pton(AF_INET6) 解析地址；
 * 失败时释放内存返回 NULL。 */
static void *route_set_vpnv6_nexthop_compile(const char *arg)
{
	int ret;
	struct in6_addr *address;

	address = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct in6_addr));
	ret = inet_pton(AF_INET6, arg, address);

	if (ret == 0) {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, address);
		return NULL;
	}

	return address;
}

/* 【中文补充】route_set_vpn_nexthop_free：vpnv4 与 vpnv6 下一跳规则共用的释放函数。 */
static void route_set_vpn_nexthop_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_set_vpnv4_nexthop_cmd：绑定规则名 "ipv4 vpn next-hop"。 */
/* Route map commands for ipv4 next-hop set. */
static const struct route_map_rule_cmd route_set_vpnv4_nexthop_cmd = {
	"ipv4 vpn next-hop",
	route_set_vpnv4_nexthop,
	route_set_vpnv4_nexthop_compile,
	route_set_vpn_nexthop_free
};

/* 【中文补充】route_set_vpnv6_nexthop_cmd：绑定规则名 "ipv6 vpn next-hop"。 */
/* Route map commands for ipv6 next-hop set. */
static const struct route_map_rule_cmd route_set_vpnv6_nexthop_cmd = {
	"ipv6 vpn next-hop",
	route_set_vpnv6_nexthop,
	route_set_vpnv6_nexthop_compile,
	route_set_vpn_nexthop_free
};

/* 【中文补充】route_set_originator_id：set originator-id 的执行体。
 * 将规则地址写入 path->attr->originator_id，并调用 bgp_attr_set 标记
 * BGP_ATTR_ORIGINATOR_ID 属性已设置。该属性参与 BGP 环路检测，改动后应重新宣告。 */
/* `set originator-id' */

/* For origin set. */
static enum route_map_cmd_result_t
route_set_originator_id(void *rule, const struct prefix *prefix, void *object)
{
	struct in_addr *address;
	struct bgp_path_info *path;

	address = rule;
	path = object;

	bgp_attr_set(path->attr, BGP_ATTR_ORIGINATOR_ID);
	path->attr->originator_id = *address;

	return RMAP_OKAY;
}

/* 【中文补充】route_set_originator_id_compile：用 inet_aton 解析 IPv4 originator-id；
 * 失败时释放内存返回 NULL。 */
/* Compile function for originator-id set. */
static void *route_set_originator_id_compile(const char *arg)
{
	int ret;
	struct in_addr *address;

	address = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(struct in_addr));

	ret = inet_aton(arg, address);

	if (ret == 0) {
		XFREE(MTYPE_ROUTE_MAP_COMPILED, address);
		return NULL;
	}

	return address;
}

/* 【中文补充】route_set_originator_id_free：释放编译期分配的地址内存。 */
/* Compile function for originator_id set. */
static void route_set_originator_id_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【中文补充】route_set_originator_id_cmd：绑定规则名 "originator-id"。 */
/* Set originator-id rule structure. */
static const struct route_map_rule_cmd route_set_originator_id_cmd = {
	"originator-id",
	route_set_originator_id,
	route_set_originator_id_compile,
	route_set_originator_id_free,
};

/* 【中文补充】route_match_rpki_extcommunity：match rpki-extcommunity 的执行体(ROA 源验证)。
 * 从 path 的扩展团体里查 OPAQUE_NON_TRANS 类型、ORIGIN_VALIDATION_STATE 子类型属性，
 * 其最后一个字节(val[7])即 RPKI 状态，映射为 VALID / NOTFOUND / INVALID。
 * 与规则期望状态 rpki_status 相等返回 RMAP_MATCH，否则 RMAP_NOMATCH；
 * 缺扩展团体或缺该属性时也返回 RMAP_NOMATCH。只读匹配，不修改路径。 */
static enum route_map_cmd_result_t
route_match_rpki_extcommunity(void *rule, const struct prefix *prefix,
			      void *object)
{
	struct bgp_path_info *path;
	struct ecommunity *ecomm;
	struct ecommunity_val *ecomm_val;
	enum rpki_states *rpki_status = rule;
	enum rpki_states ecomm_rpki_status = RPKI_NOT_BEING_USED;

	path = object;

	ecomm = bgp_attr_get_ecommunity(path->attr);
	if (!ecomm)
		return RMAP_NOMATCH;

	ecomm_val = ecommunity_lookup(ecomm, ECOMMUNITY_ENCODE_OPAQUE_NON_TRANS,
				      ECOMMUNITY_ORIGIN_VALIDATION_STATE);
	if (!ecomm_val)
		return RMAP_NOMATCH;

	/* The Origin Validation State is encoded in the last octet of
	 * the extended community.
	 */
	switch (ecomm_val->val[7]) {
	case ECOMMUNITY_ORIGIN_VALIDATION_STATE_VALID:
		ecomm_rpki_status = RPKI_VALID;
		break;
	case ECOMMUNITY_ORIGIN_VALIDATION_STATE_NOTFOUND:
		ecomm_rpki_status = RPKI_NOTFOUND;
		break;
	case ECOMMUNITY_ORIGIN_VALIDATION_STATE_INVALID:
		ecomm_rpki_status = RPKI_INVALID;
		break;
	case ECOMMUNITY_ORIGIN_VALIDATION_STATE_NOTUSED:
		break;
	}

	if (ecomm_rpki_status == *rpki_status)
		return RMAP_MATCH;

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_extcommunity_compile：把 "valid" 文本编译为 RPKI_VALID、
 * "invalid" 为 RPKI_INVALID，其余一律 RPKI_NOTFOUND，存入 MTYPE_ROUTE_MAP_COMPILED。
 * 注意函数名看似扩展团体编译，实为 origin validation 状态编译。 */
static void *route_match_extcommunity_compile(const char *arg)
{
	int *rpki_status;

	rpki_status = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(int));

	if (strcmp(arg, "valid") == 0)
		*rpki_status = RPKI_VALID;
	else if (strcmp(arg, "invalid") == 0)
		*rpki_status = RPKI_INVALID;
	else
		*rpki_status = RPKI_NOTFOUND;

	return rpki_status;
}

/* 【中文补充】route_match_rpki_extcommunity_cmd：绑定规则名 "rpki-extcommunity"，
 * free 回调复用通用 route_value_free。 */
static const struct route_map_rule_cmd route_match_rpki_extcommunity_cmd = {
	"rpki-extcommunity",
	route_match_rpki_extcommunity,
	route_match_extcommunity_compile,
	route_value_free
};

/* 【中文补充】route_match_vpn_dataplane：match vpn dataplane 的执行体。
 * 依据表 safi 判断承载平面：EVPN 比较 attr->encap_tunneltype；L3VPN 检查 MPLS 标签
 * 是否有效，或 SRv6(l3service / vpn 标志)。命中返回 RMAP_MATCH，否则 RMAP_NOMATCH。
 * 坑：unicast 表或表为空时直接返回 RMAP_OKAY(该规则在此不做判定)；只读匹配。 */
static enum route_map_cmd_result_t
route_match_vpn_dataplane(void *rule, const struct prefix *prefix, void *object)
{
	struct bgp_path_info *path_vpn;
	bgp_encap_types *bgp_encap_type = rule;
	struct bgp_dest *dest;
	struct bgp_table *table = NULL;

	/* Fetch routemap's rule information. */
	path_vpn = object;
	dest = path_vpn->net;

	assert(path_vpn->attr);

	if (dest)
		table = bgp_dest_table(dest);

	if (!table || table->safi == SAFI_UNICAST)
		/* applied to unicast packet, find out the path_vpn origin. XXX */
		return RMAP_OKAY;

	/* for L2VPN */
	if (table->safi == SAFI_EVPN) {
		if (path_vpn->attr->encap_tunneltype == *bgp_encap_type)
			return RMAP_MATCH;
		return RMAP_NOMATCH;
	}
	/* for L3VPN */
	if (*bgp_encap_type == BGP_ENCAP_TYPE_MPLS &&
	    bgp_mplsvpn_path_uses_valid_mpls_label(path_vpn))
		return RMAP_MATCH;

	if (*bgp_encap_type == BGP_ENCAP_TYPE_SRV6 &&
	    (path_vpn->attr->srv6_l3service || path_vpn->attr->srv6_vpn))
		return RMAP_MATCH;

	return RMAP_NOMATCH;
}

/* 【中文补充】route_match_vpn_dataplane_compile：把 "vxlan" / "mpls" / "srv6" 文本编译成
 * bgp_encap_types 数值存入 MTYPE_ROUTE_MAP_COMPILED。 */
static void *route_match_vpn_dataplane_compile(const char *arg)
{
	uint16_t *tunnel_type;

	tunnel_type = XCALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(uint32_t));

	if (strmatch(arg, "vxlan"))
		*tunnel_type = BGP_ENCAP_TYPE_VXLAN;
	else if (strmatch(arg, "mpls"))
		*tunnel_type = BGP_ENCAP_TYPE_MPLS;
	else if (strmatch(arg, "srv6"))
		*tunnel_type = BGP_ENCAP_TYPE_SRV6;

	return tunnel_type;
}

/* 【中文补充】route_match_vpn_dataplane_cmd：绑定规则名 "vpn dataplane"，free 复用
 * 通用 route_value_free。 */
static const struct route_map_rule_cmd route_match_vpn_dataplane_cmd = {
	"vpn dataplane", route_match_vpn_dataplane, route_match_vpn_dataplane_compile,
	route_value_free
};


/* 【中文补充】bgp_route_map_process_peer：route-map 变更时对单个 peer 的处理(工作函数)。
 * 参数：rmap_name 变更的 map 名；map 新对象；peer 目标邻居；afi / safi 地址族；
 *   route_update 是否触发路由更新。
 * 名字匹配 filter->map[RMAP_IN] 时更新 map 指针，且对端已建连时：有 SOFT_RECONFIG
 *   则调 bgp_soft_reconfig_in 软重配，否则有 route-refresh 能力则发
 *   BGP_ROUTE_REFRESH_NORMAL。出方向与 usmap 只更新配置引用，实际宣告在 group 层完成。
 * 坑：peer 或 rmap_name 为空时直接返回。 */
/*
 * This is the workhorse routine for processing in/out routemap
 * modifications.
 */
static void bgp_route_map_process_peer(const char *rmap_name,
				       struct route_map *map, struct peer *peer,
				       int afi, int safi, int route_update)
{
	struct bgp_filter *filter;

	if (!peer || !rmap_name)
		return;

	filter = &peer->filter[afi][safi];
	/*
	 * in is for non-route-server clients,
	 * out is for all peers
	 */
	if (filter->map[RMAP_IN].name
	    && (strcmp(rmap_name, filter->map[RMAP_IN].name) == 0)) {
		filter->map[RMAP_IN].map = map;

		if (route_update && peer_established(peer->connection)) {
			if (CHECK_FLAG(peer->af_flags[afi][safi],
				       PEER_FLAG_SOFT_RECONFIG)) {
				if (bgp_debug_update(peer, NULL, NULL, 1))
					zlog_debug(
						"Processing route_map %s(%s:%s) update on peer %s (inbound, soft-reconfig)",
						rmap_name, afi2str(afi),
						safi2str(safi), peer->host);

				bgp_soft_reconfig_in(peer, afi, safi);
			} else if (CHECK_FLAG(peer->cap, PEER_CAP_REFRESH_RCV)) {
				if (bgp_debug_update(peer, NULL, NULL, 1))
					zlog_debug(
						"Processing route_map %s(%s:%s) update on peer %s (inbound, route-refresh)",
						rmap_name, afi2str(afi),
						safi2str(safi), peer->host);
				bgp_route_refresh_send(
					peer, afi, safi, 0, 0, 0,
					BGP_ROUTE_REFRESH_NORMAL);
			}
		}
	}

	/*
	 * For outbound, unsuppress and default-originate map change (content or
	 * map created), merely update the "config" here, the actual route
	 * announcement happens at the group level.
	 */
	if (filter->map[RMAP_OUT].name
	    && (strcmp(rmap_name, filter->map[RMAP_OUT].name) == 0))
		filter->map[RMAP_OUT].map = map;

	if (filter->usmap.name && (strcmp(rmap_name, filter->usmap.name) == 0))
		filter->usmap.map = map;

	if (filter->advmap.aname
	    && (strcmp(rmap_name, filter->advmap.aname) == 0)) {
		filter->advmap.amap = map;
	}

	if (filter->advmap.cname
	    && (strcmp(rmap_name, filter->advmap.cname) == 0)) {
		filter->advmap.cmap = map;
	}

	if (peer->default_rmap[afi][safi].name
	    && (strcmp(rmap_name, peer->default_rmap[afi][safi].name) == 0))
		peer->default_rmap[afi][safi].map = map;

	/* Notify BGP conditional advertisement scanner percess */
	peer->advmap_config_change[afi][safi] = true;
}

/* 【中文补充】bgp_route_map_update_peer_group：更新 peer-group 模板里的 route-map 引用。
 * 对每个 group、每个 afi / safi，把匹配 rmap_name 的 map[direct](IN / OUT)、
 * default_rmap、usmap、advmap(aname / cname)指针替换成新的 map。
 * 坑：这里只更新占位数据(template 配置)，真正的成员 peer 已在别处更新；bgp 为空直接返回。 */
static void bgp_route_map_update_peer_group(const char *rmap_name,
					    struct route_map *map,
					    struct bgp *bgp)
{
	struct peer_group *group;
	struct listnode *node, *nnode;
	struct bgp_filter *filter;
	int afi, safi;
	int direct;

	if (!bgp)
		return;

	/* All the peers have been updated correctly already. This is
	 * just updating the placeholder data. No real update required.
	 */
	for (ALL_LIST_ELEMENTS(bgp->group, node, nnode, group)) {
		FOREACH_AFI_SAFI (afi, safi) {
			filter = &group->conf->filter[afi][safi];

			for (direct = RMAP_IN; direct < RMAP_MAX; direct++) {
				if ((filter->map[direct].name)
				    && (strcmp(rmap_name,
					       filter->map[direct].name)
					== 0))
					filter->map[direct].map = map;

				if (group->conf->default_rmap[afi][safi].name &&
				    strmatch(group->conf->default_rmap[afi][safi]
						     .name,
					     rmap_name))
					group->conf->default_rmap[afi][safi].map =
						map;
			}

			if (filter->usmap.name
			    && (strcmp(rmap_name, filter->usmap.name) == 0))
				filter->usmap.map = map;

			if (filter->advmap.aname &&
			    (strcmp(rmap_name, filter->advmap.aname) == 0))
				filter->advmap.amap = map;

			if (filter->advmap.cname &&
			    (strcmp(rmap_name, filter->advmap.cname) == 0))
				filter->advmap.cmap = map;
		}
	}
}

/* 【中文补充】bgp_route_map_process_update：route-map 变更后整台 BGP 实例的重处理主流程。
 * 依次处理：遍历所有非 group peer 调 bgp_route_map_process_peer；出方向与 default-orig
 *   交给 update_group_policy_update；更新 peer-group 模板；再按 afi / safi 处理
 *   table_map(改 counter 并按需 bgp_zebra_announce_table)、static_routes(network 命令)、
 *   aggregate(汇总，含 suppress_map)、redistribute(重新分发)、以及 type5 的 adv_cmd_rmap。
 * 参数：route_update 为真才真正触发路由重算与重新宣告，否则仅替换指针。
 * 坑：route-map 数量与 peer 数量都极大时本函数开销很高(英文注释已说明)。 */
/*
 * Note that if an extreme number (tens of thousands) of route-maps are in use
 * and if bgp has an extreme number of peers, network statements, etc then this
 * function can consume a lot of cycles. This is due to this function being
 * called for each route-map and within this function we walk the list of peers,
 * network statements, etc looking to see if they use this route-map.
 */
static void bgp_route_map_process_update(struct bgp *bgp, const char *rmap_name,
					 bool route_update)
{
	int i;
	bool matched;
	afi_t afi;
	safi_t safi;
	struct peer *peer;
	struct bgp_dest *bn;
	struct bgp_static *bgp_static;
	struct bgp_aggregate *aggregate;
	struct listnode *node, *nnode;
	struct route_map *map;
	char buf[INET6_ADDRSTRLEN];

	map = route_map_lookup_by_name(rmap_name);

	for (ALL_LIST_ELEMENTS(bgp->peer, node, nnode, peer)) {

		/* Ignore dummy peer-group structure */
		if (CHECK_FLAG(peer->sflags, PEER_STATUS_GROUP))
			continue;

		FOREACH_AFI_SAFI (afi, safi) {
			/* process in/out/import/export/default-orig
			 * route-maps */
			bgp_route_map_process_peer(rmap_name, map, peer, afi,
						   safi, route_update);
		}
	}

	/* for outbound/default-orig route-maps, process for groups */
	update_group_policy_update(bgp, BGP_POLICY_ROUTE_MAP, rmap_name,
				   route_update, 0);

	/* update peer-group config (template) */
	bgp_route_map_update_peer_group(rmap_name, map, bgp);

	FOREACH_AFI_SAFI (afi, safi) {
		/* For table route-map updates. */
		if (!bgp_fibupd_safi(safi))
			continue;

		if (bgp->table_map[afi][safi].name
		    && (strcmp(rmap_name, bgp->table_map[afi][safi].name)
			== 0)) {

			/* bgp->table_map[afi][safi].map  is NULL.
			 * i.e Route map creation event.
			 * So update applied_counter.
			 * If it is not NULL, i.e It may be routemap updation or
			 * deletion. so no need to update the counter.
			 */
			if (!bgp->table_map[afi][safi].map)
				route_map_counter_increment(map);
			bgp->table_map[afi][safi].map = map;

			if (BGP_DEBUG(zebra, ZEBRA))
				zlog_debug(
					"Processing route_map %s(%s:%s) update on table map",
					rmap_name, afi2str(afi),
					safi2str(safi));
			if (route_update)
				bgp_zebra_announce_table(bgp, afi, safi);
		}

		/* For network route-map updates. */
		for (bn = bgp_table_top(bgp->static_routes[afi][safi]); bn;
		     bn = bgp_route_next(bn)) {
			bgp_static = bgp_dest_get_bgp_static_info(bn);
			if (!bgp_static)
				continue;

			if (!bgp_static->rmap.name
			    || (strcmp(rmap_name, bgp_static->rmap.name) != 0))
				continue;

			if (!bgp_static->rmap.map)
				route_map_counter_increment(map);

			bgp_static->rmap.map = map;

			if (route_update && !bgp_static->backdoor) {
				const struct prefix *bn_p =
					bgp_dest_get_prefix(bn);

				if (bgp_debug_zebra(bn_p))
					zlog_debug(
						"Processing route_map %s(%s:%s) update on static route %s",
						rmap_name, afi2str(afi),
						safi2str(safi),
						inet_ntop(bn_p->family,
							  &bn_p->u.prefix, buf,
							  sizeof(buf)));
				bgp_static_update(bgp, bn_p, bgp_static, afi,
						  safi);
			}
		}

		/* For aggregate-address route-map updates. */
		for (bn = bgp_table_top(bgp->aggregate[afi][safi]); bn;
		     bn = bgp_route_next(bn)) {
			aggregate = bgp_dest_get_bgp_aggregate_info(bn);
			if (!aggregate)
				continue;

			matched = false;

			/* Update suppress map pointer. */
			if (aggregate->suppress_map_name
			    && strmatch(aggregate->suppress_map_name,
					rmap_name)) {
				if (aggregate->rmap.map == NULL)
					route_map_counter_increment(map);

				aggregate->suppress_map = map;

				bgp_aggregate_toggle_suppressed(
					aggregate, bgp, bgp_dest_get_prefix(bn),
					afi, safi, false);

				matched = true;
			}

			if (aggregate->rmap.name
			    && strmatch(rmap_name, aggregate->rmap.name)) {
				if (aggregate->rmap.map == NULL)
					route_map_counter_increment(map);

				aggregate->rmap.map = map;

				matched = true;
			}

			if (matched && route_update) {
				const struct prefix *bn_p =
					bgp_dest_get_prefix(bn);

				if (bgp_debug_zebra(bn_p))
					zlog_debug(
						"Processing route_map %s(%s:%s) update on aggregate-address route %s",
						rmap_name, afi2str(afi),
						safi2str(safi),
						inet_ntop(bn_p->family,
							  &bn_p->u.prefix, buf,
							  sizeof(buf)));
				(void)bgp_aggregate_route(bgp, bn_p, afi, safi,
							  aggregate);
			}
		}
	}

	/* For redistribute route-map updates. */
	for (afi = AFI_IP; afi < AFI_MAX; afi++)
		for (i = 0; i < ZEBRA_ROUTE_MAX; i++) {
			struct list *red_list;
			struct bgp_redist *red;

			red_list = bgp->redist[afi][i];
			if (!red_list)
				continue;

			for (ALL_LIST_ELEMENTS_RO(red_list, node, red)) {
				if (!red->rmap.name
				    || (strcmp(rmap_name, red->rmap.name) != 0))
					continue;

				if (!red->rmap.map)
					route_map_counter_increment(map);

				red->rmap.map = map;

				if (!route_update)
					continue;

				if (BGP_DEBUG(zebra, ZEBRA))
					zlog_debug(
						"Processing route_map %s(%s:%s) update on redistributed routes",
						rmap_name, afi2str(afi),
						safi2str(safi));

				bgp_redistribute_resend(bgp, afi, i,
							red->instance);
			}
		}

	/* for type5 command route-maps */
	FOREACH_AFI_SAFI (afi, safi) {
		if (!bgp->adv_cmd_rmap[afi][safi].name
		    || strcmp(rmap_name, bgp->adv_cmd_rmap[afi][safi].name)
			       != 0)
			continue;

		/* Make sure the route-map is populated here if not already done */
		bgp->adv_cmd_rmap[afi][safi].map = map;

		if (BGP_DEBUG(zebra, ZEBRA))
			zlog_debug(
				"Processing route_map %s(%s:%s) update on advertise type5 route command",
				rmap_name, afi2str(afi), safi2str(safi));

		if (route_update && (advertise_type5_routes_bestpath(bgp, afi) ||
				     advertise_type5_routes_multipath(bgp, afi))) {
			bgp_evpn_withdraw_type5_routes(bgp, afi, safi);
			bgp_evpn_advertise_type5_routes(bgp, afi, safi);
		}
	}
}

/* 【中文补充】bgp_route_map_process_update_cb：route_map_walk_update_list 的回调。
 * 对 bm->bgp 里每个 BGP 实例以 route_update=true 调 process_update，
 * 最后通知 VPN policy 模块。带 ENABLE_BGP_VNC 时额外调 vnc_routemap_update。 */
static void bgp_route_map_process_update_cb(char *rmap_name)
{
	struct listnode *node, *nnode;
	struct bgp *bgp;

	for (ALL_LIST_ELEMENTS(bm->bgp, node, nnode, bgp)) {
		bgp_route_map_process_update(bgp, rmap_name, true);

#ifdef ENABLE_BGP_VNC
		vnc_routemap_update(bgp, __func__);
#endif
	}

	vpn_policy_routemap_event(rmap_name);
}

/* 【中文补充】bgp_route_map_update_timer：延时更新定时器的回调。
 * 触发 route_map_walk_update_list，遍历待更新列表里的 route-map 并逐个回调处理。 */
void bgp_route_map_update_timer(struct event *event)
{
	route_map_walk_update_list(bgp_route_map_process_update_cb);
}

/* 【中文补充】bgp_route_map_mark_update：把某个 route-map 的变更标记为待处理。
 * 先取消已有定时器；若 bm->rmap_update_timer 非 0 则重起定时器(合并多次变更，
 *   并通知各 group route-map 事件已开始)，否则立即对所有 BGP 实例调 process_update
 *   并通知 VPN policy。坑：定时器为 0 表示不做延时聚合，直接同步处理。 */
static void bgp_route_map_mark_update(const char *rmap_name)
{
	struct listnode *node, *nnode;
	struct bgp *bgp;

	/* If new update is received before the current timer timed out,
	 * turn it off and start a new timer.
	 */
	event_cancel(&bm->t_rmap_update);

	/* rmap_update_timer of 0 means don't do route updates */
	if (bm->rmap_update_timer) {
		event_add_timer(bm->master, bgp_route_map_update_timer, NULL,
				bm->rmap_update_timer, &bm->t_rmap_update);

		/* Signal the groups that a route-map update event has
		 * started */
		for (ALL_LIST_ELEMENTS(bm->bgp, node, nnode, bgp))
			update_group_policy_update(bgp, BGP_POLICY_ROUTE_MAP,
						   rmap_name, true, 1);
	} else {
		for (ALL_LIST_ELEMENTS(bm->bgp, node, nnode, bgp)) {
			bgp_route_map_process_update(bgp, rmap_name, false);
#ifdef ENABLE_BGP_VNC
			vnc_routemap_update(bgp, __func__);
#endif
		}

		vpn_policy_routemap_event(rmap_name);
	}
}

/* 【中文补充】bgp_route_map_add：route-map 被创建时的入口。
 * 若尚需更新则调 mark_update 安排处理，再以 RMAP_EVENT_MATCH_ADDED 通知依赖它的模块。 */
static void bgp_route_map_add(const char *rmap_name)
{
	if (route_map_mark_updated(rmap_name) == 0)
		bgp_route_map_mark_update(rmap_name);

	route_map_notify_dependencies(rmap_name, RMAP_EVENT_MATCH_ADDED);
}

/* 【中文补充】bgp_route_map_delete：route-map 被删除时的入口。
 * 语义同 add，但通知依赖模块的事件为 RMAP_EVENT_MATCH_DELETED。 */
static void bgp_route_map_delete(const char *rmap_name)
{
	if (route_map_mark_updated(rmap_name) == 0)
		bgp_route_map_mark_update(rmap_name);

	route_map_notify_dependencies(rmap_name, RMAP_EVENT_MATCH_DELETED);
}

/* 【中文补充】bgp_route_map_has_extcommunity_rt：判断一个 route-map 的 set 列表里
 * 是否含 "extcommunity rt" 或 "extended-comm-list" 规则(供 EVPN 与 RT 相关逻辑使用)。
 * 遍历所有 index 的 set_list，命中即返回 true，否则 false；map 为空会 assert 失败。 */
bool bgp_route_map_has_extcommunity_rt(const struct route_map *map)
{
	struct route_map_index *index = NULL;
	struct route_map_rule *set = NULL;

	assert(map);

	for (index = map->head; index; index = index->next) {
		for (set = index->set_list.head; set; set = set->next) {
			if (set->cmd && set->cmd->str &&
			    (strmatch(set->cmd->str, "extcommunity rt") ||
			     strmatch(set->cmd->str, "extended-comm-list")))
				return true;
		}
	}
	return false;
}

/* 【中文补充】bgp_route_map_event：route-map 变更事件入口(与 add 类似)。
 * 若需更新则安排处理，并以 RMAP_EVENT_MATCH_ADDED 通知依赖模块。 */
static void bgp_route_map_event(const char *rmap_name)
{
	if (route_map_mark_updated(rmap_name) == 0)
		bgp_route_map_mark_update(rmap_name);

	route_map_notify_dependencies(rmap_name, RMAP_EVENT_MATCH_ADDED);
}

/* 【中文补充】match_mac_address：CLI "match mac address <NAME>"(YANG / nb 实现)。
 * 创建 mac-address-list 类型的 match-condition，并写入 MAC 列表名。
 * 依赖 MAC 访问列表判断路由 MAC 是否命中；只读匹配，不修改路径。 */
DEFUN_YANG (match_mac_address,
	    match_mac_address_cmd,
	    "match mac address ACCESSLIST_MAC_NAME",
	    MATCH_STR
	    "mac address\n"
	    "Match address of route\n"
	    "MAC Access-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:mac-address-list']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:list-name", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, argv[3]->arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_match_mac_address：上述命令的 no 形式，销毁对应 match-condition 节点。 */
DEFUN_YANG (no_match_mac_address,
	    no_match_mac_address_cmd,
	    "no match mac address ACCESSLIST_MAC_NAME",
	    NO_STR
	    MATCH_STR
	    "mac\n"
	    "Match address of route\n"
	    "MAC acess-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:mac-address-list']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】parse_evpn_rt_type：把 EVPN 路由类型参数规整成字符串。
 * 传入单字符 '1'~'5' 时映射为 ead / macip / multicast / es / prefix；
 * 否则原样返回(视为已是完整类型串)。返回静态字符串，不需释放。 */
/*
 * Helper to handle the case of the user passing in a number or type string
 */
static const char *parse_evpn_rt_type(const char *num_rt_type)
{
	switch (num_rt_type[0]) {
	case '1':
		return "ead";
	case '2':
		return "macip";
	case '3':
		return "multicast";
	case '4':
		return "es";
	case '5':
		return "prefix";
	default:
		break;
	}

	/* Was already full type string */
	return num_rt_type;
}

/* 【中文补充】match_evpn_route_type：CLI "match evpn route-type"。
 * 创建 evpn-route-type match-condition，值经 parse_evpn_rt_type 规整后写入。 */
DEFUN_YANG (match_evpn_route_type,
	    match_evpn_route_type_cmd,
	    "match evpn route-type <ead|1|macip|2|multicast|3|es|4|prefix|5>",
	    MATCH_STR
	    EVPN_HELP_STR
	    EVPN_TYPE_HELP_STR
	    EVPN_TYPE_1_HELP_STR
	    EVPN_TYPE_1_HELP_STR
	    EVPN_TYPE_2_HELP_STR
	    EVPN_TYPE_2_HELP_STR
	    EVPN_TYPE_3_HELP_STR
	    EVPN_TYPE_3_HELP_STR
	    EVPN_TYPE_4_HELP_STR
	    EVPN_TYPE_4_HELP_STR
	    EVPN_TYPE_5_HELP_STR
	    EVPN_TYPE_5_HELP_STR)
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:evpn-route-type']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:evpn-route-type",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			      parse_evpn_rt_type(argv[3]->arg));

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_match_evpn_route_type：上述命令的 no 形式，销毁对应节点。 */
DEFUN_YANG (no_match_evpn_route_type,
	    no_match_evpn_route_type_cmd,
	    "no match evpn route-type <ead|1|macip|2|multicast|3|es|4|prefix|5>",
	    NO_STR
	    MATCH_STR
	    EVPN_HELP_STR
	    EVPN_TYPE_HELP_STR
	    EVPN_TYPE_1_HELP_STR
	    EVPN_TYPE_1_HELP_STR
	    EVPN_TYPE_2_HELP_STR
	    EVPN_TYPE_2_HELP_STR
	    EVPN_TYPE_3_HELP_STR
	    EVPN_TYPE_3_HELP_STR
	    EVPN_TYPE_4_HELP_STR
	    EVPN_TYPE_4_HELP_STR
	    EVPN_TYPE_5_HELP_STR
	    EVPN_TYPE_5_HELP_STR)
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:evpn-route-type']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】match_evpn_vni：CLI "match evpn vni"。
 * 创建 evpn-vni 类型 match-condition，并写入 VNI 值。 */
DEFUN_YANG (match_evpn_vni,
	    match_evpn_vni_cmd,
	    "match evpn vni " CMD_VNI_RANGE,
	    MATCH_STR
	    EVPN_HELP_STR
	    "Match VNI\n"
	    "VNI ID\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:evpn-vni']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:evpn-vni", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, argv[3]->arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_match_evpn_vni：上述命令的 no 形式，销毁该 match-condition 与对应值。 */
DEFUN_YANG (no_match_evpn_vni,
	    no_match_evpn_vni_cmd,
	    "no match evpn vni " CMD_VNI_RANGE,
	    NO_STR
	    MATCH_STR
	    EVPN_HELP_STR
	    "Match VNI\n"
	    "VNI ID\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:evpn-vni']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:evpn-vni", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_DESTROY, argv[3]->arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_evpn_default_route：CLI "match evpn default-route"。
 * 创建 evpn-default-route match-condition，匹配 EVPN type-5 默认路由，无附加值。 */
DEFUN_YANG (match_evpn_default_route,
	    match_evpn_default_route_cmd,
	    "match evpn default-route",
	    MATCH_STR
	    EVPN_HELP_STR
	    "default EVPN type-5 route\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:evpn-default-route']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:evpn-default-route",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_match_evpn_default_route：上述命令的 no 形式，销毁对应节点。 */
DEFUN_YANG (no_match_evpn_default_route,
	    no_match_evpn_default_route_cmd,
	    "no match evpn default-route",
	    NO_STR
	    MATCH_STR
	    EVPN_HELP_STR
	    "default EVPN type-5 route\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:evpn-default-route']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_evpn_rd：CLI "match evpn rd"。
 * 创建 evpn-rd match-condition，并写入 route-distinguisher 值。 */
DEFUN_YANG (match_evpn_rd,
	    match_evpn_rd_cmd,
	    "match evpn rd ASN:NN_OR_IP-ADDRESS:NN",
	    MATCH_STR
	    EVPN_HELP_STR
	    "Route Distinguisher\n"
	    "ASN:XX or A.B.C.D:XX\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:evpn-rd']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(
		xpath_value, sizeof(xpath_value),
		"%s/rmap-match-condition/frr-bgp-route-map:route-distinguisher",
		xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, argv[3]->arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_match_evpn_rd：上述命令的 no 形式，销毁对应节点。 */
DEFUN_YANG (no_match_evpn_rd,
	    no_match_evpn_rd_cmd,
	    "no match evpn rd ASN:NN_OR_IP-ADDRESS:NN",
	    NO_STR
	    MATCH_STR
	    EVPN_HELP_STR
	    "Route Distinguisher\n"
	    "ASN:XX or A.B.C.D:XX\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:evpn-rd']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_evpn_gw_ip_ipv4：CLI "set evpn gateway-ip ipv4 A.B.C.D"。
 * 解析并校验网关地址(拒绝 0.0.0.0、组播与保留地址)，再写入 set-action。
 * 为 type-5 前缀路由设置网关 IP；影响 EVPN 路由属性，需重新宣告。 */
DEFUN_YANG (set_evpn_gw_ip_ipv4,
	    set_evpn_gw_ip_ipv4_cmd,
	    "set evpn gateway-ip ipv4 A.B.C.D",
	    SET_STR
	    EVPN_HELP_STR
	    "Set gateway IP for prefix advertisement route\n"
	    "IPv4 address\n"
	    "Gateway IP address in IPv4 format\n")
{
	int ret;
	union sockunion su;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-evpn-gateway-ip-ipv4']";
	char xpath_value[XPATH_MAXLEN];

	ret = str2sockunion(argv[4]->arg, &su);
	if (ret < 0) {
		vty_out(vty, "%% Malformed gateway IP\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	if (su.sin.sin_addr.s_addr == 0 ||
	    !ipv4_unicast_valid(&su.sin.sin_addr)) {
		vty_out(vty,
			"%% Gateway IP cannot be 0.0.0.0, multicast or reserved\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:evpn-gateway-ip-ipv4",
		 xpath);

	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, argv[4]->arg);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_evpn_gw_ip_ipv4：上述命令的 no 形式，同样校验地址后销毁对应 set-action。 */
DEFUN_YANG (no_set_evpn_gw_ip_ipv4,
	    no_set_evpn_gw_ip_ipv4_cmd,
	    "no set evpn gateway-ip ipv4 A.B.C.D",
	    NO_STR
	    SET_STR
	    EVPN_HELP_STR
	    "Set gateway IP for prefix advertisement route\n"
	    "IPv4 address\n"
	    "Gateway IP address in IPv4 format\n")
{
	int ret;
	union sockunion su;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-evpn-gateway-ip-ipv4']";

	ret = str2sockunion(argv[5]->arg, &su);
	if (ret < 0) {
		vty_out(vty, "%% Malformed gateway IP\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	if (su.sin.sin_addr.s_addr == 0 ||
	    !ipv4_unicast_valid(&su.sin.sin_addr)) {
		vty_out(vty,
			"%% Gateway IP cannot be 0.0.0.0, multicast or reserved\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_evpn_gw_ip_ipv6：CLI "set evpn gateway-ip ipv6 X:X::X:X"。
 * 解析校验 IPv6 网关地址并写入 set-action，语义同 IPv4 版本。 */
DEFUN_YANG (set_evpn_gw_ip_ipv6,
	    set_evpn_gw_ip_ipv6_cmd,
	    "set evpn gateway-ip ipv6 X:X::X:X",
	    SET_STR
	    EVPN_HELP_STR
	    "Set gateway IP for prefix advertisement route\n"
	    "IPv6 address\n"
	    "Gateway IP address in IPv6 format\n")
{
	int ret;
	union sockunion su;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-evpn-gateway-ip-ipv6']";
	char xpath_value[XPATH_MAXLEN];

	ret = str2sockunion(argv[4]->arg, &su);
	if (ret < 0) {
		vty_out(vty, "%% Malformed gateway IP\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	if (IN6_IS_ADDR_LINKLOCAL(&su.sin6.sin6_addr)
	    || IN6_IS_ADDR_MULTICAST(&su.sin6.sin6_addr)) {
		vty_out(vty,
			"%% Gateway IP cannot be a linklocal or multicast address\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:evpn-gateway-ip-ipv6",
		 xpath);

	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, argv[4]->arg);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_evpn_gw_ip_ipv6：set evpn gateway-ip ipv6 的 no 形式。
 * 解析并校验网关地址(拒绝 link-local 与组播)后销毁对应 set-action。 */
DEFUN_YANG (no_set_evpn_gw_ip_ipv6,
	    no_set_evpn_gw_ip_ipv6_cmd,
	    "no set evpn gateway-ip ipv6 X:X::X:X",
	    NO_STR
	    SET_STR
	    EVPN_HELP_STR
	    "Set gateway IP for prefix advertisement route\n"
	    "IPv4 address\n"
	    "Gateway IP address in IPv4 format\n")
{
	int ret;
	union sockunion su;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-evpn-gateway-ip-ipv6']";

	ret = str2sockunion(argv[5]->arg, &su);
	if (ret < 0) {
		vty_out(vty, "%% Malformed gateway IP\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	if (IN6_IS_ADDR_LINKLOCAL(&su.sin6.sin6_addr)
	    || IN6_IS_ADDR_MULTICAST(&su.sin6.sin6_addr)) {
		vty_out(vty,
			"%% Gateway IP cannot be a linklocal or multicast address\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_vrl_source_vrf：CLI "match source-vrf NAME"(YANG / nb 实现)。
 * 创建 source-vrf match-condition，并把 VRF 名写入。用于匹配路由来源 VRF。 */
DEFPY_YANG(match_vrl_source_vrf,
      match_vrl_source_vrf_cmd,
      "match source-vrf NAME$vrf_name",
      MATCH_STR
      "source vrf\n"
      "The VRF name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:source-vrf']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:source-vrf", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, vrf_name);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_match_vrl_source_vrf：上述命令的 no 形式，销毁对应 match-condition。 */
DEFPY_YANG(no_match_vrl_source_vrf,
      no_match_vrl_source_vrf_cmd,
      "no match source-vrf NAME$vrf_name",
      NO_STR MATCH_STR
      "source vrf\n"
      "The VRF name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:source-vrf']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_peer：CLI "match peer <A.B.C.D|X:X::X:X|WORD>"。
 * 创建 peer match-condition，并按参数是否给出分别 MODIFY / DESTROY 三个子节点
 * (peer-ipv4-address、peer-ipv6-address、peer-interface)。
 * 依赖对端地址与接口匹配；只读匹配，不修改路径。 */
DEFPY_YANG (match_peer,
       match_peer_cmd,
       "match peer <A.B.C.D$addrv4|X:X::X:X$addrv6|WORD$intf>",
       MATCH_STR
       "Match peer address\n"
       "IP address of peer\n"
       "IPv6 address of peer\n"
       "Interface name of peer or peer group name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:peer']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:peer-ipv4-address",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value,
			      addrv4_str ? NB_OP_MODIFY : NB_OP_DESTROY,
			      addrv4_str);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:peer-ipv6-address",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value,
			      addrv6_str ? NB_OP_MODIFY : NB_OP_DESTROY,
			      addrv6_str);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:peer-interface",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value,
			      intf ? NB_OP_MODIFY : NB_OP_DESTROY, intf);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_peer_local：CLI "match peer local"。
 * 在 peer match-condition 下把 peer-local 置为 "true"，用于匹配本地(静态或重分发)路由。 */
DEFUN_YANG (match_peer_local,
	    match_peer_local_cmd,
	    "match peer local",
	    MATCH_STR
	    "Match peer address\n"
	    "Static or Redistributed routes\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:peer']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:peer-local", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, "true");

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_match_peer：CLI "no match peer [...]"。
 * 整体销毁 peer match-condition，不区分具体子参数。 */
DEFUN_YANG (no_match_peer,
	    no_match_peer_cmd,
	    "no match peer [<local|A.B.C.D|X:X::X:X|WORD>]",
	    NO_STR
	    MATCH_STR
	    "Match peer address\n"
	    "Static or Redistributed routes\n"
	    "IP address of peer\n"
	    "IPv6 address of peer\n"
	    "Interface name of peer\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:peer']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_src_peer：CLI "match src-peer <A.B.C.D|X:X::X:X|WORD>"。
 * 与 match_peer 类似，但作用于 src-peer match-condition，用于匹配源对端。 */
DEFPY_YANG (match_src_peer,
       match_src_peer_cmd,
       "match src-peer <A.B.C.D$addrv4|X:X::X:X$addrv6|WORD$intf>",
       MATCH_STR
       "Match source peer address\n"
       "IP address of peer\n"
       "IPv6 address of peer\n"
       "Interface name of peer or peer group name\n")
{
	const char *xpath = "./match-condition[condition='frr-bgp-route-map:src-peer']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:src-peer-ipv4-address", xpath);
	nb_cli_enqueue_change(vty, xpath_value, addrv4_str ? NB_OP_MODIFY : NB_OP_DESTROY,
			      addrv4_str);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:src-peer-ipv6-address", xpath);
	nb_cli_enqueue_change(vty, xpath_value, addrv6_str ? NB_OP_MODIFY : NB_OP_DESTROY,
			      addrv6_str);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:src-peer-interface", xpath);
	nb_cli_enqueue_change(vty, xpath_value, intf ? NB_OP_MODIFY : NB_OP_DESTROY, intf);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_match_src_peer：上述命令的 no 形式，销毁 src-peer match-condition。 */
DEFUN_YANG (no_match_src_peer,
	    no_match_src_peer_cmd,
	    "no match src-peer [<A.B.C.D|X:X::X:X|WORD>]",
	    NO_STR
	    MATCH_STR
	    "Match peer address\n"
	    "IP address of peer\n"
	    "IPv6 address of peer\n"
	    "Interface name of peer\n")
{
	const char *xpath = "./match-condition[condition='frr-bgp-route-map:src-peer']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

#ifdef HAVE_SCRIPTING
/* 【中文补充】match_script：CLI "[no] match script WORD"(仅编译脚本支持时可用)。
 * 依据首 token 是否为 "no" 决定创建还是销毁 match-script condition，并写入脚本名
 * (不含 .lua 后缀)。运行时由 Lua 脚本决定是否命中；只读匹配。 */
DEFUN_YANG (match_script,
	    match_script_cmd,
	    "[no] match script WORD",
	    NO_STR
	    MATCH_STR
	    "Execute script to determine match\n"
	    "The script name to run, without .lua; e.g. 'myroutemap' to run myroutemap.lua\n")
{
	bool no = strmatch(argv[0]->text, "no");
	int i = 0;
	argv_find(argv, argc, "WORD", &i);
	const char *script = argv[i]->arg;
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:match-script']";
	char xpath_value[XPATH_MAXLEN];

	if (no) {
		nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
		snprintf(xpath_value, sizeof(xpath_value),
			 "%s/rmap-match-condition/frr-bgp-route-map:script",
			 xpath);
		nb_cli_enqueue_change(vty, xpath_value, NB_OP_DESTROY,
				      script);

		return nb_cli_apply_changes(vty, NULL);
	}

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
			"%s/rmap-match-condition/frr-bgp-route-map:script",
			xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			script);

	return nb_cli_apply_changes(vty, NULL);
}
#endif /* HAVE_SCRIPTING */

/* 【中文补充】match_probability：CLI "match probability (0-100)"。
 * 创建 probability match-condition，写入百分比值，用于按比例随机匹配路由。 */
/* match probability */
DEFUN_YANG (match_probability,
	    match_probability_cmd,
	    "match probability (0-100)",
	    MATCH_STR
	    "Match portion of routes defined by percentage value\n"
	    "Percentage of routes\n")
{
	int idx_number = 2;

	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:probability']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:probability",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			      argv[idx_number]->arg);

	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】no_match_probability：CLI "no match probability [(0-100)]"。
 * 先销毁整个 probability condition；若命令行还带了具体百分比，则再销毁该值节点。 */
DEFUN_YANG (no_match_probability,
	    no_match_probability_cmd,
	    "no match probability [(0-100)]",
	    NO_STR
	    MATCH_STR
	    "Match portion of routes defined by percentage value\n"
	    "Percentage of routes\n")
{
	int idx_number = 3;
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:probability']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	if (argc <= idx_number)
		return nb_cli_apply_changes(vty, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:probability",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_DESTROY,
			      argv[idx_number]->arg);

	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】match_ip_route_source：CLI "match ip route-source ACCESSLIST4_NAME"。
 * 创建 ip-route-source match-condition，写入 ACL 名，匹配路由的发布源地址。 */
DEFPY_YANG (match_ip_route_source,
       match_ip_route_source_cmd,
       "match ip route-source ACCESSLIST4_NAME",
       MATCH_STR
       IP_STR
       "Match advertising source address of route\n"
       "IP Access-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:ip-route-source']";
	char xpath_value[XPATH_MAXLEN + 32];
	int idx_acl = 3;

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
			"%s/rmap-match-condition/frr-bgp-route-map:list-name",
			xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			      argv[idx_acl]->arg);

	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】no_match_ip_route_source：上述命令的 no 形式，销毁对应 match-condition。 */
DEFUN_YANG (no_match_ip_route_source,
	    no_match_ip_route_source_cmd,
	    "no match ip route-source [ACCESSLIST4_NAME]",
	    NO_STR
	    MATCH_STR
	    IP_STR
	    "Match advertising source address of route\n"
	    "IP Access-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:ip-route-source']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_ip_route_source_prefix_list：CLI "match ip route-source prefix-list"。
 * 创建 ip-route-source-prefix-list condition，写入前缀列表名。 */
DEFUN_YANG (match_ip_route_source_prefix_list,
	    match_ip_route_source_prefix_list_cmd,
	    "match ip route-source prefix-list PREFIXLIST_NAME",
	    MATCH_STR
	    IP_STR
	    "Match advertising source address of route\n"
	    "Match entries of prefix-lists\n"
	    "IP prefix-list name\n")
{
	int idx_word = 4;
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:ip-route-source-prefix-list']";
	char xpath_value[XPATH_MAXLEN + 32];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:list-name", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			      argv[idx_word]->arg);

	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】no_match_ip_route_source_prefix_list：上述命令的 no 形式，销毁对应节点。 */
DEFUN_YANG (no_match_ip_route_source_prefix_list,
	    no_match_ip_route_source_prefix_list_cmd,
	    "no match ip route-source prefix-list [PREFIXLIST_NAME]",
	    NO_STR
	    MATCH_STR
	    IP_STR
	    "Match advertising source address of route\n"
	    "Match entries of prefix-lists\n"
	    "IP prefix-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:ip-route-source-prefix-list']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_local_pref：CLI "match local-preference (0-4294967295)"。
 * 创建 match-local-preference condition，写入 local-preference 值。
 * 依赖路径本地优先级属性匹配；只读，不修改路径。 */
DEFUN_YANG (match_local_pref,
	    match_local_pref_cmd,
	    "match local-preference (0-4294967295)",
	    MATCH_STR
	    "Match local-preference of route\n"
	    "Metric value\n")
{
	int idx_number = 2;

	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:match-local-preference']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:local-preference",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			      argv[idx_number]->arg);

	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】no_match_local_pref：CLI "no match local-preference [(...)]"。
 * 先销毁整个 condition；若带具体数值则再销毁该值节点。 */
DEFUN_YANG (no_match_local_pref,
	    no_match_local_pref_cmd,
	    "no match local-preference [(0-4294967295)]",
	    NO_STR
	    MATCH_STR
	    "Match local preference of route\n"
	    "Local preference value\n")
{
	int idx_localpref = 3;
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:match-local-preference']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	if (argc <= idx_localpref)
		return nb_cli_apply_changes(vty, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:local-preference",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_DESTROY,
			      argv[idx_localpref]->arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_alias：CLI "match alias ALIAS_NAME"。
 * 先用 bgp_ca_alias_lookup 校验别名存在(不存在报错并返回失败)，再创建
 * match-alias condition 并写入别名。依赖 BGP 团体别名机制匹配；只读。 */
DEFUN_YANG(match_alias, match_alias_cmd, "match alias ALIAS_NAME",
	   MATCH_STR
	   "Match BGP community alias name\n"
	   "BGP community alias name\n")
{
	const char *alias = argv[2]->arg;
	struct community_alias ca1;
	struct community_alias *lookup_alias;

	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:match-alias']";
	char xpath_value[XPATH_MAXLEN];

	memset(&ca1, 0, sizeof(ca1));
	strlcpy(ca1.alias, alias, sizeof(ca1.alias));
	lookup_alias = bgp_ca_alias_lookup(&ca1);
	if (!lookup_alias) {
		vty_out(vty, "%% BGP alias name '%s' does not exist\n", alias);
		return CMD_WARNING_CONFIG_FAILED;
	}

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:alias", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, alias);

	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】no_match_alias：CLI "no match alias [ALIAS_NAME]"。
 * 先销毁整个 condition；若带别名则再销毁该值节点。 */
DEFUN_YANG(no_match_alias, no_match_alias_cmd, "no match alias [ALIAS_NAME]",
	   NO_STR MATCH_STR
	   "Match BGP community alias name\n"
	   "BGP community alias name\n")
{
	int idx_alias = 3;
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:match-alias']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	if (argc <= idx_alias)
		return nb_cli_apply_changes(vty, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:alias", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_DESTROY,
			      argv[idx_alias]->arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_community：CLI "match community <NUM|NAME> [exact-match|any]"。
 * 创建 match-community condition，写入团体列表名，并按 exact / any 参数设置
 * comm-list-name-exact-match 与 comm-list-name-any 两个布尔子节点。
 * 依赖标准与扩展团体列表匹配；只读。 */
DEFPY_YANG(
	match_community, match_community_cmd,
	"match community <(1-99)|(100-500)|COMMUNITY_LIST_NAME> [<exact-match$exact|any$any>]",
	MATCH_STR "Match BGP community list\n"
		  "Community-list number (standard)\n"
		  "Community-list number (expanded)\n"
		  "Community-list name\n"
		  "Do exact matching of communities\n"
		  "Do matching of any community\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:match-community']";
	char xpath_value[XPATH_MAXLEN];
	char xpath_match[XPATH_MAXLEN];
	int idx_comm_list = 2;

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(
		xpath_value, sizeof(xpath_value),
		"%s/rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name",
		xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, argv[idx_comm_list]->arg);

	snprintf(xpath_match, sizeof(xpath_match),
		 "%s/rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-exact-match",
		 xpath);
	if (exact)
		nb_cli_enqueue_change(vty, xpath_match, NB_OP_MODIFY,
				"true");
	else
		nb_cli_enqueue_change(vty, xpath_match, NB_OP_MODIFY, "false");

	snprintf(xpath_match, sizeof(xpath_match),
		 "%s/rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-any",
		 xpath);
	if (any)
		nb_cli_enqueue_change(vty, xpath_match, NB_OP_MODIFY, "true");
	else
		nb_cli_enqueue_change(vty, xpath_match, NB_OP_MODIFY, "false");

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_community_limit：CLI "[no] match community-limit [(0-65535)]"。
 * 依据 no 标志决定 CREATE / DESTROY 与 MODIFY / DESTROY，写入 community-limit 值。
 * 用于限制所匹配团体的数量上限。 */
DEFPY_YANG(
	match_community_limit, match_community_limit_cmd,
	"[no$no] match community-limit ![(0-65535)$limit]",
	NO_STR
	MATCH_STR
	"Match BGP community limit\n"
	"Community limit number\n")
{
	const char *xpath = "./match-condition[condition='frr-bgp-route-map:match-community-limit']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, no ? NB_OP_DESTROY : NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:community-limit", xpath);

	nb_cli_enqueue_change(vty, xpath_value, no ? NB_OP_DESTROY : NB_OP_MODIFY, limit_str);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_match_community：CLI "no match community [...]"。
 * 整体销毁 match-community condition。 */
DEFUN_YANG(
	no_match_community, no_match_community_cmd,
	"no match community [<(1-99)|(100-500)|COMMUNITY_LIST_NAME> [<exact-match$exact|any$any>]]",
	NO_STR MATCH_STR "Match BGP community list\n"
			 "Community-list number (standard)\n"
			 "Community-list number (expanded)\n"
			 "Community-list name\n"
			 "Do exact matching of communities\n"
			 "Do matching of any community\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:match-community']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_lcommunity：CLI "match large-community <NUM|NAME> [exact-match|any]"。
 * 创建 match-large-community condition，写入大团体列表名，并设置 exact / any 子节点。
 * 与 match_community 逻辑对称，只是对象为 large community；只读。 */
DEFPY_YANG(
	match_lcommunity, match_lcommunity_cmd,
	"match large-community <(1-99)|(100-500)|LCOMMUNITY_LIST_NAME> [<exact-match$exact|any$any>]",
	MATCH_STR "Match BGP large community list\n"
		  "Large Community-list number (standard)\n"
		  "Large Community-list number (expanded)\n"
		  "Large Community-list name\n"
		  "Do exact matching of communities\n"
		  "Do matching of any community\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:match-large-community']";
	char xpath_value[XPATH_MAXLEN];
	char xpath_match[XPATH_MAXLEN];
	int idx_lcomm_list = 2;

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(
		xpath_value, sizeof(xpath_value),
		"%s/rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name",
		xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, argv[idx_lcomm_list]->arg);

	snprintf(xpath_match, sizeof(xpath_match),
		 "%s/rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-exact-match",
		 xpath);
	if (exact)
		nb_cli_enqueue_change(vty, xpath_match, NB_OP_MODIFY,
				"true");
	else
		nb_cli_enqueue_change(vty, xpath_match, NB_OP_MODIFY, "false");

	snprintf(xpath_match, sizeof(xpath_match),
		 "%s/rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-any",
		 xpath);
	if (any)
		nb_cli_enqueue_change(vty, xpath_match, NB_OP_MODIFY, "true");
	else
		nb_cli_enqueue_change(vty, xpath_match, NB_OP_MODIFY, "false");

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_match_lcommunity：CLI "no match large-community [...]"。
 * 整体销毁 match-large-community condition。 */
DEFUN_YANG(
	no_match_lcommunity, no_match_lcommunity_cmd,
	"no match large-community [<(1-99)|(100-500)|LCOMMUNITY_LIST_NAME> [<exact-match|any>]]",
	NO_STR MATCH_STR "Match BGP large community list\n"
			 "Large Community-list number (standard)\n"
			 "Large Community-list number (expanded)\n"
			 "Large Community-list name\n"
			 "Do exact matching of communities\n"
			 "Do matching of any community\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:match-large-community']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_ecommunity：CLI "match extcommunity <NUM|NAME> [exact-match|any]"。
 * 创建 match-extcommunity condition，写入扩展团体列表名，并设置 exact / any 子节点。
 * 依赖扩展团体列表匹配(VPN 场景常用)；只读。 */
DEFPY_YANG (match_ecommunity,
	    match_ecommunity_cmd,
            "match extcommunity <(1-99)|(100-500)|EXTCOMMUNITY_LIST_NAME> [<exact-match$exact|any$any>]",
	    MATCH_STR
	    "Match BGP/VPN extended community list\n"
	    "Extended community-list number (standard)\n"
	    "Extended community-list number (expanded)\n"
	    "Extended community-list name\n"
	    "Do exact matching of communities\n"
	    "Do matching of any community\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:match-extcommunity']";
	char xpath_value[XPATH_MAXLEN];
	char xpath_match[XPATH_MAXLEN];
	int idx_comm_list = 2;

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(
		xpath_value, sizeof(xpath_value),
		"%s/rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name",
		xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, argv[idx_comm_list]->arg);

	snprintf(xpath_match, sizeof(xpath_match),
		 "%s/rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-exact-match",
		 xpath);
	if (exact)
		nb_cli_enqueue_change(vty, xpath_match, NB_OP_MODIFY, "true");
	else
		nb_cli_enqueue_change(vty, xpath_match, NB_OP_MODIFY, "false");

	snprintf(xpath_match, sizeof(xpath_match),
		 "%s/rmap-match-condition/frr-bgp-route-map:comm-list/comm-list-name-any", xpath);
	if (any)
		nb_cli_enqueue_change(vty, xpath_match, NB_OP_MODIFY, "true");
	else
		nb_cli_enqueue_change(vty, xpath_match, NB_OP_MODIFY, "false");

	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】match_extcommunity_limit：CLI "[no] match extcommunity-limit [(0-65535)]"。
 * 依据 no 标志决定 CREATE / DESTROY 与 MODIFY / DESTROY，写入 extcommunity-limit 值。
 * 用于限制所匹配扩展团体的数量上限。 */
DEFPY_YANG(
	match_extcommunity_limit, match_extcommunity_limit_cmd,
	"[no$no] match extcommunity-limit ![(0-65535)$limit]",
	NO_STR
	MATCH_STR
	"Match BGP extended community limit\n"
	"Extended community limit number\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:match-extcommunity-limit']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, no ? NB_OP_DESTROY : NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:extcommunity-limit", xpath);

	nb_cli_enqueue_change(vty, xpath_value, no ? NB_OP_DESTROY : NB_OP_MODIFY, limit_str);
	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】no_match_ecommunity：CLI "no match extcommunity [...]"。
 * 整体销毁 match-extcommunity condition。 */
DEFUN_YANG (no_match_ecommunity,
	    no_match_ecommunity_cmd,
	    "no match extcommunity [<(1-99)|(100-500)|EXTCOMMUNITY_LIST_NAME> [<exact-match$exact|any$any>]]",
	    NO_STR
	    MATCH_STR
	    "Match BGP/VPN extended community list\n"
	    "Extended community-list number (standard)\n"
	    "Extended community-list number (expanded)\n"
	    "Extended community-list name\n"
	    "Do exact matching of communities\n"
	    "Do matching of any community\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:match-extcommunity']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】set_ecommunity_delete：CLI "set extended-comm-list <LIST> delete"。
 * 创建 extended-comm-list-delete 的 set-action，写入待删除的扩展团体列表名。
 * 语义是从路由的扩展团体里删除命中该列表的项；修改属性，需重新宣告。 */
DEFPY_YANG (set_ecommunity_delete,
	    set_ecommunity_delete_cmd,
            "set extended-comm-list " EXTCOMM_LIST_CMD_STR " delete",
	    SET_STR
	    "set BGP extended community list (for deletion)\n"
	    EXTCOMM_STD_LIST_NUM_STR
	    EXTCOMM_EXP_LIST_NUM_STR
	    EXTCOMM_LIST_NAME_STR
            "Delete matching extended communities\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:extended-comm-list-delete']";
	char xpath_value[XPATH_MAXLEN];
	int idx_comm_list = 2;

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:comm-list-name",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			argv[idx_comm_list]->arg);
	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】no_set_ecommunity_delete：上述命令的 no 形式，销毁对应 set-action。 */
DEFPY_YANG (no_set_ecommunity_delete,
	    no_set_ecommunity_delete_cmd,
            "no set extended-comm-list [" EXTCOMM_LIST_CMD_STR "] delete",
	    NO_STR
	    SET_STR
	    "set BGP extended community list (for deletion)\n"
	    EXTCOMM_STD_LIST_NUM_STR
	    EXTCOMM_EXP_LIST_NUM_STR
	    EXTCOMM_LIST_NAME_STR
            "Delete matching extended communities\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:extended-comm-list-delete']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】match_aspath：CLI "match as-path AS_PATH_FILTER_NAME"。
 * 创建 as-path-list condition，写入 AS 路径访问列表名。依赖 AS 路径过滤匹配；只读。 */
DEFUN_YANG (match_aspath,
	    match_aspath_cmd,
	    "match as-path AS_PATH_FILTER_NAME",
	    MATCH_STR
	    "Match BGP AS path list\n"
	    "AS path access-list name\n")
{
	int idx_word = 2;

	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:as-path-list']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:list-name", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			      argv[idx_word]->arg);

	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】no_match_aspath：CLI "no match as-path [...]"。销毁 as-path-list condition。 */
DEFUN_YANG (no_match_aspath,
	    no_match_aspath_cmd,
	    "no match as-path [AS_PATH_FILTER_NAME]",
	    NO_STR
	    MATCH_STR
	    "Match BGP AS path list\n"
	    "AS path access-list name\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:as-path-list']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】match_aspath_count：CLI "[no] match as-path-count [(0-1028)]"。
 * 依据 no 标志决定 CREATE / DESTROY 与 MODIFY / DESTROY，写入 as-path-count 值。
 * 按 AS 路径长度匹配。 */
DEFPY_YANG(
	match_aspath_count, match_aspath_count_cmd,
	"[no$no] match as-path-count ![(0-1028)$count]",
	NO_STR
	MATCH_STR
	"Match BGP AS path count\n"
	"AS path count number\n")
{
	const char *xpath = "./match-condition[condition='frr-bgp-route-map:match-as-path-count']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, no ? NB_OP_DESTROY : NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:as-path-count", xpath);

	nb_cli_enqueue_change(vty, xpath_value, no ? NB_OP_DESTROY : NB_OP_MODIFY, count_str);
	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】match_origin：CLI "match origin <egp|igp|incomplete>"。
 * 把参数规整为 igp / egp / incomplete，非法值报错并返回失败；
 * 然后创建 match-origin condition 并写入。依赖路径 ORIGIN 属性匹配；只读。 */
DEFUN_YANG (match_origin,
	    match_origin_cmd,
	    "match origin <egp|igp|incomplete>",
	    MATCH_STR
	    "BGP origin code\n"
	    "remote EGP\n"
	    "local IGP\n"
	     "unknown heritage\n")
{
	int idx_origin = 2;
	const char *origin_type;
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:match-origin']";
	char xpath_value[XPATH_MAXLEN];

	if (strncmp(argv[idx_origin]->arg, "igp", 2) == 0)
		origin_type = "igp";
	else if (strncmp(argv[idx_origin]->arg, "egp", 1) == 0)
		origin_type = "egp";
	else if (strncmp(argv[idx_origin]->arg, "incomplete", 2) == 0)
		origin_type = "incomplete";
	else {
		vty_out(vty, "%% Invalid match origin type\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:origin", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, origin_type);

	return nb_cli_apply_changes(vty, NULL);
}


/* 【中文补充】no_match_origin：CLI "no match origin [...]"。销毁 match-origin condition。 */
DEFUN_YANG (no_match_origin,
	    no_match_origin_cmd,
	    "no match origin [<egp|igp|incomplete>]",
	    NO_STR
	    MATCH_STR
	    "BGP origin code\n"
	    "remote EGP\n"
	    "local IGP\n"
	    "unknown heritage\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:match-origin']";
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_table_id：CLI "set table (1-4294967295)"。
 * 创建 table set-action，写入内核路由表 id，把路由导出到非 main 表。 */
DEFUN_YANG (set_table_id,
	    set_table_id_cmd,
	    "set table (1-4294967295)",
	    SET_STR
	    "export route to non-main kernel table\n"
	    "Kernel routing table id\n")
{
	int idx_number = 2;
	const char *xpath = "./set-action[action='frr-bgp-route-map:table']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:table", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			      argv[idx_number]->arg);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_table_id：上述命令的 no 形式，销毁 table set-action。 */
DEFUN_YANG (no_set_table_id,
	    no_set_table_id_cmd,
	    "no set table [(1-4294967295)]",
	    NO_STR
	    SET_STR
	    "export route to non-main kernel table\n"
	    "Kernel routing table id\n")
{
	const char *xpath = "./set-action[action='frr-bgp-route-map:table']";
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_ip_nexthop_peer：CLI "[no] set ip next-hop peer-address"。
 * 创建 / 销毁 set-ipv4-nexthop set-action；创建时把 ipv4-nexthop 置为 "peer-address"
 * (仅 BGP 使用)。影响下一跳，需重新宣告。 */
DEFUN_YANG (set_ip_nexthop_peer,
	    set_ip_nexthop_peer_cmd,
	    "[no] set ip next-hop peer-address",
	    NO_STR
	    SET_STR
	    IP_STR
	    "Next hop address\n"
	    "Use peer address (for BGP only)\n")
{
	char xpath_value[XPATH_MAXLEN];
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-ipv4-nexthop']";

	if (strmatch(argv[0]->text, "no"))
		nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	else {
		nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
		snprintf(xpath_value, sizeof(xpath_value),
			 "%s/rmap-set-action/frr-bgp-route-map:ipv4-nexthop",
			 xpath);
		nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
				      "peer-address");
	}
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_ip_nexthop_unchanged：CLI "[no] set ip next-hop unchanged"。
 * 形式同 peer-address 版，但把 ipv4-nexthop 置为 "unchanged"，表示不改动下一跳。 */
DEFUN_YANG (set_ip_nexthop_unchanged,
	    set_ip_nexthop_unchanged_cmd,
	    "[no] set ip next-hop unchanged",
	    NO_STR
	    SET_STR
	    IP_STR
	    "Next hop address\n"
	    "Don't modify existing Next hop address\n")
{
	char xpath_value[XPATH_MAXLEN];
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-ipv4-nexthop']";

	if (strmatch(argv[0]->text, "no"))
		nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	else {
		nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
		snprintf(xpath_value, sizeof(xpath_value),
			 "%s/rmap-set-action/frr-bgp-route-map:ipv4-nexthop",
			 xpath);
		nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
				      "unchanged");
	}
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_distance：CLI "set distance (1-255)"。
 * 创建 distance set-action，写入管理距离(AD)，影响该前缀的 AD。 */
DEFUN_YANG (set_distance,
	    set_distance_cmd,
	    "set distance (1-255)",
	    SET_STR
	    "BGP Administrative Distance to use\n"
	    "Distance value\n")
{
	int idx_number = 2;
	const char *xpath = "./set-action[action='frr-bgp-route-map:distance']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:distance", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			      argv[idx_number]->arg);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_distance：上述命令的 no 形式，销毁 distance set-action。 */
DEFUN_YANG (no_set_distance,
	    no_set_distance_cmd,
	    "no set distance [(1-255)]",
	    NO_STR SET_STR
	    "BGP Administrative Distance to use\n"
	    "Distance value\n")
{
	const char *xpath = "./set-action[action='frr-bgp-route-map:distance']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_l3vpn_nexthop_encapsulation：CLI "[no] set l3vpn next-hop encapsulation <gre|gretap>"。
 * no 时销毁 action 节点；否则创建并写入封装类型(gre / gretap)。仅作用于 BGP L3VPN 数据面。 */
DEFPY_YANG(set_l3vpn_nexthop_encapsulation, set_l3vpn_nexthop_encapsulation_cmd,
	   "[no] set l3vpn next-hop encapsulation <gre|gretap>$ziftype",
	   NO_STR SET_STR
	   "L3VPN operations\n"
	   "Next hop Information\n"
	   "Encapsulation options (for BGP only)\n"
	   "Accept L3VPN traffic over GRE encapsulation\n"
	   "Accept L3VPN traffic over GRETAP encapsulation\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-l3vpn-nexthop-encapsulation']";
	const char *xpath_value =
		"./set-action[action='frr-bgp-route-map:set-l3vpn-nexthop-encapsulation']/rmap-set-action/frr-bgp-route-map:l3vpn-nexthop-encapsulation";
	enum nb_operation operation;

	if (no)
		operation = NB_OP_DESTROY;
	else
		operation = NB_OP_CREATE;

	nb_cli_enqueue_change(vty, xpath, operation, NULL);
	if (operation == NB_OP_DESTROY)
		return nb_cli_apply_changes(vty, NULL);

	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, ziftype);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_local_pref：CLI "set local-preference WORD"。
 * 创建 set-local-preference set-action，写入 local-pref 值。影响 iBGP 选路，需重新宣告。 */
DEFUN_YANG (set_local_pref,
	    set_local_pref_cmd,
	    "set local-preference WORD",
	    SET_STR
	    "BGP local preference path attribute\n"
	    "Preference value (0-4294967295)\n")
{
	int idx_number = 2;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-local-preference']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:local-pref", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			      argv[idx_number]->arg);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_local_pref：上述命令的 no 形式，销毁 set-local-preference action。 */
DEFUN_YANG (no_set_local_pref,
	    no_set_local_pref_cmd,
	    "no set local-preference [WORD]",
	    NO_STR
	    SET_STR
	    "BGP local preference path attribute\n"
	    "Preference value (0-4294967295)\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-local-preference']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_weight：CLI "set weight (0-4294967295)"。
 * 创建 weight set-action，写入 weight 值。weight 为本地私有属性，影响本机选路但不对外宣告。 */
DEFUN_YANG (set_weight,
	    set_weight_cmd,
	    "set weight (0-4294967295)",
	    SET_STR
	    "BGP weight for routing table\n"
	    "Weight value\n")
{
	int idx_number = 2;
	const char *xpath = "./set-action[action='frr-bgp-route-map:weight']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:weight", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			      argv[idx_number]->arg);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_weight：上述命令的 no 形式，销毁 weight set-action。 */
DEFUN_YANG (no_set_weight,
	    no_set_weight_cmd,
	    "no set weight [(0-4294967295)]",
	    NO_STR
	    SET_STR
	    "BGP weight for routing table\n"
	    "Weight value\n")
{
	const char *xpath = "./set-action[action='frr-bgp-route-map:weight']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_label_index：CLI "set label-index (0-1048560)"。
 * 创建 label-index set-action，写入标签索引(用于为前缀分配合成标签)。 */
DEFUN_YANG (set_label_index,
	    set_label_index_cmd,
	    "set label-index (0-1048560)",
	    SET_STR
	    "Label index to associate with the prefix\n"
	    "Label index value\n")
{
	int idx_number = 2;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:label-index']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:label-index", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			      argv[idx_number]->arg);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_label_index：上述 set label-index 的 no 形式，销毁 label-index action。 */
DEFUN_YANG (no_set_label_index,
	    no_set_label_index_cmd,
	    "no set label-index [(0-1048560)]",
	    NO_STR
	    SET_STR
	    "Label index to associate with the prefix\n"
	    "Label index value\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:label-index']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_aspath_prepend_asn：CLI "set as-path prepend ASNUM..."。
 * 先把整串 AS 号拼成字符串并用 route_aspath_compile 校验合法，非法则报错返回；
 * 再创建 as-path-prepend action 并写入 prepend-as-path 值。改变 AS_PATH，需重新宣告。 */
DEFUN_YANG (set_aspath_prepend_asn,
	    set_aspath_prepend_asn_cmd,
	    "set as-path prepend ASNUM...",
	    SET_STR
	    "Transform BGP AS_PATH attribute\n"
	    "Prepend to the as-path\n"
	    AS_STR)
{
	int idx_asn = 3;
	int ret;
	char *str;
	struct aspath *aspath;

	str = argv_concat(argv, argc, idx_asn);

	const char *xpath =
		"./set-action[action='frr-bgp-route-map:as-path-prepend']";
	char xpath_value[XPATH_MAXLEN];

	aspath = route_aspath_compile(str);
	if (!aspath) {
		vty_out(vty, "%% Invalid AS path value %s\n", str);
		return CMD_WARNING_CONFIG_FAILED;
	}
	route_aspath_free(aspath);
	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:prepend-as-path", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, str);
	ret = nb_cli_apply_changes(vty, NULL);
	XFREE(MTYPE_TMP, str);
	return ret;
}

/* 【中文补充】set_aspath_prepend_lastas：CLI "set as-path prepend last-as (1-10)"。
 * 创建 as-path-prepend action 并写入 last-as 次数，表示把路径中最后一个 AS 号重复插入。 */
DEFUN_YANG (set_aspath_prepend_lastas,
	    set_aspath_prepend_lastas_cmd,
	    "set as-path prepend last-as (1-10)",
	    SET_STR
	    "Transform BGP AS_PATH attribute\n"
	    "Prepend to the as-path\n"
	    "Use the last AS-number in the as-path\n"
	    "Number of times to insert\n")
{
	int idx_num = 4;

	const char *xpath =
		"./set-action[action='frr-bgp-route-map:as-path-prepend']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:last-as", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			      argv[idx_num]->arg);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_aspath_replace_asn：CLI "set as-path replace <any|ASNUM> [ASNUM]"。
 * 校验 replace 与可选 configured_asn 的合法性(asn_str2asn)，非法报错；
 * 把两者拼成 replace_value 写入 as-path-replace 的 replace-as-path 节点。改变 AS_PATH，需重新宣告。 */
DEFPY_YANG(set_aspath_replace_asn, set_aspath_replace_asn_cmd,
	   "set as-path replace <any|ASNUM>$replace [<ASNUM>$configured_asn]",
	   SET_STR
	   "Transform BGP AS_PATH attribute\n"
	   "Replace AS number to local or configured AS number\n"
	   "Replace any AS number to local or configured AS number\n"
	   "Replace a specific AS number to local or configured AS number\n"
	   "Define the configured AS number\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:as-path-replace']";
	char xpath_value[XPATH_MAXLEN];
	as_t as_value, as_configured_value;
	char replace_value[ASN_STRING_MAX_SIZE * 2];

	if (!strmatch(replace, "any") && !asn_str2asn(replace, &as_value)) {
		vty_out(vty, "%% Invalid AS value %s\n", replace);
		return CMD_WARNING_CONFIG_FAILED;
	}
	if (configured_asn_str &&
	    !asn_str2asn(configured_asn_str, &as_configured_value)) {
		vty_out(vty, "%% Invalid AS configured value %s\n",
			configured_asn_str);
		return CMD_WARNING_CONFIG_FAILED;
	}
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:replace-as-path", xpath);
	snprintf(replace_value, sizeof(replace_value), "%s%s%s", replace,
		 configured_asn_str ? " " : "",
		 configured_asn_str ? configured_asn_str : "");
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, replace_value);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_aspath_replace_asn：set as-path replace 的 no 形式，销毁 as-path-replace action。 */
DEFPY_YANG(no_set_aspath_replace_asn, no_set_aspath_replace_asn_cmd,
	   "no set as-path replace [<any|ASNUM>] [<ASNUM>$configured_asn]",
	   NO_STR SET_STR
	   "Transform BGP AS_PATH attribute\n"
	   "Replace AS number to local or configured AS number\n"
	   "Replace any AS number to local or configured AS number\n"
	   "Replace a specific AS number to local or configured AS number\n"
	   "Define the configured AS number\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:as-path-replace']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_aspath_replace_access_list：CLI "set as-path replace as-path-access-list NAME [ASNUM]"。
 * 校验可选 configured_asn，拼接参数，创建 as-path-replace action 并写入 replace-as-path。
 * 按 AS 路径访问列表命中的 AS 号做替换。改变 AS_PATH，需重新宣告。 */
DEFPY_YANG(
	set_aspath_replace_access_list, set_aspath_replace_access_list_cmd,
	"set as-path replace as-path-access-list AS_PATH_FILTER_NAME$aspath_filter_name [<ASNUM>$configured_asn]",
	SET_STR
	"Transform BGP AS-path attribute\n"
	"Replace AS number to local or configured AS number\n"
	"Specify an as path access list name\n"
	"AS path access list name\n"
	"Define the configured AS number\n")
{
	char *str;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:as-path-replace']";
	char xpath_value[XPATH_MAXLEN];
	as_t as_configured_value;
	char replace_value[ASN_STRING_MAX_SIZE * 2];
	int ret;

	if (configured_asn_str &&
	    !asn_str2asn(configured_asn_str, &as_configured_value)) {
		vty_out(vty, "%% Invalid AS configured value %s\n",
			configured_asn_str);
		return CMD_WARNING_CONFIG_FAILED;
	}

	str = argv_concat(argv, argc, 3);

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(replace_value, sizeof(replace_value), "%s %s", aspath_filter_name, str);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:replace-as-path", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, str);

	ret = nb_cli_apply_changes(vty, NULL);
	XFREE(MTYPE_TMP, str);
	return ret;
}

/* 【中文补充】no_set_aspath_replace_access_list：上述命令的 no 形式，销毁 as-path-replace action。 */
DEFPY_YANG(
	no_set_aspath_replace_access_list, no_set_aspath_replace_access_list_cmd,
	"no set as-path replace as-path-access-list [AS_PATH_FILTER_NAME] [<ASNUM>$configured_asn]",
	NO_STR
	SET_STR
	"Transform BGP AS_PATH attribute\n"
	"Replace AS number to local or configured AS number\n"
	"Specify an as path access list name\n"
	"AS path access list name\n"
	"Define the configured AS number\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:as-path-replace']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_aspath_prepend：CLI "no set as-path prepend [last-as [(1-10)]]"。
 * 销毁 as-path-prepend action(对应 prepend last-as 形式的否定)。 */
DEFUN_YANG (no_set_aspath_prepend,
	    no_set_aspath_prepend_last_as_cmd,
	    "no set as-path prepend [last-as [(1-10)]]",
	    NO_STR
	    SET_STR
	    "Transform BGP AS_PATH attribute\n"
	    "Prepend to the as-path\n"
	    "Use the peers AS-number\n"
	    "Number of times to insert\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:as-path-prepend']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】ALIAS no_set_aspath_prepend(no_set_aspath_prepend_as_cmd)：把
 * "no set as-path prepend ASNUM..." 命令别名到 no_set_aspath_prepend 的实现。 */
ALIAS_YANG (no_set_aspath_prepend,
            no_set_aspath_prepend_as_cmd,
            "no set as-path prepend ASNUM...",
            NO_STR
            SET_STR
            "Transform BGP AS_PATH attribute\n"
            "Prepend to the as-path\n"
            AS_STR)

/* 【中文补充】set_aspath_exclude：CLI "set as-path exclude ASNUM..."。
 * 拼接 AS 号串并 route_aspath_compile 校验，非法报错；创建 as-path-exclude action，
 * 写入 exclude-as-path(要从 AS_PATH 中剔除的 AS 号)。改变 AS_PATH，需重新宣告。 */
DEFUN_YANG (set_aspath_exclude,
	    set_aspath_exclude_cmd,
	    "set as-path exclude ASNUM...",
	    SET_STR
	    "Transform BGP AS-path attribute\n"
	    "Exclude from the as-path\n"
	    AS_STR)
{
	int idx_asn = 3;
	int ret;
	char *str;
	struct aspath *aspath;

	str = argv_concat(argv, argc, idx_asn);

	const char *xpath =
		"./set-action[action='frr-bgp-route-map:as-path-exclude']";
	char xpath_value[XPATH_MAXLEN];

	aspath = route_aspath_compile(str);
	if (!aspath) {
		vty_out(vty, "%% Invalid AS path value %s\n", str);
		return CMD_WARNING_CONFIG_FAILED;
	}
	route_aspath_free(aspath);
	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:exclude-as-path", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, str);
	ret = nb_cli_apply_changes(vty, NULL);
	XFREE(MTYPE_TMP, str);
	return ret;
}

/* 【中文补充】set_aspath_exclude_all：CLI "[no] set as-path exclude all"。
 * no 时销毁 action；否则创建并写入 exclude-as-path = all，表示剔除全部 AS 号。 */
DEFPY_YANG(set_aspath_exclude_all, set_aspath_exclude_all_cmd,
	   "[no$no] set as-path exclude all$all",
	   NO_STR SET_STR
	   "Transform BGP AS-path attribute\n"
	   "Exclude from the as-path\n"
	   "Exclude all AS numbers from the as-path\n")
{
	int ret;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:as-path-exclude']";
	char xpath_value[XPATH_MAXLEN];

	if (no)
		nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	else {
		nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
		snprintf(xpath_value, sizeof(xpath_value),
			 "%s/rmap-set-action/frr-bgp-route-map:exclude-as-path",
			 xpath);
		nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, all);
	}
	ret = nb_cli_apply_changes(vty, NULL);

	return ret;
}

/* 【中文补充】no_set_aspath_exclude：CLI "no set as-path exclude ASNUM..."。
 * 销毁 as-path-exclude action。 */
DEFUN_YANG (no_set_aspath_exclude,
	    no_set_aspath_exclude_cmd,
	    "no set as-path exclude ASNUM...",
	    NO_STR
	    SET_STR
	    "Transform BGP AS_PATH attribute\n"
	    "Exclude from the as-path\n"
	    "AS number\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:as-path-exclude']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_aspath_exclude_access_list：CLI "set as-path exclude as-path-access-list NAME"。
 * 创建 as-path-exclude action 并写入 exclude-as-path(按 AS 路径访问列表指定待剔除的 AS)。 */
DEFPY_YANG(set_aspath_exclude_access_list, set_aspath_exclude_access_list_cmd,
	   "set as-path exclude as-path-access-list AS_PATH_FILTER_NAME",
	   SET_STR
	   "Transform BGP AS-path attribute\n"
	   "Exclude from the as-path\n"
	   "Specify an as path access list name\n"
	   "AS path access list name\n")
{
	char *str;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:as-path-exclude']";
	char xpath_value[XPATH_MAXLEN];
	int ret;

	str = argv_concat(argv, argc, 3);

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:exclude-as-path", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, str);

	ret = nb_cli_apply_changes(vty, NULL);
	XFREE(MTYPE_TMP, str);
	return ret;
}

/* 【中文补充】no_set_aspath_exclude_access_list：上述命令的 no 形式，销毁 as-path-exclude action。 */
DEFPY_YANG(no_set_aspath_exclude_access_list, no_set_aspath_exclude_access_list_cmd,
	   "no set as-path exclude as-path-access-list [AS_PATH_FILTER_NAME]",
	   NO_STR
	   SET_STR
	   "Transform BGP AS_PATH attribute\n"
	   "Exclude from the as-path\n"
	   "Specify an as path access list name\n"
	   "AS path access list name\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:as-path-exclude']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】ALIAS no_set_aspath_exclude(no_set_aspath_exclude_all_cmd)：把
 * "no set as-path exclude" 别名到 no_set_aspath_exclude 的实现。 */
ALIAS_YANG (no_set_aspath_exclude, no_set_aspath_exclude_all_cmd,
            "no set as-path exclude",
            NO_STR SET_STR
            "Transform BGP AS_PATH attribute\n"
            "Exclude from the as-path\n")

/* 【中文补充】set_community：CLI "set community AA:NN..."(含 additive / local-AS / no-advertise 等关键字)。
 * 把参数拼接成字符串并 community_str2com 校验，非法报错；再 community_str 归一化后写入
 * set-community 的 community-string；末尾带 additive 时附加 " additive"。
 * 修改团体属性，需重新宣告。 */
DEFUN_YANG (set_community,
	    set_community_cmd,
	    "set community AA:NN...",
	    SET_STR
	    "BGP community attribute\n"
	    COMMUNITY_VAL_STR)
{
	int idx_aa_nn = 2;
	int i;
	int first = 0;
	int additive = 0;
	struct buffer *b;
	struct community *com = NULL;
	char *str;
	char *argstr = NULL;
	int ret;

	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-community']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:community-string",
		 xpath);

	b = buffer_new(1024);

	for (i = idx_aa_nn; i < argc; i++) {
		if (strncmp(argv[i]->arg, "additive", strlen(argv[i]->arg))
		    == 0) {
			additive = 1;
			continue;
		}

		if (first)
			buffer_putc(b, ' ');
		else
			first = 1;

		if (strncmp(argv[i]->arg, "local-AS", strlen(argv[i]->arg))
		    == 0) {
			buffer_putstr(b, "local-AS");
			continue;
		}
		if (strncmp(argv[i]->arg, "no-a", strlen("no-a")) == 0
		    && strncmp(argv[i]->arg, "no-advertise",
			       strlen(argv[i]->arg))
			       == 0) {
			buffer_putstr(b, "no-advertise");
			continue;
		}
		if (strncmp(argv[i]->arg, "no-e", strlen("no-e")) == 0
		    && strncmp(argv[i]->arg, "no-export", strlen(argv[i]->arg))
			       == 0) {
			buffer_putstr(b, "no-export");
			continue;
		}
		if (strncmp(argv[i]->arg, "blackhole", strlen(argv[i]->arg))
		    == 0) {
			buffer_putstr(b, "blackhole");
			continue;
		}
		if (strncmp(argv[i]->arg, "graceful-shutdown",
			    strlen(argv[i]->arg))
		    == 0) {
			buffer_putstr(b, "graceful-shutdown");
			continue;
		}
		buffer_putstr(b, argv[i]->arg);
	}
	buffer_putc(b, '\0');

	/* Fetch result string then compile it to communities attribute.  */
	str = buffer_getstr(b);
	buffer_free(b);

	if (str)
		com = community_str2com(str);

	/* Can't compile user input into communities attribute.  */
	if (!com) {
		vty_out(vty, "%% Malformed communities attribute '%s'\n", str);
		XFREE(MTYPE_TMP, str);
		return CMD_WARNING_CONFIG_FAILED;
	}
	XFREE(MTYPE_TMP, str);

	/* Set communites attribute string.  */
	str = community_str(com, false, false);

	if (additive) {
		size_t argstr_sz = strlen(str) + strlen(" additive") + 1;
		argstr = XCALLOC(MTYPE_TMP, argstr_sz);
		strlcpy(argstr, str, argstr_sz);
		strlcat(argstr, " additive", argstr_sz);
		nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, argstr);
	} else
		nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, str);

	ret = nb_cli_apply_changes(vty, NULL);

	if (argstr)
		XFREE(MTYPE_TMP, argstr);
	community_free(&com);

	return ret;
}

/* 【中文补充】set_community_none：CLI "set community none"。
 * 创建 set-community action 并写入 community-none = "true"，表示清空(不携带)团体属性。 */
DEFUN_YANG (set_community_none,
	    set_community_none_cmd,
	    "set community none",
	    SET_STR
	    "BGP community attribute\n"
	    "No community attribute\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-community']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:community-none", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, "true");
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_community：CLI "no set community AA:NN..."。销毁 set-community action。 */
DEFUN_YANG (no_set_community,
	    no_set_community_cmd,
	    "no set community AA:NN...",
	    NO_STR
	    SET_STR
	    "BGP community attribute\n"
	    COMMUNITY_VAL_STR)
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-community']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】ALIAS no_set_community(no_set_community_short_cmd)：把 "no set community"
 * 别名到 no_set_community 的实现。 */
ALIAS_YANG (no_set_community,
            no_set_community_short_cmd,
            "no set community",
            NO_STR
            SET_STR
            "BGP community attribute\n")

/* 【中文补充】set_community_delete：CLI "set comm-list <LIST> delete"。
 * 创建 comm-list-delete set-action，写入团体列表名，从路由团体中删除命中项。
 * 修改属性，需重新宣告。 */
DEFPY_YANG (set_community_delete,
       set_community_delete_cmd,
       "set comm-list <(1-99)|(100-500)|COMMUNITY_LIST_NAME> delete",
       SET_STR
       "set BGP community list (for deletion)\n"
       "Community-list number (standard)\n"
       "Community-list number (expanded)\n"
       "Community-list name\n"
       "Delete matching communities\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:comm-list-delete']";
	char xpath_value[XPATH_MAXLEN];
	int idx_comm_list = 2;

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:comm-list-name",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			argv[idx_comm_list]->arg);

	return nb_cli_apply_changes(vty, NULL);

}

/* 【中文补充】no_set_community_delete：上述命令的 no 形式，销毁 comm-list-delete action。 */
DEFUN_YANG (no_set_community_delete,
	    no_set_community_delete_cmd,
	    "no set comm-list [<(1-99)|(100-500)|COMMUNITY_LIST_NAME> delete]",
	    NO_STR
	    SET_STR
	    "set BGP community list (for deletion)\n"
	    "Community-list number (standard)\n"
	    "Community-list number (expanded)\n"
	    "Community-list name\n"
	    "Delete matching communities\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:comm-list-delete']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_lcommunity：CLI "set large-community AA:BB:CC..."(可带 additive)。
 * 拼接参数，创建 set-large-community action，写入 large-community-string。
 * 修改大团体属性，需重新宣告。 */
DEFUN_YANG (set_lcommunity,
	    set_lcommunity_cmd,
	    "set large-community AA:BB:CC...",
	    SET_STR
	    "BGP large community attribute\n"
	    "Large Community number in aa:bb:cc format or additive\n")
{
	char *str;
	int ret;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-large-community']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:large-community-string",
		 xpath);
	str = argv_concat(argv, argc, 2);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, str);
	ret = nb_cli_apply_changes(vty, NULL);
	XFREE(MTYPE_TMP, str);
	return ret;
}

/* 【中文补充】set_lcommunity_none：CLI "set large-community none"。
 * 创建 set-large-community action 并写入 large-community-none = "true"，清空大团体。 */
DEFUN_YANG (set_lcommunity_none,
	    set_lcommunity_none_cmd,
	    "set large-community none",
	    SET_STR
	    "BGP large community attribute\n"
	    "No large community attribute\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-large-community']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:large-community-none",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, "true");
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_lcommunity：CLI "no set large-community none"。
 * 销毁 set-large-community action(对应 none 形式的否定)。 */
DEFUN_YANG (no_set_lcommunity,
	    no_set_lcommunity_cmd,
	    "no set large-community none",
	    NO_STR
	    SET_STR
	    "BGP large community attribute\n"
	    "No community attribute\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-large-community']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_lcommunity1：CLI "no set large-community AA:BB:CC..."。
 * 销毁 set-large-community action(对应具体大团体值的否定)。 */
DEFUN_YANG (no_set_lcommunity1,
	    no_set_lcommunity1_cmd,
	    "no set large-community AA:BB:CC...",
	    NO_STR
	    SET_STR
	    "BGP large community attribute\n"
	    "Large community in AA:BB:CC... format or additive\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-large-community']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】ALIAS no_set_lcommunity1(no_set_lcommunity1_short_cmd)：把
 * "no set large-community" 别名到 no_set_lcommunity1 的实现。 */
ALIAS_YANG (no_set_lcommunity1,
            no_set_lcommunity1_short_cmd,
            "no set large-community",
            NO_STR
            SET_STR
            "BGP large community attribute\n")

/* 【中文补充】set_lcommunity_delete：CLI "set large-comm-list <LIST> delete"。
 * 创建 large-comm-list-delete set-action 写入大团体列表名，删除命中项。修改属性，需重新宣告。 */
DEFPY_YANG (set_lcommunity_delete,
       set_lcommunity_delete_cmd,
       "set large-comm-list <(1-99)|(100-500)|LCOMMUNITY_LIST_NAME> delete",
       SET_STR
       "set BGP large community list (for deletion)\n"
       "Large Community-list number (standard)\n"
       "Large Communitly-list number (expanded)\n"
       "Large Community-list name\n"
       "Delete matching large communities\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:large-comm-list-delete']";
	char xpath_value[XPATH_MAXLEN];
	int idx_lcomm_list = 2;

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
			"%s/rmap-set-action/frr-bgp-route-map:comm-list-name",
			xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			argv[idx_lcomm_list]->arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_lcommunity_delete：上述命令的 no 形式，销毁 large-comm-list-delete action。 */
DEFUN_YANG (no_set_lcommunity_delete,
	    no_set_lcommunity_delete_cmd,
	    "no set large-comm-list <(1-99)|(100-500)|LCOMMUNITY_LIST_NAME> [delete]",
	    NO_STR
	    SET_STR
	    "set BGP large community list (for deletion)\n"
	    "Large Community-list number (standard)\n"
	    "Large Communitly-list number (expanded)\n"
	    "Large Community-list name\n"
	    "Delete matching large communities\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:large-comm-list-delete']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】ALIAS no_set_lcommunity_delete(no_set_lcommunity_delete_short_cmd)：
 * 把 "no set large-comm-list" 别名到 no_set_lcommunity_delete 的实现。 */
ALIAS_YANG (no_set_lcommunity_delete,
            no_set_lcommunity_delete_short_cmd,
            "no set large-comm-list",
            NO_STR
            SET_STR
            "set BGP large community list (for deletion)\n")

/* 【中文补充】set_ecommunity_rt：CLI "set extcommunity rt ASN:NN..."。
 * 拼接参数，创建 set-extcommunity-rt action，写入 extcommunity-rt(RT 扩展团体)。
 * VPN 与 EVPN 场景常用；修改属性，需重新宣告。 */
DEFUN_YANG (set_ecommunity_rt,
	    set_ecommunity_rt_cmd,
	    "set extcommunity rt ASN:NN_OR_IP-ADDRESS:NN...",
	    SET_STR
	    "BGP extended community attribute\n"
	    "Route Target extended community\n"
	    "VPN extended community\n")
{
	int idx_asn_nn = 3;
	char *str;
	int ret;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-extcommunity-rt']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:extcommunity-rt", xpath);
	str = argv_concat(argv, argc, idx_asn_nn);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, str);
	ret = nb_cli_apply_changes(vty, NULL);
	XFREE(MTYPE_TMP, str);
	return ret;
}

/* 【中文补充】no_set_ecommunity_rt：CLI "no set extcommunity rt ..."。销毁该 action。 */
DEFUN_YANG (no_set_ecommunity_rt,
	    no_set_ecommunity_rt_cmd,
	    "no set extcommunity rt ASN:NN_OR_IP-ADDRESS:NN...",
	    NO_STR
	    SET_STR
	    "BGP extended community attribute\n"
	    "Route Target extended community\n"
	    "VPN extended community\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-extcommunity-rt']";
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】ALIAS no_set_ecommunity_rt(no_set_ecommunity_rt_short_cmd)：把
 * "no set extcommunity rt" 别名到 no_set_ecommunity_rt 的实现。 */
ALIAS_YANG (no_set_ecommunity_rt,
            no_set_ecommunity_rt_short_cmd,
            "no set extcommunity rt",
            NO_STR
            SET_STR
            "BGP extended community attribute\n"
            "Route Target extended community\n")

/* 【中文补充】set_ecommunity_soo：CLI "set extcommunity soo ASN:NN..."。
 * 拼接参数，创建 set-extcommunity-soo action，写入 SoO(Site-of-Origin)扩展团体。 */
DEFUN_YANG (set_ecommunity_soo,
	    set_ecommunity_soo_cmd,
	    "set extcommunity soo ASN:NN_OR_IP-ADDRESS:NN...",
	    SET_STR
	   "BGP extended community attribute\n"
	   "Site-of-Origin extended community\n"
	   "VPN extended community\n")
{
	int idx_asn_nn = 3;
	char *str;
	int ret;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-extcommunity-soo']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:extcommunity-soo",
		 xpath);
	str = argv_concat(argv, argc, idx_asn_nn);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, str);
	ret = nb_cli_apply_changes(vty, NULL);
	XFREE(MTYPE_TMP, str);
	return ret;
}

/* 【中文补充】no_set_ecommunity_soo：CLI "no set extcommunity soo ..."。销毁 set-extcommunity-soo action。 */
DEFUN_YANG (no_set_ecommunity_soo,
	    no_set_ecommunity_soo_cmd,
	    "no set extcommunity soo ASN:NN_OR_IP-ADDRESS:NN...",
	    NO_STR
	    SET_STR
	    "BGP extended community attribute\n"
	    "Site-of-Origin extended community\n"
	    "VPN extended community\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-extcommunity-soo']";
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】ALIAS no_set_ecommunity_soo(no_set_ecommunity_soo_short_cmd)：把
 * "no set extcommunity soo" 别名到 no_set_ecommunity_soo 的实现。 */
ALIAS_YANG (no_set_ecommunity_soo,
            no_set_ecommunity_soo_short_cmd,
            "no set extcommunity soo",
            NO_STR
            SET_STR
            "GP extended community attribute\n"
            "Site-of-Origin extended community\n")

/* 【中文补充】set_ecommunity_none：CLI "set extcommunity none"。
 * 创建 set-extcommunity-none action 并写入 extcommunity-none = "true"，清空扩展团体。 */
DEFUN_YANG(set_ecommunity_none, set_ecommunity_none_cmd,
	   "set extcommunity none",
	   SET_STR
	   "BGP extended community attribute\n"
	   "No extended community attribute\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-extcommunity-none']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:extcommunity-none",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, "true");
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_ecommunity_none：上述命令的 no 形式，销毁 set-extcommunity-none action。 */
DEFUN_YANG(no_set_ecommunity_none, no_set_ecommunity_none_cmd,
	   "no set extcommunity none",
	   NO_STR SET_STR
	   "BGP extended community attribute\n"
	   "No extended community attribute\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-extcommunity-none']";
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_ecommunity_lb：CLI "set extcommunity bandwidth <VALUE|cumulative|num-multipaths> [non-transitive]"。
 * 创建 set-extcommunity-lb action；按参数写 lb-type(explicit-bandwidth / cumulative-bandwidth /
 *   computed-bandwidth)，显式带宽时另写 bandwidth，并按是否有 non-transitive 写
 *   two-octet-as-specific(布尔)。设置链路带宽扩展团体，需重新宣告。 */
DEFUN_YANG (set_ecommunity_lb,
	    set_ecommunity_lb_cmd,
	    "set extcommunity bandwidth <(0-4294967295)|cumulative|num-multipaths> [non-transitive]",
	    SET_STR
	    "BGP extended community attribute\n"
	    "Link bandwidth extended community\n"
	    "Bandwidth value in Mbps\n"
	    "Cumulative bandwidth of all multipaths (outbound-only)\n"
	    "Internally computed bandwidth based on number of multipaths (outbound-only)\n"
	    "Attribute is set as non-transitive\n")
{
	int idx_lb = 3;
	int idx_non_transitive = 0;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-extcommunity-lb']";
	char xpath_lb_type[XPATH_MAXLEN];
	char xpath_bandwidth[XPATH_MAXLEN];
	char xpath_non_transitive[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_lb_type, sizeof(xpath_lb_type),
		 "%s/rmap-set-action/frr-bgp-route-map:extcommunity-lb/lb-type",
		 xpath);
	snprintf(xpath_bandwidth, sizeof(xpath_bandwidth),
		 "%s/rmap-set-action/frr-bgp-route-map:extcommunity-lb/bandwidth",
		 xpath);
	snprintf(xpath_non_transitive, sizeof(xpath_non_transitive),
		 "%s/rmap-set-action/frr-bgp-route-map:extcommunity-lb/two-octet-as-specific",
		 xpath);

	if ((strcmp(argv[idx_lb]->arg, "cumulative")) == 0)
		nb_cli_enqueue_change(vty, xpath_lb_type, NB_OP_MODIFY,
				      "cumulative-bandwidth");
	else if ((strcmp(argv[idx_lb]->arg, "num-multipaths")) == 0)
		nb_cli_enqueue_change(vty, xpath_lb_type, NB_OP_MODIFY,
				      "computed-bandwidth");
	else {
		nb_cli_enqueue_change(vty, xpath_lb_type, NB_OP_MODIFY,
				      "explicit-bandwidth");
		nb_cli_enqueue_change(vty, xpath_bandwidth, NB_OP_MODIFY,
				      argv[idx_lb]->arg);
	}

	if (argv_find(argv, argc, "non-transitive", &idx_non_transitive))
		nb_cli_enqueue_change(vty, xpath_non_transitive, NB_OP_MODIFY,
				      "true");
	else
		nb_cli_enqueue_change(vty, xpath_non_transitive, NB_OP_MODIFY,
				      "false");

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_ecommunity_lb：上述命令的 no 形式，销毁 set-extcommunity-lb action。 */
DEFUN_YANG (no_set_ecommunity_lb,
	    no_set_ecommunity_lb_cmd,
	    "no set extcommunity bandwidth <(0-4294967295)|cumulative|num-multipaths> [non-transitive]",
	    NO_STR
	    SET_STR
	    "BGP extended community attribute\n"
	    "Link bandwidth extended community\n"
	    "Bandwidth value in Mbps\n"
	    "Cumulative bandwidth of all multipaths (outbound-only)\n"
	    "Internally computed bandwidth based on number of multipaths (outbound-only)\n"
	    "Attribute is set as non-transitive\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-extcommunity-lb']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】ALIAS no_set_ecommunity_lb(no_set_ecommunity_lb_short_cmd)：把
 * "no set extcommunity bandwidth" 别名到 no_set_ecommunity_lb 的实现。 */
ALIAS_YANG (no_set_ecommunity_lb,
            no_set_ecommunity_lb_short_cmd,
            "no set extcommunity bandwidth",
            NO_STR
            SET_STR
            "BGP extended community attribute\n"
            "Link bandwidth extended community\n")

/* 【中文补充】set_ecommunity_nt：CLI "set extcommunity nt RTLIST..."。
 * 拼接参数，创建 set-extcommunity-nt action，写入 extcommunity-nt(Node Target 扩展团体)。 */
DEFPY_YANG (set_ecommunity_nt,
	    set_ecommunity_nt_cmd,
	    "set extcommunity nt RTLIST...",
	    SET_STR
	    "BGP extended community attribute\n"
	    "Node Target extended community\n"
	    "Node Target ID\n")
{
	int idx_nt = 3;
	char *str;
	int ret;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-extcommunity-nt']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:extcommunity-nt", xpath);
	str = argv_concat(argv, argc, idx_nt);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, str);
	ret = nb_cli_apply_changes(vty, NULL);
	XFREE(MTYPE_TMP, str);
	return ret;
}

/* 【中文补充】no_set_ecommunity_nt：上述命令的 no 形式，销毁 set-extcommunity-nt action。 */
DEFPY_YANG (no_set_ecommunity_nt,
	    no_set_ecommunity_nt_cmd,
	    "no set extcommunity nt RTLIST...",
	    NO_STR
	    SET_STR
	    "BGP extended community attribute\n"
	    "Node Target extended community\n"
	    "Node Target ID\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-extcommunity-nt']";
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_ecommunity_color：CLI "set extcommunity color RTLIST..."。
 * 拼接参数，创建 set-extcommunity-color action，写入 extcommunity-color(Color 扩展团体，用于 SR 等)。 */
DEFPY_YANG(set_ecommunity_color, set_ecommunity_color_cmd,
	   "set extcommunity color RTLIST...",
	   SET_STR
	   "BGP extended community attribute\n"
	   "Color extended community\n"
	   "Color ID\n")
{
	int idx_color = 3;
	char *str;
	int ret;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-extcommunity-color']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:extcommunity-color",
		 xpath);
	str = argv_concat(argv, argc, idx_color);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, str);
	ret = nb_cli_apply_changes(vty, NULL);
	XFREE(MTYPE_TMP, str);
	return ret;
}

/* 【中文补充】no_set_ecommunity_color_all：CLI "no set extcommunity color"。
 * 整体销毁 set-extcommunity-color action(不带具体颜色值)。 */
DEFPY_YANG(no_set_ecommunity_color_all, no_set_ecommunity_color_all_cmd,
	   "no set extcommunity color",
	   NO_STR SET_STR
	   "BGP extended community attribute\n"
	   "Color extended community\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-extcommunity-color']";
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_ecommunity_color：CLI "no set extcommunity color RTLIST..."。
 * 同样销毁 set-extcommunity-color action(带具体颜色值的否定形式)。 */
DEFPY_YANG(no_set_ecommunity_color, no_set_ecommunity_color_cmd,
	   "no set extcommunity color RTLIST...",
	   NO_STR SET_STR
	   "BGP extended community attribute\n"
	   "Color extended community\n"
	   "Color ID\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-extcommunity-color']";
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】ALIAS no_set_ecommunity_nt(no_set_ecommunity_nt_short_cmd)：把
 * "no set extcommunity nt" 别名到 no_set_ecommunity_nt 的实现。 */
ALIAS_YANG (no_set_ecommunity_nt,
            no_set_ecommunity_nt_short_cmd,
            "no set extcommunity nt",
            NO_STR
            SET_STR
            "BGP extended community attribute\n"
            "Node Target extended community\n")

/* 【中文补充】set_origin：CLI "set origin <egp|igp|incomplete>"。
 * 把参数规整为 igp / egp / incomplete，非法值报错返回失败；创建 set-origin action
 * 并写入 origin。修改 ORIGIN 属性，需重新宣告。 */
DEFUN_YANG (set_origin,
	    set_origin_cmd,
	    "set origin <egp|igp|incomplete>",
	    SET_STR
	    "BGP origin code\n"
	    "remote EGP\n"
	    "local IGP\n"
	    "unknown heritage\n")
{
	int idx_origin = 2;
	const char *origin_type;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-origin']";
	char xpath_value[XPATH_MAXLEN];

	if (strncmp(argv[idx_origin]->arg, "igp", 2) == 0)
		origin_type = "igp";
	else if (strncmp(argv[idx_origin]->arg, "egp", 1) == 0)
		origin_type = "egp";
	else if (strncmp(argv[idx_origin]->arg, "incomplete", 2) == 0)
		origin_type = "incomplete";
	else {
		vty_out(vty, "%% Invalid match origin type\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:origin", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, origin_type);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_origin：上述命令的 no 形式，销毁 set-origin action。 */
DEFUN_YANG (no_set_origin,
	    no_set_origin_cmd,
	    "no set origin [<egp|igp|incomplete>]",
	    NO_STR
	    SET_STR
	    "BGP origin code\n"
	    "remote EGP\n"
	    "local IGP\n"
	    "unknown heritage\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:set-origin']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_atomic_aggregate：CLI "set atomic-aggregate"。
 * 创建 atomic-aggregate action(写入 NULL)，给路由打上 ATOMIC_AGGREGATE 属性。 */
DEFUN_YANG (set_atomic_aggregate,
	    set_atomic_aggregate_cmd,
	    "set atomic-aggregate",
	    SET_STR
	    "BGP atomic aggregate attribute\n" )
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:atomic-aggregate']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:atomic-aggregate",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_atomic_aggregate：上述命令的 no 形式，销毁 atomic-aggregate action。 */
DEFUN_YANG (no_set_atomic_aggregate,
	    no_set_atomic_aggregate_cmd,
	    "no set atomic-aggregate",
	    NO_STR
	    SET_STR
	    "BGP atomic aggregate attribute\n" )
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:atomic-aggregate']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_aigp_metric：CLI "set aigp-metric <igp-metric|(0-4294967295)>"。
 * 创建 aigp-metric action，写入 aigp_metric(可为 igp-metric 或手工数值)。
 * 修改 AIGP 属性，参与选路，需重新宣告。 */
DEFPY_YANG (set_aigp_metric,
	    set_aigp_metric_cmd,
	    "set aigp-metric <igp-metric|(0-4294967295)>$aigp_metric",
	    SET_STR
	    "BGP AIGP attribute (AIGP Metric TLV)\n"
	    "AIGP Metric value from IGP protocol\n"
	    "Manual AIGP Metric value\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:aigp-metric']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:aigp-metric", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, aigp_metric);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_aigp_metric：上述命令的 no 形式，销毁 aigp-metric action。 */
DEFPY_YANG (no_set_aigp_metric,
	    no_set_aigp_metric_cmd,
	    "no set aigp-metric [<igp-metric|(0-4294967295)>]",
	    NO_STR
	    SET_STR
	    "BGP AIGP attribute (AIGP Metric TLV)\n"
	    "AIGP Metric value from IGP protocol\n"
	    "Manual AIGP Metric value\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:aigp-metric']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_aggregator_as：CLI "set aggregator as ASNUM A.B.C.D"。
 * 校验 AS 号(asn_str2asn)非法报错；创建 aggregator action，分别写入 AS 号与聚合者 IP。
 * 修改聚合者属性，需重新宣告。 */
DEFUN_YANG (set_aggregator_as,
	    set_aggregator_as_cmd,
	    "set aggregator as ASNUM A.B.C.D",
	    SET_STR
	    "BGP aggregator attribute\n"
	    "AS number of aggregator\n"
	    AS_STR
	    "IP address of aggregator\n")
{
	int idx_number = 3;
	int idx_ipv4 = 4;
	char xpath_asn[XPATH_MAXLEN];
	char xpath_addr[XPATH_MAXLEN];
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:aggregator']";
	as_t as_value;

	if (!asn_str2asn(argv[idx_number]->arg, &as_value)) {
		vty_out(vty, "%% Invalid AS value %s\n", argv[idx_number]->arg);
		return CMD_WARNING_CONFIG_FAILED;
	}

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	snprintf(
		xpath_asn, sizeof(xpath_asn),
		"%s/rmap-set-action/frr-bgp-route-map:aggregator/aggregator-asn",
		xpath);
	nb_cli_enqueue_change(vty, xpath_asn, NB_OP_MODIFY,
			      argv[idx_number]->arg);

	snprintf(
		xpath_addr, sizeof(xpath_addr),
		"%s/rmap-set-action/frr-bgp-route-map:aggregator/aggregator-address",
		xpath);
	nb_cli_enqueue_change(vty, xpath_addr, NB_OP_MODIFY,
			      argv[idx_ipv4]->arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_aggregator_as：CLI "no set aggregator as [ASNUM A.B.C.D]"。
 * 整体销毁 aggregator action。 */
DEFUN_YANG (no_set_aggregator_as,
	    no_set_aggregator_as_cmd,
	    "no set aggregator as [ASNUM A.B.C.D]",
	    NO_STR
	    SET_STR
	    "BGP aggregator attribute\n"
	    "AS number of aggregator\n"
	    AS_STR
	    "IP address of aggregator\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:aggregator']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_ipv6_next_hop_address：CLI "match ipv6 next-hop address X:X::X:X"。
 * 创建 ipv6-nexthop match-condition，写入 ipv6-address。依赖下一跳地址匹配；只读。 */
DEFUN_YANG (match_ipv6_next_hop_address,
	    match_ipv6_next_hop_address_cmd,
	    "match ipv6 next-hop address X:X::X:X",
	    MATCH_STR
	    IPV6_STR
	    "Match IPv6 next-hop address of route\n"
	    "IPv6 address\n"
	    "IPv6 address of next hop\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:ipv6-nexthop']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:ipv6-address",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			      argv[argc - 1]->arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_match_ipv6_next_hop_address：上述命令的 no 形式，销毁 ipv6-nexthop condition。 */
DEFUN_YANG (no_match_ipv6_next_hop_address,
	    no_match_ipv6_next_hop_address_cmd,
	    "no match ipv6 next-hop address X:X::X:X",
	    NO_STR
	    MATCH_STR
	    IPV6_STR
	    "Match IPv6 next-hop address of route\n"
	    "IPv6 address\n"
	    "IPv6 address of next hop\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:ipv6-nexthop']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】ALIAS_HIDDEN match_ipv6_next_hop_address(match_ipv6_next_hop_old_cmd)：
 * 隐藏别名，兼容旧写法 "match ipv6 next-hop X:X::X:X"。 */
ALIAS_HIDDEN (match_ipv6_next_hop_address,
	      match_ipv6_next_hop_old_cmd,
	      "match ipv6 next-hop X:X::X:X",
	      MATCH_STR
	      IPV6_STR
	      "Match IPv6 next-hop address of route\n"
	      "IPv6 address of next hop\n")

/* 【中文补充】ALIAS_HIDDEN no_match_ipv6_next_hop_address(no_match_ipv6_next_hop_old_cmd)：
 * 隐藏别名，兼容旧写法 "no match ipv6 next-hop X:X::X:X"。 */
ALIAS_HIDDEN (no_match_ipv6_next_hop_address,
	      no_match_ipv6_next_hop_old_cmd,
	      "no match ipv6 next-hop X:X::X:X",
	      NO_STR
	      MATCH_STR
	      IPV6_STR
	      "Match IPv6 next-hop address of route\n"
	      "IPv6 address of next hop\n")

/* 【中文补充】match_ipv4_next_hop：CLI "match ip next-hop address A.B.C.D"。
 * 创建 ipv4-nexthop match-condition，写入 ipv4-address。依赖下一跳地址匹配；只读。 */
DEFPY_YANG (match_ipv4_next_hop,
       match_ipv4_next_hop_cmd,
       "match ip next-hop address A.B.C.D",
       MATCH_STR
       IP_STR
       "Match IP next-hop address of route\n"
       "IP address\n"
       "IP address of next-hop\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:ipv4-nexthop']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:ipv4-address",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, argv[4]->arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_match_ipv4_next_hop：上述命令的 no 形式，销毁 ipv4-nexthop condition。 */
DEFPY_YANG (no_match_ipv4_next_hop,
       no_match_ipv4_next_hop_cmd,
       "no match ip next-hop address [A.B.C.D]",
       NO_STR
       MATCH_STR
       IP_STR
       "Match IP next-hop address of route\n"
       "IP address\n"
       "IP address of next-hop\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:ipv4-nexthop']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_ipv6_nexthop_peer：CLI "set ipv6 next-hop peer-address"。
 * 创建 ipv6-peer-address action 并把 preference 置为 "true"，表示用对端地址作下一跳。
 * 影响下一跳，需重新宣告。 */
DEFUN_YANG (set_ipv6_nexthop_peer,
	    set_ipv6_nexthop_peer_cmd,
	    "set ipv6 next-hop peer-address",
	    SET_STR
	    IPV6_STR
	    "Next hop address\n"
	    "Use peer address (for BGP only)\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:ipv6-peer-address']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:preference", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, "true");

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_ipv6_nexthop_peer：上述命令的 no 形式，销毁 ipv6-peer-address action。 */
DEFUN_YANG (no_set_ipv6_nexthop_peer,
	    no_set_ipv6_nexthop_peer_cmd,
	    "no set ipv6 next-hop peer-address",
	    NO_STR
	    SET_STR
	    IPV6_STR
	    "IPv6 next-hop address\n"
	    "Use peer address (for BGP only)\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:ipv6-peer-address']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_ipv6_nexthop_prefer_global：CLI "set ipv6 next-hop prefer-global"。
 * 创建 ipv6-prefer-global action 并把 preference 置为 "true"，全局地址优先于 link-local。 */
DEFUN_YANG (set_ipv6_nexthop_prefer_global,
	    set_ipv6_nexthop_prefer_global_cmd,
	    "set ipv6 next-hop prefer-global",
	    SET_STR
	    IPV6_STR
	    "IPv6 next-hop address\n"
	    "Prefer global over link-local if both exist\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:ipv6-prefer-global']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:preference", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, "true");

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_ipv6_nexthop_prefer_global：上述命令的 no 形式，销毁 ipv6-prefer-global action。 */
DEFUN_YANG (no_set_ipv6_nexthop_prefer_global,
	    no_set_ipv6_nexthop_prefer_global_cmd,
	    "no set ipv6 next-hop prefer-global",
	    NO_STR
	    SET_STR
	    IPV6_STR
	    "IPv6 next-hop address\n"
	    "Prefer global over link-local if both exist\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:ipv6-prefer-global']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】set_ipv6_nexthop_global：CLI "set ipv6 next-hop global X:X::X:X"。
 * 创建 ipv6-nexthop-global action，写入全局 IPv6 下一跳地址。影响选路，需重新宣告。 */
DEFUN_YANG (set_ipv6_nexthop_global,
	    set_ipv6_nexthop_global_cmd,
	    "set ipv6 next-hop global X:X::X:X",
	    SET_STR
	    IPV6_STR
	    "IPv6 next-hop address\n"
	    "IPv6 global address\n"
	    "IPv6 address of next hop\n")
{
	int idx_ipv6 = 4;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:ipv6-nexthop-global']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:ipv6-address", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			      argv[idx_ipv6]->arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_ipv6_nexthop_global：上述命令的 no 形式，销毁 ipv6-nexthop-global action。 */
DEFUN_YANG (no_set_ipv6_nexthop_global,
	    no_set_ipv6_nexthop_global_cmd,
	    "no set ipv6 next-hop global X:X::X:X",
	    NO_STR
	    SET_STR
	    IPV6_STR
	    "IPv6 next-hop address\n"
	    "IPv6 global address\n"
	    "IPv6 address of next hop\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:ipv6-nexthop-global']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

#ifdef KEEP_OLD_VPN_COMMANDS
/* 【中文补充】set_vpn_nexthop：旧式 CLI "set <vpnv4|vpnv6> next-hop ..."(受 KEEP_OLD_VPN_COMMANDS 保护)。
 * 用 argv_find_and_parse_vpnvx 判族：IPv4 写 ipv4-vpn-address，IPv6 写 ipv6-vpn-address。
 * 未识别族时返回 CMD_SUCCESS 不做变更。 */
DEFUN_YANG (set_vpn_nexthop,
	    set_vpn_nexthop_cmd,
	    "set <vpnv4 next-hop A.B.C.D|vpnv6 next-hop X:X::X:X>",
	    SET_STR
	    "VPNv4 information\n"
	    "VPN next-hop address\n"
	    "IP address of next hop\n"
	    "VPNv6 information\n"
	    "VPN next-hop address\n"
	    "IPv6 address of next hop\n")
{
	int idx_ip = 3;
	afi_t afi;
	int idx = 0;
	char xpath_value[XPATH_MAXLEN];

	if (argv_find_and_parse_vpnvx(argv, argc, &idx, &afi)) {
		if (afi == AFI_IP) {
			const char *xpath =
				"./set-action[action='frr-bgp-route-map:ipv4-vpn-address']";

			nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
			snprintf(
				xpath_value, sizeof(xpath_value),
				"%s/rmap-set-action/frr-bgp-route-map:ipv4-address",
				xpath);
		} else {
			const char *xpath =
				"./set-action[action='frr-bgp-route-map:ipv6-vpn-address']";

			nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
			snprintf(
				xpath_value, sizeof(xpath_value),
				"%s/rmap-set-action/frr-bgp-route-map:ipv6-address",
				xpath);
		}

		nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
				argv[idx_ip]->arg);

		return nb_cli_apply_changes(vty, NULL);
	}

	return CMD_SUCCESS;
}

/* 【中文补充】no_set_vpn_nexthop：上述旧式命令的 no 形式，按族销毁 ipv4-vpn-address 或 ipv6-vpn-address。 */
DEFUN_YANG (no_set_vpn_nexthop,
	   no_set_vpn_nexthop_cmd,
	   "no set <vpnv4 next-hop A.B.C.D|vpnv6 next-hop X:X::X:X>",
	   NO_STR
	   SET_STR
	   "VPNv4 information\n"
	   "VPN next-hop address\n"
	   "IP address of next hop\n"
	   "VPNv6 information\n"
	   "VPN next-hop address\n"
	   "IPv6 address of next hop\n")
{
	afi_t afi;
	int idx = 0;

	if (argv_find_and_parse_vpnvx(argv, argc, &idx, &afi)) {
		if (afi == AFI_IP) {
			const char *xpath =
				"./set-action[action='frr-bgp-route-map:ipv4-vpn-address']";
			nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
		} else {
			const char *xpath =
				"./set-action[action='frr-bgp-route-map:ipv6-vpn-address']";
			nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
		}
		return nb_cli_apply_changes(vty, NULL);
	}
	return CMD_SUCCESS;
}
#endif /* KEEP_OLD_VPN_COMMANDS */

/* 【中文补充】set_ipx_vpn_nexthop：CLI "set <ipv4|ipv6> vpn next-hop <A.B.C.D|X:X::X:X>"。
 * 用 argv_find_and_parse_afi 判族并校验地址类型匹配(IPv4 族不接受 IPv6 地址，反之亦然，
 * 不匹配报错)；创建对应 ipv4-vpn-address 或 ipv6-vpn-address action 并写入地址。 */
DEFPY_YANG (set_ipx_vpn_nexthop,
	    set_ipx_vpn_nexthop_cmd,
	    "set <ipv4|ipv6> vpn next-hop <A.B.C.D$addrv4|X:X::X:X$addrv6>",
	    SET_STR
	    "IPv4 information\n"
	    "IPv6 information\n"
	    "VPN information\n"
	    "VPN next-hop address\n"
	    "IP address of next hop\n"
	    "IPv6 address of next hop\n")
{
	int idx_ip = 4;
	afi_t afi;
	int idx = 0;
	char xpath_value[XPATH_MAXLEN];

	if (argv_find_and_parse_afi(argv, argc, &idx, &afi)) {
		if (afi == AFI_IP) {
			if (addrv6_str) {
				vty_out(vty, "%% IPv4 next-hop expected\n");
				return CMD_WARNING_CONFIG_FAILED;
			}

			const char *xpath =
				"./set-action[action='frr-bgp-route-map:ipv4-vpn-address']";

			nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
			snprintf(
				xpath_value, sizeof(xpath_value),
				"%s/rmap-set-action/frr-bgp-route-map:ipv4-address",
				xpath);
		} else {
			if (addrv4_str) {
				vty_out(vty, "%% IPv6 next-hop expected\n");
				return CMD_WARNING_CONFIG_FAILED;
			}

			const char *xpath =
				"./set-action[action='frr-bgp-route-map:ipv6-vpn-address']";

			nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
			snprintf(
				xpath_value, sizeof(xpath_value),
				"%s/rmap-set-action/frr-bgp-route-map:ipv6-address",
				xpath);
		}
		nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
				      argv[idx_ip]->arg);
		return nb_cli_apply_changes(vty, NULL);
	}
	return CMD_SUCCESS;
}

/* 【中文补充】no_set_ipx_vpn_nexthop：上述命令的 no 形式，按族销毁 ipv4-vpn-address 或 ipv6-vpn-address。 */
DEFUN_YANG (no_set_ipx_vpn_nexthop,
	    no_set_ipx_vpn_nexthop_cmd,
	    "no set <ipv4|ipv6> vpn next-hop [<A.B.C.D|X:X::X:X>]",
	    NO_STR
	    SET_STR
	    "IPv4 information\n"
	    "IPv6 information\n"
	    "VPN information\n"
	    "VPN next-hop address\n"
	    "IP address of next hop\n"
	    "IPv6 address of next hop\n")
{
	afi_t afi;
	int idx = 0;

	if (argv_find_and_parse_afi(argv, argc, &idx, &afi)) {
		if (afi == AFI_IP) {
			const char *xpath =
				"./set-action[action='frr-bgp-route-map:ipv4-vpn-address']";
			nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
		} else {
			const char *xpath =
				"./set-action[action='frr-bgp-route-map:ipv6-vpn-address']";
			nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
		}
		return nb_cli_apply_changes(vty, NULL);
	}
	return CMD_SUCCESS;
}

/* 【中文补充】set_originator_id：CLI "set originator-id A.B.C.D"。
 * 创建 originator-id action，写入 originator-id 地址。修改属性，需重新宣告。 */
DEFUN_YANG (set_originator_id,
	    set_originator_id_cmd,
	    "set originator-id A.B.C.D",
	    SET_STR
	   "BGP originator ID attribute\n"
	   "IP address of originator\n")
{
	int idx_ipv4 = 2;
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:originator-id']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-set-action/frr-bgp-route-map:originator-id", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
			      argv[idx_ipv4]->arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_set_originator_id：上述命令的 no 形式，销毁 originator-id action。 */
DEFUN_YANG (no_set_originator_id,
	    no_set_originator_id_cmd,
	    "no set originator-id [A.B.C.D]",
	    NO_STR
	    SET_STR
	    "BGP originator ID attribute\n"
	    "IP address of originator\n")
{
	const char *xpath =
		"./set-action[action='frr-bgp-route-map:originator-id']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_rpki_extcommunity：CLI "[no] match rpki-extcommunity <valid|invalid|notfound>"。
 * 创建 rpki-extcommunity match-condition；非 no 时写入状态值(argv[2])，no 时留空。
 * 运行时按 RPKI 源验证状态匹配。 */
DEFPY_YANG (match_rpki_extcommunity,
       match_rpki_extcommunity_cmd,
       "[no$no] match rpki-extcommunity <valid|invalid|notfound>",
       NO_STR
       MATCH_STR
       "BGP RPKI (Origin Validation State) extended community attribute\n"
       "Valid prefix\n"
       "Invalid prefix\n"
       "Prefix not found\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:rpki-extcommunity']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);

	if (!no) {
		snprintf(
			xpath_value, sizeof(xpath_value),
			"%s/rmap-match-condition/frr-bgp-route-map:rpki-extcommunity",
			xpath);
		nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY,
				      argv[2]->arg);
	}

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_source_protocol：CLI "match source-protocol <PROTO>"。
 * 创建 source-protocol match-condition，写入协议名(proto)，匹配路由学到的来源协议。 */
DEFPY_YANG (match_source_protocol,
            match_source_protocol_cmd,
	    "match source-protocol " FRR_REDIST_STR_ZEBRA "$proto",
	    MATCH_STR
	    "Match protocol via which the route was learnt\n"
	    FRR_REDIST_HELP_STR_ZEBRA)
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:source-protocol']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:source-protocol",
		 xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, proto);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】no_match_source_protocol：上述命令的 no 形式，销毁 source-protocol condition。 */
DEFPY_YANG (no_match_source_protocol,
            no_match_source_protocol_cmd,
	    "no match source-protocol [" FRR_REDIST_STR_ZEBRA "]",
	    NO_STR
	    MATCH_STR
	    "Match protocol via which the route was learnt\n"
	    FRR_REDIST_HELP_STR_ZEBRA)
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:source-protocol']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】match_vpn_dataplane：CLI "[no] match vpn dataplane [<mpls|srv6|vxlan>]"。
 * no 或缺省 dataplane 时销毁 condition；否则创建并写入 vpn-dataplane 值。 */
DEFPY_YANG (match_vpn_dataplane,
       match_vpn_dataplane_cmd,
       "[no$no] match vpn dataplane [<mpls|srv6|vxlan>$dataplane]",
       NO_STR
       MATCH_STR
       "VPN operations\n"
       "Dataplane operation\n"
       "Valid MPLS path\n"
       "Valid SRv6 path\n"
       "Valid VXLAN path\n")
{
	const char *xpath = "./match-condition[condition='frr-bgp-route-map:match-vpn-dataplane']";
	char xpath_value[XPATH_MAXLEN];
	enum nb_operation operation = NB_OP_CREATE;

	if (no || !dataplane)
		operation = NB_OP_DESTROY;

	nb_cli_enqueue_change(vty, xpath, operation, NULL);

	if (!no && dataplane) {
		snprintf(xpath_value, sizeof(xpath_value),
			 "%s/rmap-match-condition/frr-bgp-route-map:vpn-dataplane", xpath);
		nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, dataplane);
	}

	return nb_cli_apply_changes(vty, NULL);
}

/* 【中文补充】bgp_route_map_init：BGP route-map 子系统初始化入口。
 * 先调 route_map_init，再注册生命周期钩子 add / delete / event，
 * 并以 generic_match_add / generic_match_delete 成对注册大量通用 match 钩子
 * (接口、IP 地址、前缀列表、下一跳、下一跳类型、weight、metric、tag、community 等)；
 * 最后逐个 install_element 把本文件所有 match / set 规则命令注册到 RMAP_NODE。 */
/* Initialization of route map. */
void bgp_route_map_init(void)
{
	route_map_init();

	route_map_add_hook(bgp_route_map_add);
	route_map_delete_hook(bgp_route_map_delete);
	route_map_event_hook(bgp_route_map_event);

	route_map_match_interface_hook(generic_match_add);
	route_map_no_match_interface_hook(generic_match_delete);

	route_map_match_ip_address_hook(generic_match_add);
	route_map_no_match_ip_address_hook(generic_match_delete);

	route_map_match_ip_address_prefix_list_hook(generic_match_add);
	route_map_no_match_ip_address_prefix_list_hook(generic_match_delete);

	route_map_match_ip_next_hop_hook(generic_match_add);
	route_map_no_match_ip_next_hop_hook(generic_match_delete);

	route_map_match_ipv6_next_hop_hook(generic_match_add);
	route_map_no_match_ipv6_next_hop_hook(generic_match_delete);

	route_map_match_ip_next_hop_prefix_list_hook(generic_match_add);
	route_map_no_match_ip_next_hop_prefix_list_hook(generic_match_delete);

	route_map_match_ip_next_hop_type_hook(generic_match_add);
	route_map_no_match_ip_next_hop_type_hook(generic_match_delete);

	route_map_match_ipv6_address_hook(generic_match_add);
	route_map_no_match_ipv6_address_hook(generic_match_delete);

	route_map_match_ipv6_address_prefix_list_hook(generic_match_add);
	route_map_no_match_ipv6_address_prefix_list_hook(generic_match_delete);

	route_map_match_ipv6_next_hop_type_hook(generic_match_add);
	route_map_no_match_ipv6_next_hop_type_hook(generic_match_delete);

	route_map_match_ipv6_next_hop_prefix_list_hook(generic_match_add);
	route_map_no_match_ipv6_next_hop_prefix_list_hook(generic_match_delete);

	route_map_match_metric_hook(generic_match_add);
	route_map_no_match_metric_hook(generic_match_delete);

	route_map_match_tag_hook(generic_match_add);
	route_map_no_match_tag_hook(generic_match_delete);

	route_map_set_srte_color_hook(generic_set_add);
	route_map_no_set_srte_color_hook(generic_set_delete);

	route_map_set_ip_nexthop_hook(generic_set_add);
	route_map_no_set_ip_nexthop_hook(generic_set_delete);

	route_map_set_ipv6_nexthop_local_hook(generic_set_add);
	route_map_no_set_ipv6_nexthop_local_hook(generic_set_delete);

	route_map_set_metric_hook(generic_set_add);
	route_map_no_set_metric_hook(generic_set_delete);

	route_map_set_tag_hook(generic_set_add);
	route_map_no_set_tag_hook(generic_set_delete);

	route_map_install_match(&route_match_peer_cmd);
	route_map_install_match(&route_match_src_peer_cmd);
	route_map_install_match(&route_match_alias_cmd);
	route_map_install_match(&route_match_local_pref_cmd);
#ifdef HAVE_SCRIPTING
	route_map_install_match(&route_match_script_cmd);
#endif
	route_map_install_match(&route_match_ip_address_cmd);
	route_map_install_match(&route_match_ip_next_hop_cmd);
	route_map_install_match(&route_match_ip_route_source_cmd);
	route_map_install_match(&route_match_ip_address_prefix_list_cmd);
	route_map_install_match(&route_match_ip_next_hop_prefix_list_cmd);
	route_map_install_match(&route_match_ip_next_hop_type_cmd);
	route_map_install_match(&route_match_source_protocol_cmd);
	route_map_install_match(&route_match_ip_route_source_prefix_list_cmd);
	route_map_install_match(&route_match_aspath_cmd);
	route_map_install_match(&route_match_aspath_count_cmd);
	route_map_install_match(&route_match_community_cmd);
	route_map_install_match(&route_match_lcommunity_cmd);
	route_map_install_match(&route_match_ecommunity_cmd);
	route_map_install_match(&route_match_local_pref_cmd);
	route_map_install_match(&route_match_metric_cmd);
	route_map_install_match(&route_match_origin_cmd);
	route_map_install_match(&route_match_probability_cmd);
	route_map_install_match(&route_match_interface_cmd);
	route_map_install_match(&route_match_tag_cmd);
	route_map_install_match(&route_match_mac_address_cmd);
	route_map_install_match(&route_match_evpn_vni_cmd);
	route_map_install_match(&route_match_evpn_route_type_cmd);
	route_map_install_match(&route_match_evpn_rd_cmd);
	route_map_install_match(&route_match_community_limit_cmd);
	route_map_install_match(&route_match_extcommunity_limit_cmd);
	route_map_install_match(&route_match_evpn_default_route_cmd);
	route_map_install_match(&route_match_vrl_source_vrf_cmd);

	route_map_install_set(&route_set_evpn_gateway_ip_ipv4_cmd);
	route_map_install_set(&route_set_evpn_gateway_ip_ipv6_cmd);
	route_map_install_set(&route_set_table_id_cmd);
	route_map_install_set(&route_set_srte_color_cmd);
	route_map_install_set(&route_set_ip_nexthop_cmd);
	route_map_install_set(&route_set_local_pref_cmd);
	route_map_install_set(&route_set_weight_cmd);
	route_map_install_set(&route_set_label_index_cmd);
	route_map_install_set(&route_set_metric_cmd);
	route_map_install_set(&route_set_distance_cmd);
	route_map_install_set(&route_set_aspath_prepend_cmd);
	route_map_install_set(&route_set_aspath_exclude_cmd);
	route_map_install_set(&route_set_aspath_replace_cmd);
	route_map_install_set(&route_set_origin_cmd);
	route_map_install_set(&route_set_atomic_aggregate_cmd);
	route_map_install_set(&route_set_aigp_metric_cmd);
	route_map_install_set(&route_set_aggregator_as_cmd);
	route_map_install_set(&route_set_community_cmd);
	route_map_install_set(&route_set_community_delete_cmd);
	route_map_install_set(&route_set_ecommunity_delete_cmd);
	route_map_install_set(&route_set_lcommunity_cmd);
	route_map_install_set(&route_set_lcommunity_delete_cmd);
	route_map_install_set(&route_set_vpnv4_nexthop_cmd);
	route_map_install_set(&route_set_vpnv6_nexthop_cmd);
	route_map_install_set(&route_set_originator_id_cmd);
	route_map_install_set(&route_set_ecommunity_rt_cmd);
	route_map_install_set(&route_set_ecommunity_nt_cmd);
	route_map_install_set(&route_set_ecommunity_soo_cmd);
	route_map_install_set(&route_set_ecommunity_lb_cmd);
	route_map_install_set(&route_set_ecommunity_color_cmd);
	route_map_install_set(&route_set_ecommunity_none_cmd);
	route_map_install_set(&route_set_tag_cmd);
	route_map_install_set(&route_set_label_index_cmd);
	route_map_install_set(&route_set_l3vpn_nexthop_encapsulation_cmd);

	install_element(RMAP_NODE, &match_peer_cmd);
	install_element(RMAP_NODE, &match_peer_local_cmd);
	install_element(RMAP_NODE, &match_src_peer_cmd);
	install_element(RMAP_NODE, &no_match_src_peer_cmd);
	install_element(RMAP_NODE, &no_match_peer_cmd);
	install_element(RMAP_NODE, &match_ip_route_source_cmd);
	install_element(RMAP_NODE, &no_match_ip_route_source_cmd);
	install_element(RMAP_NODE, &match_ip_route_source_prefix_list_cmd);
	install_element(RMAP_NODE, &no_match_ip_route_source_prefix_list_cmd);
	install_element(RMAP_NODE, &match_mac_address_cmd);
	install_element(RMAP_NODE, &no_match_mac_address_cmd);
	install_element(RMAP_NODE, &match_evpn_vni_cmd);
	install_element(RMAP_NODE, &no_match_evpn_vni_cmd);
	install_element(RMAP_NODE, &match_evpn_route_type_cmd);
	install_element(RMAP_NODE, &no_match_evpn_route_type_cmd);
	install_element(RMAP_NODE, &match_evpn_rd_cmd);
	install_element(RMAP_NODE, &no_match_evpn_rd_cmd);
	install_element(RMAP_NODE, &match_evpn_default_route_cmd);
	install_element(RMAP_NODE, &no_match_evpn_default_route_cmd);
	install_element(RMAP_NODE, &set_evpn_gw_ip_ipv4_cmd);
	install_element(RMAP_NODE, &no_set_evpn_gw_ip_ipv4_cmd);
	install_element(RMAP_NODE, &set_evpn_gw_ip_ipv6_cmd);
	install_element(RMAP_NODE, &no_set_evpn_gw_ip_ipv6_cmd);
	install_element(RMAP_NODE, &match_vrl_source_vrf_cmd);
	install_element(RMAP_NODE, &no_match_vrl_source_vrf_cmd);

	install_element(RMAP_NODE, &match_aspath_cmd);
	install_element(RMAP_NODE, &no_match_aspath_cmd);
	install_element(RMAP_NODE, &match_aspath_count_cmd);
	install_element(RMAP_NODE, &match_local_pref_cmd);
	install_element(RMAP_NODE, &no_match_local_pref_cmd);
	install_element(RMAP_NODE, &match_alias_cmd);
	install_element(RMAP_NODE, &no_match_alias_cmd);
	install_element(RMAP_NODE, &match_community_cmd);
	install_element(RMAP_NODE, &no_match_community_cmd);
	install_element(RMAP_NODE, &match_community_limit_cmd);
	install_element(RMAP_NODE, &match_extcommunity_limit_cmd);
	install_element(RMAP_NODE, &match_lcommunity_cmd);
	install_element(RMAP_NODE, &no_match_lcommunity_cmd);
	install_element(RMAP_NODE, &match_ecommunity_cmd);
	install_element(RMAP_NODE, &no_match_ecommunity_cmd);
	install_element(RMAP_NODE, &match_origin_cmd);
	install_element(RMAP_NODE, &no_match_origin_cmd);
	install_element(RMAP_NODE, &match_probability_cmd);
	install_element(RMAP_NODE, &no_match_probability_cmd);

	install_element(RMAP_NODE, &no_set_table_id_cmd);
	install_element(RMAP_NODE, &set_table_id_cmd);
	install_element(RMAP_NODE, &set_ip_nexthop_peer_cmd);
	install_element(RMAP_NODE, &set_ip_nexthop_unchanged_cmd);
	install_element(RMAP_NODE, &set_local_pref_cmd);
	install_element(RMAP_NODE, &set_distance_cmd);
	install_element(RMAP_NODE, &no_set_distance_cmd);
	install_element(RMAP_NODE, &no_set_local_pref_cmd);
	install_element(RMAP_NODE, &set_weight_cmd);
	install_element(RMAP_NODE, &set_label_index_cmd);
	install_element(RMAP_NODE, &no_set_weight_cmd);
	install_element(RMAP_NODE, &no_set_label_index_cmd);
	install_element(RMAP_NODE, &set_aspath_prepend_asn_cmd);
	install_element(RMAP_NODE, &set_aspath_prepend_lastas_cmd);
	install_element(RMAP_NODE, &set_aspath_exclude_cmd);
	install_element(RMAP_NODE, &set_aspath_exclude_all_cmd);
	install_element(RMAP_NODE, &set_aspath_exclude_access_list_cmd);
	install_element(RMAP_NODE, &set_aspath_replace_asn_cmd);
	install_element(RMAP_NODE, &set_aspath_replace_access_list_cmd);
	install_element(RMAP_NODE, &no_set_aspath_prepend_last_as_cmd);
	install_element(RMAP_NODE, &no_set_aspath_prepend_as_cmd);
	install_element(RMAP_NODE, &no_set_aspath_exclude_cmd);
	install_element(RMAP_NODE, &no_set_aspath_exclude_all_cmd);
	install_element(RMAP_NODE, &no_set_aspath_exclude_access_list_cmd);
	install_element(RMAP_NODE, &no_set_aspath_replace_asn_cmd);
	install_element(RMAP_NODE, &no_set_aspath_replace_access_list_cmd);
	install_element(RMAP_NODE, &set_origin_cmd);
	install_element(RMAP_NODE, &no_set_origin_cmd);
	install_element(RMAP_NODE, &set_atomic_aggregate_cmd);
	install_element(RMAP_NODE, &no_set_atomic_aggregate_cmd);
	install_element(RMAP_NODE, &set_aigp_metric_cmd);
	install_element(RMAP_NODE, &no_set_aigp_metric_cmd);
	install_element(RMAP_NODE, &set_aggregator_as_cmd);
	install_element(RMAP_NODE, &no_set_aggregator_as_cmd);
	install_element(RMAP_NODE, &set_community_cmd);
	install_element(RMAP_NODE, &set_community_none_cmd);
	install_element(RMAP_NODE, &no_set_community_cmd);
	install_element(RMAP_NODE, &no_set_community_short_cmd);
	install_element(RMAP_NODE, &set_community_delete_cmd);
	install_element(RMAP_NODE, &no_set_community_delete_cmd);
	install_element(RMAP_NODE, &set_lcommunity_cmd);
	install_element(RMAP_NODE, &set_lcommunity_none_cmd);
	install_element(RMAP_NODE, &no_set_lcommunity_cmd);
	install_element(RMAP_NODE, &no_set_lcommunity1_cmd);
	install_element(RMAP_NODE, &no_set_lcommunity1_short_cmd);
	install_element(RMAP_NODE, &set_lcommunity_delete_cmd);
	install_element(RMAP_NODE, &no_set_lcommunity_delete_cmd);
	install_element(RMAP_NODE, &no_set_lcommunity_delete_short_cmd);
	install_element(RMAP_NODE, &set_ecommunity_rt_cmd);
	install_element(RMAP_NODE, &no_set_ecommunity_rt_cmd);
	install_element(RMAP_NODE, &no_set_ecommunity_rt_short_cmd);
	install_element(RMAP_NODE, &set_ecommunity_soo_cmd);
	install_element(RMAP_NODE, &no_set_ecommunity_soo_cmd);
	install_element(RMAP_NODE, &no_set_ecommunity_soo_short_cmd);
	install_element(RMAP_NODE, &set_ecommunity_lb_cmd);
	install_element(RMAP_NODE, &no_set_ecommunity_lb_cmd);
	install_element(RMAP_NODE, &no_set_ecommunity_lb_short_cmd);
	install_element(RMAP_NODE, &set_ecommunity_none_cmd);
	install_element(RMAP_NODE, &no_set_ecommunity_none_cmd);
	install_element(RMAP_NODE, &set_ecommunity_nt_cmd);
	install_element(RMAP_NODE, &no_set_ecommunity_nt_cmd);
	install_element(RMAP_NODE, &no_set_ecommunity_nt_short_cmd);
	install_element(RMAP_NODE, &set_ecommunity_color_cmd);
	install_element(RMAP_NODE, &no_set_ecommunity_color_cmd);
	install_element(RMAP_NODE, &no_set_ecommunity_color_all_cmd);
	install_element(RMAP_NODE, &set_ecommunity_delete_cmd);
	install_element(RMAP_NODE, &no_set_ecommunity_delete_cmd);
#ifdef KEEP_OLD_VPN_COMMANDS
	install_element(RMAP_NODE, &set_vpn_nexthop_cmd);
	install_element(RMAP_NODE, &no_set_vpn_nexthop_cmd);
#endif /* KEEP_OLD_VPN_COMMANDS */
	install_element(RMAP_NODE, &set_ipx_vpn_nexthop_cmd);
	install_element(RMAP_NODE, &no_set_ipx_vpn_nexthop_cmd);
	install_element(RMAP_NODE, &set_originator_id_cmd);
	install_element(RMAP_NODE, &no_set_originator_id_cmd);
	install_element(RMAP_NODE, &set_l3vpn_nexthop_encapsulation_cmd);

	route_map_install_match(&route_match_ipv6_address_cmd);
	route_map_install_match(&route_match_ipv6_next_hop_cmd);
	route_map_install_match(&route_match_ipv6_next_hop_address_cmd);
	route_map_install_match(&route_match_ipv6_next_hop_prefix_list_cmd);
	route_map_install_match(&route_match_ipv4_next_hop_cmd);
	route_map_install_match(&route_match_ipv6_address_prefix_list_cmd);
	route_map_install_match(&route_match_ipv6_next_hop_type_cmd);
	route_map_install_set(&route_set_ipv6_nexthop_global_cmd);
	route_map_install_set(&route_set_ipv6_nexthop_prefer_global_cmd);
	route_map_install_set(&route_set_ipv6_nexthop_local_cmd);
	route_map_install_set(&route_set_ipv6_nexthop_peer_cmd);
	route_map_install_match(&route_match_rpki_extcommunity_cmd);
	route_map_install_match(&route_match_vpn_dataplane_cmd);

	install_element(RMAP_NODE, &match_vpn_dataplane_cmd);
	install_element(RMAP_NODE, &match_ipv6_next_hop_address_cmd);
	install_element(RMAP_NODE, &no_match_ipv6_next_hop_address_cmd);
	install_element(RMAP_NODE, &match_ipv6_next_hop_old_cmd);
	install_element(RMAP_NODE, &no_match_ipv6_next_hop_old_cmd);
	install_element(RMAP_NODE, &match_ipv4_next_hop_cmd);
	install_element(RMAP_NODE, &no_match_ipv4_next_hop_cmd);
	install_element(RMAP_NODE, &set_ipv6_nexthop_global_cmd);
	install_element(RMAP_NODE, &no_set_ipv6_nexthop_global_cmd);
	install_element(RMAP_NODE, &set_ipv6_nexthop_prefer_global_cmd);
	install_element(RMAP_NODE, &no_set_ipv6_nexthop_prefer_global_cmd);
	install_element(RMAP_NODE, &set_ipv6_nexthop_peer_cmd);
	install_element(RMAP_NODE, &no_set_ipv6_nexthop_peer_cmd);
	install_element(RMAP_NODE, &match_rpki_extcommunity_cmd);
	install_element(RMAP_NODE, &match_source_protocol_cmd);
	install_element(RMAP_NODE, &no_match_source_protocol_cmd);
#ifdef HAVE_SCRIPTING
	install_element(RMAP_NODE, &match_script_cmd);
#endif
}

/* 【中文补充】bgp_route_map_terminate：BGP route-map 子系统退出清理入口。
 * 目前主要调 route_map_finish 反初始化通用 route-map 框架(注释标有待办：清理全部内存)。 */
void bgp_route_map_terminate(void)
{
	/* ToDo: Cleanup all the used memory */
	route_map_finish();
}
