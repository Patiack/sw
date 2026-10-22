// SPDX-License-Identifier: GPL-2.0-or-later
/* Main routine of bgpd.
 * Copyright (C) 1996, 97, 98, 1999 Kunihiro Ishiguro
 */

/* ============================================================================
 * 【文件导读】bgp_main.c —— bgpd 守护进程的入口与生命周期
 * ============================================================================
 * 【职责】这是 bgpd 可执行文件的最外层：解析命令行启动参数 → 初始化 FRR 库与
 * BGP 主结构 bm → 注册信号处理 → 降权 / 设置能力 → 进入事件主循环；退出时按
 * 相反顺序释放资源。
 * 【与 lib 框架的关系】FRR 各守护进程都走同一套 libfrr 框架：FRR_DAEMON_INFO 宏
 * 生成进程元信息(vty 端口、信号表、能力、YANG 模块)，main 里 frr_preinit /
 * frr_opt_add(注册选项) / frr_init / frr_run 串起整个启动流程。所以本文件读起来
 * 很像“模板”，bgpd 特有的东西主要是：bgp_master_init、bgp_init、VRF 生命周期
 * 回调、以及信号处理。
 * 【关键启动参数】(对应下面 longopts 与 frr_opt_add 的说明串)
 *   -p/--bgp_port   : BGP 监听端口；给 0 表示不监听(只作为客户端主动连接)。
 *   -l/--listenon   : 只在指定地址上监听(隐含 -n)。
 *   -n/--no_kernel  : 不把路由安装进内核 FIB。
 *   -Z/--no_zebra   : 不与 zebra 通信(独立运行，无法下发内核 / 做重分发)。
 *   -S/--skip_runas : 跳过能力检查与 uid / gid 切换(调试用，保持 root)。
 *   -e/--ecmp       : 指定 ECMP 多路径条数上限。
 *   -I/--int_num    : 设置 BGP 实例号(label-manager 场景)。
 *   -s/--socket_size: 设置 BGP 邻居 socket 发送缓冲大小。
 *   -x              : 允许用 v4 下一跳建立 v6 邻居。
 * 【阅读建议】main() 是主线：先看它按顺序调用的 init / run；再回来看各静态回调
 * (信号处理、VRF 生命周期)与 bgp_exit 的逆序清理。
 * ============================================================================
 */

#include <zebra.h>

#include <pthread.h>
#include "vector.h"
#include "command.h"
#include "getopt.h"
#include "frrevent.h"
#include <lib/version.h>
#include "memory.h"
#include "prefix.h"
#include "log.h"
#include "privs.h"
#include "sigevent.h"
#include "zclient.h"
#include "routemap.h"
#include "filter.h"
#include "plist.h"
#include "stream.h"
#include "queue.h"
#include "vrf.h"
#include "bfd.h"
#include "libfrr.h"
#include "ns.h"
#include "libagentx.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_mplsvpn.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_dump.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_nexthop.h"
#include "bgpd/bgp_regex.h"
#include "bgpd/bgp_clist.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_errors.h"
#include "bgpd/bgp_filter.h"
#include "bgpd/bgp_zebra.h"
#include "bgpd/bgp_packet.h"
#include "bgpd/bgp_keepalives.h"
#include "bgpd/bgp_network.h"
#include "bgpd/bgp_errors.h"
#include "bgpd/bgp_script.h"
#include "bgpd/bgp_evpn_mh.h"
#include "bgpd/bgp_nhg.h"
#include "bgpd/bgp_routemap_nb.h"
#include "bgpd/bgp_community_alias.h"

/* 【配置写入钩子】VRF 段配置写出时被调用，供各模块往 `vrf <name>` 段落里追加
 * 自己的 BGP 配置行。DEFINE_HOOK 是 FRR 的钩子机制：定义钩子并由各模块挂回调。 */
DEFINE_HOOK(bgp_hook_config_write_vrf, (struct vty *vty, struct vrf *vrf),
	    (vty, vrf));

#ifdef ENABLE_BGP_VNC
#include "bgpd/rfapi/rfapi_backend.h"
#endif

DEFINE_HOOK(bgp_hook_vrf_update, (struct vrf *vrf, bool enabled),
	    (vrf, enabled));

/* bgpd options, we use GNU getopt library. */
/* 【选项表】GNU getopt 的长选项定义：第 4 列的短选项字母要与下面 frr_opt_add 的
 * 选项串 p:l:SnZe:I:s:x 对应；带 ':' 的需要参数。具体含义见文件头导读。 */
