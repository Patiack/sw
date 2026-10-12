/**
 @file sys_greatbelt_nexthop_l2.c

 @date 2009-09-19

 @version v2.0

 The file contains all nexthop layer2 related callback function
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
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_aps.h"
#include "sys_greatbelt_queue_enq.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/

STATIC  int32
_sys_greatbelt_nh_brguc_write_dsnh_with_vlan_edit(uint8 lchip, uint8 gchip,
                                                  uint16 dest_vlan_ptr,  uint8 dsnh_type,
                                                  ctc_vlan_egress_edit_info_t* p_vlan_edit_info,
                                                  sys_nh_info_brguc_t* p_nhdb_ucast_brg,
                                                  bool is_protection_path,
                                                  uint16 logic_port)
{
    sys_nh_param_dsnh_t dsnh_param;

    sal_memset(&dsnh_param, 0, sizeof(sys_nh_param_dsnh_t));

    if (p_vlan_edit_info)
    {
        if (p_vlan_edit_info->user_vlanptr == 0xffff)
        {
            dsnh_param.dest_vlan_ptr = 0x1800;
        }
        else if (p_vlan_edit_info->user_vlanptr == 0)
        {
            dsnh_param.dest_vlan_ptr = 0;
        }
        else
        {
          dsnh_param.dest_vlan_ptr = p_vlan_edit_info->user_vlanptr;
        }
    }
    dsnh_param.dsnh_type = dsnh_type;
    dsnh_param.p_vlan_info = p_vlan_edit_info;

    if (p_vlan_edit_info && CTC_FLAG_ISSET(p_vlan_edit_info->flag, CTC_VLAN_NH_HORIZON_SPLIT_EN)
        && CTC_FLAG_ISSET(p_vlan_edit_info->flag, CTC_VLAN_NH_PASS_THROUGH))
    {
        return CTC_E_INVALID_CONFIG;
    }

    if (CTC_FLAG_ISSET(p_nhdb_ucast_brg->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE))
    {
        if (SYS_GCHIP_IS_REMOTE(lchip, gchip)
            && (SYS_NH_PARAM_BRGUC_SUB_TYPE_APS_VLAN_EDIT != p_nhdb_ucast_brg->nh_sub_type))
        {
            return CTC_E_NONE;
        }
    }

    if (is_protection_path)
    {
        if (CTC_FLAG_ISSET(p_nhdb_ucast_brg->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
        {
            dsnh_param.dsnh_offset = p_nhdb_ucast_brg->hdr.dsfwd_info.dsnh_offset + 2;
            CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsnh8w(lchip, &dsnh_param));
        }
        else
        {
            dsnh_param.dsnh_offset = p_nhdb_ucast_brg->hdr.dsfwd_info.dsnh_offset + 1;
            CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsnh4w(lchip, &dsnh_param));
        }
    }
    else
    {
        dsnh_param.dsnh_offset = p_nhdb_ucast_brg->hdr.dsfwd_info.dsnh_offset;

        if (CTC_FLAG_ISSET(p_nhdb_ucast_brg->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
        {
            if (p_nhdb_ucast_brg->loop_nhid)
            {
                uint32 l2edit_ptr;
                CTC_ERROR_RETURN(sys_greatbelt_nh_add_eloop_edit(lchip, p_nhdb_ucast_brg->loop_nhid, 1, &l2edit_ptr));
                dsnh_param.l2edit_ptr = l2edit_ptr;
            }

            if (logic_port)
            {
                dsnh_param.logic_port               = logic_port;
                dsnh_param.logic_port_check         = 1;
                p_nhdb_ucast_brg->dest_logic_port   = logic_port;
                CTC_SET_FLAG(p_nhdb_ucast_brg->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LOGIC_PORT);
            }

            CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsnh8w(lchip, &dsnh_param));
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsnh4w(lchip, &dsnh_param));
        }

    }

    return CTC_E_NONE;
}

/**
 @brief the subfunction of create unicast APS bridge nexthop

 @return CTC_E_XXX
 */
