/**
 @file sys_goldengate_nexthop_l3.c

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
#include "sys_goldengate_chip.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_nexthop_hw.h"
#include "sys_goldengate_vlan.h"
#include "sys_goldengate_l3if.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"


int32
_sys_goldengate_nh_ipuc_update_dsnh_cb(uint8 lchip, uint32 nh_offset, void* p_arp_db)
{
    sys_nexthop_t sys_dsnh;

    sys_nh_db_arp_t*  p_arp = NULL;

    sal_memset(&sys_dsnh, 0, sizeof(sys_nexthop_t));

    p_arp = p_arp_db;

    if(!p_arp->in_l2_offset)
    {
        if(CTC_FLAG_ISSET(p_arp->flag, SYS_NH_ARP_FLAG_DROP))
        {
            sys_dsnh.update_type = 2;
        }
        else
        {
            sys_dsnh.update_type = 1;
            sal_memcpy(sys_dsnh.mac_da, p_arp->mac_da, sizeof(mac_addr_t));
        }
        sys_dsnh.offset = nh_offset;
        sys_goldengate_nh_update_asic_table(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, p_arp->nh_offset, &sys_dsnh);
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_nh_ipuc_update_dsnh(uint8 lchip, sys_nh_param_ipuc_t* p_nhpara,sys_nh_info_ipuc_t* p_nhdb, bool working_path)
{
    sys_l3if_prop_t l3if_prop;
    sys_nh_param_dsnh_t dsnh_param;
    ctc_nh_oif_info_t *p_oif = NULL;
    sys_nh_db_dsl2editeth4w_t   dsl2edit_4w;
    sys_nh_db_dsl2editeth4w_t*  p_dsl2edit_4w = NULL;
    sys_nh_db_dsl2editeth8w_t   dsl2edit_8w;
    sys_nh_db_dsl2editeth8w_t*  p_dsl2edit_8w  = NULL;
    ctc_vlan_egress_edit_info_t  vlan_edit;
    uint16 arp_id = 0;
    mac_addr_t mac = {0};
    uint8 l2edit_8w = 0;


    uint8 use_l2edit = p_nhpara->have_dsl2edit;
    sal_memset(&dsnh_param, 0, sizeof(sys_nh_param_dsnh_t));
    sal_memset(&dsl2edit_4w, 0, sizeof(dsl2edit_4w));
    sal_memset(&dsl2edit_8w, 0, sizeof(dsl2edit_8w));
    sal_memset(&l3if_prop, 0, sizeof(l3if_prop));

    p_oif = working_path ? &p_nhpara->oif : &p_nhpara->p_oif;
    arp_id = working_path ? p_nhpara->arp_id: p_nhpara->p_arp_id;
    if (!(CTC_IS_CPU_PORT(p_nhpara->oif.gport) || p_nhpara->oif.is_l2_port || SYS_IS_DROP_PORT(p_nhpara->oif.gport)))
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_get_l3if_info_with_port_and_vlan(lchip, p_oif->gport, p_oif->vid, &l3if_prop));
    }

    if (l3if_prop.l3if_type == CTC_L3IF_TYPE_PHY_IF && p_oif->vid)
    {
        sal_memset(&vlan_edit, 0, sizeof(ctc_vlan_egress_edit_info_t));
        dsl2edit_4w.output_vid = p_oif->vid;
        use_l2edit = 1;
    }

    if (p_nhpara->fpma)
    {
        use_l2edit = 1;
        dsl2edit_4w.fpma = 1;
    }

    if(0 != sal_memcmp(p_nhpara->mac_sa, mac, sizeof(mac_addr_t)))/*edit mac sa, must use l2edit8w, for openflow*/
    {
        use_l2edit = 1;
        l2edit_8w  =1;
    }

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
    sal_memcpy(dsl2edit_4w.mac_da, dsnh_param.macDa, sizeof(mac_addr_t));

    if (0 != arp_id)
    {
        use_l2edit = 0;
    }

    if (use_l2edit)
    {
        if (l2edit_8w)
        {
            sal_memcpy(dsl2edit_8w.mac_da, p_nhpara->mac, sizeof(mac_addr_t));
            sal_memcpy(dsl2edit_8w.mac_sa, p_nhpara->mac_sa, sizeof(mac_addr_t));
            dsl2edit_8w.output_vid = p_oif->vid;

            p_dsl2edit_8w  = &dsl2edit_8w;
            CTC_ERROR_RETURN(sys_goldengate_nh_add_route_l2edit_8w(lchip, &p_dsl2edit_8w));
            p_nhdb->p_dsl2edit = (sys_nh_db_dsl2editeth4w_t*)p_dsl2edit_8w;
            p_nhdb->l2edit_8w = l2edit_8w;
            dsnh_param.l2edit_ptr  = p_dsl2edit_8w->offset;
        }
        else
        {
            p_dsl2edit_4w   = &dsl2edit_4w;
            CTC_ERROR_RETURN(sys_goldengate_nh_add_route_l2edit(lchip, &p_dsl2edit_4w));
            if (working_path)
            {
                p_nhdb->p_dsl2edit = p_dsl2edit_4w;
            }
            else
            {
                p_nhdb->protection_path->p_dsl2edit = p_dsl2edit_4w;
            }
            dsnh_param.l2edit_ptr  = p_dsl2edit_4w->offset;
        }
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
    }
    else
    {
        if (working_path)
        {
            sal_memcpy(p_nhdb->mac_da, p_nhpara->mac, sizeof(mac_addr_t));
        }
        else
        {
            sal_memcpy(p_nhdb->protection_path->mac_da, p_nhpara->p_mac, sizeof(mac_addr_t));
        }

        if (arp_id)
        {
            sys_nh_db_arp_t *p_arp = NULL;
            sys_nh_info_arp_param_t arp_parm;

            sal_memset(&arp_parm, 0, sizeof(sys_nh_info_arp_param_t));
            arp_parm.nh_entry_type  = SYS_NH_TYPE_IPUC;
            arp_parm.nh_offset      = dsnh_param.dsnh_offset;
            arp_parm.updateNhp      = (updatenh_fn)_sys_goldengate_nh_ipuc_update_dsnh_cb;
            arp_parm.p_oif          = p_oif;

            CTC_ERROR_RETURN(sys_goldengate_nh_bind_arp_cb(lchip, &arp_parm, arp_id));
            CTC_ERROR_RETURN(sys_goldengate_nh_lkup_arp_id(lchip, arp_id, &p_arp));

            dsnh_param.inner_l2edit_ptr  = p_arp->in_l2_offset;
            dsnh_param.is_drop           = CTC_FLAG_ISSET(p_arp->flag, SYS_NH_ARP_FLAG_DROP);
            sal_memcpy(dsnh_param.macDa, p_arp->mac_da, sizeof(mac_addr_t));
        }
    }

    if (working_path)
    {
        p_nhdb->l3ifid = l3if_prop.l3if_id;
        p_nhdb->arp_id = arp_id;
    }
    else
    {
        p_nhdb->protection_path->l3ifid = l3if_prop.l3if_id;
        p_nhdb->protection_path->arp_id = arp_id;
    }
    dsnh_param.dsnh_type = SYS_NH_PARAM_DSNH_TYPE_IPUC;
    dsnh_param.dest_vlan_ptr = l3if_prop.vlan_ptr;
    dsnh_param.flag  = p_nhdb->flag;
    dsnh_param.mtu_no_chk = p_nhpara->mtu_no_chk;

    if(p_nhpara->ttl_no_dec)
    {
        CTC_SET_FLAG(dsnh_param.flag, SYS_NH_PARAM_FLAG_ROUTE_NOTTL);
    }

    CTC_ERROR_RETURN(sys_goldengate_nh_write_entry_dsnh4w(lchip, &dsnh_param));

    return CTC_E_NONE;
}
int32
_sys_goldengate_nh_free_ipuc_nh_resource(uint8 lchip, sys_nh_info_ipuc_t* p_nhinfo)
{
    sys_nh_db_arp_t *p_arp = NULL;

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT))
    {
        if (p_nhinfo->l2edit_8w)
        {
            sys_goldengate_nh_remove_route_l2edit_8w(lchip, (sys_nh_db_dsl2editeth8w_t*)(p_nhinfo->p_dsl2edit));
            p_nhinfo->l2edit_8w = 0;
        }
        else
        {
            sys_goldengate_nh_remove_route_l2edit(lchip, p_nhinfo->p_dsl2edit);
        }

        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
        {
            sys_goldengate_nh_remove_route_l2edit(lchip, p_nhinfo->protection_path->p_dsl2edit);
        }
    }

    if(p_nhinfo->arp_id)
    {
        sys_goldengate_nh_lkup_arp_id(lchip, p_nhinfo->arp_id, &p_arp);
        if(p_arp)
        {
            p_arp->nh_offset = 0;
            p_arp->updateNhp = NULL;
        }
    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
    {
        if(p_nhinfo->protection_path->arp_id)
        {
            p_arp = NULL;
            sys_goldengate_nh_lkup_arp_id(lchip, p_nhinfo->protection_path->arp_id, &p_arp);
            if(p_arp)
            {
                p_arp->nh_offset = 0;
                p_arp->updateNhp = NULL;
            }
        }
    }

    CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
    CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN);

    return CTC_E_NONE;
}

