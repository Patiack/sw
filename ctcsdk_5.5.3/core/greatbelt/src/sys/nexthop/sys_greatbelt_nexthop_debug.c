/**
 @file sys_greatbelt_nexthop_debug.c

 @date 2009-12-28

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
#include "ctc_linklist.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_common.h"

#include "sys_greatbelt_register.h"

#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_aps.h"
#include "sys_greatbelt_l3if.h"
#include "sys_greatbelt_internal_port.h"

#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_tbl_reg.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

#define SYS_NH_DEBUG_DUMP_HW_TABLE(tbl_str, FMT, ...)              \
    do                                                       \
    {                                                        \
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    "tbl_str "::");                                  \
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__);                      \
    } while (0)

#define SYS_NH_DEBUG_TYPE_HEAD(type) " ~~~~~ DUMP "type " Nexthop ~~~~~~\n"

#define SYS_NH_IP_TUNNEL_IPV6_PROTO_TYPE 41
#define SYS_NH_IP_TUNNEL_IPV4_PROTO_TYPE 4

typedef int32 (* DUMP_L2EDIT)  (uint8 lchip, uint32 dsl2edit_offset, bool detail);
/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/

extern int32
sys_greatbelt_nh_debug_get_nh_master(uint8 lchip, sys_greatbelt_nh_master_t** p_gb_nh_master);
extern int32
sys_greatbelt_nh_debug_get_nhinfo_by_nhid(uint8 lchip, uint32 nhid, sys_greatbelt_nh_master_t* p_gb_nh_master, sys_nh_info_com_t** pp_nhinfo);

int32
sys_greatbelt_nh_display_current_global_sram_info(uint8 lchip)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    uint32* p_bmp;
    uint8  line_num = 16;
    uint8  line_loop = 0;
    int32 index, pos;
    bool have_used = FALSE;

    CTC_ERROR_RETURN(sys_greatbelt_nh_debug_get_nh_master(lchip, &p_gb_nh_master));
    if (NULL == p_gb_nh_master)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Nexthop Module have not been inited \n");
    }
    else
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Systme max global nexthop offset is %d,global met offset is %d\n",
                       p_gb_nh_master->max_glb_nh_offset, p_gb_nh_master->max_glb_met_offset);
        p_bmp = p_gb_nh_master->p_occupid_nh_offset_bmp;
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Occupied global nexthop Offsets are: \n");
        line_loop = 0;

        for (index = 0; index < (p_gb_nh_master->max_glb_nh_offset >> BITS_SHIFT_OF_WORD); index++)
        {
            for (pos = 0; pos < BITS_NUM_OF_WORD; pos++)
            {
                if (CTC_FLAG_ISSET(p_bmp[index], 1 << pos))
                {
                    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%d, ", (index << BITS_SHIFT_OF_WORD) | (pos));
                    have_used = TRUE;
                    line_loop++;
                }

                if (line_loop == line_num)
                {
                    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
                    line_loop = 0;
                }
            }
        }

        if (!have_used)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " No offset in use");
        }

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");

        p_bmp = p_gb_nh_master->p_occupid_met_offset_bmp;
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Occupied global met Offsets are: \n ");
        line_loop = 0;

        for (index = 0; index < (p_gb_nh_master->max_glb_met_offset >> BITS_SHIFT_OF_WORD); index++)
        {
            for (pos = 0; pos < BITS_NUM_OF_WORD; pos++)
            {
                if (CTC_FLAG_ISSET(p_bmp[index], 1 << pos))
                {
                    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%d, ", (index << BITS_SHIFT_OF_WORD) | (pos));
                    have_used = TRUE;
                    line_loop++;
                }

                if (line_loop == line_num)
                {
                    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
                    line_loop = 0;
                }
            }
        }
    }

    if (!have_used)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " No offset in use");
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_dsfwd(uint8 lchip, uint32 dsfwd_offset, bool detail)
{

    ds_fwd_t dsfwd;
    uint32 cmd;

    cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, dsfwd_offset, cmd, &dsfwd));

    SYS_NH_DEBUG_DUMP_HW_TABLE("DsFwd",
                               "------------------DsFwd(0x%x)--------------\n", dsfwd_offset);

    SYS_NH_DEBUG_DUMP_HW_TABLE("DsFwd", "DestMap = 0x%x, nextHopExt:%d,NextHopPtr = %u\n", dsfwd.dest_map, dsfwd.next_hop_ext,
                               dsfwd.next_hop_ptr);
    if (detail)
    {
        SYS_NH_DEBUG_DUMP_HW_TABLE("DsFwd", "isMet = %d,isNexthop = %d\n", dsfwd.is_met, dsfwd.is_nexthop);

        SYS_NH_DEBUG_DUMP_HW_TABLE("DsFwd", "CriticalPkt = %d,SendLocalPhyPort = %d, LengthAdjType = %d\n", dsfwd.critical_packet,
                                   dsfwd.send_local_phy_port, dsfwd.length_adjust_type);
        SYS_NH_DEBUG_DUMP_HW_TABLE("DsFwd", "ApsType = %d, force_back_en = %d, speed = %d, StatsPtr = 0x%x, \n",
                                   dsfwd.aps_type, dsfwd.force_back_en, dsfwd.speed, dsfwd.stats_ptr_high << 12 | dsfwd.stats_ptr_low);
        SYS_NH_DEBUG_DUMP_HW_TABLE("DsFwd", "MacIsolatedGroupEn = %d, MacIsolatedGroupId = %d\n",
                                   dsfwd.mac_isolated_group_en, ((dsfwd.mac_isolated_group_id5_1 << 1) | dsfwd.mac_isolated_group_id0));

    }

    return CTC_E_NONE;
}

#define SYS_NH_GET_MAC_FROM_DSNH(dsnh, mac_high,mac_low) \
  do {\
mac_high = (dsnh.vlan_xlate_mode << 15)            /* macDa[47] */\
         | (((dsnh.stats_ptr >> 8) & 0x3F) << 9) /* macDa[46:41] */\
         | (dsnh.stag_cfi << 8)                  /* macDa[40] */\
         | (dsnh.stag_cos << 5)                  /* macDa[39:37] */\
         | (dsnh.copy_ctag_cos << 4)             /* macDa[36] */\
         | (dsnh.l3_edit_ptr15_15 << 3)              /* macDa[35] */\
         | (dsnh.l2_edit_ptr15 << 2)              /* macDa[34] */\
         | (dsnh.output_cvlan_id_valid << 1)     /* macDa[33] */\
         | ((dsnh.l3_edit_ptr14_0 >> 14) & 0x1);    /* macDa[32] */\
mac_low = ((dsnh.l3_edit_ptr14_0 & 0x3FFF) << 18)    /* macDa[31:18] */\
         | (dsnh.l2_edit_ptr14_12 << 15)          /* macDa[17:15] */\
         | (dsnh.replace_ctag_cos << 14)          /* macDa[14] */\
         | (dsnh.derive_stag_cos << 13)           /* macDa[13] */\
         | (dsnh.tagged_mode << 12)               /* macDa[12] */\
         | dsnh.l2_edit_ptr11_0;                  /* macDa[11:0] */\
      }while(0)


int32
sys_greatbelt_nh_dump_dsnh(uint8 lchip, uint32 dsnh_offset, bool detail, uint32* dsl2edit_offset)
{

    uint32 cmd;
    ds_next_hop4_w_t   dsnh;
    ds_next_hop8_w_t   dsnh8w;
    uint32 mac_high = 0;
    uint32 mac_low = 0;




#define SYS_NH_DSNH_HD "DsNH4w"

    if (((dsnh_offset >> 2) & 0x3FFF) != SYS_GREATBELT_DSNH_INTERNAL_BASE)
    {
        cmd = DRV_IOR(DsNextHop4W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, dsnh_offset, cmd, &dsnh));

    }
    else
    {
        cmd = DRV_IOR(EpeNextHopInternal_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (dsnh_offset & 0x2) >> 1, cmd, &dsnh8w));
        sal_memcpy((uint8*)&dsnh, ((uint8*)&dsnh8w + sizeof(ds_next_hop4_w_t) * (dsnh_offset & 0x1)), sizeof(ds_next_hop4_w_t));
    }
    if (dsl2edit_offset != NULL)
    {
        *dsl2edit_offset = ((dsnh.l2_edit_ptr15 << 14) | (dsnh.l2_edit_ptr14_12 << 12) | dsnh.l2_edit_ptr11_0);
    }

    if (detail)
    {

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH_HD,
                                   "------------------DsNextHop4W(0x%x)--------------\n", dsnh_offset);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH_HD,
                                   "vlanXlateMode =%d , PayloadOP = %d, DestVlanPtr = %d, isMet = %d, isNexthop = %d\n",
                                   dsnh.vlan_xlate_mode, dsnh.payload_operation, dsnh.dest_vlan_ptr,dsnh.is_met, dsnh.is_nexthop);

        if (dsnh.payload_operation == SYS_NH_OP_ROUTE_COMPACT)
        {
            SYS_NH_GET_MAC_FROM_DSNH(dsnh, mac_high, mac_low);
            SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH_HD,"macDa47To32 = 0x%x ,macDa31To0 = 0x%x\n",mac_high,mac_low);
        }
        else
        {
            SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH_HD,
                                       "isLeaf:%d l3_edit_ptr = 0x%x\n",
                                        dsnh.is_leaf, dsnh.l3_edit_ptr15_15 << 15 | dsnh.l3_edit_ptr14_0);
            SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH_HD,
                                       "l2EditPtr = 0x%x  statsPtr:0x%x ,SvlanTagged = %d\n",
                                       ((dsnh.l2_edit_ptr15 << 14) | (dsnh.l2_edit_ptr14_12 << 12) | dsnh.l2_edit_ptr11_0), dsnh.stats_ptr,
                                       dsnh.svlan_tagged);
            SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH_HD,
                                       "TaggedMode = %d, MtuCheckEn = %d, StagCfi = %d, StagCos = %d\n",
                                       dsnh.tagged_mode, dsnh.mtu_check_en, dsnh.stag_cfi, dsnh.stag_cos);
            SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH_HD,
                                       "OutputSvlanValid = %d, OutputCvlanValid = %d, taggedMode = %d\n",
                                       dsnh.output_svlan_id_valid, dsnh.output_cvlan_id_valid, dsnh.tagged_mode);
            SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH_HD,
                                       "ReplaceCtagCos = %d, DeriveStagCos = %d, CopyCtagCos = %d\n",
                                       dsnh.replace_ctag_cos, dsnh.derive_stag_cos, dsnh.copy_ctag_cos);
        }

    }



    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_dsnh8w(uint8 lchip, uint32 dsnh_offset, bool detail, uint32* dsl2edit_offset)
{

    uint32 cmd;
    ds_next_hop8_w_t   dsnh;

#define SYS_NH_DSNH8W_HD "DsNH8W"
    cmd = DRV_IOR(DsNextHop8W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, dsnh_offset, cmd, &dsnh));

    if (dsl2edit_offset != NULL)
    {
        *dsl2edit_offset = ((dsnh.l2_edit_ptr15 << 14) | (dsnh.l2_edit_ptr14_12 << 12) | dsnh.l2_edit_ptr11_0);
    }

    if (detail)
    {
        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH8W_HD,
                                   "------------------DsNextHop8W(0x%x)--------------\n", dsnh_offset);

        SYS_NH_DEBUG_DUMP_HW_TABLE("DsNH8W", "vlanXlateMode = %d, PayloadOP = %d, DestVlanPtr = %d\n",
                                   dsnh.vlan_xlate_mode, dsnh.payload_operation, dsnh.dest_vlan_ptr);
        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH8W_HD,
                                   "isMet = %d, isNexthop = %d\n",
                                   dsnh.is_met, dsnh.is_nexthop);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH8W_HD,
                                   "L2EditPtr = 0x%x\n",
                                   ((dsnh.l2_edit_ptr15 << 14) | (dsnh.l2_edit_ptr14_12 << 12) | dsnh.l2_edit_ptr11_0));

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH8W_HD,
                                   "L3EditPtr = 0x%x stats_ptr:%d \n",
                                   ((dsnh.l3_edit_ptr17_16 << 15) | (dsnh.l3_edit_ptr15_15 << 14) | dsnh.l3_edit_ptr14_0), dsnh.stats_ptr);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH8W_HD,
                                   "TaggedMode = %d, MtuCheckEn = %d, StagCfi = %d, StagCos = %d\n",
                                   dsnh.tagged_mode, dsnh.mtu_check_en, dsnh.stag_cfi, dsnh.stag_cos);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH8W_HD,
                                   "OutputSvlanValid = %d, OutputCvlanValid = %d\n",
                                   dsnh.output_svlan_id_valid, dsnh.output_cvlan_id_valid);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH8W_HD,
                                   "OutputSvlanIdExt = %d, OutputCvlanIdExt = %d, OutputSvlanValidExt = %d\n",
                                   dsnh.output_svlan_id_ext, dsnh.output_cvlan_id_ext, dsnh.output_svlan_id_valid_ext);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH8W_HD,
                                   "OutputCvlanValidExt = %d, SvlanTpidEn = %d, SvlanTpid = %d\n",
                                   dsnh.output_cvlan_id_valid_ext, dsnh.svlan_tpid_en, dsnh.svlan_tpid);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH8W_HD,
                                   "SvlanTagged = %d, CvlanTagged = %d [No such field]\n",
                                   dsnh.svlan_tagged, 0);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH8W_HD,
                                   "ReplaceCtagCos = %d, DeriveStagCos = %d, CopyCtagCos = %d\n",
                                   dsnh.replace_ctag_cos, dsnh.derive_stag_cos, dsnh.copy_ctag_cos);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH8W_HD,
                                   "LogicDestPort =%d, LogicPortCheck = %d, TunnelMtuCheck = %d\n",
                                   dsnh.logic_dest_port, dsnh.logic_port_check, dsnh.tunnel_mtu_check_en);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH8W_HD,
                                   "tunnelMtuSize =%d, isLeaf = %d\n",
                                   dsnh.tunnel_mtu_size1_0, dsnh.is_leaf);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSNH8W_HD,
                                   "radionMacEn = %d,  radioMac = 0x%x\n",
                                   dsnh.radio_mac_en, dsnh.radio_mac);

    }

    return CTC_E_NONE;

}

