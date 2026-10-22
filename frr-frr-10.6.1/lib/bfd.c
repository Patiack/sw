// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * bfd.c: BFD handling routines
 *
 * @copyright Copyright (C) 2015 Cumulus Networks, Inc.
 */

/*
 * 文件导读：bfd.c —— BFD 协议集成库的实现（客户端侧，供各守护进程链接）
 *
 * 一、这个文件在 FRR 里干什么
 *   实现 lib/bfd.h 声明的 API。真正的 BFD 状态机（RFC 5880 的 Down / Init /
 *   Up / AdminDown 迁移、收发控制报文、定时器）由 bfdd 守护进程完成；本文件：
 *     1) 维护应用侧的“会话参数”对象，供 BGP / OSPF 等配置；
 *     2) 把这些参数经 zebra（zclient）下发为 bfdd 的注册/更新/注销命令；
 *     3) 接收 bfdd 回传的会话状态并在状态变化时回调应用；
 *     4) 通过 NHT（nexthop tracking）实现自动源地址选择。
 *
 * 二、核心数据结构
 *   struct bfd_sessions_global bsglobal：文件级单例，聚合会话列表、源地址
 *       缓存、事件循环与 zebra 客户端指针、调试/关闭标志。
 *   struct bfd_session_params：单会话参数 + 状态 + 回调（对应用不透明）。
 *   struct bfd_source_cache：自动源地址选择的结果缓存（按目的前缀去重）。
 *   enum bfd_session_event：安装/卸载/待有效安装三种动作。
 *
 * 三、与其他模块的关系
 *   - zclient.h：报文经 zebra 转 bfdd；
 *   - bfdd/ptm_adapter.c：bfdd 侧的对端解析（见 zclient_bfd_command 注释）；
 *   - NHT：bfd_nht_update 处理 ZEBRA_NEXTHOP_UPDATE，驱动源地址更新；
 *   - frrevent：event_add_event 实现延迟安装，libfrr 的 frr_fini 钩子做收尾。
 *
 * 四、阅读顺序
 *   先看结构体定义 -> 参数设置器 set_XXX -> _bfd_sess_send（真正下发）
 *   -> zclient_bfd_session_update（状态回传）与 zclient_bfd_session_replay
 *   （重连重放）-> 末尾的自动源地址选择（bfd_source_cache_XXX 与 bfd_nht_update）。
 *
 * 五、关键“坑”
 *   - set_XXX 改动关键字段会先移除旧会话，必须再次 install；
 *   - install 延迟生效（命令批次结束），一次批量只执行一次；
 *   - 关闭中（shutting_down）会抑制增删消息，避免回调风暴；
 *   - 源地址缓存有引用计数，session 与 cache 的配对必须成对 get / put。
 */
#include <zebra.h>

#include "command.h"
#include "memory.h"
#include "prefix.h"
#include "frrevent.h"
#include "stream.h"
#include "vrf.h"
#include "zclient.h"
#include "libfrr.h"
#include "table.h"
#include "vty.h"
#include "lib/json.h"
#include "bfd.h"
#include "bfdd/bfd.h"

/*
 * 内存类型定义：BFD 会话参数对象与源地址缓存对象各用一个内存池，
 * 便于内存统计与泄漏排查。
 */
DEFINE_MTYPE_STATIC(LIB, BFD_INFO, "BFD info");
DEFINE_MTYPE_STATIC(LIB, BFD_SOURCE, "BFD source cache");

/**
 * BFD protocol integration configuration.
 */

/** Events definitions. */
/*
 * 会话事件/待执行动作枚举。
 * BSE_UNINSTALL: 需要卸载会话；
 * BSE_INSTALL  : 需要安装/更新会话；
 * BSE_VALID_FOR_INSTALL: 本应安装但因与 zebra 通信失败而未装，待重连后重试。
 */
enum bfd_session_event {
	/** Remove the BFD session configuration. */
	BSE_UNINSTALL,
	/** Install the BFD session configuration. */
	BSE_INSTALL,
	/** We should install but it couldn't because of a error talking to zebra */
	BSE_VALID_FOR_INSTALL,
};

/**
 * BFD source selection result cache.
 *
 * This structure will keep track of the result based on the destination
 * prefix. When the result changes all related BFD sessions with automatic
 * source will be updated.
 */
/*
 * 自动源地址选择的结果缓存（按 VRF + 目的前缀去重）。
 * vrf_id : 所属 VRF。
 * address: 目的网络地址（查表 key）。
 * source : 解析出的源地址。
 * valid  : 源地址是否有效。
 * refcount: 引用该缓存的会话数量，归零才释放。
 * 多个目标地址相同的会话共享同一份缓存，NHT 更新时统一刷新。
 */
struct bfd_source_cache {
	/** Address VRF belongs. */
	vrf_id_t vrf_id;
	/** Destination network address. */
	struct prefix address;
	/** Source selected. */
	struct prefix source;
	/** Is the source address valid? */
	bool valid;
	/** BFD sessions using this. */
	size_t refcount;

	SLIST_ENTRY(bfd_source_cache) entry;
};
/* 源地址缓存链表头类型；SLIST 为单向链表，头部插入/删除代价 O(1)。 */
SLIST_HEAD(bfd_source_list, bfd_source_cache);

/**
 * Data structure to do the necessary tricks to hide the BFD protocol
 * integration internals.
 */
/*
 * 单个 BFD 会话的应用侧句柄（对应用不透明）。
 * args      : 要下发给 bfdd 的会话参数（地址/接口/VRF/profile/定时器等）。
 * bss       : 会话状态快照（供回调与查询）。
 * updatecb  : 状态变化回调；arg: 回调自定义数据。
 * lastev    : 当前命令批次结束后要执行的动作（install / uninstall / 待安装）。
 * installev : 延迟安装事件句柄，多次 install 只保留一个。
 * installed : 是否已成功在 bfdd 侧安装。
 * auto_source / source_cache: 自动源地址选择开关与缓存引用。
 * entry     : 挂入全局列表 bsglobal.bsplist 的链表节点。
 */
struct bfd_session_params {
	/** Contains the session parameters and more. */
	struct bfd_session_arg args;
	/** Contains the session state. */
	struct bfd_session_status bss;
	/** Protocol implementation status update callback. */
	bsp_status_update updatecb;
	/** Protocol implementation custom data pointer. */
	void *arg;

	/**
	 * Next event.
	 *
	 * This variable controls what action to execute when the command batch
	 * finishes. Normally we'd use `event_add_event` value, however since
	 * that function is going to be called multiple times and the value
	 * might be different we'll use this variable to keep track of it.
	 */
	enum bfd_session_event lastev;
	/**
	 * BFD session configuration event.
	 *
	 * Multiple actions might be asked during a command batch (either via
	 * configuration load or northbound batch), so we'll use this to
	 * install/uninstall the BFD session parameters only once.
	 */
	struct event *installev;

	/** BFD session installation state. */
	bool installed;

	/** Automatic source selection. */
	bool auto_source;
	/** Currently selected source. */
	struct bfd_source_cache *source_cache;

