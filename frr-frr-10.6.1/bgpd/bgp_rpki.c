// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BGP RPKI
 * Copyright (C) 2013 Michael Mester (m.mester@fu-berlin.de), for FU Berlin
 * Copyright (C) 2014-2017 Andreas Reuter (andreas.reuter@fu-berlin.de), for FU
 * Berlin
 * Copyright (C) 2016-2017 Colin Sames (colin.sames@haw-hamburg.de), for HAW
 * Hamburg
 * Copyright (C) 2017-2018 Marcel Röthke (marcel.roethke@haw-hamburg.de),
 * for HAW Hamburg
 */

/* ============================================================================
 * 【文件导读】bgp_rpki.c —— RPKI 源验证与 FRR 缓存服务器的对接
 * ============================================================================
 * 【RPKI 与 RTR 协议】RPKI 用签名保护的 ROA 数据库回答“某 AS 是否有权通告某前缀
 * （及允许的最大前缀长度）”。bgpd 自己不下载和校验 RPKI 数据，而是通过 **RTR
 * 协议（RFC 8210）** 从本机或远程的 rpki 缓存服务器（rpki 守护进程）取数据。本
 * 文件就是这条对接链路的全部实现，底层用 rtrlib 库（struct rtr_mgr 系列 /
 * struct tr 系列 / struct pfx_table）。
 *
 * 【总体结构】
 *   1) 缓存服务器配置：add_tcp_cache / add_ssh_cache / free_cache 管理 struct
 *      cache；每个 cache 对应一个传输套接字 tr_socket（TCP 或 SSH）和一个 RTR
 *      套接字 rtr_socket。
 *   2) 每 VRF 一个 rpki_vrf：持有 rtr_mgr_config（含一组按 preference 排序的
 *      cache 组）、cache_list，以及启动 / 停止 / 同步状态标志。
 *   3) 生命周期：start（初始化并启动 rtr_mgr）/ stop / reset；同步完成由
 *      sync_expired 定时器轮询 rtr_mgr_conf_in_sync 判定。
 *   4) 数据同步：rtrlib 的回调 rpki_update_cb_sync_rtr 把变更的 pfx_record 写进
 *      socketpair；bgpd 侧的 bgpd_sync_callback 读出来，转成 prefix 后触发
 *      revalidate（重新校验相关前缀）。
 *   5) 校验：rpki_validate_prefix 对一条路由算出 VALID / INVALID / NOTFOUND；
 *      经 hook bgp_rpki_prefix_status 暴露给选路与策略；route_match 让 route-map
 *      的 match rpki 能用它过滤。
 *   6) CLI：rpki 配置节点、cache 配置、周期参数、各种 show、debug，以及 route-map
 *      的 match rpki。
 *
 * 【连接状态机 / 版本协商（重点）】
 *   连接由 rtrlib 内部维护：tr_socket 建立传输连接后，RTR 层自行做协议版本协商并
 *   下发数据。对上层可见的状态是本文件里 rpki_vrf 的三个布尔量——
 *   rtr_is_running（已 start）、rtr_is_stopping（正在停）、rtr_is_synced（初次
 *   数据同步完成）。is_running / is_synchronized / is_stopping 就是它们的读取口。
 *   单个 cache 的 RTR 会话状态另看 rtr_socket->state == RTR_ESTABLISHED。
 *   注意：只有 is_synchronized 为真才认为可以对外提供校验——数据未同步前校验
 *   结果不可信，故各 show 函数在未同步时会提示“无连接”。
 *
 * 【校验结果如何影响选路】三态语义见 bgp_rpki.h（NOTFOUND 是“无覆盖”、INVALID
 *   是“有覆盖但不符”）。rpki_validate_prefix 把结果返回给调用方；重校验路径
 *   （revalidate 系列）在 ROA 变化时重新导出 / 更新相关前缀，从而让 outbound
 *   route-map 的 match rpki 重新评估，决定放行还是抑制。
 *
 * 【阅读建议】先看数据结构（struct cache / struct rpki_vrf）→ 生命周期
 *   （start / stop / reset）→ 同步（rpki_update_cb_sync_rtr / bgpd_sync_callback）
 *   → 校验（rpki_validate_prefix）→ 最后是 CLI 部分（细节可略读）。
 * ============================================================================
 */

/* If rtrlib compiled with ssh support, don`t fail build */
#define LIBSSH_LEGACY_0_4

#include <zebra.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include "prefix.h"
#include "log.h"
#include "command.h"
#include "linklist.h"
#include "memory.h"
#include "frrevent.h"
#include "filter.h"
#include "lib_errors.h"
#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgp_advertise.h"
#include "bgp_label.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_rpki.h"
#include "bgpd/bgp_debug.h"
#include "northbound_cli.h"

#include "bgpd/bgp_errors.h"
#include "lib/network.h"
#include "rtrlib/rtrlib.h"
#include "hook.h"
#include "libfrr.h"
#include "lib/version.h"

#include "bgpd/bgp_rpki_clippy.c"

/* 【内存类型】为 RPKI 各用途注册独立的 FRR 内存类型（便于调试内存统计）：
 * 中间缓冲、cache 节点、cache 组、rtrlib 分配器、重校验事件参数。 */
DEFINE_MTYPE_STATIC(BGPD, BGP_RPKI_TEMP, "BGP RPKI Intermediate Buffer");
DEFINE_MTYPE_STATIC(BGPD, BGP_RPKI_CACHE, "BGP RPKI Cache server");
DEFINE_MTYPE_STATIC(BGPD, BGP_RPKI_CACHE_GROUP, "BGP RPKI Cache server group");
DEFINE_MTYPE_STATIC(BGPD, BGP_RPKI_RTRLIB, "BGP RPKI RTRLib");
DEFINE_MTYPE_STATIC(BGPD, BGP_RPKI_REVALIDATE, "BGP RPKI Revalidation");

/* 【宏】分隔缓冲大小：配置回写时用于拼接前缀分隔符（空格等）的小缓冲长度。 */
#define STR_SEPARATOR 10

/* 【默认值】RPKI 三个时间参数的缺省值（秒）：
 *   POLLING_PERIOD_DEFAULT    : 轮询周期，定期向缓存服务器询问数据版本。
 *   EXPIRE_INTERVAL_DEFAULT   : 数据过期时限，超过则视为陈旧。
 *   RETRY_INTERVAL_DEFAULT    : 连接失败后的重试间隔。
 *   BGP_RPKI_CACHE_SERVER_SYNC_RETRY_TIMEOUT : 判定“初次同步是否完成”的重试超时，
 *   sync_expired 用它调度下一次检查。 */
#define POLLING_PERIOD_DEFAULT 3600
#define EXPIRE_INTERVAL_DEFAULT 7200
#define RETRY_INTERVAL_DEFAULT 600
#define BGP_RPKI_CACHE_SERVER_SYNC_RETRY_TIMEOUT 3

/* 【宏】RPKI_DEBUG —— 仅当配置模式调试（rpki_debug_conf）或终端调试
 * （rpki_debug_term）打开时才输出调试日志，消息统一加 "RPKI: " 前缀。
 * 注意：用法与 zlog_debug 相同，可变参数；关闭调试时为无输出（但仍会求值条件）。 */
#define RPKI_DEBUG(...)                                                        \
	if (rpki_debug_conf || rpki_debug_term) {                              \
		zlog_debug("RPKI: " __VA_ARGS__);                              \
	}

/* 【宏】多个 RPKI 命令共用的帮助文本串（避免各命令重复书写同一句）。 */
#define RPKI_OUTPUT_STRING "Control rpki specific settings\n"

/* 【结构】struct cache —— 一台上游 RPKI 缓存服务器（一条 rtr_mgr 组里的一个端点）。
 *   type        : 传输类型，TCP 或（编译期支持 SSH 时）SSH，决定 tr_config 用哪个
 *                 联合成员。枚举是匿名的，取值 TCP / SSH。
 *   tr_socket   : rtrlib 的**传输层**套接字（负责建连、收发字节）。
 *   tr_config   : 联合体，TCP 存 tr_tcp_config（host / port / bindaddr），SSH 存
 *                 tr_ssh_config。取用前必须判 type，否则读到错误成员。
 *   rtr_socket  : rtrlib 的**RTR 协议层**套接字，绑定在 tr_socket 之上，其 state
 *                 即 RTR_ESTABLISHED 时表示会话已建立。
 *   preference  : 优先级（1-255），越小越优先；find_cache / rtr_mgr 用它做唯一键，
 *                 所以同一 rpki_vrf 里 preference 不可重复。
 *   rpki_vrf    : 回指所属的 RPKI VRF 上下文。 */
struct cache {
	enum {
		TCP,
#if defined(FOUND_SSH)
		SSH
#endif
	} type;
	struct tr_socket *tr_socket;
	union {
		struct tr_tcp_config *tcp_config;
		struct tr_ssh_config *ssh_config;
	} tr_config;
	struct rtr_socket *rtr_socket;
	uint8_t preference;
	struct rpki_vrf *rpki_vrf;
};

/* 【枚举】本文件内部通用的成功 / 失败返回码，0 成功、-1 失败。 */
enum return_values { SUCCESS = 0, ERROR = -1 };

/* 【结构】struct rpki_for_each_record_arg —— 遍历 pfx_table 时传给回调的上下文。
 *   vty / json / asnotation : 输出目标与 AS 号显示格式。
 *   prefix_amount           : 计数出参（回调每处理一条就 ++，实现按 as 过滤计数）。
 *   as                      : 仅 print_record_by_asn 用，过滤出指定 AS 的记录。
 * 同一个结构被“打印回调”和“计数回调”共用，所以字段是超集。 */
struct rpki_for_each_record_arg {
	struct vty *vty;
	unsigned int *prefix_amount;
	as_t as;
	json_object *json;
	enum asnotation_mode asnotation;
};

/* 【结构】struct rpki_vrf —— 一个 VRF 下的 RPKI 运行上下文（每个 VRF 一份）。
 *   rtr_config       : rtrlib 的会话管理器配置（由 rtr_mgr_init 建立、rtr_mgr_start
 *                      启动），内部持有按 preference 排序的缓存组。
 *   cache_list       : 该 VRF 下配置的 cache 列表（元素为 struct cache）。
 *   rtr_is_running   : 已 start（rtr_mgr_start 成功）。
 *   rtr_is_stopping  : 正在停（stop 置位，回调据此前置判断避免写入）。
 *   rtr_is_synced    : 初次数据同步已完成（sync_expired 判定后置真），只有它为真
 *                      才对外提供校验结果。
 *   rtr_update_overflow : 原子标志；socketpair 写满（EAGAIN）时置位，提示 bgpd 侧
 *                      要吞掉积压数据后整体重校验。
 *   polling_period / expire_interval / retry_interval : 三个时间参数。
 *   rpki_sync_socket_rtr / _bgpd : socketpair 的两端，RTR 回调写一端、bgpd 读另一端。
 *   vrfname          : VRF 名（默认 VRF 为 NULL）。
 *   t_rpki_sync      : sync_expired 的定时器事件句柄。
 * 注意 QOBJ_FIELDS 是 FRR 对象管理宏，提供引用计数与类型标识。 */
struct rpki_vrf {
	struct rtr_mgr_config *rtr_config;
	struct list *cache_list;
	bool rtr_is_running;
	bool rtr_is_stopping;
	bool rtr_is_synced;
	_Atomic int rtr_update_overflow;
	unsigned int polling_period;
	unsigned int expire_interval;
	unsigned int retry_interval;
	int rpki_sync_socket_rtr;
	int rpki_sync_socket_bgpd;
	char *vrfname;
	struct event *t_rpki_sync;

	QOBJ_FIELDS;
};

/* 【线程键】用于标记“某个 pthread 已接入 FRR 的 rcu/pthread 框架”，避免 rpki
 * 基础设施对同一线程重复注册（重复注册会导致关闭时 rcu 断言）。见
 * rpki_create_socket 里的用法。 */
static pthread_key_t rpki_pthread;

/* 【前置声明】下面这一批是本文件内部函数的前向声明，仅为满足定义顺序（先被调用
 * 后定义）而存在，无独立语义。集中在此，具体含义见各自定义处的注释。 */
static struct rpki_vrf *find_rpki_vrf(const char *vrfname);
static int bgp_rpki_vrf_update(struct vrf *vrf, bool enabled);
static int bgp_rpki_write_vrf(struct vty *vty, struct vrf *vrf);
static int bgp_rpki_hook_write_vrf(struct vty *vty, struct vrf *vrf);
static int bgp_rpki_write_debug(struct vty *vty, bool running);
static int start(struct rpki_vrf *rpki_vrf);
static void stop(struct rpki_vrf *rpki_vrf);
static int reset(bool force, struct rpki_vrf *rpki_vrf);
static struct rtr_mgr_group *get_connected_group(struct rpki_vrf *rpki_vrf);
static void print_prefix_table(struct vty *vty, struct rpki_vrf *rpki_vrf,
			       json_object *json, bool count_only);
static void install_cli_commands(void);
static int config_write(struct vty *vty);
static int config_on_exit(struct vty *vty);
static void free_cache(struct cache *cache);
static struct rtr_mgr_group *get_groups(struct list *cache_list);
#if defined(FOUND_SSH)
static int add_ssh_cache(struct rpki_vrf *rpki_vrf, const char *host,
			 const unsigned int port, const char *username,
			 const char *client_privkey_path,
			 const char *server_pubkey_path,
			 const uint8_t preference, const char *bindaddr);
#endif
static struct rtr_socket *create_rtr_socket(struct tr_socket *tr_socket);
static struct cache *find_cache(const uint8_t preference,
				struct list *cache_list);
static void rpki_delete_all_cache_nodes(struct rpki_vrf *rpki_vrf);
static int add_tcp_cache(struct rpki_vrf *rpki_vrf, const char *host,
			 const char *port, const uint8_t preference,
			 const char *bindaddr);
static void print_record(const struct pfx_record *record, struct vty *vty,
			 json_object *json, enum asnotation_mode asnotation);
static bool is_synchronized(struct rpki_vrf *rpki_vrf);
static bool is_running(struct rpki_vrf *rpki_vrf);
static bool is_stopping(struct rpki_vrf *rpki_vrf);
static void route_match_free(void *rule);
static enum route_map_cmd_result_t route_match(void *rule,
					       const struct prefix *prefix,

					       void *object);
static void *route_match_compile(const char *arg);
static void revalidate_bgp_node(struct bgp *bgp, struct bgp_dest *dest, afi_t afi, safi_t safi);
static struct rpki_vrf *get_rpki_vrf(const char *vrfname);

/* 【调试开关】rpki_debug_conf 是配置模式的持久调试（debug rpki 写入配置），
 * rpki_debug_term 是终端临时调试；两者任一为真 RPKI_DEBUG 才有输出。 */
static bool rpki_debug_conf, rpki_debug_term;

DECLARE_QOBJ_TYPE(rpki_vrf);
DEFINE_QOBJ_TYPE(rpki_vrf);

/* 【全局】所有 rpki_vrf 的列表（每个 VRF 一个元素），进程内唯一。 */
struct list *rpki_vrf_list;

/* 【命令节点】全局 rpki 配置节点（config-rpki），父节点是 CONFIG_NODE；
 * config_write 负责回写配置，node_exit 在退出时调用 config_on_exit 触发 reset。 */
static struct cmd_node rpki_node = {
	.name = "rpki",
	.node = RPKI_NODE,
	.parent_node = CONFIG_NODE,
	.prompt = "%s(config-rpki)# ",
	.config_write = config_write,
	.node_exit = config_on_exit,
};

/* 【命令节点】VRF 下的 rpki 配置节点（config-vrf-rpki），父节点是 VRF_NODE；
 * config_write 为 NULL（VRF 配置由父节点统一回写），node_exit 同样调 config_on_exit。 */
static struct cmd_node rpki_vrf_node = {
	.name = "rpki",
	.node = RPKI_VRF_NODE,
	.parent_node = VRF_NODE,
	.prompt = "%s(config-vrf-rpki)# ",
	.config_write = NULL,
	.node_exit = config_on_exit,
};

/* 【route-map 规则】把 "rpki" 这个 match 条件注册进 route-map 框架：
 * route_match 做实际比对，route_match_compile 编译参数，route_match_free 释放。 */
static const struct route_map_rule_cmd route_match_rpki_cmd = {
	"rpki", route_match, route_match_compile, route_match_free};

