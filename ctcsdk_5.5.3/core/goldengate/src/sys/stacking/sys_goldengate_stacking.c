/**
 @file sys_goldengate_stacking.c

 @date 2010-3-9

 @version v2.0

  This file contains stakcing sys layer function implementation
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_stacking.h"

#include "sal.h"
#include "ctc_const.h"
#include "ctc_macro.h"
#include "ctc_vector.h"
#include "ctc_linklist.h"
#include "ctc_error.h"
#include "ctc_warmboot.h"

#include "sys_goldengate_opf.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_stacking.h"

#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_nexthop_hw.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_linkagg.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_wb_common.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_packet.h"

#include "goldengate/include/drv_io.h"
#include "goldengate/include/drv_lib.h"

/****************************************************************************
*
* Defines and Macros
*
*****************************************************************************/
#define SYS_STK_TRUNK_MEMBERS 512
#define SYS_STK_TRUNK_DLB_MAX_MEMBERS 16
#define SYS_STK_TRUNK_MAX_MEMBERS 32
#define SYS_STK_TRUNK_STATIC_MAX_MEMBERS 24

#define SYS_STK_TRUNK_MIN_ID 1
#define SYS_STK_TRUNK_MAX_ID 63

#define SYS_STK_TRUNK_VEC_BLOCK_NUM  64
#define SYS_STK_TRUNK_VEC_BLOCK_SIZE 8

#define SYS_STK_KEEPLIVE_VEC_BLOCK_SIZE 1024
#define SYS_STK_KEEPLIVE_MEM_MAX 3

#define SYS_STK_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == g_gg_stacking_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define SYS_STK_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(stacking, stacking, STK_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0);

#define SYS_STK_DBG_FUNC()           SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_FUNC,  "%s()\n", __FUNCTION__)
#define SYS_STK_DBG_DUMP(FMT, ...)  SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,  FMT, ##__VA_ARGS__)
#define SYS_STK_DBG_INFO(FMT, ...)  SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO,  FMT, ##__VA_ARGS__)
#define SYS_STK_DBG_PARAM(FMT, ...) SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ##__VA_ARGS__)

#define STACKING_LOCK \
    if (g_gg_stacking_master[lchip]->p_stacking_mutex) sal_mutex_lock(g_gg_stacking_master[lchip]->p_stacking_mutex)
#define STACKING_UNLOCK \
    if (g_gg_stacking_master[lchip]->p_stacking_mutex) sal_mutex_unlock(g_gg_stacking_master[lchip]->p_stacking_mutex)

/****************************************************************************
*
* Global and Declaration
*
*****************************************************************************/

enum sys_stacking_opf_type_e
{
    SYS_STK_OPF_MEM_BASE,
    SYS_STK_OPF_MCAST_PROFILE,
    SYS_STK_OPF_MAX
};
typedef enum sys_stacking_opf_type_e sys_stacking_opf_type_t;


enum sys_stacking_mux_type_e
{
    SYS_STK_MUX_TYPE_HDR_REGULAR_PORT     = 0,  /* 4'b0000: Regular port(No MUX)           */
    SYS_STK_MUX_TYPE_HDR_WITHOUT_TUNNEL   = 6,  /* 4'b0110: BridgeHeader without tunnel    */
    SYS_STK_MUX_TYPE_HDR_WITH_L2          = 7,  /* 4'b0111: BridgeHeader with L2           */
    SYS_STK_MUX_TYPE_HDR_WITH_L2_AND_IPV4 = 8,  /* 4'b1000: BridgeHeader with L2 + IPv4    */
    SYS_STK_MUX_TYPE_HDR_WITH_L2_AND_IPV6 = 9,  /* 4'b1001: BridgeHeader with L2 + IPv6    */
    SYS_STK_MUX_TYPE_HDR_WITH_IPV4        = 10, /* 4'b1010: BridgeHeader IPv4              */
    SYS_STK_MUX_TYPE_HDR_WITH_IPV6        = 11, /* 4'b1011: BridgeHeader IPv6              */
    SYS_STK_MUX_TYPE_RESERVED             = 12  /* 4'b1100 ~ 4'b1101: Reserved             */
};
typedef enum mux_header_adjust_type_e mux_header_adjust_type_t;

enum sys_stacking_out_encap_e
{
    SYS_STK_OUT_ENCAP_NONE                = 0,  /* 4'b0000: BridgeHeader with no ipv4/ipv6   */
    SYS_STK_OUT_ENCAP_IPV4                = 1,  /* 4'b0001: BridgeHeader with ipv4           */
    SYS_STK_OUT_ENCAP_IPV6                = 2  /* 4'b0002: BridgeHeader with ipv6           */
};
typedef enum sys_stacking_out_encap_e sys_stacking_out_encap_t;


struct sys_stacking_trunk_info_s
{
    ctc_list_pointer_node_t list_head;
    uint8 trunk_id;
    uint8 mem_cnt;
    uint8 max_mem_cnt;
    uint8 mode;

    uint32 mem_base;
    uint32 mem_ports[SYS_STK_TRUNK_MAX_MEMBERS];
    uint32 rgchip_bitmap;
    ctc_stacking_hdr_encap_t encap_hdr;

    uint32 flag;
    uint8  replace_en;
};
typedef struct sys_stacking_trunk_info_s sys_stacking_trunk_info_t;

struct sys_stacking_trunk_bitmap_s
{
    uint32 bit0_31;
    uint32 bit32_63;
};
typedef struct sys_stacking_trunk_bitmap_s sys_stacking_trunk_bitmap_t;

struct sys_stacking_mcast_db_s
{
    uint8   type;
    uint8   rsv;
    uint16  id;

    uint32   head_met_offset;
    uint32   tail_met_offset;

    uint8 append_en;
    uint8 alloc_id;
    uint32 last_tail_offset;

    uint32   trunk_bitmap[CTC_STK_TRUNK_BMP_NUM];
};
typedef struct sys_stacking_mcast_db_s sys_stacking_mcast_db_t;



struct sys_stacking_master_s
{
    uint8 edit_mode;
    uint8 stacking_en;
    uint8 ipv4_encap;
    uint8 trunk_num;
    uint8 mcast_mode;/*0: add trunk to mcast group auto; 1: add trunk to mcast group by user*/
    uint8 binding_trunk;/*0: binding port to trunk auto when add port to trunk; 1: binding by user*/
    uint8 opf_type;
    uint8 trunk_mode;
    uint8 bind_mcast_en;
    uint32 stacking_mcast_offset;
    ctc_hash_t* mcast_hash;
    ctc_vector_t* p_trunk_vec;
    uint8 neigbor_chip[CTC_MAX_CHIP_NUM];
    sal_mutex_t* p_stacking_mutex;
    sys_stacking_mcast_db_t *p_default_prof_db;

};
typedef struct sys_stacking_master_s sys_stacking_master_t;

#define SYS_STK_TRUNKID_RANGE_CHECK(val) \
    { \
        if ((val) < SYS_STK_TRUNK_MIN_ID || (val) > SYS_STK_TRUNK_MAX_ID){ \
            return CTC_E_STK_INVALID_TRUNK_ID; } \
    }

#define SYS_STK_DSCP_CHECK(dscp) \
    { \
        if ((dscp) > 63){ \
            return CTC_E_STK_TRUNK_HDR_DSCP_NOT_VALID; } \
    }

#define SYS_STK_COS_CHECK(dscp) \
    { \
        if ((dscp) > 7){ \
            return CTC_E_STK_TRUNK_HDR_COS_NOT_VALID; } \
    }

#define SYS_STK_LPORT_CHECK(lport) \
    { \
        if ((lport&0x3F) >= 64){ \
            return CTC_E_STK_TRUNK_MEMBER_PORT_INVALID; } \
    }


#define SYS_STK_EXTEND_EN(extend) \
    { \
        if (extend.enable) \
        { \
        } \
    }

