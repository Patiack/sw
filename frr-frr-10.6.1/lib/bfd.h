// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * bfd.h: BFD definitions and structures
 *
 * @copyright Copyright (C) 2015 Cumulus Networks, Inc.
 */

/*
 * 文件导读：bfd.h —— BFD 协议集成库（libbfd，供各守护进程使用的头文件）
 *
 * 一、这个文件在 FRR 里干什么
 *   BFD（Bidirectional Forwarding Detection，双向转发检测，RFC 5880）用于
 *   在相邻设备或转发路径上快速检测链路/转发故障。真正的 BFD 状态机由独立的
 *   bfdd 守护进程实现；本头文件定义的是“客户端侧”集成 API：BGP / OSPF 等
 *   协议通过它与 bfdd 建立、更新、删除 BFD 会话，并接收会话状态回调。
 *
 * 二、核心数据结构
 *   struct bfd_session_params：单个 BFD 会话的全部参数与状态，由
 *       bfd_sess_new() 分配，是应用侧句柄（完整结构定义在 bfd.c 中）。
 *   struct bfd_session_status：会话当前/前一次状态、远端 C 位、最后事件时间。
 *   enum bfd_session_state ：会话状态（Unknown / AdminDown / Down / Up）。
 *   struct bfd_session_arg  ：与 bfdd 通信时序列化用的参数包。
 *   bsp_status_update       ：状态变化回调函数类型。
 *
 * 三、与其他模块的关系
 *   - zclient.h：经 zebra 中转与 bfdd 通信（ZEBRA_BFD_DEST_* 命令）；
 *   - bfdd/ptm_adapter.c：对端解析这些消息；
 *   - NHT（nexthop tracking）：bfd_nht_update() 支撑自动源地址选择。
 *
 * 四、阅读顺序
 *   先看宏与状态枚举 -> bfd_session_params / status 概念 -> 会话生命周期
 *   （new / set_XXX / install / uninstall / free）-> 查询接口（get 系列）
 *   -> 回调与 NHT。这些接口的实现都在 bfd.c 中。
 *
 * 五、关键“坑”
 *   - bfd_sess_set_XXX 修改地址/接口/VRF/跳数会移除旧会话，必须重新 install；
 *   - install / uninstall 是延迟生效（命令批次结束才执行）；
 *   - 定时器参数接口已 DEPRECATED，建议改用 profile。
 */
#ifndef _ZEBRA_BFD_H
#define _ZEBRA_BFD_H

#include "lib/zclient.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BFD 默认定时参数（单位：毫秒）。
 * MIN_RX / MIN_TX：最小接收/发送间隔；DETECT_MULT：检测倍数，
 * 即“连续多少个发送间隔收不到对端报文则判 Down”。
 * STRICT_HOLD_TIME（秒）：严格保持时间，用于避免抖动。
 */
#define BFD_DEF_MIN_RX 300
#define BFD_DEF_MIN_TX 300
#define BFD_DEF_DETECT_MULT 3
#define BFD_DEF_STRICT_HOLD_TIME 30 /* seconds */

/*
 * 会话状态位标志，与 enum bfd_session_state 取值对应（见下）。
 * UNKNOWN：从未收到过状态；DOWN：会话 Down；UP：会话 Up；
 * ADMIN_DOWN：对端或本地管理性关闭。
 */
#define BFD_STATUS_UNKNOWN    (1 << 0) /* BFD session status never received */
#define BFD_STATUS_DOWN       (1 << 1) /* BFD session status is down */
#define BFD_STATUS_UP         (1 << 2) /* BFD session status is up */
#define BFD_STATUS_ADMIN_DOWN (1 << 3) /* BFD session is admin down */

/* BFD profile（配置模板）名称最大长度（含结尾 NUL）。 */
#define BFD_PROFILE_NAME_LEN 64

/* BFD 会话名称最大长度（不含结尾 NUL，数据结构里会留 255+1）。 */
#define BFD_NAME_SIZE 255

/*
 * 把状态数值转成可读字符串（"Up" / "Down" / "Admin Down" / "Unknown"）。
 * 用于 show 与日志输出；返回静态字符串，勿释放。
 */
const char *bfd_get_status_str(int status);

