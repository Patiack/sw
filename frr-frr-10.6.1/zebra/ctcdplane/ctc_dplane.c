#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netinet/in.h>
#include <arpa/inet.h>

#include "lib/libfrr.h"
#include "lib/zebra.h"
#include "lib/prefix.h"
#include "lib/nexthop.h"
#include "lib/nexthop_group.h"
#include "lib/log.h"
#include "lib_errors.h"
#include "zebra/zebra_dplane.h"
#include "zebra/zebra_nhg.h"
#include "zebra/zebra_router.h"
#include "zebra/debug.h"
#include "lib/pw.h"
#include "runoslib.h"
//#include "zebra/ctc/ctc_hal.h"
#include "zebra/ctc/ctc_hal.h"
#include "../../../../hal/hal_msg.h"

enum {
	/* Route commands */
	HAL_MSG_RT_IPV4_ADD = 1,
	HAL_MSG_RT_IPV4_DEL = 2,
	HAL_MSG_RT_IPV6_ADD = 3,
	HAL_MSG_RT_IPV6_DEL = 4,
	/* MPLS/LSP 命令 */
	HAL_MSG_MPLS_LSP_ADD    = 10,
	HAL_MSG_MPLS_LSP_DEL    = 11,
	HAL_MSG_MPLS_PW_ADD     = 12,
	HAL_MSG_MPLS_PW_DEL     = 13,
	HAL_MSG_MPLS_STATS_ADD  = 14,
	HAL_MSG_MPLS_STATS_DEL  = 15,
	HAL_MSG_MPLS_LSP_UPDATE = 16,

	HAL_MSG_RT_VRF_ADD      = 17,   /* VRF route add/update  (HAL_MODULE_ROUTE) */
	HAL_MSG_RT_VRF_DEL      = 18,   /* VRF route delete      (HAL_MODULE_ROUTE) */

	/* PIM commands (HAL_MODULE_PIM, no conflict with MPLS NH IDs) */
	HAL_MSG_PIM_RP_ADD = 20,
	HAL_MSG_PIM_RP_DEL = 21,
	HAL_MSG_PIM_SSM_ADD = 22,
	HAL_MSG_PIM_SSM_DEL = 23,
	/* NH MPLS 命令（必须与 def_mpls.h 保持同步） */
	HAL_MSG_NH_MPLS_UPDATE = 30,

	/* VRF/Interface binding HAL command (new, free slot 34) */
	HAL_MSG_INTF_VRF_UPDATE = 34,   /* Interface VRF membership update (HAL_MODULE_ROUTE) */

    HAL_MSG_MPLS_LSP_VRF_ADD = 40,
    HAL_MSG_MPLS_LSP_VRF_DEL = 41,
};

/* MPLS LSP 动作 flag（必须与 def_mpls.h 的 HAL_MPLS_FLAG_* 一致） */
#define HAL_MPLS_FLAG_POP    0x01   /* 纯 POP（无出标签） */
#define HAL_MPLS_FLAG_SWAP   0x02   /* SWAP：in_label -> out_label */
#define HAL_MPLS_FLAG_PUSH   0x04   /* PUSH：路由的入向压标签 */
#define HAL_MPLS_FLAG_DECAP  0x08   /* 已废弃：本平台的 ilm->decap 等价"把
				     * 下层标签当 IP 头"，L3VPN/PHP 都是错的；
				     * 保留位定义与 def_mpls.h 对齐，不再发送 */
#define HAL_MPLS_FLAG_TTL_PIPE 0x10 /* POP TTL pipe 模式（RFC 3443） */

/* VPWS PW flag 位定义（必须与 def_mpls.h 的 HAL_MPLS_PW_FLAG_* 一致） */
#define HAL_MPLS_PW_FLAG_CWORD  0x01 /* 控制字使能（F_PSEUDOWIRE_CWORD） */
#define HAL_MPLS_PW_FLAG_STATIC 0x02 /* 静态 PW（保留） */

/* HSL 返回码：因下一跳 ARP 尚未解析，ILM 以 ARP 占位模式安装
 * （绑定到保留 TOCPU nexthop）。内核 netlink 更新同时被跳过
 * （内核不支持 MPLS）；ARP 学习后 HSL 升级表项。
 * （必须与 def_mpls.h 的 CTC_HAL_RET_MPLS_PLACEHOLDER 一致） */
#define CTC_HAL_RET_MPLS_PLACEHOLDER 1

#define CTC_HAL_MSG_MAX_NH 16

/* HAL 消息的协议类型映射 */
#define CTC_PROTO_RIP     1
#define CTC_PROTO_OSPF    2
#define CTC_PROTO_ISIS    3
#define CTC_PROTO_BGP     4
#define CTC_PROTO_STATIC  5
#define CTC_PROTO_CONNECT 6
#define CTC_PROTO_KERNEL  7
#define CTC_PROTO_LDP     8
#define CTC_PROTO_PIM     9
#define CTC_PROTO_VRRP    10
#define CTC_PROTO_OTHER   255

/* ------------------------------------------------------------------
 * HAL wire format for the VRF LSP (ILM) message.
 * Must stay layout-identical with zebra/ctcdplane/ctc_dplane.h.
 * ------------------------------------------------------------------ */
#define CTC_HAL_MSG_MAX_LSP_NH_VRF  4

struct ctc_hal_msg_mpls_lsp_nh_vrf {
	uint32_t ifindex;      /* output interface index */
	struct in_addr gate;   /* nexthop IPv4 (network byte order) */
	uint32_t out_label;    /* label to push/swap to */
	uint8_t  is_mpls;      /* 1: MPLS nexthop */
	uint8_t  pad[3];
};

_Static_assert(sizeof(struct ctc_hal_msg_mpls_lsp_nh_vrf) == 16,
	       "VRF LSP nexthop size mismatch");

struct ctc_hal_msg_mpls_lsp_vrf {
	uint32_t vrf_id;            /* VRF identifier */
	uint32_t in_label;          /* incoming label (ILM lookup key) */
	uint32_t out_label;         /* outgoing label after swap */
	uint8_t  op_type;           /* HSL_MPLS_OP_* (PUSH/POP/SWAP) */
	uint8_t  nh_count;
	uint8_t  pad[2];
	struct ctc_hal_msg_mpls_lsp_nh_vrf nh[CTC_HAL_MSG_MAX_LSP_NH_VRF];
};

_Static_assert(sizeof(struct ctc_hal_msg_mpls_lsp_vrf) == 80,
	       "VRF LSP message size mismatch");

/**
 * VRF nexthop structure (wire format, 28 bytes, dual-stack).
 *
 * gate carries the nexthop IP address in network byte order; gate_af
 * selects the union member (AF_INET -> gate.ipv4, AF_INET6 -> gate.ipv6,
 * 0 -> gate unused, e.g. pure-IFINDEX nexthop). gate_af is per-nexthop on
 * purpose: a v6-prefix route may egress through a v4 gateway (v6 payload
 * over an IPv4 core) or vice versa, so the gateway family is not implied
 * by the route family carried in msg.af.
 */
struct ctc_hal_msg_nexthop_vrf {
	uint32_t ifindex;      /* output interface index (HSL-mapped) */
	union {
		uint32_t ipv4;   /* AF_INET  nexthop (network byte order) */
		uint8_t  ipv6[16]; /* AF_INET6 nexthop (network byte order bytes) */
	} gate;
	uint8_t  type;         /* nexthop type (FRR NEXTHOP_TYPE_*) */
	uint8_t  flags;        /* nexthop flags (FRR NEXTHOP_FLAG_*) */
	uint8_t  is_mpls;      /* 1: push out_label on egress */
	uint8_t  gate_af;      /* AF_INET / AF_INET6: selects gate union */
	uint32_t out_label;    /* outgoing MPLS label (network byte order) */
};

_Static_assert(sizeof(struct ctc_hal_msg_nexthop_vrf) == 28,
	       "VRF nexthop structure size mismatch");

/* Maximum nexthop count per VRF route message (288-byte message with 8 nh) */
#ifndef CTC_HAL_MSG_MAX_NH_VRF
#define CTC_HAL_MSG_MAX_NH_VRF         8
#endif

/**
 * VRF-aware route entry with MPLS label stack support (dual-stack).
 *
 * Layout must stay byte-identical with hsl/hsl_rt_vrf.h's
 * struct ctc_hal_msg_route_vrf -- the HSL receiver decodes this buffer
 * directly (288 bytes with 8 nexthops):
 *   4 vrf_id + 1 af + 1 prefixlen + 2 pad + 16 prefix + 16 src_prefix +
 *   1 proto + 1 distance + 2 metric + 1 label_depth + 3 pad +
 *   12 label_stack + 1 nh_count + 3 pad + 8*28 nexthops = 288
 */
struct ctc_hal_msg_route_vrf {
	uint32_t vrf_id;            /* VRF identifier */
	uint8_t  af;                /* AF_INET / AF_INET6 */
	uint8_t  prefixlen;         /* prefix length */
	uint8_t  pad1[2];
	union {
		uint32_t ipv4;       /* AF_INET  prefix (network byte order) */
		uint8_t  ipv6[16];   /* AF_INET6 prefix (network byte order bytes) */
	} prefix;                  /* destination prefix, chosen by af */
	union {
		uint32_t ipv4;       /* AF_INET  source prefix (network byte order) */
		uint8_t  ipv6[16];   /* AF_INET6 source prefix (network byte order bytes) */
	} src_prefix;              /* source prefix, all-zero if none */
	uint8_t  proto_type;        /* CTC_PROTO_* */
	uint8_t  distance;          /* administrative distance */
	uint16_t metric;            /* route metric */

	/* MPLS label stack for L3VPN routes: [transport, vpn, ...] */
	uint8_t  label_stack_depth; /* 0-3 */
	uint8_t  pad2[3];
	uint32_t label_stack[3];    /* network byte order */

	uint8_t  nh_count;          /* number of valid entries in nh[] */
	uint8_t  pad3[3];
	struct ctc_hal_msg_nexthop_vrf nh[CTC_HAL_MSG_MAX_NH_VRF];
};

