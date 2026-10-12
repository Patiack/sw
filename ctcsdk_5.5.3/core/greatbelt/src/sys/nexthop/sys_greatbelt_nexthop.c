/**
 @file sys_greatbelt_nexthop.c

 @date 2009-09-16

 @version v2.0

 The file contains all nexthop module core logic
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_common.h"
#include "ctc_linklist.h"

#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_register.h"
#include "sys_greatbelt_stats.h"
#include "sys_greatbelt_register.h"
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_l3if.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_aps.h"
#include "sys_greatbelt_internal_port.h"

#include "sys_greatbelt_cpu_reason.h"

#include "greatbelt/include/drv_lib.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

#define SYS_GREATBELT_INVALID_STATS_PTR 0
#define SYS_GREATBELT_NH_DROP_DESTMAP          0xFFFE

#define SYS_DSNH4WREG_INDEX_FOR_BRG            0  /*must be 0    for GB/GG*/
#define SYS_DSNH4WREG_INDEX_FOR_MIRROR         1  /*must be 1    for GB/GG*/
#define SYS_DSNH4WREG_INDEX_FOR_BYPASS         2  /*must be 2/3  for GB/GG* use 8w*/

#define SYS_DSNH_FOR_REMOTE_MIRROR  0             /*Must be 0, IngressEdit Mode for remote mirror, for GB/GG*/

#define SYS_NH_EXTERNAL_NHID_DEFAUL_NUM 16384
#define SYS_NH_TUNNEL_ID_DEFAUL_NUM     1024
#define SYS_NH_ARP_ID_DEFAUL_NUM        2048

#define SYS_NH_MCAST_LOGIC_MAX_CNT       32


#define SYS_GREATBELT_NH_DSNH_SET_DEFAULT_COS_ACTION(_dsnh)    \
    do {                                                      \
        _dsnh.replace_ctag_cos = 0;                         \
        _dsnh.copy_ctag_cos = 0;                            \
        _dsnh.derive_stag_cos = 0;                          \
        _dsnh.stag_cfi = 0; /*default not replace cos*/      \
    } while (0)

#define SYS_GREATBELT_NH_DSNH_ASSIGN_MACDA(_dsnh, _dsnh_param)         \
    do {                                                               \
        uint16 mac_47_to_32 = 0; \
        uint32 mac_31_to_0 = 0;  \
        mac_47_to_32 = (_dsnh_param.macDa[0] << 8) | \
            (_dsnh_param.macDa[1] << 0); \
        mac_31_to_0  = (_dsnh_param.macDa[2] << 24) | \
            (_dsnh_param.macDa[3] << 16) | \
            (_dsnh_param.macDa[4] << 8) | \
            (_dsnh_param.macDa[5] << 0); \
        _dsnh.dest_vlan_ptr  = _dsnh_param.dest_vlan_ptr; \
        _dsnh.vlan_xlate_mode   = (mac_47_to_32 >> 15) & 0x1;                  /* macDa[47] */   \
        _dsnh.stats_ptr         = ((mac_47_to_32 >> 9) & 0x3F) << 8;           /* macDa[46:41]*/ \
        _dsnh.stag_cfi          = (mac_47_to_32 >> 8) & 0x1;                   /* macDa[40] */   \
        _dsnh.stag_cos          = (mac_47_to_32 >> 5) & 0x7;                  /* macDa[39:37]*/ \
        _dsnh.copy_ctag_cos     = (mac_47_to_32 >> 4) & 0x1;     /* macDa[36] */ \
        _dsnh.l3_edit_ptr15_15  = (mac_47_to_32 >> 3) & 0x1;    /* macDa[35] */ \
        _dsnh.l2_edit_ptr15     = (mac_47_to_32 >> 2) & 0x1;    /* macDa[34] */ \
        _dsnh.output_cvlan_id_valid = (mac_47_to_32 >> 1) & 0x1;        /* macDa[33] */ \
        _dsnh.l3_edit_ptr14_0   = ((mac_47_to_32 & 0x1) << 14) | ((mac_31_to_0 >> 18) & 0x3FFF);   /* macDa[32:18] */ \
        _dsnh.l2_edit_ptr14_12  = (mac_31_to_0 >> 15) & 0x7;      /* macDa[17:15] */ \
        _dsnh.replace_ctag_cos  = (mac_31_to_0 >> 14) & 0x1;      /* macDa[14] */ \
        _dsnh.derive_stag_cos   = (mac_31_to_0 >> 13) & 0x1;      /* macDa[13] */ \
        _dsnh.tagged_mode       = (mac_31_to_0 >> 12) & 0x1;      /* macDa[12] */ \
        _dsnh.l2_edit_ptr11_0   = mac_31_to_0 & 0xFFF;             /* macDa[11:0] */ \
    } while (0)

#define SYS_GREATBELT_NH_DSNH_BUILD_L2EDITPTR_L3EDITPTR(__dsnh,     \
                                                        __dsnh_l2editptr, __dsnh_l3editptr)     \
    do {                                                            \
        __dsnh.l2_edit_ptr11_0  = (__dsnh_l2editptr) & 0xFFF;          /* l2EditPtr[11:0] */   \
        __dsnh.l2_edit_ptr14_12 = (__dsnh_l2editptr >> 12) & 0x7;        /* l2EditPtr[14:12] */  \
        __dsnh.l2_edit_ptr15    = (__dsnh_l2editptr >> 15) & 0x1;        /* l2EditPtr[15] */     \
        __dsnh.l3_edit_ptr14_0  = (__dsnh_l3editptr) & 0x7FFF;         /* l3EditPtr[14:0] */   \
        __dsnh.l3_edit_ptr15_15 = (__dsnh_l3editptr >> 15) & 0x1;        /* l3EditPtr[15] */     \
    } while (0)

#define SYS_GREATBELT_NH_DSNH8W_BUILD_L2EDITPTR_L3EDITPTR(__dsnh,   \
                                                          __dsnh_l2editptr, __dsnh_l3editptr)     \
    do {                                                            \
        __dsnh.l2_edit_ptr11_0  = (__dsnh_l2editptr) & 0xFFF;          /* l2EditPtr[11:0] */  \
        __dsnh.l2_edit_ptr14_12 = (__dsnh_l2editptr >> 12) & 0x7;        /* l2EditPtr[14:12] */ \
        __dsnh.l2_edit_ptr15    = (__dsnh_l2editptr >> 15) & 0x1;        /* l2EditPtr[15] */    \
        __dsnh.l2_edit_ptr17_16 = (__dsnh_l2editptr >> 16) & 0x3;        /* l2EditPtr[17:16] */ \
        __dsnh.l3_edit_ptr14_0  = (__dsnh_l3editptr) & 0x7FFF;         /* l3EditPtr[14:0] */  \
        __dsnh.l3_edit_ptr15_15 = (__dsnh_l3editptr >> 15) & 0x1;        /* l3EditPtr[15] */    \
        __dsnh.l3_edit_ptr17_16 = (__dsnh_l3editptr >> 16) & 0x3;        /* l3EditPtr[17:16] */ \
    } while (0)

#define SYS_GREATBELT_NH_DSNH_BUILD_FLAG(__dsnh,  flag)  \
    do {                                                                    \
        if (CTC_FLAG_ISSET(flag, SYS_NH_PARAM_FLAG_USE_TTL_FROM_PKT)) \
        { \
            __dsnh.stag_cos |= 1; \
            __dsnh.derive_stag_cos = 1; \
        } \
        if (CTC_FLAG_ISSET(flag, SYS_NH_PARAM_FLAG_USE_MAPPED_STAG_COS)) \
        { \
            __dsnh.stag_cfi = 1; \
        } \
        if (CTC_FLAG_ISSET(flag, SYS_NH_PARAM_FLAG_USE_MAPPED_CTAG_COS)) \
        { \
            __dsnh.replace_ctag_cos = 1; \
        } \
        if (CTC_FLAG_ISSET(flag, SYS_NH_PARAM_FLAG_USE_MAPPED_DSCP)) \
        { \
            __dsnh.stag_cos |= 1 << 1; \
        } \
    } while (0)

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/

sys_greatbelt_nh_master_t* p_gb_nh_master[CTC_MAX_LOCAL_CHIP_NUM] = { NULL };

static sys_nh_table_entry_info_t nh_table_info_array[] =
{
    /*SYS_NH_ENTRY_TYPE_FWD*/
    {
        DsFwd_t,
        1,
        OPF_NH_DYN1_SRAM,
        0,
        0,
    },
    /*SYS_NH_ENTRY_TYPE_MET*/
    {
        DsMetEntry_t,
        1,
        OPF_NH_DYN1_SRAM,
        0,
        0,
    },
    /*SYS_NH_ENTRY_TYPE_MET_FOR_IPMCBITMAP*/
    {
        DsMetEntry_t,
        1,
        OPF_NH_DSMET_FOR_IPMCBITMAP,
        0,
        0,
    },
    /*SYS_NH_ENTRY_TYPE_NEXTHOP_4W*/
    {
        DsNextHop4W_t,
        1,
        OPF_NH_DYN2_SRAM,
        0,
        0,
    },
    /*SYS_NH_ENTRY_TYPE_NEXTHOP_8W*/
    {
        DsNextHop8W_t,
        2,
        OPF_NH_DYN2_SRAM,
        0,
        1,
    },

    /*SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W*/
    {
        DsL2EditEth4W_t,
        1,
        OPF_NH_DYN2_SRAM,
        0,
        0,
    },
    /*SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W*/
    {
        DsL2EditEth8W_t,
        2,
        OPF_NH_DYN2_SRAM,
        0,
        1,
    },
    /*SYS_NH_ENTRY_TYPE_L2EDIT_FLEX_8W*/
    {
        DsL2EditFlex8W_t,
        2,
        OPF_NH_DYN2_SRAM,
        0,
        1,
    },
    /*SYS_NH_ENTRY_TYPE_L2EDIT_LPBK*/
    {
        DsL2EditLoopback_t,
        1,
        OPF_NH_DYN2_SRAM,
        0,
        0,
    },
    /*SYS_NH_ENTRY_TYPE_L2EDIT_PBB_4W*/
    {
        DsL2EditPbb4W_t,
        1,
        OPF_NH_DYN2_SRAM,
        0,
        0,
    },
    /*SYS_NH_ENTRY_TYPE_L2EDIT_PBB_8W*/
    {
        DsL2EditPbb8W_t,
        2,
        OPF_NH_DYN2_SRAM,
        0,
        1,
    }
    ,
    /*SYS_NH_ENTRY_TYPE_L2EDIT_SWAP*/
    {
        DsL2EditSwap_t,
        2,
        OPF_NH_DYN2_SRAM,
        0,
        1,
    },
    /*SYS_NH_ENTRY_TYPE_L3EDIT_FLEX*/
    {
        DsL3EditFlex_t,
        1,
        OPF_NH_DYN2_SRAM,
        0,
        0,
    },
    /*SYS_NH_ENTRY_TYPE_L3EDIT_MPLS_4W*/
    {
        DsL3EditMpls4W_t,
        1,
        OPF_NH_DYN2_SRAM,
        0,
        0,
    }
    ,
    /*SYS_NH_ENTRY_TYPE_L3EDIT_MPLS_8W*/
    {
        DsL3EditMpls8W_t,
        2,
        OPF_NH_DYN2_SRAM,
        0,
        1,
    }
    ,
    /*SYS_NH_ENTRY_TYPE_L3EDIT_NAT_4W*/
    {
        DsL3EditNat4W_t,
        1,
        OPF_NH_DYN2_SRAM,
        0,
        0,
    }
    ,
    /*SYS_NH_ENTRY_TYPE_L3EDIT_NAT_8W*/
    {
        DsL3EditNat8W_t,
        2,
        OPF_NH_DYN2_SRAM,
        0,
        1,
    }
    ,

    /*SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V4*/
    {
        DsL3EditTunnelV4_t,
        2,
        OPF_NH_DYN2_SRAM,
        0,
        1,
    }
    ,
    /*SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V6*/
    {
        DsL3EditTunnelV6_t,
        2,
        OPF_NH_DYN2_SRAM,
        0,
        1,
    },

    /*SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V4*/
    {
        DsL3TunnelV4IpSa_t,
        1,
        OPF_NH_DYN2_SRAM,
        0,
        0,
    }
    ,
    /*SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V6*/
    {
        DsL3TunnelV6IpSa_t,
        2,
        OPF_NH_DYN2_SRAM,
        0,
        1,
    }
}
;

/**
 @brief This function is used to get nexthop module master data
 */
STATIC int32
_sys_greatbelt_nh_get_nh_master(uint8 lchip, sys_greatbelt_nh_master_t** p_out_nh_master)
{
    if (NULL == p_gb_nh_master[lchip])
    {
        return CTC_E_NH_NOT_INIT;
    }

    *p_out_nh_master = p_gb_nh_master[lchip];
    return CTC_E_NONE;
}


int32 sys_greatbelt_nh_dump_resource_usage(uint8 lchip)
{

    uint32 used_cnt = 0;
    uint8 dyn_tbl_idx = 0;
    uint32 entry_enum = 0;
    uint32 glb_entry_num = 0;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------------Work Mode----------------------------\n");
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-32s: %s\n","Packet Edit mode", p_gb_nh_master->pkt_nh_edit_mode ?"Egress Chip":"Ingess Chip");
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-32s: %d\n", "Ecmp mode",p_gb_nh_master->ecmp_mode);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-32s: %d\n", "Merge dsfwd mode",p_gb_nh_master->no_dsfwd);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-32s: %d\n", "Max ecmp member",p_gb_nh_master->max_ecmp);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-32s: %d\n", "ECMP group count",p_gb_nh_master->cur_ecmp_cnt);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-32s: %s\n", "IPMC support logic replication",p_gb_nh_master->ipmc_logic_replication ? "Yes":"No");

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n------------------ Nexthop Resource ---------------------------\n");
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s\n","External Nexthop ");
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--Total count ",p_gb_nh_master->max_external_nhid);
    /*nhid 0: reserved*/
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--Used count", p_gb_nh_master->external_nhid_vec->used_cnt  + 1);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s\n","Internal Nexthop");
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--Total count", SYS_NH_INTERNAL_NHID_MAX_BLK_NUM *
    SYS_NH_INTERNAL_NHID_BLK_SIZE);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--Used  count ",p_gb_nh_master->internal_nhid_vec->used_cnt);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s:\n","MPLS Tunnel");
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--Total count ",p_gb_nh_master->max_tunnel_id);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--Used  count ",p_gb_nh_master->tunnel_id_vec->used_cnt);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s:\n","ARP ID");
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--Total count ",p_gb_nh_master->max_arp_id);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--Used  count ",p_gb_nh_master->arp_id_vec->used_cnt);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n------------------Created Nexthop ---------------------------\n");
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","Mcast Nexthop", p_gb_nh_master->nhid_used_cnt[SYS_NH_TYPE_MCAST]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","BRGUC Nexthop", p_gb_nh_master->nhid_used_cnt[SYS_NH_TYPE_BRGUC]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","IPUC Nexthop", p_gb_nh_master->nhid_used_cnt[SYS_NH_TYPE_IPUC]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","MPLS Nexthop", p_gb_nh_master->nhid_used_cnt[SYS_NH_TYPE_MPLS]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","ECMP Nexthop", p_gb_nh_master->nhid_used_cnt[SYS_NH_TYPE_ECMP]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","Rspan Nexthop", p_gb_nh_master->nhid_used_cnt[SYS_NH_TYPE_RSPAN]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","Ip Tunnel Nexthop", p_gb_nh_master->nhid_used_cnt[SYS_NH_TYPE_IP_TUNNEL]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","Misc Nexthop", p_gb_nh_master->nhid_used_cnt[SYS_NH_TYPE_MISC]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","ToCpu Nexthop", p_gb_nh_master->nhid_used_cnt[SYS_NH_TYPE_TOCPU]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","Drop Nexthop", p_gb_nh_master->nhid_used_cnt[SYS_NH_TYPE_DROP]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","unRov Nexthop", p_gb_nh_master->nhid_used_cnt[SYS_NH_TYPE_UNROV]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","ILoop Nexthop", p_gb_nh_master->nhid_used_cnt[SYS_NH_TYPE_ILOOP]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","ELoop Nexthop", p_gb_nh_master->nhid_used_cnt[SYS_NH_TYPE_ELOOP]);


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n----------------------Nexthop Table from Profile----------------\n");
    sys_greatbelt_ftm_get_dynamic_table_info(lchip, DsFwd_t,  &dyn_tbl_idx, &entry_enum, &glb_entry_num);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-16s: %-8u in DYN%d_SRAM\n","DsFwd",entry_enum,dyn_tbl_idx);
    sys_greatbelt_ftm_get_dynamic_table_info(lchip, DsMetEntry_t,  &dyn_tbl_idx, &entry_enum, &glb_entry_num);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-16s: %-8u in DYN%d_SRAM \n","DsMet",entry_enum,dyn_tbl_idx);
    sys_greatbelt_ftm_get_dynamic_table_info(lchip, DsNextHop4W_t,  &dyn_tbl_idx, &entry_enum, &glb_entry_num);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-16s: %-8u in DYN%d_SRAM \n","DsNexthop",entry_enum,dyn_tbl_idx);
    sys_greatbelt_ftm_get_dynamic_table_info(lchip, DsL2EditEth4W_t,  &dyn_tbl_idx, &entry_enum, &glb_entry_num);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-16s: %-8u in DYN%d_SRAM\n","DsEdit",entry_enum,dyn_tbl_idx);


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n--------------------[lchip :%d]Nexthop Table Usage -------------------------\n",lchip);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-16s: %u\n","DsFwd",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_FWD]);

    used_cnt = p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_MET]
             + p_gb_nh_master->ipmc_bitmap_met_num
             + p_gb_nh_master->glb_met_used_cnt;
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-16s: %u\n","DsMet",used_cnt);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-32s: %u\n","--For IPMC Bitmap",p_gb_nh_master->ipmc_bitmap_met_num);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-32s: %u \n","--Global DsMet",p_gb_nh_master->glb_met_used_cnt);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-32s: %u \n","--Local DsMet",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_MET]);

    used_cnt = p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_NEXTHOP_4W]
             + p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_NEXTHOP_8W]
             + p_gb_nh_master->ipmc_bitmap_met_num
             + p_gb_nh_master->glb_nh_used_cnt;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-16s: %u\n","DsNexthop",used_cnt);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-32s: %u\n","--For IPMC Bitmap",p_gb_nh_master->ipmc_bitmap_met_num);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--Global DsNexthop",p_gb_nh_master->glb_nh_used_cnt);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--DsNexthop4w", p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_NEXTHOP_4W]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u [4w]\n","--DsNexthop8w",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_NEXTHOP_8W]);
    used_cnt = p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W]
             + p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W]
             + p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L2EDIT_FLEX_8W]
             + p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L2EDIT_LPBK]
             + p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L2EDIT_PBB_4W]
             + p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L2EDIT_PBB_8W]
             + p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L2EDIT_SWAP];

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n%-16s: %u\n","DsL2Edit",used_cnt);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--DsL2EditEth4W",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u [4w]\n","--DsL2EditEth8W",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u [4w]\n","--DsL2EditFlex8W",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L2EDIT_FLEX_8W]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--DsL2EditLoopback",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L2EDIT_LPBK]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--DsL2EditPbb4W",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L2EDIT_PBB_4W]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u [4w]\n","--DsL2EditPbb8W",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L2EDIT_PBB_8W]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--DsL2EditSwap",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L2EDIT_SWAP]);

    used_cnt = p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_FLEX]
             + p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_MPLS_4W]
             + p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_MPLS_8W]
             + p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_NAT_4W]
             + p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_NAT_8W]
             + p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V4]
             + p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V6]
             + p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V4_IPSA]
             + p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V6_IPSA];
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n%-16s:%u\n","DsL3Edit",used_cnt);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--DsL3EditFlex",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_FLEX]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--DsL3EditMpls4W",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_MPLS_4W]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u [4w]\n","--DsL3EditMpls8W ",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_MPLS_8W]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--DsL3TunnelV4IpSa",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V4_IPSA]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u [4w]\n","--DsL3TunnelV6IpSa",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V6_IPSA]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u [4w]\n","--DsL3EditTunnelV4",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V4]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u [4w]\n","--DsL3EditTunnelV6",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V6]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u\n","--DsL3EditNat4W",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_NAT_4W]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %u [4w]\n","--DsL3EditNat8W",p_gb_nh_master->nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_L3EDIT_NAT_8W]);


   return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_offset_init(uint8 lchip, sys_greatbelt_nh_master_t* p_master);

/****************************************************************************
 *
* Function
*
*****************************************************************************/

/**
 @brief AVL compare function for dsl2editeth4w tree
 */
STATIC int32
_sys_greatbelt_nh_db_dsl2editeth4w_cmp(void* p_data_new, void* p_data_old)
{
    sys_nh_db_dsl2editeth4w_t* p_new = (sys_nh_db_dsl2editeth4w_t*)p_data_new;
    sys_nh_db_dsl2editeth4w_t* p_old = (sys_nh_db_dsl2editeth4w_t*)p_data_old;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    if ((NULL == p_new) || (NULL == p_old))
    {
        return -1;
    }

    if (p_new->output_vid < p_old->output_vid)
    {
        return -1;
    }

    if (p_new->output_vid > p_old->output_vid)
    {
        return 1;
    }

    if (p_new->packet_type < p_old->packet_type)
    {
        return -1;
    }

    if (p_new->packet_type > p_old->packet_type)
    {
        return 1;
    }

    if (p_new->phy_if !=  p_old->phy_if )
    {
        return -1;
    }

    if (p_new->output_vlan_is_svlan != p_old->output_vlan_is_svlan)
    {
        return -1;
    }

    return sal_memcmp(p_new->mac_da, p_old->mac_da, sizeof(mac_addr_t));
}


/**
 @brief AVL compare function for dsl2editeth8w tree
 */
STATIC int32
_sys_greatbelt_nh_db_dsl2editeth8w_cmp(void* p_data_new, void* p_data_old)
{
    sys_nh_db_dsl2editeth8w_t* p_new = (sys_nh_db_dsl2editeth8w_t*)p_data_new;
    sys_nh_db_dsl2editeth8w_t* p_old = (sys_nh_db_dsl2editeth8w_t*)p_data_old;
    int32 ret = CTC_E_NONE;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    if ((NULL == p_new) || (NULL == p_old))
    {
        return -1;
    }

    ret = sal_memcmp(p_new->mac_da, p_old->mac_da, sizeof(mac_addr_t));
    if (ret)
    {
        return ret;
    }

    if(p_new->ip64_79 > p_old->ip64_79)
    {
        return 1;
    }
    else if(p_new->ip64_79 < p_old->ip64_79)
    {
        return -1;
    }
    else
    {
        if (p_new->ip32_63 > p_old->ip32_63)
        {
            return 1;
        }
        else if(p_new->ip32_63 < p_old->ip32_63)
        {
            return -1;
        }
        else
        {
            if (p_new->ip0_31 > p_old->ip0_31)
            {
                return 1;
            }
            else if(p_new->ip0_31 < p_old->ip0_31)
            {
                return -1;
            }
        }
    }

    if (p_new->output_vid < p_old->output_vid)
    {
        return -1;
    }

    if (p_new->output_vid > p_old->output_vid)
    {
        return 1;
    }

    if (p_new->packet_type < p_old->packet_type)
    {
        return -1;
    }

    if (p_new->packet_type > p_old->packet_type)
    {
        return 1;
    }

    if (p_new->output_vlan_is_svlan != p_old->output_vlan_is_svlan)
    {
        return -1;
    }

    return ret;
}

/**
 @brief This function is to initialize avl tree in nexthop module,
        this avl is used to maintains shared entry
 */
STATIC int32
_sys_greatbelt_nh_avl_init(uint8 lchip, sys_greatbelt_nh_master_t* p_master)
{
    int32 ret;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    ret = ctc_avl_create(&p_master->dsl2edit4w_tree, 0, _sys_greatbelt_nh_db_dsl2editeth4w_cmp);
    if (ret)
    {
        return CTC_E_CANT_CREATE_AVL;
    }

    ret = ctc_avl_create(&p_master->dsl2edit8w_tree, 0, _sys_greatbelt_nh_db_dsl2editeth8w_cmp);
    if (ret)
    {
        return CTC_E_CANT_CREATE_AVL;
    }

    return CTC_E_NONE;
}

/**
 @brief This function is used to initilize internal nexthop id pool
 */
STATIC int32
_sys_greatbelt_nh_external_id_init(uint8 lchip, ctc_vector_t** pp_nhid_vec, uint32 max_external_nhid)
{
    ctc_vector_t* p_nhid_vec;
    uint32 block_size = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    /*1. Creat vector, use vector to store external nh_info*/
    block_size = max_external_nhid / SYS_NH_EXTERNAL_NHID_MAX_BLK_NUM;
    p_nhid_vec = ctc_vector_init(SYS_NH_EXTERNAL_NHID_MAX_BLK_NUM, block_size);
    if (NULL == p_nhid_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    *pp_nhid_vec = p_nhid_vec;

    return CTC_E_NONE;
}

/**
 @brief This function is used to initilize internal nexthop id pool
 */
STATIC int32
_sys_greatbelt_nh_internal_id_init(uint8 lchip, ctc_vector_t** pp_nhid_vec, uint32 start_internal_nhid)
{
    ctc_vector_t* p_nhid_vec;
    sys_greatbelt_opf_t opf;
    uint32 nh_id_num;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    /*1. Creat vector, use vector to store internal nh_info*/
    p_nhid_vec = ctc_vector_init(SYS_NH_INTERNAL_NHID_MAX_BLK_NUM, SYS_NH_INTERNAL_NHID_BLK_SIZE);
    if (NULL == p_nhid_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    *pp_nhid_vec = p_nhid_vec;

    /*2. Init nhid pool, use opf to alloc/free internal nhid*/
    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_NH_NHID_INTERNAL, 1));
    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = OPF_NH_NHID_INTERNAL;
    opf.pool_index = 0;
    nh_id_num = (SYS_NH_INTERNAL_NHID_MAX_BLK_NUM * \
                 SYS_NH_INTERNAL_NHID_BLK_SIZE);
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf,
                                                   start_internal_nhid, nh_id_num));

    return CTC_E_NONE;
}

/**
 @brief This function is used to initilize internal nexthop id pool
 */
