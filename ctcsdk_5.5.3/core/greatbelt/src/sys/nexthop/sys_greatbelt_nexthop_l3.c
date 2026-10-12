/**
 @file sys_greatbelt_nexthop_l3.c

 @date 2009-11-23

 @version v2.0

 The file contains all nexthop layer3 related callback function
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
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_l3if.h"
#include "sys_greatbelt_stats.h"
#include "sys_greatbelt_common.h"

#include "greatbelt/include/drv_io.h"

STATIC int32
_sys_greatbelt_nh_ipuc_update_dsnh(uint8 lchip, sys_nh_param_ipuc_t* p_nhpara,sys_nh_info_ipuc_t* p_nhdb, bool working_path)
{
    sys_l3if_prop_t l3if_prop;
    sys_nh_param_dsnh_t dsnh_param;
    sys_nh_db_dsl2editeth4w_t   dsl2edit_4w;
    sys_nh_db_dsl2editeth4w_t*  p_dsl2edit_4w;
    ctc_nh_oif_info_t *p_oif;
    uint8 use_l2edit    = 0;
    uint16 arp_id       = 0;

    sal_memset(&dsnh_param, 0, sizeof(sys_nh_param_dsnh_t));
    sal_memset(&l3if_prop, 0, sizeof(sys_l3if_prop_t));
    sal_memset(&dsl2edit_4w, 0, sizeof(sys_nh_db_dsl2editeth4w_t));

    if(p_nhpara->ttl_no_dec)
    {
        CTC_SET_FLAG(dsnh_param.flag, SYS_NH_PARAM_FLAG_ROUTE_NOTTL);
    }

    if((SYS_RESERVE_PORT_MPLS_BFD == CTC_MAP_GPORT_TO_LPORT(p_nhpara->oif.gport))
        || (SYS_RESERVE_PORT_PW_VCCV_BFD == CTC_MAP_GPORT_TO_LPORT(p_nhpara->oif.gport))
        || p_nhpara->strip_l3)
    {
       use_l2edit = 0 ;
       CTC_SET_FLAG(dsnh_param.flag, SYS_NH_PARAM_FLAG_PW_BFD_PLD_ROUTE);
       CTC_SET_FLAG(p_nhdb->flag, SYS_NH_IPUC_FLAG_PW_BFD_PLD_ROUTE);
       p_oif = &p_nhpara->oif;
       p_oif->is_l2_port = 1;
    }
    else
    {
        p_oif = working_path ? &p_nhpara->oif : &p_nhpara->p_oif;
        if (!(CTC_IS_CPU_PORT(p_nhpara->oif.gport) || p_nhpara->oif.is_l2_port))
        {
            CTC_ERROR_RETURN(sys_greatbelt_l3if_get_l3if_info_with_port_and_vlan(lchip, p_oif->gport, p_oif->vid, &l3if_prop));
        }

        arp_id = working_path ? p_nhpara->arp_id: p_nhpara->p_arp_id;

        sal_memcpy(dsl2edit_4w.mac_da,working_path?(p_nhpara->mac):(p_nhpara->p_mac), sizeof(mac_addr_t));

        if (l3if_prop.l3if_type == CTC_L3IF_TYPE_PHY_IF && p_oif->vid)
        {
            dsl2edit_4w.output_vid           = p_oif->vid;
            dsl2edit_4w.output_vlan_is_svlan = 1;
        }

        if (l3if_prop.l3if_type == CTC_L3IF_TYPE_PHY_IF)
        {
            dsl2edit_4w.phy_if               = 1;
        }

        sys_greatbelt_nh_get_ip_use_l2edit(lchip, &use_l2edit);
        use_l2edit =  use_l2edit | (p_nhpara->loop_nhid !=0);
        use_l2edit |=  (l3if_prop.l3if_type == CTC_L3IF_TYPE_PHY_IF && p_oif->vid);
        CTC_UNSET_FLAG(p_nhdb->flag, SYS_NH_IPUC_FLAG_PW_BFD_PLD_ROUTE);

        if (0 != arp_id)
        {
            use_l2edit = 0;
        }

        if (p_nhpara->ttl_no_dec)
        {
            use_l2edit = 1;
        }

    }

    /*
    if(p_nhpara->ttl_no_dec)
    {
        CTC_SET_FLAG(p_nhinfo->flag, SYS_NH_PARAM_FLAG_TTL_NO_DEC);
    }
    */

    if(use_l2edit)
    {
        p_dsl2edit_4w   = &dsl2edit_4w;
        CTC_ERROR_RETURN(sys_greatbelt_nh_add_route_l2edit(lchip, &p_dsl2edit_4w));
        if(working_path)
            p_nhdb->p_dsl2edit = p_dsl2edit_4w;
        else
            p_nhdb->protection_path->p_dsl2edit = p_dsl2edit_4w;
        dsnh_param.l2edit_ptr  = p_dsl2edit_4w->offset;
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
    }
    else if(arp_id)
    {
        sys_nh_db_arp_t *p_arp = NULL;
        CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_arp_id(lchip, arp_id, &p_arp));
        dsnh_param.l2edit_ptr  = p_arp->offset;
    }
    dsnh_param.dsnh_type = SYS_NH_PARAM_DSNH_TYPE_IPUC;
    if(working_path)
    {
        sal_memcpy(dsnh_param.macDa, p_nhpara->mac, sizeof(mac_addr_t));
        dsnh_param.dsnh_offset = p_nhdb->hdr.dsfwd_info.dsnh_offset;
    }
    else
    {
        sal_memcpy(dsnh_param.macDa, p_nhpara->p_mac, sizeof(mac_addr_t));
        dsnh_param.dsnh_offset = (p_nhdb->hdr.dsfwd_info.dsnh_offset) + 1;
    }

    dsnh_param.dest_vlan_ptr = l3if_prop.vlan_ptr;


    if (p_nhpara->loop_nhid)
    {
        uint32 l3edit_ptr;
        CTC_ERROR_RETURN(sys_greatbelt_nh_add_eloop_edit(lchip, p_nhpara->loop_nhid, 0,  &l3edit_ptr));
        dsnh_param.l3edit_ptr = l3edit_ptr;
        CTC_SET_FLAG(p_nhdb->flag, SYS_NH_IPUC_FLAG_LOOP_NH);
        p_nhdb->loop_nhid = p_nhpara->loop_nhid;
    }
    else
    {
        CTC_UNSET_FLAG(p_nhdb->flag, SYS_NH_IPUC_FLAG_LOOP_NH);
    }
    dsnh_param.mtu_no_chk = p_nhpara->mtu_no_chk;
    CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsnh4w(lchip, &dsnh_param));

    if(working_path)
    {
        p_nhdb->l3ifid = l3if_prop.l3if_id ;
        p_nhdb->arp_id = p_nhpara->arp_id;
    }
    else
    {
        p_nhdb->protection_path->l3ifid = l3if_prop.l3if_id;
        p_nhdb->protection_path->arp_id = p_nhpara->p_arp_id;
    }
    return CTC_E_NONE;
}
int32
_sys_greatbelt_nh_free_ipuc_nh_resource(uint8 lchip, sys_nh_info_ipuc_t* p_nhinfo)
{

   if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT))
   {
       sys_greatbelt_nh_remove_route_l2edit(lchip, p_nhinfo->p_dsl2edit);
       if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
       {
           sys_greatbelt_nh_remove_route_l2edit(lchip, p_nhinfo->protection_path->p_dsl2edit);
       }
   }

    if (CTC_FLAG_ISSET(p_nhinfo->flag, SYS_NH_IPUC_FLAG_LOOP_NH))
    {
         CTC_ERROR_RETURN(sys_greatbelt_nh_remove_eloop_edit(lchip, p_nhinfo->loop_nhid));
    }
    CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
    CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN);
    return CTC_E_NONE;
}
int32
_sys_greatbelt_nh_ipuc_add_dsfwd(uint8 lchip, sys_nh_info_com_t* p_com_db)
{
   sys_nh_param_dsfwd_t dsfwd_param;
   sys_nh_info_ipuc_t* p_nhinfo = (sys_nh_info_ipuc_t*)(p_com_db);
   uint8 gchip = 0;
   int ret = 0;

   sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));

   if (!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
   {
       ret = sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,  &(p_nhinfo->hdr.dsfwd_info.dsfwd_offset));
   }
   if (!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
   {
       dsfwd_param.dest_chipid = SYS_MAP_GPORT_TO_GCHIP(p_nhinfo->gport);
       if (CTC_FLAG_ISSET(p_nhinfo->flag, SYS_NH_IPUC_FLAG_PW_BFD_PLD_ROUTE))
       {
           sys_greatbelt_get_gchip_id(lchip, &gchip);
           dsfwd_param.dest_chipid = gchip;
       }
       dsfwd_param.dest_id = CTC_MAP_GPORT_TO_LPORT(p_nhinfo->gport);
   }
   else
   {
       dsfwd_param.dest_id = (p_nhinfo->gport);
       dsfwd_param.aps_type = 3;
   }

   if (CTC_FLAG_ISSET(p_nhinfo->flag, SYS_NH_IPUC_FLAG_STATS_EN))
   {
       uint16 stats_ptr = 0;
       ret = sys_greatbelt_stats_get_statsptr(lchip, p_nhinfo->stats_id, &stats_ptr);
       dsfwd_param.stats_ptr = stats_ptr;
   }
   dsfwd_param.is_mcast = 0;
   dsfwd_param.drop_pkt  = CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
   dsfwd_param.bypass_ingress_edit = CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE);
   dsfwd_param.dsnh_offset = p_nhinfo->hdr.dsfwd_info.dsnh_offset;
   dsfwd_param.dsfwd_offset  = p_nhinfo->hdr.dsfwd_info.dsfwd_offset;
   if (CTC_IS_CPU_PORT(p_nhinfo->gport))
   {
       dsfwd_param.is_to_cpu = 1;
   }
   /*Write table*/
   ret = ret ? ret : sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param);


    if (ret != CTC_E_NONE)
    {
        /*Free DsFwd offset*/
        sys_greatbelt_nh_offset_free(lchip,  SYS_NH_ENTRY_TYPE_FWD, 1, p_nhinfo->hdr.dsfwd_info.dsfwd_offset);
    }
    else
    {
        CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
    }
    return ret;

}
STATIC int32
_sys_greatbelt_nh_update_ipuc_fwd_attr(uint8 lchip, sys_nh_param_ipuc_t* p_nhpara, sys_nh_info_ipuc_t* p_nhinfo)
{
    uint8 gchip = 0;
    int ret = 0;
    sys_nh_param_dsfwd_t dsfwd_param;
    uint8 bfd_reserve_port = 0;
    uint8 edit_dsnh = 1;

    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));
    _sys_greatbelt_nh_free_ipuc_nh_resource(lchip, p_nhinfo);

    gchip = SYS_MAP_GPORT_TO_GCHIP(p_nhpara->oif.gport);

    bfd_reserve_port = ((SYS_RESERVE_PORT_MPLS_BFD == CTC_MAP_GPORT_TO_LPORT(p_nhpara->oif.gport))
    || (SYS_RESERVE_PORT_PW_VCCV_BFD == CTC_MAP_GPORT_TO_LPORT(p_nhpara->oif.gport)));

    if (p_nhpara->merge_dsfwd)
    {
       CTC_SET_FLAG(p_nhinfo->flag, SYS_NH_IPUC_FLAG_MERGE_DSFWD);
    }
    else
    {
       CTC_UNSET_FLAG(p_nhinfo->flag, SYS_NH_IPUC_FLAG_MERGE_DSFWD);
    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE)
        && !p_nhpara->aps_en)
    {
        if (SYS_GCHIP_IS_REMOTE(lchip, gchip)
            && (!bfd_reserve_port))
        {
            edit_dsnh  = 0;
        }
    }

    if (edit_dsnh)
    {
        ret = _sys_greatbelt_nh_ipuc_update_dsnh(lchip, p_nhpara, p_nhinfo, TRUE);
        if (p_nhpara->aps_en
            && (!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH)))
        {
            ret =  ret ? ret: _sys_greatbelt_nh_ipuc_update_dsnh(lchip, p_nhpara, p_nhinfo, FALSE);
        }
        if (ret)
        {
            return ret;
        }
    }

    if(!p_nhpara->aps_en)
    {
        sys_l3if_prop_t l3if_prop;
        sal_memset(&l3if_prop, 0, sizeof(sys_l3if_prop_t));
        if (!(CTC_IS_CPU_PORT(p_nhpara->oif.gport) || p_nhpara->oif.is_l2_port))
        {
           ret = sys_greatbelt_l3if_get_l3if_info_with_port_and_vlan(lchip, p_nhpara->oif.gport,
                                                                                 p_nhpara->oif.vid, &l3if_prop);
        }
        p_nhinfo->l3ifid  = l3if_prop.l3if_id;
        p_nhinfo->gport = p_nhpara->oif.gport;
    }
    else
    {
        p_nhinfo->gport = p_nhpara->aps_bridge_group_id;
        p_nhinfo->l3ifid = 0;
        CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN);
    }

    if(ret != CTC_E_NONE)
    {
         goto error_proc;
    }
    if (!p_nhpara->hdr.have_dsfwd)
    {
        return ret ;
    }
   ret = _sys_greatbelt_nh_ipuc_add_dsfwd(lchip, (sys_nh_info_com_t*) p_nhinfo);
