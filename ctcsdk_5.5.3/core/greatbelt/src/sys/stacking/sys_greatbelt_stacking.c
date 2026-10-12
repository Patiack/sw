/**
 @file sys_greatbelt_stacking.c

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

#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_stacking.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_queue_drop.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_linkagg.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_packet.h"

#include "sys_greatbelt_common.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_internal_port.h"

#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_lib.h"
/****************************************************************************
*
* Defines and Macros
*
*****************************************************************************/
#define SYS_STK_TRUNK_MEMBERS 512
#define SYS_STK_TRUNK_DLB_MAX_MEMBERS 8
#define SYS_STK_TRUNK_MAX_MEMBERS 32
#define SYS_STK_TRUNK_STATIC_MAX_MEMBERS 8

#define SYS_STK_TRUNK_MAX_ID 63
#define SYS_STK_TRUNK_MIN_ID 1

#define SYS_STK_TRUNK_VEC_BLOCK_NUM  64
#define SYS_STK_TRUNK_VEC_BLOCK_SIZE 8

#define SYS_STK_KEEPLIVE_VEC_BLOCK_SIZE 1024
#define SYS_STK_KEEPLIVE_MEM_MAX 3

#define SYS_STK_DLB_TS_THRES 200
#define SYS_STK_DLB_MAX_PTR 9999
#define SYS_STK_DLB_UPDATE_THRES 10000
#define SYS_STK_DLB_TIMER_INTERVAL 3

#define SYS_STK_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip);\
        if (NULL == g_gb_stacking_master[lchip]){ \
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
    if (g_gb_stacking_master[lchip]->p_stacking_mutex) sal_mutex_lock(g_gb_stacking_master[lchip]->p_stacking_mutex)
#define STACKING_UNLOCK \
    if (g_gb_stacking_master[lchip]->p_stacking_mutex) sal_mutex_unlock(g_gb_stacking_master[lchip]->p_stacking_mutex)

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
    uint16 mem_base;

    uint32 mem_ports[SYS_STK_TRUNK_MAX_MEMBERS];

    uint32 rgchip_bitmap;
    uint32 flag;
    ctc_stacking_hdr_encap_t encap_hdr;

    uint16   dsmet_offset;
    uint16   next_dsmet_offset;

    uint8 lchip;
    uint8 mode;
    uint8 replace_en;

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

    uint16   head_met_offset;
    uint16   tail_met_offset;

    uint8 append_en;
    uint8 alloc_id;
    uint16 last_tail_offset;

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
    ctc_hash_t* mcast_hash;
    uint32 stacking_mcast_offset;
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
        if (((lport) >= 64) && ((lport) != SYS_RESERVE_PORT_ID_INTLK)){ \
            return CTC_E_STK_TRUNK_MEMBER_PORT_INVALID; } \
    }


#define SYS_STK_EXTEND_EN(extend) \
    { \
        if (extend.enable) \
        { \
            SYS_MAP_GCHIP_TO_LCHIP(extend.gchip, lchip); \
        } \
    }

#define SYS_STK_MET_TRUNKID(trunk_id) (((trunk_id & 0x3F) << 5) | 0x1F)

sys_stacking_master_t* g_gb_stacking_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
static uint8 dlb_port_index_mapping[SYS_STK_TRUNK_DLB_MAX_MEMBERS] = {2, 3, 0, 1, 6, 7, 4, 5};

#define SYS_STK_FABRIC_PORT 55
#define SYS_STK_FABRIC_TRUNK 63
#define SYS_STK_MAX_PORT_NUM_PER_CHIP 512
/****************************************************************************
*
* Function
*
**
***************************************************************************/
extern int32
sys_greatbelt_l2_reserve_rchip_ad_index(uint8 lchip, uint8 gchip, uint16 max_port_num, uint8 slice_en);

extern int32
sys_greatbelt_l2_free_rchip_ad_index(uint8 lchip, uint8 gchip);

STATIC int32
_sys_greatbelt_stacking_reverve_member(uint8 lchip, uint8 tid)
{
    uint32 dest_channel = 0;
    uint32 cmd_member_set = 0;
    uint32 cmd_group_r = 0;
    uint8 index = 0;
    ds_link_aggregate_sgmac_group_t ds_linkagg_sgmac_group;

    sal_memset(&ds_linkagg_sgmac_group, 0, sizeof(ds_link_aggregate_sgmac_group_t));

    dest_channel = SYS_GET_CHANNEL_ID(lchip, SYS_RESERVE_PORT_ID_DROP);
    cmd_group_r = DRV_IOR(DsLinkAggregateSgmacMemberSet_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tid, cmd_group_r, &ds_linkagg_sgmac_group));

    for (index = 0; index < 8; index++)
    {
        cmd_member_set = DRV_IOW(DsLinkAggregateSgmacMemberSet_t, DsLinkAggregateSgmacMemberSet_DestSgmac0_f + index);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_linkagg_sgmac_group.sgmac_link_agg_member_ptr, cmd_member_set, &dest_channel));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stacking_dlb_add_member(uint8 lchip, uint8 trunk_id, uint8 port_index, uint16 channel_id)
{
    uint32 cmd = 0;
    uint32 field_value = 0;

    cmd = DRV_IOW(DsLinkAggregateSgmacMemberSet_t, DsLinkAggregateSgmacMemberSet_DestSgmac0_f+ dlb_port_index_mapping[port_index]);
    field_value = channel_id;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, trunk_id, cmd, &field_value));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stacking_dlb_del_member(uint8 lchip, uint8 trunk_id, uint8 port_index, uint8 tail_index, uint16 channel_id)
{
    uint32 cmd = 0;
    uint32 field_value = 0;

    if (tail_index >= SYS_STK_TRUNK_DLB_MAX_MEMBERS)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    /* copy the last one to the removed position */
    field_value = channel_id;
    cmd = DRV_IOR(DsLinkAggregateSgmacMemberSet_t, DsLinkAggregateSgmacMemberSet_DestSgmac0_f+dlb_port_index_mapping[tail_index]);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, trunk_id, cmd, &field_value));

    cmd = DRV_IOW(DsLinkAggregateSgmacMemberSet_t, DsLinkAggregateSgmacMemberSet_DestSgmac0_f+dlb_port_index_mapping[port_index]);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, trunk_id, cmd, &field_value));

    /* set the last one to reserve */
    field_value = SYS_GET_CHANNEL_ID(lchip, SYS_RESERVE_PORT_ID_DROP);
    cmd = DRV_IOW(DsLinkAggregateSgmacMemberSet_t, DsLinkAggregateSgmacMemberSet_DestSgmac0_f+dlb_port_index_mapping[tail_index]);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, trunk_id, cmd, &field_value));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stacking_clear_flow_active(uint8 lchip, uint8 trunk_id)
{
    uint32 cmd_r = 0;
    uint32 cmd_w = 0;
    ds_link_aggregate_sgmac_group_t ds_link_aggregate_sgmac_group;
    ds_link_aggregate_sgmac_member_t ds_link_aggregate_sgmac_member;
    uint16 flow_num = 0;
    uint32 flow_base = 0;
    uint16 index = 0;

    sal_memset(&ds_link_aggregate_sgmac_group, 0, sizeof(ds_link_aggregate_sgmac_group_t));
    sal_memset(&ds_link_aggregate_sgmac_member, 0, sizeof(ds_link_aggregate_sgmac_member_t));

    cmd_r = DRV_IOR(DsLinkAggregateSgmacGroup_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, trunk_id, cmd_r, &ds_link_aggregate_sgmac_group));

    flow_base = ds_link_aggregate_sgmac_group.sgmac_link_agg_mem_base;

    flow_num = 32;

    /* clear active */
    cmd_r = DRV_IOR(DsLinkAggregateSgmacMember_t, DRV_ENTRY_FLAG);
    cmd_w = DRV_IOW(DsLinkAggregateSgmacMember_t, DRV_ENTRY_FLAG);

    for (index = 0; index < flow_num; index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, flow_base+index, cmd_r, &ds_link_aggregate_sgmac_member));
        ds_link_aggregate_sgmac_member.active = 0;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, flow_base+index, cmd_w, &ds_link_aggregate_sgmac_member));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stacking_failover_add_member(uint8 lchip, uint8 trunk_id, uint16 gport)
{
    uint32 cmd = 0;
    ds_link_aggregate_channel_t linkagg_channel;
    uint32 channel_id = 0;

    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "tid = 0x%x gport =  %d \n", trunk_id, gport);

    sal_memset(&linkagg_channel, 0, sizeof(linkagg_channel));

    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);

    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    linkagg_channel.group_type = 2;
    linkagg_channel.group_en = 1;
    linkagg_channel.link_aggregation_group = trunk_id;

    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_stacking_failover_del_member(uint8 lchip, uint8 trunk_id, uint16 gport)
{
    uint32 cmd = 0;
    ds_link_aggregate_channel_t linkagg_channel;
    uint8 channel_id = 0;

    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "tid = 0x%x gport =  %d \n", trunk_id, gport);

    sal_memset(&linkagg_channel, 0, sizeof(linkagg_channel));

    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);

    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    linkagg_channel.group_type = 0;
    linkagg_channel.group_en = 0;

    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    return CTC_E_NONE;
}


int32
sys_greatbelt_stacking_get_rsv_trunk_number(uint8 lchip, uint8* number)
{
    CTC_PTR_VALID_CHECK(number);

    if (NULL == g_gb_stacking_master[lchip])
    {
        *number = 0;
        return CTC_E_NONE;
    }

    if (g_gb_stacking_master[lchip]->trunk_num > 2)
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
_sys_greatbelt_stacking_hdr_set(uint8 lchip, ctc_stacking_hdr_glb_t* p_hdr_glb)
{
    ipe_header_adjust_ctl_t ipe_header_adjust_ctl;
    epe_pkt_hdr_ctl_t epe_pkt_hdr_ctl;
    uint32 cmd  = 0;
    uint32 ipv6_sa[CTC_IPV6_ADDR_LEN];

    SYS_STK_DSCP_CHECK(p_hdr_glb->ip_dscp);
    SYS_STK_COS_CHECK(p_hdr_glb->cos);

    sal_memset(&ipe_header_adjust_ctl, 0, sizeof(ipe_header_adjust_ctl_t));
    cmd = DRV_IOR(IpeHeaderAdjustCtl_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_header_adjust_ctl));

    sal_memset(&epe_pkt_hdr_ctl, 0, sizeof(epe_pkt_hdr_ctl_t));
    cmd = DRV_IOR(EpePktHdrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_hdr_ctl));

    ipe_header_adjust_ctl.header_mac_da_check_en = p_hdr_glb->mac_da_chk_en;
    ipe_header_adjust_ctl.header_tpid = p_hdr_glb->vlan_tpid;
    ipe_header_adjust_ctl.header_ether_type_check_disable = p_hdr_glb->ether_type_chk_en ? 0 : 1;
    ipe_header_adjust_ctl.header_ether_type = p_hdr_glb->ether_type;
    ipe_header_adjust_ctl.header_ip_protocol = p_hdr_glb->ip_protocol;
    ipe_header_adjust_ctl.header_udp_en      = p_hdr_glb->udp_en;
    ipe_header_adjust_ctl.udp_src_port = p_hdr_glb->udp_dest_port;
    ipe_header_adjust_ctl.udp_dest_port = p_hdr_glb->udp_src_port;

    epe_pkt_hdr_ctl.tpid = p_hdr_glb->vlan_tpid;
    epe_pkt_hdr_ctl.header_ether_type  = p_hdr_glb->ether_type;
    epe_pkt_hdr_ctl.header_ip_protocol = p_hdr_glb->ip_protocol;

    epe_pkt_hdr_ctl.header_ttl      = p_hdr_glb->ip_ttl;
    epe_pkt_hdr_ctl.header_dscp     = p_hdr_glb->ip_dscp;
    epe_pkt_hdr_ctl.header_cos2_1   = (p_hdr_glb->cos >> 1) & 0x3;
    epe_pkt_hdr_ctl.header_cos0     = (p_hdr_glb->cos & 0x1);

    g_gb_stacking_master[lchip]->ipv4_encap = p_hdr_glb->is_ipv4;

    if (p_hdr_glb->is_ipv4)
    {
        epe_pkt_hdr_ctl.header_ip_sa31_0   = p_hdr_glb->ipsa.ipv4;
    }
    else
    {
        sal_memcpy(ipv6_sa, p_hdr_glb->ipsa.ipv6, sizeof(ipv6_addr_t));
        epe_pkt_hdr_ctl.header_ip_sa31_0   = ipv6_sa[3];
        epe_pkt_hdr_ctl.header_ip_sa63_32  = ipv6_sa[2];
        epe_pkt_hdr_ctl.header_ip_sa95_64  = ipv6_sa[1];
        epe_pkt_hdr_ctl.header_ip_sa127_96 = ipv6_sa[0];
    }

    epe_pkt_hdr_ctl.header_udp_en = p_hdr_glb->udp_en;
    epe_pkt_hdr_ctl.use_udp_payload_length = 1;
    epe_pkt_hdr_ctl.use_ip_payload_length = 1;
    epe_pkt_hdr_ctl.udp_src_port = p_hdr_glb->udp_src_port;
    epe_pkt_hdr_ctl.udp_dest_port = p_hdr_glb->udp_dest_port;

    /*Encap header param*/
    cmd = DRV_IOW(IpeHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_header_adjust_ctl));

    cmd = DRV_IOW(EpePktHdrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_hdr_ctl));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_stacking_hdr_get(uint8 lchip, ctc_stacking_hdr_glb_t* p_hdr_glb)
{

    ipe_header_adjust_ctl_t ipe_header_adjust_ctl;
    epe_pkt_hdr_ctl_t epe_pkt_hdr_ctl;
    uint32 cmd  = 0;
    uint32 ipv6_sa[CTC_IPV6_ADDR_LEN];

    sal_memset(&ipe_header_adjust_ctl, 0, sizeof(ipe_header_adjust_ctl_t));
    cmd = DRV_IOR(IpeHeaderAdjustCtl_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_header_adjust_ctl));

    sal_memset(&epe_pkt_hdr_ctl, 0, sizeof(epe_pkt_hdr_ctl_t));
    cmd = DRV_IOR(EpePktHdrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_hdr_ctl));

    p_hdr_glb->mac_da_chk_en     = ipe_header_adjust_ctl.header_mac_da_check_en;
    p_hdr_glb->vlan_tpid         = ipe_header_adjust_ctl.header_tpid;
    p_hdr_glb->ether_type_chk_en = ipe_header_adjust_ctl.header_ether_type_check_disable ? 0 : 1;
    p_hdr_glb->ether_type        = ipe_header_adjust_ctl.header_ether_type;

    p_hdr_glb->ip_protocol   = ipe_header_adjust_ctl.header_ip_protocol;
    p_hdr_glb->udp_en        = ipe_header_adjust_ctl.header_udp_en;
    p_hdr_glb->udp_dest_port = ipe_header_adjust_ctl.udp_src_port;
    p_hdr_glb->udp_src_port  = ipe_header_adjust_ctl.udp_dest_port;

    p_hdr_glb->ip_protocol = epe_pkt_hdr_ctl.header_ip_protocol;

    p_hdr_glb->ip_ttl        = epe_pkt_hdr_ctl.header_ttl;
    p_hdr_glb->ip_dscp       = epe_pkt_hdr_ctl.header_dscp;
    p_hdr_glb->cos           = (epe_pkt_hdr_ctl.header_cos2_1 << 1) | epe_pkt_hdr_ctl.header_cos0;

    p_hdr_glb->is_ipv4 = g_gb_stacking_master[lchip]->ipv4_encap;

    if (p_hdr_glb->is_ipv4)
    {
        p_hdr_glb->ipsa.ipv4        = epe_pkt_hdr_ctl.header_ip_sa31_0;
    }
    else
    {
        ipv6_sa[3] = epe_pkt_hdr_ctl.header_ip_sa31_0;
        ipv6_sa[2] = epe_pkt_hdr_ctl.header_ip_sa63_32;
        ipv6_sa[1] = epe_pkt_hdr_ctl.header_ip_sa95_64;
        ipv6_sa[0] = epe_pkt_hdr_ctl.header_ip_sa127_96;
        sal_memcpy(p_hdr_glb->ipsa.ipv6, ipv6_sa, sizeof(ipv6_addr_t));
    }

    return CTC_E_NONE;

}