_Static_assert(sizeof(struct ctc_hal_msg_route_vrf) == 288,
	       "ctc_hal_msg_route_vrf must be 288 bytes to match hsl_rt_vrf.h");

/**
 * Interface VRF binding message (wire format, 12 bytes).
 * Sent to HSL when an interface is bound to/unbound from a VRF.
 */
struct ctc_hal_msg_intf_vrf {
	uint32_t ifindex;      /* HSL-mapped interface index */
	uint32_t vrf_id;       /* VRF table ID (0 for default VRF) */
	uint8_t  is_vrf_dev;   /* 1: this is a VRF device itself (CUST1) */
	uint8_t  op_type;      /* 0: add/update, 1: delete */
	uint8_t  pad[2];
};

_Static_assert(sizeof(struct ctc_hal_msg_intf_vrf) == 12,
	       "Interface VRF message size mismatch");

/* IPv4 route entry */
struct ctc_hal_msg_rt_ipv4 {
	struct in_addr dest;
	uint8_t prefixlen;
	uint8_t proto_type; /* CTC_PROTO_* */
	uint8_t pad[2];
	struct in_addr nexthop;
	ifindex_t ifindex;
	uint32_t metric;
	uint32_t table_id;
	uint8_t distance;
	uint8_t pad2[3];
};

/* IPv6 route entry */
struct ctc_hal_msg_rt_ipv6 {
	struct in6_addr dest;
	uint8_t prefixlen;
	uint8_t proto_type; /* CTC_PROTO_* */
	uint8_t pad[2];
	struct in6_addr nexthop;
	ifindex_t ifindex;
	uint32_t metric;
	uint32_t table_id;
	uint8_t distance;
	uint8_t pad2[3];
};

/* 单条 LSP 消息可携带的最大路径（NHLFE）数
 * （必须与 def_mpls.h 的 CTC_HAL_MSG_MAX_LSP_NH 一致） */
#define CTC_HAL_MSG_MAX_LSP_NH 4

/* MPLS LSP 表项的一条路径（必须与 def_mpls.h 一致） */
struct ctc_hal_msg_mpls_lsp_nh {
	uint32_t out_label;
	uint32_t flags;       /* HAL_MPLS_FLAG_* */
	struct in_addr nexthop;
	ifindex_t ifindex;
};

/* MPLS LSP 表项（必须与 def_mpls.h 的 struct ctc_hal_msg_mpls_lsp 一致） */
struct ctc_hal_msg_mpls_lsp {
	uint32_t in_label;
	uint8_t  nh_count;    /* nh[] 中有效表项数 */
	uint8_t  proto_type;  /* CTC_PROTO_* */
	uint8_t  pad[2];
	struct ctc_hal_msg_mpls_lsp_nh nh[CTC_HAL_MSG_MAX_LSP_NH];
};

/* 与 hal/def_mpls.h 保持线格式一致（72 字节）：不匹配会使 HSL 侧
 * 的 nexthop/ifindex 错位，破坏所有 ILM。
 */
_Static_assert(sizeof(struct ctc_hal_msg_mpls_lsp) == 72,
	       "ctc_hal_msg_mpls_lsp must be 72 bytes to match def_mpls.h");

/* MPLS 统计表项（必须与 def_mpls.h 的 struct ctc_hal_msg_mpls_stats 一致） */
struct ctc_hal_msg_mpls_stats {
	uint32_t stats_id; /* 0：由 HSL 分配流统计 id */
	uint32_t label;
	uint8_t  spaceid;
	uint8_t  pad[3];
};

_Static_assert(sizeof(struct ctc_hal_msg_mpls_stats) == 12,
	       "ctc_hal_msg_mpls_stats must be 12 bytes to match def_mpls.h");

/* VPWS 伪线 (PW) 条目（必须与 def_mpls.h 的 struct ctc_hal_msg_mpls_pw 一致）
 *
 * 参考 SAI 方式的 VPWS 语义：
 *   - 入方向 (PW->AC)：VC 入标签查 ILM（CTC_MPLS_LABEL_TYPE_VPWS），
 *     剥除 VC 标签/控制字后按 AC 出口 nexthop 做二层转发；
 *   - 出方向 (AC->PW)：AC 口 vlan mapping 命中后走 VPWS push nexthop
 *     （CTC_MPLS_NH_PUSH_OP_L2VPN），依次压入 VC 出标签与隧道标签。
 *
 * zebra PW dplane ctx 不携带 AC 接口信息（ldpd pw2zpw 不传 member
 * interface），因此用 PW 接口 ifindex 换算后作为 AC 关联口传递，
 * 由 HSL 侧解析为真实 AC gport。 */

/* PSN 隧道路径（必须与 def_mpls.h 的 struct ctc_hal_msg_mpls_pw_nh 一致） */
struct ctc_hal_msg_mpls_pw_nh {
	uint32_t tunnel_label;    /* 隧道标签；0 表示 PHP，只压 VC 标签 */
	struct in_addr nexthop;   /* PSN 对端 IP */
	ifindex_t ifindex;        /* PSN 出接口（HSL ifindex） */
};

/* VPWS PW 条目（必须与 def_mpls.h 的 struct ctc_hal_msg_mpls_pw 一致） */
struct ctc_hal_msg_mpls_pw {
	uint32_t in_label;    /* VC 入标签（PW->AC 方向查 ILM） */
	uint32_t out_label;   /* VC 出标签（AC->PW 方向压标签） */
	uint32_t flags;       /* HAL_MPLS_PW_FLAG_* */
	uint32_t pw_type;     /* PW_TYPE_*（zebra 取值直接透传） */
	uint32_t ac_ifindex;  /* AC 关联口（HSL ifindex） */
	uint8_t  proto_type;  /* CTC_PROTO_* */
	uint8_t  nh_count;    /* PSN 路径条数 */
	uint8_t  pad[2];
	struct ctc_hal_msg_mpls_pw_nh nh[CTC_HAL_MSG_MAX_LSP_NH];
};

/* 与 hal/def_mpls.h 保持线格式一致（72 字节）：不匹配会错位解析 */
_Static_assert(sizeof(struct ctc_hal_msg_mpls_pw) == 72,
	       "ctc_hal_msg_mpls_pw must be 72 bytes to match def_mpls.h");

/* PIM entry */
struct ctc_hal_msg_pim {
	struct in_addr group;
	struct in_addr source;
	struct in_addr rp;
	ifindex_t ifindex;
	uint8_t proto_type;
	uint8_t pad[3];
};

typedef struct runos_ipv6_s {
	unsigned int word[4];
} runos_ipv6_t;

typedef struct hal_msg_nh_s {
	unsigned int ifindex;
	union {
		unsigned int ip;
		runos_ipv6_t ip6;
	} u;
	unsigned int is_mpls;
	unsigned int mpls_label;
} hal_msg_nh_t;

typedef struct hal_msg_rt_s {
	uint8_t family;
	uint8_t prefixlen;
	uint8_t count;

	union {
		unsigned int ip;
		runos_ipv6_t ip6;
	} u;

	hal_msg_nh_t nh[CTC_HAL_MSG_MAX_NH];
} hal_msg_rt_t;

uint32_t ToHslIfindex(uint32_t ifindex)
{
	uint32_t vid = 0;
	struct interface *ifp = if_lookup_by_index(ifindex, VRF_DEFAULT);

	if (!ifp)
		return ifindex;

	if (strstr(ifp->name, "vlanif") == ifp->name) {
		const char *num_str = ifp->name + strlen("vlanif");
		char *endptr = NULL;
		unsigned long val = strtoul(num_str, &endptr, 10);

		/* 确保全部是数字，且VLAN合法范围 */
		if (*endptr == '\0' && val >= 1 && val <= 4094)
			vid = (uint32_t)val;
		else
			return ifindex;
	} else {
		return ifindex;
	}

	return (0x06000000 | vid); /* (PORT_TYPE_VLANIF<<UIF_TYPE_OFFSET | vid) */
}

static struct {
	int module;
	int cmd;
	size_t data_len;
	unsigned char data[512]; /* >= largest sent message (288B VRF route) */
} ctc_last_msg;

static inline int ctc_hal_cmd_send(int module, int cmd, void *data,
				   size_t data_len)
{
	int ret;
	size_t cmp_len;

	/* Skip consecutive duplicate messages */
	cmp_len = data_len < sizeof(ctc_last_msg.data) ?
		  data_len : sizeof(ctc_last_msg.data);
	if (module == ctc_last_msg.module && cmd == ctc_last_msg.cmd &&
	    data_len == ctc_last_msg.data_len &&
	    memcmp(data, ctc_last_msg.data, cmp_len) == 0) {
		return 0;
	}
	ret = hal_cmd_send(module, cmd, data, (int)data_len, NULL, NULL);

	/* Update the cache only on success: a failed or placeholder
	 * install must be retried when the next identical request
	 * arrives (e.g. ARP learning upgrading a placeholder ILM). */
	if (ret == 0) {
		ctc_last_msg.module = module;
		ctc_last_msg.cmd = cmd;
		ctc_last_msg.data_len = data_len;
		if (data_len > 0 && data_len <= sizeof(ctc_last_msg.data))
			memcpy(ctc_last_msg.data, data, data_len);
	}

	return ret;
}

/* Map FRR zebra route type to CTC HAL protocol type */
static uint8_t ctc_proto_from_zebra(int zebra_type)
{
	switch (zebra_type) {
	case ZEBRA_ROUTE_RIP:
	case ZEBRA_ROUTE_RIPNG:
		return CTC_PROTO_RIP;
	case ZEBRA_ROUTE_OSPF:
	case ZEBRA_ROUTE_OSPF6:
		return CTC_PROTO_OSPF;
	case ZEBRA_ROUTE_ISIS:
		return CTC_PROTO_ISIS;
	case ZEBRA_ROUTE_BGP:
		return CTC_PROTO_BGP;
	case ZEBRA_ROUTE_STATIC:
		return CTC_PROTO_STATIC;
	case ZEBRA_ROUTE_CONNECT:
		return CTC_PROTO_CONNECT;
	case ZEBRA_ROUTE_KERNEL:
	case ZEBRA_ROUTE_SYSTEM:
		return CTC_PROTO_KERNEL;
	case ZEBRA_ROUTE_LDP:
		return CTC_PROTO_LDP;
	case ZEBRA_ROUTE_PIM:
		return CTC_PROTO_PIM;
	case ZEBRA_ROUTE_VRRP:
		return CTC_PROTO_VRRP;
	default:
		return CTC_PROTO_OTHER;
	}
}

