/**
 @file sys_greatbelt_nexthop_misc.c

 @date 2009-11-25

 @version v2.0

 The file contains all non-layer2 and non-layer3 nexthop related callback function
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
#include "ctc_cpu_traffic.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_queue_enq.h"

#include "sys_greatbelt_cpu_reason.h"
#include "sys_greatbelt_stats.h"

#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_tbl_reg.h"
/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
#define SYS_NH_ILOOP_MAX_REMOVE_WORDS    31

#define SYS_NH_MISC_FLAG_CONFLICT_CHECK(flag) \
    { \
        if (flag & CTC_MISC_NH_L2_EDIT_SWAP_MAC) \
        { \
            if ((flag & ~CTC_MISC_NH_L2_EDIT_SWAP_MAC) > 0) \
            { \
                return CTC_E_INVALID_PARAM; \
            } \
        } \
    }

#define SYS_NH_MAX_CPU_RESEAON_ID    4096

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/

/****************************************************************************
 *
* Function
*
*****************************************************************************/
/**
 @brief Callback function of create unicast bridge nexthop

 @param[in,out] p_com_nh_para, parameters used to create bridge nexthop,
                writeback dsfwd offset array to this param

 @param[out] p_com_db, pointer used to store nexthop data

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_create_special_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para, sys_nh_info_com_t* p_com_db)
{
    sys_nh_param_special_t* p_nh_para;
    sys_nh_info_special_t* p_nhdb;
    uint32 dsfwd_offset;
    uint8 gchip;
    sys_nh_param_dsfwd_t dsfwd_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    /* 1. sanity check & init */
    CTC_PTR_VALID_CHECK(p_com_nh_para);
    CTC_PTR_VALID_CHECK(p_com_db);
    p_nh_para = (sys_nh_param_special_t*)p_com_nh_para;
    p_nhdb = (sys_nh_info_special_t*)p_com_db;
    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));

    switch (p_nh_para->hdr.nh_param_type)
    {
    case SYS_NH_TYPE_UNROV:
    case SYS_NH_TYPE_DROP:
        dsfwd_param.drop_pkt = TRUE;
        break;

    case SYS_NH_TYPE_TOCPU:
        dsfwd_param.is_to_cpu = 1;
        break;

    default:
        CTC_ERROR_RETURN(CTC_E_INVALID_PARAM);
    }

    p_nhdb->hdr.nh_entry_type =  \
    (p_nhdb->hdr.nh_entry_type == SYS_NH_TYPE_NULL) ? (p_nh_para->hdr.nh_param_type) : p_nhdb->hdr.nh_entry_type;


    /*Build DsFwd Table*/
    if (!CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,
                                                       &dsfwd_offset));
        p_nhdb->hdr.dsfwd_info.dsfwd_offset = dsfwd_offset;
    }
    sys_greatbelt_get_gchip_id(lchip, &gchip);
    if (dsfwd_param.is_to_cpu)
    {
        sys_greatbelt_get_gchip_id(lchip, &gchip);
    }
    dsfwd_param.dest_chipid = gchip;
    /*Writeback dsfwd offset*/
    dsfwd_param.dsfwd_offset = p_nhdb->hdr.dsfwd_info.dsfwd_offset;

    dsfwd_param.is_mcast = FALSE;
    if (SYS_NH_TYPE_TOCPU == p_nh_para->hdr.nh_param_type)
    {
        dsfwd_param.dsnh_offset = CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_FWD_CPU, 0);
        p_nhdb->hdr.dsfwd_info.dsnh_offset = dsfwd_param.dsnh_offset;
    }

    if (p_nh_para->hdr.stats_valid)
    {
        dsfwd_param.stats_ptr = p_nh_para->hdr.stats_ptr;
    }

    /*Write table*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param));

    CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_delete_special_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid)
{
    sys_nh_info_special_t* nh_info;
    sys_nh_param_dsfwd_t dsfwd_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_data);
    nh_info = (sys_nh_info_special_t*)(p_data);

    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));

    /*Free dsfwd offset*/

    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, \
    SYS_NH_ENTRY_TYPE_FWD, 1, nh_info->hdr.dsfwd_info.dsfwd_offset));

    dsfwd_param.dsfwd_offset = nh_info->hdr.dsfwd_info.dsfwd_offset;
    dsfwd_param.drop_pkt = 1;
    /*Write table*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param));


    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_create_iloop_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                 sys_nh_info_com_t* p_com_db)
{
    sys_nh_param_iloop_t* p_nh_para;
    sys_nh_info_special_t* p_nhdb;
    uint32 dsfwd_offset;
    uint8 gchip;
    sys_nh_param_dsfwd_t dsfwd_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    /* 1. sanity check & init */
    CTC_PTR_VALID_CHECK(p_com_nh_para);
    CTC_PTR_VALID_CHECK(p_com_db);
    p_nh_para = (sys_nh_param_iloop_t*)p_com_nh_para;
    CTC_PTR_VALID_CHECK(p_nh_para->p_iloop_param);
    if (p_nh_para->p_iloop_param->words_removed_from_hdr > SYS_NH_ILOOP_MAX_REMOVE_WORDS)
    {
        return CTC_E_INVALID_PARAM;
    }

    p_nhdb = (sys_nh_info_special_t*)p_com_db;
    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));
    p_nhdb->hdr.nh_entry_type = (p_nh_para->hdr.nh_param_type);
    dsfwd_param.dest_id = SYS_RESERVE_PORT_ID_ILOOP;
    dsfwd_param.dsnh_offset = SYS_NH_ENCODE_ILOOP_DSNH(
            p_nh_para->p_iloop_param->lpbk_lport,
            (p_nh_para->p_iloop_param->logic_port ? \
             1 : 0), p_nh_para->p_iloop_param->customerid_valid,
            p_nh_para->p_iloop_param->words_removed_from_hdr);

    /* decide nexthop ext */
    if(p_nh_para->p_iloop_param->customerid_valid
        || p_nh_para->p_iloop_param->words_removed_from_hdr)
    {
        dsfwd_param.nexthop_ext = 1;
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
    }

    if(p_nh_para->p_iloop_param->inner_packet_type_valid)
    {
        CTC_BIT_SET(dsfwd_param.dsnh_offset, 11);
        dsfwd_param.dsnh_offset &= ~(0x7000);
        dsfwd_param.dsnh_offset |= (p_nh_para->p_iloop_param->inner_packet_type << 12);
    }

    sys_greatbelt_get_gchip_id(lchip, &gchip);
    p_nhdb->dest_gport = CTC_MAP_LPORT_TO_GPORT(gchip, SYS_RESERVE_PORT_ID_ILOOP);

    if (p_nh_para->p_iloop_param->sequence_en)
    {
        dsfwd_param.sequence_chk_en = TRUE;
        dsfwd_param.stats_ptr = p_nh_para->p_iloop_param->sequence_counter_index;
    }


    /*Build DsFwd Table*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,
                                                   &dsfwd_offset));
    CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
    p_nhdb->hdr.dsfwd_info.dsfwd_offset = dsfwd_offset;

    p_nhdb->hdr.dsfwd_info.dsnh_offset = dsfwd_param.dsnh_offset;

    sys_greatbelt_get_gchip_id(lchip, &gchip);
    dsfwd_param.dest_chipid = gchip;
    /*Writeback dsfwd offset*/
    dsfwd_param.dsfwd_offset = p_nhdb->hdr.dsfwd_info.dsfwd_offset;

    dsfwd_param.is_mcast = FALSE;
    dsfwd_param.service_queue_en = p_nh_para->p_iloop_param->service_queue_en;
    dsfwd_param.bypass_ingress_edit = 1;
    /*Write table*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param));


    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_delete_iloop_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid)
{
    sys_nh_info_special_t* nh_info;
    sys_nh_param_dsfwd_t dsfwd_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_data);
    nh_info = (sys_nh_info_special_t*)(p_data);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_ILOOP, p_data->hdr.nh_entry_type);

    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));

    /*Free dsfwd offset*/

    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, \
    SYS_NH_ENTRY_TYPE_FWD, 1, nh_info->hdr.dsfwd_info.dsfwd_offset));

    dsfwd_param.dsfwd_offset = nh_info->hdr.dsfwd_info.dsfwd_offset;
    dsfwd_param.drop_pkt = 1;
    /*Write table*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param));


    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_update_iloop_cb(uint8 lchip, sys_nh_info_com_t* p_iloop_info, sys_nh_param_com_t* p_iloop_param)
{

    return CTC_E_NONE;
}

#if 0
/**
 @brief This function is used to config  crosschip rspan dsnexthop
 */