STATIC int32
_sys_greatbelt_nh_tunnel_id_init(uint8 lchip, ctc_vector_t** pp_tunnel_id_vec, uint16 max_tunnel_id)
{
    ctc_vector_t* p_tunnel_id_vec;
    uint16 block_size = (max_tunnel_id + SYS_NH_TUNNEL_ID_MAX_BLK_NUM - 1) / SYS_NH_TUNNEL_ID_MAX_BLK_NUM;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    /*1. Creat vector, use vector to store internal nh_info*/
    p_tunnel_id_vec = ctc_vector_init(SYS_NH_TUNNEL_ID_MAX_BLK_NUM, block_size);
    if (NULL == p_tunnel_id_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    *pp_tunnel_id_vec = p_tunnel_id_vec;

    return CTC_E_NONE;
}

/**
 @brief This function is used to create and init nexthop module master data
 */
STATIC int32
_sys_greatbelt_nh_mcast_id_init(uint8 lchip, ctc_vector_t** pp_mcast_id_vec, uint16 mcast_number)
{
    ctc_vector_t* p_mcast_id_vec;
    uint16 block_size = (mcast_number + SYS_NH_MCAST_ID_MAX_BLK_NUM - 1) / SYS_NH_MCAST_ID_MAX_BLK_NUM;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    /*1. Creat vector, use vector to store internal nh_info*/
    p_mcast_id_vec = ctc_vector_init(SYS_NH_MCAST_ID_MAX_BLK_NUM, block_size);
    if (NULL == p_mcast_id_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    *pp_mcast_id_vec = p_mcast_id_vec;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_arp_id_init(uint8 lchip, ctc_vector_t** pp_arp_id_vec, uint16 arp_number)
{
    ctc_vector_t* p_arp_id_vec;
    uint16 block_size = (arp_number + SYS_NH_ARP_ID_MAX_BLK_NUM - 1) / SYS_NH_ARP_ID_MAX_BLK_NUM;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    /*1. Creat vector, use vector to store internal nh_info*/
    p_arp_id_vec = ctc_vector_init(SYS_NH_ARP_ID_MAX_BLK_NUM, block_size);
    if (NULL == p_arp_id_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    *pp_arp_id_vec = p_arp_id_vec;

    return CTC_E_NONE;
}


/**
 @brief This function is used to create and init nexthop module master data
 */
STATIC int32
_sys_greatbelt_nh_master_new(uint8 lchip, sys_greatbelt_nh_master_t** pp_nexthop_master, uint32 boundary_of_extnl_intnl_nhid, uint16 max_tunnel_id, uint16 arp_num)
{
    sys_greatbelt_nh_master_t* p_master;
    uint8  dyn_tbl_idx = 0;
    uint32  mcast_group_num = 0;
    uint32   entry_enum = 0;
    int32 ret = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    if (NULL == pp_nexthop_master || (NULL != *pp_nexthop_master))
    {
        return CTC_E_INVALID_PTR;
    }

    /*1. allocate memory for nexthop master*/
    p_master = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_greatbelt_nh_master_t));
    if (NULL == p_master)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_master, 0, sizeof(sys_greatbelt_nh_master_t));

    /*2. Create Mutex*/
    ret = sal_mutex_create(&p_master->p_mutex);
    if (NULL == p_master->p_mutex)
    {
        ret = CTC_E_FAIL_CREATE_MUTEX;
        goto error_2;
    }

    /*3. AVL tree init*/
    CTC_ERROR_GOTO(_sys_greatbelt_nh_avl_init(lchip, p_master), ret, error_2);

    /*4. Nexthop external vector init*/
    CTC_ERROR_GOTO(_sys_greatbelt_nh_external_id_init(lchip, (&p_master->external_nhid_vec), boundary_of_extnl_intnl_nhid), ret, error_2);

    /*5. Nexthop internal vector init*/
    CTC_ERROR_GOTO(_sys_greatbelt_nh_internal_id_init(lchip, &(p_master->internal_nhid_vec), boundary_of_extnl_intnl_nhid), ret, error_2);

    /*6. Tunnel id vector init*/
    CTC_ERROR_GOTO(_sys_greatbelt_nh_tunnel_id_init(lchip, &(p_master->tunnel_id_vec), max_tunnel_id), ret, error_2);

    /*7. Arp id vector init*/
    CTC_ERROR_GOTO(_sys_greatbelt_nh_arp_id_init(lchip, &(p_master->arp_id_vec), arp_num), ret, error_2);

    /*8. Eloop list init*/
    p_master->eloop_list = ctc_slist_new();
    if (p_master->eloop_list == NULL)
    {
        ret = CTC_E_INVALID_PARAM;
        goto error_2;
    }

    /*9. Mcast vector init*/
    sys_greatbelt_ftm_get_dynamic_table_info(lchip, DsMetEntry_t,  &dyn_tbl_idx, &entry_enum, &mcast_group_num);
    CTC_ERROR_GOTO(_sys_greatbelt_nh_mcast_id_init(lchip, &(p_master->mcast_nhid_vec), mcast_group_num), ret, error_1);

    /*10. Eloop list init*/
    p_master->repli_offset_list = ctc_slist_new();
    if (p_master->repli_offset_list == NULL)
    {
        ret = CTC_E_INVALID_PARAM;
        goto error_1;
    }

    /*Return PTR*/
    *pp_nexthop_master = p_master;

    return CTC_E_NONE;
error_1:
    if (p_master->eloop_list)
    {
        ctc_slist_free(p_master->eloop_list);
    }
error_2:
    if (p_master)
    {
        mem_free(p_master);
    }
    if (p_master->p_mutex)
    {

SYS_NH_DESTROY_LOCK(p_master->p_mutex);
    }
    return ret;
}

/**
 @brief This function is register nexthop module callback function
 */
STATIC int32
_sys_greatbelt_nh_register_callback(uint8 lchip, sys_greatbelt_nh_master_t* p_master, uint16 nh_param_type,
                                    p_sys_nh_create_cb_t nh_create_cb, p_sys_nh_delete_cb_t nh_del_cb,
                                    p_sys_nh_update_cb_t nh_update_cb)
{
    p_master->callbacks_nh_create[nh_param_type] = nh_create_cb;
    p_master->callbacks_nh_delete[nh_param_type] = nh_del_cb;
    p_master->callbacks_nh_update[nh_param_type] = nh_update_cb;
    return CTC_E_NONE;
}

/**
 @brief This function is initialize nexthop module
        callback function for each nexthop type

 */
STATIC int32
_sys_greatbelt_nh_callback_init(uint8 lchip, sys_greatbelt_nh_master_t* p_master)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    /* 1. Ucast bridge */
    CTC_ERROR_RETURN(_sys_greatbelt_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_BRGUC,
                                                         &sys_greatbelt_nh_create_brguc_cb,
                                                         &sys_greatbelt_nh_delete_brguc_cb, NULL));

    /* 2. Mcast bridge */
    CTC_ERROR_RETURN(_sys_greatbelt_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_MCAST,
                                                         &sys_greatbelt_nh_create_mcast_cb,
                                                         &sys_greatbelt_nh_delete_mcast_cb,
                                                         &sys_greatbelt_nh_update_mcast_cb));

    /* 3. IPUC */
    CTC_ERROR_RETURN(_sys_greatbelt_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_IPUC,
                                                         &sys_greatbelt_nh_create_ipuc_cb,
                                                         &sys_greatbelt_nh_delete_ipuc_cb,
                                                         &sys_greatbelt_nh_update_ipuc_cb));

    /* 4. ECMP */
    CTC_ERROR_RETURN(_sys_greatbelt_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_ECMP,
                                                         &sys_greatbelt_nh_create_ecmp_cb,
                                                         &sys_greatbelt_nh_delete_ecmp_cb,
                                                         &sys_greatbelt_nh_update_ecmp_cb));

    /* 5. MPLS */
    CTC_ERROR_RETURN(_sys_greatbelt_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_MPLS,
                                                         &sys_greatbelt_nh_create_mpls_cb,
                                                         &sys_greatbelt_nh_delete_mpls_cb,
                                                         &sys_greatbelt_nh_update_mpls_cb));

    /* 6. Drop */
    CTC_ERROR_RETURN(_sys_greatbelt_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_DROP,
                                                         &sys_greatbelt_nh_create_special_cb,
                                                         &sys_greatbelt_nh_delete_special_cb, NULL));

    /* 7. ToCPU */
    CTC_ERROR_RETURN(_sys_greatbelt_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_TOCPU,
                                                         &sys_greatbelt_nh_create_special_cb,
                                                         &sys_greatbelt_nh_delete_special_cb, NULL));

    /* 8. Unresolve */
    CTC_ERROR_RETURN(_sys_greatbelt_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_UNROV,
                                                         &sys_greatbelt_nh_create_special_cb,
                                                         &sys_greatbelt_nh_delete_special_cb, NULL));

    /* 9. ILoop */
    CTC_ERROR_RETURN(_sys_greatbelt_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_ILOOP,
                                                         &sys_greatbelt_nh_create_iloop_cb,
                                                         &sys_greatbelt_nh_delete_iloop_cb,
                                                         &sys_greatbelt_nh_update_iloop_cb));

    /* 10. rspan */
    CTC_ERROR_RETURN(_sys_greatbelt_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_RSPAN,
                                                         &sys_greatbelt_nh_create_rspan_cb,
                                                         &sys_greatbelt_nh_delete_rspan_cb,
                                                         NULL));

    /* 11. ip-tunnel */
    CTC_ERROR_RETURN(_sys_greatbelt_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_IP_TUNNEL,
                                                         &sys_greatbelt_nh_create_ip_tunnel_cb,
                                                         &sys_greatbelt_nh_delete_ip_tunnel_cb,
                                                         &sys_greatbelt_nh_update_ip_tunnel_cb));

    /* 12. misc */
    CTC_ERROR_RETURN(_sys_greatbelt_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_MISC,
                                                         &sys_greatbelt_nh_create_misc_cb,
                                                         &sys_greatbelt_nh_delete_misc_cb,
                                                         &sys_greatbelt_nh_update_misc_cb));
    return CTC_E_NONE;
}



/**
 @brief This function is deinit nexthop module   callback function for each nexthop type
 */
int32
sys_greatbelt_nh_global_dync_entry_set_default(uint8 lchip, uint32 min_offset, uint32 max_offset)
{

    uint32 offset;
    ds_met_entry_t dsmet;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    /* assign default value for met entry. the default value is discarding packets */
    sal_memset(&dsmet, 0, sizeof(ds_met_entry_t));

    for (offset = min_offset; offset <= max_offset; offset++)
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET,
                                                           offset, &dsmet));
    }

    return CTC_E_NONE;
}

/**
 @brief This function is used to init resolved dsnexthop for bridge

 */
STATIC int32
_sys_greatbelt_nh_dsnh_init_for_brg(uint8 lchip, sys_nh_offset_attr_t* p_offset_attr)
{
    uint32 cmd;
    uint32 nhp_ptr = 0;
    ds_next_hop4_w_t dsnh;
    ds_next_hop8_w_t dsnh8w;

    CTC_PTR_VALID_CHECK(p_offset_attr);
    sal_memset(&dsnh, 0, sizeof(ds_next_hop4_w_t));
    sal_memset(&dsnh8w, 0, sizeof(ds_next_hop8_w_t));

    nhp_ptr = (SYS_GREATBELT_DSNH_INTERNAL_BASE << SYS_GREATBELT_DSNH_INTERNAL_SHIFT)
        | SYS_DSNH4WREG_INDEX_FOR_BRG;

    p_offset_attr->offset_base = nhp_ptr;
    p_offset_attr->entry_size = 1;
    p_offset_attr->entry_num = 1;

    SYS_GREATBELT_NH_DSNH_SET_DEFAULT_COS_ACTION(dsnh);
    dsnh.is_nexthop   = TRUE;
    dsnh.svlan_tagged = TRUE;
    dsnh.payload_operation = SYS_NH_OP_BRIDGE;
    dsnh.dest_vlan_ptr = SYS_DSNH_DESTVLANPTR_SPECIAL;

    cmd = DRV_IOR(EpeNextHopInternal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ((nhp_ptr&0x3)>>1), cmd, &dsnh8w));

    if(CTC_IS_BIT_SET(nhp_ptr, 0))
    {
        sal_memcpy(((uint8*)&dsnh8w + sizeof(ds_next_hop4_w_t)), &dsnh, sizeof(ds_next_hop4_w_t));
    }
    else
    {
        sal_memcpy(&dsnh8w, &dsnh, sizeof(ds_next_hop4_w_t));
    }

    cmd = DRV_IOW(EpeNextHopInternal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ((nhp_ptr&0x3)>>1), cmd, &dsnh8w));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_dsnh_init_for_oam_brg(uint8 lchip, sys_nh_offset_attr_t* p_offset_attr)
{
    uint32 nhp_ptr = 0;
    ds_next_hop4_w_t dsnh;

    CTC_PTR_VALID_CHECK(p_offset_attr);
    sal_memset(&dsnh, 0, sizeof(ds_next_hop4_w_t));

    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc_with_multiple(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, 1, 2, &nhp_ptr));

    p_offset_attr->offset_base = nhp_ptr;
    p_offset_attr->entry_size = 1;
    p_offset_attr->entry_num = 1;

    SYS_GREATBELT_NH_DSNH_SET_DEFAULT_COS_ACTION(dsnh);
    dsnh.is_nexthop   = TRUE;
    dsnh.svlan_tagged = TRUE;
    dsnh.payload_operation = SYS_NH_OP_BRIDGE;
    dsnh.dest_vlan_ptr = SYS_DSNH_DESTVLANPTR_SPECIAL;

    CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, nhp_ptr, &dsnh));
    return CTC_E_NONE;
}



/**
 @brief This function is used to init resolved dsnexthop for egress bypass
 */
STATIC int32
_sys_greatbelt_nh_dsnh_init_for_bypass(uint8 lchip, sys_nh_offset_attr_t* p_offset_attr)
{

    uint32 cmd;
    uint32 nhp_ptr = 0;
    ds_next_hop8_w_t dsnh8w;

    epe_next_hop_ptr_cam_t epe_next_hop_ptr_cam;

    CTC_PTR_VALID_CHECK(p_offset_attr);

    nhp_ptr = (SYS_GREATBELT_DSNH_INTERNAL_BASE << SYS_GREATBELT_DSNH_INTERNAL_SHIFT)
        | SYS_DSNH4WREG_INDEX_FOR_BYPASS;

    p_offset_attr->offset_base = nhp_ptr;
    p_offset_attr->entry_size = 1;
    p_offset_attr->entry_num = 1;

    sal_memset(&epe_next_hop_ptr_cam, 0, sizeof(epe_next_hop_ptr_cam));
    epe_next_hop_ptr_cam.pointer_cam_mask0  = 0x1FFFF;
    epe_next_hop_ptr_cam.pointer_cam_value0 = nhp_ptr;
    epe_next_hop_ptr_cam.pointer_cam_mask1  = 0x1FFFF;
    epe_next_hop_ptr_cam.pointer_cam_value1 = 0x1FFFF;

    cmd = DRV_IOW(EpeNextHopPtrCam_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_next_hop_ptr_cam));

    sal_memset(&dsnh8w, 0, sizeof(ds_next_hop8_w_t));

    cmd = DRV_IOR(EpeNextHopInternal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ((nhp_ptr&0x3)>>1), cmd, &dsnh8w));

    dsnh8w.is_nexthop   = 1;
    dsnh8w.is_nexthop8_w = 1;

    cmd = DRV_IOW(EpeNextHopInternal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ((nhp_ptr&0x3)>>1), cmd, &dsnh8w));

    return CTC_E_NONE;
}

/**
 @brief This function is used to init resolved dsnexthop for mirror
 */
STATIC int32
_sys_greatbelt_nh_dsnh_init_for_mirror(uint8 lchip, sys_nh_offset_attr_t* p_offset_attr)
{

    uint32 cmd;
    uint32 nhp_ptr = 0;
    ds_next_hop4_w_t dsnh;
    ds_next_hop8_w_t dsnh8w;

    CTC_PTR_VALID_CHECK(p_offset_attr);
    sal_memset(&dsnh, 0, sizeof(ds_next_hop4_w_t));
    sal_memset(&dsnh8w, 0, sizeof(ds_next_hop8_w_t));

    nhp_ptr = (SYS_GREATBELT_DSNH_INTERNAL_BASE << SYS_GREATBELT_DSNH_INTERNAL_SHIFT)
        | SYS_DSNH4WREG_INDEX_FOR_MIRROR;

    p_offset_attr->offset_base = nhp_ptr;
    p_offset_attr->entry_size = 1;
    p_offset_attr->entry_num = 1;

    dsnh.vlan_xlate_mode = 1;
    dsnh.payload_operation = SYS_NH_OP_MIRROR;
    dsnh.is_nexthop = 1;
    dsnh.tagged_mode = 1;

    cmd = DRV_IOR(EpeNextHopInternal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ((nhp_ptr&0x3)>>1), cmd, &dsnh8w));

    if(CTC_IS_BIT_SET(nhp_ptr, 0))
    {
        sal_memcpy(((uint8*)&dsnh8w + sizeof(ds_next_hop4_w_t)), &dsnh, sizeof(ds_next_hop4_w_t));
    }
    else
    {
        sal_memcpy(&dsnh8w, &dsnh, sizeof(ds_next_hop4_w_t));
    }

    cmd = DRV_IOW(EpeNextHopInternal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ((nhp_ptr&0x3)>>1), cmd, &dsnh8w));
    return CTC_E_NONE;
}

/**
 @brief This function is used to init resolved l2edit  for ipmc
 */
STATIC int32
_sys_greatbelt_nh_l2edit_init_for_ipmc(uint8 lchip, uint16* p_offset_attr)
{
    uint32 cmd;
    uint32 nhp_ptr = 0;
    ds_l2_edit_eth4_w_t ds_l2_edit_4w;

    sal_memset(&ds_l2_edit_4w, 0, sizeof(ds_l2_edit_eth4_w_t));
    ds_l2_edit_4w.derive_mcast_mac = 1;
    ds_l2_edit_4w.ds_type  = SYS_NH_DS_TYPE_L2EDIT;
    ds_l2_edit_4w.l2_rewrite_type  = SYS_NH_L2EDIT_TYPE_ETH_4W;

    sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 1,
                                  &nhp_ptr);

    cmd = DRV_IOW(DsL2EditEth4W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, nhp_ptr, cmd, &ds_l2_edit_4w));

    *p_offset_attr = nhp_ptr;

    return CTC_E_NONE;
}

/**
 This function is used to init resolved dsFwd for loopback to itself
 */
STATIC int32
_sys_greatbelt_nh_dsfwd_init_for_reflective(uint8 lchip, uint16* p_offset_attr)
{
    uint32 dsfwd_ptr = 0;
    uint32 nhp_ptr = 0;
    ds_fwd_t dsfwd;

    nhp_ptr = (SYS_GREATBELT_DSNH_INTERNAL_BASE << SYS_GREATBELT_DSNH_INTERNAL_SHIFT)
        | SYS_DSNH4WREG_INDEX_FOR_BYPASS;

    sal_memset(&dsfwd, 0, sizeof(ds_fwd_t));
    dsfwd.force_back_en = 1;
    dsfwd.next_hop_ptr = nhp_ptr & 0xFFFF;
    dsfwd.next_hop_ext = 1;
    sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,
                                  &dsfwd_ptr);

    CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_FWD,
                                                       dsfwd_ptr, &dsfwd));
    *p_offset_attr = dsfwd_ptr;

    return CTC_E_NONE;
}

/**
 @brief This function is used to initilize dynamic offset, inclue offset pool,
        and resolved offset
*/
STATIC int32
_sys_greatbelt_nh_offset_init(uint8 lchip, sys_greatbelt_nh_master_t* p_master)
{
    sys_greatbelt_opf_t opf;
    uint32 tbl_entry_num[4] = {0};
    uint32 glb_tbl_entry_num[4] = {0};
    uint8  dyn_tbl_idx = 0;
    uint8  max_dyn_tbl_idx = 0;
    uint32  entry_enum = 0, nh_glb_entry_num = 0;
    uint8   dyn_tbl_opf_type = 0;
    uint8  loop = 0;
    uint32 rsv_dsmet_base = 0;
    uint32 rsv_dsnh_base  = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);


    sys_greatbelt_ftm_get_dynamic_table_info(lchip, DsFwd_t,  &dyn_tbl_idx, &entry_enum, &nh_glb_entry_num);
    dyn_tbl_opf_type  = OPF_NH_DYN1_SRAM + dyn_tbl_idx;
    nh_table_info_array[SYS_NH_ENTRY_TYPE_FWD].opf_pool_type = dyn_tbl_opf_type;
    tbl_entry_num[dyn_tbl_idx]  = entry_enum;
    max_dyn_tbl_idx = dyn_tbl_idx;

    sys_greatbelt_ftm_get_dynamic_table_info(lchip, DsMetEntry_t,  &dyn_tbl_idx, &entry_enum, &nh_glb_entry_num);
    dyn_tbl_opf_type  = OPF_NH_DYN1_SRAM + dyn_tbl_idx;
    nh_table_info_array[SYS_NH_ENTRY_TYPE_MET].opf_pool_type = dyn_tbl_opf_type;
    tbl_entry_num[dyn_tbl_idx]  = entry_enum;
    glb_tbl_entry_num[dyn_tbl_idx]  = nh_glb_entry_num;
    max_dyn_tbl_idx = (dyn_tbl_idx > max_dyn_tbl_idx) ? dyn_tbl_idx : max_dyn_tbl_idx;

    sys_greatbelt_ftm_get_dynamic_table_info(lchip, DsNextHop4W_t,  &dyn_tbl_idx, &entry_enum, &nh_glb_entry_num);
    dyn_tbl_opf_type  = OPF_NH_DYN1_SRAM + dyn_tbl_idx;
    nh_table_info_array[SYS_NH_ENTRY_TYPE_NEXTHOP_4W].opf_pool_type = dyn_tbl_opf_type;
    nh_table_info_array[SYS_NH_ENTRY_TYPE_NEXTHOP_8W].opf_pool_type = dyn_tbl_opf_type;
    tbl_entry_num[dyn_tbl_idx]  = entry_enum;
    if (p_master->pkt_nh_edit_mode != SYS_NH_IGS_CHIP_EDIT_MODE)
    {
        glb_tbl_entry_num[dyn_tbl_idx]  = nh_glb_entry_num;
    }

    max_dyn_tbl_idx = (dyn_tbl_idx > max_dyn_tbl_idx) ? dyn_tbl_idx : max_dyn_tbl_idx;

    sys_greatbelt_ftm_get_dynamic_table_info(lchip, DsL2EditEth4W_t,  &dyn_tbl_idx, &entry_enum, &nh_glb_entry_num);
    dyn_tbl_opf_type  = OPF_NH_DYN1_SRAM + dyn_tbl_idx;
    tbl_entry_num[dyn_tbl_idx]  = entry_enum;
    max_dyn_tbl_idx = (dyn_tbl_idx > max_dyn_tbl_idx) ? dyn_tbl_idx : max_dyn_tbl_idx;

    for (loop = SYS_NH_ENTRY_TYPE_L2EDIT_FROM; loop <= SYS_NH_ENTRY_TYPE_L3EDIT_TO; loop++)
    {
        nh_table_info_array[loop].opf_pool_type = dyn_tbl_opf_type;
    }

    /*Init  global dsnexthop & dsMet offset*/
    dyn_tbl_idx = nh_table_info_array[SYS_NH_ENTRY_TYPE_NEXTHOP_4W].opf_pool_type - OPF_NH_DYN1_SRAM;
    if (p_master->pkt_nh_edit_mode == SYS_NH_IGS_CHIP_EDIT_MODE || (glb_tbl_entry_num[dyn_tbl_idx ] == 0))
    {
        p_master->max_glb_nh_offset  = 0;
    }
    else
    {
        p_master->max_glb_nh_offset  = glb_tbl_entry_num[dyn_tbl_idx ];
        p_master->p_occupid_nh_offset_bmp = (uint32*)mem_malloc(MEM_NEXTHOP_MODULE,
                                                                (sizeof(uint32) * (p_master->max_glb_nh_offset / BITS_NUM_OF_WORD + 1)));
        if (NULL == p_master->p_occupid_nh_offset_bmp)
        {
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_master->p_occupid_nh_offset_bmp, 0,  (sizeof(uint32) * (p_master->max_glb_nh_offset / BITS_NUM_OF_WORD + 1)));

    }

    dyn_tbl_idx =nh_table_info_array[SYS_NH_ENTRY_TYPE_MET].opf_pool_type - OPF_NH_DYN1_SRAM;
    p_master->max_glb_met_offset  = glb_tbl_entry_num[dyn_tbl_idx ];
    if (p_master->max_glb_met_offset)
    {
        p_master->p_occupid_met_offset_bmp = (uint32*)mem_malloc(MEM_NEXTHOP_MODULE, (sizeof(uint32) * (p_master->max_glb_met_offset / BITS_NUM_OF_WORD + 1)));
        if (NULL == p_master->p_occupid_met_offset_bmp)
        {
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_master->p_occupid_met_offset_bmp, 0,  (sizeof(uint32) * (p_master->max_glb_met_offset / BITS_NUM_OF_WORD + 1)));
        /*rsv group 0 for stacking keeplive group*/
        CTC_BIT_SET(p_master->p_occupid_met_offset_bmp[0], 0);
    }

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    for (loop = 0; loop <= max_dyn_tbl_idx; loop++)
    {
        CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_NH_DYN1_SRAM + loop, 1));
        dyn_tbl_idx = loop;


        /*all nexthop opf min offset should be greater than 1*/

        if (0 == glb_tbl_entry_num[dyn_tbl_idx])
        {
            glb_tbl_entry_num[dyn_tbl_idx]++;
        }
        tbl_entry_num[dyn_tbl_idx] = tbl_entry_num[dyn_tbl_idx] - glb_tbl_entry_num[dyn_tbl_idx];

        opf.pool_type = OPF_NH_DYN1_SRAM + loop;
        opf.pool_index = 0;

        CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, glb_tbl_entry_num[dyn_tbl_idx], tbl_entry_num[dyn_tbl_idx]));

    }

    sys_greatbelt_ftm_get_dyn_entry_size(lchip, SYS_FTM_DYN_ENTRY_LOGIC_MET, &entry_enum);
    p_master->ipmc_bitmap_met_num = entry_enum;

    /*reserved DsMet & DsNexthop for ipmc port bitmap*/
    if (p_master->ipmc_bitmap_met_num)
    {
        int32 l3_mcast_met_nexthop_base = 0;

        opf.pool_type = nh_table_info_array[SYS_NH_ENTRY_TYPE_NEXTHOP_4W].opf_pool_type;
        opf.pool_index = 0;
        opf.reverse = 1;
        sys_greatbelt_opf_alloc_offset(lchip, &opf, p_master->ipmc_bitmap_met_num, &rsv_dsnh_base);

        opf.reverse = 0;
        opf.pool_type = nh_table_info_array[SYS_NH_ENTRY_TYPE_MET].opf_pool_type;
        opf.pool_index = 0;
        sys_greatbelt_opf_alloc_offset(lchip, &opf, p_master->ipmc_bitmap_met_num, &rsv_dsmet_base);


        l3_mcast_met_nexthop_base = rsv_dsnh_base - rsv_dsmet_base;
        if (l3_mcast_met_nexthop_base < 0)
        {
            p_master->ipmc_bitmap_met_num = 0;

            opf.pool_type = nh_table_info_array[SYS_NH_ENTRY_TYPE_MET].opf_pool_type;
            opf.pool_index = 0;
            sys_greatbelt_opf_free_offset(lchip, &opf, p_master->ipmc_bitmap_met_num, rsv_dsnh_base);

            opf.pool_type = nh_table_info_array[SYS_NH_ENTRY_TYPE_NEXTHOP_4W].opf_pool_type;
            opf.pool_index = 0;
            sys_greatbelt_opf_free_offset(lchip, &opf, p_master->ipmc_bitmap_met_num, rsv_dsmet_base);

        }
        else
        {

            met_fifo_ctl_t  met_fifo_ctl;
            uint32 cmd = 0;

            sal_memset(&met_fifo_ctl, 0, sizeof(met_fifo_ctl_t));

            sys_greatbelt_opf_init(lchip, OPF_NH_DSMET_FOR_IPMCBITMAP, 1);

            opf.pool_type = OPF_NH_DSMET_FOR_IPMCBITMAP;
            opf.pool_index = 0;
            /*all nexthop  opf min offset should be greater than 1*/

            sys_greatbelt_opf_init_offset(lchip, &opf, rsv_dsmet_base, p_master->ipmc_bitmap_met_num);

            cmd = DRV_IOR(MetFifoCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &met_fifo_ctl));

            cmd = DRV_IOW(MetFifoCtl_t, DRV_ENTRY_FLAG);
            met_fifo_ctl.l3_mcast_met_min_ptr0 = rsv_dsmet_base - 1;
            met_fifo_ctl.l3_mcast_mode_en = 1;
            met_fifo_ctl.l3_mcast_met_max_ptr0 = rsv_dsmet_base + p_master->ipmc_bitmap_met_num;
            met_fifo_ctl.l3_mcast_met_ptr_valid0 = 1;
            met_fifo_ctl.l3_mcast_met_ptr_valid1 = 0;
            met_fifo_ctl.l3_mcast_met_nexthop_base = l3_mcast_met_nexthop_base;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &met_fifo_ctl));


            p_master->ipmc_bitmap_nh_base = l3_mcast_met_nexthop_base;
        }
    }


    /*1. DsNexthop offset for bridge, */
    CTC_ERROR_RETURN(_sys_greatbelt_nh_dsnh_init_for_brg(lchip,
                                                         &(p_master->sys_nh_resolved_offset \
                                                         [SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH])));

    /*2. DsNexthop for bypassall*/
    CTC_ERROR_RETURN(_sys_greatbelt_nh_dsnh_init_for_bypass(lchip,
                                                            &(p_master->sys_nh_resolved_offset \
                                                            [SYS_NH_RES_OFFSET_TYPE_BYPASS_NH])));

    /*3. DsNexthop for mirror*/
    CTC_ERROR_RETURN(_sys_greatbelt_nh_dsnh_init_for_mirror(lchip,
                                                            &(p_master->sys_nh_resolved_offset \
                                                            [SYS_NH_RES_OFFSET_TYPE_MIRROR_NH])));

    /*4. l2edit for ipmc phy if*/
    CTC_ERROR_RETURN(_sys_greatbelt_nh_l2edit_init_for_ipmc(lchip,
                                                            &(p_master->ipmc_resolved_l2edit)));

    /*5. dsfwd for reflective (loopback)*/
    CTC_ERROR_RETURN(_sys_greatbelt_nh_dsfwd_init_for_reflective(lchip,
                                                                 &(p_master->reflective_resolved_dsfwd_offset)));

    /*6. DsNexthop offset for oam bridge*/
    CTC_ERROR_RETURN(_sys_greatbelt_nh_dsnh_init_for_oam_brg(lchip,
                                                             &(p_master->sys_nh_resolved_offset \
                                                             [SYS_NH_RES_OFFSET_TYPE_OAM_BRIDGE_NH])));



    /* init for ip tunnel*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_ip_tunnel_init(lchip));
    return CTC_E_NONE;
}

/**
 @brief This function is used to get fatal exception dsnexthop offset
 */