/* Convert a zebra dplane route op into the corresponding HAL command.
 * Returns 0 when the op is not a route operation we care about.
 */
static int ctc_route_op_to_hal_cmd(enum dplane_op_e op, afi_t afi,
				   int *hal_cmd)
{
	if (afi == AFI_IP) {
		switch (op) {
		case DPLANE_OP_ROUTE_INSTALL:
		case DPLANE_OP_ROUTE_UPDATE:
			*hal_cmd = HAL_MSG_RT_IPV4_ADD;
			return 1;
		case DPLANE_OP_ROUTE_DELETE:
			*hal_cmd = HAL_MSG_RT_IPV4_DEL;
			return 1;
		default:
			return 0;
		}
	}

	if (afi == AFI_IP6) {
		switch (op) {
		case DPLANE_OP_ROUTE_INSTALL:
		case DPLANE_OP_ROUTE_UPDATE:
			*hal_cmd = HAL_MSG_RT_IPV6_ADD;
			return 1;
		case DPLANE_OP_ROUTE_DELETE:
			*hal_cmd = HAL_MSG_RT_IPV6_DEL;
			return 1;
		default:
			return 0;
		}
	}

	return 0;
}

/* ----------------------------------------------------------------------
 * Route handling (RIP, OSPF, IS-IS, BGP, Static, Connected, etc.)
 * ---------------------------------------------------------------------- */

static void ctc_dplane_send_route_ipv4(struct zebra_dplane_ctx *ctx,
				       const struct prefix *dest_pfx,
				       const struct nexthop_group *ng,
				       int hal_cmd)
{
	struct ctc_hal_msg_rt_ipv4 msg = {};
	hal_msg_rt_t msg1 = {};
	struct nexthop *nh;
	int nh_count = 0;
	int route_type = dplane_ctx_get_type(ctx);

	msg.dest = dest_pfx->u.prefix4;
	msg.prefixlen = dest_pfx->prefixlen;
	msg.table_id = dplane_ctx_get_table(ctx);
	msg.metric = dplane_ctx_get_metric(ctx);
	msg.distance = dplane_ctx_get_distance(ctx);
	msg.proto_type = ctc_proto_from_zebra(route_type);
	msg1.prefixlen = dest_pfx->prefixlen;
	msg1.family = AF_INET;
	msg1.u.ip = dest_pfx->u.prefix4.s_addr;

	if (IS_ZEBRA_DEBUG_DPLANE)
		zlog_debug("CTC-DPLANE: route %s len=%d table=%u metric=%u distance=%u proto=%d",
			   inet_ntoa(msg.dest), msg.prefixlen,
			   msg.table_id, msg.metric, msg.distance,
			   msg.proto_type);

	for (ALL_NEXTHOPS_PTR(ng, nh)) {
		if (CHECK_FLAG(nh->flags, NEXTHOP_FLAG_RECURSIVE))
			continue;

		if (!nh->ifindex)
			continue;

		msg.ifindex = ToHslIfindex(nh->ifindex);
		msg1.nh[nh_count].ifindex = ToHslIfindex(nh->ifindex);

		if (nh->type == NEXTHOP_TYPE_IPV4 ||
		    nh->type == NEXTHOP_TYPE_IPV4_IFINDEX) {
			msg.nexthop = nh->gate.ipv4;
			msg1.nh[nh_count].u.ip = nh->gate.ipv4.s_addr;
		} else {
			msg.nexthop.s_addr = INADDR_ANY;
			msg1.nh[nh_count].u.ip = INADDR_ANY;
		}

		/* FIX: 处理 MPLS 标签（Ingress Push）。
		 * implicit-null (label 3) 不能压到报文上，必须过滤。 */
		if (nh->nh_label && nh->nh_label->num_labels > 0 &&
		    nh->nh_label->label[0] != MPLS_LABEL_IMPLICIT_NULL) {
			msg1.nh[nh_count].mpls_label =
				nh->nh_label->label[0];
			msg1.nh[nh_count].is_mpls = 1;
		}

		if (IS_ZEBRA_DEBUG_DPLANE)
			zlog_debug("CTC-DPLANE:   nh#%d ifindex=%d(%d) nexthop=%s label=%u is_mpls=%u",
				   nh_count, msg.ifindex,
				   ToHslIfindex(msg.ifindex),
				   inet_ntoa(msg.nexthop),
				   msg1.nh[nh_count].mpls_label,
				   msg1.nh[nh_count].is_mpls);

		nh_count++;
		if (nh_count >= CTC_HAL_MSG_MAX_NH)
			break;
	}
	msg1.count = nh_count;

	if (ctc_hal_cmd_send(HAL_MODULE_ROUTE, hal_cmd, &msg1,
			     sizeof(msg1)) < 0) {
		zlog_warn("CTC-DPLANE: ctc_hal_cmd_send (no nh) failed");
	}
}

static void ctc_dplane_send_route_ipv6(struct zebra_dplane_ctx *ctx,
				       const struct prefix *dest_pfx,
				       const struct nexthop_group *ng,
				       int hal_cmd)
{
	struct ctc_hal_msg_rt_ipv6 msg = {};
	hal_msg_rt_t msg1 = {};
	struct nexthop *nh;
	int nh_count = 0;
	char buf[INET6_ADDRSTRLEN];
	int route_type = dplane_ctx_get_type(ctx);

	msg.dest = dest_pfx->u.prefix6;
	msg.prefixlen = dest_pfx->prefixlen;
	msg.table_id = dplane_ctx_get_table(ctx);
	msg.metric = dplane_ctx_get_metric(ctx);
	msg.distance = dplane_ctx_get_distance(ctx);
	msg.proto_type = ctc_proto_from_zebra(route_type);

	msg1.prefixlen = dest_pfx->prefixlen;
	msg1.family = AF_INET6;
	memcpy(&msg1.u.ip6, &dest_pfx->u.prefix6, sizeof(msg1.u.ip6));

	if (IS_ZEBRA_DEBUG_DPLANE)
		zlog_debug("CTC-DPLANE: routev6 %s/%d table=%u metric=%u distance=%u proto=%d",
			   inet_ntop(AF_INET6, &msg.dest, buf, sizeof(buf)),
			   msg.prefixlen, msg.table_id, msg.metric,
			   msg.distance, msg.proto_type);

	for (ALL_NEXTHOPS_PTR(ng, nh)) {
		if (CHECK_FLAG(nh->flags, NEXTHOP_FLAG_RECURSIVE))
			continue;

		if (!nh->ifindex)
			continue;

		msg.ifindex = ToHslIfindex(nh->ifindex);
		msg1.nh[nh_count].ifindex = ToHslIfindex(nh->ifindex);

		if (nh->type == NEXTHOP_TYPE_IPV6 ||
		    nh->type == NEXTHOP_TYPE_IPV6_IFINDEX) {
			msg.nexthop = nh->gate.ipv6;
			memcpy(&msg1.nh[nh_count].u.ip6,
			       &(nh->gate.ipv6), sizeof(nh->gate.ipv6));
		} else {
			memset(&msg.nexthop, 0, sizeof(msg.nexthop));
			memset(&msg1.nh[nh_count].u.ip6, 0,
			       sizeof(nh->gate.ipv6));
		}

		if (IS_ZEBRA_DEBUG_DPLANE)
			zlog_debug("CTC-DPLANE:   nh#%d ifindex=%d",
				   nh_count, msg.ifindex);

		/* 处理 IPv6 的 MPLS 标签（入向压标签）。
		 * implicit-null（label 3）不能压到报文上，必须过滤。 */
		if (nh->nh_label && nh->nh_label->num_labels > 0 &&
		    nh->nh_label->label[0] != MPLS_LABEL_IMPLICIT_NULL) {
			msg1.nh[nh_count].mpls_label =
				nh->nh_label->label[0];
			msg1.nh[nh_count].is_mpls = 1;
		}

		nh_count++;
		if (nh_count >= CTC_HAL_MSG_MAX_NH)
			break;
	}
	msg1.count = nh_count;

	if (ctc_hal_cmd_send(HAL_MODULE_ROUTE, hal_cmd, &msg1,
			     sizeof(msg1)) < 0) {
		zlog_warn("CTC-DPLANE: ctc_hal_cmd_send (no nh) failed");
	}
}