uint32
_sys_greatbelt_stacking_mcast_hash_make(sys_stacking_mcast_db_t* backet)
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
_sys_greatbelt_stacking_mcast_hash_cmp(sys_stacking_mcast_db_t* backet1,
                                sys_stacking_mcast_db_t* backet2)
{

    if (!backet1 || !backet1)
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
sys_greatbelt_stacking_mcast_group_create(uint8 lchip,
                                           uint8 type,
                                           uint16 id,
                                           sys_stacking_mcast_db_t **pp_mcast_db,
                                           uint8 append_en)
{
    int32  ret                     = CTC_E_NONE;
    uint32 new_met_offset          = 0;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;
    ds_met_entry_t dsmet;

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
        p_mcast_db->last_tail_offset = g_gb_stacking_master[lchip]->stacking_mcast_offset;
    }
    else
    {
        p_mcast_db->last_tail_offset = SYS_NH_INVALID_OFFSET;
    }

    if (type == 0)
    {
        /*alloc resource*/
        CTC_ERROR_GOTO(sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_MET, 1, &new_met_offset),
                       ret, Error1);

        sal_memset(&dsmet, 0, sizeof(dsmet));
        dsmet.next_met_entry_ptr = p_mcast_db->last_tail_offset;
        dsmet.ucast_id_low = SYS_STK_MET_TRUNKID(0);
        dsmet.remote_chip = 1;
        dsmet.is_met = 1;
        CTC_ERROR_GOTO(sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET, new_met_offset, &dsmet),
                       ret, Error2);
        /*add db*/
        p_mcast_db->head_met_offset = new_met_offset;
        p_mcast_db->tail_met_offset = p_mcast_db->last_tail_offset;
        p_mcast_db->append_en = append_en;


    }
    else
    {
        new_met_offset = id;

        sal_memset(&dsmet, 0, sizeof(dsmet));
        dsmet.next_met_entry_ptr = p_mcast_db->last_tail_offset;
        dsmet.ucast_id_low = SYS_RESERVE_PORT_ID_DROP;
        dsmet.end_local_rep = 1;
        dsmet.is_met = 1;
        CTC_ERROR_GOTO(sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET, new_met_offset, &dsmet),
                       ret, Error1);
        /*add db*/
        p_mcast_db->head_met_offset = new_met_offset;
        p_mcast_db->tail_met_offset = new_met_offset;
    }

    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "head_met_offset :%d\n ", new_met_offset);
    ctc_hash_insert(g_gb_stacking_master[lchip]->mcast_hash, p_mcast_db);


    *pp_mcast_db = p_mcast_db;

    return CTC_E_NONE;


    Error2:

    if (type == 0)
    {
        sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_MET, 1, new_met_offset);
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
sys_greatbelt_stacking_mcast_group_destroy(uint8 lchip,
                                            uint8 type,
                                            uint16 id,
                                            sys_stacking_mcast_db_t *p_mcast_db)
{
    ds_met_entry_t dsmet;

    sal_memset(&dsmet, 0, sizeof(dsmet));
    dsmet.next_met_entry_ptr = SYS_NH_INVALID_OFFSET;
    dsmet.ucast_id_low = SYS_STK_MET_TRUNKID(0);
    dsmet.remote_chip = 1;
    dsmet.is_met = 1;
    CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET,
                                                        p_mcast_db->head_met_offset, &dsmet));

    if (type == 0)
    {
        /*free resource*/
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_MET, 1, p_mcast_db->head_met_offset));

    }

    ctc_hash_remove(g_gb_stacking_master[lchip]->mcast_hash, p_mcast_db);

    mem_free(p_mcast_db);

    return CTC_E_NONE;
}

int32
sys_greatbelt_stacking_mcast_group_add_member(uint8 lchip,
                                               sys_stacking_mcast_db_t *p_mcast_db,
                                               uint8 trunk_id)
{
    int32  ret                     = CTC_E_NONE;
    uint32 new_met_offset          = 0;
    uint32 cmd                     = 0;
    ds_met_entry_t dsmet;

    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "trunk_id = %d, head:%d \n", trunk_id, p_mcast_db->head_met_offset);

    if (p_mcast_db->tail_met_offset == p_mcast_db->last_tail_offset)
    {

        sal_memset(&dsmet, 0, sizeof(dsmet));
        dsmet.next_met_entry_ptr = p_mcast_db->last_tail_offset;
        dsmet.ucast_id_low = SYS_STK_MET_TRUNKID(trunk_id);
        dsmet.remote_chip = 1;
        dsmet.is_met = 1;
        CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET,
                                                            p_mcast_db->head_met_offset, &dsmet));

        p_mcast_db->tail_met_offset = p_mcast_db->head_met_offset;

        SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "update head_met_offset :%d, trunk-id:%d \n ", new_met_offset, trunk_id);

    }
    else
    {

        /*new mcast offset*/
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_MET, 1, &new_met_offset));
        sal_memset(&dsmet, 0, sizeof(dsmet));
        dsmet.next_met_entry_ptr = p_mcast_db->last_tail_offset;
        dsmet.ucast_id_low = SYS_STK_MET_TRUNKID(trunk_id);
        dsmet.remote_chip = 1;
        dsmet.is_met = 1;
        CTC_ERROR_GOTO(sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET, new_met_offset, &dsmet),
                       ret, Error0);


        cmd = DRV_IOR(DsMetEntry_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO( DRV_IOCTL(lchip, p_mcast_db->tail_met_offset, cmd, &dsmet),
                       ret, Error0);

        dsmet.next_met_entry_ptr = new_met_offset;

        cmd = DRV_IOW(DsMetEntry_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO( DRV_IOCTL(lchip, p_mcast_db->tail_met_offset, cmd, &dsmet),
                       ret, Error0);


        SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "add new_met_offset :%d, pre_offset:%d, trunk-id:%d\n ", new_met_offset, p_mcast_db->tail_met_offset, trunk_id);


        p_mcast_db->tail_met_offset = new_met_offset;
    }

    CTC_BIT_SET(p_mcast_db->trunk_bitmap[trunk_id / 32], trunk_id % 32);

    return CTC_E_NONE;

    Error0:
    sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_MET, 1, new_met_offset);

    return ret;


}

int32
sys_greatbelt_stacking_mcast_group_remove_member(uint8 lchip,
                                                  sys_stacking_mcast_db_t *p_mcast_db,
                                                  uint8 trunk_id)
{
    uint16 trunk_id_hw             = 0;
    uint16 remote_chip             = 0;
    uint32 cmd                     = 0;
    uint32 head_met_offset         = 0;
    uint32 pre_met_offset          = 0;
    uint32 cur_met_offset          = 0;
    uint32 next_met_offset         = 0;
    ds_met_entry_t dsmet;

    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "trunk_id = %d, head:%d \n", trunk_id, p_mcast_db->head_met_offset);

    head_met_offset = p_mcast_db->head_met_offset;
    cur_met_offset  = head_met_offset;
    next_met_offset = head_met_offset;


    do
    {
        pre_met_offset = cur_met_offset;
        cur_met_offset = next_met_offset;

        cmd = DRV_IOR(DsMetEntry_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN( DRV_IOCTL(lchip, cur_met_offset, cmd, &dsmet));

        trunk_id_hw = ((dsmet.ucast_id_low>>5)&0x3F);
        remote_chip =  dsmet.remote_chip;
        next_met_offset = dsmet.next_met_entry_ptr;

        if ((remote_chip == 1) && (trunk_id_hw == trunk_id))
        {
            break;
        }

    }
    while (next_met_offset != p_mcast_db->last_tail_offset);


    if (cur_met_offset == head_met_offset)
    {
        /*remove is first met*/

        if (next_met_offset == p_mcast_db->last_tail_offset)
        {
            /*remove all*/

            sal_memset(&dsmet, 0, sizeof(dsmet));
            dsmet.next_met_entry_ptr= p_mcast_db->last_tail_offset;
            dsmet.ucast_id_low = SYS_STK_MET_TRUNKID(0);
            dsmet.remote_chip = 1;
            dsmet.is_met = 1;
            CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET,
                                                                head_met_offset, &dsmet));

            p_mcast_db->tail_met_offset = p_mcast_db->last_tail_offset;

            SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Update first to drop, head_met_offset: %d, trunk_id:%d\n ",
                            head_met_offset, trunk_id);

            SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "tail:%d\n ",  p_mcast_db->tail_met_offset);
        }
        else
        {

            /*nexthop change to first*/
            cmd = DRV_IOR(DsMetEntry_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN( DRV_IOCTL(lchip, next_met_offset, cmd, &dsmet));

            /*nexthop change to first*/
            cmd = DRV_IOW(DsMetEntry_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, cur_met_offset, cmd, &dsmet));

            /*free currect offset*/
            CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_MET, 1, next_met_offset));


            if (p_mcast_db->tail_met_offset == next_met_offset)
            {
                p_mcast_db->tail_met_offset = cur_met_offset;
            }


            SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Replace first head_met_offset: %d, trunk_id:%d\n ",
                            head_met_offset, trunk_id);

            SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Free Met offset:%d, tail:%d\n ",  next_met_offset, p_mcast_db->tail_met_offset);

        }
    }
    else
    {
        cmd = DRV_IOR(DsMetEntry_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN( DRV_IOCTL(lchip, pre_met_offset, cmd, &dsmet));

        dsmet.next_met_entry_ptr = next_met_offset;

        cmd = DRV_IOW(DsMetEntry_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN( DRV_IOCTL(lchip, pre_met_offset, cmd, &dsmet));

        /*free currect offset*/
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_MET, 1, cur_met_offset));

        if (p_mcast_db->tail_met_offset == cur_met_offset)
        {
            p_mcast_db->tail_met_offset = pre_met_offset;
        }

        SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Remove middle pre: %d, cur:%d, next:%d, trunk_id:%d\n ",
                        pre_met_offset, cur_met_offset, next_met_offset,  trunk_id);

        SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Free Met offset:%d, tail:%d\n ",  cur_met_offset, p_mcast_db->tail_met_offset);


    }


    CTC_BIT_UNSET(p_mcast_db->trunk_bitmap[trunk_id / 32], trunk_id % 32);

    return CTC_E_NONE;
}



STATIC int32
_sys_greatbelt_stacking_register_en(uint8 lchip, uint8 enable)
{
    int32 ret           = CTC_E_NONE;
    uint32 cmd          = 0;
    uint32 value        = 0;
    uint32 ds_nh_offset = 0;
    uint8 gchip_id      = 0;
    uint32 field_val    = 0;
    uint8  excp_idx     = 0;
    ipe_header_adjust_ctl_t  ipe_hdr_adjust_ctl;
    ipe_fwd_ctl_t            ipe_fwd_ctl;
    met_fifo_ctl_t           met_fifo_ctl;
    q_write_sgmac_ctl_t      qwrite_sgmac_ctl;
    buffer_retrieve_ctl_t    buffer_retrieve_ctl;
    epe_hdr_adjust_ctl_t     epe_hdr_adjust_ctl;
    epe_next_hop_ctl_t       epe_next_hop_ctl;
    epe_pkt_proc_ctl_t       epe_pkt_proc_ctl;
    epe_header_edit_ctl_t    epe_header_edit_ctl;
    epe_pkt_hdr_ctl_t        epe_pkt_hdr_ctl;
    buffer_store_ctl_t buffer_store_ctl;
    ds_queue_num_gen_ctl_t ds_queue_num_gen_ctl;
    ds_buf_retrv_excp_t ds_buf_retrv_excp;

    value = enable ? 1 : 0;
    sys_greatbelt_get_gchip_id(lchip, &gchip_id);

    /*IPE Hdr Adjust Ctl*/
    sal_memset(&ipe_hdr_adjust_ctl, 0, sizeof(ipe_header_adjust_ctl_t));
    cmd = DRV_IOR(IpeHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_hdr_adjust_ctl));
    ipe_hdr_adjust_ctl.packet_header_bypass_all  = value;
    ipe_hdr_adjust_ctl.discard_non_packet_header = value;
    cmd = DRV_IOW(IpeHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_hdr_adjust_ctl));

    /*ingress edit*/
    sal_memset(&ipe_fwd_ctl, 0, sizeof(ipe_fwd_ctl));
    cmd = DRV_IOR(IpeFwdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_fwd_ctl));
    ipe_fwd_ctl.stacking_en                = value;

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_resolved_offset(lchip,
                         SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH,  &ds_nh_offset));
    ipe_fwd_ctl.ingress_edit_nexthop_ptr = ds_nh_offset;

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_resolved_offset(lchip,
                         SYS_NH_RES_OFFSET_TYPE_MIRROR_NH,  &ds_nh_offset));
    ipe_fwd_ctl.ingress_edit_mirror_nexthop_ptr = ds_nh_offset;

    cmd = DRV_IOW(IpeFwdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_fwd_ctl));

    /*Buffer Store Ctl init*/
    sal_memset(&buffer_store_ctl, 0, sizeof(buffer_store_ctl));
    cmd = DRV_IOR(BufferStoreCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));
    buffer_store_ctl.stacking_en                 = value;
    buffer_store_ctl.chip_id_check_disable       = value;
    buffer_store_ctl.local_switching_disable     = 0;
    buffer_store_ctl.force_discard_source_chip   = 0;
    buffer_store_ctl.critical_packet_from_fabric = 0;
     /*buffer_store_ctl.discard_source_chip         = 1 << gchip_id;  //Discard source-orginal packet*/
    cmd = DRV_IOW(BufferStoreCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));

    /*stacking header can be mirror*/
    sal_memset(&ds_buf_retrv_excp, 0, sizeof(ds_buf_retrv_excp_t));

    for (excp_idx = 0; excp_idx < 24; excp_idx++)
    {
        /*ingress mirror*/
        field_val = value;
        cmd = DRV_IOW(DsBufRetrvExcp_t, DsBufRetrvExcp_PacketOffsetReset_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &field_val));
    }

    for (excp_idx = 32; excp_idx < 55; excp_idx++)
    {
        /*egress mirror*/
        field_val = value;
        cmd = DRV_IOW(DsBufRetrvExcp_t, DsBufRetrvExcp_PacketOffsetReset_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &field_val));
    }

    /*MetFifo Ctl init*/
    sal_memset(&met_fifo_ctl, 0, sizeof(met_fifo_ctl_t));
    cmd = DRV_IOR(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &met_fifo_ctl));
    met_fifo_ctl.stacking_en                     = value;
    met_fifo_ctl.stacking_mcast_end_en           = value;
    met_fifo_ctl.from_fabric_multicast_en        = value;
    met_fifo_ctl.exception_reset_src_sgmac       = value;
    met_fifo_ctl.uplink_reflect_check_en         = value;
    met_fifo_ctl.force_replication_from_fabric   = value;
    met_fifo_ctl.stacking_broken                 = 0;
    met_fifo_ctl.discard_met_loop                = 0;
    cmd = DRV_IOW(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &met_fifo_ctl));

    /*Qwrite Sgmac Ctl init*/
    sal_memset(&qwrite_sgmac_ctl, 0, sizeof(q_write_sgmac_ctl_t));
    cmd = DRV_IOR(QWriteSgmacCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qwrite_sgmac_ctl));
    qwrite_sgmac_ctl.sgmac_en                   = value;
    qwrite_sgmac_ctl.uplink_reflect_check_en    = value;
    qwrite_sgmac_ctl.discard_unkown_sgmac_group = value;
    qwrite_sgmac_ctl.sgmac_index_mcast_en       = 0;
    cmd = DRV_IOW(QWriteSgmacCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qwrite_sgmac_ctl));

    /*Buffer Retrive Ctl init*/
    sal_memset(&buffer_retrieve_ctl, 0, sizeof(buffer_retrieve_ctl));
    cmd = DRV_IOR(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_retrieve_ctl));
    buffer_retrieve_ctl.stacking_en                 = value;
    buffer_retrieve_ctl.cross_chip_high_priority_en = 0;

    for (excp_idx = 0; excp_idx < 8; excp_idx++)
    {
        /*Reset ingress layer2/layer3 span*/
        CTC_BIT_SET(buffer_retrieve_ctl.exception_reset_header31_0, excp_idx);

        /*Reset egress layer2/layer3 span*/
        CTC_BIT_SET(buffer_retrieve_ctl.exception_reset_header63_32, excp_idx);
    }
    cmd = DRV_IOW(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_retrieve_ctl));

    /*Epe Head Adjust Ctl init*/
    sal_memset(&epe_hdr_adjust_ctl, 0, sizeof(epe_hdr_adjust_ctl_t));
    cmd = DRV_IOR(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl));
    epe_hdr_adjust_ctl.packet_header_bypass_all = 1;

    {
        epe_hdr_adjust_ctl.packet_header_edit_ingress31_0 = 0xFFFFFFFF;
        epe_hdr_adjust_ctl.packet_header_edit_ingress63_32 = 0xFFFFFFFF;
    }

    cmd = DRV_IOW(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl));

    /*Epe Nexthop Ctl init*/
    sal_memset(&epe_next_hop_ctl, 0, sizeof(epe_next_hop_ctl_t));
    cmd = DRV_IOR(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_next_hop_ctl));
    epe_next_hop_ctl.stacking_en = value;
    cmd = DRV_IOW(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_next_hop_ctl));

    /*Epe Packet Process Ctl init*/
    sal_memset(&epe_pkt_proc_ctl, 0, sizeof(epe_pkt_proc_ctl_t));
    cmd = DRV_IOR(EpePktProcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_proc_ctl));
    epe_pkt_proc_ctl.stacking_en = value;
    cmd = DRV_IOW(EpePktProcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_proc_ctl));

    /*Epe Header Edit Ctl init*/
    sal_memset(&epe_header_edit_ctl, 0, sizeof(epe_header_edit_ctl_t));
    cmd = DRV_IOR(EpeHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_header_edit_ctl));
    epe_header_edit_ctl.stacking_en = value;
    cmd = DRV_IOW(EpeHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_header_edit_ctl));

    /*Epe Packet Header Ctl init*/
    sal_memset(&epe_pkt_hdr_ctl, 0, sizeof(epe_pkt_hdr_ctl_t));
    cmd = DRV_IOR(EpePktHdrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_hdr_ctl));
    epe_pkt_hdr_ctl.header_udp_en = 0;
    cmd = DRV_IOW(EpePktHdrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_hdr_ctl));

    /*Enqueue Config*/
    sal_memset(&ds_queue_num_gen_ctl, 0, sizeof(ds_queue_num_gen_ctl));
    cmd = DRV_IOW(DsQueueNumGenCtl_t, DRV_ENTRY_FLAG);
    ds_queue_num_gen_ctl.queue_gen_mode = 1;
    ds_queue_num_gen_ctl.sgmac_en = 1;
    ds_queue_num_gen_ctl.sgmac_mask = 0x3F;
    ds_queue_num_gen_ctl.queue_select_shift = 3;  /*one channel 8 queues*/
    /*ucast*/
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_queue_num_gen_ctl));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, (1 << 7), cmd, &ds_queue_num_gen_ctl));
    /*mcast*/
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, (1 << 6 | 0xF), cmd, &ds_queue_num_gen_ctl));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, (1 << 7 | 1 << 6 | 0xF), cmd, &ds_queue_num_gen_ctl));

    sal_memset(&ds_queue_num_gen_ctl, 0, sizeof(ds_queue_num_gen_ctl));
    cmd = DRV_IOW(DsQueueNumGenCtl_t, DRV_ENTRY_FLAG);
    ds_queue_num_gen_ctl.queue_gen_mode = 1;
    ds_queue_num_gen_ctl.dest_queue_en  = 1;
    ds_queue_num_gen_ctl.dest_queue_mask = 0xFFFF;
    ds_queue_num_gen_ctl.queue_select_shift = 3;  /*one channel 8 queues*/
    /*ucast*/
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, (1 << 7 | 1 << 4), cmd, &ds_queue_num_gen_ctl));
    /*mcast*/
     /*CTC_ERROR_RETURN(DRV_IOCTL(lchip, (1<<6 | 0xF) , cmd, &ds_queue_num_gen_ctl));*/
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, (1 << 7 | 1 << 6 | 1 << 4 | 0xF), cmd, &ds_queue_num_gen_ctl));

    return ret;
}


