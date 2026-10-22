// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP open message handling
 * Copyright (C) 1999 Kunihiro Ishiguro
 */
/* ============================================================================
 * 【文件导读】bgp_open.h —— OPEN 报文与能力协商的常量、结构、接口
 * ============================================================================
 * 职责与定位：bgp_open.c 的头文件。OPEN 报文 = BGP 版本 + 本端 AS + HoldTime +
 *   Router-ID + 一串**可选参数(Optional Parameters)**，而 BGP 的几乎所有扩展
 *   能力都是塞在可选参数里的 **Capability TLV** 中协商的。所以本文件本质上是
 *   “BGP 能力清单”的集中定义，读它等于读 BGP 扩展能力的目录。
 *
 * 能力 TLV 的线格式（务必记住，因为收发两侧都靠它）：
 *     ┌ code(1B) ┬ length(1B) ┬ value(length B) ┐
 *   本文件的 CAPABILITY_CODE_xxx 是 code；CAPABILITY_CODE_xxx_LEN 是各能力的
 *   典型 value 长度（用于快速校验，注意有的表示“最小长度”而非精确长度）。
 *
 * 一个特别容易踩的坑：**RFC9072 扩展可选参数**。
 *   默认(非扩展)每个可选参数的长度字段是 1 字节，最大 255 —— 一旦能力总长超过 255
 *   字节就必须改用扩展格式：长度字段变 2 字节、类型字段由 255 表示“后续是扩展长度”。
 *   所以本文件有 BGP_OPEN_NON_EXT_OPT_LEN(=255) 与 ..._TYPE_EXTENDED_LENGTH(=255)，
 *   以及 BGP_OPEN_EXT_OPT_PARAMS_CAPABLE(peer) 这个判断宏。凡看到 `ext_opt_params`
 *   参数贯穿某个能力构造函数，讲的就是这件事。（bgp_open.c 里 GR/FQDN 回填长度时
 *   写 len-1、而 ORF 没写，就是这条规则下的差异。）
 *
 * 建议阅读顺序：capability_header / capability_mp_data（TLV 外形）
 *               → CAPABILITY_CODE_* 与 *_LEN（能力清单）
 *               → ORF/GR/LLGR 各自的位定义 → 末尾函数声明。
 * ============================================================================
 */

#ifndef _QUAGGA_BGP_OPEN_H
#define _QUAGGA_BGP_OPEN_H

/* Standard header for capability TLV */
/* 【能力 TLV 的通用头】所有能力都以 code+length 开头，解析时先读这两个字节，
 * 再按 length 取 value。注意：在扩展可选参数(RFC9072)模式下，只有**可选参数**
 * 的长度字段会变成 2 字节，**能力自己的 length 字段仍固定 1 字节**——这个区别
 * 是读 bgp_open.c 解析/构造代码时最常搞混的地方。 */
struct capability_header {
	uint8_t code;
	uint8_t length;
};

/* Generic MP capability data */
/* 【MP-BGP 能力(码 1)的 value】固定 4 字节：(afi16, reserved8, safi8)。
 * 每声明一项“我支持 (afi,safi) 的多协议扩展”。注意这里的 afi/safi 是**线上值**，
 * 类型注释标为 iana_afi_t/iana_safi_t——与 FRR 内部 enum afi_t/safi_t 不是一回事，
 * 转换靠 bgp_map_afi_safi_* 系列函数。 */
struct capability_mp_data {
	uint16_t afi; /* iana_afi_t */
	uint8_t reserved;
	uint8_t safi; /* iana_safi_t */
};

/* 【结构】graceful_restart_af —— GR 能力里“每个地址族一项”的 TLF(RFC4724 §3)。
 * flag 的第 7 位(F 位)表示“该地址族在重启后是否保留转发状态(Forwarding State
 * Preserved)”。本文件后面的 GRACEFUL_RESTART_F_BIT(0x80) 就是它。 */
