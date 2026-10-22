// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Zebra configuration command interface routine
 * Copyright (C) 1997, 98 Kunihiro Ishiguro
 */

/*
 * 【文件导读】command.h —— FRR 命令树（command tree）的公共接口
 *
 * 本头文件定义了 FRR 整个命令行解析引擎对外暴露的数据结构与宏，是
 * lib/command.c、lib/command_match.c、lib/command_graph.c 三个实现文件的
 * 公共契约。FRR 每个守护进程（bgpd / ospfd / zebra ...）都通过这里的
 * DEFUN / DEFPY 宏把命令注册进命令树，再由 vty 会话实时解析。
 *
 * 核心概念：
 *   cmd_node   —— 一个“配置模式”，例如 CONFIG_NODE（全局配置）、
 *                 INTERFACE_NODE（接口模式）、BGP_NODE 等。它绑定提示串、
 *                 写配置回调，并持有该模式下的命令集合。
 *   node_type  —— 上面的枚举，列出所有已知的配置节点类型。
 *   cmd_element—— 一条具体命令（命令串 + 回调 + 帮助串 + 属性）。
 *   cmd_token  —— 命令行按空格切分、并经过词法类型判定的最小单元；
 *                 命令图（command_graph）由 token 构成有向图。
 *   graph      —— cmd_node 内的命令图，支持并行分支与类型化 token。
 *
 * 一条命令从源码到可执行的流程：
 *   1) 编译期：DEFUN / DEFPY 宏展开成静态的 cmd_element 结构体；
 *   2) 初始化：install_node / install_element 把命令挂到对应 cmd_node，
 *      首次进入节点时由 command.c 调用 command_graph 构建 token 图；
 *   3) 运行期：vty 读到一行 -> cmd_make_strvec 切词 -> cmd_execute_command
 *      在图中匹配 token -> 命中后调用 cmd_element->func 回调。
 *   4) 补全：command_match.c 遍历同一张图，用 token 的类型信息给候选词。
 *
 * 关键易踩的坑：
 *   - 节点类型枚举顺序不能随意改动，command.c 中维护了同名数组；
 *   - 命令串里形如 A.B.C.D / WORD / IPV4 的记号是 token 类型编码，
 *     代表类型化占位符，而不是字面量；
 *   - 帮助串按行用换行符分隔，行数必须与 token 数一一对应。
 */

#ifndef _ZEBRA_COMMAND_H
#define _ZEBRA_COMMAND_H

#include "vector.h"
#include "vty.h"
#include "lib/route_types.h"
#include "graph.h"
#include "memory.h"
#include "hash.h"
#include "command_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

DECLARE_MTYPE(COMPLETION);

/*
 * HOSTNAME_LEN —— 主机名允许的最大长度。按 RFC 1123，主机软件必须能处理
 * 最多 63 字符、并应能容纳最多 255 字符的主机名，这里取 255。
 * 下面的英文块是对应 RFC 原文摘录，原样保留。
 */
/*
 * From RFC 1123 (Requirements for Internet Hosts), Section 2.1 on hostnames:
 * One aspect of host name syntax is hereby changed: the restriction on
 * the first character is relaxed to allow either a letter or a digit.
 * Host software MUST support this more liberal syntax.
 *
 * Host software MUST handle host names of up to 63 characters and
 * SHOULD handle host names of up to 255 characters.
 */
#define HOSTNAME_LEN   255

/*
 * struct host —— 本机（路由器）的全局主机信息缓存。
 *
 * 命令模块持有唯一的 host 实例，保存主机名、域名、版本串、vty 口令、
 * banner(motd) 文件等，多用于 “show version / show running-config” 的只读展示。
 * 注意：password 与 password_encrypt、enable 与 enable_encrypt 成对存在，
 * 分别对应明文与密文；配置回写时优先使用加密版本。lines 是系统级 vty 终端行数。
 */
/* Host configuration variable */
struct host {
	/* Host name of this router. */
	char *name;

	/* Domainname of this router */
	char *domainname;

	/*
	 * Some extra system data that is useful
	 */
	char *system;
	char *release;
	char *version;

	/* Password for vty interface. */
	char *password;
	char *password_encrypt;

	/* Enable password */
	char *enable;
	char *enable_encrypt;

	/* System wide terminal lines. */
	int lines;

	/* config file name of this host */
	char *config;
	int noconfig;

	/* Flags for services */
	int advanced;
	int encrypt;

	/* Banner configuration. */
	char *motd;
	char *motdfile;

	/* Allow using IPv4 (Class E) reserved IP space */
	bool allow_reserved_ranges;
};

/*
 * enum node_type —— 所有 CLI 节点（配置模式）的枚举。
 *
 * 每一项对应一种 vty 提示符模式，枚举值即节点索引，被全局向量 cmdvec 直接下标访问。
 * command.c 里维护了一份同序的节点名字符串数组，新增节点时两处都要同步更新
 * （见上面第 77 行附近的英文提示）。
 * 注意：不要把新节点插到中间，只能追加在 NODE_TYPE_MAX 之前，否则会打乱既有索引。
 */