error_proc:
    if(ret != CTC_E_NONE)
    {
        _sys_greatbelt_nh_free_ipuc_nh_resource(lchip, p_nhinfo);
    }
    return ret;
}

int32
sys_greatbelt_nh_create_ipuc_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                sys_nh_info_com_t* p_com_db)
{
    int32 ret = CTC_E_NONE;
    sys_nh_param_ipuc_t* p_nh_para;
    sys_nh_info_ipuc_t* p_nhdb;


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_com_nh_para);
    CTC_PTR_VALID_CHECK(p_com_db);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_IPUC, p_com_nh_para->hdr.nh_param_type);
    p_nh_para = (sys_nh_param_ipuc_t*)(p_com_nh_para);
    p_nhdb = (sys_nh_info_ipuc_t*)(p_com_db);

    p_nhdb->hdr.nh_entry_type = SYS_NH_TYPE_IPUC;

    if(p_nh_para->aps_en)
    {
        /*protection path -- */
        p_nhdb->protection_path  = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_info_ipuc_edit_info_t));
        if (!p_nhdb->protection_path)
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_nhdb->protection_path, 0, sizeof(sys_nh_info_ipuc_edit_info_t));
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "dsnh_offset:%d  gport:0x%x vid:%d \n",
                   p_nh_para->dsnh_offset, p_nh_para->oif.gport, p_nh_para->oif.vid);

    /*Create unresolved ipuc nh*/
    if (p_nh_para->is_unrov_nh)
    {
        sys_l3if_prop_t l3if_prop;
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);

        sal_memset(&l3if_prop, 0, sizeof(sys_l3if_prop_t));
        if (!(CTC_IS_CPU_PORT(p_nh_para->oif.gport) || p_nh_para->oif.is_l2_port))
        {
            CTC_ERROR_GOTO(sys_greatbelt_l3if_get_l3if_info_with_port_and_vlan(lchip, p_nh_para->oif.gport,
                                                                                 p_nh_para->oif.vid, &l3if_prop), ret, error1);
        }
        p_nhdb->l3ifid  = l3if_prop.l3if_id;
        return CTC_E_NONE;
    }

    CTC_ERROR_GOTO(_sys_greatbelt_nh_update_ipuc_fwd_attr(lchip, p_nh_para, p_nhdb), ret, error1);

    return CTC_E_NONE;

    error1:
    if (p_nhdb->protection_path && p_nh_para->aps_en)
    {
        mem_free(p_nhdb->protection_path);
    }
    return ret;

}