/* 【适配】rtrlib 内存分配三件套的包装：统一走 FRR 的 MTYPE_BGP_RPKI_RTRLIB，
 * 由 frr_pthread/lrtr 在启动时注册（见 bgp_rpki_module_init 里的
 * lrtr_set_alloc_functions），使 rtrlib 的内存也纳入 FRR 统计。 */
static void *malloc_wrapper(size_t size)
{
	return XMALLOC(MTYPE_BGP_RPKI_RTRLIB, size);
}

/* 【适配】上面 malloc_wrapper 的 realloc 版本（同一 MTYPE）。 */
static void *realloc_wrapper(void *ptr, size_t size)
{
	return XREALLOC(MTYPE_BGP_RPKI_RTRLIB, ptr, size);
}

/* 【适配】malloc_wrapper 对应的释放函数（同一 MTYPE）。 */
static void free_wrapper(void *ptr)
{
	XFREE(MTYPE_BGP_RPKI_RTRLIB, ptr);
}

/* 【初始化】按 cache->type 初始化 tr_socket：TCP 走 tr_tcp_init，SSH（编译期支持
 * 时）走 tr_ssh_init。必须在把该 cache 交给 rtr_mgr 之前调用。 */
static void init_tr_socket(struct cache *cache)
{
	if (cache->type == TCP)
		tr_tcp_init(cache->tr_config.tcp_config,
			    cache->tr_socket);
#if defined(FOUND_SSH)
	else
		tr_ssh_init(cache->tr_config.ssh_config,
			    cache->tr_socket);
#endif
}

/* 【释放】名字叫 free，但实现与 init_tr_socket 完全相同（都是再次调用 tr_tcp_init /
 * tr_ssh_init）——这看起来是复制粘贴遗留、并非真正的释放逻辑，读代码时不要被名字
 * 误导；真正的套接字释放发生在 rtrlib 内部与 free_cache 里。 */
static void free_tr_socket(struct cache *cache)
{
	if (cache->type == TCP)
		tr_tcp_init(cache->tr_config.tcp_config,
			    cache->tr_socket);
#if defined(FOUND_SSH)
	else
		tr_ssh_init(cache->tr_config.ssh_config,
			    cache->tr_socket);
#endif
}

static int rpki_validate_prefix(struct peer *peer, struct attr *attr,
				const struct prefix *prefix);

/* 【工具】把 IPv6 地址的 4 个 32 位字由主机序转成网络序（大端），因为 rtrlib 的
 * LRTR_IPV6 地址内部按网络序存放，而 FRR 前缀里存主机序。 */
static void ipv6_addr_to_network_byte_order(const uint32_t *src, uint32_t *dest)
{
	int i;

	for (i = 0; i < 4; i++)
		dest[i] = htonl(src[i]);
}

/* 【工具】上者的逆操作：网络序转主机序，用于把 FRR 前缀送进 rtrlib 做校验。 */
static void ipv6_addr_to_host_byte_order(const uint32_t *src, uint32_t *dest)
{
	int i;

	for (i = 0; i < 4; i++)
		dest[i] = ntohl(src[i]);
}

/* 【route-map 比对】route_match —— 实现 `match rpki <valid|invalid|notfound>`。
 * rule 是编译期保存的目标状态（int *，见 route_match_compile）；object 是被匹配
 * 的 bgp_path_info。对路径实时做一次 RPKI 校验（rpki_validate_prefix），与目标
 * 状态相等则返回 RMAP_MATCH，否则 RMAP_NOMATCH。
 * 坑：每次匹配都会**实时重算**校验结果（不是读缓存的 rpki 字段），开销与调用频率
 * 相关；rpki_validate_prefix 在功能未启用时会返回 RPKI_NOT_BEING_USED。 */
static enum route_map_cmd_result_t route_match(void *rule,
					       const struct prefix *prefix,
					       void *object)
{
	int *rpki_status = rule;
	struct bgp_path_info *path;

	path = object;

	if (rpki_validate_prefix(path->peer, path->attr, prefix)
	    == *rpki_status) {
		return RMAP_MATCH;
	}

	return RMAP_NOMATCH;
}

/* 【编译】route_match_compile —— 把 match 关键字（valid / invalid / notfound）翻
 * 译成 enum rpki_states 值，存入新分配的 int 作为 route_map rule。分配用
 * MTYPE_ROUTE_MAP_COMPILED，释放见 route_match_free；非三者之一时默认 NOTFOUND。 */
static void *route_match_compile(const char *arg)
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