int32
_sys_greatbelt_stacking_encap_header_enable(uint8 lchip, uint16 gport, ctc_stacking_hdr_encap_t* p_encap)
{
    uint8 chan_id = 0;
    uint8 lport   = 0;
    uint32 cmd  = 0;
    uint8 mux_type = 0;
    uint32 field_val = 0;
    uint8 out_encap_type = 0;
    ds_packet_header_edit_tunnel_t  ds_packet_header_edit_tunnel;

    CTC_PTR_VALID_CHECK(p_encap);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == chan_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

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

    sal_memset(&ds_packet_header_edit_tunnel, 0, sizeof(ds_packet_header_edit_tunnel_t));

    if (SYS_STK_MUX_TYPE_HDR_WITH_L2 == mux_type ||
        SYS_STK_MUX_TYPE_HDR_WITH_L2_AND_IPV4 == mux_type ||
        SYS_STK_MUX_TYPE_HDR_WITH_L2_AND_IPV6 == mux_type)
    {
        ds_packet_header_edit_tunnel.packet_header_l2_en = 1;
        ds_packet_header_edit_tunnel.vlan_id_valid = p_encap->vlan_valid;

        if (p_encap->vlan_valid)
        {
            ds_packet_header_edit_tunnel.vlan_id = p_encap->vlan_id;
        }

        ds_packet_header_edit_tunnel.mac_da47_32 = (p_encap->mac_da[0] << 8) |
            (p_encap->mac_da[1] << 0);
        ds_packet_header_edit_tunnel.mac_da31_0 = (p_encap->mac_da[2] << 24) |
            (p_encap->mac_da[3] << 16) |
            (p_encap->mac_da[4] << 8) |
            (p_encap->mac_da[5] << 0);

        ds_packet_header_edit_tunnel.mac_sa47_32 = (p_encap->mac_sa[0] << 8) |
            (p_encap->mac_sa[1] << 0);
        ds_packet_header_edit_tunnel.mac_sa31_0 = (p_encap->mac_sa[2] << 24) |
            (p_encap->mac_sa[3] << 16) |
            (p_encap->mac_sa[4] << 8) |
            (p_encap->mac_sa[5] << 0);

    }

    ds_packet_header_edit_tunnel.packet_header_l3_type = out_encap_type;

    if (SYS_STK_OUT_ENCAP_IPV4 == out_encap_type)
    {
        ds_packet_header_edit_tunnel.ip_da31_0 = p_encap->ipda.ipv4;
    }

    if (SYS_STK_OUT_ENCAP_IPV6 == out_encap_type)
    {
        uint32 ipv6_da[CTC_IPV6_ADDR_LEN];
        sal_memcpy(ipv6_da, p_encap->ipda.ipv6, sizeof(ipv6_addr_t));
        ds_packet_header_edit_tunnel.ip_da31_0   = ipv6_da[3];
        ds_packet_header_edit_tunnel.ip_da63_32  = ipv6_da[2];
        ds_packet_header_edit_tunnel.ip_da95_64  = ipv6_da[1];
        ds_packet_header_edit_tunnel.ip_da127_96 = ipv6_da[0];
    }

    /*mux type*/
    cmd = DRV_IOW(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_MuxType0_f + chan_id);
    field_val = mux_type;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(DsPacketHeaderEditTunnel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &ds_packet_header_edit_tunnel));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_stacking_encap_header_disable(uint8 lchip, uint16 gport)
{
    uint8 chan_id = 0;
    uint8 lport   = 0;
    uint32 cmd  = 0;
    uint8 mux_type = 0;
    uint32 field_val = 0;
    ds_packet_header_edit_tunnel_t  ds_packet_header_edit_tunnel;

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == chan_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    mux_type = SYS_STK_MUX_TYPE_HDR_REGULAR_PORT;

    /*mux type*/
    cmd = DRV_IOW(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_MuxType0_f + chan_id);
    field_val = mux_type;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sal_memset(&ds_packet_header_edit_tunnel, 0, sizeof(ds_packet_header_edit_tunnel_t));
    cmd = DRV_IOW(DsPacketHeaderEditTunnel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &ds_packet_header_edit_tunnel))

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stacking_get_port_ref_cnt(uint8 lchip, uint32 gport, uint8* port_ref_cnt, uint16 excp_trunk_id)
{
    uint16 trunk_id = 0;
    uint8 i = 0;
    uint8 ref_cnt = 0;
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;

    for (trunk_id = SYS_STK_TRUNK_MIN_ID; trunk_id <= SYS_STK_TRUNK_MAX_ID; trunk_id++)
    {
        if(excp_trunk_id != 0xFFFF && (excp_trunk_id == trunk_id))
        {
          continue;
        }
        p_sys_trunk = ctc_vector_get(g_gb_stacking_master[lchip]->p_trunk_vec, trunk_id);
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
_sys_greatbelt_stacking_is_trunk_have_member(uint8 lchip)
{
    epe_hdr_adjust_ctl_t epe_hdr_adjust_ctl;
    uint32 cmd = 0;

    sal_memset(&epe_hdr_adjust_ctl, 0, sizeof(epe_hdr_adjust_ctl_t));
    cmd = DRV_IOR(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl));
    if (epe_hdr_adjust_ctl.packet_header_en31_0 || epe_hdr_adjust_ctl.packet_header_en63_32)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}


STATIC int32
_sys_greatbelt_stacking_trunk_alloc_mem_base(uint8 lchip,
                                         sys_stacking_trunk_info_t* p_sys_trunk,
                                         uint16 max_mem_cnt)
{
    uint32 value_32 = 0;
    uint16 mem_base  = 0;
    sys_greatbelt_opf_t  opf;

    switch(g_gb_stacking_master[lchip]->trunk_mode)
    {
    case 0:
        p_sys_trunk->max_mem_cnt = 8;
        break;

    case 1:
        p_sys_trunk->max_mem_cnt = 32;
        break;

    case 2:
        p_sys_trunk->max_mem_cnt = max_mem_cnt;
        break;

    default:
        return CTC_E_INVALID_PARAM;

    }

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type  = g_gb_stacking_master[lchip]->opf_type;
    opf.pool_index = SYS_STK_OPF_MEM_BASE;
    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, p_sys_trunk->max_mem_cnt, &value_32));
    mem_base = value_32;

    p_sys_trunk->mem_base = mem_base;
    SYS_STK_DBG_INFO("_sys_greatbelt_stacking_trunk_alloc_mem_base :%d\n",mem_base);

    return CTC_E_NONE;

}


STATIC int32
_sys_greatbelt_stacking_trunk_free_mem_base(uint8 lchip,
                                        sys_stacking_trunk_info_t* p_sys_trunk)
{
    uint32 value_32 = 0;
    sys_greatbelt_opf_t  opf;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type  = g_gb_stacking_master[lchip]->opf_type;
    opf.pool_index = SYS_STK_OPF_MEM_BASE;
    value_32 = p_sys_trunk->mem_base;
    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, p_sys_trunk->max_mem_cnt, value_32));


    SYS_STK_DBG_INFO("_sys_greatbelt_stacking_trunk_free_mem_base :%d\n",p_sys_trunk->mem_base);

    return CTC_E_NONE;

}



STATIC int32
_sys_greatbelt_stacking_all_mcast_add_default_profile(void* array_data, uint32 group_id, void* user_data)
{
    uint8 lchip                    = 0;
    uint32 cmd                     = 0;
    uint32 cur_met_offset          = 0;
    uint32 next_met_offset         = 0;

    lchip = *((uint8*) user_data);
    cur_met_offset  = group_id;
    next_met_offset = cur_met_offset;

    do
    {
        cur_met_offset = next_met_offset;

        cmd = DRV_IOR(DsMetEntry_t, DsMetEntry_NextMetEntryPtr_f);
        CTC_ERROR_RETURN( DRV_IOCTL(lchip, cur_met_offset, cmd, &next_met_offset));

        if (next_met_offset == g_gb_stacking_master[lchip]->stacking_mcast_offset)
        {
            return CTC_E_NONE;
        }

    }
    while (next_met_offset != SYS_NH_INVALID_OFFSET);

    next_met_offset = g_gb_stacking_master[lchip]->stacking_mcast_offset;
    cmd = DRV_IOW(DsMetEntry_t, DsMetEntry_NextMetEntryPtr_f);
    CTC_ERROR_RETURN( DRV_IOCTL(lchip, cur_met_offset, cmd, &next_met_offset));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stacking_all_mcast_remove_default_profile(void* array_data, uint32 group_id, void* user_data)
{
    uint8 lchip                    = 0;
    uint32 cmd                     = 0;
    uint32 cur_met_offset          = 0;
    uint32 next_met_offset         = 0;

    lchip = *((uint8*) user_data);
    cur_met_offset  = group_id;
    next_met_offset = cur_met_offset;

    do
    {
        cur_met_offset = next_met_offset;

        cmd = DRV_IOR(DsMetEntry_t, DsMetEntry_NextMetEntryPtr_f);
        CTC_ERROR_RETURN( DRV_IOCTL(lchip, cur_met_offset, cmd, &next_met_offset));


        if (next_met_offset == g_gb_stacking_master[lchip]->stacking_mcast_offset)
        {
            break;
        }

    }
    while (next_met_offset != SYS_NH_INVALID_OFFSET);


    if (next_met_offset !=  g_gb_stacking_master[lchip]->stacking_mcast_offset)
    {
        return CTC_E_NONE;
    }

    next_met_offset = SYS_NH_INVALID_OFFSET;
    cmd = DRV_IOW(DsMetEntry_t, DsMetEntry_NextMetEntryPtr_f);
    CTC_ERROR_RETURN( DRV_IOCTL(lchip, cur_met_offset, cmd, &next_met_offset));

    return CTC_E_NONE;
}


int32
sys_greatbelt_stacking_all_mcast_bind_en(uint8 lchip, uint8 enable)
{
    vector_traversal_fn2 fn = NULL;

    SYS_STK_INIT_CHECK(lchip);

    if (enable)
    {
        fn = _sys_greatbelt_stacking_all_mcast_add_default_profile;
    }
    else
    {
        fn = _sys_greatbelt_stacking_all_mcast_remove_default_profile;
    }

    sys_greatbelt_nh_traverse_mcast_db( lchip,  fn,  &lchip);

    g_gb_stacking_master[lchip]->bind_mcast_en = enable?1:0;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stacking_check_port_status(uint8 lchip, uint32 gport)
{
    uint32 depth                   = 0;
    uint32 index                   = 0;

    /*check queue flush clear*/
    sys_greatbelt_queue_get_port_depth(lchip, gport, &depth);
    while (depth)
    {
        sal_task_sleep(20);
        if ((index++) > 50)
        {
            SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "the queue depth(%d) not Zero \n", depth);
            return CTC_E_HW_TIME_OUT;
        }
        sys_greatbelt_queue_get_port_depth(lchip, gport, &depth);
    }
    sal_task_sleep(20);
    return CTC_E_NONE;
}

int32
sys_greatbelt_stacking_create_trunk(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    uint32 cmd = 0;
    uint8 load_mode = 0;
    int32 ret       = CTC_E_NONE;
    uint8 trunk_id  = 0;
    ds_link_aggregate_sgmac_group_t ds_link_aggregate_sgmac_group;
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

    if ((CTC_STACKING_TRUNK_MODE_0 == g_gb_stacking_master[lchip]->trunk_mode)
        && (CTC_STK_LOAD_DYNAMIC == p_trunk->load_mode))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_STACKING_TRUNK_MODE_2 == g_gb_stacking_master[lchip]->trunk_mode)
    {
        if (CTC_STK_LOAD_DYNAMIC == p_trunk->load_mode)
        {
            p_trunk->max_mem_cnt = 32;
        }
        else
        {
            /*Spec support max 15 members , so api limit up to 15 members*/
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

    p_sys_trunk = ctc_vector_get(g_gb_stacking_master[lchip]->p_trunk_vec, trunk_id);
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
    p_sys_trunk->lchip = lchip;
    p_sys_trunk->trunk_id = trunk_id;
    p_sys_trunk->mode = p_trunk->load_mode;
    p_sys_trunk->flag = p_trunk->flag;

    /*Alloc member base*/
    CTC_ERROR_GOTO(_sys_greatbelt_stacking_trunk_alloc_mem_base(lchip, p_sys_trunk, p_trunk->max_mem_cnt),
                   ret, Error0);

    if (0 == g_gb_stacking_master[lchip]->mcast_mode)
    {
        CTC_ERROR_GOTO(sys_greatbelt_stacking_mcast_group_add_member(lchip,
                                                                      g_gb_stacking_master[lchip]->p_default_prof_db,
                                                                      p_sys_trunk->trunk_id), ret, Error1);
    }

    p_sys_trunk->mem_cnt = 0;
    ctc_vector_add(g_gb_stacking_master[lchip]->p_trunk_vec, trunk_id, p_sys_trunk);

    sal_memset(&ds_link_aggregate_sgmac_group, 0, sizeof(ds_link_aggregate_sgmac_group));
    cmd = DRV_IOW(DsLinkAggregateSgmacGroup_t, DRV_ENTRY_FLAG);
    ds_link_aggregate_sgmac_group.sgmac_link_agg_mem_base = p_sys_trunk->mem_base;
    load_mode = (CTC_STK_LOAD_DYNAMIC == p_trunk->load_mode) ? 1 : 0;
    ds_link_aggregate_sgmac_group.sgmac_link_agg_remap_en = load_mode;
    ds_link_aggregate_sgmac_group.sgmac_link_agg_mem_num  = 0;

    if (load_mode)
    {
        ds_link_aggregate_sgmac_group.sgmac_link_agg_member_ptr  = trunk_id;
        ds_link_aggregate_sgmac_group.sgmac_link_agg_flow_num  = 3;
    }
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, trunk_id, cmd, &ds_link_aggregate_sgmac_group), ret, Error2);

    sal_memcpy(&p_sys_trunk->encap_hdr, &p_trunk->encap_hdr, sizeof(ctc_stacking_hdr_encap_t));

    /* flush all DsLinkAggregateSgmacMemberSet_t to reverve channel for dlb mode */
    if (CTC_STK_LOAD_DYNAMIC == p_trunk->load_mode)
    {
       CTC_ERROR_GOTO(_sys_greatbelt_stacking_reverve_member(lchip, trunk_id), ret, Error2);
    }

    g_gb_stacking_master[lchip]->trunk_num++;

    STACKING_UNLOCK;
    return CTC_E_NONE;


    Error2:
    if (0 == g_gb_stacking_master[lchip]->mcast_mode)
    {
        sys_greatbelt_stacking_mcast_group_remove_member(lchip,
                                                         g_gb_stacking_master[lchip]->p_default_prof_db,
                                                         p_sys_trunk->trunk_id);
    }
    ctc_vector_del(g_gb_stacking_master[lchip]->p_trunk_vec, trunk_id);

    Error1:
    _sys_greatbelt_stacking_trunk_free_mem_base(lchip, p_sys_trunk);

    Error0:
    mem_free(p_sys_trunk);
    STACKING_UNLOCK;
    return ret;
}