sys_stacking_master_t* g_gg_stacking_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
extern int32 sys_goldengate_l2_reserve_rchip_ad_index(uint8 lchip, uint8 gchip, uint16 max_port_num, uint8 slice_en);
extern int32  sys_goldengate_l2_free_rchip_ad_index(uint8 lchip, uint8 gchip);
/****************************************************************************
*
* Function
*
**
***************************************************************************/
STATIC int32
_sys_goldengate_stacking_failover_add_member(uint8 lchip, uint8 trunk_id, uint16 gport)
{
    uint32 cmd = 0;
    DsLinkAggregateChannel_m linkagg_channel;
    uint32 channel_id = 0;

    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "tid = 0x%x gport =  %d \n", trunk_id, gport);

    sal_memset(&linkagg_channel, 0, sizeof(linkagg_channel));

    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);

    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));
    SetDsLinkAggregateChannel(V, groupType_f, &linkagg_channel, 1);
    SetDsLinkAggregateChannel(V, groupEn_f, &linkagg_channel, 1);
    SetDsLinkAggregateChannel(V, u1_g2_linkAggregationChannelGroup_f, &linkagg_channel, trunk_id);

    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_stacking_failover_del_member(uint8 lchip, uint8 trunk_id, uint16 gport)
{
    uint32 cmd = 0;
    DsLinkAggregateChannel_m linkagg_channel;
    uint8 channel_id = 0;

    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "tid = 0x%x gport =  %d \n", trunk_id, gport);

    sal_memset(&linkagg_channel, 0, sizeof(linkagg_channel));

    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);

    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));
    SetDsLinkAggregateChannel(V, groupType_f, &linkagg_channel, 0);
    SetDsLinkAggregateChannel(V, groupEn_f, &linkagg_channel, 0);

    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stacking_dlb_add_member_channel(uint8 lchip, uint8 trunk_id, uint16 channel_id)
{
    uint32 cmd = 0;
    DsLinkAggregateChannel_m linkagg_channel;

    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "trunk_id = 0x%x channel_id =  %d \n", trunk_id, channel_id);

    sal_memset(&linkagg_channel, 0, sizeof(linkagg_channel));

    SetDsLinkAggregateChannel(V, groupEn_f, &linkagg_channel, 1);
    SetDsLinkAggregateChannel(V, groupType_f, &linkagg_channel, 1);
    SetDsLinkAggregateChannel(V, linkChangeEn_f, &linkagg_channel, 0);
    SetDsLinkAggregateChannel(V, u1_g2_linkAggregationChannelGroup_f, &linkagg_channel, trunk_id);

    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stacking_dlb_del_member_channel(uint8 lchip, uint8 trunk_id, uint16 channel_id)
{
    uint32 cmd = 0;
    DsLinkAggregateChannel_m linkagg_channel;

    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "trunk_id = 0x%x channel_id =  %d \n", trunk_id, channel_id);

    sal_memset(&linkagg_channel, 0, sizeof(linkagg_channel));

    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stacking_dlb_add_member(uint8 lchip, uint8 trunk_id, uint8 port_index, uint16 channel_id)
{
    uint32 cmd = 0;
    uint32 field_value = 0;

    cmd = DRV_IOW(DsLinkAggregateChannelMemberSet_t, DsLinkAggregateChannelMemberSet_array_0_destChannel_f + port_index);
    field_value = channel_id;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, trunk_id, cmd, &field_value));

    CTC_ERROR_RETURN(_sys_goldengate_stacking_dlb_add_member_channel(lchip, trunk_id, channel_id));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stacking_dlb_del_member(uint8 lchip, uint8 trunk_id, uint8 port_index, uint8 tail_index, uint16 channel_id)
{
    uint32 cmd = 0;
    uint32 field_value = 0;

    if (tail_index >= SYS_STK_TRUNK_DLB_MAX_MEMBERS)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    /* copy the last one to the removed position */
    field_value = channel_id;
    cmd = DRV_IOR(DsLinkAggregateChannelMemberSet_t, DsLinkAggregateChannelMemberSet_array_0_destChannel_f+tail_index);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, trunk_id, cmd, &field_value));

    cmd = DRV_IOW(DsLinkAggregateChannelMemberSet_t, DsLinkAggregateChannelMemberSet_array_0_destChannel_f+port_index);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, trunk_id, cmd, &field_value));

    CTC_ERROR_RETURN(_sys_goldengate_stacking_dlb_del_member_channel(lchip, trunk_id, channel_id));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stacking_clear_flow_active(uint8 lchip, uint8 trunk_id)
{
    uint32 cmd_r = 0;
    uint32 cmd_w = 0;
    DsLinkAggregateChannelGroup_m ds_link_aggregate_channel_group;
    DsLinkAggregateChannelMember_m ds_link_aggregate_channel_member;
    uint16 flow_num = 0;
    uint32 flow_base = 0;
    uint16 index = 0;

    sal_memset(&ds_link_aggregate_channel_group, 0, sizeof(DsLinkAggregateChannelGroup_m));
    sal_memset(&ds_link_aggregate_channel_member, 0, sizeof(DsLinkAggregateChannelMember_m));

    cmd_r = DRV_IOR(DsLinkAggregateChannelGroup_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, trunk_id, cmd_r, &ds_link_aggregate_channel_group));

    flow_base = GetDsLinkAggregateChannelGroup(V, channelLinkAggMemBase_f, &ds_link_aggregate_channel_group);

    flow_num = 32;

    /* clear active */
    cmd_r = DRV_IOR(DsLinkAggregateChannelMember_t, DRV_ENTRY_FLAG);
    cmd_w = DRV_IOW(DsLinkAggregateChannelMember_t, DRV_ENTRY_FLAG);

    for (index = 0; index < flow_num; index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, flow_base+index, cmd_r, &ds_link_aggregate_channel_member));
        SetDsLinkAggregateChannelMember(V, active_f, &ds_link_aggregate_channel_member, 0);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, flow_base+index, cmd_w, &ds_link_aggregate_channel_member));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_stacking_get_rsv_trunk_number(uint8 lchip, uint8* number)
{
    CTC_PTR_VALID_CHECK(number);

    if (NULL == g_gg_stacking_master[lchip] || g_gg_stacking_master[lchip]->trunk_num == 0)
    {
        *number = 0;
        return CTC_E_NONE;
    }

    if (g_gg_stacking_master[lchip]->trunk_num > 2)
    {
        *number = 8;
    }
    else
    {
        *number = 2;
    }

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_stacking_hdr_set(uint8 lchip, ctc_stacking_hdr_glb_t* p_hdr_glb)
{
    IpeHeaderAdjustCtl_m ipe_header_adjust_ctl;
    EpePktHdrCtl_m epe_pkt_hdr_ctl;
    uint32 cmd  = 0;
    uint32 ipv6_sa[CTC_IPV6_ADDR_LEN] = {0};

    SYS_STK_DSCP_CHECK(p_hdr_glb->ip_dscp);
    SYS_STK_COS_CHECK(p_hdr_glb->cos);

    sal_memset(&ipe_header_adjust_ctl, 0, sizeof(ipe_header_adjust_ctl));
    cmd = DRV_IOR(IpeHeaderAdjustCtl_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_header_adjust_ctl));

    sal_memset(&epe_pkt_hdr_ctl, 0, sizeof(epe_pkt_hdr_ctl));
    cmd = DRV_IOR(EpePktHdrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_hdr_ctl));

    SetIpeHeaderAdjustCtl(V, headerMacDaCheckEn_f, &ipe_header_adjust_ctl, p_hdr_glb->mac_da_chk_en);
    SetIpeHeaderAdjustCtl(V, headerTpid_f, &ipe_header_adjust_ctl, p_hdr_glb->vlan_tpid);
    SetIpeHeaderAdjustCtl(V, headerEtherTypeCheckDisable_f, &ipe_header_adjust_ctl, p_hdr_glb->ether_type_chk_en?0:1);
    SetIpeHeaderAdjustCtl(V, headerEtherType_f, &ipe_header_adjust_ctl, p_hdr_glb->ether_type);
    SetIpeHeaderAdjustCtl(V, headerIpProtocol_f, &ipe_header_adjust_ctl, p_hdr_glb->ip_protocol);
    SetIpeHeaderAdjustCtl(V, headerUdpEn_f, &ipe_header_adjust_ctl, p_hdr_glb->udp_en);
    SetIpeHeaderAdjustCtl(V, udpSrcPort_f, &ipe_header_adjust_ctl, p_hdr_glb->udp_dest_port);
    SetIpeHeaderAdjustCtl(V, udpDestPort_f, &ipe_header_adjust_ctl, p_hdr_glb->udp_src_port);

    SetEpePktHdrCtl(V, tpid_f, &epe_pkt_hdr_ctl, p_hdr_glb->vlan_tpid);
    SetEpePktHdrCtl(V, headerEtherType_f, &epe_pkt_hdr_ctl, p_hdr_glb->ether_type);
    SetEpePktHdrCtl(V, headerIpProtocol_f, &epe_pkt_hdr_ctl, p_hdr_glb->ip_protocol);
    SetEpePktHdrCtl(V, headerTtl_f, &epe_pkt_hdr_ctl, p_hdr_glb->ip_ttl);
    SetEpePktHdrCtl(V, headerDscp_f, &epe_pkt_hdr_ctl, p_hdr_glb->ip_dscp);
    SetEpePktHdrCtl(V, headerCos_f, &epe_pkt_hdr_ctl, p_hdr_glb->cos);

    g_gg_stacking_master[lchip]->ipv4_encap = p_hdr_glb->is_ipv4;

    if (p_hdr_glb->is_ipv4)
    {
            SetEpePktHdrCtl(V, headerIpSa_f, &epe_pkt_hdr_ctl, p_hdr_glb->ipsa.ipv4);
    }
    else
    {
        ipv6_sa[0] = p_hdr_glb->ipsa.ipv6[3];
        ipv6_sa[1] = p_hdr_glb->ipsa.ipv6[2];
        ipv6_sa[2] = p_hdr_glb->ipsa.ipv6[1];
        ipv6_sa[3] = p_hdr_glb->ipsa.ipv6[0];
        SetEpePktHdrCtl(A, headerIpSa_f, &epe_pkt_hdr_ctl, ipv6_sa);
    }

    SetEpePktHdrCtl(V, headerUdpEn_f, &epe_pkt_hdr_ctl, p_hdr_glb->udp_en);
    SetEpePktHdrCtl(V, useUdpPayloadLength_f, &epe_pkt_hdr_ctl, 1);
    SetEpePktHdrCtl(V, useIpPayloadLength_f, &epe_pkt_hdr_ctl, 1);
    SetEpePktHdrCtl(V, udpSrcPort_f, &epe_pkt_hdr_ctl, p_hdr_glb->udp_src_port);
    SetEpePktHdrCtl(V, udpDestPort_f, &epe_pkt_hdr_ctl, p_hdr_glb->udp_dest_port);

    /*Encap header param*/
    cmd = DRV_IOW(IpeHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_header_adjust_ctl));

    cmd = DRV_IOW(EpePktHdrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_hdr_ctl));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &epe_pkt_hdr_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stacking_hdr_get(uint8 lchip, ctc_stacking_hdr_glb_t* p_hdr_glb)
{

    IpeHeaderAdjustCtl_m ipe_header_adjust_ctl;
    EpePktHdrCtl_m epe_pkt_hdr_ctl;
    uint32 cmd  = 0;
    uint32 ipv6_sa[CTC_IPV6_ADDR_LEN] = {0};

    sal_memset(&ipe_header_adjust_ctl, 0, sizeof(ipe_header_adjust_ctl));
    cmd = DRV_IOR(IpeHeaderAdjustCtl_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_header_adjust_ctl));

    sal_memset(&epe_pkt_hdr_ctl, 0, sizeof(epe_pkt_hdr_ctl));
    cmd = DRV_IOR(EpePktHdrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_hdr_ctl));

    GetIpeHeaderAdjustCtl(V, headerMacDaCheckEn_f, &ipe_header_adjust_ctl);
    GetIpeHeaderAdjustCtl(V, headerTpid_f, &ipe_header_adjust_ctl);
    GetIpeHeaderAdjustCtl(V, headerEtherTypeCheckDisable_f, &ipe_header_adjust_ctl);
    GetIpeHeaderAdjustCtl(V, headerEtherType_f, &ipe_header_adjust_ctl);
    GetIpeHeaderAdjustCtl(V, headerIpProtocol_f, &ipe_header_adjust_ctl);
    GetIpeHeaderAdjustCtl(V, headerUdpEn_f, &ipe_header_adjust_ctl);
    GetIpeHeaderAdjustCtl(V, udpSrcPort_f, &ipe_header_adjust_ctl);
    GetIpeHeaderAdjustCtl(V, udpDestPort_f, &ipe_header_adjust_ctl);

    p_hdr_glb->mac_da_chk_en     = GetIpeHeaderAdjustCtl(V, headerMacDaCheckEn_f, &ipe_header_adjust_ctl);
    p_hdr_glb->vlan_tpid         = GetIpeHeaderAdjustCtl(V, headerTpid_f, &ipe_header_adjust_ctl);
    p_hdr_glb->ether_type_chk_en = GetIpeHeaderAdjustCtl(V, headerEtherTypeCheckDisable_f, &ipe_header_adjust_ctl) ? 0 : 1;
    p_hdr_glb->ether_type        = GetIpeHeaderAdjustCtl(V, headerEtherType_f, &ipe_header_adjust_ctl);

    p_hdr_glb->ip_protocol   = GetIpeHeaderAdjustCtl(V, headerIpProtocol_f, &ipe_header_adjust_ctl);
    p_hdr_glb->udp_en        = GetIpeHeaderAdjustCtl(V, headerUdpEn_f, &ipe_header_adjust_ctl);
    p_hdr_glb->udp_dest_port = GetIpeHeaderAdjustCtl(V, udpSrcPort_f, &ipe_header_adjust_ctl);
    p_hdr_glb->udp_src_port  = GetIpeHeaderAdjustCtl(V, udpDestPort_f, &ipe_header_adjust_ctl);



    p_hdr_glb->ip_protocol   = GetEpePktHdrCtl(V, headerIpProtocol_f, &epe_pkt_hdr_ctl);
    p_hdr_glb->ip_ttl        = GetEpePktHdrCtl(V, headerTtl_f, &epe_pkt_hdr_ctl);
    p_hdr_glb->ip_dscp       = GetEpePktHdrCtl(V, headerDscp_f, &epe_pkt_hdr_ctl);
    p_hdr_glb->cos           = GetEpePktHdrCtl(V, headerCos_f, &epe_pkt_hdr_ctl);


    p_hdr_glb->is_ipv4 = g_gg_stacking_master[lchip]->ipv4_encap;

    GetEpePktHdrCtl(A, headerIpSa_f, &epe_pkt_hdr_ctl, ipv6_sa);

    if (p_hdr_glb->is_ipv4)
    {
        p_hdr_glb->ipsa.ipv4        = ipv6_sa[0];
    }
    else
    {
        sal_memcpy(p_hdr_glb->ipsa.ipv6, ipv6_sa, sizeof(ipv6_addr_t));
        p_hdr_glb->ipsa.ipv6[3] = ipv6_sa[0];
        p_hdr_glb->ipsa.ipv6[2] = ipv6_sa[1];
        p_hdr_glb->ipsa.ipv6[1] = ipv6_sa[2];
        p_hdr_glb->ipsa.ipv6[0] = ipv6_sa[3];
    }

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_stacking_register_en(uint8 lchip, uint8 enable, uint8 mode)
{
    uint32 cmd          = 0;
    uint32 value        = 0;
    uint32 ds_nh_offset = 0;
    uint32 value_mode   = 0;
    uint32 field_val    = 0;
    uint8 gchip_id      = 0;
    IpeHeaderAdjustCtl_m  ipe_hdr_adjust_ctl;
    IpeFwdCtl_m            ipe_fwd_ctl;
    MetFifoCtl_m          met_fifo_ctl;
    QWriteSgmacCtl_m      qwrite_sgmac_ctl;
    BufferRetrieveCtl_m   buffer_retrieve_ctl;
    EpeHdrAdjustCtl_m     epe_hdr_adjust_ctl;
    EpeNextHopCtl_m       epe_next_hop_ctl;
    EpePktProcCtl_m      epe_pkt_proc_ctl;
    EpeHeaderEditCtl_m    epe_header_edit_ctl;
    EpePktHdrCtl_m        epe_pkt_hdr_ctl;
    BufferStoreCtl_m      buffer_store_ctl;
    EpeHdrAdjustChanCtl_m epe_hdr_adjust_chan_ctl;
    uint32 packet_header_en[2] = {0};

    value = enable ? 1 : 0;
    value_mode = (mode == 1) ? 0 : value;
    sys_goldengate_get_gchip_id(lchip, &gchip_id);

    /*IPE Hdr Adjust Ctl*/
    sal_memset(&ipe_hdr_adjust_ctl, 0, sizeof(ipe_hdr_adjust_ctl));
    cmd = DRV_IOR(IpeHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_hdr_adjust_ctl));
    SetIpeHeaderAdjustCtl(V, packetHeaderBypassAll_f, &ipe_hdr_adjust_ctl, value);
    SetIpeHeaderAdjustCtl(V, discardNonPacketHeader_f, &ipe_hdr_adjust_ctl, value_mode);
    cmd = DRV_IOW(IpeHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_hdr_adjust_ctl));

    /*ingress edit*/
    sal_memset(&ipe_fwd_ctl, 0, sizeof(ipe_fwd_ctl));
    cmd = DRV_IOR(IpeFwdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_fwd_ctl));
    SetIpeFwdCtl(V, stackingEn_f, &ipe_fwd_ctl, value);

    CTC_ERROR_RETURN(sys_goldengate_nh_get_resolved_offset(lchip,
    SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH,  &ds_nh_offset));
    SetIpeFwdCtl(V, ingressEditNexthopPtr_f, &ipe_fwd_ctl, ds_nh_offset);

    CTC_ERROR_RETURN(sys_goldengate_nh_get_resolved_offset(lchip,
    SYS_NH_RES_OFFSET_TYPE_MIRROR_NH,  &ds_nh_offset));
    SetIpeFwdCtl(V, ingressEditMirrorNexthopPtr_f, &ipe_fwd_ctl, ds_nh_offset);

    cmd = DRV_IOW(IpeFwdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_fwd_ctl));

    /*Buffer Store Ctl init*/
    sal_memset(&buffer_store_ctl, 0, sizeof(buffer_store_ctl));
    cmd = DRV_IOR(BufferStoreCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));
    SetBufferStoreCtl(V, stackingEn_f, &buffer_store_ctl, value);
    SetBufferStoreCtl(V, chipIdCheckDisable_f, &buffer_store_ctl, value);
    SetBufferStoreCtl(V, localSwitchingDisable_f, &buffer_store_ctl, 0);
    SetBufferStoreCtl(V, forceDiscardSourceChip_f, &buffer_store_ctl, 0);
    SetBufferStoreCtl(V, criticalPacketFromFabric_f, &buffer_store_ctl, 0);
     /*SetBufferStoreCtl(V, discardSourceChip_f, &buffer_store_ctl, 1 << gchip_id);*/
    cmd = DRV_IOW(BufferStoreCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));

    /*MetFifo Ctl init*/
    sal_memset(&met_fifo_ctl, 0, sizeof(met_fifo_ctl));
    cmd = DRV_IOR(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &met_fifo_ctl));
    SetMetFifoCtl(V, stackingEn_f, &met_fifo_ctl, value);
    SetMetFifoCtl(V, stackingMcastEndEn_f, &met_fifo_ctl, value);
     /*SetMetFifoCtl(V, fromFabricMulticastEn_f, &met_fifo_ctl, value);*/
    SetMetFifoCtl(V, exceptionResetSrcSgmac_f, &met_fifo_ctl, value);
    SetMetFifoCtl(V, uplinkReflectCheckEn_f, &met_fifo_ctl, value);
    SetMetFifoCtl(V, forceReplicationFromFabric_f, &met_fifo_ctl, value);
    SetMetFifoCtl(V, stackingBroken_f, &met_fifo_ctl, 0);
    SetMetFifoCtl(V, discardMetLoop_f, &met_fifo_ctl, 0);
    cmd = DRV_IOW(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &met_fifo_ctl));

    /*Qwrite Sgmac Ctl init*/
    sal_memset(&qwrite_sgmac_ctl, 0, sizeof(qwrite_sgmac_ctl));
    cmd = DRV_IOR(QWriteSgmacCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qwrite_sgmac_ctl));
    SetQWriteSgmacCtl(V, sgmacEn_f, &qwrite_sgmac_ctl, value);
    SetQWriteSgmacCtl(V, uplinkReflectCheckEn_f, &qwrite_sgmac_ctl, value_mode);
    SetQWriteSgmacCtl(V, discardUnkownSgmacGroup_f, &qwrite_sgmac_ctl, enable ? 3 : 0);
 /*    SetQWriteSgmacCtl(V, sgmacIndexMcastEn_f, &qwrite_sgmac_ctl, 0);*/
    cmd = DRV_IOW(QWriteSgmacCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qwrite_sgmac_ctl));

    /*Buffer Retrive Ctl init*/
    sal_memset(&buffer_retrieve_ctl, 0, sizeof(buffer_retrieve_ctl));
    cmd = DRV_IOR(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_retrieve_ctl));
    SetBufferRetrieveCtl(V, stackingEn_f, &buffer_retrieve_ctl, value);
    SetBufferRetrieveCtl(V, crossChipHighPriorityEn_f, &buffer_retrieve_ctl, 0);
    SetBufferRetrieveCtl(V, exceptionPktEditMode_f, &buffer_retrieve_ctl, 1);
    cmd = DRV_IOW(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_retrieve_ctl));
    cmd = DRV_IOW(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &buffer_retrieve_ctl));

    /*Epe Head Adjust Ctl init*/
    sal_memset(&epe_hdr_adjust_ctl, 0, sizeof(epe_hdr_adjust_ctl));
    cmd = DRV_IOR(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl));
    SetEpeHdrAdjustCtl(V, packetHeaderBypassAll_f, &epe_hdr_adjust_ctl, 1);
    cmd = DRV_IOW(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl));

    sal_memset(&epe_hdr_adjust_chan_ctl, 0, sizeof(epe_hdr_adjust_chan_ctl));
    cmd = DRV_IOR(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_chan_ctl));
    GetEpeHdrAdjustChanCtl(A, packetHeaderEditIngress_f, &epe_hdr_adjust_chan_ctl, packet_header_en);

    {
        packet_header_en[0] = 0xFFFFFFFF;
        packet_header_en[1] = 0xFFFFFFFF;
    }

    SetEpeHdrAdjustChanCtl(A, packetHeaderEditIngress_f, &epe_hdr_adjust_chan_ctl, packet_header_en);

    cmd = DRV_IOW(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_chan_ctl));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &epe_hdr_adjust_chan_ctl))

    /*Epe Nexthop Ctl init*/
    sal_memset(&epe_next_hop_ctl, 0, sizeof(epe_next_hop_ctl));
    cmd = DRV_IOR(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_next_hop_ctl));
    SetEpeNextHopCtl(V,stackingEn_f, &epe_next_hop_ctl, value);
    cmd = DRV_IOW(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_next_hop_ctl));

    /*Epe Packet Process Ctl init*/
    sal_memset(&epe_pkt_proc_ctl, 0, sizeof(epe_pkt_proc_ctl));
    cmd = DRV_IOR(EpePktProcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_proc_ctl));
    SetEpePktProcCtl(V, stackingEn_f, &epe_pkt_proc_ctl, value);
    cmd = DRV_IOW(EpePktProcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_proc_ctl));

    /*Epe Header Edit Ctl init*/
    sal_memset(&epe_header_edit_ctl, 0, sizeof(epe_header_edit_ctl));
    cmd = DRV_IOR(EpeHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_header_edit_ctl));
    SetEpeHeaderEditCtl(V, stackingEn_f, &epe_header_edit_ctl, value);
    SetEpeHeaderEditCtl(V, stackingCompatibleMode_f, &epe_header_edit_ctl, value);
    cmd = DRV_IOW(EpeHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_header_edit_ctl));

    /*Epe Packet Header Ctl init*/
    sal_memset(&epe_pkt_hdr_ctl, 0, sizeof(epe_pkt_hdr_ctl));
    cmd = DRV_IOR(EpePktHdrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_hdr_ctl));
    SetEpePktHdrCtl(V, headerUdpEn_f, &epe_pkt_hdr_ctl, 0);
    cmd = DRV_IOW(EpePktHdrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_hdr_ctl));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &epe_pkt_hdr_ctl));

    /* spine-leaf mode init */
    if (mode == 1)
    {
        cmd = DRV_IOR(EpeHdrEditReserved2_t, EpeHdrEditReserved2_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        if (enable)
        {
            CTC_BIT_SET(field_val, 3);
        }
        else
        {
            CTC_BIT_UNSET(field_val, 3);
        }
        cmd = DRV_IOW(EpeHdrEditReserved2_t, EpeHdrEditReserved2_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    return CTC_E_NONE;
}



uint32
_sys_goldengate_stacking_mcast_hash_make(sys_stacking_mcast_db_t* backet)
{
    uint32 val = 0;
    uint8* data = NULL;
    uint8   length = 0;


    if (!backet)
    {
        return 0;
    }

    val = (backet->type << 24) | (backet->id);

    data = (uint8*)&val;
    length = sizeof(uint32);

    return ctc_hash_caculate(length, data);
}

/**
 @brief  hash comparison hook.
*/
bool
_sys_goldengate_stacking_mcast_hash_cmp(sys_stacking_mcast_db_t* backet1,
                                sys_stacking_mcast_db_t* backet2)
{

    if (!backet1 || !backet2)
    {
        return FALSE;
    }

    if ((backet1->type == backet2->type) &&
        (backet1->id == backet2->id))
    {
        return TRUE;
    }

    return FALSE;
}

int32
sys_goldengate_stacking_mcast_group_create(uint8 lchip,
                                           uint8 type,
                                           uint16 id,
                                           sys_stacking_mcast_db_t **pp_mcast_db,
                                           uint8 append_en)
{
    int32  ret                     = CTC_E_NONE;
    uint32 new_met_offset          = 0;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;
    sys_met_t dsmet;


    /*build node*/
    p_mcast_db = (sys_stacking_mcast_db_t*)mem_malloc(MEM_STK_MODULE, sizeof(sys_stacking_mcast_db_t));
    if (NULL == p_mcast_db)
    {
        ret =  CTC_E_NO_MEMORY;
        goto Error0;
    }

    sal_memset(p_mcast_db, 0, sizeof(sys_stacking_mcast_db_t));
    p_mcast_db->type = type;
    p_mcast_db->id   = id;

    if (append_en)
    {
        p_mcast_db->last_tail_offset = g_gg_stacking_master[lchip]->stacking_mcast_offset;
    }
    else
    {
        p_mcast_db->last_tail_offset = SYS_NH_INVALID_OFFSET;
    }

    sal_memset(&dsmet, 0, sizeof(dsmet));
    dsmet.next_met_entry_ptr = SYS_NH_NEXT_MET_ENTRY(lchip, p_mcast_db->last_tail_offset);

    if (type == 0) /* mcast profile */
    {
        /*alloc resource*/
        CTC_ERROR_GOTO(sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_MET, 1, &new_met_offset),
                       ret, Error1);

        dsmet.dest_id = 0 ;
        dsmet.remote_chip = 1;
        CTC_ERROR_GOTO(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET, new_met_offset, &dsmet),
                       ret, Error2);
        /*add db*/
        p_mcast_db->head_met_offset = new_met_offset;
        p_mcast_db->tail_met_offset = p_mcast_db->last_tail_offset;
        p_mcast_db->append_en = append_en;
    }
    else/* keeplive */
    {
        new_met_offset = id*2 + sys_goldengate_nh_get_dsmet_base(lchip);

        dsmet.dest_id = SYS_RSV_PORT_DROP_ID ;
        dsmet.end_local_rep = 1;
        CTC_ERROR_GOTO(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET, new_met_offset, &dsmet),
                       ret, Error1);
        /*add db*/
        p_mcast_db->head_met_offset = new_met_offset;
        p_mcast_db->tail_met_offset = new_met_offset;
    }

    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "head_met_offset :%d\n ", new_met_offset);
    ctc_hash_insert(g_gg_stacking_master[lchip]->mcast_hash, p_mcast_db);


    *pp_mcast_db = p_mcast_db;

    return CTC_E_NONE;


    Error2:

    if (type == 0)
    {
        sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_MET, 1, new_met_offset);
    }

    Error1:

    if (p_mcast_db)
    {
        mem_free(p_mcast_db);
    }

    Error0:

    return ret;

}



int32
sys_goldengate_stacking_mcast_group_destroy(uint8 lchip,
                                            uint8 type,
                                            uint16 id,
                                            sys_stacking_mcast_db_t *p_mcast_db)
{
    sys_met_t dsmet;

    sal_memset(&dsmet, 0, sizeof(dsmet));
    dsmet.next_met_entry_ptr = SYS_NH_INVALID_OFFSET;
    dsmet.dest_id = 0 ;
    dsmet.remote_chip = 1;
    CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET,
                                                        p_mcast_db->head_met_offset, &dsmet));

    if (type == 0)/* mcast profile */
    {
        /*free resource*/
        CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_MET, 1, p_mcast_db->head_met_offset));
    }

    ctc_hash_remove(g_gg_stacking_master[lchip]->mcast_hash, p_mcast_db);

    mem_free(p_mcast_db);

    return CTC_E_NONE;
}

int32
sys_goldengate_stacking_mcast_group_add_member(uint8 lchip,
                                               sys_stacking_mcast_db_t *p_mcast_db,
                                               uint16 dest_id, uint8 is_remote)
{
    int32  ret                     = CTC_E_NONE;
    uint32 new_met_offset          = 0;
    uint32 cmd                     = 0;
    DsMetEntry3W_m dsmet3w;
    sys_met_t dsmet;
    uint32 dsnh_offset             = 0;

    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "dest_id = %d, is_remote:0x%4x, head:%d\n", dest_id, is_remote, p_mcast_db->head_met_offset);

    if (!is_remote)
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_get_resolved_offset(lchip, SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH, &dsnh_offset));
    }

    if (p_mcast_db->tail_met_offset == p_mcast_db->last_tail_offset)
    {
        sal_memset(&dsmet, 0, sizeof(dsmet));
        dsmet.next_met_entry_ptr = SYS_NH_NEXT_MET_ENTRY(lchip, p_mcast_db->last_tail_offset);
        dsmet.dest_id = dest_id ;
        dsmet.remote_chip = is_remote;
        dsmet.next_hop_ptr = is_remote?0:dsnh_offset;
        CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET,
                                                            p_mcast_db->head_met_offset, &dsmet));

        p_mcast_db->tail_met_offset = p_mcast_db->head_met_offset;

        SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "update head_met_offset :%d, trunk-id:%d \n ", new_met_offset, dest_id);

    }
    else
    {

        /*new mcast offset*/
        CTC_ERROR_RETURN(sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_MET, 1, &new_met_offset));
        sal_memset(&dsmet, 0, sizeof(dsmet));
        dsmet.next_met_entry_ptr = SYS_NH_NEXT_MET_ENTRY(lchip, p_mcast_db->last_tail_offset);
        dsmet.dest_id = dest_id ;
        dsmet.remote_chip = is_remote;
        dsmet.next_hop_ptr = dsnh_offset;
        CTC_ERROR_GOTO(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET, new_met_offset, &dsmet),
                       ret, Error0);


        cmd = DRV_IOR(DsMetEntry3W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO( DRV_IOCTL(lchip, p_mcast_db->tail_met_offset, cmd, &dsmet3w),
                       ret, Error0);

        SetDsMetEntry3W(V, nextMetEntryPtr_f,  &dsmet3w, SYS_NH_NEXT_MET_ENTRY(lchip, new_met_offset));

        cmd = DRV_IOW(DsMetEntry3W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO( DRV_IOCTL(lchip, p_mcast_db->tail_met_offset, cmd, &dsmet3w),
                       ret, Error0);

        SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "add new_met_offset :%d, pre_offset:%d, trunk-id:%d\n ", new_met_offset, p_mcast_db->tail_met_offset, dest_id);

        p_mcast_db->tail_met_offset = new_met_offset;

    }

    if (is_remote)
    {
        CTC_BIT_SET(p_mcast_db->trunk_bitmap[dest_id / 32], dest_id % 32);
    }

    return CTC_E_NONE;

    Error0:
    sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_MET, 1, new_met_offset);

    return ret;


}

int32
sys_goldengate_stacking_mcast_group_remove_member(uint8 lchip,
                                                  sys_stacking_mcast_db_t *p_mcast_db,
                                                  uint16 dest_id, uint8 is_remote)
{
    uint16 trunk_id_hw             = 0;
    uint16 remote_chip             = 0;
    uint32 cmd                     = 0;
    uint32 head_met_offset         = 0;
    uint32 pre_met_offset          = 0;
    uint32 cur_met_offset          = 0;
    uint32 next_met_offset         = 0;
    DsMetEntry3W_m dsmet3w;
    sys_met_t dsmet;
    uint8   is_found               = 0;


    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "dest_id = %d, is_remote:0x%4x, head:%d \n", dest_id, is_remote, p_mcast_db->head_met_offset);

    head_met_offset = p_mcast_db->head_met_offset;
    cur_met_offset  = head_met_offset;
    next_met_offset = head_met_offset;


    do
    {
        pre_met_offset = cur_met_offset;
        cur_met_offset = next_met_offset;

        cmd = DRV_IOR(DsMetEntry3W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN( DRV_IOCTL(lchip, cur_met_offset, cmd, &dsmet3w));

        trunk_id_hw = GetDsMetEntry3W(V, u1_g2_ucastId_f,  &dsmet3w);
        remote_chip =  GetDsMetEntry3W(V, remoteChip_f,  &dsmet3w);
        next_met_offset = GetDsMetEntry3W(V, nextMetEntryPtr_f,  &dsmet3w);
        if (SYS_NH_INVALID_OFFSET != next_met_offset)
        {
            next_met_offset += sys_goldengate_nh_get_dsmet_base(lchip);
        }

        if ((remote_chip == is_remote) && (trunk_id_hw == dest_id))
        {
            is_found = 1;
            break;
        }

    }
    while (next_met_offset != p_mcast_db->last_tail_offset);

    if (!is_found)
    {
        return  CTC_E_ENTRY_NOT_EXIST;
    }

    if (cur_met_offset == head_met_offset)
    {
        /*remove is first met*/

        if (next_met_offset == p_mcast_db->last_tail_offset)
        {
            /*remove all*/

            sal_memset(&dsmet, 0, sizeof(dsmet));
            dsmet.next_met_entry_ptr = SYS_NH_NEXT_MET_ENTRY(lchip, p_mcast_db->last_tail_offset);
            dsmet.dest_id = 0 ;
            dsmet.remote_chip = is_remote;
            CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET,
                                                                head_met_offset, &dsmet));

            p_mcast_db->tail_met_offset = p_mcast_db->last_tail_offset;

            SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Update first to drop, head_met_offset: %d, trunk_id:%d\n ",
                            head_met_offset, dest_id);

            SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "tail:%d\n ",  p_mcast_db->tail_met_offset);
        }
        else
        {

            /*nexthop change to first*/
            cmd = DRV_IOR(DsMetEntry3W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN( DRV_IOCTL(lchip, next_met_offset, cmd, &dsmet3w));

            /*nexthop change to first*/
            cmd = DRV_IOW(DsMetEntry3W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, cur_met_offset, cmd, &dsmet3w));

            /*free currect offset*/
            CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_MET, 1, next_met_offset));


            if (p_mcast_db->tail_met_offset == next_met_offset)
            {
                p_mcast_db->tail_met_offset = cur_met_offset;
            }


            SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Replace first head_met_offset: %d, trunk_id:%d\n ",
                            head_met_offset, dest_id);

            SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Free Met offset:%d, tail:%d\n ",  next_met_offset, p_mcast_db->tail_met_offset);

        }
    }
    else
    {
        cmd = DRV_IOR(DsMetEntry3W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN( DRV_IOCTL(lchip, pre_met_offset, cmd, &dsmet3w));

        SetDsMetEntry3W(V, nextMetEntryPtr_f,  &dsmet3w, SYS_NH_NEXT_MET_ENTRY(lchip, next_met_offset));

        cmd = DRV_IOW(DsMetEntry3W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN( DRV_IOCTL(lchip, pre_met_offset, cmd, &dsmet3w));

        /*free currect offset*/
        CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_MET, 1, cur_met_offset));

        if (p_mcast_db->tail_met_offset == cur_met_offset)
        {
            p_mcast_db->tail_met_offset = pre_met_offset;
        }

        SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Remove middle pre: %d, cur:%d, next:%d, trunk_id:%d\n ",
                        pre_met_offset, cur_met_offset, next_met_offset,  dest_id);

        SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Free Met offset:%d, tail:%d\n ",  cur_met_offset, p_mcast_db->tail_met_offset);


    }

    if (is_remote)
    {
        CTC_BIT_UNSET(p_mcast_db->trunk_bitmap[dest_id / 32], dest_id % 32);
    }

    return CTC_E_NONE;
}