int32
sys_greatbelt_nh_dump_dsl2edit(uint8 lchip, uint32 dsl2edit_offset, bool detail)
{

    uint32 cmd;
    ds_l2_edit_eth4_w_t  dsl2edit;

#define SYS_NH_DSL2EDIT_ETH4W_HD "DsL2Edit"

    cmd = DRV_IOR(DsL2EditEth4W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, dsl2edit_offset, cmd, &dsl2edit));

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH4W_HD,
                               "------------------DsL2EditEth4W(0x%x)--------------\n", dsl2edit_offset);

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH4W_HD,
                               "OutputVidValid = %d, OutPutVid = %d, OutPutVidIsSvlan = %d\n",
                               dsl2edit.output_vlan_id_valid, dsl2edit.output_vlan_id, dsl2edit.output_vlan_id_is_svlan);
    if (detail)
    {
        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH4W_HD,
                                   "isMet = %d, isNexthop = %d, dsType = %d\n",
                                   dsl2edit.is_met, dsl2edit.is_nexthop, dsl2edit.ds_type);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH4W_HD,
                                   "type = %d, OverwritEtherType = %d, DerivMcastMac = %d\n",
                                   dsl2edit.type, dsl2edit.overwrite_ether_type, dsl2edit.derive_mcast_mac);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH4W_HD,
                                   " deriveFcoeMacSa = %d, l2RewriteType = %d\n",
                                   dsl2edit.derive_fcoe_mac_sa, dsl2edit.l2_rewrite_type);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH4W_HD,
                                   "packet_type = %d, macDa47To32 = 0x%x ,macDa31To0 = 0x%x\n",
                                   dsl2edit.packet_type, dsl2edit.mac_da47_32, ((dsl2edit.mac_da31_30 << 30) | dsl2edit.mac_da29_0));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_dsl2edit_eth8w(uint8 lchip, uint32 dsl2edit_offset, bool detail)
{

    uint32 cmd;
    ds_l2_edit_eth8_w_t  dsl2edit;
    uint8 extra_header_type = 0;

#define SYS_NH_DSL2EDIT_ETH8W_HD "DsL2Edit8w"

    if (dsl2edit_offset == 0xFFFF)
    {
        return CTC_E_NONE;
    }

    cmd = DRV_IOR(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, dsl2edit_offset, cmd, &dsl2edit));

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH8W_HD,
                               "------------------DsL2EditEth8W(0x%x)--------------\n", dsl2edit_offset);

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH8W_HD,
                               "OutputVidValid = %d, OutPutVid = %d, OutPutVidIsSvlan = %d\n",
    dsl2edit.output_vlan_id_valid, dsl2edit.output_vlan_id, dsl2edit.output_vlan_id_is_svlan);

    extra_header_type = (dsl2edit.extra_header_type1_1 << 1) | dsl2edit.extra_header_type0_0;

    if (detail)
    {
        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH8W_HD,
                                   "isMet = %d, isNexthop = %d, dsType = %d extraHeaderType = %d\n",
                                   dsl2edit.is_met, dsl2edit.is_nexthop, dsl2edit.ds_type,extra_header_type);


        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH8W_HD,
                                   "type = %d, OverwritEtherType = %d, DerivMcastMac = %d\n",
                                   dsl2edit.type, dsl2edit.overwrite_ether_type, dsl2edit.derive_mcast_mac);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH8W_HD,
                                   " l2RewriteType = %d\n", dsl2edit.l2_rewrite_type);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH8W_HD,
                                   "packet_type = %d, macDa47To32 = 0x%x ,macDa31To0 = 0x%x\n",
                                   dsl2edit.packet_type, dsl2edit.mac_da47_32, (dsl2edit.mac_da31_30 << 30) | dsl2edit.mac_da29_0);

      if(extra_header_type == SYS_L2EDIT_EXTRA_HEADER_TYPE_MPLS)
      {
        ds_l3_edit_mpls8_w_t dsl3editMpls8w;
        uint32 ttl = 0;
        sal_memcpy((uint8*)&dsl3editMpls8w + sizeof(ds_l3_edit_mpls4_w_t),
                 (uint8*)&dsl2edit + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));

         SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH8W_HD,
                                   "labelValid2 = %d, mcastLabel2 = %d label2 = 0x%x ,oamEn2 = %d\n",
                                   dsl3editMpls8w.label_valid2,dsl3editMpls8w.mcast_label2,dsl3editMpls8w.label2, dsl3editMpls8w.oam_en2);
         sys_greatbelt_lkup_ttl(lchip, dsl3editMpls8w.ttl_index2, &ttl);
         SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH8W_HD,
                                   "mapttl2 = %d, deriveExp2 = %d ,ttl = %d exp2 = %d\n",
                                   dsl3editMpls8w.map_ttl2, dsl3editMpls8w.derive_exp2, ttl,dsl3editMpls8w.exp2);
         SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH8W_HD,
                                   "labelValid3 = %d, mcastLabel3 = %d label3 = 0x%x ,oamEn3 = %d\n",
                                   dsl3editMpls8w.label_valid3,dsl3editMpls8w.mcast_label3,dsl3editMpls8w.label3, dsl3editMpls8w.oam_en3);
         sys_greatbelt_lkup_ttl(lchip, dsl3editMpls8w.ttl_index3, &ttl);
         SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH8W_HD,
                                   "mapttl3 = %d, deriveExp3 = %d ,ttlIndex3 = %d exp3 = %d\n",
                                   dsl3editMpls8w.map_ttl3, dsl3editMpls8w.derive_exp3,ttl,dsl3editMpls8w.exp3);
      }
      else
      {
        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL2EDIT_ETH8W_HD,
                                   "ipDa79to64 = 0x%x ,ipDa64to32 = 0x%x ipDa31to0 = 0x%x\n",
                                   dsl2edit.ip_da79_79 << 15 | dsl2edit.ip_da78_64, dsl2edit.ip_da63_32, dsl2edit.ip_da31_0);
      }

    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_dsl3edit_mpls4w(uint8 lchip, uint32 offset, bool detail)
{

    uint32 cmd;
    ds_l3_edit_mpls4_w_t  dsl3edit;
    uint32 ttl = 0;

    if (offset == 0xFFFF)
    {
        return CTC_E_NONE;
    }
    if (detail)
    {

#define SYS_NH_DSL3EDIT_MPLS4W_HD "DsL3EditMpls4W"

    cmd = DRV_IOR(DsL3EditMpls4W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset, cmd, &dsl3edit));

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_MPLS4W_HD,
                               "------------------DsL3EditMpls4W(0x%x)--------------\n", offset);

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_MPLS4W_HD,
                               "MartiniEncapValid = %d martiniEncapType:%d \n",
                               dsl3edit.martini_encap_valid, dsl3edit.martini_encap_type);

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_MPLS4W_HD,
                               "srcDscpType = %d,etreeLeafEn = %d,l3RewriteType = %d, \n",
                               dsl3edit.src_dscp_type, dsl3edit.etree_leaf_en, dsl3edit.l3_rewrite_type);

    sys_greatbelt_lkup_ttl(lchip, dsl3edit.ttl_index0, &ttl);
    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_MPLS4W_HD,
                               "DeriveExp0 = %d,label0 = %u, ttl = %d exp0 = %d\n",
                               dsl3edit.derive_exp0,  dsl3edit.label0, ttl, dsl3edit.exp0);

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_MPLS4W_HD,
                               "DeriveExp0 = %d, oamEn0 = %d,mapTtl0=%d, mcastLabel0 = %d\n",
                               dsl3edit.derive_exp0, dsl3edit.oam_en0, dsl3edit.map_ttl0, dsl3edit.mcast_label0);



        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_MPLS4W_HD,
                                   "isMet = %d, isNexthop = %d, dsType = %d\n",
                                   dsl3edit.is_met, dsl3edit.is_nexthop, dsl3edit.ds_type);
        sys_greatbelt_lkup_ttl(lchip, dsl3edit.ttl_index1, &ttl);
        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_MPLS4W_HD,
                                   "Label1Valid = %d, Label1 = 0x%x, ttl = %d, exp1 = %d \n",
                                   dsl3edit.label_valid1, dsl3edit.label1,
                                   ttl, dsl3edit.exp1);
        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_MPLS4W_HD,
                                   "DeriveExp1 = %d, oamEn1 = %d, mapTtl1 = %d ,mcastLabel1 = %d\n",
                                   dsl3edit.derive_exp1, dsl3edit.oam_en1, dsl3edit.map_ttl0, dsl3edit.mcast_label0);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_dsl3edit_tunnel_v4(uint8 lchip, uint32 offset, bool detail)
{

    uint32 cmd;
    uint32 ttl = 0;
    ds_l3_edit_tunnel_v4_t  dsl3edit;

#define SYS_NH_DSL3EDIT_TUNNEL_V4_HD "DsL3EditTunnelV4"

    cmd = DRV_IOR(DsL3EditTunnelV4_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset, cmd, &dsl3edit));

    sys_greatbelt_lkup_ttl(lchip, dsl3edit.ttl_index, &ttl);

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_TUNNEL_V4_HD,
                               "------------------DsL3EditTunnelV4(0x%x)--------------\n", offset);

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_TUNNEL_V4_HD,
                               "ttl = %d, dscp = %d, map_ttl = %d, derive_dscp = %d\n",
                               ttl, dsl3edit.dscp,
                               dsl3edit.map_ttl, dsl3edit.derive_dscp);

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_TUNNEL_V4_HD,
                               "gre_key_31_16(mtu_size) = 0x%x,ipsa_index =%d ,ip_da = 0x%x, inner_header_type = %d\n",
                               dsl3edit.ipv4_sa_index >> 16, dsl3edit.ipv4_sa_index & 0x7, dsl3edit.ip_da, dsl3edit.inner_header_type);
    if (detail)
    {

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_TUNNEL_V4_HD,
                                   "isMet = %d, isNexthop = %d, dsType = %d\n",
                                   dsl3edit.is_met, dsl3edit.is_nexthop, dsl3edit.ds_type);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_TUNNEL_V4_HD,
                                   "is_atap_tunnel = %d, t6to4_tunnel_sa =  %d, t6to4_tunnel = %d, inner_header_valid = %d\n",
                                   dsl3edit.isatp_tunnel, dsl3edit.tunnel6_to4_sa,
                                   dsl3edit.tunnel6_to4_da, dsl3edit.inner_header_valid);
        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_TUNNEL_V4_HD,
                                   "mtu_check_en = %d, gre_key_15_0_udp_dest_port = %d, gre_flags = %d, gre_version = %d,protocol_type:0x%x\n",
                                   dsl3edit.mtu_check_en, dsl3edit.gre_key, dsl3edit.gre_flags, dsl3edit.gre_version,
                                   (dsl3edit.gre_protocol15_14 << 14 | dsl3edit.gre_protocol13_0));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_dsl3edit_tunnel_v6(uint8 lchip, uint32 offset, bool detail)
{
    uint32 cmd;
    uint32 ttl = 0;
    ds_l3_edit_tunnel_v6_t  dsl3edit;
    uint32 ipda127_96 = 0;
    uint32 ipda95_80 = 0;

#define SYS_NH_DSL3EDIT_TUNNEL_V6_HD "DsL3EditTunnelV6"

    cmd = DRV_IOR(DsL3EditTunnelV6_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset, cmd, &dsl3edit));

    ipda127_96 = dsl3edit.ip_da127_126 << 30 | dsl3edit.ip_da125_123 << 27 | dsl3edit.ip_da122_122 << 26 |
        dsl3edit.ip_da121_114 << 18 | dsl3edit.ip_da113_102 << 6 | (dsl3edit.ip_da101_90 >> 6);

    ipda95_80 = (dsl3edit.ip_da101_90 & 0x3F) << 11 | dsl3edit.ip_da89_80;

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_TUNNEL_V6_HD,
                               "------------------DsL3EditTunnelV6(0x%x)--------------\n", offset);

    sys_greatbelt_lkup_ttl(lchip, dsl3edit.ttl_index, &ttl);
    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_TUNNEL_V6_HD,
                               "ttl = %d, tos = %d, map_ttl = %d, derive_dscp = %d\n",
                               ttl, dsl3edit.tos,
                               dsl3edit.map_ttl, dsl3edit.derive_dscp);

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_TUNNEL_V6_HD,
                               "ip_sa_index = 0x%x, ip_da127_80= 0x%.8x%.4x, ip_protocol_type = 0x%x\n",
                               dsl3edit.ip_sa_index, ipda127_96, ipda95_80, dsl3edit.ip_protocol_type);

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_TUNNEL_V6_HD, "inner_header_valid = %d, inner_header_type = 0x%x\n",
                               dsl3edit.inner_header_valid, dsl3edit.inner_header_type);
    if (detail)
    {
        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_TUNNEL_V6_HD,
                                   "isMet = %d, isNexthop = %d, dsType = %d\n",
                                   dsl3edit.is_met, dsl3edit.is_nexthop, dsl3edit.ds_type);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_TUNNEL_V6_HD,
                                   "gre_protocol = 0x%x, gre_key =  0x%x, flow_label= 0x%x\n",
                                   dsl3edit.gre_protocol15_14 << 14 | dsl3edit.gre_protocol13_0, dsl3edit.gre_key31_16 << 16 | dsl3edit.gre_key15_0,
                                   dsl3edit.flow_label);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_dsl3edit_tunnel_nat_v4(uint8 lchip, uint32 offset, bool detail)
{
    uint32 cmd;
    ip_addr_t   ipv4 = 0;
    ds_l3_edit_nat4_w_t  dsl3edit;

#define SYS_NH_DSL3EDIT_NAT_4W_HD "DsL3EditNat4W"

    cmd = DRV_IOR(DsL3EditNat4W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset, cmd, &dsl3edit));

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_NAT_4W_HD,
                               "------------------DsL3EditNat4W(0x%x) for NAT--------------\n", offset);

    ipv4 = (dsl3edit.ip_da31_30 << 30) | dsl3edit.ip_da29_0;
    if (dsl3edit.l4_dest_port)
    {
        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_NAT_4W_HD,
                                   "new_ipda = 0x%08x, new_l4dstport = %d\n",
                                   ipv4, dsl3edit.l4_dest_port);
    }
    else
    {
        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_NAT_4W_HD,
                                   "new_ipda = 0x%08x\n", ipv4);
    }

    if (detail)
    {

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_NAT_4W_HD,
                                   "isMet = %d, isNexthop = %d, dsType = %d\n",
                                   dsl3edit.is_met, dsl3edit.is_next_hop, dsl3edit.ds_type);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_dsl3edit_tunnel_ivi_6to4(uint8 lchip, uint32 offset, bool detail)
{

    uint32 cmd;
    uint32 prefix_len = 0;
    ds_l3_edit_nat4_w_t  dsl3edit;

#define SYS_NH_DSL3EDIT_NAT_4W_HD "DsL3EditNat4W"

    cmd = DRV_IOR(DsL3EditNat4W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset, cmd, &dsl3edit));

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_NAT_4W_HD,
                               "------------------DsL3EditNat4W(0x%x) for IVI6to4--------------\n", offset);

    prefix_len = (dsl3edit.ip_da_prefix_length2_2 << 2) + dsl3edit.ip_da_prefix_length1_0;
    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_NAT_4W_HD,
                               "prefix_len = %d, is_rfc6052 = %d\n",
                               prefix_len, dsl3edit.ipv4_embeded_mode);

    if (detail)
    {

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_NAT_4W_HD,
                                   "isMet = %d, isNexthop = %d, dsType = %d\n",
                                   dsl3edit.is_met, dsl3edit.is_next_hop, dsl3edit.ds_type);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_dsl3edit_tunnel_ivi_4to6(uint8 lchip, uint32 offset, bool detail)
{

    uint32 cmd;
    uint32 prefix_len = 0;
    ipv6_addr_t ipv6, ipv6_address;
    ds_l3_edit_nat8_w_t  dsl3edit;
    char buf[CTC_IPV6_ADDR_STR_LEN];

    sal_memset(ipv6, 0, sizeof(ipv6));
    sal_memset(&dsl3edit, 0, sizeof(dsl3edit));

#define SYS_NH_DSL3EDIT_NAT_8W_HD "DsL3EditNat8W"

    cmd = DRV_IOR(DsL3EditNat8W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset, cmd, &dsl3edit));

    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_NAT_8W_HD,
                               "------------------DsL3EditNat8W(0x%x) for IVI4to6--------------\n", offset);

    prefix_len = (dsl3edit.ip_da_prefix_length2_2 << 2) + dsl3edit.ip_da_prefix_length1_0;
    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_NAT_8W_HD,
                               "prefix_len = %d, is_rfc6052 = %d\n",
                               prefix_len, dsl3edit.ipv4_embeded_mode);

    ipv6[0] = (dsl3edit.ip_da127_124 << 28) | (dsl3edit.ip_da123_120 << 24) | dsl3edit.ip_da119_96;
    ipv6[1] = dsl3edit.ip_da95_64;
    ipv6[2] = (dsl3edit.ip_da63_49 << 17) | (dsl3edit.ip_da48 << 18) |
        (dsl3edit.ip_da47_40 << 8) | dsl3edit.ip_da39_32;
    ipv6[3] = (dsl3edit.ip_da31_30 << 30) | dsl3edit.ip_da29_0;

    ipv6_address[0] = sal_ntohl(ipv6[0]);
    ipv6_address[1] = sal_ntohl(ipv6[1]);
    ipv6_address[2] = sal_ntohl(ipv6[2]);
    ipv6_address[3] = sal_ntohl(ipv6[3]);

    sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_IPV6_ADDR_STR_LEN);
    SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_NAT_8W_HD,
                               "ipv6_prefix_addr = %s\n", buf);

    if (detail)
    {

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSL3EDIT_NAT_8W_HD,
                                   "isMet = %d, isNexthop = %d, dsType = %d\n",
                                   dsl3edit.is_met, dsl3edit.is_next_hop, dsl3edit.ds_type);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_dsmet(uint8 lchip, uint32 dsmet_offset, bool detail)
{

    uint32 cmd;
    ds_met_entry_t dsmet;
    uint16 logicDestPort;
    uint16 ucastId;
    uint32 dest_port_bitmap_0;
    uint32 dest_port_bitmap_1;

#define SYS_NH_DSMET_HD "DsMet"

    cmd = DRV_IOR(DsMetEntry_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, dsmet_offset, cmd, &dsmet));

    logicDestPort  = (dsmet.logic_dest_port12_11 << 11) | (dsmet.logic_dest_port10 << 10) | (dsmet.logic_dest_port9_8 << 8)
        | (dsmet.logic_dest_port7_6 << 6) | (dsmet.logic_dest_port5 << 5) | dsmet.logic_dest_port4_0;
    dest_port_bitmap_0 = (dsmet.next_hop_ext << 31)
        | (dsmet.logic_dest_port7_6 << 29)
        | (dsmet.leaf_check_en << 28)
        | (dsmet.ucast_id13 << 27)
        | (dsmet.logic_port_type_check << 26)
        | (dsmet.logic_dest_port5 << 25)
        | (dsmet.logic_dest_port4_0 << 20)
        | dsmet.replication_ctl;
    dest_port_bitmap_1 = (dsmet.logic_dest_port12_11 << 22)
        | (dsmet.logic_dest_port10 << 21)
        | (dsmet.port_bitmap52_50 << 18)
        | (dsmet.logic_port_check_en << 15)
        | (dsmet.logic_dest_port9_8 << 13)
        | (dsmet.aps_bridge_en << 12)
        | dsmet.ucast_id_low;
    ucastId = (dsmet.ucast_id15_14 << 14)
        | (dsmet.ucast_id13 << 13)
        | (((dsmet.logic_port_check_en >> 1) & 0x1) << 12)
        | (dsmet.ucast_id_low);
    if (detail)
    {
        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSMET_HD,
                                   "DsMetOffset = 0x%x, NextMetOffset = 0x%x, EndLocalRepli = %d\n",
                                   dsmet_offset, dsmet.next_met_entry_ptr, dsmet.end_local_rep);
        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSMET_HD,
                                   "ReplicationCtl = 0x%x, NexthopExt = %d, portCheckDiscard = %d\n",
                                   dsmet.replication_ctl, dsmet.next_hop_ext, dsmet.phy_port_check_discard);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSMET_HD,
                                   "ApsBrgEn = %d, isMet = %d, leafCheckEn = %d isLinkagg:%d \n",
                                   dsmet.aps_bridge_en, dsmet.is_met, dsmet.leaf_check_en, dsmet.is_link_aggregation);
        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSMET_HD,
                                   "McastMode = %d ucastId:0x%x PortBitmapLow:0x%x,PortBitmapHigh:0x%x\n",
                                   dsmet.mcast_mode, ucastId, dest_port_bitmap_0, dest_port_bitmap_1);

        SYS_NH_DEBUG_DUMP_HW_TABLE(SYS_NH_DSMET_HD,
                                   "logicPortCheckEn = %d logicDestPort:0x%x\n",
                                   dsmet.logic_port_check_en, logicDestPort);
    }

    return CTC_E_NONE;

}