int32
sys_greatbelt_stacking_destroy_trunk(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    uint32 cmd = 0;
    uint8 trunk_id  = 0;
    uint8 mem_cnt   = 0;
    uint8 mem_idx   = 0;
    uint16 gport    = 0;
    uint8  lport    = 0;
    uint32  field_val = 0;
    epe_hdr_adjust_ctl_t epe_hdr_adjust_ctl;
    buf_retrv_stacking_en_t buf_retrv_stacking_en;

    sys_stacking_trunk_info_t* p_sys_trunk = NULL;
    ds_link_aggregate_sgmac_group_t ds_link_aggregate_sgmac_group;
    uint8 port_ref_cnt = 0;
    int32 ret = CTC_E_NONE;

    SYS_STK_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_trunk);
    trunk_id = p_trunk->trunk_id;
    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    SYS_STK_EXTEND_EN(p_trunk->extend);

    STACKING_LOCK;

    p_sys_trunk = ctc_vector_get(g_gb_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }

    sal_memset(&ds_link_aggregate_sgmac_group, 0, sizeof(ds_link_aggregate_sgmac_group));
    cmd = DRV_IOW(DsLinkAggregateSgmacGroup_t, DRV_ENTRY_FLAG);
    ds_link_aggregate_sgmac_group.sgmac_link_agg_mem_base = p_sys_trunk->mem_base;
    ds_link_aggregate_sgmac_group.sgmac_link_agg_remap_en = 0;
    ds_link_aggregate_sgmac_group.sgmac_link_agg_mem_num  = 0;
    ds_link_aggregate_sgmac_group.sgmac_link_agg_flow_num = 0;

    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, trunk_id, cmd, &ds_link_aggregate_sgmac_group),
            g_gb_stacking_master[lchip]->p_stacking_mutex);

    mem_cnt = p_sys_trunk->mem_cnt;

    for (mem_idx = 0; mem_idx < mem_cnt; mem_idx++)
    {
        gport = p_sys_trunk->mem_ports[mem_idx];

        _sys_greatbelt_stacking_get_port_ref_cnt(lchip, gport, &port_ref_cnt, 0xFFFF);
        if (port_ref_cnt > 1)
        {
            continue;
        }

        /*enq drop*/
        sys_greatbelt_queue_set_port_drop_en(lchip, gport, TRUE, SYS_QUEUE_DROP_TYPE_PROFILE);
        ret = _sys_greatbelt_stacking_check_port_status(lchip, gport);
        if (ret < 0)
        {
            /*restore buffer*/
            sys_greatbelt_queue_set_port_drop_en(lchip, gport, FALSE, SYS_QUEUE_DROP_TYPE_PROFILE);
            STACKING_UNLOCK;
            return ret;
        }

        /*Cancel SrcSgmac and Chan mapping*/
        lport = CTC_MAP_GPORT_TO_LPORT(gport);
        if (0 == g_gb_stacking_master[lchip]->binding_trunk)
        {
            field_val = 0;
            cmd = DRV_IOW(DsSrcSgmacGroup_t, DsSrcSgmacGroup_SgmacGroupId_f);
            DRV_IOCTL(lchip, lport, cmd, &field_val);
        }

        /*recovery edit packet on port*/
        field_val = 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_CtagOperationDisable_f);
        DRV_IOCTL(lchip, lport, cmd, &field_val);

        cmd = DRV_IOW(DsDestPort_t, DsDestPort_StagOperationDisable_f);
        DRV_IOCTL(lchip, lport, cmd, &field_val);

        field_val = 1;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_UntagDefaultVlanId_f);
        DRV_IOCTL(lchip, lport, cmd, &field_val);

        /*Ecap Output packet Heaer disable*/
        _sys_greatbelt_stacking_encap_header_disable(lchip, gport);

        sal_memset(&epe_hdr_adjust_ctl, 0, sizeof(epe_hdr_adjust_ctl_t));
        cmd = DRV_IOR(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl);

        sal_memset(&buf_retrv_stacking_en, 0, sizeof(buf_retrv_stacking_en_t));
        cmd = DRV_IOR(BufRetrvStackingEn_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &buf_retrv_stacking_en);

        if (lport >= 32)
        {
            CTC_BIT_UNSET(epe_hdr_adjust_ctl.packet_header_en63_32, (lport - 32));
            CTC_BIT_UNSET(buf_retrv_stacking_en.stacking_en63_to32, (lport - 32));
        }
        else
        {
            CTC_BIT_UNSET(epe_hdr_adjust_ctl.packet_header_en31_0, lport);
            CTC_BIT_UNSET(buf_retrv_stacking_en.stacking_en31_to0, lport);
        }

        cmd = DRV_IOW(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl);

        cmd = DRV_IOW(BufRetrvStackingEn_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &buf_retrv_stacking_en);

        /*restore buffer*/
        sys_greatbelt_queue_set_port_drop_en(lchip, gport, FALSE, SYS_QUEUE_DROP_TYPE_PROFILE);

        /*revory queue alloc*/
        if (sys_greatbelt_queue_get_enqueue_mode(lchip))
        {
            CTC_ERROR_RETURN_WITH_UNLOCK(sys_greatbelt_port_queue_add_for_stacking(lchip, SYS_QUEUE_TYPE_NORMAL, lport, 0),
                                         g_gb_stacking_master[lchip]->p_stacking_mutex);
        }
        else
        {
        CTC_ERROR_RETURN_WITH_UNLOCK(sys_greatbelt_port_queue_add(lchip, SYS_QUEUE_TYPE_NORMAL, lport, 0),
            g_gb_stacking_master[lchip]->p_stacking_mutex);
        }

    }
    if (0 == g_gb_stacking_master[lchip]->mcast_mode)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(sys_greatbelt_stacking_mcast_group_remove_member(lchip,
                                                                                    g_gb_stacking_master[lchip]->p_default_prof_db,
                                                                                    p_sys_trunk->trunk_id),
                                                                      g_gb_stacking_master[lchip]->p_stacking_mutex);
    }

    CTC_ERROR_RETURN_WITH_UNLOCK(_sys_greatbelt_stacking_trunk_free_mem_base(lchip, p_sys_trunk),g_gb_stacking_master[lchip]->p_stacking_mutex);

    ctc_vector_del(g_gb_stacking_master[lchip]->p_trunk_vec, trunk_id);
    mem_free(p_sys_trunk);
    g_gb_stacking_master[lchip]->trunk_num--;

    if ((1 == g_gb_stacking_master[lchip]->bind_mcast_en)
        && (0 == _sys_greatbelt_stacking_is_trunk_have_member(lchip)))
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(sys_greatbelt_stacking_all_mcast_bind_en(lchip, 0), g_gb_stacking_master[lchip]->p_stacking_mutex);
    }

    STACKING_UNLOCK;

    return CTC_E_NONE;
}

STATIC bool
_sys_greatelt_stacking_find_trunk_port(sys_stacking_trunk_info_t* p_sys_trunk, uint16 gport, uint32* index)
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
sys_greatbelt_stacking_add_trunk_port(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    uint32 cmd       = 0;
    uint8 trunk_id   = 0;
    uint16 gport     = 0;
    uint8 lport      = 0;
    uint32 index     = 0;
    uint32 mem_index = 0;
    uint8 chan_id    = 0;
    uint32 field_val = 0;
    uint8 max_mem_num = 0;
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;
    ds_link_aggregate_sgmac_group_t ds_link_aggregate_sgmac_group;
    ds_link_aggregate_sgmac_member_t ds_link_aggregate_sgmac_member;
    epe_hdr_adjust_ctl_t     epe_hdr_adjust_ctl;
    buf_retrv_stacking_en_t buf_retrv_stacking_en;
    ds_link_aggregate_channel_t agg_chan;
    int32 ret = CTC_E_NONE;

    SYS_STK_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_trunk);
    trunk_id = p_trunk->trunk_id;
    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    gport    = p_trunk->gport;
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    SYS_STK_LPORT_CHECK(lport);

    SYS_STK_EXTEND_EN(p_trunk->extend);

    STACKING_LOCK;

    p_sys_trunk = ctc_vector_get(g_gb_stacking_master[lchip]->p_trunk_vec, trunk_id);
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
    if (TRUE == _sys_greatelt_stacking_find_trunk_port(p_sys_trunk, gport, &mem_index))
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
        max_mem_num =  p_sys_trunk->max_mem_cnt;
    }
    if (p_sys_trunk->mem_cnt >= max_mem_num)
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_EXCEED_MAX_MEMBER_PORT;
    }

    SYS_STK_DBG_INFO("Add uplink lport :%d\n", lport);

    /* SrcSgmac and Chan mapping*/
    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == chan_id)
    {
        STACKING_UNLOCK;
        return CTC_E_INVALID_LOCAL_PORT;
    }

    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, chan_id, cmd, &agg_chan), ret, error);

    agg_chan.link_aggregation_group = trunk_id;

    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, chan_id, cmd, &agg_chan), ret, error);

    if (CTC_STK_LOAD_DYNAMIC == p_sys_trunk->mode)
    {
        CTC_ERROR_GOTO(_sys_greatbelt_stacking_dlb_add_member(lchip, trunk_id, p_sys_trunk->mem_cnt, chan_id),ret, error);
    }
    else if (CTC_STK_LOAD_STATIC_FAILOVER == p_sys_trunk->mode)
    {
        CTC_ERROR_GOTO(_sys_greatbelt_stacking_failover_add_member(lchip, trunk_id, gport), ret, error);
    }

    if(CTC_FLAG_ISSET(p_sys_trunk->flag, CTC_STACKING_TRUNK_MEM_ASCEND_ORDER))
    {
        uint8 temp_mem_idx = p_sys_trunk->mem_cnt;
        ds_link_aggregate_sgmac_member_t  hw_member;
        uint32 cmdr = DRV_IOR(DsLinkAggregateSgmacMember_t, DRV_ENTRY_FLAG);
        uint32 cmdw = DRV_IOW(DsLinkAggregateSgmacMember_t, DRV_ENTRY_FLAG);
        for(; temp_mem_idx > 0; temp_mem_idx--)
        {
            if(lport >= CTC_MAP_GPORT_TO_LPORT(p_sys_trunk->mem_ports[temp_mem_idx-1]))
            {
                break;
            }
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, temp_mem_idx+p_sys_trunk->mem_base-1, cmdr, &hw_member), g_gb_stacking_master[lchip]->p_stacking_mutex);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, temp_mem_idx+p_sys_trunk->mem_base, cmdw, &hw_member), g_gb_stacking_master[lchip]->p_stacking_mutex);

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
        cmd = DRV_IOW(DsLinkAggregateSgmacMember_t, DRV_ENTRY_FLAG);

        ds_link_aggregate_sgmac_member.sgmac = chan_id;

        CTC_ERROR_GOTO(DRV_IOCTL(lchip, index+p_sys_trunk->mem_base, cmd, &ds_link_aggregate_sgmac_member), ret, error);
    }

    /*update trunk members*/
    p_sys_trunk->mem_ports[index] = gport;
    p_sys_trunk->mem_cnt++;

    if ((0 == g_gb_stacking_master[lchip]->bind_mcast_en))
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(sys_greatbelt_stacking_all_mcast_bind_en(lchip, 1), g_gb_stacking_master[lchip]->p_stacking_mutex);
    }

    if ((p_sys_trunk->mode == CTC_STK_LOAD_DYNAMIC) && (1 == p_sys_trunk->mem_cnt))
    {
       CTC_ERROR_RETURN_WITH_UNLOCK(_sys_greatbelt_stacking_clear_flow_active(lchip, trunk_id), g_gb_stacking_master[lchip]->p_stacking_mutex);
    }

    sal_memset(&ds_link_aggregate_sgmac_group, 0, sizeof(ds_link_aggregate_sgmac_group_t));
    cmd = DRV_IOR(DsLinkAggregateSgmacGroup_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, trunk_id, cmd, &ds_link_aggregate_sgmac_group), ret, error);

    ds_link_aggregate_sgmac_group.sgmac_link_agg_mem_num = p_sys_trunk->mem_cnt;
    if (1 == p_sys_trunk->mem_cnt)
    {
        SYS_STK_DBG_INFO("First member port\n");
        ds_link_aggregate_sgmac_group.sgmac_id = chan_id;
    }

    cmd = DRV_IOW(DsLinkAggregateSgmacGroup_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, trunk_id, cmd, &ds_link_aggregate_sgmac_group), ret, error);

    /*enq drop*/
    sys_greatbelt_queue_set_port_drop_en(lchip, gport, TRUE, SYS_QUEUE_DROP_TYPE_PROFILE);
    CTC_ERROR_GOTO(_sys_greatbelt_stacking_check_port_status(lchip, gport), ret, error);

    if (0 == g_gb_stacking_master[lchip]->binding_trunk)
    {
    field_val = trunk_id;
    cmd = DRV_IOW(DsSrcSgmacGroup_t, DsSrcSgmacGroup_SgmacGroupId_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, chan_id, cmd, &field_val), ret, error);
    }

    /*set not edit packet on stacking port*/
    field_val = 1;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_CtagOperationDisable_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport, cmd, &field_val), ret, error);
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_StagOperationDisable_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport, cmd, &field_val), ret, error);
    field_val = 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_UntagDefaultVlanId_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport, cmd, &field_val), ret, error);

    /*Ecap Output packet Heaer enable*/
    CTC_ERROR_GOTO(_sys_greatbelt_stacking_encap_header_enable(lchip, gport, &p_sys_trunk->encap_hdr), ret, error);

    sal_memset(&epe_hdr_adjust_ctl, 0, sizeof(epe_hdr_adjust_ctl_t));
    cmd = DRV_IOR(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl), ret, error);

    cmd = DRV_IOR(BufRetrvStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &buf_retrv_stacking_en), ret, error);

    if (chan_id >= 32)
    {
        CTC_BIT_SET(epe_hdr_adjust_ctl.packet_header_en63_32, (chan_id - 32));
        CTC_BIT_SET(buf_retrv_stacking_en.stacking_en63_to32, (chan_id - 32));
    }
    else
    {
        CTC_BIT_SET(epe_hdr_adjust_ctl.packet_header_en31_0, chan_id);
        CTC_BIT_SET(buf_retrv_stacking_en.stacking_en31_to0, chan_id);
    }

    cmd = DRV_IOW(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl), ret, error);

    cmd = DRV_IOW(BufRetrvStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &buf_retrv_stacking_en), ret, error);

    /*restore buffer*/
    sys_greatbelt_queue_set_port_drop_en(lchip, gport, FALSE, SYS_QUEUE_DROP_TYPE_PROFILE);

    /*Add Queue alloc*/
    if (sys_greatbelt_queue_get_enqueue_mode(lchip))
    {
        ret = sys_greatbelt_port_queue_add_for_stacking(lchip, SYS_QUEUE_TYPE_STACKING, lport, 0);
    }
    else
    {
        ret = sys_greatbelt_port_queue_add(lchip, SYS_QUEUE_TYPE_STACKING, lport, 0);
    }

    STACKING_UNLOCK;
    return ret;

error:
    sys_greatbelt_queue_set_port_drop_en(lchip, gport, FALSE, SYS_QUEUE_DROP_TYPE_PROFILE);
    STACKING_UNLOCK;
    return ret;

}

int32
sys_greatbelt_stacking_remove_trunk_port(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    uint32 cmd = 0;
    uint8 trunk_id   = 0;
    uint16 gport     = 0;
    uint8 lport     = 0;
    uint32 mem_index = 0;
    uint32 index = 0;
    uint16 last_gport = 0;
    uint32 field_val = 0;
    uint8 chan_id    = 0;
    sys_stacking_trunk_info_t* p_sys_trunk;
    ds_link_aggregate_sgmac_member_t ds_link_aggregate_sgmac_member;
    epe_hdr_adjust_ctl_t     epe_hdr_adjust_ctl;
    buf_retrv_stacking_en_t buf_retrv_stacking_en;
    uint8 port_ref_cnt = 0;
    int32 ret = CTC_E_NONE;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trunk);

    trunk_id = p_trunk->trunk_id;
    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);
    gport    = p_trunk->gport;
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    SYS_STK_LPORT_CHECK(lport);

    SYS_STK_EXTEND_EN(p_trunk->extend);

    STACKING_LOCK;
    p_sys_trunk = ctc_vector_get(g_gb_stacking_master[lchip]->p_trunk_vec, trunk_id);
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
    if (FALSE == _sys_greatelt_stacking_find_trunk_port(p_sys_trunk, gport, &mem_index))
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_MEMBER_PORT_NOT_EXIST;
    }

    SYS_STK_DBG_INFO("Remove member port %d, mem_index = %d\n", lport, mem_index);

    _sys_greatbelt_stacking_get_port_ref_cnt(lchip, gport, &port_ref_cnt, 0xFFFF);

    if (mem_index < (p_sys_trunk->mem_cnt - 1))
    {   /*Need replace this member using last member*/
        sal_memset(&ds_link_aggregate_sgmac_member, 0, sizeof(ds_link_aggregate_sgmac_member));
        if (CTC_FLAG_ISSET(p_sys_trunk->flag, CTC_STACKING_TRUNK_MEM_ASCEND_ORDER))
        {
            uint8 tmp_index = mem_index;
            ds_link_aggregate_sgmac_member_t  hw_member;
            uint32 cmdr = DRV_IOR(DsLinkAggregateSgmacMember_t, DRV_ENTRY_FLAG);
            uint32 cmdw = DRV_IOW(DsLinkAggregateSgmacMember_t, DRV_ENTRY_FLAG);

            for(;tmp_index < p_sys_trunk->mem_cnt-1; tmp_index++)
            {
                CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, tmp_index+p_sys_trunk->mem_base+1, cmdr, &hw_member), g_gb_stacking_master[lchip]->p_stacking_mutex);
                CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, tmp_index+p_sys_trunk->mem_base, cmdw, &hw_member), g_gb_stacking_master[lchip]->p_stacking_mutex);
                p_sys_trunk->mem_ports[tmp_index] = p_sys_trunk->mem_ports[tmp_index+1];
            }
        }
        else
        {
            last_gport = p_sys_trunk->mem_ports[p_sys_trunk->mem_cnt - 1];
            p_sys_trunk->mem_ports[mem_index] = last_gport;

            if (CTC_STK_LOAD_DYNAMIC != p_sys_trunk->mode)
            {
                field_val = SYS_GET_CHANNEL_ID(lchip, last_gport);
                if (0xFF == field_val)
                {
                    STACKING_UNLOCK;
                    return CTC_E_INVALID_LOCAL_PORT;
                }
                ds_link_aggregate_sgmac_member.sgmac = field_val;

                SYS_STK_DBG_INFO("Need replace this member using last member :%d\n", last_gport);

                index = p_sys_trunk->mem_base + mem_index;
                cmd = DRV_IOW(DsLinkAggregateSgmacMember_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, index, cmd, &ds_link_aggregate_sgmac_member),
                                             g_gb_stacking_master[lchip]->p_stacking_mutex);
            }
        }
    }

    /*update trunk members*/
    p_sys_trunk->mem_cnt--;

    if (1 == p_sys_trunk->mem_cnt)
    {
        field_val = SYS_GET_CHANNEL_ID(lchip, p_sys_trunk->mem_ports[0]);
        if (0xFF == field_val)
        {
            STACKING_UNLOCK;
            return CTC_E_INVALID_LOCAL_PORT;
        }
        SYS_STK_DBG_INFO("Only have last member port %d\n", field_val);
        cmd = DRV_IOW(DsLinkAggregateSgmacGroup_t, DsLinkAggregateSgmacGroup_SgmacId_f);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, trunk_id, cmd, &field_val),
            g_gb_stacking_master[lchip]->p_stacking_mutex);
    }

    field_val = p_sys_trunk->mem_cnt;
    cmd = DRV_IOW(DsLinkAggregateSgmacGroup_t, DsLinkAggregateSgmacGroup_SgmacLinkAggMemNum_f);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, trunk_id, cmd, &field_val),
            g_gb_stacking_master[lchip]->p_stacking_mutex);

    if (CTC_STK_LOAD_DYNAMIC == p_sys_trunk->mode)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(_sys_greatbelt_stacking_dlb_del_member(lchip, trunk_id, mem_index, p_sys_trunk->mem_cnt, chan_id), g_gb_stacking_master[lchip]->p_stacking_mutex);
    }
    else if (CTC_STK_LOAD_STATIC_FAILOVER == p_sys_trunk->mode)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(_sys_greatbelt_stacking_failover_del_member(lchip, trunk_id, gport), g_gb_stacking_master[lchip]->p_stacking_mutex);
    }

    /*Cancel SrcSgmac and Chan mapping*/
    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == chan_id)
    {
        STACKING_UNLOCK;
        return CTC_E_INVALID_LOCAL_PORT;
    }

    if (port_ref_cnt > 1)/*the members is still in other trunk, must not disable stacking port*/
    {
        STACKING_UNLOCK;
        return CTC_E_NONE;
    }

    /*enq drop*/
    sys_greatbelt_queue_set_port_drop_en(lchip, gport, TRUE, SYS_QUEUE_DROP_TYPE_PROFILE);
    CTC_ERROR_GOTO(_sys_greatbelt_stacking_check_port_status(lchip, gport), ret, end);

    if (0 == g_gb_stacking_master[lchip]->binding_trunk)
    {
        field_val = 0;
        cmd = DRV_IOW(DsSrcSgmacGroup_t, DsSrcSgmacGroup_SgmacGroupId_f);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, chan_id, cmd, &field_val), ret, end);
    }

    /*recovery edit packet on port*/
    field_val = 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_CtagOperationDisable_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport, cmd, &field_val), ret, end);
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_StagOperationDisable_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport, cmd, &field_val), ret, end);
    field_val = 1;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_UntagDefaultVlanId_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport, cmd, &field_val), ret, end);

    /*Ecap Output packet Heaer disable*/
    CTC_ERROR_GOTO(_sys_greatbelt_stacking_encap_header_disable(lchip, gport), ret, end);

    /*Ecap Output packet Heaer disable*/
    sal_memset(&epe_hdr_adjust_ctl, 0, sizeof(epe_hdr_adjust_ctl_t));
    cmd = DRV_IOR(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl), ret, end);

    sal_memset(&buf_retrv_stacking_en, 0, sizeof(buf_retrv_stacking_en_t));
    cmd = DRV_IOR(BufRetrvStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &buf_retrv_stacking_en), ret, end);

    if (chan_id >= 32)
    {
        CTC_BIT_UNSET(epe_hdr_adjust_ctl.packet_header_en63_32, (chan_id - 32));
        CTC_BIT_UNSET(buf_retrv_stacking_en.stacking_en63_to32, (chan_id - 32));
    }
    else
    {
        CTC_BIT_UNSET(epe_hdr_adjust_ctl.packet_header_en31_0, chan_id);
        CTC_BIT_UNSET(buf_retrv_stacking_en.stacking_en31_to0, chan_id);
    }

    cmd = DRV_IOW(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl), ret, end);

    cmd = DRV_IOW(BufRetrvStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &buf_retrv_stacking_en), ret, end);

    sys_greatbelt_queue_set_port_drop_en(lchip, gport, FALSE, SYS_QUEUE_DROP_TYPE_PROFILE);


    /*Recovery Queue alloc*/
    if (sys_greatbelt_queue_get_enqueue_mode(lchip))
    {
        ret = sys_greatbelt_port_queue_add_for_stacking(lchip, SYS_QUEUE_TYPE_NORMAL, lport, 0);
    }
    else
    {
        ret = sys_greatbelt_port_queue_add(lchip, SYS_QUEUE_TYPE_NORMAL, lport, 0);
    }