	/** Global BFD paramaters list. */
	TAILQ_ENTRY(bfd_session_params) entry;
};

/*
 * 全局单例，聚合 BFD 集成库的全部运行状态。
 * bsplist    : 所有会话参数对象列表（TAILQ）。
 * source_list: 自动源地址缓存列表（SLIST）。
 * tm         : 事件循环指针；zc: zebra 客户端指针。
 * debugging  : 调试开关；shutting_down: 关闭中开关。
 */
struct bfd_sessions_global {
	/**
	 * Global BFD session parameters list for (re)installation and update
	 * without code duplication among daemons.
	 */
	TAILQ_HEAD(bsplist, bfd_session_params) bsplist;
	/** BFD automatic source selection cache. */
	struct bfd_source_list source_list;

	/** Pointer to FRR's event manager. */
	struct event_loop *tm;
	/** Pointer to zebra client data structure. */
	struct zclient *zc;

	/** Debugging state. */
	bool debugging;
	/** Is shutting down? */
	bool shutting_down;
};

/** Global configuration variable. */
/* 全局单例实例（文件私有）。 */
static struct bfd_sessions_global bsglobal;

/** Global empty address for IPv4/IPv6. */
/* 全零 IPv6 地址常量，用于判断地址是否“未配置”及快速比较。 */
static const struct in6_addr i6a_zero;

/*
 * Prototypes
 */

/*
 * 前向声明：自动源地址缓存的取用/归还（实现见文件末尾），
 * 供前面的地址/VRF 设置器调用。
 */
static void bfd_source_cache_get(struct bfd_session_params *session);
static void bfd_source_cache_put(struct bfd_session_params *session);

/*
 * bfd_get_peer_info - Extract the Peer information for which the BFD session
 *                     went down from the message sent from Zebra to clients.
 */
/*
 * 从 zebra 转发的 BFD 状态消息中解析出对端/本地信息。
 * s          : 输入流；dp / sp: 输出目的/源前缀；status: 输出状态；
 * remote_cbit: 输出远端 C 位；vrf_id: 用于查接口；bfd_name: 输出会话名。
 * 返回接口指针（找不到接口时返回 NULL，并可能中途退出）。
 * 关键坑: 一旦流读取出错（stream_failure）或接口查不到，必须把 dp / sp
 *         清零，否则调用者会拿随机数据当有效地址使用。
 */
static struct interface *bfd_get_peer_info(struct stream *s, struct prefix *dp, struct prefix *sp,
					   int *status, int *remote_cbit, vrf_id_t vrf_id,
					   char *bfd_name)
{
	unsigned int ifindex;
	struct interface *ifp = NULL;
	int plen;
	int local_remote_cbit;
	uint8_t bfd_name_len = 0;

	/*
	 * If the ifindex lookup fails the
	 * rest of the data in the stream is
	 * not read.  All examples of this function
	 * call immediately use the dp->family which
	 * is not good.  Ensure we are not using
	 * random data
	 */
	memset(dp, 0, sizeof(*dp));
	memset(sp, 0, sizeof(*sp));

	/* Get interface index. */
	STREAM_GETL(s, ifindex);

	/* Lookup index. */
	if (ifindex != 0) {
		ifp = if_lookup_by_index(ifindex, vrf_id);
		if (ifp == NULL) {
			if (bsglobal.debugging)
				zlog_debug(
					"%s: Can't find interface by ifindex: %d ",
					__func__, ifindex);
			return NULL;
		}
	}

	/* Fetch destination address. */
	STREAM_GETC(s, dp->family);

	plen = prefix_blen(dp);
	STREAM_GET(&dp->u.prefix, s, plen);
	STREAM_GETC(s, dp->prefixlen);

	/* Get BFD status. */
	STREAM_GETL(s, (*status));

	STREAM_GETC(s, sp->family);

	plen = prefix_blen(sp);
	STREAM_GET(&sp->u.prefix, s, plen);
	STREAM_GETC(s, sp->prefixlen);

	STREAM_GETC(s, local_remote_cbit);
	if (remote_cbit)
		*remote_cbit = local_remote_cbit;

	STREAM_GETC(s, bfd_name_len);
	if (bfd_name_len) {
		STREAM_GET(bfd_name, s, bfd_name_len);
		*(bfd_name + bfd_name_len) = 0;
	}

	return ifp;

stream_failure:
	/*
	 * Clean dp and sp because caller
	 * will immediately check them valid or not
	 */
	memset(dp, 0, sizeof(*dp));
	memset(sp, 0, sizeof(*sp));
	return NULL;
}

/*
 * bfd_get_status_str - Convert BFD status to a display string.
 */
/*
 * 把状态位转换成显示字符串。
 * 返回 "Down" / "Up" / "Admin Down" / "Unknown"（默认分支）。返回静态字符串。
 */
const char *bfd_get_status_str(int status)
{
	switch (status) {
	case BFD_STATUS_DOWN:
		return "Down";
	case BFD_STATUS_UP:
		return "Up";
	case BFD_STATUS_ADMIN_DOWN:
		return "Admin Down";
	case BFD_STATUS_UNKNOWN:
	default:
		return "Unknown";
	}
}

/*
 * bfd_last_update - Calculate the last BFD update time and convert it
 *                   into a dd:hh:mm:ss display format.
 */
/*
 * 计算“距上次状态更新”的时长并格式化为 dd:hh:mm:ss 文本。
 * last_update: 上次更新时刻；buf / len: 输出缓冲区。
 * 若 last_update 为 0（从未收到状态）则写 "never"。
 * 注意: 时间差用 gmtime_r 拆解，天然按 UTC 计算，不受本地时区影响。
 */
static void bfd_last_update(time_t last_update, char *buf, size_t len)
{
	time_t curr;
	time_t diff;
	struct tm tm;
	struct timeval tv;

	/* If no BFD status update has ever been received, print `never'. */
	if (last_update == 0) {
		snprintf(buf, len, "never");
		return;
	}

	/* Get current time. */
	monotime(&tv);
	curr = tv.tv_sec;
	diff = curr - last_update;
	gmtime_r(&diff, &tm);

	snprintf(buf, len, "%d:%02d:%02d:%02d", tm.tm_yday, tm.tm_hour,
		 tm.tm_min, tm.tm_sec);
}

/*
 * bfd_client_sendmsg - Format and send a client register
 *                    command to Zebra to be forwarded to BFD
 */
/*
 * 组装并发送 BFD 客户端注册类消息给 zebra（转 bfdd）。
 * zclient: 客户端；command: 命令号；vrf_id: VRF。
 * 消息体仅包含本进程 pid。socket 未建立或发送失败时按调试开关打印并返回。
 */