STATIC int32
_sys_greatbelt_nh_set_crosschip_rspan(uint8 lchip, sys_nh_param_dsnh_t* p_dsnh_param)
{

    uint32 cmd;
    ds_next_hop4_w_t dsnh;
    ds_next_hop8_w_t dsnh8w;

    sal_memset(&dsnh, 0, sizeof(ds_next_hop4_w_t));
    sal_memset(&dsnh8w, 0, sizeof(ds_next_hop8_w_t));

    dsnh.vlan_xlate_mode = 1;    /*greatbelt mode*/
    dsnh.tagged_mode = 1;
    dsnh.derive_stag_cos = 1;  /*restore vlantag for mirror*/
    dsnh.replace_ctag_cos = 1; /*add  rspan vlan id*/
    dsnh.output_svlan_id_valid = 1;
    dsnh.l3_edit_ptr14_0 = p_dsnh_param->p_vlan_info->output_svid & 0xFFF;
    dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
    dsnh.payload_operation = SYS_NH_OP_MIRROR;
    dsnh.is_nexthop   = 1;

    cmd = DRV_IOR(EpeNextHopInternal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &dsnh8w));
    sal_memcpy(((uint8*)&dsnh8w + sizeof(ds_next_hop4_w_t)), &dsnh, sizeof(ds_next_hop4_w_t));

    cmd = DRV_IOW(EpeNextHopInternal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &dsnh8w));
    return CTC_E_NONE;
}
#endif