static void ctc_dplane_process_route(struct zebra_dplane_ctx *ctx)
{
	const struct prefix *dest_pfx = dplane_ctx_get_dest(ctx);
	enum dplane_op_e op = dplane_ctx_get_op(ctx);
	afi_t afi = dplane_ctx_get_afi(ctx);
	const struct nexthop_group *ng = dplane_ctx_get_ng(ctx);
	const struct nexthop *nh;
	bool has_real_label = false;
	int hal_cmd;

	if (!dest_pfx || !ng) {
		if (IS_ZEBRA_DEBUG_DPLANE)
			zlog_debug("CTC-DPLANE: skipping route, missing dest or ng");
		return;
	}

	/* 带真实出标签的 FEC（MPLS 压标签路由）由 CTC 硬件承载，
	 * 不需要进内核：这台平台的内核 MPLS FIB 未启用
	 * （net.mpls.platform_labels=0），netlink 安装必然返回
	 * EOPNOTSUPP，zebra 会把路由标记 ROUTE_ENTRY_FAILED 并周期
	 * 重试（每 ~60s 一轮全量重装刷屏）。与 LSP 一样跳过内核
	 * 更新。implicit-null（label 3）在 send_route 里被过滤成
	 * 普通路由，netlink 安装成功，不在此列。 */
	for (ALL_NEXTHOPS_PTR(ng, nh)) {
		if (nh->nh_label && nh->nh_label->num_labels > 0 &&
		    nh->nh_label->label[0] != MPLS_LABEL_IMPLICIT_NULL) {
			has_real_label = true;
			break;
		}
	}
	if (has_real_label)
		dplane_ctx_set_skip_kernel(ctx);

	if (!ctc_route_op_to_hal_cmd(op, afi, &hal_cmd))
		return;

	/* For route updates, delete the existing entry first to avoid
	 * duplicate entries in hardware tables. */
	if (op == DPLANE_OP_ROUTE_UPDATE) {
		int del_cmd = (afi == AFI_IP) ?
			HAL_MSG_RT_IPV4_DEL : HAL_MSG_RT_IPV6_DEL;

		if (afi == AFI_IP)
			ctc_dplane_send_route_ipv4(ctx, dest_pfx, ng,
						   del_cmd);
		else if (afi == AFI_IP6)
			ctc_dplane_send_route_ipv6(ctx, dest_pfx, ng,
						   del_cmd);
	}

	if (afi == AFI_IP)
		ctc_dplane_send_route_ipv4(ctx, dest_pfx, ng, hal_cmd);
	else if (afi == AFI_IP6)
		ctc_dplane_send_route_ipv6(ctx, dest_pfx, ng, hal_cmd);
}

/* ----------------------------------------------------------------------
 * LSP（MPLS/LDP）处理
 * ---------------------------------------------------------------------- */

/* 把 zebra LSP 类型映射到 CTC HAL 协议类型 */
static uint8_t ctc_lsp_proto_from_zebra(enum lsp_types_t type)
{
	switch (type) {
	case ZEBRA_LSP_STATIC:
		return CTC_PROTO_STATIC;
	case ZEBRA_LSP_LDP:
		return CTC_PROTO_LDP;
	case ZEBRA_LSP_BGP:
		return CTC_PROTO_BGP;
	case ZEBRA_LSP_OSPF_SR:
		return CTC_PROTO_OSPF;
	case ZEBRA_LSP_ISIS_SR:
		return CTC_PROTO_ISIS;
	case ZEBRA_LSP_EVPN:
	case ZEBRA_LSP_SRTE:
	case ZEBRA_LSP_SHARP:
	default:
		return CTC_PROTO_OTHER;
	}
}

/* 让芯片侧 MPLS 流统计与 LSP 生命周期保持同步：安装/更新时
 * 分配并挂上（stats_id=0 告诉 HSL 分配或复用），删除时释放。
 * HSL 持有流统计 id，直到 HAL_MSG_MPLS_STATS_DEL 到达。 */
static void ctc_dplane_mpls_stats_send(int hal_cmd, uint32_t in_label)
{
	struct ctc_hal_msg_mpls_stats msg = {};

	msg.stats_id = 0;
	msg.label = in_label;
	msg.spaceid = 0;

	if (ctc_hal_cmd_send(HAL_MODULE_MPLS, hal_cmd, &msg,
			     sizeof(msg)) != 0)
		zlog_warn("CTC-DPLANE: MPLS stats cmd=%d label=%u send failed "
			  "(chip counters will be stale)",
			  hal_cmd, in_label);
}

/* VRF 出口标签 LSP 的 in_label -> vrf_id 镜像。
 *
 * zebra 拆除 VRF / VRF 设备口时，LSP DELETE 的 dplane ctx 只携带 nexthop 的
 * ifindex 数字（dplane_ctx_lsp_init 按值复制，不带 vrf_id）；若该接口已随 VRF
 * 拆除而不在 zebra 接口表中，ctc_dplane_lsp_is_vrf() 的 if_lookup_by_index
 * 会返回 NULL，无法再解析出 vrf_id。因此 INSTALL/UPDATE 时按 in_label 记录
 * 归属，DELETE 兜底据此仍发对 HAL_MSG_MPLS_LSP_VRF_DEL（键是 (vrf_id,
 * in_label)，错发普通 LSP DEL 会遗留 stale 的 L3VPN ILM）。
 *
 * 并发：dplane provider 单线程消费 ctx（与下方 ctc_last_msg 去重缓存同一
 * 上下文），读写都发生在本线程，无需加锁。 */
#define CTC_MPLS_VRF_LSP_MIRROR_MAX 256
static struct {
	mpls_label_t in_label;
	uint32_t vrf_id;
	uint8_t used;
} ctc_dplane_vrf_lsp[CTC_MPLS_VRF_LSP_MIRROR_MAX];

static void ctc_dplane_vrf_lsp_record(uint32_t vrf_id, mpls_label_t in_label)
{
	int i;
	int free_slot = -1;

	for (i = 0; i < CTC_MPLS_VRF_LSP_MIRROR_MAX; i++) {
		if (ctc_dplane_vrf_lsp[i].used &&
		    ctc_dplane_vrf_lsp[i].in_label == in_label) {
			ctc_dplane_vrf_lsp[i].vrf_id = vrf_id; /* 幂等更新 */
			return;
		}
		if (!ctc_dplane_vrf_lsp[i].used && free_slot < 0)
			free_slot = i;
	}
	if (free_slot < 0)
		return;
	ctc_dplane_vrf_lsp[free_slot].in_label = in_label;
	ctc_dplane_vrf_lsp[free_slot].vrf_id = vrf_id;
	ctc_dplane_vrf_lsp[free_slot].used = 1;
}

static int ctc_dplane_vrf_lsp_lookup(mpls_label_t in_label, uint32_t *vrf_id)
{
	int i;

	for (i = 0; i < CTC_MPLS_VRF_LSP_MIRROR_MAX; i++) {
		if (ctc_dplane_vrf_lsp[i].used &&
		    ctc_dplane_vrf_lsp[i].in_label == in_label) {
			if (vrf_id)
				*vrf_id = ctc_dplane_vrf_lsp[i].vrf_id;
			return 1;
		}
	}
	return 0;
}

static void ctc_dplane_vrf_lsp_clear(mpls_label_t in_label)
{
	int i;

	for (i = 0; i < CTC_MPLS_VRF_LSP_MIRROR_MAX; i++) {
		if (ctc_dplane_vrf_lsp[i].used &&
		    ctc_dplane_vrf_lsp[i].in_label == in_label) {
			ctc_dplane_vrf_lsp[i].used = 0;
			return;
		}
	}
}

/* ----------------------------------------------------------------------
 * VRF 出口标签（egress PE 的 L3VPN VC 标签）识别
 *
 * FRR 通过 ZEBRA_VRF_LABEL（zread_vrf_label）安装这类 LSP：LSP 落在默认
 * VRF 的 lsp_table、按 in_label 查，唯一 NHLFE 的 nexthop 指向所属 VRF 的
 * VRF 设备口（kind="vrf"，如 CUST1；NEXTHOP_TYPE_IFINDEX、无 gate、出标签
 * 隐式空）。LSP 的 dplane 上下文不带 vrf_id（dplane_ctx_lsp_init 只置
 * NS_DEFAULT），所以只能由 nexthop ifindex 解析出接口后判定 if_is_vrf() ——
 * 与 zebra 自身对这类 nexthop 的处理约定一致（zebra_mpls.c
 * nhlfe_nexthop_active: "this ifindex ... may be a VRF device, let's look in
 * all of them"）。
 * ------------------------------------------------------------------ */
static bool ctc_dplane_lsp_is_vrf(struct zebra_dplane_ctx *ctx,
				  uint32_t *vrf_id)
{
	const struct nhlfe_list_head *nh_list;
	struct zebra_nhlfe *nhlfe;
	struct interface *ifp;

	nh_list = dplane_ctx_get_nhlfe_list(ctx);
	if (!nh_list)
		return false;

	frr_each (nhlfe_list, nh_list, nhlfe) {
		if (!nhlfe->nexthop || nhlfe->nexthop->ifindex == 0)
			continue;
		ifp = if_lookup_by_index(nhlfe->nexthop->ifindex, VRF_DEFAULT);
		if (ifp && if_is_vrf(ifp) && ifp->vrf &&
		    ifp->vrf->vrf_id != VRF_DEFAULT) {
			if (vrf_id)
				*vrf_id = ifp->vrf->vrf_id;
			return true;
		}
	}
	return false;
}

/* 下发 VRF 出口标签 ILM（egress PE 的 VC 标签）。
 *
 * 语义 = "pop 该标签（BOS），然后按内层 IP 在 vrf_id 对应的 L3 路由表转发"
 * （bgp_mplsvpn.c 对每 VRF 出口标签的注释原文）。这只有 L3VPN 型 ILM 能
 * 表达（type=L3VPN + id_type=VRF + vrf_id，芯片写 innerPacketLookup），且
 * 必须走 HAL_MSG_MPLS_LSP_VRF_ADD/_DEL 让 HSL 进入 hw_mpls_lsp_add_vrf 的
 * L3VPN 构造；走普通 LSP 消息会被建成 NORMAL pop 并因 VRF 设备口无 HSL
 * nexthop 而永久停留在 TOCPU 占位。nh_id 恒为
 * CTC_NH_RESERVED_NHID_FOR_TOCPU(=2)（官方 sample 语义：内层查表出口由 VRF
 * 路由表决定，ILM 不携带真实出口）。 */