int32
_sys_goldengate_stacking_encap_header_enable(uint8 lchip, uint16 gport, ctc_stacking_hdr_encap_t* p_encap)
{
    uint16 chan_id = 0;
    uint32 cmd  = 0;
    uint8 mux_type = 0;
    uint32 field_val = 0;
    uint8 out_encap_type = 0;
    hw_mac_addr_t mac;
    DsPacketHeaderEditTunnel_m  ds_packet_header_edit_tunnel;

    CTC_PTR_VALID_CHECK(p_encap);

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);

    switch (p_encap->hdr_flag)
    {
    case CTC_STK_HDR_WITH_NONE:
        mux_type = SYS_STK_MUX_TYPE_HDR_WITHOUT_TUNNEL;
        out_encap_type = SYS_STK_OUT_ENCAP_NONE;
        break;

    case CTC_STK_HDR_WITH_L2:
        mux_type = SYS_STK_MUX_TYPE_HDR_WITH_L2;
        out_encap_type = SYS_STK_OUT_ENCAP_NONE;
        break;

    case CTC_STK_HDR_WITH_L2_AND_IPV4:
        mux_type = SYS_STK_MUX_TYPE_HDR_WITH_L2_AND_IPV4;
        out_encap_type = SYS_STK_OUT_ENCAP_IPV4;
        break;

    case CTC_STK_HDR_WITH_L2_AND_IPV6:
        mux_type = SYS_STK_MUX_TYPE_HDR_WITH_L2_AND_IPV6;
        out_encap_type = SYS_STK_OUT_ENCAP_IPV6;
        break;

    case CTC_STK_HDR_WITH_IPV4:
        mux_type = SYS_STK_MUX_TYPE_HDR_WITH_IPV4;
        out_encap_type = SYS_STK_OUT_ENCAP_IPV4;
        break;

    case CTC_STK_HDR_WITH_IPV6:
        mux_type = SYS_STK_MUX_TYPE_HDR_WITH_IPV6;
        out_encap_type = SYS_STK_OUT_ENCAP_IPV6;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&ds_packet_header_edit_tunnel, 0, sizeof(ds_packet_header_edit_tunnel));

   if (SYS_STK_MUX_TYPE_HDR_WITH_L2 == mux_type ||
       SYS_STK_MUX_TYPE_HDR_WITH_L2_AND_IPV4 == mux_type ||
   SYS_STK_MUX_TYPE_HDR_WITH_L2_AND_IPV6 == mux_type)
   {
       SetDsPacketHeaderEditTunnel(V, packetHeaderL2En_f , &ds_packet_header_edit_tunnel, 1);
       SetDsPacketHeaderEditTunnel(V, vlanIdValid_f, &ds_packet_header_edit_tunnel, p_encap->vlan_valid);


       if (p_encap->vlan_valid)
       {
           SetDsPacketHeaderEditTunnel(V, vlanId_f , &ds_packet_header_edit_tunnel, p_encap->vlan_id);
       }

       SYS_GOLDENGATE_SET_HW_MAC(mac, p_encap->mac_da)
       SetDsPacketHeaderEditTunnel(A, macDa_f, &ds_packet_header_edit_tunnel, mac);
       SYS_GOLDENGATE_SET_HW_MAC(mac, p_encap->mac_sa)
       SetDsPacketHeaderEditTunnel(A, macSa_f , &ds_packet_header_edit_tunnel, mac);
   }
   SetDsPacketHeaderEditTunnel(V, packetHeaderL3Type_f , &ds_packet_header_edit_tunnel, out_encap_type);


   if (SYS_STK_OUT_ENCAP_IPV4 == out_encap_type)
   {
       SetDsPacketHeaderEditTunnel(A, ipDa_f  , &ds_packet_header_edit_tunnel, &p_encap->ipda.ipv4);
   }

    if (SYS_STK_OUT_ENCAP_IPV6 == out_encap_type)
    {
        uint32 ipv6_da[CTC_IPV6_ADDR_LEN];
        /* ipda, use little india for DRV_SET_FIELD_A */
        ipv6_da[0] = p_encap->ipda.ipv6[3];
        ipv6_da[1] = p_encap->ipda.ipv6[2];
        ipv6_da[2] = p_encap->ipda.ipv6[1];
        ipv6_da[3] = p_encap->ipda.ipv6[0];
        SetDsPacketHeaderEditTunnel(A, ipDa_f  , &ds_packet_header_edit_tunnel, &ipv6_da);
    }

    /*mux type*/
    cmd = DRV_IOW(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_muxType_f);
    field_val = mux_type;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));

    cmd = DRV_IOW(DsPacketHeaderEditTunnel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &ds_packet_header_edit_tunnel));

    return CTC_E_NONE;
}

int32
_sys_goldengate_stacking_encap_header_disable(uint8 lchip, uint16 gport)
{
    uint16 chan_id = 0;
    uint32 cmd  = 0;
    uint8 mux_type = 0;
    uint32 field_val = 0;

    DsPacketHeaderEditTunnel_m  ds_packet_header_edit_tunnel;

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);

    mux_type = SYS_STK_MUX_TYPE_HDR_REGULAR_PORT;

    /*mux type*/
    cmd = DRV_IOW(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_muxType_f);
    field_val = mux_type;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));

    sal_memset(&ds_packet_header_edit_tunnel, 0, sizeof(ds_packet_header_edit_tunnel));
    cmd = DRV_IOW(DsPacketHeaderEditTunnel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &ds_packet_header_edit_tunnel))

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_stacking_get_port_ref_cnt(uint8 lchip, uint32 gport, uint8* port_ref_cnt, uint16 except_trunk_id)
{
    uint16 trunk_id = 0;
    uint16 max_trunk_id = 0;
    uint8 i = 0;
    uint8 ref_cnt = 0;
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;

    max_trunk_id = SYS_STK_TRUNK_MAX_ID;
    for (trunk_id = SYS_STK_TRUNK_MIN_ID; trunk_id <= max_trunk_id; trunk_id++)
    {
        if(except_trunk_id != 0xFFFF &&  trunk_id == except_trunk_id )
        {
           continue;
        }
        p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id);
        if (p_sys_trunk)
        {
            for (i = 0; i < p_sys_trunk->mem_cnt; i++)
            {
                if (gport == p_sys_trunk->mem_ports[i])
                {
                    ref_cnt++;
                }
            }
        }
    }
    *port_ref_cnt = ref_cnt;
    return CTC_E_NONE;
}

STATIC uint8
_sys_goldengate_stacking_is_trunk_have_member(uint8 lchip)
{
    uint32 cmd = 0;
    uint8 slice_id = 0;
    uint32 packet_header_en[2] = {0};
    EpeHdrAdjustChanCtl_m epe_hdr_adjust_chan_ctl;
    for (slice_id = 0; slice_id < 2; slice_id++)
    {
        sal_memset(&epe_hdr_adjust_chan_ctl, 0, sizeof(epe_hdr_adjust_chan_ctl));
        cmd = DRV_IOR(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &epe_hdr_adjust_chan_ctl));
        GetEpeHdrAdjustChanCtl(A, packetHeaderEn_f, &epe_hdr_adjust_chan_ctl, packet_header_en);
        if (packet_header_en[0] || packet_header_en[1])
        {
            return 1;
        }
    }

    return 0;
}


STATIC int32
_sys_goldengate_stacking_trunk_alloc_mem_base(uint8 lchip,
                                         sys_stacking_trunk_info_t* p_sys_trunk)
{
    uint32 value_32 = 0;
    uint32 mem_base  = 0;
    sys_goldengate_opf_t  opf;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type  = g_gg_stacking_master[lchip]->opf_type;
    opf.pool_index = SYS_STK_OPF_MEM_BASE;
    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, p_sys_trunk->max_mem_cnt, &value_32));
    mem_base = value_32;

    p_sys_trunk->mem_base = mem_base;
    SYS_STK_DBG_INFO("_sys_goldengate_stacking_trunk_alloc_mem_base :%d\n",mem_base);

    return CTC_E_NONE;

}


STATIC int32
_sys_goldengate_stacking_trunk_free_mem_base(uint8 lchip,
                                        sys_stacking_trunk_info_t* p_sys_trunk)
{
    uint32 value_32 = 0;
    sys_goldengate_opf_t  opf;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type  = g_gg_stacking_master[lchip]->opf_type;
    opf.pool_index = SYS_STK_OPF_MEM_BASE;
    value_32 = p_sys_trunk->mem_base;
    CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, p_sys_trunk->max_mem_cnt, value_32));

    SYS_STK_DBG_INFO("_sys_goldengate_stacking_trunk_free_mem_base :%d\n", p_sys_trunk->mem_base);

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_stacking_all_mcast_add_default_profile(void* array_data, uint32 group_id, void* user_data)
{
    uint8 lchip                    = 0;
    uint32 cmd                     = 0;
    uint32 cur_met_offset          = 0;
    uint32 next_met_offset         = 0;

    lchip = *((uint8*) user_data);
    cur_met_offset  = group_id*2;
    next_met_offset = cur_met_offset;

    do
    {
        cur_met_offset = next_met_offset;

        cmd = DRV_IOR(DsMetEntry3W_t, DsMetEntry3W_nextMetEntryPtr_f);
        CTC_ERROR_RETURN( DRV_IOCTL(lchip, cur_met_offset, cmd, &next_met_offset));

        if (SYS_NH_INVALID_OFFSET != next_met_offset)
        {
            next_met_offset += sys_goldengate_nh_get_dsmet_base(lchip);
        }

        if (next_met_offset == g_gg_stacking_master[lchip]->stacking_mcast_offset)
        {
            return CTC_E_NONE;
        }

    }
    while (next_met_offset != SYS_NH_INVALID_OFFSET);

    next_met_offset = SYS_NH_NEXT_MET_ENTRY(lchip, g_gg_stacking_master[lchip]->stacking_mcast_offset);
    cmd = DRV_IOW(DsMetEntry3W_t, DsMetEntry3W_nextMetEntryPtr_f);
    CTC_ERROR_RETURN( DRV_IOCTL(lchip, cur_met_offset, cmd, &next_met_offset));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stacking_all_mcast_remove_default_profile(void* array_data, uint32 group_id, void* user_data)
{
    uint8 lchip                    = 0;
    uint32 cmd                     = 0;
    uint32 cur_met_offset          = 0;
    uint32 next_met_offset         = 0;

    lchip = *((uint8*) user_data);
    cur_met_offset  = group_id*2;
    next_met_offset = cur_met_offset;

    do
    {
        cur_met_offset = next_met_offset;

        cmd = DRV_IOR(DsMetEntry3W_t, DsMetEntry3W_nextMetEntryPtr_f);
        CTC_ERROR_RETURN( DRV_IOCTL(lchip, cur_met_offset, cmd, &next_met_offset));

        if (SYS_NH_INVALID_OFFSET != next_met_offset)
        {
            next_met_offset += sys_goldengate_nh_get_dsmet_base(lchip);
        }

        if (next_met_offset == g_gg_stacking_master[lchip]->stacking_mcast_offset)
        {
            break;
        }

    }
    while (next_met_offset != SYS_NH_INVALID_OFFSET);


    if (next_met_offset !=  g_gg_stacking_master[lchip]->stacking_mcast_offset)
    {
        return CTC_E_NONE;
    }

    next_met_offset = SYS_NH_INVALID_OFFSET;
    cmd = DRV_IOW(DsMetEntry3W_t, DsMetEntry3W_nextMetEntryPtr_f);
    CTC_ERROR_RETURN( DRV_IOCTL(lchip, cur_met_offset, cmd, &next_met_offset));

    return CTC_E_NONE;
}


int32
sys_goldengate_stacking_all_mcast_bind_en(uint8 lchip, uint8 enable)
{
    vector_traversal_fn2 fn = NULL;

    SYS_STK_INIT_CHECK(lchip);

    if (enable)
    {
        fn = _sys_goldengate_stacking_all_mcast_add_default_profile;
    }
    else
    {
        fn = _sys_goldengate_stacking_all_mcast_remove_default_profile;
    }

    sys_goldengate_nh_traverse_mcast_db( lchip,  fn,  &lchip);

    g_gg_stacking_master[lchip]->bind_mcast_en = enable?1:0;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stacking_check_port_status(uint8 lchip, uint32 gport)
{
    uint32 depth                   = 0;
    uint32 index                   = 0;

    /*check queue flush clear*/
    sys_goldengate_queue_get_port_depth(lchip, gport, &depth);
    while (depth)
    {
        sal_task_sleep(20);
        if ((index++) > 50)
        {
            SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "the queue depth(%d) not Zero \n", depth);
            return CTC_E_HW_TIME_OUT;
        }
        sys_goldengate_queue_get_port_depth(lchip, gport, &depth);
    }
    sal_task_sleep(20);
    return CTC_E_NONE;
}


int32
sys_goldengate_stacking_create_trunk(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    uint32 cmd = 0;
    uint8 load_mode = 0;
    int32 ret       = CTC_E_NONE;
    uint8 trunk_id  = 0;
    uint8 index = 0;
    uint32 channel_id = 0;
    DsLinkAggregateChannelGroup_m  ds_link_aggregate_sgmac_group;
    sys_stacking_trunk_info_t* p_sys_trunk;

    SYS_STK_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_trunk);

    if (p_trunk->encap_hdr.vlan_valid)
    {
        CTC_VLAN_RANGE_CHECK(p_trunk->encap_hdr.vlan_id);
    }

    trunk_id = p_trunk->trunk_id;

    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    SYS_STK_EXTEND_EN(p_trunk->extend);

    if (p_trunk->load_mode >= CTC_STK_LOAD_MODE_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((CTC_STACKING_TRUNK_MODE_0 == g_gg_stacking_master[lchip]->trunk_mode)
        && (CTC_STK_LOAD_DYNAMIC == p_trunk->load_mode))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_STACKING_TRUNK_MODE_2 == g_gg_stacking_master[lchip]->trunk_mode)
    {
        if (CTC_STK_LOAD_DYNAMIC == p_trunk->load_mode)
        {
            p_trunk->max_mem_cnt = 32;
        }
        else
        {
            /*If member more than 24, need pading , so api limit up to 24 members*/
            if (p_trunk->max_mem_cnt > SYS_STK_TRUNK_STATIC_MAX_MEMBERS ||  0 == p_trunk->max_mem_cnt)
            {
                return CTC_E_INVALID_PARAM;
            }
        }
    }
    if(CTC_FLAG_ISSET(p_trunk->flag, CTC_STACKING_TRUNK_MEM_ASCEND_ORDER) && p_trunk->load_mode != CTC_STK_LOAD_STATIC)
    {
        SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"Only static mode support mem ascend order\n");
        return CTC_E_INVALID_PARAM;
    }
    STACKING_LOCK;

    p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL != p_sys_trunk)
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_HAS_EXIST;
    }

    p_sys_trunk = (sys_stacking_trunk_info_t*)mem_malloc(MEM_STK_MODULE, sizeof(sys_stacking_trunk_info_t));
    if (NULL == p_sys_trunk)
    {
        STACKING_UNLOCK;
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_sys_trunk, 0, sizeof(sys_stacking_trunk_info_t));
    p_sys_trunk->trunk_id = trunk_id;
    p_sys_trunk->mode = p_trunk->load_mode;
    p_sys_trunk->flag = p_trunk->flag;

    switch(g_gg_stacking_master[lchip]->trunk_mode)
    {
    case 0:
        p_sys_trunk->max_mem_cnt = 8;
        break;

    case 1:
        p_sys_trunk->max_mem_cnt = 32;
        break;

    case 2:
        p_sys_trunk->max_mem_cnt = p_trunk->max_mem_cnt;
        break;

    default:
        ret =  CTC_E_INVALID_PARAM;
        goto Error0;
    }
    CTC_ERROR_GOTO(_sys_goldengate_stacking_trunk_alloc_mem_base(lchip, p_sys_trunk),
                   ret, Error0);

    if (0 == g_gg_stacking_master[lchip]->mcast_mode)
    {
        CTC_ERROR_GOTO(sys_goldengate_stacking_mcast_group_add_member(lchip,
                                                                      g_gg_stacking_master[lchip]->p_default_prof_db,
                                                                      p_sys_trunk->trunk_id, 1), ret, Error1);
    }

    p_sys_trunk->mem_cnt = 0;
    ctc_vector_add(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id, p_sys_trunk);

    sal_memset(&ds_link_aggregate_sgmac_group, 0, sizeof(ds_link_aggregate_sgmac_group));
    cmd = DRV_IOW(DsLinkAggregateChannelGroup_t, DRV_ENTRY_FLAG);
    SetDsLinkAggregateChannelGroup(V, channelLinkAggMemBase_f, &ds_link_aggregate_sgmac_group, p_sys_trunk->mem_base);
    load_mode = (CTC_STK_LOAD_DYNAMIC == p_trunk->load_mode) ? 1 : 0;
    SetDsLinkAggregateChannelGroup(V, channelLinkAggRemapEn_f, &ds_link_aggregate_sgmac_group, load_mode);
    if (load_mode)
    {
        SetDsLinkAggregateChannelGroup(V, channelLinkAggMemberPtr_f, &ds_link_aggregate_sgmac_group, trunk_id);
        SetDsLinkAggregateChannelGroup(V, channelLinkAggFlowNum_f, &ds_link_aggregate_sgmac_group, 3);
    }
    SetDsLinkAggregateChannelGroup(V, channelLinkAggMemNum_f, &ds_link_aggregate_sgmac_group, 0);

    CTC_ERROR_GOTO(DRV_IOCTL(lchip, trunk_id, cmd, &ds_link_aggregate_sgmac_group), ret, Error2);

    sal_memcpy(&p_sys_trunk->encap_hdr, &p_trunk->encap_hdr, sizeof(ctc_stacking_hdr_encap_t));

    /* init all DsLinkAggregateChannelMemberSet_t for dlb mode */
    if (CTC_STK_LOAD_DYNAMIC == p_trunk->load_mode)
    {
        channel_id = SYS_DROP_CHANNEL_ID;
        for (index = 0; index < 16; index++)
        {
            cmd = DRV_IOW(DsLinkAggregateChannelMemberSet_t, DsLinkAggregateChannelMemberSet_array_0_destChannel_f + index);
            CTC_ERROR_GOTO(DRV_IOCTL(lchip, trunk_id, cmd, &channel_id), ret, Error2);
        }
    }

    g_gg_stacking_master[lchip]->trunk_num++;

    STACKING_UNLOCK;
    return CTC_E_NONE;


    Error2:
    if (0 == g_gg_stacking_master[lchip]->mcast_mode)
    {
       sys_goldengate_stacking_mcast_group_remove_member(lchip, g_gg_stacking_master[lchip]->p_default_prof_db,
                                                                                    p_sys_trunk->trunk_id, 1);
    }
    ctc_vector_del(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id);

    Error1:
    _sys_goldengate_stacking_trunk_free_mem_base(lchip, p_sys_trunk);

    Error0:
    mem_free(p_sys_trunk);
    STACKING_UNLOCK;
    return ret;
}