int32
_sys_goldengate_nh_ipuc_add_dsfwd(uint8 lchip, sys_nh_info_com_t* p_com_db)
{
   sys_nh_param_dsfwd_t dsfwd_param;
   sys_nh_info_ipuc_t* p_nhinfo = (sys_nh_info_ipuc_t*)(p_com_db);
   int ret = 0;

   sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));
   if (!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
   {
       ret = sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,  &(p_nhinfo->hdr.dsfwd_info.dsfwd_offset));
   }

   if (!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
   {
       dsfwd_param.dest_chipid = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nhinfo->gport);
       dsfwd_param.dest_id = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_nhinfo->gport);
   }
   else
   {
       dsfwd_param.dest_id = p_nhinfo->gport;
       dsfwd_param.aps_type = 3;
   }
   dsfwd_param.nexthop_ext = CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
   dsfwd_param.is_mcast = 0;
   dsfwd_param.drop_pkt  = CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
   dsfwd_param.is_egress_edit =  CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE);
   dsfwd_param.dsnh_offset = p_nhinfo->hdr.dsfwd_info.dsnh_offset;
   dsfwd_param.dsfwd_offset  = p_nhinfo->hdr.dsfwd_info.dsfwd_offset;
   if (CTC_IS_CPU_PORT(p_nhinfo->gport))
   {
       dsfwd_param.is_cpu = 1;
   }

   if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LEN_ADJUST_EN))
   {
        sys_goldengate_lkup_adjust_len_index(lchip, p_nhinfo->hdr.adjust_len, &dsfwd_param.adjust_len_idx);
   }

   /*Write table*/
   ret = ret ? ret : sys_goldengate_nh_write_entry_dsfwd(lchip, &dsfwd_param);

   if (ret != CTC_E_NONE)
    {
      sys_goldengate_nh_offset_free(lchip,  SYS_NH_ENTRY_TYPE_FWD, 1, p_nhinfo->hdr.dsfwd_info.dsfwd_offset);

    }
    else
    {
        CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
    }
	return ret;
}
STATIC int32
_sys_goldengate_nh_update_ipuc_fwd_attr(uint8 lchip, sys_nh_param_ipuc_t* p_nhpara, sys_nh_info_ipuc_t* p_nhinfo)
{
    int ret = 0;
    uint8 edit_dsnh = 1;
    sys_nh_param_dsfwd_t dsfwd_param;
    uint8 gchip = 0;

    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));


    /*check arp id can not update*/
    if (p_nhinfo->arp_id && (p_nhpara->arp_id != p_nhinfo->arp_id))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if (p_nhpara->aps_en && p_nhinfo->protection_path)
    {
        if (p_nhinfo->protection_path->arp_id && (p_nhpara->p_arp_id != p_nhinfo->protection_path->arp_id))
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }
    }

    _sys_goldengate_nh_free_ipuc_nh_resource(lchip, p_nhinfo);

    if (p_nhpara->merge_dsfwd)
    {
       CTC_SET_FLAG(p_nhinfo->flag, SYS_NH_IPUC_FLAG_MERGE_DSFWD);
    }
    else
    {
       CTC_UNSET_FLAG(p_nhinfo->flag, SYS_NH_IPUC_FLAG_MERGE_DSFWD);
    }

    if (p_nhpara->adjust_length != 0)
    {
        CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LEN_ADJUST_EN);
        p_nhinfo->hdr.adjust_len = p_nhpara->adjust_length;
    }
    else
    {
        CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LEN_ADJUST_EN);
        p_nhinfo->hdr.adjust_len = 0;
    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE) && !p_nhpara->aps_en)
    {
        gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nhpara->oif.gport);

        if (SYS_GCHIP_IS_REMOTE(lchip, gchip))
        {
            edit_dsnh  = 0;
        }
    }
    if (edit_dsnh)
    {
        ret = _sys_goldengate_nh_ipuc_update_dsnh(lchip, p_nhpara, p_nhinfo, TRUE);
        if (p_nhpara->aps_en)
        {
            ret =  ret ? ret: _sys_goldengate_nh_ipuc_update_dsnh(lchip,  p_nhpara, p_nhinfo, FALSE);

        }
    }

    if(!p_nhpara->aps_en)
    {
        sys_l3if_prop_t l3if_prop;
        sal_memset(&l3if_prop, 0, sizeof(sys_l3if_prop_t));
        if(!(CTC_IS_CPU_PORT(p_nhpara->oif.gport) || p_nhpara->oif.is_l2_port || SYS_IS_DROP_PORT(p_nhpara->oif.gport)))
        {
           ret =  ret ? ret: sys_goldengate_l3if_get_l3if_info_with_port_and_vlan(lchip, p_nhpara->oif.gport,
                                                                                 p_nhpara->oif.vid, &l3if_prop);
        }
        p_nhinfo->l3ifid  = l3if_prop.l3if_id;
        p_nhinfo->gport = p_nhpara->oif.gport;
    }
    else
    {
        p_nhinfo->gport = p_nhpara->aps_bridge_group_id;
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
    ret = _sys_goldengate_nh_ipuc_add_dsfwd(lchip, (sys_nh_info_com_t*) p_nhinfo);
error_proc:
    if(ret != CTC_E_NONE)
    {
        _sys_goldengate_nh_free_ipuc_nh_resource(lchip, p_nhinfo);
    }
    return ret;
}