/* List of CLI nodes. Please remember to update the name array in command.c. */
/* clang-format off */
enum node_type {
	AUTH_NODE,		 /* Authentication mode of vty interface. */
	VIEW_NODE,		 /* View node. Default mode of vty interface. */
	AUTH_ENABLE_NODE,	/* Authentication mode for change enable. */
	ENABLE_NODE,		 /* Enable node. */
	CONFIG_NODE,		 /* Config node. Default mode of config file. */
	PREFIX_NODE, /* ip prefix-list node. */
	PREFIX_IPV6_NODE, /* ipv6 prefix-list node. */
	RMAP_NODE, /* Route map node. */
	LIB_DEBUG_NODE,		 /* frrlib debug node. */
	DEBUG_NODE,		 /* Debug node. */
	VRF_DEBUG_NODE,		 /* Vrf Debug node. */
	DEBUG_VNC_NODE,		 /* Debug VNC node. */
	RMAP_DEBUG_NODE,         /* Route-map debug node */
	RESOLVER_DEBUG_NODE,	 /* Resolver debug node */
	AAA_NODE,		 /* AAA node. */
	EXTLOG_NODE,		 /* RFC5424 & co. extended syslog */
	KEYCHAIN_NODE,		 /* Key-chain node. */
	KEYCHAIN_KEY_NODE,       /* Key-chain key node. */
	AFFMAP_NODE,		 /* Affinity map node. */
	IP_NODE,		 /* Static ip route node. */
	VRF_NODE,		 /* VRF mode node. */
	INTERFACE_NODE,		 /* Interface mode node. */
	NH_GROUP_NODE,		 /* Nexthop-Group mode node. */
	ZEBRA_NODE,		 /* zebra connection node. */
	RIP_NODE,		 /* RIP protocol mode node. */
	RIPNG_NODE,		 /* RIPng protocol mode node. */
	BABEL_NODE,		 /* BABEL protocol mode node. */
	EIGRP_NODE,		 /* EIGRP protocol mode node. */
	BGP_NODE,		 /* BGP protocol mode which includes BGP4+ */
	BGP_VPNV4_NODE,		 /* BGP MPLS-VPN PE exchange. */
	BGP_VPNV6_NODE,		 /* BGP MPLS-VPN PE exchange. */
	BGP_IPV4_NODE,		 /* BGP IPv4 unicast address family.  */
	BGP_IPV4M_NODE,		 /* BGP IPv4 multicast address family.  */
	BGP_IPV4L_NODE,		 /* BGP IPv4 labeled unicast address family.  */
	BGP_IPV6_NODE,		 /* BGP IPv6 address family */
	BGP_IPV6M_NODE,		 /* BGP IPv6 multicast address family. */
	BGP_IPV6L_NODE,		 /* BGP IPv6 labeled unicast address family. */
	BGP_VRF_POLICY_NODE,     /* BGP VRF policy */
	BGP_VNC_DEFAULTS_NODE,   /* BGP VNC nve defaults */
	BGP_VNC_NVE_GROUP_NODE,  /* BGP VNC nve group */
	BGP_VNC_L2_GROUP_NODE,   /* BGP VNC L2 group */
	BGP_EVPN_NODE,		 /* BGP EVPN node. */
	BGP_SRV6_NODE,		 /* BGP SRv6 node. */
	OSPF_NODE,		 /* OSPF protocol mode */
	OSPF6_NODE,		 /* OSPF protocol for IPv6 mode */
	LDP_NODE,		 /* LDP protocol mode */
	LDP_IPV4_NODE,		 /* LDP IPv4 address family */
	LDP_IPV6_NODE,		 /* LDP IPv6 address family */
	LDP_IPV4_IFACE_NODE,     /* LDP IPv4 Interface */
	LDP_IPV6_IFACE_NODE,     /* LDP IPv6 Interface */
	LDP_L2VPN_NODE,		 /* LDP L2VPN node */
	LDP_PSEUDOWIRE_NODE,     /* LDP Pseudowire node */
	ISIS_NODE,		 /* ISIS protocol mode */
	ISIS_FLEX_ALGO_NODE,    /* ISIS Flex Algo mode */
	ACCESS_NODE,		 /* Access list node. */
	ACCESS_IPV6_NODE,	/* Access list node. */
	ACCESS_MAC_NODE,	 /* MAC access list node*/
	AS_LIST_NODE,		 /* AS list node. */
	COMMUNITY_LIST_NODE,     /* Community list node. */
	COMMUNITY_ALIAS_NODE, /* Community alias node. */
	PBRMAP_NODE,		 /* PBR map node. */
	SMUX_NODE,		 /* SNMP configuration node. */
	DUMP_NODE,		 /* Packet dump node. */
	PROTOCOL_NODE,		 /* protocol filtering node */
	MPLS_NODE,		 /* MPLS config node */
	PW_NODE,		 /* Pseudowire config node */
	SEGMENT_ROUTING_NODE,	 /* Segment routing root node */
	SR_TRAFFIC_ENG_NODE,	 /* SR Traffic Engineering node */
	SR_SEGMENT_LIST_NODE,	 /* SR segment list config node */
	SR_POLICY_NODE,		 /* SR policy config node */
	SR_CANDIDATE_DYN_NODE,	 /* SR dynamic candidate path config node */
	PCEP_NODE,	 	 /* PCEP node */
	PCEP_PCE_CONFIG_NODE,	 /* PCE shared configuration node */
	PCEP_PCE_NODE,		 /* PCE configuration node */
	PCEP_PCC_NODE,		 /* PCC configuration node */
	SRV6_NODE,		 /* SRv6 node */
	SRV6_SIDS_NODE,		 /* SRv6 SIDs node */
	SRV6_LOCS_NODE,		 /* SRv6 locators node */
	SRV6_LOC_NODE,		 /* SRv6 locator node */
	SRV6_ENCAP_NODE,		 /* SRv6 encapsulation node */
	SRV6_SID_FORMATS_NODE,		 /* SRv6 SID formats config node */
	SRV6_SID_FORMAT_USID_F3216_NODE,		 /* SRv6 uSID f3216 format config node */
	SRV6_SID_FORMAT_USID_F4816_NODE,		 /* SRv6 uSID f4816 format config node */
	SRV6_SID_FORMAT_UNCOMPRESSED_F4024_NODE,		 /* SRv6 uncompressed f4024 format config node */
	VTY_NODE,		 /* Vty node. */
	FPM_NODE,		 /* Dataplane FPM node. */
	LINK_PARAMS_NODE,	/* Link-parameters node */
	BGP_EVPN_VNI_NODE,       /* BGP EVPN VNI */
	RPKI_NODE,     /* RPKI node for configuration of RPKI cache server
			  connections.*/
	BGP_FLOWSPECV4_NODE,	/* BGP IPv4 FLOWSPEC Address-Family */
	BGP_FLOWSPECV6_NODE,	/* BGP IPv6 FLOWSPEC Address-Family */
	BFD_NODE,		 /* BFD protocol mode. */
	BFD_PEER_NODE,		 /* BFD peer configuration mode. */
	BFD_PROFILE_NODE,	 /* BFD profile configuration mode. */
	OPENFABRIC_NODE,	/* OpenFabric router configuration node */
	VRRP_NODE,		 /* VRRP node */
	BMP_NODE,		/* BMP config under router bgp */
	ISIS_SRV6_NODE,    /* ISIS SRv6 node */
	ISIS_SRV6_NODE_MSD_NODE,    /* ISIS SRv6 Node MSDs node */
	MGMTD_NODE,		 /* MGMTD node. */
	RPKI_VRF_NODE,  /* RPKI node for VRF */
	PIM_NODE,		 /* PIM protocol mode */
	PIM6_NODE,		 /* PIM protocol for IPv6 mode */
	NODE_TYPE_MAX, /* maximum */
};
/* clang-format on */