/**
 @brief Callback function of delete unicast ip nexthop

 @param[in] p_com_db, pointer used to store nexthop data

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_delete_ipuc_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid)
{
    sys_nh_info_ipuc_t* p_nhinfo;
    sys_nh_ref_list_node_t* p_ref_node, * p_tmp_node;
    sys_nh_param_dsfwd_t dsfwd_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_data);
    CTC_PTR_VALID_CHECK(p_nhid);


    CTC_EQUAL_CHECK(SYS_NH_TYPE_IPUC, p_data->hdr.nh_entry_type);
    p_nhinfo = (sys_nh_info_ipuc_t*)(p_data);

    sys_greatbelt_nh_update_ecmp_member(lchip, *p_nhid, SYS_NH_CHANGE_TYPE_NH_DELETE);

    /*1. Delete ecmp  list */
    p_ref_node = p_nhinfo->hdr.p_ref_nh_list;

    while (p_ref_node)
    {
        /*Remove from db*/
        p_tmp_node = p_ref_node;
        p_ref_node = p_ref_node->p_next;
        mem_free(p_tmp_node);
    }


    /*2. Delete this ipuc nexthop*/
    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        dsfwd_param.dsfwd_offset = p_nhinfo->hdr.dsfwd_info.dsfwd_offset;
        dsfwd_param.drop_pkt = 1;
        /*Write table*/
        sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param);

        /*Free DsFwd offset*/
        sys_greatbelt_nh_offset_free(lchip,  SYS_NH_ENTRY_TYPE_FWD, 1, p_nhinfo->hdr.dsfwd_info.dsfwd_offset);
    }

    _sys_greatbelt_nh_free_ipuc_nh_resource(lchip, p_nhinfo);
    mem_free(p_nhinfo->protection_path);
    CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_update_ipuc_fwd_to_unrov(uint8 lchip, sys_nh_param_ipuc_t* p_nhpara, sys_nh_info_ipuc_t* p_nhinfo)
{

    sys_nh_param_special_t nh_para_spec;


    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        sal_memset(&nh_para_spec, 0, sizeof(sys_nh_param_special_t));

        nh_para_spec.hdr.have_dsfwd= TRUE;
        nh_para_spec.hdr.nh_param_type = SYS_NH_TYPE_UNROV;
        nh_para_spec.hdr.is_internal_nh = TRUE;

        /*1.2 update dsfwd to unrov nh's dsfwd, write dsfwd*/
        CTC_ERROR_RETURN(sys_greatbelt_nh_create_special_cb(lchip, (sys_nh_param_com_t*)(&nh_para_spec), (sys_nh_info_com_t*)(p_nhinfo)));

    }
     CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
     _sys_greatbelt_nh_free_ipuc_nh_resource(lchip, p_nhinfo);
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_mapping_to_nhinfo(uint8 lchip, sys_nh_info_ipuc_t* p_nh_info,
                                   sys_nh_info_dsnh_t* p_dsnh_info)
{
    p_dsnh_info->aps_en       = CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN);
    if (p_dsnh_info->aps_en)
    {
        p_dsnh_info->dest_id = p_nh_info->gport;
    }
    else
    {
        p_dsnh_info->dest_chipid  = SYS_MAP_GPORT_TO_GCHIP(p_nh_info->gport);
        p_dsnh_info->dest_id     = CTC_MAP_GPORT_TO_LPORT(p_nh_info->gport);
    }
    p_dsnh_info->dsfwd_valid  = CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
    p_dsnh_info->nexthop_ext = CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
    p_dsnh_info->is_mcast   = 0;
    p_dsnh_info->drop_pkt   = CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);

    p_dsnh_info->dsnh_offset = p_nh_info->hdr.dsfwd_info.dsnh_offset;


    return CTC_E_NONE;

}