int32
sys_goldengate_nh_create_ipuc_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                sys_nh_info_com_t* p_com_db)
{
    sys_nh_param_ipuc_t* p_nh_para;
    sys_nh_info_ipuc_t* p_nhdb;
	int32 ret = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_com_nh_para);
    CTC_PTR_VALID_CHECK(p_com_db);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_IPUC, p_com_nh_para->hdr.nh_param_type);
    p_nh_para = (sys_nh_param_ipuc_t*)(p_com_nh_para);
    p_nhdb = (sys_nh_info_ipuc_t*)(p_com_db);

    p_nhdb->hdr.nh_entry_type = SYS_NH_TYPE_IPUC;
    p_nhdb->gport = p_nh_para->oif.gport;
    sal_memcpy(p_nhdb->mac_da, p_nh_para->mac, sizeof(mac_addr_t));

    if(p_nh_para->aps_en)
    {
        /*protection path -- */
        p_nhdb->protection_path  = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_info_ipuc_edit_info_t));
        if (!p_nhdb->protection_path)
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_nhdb->protection_path, 0, sizeof(sys_nh_info_ipuc_edit_info_t));
        sal_memcpy(p_nhdb->protection_path->mac_da, p_nh_para->p_mac, sizeof(mac_addr_t));
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "dsnh_offset:%d  gport:0x%x vid:%d \n",
                   p_nh_para->dsnh_offset, p_nh_para->oif.gport, p_nh_para->oif.vid);

    /*Create unresolved ipuc nh*/
    if (p_nh_para->is_unrov_nh)
    {
        sys_l3if_prop_t l3if_prop;
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);

        sal_memset(&l3if_prop, 0, sizeof(sys_l3if_prop_t));
        if (!(CTC_IS_CPU_PORT(p_nh_para->oif.gport) || p_nh_para->oif.is_l2_port || SYS_IS_DROP_PORT(p_nh_para->oif.gport)))
        {
            CTC_ERROR_RETURN(sys_goldengate_l3if_get_l3if_info_with_port_and_vlan(lchip, p_nh_para->oif.gport, p_nh_para->oif.vid, &l3if_prop));
        }

        p_nhdb->l3ifid  = l3if_prop.l3if_id;

        return CTC_E_NONE;
    }

    CTC_ERROR_GOTO(_sys_goldengate_nh_update_ipuc_fwd_attr(lchip, p_nh_para, p_nhdb),ret, error1);
 error1:
    if ((ret != CTC_E_NONE) && p_nhdb->protection_path && p_nh_para->aps_en)
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
sys_goldengate_nh_delete_ipuc_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid)
{
    sys_nh_info_ipuc_t* p_nhinfo;
    sys_nh_ref_list_node_t* p_ref_node, * p_tmp_node;
    sys_nh_param_dsfwd_t dsfwd_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_data);
    CTC_PTR_VALID_CHECK(p_nhid);

    CTC_EQUAL_CHECK(SYS_NH_TYPE_IPUC, p_data->hdr.nh_entry_type);
    p_nhinfo = (sys_nh_info_ipuc_t*)(p_data);

    sys_goldengate_nh_update_ecmp_member(lchip, *p_nhid, SYS_NH_CHANGE_TYPE_NH_DELETE);

    /*1. Delete ecmp  list */
    p_ref_node = p_nhinfo->hdr.p_ref_nh_list;

    while (p_ref_node)
    {
        /*Remove from db*/
        p_tmp_node = p_ref_node;
        p_ref_node = p_ref_node->p_next;
        mem_free(p_tmp_node);
    }

    p_nhinfo->hdr.p_ref_nh_list = NULL;

    /*2. Delete this ipuc nexthop*/
    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));


    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        dsfwd_param.dsfwd_offset = p_nhinfo->hdr.dsfwd_info.dsfwd_offset;

        dsfwd_param.drop_pkt = 1;
        /*Write table*/
        sys_goldengate_nh_write_entry_dsfwd(lchip, &dsfwd_param);

        /*Free DsFwd offset*/
        sys_goldengate_nh_offset_free(lchip,  SYS_NH_ENTRY_TYPE_FWD, 1, p_nhinfo->hdr.dsfwd_info.dsfwd_offset);
    }

     if (NULL != p_nhinfo->updateAd)
      {
             sys_nh_info_dsnh_t dsnh_info;
             dsnh_info.drop_pkt =1;
             p_nhinfo->updateAd(lchip, p_nhinfo->data, &dsnh_info);
     }

    CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
	_sys_goldengate_nh_free_ipuc_nh_resource(lchip, p_nhinfo);
	mem_free(p_nhinfo->protection_path);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_update_ipuc_fwd_to_unrov(uint8 lchip, sys_nh_param_ipuc_t* p_nhpara, sys_nh_info_ipuc_t* p_nhinfo)
{

    sys_nh_param_special_t nh_para_spec;

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        sal_memset(&nh_para_spec, 0, sizeof(sys_nh_param_special_t));
        nh_para_spec.hdr.have_dsfwd= TRUE;
        nh_para_spec.hdr.nh_param_type = SYS_NH_TYPE_UNROV;
        nh_para_spec.hdr.is_internal_nh = TRUE;
        /*1.2 update dsfwd to unrov nh's dsfwd, write dsfwd*/
        CTC_ERROR_RETURN(sys_goldengate_nh_create_special_cb(lchip, (sys_nh_param_com_t*)(&nh_para_spec), (sys_nh_info_com_t*)(p_nhinfo)));

    }
    CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
    _sys_goldengate_nh_free_ipuc_nh_resource(lchip, p_nhinfo);
    return CTC_E_NONE;
}