extern vector cmdvec;
extern const struct message tokennames[];

/* for external users depending on struct layout */
#define FRR_CMD_NODE_20200416

/*
 * struct cmd_node —— 描述一个 CLI 节点（配置模式）的全部元数据。
 *
 * node / parent_node：本节点类型与父节点类型（用于 “exit” 时回退到哪一级）。
 * prompt：该模式下的提示符字符串，例如 “(config-if)#”。
 * config_write：用户执行 write / show running-config 时被调用，负责输出本节点配置。
 * node_exit：离开本节点时的钩子，返回 1 表示继续走正常的退栈处理，0 表示抑制。
 * cmdgraph：本节点的命令图（token 有向图），首条命令安装时由 command_graph 构建。
 * cmd_vector / cmd_hash：命令元素列表及去重用的哈希（安装命令时避免重复）。
 * graph_built：命令图是否已经构建完成。
 * no_xpath：离开本节点时是否不要回退 vty->xpath_index（MGMTD / 特殊节点用）。
 */
/* Node which has some commands and prompt string and configuration
   function pointer . */
struct cmd_node {
	const char *name;

	/* Node index. */
	enum node_type node;
	enum node_type parent_node;

	/* Prompt character at vty interface. */
	const char *prompt;

	/* Node's configuration write function */
	int (*config_write)(struct vty *);

	/* called when leaving the node on a VTY session.
	 * return 1 if normal exit processing should happen, 0 to suppress
	 */
	int (*node_exit)(struct vty *);

	/* Node's command graph */
	struct graph *cmdgraph;

	/* Vector of this node's command list. */
	vector cmd_vector;

	/* Hashed index of command node list, for de-dupping primarily */
	struct hash *cmd_hash;

	/* set as soon as any command is in cmdgraph */
	bool graph_built;

	/* don't decrement vty->xpath_index on leaving this node */
	bool no_xpath;
};

/*
 * 命令回调与解析器的返回码。
 *
 * CMD_SUCCESS / CMD_WARNING ... 是命令回调函数的通用返回值；
 * CMD_COMPLETE_* 是解析器对“本行已输入完整、可执行/可补全”的匹配结论；
 * CMD_ERR_* 表示各类失败（无匹配 / 歧义 / 未完整 / argc 溢出 / 无此实例等）。
 * 命令行层最终根据这些返回值决定是执行、继续补全还是给出错误提示。
 */
/* Return value of the commands. */
#define CMD_SUCCESS              0
#define CMD_WARNING              1
#define CMD_ERR_NO_MATCH         2
#define CMD_ERR_AMBIGUOUS        3
#define CMD_ERR_INCOMPLETE       4
#define CMD_ERR_EXEED_ARGC_MAX   5
#define CMD_ERR_NOTHING_TODO     6
#define CMD_COMPLETE_FULL_MATCH  7
#define CMD_COMPLETE_MATCH       8
#define CMD_COMPLETE_LIST_MATCH  9
#define CMD_SUCCESS_DAEMON      10
#define CMD_ERR_NO_FILE         11
#define CMD_SUSPEND             12
#define CMD_WARNING_CONFIG_FAILED 13
#define CMD_NOT_MY_INSTANCE	14
#define CMD_NO_LEVEL_UP 15
#define CMD_ERR_NO_DAEMON 16

