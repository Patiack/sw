#include "hw.h"
#include "hw_chip.h"
#include "hsl_lib.h"
#include "hsl_port.h"
#include "hsl_trunk.h"
#include "hsl_nh.h"
#include "hsl_rt_vrf.h"     /* L3VPN：struct hsl_nexthop / struct in_addr */
#include "hw_mac.h"
#include "hw_port.h"

#include <linux/mutex.h> /* DEFINE_MUTEX（与 hsl_nh.c 一致） */

#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_mpls.h"
#include "ctc_nexthop.h"
#include "ctc_const.h"
#include "ctc_vlan.h"
#include "def_mpls.h"

/* ------------------------------------------------------------------
 * MPLS Nexthop 缓存
 *
 * 一个 MPLS push nexthop（SWAP：PUSH_OP_NONE，BGP-LU：PUSH_OP_ROUTE）
 * 在多个 ILM 表项 / 路由之间共享。此缓存跟踪已创建的 SDK nexthop，
 * 以便在最后一个引用释放后才真正删除。
 * ------------------------------------------------------------------ */

#define HW_MPLS_NH_MAX      128
#define HW_MPLS_ILM_MAX     2048
#define HW_MPLS_NH_ID_MIN   3
#define HW_MPLS_NH_ID_MAX   4096
#define HW_MPLS_ECMP_MAX        32
#define HW_MPLS_ECMP_MEMBER_MAX 8
#define HW_MPLS_NH_LABEL_MAX    3    /* 单个 push nexthop 最多压入标签数 */

struct hw_mpls_nh_cache {
    uint32_t nhid;
    uint32_t out_label;     /* 最外层标签（= labels[0]，日志 / 单标签兼容） */
    uint32_t labels[HW_MPLS_NH_LABEL_MAX]; /* 完整压入栈，网络序、外层在前；
                                            * 单标签时 labels[0] = out_label */
    uint8_t  label_num;     /* 1..HW_MPLS_NH_LABEL_MAX（默认 1） */
    uint32_t nexthop;   /* 主机序下一跳 IP：ARP 刷新事件匹配用 */
    uint32_t ifindex;   /* HSL ifindex（vlanif 编码或物理口） */
    uint32_t opcode;    /* CTC_MPLS_NH_PUSH_OP_NONE / _ROUTE */
    uint32_t gport;     /* 最近一次下发到 tunnel 的出端口（刷新比对） */
    uint32_t ref_count;
    uint8_t  mac[6];
    uint8_t  used;
};

static struct hw_mpls_nh_cache g_hw_mpls_nh[HW_MPLS_NH_MAX];

/* ECMP 组缓存（完整定义前置：hw_mpls_nh_alloc_id 需要避开 ECMP
 * 组已占用的 nhid，而 ECMP 定义在文件后部）。 */
struct hw_mpls_ecmp_cache {
    uint32_t nhid;
    uint32_t members[HW_MPLS_ECMP_MEMBER_MAX];
    uint8_t  member_is_push[HW_MPLS_ECMP_MEMBER_MAX]; /* 1：MPLS push NH */
    uint8_t  member_num;
    uint32_t ref_count;
    uint8_t  used;
};
static struct hw_mpls_ecmp_cache g_hw_mpls_ecmp[HW_MPLS_ECMP_MAX];

/* PHP（倒数第二跳弹出）出向 nexthop 缓存。
 *
 * 普通 LSP 的 POP/PHP 路径**不能**复用 FEC 的 IP 单播 nexthop：那种
 * nexthop 没有任何标签动作，SDK 在 _sys_usw_mpls_ilm_set_nhinfo 里判
 * dsfwd_valid 后走 dsFwd 路径，ILM 的 adNextHopPtr 无效（实测 P 收到
 * [16][144][IP] 后 dest-type DROP、ipe-drop IPE_FATAL_EXCEP_DIS）；
 * 也不能给 ILM 置 ilm->pop（SDK 直写成 DsMpls.continuing_f = "弹掉本
 * 标签后继续用剩余标签查表"，在倒数第二跳上剩余的标签是 egress PE 的
 * VPN 标签，本机没有它的 ILM → 二级查表 miss → 无转发结果 → 致命异常
 * 丢弃，见 hw_mpls_ilm_param_fill）。
 *
 * 正确构造：ILM 不置 pop/decap，绑定一个"弹掉本层后直接转发"的出向
 * nexthop——只带二层改写、不带任何标签 edit，报文以 [VPN 标签][IP]
 * 原样出向。两种实现（kind，B 失败回退 A）：
 *   - B（首选，SDK 官方 PHP 构造）：nh_prop = CTC_MPLS_NH_POP_TYPE +
 *     nh_com.opcode = CTC_MPLS_NH_PHP（无标签 edit，只建 l2edit）。
 *   - A（回退，空标签链 push）：PUSH_OP_NONE + label_num = 0，tunnel
 *     只承载 oif/DMAC（MAC fwd 模式），push 不压标签——与单标签 SWAP
 *     路径完全同构，只是压 0 个标签。
 * key = (nexthop, ifindex)：同一 (出接口, 下一跳 IP) 上的多条 ILM 共享。
 * 数据面验证：P 的 pkt-trace 不应再出现 2nd-label-lookup-result，
 * dest-type 应为 Network Port；PE2 核心口只应看到一个标签（VPN 标签）。 */
#define HW_MPLS_PHP_NH_KIND_SDK_POP    1 /* B：CTC_MPLS_NH_POP_TYPE/PHP */
#define HW_MPLS_PHP_NH_KIND_EMPTY_PUSH 2 /* A：PUSH_OP_NONE + label_num=0 */

struct hw_mpls_php_nh_cache {
    uint32_t nhid;
    uint32_t nexthop;   /* 主机序下一跳 IP：ARP 刷新事件匹配用 */
    uint32_t ifindex;
    uint32_t flags;     /* 原始路径 flags（TTL_PIPE 等，刷新时重建参数） */
    uint32_t gport;     /* 最近一次下发的出端口（刷新比对） */
    uint32_t ref_count;
    uint8_t  mac[6];
    uint8_t  kind;      /* HW_MPLS_PHP_NH_KIND_* */
    uint8_t  used;
};
static struct hw_mpls_php_nh_cache g_hw_mpls_php_nh[HW_MPLS_NH_MAX];

/* 保护 g_hw_mpls_nh / g_hw_mpls_ecmp / g_hw_mpls_php_nh 的分配与释放。HSL 的消息
 * 处理是多线程的（路由 / IPUC / MPLS 消息可能并发到达），
 * hw_mpls_nh_create 的 "alloc_id -> SDK 创建 -> 写缓存" 序列若不加锁，
 * 两个线程会同时分配到同一个 nhid（实测 nhid=6 的 push 被 label=17
 * 覆盖，3.3.3.3 的转发标签被污染）。 */
static DEFINE_MUTEX(hw_mpls_nh_mutex);

/* 多路径 ILM nexthop 表项（参见 ctc_hal_msg_mpls_lsp_nh） */
struct hw_mpls_ilm_nh {
    uint32_t out_label;
    uint32_t flags;
    uint32_t nexthop;
    uint32_t ifindex;
};

/* ILM 表项的 in_label -> nhid 映射。nhid 可能是单个 MPLS push
 * nexthop（SWAP）、IP nexthop（POP）、ECMP 组（多路径 LSP）或保留的
 * TOCPU nexthop（ARP 占位）；is_ecmp 告诉释放路径是哪种。
 *
 * 当某条路径的 ARP 未解析时，ILM 安装为指向
 * CTC_NH_RESERVED_NHID_FOR_TOCPU，报文被上送 CPU 而不是被芯片丢弃
 * （内核也无法转发——不支持 MPLS——所以流量会中断直到 ARP 解析）；
 * 原始路径保留在 ph_nhs[] 中，直到 hw_mpls_arp_resolved() 把表项
 * 升级到真实 nexthop。 */
struct hw_mpls_ilm_cache {
    uint32_t in_label;
    uint32_t nhid;
    uint8_t  is_ecmp;
    uint8_t  placeholder; /* 1：绑定 TOCPU，等待 ARP */
    uint8_t  ph_count;    /* ph_nhs[] 中的有效路径数（占位模式） */
    uint8_t  used;
    struct hw_mpls_ilm_nh ph_nhs[HW_MPLS_ECMP_MEMBER_MAX];
};

static struct hw_mpls_ilm_cache g_hw_mpls_ilm[HW_MPLS_ILM_MAX];

/* 每标签 MPLS 流统计：stats_id 从 SDK 统计池分配
 * （CTC_STATS_STATSID_TYPE_MPLS，与 SAI 层相同），通过
 * SYS_MPLS_STATS_EN 挂到 ILM 上。与 g_hw_mpls_ilm 分开保存，
 * 使 id 在 ILM 重建（update / ARP 升级）后仍然有效。 */
struct hw_mpls_stats_cache {
    uint32_t label;
    uint32_t stats_id;
    uint8_t  used;
};
static struct hw_mpls_stats_cache g_hw_mpls_stats[HW_MPLS_ILM_MAX];

static int hw_mpls_stats_find(uint32_t label);
static int hw_mpls_stats_alloc(uint32_t label, uint32_t stats_id);
static void hw_mpls_stats_free(int idx);
static int hw_mpls_stats_attach(uint32_t label, uint8_t spaceid);

/* ------------------------------------------------------------------
 * L3 VPN：VRF LSP（ILM）/ FEC 缓存与常量
 *
 * SDK 5.5.3 对 L3VPN 的支持方式（与旧 hw_mpls.c 一致）：
 *   - egress PE 的 VPN 标签用 type = CTC_MPLS_LABEL_TYPE_L3VPN +
 *     id_type = CTC_MPLS_ID_VRF + vrf_id（ilm.vrf_id 与
 *     ilm.flw_vrf_srv_aps.vrf_id）。芯片对这类 ILM 设
 *     innerPacketLookup_f = TRUE：弹掉 VC 标签后按内层 IP 在 vrf_id
 *     对应 L3 路由表查找（u4_g2_fid_f = vrf_id）并校验 BOS。
 *   - 普通 ILM 缓存以 in_label 为全局键；VRF ILM 键为 (vrf_id, in_label)，
 *     必须独立缓存。FEC 绑定（SDK 无 FTN API）只做软件登记。
 *   - g_hw_mpls_vrf_ilm / g_hw_mpls_vrf_fec 与普通 ILM 数组一样，在 HSL
 *     NH 串行化上下文（ARP 事件、消息处理）中无锁访问；硬件增删受
 *     hw_mpls_vrf_mutex 保护。
 * ------------------------------------------------------------------ */
#define HW_MPLS_VRF_ILM_MAX   512
#define HW_MPLS_VRF_FEC_MAX   1024
#define HW_MPLS_VRF_ID_MAX    4095

/* HSL_MPLS_OP_*（与 hsl_mpls_vrf.h 取值一致：PUSH=1/POP=2/SWAP=3，
 * 数值等于线上 CTC_MPLS_OP_*）。hw 层不引 hsl_mpls_vrf.h，单点定义。 */
#ifndef HSL_MPLS_OP_PUSH
#define HSL_MPLS_OP_PUSH  1
#define HSL_MPLS_OP_POP   2
#define HSL_MPLS_OP_SWAP  3
#endif

struct hw_mpls_vrf_ilm_cache {
    uint32_t vrf_id;
    uint32_t in_label;
    uint32_t nhid;
    uint8_t  is_ecmp;
    uint8_t  placeholder; /* 1：绑定 TOCPU，等待 ARP */
    uint8_t  ph_count;    /* ph_nhs[] 中的有效路径数 */
    uint8_t  used;
    struct hw_mpls_ilm_nh ph_nhs[HW_MPLS_ECMP_MEMBER_MAX];
};

static struct hw_mpls_vrf_ilm_cache g_hw_mpls_vrf_ilm[HW_MPLS_VRF_ILM_MAX];

struct hw_mpls_vrf_fec_entry {
    uint32_t vrf_id;
    uint32_t prefix;    /* 网络序 */
    uint8_t  prefixlen;
    uint8_t  used;
    uint32_t label;     /* 网络序 */
};
static struct hw_mpls_vrf_fec_entry g_hw_mpls_vrf_fec[HW_MPLS_VRF_FEC_MAX];

/* 保护 g_hw_mpls_vrf_ilm / g_hw_mpls_vrf_fec 硬件增删（ARP 扫描与消息
 * 处理互斥；ARP 事件本身无锁串行，见文件头部 hw_mpls_nh_mutex 注释） */
static DEFINE_MUTEX(hw_mpls_vrf_mutex);

/* MPLS tunnel label nexthop（hw_mpls_nh_create 的两步模型第一步）
 * 定义在文件后部，这里前置声明 */
int hw_nh_add_mpls_tunnel_label(uint16_t tunnel_id,
		ctc_mpls_nexthop_tunnel_param_t *p_nh_param);
int hw_nh_update_mpls_tunnel_label(uint16_t tunnel_id,
		ctc_mpls_nexthop_tunnel_param_t *p_nh_param);
int hw_nh_remove_mpls_tunnel_label(uint16_t tunnel_id);
/* 定义在文件后部（PHP 出向 nexthop 的 ARP 刷新使用） */
int hw_nh_update_mpls(uint32_t nhid, ctc_mpls_nexthop_param_t *p_nh_param);

/* 定义在 ILM 段（hw_mpls_nh_tunnel_refresh / hw_mpls_ilm_refresh
 * 在文件前部使用，这里前置声明） */
static int hw_mpls_nh_lookup(uint32_t nexthop, uint32_t ifindex,
                             uint32_t *p_nhid, uint8_t *mac);

/* 全栈匹配（单标签 / 多标签通用）：labels 网络序外层在前 */
static int hw_mpls_nh_find_key(const uint32_t *labels, uint8_t label_num,
                               uint32_t ifindex, uint32_t opcode)
{
    int i, j;

    if (label_num == 0 || label_num > HW_MPLS_NH_LABEL_MAX)
        return -1;
    for (i = 0; i < HW_MPLS_NH_MAX; i++) {
        if (!g_hw_mpls_nh[i].used ||
            g_hw_mpls_nh[i].label_num != label_num ||
            g_hw_mpls_nh[i].ifindex != ifindex ||
            g_hw_mpls_nh[i].opcode != opcode)
            continue;
        for (j = 0; j < label_num; j++) {
            if (g_hw_mpls_nh[i].labels[j] != labels[j])
                break;
        }
        if (j == label_num)
            return i;
    }
    return -1;
}

/* 单标签版：只匹配 label_num==1 的条目，避免把“同外层的多标签
 * 栈条目”误当作单标签 push 复用（L3VPN 双层栈与 BGP-LU 单标签
 * 可能共享外层 transport 标签） */
static int hw_mpls_nh_find(uint32_t out_label, uint32_t ifindex, uint32_t opcode)
{
    int i;

    for (i = 0; i < HW_MPLS_NH_MAX; i++) {
        if (g_hw_mpls_nh[i].used &&
            g_hw_mpls_nh[i].label_num == 1 &&
            g_hw_mpls_nh[i].labels[0] == out_label &&
            g_hw_mpls_nh[i].ifindex == ifindex &&
            g_hw_mpls_nh[i].opcode == opcode)
            return i;
    }
    return -1;
}

/* 扫描保留区间，找一个空闲的 SDK nhid */
static int hw_mpls_nh_alloc_id(uint8 unit, uint32_t *p_nhid)
{
    uint32_t i;
    int j;
    ctc_nh_info_t out;
    int32_t ret;

    for (i = HW_MPLS_NH_ID_MIN; i <= HW_MPLS_NH_ID_MAX; i++) {
        /* 先查 HSL 本地缓存：跳过已分配给 MPLS push / ECMP 组的
         * nhid，保证 MPLS 内部不重复分配（实测 label 16 的 push
         * 被 label 17 覆盖）。
         *
         * 与 IP 路由的 nhid（hw_l3_nexthop_find，同一 3..4096
         * 区间）的避让靠 ctcs_nh_get_nh_info 探测统一 nh 表
         * （dsnh，按 nhid 索引）完成：IPUC / MPLS push / ECMP 均
         * 可见（见 ctc_tmx_nh_get_nh_info）。但 tunnel label 对象
         * （ctcs_nh_add_mpls_tunnel_label，tunnel_id == nhid）注册
         * 在 MPLS 独立表（sys_nh_db_mpls_tunnel_t，按 tunnel_id
         * 索引，见 sys_usw_mpls_nh_remove_tunnel_label），不在
         * dsnh 表内、探测不到——本地缓存必须覆盖它，否则后续
         * 分配会踩到已被 tunnel 占用的 id。 */
        for (j = 0; j < HW_MPLS_NH_MAX; j++) {
            if (g_hw_mpls_nh[j].used && g_hw_mpls_nh[j].nhid == i)
                break;
        }
        if (j < HW_MPLS_NH_MAX)
            continue;
        for (j = 0; j < HW_MPLS_ECMP_MAX; j++) {
            if (g_hw_mpls_ecmp[j].used && g_hw_mpls_ecmp[j].nhid == i)
                break;
        }
        if (j < HW_MPLS_ECMP_MAX)
            continue;
        /* PHP 出向 nexthop 同样占用 3..4096 区间（无 local cache 覆盖
         * 时会被后续分配踩掉，实测 push 被覆盖过） */
        for (j = 0; j < HW_MPLS_NH_MAX; j++) {
            if (g_hw_mpls_php_nh[j].used && g_hw_mpls_php_nh[j].nhid == i)
                break;
        }
        if (j < HW_MPLS_NH_MAX)
            continue;

        sal_memset(&out, 0, sizeof(out));
        ret = ctcs_nh_get_nh_info(unit, i, &out);
        if (ret != CTC_E_NONE) {
            *p_nhid = i;
            return RUNOS_OK;
        }
    }
    return RUNOS_FULL;
}

/* 把 HSL ifindex（以及可选的学习到的 MAC）解析为 SDK oif。
 * nexthop 为主机序下一跳 IP（HSL nh 表键值，0 表示未知）。
 *
 * 解析优先级与 IPUC 路径 hw_l3_add_nh 保持一致：优先使用 HSL
 * nexthop 表（g_hsl_nh）中 ARP 学习到的 l2_ifindex——它是 FDB
 * 记录的真实物理出端口且经过 up 校验；MAC 反查（hw_mac_get_port）
 * 可能命中错误的 FDB 端口条目（如 down 的 XFI 口），仅作 fallback。 */