void bfd_client_sendmsg(struct zclient *zclient, int command,
			vrf_id_t vrf_id)
{
	struct stream *s;
	enum zclient_send_status ret;

	/* Check socket. */
	if (!zclient || zclient->sock < 0) {
		if (bsglobal.debugging)
			zlog_debug(
				"%s: Can't send BFD client register, Zebra client not established",
				__func__);
		return;
	}

	s = zclient->obuf;
	stream_reset(s);
	zclient_create_header(s, command, vrf_id);

	stream_putl(s, getpid());

	stream_putw_at(s, 0, stream_get_endp(s));

	ret = zclient_send_message(zclient);

	if (ret == ZCLIENT_SEND_FAILURE) {
		if (bsglobal.debugging)
			zlog_debug(
				"%s:  %ld: zclient_send_message() failed",
				__func__, (long)getpid());
		return;
	}

	return;
}

/*
 * 把一个 BFD 会话命令（注册/更新/注销）编码后经 zebra 发给 bfdd。
 * zc  : zebra 客户端；args: 会话参数（见 bfd.h 的 struct bfd_session_arg）。
 * 返回 0 成功，-1 失败（关闭中 / zclient 不可用 / 发送失败）。
 * 编码分支: HAVE_BFDD 时按 bfdd 的 ptm_adapter 格式发送完整字段；
 *           否则走旧 ptm-bfd 格式（仅注册时带定时器，单跳 IPv4 不带源地址）。
 * 末尾用 stream_putw_at 回填消息总长度。
 */
int zclient_bfd_command(struct zclient *zc, struct bfd_session_arg *args)
{
	struct stream *s;
	size_t addrlen;

	/* Individual reg/dereg messages are suppressed during shutdown. */
	if (bsglobal.shutting_down) {
		if (bsglobal.debugging)
			zlog_debug(
				"%s: Suppressing BFD peer reg/dereg messages",
				__func__);
		return -1;
	}

	/* Check socket. */
	if (!zc || zc->sock < 0) {
		if (bsglobal.debugging)
			zlog_debug("%s: zclient unavailable", __func__);
		return -1;
	}

	s = zc->obuf;
	stream_reset(s);

	/* Create new message. */
	zclient_create_header(s, args->command, args->vrf_id);
	stream_putl(s, getpid());

	/* Encode destination address. */
	stream_putw(s, args->family);
	addrlen = (args->family == AF_INET) ? sizeof(struct in_addr)
					    : sizeof(struct in6_addr);
	stream_put(s, &args->dst, addrlen);

	/*
	 * For more BFD integration protocol details, see function
	 * `_ptm_msg_read` in `bfdd/ptm_adapter.c`.
	 */
#if HAVE_BFDD > 0
	/* Session timers. */
	stream_putl(s, args->min_rx);
	stream_putl(s, args->min_tx);
	stream_putc(s, args->detection_multiplier);

	/* Is multi hop? */
	stream_putc(s, args->mhop != 0);

	/* Source address. */
	stream_putw(s, args->family);
	stream_put(s, &args->src, addrlen);

	/* Send the expected hops. */
	stream_putc(s, args->hops);

	/* Send interface name if any. */
	if (args->mhop) {
		/* Don't send interface. */
		stream_putc(s, 0);
		if (bsglobal.debugging && args->ifnamelen)
			zlog_debug("%s: multi hop is configured, not sending interface",
				   __func__);
	} else {
		stream_putc(s, args->ifnamelen);
		if (args->ifnamelen)
			stream_put(s, args->ifname, args->ifnamelen);
	}

	/* Send the C bit indicator. */
	stream_putc(s, args->cbit);

	/* Send profile name if any. */
	stream_putc(s, args->profilelen);
	if (args->profilelen)
		stream_put(s, args->profile, args->profilelen);
#else /* PTM BFD */
	/* Encode timers if this is a registration message. */
	if (args->command != ZEBRA_BFD_DEST_DEREGISTER) {
		stream_putl(s, args->min_rx);
		stream_putl(s, args->min_tx);
		stream_putc(s, args->detection_multiplier);
	}

	if (args->mhop) {
		/* Multi hop indicator. */
		stream_putc(s, 1);

		/* Multi hop always sends the source address. */
		stream_putw(s, args->family);
		stream_put(s, &args->src, addrlen);

		/* Send the expected hops. */
		stream_putc(s, args->hops);
	} else {
		/* Multi hop indicator. */
		stream_putc(s, 0);

		/* Single hop only sends the source address when IPv6. */
		if (args->family == AF_INET6) {
			stream_putw(s, args->family);
			stream_put(s, &args->src, addrlen);
		}

		/* Send interface name if any. */
		stream_putc(s, args->ifnamelen);
		if (args->ifnamelen)
			stream_put(s, args->ifname, args->ifnamelen);
	}

	/* Send the C bit indicator. */
	stream_putc(s, args->cbit);
#endif /* HAVE_BFDD */

	/* Finish the message by writing the size. */
	stream_putw_at(s, 0, stream_get_endp(s));

	/* Send message to zebra. */
	if (zclient_send_message(zc) == ZCLIENT_SEND_FAILURE) {
		if (bsglobal.debugging)
			zlog_debug("%s: zclient_send_message failed", __func__);
		return -1;
	}

	return 0;
}

/*
 * 创建一个 BFD 会话参数对象并挂入全局列表。
 * updatecb: 状态回调；arg: 回调自定义数据。
 * 返回新对象；默认值：检测倍数 3、跳数 1、min_rx / min_tx=300、默认 VRF。
 * 用 XCALLOC 清零分配，内存类型 MTYPE_BFD_INFO。
 */
struct bfd_session_params *bfd_sess_new(bsp_status_update updatecb, void *arg)
{
	struct bfd_session_params *bsp;

	bsp = XCALLOC(MTYPE_BFD_INFO, sizeof(*bsp));

	/* Save application data. */
	bsp->updatecb = updatecb;
	bsp->arg = arg;

	/* Set defaults. */
	bsp->args.detection_multiplier = BFD_DEF_DETECT_MULT;
	bsp->args.hops = 1;
	bsp->args.min_rx = BFD_DEF_MIN_RX;
	bsp->args.min_tx = BFD_DEF_MIN_TX;
	bsp->args.vrf_id = VRF_DEFAULT;

	/* Register in global list. */
	TAILQ_INSERT_TAIL(&bsglobal.bsplist, bsp, entry);

	return bsp;
}

/*
 * 校验会话参数是否可下发，防止发送无意义/非法数据。
 * 检查：地址族已设置且合法、目的地址非全零、多跳必须有源地址、VRF 已知。
 * 返回 true 表示有效；失败时按调试开关打印原因。
 */