/*
 * 通过 zclient 向 zebra 发送 BFD 客户端注册/注销消息（会被转发给 bfdd）。
 * zclient: zebra 客户端；command: 如 ZEBRA_BFD_CLIENT_REGISTER；
 * vrf_id : 目标 VRF。socket 未就绪时静默返回。
 */
extern void bfd_client_sendmsg(struct zclient *zclient, int command,
			       vrf_id_t vrf_id);

/*
 * BFD new API.
 */

/* Forward declaration of argument struct. */
/*
 * 前向声明：BFD 会话参数结构体（完整定义在 bfd.c 中，对使用者保持不透明）。
 * 应用一律通过 bfd_sess_XXX 接口操作它，不要直接访问其字段。
 */
struct bfd_session_params;

/** Session state definitions. */
/*
 * BFD 会话状态枚举，取值刻意与上面的 BFD_STATUS_* 位宏对齐，
 * 便于在新旧 API 之间直接转换。
 */
enum bfd_session_state {
	/** Session state is unknown or not initialized. */
	BSS_UNKNOWN = BFD_STATUS_UNKNOWN,
	/** Local or remote peer administratively shutdown the session. */
	BSS_ADMIN_DOWN = BFD_STATUS_ADMIN_DOWN,
	/** Session is down. */
	BSS_DOWN = BFD_STATUS_DOWN,
	/** Session is up and working correctly. */
	BSS_UP = BFD_STATUS_UP,
};

/** BFD session status information */
/*
 * 一次状态更新的快照，随状态回调传入。
 * state          : 当前状态。
 * previous_state : 变化前状态（应用可据此判断是 Down->Up 还是 Up->Down）。
 * remote_cbit    : 远端 Control Plane Independent 位。
 * last_event     : 最后一次事件发生时刻（monotonic 秒）。
 */
struct bfd_session_status {
	/** Current session state. */
	enum bfd_session_state state;
	/** Previous session state. */
	enum bfd_session_state previous_state;
	/** Remote Control Plane Independent bit state. */
	bool remote_cbit;
	/** Last event occurrence. */
	time_t last_event;
};

/**
 * Session status update callback.
 *
 * \param bsp BFD session parameters.
 * \param bss BFD session status.
 * \param arg application independent data.
 */
/*
 * 会话状态变化回调类型。
 * bsp : 触发回调的会话句柄；bss: 新的状态快照；arg: bfd_sess_new 时传入的
 *       应用自定义指针。
 * 注意：回调在事件循环上下文执行，勿在其中阻塞或做重活。
 */
typedef void (*bsp_status_update)(struct bfd_session_params *bsp,
				  const struct bfd_session_status *bss,
				  void *arg);

/**
 * Allocates and initializes the session parameters.
 *
 * \param updatecb status update notification callback.
 * \param args application independent data.
 *
 * \returns pointer to configuration storage.
 */
/*
 * 分配并初始化一个 BFD 会话参数对象，并登记到全局列表。
 * updatecb: 状态变化回调；args: 应用自定义数据，会原样回传给回调。
 * 返回    : 新对象指针；默认参数为 300ms/300ms、检测倍数 3、跳数 1、
 *           默认 VRF。
 * 副作用  : 内存按 MTYPE_BFD_INFO 分配，须用 bfd_sess_free 释放。
 */
struct bfd_session_params *bfd_sess_new(bsp_status_update updatecb, void *args);

/**
 * Uninstall session if installed and free resources allocated by the
 * parameters. Already sets pointer to `NULL` to avoid dangling references.
 *
 * \param bsp session parameters.
 */
/*
 * 卸载会话（若已安装）并释放资源，随后把指针置 NULL 防止悬垂。
 * bsp: 指向会话指针的指针（因此必须传 &ptr）。
 * 注意: *bsp 为 NULL 时安全返回；释放后不要再用旧指针。
 */
void bfd_sess_free(struct bfd_session_params **bsp);

/**
 * Set the local and peer address of the BFD session.
 *
 * NOTE:
 * If the address changed the session is removed and must be installed again
 * with `bfd_sess_install`.
 *
 * \param bsp BFD session parameters.
 * \param src local address (optional, can be `NULL`).
 * \param dst remote address (mandatory).
 */