int32
sys_greatbelt_nh_get_fatal_excp_dsnh_offset(uint8 lchip, uint32* p_offset)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_PTR_VALID_CHECK(p_offset);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(p_offset);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    *p_offset = p_gb_nh_master->fatal_excp_base;

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_check_glb_nh_offset(uint8 lchip, uint32 start_offset, uint32 entry_num,
                                     bool should_not_inuse)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    uint32* p_bmp, curr_offset;
    uint32 i;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    if (NULL == (p_bmp = p_gb_nh_master->p_occupid_nh_offset_bmp))
    {
        return CTC_E_NOT_INIT;
    }

    if (0 == entry_num)
    {
        return CTC_E_NONE;
    }

    for (i = 0; i < entry_num; i++)
    {
        curr_offset = start_offset + i;
        if (should_not_inuse && CTC_FLAG_ISSET(p_bmp[((curr_offset) >> BITS_SHIFT_OF_WORD)],
                                               (1 << (curr_offset & BITS_MASK_OF_WORD))))
        {
            return CTC_E_NH_GLB_SRAM_IS_INUSE;
        }

        if ((!should_not_inuse) && (!CTC_FLAG_ISSET(p_bmp[((curr_offset) >> BITS_SHIFT_OF_WORD)],
                                                    (1 << (curr_offset & BITS_MASK_OF_WORD)))))
        {
            return CTC_E_NH_GLB_SRAM_ISNOT_INUSE;
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_set_glb_nh_offset(uint8 lchip, uint32 start_offset, uint32 entry_num, bool is_set)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    uint32* p_bmp, curr_offset;
    int32 i;

    if (0 == entry_num)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    if ((start_offset + entry_num - 1) > p_gb_nh_master->max_glb_nh_offset)
    {
        return CTC_E_NH_GLB_SRAM_INDEX_EXCEED;
    }

    if (NULL == (p_bmp = p_gb_nh_master->p_occupid_nh_offset_bmp))
    {
        return CTC_E_NOT_INIT;
    }

    for (i = 0; i < entry_num; i++)
    {
        curr_offset = start_offset + i;
        if (is_set)
        {
            CTC_SET_FLAG(p_bmp[(curr_offset >> BITS_SHIFT_OF_WORD)],
                         (1 << (curr_offset & BITS_MASK_OF_WORD)));
        }
        else
        {
            CTC_UNSET_FLAG(p_bmp[(curr_offset >> BITS_SHIFT_OF_WORD)],
                           (1 << (curr_offset & BITS_MASK_OF_WORD)));
        }
    }
	if(is_set)
	{
	     p_gb_nh_master->glb_nh_used_cnt += entry_num;
	}
	else if(p_gb_nh_master->glb_nh_used_cnt >= entry_num)
	{
	  	  p_gb_nh_master->glb_nh_used_cnt  -= entry_num;
	}
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_set_glb_met_offset(uint8 lchip, uint32 start_offset, uint32 entry_num, bool is_set)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    uint32* p_bmp, curr_offset;
    int32 i;

    if (0 == entry_num)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    if ((start_offset + entry_num - 1) > p_gb_nh_master->max_glb_met_offset)
    {
        return CTC_E_NH_GLB_SRAM_INDEX_EXCEED;
    }

    if (NULL == (p_bmp = p_gb_nh_master->p_occupid_met_offset_bmp))
    {
        return CTC_E_NOT_INIT;
    }

    for (i = 0; i < entry_num; i++)
    {
        curr_offset = start_offset + i;
        if (is_set)
        {
            CTC_SET_FLAG(p_bmp[(curr_offset >> BITS_SHIFT_OF_WORD)],
                         (1 << (curr_offset & BITS_MASK_OF_WORD)));
        }
        else
        {
            CTC_UNSET_FLAG(p_bmp[(curr_offset >> BITS_SHIFT_OF_WORD)],
                           (1 << (curr_offset & BITS_MASK_OF_WORD)));
        }
    }
   if(is_set)
	{
	     p_gb_nh_master->glb_met_used_cnt += entry_num;
	}
	else if(p_gb_nh_master->glb_met_used_cnt >= entry_num)
	{
	  	  p_gb_nh_master->glb_met_used_cnt  -= entry_num;
	}
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_check_glb_met_offset(uint8 lchip, uint32 start_offset, uint32 entry_num,
                                      bool should_not_inuse)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    uint32* p_bmp, curr_offset;
    uint32 i;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    if (NULL == (p_bmp = p_gb_nh_master->p_occupid_met_offset_bmp))
    {
        return CTC_E_NOT_INIT;
    }

    if (0 == entry_num)
    {
        return CTC_E_NONE;
    }

    for (i = 0; i < entry_num; i++)
    {
        curr_offset = start_offset + i;
        if (should_not_inuse && CTC_FLAG_ISSET(p_bmp[((curr_offset) >> BITS_SHIFT_OF_WORD)],
                                               (1 << (curr_offset & BITS_MASK_OF_WORD))))
        {
            return CTC_E_NH_GLB_SRAM_IS_INUSE;
        }

        if ((!should_not_inuse) && (!CTC_FLAG_ISSET(p_bmp[((curr_offset) >> BITS_SHIFT_OF_WORD)],
                                                    (1 << (curr_offset & BITS_MASK_OF_WORD)))))
        {
            return CTC_E_NH_GLB_SRAM_ISNOT_INUSE;
        }
    }

    return CTC_E_NONE;
}

/**
 @brief This function is used to get nexthop module master data
 */
bool
_sys_greatbelt_nh_is_ipmc_logic_rep_enable(uint8 lchip)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    return p_gb_nh_master->ipmc_logic_replication ? TRUE : FALSE;
}

bool
sys_greatbelt_nh_is_ipmc_bitmap_enable(uint8 lchip)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    return p_gb_nh_master->ipmc_bitmap_met_num ? TRUE : FALSE;
}

int32
sys_greatbelt_nh_get_ipmc_bitmap_dsnh_offset(uint8 lchip, uint32 met_offset, uint32* dsnh_offset)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    *dsnh_offset = met_offset + p_gb_nh_master->ipmc_bitmap_nh_base;
    return CTC_E_NONE;
}

/**
 @brief This function is used to Write/Read asic table
 */
int32
sys_greatbelt_nh_write_asic_table(uint8 lchip,
                                  sys_nh_entry_table_type_t table_type, uint32 offset, void* value)
{

    uint32 cmd;
    uint32 tbl_id;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip = %d, table_type = %d, offset = %d\n",
                   lchip, table_type, offset);

    /*Sanity check*/
    CTC_PTR_VALID_CHECK(value);

    tbl_id = nh_table_info_array[table_type].table_id;
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    if (DRV_E_NONE != DRV_IOCTL(lchip, offset, cmd, value))
    {
        return CTC_E_HW_OP_FAIL;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_get_asic_table(uint8 lchip,
                                sys_nh_entry_table_type_t table_type, uint32 offset, void* value)
{

    uint32 cmd;
    uint32 tbl_id;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip = %d, table_type = %d, offset = %d\n",
                   lchip, table_type, offset);

    /*Sanity check*/
    CTC_PTR_VALID_CHECK(value);

    tbl_id = nh_table_info_array[table_type].table_id;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    if (DRV_E_NONE != DRV_IOCTL(lchip, offset, cmd, value))
    {
        return CTC_E_HW_OP_FAIL;
    }

    return CTC_E_NONE;
}

/**
 @brief This function is used to build and write dsl2edit eth4w
 */
STATIC int32
_sys_greatbelt_nh_write_entry_dsl2editeth4w(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_ds_l2_edit_4w)
{
    uint32 cmd;
    ds_l2_edit_eth4_w_t ds_l2_edit_4w;

    mac_addr_t mac_da;
    sal_memset(&mac_da, 0, sizeof(mac_addr_t));

    sal_memset(&ds_l2_edit_4w, 0, sizeof(ds_l2_edit_eth4_w_t));
    ds_l2_edit_4w.ds_type  = SYS_NH_DS_TYPE_L2EDIT;
    ds_l2_edit_4w.l2_rewrite_type  = SYS_NH_L2EDIT_TYPE_ETH_4W;
    ds_l2_edit_4w.mac_da47_32  = p_ds_l2_edit_4w->mac_da[0] << 8 | p_ds_l2_edit_4w->mac_da[1];
    ds_l2_edit_4w.mac_da31_30  = (p_ds_l2_edit_4w->mac_da[2] >> 6);
    ds_l2_edit_4w.mac_da29_0   = ((p_ds_l2_edit_4w->mac_da[2] & 0x3F) << 24) | p_ds_l2_edit_4w->mac_da[3] << 16 | p_ds_l2_edit_4w->mac_da[4] << 8 | p_ds_l2_edit_4w->mac_da[5];

    if (0 == sal_memcmp(p_ds_l2_edit_4w->mac_da, mac_da, sizeof(mac_addr_t)))
    {
        ds_l2_edit_4w.derive_mcast_mac = 1;
    }

    if(p_ds_l2_edit_4w->output_vid)
    {
        ds_l2_edit_4w.output_vlan_id_valid      = 1;
        ds_l2_edit_4w.output_vlan_id_is_svlan   = p_ds_l2_edit_4w->output_vlan_is_svlan;
        ds_l2_edit_4w.output_vlan_id            = p_ds_l2_edit_4w->output_vid;
    }
    else if (p_ds_l2_edit_4w->phy_if)
    {
         ds_l2_edit_4w.output_vlan_id            = 0xfff;
    }

    if(p_ds_l2_edit_4w->packet_type)
    {
        ds_l2_edit_4w.packet_type           = p_ds_l2_edit_4w->packet_type;
        ds_l2_edit_4w.overwrite_ether_type  = 1;
    }

    cmd = DRV_IOW(DsL2EditEth4W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_ds_l2_edit_4w->offset, cmd, &ds_l2_edit_4w));
    return CTC_E_NONE;

}

/**
 @brief This function is used to build and write dsl2edit eth8w
 */
STATIC int32
_sys_greatbelt_nh_write_entry_dsl2editeth8w(uint8 lchip, sys_nh_db_dsl2editeth8w_t* p_ds_l2_edit_8w)
{
    uint32 cmd;
    ds_l2_edit_eth8_w_t ds_l2_edit_8w;

    sal_memset(&ds_l2_edit_8w, 0, sizeof(ds_l2_edit_eth8_w_t));
    ds_l2_edit_8w.ds_type  = SYS_NH_DS_TYPE_L2EDIT;
    ds_l2_edit_8w.l2_rewrite_type  = SYS_NH_L2EDIT_TYPE_ETH_8W;
    ds_l2_edit_8w.mac_da47_32  = p_ds_l2_edit_8w->mac_da[0] << 8 | p_ds_l2_edit_8w->mac_da[1];
    ds_l2_edit_8w.mac_da31_30  = (p_ds_l2_edit_8w->mac_da[2] >> 6);
    ds_l2_edit_8w.mac_da29_0   = ((p_ds_l2_edit_8w->mac_da[2] & 0x3F) << 24) | p_ds_l2_edit_8w->mac_da[3] << 16 | p_ds_l2_edit_8w->mac_da[4] << 8 | p_ds_l2_edit_8w->mac_da[5];
    ds_l2_edit_8w.ip_da31_0 = p_ds_l2_edit_8w->ip0_31;
    ds_l2_edit_8w.ip_da63_32 = p_ds_l2_edit_8w->ip32_63;
    ds_l2_edit_8w.ip_da78_64 = p_ds_l2_edit_8w->ip64_79 & 0x7FFF;
    ds_l2_edit_8w.ip_da79_79 = (p_ds_l2_edit_8w->ip64_79 >> 15) & 0x1;
    if(p_ds_l2_edit_8w->output_vid)
    {
        ds_l2_edit_8w.output_vlan_id_valid      = 1;
        ds_l2_edit_8w.output_vlan_id_is_svlan   = p_ds_l2_edit_8w->output_vlan_is_svlan;
        ds_l2_edit_8w.output_vlan_id            = p_ds_l2_edit_8w->output_vid;
    }

    if(p_ds_l2_edit_8w->packet_type)
    {
        ds_l2_edit_8w.packet_type           = p_ds_l2_edit_8w->packet_type;
        ds_l2_edit_8w.overwrite_ether_type  = 1;
    }
    cmd = DRV_IOW(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_ds_l2_edit_8w->offset, cmd, &ds_l2_edit_8w));
    return CTC_E_NONE;

}



int32
sys_greatbelt_nh_write_entry_arp(uint8 lchip, sys_nh_db_arp_t* p_ds_arp)
{
    uint32 cmd;

    ds_l2_edit_eth4_w_t ds_l2_edit_4w;
    ds_l2_edit_loopback_t l2edit_loopback;
    void *p_edit = NULL;

    mac_addr_t mac_da;
    sal_memset(&mac_da, 0, sizeof(mac_addr_t));

    sal_memset(&ds_l2_edit_4w, 0, sizeof(ds_l2_edit_eth4_w_t));
    sal_memset(&l2edit_loopback, 0, sizeof(ds_l2_edit_loopback_t));

    if (CTC_FLAG_ISSET(p_ds_arp->flag, SYS_NH_ARP_FLAG_DROP))
    {
        ds_l2_edit_4w.ds_type  = SYS_NH_DS_TYPE_DISCARD;
    }
    else
    {
        ds_l2_edit_4w.ds_type  = SYS_NH_DS_TYPE_L2EDIT;
    }

    if (CTC_FLAG_ISSET(p_ds_arp->flag, SYS_NH_ARP_FLAG_REDIRECT_TO_CPU))
    {
        uint32 lb_dest_map = 0;
        cmd = DRV_IOW(DsL2EditLoopback_t, DRV_ENTRY_FLAG);
        l2edit_loopback.ds_type                 = SYS_NH_DS_TYPE_L2EDIT;
        l2edit_loopback.l2_rewrite_type         = SYS_NH_L2EDIT_TYPE_LOOPBACK;
        l2edit_loopback.lb_length_adjust_type   = 0;
        l2edit_loopback.lb_next_hop_ext         = 0;
        sys_greatbelt_cpu_reason_get_info(lchip,  CTC_PKT_CPU_REASON_ARP_MISS, &lb_dest_map);
        l2edit_loopback.lb_dest_map             = lb_dest_map;
        l2edit_loopback.lb_next_hop_ptr         = CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_ARP_MISS, 0);

        p_edit = &l2edit_loopback;
    }
    else
    {
        ds_l2_edit_4w.l2_rewrite_type  = SYS_NH_L2EDIT_TYPE_ETH_4W;
        ds_l2_edit_4w.mac_da47_32  = p_ds_arp->mac_da[0] << 8 | p_ds_arp->mac_da[1];
        ds_l2_edit_4w.mac_da31_30  = (p_ds_arp->mac_da[2] >> 6);
        ds_l2_edit_4w.mac_da29_0   = ((p_ds_arp->mac_da[2] & 0x3F) << 24) | p_ds_arp->mac_da[3] << 16 | p_ds_arp->mac_da[4] << 8 | p_ds_arp->mac_da[5];

        if(CTC_FLAG_ISSET(p_ds_arp->flag, SYS_NH_ARP_VLAN_VALID))
        {
            if (p_ds_arp->output_vid)
            {
                ds_l2_edit_4w.output_vlan_id_valid      = 1;
                ds_l2_edit_4w.output_vlan_id_is_svlan   = 1;
                ds_l2_edit_4w.output_vlan_id            = p_ds_arp->output_vid;
            }
            else
            {
                ds_l2_edit_4w.output_vlan_id  = 0xFFF;
            }

        }

        if(p_ds_arp->packet_type)
        {
            ds_l2_edit_4w.packet_type           = p_ds_arp->packet_type;
            ds_l2_edit_4w.overwrite_ether_type  = 1;
        }
        cmd = DRV_IOW(DsL2EditEth4W_t, DRV_ENTRY_FLAG);
        p_edit = &ds_l2_edit_4w;
    }


    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_ds_arp->offset, cmd, p_edit));


    return CTC_E_NONE;

}



STATIC int32
sys_greatbelt_nh_dsnh_build_vlan_info(uint8 lchip, ds_next_hop4_w_t* p_dsnh,
                                      ctc_vlan_egress_edit_info_t* p_vlan_info)
{
    if (!p_vlan_info)
    {
        return CTC_E_INVALID_PTR;
    }

    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_VLAN_SWAP_EN))
    {
        /*1. cvlan_tag_op_disable = 1*/
        p_dsnh->replace_ctag_cos = FALSE;
        p_dsnh->copy_ctag_cos = TRUE;

        /*2. svlan_tag_op_disable = 1*/
        p_dsnh->derive_stag_cos = TRUE;
        p_dsnh->stag_cos  = 1 << 1;

        /*3. tagged_mode = 1*/
        p_dsnh->tagged_mode = TRUE;

        p_dsnh->vlan_xlate_mode = 1; /*humber mode*/

        /*Unset  parametre's cvid valid bit, other with output
        cvlan id valid will be overwrited in function
        sys_greatbelt_nh_dsnh4w_assign_vid, bug11323*/
        CTC_UNSET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID);
        CTC_UNSET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID);

        /*Swap TPID*/
        if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_TPID_SWAP_EN))
        {
            p_dsnh->output_svlan_id_valid = TRUE;
        }

        /*Swap Cos*/
        if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_COS_SWAP_EN))
        {
            p_dsnh->output_cvlan_id_valid = TRUE;
        }
    }
    else
    {
    #if 0
        if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_NH_SVLAN_TPID))
        {
            CTC_MAX_VALUE_CHECK(p_vlan_info->svlan_tpid_index, 3);
            p_dsnh->svlan_tpid_en = TRUE;
            p_dsnh->svlan_tpid = p_vlan_info->svlan_tpid_index;
        }
    #endif

        switch (p_vlan_info->svlan_edit_type)
        {
        case CTC_VLAN_EGRESS_EDIT_NONE:
        case CTC_VLAN_EGRESS_EDIT_KEEP_VLAN_UNCHANGE:
            p_dsnh->svlan_tagged = TRUE;
            p_dsnh->output_svlan_id_valid = FALSE;
            p_dsnh->vlan_xlate_mode = 0 ; /*GB mode*/
            p_dsnh->derive_stag_cos  = 0;
            CTC_UNSET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID);
            break;
        case CTC_VLAN_EGRESS_EDIT_INSERT_VLAN:
            if (CTC_VLAN_EGRESS_EDIT_REPLACE_VLAN == p_vlan_info->cvlan_edit_type ||
                CTC_VLAN_EGRESS_EDIT_NONE == p_vlan_info->cvlan_edit_type)
            {
                return CTC_E_NH_VLAN_EDIT_CONFLICT;
            }

            p_dsnh->tagged_mode = TRUE;
            p_dsnh->svlan_tagged = TRUE;
            p_dsnh->stag_cfi  = 0;
            p_dsnh->derive_stag_cos  = 1;
            p_dsnh->vlan_xlate_mode = 1 ; /*humber mode*/
            break;

        case CTC_VLAN_EGRESS_EDIT_REPLACE_VLAN:
            p_dsnh->svlan_tagged = TRUE;
            p_dsnh->stag_cfi  = 0;
            p_dsnh->derive_stag_cos  = 1;
            p_dsnh->vlan_xlate_mode = 1 ; /*humber mode*/
            break;

        case CTC_VLAN_EGRESS_EDIT_STRIP_VLAN:
            p_dsnh->svlan_tagged = FALSE;
            p_dsnh->vlan_xlate_mode = 1 ;
            break;

        default:
            return CTC_E_NH_INVALID_VLAN_EDIT_TYPE;
        }

       /* stag cos opreation */
        if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_REPLACE_SVLAN_COS))
        {
           /* when not derive, new stag cos is DsNextHop.stag_cos*/
           p_dsnh->derive_stag_cos = 0;
           p_dsnh->stag_cfi = 0;
           p_dsnh->stag_cos = p_vlan_info->stag_cos;
           p_dsnh->vlan_xlate_mode = 1;

        }
        else if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_MAP_SVLAN_COS))
        {
            /* new stag cos is mapped from priority.*/
            p_dsnh->derive_stag_cos = 1;
            p_dsnh->stag_cfi = 1;
            p_dsnh->vlan_xlate_mode = 1;
        }

        switch (p_vlan_info->cvlan_edit_type)
        {
        case CTC_VLAN_EGRESS_EDIT_NONE:
              /*Don't use DsNexthop's output cvlanid*/
            /*if s-tag is humber mode ,c-tag will use humber mode*/
            if(p_dsnh->vlan_xlate_mode)
            {
                p_dsnh->copy_ctag_cos = 0;
                p_dsnh->replace_ctag_cos = 0;
            }
            else
            {
                p_dsnh->copy_ctag_cos = 0;
            }
            p_dsnh->output_cvlan_id_valid = FALSE;
            CTC_UNSET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID);
            break;
        case CTC_VLAN_EGRESS_EDIT_KEEP_VLAN_UNCHANGE:
            /*Don't use DsNexthop's output cvlanid*/
            /*if s-tag is humber mode ,c-tag will use humber mode*/
            if(p_dsnh->vlan_xlate_mode)
            {
                p_dsnh->copy_ctag_cos = 1;
                p_dsnh->replace_ctag_cos = 0;
            }
            else
            {
                p_dsnh->copy_ctag_cos = 0;
            }
            p_dsnh->output_cvlan_id_valid = FALSE;
            CTC_UNSET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID);

            break;

        case CTC_VLAN_EGRESS_EDIT_INSERT_VLAN:
            if (CTC_VLAN_EGRESS_EDIT_REPLACE_VLAN == p_vlan_info->svlan_edit_type ||
                CTC_VLAN_EGRESS_EDIT_NONE == p_vlan_info->svlan_edit_type)
            {
                return CTC_E_NH_VLAN_EDIT_CONFLICT;
            }

            if(!p_dsnh->vlan_xlate_mode)
            {  /*Modify S-tag from gb mode to humer mode (CTC_VLAN_EGRESS_EDIT_NONE/CTC_VLAN_EGRESS_EDIT_KEEP_VLAN_UNCHANGE)*/
                p_dsnh->output_svlan_id_valid = FALSE;
                CTC_UNSET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID);
                p_dsnh->svlan_tagged = TRUE;
                p_dsnh->tagged_mode = TRUE;
                p_dsnh->derive_stag_cos  =1;
                p_dsnh->stag_cfi  =0;   /*use  raw packet's cos*/
                p_dsnh->vlan_xlate_mode = 1;
            }
            break;
        case CTC_VLAN_EGRESS_EDIT_REPLACE_VLAN:       /* greatbelt support*/
           if(!p_dsnh->vlan_xlate_mode)
            {  /*Modify S-tag from gb mode to humer mode (CTC_VLAN_EGRESS_EDIT_NONE/CTC_VLAN_EGRESS_EDIT_KEEP_VLAN_UNCHANGE)*/
                p_dsnh->output_svlan_id_valid = FALSE;
                CTC_UNSET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID);
                p_dsnh->svlan_tagged = TRUE;
                p_dsnh->derive_stag_cos  =1;
                p_dsnh->stag_cfi  =0;   /*use  raw packet's cos*/
                p_dsnh->vlan_xlate_mode = 1;
            }

            break;
        case CTC_VLAN_EGRESS_EDIT_STRIP_VLAN:      /*greatbelt don't support*/

            return CTC_E_NOT_SUPPORT;
            break;

        default:
            return CTC_E_NH_INVALID_VLAN_EDIT_TYPE;
        }


    }
    return CTC_E_NONE;
}

STATIC int32
sys_greatbelt_nh_dsnh4w_assign_vid(uint8 lchip, ds_next_hop4_w_t* p_dsnh,
                                   ctc_vlan_egress_edit_info_t* p_vlan_info)
{
    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID))
    {
        p_dsnh->output_cvlan_id_valid = TRUE;
        p_dsnh->l2_edit_ptr11_0 = p_vlan_info->output_cvid;
    }

    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))
    {
        p_dsnh->output_svlan_id_valid = TRUE;
        p_dsnh->l3_edit_ptr14_0 = p_vlan_info->output_svid;
    }

    return CTC_E_NONE;
}

STATIC int32
sys_greatbelt_nh_dsnh8w_assign_vid(uint8 lchip, ds_next_hop8_w_t* p_dsnh8w,
                                   ctc_vlan_egress_edit_info_t* p_vlan_info)
{
    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID))
    {
        p_dsnh8w->output_cvlan_id_valid_ext = TRUE;
        p_dsnh8w->output_cvlan_id_ext = p_vlan_info->output_cvid;
    }

    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))
    {
        p_dsnh8w->output_svlan_id_valid_ext = TRUE;
        p_dsnh8w->output_svlan_id_ext = p_vlan_info->output_svid;
    }

    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUTPUT_SVLAN_TPID_VALID))
    {
        if(p_vlan_info->svlan_tpid_index>3)
        {
            return CTC_E_NH_INVALID_TPID_INDEX;
        }
        p_dsnh8w->svlan_tpid_en = TRUE;
        p_dsnh8w->svlan_tpid = p_vlan_info->svlan_tpid_index;
    }

    return CTC_E_NONE;
}

/**
 @brief This function is used to build and write dsnexthop4w table
 */

