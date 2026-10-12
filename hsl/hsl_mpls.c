/*
 * hsl_mpls.c -- HSL（硬件服务层）MPLS 命令处理
 *
 * 本文件接收 zebra/ctc_dplane.c 发送的 HAL 消息，用 basic 类型参数
 * 分发给 hw_mpls_* SDK 封装函数。
 */

#include "hsl.h"
#include "hsl_avl.h"
#include "hsl_module.h"
#include "hal_msg.h"
#include <def_mpls.h>
#include "hw.h"
#include "hsl_mpls_vrf.h"


/* ------------------------------------------------------------------
 * hw_mpls API 前置声明（仅 basic 类型）
 * ------------------------------------------------------------------ */

/* 多路径 ILM nexthop 表项（必须与 hw_mpls.c 中的定义保持同步；
 * 在这里保留一份，以便 hsl_mpls.c 能把 HAL 消息解包成 basic 类型）。 */
struct hw_mpls_ilm_nh {
    uint32_t out_label;
    uint32_t flags;
    uint32_t nexthop;
    uint32_t ifindex;
};

/* VPWS PW 的 PSN 隧道路径（与 hw_mpls.c 中定义同步，
 * 用于把 HAL 消息解包成 basic 类型参数） */
struct hw_mpls_pw_nh {
    uint32_t tunnel_label;
    uint32_t nexthop;
    uint32_t ifindex;
};

extern void hw_mpls_init(void);
extern void hw_mpls_deinit(void);
extern int  hw_mpls_add_ilm(uint32_t in_label, uint32_t nh_count,
                const struct hw_mpls_ilm_nh *nhs);
extern int  hw_mpls_del_ilm(uint32_t in_label);
extern int  hw_mpls_update_ilm(uint32_t in_label, uint32_t nh_count,
                 const struct hw_mpls_ilm_nh *nhs);
extern int  hw_mpls_add_l2vpn_pw(uint32_t in_label, uint32_t out_label,
                uint32_t flags, uint32_t pw_type,
                uint32_t ac_ifindex, uint32_t nh_count,
                const struct hw_mpls_pw_nh *nhs);
extern int  hw_mpls_del_l2vpn_pw(uint32_t in_label);
extern int  hw_mpls_add_stats(uint32_t stats_id, uint32_t label,
                  uint8_t spaceid);
extern int  hw_mpls_remove_stats(uint8_t spaceid, uint32_t label);
extern int  hw_mpls_set_ilm_property(void *p_mpls_pro);
extern int  hw_mpls_get_ilm_property(void *p_mpls_pro);
extern int  hw_mpls_get_ilm(void *p_mpls_ilm);

/* hw_nexthop API 前置声明 */
extern int  hw_nh_add_mpls_tunnel_label(uint16_t tunnel_id,
                    void *p_nh_param);
extern int  hw_nh_update_mpls_tunnel_label(uint16_t tunnel_id,
                      void *p_nh_param);
extern int  hw_nh_remove_mpls_tunnel_label(uint16_t tunnel_id);
extern int  hw_nh_swap_mpls_tunnel_label(uint16_t old_tunnel_id,
                     uint16_t new_tunnel_id);
extern int  hw_nh_add_mpls(uint32_t nhid, void *p_nh_param);
extern int  hw_nh_update_mpls(uint32_t nhid, void *p_nh_param);
extern int  hw_nh_remove_mpls(uint32_t nhid);
extern int  hw_nh_add_ecmp(uint32_t nhid, void *p_nh_param);
extern int  hw_nh_update_ecmp(uint32_t nhid, void *p_nh_param);
extern int  hw_nh_remove_ecmp(uint32_t nhid);
extern int  hw_nh_add_ip_tunnel(uint32_t nhid, void *p_nh_param);
extern int  hw_nh_update_ip_tunnel(uint32_t nhid, void *p_nh_param);
extern int  hw_nh_remove_ip_tunnel(uint32_t nhid);