static const struct option longopts[] = { { "bgp_port", required_argument, NULL, 'p' },
					  { "listenon", required_argument, NULL, 'l' },
					  { "no_kernel", no_argument, NULL, 'n' },
					  { "skip_runas", no_argument, NULL, 'S' },
					  { "ecmp", required_argument, NULL, 'e' },
					  { "int_num", required_argument, NULL, 'I' },
					  { "no_zebra", no_argument, NULL, 'Z' },
					  { "socket_size", required_argument, NULL, 's' },
					  { "v6-with-v4-nexthops", no_argument, NULL, 'x' },
					  { 0 } };

/* signal definitions */
/* 【信号处理函数】SIGHUP / SIGINT / SIGUSR1 的处理入口声明(实现见下)。 */
void sighup(void);
void sigint(void);
void sigusr1(void);

static void bgp_exit(int);
static void bgp_vrf_terminate(void);

/* 【信号分发表】把信号映射到处理函数：SIGHUP→sighup(仅记日志并忽略)、
 * SIGUSR1→sigusr1(轮转日志)、SIGINT 与 SIGTERM→sigint(优雅退出)。 */
static struct frr_signal_t bgp_signals[] = {
	{
		.signal = SIGHUP,
		.handler = &sighup,
	},
	{
		.signal = SIGUSR1,
		.handler = &sigusr1,
	},
	{
		.signal = SIGINT,
		.handler = &sigint,
	},
	{
		.signal = SIGTERM,
		.handler = &sigint,
	},
};

/* privileges */
/* 【降权与能力】bgpd 启动后一般从 root 降到 FRR_USER / FRR_GROUP，并仅保留必要
 * 的 Linux capability：ZCAP_BIND(绑定特权端口)、ZCAP_NET_RAW(原始套接字)、
 * ZCAP_NET_ADMIN(网络管理)、ZCAP_SYS_ADMIN。cap_num_i=0 表示不限制上限。-S 选项
 * 会跳过这一切(保持 root，调试用)。 */
static zebra_capabilities_t _caps_p[] = {ZCAP_BIND, ZCAP_NET_RAW,
					 ZCAP_NET_ADMIN, ZCAP_SYS_ADMIN};

struct zebra_privs_t bgpd_privs = {
#if defined(FRR_USER) && defined(FRR_GROUP)
	.user = FRR_USER,
	.group = FRR_GROUP,
#endif
#ifdef VTY_GROUP
	.vty_group = VTY_GROUP,
#endif
	.caps_p = _caps_p,
	.cap_num_p = array_size(_caps_p),
	.cap_num_i = 0,
};

static struct frr_daemon_info bgpd_di;

/* SIGHUP handler. */
/* 【SIGHUP】收到 SIGHUP 时仅打印日志并忽略——bgpd 不通过发 SIGHUP 来重载配置。 */
void sighup(void)
{
	zlog_info("SIGHUP received, ignoring");
}

/* SIGINT handler. */
/* 【优雅退出】SIGINT / SIGTERM 的处理：置终止标志、停 BFD、bgp_terminate 拆除
 * 邻居与路由、bgp_exit 逆序清理；随后清连接 FIFO、取消事件、销毁互斥锁并 exit(0)。
 * 中间那几步放在 bgp_exit 之后，是因为清理时可能会从 connection_fifo 摘除元素。 */
FRR_NORETURN void sigint(void)
{
	zlog_notice("Terminating on signal");
	assert(bm->terminating == false);
	bm->terminating = true;	/* global flag that shutting down */

	/* Disable BFD events to avoid wasting processing. */
	bfd_protocol_integration_set_shutdown(true);

	bgp_terminate();

	bgp_exit(0);

	/*
	 * This is being done after bgp_exit because items may be removed
	 * from the connection_fifo
	 */
	peer_connection_fifo_fini(&bm->connection_fifo);
	event_cancel(&bm->e_process_packet);
	pthread_mutex_destroy(&bm->peer_connection_mtx);

	exit(0);
}

/* SIGUSR1 handler. */
/* 【SIGUSR1】收到后轮转日志文件(zlog_rotate)。 */
void sigusr1(void)
{
	zlog_rotate();
}