/* Argc max counts. */
/* CMD_ARGC_MAX —— 单条命令允许的最大 token（参数）个数；超出即报 CMD_ERR_EXEED_ARGC_MAX。 */
#define CMD_ARGC_MAX   256

/* clang-format off */

/*
 * DEFUN / DEFPY 系列宏 —— 让一条命令“声明 + 定义”一步到位。
 *
 * DEFUN(函数名, 命令变量名, 命令串, 帮助串) 展开出三部分：
 *   1) 函数前置声明          —— DEFUN_CMD_FUNC_DECL；
 *   2) 静态 cmd_element 结构体 —— DEFUN_CMD_ELEMENT，含 .string/.func/.doc 等；
 *   3) 函数体头部            —— DEFUN_CMD_FUNC_TEXT。
 * DEFPY 会再经 clippy 预处理，把命令串中的 $变量 抽取成函数的具名形参。
 * DEFSH / DEFUNSH 只注册命令、不实现（给 vtysh 转发用）；ALIAS 为已存在的
 * 回调再挂一条命令字符串。
 * 注意：命令变量名（cmdname）即 struct cmd_element 的变量名，必须全局唯一。
 */
/* helper defines for end-user DEFUN* macros */
#define DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, attrs, dnum)     \
	const struct cmd_element cmdname = {                                   \
		.string = cmdstr,                                              \
		.func = funcname,                                              \
		.doc = helpstr,                                                \
		.attr = attrs,                                                 \
		.daemon = dnum,                                                \
		.name = #cmdname,                                              \
		.xref = XREF_INIT(XREFT_DEFUN, NULL, #funcname),               \
	};                                                                     \
	XREF_LINK(cmdname.xref);                                               \
	/* end */


#define DEFUN_CMD_FUNC_DECL(funcname)                                          \
	static int funcname(const struct cmd_element *, struct vty *, int,     \
			    struct cmd_token *[]);

#define DEFUN_CMD_FUNC_TEXT(funcname)                                          \
	static int funcname(const struct cmd_element *self                     \
			    __attribute__((unused)),                           \
			    struct vty *vty __attribute__((unused)),           \
			    int argc __attribute__((unused)),                  \
			    struct cmd_token *argv[] __attribute__((unused)))

/*
 * DEFPY 变体 —— 与 DEFUN 用法相同，但产生的是 DEFPY_ATTR：内部以
 * DEFUN_CMD_ELEMENT 加 daemon=0 注册，函数体形参由 clippy 依据命令串自动生成。
 */
/* DEFPY variants */

#define DEFPY_ATTR(funcname, cmdname, cmdstr, helpstr, attr)                   \
	static DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, attr, 0)  \
	funcdecl_##funcname

#define DEFPY(funcname, cmdname, cmdstr, helpstr)                              \
	DEFPY_ATTR(funcname, cmdname, cmdstr, helpstr, 0)

#define DEFPY_NOSH(funcname, cmdname, cmdstr, helpstr)                         \
	DEFPY_ATTR(funcname, cmdname, cmdstr, helpstr, CMD_ATTR_NOSH)

#define DEFPY_HIDDEN(funcname, cmdname, cmdstr, helpstr)                       \
	DEFPY_ATTR(funcname, cmdname, cmdstr, helpstr, CMD_ATTR_HIDDEN)

#define DEFPY_YANG(funcname, cmdname, cmdstr, helpstr)                         \
	DEFPY_ATTR(funcname, cmdname, cmdstr, helpstr, CMD_ATTR_YANG)

#define DEFPY_YANG_HIDDEN(funcname, cmdname, cmdstr, helpstr)                  \
	DEFPY_ATTR(funcname, cmdname, cmdstr, helpstr,                         \
		   CMD_ATTR_YANG | CMD_ATTR_HIDDEN)

#define DEFPY_YANG_NOSH(funcname, cmdname, cmdstr, helpstr)                    \
	DEFPY_ATTR(funcname, cmdname, cmdstr, helpstr,                         \
		   CMD_ATTR_YANG | CMD_ATTR_NOSH)

/*
 * DEFUN 变体 —— DEFUN_ATTR 是基础形式；DEFUN 无属性，其余 *_HIDDEN / *_YANG /
 * *_NOSH 分别附加 CMD_ATTR_HIDDEN / CMD_ATTR_YANG / CMD_ATTR_NOSH 属性位。
 */
/* DEFUN variants */

#define DEFUN_ATTR(funcname, cmdname, cmdstr, helpstr, attr)                   \
	DEFUN_CMD_FUNC_DECL(funcname)                                          \
	static DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, attr, 0)  \
	DEFUN_CMD_FUNC_TEXT(funcname)

#define DEFUN(funcname, cmdname, cmdstr, helpstr)                              \
	DEFUN_ATTR(funcname, cmdname, cmdstr, helpstr, 0)

#define DEFUN_HIDDEN(funcname, cmdname, cmdstr, helpstr)                       \
	DEFUN_ATTR(funcname, cmdname, cmdstr, helpstr, CMD_ATTR_HIDDEN)

#define DEFUN_YANG(funcname, cmdname, cmdstr, helpstr)                         \
	DEFUN_ATTR(funcname, cmdname, cmdstr, helpstr, CMD_ATTR_YANG)

/* DEFUN_NOSH for commands that vtysh should ignore */
#define DEFUN_NOSH(funcname, cmdname, cmdstr, helpstr)                         \
	DEFUN_ATTR(funcname, cmdname, cmdstr, helpstr, CMD_ATTR_NOSH)