/* 【释放】route_match_free —— 释放 route_match_compile 分配的 rule。 */
static void route_match_free(void *rule)
{
	XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* 【创建】create_rtr_socket —— 分配一个 rtr_socket 并绑定给定 tr_socket，返回它。
 * 分配走 MTYPE_BGP_RPKI_CACHE（XMALLOC 失败会直接中止）。 */
static struct rtr_socket *create_rtr_socket(struct tr_socket *tr_socket)
{
	struct rtr_socket *rtr_socket =
		XMALLOC(MTYPE_BGP_RPKI_CACHE, sizeof(struct rtr_socket));
	rtr_socket->tr_socket = tr_socket;
	return rtr_socket;
}

/* 【hook 回调】bgp_rpki_vrf_update —— VRF 启用 / 停用时（bgp_hook_vrf_update）
 * 启动或停止该 VRF 的 RPKI。默认 VRF 取 vrfname 为 NULL 的 rpki_vrf，其余按名查。
 * 找不到对应的 rpki_vrf 返回 0（该 VRF 未配置 RPKI），否则 start/stop 并返回 1。 */
static int bgp_rpki_vrf_update(struct vrf *vrf, bool enabled)
{
	struct rpki_vrf *rpki;

	if (vrf->vrf_id == VRF_DEFAULT)
		rpki = find_rpki_vrf(NULL);
	else
		rpki = find_rpki_vrf(vrf->name);
	if (!rpki)
		return 0;

	if (enabled)
		start(rpki);
	else
		stop(rpki);
	return 1;
}

/* tcp identifier : <HOST>:<PORT>
 * ssh identifier : <user>@<HOST>:<PORT>
 */
/* 【查找】find_rpki_vrf_from_ident —— 由 rtrlib 给出的连接标识 ident 反查它属于哪个
 * rpki_vrf。ident 形如 TCP 的 <HOST>:<PORT> 或 SSH 的 <user>@<HOST>:<PORT>
 * （见上方英文注释）。逻辑：取最后一个 ':' 后为端口、其前为 host；有 '@' 则视为
 * SSH。然后在所有 rpki_vrf 的所有 cache 里比对类型与 host/port，命中即返回所属
 * rpki_vrf。返回 NULL 表示没有匹配（或不含 ':' 的标识）。
 * 坑：host_len 用 strrchr 的指针差算出，SSH 分支会在 '@' 处截断用户名；临时缓冲
 * 用完必须释放（有 XFREE 分支）。 */
static struct rpki_vrf *find_rpki_vrf_from_ident(const char *ident)
{
#if defined(FOUND_SSH)
	struct tr_ssh_config *ssh_config;
#endif
	struct tr_tcp_config *tcp_config;
	struct listnode *rpki_vrf_nnode;
	unsigned int cache_port, port;
	struct listnode *cache_node;
	struct rpki_vrf *rpki_vrf;
	struct cache *cache;
	bool is_tcp = true;
	size_t host_len;
	char *endptr;
	char *host;
	char *ptr;
	char *buf;

	/* extract the <SOCKET> */
	ptr = strrchr(ident, ':');
	if (!ptr)
		return NULL;

	ptr++;
	/* extract port */
	port = atoi(ptr);
	if (port == 0)
		/* not ours */
		return NULL;

	/* extract host */
	ptr--;
	host_len = (size_t)(ptr - ident);
	buf = XCALLOC(MTYPE_BGP_RPKI_TEMP, host_len + 1);
	memcpy(buf, ident, host_len);
	buf[host_len] = '\0';
	endptr = strrchr(buf, '@');

	/* ssh session */
	if (endptr) {
		host = XCALLOC(MTYPE_BGP_RPKI_TEMP,
			       (size_t)(buf + host_len - endptr) + 1);
		memcpy(host, endptr + 1, (size_t)(buf + host_len - endptr) + 1);
		is_tcp = false;
	} else {
		host = buf;
		buf = NULL;
	}

	for (ALL_LIST_ELEMENTS_RO(rpki_vrf_list, rpki_vrf_nnode, rpki_vrf)) {
		for (ALL_LIST_ELEMENTS_RO(rpki_vrf->cache_list, cache_node,
					  cache)) {
			if ((cache->type == TCP && !is_tcp)
#if defined(FOUND_SSH)
			    || (cache->type == SSH && is_tcp)
#endif
			)
				continue;

			if (is_tcp) {
				tcp_config = cache->tr_config.tcp_config;
				cache_port = atoi(tcp_config->port);
				if (cache_port != port)
					continue;
				if (strlen(tcp_config->host) != strlen(host))
					continue;
				if (memcmp(tcp_config->host, host, host_len) ==
				    0)
					break;
			}
#if defined(FOUND_SSH)
			else {
				ssh_config = cache->tr_config.ssh_config;
				if (port != ssh_config->port)
					continue;
				if (strmatch(ssh_config->host, host))
					break;
			}
#endif
		}
		if (cache)
			break;
	}
	if (host)
		XFREE(MTYPE_BGP_RPKI_TEMP, host);
	if (buf)
		XFREE(MTYPE_BGP_RPKI_TEMP, buf);
	return rpki_vrf;
}

/* 【查找】find_rpki_vrf —— 在 rpki_vrf_list 里按 VRF 名线性查找。
 * vrfname 为 NULL 表示默认 VRF（匹配 vrfname 也为 NULL 的那个）。返回匹配的
 * rpki_vrf 或 NULL。O(n) 线性扫描，列表通常很小。 */
static struct rpki_vrf *find_rpki_vrf(const char *vrfname)
{
	struct listnode *rpki_vrf_nnode;
	struct rpki_vrf *rpki_vrf;

	for (ALL_LIST_ELEMENTS_RO(rpki_vrf_list, rpki_vrf_nnode, rpki_vrf)) {
		if (!vrfname && !rpki_vrf->vrfname)
			/* rpki_vrf struct of the default VRF */
			return rpki_vrf;
		if (vrfname && rpki_vrf->vrfname &&
		    strmatch(vrfname, rpki_vrf->vrfname))
			return rpki_vrf;
	}
	return NULL;
}

/* 【查找】find_cache —— 在 cache_list 里按 preference 线性查找 cache。preference
 * 在一个 rpki_vrf 内唯一（配置时校验），所以可作键。cache_list 为空返回 NULL。 */
static struct cache *find_cache(const uint8_t preference,
				struct list *cache_list)
{
	struct listnode *cache_node;
	struct cache *cache;

	if (!cache_list)
		return NULL;

	for (ALL_LIST_ELEMENTS_RO(cache_list, cache_node, cache)) {
		if (cache->preference == preference)
			return cache;
	}
	return NULL;
}

/* 【清理】rpki_delete_all_cache_nodes —— 清空某 rpki_vrf 的全部 cache。
 * 若 rpki 正在运行，先对每个 cache 调用 rtr_mgr_remove_group（按 preference 从
 * rtr_mgr 移除组），再从 cache_list 删除节点（list 的 del 回调 free_cache 会释放）。
 * 注意本函数只清节点，不 stop rtr_mgr 本身（调用方负责，例如 no rpki 命令）。 */
static void rpki_delete_all_cache_nodes(struct rpki_vrf *rpki_vrf)
{
	struct listnode *cache_node, *cache_next;
	struct cache *cache;

	for (ALL_LIST_ELEMENTS(rpki_vrf->cache_list, cache_node, cache_next,
			       cache)) {
		if (is_running(rpki_vrf))
			rtr_mgr_remove_group(rpki_vrf->rtr_config,
					     cache->preference);
		listnode_delete(rpki_vrf->cache_list, cache);
	}
}

/* 【打印】print_record —— 输出单条 ROA 记录：前缀、允许的最小 / 最大前缀长度、
 * 源 ASN。json 非空时写入一个 JSON 对象（prefix / prefixLenMin / prefixLenMax /
 * asn）并挂到数组；否则按文本列打印。ASN 显示格式由 asnotation 决定（asplain /
 * asdot）。无返回值。 */
static void print_record(const struct pfx_record *record, struct vty *vty,
			 json_object *json, enum asnotation_mode asnotation)
{
	char ip[INET6_ADDRSTRLEN];
	json_object *json_record = NULL;

	lrtr_ip_addr_to_str(&record->prefix, ip, sizeof(ip));

	if (!json) {
		vty_out(vty, "%-40s   %3u - %3u   ", ip, record->min_len,
			record->max_len);
		vty_out(vty, ASN_FORMAT(asnotation), (as_t *)&record->asn);
		vty_out(vty, "\n");
	} else {
		json_record = json_object_new_object();
		json_object_string_add(json_record, "prefix", ip);
		json_object_int_add(json_record, "prefixLenMin",
				    record->min_len);
		json_object_int_add(json_record, "prefixLenMax",
				    record->max_len);
		asn_asn2json(json_record, "asn", record->asn, asnotation);
		json_object_array_add(json, json_record);
	}
}

/* 【回调】print_record_by_asn —— pfx_table 遍历回调：只打印 asn 等于 arg->as 的
 * 记录，并累加计数。用于 `show rpki as-number`。 */
static void print_record_by_asn(const struct pfx_record *record, void *data)
{
	struct rpki_for_each_record_arg *arg = data;
	struct vty *vty = arg->vty;

	if (record->asn == arg->as) {
		(*arg->prefix_amount)++;
		print_record(record, vty, arg->json, arg->asnotation);
	}
}

/* 【回调】print_record_cb —— pfx_table 遍历回调：对每条记录计数并打印。 */
static void print_record_cb(const struct pfx_record *record, void *data)
{
	struct rpki_for_each_record_arg *arg = data;
	struct vty *vty = arg->vty;

	(*arg->prefix_amount)++;

	print_record(record, vty, arg->json, arg->asnotation);
}

/* 【回调】count_record_cb —— pfx_table 遍历回调：只计数、不打印（供 prefix-count
 * 使用）。 */
static void count_record_cb(const struct pfx_record *record, void *data)
{
	struct rpki_for_each_record_arg *arg = data;

	(*arg->prefix_amount)++;
}

/* 【构造】get_groups —— 把 cache_list 转成 rtr_mgr 需要的组数组：每个 cache 一个
 * 组（sockets 指向该 cache 的 rtr_socket，sockets_len=1，preference 照搬）。
 * 分配前先 init_tr_socket。返回数组（调用方负责 XFREE）或 NULL（无 cache）。
 * 坑：数组按 MTYPE_BGP_RPKI_CACHE_GROUP 分配，大小是 listcount * sizeof(group)，
 * 与 cache_list 顺序一一对应。 */
static struct rtr_mgr_group *get_groups(struct list *cache_list)
{
	struct listnode *cache_node;
	struct rtr_mgr_group *rtr_mgr_groups;
	struct cache *cache;

	int group_count = listcount(cache_list);

	if (group_count == 0)
		return NULL;

	rtr_mgr_groups = XMALLOC(MTYPE_BGP_RPKI_CACHE_GROUP,
				 group_count * sizeof(struct rtr_mgr_group));

	size_t i = 0;

	for (ALL_LIST_ELEMENTS_RO(cache_list, cache_node, cache)) {
		rtr_mgr_groups[i].sockets = &cache->rtr_socket;
		rtr_mgr_groups[i].sockets_len = 1;
		rtr_mgr_groups[i].preference = cache->preference;

		init_tr_socket(cache);

		i++;
	}

	return rtr_mgr_groups;
}

/* 【状态】is_synchronized —— 仅在 rpki 正在运行且 rtr_is_synced 为真时返回 true，
 * 否则 false。这是“数据可用、校验结果可信”的判据。 */
inline bool is_synchronized(struct rpki_vrf *rpki_vrf)
{
	if (is_running(rpki_vrf))
		return rpki_vrf->rtr_is_synced;
	else
		return false;
}

/* 【状态】is_running —— 返回 rtr_is_running（是否已 start）。 */
inline bool is_running(struct rpki_vrf *rpki_vrf)
{
	return rpki_vrf->rtr_is_running;
}

/* 【状态】is_stopping —— 返回 rtr_is_stopping（是否正在停止）。 */
inline bool is_stopping(struct rpki_vrf *rpki_vrf)
{
	return rpki_vrf->rtr_is_stopping;
}

/* 【hook 回调】bgp_rpki_is_connected —— 供 bgp_rpki_connection_status 钩子判断某
 * VRF 的 RPKI 是否可用：要求 rpki_vrf 存在、is_running、is_synchronized、能取到已
 * 连接组，且至少有一个 cache 的 rtr_socket 状态是 RTR_ESTABLISHED。返回 1/0。 */
static int bgp_rpki_is_connected(const char *vrf_name)
{
	struct rtr_mgr_group *group;
	struct listnode *cache_node;
	struct cache *cache;
	struct rpki_vrf *rpki_vrf;

	rpki_vrf = get_rpki_vrf(vrf_name);
	if (!rpki_vrf)
		return 0;

	if (!is_running(rpki_vrf))
		return 0;

	if (!is_synchronized(rpki_vrf))
		return 0;

	group = get_connected_group(rpki_vrf);
	if (!group)
		return 0;

	for (ALL_LIST_ELEMENTS_RO(rpki_vrf->cache_list, cache_node, cache)) {
		if (cache->rtr_socket->state == RTR_ESTABLISHED)
			return 1;
	}

	return 0;
}

/* 【转换】pfx_record_to_prefix —— 把 rtrlib 的 pfx_record 转成 FRR 的 prefix：
 * prefixlen 取 record->min_len；按 record->prefix.ver 决定 family——LRTR_IPV4 时把
 * addr4 经 htonl 写入 prefix4（FRR 侧存网络序），LRTR_IPV6 时用
 * ipv6_addr_to_network_byte_order 写 prefix6。
 * 坑：v4 地址这里做了 htonl，而 ip_addr_prefix 方向（rpki_validate_prefix）用的是
 * ntohl，是相反的两段转换，别混。无返回值。 */
static void pfx_record_to_prefix(struct pfx_record *record,
				 struct prefix *prefix)
{
	prefix->prefixlen = record->min_len;

	if (record->prefix.ver == LRTR_IPV4) {
		prefix->family = AF_INET;
		prefix->u.prefix4.s_addr = htonl(record->prefix.u.addr4.addr);
	} else {
		prefix->family = AF_INET6;
		ipv6_addr_to_network_byte_order(record->prefix.u.addr6.addr,
						prefix->u.prefix6.s6_addr32);
	}
}

/* 【结构】struct rpki_revalidate_prefix —— 重校验事件的参数载体：把“要在哪个 bgp
 * 的哪个 afi/safi 表上、对哪个 prefix 做重校验”打包，投递给 rpki_revalidate_prefix
 * 事件。bgp 是目标实例，prefix/afi/safi 共同定位表与子树。 */
struct rpki_revalidate_prefix {
	struct bgp *bgp;
	struct prefix prefix;
	afi_t afi;
	safi_t safi;
};

/* 【事件】rpki_revalidate_prefix —— 定时 / 队列化的重校验：取出参数里的 bgp 与
 * prefix，在其 rib[afi][safi] 上做子树查找，遍历该子树下所有节点，对含路径信息的
 * 节点调用 revalidate_bgp_node。完成后释放事件参数 rrp。
 * 坑：这是异步事件，参数 rrp 由 revalidate_single_prefix 分配，这里必须释放。 */
static void rpki_revalidate_prefix(struct event *event)
{
	struct rpki_revalidate_prefix *rrp = EVENT_ARG(event);
	struct bgp_dest *match, *node;

	match = bgp_table_subtree_lookup(rrp->bgp->rib[rrp->afi][rrp->safi],
					 &rrp->prefix);

	node = match;

	while (node) {
		if (bgp_dest_has_bgp_path_info_data(node)) {
			revalidate_bgp_node(rrp->bgp, node, rrp->afi, rrp->safi);
		}

		node = bgp_route_next_until(node, match);
	}

	XFREE(MTYPE_BGP_RPKI_REVALIDATE, rrp);
}

/* 【调度】revalidate_single_prefix —— 对某个前缀，在匹配 VRF 的所有 bgp 实例、
 * 所有 SAFI 的表上安排一次重校验：vrf 为 NULL 时只处理默认 VRF 的 bgp，否则只处理
 * 该 vrf 的 bgp。为每个 (bgp, safi) 分配 rpki_revalidate_prefix 参数并投递到
 * bgp->t_revalidate[afi][safi] 事件。无返回值。
 * 坑：只为**已存在**的表（table 非空）投递；参数由事件处理函数负责释放。 */
static void revalidate_single_prefix(struct vrf *vrf, struct prefix prefix, afi_t afi)
{
	struct bgp *bgp;
	struct listnode *node;

	for (ALL_LIST_ELEMENTS_RO(bm->bgp, node, bgp)) {
		safi_t safi;

		if (!vrf && bgp->vrf_id != VRF_DEFAULT)
			continue;
		if (vrf && bgp->vrf_id != vrf->vrf_id)
			continue;

		for (safi = SAFI_UNICAST; safi < SAFI_MAX; safi++) {
			struct bgp_table *table = bgp->rib[afi][safi];
			struct rpki_revalidate_prefix *rrp;

			if (!table)
				continue;

			rrp = XCALLOC(MTYPE_BGP_RPKI_REVALIDATE, sizeof(*rrp));
			rrp->bgp = bgp;
			rrp->prefix = prefix;
			rrp->afi = afi;
			rrp->safi = safi;
			event_add_event(bm->master, rpki_revalidate_prefix, rrp,
					0, &bgp->t_revalidate[afi][safi]);
		}
	}
}

/* 【同步入口】bgpd_sync_callback —— bgpd 侧读 socketpair 的事件处理函数（由
 * rpki_init_sync_socket 注册）。进入后先重新 arm 读事件（保证持续监听）。
 * 两种路径：
 *   1) 若 set 了 rtr_update_overflow（RTR 侧曾写满）：循环把所有积压的 pfx_record
 *      读完、逐条 revalidate，然后清标志并返回（避免逐条触发、整体重算）；
 *   2) 否则只读一条记录并 revalidate。读到的记录经 pfx_record_to_prefix 转成
 *      prefix，地址族由 rec.prefix.ver 决定 AFI。
 * 无返回值。坑：非默认 VRF 时要先按名查到 vrf，查不到记错误日志返回。 */
static void bgpd_sync_callback(struct event *event)
{
	struct prefix prefix;
	struct pfx_record rec;
	struct rpki_vrf *rpki_vrf = EVENT_ARG(event);
	struct vrf *vrf = NULL;
	afi_t afi;
	int retval;

	event_add_read(bm->master, bgpd_sync_callback, rpki_vrf, rpki_vrf->rpki_sync_socket_bgpd,
		       NULL);

	if (rpki_vrf->vrfname) {
		vrf = vrf_lookup_by_name(rpki_vrf->vrfname);
		if (!vrf) {
			flog_err(EC_BGP_VRF_NOT_FOUND, "%s(): vrf for rpki %s not found", __func__,
				 rpki_vrf->vrfname);
			return;
		}
	}

	if (atomic_load_explicit(&rpki_vrf->rtr_update_overflow, memory_order_seq_cst)) {
		ssize_t size = 0;

		retval = read(rpki_vrf->rpki_sync_socket_bgpd, &rec, sizeof(struct pfx_record));
		while (retval != -1) {
			if (retval != sizeof(struct pfx_record))
				break;

			size += retval;
			pfx_record_to_prefix(&rec, &prefix);
			afi = (rec.prefix.ver == LRTR_IPV4) ? AFI_IP : AFI_IP6;
			revalidate_single_prefix(vrf, prefix, afi);

			retval = read(rpki_vrf->rpki_sync_socket_bgpd, &rec,
				      sizeof(struct pfx_record));
		}

		RPKI_DEBUG("Socket overflow detected (%zu), revalidating affected prefixes", size);

		atomic_store_explicit(&rpki_vrf->rtr_update_overflow, 0, memory_order_seq_cst);
		return;
	}

	retval = read(rpki_vrf->rpki_sync_socket_bgpd, &rec, sizeof(struct pfx_record));
	if (retval != sizeof(struct pfx_record)) {
		RPKI_DEBUG("Could not read from rpki_sync_socket_bgpd");
		return;
	}
	pfx_record_to_prefix(&rec, &prefix);

	afi = (rec.prefix.ver == LRTR_IPV4) ? AFI_IP : AFI_IP6;

	revalidate_single_prefix(vrf, prefix, afi);
}

/* 【重校验】revalidate_bgp_node —— 对某个 bgp_dest 上的路径重新跑一遍导入处理，
 * 使 RPKI 状态被重新计算。两步：
 *   1) 遍历 adj_in（从邻居学来的路径），逐条调用 bgp_update 重新导入（带着原有的
 *      addpath_rx_id / 标签信息），从而让入向 route-map 重新评估 rpki 状态；
 *   2) 对本机起源的路由（peer_self 且 type 为 BGP 的 static/aggregate，它们没有
 *      adj_in 条目）置 BGP_PATH_ATTR_CHANGED 并 bgp_process，强制重新导出，让**出向**
 *      route-map 的 match rpki 重新评估——上面那段英文注释就是在解释这一步的必要性
 *      （否则本机路由的 RPKI 失效不会被出向策略感知）。 */
static void revalidate_bgp_node(struct bgp *bgp, struct bgp_dest *bgp_dest, afi_t afi, safi_t safi)
{
	struct bgp_adj_in *ain;
	struct bgp_path_info *bpi;
	mpls_label_t *label;
	uint8_t num_labels;

	for (ain = bgp_dest->adj_in; ain; ain = ain->next) {
		struct bgp_path_info *path = bgp_dest_get_bgp_path_info(bgp_dest);

		num_labels = BGP_PATH_INFO_NUM_LABELS(path);
		label = num_labels ? path->extra->labels->label : NULL;

		(void)bgp_update(ain->peer, bgp_dest_get_prefix(bgp_dest), ain->addpath_rx_id,
				 ain->attr, afi, safi, ZEBRA_ROUTE_BGP, BGP_ROUTE_NORMAL, NULL,
				 label, num_labels, 1, NULL);
	}

	/* Locally originated routes (e.g., 'network' statement) have no adj_in
	 * entry, so bgp_update() above won't cover them.  Force re-advertisement
	 * so the outbound route-map re-evaluates the updated RPKI state.
	 * If we have something like:
	 * route-map out deny 10
	 *   match rpki invalid
	 * route-map out permit 20
	 * Then a locally originated route that becomes RPKI invalid needs to be
	 * re-evaluated against the route-map, and if we don't force
	 * re-advertisement, it won't be.
	 */
	for (bpi = bgp_dest_get_bgp_path_info(bgp_dest); bpi; bpi = bpi->next) {
		if (bpi->peer == bgp->peer_self && bpi->type == ZEBRA_ROUTE_BGP &&
		    (bpi->sub_type == BGP_ROUTE_STATIC || bpi->sub_type == BGP_ROUTE_AGGREGATE)) {
			bgp_path_info_set_flag(bgp_dest, bpi, BGP_PATH_ATTR_CHANGED);
			bgp_process(bgp, bgp_dest, bpi, afi, safi);
			break;
		}
	}
}

/* 【RTR 回调】rpki_update_cb_sync_rtr —— rtrlib 在 ROA 变化（新增 / 撤销一条记录）时
 * 调用的回调（注册于 rtr_mgr_init）。它把这条 pfx_record **原样写进 socketpair**，
 * 交给 bgpd 事件循环里的 bgpd_sync_callback 去重校验——本回调运行在 rtrlib 自己的
 * 线程里，不能直接操作 BGP 数据结构，故用 socket 传递。
 * 处理：由 rec.socket 反查 tr_socket 与标识 ident，进而 find_rpki_vrf_from_ident 定位
 * rpki_vrf；对任何一步失败打错误日志（err 标签）。
 * 坑：若 rpki 正在停止或已发生溢出，则直接丢弃本次记录；写 socket 遇到
 * EAGAIN/EWOULDBLOCK（缓冲满）时置 rtr_update_overflow=1，让 bgpd 侧整体重算。
 * 参数 p / added 未使用（属性标注 unused）。无返回值。 */
static void rpki_update_cb_sync_rtr(struct pfx_table *p __attribute__((unused)),
				    const struct pfx_record rec,
				    const bool added __attribute__((unused)))
{
	struct rpki_vrf *rpki_vrf;
	const char *msg;
	const struct rtr_socket *rtr = rec.socket;
	const char *ident;

	if (!rtr) {
		msg = "could not find rtr_socket from cb_sync_rtr";
		goto err;
	}
	if (!rtr->tr_socket) {
		msg = "could not find tr_socket from cb_sync_rtr";
		goto err;
	}
	ident = rtr->tr_socket->ident_fp(rtr->tr_socket->socket);
	if (!ident) {
		msg = "could not find ident from cb_sync_rtr";
		goto err;
	}
	rpki_vrf = find_rpki_vrf_from_ident(ident);
	if (!rpki_vrf) {
		msg = "could not find rpki_vrf";
		goto err;
	}

	if (is_stopping(rpki_vrf) ||
	    atomic_load_explicit(&rpki_vrf->rtr_update_overflow,
				 memory_order_seq_cst))
		return;

	int retval = write(rpki_vrf->rpki_sync_socket_rtr, &rec,
			   sizeof(struct pfx_record));
	if (retval == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
		atomic_store_explicit(&rpki_vrf->rtr_update_overflow, 1,
				      memory_order_seq_cst);

	else if (retval != sizeof(struct pfx_record))
		RPKI_DEBUG("Could not write to rpki_sync_socket_rtr");
	return;
err:
	flog_err(EC_LIB_DEVELOPMENT, "RPKI: %s", msg);
}

/* 【初始化】rpki_init_sync_socket —— 建立 RTR 回调与 bgpd 事件循环之间的
 * socketpair：用 socketpair(PF_LOCAL, SOCK_DGRAM) 得一对 fd，分别存到
 * rpki_sync_socket_rtr（回调写）与 rpki_sync_socket_bgpd（bgpd 读），两端都设为
 * 非阻塞，并给 bgpd 端注册读事件 bgpd_sync_callback。
 * 任何一步失败都打错误日志并 abort()（这是启动期不可恢复的错误）。无返回值。 */
static void rpki_init_sync_socket(struct rpki_vrf *rpki_vrf)
{
	int fds[2];
	const char *msg;

	RPKI_DEBUG("initializing sync socket");
	if (socketpair(PF_LOCAL, SOCK_DGRAM, 0, fds) != 0) {
		msg = "could not open rpki sync socketpair";
		goto err;
	}
	rpki_vrf->rpki_sync_socket_rtr = fds[0];
	rpki_vrf->rpki_sync_socket_bgpd = fds[1];

	if (set_nonblocking(rpki_vrf->rpki_sync_socket_rtr) != 0) {
		msg = "could not set rpki_sync_socket_rtr to non blocking";
		goto err;
	}

	if (set_nonblocking(rpki_vrf->rpki_sync_socket_bgpd) != 0) {
		msg = "could not set rpki_sync_socket_bgpd to non blocking";
		goto err;
	}


	event_add_read(bm->master, bgpd_sync_callback, rpki_vrf,
		       rpki_vrf->rpki_sync_socket_bgpd, NULL);

	return;

err:
	flog_err(EC_LIB_DEVELOPMENT, "RPKI: %s", msg);
	abort();

}

/* 【分配】bgp_rpki_allocate —— 新建一个 rpki_vrf 上下文：分配结构、建 cache_list
 * （del 回调设为 free_cache，删节点时自动释放）、置三个时间参数为默认值、按需复制
 * vrfname（默认 VRF 不复制）、注册 QOBJ、加入 rpki_vrf_list，返回新对象。
 * 调用方仍需自行初始化 sync socket（见 rpki 命令处）。 */
static struct rpki_vrf *bgp_rpki_allocate(const char *vrfname)
{
	struct rpki_vrf *rpki_vrf;

	/* initialise default vrf cache list */
	rpki_vrf = XCALLOC(MTYPE_BGP_RPKI_CACHE, sizeof(struct rpki_vrf));

	rpki_vrf->cache_list = list_new();
	rpki_vrf->cache_list->del = (void (*)(void *)) & free_cache;
	rpki_vrf->polling_period = POLLING_PERIOD_DEFAULT;
	rpki_vrf->expire_interval = EXPIRE_INTERVAL_DEFAULT;
	rpki_vrf->retry_interval = RETRY_INTERVAL_DEFAULT;

	if (vrfname && !strmatch(vrfname, VRF_DEFAULT_NAME))
		rpki_vrf->vrfname = XSTRDUP(MTYPE_BGP_RPKI_CACHE, vrfname);
	QOBJ_REG(rpki_vrf, rpki_vrf);
	listnode_add(rpki_vrf_list, rpki_vrf);

	return rpki_vrf;
}

/* 【初始化】bgp_rpki_init —— 注册到 frr_late_init 钩子，在 FRR 启动晚期执行：
 * 关闭调试标志、创建全局 rpki_vrf_list、安装全部 RPKI 命令。返回 0。 */
static int bgp_rpki_init(struct event_loop *master)
{
	rpki_debug_conf = false;
	rpki_debug_term = false;

	rpki_vrf_list = list_new();

	install_cli_commands();

	return 0;
}

/* 【清理】bgp_rpki_fini —— 注册到 frr_early_fini 钩子，退出时执行：遍历每个
 * rpki_vrf，stop 之、删除 cache_list、关闭 socketpair 两端、从列表移除、注销
 * QOBJ、释放 vrfname 与结构体。返回 0。 */
static int bgp_rpki_fini(void)
{
	struct listnode *node, *nnode;
	struct rpki_vrf *rpki_vrf;

	for (ALL_LIST_ELEMENTS(rpki_vrf_list, node, nnode, rpki_vrf)) {
		stop(rpki_vrf);
		list_delete(&rpki_vrf->cache_list);

		close(rpki_vrf->rpki_sync_socket_rtr);
		close(rpki_vrf->rpki_sync_socket_bgpd);

		listnode_delete(rpki_vrf_list, rpki_vrf);
		QOBJ_UNREG(rpki_vrf);
		if (rpki_vrf->vrfname)
			XFREE(MTYPE_BGP_RPKI_CACHE, rpki_vrf->vrfname);
		XFREE(MTYPE_BGP_RPKI_CACHE, rpki_vrf);
	}

	return 0;
}

/* 【模块初始化】bgp_rpki_module_init —— 模块入口：创建线程键 rpki_pthread，把
 * rtrlib 的分配函数替换为 FRR 包装，并注册各钩子（前缀校验、连接状态、late_init /
 * early_fini、debug 回写、VRF 更新、VRF 配置回写）。返回 0。 */
static int bgp_rpki_module_init(void)
{
	pthread_key_create(&rpki_pthread, NULL);

	lrtr_set_alloc_functions(malloc_wrapper, realloc_wrapper, free_wrapper);

	hook_register(bgp_rpki_prefix_status, rpki_validate_prefix);
	hook_register(bgp_rpki_connection_status, bgp_rpki_is_connected);
	hook_register(frr_late_init, bgp_rpki_init);
	hook_register(frr_early_fini, bgp_rpki_fini);
	hook_register(bgp_hook_config_write_debug, &bgp_rpki_write_debug);
	hook_register(bgp_hook_vrf_update, &bgp_rpki_vrf_update);
	hook_register(bgp_hook_config_write_vrf, &bgp_rpki_hook_write_vrf);

	return 0;
}

/* 【同步判定】sync_expired —— 定时回调（start 时以 0 延时首次投递）：轮询
 * rtr_mgr_conf_in_sync。若尚未同步完成，则按 BGP_RPKI_CACHE_SERVER_SYNC_RETRY_TIMEOUT
 * 秒后重排本回调继续等；一旦同步完成，置 rtr_is_synced = true（此后校验结果可信）。
 * 无返回值。 */
static void sync_expired(struct event *event)
{
	struct rpki_vrf *rpki_vrf = EVENT_ARG(event);

	if (!rtr_mgr_conf_in_sync(rpki_vrf->rtr_config)) {
		RPKI_DEBUG("rtr_mgr is not synced, retrying.");
		event_add_timer(bm->master, sync_expired, rpki_vrf,
				BGP_RPKI_CACHE_SERVER_SYNC_RETRY_TIMEOUT,
				&rpki_vrf->t_rpki_sync);
		return;
	}

	RPKI_DEBUG("rtr_mgr sync is done.");

	rpki_vrf->rtr_is_synced = true;
}

/* 【启动】start —— 启动某 VRF 的 RPKI 会话：清零停止/同步/溢出标志；缓存列表为空
 * 则直接返回 ERROR（无 cache 无可校验）；按 vrfname 查到 VRF 且必须 VRF_ACTIVE；
 * 用 get_groups 组装组数组后 rtr_mgr_init 初始化、rtr_mgr_start 启动；投递
 * sync_expired 定时器等待同步；释放组数组，置 rtr_is_running = true，返回 SUCCESS。
 * 失败路径返回 ERROR（rtr_mgr_start 失败还会先 rtr_mgr_free）。
 * 坑：groups 无论成败在最后都需 XFREE（本函数在成功末尾释放）。 */
static int start(struct rpki_vrf *rpki_vrf)
{
	struct list *cache_list = NULL;
	struct vrf *vrf;
	int ret;

	rpki_vrf->rtr_is_stopping = false;
	rpki_vrf->rtr_is_synced = false;
	rpki_vrf->rtr_update_overflow = 0;
	cache_list = rpki_vrf->cache_list;
	rpki_vrf->rtr_update_overflow = 0;

	if (!cache_list || list_isempty(cache_list)) {
		RPKI_DEBUG(
			"No caches were found in config. Prefix validation is off.");
		return ERROR;
	}

	if (rpki_vrf->vrfname)
		vrf = vrf_lookup_by_name(rpki_vrf->vrfname);
	else
		vrf = vrf_lookup_by_id(VRF_DEFAULT);
	if (!vrf || !CHECK_FLAG(vrf->status, VRF_ACTIVE)) {
		RPKI_DEBUG("VRF %s not present or disabled", rpki_vrf->vrfname);
		return ERROR;
	}

	RPKI_DEBUG("Init rtr_mgr (%s).", vrf->name);
	int groups_len = listcount(cache_list);
	struct rtr_mgr_group *groups = get_groups(rpki_vrf->cache_list);

	RPKI_DEBUG("Polling period: %d", rpki_vrf->polling_period);
	ret = rtr_mgr_init(&rpki_vrf->rtr_config, groups, groups_len,
			   rpki_vrf->polling_period, rpki_vrf->expire_interval,
			   rpki_vrf->retry_interval, rpki_update_cb_sync_rtr,
			   NULL, NULL, NULL);
	if (ret == RTR_ERROR) {
		RPKI_DEBUG("Init rtr_mgr failed (%s).", vrf->name);
		return ERROR;
	}

	RPKI_DEBUG("Starting rtr_mgr (%s).", vrf->name);
	ret = rtr_mgr_start(rpki_vrf->rtr_config);
	if (ret == RTR_ERROR) {
		RPKI_DEBUG("Starting rtr_mgr failed (%s).", vrf->name);
		rtr_mgr_free(rpki_vrf->rtr_config);
		return ERROR;
	}

	event_add_timer(bm->master, sync_expired, rpki_vrf, 0,
			&rpki_vrf->t_rpki_sync);

	XFREE(MTYPE_BGP_RPKI_CACHE_GROUP, groups);

	rpki_vrf->rtr_is_running = true;

	return SUCCESS;
}

/* 【停止】stop —— 停掉某 VRF 的 RPKI 会话：先置 rtr_is_stopping = true（让 RTR
 * 回调知道正在停、不再写 socket）；若在运行则取消同步定时器、rtr_mgr_stop 停止会话、
 * rtr_mgr_free 释放配置，最后清 rtr_is_running。无返回值。 */
static void stop(struct rpki_vrf *rpki_vrf)
{
	rpki_vrf->rtr_is_stopping = true;
	if (is_running(rpki_vrf)) {
		event_cancel(&rpki_vrf->t_rpki_sync);
		rtr_mgr_stop(rpki_vrf->rtr_config);
		rtr_mgr_free(rpki_vrf->rtr_config);
		rpki_vrf->rtr_is_running = false;
	}
}

/* 【重置】reset —— 重启某 VRF 的 RPKI 会话 = stop 后 start。参数 force 为假且会话
 * 已在运行时直接返回 SUCCESS（不打断现有连接）；force 为真则无论是否在运行都重启。
 * 返回 start 的结果（SUCCESS / ERROR）。 */
static int reset(bool force, struct rpki_vrf *rpki_vrf)
{
	if (is_running(rpki_vrf) && !force)
		return SUCCESS;

	RPKI_DEBUG("Resetting RPKI Session");
	stop(rpki_vrf);
	return start(rpki_vrf);
}

/* 【获取】get_connected_group —— 返回 rtr_mgr 的“第一个组”（rtr_mgr_get_first_group）
 * 作为当前使用的组；rpki_vrf 为空或 cache 列表为空时返回 NULL。打印 prefix table 与
 * show 命令据此拿 pfx_table。 */
static struct rtr_mgr_group *get_connected_group(struct rpki_vrf *rpki_vrf)
{
	struct list *cache_list;

	if (!rpki_vrf)
		return NULL;

	cache_list = rpki_vrf->cache_list;
	if (!cache_list || list_isempty(cache_list))
		return NULL;

	return rtr_mgr_get_first_group(rpki_vrf->rtr_config);
}

/* 【输出】print_prefix_table_by_asn —— 打印 ROA 表里**源 AS 等于 as** 的记录，供
 * `show rpki as-number`。与 print_prefix_table 的区别：只列 as-match 的记录，且
 * 分别统计 IPv4 / IPv6 条数。json 非空则输出 JSON（含 error / prefixes / 计数），
 * 否则输出文本表头与计数。取不到连接组时报 “Cannot find a connected group.”。 */
static void print_prefix_table_by_asn(struct vty *vty, as_t as,
				      struct rpki_vrf *rpki_vrf,
				      json_object *json)
{
	unsigned int number_of_ipv4_prefixes = 0;
	unsigned int number_of_ipv6_prefixes = 0;
	struct rtr_mgr_group *group = get_connected_group(rpki_vrf);
	struct rpki_for_each_record_arg arg;
	json_object *json_records = NULL;

	arg.vty = vty;
	arg.as = as;
	arg.json = NULL;
	arg.asnotation = bgp_get_asnotation(bgp_lookup_by_vrf_id(VRF_DEFAULT));

	if (!rpki_vrf)
		return;

	if (!group) {
		if (json) {
			json_object_string_add(json, "error", "Cannot find a connected group.");
			vty_json(vty, json);
		} else
			vty_out(vty, "Cannot find a connected group.\n");
		return;
	}

	struct pfx_table *pfx_table = group->sockets[0]->pfx_table;

	if (!json) {
		vty_out(vty, "RPKI/RTR prefix table\n");
		vty_out(vty, "%-40s %s  %s\n", "Prefix", "Prefix Length",
			"Origin-AS");
	} else {
		json_records = json_object_new_array();
		json_object_object_add(json, "prefixes", json_records);
		arg.json = json_records;
	}

	arg.prefix_amount = &number_of_ipv4_prefixes;
	pfx_table_for_each_ipv4_record(pfx_table, print_record_by_asn, &arg);

	arg.prefix_amount = &number_of_ipv6_prefixes;
	pfx_table_for_each_ipv6_record(pfx_table, print_record_by_asn, &arg);

	if (!json) {
		vty_out(vty, "Number of IPv4 Prefixes: %u\n",
			number_of_ipv4_prefixes);
		vty_out(vty, "Number of IPv6 Prefixes: %u\n",
			number_of_ipv6_prefixes);
	} else {
		json_object_int_add(json, "ipv4PrefixCount",
				    number_of_ipv4_prefixes);
		json_object_int_add(json, "ipv6PrefixCount",
				    number_of_ipv6_prefixes);
	}

	if (json)
		vty_json(vty, json);
}

/* 【输出】print_prefix_table —— 打印整张 ROA 表（`show rpki prefix-table`）。
 * count_only 为真只看条数（用 count_record_cb），否则逐条打印（print_record_cb）。
 * 分别对 IPv4 / IPv6 记录遍历两次、给出两个计数；json 非空则输出 JSON。取不到连接
 * 组时报 “Cannot find a connected group.”。 */
static void print_prefix_table(struct vty *vty, struct rpki_vrf *rpki_vrf,
			       json_object *json, bool count_only)
{
	struct rpki_for_each_record_arg arg;

	unsigned int number_of_ipv4_prefixes = 0;
	unsigned int number_of_ipv6_prefixes = 0;
	struct rtr_mgr_group *group;
	json_object *json_records = NULL;

	if (!rpki_vrf)
		return;

	group = get_connected_group(rpki_vrf);
	arg.vty = vty;
	arg.json = NULL;
	arg.asnotation = bgp_get_asnotation(bgp_lookup_by_vrf_id(VRF_DEFAULT));

	if (!group) {
		if (json) {
			json_object_string_add(json, "error", "Cannot find a connected group.");
			vty_json(vty, json);
		} else
			vty_out(vty, "Cannot find a connected group.\n");
		return;
	}

	struct pfx_table *pfx_table = group->sockets[0]->pfx_table;

	if (!count_only) {
		if (!json) {
			vty_out(vty, "RPKI/RTR prefix table\n");
			vty_out(vty, "%-40s %s  %s\n", "Prefix",
				"Prefix Length", "Origin-AS");
		} else {
			json_records = json_object_new_array();
			json_object_object_add(json, "prefixes", json_records);
			arg.json = json_records;
		}
	}

	arg.prefix_amount = &number_of_ipv4_prefixes;
	if (count_only)
		pfx_table_for_each_ipv4_record(pfx_table, count_record_cb, &arg);
	else
		pfx_table_for_each_ipv4_record(pfx_table, print_record_cb, &arg);

	arg.prefix_amount = &number_of_ipv6_prefixes;
	if (count_only)
		pfx_table_for_each_ipv6_record(pfx_table, count_record_cb, &arg);
	else
		pfx_table_for_each_ipv6_record(pfx_table, print_record_cb, &arg);

	if (!json) {
		vty_out(vty, "Number of IPv4 Prefixes: %u\n",
			number_of_ipv4_prefixes);
		vty_out(vty, "Number of IPv6 Prefixes: %u\n",
			number_of_ipv6_prefixes);
	} else {
		json_object_int_add(json, "ipv4PrefixCount",
				    number_of_ipv4_prefixes);
		json_object_int_add(json, "ipv6PrefixCount",
				    number_of_ipv6_prefixes);
	}

	if (json)
		vty_json(vty, json);
}

/* 【核心校验】rpki_validate_prefix —— 走 RTR 数据源判定一条路由的 RPKI 三态，
 * 是选路 / 过滤的判据来源（经 hook bgp_rpki_prefix_status 暴露）。
 * 步骤：
 *   1) 定位该 VRF 的 rpki_vrf；未配置或未同步（is_synchronized 为假）则返回 0 /
 *      RPKI_NOT_BEING_USED——**数据未就绪时不武断判 invalid**。
 *   2) 求源 AS：AS_PATH 为空（iBGP 来的，无 AS_PATH）时取本方 AS；否则取**最右**
 *      （最后一个）AS_SEQUENCE 段的最后一个 ASN；若尾段是 confed 段也取本方 AS；
 *      其它段类型按 RFC 取“NONE”即返回 RPKI_NOTFOUND（未知）。
 *   3) 把前缀转成 rtrlib 的 lrtr_ip_addr（v4 直接、v6 用字节序转换），
 *      rtr_mgr_validate 做实际比对。
 *   4) 把结果映射成 enum rpki_states：VALID→RPKI_VALID、NOT_FOUND→RPKI_NOTFOUND、
 *      INVALID→RPKI_INVALID，其它→RPKI_NOT_BEING_USED；并打调试日志。
 * 返回 enum rpki_states 值（0 亦表示功能未启用）。 */
static int rpki_validate_prefix(struct peer *peer, struct attr *attr,
				const struct prefix *prefix)
{
	struct assegment *as_segment;
	as_t as_number = 0;
	struct lrtr_ip_addr ip_addr_prefix;
	enum pfxv_state result;
	struct bgp *bgp = peer->bgp;
	struct vrf *vrf;
	struct rpki_vrf *rpki_vrf;

	if (!bgp)
		return 0;

	vrf = vrf_lookup_by_id(bgp->vrf_id);
	if (!vrf)
		return 0;

	if (vrf->vrf_id == VRF_DEFAULT)
		rpki_vrf = find_rpki_vrf(NULL);
	else
		rpki_vrf = find_rpki_vrf(vrf->name);
	if (!rpki_vrf || !is_synchronized(rpki_vrf))
		return 0;

	if (!is_synchronized(rpki_vrf))
		return RPKI_NOT_BEING_USED;

	// No aspath means route comes from iBGP
	if (!attr->aspath || !attr->aspath->segments) {
		// Set own as number
		as_number = peer->bgp->as;
	} else {
		as_segment = attr->aspath->segments;
		// Find last AsSegment
		while (as_segment->next)
			as_segment = as_segment->next;

		if (as_segment->type == AS_SEQUENCE) {
			// Get rightmost asn
			as_number = as_segment->as[as_segment->length - 1];
		} else if (as_segment->type == AS_CONFED_SEQUENCE
			   || as_segment->type == AS_CONFED_SET) {
			// Set own as number
			as_number = peer->bgp->as;
		} else {
			// RFC says: "Take distinguished value NONE as asn"
			// which means state is unknown
			return RPKI_NOTFOUND;
		}
	}

	// Get the prefix in requested format
	switch (prefix->family) {
	case AF_INET:
		ip_addr_prefix.ver = LRTR_IPV4;
		ip_addr_prefix.u.addr4.addr = ntohl(prefix->u.prefix4.s_addr);
		break;

	case AF_INET6:
		ip_addr_prefix.ver = LRTR_IPV6;
		ipv6_addr_to_host_byte_order(prefix->u.prefix6.s6_addr32,
					     ip_addr_prefix.u.addr6.addr);
		break;

	default:
		return RPKI_NOT_BEING_USED;
	}

	// Do the actual validation
	rtr_mgr_validate(rpki_vrf->rtr_config, as_number, &ip_addr_prefix,
			 prefix->prefixlen, &result);

	// Print Debug output
	switch (result) {
	case BGP_PFXV_STATE_VALID:
		RPKI_DEBUG(
			"Validating Prefix %pFX from asn %u    Result: VALID",
			prefix, as_number);
		return RPKI_VALID;
	case BGP_PFXV_STATE_NOT_FOUND:
		RPKI_DEBUG(
			"Validating Prefix %pFX from asn %u    Result: NOT FOUND",
			prefix, as_number);
		return RPKI_NOTFOUND;
	case BGP_PFXV_STATE_INVALID:
		RPKI_DEBUG(
			"Validating Prefix %pFX from asn %u    Result: INVALID",
			prefix, as_number);
		return RPKI_INVALID;
	default:
		RPKI_DEBUG(
			"Validating Prefix %pFX from asn %u    Result: CANNOT VALIDATE",
			prefix, as_number);
		break;
	}
	return RPKI_NOT_BEING_USED;
}

/* 【添加】add_cache —— 把一个新建的 cache 挂进它所属的 rpki_vrf：先取 preference /
 * cache_list；若会话正在运行，则 init_tr_socket 后 rtr_mgr_add_group 把该组加进正在
 * 跑的管理器（失败则 free_tr_socket 回滚并返回 ERROR）；最后 listnode_add 进
 * cache_list。返回 SUCCESS / ERROR。 */
static int add_cache(struct cache *cache)
{
	uint8_t preference = cache->preference;
	struct rtr_mgr_group group;
	struct list *cache_list;
	struct rpki_vrf *rpki_vrf;

	rpki_vrf = cache->rpki_vrf;
	if (!rpki_vrf)
		return ERROR;

	group.preference = preference;
	group.sockets_len = 1;
	group.sockets = &cache->rtr_socket;

	cache_list = rpki_vrf->cache_list;
	if (!cache_list)
		return ERROR;

	if (is_running(rpki_vrf)) {
		init_tr_socket(cache);

		if (rtr_mgr_add_group(rpki_vrf->rtr_config, &group) !=
		    RTR_SUCCESS) {
			free_tr_socket(cache);
			return ERROR;
		}
	}

	listnode_add(cache_list, cache);

	return SUCCESS;
}

/* 【建连】rpki_create_socket —— rtrlib 建连接时回调的“创建真实套接字”函数（通过
 * tr_config->new_socket 注册，见 add_tcp_cache / add_ssh_cache）。
 * 职责：把该 cache 的 host/port 解析成地址（vrf_getaddrinfo，带 bgpd 特权），创建
 * 套接字（vrf_socket），临时设置收发超时为 30 秒，connect 成功后恢复原超时并返回 fd。
 * 关键点：
 *   - 线程接入：rtrlib 可能对同一 pthread 多次调用本函数，重复接入 rcu 会在关闭时
 *     断言，故用 pthread_getspecific 检查是否已接入，未接入才调用
 *     frr_pthread_non_controlled_startup（下面那段英文长注释即解释此点，并警告**在
 *     此调用之前打任何 zlog 会导致崩溃**）。
 *   - VRF：按 vrfname 找 vrf，必须存在且 VRF_ACTIVE，否则返回 -1。
 *   - TCP 用字面 host/port；SSH 把端口格式化成字符串并加 AI_NUMERICHOST。
 * 返回套接字 fd；失败返回 -1。副作用：连接临时调整为 30 秒超时（成功后还原）。 */
static int rpki_create_socket(void *_cache)
{
	struct timeval prev_snd_tmout, prev_rcv_tmout, timeout;
	struct cache *cache = (struct cache *)_cache;
	struct rpki_vrf *rpki_vrf;
	struct tr_tcp_config *tcp_config;
	struct addrinfo *res = NULL;
	struct addrinfo hints = {};
	socklen_t optlen;
	char *host, *port;
	struct vrf *vrf;
	int cancel_state;
	int socket;
	int ret;
#if defined(FOUND_SSH)
	struct tr_ssh_config *ssh_config;
	char s_port[10];
#endif

	if (!cache)
		return -1;

	rpki_vrf = cache->rpki_vrf;

	/*
	 * the rpki infrastructure can call this function
	 * multiple times per pthread.  Why?  I have absolutely
	 * no idea, and I am not sure I care a whole bunch.
	 * Why does this matter?  Well when we attempt to
	 * hook this pthread into the rcu structure multiple
	 * times the rcu code asserts on shutdown.  Clearly
	 * upset that you have rcu data associated with a pthread
	 * that has not been cleaned up.  And frankly this is rightly so.
	 *
	 * At this point we know that this function is not
	 * called a million bajillion times so let's just
	 * add a bit of insurance by looking to see if
	 * some thread specific code has been set for this
	 * pthread.  If not, hook into the rcu code and
	 * make things happy.
	 *
	 * IF YOU PUT A ZLOG_XXXX prior to the call into
	 * frr_pthread_non_controlled_startup in this function
	 * BGP WILL CRASH. You have been warned.
	 */
	if (!pthread_getspecific(rpki_pthread) &&
	    frr_pthread_non_controlled_startup(cache->rtr_socket->thread_id,
					       "RPKI RTRLIB socket",
					       "rpki_create_socket") < 0)
		return -1;

	pthread_setspecific(rpki_pthread, &rpki_pthread);

	if (rpki_vrf->vrfname == NULL)
		vrf = vrf_lookup_by_id(VRF_DEFAULT);
	else
		vrf = vrf_lookup_by_name(rpki_vrf->vrfname);
	if (!vrf)
		return -1;

	if (!CHECK_FLAG(vrf->status, VRF_ACTIVE) || vrf->vrf_id == VRF_UNKNOWN)
		return -1;

	if (cache->type == TCP) {
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_ADDRCONFIG;

		tcp_config = cache->tr_config.tcp_config;
		host = tcp_config->host;
		port = tcp_config->port;
	}
#if defined(FOUND_SSH)
	else {
		ssh_config = cache->tr_config.ssh_config;
		host = ssh_config->host;
		snprintf(s_port, sizeof(s_port), "%u", ssh_config->port);
		port = s_port;

		hints.ai_flags |= AI_NUMERICHOST;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
	}
#endif

	frr_with_privs (&bgpd_privs) {
		ret = vrf_getaddrinfo(host, port, &hints, &res, vrf->vrf_id);
	}
	if (ret != 0) {
		flog_err_sys(EC_LIB_SOCKET, "getaddrinfo: %s",
			     gai_strerror(ret));
		return -1;
	}

	frr_with_privs (&bgpd_privs) {
		socket = vrf_socket(res->ai_family, res->ai_socktype,
				    res->ai_protocol, vrf->vrf_id, NULL);
	}
	if (socket < 0) {
		freeaddrinfo(res);
		return -1;
	}

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancel_state);
	timeout.tv_sec = 30;
	timeout.tv_usec = 0;

	optlen = sizeof(prev_rcv_tmout);
	ret = getsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &prev_rcv_tmout,
			 &optlen);
	if (ret < 0)
		zlog_warn("%s: failed to getsockopt SO_RCVTIMEO for socket %d",
			  __func__, socket);
	ret = getsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &prev_snd_tmout,
			 &optlen);
	if (ret < 0)
		zlog_warn("%s: failed to getsockopt SO_SNDTIMEO for socket %d",
			  __func__, socket);
	ret = setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
			 sizeof(timeout));
	if (ret < 0)
		zlog_warn("%s: failed to setsockopt SO_RCVTIMEO for socket %d",
			  __func__, socket);

	ret = setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout,
			 sizeof(timeout));
	if (ret < 0)
		zlog_warn("%s: failed to setsockopt SO_SNDTIMEO for socket %d",
			  __func__, socket);

	if (connect(socket, res->ai_addr, res->ai_addrlen) == -1) {
		freeaddrinfo(res);
		close(socket);
		pthread_setcancelstate(cancel_state, NULL);
		return -1;
	}

	freeaddrinfo(res);
	pthread_setcancelstate(cancel_state, NULL);

	ret = setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &prev_rcv_tmout,
			 sizeof(prev_rcv_tmout));
	if (ret < 0)
		zlog_warn("%s: failed to setsockopt SO_RCVTIMEO for socket %d",
			  __func__, socket);

	ret = setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &prev_snd_tmout,
			 sizeof(prev_snd_tmout));
	if (ret < 0)
		zlog_warn("%s: failed to setsockopt SO_SNDTIMEO for socket %d",
			  __func__, socket);

	return socket;
}