static bool _bfd_sess_valid(const struct bfd_session_params *bsp)
{
	/* Peer/local address not configured. */
	if (bsp->args.family == 0)
		return false;

	/* Address configured but invalid. */
	if (bsp->args.family != AF_INET && bsp->args.family != AF_INET6) {
		if (bsglobal.debugging)
			zlog_debug("%s: invalid session family: %d", __func__,
				   bsp->args.family);
		return false;
	}

	/* Invalid address. */
	if (memcmp(&bsp->args.dst, &i6a_zero, sizeof(i6a_zero)) == 0) {
		if (bsglobal.debugging) {
			if (bsp->args.family == AF_INET)
				zlog_debug("%s: invalid address: %pI4",
					   __func__,
					   (struct in_addr *)&bsp->args.dst);
			else
				zlog_debug("%s: invalid address: %pI6",
					   __func__, &bsp->args.dst);
		}
		return false;
	}

	/* Multi hop requires local address. */
	if (bsp->args.mhop
	    && memcmp(&i6a_zero, &bsp->args.src, sizeof(i6a_zero)) == 0) {
		if (bsglobal.debugging)
			zlog_debug(
				"%s: multi hop but no local address provided",
				__func__);
		return false;
	}

	/* Check VRF ID. */
	if (bsp->args.vrf_id == VRF_UNKNOWN) {
		if (bsglobal.debugging)
			zlog_debug("%s: asked for unknown VRF", __func__);
		return false;
	}

	return true;
}

/*
 * 延迟事件的执行体：真正把会话参数下发/撤销给 bfdd。
 * 依据 lastev 决定命令：INSTALL 时，未安装->REGISTER，已安装->UPDATE；
 * 否则为 DEREGISTER。先校验参数，再调用 zclient_bfd_command。
 * 成功则更新 installed 状态；失败打印错误，卸载方向保持未安装，
 * 安装方向转为 BSE_VALID_FOR_INSTALL 等待 zebra 重连后重试。
 */
static void _bfd_sess_send(struct event *t)
{
	struct bfd_session_params *bsp = EVENT_ARG(t);
	int rv;

	/* Validate configuration before trying to send bogus data. */
	if (!_bfd_sess_valid(bsp))
		return;

	if (bsp->lastev == BSE_INSTALL) {
		bsp->args.command = bsp->installed ? ZEBRA_BFD_DEST_UPDATE
						   : ZEBRA_BFD_DEST_REGISTER;
	} else
		bsp->args.command = ZEBRA_BFD_DEST_DEREGISTER;

	/* If not installed and asked for uninstall, do nothing. */
	if (!bsp->installed && bsp->args.command == ZEBRA_BFD_DEST_DEREGISTER)
		return;

	rv = zclient_bfd_command(bsglobal.zc, &bsp->args);
	/* Command was sent successfully. */
	if (rv == 0) {
		/* Update installation status. */
		if (bsp->args.command == ZEBRA_BFD_DEST_DEREGISTER)
			bsp->installed = false;
		else if (bsp->args.command == ZEBRA_BFD_DEST_REGISTER)
			bsp->installed = true;
	} else {
		struct ipaddr src, dst;

		src.ipa_type = bsp->args.family;
		src.ipaddr_v6 = bsp->args.src;
		dst.ipa_type = bsp->args.family;
		dst.ipaddr_v6 = bsp->args.dst;

		zlog_err(
			"%s: BFD session %pIA -> %pIA interface %s VRF %s(%u) was not %s",
			__func__, &src, &dst,
			bsp->args.ifnamelen ? bsp->args.ifname : "*",
			vrf_id_to_name(bsp->args.vrf_id), bsp->args.vrf_id,
			bsp->lastev == BSE_INSTALL ? "installed"
						   : "uninstalled");

		bsp->installed = false;
		if (bsp->lastev == BSE_INSTALL)
			bsp->lastev = BSE_VALID_FOR_INSTALL;
	}
}

/*
 * 移除（卸载）一个已安装的会话。
 * 取消挂起的安装事件；未安装则无需操作；否则设置 lastev=UNINSTALL 并
 * 立即（event_execute，延迟 0）执行 _bfd_sess_send 发送注销消息。
 */
static void _bfd_sess_remove(struct bfd_session_params *bsp)
{
	/* Cancel any pending installation request. */
	event_cancel(&bsp->installev);

	/* Not installed, nothing to do. */
	if (!bsp->installed)
		return;

	/* Send request to remove any session. */
	bsp->lastev = BSE_UNINSTALL;
	event_execute(bsglobal.tm, _bfd_sess_send, bsp, 0, NULL);
}

/*
 * 释放会话对象：先卸载已安装会话，从全局列表摘除，归还源地址缓存引用，
 * 释放内存并把 *bsp 置 NULL，避免悬垂指针。
 * bsp: 指向指针的指针；*bsp 为 NULL 时安全返回。
 */
void bfd_sess_free(struct bfd_session_params **bsp)
{
	if (*bsp == NULL)
		return;

	/* Remove any installed session. */
	_bfd_sess_remove(*bsp);

	/* Remove from global list. */
	TAILQ_REMOVE(&bsglobal.bsplist, (*bsp), entry);

	bfd_source_cache_put(*bsp);

	/* Free the memory and point to NULL. */
	XFREE(MTYPE_BFD_INFO, (*bsp));
}

/*
 * 判断新传入的地址族/源/目的地址相比现有配置是否发生变化。
 * 变化则应先移除旧会话再应用新值（见各 set_XXX 实现）。
 * 返回 true 表示有变化。
 */
static bool bfd_sess_address_changed(const struct bfd_session_params *bsp,
				     uint32_t family,
				     const struct in6_addr *src,
				     const struct in6_addr *dst)
{
	size_t addrlen;

	if (bsp->args.family != family)
		return true;

	addrlen = (family == AF_INET) ? sizeof(struct in_addr)
				      : sizeof(struct in6_addr);
	if ((src == NULL && memcmp(&bsp->args.src, &i6a_zero, addrlen))
	    || (src && memcmp(src, &bsp->args.src, addrlen))
	    || memcmp(dst, &bsp->args.dst, addrlen))
		return true;

	return false;
}

/*
 * 设置 IPv4 本地/对端地址。
 * 若地址未变直接返回；否则移除旧会话、归还自动源缓存，写入新地址，
 * 若开了 auto_source 再重新申请缓存。dst 不可为 NULL（有 assert 保护）。
 */
void bfd_sess_set_ipv4_addrs(struct bfd_session_params *bsp,
			     const struct in_addr *src,
			     const struct in_addr *dst)
{
	if (!bfd_sess_address_changed(bsp, AF_INET, (struct in6_addr *)src,
				      (struct in6_addr *)dst))
		return;

	/* If already installed, remove the old setting. */
	_bfd_sess_remove(bsp);
	/* Address changed so we must reapply auto source. */
	bfd_source_cache_put(bsp);

	bsp->args.family = AF_INET;

	/* Clean memory, set zero value and avoid static analyser warnings. */
	memset(&bsp->args.src, 0, sizeof(bsp->args.src));
	memset(&bsp->args.dst, 0, sizeof(bsp->args.dst));

	/* Copy the equivalent of IPv4 to arguments structure. */
	if (src)
		memcpy(&bsp->args.src, src, sizeof(struct in_addr));

	assert(dst);
	memcpy(&bsp->args.dst, dst, sizeof(struct in_addr));

	if (bsp->auto_source)
		bfd_source_cache_get(bsp);
}

/*
 * 设置 IPv6 本地/对端地址（逻辑同 IPv4 版本）。
 * src 可空；dst 必有（assert 保护）；地址变化会移除旧会话并重挂 auto source。
 */