static int hw_mpls_oif_fill(uint32_t nexthop, uint32_t ifindex,
                            uint8_t *mac, ctc_nh_oif_info_t *oif)
{
    hsl_if_t *ifp;
    hsl_nh_entry_t *nh = NULL;
    uint32_t l2_ifindex;
    hw_lport_t hport;

    sal_memset(oif, 0, sizeof(*oif));

    ifp = hsl_get_if_by_ifindex(ifindex);
    if (ifp == NULL)
        return RUNOS_PORT;

    oif->vid = ifp->l3_intf.vid;

    /* 优先：HSL nexthop 表中的 l2_ifindex（ARP 学习时从 FDB 记录的
     * 真实物理端口，且校验端口 up）。与 IPUC 路径 hw_l3_add_nh 的
     * 解析优先级一致，避免 MPLS tunnel 指向 MAC 反查出的错误端口 */
    if (nexthop) {
        hsl_prefix_t prefix;

        sal_memset(&prefix, 0, sizeof(prefix));
        prefix.family = AF_INET;
        prefix.prefixlen = 32;
        prefix.u.prefix4 = htonl(nexthop); /* HSL nh 表按网络序保存 */
        nh = hsl_rt_lookup_nh__(ifindex, &prefix);
    }
    if (nh && nh->l2_ifindex > 0 && hsl_port_is_running(nh->l2_ifindex)) {
        ifindex = nh->l2_ifindex;
    } else {
        /* fallback：下一跳 MAC 已知时，从 FDB 反查二层出接口 */
        l2_ifindex = ifindex;
        if (mac && (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) &&
            hw_mac_get_port(oif->vid, mac, &l2_ifindex) == RUNOS_OK &&
            l2_ifindex != UIF_IFINDEX_INVALID) {
            ifindex = l2_ifindex;
        }
    }

    if (UIF_IFINDEX_IS_TRUNK(ifindex)) {
        oif->gport = CTC_MAP_TID_TO_GPORT(HW_TRUNK_ID(UIF_GET_TRUNK_ID(ifindex)));
    } else {
        ifindex2hport(ifindex, &hport);
        oif->gport = hport.port;
    }

    return RUNOS_OK;
}

/* 创建（或复用）MPLS push nexthop 并持有一个引用。
 * 整个 alloc -> SDK 创建 -> 写缓存序列在 hw_mpls_nh_mutex 保护下
 * 原子执行，避免并发消息处理线程分配到同一个 nhid。
 *
 * labels[]：网络序、外层在前（= FRR label_stack 原样）。
 *  - label_num == 1：普通 LSP（SWAP/PUSH_OP_NONE 或 BGP-LU 单标签
 *    PUSH_OP_ROUTE），单标签放 push，tunnel 只承载 oif/mac。
 *  - label_num >= 2（PUSH_OP_ROUTE，L3VPN [transport, vpn] 双层栈）：
 *    外层 transport 标签 labels[0] 放 tunnel label nexthop（压最外层），
 *    内层 VPN 标签放 push（push_label[] 内层在前逆序），与本文件 VPWS
 *    双标签实测构造一致。 */
static int hw_mpls_nh_create_key(const uint32_t *labels, uint8_t label_num,
                                 uint32_t nexthop, uint32_t ifindex,
                                 uint32_t opcode, uint8_t *mac,
                                 uint32_t *p_nhid)
{
    uint8 unit;
    int idx, k;
    int32_t ret;
    uint32_t nhid = 0;
    uint8_t tunnel_num = 0;  /* tunnel 承载的外层标签数（0 或 1） */
    uint8_t push_num;        /* push 承载的内层标签数 */
    ctc_mpls_nexthop_param_t nh_param;
    ctc_mpls_nexthop_tunnel_param_t tunnel_param;
    ctc_mpls_nexthop_push_param_t *push;

    if (label_num == 0 || label_num > HW_MPLS_NH_LABEL_MAX)
        return RUNOS_PARAM;

    /* L3VPN 双层栈：外层 transport 走 tunnel，内层 VPN 走 push */
    if (opcode == CTC_MPLS_NH_PUSH_OP_ROUTE && label_num >= 2)
        tunnel_num = 1;
    push_num = label_num - tunnel_num;

    mutex_lock(&hw_mpls_nh_mutex);

    idx = hw_mpls_nh_find_key(labels, label_num, ifindex, opcode);
    if (idx >= 0) {
        g_hw_mpls_nh[idx].ref_count++;
        *p_nhid = g_hw_mpls_nh[idx].nhid;
        ret = RUNOS_OK;
        goto out;
    }

    sal_memset(&nh_param, 0, sizeof(nh_param));
    push = &nh_param.nh_para.nh_param_push;
    nh_param.nh_prop = CTC_MPLS_NH_PUSH_TYPE;
    nh_param.upd_type = CTC_NH_UPD_FWD_ATTR;
    push->nh_com.opcode = opcode;
    push->label_num = push_num;
    for (k = 0; k < push_num; k++) {
        /* SDK push_label[] 内层在前：labels 栈底（最内层）写入
         * push_label[0]，其余逆序；外层 transport 在 tunnel，不占
         * push_label。单标签时 k 循环等价于原实现的 push_label[0]。 */
        push->push_label[k].label = labels[label_num - 1 - k];
        /* 统一 TTL（SAI 默认）：把报文 TTL 映射到压入的标签
         * （SDK：标签 TTL = pkt_ttl - ttl），使 MPLS TTL 逐跳递减——
         * 固定 TTL 会破坏 MPLS 域内的 traceroute（与 ctc_sai_next_hop.c
         * 相同的构造）。SWAP（PUSH_OP_NONE）用 ttl=1 补偿外层 MPLS TTL，
         * 使新标签 TTL 与内层 IP TTL 一致；PUSH 直接映射 IP TTL
         * （ttl=0）。 */
        push->push_label[k].ttl =
            (push_num == 1 && opcode == CTC_MPLS_NH_PUSH_OP_NONE) ? 1 : 0;
        push->push_label[k].exp_type = CTC_NH_EXP_SELECT_PACKET;
        push->push_label[k].lable_flag |= CTC_MPLS_NH_LABEL_MAP_TTL;
        push->push_label[k].lable_flag |= CTC_MPLS_NH_LABEL_IS_VALID;
    }

    /* push 不携带 DMAC：外层 MAC 由 tunnel label nexthop 承载
     * （与 SAI ctc_sai_next_hop.c 一致）。SDK usw 中 push.nh_com.mac
     * 非零会被判为 inner l2 edit，普通 LSP push（label_num=1、
     * 非 SPME、非 loop）直接返回 CTC_E_INVALID_CONFIG=-31995。 */

    ret = hw_mpls_oif_fill(nexthop, ifindex, mac,
                           &push->nh_com.oif);
    if (ret != RUNOS_OK) {
        printk("hw_mpls: push nh oif_fill failed opcode=%u label=%u nexthop=0x%x ifindex=0x%x ret=%d\n",
               opcode, labels[0], nexthop, ifindex, ret);
        goto out;
    }

    FOREACH_UNIT(unit)
    {
        ret = hw_mpls_nh_alloc_id(unit, &nhid);
        if (ret != RUNOS_OK) {
            printk("hw_mpls: push nh alloc_id failed opcode=%u label=%u ifindex=0x%x ret=%d\n",
                   opcode, labels[0], ifindex, ret);
            goto out;
        }

        /* SDK 两步模型：push nexthop 必须引用一个已创建的 tunnel label
         * nexthop（tunnel_id == nhid）。tunnel 承载 oif / mac；仅 L3VPN
         * 双层栈时额外承载外层 transport 标签（tunnel 压最外层，
         * 与 VPWS 隧道标签用法一致）。单标签路径 tunnel.label_num=0，
         * 走 MAC fwd 模式（不影响单标签 LSP 语义）。 */
        sal_memset(&tunnel_param, 0, sizeof(tunnel_param));
        if (tunnel_num) {
            tunnel_param.nh_param.tunnel_label[0].label = labels[0];
            tunnel_param.nh_param.tunnel_label[0].ttl = 64;
            tunnel_param.nh_param.tunnel_label[0].exp_type =
                CTC_NH_EXP_SELECT_PACKET;
            tunnel_param.nh_param.tunnel_label[0].lable_flag |=
                CTC_MPLS_NH_LABEL_IS_VALID;
            tunnel_param.nh_param.label_num = 1;
        }
        tunnel_param.nh_param.oif = push->nh_com.oif;
        if (mac)
            memcpy(tunnel_param.nh_param.mac, mac, 6);
        ret = hw_nh_add_mpls_tunnel_label((uint16_t)nhid, &tunnel_param);
        if (ret != RUNOS_OK) {
            printk("hw_mpls: push nh tunnel label add failed nhid=%u opcode=%u label=%u ifindex=0x%x ret=%d\n",
                   nhid, opcode, labels[0], ifindex, ret);
            goto out;
        }

        push->tunnel_id = (uint16_t)nhid;
        ret = ctcs_nh_add_mpls(unit, nhid, &nh_param);
        if (ret != CTC_E_NONE) {
            /* push 创建失败：回滚刚建的 tunnel，避免 SDK 对象泄漏 */
            printk("hw_mpls: push nh add failed nhid=%u opcode=%u label=%u ifindex=0x%x ret=%d\n",
                   nhid, opcode, labels[0], ifindex, ret);
            hw_nh_remove_mpls_tunnel_label((uint16_t)nhid);
            goto out;
        }
    }

    for (idx = 0; idx < HW_MPLS_NH_MAX; idx++) {
        if (!g_hw_mpls_nh[idx].used)
            break;
    }
    if (idx >= HW_MPLS_NH_MAX) {
        ret = RUNOS_FULL;
        goto out;
    }

    g_hw_mpls_nh[idx].nhid = nhid;
    g_hw_mpls_nh[idx].out_label = labels[0];
    memcpy(g_hw_mpls_nh[idx].labels, labels, label_num * sizeof(uint32_t));
    g_hw_mpls_nh[idx].label_num = label_num;
    g_hw_mpls_nh[idx].nexthop = nexthop;
    g_hw_mpls_nh[idx].ifindex = ifindex;
    g_hw_mpls_nh[idx].opcode = opcode;
    g_hw_mpls_nh[idx].gport = push->nh_com.oif.gport;
    g_hw_mpls_nh[idx].ref_count = 1;
    if (mac)
        memcpy(g_hw_mpls_nh[idx].mac, mac, 6);
    g_hw_mpls_nh[idx].used = 1;

    *p_nhid = nhid;
    printk("hw_mpls: push nh created nhid=%u opcode=%u labels=%u[%u] ifindex=0x%x nexthop=0x%x\n",
           nhid, opcode, labels[0], label_num, ifindex, nexthop);
    ret = RUNOS_OK;
out:
    mutex_unlock(&hw_mpls_nh_mutex);
    return ret;
}

/* 单标签入口（普通 LSP SWAP / BGP-LU 单标签 route push） */
static int hw_mpls_nh_create(uint32_t out_label, uint32_t nexthop,
                             uint32_t ifindex, uint32_t opcode,
                             uint8_t *mac, uint32_t *p_nhid)
{
    uint32_t labels[1];

    labels[0] = out_label;
    return hw_mpls_nh_create_key(labels, 1, nexthop, ifindex, opcode,
                                 mac, p_nhid);
}

/* ------------------------------------------------------------------
 * PHP 出向 nexthop（弹掉本层标签后直接转发，见 g_hw_mpls_php_nh 注释）
 * ------------------------------------------------------------------ */

static int hw_mpls_php_nh_find(uint32_t nexthop, uint32_t ifindex)
{
    int i;

    for (i = 0; i < HW_MPLS_NH_MAX; i++) {
        if (g_hw_mpls_php_nh[i].used &&
            g_hw_mpls_php_nh[i].nexthop == nexthop &&
            g_hw_mpls_php_nh[i].ifindex == ifindex)
            return i;
    }
    return -1;
}

/* kind B：SDK 官方 MPLS POP(PHP) nexthop（CTC_MPLS_NH_POP_TYPE +
 * CTC_MPLS_NH_PHP，与厂商 CLI `nexthop add mpls NHID pop fwd
 * payload-op op-route ...` 同一构造，见 ctc_nexthop_cli.c）。
 * SDK 侧只建二层改写（l2edit），不带任何标签 edit——这正是"弹掉本层
 * 标签后转发"所需；标签的消耗由 ILM 命中本身完成（continuing=0）。
 *
 * ttl_mode：载荷还可能有 VPN 标签，不能用 uniform（SDK 侧
 * payload_operation=ROUTE 会要求载荷是 IP，触发 EPE_ROUTE_PLD_OP_DIS
 * "route payload operation no IP"），默认 short-pipe
 * （→ ROUTE_NOTTL + use_packet_ttl，TTL 只在 MPLS 域内处理）；
 * 路径显式带 TTL_PIPE 时按 pipe 下发。
 * mac_sa 保持 0：SDK 以 mac_sa 是否为零决定建 4W / 8W l2edit。 */
static int hw_mpls_php_nh_add_sdk_pop(uint32_t nhid,
                                      const ctc_nh_oif_info_t *oif,
                                      uint8_t *mac, uint32_t flags)
{
    uint8 unit;
    int32_t ret = RUNOS_OK;
    ctc_mpls_nexthop_param_t nh_param;
    ctc_mpls_nexthop_pop_param_t *pop;

    sal_memset(&nh_param, 0, sizeof(nh_param));
    nh_param.nh_prop = CTC_MPLS_NH_POP_TYPE;
    nh_param.upd_type = CTC_NH_UPD_FWD_ATTR;
    pop = &nh_param.nh_para.nh_param_pop;
    pop->nh_com.opcode = CTC_MPLS_NH_PHP;
    pop->ttl_mode = (flags & HAL_MPLS_FLAG_TTL_PIPE) ?
                    CTC_MPLS_TUNNEL_MODE_PIPE :
                    CTC_MPLS_TUNNEL_MODE_SHORT_PIPE;
    pop->nh_com.oif = *oif;
    if (mac)
        memcpy(pop->nh_com.mac, mac, RUNOS_MAC_LEN);

    FOREACH_UNIT(unit)
    {
        ret = ctcs_nh_add_mpls(unit, nhid, &nh_param);
        if (ret != CTC_E_NONE) {
            /* 部分 unit 成功时回滚，避免 SDK 对象泄漏 */
            uint8 u;

            FOREACH_UNIT(u)
            {
                ctcs_nh_remove_mpls(u, nhid);
            }
            break;
        }
    }
    return (ret == CTC_E_NONE) ? RUNOS_OK : ret;
}

/* kind A（回退）：空标签链的 MPLS push nexthop——与单标签 SWAP 路径
 * 完全同构（tunnel 走 MAC fwd 模式），只是 push 压 0 个标签。 */
static int hw_mpls_php_nh_add_empty_push(uint32_t nhid,
                                         const ctc_nh_oif_info_t *oif,
                                         uint8_t *mac)
{
    uint8 unit;
    int32_t ret = RUNOS_OK;
    ctc_mpls_nexthop_param_t nh_param;
    ctc_mpls_nexthop_tunnel_param_t tunnel_param;
    ctc_mpls_nexthop_push_param_t *push;

    sal_memset(&nh_param, 0, sizeof(nh_param));
    push = &nh_param.nh_para.nh_param_push;
    nh_param.nh_prop = CTC_MPLS_NH_PUSH_TYPE;
    nh_param.upd_type = CTC_NH_UPD_FWD_ATTR;
    push->nh_com.opcode = CTC_MPLS_NH_PUSH_OP_NONE;
    push->label_num = 0;               /* 不压标签：标签栈原样出向 */
    /* push 不携带 DMAC（会被判为 inner l2 edit，见 hw_mpls_nh_create_key
     * 注释）；外层 MAC 由 tunnel label 承载 */
    push->nh_com.oif = *oif;

    sal_memset(&tunnel_param, 0, sizeof(tunnel_param));
    tunnel_param.nh_param.label_num = 0;  /* MAC fwd 模式：只承载 oif/DMAC */
    tunnel_param.nh_param.oif = *oif;
    if (mac)
        memcpy(tunnel_param.nh_param.mac, mac, RUNOS_MAC_LEN);

    FOREACH_UNIT(unit)
    {
        /* 两步模型（与 hw_mpls_nh_create_key 一致）：先建 tunnel label
         * 对象（tunnel_id == nhid），再建引用它的 push nexthop */
        ret = hw_nh_add_mpls_tunnel_label((uint16_t)nhid, &tunnel_param);
        if (ret != RUNOS_OK)
            break;
        push->tunnel_id = (uint16_t)nhid;
        ret = ctcs_nh_add_mpls(unit, nhid, &nh_param);
        if (ret != CTC_E_NONE) {
            hw_nh_remove_mpls_tunnel_label((uint16_t)nhid);
            break;
        }
    }
    return (ret == CTC_E_NONE) ? RUNOS_OK : ret;
}

/* 创建（或复用）PHP 出向 nexthop 并持有一个引用。整段在
 * hw_mpls_nh_mutex 下原子执行（alloc_id -> SDK 创建 -> 写缓存），与
 * hw_mpls_nh_create_key 一致。kind B 失败时回退 kind A；两者都失败才
 * 报错（调用方把该路径当失败回滚）。 */
static int hw_mpls_nh_create_pop(uint32_t nexthop, uint32_t ifindex,
                                 uint32_t flags, uint8_t *mac,
                                 uint32_t *p_nhid)
{
    uint8 unit;
    int idx;
    int ret = RUNOS_OK;
    uint32_t nhid = 0;
    uint8_t kind = HW_MPLS_PHP_NH_KIND_SDK_POP;
    ctc_nh_oif_info_t oif;

    mutex_lock(&hw_mpls_nh_mutex);

    idx = hw_mpls_php_nh_find(nexthop, ifindex);
    if (idx >= 0) {
        g_hw_mpls_php_nh[idx].ref_count++;
        *p_nhid = g_hw_mpls_php_nh[idx].nhid;
        goto out;
    }

    ret = hw_mpls_oif_fill(nexthop, ifindex, mac, &oif);
    if (ret != RUNOS_OK) {
        printk("hw_mpls: PHP nh oif_fill failed ifindex=0x%x nexthop=0x%x ret=%d\n",
               ifindex, nexthop, ret);
        goto out;
    }

    /* 先占好缓存槽：SDK 对象创建成功后再发现无处记录会泄漏芯片 nexthop
     * （nhid 也已分配）。放在创建之前，失败路径无副作用。 */
    for (idx = 0; idx < HW_MPLS_NH_MAX; idx++) {
        if (!g_hw_mpls_php_nh[idx].used)
            break;
    }
    if (idx >= HW_MPLS_NH_MAX) {
        printk("hw_mpls: PHP nh cache full, ifindex=0x%x nexthop=0x%x\n",
               ifindex, nexthop);
        ret = RUNOS_FULL;
        goto out;
    }

    FOREACH_UNIT(unit)
    {
        ret = hw_mpls_nh_alloc_id(unit, &nhid);
        if (ret != RUNOS_OK) {
            printk("hw_mpls: PHP nh alloc_id failed ifindex=0x%x nexthop=0x%x ret=%d\n",
                   ifindex, nexthop, ret);
            goto out;
        }
        break; /* 单个 nhid 供所有 unit 使用（与 push 路径一致） */
    }

    ret = hw_mpls_php_nh_add_sdk_pop(nhid, &oif, mac, flags);
    if (ret != RUNOS_OK) {
        printk("hw_mpls: PHP nh SDK pop create failed nhid=%u ifindex=0x%x nexthop=0x%x ret=%d, fallback to empty-label push\n",
               nhid, ifindex, nexthop, ret);
        kind = HW_MPLS_PHP_NH_KIND_EMPTY_PUSH;
        ret = hw_mpls_php_nh_add_empty_push(nhid, &oif, mac);
        if (ret != RUNOS_OK) {
            printk("hw_mpls: PHP nh empty-push create failed nhid=%u ifindex=0x%x ret=%d\n",
                   nhid, ifindex, ret);
            goto out;
        }
    }

    g_hw_mpls_php_nh[idx].nhid = nhid;
    g_hw_mpls_php_nh[idx].nexthop = nexthop;
    g_hw_mpls_php_nh[idx].ifindex = ifindex;
    g_hw_mpls_php_nh[idx].flags = flags;
    g_hw_mpls_php_nh[idx].gport = oif.gport;
    g_hw_mpls_php_nh[idx].ref_count = 1;
    g_hw_mpls_php_nh[idx].kind = kind;
    if (mac)
        memcpy(g_hw_mpls_php_nh[idx].mac, mac, RUNOS_MAC_LEN);
    g_hw_mpls_php_nh[idx].used = 1;

    *p_nhid = nhid;
    printk("hw_mpls: PHP nh created nhid=%u kind=%u ifindex=0x%x nexthop=0x%x gport=0x%x mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
           nhid, kind, ifindex, nexthop, oif.gport,
           mac ? mac[0] : 0, mac ? mac[1] : 0, mac ? mac[2] : 0,
           mac ? mac[3] : 0, mac ? mac[4] : 0, mac ? mac[5] : 0);
    ret = RUNOS_OK;
out:
    mutex_unlock(&hw_mpls_nh_mutex);
    return ret;
}