/* 【创建】add_tcp_cache —— 新建一个 TCP 类型的 cache 并加入 rpki_vrf：分配
 * tr_tcp_config / tr_socket / cache，复制 host、port、可选 bindaddr，设
 * new_socket=rpki_create_socket 与 data=cache，建 rtr_socket，填 cache 各字段，最后
 * add_cache 入库；add_cache 失败时清 data 并 free_cache 回滚。返回 SUCCESS / ERROR。 */
static int add_tcp_cache(struct rpki_vrf *rpki_vrf, const char *host,
			 const char *port, const uint8_t preference,
			 const char *bindaddr)
{
	struct rtr_socket *rtr_socket;
	struct tr_tcp_config *tcp_config =
		XCALLOC(MTYPE_BGP_RPKI_CACHE, sizeof(struct tr_tcp_config));
	struct tr_socket *tr_socket =
		XMALLOC(MTYPE_BGP_RPKI_CACHE, sizeof(struct tr_socket));
	struct cache *cache =
		XMALLOC(MTYPE_BGP_RPKI_CACHE, sizeof(struct cache));

	tcp_config->host = XSTRDUP(MTYPE_BGP_RPKI_CACHE, host);
	tcp_config->port = XSTRDUP(MTYPE_BGP_RPKI_CACHE, port);
	if (bindaddr)
		tcp_config->bindaddr = XSTRDUP(MTYPE_BGP_RPKI_CACHE, bindaddr);
	else
		tcp_config->bindaddr = NULL;

	tcp_config->data = cache;
	tcp_config->new_socket = rpki_create_socket;
	rtr_socket = create_rtr_socket(tr_socket);

	cache->rpki_vrf = rpki_vrf;
	cache->type = TCP;
	cache->tr_socket = tr_socket;
	cache->tr_config.tcp_config = tcp_config;
	cache->rtr_socket = rtr_socket;
	cache->preference = preference;

	int ret = add_cache(cache);
	if (ret != SUCCESS) {
		tcp_config->data = NULL;
		free_cache(cache);
	}
	return ret;
}