int32
sys_greatbelt_nh_dump_iloop(uint8 lchip, uint32 nhid, bool detail)
{
    sys_nh_info_brguc_t* p_brguc_info;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    int32 ret;

    CTC_ERROR_RETURN(sys_greatbelt_nh_debug_get_nh_master(lchip, &p_gb_nh_master));

    ret = (sys_greatbelt_nh_debug_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo));
    if (CTC_E_NH_NOT_EXIST == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "  BridgeUC nexthop %d not exist\n", nhid);
        return CTC_E_NONE;
    }
    else if (CTC_E_NH_INVALID_NHID == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Invalid nexthopId %d\n", nhid);
        return CTC_E_NONE;
    }
    else
    {
        CTC_ERROR_RETURN(ret);
    }

    if (SYS_NH_TYPE_ILOOP != p_nhinfo->hdr.nh_entry_type)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Isn't BridgeUC nexthop for nexthopId %d\n", nhid);
        return CTC_E_INVALID_PARAM;
    }

    p_brguc_info = (sys_nh_info_brguc_t*)(p_nhinfo);

    if (detail)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, SYS_NH_DEBUG_TYPE_HEAD("IPE Loopback"));
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " EntryType = %d, EntryFlags = 0x%x\n",
                       p_nhinfo->hdr.nh_entry_type, p_nhinfo->hdr.nh_entry_flags);

        /*Dump dsFwd*/
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "   <<< Dump DsFwd, Offset = %d, Lchip = %d>>>\n",
                       p_brguc_info->hdr.dsfwd_info.dsfwd_offset, lchip);
        CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsfwd(lchip, p_brguc_info->hdr.dsfwd_info.dsfwd_offset, detail));

    }
    else
    {
        ds_fwd_t dsfwd;
        uint32 cmd;
        cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_brguc_info->hdr.dsfwd_info.dsfwd_offset, cmd, &dsfwd));

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8d", nhid);
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s", "I-LOOP");
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "0x%-4x\n", dsfwd.next_hop_ptr);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_rspan(uint8 lchip, uint32 nhid, bool detail)
{
    sys_nh_info_rspan_t* p_rspan_info;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    int32 ret;

    CTC_ERROR_RETURN(sys_greatbelt_nh_debug_get_nh_master(lchip, &p_gb_nh_master));

    ret = (sys_greatbelt_nh_debug_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo));
    if (CTC_E_NH_NOT_EXIST == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "  RSPAN nexthop %d not exist\n", nhid);
        return CTC_E_NONE;
    }
    else if (CTC_E_NH_INVALID_NHID == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Invalid nexthopId %d\n", nhid);
        return CTC_E_NONE;
    }
    else
    {
        CTC_ERROR_RETURN(ret);
    }

    if (SYS_NH_TYPE_RSPAN != p_nhinfo->hdr.nh_entry_type)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Isn't RSPAN nexthop for nexthopId %d\n", nhid);
        return CTC_E_INVALID_PARAM;
    }

    p_rspan_info = (sys_nh_info_rspan_t*)(p_nhinfo);

    if (detail)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, SYS_NH_DEBUG_TYPE_HEAD("RSPAN"));
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " EntryType = %d, EntryFlags = 0x%x ,Cross_chip= %d\n",
                       p_nhinfo->hdr.nh_entry_type, p_nhinfo->hdr.nh_entry_flags, p_rspan_info->remote_chip);


        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "   <<< Dump DsNexthop, lchip = %d ,Offset = %d >>>\n", lchip, p_rspan_info->hdr.dsfwd_info.dsnh_offset);

        /*Dump Dsnh */
        CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh(lchip, p_rspan_info->hdr.dsfwd_info.dsnh_offset, detail, NULL));

    }
    else
    {
        uint32 cmd;
        ds_next_hop4_w_t   dsnh;
        ds_next_hop8_w_t   dsnh8w;
        uint32 dsnh_offset = 0;

        dsnh_offset = p_rspan_info->hdr.dsfwd_info.dsnh_offset;
        if (((dsnh_offset >> 2) & 0x3FFF) != SYS_GREATBELT_DSNH_INTERNAL_BASE)
        {
            cmd = DRV_IOR(DsNextHop4W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, dsnh_offset, cmd, &dsnh));

        }
        else
        {
            cmd = DRV_IOR(EpeNextHopInternal_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (dsnh_offset & 0x2) >> 1, cmd, &dsnh8w));
            sal_memcpy((uint8*)&dsnh, ((uint8*)&dsnh8w + sizeof(ds_next_hop4_w_t) * (dsnh_offset & 0x1)), sizeof(ds_next_hop4_w_t));
        }

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8d", nhid);
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s", "RSPAN");
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%d\n", dsnh.dest_vlan_ptr);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_brguc(uint8 lchip, uint32 nhid, bool detail)
{
    sys_nh_info_brguc_t* p_brguc_info;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    int32 ret;

    CTC_ERROR_RETURN(sys_greatbelt_nh_debug_get_nh_master(lchip, &p_gb_nh_master));

    ret = (sys_greatbelt_nh_debug_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo));
    if ((CTC_E_NH_NOT_EXIST == ret) && detail)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "  BridgeUC nexthop %d not exist\n", nhid);
        return CTC_E_NONE;
    }
    else if ((CTC_E_NH_INVALID_NHID == ret) && detail)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Invalid nexthopId %d\n", nhid);
        return CTC_E_NONE;
    }
    else
    {
        CTC_ERROR_RETURN(ret);
    }

    if ((SYS_NH_TYPE_BRGUC != p_nhinfo->hdr.nh_entry_type)&& detail)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Isn't BridgeUC nexthop for nexthopId %d\n", nhid);
        return CTC_E_INVALID_PARAM;
    }

    p_brguc_info = (sys_nh_info_brguc_t*)(p_nhinfo);
    if(detail)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, SYS_NH_DEBUG_TYPE_HEAD("BridgeUC"));
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " EntryType = %d, EntryFlags = 0x%x\n",
                       p_nhinfo->hdr.nh_entry_type, p_nhinfo->hdr.nh_entry_flags);

        /*Dump dsFwd*/
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "   <<< Dump DsFwd, Offset = %d, Lchip = %d>>>\n",
                       p_brguc_info->hdr.dsfwd_info.dsfwd_offset, lchip);
        CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsfwd(lchip, p_brguc_info->hdr.dsfwd_info.dsfwd_offset, detail));

        /*Dump Dsnh*/
        switch (p_brguc_info->nh_sub_type)
        {
            case SYS_NH_PARAM_BRGUC_SUB_TYPE_BASIC:
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    BridgeUC nexthop sub type is: Normal bridge unicast nexthop\n");
                break;

            case SYS_NH_PARAM_BRGUC_SUB_TYPE_VLAN_EDIT:
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    BridgeUC nexthop sub type is: Egress vlan translation nexthop\n");
                break;

            case SYS_NH_PARAM_BRGUC_SUB_TYPE_BYPASS:
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    BridgeUC nexthop sub type is: Bypass all nexthop\n");
                break;

            case SYS_NH_PARAM_BRGUC_SUB_TYPE_APS_VLAN_EDIT:
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    BridgeUC nexthop sub type is: APS egress vlan edit nexthop\n");
                break;

            default:
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    Dump bridgeUC nexthop ERROR, sub type is %d\n", p_brguc_info->nh_sub_type);
                return CTC_E_NH_INVALID_NH_SUB_TYPE;
        }

        if (CTC_FLAG_ISSET(p_brguc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh8w(lchip, p_brguc_info->hdr.dsfwd_info.dsnh_offset, detail, NULL));
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh(lchip, p_brguc_info->hdr.dsfwd_info.dsnh_offset, detail, NULL));
        }

        if (SYS_NH_PARAM_BRGUC_SUB_TYPE_APS_VLAN_EDIT == p_brguc_info->nh_sub_type
            && !CTC_FLAG_ISSET(p_brguc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH))
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    APS protection path's dsnexthop information:\n");

            if (CTC_FLAG_ISSET(p_brguc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh8w(lchip, (p_brguc_info->hdr.dsfwd_info.dsnh_offset+2), detail, NULL));
            }
            else
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh(lchip, (p_brguc_info->hdr.dsfwd_info.dsnh_offset + 1), detail, NULL));
            }
        }

    }
    else
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8d%-15s0x%04x\n",nhid, "Bridge Unicast",p_brguc_info->dest.dest_gport);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_ipuc_brief(uint8 lchip, uint32 nhid,sys_nh_info_ipuc_t * p_ipuc_info)
{
    uint32 dsl2edit_ptr = 0;
    sys_l3if_prop_t l3if_prop;
    ds_next_hop4_w_t   dsnh;
    ds_next_hop8_w_t   dsnh8w;
    uint32 mac_high = 0;
    uint32 mac_low = 0;
    uint32 cmd;
    ds_fwd_t dsfwd;
    uint32 dsnh_offset;
    ds_l2_edit_eth4_w_t  dsl2edit;
    uint16 gport = 0;
    uint8 mac_en = 0;
    int32 ret = CTC_E_NONE;
    char strbuf[64] = {0};

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8d", nhid);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s", "IP Unicast");
    if (CTC_FLAG_ISSET(p_ipuc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-13s","-");
    }
    else if (CTC_FLAG_ISSET(p_ipuc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
        ret = DRV_IOCTL(lchip, p_ipuc_info->hdr.dsfwd_info.dsfwd_offset, cmd, &dsfwd);
        if (CTC_E_NONE != ret)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-13s", "-");
        }
        gport = SYS_GREATBELT_DESTMAP_TO_GPORT(dsfwd.dest_map);
        sal_sprintf(strbuf, "0x%04x", gport);
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-13s", strbuf);
    }
    else
    {
        sal_sprintf(strbuf, "0x%04x", p_ipuc_info->gport);
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-13s", strbuf);
    }

    l3if_prop.l3if_id = p_ipuc_info->l3ifid;
    ret = sys_greatbelt_l3if_get_l3if_info(lchip, 1, &l3if_prop);
    if ((CTC_E_NONE != ret) || (CTC_L3IF_TYPE_PHY_IF == l3if_prop.l3if_type))
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9s", "-");
    }
    else
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9d", l3if_prop.vlan_id);
    }

    if (CTC_E_NONE != ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-10s","-");
    }
    else
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-10d", p_ipuc_info->l3ifid);
    }


    dsnh_offset = p_ipuc_info->hdr.dsfwd_info.dsnh_offset;
    if (((dsnh_offset >> 2) & 0x3FFF) != SYS_GREATBELT_DSNH_INTERNAL_BASE)
    {
        cmd = DRV_IOR(DsNextHop4W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, dsnh_offset, cmd, &dsnh));

    }
    else
    {
        cmd = DRV_IOR(EpeNextHopInternal_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (dsnh_offset & 0x2) >> 1, cmd, &dsnh8w));
        sal_memcpy((uint8*)&dsnh, ((uint8*)&dsnh8w + sizeof(ds_next_hop4_w_t) * (dsnh_offset & 0x1)), sizeof(ds_next_hop4_w_t));
    }
    if (dsnh.is_nexthop)
    {
        if (dsnh.payload_operation == SYS_NH_OP_ROUTE_COMPACT)
        {
            SYS_NH_GET_MAC_FROM_DSNH(dsnh, mac_high, mac_low);
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%04x.%04x.%04x\n", mac_high&0xFFFF, mac_low >> 16, mac_low&0xFFFF);
        }
        else
        {
            dsl2edit_ptr = ((dsnh.l2_edit_ptr15 << 14) | (dsnh.l2_edit_ptr14_12 << 12) | dsnh.l2_edit_ptr11_0);
            cmd = DRV_IOR(DsL2EditEth4W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, dsl2edit_ptr, cmd, &dsl2edit));
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%04x.%04x.%04x\n", dsl2edit.mac_da47_32&0xFFFF,
                           ((dsl2edit.mac_da31_30 << 30) | dsl2edit.mac_da29_0) >> 16,
                           ((dsl2edit.mac_da31_30 << 30) | dsl2edit.mac_da29_0)&0xFFFF);
        }
        mac_en = 1;
    }

    if (!mac_en)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"-\n");
    }
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_ipuc(uint8 lchip, uint32 nhid, bool detail)
{
    sys_nh_info_ipuc_t* p_ipuc_info;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    uint32 dsl2edit_ptr = 0;
    int32 ret;

    CTC_ERROR_RETURN(sys_greatbelt_nh_debug_get_nh_master(lchip, &p_gb_nh_master));

    ret = (sys_greatbelt_nh_debug_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo));
    if (CTC_E_NH_NOT_EXIST == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "  IPUC nexthop %d not exist\n", nhid);
        return CTC_E_NONE;
    }
    else if (CTC_E_NH_INVALID_NHID == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Invalid nexthopId %d\n", nhid);
        return CTC_E_NONE;
    }
    else
    {
        CTC_ERROR_RETURN(ret);
    }

    if (SYS_NH_TYPE_IPUC != p_nhinfo->hdr.nh_entry_type)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Isn't IPUC nexthop for nexthopId %d\n", nhid);
        return CTC_E_INVALID_PARAM;
    }

    p_ipuc_info = (sys_nh_info_ipuc_t*)(p_nhinfo);

    if (detail)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, SYS_NH_DEBUG_TYPE_HEAD("IPUC"));
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " EntryType = %d, EntryFlags = 0x%x\n",
                       p_nhinfo->hdr.nh_entry_type, p_nhinfo->hdr.nh_entry_flags);


        /*Dump dsFwd*/
        if (CTC_FLAG_ISSET(p_ipuc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "   <<< Dump DsFwd, Offset = %d, Lchip = %d>>>\n",
                           p_ipuc_info->hdr.dsfwd_info.dsfwd_offset, lchip);
            CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsfwd(lchip, p_ipuc_info->hdr.dsfwd_info.dsfwd_offset, detail));
        }
        else
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "   <<< No DsFwd>>>\n");
        }

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    L3 interface Id is %d\n", p_ipuc_info->l3ifid);
        /*Dump Dsnh & DsL2Edit*/
        CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh(lchip, p_ipuc_info->hdr.dsfwd_info.dsnh_offset, detail, &dsl2edit_ptr));
        if (CTC_FLAG_ISSET(p_ipuc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT))
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsl2edit(lchip, dsl2edit_ptr, detail));
        }


        if (p_ipuc_info->hdr.p_ref_nh_list)
        {
            sys_nh_ref_list_node_t* p_curr = NULL;
            sys_nh_info_ecmp_t* p_ref_nhinfo;

            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n------------------ECMP Information---------------------\n");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "The Nexthop was other ECMP refenerced:\n");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-16s %-16s %-10s\n", "ECMP NHID", "ecmp_cnt", "valid_cnt");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "--------------------------------------------------------\n");

            p_curr = p_ipuc_info->hdr.p_ref_nh_list;

            while (p_curr)
            {
                p_ref_nhinfo = (sys_nh_info_ecmp_t*)p_curr->p_ref_nhinfo;
                if (p_ref_nhinfo)
                {
                    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-16d %-16d %-16d\n", p_ref_nhinfo->ecmp_nh_id, p_ref_nhinfo->ecmp_cnt, p_ref_nhinfo->valid_cnt);
                }

                p_curr = p_curr->p_next;
            }
        }
    }
    else
    {
        sys_greatbelt_nh_dump_ipuc_brief(lchip, nhid,p_ipuc_info);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_ecmp(uint8 lchip, uint32 nhid, bool detail, bool is_all)
{

    sys_nh_info_ecmp_t* p_ecmp_info;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    int32 ret;
    uint8 loop = 0;
    uint8 loop2 = 0;

    CTC_ERROR_RETURN(sys_greatbelt_nh_debug_get_nh_master(lchip, &p_gb_nh_master));

    ret = (sys_greatbelt_nh_debug_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo));
    if (CTC_E_NH_NOT_EXIST == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "  ECMP nexthop %d not exist\n", nhid);
        return CTC_E_NONE;
    }
    else if (CTC_E_NH_INVALID_NHID == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Invalid nexthopId %d\n", nhid);
        return CTC_E_NONE;
    }
    else
    {
        CTC_ERROR_RETURN(ret);
    }

    if (SYS_NH_TYPE_ECMP != p_nhinfo->hdr.nh_entry_type)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Isn't ECMP nexthop for nexthopId %d\n", nhid);
        return CTC_E_INVALID_PARAM;
    }

    p_ecmp_info = (sys_nh_info_ecmp_t*)(p_nhinfo);

    if (detail)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, SYS_NH_DEBUG_TYPE_HEAD("ECMP"));
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " EntryType = %d, EntryFlags = 0x%x\n",
                       p_nhinfo->hdr.nh_entry_type, p_nhinfo->hdr.nh_entry_flags);

        if (!p_ecmp_info)
        {
            return CTC_E_NONE;
        }

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " Ecmp member number:%d, member NHID:\n", p_ecmp_info->ecmp_cnt);

        for (loop = 0; loop < p_ecmp_info->ecmp_cnt; loop++)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%d  ", p_ecmp_info->nh_array[loop]);
        }

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " Valid ecmp member number:%d,member NHID:\n", p_ecmp_info->valid_cnt);

        for (loop = 0; loop < p_ecmp_info->valid_cnt; loop++)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%d  ", p_ecmp_info->nh_array[loop]);
        }

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");


        /*Dump dsFwd*/
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "   <<< Dump DsFwd, Based Offset = %d, Lchip = %d>>>\n",
                       p_ecmp_info->hdr.dsfwd_info.dsfwd_offset, lchip);

        for (loop = 0; loop < p_ecmp_info->mem_num; loop++)
        {
            if (p_ecmp_info->valid_cnt == 0)
            {
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "   <<< Member NHID:%d\n", CTC_NH_RESERVED_NHID_FOR_DROP);
            }
            else
            {
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "   <<< Member NHID:%d\n", p_ecmp_info->nh_array[loop2]);

                if ((++loop2) == p_ecmp_info->valid_cnt)
                {
                    loop2 = 0;
                }
            }

            CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsfwd(lchip, p_ecmp_info->hdr.dsfwd_info.dsfwd_offset + loop, detail));
        }

    }
    else
    {

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-8dECMP\n",nhid);

        if (! is_all)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"\n%-6s%s\n","No.","MEMBER-NHID");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"-------------------\n");

            for (loop = 0; loop < p_ecmp_info->ecmp_cnt; loop++)
            {
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6d%d\n", loop, p_ecmp_info->nh_array[loop]);
            }
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_ip_mcast_l3if_info(uint8 lchip, uint16 gport, uint16 vlan_id)
{
    int32 ret = CTC_E_NONE;
    sys_l3if_prop_t l3if_prop;

    sal_memset(&l3if_prop,0,sizeof(sys_l3if_prop_t));
    l3if_prop.gport = gport;
    l3if_prop.vlan_id = vlan_id;

    l3if_prop.l3if_type = CTC_L3IF_TYPE_PHY_IF;
    ret = sys_greatbelt_l3if_get_l3if_info(lchip, 0, &l3if_prop);
    if (CTC_E_NONE == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9s%-8d%s\n","-",l3if_prop.l3if_id,"-");
        return CTC_E_NONE;
    }

    l3if_prop.l3if_type = CTC_L3IF_TYPE_VLAN_IF;
    ret = sys_greatbelt_l3if_get_l3if_info(lchip, 0, &l3if_prop);
    if (CTC_E_NONE == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9d%-8d%s\n",l3if_prop.vlan_id, l3if_prop.l3if_id,"-");
        return CTC_E_NONE;
    }

    l3if_prop.l3if_type = CTC_L3IF_TYPE_SUB_IF;
    ret = sys_greatbelt_l3if_get_l3if_info(lchip, 0, &l3if_prop);
    if (CTC_E_NONE == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9d%-8d%s\n", l3if_prop.vlan_id, l3if_prop.l3if_id,"-");
        return CTC_E_NONE;
    }
    return CTC_E_NONE;
}