void bfd_sess_set_ipv6_addrs(struct bfd_session_params *bsp,
			     const struct in6_addr *src,
			     const struct in6_addr *dst)
{
	if (!bfd_sess_address_changed(bsp, AF_INET6, src, dst))
		return;

	/* If already installed, remove the old setting. */
	_bfd_sess_remove(bsp);
	/* Address changed so we must reapply auto source. */
	bfd_source_cache_put(bsp);

	bsp->args.family = AF_INET6;

	/* Clean memory, set zero value and avoid static analyser warnings. */
	memset(&bsp->args.src, 0, sizeof(bsp->args.src));

	if (src)
		bsp->args.src = *src;

	assert(dst);
	bsp->args.dst = *dst;

	if (bsp->auto_source)
		bfd_source_cache_get(bsp);
}

/*
 * 设置/清除会话绑定接口名。
 * ifname 为 NULL 且当前也无接口、或与当前同名则直接返回；
 * 否则移除旧会话，写入新名（超长截断并告警）并更新长度。
 */
void bfd_sess_set_interface(struct bfd_session_params *bsp, const char *ifname)
{
	if ((ifname == NULL && bsp->args.ifnamelen == 0)
	    || (ifname && strcmp(bsp->args.ifname, ifname) == 0))
		return;

	/* If already installed, remove the old setting. */
	_bfd_sess_remove(bsp);

	if (ifname == NULL) {
		bsp->args.ifname[0] = 0;
		bsp->args.ifnamelen = 0;
		return;
	}

	if (strlcpy(bsp->args.ifname, ifname, sizeof(bsp->args.ifname))
	    > sizeof(bsp->args.ifname))
		zlog_warn("%s: interface name truncated: %s", __func__, ifname);

	bsp->args.ifnamelen = strlen(bsp->args.ifname);
}

/*
 * 设置/清除会话 profile 名。
 * NULL 表示清除；超长截断并告警；更新 profilelen。
 * 注意: 与地址/接口不同，此处不主动移除旧会话，profile 在下次 install 生效。
 */
void bfd_sess_set_profile(struct bfd_session_params *bsp, const char *profile)
{
	if (profile == NULL) {
		bsp->args.profile[0] = 0;
		bsp->args.profilelen = 0;
		return;
	}

	if (strlcpy(bsp->args.profile, profile, sizeof(bsp->args.profile))
	    > sizeof(bsp->args.profile))
		zlog_warn("%s: profile name truncated: %s", __func__, profile);

	bsp->args.profilelen = strlen(bsp->args.profile);
}

/*
 * 设置会话所属 VRF。
 * 未变则返回；否则移除旧会话、归还 auto source 缓存，写入新 VRF，
 * 若开启 auto_source 重新申请缓存（不同 VRF 的源地址解析结果不同）。
 */
void bfd_sess_set_vrf(struct bfd_session_params *bsp, vrf_id_t vrf_id)
{
	if (bsp->args.vrf_id == vrf_id)
		return;

	/* If already installed, remove the old setting. */
	_bfd_sess_remove(bsp);
	/* Address changed so we must reapply auto source. */
	bfd_source_cache_put(bsp);

	bsp->args.vrf_id = vrf_id;

	if (bsp->auto_source)
		bfd_source_cache_get(bsp);
}

/*
 * 设置期望跳数，并据此更新多跳标志（hops 大于 1 即 mhop）。
 * 跳数变化会移除旧会话。
 */
void bfd_sess_set_hop_count(struct bfd_session_params *bsp, uint8_t hops)
{
	if (bsp->args.hops == hops)
		return;

	/* If already installed, remove the old setting. */
	_bfd_sess_remove(bsp);

	bsp->args.hops = hops;
	bsp->args.mhop = (hops > 1);
}


/* 直接设置 C 位（CPI）标志，不触发移除（在下次 install 时生效）。 */
void bfd_sess_set_cbit(struct bfd_session_params *bsp, bool enable)
{
	bsp->args.cbit = enable;
}

/*
 * [已废弃] 覆盖会话定时器参数（检测倍数与最小收/发间隔）。
 * 仅对尚未创建的会话有效；直接写字段，不触发移除。
 */
void bfd_sess_set_timers(struct bfd_session_params *bsp,
			 uint8_t detection_multiplier, uint32_t min_rx,
			 uint32_t min_tx)
{
	bsp->args.detection_multiplier = detection_multiplier;
	bsp->args.min_rx = min_rx;
	bsp->args.min_tx = min_tx;
}

/*
 * 开/关自动源地址选择。
 * 状态未变则返回；否则置位并相应申请（get）或归还（put）源地址缓存引用。
 */
void bfd_sess_set_auto_source(struct bfd_session_params *bsp, bool enable)
{
	if (bsp->auto_source == enable)
		return;

	bsp->auto_source = enable;
	if (enable)
		bfd_source_cache_get(bsp);
	else
		bfd_source_cache_put(bsp);
}

/*
 * 请求安装/更新会话：置 lastev=INSTALL 并安排延迟事件。
 * 批次内多次调用会被合并（event_add_event 复用同一 installev 句柄）。
 */
void bfd_sess_install(struct bfd_session_params *bsp)
{
	bsp->lastev = BSE_INSTALL;
	event_add_event(bsglobal.tm, _bfd_sess_send, bsp, 0, &bsp->installev);
}

/* 请求卸载会话：置 lastev=UNINSTALL 并安排延迟事件。 */
void bfd_sess_uninstall(struct bfd_session_params *bsp)
{
	bsp->lastev = BSE_UNINSTALL;
	event_add_event(bsglobal.tm, _bfd_sess_send, bsp, 0, &bsp->installev);
}

/* 取会话当前状态。 */
enum bfd_session_state bfd_sess_status(const struct bfd_session_params *bsp)
{
	return bsp->bss.state;
}

/* 取会话配置的期望跳数。 */
uint8_t bfd_sess_hop_count(const struct bfd_session_params *bsp)
{
	return bsp->args.hops;
}

/* 取 profile 名；未配置（长度为 0）返回 NULL。 */
const char *bfd_sess_profile(const struct bfd_session_params *bsp)
{
	return bsp->args.profilelen ? bsp->args.profile : NULL;
}

/* 取地址族与本地/对端地址；src / dst 为输出参数，可传 NULL 表示不需要。 */
void bfd_sess_addresses(const struct bfd_session_params *bsp, int *family,
			struct in6_addr *src, struct in6_addr *dst)
{
	*family = bsp->args.family;
	if (src)
		*src = bsp->args.src;
	if (dst)
		*dst = bsp->args.dst;
}

/* 取绑定的接口名；未绑定返回 NULL。 */
const char *bfd_sess_interface(const struct bfd_session_params *bsp)
{
	if (bsp->args.ifnamelen)
		return bsp->args.ifname;

	return NULL;
}

/* 取会话 VRF 名称（经 vrf_id_to_name 转换）。 */
const char *bfd_sess_vrf(const struct bfd_session_params *bsp)
{
	return vrf_id_to_name(bsp->args.vrf_id);
}