int32
_sys_greatbelt_nh_brguc_aps_process(uint8 lchip, sys_nh_param_brguc_t* p_nh_para_bridge,
                                    sys_nh_info_brguc_t* p_nhdb_ucast_brg,
                                    sys_nh_param_dsfwd_t* p_dsfwd_param)
{
    uint8  gchip  = 0;
    uint32 wroking_gport = 0;
    uint32 protect_gport = 0;
    uint16 aps_bridge_group_id = 0;
    bool raps_en = FALSE;

    CTC_PTR_VALID_CHECK(p_nh_para_bridge->p_vlan_edit_info);
    CTC_PTR_VALID_CHECK(p_nh_para_bridge->p_vlan_edit_info_prot_path);

    /*1.get working port & protection port by aps group id*/
    aps_bridge_group_id = p_nh_para_bridge->gport;

    CTC_ERROR_RETURN(sys_greatbelt_aps_get_aps_bridge_working_path(lchip, aps_bridge_group_id, &wroking_gport));
    CTC_ERROR_RETURN(sys_greatbelt_aps_get_aps_bridge_protection_path(lchip, aps_bridge_group_id, &protect_gport));
    CTC_ERROR_RETURN(sys_greatbelt_aps_get_aps_raps_en(lchip, aps_bridge_group_id, &raps_en));


    if ((CTC_MAP_GPORT_TO_GCHIP(wroking_gport) != CTC_MAP_GPORT_TO_GCHIP(protect_gport))
        || (CTC_MAP_GPORT_TO_GCHIP(wroking_gport) == 0x1F)
        || (CTC_MAP_GPORT_TO_GCHIP(protect_gport) == 0x1F)
        || raps_en)
    {
        gchip = 0x1F;
    }
    else
    {
        gchip = CTC_MAP_GPORT_TO_GCHIP(wroking_gport);
    }


    CTC_ERROR_RETURN(_sys_greatbelt_nh_brguc_write_dsnh_with_vlan_edit(lchip,
                                                                       gchip,
                                                                       SYS_DSNH_DESTVLANPTR_SPECIAL,
                                                                       SYS_NH_PARAM_DSNH_TYPE_BRGUC,
                                                                       p_nh_para_bridge->p_vlan_edit_info,
                                                                       p_nhdb_ucast_brg, FALSE, 0));

    if (!CTC_FLAG_ISSET(p_nhdb_ucast_brg->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH))
    {

        CTC_ERROR_RETURN(_sys_greatbelt_nh_brguc_write_dsnh_with_vlan_edit(lchip,
                                                                           gchip,
                                                                           SYS_DSNH_DESTVLANPTR_SPECIAL,
                                                                           SYS_NH_PARAM_DSNH_TYPE_BRGUC,
                                                                           p_nh_para_bridge->p_vlan_edit_info_prot_path,
                                                                           p_nhdb_ucast_brg, TRUE, 0));
    }

    return CTC_E_NONE;
}

/**
 @brief Callback function of create unicast bridge nexthop

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_create_brguc_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                 sys_nh_info_com_t* p_com_db)
{
    sys_nh_param_brguc_t* p_nh_para_bridge;
    sys_nh_info_brguc_t* p_nhdb_ucast_brg;
    sys_nh_param_dsfwd_t dsfwd_param;
    int32 ret;
    uint8 gchip = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    /* 1. sanity check & init */
    CTC_PTR_VALID_CHECK(p_com_nh_para);
    CTC_PTR_VALID_CHECK(p_com_db);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_BRGUC, p_com_nh_para->hdr.nh_param_type);
    p_nh_para_bridge = (sys_nh_param_brguc_t*)p_com_nh_para;
    p_nhdb_ucast_brg = (sys_nh_info_brguc_t*)p_com_db;
    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));
    sal_memset(&dsfwd_param.dsfwd_offset, 0xFF, sizeof(dsfwd_param.dsfwd_offset));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "nhType = %d, NHID = %d, isInternaNH = %d, \n\
        GlobalPort = %d, DsNHOffset = %d\n",
                   p_nh_para_bridge->hdr.nh_param_type,
                   p_nh_para_bridge->hdr.nhid,
                   p_nh_para_bridge->hdr.is_internal_nh,
                   p_nh_para_bridge->gport,
                   p_nh_para_bridge->dsnh_offset);

    p_nhdb_ucast_brg->hdr.nh_entry_type = SYS_NH_TYPE_BRGUC;