#define DEFUN_YANG_HIDDEN(funcname, cmdname, cmdstr, helpstr)                  \
	DEFUN_ATTR(funcname, cmdname, cmdstr, helpstr,                         \
		   CMD_ATTR_YANG | CMD_ATTR_HIDDEN)

#define DEFUN_YANG_NOSH(funcname, cmdname, cmdstr, helpstr)                    \
	DEFUN_ATTR(funcname, cmdname, cmdstr, helpstr,                         \
		   CMD_ATTR_YANG | CMD_ATTR_NOSH)

/*
 * DEFSH / DEFUNSH 变体 —— 面向 vtysh 的“只注册靠山”命令。
 * daemon 参数指明该命令属于哪个守护进程；func 为 NULL 时表示命令只被登记
 * （DEFSH），由 vtysh 转发给真正的守护进程执行；DEFUNSH 则同时提供本地函数体。
 */
/* DEFSH for vtysh. */
#define DEFSH_ATTR(daemon, cmdname, cmdstr, helpstr, attr)                     \
	DEFUN_CMD_ELEMENT(NULL, cmdname, cmdstr, helpstr, attr, daemon)

#define DEFSH(daemon, cmdname, cmdstr, helpstr)                                \
	DEFSH_ATTR(daemon, cmdname, cmdstr, helpstr, 0)

#define DEFSH_HIDDEN(daemon, cmdname, cmdstr, helpstr)                         \
	DEFSH_ATTR(daemon, cmdname, cmdstr, helpstr, CMD_ATTR_HIDDEN)

/* DEFUN + DEFSH */
#define DEFUNSH_ATTR(daemon, funcname, cmdname, cmdstr, helpstr, attr)         \
	DEFUN_CMD_FUNC_DECL(funcname)                                          \
	static DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, attr,     \
				 daemon)                                       \
	DEFUN_CMD_FUNC_TEXT(funcname)

#define DEFUNSH(daemon, funcname, cmdname, cmdstr, helpstr)                    \
	DEFUNSH_ATTR(daemon, funcname, cmdname, cmdstr, helpstr, 0)

#define DEFUNSH_HIDDEN(daemon, funcname, cmdname, cmdstr, helpstr)             \
	DEFUNSH_ATTR(daemon, funcname, cmdname, cmdstr, helpstr,               \
		     CMD_ATTR_HIDDEN)

/*
 * ALIAS 系列 —— 为一条“已存在的回调函数”再挂上一条新的命令字符串，
 * 属于同一命令的别名/历史写法，不产生新的回调。
 * ALIAS_DEPRECATED 隐含 HIDDEN，仅用于表达“即将废弃”的意图（见上方英文注释）。
 */
/* ALIAS macro which define existing command's alias. */
#define ALIAS_ATTR(funcname, cmdname, cmdstr, helpstr, attr)                   \
	static DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, attr, 0)

#define ALIAS(funcname, cmdname, cmdstr, helpstr)                              \
	ALIAS_ATTR(funcname, cmdname, cmdstr, helpstr, 0)

#define ALIAS_HIDDEN(funcname, cmdname, cmdstr, helpstr)                       \
	ALIAS_ATTR(funcname, cmdname, cmdstr, helpstr, CMD_ATTR_HIDDEN)

/* note: DEPRECATED implies HIDDEN, and other than that there is currently no
 * difference.  It's purely for expressing intent in the source code - a
 * DEPRECATED command is supposed to go away, a HIDDEN one is likely to stay.
 */
#define ALIAS_DEPRECATED(funcname, cmdname, cmdstr, helpstr)                   \
	ALIAS_ATTR(funcname, cmdname, cmdstr, helpstr,                         \
		   CMD_ATTR_DEPRECATED | CMD_ATTR_HIDDEN)

#define ALIAS_YANG(funcname, cmdname, cmdstr, helpstr)                         \
	ALIAS_ATTR(funcname, cmdname, cmdstr, helpstr, CMD_ATTR_YANG)

/* clang-format on */

/*
 * CMD_CREATE_STR / CMD_RANGE_STR —— 把宏常量“字符串化”，以便命令解析时按字面匹配。
 * 例：CMD_CREATE_STR(64) 得到 "64"；CMD_RANGE_STR(a,s) 生成 "(a-s)" 形式的区间 token。
 */
/* Some macroes */

/*
 * Sometimes #defines create maximum values that
 * need to have strings created from them that
 * allow the parser to match against them.
 * These macros allow that.
 */
#define CMD_CREATE_STR(s)  CMD_CREATE_STR_HELPER(s)
#define CMD_CREATE_STR_HELPER(s) #s
#define CMD_RANGE_STR(a,s) "(" CMD_CREATE_STR(a) "-" CMD_CREATE_STR(s) ")"

/*
 * 常用帮助串宏 —— 这些字符串用于命令的 .doc（帮助），在 “?” 补全时逐行显示。
 * 帮助串的行数/顺序必须与被描述命令的 token 一一对应，故许多命令会直接
 * 复用这些预定义串（如 SHOW_STR / NO_STR / INTERFACE_STR）。
 * 带 "str" 后缀的 *_STR 结尾都带换行，注意别在拼接时漏掉分隔。
 */