struct graceful_restart_af {
	uint16_t afi;
	uint8_t safi;
	uint8_t flag;
};

/*
 * +--------------------------------------------------+
 * | Address Family Identifier (16 bits)              |
 * +--------------------------------------------------+
 * | Subsequent Address Family Identifier (8 bits)    |
 * +--------------------------------------------------+
 * | Flags for Address Family (8 bits)                |
 * +--------------------------------------------------+
 * | Long-lived Stale Time (24 bits)                  |
 * +--------------------------------------------------+
 */
/* LLGR(码71) 里每个地址族 TLF 的最小长度：afi(2)+safi(1)+flags(1)+stale_time(3)=7。
 * 校验对端报文时用它做“别读越界”的下限检查。 */
#define BGP_CAP_LLGR_MIN_PACKET_LEN 7

/* Capability Code */
/* 【BGP 能力码总表】括号内是定义它的 RFC/草案。这些码值直接出现在 OPEN 报文的
 * 可选参数里，抓包时对号入座：
 *   1  MP(RFC4760)  2  Route Refresh(RFC2918)  3  ORF(RFC5291)
 *   5  ENHE(RFC5549)  6  Extended Message(RFC8654)  9  Role(RFC9234)
 *   64 GR(RFC4724)  65 AS4(RFC6793)  67 动态能力(draft)  69 Add-Path(RFC7911)
 *   70 增强 Route Refresh(BoRR/EoRR, RFC7313)  71 LLGR(RFC9494)
 *   73 FQDN主机名  75 软件版本  76 Paths Limit(draft)  77 Link-Local(draft)
 * 注意 5/6/9 是散落在小数值区间的“后来者”，不是按时间顺序编号。 */
#define CAPABILITY_CODE_MP              1 /* Multiprotocol Extensions */
#define CAPABILITY_CODE_REFRESH         2 /* Route Refresh Capability */
#define CAPABILITY_CODE_ORF             3 /* Cooperative Route Filtering Capability */
#define CAPABILITY_CODE_RESTART        64 /* Graceful Restart Capability */
#define CAPABILITY_CODE_AS4            65 /* 4-octet AS number Capability */
#define CAPABILITY_CODE_DYNAMIC        67 /* Dynamic Capability */
#define CAPABILITY_CODE_ADDPATH        69 /* Addpath Capability */
#define CAPABILITY_CODE_ENHANCED_RR    70 /* Enhanced Route Refresh capability */
#define CAPABILITY_CODE_LLGR           71 /* Long-lived Graceful Restart */
#define CAPABILITY_CODE_FQDN           73 /* Advertise hostname capability */
#define CAPABILITY_CODE_SOFT_VERSION   75 /* Software Version capability */
#define CAPABILITY_CODE_ENHE            5 /* Extended Next Hop Encoding */
#define CAPABILITY_CODE_EXT_MESSAGE     6 /* Extended Message Support */
#define CAPABILITY_CODE_ROLE            9 /* Role Capability */
#define CAPABILITY_CODE_PATHS_LIMIT    76 /* Paths Limit Capability */
#define CAPABILITY_CODE_LINK_LOCAL	77 /* draft-white-linklocal-capability */

/* Capability Length */
/* 【各能力 value 的典型长度】
 * 注意语义不完全统一：
 *   - 大多数是“固定长度”的校验值（如 MP=4、AS4=4、ADDPATH=4）。
 *   - RESTART_LEN=2 注释写明是 “Receiving only case” —— 即“只收不发”场景下
 *     本端发出的 GR 能力最小长度（只带标志+时间，不带任何地址族 TLF）；
 *     实际发出长度取决于配置了多少个地址族，所以**不是**精确长度。
 *   - MIN_FQDN_LEN 带 MIN 前缀，是下限。
 *   - 值为 0 的那些(REFRESH/DYNAMIC/ENHANCED/LLGR/EXT_MESSAGE/LINK_LOCAL)
 *     表示该能力没有 value（纯能力位），length 字段写 0。
 * 校验时把它们当精确长度直接用会误判 GR/FQDN，需结合具体解析代码看。 */