/* 取会话 VRF ID。 */
vrf_id_t bfd_sess_vrf_id(const struct bfd_session_params *bsp)
{
	return bsp->args.vrf_id;
}

/* 取 C 位（CPI）配置。 */
bool bfd_sess_cbit(const struct bfd_session_params *bsp)
{
	return bsp->args.cbit;
}

/* [已废弃] 取会话定时器参数，写入三个输出参数。 */
void bfd_sess_timers(const struct bfd_session_params *bsp,
		     uint8_t *detection_multiplier, uint32_t *min_rx,
		     uint32_t *min_tx)
{
	*detection_multiplier = bsp->args.detection_multiplier;
	*min_rx = bsp->args.min_rx;
	*min_tx = bsp->args.min_tx;
}

/* 取自动源地址选择开关。 */
bool bfd_sess_auto_source(const struct bfd_session_params *bsp)
{
	return bsp->auto_source;
}

/*
 * 输出会话的类型/配置/状态。
 * vty: 文本输出；json: 非空则输出 JSON（键 peerBfdInfo）。
 * bsp : 目标会话，NULL 直接返回。
 * 时间字段用 bfd_last_update 生成，状态用 bfd_get_status_str 转换。
 */
void bfd_sess_show(struct vty *vty, struct json_object *json,
		   struct bfd_session_params *bsp)
{
	json_object *json_bfd = NULL;
	char time_buf[64];
	const char *profile_name;

	if (!bsp)
		return;

	/* Show type. */
	if (json) {
		json_bfd = json_object_new_object();
		if (bsp->args.mhop)
			json_object_string_add(json_bfd, "type", "multi hop");
		else
			json_object_string_add(json_bfd, "type", "single hop");
	} else
		vty_out(vty, "  BFD: Type: %s\n",
			bsp->args.mhop ? "multi hop" : "single hop");

	/* Show configuration. */
	if (json) {
		json_object_int_add(json_bfd, "detectMultiplier",
				    bsp->args.detection_multiplier);
		json_object_int_add(json_bfd, "rxMinInterval",
				    bsp->args.min_rx);
		json_object_int_add(json_bfd, "txMinInterval",
				    bsp->args.min_tx);
	} else {
		vty_out(vty,
			"  Detect Multiplier: %d, Min Rx interval: %d, Min Tx interval: %d\n",
			bsp->args.detection_multiplier, bsp->args.min_rx,
			bsp->args.min_tx);
	}

	/* Show profile if configured. */
	profile_name = bfd_sess_profile(bsp);
	if (profile_name) {
		if (json)
			json_object_string_add(json_bfd, "profile", profile_name);
		else
			vty_out(vty, "  Profile: %s\n", profile_name);
	}

	bfd_last_update(bsp->bss.last_event, time_buf, sizeof(time_buf));
	if (json) {
		json_object_string_add(json_bfd, "status",
				       bfd_get_status_str(bsp->bss.state));
		json_object_string_add(json_bfd, "lastUpdate", time_buf);
	} else
		vty_out(vty, "  Status: %s, Last update: %s\n",
			bfd_get_status_str(bsp->bss.state), time_buf);

	if (json)
		json_object_object_add(json, "peerBfdInfo", json_bfd);
	else
		vty_out(vty, "\n");
}

/*
 * Zebra communication related.
 */

/**
 * Callback for reinstallation of all registered BFD sessions.
 *
 * Use this as `zclient` `bfd_dest_replay` callback.
 */
/*
 * zebra 重连后的重放回调（注册到 zclient 的 bfd_dest_replay）。
 * 重发客户端注册，然后遍历全部会话：把仍在用或待安装的会话重新标记为
 * 未安装并立即下发安装，从而在 zebra/bfdd 重启后恢复会话。
 * 未启用集成或正在关闭时直接返回 0。
 */
int zclient_bfd_session_replay(ZAPI_CALLBACK_ARGS)
{
	struct bfd_session_params *bsp;

	if (!zclient->bfd_integration)
		return 0;

	/* Do nothing when shutting down. */
	if (bsglobal.shutting_down)
		return 0;

	if (bsglobal.debugging)
		zlog_debug("%s: sending all sessions registered", __func__);

	/* Send the client registration */
	bfd_client_sendmsg(zclient, ZEBRA_BFD_CLIENT_REGISTER, vrf_id);

	/* Replay all activated peers. */
	TAILQ_FOREACH (bsp, &bsglobal.bsplist, entry) {
		/* Skip not installed sessions. */
		if (!bsp->installed && bsp->lastev != BSE_VALID_FOR_INSTALL)
			continue;

		/* We are reconnecting, so we must send installation. */
		bsp->installed = false;

		/* Cancel any pending installation request. */
		event_cancel(&bsp->installev);

		/* Ask for installation. */
		bsp->lastev = BSE_INSTALL;
		event_execute(bsglobal.tm, _bfd_sess_send, bsp, 0, NULL);
	}

	return 0;
}

/*
 * 处理 bfdd 经 zebra 回传的会话状态更新。
 * 解析出接口、目的/源前缀、状态、远端 C 位与会话名；随后遍历全局会话，
 * 按 VRF、地址族、接口、目的地址（源地址仅在双方都非零时比较）匹配，
 * 命中且状态有变化时更新 bss 并调用应用回调，最后统计更新数量。
 * 返回 0。关键：接口查找失败或前缀族为 0 时必须提前返回，否则会触发断言。
 */