/*
  Try to free up allocations we know about so that diagnostic tools such as
  valgrind are able to better illuminate leaks.

  Zebra route removal and protocol teardown are not meant to be done here.
  For example, "retain_mode" may be set.
*/
/* 【退出清理】按“初始化顺序的逆序”释放：关 socket、删各 BGP 实例(保留 default /
 * evpn 的处理顺序)、停 EVPN-MH 与 NHG、收尾 zebra 通告、dump 收尾、拆路由 / 属性 /
 * 标签表、停 I/O 线程、拆 ACL / AS / 前缀 / 团体列表、拆 VRF 与 zebra 连接，最后
 * 释放内存池与全局结构 bm 并 exit。仅允许在干净退出(status==0)时调用(见上方英文
 * 注释：zebra 路由删除与协议拆除不在此处做，例如 retain_mode 场景)。 */
static FRR_NORETURN void bgp_exit(int status)
{
	struct bgp *bgp, *bgp_default, *bgp_evpn;
	struct listnode *node, *nnode;

	/* it only makes sense for this to be called on a clean exit */
	assert(status == 0);

	frr_early_fini();

	bgp_close();

	bgp_default = bgp_get_default();
	bgp_evpn = bgp_get_evpn();

	/* reverse bgp_master_init */
	for (ALL_LIST_ELEMENTS(bm->bgp, node, nnode, bgp)) {
		if (bgp_default == bgp || bgp_evpn == bgp)
			continue;
		bgp_delete(bgp);
	}
	if (bgp_evpn && bgp_evpn != bgp_default)
		bgp_delete(bgp_evpn);
	if (bgp_default)
		bgp_delete(bgp_default);

	bgp_evpn_mh_finish();
	bgp_nhg_finish();

	zebra_announce_fini(&bm->zebra_announce_head);
	zebra_l2_vni_fini(&bm->zebra_l2_vni_head);

	/* reverse bgp_dump_init */
	bgp_dump_finish();

	/* BGP community aliases */
	bgp_community_alias_finish();

	/* reverse bgp_route_init */
	bgp_route_finish();

	/* cleanup route maps */
	bgp_route_map_terminate();

	/* reverse bgp_attr_init */
	bgp_attr_finish();

	/* reverse bgp_labels_init */
	bgp_labels_finish();

	/* stop pthreads */
	bgp_pthreads_finish();

	/* reverse access_list_init */
	access_list_add_hook(NULL);
	access_list_delete_hook(NULL);
	access_list_reset();

	/* reverse bgp_filter_init */
	as_list_add_hook(NULL);
	as_list_delete_hook(NULL);
	bgp_filter_reset();

	/* reverse prefix_list_init */
	prefix_list_add_hook(NULL);
	prefix_list_delete_hook(NULL);
	prefix_list_reset();

	/* reverse community_list_init */
	community_list_terminate(bgp_clist);

	bgp_vrf_terminate();
#ifdef ENABLE_BGP_VNC
	vnc_zebra_destroy();
#endif
	bgp_zebra_destroy();

	bf_free(bm->rd_idspace);
	list_delete(&bm->bgp);
	list_delete(&bm->addresses);

	bgp_lp_finish();

	memset(bm, 0, sizeof(*bm));

	frr_fini();
	exit(status);
}

/* 【VRF 新建】zebra 侧新建 VRF 时回调，这里仅打印调试日志。 */
static int bgp_vrf_new(struct vrf *vrf)
{
	if (BGP_DEBUG(zebra, ZEBRA))
		zlog_debug("VRF Created: %s(%u)", vrf->name, vrf->vrf_id);

	return 0;
}

/* 【VRF 删除】仅打印调试日志。 */
static int bgp_vrf_delete(struct vrf *vrf)
{
	if (BGP_DEBUG(zebra, ZEBRA))
		zlog_debug("VRF Deletion: %s(%u)", vrf->name, vrf->vrf_id);

	return 0;
}

/* 【VRF 使能(up)】VRF 变为可用时调用：若已有同名 BGP 实例且其 vrf_id 变化，则把
 * 实例与 VRF 关联、更新发给 zebra 的 GR 能力、按新 VRF 重建监听 socket、置实例为
 * up、触发 vrf_update 钩子，并重算 VPN / VRF 之间的标签、SID 与路由泄漏(四个方向 ×
 * v4 / v6)。 */