/*
 * 配置会话的 IPv4 本地/对端地址。
 * src: 本地地址，可 NULL；dst: 对端地址，必填。
 * 注意: 地址变化会移除已安装会话，需再次 bfd_sess_install 才生效。
 */
void bfd_sess_set_ipv4_addrs(struct bfd_session_params *bsp,
			     const struct in_addr *src,
			     const struct in_addr *dst);

/**
 * Set the local and peer address of the BFD session.
 *
 * NOTE:
 * If the address changed the session is removed and must be installed again
 * with `bfd_sess_install`.
 *
 * \param bsp BFD session parameters.
 * \param src local address (optional, can be `NULL`).
 * \param dst remote address (mandatory).
 */
/*
 * 配置会话的 IPv6 本地/对端地址（语义同 IPv4 版本）。
 * src: 本地地址，可 NULL；dst: 对端地址，必填。
 * 注意: 地址变化会移除已安装会话，需重新 install 生效。
 */
void bfd_sess_set_ipv6_addrs(struct bfd_session_params *bsp,
			     const struct in6_addr *src,
			     const struct in6_addr *dst);

/**
 * Configure the BFD session interface.
 *
 * NOTE:
 * If the interface changed the session is removed and must be installed again
 * with `bfd_sess_install`.
 *
 * \param bsp BFD session parameters.
 * \param ifname interface name (or `NULL` to remove it).
 */
/*
 * 设置会话绑定的接口名。
 * ifname: 接口名；传 NULL 表示清除绑定。
 * 注意: 接口变化会移除已安装会话，需重新 install 生效。
 */
void bfd_sess_set_interface(struct bfd_session_params *bsp, const char *ifname);

/**
 * Configure the BFD session profile name.
 *
 * NOTE:
 * Session profile will only change after a `bfd_sess_install`.
 *
 * \param bsp BFD session parameters.
 * \param profile profile name (or `NULL` to remove it).
 */
/*
 * 设置会话使用的 BFD profile（模板）名。
 * profile: profile 名；NULL 表示清除。
 * 注意: profile 仅在下次 bfd_sess_install 后才生效。
 */
void bfd_sess_set_profile(struct bfd_session_params *bsp, const char *profile);

/**
 * Configure the BFD session VRF.
 *
 * NOTE:
 * If the VRF changed the session is removed and must be installed again
 * with `bfd_sess_install`.
 *
 * \param bsp BFD session parameters.
 * \param vrf_id the VRF identification number.
 */
/*
 * 设置会话所属 VRF。
 * vrf_id: VRF 标识（VRF_UNKNOWN 会被视为无效而拒绝安装）。
 * 注意: VRF 变化会移除已安装会话，需重新 install 生效。
 */
void bfd_sess_set_vrf(struct bfd_session_params *bsp, vrf_id_t vrf_id);

/**
 * Configure the BFD session single/multi hop setting.
 *
 * NOTE:
 * If the number of hops is changed the session is removed and must be
 * installed again with `bfd_sess_install`.
 *
 * \param bsp BFD session parameters.
 * \param hops maximum amount of hops expected (1 for single hop, 2 or
 *             more for multi hop).
 */
/*
 * 设置会话的单跳/多跳属性。
 * hops: 期望跳数，1 为单跳，>=2 为多跳（内部据此设置 mhop 标志）。
 * 注意: 跳数变化会移除已安装会话，需重新 install 生效。
 */
void bfd_sess_set_hop_count(struct bfd_session_params *bsp, uint8_t hops);

/**
 * Configure the BFD session to set the Control Plane Independent bit.
 *
 * NOTE:
 * Session CPI bit will only change after a `bfd_sess_install`.
 *
 * \param bsp BFD session parameters.
 * \param enable BFD Control Plane Independent state.
 */
/*
 * 设置 Control Plane Independent（C）位。
 * enable: true 打开 CPI，表示转发面与控制面相互独立。
 * 注意: 该位仅在下次 bfd_sess_install 后生效。
 */
void bfd_sess_set_cbit(struct bfd_session_params *bsp, bool enable);