static void ctc_dplane_process_lsp_vrf(struct zebra_dplane_ctx *ctx,
				       mpls_label_t in_label, int op,
				       uint32_t vrf_id)
{
	struct ctc_hal_msg_mpls_lsp_vrf msg = {};
	const struct nhlfe_list_head *nh_list;
	struct zebra_nhlfe *nhlfe;
	int hal_cmd;
	int ret;

	switch (op) {
	case DPLANE_OP_LSP_INSTALL:
	case DPLANE_OP_LSP_UPDATE:
		hal_cmd = HAL_MSG_MPLS_LSP_VRF_ADD;
		break;
	case DPLANE_OP_LSP_DELETE:
		hal_cmd = HAL_MSG_MPLS_LSP_VRF_DEL;
		break;
	default:
		return;
	}

	/* DELETE：清除镜像归属（zebra 对 LSP 删除是一次性事件、不重发；无论本次
	 * HAL 下发成败都不再需要该记录。若该 in_label 日后被新的 VRF 出口标签
	 * 复用，record() 会重新写入同槽位。） */
	if (op == DPLANE_OP_LSP_DELETE)
		ctc_dplane_vrf_lsp_clear(in_label);

	msg.vrf_id = vrf_id;
	msg.in_label = in_label;
	/* per-VRF 出口标签 = implicit-null：pop 后内层按 vrf 查表 */
	msg.op_type = CTC_MPLS_OP_POP;
	msg.nh_count = 0;

	/* ADD/UPDATE：hsl_mpls_lsp_add_vrf 要求 nh_count>0。携带一条 VRF
	 * 设备口的空 gate 路径即可 —— hw_mpls_ilm_build_nh 对 gate=0 探测失败
	 * 后走占位，把 ILM 装成 nh_id=TOCPU 的 L3VPN 终态（正是我们想要的）。
	 * DELETE 只按 (vrf_id, in_label) 键下发（hw_mpls_lsp_del_vrf），无需 nh。 */
	if (op != DPLANE_OP_LSP_DELETE) {
		nh_list = dplane_ctx_get_nhlfe_list(ctx);
		if (nh_list) {
			frr_each (nhlfe_list, nh_list, nhlfe) {
				if (!nhlfe->nexthop)
					continue;
				msg.nh[msg.nh_count].ifindex =
					ToHslIfindex(nhlfe->nexthop->ifindex);
				msg.nh[msg.nh_count].gate =
					nhlfe->nexthop->gate.ipv4;
				msg.nh[msg.nh_count].out_label = 0;
				msg.nh[msg.nh_count].is_mpls = 0;
				msg.nh_count++;
				break; /* 一条足够 */
			}
		}
	}

	if (IS_ZEBRA_DEBUG_DPLANE)
		zlog_debug("CTC-DPLANE: MPLS VRF-LSP %s vrf=%u in_label=%u",
			   dplane_op2str(op), msg.vrf_id, msg.in_label);

	ret = ctc_hal_cmd_send(HAL_MODULE_MPLS, hal_cmd, &msg, sizeof(msg));
	if (ret == 0) {
		dplane_ctx_set_skip_kernel(ctx);
	} else if (ret == CTC_HAL_RET_MPLS_PLACEHOLDER) {
		/* L3VPN ILM 以 nh_id=TOCPU 安装。这是该表的终态（内层查表在
		 * VRF 路由表完成），不是 "等 ARP 升级"，故日志措辞不同。 */
		dplane_ctx_set_skip_kernel(ctx);
		if (IS_ZEBRA_DEBUG_DPLANE)
			zlog_debug("CTC-DPLANE: MPLS VRF-LSP %u installed "
				   "(L3VPN inner-IP lookup in vrf %u)",
				   in_label, vrf_id);
	} else {
		/* 失败不 skip kernel：zebra 保持未卸载状态以便重试。 */
		zlog_warn("CTC-DPLANE: MPLS VRF-LSP cmd=%d vrf=%u in_label=%u "
			  "send failed ret=%d, falling back to kernel",
			  hal_cmd, vrf_id, in_label);
	}
}

static void ctc_dplane_process_lsp(struct zebra_dplane_ctx *ctx)
{
	mpls_label_t in_label = dplane_ctx_get_in_label(ctx);
	int op = dplane_ctx_get_op(ctx);
	int hal_cmd;
	int ret = 0;
	struct ctc_hal_msg_mpls_lsp msg = {};
	const struct nhlfe_list_head *nh_list;
	int nh_count = 0;

	switch (op) {
	case DPLANE_OP_LSP_INSTALL:
		hal_cmd = HAL_MSG_MPLS_LSP_ADD;
		break;
	case DPLANE_OP_LSP_UPDATE:
		hal_cmd = HAL_MSG_MPLS_LSP_UPDATE;
		break;
	case DPLANE_OP_LSP_DELETE:
		hal_cmd = HAL_MSG_MPLS_LSP_DEL;
		break;
	default:
		return;
	}

	/* egress PE 的每-VRF 出口标签（NHLFE 下一跳是 VRF 设备口）：
	 * 改走 VRF LSP 消息，构造 L3VPN 型 ILM（见 ctc_dplane_process_lsp_vrf）。 */
	{
		uint32_t vrf_id;

		if (ctc_dplane_lsp_is_vrf(ctx, &vrf_id)) {
			/* INSTALL/UPDATE：记录 in_label -> vrf_id，供删除兜底
			 * （dplane ctx 不带 vrf_id，VRF 设备口可能先于 DELETE 拆除）。 */
			if (op != DPLANE_OP_LSP_DELETE)
				ctc_dplane_vrf_lsp_record(vrf_id, in_label);
			ctc_dplane_process_lsp_vrf(ctx, in_label, op, vrf_id);
			return;
		}
	}

	/* DELETE 兜底：若 VRF 设备口已不在 zebra 接口表（VRF 拆除先于本 DELETE
	 * 被消费），上面的 ifindex 解析失败；改查 INSTALL/UPDATE 时记录的镜像，
	 * 仍发对 HAL_MSG_MPLS_LSP_VRF_DEL（键为 (vrf_id, in_label)），避免遗留
	 * stale L3VPN ILM。 */
	if (op == DPLANE_OP_LSP_DELETE) {
		uint32_t vrf_id;

		if (ctc_dplane_vrf_lsp_lookup(in_label, &vrf_id)) {
			ctc_dplane_process_lsp_vrf(ctx, in_label, op, vrf_id);
			return;
		}
	}

	msg.in_label = in_label;

	/* 从 NHLFE 列表提取 nexthop 信息：LSP 的每条路径都下发，
	 * 多路径（ECMP）LSP 完整卸载，而不是只钉在第一条 NHLFE 上。 */
	nh_list = dplane_ctx_get_nhlfe_list(ctx);
	if (nh_list) {
		struct zebra_nhlfe *nhlfe;

		frr_each (nhlfe_list, nh_list, nhlfe) {
			if (!nhlfe->nexthop)
				continue;

			if (nh_count == 0)
				msg.proto_type =
					ctc_lsp_proto_from_zebra(nhlfe->type);
			if (nh_count >= CTC_HAL_MSG_MAX_LSP_NH)
				break;

			msg.nh[nh_count].ifindex =
				ToHslIfindex(nhlfe->nexthop->ifindex);
			msg.nh[nh_count].nexthop =
				nhlfe->nexthop->gate.ipv4;

			/* 只有真实出标签才用 SWAP：隐式空标签（label 3）
			 * 必须按 POP/PHP 处理，把 label 3 压上线是非法的。 */
			if (nhlfe->nexthop->nh_label &&
			    nhlfe->nexthop->nh_label->num_labels > 0 &&
			    nhlfe->nexthop->nh_label->label[0] !=
				    MPLS_LABEL_IMPLICIT_NULL) {
				msg.nh[nh_count].out_label =
					nhlfe->nexthop->nh_label->label[0];
				/* SWAP：in_label -> out_label */
				msg.nh[nh_count].flags = HAL_MPLS_FLAG_SWAP;
 			} else {

				/* POP/PHP（隐式空标签）：无出标签。只发 POP 位；
				 * 不再发 DECAP——本芯片的 ilm->pop 等价
				 * DsMpls.continuing，是"弹掉本标签后继续用剩余
				 * 标签查表"，用在倒数第二跳（下层是 egress PE 的
				 * VPN 标签）必 miss 丢包；而 decap 会把下层标签当
				 * IP 头解析，同样是错的。PHP 的正确收尾是
				 * "弹掉本层 + 绑定一个 MPLS pop/PHP 型 nexthop
				 * 直接转发"，由 HSL 侧实现。 */
 				msg.nh[nh_count].out_label = 0;
 				msg.nh[nh_count].flags =
					HAL_MPLS_FLAG_POP;
 			}
			nh_count++;
		}
	}
	msg.nh_count = nh_count;

	if (IS_ZEBRA_DEBUG_DPLANE)
		zlog_debug("CTC-DPLANE: MPLS LSP op=%s in_label=%u nh_count=%d proto=%u",
			   dplane_op2str(op), msg.in_label, msg.nh_count,
			   msg.proto_type);

	ret = ctc_hal_cmd_send(HAL_MODULE_MPLS, hal_cmd, &msg, sizeof(msg));
	if (ret == 0) {
		/* 硬件下发完成——跳过内核 netlink 更新 */
		dplane_ctx_set_skip_kernel(ctx);
	} else if (ret == CTC_HAL_RET_MPLS_PLACEHOLDER) {
		/* ARP 尚未解析：HSL 把 ILM 挂在 TOCPU nexthop 上，芯片把
		 * MPLS 报文上送 CPU。内核不支持 MPLS，所以也跳过 netlink
		 * 更新——ARP 学习后由 HSL 升级 ILM 为硬件转发
		 * （hw_mpls_arp_resolved）。 */
		dplane_ctx_set_skip_kernel(ctx);
		if (IS_ZEBRA_DEBUG_DPLANE)
			zlog_debug("CTC-DPLANE: MPLS LSP %u installed in ARP placeholder mode, waiting for ARP",
				   in_label);
	} else {
		/* HAL/HW 安装失败（如 POP nexthop 未解析）：不跳过内核
		 * 更新，让 zebra 记录失败状态，LSP 保持在非硬件卸载
		 * 路径。 */
		zlog_warn("CTC-DPLANE: MPLS ctc_hal_cmd_send failed, falling back to kernel");
	}

	/* MPLS 流统计跟随 LSP 生命周期（仅成功时） */
	if (op == DPLANE_OP_LSP_DELETE) {
		if (ret == 0)
			ctc_dplane_mpls_stats_send(HAL_MSG_MPLS_STATS_DEL,
						   in_label);
	} else if (ret == 0 || ret == CTC_HAL_RET_MPLS_PLACEHOLDER) {
		ctc_dplane_mpls_stats_send(HAL_MSG_MPLS_STATS_ADD, in_label);
	}
}