/**
 @brief Callback function used to update ipuc nexthop
 */
int32
sys_goldengate_nh_update_ipuc_cb(uint8 lchip, sys_nh_info_com_t* p_nh_db,
                                sys_nh_param_com_t* p_para)
{
    sys_nh_info_ipuc_t* p_nh_info;
    sys_nh_param_ipuc_t* p_nh_para;
    sys_nh_info_dsnh_t        dsnh_info;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_para);
    CTC_PTR_VALID_CHECK(p_nh_db);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_IPUC, p_para->hdr.nh_param_type);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_IPUC, p_nh_db->hdr.nh_entry_type);
    p_nh_info = (sys_nh_info_ipuc_t*)(p_nh_db);
    p_nh_para = (sys_nh_param_ipuc_t*)(p_para);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "change_type:%d  have_dsfwd:%d \n",
                   p_nh_para->hdr.change_type, CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD));

    sal_memset(&dsnh_info, 0, sizeof(sys_nh_info_dsnh_t));
    switch (p_nh_para->hdr.change_type)
    {
    case SYS_NH_CHANGE_TYPE_FWD_TO_UNROV:

        CTC_ERROR_RETURN(_sys_goldengate_nh_update_ipuc_fwd_to_unrov(lchip, p_nh_para, p_nh_info));
        if (NULL != p_nh_info->updateAd)
        {
            sys_goldengate_nh_mapping_to_nhinfo(lchip, (sys_nh_info_com_t*)p_nh_info, &dsnh_info);
            p_nh_info->updateAd(lchip, p_nh_info->data, &dsnh_info);
        }
		CTC_SET_FLAG(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);

        break;
    case SYS_NH_CHANGE_TYPE_UPDATE_FWD_ATTR:

        p_nh_para->hdr.have_dsfwd  = CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD)
                               || (NULL != p_nh_info->hdr.p_ref_nh_list);
        p_nh_para->have_dsl2edit  = CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);

        CTC_ERROR_RETURN(_sys_goldengate_nh_update_ipuc_fwd_attr(lchip, p_nh_para, p_nh_info));
        if (NULL != p_nh_info->updateAd)
        {
            sys_goldengate_nh_mapping_to_nhinfo(lchip, (sys_nh_info_com_t*)p_nh_info, &dsnh_info);
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
        p_nh_para->hdr.have_dsfwd  = CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD)
		                            || (NULL != p_nh_info->hdr.p_ref_nh_list);


         CTC_ERROR_RETURN(_sys_goldengate_nh_update_ipuc_fwd_attr(lchip, p_nh_para, p_nh_info));
        if (NULL != p_nh_info->updateAd)
        {
            sys_goldengate_nh_mapping_to_nhinfo(lchip,(sys_nh_info_com_t* )p_nh_info, &dsnh_info);
            p_nh_info->updateAd(lchip, p_nh_info->data, &dsnh_info);
        }


        break;

    case SYS_NH_CHANGE_TYPE_ADD_DYNTBL:
        {
           if (CTC_FLAG_ISSET(p_nh_db->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
           {
                return CTC_E_NONE;
           }
           _sys_goldengate_nh_ipuc_add_dsfwd(lchip, p_nh_db);
        }
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }
   if (p_nh_info->hdr.p_ref_nh_list)
   {
       sys_goldengate_nh_update_ecmp_member(lchip, p_nh_para->hdr.nhid, p_nh_para->hdr.change_type);
   }

   CTC_ERROR_RETURN(sys_goldengate_nh_update_eloop_edit(lchip, p_nh_para->hdr.nhid));

    return CTC_E_NONE;
}