/**
 * DEPRECATED: please avoid using timers directly and use profiles instead.
 *
 * Configures the BFD session timers to use. This is specially useful with
 * `ptm-bfd` which does not support timers.
 *
 * NOTE:
 * Session timers will only apply if the session has not been created yet.
 * If the session is already installed you must uninstall and install again
 * to take effect.
 *
 * \param bsp BFD session parameters.
 * \param detection_multiplier the detection multiplier value.
 * \param min_rx minimum required receive period.
 * \param min_tx minimum required transmission period.
 */
/*
 * [已废弃] 直接设置会话定时器。建议改用 profile。
 * detection_multiplier: 检测倍数；min_rx / min_tx: 最小收/发间隔（毫秒）。
 * 注意: 定时器仅在会话尚未创建时有效；已安装需先卸载再安装。
 */
void bfd_sess_set_timers(struct bfd_session_params *bsp,
			 uint8_t detection_multiplier, uint32_t min_rx,
			 uint32_t min_tx);

/**
 * Configures the automatic source selection for the BFD session.
 *
 * NOTE:
 * Setting this configuration will override the IP source value set by
 * `bfd_sess_set_ipv4_addrs` or `bfd_sess_set_ipv6_addrs`.
 *
 * \param bsp BFD session parameters
 * \param enable BFD automatic source selection state.
 */
/*
 * 开启/关闭自动源地址选择（由 NHT 解析出出接口的源地址）。
 * enable: true 开启。开启后会覆盖 set_ipv4/ipv6_addrs 设置的本地地址。
 */
void bfd_sess_set_auto_source(struct bfd_session_params *bsp, bool enable);

/**
 * Installs or updates the BFD session based on the saved session arguments.
 *
 * NOTE:
 * This function has a delayed effect: it will only install/update after
 * all northbound/CLI command batch finishes.
 *
 * \param bsp session parameters.
 */
/*
 * 安装或更新 BFD 会话（依据已保存的参数）。
 * 注意: 延迟生效——本函数只安排事件，等当前命令批次结束后才真正下发，
 *       因此一次配置批量里多次调用最终只执行一次。
 */
void bfd_sess_install(struct bfd_session_params *bsp);

/**
 * Uninstall the BFD session based on the saved session arguments.
 *
 * NOTE:
 * This function uninstalls the session immediately (if installed) and cancels
 * any previous `bfd_sess_install` calls.
 *
 * \param bsp session parameters.
 */
/*
 * 卸载 BFD 会话。
 * 注意: 立即生效（若已安装），并取消此前挂起的 bfd_sess_install。
 */
void bfd_sess_uninstall(struct bfd_session_params *bsp);

/**
 * Get BFD session current status.
 *
 * \param bsp session parameters.
 *
 * \returns BFD session status data structure.
 */
/*
 * 取会话当前状态。
 * 返回: enum bfd_session_state（Unknown / AdminDown / Down / Up）。
 */
enum bfd_session_state bfd_sess_status(const struct bfd_session_params *bsp);

/**
 * Get BFD session amount of hops configured value.
 *
 * \param bsp session parameters.
 *
 * \returns configured amount of hops.
 */
/* 取会话配置的期望跳数。 */
uint8_t bfd_sess_hop_count(const struct bfd_session_params *bsp);

/**
 * Get BFD session profile configured value.
 *
 * \param bsp session parameters.
 *
 * \returns configured profile name (or `NULL` if empty).
 */
/* 取会话配置的 profile 名；未配置返回 NULL。 */
const char *bfd_sess_profile(const struct bfd_session_params *bsp);

/**
 * Get BFD session addresses.
 *
 * \param bsp session parameters.
 * \param family the address family being used (AF_INET or AF_INET6).
 * \param src source address (optional, may be `NULL`).
 * \param dst peer address (optional, may be `NULL`).
 */
/*
 * 取会话的地址族与本地/对端地址。
 * family: 输出地址族（AF_INET/AF_INET6）；src / dst: 输出地址，均可传 NULL。
 */
void bfd_sess_addresses(const struct bfd_session_params *bsp, int *family,
			struct in6_addr *src, struct in6_addr *dst);
/**
 * Get BFD session interface name.
 *
 * \param bsp session parameters.
 *
 * \returns `NULL` if not set otherwise the interface name.
 */
/* 取会话绑定的接口名；未设置返回 NULL。 */
const char *bfd_sess_interface(const struct bfd_session_params *bsp);