static int bgp_vrf_enable(struct vrf *vrf)
{
	struct bgp *bgp;
	vrf_id_t old_vrf_id;
	int ret = BGP_GR_FAILURE;
	(void)ret; /* for unused variable warning */

	if (BGP_DEBUG(zebra, ZEBRA))
		zlog_debug("VRF enable add %s id %u", vrf->name, vrf->vrf_id);

	bgp = bgp_lookup_by_name(vrf->name);
	if (bgp && bgp->vrf_id != vrf->vrf_id) {
		old_vrf_id = bgp->vrf_id;
		/* We have instance configured, link to VRF and make it "up". */
		bgp_vrf_link(bgp, vrf);

		VTY_BGP_GR_ROUTER_DETECT_AND_SEND_CAPABILITY_TO_ZEBRA(bgp, bgp->peer, ret);

		bgp_handle_socket(bgp, vrf, old_vrf_id, true);
		bgp_instance_up(bgp);
		hook_call(bgp_hook_vrf_update, vrf, true);
		vpn_leak_zebra_vrf_label_update(bgp, AFI_IP);
		vpn_leak_zebra_vrf_label_update(bgp, AFI_IP6);
		vpn_leak_zebra_vrf_sid_update(bgp, AFI_IP);
		vpn_leak_zebra_vrf_sid_update(bgp, AFI_IP6);
		vpn_leak_postchange(BGP_VPN_POLICY_DIR_TOVPN, AFI_IP,
				    bgp_get_default(), bgp);
		vpn_leak_postchange(BGP_VPN_POLICY_DIR_FROMVPN, AFI_IP,
				    bgp_get_default(), bgp);
		vpn_leak_postchange(BGP_VPN_POLICY_DIR_TOVPN, AFI_IP6,
				    bgp_get_default(), bgp);
		vpn_leak_postchange(BGP_VPN_POLICY_DIR_FROMVPN, AFI_IP6,
				    bgp_get_default(), bgp);
	}

	return 0;
}

/* 【VRF 关闭(down)】与 enable 相反：对默认 VRF 直接返回；否则撤标签、回退 VPN 泄漏、
 * 关闭监听 socket、置实例为 down、解除实例与 VRF 的关联并触发钩子。注意这是回调，
 * VRF 本体由调用方负责删除(见函数末英文注释)。 */
static int bgp_vrf_disable(struct vrf *vrf)
{
	struct bgp *bgp;

	if (vrf->vrf_id == VRF_DEFAULT)
		return 0;

	if (BGP_DEBUG(zebra, ZEBRA))
		zlog_debug("VRF disable %s id %d", vrf->name, vrf->vrf_id);

	bgp = bgp_lookup_by_name_filter(vrf->name, false);
	if (bgp) {

		vpn_leak_zebra_vrf_label_withdraw(bgp, AFI_IP);
		vpn_leak_zebra_vrf_label_withdraw(bgp, AFI_IP6);
		vpn_leak_prechange(BGP_VPN_POLICY_DIR_TOVPN, AFI_IP,
				   bgp_get_default(), bgp);
		vpn_leak_prechange(BGP_VPN_POLICY_DIR_FROMVPN, AFI_IP,
				   bgp_get_default(), bgp);
		vpn_leak_prechange(BGP_VPN_POLICY_DIR_TOVPN, AFI_IP6,
				   bgp_get_default(), bgp);
		vpn_leak_prechange(BGP_VPN_POLICY_DIR_FROMVPN, AFI_IP6,
				   bgp_get_default(), bgp);

		bgp_handle_socket(bgp, vrf, VRF_UNKNOWN, false);
		/* We have instance configured, unlink from VRF and make it
		 * "down". */
		bgp_instance_down(bgp);
		bgp_vrf_unlink(bgp, vrf);
		hook_call(bgp_hook_vrf_update, vrf, false);
	}

	/* Note: This is a callback, the VRF will be deleted by the caller. */
	return 0;
}

/* 【配置写出】遍历所有 VRF，非默认 VRF 写出 `vrf <name>` 段(默认 VRF 只写一个
 * `!` 分隔)，并调用 bgp_hook_config_write_vrf 让各模块补充内容，最后写 exit-vrf。 */
static int bgp_vrf_config_write(struct vty *vty)
{
	struct vrf *vrf;

	RB_FOREACH (vrf, vrf_name_head, &vrfs_by_name) {
		if (vrf->vrf_id == VRF_DEFAULT) {
			vty_out(vty, "!\n");
			continue;
		}
		vty_out(vty, "vrf %s\n", vrf->name);

		hook_call(bgp_hook_config_write_vrf, vty, vrf);

		vty_out(vty, "exit-vrf\n!\n");
	}

	return 0;
}

/* 【VRF 初始化】注册 VRF 的 new / enable / disable / delete 生命周期回调，并注册
 * 配置写出函数 vrf_cmd_init(bgp_vrf_config_write)。 */