int zclient_bfd_session_update(ZAPI_CALLBACK_ARGS)
{
	struct bfd_session_params *bsp, *bspn;
	size_t sessions_updated = 0;
	struct interface *ifp;
	int remote_cbit = false;
	int state = BFD_STATUS_UNKNOWN;
	time_t now;
	size_t addrlen;
	struct prefix dp;
	struct prefix sp;
	char ifstr[128], cbitstr[32];
	char bfd_name[BFD_NAME_SIZE + 1] = { 0 };

	if (!zclient->bfd_integration)
		return 0;

	/* Do nothing when shutting down. */
	if (bsglobal.shutting_down)
		return 0;

	ifp = bfd_get_peer_info(zclient->ibuf, &dp, &sp, &state, &remote_cbit, vrf_id, bfd_name);
	/*
	 * When interface lookup fails or an invalid stream is read, we must
	 * not proceed otherwise it will trigger an assertion while checking
	 * family type below.
	 */
	if (dp.family == 0 || sp.family == 0)
		return 0;

	if (bsglobal.debugging) {
		ifstr[0] = 0;
		if (ifp)
			snprintf(ifstr, sizeof(ifstr), " (interface %s)",
				 ifp->name);

		snprintf(cbitstr, sizeof(cbitstr), " (CPI bit %s)",
			 remote_cbit ? "yes" : "no");

		zlog_debug("%s: %pFX -> %pFX%s VRF %s(%u)%s: %s", __func__, &sp,
			   &dp, ifstr, vrf_id_to_name(vrf_id), vrf_id, cbitstr,
			   bfd_get_status_str(state));
	}

	switch (dp.family) {
	case AF_INET:
		addrlen = sizeof(struct in_addr);
		break;
	case AF_INET6:
		addrlen = sizeof(struct in6_addr);
		break;

	default:
		/* Unexpected value. */
		assert(0);
		break;
	}

	/* Cache current time to avoid multiple monotime clock calls. */
	now = monotime(NULL);

	/* Notify all matching sessions about update. */
	TAILQ_FOREACH_SAFE (bsp, &bsglobal.bsplist, entry, bspn) {
		/* Skip not installed entries. */
		if (!bsp->installed)
			continue;
		/* Skip different VRFs. */
		if (bsp->args.vrf_id != vrf_id)
			continue;
		/* Skip different families. */
		if (bsp->args.family != dp.family)
			continue;
		/* Skip different interface. */
		if (bsp->args.ifnamelen && ifp
		    && strcmp(bsp->args.ifname, ifp->name) != 0)
			continue;
		/* Skip non matching destination addresses. */
		if (memcmp(&bsp->args.dst, &dp.u, addrlen) != 0)
			continue;
		/*
		 * Source comparison test:
		 * We will only compare source if BFD daemon provided the
		 * source address and the protocol set a source address in
		 * the configuration otherwise we'll just skip it.
		 */
		if (sp.family && memcmp(&bsp->args.src, &i6a_zero, addrlen) != 0
		    && memcmp(&sp.u, &i6a_zero, addrlen) != 0
		    && memcmp(&bsp->args.src, &sp.u, addrlen) != 0)
			continue;
		/* No session state change. */
		if ((int)bsp->bss.state == state)
			continue;

		bsp->bss.last_event = now;
		bsp->bss.previous_state = bsp->bss.state;
		bsp->bss.state = state;
		bsp->bss.remote_cbit = remote_cbit;
		bsp->updatecb(bsp, &bsp->bss, bsp->arg);
		sessions_updated++;
	}

	if (bsglobal.debugging)
		zlog_debug("%s:   sessions updated: %zu", __func__,
			   sessions_updated);

	return 0;
}

/**
 * Frees all allocated resources and stops any activity.
 *
 * Must be called after every BFD session has been successfully
 * unconfigured otherwise this function will `free()` any available
 * session causing existing pointers to dangle.
 *
 * This is just a comment, in practice it will be called by the FRR
 * library late finish hook. \see `bfd_protocol_integration_init`.
 */
/*
 * 库收尾（注册到 libfrr 的 frr_fini 钩子）：逐个释放全部会话，
 * 之后源地址缓存应随之清空，否则给出告警（说明有引用计数泄漏）。
 * 返回 0。必须在所有会话都已卸载完成后才可调用，否则会留下悬垂指针。
 */
static int bfd_protocol_integration_finish(void)
{
	if (bsglobal.zc == NULL)
		return 0;

	while (!TAILQ_EMPTY(&bsglobal.bsplist)) {
		struct bfd_session_params *session =
			TAILQ_FIRST(&bsglobal.bsplist);
		bfd_sess_free(&session);
	}

	/*
	 * BFD source cache is linked to sessions, if all sessions are gone
	 * then the source cache must be empty.
	 */
	if (!SLIST_EMPTY(&bsglobal.source_list))
		zlog_warn("BFD integration source cache not empty");

	return 0;
}

/*
 * 初始化 BFD 集成库。
 * zc: 守护进程 zebra 客户端（打开其 bfd_integration 开关以启回调）；
 * tm: 事件循环，用于延迟执行会话安装。
 * 过程：初始化两个列表、保存指针、发送客户端注册、注册 frr_fini 收尾钩子。
 */
void bfd_protocol_integration_init(struct zclient *zc, struct event_loop *tm)
{
	/* Initialize data structure. */
	TAILQ_INIT(&bsglobal.bsplist);
	SLIST_INIT(&bsglobal.source_list);

	/* Copy pointers. */
	bsglobal.zc = zc;
	bsglobal.tm = tm;

	/* Enable BFD callbacks. */
	zc->bfd_integration = true;

	/* Send the client registration */
	bfd_client_sendmsg(zc, ZEBRA_BFD_CLIENT_REGISTER, VRF_DEFAULT);

	hook_register(frr_fini, bfd_protocol_integration_finish);
}

/* 打开/关闭集成库的调试日志。 */
void bfd_protocol_integration_set_debug(bool enable)
{
	bsglobal.debugging = enable;
}

/* 设置关闭中状态；置位后 zclient_bfd_command 会抑制会话增删消息。 */
void bfd_protocol_integration_set_shutdown(bool enable)
{
	bsglobal.shutting_down = enable;
}

/* 返回调试开关状态。 */
bool bfd_protocol_integration_debug(void)
{
	return bsglobal.debugging;
}

/* 返回关闭中状态。 */
bool bfd_protocol_integration_shutting_down(void)
{
	return bsglobal.shutting_down;
}

/*
 * BFD automatic source selection
 *
 * This feature will use the next hop tracking (NHT) provided by zebra
 * to find out the source address by looking at the output interface.
 *
 * When the interface address / routing table change we'll be notified
 * and be able to update the source address accordingly.
 *
 *     <daemon>                 zebra
 *         |
 * +-----------------+
 * | BFD session set |
 * | to auto source  |
 * +-----------------+
 *         |
 *         \                 +-----------------+
 *          -------------->  | Resolves        |
 *                           | destination     |
 *                           | address         |
 *                           +-----------------+
 *                                |
 * +-----------------+            /
 * | Sets resolved   | <----------
 * | source address  |
 * +-----------------+
 */
/*
 * 判断某会话是否“属于”该源缓存条目：VRF、地址族、目的地址都要匹配。
 * 匹配成功的会话会在缓存刷新时被同步更新源地址。
 * 返回 true 表示匹配。
 */
static bool
bfd_source_cache_session_match(const struct bfd_source_cache *source,
			       const struct bfd_session_params *session)
{
	const struct in_addr *address;
	const struct in6_addr *address_v6;

	if (session->args.vrf_id != source->vrf_id)
		return false;
	if (session->args.family != source->address.family)
		return false;

	switch (session->args.family) {
	case AF_INET:
		address = (const struct in_addr *)&session->args.dst;
		if (address->s_addr != source->address.u.prefix4.s_addr)
			return false;
		break;
	case AF_INET6:
		address_v6 = &session->args.dst;
		if (memcmp(address_v6, &source->address.u.prefix6,
			   sizeof(struct in6_addr)))
			return false;
		break;
	default:
		return false;
	}

	return true;
}

/*
 * 在源缓存链表中按 VRF 与前缀查找已有条目。
 * 返回匹配条目指针，找不到返回 NULL。
 */
static struct bfd_source_cache *
bfd_source_cache_find(vrf_id_t vrf_id, const struct prefix *prefix)
{
	struct bfd_source_cache *source;

	SLIST_FOREACH (source, &bsglobal.source_list, entry) {
		if (source->vrf_id != vrf_id)
			continue;
		if (!prefix_same(&source->address, prefix))
			continue;

		return source;
	}

	return NULL;
}