int32
sys_greatbelt_nh_write_entry_dsnh4w(uint8 lchip, sys_nh_param_dsnh_t* p_dsnh_param)
{

    ds_next_hop4_w_t dsnh;
    uint16 stats_ptr = 0;
    uint32 op_bridge = SYS_NH_OP_NONE;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    CTC_PTR_VALID_CHECK(p_dsnh_param);
    sal_memset(&dsnh, 0, sizeof(ds_next_hop4_w_t));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "\n\
        lchip = %d, \n\
        dsnh_offset = %d, \n\
        dsnh_type = %d\n, \
        l2EditPtr(outputCvlanID) = %d,\n\
        l3EditPtr(outputSvlanId) = %d \n",
                   lchip,
                   p_dsnh_param->dsnh_offset,
                   p_dsnh_param->dsnh_type,
                   p_dsnh_param->l2edit_ptr,
                   p_dsnh_param->l3edit_ptr);

    if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_L2EDIT_AS_APS_GROUP))
    {
        p_dsnh_param->l2edit_ptr = (0x1F << 11) | (p_dsnh_param->l2edit_ptr & 0x7FF);
    }

    switch (p_dsnh_param->dsnh_type)
    {
    case SYS_NH_PARAM_DSNH_TYPE_IPUC:
        dsnh.mtu_check_en = !(p_dsnh_param->mtu_no_chk);
        /*IPUC only use ROUTE_COMPACT in current GB SDK*/
        if ((p_dsnh_param->l2edit_ptr == 0)
            && (!CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_PW_BFD_PLD_ROUTE))
            && (!CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_ROUTE_NOTTL)))
        {
            dsnh.payload_operation = SYS_NH_OP_ROUTE_COMPACT;
            SYS_GREATBELT_NH_DSNH_ASSIGN_MACDA(dsnh, (*p_dsnh_param));
            dsnh.output_svlan_id_valid = 1;
        }
        else
        {
            CTC_SET_FLAG(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_USE_MAPPED_DSCP);
            SYS_GREATBELT_NH_DSNH_BUILD_FLAG(dsnh, p_dsnh_param->flag);
            SYS_GREATBELT_NH_DSNH_BUILD_L2EDITPTR_L3EDITPTR(dsnh, p_dsnh_param->l2edit_ptr, p_dsnh_param->l3edit_ptr);
            if(CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_ROUTE_NOTTL))
            {
                dsnh.payload_operation = SYS_NH_OP_ROUTE_NOTTL;
            }
            else
            {
                dsnh.payload_operation = SYS_NH_OP_ROUTE;
            }
        }

        dsnh.dest_vlan_ptr = p_dsnh_param->dest_vlan_ptr;
        break;

    case SYS_NH_PARAM_DSNH_TYPE_MPLS_PHP: /*PHP */
        if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_PHP_SHORT_PIPE))
        {
            dsnh.payload_operation = SYS_NH_OP_NONE;
        }
        else
        {
            dsnh.payload_operation = SYS_NH_OP_ROUTE;
        }

        dsnh.mtu_check_en = TRUE;
        dsnh.dest_vlan_ptr = p_dsnh_param->dest_vlan_ptr;
        SYS_GREATBELT_NH_DSNH_BUILD_FLAG(dsnh, p_dsnh_param->flag);
        SYS_GREATBELT_NH_DSNH_BUILD_L2EDITPTR_L3EDITPTR(dsnh, p_dsnh_param->l2edit_ptr, p_dsnh_param->l3edit_ptr);
        break;

    case SYS_NH_PARAM_DSNH_TYPE_IPMC:
        dsnh.payload_operation = SYS_NH_OP_ROUTE;
        dsnh.mtu_check_en = !(p_dsnh_param->mtu_no_chk);
        dsnh.svlan_tagged = 1;
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        SYS_GREATBELT_NH_DSNH_BUILD_FLAG(dsnh, p_dsnh_param->flag);
        SYS_GREATBELT_NH_DSNH_BUILD_L2EDITPTR_L3EDITPTR(dsnh, p_dsnh_param->l2edit_ptr, p_dsnh_param->l3edit_ptr);
        break;

    case SYS_NH_PARAM_DSNH_TYPE_BRGUC:
        /*Set default cos action*/
        SYS_GREATBELT_NH_DSNH_SET_DEFAULT_COS_ACTION(dsnh);
        dsnh.is_nexthop   = TRUE;
        dsnh.svlan_tagged = 1;
        dsnh.dest_vlan_ptr = p_dsnh_param->dest_vlan_ptr;   /*value 0 means use srcvlanPtr*/
        dsnh.payload_operation = CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_SWAP_MAC) ?
            SYS_NH_OP_NONE : SYS_NH_OP_BRIDGE;
        dsnh.stats_ptr = p_dsnh_param->stats_ptr;

        /*only  may be l2edit*/
        if (p_dsnh_param->l2edit_ptr)
        {
            SYS_GREATBELT_NH_DSNH_BUILD_L2EDITPTR_L3EDITPTR(dsnh, p_dsnh_param->l2edit_ptr, p_dsnh_param->l3edit_ptr);
        }

        if (p_dsnh_param->p_vlan_info)
        {
            if (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag, CTC_VLAN_NH_STATS_EN))
            {
                sys_greatbelt_stats_get_statsptr(lchip, p_dsnh_param->p_vlan_info->stats_id, &stats_ptr);
                dsnh.stats_ptr = stats_ptr;
            }
            if (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag, CTC_VLAN_NH_ETREE_LEAF))
            {
                dsnh.is_leaf = 1;
            }
            if(CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag,CTC_VLAN_NH_HORIZON_SPLIT_EN))
            {
                dsnh.payload_operation = SYS_NH_OP_BRIDGE_VPLS;
            }
            else if (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag, CTC_VLAN_NH_PASS_THROUGH))
            {
                dsnh.payload_operation = SYS_NH_OP_BRIDGE_INNER;
            }
            CTC_ERROR_RETURN(sys_greatbelt_nh_dsnh_build_vlan_info(lchip, &dsnh, p_dsnh_param->p_vlan_info));
            CTC_ERROR_RETURN(sys_greatbelt_nh_dsnh4w_assign_vid(lchip, &dsnh, p_dsnh_param->p_vlan_info));
        }
        break;

    case SYS_NH_PARAM_DSNH_TYPE_RSPAN:
        dsnh.vlan_xlate_mode = 1;    /*humber mode*/
        dsnh.tagged_mode = 1;
        dsnh.derive_stag_cos = 1;  /*restore vlantag for mirror*/
        dsnh.replace_ctag_cos = 1; /*add  rspan vlan id*/
        dsnh.output_svlan_id_valid = 1;
        dsnh.l3_edit_ptr14_0 = p_dsnh_param->p_vlan_info->output_svid & 0xFFF;
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        dsnh.payload_operation = SYS_NH_OP_MIRROR;
        break;

    case SYS_NH_PARAM_DSNH_TYPE_BYPASS:
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        dsnh.payload_operation = SYS_NH_OP_NONE;
        dsnh.vlan_xlate_mode = 1;    /*humber mode*/
        break;

    case SYS_NH_PARAM_DSNH_TYPE_MPLS_OP_NONE:   /* mpls switch*/
        return CTC_E_NH_SHOULD_USE_DSNH8W;
        break;

    case SYS_NH_PARAM_DSNH_TYPE_MPLS_PUSH_OP_NONE: /*Swap  label on LSR /Pop label and do  label  Swap on LER*/
        dsnh.payload_operation = SYS_NH_OP_NONE;
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        SYS_GREATBELT_NH_DSNH_BUILD_FLAG(dsnh, p_dsnh_param->flag);
        SYS_GREATBELT_NH_DSNH_BUILD_L2EDITPTR_L3EDITPTR(dsnh, p_dsnh_param->l2edit_ptr, p_dsnh_param->l3edit_ptr);
        break;

    case SYS_NH_PARAM_DSNH_TYPE_MPLS_PUSH_OP_ROUTE: /* (L3VPN/FTN)*/
        dsnh.payload_operation = SYS_NH_OP_ROUTE;
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        SYS_GREATBELT_NH_DSNH_BUILD_FLAG(dsnh, p_dsnh_param->flag);
        SYS_GREATBELT_NH_DSNH_BUILD_L2EDITPTR_L3EDITPTR(dsnh, p_dsnh_param->l2edit_ptr, p_dsnh_param->l3edit_ptr);
        dsnh.mtu_check_en = TRUE;
        if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_STATS_EN))
        {
            dsnh.stats_ptr = p_dsnh_param->stats_ptr;
        }
        break;

    case SYS_NH_PARAM_DSNH_TYPE_MPLS_PUSH_OP_L2VPN:
        if (p_dsnh_param->hvpls)
        {
            op_bridge = SYS_NH_OP_BRIDGE_INNER;
        }
        else
        {
            op_bridge = SYS_NH_OP_BRIDGE_VPLS;
        }

        dsnh.payload_operation = op_bridge;
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;

        if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_STATS_EN))
        {
            dsnh.stats_ptr = p_dsnh_param->stats_ptr;
        }

        SYS_GREATBELT_NH_DSNH_BUILD_L2EDITPTR_L3EDITPTR(dsnh, p_dsnh_param->l2edit_ptr, p_dsnh_param->l3edit_ptr);
        dsnh.mtu_check_en = TRUE;
        if (p_dsnh_param->p_vlan_info &&
            ((CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID)) ||
             (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))))
        {
            return CTC_E_NH_SHOULD_USE_DSNH8W;
        }
        else
        {
            /*Set default cos action*/
            if (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag, CTC_VLAN_NH_ETREE_LEAF))
            {
                dsnh.is_leaf = 1;
            }

            SYS_GREATBELT_NH_DSNH_SET_DEFAULT_COS_ACTION(dsnh);
            CTC_ERROR_RETURN(sys_greatbelt_nh_dsnh_build_vlan_info(lchip, &dsnh, p_dsnh_param->p_vlan_info));
         }

        break;

    case SYS_NH_PARAM_DSNH_TYPE_IPTUNNEL:
        dsnh.payload_operation = (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_IP_TUNNEL_PLD_BRIDGE)) ?
            SYS_NH_OP_NONE : ((CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_ROUTE_NOTTL)) ?
            SYS_NH_OP_ROUTE_NOTTL : SYS_NH_OP_ROUTE);
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        dsnh.mtu_check_en = TRUE;
        dsnh.stats_ptr = p_dsnh_param->stats_ptr;

        SYS_GREATBELT_NH_DSNH_BUILD_FLAG(dsnh, p_dsnh_param->flag);
        SYS_GREATBELT_NH_DSNH_BUILD_L2EDITPTR_L3EDITPTR(dsnh, p_dsnh_param->l2edit_ptr, p_dsnh_param->l3edit_ptr);
        break;

    case SYS_NH_PARAM_DSNH_TYPE_IPTUNNEL_FOR_MIRROR:
        dsnh.payload_operation = SYS_NH_OP_MIRROR;
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        SYS_GREATBELT_NH_DSNH_BUILD_FLAG(dsnh, p_dsnh_param->flag);
        SYS_GREATBELT_NH_DSNH_BUILD_L2EDITPTR_L3EDITPTR(dsnh, p_dsnh_param->l2edit_ptr, p_dsnh_param->l3edit_ptr);
        break;

    default:
        return CTC_E_NH_INVALID_DSNH_TYPE;
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "DsNH4W :: DestVlanPtr = %d, PldOP = %d , "
                   "copyCtagCos = %d, CvlanTagged = %d, DeriveStagCos = %d, L2EditPtr = %d, L3EditPtr = %d, "
                   "MtuCheckEn = %d, OutputCvlanVlid = %d, OutputSvlanVlid = %d, "
                   "ReplaceCtagCos = %d StagCfi = %d, StagCos = %d, "
                   "SvlanTagged = %d, TaggedMode = %d",
                   dsnh.dest_vlan_ptr, dsnh.payload_operation,
                   dsnh.copy_ctag_cos, 0,
                   dsnh.derive_stag_cos, dsnh.l2_edit_ptr11_0, dsnh.l3_edit_ptr14_0,
                   dsnh.mtu_check_en, dsnh.output_cvlan_id_valid,
                   dsnh.output_svlan_id_valid, dsnh.replace_ctag_cos,
                   dsnh.stag_cfi, dsnh.stag_cos, dsnh.svlan_tagged, dsnh.tagged_mode);

    dsnh.is_nexthop   = TRUE;
    CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip,
                                                       SYS_NH_ENTRY_TYPE_NEXTHOP_4W, p_dsnh_param->dsnh_offset, &dsnh));

    return CTC_E_NONE;

}

int32
sys_greatbelt_nh_write_entry_dsnh8w(uint8 lchip, sys_nh_param_dsnh_t* p_dsnh_param)
{

    ds_next_hop8_w_t dsnh_8w;
    uint32 op_bridge = 0;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    uint8 xgpon_en = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_dsnh_param);
    sal_memset(&dsnh_8w, 0, sizeof(ds_next_hop8_w_t));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "\n\
        lchip = %d, \n\
        dsnh_offset = %d, \n\
        dsnh_type = %d\n, \
        l2EditPtr(outputCvlanID) = %d, \n\
        l3EditPtr(outputSvlanId) = %d \n",
                   lchip,
                   p_dsnh_param->dsnh_offset,
                   p_dsnh_param->dsnh_type,
                   p_dsnh_param->l2edit_ptr,
                   p_dsnh_param->l3edit_ptr);


    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_L2EDIT_AS_APS_GROUP))
    {
        p_dsnh_param->l2edit_ptr = (0x7F << 11) | (p_dsnh_param->l2edit_ptr & 0x7FF);
    }

    dsnh_8w.stag_cos |= 1;

    switch (p_dsnh_param->dsnh_type)
    {
    case SYS_NH_PARAM_DSNH_TYPE_BRGUC:
        SYS_GREATBELT_NH_DSNH_SET_DEFAULT_COS_ACTION(dsnh_8w);
        dsnh_8w.payload_operation = SYS_NH_OP_BRIDGE;
        dsnh_8w.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        dsnh_8w.stats_ptr = p_dsnh_param->stats_ptr;
        SYS_GREATBELT_NH_DSNH8W_BUILD_L2EDITPTR_L3EDITPTR(dsnh_8w,
                                                       p_dsnh_param->l2edit_ptr,
                                                       p_dsnh_param->l3edit_ptr);
        if(p_dsnh_param->logic_port_check)
        {
            dsnh_8w.logic_dest_port     = p_dsnh_param->logic_port;
            dsnh_8w.logic_port_check    = 1;
        }

        if (p_dsnh_param->p_vlan_info)
        {
            if (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag, CTC_VLAN_NH_ETREE_LEAF))
            {
                dsnh_8w.is_leaf = 1;
            }
            if (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag, CTC_VLAN_NH_PASS_THROUGH))
            {
                dsnh_8w.payload_operation = SYS_NH_OP_BRIDGE_INNER;
            }
            if (p_dsnh_param->p_vlan_info->flag & CTC_VLAN_NH_MTU_CHECK_EN)
            {
                dsnh_8w.tunnel_mtu_check_en = 1;
                dsnh_8w.output_cvlan_id_ext = p_dsnh_param->p_vlan_info->mtu_size >> 2;
                dsnh_8w.tunnel_mtu_size1_0 = p_dsnh_param->p_vlan_info->mtu_size & 0x3;
            }
            CTC_ERROR_RETURN(sys_greatbelt_nh_dsnh_build_vlan_info(lchip, (ds_next_hop4_w_t*)(&dsnh_8w), p_dsnh_param->p_vlan_info));
            CTC_ERROR_RETURN(sys_greatbelt_nh_dsnh8w_assign_vid(lchip, &dsnh_8w, p_dsnh_param->p_vlan_info));
        }

#if 1
        /*XGPON*/
        sys_greatbelt_global_get_xgpon_en(lchip, &xgpon_en);
        if (xgpon_en && dsnh_8w.logic_port_check)
        {
            dsnh_8w.use_logic_port_no_check = 1;
        }
#endif
        break;

    case SYS_NH_PARAM_DSNH_TYPE_MPLS_PUSH_OP_L2VPN:
        if (p_dsnh_param->hvpls)
        {
            op_bridge = SYS_NH_OP_BRIDGE_INNER;
        }
        else
        {
            op_bridge = SYS_NH_OP_BRIDGE_VPLS;
        }
        if (p_dsnh_param->p_vlan_info)
        {
            if (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag, CTC_VLAN_NH_ETREE_LEAF))
            {
                dsnh_8w.is_leaf = 1;
            }

            if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_STATS_EN))
            {
                dsnh_8w.stats_ptr = p_dsnh_param->stats_ptr;
            }
            dsnh_8w.payload_operation = op_bridge;
            dsnh_8w.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;

            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "dsnh8w.dest_vlanptr = %d, payloadOp = %d \n",
                           dsnh_8w.dest_vlan_ptr, dsnh_8w.payload_operation);
            CTC_ERROR_RETURN(sys_greatbelt_nh_dsnh_build_vlan_info(lchip, (ds_next_hop4_w_t*)(&dsnh_8w) /*Have same structure*/,
                                                                   p_dsnh_param->p_vlan_info));
            CTC_ERROR_RETURN(sys_greatbelt_nh_dsnh8w_assign_vid(lchip, &dsnh_8w, p_dsnh_param->p_vlan_info));
        }
        else
        {
            CTC_ERROR_RETURN(CTC_E_INVALID_PARAM);
        }

        dsnh_8w.logic_dest_port     = p_dsnh_param->logic_port;
        dsnh_8w.mtu_check_en = TRUE;
        SYS_GREATBELT_NH_DSNH8W_BUILD_L2EDITPTR_L3EDITPTR(dsnh_8w,
                                                          p_dsnh_param->l2edit_ptr,
                                                          p_dsnh_param->l3edit_ptr);
        break;
#if 0
    case SYS_NH_PARAM_DSNH_TYPE_BRGUC:
        if (p_dsnh_param->p_vlan_info)
        {
            dsnh_8w.vlan_xlate_mode = 1;
            dsnh_8w.payload_operation = SYS_NH_OP_BRIDGE;
            dsnh_8w.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;

            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "dsnh8w.dest_vlanptr = %d, payloadOp = %d \n",
                           dsnh_8w.dest_vlan_ptr, dsnh_8w.payload_operation);
            CTC_ERROR_RETURN(sys_greatbelt_nh_dsnh_build_vlan_info(lchip, (ds_next_hop4_w_t*)(&dsnh_8w) /*Have same structure*/,
                                                                   p_dsnh_param->p_vlan_info));
            CTC_ERROR_RETURN(sys_greatbelt_nh_dsnh8w_assign_vid(lchip, &dsnh_8w, p_dsnh_param->p_vlan_info));
            SYS_GREATBELT_NH_DSNH8W_BUILD_L2EDITPTR_L3EDITPTR(dsnh_8w,
                                                  p_dsnh_param->l2edit_ptr,
                                                  p_dsnh_param->l3edit_ptr);
        }
        else
        {
            CTC_ERROR_RETURN(CTC_E_INVALID_PARAM);
        }
        break;
#endif
    default:
        return CTC_E_NH_SHOULD_USE_DSNH4W;
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "DsNH8W :: DestVlanPtr = %d, PldOP = %d"
                   "  copyCtagCos = %d,CvlanTagged = %d, DeriveStagCos = %d, L2EditPtr= %u, "
                   "L3EditPtr = %u, MtuCheckEn = %d, OutputCvlanVlid = %d, OutputSvlanVlid = %d, "
                   "ReplaceCtagCos = %d, StagCfi = %d, StagCos = %d, "
                   "SvlanTagged = %d, TaggedMode = %d",
                   dsnh_8w.dest_vlan_ptr, dsnh_8w.payload_operation,
                   dsnh_8w.copy_ctag_cos, 0,
                   dsnh_8w.derive_stag_cos, dsnh_8w.l2_edit_ptr11_0,
                   dsnh_8w.l3_edit_ptr14_0,
                   dsnh_8w.mtu_check_en, dsnh_8w.output_cvlan_id_valid,
                   dsnh_8w.output_svlan_id_valid, dsnh_8w.replace_ctag_cos,
                   dsnh_8w.stag_cfi, dsnh_8w.stag_cos, dsnh_8w.svlan_tagged, dsnh_8w.tagged_mode);

    dsnh_8w.is_nexthop = TRUE;
    dsnh_8w.is_nexthop8_w = TRUE;

    CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip,
                                                       SYS_NH_ENTRY_TYPE_NEXTHOP_8W, p_dsnh_param->dsnh_offset, &dsnh_8w));

    return CTC_E_NONE;
}

/**
 @brief This function is used to get dsfwd info
 */
int32
sys_greatbelt_nh_get_entry_dsfwd(uint8 lchip, uint32 dsfwd_offset, void* p_dsfwd)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_dsfwd);

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_asic_table(lchip, SYS_NH_ENTRY_TYPE_FWD,
                                                     dsfwd_offset, p_dsfwd));

    return CTC_E_NONE;
}

/**
 @brief This function is used to build and write dsfwd table
 */
int32
sys_greatbelt_nh_write_entry_dsfwd(uint8 lchip, sys_nh_param_dsfwd_t* p_dsfwd_param)
{

    ds_fwd_t dsfwd;
    uint8 gchip = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_dsfwd_param);

    sal_memset(&dsfwd, 0, sizeof(ds_fwd_t));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "\n\
        lchip = %d, \n\
        dsfwd_offset = %d, \n\
        dest_chip_id = %d, \n\
        dest_id = %d, \n\
        dropPkt = %d, \n\
        dsnh_offset = %d, \n\
        is_mcast = %d\n",
                   lchip,
                   p_dsfwd_param->dsfwd_offset,
                   p_dsfwd_param->dest_chipid,
                   p_dsfwd_param->dest_id,
                   p_dsfwd_param->drop_pkt,
                   p_dsfwd_param->dsnh_offset,
                   p_dsfwd_param->is_mcast);

    if (p_dsfwd_param->dsnh_offset > 0xFFFFF)
    {
        return CTC_E_NH_EXCEED_MAX_DSNH_OFFSET;
    }

 /*    CTC_ERROR_RETURN(sys_greatbelt_get_service_queue_enable(&enable));*/

    dsfwd.next_hop_ptr = (p_dsfwd_param->dsnh_offset) & 0x3FFFF;
    dsfwd.aps_type = p_dsfwd_param->aps_type;
    if (p_dsfwd_param->nexthop_ext)
    {
        dsfwd.next_hop_ext = TRUE;
    }
    else
    {
        dsfwd.next_hop_ext = FALSE;
    }

    if (p_dsfwd_param->length_adjust_type)
    {
        dsfwd.length_adjust_type = 1;
    }
    else
    {
        dsfwd.length_adjust_type = 0;
    }

    if (p_dsfwd_param->drop_pkt)
    {
        CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
        dsfwd.dest_map = SYS_NH_ENCODE_DESTMAP(0, gchip, SYS_RESERVE_PORT_ID_DROP);
    }
    else
    {
        if (p_dsfwd_param->service_queue_en)
        {
            p_dsfwd_param->dest_id &= 0xFFF;
            p_dsfwd_param->dest_id |= (SYS_QSEL_TYPE_SERVICE << 13); /*queueseltype:destmap[15:12]*/
        }
        else
        {
#if 0 /*!!!!!need refer to sys_greatbelt_queue_enq.c, the internal port is from 64 ~ xx ,and the queue select type == SYS_QSEL_TYPE_INTERNAL_PORT*/

            if ((FALSE == p_dsfwd_param->is_mcast)
                && (dsfwd.aps_type != CTC_APS_BRIDGE && dsfwd.aps_type != CTC_APS_SELECT)
                && sys_greatbelt_chip_is_local(lchip, p_dsfwd_param->dest_chipid)
                && ((p_dsfwd_param->dest_id & 0xFF) > SYS_STATIC_INT_PORT_END))
            {
                p_dsfwd_param->dest_id |= (SYS_QSEL_TYPE_INTERNAL_PORT << 12); /*queueseltype:destmap[15:12]*/
            }

            if ((FALSE == p_dsfwd_param->is_mcast)
                && (dsfwd.aps_type != CTC_APS_BRIDGE && dsfwd.aps_type != CTC_APS_SELECT)
                && sys_greatbelt_chip_is_local(lchip, p_dsfwd_param->dest_chipid)
                && ((p_dsfwd_param->dest_id & 0xFF) >= SYS_STATIC_INT_PORT_START)
                && ((p_dsfwd_param->dest_id & 0xFF) <= SYS_STATIC_INT_PORT_END))
            {
                p_dsfwd_param->dest_id |= (SYS_QSEL_TYPE_STATIC_INT_PORT << 12); /*queueseltype:destmap[15:12]*/
            }

#endif
        }

        if (p_dsfwd_param->is_to_cpu)
        {
            dsfwd.dest_map = SYS_REASON_ENCAP_DEST_MAP(p_dsfwd_param->dest_chipid, SYS_PKT_CPU_QDEST_BY_DMA);
            dsfwd.next_hop_ptr = CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_FWD_CPU, 0);
        }
        else
        {
            dsfwd.dest_map = SYS_NH_ENCODE_DESTMAP(p_dsfwd_param->is_mcast,
                                                   p_dsfwd_param->dest_chipid, p_dsfwd_param->dest_id);
        }

        if(sys_greatbelt_get_cut_through_en(lchip))
        {
            /*aps destid not consider, not support multicast, linkagg*/
            /*
            if (!p_dsfwd_param->is_mcast && (CTC_LINKAGG_CHIPID != p_dsfwd_param->dest_chipid)
                && (p_dsfwd_param->dest_id < CTC_MAX_PHY_PORT))
            {
                dsfwd.speed = sys_greatbelt_get_cut_through_speed(lchip);
            }
            */
            if (p_dsfwd_param->is_mcast)
            {
                dsfwd.speed = 3;
            }
            else if((CTC_APS_DISABLE ==p_dsfwd_param->aps_type) && (CTC_LINKAGG_CHIPID != p_dsfwd_param->dest_chipid))
            {
                uint16 gport = 0;
                gport = SYS_GREATBELT_DESTMAP_TO_GPORT(dsfwd.dest_map);
                dsfwd.speed = sys_greatbelt_get_cut_through_speed(lchip, gport);
            }
            else
            {
                dsfwd.speed = 0;
            }

        }
    }

    dsfwd.stats_ptr_low = p_dsfwd_param->stats_ptr & 0xfff;
    dsfwd.stats_ptr_high = (p_dsfwd_param->stats_ptr >> 12) & 0xf;
    dsfwd.force_back_en = p_dsfwd_param->is_reflective ? 1 : 0;


    dsfwd.bypass_ingress_edit = (p_dsfwd_param->is_to_cpu || p_dsfwd_param->bypass_ingress_edit);
    dsfwd.send_local_phy_port = p_dsfwd_param->lport_en;

    if (p_dsfwd_param->dest_map)
    {
        dsfwd.dest_map = p_dsfwd_param->dest_map;
    }

    CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_FWD,
                                                       p_dsfwd_param->dsfwd_offset, &dsfwd));

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_update_dsfwd_speed(uint8 lchip, uint32 fwd_offset, uint8 speed)
{
    ds_fwd_t ds_fwd;
    uint32 cmd = 0;

    sal_memset(&ds_fwd, 0, sizeof(ds_fwd_t));

    cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, fwd_offset, cmd, &ds_fwd));
    ds_fwd.speed = speed;
    cmd = DRV_IOW(DsFwd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, fwd_offset, cmd, &ds_fwd));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Update dsfwd(0x%x) speed:%d\n", fwd_offset, speed);


    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_update_dsfwd_lport_en(uint8 lchip, uint32 fwd_offset, uint8 lport_en)
{
    ds_fwd_t ds_fwd;
    uint32 cmd = 0;

    sal_memset(&ds_fwd, 0, sizeof(ds_fwd_t));

    if (fwd_offset)
    {
        cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, fwd_offset, cmd, &ds_fwd));
        ds_fwd.send_local_phy_port = (lport_en?1:0);
        cmd = DRV_IOW(DsFwd_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, fwd_offset, cmd, &ds_fwd));
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Update dsfwd(0x%x) lport en:%d\n", fwd_offset, lport_en);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_update_entry_dsfwd(uint8 lchip, uint32 *offset,
                               uint32 dest_map,
                               uint32 dsnh_offset,
                               uint8 dsnh_8w,
                               uint8 del,
                               uint8 lport_en)
{

    sys_nh_param_dsfwd_t sys_fwd;
    sal_memset(&sys_fwd, 0, sizeof(sys_nh_param_dsfwd_t));


    if((*offset) == 0)
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1, offset));
    }
    else if(del)
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_FWD, 1, *offset));
    }
    sys_fwd.dsnh_offset = (dsnh_offset & 0x3FFFF);
    sys_fwd.nexthop_ext = dsnh_8w;
    sys_fwd.dest_map  = dest_map;
    sys_fwd.dsfwd_offset = *offset;
    sys_fwd.bypass_ingress_edit = 1;
    sys_fwd.lport_en = lport_en;

    CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsfwd(lchip, &sys_fwd));
    return CTC_E_NONE;

}

#if 0
/**
 @brief Get AVL tree root by AVL node type
 */