if ((1 == g_gb_stacking_master[lchip]->bind_mcast_en)
        && (0 == _sys_greatbelt_stacking_is_trunk_have_member(lchip)))
    {
        ret = ret ? ret : sys_greatbelt_stacking_all_mcast_bind_en(lchip, 0);
    }

    STACKING_UNLOCK;
    return ret;
end:
    sys_greatbelt_queue_set_port_drop_en(lchip, gport, FALSE, SYS_QUEUE_DROP_TYPE_PROFILE);
    STACKING_UNLOCK;
    return ret;
}

STATIC int32
_sys_greatbelt_stacking_set_stacking_port_en(uint8 lchip, sys_stacking_trunk_info_t* p_trunk, uint32 gport, uint8 is_add)
{
    epe_hdr_adjust_ctl_t     epe_hdr_adjust_ctl;
    buf_retrv_stacking_en_t buf_retrv_stacking_en;

    uint32 field_val;
    uint32 cmd;
    uint16 chan_id;
    uint16 lport;

    SYS_STK_DBG_FUNC();

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    SYS_STK_LPORT_CHECK(lport);
    /* SrcSgmac and Chan mapping*/
    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == chan_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    /*set not edit packet on stacking port*/
    field_val = is_add ? 1:0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_CtagOperationDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_StagOperationDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = is_add ? 0:1;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_UntagDefaultVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    /*2. Ecap Output packet Header enable*/
    if (is_add)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_stacking_encap_header_enable(lchip, gport, &(p_trunk->encap_hdr)));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_stacking_encap_header_disable(lchip, gport));
    }


    sal_memset(&epe_hdr_adjust_ctl, 0, sizeof(epe_hdr_adjust_ctl_t));
    cmd = DRV_IOR(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl));
    cmd = DRV_IOR(BufRetrvStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_retrv_stacking_en));

    if(is_add)
    {
        if (chan_id >= 32)
        {
            CTC_BIT_SET(epe_hdr_adjust_ctl.packet_header_en63_32, (chan_id - 32));
            CTC_BIT_SET(buf_retrv_stacking_en.stacking_en63_to32, (chan_id - 32));
        }
        else
        {
            CTC_BIT_SET(epe_hdr_adjust_ctl.packet_header_en31_0, chan_id);
            CTC_BIT_SET(buf_retrv_stacking_en.stacking_en31_to0, chan_id);
        }
    }
    else
    {
        if (chan_id >= 32)
        {
            CTC_BIT_UNSET(epe_hdr_adjust_ctl.packet_header_en63_32, (chan_id - 32));
            CTC_BIT_UNSET(buf_retrv_stacking_en.stacking_en63_to32, (chan_id - 32));
        }
        else
        {
            CTC_BIT_UNSET(epe_hdr_adjust_ctl.packet_header_en31_0, chan_id);
            CTC_BIT_UNSET(buf_retrv_stacking_en.stacking_en31_to0, chan_id);
        }
    }

    cmd = DRV_IOW(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl));
    cmd = DRV_IOW(BufRetrvStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_retrv_stacking_en));

    if (0 == g_gb_stacking_master[lchip]->binding_trunk)
    {
        field_val = is_add ? p_trunk->trunk_id :0;
        cmd = DRV_IOW(DsSrcSgmacGroup_t, DsSrcSgmacGroup_SgmacGroupId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_stacking_trunk_change_port(uint8 lchip, sys_stacking_trunk_info_t* p_sys_trunk,
                                           uint32 gport, uint8 enable)
{
    int32 ret = 0;
    uint8 port_ref_cnt = 0;
    uint8 lport = 0;
    uint8 lchip2 = 0;

     SYS_MAP_GPORT_TO_LPORT(gport, lchip2, lport);

    _sys_greatbelt_stacking_get_port_ref_cnt(lchip, gport, &port_ref_cnt,p_sys_trunk->trunk_id);

    if (port_ref_cnt > 0)
    {
        return CTC_E_NONE;
    }

    sys_greatbelt_queue_set_port_drop_en(lchip, gport, TRUE, SYS_QUEUE_DROP_TYPE_PROFILE);

    CTC_ERROR_GOTO(_sys_greatbelt_stacking_check_port_status(lchip, gport), ret, Error0);

    CTC_ERROR_GOTO(_sys_greatbelt_stacking_set_stacking_port_en(lchip, p_sys_trunk, gport, enable), ret, Error0);

    /*Recovery Queue alloc*/
    if (sys_greatbelt_queue_get_enqueue_mode(lchip))
    {
        CTC_ERROR_GOTO(sys_greatbelt_port_queue_add_for_stacking(lchip, (enable)? SYS_QUEUE_TYPE_STACKING:SYS_QUEUE_TYPE_NORMAL, lport, 0), ret, Error1);
    }
    else
    {
        CTC_ERROR_GOTO(sys_greatbelt_port_queue_add(lchip, (enable)? SYS_QUEUE_TYPE_STACKING:SYS_QUEUE_TYPE_NORMAL, lport, 0),ret, Error1);
    }

    sys_greatbelt_queue_set_port_drop_en(lchip, gport, FALSE, SYS_QUEUE_DROP_TYPE_PROFILE);

   return CTC_E_NONE;


   Error1:
   _sys_greatbelt_stacking_set_stacking_port_en(lchip, p_sys_trunk, gport, enable?0:1);

   Error0:
   sys_greatbelt_queue_set_port_drop_en(lchip, gport, FALSE, SYS_QUEUE_DROP_TYPE_PROFILE);

    return ret;

}
STATIC int32
_sys_greatbelt_stacking_find_no_dupicated(uint8 lchip, uint32* new_members, uint8 new_cnt,
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
sys_greatbelt_stacking_replace_trunk_ports(uint8 lchip, uint8 trunk_id, uint32* gports, uint8 mem_ports)
{
    int32  ret = 0;
    uint32 cmd;
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;
    uint32 fld_val = 0;
    uint8  loop = 0;
    uint8  loop_add = 0;
    uint8  loop_del = 0;
    uint8  add_ports_num;
    uint8  del_ports_num;
    uint32 add_ports[SYS_STK_TRUNK_MAX_MEMBERS] = {0};
    uint32 del_ports[SYS_STK_TRUNK_MAX_MEMBERS] = {0};
    uint16 chan_id = 0;

    SYS_STK_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(gports);
    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    SYS_STK_DBG_FUNC();
    STACKING_LOCK;
    p_sys_trunk = ctc_vector_get(g_gb_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }

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
    CTC_ERROR_GOTO(_sys_greatbelt_stacking_find_no_dupicated(lchip, gports, mem_ports, p_sys_trunk->mem_ports, \
                                                             p_sys_trunk->mem_cnt, add_ports, &add_ports_num), ret, error_proc0);

    CTC_ERROR_GOTO(_sys_greatbelt_stacking_find_no_dupicated(lchip, p_sys_trunk->mem_ports, p_sys_trunk->mem_cnt, \
                                                             gports, mem_ports, del_ports, &del_ports_num), ret, error_proc0);


    /*3. set add ports, change normal port to stacking port */
    for (loop_add = 0; loop_add < add_ports_num; loop_add++)
    {
       CTC_ERROR_GOTO(_sys_greatbelt_stacking_trunk_change_port(lchip,  p_sys_trunk, add_ports[loop_add], 1), ret, error_proc);
    }

    /*4. set delete ports, change stacking port to normal port*/
    for (loop_del = 0; loop_del < del_ports_num; loop_del++)
    {
       CTC_ERROR_GOTO(_sys_greatbelt_stacking_trunk_change_port(lchip,  p_sys_trunk, del_ports[loop_del], 0), ret, error_proc);
    }

    /*5. replace per port,don't need rollback !!!!!!!!!!!!!!*/
    for (loop = 0; loop < mem_ports; loop++)
    {
       chan_id = SYS_GET_CHANNEL_ID(lchip,  gports[loop]);
       fld_val = chan_id;
       cmd = DRV_IOW(DsLinkAggregateSgmacMember_t, DsLinkAggregateSgmacMember_Sgmac_f);
       CTC_ERROR_GOTO(DRV_IOCTL(lchip, p_sys_trunk->mem_base + loop, cmd, &fld_val), ret, error_proc);

       fld_val = p_sys_trunk->trunk_id;
       cmd = DRV_IOW(DsLinkAggregateChannel_t, DsLinkAggregateChannel_LinkAggregationGroup_f);
       CTC_ERROR_GOTO(DRV_IOCTL(lchip, chan_id, cmd, &fld_val), ret, error_proc);
    }

    fld_val = mem_ports;
    cmd = DRV_IOW(DsLinkAggregateSgmacGroup_t, DsLinkAggregateSgmacGroup_SgmacLinkAggMemNum_f);
    DRV_IOCTL(lchip, trunk_id, cmd, &fld_val) ;
    if (mem_ports == 1)
    {
       fld_val = gports[0];
       cmd = DRV_IOW(DsLinkAggregateSgmacGroup_t, DsLinkAggregateSgmacGroup_SgmacId_f);
       DRV_IOCTL(lchip, trunk_id, cmd, &fld_val) ;
    }

    if (!g_gb_stacking_master[lchip]->bind_mcast_en && mem_ports)
    {
       sys_greatbelt_stacking_all_mcast_bind_en(lchip, 1);
    }

    if ((1 == g_gb_stacking_master[lchip]->bind_mcast_en)
       && (0 == _sys_greatbelt_stacking_is_trunk_have_member(lchip)))
    {
       sys_greatbelt_stacking_all_mcast_bind_en(lchip, 0) ;
    }

    /*update softtable*/
    p_sys_trunk->mem_cnt = mem_ports;
    if (mem_ports)
    {
       sal_memcpy(p_sys_trunk->mem_ports, gports, mem_ports*sizeof(uint32));
    }
    p_sys_trunk->replace_en = 1;

    STACKING_UNLOCK;
    return ret;

error_proc:
    for (loop = 0; loop < loop_add; loop++)
    {
        _sys_greatbelt_stacking_trunk_change_port(lchip,  p_sys_trunk, add_ports[loop], 0);
    }

    for (loop = 0; loop < loop_del; loop++)
    {
        _sys_greatbelt_stacking_trunk_change_port(lchip,  p_sys_trunk, del_ports[loop], 1);
    }

error_proc0:
    STACKING_UNLOCK;
    return ret;
}
int32
sys_greatbelt_stacking_get_member_ports(uint8 lchip, uint8 trunk_id, uint32* p_gports, uint8* cnt)
{
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_gports);
    CTC_PTR_VALID_CHECK(cnt);

    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    STACKING_LOCK;

    p_sys_trunk = ctc_vector_get(g_gb_stacking_master[lchip]->p_trunk_vec, trunk_id);
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
sys_greatbelt_stacking_add_trunk_rchip(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    uint32 cmd = 0;
    uint8 trunk_id   = 0;
    uint8 rgchip     = 0;
    uint32 index = 0;
    uint32 field_val = 0;
    sys_stacking_trunk_info_t* p_sys_trunk;

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
        if (sys_greatbelt_chip_is_local(lchip, rgchip))
        {
            return CTC_E_NONE;
        }
    }

    index = rgchip;
    cmd = DRV_IOR(DsSgmacMap_t, DsSgmacMap_SgmacGroupId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
    p_sys_trunk = ctc_vector_get(g_gb_stacking_master[lchip]->p_trunk_vec, field_val);
    if(p_sys_trunk)
    {
         CTC_BIT_UNSET(p_sys_trunk->rgchip_bitmap, rgchip);
    }

    p_sys_trunk = ctc_vector_get(g_gb_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }

    CTC_BIT_SET(p_sys_trunk->rgchip_bitmap, rgchip);

    index = rgchip;
    field_val = trunk_id;
    cmd = DRV_IOW(DsSgmacMap_t, DsSgmacMap_SgmacGroupId_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

    /*Record the remote chip from the local trunk*/
    g_gb_stacking_master[lchip]->neigbor_chip[rgchip] = trunk_id;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stacking_remove_trunk_rchip(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    uint32 cmd = 0;
    uint8 trunk_id   = 0;
    uint8 rgchip     = 0;
    uint32 index = 0;
    uint32 field_val = 0;
    sys_stacking_trunk_info_t* p_sys_trunk;

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
        if (sys_greatbelt_chip_is_local(lchip, rgchip))
        {
            return CTC_E_NONE;
        }
    }
    p_sys_trunk = ctc_vector_get(g_gb_stacking_master[lchip]->p_trunk_vec, trunk_id);
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
    cmd = DRV_IOW(DsSgmacMap_t, DsSgmacMap_SgmacGroupId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

    g_gb_stacking_master[lchip]->neigbor_chip[rgchip] = 0;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stacking_get_trunk_rchip(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trunk);

    SYS_GLOBAL_CHIPID_CHECK(p_trunk->remote_gchip);

    cmd = DRV_IOR(DsSgmacMap_t, DsSgmacMap_SgmacGroupId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_trunk->remote_gchip, cmd, &field_val));

    p_sys_trunk = ctc_vector_get(g_gb_stacking_master[lchip]->p_trunk_vec, field_val);
    if (NULL == p_sys_trunk)
    {
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }

    p_trunk->trunk_id = field_val;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stacking_set_stop_rchip(uint8 lchip, ctc_stacking_stop_rchip_t *p_stop_rchip)
{
    uint32 cmd       = 0;
    uint32 field_val = 0;

    field_val = p_stop_rchip->rchip_bitmap;
    cmd = DRV_IOW(MetFifoCtl_t, MetFifoCtl_EndRemoteChip_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_stacking_get_stop_rchip(uint8 lchip, ctc_stacking_stop_rchip_t *p_stop_rchip)
{
    uint32 cmd       = 0;
    uint32 field_val = 0;

    cmd = DRV_IOR(MetFifoCtl_t, MetFifoCtl_EndRemoteChip_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    p_stop_rchip->rchip_bitmap = field_val;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stacking_set_break_en(uint8 lchip, uint32 enable)
{
    uint32 cmd       = 0;
    uint32 field_val = 0;

    field_val = enable;
    cmd = DRV_IOW(MetFifoCtl_t, MetFifoCtl_StackingBroken_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stacking_get_break_en(uint8 lchip, uint32* enable)
{
    uint32 cmd       = 0;
    uint32 field_val = 0;

    cmd = DRV_IOR(MetFifoCtl_t, MetFifoCtl_StackingBroken_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    *enable = field_val;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stacking_set_full_mesh_en(uint8 lchip, uint32 enable)
{
    uint32 cmd       = 0;
    uint32 field_val = 0;

    field_val = enable ? 0 : 1;
    cmd = DRV_IOW(MetFifoCtl_t, MetFifoCtl_ForceReplicationFromFabric_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_stacking_get_full_mesh_en(uint8 lchip, uint32* enable)
{
    uint32 cmd       = 0;
    uint32 field_val = 0;

    cmd = DRV_IOR(MetFifoCtl_t, MetFifoCtl_ForceReplicationFromFabric_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    *enable = field_val ? 0 : 1;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stacking_set_global_cfg(uint8 lchip, ctc_stacking_glb_cfg_t* p_glb_cfg)
{

    CTC_ERROR_RETURN(_sys_greatbelt_stacking_hdr_set(lchip, &p_glb_cfg->hdr_glb));
    return CTC_E_NONE;
}

int32
sys_greatbelt_stacking_get_global_cfg(uint8 lchip, ctc_stacking_glb_cfg_t* p_glb_cfg)
{
    CTC_ERROR_RETURN(_sys_greatbelt_stacking_hdr_get(lchip, &p_glb_cfg->hdr_glb));
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stacking_set_break_truck_id(uint8 lchip, ctc_stacking_mcast_break_point_t *p_break_point)
{
    uint32 cmd       = 0;
    met_fifo_ctl_t met_fifo_ctl;
    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_break_point);
    SYS_STK_TRUNKID_RANGE_CHECK(p_break_point->trunk_id)

    sal_memset(&met_fifo_ctl, 0 , sizeof(met_fifo_ctl_t));

    cmd = DRV_IOR(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &met_fifo_ctl));


    if(p_break_point->enable)
    {
        if (p_break_point->trunk_id < 32)
        {
            CTC_BIT_SET(met_fifo_ctl.sgmac_discard_source_replication31_0, p_break_point->trunk_id);
        }
        else if(p_break_point->trunk_id < 64)
        {
            CTC_BIT_SET(met_fifo_ctl.sgmac_discard_source_replication63_32, (p_break_point->trunk_id - 32));
        }
    }
    else
    {
        if (p_break_point->trunk_id < 32)
        {
            CTC_BIT_UNSET(met_fifo_ctl.sgmac_discard_source_replication31_0, p_break_point->trunk_id);
        }
        else if(p_break_point->trunk_id < 64)
        {
            CTC_BIT_UNSET(met_fifo_ctl.sgmac_discard_source_replication63_32, (p_break_point->trunk_id - 32));
        }
    }

    cmd = DRV_IOW(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &met_fifo_ctl));

    return CTC_E_NONE;

}

int32
sys_greatbelt_stacking_set_rchip_port(uint8 lchip, ctc_stacking_rchip_port_t * p_rchip_port)
{
    uint16 loop1                   = 0;
    uint16 loop2                   = 0;
    uint16 lport                   = 0;
    uint32 gport                   = 0;
    uint8 gchip_id                 = 0;
    uint16 valid_cnt               = 0;
    uint16 rsv_num                 = 0;
    uint32 max_port_num_slice[2]   = {0};
    uint8  slice_en                = 0;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_rchip_port);
    SYS_GLOBAL_CHIPID_CHECK(p_rchip_port->rchip);

    sys_greatbelt_get_gchip_id(lchip, &gchip_id);


    for (loop1 = 0; loop1 < sizeof(ctc_port_bitmap_t) / sizeof(uint32); loop1++)
    {
        for (loop2 = 0; loop2 < CTC_UINT32_BITS; loop2++)
        {
            lport = loop1 * CTC_UINT32_BITS + loop2;

            gport = CTC_MAP_LPORT_TO_GPORT(p_rchip_port->rchip, lport);

            if (CTC_IS_BIT_SET(p_rchip_port->pbm[loop1], loop2))
            {
                max_port_num_slice[lport/256] = lport % 256 + 1;
                valid_cnt++;
                sys_greatbelt_brguc_nh_create(lchip, gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC);
            }
            else
            {
                sys_greatbelt_brguc_nh_delete(lchip, gport);
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

    CTC_ERROR_RETURN(sys_greatbelt_l2_free_rchip_ad_index(lchip, p_rchip_port->rchip));
    if (valid_cnt)
    {
        CTC_ERROR_RETURN(sys_greatbelt_l2_reserve_rchip_ad_index(lchip, p_rchip_port->rchip, rsv_num, slice_en));
    }

    return CTC_E_NONE;

}
int32
sys_greatbelt_stacking_binding_trunk(uint8 lchip, uint32 gport, uint8 trunk_id)/*trunk_id = 0 means unbinding*/
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

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xff == chan_id)
    {
        return CTC_E_INVALID_PORT;
    }
    field_val = trunk_id;
    cmd = DRV_IOW(DsSrcSgmacGroup_t, DsSrcSgmacGroup_SgmacGroupId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
    g_gb_stacking_master[lchip]->binding_trunk = 1; /*change to 1 once call by user*/

    return CTC_E_NONE;
}



int32
sys_greatbelt_stacking_get_binding_trunk(uint8 lchip, uint32 gport, uint8* trunk_id)/*trunk_id = 0 means unbinding*/
{
    uint32 field_val = 0;
    uint8 chan_id = 0;
    uint32 cmd = 0;
	uint32 lport = 0;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(trunk_id);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xff == chan_id)
    {
        return CTC_E_INVALID_PORT;
    }

    cmd = DRV_IOR(DsSrcSgmacGroup_t, DsSrcSgmacGroup_SgmacGroupId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
	*trunk_id = field_val;

    return CTC_E_NONE;
}



int32
sys_greatbelt_stacking_set_property(uint8 lchip, ctc_stacking_property_t* p_prop)
{
    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_prop);

    switch (p_prop->prop_type)
    {

    case CTC_STK_PROP_GLOBAL_CONFIG:
        {
            ctc_stacking_glb_cfg_t* p_glb_cfg = NULL;
            p_glb_cfg = (ctc_stacking_glb_cfg_t *)p_prop->p_value;
            CTC_ERROR_RETURN(sys_greatbelt_stacking_set_global_cfg(lchip, p_glb_cfg));
        }
        break;

    case CTC_STK_PROP_MCAST_STOP_RCHIP:
        {
            ctc_stacking_stop_rchip_t* p_stop_rchip = NULL;
            p_stop_rchip = (ctc_stacking_stop_rchip_t *)p_prop->p_value;
            CTC_ERROR_RETURN(_sys_greatbelt_stacking_set_stop_rchip(lchip, p_stop_rchip));
        }
        break;

    case CTC_STK_PROP_BREAK_EN:
        CTC_ERROR_RETURN(_sys_greatbelt_stacking_set_break_en(lchip, p_prop->value));
        break;

    case CTC_STK_PROP_FULL_MESH_EN:
        CTC_ERROR_RETURN(_sys_greatbelt_stacking_set_full_mesh_en(lchip, p_prop->value));
        break;

    case CTC_STK_PROP_MCAST_BREAK_POINT:
        {
            ctc_stacking_mcast_break_point_t *p_break_point = NULL;
            p_break_point = (ctc_stacking_mcast_break_point_t *)p_prop->p_value;
            CTC_ERROR_RETURN(_sys_greatbelt_stacking_set_break_truck_id(lchip, p_break_point));
        }
        break;

    case CTC_STK_PROP_PORT_BIND_TRUNK:
        {
            ctc_stacking_bind_trunk_t *p_bind_trunk = NULL;
            p_bind_trunk = (ctc_stacking_bind_trunk_t *)p_prop->p_value;
			CTC_PTR_VALID_CHECK(p_bind_trunk);
            CTC_ERROR_RETURN(sys_greatbelt_stacking_binding_trunk(lchip, p_bind_trunk->gport, p_bind_trunk->trunk_id));
        }
        break;

    case CTC_STK_PROP_RCHIP_PORT_EN:
        {
            ctc_stacking_rchip_port_t *p_rchip_port = NULL;
            p_rchip_port = (ctc_stacking_rchip_port_t *)p_prop->p_value;
            CTC_ERROR_RETURN(sys_greatbelt_stacking_set_rchip_port(lchip, p_rchip_port));
        }
        break;

    default:
        return CTC_E_NONE;
    }

    return CTC_E_NONE;

}

int32
sys_greatbelt_stacking_get_property(uint8 lchip, ctc_stacking_property_t* p_prop)
{
    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_prop);

    switch (p_prop->prop_type)
    {

    case CTC_STK_PROP_GLOBAL_CONFIG:
        CTC_ERROR_RETURN(sys_greatbelt_stacking_get_global_cfg(lchip, (ctc_stacking_glb_cfg_t *)p_prop->p_value));
        break;

    case CTC_STK_PROP_MCAST_STOP_RCHIP:
        CTC_ERROR_RETURN(_sys_greatbelt_stacking_get_stop_rchip(lchip, (ctc_stacking_stop_rchip_t *)p_prop->p_value));
        break;

    case CTC_STK_PROP_BREAK_EN:
        CTC_ERROR_RETURN(_sys_greatbelt_stacking_get_break_en(lchip, &p_prop->value));
        break;

    case CTC_STK_PROP_FULL_MESH_EN:
        CTC_ERROR_RETURN(_sys_greatbelt_stacking_get_full_mesh_en(lchip, &p_prop->value));
        break;


    case CTC_STK_PROP_PORT_BIND_TRUNK:
        {
            ctc_stacking_bind_trunk_t *p_bind_trunk = NULL;
            p_bind_trunk = (ctc_stacking_bind_trunk_t *)p_prop->p_value;
			CTC_PTR_VALID_CHECK(p_bind_trunk);
            CTC_ERROR_RETURN(sys_greatbelt_stacking_get_binding_trunk(lchip, p_bind_trunk->gport, &p_bind_trunk->trunk_id));
        }
        break;

    default:
        return CTC_E_NONE;
    }

    return CTC_E_NONE;

}




#define _____MCAST_TRUNK_GROUP_____ ""



int32
sys_greatbelt_stacking_set_trunk_mcast_profile(uint8 lchip, ctc_stacking_trunk_mcast_profile_t* p_mcast_profile)
{
    int32 ret = CTC_E_NONE;
    sys_stacking_mcast_db_t mcast_db;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;

    SYS_STK_INIT_CHECK(lchip);

    if (p_mcast_profile->mcast_profile_id >= 256)
    {
        return CTC_E_INVALID_PARAM;
    }

    STACKING_LOCK;

    /*lookup*/
    sal_memset(&mcast_db, 0, sizeof(mcast_db));

    mcast_db.type = 0;
    mcast_db.id   = p_mcast_profile->mcast_profile_id;

    p_mcast_db = ctc_hash_lookup(g_gb_stacking_master[lchip]->mcast_hash, &mcast_db);

    switch(p_mcast_profile->type)
    {
    case CTC_STK_MCAST_PROFILE_CREATE:
        {
            uint16 mcast_profile_id = 0;

            if (0 == p_mcast_profile->mcast_profile_id)
            {
                sys_greatbelt_opf_t  opf;
                /*alloc profile id*/
                uint32 value_32 = 0;

                sal_memset(&opf, 0, sizeof(opf));
                opf.pool_type  = g_gb_stacking_master[lchip]->opf_type;
                opf.pool_index = SYS_STK_OPF_MCAST_PROFILE;
                CTC_ERROR_GOTO(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &value_32),
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

            CTC_ERROR_GOTO(sys_greatbelt_stacking_mcast_group_create(lchip, 0,
                                                             mcast_profile_id,
                                                             &p_mcast_db,
                                                             p_mcast_profile->append_en),
                                                             ret, Error);

            if (CTC_E_NONE != ret) /*roolback*/
            {
                sys_greatbelt_opf_t  opf;
                sal_memset(&opf, 0, sizeof(opf));
                opf.pool_type  = g_gb_stacking_master[lchip]->opf_type;
                opf.pool_index = SYS_STK_OPF_MCAST_PROFILE;
                sys_greatbelt_opf_free_offset(lchip, &opf, 1, mcast_profile_id);
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
            CTC_ERROR_GOTO(sys_greatbelt_stacking_mcast_group_destroy(lchip, 0,
                                                            p_mcast_profile->mcast_profile_id, p_mcast_db),
                                                            ret, Error);

            if ( 1 == p_mcast_db->alloc_id)
            {
                /*alloc profile id*/
                sys_greatbelt_opf_t  opf;

                sal_memset(&opf, 0, sizeof(opf));
                opf.pool_type  = g_gb_stacking_master[lchip]->opf_type;
                opf.pool_index = SYS_STK_OPF_MCAST_PROFILE;
                CTC_ERROR_GOTO(sys_greatbelt_opf_free_offset(lchip, &opf, 1, p_mcast_profile->mcast_profile_id),
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

                ret =   sys_greatbelt_stacking_mcast_group_add_member(lchip, p_mcast_db, trunk_id);

                if (CTC_E_NONE != ret) /*roolback*/
                {
                    uint8 tmp_trunk_id = 0;

                    for (tmp_trunk_id = 1; tmp_trunk_id < trunk_id; tmp_trunk_id++)
                    {
                        if (!CTC_IS_BIT_SET(bitmap[tmp_trunk_id / 32], tmp_trunk_id % 32))
                        {
                            continue;
                        }
                        sys_greatbelt_stacking_mcast_group_remove_member(lchip, p_mcast_db, tmp_trunk_id);
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
                ret = sys_greatbelt_stacking_mcast_group_remove_member(lchip, p_mcast_db, trunk_id);

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
sys_greatbelt_stacking_get_trunk_mcast_profile(uint8 lchip, ctc_stacking_trunk_mcast_profile_t* p_mcast_profile)
{
    sys_stacking_mcast_db_t mcast_db;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;
    uint16 trunk_id_hw             = 0;
    uint32 cmd                     = 0;
    uint32 cur_met_offset          = 0;
    uint32 next_met_offset         = 0;
    ds_met_entry_t dsmet;

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
    p_mcast_db = ctc_hash_lookup(g_gb_stacking_master[lchip]->mcast_hash, &mcast_db);
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

        cmd = DRV_IOR(DsMetEntry_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN( DRV_IOCTL(lchip, cur_met_offset, cmd, &dsmet));

        trunk_id_hw = ((dsmet.ucast_id_low>>5)&0x3F);

        next_met_offset = dsmet.next_met_entry_ptr;

        SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "cur:%d, trunk_id:%d\n ",  cur_met_offset, trunk_id_hw);

    }
    while (next_met_offset != SYS_NH_INVALID_OFFSET);


    p_mcast_profile->trunk_bitmap[0] = p_mcast_db->trunk_bitmap[0];
    p_mcast_profile->trunk_bitmap[1] = p_mcast_db->trunk_bitmap[1];

    return CTC_E_NONE;
}


int32
sys_greatbelt_stacking_create_keeplive_group(uint8 lchip, uint16 group_id)
{
    int32 ret = CTC_E_NONE;
    uint32 group_size = 0;
    sys_stacking_mcast_db_t mcast_db;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;


    SYS_STK_INIT_CHECK(lchip);

    SYS_STK_DBG_PARAM("create group:0x%x\n", group_id);

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_dyn_entry_size(lchip, SYS_FTM_DYN_ENTRY_GLB_MET, &group_size));

    if (group_id >= group_size)
    {
        return CTC_E_INVALID_PARAM;
    }

    STACKING_LOCK;


    /*lookup*/
    sal_memset(&mcast_db, 0, sizeof(mcast_db));

    mcast_db.type = 1;
    mcast_db.id   = group_id;
    p_mcast_db = ctc_hash_lookup(g_gb_stacking_master[lchip]->mcast_hash, &mcast_db);

    if (NULL != p_mcast_db)
    {
        ret =  CTC_E_ENTRY_EXIST;
                goto Error;
    }

    CTC_ERROR_GOTO(sys_greatbelt_stacking_mcast_group_create( lchip,  1, group_id, &p_mcast_db, 0),
                   ret, Error);

    STACKING_UNLOCK;
    return CTC_E_NONE;

    Error:
    STACKING_UNLOCK;
    return ret;


}


int32
sys_greatbelt_stacking_destroy_keeplive_group(uint8 lchip, uint16 group_id)
{
    int32 ret = CTC_E_NONE;
    uint32 group_size = 0;
    sys_stacking_mcast_db_t mcast_db;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;

    SYS_STK_INIT_CHECK(lchip);

    SYS_STK_DBG_PARAM("destroy group:0x%x\n", group_id);

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_dyn_entry_size(lchip, SYS_FTM_DYN_ENTRY_GLB_MET, &group_size));

    if (group_id >= group_size)
    {
        return CTC_E_INVALID_PARAM;
    }


    STACKING_LOCK;


    /*lookup*/
    sal_memset(&mcast_db, 0, sizeof(mcast_db));

    mcast_db.type = 1;
    mcast_db.id   = group_id;
    p_mcast_db = ctc_hash_lookup(g_gb_stacking_master[lchip]->mcast_hash, &mcast_db);

    if (NULL == p_mcast_db)
    {
        ret =  CTC_E_ENTRY_NOT_EXIST;
                goto Error;
    }

    CTC_ERROR_GOTO(sys_greatbelt_stacking_mcast_group_destroy( lchip,  1, group_id, p_mcast_db),
                   ret, Error);

    STACKING_UNLOCK;
    return CTC_E_NONE;

    Error:
    STACKING_UNLOCK;
    return ret;


}


int32
sys_greatbelt_stacking_add_keeplive_member(uint8 lchip, ctc_stacking_keeplive_t* p_keeplive)
{
    int32 ret = CTC_E_NONE;
    sys_stacking_mcast_db_t mcast_db;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_keeplive);


    SYS_STK_DBG_PARAM("trunk:0x%x, gport:0x%x, type:%d, group:0x%x\n",
                      p_keeplive->trunk_id, p_keeplive->cpu_gport, p_keeplive->mem_type, p_keeplive->group_id);



    if (p_keeplive->mem_type == CTC_STK_KEEPLIVE_MEMBER_TRUNK)
    {
        SYS_STK_TRUNKID_RANGE_CHECK(p_keeplive->trunk_id);
    }

    STACKING_LOCK;


    /*lookup*/
    sal_memset(&mcast_db, 0, sizeof(mcast_db));

    mcast_db.type = 1;
    mcast_db.id   = p_keeplive->group_id;
    p_mcast_db = ctc_hash_lookup(g_gb_stacking_master[lchip]->mcast_hash, &mcast_db);

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
            ret =  CTC_E_ENTRY_EXIST;
            goto Error;
        }

        CTC_ERROR_GOTO(sys_greatbelt_stacking_mcast_group_add_member(lchip, p_mcast_db, trunk_id),
                       ret, Error);

        CTC_BIT_SET(p_mcast_db->trunk_bitmap[trunk_id / 32], trunk_id % 32);
    }
    else
    {
        uint16 dsnh_offset = 0;
        uint32 cmd = 0;
        ds_met_entry_t dsmet;
        dsnh_offset = CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_C2C_PKT, 0);

        cmd = DRV_IOR(DsMetEntry_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO( DRV_IOCTL(lchip, p_mcast_db->head_met_offset, cmd, &dsmet),
                       ret, Error);

        /* ucast_id = (ds_met_entry.ucast_id15_14 << 14)| (ds_met_entry.ucast_id13 << 13) | (ds_met_entry.ucast_id_low);*/
        dsmet.ucast_id_low = 0;
        dsmet.ucast_id15_14 = (SYS_QSEL_TYPE_C2C >> 1) & 3;
        dsmet.ucast_id13 = SYS_QSEL_TYPE_C2C & 1;
        dsmet.replication_ctl = (dsnh_offset << 4);

        cmd = DRV_IOW(DsMetEntry_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO( DRV_IOCTL(lchip, p_mcast_db->head_met_offset, cmd, &dsmet),
                       ret, Error);

    }
Error:
    STACKING_UNLOCK;
    return ret;
}


int32
sys_greatbelt_stacking_remove_keeplive_member(uint8 lchip, ctc_stacking_keeplive_t* p_keeplive)
{
    int32 ret = CTC_E_NONE;
    sys_stacking_mcast_db_t mcast_db;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_keeplive);


    SYS_STK_DBG_PARAM("trunk:0x%x, gport:0x%x, type:%d, group:0x%x\n",
                      p_keeplive->trunk_id, p_keeplive->cpu_gport, p_keeplive->mem_type, p_keeplive->group_id);


    if (p_keeplive->mem_type == CTC_STK_KEEPLIVE_MEMBER_TRUNK)
    {
        SYS_STK_TRUNKID_RANGE_CHECK(p_keeplive->trunk_id);
    }


    STACKING_LOCK;


    /*lookup*/
    sal_memset(&mcast_db, 0, sizeof(mcast_db));

    mcast_db.type = 1;
    mcast_db.id   = p_keeplive->group_id;
    p_mcast_db = ctc_hash_lookup(g_gb_stacking_master[lchip]->mcast_hash, &mcast_db);

    if (NULL == p_mcast_db)
    {
        ret = CTC_E_ENTRY_NOT_EXIST;
        goto Error;
    }


    if (p_keeplive->mem_type == CTC_STK_KEEPLIVE_MEMBER_TRUNK)
    {
        uint16 trunk_id = 0;

        trunk_id = p_keeplive->trunk_id;

        if (!CTC_IS_BIT_SET(p_mcast_db->trunk_bitmap[trunk_id / 32], trunk_id % 32))
        {
            ret = CTC_E_ENTRY_NOT_EXIST;
            goto Error;
        }

        CTC_ERROR_GOTO(sys_greatbelt_stacking_mcast_group_remove_member(lchip, p_mcast_db, p_keeplive->trunk_id),
                       ret, Error);
        CTC_BIT_UNSET(p_mcast_db->trunk_bitmap[trunk_id / 32], trunk_id % 32);
    }
    else
    {
        uint16 dest_id                 = 0;
        uint16 dsnh_offset             = 0;
        uint32 cmd                     = 0;
        ds_met_entry_t dsmet;

        dest_id  = SYS_RESERVE_PORT_ID_DROP;
        dsnh_offset = CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_C2C_PKT, 0);

        cmd = DRV_IOR(DsMetEntry_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO( DRV_IOCTL(lchip, p_mcast_db->head_met_offset, cmd, &dsmet),
                       ret, Error);

        dsmet.ucast_id_low = dest_id;
        dsmet.replication_ctl = (dsnh_offset << 4);

        cmd = DRV_IOW(DsMetEntry_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO( DRV_IOCTL(lchip, p_mcast_db->head_met_offset, cmd, &dsmet),
                       ret, Error);

    }

Error:
    STACKING_UNLOCK;
    return ret;

}

int32
sys_greatbelt_stacking_get_keeplive_members(uint8 lchip, ctc_stacking_keeplive_t* p_keeplive)
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
    p_mcast_db = ctc_hash_lookup(g_gb_stacking_master[lchip]->mcast_hash, &mcast_db);
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
        uint32 dsnh_offset = 0;
        ds_met_entry_t dsmet;
        uint8 gchip = 0;
        uint16 dest_id = 0;

        sal_memset(&dsmet, 0, sizeof(ds_met_entry_t));
        cmd = DRV_IOR(DsMetEntry_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, p_mcast_db->head_met_offset, cmd, &dsmet), ret, EXIT);
        dsnh_offset = dsmet.replication_ctl;
        dest_id = dsmet.ucast_id_low;

        if ((dest_id != SYS_RESERVE_PORT_ID_DROP)
            && ((dsnh_offset >> 4) == CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_C2C_PKT, 0)))
        {
            sys_greatbelt_get_gchip_id(lchip, &gchip);
            p_keeplive->cpu_gport = CTC_LPORT_CPU | ((gchip & CTC_GCHIP_MASK) << CTC_LOCAL_PORT_LENGTH);
        }
    }

EXIT:
    STACKING_UNLOCK;
    return ret;
}


int32
sys_greatbelt_stacking_get_mcast_profile_met_offset(uint8 lchip,  uint16 mcast_profile_id, uint16 *p_stacking_met_offset)
{
    sys_stacking_mcast_db_t mcast_db;
    sys_stacking_mcast_db_t *p_mcast_db = NULL;

    CTC_PTR_VALID_CHECK(p_stacking_met_offset);

    if (NULL == g_gb_stacking_master[lchip])
    {
        *p_stacking_met_offset = 0;
        return CTC_E_NONE;
    }

    if (0 == mcast_profile_id)
    {
        *p_stacking_met_offset = g_gb_stacking_master[lchip]->bind_mcast_en?
                                                                        g_gb_stacking_master[lchip]->stacking_mcast_offset : SYS_NH_INVALID_OFFSET;

        return CTC_E_NONE;
    }

    STACKING_LOCK;

    sal_memset(&mcast_db, 0, sizeof(mcast_db));

    mcast_db.type = 0;
    mcast_db.id   = mcast_profile_id;
    p_mcast_db = ctc_hash_lookup(g_gb_stacking_master[lchip]->mcast_hash, &mcast_db);


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
_sys_greatbelt_stacking_set_enable(uint8 lchip, uint8 enable)
{
    CTC_ERROR_RETURN(_sys_greatbelt_stacking_register_en(lchip, enable));
    return CTC_E_NONE;
}

int32
sys_greatbelt_stacking_get_enable(uint8 lchip, bool* enable, uint32* stacking_mcast_offset)
{
    CTC_PTR_VALID_CHECK(enable);

    if (NULL == g_gb_stacking_master[lchip])
    {
        *enable = FALSE;
        return CTC_E_NONE;
    }

    *enable = (g_gb_stacking_master[lchip]->stacking_en && g_gb_stacking_master[lchip]->bind_mcast_en) ? TRUE : FALSE;
    *stacking_mcast_offset = g_gb_stacking_master[lchip]->stacking_mcast_offset;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stacking_failover_detect(uint8 lchip, uint8 linkdown_chan)
{
    uint32 cmd = 0;
    uint8 trunk_id   = 0;
    ds_link_aggregate_channel_t linkagg_channel;
    uint16 lport = 0;
    uint16 gport = 0;
    uint8 gchip = 0;
    sys_stacking_trunk_info_t* p_sys_trunk = NULL;
    uint16 last_gport = 0;
    uint32 mem_index = 0;
    uint32 field_val = 0;

    SYS_STK_INIT_CHECK(lchip);
    SYS_STK_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "chan_down:0x%x\n ", linkdown_chan);

    lport = SYS_GET_LPORT_ID_WITH_CHAN(lchip, linkdown_chan);
    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    sys_greatbelt_get_gchip_id(lchip, &gchip);
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, linkdown_chan, cmd, &linkagg_channel));
    trunk_id = linkagg_channel.link_aggregation_group;

    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    STACKING_LOCK;

    p_sys_trunk = ctc_vector_get(g_gb_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_NOT_EXIST;
    }

    if (FALSE == _sys_greatelt_stacking_find_trunk_port(p_sys_trunk, gport, &mem_index))
    {
        STACKING_UNLOCK;
        return CTC_E_STK_TRUNK_MEMBER_PORT_NOT_EXIST;
    }

    last_gport = p_sys_trunk->mem_ports[p_sys_trunk->mem_cnt - 1];
    p_sys_trunk->mem_ports[mem_index] = last_gport;

    p_sys_trunk->mem_cnt--;

    if (1 == p_sys_trunk->mem_cnt)
    {
        field_val = SYS_GET_CHANNEL_ID(lchip, p_sys_trunk->mem_ports[0]);
        if (0xFF == field_val)
        {
            STACKING_UNLOCK;
            return CTC_E_INVALID_LOCAL_PORT;
        }

        SYS_STK_DBG_INFO("Only have last member port %d\n", field_val);
        cmd = DRV_IOW(DsLinkAggregateSgmacGroup_t, DsLinkAggregateSgmacGroup_SgmacId_f);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, trunk_id, cmd, &field_val),
            g_gb_stacking_master[lchip]->p_stacking_mutex);
    }

    STACKING_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stacking_get_stkhdr_len(uint8 lchip, uint16 gport, uint16 gchip, uint16* p_stkhdr_len)
{

    uint32 cmd = 0;
    uint32 field_val = 0;
    uint8 vlan_valid = 0;
    uint8 header_l3_offset = 0;
    uint8 packet_header_len = 0;
    uint8 mux_type = 0;
    uint8 udp_en = 0;
    uint8 chan_id = 0;
    uint8 lport = 0;
    uint8 rgchip  = 0;
    uint8 trunk_id  = 0;
    sys_stacking_trunk_info_t* p_sys_trunk;
    uint16 local_cnt = 0;
    uint32 truck_port = 0;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stkhdr_len);
    if (NULL == g_gb_stacking_master[lchip])
    {
        *p_stkhdr_len = 0;
        return CTC_E_NONE;
    }

    rgchip = gchip;

    trunk_id = g_gb_stacking_master[lchip]->neigbor_chip[rgchip];
    p_sys_trunk = ctc_vector_get(g_gb_stacking_master[lchip]->p_trunk_vec, trunk_id);
    if (NULL == p_sys_trunk)
    {
        if((CTC_IS_LINKAGG_PORT(gport)))
        {
            if (CTC_E_NONE != sys_greatbelt_linkagg_get_1st_local_port(lchip, CTC_MAP_GPORT_TO_LPORT(gport), &truck_port, &local_cnt))
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


    SYS_MAP_GPORT_TO_LPORT(truck_port, lchip, lport);

    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        packet_header_len = 0;
    }
    else
    {
        chan_id = SYS_GET_CHANNEL_ID(lchip, truck_port);
        if (0xFF == chan_id)
        {
            return CTC_E_INVALID_LOCAL_PORT;
        }

        /*mux type*/
        cmd = DRV_IOR(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_MuxType0_f + chan_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        mux_type = field_val;

        cmd = DRV_IOR(DsPacketHeaderEditTunnel_t, DsPacketHeaderEditTunnel_VlanIdValid_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
        vlan_valid = field_val;

        cmd = DRV_IOR(IpeHeaderAdjustCtl_t, IpeHeaderAdjustCtl_HeaderUdpEn_f);
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
    }

    *p_stkhdr_len = packet_header_len;

    return DRV_E_NONE;
}

int32
sys_greatbelt_stacking_show_trunk_info(uint8 lchip, uint8 trunk_id)
{
    sys_stacking_trunk_info_t* p_sys_trunk;
    uint8 idx = 0;
    uint8 cnt = 0;
    char* mode[3] = {"Static", "Dynamic", "Failover"};
    SYS_STK_INIT_CHECK(lchip);
    SYS_STK_TRUNKID_RANGE_CHECK(trunk_id);

    p_sys_trunk = ctc_vector_get(g_gb_stacking_master[lchip]->p_trunk_vec, trunk_id);
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

int32 sys_greatbelt_fabric_enable(uint8 lchip, ctc_stacking_inter_connect_glb_t* p_inter)
{
    ctc_stacking_trunk_t trunk;
    uint8 gchip0 = 0;
    uint8 gchip1 = 0;
    uint8 port_temp = 0;
    sal_memset(&trunk, 0, sizeof(ctc_stacking_trunk_t));

    sys_greatbelt_get_gchip_id(0, &gchip0);
    sys_greatbelt_get_gchip_id(1, &gchip1);

    trunk.extend.enable = 1;
    trunk.extend.gchip = gchip0;
    trunk.trunk_id = SYS_STK_FABRIC_TRUNK;
    CTC_ERROR_RETURN(sys_greatbelt_stacking_create_trunk(lchip, &trunk));
    if (p_inter->mode)
    {/*interlaken mode*/
        /*chip0 interlaken stacking enviroment*/
        trunk.gport = CTC_MAP_LPORT_TO_GPORT(gchip0, SYS_RESERVE_PORT_ID_INTLK);
        CTC_ERROR_RETURN(sys_greatbelt_stacking_add_trunk_port(lchip, &trunk));
    }
    else
    {
        for (port_temp = 0; port_temp < p_inter->member_num; port_temp++)
        {
            /*chip0 stacking enviroment*/
            trunk.gport = CTC_MAP_LPORT_TO_GPORT(gchip0, p_inter->member_port[0][port_temp]);
            CTC_ERROR_RETURN(sys_greatbelt_stacking_add_trunk_port(lchip, &trunk));
        }
    }
    trunk.remote_gchip = gchip1;
    CTC_ERROR_RETURN(sys_greatbelt_stacking_add_trunk_rchip(lchip, &trunk));

    trunk.extend.enable = 1;
    trunk.extend.gchip = gchip1;
    trunk.trunk_id = SYS_STK_FABRIC_TRUNK;
    CTC_ERROR_RETURN(sys_greatbelt_stacking_create_trunk(lchip, &trunk));
    if (p_inter->mode)
    {/*interlaken mode*/
        /*chip1 interlaken stacking enviroment*/
        trunk.gport = CTC_MAP_LPORT_TO_GPORT(gchip1, SYS_RESERVE_PORT_ID_INTLK);
        CTC_ERROR_RETURN(sys_greatbelt_stacking_add_trunk_port(lchip, &trunk));
    }
    else
    {
        for (port_temp = 0; port_temp < p_inter->member_num; port_temp++)
        {
            /*chip1 stacking enviroment*/
            //trunk.gport = CTC_MAP_LPORT_TO_GPORT(gchip1, p_inter->member_port[1][port_temp]);
            CTC_ERROR_RETURN(sys_greatbelt_stacking_add_trunk_port(lchip, &trunk));
        }
    }
    trunk.remote_gchip = gchip0;
    CTC_ERROR_RETURN(sys_greatbelt_stacking_add_trunk_rchip(lchip, &trunk));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_stacking_interlaken_en(uint8 lchip)
{
    uint32 cmd = 0;
    int_lk_lane_rx_debug_state_t rx_state;
    uint32 value = 0;
    int_lk_soft_reset_t intlk_reset;
    uint8 repeat = 0;
    uint8 need_reset = 0;
    uint32 err_cnt = 0;
    int_lk_pkt_stats_t intlk_stats;
    uint8 i = 0;
    drv_work_platform_type_t platform_type;


    CTC_ERROR_RETURN(drv_greatbelt_get_platform_type(&platform_type));

    cmd = DRV_IOW(ResetIntRelated_t, ResetIntRelated_ResetIntLkIntf_f);
    value = 1;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    value = 0;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    sys_greatbelt_chip_intlk_register_init(lchip, SYS_CHIP_INTLK_FABRIC_MODE);

    /* do intlk rx and align reset*/

    cmd = DRV_IOR(IntLkSoftReset_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));
    intlk_reset.soft_reset_lrx = 0xff;
    intlk_reset.soft_reset_rx_align = 1;
    cmd = DRV_IOW(IntLkSoftReset_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));

    sal_task_sleep(1);

    /*need do serdes reset and release*/
    sys_greatbelt_chip_reset_rx_hss(lchip, 0, 1);
    sal_task_sleep(1);
    sys_greatbelt_chip_reset_rx_hss(lchip, 0, 0);

    /*release align reset*/
    cmd = DRV_IOR(IntLkSoftReset_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));
    intlk_reset.soft_reset_rx_align = 0;
    cmd = DRV_IOW(IntLkSoftReset_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));

    /*release rx reset*/
    intlk_reset.soft_reset_lrx = 0;
    cmd = DRV_IOW(IntLkSoftReset_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));

    sal_task_sleep(1);

    for (repeat = 0; repeat < 5; repeat++)
    {
        cmd = DRV_IOR(IntLkLaneRxDebugState_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rx_state));

        err_cnt = 0;
        need_reset = 0;

        for (i = 0; i < 5; i++)
        {
            cmd = DRV_IOR(IntLkPktStats_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_stats));
            if ((0 != i) && (intlk_stats.pkt_proc_crc24_err_cnt != err_cnt))
            {
               need_reset = 1;
               break;
            }
            err_cnt = intlk_stats.pkt_proc_crc24_err_cnt;
         }

        if (need_reset || !rx_state.rx_intf_align_status)
        {
            cmd = DRV_IOR(IntLkSoftReset_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));
            intlk_reset.soft_reset_lrx = 0xff;
            cmd = DRV_IOW(IntLkSoftReset_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));
            intlk_reset.soft_reset_rx_align = 1;
            cmd = DRV_IOW(IntLkSoftReset_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));

            sal_task_sleep(1);

            sys_greatbelt_chip_reset_rx_hss(lchip, 0, 1);
            sal_task_sleep(1);
            sys_greatbelt_chip_reset_rx_hss(lchip, 0, 0);

            cmd = DRV_IOR(IntLkSoftReset_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));

              /*release align reset */
            intlk_reset.soft_reset_rx_align = 0;
            cmd = DRV_IOW(IntLkSoftReset_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));

            intlk_reset.soft_reset_lrx = 0;
            cmd = DRV_IOW(IntLkSoftReset_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));

            sal_task_sleep(1);
            cmd = DRV_IOR(IntLkLaneRxDebugState_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rx_state));
        }
    }

    if (HW_PLATFORM == platform_type)
    {
        /*check intlk link status*/

        cmd = DRV_IOR(IntLkLaneRxDebugState_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rx_state));

        if (rx_state.rx_intf_align_status == 0)
        {
            SYS_STK_DBG_DUMP("Chip %d Intlk Align is not link!!!!!!!\n", lchip);
            return CTC_E_UNEXPECT;
        }

    }

    return CTC_E_NONE;
}


#define SYS_API_START

int32
sys_greatbelt_stacking_mcast_mode(uint8 lchip, uint8 mcast_mode)
{
    SYS_STK_INIT_CHECK(lchip);
    if (g_gb_stacking_master[lchip]->trunk_num)
    {
        return CTC_E_NOT_READY;
    }
    g_gb_stacking_master[lchip]->mcast_mode = mcast_mode;
    return CTC_E_NONE;

}


int32
sys_greatbelt_stacking_port_enable(uint8 lchip, ctc_stacking_trunk_t* p_trunk, uint8 enable)
{
    uint32 cmd       = 0;
    uint16 gport     = 0;
    uint16 lport      = 0;
    uint16 chan_id    = 0;
    uint32 field_val = 0;
    epe_hdr_adjust_ctl_t     epe_hdr_adjust_ctl;
    buf_retrv_stacking_en_t buf_retrv_stacking_en;

    SYS_STK_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trunk);
    gport    = p_trunk->gport;
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    SYS_STK_LPORT_CHECK(lport);

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == chan_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    /*1. set not edit packet on stacking port*/
    field_val = enable;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_CtagOperationDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_StagOperationDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = !enable;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_UntagDefaultVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    /*2. Ecap Output packet Header enable*/
    if (enable)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_stacking_encap_header_enable(lchip, gport, &(p_trunk->encap_hdr)));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_stacking_encap_header_disable(lchip, gport));
    }

    /*3. config EpeHdrAdjustChanCtl and BufRetrvStackingEn*/
    sal_memset(&epe_hdr_adjust_ctl, 0, sizeof(epe_hdr_adjust_ctl_t));
    sal_memset(&buf_retrv_stacking_en, 0, sizeof(buf_retrv_stacking_en_t));
    cmd = DRV_IOR(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl));
    cmd = DRV_IOR(BufRetrvStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_retrv_stacking_en));
    if (enable)
    {
        if (chan_id >= 32)
        {
            CTC_BIT_SET(epe_hdr_adjust_ctl.packet_header_en63_32, (chan_id - 32));
            CTC_BIT_SET(buf_retrv_stacking_en.stacking_en63_to32, (chan_id - 32));
        }
        else
        {
            CTC_BIT_SET(epe_hdr_adjust_ctl.packet_header_en31_0, chan_id);
            CTC_BIT_SET(buf_retrv_stacking_en.stacking_en31_to0, chan_id);
        }
    }
    else
    {
        if (chan_id >= 32)
        {
            CTC_BIT_UNSET(epe_hdr_adjust_ctl.packet_header_en63_32, (chan_id - 32));
            CTC_BIT_UNSET(buf_retrv_stacking_en.stacking_en63_to32, (chan_id - 32));
        }
        else
        {
            CTC_BIT_UNSET(epe_hdr_adjust_ctl.packet_header_en31_0, chan_id);
            CTC_BIT_UNSET(buf_retrv_stacking_en.stacking_en31_to0, chan_id);
        }
    }
    cmd = DRV_IOW(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl));

    cmd = DRV_IOW(BufRetrvStackingEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_retrv_stacking_en));

    if (enable)
    {
        CTC_ERROR_RETURN(sys_greatbelt_port_queue_add_for_stacking(lchip, SYS_QUEUE_TYPE_STACKING, lport, 0));
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_port_queue_add_for_stacking(lchip, SYS_QUEUE_TYPE_NORMAL, lport, 0));
    }

    return CTC_E_NONE;

}