#if defined(FOUND_SSH)
/* 【创建】add_ssh_cache —— 与 add_tcp_cache 平行，但建的是 SSH 类型 cache：
 * 额外带 username / client_privkey_path / server_hostkey_path（server host key 校验
 * 用），同样设 new_socket=rpki_create_socket。add_cache 失败时清 data 并回滚。
 * 返回 SUCCESS / ERROR。注意本函数仅在编译期支持 SSH（FOUND_SSH）时才存在。 */
static int add_ssh_cache(struct rpki_vrf *rpki_vrf, const char *host,
			 const unsigned int port, const char *username,
			 const char *client_privkey_path,
			 const char *server_pubkey_path,
			 const uint8_t preference, const char *bindaddr)
{
	struct tr_ssh_config *ssh_config =
		XCALLOC(MTYPE_BGP_RPKI_CACHE, sizeof(struct tr_ssh_config));
	struct cache *cache =
		XMALLOC(MTYPE_BGP_RPKI_CACHE, sizeof(struct cache));
	struct tr_socket *tr_socket =
		XMALLOC(MTYPE_BGP_RPKI_CACHE, sizeof(struct tr_socket));
	struct rtr_socket *rtr_socket;

	ssh_config->port = port;
	ssh_config->host = XSTRDUP(MTYPE_BGP_RPKI_CACHE, host);
	if (bindaddr)
		ssh_config->bindaddr = XSTRDUP(MTYPE_BGP_RPKI_CACHE, bindaddr);
	else
		ssh_config->bindaddr = NULL;
	ssh_config->data = cache;
	ssh_config->new_socket = rpki_create_socket;

	ssh_config->username = XSTRDUP(MTYPE_BGP_RPKI_CACHE, username);
	ssh_config->client_privkey_path =
		XSTRDUP(MTYPE_BGP_RPKI_CACHE, client_privkey_path);
	ssh_config->server_hostkey_path =
		XSTRDUP(MTYPE_BGP_RPKI_CACHE, server_pubkey_path);

	rtr_socket = create_rtr_socket(tr_socket);

	cache->rpki_vrf = rpki_vrf;
	cache->type = SSH;
	cache->tr_socket = tr_socket;
	cache->tr_config.ssh_config = ssh_config;
	cache->rtr_socket = rtr_socket;
	cache->preference = preference;

	int ret = add_cache(cache);
	if (ret != SUCCESS) {
		ssh_config->data = NULL;
		free_cache(cache);
	}

	return ret;
}
#endif

/* 【释放】free_cache —— 释放一个 cache：按 type 释放 tr_config 里 TCP 或 SSH 的各
 * 字符串字段与结构体本身，再释放 tr_socket、rtr_socket，最后释放 cache。
 * 它同时是 cache_list 的 del 回调（见 bgp_rpki_allocate），删节点时自动调用。 */
static void free_cache(struct cache *cache)
{
	if (cache->type == TCP) {
		XFREE(MTYPE_BGP_RPKI_CACHE, cache->tr_config.tcp_config->host);
		XFREE(MTYPE_BGP_RPKI_CACHE, cache->tr_config.tcp_config->port);
		XFREE(MTYPE_BGP_RPKI_CACHE,
		      cache->tr_config.tcp_config->bindaddr);
		XFREE(MTYPE_BGP_RPKI_CACHE, cache->tr_config.tcp_config);
	}
#if defined(FOUND_SSH)
	else {
		XFREE(MTYPE_BGP_RPKI_CACHE, cache->tr_config.ssh_config->host);
		XFREE(MTYPE_BGP_RPKI_CACHE,
		      cache->tr_config.ssh_config->username);
		XFREE(MTYPE_BGP_RPKI_CACHE,
		      cache->tr_config.ssh_config->client_privkey_path);
		XFREE(MTYPE_BGP_RPKI_CACHE,
		      cache->tr_config.ssh_config->server_hostkey_path);
		XFREE(MTYPE_BGP_RPKI_CACHE,
		      cache->tr_config.ssh_config->bindaddr);
		XFREE(MTYPE_BGP_RPKI_CACHE, cache->tr_config.ssh_config);
	}
#endif
	XFREE(MTYPE_BGP_RPKI_CACHE, cache->tr_socket);
	XFREE(MTYPE_BGP_RPKI_CACHE, cache->rtr_socket);
	XFREE(MTYPE_BGP_RPKI_CACHE, cache);
}