void
sys_greatbelt_nh_dump_mcast_member_info(uint8 lchip, uint32 memcnt, char * type, uint8 gchip, uint8 gport, bool is_l2)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-6d",memcnt);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-14s",type);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"0x%02x%02x       ",gchip,gport);

    if(is_l2)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9s%-8s%s\n","-","-","-");
    }
}


void
sys_greatbelt_nh_dump_mcast_bitmap_info(uint8 lchip, sys_nh_mcast_meminfo_t* p_meminfo_com , uint8 gchip, uint32 * memcnt , bool is_l2)
{
    uint8 loop = 0;
    uint32 temp = 0;
    uint16 gport = 0;
    for (loop = 0; loop < 64 ; loop ++)
    {
        temp = (loop <32) ? p_meminfo_com->dsmet.pbm0 : p_meminfo_com->dsmet.pbm1;

        if(CTC_IS_BIT_SET(temp, ((loop <32)? loop : loop - 32)))
        {
            (*memcnt)++;
            sys_greatbelt_nh_dump_mcast_member_info(lchip, (*memcnt), (is_l2?"L2 Mcast":"IP Mcast"),gchip,loop,is_l2);
            if (!is_l2)
            {
                gport = ( ( gchip & 0xFF ) << 8) + ( loop & 0xFF);
                sys_greatbelt_nh_dump_ip_mcast_l3if_info(lchip, gport, p_meminfo_com->dsmet.vid);
            }
        }

    }
}