STATIC int32
_sys_greatbelt_nh_db_get_avl_by_type(uint8 lchip,
    sys_nh_entry_table_type_t entry_type, struct ctc_avl_tree** pp_avl_root)
{
    sys_greatbelt_nh_master_t* p_master;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Entry type = %d\n", entry_type);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_master));

    switch (entry_type)
    {
    case SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W:
        *pp_avl_root = p_master->dsl2edit4w_tree;
        break;

    case SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W:
        *pp_avl_root = p_master->dsl2edit8w_tree;
        break;

    case SYS_NH_ENTRY_TYPE_NEXTHOP_4W:
    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/**
 @brief Get AVL node size by AVL node type
 */
STATIC int32
_sys_greatbelt_nh_db_get_node_size_by_type(uint8 lchip,
    sys_nh_entry_table_type_t entry_type, uint32* p_size)
{
    sys_greatbelt_nh_master_t* p_master;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Entry type = %d\n", entry_type);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_master));

    switch (entry_type)
    {
    case SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W:
        *p_size = sizeof(sys_nh_db_dsl2editeth4w_t);
        break;

    case SYS_NH_ENTRY_TYPE_NEXTHOP_4W:
    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}
#endif
/**
 @brief Get nh db entry size
 */
STATIC int32
_sys_greatbelt_nh_get_nhentry_size_by_nhtype(uint8 lchip,
    sys_greatbelt_nh_type_t nh_type, uint32* p_size)
{
    sys_greatbelt_nh_master_t* p_master;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "NH type = %d\n", nh_type);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_master));

    switch (nh_type)
    {
    case SYS_NH_TYPE_BRGUC:
        *p_size = sizeof(sys_nh_info_brguc_t);
        break;

    case SYS_NH_TYPE_MCAST:
        *p_size = sizeof(sys_nh_info_mcast_t);
        break;

    case SYS_NH_TYPE_IPUC:
        *p_size = sizeof(sys_nh_info_ipuc_t);
        break;

    case SYS_NH_TYPE_ECMP:
        *p_size = sizeof(sys_nh_info_ecmp_t);
        break;

    case SYS_NH_TYPE_MPLS:
        *p_size = sizeof(sys_nh_info_mpls_t);
        break;

    case SYS_NH_TYPE_DROP:
    case SYS_NH_TYPE_TOCPU:
    case SYS_NH_TYPE_UNROV:
    case SYS_NH_TYPE_ILOOP:
        *p_size = sizeof(sys_nh_info_special_t);
        break;

    case SYS_NH_TYPE_RSPAN:
        *p_size = sizeof(sys_nh_info_rspan_t);
        break;

    case SYS_NH_TYPE_IP_TUNNEL:
        *p_size = sizeof(sys_nh_info_ip_tunnel_t);
        break;

    case SYS_NH_TYPE_MISC:
        *p_size = sizeof(sys_nh_info_misc_t);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/**
 @brief Shared entry lookup in AVL treee
 */
STATIC sys_nh_db_dsl2editeth4w_t*
_sys_greatbelt_nh_lkup_route_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w)
{
    sys_greatbelt_nh_master_t* p_master = NULL;
    struct ctc_avl_node* avl_node = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    _sys_greatbelt_nh_get_nh_master(lchip, &p_master);
    /*2. Do lookup*/
    avl_node = ctc_avl_search(p_master->dsl2edit4w_tree,  p_dsl2edit_4w);

    return avl_node ? avl_node->info : NULL;

}

/**
 @brief Shared entry lookup in AVL treee
 */
STATIC sys_nh_db_dsl2editeth8w_t*
_sys_greatbelt_nh_lkup_route_l2edit_8w(uint8 lchip, sys_nh_db_dsl2editeth8w_t* p_dsl2edit_8w)
{
    sys_greatbelt_nh_master_t* p_master = NULL;
    struct ctc_avl_node* avl_node = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    _sys_greatbelt_nh_get_nh_master(lchip, &p_master);
    /*2. Do lookup*/
    avl_node = ctc_avl_search(p_master->dsl2edit8w_tree,  p_dsl2edit_8w);

    return avl_node ? avl_node->info : NULL;

}

/**
 @brief Insert node into AVL tree
 */
STATIC int32
_sys_greatbelt_nh_insert_route_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w)
{
    int32 ret = CTC_E_NONE;
    sys_greatbelt_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    _sys_greatbelt_nh_get_nh_master(lchip, &p_master);

    /*2. Insert node*/
    ret = ctc_avl_insert(p_master->dsl2edit4w_tree,  p_dsl2edit_4w);

    return ret;
}

/**
 @brief Insert node into AVL tree
 */
STATIC int32
_sys_greatbelt_nh_insert_route_l2edit_8w(uint8 lchip, sys_nh_db_dsl2editeth8w_t* p_dsl2edit_8w)
{
    int32 ret = CTC_E_NONE;
    sys_greatbelt_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    _sys_greatbelt_nh_get_nh_master(lchip, &p_master);

    /*2. Insert node*/
    ret = ctc_avl_insert(p_master->dsl2edit8w_tree,  p_dsl2edit_8w);

    return ret;
}

int32
sys_greatbelt_nh_add_route_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t** p_dsl2edit_4w)  /*DsL2Edit*/
{
    sys_nh_db_dsl2editeth4w_t* p_new_dsl2edit_4w = NULL;
    int32 ret = CTC_E_NONE;
    uint32 new_offset = 0;

    p_new_dsl2edit_4w = _sys_greatbelt_nh_lkup_route_l2edit(lchip, (*p_dsl2edit_4w));

    if (p_new_dsl2edit_4w)
    /*Found node*/
    {
        p_new_dsl2edit_4w->ref_cnt++;
        *p_dsl2edit_4w = p_new_dsl2edit_4w;

        return CTC_E_NONE;
    }

    p_new_dsl2edit_4w = (sys_nh_db_dsl2editeth4w_t*)mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_db_dsl2editeth4w_t));
    if (p_new_dsl2edit_4w == NULL)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_new_dsl2edit_4w, 0, sizeof(sys_nh_db_dsl2editeth4w_t));
    /*Insert new one*/
    ret = sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 1, &new_offset);
    if (ret != CTC_E_NONE)
    {
        mem_free(p_new_dsl2edit_4w);
        p_new_dsl2edit_4w = NULL;
        return ret;
    }


    p_new_dsl2edit_4w->ref_cnt = 1; /*New create node*/
    p_new_dsl2edit_4w->offset = new_offset;

    p_new_dsl2edit_4w->output_vid = (*p_dsl2edit_4w)->output_vid;
    p_new_dsl2edit_4w->output_vlan_is_svlan = (*p_dsl2edit_4w)->output_vlan_is_svlan;
    p_new_dsl2edit_4w->packet_type          = (*p_dsl2edit_4w)->packet_type;
    p_new_dsl2edit_4w->phy_if               = (*p_dsl2edit_4w)->phy_if;
    sal_memcpy(p_new_dsl2edit_4w->mac_da, (*p_dsl2edit_4w)->mac_da, sizeof(mac_addr_t));
    ret = _sys_greatbelt_nh_write_entry_dsl2editeth4w(lchip, p_new_dsl2edit_4w);
    ret = ret ? ret : _sys_greatbelt_nh_insert_route_l2edit(lchip, p_new_dsl2edit_4w);
    if (ret != CTC_E_NONE)
    {
        mem_free(p_new_dsl2edit_4w);
        p_new_dsl2edit_4w = NULL;
    }

    *p_dsl2edit_4w = p_new_dsl2edit_4w;
    return ret;
}

int32
sys_greatbelt_nh_add_route_l2edit_8w(uint8 lchip, sys_nh_db_dsl2editeth8w_t** p_dsl2edit_8w)  /*DsL2Edit*/
{
    sys_nh_db_dsl2editeth8w_t* p_new_dsl2edit_8w = NULL;
    int32 ret = CTC_E_NONE;
    uint32 new_offset = 0;

    p_new_dsl2edit_8w = _sys_greatbelt_nh_lkup_route_l2edit_8w(lchip, (*p_dsl2edit_8w));

    if (p_new_dsl2edit_8w)
    /*Found node*/
    {
        p_new_dsl2edit_8w->ref_cnt++;
        *p_dsl2edit_8w = p_new_dsl2edit_8w;

        return CTC_E_NONE;
    }

    p_new_dsl2edit_8w = (sys_nh_db_dsl2editeth8w_t*)mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_db_dsl2editeth8w_t));
    if (p_new_dsl2edit_8w == NULL)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_new_dsl2edit_8w, 0, sizeof(sys_nh_db_dsl2editeth8w_t));

    /*Insert new one*/
    ret = sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W, 1, &new_offset);
    if (ret != CTC_E_NONE)
    {
        mem_free(p_new_dsl2edit_8w);
        p_new_dsl2edit_8w = NULL;
		return ret;
    }

    p_new_dsl2edit_8w->ref_cnt = 1; /*New create node*/
    p_new_dsl2edit_8w->offset = new_offset;

    sal_memcpy(p_new_dsl2edit_8w->mac_da, (*p_dsl2edit_8w)->mac_da, sizeof(mac_addr_t));
    p_new_dsl2edit_8w->ip0_31  = (*p_dsl2edit_8w)->ip0_31;
    p_new_dsl2edit_8w->ip32_63 = (*p_dsl2edit_8w)->ip32_63;
    p_new_dsl2edit_8w->ip64_79 = (*p_dsl2edit_8w)->ip64_79;
    p_new_dsl2edit_8w->output_vid = (*p_dsl2edit_8w)->output_vid;
    p_new_dsl2edit_8w->output_vlan_is_svlan = (*p_dsl2edit_8w)->output_vlan_is_svlan;
    p_new_dsl2edit_8w->packet_type          = (*p_dsl2edit_8w)->packet_type;
    ret = _sys_greatbelt_nh_write_entry_dsl2editeth8w(lchip, p_new_dsl2edit_8w);
    ret = ret ? ret : _sys_greatbelt_nh_insert_route_l2edit_8w(lchip, p_new_dsl2edit_8w);
    if (ret != CTC_E_NONE)
    {
        mem_free(p_new_dsl2edit_8w);
        p_new_dsl2edit_8w = NULL;
    }

    *p_dsl2edit_8w = p_new_dsl2edit_8w;
    return ret;
}

int32
sys_greatbelt_nh_remove_route_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w)
{
    sys_greatbelt_nh_master_t* p_master = NULL;
    sys_nh_db_dsl2editeth4w_t* p_new_dsl2edit_4w = NULL;
    if(!p_dsl2edit_4w)
    {
        return CTC_E_NONE;
    }

    p_new_dsl2edit_4w = _sys_greatbelt_nh_lkup_route_l2edit(lchip, p_dsl2edit_4w);

    if (!p_new_dsl2edit_4w)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    p_new_dsl2edit_4w->ref_cnt--;
    if (p_new_dsl2edit_4w->ref_cnt > 0)
    {
        return CTC_E_NONE;
    }
    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip,
                                                   SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 1, p_new_dsl2edit_4w->offset));

    _sys_greatbelt_nh_get_nh_master(lchip, &p_master);
    CTC_ERROR_RETURN(ctc_avl_remove(p_master->dsl2edit4w_tree, p_new_dsl2edit_4w));
    mem_free(p_new_dsl2edit_4w);
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_remove_route_l2edit_8w(uint8 lchip, sys_nh_db_dsl2editeth8w_t* p_dsl2edit_8w)
{
    sys_greatbelt_nh_master_t* p_master = NULL;
    sys_nh_db_dsl2editeth8w_t* p_new_dsl2edit_8w = NULL;

    p_new_dsl2edit_8w = _sys_greatbelt_nh_lkup_route_l2edit_8w(lchip, p_dsl2edit_8w);

    if (!p_new_dsl2edit_8w)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    p_new_dsl2edit_8w->ref_cnt--;
    if (p_new_dsl2edit_8w->ref_cnt > 0)
    {
        return CTC_E_NONE;
    }
    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip,
                                                   SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W, 1, p_new_dsl2edit_8w->offset));

    _sys_greatbelt_nh_get_nh_master(lchip, &p_master);
    CTC_ERROR_RETURN(ctc_avl_remove(p_master->dsl2edit8w_tree, p_new_dsl2edit_8w));
    mem_free(p_new_dsl2edit_8w);
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_lkup_mpls_tunnel(uint8 lchip, uint16 tunnel_id, sys_nh_db_mpls_tunnel_t** pp_mpls_tunnel)
{

    sys_greatbelt_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_master));

    if (tunnel_id >= p_master->max_tunnel_id)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    *pp_mpls_tunnel = ctc_vector_get(p_master->tunnel_id_vec, tunnel_id);

    return CTC_E_NONE;

}

/**
 @brief Insert node into AVL tree
 */
int32
sys_greatbelt_nh_add_mpls_tunnel(uint8 lchip, uint16 tunnel_id, sys_nh_db_mpls_tunnel_t* p_mpls_tunnel)
{
    int32 ret = CTC_E_NONE;
    sys_greatbelt_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_master));

    if (FALSE == ctc_vector_add(p_master->tunnel_id_vec, tunnel_id, p_mpls_tunnel))
    {
        return CTC_E_NO_MEMORY;
    }

    return ret;
}

int32
sys_greatbelt_nh_remove_mpls_tunnel(uint8 lchip, uint16 tunnel_id)
{

    int32 ret = CTC_E_NONE;
    sys_greatbelt_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_master));

    if (FALSE == ctc_vector_del(p_master->tunnel_id_vec, tunnel_id))
    {
        return CTC_E_NO_MEMORY;
    }

    return ret;
}

int32
sys_greatbelt_nh_lkup_mcast_id(uint8 lchip, uint16 mcast_id, sys_nh_info_mcast_t** pp_mcast)
{
    sys_greatbelt_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    _sys_greatbelt_nh_get_nh_master(lchip, &p_master);

    if (mcast_id >= p_master->max_glb_met_offset)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    *pp_mcast = ctc_vector_get(p_master->mcast_nhid_vec, mcast_id);
    if (NULL == *pp_mcast)
    {
        return CTC_E_INVALID_PARAM;
    }
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_get_mcast_nh(uint8 lchip, uint32 mcast_id, uint32* nhid)
{
    sys_nh_info_mcast_t* p_mcast_db = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_PTR_VALID_CHECK(nhid);
    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    p_mcast_db =  ctc_vector_get(p_gb_nh_master->mcast_nhid_vec, mcast_id);
    if (NULL == p_mcast_db)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    *nhid = p_mcast_db->nhid;

    return CTC_E_NONE;
}


int32
sys_greatbelt_nh_add_mcast_id(uint8 lchip, uint16 mcast_id, sys_nh_info_mcast_t* p_macst)
{
    int32 ret = CTC_E_NONE;
    sys_greatbelt_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_master));

    if (FALSE == ctc_vector_add(p_master->mcast_nhid_vec, mcast_id, p_macst))
    {
        return CTC_E_NO_MEMORY;
    }

    return ret;
}

int32
sys_greatbelt_nh_remove_mcast_id(uint8 lchip, uint16 mcast_id)
{

    int32 ret = CTC_E_NONE;
    sys_greatbelt_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_master));

    if (FALSE == ctc_vector_del(p_master->mcast_nhid_vec, mcast_id))
    {
        return CTC_E_NO_MEMORY;
    }

    return ret;
}

int32
sys_greatbelt_nh_traverse_mcast_db(uint8 lchip, vector_traversal_fn2 fn, void* data)
{
    sys_greatbelt_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_nh_master));

    ctc_vector_traverse2(p_nh_master->mcast_nhid_vec, 0, fn, data);

    return CTC_E_NONE;
}


uint32
sys_greatbelt_nh_get_arp_num(uint8 lchip)
{
    sys_greatbelt_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    _sys_greatbelt_nh_get_nh_master(lchip, &p_master);
    return p_master->arp_id_vec->used_cnt;
}

int32
sys_greatbelt_nh_lkup_arp_id(uint8 lchip, uint16 arp_id, sys_nh_db_arp_t** pp_arp)
{
    sys_greatbelt_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_master));

    if (arp_id >= SYS_NH_ARP_ID_DEFAUL_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    *pp_arp = ctc_vector_get(p_master->arp_id_vec, arp_id);
    if (NULL == *pp_arp)
    {
        return CTC_E_INVALID_PARAM;
    }
    return CTC_E_NONE;
}


int32
sys_greatbelt_nh_add_arp_id(uint8 lchip, uint16 arp_id, sys_nh_db_arp_t* p_arp)
{
    int32 ret = CTC_E_NONE;
    sys_greatbelt_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_master));

    if (FALSE == ctc_vector_add(p_master->arp_id_vec, arp_id, p_arp))
    {
        return CTC_E_NO_MEMORY;
    }

    return ret;
}

int32
sys_greatbelt_nh_remove_arp_id(uint8 lchip, uint16 arp_id)
{

    int32 ret = CTC_E_NONE;
    sys_greatbelt_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_master));

    if (FALSE == ctc_vector_del(p_master->arp_id_vec, arp_id))
    {
        return CTC_E_NO_MEMORY;
    }

    return ret;
}


/**
 @brief This function is used to get nexthop information by nexthop id
 */
STATIC int32
_sys_greatbelt_nh_api_get_nhinfo_by_nhid(uint8 lchip, uint32 nhid, sys_greatbelt_nh_master_t* p_gb_nh_master,
                                         sys_nh_info_com_t** pp_nhinfo)
{
    sys_nh_info_com_t* p_nh_info;

    if (nhid < p_gb_nh_master->max_external_nhid)
    {
        p_nh_info = ctc_vector_get(p_gb_nh_master->external_nhid_vec, nhid);
    }
    else if (nhid < SYS_NH_INTERNAL_NHID_MAX)
    {
        p_nh_info = ctc_vector_get(p_gb_nh_master->internal_nhid_vec,
                                   SYS_NH_GET_VECTOR_INDEX_BY_NHID(nhid));
    }
    else
    {
        return CTC_E_NH_INVALID_NHID;
    }

    if (NULL == p_nh_info)
    {
        return CTC_E_NH_NOT_EXIST;
    }

    *pp_nhinfo = p_nh_info;

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_get_nhinfo_by_nhid(uint8 lchip, uint32 nhid, sys_nh_info_com_t** pp_nhinfo)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    sys_nh_info_com_t* p_nhinfo = NULL;

    CTC_PTR_VALID_CHECK(pp_nhinfo);
    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    CTC_ERROR_RETURN(_sys_greatbelt_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo));

    *pp_nhinfo = p_nhinfo;

    return CTC_E_NONE;
}

uint8
sys_greatbelt_nh_get_nh_valid(uint8 lchip, uint32 nhid)
{
    int32 ret = CTC_E_NONE;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_greatbelt_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_nh_master));
    ret = _sys_greatbelt_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_nh_master, &p_nhinfo);
    if (ret || (NULL == p_nhinfo))
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

int32
sys_greatbelt_nh_is_cpu_nhid(uint8 lchip, uint32 nhid, uint8 *is_cpu_nh)
{
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_nh_info_misc_t* p_nh_misc_info = NULL;

    if (nhid == SYS_NH_RESOLVED_NHID_FOR_TOCPU)
    {
        *is_cpu_nh = 1;
    }

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid, &p_nhinfo));
    if (p_nhinfo->hdr.nh_entry_type == SYS_NH_TYPE_MISC)
    {
        p_nh_misc_info = (sys_nh_info_misc_t*)p_nhinfo;
        if (p_nh_misc_info == NULL)
        {
            return CTC_E_NONE;
        }
        if (p_nh_misc_info->misc_nh_type == CTC_MISC_NH_TYPE_TO_CPU)
        {
            *is_cpu_nh = 1;
        }
    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_nh_is_logic_repli_en(uint8 lchip, uint32 nhid, uint8 *is_repli_en)
{
    sys_nh_info_com_t* p_nhinfo = NULL;

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid, &p_nhinfo));
    *is_repli_en = CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LOGIC_REPLI_EN);

    return CTC_E_NONE;
}


int32
sys_greatbelt_nh_get_ecmp_mode(uint8 lchip, bool* ecmp_mode)
{

    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    *ecmp_mode = p_gb_nh_master->ecmp_mode ? TRUE : FALSE;
    return CTC_E_NONE;
}
int32
sys_greatbelt_nh_get_dsfwd_mode(uint8 lchip, uint8* have_dsfwd)
{

    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    *have_dsfwd = !p_gb_nh_master->no_dsfwd;
    return CTC_E_NONE;
}
int32
sys_greatbelt_nh_set_dsfwd_mode(uint8 lchip, uint8 have_dsfwd)
{

    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

     p_gb_nh_master->no_dsfwd =  !have_dsfwd;
    return CTC_E_NONE;
}
int32
sys_greatbelt_nh_get_max_ecmp(uint8 lchip, uint16* max_ecmp)
{

    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    *max_ecmp = p_gb_nh_master->max_ecmp;
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_set_max_ecmp(uint8 lchip, uint16 max_ecmp)
{

    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    /* the max ecmp num should be 2~16 */
    if (1 == max_ecmp)
    {
        return CTC_E_INVALID_PARAM;
    }
    else if (max_ecmp > SYS_GB_MAX_ECPN)
    {
        return CTC_E_NH_EXCEED_MAX_ECMP_NUM;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    if (p_gb_nh_master->cur_ecmp_cnt != 0)
    {
        return CTC_E_NH_ECMP_IN_USE;
    }

    if (max_ecmp > 8)
    { /*In asic, 9~16 is regarded as 16 */
        p_gb_nh_master->max_ecmp = SYS_GB_MAX_ECPN;
    }
    else
    {
        if (0 == max_ecmp)
        {
            p_gb_nh_master->ecmp_mode = 1; /* Use flex member num mode */
        }
        p_gb_nh_master->max_ecmp = max_ecmp;
    }

    return CTC_E_NONE;
}

/**
 @brief This function is used to get DsNexthop offset by NexthopID
 */
int32
sys_greatbelt_nh_get_dsnh_offset_by_nhid(uint8 lchip, uint32 nhid, uint16 *offset, uint8* p_use_dsnh8w)
{
    sys_nh_info_com_t* p_nhinfo;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    CTC_ERROR_RETURN(_sys_greatbelt_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo));

    *offset = p_nhinfo->hdr.dsfwd_info.dsnh_offset;  /* modified */


    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        *p_use_dsnh8w = TRUE;
    }

    return CTC_E_NONE;
}

/**
 @brief This function is used to get DsNexthop offset by NexthopID
 */
int32
sys_greatbelt_nh_get_mirror_info_by_nhid(uint8 lchip, uint32 nhid, uint16* dsnh_offset, uint32* gport, bool enable)
{
    sys_nh_info_com_t* p_nhinfo;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    uint8  gchip = 0;
    uint8  lport = 0;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    CTC_ERROR_RETURN(_sys_greatbelt_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo));


    switch (p_nhinfo->hdr.nh_entry_type)
    {
    case SYS_NH_TYPE_RSPAN:
        *gport = 0xFFFF;

        *dsnh_offset = p_nhinfo->hdr.dsfwd_info.dsnh_offset;

        break;

    case SYS_NH_TYPE_MCAST:
        {

            ds_phy_port_ext_t phy_port_ext;
            ds_phy_port_t phy_port;
            ds_src_port_t src_port;
            uint32 cmd = 0;
            uint8  mcast_mirror_port = 0;
            static uint8 in_use = 0;
            static uint32 mcast_mirror_nhid = 0;
            sys_nh_info_mcast_t* p_nhinfo_mcast = (sys_nh_info_mcast_t*)p_nhinfo;

            if (enable && in_use && (mcast_mirror_nhid != nhid))
            {
                return CTC_E_NH_MCAST_MIRROR_NH_IN_USE;
            }

            if (enable)
            {
                p_nhinfo_mcast->mirror_ref_cnt++;
                in_use = 1;
            }
            else
            {
                if (p_nhinfo_mcast->mirror_ref_cnt)
                {
                    p_nhinfo_mcast->mirror_ref_cnt--;
                }

                in_use = (p_nhinfo_mcast->mirror_ref_cnt != 0);
            }

            mcast_mirror_nhid = nhid;

            sys_greatbelt_internal_port_get_rsv_port(lchip, SYS_INTERNAL_PORT_TYPE_MIRROR,  &mcast_mirror_port);


            cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, mcast_mirror_port, cmd, &src_port));
            src_port.port_cross_connect  = 1;
            src_port.receive_disable        = 0;
            src_port.bridge_en                = 0;
            src_port.add_default_vlan_disable = 1;
            src_port.route_disable = 1;
            src_port.stp_disable = 1;
            src_port.learning_disable = 1;

            cmd = DRV_IOW(DsSrcPort_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, mcast_mirror_port, cmd, &src_port));

            cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, mcast_mirror_port, cmd, &phy_port_ext));

            phy_port_ext.ds_fwd_ptr = p_nhinfo->hdr.dsfwd_info.dsfwd_offset;
            phy_port_ext.default_vlan_id = 0;
            cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, mcast_mirror_port, cmd, &phy_port_ext));

            cmd = DRV_IOR(DsPhyPort_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, mcast_mirror_port, cmd, &phy_port));

            phy_port.packet_type_valid  = 1;
            phy_port.packet_type        = 7;
            cmd = DRV_IOW(DsPhyPort_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, mcast_mirror_port, cmd, &phy_port));

            sys_greatbelt_get_gchip_id(lchip, &gchip);
            *gport  = CTC_MAP_LPORT_TO_GPORT(gchip, SYS_RESERVE_PORT_ID_ILOOP);

            *dsnh_offset = SYS_NH_ENCODE_ILOOP_DSNH(mcast_mirror_port, 0, 0, 0);


        }
        break;

    case SYS_NH_TYPE_IP_TUNNEL:
        {
            sys_nh_info_ip_tunnel_t* p_ip_tunnel = (sys_nh_info_ip_tunnel_t*)p_nhinfo;

            CTC_ERROR_RETURN(sys_greatbelt_internal_port_get_rsv_port(lchip, SYS_INTERNAL_PORT_TYPE_IP_TUNNEL, &lport));
            if (lport == p_ip_tunnel->gport)
            {
                sys_greatbelt_get_gchip_id(lchip, &gchip);
                *gport = ( gchip << 8 )  | p_ip_tunnel->gport;
            }
            else
            {
                *gport = p_ip_tunnel->gport;
            }

            *dsnh_offset = p_nhinfo->hdr.dsfwd_info.dsnh_offset;

        }
        break;

    case  SYS_NH_TYPE_TOCPU:
        sys_greatbelt_get_gchip_id(lchip, &gchip);
        *gport = CTC_GPORT_RCPU(gchip);
        *dsnh_offset = (SYS_GREATBELT_DSNH_INTERNAL_BASE << SYS_GREATBELT_DSNH_INTERNAL_SHIFT)
                            | SYS_DSNH4WREG_INDEX_FOR_BYPASS;

        break;

    case SYS_NH_TYPE_ILOOP:
        {
            sys_nh_info_special_t* p_nh_spec_info = (sys_nh_info_special_t*)p_nhinfo;
            *gport = p_nh_spec_info->dest_gport;
            *dsnh_offset = p_nh_spec_info->hdr.dsfwd_info.dsnh_offset;
        }
        break;

    case SYS_NH_TYPE_MISC:
        {
            sys_nh_info_misc_t* p_nh_misc_info = (sys_nh_info_misc_t*)p_nhinfo;
            *gport = p_nh_misc_info->gport;
            *dsnh_offset = p_nh_misc_info->hdr.dsfwd_info.dsnh_offset;

        }
        break;
    default:
        return CTC_E_INVALID_PARAM;

    }

    return CTC_E_NONE;
}

/**
 @brief This function is used to get destport by NexthopID
 */