int32
sys_greatbelt_nh_create_rspan_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                 sys_nh_info_com_t* p_com_db)
{

    sys_nh_param_rspan_t* p_nh_para;
    sys_nh_param_dsnh_t dsnh_param;
    ctc_vlan_egress_edit_info_t  vlan_egress_edit_info;
    sys_nh_info_rspan_t* p_nhdb = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    /* 1. sanity check & init */
    CTC_PTR_VALID_CHECK(p_com_nh_para);
    CTC_PTR_VALID_CHECK(p_com_db);
    p_nh_para = (sys_nh_param_rspan_t*)p_com_nh_para;
    p_nhdb = (sys_nh_info_rspan_t*)p_com_db;

    CTC_PTR_VALID_CHECK(p_nh_para->p_rspan_param);
    CTC_VLAN_RANGE_CHECK(p_nh_para->p_rspan_param->rspan_vid);

    sal_memset(&dsnh_param, 0, sizeof(sys_nh_param_dsnh_t));

    sal_memset(&vlan_egress_edit_info, 0, sizeof(ctc_vlan_egress_edit_info_t));
    vlan_egress_edit_info.output_svid = p_nh_para->p_rspan_param->rspan_vid;

    dsnh_param.dsnh_type = SYS_NH_PARAM_DSNH_TYPE_RSPAN;
    dsnh_param.p_vlan_info = &vlan_egress_edit_info;
    dsnh_param.dsnh_offset = p_nh_para->dsnh_offset;
    dsnh_param.dest_vlan_ptr = p_nh_para->p_rspan_param->rspan_vid;

    p_nhdb->hdr.nh_entry_type = SYS_NH_TYPE_RSPAN;

    dsnh_param.dsnh_offset = p_nhdb->hdr.dsfwd_info.dsnh_offset;

    CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsnh4w(lchip, &dsnh_param));



    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_delete_rspan_cb(uint8 lchip, sys_nh_info_com_t* p_com_db, uint32* p_nhid)
{
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_misc_write_dsnh(uint8 lchip,
                                            uint8 gchip,
                                            uint16 dest_vlan_ptr, uint8 dsnh_type,
                                            sys_misc_nh_edit_param_t* psys_edit_param,
                                            sys_nh_info_misc_t* p_nhdb)
{
    sys_nh_param_dsnh_t dsnh_param;

    ds_l2_edit_swap_t l2edit_swap;
    ds_l3_edit_nat4_w_t  l3edit_4w;
    uint32 param_flag = 0;
    bool do_l2_edit = FALSE;
    bool do_l3_edit = FALSE;

    sal_memset(&l2edit_swap, 0, sizeof(ds_l2_edit_swap_t));
    sal_memset(&l3edit_4w, 0, sizeof(ds_l3_edit_nat4_w_t));
    sal_memset(&dsnh_param, 0, sizeof(sys_nh_param_dsnh_t));


    dsnh_param.stats_ptr = psys_edit_param->stats_ptr;
    dsnh_param.dest_vlan_ptr = dest_vlan_ptr;
    dsnh_param.dsnh_type = dsnh_type;
    param_flag = psys_edit_param->flag;
    if(param_flag & CTC_MISC_NH_L2_EDIT_VLAN_EDIT)
    {
        dsnh_param.p_vlan_info = &(psys_edit_param->vlan_edit_info);
        dsnh_param.dest_vlan_ptr = psys_edit_param->vlan_edit_info.user_vlanptr ? psys_edit_param->vlan_edit_info.user_vlanptr : dsnh_param.dest_vlan_ptr;
    }

     /* L2 MACDA or MACSA Edit Start!!!*/
    l2edit_swap.ds_type = SYS_NH_DS_TYPE_L2EDIT;
    l2edit_swap.l2_rewrite_type = SYS_NH_L2EDIT_TYPE_MAC_SWAP;

    if (param_flag & CTC_MISC_NH_L2_EDIT_SWAP_MAC)
    {
        l2edit_swap.type = 0;
        l2edit_swap.derive_mcast_mac = 1;
        l2edit_swap.output_vlan_id_valid = 1;
        l2edit_swap.overwrite_ether_type = 0;
        dsnh_param.flag |= SYS_NH_PARAM_FLAG_SWAP_MAC;
    }
    else
    {
        if (param_flag & CTC_MISC_NH_L2_EDIT_REPLACE_MAC_DA)
        {
            l2edit_swap.type = 1;
            l2edit_swap.derive_mcast_mac = 1;
            l2edit_swap.mac_da47_32 = (psys_edit_param->mac_da[0] << 8) | psys_edit_param->mac_da[1];
            l2edit_swap.mac_da31_30 = (psys_edit_param->mac_da[2] >> 6);
            l2edit_swap.mac_da29_0 = psys_edit_param->mac_da[5] | (psys_edit_param->mac_da[4] << 8) | (psys_edit_param->mac_da[3] << 16) | ((psys_edit_param->mac_da[2] & 0x3F) << 24);
        }

        if (param_flag & CTC_MISC_NH_L2_EDIT_REPLACE_MAC_SA)
        {
            l2edit_swap.mac_sa47_32 = (psys_edit_param->mac_sa[0] << 8) | psys_edit_param->mac_sa[1];
            l2edit_swap.mac_sa31_0 = psys_edit_param->mac_sa[5] | (psys_edit_param->mac_sa[4] << 8) | (psys_edit_param->mac_sa[3] << 16) | (psys_edit_param->mac_sa[2] << 24);
            l2edit_swap.output_vlan_id_valid = 1;
            l2edit_swap.overwrite_ether_type = 1;
        }
    }

    if((param_flag & CTC_MISC_NH_L2_EDIT_SWAP_MAC)||(param_flag & CTC_MISC_NH_L2_EDIT_REPLACE_MAC_DA)||(param_flag & CTC_MISC_NH_L2_EDIT_REPLACE_MAC_SA))
    {
        do_l2_edit = TRUE;
    }
     /* L2 MACDA or MACSA Edit Start!!!*/

     /* IpDa & DPort Edit Start!!!*/
    if (CTC_FLAG_ISSET(param_flag, CTC_MISC_NH_L3_EDIT_REPLACE_IPDA))
    {
        l3edit_4w.ds_type = SYS_NH_DS_TYPE_L3EDIT;
        l3edit_4w.l3_rewrite_type = SYS_NH_L3EDIT_TYPE_NAT_4W;
        l3edit_4w.replace_ip_da = 1;
        l3edit_4w.ip_da31_30 = ((psys_edit_param->ipda)>>30)&0x3;
        l3edit_4w.ip_da29_0 = (psys_edit_param->ipda)&0x3fffffff;
        l3edit_4w.nat_mode = 1;
        do_l3_edit = TRUE;
    }

    if (CTC_FLAG_ISSET(param_flag, CTC_MISC_NH_L3_EDIT_REPLACE_DST_PORT))
    {
        l3edit_4w.ds_type = SYS_NH_DS_TYPE_L3EDIT;
        l3edit_4w.l3_rewrite_type = SYS_NH_L3EDIT_TYPE_NAT_4W;
        l3edit_4w.replace_l4_dest_port = 1;
        l3edit_4w.l4_dest_port = psys_edit_param->dst_port;
        l3edit_4w.nat_mode = 1;
        do_l3_edit = TRUE;
    }
     /* IpDa & DPort Edit End!!!*/


    if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE))
    {
        if (SYS_GCHIP_IS_REMOTE(lchip, gchip))
        {
            return CTC_E_NONE;
        }
    }

    if (TRUE == do_l2_edit)
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip,  SYS_NH_ENTRY_TYPE_L2EDIT_SWAP, 1,  &p_nhdb->dsl2edit_offset));
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
        CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip,  SYS_NH_ENTRY_TYPE_L2EDIT_SWAP, p_nhdb->dsl2edit_offset, &l2edit_swap));
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Lchip = %d, DsL2Edit, offset = %d\n", lchip, p_nhdb->dsl2edit_offset);

    }

    if (TRUE == do_l3_edit)
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip,  SYS_NH_ENTRY_TYPE_L3EDIT_NAT_4W, 1,  &p_nhdb->dsl3edit_offset));
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT);
        CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip,  SYS_NH_ENTRY_TYPE_L3EDIT_NAT_4W, p_nhdb->dsl3edit_offset, &l3edit_4w));
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Lchip = %d, DsL3Edit, offset = %d\n", lchip, p_nhdb->dsl3edit_offset);
    }

    dsnh_param.l2edit_ptr = p_nhdb->dsl2edit_offset;
    dsnh_param.l3edit_ptr = p_nhdb->dsl3edit_offset;
    dsnh_param.dsnh_offset = p_nhdb->hdr.dsfwd_info.dsnh_offset;

    if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsnh8w(lchip, &dsnh_param));
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsnh4w(lchip, &dsnh_param));
    }



    return CTC_E_NONE;
}