int32
sys_greatbelt_encap_stk_header(uint8 lchip, ctc_stacking_cloud_hdr_t* p_hdr)
{
    packet_header_outer_t* p_stk_hdr = NULL;
    uint32  dest_map = 0;
    uint8   gchip    = 0;
    uint32  src_port                = 0;
    uint8*  p_cld_header            = NULL;

    CTC_PTR_VALID_CHECK(p_hdr);

    if (p_hdr->hdr.hdr_flag != CTC_STK_HDR_WITH_L2)
    {
        return CTC_E_NOT_SUPPORT;
    }

    /*Step1: encap stacking hdr*/
    p_stk_hdr = (packet_header_outer_t*)ctc_packet_skb_push(&p_hdr->skb, 32);
    if (p_hdr->is_mcast)
    {
        dest_map = SYS_GREATBELT_BUILD_DESTMAP(1, gchip, p_hdr->dest_group_id);
        p_stk_hdr->header_type =  1;
    }
    else
    {
        if (CTC_IS_LINKAGG_PORT(p_hdr->dest_gport))
        {
            gchip = CTC_LINKAGG_CHIPID;
        }
        else
        {
            gchip = CTC_MAP_GPORT_TO_GCHIP(p_hdr->dest_gport);
        }
        dest_map = SYS_GREATBELT_BUILD_DESTMAP(0, gchip, CTC_MAP_GPORT_TO_LPORT(p_hdr->dest_gport));
    }

    src_port = CTC_MAP_LPORT_TO_GPORT(p_hdr->src_gchip, 249);
    SYS_GREATBELT_GPORT_TO_GPORT14(src_port);

    p_stk_hdr->dest_map = dest_map;
    p_stk_hdr->source_port = src_port;
    p_stk_hdr->priority =  p_hdr->priority;
    CTC_ERROR_RETURN(sys_greatbelt_packet_swap32(lchip, (uint32*)p_stk_hdr, 32/4, TRUE));

    /*Step2: encap l2 cloud hdr*/
    p_cld_header = (uint8*)ctc_packet_skb_push(&p_hdr->skb, 18);
    sal_memcpy(p_cld_header, p_hdr->hdr.mac_da, sizeof(mac_addr_t));
    p_cld_header += sizeof(mac_addr_t);
    sal_memcpy((p_cld_header), p_hdr->hdr.mac_sa, sizeof(mac_addr_t));
    p_cld_header += sizeof(mac_addr_t);
    if (p_hdr->hdr.vlan_valid)
    {
        *((uint16*)p_cld_header) = sal_htons(0x8100);
        p_cld_header += 2;
        *((uint16*)p_cld_header) = sal_htons(p_hdr->hdr.vlan_id);
        p_cld_header += 2;
    }
    *((uint16*)p_cld_header) = sal_htons(0x55BB);

    return CTC_E_NONE;
}