/* 释放一个引用；引用计数归零时删除 SDK nexthop。
 * 调用方必须已持有 hw_mpls_nh_mutex（供嵌套调用如 ecmp_release 使用）。 */
static int hw_mpls_nh_release_locked(const uint32_t *labels, uint8_t label_num,
                                     uint32_t ifindex, uint32_t opcode)
{
    uint8 unit;
    int idx;

    idx = hw_mpls_nh_find_key(labels, label_num, ifindex, opcode);
    if (idx < 0)
        return RUNOS_OK;

    if (g_hw_mpls_nh[idx].ref_count > 0)
        g_hw_mpls_nh[idx].ref_count--;

    if (g_hw_mpls_nh[idx].ref_count > 0)
        return RUNOS_OK;

    FOREACH_UNIT(unit)
    {
        ctcs_nh_remove_mpls(unit, g_hw_mpls_nh[idx].nhid);
    }
    /* push 引用已释放（SDK ref_cnt 归零），删除 tunnel label 对象 */
    hw_nh_remove_mpls_tunnel_label((uint16_t)g_hw_mpls_nh[idx].nhid);

    sal_memset(&g_hw_mpls_nh[idx], 0, sizeof(g_hw_mpls_nh[idx]));
    return RUNOS_OK;
}

/* 释放一个引用（加锁包装，单标签） */
static int hw_mpls_nh_release(uint32_t out_label, uint32_t ifindex,
                              uint32_t opcode)
{
    uint32_t labels[1];
    int ret;

    labels[0] = out_label;
    mutex_lock(&hw_mpls_nh_mutex);
    ret = hw_mpls_nh_release_locked(labels, 1, ifindex, opcode);
    mutex_unlock(&hw_mpls_nh_mutex);
    return ret;
}

/* 释放一个引用（加锁包装，标签栈；供 route push_stack 删除与 ECMP
 * 成员释放按缓存条目完整键释放） */
static int hw_mpls_nh_release_key(const uint32_t *labels, uint8_t label_num,
                                  uint32_t ifindex, uint32_t opcode)
{
    int ret;

    if (label_num == 0 || label_num > HW_MPLS_NH_LABEL_MAX)
        return RUNOS_PARAM;
    mutex_lock(&hw_mpls_nh_mutex);
    ret = hw_mpls_nh_release_locked(labels, label_num, ifindex, opcode);
    mutex_unlock(&hw_mpls_nh_mutex);
    return ret;
}

/* 按 nhid 释放一个"由本模块创建并持有引用"的 nexthop（push 或 PHP）。
 *
 * ILM / ECMP 缓存里存的是 nhid，删除路径（hw_mpls_ilm_release_nh /
 * hw_mpls_arp_del / hw_mpls_ecmp_release / hw_mpls_vrf_ilm_release_nh）
 * 只知道 nhid 而不知道该去哪个缓存取完整的 key，因此必须能按 id 反查。
 * 返回 RUNOS_NOT_FOUND 表示该 nhid 不是本模块创建的（例如 VRF ILM 的
 * POP 路径复用的 IPUC nexthop），调用方忽略即可。
 * 调用方必须已持有 hw_mpls_nh_mutex。 */
static int hw_mpls_nh_release_by_id_locked(uint32_t nhid)
{
    uint8 unit;
    int i;

    for (i = 0; i < HW_MPLS_NH_MAX; i++) {
        if (g_hw_mpls_nh[i].used && g_hw_mpls_nh[i].nhid == nhid)
            return hw_mpls_nh_release_locked(g_hw_mpls_nh[i].labels,
                                             g_hw_mpls_nh[i].label_num,
                                             g_hw_mpls_nh[i].ifindex,
                                             g_hw_mpls_nh[i].opcode);
    }

    for (i = 0; i < HW_MPLS_NH_MAX; i++) {
        if (!g_hw_mpls_php_nh[i].used || g_hw_mpls_php_nh[i].nhid != nhid)
            continue;

        if (g_hw_mpls_php_nh[i].ref_count > 0)
            g_hw_mpls_php_nh[i].ref_count--;
        if (g_hw_mpls_php_nh[i].ref_count > 0)
            return RUNOS_OK;

        FOREACH_UNIT(unit)
        {
            ctcs_nh_remove_mpls(unit, nhid);
        }
        /* kind A 额外创建了 tunnel label 对象（tunnel_id == nhid） */
        if (g_hw_mpls_php_nh[i].kind == HW_MPLS_PHP_NH_KIND_EMPTY_PUSH)
            hw_nh_remove_mpls_tunnel_label((uint16_t)nhid);

        printk("hw_mpls: PHP nh removed nhid=%u kind=%u ifindex=0x%x nexthop=0x%x\n",
               nhid, g_hw_mpls_php_nh[i].kind,
               g_hw_mpls_php_nh[i].ifindex, g_hw_mpls_php_nh[i].nexthop);
        sal_memset(&g_hw_mpls_php_nh[i], 0, sizeof(g_hw_mpls_php_nh[i]));
        return RUNOS_OK;
    }
    return RUNOS_NOT_FOUND;
}

/* 按 nhid 释放一个引用（加锁包装） */
static int hw_mpls_nh_release_by_id(uint32_t nhid)
{
    int ret;

    mutex_lock(&hw_mpls_nh_mutex);
    ret = hw_mpls_nh_release_by_id_locked(nhid);
    mutex_unlock(&hw_mpls_nh_mutex);
    return ret;
}

/* ARP 刷新钩子：PHP 出向 nexthop 的 oif/DMAC 快照更新。
 *
 * PHP nexthop 的 key 是 (nexthop, ifindex)，nhid 不随 ARP 变化——ARP
 * 学习到新 MAC/出端口时只需刷新它承载的二层改写（kind B 用
 * hw_nh_update_mpls 重新下发 POP 参数，kind A 更新 tunnel label 的
 * oif/DMAC）。与 hw_mpls_nh_tunnel_refresh 同样在快照无变化时零芯片写。
 * 调用上下文：hw_mpls_ilm_refresh（g_hw_mpls_ilm 无锁串行访问）。 */