/* ----------------------------------------------------------------------
 * VPWS 伪线 (PW) 处理
 *
 * 参考 SAI 方式（ctc_sai_mpls.c VPWS 分支 + SDK 官方 VPWS 示例）的
 * 语义，把 PW 的 VC 标签、PW 类型、控制字标志、AC 关联口以及 PSN
 * 隧道路径打包成 HAL 消息下发给 HSL：
 *   - 入方向 (PW->AC)：VC 入标签 -> VPWS 类型 ILM（decap 后按 AC 出口转发）
 *   - 出方向 (AC->PW)：AC 口 vlan mapping -> VPWS push nexthop（VC + 隧道标签）
 * ---------------------------------------------------------------------- */
static void ctc_dplane_process_pw(struct zebra_dplane_ctx *ctx)
{
	enum dplane_op_e op = dplane_ctx_get_op(ctx);
	struct ctc_hal_msg_mpls_pw msg = {};
	const struct nexthop_group *nhg;
	struct nexthop *nh;
	int hal_cmd;
	int nh_count = 0;
	int ret;

	switch (op) {
	case DPLANE_OP_PW_INSTALL:
		hal_cmd = HAL_MSG_MPLS_PW_ADD;
		break;
	case DPLANE_OP_PW_UNINSTALL:
		hal_cmd = HAL_MSG_MPLS_PW_DEL;
		break;
	default:
		return;
	}

	msg.in_label = dplane_ctx_get_pw_local_label(ctx);
	msg.out_label = dplane_ctx_get_pw_remote_label(ctx);
	msg.pw_type = dplane_ctx_get_pw_type(ctx);
	msg.proto_type = CTC_PROTO_LDP; /* PW 由 LDP 下发 */
	/* AC 关联口：zebra PW ctx 无 AC 信息（ldpd 不传 member interface），
	 * 用 PW 接口 ifindex 换算后传递，HSL 侧解析为真实 AC gport。
	 * HSL 会拒绝 L3 接口（vlanif 编码）作为 AC：VPWS AC 必须
	 * 是二层口。 */
	msg.ac_ifindex = ToHslIfindex(dplane_ctx_get_ifindex(ctx));
	if (dplane_ctx_get_pw_flags(ctx) & F_PSEUDOWIRE_CWORD)
		msg.flags |= HAL_MPLS_PW_FLAG_CWORD;

	/* PSN 隧道路径：从 FIB nexthop 组提取带标签 nexthop（隧道标签 + 出口） */
	if (op == DPLANE_OP_PW_INSTALL) {
		nhg = dplane_ctx_get_pw_nhg(ctx);
		if (nhg) {
			for (ALL_NEXTHOPS_PTR(nhg, nh)) {
				if (CHECK_FLAG(nh->flags,
					       NEXTHOP_FLAG_RECURSIVE))
					continue;
				if (nh_count >= CTC_HAL_MSG_MAX_LSP_NH)
					break;
				msg.nh[nh_count].ifindex =
					ToHslIfindex(nh->ifindex);
				if (nh->type == NEXTHOP_TYPE_IPV4 ||
				    nh->type == NEXTHOP_TYPE_IPV4_IFINDEX)
					msg.nh[nh_count].nexthop =
						nh->gate.ipv4;
				/* implicit-null(label 3) = PHP：
				 * 不压隧道标签，只压 VC 标签 */
				if (nh->nh_label &&
				    nh->nh_label->num_labels > 0 &&
				    nh->nh_label->label[0] !=
					    MPLS_LABEL_IMPLICIT_NULL)
					msg.nh[nh_count].tunnel_label =
						nh->nh_label->label[0];
				nh_count++;
			}
		}
	}
	msg.nh_count = nh_count;

	CDBG("CTC-DPLANE: VPWS PW %s in_label=%u out_label=%u type=0x%x cwen=%d ac_ifindex=%u psn_nh=%d",
	     dplane_op2str(op), msg.in_label, msg.out_label, msg.pw_type,
	     !!(msg.flags & HAL_MPLS_PW_FLAG_CWORD), msg.ac_ifindex,
	     msg.nh_count);

	ret = ctc_hal_cmd_send(HAL_MODULE_MPLS, hal_cmd, &msg, sizeof(msg));
	if (ret == 0) {
		/* 硬件下发完成：内核无 PW 支持，跳过 netlink 更新 */
		dplane_ctx_set_skip_kernel(ctx);
	} else {
		zlog_warn("CTC-DPLANE: VPWS PW %s in_label=%u HAL send failed ret=%d",
			  dplane_op2str(op), msg.in_label, ret);
		/* 让 zebra 感知失败：置 FAILURE + PW_NOT_FORWARDING，zebra
		 * 会通知 PW client（ldpd）并定时重试（HSL 侧 AC 不可用/
		 * 下发失败时 PW 不能静默报成功，否则管理面误报转发态）。
		 * 注意 process_func 末尾默认置 SUCCESS，此处先置 FAILURE，
		 * 由 process_func 检查后保留。 */
		dplane_ctx_set_pw_status(ctx, PW_NOT_FORWARDING);
		dplane_ctx_set_status(ctx, ZEBRA_DPLANE_REQUEST_FAILURE);
	}
}

/* ----------------------------------------------------------------------
 * Nexthop group handling
 * ---------------------------------------------------------------------- */

static void ctc_dplane_process_nexthop(struct zebra_dplane_ctx *ctx)
{
	/* Nexthop group install/update/delete –
	 * The CTC7132 driver currently processes nexthops inline with routes. */
	if (IS_ZEBRA_DEBUG_DPLANE)
		zlog_debug("CTC-DPLANE: NH op %s ignored (handled per-route)",
			   dplane_op2str(dplane_ctx_get_op(ctx)));
}

/* ----------------------------------------------------------------------
 * Interface address handling (VRRP, etc.)
 * ---------------------------------------------------------------------- */

static void ctc_dplane_process_intf_addr(struct zebra_dplane_ctx *ctx)
{
	/* Interface address install/uninstall –
	 * Extend here when the CTC7132 driver needs interface address sync. */
	if (IS_ZEBRA_DEBUG_DPLANE)
		zlog_debug("CTC-DPLANE: interface addr op %s not yet offloaded",
			   dplane_op2str(dplane_ctx_get_op(ctx)));
}

/* ----------------------------------------------------------------------
 * PIM handling
 * ---------------------------------------------------------------------- */

static void ctc_dplane_process_pim(struct zebra_dplane_ctx *ctx)
{
	/* PIM specific operations –
	 * Extend here when the CTC7132 driver supports PIM offloading. */
	if (IS_ZEBRA_DEBUG_DPLANE)
		zlog_debug("CTC-DPLANE: PIM op %s not yet offloaded",
			   dplane_op2str(dplane_ctx_get_op(ctx)));
}

/* ----------------------------------------------------------------------
 * VRF-aware Route Handling for L3VPN MPLS
 * ---------------------------------------------------------------------- */

/**
 * Send VRF-aware route to HSL with MPLS label stack support
 * 
 * This function handles routes that belong to specific VRFs and may carry
 * MPLS labels for L3VPN services. It extracts the full label stack from
 * nexthops and sends it to the hardware layer.
 */