/**
 @brief Callback function used to update ipuc nexthop

 @param[in] p_nh_ptr, pointer of ipuc nexthop DB

 @param[in] p_para, member information

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_update_ipuc_cb(uint8 lchip, sys_nh_info_com_t* p_nh_db,
                                sys_nh_param_com_t* p_para)
{
    sys_nh_info_ipuc_t* p_nh_info;
    sys_nh_param_ipuc_t* p_nh_para;
    sys_nh_info_dsnh_t        dsnh_info;
    ds_fwd_t dsfwd;
    uint16 stats_ptr = 0;
    int32 ret = CTC_E_NONE;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_para);
    CTC_PTR_VALID_CHECK(p_nh_db);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_IPUC, p_para->hdr.nh_param_type);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_IPUC, p_nh_db->hdr.nh_entry_type);
    p_nh_info = (sys_nh_info_ipuc_t*)(p_nh_db);
    p_nh_para = (sys_nh_param_ipuc_t*)(p_para);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "change_type:%d  dsfwd_valid:%d \n",
                   p_nh_para->hdr.change_type, CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD));

    switch (p_nh_para->hdr.change_type)
    {
    case SYS_NH_CHANGE_TYPE_FWD_TO_UNROV:
        CTC_ERROR_RETURN(_sys_greatbelt_nh_update_ipuc_fwd_to_unrov(lchip, p_nh_para, p_nh_info));
        if (NULL != p_nh_info->updateAd)
        {
            sys_greatbelt_nh_mapping_to_nhinfo(lchip, p_nh_info, &dsnh_info);
            p_nh_info->updateAd(lchip, p_nh_info->data, &dsnh_info);
        }
        CTC_SET_FLAG(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
        break;
    case SYS_NH_CHANGE_TYPE_UPDATE_FWD_ATTR:
         p_nh_para->is_dsl2edit  = CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
         p_nh_para->hdr.have_dsfwd = CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD)
                                        || (NULL != p_nh_info->hdr.p_ref_nh_list);


        CTC_ERROR_RETURN(_sys_greatbelt_nh_update_ipuc_fwd_attr(lchip, p_nh_para, p_nh_info));
        if (NULL != p_nh_info->updateAd)
        {
            sys_greatbelt_nh_mapping_to_nhinfo(lchip, p_nh_info, &dsnh_info);
            p_nh_info->updateAd(lchip, p_nh_info->data, &dsnh_info);
        }
        break;
    case SYS_NH_CHANGE_TYPE_UNROV_TO_FWD:
        if (!CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
        {
            return CTC_E_NH_ISNT_UNROV;
        }

        CTC_UNSET_FLAG(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
        p_nh_para->is_unrov_nh = FALSE;
        p_nh_para->hdr.have_dsfwd = CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD)
                                    || (NULL != p_nh_info->hdr.p_ref_nh_list);


        if (CTC_FLAG_ISSET(p_nh_info->flag, SYS_NH_IPUC_FLAG_LOOP_NH))
        {
          p_nh_para->loop_nhid = p_nh_info->loop_nhid;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_nh_update_ipuc_fwd_attr(lchip, p_nh_para, p_nh_info));
        if (NULL != p_nh_info->updateAd)
        {
            sys_greatbelt_nh_mapping_to_nhinfo(lchip, p_nh_info, &dsnh_info);
            p_nh_info->updateAd(lchip, p_nh_info->data, &dsnh_info);
        }
        break;
    case SYS_NH_CHANGE_TYPE_ADD_DYNTBL:
    {
        if (CTC_FLAG_ISSET(p_nh_db->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
        {
            if (p_nh_para->hdr.stats_valid && CTC_FLAG_ISSET(p_nh_info->flag,SYS_NH_IPUC_FLAG_STATS_EN))
            {
                return CTC_E_INVALID_CONFIG;
            }
            else if (p_nh_para->hdr.stats_valid)
            {
                sal_memset(&dsfwd, 0, sizeof(ds_fwd_t));
                ret = ret ? ret : sys_greatbelt_stats_get_statsptr(lchip, p_nh_para->hdr.stats_id, &stats_ptr);
                ret = ret ? ret : sys_greatbelt_nh_get_entry_dsfwd(lchip, p_nh_info->hdr.dsfwd_info.dsfwd_offset, &dsfwd);
                dsfwd.stats_ptr_low = stats_ptr & 0xFFF;
                dsfwd.stats_ptr_high = (stats_ptr >> 12) & 0xF;
                ret = ret ? ret : sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_FWD, p_nh_info->hdr.dsfwd_info.dsfwd_offset, &dsfwd);

                if (CTC_E_NONE == ret)
                {
                    CTC_SET_FLAG(p_nh_info->flag, SYS_NH_IPUC_FLAG_STATS_EN);
                    p_nh_info->stats_id = p_nh_para->hdr.stats_id;
                }
            }
            else
            {
                return CTC_E_NONE;
            }
        }
        else
        {
            if (p_nh_para->hdr.stats_valid)
            {
                CTC_SET_FLAG(p_nh_info->flag,SYS_NH_IPUC_FLAG_STATS_EN);
                p_nh_info->stats_id = p_nh_para->hdr.stats_id;
            }
            _sys_greatbelt_nh_ipuc_add_dsfwd(lchip, p_nh_db);
        }
        break;
    }
    default:
        return CTC_E_INVALID_PARAM;
    }
   if (p_nh_info->hdr.p_ref_nh_list)
   {
       sys_greatbelt_nh_update_ecmp_member(lchip, p_nh_para->hdr.nhid, p_nh_para->hdr.change_type);
   }

   CTC_ERROR_RETURN(sys_greatbelt_nh_update_eloop_edit(lchip, p_nh_para->hdr.nhid));

    return ret;
}

STATIC int32
_sys_greatbelt_nh_arp_free_resource(uint8 lchip, uint16 arp_id,  sys_nh_db_arp_t* p_arp)
{

    if (p_arp->offset)
    {
        sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 1, p_arp->offset);
    }

    sys_greatbelt_nh_remove_arp_id(lchip, arp_id);
    mem_free(p_arp);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_build_arp_info(uint8 lchip, sys_nh_db_arp_t* p_arp, ctc_nh_nexthop_mac_param_t* p_param)
{
    /*db*/
    if (CTC_FLAG_ISSET(p_param->flag, CTC_NH_NEXTHOP_MAC_DISCARD))
    {
        CTC_SET_FLAG(p_arp->flag, SYS_NH_ARP_FLAG_DROP);
    }
    else
    {
        CTC_UNSET_FLAG(p_arp->flag, SYS_NH_ARP_FLAG_DROP);
    }

    if (CTC_FLAG_ISSET(p_param->flag, CTC_NH_NEXTHOP_MAC_REDIRECT_TO_CPU))
    {
        CTC_SET_FLAG(p_arp->flag, SYS_NH_ARP_FLAG_REDIRECT_TO_CPU);
    }
    else
    {
        CTC_UNSET_FLAG(p_arp->flag, SYS_NH_ARP_FLAG_REDIRECT_TO_CPU);
    }

    sal_memcpy(p_arp->mac_da, p_param->mac, sizeof(mac_addr_t));

    if (CTC_FLAG_ISSET(p_param->flag, CTC_NH_NEXTHOP_MAC_VLAN_VALID))
    {
        CTC_SET_FLAG(p_arp->flag, SYS_NH_ARP_VLAN_VALID);
        p_arp->output_vid = p_param->vlan_id;
        p_arp->output_vlan_is_svlan = 1;
    }
    else
    {
        CTC_UNSET_FLAG(p_arp->flag, SYS_NH_ARP_VLAN_VALID);
    }

    p_arp->packet_type = 0;
    /*hw*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_arp(lchip, p_arp));
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_add_nexthop_mac(uint8 lchip, uint16 arp_id, ctc_nh_nexthop_mac_param_t* p_param)
{
    int32 ret = CTC_E_NONE;

    sys_nh_db_arp_t   arp_info;
    sys_nh_db_arp_t*  p_arp = NULL;

    CTC_PTR_VALID_CHECK(p_param);

    if ( (CTC_FLAG_ISSET(p_param->flag, CTC_NH_NEXTHOP_MAC_DISCARD)
            + CTC_FLAG_ISSET(p_param->flag, CTC_NH_NEXTHOP_MAC_REDIRECT_TO_CPU)) > 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&arp_info, 0, sizeof(sys_nh_db_arp_t));

    ret = sys_greatbelt_nh_lkup_arp_id(lchip, arp_id, &p_arp);
    if (CTC_E_EXCEED_MAX_SIZE == ret)
    {
        return ret;
    }

    if (NULL == p_arp)
    {
        p_arp  = (sys_nh_db_arp_t*)mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_db_arp_t));
        if (NULL == p_arp)
        {
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_arp, 0, sizeof(sys_nh_db_arp_t));

        /*alloc*/
        ret = sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 1, &p_arp->offset);
        if (CTC_E_NONE != ret)
        {
            goto error_proc;
        }

        ret = _sys_greatbelt_nh_build_arp_info(lchip, p_arp, p_param);
        if (CTC_E_NONE != ret)
        {
           goto error_proc;
        }

        ret = sys_greatbelt_nh_add_arp_id(lchip, arp_id, p_arp);
        if (ret == CTC_E_NONE)
        {
            return CTC_E_NONE;
        }
    }
    else
    {
        return CTC_E_ENTRY_EXIST;
    }