#define SYS_API_END

int32
sys_greatbelt_stacking_init(uint8 lchip, void* p_cfg)
{
    int32 ret           = CTC_E_NONE;
    sys_greatbelt_opf_t opf;
    ctc_stacking_glb_cfg_t stacking_glb_cfg;
    ctc_stacking_glb_cfg_t* p_glb_cfg = (ctc_stacking_glb_cfg_t*)p_cfg;
    q_link_agg_timer_ctl2_t  q_linkagg_timer_ctl2;
    uint32 linkagg_interval = SYS_STK_DLB_TIMER_INTERVAL; /*s*/
    uint8  ts_threshold2 = SYS_STK_DLB_TS_THRES;
    uint16 max_ptr2 = SYS_STK_DLB_MAX_PTR;
    uint16 min_ptr2 = 0;
    uint32 core_frequecy = 0;
    uint32 cmd_timer = 0;

    /* init stacking*/
    if (NULL == p_glb_cfg)
    {
        stacking_glb_cfg.hdr_glb.mac_da_chk_en     = 0;
        stacking_glb_cfg.hdr_glb.ether_type_chk_en = 0;
        stacking_glb_cfg.hdr_glb.vlan_tpid         = 0x8100;
        stacking_glb_cfg.hdr_glb.ether_type        = 0x55bb;
        stacking_glb_cfg.hdr_glb.ip_protocol       = 255;
        stacking_glb_cfg.hdr_glb.udp_dest_port     = 0x1234;
        stacking_glb_cfg.hdr_glb.udp_src_port      = 0x5678;
        stacking_glb_cfg.hdr_glb.udp_en            = 0;

        stacking_glb_cfg.hdr_glb.ip_ttl            = 255;
        stacking_glb_cfg.hdr_glb.ip_dscp           = 63;
        stacking_glb_cfg.hdr_glb.cos               = 7;

        stacking_glb_cfg.hdr_glb.is_ipv4           = 1;
        stacking_glb_cfg.hdr_glb.ipsa.ipv4         = 0x11223344;

        stacking_glb_cfg.connect_glb.member_num = 1;

        stacking_glb_cfg.connect_glb.member_port[lchip][0] = SYS_STK_FABRIC_PORT;

        stacking_glb_cfg.connect_glb.mode = 1;
        stacking_glb_cfg.trunk_mode = 0;

        p_glb_cfg = &stacking_glb_cfg;
    }

    g_gb_stacking_master[lchip] = (sys_stacking_master_t*)mem_malloc(MEM_STK_MODULE, sizeof(sys_stacking_master_t));
    if (NULL == g_gb_stacking_master[lchip])
    {
        ret = CTC_E_NO_MEMORY;
        goto ERROR_FREE_MEM;
    }

    sal_memset(g_gb_stacking_master[lchip], 0, sizeof(sys_stacking_master_t));

    ret = sal_mutex_create(&(g_gb_stacking_master[lchip]->p_stacking_mutex));
    if (ret || !(g_gb_stacking_master[lchip]->p_stacking_mutex))
    {
            goto ERROR_FREE_MEM;
    }

    /*packet edit mode*/
    g_gb_stacking_master[lchip]->edit_mode = SYS_NH_EDIT_MODE();


    g_gb_stacking_master[lchip]->p_trunk_vec = ctc_vector_init(SYS_STK_TRUNK_VEC_BLOCK_NUM, SYS_STK_TRUNK_VEC_BLOCK_SIZE);
    if (NULL == g_gb_stacking_master[lchip]->p_trunk_vec)
    {
        ret = CTC_E_NO_MEMORY;
        goto ERROR_FREE_MEM;
    }


    g_gb_stacking_master[lchip]->mcast_hash  = ctc_hash_create(
            16,
            16,
            (hash_key_fn)_sys_greatbelt_stacking_mcast_hash_make,
            (hash_cmp_fn)_sys_greatbelt_stacking_mcast_hash_cmp);

    if (NULL == g_gb_stacking_master[lchip]->mcast_hash)
    {
        ret = CTC_E_NO_MEMORY;
        goto ERROR_FREE_MEM;
    }


    /*init opf*/
    g_gb_stacking_master[lchip]->opf_type = OPF_STACKING;
    ret = sys_greatbelt_opf_init(lchip, g_gb_stacking_master[lchip]->opf_type, SYS_STK_OPF_MAX);
    if (CTC_E_NONE != ret)
    {
        goto ERROR_FREE_MEM;
    }

    sal_memset(&opf, 0, sizeof(opf));
    opf.pool_type  = g_gb_stacking_master[lchip]->opf_type;


    opf.pool_index = SYS_STK_OPF_MEM_BASE;
    ret = sys_greatbelt_opf_init_offset(lchip, &opf, 0 , SYS_STK_TRUNK_MEMBERS);
    if (CTC_E_NONE != ret)
    {
        goto ERROR_FREE_MEM;
    }

    opf.pool_index = SYS_STK_OPF_MCAST_PROFILE;
    ret = sys_greatbelt_opf_init_offset(lchip, &opf, 1 , (256-1));
    if (CTC_E_NONE != ret)
    {
        goto ERROR_FREE_MEM;
    }

    ret =  sys_greatbelt_stacking_mcast_group_create(lchip, 0, 0, &g_gb_stacking_master[lchip]->p_default_prof_db, 0);
    if (NULL == g_gb_stacking_master[lchip]->p_default_prof_db)
    {
        goto ERROR_FREE_MEM;
    }
    g_gb_stacking_master[lchip]->stacking_mcast_offset = g_gb_stacking_master[lchip]->p_default_prof_db->head_met_offset;

    ret = sys_greatbelt_stacking_create_keeplive_group(lchip, 0);
    if (CTC_E_NONE != ret)
    {
        goto ERROR_FREE_MEM;
    }

    ret = _sys_greatbelt_stacking_hdr_set(lchip, &p_glb_cfg->hdr_glb);
    if (CTC_E_NONE != ret)
    {
        goto ERROR_FREE_MEM;
    }

    ret = _sys_greatbelt_stacking_set_enable(lchip, TRUE);
    if (CTC_E_NONE != ret)
    {
        goto ERROR_FREE_MEM;
    }


    g_gb_stacking_master[lchip]->stacking_en = 1;
    g_gb_stacking_master[lchip]->mcast_mode = p_glb_cfg->mcast_mode;
    g_gb_stacking_master[lchip]->trunk_mode = p_glb_cfg->trunk_mode;
    g_gb_stacking_master[lchip]->bind_mcast_en = 0;

    core_frequecy = sys_greatbelt_get_core_freq(lchip);
    sal_memset(&q_linkagg_timer_ctl2, 0, sizeof(q_link_agg_timer_ctl2_t));
    cmd_timer = DRV_IOW(QLinkAggTimerCtl2_t, DRV_ENTRY_FLAG);
    q_linkagg_timer_ctl2.max_phy_ptr2 = 511;
    q_linkagg_timer_ctl2.max_ptr2 = max_ptr2;
    q_linkagg_timer_ctl2.min_ptr2 = min_ptr2;
    q_linkagg_timer_ctl2.update_en2 = 1;

    q_linkagg_timer_ctl2.ts_threshold2 = ts_threshold2;
    q_linkagg_timer_ctl2.update_threshold2 =
        (linkagg_interval * (core_frequecy * 1000000 / DOWN_FRE_RATE)) / ((max_ptr2 - min_ptr2 + 1) * ts_threshold2);
    /* init dlb timer control */
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_timer, &q_linkagg_timer_ctl2));


    return CTC_E_NONE;