#define CAPABILITY_CODE_MP_LEN          4
#define CAPABILITY_CODE_REFRESH_LEN     0
#define CAPABILITY_CODE_DYNAMIC_LEN     0
#define CAPABILITY_CODE_RESTART_LEN     2 /* Receiving only case */
#define CAPABILITY_CODE_AS4_LEN         4
#define CAPABILITY_CODE_ADDPATH_LEN     4
#define CAPABILITY_CODE_PATHS_LIMIT_LEN 5
#define CAPABILITY_CODE_ENHE_LEN        6 /* NRLI AFI = 2, SAFI = 2, Nexthop AFI = 2 */
#define CAPABILITY_CODE_MIN_FQDN_LEN    2
#define CAPABILITY_CODE_ENHANCED_LEN    0
#define CAPABILITY_CODE_LLGR_LEN        0
#define CAPABILITY_CODE_ORF_LEN         5
#define CAPABILITY_CODE_EXT_MESSAGE_LEN 0 /* Extended Message Support */
#define CAPABILITY_CODE_ROLE_LEN        1
#define CAPABILITY_CODE_SOFT_VERSION_LEN 1
#define CAPABILITY_CODE_LINK_LOCAL_LEN	 0

/* Cooperative Route Filtering Capability.  */
/* ========== ORF(RFC5291)：让对端替我过滤，减少不必要的路由传输 ========== */

/* ORF Type */
/* ORF_TYPE_PREFIX(64)：目前唯一实现的类型——按前缀过滤。 */
#define ORF_TYPE_RESERVED               0
#define ORF_TYPE_PREFIX                64

/* ORF Mode */
/* ORF 是**双向**的，协商时各自声明自己的方向：
 *   RECEIVE(1) : 我愿意接收对端发来的 ORF 条目（即请对端来过滤我发的路由）→
 *                对应 bgp 配置里的 orf prefix-list ... receive。
 *   SEND(2)    : 我会向对端发送 ORF 条目（即我要求对端按我的前缀表过滤）→
 *                对应 send。
 *   BOTH(3)    : 两个方向都开。
 * 记忆窍门：**Receive = 我收 ORF（对方过滤）；Send = 我发 ORF（我过滤对方）**，
 * 这里“收/发”指的是 ORF 条目本身，不是路由，极易搞反。 */
#define ORF_MODE_RECEIVE                1
#define ORF_MODE_SEND                   2
#define ORF_MODE_BOTH                   3

/* Capability Message Action.  */
/* 动态能力(draft-ietf-idr-dynamic-cap, 码67)协商时用：
 *   SET   = 请对端**启用**某能力（不重开连接即可增删能力）。
 *   UNSET = 请对端**关闭**某能力。
 * 普通(非动态)能力协商不使用这两个值。 */
#define CAPABILITY_ACTION_SET           0
#define CAPABILITY_ACTION_UNSET         1

/* Graceful Restart */
/* GR 能力 value 的前 2 字节是标志+时间：高 4 位是 Flags，低 12 位是 Restart Time。
 *   R 位(0x8000) : 发送方**自己**刚刚/正在重启（真正的重启方才会置）。
 *   N 位(0x4000) : 发送方支持“优雅通知(Notification)”——即用 NOTIFY 的
 *                  Hard Reset 子码告知对端“我要重启了”，见 RFC8538。
 *   F 位(0x80)   : 用于**地址族 TLF 的 flag 字段**（不是上面 2 字节里的位！），
 *                  表示该地址族重启期间保留转发状态。
 * 注意 0x8000/0x4000 是 16 位字里的位、0x80 是 8 位字节里的位——两处不要混用。 */