/* 【配置回写】bgp_rpki_write_debug —— bgp_hook_config_write_debug 回调。running 为
 * 真（写运行配置）时，若配置模式调试开着就输出 "debug rpki" 并返回 1；running 为假
 * （写运行状态 / 提示）时，若任一调试开着就输出 “BGP RPKI debugging is on” 并返回 1。
 * 都未命中返回 0。 */
static int bgp_rpki_write_debug(struct vty *vty, bool running)
{
	if (rpki_debug_conf && running) {
		vty_out(vty, "debug rpki\n");
		return 1;
	}
	if ((rpki_debug_conf || rpki_debug_term) && !running) {
		vty_out(vty, "  BGP RPKI debugging is on\n");
		return 1;
	}
	return 0;
}

/* 【hook 回调】bgp_rpki_hook_write_vrf —— bgp_hook_config_write_vrf 回调，转调
 * bgp_rpki_write_vrf；把内部 ERROR 码折成 0（表示本 VRF 没写任何 RPKI 配置）。 */
static int bgp_rpki_hook_write_vrf(struct vty *vty, struct vrf *vrf)
{
	int ret;

	ret = bgp_rpki_write_vrf(vty, vrf);
	if (ret == ERROR)
		return 0;
	return ret;
}

/* 【配置回写】bgp_rpki_write_vrf —— 把某 VRF 的 RPKI 配置写成 rpki 配置块：
 *   - vrf 为空 → 默认 VRF，sep 用 ""；非默认 VRF → sep 用 " "（缩进）。
 *   - 全是默认值且无 cache 时直接返回 0（不输出，避免刷屏）。
 *   - 依次输出 rpki 头、非默认的 polling / retry / expire 参数，再遍历 cache_list
 *     逐条输出 tcp / ssh cache 行（含可选 source）与 preference，最后 exit。
 * 返回 1 表示写了配置，0 / ERROR(经 hook 折为 0) 表示没有。
 * 坑：调用方的入参约定——config_write 用 (vty, NULL)，hook 用 (vty, vrf)。 */
static int bgp_rpki_write_vrf(struct vty *vty, struct vrf *vrf)
{
	struct listnode *cache_node;
	struct cache *cache;
	struct rpki_vrf *rpki_vrf = NULL;
	char sep[STR_SEPARATOR];
	vrf_id_t vrf_id = VRF_DEFAULT;

	if (!vrf) {
		rpki_vrf = find_rpki_vrf(NULL);
		snprintf(sep, sizeof(sep), "%s", "");
	} else if (vrf->vrf_id != VRF_DEFAULT) {
		rpki_vrf = find_rpki_vrf(vrf->name);
		snprintf(sep, sizeof(sep), "%s", " ");
		vrf_id = vrf->vrf_id;
	} else
		return ERROR;

	if (!rpki_vrf)
		return ERROR;

	if (rpki_vrf->cache_list && list_isempty(rpki_vrf->cache_list) &&
	    rpki_vrf->polling_period == POLLING_PERIOD_DEFAULT &&
	    rpki_vrf->retry_interval == RETRY_INTERVAL_DEFAULT &&
	    rpki_vrf->expire_interval == EXPIRE_INTERVAL_DEFAULT)
		/* do not display the default config values */
		return 0;

	if (vrf_id == VRF_DEFAULT)
		vty_out(vty, "%s!\n", sep);
	vty_out(vty, "%srpki\n", sep);

	if (rpki_vrf->polling_period != POLLING_PERIOD_DEFAULT)
		vty_out(vty, "%s rpki polling_period %d\n", sep,
			rpki_vrf->polling_period);
	if (rpki_vrf->retry_interval != RETRY_INTERVAL_DEFAULT)
		vty_out(vty, "%s rpki retry_interval %d\n", sep,
			rpki_vrf->retry_interval);
	if (rpki_vrf->expire_interval != EXPIRE_INTERVAL_DEFAULT)
		vty_out(vty, "%s rpki expire_interval %d\n", sep,
			rpki_vrf->expire_interval);

	for (ALL_LIST_ELEMENTS_RO(rpki_vrf->cache_list, cache_node, cache)) {
		switch (cache->type) {
			struct tr_tcp_config *tcp_config;
#if defined(FOUND_SSH)
			struct tr_ssh_config *ssh_config;
#endif
		case TCP:
			tcp_config = cache->tr_config.tcp_config;
			vty_out(vty, "%s rpki cache tcp %s %s ", sep,
				tcp_config->host, tcp_config->port);
			if (tcp_config->bindaddr)
				vty_out(vty, "source %s ",
					tcp_config->bindaddr);
			break;
#if defined(FOUND_SSH)
		case SSH:
			ssh_config = cache->tr_config.ssh_config;
			vty_out(vty, "%s rpki cache ssh %s %u %s %s %s ", sep,
				ssh_config->host, ssh_config->port,
				ssh_config->username,
				ssh_config->client_privkey_path,
				ssh_config->server_hostkey_path != NULL
					? ssh_config->server_hostkey_path
					: "");
			if (ssh_config->bindaddr)
				vty_out(vty, "source %s ",
					ssh_config->bindaddr);
			break;
#endif
		default:
			break;
		}

		vty_out(vty, "preference %hhu\n", cache->preference);
	}

	vty_out(vty, "%sexit\n%s", sep, vrf_id == VRF_DEFAULT ? "!\n" : "");

	return 1;
}

/* 【配置回写】config_write —— rpki_node 的 config_write 回调，直接回写默认 VRF 的
 * RPKI 配置（= bgp_rpki_write_vrf(vty, NULL)）。 */
static int config_write(struct vty *vty)
{
	return bgp_rpki_write_vrf(vty, NULL);
}

/* 【查找】get_rpki_vrf —— 由 VRF 名取 rpki_vrf：非默认 VRF 先按名查到 vrf 再
 * find_rpki_vrf(vrf->name)，vrf 不存在返回 NULL；默认 VRF 直接 find_rpki_vrf(NULL)。 */
static struct rpki_vrf *get_rpki_vrf(const char *vrfname)
{
	struct rpki_vrf *rpki_vrf = NULL;
	struct vrf *vrf = NULL;

	if (vrfname && !strmatch(vrfname, VRF_DEFAULT_NAME)) {
		vrf = vrf_lookup_by_name(vrfname);
		if (!vrf)
			return NULL;
		rpki_vrf = find_rpki_vrf(vrf->name);
	} else
		/* default VRF */
		rpki_vrf = find_rpki_vrf(NULL);

	return rpki_vrf;
}

/* 【命令】`rpki`：进入 RPKI 配置模式（全局时进 RPKI_NODE，VRF 上下文里进
 * RPKI_VRF_NODE）。若该 VRF 尚无 rpki_vrf，则分配一个并初始化同步 socket，然后把
 * 该 rpki_vrf 压入 vty 上下文。返回 CMD_SUCCESS（无 vrf 上下文时 CMD_WARNING）。 */
DEFUN_NOSH (rpki,
	    rpki_cmd,
	    "rpki",
	    "Enable rpki and enter rpki configuration mode\n")
{
	struct rpki_vrf *rpki_vrf;
	char *vrfname = NULL;
	struct vrf *vrf;

	if (vty->node == CONFIG_NODE)
		vty->node = RPKI_NODE;
	else {
		vrf = VTY_GET_CONTEXT(vrf);

		if (!vrf)
			return CMD_WARNING;

		vty->node = RPKI_VRF_NODE;
		if (vrf->vrf_id != VRF_DEFAULT)
			vrfname = vrf->name;
	}

	rpki_vrf = find_rpki_vrf(vrfname);
	if (!rpki_vrf) {
		rpki_vrf = bgp_rpki_allocate(vrfname);

		rpki_init_sync_socket(rpki_vrf);
	}
	if (vty->node == RPKI_VRF_NODE)
		VTY_PUSH_CONTEXT_SUB(vty->node, rpki_vrf);
	else
		VTY_PUSH_CONTEXT(vty->node, rpki_vrf);
	return CMD_SUCCESS;
}

/* 【命令】`no rpki`：删除该 VRF 的全部 RPKI 配置——清空所有 cache 节点、停止会话、
 * 把三个时间参数复位为默认值。返回 CMD_SUCCESS（未配置 RPKI 时 CMD_WARNING）。 */
DEFPY (no_rpki,
       no_rpki_cmd,
       "no rpki",
       NO_STR
       "Enable rpki and enter rpki configuration mode\n")
{
	struct rpki_vrf *rpki_vrf;
	char *vrfname = NULL;

	if (vty->node == VRF_NODE) {
		VTY_DECLVAR_CONTEXT(vrf, vrf);

		if (vrf->vrf_id != VRF_DEFAULT)
			vrfname = vrf->name;
	}

	rpki_vrf = find_rpki_vrf(vrfname);
	if (!rpki_vrf)
		return CMD_WARNING;

	rpki_delete_all_cache_nodes(rpki_vrf);
	stop(rpki_vrf);
	rpki_vrf->polling_period = POLLING_PERIOD_DEFAULT;
	rpki_vrf->expire_interval = EXPIRE_INTERVAL_DEFAULT;
	rpki_vrf->retry_interval = RETRY_INTERVAL_DEFAULT;

	return CMD_SUCCESS;
}

/* 【命令】`rpki start [vrf NAME]`：启动指定（或默认）VRF 的 RPKI 会话。无 cache 时
 * 给出提示但仍走 start（start 会因无 cache 返回 ERROR）。返回 CMD_SUCCESS /
 * CMD_WARNING。 */
DEFPY (bgp_rpki_start,
       bgp_rpki_start_cmd,
       "rpki start [vrf NAME$vrfname]",
       RPKI_OUTPUT_STRING
       "start rpki support\n"
       VRF_CMD_HELP_STR)
{
	struct list *cache_list = NULL;
	struct rpki_vrf *rpki_vrf;

	rpki_vrf = get_rpki_vrf(vrfname);

	if (!rpki_vrf)
		return CMD_WARNING;

	cache_list = rpki_vrf->cache_list;
	if (!cache_list || listcount(cache_list) == 0)
		vty_out(vty,
			"Could not start rpki because no caches are configured\n");

	if (!is_running(rpki_vrf)) {
		if (start(rpki_vrf) == ERROR) {
			RPKI_DEBUG("RPKI failed to start");
			return CMD_WARNING;
		}
	}
	return CMD_SUCCESS;
}

/* 【命令】`rpki stop [vrf NAME]`：若会话在运行则 stop 之。返回 CMD_SUCCESS。 */
DEFPY (bgp_rpki_stop,
       bgp_rpki_stop_cmd,
       "rpki stop [vrf NAME$vrfname]",
       RPKI_OUTPUT_STRING
       "start rpki support\n"
       VRF_CMD_HELP_STR)
{
	struct rpki_vrf *rpki_vrf;

	rpki_vrf = get_rpki_vrf(vrfname);

	if (rpki_vrf && is_running(rpki_vrf))
		stop(rpki_vrf);

	return CMD_SUCCESS;
}

/* 【命令】`rpki polling_period (1-86400)`：设置轮询周期。上下文可能来自 RPKI_NODE 或
 * RPKI_VRF_NODE，两种取法都试。无上下文返回 CMD_WARNING_CONFIG_FAILED。 */
DEFPY (rpki_polling_period,
       rpki_polling_period_cmd,
       "rpki polling_period (1-86400)$pp",
       RPKI_OUTPUT_STRING
       "Set polling period\n"
       "Polling period value\n")
{
	struct rpki_vrf *rpki_vrf;

	if (vty->node == RPKI_VRF_NODE)
		rpki_vrf = VTY_GET_CONTEXT_SUB(rpki_vrf);
	else
		rpki_vrf = VTY_GET_CONTEXT(rpki_vrf);

	if (!rpki_vrf)
		return CMD_WARNING_CONFIG_FAILED;

	rpki_vrf->polling_period = pp;
	return CMD_SUCCESS;
}

/* 【命令】`no rpki polling_period`：把轮询周期复位为默认值。 */
DEFUN (no_rpki_polling_period,
       no_rpki_polling_period_cmd,
       "no rpki polling_period [(1-86400)]",
       NO_STR
       RPKI_OUTPUT_STRING
       "Set polling period back to default\n"
       "Polling period value\n")
{
	struct rpki_vrf *rpki_vrf;

	if (vty->node == RPKI_VRF_NODE)
		rpki_vrf = VTY_GET_CONTEXT_SUB(rpki_vrf);
	else
		rpki_vrf = VTY_GET_CONTEXT(rpki_vrf);

	if (!rpki_vrf)
		return CMD_WARNING_CONFIG_FAILED;

	rpki_vrf->polling_period = POLLING_PERIOD_DEFAULT;
	return CMD_SUCCESS;
}

/* 【命令】`rpki expire_interval (600-172800)`：设置数据过期时限；**必须 >= 轮询周期**，
 * 否则报错返回 CMD_WARNING_CONFIG_FAILED。 */
DEFPY (rpki_expire_interval,
       rpki_expire_interval_cmd,
       "rpki expire_interval (600-172800)$tmp",
       RPKI_OUTPUT_STRING
       "Set expire interval\n"
       "Expire interval value\n")
{
	struct rpki_vrf *rpki_vrf;

	if (vty->node == RPKI_VRF_NODE)
		rpki_vrf = VTY_GET_CONTEXT_SUB(rpki_vrf);
	else
		rpki_vrf = VTY_GET_CONTEXT(rpki_vrf);

	if (!rpki_vrf)
		return CMD_WARNING_CONFIG_FAILED;

	if ((unsigned int)tmp >= rpki_vrf->polling_period) {
		rpki_vrf->expire_interval = tmp;
		return CMD_SUCCESS;
	}

	vty_out(vty, "%% Expiry interval must be polling period or larger\n");
	return CMD_WARNING_CONFIG_FAILED;
}

/* 【命令】`no rpki expire_interval`：把过期时限复位为轮询周期的 2 倍（注意不是固定
 * 默认常量，而是跟随当前轮询周期）。 */
DEFUN (no_rpki_expire_interval,
       no_rpki_expire_interval_cmd,
       "no rpki expire_interval [(600-172800)]",
       NO_STR
       RPKI_OUTPUT_STRING
       "Set expire interval back to default\n"
       "Expire interval value\n")
{
	struct rpki_vrf *rpki_vrf;

	if (vty->node == RPKI_VRF_NODE)
		rpki_vrf = VTY_GET_CONTEXT_SUB(rpki_vrf);
	else
		rpki_vrf = VTY_GET_CONTEXT(rpki_vrf);

	if (!rpki_vrf)
		return CMD_WARNING_CONFIG_FAILED;

	rpki_vrf->expire_interval = rpki_vrf->polling_period * 2;
	return CMD_SUCCESS;
}

/* 【命令】`rpki retry_interval (1-7200)`：设置连接重试间隔。 */
DEFPY (rpki_retry_interval,
       rpki_retry_interval_cmd,
       "rpki retry_interval (1-7200)$tmp",
       RPKI_OUTPUT_STRING
       "Set retry interval\n"
       "retry interval value\n")
{
	struct rpki_vrf *rpki_vrf;

	if (vty->node == RPKI_VRF_NODE)
		rpki_vrf = VTY_GET_CONTEXT_SUB(rpki_vrf);
	else
		rpki_vrf = VTY_GET_CONTEXT(rpki_vrf);

	if (!rpki_vrf)
		return CMD_WARNING_CONFIG_FAILED;

	rpki_vrf->retry_interval = tmp;
	return CMD_SUCCESS;
}

/* 【命令】`no rpki retry_interval`：把重试间隔复位为默认值。 */
DEFUN (no_rpki_retry_interval,
       no_rpki_retry_interval_cmd,
       "no rpki retry_interval [(1-7200)]",
       NO_STR
       RPKI_OUTPUT_STRING
       "Set retry interval back to default\n"
       "retry interval value\n")
{
	struct rpki_vrf *rpki_vrf;

	if (vty->node == RPKI_VRF_NODE)
		rpki_vrf = VTY_GET_CONTEXT_SUB(rpki_vrf);
	else
		rpki_vrf = VTY_GET_CONTEXT(rpki_vrf);

	if (!rpki_vrf)
		return CMD_WARNING_CONFIG_FAILED;

	rpki_vrf->retry_interval = RETRY_INTERVAL_DEFAULT;
	return CMD_SUCCESS;
}