/* Common descriptions. */
#define SHOW_STR "Show running system information\n"
#define IP_STR "IP information\n"
#define IPV6_STR "IPv6 information\n"
#define IP_ADDR_STR "IPv4 Address\n"
#define IP6_ADDR_STR "IPv6 Address\n"
#define SRTE_STR "SR-TE information\n"
#define SRTE_COLOR_STR "SR-TE Color information\n"
#define NO_STR "Negate a command or set its defaults\n"
#define IGNORED_IN_NO_STR "Ignored value in no form\n"
#define REDIST_STR "Redistribute information from another routing protocol\n"
#define CLEAR_STR "Reset functions\n"
#define RIP_STR "RIP information\n"
#define EIGRP_STR "EIGRP information\n"
#define BGP_STR "BGP information\n"
#define BGP_SOFT_STR "Soft reconfig inbound and outbound updates\n"
#define BGP_SOFT_IN_STR "Send route-refresh unless using 'soft-reconfiguration inbound'\n"
#define BGP_SOFT_OUT_STR "Resend all outbound updates\n"
#define BGP_SOFT_RSCLIENT_RIB_STR "Soft reconfig for rsclient RIB\n"
#define OSPF_STR "OSPF information\n"
#define NEIGHBOR_STR "Specify neighbor router\n"
#define DEBUG_STR "Debugging functions\n"
#define UNDEBUG_STR "Disable debugging functions (see also 'debug')\n"
#define ROUTER_STR "Enable a routing process\n"
#define AS_STR                                                                 \
	"AS number in plain  <1-4294967295> or dotted <0-65535>.<0-65535> format\n"
#define MAC_STR "MAC address\n"
#define MBGP_STR "MBGP information\n"
#define MATCH_STR "Match values from routing table\n"
#define SET_STR "Set values in destination routing protocol\n"
#define OUT_STR "Filter outgoing routing updates\n"
#define IN_STR  "Filter incoming routing updates\n"
#define V4NOTATION_STR "specify by IPv4 address notation(e.g. 0.0.0.0)\n"
#define OSPF6_NUMBER_STR "Specify by number\n"
#define INTERFACE_STR "Interface information\n"
#define IFNAME_STR "Interface name(e.g. ep0)\n"
#define IP6_STR "IPv6 Information\n"
#define OSPF6_STR "Open Shortest Path First (OSPF) for IPv6\n"
#define OSPF6_INSTANCE_STR "(1-65535) Instance ID\n"
#define SECONDS_STR "Seconds\n"
#define ROUTE_STR "Routing Table\n"
#define PREFIX_LIST_STR "Build a prefix list\n"
#define OSPF6_DUMP_TYPE_LIST                                                   \
	"<neighbor|interface|area|lsa|zebra|config|dbex|spf|route|lsdb|redistribute|hook|asbr|prefix|abr>"
#define AREA_TAG_STR "[area tag]\n"
#define COMMUNITY_AANN_STR "Community number where AA and NN are (0-65535)\n"
#define COMMUNITY_VAL_STR                                                      \
	"Community number in AA:NN format (where AA and NN are (0-65535)) or local-AS|no-advertise|no-export|graceful-shutdown|accept-own-nexthop|accept-own|route-filter-translated-v4|route-filter-v4|route-filter-translated-v6|route-filter-v6|llgr-stale|no-llgr|blackhole|no-peer or additive\n"
#define EXTCOMM_LIST_CMD_STR "<(1-99)|(100-500)|EXTCOMMUNITY_LIST_NAME>"
#define EXTCOMM_STD_LIST_NUM_STR "Extended community-list number (standard)\n"
#define EXTCOMM_EXP_LIST_NUM_STR "Extended community-list number (expanded)\n"
#define EXTCOMM_LIST_NAME_STR "Extended community-list name\n"
#define MPLS_TE_STR "MPLS-TE specific commands\n"
#define LINK_PARAMS_STR "Configure interface link parameters\n"
#define OSPF_RI_STR "OSPF Router Information specific commands\n"
#define PCE_STR "PCE Router Information specific commands\n"
#define MPLS_STR "MPLS information\n"
#define SR_STR "Segment-Routing specific commands\n"
#define WATCHFRR_STR "watchfrr information\n"
#define ZEBRA_STR "Zebra information\n"
#define FILTER_LOG_STR "Filter Logs\n"
#define BFD_PROFILE_STR "BFD profile.\n"
#define BFD_PROFILE_NAME_STR "BFD profile name.\n"
#define SHARP_STR "Sharp Routing Protocol\n"
#define OSPF_GR_STR                                                            \
	"OSPF non-stop forwarding (NSF) also known as OSPF Graceful Restart\n"
#define MGMTD_STR "Management Daemon (MGMTD) information\n"
#define MGMTD_BE_ADAPTER_STR "MGMTD Backend Adapter information\n"
#define MGMTD_FE_ADAPTER_STR "MGMTD Frontend Adapter information\n"
#define MGMTD_TXN_STR "MGMTD Transaction information\n"
#define MGMTD_DS_STR "MGMTD Datastore information\n"

#define CMD_VNI_RANGE "(1-16777215)"
#define CONF_BACKUP_EXT ".sav"
#define MPLS_LDP_SYNC_STR "Enable MPLS LDP-SYNC\n"
#define NO_MPLS_LDP_SYNC_STR "Disable MPLS LDP-SYNC\n"
#define MPLS_LDP_SYNC_HOLDDOWN_STR                                             \
	"Time to wait for LDP-SYNC to occur before restoring if cost\n"