int32
sys_goldengate_stacking_destroy_trunk(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    uint32 cmd = 0;
    uint8 trunk_id  = 0;
    uint8 mem_cnt   = 0;
    uint8 mem_idx   = 0;
    uint16 gport    = 0;
    uint32  field_val = 0;
    uint16 chan_id = 0;
    uint8 lchan_id = 0;
    uint8 slice_id = 0;
    BufRetrvChanStackingEn_m stacking_en;
    EpeHdrAdjustChanCtl_m epe_hdr_adjust_chan_ctl;
    uint32 packet_header_en[2];

    sys_stacking_trunk_info_t* p_sys_trunk = NULL;
    DsLinkAggregateChannelGroup_m ds_link_aggregate_sgmac_group;
    uint8 port_ref_cnt = 0;
    int32 ret = CTC_E_NONE;

    SYS_STK_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_trunk);
    trunk_id = p_trunk->trunk_id;
    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    SYS_STK_EXTEND_EN(p_trunk->extend);

    STACKING_LOCK;

    p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }

    sal_memset(&ds_link_aggregate_sgmac_group, 0, sizeof(ds_link_aggregate_sgmac_group));
    cmd = DRV_IOW(DsLinkAggregateChannelGroup_t, DRV_ENTRY_FLAG);
    SetDsLinkAggregateChannelGroup(V, channelLinkAggMemBase_f, &ds_link_aggregate_sgmac_group, p_sys_trunk->mem_base);
    SetDsLinkAggregateChannelGroup(V, channelLinkAggRemapEn_f, &ds_link_aggregate_sgmac_group, 0);
    SetDsLinkAggregateChannelGroup(V, channelLinkAggMemNum_f, &ds_link_aggregate_sgmac_group, 0);
    SetDsLinkAggregateChannelGroup(V, channelLinkAggFlowNum_f, &ds_link_aggregate_sgmac_group, 0);

    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, trunk_id, cmd, &ds_link_aggregate_sgmac_group), g_gg_stacking_master[lchip]->p_stacking_mutex);

    mem_cnt = p_sys_trunk->mem_cnt;

    for (mem_idx = 0; mem_idx < mem_cnt; mem_idx++)
    {
        gport = p_sys_trunk->mem_ports[mem_idx];

        _sys_goldengate_stacking_get_port_ref_cnt(lchip, gport, &port_ref_cnt, 0xFFFF);
        if(port_ref_cnt > 1)
        {
            continue;
        }

        /*enq drop*/
        sys_goldengate_queue_set_port_drop_en(lchip, gport, TRUE);
        ret = _sys_goldengate_stacking_check_port_status(lchip, gport);
        if (ret < 0)
        {
            /*restore buffer*/
            sys_goldengate_queue_set_port_drop_en(lchip, gport, FALSE);
            STACKING_UNLOCK;
            return ret;
        }

        /*Cancel SrcSgmac and Chan mapping*/
        chan_id = SYS_GET_CHANNEL_ID(lchip, gport);

        if (0 == g_gg_stacking_master[lchip]->binding_trunk)
        {
            field_val = 0;
            cmd = DRV_IOW(DsSrcSgmacGroup_t, DsSrcSgmacGroup_sgmacGroupId_f);
            DRV_IOCTL(lchip, chan_id, cmd, &field_val);
        }

        /*recovery edit packet on port*/
        field_val = 0;
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_VLAN_OPERATION_DIS, field_val));
        field_val = 1;
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_UNTAG_DEF_VLAN_EN, field_val));

        /*Ecap Output packet Heaer disable*/
        _sys_goldengate_stacking_encap_header_disable(lchip, gport);

        /*Ecap Output packet Heaer disable*/
        slice_id = SYS_MAP_CHANID_TO_SLICE(chan_id);
        sal_memset(&epe_hdr_adjust_chan_ctl, 0, sizeof(epe_hdr_adjust_chan_ctl));
        cmd = DRV_IOR(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, slice_id, cmd, &epe_hdr_adjust_chan_ctl);
        GetEpeHdrAdjustChanCtl(A, packetHeaderEn_f, &epe_hdr_adjust_chan_ctl, packet_header_en);
        lchan_id = (chan_id&0x3F);
        CTC_BIT_UNSET(packet_header_en[lchan_id >> 5], (lchan_id &0x1F));
        SetEpeHdrAdjustChanCtl(A, packetHeaderEn_f, &epe_hdr_adjust_chan_ctl, packet_header_en);
        cmd = DRV_IOW(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, slice_id, cmd, &epe_hdr_adjust_chan_ctl);

        sal_memset(&stacking_en, 0, sizeof(stacking_en));
        sal_memset(packet_header_en, 0, sizeof(packet_header_en));
        cmd = DRV_IOR(BufRetrvChanStackingEn_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, slice_id, cmd, &stacking_en);
        GetBufRetrvChanStackingEn(A, chanStackingEn_f, &stacking_en, packet_header_en);
        CTC_BIT_UNSET(packet_header_en[lchan_id >> 5], lchan_id&0x1F);
        SetBufRetrvChanStackingEn(A, chanStackingEn_f, &stacking_en, packet_header_en);
        cmd = DRV_IOW(BufRetrvChanStackingEn_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, slice_id, cmd, &stacking_en);

        sys_goldengate_queue_set_port_drop_en(lchip, gport, FALSE);

    }
    if (0 == g_gg_stacking_master[lchip]->mcast_mode)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(sys_goldengate_stacking_mcast_group_remove_member(lchip,
                                                                                    g_gg_stacking_master[lchip]->p_default_prof_db,
                                                                                    p_sys_trunk->trunk_id, 1),
                                                                      g_gg_stacking_master[lchip]->p_stacking_mutex);
    }


    CTC_ERROR_RETURN_WITH_UNLOCK(_sys_goldengate_stacking_trunk_free_mem_base(lchip, p_sys_trunk),g_gg_stacking_master[lchip]->p_stacking_mutex);

    ctc_vector_del(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id);
    mem_free(p_sys_trunk);
    g_gg_stacking_master[lchip]->trunk_num--;

    if ((1 == g_gg_stacking_master[lchip]->bind_mcast_en)
        && (0 == _sys_goldengate_stacking_is_trunk_have_member(lchip)))
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(sys_goldengate_stacking_all_mcast_bind_en(lchip, 0), g_gg_stacking_master[lchip]->p_stacking_mutex);
    }
    STACKING_UNLOCK;

    return CTC_E_NONE;

}

STATIC bool
_sys_goldengate_stacking_find_trunk_port(sys_stacking_trunk_info_t* p_sys_trunk, uint16 gport, uint32* index)
{
    uint32 mem_index = 0;

    CTC_PTR_VALID_CHECK(p_sys_trunk);
    CTC_PTR_VALID_CHECK(index);

    for (mem_index = 0; mem_index < p_sys_trunk->mem_cnt; mem_index++)
    {
        if (gport == p_sys_trunk->mem_ports[mem_index])
        {
            *index = mem_index;
            return TRUE;
        }
    }

    return FALSE;
}

int32
sys_goldengate_stacking_add_trunk_port(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    uint32 cmd       = 0;
    uint8 trunk_id   = 0;
    uint16 gport     = 0;
    uint16 lport      = 0;
    uint32 index     = 0;
    uint32 mem_index = 0;
    uint16 chan_id    = 0;
    uint8 lchan_id    = 0;
    uint32 field_val = 0;
    uint8 slice_id    = 0;
    uint8 max_mem_num = 0;
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;
    DsLinkAggregateChannelGroup_m ds_link_aggregate_sgmac_group;
    DsLinkAggregateChannelMember_m ds_link_aggregate_sgmac_member;
    EpeHdrAdjustChanCtl_m    epe_hdr_adjust_chan_ctl;
    BufRetrvChanStackingEn_m stacking_en;
    uint32 packet_header_en[2] = {0};
    DsLinkAggregateChannel_m linkagg_channel;

    SYS_STK_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_trunk);
    trunk_id = p_trunk->trunk_id;
    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    gport    = p_trunk->gport;
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    SYS_STK_LPORT_CHECK(lport);

    SYS_STK_EXTEND_EN(p_trunk->extend);

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);

    if (0xFF == chan_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    lchan_id = (chan_id&0x3F);

    STACKING_LOCK;

    p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }
    if(p_sys_trunk->replace_en)
    {
        SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " [STACKING] Trunk %d only support replace API operation\n", p_sys_trunk->trunk_id);
        STACKING_UNLOCK;
        return CTC_E_PARAM_CONFLICT;
    }
    if (TRUE == _sys_goldengate_stacking_find_trunk_port(p_sys_trunk, gport, &mem_index))
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_MEMBER_PORT_EXIST;
    }

    if (CTC_STK_LOAD_DYNAMIC == p_sys_trunk->mode)
    {
        max_mem_num = SYS_STK_TRUNK_DLB_MAX_MEMBERS;
    }
    else
    {
        max_mem_num = p_sys_trunk->max_mem_cnt;
    }
    if (p_sys_trunk->mem_cnt >= max_mem_num)
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_EXCEED_MAX_MEMBER_PORT;
    }
    SYS_STK_DBG_INFO("Add uplink drv lport :%d, chan_id:%d\n", lport, chan_id);

    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, chan_id, cmd, &linkagg_channel), g_gg_stacking_master[lchip]->p_stacking_mutex);
    field_val = trunk_id;
    SetDsLinkAggregateChannel(V, u1_g2_linkAggregationChannelGroup_f, &linkagg_channel, field_val);
    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, chan_id, cmd, &linkagg_channel), g_gg_stacking_master[lchip]->p_stacking_mutex);

    /*set not edit packet on stacking port*/
    field_val = 1;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_ctagOperationDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_stagOperationDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_untagDefaultVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    /*Ecap Output packet Heaer enable*/
    CTC_ERROR_RETURN(_sys_goldengate_stacking_encap_header_enable(lchip, gport, &p_sys_trunk->encap_hdr));

    slice_id = SYS_MAP_CHANID_TO_SLICE(chan_id);
    sal_memset(&epe_hdr_adjust_chan_ctl, 0, sizeof(epe_hdr_adjust_chan_ctl));
    cmd = DRV_IOR(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &epe_hdr_adjust_chan_ctl));
    GetEpeHdrAdjustChanCtl(A, packetHeaderEn_f, &epe_hdr_adjust_chan_ctl, packet_header_en);

    CTC_BIT_SET(packet_header_en[lchan_id>>5], lchan_id&0x1F);

    SetEpeHdrAdjustChanCtl(A, packetHeaderEn_f, &epe_hdr_adjust_chan_ctl, packet_header_en);
    cmd = DRV_IOW(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &epe_hdr_adjust_chan_ctl));


    sal_memset(&stacking_en, 0, sizeof(stacking_en));
    sal_memset(packet_header_en, 0, sizeof(packet_header_en));
    cmd = DRV_IOR(BufRetrvChanStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &stacking_en));
    GetBufRetrvChanStackingEn(A, chanStackingEn_f, &stacking_en, packet_header_en);
    CTC_BIT_SET(packet_header_en[lchan_id>>5], lchan_id&0x1F);
    SetBufRetrvChanStackingEn(A, chanStackingEn_f, &stacking_en, packet_header_en);
    cmd = DRV_IOW(BufRetrvChanStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &stacking_en));
    
    if (CTC_STK_LOAD_DYNAMIC == p_sys_trunk->mode)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(_sys_goldengate_stacking_dlb_add_member(lchip, trunk_id, p_sys_trunk->mem_cnt, chan_id),g_gg_stacking_master[lchip]->p_stacking_mutex);
    }
    else if (CTC_STK_LOAD_STATIC_FAILOVER == p_sys_trunk->mode)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(_sys_goldengate_stacking_failover_add_member(lchip, trunk_id, gport), g_gg_stacking_master[lchip]->p_stacking_mutex);
    }

    if(CTC_FLAG_ISSET(p_sys_trunk->flag, CTC_STACKING_TRUNK_MEM_ASCEND_ORDER))
    {
        uint8 temp_mem_idx = p_sys_trunk->mem_cnt;
        DsLinkAggregateChannelMember_m  hw_member;
        uint32 cmdr = DRV_IOR(DsLinkAggregateChannelMember_t, DRV_ENTRY_FLAG);
        uint32 cmdw = DRV_IOW(DsLinkAggregateChannelMember_t, DRV_ENTRY_FLAG);
        for(; temp_mem_idx > 0; temp_mem_idx--)
        {
            if(lport >= CTC_MAP_GPORT_TO_LPORT(p_sys_trunk->mem_ports[temp_mem_idx-1]))
            {
                break;
            }
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, temp_mem_idx+p_sys_trunk->mem_base-1, cmdr, &hw_member), g_gg_stacking_master[lchip]->p_stacking_mutex);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, temp_mem_idx+p_sys_trunk->mem_base, cmdw, &hw_member), g_gg_stacking_master[lchip]->p_stacking_mutex);

            p_sys_trunk->mem_ports[temp_mem_idx] = p_sys_trunk->mem_ports[temp_mem_idx-1];
        }

        index = temp_mem_idx;
    }
    else
    {
        index = p_sys_trunk->mem_cnt;
    }
    if (CTC_STK_LOAD_DYNAMIC != p_sys_trunk->mode)
    {
        sal_memset(&ds_link_aggregate_sgmac_member, 0, sizeof(ds_link_aggregate_sgmac_member));
        cmd = DRV_IOW(DsLinkAggregateChannelMember_t, DRV_ENTRY_FLAG);

        SetDsLinkAggregateChannelMember(V, channelId_f, &ds_link_aggregate_sgmac_member, chan_id);

        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, p_sys_trunk->mem_base+index, cmd, &ds_link_aggregate_sgmac_member), g_gg_stacking_master[lchip]->p_stacking_mutex);
    }

    /*update trunk members*/
    p_sys_trunk->mem_ports[index] = gport;
    p_sys_trunk->mem_cnt++;

    if ((0 == g_gg_stacking_master[lchip]->bind_mcast_en))
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(sys_goldengate_stacking_all_mcast_bind_en(lchip, 1), g_gg_stacking_master[lchip]->p_stacking_mutex);
    }

    STACKING_UNLOCK;

    /*for the first member in dlb mode ,need flush active */
    if ((CTC_STK_LOAD_DYNAMIC == p_sys_trunk->mode) && (1 == p_sys_trunk->mem_cnt))
    {
       CTC_ERROR_RETURN(_sys_goldengate_stacking_clear_flow_active(lchip, trunk_id));
    }

    sal_memset(&ds_link_aggregate_sgmac_group, 0, sizeof(DsLinkAggregateChannelGroup_m));
    cmd = DRV_IOR(DsLinkAggregateChannelGroup_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, trunk_id, cmd, &ds_link_aggregate_sgmac_group));
    SetDsLinkAggregateChannelGroup(V, channelLinkAggMemNum_f, &ds_link_aggregate_sgmac_group, p_sys_trunk->mem_cnt);

    cmd = DRV_IOW(DsLinkAggregateChannelGroup_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, trunk_id, cmd, &ds_link_aggregate_sgmac_group));

    if (0 == g_gg_stacking_master[lchip]->binding_trunk)
    {
        field_val = trunk_id;
        cmd = DRV_IOW(DsSrcSgmacGroup_t, DsSrcSgmacGroup_sgmacGroupId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
    }

    return CTC_E_NONE;

}

int32
sys_goldengate_stacking_remove_trunk_port(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    uint32 cmd = 0;
    uint8 trunk_id   = 0;
    uint16 gport     = 0;
    uint16 lport     = 0;
    uint16 last_lport     = 0;
    uint32 mem_index = 0;
    uint32 index = 0;
    uint16 last_gport = 0;
    uint32 field_val = 0;
    uint16 chan_id    = 0;
    uint16 chan_tmp    = 0;
    uint8 lchan_id    = 0;
    uint8 slice_id    = 0;
    sys_stacking_trunk_info_t* p_sys_trunk;
    DsLinkAggregateChannelMember_m ds_link_aggregate_sgmac_member;
    EpeHdrAdjustChanCtl_m     epe_hdr_adjust_chan_ctl;
    uint32 packet_header_en[2] = {0};
    BufRetrvChanStackingEn_m stacking_en;
    uint8 port_ref_cnt = 0;
    int32 ret = CTC_E_NONE;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trunk);

    trunk_id = p_trunk->trunk_id;
    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);
    gport    = p_trunk->gport;
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    SYS_STK_LPORT_CHECK(lport);

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == chan_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    lchan_id = (chan_id&0x3F);

    SYS_STK_EXTEND_EN(p_trunk->extend);

    STACKING_LOCK;

    p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }
    if(p_sys_trunk->replace_en)
    {
        SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " [STACKING] Trunk %d only support replace API operation\n", p_sys_trunk->trunk_id);
        STACKING_UNLOCK;
        return CTC_E_PARAM_CONFLICT;
    }
    if (FALSE == _sys_goldengate_stacking_find_trunk_port(p_sys_trunk, gport, &mem_index))
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_MEMBER_PORT_NOT_EXIST;
    }

    SYS_STK_DBG_INFO("Remove drv lport %d, mem_index = %d\n", lport, mem_index);

    _sys_goldengate_stacking_get_port_ref_cnt(lchip, gport, &port_ref_cnt, 0xFFFF);
    if (1 == port_ref_cnt)/*members in one trunk*/
    {
        sys_goldengate_queue_set_port_drop_en(lchip, gport, TRUE);
        CTC_ERROR_GOTO(_sys_goldengate_stacking_check_port_status(lchip, gport), ret, end);
    }

    if (mem_index < (p_sys_trunk->mem_cnt - 1))
    {

        if (CTC_FLAG_ISSET(p_sys_trunk->flag, CTC_STACKING_TRUNK_MEM_ASCEND_ORDER))
        {
            uint8 tmp_index = mem_index;
            DsLinkAggregateChannelMember_m  hw_member;
            uint32 cmdr = DRV_IOR(DsLinkAggregateChannelMember_t, DRV_ENTRY_FLAG);
            uint32 cmdw = DRV_IOW(DsLinkAggregateChannelMember_t, DRV_ENTRY_FLAG);

            for(;tmp_index < p_sys_trunk->mem_cnt-1; tmp_index++)
            {
                CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, tmp_index+p_sys_trunk->mem_base+1, cmdr, &hw_member), g_gg_stacking_master[lchip]->p_stacking_mutex);
                CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, tmp_index+p_sys_trunk->mem_base, cmdw, &hw_member), g_gg_stacking_master[lchip]->p_stacking_mutex);
                p_sys_trunk->mem_ports[tmp_index] = p_sys_trunk->mem_ports[tmp_index+1];
            }
        }
        else
        {
            /*Need replace this member using last member*/
            sal_memset(&ds_link_aggregate_sgmac_member, 0, sizeof(ds_link_aggregate_sgmac_member));
            last_gport = p_sys_trunk->mem_ports[p_sys_trunk->mem_cnt - 1];
            p_sys_trunk->mem_ports[mem_index] = last_gport;

            if (CTC_STK_LOAD_DYNAMIC != p_sys_trunk->mode)
            {
                last_lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(last_gport);

                chan_tmp = SYS_GET_CHANNEL_ID(lchip, last_gport);
                if (0xFF == chan_tmp)
                {
                    ret = CTC_E_INVALID_LOCAL_PORT;
                    goto end;
                }

                SetDsLinkAggregateChannelMember(V, channelId_f, &ds_link_aggregate_sgmac_member, chan_tmp);

                SYS_STK_DBG_INFO("Need replace this member using last member drv lport:%d, chan:%d\n", last_lport, chan_tmp);

                index = p_sys_trunk->mem_base + mem_index;
                cmd = DRV_IOW(DsLinkAggregateChannelMember_t, DRV_ENTRY_FLAG);
                CTC_ERROR_GOTO(DRV_IOCTL(lchip, index, cmd, &ds_link_aggregate_sgmac_member), ret, end);
            }
        }
    }

    /*update trunk members*/
    p_sys_trunk->mem_cnt--;

    field_val = p_sys_trunk->mem_cnt;
    cmd = DRV_IOW(DsLinkAggregateChannelGroup_t, DsLinkAggregateChannelGroup_channelLinkAggMemNum_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, trunk_id, cmd, &field_val), ret, end);

    if (CTC_STK_LOAD_DYNAMIC == p_sys_trunk->mode)
    {
        CTC_ERROR_GOTO(_sys_goldengate_stacking_dlb_del_member(lchip, trunk_id, mem_index, p_sys_trunk->mem_cnt, chan_id), ret, end);
    }
    else if (CTC_STK_LOAD_STATIC_FAILOVER == p_sys_trunk->mode)
    {
        CTC_ERROR_GOTO(_sys_goldengate_stacking_failover_del_member(lchip, trunk_id, gport), ret, end);
    }


    if (port_ref_cnt > 1)/*the members is still in other trunk, must not disable stacking port*/
    {
        STACKING_UNLOCK;
        return CTC_E_NONE;
    }


    if (0 == g_gg_stacking_master[lchip]->binding_trunk)
    {
        /*Cancel SrcSgmac and Chan mapping*/
        /* SrcSgmac and Chan mapping*/
        field_val = 0;
        cmd = DRV_IOW(DsSrcSgmacGroup_t, DsSrcSgmacGroup_sgmacGroupId_f);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, chan_id, cmd, &field_val), ret, end);
    }
    /*recovery edit packet on port*/
    field_val = 0;
    CTC_ERROR_GOTO(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_VLAN_OPERATION_DIS, field_val), ret, end);
    field_val = 1;
    CTC_ERROR_GOTO(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_UNTAG_DEF_VLAN_EN, field_val), ret, end);
    /*Ecap Output packet Heaer disable*/
    CTC_ERROR_GOTO(_sys_goldengate_stacking_encap_header_disable(lchip, gport), ret, end);

    /*Ecap Output packet Heaer disable*/
    slice_id = SYS_MAP_CHANID_TO_SLICE(chan_id);
    sal_memset(&epe_hdr_adjust_chan_ctl, 0, sizeof(epe_hdr_adjust_chan_ctl));
    cmd = DRV_IOR(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, slice_id, cmd, &epe_hdr_adjust_chan_ctl), ret, end);
    GetEpeHdrAdjustChanCtl(A, packetHeaderEn_f, &epe_hdr_adjust_chan_ctl, packet_header_en);
    CTC_BIT_UNSET(packet_header_en[lchan_id>>5], lchan_id&0x1F);
    SetEpeHdrAdjustChanCtl(A, packetHeaderEn_f, &epe_hdr_adjust_chan_ctl, packet_header_en);
    cmd = DRV_IOW(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, slice_id, cmd, &epe_hdr_adjust_chan_ctl), ret, end);

    sal_memset(&stacking_en, 0, sizeof(stacking_en));
    sal_memset(packet_header_en, 0, sizeof(packet_header_en));
    cmd = DRV_IOR(BufRetrvChanStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, slice_id, cmd, &stacking_en), ret, end);
    GetBufRetrvChanStackingEn(A, chanStackingEn_f, &stacking_en, packet_header_en);
    CTC_BIT_UNSET(packet_header_en[lchan_id>>5], lchan_id&0x1F);
    SetBufRetrvChanStackingEn(A, chanStackingEn_f, &stacking_en, packet_header_en);
    cmd = DRV_IOW(BufRetrvChanStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, slice_id, cmd, &stacking_en), ret, end);

    if ((1 == g_gg_stacking_master[lchip]->bind_mcast_en)
        && (0 == _sys_goldengate_stacking_is_trunk_have_member(lchip)))
    {
        CTC_ERROR_GOTO(sys_goldengate_stacking_all_mcast_bind_en(lchip, 0), ret, end);
    }

    sys_goldengate_queue_set_port_drop_en(lchip, gport, FALSE);
    STACKING_UNLOCK;
    return CTC_E_NONE;