static void bgp_vrf_init(void)
{
	vrf_init(bgp_vrf_new, bgp_vrf_enable, bgp_vrf_disable, bgp_vrf_delete);
	vrf_cmd_init(bgp_vrf_config_write);
}

/* 【VRF 收尾】注销 VRF 子系统(vrf_terminate)。 */
static void bgp_vrf_terminate(void)
{
	vrf_terminate();
}

/* 【YANG 模块】bgpd 加载的北向模型：filter / interface / route-map / vrf，以及
 * BGP 自己扩展的 route-map 模型；供 mgmtd 等北向管理组件使用。 */
static const struct frr_yang_module_info *const bgpd_yang_modules[] = {
	&frr_filter_info,
	&frr_interface_info,
	&frr_route_map_info,
	&frr_vrf_info,
	&frr_bgp_route_map_info,
};

/* 【进程元信息】FRR_DAEMON_INFO 生成本守护进程的描述结构：vty 端口、帮助串、
 * 信号表、privs、YANG 模块等，供 libfrr 框架在启动 / 帮助 / 退出时使用。 */
/* clang-format off */
FRR_DAEMON_INFO(bgpd, BGP,
	.vty_port = BGP_VTY_PORT,
	.proghelp = "Implementation of the BGP routing protocol.",

	.signals = bgp_signals,
	.n_signals = array_size(bgp_signals),

	.privs = &bgpd_privs,

	.yang_modules = bgpd_yang_modules,
	.n_yang_modules = array_size(bgpd_yang_modules),
);
/* clang-format on */

#define DEPRECATED_OPTIONS ""

/* Main routine of bgpd. Treatment of argument and start bgp finite
   state machine is handled at here. */
/* 【入口】bgpd 主函数：解析参数 → bgp_master_init 建主结构 bm → 设置各类开关(端口 /
 * 是否连 zebra / 是否装 FIB / GR) → 初始化各子系统 → 拼启动信息 → frr_config_fork
 * 后台化 → 应用运行配置 → 启动 I/O 线程 → frr_run 进入主循环。下面分块注明。 */