int32
sys_greatbelt_nh_get_port(uint8 lchip, uint32 nhid, uint8* aps_en, uint16* gport)
{
    sys_nh_info_com_t* p_nhinfo;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    uint8 gchip = 0;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    CTC_ERROR_RETURN(_sys_greatbelt_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo));

    *aps_en = 0;

    switch (p_nhinfo->hdr.nh_entry_type)
    {
    case SYS_NH_TYPE_BRGUC:
        {
            sys_nh_info_brguc_t* p_brguc_info;
            p_brguc_info = (sys_nh_info_brguc_t*)p_nhinfo;
            if ((p_brguc_info->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BASIC || p_brguc_info->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_VLAN_EDIT
                 || p_brguc_info->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BYPASS))
            {
                *gport = p_brguc_info->dest.dest_gport;
            }
            else if (p_brguc_info->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_APS_VLAN_EDIT)
            {
                *gport = p_brguc_info->dest.aps_bridge_group_id;
                *aps_en = 1;

            }
            else
            {
                return CTC_E_INVALID_PARAM;
            }
        }
        break;

    case SYS_NH_TYPE_IPUC:
        {
            sys_nh_info_ipuc_t* p_ipuc_info;
            p_ipuc_info = (sys_nh_info_ipuc_t*)(p_nhinfo);
            if (CTC_FLAG_ISSET(p_ipuc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
            {
                *gport = CTC_MAX_UINT16_VALUE;
            }
            else
            {
                *gport = p_ipuc_info->gport;
            }

            *aps_en = FALSE;
        }
        break;

    case SYS_NH_TYPE_MPLS:
        {
            sys_nh_info_mpls_t* p_mpls_info;
            p_mpls_info = (sys_nh_info_mpls_t*)(p_nhinfo);

            if (CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
            {
                *gport = CTC_MAX_UINT16_VALUE;
                *aps_en = 0;
            }
            else if (CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
            {
                *gport = p_mpls_info->aps_group_id;
                *aps_en = 1;
            }
            else
            {
                *gport = p_mpls_info->gport;
                *aps_en = 0;
            }
        }
        break;

    case SYS_NH_TYPE_MCAST:
        {
            *gport = CTC_MAX_UINT16_VALUE;
            *aps_en = 0;
        }
        break;

    case SYS_NH_TYPE_MISC:
        {
            sys_nh_info_misc_t* p_nh_info;
            p_nh_info = (sys_nh_info_misc_t*)(p_nhinfo);
            *gport = p_nh_info->gport;
        }
        break;

    case  SYS_NH_TYPE_DROP:
        {
            *gport = CTC_MAX_UINT16_VALUE;
            *aps_en = 0;
        }
        break;

    case  SYS_NH_TYPE_TOCPU:
        {
            sys_greatbelt_get_gchip_id(lchip, &gchip);
            *gport =  CTC_MAP_LPORT_TO_GPORT(gchip, SYS_RESERVE_PORT_ID_CPU);
            *aps_en = 0;
        }
        break;

    case SYS_NH_TYPE_IP_TUNNEL:
        {
            sys_nh_info_ip_tunnel_t* p_ip_tunnel_info;
            p_ip_tunnel_info = (sys_nh_info_ip_tunnel_t*)(p_nhinfo);
            if (CTC_FLAG_ISSET(p_ip_tunnel_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
            {
                *gport = CTC_MAX_UINT16_VALUE;
            }
            else
            {
                *gport = p_ip_tunnel_info->gport;
            }

            *aps_en = FALSE;
        }
        break;

    case SYS_NH_TYPE_ILOOP:
        {
            sys_nh_info_special_t* p_nh_info;
            p_nh_info = (sys_nh_info_special_t*)(p_nhinfo);
            *gport = p_nh_info->dest_gport;
        }
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/**
 @brief This function is used to allocate a new internal nexthop id
 */
STATIC int32
_sys_greatbelt_nh_new_nh_id(uint8 lchip, uint32* p_nhid)
{
    sys_greatbelt_opf_t opf;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(p_nhid);
    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = OPF_NH_NHID_INTERNAL;
    opf.pool_index = 0;
    opf.multiple  = 1;

    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, p_nhid));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "New allocated nhid = %d\n", *p_nhid);

    return CTC_E_NONE;
}

/**
 @brief This function is used to free a nexthop inforamtion data
 */
int32
_sys_greatbelt_nh_info_free(uint8 lchip, uint32 nhid)
{
    sys_greatbelt_opf_t opf;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Need freed nhid = %d\n", nhid);
    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    if ((nhid >= p_gb_nh_master->max_external_nhid) &&
        (nhid < SYS_NH_INTERNAL_NHID_MAX))
    {
        opf.pool_type = OPF_NH_NHID_INTERNAL;
        opf.pool_index = 0;
        CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, nhid));
        p_nhinfo = (sys_nh_info_com_t*)ctc_vector_del(p_gb_nh_master->internal_nhid_vec,
                                                      (nhid - p_gb_nh_master->max_external_nhid));
    }
    else if (nhid < p_gb_nh_master->max_external_nhid)
    {
        p_nhinfo = (sys_nh_info_com_t*)ctc_vector_del(p_gb_nh_master->external_nhid_vec, nhid);
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    mem_free(p_nhinfo);
    return CTC_E_NONE;
}

/**
 @brief This function is used to get resolved offset
 */
int32
sys_greatbelt_nh_get_resolved_offset(uint8 lchip, sys_greatbelt_nh_res_offset_type_t type, uint32* p_offset)
{
    sys_nh_offset_attr_t* p_res_offset;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(p_offset);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    /*Always get chip 0's offset, all chip should be same,
    maybe we don't need to store this value per-chip*/
    p_res_offset = &(p_gb_nh_master->sys_nh_resolved_offset[type]);
    *p_offset = p_res_offset->offset_base;
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Type = %d, resolved offset = %d\n", type, *p_offset);

    return CTC_E_NONE;
}

/**
 @brief This function is used to alloc dynamic offset

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_offset_alloc(uint8 lchip, sys_nh_entry_table_type_t entry_type,
                              uint32 entry_num, uint32* p_offset)
{
    sys_greatbelt_opf_t opf;
    uint32 entry_size;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_offset);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;
    entry_size = nh_table_info_array[entry_type].entry_size;
    opf.pool_type = nh_table_info_array[entry_type].opf_pool_type;
    entry_size = entry_size * entry_num;
    if (nh_table_info_array[entry_type].table_8w)
    {
        opf.multiple = 2;
    }
    else
    {
        opf.multiple = 1;
    }
    opf.reverse = (nh_table_info_array[entry_type].alloc_dir == 1) ? 1 : 0;
    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, entry_size, p_offset));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip = %d, entry_type = %d, entry_number = %d, offset = 0x%x\n",
                   lchip, entry_type, entry_num, *p_offset);
    p_gb_nh_master->nhtbl_used_cnt[entry_type] += entry_size;

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_offset_alloc_with_multiple(uint8 lchip, sys_nh_entry_table_type_t entry_type,
                                            uint32 entry_num, uint16 multi, uint32* p_offset)
{
    sys_greatbelt_opf_t opf;
    uint32 entry_size;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_offset);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;
    entry_size = nh_table_info_array[entry_type].entry_size;
    opf.pool_type = nh_table_info_array[entry_type].opf_pool_type;
    opf.multiple = multi;
    opf.reverse = (nh_table_info_array[entry_type].alloc_dir == 1) ? 1 : 0;
    entry_size = entry_size * entry_num;

    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, entry_size, p_offset));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip = %d, entry_type = %d, entry_number = %d, multi = %d, offset = 0x%x\n",
                   lchip, entry_type, entry_num, multi, *p_offset);
    p_gb_nh_master->nhtbl_used_cnt[entry_type] += entry_size;
    return CTC_E_NONE;
}

/**
 @brief This function is used to free dynamic offset

 */
int32
sys_greatbelt_nh_offset_free(uint8 lchip, sys_nh_entry_table_type_t entry_type,
                             uint32 entry_num, uint32 offset)
{

    sys_greatbelt_opf_t opf;
    uint32 entry_size;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    if (offset == 0)
    {/*offset 0 is reserved*/
        return CTC_E_NONE;
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip = %d, entry_type = %d, entry_number = %d, offset = %d\n",
                   lchip, entry_type, entry_num, offset);


    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;
    opf.pool_type = nh_table_info_array[entry_type].opf_pool_type;
    entry_size = nh_table_info_array[entry_type].entry_size;
    entry_size = entry_size * entry_num;
    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, entry_size, offset));

    if(p_gb_nh_master->nhtbl_used_cnt[entry_type] >= entry_size)
    {
       p_gb_nh_master->nhtbl_used_cnt[entry_type] -= entry_size;
    }
    return CTC_E_NONE;

}



int32
sys_greatbelt_nh_swap_nexthop_offset(uint8 lchip, uint16 nhid, uint16 nhid2)
{
    uint32 dsnh_offset = 0;
    uint32 dsnh_offset2 = 0;
    ds_next_hop4_w_t dsnh;
    ds_next_hop4_w_t dsnh2;
    uint32 cmd = 0;
    sys_nh_info_com_t*p_nhinfo = NULL;
    sys_nh_info_com_t*p_nhinfo2 = NULL;

    if (nhid == nhid2)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid, &p_nhinfo));
    if (NULL == p_nhinfo)
    {
        return CTC_E_NH_INVALID_NHID;
    }

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid2, &p_nhinfo2));
    if (NULL == p_nhinfo2)
    {
        return CTC_E_NH_INVALID_NHID;
    }

    dsnh_offset = p_nhinfo->hdr.dsfwd_info.dsnh_offset;
    dsnh_offset2 = p_nhinfo2->hdr.dsfwd_info.dsnh_offset;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Swap nhid%d, offset:%d, nhid2:%d, offset2:%d \n", nhid, dsnh_offset, nhid2, dsnh_offset2);

    cmd = DRV_IOR(DsNextHop4W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, dsnh_offset, cmd, &dsnh));
    cmd = DRV_IOR(DsNextHop4W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, dsnh_offset2, cmd, &dsnh2));

    cmd = DRV_IOW(DsNextHop4W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, dsnh_offset, cmd, &dsnh2));
    cmd = DRV_IOW(DsFwd_t, DsFwd_NextHopPtr_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nhinfo->hdr.dsfwd_info.dsfwd_offset, cmd, &dsnh_offset2));
    p_nhinfo->hdr.dsfwd_info.dsnh_offset = dsnh_offset2;

    cmd = DRV_IOW(DsNextHop4W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, dsnh_offset2, cmd, &dsnh));
    cmd = DRV_IOW(DsFwd_t, DsFwd_NextHopPtr_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nhinfo2->hdr.dsfwd_info.dsfwd_offset, cmd, &dsnh_offset));
    p_nhinfo2->hdr.dsfwd_info.dsnh_offset = dsnh_offset;

    return CTC_E_NONE;
}


int32
sys_greatbelt_nh_update_logic_port(uint8 lchip, uint32 nhid, uint32 logic_port)
{
    uint32 dsnh_offset = 0;
    sys_nh_info_com_t*p_nhinfo = NULL;
    ds_next_hop8_w_t dsnh_8w;

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid, &p_nhinfo));
    if (NULL == p_nhinfo)
    {
        return CTC_E_NH_INVALID_NHID;
    }

    if (!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        return CTC_E_NOT_SUPPORT;
    }

    dsnh_offset = p_nhinfo->hdr.dsfwd_info.dsnh_offset;

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_asic_table(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_8W, dsnh_offset, &dsnh_8w));

    dsnh_8w.logic_dest_port = logic_port;

    CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_8W, dsnh_offset, &dsnh_8w));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_alloc_logic_replicate_offset(uint8 lchip, sys_nh_info_com_t* p_nhinfo, uint32 dest_gport, uint32 group_id)
{
    ctc_slistnode_t* ctc_slistnode = NULL;
    sys_nh_repli_offset_node_t *p_offset_node = NULL;
    bool found = FALSE;
    int32 ret = 0;
    uint8 type = 0;
    uint8 multi = 0;
    uint32 offset = 0;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        type = SYS_NH_ENTRY_TYPE_NEXTHOP_8W;
        multi = 2;
    }
    else
    {
        type = SYS_NH_ENTRY_TYPE_NEXTHOP_4W;
        multi = 1;
    }


    CTC_SLIST_LOOP(p_gb_nh_master->repli_offset_list, ctc_slistnode)
    {
        p_offset_node = _ctc_container_of(ctc_slistnode, sys_nh_repli_offset_node_t, head);
        if (p_offset_node->bitmap != 0xFFFFFFFF &&
            p_offset_node->dest_gport == dest_gport &&
            p_offset_node->group_id == group_id)
        {
            found = TRUE;
            break;
        }
    }

    if (TRUE == found)
    {
        uint8 bit = 0;

        for (bit = 0; bit < 32; bit++)
        {
            if (!CTC_IS_BIT_SET(p_offset_node->bitmap, bit))
            {
                CTC_BIT_SET(p_offset_node->bitmap , bit);
                offset = bit*multi;
                p_nhinfo->hdr.dsfwd_info.dsnh_offset = p_offset_node->start_offset + offset;
                p_nhinfo->hdr.p_data = (void*)p_offset_node;

                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Update alloc_logic_replicate_offset :%d, gport:0x%x, group-id:%d\n",
                               p_nhinfo->hdr.dsfwd_info.dsnh_offset, dest_gport, group_id);

                return CTC_E_NONE;
            }
        }
    }

    p_offset_node =  mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_repli_offset_node_t));
    if (NULL == p_offset_node)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_offset_node, 0, sizeof(sys_nh_repli_offset_node_t));

    offset = SYS_NH_MCAST_LOGIC_MAX_CNT*multi;
    ret = sys_greatbelt_nh_offset_alloc_with_multiple(lchip, type,
                                                      SYS_NH_MCAST_LOGIC_MAX_CNT,
                                                      offset,
                                                      &p_offset_node->start_offset);
    if (ret < 0)
    {
        mem_free(p_offset_node);
        return ret;
    }

    CTC_BIT_SET(p_offset_node->bitmap , 0);
    p_offset_node->dest_gport = dest_gport;
    p_offset_node->group_id = group_id;

    p_nhinfo->hdr.dsfwd_info.dsnh_offset = p_offset_node->start_offset;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "New alloc_logic_replicate_offset :%d, gport:0x%x, group-id:%d\n",
                   p_nhinfo->hdr.dsfwd_info.dsnh_offset, dest_gport, group_id);

	ctc_slist_add_tail(p_gb_nh_master->repli_offset_list, &(p_offset_node->head));

    p_nhinfo->hdr.p_data = (void*)p_offset_node;

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_nh_free_logic_replicate_offset(uint8 lchip, sys_nh_info_com_t* p_nhinfo)
{
    sys_nh_repli_offset_node_t *p_offset_node = NULL;
    uint8 type = 0;
    uint8 multi = 0;
    uint8 bit = 0;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        type = SYS_NH_ENTRY_TYPE_NEXTHOP_8W;
        multi = 2;
    }
    else
    {
        type = SYS_NH_ENTRY_TYPE_NEXTHOP_4W;
        multi = 1;
    }


    p_offset_node = (sys_nh_repli_offset_node_t *)p_nhinfo->hdr.p_data;

    if (NULL == p_offset_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }


    bit =  (p_nhinfo->hdr.dsfwd_info.dsnh_offset - p_offset_node->start_offset) / multi;

    CTC_BIT_UNSET(p_offset_node->bitmap , bit);


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "free_logic_replicate_offset :%d, gport:0x%x\n", p_nhinfo->hdr.dsfwd_info.dsnh_offset, p_offset_node->dest_gport);

    if (p_offset_node->bitmap == 0)
    {

        ctc_slist_delete_node(p_gb_nh_master->repli_offset_list, &(p_offset_node->head));

        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, type,
                                                      SYS_NH_MCAST_LOGIC_MAX_CNT,
                                                      p_offset_node->start_offset));
        mem_free(p_offset_node);
        p_nhinfo->hdr.p_data = NULL;
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "free_offset node!!!!!\n");
    }

    return CTC_E_NONE;
}




STATIC int32
_sys_greatbelt_nh_alloc_offset_by_nhtype(uint8 lchip, sys_nh_param_com_t* p_nh_com_para, sys_nh_info_com_t* p_nhinfo)
{
    sys_greatbelt_nh_master_t* p_master;
    uint32 dsnh_offset = 0;
    uint8 use_rsv_dsnh = 0, remote_chip = 0,dsnh_entry_num = 0;
    uint8 pkt_nh_edit_mode  = SYS_NH_IGS_CHIP_EDIT_MODE;
    uint8 dest_gchip = 0;
    uint32 dest_gport = 0;
    uint8 xgpon_en = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " entry_type = %d\n", p_nh_com_para->hdr.nh_param_type);

    sys_greatbelt_get_gchip_id(lchip, &dest_gchip);
    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_master));

    switch (p_nh_com_para->hdr.nh_param_type)
    {
    case SYS_NH_TYPE_BRGUC:
        {
            sys_nh_param_brguc_t* p_nh_param = (sys_nh_param_brguc_t*)p_nh_com_para;
            sys_nh_info_brguc_t* p_brg_nhinfo = (sys_nh_info_brguc_t*)p_nhinfo;
            dsnh_offset = p_nh_param->dsnh_offset;
            p_brg_nhinfo->nh_sub_type = p_nh_param->nh_sub_type;
            if (p_nh_param->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_VLAN_EDIT)
            {
                if ((0 == p_nh_param->p_vlan_edit_info->flag)
                    &&(0 == p_nh_param->p_vlan_edit_info->edit_flag)
                    &&(0 == p_nh_param->p_vlan_edit_info->loop_nhid)
                    && (p_nh_param->p_vlan_edit_info->svlan_edit_type == CTC_VLAN_EGRESS_EDIT_NONE
                        && p_nh_param->p_vlan_edit_info->cvlan_edit_type == CTC_VLAN_EGRESS_EDIT_NONE)
                     && (0 == p_nh_param->p_vlan_edit_info->user_vlanptr))
                {
                    CTC_ERROR_RETURN(sys_greatbelt_nh_get_resolved_offset(lchip,
                                         SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH, &dsnh_offset));
                    p_nhinfo->hdr.dsnh_entry_num  = 1;
                    use_rsv_dsnh = 1;
                }
                else
                {
                    p_nhinfo->hdr.dsnh_entry_num  = 1;

                    if ((p_nh_param->loop_nhid)
                        || (CTC_FLAG_ISSET(p_nh_param->p_vlan_edit_info->flag, CTC_VLAN_NH_MTU_CHECK_EN))
                        || (p_nh_param->p_vlan_edit_nh_param && p_nh_param->p_vlan_edit_nh_param->logic_port_valid))
                    {
                        CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
                    }

                    /*!!!! ONLY for XGPON, nexthop doing logic Replicate*/
                    sys_greatbelt_global_get_xgpon_en(lchip, &xgpon_en);
                    if (xgpon_en)
                    {
                        if (CTC_FLAG_ISSET(p_nh_param->p_vlan_edit_info->flag, CTC_VLAN_NH_SERVICE_QUEUE_FLAG))
                        {
                            dest_gport = p_nh_param->gport;
                            p_nh_param->p_vlan_edit_info->stats_id = 0;
                            CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LOGIC_REPLI_EN);
                            CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HORIZON_SPLIT_EN);
                        }

                        if (CTC_FLAG_ISSET(p_nh_param->p_vlan_edit_info->flag, CTC_VLAN_NH_LENGTH_ADJUST_EN))
                        {
                            p_nh_param->p_vlan_edit_info->stats_id = 0;
                            use_rsv_dsnh = 1;
                            dsnh_offset = p_master->xlate_nh_offset + p_nh_param->p_vlan_edit_info->output_svid;
                        }
                    }

                }
                pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE;
                dest_gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_param->gport);

            }
            else if (p_nh_param->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_APS_VLAN_EDIT)
            {
                uint8 physical_isolated = 0;
                CTC_ERROR_RETURN(sys_greatbelt_aps_get_aps_physical_isolated(lchip, p_nh_param->gport, &physical_isolated));

                if (physical_isolated)
                {
                    p_nhinfo->hdr.dsnh_entry_num  = 1;
                    if (p_nh_param->p_vlan_edit_info->svlan_edit_type == CTC_VLAN_EGRESS_EDIT_NONE
                        && p_nh_param->p_vlan_edit_info->cvlan_edit_type == CTC_VLAN_EGRESS_EDIT_NONE)
                    {
                        CTC_ERROR_RETURN(sys_greatbelt_nh_get_resolved_offset(lchip,
                                             SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH, &dsnh_offset));
                        use_rsv_dsnh = 1;
                    }

                    CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH);
                }
                else
                {
                    p_nhinfo->hdr.dsnh_entry_num  = 2;
                    if ((CTC_FLAG_ISSET(p_nh_param->p_vlan_edit_info->flag, CTC_VLAN_NH_MTU_CHECK_EN))
                        || (CTC_FLAG_ISSET(p_nh_param->p_vlan_edit_info_prot_path->flag, CTC_VLAN_NH_MTU_CHECK_EN)))
                    {
                        CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
                    }
                }
                pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE;
            }
            else if (p_nh_param->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BASIC)
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_get_resolved_offset(lchip,
                                     SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH, &dsnh_offset));
                p_nhinfo->hdr.dsnh_entry_num  = 1;
                use_rsv_dsnh = 1;
            }
            else if (p_nh_param->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BYPASS)
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_get_resolved_offset(lchip,
                                     SYS_NH_RES_OFFSET_TYPE_BYPASS_NH, &dsnh_offset));

                p_nhinfo->hdr.dsnh_entry_num  = 1;
                use_rsv_dsnh = 1;
                CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
            }
        }
        break;

    case SYS_NH_TYPE_IPUC:
        {
            uint8 physical_isolated = 0;
            sys_nh_param_ipuc_t* p_nh_param = (sys_nh_param_ipuc_t*)p_nh_com_para;

            dsnh_offset = p_nh_param->dsnh_offset;
            if(p_nh_param->aps_en)
            {
                CTC_ERROR_RETURN(sys_greatbelt_aps_get_aps_physical_isolated(lchip, p_nh_param->aps_bridge_group_id, &physical_isolated));
                if (physical_isolated)
                {
                    CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH);
                    p_nhinfo->hdr.dsnh_entry_num  = 1;
                }
                else
                {
                    p_nhinfo->hdr.dsnh_entry_num  = 2;
                }

            }
            else
            {
                p_nhinfo->hdr.dsnh_entry_num  = 1;
                dest_gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_param->oif.gport);
            }
            if (p_master->pkt_nh_edit_mode == 1)
            {
                pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE;
            }
        }
        break;

    case SYS_NH_TYPE_IP_TUNNEL:
        {
            sys_nh_param_ip_tunnel_t* p_nh_param = (sys_nh_param_ip_tunnel_t*)p_nh_com_para;

            dsnh_offset = p_nh_param->p_ip_nh_param->dsnh_offset;
            p_nhinfo->hdr.dsnh_entry_num  = 1;
            pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE;
            if (!CTC_FLAG_ISSET(p_nh_param->p_ip_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_REROUTE_WITH_TUNNEL_HDR))
            {
                dest_gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_param->p_ip_nh_param->oif.gport);
            }
        }
        break;

    case SYS_NH_TYPE_MISC:
        {
            sys_nh_param_misc_t* p_nh_param = (sys_nh_param_misc_t*)p_nh_com_para;

            if(CTC_MISC_NH_TYPE_TO_CPU == p_nh_param->p_misc_param->type)
            {
                p_nhinfo->hdr.dsnh_entry_num  = 0;
            }
            else
            {
                dsnh_offset = p_nh_param->p_misc_param->dsnh_offset;
                p_nhinfo->hdr.dsnh_entry_num  = 1;
                if(CTC_MISC_NH_TYPE_REPLACE_L2_L3HDR == p_nh_param->p_misc_param->type)
                {
                    CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
                }
                pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE;
                dest_gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_param->p_misc_param->gport);
            }
        }
        break;

    case SYS_NH_TYPE_RSPAN:
        {
            sys_nh_param_rspan_t* p_nh_para = (sys_nh_param_rspan_t*)p_nh_com_para;
            dsnh_offset = p_nh_para->p_rspan_param->dsnh_offset;
            p_nhinfo->hdr.dsnh_entry_num  = 1;
            remote_chip = p_nh_para->p_rspan_param->remote_chip;
            pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE;
        }
        break;

    case SYS_NH_TYPE_MPLS:
        {
            sys_nh_param_mpls_t* p_nh_para = (sys_nh_param_mpls_t*)p_nh_com_para;

            ctc_vlan_egress_edit_info_t* p_vlan_info = &p_nh_para->p_mpls_nh_param->nh_para.nh_param_push.nh_com.vlan_info;

            dsnh_offset = p_nh_para->p_mpls_nh_param->dsnh_offset;
            if ((p_nh_para->p_mpls_nh_param->nh_prop == CTC_MPLS_NH_PUSH_TYPE) &&
                (p_nh_para->p_mpls_nh_param->nh_para.nh_param_push.nh_com.opcode == CTC_MPLS_NH_PUSH_OP_L2VPN)
                && (p_vlan_info != NULL)
                && ((CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUTPUT_SVLAN_TPID_VALID))
                     ||((CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID))
                     ||(CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID)))))
            {
                CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
            }

            if (p_nh_para->p_mpls_nh_param->aps_en)
            {
                p_nhinfo->hdr.dsnh_entry_num  = 2;
            }
            else
            {
                p_nhinfo->hdr.dsnh_entry_num  = 1;
                if (CTC_MPLS_NH_PUSH_TYPE == p_nh_para->p_mpls_nh_param->nh_prop)
                {
                    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel = NULL;
                    CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_mpls_tunnel(lchip, p_nh_para->p_mpls_nh_param->nh_para.nh_param_push.tunnel_id, &p_mpls_tunnel));
                    if (!p_mpls_tunnel)
                    {
                        return CTC_E_NH_NOT_EXIST_TUNNEL_LABEL;
                    }
                    if (!CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS)
                        && !CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS))
                    {
                        dest_gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_mpls_tunnel->gport);
                    }
                }
                else if(CTC_MPLS_NH_POP_TYPE == p_nh_para->p_mpls_nh_param->nh_prop)
                {
                    dest_gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_para->p_mpls_nh_param->nh_para.nh_param_pop.nh_com.oif.gport);
                }
            }
            pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE;
        }
        break;

    case SYS_NH_TYPE_ECMP:
    case SYS_NH_TYPE_MCAST:
    case SYS_NH_TYPE_DROP:
    case SYS_NH_TYPE_TOCPU:
    case SYS_NH_TYPE_UNROV:
    case SYS_NH_TYPE_ILOOP:
        p_nhinfo->hdr.dsnh_entry_num  = 0;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    if(p_nhinfo->hdr.dsnh_entry_num == 0)
    {
         return CTC_E_NONE;
     }

        if (use_rsv_dsnh)
        {
            p_nhinfo->hdr.dsfwd_info.dsnh_offset = dsnh_offset;
            CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_RSV_DSNH);

        }
        else if(CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LOGIC_REPLI_EN))
        {
            CTC_ERROR_RETURN(_sys_greatbelt_nh_alloc_logic_replicate_offset(lchip, p_nhinfo, dest_gport, dsnh_offset));
            CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH);
        }
        else if ((p_master->pkt_nh_edit_mode == 1 && (pkt_nh_edit_mode == SYS_NH_EGS_CHIP_EDIT_MODE) && dsnh_offset)
                          || (p_master->pkt_nh_edit_mode > 1 && (pkt_nh_edit_mode  == SYS_NH_EGS_CHIP_EDIT_MODE)))
        {
            if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
            {
                dsnh_entry_num = p_nhinfo->hdr.dsnh_entry_num  * 2;
            }
            else
            {
                dsnh_entry_num = p_nhinfo->hdr.dsnh_entry_num;
            }
            p_nhinfo->hdr.dsfwd_info.dsnh_offset = dsnh_offset;
            CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE);
            if (!SYS_GCHIP_IS_REMOTE(lchip, dest_gchip))
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_check_max_glb_nh_offset(lchip, dsnh_offset));
                CTC_ERROR_RETURN(sys_greatbelt_nh_check_glb_nh_offset(lchip, dsnh_offset, dsnh_entry_num, TRUE));
                CTC_ERROR_RETURN(sys_greatbelt_nh_set_glb_nh_offset(lchip, p_nhinfo->hdr.dsfwd_info.dsnh_offset, dsnh_entry_num, TRUE));
            }
        }
        else
        {
            if (p_nh_com_para->hdr.nh_param_type == SYS_NH_TYPE_RSPAN && remote_chip)
            {
                sys_nh_info_rspan_t* p_rspan_nhinfo = (sys_nh_info_rspan_t*)p_nhinfo;
                sys_nh_param_rspan_t* p_nh_para = (sys_nh_param_rspan_t*)p_nh_com_para;

                if (p_master->rspan_nh_in_use
                    && (p_master->rspan_vlan_id != p_nh_para->p_rspan_param->rspan_vid))
                {
                    return CTC_E_NH_CROSS_CHIP_RSPAN_NH_IN_USE;
                }
                else
                {
                    dsnh_offset = SYS_DSNH_FOR_REMOTE_MIRROR;
                    p_master->rspan_nh_in_use = 1;
                    p_master->rspan_vlan_id  = p_nh_para->p_rspan_param->rspan_vid;
                    p_rspan_nhinfo->remote_chip = 1;

                }
            }
            else
            {
                if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
                {
                    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_8W, p_nhinfo->hdr.dsnh_entry_num, &dsnh_offset));
                }
                else
                {
                    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, p_nhinfo->hdr.dsnh_entry_num, &dsnh_offset));

                }

                CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH);
            }

            p_nhinfo->hdr.dsfwd_info.dsnh_offset = dsnh_offset;
        }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_free_offset_by_nhinfo(uint8 lchip, sys_greatbelt_nh_type_t nh_type, sys_nh_info_com_t* p_nhinfo)
{
    sys_greatbelt_nh_master_t* p_master;
    uint8 dsnh_entry_num = 0;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_master));

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_RSV_DSNH))
    {
        return CTC_E_NONE;
    }

    if ((nh_type == SYS_NH_TYPE_RSPAN)
         && (((p_master->pkt_nh_edit_mode == SYS_NH_EGS_CHIP_EDIT_MODE))
         || (p_master->pkt_nh_edit_mode == SYS_NH_IGS_CHIP_EDIT_MODE)))
    {
        sys_nh_info_rspan_t* p_rspan_nhinfo = (sys_nh_info_rspan_t*)p_nhinfo;
        if (p_rspan_nhinfo->remote_chip)
        {
            p_master->rspan_nh_in_use = 0;
            return CTC_E_NONE;
        }
    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LOGIC_REPLI_EN))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_nh_free_logic_replicate_offset(lchip, p_nhinfo));
        CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH);
    }
    else if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH))
    {
        /*for two chip  nexthop only use one pool, so free only 1*/
        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_8W, p_nhinfo->hdr.dsnh_entry_num, p_nhinfo->hdr.dsfwd_info.dsnh_offset));
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, p_nhinfo->hdr.dsnh_entry_num, p_nhinfo->hdr.dsfwd_info.dsnh_offset));
        }

        CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH);

    }
    else
    {
        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
        {
            dsnh_entry_num = p_nhinfo->hdr.dsnh_entry_num *2;
        }
        else
        {
            dsnh_entry_num = p_nhinfo->hdr.dsnh_entry_num;
        }
        CTC_ERROR_RETURN(sys_greatbelt_nh_set_glb_nh_offset(lchip, p_nhinfo->hdr.dsfwd_info.dsnh_offset,dsnh_entry_num , FALSE));
    }


    return CTC_E_NONE;
}