#define GRACEFUL_RESTART_R_BIT 0x8000
#define GRACEFUL_RESTART_N_BIT 0x4000
#define GRACEFUL_RESTART_F_BIT 0x80

/* Long-lived Graceful Restart */
/* LLGR(RFC9494) 地址族 TLF 的 F 位：表示该地址族支持“长时间保留陈旧路由”
 * （带 LLGR_STALE 团体，保留时长可达数小时/数天，用于跨维护窗口）。 */
#define LLGR_F_BIT 0x80

/* Optional Parameters */
/* RFC9072 扩展可选参数：非扩展格式下长度字段只有 1 字节，最大 255；当所有能力
 * 塞不下 255 字节时，改用扩展格式——长度字段变 2 字节，且**可选参数类型字段写 255**
 * (BGP_OPEN_NON_EXT_OPT_TYPE_EXTENDED_LENGTH) 作为“后面是扩展长度”的标记。
 * BGP_OPEN_EXT_OPT_PARAMS_CAPABLE(peer)：本端是否可走扩展格式。两个来源——
 *   - 静态配置 PEER_FLAG_EXTENDED_OPT_PARAMS；
 *   - 或运行期被置上 PEER_STATUS_EXT_OPT_PARAMS_LENGTH（例如对端明确支持）。
 * 收发两侧都必须用同一判断，否则长度字段宽度不一致会把报文解析错位。 */
#define BGP_OPEN_NON_EXT_OPT_LEN 255		      /* Non-Ext OP Len. */
#define BGP_OPEN_NON_EXT_OPT_TYPE_EXTENDED_LENGTH 255 /* Non-Ext OP Type */
#define BGP_OPEN_EXT_OPT_PARAMS_CAPABLE(peer)                                  \
	(CHECK_FLAG(peer->flags, PEER_FLAG_EXTENDED_OPT_PARAMS)                \
	 || CHECK_FLAG(peer->sflags, PEER_STATUS_EXT_OPT_PARAMS_LENGTH))

/* 【接口说明】
 *   bgp_open_option_parse  : 解析对端 OPEN 的可选参数区（逐个能力 TLV 分派），
 *                            出参 mp_capability 告知“对端是否通告了 MP 能力”。
 *   bgp_open_capability    : 构造本端要发送的能力 TLV 区，返回写入的字节数；
 *                            ext_opt_params 决定用不用 RFC9072 扩展长度格式。
 *   bgp_capability_vty_out : show 命令里把已协商的能力打印出来。
 *   peek_for_as4_capability: 在正式解析前**预读**对端 AS4 能力里的 AS 号
 *                            （因为要先知道对端是谁才能找到对应 peer 配置，
 *                             而 peer 查找在能力解析之前）。
 *   capcode_str/orf_type_str/orf_mode_str : 能力码/ORF 类型/模式的**名字表**，
 *                            用于日志与 show 输出（FRR 的 message 表机制）。
 *   cap_minsizes / cap_modsizes : 各能力的**最小长度**与**长度取模值**校验表，
 *                            用于快速判断对端 TLV 长度是否合法（配合
 *                            CAPABILITY_CODE_*_LEN 使用）。 */
extern int bgp_open_option_parse(struct peer *peer, struct peer_connection *connection,
				 uint16_t length, int *mp_capability);
extern uint16_t bgp_open_capability(struct stream *s, struct peer *peer,
				    struct peer_connection *connection, bool ext_opt_params);
extern void bgp_capability_vty_out(struct vty *vty, struct peer *peer,
				   bool use_json, json_object *json_neigh);
extern as_t peek_for_as4_capability(struct peer_connection *connection, uint16_t length);
extern const struct message capcode_str[];
extern const struct message orf_type_str[];
extern const struct message orf_mode_str[];
extern const size_t cap_minsizes[];
extern const size_t cap_modsizes[];

#endif /* _QUAGGA_BGP_OPEN_H */