int main(int argc, char **argv)
{
	int opt;
	int tmp_port;

	int bgp_port = BGP_PORT_DEFAULT;
	struct list *addresses = list_new();
	int no_fib_flag = 0;
	int no_zebra_flag = 0;
	int skip_runas = 0;
	int instance = 0;
	int buffer_size = BGP_SOCKET_SNDBUF_SIZE;
	char *address;
	struct listnode *node;
	bool v6_with_v4_nexthops = false;

	addresses->cmp = (int (*)(void *, void *))strcmp;

	/* 【预初始化】建立 libfrr 运行时(解析内建参数、准备日志与内存框架)。 */
	frr_preinit(&bgpd_di, argc, argv);
	/* 【注册选项】把短选项串(带 ':' 表示需参数)与长选项表交给框架，并给出帮助文本；
	 * 之后 frr_getopt 才能解析它们。 */
	frr_opt_add("p:l:SnZe:I:s:x" DEPRECATED_OPTIONS, longopts,
		    "  -p, --bgp_port           Set BGP listen port number (0 means do not listen).\n"
		    "  -l, --listenon           Listen on specified address (implies -n)\n"
		    "  -n, --no_kernel          Do not install route to kernel.\n"
		    "  -Z, --no_zebra           Do not communicate with Zebra.\n"
		    "  -S, --skip_runas         Skip capabilities checks, and changing user and group IDs.\n"
		    "  -e, --ecmp               Specify ECMP to use.\n"
		    "  -I, --int_num            Set instance number (label-manager)\n"
		    "  -s, --socket_size        Set BGP peer socket send buffer size\n"
		    "  -x, --v6-with-v4-nexthop Allow BGP to form v6 neighbors using v4 nexthops\n");

	/* Command line argument treatment. */
	/* 【解析循环】逐个取参数；命中已废弃选项(opt<128 且在 DEPRECATED_OPTIONS 中)
	 * 只报错并跳过；随后 switch 分派到各 case 记录配置(真正生效在下面的 bm 设置)。 */
	while (1) {
		opt = frr_getopt(argc, argv, 0);

		if (opt && opt < 128 && strchr(DEPRECATED_OPTIONS, opt)) {
			fprintf(stderr,
				"The -%c option no longer exists.\nPlease refer to the manual.\n",
				opt);
			continue;
		}

		if (opt == EOF)
			break;

		switch (opt) {
		case 0:
			break;
		case 'p':
			tmp_port = atoi(optarg);
			if (tmp_port < 0 || tmp_port > 0xffff)
				bgp_port = BGP_PORT_DEFAULT;
			else
				bgp_port = tmp_port;
			break;
		case 'e': {
			unsigned long int parsed_multipath =
				strtoul(optarg, NULL, 10);
			if (parsed_multipath == 0
			    || parsed_multipath > MULTIPATH_NUM
			    || parsed_multipath > UINT_MAX) {
				flog_err(
					EC_BGP_MULTIPATH,
					"Multipath Number specified must be less than %u and greater than 0",
					MULTIPATH_NUM);
				return 1;
			}
			multipath_num = parsed_multipath;
			break;
		}
		case 'l':
			listnode_add_sort_nodup(addresses, optarg);
			break;
		case 'n':
			no_fib_flag = 1;
			break;
		case 'Z':
			no_zebra_flag = 1;
			break;
		case 'S':
			skip_runas = 1;
			break;
		case 'I':
			instance = atoi(optarg);
			if (instance > (unsigned short)-1)
				flog_err(EC_BGP_INVALID_BGP_INSTANCE_ID,
					 "Instance %i out of range (0..%u)", instance,
					 (unsigned short)-1);
			break;
		case 's':
			buffer_size = atoi(optarg);
			break;
		case 'x':
			v6_with_v4_nexthops = true;
			break;
		default:
			frr_help_exit(1);
		}
	}
	if (skip_runas)
		/* 【-S 跳降权】把 privs 结构清零，框架便不再切 uid/gid、不设能力。 */
		memset(&bgpd_privs, 0, sizeof(bgpd_privs));

	/* BGP master init. */
	/* 【建主结构】bgp_master_init 分配全局 bm，并把监听地址列表与 socket 缓冲
	 * 大小记进去；frr_init() 完成 libfrr 初始化并返回事件主循环 master。 */
	bgp_master_init(frr_init(), buffer_size, addresses);

	bm->startup_time = monotime(NULL);
	bm->port = bgp_port;
	bm->v6_with_v4_nexthops = v6_with_v4_nexthops;
	/* 【选项开关】端口 0 → 设为“不监听”；-n 或 -Z → “不装 FIB”；-Z → “不连
	 * zebra”；框架带 --graceful-restart → 置 GR 标志。 */
	if (bgp_port == 0)
		bgp_option_set(BGP_OPT_NO_LISTEN);
	if (no_fib_flag || no_zebra_flag)
		bgp_option_set(BGP_OPT_NO_FIB);
	if (no_zebra_flag)
		bgp_option_set(BGP_OPT_NO_ZEBRA);
	if (bgpd_di.graceful_restart)
		SET_FLAG(bm->flags, BM_FLAG_GRACEFUL_RESTART);

	/* 【子系统初始化】错误码表、agentx、VRF 回调；脚本支持(可选)与 BGP 本体
	 * (bgp_init：建默认实例、注册 vty/命令、zebra 连接等)。 */
	bgp_error_init();
	/* Initializations. */
	libagentx_init();
	bgp_vrf_init();


#ifdef HAVE_SCRIPTING
	bgp_script_init();
#endif

	/* BGP related initialization.  */
	bgp_init((unsigned short)instance);

	/* 【启动信息】拼出 "bgp@<all>:port" 或逐个 "bgp@addr:port"，写入 startinfo，
	 * 显示在如 show daemons 的界面里。 */
	if (list_isempty(bm->addresses)) {
		snprintf(bgpd_di.startinfo, sizeof(bgpd_di.startinfo),
			 ", bgp@<all>:%d", bm->port);
	} else {
		for (ALL_LIST_ELEMENTS_RO(bm->addresses, node, address))
			snprintf(bgpd_di.startinfo + strlen(bgpd_di.startinfo),
				 sizeof(bgpd_di.startinfo)
					 - strlen(bgpd_di.startinfo),
				 ", bgp@%s:%d", address, bm->port);
	}

	/* 【接口初始化】注册接口相关回调(与 zebra 同步接口状态)。 */
	bgp_if_init();

	frr_config_fork();
	/* must be called after fork() */
	/* 【应用运行配置】必须在 fork 之后调用，把运行时配置(如 GR)应用上。 */
	bgp_gr_apply_running_config();
	/* 【启动 I/O 线程】拉起 I/O / keepalive / 等工作线程。 */
	bgp_pthreads_run();
	/* 【主循环】进入事件主循环，直到收到退出信号。 */
	frr_run(bm->master);

	/* Not reached. */
	return 0;
}