end:
    sys_goldengate_queue_set_port_drop_en(lchip, gport, FALSE);
    STACKING_UNLOCK;
    return ret;

}
STATIC int32
_sys_goldengate_stacking_set_stacking_port_en(uint8 lchip, sys_stacking_trunk_info_t* p_trunk, uint32 gport, uint8 is_add)
{
    EpeHdrAdjustChanCtl_m    epe_hdr_adjust_chan_ctl;
    BufRetrvChanStackingEn_m stacking_en;
    uint32 packet_header_en[2] = {0};
    uint32 field_val;
    uint32 cmd;
    uint16 lport;
    uint16 chan_id;
    uint8  slice_id;
    uint8  lchan_id;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    SYS_STK_LPORT_CHECK(lport);

    SYS_STK_DBG_FUNC();

    /*set not edit packet on stacking port*/
    field_val = is_add ? 1:0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_ctagOperationDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_stagOperationDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = is_add ? 0:1;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_untagDefaultVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == chan_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    lchan_id = (chan_id&0x3F);
    slice_id = SYS_MAP_CHANID_TO_SLICE(chan_id);

    cmd = DRV_IOR(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &epe_hdr_adjust_chan_ctl));
    GetEpeHdrAdjustChanCtl(A, packetHeaderEn_f, &epe_hdr_adjust_chan_ctl, packet_header_en);

    if(is_add)
    {
        CTC_BIT_SET(packet_header_en[lchan_id>>5], lchan_id&0x1F);
    }
    else
    {
        CTC_BIT_UNSET(packet_header_en[lchan_id>>5], lchan_id&0x1F);
    }
    SetEpeHdrAdjustChanCtl(A, packetHeaderEn_f, &epe_hdr_adjust_chan_ctl, packet_header_en);
    cmd = DRV_IOW(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &epe_hdr_adjust_chan_ctl));


    cmd = DRV_IOR(BufRetrvChanStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &stacking_en));
    GetBufRetrvChanStackingEn(A, chanStackingEn_f, &stacking_en, packet_header_en);
    if(is_add)
    {
        CTC_BIT_SET(packet_header_en[lchan_id>>5], lchan_id&0x1F);
    }
    else
    {
        CTC_BIT_UNSET(packet_header_en[lchan_id>>5], lchan_id&0x1F);
    }
    SetBufRetrvChanStackingEn(A, chanStackingEn_f, &stacking_en, packet_header_en);
    cmd = DRV_IOW(BufRetrvChanStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &stacking_en));

    if(is_add)
    {
        /*Ecap Output packet Heaer enable*/
        CTC_ERROR_RETURN(_sys_goldengate_stacking_encap_header_enable(lchip, gport, &p_trunk->encap_hdr));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_stacking_encap_header_disable( lchip,  gport));
    }
    if(0 == g_gg_stacking_master[lchip]->binding_trunk)
    {
        field_val = is_add ? p_trunk->trunk_id :0;
        cmd = DRV_IOW(DsSrcSgmacGroup_t, DsSrcSgmacGroup_sgmacGroupId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_stacking_trunk_change_port(uint8 lchip, sys_stacking_trunk_info_t* p_sys_trunk,
                                           uint32 gport, uint8 enable)
{
    int32 ret = 0;
    uint8 port_ref_cnt = 0;

    _sys_goldengate_stacking_get_port_ref_cnt(lchip, gport, &port_ref_cnt, p_sys_trunk->trunk_id);
    if (port_ref_cnt > 0)
    {
        return CTC_E_NONE;
    }

    sys_goldengate_queue_set_port_drop_en(lchip, gport, TRUE);

    CTC_ERROR_GOTO(_sys_goldengate_stacking_check_port_status(lchip, gport), ret, Label);

    CTC_ERROR_GOTO(_sys_goldengate_stacking_set_stacking_port_en(lchip, p_sys_trunk, gport, enable), ret, Label);

Label:
    sys_goldengate_queue_set_port_drop_en(lchip, gport, FALSE);
    return ret;

}
STATIC int32
_sys_goldengate_stacking_find_no_dupicated(uint8 lchip, uint32* new_members, uint8 new_cnt,
                                          uint32* old_members, uint8 old_cnt,
                                          uint32* find_members, uint8* find_cnt)
{
    uint16 count = 0;
    uint16 loop, loop1;
    uint8  is_new_member;
    uint8  duplicated_member;
    uint32  temp_port;

    for(loop=0;loop<new_cnt; loop++)
    {
        if (0xFF == SYS_GET_CHANNEL_ID(lchip, new_members[loop]))
        {
           return CTC_E_INVALID_LOCAL_PORT;
        }
        temp_port = new_members[loop];
        is_new_member = 1;
        for(loop1=0; loop1 < old_cnt; loop1++)
        {
            if(temp_port == old_members[loop1])
            {
                is_new_member = 0;
                break;
            }
        }
        if(is_new_member)
        {
            duplicated_member = 0;
            for(loop1=0; loop1 < count; loop1++)
            {
                if(temp_port == find_members[loop1])
                {
                    duplicated_member = 1;
                    break;
                }
            }

            if(!duplicated_member)
            {
                find_members[count++] = temp_port;
            }
        }
    }
    *find_cnt = count;
    return CTC_E_NONE;
}

int32
sys_goldengate_stacking_replace_trunk_ports(uint8 lchip, uint8 trunk_id, uint32* gports, uint8 mem_ports)
{
    int32  ret = 0;
    uint32 cmd;
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;
    sys_stacking_trunk_info_t  bk_trunk;
    uint32 fld_val = 0;
    uint8  loop = 0;
    uint8  loop_add = 0;
    uint8  loop_del = 0;
    uint8  add_ports_num;
    uint8  del_ports_num;
    uint32 add_ports[SYS_STK_TRUNK_MAX_MEMBERS] = {0};
    uint32 del_ports[SYS_STK_TRUNK_MAX_MEMBERS] = {0};
    uint16 chan_id = 0;
    uint8  bk_replace_en = 0;
    uint32 field_val = 0;

    SYS_STK_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(gports);
    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    SYS_STK_DBG_FUNC();
    STACKING_LOCK;
    p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }
    sal_memset(&bk_trunk, 0, sizeof(sys_stacking_trunk_info_t));
    sal_memcpy(&bk_trunk, p_sys_trunk, sizeof(sys_stacking_trunk_info_t));

    if (CTC_STK_LOAD_STATIC != p_sys_trunk->mode)
    {
        STACKING_UNLOCK;
        return CTC_E_NOT_SUPPORT;
    }
    if (mem_ports > p_sys_trunk->max_mem_cnt)
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_EXCEED_MAX_MEMBER_PORT;
    }

    /*1.find add ports */
    /*2.find delete ports */
    CTC_ERROR_GOTO(_sys_goldengate_stacking_find_no_dupicated(lchip, gports, mem_ports, p_sys_trunk->mem_ports,\
                                                        p_sys_trunk->mem_cnt, add_ports, &add_ports_num),ret, error_proc0);
    CTC_ERROR_GOTO(_sys_goldengate_stacking_find_no_dupicated(lchip, p_sys_trunk->mem_ports, p_sys_trunk->mem_cnt, gports,\
                                                                    mem_ports, del_ports, &del_ports_num), ret ,error_proc0);

    /*3. set add ports, change normal port to stacking port */
    for (loop_add = 0; loop_add < add_ports_num; loop_add++)
    {
        CTC_ERROR_GOTO(_sys_goldengate_stacking_trunk_change_port(lchip,  p_sys_trunk, add_ports[loop_add], 1), ret, error_proc);
    }

    /*4. set delete ports, change stacking port to normal port*/
    for (loop_del = 0; loop_del < del_ports_num; loop_del++)
    {
        CTC_ERROR_GOTO(_sys_goldengate_stacking_trunk_change_port(lchip,  p_sys_trunk, del_ports[loop_del], 0), ret, error_proc);
    }

    bk_trunk.max_mem_cnt = mem_ports;
    if (CTC_E_NONE == _sys_goldengate_stacking_trunk_alloc_mem_base(lchip, &bk_trunk))
    {
        for (loop = 0; loop < mem_ports; loop++)
        {
            chan_id = SYS_GET_CHANNEL_ID(lchip,  gports[loop]);
            fld_val = chan_id;
            cmd = DRV_IOW(DsLinkAggregateChannelMember_t, DsLinkAggregateChannelMember_channelId_f);
            DRV_IOCTL(lchip, bk_trunk.mem_base + loop, cmd, &fld_val);
        }
        bk_replace_en = 1;
        field_val = bk_trunk.mem_base;
        cmd = DRV_IOW(DsLinkAggregateChannelGroup_t, DsLinkAggregateChannelGroup_channelLinkAggMemBase_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, trunk_id, cmd, &field_val));        
    }
    
    /*5. replace per port,don't need rollback !!!!!!!!!!!!!!*/
    for (loop = 0; loop < mem_ports; loop++)
    {
        chan_id = SYS_GET_CHANNEL_ID(lchip,  gports[loop]);
        fld_val = chan_id;
        cmd = DRV_IOW(DsLinkAggregateChannelMember_t, DsLinkAggregateChannelMember_channelId_f);
        DRV_IOCTL(lchip, p_sys_trunk->mem_base + loop, cmd, &fld_val);

        fld_val = p_sys_trunk->trunk_id;
        cmd = DRV_IOW(DsLinkAggregateChannel_t, DsLinkAggregateChannel_u1_g2_linkAggregationChannelGroup_f);
        DRV_IOCTL(lchip, chan_id, cmd, &fld_val);
    }

    fld_val = mem_ports;
    cmd = DRV_IOW(DsLinkAggregateChannelGroup_t, DsLinkAggregateChannelGroup_channelLinkAggMemNum_f);
    DRV_IOCTL(lchip, trunk_id, cmd, &fld_val);

    if (!g_gg_stacking_master[lchip]->bind_mcast_en && mem_ports)
    {
        sys_goldengate_stacking_all_mcast_bind_en(lchip, 1);
    }

    if ((1 == g_gg_stacking_master[lchip]->bind_mcast_en)
        && (0 == _sys_goldengate_stacking_is_trunk_have_member(lchip)))
    {
        sys_goldengate_stacking_all_mcast_bind_en(lchip, 0);
    }

    /*update softtable*/
    p_sys_trunk->mem_cnt = mem_ports;
    if(mem_ports)
    {
        sal_memcpy(p_sys_trunk->mem_ports, gports, mem_ports*sizeof(uint32));
    }

    p_sys_trunk->replace_en = 1;

    if (bk_replace_en)
    {
        _sys_goldengate_stacking_trunk_free_mem_base(lchip, &bk_trunk); 
        field_val = p_sys_trunk->mem_base;
        cmd = DRV_IOW(DsLinkAggregateChannelGroup_t, DsLinkAggregateChannelGroup_channelLinkAggMemBase_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, trunk_id, cmd, &field_val));        
    }
    STACKING_UNLOCK;
    return ret;


error_proc:
    for (loop = 0; loop < loop_add; loop++)
    {
        _sys_goldengate_stacking_trunk_change_port(lchip,  p_sys_trunk, add_ports[loop], 0);
    }

    for (loop = 0; loop < loop_del; loop++)
    {
        _sys_goldengate_stacking_trunk_change_port(lchip,  p_sys_trunk, del_ports[loop], 1);
    }


error_proc0:
    STACKING_UNLOCK;
    return ret;
}
int32
sys_goldengate_stacking_get_member_ports(uint8 lchip, uint8 trunk_id, uint32* p_gports, uint8* cnt)
{
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_gports);
    CTC_PTR_VALID_CHECK(cnt);

    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    STACKING_LOCK;

    p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }

    STACKING_UNLOCK;


    sal_memcpy(p_gports, p_sys_trunk->mem_ports, sizeof(uint32)*p_sys_trunk->mem_cnt);

    *cnt = p_sys_trunk->mem_cnt;

    return CTC_E_NONE;
}

int32
sys_goldengate_stacking_add_trunk_rchip(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    uint32 cmd = 0;
    uint8 trunk_id   = 0;
    uint8 rgchip     = 0;
    uint32 index = 0;
    uint32 field_val = 0;
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trunk);


    trunk_id = p_trunk->trunk_id;
    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    SYS_STK_EXTEND_EN(p_trunk->extend);

    rgchip    = p_trunk->remote_gchip;
    SYS_GLOBAL_CHIPID_CHECK(rgchip);

    /*if gchip is local, return */
    if (p_trunk->extend.enable)
    {
        if (rgchip == p_trunk->extend.gchip)
        {
            return CTC_E_NONE;
        }
    }
    else
    {
        if (sys_goldengate_chip_is_local(lchip, rgchip))
        {
            return CTC_E_NONE;
        }
    }

    index = rgchip;
    cmd = DRV_IOR(DsSgmacMap_t, DsSgmacMap_linkAggregationChannelGroup_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
    p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, field_val);
    if (p_sys_trunk)
    {
        CTC_BIT_UNSET(p_sys_trunk->rgchip_bitmap, rgchip);
    }

    p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }

    CTC_BIT_SET(p_sys_trunk->rgchip_bitmap, rgchip);

    index = rgchip;
    field_val = trunk_id;
    cmd = DRV_IOW(DsSgmacMap_t, DsSgmacMap_linkAggregationChannelGroup_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

    /*Record the remote chip from the local trunk*/
    g_gg_stacking_master[lchip]->neigbor_chip[rgchip] = trunk_id;

    return CTC_E_NONE;

}

int32
sys_goldengate_stacking_remove_trunk_rchip(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    uint32 cmd = 0;
    uint8 trunk_id   = 0;
    uint8 rgchip     = 0;
    uint32 index = 0;
    uint32 field_val = 0;
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trunk);

    trunk_id = p_trunk->trunk_id;
    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    SYS_STK_EXTEND_EN(p_trunk->extend);

    rgchip    = p_trunk->remote_gchip;
    SYS_GLOBAL_CHIPID_CHECK(rgchip);

    /*if gchip is local, return */
    if (p_trunk->extend.enable)
    {
        if (rgchip == p_trunk->extend.gchip)
        {
            return CTC_E_NONE;
        }
    }
    else
    {
        if (sys_goldengate_chip_is_local(lchip, rgchip))
        {
            return CTC_E_NONE;
        }
    }

    p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }

    if (!CTC_IS_BIT_SET(p_sys_trunk->rgchip_bitmap, rgchip))
    {
        return CTC_E_NONE;
    }
    CTC_BIT_UNSET(p_sys_trunk->rgchip_bitmap, rgchip);

    index = rgchip;
    field_val = 0; /*Need Consideration, update new spec*/
    cmd = DRV_IOW(DsSgmacMap_t, DsSgmacMap_linkAggregationChannelGroup_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));


    g_gg_stacking_master[lchip]->neigbor_chip[rgchip] = 0;

    return CTC_E_NONE;

}