static void ctc_dplane_send_route_vrf(struct zebra_dplane_ctx *ctx,
				      const struct prefix *dest_pfx,
				      const struct prefix *src_pfx,
				      const struct nexthop_group *ng,
				      int hal_cmd)
{
	struct ctc_hal_msg_route_vrf msg = {};
	struct nexthop *nh;
	int nh_count = 0;
	bool truncated = false;
	uint32_t vrf_id;
	afi_t afi;
	int route_type;
	char buf[INET6_ADDRSTRLEN];
	char buf2[INET6_ADDRSTRLEN];

	/* Extract context information */
	vrf_id = dplane_ctx_get_vrf(ctx);
	afi = dplane_ctx_get_afi(ctx);
	route_type = dplane_ctx_get_type(ctx);

	if (IS_ZEBRA_DEBUG_DPLANE) {
		if (afi == AFI_IP6)
			inet_ntop(AF_INET6, &dest_pfx->u.prefix6, buf, sizeof(buf));
		else
			inet_ntop(AF_INET, &dest_pfx->u.prefix4, buf, sizeof(buf));
		zlog_debug("CTC-DPLANE: VRF route %s/%d vrf=%u afi=%d proto=%d",
			   buf, dest_pfx->prefixlen, vrf_id, afi, route_type);
	}

	/* Fill message header */
	msg.vrf_id = vrf_id;
	msg.af = (afi == AFI_IP) ? AF_INET : AF_INET6;
	msg.prefixlen = dest_pfx->prefixlen;

	if (afi == AFI_IP) {
		msg.prefix.ipv4 = dest_pfx->u.prefix4.s_addr;
		if (src_pfx && src_pfx->family == AF_INET)
			msg.src_prefix.ipv4 = src_pfx->u.prefix4.s_addr;
	} else {
		memcpy(msg.prefix.ipv6, &dest_pfx->u.prefix6, sizeof(dest_pfx->u.prefix6));
		if (src_pfx && src_pfx->family == AF_INET6)
			memcpy(msg.src_prefix.ipv6, &src_pfx->u.prefix6,
			       sizeof(src_pfx->u.prefix6));
	}

	msg.proto_type = ctc_proto_from_zebra(route_type);
	msg.distance = dplane_ctx_get_distance(ctx);
	msg.metric = dplane_ctx_get_metric(ctx);

	/* Extract nexthops and MPLS label stack */
	for (ALL_NEXTHOPS_PTR(ng, nh)) {
		if (CHECK_FLAG(nh->flags, NEXTHOP_FLAG_RECURSIVE))
			continue;

		if (!nh->ifindex)
			continue;

		if (nh_count >= CTC_HAL_MSG_MAX_NH_VRF) {
			/* ECMP 成员超过单消息容量：标记截断并跳过该成员，绝不静默
			 * break —— 少装一条臂意味着芯片里该 ECMP 组永久缺臂，只能等
			 * 路由下次全量 UPDATE 才自愈。收尾按 truncated 告警。 */
			truncated = true;
			continue;
		}

		/* Fill nexthop info. gate union + gate_af follow the nexthop's
		 * own family (not the route family): a v6 VRF route can egress
		 * via a v4 gateway or vice versa. */
		msg.nh[nh_count].ifindex = ToHslIfindex(nh->ifindex);
		msg.nh[nh_count].type = nh->type;
		msg.nh[nh_count].flags = nh->flags;

		if (nh->type == NEXTHOP_TYPE_IPV4 ||
		    nh->type == NEXTHOP_TYPE_IPV4_IFINDEX) {
			msg.nh[nh_count].gate.ipv4 = nh->gate.ipv4.s_addr;
			msg.nh[nh_count].gate_af = AF_INET;
		} else if (nh->type == NEXTHOP_TYPE_IPV6 ||
			   nh->type == NEXTHOP_TYPE_IPV6_IFINDEX) {
			memcpy(msg.nh[nh_count].gate.ipv6, &nh->gate.ipv6,
			       sizeof(nh->gate.ipv6));
			msg.nh[nh_count].gate_af = AF_INET6;
		} else {
			/* Pure-IFINDEX (connected / VRF-device) nexthop: no gate */
			memset(msg.nh[nh_count].gate.ipv6, 0,
			       sizeof(msg.nh[nh_count].gate.ipv6));
			msg.nh[nh_count].gate_af = 0;
		}

		/* Extract outgoing MPLS label (network byte order, as stored by FRR) */
		if (nh->nh_label && nh->nh_label->num_labels > 0) {
			/* Filter implicit-null label (label 3) */
			if (nh->nh_label->label[0] != MPLS_LABEL_IMPLICIT_NULL) {
				msg.nh[nh_count].out_label = nh->nh_label->label[0];
				msg.nh[nh_count].is_mpls = 1;

				/* For VPN routes, capture full label stack */
				if (nh->nh_label->num_labels > 1) {
					msg.label_stack_depth = nh->nh_label->num_labels;
					if (msg.label_stack_depth > 3)
						msg.label_stack_depth = 3;
					for (int i = 0; i < msg.label_stack_depth; i++) {
						msg.label_stack[i] = nh->nh_label->label[i];
					}
				}
			}
		}

		if (IS_ZEBRA_DEBUG_DPLANE) {
			if (msg.nh[nh_count].gate_af == AF_INET6) {
				inet_ntop(AF_INET6, msg.nh[nh_count].gate.ipv6,
					  buf2, sizeof(buf2));
				zlog_debug("CTC-DPLANE:   nh#%d ifindex=%d gate6=%s label=%u is_mpls=%u",
					   nh_count, msg.nh[nh_count].ifindex, buf2,
					   msg.nh[nh_count].out_label, msg.nh[nh_count].is_mpls);
			} else {
				struct in_addr g4;

				g4.s_addr = msg.nh[nh_count].gate.ipv4;
				inet_ntop(AF_INET, &g4, buf2, sizeof(buf2));
				zlog_debug("CTC-DPLANE:   nh#%d ifindex=%d gate=%s label=%u is_mpls=%u",
					   nh_count, msg.nh[nh_count].ifindex, buf2,
					   msg.nh[nh_count].out_label, msg.nh[nh_count].is_mpls);
			}
		}

		nh_count++;
	}

	msg.nh_count = nh_count;

	if (truncated) {
		if (afi == AFI_IP6)
			inet_ntop(AF_INET6, &dest_pfx->u.prefix6, buf, sizeof(buf));
		else
			inet_ntop(AF_INET, &dest_pfx->u.prefix4, buf, sizeof(buf));
		zlog_warn("CTC-DPLANE: VRF route %s/%u vrf=%u has >%d ECMP nexthops; "
			  "only first %d sent to HSL, remaining dropped",
			  buf, dest_pfx->prefixlen, vrf_id,
			  CTC_HAL_MSG_MAX_NH_VRF, nh_count);
	}

	/* Send to HSL via HAL command (rides on HAL_MODULE_ROUTE) */
	if (ctc_hal_cmd_send(HAL_MODULE_ROUTE, hal_cmd, &msg, sizeof(msg)) < 0) {
		if (afi == AFI_IP6)
			inet_ntop(AF_INET6, &dest_pfx->u.prefix6, buf, sizeof(buf));
		else
			inet_ntop(AF_INET, &dest_pfx->u.prefix4, buf, sizeof(buf));
		zlog_warn("CTC-DPLANE: Failed to send VRF route to HSL (vrf=%u prefix=%s/%u)",
			  vrf_id, buf, msg.prefixlen);
	} else {
		if (IS_ZEBRA_DEBUG_DPLANE)
			zlog_debug("CTC-DPLANE: VRF route sent successfully (vrf=%u labels=%u)",
				   vrf_id, msg.label_stack_depth);
	}
}

/**
 * Process VRF-aware route update from Zebra
 * 
 * This function determines whether a route belongs to a non-default VRF
 * and dispatches it to the appropriate handler.
 */
static void ctc_dplane_process_route_vrf(struct zebra_dplane_ctx *ctx)
{
	const struct prefix *dest_pfx = dplane_ctx_get_dest(ctx);
	const struct prefix *src_pfx = dplane_ctx_get_src(ctx);
	enum dplane_op_e op = dplane_ctx_get_op(ctx);
	const struct nexthop_group *ng = dplane_ctx_get_ng(ctx);
	uint32_t vrf_id;
	int hal_cmd;

	if (!dest_pfx || !ng) {
		if (IS_ZEBRA_DEBUG_DPLANE)
			zlog_debug("CTC-DPLANE: skipping VRF route, missing dest or ng");
		return;
	}

	vrf_id = dplane_ctx_get_vrf(ctx);

	/* Only process non-default VRF routes here */
	if (vrf_id == VRF_DEFAULT) {
		/* Default VRF routes use standard handler */
		ctc_dplane_process_route(ctx);
		return;
	}

	/* Determine HAL command based on operation.
	 * The af field inside the message distinguishes IPv4/IPv6, so only
	 * two commands are needed (see ctc_dplane.h for the wire format). */
	switch (op) {
	case DPLANE_OP_ROUTE_INSTALL:
	case DPLANE_OP_ROUTE_UPDATE:
		hal_cmd = HAL_MSG_RT_VRF_ADD;
		break;
	case DPLANE_OP_ROUTE_DELETE:
		hal_cmd = HAL_MSG_RT_VRF_DEL;
		break;
	default:
		if (IS_ZEBRA_DEBUG_DPLANE)
			zlog_debug("CTC-DPLANE: unsupported VRF route op %d", op);
		return;
	}

	/* 命中 MPLS 出口标签（带标签泄漏路由 / L3VPN 前缀）时跳过内核安装：
	 * hw 已装 push/ILM 表。implicit-null（label 3）不算 —— send_route_vrf
	 * 会把它过滤成普通路由，netlink 仍安装内核 FIB。v4/v6 VRF 路由同策略。 */
	{
		const struct nexthop *nh;
		bool has_real_label = false;
		for (ALL_NEXTHOPS_PTR(ng, nh)) {
			if (nh->nh_label && nh->nh_label->num_labels > 0 &&
			    nh->nh_label->label[0] != MPLS_LABEL_IMPLICIT_NULL) {
				has_real_label = true;
				break;
			}
		}
		if (has_real_label)
			dplane_ctx_set_skip_kernel(ctx);
	}

	/* Dispatch to VRF-aware handler (dual-stack：send_route_vrf 按 afi/nexthop
	 * 类型编码 prefix 与逐 nh gate，msg.af 区分 wire 上的 v4/v6)。 */
	ctc_dplane_send_route_vrf(ctx, dest_pfx, src_pfx, ng, hal_cmd);
}

/* ----------------------------------------------------------------------
 * Interface & VRF handling (for 'ip link add VRF' and 'master vrf')
 * ---------------------------------------------------------------------- */
static bool ctc_intf_is_vrf_related(struct zebra_dplane_ctx *ctx,
                                    const struct interface *ifp)
{
    /* ctx 侧：VRF 设备本身、VRF 成员口、或 ctx 说它归属某个非 default VRF */
    if (dplane_ctx_get_ifp_zif_type(ctx) == ZEBRA_IF_VRF)
        return true;
    if (dplane_ctx_get_ifp_zif_slave_type(ctx) == ZEBRA_IF_SLAVE_VRF)
        return true;
    if (dplane_ctx_get_ifp_vrf_id(ctx) != VRF_DEFAULT)
        return true;

    /* ifp 侧：它现在（删除前）就待在某个非 default VRF 里。
     * 覆盖 nomaster、删成员口、删 VRF 设备这些「离开」场景。 */
    if (ifp && ifp->vrf && ifp->vrf->vrf_id != VRF_DEFAULT)
        return true;
    if (ifp && if_is_vrf(ifp))
        return true;

    return false;
}

/* 由 zebra core (interface.c 的 zebra_if_dplane_result()) 调用 */
void ctc_dplane_intf_event(struct zebra_dplane_ctx *ctx);