/*
 * 为会话申请源地址缓存引用。
 * 依据会话目的地址构造精确前缀，在链表中查找：命中则把会话指向它并
 * 增加引用计数；未命中则新建条目并插入链表头。
 * 副作用: 会先归还会话当前持有的旧缓存，再绑定新缓存。
 * 会话已指向同一缓存时直接返回。
 */
static void bfd_source_cache_get(struct bfd_session_params *session)
{
	struct bfd_source_cache *source;
	struct prefix target = {};

	switch (session->args.family) {
	case AF_INET:
		target.family = AF_INET;
		target.prefixlen = IPV4_MAX_BITLEN;
		memcpy(&target.u.prefix4, &session->args.dst,
		       sizeof(struct in_addr));
		break;
	case AF_INET6:
		target.family = AF_INET6;
		target.prefixlen = IPV6_MAX_BITLEN;
		memcpy(&target.u.prefix6, &session->args.dst,
		       sizeof(struct in6_addr));
		break;
	default:
		return;
	}

	source = bfd_source_cache_find(session->args.vrf_id, &target);
	if (source) {
		if (session->source_cache == source)
			return;

		bfd_source_cache_put(session);
		session->source_cache = source;
		source->refcount++;
		return;
	}

	source = XCALLOC(MTYPE_BFD_SOURCE, sizeof(*source));
	prefix_copy(&source->address, &target);
	source->vrf_id = session->args.vrf_id;
	SLIST_INSERT_HEAD(&bsglobal.source_list, source, entry);

	bfd_source_cache_put(session);
	session->source_cache = source;
	source->refcount = 1;

	return;
}

/*
 * 归还会话持有的源地址缓存引用：引用计数减一，若仍有其他会话使用则
 * 仅解除本会话的绑定；计数归零则从链表摘除并释放内存。
 * 会话未持有缓存时直接返回。
 */
static void bfd_source_cache_put(struct bfd_session_params *session)
{
	if (session->source_cache == NULL)
		return;

	session->source_cache->refcount--;
	if (session->source_cache->refcount > 0) {
		session->source_cache = NULL;
		return;
	}

	SLIST_REMOVE(&bsglobal.source_list, session->source_cache,
		     bfd_source_cache, entry);
	XFREE(MTYPE_BFD_SOURCE, session->source_cache);
}

/** Updates BFD running session if source address has changed. */
/*
 * 若缓存解析出的源地址与会话当前源地址不同，则更新会话并重装：
 * 先移除旧会话，写入新源地址，再触发 install。
 * （该函数只处理地址实际变化的情况。）
 */
static void
bfd_source_cache_update_session(const struct bfd_source_cache *source,
				struct bfd_session_params *session)
{
	const struct in_addr *address;
	const struct in6_addr *address_v6;

	switch (session->args.family) {
	case AF_INET:
		address = (const struct in_addr *)&session->args.src;
		if (memcmp(address, &source->source.u.prefix4,
			   sizeof(struct in_addr)) == 0)
			return;

		_bfd_sess_remove(session);
		memcpy(&session->args.src, &source->source.u.prefix4,
		       sizeof(struct in_addr));
		break;
	case AF_INET6:
		address_v6 = &session->args.src;
		if (memcmp(address_v6, &source->source.u.prefix6,
			   sizeof(struct in6_addr)) == 0)
			return;

		_bfd_sess_remove(session);
		memcpy(&session->args.src, &source->source.u.prefix6,
		       sizeof(struct in6_addr));
		break;
	default:
		return;
	}

	bfd_sess_install(session);
}

/*
 * 遍历所有会话，把匹配该缓存的自动源会话逐个刷新（见上一函数）。
 * 缓存无效（valid=false）时直接返回。
 */
static void
bfd_source_cache_update_sessions(const struct bfd_source_cache *source)
{
	struct bfd_session_params *session;

	if (!source->valid)
		return;

	TAILQ_FOREACH (session, &bsglobal.bsplist, entry) {
		if (!session->auto_source)
			continue;
		if (!bfd_source_cache_session_match(source, session))
			continue;

		bfd_source_cache_update_session(source, session);
	}
}

/**
 * Try to translate next hop information into source address.
 *
 * \returns `true` if source changed otherwise `false`.
 */
/*
 * 把 NHT 路由信息翻译成源地址：遍历路由的每个 nexthop，取其出接口上
 * 与该前缀同族的接口地址作为源地址；跳过 IPv6 link-local。
 * 返回 true 表示源地址发生了变化（有效），false 表示未变或解析失败
 * （此时把 source 清零并置 valid=false）。
 */
static bool bfd_source_cache_update(struct bfd_source_cache *source,
				    const struct zapi_route *route)
{
	size_t nh_index;

	for (nh_index = 0; nh_index < route->nexthop_num; nh_index++) {
		const struct zapi_nexthop *nh = &route->nexthops[nh_index];
		const struct interface *interface;
		const struct connected *connected;

		interface = if_lookup_by_index(nh->ifindex, nh->vrf_id);
		if (interface == NULL) {
			zlog_err("next hop interface not found (index %d)",
				 nh->ifindex);
			continue;
		}

		frr_each (if_connected_const, interface->connected, connected) {
			if (source->address.family !=
			    connected->address->family)
				continue;
			if (prefix_same(connected->address, &source->source))
				return false;
			/*
			 * Skip link-local as it is only useful for single hop
			 * and in that case no source is specified usually.
			 */
			if (source->address.family == AF_INET6 &&
			    IN6_IS_ADDR_LINKLOCAL(
				    &connected->address->u.prefix6))
				continue;

			prefix_copy(&source->source, connected->address);
			source->valid = true;
			return true;
		}
	}

	memset(&source->source, 0, sizeof(source->source));
	source->valid = false;
	return false;
}

/*
 * 处理一次 NHT 更新：找到 VRF 与目的前缀都匹配的源缓存条目，
 * 重新解析源地址；若有变化则同步刷新所有相关会话。
 * match: 匹配前缀（会话目的地址）；route: zebra 上报的路由信息。
 * 返回 0。
 */
int bfd_nht_update(const struct prefix *match, const struct zapi_route *route)
{
	struct bfd_source_cache *source;

	if (bsglobal.debugging)
		zlog_debug("BFD NHT update for %pFX", &route->prefix);

	SLIST_FOREACH (source, &bsglobal.source_list, entry) {
		if (source->vrf_id != route->vrf_id)
			continue;
		if (!prefix_same(match, &source->address))
			continue;
		if (bfd_source_cache_update(source, route))
			bfd_source_cache_update_sessions(source);
	}

	return 0;
}

/* 会话是否处于 Down 或 AdminDown。 */
bool bfd_session_is_down(const struct bfd_session_params *session)
{
	return session->bss.state == BSS_DOWN ||
	       session->bss.state == BSS_ADMIN_DOWN;
}

/* 会话是否处于管理性关闭（AdminDown）。 */
bool bfd_session_is_admin_down(const struct bfd_session_params *session)
{
	return session->bss.state == BSS_ADMIN_DOWN;
}