/* 【命令】`rpki cache tcp ... preference N`：新增一台 TCP 缓存服务器。先查 preference
 * 是否已存在（存在则拒绝）；add_tcp_cache 成功后，若此前 cache_list 为空（init）则
 * 自动 start。返回 CMD_SUCCESS / CMD_WARNING。 */
DEFPY(rpki_cache_tcp, rpki_cache_tcp_cmd,
      "rpki cache tcp <A.B.C.D|WORD>$cache TCPPORT [source <A.B.C.D>$bindaddr] preference (1-255)",
      RPKI_OUTPUT_STRING
      "Install a cache server to current group\n"
      "Use TCP\n"
      "IP address of cache server\n"
      "Hostname of cache server\n"
      "TCP port number\n"
      "Configure source IP address of RPKI connection\n"
      "Define a Source IP Address\n"
      "Preference of the cache server\n"
      "Preference value\n")
{
	int return_value;
	struct listnode *cache_node;
	struct cache *current_cache;
	struct rpki_vrf *rpki_vrf;
	bool init;

	if (vty->node == RPKI_VRF_NODE)
		rpki_vrf = VTY_GET_CONTEXT_SUB(rpki_vrf);
	else
		rpki_vrf = VTY_GET_CONTEXT(rpki_vrf);

	if (!rpki_vrf)
		return CMD_WARNING_CONFIG_FAILED;

	if (!rpki_vrf || !rpki_vrf->cache_list)
		return CMD_WARNING;

	init = !!list_isempty(rpki_vrf->cache_list);

	for (ALL_LIST_ELEMENTS_RO(rpki_vrf->cache_list, cache_node,
				  current_cache)) {
		if (current_cache->preference == preference) {
			vty_out(vty, "Cache with preference %" PRId64 " is already configured\n",
				preference);
			return CMD_WARNING;
		}
	}

	return_value = add_tcp_cache(rpki_vrf, cache, tcpport, preference,
				     bindaddr_str);

	if (return_value == ERROR) {
		vty_out(vty, "Could not create new rpki cache\n");
		return CMD_WARNING;
	}

	if (init)
		start(rpki_vrf);

	return CMD_SUCCESS;
}

/* 【命令】`rpki cache ssh ... preference N`：新增一台 SSH 缓存服务器。逻辑同
 * rpki_cache_tcp；若编译期不支持 SSH，则提示无法使用并置 return_value=SUCCESS。
 * 首个 cache 加入时自动 start。 */
DEFPY(rpki_cache_ssh, rpki_cache_ssh_cmd,
      "rpki cache ssh <A.B.C.D|WORD>$cache (1-65535)$sshport SSH_UNAME SSH_PRIVKEY [KNOWN_HOSTS_PATH] [source <A.B.C.D>$bindaddr] preference (1-255)",
      RPKI_OUTPUT_STRING
      "Install a cache server to current group\n"
      "Use SSH\n"
      "IP address of cache server\n"
      "Hostname of cache server\n"
      "SSH port number\n"
      "SSH user name\n"
      "Path to own SSH private key\n"
      "Path to the known hosts file\n"
      "Configure source IP address of RPKI connection\n"
      "Define a Source IP Address\n"
      "Preference of the cache server\n"
      "Preference value\n")
{
	int return_value;
	struct listnode *cache_node;
	struct cache *current_cache;
	struct rpki_vrf *rpki_vrf;
	bool init;

	if (vty->node == RPKI_VRF_NODE)
		rpki_vrf = VTY_GET_CONTEXT_SUB(rpki_vrf);
	else
		rpki_vrf = VTY_GET_CONTEXT(rpki_vrf);

	if (!rpki_vrf)
		return CMD_WARNING_CONFIG_FAILED;

	if (!rpki_vrf || !rpki_vrf->cache_list)
		return CMD_WARNING;

	init = !!list_isempty(rpki_vrf->cache_list);

	for (ALL_LIST_ELEMENTS_RO(rpki_vrf->cache_list, cache_node,
				  current_cache)) {
		if (current_cache->preference == preference) {
			vty_out(vty, "Cache with preference %" PRId64 " is already configured\n",
				preference);
			return CMD_WARNING;
		}
	}

#if defined(FOUND_SSH)
	return_value = add_ssh_cache(rpki_vrf, cache, sshport, ssh_uname,
				     ssh_privkey, known_hosts_path, preference,
				     bindaddr_str);
#else
	return_value = SUCCESS;
	vty_out(vty,
		"ssh sockets are not supported. Please recompile rtrlib and frr with ssh support. If you want to use it\n");
#endif

	if (return_value == ERROR) {
		vty_out(vty, "Could not create new rpki cache\n");
		return CMD_WARNING;
	}

	if (init)
		start(rpki_vrf);

	return CMD_SUCCESS;
}

/* 【命令】`no rpki cache <tcp|ssh> ... preference N`：按 preference 删除一台 cache。
 * 若删除后仅剩 0 台（原先仅 1 台）则直接 stop；否则从 rtr_mgr 移除对应组。再
 * listnode_delete + free_cache。返回 CMD_SUCCESS / CMD_WARNING。 */
DEFPY (no_rpki_cache,
       no_rpki_cache_cmd,
       "no rpki cache <tcp|ssh> <A.B.C.D|WORD> <TCPPORT|(1-65535)$sshport SSH_UNAME SSH_PRIVKEY [KNOWN_HOSTS_PATH]> [source <A.B.C.D>$bindaddr] preference (1-255)",
       NO_STR
       RPKI_OUTPUT_STRING
       "Install a cache server to current group\n"
       "Use TCP\n"
       "Use SSH\n"
       "IP address of cache server\n"
       "Hostname of cache server\n"
       "TCP port number\n"
       "SSH port number\n"
       "SSH user name\n"
       "Path to own SSH private key\n"
       "Path to the known hosts file\n"
       "Configure source IP address of RPKI connection\n"
       "Define a Source IP Address\n"
       "Preference of the cache server\n"
       "Preference value\n")
{
	struct cache *cache_p;
	struct list *cache_list = NULL;
	struct rpki_vrf *rpki_vrf;

	if (vty->node == RPKI_VRF_NODE)
		rpki_vrf = VTY_GET_CONTEXT_SUB(rpki_vrf);
	else
		rpki_vrf = VTY_GET_CONTEXT(rpki_vrf);

	if (!rpki_vrf)
		return CMD_WARNING_CONFIG_FAILED;

	cache_list = rpki_vrf->cache_list;
	cache_p = find_cache(preference, cache_list);
	if (!rpki_vrf || !cache_p) {
		vty_out(vty, "Could not find cache with preference %" PRId64 "\n", preference);
		return CMD_WARNING;
	}

	if (is_running(rpki_vrf) && listcount(cache_list) == 1) {
		stop(rpki_vrf);
	} else if (is_running(rpki_vrf)) {
		if (rtr_mgr_remove_group(rpki_vrf->rtr_config, preference) ==
		    RTR_ERROR) {
			vty_out(vty, "Could not remove cache with preference %" PRId64 "\n",
				preference);
			return CMD_WARNING;
		}
	}

	listnode_delete(cache_list, cache_p);
	free_cache(cache_p);

	return CMD_SUCCESS;
}

/* 【命令】`show rpki <prefix-table|prefix-count> [vrf] [json]`：显示缓存的 ROA 表或
 * 其条数。未同步时提示无连接并返回 CMD_WARNING；prefix-count 走 count_only。 */