/* ------------------------------------------------------------------
 * HSL 消息处理
 * ------------------------------------------------------------------ */

int hsl_mpls_msg(hsl_msg_t *msg)
{
    struct ctc_hal_msg_mpls_lsp *lsp_msg = NULL;
    struct ctc_hal_msg_mpls_stats *stats_msg = NULL;
    struct ctc_hal_msg_mpls_pw *pw_msg = NULL;
    struct ctc_hal_msg_nexthop *nh_msg = NULL;
    struct hw_mpls_ilm_nh lsp_nhs[CTC_HAL_MSG_MAX_LSP_NH];
    struct hw_mpls_pw_nh pw_nhs[CTC_HAL_MSG_MAX_LSP_NH];
    uint32_t lsp_nh_count = 0;
    uint32_t pw_nh_count = 0;
    int ret = 0;
    int i;
    
    if (!msg) {
        if(HSL_DEBUG_CHECK(HSL_DBG_MPLS)) {
            printk("hsl_mpls_msg para is null.\n");
        }
        return -1;
    }
    lsp_msg = HSL_MSG_DATA(msg);
    stats_msg = HSL_MSG_DATA(msg);
    pw_msg = HSL_MSG_DATA(msg);
    nh_msg = HSL_MSG_DATA(msg);
    if(HSL_DEBUG_CHECK(HSL_DBG_MPLS)) {
        printk("hsl_mpls_msg cmd=%d.\n", msg->cmd);
    }
    switch (msg->cmd) {
    case HAL_MSG_MPLS_LSP_ADD:
    case HAL_MSG_MPLS_LSP_UPDATE:
        /* 把逐路径 nexthop 数组解包成 basic 类型 */
        lsp_nh_count = lsp_msg->nh_count;
        if (lsp_nh_count > CTC_HAL_MSG_MAX_LSP_NH)
            lsp_nh_count = CTC_HAL_MSG_MAX_LSP_NH;
        for (i = 0; i < lsp_nh_count; i++) {
            lsp_nhs[i].out_label = lsp_msg->nh[i].out_label;
            lsp_nhs[i].flags     = lsp_msg->nh[i].flags;
            lsp_nhs[i].nexthop   = ntohl(lsp_msg->nh[i].nexthop.s_addr);
            lsp_nhs[i].ifindex   = lsp_msg->nh[i].ifindex;
        }
        if (msg->cmd == HAL_MSG_MPLS_LSP_ADD)
            ret = hw_mpls_add_ilm(lsp_msg->in_label, lsp_nh_count, lsp_nhs);
        else
            ret = hw_mpls_update_ilm(lsp_msg->in_label, lsp_nh_count, lsp_nhs);
        break;
    case HAL_MSG_MPLS_LSP_DEL:
        ret = hw_mpls_del_ilm(lsp_msg->in_label);
        break;

    case HAL_MSG_MPLS_LSP_VRF_ADD:
    case HAL_MSG_MPLS_LSP_VRF_DEL:
        /* VRF LSP (ILM) 消息：解包 ctc_hal_msg_mpls_lsp_vrf 并下发 */
        ret = hsl_mpls_vrf_msg(msg);
        break;

    case HAL_MSG_MPLS_PW_ADD:
        /* 解包 VPWS PW 消息：VC 标签、PW 类型、AC 关联口与 PSN 隧道路径 */
        pw_nh_count = pw_msg->nh_count;
        if (pw_nh_count > CTC_HAL_MSG_MAX_LSP_NH)
            pw_nh_count = CTC_HAL_MSG_MAX_LSP_NH;
        for (i = 0; i < pw_nh_count; i++) {
            pw_nhs[i].tunnel_label = pw_msg->nh[i].tunnel_label;
            pw_nhs[i].nexthop      = ntohl(pw_msg->nh[i].nexthop.s_addr);
            pw_nhs[i].ifindex      = pw_msg->nh[i].ifindex;
        }
        printk("PW_ADD in_label=%u out_label=%u type=0x%x flags=0x%x ac_ifindex=%u nh=%u",
             pw_msg->in_label, pw_msg->out_label, pw_msg->pw_type,
             pw_msg->flags, pw_msg->ac_ifindex, pw_nh_count);
        ret = hw_mpls_add_l2vpn_pw(pw_msg->in_label, pw_msg->out_label,
                    pw_msg->flags, pw_msg->pw_type,
                    pw_msg->ac_ifindex, pw_nh_count, pw_nhs);
        break;

    case HAL_MSG_MPLS_PW_DEL:
        printk("PW_DEL in_label=%u", pw_msg->in_label);
        ret = hw_mpls_del_l2vpn_pw(pw_msg->in_label);
        break;

    case HAL_MSG_MPLS_STATS_ADD:
        ret = hw_mpls_add_stats(stats_msg->stats_id, stats_msg->label,
                    stats_msg->spaceid);
        break;

    case HAL_MSG_MPLS_STATS_DEL:
        ret = hw_mpls_remove_stats(stats_msg->spaceid, stats_msg->label);
        break;
        

    case HAL_MSG_NH_MPLS_TUNNEL_ADD:
        ret = hw_nh_add_mpls_tunnel_label((uint16_t)nh_msg->nhid,
                          nh_msg->param);
        break;

    case HAL_MSG_NH_MPLS_TUNNEL_DEL:
        ret = hw_nh_remove_mpls_tunnel_label((uint16_t)nh_msg->nhid);
        break;

    case HAL_MSG_NH_MPLS_ADD:
        ret = hw_nh_add_mpls(nh_msg->nhid, nh_msg->param);
        break;

    case HAL_MSG_NH_MPLS_DEL:
        ret = hw_nh_remove_mpls(nh_msg->nhid);
        break;

    case HAL_MSG_NH_MPLS_UPDATE:
        ret = hw_nh_update_mpls(nh_msg->nhid, nh_msg->param);
        break;

    case HAL_MSG_NH_IPUC_ADD:
        /* IPUC nexthop 由路由模块（hw_l3.c）处理 */
        if(HSL_DEBUG_CHECK(HSL_DBG_MPLS)) {
            printk("hsl_mpls: NH_IPUC_ADD delegated to route module.\n");
        }
        break;

    case HAL_MSG_NH_IPUC_DEL:
        if(HSL_DEBUG_CHECK(HSL_DBG_MPLS)) {
            printk("hsl_mpls: NH_IPUC_DEL delegated to route module.\n");
        }
        break;

    case HAL_MSG_NH_ECMP_ADD:
        ret = hw_nh_add_ecmp(nh_msg->nhid, nh_msg->param);
        break;

    case HAL_MSG_NH_ECMP_DEL:
        ret = hw_nh_remove_ecmp(nh_msg->nhid);
        break;

    case HAL_MSG_NH_IP_TUNNEL_ADD:
        ret = hw_nh_add_ip_tunnel(nh_msg->nhid, nh_msg->param);
        break;

    case HAL_MSG_NH_IP_TUNNEL_DEL:
        ret = hw_nh_remove_ip_tunnel(nh_msg->nhid);
        break;
        
    default:
        if(HSL_DEBUG_CHECK(HSL_DBG_MPLS)) {
            printk("hsl_mpls_msg cmd %d is unkown.\n", msg->cmd);
        }
        ret = -1;
        break;
    }
    if(HSL_DEBUG_CHECK(HSL_DBG_MPLS)) {
        printk("hsl_mpls_msg cmd %d ret %d.\n", msg->cmd, ret);
    }
    /* 回写处理结果，使同步 hal_cmd_send()/ioctl() 调用方能信任
     * msg.ret。 */
    hsl_msg_output(msg, ret, NULL, 0);
    return ret;
}

/* ------------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------------ */

int hsl_mpls_init(void)
{
    int ret;

    hw_mpls_init();

    ret = hsl_cmd_module_register(HAL_MODULE_MPLS, hsl_mpls_msg);

    return ret;
}