#define NO_MPLS_LDP_SYNC_HOLDDOWN_STR "holddown timer disable\n"
#define BGP_AF_STR "Address Family\n"
#define BGP_AF_MODIFIER_STR "Address Family modifier\n"

/* Command warnings. */
/* 命令级警告文案：例如删除口令时要提示安全风险。 */
#define NO_PASSWD_CMD_WARNING                                                  \
	"Please be aware that removing the password is a security risk and you should think twice about this command.\n"

/* IPv4 only machine should not accept IPv6 address for peer's IP
   address.  So we replace VTY command string like below. */
#define NEIGHBOR_ADDR_STR  "Neighbor address\nIPv6 address\n"
#define NEIGHBOR_ADDR_STR2 "Neighbor address\nNeighbor IPv6 address\nInterface name or neighbor tag\n"
#define NEIGHBOR_ADDR_STR3 "Neighbor address\nIPv6 address\nInterface name\n"

/* Graceful Restart cli help strings */
#define GR_CMD "Global Graceful Restart command\n"
#define NO_GR_CMD "Undo Global Graceful Restart command\n"
#define GR_DISABLE "Global Graceful Restart - Disable Mode\n"
#define NO_GR_DISABLE "Undo Global Graceful Restart - Disable Mode\n"
#define GR_DEBUG "Graceful Restart - Enable Debug Logs\n"
#define GR_SHOW "Graceful Restart - Show command for Global and all neighbor mode\n"
#define GR_NEIGHBOR_CMD "Graceful Restart command for a neighbor\n"
#define NO_GR_NEIGHBOR_CMD "Undo Graceful Restart command for a neighbor\n"
#define GR_NEIGHBOR_DISABLE_CMD "Graceful Restart Disable command for a neighbor\n"
#define NO_GR_NEIGHBOR_DISABLE_CMD "Undo Graceful Restart Disable command for a neighbor\n"
#define GR_NEIGHBOR_HELPER_CMD "Graceful Restart Helper command for a neighbor\n"
#define NO_GR_NEIGHBOR_HELPER_CMD "Undo Graceful Restart Helper command for a neighbor\n"

/* EVPN help Strings */
#define EVPN_RT_HELP_STR "EVPN route information\n"
#define EVPN_RT_DIST_HELP_STR "Route Distinguisher\n"
#define EVPN_ASN_IP_HELP_STR "ASN:XX or A.B.C.D:XX\n"
#define EVPN_TYPE_HELP_STR "Specify Route type\n"
#define EVPN_TYPE_1_HELP_STR "EAD (Type-1) route\n"
#define EVPN_TYPE_2_HELP_STR "MAC-IP (Type-2) route\n"
#define EVPN_TYPE_3_HELP_STR "Multicast (Type-3) route\n"
#define EVPN_TYPE_4_HELP_STR "Ethernet Segment (Type-4) route\n"
#define EVPN_TYPE_5_HELP_STR "Prefix (Type-5) route\n"
#define EVPN_TYPE_ALL_LIST "<ead|1|macip|2|multicast|3|es|4|prefix|5>"
#define EVPN_TYPE_ALL_LIST_HELP_STR                                            \
	EVPN_TYPE_1_HELP_STR EVPN_TYPE_1_HELP_STR                              \
	EVPN_TYPE_2_HELP_STR EVPN_TYPE_2_HELP_STR                              \
	EVPN_TYPE_3_HELP_STR EVPN_TYPE_3_HELP_STR                              \
	EVPN_TYPE_4_HELP_STR EVPN_TYPE_4_HELP_STR                              \
	EVPN_TYPE_5_HELP_STR EVPN_TYPE_5_HELP_STR

/* Describing roles */
#define ROLE_STR                                                               \
	"Providing transit\nRoute server\nRS client\nUsing transit\nPublic/private peering\n"

/* BFD protocol integration strings. */
#define BFD_INTEGRATION_STR "BFD monitoring\n"
#define BFD_INTEGRATION_MULTI_HOP_STR "Use BFD multi hop session\n"
#define BFD_INTEGRATION_SOURCE_STR "Use source for BFD session\n"
#define BFD_INTEGRATION_SOURCEV4_STR "Use IPv4 source for BFD session\n"
#define BFD_INTEGRATION_SOURCEV6_STR "Use IPv4 source for BFD session\n"

/*
 * 命令树对外主要接口原型。
 *
 * install_node / install_default：注册一个配置节点、并为其安装默认命令。
 * install_element / _install_element：把一条命令挂到指定节点，是 DEFUN 生效的入口。
 * cmd_execute / cmd_execute_command / cmd_execute_command_strict：解析并执行整行命令。
 * cmd_describe_command / cmd_complete_command：给 “?” 与 TAB 补全使用。
 * config_from_file / command_config_read_one_line：从配置文件逐行回放命令。
 * cmd_make_strvec / cmd_free_strvec：把一行字符串切成 token 向量及释放。
 */
/* Prototypes. */
extern void install_node(struct cmd_node *node);
extern void install_default(enum node_type node);

/*
 * struct xref_install_element —— install_element 宏生成的静态交叉引用记录。
 * 把命令元素与节点类型登记到 xref 表，便于开发/调试构建下回溯来源，
 * 以及 XREF_LINK 生成带源码位置信息的引用，正常运行时不影响命令逻辑。
 */
struct xref_install_element {
	struct xref xref;

	const struct cmd_element *cmd_element;
	enum node_type node_type;
};