/*
 * 接口 VRF 事件转发（HAL_MSG_INTF_VRF_UPDATE）。
 *
 * 为什么需要这个额外入口：本 FRR 版本没有任何地方产生“出方向”的
 * DPLANE_OP_INTF_* 事件 —— dplane_intf_add() 全树零调用者，
 * dplane_intf_update() 只服务于 protodown（EVPN-MH/MLAG）。
 * 内核 link 事件的 ctx 由 dataplane 线程构造后经
 * dplane_provider_enqueue_to_zebra() (if_netlink.c:1555) 直接投递给
 * zebra 主线程的结果回调 (zebra_dplane.c:6853 dg_results_cb ->
 * zebra_if_result -> zebra_if_dplane_result())，它不进入
 * dg_update_list，也不经过 provider 链，框架不会把它再分发给任何
 * provider。因此 ctc_dplane_process_func() 里
 * case DPLANE_OP_INTF_INSTALL/DELETE/UPDATE 永不命中，
 * HAL_MSG_INTF_VRF_UPDATE 也就从不发出，HSL 侧 hsl_vrf_create()
 * 从不执行。
 *
 * 于是改由 zebra 主线程在 ctx 到达时显式调用本函数。实测日志
 * （ip link add CUST1 type vrf table 100）：
 *   netlink-dp-in type RTM_NEWLINK(16)
 *   -> Intf dplane ctx ... op INTF_INSTALL, ifindex (13), result QUEUED
 *   -> [本函数] -> INTF_INSTALL for CUST1(13) / DPLANE_OP_INTF_UPDATE for
 *      VRF CUST1(13) table 100 / VRF CUST1 created, id 13
 *
 * 字段取法：
 *   op_type    : DELETE -> 1，其余 -> 0（HSL 侧 0=add/update，1=delete）
 *   vrf_id     : 取 netlink 解析出的新归属（VRF 设备=自身 ifindex、
 *                成员口=master ifindex、未绑定=VRF_DEFAULT）。RTM_DELLINK 的
 *                ctx 不填该字段，只能退回 ifp->vrf_id；此刻本函数在
 *                zebra_if_dplane_ifp_handling() 之前调用，ifp 仍然有效。
 *   is_vrf_dev : 取 ctx 的 zif_type（if_netlink.c 已由
 *                netlink_determine_zebra_iftype() 填好）。不能用
 *                if_is_vrf()：它看的 ZEBRA_INTERFACE_VRF_LOOPBACK 要等
 *                ifp_handling() 处理完才置位，此处恒为假。DELETE 的 ctx 不填
 *                zif_type，退回 if_is_vrf(ifp)（该标志在 if_delete_update()
 *                里才清，本函数调用时仍在）。
 */
void ctc_dplane_intf_event(struct zebra_dplane_ctx *ctx)
{
	ifindex_t ifindex = dplane_ctx_get_ifindex(ctx);
	enum dplane_op_e op = dplane_ctx_get_op(ctx);
	struct interface *ifp;
	struct ctc_hal_msg_intf_vrf msg = {};
	vrf_id_t vrf_id;
	uint8_t is_vrf_dev;

	if (ifindex == IFINDEX_INTERNAL)
		return;

	/* VRF 设备与成员接口都能查到：VRF_LITE 后端下
	 * if_lookup_by_index() 走 if_lookup_by_index_all_vrf()。 */
	ifp = if_lookup_by_index(ifindex, VRF_DEFAULT);
	if (!ctc_intf_is_vrf_related(ctx, ifp)) {
		if (IS_ZEBRA_DEBUG_DPLANE || IS_ZEBRA_DEBUG_KERNEL)
			zlog_debug("CTC-DPLANE: intf %s (idx %u) op %s vrf %u, is_vrf_dev=%u, 
				tabel_id %u, zip_slave_type %u, master_ifindex %u",
				ifp ? ifp->name : "?", ifindex, dplane_op2str(op),
				dplane_ctx_get_ifp_vrf_id(ctx), dplane_ctx_get_ifp_zif_type(ctx),
				dplane_ctx_get_ifp_table_id(ctx), dplane_ctx_get_ifp_zif_slave_type(ctx),
				dplane_ctx_get_ifp_master_ifindex(ctx));
		return;
	}

	if (op == DPLANE_OP_INTF_DELETE) {
		if (!ifp) {
			if (IS_ZEBRA_DEBUG_DPLANE)
				zlog_debug("CTC-DPLANE: intf %u not found",
					   ifindex);
			return;
		}
		/* 本版本 struct interface 无 vrf_id 成员，取所属 VRF 的 id
		 * （VRF 设备的 vrf_id == 自身 ifindex）。此刻
		 * if_delete_update() 尚未执行，ifp->vrf 仍是原 VRF。 */
		vrf_id = ifp->vrf->vrf_id;
		is_vrf_dev = if_is_vrf(ifp) ? 1 : 0;
	} else {
		vrf_id = dplane_ctx_get_ifp_vrf_id(ctx);
		is_vrf_dev = (dplane_ctx_get_ifp_zif_type(ctx) == ZEBRA_IF_VRF)
				     ? 1 : 0;
	}

	/* Fill message */
	msg.ifindex = ToHslIfindex(ifindex);
	msg.vrf_id = vrf_id;
	msg.is_vrf_dev = is_vrf_dev;
	msg.op_type = (op == DPLANE_OP_INTF_DELETE) ? 1 : 0;

	if (IS_ZEBRA_DEBUG_DPLANE || IS_ZEBRA_DEBUG_KERNEL)
		zlog_debug("CTC-DPLANE: intf %s (idx %u) op %s vrf %u, is_vrf_dev=%u",
			   ifp ? ifp->name : "?", ifindex, dplane_op2str(op),
			   vrf_id, is_vrf_dev);

	/* Send to HSL via HAL command.
	 * Note: If HSL does not yet support HAL_MSG_INTF_VRF_UPDATE, this will log
	 * an error but will not affect zebra core operation. */
	if (ctc_hal_cmd_send(HAL_MODULE_ROUTE, HAL_MSG_INTF_VRF_UPDATE,
			     &msg, sizeof(msg)) < 0) {
		zlog_warn("CTC-DPLANE: intf %s (idx %u) VRF update send failed (cmd %d)",
			  ifp ? ifp->name : "?", ifindex,
			  HAL_MSG_INTF_VRF_UPDATE);
	}
}

/* provider 出方向回调里的入口（本版本不会命中，保留以便将来
 * dplane_intf_add()/dplane_intf_update() 被接上时自动生效）。 */
static void ctc_dplane_process_intf(struct zebra_dplane_ctx *ctx)
{
	ctc_dplane_intf_event(ctx);
}
/* ----------------------------------------------------------------------
 * Dataplane provider callbacks
 * ---------------------------------------------------------------------- */

static int ctc_dplane_process_func(struct zebra_dplane_provider *prov)
{
	struct zebra_dplane_ctx *ctx;
	int counter, limit;

	limit = dplane_provider_get_work_limit(prov);

	for (counter = 0; counter < limit; counter++) {
		ctx = dplane_provider_dequeue_in_ctx(prov);
		if (!ctx)
			break;

		switch (dplane_ctx_get_op(ctx)) {
		case DPLANE_OP_ROUTE_INSTALL:
		case DPLANE_OP_ROUTE_UPDATE:
		case DPLANE_OP_ROUTE_DELETE:
			/* Check if this is a VRF route */
			if (dplane_ctx_get_vrf(ctx) != VRF_DEFAULT) {
				/* Non-default VRF: use VRF-aware handler */
				ctc_dplane_process_route_vrf(ctx);
			} else {
				/* Default VRF: use standard handler */
				ctc_dplane_process_route(ctx);
			}
			//ctc_dplane_process_route(ctx);
			break;

		case DPLANE_OP_LSP_INSTALL:
		case DPLANE_OP_LSP_UPDATE:
		case DPLANE_OP_LSP_DELETE:
			ctc_dplane_process_lsp(ctx);
			break;

		case DPLANE_OP_PW_INSTALL:
		case DPLANE_OP_PW_UNINSTALL:
			ctc_dplane_process_pw(ctx);
			break;

		case DPLANE_OP_NH_INSTALL:
		case DPLANE_OP_NH_UPDATE:
		case DPLANE_OP_NH_DELETE:
			ctc_dplane_process_nexthop(ctx);
			break;

		case DPLANE_OP_ADDR_INSTALL:
		case DPLANE_OP_ADDR_UNINSTALL:
			ctc_dplane_process_intf_addr(ctx);
			break;

		case DPLANE_OP_INTF_INSTALL:
		case DPLANE_OP_INTF_UPDATE:
		case DPLANE_OP_INTF_DELETE:
			ctc_dplane_process_intf(ctx);
			break;

		default:
			/* Unhandled – just log when debugging */
			if (IS_ZEBRA_DEBUG_DPLANE)
				zlog_debug("CTC-DPLANE: unhandled op %s",
					   dplane_op2str(
						   dplane_ctx_get_op(ctx)));
			break;
		}

		/* 默认成功：让 zebra 继续推进；process_xxx 内部置 FAILURE
		 * 时（如 VPWS PW 的 HAL 下发失败）不覆盖，zebra 会按失败
		 * 路径处理（PW 置 NOT_FORWARDING 并定时重试）。 */
		if (dplane_ctx_get_status(ctx) != ZEBRA_DPLANE_REQUEST_FAILURE)
			dplane_ctx_set_status(ctx, ZEBRA_DPLANE_REQUEST_SUCCESS);
		dplane_provider_enqueue_out_ctx(prov, ctx);
	}

	return 0;
}

static int ctc_dplane_shutdown_func(struct zebra_dplane_provider *prov,
				    bool early)
{
	if (IS_ZEBRA_DEBUG_DPLANE)
		zlog_debug("CTC-DPLANE: shutdown %s",
			   early ? "early" : "final");

	return 0;
}

/* ----------------------------------------------------------------------
 * Public initialisation
 * ---------------------------------------------------------------------- */

void ctc_dplane_provider_init(void)
{
	int ret;

	hal_init(HAL_FRR);

	ret = dplane_provider_register("CTC-HW", DPLANE_PRIO_PRE_KERNEL,
				       DPLANE_PROV_FLAGS_DEFAULT, NULL,
				       ctc_dplane_process_func,
				       ctc_dplane_shutdown_func,
				       NULL, NULL);

	if (ret != 0)
		flog_err(EC_LIB_DEVELOPMENT,
			 "Unable to register CTC dplane provider: %d", ret);
	else
		zlog_info("CTC-DPLANE: CTC hardware offload provider registered");
}