int32
_sys_greatbelt_nh_misc_add_dsfwd(uint8 lchip, sys_nh_info_com_t* p_com_db)
{
   sys_nh_param_dsfwd_t dsfwd_param;
   sys_nh_info_misc_t* p_nhdb = (sys_nh_info_misc_t*)(p_com_db);
   uint32 dsfwd_offset;
   int ret = 0;


   sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));
   sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));

    dsfwd_param.dest_chipid = SYS_MAP_GPORT_TO_GCHIP(p_nhdb->gport);
    dsfwd_param.dest_id = CTC_MAP_GPORT_TO_LPORT(p_nhdb->gport);
    dsfwd_param.is_mcast = FALSE;
    dsfwd_param.is_reflective = p_nhdb->is_reflective;

   if (p_nhdb->stats_valid)
   {
       uint16 stats_ptr = 0;
       ret = sys_greatbelt_stats_get_statsptr(lchip, p_nhdb->stats_id, &stats_ptr);
       dsfwd_param.stats_ptr = stats_ptr;
   }

    if(CTC_MISC_NH_TYPE_TO_CPU == p_nhdb->misc_nh_type)
    {
        dsfwd_param.is_to_cpu = 1;
    }

    if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        dsfwd_param.nexthop_ext = 1;
    }


    /*Build DsFwd Table*/
    ret = sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,
                                        &dsfwd_offset);


    p_nhdb->hdr.dsfwd_info.dsfwd_offset = dsfwd_offset;
    dsfwd_param.dsnh_offset = p_nhdb->hdr.dsfwd_info.dsnh_offset;
    dsfwd_param.dsfwd_offset = dsfwd_offset;
    dsfwd_param.bypass_ingress_edit = CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE);

    /*Write table*/
    ret = ret ? ret : sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param);

    if (ret != CTC_E_NONE)
    {
        /*Free DsFwd offset*/
        sys_greatbelt_nh_offset_free(lchip,  SYS_NH_ENTRY_TYPE_FWD, 1, p_nhdb->hdr.dsfwd_info.dsfwd_offset);
    }
    else
    {
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
    }
    return ret;
}