/*
 * install_element —— 安装命令的推荐宏。它在编译期构造一份 static 交叉引用
 * 记录（xref_install_element），再调用 _install_element 真正把命令挂到节点。
 * 之所以包一层，是为了给每条命令留下可追溯的源码位置。
 */
#define install_element(node_type_, cmd_element_) do {                         \
		static const struct xref_install_element _xref                 \
				__attribute__((used)) = {                      \
			.xref = XREF_INIT(XREFT_INSTALL_ELEMENT, NULL,         \
					  __func__),                           \
			.cmd_element = cmd_element_,                           \
			.node_type = node_type_,                               \
		};                                                             \
		XREF_LINK(_xref.xref);                                         \
		_install_element(node_type_, cmd_element_);                    \
	} while (0)

extern void _install_element(enum node_type ntype, const struct cmd_element *cmd);

/* known issue with uninstall_element:  changes to cmd_token->attr (i.e.
 * deprecated/hidden) are not reversed. */
extern void uninstall_element(enum node_type ntype, const struct cmd_element *cmd);

/* construct CLI tree only when entering nodes */
extern void cmd_defer_tree(bool val);

/* finish CLI tree for node when above is true (noop otherwise) */
extern void cmd_finalize_node(struct cmd_node *node);

/* Concatenates argv[shift] through argv[argc-1] into a single NUL-terminated
   string with a space between each element (allocated using
   XMALLOC(MTYPE_TMP)).  Returns NULL if shift >= argc. */
extern char *argv_concat(struct cmd_token **argv, int argc, int shift);

/*
 * It is preferred that you set the index initial value
 * to a 0.  This way in the future if you modify the
 * cli then there is no need to modify the initial
 * value of the index
 */
extern int argv_find(struct cmd_token **argv, int argc, const char *text,
		     int *index);

extern vector cmd_make_strvec(const char *string);
extern void cmd_free_strvec(vector);
extern vector cmd_describe_command(vector vline, struct vty *vty, int *status);
extern char **cmd_complete_command(vector vline, struct vty *vty, int *status);
extern const char *cmd_prompt(enum node_type);
extern int command_config_read_one_line(struct vty *vty,
					const struct cmd_element **cmd,
					uint32_t line_num, int use_config_node);
extern int config_from_file(struct vty *vty, FILE *fp, unsigned int *line_num);
extern enum node_type node_parent(enum node_type node);
/*
 * Execute command under the given vty context.
 *
 * vty
 *    The vty context to execute under.
 *
 * cmd
 *    The command string to execute.
 *
 * matched
 *    If non-null and a match was found, the address of the matched command is
 *    stored here. No action otherwise.
 *
 * vtysh
 *    Whether or not this is being called from vtysh. If this is nonzero,
 *    XXX: then what?
 *
 * Returns:
 *    XXX: what does it return
 */
extern int cmd_execute(struct vty *vty, const char *cmd,
		       const struct cmd_element **matched, int vtysh);
extern int cmd_execute_command(vector vline, struct vty *vty, const struct cmd_element **cmd,
			       int vtysh);
extern int cmd_execute_command_strict(vector vline, struct vty *vty,
				      const struct cmd_element **cmd);
extern void cmd_init(int terminal);
extern void cmd_init_config_callbacks(void (*start_config_cb)(struct vty *vty),
				      void (*end_config_cb)(struct vty *vty));
extern void cmd_terminate(void);
extern void cmd_exit(struct vty *vty);
extern int cmd_list_cmds(struct vty *vty, int do_permute);
extern int cmd_find_cmds(struct vty *vty, struct cmd_token **argv, int argc);

extern int cmd_domainname_set(const char *domainname);
extern int cmd_hostname_set(const char *hostname);
extern const char *cmd_hostname_get(void);
extern const char *cmd_domainname_get(void);
extern const char *cmd_system_get(void);
extern const char *cmd_release_get(void);
extern const char *cmd_version_get(void);
extern const char *cmd_software_version_get(void);
extern bool cmd_allow_reserved_ranges_get(void);

/* NOT safe for general use; call this only if DEV_BUILD! */
extern void grammar_sandbox_init(void);

extern vector completions_to_vec(struct list *completions);

/* Export typical functions. */
extern const char *host_config_get(void);
extern void host_config_set(const char *filename);

extern void print_version(const char *progname);

extern int cmd_banner_motd_file(const char *file);
extern void cmd_banner_motd_line(const char *line);

/*
 * struct cmd_variable_handler —— 变量型 token（如 $ifname / $vrf）的补全处理器注册项。
 * tokenname 是命令串中的变量名；completions 产出候选值填充 vector out；
 * xpath 可选，表示从某个 YANG xpath 的取值集合来生成补全候选。
 */
struct cmd_variable_handler {
	const char *tokenname, *varname;
	const char *xpath;	/* fill comps from set of values at xpath */
	void (*completions)(vector out, struct cmd_token *token);
};

extern void cmd_variable_complete(struct cmd_token *token, const char *arg,
				  vector comps);
extern void
cmd_variable_handler_register(const struct cmd_variable_handler *cvh);
extern char *cmd_variable_comp2str(vector comps, unsigned short cols);

extern void command_setup_early_logging(const char *dest, const char *level);

/*
 * Allow a mechanism for `debug XXX` commands that live
 * under the lib directory to output their debug status
 */
extern void cmd_show_lib_debugs(struct vty *vty);

extern const struct frr_yang_module_info frr_host_cli_info;
extern void host_cli_init(void);

extern void log_cli_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_COMMAND_H */