#define ___ARP___

STATIC int32
_sys_goldengate_nh_arp_free_resource(uint8 lchip, uint16 arp_id,  sys_nh_db_arp_t* p_arp)
{

    if (p_arp->offset)
    {
        sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 1, p_arp->offset);
    }
    if (p_arp->in_l2_offset)
    {
        sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_3W, 1, p_arp->in_l2_offset);
    }

    sys_goldengate_nh_remove_arp_id(lchip, arp_id);
    mem_free(p_arp);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_build_arp_info(uint8 lchip, sys_nh_db_arp_t* p_arp, ctc_nh_nexthop_mac_param_t* p_param)
{

    /*db*/
    if (CTC_FLAG_ISSET(p_param->flag, CTC_NH_NEXTHOP_MAC_ECMP_IF_ARP))
    {
        CTC_SET_FLAG(p_arp->flag, SYS_NH_ARP_ECMP_IF);
        p_arp->gport = p_param->gport;
    }
    else
    {
        CTC_UNSET_FLAG(p_arp->flag, SYS_NH_ARP_ECMP_IF);
    }


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
    return CTC_E_NONE;
}


/**
 @brief Add Next Hop Router Mac
*/
int32
sys_goldengate_nh_add_nexthop_mac(uint8 lchip, uint16 arp_id, ctc_nh_nexthop_mac_param_t* p_param)
{
    int32 ret = CTC_E_NONE;

    sys_nh_db_arp_t   arp_info;
    sys_nh_db_arp_t*  p_arp = NULL;

    CTC_PTR_VALID_CHECK(p_param);

    /*check flag*/
    if ( (CTC_FLAG_ISSET(p_param->flag, CTC_NH_NEXTHOP_MAC_DISCARD)
            + CTC_FLAG_ISSET(p_param->flag, CTC_NH_NEXTHOP_MAC_REDIRECT_TO_CPU)
            +  CTC_FLAG_ISSET(p_param->flag, CTC_NH_NEXTHOP_MAC_ECMP_IF_ARP)) > 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (arp_id < 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&arp_info, 0, sizeof(sys_nh_db_arp_t));

    ret = sys_goldengate_nh_lkup_arp_id(lchip, arp_id, &p_arp);
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
        ret = _sys_goldengate_nh_build_arp_info(lchip, p_arp, p_param);

        if (CTC_E_NONE != ret)
        {
           goto error_proc;
        }

        ret = sys_goldengate_nh_add_arp_id(lchip, arp_id, p_arp);
        if (CTC_E_NONE != ret)
        {
           goto error_proc;
        }

        if (p_param->flag & CTC_NH_NEXTHOP_MAC_ECMP_IF_ARP)
        {
            ret = sys_goldengate_nh_write_entry_arp(lchip, p_arp);
            if (ret == CTC_E_NONE)
            {
                return CTC_E_NONE;
            }
        }

        return CTC_E_NONE;

    }
    else
    {
        return CTC_E_ENTRY_EXIST;
    }
error_proc:

    _sys_goldengate_nh_arp_free_resource(lchip, arp_id,  p_arp);
    return ret;

}