/**
 @brief This function is used to create nexthop
 */
int32
sys_greatbelt_nh_api_create(uint8 lchip, sys_nh_param_com_t* p_nh_com_para)
{
    sys_greatbelt_nh_type_t nh_type;
    p_sys_nh_create_cb_t nh_sem_map_cb;
    sys_nh_info_com_t* p_nhinfo = NULL;
    int32 ret = CTC_E_NONE;
    uint32 tmp_nh_id, db_entry_size = 0;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    /*1. Sanity check and init*/
    if (NULL == p_nh_com_para)
    {
        return CTC_E_INVALID_PARAM;
    }


    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    SYS_NH_LOCK(p_gb_nh_master->p_mutex);

    if (!p_nh_com_para->hdr.is_internal_nh
        && (p_nh_com_para->hdr.nh_param_type != SYS_NH_TYPE_DROP)
        && (p_nh_com_para->hdr.nh_param_type != SYS_NH_TYPE_TOCPU)
        && (p_nh_com_para->hdr.nhid != CTC_NH_RESERVED_NHID_FOR_NONE))
    {
        if (p_nh_com_para->hdr.nhid < SYS_NH_RESOLVED_NHID_MAX ||
            p_nh_com_para->hdr.nhid >= (p_gb_nh_master->max_external_nhid))
        {
            SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
            return CTC_E_NH_INVALID_NHID;
        }
    }

    /*Check If this nexthop is exist*/
    if ((!(p_nh_com_para->hdr.is_internal_nh)) &&
        (NULL != ctc_vector_get(p_gb_nh_master->external_nhid_vec, p_nh_com_para->hdr.nhid)))
    {
        SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
        return CTC_E_NH_EXIST;
    }

    nh_type = p_nh_com_para->hdr.nh_param_type;
    ret = _sys_greatbelt_nh_get_nhentry_size_by_nhtype(lchip, nh_type, &db_entry_size);
    if (ret)
    {
        SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
        return CTC_E_NH_INVALID_NH_TYPE;
    }

    p_nhinfo = mem_malloc(MEM_NEXTHOP_MODULE, db_entry_size);
    if (NULL == p_nhinfo)
    {
        SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
        return CTC_E_NO_MEMORY;
    }
    else
    {
        sal_memset(p_nhinfo, 0, db_entry_size);
    }

    ret = _sys_greatbelt_nh_alloc_offset_by_nhtype(lchip, p_nh_com_para, p_nhinfo);
    if (ret)
    {
        mem_free(p_nhinfo);
        SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
        return ret;
    }

    /*2. Semantic mapping Callback*/
    nh_sem_map_cb = p_gb_nh_master->callbacks_nh_create[nh_type];
    ret = (* nh_sem_map_cb)(lchip, p_nh_com_para, p_nhinfo);
    if (ret)
    {
        _sys_greatbelt_nh_free_offset_by_nhinfo(lchip, nh_type, p_nhinfo);
        mem_free(p_nhinfo);
        SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
        return ret;
    }

    /*3. Store nh infor into nh_array*/
    if (p_nh_com_para->hdr.is_internal_nh)
    {
        /*alloc a new nhid*/
        ret = ret ? ret : _sys_greatbelt_nh_new_nh_id(lchip, &tmp_nh_id);
        if (FALSE == ctc_vector_add(p_gb_nh_master->internal_nhid_vec,
                                    (tmp_nh_id - p_gb_nh_master->max_external_nhid), p_nhinfo))
        {
            _sys_greatbelt_nh_free_offset_by_nhinfo(lchip, nh_type, p_nhinfo);
            mem_free(p_nhinfo);
            SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
            return CTC_E_NO_MEMORY;
        }

        p_nh_com_para->hdr.nhid = tmp_nh_id; /*Write back the nhid*/
        if ( nh_type == SYS_NH_TYPE_MCAST)
        {
            sys_nh_info_mcast_t* p_mcast_nhinfo = (sys_nh_info_mcast_t*)p_nhinfo;
            p_mcast_nhinfo->nhid = tmp_nh_id;
        }
    }
    else
    {
        if (FALSE == ctc_vector_add(p_gb_nh_master->external_nhid_vec,
                                    (p_nh_com_para->hdr.nhid), p_nhinfo))
        {
            _sys_greatbelt_nh_free_offset_by_nhinfo(lchip, nh_type, p_nhinfo);
            mem_free(p_nhinfo);
            SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
            return CTC_E_NO_MEMORY;
        }
    }

    SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
    if (ret == CTC_E_NONE && nh_type == SYS_NH_TYPE_ECMP)
    {
        p_gb_nh_master->cur_ecmp_cnt++;
    }
    p_gb_nh_master->nhid_used_cnt[nh_type]++;
    return ret;
}

/**
 @brief This function is used to delete nexthop

 @param[in] nhid, nexthop id

 @param[in] p_nh_db, the pointer used to store nexthop information

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_api_delete(uint8 lchip, uint32 nhid, sys_greatbelt_nh_type_t nhid_type)
{
    p_sys_nh_delete_cb_t nh_del_cb;
    sys_greatbelt_nh_type_t nh_type;
    sys_nh_info_com_t* p_nh_info = NULL;
    int32 ret = CTC_E_NONE;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    SYS_NH_LOCK(p_gb_nh_master->p_mutex);

    CTC_ERROR_RETURN_WITH_UNLOCK(
        _sys_greatbelt_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nh_info),
        p_gb_nh_master->p_mutex);

    if ((nhid_type != p_nh_info->hdr.nh_entry_type))
    {
        SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
        return CTC_E_NH_NHID_NOT_MATCH_NHTYPE;
    }

    nh_type = p_nh_info->hdr.nh_entry_type;

    nh_del_cb = p_gb_nh_master->callbacks_nh_delete[nh_type];

    ret = (* nh_del_cb)(lchip, p_nh_info, & nhid);

    ret = ret ? ret : _sys_greatbelt_nh_free_offset_by_nhinfo(lchip, nh_type, p_nh_info);

    ret = ret ? ret : _sys_greatbelt_nh_info_free(lchip, nhid);

    SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
    if (ret == CTC_E_NONE && nh_type == SYS_NH_TYPE_ECMP)
    {
        p_gb_nh_master->cur_ecmp_cnt = (p_gb_nh_master->cur_ecmp_cnt > 0) ? (p_gb_nh_master->cur_ecmp_cnt - 1) : p_gb_nh_master->cur_ecmp_cnt;
    }
    if(p_gb_nh_master->nhid_used_cnt[nh_type])
    {
      p_gb_nh_master->nhid_used_cnt[nh_type]--;
    }
    return ret;
}

/**
 @brief This function is used to update nexthop

 @param[in] nhid, nexthop id

 @param[in] p_nh_db, the pointer used to store nexthop information

 @param[in] p_nh_com_para, parameters used to update the nexthop

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_api_update(uint8 lchip, uint32 nhid, sys_nh_param_com_t* p_nh_com_para)
{
    p_sys_nh_update_cb_t nh_update_cb;
    sys_greatbelt_nh_type_t nh_type;
    sys_nh_info_com_t* p_nh_info;
    int32 ret = CTC_E_NONE;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    SYS_NH_LOCK(p_gb_nh_master->p_mutex);

    CTC_ERROR_RETURN_WITH_UNLOCK(
        _sys_greatbelt_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nh_info),
        p_gb_nh_master->p_mutex);

    nh_type = p_nh_com_para->hdr.nh_param_type;
    if ((nh_type != p_nh_info->hdr.nh_entry_type))
    {
        SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
        return CTC_E_NH_NHID_NOT_MATCH_NHTYPE;
    }

    if (CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
    {
        if(p_nh_com_para->hdr.change_type == SYS_NH_CHANGE_TYPE_FWD_TO_UNROV)
        {
            SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
            return CTC_E_NONE;
        }
    }
    else
    {
        if(p_nh_com_para->hdr.change_type == SYS_NH_CHANGE_TYPE_UNROV_TO_FWD)
        {
            p_nh_com_para->hdr.change_type = SYS_NH_CHANGE_TYPE_UPDATE_FWD_ATTR;
        }
    }

    nh_update_cb = p_gb_nh_master->callbacks_nh_update[nh_type];
    ret = (* nh_update_cb)(lchip, p_nh_info, p_nh_com_para);

    SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);

    return ret;
}

/**
 @brief This function is used to get l3ifid

 @param[in] nhid, nexthop id

 @param[out] p_l3ifid, l3 interface id

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_get_l3ifid(uint8 lchip, uint32 nhid, uint16* p_l3ifid)
{
    sys_nh_info_com_t* p_nhinfo;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    sys_nh_info_ipuc_t* p_ipuc_info;

    CTC_PTR_VALID_CHECK(p_l3ifid);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    if (nhid == SYS_NH_RESOLVED_NHID_FOR_DROP ||
        nhid == SYS_NH_RESOLVED_NHID_FOR_TOCPU)
    {
        *p_l3ifid = SYS_L3IF_INVALID_L3IF_ID;
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    SYS_NH_LOCK(p_gb_nh_master->p_mutex);

    CTC_ERROR_RETURN_WITH_UNLOCK(
        _sys_greatbelt_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo),
        p_gb_nh_master->p_mutex);

    switch (p_nhinfo->hdr.nh_entry_type)
    {
    case SYS_NH_TYPE_IPUC:
        p_ipuc_info = (sys_nh_info_ipuc_t*)(p_nhinfo);
        *p_l3ifid = p_ipuc_info->l3ifid;
        break;

    case SYS_NH_TYPE_MPLS:
        {
            sys_nh_info_mpls_t* p_mpls_info;
            p_mpls_info = (sys_nh_info_mpls_t*)(p_nhinfo);
            *p_l3ifid = p_mpls_info->l3ifid;
            break;
        }

    case SYS_NH_TYPE_IP_TUNNEL:
        {
            sys_nh_info_ip_tunnel_t* p_ip_tunnel_info;
            p_ip_tunnel_info = (sys_nh_info_ip_tunnel_t*)(p_nhinfo);
            *p_l3ifid = p_ip_tunnel_info->l3ifid;
            break;
        }

    default:
        SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
        return CTC_E_NH_INVALID_NH_TYPE;
    }

    SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_get_statsptr(uint8 lchip, uint32 nhid,  uint16* stats_ptr)
{
    sys_nh_info_com_t* p_nhinfo;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    SYS_NH_LOCK(p_gb_nh_master->p_mutex);

    CTC_ERROR_RETURN_WITH_UNLOCK(
        _sys_greatbelt_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo),
        p_gb_nh_master->p_mutex);


    *stats_ptr = p_nhinfo->hdr.dsfwd_info.stats_ptr;


    SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);

    return CTC_E_NONE;
}

/**
 @brief This function is used to get bypass dsnexthop offset
 */
int32
sys_greatbelt_nh_get_bypass_dsnh_offset(uint8 lchip, uint32* p_offset)
{
    int32 ret;

    CTC_PTR_VALID_CHECK(p_offset);
    ret = sys_greatbelt_nh_get_resolved_offset(lchip,
            SYS_NH_RES_OFFSET_TYPE_BYPASS_NH, p_offset);

    return ret;
}

/**
 @brief This function is used to get mirror dsnexthop offset
 */
int32
sys_greatbelt_nh_get_mirror_dsnh_offset(uint8 lchip, uint32* p_offset)
{
    int32 ret;

    CTC_PTR_VALID_CHECK(p_offset);
    ret = sys_greatbelt_nh_get_resolved_offset(lchip,
            SYS_NH_RES_OFFSET_TYPE_MIRROR_NH, p_offset);

    return ret;
}

/**
 @brief This function is used to get  ipmc's l2edit offset
 */
int32
sys_greatbelt_nh_get_ipmc_l2edit_offset(uint8 lchip, uint16* p_l2edit_offset)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    *p_l2edit_offset = p_gb_nh_master->ipmc_resolved_l2edit;

    return CTC_E_NONE;
}

/**
 @brief This function is used to get  reflective's loopback dsfwd_offset
 */
int32
sys_greatbelt_nh_get_reflective_dsfwd_offset(uint8 lchip, uint16* p_dsfwd_offset)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    *p_dsfwd_offset = p_gb_nh_master->reflective_resolved_dsfwd_offset;

    return CTC_E_NONE;
}

/**
 @brief This function is used to get  ipmc's l2edit offset
 */
int32
sys_greatbelt_nh_get_ipmc_dsnh_offset(uint8 lchip, uint16 l3ifid, uint8 mtu_no_chk, uint32* p_dsnh_offset)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    if (l3ifid > SYS_GB_MAX_CTC_L3IF_ID)
    {
        return CTC_E_L3IF_INVALID_IF_ID;
    }

    if (p_gb_nh_master->ipmc_dsnh_offset[l3ifid] == 0)
    {

        sys_nh_param_dsnh_t dsnh_param;
        sys_l3if_prop_t l3if_prop;
        uint16 l2edit_ptr = 0;

        sal_memset(&dsnh_param, 0, sizeof(sys_nh_param_dsnh_t));

        l3if_prop.l3if_id = l3ifid;
        CTC_ERROR_RETURN(sys_greatbelt_l3if_get_l3if_info(lchip, 1, &l3if_prop));
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_ipmc_l2edit_offset(lchip, &l2edit_ptr));
        dsnh_param.l2edit_ptr = l2edit_ptr;
        dsnh_param.l3edit_ptr = 0;
        dsnh_param.dest_vlan_ptr = l3if_prop.vlan_ptr;
        dsnh_param.dsnh_type = SYS_NH_PARAM_DSNH_TYPE_IPMC;
        dsnh_param.mtu_no_chk = mtu_no_chk;


        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, 1,
                                                       &dsnh_param.dsnh_offset));

        CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsnh4w(lchip, &dsnh_param));
        p_gb_nh_master->ipmc_dsnh_offset[l3ifid] = dsnh_param.dsnh_offset;
        *p_dsnh_offset = dsnh_param.dsnh_offset;

    }
    else
    {
        *p_dsnh_offset = p_gb_nh_master->ipmc_dsnh_offset[l3ifid];
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_del_ipmc_dsnh_offset(uint8 lchip, uint16 l3ifid)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    if (p_gb_nh_master->ipmc_dsnh_offset[l3ifid] != 0)
    {
        sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, 1,
                                     p_gb_nh_master->ipmc_dsnh_offset[l3ifid]);
        p_gb_nh_master->ipmc_dsnh_offset[l3ifid] = 0;
    }

    return CTC_E_NONE;
}

/**
 @brief This function is used to get mcast member
 */
int32
sys_greatbelt_nh_get_mcast_member(uint8 lchip, uint32 nhid, ctc_nh_info_t* p_nh_info)
{
    sys_nh_info_com_t* p_com_db = NULL;
    sys_nh_info_mcast_t* p_mcast_db = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    SYS_NH_LOCK(p_gb_nh_master->p_mutex);

    CTC_ERROR_RETURN_WITH_UNLOCK(
        _sys_greatbelt_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_com_db),
        p_gb_nh_master->p_mutex);

    p_mcast_db = (sys_nh_info_mcast_t*)p_com_db;

    CTC_ERROR_RETURN_WITH_UNLOCK(
        sys_greatbelt_nh_get_mcast_member_info(lchip, p_mcast_db, p_nh_info),
        p_gb_nh_master->p_mutex);
    SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);

    return CTC_E_NONE;
}


/**
 @brief This function is used to get max global dynamic offset
 */
int32
sys_greatbelt_nh_check_max_glb_nh_offset(uint8 lchip, uint32 offset)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;


    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    if (offset >= p_gb_nh_master->max_glb_nh_offset)
    {
        return CTC_E_NH_GLB_SRAM_INDEX_EXCEED;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_check_max_glb_met_offset(uint8 lchip, uint32 offset)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    if (offset >= p_gb_nh_master->max_glb_met_offset)
    {
        return CTC_E_NH_GLB_SRAM_INDEX_EXCEED;
    }

    return CTC_E_NONE;
}

/*Following function just for debug*/
int32
sys_greatbelt_nh_debug_get_nh_master(uint8 lchip, sys_greatbelt_nh_master_t** p_gb_nh_master)
{
    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, p_gb_nh_master));
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_debug_get_nhinfo_by_nhid(uint8 lchip, uint32 nhid, sys_greatbelt_nh_master_t* p_gb_nh_master,
                                          sys_nh_info_com_t** pp_nhinfo)
{
    return (_sys_greatbelt_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, pp_nhinfo));
}