int32
sys_goldengate_stacking_get_trunk_rchip(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trunk);

    SYS_GLOBAL_CHIPID_CHECK(p_trunk->remote_gchip);

    cmd = DRV_IOR(DsSgmacMap_t, DsSgmacMap_linkAggregationChannelGroup_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_trunk->remote_gchip, cmd, &field_val));

    p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, field_val);
    if (NULL == p_sys_trunk)
    {
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }

    p_trunk->trunk_id = field_val;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stacking_set_stop_rchip(uint8 lchip, ctc_stacking_stop_rchip_t *p_stop_rchip)
{
    uint32 cmd       = 0;
    uint32 field_val = 0;


    field_val = p_stop_rchip->rchip_bitmap;

    cmd = DRV_IOW(MetFifoCtl_t, MetFifoCtl_endRemoteChip_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_stacking_get_stop_rchip(uint8 lchip, ctc_stacking_stop_rchip_t *p_stop_rchip)
{
    uint32 cmd       = 0;
    uint32 field_val = 0;


    cmd = DRV_IOR(MetFifoCtl_t, MetFifoCtl_endRemoteChip_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    p_stop_rchip->rchip_bitmap = field_val;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stacking_set_break_en(uint8 lchip, uint32 enable)
{
    uint32 cmd       = 0;
    uint32 field_val = 0;

    field_val = enable;
    cmd = DRV_IOW(MetFifoCtl_t, MetFifoCtl_stackingBroken_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stacking_get_break_en(uint8 lchip, uint32* enable)
{
    uint32 cmd       = 0;
    uint32 field_val = 0;

    cmd = DRV_IOR(MetFifoCtl_t, MetFifoCtl_stackingBroken_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    *enable = field_val;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stacking_set_full_mesh_en(uint8 lchip, uint32 enable)
{
    uint32 cmd       = 0;
    uint32 field_val = 0;

    field_val = enable ? 0 : 1;
    cmd = DRV_IOW(MetFifoCtl_t, MetFifoCtl_forceReplicationFromFabric_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stacking_get_full_mesh_en(uint8 lchip, uint32* enable)
{
    uint32 cmd       = 0;
    uint32 field_val = 0;

    cmd = DRV_IOR(MetFifoCtl_t, MetFifoCtl_forceReplicationFromFabric_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    *enable = field_val ? 0 : 1;

    return CTC_E_NONE;
}

int32
sys_goldengate_stacking_set_global_cfg(uint8 lchip, ctc_stacking_glb_cfg_t* p_glb_cfg)
{

    CTC_ERROR_RETURN(_sys_goldengate_stacking_hdr_set(lchip, &p_glb_cfg->hdr_glb));

    return CTC_E_NONE;
}

int32
sys_goldengate_stacking_get_global_cfg(uint8 lchip, ctc_stacking_glb_cfg_t* p_glb_cfg)
{


    CTC_ERROR_RETURN(_sys_goldengate_stacking_hdr_get(lchip, &p_glb_cfg->hdr_glb));

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_stacking_set_break_truck_id(uint8 lchip, ctc_stacking_mcast_break_point_t *p_break_point)
{
    uint32 cmd = 0;
    MetFifoCtl_m met_fifo_ctl;
    uint32 source_bitmap[2] = {0};

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_break_point);
    SYS_STK_TRUNKID_RANGE_CHECK(p_break_point->trunk_id)

    sal_memset(&met_fifo_ctl, 0, sizeof(met_fifo_ctl));
    cmd = DRV_IOR(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &met_fifo_ctl));
    GetMetFifoCtl(A, sgmacDiscardSourceReplication_f, &met_fifo_ctl, source_bitmap);

    if (p_break_point->enable)
    {
        if (p_break_point->trunk_id >= 32)
        {
            CTC_BIT_SET(source_bitmap[1], (p_break_point->trunk_id - 32));
        }
        else
        {
            CTC_BIT_SET(source_bitmap[0], p_break_point->trunk_id );
        }
    }
    else
    {
        if (p_break_point->trunk_id >= 32)
        {
            CTC_BIT_UNSET(source_bitmap[1], (p_break_point->trunk_id - 32));
        }
        else
        {
            CTC_BIT_UNSET(source_bitmap[0], p_break_point->trunk_id );
        }
    }

    SetMetFifoCtl(A, sgmacDiscardSourceReplication_f, &met_fifo_ctl, source_bitmap);
    cmd = DRV_IOW(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &met_fifo_ctl));

    return CTC_E_NONE;

}
int32
sys_goldengate_stacking_set_rchip_port(uint8 lchip, ctc_stacking_rchip_port_t * p_rchip_port)
{
    uint16 loop1                   = 0;
    uint16 loop2                   = 0;
    uint16 lport                   = 0;
    uint32 gport                   = 0;
    uint8 gchip_id                 = 0;
    uint16 valid_cnt               = 0;
    uint32 max_port_num_slice[2]      = {0};
    uint16 rsv_num                 = 0;
    uint8  slice_en            = 0;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_rchip_port);
    SYS_GLOBAL_CHIPID_CHECK(p_rchip_port->rchip);

    sys_goldengate_get_gchip_id(lchip, &gchip_id);


    for (loop1 = 0; loop1 < sizeof(ctc_port_bitmap_t) / sizeof(uint32); loop1++)
    {
        for (loop2 = 0; loop2 < CTC_UINT32_BITS; loop2++)
        {
            lport = loop1 * CTC_UINT32_BITS + loop2;

            gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(p_rchip_port->rchip, lport);

            if (CTC_IS_BIT_SET(p_rchip_port->pbm[loop1], loop2))
            {
                max_port_num_slice[lport/256] = lport % 256 + 1;
                valid_cnt++;
                sys_goldengate_brguc_nh_create(lchip, gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC);
            }
            else
            {
                sys_goldengate_brguc_nh_delete(lchip, gport);
            }
        }
    }

    if ((max_port_num_slice[0] == 0) || (max_port_num_slice[1] == 0))
    {
        rsv_num = max_port_num_slice[(max_port_num_slice[0] >= max_port_num_slice[1]) ? 0 : 1];
    }
    else
    {
         /*rsv_num = double max port of one slice if has slice1 port*/
        rsv_num = max_port_num_slice[(max_port_num_slice[0] >= max_port_num_slice[1]) ? 0 : 1] * 2;
    }
    if (max_port_num_slice[0])
    {
        CTC_BIT_SET(slice_en, 0);
    }
    if (max_port_num_slice[1])
    {
        CTC_BIT_SET(slice_en, 1);
    }

    CTC_ERROR_RETURN(sys_goldengate_l2_free_rchip_ad_index(lchip, p_rchip_port->rchip));
    if (valid_cnt)
    {
        CTC_ERROR_RETURN(sys_goldengate_l2_reserve_rchip_ad_index(lchip, p_rchip_port->rchip, rsv_num, slice_en));
    }

    return CTC_E_NONE;

}


int32
sys_goldengate_stacking_binding_trunk(uint8 lchip, uint32 gport, uint8 trunk_id)/*trunk_id = 0 means unbinding*/
{
    uint32 field_val = 0;
    uint8 chan_id = 0;
    uint32 cmd = 0;
	uint32 lport = 0;

    SYS_STK_INIT_CHECK(lchip);

    if (trunk_id > SYS_STK_TRUNK_MAX_ID)
    {
        return CTC_E_STK_INVALID_TRUNK_ID;
    }

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xff == chan_id)
    {
        return CTC_E_INVALID_PORT;
    }
    field_val = trunk_id;
    cmd = DRV_IOW(DsSrcSgmacGroup_t, DsSrcSgmacGroup_sgmacGroupId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
    g_gg_stacking_master[lchip]->binding_trunk = 1; /*change to 1 once call by user*/

    return CTC_E_NONE;
}

int32
sys_goldengate_stacking_get_binding_trunk(uint8 lchip, uint32 gport, uint8* trunk_id)/*trunk_id = 0 means unbinding*/
{
    uint32 field_val = 0;
    uint8 chan_id = 0;
    uint32 cmd = 0;
	uint32 lport = 0;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(trunk_id);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xff == chan_id)
    {
        return CTC_E_INVALID_PORT;
    }

    cmd = DRV_IOR(DsSrcSgmacGroup_t, DsSrcSgmacGroup_sgmacGroupId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
	*trunk_id = field_val;

    return CTC_E_NONE;
}


int32
sys_goldengate_stacking_set_property(uint8 lchip, ctc_stacking_property_t* p_prop)
{
    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_prop);

    switch (p_prop->prop_type)
    {

    case CTC_STK_PROP_GLOBAL_CONFIG:
        {
            ctc_stacking_glb_cfg_t* p_glb_cfg = NULL;
            p_glb_cfg = (ctc_stacking_glb_cfg_t *)p_prop->p_value;
            CTC_ERROR_RETURN(sys_goldengate_stacking_set_global_cfg(lchip, p_glb_cfg));
        }
        break;

    case CTC_STK_PROP_MCAST_STOP_RCHIP:
        {
            ctc_stacking_stop_rchip_t* p_stop_rchip = NULL;
            p_stop_rchip = (ctc_stacking_stop_rchip_t *)p_prop->p_value;
            CTC_ERROR_RETURN(_sys_goldengate_stacking_set_stop_rchip(lchip, p_stop_rchip));
        }
        break;

    case CTC_STK_PROP_BREAK_EN:
        CTC_ERROR_RETURN(_sys_goldengate_stacking_set_break_en(lchip, p_prop->value));
        break;

    case CTC_STK_PROP_FULL_MESH_EN:
        CTC_ERROR_RETURN(_sys_goldengate_stacking_set_full_mesh_en(lchip, p_prop->value));
        break;

    case CTC_STK_PROP_MCAST_BREAK_POINT:
        {
            ctc_stacking_mcast_break_point_t *p_break_point = NULL;
            p_break_point = (ctc_stacking_mcast_break_point_t *)p_prop->p_value;
            CTC_ERROR_RETURN(_sys_goldengate_stacking_set_break_truck_id(lchip, p_break_point));
        }
        break;

    case CTC_STK_PROP_PORT_BIND_TRUNK:
        {
            ctc_stacking_bind_trunk_t *p_bind_trunk = NULL;
            p_bind_trunk = (ctc_stacking_bind_trunk_t *)p_prop->p_value;
			CTC_PTR_VALID_CHECK(p_bind_trunk);
            CTC_ERROR_RETURN(sys_goldengate_stacking_binding_trunk(lchip, p_bind_trunk->gport, p_bind_trunk->trunk_id));
        }
        break;

    case CTC_STK_PROP_RCHIP_PORT_EN:
        {
            ctc_stacking_rchip_port_t *p_rchip_port = NULL;
            p_rchip_port = (ctc_stacking_rchip_port_t *)p_prop->p_value;
            CTC_ERROR_RETURN(sys_goldengate_stacking_set_rchip_port(lchip, p_rchip_port));
        }
        break;

    default:
        return CTC_E_NONE;
    }

    return CTC_E_NONE;

}

int32
sys_goldengate_stacking_get_property(uint8 lchip, ctc_stacking_property_t* p_prop)
{
    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_prop);

    switch (p_prop->prop_type)
    {

    case CTC_STK_PROP_GLOBAL_CONFIG:
        CTC_ERROR_RETURN(sys_goldengate_stacking_get_global_cfg(lchip, (ctc_stacking_glb_cfg_t *)p_prop->p_value));
        break;

    case CTC_STK_PROP_MCAST_STOP_RCHIP:
        CTC_ERROR_RETURN(_sys_goldengate_stacking_get_stop_rchip(lchip, (ctc_stacking_stop_rchip_t *)p_prop->p_value));
        break;

    case CTC_STK_PROP_BREAK_EN:
        CTC_ERROR_RETURN(_sys_goldengate_stacking_get_break_en(lchip, &p_prop->value));
        break;

    case CTC_STK_PROP_FULL_MESH_EN:
        CTC_ERROR_RETURN(_sys_goldengate_stacking_get_full_mesh_en(lchip, &p_prop->value));
        break;

    case CTC_STK_PROP_PORT_BIND_TRUNK:
        {
            ctc_stacking_bind_trunk_t *p_bind_trunk = NULL;
            p_bind_trunk = (ctc_stacking_bind_trunk_t *)p_prop->p_value;
			CTC_PTR_VALID_CHECK(p_bind_trunk);
            CTC_ERROR_RETURN(sys_goldengate_stacking_get_binding_trunk(lchip, p_bind_trunk->gport, &p_bind_trunk->trunk_id));
        }
        break
			;
    default:
        return CTC_E_NONE;
    }

    return CTC_E_NONE;

}


#define _____MCAST_TRUNK_GROUP_____ ""

int32
sys_goldengate_stacking_set_trunk_mcast_profile(uint8 lchip, ctc_stacking_trunk_mcast_profile_t* p_mcast_profile)
{
    int32 ret = CTC_E_NONE;
    sys_stacking_mcast_db_t mcast_db;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;

    SYS_STK_INIT_CHECK(lchip);
    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "profile id: %d \n", p_mcast_profile->mcast_profile_id);

    if (p_mcast_profile->mcast_profile_id >= 256)
    {
        return CTC_E_INVALID_PARAM;
    }

    STACKING_LOCK;

    /*lookup*/
    sal_memset(&mcast_db, 0, sizeof(mcast_db));

    mcast_db.type = 0;
    mcast_db.id   = p_mcast_profile->mcast_profile_id;

    p_mcast_db = ctc_hash_lookup(g_gg_stacking_master[lchip]->mcast_hash, &mcast_db);

    switch(p_mcast_profile->type)
    {
    case CTC_STK_MCAST_PROFILE_CREATE:
        {
            uint16 mcast_profile_id = 0;

            if (0 == p_mcast_profile->mcast_profile_id)
            {
                sys_goldengate_opf_t  opf;
                /*alloc profile id*/
                uint32 value_32 = 0;

                sal_memset(&opf, 0, sizeof(opf));
                opf.pool_type  = g_gg_stacking_master[lchip]->opf_type;
                opf.pool_index = SYS_STK_OPF_MCAST_PROFILE;
                CTC_ERROR_GOTO(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &value_32),
                                                                       ret, Error);
                mcast_profile_id = value_32;

            }
            else
            {
                if (NULL != p_mcast_db)
                {
                    ret = CTC_E_ENTRY_EXIST;
                    goto Error;
                }

                mcast_profile_id = p_mcast_profile->mcast_profile_id;
            }

            ret = sys_goldengate_stacking_mcast_group_create(lchip, 0,
                                                              mcast_profile_id,
                                                              &p_mcast_db,
                                                              p_mcast_profile->append_en);

            if (CTC_E_NONE != ret) /*roolback*/
            {
                sys_goldengate_opf_t  opf;
                sal_memset(&opf, 0, sizeof(opf));
                opf.pool_type  = g_gg_stacking_master[lchip]->opf_type;
                opf.pool_index = SYS_STK_OPF_MCAST_PROFILE;
                sys_goldengate_opf_free_offset(lchip, &opf, 1, mcast_profile_id);
                goto Error;
            }

            if (0 == p_mcast_profile->mcast_profile_id)
            {
                if (p_mcast_db)
                {
                    p_mcast_db->alloc_id = 1;
                }
                p_mcast_profile->mcast_profile_id = mcast_profile_id;
            }


        }

        break;

    case CTC_STK_MCAST_PROFILE_DESTROY:
        {
            if (0 == p_mcast_profile->mcast_profile_id)
            {
                ret = CTC_E_NONE;
                goto Error;
            }

            if (NULL == p_mcast_db)
            {
                ret = CTC_E_ENTRY_NOT_EXIST;
                goto Error;
            }

            CTC_ERROR_GOTO(sys_goldengate_stacking_mcast_group_destroy(lchip, 0,
                                                                       p_mcast_profile->mcast_profile_id, p_mcast_db),
                                                                       ret, Error);

            if ( 1 == p_mcast_db->alloc_id)
            {
                /*alloc profile id*/
                sys_goldengate_opf_t  opf;

                sal_memset(&opf, 0, sizeof(opf));
                opf.pool_type  = g_gg_stacking_master[lchip]->opf_type;
                opf.pool_index = SYS_STK_OPF_MCAST_PROFILE;
                CTC_ERROR_GOTO(sys_goldengate_opf_free_offset(lchip, &opf, 1, p_mcast_profile->mcast_profile_id),
                                                                       ret, Error);
            }
        }

        break;

    case CTC_STK_MCAST_PROFILE_ADD:
        {
            uint32 bitmap[2] = {0};
            uint8 trunk_id = 0;

            if (NULL == p_mcast_db)
            {
                ret = CTC_E_ENTRY_NOT_EXIST;
                goto Error;
            }

            bitmap[0] = p_mcast_profile->trunk_bitmap[0] & (p_mcast_profile->trunk_bitmap[0] ^ p_mcast_db->trunk_bitmap[0]);
            bitmap[1] = p_mcast_profile->trunk_bitmap[1] & (p_mcast_profile->trunk_bitmap[1] ^ p_mcast_db->trunk_bitmap[1]);
            for (trunk_id = 1; trunk_id <= 63; trunk_id++)
            {
                if (!CTC_IS_BIT_SET(bitmap[trunk_id / 32], trunk_id % 32))
                {
                    continue;
                }

                ret =   sys_goldengate_stacking_mcast_group_add_member(lchip, p_mcast_db, trunk_id, 1);

                if (CTC_E_NONE != ret) /*roolback*/
                {
                    uint8 tmp_trunk_id = 0;

                    for (tmp_trunk_id = 1; tmp_trunk_id < trunk_id; tmp_trunk_id++)
                    {
                        if (!CTC_IS_BIT_SET(bitmap[tmp_trunk_id / 32], tmp_trunk_id % 32))
                        {
                            continue;
                        }
                        sys_goldengate_stacking_mcast_group_remove_member(lchip, p_mcast_db, tmp_trunk_id, 1);
                    }

                    goto Error;
                }

            }
        }

        break;

    case CTC_STK_MCAST_PROFILE_REMOVE:
        {
            uint32 bitmap[2] = {0};
            uint8 trunk_id = 0;

            if (NULL == p_mcast_db)
            {
                ret = CTC_E_ENTRY_NOT_EXIST;
                goto Error;
            }

            bitmap[0] = p_mcast_profile->trunk_bitmap[0] & p_mcast_db->trunk_bitmap[0];
            bitmap[1] = p_mcast_profile->trunk_bitmap[1] & p_mcast_db->trunk_bitmap[1];

            for (trunk_id = 1; trunk_id <= 63; trunk_id++)
            {
                if (!CTC_IS_BIT_SET(bitmap[trunk_id / 32], trunk_id % 32))
                {
                    continue;
                }
                ret = sys_goldengate_stacking_mcast_group_remove_member(lchip, p_mcast_db, trunk_id, 1);

                if (CTC_E_NONE != ret)
                {
                    goto Error;
                }
            }
        }

        break;
    }

    STACKING_UNLOCK;
    return CTC_E_NONE;

    Error:
    STACKING_UNLOCK;
    return ret;
}


int32
sys_goldengate_stacking_get_trunk_mcast_profile(uint8 lchip, ctc_stacking_trunk_mcast_profile_t* p_mcast_profile)
{
    sys_stacking_mcast_db_t mcast_db;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;
    uint16 trunk_id_hw             = 0;
    uint32 cmd                     = 0;
    uint32 cur_met_offset          = 0;
    uint32 next_met_offset         = 0;
    DsMetEntry3W_m dsmet3w;

    SYS_STK_INIT_CHECK(lchip);
    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "profile id: %d \n", p_mcast_profile->mcast_profile_id);


    if (p_mcast_profile->mcast_profile_id >= 256)
    {
        return CTC_E_INVALID_PARAM;
    }


    /*lookup*/
    sal_memset(&mcast_db, 0, sizeof(mcast_db));

    mcast_db.type = 0;
    mcast_db.id   = p_mcast_profile->mcast_profile_id;

    STACKING_LOCK;
    p_mcast_db = ctc_hash_lookup(g_gg_stacking_master[lchip]->mcast_hash, &mcast_db);
    STACKING_UNLOCK;

    if (NULL == p_mcast_db)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    cur_met_offset  = p_mcast_db->head_met_offset;
    next_met_offset = cur_met_offset;


    do
    {
        cur_met_offset = next_met_offset;

        cmd = DRV_IOR(DsMetEntry3W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN( DRV_IOCTL(lchip, cur_met_offset, cmd, &dsmet3w));

        trunk_id_hw = GetDsMetEntry3W(V, u1_g2_ucastId_f,  &dsmet3w);

        next_met_offset = GetDsMetEntry3W(V, nextMetEntryPtr_f,  &dsmet3w);

        if (SYS_NH_INVALID_OFFSET != next_met_offset)
        {
            next_met_offset += sys_goldengate_nh_get_dsmet_base(lchip);
        }
        SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "cur:%d, trunk_id:%d\n ",  cur_met_offset, trunk_id_hw);

    }
    while (next_met_offset != SYS_NH_INVALID_OFFSET);


    p_mcast_profile->trunk_bitmap[0] = p_mcast_db->trunk_bitmap[0];
    p_mcast_profile->trunk_bitmap[1] = p_mcast_db->trunk_bitmap[1];

    return CTC_E_NONE;
}


int32
sys_goldengate_stacking_create_keeplive_group(uint8 lchip, uint16 group_id)
{
    int32 ret = CTC_E_NONE;
    uint32 group_size = 0;
    sys_stacking_mcast_db_t mcast_db;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;


    SYS_STK_INIT_CHECK(lchip);

    SYS_STK_DBG_PARAM("create group:0x%x\n", group_id);

    CTC_ERROR_RETURN(sys_goldengate_ftm_get_dyn_entry_size(lchip, SYS_FTM_DYN_ENTRY_GLB_MET, &group_size));

    if (group_id >= group_size/2)
    {
        return CTC_E_INVALID_PARAM;
    }

    STACKING_LOCK;

    /*lookup*/
    sal_memset(&mcast_db, 0, sizeof(mcast_db));

    mcast_db.type = 1;
    mcast_db.id   = group_id;
    p_mcast_db = ctc_hash_lookup(g_gg_stacking_master[lchip]->mcast_hash, &mcast_db);

    if (NULL != p_mcast_db)
    {
        ret =  CTC_E_ENTRY_EXIST;
        goto Error;
    }

    CTC_ERROR_GOTO(sys_goldengate_stacking_mcast_group_create( lchip,  1, group_id, &p_mcast_db, 0),
                   ret, Error);

    STACKING_UNLOCK;
    return CTC_E_NONE;

    Error:
    STACKING_UNLOCK;
    return ret;

}


int32
sys_goldengate_stacking_destroy_keeplive_group(uint8 lchip, uint16 group_id)
{
    int32 ret = CTC_E_NONE;
    uint32 group_size = 0;
    sys_stacking_mcast_db_t mcast_db;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;

    SYS_STK_INIT_CHECK(lchip);

    SYS_STK_DBG_PARAM("destroy group:0x%x\n", group_id);

    CTC_ERROR_RETURN(sys_goldengate_ftm_get_dyn_entry_size(lchip, SYS_FTM_DYN_ENTRY_GLB_MET, &group_size));

    if (group_id >= group_size/2)
    {
        return CTC_E_INVALID_PARAM;
    }


    SYS_STK_INIT_CHECK(lchip);

    STACKING_LOCK;

    /*lookup*/
    sal_memset(&mcast_db, 0, sizeof(mcast_db));

    mcast_db.type = 1;
    mcast_db.id   = group_id;
    p_mcast_db = ctc_hash_lookup(g_gg_stacking_master[lchip]->mcast_hash, &mcast_db);

    if (NULL == p_mcast_db)
    {
        ret =  CTC_E_ENTRY_NOT_EXIST;
        goto Error;
    }

    CTC_ERROR_GOTO(sys_goldengate_stacking_mcast_group_destroy( lchip,  1, group_id, p_mcast_db),
                   ret, Error);

    STACKING_UNLOCK;
    return CTC_E_NONE;

    Error:
    STACKING_UNLOCK;
    return ret;


}


int32
sys_goldengate_stacking_keeplive_add_member(uint8 lchip, ctc_stacking_keeplive_t* p_keeplive)
{
    int32 ret = CTC_E_NONE;
    sys_stacking_mcast_db_t mcast_db;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_keeplive);


    SYS_STK_DBG_PARAM("trunk:0x%x, gport:0x%x, type:%d, group:0x%x\n",
                      p_keeplive->trunk_id, p_keeplive->cpu_gport, p_keeplive->mem_type, p_keeplive->group_id);


    SYS_STK_INIT_CHECK(lchip);

    if (p_keeplive->mem_type == CTC_STK_KEEPLIVE_MEMBER_TRUNK)
    {
        SYS_STK_TRUNKID_RANGE_CHECK(p_keeplive->trunk_id);
    }

    STACKING_LOCK;

    /*lookup*/
    sal_memset(&mcast_db, 0, sizeof(mcast_db));

    mcast_db.type = 1;
    mcast_db.id   = p_keeplive->group_id;
    p_mcast_db = ctc_hash_lookup(g_gg_stacking_master[lchip]->mcast_hash, &mcast_db);

    if (NULL == p_mcast_db)
    {
        ret =  CTC_E_ENTRY_NOT_EXIST;
        goto Error;
    }


    if (p_keeplive->mem_type == CTC_STK_KEEPLIVE_MEMBER_TRUNK)
    {
        uint16 trunk_id = 0;

        trunk_id = p_keeplive->trunk_id;

        if (CTC_IS_BIT_SET(p_mcast_db->trunk_bitmap[trunk_id / 32], trunk_id % 32))
        {
            ret = CTC_E_MEMBER_EXIST;
            goto Error;
        }

        CTC_ERROR_GOTO(sys_goldengate_stacking_mcast_group_add_member(lchip, p_mcast_db, trunk_id, 1),
                       ret, Error);

    }
    else if (!CTC_IS_CPU_PORT(p_keeplive->cpu_gport))
    {
        uint16 dest_id = 0;
        if (FALSE == sys_goldengate_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(p_keeplive->cpu_gport)))
        {
            ret = CTC_E_INVALID_PORT;
            goto Error;
        }
        dest_id = CTC_MAP_GPORT_TO_LPORT(p_keeplive->cpu_gport);
        CTC_ERROR_GOTO(sys_goldengate_stacking_mcast_group_add_member(lchip, p_mcast_db, dest_id, 0),
                       ret, Error);
    }
    else
    {
        uint16 dest_id = 0;
        uint32 dsnh_offset = 0;
        uint32 cmd = 0;
        DsMetEntry3W_m dsmet3w;

        dest_id |= SYS_QSEL_TYPE_C2C << 9;
        dsnh_offset = CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_C2C_PKT, 0);

        cmd = DRV_IOR(DsMetEntry3W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO( DRV_IOCTL(lchip, p_mcast_db->head_met_offset, cmd, &dsmet3w),
                       ret, Error);

        SetDsMetEntry3W(V, u1_g2_ucastId_f,  &dsmet3w, dest_id);
        SetDsMetEntry3W(V, u1_g2_replicationCtl_f,  &dsmet3w, (dsnh_offset << 5));

        cmd = DRV_IOW(DsMetEntry3W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO( DRV_IOCTL(lchip, p_mcast_db->head_met_offset, cmd, &dsmet3w),
                       ret, Error);

    }

Error:
    STACKING_UNLOCK;
    return ret;
}


int32
sys_goldengate_stacking_keeplive_remove_member(uint8 lchip, ctc_stacking_keeplive_t* p_keeplive)
{
    int32 ret = CTC_E_NONE;
    sys_stacking_mcast_db_t mcast_db;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_keeplive);


    SYS_STK_DBG_PARAM("trunk:0x%x, gport:0x%x, type:%d, group:0x%x\n",
                      p_keeplive->trunk_id, p_keeplive->cpu_gport, p_keeplive->mem_type, p_keeplive->group_id);


    SYS_STK_INIT_CHECK(lchip);

    if (p_keeplive->mem_type == CTC_STK_KEEPLIVE_MEMBER_TRUNK)
    {
        SYS_STK_TRUNKID_RANGE_CHECK(p_keeplive->trunk_id);
    }

    STACKING_LOCK;

    /*lookup*/
    sal_memset(&mcast_db, 0, sizeof(mcast_db));

    mcast_db.type = 1;
    mcast_db.id   = p_keeplive->group_id;
    p_mcast_db = ctc_hash_lookup(g_gg_stacking_master[lchip]->mcast_hash, &mcast_db);

    if (NULL == p_mcast_db)
    {
        ret = CTC_E_ENTRY_NOT_EXIST;
        goto Error;
    }


    if (p_keeplive->mem_type == CTC_STK_KEEPLIVE_MEMBER_TRUNK)
    {
        uint16 trunk_id = 0;

        trunk_id = p_keeplive->trunk_id;
        SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

        if (!CTC_IS_BIT_SET(p_mcast_db->trunk_bitmap[trunk_id / 32], trunk_id % 32))
        {
            ret =  CTC_E_MEMBER_NOT_EXIST;
            goto Error;
        }

        CTC_ERROR_GOTO(sys_goldengate_stacking_mcast_group_remove_member(lchip, p_mcast_db, p_keeplive->trunk_id, 1),
                         ret, Error);
    }
    else if (!CTC_IS_CPU_PORT(p_keeplive->cpu_gport))
    {
        uint16 dest_id = 0;
        if (FALSE == sys_goldengate_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(p_keeplive->cpu_gport)))
        {
            ret = CTC_E_INVALID_PORT;
            goto Error;
        }
        dest_id = CTC_MAP_GPORT_TO_LPORT(p_keeplive->cpu_gport);
        CTC_ERROR_GOTO(sys_goldengate_stacking_mcast_group_remove_member(lchip, p_mcast_db, dest_id, 0),
                       ret, Error);
    }
    else
    {
        uint16 dest_id                 = 0;
        uint32 dsnh_offset             = 0;
        uint32 cmd                     = 0;
        DsMetEntry3W_m dsmet3w;

        dest_id  = SYS_RSV_PORT_DROP_ID;
        dsnh_offset = CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_C2C_PKT, 0);

        cmd = DRV_IOR(DsMetEntry3W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO( DRV_IOCTL(lchip, p_mcast_db->head_met_offset, cmd, &dsmet3w),
                         ret, Error);

        SetDsMetEntry3W(V, u1_g2_ucastId_f,  &dsmet3w, dest_id);
        SetDsMetEntry3W(V, u1_g2_replicationCtl_f,  &dsmet3w, (dsnh_offset << 5));

        cmd = DRV_IOW(DsMetEntry3W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO( DRV_IOCTL(lchip, p_mcast_db->head_met_offset, cmd, &dsmet3w),
                         ret, Error);

    }

Error:
    STACKING_UNLOCK;
    return ret;
}

int32
sys_goldengate_stacking_keeplive_get_members(uint8 lchip, ctc_stacking_keeplive_t* p_keeplive)
{
    int32 ret = CTC_E_NONE;
    sys_stacking_mcast_db_t mcast_db;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_keeplive);

    SYS_STK_DBG_PARAM("group:0x%x mem_type:%d\n", p_keeplive->group_id, p_keeplive->mem_type);

    STACKING_LOCK;
    /*lookup*/
    sal_memset(&mcast_db, 0, sizeof(mcast_db));
    mcast_db.type = 1;
    mcast_db.id   = p_keeplive->group_id;
    p_mcast_db = ctc_hash_lookup(g_gg_stacking_master[lchip]->mcast_hash, &mcast_db);
    if (NULL == p_mcast_db)

{
        ret = CTC_E_ENTRY_NOT_EXIST;
        goto EXIT;
    }

    if (p_keeplive->mem_type == CTC_STK_KEEPLIVE_MEMBER_TRUNK)
    {
        sal_memcpy(p_keeplive->trunk_bitmap, p_mcast_db->trunk_bitmap, sizeof(p_keeplive->trunk_bitmap));
        p_keeplive->trunk_bmp_en = 1;
    }
    else
    {
        uint32 cmd = 0;
        uint16 dest_id                 = 0;
        uint32 dsnh_offset             = 0;
        DsMetEntry3W_m dsmet3w;
        uint8 gchip = 0;

        sal_memset(&dsmet3w, 0, sizeof(dsmet3w));
        cmd = DRV_IOR(DsMetEntry3W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, p_mcast_db->head_met_offset, cmd, &dsmet3w),
                         ret, EXIT);
        dest_id = GetDsMetEntry3W(V, u1_g2_ucastId_f, &dsmet3w);
        dsnh_offset = GetDsMetEntry3W(V, u1_g2_replicationCtl_f, &dsmet3w);

        if ((dest_id != SYS_RSV_PORT_DROP_ID)
            && ((dsnh_offset >> 5) == CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_C2C_PKT, 0)))
        {
            sys_goldengate_get_gchip_id(lchip, &gchip);
            p_keeplive->cpu_gport = CTC_LPORT_CPU | ((gchip & CTC_GCHIP_MASK) << CTC_LOCAL_PORT_LENGTH);
        }
    }