int32
sys_greatbelt_nh_create_misc_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                sys_nh_info_com_t* p_com_db)
{
    sys_nh_param_misc_t* p_nh_misc;
    int32 ret =CTC_E_NONE;
    uint32 gport = 0;

    sys_nh_info_misc_t* p_nhdb = NULL;
    ctc_misc_nh_param_t* p_tmp_misc_param = NULL;

    sys_misc_nh_edit_param_t sys_edit_param;
    ctc_misc_nh_type_t misc_nh_type;

    sal_memset(&sys_edit_param, 0, sizeof(sys_misc_nh_edit_param_t));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    /* 1. sanity check & init */
    CTC_PTR_VALID_CHECK(p_com_nh_para);
    CTC_PTR_VALID_CHECK(p_com_db);

    p_nh_misc = (sys_nh_param_misc_t*)p_com_nh_para;
    p_nhdb    = (sys_nh_info_misc_t*)p_com_db;
    p_tmp_misc_param = p_nh_misc->p_misc_param;
    misc_nh_type = p_tmp_misc_param->type;

    if(CTC_MISC_NH_TYPE_REPLACE_L2HDR == misc_nh_type)
    {
        sys_edit_param.flag = p_tmp_misc_param->misc_param.l2edit.flag;
        sal_memcpy(sys_edit_param.mac_da,p_tmp_misc_param->misc_param.l2edit.mac_da,sizeof(mac_addr_t));
        sal_memcpy(sys_edit_param.mac_sa,p_tmp_misc_param->misc_param.l2edit.mac_sa,sizeof(mac_addr_t));
        sys_edit_param.is_reflective = p_tmp_misc_param->misc_param.l2edit.is_reflective;
    }
    else if(CTC_MISC_NH_TYPE_REPLACE_L2_L3HDR == misc_nh_type)
    {
        sys_edit_param.flag = p_tmp_misc_param->misc_param.l2_l3edit.flag;
        sal_memcpy(sys_edit_param.mac_da,p_tmp_misc_param->misc_param.l2_l3edit.mac_da,sizeof(mac_addr_t));
        sal_memcpy(sys_edit_param.mac_sa,p_tmp_misc_param->misc_param.l2_l3edit.mac_sa,sizeof(mac_addr_t));
        sys_edit_param.ipda = p_tmp_misc_param->misc_param.l2_l3edit.ipda;
        sys_edit_param.dst_port = p_tmp_misc_param->misc_param.l2_l3edit.dst_port;
        sys_edit_param.is_reflective = p_tmp_misc_param->misc_param.l2_l3edit.is_reflective;
        CTC_MAX_VALUE_CHECK(p_tmp_misc_param->misc_param.l2_l3edit.vlan_edit_info.user_vlanptr, 0x1fff);
        sal_memcpy(&(sys_edit_param.vlan_edit_info),(&((p_tmp_misc_param->misc_param.l2_l3edit).vlan_edit_info)),
                    sizeof(ctc_vlan_egress_edit_info_t));
    }

    if(CTC_MISC_NH_TYPE_TO_CPU != misc_nh_type)
    {
        SYS_NH_MISC_FLAG_CONFLICT_CHECK(sys_edit_param.flag);
        if ((sys_edit_param.flag&CTC_MISC_NH_L2_EDIT_VLAN_EDIT) &&
            (CTC_FLAG_ISSET(sys_edit_param.vlan_edit_info.flag, CTC_VLAN_NH_STATS_EN)) &&
            p_tmp_misc_param->stats_id)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "vlan edit stats and misc nexthop stats cannot enable at the same time! \n");
            return CTC_E_INVALID_CONFIG;
        }
    }
    else
    {
        if(p_tmp_misc_param->misc_param.cpu_reason.cpu_reason_id >= SYS_NH_MAX_CPU_RESEAON_ID)
        {
            return CTC_E_INVALID_PARAM;
        }

        if (p_tmp_misc_param->stats_id)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " Feature not supported \n");
            return CTC_E_NOT_SUPPORT;
        }
    }

    if (p_tmp_misc_param->stats_id)
    {
        CTC_ERROR_RETURN(sys_greatbelt_stats_get_statsptr(lchip,
                                                           p_tmp_misc_param->stats_id,
                                                           &p_nh_misc->hdr.stats_ptr));
        sys_edit_param.stats_ptr = p_nh_misc->hdr.stats_ptr;
    }

    p_nhdb->hdr.nh_entry_type   = SYS_NH_TYPE_MISC;
    p_nhdb->misc_nh_type        = misc_nh_type;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhType = %d, miscNhType = %d, NHID = %d, isInternaNH = %d, \n\
        GlobalPort = %d, DsNHOffset = %d\n",
                   p_nh_misc->hdr.nh_param_type,
                   misc_nh_type,
                   p_nh_misc->hdr.nhid,
                   p_nh_misc->hdr.is_internal_nh,
                   p_nh_misc->p_misc_param->gport,
                   p_nh_misc->p_misc_param->dsnh_offset);

    if (sys_edit_param.is_reflective)
    {
        uint8 gchip = 0;
        sys_greatbelt_get_gchip_id(lchip, &gchip);
        p_nh_misc->p_misc_param->gport = CTC_MAP_LPORT_TO_GPORT(gchip, 0);
    }

    p_nhdb->gport = p_nh_misc->p_misc_param->gport;

    if (CTC_MISC_NH_TYPE_TO_CPU == misc_nh_type)
    {
        if (0 == p_nhdb->gport)
        {
            CTC_ERROR_GOTO(sys_greatbelt_cpu_reason_get_dsfwd_offset(lchip,
                                                                     p_nh_misc->p_misc_param->misc_param.cpu_reason.cpu_reason_id,
                                                                     p_nh_misc->p_misc_param->misc_param.cpu_reason.lport_en,
                                                                     &p_nhdb->hdr.dsfwd_info.dsfwd_offset,
                                                                     &p_nhdb->hdr.dsfwd_info.dsnh_offset, &gport), ret , error_proc);

            p_nhdb->gport = gport;
            CTC_ERROR_GOTO(sys_greatbelt_nh_update_dsfwd_lport_en(lchip, p_nhdb->hdr.dsfwd_info.dsfwd_offset,
                                                      p_nh_misc->p_misc_param->misc_param.cpu_reason.lport_en), ret, error_proc);
        }
        else
        {
            p_nhdb->gport = CTC_GPORT_RCPU(CTC_MAP_GPORT_TO_GCHIP(p_nh_misc->p_misc_param->gport));
        }

        p_nhdb->hdr.dsfwd_info.dsnh_offset = CTC_PKT_CPU_REASON_BUILD_NHPTR(p_nh_misc->p_misc_param->misc_param.cpu_reason.cpu_reason_id, 0);

        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_RSV_DSFWD);
        p_nh_misc->hdr.have_dsfwd = 0;
    }
    else
    {
        /*write l2edit and nexthop*/
        CTC_ERROR_GOTO(_sys_greatbelt_nh_misc_write_dsnh(lchip,
                                                           CTC_MAP_GPORT_TO_GCHIP(p_nhdb->gport),
                                                           SYS_DSNH_DESTVLANPTR_SPECIAL,
                                                           SYS_NH_PARAM_DSNH_TYPE_BRGUC,
                                                           &sys_edit_param,
                                                           p_nhdb),ret,error_proc);
    }
	p_nhdb->is_reflective = sys_edit_param.is_reflective;


	if (!(sys_edit_param.is_reflective) && !p_nh_misc->hdr.have_dsfwd)
    {
        return CTC_E_NONE ;
    }
    CTC_ERROR_GOTO(_sys_greatbelt_nh_misc_add_dsfwd(lchip, p_com_db),ret,error_proc);