DEFPY (show_rpki_prefix_table,
       show_rpki_prefix_table_cmd,
       "show rpki <prefix-table|prefix-count>$prefixkind [vrf NAME$vrfname] [json$uj]",
       SHOW_STR
       RPKI_OUTPUT_STRING
       "Show validated prefixes which were received from RPKI Cache\n"
       "Show prefixes count which were received from RPKI Cache\n"
       VRF_CMD_HELP_STR
       JSON_STR)
{
	struct json_object *json = NULL;
	struct rpki_vrf *rpki_vrf;

	if (uj)
		json = json_object_new_object();

	rpki_vrf = get_rpki_vrf(vrfname);
	if (!rpki_vrf) {
		if (uj)
			vty_json(vty, json);
		return CMD_SUCCESS;
	}

	if (is_synchronized(rpki_vrf)) {
		if (strmatch(prefixkind, "prefix-count"))
			print_prefix_table(vty, rpki_vrf, json, true);
		else
			print_prefix_table(vty, rpki_vrf, json, false);
	} else {
		if (json) {
			json_object_string_add(json, "error", "No Connection to RPKI cache server.");
			vty_json(vty, json);
		} else
			vty_out(vty, "No connection to RPKI cache server.\n");
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

/* 【命令】`show rpki as-number <0|ASNUM> [vrf] [json]`：按源 AS 查 ROA 记录。as 为 0
 * 表示 RFC-7607 的 “AS0”（无源 AS 的 ROA）。未同步时返回 CMD_WARNING。 */
DEFPY (show_rpki_as_number,
       show_rpki_as_number_cmd,
       "show rpki as-number <0$zero|ASNUM$by_asn> [vrf NAME$vrfname] [json$uj]",
       SHOW_STR
       RPKI_OUTPUT_STRING
       "Lookup by ASN in prefix table\n"
       "AS Number of 0, see RFC-7607\n"
       "AS Number\n"
       VRF_CMD_HELP_STR
       JSON_STR)
{
	struct json_object *json = NULL;
	struct rpki_vrf *rpki_vrf;
	as_t as;

	if (uj)
		json = json_object_new_object();

	rpki_vrf = get_rpki_vrf(vrfname);
	if (!rpki_vrf) {
		if (uj)
			vty_json(vty, json);
		return CMD_SUCCESS;
	}

	if (!is_synchronized(rpki_vrf)) {
		if (json) {
			json_object_string_add(json, "error", "No Connection to RPKI cache server.");
			vty_json(vty, json);
		} else
			vty_out(vty, "No Connection to RPKI cache server.\n");
		return CMD_WARNING;
	}

	if (zero)
		as = 0;
	else
		as = by_asn;

	print_prefix_table_by_asn(vty, as, rpki_vrf, json);
	return CMD_SUCCESS;
}

/* 【命令】`show rpki prefix <A.B.C.D/M|X:X::X:X/M> [0|ASNUM] [vrf] [json]`：查某前缀
 * （可带源 AS）在 ROA 表里的匹配记录。用 pfx_table_validate_r 取匹配集，再按
 * max_len >= prefixlen 与 AS 条件筛选后打印。未同步或前缀非法时返回 CMD_WARNING。 */
DEFPY (show_rpki_prefix,
       show_rpki_prefix_cmd,
       "show rpki prefix <A.B.C.D/M|X:X::X:X/M> [0$zero|ASNUM$asn] [vrf NAME$vrfname] [json$uj]",
       SHOW_STR
       RPKI_OUTPUT_STRING
       "Lookup IP prefix and optionally ASN in prefix table\n"
       "IPv4 prefix\n"
       "IPv6 prefix\n"
       "AS Number of 0, see RFC-7607\n"
       "AS Number\n"
       VRF_CMD_HELP_STR
       JSON_STR)
{
	json_object *json = NULL;
	json_object *json_records = NULL;
	enum asnotation_mode asnotation;
	struct rpki_vrf *rpki_vrf;
	as_t as;

	if (uj)
		json = json_object_new_object();

	rpki_vrf = get_rpki_vrf(vrfname);

	if (!rpki_vrf || !is_synchronized(rpki_vrf)) {
		if (json) {
			json_object_string_add(json, "error", "No Connection to RPKI cache server.");
			vty_json(vty, json);
		} else
			vty_out(vty, "No Connection to RPKI cache server.\n");
		return CMD_WARNING;
	}

	if (zero)
		as = 0;
	else
		as = asn;

	struct lrtr_ip_addr addr;
	char addr_str[INET6_ADDRSTRLEN];
	size_t addr_len = strchr(prefix_str, '/') - prefix_str;

	memset(addr_str, 0, sizeof(addr_str));
	memcpy(addr_str, prefix_str, addr_len);

	if (lrtr_ip_str_to_addr(addr_str, &addr) != 0) {
		if (json) {
			json_object_string_add(json, "error", "Invalid IP prefix.");
			vty_json(vty, json);
		} else
			vty_out(vty, "Invalid IP prefix\n");
		return CMD_WARNING;
	}

	struct pfx_record *matches = NULL;
	unsigned int match_count = 0;
	enum pfxv_state result;

	if (pfx_table_validate_r(rpki_vrf->rtr_config->pfx_table, &matches,
				 &match_count, as, &addr, prefix->prefixlen,
				 &result) != PFX_SUCCESS) {
		if (json) {
			json_object_string_add(json, "error", "Prefix lookup failed.");
			vty_json(vty, json);
		} else
			vty_out(vty, "Prefix lookup failed\n");
		return CMD_WARNING;
	}


	if (!json) {
		vty_out(vty, "%-40s %s  %s\n", "Prefix", "Prefix Length",
			"Origin-AS");
	} else {
		json_records = json_object_new_array();
		json_object_object_add(json, "prefixes", json_records);
	}

	asnotation = bgp_get_asnotation(bgp_lookup_by_vrf_id(VRF_DEFAULT));
	for (size_t i = 0; i < match_count; ++i) {
		const struct pfx_record *record = &matches[i];

		if (record->max_len >= prefix->prefixlen &&
		    ((as != 0 && (uint32_t)as == record->asn) || asn == 0)) {
			print_record(&matches[i], vty, json_records,
				     asnotation);
		}
	}

	if (json)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】`show rpki cache-server [vrf] [json]`：列出已配置的缓存服务器
 * （host / port / preference / protocol / 可选 source）。 */
DEFPY (show_rpki_cache_server,
       show_rpki_cache_server_cmd,
       "show rpki cache-server [vrf NAME$vrfname] [json$uj]",
       SHOW_STR
       RPKI_OUTPUT_STRING
       "Show configured cache server\n"
       VRF_CMD_HELP_STR
       JSON_STR)
{
	struct json_object *json = NULL;
	struct json_object *json_server = NULL;
	struct json_object *json_servers = NULL;
	struct listnode *cache_node;
	struct cache *cache;
	struct rpki_vrf *rpki_vrf;

	if (uj) {
		json = json_object_new_object();
		json_servers = json_object_new_array();
		json_object_object_add(json, "servers", json_servers);
	}

	rpki_vrf = get_rpki_vrf(vrfname);
	if (!rpki_vrf) {
		if (json)
			vty_json(vty, json);
		return CMD_SUCCESS;
	}

	for (ALL_LIST_ELEMENTS_RO(rpki_vrf->cache_list, cache_node, cache)) {
		if (cache->type == TCP) {
			if (!json) {
				vty_out(vty,
					"host: %s port: %s, preference: %hhu, protocol: tcp",
					cache->tr_config.tcp_config->host,
					cache->tr_config.tcp_config->port,
					cache->preference);
				if (cache->tr_config.tcp_config->bindaddr)
					vty_out(vty, ", source: %s\n",
						cache->tr_config.tcp_config
							->bindaddr);
				else
					vty_out(vty, "\n");
			} else {
				json_server = json_object_new_object();
				json_object_string_add(json_server, "mode",
						       "tcp");
				json_object_string_add(
					json_server, "host",
					cache->tr_config.tcp_config->host);
				json_object_string_add(
					json_server, "port",
					cache->tr_config.tcp_config->port);
				json_object_int_add(json_server, "preference",
						    cache->preference);
				if (cache->tr_config.tcp_config->bindaddr)
					json_object_string_add(json_server,
							       "source",
							       cache->tr_config
								       .tcp_config
								       ->bindaddr);
				json_object_array_add(json_servers,
						      json_server);
			}

#if defined(FOUND_SSH)
		} else if (cache->type == SSH) {
			if (!json) {
				vty_out(vty,
					"host: %s, port: %d, username: %s, server_hostkey_path: %s, client_privkey_path: %s, preference: %hhu, protocol: ssh",
					cache->tr_config.ssh_config->host,
					cache->tr_config.ssh_config->port,
					cache->tr_config.ssh_config->username,
					cache->tr_config.ssh_config
						->server_hostkey_path,
					cache->tr_config.ssh_config
						->client_privkey_path,
					cache->preference);
				if (cache->tr_config.ssh_config->bindaddr)
					vty_out(vty, ", source: %s\n",
						cache->tr_config.ssh_config
							->bindaddr);
				else
					vty_out(vty, "\n");
			} else {
				json_server = json_object_new_object();
				json_object_string_add(json_server, "mode",
						       "ssh");
				json_object_string_add(
					json_server, "host",
					cache->tr_config.ssh_config->host);
				json_object_int_add(
					json_server, "port",
					cache->tr_config.ssh_config->port);
				json_object_string_add(
					json_server, "username",
					cache->tr_config.ssh_config->username);
				json_object_string_add(
					json_server, "serverHostkeyPath",
					cache->tr_config.ssh_config
						->server_hostkey_path);
				json_object_string_add(
					json_server, "clientPrivkeyPath",
					cache->tr_config.ssh_config
						->client_privkey_path);
				json_object_int_add(json_server, "preference",
						    cache->preference);
				if (cache->tr_config.ssh_config->bindaddr)
					json_object_string_add(json_server,
							       "source",
							       cache->tr_config
								       .ssh_config
								       ->bindaddr);
				json_object_array_add(json_servers,
						      json_server);
			}
#endif
		}
	}

	if (json)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】`show rpki cache-connection [vrf] [json]`：显示各缓存服务器的连接状态
 * （每个 cache 的 rtr_socket->state 是否为 RTR_ESTABLISHED）。未同步或取不到组时
 * 给出相应提示。 */
DEFPY (show_rpki_cache_connection,
       show_rpki_cache_connection_cmd,
       "show rpki cache-connection [vrf NAME$vrfname] [json$uj]",
       SHOW_STR
       RPKI_OUTPUT_STRING
       "Show to which RPKI Cache Servers we have a connection\n"
       VRF_CMD_HELP_STR
       JSON_STR)
{
	struct json_object *json = NULL;
	struct json_object *json_conn = NULL;
	struct json_object *json_conns = NULL;
	struct listnode *cache_node;
	struct cache *cache;
	struct rtr_mgr_group *group;
	struct rpki_vrf *rpki_vrf;

	if (uj)
		json = json_object_new_object();

	rpki_vrf = get_rpki_vrf(vrfname);
	if (!rpki_vrf) {
		if (json)
			vty_json(vty, json);
		return CMD_SUCCESS;
	}

	if (!is_synchronized(rpki_vrf)) {
		if (json) {
			json_object_string_add(json, "error", "No connection to RPKI cache server.");
			vty_json(vty, json);
		} else
			vty_out(vty, "No connection to RPKI cache server.\n");

		return CMD_SUCCESS;
	}

	group = get_connected_group(rpki_vrf);
	if (!group) {
		if (json) {
			json_object_string_add(json, "error", "Cannot find a connected group.");
			vty_json(vty, json);
		} else
			vty_out(vty, "Cannot find a connected group.\n");

		return CMD_SUCCESS;
	}

	if (!json) {
		vty_out(vty, "Connected to group %d\n", group->preference);
	} else {
		json_conns = json_object_new_array();
		json_object_int_add(json, "connectedGroup", group->preference);
		json_object_object_add(json, "connections", json_conns);
	}

	for (ALL_LIST_ELEMENTS_RO(rpki_vrf->cache_list, cache_node, cache)) {
		struct tr_tcp_config *tcp_config;
#if defined(FOUND_SSH)
		struct tr_ssh_config *ssh_config;
#endif
		switch (cache->type) {
		case TCP:
			tcp_config = cache->tr_config.tcp_config;

			if (!json) {
				vty_out(vty,
					"rpki tcp cache %s %s pref %hhu%s\n",
					tcp_config->host, tcp_config->port,
					cache->preference,
					cache->rtr_socket->state ==
							RTR_ESTABLISHED
						? " (connected)"
						: "");
			} else {
				json_conn = json_object_new_object();
				json_object_string_add(json_conn, "mode",
						       "tcp");
				json_object_string_add(json_conn, "host",
						       tcp_config->host);
				json_object_string_add(json_conn, "port",
						       tcp_config->port);
				json_object_int_add(json_conn, "preference",
						    cache->preference);
				json_object_string_add(
					json_conn, "state",
					cache->rtr_socket->state ==
							RTR_ESTABLISHED
						? "connected"
						: "disconnected");
				json_object_array_add(json_conns, json_conn);
			}
			break;
#if defined(FOUND_SSH)
		case SSH:
			ssh_config = cache->tr_config.ssh_config;

			if (!json) {
				vty_out(vty,
					"rpki ssh cache %s %u pref %hhu%s\n",
					ssh_config->host, ssh_config->port,
					cache->preference,
					cache->rtr_socket->state ==
							RTR_ESTABLISHED
						? " (connected)"
						: "");
			} else {
				json_conn = json_object_new_object();
				json_object_string_add(json_conn, "mode",
						       "ssh");
				json_object_string_add(json_conn, "host",
						       ssh_config->host);
				json_object_int_add(json_conn, "port",
						    ssh_config->port);
				json_object_int_add(json_conn, "preference",
						    cache->preference);
				json_object_string_add(
					json_conn, "state",
					cache->rtr_socket->state ==
							RTR_ESTABLISHED
						? "connected"
						: "disconnected");
				json_object_array_add(json_conns, json_conn);
			}
			break;
#endif
		default:
			break;
		}
	}

	if (json)
		vty_json(vty, json);

	return CMD_SUCCESS;
}

/* 【命令】`show rpki configuration [vrf] [json]`：显示该 VRF 的 RPKI 总体配置
 * （是否启用、cache 数、三个时间参数）。注意这里用 find_rpki_vrf（不是 get_rpki_vrf），
 * 不做 vrf 存在性校验。 */
DEFPY(show_rpki_configuration, show_rpki_configuration_cmd,
      "show rpki configuration [vrf NAME$vrfname] [json$uj]",
      SHOW_STR RPKI_OUTPUT_STRING
      "Show RPKI configuration\n"
      VRF_CMD_HELP_STR
      JSON_STR)
{
	struct json_object *json = NULL;
	struct rpki_vrf *rpki_vrf;

	if (uj)
		json = json_object_new_object();

	rpki_vrf = find_rpki_vrf(vrfname);
	if (!rpki_vrf) {
		if (uj)
			vty_json(vty, json);
		return CMD_SUCCESS;
	}

	if (uj) {
		json_object_boolean_add(json, "enabled",
					!!listcount(rpki_vrf->cache_list));
		json_object_int_add(json, "serversCount",
				    listcount(rpki_vrf->cache_list));
		json_object_int_add(json, "pollingPeriodSeconds",
				    rpki_vrf->polling_period);
		json_object_int_add(json, "retryIntervalSeconds",
				    rpki_vrf->retry_interval);
		json_object_int_add(json, "expireIntervalSeconds",
				    rpki_vrf->expire_interval);

		vty_json(vty, json);

		return CMD_SUCCESS;
	}

	vty_out(vty, "rpki is %s",
		listcount(rpki_vrf->cache_list) ? "Enabled" : "Disabled");

	if (list_isempty(rpki_vrf->cache_list)) {
		vty_out(vty, "\n");
		return CMD_SUCCESS;
	}

	vty_out(vty, " (%d cache servers configured)",
		listcount(rpki_vrf->cache_list));
	vty_out(vty, "\n");
	vty_out(vty, "\tpolling period %d\n", rpki_vrf->polling_period);
	vty_out(vty, "\tretry interval %d\n", rpki_vrf->retry_interval);
	vty_out(vty, "\texpire interval %d\n", rpki_vrf->expire_interval);

	return CMD_SUCCESS;
}

/* 【退出钩子】config_on_exit —— rpki 配置节点的 node_exit：退出该节点时从 vty 上下文
 * 取回 rpki_vrf，调用 reset(false, rpki_vrf)（非强制，仅在会话未运行时才重启，
 * 避免打断正在跑的连接）。返回 1。 */
static int config_on_exit(struct vty *vty)
{
	struct rpki_vrf *rpki_vrf;

	if (vty->node == RPKI_VRF_NODE)
		rpki_vrf = VTY_GET_CONTEXT_SUB(rpki_vrf);
	else
		rpki_vrf = VTY_GET_CONTEXT(rpki_vrf);

	if (!rpki_vrf)
		return CMD_WARNING_CONFIG_FAILED;

	reset(false, rpki_vrf);
	return 1;
}

/* 【命令】`rpki reset [vrf]`（ENABLE 模式）：强制重启该 VRF 的 RPKI 会话
 * （reset(true)）。返回 CMD_SUCCESS / CMD_WARNING。 */
DEFPY(rpki_reset,
       rpki_reset_cmd,
       "rpki reset [vrf NAME$vrfname]",
       RPKI_OUTPUT_STRING
       "reset rpki\n"
       VRF_CMD_HELP_STR)
{
	struct rpki_vrf *rpki_vrf;

	rpki_vrf = find_rpki_vrf(vrfname);
	if (!rpki_vrf)
		return CMD_WARNING;

	return reset(true, rpki_vrf) == SUCCESS ? CMD_SUCCESS : CMD_WARNING;
}

/* 【命令】`rpki reset`（配置模式，RPKI_NODE / RPKI_VRF_NODE 下）：强制重启会话，
 * 上下文从 vty 取 rpki_vrf（config 模式用普通的或 sub 的）。返回 CMD_SUCCESS /
 * CMD_WARNING。 */
DEFPY (rpki_reset_config_mode,
       rpki_reset_config_mode_cmd,
       "rpki reset",
       RPKI_OUTPUT_STRING
       "reset rpki\n")
{
	struct rpki_vrf *rpki_vrf;

	if (vty->node == RPKI_VRF_NODE)
		rpki_vrf = VTY_GET_CONTEXT_SUB(rpki_vrf);
	else
		rpki_vrf = VTY_GET_CONTEXT(rpki_vrf);

	if (!rpki_vrf)
		return CMD_WARNING_CONFIG_FAILED;

	return reset(true, rpki_vrf) == SUCCESS ? CMD_SUCCESS : CMD_WARNING;
}

/* 【命令】`debug rpki`：开启 RPKI 调试。CONFIG_NODE 下置持久调试 rpki_debug_conf，
 * 否则置终端调试 rpki_debug_term。返回 CMD_SUCCESS。 */
DEFUN (debug_rpki,
       debug_rpki_cmd,
       "debug rpki",
       DEBUG_STR
       "Enable debugging for rpki\n")
{
	if (vty->node == CONFIG_NODE)
		rpki_debug_conf = true;
	else
		rpki_debug_term = true;
	return CMD_SUCCESS;
}

/* 【命令】`no debug rpki`：关闭 RPKI 调试，逻辑与 debug_rpki 相反。 */
DEFUN (no_debug_rpki,
       no_debug_rpki_cmd,
       "no debug rpki",
       NO_STR
       DEBUG_STR
       "Disable debugging for rpki\n")
{
	if (vty->node == CONFIG_NODE)
		rpki_debug_conf = false;
	else
		rpki_debug_term = false;
	return CMD_SUCCESS;
}

/* 【命令】`match rpki <valid|invalid|notfound>`：route-map 的 RPKI 匹配条件（北向
 * YANG 实现）。把 match-condition 创建 / 修改为 frr-bgp-route-map:rpki 并写入参数值
 * （argv[2]->arg），最后提交变更。实际比对逻辑见 route_match。 */
DEFUN_YANG (match_rpki,
       match_rpki_cmd,
       "match rpki <valid|invalid|notfound>",
       MATCH_STR
       RPKI_OUTPUT_STRING
       "Valid prefix\n"
       "Invalid prefix\n"
       "Prefix not found\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:rpki']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:rpki", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, argv[2]->arg);

	return nb_cli_apply_changes(vty, NULL);
}

/* 【命令】`no match rpki <valid|invalid|notfound>`：删除 route-map 的 RPKI 匹配条件
 * （北向 YANG：销毁 match-condition 节点）。 */
DEFUN_YANG (no_match_rpki,
       no_match_rpki_cmd,
       "no match rpki <valid|invalid|notfound>",
       NO_STR
       MATCH_STR
       RPKI_OUTPUT_STRING
       "Valid prefix\n"
       "Invalid prefix\n"
       "Prefix not found\n")
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:rpki']";

	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/* 【注册】install_cli_commands —— 把全部 RPKI 命令安装到各节点：rpki 配置节点与
 * VRF 子节点、ENABLE 下的 start/stop/reset、RPKI_NODE / RPKI_VRF_NODE 下的周期与
 * cache 命令、VIEW 下的各 show、CONFIG/ENABLE 下的 debug，以及 route-map 的
 * match rpki（route_map_install_match + RMAP_NODE 命令）。无返回值。 */
static void install_cli_commands(void)
{
	// TODO: make config write work
	install_node(&rpki_node);
	install_default(RPKI_NODE);
	install_node(&rpki_vrf_node);
	install_default(RPKI_VRF_NODE);
	install_element(CONFIG_NODE, &rpki_cmd);
	install_element(CONFIG_NODE, &no_rpki_cmd);


	install_element(ENABLE_NODE, &bgp_rpki_start_cmd);
	install_element(ENABLE_NODE, &bgp_rpki_stop_cmd);

	/* Install rpki reset command */
	install_element(ENABLE_NODE, &rpki_reset_cmd);
	install_element(RPKI_NODE, &rpki_reset_config_mode_cmd);

	/* Install rpki polling period commands */
	install_element(RPKI_NODE, &rpki_polling_period_cmd);
	install_element(RPKI_NODE, &no_rpki_polling_period_cmd);

	/* Install rpki expire interval commands */
	install_element(RPKI_NODE, &rpki_expire_interval_cmd);
	install_element(RPKI_NODE, &no_rpki_expire_interval_cmd);

	/* Install rpki retry interval commands */
	install_element(RPKI_NODE, &rpki_retry_interval_cmd);
	install_element(RPKI_NODE, &no_rpki_retry_interval_cmd);

	/* Install rpki cache commands */
	install_element(RPKI_NODE, &rpki_cache_tcp_cmd);
	install_element(RPKI_NODE, &rpki_cache_ssh_cmd);
	install_element(RPKI_NODE, &no_rpki_cache_cmd);

	/* RPKI_VRF_NODE commands */
	install_element(VRF_NODE, &rpki_cmd);
	install_element(VRF_NODE, &no_rpki_cmd);
	/* Install rpki reset command */
	install_element(RPKI_VRF_NODE, &rpki_reset_config_mode_cmd);

	/* Install rpki polling period commands */
	install_element(RPKI_VRF_NODE, &rpki_polling_period_cmd);
	install_element(RPKI_VRF_NODE, &no_rpki_polling_period_cmd);

	/* Install rpki expire interval commands */
	install_element(RPKI_VRF_NODE, &rpki_expire_interval_cmd);
	install_element(RPKI_VRF_NODE, &no_rpki_expire_interval_cmd);

	/* Install rpki retry interval commands */
	install_element(RPKI_VRF_NODE, &rpki_retry_interval_cmd);
	install_element(RPKI_VRF_NODE, &no_rpki_retry_interval_cmd);

	/* Install rpki cache commands */
	install_element(RPKI_VRF_NODE, &rpki_cache_tcp_cmd);
	install_element(RPKI_VRF_NODE, &rpki_cache_ssh_cmd);
	install_element(RPKI_VRF_NODE, &no_rpki_cache_cmd);

	/* Install show commands */
	install_element(VIEW_NODE, &show_rpki_prefix_table_cmd);
	install_element(VIEW_NODE, &show_rpki_cache_connection_cmd);
	install_element(VIEW_NODE, &show_rpki_cache_server_cmd);
	install_element(VIEW_NODE, &show_rpki_prefix_cmd);
	install_element(VIEW_NODE, &show_rpki_as_number_cmd);
	install_element(VIEW_NODE, &show_rpki_configuration_cmd);

	/* Install debug commands */
	install_element(CONFIG_NODE, &debug_rpki_cmd);
	install_element(ENABLE_NODE, &debug_rpki_cmd);
	install_element(CONFIG_NODE, &no_debug_rpki_cmd);
	install_element(ENABLE_NODE, &no_debug_rpki_cmd);

	/* Install route match */
	route_map_install_match(&route_match_rpki_cmd);
	install_element(RMAP_NODE, &match_rpki_cmd);
	install_element(RMAP_NODE, &no_match_rpki_cmd);
}

/* 【模块声明】把本文件注册为一个 FRR 模块：名字 bgpd_rpki、版本 0.3.6，init 指向
 * bgp_rpki_module_init（由模块框架在加载时调用）。 */
FRR_MODULE_SETUP(.name = "bgpd_rpki", .version = "0.3.6",
		 .description = "Enable RPKI support for FRR.",
		 .init = bgp_rpki_module_init,
);