int32
sys_greatbelt_nh_dump_mcast_brief(uint8 lchip, uint32 nhid,sys_nh_info_mcast_t*  p_mcast_info, bool is_all)
{
    sys_nh_mcast_meminfo_t* p_meminfo_com;
    ctc_list_pointer_node_t* p_pos;
    uint32 memcnt = 0;
    uint8 loop = 0;
    uint8 gchip = 0;
    uint16 gport = 0;
    uint32 cmd = 0;
    ds_next_hop4_w_t dsnh;
    uint16 vlan_id;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8d",nhid);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s", "Mcast");
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%d\n", p_mcast_info->basic_met_offset);
    if (is_all)
    {
        return CTC_E_NONE;
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"\nMulticast Member:\n");
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"-------------------------------------------------------------\n");
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6s%-14s%-13s%-9s%-8s%s\n", "No.", "MEMBER-TYPE", "DESTPORT", "VLAN", "L3IFID", "MEMBER-NHID");

    CTC_LIST_POINTER_LOOP(p_pos, &(p_mcast_info->p_mem_list))
    {

        p_meminfo_com = _ctc_container_of(p_pos, sys_nh_mcast_meminfo_t, list_head);

        if (CTC_FLAG_ISSET(p_meminfo_com->dsmet.flag, SYS_NH_DSMET_FLAG_IS_LINKAGG))
        {
            gchip = 0x1F;
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
        }

        switch (p_meminfo_com->dsmet.member_type)
        {
            case SYS_NH_PARAM_BRGMC_MEM_LOCAL:
                if (CTC_FLAG_ISSET(p_meminfo_com->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM))
                {
                    sys_greatbelt_nh_dump_mcast_bitmap_info(lchip, p_meminfo_com , gchip, &memcnt , TRUE);
                }
                else
                {
                    if (0x7FFF == p_meminfo_com->dsmet.ucastid )
                    {
                        continue;
                    }
                    memcnt++;
                    sys_greatbelt_nh_dump_mcast_member_info(lchip, memcnt, "L2 Mcast", gchip,
                                                            (p_meminfo_com->dsmet.ucastid & 0xFF), TRUE);
                }
                break;

            case SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH:
                memcnt++;
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6d", memcnt);
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-14s", "L2 Mcast");
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-13s%-9s%-8s%d\n", "-", "-", "-", p_meminfo_com->dsmet.ref_nhid);
                break;
            case SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH_DESTID:
                memcnt++;
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6d", memcnt);
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-14s", "L2 Mcast");
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-13s%-9s%-8s%d\n", "-", "-", "-", p_meminfo_com->dsmet.ref_nhid);
                break;

            case SYS_NH_PARAM_IPMC_MEM_LOCAL:
                if (CTC_FLAG_ISSET(p_meminfo_com->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM))
                {
                    sys_greatbelt_nh_dump_mcast_bitmap_info(lchip, p_meminfo_com, gchip , &memcnt , FALSE);
                }
                else
                {
                    if (0x7FFF == p_meminfo_com->dsmet.ucastid )
                    {
                        continue;
                    }
                    memcnt++;
                    sys_greatbelt_nh_dump_mcast_member_info(lchip, memcnt, "IP Mcast", gchip,
                                                            (p_meminfo_com->dsmet.ucastid & 0xFF), FALSE);
                    if (CTC_FLAG_ISSET(p_meminfo_com->dsmet.flag, SYS_NH_DSMET_FLAG_IS_LINKAGG))
                    {
                        gport = CTC_MAP_TID_TO_GPORT(p_meminfo_com->dsmet.ucastid);
                    }
                    else
                    {
                        gport = CTC_MAP_LPORT_TO_GPORT(gchip, (p_meminfo_com->dsmet.ucastid & 0xFF));
                    }

                    sys_greatbelt_nh_dump_ip_mcast_l3if_info(lchip, gport, p_meminfo_com->dsmet.vid);
                }
                break;

            case SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP:
                for (loop = 0; loop <= p_meminfo_com->dsmet.replicate_num ; loop ++)
                {
                    sys_l3if_prop_t l3if_prop;
                    memcnt++;
                    sys_greatbelt_nh_dump_mcast_member_info(lchip, memcnt, "IP Mcast", gchip,
                                                            (p_meminfo_com->dsmet.ucastid & 0xFF), FALSE);
                    if (CTC_FLAG_ISSET(p_meminfo_com->dsmet.flag, SYS_NH_DSMET_FLAG_IS_LINKAGG))
                    {
                        gport = CTC_MAP_TID_TO_GPORT(p_meminfo_com->dsmet.ucastid);
                    }
                    else
                    {
                        gport = CTC_MAP_LPORT_TO_GPORT(gchip, (p_meminfo_com->dsmet.ucastid & 0xFF));
                    }
                    cmd = DRV_IOR(DsNextHop4W_t, DRV_ENTRY_FLAG);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_meminfo_com->dsmet.dsnh_offset + loop, cmd, &dsnh));
                    if (dsnh.dest_vlan_ptr > 0x1000)
                    {
                        l3if_prop.l3if_id = dsnh.dest_vlan_ptr - 0x1000;
                        sys_greatbelt_l3if_get_l3if_info(lchip, 1, &l3if_prop);
                        vlan_id = l3if_prop.vlan_id;
                    }
                    else
                    {
                        vlan_id = dsnh.dest_vlan_ptr;
                    }
                    sys_greatbelt_nh_dump_ip_mcast_l3if_info(lchip, gport, vlan_id);
                }
                break;

            case SYS_NH_PARAM_MCAST_MEM_LOCAL_LOGIC_REP:

                for (loop = 0; loop <= p_meminfo_com->dsmet.replicate_num ; loop ++)
                {
                    memcnt++;
                    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6d", memcnt);
                    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-14s", "L2 Mcast");
                    if (p_meminfo_com->dsmet.replicate_num == 0)
                    {
                        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-13s%-9s%-8s%d\n", "-", "-", "-", p_meminfo_com->dsmet.ref_nhid);
                    }
                    else
                    {
                        if (CTC_FLAG_ISSET(p_meminfo_com->dsmet.flag, SYS_NH_DSMET_FLAG_USE_DSNH8W))
                        {
                            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-13s%-9s%-8s%d\n", "-", "-", "-", p_meminfo_com->dsmet.vid_list[loop*2]);
                        }
                        else
                        {
                            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-13s%-9s%-8s%d\n", "-", "-", "-", p_meminfo_com->dsmet.vid_list[loop]);
                        }
                    }
                }
                break;


            case SYS_NH_PARAM_MCAST_MEM_REMOTE:
                memcnt++;
                gport = CTC_MAP_LPORT_TO_GPORT(gchip, (p_meminfo_com->dsmet.ucastid & 0xFF));
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6u", memcnt);
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-14s0x%.4x(gchip = %d, trunk id = %d)\n", "Remote", gport, (gport >> 8)&0xFF, gport&0xFF);


                break;

            case SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE:
                memcnt++;
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%6d", memcnt);
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%14s", "L2 Mcast");
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-13s%-9s%-8s%d\n", "-", "-", "-", p_meminfo_com->dsmet.ref_nhid);
                break;

            case SYS_NH_PARAM_BRGMC_MEM_RAPS:
                if (0x7FFF == p_meminfo_com->dsmet.ucastid )
                {
                    continue;
                }
                memcnt++;
                if (CTC_FLAG_ISSET(p_meminfo_com->dsmet.flag, SYS_NH_DSMET_FLAG_IS_LINKAGG))
                {
                    gport = CTC_MAP_TID_TO_GPORT(p_meminfo_com->dsmet.ucastid);
                }
                else
                {
                    gport = CTC_MAP_LPORT_TO_GPORT(gchip, (p_meminfo_com->dsmet.ucastid & 0xFF));
                }
                sys_greatbelt_nh_dump_mcast_member_info(lchip, memcnt, "L2 Mcast(RAPS)", gchip,
                                                        (p_meminfo_com->dsmet.ucastid & 0xFF), TRUE);
                break;

            case SYS_NH_PARAM_MCAST_MEM_MIRROR_WITH_NH:
                if (0x7FFF == p_meminfo_com->dsmet.ucastid )
                {
                    continue;
                }
                memcnt++;
                sys_greatbelt_nh_dump_mcast_member_info(lchip, memcnt, "MIRROR", gchip,
                                                        (p_meminfo_com->dsmet.ucastid & 0xFF), TRUE);
                break;

            default :
                break;
        }
    }
    ;

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_mcast(uint8 lchip, uint32 nhid, bool detail, bool is_all)
{
    sys_nh_info_mcast_t* p_mcast_info;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    sys_nh_info_com_t* p_nhinfo = NULL;
    ctc_list_pointer_node_t* p_pos;
    sys_nh_mcast_meminfo_t* p_meminfo_com;
    uint8  index = 0;
    int32 memcnt, ret;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    CTC_ERROR_RETURN(sys_greatbelt_nh_debug_get_nh_master(lchip, &p_gb_nh_master));

    ret = (sys_greatbelt_nh_debug_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo));
    if (CTC_E_NH_NOT_EXIST == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Mcast nexthop %d not exist\n", nhid);
        return CTC_E_NONE;
    }
    else if (CTC_E_NH_INVALID_NHID == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Invalid nexthopId %d\n", nhid);
        return CTC_E_NONE;
    }
    else
    {
        CTC_ERROR_RETURN(ret);
    }

    if (SYS_NH_TYPE_MCAST != p_nhinfo->hdr.nh_entry_type)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Isn't BridgeMC nexthop for nexthopId %d\n", nhid);
        return CTC_E_INVALID_PARAM;
    }


    p_mcast_info = (sys_nh_info_mcast_t*)(p_nhinfo);

    if (!detail)
    {
        sys_greatbelt_nh_dump_mcast_brief(lchip, nhid, p_mcast_info, is_all);
        return CTC_E_NONE;
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, SYS_NH_DEBUG_TYPE_HEAD("Mcast"));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " EntryType = %d, EntryFlags = 0x%x\n",
                   p_mcast_info->hdr.nh_entry_type, p_mcast_info->hdr.nh_entry_flags);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " Baisc Met offset = %d\n", p_mcast_info->basic_met_offset);


    if (CTC_FLAG_ISSET(p_mcast_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsfwd(lchip, p_mcast_info->hdr.dsfwd_info.dsfwd_offset, detail));
    }
    else
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n @@@ Dump member on Local Chip %d,  no DsFwd @@@\n", lchip);
    }

    memcnt = 0;
    CTC_LIST_POINTER_LOOP(p_pos, &(p_mcast_info->p_mem_list))
    {
        memcnt++;
        p_meminfo_com = _ctc_container_of(p_pos, sys_nh_mcast_meminfo_t, list_head);
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n<<< Multicast Member: %d, met offset = 0x%x  >>>\n",
                       memcnt, p_meminfo_com->dsmet.dsmet_offset);

        sys_greatbelt_nh_dump_dsmet(lchip, p_meminfo_com->dsmet.dsmet_offset, detail);

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    use_port_bitmap = %d\n", CTC_FLAG_ISSET(p_meminfo_com->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM));

        if (CTC_FLAG_ISSET(p_meminfo_com->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM))
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    pbm0 = 0x%x   pbm1 = 0x%x\n",
                           p_meminfo_com->dsmet.pbm0, p_meminfo_com->dsmet.pbm1);
        }
        else
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    ucastid = 0x%x\n", p_meminfo_com->dsmet.ucastid);
        }

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    NextMetOffset = 0x%x\n    DsNexthopOffset = 0x%x\n",
                       p_meminfo_com->dsmet.next_dsmet_offset,
                       p_meminfo_com->dsmet.dsnh_offset);
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    IsLinkagg = %d\n    EndLocal = %d\n    ReplicateNum = %d\n    PortCheckDiscard = %d\n",
                       CTC_FLAG_ISSET(p_meminfo_com->dsmet.flag, SYS_NH_DSMET_FLAG_IS_LINKAGG),
                       CTC_FLAG_ISSET(p_meminfo_com->dsmet.flag, SYS_NH_DSMET_FLAG_ENDLOCAL),
                       (p_meminfo_com->dsmet.replicate_num + 1),
                       CTC_FLAG_ISSET(p_meminfo_com->dsmet.flag, SYS_NH_DSMET_FLAG_PORT_CHK_DISCARD));


        if (p_meminfo_com->dsmet.member_type == SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP
            && p_meminfo_com->dsmet.replicate_num)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    free_dsnh_offset_cnt = %d\n",
                           p_meminfo_com->dsmet.free_dsnh_offset_cnt);

            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    Vlan list:");

            for (index = 0; index < (p_meminfo_com->dsmet.replicate_num + 1); index++)
            {

                if (index == p_meminfo_com->dsmet.replicate_num)
                {
                    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%d", p_meminfo_com->dsmet.vid_list[index]);
                }
                else
                {
                    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%d,", p_meminfo_com->dsmet.vid_list[index]);
                }

                if (index == 15)
                {
                    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n              ");

                }
            }

            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");

        }
        else
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    Vid:%d", p_meminfo_com->dsmet.vid);
        }

        switch (p_meminfo_com->dsmet.member_type)
        {
            case SYS_NH_PARAM_BRGMC_MEM_LOCAL:
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    McastMemberType = Bridge Multicast Local Member(No VlanTranslation)\n");
                break;

            case SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH:
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    McastMemberType = Bridge Multicast Local Member(With VlanTranslation)\n");
                break;

            case SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH_DESTID:
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    McastMemberType = Bridge Multicast Local Member With Nexthop And Assign Port\n");
                break;

            case SYS_NH_PARAM_IPMC_MEM_LOCAL:
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    McastMemberType = IP Multicast Local Member\n");
                break;

            case SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP:
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    McastMemberType = IPMC Logic Replication Member\n");
                break;

            case SYS_NH_PARAM_MCAST_MEM_REMOTE:
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    McastMemberType = Remote Entry\n");
                break;

            default:
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    McastMemberType = Invalid\n");
                break;
        }
    }
    ;


    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_mpls(uint8 lchip, uint32 nhid, bool detail)
{
    sys_nh_info_mpls_t* p_mpls_info;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    int32 ret;
    uint32 cmd;
    uint16 vc_label = 0;
    uint16 tunnel_id = 0;
    char  vc_label_str[10] = {0};
    char  aps_group_str[10] = {0};
    char  aps_path_str[10] = {0};

    ds_l3_edit_mpls4_w_t  dsl3edit;
    ds_aps_bridge_t ds_aps_bridge;

    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge_t));
    sal_memset(&dsl3edit, 0, sizeof(ds_l3_edit_mpls4_w_t));

    CTC_ERROR_RETURN(sys_greatbelt_nh_debug_get_nh_master(lchip, &p_gb_nh_master));

    ret = (sys_greatbelt_nh_debug_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo));
    if (CTC_E_NH_NOT_EXIST == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "  MPLS nexthop %d not exist\n", nhid);
        return CTC_E_NONE;
    }
    else if (CTC_E_NH_INVALID_NHID == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Invalid nexthopId %d\n", nhid);
        return CTC_E_NONE;
    }
    else
    {
        CTC_ERROR_RETURN(ret);
    }

    if (SYS_NH_TYPE_MPLS != p_nhinfo->hdr.nh_entry_type)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Isn't MPLS nexthop for nexthopId %d\n", nhid);
        return CTC_E_INVALID_PARAM;
    }

    p_mpls_info = (sys_nh_info_mpls_t*)(p_nhinfo);

    if (TRUE == detail)
    {

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, SYS_NH_DEBUG_TYPE_HEAD("MPLS"));
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " EntryType = %d, EntryFlags = 0x%x\n",
                       p_nhinfo->hdr.nh_entry_type, p_nhinfo->hdr.nh_entry_flags);

        /*Dump dsFwd*/
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "   <<< Dump DsFwd, Offset = %d, Lchip = %d>>>\n",
                       p_mpls_info->hdr.dsfwd_info.dsfwd_offset, lchip);
        CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsfwd(lchip, p_mpls_info->hdr.dsfwd_info.dsfwd_offset, detail));

        if (CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh8w(lchip, p_mpls_info->hdr.dsfwd_info.dsnh_offset, detail, NULL));
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh(lchip, p_mpls_info->hdr.dsfwd_info.dsnh_offset, detail, NULL));
        }

        if (CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN)
            && !CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH))
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "#####Protection Path's DsNexthop\n");

            if (CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh8w(lchip, p_mpls_info->hdr.dsfwd_info.dsnh_offset + 2, detail, NULL));
            }
            else
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh(lchip, p_mpls_info->hdr.dsfwd_info.dsnh_offset + 1, detail, NULL));
            }
        }

        if (CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_MPLS_POP_NH)
            && CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT))
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsl2edit(lchip,
                                                            p_mpls_info->p_dsl2edit->offset, detail));
        }
        if (p_mpls_info->working_path.p_mpls_tunnel)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Tunnel Id = %d \n", p_mpls_info->working_path.p_mpls_tunnel->tunnel_id);
        }

        if (p_mpls_info->protection_path && p_mpls_info->protection_path->p_mpls_tunnel)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Protection Path's Tunnel Id = %d \n", p_mpls_info->protection_path->p_mpls_tunnel->tunnel_id);
        }

        if (CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
        {
            if ((CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH)
                || !CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
            && p_mpls_info->working_path.p_mpls_tunnel)
            {

                sys_greatbelt_nh_dump_dsl3edit_mpls4w(lchip, p_mpls_info->working_path.dsl3edit_offset, detail);

            }
            else if (CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN) && p_mpls_info->working_path.p_mpls_tunnel)
            {
                sys_greatbelt_nh_dump_dsl3edit_mpls4w(lchip, p_mpls_info->working_path.dsl3edit_offset, detail);

                if (p_mpls_info->protection_path)
                {
                    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "#####Protection Path's DsL3Edit\n");
                    sys_greatbelt_nh_dump_dsl3edit_mpls4w(lchip, p_mpls_info->protection_path->dsl3edit_offset, detail);
                }
            }
        }


        if (p_mpls_info->hdr.p_ref_nh_list)
        {
            sys_nh_ref_list_node_t* p_curr = NULL;
            sys_nh_info_ecmp_t* p_ref_nhinfo;

            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n------------------ECMP Information---------------------\n");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "The Nexthop was other ECMP refenerced:\n");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-16s %-16s %-10s\n", "ECMP NHID", "ecmp_cnt", "valid_cnt");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "--------------------------------------------------------\n");

            p_curr = p_mpls_info->hdr.p_ref_nh_list;

            while (p_curr)
            {
                p_ref_nhinfo = (sys_nh_info_ecmp_t*)p_curr->p_ref_nhinfo;
                if (p_ref_nhinfo)
                {
                    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-16d %-16d %-16d\n", p_ref_nhinfo->ecmp_nh_id, p_ref_nhinfo->ecmp_cnt, p_ref_nhinfo->valid_cnt);
                }

                p_curr = p_curr->p_next;
            }
        }
    }
    else
    {
        sal_sprintf(aps_group_str, "%s", "-");
        sal_sprintf(aps_path_str, "%s", "-");
        sal_sprintf(vc_label_str, "%s", "-");

        if (CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
        {

            cmd = DRV_IOR(DsL3EditMpls4W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_mpls_info->working_path.dsl3edit_offset, cmd, &dsl3edit));
            vc_label = dsl3edit.label0;
            tunnel_id = p_mpls_info->working_path.p_mpls_tunnel->tunnel_id;
            sal_sprintf(vc_label_str, "%d", vc_label);
            if (CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN)
                && !CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH))
            {
                sal_sprintf(aps_group_str, "%u", p_mpls_info->aps_group_id);
                cmd  = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_mpls_info->aps_group_id, cmd, &ds_aps_bridge));
                sal_sprintf(aps_path_str, "%s", ds_aps_bridge.protecting_en ? "N" : "Y");
            }

            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8d %-8s %-13d %-12s %-13s %s\n",
                           nhid, "MPLS", tunnel_id, vc_label_str, aps_group_str, aps_path_str);

            if (p_mpls_info->protection_path)
            {
                cmd = DRV_IOR(DsL3EditMpls4W_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_mpls_info->protection_path->dsl3edit_offset, cmd, &dsl3edit));

                vc_label = dsl3edit.label0;
                if (NULL != p_mpls_info->protection_path->p_mpls_tunnel)
                {
                    tunnel_id = p_mpls_info->protection_path->p_mpls_tunnel->tunnel_id;
                }
                sal_sprintf(aps_group_str, "%d", p_mpls_info->aps_group_id);
                sal_sprintf(vc_label_str, "%d", vc_label);
                sal_sprintf(aps_path_str, "%s", ds_aps_bridge.protecting_en ? "Y" : "N");
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9s%-9s%-14u%-13s%-14s%s\n",
                               "", "", tunnel_id, vc_label_str, aps_group_str, aps_path_str);
            }
        }
        else
        {
            if (NULL != p_mpls_info->working_path.p_mpls_tunnel)
            {
                tunnel_id = p_mpls_info->working_path.p_mpls_tunnel->tunnel_id;
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9d%-9s%-14u%-13s%-14s%s\n",
                               nhid, "MPLS", tunnel_id, "-", "-", "-");
            }
            else
            {
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9d%-9s%-14s%-13s%-14s%s\n",
                               nhid, "MPLS", "-", "-", "-", "-");
            }
        }

    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_get_tunnel_info(uint8 lchip, sys_nh_info_ip_tunnel_t * p_nhinfo, uint32 l2edit_offset, char * tunnel_type,char * ipsa, char * ipda, char * gre_key)
{
    uint32 l3edit_offset = p_nhinfo->dsl3edit_offset;
    ds_l3_edit_tunnel_v4_t  dsl3edit_v4 = {0};
    ds_l3_edit_tunnel_v6_t  dsl3edit_v6 = {0};
    ds_l2_edit_eth8_w_t   dsl2edit_8w = {0};
    ds_l3_edit_nat4_w_t  dsl3edit_nat = {0};
    ds_l3_edit_nat8_w_t  dsl3edit_8w = {0};
    uint32 cmd;
    uint8 flag = p_nhinfo->flag;
    uint32 tempip = 0;
    uint32 tbl_id = 0;
    ds_l3_tunnel_v4_ip_sa_t v4_sa = {0};
    ds_l3_tunnel_v6_ip_sa_t v6_sa = {0};
    uint32 gre_info;
    uint32 ipv6_address[4] = {0, 0, 0, 0};
    uint8 is_sa = 0;
    uint8 is_da = 0;

    sal_memset(&dsl2edit_8w, 0, sizeof(ds_l2_edit_eth8_w_t));
    sal_memcpy(tunnel_type,"-",sal_strlen("-"));
    sal_memcpy(ipsa,"-",sal_strlen("-"));
    sal_memcpy(ipda,"-",sal_strlen("-"));
    sal_memcpy(gre_key,"-",sal_strlen("-"));
    flag = flag & (SYS_NH_IP_TUNNEL_FLAG_IN_V4
        | SYS_NH_IP_TUNNEL_FLAG_IN_V6
        | SYS_NH_IP_TUNNEL_FLAG_IVI_6TO4
        | SYS_NH_IP_TUNNEL_FLAG_IVI_4TO6
        | SYS_NH_IP_TUNNEL_FLAG_NAT_V4);

    switch (flag)
    {
        case SYS_NH_IP_TUNNEL_FLAG_IN_V4:
        {
            cmd = DRV_IOR(DsL3EditTunnelV4_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3edit_offset, cmd, &dsl3edit_v4));

            if ( (17 == dsl3edit_v4.ip_protocol_type)
                || ( SYS_NH_IP_TUNNEL_IPV4_PROTO_TYPE == dsl3edit_v4.ip_protocol_type))
            {
                 /*CTC_TUNNEL_TYPE_IPV4_IN4,           // RFC 2003/2893: IPv4-in-IPv4 tunnel. */
                sal_memcpy(tunnel_type,"IPv4-in-IPv4",sal_strlen("IPv4-in-IPv4"));

                is_da = 1;
                is_sa = 1;
            }
            else if ( ( SYS_NH_IP_TUNNEL_IPV6_PROTO_TYPE == dsl3edit_v4.ip_protocol_type)
                && (1 != dsl3edit_v4.tunnel6_to4_da)
                && (0 == dsl3edit_v4.gre_key))
            {
                 /*CTC_TUNNEL_TYPE_IPV6_IN4,           // RFC 2003/2893: IPv6-in-IPv4 tunnel. */
                sal_memcpy(tunnel_type,"IPv6-in-IPv4",sal_strlen("IPv6-in-IPv4"));

                is_da = 1;
                is_sa = 1;
            }
            else if ( 1 == dsl3edit_v4.inner_header_valid )
            {
                 /*CTC_TUNNEL_TYPE_GRE_IN4,            // RFC 1701/2784/2890: GRE-in-IPv4  tunnel. */
                sal_memcpy(tunnel_type,"GRE-in-IPv4",sal_strlen("GRE-in-IPv4"));

                is_da = 1;
                is_sa = 1;

                if (0x1 == dsl3edit_v4.inner_header_type )
                {
                    gre_info = dsl3edit_v4.gre_key | (dsl3edit_v4.ipv4_sa_index & 0xFFFF0000);
                    sal_sprintf(gre_key,"%d",gre_info);
                }
            }
            else if ( 1 == dsl3edit_v4.isatp_tunnel )
            {
                 /*CTC_TUNNEL_TYPE_ISATAP,             // RFC 5214/5579 ,IPv6-in-IPv4 tunnel,ipv4 da copy ipv6 da(last 32bit) */
                sal_memcpy(tunnel_type,"ISATAP",sal_strlen("ISATAP"));
            }
            else if ( SYS_NH_IP_TUNNEL_IPV6_PROTO_TYPE == dsl3edit_v4.ip_protocol_type)
            {
                 /*CTC_TUNNEL_TYPE_6TO4,               // RFC 3056,IPv6-in-IPv4 tunnel,ipv4 da and sa copy ipv6 da and (middle 32bit) */
                 /*CTC_TUNNEL_TYPE_6RD,                // RFC 3056,IPv6-in-IPv4 tunnel,ipv4 da and sa copy ipv6 da and (middle 32bit) */
                sal_memcpy(tunnel_type,"6TO4_OR_6RD",sal_strlen("6TO4_OR_6RD"));

                if (0 == dsl3edit_v4.tunnel6_to4_sa)
                {
                    is_sa = 1;
                }

                if (0 == dsl3edit_v4.tunnel6_to4_da)
                {
                    is_da = 1;
                }
            }

            if (is_sa)
            {
                tbl_id = DsL3TunnelV4IpSa_t;
                cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, (dsl3edit_v4.ipv4_sa_index & 0x1F), cmd, &v4_sa));
                tempip = sal_ntohl(v4_sa.ip_sa);
                sal_inet_ntop(AF_INET, &tempip, ipsa, CTC_IPV6_ADDR_STR_LEN);
            }

            if (is_da)
            {
                tempip = sal_ntohl(dsl3edit_v4.ip_da);
                sal_inet_ntop(AF_INET, &tempip, ipda, CTC_IPV6_ADDR_STR_LEN);
            }
            break;
        }

        case SYS_NH_IP_TUNNEL_FLAG_IN_V6:
        {
            cmd = DRV_IOR(DsL3EditTunnelV6_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3edit_offset, cmd, &dsl3edit_v6));

            if ( (17 == dsl3edit_v6.ip_protocol_type)
                || ( SYS_NH_IP_TUNNEL_IPV4_PROTO_TYPE == dsl3edit_v6.ip_protocol_type))
            {
                 /*CTC_TUNNEL_TYPE_IPV4_IN6,           // RFC 2003/2893: IPv4-in-IPv6 tunnel. */
                sal_memcpy(tunnel_type,"IPv4-in-IPv6",sal_strlen("IPv4-in-IPv6"));
            }
            else if (SYS_NH_IP_TUNNEL_IPV6_PROTO_TYPE == dsl3edit_v6.ip_protocol_type)
            {
                 /*CTC_TUNNEL_TYPE_IPV6_IN6,           // RFC 2003/2893: IPv6-in-IPv6 tunnel. */
                sal_memcpy(tunnel_type,"IPv6-in-IPv6",sal_strlen("IPv6-in-IPv6"));
            }
            else if ( 1 == dsl3edit_v6.inner_header_valid)
            {
                 /*CTC_TUNNEL_TYPE_GRE_IN6,            // RFC 1701/2784/2890: GRE-in-IPv6  tunnel. */
                sal_memcpy(tunnel_type,"GRE-in-IPv6",sal_strlen("GRE-in-IPv6"));
                if (0x1 == dsl3edit_v6.inner_header_type)
                {
                    gre_info = dsl3edit_v6.gre_key15_0 | (dsl3edit_v6.gre_key31_16 << 16);
                    sal_sprintf(gre_key,"%d",gre_info);
                }
            }

            cmd = DRV_IOR(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, l2edit_offset, cmd, &dsl2edit_8w));

            ipv6_address[3] = sal_ntohl(dsl2edit_8w.ip_da31_0);
            ipv6_address[2] = sal_ntohl(dsl2edit_8w.ip_da63_32);
            ipv6_address[1] = sal_ntohl(dsl2edit_8w.ip_da78_64
                | (dsl2edit_8w.ip_da79_79 <<15)
                | (dsl3edit_v6.ip_da89_80<< 16)
                | ((dsl3edit_v6.ip_da101_90 &  0x3F ) << 26));
            ipv6_address[0] = sal_ntohl((dsl3edit_v6.ip_da101_90 >>6)
                | dsl3edit_v6.ip_da113_102 << 6
                | dsl3edit_v6.ip_da121_114 << 18
                | dsl3edit_v6.ip_da122_122 << 26
                | dsl3edit_v6.ip_da125_123 <<27
                | dsl3edit_v6.ip_da127_126 << 30);

            sal_inet_ntop(AF_INET6, ipv6_address, ipda, CTC_IPV6_ADDR_STR_LEN);

            tbl_id = DsL3TunnelV6IpSa_t;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (dsl3edit_v6.ip_sa_index & 0x1F), cmd, &v6_sa));
            ipv6_address[3] = sal_ntohl(v6_sa.ip_sa31_0);
            ipv6_address[2] = sal_ntohl(v6_sa.ip_sa63_32);
            ipv6_address[1] = sal_ntohl(v6_sa.ip_sa95_64);
            ipv6_address[0] = sal_ntohl(v6_sa.ip_sa127_96);
            sal_inet_ntop(AF_INET6, ipv6_address, ipsa, CTC_IPV6_ADDR_STR_LEN);
            break;
        }

        case SYS_NH_IP_TUNNEL_FLAG_IVI_6TO4:
        {
            sal_memcpy(tunnel_type,"IVI_6TO4",sal_strlen("IVI_6TO4"));
            break;
        }

        case SYS_NH_IP_TUNNEL_FLAG_IVI_4TO6:
        {
            sal_memcpy(tunnel_type,"IVI_4TO6",sal_strlen("IVI_4TO6"));

            cmd = DRV_IOR(DsL3EditNat8W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3edit_offset, cmd, &dsl3edit_8w));

            ipv6_address[3] = sal_ntohl(dsl3edit_8w.ip_da29_0 | dsl3edit_8w.ip_da31_30 << 30);
            ipv6_address[2] = sal_ntohl(dsl3edit_8w.ip_da39_32 | dsl3edit_8w.ip_da47_40 << 8
                | dsl3edit_8w.ip_da48 << 16 | dsl3edit_8w.ip_da63_49 << 17);
            ipv6_address[1] = sal_ntohl(dsl3edit_8w.ip_da95_64);
            ipv6_address[0] = sal_ntohl(dsl3edit_8w.ip_da119_96 | dsl3edit_8w.ip_da123_120 << 24
                | dsl3edit_8w.ip_da127_124 << 28);
            sal_inet_ntop(AF_INET6, ipv6_address, ipda, CTC_IPV6_ADDR_STR_LEN);
            break;
        }

        case SYS_NH_IP_TUNNEL_FLAG_NAT_V4:
        {
            sal_memcpy(tunnel_type,"NAT",sal_strlen("NAT"));

            cmd = DRV_IOR(DsL3EditNat4W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3edit_offset, cmd, &dsl3edit_nat));
            tempip = sal_ntohl(dsl3edit_nat.ip_da29_0 | dsl3edit_nat.ip_da31_30 << 30);
            sal_inet_ntop(AF_INET, &tempip, ipda, CTC_IPV6_ADDR_STR_LEN);

            break;
        }
    }
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_ip_tunnel_brief(uint8 lchip, uint32 nhid,sys_nh_info_ip_tunnel_t * p_nhinfo)
{
    uint32 cmd;
    uint16 gport = 0;
    int32 ret = CTC_E_NONE;
    sys_l3if_prop_t l3if_prop;
    ds_fwd_t dsfwd;
    char tunnel_type[32] = {0};
    char ipda[CTC_IPV6_ADDR_STR_LEN] = {0};
    char ipsa[CTC_IPV6_ADDR_STR_LEN] = {0};
    char gre_key[40] = {0};
    uint8 lport = 0;
    uint32 dsl2edit_offset = 0;
    ds_l2_edit_eth4_w_t  dsl2edit;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8d", nhid);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s", "IP-Tunnel");

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh8w(lchip, p_nhinfo->hdr.dsfwd_info.dsnh_offset, 0, &dsl2edit_offset));
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh(lchip, p_nhinfo->hdr.dsfwd_info.dsnh_offset, 0, &dsl2edit_offset));
    }

    sys_greatbelt_nh_dump_get_tunnel_info(lchip, p_nhinfo,dsl2edit_offset,tunnel_type,ipsa,ipda,gre_key);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s",tunnel_type);

    CTC_ERROR_RETURN(sys_greatbelt_internal_port_get_rsv_port(lchip, SYS_INTERNAL_PORT_TYPE_IP_TUNNEL, &lport))

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9s","-");
    }
    else if (CTC_FLAG_ISSET(p_nhinfo->flag, SYS_NH_IP_TUNNEL_REROUTE))
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9s", "RE-ROUTE");
    }
    else if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
        ret = DRV_IOCTL(lchip, p_nhinfo->hdr.dsfwd_info.dsfwd_offset, cmd, &dsfwd);
        if (CTC_E_NONE != ret)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9s","-");
        }
        gport = SYS_GREATBELT_DESTMAP_TO_GPORT(dsfwd.dest_map);
        if (lport == gport)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9s","RE-ROUTE");
        }
        else
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"0x%04x   ", gport);
        }
    }
    else
    {
        if (lport == p_nhinfo->gport)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9s","RE-ROUTE");
        }
        else
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"0x%04x   ", p_nhinfo->gport);
        }
    }

    l3if_prop.l3if_id = p_nhinfo->l3ifid;
    ret = sys_greatbelt_l3if_get_l3if_info(lchip, 1, &l3if_prop);
    if (CTC_L3IF_TYPE_PHY_IF == l3if_prop.l3if_type || (CTC_E_NONE != ret))
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8s","-");
    }
    else
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8d",l3if_prop.vlan_id);
    }

    if (CTC_E_NONE != ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8s","-");
    }
    else
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8d", p_nhinfo->l3ifid);
    }

    cmd = DRV_IOR(DsL2EditEth4W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, dsl2edit_offset, cmd, &dsl2edit));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%04x.%04x.%04x ",dsl2edit.mac_da47_32&0xFFFF,
        ((dsl2edit.mac_da31_30 << 30) | dsl2edit.mac_da29_0)>>16,
        ((dsl2edit.mac_da31_30 << 30) | dsl2edit.mac_da29_0)&0xFFFF);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-18s", ipsa);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-18s", ipda);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s", gre_key);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_ip_tunnel(uint8 lchip, uint32 nhid, bool detail)
{
    sys_nh_info_ip_tunnel_t* p_ip_tunnel_info;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    int32 ret;
    uint32 dsl2edit_offset = 0;

    CTC_ERROR_RETURN(sys_greatbelt_nh_debug_get_nh_master(lchip, &p_gb_nh_master));

    ret = (sys_greatbelt_nh_debug_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo));
    if (CTC_E_NH_NOT_EXIST == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "  IP tunnel nexthop %d not exist\n", nhid);
        return CTC_E_NONE;
    }
    else if (CTC_E_NH_INVALID_NHID == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Invalid nexthopId %d\n", nhid);
        return CTC_E_NONE;
    }
    else
    {
        CTC_ERROR_RETURN(ret);
    }

    if (SYS_NH_TYPE_IP_TUNNEL != p_nhinfo->hdr.nh_entry_type)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Isn't IP tunnel nexthop for nexthopId %d\n", nhid);
        return CTC_E_INVALID_PARAM;
    }

    p_ip_tunnel_info = (sys_nh_info_ip_tunnel_t*)(p_nhinfo);

    if (detail)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, SYS_NH_DEBUG_TYPE_HEAD("IP tunnel"));
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " EntryType = %d, EntryFlags = 0x%x\n",
                       p_nhinfo->hdr.nh_entry_type, p_nhinfo->hdr.nh_entry_flags);


        /*Dump dsFwd*/
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "   <<< Dump DsFwd, Offset = %d, Lchip = %d>>>\n",
                       p_ip_tunnel_info->hdr.dsfwd_info.dsfwd_offset, lchip);
        CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsfwd(lchip, p_ip_tunnel_info->hdr.dsfwd_info.dsfwd_offset, detail));
        /*Dump Dsnh & DsL2Edit*/

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    L3 interface Id is %d  gport = 0x%x\n", p_ip_tunnel_info->l3ifid, p_ip_tunnel_info->gport);
        if (CTC_FLAG_ISSET(p_ip_tunnel_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh8w(lchip, p_ip_tunnel_info->hdr.dsfwd_info.dsnh_offset, detail, &dsl2edit_offset));
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh(lchip, p_ip_tunnel_info->hdr.dsfwd_info.dsnh_offset, detail, &dsl2edit_offset));
        }

        if (CTC_FLAG_ISSET(p_ip_tunnel_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT))
        {
            if (p_ip_tunnel_info->flag & SYS_NH_IP_TUNNEL_FLAG_L2EDIT8W)
            {
                sys_greatbelt_nh_dump_dsl2edit_eth8w(lchip, dsl2edit_offset, detail);
            }
            else
            {
                sys_greatbelt_nh_dump_dsl2edit(lchip, dsl2edit_offset, detail);
            }
        }
        else
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    Packet will re-route with new tunnel header\n");
        }

        if (CTC_FLAG_ISSET(p_ip_tunnel_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
        {
            if (p_ip_tunnel_info->flag & SYS_NH_IP_TUNNEL_FLAG_IN_V4)
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsl3edit_tunnel_v4(lchip,
                                                                          p_ip_tunnel_info->dsl3edit_offset, detail));
            }
            else if (p_ip_tunnel_info->flag & SYS_NH_IP_TUNNEL_FLAG_IN_V6)
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsl3edit_tunnel_v6(lchip,
                                                                          p_ip_tunnel_info->dsl3edit_offset, detail));
            }
            else if (p_ip_tunnel_info->flag & SYS_NH_IP_TUNNEL_FLAG_IVI_6TO4)
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsl3edit_tunnel_ivi_6to4(lchip,
                                                                                p_ip_tunnel_info->dsl3edit_offset, detail));
            }
            else if (p_ip_tunnel_info->flag & SYS_NH_IP_TUNNEL_FLAG_IVI_4TO6)
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsl3edit_tunnel_ivi_4to6(lchip,
                                                                                p_ip_tunnel_info->dsl3edit_offset, detail));
            }
            else if (p_ip_tunnel_info->flag & SYS_NH_IP_TUNNEL_FLAG_NAT_V4)
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsl3edit_tunnel_nat_v4(lchip,
                                                                              p_ip_tunnel_info->dsl3edit_offset, detail));
            }
        }
        else
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    No DsL3EditTunnel Table on local chip %d\n", lchip);
        }


        if (p_ip_tunnel_info->hdr.p_ref_nh_list)
        {
            sys_nh_ref_list_node_t* p_curr = NULL;
            sys_nh_info_ecmp_t* p_ref_nhinfo;

            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n------------------ECMP Information---------------------\n");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "The Nexthop was other ECMP refenerced:\n");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-16s %-16s %-10s\n", "ECMP NHID", "ecmp_cnt", "valid_cnt");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "--------------------------------------------------------\n");

            p_curr = p_ip_tunnel_info->hdr.p_ref_nh_list;

            while (p_curr)
            {
                p_ref_nhinfo = (sys_nh_info_ecmp_t*)p_curr->p_ref_nhinfo;
                if (p_ref_nhinfo)
                {
                    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-16d %-16d %-16d\n", p_ref_nhinfo->ecmp_nh_id, p_ref_nhinfo->ecmp_cnt, p_ref_nhinfo->valid_cnt);
                }

                p_curr = p_curr->p_next;
            }
        }

    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_dump_ip_tunnel_brief(lchip, nhid,p_ip_tunnel_info));
    }
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_mpls_tunnel(uint8 lchip, uint16 tunnel_id, bool detail)
{

    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel;
    uint32 cmd;
    uint16 aps_group_id = 0;
    uint32 w_interface_id = 0;
    uint32 p_interface_id = 0;
    uint32 w_gport = 0;
    uint32 p_gport = 0;
    char vc_label_str[10] = {0};
    DUMP_L2EDIT dump_l2edit = NULL;
    ds_l2_edit_eth8_w_t  dsl2edit;
    ds_l3_edit_mpls8_w_t dsl3editMpls8w;
    ds_aps_bridge_t ds_aps_bridge;
    sal_memset(&dsl2edit, 0, sizeof(ds_l2_edit_eth8_w_t));
    sal_memset(&dsl3editMpls8w, 0, sizeof(ds_l3_edit_mpls8_w_t));
    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge_t));
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_mpls_tunnel(lchip, tunnel_id, &p_mpls_tunnel));

    if (!p_mpls_tunnel)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% MPLS Tunnel is not exist \n");
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (FALSE == detail)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "TUNNEL_ID     REF_CN     DESTPORT     L3IFID        MAC_DA       LSP     SPME     ACTIVE\n");
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "----------------------------------------------------------------------------------------\n");
    }


    if (TRUE == detail)
    {
        if (p_mpls_tunnel->p_dsl2edit)
        {
            dump_l2edit = sys_greatbelt_nh_dump_dsl2edit;
        }
        else
        {
            dump_l2edit = sys_greatbelt_nh_dump_dsl2edit_eth8w;
        }
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "tunnel_id = %d,ref_cnt = %d \n", tunnel_id, p_mpls_tunnel->ref_cnt);
        if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "The tunnel APS enable :APS group Id :%d\n", p_mpls_tunnel->gport);
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "#####Working Path's DsL2Edit\n");
            CTC_ERROR_RETURN(dump_l2edit(lchip, p_mpls_tunnel->dsl2edit_offset, detail));
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "#####Protection Path's DsL2Edit\n");
            CTC_ERROR_RETURN(dump_l2edit(lchip, p_mpls_tunnel->p_dsl2edit_offset, detail));
        }
        else
        {
            CTC_ERROR_RETURN(dump_l2edit(lchip, p_mpls_tunnel->dsl2edit_offset, detail));
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Output port = %d ,l3if = %d\n", p_mpls_tunnel->gport, p_mpls_tunnel->l3ifid);
        }
    }
    else
    {
        w_gport = p_mpls_tunnel->gport;
        w_interface_id = p_mpls_tunnel->l3ifid;
        sal_sprintf(vc_label_str, "%s", "-");
        if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
        {
            aps_group_id = p_mpls_tunnel->gport;
            cmd = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, aps_group_id, cmd, &ds_aps_bridge));
            w_gport = SYS_GREATBELT_DESTMAP_TO_GPORT(ds_aps_bridge.working_dest_map);
            p_gport = SYS_GREATBELT_DESTMAP_TO_GPORT(ds_aps_bridge.protecting_dest_map);
            if (!SYS_GCHIP_IS_REMOTE(lchip, SYS_DRV_GPORT_TO_GCHIP(ds_aps_bridge.working_dest_map)))
            {
                cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_InterfaceId_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_DRV_PORT_TO_CTC_LPORT(ds_aps_bridge.working_dest_map), cmd, &w_interface_id));
            }
            if (!SYS_GCHIP_IS_REMOTE(lchip, SYS_DRV_GPORT_TO_GCHIP(ds_aps_bridge.protecting_dest_map)))
            {
                cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_InterfaceId_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_DRV_PORT_TO_CTC_LPORT(ds_aps_bridge.protecting_dest_map), cmd, &p_interface_id));
            }
        }

        cmd = DRV_IOR(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_mpls_tunnel->dsl2edit_offset, cmd, &dsl2edit));

        sal_memcpy((uint8*)&dsl3editMpls8w + sizeof(ds_l3_edit_mpls4_w_t),
                   (uint8*)&dsl2edit + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));
        if (dsl3editMpls8w.label_valid3)
        {
            sal_sprintf(vc_label_str, "%d", dsl3editMpls8w.label3);
        }

        /* dump mpls tunnel in brief */
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-13d %-10d ",
                       tunnel_id, p_mpls_tunnel->ref_cnt);

        /* work path */
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "0x%.4x       %-10d ",
                       w_gport, w_interface_id);


        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%04x.%04x.%04x  ",
                       dsl2edit.mac_da47_32, ((dsl2edit.mac_da31_30 << 14) | (dsl2edit.mac_da29_0 >> 16)), (dsl2edit.mac_da29_0 & 0xFFFF));


        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-7d ", dsl3editMpls8w.label2);
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9s", vc_label_str);

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-5s\n",
                       ds_aps_bridge.protecting_en ? "N" : "Y");

        /* protection path */
        if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
        {
            cmd = DRV_IOR(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_mpls_tunnel->p_dsl2edit_offset, cmd, &dsl2edit));

            sal_memcpy((uint8*)&dsl3editMpls8w + sizeof(ds_l3_edit_mpls4_w_t),
                       (uint8*)&dsl2edit + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));

            if (dsl3editMpls8w.label_valid3)
            {
                sal_sprintf(vc_label_str, "%d", dsl3editMpls8w.label3);
            }
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "                         ");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "0x%.4x       %-10d ",  p_gport, p_interface_id);

            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%04x.%04x.%04x  ",
                           dsl2edit.mac_da47_32, ((dsl2edit.mac_da31_30 << 14) | (dsl2edit.mac_da29_0 >> 16)), (dsl2edit.mac_da29_0 & 0xFFFF));

            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-7d ", dsl3editMpls8w.label2);
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9s", vc_label_str);
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-5s\n", ds_aps_bridge.protecting_en ? "Y" : "N");
        }

    }



    return CTC_E_NONE;
}