error_proc:
    if (CTC_E_NONE == ret)
    {
        return ret;
    }

    if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT))
    {

        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip,  SYS_NH_ENTRY_TYPE_L2EDIT_SWAP, 1, p_nhdb->dsl2edit_offset));
    }

    if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
    {

        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip,  SYS_NH_ENTRY_TYPE_L3EDIT_NAT_4W, 1, p_nhdb->dsl3edit_offset));
    }

    return ret;

}

int32
sys_greatbelt_nh_delete_misc_cb(uint8 lchip, sys_nh_info_com_t* p_flex_info, uint32* p_nhid)
{

    sys_nh_info_misc_t* p_nh_info;
    sys_nh_param_dsfwd_t dsfwd_param;
    sys_nh_ref_list_node_t* p_ref_node, * p_tmp_node;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_flex_info);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_MISC, p_flex_info->hdr.nh_entry_type);

    p_nh_info = (sys_nh_info_misc_t*)(p_flex_info);

    sys_greatbelt_nh_update_ecmp_member(lchip, *p_nhid, SYS_NH_CHANGE_TYPE_NH_DELETE);

    /*1. Delete ecmp  list */
    p_ref_node = p_nh_info->hdr.p_ref_nh_list;

    while (p_ref_node)
    {
        /*Remove from db*/
        p_tmp_node = p_ref_node;
        p_ref_node = p_ref_node->p_next;
        mem_free(p_tmp_node);
    }

    /*2. Free DsFwd/DsNexthop/DsL2Edit offset*/
    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));


    if (CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD)
        && !CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_RSV_DSFWD))
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, \
        SYS_NH_ENTRY_TYPE_FWD, 1, p_nh_info->hdr.dsfwd_info.dsfwd_offset));

        dsfwd_param.drop_pkt  = TRUE;
        dsfwd_param.dsfwd_offset = p_nh_info->hdr.dsfwd_info.dsfwd_offset;
        /*Write table*/
        CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param));
    }

    if (CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT))
    {

        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip,  SYS_NH_ENTRY_TYPE_L2EDIT_SWAP, 1, p_nh_info->dsl2edit_offset));
    }

    if (CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
    {

        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip,  SYS_NH_ENTRY_TYPE_L3EDIT_NAT_4W, 1, p_nh_info->dsl3edit_offset));
    }


    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_update_misc_cb(uint8 lchip, sys_nh_info_com_t* p_nh_db,
                                sys_nh_param_com_t* p_para)
{
    sys_nh_info_misc_t* p_nh_info = NULL;
    sys_nh_param_misc_t* p_nh_para = NULL;
    ds_fwd_t dsfwd;
    uint16 stats_ptr = 0;
    int32 ret = CTC_E_NONE;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(p_para);
    CTC_PTR_VALID_CHECK(p_nh_db);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_MISC, p_para->hdr.nh_param_type);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_MISC, p_nh_db->hdr.nh_entry_type);
    p_nh_info = (sys_nh_info_misc_t*)(p_nh_db);
    p_nh_para = (sys_nh_param_misc_t*)(p_para);
    switch (p_para->hdr.change_type)
    {
    case SYS_NH_CHANGE_TYPE_ADD_DYNTBL:
        {

           if (CTC_FLAG_ISSET(p_nh_db->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
            {
                sal_memset(&dsfwd, 0, sizeof(ds_fwd_t));
                if (p_nh_para->hdr.stats_valid && p_nh_info->stats_valid)
                {
                    return CTC_E_INVALID_CONFIG;
                }
                else if (p_nh_para->hdr.stats_valid)
                {
                    ret = ret ? ret : sys_greatbelt_stats_get_statsptr(lchip, p_nh_para->hdr.stats_id, &stats_ptr);
                }
                else if (p_nh_info->stats_valid)
                {
                    stats_ptr = 0;
                }
                sal_memset(&dsfwd, 0, sizeof(ds_fwd_t));
                ret = ret ? ret : sys_greatbelt_nh_get_entry_dsfwd(lchip, p_nh_info->hdr.dsfwd_info.dsfwd_offset, &dsfwd);
                dsfwd.stats_ptr_low = stats_ptr & 0xFFF;
                dsfwd.stats_ptr_high = (stats_ptr >> 12) & 0xF;
                ret = ret ? ret : sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_FWD, p_nh_info->hdr.dsfwd_info.dsfwd_offset, &dsfwd);
                if (CTC_E_NONE == ret)
                {
                    p_nh_info->stats_valid = p_nh_para->hdr.stats_id?TRUE:FALSE;
                    p_nh_info->stats_id = p_nh_para->hdr.stats_id;
                }
            }
            else
            {
                p_nh_info->stats_valid = p_nh_para->hdr.stats_valid?TRUE:FALSE;
                p_nh_info->stats_id = p_nh_para->hdr.stats_id;
            _sys_greatbelt_nh_misc_add_dsfwd(lchip, p_nh_db);
            }
        }
    	break;
    default:
        return CTC_E_INVALID_PARAM;
    }

  return ret;
}