error_proc:

    _sys_greatbelt_nh_arp_free_resource(lchip, arp_id,  p_arp);
    return ret;

}

/**
 @brief Remove Next Hop Router Mac
*/
int32
sys_greatbelt_nh_remove_nexthop_mac(uint8 lchip, uint16 arp_id)
{
    sys_nh_db_arp_t*  p_arp = NULL;

    CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_arp_id(lchip, arp_id, &p_arp));

    if (p_arp && p_arp->ref_cnt != 0)
    {
        return CTC_E_NH_EXIST;
    }

    if (!p_arp)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    _sys_greatbelt_nh_arp_free_resource(lchip, arp_id,  p_arp);
    return CTC_E_NONE;
}

/**
 @brief Update Next Hop Router Mac
*/
int32
sys_greatbelt_nh_update_nexthop_mac(uint8 lchip, uint16 arp_id, ctc_nh_nexthop_mac_param_t* p_param)
{
    sys_nh_db_arp_t   arp_info;
    sys_nh_db_arp_t*  p_arp = NULL;

    CTC_PTR_VALID_CHECK(p_param);

    if ( (CTC_FLAG_ISSET(p_param->flag, CTC_NH_NEXTHOP_MAC_DISCARD)
            + CTC_FLAG_ISSET(p_param->flag, CTC_NH_NEXTHOP_MAC_REDIRECT_TO_CPU)) > 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&arp_info, 0, sizeof(sys_nh_db_arp_t));

    CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_arp_id(lchip, arp_id, &p_arp));

    if (NULL == p_arp)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_nh_build_arp_info(lchip, p_arp, p_param));
    }
   return CTC_E_NONE;

}