int32
sys_greatbelt_nh_dump_arp(uint8 lchip, uint16 arp_id, bool detail)
{

    sys_nh_db_arp_t* p_arp = NULL;

    ds_l2_edit_eth8_w_t  dsl2edit;
    sal_memset(&dsl2edit, 0, sizeof(ds_l2_edit_eth8_w_t));
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_arp_id(lchip, arp_id, &p_arp));

    if (!p_arp)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% ARP Id is not exist \n");
        return CTC_E_ENTRY_NOT_EXIST;
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ARP_ID     ACTION      MAC_DA         VLAN     \n");
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "------------------------------------------------\n");
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-10d %-10s ",
                   arp_id,     (CTC_FLAG_ISSET(p_arp->flag, SYS_NH_ARP_FLAG_DROP) ? "Discard" :
                            ((CTC_FLAG_ISSET(p_arp->flag, SYS_NH_ARP_FLAG_REDIRECT_TO_CPU)) ? "TO CPU" : "FWD")));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%04x.%04x.%04x  ",
                            (p_arp->mac_da[0] << 8 | p_arp->mac_da[1]), (p_arp->mac_da[2] << 8 | p_arp->mac_da[3]),(p_arp->mac_da[4] << 8 | p_arp->mac_da[5]));
    if(CTC_FLAG_ISSET(p_arp->flag, SYS_NH_ARP_VLAN_VALID))
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-12d \n",
                   p_arp->output_vid);
    }
    else
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-12s \n",
                       "-");
    }


    if (TRUE == detail)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\nDetail Table offset \n");
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------------------------------\n");

        if (CTC_FLAG_ISSET(p_arp->flag, SYS_NH_ARP_FLAG_REDIRECT_TO_CPU))
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " --%-32s :%u \n", "DsL2EditLoopback", p_arp->offset);
        }
        else
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " --%-32s :%u \n", "DsL2EditEth4W", p_arp->offset);
        }


    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_nh_dump_misc(uint8 lchip, uint32 nhid, bool detail)
{
    sys_nh_info_misc_t* p_nh_flex;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    int32 ret;

    CTC_ERROR_RETURN(sys_greatbelt_nh_debug_get_nh_master(lchip, &p_gb_nh_master));

    ret = (sys_greatbelt_nh_debug_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo));
    if (CTC_E_NH_NOT_EXIST == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "  Misc nexthop %d not exist\n", nhid);
        return CTC_E_NONE;
    }
    else if (CTC_E_NH_INVALID_NHID == ret)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Invalid nexthopId %d\n", nhid);
        return CTC_E_NONE;
    }
    else
    {
        CTC_ERROR_RETURN(ret);
    }

    if (SYS_NH_TYPE_MISC != p_nhinfo->hdr.nh_entry_type)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Isn't misc nexthop for nexthopId %d\n", nhid);
        return CTC_E_INVALID_PARAM;
    }

    p_nh_flex = (sys_nh_info_misc_t*)(p_nhinfo);

    if (detail)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, SYS_NH_DEBUG_TYPE_HEAD("Misc"));
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "gport:0x%x EntryType = %d, EntryFlags = 0x%x\n", p_nh_flex->gport,
                       p_nhinfo->hdr.nh_entry_type, p_nhinfo->hdr.nh_entry_flags);

         /*Dump dsFwd*/
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "   <<< Dump DsFwd, Offset = %d, Lchip = %d>>>\n",
                       p_nh_flex->hdr.dsfwd_info.dsfwd_offset, lchip);
        CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsfwd(lchip, p_nh_flex->hdr.dsfwd_info.dsfwd_offset, detail));

        /*Dump Dsnh & DsL2Edit*/
        if (p_nh_flex->hdr.dsnh_entry_num)
        {
            if (CTC_FLAG_ISSET(p_nh_flex->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh8w(lchip, p_nh_flex->hdr.dsfwd_info.dsnh_offset, detail, NULL));
            }
            else
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_dump_dsnh(lchip, p_nh_flex->hdr.dsfwd_info.dsnh_offset, detail, NULL));
            }
        }

        if (CTC_FLAG_ISSET(p_nh_flex->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT))
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "    L2Editptr is %d type is L2EDIT_MAC_SWAP\n", p_nh_flex->dsl2edit_offset);

        }

    }
    else
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8d", nhid);
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s\n", "MISC");
    }
    return CTC_E_NONE;
}