/*    p_nhdb_ucast_brg->hdr.nh_entry_flags = 0; */
    p_nhdb_ucast_brg->loop_nhid = p_nh_para_bridge->loop_nhid;
    p_nhdb_ucast_brg->dest.dest_gport = p_nh_para_bridge->gport;

    if(p_nh_para_bridge->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_APS_VLAN_EDIT)
    {
        dsfwd_param.dest_id = p_nh_para_bridge->gport;
    }
    else
    {
        dsfwd_param.dest_chipid = SYS_MAP_GPORT_TO_GCHIP(p_nh_para_bridge->gport);
        dsfwd_param.dest_id = CTC_MAP_GPORT_TO_LPORT(p_nh_para_bridge->gport);
    }

    if (!CTC_FLAG_ISSET(p_nhdb_ucast_brg->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_RSV_DSNH))
    {
        if (p_nh_para_bridge->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_VLAN_EDIT)
        {

            CTC_ERROR_RETURN(_sys_greatbelt_nh_brguc_write_dsnh_with_vlan_edit(lchip,
                                                                               CTC_MAP_GPORT_TO_GCHIP(p_nh_para_bridge->gport),
                                                                               SYS_DSNH_DESTVLANPTR_SPECIAL,
                                                                               SYS_NH_PARAM_DSNH_TYPE_BRGUC,
                                                                               p_nh_para_bridge->p_vlan_edit_info,
                                                                               p_nhdb_ucast_brg, FALSE, p_nh_para_bridge->p_vlan_edit_nh_param->logic_port));

        }
        else if (p_nh_para_bridge->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_APS_VLAN_EDIT)
        {

            CTC_ERROR_RETURN(_sys_greatbelt_nh_brguc_aps_process(lchip, p_nh_para_bridge, p_nhdb_ucast_brg, &dsfwd_param));
            p_nhdb_ucast_brg->dest.aps_bridge_group_id = p_nh_para_bridge->gport;
            dsfwd_param.aps_type = CTC_APS_BRIDGE;

        }
    }
    else
    {
        if (p_nh_para_bridge->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_APS_VLAN_EDIT)
        {
            p_nhdb_ucast_brg->dest.aps_bridge_group_id = p_nh_para_bridge->gport;
            dsfwd_param.aps_type = CTC_APS_BRIDGE;
        }
    }

   /*service en queue*/
    if ((p_nh_para_bridge->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_VLAN_EDIT) ||
        (p_nh_para_bridge->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_APS_VLAN_EDIT))
    {
        if (p_nh_para_bridge->p_vlan_edit_info &&
            CTC_FLAG_ISSET(p_nh_para_bridge->p_vlan_edit_info->flag, CTC_VLAN_NH_SERVICE_QUEUE_FLAG))
        {
            dsfwd_param.service_queue_en = TRUE;
        }

        if (p_nh_para_bridge->p_vlan_edit_info_prot_path &&
            CTC_FLAG_ISSET(p_nh_para_bridge->p_vlan_edit_info_prot_path->flag, CTC_VLAN_NH_SERVICE_QUEUE_FLAG))
        {
            dsfwd_param.service_queue_en = TRUE;
        }
    }


    dsfwd_param.is_mcast = FALSE;
    dsfwd_param.bypass_ingress_edit = CTC_FLAG_ISSET(p_nhdb_ucast_brg->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE);

    /*Build DsFwd Table*/
    ret = sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,
                                        &dsfwd_param.dsfwd_offset);
    if (ret)
    {
        return ret;
    }

    if (p_nh_para_bridge->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_APS_VLAN_EDIT)
    {
        sys_greatbelt_get_gchip_id(lchip, &gchip);
        dsfwd_param.dest_chipid = gchip;
    }

    p_nhdb_ucast_brg->hdr.dsfwd_info.dsfwd_offset = dsfwd_param.dsfwd_offset;
    p_nh_para_bridge->fwd_offset = dsfwd_param.dsfwd_offset;

    CTC_SET_FLAG(p_nhdb_ucast_brg->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);

    if (CTC_FLAG_ISSET(p_nhdb_ucast_brg->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        dsfwd_param.nexthop_ext = 1;
    }

    dsfwd_param.dsnh_offset = p_nhdb_ucast_brg->hdr.dsfwd_info.dsnh_offset;

    /*Write table*/
    ret = sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param);


    return CTC_E_NONE;
}

/**
 @brief Callback function of delete unicast bridge nexthop

 @param[in] p_com_db, pointer used to store nexthop data

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_delete_brguc_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid)
{
    sys_nh_info_brguc_t* nh_brg_ucast;
    sys_nh_param_dsfwd_t dsfwd_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_data);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_BRGUC, p_data->hdr.nh_entry_type);
    nh_brg_ucast = (sys_nh_info_brguc_t*)(p_data);

    /*2. Free DsFwd offset*/
    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));


    if (CTC_FLAG_ISSET(nh_brg_ucast->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, \
        SYS_NH_ENTRY_TYPE_FWD, 1, nh_brg_ucast->hdr.dsfwd_info.dsfwd_offset));


        if (nh_brg_ucast->loop_nhid)
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_remove_eloop_edit(lchip, nh_brg_ucast->loop_nhid));
        }

        dsfwd_param.drop_pkt  = TRUE;
        dsfwd_param.dsfwd_offset = nh_brg_ucast->hdr.dsfwd_info.dsfwd_offset;
        /*Write table*/
        CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param));
    }


    return CTC_E_NONE;
}