/**
 @brief Remove Next Hop Router Mac
*/
int32
sys_goldengate_nh_remove_nexthop_mac(uint8 lchip, uint16 arp_id)
{
    sys_nh_db_arp_t*  p_arp = NULL;

    if (arp_id < 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_goldengate_nh_lkup_arp_id(lchip, arp_id, &p_arp));

    if (!p_arp)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if(p_arp->nh_offset)
    {
        return CTC_E_NH_HR_NH_IN_USE;
    }

    if (CTC_FLAG_ISSET(p_arp->flag , SYS_NH_ARP_ECMP_IF))
    {
        sal_memset(p_arp->mac_da, 0, sizeof(mac_addr_t));
        sys_goldengate_nh_write_entry_arp(lchip, p_arp);
    }

    _sys_goldengate_nh_arp_free_resource(lchip, arp_id,  p_arp);
    return CTC_E_NONE;

}

/**
 @brief Update Next Hop Router Mac
*/
int32
sys_goldengate_nh_update_nexthop_mac(uint8 lchip, uint16 arp_id, ctc_nh_nexthop_mac_param_t* p_new_param)
{
    sys_nh_db_arp_t*  p_arp = NULL;

    CTC_PTR_VALID_CHECK(p_new_param);

    if ( (CTC_FLAG_ISSET(p_new_param->flag, CTC_NH_NEXTHOP_MAC_DISCARD)
            + CTC_FLAG_ISSET(p_new_param->flag, CTC_NH_NEXTHOP_MAC_REDIRECT_TO_CPU)
            +  CTC_FLAG_ISSET(p_new_param->flag, CTC_NH_NEXTHOP_MAC_ECMP_IF_ARP)) > 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (arp_id < 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_goldengate_nh_lkup_arp_id(lchip, arp_id, &p_arp));

    if (NULL == p_arp)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    else
    {
        if (!CTC_FLAG_ISSET(p_arp->flag, SYS_NH_ARP_FLAG_DROP)
            && !CTC_FLAG_ISSET(p_arp->flag, SYS_NH_ARP_FLAG_REDIRECT_TO_CPU)
            && !CTC_FLAG_ISSET(p_new_param->flag, CTC_NH_NEXTHOP_MAC_DISCARD)
            && !CTC_FLAG_ISSET(p_new_param->flag, CTC_NH_NEXTHOP_MAC_REDIRECT_TO_CPU))
        {
            /*check vlan if, sub if */
            if ((CTC_L3IF_TYPE_SUB_IF == p_arp->l3if_type) || (CTC_L3IF_TYPE_VLAN_IF == p_arp->l3if_type))
            {
                if (!CTC_FLAG_ISSET(p_new_param->flag, CTC_NH_NEXTHOP_MAC_VLAN_VALID))
                {
                    return CTC_E_INVALID_PARAM;
                }

                if(p_new_param->vlan_id != p_arp->output_vid)
                {
                    return CTC_E_INVALID_PARAM;
                }
            }
        }
        CTC_ERROR_RETURN(_sys_goldengate_nh_build_arp_info(lchip, p_arp, p_new_param));
    }

    sys_goldengate_nh_write_entry_arp(lchip, p_arp);
    if (p_arp->updateNhp && p_arp->nh_offset)
    {
        p_arp->updateNhp(lchip, p_arp->nh_offset, p_arp);
    }

    return CTC_E_NONE;

}
int32
sys_goldengate_nh_bind_arp_cb(uint8 lchip, sys_nh_info_arp_param_t* p_arp_parm, uint16 arp_id)
{

    sys_nh_db_arp_t*  p_arp = NULL;
    ctc_nh_oif_info_t* p_oif = NULL;
    sys_l3if_prop_t l3if_prop;
    uint8 use_l2edit = 0;
    int32 ret = CTC_E_NONE;

    CTC_ERROR_RETURN(sys_goldengate_nh_lkup_arp_id(lchip, arp_id, &p_arp));

    if (NULL == p_arp)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    p_oif = p_arp_parm->p_oif;

    if (0 == p_oif->is_l2_port)
    {
        sal_memset(&l3if_prop, 0 ,sizeof(sys_l3if_prop_t));
        CTC_ERROR_RETURN(sys_goldengate_l3if_get_l3if_info_with_port_and_vlan(lchip, p_oif->gport, p_oif->vid, &l3if_prop));
    }

    if (SYS_NH_TYPE_IPUC == p_arp_parm->nh_entry_type)
    {

        if(p_arp->nh_offset || p_arp->updateNhp)
        {
            return CTC_E_NH_HR_NH_IN_USE;
        }

        if ((CTC_L3IF_TYPE_PHY_IF == l3if_prop.l3if_type) && p_oif->vid)
        {
            use_l2edit = 1;
        }
        if (use_l2edit)
        {
            if(0 == p_arp->in_l2_offset)
            {
                ret = sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_3W, 1, &p_arp->in_l2_offset);
                if (CTC_E_NONE != ret)
                {
                    p_arp->in_l2_offset = 0;
                    goto err;
                }

                ret = sys_goldengate_nh_write_entry_arp(lchip, p_arp);
                if (CTC_E_NONE != ret)
                {
                    sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_3W, 1, p_arp->in_l2_offset);
                    p_arp->in_l2_offset = 0;
                    goto err;
                }
            }
        }
        else
        {
            p_arp->nh_offset    = p_arp_parm->nh_offset;
            p_arp->updateNhp    = p_arp_parm->updateNhp;
        }
        p_arp->l3if_type    = l3if_prop.l3if_type;
    }
    else
    {
        if(0 == p_arp->offset)
        {
            ret = sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 1, &p_arp->offset);
            if (CTC_E_NONE != ret)
            {
                p_arp->offset = 0;
                goto err;
            }

            ret = sys_goldengate_nh_write_entry_arp(lchip, p_arp);
            if (CTC_E_NONE != ret)
            {
                sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 1, p_arp->offset);
                p_arp->offset = 0;
                goto err;
            }
            p_arp->l3if_type = l3if_prop.l3if_type;
        }
    }
err:
    /*error process*/
    return ret;
}