void
sys_greatbelt_nh_dump_head_by_type(uint8 lchip, uint32 nhid, sys_greatbelt_nh_type_t nh_type,bool detail, bool is_all, bool is_head)
{
    switch (nh_type)
    {
    case SYS_NH_TYPE_NULL:
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "NHID is %d, Invalid Nexthop\n", nhid);
        break;

    case SYS_NH_TYPE_MCAST:
        if ( (!is_all || is_head ) && !detail)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8s%-15s%s\n","NHID","TYPE","GROUP-ID");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "--------------------------------------\n");
        }
        else if (detail && !is_head)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "NHID is %d, Mcast Nexthop\n", nhid);
        }
        break;

    case SYS_NH_TYPE_BRGUC:
        if ( (!is_all || is_head ) && !detail)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8s%-15s%s\n","NHID","TYPE","DESTPORT");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "--------------------------------------\n");
        }
        else if (detail && !is_head)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "NHID is %d, Bridge Unicast Nexthop\n", nhid);
        }
        break;

    case SYS_NH_TYPE_IPUC:
        if ( (!is_all || is_head ) && !detail)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8s%-15s%-13s%-9s%-10s%s\n","NHID","TYPE","DESTPORT","VLAN","L3IFID","MACDA");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "----------------------------------------------------------------------------\n");
        }
        else if (detail && !is_head)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "NHID is %d, IP Unicast Nexthop\n", nhid);
        }
        break;

    case SYS_NH_TYPE_MPLS:
        if ( (!is_all || is_head ) && !detail)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "NHID     TYPE     TUNNEL_ID     VC_LABEL     APS_GROUP     W\n");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "------------------------------------------------------------\n");
        }
        else if (detail && !is_head)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "NHID is %d, MPLS Nexthop\n", nhid);
        }


        break;

    case SYS_NH_TYPE_ECMP:
        if ( (!is_all || is_head ) && !detail)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8s%-15s\n","NHID","TYPE");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------\n");
        }
        else if (detail && !is_head)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "NHID is %d, ECMP Nexthop\n", nhid);
        }
        break;

    case SYS_NH_TYPE_ILOOP:
        if ( (!is_all || is_head ) && !detail)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8s%-15s%s\n","NHID","TYPE","LPBK-LPORT");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "----------------------------------------\n");
        }
        else if (detail && !is_head)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "NHID is %d, I-Loop Nexthop\n", nhid);
        }
        break;

    case SYS_NH_TYPE_IP_TUNNEL:
        if ( (!is_all || is_head ) && !detail)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8s%-15s%-15s%-9s%-8s%-8s%-15s%-18s%-18s%s\n",
                "NHID","TYPE","TUNNEL-TYPE","DESTPORT","VLAN","L3IFID","MACDA","IPSA","IPDA","GRE-KEY");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,
                "--------------------------------------------------------------------------------------------------------------------------\n");
        }
        else if (detail && !is_head)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "NHID is %d, IP Tunnel Nexthop\n", nhid);
        }
        break;

    case SYS_NH_TYPE_RSPAN:
        if ( (!is_all || is_head ) && !detail)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8s%-15s%s\n","NHID","TYPE","VLAN");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "------------------------------------\n");
        }
        else if (detail && !is_head)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "NHID is %d, RSPAN Nexthop\n", nhid);
        }
        break;

    case SYS_NH_TYPE_MISC:
        if ( (!is_all || is_head ) && !detail)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8s%-15s\n","NHID","TYPE");
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------\n");
        }
        else if (detail && !is_head)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "NHID is %d, Misc Nexthop\n", nhid);
        }
        break;

    default:
        return;
    }

    return;
}