static int hw_mpls_nh_pop_refresh(uint32_t nexthop, uint32_t ifindex)
{
    int idx;
    int ret = RUNOS_OK;
    uint8_t mac[6];
    ctc_nh_oif_info_t oif;
    ctc_mpls_nexthop_param_t nh_param;
    ctc_mpls_nexthop_tunnel_param_t tunnel_param;
    ctc_mpls_nexthop_pop_param_t *pop;

    mutex_lock(&hw_mpls_nh_mutex);

    idx = hw_mpls_php_nh_find(nexthop, ifindex);
    if (idx < 0) {
        /* nexthop 已被释放（ILM 删除与 ARP 事件竞态）：无需刷新 */
        goto out;
    }

    if (hw_mpls_nh_lookup(nexthop, ifindex, NULL, mac) != RUNOS_OK) {
        ret = RUNOS_NOT_FOUND;
        goto out;
    }
    if (hw_mpls_oif_fill(nexthop, ifindex, mac, &oif) != RUNOS_OK) {
        ret = RUNOS_PORT;
        goto out;
    }
    if (g_hw_mpls_php_nh[idx].gport == oif.gport &&
        !memcmp(g_hw_mpls_php_nh[idx].mac, mac, RUNOS_MAC_LEN))
        goto out; /* 无变化：ARP 周期刷新 */

    if (g_hw_mpls_php_nh[idx].kind == HW_MPLS_PHP_NH_KIND_SDK_POP) {
        sal_memset(&nh_param, 0, sizeof(nh_param));
        nh_param.nh_prop = CTC_MPLS_NH_POP_TYPE;
        nh_param.upd_type = CTC_NH_UPD_FWD_ATTR;
        pop = &nh_param.nh_para.nh_param_pop;
        pop->nh_com.opcode = CTC_MPLS_NH_PHP;
        pop->ttl_mode = (g_hw_mpls_php_nh[idx].flags & HAL_MPLS_FLAG_TTL_PIPE) ?
                        CTC_MPLS_TUNNEL_MODE_PIPE :
                        CTC_MPLS_TUNNEL_MODE_SHORT_PIPE;
        pop->nh_com.oif = oif;
        memcpy(pop->nh_com.mac, mac, RUNOS_MAC_LEN);
        ret = hw_nh_update_mpls(g_hw_mpls_php_nh[idx].nhid, &nh_param);
    } else {
        sal_memset(&tunnel_param, 0, sizeof(tunnel_param));
        tunnel_param.nh_param.label_num = 0;
        tunnel_param.nh_param.oif = oif;
        memcpy(tunnel_param.nh_param.mac, mac, RUNOS_MAC_LEN);
        ret = hw_nh_update_mpls_tunnel_label(
            (uint16_t)g_hw_mpls_php_nh[idx].nhid, &tunnel_param);
    }
    if (ret != RUNOS_OK) {
        printk("hw_mpls: PHP nhid=%u ifindex=0x%x nexthop=0x%x refresh failed ret=%d\n",
               g_hw_mpls_php_nh[idx].nhid, ifindex, nexthop, ret);
        goto out;
    }

    printk("hw_mpls: PHP nhid=%u refreshed gport 0x%x->0x%x mac %02x:%02x:%02x:%02x:%02x:%02x->%02x:%02x:%02x:%02x:%02x:%02x\n",
           g_hw_mpls_php_nh[idx].nhid,
           g_hw_mpls_php_nh[idx].gport, oif.gport,
           g_hw_mpls_php_nh[idx].mac[0], g_hw_mpls_php_nh[idx].mac[1],
           g_hw_mpls_php_nh[idx].mac[2], g_hw_mpls_php_nh[idx].mac[3],
           g_hw_mpls_php_nh[idx].mac[4], g_hw_mpls_php_nh[idx].mac[5],
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    g_hw_mpls_php_nh[idx].gport = oif.gport;
    memcpy(g_hw_mpls_php_nh[idx].mac, mac, RUNOS_MAC_LEN);
    ret = RUNOS_OK;
out:
    mutex_unlock(&hw_mpls_nh_mutex);
    return ret;
}

/* ARP 刷新钩子：SWAP push nexthop 的 tunnel 快照更新。
 *
 * push 缓存 key = (out_label, ifindex, opcode)，不含 MAC / gport；
 * ARP MAC 变化时 hw_mpls_nh_create 会复用旧 push（返回旧 nhid），
 * tunnel 里承载的 oif.gport + DMAC 快照因此停留在旧值，芯片转发
 * 的帧头 DMAC 错误。这里重新解析最新 MAC 与出端口
 * （hw_mpls_oif_fill 内部查 HSL nh 表——链表头即最近一次 ARP
 * 学习/刷新结果，见 hsl_nh_add_rt_info），与缓存快照比较，有变化
 * 才调用 hw_nh_update_mpls_tunnel_label 下发。
 *
 * 同 MAC 的周期 ARP 刷新（内容没变）不触发芯片写。ARP 又消失时
 * 返回 RUNOS_NOT_FOUND，调用方保留旧快照（回退占位由
 * hw_mpls_arp_del 负责）。
 * 调用上下文：hw_mpls_arp_resolved（hsl_nh_add 释放锁后串行调用）。 */
static int hw_mpls_nh_tunnel_refresh(uint32_t out_label, uint32_t nexthop,
                                     uint32_t ifindex)
{
    int idx;
    int ret = RUNOS_OK;
    uint8_t mac[6];
    ctc_nh_oif_info_t oif;
    ctc_mpls_nexthop_tunnel_param_t tunnel_param;

    mutex_lock(&hw_mpls_nh_mutex);

    idx = hw_mpls_nh_find(out_label, ifindex, CTC_MPLS_NH_PUSH_OP_NONE);
    if (idx < 0) {
        /* push 已被释放（ILM 删除与 ARP 事件竞态）：无需刷新 */
        goto out;
    }

    if (hw_mpls_nh_lookup(nexthop, ifindex, NULL, mac) != RUNOS_OK) {
        ret = RUNOS_NOT_FOUND;
        goto out;
    }
    if (hw_mpls_oif_fill(nexthop, ifindex, mac, &oif) != RUNOS_OK) {
        ret = RUNOS_PORT;
        goto out;
    }

    if (g_hw_mpls_nh[idx].gport == oif.gport &&
        !memcmp(g_hw_mpls_nh[idx].mac, mac, RUNOS_MAC_LEN))
        goto out; /* 无变化：ARP 周期刷新 */

    sal_memset(&tunnel_param, 0, sizeof(tunnel_param));
    tunnel_param.nh_param.oif = oif;
    memcpy(tunnel_param.nh_param.mac, mac, RUNOS_MAC_LEN);
    ret = hw_nh_update_mpls_tunnel_label(
        (uint16_t)g_hw_mpls_nh[idx].nhid, &tunnel_param);
    if (ret != RUNOS_OK) {
        printk("hw_mpls: nhid=%u label=%u tunnel refresh failed gport=0x%x mac=%02x:%02x:%02x:%02x:%02x:%02x ret=%d\n",
               g_hw_mpls_nh[idx].nhid, out_label, oif.gport,
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ret);
        goto out;
    }

    printk("hw_mpls: nhid=%u label=%u tunnel refreshed gport 0x%x->0x%x mac %02x:%02x:%02x:%02x:%02x:%02x->%02x:%02x:%02x:%02x:%02x:%02x\n",
           g_hw_mpls_nh[idx].nhid, out_label,
           g_hw_mpls_nh[idx].gport, oif.gport,
           g_hw_mpls_nh[idx].mac[0], g_hw_mpls_nh[idx].mac[1],
           g_hw_mpls_nh[idx].mac[2], g_hw_mpls_nh[idx].mac[3],
           g_hw_mpls_nh[idx].mac[4], g_hw_mpls_nh[idx].mac[5],
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    g_hw_mpls_nh[idx].gport = oif.gport;
    memcpy(g_hw_mpls_nh[idx].mac, mac, RUNOS_MAC_LEN);
    ret = RUNOS_OK;
out:
    mutex_unlock(&hw_mpls_nh_mutex);
    return ret;
}

/* ------------------------------------------------------------------
 * ECMP 组缓存
 *
 * 多路径 ILM 绑定到单个 ECMP 组 nexthop，其成员是各路径的 MPLS
 * push nexthop（SWAP）或 IP nexthop（POP）。组带引用计数：首次使用
 * 时创建，最后一个引用它的 ILM 删除时才删除（同时释放对每个
 * MPLS push 成员持有的引用）。
 * ------------------------------------------------------------------ */

static int hw_mpls_ecmp_find(const uint32_t *members, uint8_t num)
{
    int i, j;

    for (i = 0; i < HW_MPLS_ECMP_MAX; i++) {
        if (!g_hw_mpls_ecmp[i].used || g_hw_mpls_ecmp[i].member_num != num)
            continue;
        for (j = 0; j < num; j++) {
            if (g_hw_mpls_ecmp[i].members[j] != members[j])
                break;
        }
        if (j == num)
            return i;
    }
    return -1;
}

/* 供 hw_l3.c（IPUC 邻接分配）检查 nhid 是否已被 MPLS push / ECMP 组
 * 占用：ctcs_nh_get_nh_info 查不到 MPLS 类型 nexthop，IPUC 分配器不
 * 加此检查会重复分配同一 nhid 覆盖 push。L3VPN 双层栈 push 也存入
 * g_hw_mpls_nh，一并被覆盖。尽力检查（调用方持 hw_l3_vrf_mutex，
 * 此处不加 hw_mpls_nh_mutex 避免锁序耦合）。 */
int hw_mpls_nh_id_in_use(uint32_t nhid)
{
    int i;

    for (i = 0; i < HW_MPLS_NH_MAX; i++) {
        if (g_hw_mpls_nh[i].used && g_hw_mpls_nh[i].nhid == nhid)
            return 1;
    }
    for (i = 0; i < HW_MPLS_ECMP_MAX; i++) {
        if (g_hw_mpls_ecmp[i].used && g_hw_mpls_ecmp[i].nhid == nhid)
            return 1;
    }
    for (i = 0; i < HW_MPLS_NH_MAX; i++) {
        if (g_hw_mpls_php_nh[i].used && g_hw_mpls_php_nh[i].nhid == nhid)
            return 1;
    }
    return 0;
}

/* 创建（或复用）ECMP 组 nexthop。与 hw_mpls_nh_create 一样在
 * hw_mpls_nh_mutex 保护下执行：ECMP 组的 nhid 也从 3..4096 区间
 * 分配，不加锁会与 push nexthop 的分配互相覆盖。
 * 非 static：hw_l3.c 的 VRF 路由多路径（IPUC + push 混合成员）复用。 */
int hw_mpls_ecmp_create(const uint32_t *members,
                        const uint8_t *member_is_push, uint8_t num,
                        uint32_t *p_nhid)
{
    uint8 unit;
    int idx;
    int32_t ret;
    uint32_t nhid = 0;
    ctc_nh_ecmp_nh_param_t ecmp_param;

    mutex_lock(&hw_mpls_nh_mutex);

    idx = hw_mpls_ecmp_find(members, num);
    if (idx >= 0) {
        g_hw_mpls_ecmp[idx].ref_count++;
        *p_nhid = g_hw_mpls_ecmp[idx].nhid;
        ret = RUNOS_OK;
        goto out;
    }

    sal_memset(&ecmp_param, 0, sizeof(ecmp_param));
    ecmp_param.type = CTC_NH_ECMP_TYPE_STATIC;
    ecmp_param.nh_num = num;
    memcpy(ecmp_param.nhid, members, num * sizeof(uint32_t));

    FOREACH_UNIT(unit)
    {
        ret = hw_mpls_nh_alloc_id(unit, &nhid);
        if (ret != RUNOS_OK)
            goto out;
        ret = ctcs_nh_add_ecmp(unit, nhid, &ecmp_param);
        if (ret != CTC_E_NONE)
            goto out;
    }

    for (idx = 0; idx < HW_MPLS_ECMP_MAX; idx++) {
        if (!g_hw_mpls_ecmp[idx].used)
            break;
    }
    if (idx >= HW_MPLS_ECMP_MAX) {
        ret = RUNOS_FULL;
        goto out;
    }

    memcpy(g_hw_mpls_ecmp[idx].members, members, num * sizeof(uint32_t));
    memcpy(g_hw_mpls_ecmp[idx].member_is_push, member_is_push, num);
    g_hw_mpls_ecmp[idx].member_num = num;
    g_hw_mpls_ecmp[idx].nhid = nhid;
    g_hw_mpls_ecmp[idx].ref_count = 1;
    g_hw_mpls_ecmp[idx].used = 1;

    *p_nhid = nhid;
    ret = RUNOS_OK;
out:
    mutex_unlock(&hw_mpls_nh_mutex);
    return ret;
}

/* 释放 ECMP 组的一个引用；最后一个引用消失时删除组
 * （以及它对每个 MPLS push 成员持有的引用）。
 * 持锁调用：成员释放用 release_locked 避免嵌套加锁。
 * 非 static：hw_l3.c 的 VRF 路由多路径（IPUC + push 混合成员）复用。 */
void hw_mpls_ecmp_release(uint32_t nhid)
{
    uint8 unit;
    int i, j;

    mutex_lock(&hw_mpls_nh_mutex);

    for (i = 0; i < HW_MPLS_ECMP_MAX; i++) {
        if (!g_hw_mpls_ecmp[i].used || g_hw_mpls_ecmp[i].nhid != nhid)
            continue;

        if (g_hw_mpls_ecmp[i].ref_count > 0)
            g_hw_mpls_ecmp[i].ref_count--;
        if (g_hw_mpls_ecmp[i].ref_count > 0)
            goto out;

        FOREACH_UNIT(unit)
        {
            ctcs_nh_remove_ecmp(unit, nhid);
        }

        /* 释放对每个自建成员 nexthop 持有的引用（成员可能是单标签 LSP /
         * BGP-LU / L3VPN 双层栈的 push，或普通 LSP POP/PHP 的 PHP
         * nexthop；按 nhid 反查缓存释放，两种都可）。member_is_push=0 的
         * 是外部（IPUC/push）nexthop，不归本模块释放。 */
        for (j = 0; j < g_hw_mpls_ecmp[i].member_num; j++) {
            if (!g_hw_mpls_ecmp[i].member_is_push[j])
                continue;
            hw_mpls_nh_release_by_id_locked(g_hw_mpls_ecmp[i].members[j]);
        }
        sal_memset(&g_hw_mpls_ecmp[i], 0, sizeof(g_hw_mpls_ecmp[i]));
        break;
    }
out:
    mutex_unlock(&hw_mpls_nh_mutex);
}

/* ILM 表项的 in_label -> nhid 映射辅助函数 */
static int hw_mpls_ilm_map_set(uint32_t in_label, uint32_t nhid,
                               uint8_t is_ecmp, uint8_t placeholder,
                               uint32_t nh_count,
                               const struct hw_mpls_ilm_nh *nhs)
{
    int i;
    int free_slot = -1;
    struct hw_mpls_ilm_cache *c = NULL;

    for (i = 0; i < HW_MPLS_ILM_MAX; i++) {
        if (g_hw_mpls_ilm[i].used && g_hw_mpls_ilm[i].in_label == in_label) {
            c = &g_hw_mpls_ilm[i];
            break;
        }
        if (!g_hw_mpls_ilm[i].used && free_slot < 0)
            free_slot = i;
    }
    if (c == NULL) {
        if (free_slot < 0)
            return RUNOS_FULL;
        c = &g_hw_mpls_ilm[free_slot];
        c->in_label = in_label;
        c->used = 1;
    }

    c->nhid = nhid;
    c->is_ecmp = is_ecmp;
    c->placeholder = placeholder;
    /* ph_nhs 无条件保留（占位与非占位）：ARP 删除
     * （hw_mpls_arp_del）回退占位时需要原始路径重建 TOCPU 表项；
     * 升级后不再清空（原实现在 hw_mpls_arp_resolved 升级成功后
     * 也清空了 ph_nhs，导致 ARP 删除时无法回退）。 */
    c->ph_count = 0;
    if (nhs && nh_count > 0) {
        uint32_t n = nh_count < HW_MPLS_ECMP_MEMBER_MAX ?
                     nh_count : HW_MPLS_ECMP_MEMBER_MAX;
        memcpy(c->ph_nhs, nhs, n * sizeof(struct hw_mpls_ilm_nh));
        c->ph_count = (uint8_t)n;
    }
    return RUNOS_OK;
}

static int hw_mpls_ilm_map_get(uint32_t in_label, uint32_t *p_nhid)
{
    int i;

    for (i = 0; i < HW_MPLS_ILM_MAX; i++) {
        if (g_hw_mpls_ilm[i].used && g_hw_mpls_ilm[i].in_label == in_label) {
            *p_nhid = g_hw_mpls_ilm[i].nhid;
            return RUNOS_OK;
        }
    }
    return RUNOS_NOT_FOUND;
}

static void hw_mpls_ilm_map_del(uint32_t in_label)
{
    int i;

    for (i = 0; i < HW_MPLS_ILM_MAX; i++) {
        if (g_hw_mpls_ilm[i].used && g_hw_mpls_ilm[i].in_label == in_label) {
            sal_memset(&g_hw_mpls_ilm[i], 0, sizeof(g_hw_mpls_ilm[i]));
            return;
        }
    }
}

/* 释放绑定在 ILM 上的 NH（del/update 使用） */
static void hw_mpls_ilm_release_nh(uint32_t in_label)
{
    uint32_t nhid = 0;
    int i;

    if (hw_mpls_ilm_map_get(in_label, &nhid) != RUNOS_OK)
        return;

    for (i = 0; i < HW_MPLS_ILM_MAX; i++) {
        if (!g_hw_mpls_ilm[i].used || g_hw_mpls_ilm[i].in_label != in_label)
            continue;
        if (g_hw_mpls_ilm[i].placeholder) {
            /* ARP 占位：没有创建 push NH / ECMP 组 */
            break;
        }
        if (g_hw_mpls_ilm[i].is_ecmp) {
            hw_mpls_ecmp_release(nhid);
        } else {
            /* 按 nhid 释放：可能是 push（SWAP）或 PHP（POP/PHP）nexthop，
             * 也可能是 VRF ILM 那条路径复用的 IPUC nexthop
             * （release_by_id 返回 NOT_FOUND，忽略） */
            hw_mpls_nh_release_by_id(nhid);
        }
        break;
    }
    hw_mpls_ilm_map_del(in_label);
}

/**
添加 ACL 规则：匹配 LDP 组播报文并上送 CPU
unit 芯片单元号
group_id ACL 组 ID
entry_id 规则 ID
0 成功，负数失败
 */
int add_ldp_multicast_acl(uint32 unit, uint32 group_id, uint32 entry_id)
{
    int ret = 0;
    ctc_acl_entry_t acl_entry;
    ctc_acl_ipv4_key_t* p_key = NULL;
    ctc_acl_action_t* p_action = NULL;

    if (group_id == 0 || entry_id == 0) {
        return -1;
    }

    memset(&acl_entry, 0, sizeof(ctc_acl_entry_t));

   //设置 Key 类型为 IPv4
    acl_entry.key.type = CTC_ACL_KEY_IPV4;
    acl_entry.entry_id = entry_id;
    p_key = &acl_entry.key.u.ipv4_key;
    p_action = &acl_entry.action;

    // 匹配目的 IP = 224.0.0.2（LDP 组播地址）
    p_key->flag = CTC_ACL_IPV4_KEY_FLAG_IP_DA |
                  CTC_ACL_IPV4_KEY_FLAG_L4_PROTOCOL;
    p_key->ip_da = 0xE0000002;          /* 224.0.0.2 */
    p_key->ip_da_mask = 0xFFFFFFFF;     /* 255.255.255.255 */

    /* 匹配协议 = UDP（17） */
    p_key->l4_protocol = 17;            /* UDP */
   p_key->l4_protocol_mask = 0xFF;

    //p_key->sub_flag = CTC_ACL_IPV4_KEY_SUB_FLAG_L4_DST_PORT;
    // p_key->l4_dst_port_0 = 646;          /* LDP 目的端口 */
    // p_key->l4_dst_port_1 = 646;          /* LDP 目的端口（范围/掩码模式） */
    // p_key->l4_dst_port_use_mask = 0;     /* 使用范围匹配（非掩码） */

    //3. 设置动作（Action）

    //清空 flag
    p_action->flag = 0;

    // 设置动作：copy-to-cpu
    SET_FLAG(p_action->flag, 1U << 12);

    ret = ctcs_acl_add_entry(unit, group_id, &acl_entry);
    if (ret < 0) {
        printk("Failed to add ACL entry %d ,ret = %d\n", entry_id,ret);
        return ret;
    }
    return 0;
}

/* ------------------------------------------------------------------
 * 初始化 / 反初始化
 * ------------------------------------------------------------------ */

void hw_mpls_init(void)
{
    uint8 unit;
    ctc_mpls_init_t initData = {};
    int ret = 0;

    FOREACH_UNIT(unit)
    {
        ctcs_mpls_init(unit, &initData);

        // 添加 Entry 100：LDP 组播上送 CPU
        ret = add_ldp_multicast_acl(unit, 1, 100);
        if (ret < 0) {
            printk("Failed to add LDP multicast ACL\n");
        }

        ctcs_acl_install_entry(unit,100);


    }
}

void hw_mpls_deinit(void)
{
    uint8 unit;

    FOREACH_UNIT(unit)
    {
        ctcs_mpls_deinit(unit);
    }
}

/* ------------------------------------------------------------------
 * ILM（入标签映射）
 * ------------------------------------------------------------------ */

/* 解析 (ifindex, /32 下一跳) 对应的 HSL nexthop，返回其硬件 nhid
 * 和 MAC。
 *
 * POP ILM 表项必须引用有效的 IP nexthop：nh_id == 0 时 SDK 会安装
 * 保留的丢弃 nexthop，静默丢包。这里复用路由/ARP 模块已经创建的
 * IPUC nexthop（hw_l3_add_nh）——不创建新 nexthop 也不持有引用，
 * 因此这里没有生命周期管理。
 *
 * SWAP ILM 表项用同样的查询取到 MPLS push nexthop 的外层 DMAC：
 * 没有它芯片会把帧重写成全零目的 MAC，转发失败。
 *
 * 下一跳 MAC 尚未学习到时返回 RUNOS_NOT_FOUND；调用方随后把 ILM
 * 安装为绑定到保留 TOCPU nexthop 的 ARP 占位，等 ARP 解析后升级
 * （hw_mpls_arp_resolved）。 */
static int hw_mpls_nh_lookup(uint32_t nexthop, uint32_t ifindex,
                             uint32_t *p_nhid, uint8_t *mac)
{
    hsl_prefix_t prefix;
    hsl_nh_entry_t *nh;

    if (!nexthop)
        return RUNOS_PARAM;

    sal_memset(&prefix, 0, sizeof(prefix));
    prefix.family = AF_INET;
    prefix.prefixlen = 32;
    prefix.u.prefix4 = htonl(nexthop); /* HSL nh 表按网络序保存 */

    nh = hsl_rt_lookup_nh__(ifindex, &prefix);
    if (nh == NULL || !(nh->flags & HSL_NH_ENTRY_IN_HW) || nh->id == 0)
        return RUNOS_NOT_FOUND;

    if (p_nhid)
        *p_nhid = nh->id;
    if (mac)
        memcpy(mac, nh->mac, RUNOS_MAC_LEN);
    return RUNOS_OK;
}

/* 为 ILM 表项构建 nexthop(s)：每个 NHLFE 一条路径，全部绑定到
 * 单个 nhid（多路径时是 ECMP 组）。
 *
 * 三类路径的出向 nexthop：
 *  - SWAP：从 HSL nexthop 表解析外层 DMAC，创建 push nexthop（芯片
 *    换标签后转发）。
 *  - POP/PHP（普通 LSP，vrf_ilm == 0）：创建本模块的 PHP 出向
 *    nexthop（hw_mpls_nh_create_pop：只弹标签不改标签栈，见
 *    g_hw_mpls_php_nh）。**不能**复用 FEC 的 IP 单播 nexthop，也不能
 *    给 ILM 置 pop/decap 位（详见 hw_mpls_ilm_param_fill 注释）。
 *  - VRF ILM（vrf_ilm == 1，L3VPN VC 标签）：保持原行为，POP 时复用
 *    已解析的 IP nexthop。这类 ILM 的转发由 ILM 类型决定
 *    （type=L3VPN + id_type=VRF 时 SDK 置 innerPacketLookup_f，弹掉
 *    VC 标签后按内层 IP 查 vrf_id 对应 L3 表），与 P 的倒数第二跳
 *    PHP 不是同一个问题，不在本次修复范围内。
 *
 * 当任一路径的 ARP 未解析时，构建以“占位”模式成功
 * （*p_placeholder = 1）：调用方把 ILM 安装为绑定到保留 TOCPU
 * nexthop，报文被上送 CPU 而不是被芯片丢弃，等 ARP 解析后由
 * hw_mpls_arp_resolved() 升级表项。真正失败时这里创建的资源
 * 全部回滚。 */
static int hw_mpls_ilm_build_nh(uint32_t in_label, uint32_t nh_count,
                                const struct hw_mpls_ilm_nh *nhs,
                                uint8_t vrf_ilm,
                                uint32_t *p_nhid, uint8_t *p_is_ecmp,
                                uint8_t *p_placeholder)
{
    int ret = 0;
    int i;
    uint32_t member_nhids[HW_MPLS_ECMP_MEMBER_MAX];
    uint8_t  member_is_push[HW_MPLS_ECMP_MEMBER_MAX];
    uint8_t  member_num = 0;
    uint8_t  is_ecmp = 0;
    uint32_t nhid = 0;

    *p_placeholder = 0;

    if (nh_count == 0 || nhs == NULL) {
        printk("hw_mpls: ILM label=%u without nexthop\n", in_label);
        return RUNOS_PARAM;
    }
    if (nh_count > HW_MPLS_ECMP_MEMBER_MAX)
        nh_count = HW_MPLS_ECMP_MEMBER_MAX;

    /* POP/PHP（普通 LSP）走 hw_mpls_nh_create_pop 自建出向 nexthop；
     * VPWS 叠加标签的 ILM 由 hw_mpls_add_l2vpn_pw 独立构建
     * （CTC_MPLS_LABEL_TYPE_VPWS + continuing），不经过本函数。 */

    /* 第一遍：对 HSL nexthop 表探测每条路径。任意一条路径未解析
     * 都使整个表项成为占位（ILM 只有一个 nhid）。 */
    for (i = 0; i < nh_count; i++) {
        ret = hw_mpls_nh_lookup(nhs[i].nexthop, nhs[i].ifindex,
                                NULL, NULL);
        if (ret != RUNOS_OK) {
            *p_placeholder = 1;
            return RUNOS_OK;
        }
    }

    for (i = 0; i < nh_count; i++) {
        uint32_t m_nhid = 0;

        if (nhs[i].flags & HAL_MPLS_FLAG_POP) {
            if (vrf_ilm) {
                /*
                 * L3VPN VC 标签 ILM：保持原行为，复用已解析的 IP
                 * nexthop（SDK 会把 nh_id == 0 当作丢弃 nexthop，所以
                 * 必须有有效 nexthop）。这类 ILM 弹掉 VC 标签后按内层
                 * IP 查 vrf_id 对应的 L3 表（ILM 类型决定），出向由 IP
                 * 路由的 nexthop 完成。
                 */
                ret = hw_mpls_nh_lookup(nhs[i].nexthop, nhs[i].ifindex,
                                        &m_nhid, NULL);
                if (ret != RUNOS_OK) {
                    printk("hw_mpls: VRF POP nh resolve failed label=%u nexthop=0x%x ifindex=%u ret=%d\n",
                           in_label, nhs[i].nexthop, nhs[i].ifindex, ret);
                    goto fail;
                }
                member_is_push[member_num] = 0;
            } else {
                /*
                 * 普通 LSP 的 POP/PHP（倒数第二跳）：自建"弹掉本层后
                 * 直接转发"的出向 nexthop。标签栈原样出向（不压不改），
                 * ILM 也不置 pop/decap 位——复用 IP nexthop 或置 pop 位
                 * 都会导致 P 收 [16][144][IP] 后不转发（见
                 * g_hw_mpls_php_nh 与 hw_mpls_ilm_param_fill 注释）。
                 * 外层 DMAC 来自已解析的 HSL nexthop；传 NULL 会让帧的
                 * 目的 MAC 为零。
                 */
                uint8_t mac[6];

                ret = hw_mpls_nh_lookup(nhs[i].nexthop, nhs[i].ifindex,
                                        NULL, mac);
                if (ret != RUNOS_OK) {
                    printk("hw_mpls: PHP nh resolve failed label=%u nexthop=0x%x ifindex=%u ret=%d\n",
                           in_label, nhs[i].nexthop, nhs[i].ifindex, ret);
                    goto fail;
                }
                ret = hw_mpls_nh_create_pop(nhs[i].nexthop, nhs[i].ifindex,
                                            nhs[i].flags, mac, &m_nhid);
                if (ret != RUNOS_OK) {
                    printk("hw_mpls: PHP nh create failed label=%u nexthop=0x%x ifindex=%u ret=%d\n",
                           in_label, nhs[i].nexthop, nhs[i].ifindex, ret);
                    goto fail;
                }
                member_is_push[member_num] = 1;
            }
        } else if (nhs[i].flags & HAL_MPLS_FLAG_SWAP) {
            /*
             * SWAP：in_label -> out_label。
             * 创建 MPLS push nexthop（PUSH_OP_NONE）并绑定到 ILM，
             * 芯片换标签后转发。外层 DMAC 来自已解析的 HSL nexthop；
             * 传 NULL 会让帧的目的 MAC 为零。
             */
            uint8_t mac[6];

            ret = hw_mpls_nh_lookup(nhs[i].nexthop, nhs[i].ifindex,
                                    NULL, mac);
            if (ret != RUNOS_OK) {
                printk("hw_mpls: SWAP nh resolve failed label=%u nexthop=0x%x ifindex=%u ret=%d\n",
                       in_label, nhs[i].nexthop, nhs[i].ifindex, ret);
                goto fail;
            }
            ret = hw_mpls_nh_create(nhs[i].out_label, nhs[i].nexthop,
                                    nhs[i].ifindex, CTC_MPLS_NH_PUSH_OP_NONE,
                                    mac, &m_nhid);
            if (ret != RUNOS_OK) {
                printk("hw_mpls: SWAP nh create failed label=%u ret=%d\n",
                       nhs[i].out_label, ret);
                goto fail;
            }
            member_is_push[member_num] = 1;
        } else {
            /* 默认按 POP/PHP 处理（含未识别 flag 的路径） */
            uint8_t mac[6];

            if (vrf_ilm) {
                ret = hw_mpls_nh_lookup(nhs[i].nexthop, nhs[i].ifindex,
                                        &m_nhid, NULL);
                if (ret != RUNOS_OK) {
                    printk("hw_mpls: VRF POP nh resolve failed label=%u nexthop=0x%x ifindex=%u ret=%d\n",
                           in_label, nhs[i].nexthop, nhs[i].ifindex, ret);
                    goto fail;
                }
                member_is_push[member_num] = 0;
            } else {
                ret = hw_mpls_nh_lookup(nhs[i].nexthop, nhs[i].ifindex,
                                        NULL, mac);
                if (ret != RUNOS_OK) {
                    printk("hw_mpls: PHP nh resolve failed label=%u nexthop=0x%x ifindex=%u ret=%d\n",
                           in_label, nhs[i].nexthop, nhs[i].ifindex, ret);
                    goto fail;
                }
                ret = hw_mpls_nh_create_pop(nhs[i].nexthop, nhs[i].ifindex,
                                            nhs[i].flags, mac, &m_nhid);
                if (ret != RUNOS_OK) {
                    printk("hw_mpls: PHP nh create failed label=%u nexthop=0x%x ifindex=%u ret=%d\n",
                           in_label, nhs[i].nexthop, nhs[i].ifindex, ret);
                    goto fail;
                }
                member_is_push[member_num] = 1;
            }
        }
        member_nhids[member_num++] = m_nhid;
    }

    if (member_num > 1) {
        /* 多路径 LSP：把 ILM 绑定到 ECMP 组 */
        ret = hw_mpls_ecmp_create(member_nhids, member_is_push,
                                  member_num, &nhid);
        if (ret != RUNOS_OK) {
            printk("hw_mpls: ECMP group create failed label=%u ret=%d\n",
                   in_label, ret);
            goto fail;
        }
        is_ecmp = 1;
    } else {
        nhid = member_nhids[0];
    }

    *p_nhid = nhid;
    *p_is_ecmp = is_ecmp;
    return RUNOS_OK;

fail:
    /* 回滚上面创建的资源（按 nhid）：member_is_push=0 的成员是外部
     * nexthop（VRF ILM 复用的 IPUC），release_by_id 反查不到、自然跳过。 */
    if (is_ecmp) {
        hw_mpls_ecmp_release(nhid);
    } else {
        for (i = 0; i < member_num; i++) {
            if (member_is_push[i])
                hw_mpls_nh_release_by_id(member_nhids[i]);
        }
    }
    return ret;
}

/* 芯片安装本身失败时，释放 hw_mpls_ilm_build_nh 构建的 nexthop(s)
 * （仅单路径；ECMP 通过 hw_mpls_ecmp_release 释放）。按 nhid 反查，push
 * （SWAP）与 PHP（POP/PHP）两种都能释放；VRF ILM 复用的 IPUC nexthop
 * 不在本模块缓存里，反查不到即无需释放。 */
static void hw_mpls_ilm_undo_nh(const struct hw_mpls_ilm_nh *nhs,
                                uint8_t is_ecmp, uint32_t nhid)
{
    if (is_ecmp) {
        hw_mpls_ecmp_release(nhid);
    } else {
        hw_mpls_nh_release_by_id(nhid);
    }
}

/* 按路径标志构造 ILM 查表参数（type/model，label 由调用方填写）。
 * 五处使用：hw_mpls_add_ilm（初始安装）、hw_mpls_update_ilm、
 * hw_mpls_arp_resolved（占位升级）、hw_mpls_arp_del（ARP 删除回退
 * 占位）、hw_mpls_ilm_refresh（POP/PHP 刷新）。VPWS 的 ILM 由
 * hw_mpls_add_l2vpn_pw 独立构建（CTC_MPLS_LABEL_TYPE_VPWS，
 * continuing），不走这里。
 *
 * **POP / PHP（普通 LSP）不能置 ilm->pop，也不能置 ilm->decap**：
 *  - 本芯片的 ilm->pop 被 SDK 直接写成 DsMpls.continuing_f
 *    （sys_usw_mpls.c _sys_usw_mpls_ilm_set_nhinfo：pop=1 → continuing=1；
 *    回读 pop = GetDsMpls(continuing_f)，DsMpls 没有独立的 pop 字段）。
 *    它的语义是"弹掉本标签后**继续用剩余标签查 MPLS 表**"，只适合
 *    VPWS 那种下一层还有本机 ILM 的叠加场景。放在 P（倒数第二跳 PHP）
 *    上，下一层是 egress PE 的 VPN 标签，本机没有它的 ILM → 第二级
 *    查表 miss → IPE 拿不到转发结果 → 致命异常丢弃。实测 pkt-trace：
 *      1st-label-lookup-result: HIT (pop-lable: YES, 仅表示命中)
 *      2nd-label-lookup-result: HIT-NONE
 *      dest-type: DROP / ipe-drop: IPE_DISCARD_FATAL_EXCEP_DIS
 *  - ilm->decap（innerPacketLookup_f）是"把内层当 IP 头解析"，对
 *    [VPN 标签][IP] 的 L3VPN 报文同样是错的（且实测未生效）。
 *
 * 正确语义：ILM 命中时芯片自己消耗掉匹配的标签（L3VPN 型 ILM 就是
 * pop=0/decap=0，标签照样消失），出向动作全部由绑定的 nexthop 承担
 * ——普通 LSP 的 POP/PHP 因此绑定 hw_mpls_nh_create_pop 建的 PHP
 * nexthop（无标签 edit），报文以 [VPN 标签][IP] 原样出向。 */
static void hw_mpls_ilm_param_fill(ctc_mpls_ilm_t *ilm,
                                   const struct hw_mpls_ilm_nh *nhs)
{
    ilm->gport = 0;
    ilm->type  = CTC_MPLS_LABEL_TYPE_NORMAL;
    if ((!(nhs[0].flags & HAL_MPLS_FLAG_SWAP)) &&
        (nhs[0].flags & HAL_MPLS_FLAG_TTL_PIPE))
        ilm->model = CTC_MPLS_TUNNEL_MODE_PIPE;
    /* 其余：统一 TTL（SAI 默认 POP_TTL_MODE） */
}

/* ARP 学习/刷新钩子的"已解析"分支：ILM 已绑定真实 nexthop，但
 * (ifindex, nexthop) 上邻居的 MAC 或出端口变了。恢复转发需要：
 *
 * 1. SWAP 路径：push 的 tunnel 快照（oif.gport + DMAC）是创建时
 *    从 HSL nh 表拷贝的，ARP 刷新后不会自动更新——显式刷新
 *    （hw_mpls_nh_tunnel_refresh，MAC/gport 有变化才下发）。
 * 2. POP / PHP 路径：ILM 引用本模块自建的 PHP 出向 nexthop（key =
 *    (nexthop, ifindex)，nhid 不随 ARP 变化）——刷新它承载的
 *    oif/DMAC 快照（hw_mpls_nh_pop_refresh）。ECMP 组的成员 nhid
 *    因此也不再变化，无需 ctcs_nh_update_ecmp。
 *
 * 同 MAC 的周期 ARP 刷新（内容没变）检测到无变化后直接返回，
 * 不产生任何芯片写。
 * 调用上下文：hw_mpls_arp_resolved（g_hw_mpls_ilm 无锁串行访问）。 */
static int hw_mpls_ilm_refresh(struct hw_mpls_ilm_cache *c,
                               uint32_t ifindex, uint32_t nexthop)
{
    int i, j, ret;
    int ecmp_idx = -1;

    if (c->is_ecmp) {
        for (i = 0; i < HW_MPLS_ECMP_MAX; i++) {
            if (g_hw_mpls_ecmp[i].used &&
                g_hw_mpls_ecmp[i].nhid == c->nhid) {
                ecmp_idx = i;
                break;
            }
        }
        if (ecmp_idx < 0) {
            printk("hw_mpls: ILM label=%u ecmp nhid=%u not in cache\n",
                   c->in_label, c->nhid);
            return RUNOS_NOT_FOUND;
        }
    }

    /* ph_nhs[j] 与 ECMP 组 members[j] 按下标一一对应
     * （hw_mpls_ilm_build_nh 按 ph_nhs 顺序填充 member_nhids） */
    for (j = 0; j < c->ph_count; j++) {
        if (c->ph_nhs[j].ifindex != ifindex ||
            c->ph_nhs[j].nexthop != nexthop)
            continue;

        if (c->ph_nhs[j].flags & HAL_MPLS_FLAG_SWAP) {
            ret = hw_mpls_nh_tunnel_refresh(c->ph_nhs[j].out_label,
                                            c->ph_nhs[j].nexthop,
                                            c->ph_nhs[j].ifindex);
            if (ret != RUNOS_OK)
                printk("hw_mpls: ILM label=%u SWAP tunnel refresh failed ret=%d\n",
                       c->in_label, ret);
            continue;
        }

        /* POP / PHP：ILM 绑定的是自建 PHP nexthop，nhid 不随 ARP 变化，
         * 只刷新它承载的 oif/DMAC 快照（ECMP 组成员 nhid 因此也不会变，
         * 无需 ctcs_nh_update_ecmp） */
        ret = hw_mpls_nh_pop_refresh(c->ph_nhs[j].nexthop,
                                     c->ph_nhs[j].ifindex);
        if (ret != RUNOS_OK)
            printk("hw_mpls: ILM label=%u PHP nh refresh failed ret=%d\n",
                   c->in_label, ret);
    }

    return RUNOS_OK;
}

int hw_mpls_add_ilm(uint32_t in_label, uint32_t nh_count,
                    const struct hw_mpls_ilm_nh *nhs)
{
    uint8 unit;
    int ret = 0;
    ctc_mpls_ilm_t ilm;
    uint8_t  is_ecmp = 0;
    uint8_t  placeholder = 0;
    uint32_t nhid = 0;

    ret = hw_mpls_ilm_build_nh(in_label, nh_count, nhs, 0, &nhid, &is_ecmp,
                               &placeholder);
    if (ret != RUNOS_OK)
        return ret;

    memset(&ilm, 0, sizeof(ilm));
    ilm.label = in_label;
    /* gport 仅在 ASSIGN_OUTPUT_PORT flag 设置时生效；普通 LSP
     * 不设置该 flag，SDK 从 nhid 取目的映射，gport 被忽略。
     * nhs[0].ifindex 是 ifindex 语义，与 gport（chip<<6+port）
     * 格式不同，不应直接填入。 */
    hw_mpls_ilm_param_fill(&ilm, nhs);
    if (placeholder) {
        /* ARP 未解析（SWAP/POP/PHP 路径）：绑定到保留 TOCPU
         * nexthop，芯片把报文上送 CPU，直到 hw_mpls_arp_resolved()
         * 升级表项。 */
        ilm.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;
    } else {
        ilm.nh_id = nhid;
    }

    FOREACH_UNIT(unit)
    {
        ret = ctcs_mpls_add_ilm(unit, &ilm);
        if (ret != CTC_E_NONE)
            break;
    }

    if (ret != CTC_E_NONE) {
        if (!placeholder)
            hw_mpls_ilm_undo_nh(nhs, is_ecmp, nhid);
        return ret;
    }

    hw_mpls_ilm_map_set(in_label, nhid, is_ecmp, placeholder,
                        nh_count, nhs);
    return placeholder ? CTC_HAL_RET_MPLS_PLACEHOLDER : RUNOS_OK;
}

int hw_mpls_del_ilm(uint32_t in_label)
{
    uint8 unit;
    int ret = 0;
    ctc_mpls_ilm_t ilm;

    memset(&ilm, 0, sizeof(ilm));
    ilm.label = in_label;

    FOREACH_UNIT(unit)
    {
        ret = ctcs_mpls_del_ilm(unit, &ilm);
        if (ret != CTC_E_NONE)
            break;
    }

    /* 释放绑定在这个 ILM 上的 push nexthop / ECMP 组 */
    hw_mpls_ilm_release_nh(in_label);

    return ret;
}

int hw_mpls_update_ilm(uint32_t in_label, uint32_t nh_count,
                       const struct hw_mpls_ilm_nh *nhs)
{
    uint8 unit;
    int ret = 0;
    ctc_mpls_ilm_t ilm;
    uint8_t  is_ecmp = 0;
    uint8_t  placeholder = 0;
    uint32_t nhid = 0;

    /* 释放之前绑定的 nexthop / ECMP 组（如果有） */
    hw_mpls_ilm_release_nh(in_label);

    ret = hw_mpls_ilm_build_nh(in_label, nh_count, nhs, 0, &nhid, &is_ecmp,
                               &placeholder);
    if (ret != RUNOS_OK)
        return ret;

    memset(&ilm, 0, sizeof(ilm));
    ilm.label = in_label;
    /* gport 仅在 ASSIGN_OUTPUT_PORT flag 设置时生效；普通 LSP
     * 不设置该 flag，SDK 从 nhid 取目的映射，gport 被忽略。
     * nhs[0].ifindex 是 ifindex 语义，与 gport（chip<<6+port）
     * 格式不同，不应直接填入。 */
    hw_mpls_ilm_param_fill(&ilm, nhs);
    if (placeholder) {
        /* ARP 未解析：把 ILM 切回 TOCPU 上送 */
        ilm.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;
    } else {
        ilm.nh_id = nhid;
    }

    FOREACH_UNIT(unit)
    {
        ret = ctcs_mpls_update_ilm(unit, &ilm);
        if (ret != CTC_E_NONE)
            break;
    }

    if (ret != CTC_E_NONE) {
        if (!placeholder)
            hw_mpls_ilm_undo_nh(nhs, is_ecmp, nhid);
        return ret;
    }

    hw_mpls_ilm_map_set(in_label, nhid, is_ecmp, placeholder,
                        nh_count, nhs);
    return placeholder ? CTC_HAL_RET_MPLS_PLACEHOLDER : RUNOS_OK;
}

/* ------------------------------------------------------------------
 * ARP 学习钩子
 * ------------------------------------------------------------------ */

/* 由 HSL NH 模块在一跳 nexthop 的 ARP 学习完成后调用（hw_l3_add_nh
 * 成功，MAC 有效）。把该 (ifindex, nexthop) 上所有占位模式的 ILM
 * 升级为真实 nexthop，芯片直接转发 MPLS 流量。
 *
 * 必须在与 ILM add/update/delete 处理相同的串行化上下文中调用
 * （g_hw_mpls_ilm 无锁）。 */

/* 前置声明：PW 占位升级（定义在 L2VPN PW 段，依赖 g_hw_mpls_pw） */
int hw_mpls_pw_arp_resolved(uint32_t ifindex, uint32_t nexthop);

/* 前置声明：VRF IPUC 路由占位升级（定义在 hw/hw_l3.c） */
void hw_l3_vrf_arp_resolved(uint32_t ifindex, uint32_t nexthop);

int hw_mpls_arp_resolved(uint32_t ifindex, uint32_t nexthop)
{
    int i, j, ret;
    uint8 unit;
    uint8_t  is_ecmp = 0;
    uint8_t  placeholder = 0;
    uint32_t nhid = 0;
    ctc_mpls_ilm_t ilm;

    for (i = 0; i < HW_MPLS_ILM_MAX; i++) {
        struct hw_mpls_ilm_cache *c = &g_hw_mpls_ilm[i];
        int matched = 0;

        if (!c->used)
            continue;

        for (j = 0; j < c->ph_count; j++) {
            if (c->ph_nhs[j].ifindex == ifindex &&
                c->ph_nhs[j].nexthop == nexthop) {
                matched = 1;
                break;
            }
        }
        if (!matched)
            continue;

        if (!c->placeholder) {
            /* 已解析后 ARP 刷新（MAC / 出端口变化）：更新 SWAP
             * tunnel 快照、POP 引用与 ECMP 成员，见
             * hw_mpls_ilm_refresh。 */
            ret = hw_mpls_ilm_refresh(c, ifindex, nexthop);
            if (ret != RUNOS_OK)
                printk("hw_mpls: ILM label=%u ARP refresh failed ret=%d\n",
                       c->in_label, ret);
            continue;
        }

        /* ARP 就绪（占位升级）：重建真实 nexthop 并更新 ILM */
        ret = hw_mpls_ilm_build_nh(c->in_label, c->ph_count, c->ph_nhs, 0,
                                   &nhid, &is_ecmp, &placeholder);
        if (ret != RUNOS_OK || placeholder)
            continue; /* 仍未解析：继续占位挂起 */

        sal_memset(&ilm, 0, sizeof(ilm));
        ilm.label = c->in_label;
        /* gport 仅在 ASSIGN_OUTPUT_PORT 时生效，普通 LSP 被 SDK
         * 忽略；ph_nhs[0].ifindex 是 ifindex 语义而非 gport 格式 */
        hw_mpls_ilm_param_fill(&ilm, c->ph_nhs);
        ilm.nh_id = nhid;

        FOREACH_UNIT(unit)
        {
            ret = ctcs_mpls_update_ilm(unit, &ilm);
            if (ret != CTC_E_NONE)
                break;
        }
        if (ret != CTC_E_NONE) {
            hw_mpls_ilm_undo_nh(c->ph_nhs, is_ecmp, nhid);
            continue;
        }

        c->nhid = nhid;
        c->is_ecmp = is_ecmp;
        c->placeholder = 0;
        /* 保留 ph_nhs / ph_count（与 hw_mpls_ilm_map_set 的承诺
         * 一致）：ARP 刷新（hw_mpls_ilm_refresh）需要原始路径匹配
         * 事件；ARP 删除回退占位也需要它们重建 TOCPU 表项。 */

        /* ILM 重建过：重新挂上流统计（如果有） */
        hw_mpls_stats_attach(c->in_label, 0);
    }

    /* VRF ILM（L3VPN VC 标签）占位升级：与上面普通 ILM 同一事件、
     * 同一串行化上下文。L3VPN 表项重建为 CTC_MPLS_LABEL_TYPE_L3VPN +
     * CTC_MPLS_ID_VRF 构造：弹掉 VC 标签后内层 IP 查 vrf_id 对应 L3
     * 路由表（innerPacketLookup_f），见表项说明与 hw_mpls_lsp_add_vrf。 */
    for (i = 0; i < HW_MPLS_VRF_ILM_MAX; i++) {
        struct hw_mpls_vrf_ilm_cache *c = &g_hw_mpls_vrf_ilm[i];
        int matched = 0;

        if (!c->used || !c->placeholder)
            continue;

        for (j = 0; j < c->ph_count; j++) {
            if (c->ph_nhs[j].ifindex == ifindex &&
                c->ph_nhs[j].nexthop == nexthop) {
                matched = 1;
                break;
            }
        }
        if (!matched)
            continue;

        /* ARP 就绪：重建真实 nexthop 并更新 ILM（vrf_ilm=1：POP 路径保持
         * 复用 CE 侧 IP nexthop 的原行为） */
        ret = hw_mpls_ilm_build_nh(c->in_label, c->ph_count, c->ph_nhs, 1,
                                   &nhid, &is_ecmp, &placeholder);
        if (ret != RUNOS_OK || placeholder)
            continue; /* 仍未解析：继续占位挂起 */

        sal_memset(&ilm, 0, sizeof(ilm));
        ilm.label = c->in_label;            /* 网络序直传 */
        ilm.gport = 0;
        ilm.type  = CTC_MPLS_LABEL_TYPE_L3VPN;
        ilm.id_type = CTC_MPLS_ID_VRF;
        ilm.vrf_id = (uint16_t)c->vrf_id;
        ilm.flw_vrf_srv_aps.vrf_id = (uint16_t)c->vrf_id;
        /* auto(IP)：与 add_vrf 一致，v4/v6 载荷共用一条 L3VPN ILM */
        ilm.inner_pkt_type = CTC_MPLS_INNER_IP;
        ilm.nh_id = nhid;

        FOREACH_UNIT(unit)
        {
            ret = ctcs_mpls_update_ilm(unit, &ilm);
            if (ret != CTC_E_NONE)
                break;
        }
        if (ret != CTC_E_NONE) {
            hw_mpls_ilm_undo_nh(c->ph_nhs, is_ecmp, nhid);
            continue;
        }

        c->nhid = nhid;
        c->is_ecmp = is_ecmp;
        c->placeholder = 0;
        /* 保留 ph_nhs / ph_count：ARP 撤销（hw_mpls_arp_del）回退
         * 占位时需要它们重建 TOCPU 表项。 */
    }

    /* ARP 学习到：升级挂在 TOCPU 上的 VPWS PW（与上面的 ILM
     * 升级同一事件、同一串行化上下文）。 */
    hw_mpls_pw_arp_resolved(ifindex, nexthop);

    /* ARP 学习到：升级 VRF IPUC 路由的 TOCPU 占位（hw_l3.c，
     * 与 ILM/PW 升级同一事件）。 */
    hw_l3_vrf_arp_resolved(ifindex, nexthop);
    return RUNOS_OK;
}

/* ARP 撤销钩子：由 HSL NH 模块在 nexthop 的 ARP 表项删除或端口
 * down（MAC 失效）时调用（hsl_nh.c _hsl_nh_del）。把绑定在该
 * (ifindex, nexthop) 上的所有 ILM（普通 + VRF）从真实 nexthop 回退到
 * TOCPU 占位，避免继续用已失效的 DMAC 转发形成黑洞；ARP 重新学习后
 * 由 hw_mpls_arp_resolved() 再次升级。与 ILM add/update/delete 处理相同
 * 的串行化上下文（g_hw_mpls_ilm / g_hw_mpls_vrf_ilm 无锁）。
 *
 * 多路径（ECMP）表项中任一路径 ARP 失效即整体回退占位——与
 * hw_mpls_ilm_build_nh 的占位策略一致（ILM 只有一个 nhid）。
 * ph_nhs[] 始终保留（map_set 持久保存），因此回退后无需重建。 */
void hw_mpls_arp_del(uint32_t ifindex, uint32_t nexthop)
{
    int i, j, ret;
    uint8 unit;
    ctc_mpls_ilm_t ilm;

    /* 普通 ILM */
    for (i = 0; i < HW_MPLS_ILM_MAX; i++) {
        struct hw_mpls_ilm_cache *c = &g_hw_mpls_ilm[i];
        int matched = 0;

        if (!c->used || c->placeholder)
            continue; /* 空槽 / 已是占位 */

        for (j = 0; j < c->ph_count; j++) {
            if (c->ph_nhs[j].ifindex == ifindex &&
                c->ph_nhs[j].nexthop == nexthop) {
                matched = 1;
                break;
            }
        }
        if (!matched)
            continue;

        /* 释放当前 nexthop / ECMP 组绑定。按 nhid 反查：可能是 push（SWAP）
         * 也可能是 PHP（POP/PHP，存在 g_hw_mpls_php_nh）——只扫
         * g_hw_mpls_nh 会漏掉 PHP nexthop，导致 ARP 每次失效-重学都漏
         * 一个引用（ref_count 只增不减，id 表最终耗尽）。 */
        if (c->is_ecmp) {
            hw_mpls_ecmp_release(c->nhid);
        } else {
            hw_mpls_nh_release_by_id(c->nhid);
        }

        /* ILM 回退到 TOCPU 上送 */
        sal_memset(&ilm, 0, sizeof(ilm));
        ilm.label = c->in_label;
        hw_mpls_ilm_param_fill(&ilm, c->ph_nhs);
        ilm.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;

        FOREACH_UNIT(unit)
        {
            ret = ctcs_mpls_update_ilm(unit, &ilm);
            if (ret != CTC_E_NONE)
                break;
        }
        if (ret != CTC_E_NONE) {
            printk("hw_mpls: arp_del ILM update FAILED label=%u ret=%d\n",
                   c->in_label, ret);
            continue; /* 芯片更新失败：保持原状（尽力而为） */
        }

        c->nhid = CTC_NH_RESERVED_NHID_FOR_TOCPU;
        c->is_ecmp = 0;
        c->placeholder = 1;
        /* ph_nhs[] 保留：等待 ARP 重新解析后升级 */
    }

    /* VRF ILM（L3VPN VC 标签）同样回退 */
    for (i = 0; i < HW_MPLS_VRF_ILM_MAX; i++) {
        struct hw_mpls_vrf_ilm_cache *c = &g_hw_mpls_vrf_ilm[i];
        int matched = 0;

        if (!c->used || c->placeholder)
            continue;

        for (j = 0; j < c->ph_count; j++) {
            if (c->ph_nhs[j].ifindex == ifindex &&
                c->ph_nhs[j].nexthop == nexthop) {
                matched = 1;
                break;
            }
        }
        if (!matched)
            continue;

        if (c->is_ecmp) {
            hw_mpls_ecmp_release(c->nhid);
        } else {
            /* 同普通 ILM：按 nhid 反查（VRF POP 复用的 IPUC nexthop 反查
             * 不到，返回 NOT_FOUND 忽略） */
            hw_mpls_nh_release_by_id(c->nhid);
        }

        sal_memset(&ilm, 0, sizeof(ilm));
        ilm.label = c->in_label;
        ilm.gport = 0;
        ilm.type  = CTC_MPLS_LABEL_TYPE_L3VPN;
        ilm.id_type = CTC_MPLS_ID_VRF;
        ilm.vrf_id = (uint16_t)c->vrf_id;
        ilm.flw_vrf_srv_aps.vrf_id = (uint16_t)c->vrf_id;
        /* auto(IP)：回退占位也保留双栈内层语义（v4/v6 共用） */
        ilm.inner_pkt_type = CTC_MPLS_INNER_IP;
        ilm.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;

        FOREACH_UNIT(unit)
        {
            ret = ctcs_mpls_update_ilm(unit, &ilm);
            if (ret != CTC_E_NONE)
                break;
        }
        if (ret != CTC_E_NONE) {
            printk("hw_mpls: arp_del VRF ILM update FAILED vrf=%u label=%u ret=%d\n",
                   c->vrf_id, c->in_label, ret);
            continue;
        }

        c->nhid = CTC_NH_RESERVED_NHID_FOR_TOCPU;
        c->is_ecmp = 0;
        c->placeholder = 1;
        /* ph_nhs[] 保留：等待 ARP 重新解析后升级 */
    }
}

/* ------------------------------------------------------------------
 * L2VPN PW（VPWS）
 *
 * 参考 SAI 方式的 VPWS 语义（ctc_sai_mpls.c VPWS 分支 + SDK 官方
 * VPWS 示例 app_example.c mpls_vpws_add_example）：
 *   - 入方向 (PW->AC)：ctcs_mpls_add_l2vpn_pw 下发 VPWS 类型 ILM
 *     （decap=1），剥掉 VC 标签/控制字后按 AC 出口 nexthop（L2UC）
 *     做二层转发（对应 SAI：CTC_MPLS_LABEL_TYPE_VPWS + cross-connect）；
 *   - 出方向 (AC->PW)：AC 口 vlan mapping（OUTPUT_NHID | FLAG_VPWS）
 *     命中后跳转到 VPWS push nexthop（PUSH_OP_L2VPN），依次压入
 *     VC 出标签与隧道标签（对应 SAI 的 L2VPN tunnel nexthop）。
 *     PSN ARP 未解析时 vlan mapping 以 TOCPU 占位挂起，ARP 学习后
 *     由 hw_mpls_pw_arp_resolved() 升级（与 LSP ILM 占位机制一致）。
 * ------------------------------------------------------------------ */

#define HW_MPLS_PW_MAX 256

/* VPWS PW 的 PSN 隧道路径（与 hsl_mpls.c 中定义同步） */
struct hw_mpls_pw_nh {
    uint32_t tunnel_label;
    uint32_t nexthop;
    uint32_t ifindex;
};

/* PW cache：记录下发参数，删除时逆序回收芯片资源 */
struct hw_mpls_pw_cache {
    uint32_t in_label;
    uint32_t out_label;
    uint32_t flags;       /* HAL_MPLS_PW_FLAG_* */
    uint32_t pw_type;     /* HAL_MPLS_PW_TYPE_* */
    uint32_t ac_ifindex;  /* AC 关联口（HSL ifindex） */
    uint32_t ac_gport;    /* AC 出口 gport（0 表示未解析到） */
    uint32_t push_nhid;   /* AC->PW 方向 VPWS push nexthop id */
    uint8_t  nh_count;
    uint8_t  used;
    uint8_t  arp_pending; /* 1: PSN ARP 未解析，vlan mapping 以 TOCPU 占位 */
    struct hw_mpls_pw_nh nhs[CTC_HAL_MSG_MAX_LSP_NH];
};

static struct hw_mpls_pw_cache g_hw_mpls_pw[HW_MPLS_PW_MAX];

static int hw_mpls_pw_find(uint32_t in_label)
{
    int i;

    for (i = 0; i < HW_MPLS_PW_MAX; i++) {
        if (g_hw_mpls_pw[i].used && g_hw_mpls_pw[i].in_label == in_label)
            return i;
    }
    return -1;
}

/* 把 AC 关联 ifindex 解析成 SDK gport（与 hw_mpls_oif_fill 相同的
 * 端口解析规则：trunk 编码或物理口直接转换）。
 * VPWS AC 必须是二层口：VLANIF（L3 VLAN 接口）编码拒绝解析
 * （返回 RUNOS_PARAM）——L3 口没有 L2UC/BRGUC 二层转发语义，
 * PW->AC 方向 pop VC 标签后会误走 L3 查表路径。
 * 其它解析失败（RUNOS_PORT：HSL 不认识接口 / port 解析为 0）同样
 * 由调用方直接拒绝整条 PW：ac_gport=0 时入方向 ILM 的 pw_nh_id
 * 只能为 0（芯片行为未定义）、出方向 vlan mapping 无法下发，
 * 降级路径双向黑洞且 zebra 误报成功，不如失败让 zebra 感知。 */
static int hw_mpls_pw_ac_resolve(uint32_t ac_ifindex, uint32_t *p_gport)
{
    hsl_if_t *ifp;
    hw_lport_t hport;

    if (!ac_ifindex)
        return RUNOS_PARAM;

    /* L3 接口（zebra ToHslIfindex 的 vlanif 编码）不能做 AC */
    if (UIF_GET_TYPE(ac_ifindex) == PORT_TYPE_VLANIF) {
        printk("pw AC ifindex=0x%x is a L3 vlanif, reject", ac_ifindex);
        return RUNOS_PARAM;
    }

    ifp = hsl_get_if_by_ifindex(ac_ifindex);
    if (ifp == NULL)
        return RUNOS_PORT; /* HSL 不认识（如 dummy PW 接口） */

    if (UIF_IFINDEX_IS_TRUNK(ac_ifindex)) {
        *p_gport = CTC_MAP_TID_TO_GPORT(
            HW_TRUNK_ID(UIF_GET_TRUNK_ID(ac_ifindex)));
        return RUNOS_OK;
    }
    ifindex2hport(ac_ifindex, &hport);
    if (hport.port == 0)
        return RUNOS_PORT;
    *p_gport = hport.port;
    return RUNOS_OK;
}

/* 创建 AC->PW 方向的 VPWS push nexthop（PUSH_OP_L2VPN）：
 * 依次压入 VC 出标签与隧道标签（PHP 时只压 VC 标签），
 * 控制字使能时封装 Martini 控制字（对应 SAI 的 L2VPN tunnel nexthop） */
static int hw_mpls_pw_push_nh_create(uint32_t out_label, uint8_t cwen,
                                     const struct hw_mpls_pw_nh *psn,
                                     uint32_t *p_nhid)
{
    uint8 unit;
    int32_t ret;
    uint8_t mac[6];
    uint32_t nhid = 0;
    ctc_nh_oif_info_t oif;
    ctc_mpls_nexthop_param_t nh_param;
    ctc_mpls_nexthop_push_param_t *push;
    ctc_mpls_nexthop_tunnel_param_t tunnel_param;

    /* 解析 PSN 出口：对端 MAC + 出接口。MAC 未学到时不下发 push
     * nexthop（全 0 DMAC 会使出方向永久黑洞），由调用方以 TOCPU
     * 占位挂起，等 hw_mpls_pw_arp_resolved() 升级。 */
    sal_memset(mac, 0, sizeof(mac));
    ret = hw_mpls_nh_lookup(psn->nexthop, psn->ifindex, NULL, mac);
    if (ret != RUNOS_OK)
        return ret;

    ret = hw_mpls_oif_fill(psn->nexthop, psn->ifindex,
                           (mac[0] | mac[1] | mac[2] |
                            mac[3] | mac[4] | mac[5]) ? mac : NULL,
                           &oif);
    if (ret != RUNOS_OK)
        return ret;

    sal_memset(&nh_param, 0, sizeof(nh_param));
    push = &nh_param.nh_para.nh_param_push;
    nh_param.nh_prop = CTC_MPLS_NH_PUSH_TYPE;
    nh_param.upd_type = CTC_NH_UPD_FWD_ATTR;
    push->nh_com.opcode = CTC_MPLS_NH_PUSH_OP_L2VPN;
    push->nh_com.oif = oif;
    /* 外层 MAC 由 tunnel label nexthop 承载，push 不携带 DMAC
     *（非零会被 SDK 判为 inner l2 edit，普通 LSP push 直接返回
     *  CTC_E_INVALID_CONFIG=-31995）。push->nh_com.mac 已 memset
     *  为 0，无需额外处理。 */

    /* 内层 VC 标签（PW 标签） */
    push->push_label[0].label = out_label;
    push->push_label[0].ttl = 255;
    push->push_label[0].exp_type = CTC_NH_EXP_SELECT_PACKET;
    push->push_label[0].lable_flag |= CTC_MPLS_NH_LABEL_IS_VALID;
    push->label_num = 1;

    /* Martini 控制字（RFC 4448）：cwen 时封装 32bit 控制字 */
    if (cwen) {
        push->martini_encap_valid = 1;
        push->martini_encap_type = 0;
    }

    FOREACH_UNIT(unit)
    {
        ret = hw_mpls_nh_alloc_id(unit, &nhid);
        if (ret != RUNOS_OK)
            return ret;

        /* SDK 两步模型：先建 tunnel label nexthop（承载外层标签 /
         * oif / mac），push 再通过 tunnel_id 引用它。 */
        sal_memset(&tunnel_param, 0, sizeof(tunnel_param));
        if (psn->tunnel_label) {
            tunnel_param.nh_param.tunnel_label[0].label = psn->tunnel_label;
            tunnel_param.nh_param.tunnel_label[0].ttl = 64;
            tunnel_param.nh_param.tunnel_label[0].exp_type = CTC_NH_EXP_SELECT_PACKET;
            tunnel_param.nh_param.tunnel_label[0].lable_flag |= CTC_MPLS_NH_LABEL_IS_VALID;
            tunnel_param.nh_param.label_num = 1;
        }
        tunnel_param.nh_param.oif = oif;
        if (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5])
            memcpy(tunnel_param.nh_param.mac, mac, 6);
        ret = hw_nh_add_mpls_tunnel_label((uint16_t)nhid, &tunnel_param);
        if (ret != RUNOS_OK)
            return ret;

        /* push 引用 tunnel */
        push->tunnel_id = (uint16_t)nhid;
        ret = ctcs_nh_add_mpls(unit, nhid, &nh_param);
        if (ret != CTC_E_NONE) {
            /* push 创建失败：回滚刚建的 tunnel */
            hw_nh_remove_mpls_tunnel_label((uint16_t)nhid);
            return ret;
        }
    }

    *p_nhid = nhid;
    return RUNOS_OK;
}

/* 在 AC 口下发 vlan mapping：AC 收包按口匹配 -> OUTPUT_NHID|FLAG_VPWS
 * 跳转到 VPWS push nexthop（对应 SDK 官方 VPWS 示例的 AC->PW 配置） */
static int hw_mpls_pw_ac_mapping_add(uint32_t ac_gport, uint32_t push_nhid)
{
    uint8 unit;
    int32_t ret;
    ctc_vlan_mapping_t vm;

    sal_memset(&vm, 0, sizeof(vm));
    vm.key = CTC_VLAN_MAPPING_KEY_NONE; /* 按 AC 口匹配（无 vlan 信息） */
    vm.action = CTC_VLAN_MAPPING_OUTPUT_NHID | CTC_VLAN_MAPPING_FLAG_VPWS;
    vm.u3.nh_id = push_nhid;

    FOREACH_UNIT(unit)
    {
        ret = ctcs_vlan_add_vlan_mapping(unit, ac_gport, &vm);
        if (ret != CTC_E_NONE)
            return ret;
        /* SCL 查询类型切到 vlan mapping 端口模式 */
        ret = ctcs_port_set_scl_key_type(unit, ac_gport, CTC_INGRESS, 1,
                                         CTC_SCL_KEY_TYPE_VLAN_MAPPING_PORT);
        if (ret != CTC_E_NONE)
            return ret;
    }
    return RUNOS_OK;
}

/* 删除 AC 口 vlan mapping 并恢复 SCL 查询类型（VPWS 一个 AC 口
 * 对应一条 PW，删除时直接复位） */
static int hw_mpls_pw_ac_mapping_del(uint32_t ac_gport, uint32_t push_nhid)
{
    uint8 unit;
    int32_t ret = 0;
    ctc_vlan_mapping_t vm;

    sal_memset(&vm, 0, sizeof(vm));
    vm.key = CTC_VLAN_MAPPING_KEY_NONE;
    vm.action = CTC_VLAN_MAPPING_OUTPUT_NHID | CTC_VLAN_MAPPING_FLAG_VPWS;
    vm.u3.nh_id = push_nhid;

    FOREACH_UNIT(unit)
    {
        ret = ctcs_vlan_remove_vlan_mapping(unit, ac_gport, &vm);
        if (ret != CTC_E_NONE)
            break;
        ctcs_port_set_scl_key_type(unit, ac_gport, CTC_INGRESS, 1,
                                   CTC_SCL_KEY_TYPE_DISABLE);
    }
    return ret;
}

/* 前置声明：add 内部幂等逻辑会先调用 del 回收旧条目 */
int hw_mpls_del_l2vpn_pw(uint32_t in_label);

/* 下发 VPWS 伪线（参考 SAI 方式的 VPWS 语义）：
 *   - 入方向 (PW->AC)：ctcs_mpls_add_l2vpn_pw（VPWS ILM，decap 后
 *     按 AC 出口 L2UC nexthop 转发）；
 *   - 出方向 (AC->PW)：VPWS push nexthop + AC 口 vlan mapping。
 * 删除时按 cache 逆序回收全部芯片资源。 */
int hw_mpls_add_l2vpn_pw(uint32_t in_label, uint32_t out_label,
                         uint32_t flags, uint32_t pw_type,
                         uint32_t ac_ifindex, uint32_t nh_count,
                         const struct hw_mpls_pw_nh *nhs)
{
    uint8 unit;
    int ret = 0;
    int idx;
    uint32_t ac_gport = 0;
    uint32_t ac_nhid = 0;
    uint32_t push_nhid = 0;
    uint8_t  cwen;
    uint8_t  arp_pending = 0;
    ctc_mpls_l2vpn_pw_t pw;
    struct hw_mpls_pw_cache *pc;

    printk("add l2vpn pw in_label=%u out_label=%u flags=0x%x type=0x%x ac=%u nh=%u",
         in_label, out_label, flags, pw_type, ac_ifindex, nh_count);

    /* 幂等：同标签旧条目先删除再重建 */
    idx = hw_mpls_pw_find(in_label);
    if (idx >= 0)
        hw_mpls_del_l2vpn_pw(in_label);

    for (idx = 0; idx < HW_MPLS_PW_MAX; idx++) {
        if (!g_hw_mpls_pw[idx].used)
            break;
    }
    if (idx >= HW_MPLS_PW_MAX)
        return RUNOS_FULL;
    pc = &g_hw_mpls_pw[idx];

    cwen = (flags & HAL_MPLS_PW_FLAG_CWORD) ? 1 : 0;

    /* AC 关联口解析：任何解析失败（L3 接口 RUNOS_PARAM、接口不存在
     * RUNOS_PORT）都拒绝整条 PW——降级路径（ac_gport=0）会下发
     * pw_nh_id=0 的 VPWS ILM（芯片行为未定义）且出方向完全不下发，
     * 双向黑洞却向 zebra 报告成功；半残不如失败，让 zebra 感知后
     * 定时重试（AC 接口可用后重试自然成功）。 */
    ret = hw_mpls_pw_ac_resolve(ac_ifindex, &ac_gport);
    if (ret != RUNOS_OK) {
        printk("pw %u: AC ifindex=0x%x not usable (ret=%d), reject", in_label,
             ac_ifindex, ret);
        return ret;
    }

    /* ---- 入方向 (PW->AC)：VPWS 类型 ILM ---- */
    FOREACH_UNIT(unit)
    {
        ret = ctcs_nh_get_l2uc(unit, ac_gport,
                               CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC,
                               &ac_nhid);
        if (ret != CTC_E_NONE)
            break;
    }
    if (ret != CTC_E_NONE) {
        printk("pw %u: get AC %u L2UC nh failed ret=%d, reject", in_label,
             ac_gport, ret);
        return ret; /* 取不到二层出口，拒绝整条 PW（避免 pw_nh_id=0） */
    }

    sal_memset(&pw, 0, sizeof(pw));
    pw.label = in_label;
    pw.l2vpntype = CTC_MPLS_L2VPN_VPWS;
    pw.u.pw_nh_id = ac_nhid;
    pw.pw_mode = (pw_type == HAL_MPLS_PW_TYPE_ETHERNET) ?
                 CTC_MPLS_L2VPN_RAW : CTC_MPLS_L2VPN_TAGGED;
    pw.cwen = cwen;
    pw.inner_pkt_type = CTC_MPLS_INNER_RAW; /* 内层是 L2 报文，不做 IP 解析 */
    pw.gport = ac_gport;
    pw.flag |= CTC_MPLS_ILM_FLAG_ASSIGN_OUTPUT_PORT;

    FOREACH_UNIT(unit)
    {
        ret = ctcs_mpls_add_l2vpn_pw(unit, &pw);
        if (ret != CTC_E_NONE) {
            printk("pw %u: add VPWS ILM failed ret=%d", in_label, ret);
            return ret;
        }
    }

    /* ---- 出方向 (AC->PW)：VPWS push nexthop + AC 口 vlan mapping ----
     * PSN 对端 ARP 未解析时不下发 push nexthop（避免以全 0 DMAC 写
     * 芯片导致出方向永久黑洞），vlan mapping 先指向 TOCPU 占位，
     * hw_mpls_pw_arp_resolved() 在 ARP 学习后升级为真实 push nexthop
     * （与 LSP ILM 的占位机制一致）。 */
    if (out_label && nh_count > 0) {
        if (hw_mpls_nh_lookup(nhs[0].nexthop, nhs[0].ifindex,
                              NULL, NULL) != RUNOS_OK) {
            /* ARP 未解析：vlan mapping 以 TOCPU 占位，等待升级 */
            ret = hw_mpls_pw_ac_mapping_add(ac_gport,
                                            CTC_NH_RESERVED_NHID_FOR_TOCPU);
            if (ret != RUNOS_OK) {
                printk("pw %u: AC %u placeholder mapping failed ret=%d",
                     in_label, ac_gport, ret);
                goto fail_ilm;
            }
            push_nhid = CTC_NH_RESERVED_NHID_FOR_TOCPU;
            arp_pending = 1;
        } else {
            ret = hw_mpls_pw_push_nh_create(out_label, cwen, &nhs[0],
                                            &push_nhid);
            if (ret != RUNOS_OK) {
                printk("pw %u: create VPWS push nh failed ret=%d",
                     in_label, ret);
                goto fail_ilm;
            }
            ret = hw_mpls_pw_ac_mapping_add(ac_gport, push_nhid);
            if (ret != RUNOS_OK) {
                printk("pw %u: AC %u vlan mapping failed ret=%d", in_label,
                     ac_gport, ret);
                goto fail_push;
            }
        }
    }

    /* ---- 记录 cache ---- */
    pc->in_label = in_label;
    pc->out_label = out_label;
    pc->flags = flags;
    pc->pw_type = pw_type;
    pc->ac_ifindex = ac_ifindex;
    pc->ac_gport = ac_gport;
    pc->push_nhid = push_nhid;
    pc->arp_pending = arp_pending;
    pc->nh_count = (nh_count > CTC_HAL_MSG_MAX_LSP_NH) ?
                   CTC_HAL_MSG_MAX_LSP_NH : (uint8_t)nh_count;
    if (nhs && nh_count > 0)
        memcpy(pc->nhs, nhs, pc->nh_count * sizeof(pc->nhs[0]));
    pc->used = 1;

    printk("add l2vpn pw %u done: ac_gport=%u ac_nhid=%u push_nhid=%u pending=%u",
         in_label, ac_gport, ac_nhid, push_nhid, arp_pending);
    return RUNOS_OK;

fail_push:
    /* 回滚：先删 push，再删 tunnel（tunnel 无 ref_cnt 时才能删除） */
    FOREACH_UNIT(unit)
    {
        ctcs_nh_remove_mpls(unit, push_nhid);
    }
    hw_nh_remove_mpls_tunnel_label((uint16_t)push_nhid);
fail_ilm:
    sal_memset(&pw, 0, sizeof(pw));
    pw.label = in_label;
    pw.l2vpntype = CTC_MPLS_L2VPN_VPWS;
    FOREACH_UNIT(unit)
    {
        ctcs_mpls_del_l2vpn_pw(unit, &pw);
    }
    return ret;
}

int hw_mpls_del_l2vpn_pw(uint32_t in_label)
{
    uint8 unit;
    int ret = 0;
    int idx;
    ctc_mpls_l2vpn_pw_t pw;
    struct hw_mpls_pw_cache *pc;

    idx = hw_mpls_pw_find(in_label);
    if (idx < 0)
        return RUNOS_OK;
    pc = &g_hw_mpls_pw[idx];

    printk("del l2vpn pw %u: ac_gport=%u push_nhid=%u pending=%u", in_label,
         pc->ac_gport, pc->push_nhid, pc->arp_pending);

    /* 逆序回收：AC vlan mapping -> push nexthop -> VPWS ILM */
    if (pc->ac_gport && pc->push_nhid)
        hw_mpls_pw_ac_mapping_del(pc->ac_gport, pc->push_nhid);

    /* 占位模式下 push_nhid 是 TOCPU 保留 nhid，没有真实 push
     * nexthop 可回收。两步模型下先删 push 再删 tunnel。 */
    if (pc->push_nhid && !pc->arp_pending) {
        FOREACH_UNIT(unit)
        {
            ctcs_nh_remove_mpls(unit, pc->push_nhid);
        }
        hw_nh_remove_mpls_tunnel_label((uint16_t)pc->push_nhid);
    }

    sal_memset(&pw, 0, sizeof(pw));
    pw.label = in_label;
    pw.l2vpntype = CTC_MPLS_L2VPN_VPWS;
    FOREACH_UNIT(unit)
    {
        ret = ctcs_mpls_del_l2vpn_pw(unit, &pw);
        if (ret != CTC_E_NONE)
            break;
    }

    sal_memset(pc, 0, sizeof(*pc));
    return ret;
}

/* ARP 学习后升级 AC->PW 方向的 VPWS push nexthop：把以 TOCPU 占位
 * 挂起的 vlan mapping 替换为指向真实 push nexthop 的条目。与 LSP
 * ILM 占位升级（hw_mpls_arp_resolved）同一事件、同一串行化上下文
 * 调用；只匹配 nhs[0]（push nexthop 只使用首条 PSN 路径）。 */
int hw_mpls_pw_arp_resolved(uint32_t ifindex, uint32_t nexthop)
{
    int i, ret;
    uint8 unit;

    for (i = 0; i < HW_MPLS_PW_MAX; i++) {
        struct hw_mpls_pw_cache *pc = &g_hw_mpls_pw[i];
        uint32_t push_nhid = 0;

        if (!pc->used || !pc->arp_pending || !pc->ac_gport ||
            pc->nh_count == 0)
            continue;
        /* nexthop=0 的 PSN 路径无法匹配 ARP 事件，保持占位挂起 */
        if (pc->nhs[0].ifindex != ifindex ||
            pc->nhs[0].nexthop != nexthop)
            continue;

        /* ARP 就绪：创建真实 VPWS push nexthop */
        ret = hw_mpls_pw_push_nh_create(pc->out_label,
                                        (pc->flags & HAL_MPLS_PW_FLAG_CWORD)
                                            ? 1 : 0,
                                        &pc->nhs[0], &push_nhid);
        if (ret != RUNOS_OK)
            continue; /* 仍未解析：继续占位挂起 */

        /* 用真实映射替换 TOCPU 占位映射 */
        hw_mpls_pw_ac_mapping_del(pc->ac_gport, pc->push_nhid);
        ret = hw_mpls_pw_ac_mapping_add(pc->ac_gport, push_nhid);
        if (ret != RUNOS_OK) {
            printk("pw %u: ARP upgrade mapping failed ret=%d", pc->in_label,
                 ret);
            /* 回滚：先删 push，再删 tunnel */
            FOREACH_UNIT(unit)
            {
                ctcs_nh_remove_mpls(unit, push_nhid);
            }
            hw_nh_remove_mpls_tunnel_label((uint16_t)push_nhid);
            /* 恢复占位状态，等待后续 ARP 事件重试 */
            hw_mpls_pw_ac_mapping_add(pc->ac_gport,
                                      CTC_NH_RESERVED_NHID_FOR_TOCPU);
            continue;
        }

        pc->push_nhid = push_nhid;
        pc->arp_pending = 0;
        printk("pw %u: ARP resolved, upgraded push nhid=%u", pc->in_label,
             push_nhid);
    }
    return RUNOS_OK;
}

/* ------------------------------------------------------------------
 * 统计
 * ------------------------------------------------------------------ */

static int hw_mpls_stats_find(uint32_t label)
{
    int i;

    for (i = 0; i < HW_MPLS_ILM_MAX; i++) {
        if (g_hw_mpls_stats[i].used &&
            g_hw_mpls_stats[i].label == label)
            return i;
    }
    return -1;
}

static int hw_mpls_stats_alloc(uint32_t label, uint32_t stats_id)
{
    int i;

    for (i = 0; i < HW_MPLS_ILM_MAX; i++) {
        if (!g_hw_mpls_stats[i].used) {
            g_hw_mpls_stats[i].label = label;
            g_hw_mpls_stats[i].stats_id = stats_id;
            g_hw_mpls_stats[i].used = 1;
            return i;
        }
    }
    return -1;
}

static void hw_mpls_stats_free(int idx)
{
    if (idx >= 0)
        sal_memset(&g_hw_mpls_stats[idx], 0,
                   sizeof(g_hw_mpls_stats[idx]));
}

/* 把缓存的流统计 id 挂到这个标签的 ILM 上（SYS_MPLS_STATS_EN：芯片
 * statsPtr_f）。该标签未分配过统计时为空操作。 */
static int hw_mpls_stats_attach(uint32_t label, uint8_t spaceid)
{
    uint8 unit;
    int ret = 0;
    int idx = hw_mpls_stats_find(label);
    ctc_mpls_stats_index_t stats_index;

    if (idx < 0)
        return RUNOS_OK;

    memset(&stats_index, 0, sizeof(stats_index));
    stats_index.stats_id = g_hw_mpls_stats[idx].stats_id;
    stats_index.label = label;
    stats_index.spaceid = spaceid;

    FOREACH_UNIT(unit)
    {
        ret = ctcs_mpls_add_stats(unit, &stats_index);
        if (ret < 0)
            break;
    }
    return ret;
}

int hw_mpls_add_stats(uint32_t stats_id, uint32_t label, uint8_t spaceid)
{
    uint8 unit;
    int ret = 0;
    int idx;
    ctc_stats_statsid_t statsid;

    idx = hw_mpls_stats_find(label);
    if (idx >= 0) {
        /* ILM 重建（LSP 更新 / ARP 升级）：重新挂上 id */
        stats_id = g_hw_mpls_stats[idx].stats_id;
    } else if (stats_id == 0) {
        /* 分配流统计 id。与 SAI 层对普通 LSP ILM 使用相同的类型
         * （CTC_STATS_STATSID_TYPE_MPLS，入向，非 VC 标签）。
         *
         * SDK stats_mode 为 CTC_STATS_MODE_USER（宿主 ctc_stats_init
         * 配置）：调用者必须自管 stats_id 池并传入 >=1 的 id，SDK
         * 只做查重；传 0 会被 CTC_MIN_VALUE_CHECK 拒绝
         * （CTC_E_INVALID_PARAM=-31998）。用缓存槽位序号派生 id：
         * 槽位全局唯一，id = idx+1 非 0 且唯一；USER 模式 destroy
         * 不归还 OPF 池，槽位释放后 id 可直接复用。 */
        idx = hw_mpls_stats_alloc(label, 0);
        if (idx < 0) {
            /* 无缓存槽位：没有地方跟踪 id，永远无法释放 */
            printk("hw_mpls: stats cache full label=%u\n", label);
            return RUNOS_FULL;
        }
        memset(&statsid, 0, sizeof(statsid));
        statsid.type = CTC_STATS_STATSID_TYPE_MPLS;
        statsid.dir  = CTC_INGRESS;
        statsid.statsid.is_vc_label = 0;
        statsid.stats_id = (uint32_t)idx + 1;
        FOREACH_UNIT(unit)
        {
            ret = ctcs_stats_create_statsid(unit, &statsid);
            if (ret != CTC_E_NONE) {
                printk("hw_mpls: stats id alloc failed label=%u ret=%d\n",
                       label, ret);
                hw_mpls_stats_free(idx);
                return ret;
            }
        }
        stats_id = statsid.stats_id;
        g_hw_mpls_stats[idx].stats_id = stats_id;
    }
    return hw_mpls_stats_attach(label, spaceid);
}

int hw_mpls_remove_stats(uint8_t spaceid, uint32_t label)
{
    uint8 unit;
    int ret = 0;
    int idx;
    uint32_t stats_id;
    ctc_mpls_stats_index_t stats_index;

    idx = hw_mpls_stats_find(label);
    if (idx < 0)
        return RUNOS_OK; /* 未挂统计 */

    stats_id = g_hw_mpls_stats[idx].stats_id;

    /* 先解绑（stats_id=0 清除 statsPtr_f），再把流统计 id
     * 归还给池 */
    memset(&stats_index, 0, sizeof(stats_index));
    stats_index.spaceid = spaceid;
    stats_index.label = label;
    stats_index.stats_id = 0;
    FOREACH_UNIT(unit)
    {
        ret = ctcs_mpls_remove_stats(unit, &stats_index);
        /* NOT_EXIST：ILM 已先于 STATS_DEL 被 LSP_DEL 删除（zebra
         * 删除顺序固定），statsPtr 绑定随之消失，解绑视为成功，
         * 继续归还 stats id；其余错误仍中止。 */
        if (ret < 0 && ret != CTC_E_NOT_EXIST)
            break;
        ret = 0;
    }
    if (ret < 0) {
        /* 失败时保留缓存表项：释放它会导致 SDK 统计对象泄漏
         * （id 无法再跟踪），芯片 statsPtr 悬空；后续 ADD 会复用
         * 缓存的 id。 */
        printk("hw_mpls: stats detach failed label=%u stats_id=%u ret=%d\n",
               label, stats_id, ret);
        return ret;
    }

    FOREACH_UNIT(unit)
    {
        ret = ctcs_stats_destroy_statsid(unit, stats_id);
        if (ret != CTC_E_NONE)
            break;
    }
    if (ret != CTC_E_NONE) {
        printk("hw_mpls: stats destroy failed label=%u stats_id=%u ret=%d\n",
               label, stats_id, ret);
        return ret;
    }

    hw_mpls_stats_free(idx);
    return RUNOS_OK;
}

/* ------------------------------------------------------------------
 * ILM 属性
 * ------------------------------------------------------------------ */

int hw_mpls_set_ilm_property(ctc_mpls_property_t *p_mpls_pro)
{
    uint8 unit;
    int ret = 0;

    FOREACH_UNIT(unit)
    {
        ret = ctcs_mpls_set_ilm_property(unit, p_mpls_pro);
        if (ret < 0)
            break;
    }
    return ret;
}

int hw_mpls_get_ilm_property(ctc_mpls_property_t *p_mpls_pro)
{
    uint8 unit;
    int ret = 0;

    FOREACH_UNIT(unit)
    {
        ret = ctcs_mpls_get_ilm_property(unit, p_mpls_pro);
        if (ret < 0)
            break;
    }
    return ret;
}

/* ------------------------------------------------------------------
 * ILM 查询
 * ------------------------------------------------------------------ */

int hw_mpls_get_ilm(ctc_mpls_ilm_t *p_mpls_ilm)
{
    uint8 unit;
    int ret = 0;
    uint32_t nh_id[64];

    FOREACH_UNIT(unit)
    {
        ret = ctcs_mpls_get_ilm(unit, nh_id, p_mpls_ilm);
        if (ret < 0)
            break;
    }
    return ret;
}

/* ------------------------------------------------------------------
 * MPLS 隧道标签 Nexthop
 * ------------------------------------------------------------------ */

int hw_nh_add_mpls_tunnel_label(uint16_t tunnel_id,
                ctc_mpls_nexthop_tunnel_param_t *p_nh_param)
{
    uint8 unit;
    int ret = 0;

    FOREACH_UNIT(unit) 
    {
        ret = ctcs_nh_add_mpls_tunnel_label(unit, tunnel_id, p_nh_param);
        if (ret < 0)
            break;
    }
    return ret;
}

int hw_nh_update_mpls_tunnel_label(uint16_t tunnel_id,
                   ctc_mpls_nexthop_tunnel_param_t *p_nh_param)
{
    uint8 unit;
    int ret = 0;

    FOREACH_UNIT(unit)
    {
        ret = ctcs_nh_update_mpls_tunnel_label(unit, tunnel_id, p_nh_param);
        if (ret < 0)
            break;
    }
    return ret;
}

int hw_nh_remove_mpls_tunnel_label(uint16_t tunnel_id)
{
    uint8 unit;
    int ret = 0;

    FOREACH_UNIT(unit)
    {
        ret = ctcs_nh_remove_mpls_tunnel_label(unit, tunnel_id);
        if (ret < 0)
            break;
    }
    return ret;
}

int hw_nh_swap_mpls_tunnel_label(uint16_t old_tunnel_id, uint16_t new_tunnel_id)
{
    uint8 unit;
    int ret = 0;

    FOREACH_UNIT(unit) 
    {
        ret = ctcs_nh_swap_mpls_tunnel_label(unit, old_tunnel_id,
                           new_tunnel_id);
        if (ret < 0)
            break;
    }
    return ret;
}

/* ------------------------------------------------------------------
 * MPLS Nexthop（压标签 / 弹标签）
 * ------------------------------------------------------------------ */

int hw_nh_add_mpls(uint32_t nhid, ctc_mpls_nexthop_param_t *p_nh_param)
{
    uint8 unit;
    int ret = 0;

    FOREACH_UNIT(unit) 
    {
        ret = ctcs_nh_add_mpls(unit, nhid, p_nh_param);
        if (ret < 0)
            break;
    }
    return ret;
}

int hw_nh_update_mpls(uint32_t nhid, ctc_mpls_nexthop_param_t *p_nh_param)
{
    uint8 unit;
    int ret = 0;

    FOREACH_UNIT(unit) 
    {
        ret = ctcs_nh_update_mpls(unit, nhid, p_nh_param);
        if (ret < 0)
            break;
    }
    return ret;
}

int hw_nh_remove_mpls(uint32_t nhid)
{
    uint8 unit;
    int ret = 0;

    FOREACH_UNIT(unit) 
    {
        ret = ctcs_nh_remove_mpls(unit, nhid);
        if (ret < 0)
            break;
    }
    return ret;
}

/* ------------------------------------------------------------------
 * BGP-LU 路由 push nexthop（hw_l3.c 为带标签路由使用）
 *
 * 携带 MPLS 标签的路由（如 BGP-LU）需要 PUSH_OP_ROUTE 的 push
 * nexthop：IP 路由 -> 压标签 -> 转发。
 * ------------------------------------------------------------------ */

int hw_nh_add_route_push(uint32_t label, uint32_t nexthop,
                         uint32_t ifindex, uint8_t *mac,
                         uint32_t *p_nhid)
{
    return hw_mpls_nh_create(label, nexthop, ifindex,
                             CTC_MPLS_NH_PUSH_OP_ROUTE, mac, p_nhid);
}

int hw_nh_del_route_push(uint32_t label, uint32_t ifindex)
{
    return hw_mpls_nh_release(label, ifindex, CTC_MPLS_NH_PUSH_OP_ROUTE);
}

/* 多标签（标签栈）版本：L3 VPN 路由压 [transport, vpn] 双层栈。
 * labels[] 与 FRR label_stack 同序（网络序，外层在前）；双标签时
 * 外层 transport 放 tunnel、内层 VPN 放 push（见 hw_mpls_nh_create_key）。
 * label_num==1 时退化为单标签（BGP-LU）。 */
int hw_nh_add_route_push_stack(const uint32_t *labels, uint8_t label_num,
                               uint32_t nexthop, uint32_t ifindex,
                               uint8_t *mac, uint32_t *p_nhid)
{
    return hw_mpls_nh_create_key(labels, label_num, nexthop, ifindex,
                                 CTC_MPLS_NH_PUSH_OP_ROUTE, mac, p_nhid);
}

int hw_nh_del_route_push_stack(const uint32_t *labels, uint8_t label_num,
                               uint32_t ifindex)
{
    return hw_mpls_nh_release_key(labels, label_num, ifindex,
                                  CTC_MPLS_NH_PUSH_OP_ROUTE);
}

/* ------------------------------------------------------------------
 * ECMP Nexthop
 * ------------------------------------------------------------------ */

int hw_nh_add_ecmp(uint32_t nhid, ctc_nh_ecmp_nh_param_t *p_nh_param)
{
    uint8 unit;
    int ret = 0;

    FOREACH_UNIT(unit) 
    {
        ret = ctcs_nh_add_ecmp(unit, nhid, p_nh_param);
        if (ret < 0)
            break;
    }
    return ret;
}

int hw_nh_update_ecmp(uint32_t nhid, ctc_nh_ecmp_nh_param_t *p_nh_param)
{
    uint8 unit;
    int ret = 0;

    FOREACH_UNIT(unit) 
    {
        ret = ctcs_nh_update_ecmp(unit, nhid, p_nh_param);
        if (ret < 0)
            break;
    }
    return ret;
}

int hw_nh_remove_ecmp(uint32_t nhid)
{
    uint8 unit;
    int ret = 0;

    FOREACH_UNIT(unit) 
    {
        ret = ctcs_nh_remove_ecmp(unit, nhid);
        if (ret < 0)
            break;
    }
    return ret;
}

int hw_nh_add_ip_tunnel(uint32_t nhid, ctc_ip_tunnel_nh_param_t *p_nh_param)
{
    uint8 unit;
    int ret = 0;

    FOREACH_UNIT(unit) 
    {
        ret = ctcs_nh_add_ip_tunnel(unit, nhid, p_nh_param);
        if (ret < 0)
            break;
    }
    return ret;
}

int hw_nh_update_ip_tunnel(uint32_t nhid, ctc_ip_tunnel_nh_param_t *p_nh_param)
{
    uint8 unit;
    int ret = 0;

    FOREACH_UNIT(unit) 
    {
        ret = ctcs_nh_update_ip_tunnel(unit, nhid, p_nh_param);
        if (ret < 0)
            break;
    }
    return ret;
}

int hw_nh_remove_ip_tunnel(uint32_t nhid)
{
    uint8 unit;
    int ret = 0;

    FOREACH_UNIT(unit)
    {
        ret = ctcs_nh_remove_ip_tunnel(unit, nhid);
        if (ret < 0)
            break;
    }
    return ret;
}

/* ------------------------------------------------------------------
 * L3 VPN：VRF LSP（ILM）/ FEC 硬件抽象层实现
 *
 * 与 hsl_mpls_vrf.c 的接口约定（extern 见 hsl_mpls_vrf.c:22-32）：
 *   - in_label / out_label：网络字节序，直传 SDK（与普通 ILM 一致）
 *   - hsl_nexthop.gate.ipv4.s_addr（family==AF_INET）：网络字节序（hsl
 *     层未做转换），此处转主机序后交给 hw_mpls_ilm_build_nh()（其内部
 *     hw_mpls_nh_lookup 再转回网络序查 HSL nexthop 表）。VRF-LSP 出路径
 *     恒 v4 网关（hsl_mpls_vrf_msg 解码固定 family=AF_INET）
 *   - op_type：HSL_MPLS_OP_POP / HSL_MPLS_OP_SWAP（HSL_MPLS_OP_PUSH
 *     属于 FEC/IPUC 出向压标签路径，不走 ILM，见 hw_l3_route_add_vrf）
 *
 * L3VPN ILM 构造（SDK 5.5.3）：type = CTC_MPLS_LABEL_TYPE_L3VPN +
 * id_type = CTC_MPLS_ID_VRF + vrf_id（ilm.vrf_id 与
 * ilm.flw_vrf_srv_aps.vrf_id）。芯片对这类表项设 innerPacketLookup_f
 * = TRUE：弹掉 VC 标签后按内层 IP 在 vrf_id 对应 L3 路由表查找
 * （u4_g2_fid_f = vrf_id）并校验 BOS（sBitCheckEn_f）。vrf_id 与
 * hw_l3.c 的 SDK VRF 映射为 1:1（hw_l3_vrf_id_to_sdk 恒等），故直传。
 * ------------------------------------------------------------------ */

static struct hw_mpls_vrf_ilm_cache *
hw_mpls_vrf_ilm_find(uint32_t vrf_id, uint32_t in_label)
{
    int i;

    for (i = 0; i < HW_MPLS_VRF_ILM_MAX; i++) {
        if (g_hw_mpls_vrf_ilm[i].used &&
            g_hw_mpls_vrf_ilm[i].vrf_id == vrf_id &&
            g_hw_mpls_vrf_ilm[i].in_label == in_label)
            return &g_hw_mpls_vrf_ilm[i];
    }
    return NULL;
}

/* VRF ILM 表项的 (vrf_id, in_label) -> nhid 映射（仿 hw_mpls_ilm_map_set） */
static int hw_mpls_vrf_ilm_map_set(uint32_t vrf_id, uint32_t in_label,
                                   uint32_t nhid, uint8_t is_ecmp,
                                   uint8_t placeholder, uint32_t nh_count,
                                   const struct hw_mpls_ilm_nh *nhs)
{
    int i;
    int free_slot = -1;
    struct hw_mpls_vrf_ilm_cache *c = NULL;

    for (i = 0; i < HW_MPLS_VRF_ILM_MAX; i++) {
        if (g_hw_mpls_vrf_ilm[i].used &&
            g_hw_mpls_vrf_ilm[i].vrf_id == vrf_id &&
            g_hw_mpls_vrf_ilm[i].in_label == in_label) {
            c = &g_hw_mpls_vrf_ilm[i];
            break;
        }
        if (!g_hw_mpls_vrf_ilm[i].used && free_slot < 0)
            free_slot = i;
    }
    if (c == NULL) {
        if (free_slot < 0)
            return RUNOS_FULL;
        c = &g_hw_mpls_vrf_ilm[free_slot];
        c->vrf_id = vrf_id;
        c->in_label = in_label;
        c->used = 1;
    }

    c->nhid = nhid;
    c->is_ecmp = is_ecmp;
    c->placeholder = placeholder;
    /* 始终保存原始路径（不只占位时）：ARP 撤销/端口 down 时
     * hw_mpls_arp_del() 靠 ph_nhs[] 判断哪些 VRF ILM 受影响并回退到
     * TOCPU；ARP 重新解析后 hw_mpls_arp_resolved() 再升级。 */
    c->ph_count = 0;
    if (nhs && nh_count > 0) {
        uint32_t n = nh_count < HW_MPLS_ECMP_MEMBER_MAX ?
                     nh_count : HW_MPLS_ECMP_MEMBER_MAX;
        memcpy(c->ph_nhs, nhs, n * sizeof(struct hw_mpls_ilm_nh));
        c->ph_count = (uint8_t)n;
    }
    return RUNOS_OK;
}

static void hw_mpls_vrf_ilm_map_del(uint32_t vrf_id, uint32_t in_label)
{
    struct hw_mpls_vrf_ilm_cache *c;

    c = hw_mpls_vrf_ilm_find(vrf_id, in_label);
    if (c != NULL)
        sal_memset(c, 0, sizeof(*c));
}

/* 释放绑定在 VRF ILM 上的 push nexthop / ECMP 组（del 使用） */
static void hw_mpls_vrf_ilm_release_nh(uint32_t vrf_id, uint32_t in_label)
{
    struct hw_mpls_vrf_ilm_cache *c;

    c = hw_mpls_vrf_ilm_find(vrf_id, in_label);
    if (c == NULL)
        return;

    if (c->placeholder) {
        /* ARP 占位：没有创建 push NH / ECMP 组 */
    } else if (c->is_ecmp) {
        hw_mpls_ecmp_release(c->nhid);
    } else {
        /* 按 nhid 反查：SWAP 的 push nexthop 命中 g_hw_mpls_nh；POP 复用的
         * IPUC nexthop 反查不到（NOT_FOUND，忽略） */
        hw_mpls_nh_release_by_id(c->nhid);
    }
    hw_mpls_vrf_ilm_map_del(vrf_id, in_label);
}

int hw_mpls_lsp_add_vrf(uint32_t vrf_id, uint32_t in_label,
                        uint32_t out_label, uint8_t op_type,
                        const struct hsl_nexthop *nh_list, uint8_t nh_count)
{
    uint8 unit;
    int ret = 0;
    int i;
    ctc_mpls_ilm_t ilm;
    struct hw_mpls_ilm_nh nhs[HW_MPLS_ECMP_MEMBER_MAX];
    uint8_t  is_ecmp = 0;
    uint8_t  placeholder = 0;
    uint32_t nhid = 0;

    if (vrf_id > HW_MPLS_VRF_ID_MAX || in_label == 0)
        return RUNOS_PARAM;
    if (nh_list == NULL || nh_count == 0)
        return RUNOS_PARAM;
    if (op_type == HSL_MPLS_OP_PUSH) {
        /* PUSH 由 hw_l3_route_add_vrf()（IPUC + push nexthop）承载，
         * 不在 VRF ILM 中创建 */
        return RUNOS_PARAM;
    }
    if (nh_count > HW_MPLS_ECMP_MEMBER_MAX)
        nh_count = HW_MPLS_ECMP_MEMBER_MAX;

    mutex_lock(&hw_mpls_vrf_mutex);

    /* 幂等：同键表项已存在时先释放旧绑定，再按新信息重建 */
    if (hw_mpls_vrf_ilm_find(vrf_id, in_label) != NULL)
        hw_mpls_vrf_ilm_release_nh(vrf_id, in_label);

    for (i = 0; i < nh_count; i++) {
        nhs[i].out_label = out_label;   /* 网络序直传 SDK */
        nhs[i].flags = (op_type == HSL_MPLS_OP_SWAP) ?
                       HAL_MPLS_FLAG_SWAP : HAL_MPLS_FLAG_POP;
        nhs[i].nexthop = ntohl(nh_list[i].gate.ipv4.s_addr); /* 网络序→主机序 */
        nhs[i].ifindex = nh_list[i].ifindex;
    }

    /* vrf_ilm=1：VRF ILM 的 POP 路径保持复用出接口 IPUC nexthop 的原行为
     * （L3VPN 出路径由 ilm.id_type=CTC_MPLS_ID_VRF 走 dsFwd + fid 查表），
     * 只有全局 NORMAL ILM 才需要新建 PHP nexthop。 */
    ret = hw_mpls_ilm_build_nh(in_label, nh_count, nhs, 1, &nhid, &is_ecmp,
                               &placeholder);
    if (ret != RUNOS_OK)
        goto out;

    memset(&ilm, 0, sizeof(ilm));
    ilm.label = in_label;               /* 网络序直传 */
    ilm.gport = 0;
    ilm.type  = CTC_MPLS_LABEL_TYPE_L3VPN;
    ilm.id_type = CTC_MPLS_ID_VRF;
    ilm.vrf_id = (uint16_t)vrf_id;
    ilm.flw_vrf_srv_aps.vrf_id = (uint16_t)vrf_id;
    /* auto(IP)：内层族自适应。egress PE 弹 VC 后按内层实际 ethertype
     * 在 vrf_id（fid）的 IPUC 表查 v4/v6 —— v4/v6 载荷共用一条
     * per-VRF 标签 ILM（与基路径 NORMAL/PHP ILM 的默认语义一致）。 */
    ilm.inner_pkt_type = CTC_MPLS_INNER_IP;
    if (placeholder)
        ilm.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;
    else
        ilm.nh_id = nhid;

    FOREACH_UNIT(unit)
    {
        ret = ctcs_mpls_add_ilm(unit, &ilm);
        if (ret != CTC_E_NONE)
            break;
    }
    if (ret != CTC_E_NONE) {
        if (!placeholder)
            hw_mpls_ilm_undo_nh(nhs, is_ecmp, nhid);
        goto out;
    }

    ret = hw_mpls_vrf_ilm_map_set(vrf_id, in_label, nhid, is_ecmp,
                                  placeholder, nh_count, nhs);
    if (ret != RUNOS_OK) {
        /* 缓存满：回滚已安装的 ILM 与 nexthop */
        int del_ret = 0;
        FOREACH_UNIT(unit)
        {
            del_ret = ctcs_mpls_del_ilm(unit, &ilm);
            if (del_ret != CTC_E_NONE)
                break;
        }
        if (!placeholder)
            hw_mpls_ilm_undo_nh(nhs, is_ecmp, nhid);
        ret = RUNOS_FULL;
        goto out;
    }

    ret = placeholder ? CTC_HAL_RET_MPLS_PLACEHOLDER : RUNOS_OK;
out:
    mutex_unlock(&hw_mpls_vrf_mutex);
    return ret;
}

int hw_mpls_lsp_del_vrf(uint32_t vrf_id, uint32_t in_label)
{
    uint8 unit;
    int ret = 0;
    ctc_mpls_ilm_t ilm;

    if (vrf_id > HW_MPLS_VRF_ID_MAX || in_label == 0)
        return RUNOS_PARAM;

    mutex_lock(&hw_mpls_vrf_mutex);

    if (hw_mpls_vrf_ilm_find(vrf_id, in_label) == NULL) {
        ret = RUNOS_NOT_FOUND;
        goto out;
    }

    memset(&ilm, 0, sizeof(ilm));
    ilm.label = in_label;
    ilm.type  = CTC_MPLS_LABEL_TYPE_L3VPN;
    ilm.id_type = CTC_MPLS_ID_VRF;
    ilm.vrf_id = (uint16_t)vrf_id;
    ilm.flw_vrf_srv_aps.vrf_id = (uint16_t)vrf_id;

    FOREACH_UNIT(unit)
    {
        ret = ctcs_mpls_del_ilm(unit, &ilm);
        if (ret != CTC_E_NONE)
            break;
    }

    /* 无论芯片删除结果如何，都释放缓存的 nexthop 绑定
     * （与 hw_mpls_del_ilm 行为一致） */
    hw_mpls_vrf_ilm_release_nh(vrf_id, in_label);

out:
    mutex_unlock(&hw_mpls_vrf_mutex);
    return ret;
}

/* VRF FEC 软件映射（SDK 5.5.3 无 FTN API） */
static struct hw_mpls_vrf_fec_entry *
hw_mpls_vrf_fec_find(uint32_t vrf_id, uint32_t prefix, uint8_t prefixlen)
{
    int i;

    for (i = 0; i < HW_MPLS_VRF_FEC_MAX; i++) {
        if (g_hw_mpls_vrf_fec[i].used &&
            g_hw_mpls_vrf_fec[i].vrf_id == vrf_id &&
            g_hw_mpls_vrf_fec[i].prefix == prefix &&
            g_hw_mpls_vrf_fec[i].prefixlen == prefixlen)
            return &g_hw_mpls_vrf_fec[i];
    }
    return NULL;
}

/* FEC 绑定：只登记软件映射（BGP 控制面语义）。数据面：
 *  - ingress PE：hw_l3_route_add_vrf() 的 IPUC 路由 + push nexthop
 *  - egress PE：本文件的 L3VPN ILM（弹 VC 标签后内层 IP 查 VRF 表） */
int hw_mpls_fec_bind_vrf(uint32_t vrf_id, const struct in_addr *prefix,
                         uint8_t prefixlen, uint32_t label)
{
    struct hw_mpls_vrf_fec_entry *e;
    int ret = 0;
    int i;
    int free_slot = -1;

    if (vrf_id > HW_MPLS_VRF_ID_MAX || prefix == NULL)
        return RUNOS_PARAM;

    mutex_lock(&hw_mpls_vrf_mutex);

    e = hw_mpls_vrf_fec_find(vrf_id, prefix->s_addr, prefixlen);
    if (e == NULL) {
        for (i = 0; i < HW_MPLS_VRF_FEC_MAX; i++) {
            if (!g_hw_mpls_vrf_fec[i].used) {
                free_slot = i;
                break;
            }
        }
        if (free_slot < 0) {
            ret = RUNOS_FULL;
            goto out;
        }
        e = &g_hw_mpls_vrf_fec[free_slot];
        e->vrf_id = vrf_id;
        e->prefix = prefix->s_addr;
        e->prefixlen = prefixlen;
        e->used = 1;
    }

    /* 重复绑定：更新标签（网络序） */
    e->label = label;

    ret = RUNOS_OK;
out:
    mutex_unlock(&hw_mpls_vrf_mutex);
    return ret;
}

int hw_mpls_fec_unbind_vrf(uint32_t vrf_id, const struct in_addr *prefix,
                           uint8_t prefixlen)
{
    struct hw_mpls_vrf_fec_entry *e;

    if (vrf_id > HW_MPLS_VRF_ID_MAX || prefix == NULL)
        return RUNOS_PARAM;

    mutex_lock(&hw_mpls_vrf_mutex);

    e = hw_mpls_vrf_fec_find(vrf_id, prefix->s_addr, prefixlen);
    if (e == NULL) {
        mutex_unlock(&hw_mpls_vrf_mutex);
        return RUNOS_NOT_FOUND;
    }
    sal_memset(e, 0, sizeof(*e));

    mutex_unlock(&hw_mpls_vrf_mutex);
    return RUNOS_OK;
}