/**
 * Get BFD session VRF name.
 *
 * \param bsp session parameters.
 *
 * \returns the VRF name.
 */
/* 取会话所属 VRF 的名称（由 vrf_id 翻译）。 */
const char *bfd_sess_vrf(const struct bfd_session_params *bsp);

/**
 * Get BFD session VRF ID.
 *
 * \param bsp session parameters.
 *
 * \returns the VRF name.
 */
/* 取会话所属 VRF 的 ID。 */
vrf_id_t bfd_sess_vrf_id(const struct bfd_session_params *bsp);

/**
 * Get BFD session control plane independent bit configuration state.
 *
 * \param bsp session parameters.
 *
 * \returns `true` if enabled otherwise `false`.
 */
/* 取会话 C 位（CPI）配置状态。 */
bool bfd_sess_cbit(const struct bfd_session_params *bsp);

/**
 * DEPRECATED: please avoid using timers directly and use profiles instead.
 *
 * Gets the configured timers.
 *
 * \param bsp BFD session parameters.
 * \param detection_multiplier the detection multiplier value.
 * \param min_rx minimum required receive period.
 * \param min_tx minimum required transmission period.
 */
/*
 * [已废弃] 取会话配置的定时器参数。
 * detection_multiplier / min_rx / min_tx: 三个输出参数。
 */
void bfd_sess_timers(const struct bfd_session_params *bsp,
		     uint8_t *detection_multiplier, uint32_t *min_rx,
		     uint32_t *min_tx);

/**
 * Gets the automatic source selection state.
 */
/* 取自动源地址选择是否开启。 */
bool bfd_sess_auto_source(const struct bfd_session_params *bsp);

/**
 * Show BFD session configuration and status. If `json` is provided (e.g. not
 * `NULL`) then information will be inserted in object, otherwise printed to
 * `vty`.
 *
 * \param vty Pointer to `vty` for outputting text.
 * \param json (optional) JSON object pointer.
 * \param bsp session parameters.
 */
/* 前向声明：JSON 对象类型（bfd_sess_show 的可选输出目标）。 */
struct json_object;
/*
 * 展示会话的类型/配置/状态。
 * vty : 文本输出目标（json 为空时使用）。
 * json: JSON 对象（非空则写入 JSON，不打印文本）。
 * bsp : 要展示的会话（NULL 时直接返回）。
 */
void bfd_sess_show(struct vty *vty, struct json_object *json,
		   struct bfd_session_params *bsp);

/**
 * Initializes the BFD integration library. This function executes the
 * following actions:
 *
 * - Copy the `struct event_loop` pointer to use as "thread" to execute
 *   the BFD session parameters installation.
 * - Copy the `struct zclient` pointer to install its callbacks.
 * - Initializes internal data structures.
 *
 * \param tm normally the daemon main thread event manager.
 * \param zc the zebra client of the daemon.
 */
/*
 * 初始化 BFD 集成库（各守护进程启动时调用一次）。
 * zc: 守护进程的 zebra 客户端（会打开其 bfd_integration 开关以启回调）。
 * tm: 主事件循环，用于延迟执行会话安装。
 * 副作用: 初始化内部列表、发送客户端注册、注册 frr_fini 钩子做收尾清理。
 */
void bfd_protocol_integration_init(struct zclient *zc, struct event_loop *tm);

/**
 * BFD session registration arguments.
 */
/*
 * 与 bfdd 通信时序列化的会话参数包。
 * 字段与 bfdd/ptm_adapter.c 的解析逻辑一一对应，顺序与宽度必须一致。
 * command: ZEBRA_BFD_DEST_REGISTER / DEREGISTER / UPDATE；
 * family : AF_INET / AF_INET6；src / dst: 本地/对端地址（统一用 in6_addr 承载）；
 * mhop / hops / cbit: 多跳标志 / 期望跳数 / CPI 位；
 * ifname / ifnamelen: 接口名及其长度；
 * vrf_id : VRF；profile / profilelen: profile 名及长度；
 * 末尾 min_rx / min_tx / detection_multiplier / bfd_name 为兼容 ptm-bfd 的遗留字段。
 */