EXIT:
    STACKING_UNLOCK;
    return ret;
}


int32
sys_goldengate_stacking_get_mcast_profile_met_offset(uint8 lchip,  uint16 mcast_profile_id, uint32 *p_stacking_met_offset)
{
    sys_stacking_mcast_db_t mcast_db;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;

    CTC_PTR_VALID_CHECK(p_stacking_met_offset);

    if (NULL == g_gg_stacking_master[lchip])
    {
        *p_stacking_met_offset = 0;
        return CTC_E_NONE;
    }

    if (0 == mcast_profile_id)
    {
        *p_stacking_met_offset = g_gg_stacking_master[lchip]->bind_mcast_en?
                                                                                 g_gg_stacking_master[lchip]->stacking_mcast_offset : SYS_NH_INVALID_OFFSET;
        return CTC_E_NONE;
    }


    STACKING_LOCK;

    sal_memset(&mcast_db, 0, sizeof(mcast_db));

    mcast_db.type = 0;
    mcast_db.id   = mcast_profile_id;
    p_mcast_db = ctc_hash_lookup(g_gg_stacking_master[lchip]->mcast_hash, &mcast_db);


    if (NULL == p_mcast_db)
    {
        STACKING_UNLOCK;
        return CTC_E_ENTRY_NOT_EXIST;
    }

    *p_stacking_met_offset = p_mcast_db->head_met_offset;

    STACKING_UNLOCK;
    return CTC_E_NONE;
}



#define _____STK_EN_____ ""


STATIC int32
_sys_goldengate_stacking_set_enable(uint8 lchip, uint8 enable, uint8 mode)
{
    CTC_ERROR_RETURN(_sys_goldengate_stacking_register_en(lchip, enable, mode));
    return CTC_E_NONE;
}

int32
sys_goldengate_stacking_get_enable(uint8 lchip, bool* enable, uint32* stacking_mcast_offset)
{
    CTC_PTR_VALID_CHECK(enable);
    CTC_PTR_VALID_CHECK(stacking_mcast_offset);

    if (NULL == g_gg_stacking_master[lchip])
    {
        *enable = FALSE;
        return CTC_E_NONE;
    }

    *enable = (g_gg_stacking_master[lchip]->stacking_en && g_gg_stacking_master[lchip]->bind_mcast_en)? TRUE : FALSE;
    *stacking_mcast_offset = g_gg_stacking_master[lchip]->stacking_mcast_offset;

    return CTC_E_NONE;
}


int32
sys_goldengate_stacking_get_stkhdr_len(uint8 lchip, uint16 gport, uint8 gchip, uint16* p_stkhdr_len)
{

    uint32 cmd = 0;
    uint32 field_val = 0;
    uint8 vlan_valid = 0;
    uint8 header_l3_offset = 0;
    uint8 packet_header_len = 0;
    uint8 mux_type = 0;
    uint8 udp_en = 0;
    uint8 chan_id = 0;
    uint8 rgchip  = 0;
    uint8 trunk_id  = 0;
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;
    uint16 local_cnt = 0;
    uint32 truck_port = 0;

    CTC_PTR_VALID_CHECK(p_stkhdr_len);

    if (NULL == g_gg_stacking_master[lchip])
    {
        *p_stkhdr_len = 0;
        return CTC_E_NONE;
    }

    rgchip = gchip;

    trunk_id = g_gg_stacking_master[lchip]->neigbor_chip[rgchip];
    p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        if((CTC_IS_LINKAGG_PORT(gport)))
        {
            if (CTC_E_NONE != sys_goldengate_linkagg_get_1st_local_port(lchip, CTC_GPORT_LINKAGG_ID(gport), &truck_port, &local_cnt))
            {
                *p_stkhdr_len = 0;
                return DRV_E_NONE;
            }
        }
        else
        {
            truck_port = gport;
        }
    }
    else
    {
        truck_port = p_sys_trunk->mem_ports[0];
    }

    chan_id = SYS_GET_CHANNEL_ID(lchip, (uint16)truck_port);
    if (0xFF == chan_id)
    {
        *p_stkhdr_len = 0;
        return DRV_E_NONE;
    }

    /*mux type*/
    cmd = DRV_IOR(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_muxType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
    mux_type = field_val;

    cmd = DRV_IOR(DsPacketHeaderEditTunnel_t, DsPacketHeaderEditTunnel_vlanIdValid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
    vlan_valid = field_val;

    cmd = DRV_IOR(IpeHeaderAdjustCtl_t, IpeHeaderAdjustCtl_headerUdpEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    udp_en = field_val;

    /* PACKET_HEADER_DECISION */
    if (SYS_STK_MUX_TYPE_HDR_WITHOUT_TUNNEL == mux_type ||
        SYS_STK_MUX_TYPE_HDR_WITH_L2 == mux_type ||
        SYS_STK_MUX_TYPE_HDR_WITH_L2_AND_IPV4 == mux_type ||
        SYS_STK_MUX_TYPE_HDR_WITH_L2_AND_IPV6 == mux_type ||
        SYS_STK_MUX_TYPE_HDR_WITH_IPV4 == mux_type ||
        SYS_STK_MUX_TYPE_HDR_WITH_IPV6 == mux_type)
    { /* With PacketHeader */
        if (SYS_STK_MUX_TYPE_HDR_WITHOUT_TUNNEL == mux_type)
        { /* without L2/L3 tunnel header */
            packet_header_len = 32;
        }
        else
        { /* with L2/L3 tunnel header */
            if (SYS_STK_MUX_TYPE_HDR_WITH_L2 == mux_type ||
                SYS_STK_MUX_TYPE_HDR_WITH_L2_AND_IPV4 == mux_type ||
                SYS_STK_MUX_TYPE_HDR_WITH_L2_AND_IPV6 == mux_type)
            { /* with L2 Header */
                header_l3_offset = 14 + (vlan_valid << 2);
            }
            else
            { /* without L2 Header */
                header_l3_offset = 0;
            }

            packet_header_len += header_l3_offset;

            if (SYS_STK_MUX_TYPE_HDR_WITH_L2 == mux_type)  /* only L2 Header */
            {
                packet_header_len += 32;
            }
            else
            {
                if ((SYS_STK_MUX_TYPE_HDR_WITH_L2_AND_IPV4 == mux_type) ||
                    (SYS_STK_MUX_TYPE_HDR_WITH_IPV4 == mux_type))  /* IPv4 */
                {
                    packet_header_len += (udp_en) ? 60 : 52;

                }
                else if ((SYS_STK_MUX_TYPE_HDR_WITH_L2_AND_IPV6 == mux_type) ||
                         (SYS_STK_MUX_TYPE_HDR_WITH_IPV6 == mux_type)) /* IPv6 */
                {
                    packet_header_len += (udp_en) ? 80 : 72;
                }
            }
        }
    }

    *p_stkhdr_len = packet_header_len;

    return CTC_E_NONE;
}

int32
sys_goldengate_stacking_show_trunk_info(uint8 lchip, uint8 trunk_id)
{
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;
    uint8 idx = 0;
    uint8 cnt = 0;
    char* mode[3] = {"Static", "Dynamic", "Failover"};

    SYS_STK_INIT_CHECK(lchip);
    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }

    cnt = p_sys_trunk->mem_cnt;

    SYS_STK_DBG_DUMP("stacking trunk info below:\n");

    SYS_STK_DBG_DUMP("--------------------------------\n");
    SYS_STK_DBG_DUMP("trunk member ports\n");
    SYS_STK_DBG_DUMP("--------------------------------\n");

    SYS_STK_DBG_DUMP("trunk id:%d, mode:%s, count:%d\n", trunk_id, mode[p_sys_trunk->mode],cnt);
    SYS_STK_DBG_DUMP("max mem cnt: %d\n", p_sys_trunk->max_mem_cnt);
    SYS_STK_DBG_DUMP("member base: %d\n", p_sys_trunk->mem_base);


    for (idx = 0; idx < cnt; idx++)
    {
        SYS_STK_DBG_DUMP("Member port:0x%x\n", p_sys_trunk->mem_ports[idx]);
    }

    SYS_STK_DBG_DUMP("--------------------------------\n");
    SYS_STK_DBG_DUMP("trunk remote chip bitmap\n");
    SYS_STK_DBG_DUMP("--------------------------------\n");
    SYS_STK_DBG_DUMP("bitmap:0x%x\n", p_sys_trunk->rgchip_bitmap);

    return CTC_E_NONE;
}

int32
sys_goldengate_stacking_failover_detect(uint8 lchip, uint8 linkdown_chan)
{
    uint32 cmd = 0;
    uint8 trunk_id   = 0;
    DsLinkAggregateChannel_m linkagg_channel;
    uint16 lport = 0;
    uint16 gport = 0;
    uint8 gchip = 0;
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;
    uint16 last_gport = 0;
    uint32 mem_index = 0;

    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "chan_down:0x%x\n ", linkdown_chan);

    lport = SYS_GET_LPORT_ID_WITH_CHAN(lchip, linkdown_chan);
    SYS_MAX_PHY_PORT_CHECK(lport);

    sys_goldengate_get_gchip_id(lchip, &gchip);
    gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, lport);

    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, linkdown_chan, cmd, &linkagg_channel));
    trunk_id = GetDsLinkAggregateChannel(V, u1_g2_linkAggregationChannelGroup_f, &linkagg_channel);

    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    STACKING_LOCK;

    p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }

    if (FALSE == _sys_goldengate_stacking_find_trunk_port(p_sys_trunk, gport, &mem_index))
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_MEMBER_PORT_NOT_EXIST;
    }

    last_gport = p_sys_trunk->mem_ports[p_sys_trunk->mem_cnt - 1];
    p_sys_trunk->mem_ports[mem_index] = last_gport;

    p_sys_trunk->mem_cnt--;

    STACKING_UNLOCK;

    return CTC_E_NONE;
}



#define SYS_API_START

int32
sys_goldengate_stacking_mcast_mode(uint8 lchip, uint8 mcast_mode)
{
    SYS_STK_INIT_CHECK(lchip);

    if (g_gg_stacking_master[lchip]->trunk_num)
    {
        return CTC_E_NOT_READY;
    }
    g_gg_stacking_master[lchip]->mcast_mode = mcast_mode;
    return CTC_E_NONE;
}


int32
sys_goldengate_stacking_port_enable(uint8 lchip, ctc_stacking_trunk_t* p_trunk, uint8 enable)
{
    uint32 cmd       = 0;
    uint16 gport     = 0;
    uint16 lport      = 0;
    uint16 chan_id    = 0;
    uint8 lchan_id    = 0;
    uint32 field_val = 0;
    uint8 slice_id    = 0;
    EpeHdrAdjustChanCtl_m    epe_hdr_adjust_chan_ctl;
    BufRetrvChanStackingEn_m stacking_en;
    uint32 packet_header_en[2] = {0};

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trunk);
    gport    = p_trunk->gport;
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    SYS_STK_LPORT_CHECK(lport);

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == chan_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }
    lchan_id = (chan_id&0x3F);


    /*1. set not edit packet on stacking port*/
    field_val = enable;
    CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_VLAN_OPERATION_DIS, field_val));
    field_val = !enable;
    CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_UNTAG_DEF_VLAN_EN, field_val));

    /*2. Ecap Output packet Header enable*/
    if (enable)
    {
        CTC_ERROR_RETURN(_sys_goldengate_stacking_encap_header_enable(lchip, gport, &(p_trunk->encap_hdr)));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_stacking_encap_header_disable(lchip, gport));
    }

    /*3. config EpeHdrAdjustChanCtl*/
    slice_id = SYS_MAP_CHANID_TO_SLICE(chan_id);
    sal_memset(&epe_hdr_adjust_chan_ctl, 0, sizeof(epe_hdr_adjust_chan_ctl));
    sal_memset(packet_header_en, 0, sizeof(packet_header_en));
    cmd = DRV_IOR(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &epe_hdr_adjust_chan_ctl));
    GetEpeHdrAdjustChanCtl(A, packetHeaderEn_f, &epe_hdr_adjust_chan_ctl, packet_header_en);
    if (enable)
    {
        CTC_BIT_SET(packet_header_en[lchan_id >> 5], lchan_id&0x1F);
    }
    else
    {
        CTC_BIT_UNSET(packet_header_en[lchan_id>>5], lchan_id&0x1F);
    }
    SetEpeHdrAdjustChanCtl(A, packetHeaderEn_f, &epe_hdr_adjust_chan_ctl, packet_header_en);
    cmd = DRV_IOW(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &epe_hdr_adjust_chan_ctl));

    /*4. config BufRetrvChanStackingEn*/
    sal_memset(&stacking_en, 0, sizeof(stacking_en));
    sal_memset(packet_header_en, 0, sizeof(packet_header_en));
    cmd = DRV_IOR(BufRetrvChanStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &stacking_en));
    GetBufRetrvChanStackingEn(A, chanStackingEn_f, &stacking_en, packet_header_en);
    if(enable)
    {
        CTC_BIT_SET(packet_header_en[lchan_id >> 5], lchan_id&0x1F);
    }
    else
    {
        CTC_BIT_UNSET(packet_header_en[lchan_id >> 5], lchan_id&0x1F);
    }
    SetBufRetrvChanStackingEn(A, chanStackingEn_f, &stacking_en, packet_header_en);
    cmd = DRV_IOW(BufRetrvChanStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &stacking_en));

    return CTC_E_NONE;

}

int32
sys_goldengate_stacking_update_load_mode(uint8 lchip, uint8 trunk_id, ctc_stacking_load_mode_t load_mode)
{
    uint8 mem_index = 0;
    uint16 chan_id = 0;
    uint32 cmd = 0;
    uint32 index = 0;
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;
    DsLinkAggregateChannelGroup_m  ds_link_aggregate_sgmac_group;
    DsLinkAggregateChannelMember_m ds_link_aggregate_sgmac_member;
    DsLinkAggregateChannel_m linkagg_channel;

    SYS_STK_INIT_CHECK(lchip);

    if ((CTC_STK_LOAD_DYNAMIC == load_mode) &&
        (0 == g_gg_stacking_master[lchip]->trunk_mode))
    {
        return CTC_E_INVALID_CONFIG;
    }

    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    if (load_mode >= CTC_STK_LOAD_MODE_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }


    STACKING_LOCK;

    p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }

    if (p_sys_trunk->mode == load_mode)
    {
        STACKING_UNLOCK;
        return CTC_E_NONE;
    }

    if (p_sys_trunk->max_mem_cnt != 32)
    {
        STACKING_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&ds_link_aggregate_sgmac_group, 0, sizeof(DsLinkAggregateChannelGroup_m));
    cmd = DRV_IOR(DsLinkAggregateChannelGroup_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, trunk_id, cmd, &ds_link_aggregate_sgmac_group),g_gg_stacking_master[lchip]->p_stacking_mutex);

    if (CTC_STK_LOAD_DYNAMIC == load_mode)
    {
        /* static/failover update to dynamic */

        /* 1.update member to dynamic */
        for (mem_index = 0; mem_index < p_sys_trunk->mem_cnt; mem_index++)
        {
            chan_id = SYS_GET_CHANNEL_ID(lchip, p_sys_trunk->mem_ports[mem_index]);

            if (0xFF == chan_id)
            {
                STACKING_UNLOCK;
                return CTC_E_INVALID_LOCAL_PORT;
            }

            CTC_ERROR_RETURN_WITH_UNLOCK(_sys_goldengate_stacking_dlb_add_member(lchip, trunk_id, mem_index, chan_id),g_gg_stacking_master[lchip]->p_stacking_mutex);
        }

        CTC_ERROR_RETURN_WITH_UNLOCK(_sys_goldengate_stacking_clear_flow_active(lchip, trunk_id),g_gg_stacking_master[lchip]->p_stacking_mutex);


        /* 2.update group to dynamic */
        SetDsLinkAggregateChannelGroup(V, channelLinkAggRemapEn_f, &ds_link_aggregate_sgmac_group, 1);
        SetDsLinkAggregateChannelGroup(V, channelLinkAggMemberPtr_f, &ds_link_aggregate_sgmac_group, trunk_id);
        SetDsLinkAggregateChannelGroup(V, channelLinkAggFlowNum_f, &ds_link_aggregate_sgmac_group, 3);

        cmd = DRV_IOW(DsLinkAggregateChannelGroup_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, trunk_id, cmd, &ds_link_aggregate_sgmac_group),g_gg_stacking_master[lchip]->p_stacking_mutex);

    }
    else
    {
        /* failover/dynamic update to static; static/dynamic update to failover */
        if (p_sys_trunk->mode == CTC_STK_LOAD_DYNAMIC)
        {
            /* 1.update group to static */
            SetDsLinkAggregateChannelGroup(V, channelLinkAggRemapEn_f, &ds_link_aggregate_sgmac_group, 0);
            SetDsLinkAggregateChannelGroup(V, channelLinkAggMemberPtr_f, &ds_link_aggregate_sgmac_group, 0);
            SetDsLinkAggregateChannelGroup(V, channelLinkAggFlowNum_f, &ds_link_aggregate_sgmac_group, 0);

            cmd = DRV_IOW(DsLinkAggregateChannelGroup_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, trunk_id, cmd, &ds_link_aggregate_sgmac_group),g_gg_stacking_master[lchip]->p_stacking_mutex);
        }
        /* 2.update member to static */
        for (mem_index = 0; mem_index < p_sys_trunk->mem_cnt; mem_index++)
        {
            chan_id = SYS_GET_CHANNEL_ID(lchip, p_sys_trunk->mem_ports[mem_index]);

            if (0xFF == chan_id)
            {
                STACKING_UNLOCK;
                return CTC_E_INVALID_LOCAL_PORT;
            }

            if (p_sys_trunk->mode == CTC_STK_LOAD_DYNAMIC)
            {
                sal_memset(&ds_link_aggregate_sgmac_member, 0, sizeof(ds_link_aggregate_sgmac_member));
                cmd = DRV_IOW(DsLinkAggregateChannelMember_t, DRV_ENTRY_FLAG);
                SetDsLinkAggregateChannelMember(V, channelId_f, &ds_link_aggregate_sgmac_member, chan_id);

                index = p_sys_trunk->mem_base + mem_index;
                CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, index, cmd, &ds_link_aggregate_sgmac_member), g_gg_stacking_master[lchip]->p_stacking_mutex);
            }

            if (CTC_STK_LOAD_STATIC == load_mode)  /* failover/dynamic update to static */
            {
                CTC_ERROR_RETURN_WITH_UNLOCK(_sys_goldengate_stacking_dlb_del_member_channel(lchip, trunk_id, chan_id), g_gg_stacking_master[lchip]->p_stacking_mutex);
            }
            else if (CTC_STK_LOAD_STATIC_FAILOVER == load_mode) /* static/dynamic update to failover */
            {
                sal_memset(&linkagg_channel, 0, sizeof(linkagg_channel));
                cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, chan_id, cmd, &linkagg_channel), g_gg_stacking_master[lchip]->p_stacking_mutex);
                SetDsLinkAggregateChannel(V, groupType_f, &linkagg_channel, 1);
                SetDsLinkAggregateChannel(V, groupEn_f, &linkagg_channel, 1);
                SetDsLinkAggregateChannel(V, u1_g2_linkAggregationChannelGroup_f, &linkagg_channel, trunk_id);

                cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, chan_id, cmd, &linkagg_channel), g_gg_stacking_master[lchip]->p_stacking_mutex);
            }
        }
    }


    p_sys_trunk->mode = load_mode;

    STACKING_UNLOCK;

    return CTC_E_NONE;
}
#define SYS_API_END