void
sys_greatbelt_nh_dump_by_type(uint8 lchip, uint32 nhid, sys_greatbelt_nh_type_t nh_type, bool detail, bool is_all)
{
    if (detail)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
    }


    sys_greatbelt_nh_dump_head_by_type(lchip, nhid, nh_type, detail, is_all,FALSE);

    switch (nh_type)
    {
    case SYS_NH_TYPE_MCAST:
        sys_greatbelt_nh_dump_mcast(lchip, nhid, detail, is_all);
        break;

    case SYS_NH_TYPE_BRGUC:
        sys_greatbelt_nh_dump_brguc(lchip, nhid, detail);
        break;

    case SYS_NH_TYPE_IPUC:
        sys_greatbelt_nh_dump_ipuc(lchip, nhid, detail);
        break;

    case SYS_NH_TYPE_MPLS:
        sys_greatbelt_nh_dump_mpls(lchip, nhid, detail);
        break;

    case SYS_NH_TYPE_ECMP:
        sys_greatbelt_nh_dump_ecmp(lchip, nhid, detail, is_all);
        break;

    case SYS_NH_TYPE_DROP:
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "NHID is %d, Drop Nexthop\n", nhid);
        break;

    case SYS_NH_TYPE_TOCPU:
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "NHID is %d, ToCPU Nexthop\n", nhid);
        break;

    case SYS_NH_TYPE_UNROV:
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "NHID is %d, Unresolved Nexthop\n", nhid);
        break;


    case SYS_NH_TYPE_ILOOP:
        sys_greatbelt_nh_dump_iloop(lchip, nhid, detail);
        break;

    case SYS_NH_TYPE_IP_TUNNEL:
        sys_greatbelt_nh_dump_ip_tunnel(lchip, nhid, detail);
        break;

    case SYS_NH_TYPE_ELOOP:
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "NHID is %d, E-Loop Nexthop\n", nhid);
        break;

    case SYS_NH_TYPE_RSPAN:
        sys_greatbelt_nh_dump_rspan(lchip, nhid, detail);
        break;

    case SYS_NH_TYPE_MISC:
        sys_greatbelt_nh_dump_misc(lchip, nhid, detail);
        break;

    default:
        return;
    }

    return;
}

int32
sys_greatbelt_nh_dump(uint8 lchip, uint32 nhid, bool detail)
{
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    sys_nh_info_com_t* p_nhinfo = NULL;
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_debug_get_nh_master(lchip, &p_gb_nh_master));
    CTC_ERROR_RETURN(sys_greatbelt_nh_debug_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo));
    if (p_nhinfo)
    {
        sys_greatbelt_nh_dump_by_type(lchip, nhid, p_nhinfo->hdr.nh_entry_type, detail,FALSE);
    }
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_all(uint8 lchip, sys_greatbelt_nh_type_t nh_type, bool detail)
{
    uint32 nhid = 0;
    sys_greatbelt_nh_master_t* p_gb_nh_master = NULL;
    sys_nh_info_com_t* p_nhinfo = NULL;
    int32 ret;
    uint32 start_type = 0;
    uint32 end_type = 0;
    uint32 loop = 0;
    uint8 is_print = 0;
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_debug_get_nh_master(lchip, &p_gb_nh_master));

    start_type = (SYS_NH_TYPE_MAX == nh_type) ? SYS_NH_TYPE_NULL : nh_type;
    end_type   = (SYS_NH_TYPE_MAX == nh_type) ? (SYS_NH_TYPE_MAX - 1): nh_type;

    for(loop = start_type ; loop <= end_type ; loop ++)
    {
        is_print = 0;
        for (nhid = 0; nhid < SYS_NH_INTERNAL_NHID_MAX; nhid++)
        {
            ret = (sys_greatbelt_nh_debug_get_nhinfo_by_nhid(lchip, nhid, p_gb_nh_master, &p_nhinfo));
            if (CTC_E_NH_NOT_EXIST == ret)
            {
                continue;
            }

            if (p_nhinfo && (p_nhinfo->hdr.nh_entry_type == loop ))
            {
                if (!is_print)
                {
                    sys_greatbelt_nh_dump_head_by_type(lchip, nhid, loop,detail,TRUE,TRUE);
                    is_print = 1;
                }
                sys_greatbelt_nh_dump_by_type(lchip, nhid, p_nhinfo->hdr.nh_entry_type, detail, TRUE);
            }
        }
        if (is_print)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
        }
    }
    return CTC_E_NONE;
}