ERROR_FREE_MEM:


    if (NULL != g_gb_stacking_master[lchip]->p_trunk_vec)
    {
        ctc_vector_release(g_gb_stacking_master[lchip]->p_trunk_vec);
    }

    if (NULL != g_gb_stacking_master[lchip]->mcast_hash)
    {
        ctc_hash_free(g_gb_stacking_master[lchip]->mcast_hash);
    }


    if (g_gb_stacking_master[lchip]->p_stacking_mutex)
    {
        sal_mutex_destroy(g_gb_stacking_master[lchip]->p_stacking_mutex);
    }

    if (NULL != g_gb_stacking_master[lchip])
    {
        mem_free(g_gb_stacking_master[lchip]);
    }

    return ret;
}

STATIC int32
_sys_greatbelt_stacking_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_stacking_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == g_gb_stacking_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (NULL != g_gb_stacking_master[lchip]->mcast_hash)
    {
        ctc_hash_free(g_gb_stacking_master[lchip]->mcast_hash);
    }

    /*free trunk vector*/
    ctc_vector_traverse(g_gb_stacking_master[lchip]->p_trunk_vec, (vector_traversal_fn)_sys_greatbelt_stacking_free_node_data, NULL);
    ctc_vector_release(g_gb_stacking_master[lchip]->p_trunk_vec);

    sal_mutex_destroy(g_gb_stacking_master[lchip]->p_stacking_mutex);
    mem_free(g_gb_stacking_master[lchip]);

    return CTC_E_NONE;
}