int32
_sys_goldengate_stacking_wb_traverse_sync_trunk_node(void *data,uint32 vec_index, void *user_data)
{
    int32 ret = 0;
    sys_stacking_trunk_info_t *p_sys_trunk = (sys_stacking_trunk_info_t*)data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t*)user_data;
    sys_wb_stacking_trunk_t   *p_wb_trunk_node = NULL;
    uint16 max_buffer_cnt = wb_data->buffer_len / (wb_data->key_len + wb_data->data_len);

    p_wb_trunk_node = (sys_wb_stacking_trunk_t *)wb_data->buffer + wb_data->valid_cnt;

    p_wb_trunk_node->trunk_id = vec_index;
    p_wb_trunk_node->mode = p_sys_trunk->mode;
    p_wb_trunk_node->max_mem_cnt = p_sys_trunk->max_mem_cnt;
    p_wb_trunk_node->replace_en = p_sys_trunk->replace_en;
    p_wb_trunk_node->encap_hdr.hdr_flag = p_sys_trunk->encap_hdr.hdr_flag;
    p_wb_trunk_node->encap_hdr.vlan_valid = p_sys_trunk->encap_hdr.vlan_valid;
    p_wb_trunk_node->encap_hdr.vlan_id = p_sys_trunk->encap_hdr.vlan_id;
    sal_memcpy(p_wb_trunk_node->encap_hdr.mac_da, p_sys_trunk->encap_hdr.mac_da, sizeof(mac_addr_t));
    sal_memcpy(p_wb_trunk_node->encap_hdr.mac_sa, p_sys_trunk->encap_hdr.mac_sa, sizeof(mac_addr_t));
    sal_memcpy(&p_wb_trunk_node->encap_hdr.ipda, &p_sys_trunk->encap_hdr.ipda, sizeof(ipv6_addr_t));

    if (++wb_data->valid_cnt ==  max_buffer_cnt)
    {
        ret = ctc_wb_add_entry(wb_data);
        if ( ret != CTC_E_NONE )
        {
           return ret;
        }
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_stacking_wb_mapping_mcast(uint8 lchip, sys_wb_stacking_mcast_db_t *p_wb_mcast_info, sys_stacking_mcast_db_t *p_mcast_info, uint8 sync)
{

    if (sync)
    {
        p_wb_mcast_info->type = p_mcast_info->type;
        p_wb_mcast_info->id = p_mcast_info->id;
        p_wb_mcast_info->head_met_offset = p_mcast_info->head_met_offset;
        p_wb_mcast_info->tail_met_offset = p_mcast_info->tail_met_offset;
        p_wb_mcast_info->append_en = p_mcast_info->append_en;
        p_wb_mcast_info->alloc_id = p_mcast_info->alloc_id;
        p_wb_mcast_info->last_tail_offset = p_mcast_info->last_tail_offset;
        sal_memcpy(p_wb_mcast_info->trunk_bitmap, p_mcast_info->trunk_bitmap, sizeof(uint32)*CTC_STK_TRUNK_BMP_NUM);
    }
    else
    {

        p_mcast_info->type = p_wb_mcast_info->type;
        p_mcast_info->id = p_wb_mcast_info->id;
        p_mcast_info->head_met_offset = p_wb_mcast_info->head_met_offset;
        p_mcast_info->tail_met_offset = p_wb_mcast_info->tail_met_offset;
        p_mcast_info->append_en = p_wb_mcast_info->append_en;
        p_mcast_info->alloc_id = p_wb_mcast_info->alloc_id;
        p_mcast_info->last_tail_offset = p_wb_mcast_info->last_tail_offset;
        sal_memcpy(p_mcast_info->trunk_bitmap, p_wb_mcast_info->trunk_bitmap, sizeof(uint32)*SYS_WB_STK_TRUNK_BMP_NUM);

        if (p_mcast_info->type == 0)
        {
            sys_goldengate_nh_offset_alloc_from_position(lchip, SYS_NH_ENTRY_TYPE_MET, 1, p_mcast_info->head_met_offset);
        }

    }

    return CTC_E_NONE;
}


int32
_sys_goldengate_ipuc_wb_sync_mcast_func(sys_stacking_mcast_db_t *p_mcast_info, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_wb_stacking_mcast_db_t  *p_wb_mcast_info;
    sys_traverse_t *data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)(data->data);
    uint8 lchip = (uint8)(data->value1);

    if ((2 > p_mcast_info->type) && (0 == p_mcast_info->id))

{
        return CTC_E_NONE;
    }

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_mcast_info = (sys_wb_stacking_mcast_db_t *)wb_data->buffer + wb_data->valid_cnt;

    CTC_ERROR_RETURN(_sys_goldengate_stacking_wb_mapping_mcast(lchip, p_wb_mcast_info, p_mcast_info, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_stacking_wb_sync(uint8 lchip)
{
    int32 ret                      = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_traverse_t user_data;
    sys_wb_stacking_master_t *p_wb_stacking_master;

    wb_data.buffer = (ctc_wb_key_data_t*)mem_malloc(MEM_STK_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " No memory \n");
        return CTC_E_NO_MEMORY;

    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);


    /*Sync master*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_stacking_master_t, CTC_FEATURE_STACKING, SYS_WB_APPID_STACKING_SUBID_MASTER);
    p_wb_stacking_master = (sys_wb_stacking_master_t  *)wb_data.buffer;

    p_wb_stacking_master->lchip = lchip;
    p_wb_stacking_master->ipv4_encap = g_gg_stacking_master[lchip]->ipv4_encap;
    p_wb_stacking_master->stacking_mcast_offset = g_gg_stacking_master[lchip]->stacking_mcast_offset;
    p_wb_stacking_master->binding_trunk = g_gg_stacking_master[lchip]->binding_trunk;
    p_wb_stacking_master->bind_mcast_en = g_gg_stacking_master[lchip]->bind_mcast_en;

    wb_data.valid_cnt = 1;
    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);


    /*Sync trunk*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_stacking_trunk_t, CTC_FEATURE_STACKING, SYS_WB_APPID_STACKING_SUBID_TRUNK);
        ctc_vector_traverse2(g_gg_stacking_master[lchip]->p_trunk_vec,0,_sys_goldengate_stacking_wb_traverse_sync_trunk_node,&wb_data);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    /*Sync mcast_hash*/
    CTC_WB_INIT_DATA_T((&wb_data), sys_wb_stacking_mcast_db_t, CTC_FEATURE_STACKING, SYS_WB_APPID_STACKING_SUBID_MCAST_DB);
    user_data.data = &wb_data;
    user_data.value1 = lchip;

    CTC_ERROR_GOTO(ctc_hash_traverse(g_gg_stacking_master[lchip]->mcast_hash, (hash_traversal_fn) _sys_goldengate_ipuc_wb_sync_mcast_func, (void *)&user_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }



done:
    if (wb_data.buffer )
    {
        mem_free(wb_data.buffer);
    }

    return ret;
}

int32 sys_goldengate_stacking_wb_restore(uint8 lchip)
{
    int32 ret = 0;
    uint32 entry_cnt = 0;
    uint32 cmd = 0;
    uint32 index = 0;
    uint16 gport     = 0;
    uint16 lport      = 0;
    uint32 chan_id    = 0;
    uint8 gchip = 0;
    uint32 field_val = 0;
    ctc_wb_query_t    wb_query;
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;
    sys_wb_stacking_master_t* p_wb_stacking_master = NULL;
    sys_wb_stacking_trunk_t* p_wb_stacking_trunk = NULL;
    sys_wb_stacking_mcast_db_t* p_wb_stacking_mcast = NULL;
    sys_stacking_mcast_db_t* p_sys_mcast = NULL;
    DsLinkAggregateChannelGroup_m  ds_link_aggregate_sgmac_group;
    sys_goldengate_opf_t  opf;

    wb_query.buffer = mem_malloc(MEM_STK_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " No memory \n");
        return CTC_E_NO_MEMORY;

    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);


    /*Restore Master*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_stacking_master_t, CTC_FEATURE_STACKING, SYS_WB_APPID_STACKING_SUBID_MASTER);
    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
    p_wb_stacking_master = (sys_wb_stacking_master_t*)wb_query.buffer + entry_cnt++;

    g_gg_stacking_master[lchip]->stacking_mcast_offset = p_wb_stacking_master->stacking_mcast_offset;
    g_gg_stacking_master[lchip]->ipv4_encap = p_wb_stacking_master->ipv4_encap;
    g_gg_stacking_master[lchip]->binding_trunk = p_wb_stacking_master->binding_trunk;
    g_gg_stacking_master[lchip]->bind_mcast_en = p_wb_stacking_master->bind_mcast_en;

    CTC_WB_QUERY_ENTRY_END((&wb_query));

    /*Restore trunk*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_stacking_trunk_t, CTC_FEATURE_STACKING, SYS_WB_APPID_STACKING_SUBID_TRUNK);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
    p_wb_stacking_trunk = (sys_wb_stacking_trunk_t*)wb_query.buffer + entry_cnt++;

    p_sys_trunk = (sys_stacking_trunk_info_t*)mem_malloc(MEM_STK_MODULE, sizeof(sys_stacking_trunk_info_t));
    if (NULL == p_sys_trunk)
    {
        ret =  CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(p_sys_trunk, 0, sizeof(sys_stacking_trunk_info_t));

    p_sys_trunk->trunk_id = p_wb_stacking_trunk->trunk_id;
    p_sys_trunk->mode = p_wb_stacking_trunk->mode;
    p_sys_trunk->max_mem_cnt = p_wb_stacking_trunk->max_mem_cnt;
    p_sys_trunk->replace_en = p_wb_stacking_trunk->replace_en;
    p_sys_trunk->encap_hdr.hdr_flag = p_wb_stacking_trunk->encap_hdr.hdr_flag;
    p_sys_trunk->encap_hdr.vlan_valid = p_wb_stacking_trunk->encap_hdr.vlan_valid;
    p_sys_trunk->encap_hdr.vlan_id = p_wb_stacking_trunk->encap_hdr.vlan_id;
    sal_memcpy(p_sys_trunk->encap_hdr.mac_da, p_wb_stacking_trunk->encap_hdr.mac_da, sizeof(mac_addr_t));
    sal_memcpy(p_sys_trunk->encap_hdr.mac_sa, p_wb_stacking_trunk->encap_hdr.mac_sa, sizeof(mac_addr_t));
    sal_memcpy(&p_sys_trunk->encap_hdr.ipda, &p_wb_stacking_trunk->encap_hdr.ipda, sizeof(ipv6_addr_t));

    sal_memset(&ds_link_aggregate_sgmac_group, 0, sizeof(ds_link_aggregate_sgmac_group));
    cmd = DRV_IOR(DsLinkAggregateChannelGroup_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_trunk->trunk_id, cmd, &ds_link_aggregate_sgmac_group));
    p_sys_trunk->mem_base = GetDsLinkAggregateChannelGroup(V, channelLinkAggMemBase_f, &ds_link_aggregate_sgmac_group);
    p_sys_trunk->mem_cnt = GetDsLinkAggregateChannelGroup(V, channelLinkAggMemNum_f, &ds_link_aggregate_sgmac_group);

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type  = g_gg_stacking_master[lchip]->opf_type;
    opf.pool_index = SYS_STK_OPF_MEM_BASE;
    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, p_sys_trunk->max_mem_cnt, p_sys_trunk->mem_base));

    for (index = 0; index < p_sys_trunk->mem_cnt; index++)
    {
        if (CTC_STK_LOAD_DYNAMIC == p_sys_trunk->mode)
        {
            cmd = DRV_IOR(DsLinkAggregateChannelMemberSet_t, DsLinkAggregateChannelMemberSet_array_0_destChannel_f + index);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_trunk->trunk_id, cmd, &chan_id));
        }
        else
        {
            cmd = DRV_IOR(DsLinkAggregateChannelMember_t, DsLinkAggregateChannelMember_channelId_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_trunk->mem_base + index, cmd, &chan_id));
        }

        lport = SYS_GET_LPORT_ID_WITH_CHAN(lchip, chan_id);
        SYS_MAX_PHY_PORT_CHECK(lport);
        sys_goldengate_get_gchip_id(lchip, &gchip);
        gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, lport);
        p_sys_trunk->mem_ports[index] = gport;
    }

    ctc_vector_add(g_gg_stacking_master[lchip]->p_trunk_vec,  p_sys_trunk->trunk_id, p_sys_trunk);
    g_gg_stacking_master[lchip]->trunk_num++;

    CTC_WB_QUERY_ENTRY_END((&wb_query));


    /*Restore  rgchip_bitmap neigbor_chip*/
    for (index = 0; index < CTC_MAX_CHIP_NUM; index++)
    {
        cmd = DRV_IOR(DsSgmacMap_t, DsSgmacMap_linkAggregationChannelGroup_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
        if (field_val != 0)
        {
            p_sys_trunk = ctc_vector_get(g_gg_stacking_master[lchip]->p_trunk_vec, field_val);
            if (NULL == p_sys_trunk)
            {
                ret = CTC_E_STK_TRUNK_NOT_EXIST;
                goto done;
            }
            CTC_BIT_SET(p_sys_trunk->rgchip_bitmap, index);

            g_gg_stacking_master[lchip]->neigbor_chip[index] = field_val;
        }
    }

    /*Restore  mcast*/
    CTC_WB_INIT_QUERY_T((&wb_query),sys_wb_stacking_mcast_db_t, CTC_FEATURE_STACKING, SYS_WB_APPID_STACKING_SUBID_MCAST_DB);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_stacking_mcast = (sys_wb_stacking_mcast_db_t *)wb_query.buffer + entry_cnt++;

        p_sys_mcast = mem_malloc(MEM_STK_MODULE, sizeof(sys_stacking_mcast_db_t));
        if (NULL == p_sys_mcast)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_sys_mcast, 0, sizeof(sys_stacking_mcast_db_t));

        ret = _sys_goldengate_stacking_wb_mapping_mcast(lchip, p_wb_stacking_mcast, p_sys_mcast, 0);
        if (ret)
        {
            mem_free(p_sys_mcast);
            goto done;
        }

        ctc_hash_insert(g_gg_stacking_master[lchip]->mcast_hash, p_sys_mcast);

    CTC_WB_QUERY_ENTRY_END((&wb_query));

done:
    if (wb_query.buffer)
    {
        mem_free(wb_query.buffer);
    }

   return ret;
}

int32
sys_goldengate_stacking_init(uint8 lchip, void* p_cfg)
{
    int32 ret           = CTC_E_NONE;

    sys_goldengate_opf_t opf;
    ctc_stacking_glb_cfg_t* p_glb_cfg = (ctc_stacking_glb_cfg_t*)p_cfg;

    if (g_gg_stacking_master[lchip])
    {
        return CTC_E_NONE;
    }
    g_gg_stacking_master[lchip] = (sys_stacking_master_t*)mem_malloc(MEM_STK_MODULE, sizeof(sys_stacking_master_t));
    if (NULL == g_gg_stacking_master[lchip])
    {
        ret = CTC_E_NO_MEMORY;
        goto ERROR_FREE_MEM;
    }

    sal_memset(g_gg_stacking_master[lchip], 0, sizeof(sys_stacking_master_t));

    g_gg_stacking_master[lchip]->p_trunk_vec = ctc_vector_init(SYS_STK_TRUNK_VEC_BLOCK_NUM, SYS_STK_TRUNK_VEC_BLOCK_SIZE);
    if (NULL == g_gg_stacking_master[lchip]->p_trunk_vec)
    {
        ret = CTC_E_NO_MEMORY;
        goto ERROR_FREE_MEM;
    }


    g_gg_stacking_master[lchip]->mcast_hash  = ctc_hash_create(
            16,
            16,
            (hash_key_fn)_sys_goldengate_stacking_mcast_hash_make,
            (hash_cmp_fn)_sys_goldengate_stacking_mcast_hash_cmp);

    if (NULL == g_gg_stacking_master[lchip]->mcast_hash)
    {
        ret = CTC_E_NO_MEMORY;
        goto ERROR_FREE_MEM;
    }

    /*init opf*/
    g_gg_stacking_master[lchip]->opf_type = OPF_STACKING;
    ret = sys_goldengate_opf_init(lchip, g_gg_stacking_master[lchip]->opf_type, SYS_STK_OPF_MAX);
    if (CTC_E_NONE != ret)
    {
        goto ERROR_FREE_MEM;
    }

    sal_memset(&opf, 0, sizeof(opf));
    opf.pool_type  = g_gg_stacking_master[lchip]->opf_type;

    opf.pool_index = SYS_STK_OPF_MEM_BASE;
    ret = sys_goldengate_opf_init_offset(lchip, &opf, 0 , SYS_STK_TRUNK_MEMBERS);
    if (CTC_E_NONE != ret)
    {
        goto ERROR_FREE_MEM;
    }

    opf.pool_index = SYS_STK_OPF_MCAST_PROFILE;
    ret = sys_goldengate_opf_init_offset(lchip, &opf, 1 , (256-1));
    if (CTC_E_NONE != ret)
    {
        goto ERROR_FREE_MEM;
    }

    ret =  sys_goldengate_stacking_mcast_group_create(lchip, 0, 0, &g_gg_stacking_master[lchip]->p_default_prof_db, 0);
    if (NULL == g_gg_stacking_master[lchip]->p_default_prof_db)
    {
        goto ERROR_FREE_MEM;
    }
    g_gg_stacking_master[lchip]->stacking_mcast_offset = g_gg_stacking_master[lchip]->p_default_prof_db->head_met_offset;

    ret = sys_goldengate_stacking_create_keeplive_group(lchip, 0);
    if (CTC_E_NONE != ret)
    {
        goto ERROR_FREE_MEM;
    }

    ret = _sys_goldengate_stacking_hdr_set(lchip, &p_glb_cfg->hdr_glb);
    if (CTC_E_NONE != ret)
    {
        goto ERROR_FREE_MEM;
    }

    ret = _sys_goldengate_stacking_set_enable(lchip, TRUE, p_glb_cfg->fabric_mode);
    if (CTC_E_NONE != ret)
    {
        goto ERROR_FREE_MEM;
    }

    ret = sal_mutex_create(&(g_gg_stacking_master[lchip]->p_stacking_mutex));
    if (ret || !(g_gg_stacking_master[lchip]->p_stacking_mutex))
    {
            goto ERROR_FREE_MEM;
    }

    g_gg_stacking_master[lchip]->stacking_en = 1;
    g_gg_stacking_master[lchip]->mcast_mode = p_glb_cfg->mcast_mode;
    g_gg_stacking_master[lchip]->ipv4_encap = p_glb_cfg->hdr_glb.is_ipv4;
    g_gg_stacking_master[lchip]->trunk_mode = p_glb_cfg->trunk_mode;
    g_gg_stacking_master[lchip]->bind_mcast_en = 0;
    CTC_ERROR_GOTO(sys_goldengate_packet_tx_set_property(lchip, SYS_PKT_TX_TYPE_STACKING_EN, 1, 0), ret, ERROR_FREE_MEM);

    CTC_ERROR_GOTO(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_STACKING, sys_goldengate_stacking_wb_sync), ret, ERROR_FREE_MEM);

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_GOTO(sys_goldengate_stacking_wb_restore(lchip), ret, ERROR_FREE_MEM);
    }

    return CTC_E_NONE;

ERROR_FREE_MEM:

    if (NULL != g_gg_stacking_master[lchip]->p_trunk_vec)
    {
        ctc_vector_release(g_gg_stacking_master[lchip]->p_trunk_vec);
    }

    if (g_gg_stacking_master[lchip]->stacking_mcast_offset)
    {

    }

    if (g_gg_stacking_master[lchip]->p_stacking_mutex)
    {
        sal_mutex_destroy(g_gg_stacking_master[lchip]->p_stacking_mutex);
    }

    if (NULL != g_gg_stacking_master[lchip])
    {
        mem_free(g_gg_stacking_master[lchip]);
    }

    return ret;
}

STATIC int32
_sys_goldengate_stacking_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_stacking_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == g_gg_stacking_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_goldengate_opf_deinit(lchip, OPF_STACKING);

    /*free trunk vector*/
    ctc_vector_traverse(g_gg_stacking_master[lchip]->p_trunk_vec, (vector_traversal_fn)_sys_goldengate_stacking_free_node_data, NULL);
    ctc_vector_release(g_gg_stacking_master[lchip]->p_trunk_vec);

    /*free mcast hash*/
    ctc_hash_traverse(g_gg_stacking_master[lchip]->mcast_hash, (hash_traversal_fn)_sys_goldengate_stacking_free_node_data, NULL);
    ctc_hash_free(g_gg_stacking_master[lchip]->mcast_hash);

    sal_mutex_destroy(g_gg_stacking_master[lchip]->p_stacking_mutex);
    mem_free(g_gg_stacking_master[lchip]);

    return CTC_E_NONE;
}