struct bfd_session_arg {
	/**
	 * BFD command.
	 *
	 * Valid commands:
	 * - `ZEBRA_BFD_DEST_REGISTER`
	 * - `ZEBRA_BFD_DEST_DEREGISTER`
	 */
	int32_t command;

	/**
	 * BFD family type.
	 *
	 * Supported types:
	 * - `AF_INET`
	 * - `AF_INET6`.
	 */
	uint32_t family;
	/** Source address. */
	struct in6_addr src;
	/** Source address. */
	struct in6_addr dst;

	/** Multi hop indicator. */
	uint8_t mhop;
	/** Expected hops. */
	uint8_t hops;
	/** C bit (Control Plane Independent bit) indicator. */
	uint8_t cbit;

	/** Interface name size. */
	uint8_t ifnamelen;
	/** Interface name. */
	char ifname[64];

	/** Daemon or session VRF. */
	vrf_id_t vrf_id;

	/** Profile name length. */
	uint8_t profilelen;
	/** Profile name. */
	char profile[BFD_PROFILE_NAME_LEN];

	/*
	 * Deprecation fields: these fields should be removed once `ptm-bfd`
	 * no longer uses this interface.
	 */

	/** Minimum required receive interval (in microseconds). */
	uint32_t min_rx;
	/** Minimum desired transmission interval (in microseconds). */
	uint32_t min_tx;
	/** Detection multiplier. */
	uint32_t detection_multiplier;
	/* bfd session name*/
	char bfd_name[BFD_NAME_SIZE + 1];
};

/**
 * Send a message to BFD daemon through the zebra client.
 *
 * \param zc the zebra client context.
 * \param arg the BFD session command arguments.
 *
 * \returns `-1` on failure otherwise `0`.
 *
 * \see bfd_session_arg.
 */
/*
 * 经 zclient 把一个 BFD 会话命令发给 zebra（再转 bfdd）。
 * zc: zebra 客户端；arg: 会话命令参数。
 * 返回 0 成功，-1 失败（关闭中 / socket 不可用 / 发送失败）。
 * 副作用: 会依据 HAVE_BFDD 选择两种不同的报文编码格式。
 */
extern int zclient_bfd_command(struct zclient *zc, struct bfd_session_arg *arg);

/**
 * Enables or disables BFD protocol integration API debugging.
 *
 * \param enable new API debug state.
 */
/* 打开/关闭 BFD 集成 API 的调试日志。 */
extern void bfd_protocol_integration_set_debug(bool enable);

/**
 * Sets shutdown mode so no more events are processed.
 *
 * This is useful to avoid the event storm that happens caused by network,
 * interfaces or VRFs removal. It should also avoid some crashes due hanging
 * pointers left overs by protocol.
 *
 * \param enable new API shutdown state.
 */
/*
 * 设置“关闭中”状态，期间不再处理事件、不再发会话增删消息，
 * 以避免网络/接口/VRF 被删除时的回调风暴和悬垂指针崩溃。
 */
extern void bfd_protocol_integration_set_shutdown(bool enable);

/**
 * Get API debugging state.
 */
/* 取 API 调试状态。 */
extern bool bfd_protocol_integration_debug(void);

/**
 * Get API shutdown state.
 */
/* 取 API 关闭中状态。 */
extern bool bfd_protocol_integration_shutting_down(void);

/* Update nexthop-tracking (nht) information for BFD auto source selection.
 * The function must be called from the daemon callback function
 * that deals with the ZEBRA_NEXTHOP_UPDATE zclient command
 */
/*
 * 更新自动源地址选择所需的 NHT 信息。
 * 必须由处理 ZEBRA_NEXTHOP_UPDATE 的守护进程回调调用。
 * match: 匹配的前缀（会话目标地址）；route: zebra 上报的路由/nexthop 信息。
 * 返回 0；副作用: 可能触发相关会话重装以更新源地址。
 */
extern int bfd_nht_update(const struct prefix *match,
			  const struct zapi_route *route);

/* 会话是否处于 Down 或 AdminDown（即不可用）。 */
extern bool bfd_session_is_down(const struct bfd_session_params *session);
/* 会话是否处于管理性关闭（AdminDown）。 */
extern bool bfd_session_is_admin_down(const struct bfd_session_params *session);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_BFD_H */