/* now this api only used for vrfid stats */
int32
sys_greatbelt_nh_add_stats_action(uint8 lchip, uint32 nhid, uint16 vrfid)
{
    int32 ret = CTC_E_NONE;
    uint32 offset;
    uint16 stats_ptr;
    ds_fwd_t dsfwd;
    sys_nh_info_com_t* p_nh_info = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    uint8 is_vrfid_stats = TRUE;


    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    SYS_NH_LOCK(p_gb_nh_master->p_mutex);

    CTC_ERROR_RETURN_WITH_UNLOCK(
        _sys_greatbelt_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nh_info),
        p_gb_nh_master->p_mutex);

    if (!p_nh_info)
    {
        SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
        return CTC_E_NH_NOT_EXIST;
    }

    /* for ipuc nexthop, it may not have dsfwd */
    if (!CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        /* here have dead lock */
         /*CTC_ERROR_RETURN_WITH_UNLOCK(sys_greatbelt_nh_get_dsfwd_offset(lchip, nhid, fwd_offset), p_gb_nh_master->p_mutex);  // add a dsfwd */
    }


    if (!is_vrfid_stats)
    {

        if (SYS_GREATBELT_INVALID_STATS_PTR != p_nh_info->hdr.dsfwd_info.stats_ptr)
        {
            SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
            return CTC_E_NH_STATS_EXIST;
        }


        /*get stats ptr*/

        ret = sys_greatbelt_stats_alloc_flow_stats_ptr(lchip, SYS_STATS_FLOW_TYPE_DSFWD, &stats_ptr);
        if (CTC_E_NONE != ret)
        {

        }

        p_nh_info->hdr.dsfwd_info.stats_ptr = stats_ptr;


        /*for rollback*/
        if (CTC_E_NONE != ret)
        {

            if (SYS_GREATBELT_INVALID_STATS_PTR != stats_ptr)
            {
                sys_greatbelt_stats_free_flow_stats_ptr(lchip, SYS_STATS_FLOW_TYPE_DSFWD, stats_ptr);
                p_nh_info->hdr.dsfwd_info.stats_ptr = SYS_GREATBELT_INVALID_STATS_PTR;
            }

            SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
            return ret;
        }
    }
    else
    {
        sys_greatbelt_stats_get_vrf_statsptr(lchip, vrfid, &stats_ptr);
        p_nh_info->hdr.dsfwd_info.stats_ptr = stats_ptr;
    }

    /*write hw*/

    offset = p_nh_info->hdr.dsfwd_info.dsfwd_offset;
    CTC_ERROR_RETURN_WITH_UNLOCK(
    sys_greatbelt_nh_get_asic_table(lchip, SYS_NH_ENTRY_TYPE_FWD, offset, &dsfwd),
            p_gb_nh_master->p_mutex);

    dsfwd.stats_ptr_low = stats_ptr & 0xFFF;
    dsfwd.stats_ptr_high = (stats_ptr >> 12) & 0xF;

    CTC_ERROR_RETURN_WITH_UNLOCK(
    sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_FWD, offset, &dsfwd),
            p_gb_nh_master->p_mutex);


    SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_del_stats_action(uint8 lchip, uint32 nhid)
{
    uint16 stats_ptr;
    uint32 offset;
    ds_fwd_t dsfwd;
    sys_nh_info_com_t* p_nh_info = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    SYS_NH_LOCK(p_gb_nh_master->p_mutex);

    CTC_ERROR_RETURN_WITH_UNLOCK(
        _sys_greatbelt_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nh_info),
        p_gb_nh_master->p_mutex);

    if (!p_nh_info)
    {
        SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
        return CTC_E_NH_NOT_EXIST;
    }

    if (SYS_GREATBELT_INVALID_STATS_PTR == p_nh_info->hdr.dsfwd_info.stats_ptr)
    {
        SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
        return CTC_E_NH_STATS_NOT_EXIST;
    }

    stats_ptr = p_nh_info->hdr.dsfwd_info.stats_ptr;
    CTC_ERROR_RETURN_WITH_UNLOCK(
    sys_greatbelt_stats_free_flow_stats_ptr(lchip, SYS_STATS_FLOW_TYPE_DSFWD, stats_ptr),
            p_gb_nh_master->p_mutex);
    p_nh_info->hdr.dsfwd_info.stats_ptr = SYS_GREATBELT_INVALID_STATS_PTR;

    offset = p_nh_info->hdr.dsfwd_info.dsfwd_offset;
    CTC_ERROR_RETURN_WITH_UNLOCK(
    sys_greatbelt_nh_get_asic_table(lchip, SYS_NH_ENTRY_TYPE_FWD, offset, &dsfwd),
            p_gb_nh_master->p_mutex);

    dsfwd.stats_ptr_low = 0;
    dsfwd.stats_ptr_high = 0;

    CTC_ERROR_RETURN_WITH_UNLOCK(
    sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_FWD, offset, &dsfwd),
            p_gb_nh_master->p_mutex);


    SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_get_stats_result(uint8 lchip, ctc_nh_stats_info_t* stats_info)
{
    sys_nh_info_com_t* p_nh_info = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    sys_nh_info_mpls_t* p_mpls_nh_info = NULL;
    sys_nh_info_mcast_t* p_mcast_nh_info = NULL;
    ctc_stats_basic_t stats;
    uint16 stats_ptr = 0;
    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    sal_memset(&stats, 0, sizeof(ctc_stats_basic_t));
    sal_memset(&stats_info->stats, 0, sizeof(ctc_stats_basic_t));


    SYS_NH_LOCK(p_gb_nh_master->p_mutex);
    if (CTC_FLAG_ISSET(stats_info->stats_type, CTC_NH_STATS_TYPE_NHID))
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(
            _sys_greatbelt_nh_api_get_nhinfo_by_nhid(lchip, stats_info->id.nhid, p_gb_nh_master, &p_nh_info),
            p_gb_nh_master->p_mutex);

        if (!p_nh_info)
        {
            SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
            return CTC_E_NH_NOT_EXIST;
        }

        /* only support mpls and mcast nexthop */
        if (SYS_NH_TYPE_MPLS == p_nh_info->hdr.nh_entry_type)
        {
            p_mpls_nh_info = (sys_nh_info_mpls_t*)p_nh_info;

            if (!stats_info->is_protection_path)
            {
                stats_ptr = p_mpls_nh_info->working_path.stats_ptr;
            }
            else
            {
                stats_ptr = p_mpls_nh_info->protection_path->stats_ptr;
            }

            if (stats_ptr != SYS_GREATBELT_INVALID_STATS_PTR)
            {
                CTC_ERROR_RETURN_WITH_UNLOCK(
                sys_greatbelt_stats_get_flow_stats(lchip, stats_ptr, 0, &stats),
                        p_gb_nh_master->p_mutex);

                stats_info->stats.byte_count += stats.byte_count;
                stats_info->stats.packet_count += stats.packet_count;
            }
            else
            {
                SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
                return CTC_E_NH_STATS_NOT_EXIST;
            }

        }
        else if (SYS_NH_TYPE_MCAST == p_nh_info->hdr.nh_entry_type)
        {
            p_mcast_nh_info = (sys_nh_info_mcast_t*)p_nh_info;

            stats_ptr = p_mcast_nh_info->hdr.dsfwd_info.stats_ptr;

            if (stats_ptr != SYS_GREATBELT_INVALID_STATS_PTR)
            {
                CTC_ERROR_RETURN_WITH_UNLOCK(
                sys_greatbelt_stats_get_flow_stats(lchip, stats_ptr, 0, &stats),
                        p_gb_nh_master->p_mutex);

                stats_info->stats.byte_count += stats.byte_count;
                stats_info->stats.packet_count += stats.packet_count;
            }
            else
            {
                SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
                return CTC_E_NH_STATS_NOT_EXIST;
            }

        }
        else
        {
            SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
            return CTC_E_FEATURE_NOT_SUPPORT;
        }
    }
    else if (CTC_FLAG_ISSET(stats_info->stats_type, CTC_NH_STATS_TYPE_TUNNELID))
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(sys_greatbelt_nh_lkup_mpls_tunnel(lchip, stats_info->id.tunnel_id, &p_mpls_tunnel),
            p_gb_nh_master->p_mutex);
        if (!p_mpls_tunnel)
        {
            SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
            return CTC_E_ENTRY_NOT_EXIST;
        }


        if (!stats_info->is_protection_path)
        {
            stats_ptr = p_mpls_tunnel->stats_ptr;
        }
        else
        {
            stats_ptr = p_mpls_tunnel->p_stats_ptr;
        }

        if (stats_ptr != SYS_GREATBELT_INVALID_STATS_PTR)
        {
            CTC_ERROR_RETURN_WITH_UNLOCK(
            sys_greatbelt_stats_get_flow_stats(lchip, stats_ptr, 0, &stats),
                    p_gb_nh_master->p_mutex);

            stats_info->stats.byte_count += stats.byte_count;
            stats_info->stats.packet_count += stats.packet_count;
        }
        else
        {
            SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
            return CTC_E_NH_STATS_NOT_EXIST;
        }

    }
    else
    {
        SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
        return CTC_E_INTR_INVALID_PARAM;
    }
    SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_reset_stats_result(uint8 lchip, ctc_nh_stats_info_t* stats_info)
{
    sys_nh_info_com_t* p_nh_info = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    sys_nh_info_mpls_t* p_mpls_nh_info = NULL;
    sys_nh_info_mcast_t* p_mcast_nh_info = NULL;
    uint16 stats_ptr = 0;
    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    SYS_NH_LOCK(p_gb_nh_master->p_mutex);
    if (CTC_FLAG_ISSET(stats_info->stats_type, CTC_NH_STATS_TYPE_NHID))
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(
            _sys_greatbelt_nh_api_get_nhinfo_by_nhid(lchip, stats_info->id.nhid, p_gb_nh_master, &p_nh_info),
            p_gb_nh_master->p_mutex);

        if (!p_nh_info)
        {
            SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
            return CTC_E_NH_NOT_EXIST;
        }

        /* only support mpls nexthop */
        if (SYS_NH_TYPE_MPLS == p_nh_info->hdr.nh_entry_type)
        {
            p_mpls_nh_info = (sys_nh_info_mpls_t*)p_nh_info;

            if (!stats_info->is_protection_path)
            {
                stats_ptr = p_mpls_nh_info->working_path.stats_ptr;
            }
            else
            {
                stats_ptr = p_mpls_nh_info->protection_path->stats_ptr;
            }

            if (SYS_GREATBELT_INVALID_STATS_PTR != stats_ptr)
            {
                CTC_ERROR_RETURN_WITH_UNLOCK(
                sys_greatbelt_stats_clear_flow_stats(lchip, stats_ptr, 0),
                        p_gb_nh_master->p_mutex);
            }
            else
            {
                SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
                return CTC_E_NH_STATS_NOT_EXIST;
            }

        }
        else if (SYS_NH_TYPE_MCAST == p_nh_info->hdr.nh_entry_type)
        {
            p_mcast_nh_info = (sys_nh_info_mcast_t*)p_nh_info;

            stats_ptr = p_mcast_nh_info->hdr.dsfwd_info.stats_ptr;

            if (stats_ptr != SYS_GREATBELT_INVALID_STATS_PTR)
            {
                CTC_ERROR_RETURN_WITH_UNLOCK(
                sys_greatbelt_stats_clear_flow_stats(lchip, stats_ptr, 0),
                        p_gb_nh_master->p_mutex);
            }
            else
            {
                SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
                return CTC_E_NH_STATS_NOT_EXIST;
            }

        }
        else
        {
            SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
            return CTC_E_FEATURE_NOT_SUPPORT;
        }
    }
    else if (CTC_FLAG_ISSET(stats_info->stats_type, CTC_NH_STATS_TYPE_TUNNELID))
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(sys_greatbelt_nh_lkup_mpls_tunnel(lchip, stats_info->id.tunnel_id, &p_mpls_tunnel),
            p_gb_nh_master->p_mutex);
        if (!p_mpls_tunnel)
        {
            SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
            return CTC_E_ENTRY_NOT_EXIST;
        }

        if (!stats_info->is_protection_path)
        {
            stats_ptr = p_mpls_tunnel->stats_ptr;
        }
        else
        {
            stats_ptr = p_mpls_tunnel->p_stats_ptr;
        }

        if (stats_ptr != SYS_GREATBELT_INVALID_STATS_PTR)
        {
            CTC_ERROR_RETURN_WITH_UNLOCK(
            sys_greatbelt_stats_clear_flow_stats(lchip, stats_ptr, 0),
                    p_gb_nh_master->p_mutex);
        }
        else
        {
            SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
            return CTC_E_NH_STATS_NOT_EXIST;
        }

    }
    else
    {
        SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);
        return CTC_E_INTR_INVALID_PARAM;
    }

    SYS_NH_UNLOCK(p_gb_nh_master->p_mutex);

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_get_max_external_nhid(uint8 lchip, uint32* nhid)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);


    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    /*Nexthop module initialize check*/
    if (NULL == p_gb_nh_master)
    {
        return CTC_E_NOT_INIT;
    }

    *nhid = p_gb_nh_master->max_external_nhid;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_global_cfg_init(uint8 lchip)
{
    int32           cmd = 0;
    uint32          nh_offset = 0;
    met_fifo_ctl_t  met_fifo_ctl;
    ipe_ds_fwd_ctl_t ipe_ds_fwd_ctl;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    uint32 dsnh_offset[2] = {0};
    ds_next_hop4_w_t dsnh;

    /*Config global  register*/

    sal_memset(&met_fifo_ctl, 0, sizeof(met_fifo_ctl_t));
    sal_memset(&ipe_ds_fwd_ctl, 0, sizeof(ipe_ds_fwd_ctl_t));


    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, 1, &dsnh_offset[0]));
    if (!(dsnh_offset[0] & 0x1))
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, 1, &dsnh_offset[1]));
        sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, 1, dsnh_offset[0]);
        dsnh_offset[0] = dsnh_offset[1];
    }

    sal_memset(&dsnh, 0, sizeof(ds_next_hop4_w_t));
    SYS_GREATBELT_NH_DSNH_SET_DEFAULT_COS_ACTION(dsnh);
    dsnh.is_nexthop   = TRUE;
    dsnh.svlan_tagged = TRUE;
    dsnh.payload_operation = SYS_NH_OP_BRIDGE;
    dsnh.dest_vlan_ptr = SYS_DSNH_DESTVLANPTR_SPECIAL;
    CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, dsnh_offset[0], &dsnh));

    cmd = DRV_IOR(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &met_fifo_ctl));
    met_fifo_ctl.port_bitmap_bit13_12 = 0;   /*queueSelType*/

    sys_greatbelt_nh_get_resolved_offset(lchip,
                                         SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH,
                                         &nh_offset);
    met_fifo_ctl.port_bitmap_next_hop_ptr = dsnh_offset[0];

    cmd = DRV_IOW(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &met_fifo_ctl));

    cmd = DRV_IOR(IpeDsFwdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ds_fwd_ctl));

    /*fatal exception*/
    sys_greatbelt_nh_offset_alloc_with_multiple(lchip, SYS_NH_ENTRY_TYPE_FWD, 16, 16, &nh_offset);

    ipe_ds_fwd_ctl.ds_fwd_index_base_fatal = nh_offset >> 4;

    cmd = DRV_IOW(IpeDsFwdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ds_fwd_ctl));

    p_gb_nh_master->fatal_excp_base = nh_offset;


    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_set_pkt_nh_edit_mode(uint8 lchip, uint8 edit_mode)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    p_gb_nh_master->pkt_nh_edit_mode  = edit_mode ;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_nh_set_ipmc_logic_replication(uint8 lchip, uint8 enable)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    p_gb_nh_master->ipmc_logic_replication  = (enable) ? 1 : 0;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_xgpon_reserve_vlan_edit(uint8 lchip)
{
    uint16 i = 0;
    uint32 offset_base = 0;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    sys_nh_param_dsnh_t dsnh_param;
    ctc_vlan_egress_edit_info_t vlan_edit_info;

    LCHIP_CHECK(lchip);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, 4096, &offset_base));
    p_gb_nh_master->xlate_nh_offset = offset_base;

    sal_memset(&dsnh_param, 0, sizeof(dsnh_param));
    dsnh_param.dest_vlan_ptr = SYS_DSNH_DESTVLANPTR_SPECIAL;
    dsnh_param.dsnh_type = SYS_NH_PARAM_DSNH_TYPE_BRGUC;

    sal_memset(&vlan_edit_info, 0, sizeof(vlan_edit_info));
    vlan_edit_info.svlan_edit_type = CTC_VLAN_EGRESS_EDIT_INSERT_VLAN;
    vlan_edit_info.cvlan_edit_type = CTC_VLAN_EGRESS_EDIT_KEEP_VLAN_UNCHANGE;
    CTC_SET_FLAG(vlan_edit_info.edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID);
    CTC_SET_FLAG(vlan_edit_info.edit_flag, CTC_VLAN_EGRESS_EDIT_REPLACE_SVLAN_COS);
    vlan_edit_info.stag_cos = 0;

    dsnh_param.p_vlan_info = &vlan_edit_info;

    for (i=0; i<4096; i++)
    {
        vlan_edit_info.output_svid = i;
        dsnh_param.dsnh_offset = offset_base+i;
        CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsnh4w(lchip, &dsnh_param));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_set_xgpon_en(uint8 lchip, uint8 enable, uint32 flag)
{
    uint32 field_val = 0;
    uint32 cmd = 0;
    uint32 lport = 0;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));


    /*rsv group 0 for common group*/
    if (enable)
    {
        CTC_BIT_UNSET(p_gb_nh_master->p_occupid_met_offset_bmp[0], 0);
        field_val = 1;
        CTC_ERROR_RETURN(_sys_greatbelt_nh_xgpon_reserve_vlan_edit(lchip));
        /*merge dsfwd mode*/
        sys_greatbelt_nh_set_dsfwd_mode(lchip, 0);

        for (lport = 56; lport < SYS_GB_MAX_PHY_PORT; lport++)
        {
            CTC_ERROR_RETURN(sys_greatbelt_port_queue_remove(lchip, SYS_QUEUE_TYPE_NORMAL, lport));
        }
    }
    else
    {
        CTC_BIT_SET(p_gb_nh_master->p_occupid_met_offset_bmp[0], 0);
        field_val = 0;
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, 4096, p_gb_nh_master->xlate_nh_offset));
        for (lport = 56; lport < SYS_GB_MAX_PHY_PORT; lport++)
        {
            CTC_ERROR_RETURN(sys_greatbelt_port_queue_add(lchip, SYS_QUEUE_TYPE_NORMAL, lport, 0));
        }
    }

    cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_LoopbackUseSourcePort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_set_mcast_bitmap_ptr(uint8 lchip, uint32 offset)
{
    uint32 field_val = 0;
    uint32 cmd = 0;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    field_val = p_gb_nh_master->xlate_nh_offset + offset;
    cmd = DRV_IOW(MetFifoCtl_t, MetFifoCtl_PortBitmapNextHopPtr_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_set_ecmp_mode(uint8 lchip, uint8 enable)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    p_gb_nh_master->ecmp_mode = (enable) ? 1 : 0;

    return CTC_E_NONE;
}
int32
sys_greatbelt_nh_set_ip_use_l2edit(uint8 lchip, uint8 enable)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    p_gb_nh_master->ip_use_l2edit = (enable) ? 1 : 0;

    return CTC_E_NONE;
}
int32
sys_greatbelt_nh_get_ip_use_l2edit(uint8 lchip, uint8* enable)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    *enable = p_gb_nh_master->ip_use_l2edit;

    return CTC_E_NONE;
}
int32
sys_greatbelt_nh_set_reflective_brg_en(uint8 lchip, uint8 enable)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    p_gb_nh_master->reflective_brg_en = enable ?1:0;
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_get_reflective_brg_en(uint8 lchip, uint8 *enable)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    *enable = p_gb_nh_master->reflective_brg_en;
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_add_eloop_edit(uint8 lchip, uint32 nhid, bool is_l2edit, uint32* p_edit_ptr)
{

    ctc_slistnode_t* ctc_slistnode = NULL;
    sys_nh_eloop_node_t *p_eloop_node = NULL;
    uint32 edit_ptr = 0;
    ds_l3_edit_nat8_w_t ds_l3_edit;
    ds_l2_edit_loopback_t ds_l2_edit;
    uint32 dest_map = 0;
    uint16 nexthiop_ptr = 0;
    uint8  nexthop_ext = 0;
    uint32 cmd = 0;
    sys_nh_info_com_t*p_nhinfo = NULL;
    uint8 type = 0;
    int32 ret = CTC_E_NONE;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    sys_nh_info_dsnh_t nhinfo;
    uint8 dest_gchip = 0;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));

    sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid, &p_nhinfo);
    if (NULL == p_nhinfo)
    {
        return CTC_E_NH_INVALID_NHID;
    }

    CTC_SLIST_LOOP(p_gb_nh_master->eloop_list, ctc_slistnode)
    {
        p_eloop_node = _ctc_container_of(ctc_slistnode, sys_nh_eloop_node_t, head);
        if (p_eloop_node->nhid == nhid)
        {
            *p_edit_ptr = p_eloop_node->edit_ptr;
            p_eloop_node->ref_cnt++;
            return CTC_E_NONE;
        }
    }

    p_eloop_node =  mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_eloop_node_t));
    if (NULL == p_eloop_node)
    {
       return CTC_E_NO_MEMORY;
    }
    sal_memset(p_eloop_node, 0, sizeof(sys_nh_eloop_node_t));
    p_eloop_node->nhid = nhid;


    type = is_l2edit?SYS_NH_ENTRY_TYPE_L2EDIT_LPBK:
    SYS_NH_ENTRY_TYPE_L3EDIT_NAT_8W;
    p_eloop_node->is_l2edit = is_l2edit?1:0;

    ret = sys_greatbelt_nh_offset_alloc(lchip, type, 1, &edit_ptr);
    if (ret < 0)
    {
        mem_free(p_eloop_node);
        return ret;
    }

    *p_edit_ptr = edit_ptr;
    p_eloop_node->edit_ptr = edit_ptr;

    sal_memset(&nhinfo, 0, sizeof(sys_nh_info_dsnh_t));
    ret = sys_greatbelt_nh_get_nhinfo(lchip, nhid, &nhinfo);
    if (ret < 0)
    {
        mem_free(p_eloop_node);
        return ret;
    }
    nexthiop_ptr = nhinfo.dsnh_offset;
    nexthop_ext = nhinfo.nexthop_ext;
    dest_map = SYS_NH_ENCODE_DESTMAP(nhinfo.is_mcast, nhinfo.dest_chipid, nhinfo.dest_id);

    if (is_l2edit)
    {
        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
        {
            sys_greatbelt_get_gchip_id(lchip, &dest_gchip);
            dest_map = SYS_NH_ENCODE_DESTMAP(0, dest_gchip, SYS_RESERVE_PORT_ID_DROP);
        }
        sal_memset(&ds_l2_edit, 0, sizeof(ds_l2_edit_loopback_t));
        ds_l2_edit.l2_rewrite_type = 1; /*4w loopback*/
        ds_l2_edit.ds_type = 1; /*l2edit*/
        ds_l2_edit.lb_dest_map = dest_map;
        ds_l2_edit.lb_next_hop_ptr = nexthiop_ptr;
        ds_l2_edit.lb_next_hop_ext = nexthop_ext;
        cmd = DRV_IOW(DsL2EditLoopback_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, edit_ptr, cmd, &ds_l2_edit);
    }
    else
    {
        sal_memset(&ds_l3_edit, 0, sizeof(ds_l3_edit_nat8_w_t));
        ds_l3_edit.replace_l4_dest_port = TRUE; /*loopback en*/
        ds_l3_edit.l3_rewrite_type = 7; /*8w Nat*/
        ds_l3_edit.ds_type = 2;  /*l3edit*/
        ds_l3_edit.ip_da39_32 = (dest_map >> 16)&0x1F;
        ds_l3_edit.l4_dest_port = dest_map&0xFFFF;
        ds_l3_edit.ip_da29_0 = ((nexthiop_ptr&0x1FFFF) | (nexthop_ext << 20));

        cmd = DRV_IOW(DsL3EditNat8W_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, edit_ptr, cmd, &ds_l3_edit);
    }


    p_eloop_node->ref_cnt++;
    ctc_slist_add_tail(p_gb_nh_master->eloop_list, &(p_eloop_node->head));

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_remove_eloop_edit(uint8 lchip, uint32 nhid)
{
    ctc_slistnode_t* ctc_slistnode = NULL;
    ctc_slistnode_t* ctc_slistnode_new = NULL;
    sys_nh_eloop_node_t *p_eloop_node = NULL;
    uint32 edit_ptr = 0;
    ds_l3_edit_nat8_w_t ds_l3_edit;
    ds_l2_edit_loopback_t ds_l2_edit;
    uint32 cmd = 0;
    uint8 type = 0;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));


    CTC_SLIST_LOOP_DEL(p_gb_nh_master->eloop_list, ctc_slistnode, ctc_slistnode_new)
    {
        p_eloop_node = _ctc_container_of(ctc_slistnode, sys_nh_eloop_node_t, head);
        if (p_eloop_node->nhid == nhid)
        {
            p_eloop_node->ref_cnt--;
            if (0 == p_eloop_node->ref_cnt)
            {
                edit_ptr = p_eloop_node->edit_ptr;
                type = p_eloop_node->is_l2edit?SYS_NH_ENTRY_TYPE_L2EDIT_LPBK:
                SYS_NH_ENTRY_TYPE_L3EDIT_NAT_8W;
                CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, type, 1, edit_ptr));
                if (p_eloop_node->is_l2edit)
                {
                    sal_memset(&ds_l2_edit, 0, sizeof(ds_l2_edit_loopback_t));
                    cmd = DRV_IOW(DsL2EditLoopback_t, DRV_ENTRY_FLAG);
                    DRV_IOCTL(lchip, edit_ptr, cmd, &ds_l2_edit);
                }
                else

                {
                    sal_memset(&ds_l3_edit, 0, sizeof(ds_l3_edit_nat8_w_t));
                    cmd = DRV_IOW(DsL3EditNat8W_t, DRV_ENTRY_FLAG);
                    DRV_IOCTL(lchip, edit_ptr, cmd, &ds_l3_edit);
                }

                ctc_slist_delete_node(p_gb_nh_master->eloop_list, &(p_eloop_node->head));

                mem_free(p_eloop_node);
            }
            return CTC_E_NONE;
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_update_eloop_edit(uint8 lchip, uint32 nhid)
{
    sys_nh_info_com_t*p_nhinfo = NULL;
    ds_l2_edit_loopback_t ds_l2_edit;
    uint32 dest_map = 0;
    uint16 nexthiop_ptr = 0;
    uint8  nexthop_ext = 0;
    uint32 cmd = 0;
    sys_nh_info_dsnh_t nhinfo;
    ctc_slistnode_t* ctc_slistnode = NULL;
    sys_nh_eloop_node_t *p_eloop_node = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    uint8 dest_gchip = 0;

    CTC_ERROR_RETURN(_sys_greatbelt_nh_get_nh_master(lchip, &p_gb_nh_master));
    CTC_SLIST_LOOP(p_gb_nh_master->eloop_list, ctc_slistnode)
    {
        p_eloop_node = _ctc_container_of(ctc_slistnode, sys_nh_eloop_node_t, head);
        if (p_eloop_node->nhid == nhid)
        {
            sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid, &p_nhinfo);
            if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
            {
                sys_greatbelt_get_gchip_id(lchip, &dest_gchip);
                dest_map = SYS_NH_ENCODE_DESTMAP(0, dest_gchip, SYS_RESERVE_PORT_ID_DROP);
            }
            else
            {
                sal_memset(&nhinfo, 0, sizeof(sys_nh_info_dsnh_t));
                CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, nhid, &nhinfo));
                nexthiop_ptr = nhinfo.dsnh_offset;
                nexthop_ext = nhinfo.nexthop_ext;
                dest_map = SYS_NH_ENCODE_DESTMAP(nhinfo.is_mcast, nhinfo.dest_chipid, nhinfo.dest_id);
            }
            sal_memset(&ds_l2_edit, 0, sizeof(ds_l2_edit_loopback_t));
            ds_l2_edit.l2_rewrite_type = 1; /*4w loopback*/
            ds_l2_edit.ds_type = 1; /*l2edit*/
            ds_l2_edit.lb_dest_map = dest_map;
            ds_l2_edit.lb_next_hop_ptr = nexthiop_ptr;
            ds_l2_edit.lb_next_hop_ext = nexthop_ext;
            cmd = DRV_IOW(DsL2EditLoopback_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, p_eloop_node->edit_ptr, cmd, &ds_l2_edit);
            return CTC_E_NONE;
        }
    }

    return CTC_E_NONE;
}
/**
 @brief This function is used to initilize nexthop module

 @param[in] dyn_tbl_timeout, microsecond, used for delay free

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_init(uint8 lchip, ctc_nh_global_cfg_t* nh_cfg)
{
    sys_greatbelt_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    /*Nexthop module have initialize*/
    if (NULL !=  p_gb_nh_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (nh_cfg->max_external_nhid == 0)
    {
        nh_cfg->max_external_nhid = SYS_NH_EXTERNAL_NHID_DEFAUL_NUM;
    }

    if (nh_cfg->max_tunnel_id == 0)
    {
        nh_cfg->max_tunnel_id = SYS_NH_TUNNEL_ID_DEFAUL_NUM;
    }
    /*1. Create master*/
    CTC_ERROR_RETURN(_sys_greatbelt_nh_master_new(lchip, &p_master, nh_cfg->max_external_nhid, nh_cfg->max_tunnel_id, SYS_NH_ARP_ID_DEFAUL_NUM));

    p_gb_nh_master[lchip] = p_master;
    p_gb_nh_master[lchip]->max_external_nhid = nh_cfg->max_external_nhid;
    p_gb_nh_master[lchip]->max_tunnel_id = nh_cfg->max_tunnel_id;
    p_gb_nh_master[lchip]->max_arp_id    = SYS_NH_ARP_ID_DEFAUL_NUM;
    p_gb_nh_master[lchip]->acl_redirect_fwd_ptr_num = nh_cfg->acl_redirect_fwd_ptr_num;

    p_gb_nh_master[lchip]->pkt_nh_edit_mode  = nh_cfg->nh_edit_mode ;

    if (nh_cfg->nh_edit_mode == CTC_NH_EDIT_MODE_SINGLE_CHIP)
    {
        p_gb_nh_master[lchip]->pkt_nh_edit_mode = SYS_NH_IGS_CHIP_EDIT_MODE;
    }
    else if(nh_cfg->nh_edit_mode == 2)
    {
        p_gb_nh_master[lchip]->pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE ;
    }
    else
    {
        p_gb_nh_master[lchip]->pkt_nh_edit_mode  = SYS_NH_MISC_CHIP_EDIT_MODE ;
    }


    p_gb_nh_master[lchip]->max_ecmp  = nh_cfg->max_ecmp;
    p_gb_nh_master[lchip]->ecmp_mode = (nh_cfg->max_ecmp == 0) ? 1 : 0;
    p_gb_nh_master[lchip]->reflective_brg_en = 0;
    p_gb_nh_master[lchip]->ipmc_logic_replication = 0;
    p_gb_nh_master[lchip]->ipmc_bitmap_met_num = 0;
    p_gb_nh_master[lchip]->ip_use_l2edit = 0;
    p_gb_nh_master[lchip]->no_dsfwd      = 0;

    /*2. Install Semantic callback*/
    if (CTC_E_NONE != _sys_greatbelt_nh_callback_init(lchip, p_master))
    {
        mem_free(p_gb_nh_master[lchip]);
        p_gb_nh_master[lchip] = NULL;
        return CTC_E_NOT_INIT;
    }

    /*3. Offset  init*/
    if (CTC_E_NONE != _sys_greatbelt_nh_offset_init(lchip, p_master))
    {
        mem_free(p_gb_nh_master[lchip]);
        p_gb_nh_master[lchip] = NULL;
        return CTC_E_NOT_INIT;
    }

    /*4. global cfg init*/
    _sys_greatbelt_nh_global_cfg_init(lchip);

    /* set chip_capability */
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip,  CTC_GLOBAL_CAPABILITY_ECMP_MEMBER_NUM, p_gb_nh_master[lchip]->max_ecmp));
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_EXTERNAL_NEXTHOP_NUM, p_gb_nh_master[lchip]->max_external_nhid));
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_GLOBAL_DSNH_NUM, p_gb_nh_master[lchip]->max_glb_nh_offset));
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_MPLS_TUNNEL_NUM, p_gb_nh_master[lchip]->max_tunnel_id));
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_ARP_ID_NUM, SYS_NH_ARP_ID_DEFAUL_NUM));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_free_node_data(void* node_data, void* user_data)
{
    uint8 type = 0;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_nh_info_mcast_t* p_mcast = NULL;
    sys_nh_info_ipuc_t* p_ipuc = NULL;
    sys_nh_info_ecmp_t* p_ecmp = NULL;
    sys_nh_info_mpls_t* p_mpls = NULL;
    ctc_list_pointer_node_t* p_pos=NULL, * p_pos_next=NULL;
    sys_nh_mcast_meminfo_t* p_member = NULL;

    if (user_data)
    {
        type = *(uint8*)user_data;
        if (0 == type)
        {
            p_nhinfo=(sys_nh_info_com_t*)node_data;
            switch(p_nhinfo->hdr.nh_entry_type)
            {
                case SYS_NH_TYPE_MCAST:
                    p_mcast = (sys_nh_info_mcast_t*)(p_nhinfo);
                    CTC_LIST_POINTER_LOOP_DEL(p_pos, p_pos_next, &p_mcast->p_mem_list)
                    {
                        p_member = _ctc_container_of(p_pos, sys_nh_mcast_meminfo_t, list_head);
                        if (p_member->dsmet.vid_list)
                        {
                            mem_free(p_member->dsmet.vid_list);
                        }
                        mem_free(p_member);
                    }
                    break;
                case SYS_NH_TYPE_IPUC:
                    p_ipuc = (sys_nh_info_ipuc_t*)(p_nhinfo);
                    if (p_ipuc->protection_path)
                    {
                        mem_free(p_ipuc->protection_path);
                    }
                    break;
                case SYS_NH_TYPE_ECMP:
                    if (p_nhinfo->hdr.p_ref_nh_list)
                    {
                        mem_free(p_nhinfo->hdr.p_ref_nh_list);
                    }
                    p_ecmp = (sys_nh_info_ecmp_t*)(p_nhinfo);
                    mem_free(p_ecmp->nh_array);
                    break;
                case SYS_NH_TYPE_MPLS:
                    p_mpls = (sys_nh_info_mpls_t*)(p_nhinfo);
                    if (p_mpls->protection_path)
                    {
                        mem_free(p_mpls->protection_path);
                    }
                    break;
                default:
                    break;
            }
        }
    }
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_deinit(uint8 lchip)
{
    uint8  loop = 0;
    uint8 type = 0;
    ctc_slistnode_t* node = NULL, *next_node = NULL;
    ctc_slistnode_t* ctc_slistnode = NULL;
    sys_nh_eloop_node_t *p_eloop_node = NULL;

    LCHIP_CHECK(lchip);
    if (NULL == p_gb_nh_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_greatbelt_nh_ip_tunnel_deinit(lchip);
    /*free eloop slist*/
    CTC_SLIST_LOOP_DEL(p_gb_nh_master[lchip]->eloop_list, node, next_node)
    {
        p_eloop_node = _ctc_container_of(ctc_slistnode, sys_nh_eloop_node_t, head);
        ctc_slist_delete_node(p_gb_nh_master[lchip]->eloop_list, &(p_eloop_node->head));
        mem_free(p_eloop_node);
    }
    ctc_slist_delete(p_gb_nh_master[lchip]->eloop_list);

    for (loop = 0; loop <= 3; loop++)
    {
        sys_greatbelt_opf_deinit(lchip, OPF_NH_DYN1_SRAM + loop);
    }

    if (p_gb_nh_master[lchip]->ipmc_bitmap_met_num)
    {
        sys_greatbelt_opf_deinit(lchip, OPF_NH_DSMET_FOR_IPMCBITMAP);
    }

    if (p_gb_nh_master[lchip]->max_glb_nh_offset)
    {
        mem_free(p_gb_nh_master[lchip]->p_occupid_nh_offset_bmp);
    }
    if (p_gb_nh_master[lchip]->max_glb_met_offset)
    {
        mem_free(p_gb_nh_master[lchip]->p_occupid_met_offset_bmp);
    }

    /*free dsl2edit4w tree*/
    ctc_avl_traverse(p_gb_nh_master[lchip]->dsl2edit4w_tree, (avl_traversal_fn)_sys_greatbelt_nh_free_node_data, NULL);
    ctc_avl_tree_free(&(p_gb_nh_master[lchip]->dsl2edit4w_tree), NULL);

    /*free dsl2edit8w tree*/
    ctc_avl_traverse(p_gb_nh_master[lchip]->dsl2edit8w_tree, (avl_traversal_fn)_sys_greatbelt_nh_free_node_data, NULL);
    ctc_avl_tree_free(&(p_gb_nh_master[lchip]->dsl2edit8w_tree), NULL);

    /*free internal nhid vector*/
    ctc_vector_traverse(p_gb_nh_master[lchip]->internal_nhid_vec, (vector_traversal_fn)_sys_greatbelt_nh_free_node_data, &type);
    ctc_vector_release(p_gb_nh_master[lchip]->internal_nhid_vec);

    /*free external nhid vector*/
    ctc_vector_traverse(p_gb_nh_master[lchip]->external_nhid_vec, (vector_traversal_fn)_sys_greatbelt_nh_free_node_data, &type);
    ctc_vector_release(p_gb_nh_master[lchip]->external_nhid_vec);

    sys_greatbelt_opf_deinit(lchip, OPF_NH_NHID_INTERNAL);

    /*free tunnel id vector*/
    ctc_vector_traverse(p_gb_nh_master[lchip]->tunnel_id_vec, (vector_traversal_fn)_sys_greatbelt_nh_free_node_data, NULL);
    ctc_vector_release(p_gb_nh_master[lchip]->tunnel_id_vec);

    /*free arp id vector*/
    ctc_vector_traverse(p_gb_nh_master[lchip]->arp_id_vec, (vector_traversal_fn)_sys_greatbelt_nh_free_node_data, NULL);
    ctc_vector_release(p_gb_nh_master[lchip]->arp_id_vec);

    /*free mcast group nhid vector*/
    /*ctc_vector_traverse(p_gb_nh_master[lchip]->mcast_nhid_vec, (vector_traversal_fn)_sys_greatbelt_nh_free_node_data, NULL);*/
    ctc_vector_release(p_gb_nh_master[lchip]->mcast_nhid_vec);

    sal_mutex_destroy(p_gb_nh_master[lchip]->p_mutex);
    mem_free(p_gb_nh_master[lchip]);

    return CTC_E_NONE;
}

