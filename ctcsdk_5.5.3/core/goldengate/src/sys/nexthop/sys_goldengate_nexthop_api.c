/**
  @file sys_goldengate_nexthop_api.c

  @date 2009-11-11

  @version v2.0

  The file contains all nexthop API function for upper layer module
 */
/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_linklist.h"
#include "ctc_nexthop.h"
#include "ctc_warmboot.h"

#include "sys_goldengate_chip.h"
#include "sys_goldengate_aps.h"
#include "sys_goldengate_l3if.h"

#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_queue_api.h"

/* #include "goldengate/include/drv_lib.h" --never--*/

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
#define SYS_NH_INIT_CHECK(lchip)                    \
    do {                                            \
        SYS_LCHIP_CHECK_ACTIVE(lchip);                   \
        if (NULL == p_gg_nh_api_master[lchip])         \
        {                                           \
            return CTC_E_NOT_INIT;                  \
        }                                           \
    } while (0)



#define SYS_NEXTHOP_MAX_CHIP_NUM  31
#define SYS_NEXTHOP_PORT_NUM_PER_CHIP 512

#define GPORT_TO_INDEX(gport)  \
    ((SYS_MAP_CTC_GPORT_TO_GCHIP(gport) == 0x1f) ? \
    (SYS_NEXTHOP_PORT_NUM_PER_CHIP * SYS_NEXTHOP_MAX_CHIP_NUM + SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport)) : \
    (SYS_MAP_CTC_GPORT_TO_GCHIP(gport) * SYS_NEXTHOP_PORT_NUM_PER_CHIP + SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport)))

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
sys_goldengate_nh_api_master_t* p_gg_nh_api_master[CTC_MAX_LOCAL_CHIP_NUM];

STATIC int32
_sys_goldengate_misc_nh_create(uint8 lchip, uint32 nhid, sys_goldengate_nh_type_t nh_param_type);

/****************************************************************************
 *
* Function
*
*****************************************************************************/
/**
 @brief This function is to initialize nexthop API data,
 */
int32
sys_goldengate_nh_api_init(uint8 lchip, ctc_nh_global_cfg_t* nh_cfg)
{

    uint32   vec_block_num  = 0;
    int32    ret = 0;

    if (p_gg_nh_api_master[lchip])
    {
        return CTC_E_NONE;
    }


    p_gg_nh_api_master[lchip] = (sys_goldengate_nh_api_master_t*)mem_malloc(MEM_NEXTHOP_MODULE, (sizeof(sys_goldengate_nh_api_master_t)));
    sal_memset(p_gg_nh_api_master[lchip], 0, (sizeof(sys_goldengate_nh_api_master_t)));
    /*1. Bridge Ucast Data init*/
    if (NULL != p_gg_nh_api_master[lchip])
    {
        SYS_NH_CREAT_LOCK(p_gg_nh_api_master[lchip]->brguc_info.brguc_mutex);
    }
    else
    {
        mem_free(p_gg_nh_api_master[lchip]);
        p_gg_nh_api_master[lchip] = NULL;
        return CTC_E_NO_MEMORY;
    }

    vec_block_num  = (SYS_NEXTHOP_PORT_NUM_PER_CHIP * SYS_NEXTHOP_MAX_CHIP_NUM + 64) / 64;  /*31*128 (physical port ) + 64 (linkagg) */
    p_gg_nh_api_master[lchip]->brguc_info.brguc_vector = ctc_vector_init(vec_block_num, 64);
    if (NULL == p_gg_nh_api_master[lchip]->brguc_info.brguc_vector)
    {
        mem_free(p_gg_nh_api_master[lchip]);
        p_gg_nh_api_master[lchip] = NULL;
        return CTC_E_NO_MEMORY;
    }

	CTC_ERROR_RETURN(sys_goldengate_nh_init(lchip, nh_cfg));

    ret = sys_goldengate_nh_get_max_external_nhid(lchip, &p_gg_nh_api_master[lchip]->max_external_nhid);

    if (ret)
    {
        mem_free(p_gg_nh_api_master[lchip]);
        p_gg_nh_api_master[lchip] = NULL;
        return ret;
    }



    /*2. Create default nexthop*/
    CTC_ERROR_RETURN(_sys_goldengate_misc_nh_create(lchip, SYS_NH_RESOLVED_NHID_FOR_DROP, SYS_NH_TYPE_DROP));
    CTC_ERROR_RETURN(_sys_goldengate_misc_nh_create(lchip, SYS_NH_RESOLVED_NHID_FOR_TOCPU, SYS_NH_TYPE_TOCPU));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_api_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_api_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_nh_api_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_goldengate_nh_deinit(lchip);

    /*free bridge uc vector*/
    ctc_vector_traverse(p_gg_nh_api_master[lchip]->brguc_info.brguc_vector, (vector_traversal_fn)_sys_goldengate_nh_api_free_node_data, NULL);
    ctc_vector_release(p_gg_nh_api_master[lchip]->brguc_info.brguc_vector);

    sal_mutex_destroy(p_gg_nh_api_master[lchip]->brguc_info.brguc_mutex);
    mem_free(p_gg_nh_api_master[lchip]);

    return CTC_E_NONE;
}
/**
 @brief This function is to create normal bridge nexthop in different types

 @param[in] gport, global port id

 @param[in] nh_type, nexthop type

 @return CTC_E_XXX
 */
int32
_sys_goldengate_brguc_nh_create_by_type(uint8 lchip, uint16 gport, sys_nh_param_brguc_sub_type_t nh_type)
{
    sys_nh_param_brguc_t brguc_nh;
    sys_hbnh_brguc_node_info_t* p_brguc_node;
    bool is_add = FALSE;
	int32 ret = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = 0x%x , nh_type = %d\n", gport, nh_type);


    sal_memset(&brguc_nh, 0, sizeof(sys_nh_param_brguc_t));
    brguc_nh.hdr.nh_param_type = SYS_NH_TYPE_BRGUC;
    brguc_nh.hdr.nhid = SYS_NH_INVALID_NHID;
    brguc_nh.hdr.is_internal_nh = TRUE;
    brguc_nh.nh_sub_type = nh_type;
    brguc_nh.gport = gport;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "CtcGport = 0x%x ,DrvGport:0x%x, nh_type = %d, index:%d\n",
                                          gport, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport), nh_type, GPORT_TO_INDEX(gport));


    p_brguc_node = ctc_vector_get(p_gg_nh_api_master[lchip]->brguc_info.brguc_vector,  GPORT_TO_INDEX(gport));
    if (NULL == p_brguc_node)
    {
        p_brguc_node = (sys_hbnh_brguc_node_info_t*)mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_hbnh_brguc_node_info_t));
        if (NULL == p_brguc_node)
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_brguc_node, 0, sizeof(sys_hbnh_brguc_node_info_t));
        p_brguc_node->nhid_brguc  = SYS_NH_INVALID_NHID;
        p_brguc_node->nhid_bypass  = SYS_NH_INVALID_NHID;
        is_add = TRUE;
    }

    if ((nh_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BASIC)
        && (p_brguc_node->nhid_brguc != SYS_NH_INVALID_NHID))
    {
        ret = CTC_E_ENTRY_EXIST;
        goto error;
    }

    if ((nh_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BYPASS)
        && (p_brguc_node->nhid_bypass != SYS_NH_INVALID_NHID))
    {
        ret = CTC_E_ENTRY_EXIST;
        goto error;
    }

    ret = sys_goldengate_nh_api_create(lchip, (sys_nh_param_com_t*)(&brguc_nh));
	if(ret != CTC_E_NONE)
    {
        goto error;;
    }
    if (nh_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BASIC)
    {
        p_brguc_node->nhid_brguc = brguc_nh.hdr.nhid;
    }

    if (nh_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BYPASS)
    {
        p_brguc_node->nhid_bypass = brguc_nh.hdr.nhid;
    }

    if (is_add)
    {
        if (FALSE == ctc_vector_add(p_gg_nh_api_master[lchip]->brguc_info.brguc_vector, GPORT_TO_INDEX(gport), p_brguc_node))
        {
            if (p_brguc_node->nhid_brguc != SYS_NH_INVALID_NHID)
            {
                sys_goldengate_nh_api_delete(lchip, p_brguc_node->nhid_brguc, SYS_NH_TYPE_BRGUC);
            }

            if (p_brguc_node->nhid_bypass != SYS_NH_INVALID_NHID)
            {
                sys_goldengate_nh_api_delete(lchip, p_brguc_node->nhid_bypass, SYS_NH_TYPE_BRGUC);
            }

            ret = CTC_E_NO_MEMORY;
            goto error;
        }
    }

    return CTC_E_NONE;

error:

    if (is_add)
    {
        mem_free(p_brguc_node);
    }

    return ret;

}

/**
 @brief This function is to create normal bridge nexthop

 @param[in] gport, global port id

 @param[in] nh_type, nexthop type

 @return CTC_E_XXX
 */
int32
sys_goldengate_brguc_nh_create(uint8 lchip, uint16 gport, ctc_nh_param_brguc_sub_type_t nh_type)
{

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = 0x%x , nh_type = %d\n", gport, nh_type);

    SYS_NH_INIT_CHECK(lchip);
    CTC_GLOBAL_PORT_CHECK(gport);

    if (nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_UNTAGGED
        || nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_RAW_PACKET_ELOG_CPU
        || nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_SERVICE_QUEUE)
    {
        return CTC_E_NOT_SUPPORT;
    }

    if (((nh_type & CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC) != 0) || (nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_ALL))
    {
        CTC_ERROR_RETURN(_sys_goldengate_brguc_nh_create_by_type(lchip, gport, SYS_NH_PARAM_BRGUC_SUB_TYPE_BASIC));
    }

    if (((nh_type & CTC_NH_PARAM_BRGUC_SUB_TYPE_BYPASS) != 0) || (nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_ALL))
    {
        CTC_ERROR_RETURN(_sys_goldengate_brguc_nh_create_by_type(lchip, gport, SYS_NH_PARAM_BRGUC_SUB_TYPE_BYPASS));
    }

    return CTC_E_NONE;
}

/**
 @brief This function is to delete normal bridge nexthop

 @param[in] gport, global port id

 @param[in] nh_type, nexthop type

 @return CTC_E_XXX
 */
int32
sys_goldengate_brguc_nh_delete(uint8 lchip, uint16 gport)
{

    sys_hbnh_brguc_node_info_t* p_brguc_node;
    int32 ret = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = %d\n", gport);

    SYS_NH_INIT_CHECK(lchip);
    CTC_GLOBAL_PORT_CHECK(gport);

    SYS_NH_LOCK(p_gg_nh_api_master[lchip]->brguc_info.brguc_mutex);

    p_brguc_node = ctc_vector_get(p_gg_nh_api_master[lchip]->brguc_info.brguc_vector, GPORT_TO_INDEX(gport));
    if (NULL == p_brguc_node)
    {
        SYS_NH_UNLOCK(p_gg_nh_api_master[lchip]->brguc_info.brguc_mutex);
        return CTC_E_NH_NOT_EXIST;
    }

    if (p_brguc_node->nhid_brguc != SYS_NH_INVALID_NHID)
    {
        ret = (sys_goldengate_nh_api_delete(lchip, p_brguc_node->nhid_brguc, SYS_NH_TYPE_BRGUC));
        p_brguc_node->nhid_brguc = SYS_NH_INVALID_NHID;

    }

    if (p_brguc_node->nhid_bypass != SYS_NH_INVALID_NHID)
    {
        ret = ret ? ret : (sys_goldengate_nh_api_delete(lchip, p_brguc_node->nhid_bypass, SYS_NH_TYPE_BRGUC));
        p_brguc_node->nhid_bypass = SYS_NH_INVALID_NHID;

    }

    if ((p_brguc_node->nhid_bypass == SYS_NH_INVALID_NHID)
        && (p_brguc_node->nhid_brguc == SYS_NH_INVALID_NHID))
    {
        ctc_vector_del(p_gg_nh_api_master[lchip]->brguc_info.brguc_vector, GPORT_TO_INDEX(gport));
        mem_free(p_brguc_node);
    }

    SYS_NH_UNLOCK(p_gg_nh_api_master[lchip]->brguc_info.brguc_mutex);
    return ret;
}

/**
 @brief This function is used to get dsFwd offset array

 @param[in] nhid, nexthop id

 @param[in] offset_array, dsfwd offset array

 @return CTC_E_XXX
 */
int32
sys_goldengate_nh_get_dsfwd_offset(uint8 lchip, uint32 nhid, uint32 *p_dsfwd_offset)
{
    sys_nh_info_com_t* p_nhinfo;


    /* get ds_fwd */

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo_by_nhid(lchip, nhid, (sys_nh_info_com_t**)&p_nhinfo));

    if (!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD)
		&& (p_nhinfo->hdr.nh_entry_type != SYS_NH_TYPE_ECMP)
		&& !(p_nhinfo->hdr.nh_entry_flags & SYS_NH_INFO_FLAG_HAVE_RSV_DSFWD))
    {
		sys_nh_param_com_t nh_param;
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

        sal_memset(&nh_param, 0, sizeof(sys_nh_param_com_t));
        nh_param.hdr.nhid = nhid;
        nh_param.hdr.nh_param_type = p_nhinfo->hdr.nh_entry_type;
        nh_param.hdr.change_type  = SYS_NH_CHANGE_TYPE_ADD_DYNTBL;
        CTC_ERROR_RETURN(sys_goldengate_nh_api_update(lchip, nhid, (sys_nh_param_com_t*)(&nh_param)));

        CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo_by_nhid(lchip, nhid, (sys_nh_info_com_t**)&p_nhinfo));

    }

    if (!(p_nhinfo->hdr.nh_entry_flags & SYS_NH_INFO_FLAG_HAVE_DSFWD)
		&& !(p_nhinfo->hdr.nh_entry_flags & SYS_NH_INFO_FLAG_HAVE_RSV_DSFWD) )
    {
        return CTC_E_INVALID_PARAM;
    }


    *p_dsfwd_offset = p_nhinfo->hdr.dsfwd_info.dsfwd_offset;


    return CTC_E_NONE;
}


int32
sys_goldengate_nh_get_aps_working_path(uint8 lchip, uint32 nhid, uint32* gport, uint32* nexthop_ptr, bool* p_protect_en)
{
    bool protect_en = 0;
    uint16 next_group_id = 0;
    sys_nh_info_dsnh_t sys_nh_info;

    sal_memset(&sys_nh_info, 0, sizeof(sys_nh_info));
    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, nhid, &sys_nh_info));

    if (NULL == p_protect_en)
    {
        CTC_ERROR_RETURN(sys_goldengate_aps_get_protection(lchip, sys_nh_info.dest_id, &protect_en));
    }
    else
    {
        protect_en = *p_protect_en;
    }

    if (sys_nh_info.individual_nexthop && protect_en)/*two dsnexthop*/
    {
        *nexthop_ptr = sys_nh_info.dsnh_offset + 1*(1 + sys_nh_info.nexthop_ext);
    }
    CTC_ERROR_RETURN(sys_goldengate_aps_get_next_group(lchip, sys_nh_info.dest_id, &next_group_id, (!protect_en)));
    if (CTC_MAX_UINT16_VALUE == next_group_id)/*no nexthop aps group*/
    {
        CTC_ERROR_RETURN(sys_goldengate_aps_get_bridge_path(lchip, sys_nh_info.dest_id, gport, (!protect_en)));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_aps_get_protection(lchip, next_group_id, &protect_en));
        CTC_ERROR_RETURN(sys_goldengate_aps_get_bridge_path(lchip, next_group_id, gport, (!protect_en)));
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_nh_get_nhinfo(uint8 lchip, uint32 nhid, sys_nh_info_dsnh_t* p_nhinfo)
{

    sys_nh_info_com_t* p_nh_com_info;
    sys_l3if_ecmp_if_t* p_ecmp_if = NULL;
	uint8   gchip ;

	if(!p_nhinfo)
	{
       return CTC_E_NONE;
	}

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo_by_nhid(lchip, nhid, (sys_nh_info_com_t**)&p_nh_com_info));

    sys_goldengate_get_gchip_id(lchip, &gchip);

    p_nhinfo->nh_entry_type = p_nh_com_info->hdr.nh_entry_type;
    p_nhinfo->aps_en        = CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN);
    p_nhinfo->drop_pkt      = CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
    p_nhinfo->dsnh_offset   = p_nh_com_info->hdr.dsfwd_info.dsnh_offset;
    p_nhinfo->nexthop_ext   = CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
    p_nhinfo->dsfwd_valid   = CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD)
                               || (p_nh_com_info->hdr.nh_entry_flags & SYS_NH_INFO_FLAG_HAVE_RSV_DSFWD);

    if(CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LEN_ADJUST_EN))
    {
        p_nhinfo->adjust_len = p_nh_com_info->hdr.adjust_len;
    }
    p_nhinfo->merge_dsfwd = 0;
    if(!p_nhinfo->dsfwd_valid)
    {
        /*Use DsFwd Mode*/
        if (CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_ECMP_IF))
        {
            p_nhinfo->merge_dsfwd = 1;
        }

    }
    else
    {
        p_nhinfo->dsfwd_offset = p_nh_com_info->hdr.dsfwd_info.dsfwd_offset;
    }

    switch (p_nh_com_info->hdr.nh_entry_type)
    {
    case SYS_NH_TYPE_MCAST:
        {
            sys_nh_info_mcast_t * p_nh_mcast_info = (sys_nh_info_mcast_t*)p_nh_com_info;
            p_nhinfo->is_mcast = 1;
            p_nhinfo->dest_chipid = gchip;
            p_nhinfo->dest_id = 0xFFFF & (p_nh_mcast_info->basic_met_offset/2);
            p_nhinfo->merge_dsfwd = 1;

        }
        break;
    case SYS_NH_TYPE_BRGUC:
    {
        sys_nh_info_brguc_t* p_nh_brguc_info = (sys_nh_info_brguc_t*)p_nh_com_info;
        if (!p_nhinfo->aps_en)
        {
            p_nhinfo->dest_chipid  = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_brguc_info->dest_gport);
            p_nhinfo->dest_id     = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_nh_brguc_info->dest_gport);
            p_nhinfo->gport       = p_nh_brguc_info->dest_gport;
        }
        else
        {
            p_nhinfo->dest_chipid = gchip;
            p_nhinfo->dest_id = (p_nh_brguc_info->dest_gport);
            p_nhinfo->individual_nexthop =
                         CTC_FLAG_ISSET(p_nh_brguc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH)? 0 : 1;
        }
        if (!p_nhinfo->dsfwd_valid)
        {
            p_nhinfo->merge_dsfwd  = 1; /*L2Uc Nexthop don't need update,so it can use merge DsFwd mode*/
	    }

        if (p_nh_brguc_info->nh_sub_type < SYS_NH_PARAM_BRGUC_SUB_TYPE_VLAN_EDIT)
        {
            /*indicate l2uc*/
            p_nhinfo->re_route = 1;
        }
    }
        break;

    case SYS_NH_TYPE_MPLS:
        {
            sys_nh_info_mpls_t* p_nh_mpls_info = (sys_nh_info_mpls_t*)p_nh_com_info;
            if (!p_nhinfo->aps_en)
            {
                p_nhinfo->dest_chipid  = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_mpls_info->gport);
                p_nhinfo->dest_id     = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_nh_mpls_info->gport);
                p_nhinfo->gport       = p_nh_mpls_info->gport;
            }
            else
            {
                p_nhinfo->dest_chipid = gchip;
                p_nhinfo->dest_id = p_nh_mpls_info->aps_group_id;
                p_nhinfo->individual_nexthop = (p_nh_mpls_info->protection_path)? 1 : 0;

                /* 1. For PW Nexthop  */
                /* PW have APS, LSP no APS, mep_on_tunnel = 1, APS group in MEP is PW APS group */
                /* PW have APS, LSP have APS, mep_on_tunnel = 0, APS group in MEP is LSP APS group */
                /* PW no APS, LSP have APS, mep_on_tunnel = 0, APS group in MEP is LSP APS group */
                /* 2. For LSP Nexthop*/
                /* LSP have APS, SPME have or no APS, mep_on_tunnel = 1, APS group in MEP is LSP APS group*/
                /* LSP no APS, SPME have APS, mep_on_tunnel = 0, APS group in MEP is SPME APS group*/
                p_nhinfo->oam_aps_en = p_nhinfo->aps_en;
                if (CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))/*PW nexthop*/
                {
                    if (p_nh_mpls_info->protection_path)/* PW have APS*/
                    {
                        p_nhinfo->mep_on_tunnel = 1;
                        p_nhinfo->oam_aps_group_id =  p_nh_mpls_info->aps_group_id;
                        /* Working LSP have APS*/
                        if ((!p_nhinfo->protection_path) && p_nh_mpls_info->working_path.p_mpls_tunnel &&
                            CTC_FLAG_ISSET(p_nh_mpls_info->working_path.p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
                        {
                            p_nhinfo->oam_aps_group_id  =  p_nh_mpls_info->working_path.p_mpls_tunnel->gport;
                            p_nhinfo->mep_on_tunnel = 0;
                            p_nhinfo->re_route = 1;  /*means multi-level aps for mpls nexthop*/
                        }
                        /* Protection LSP have APS*/
                        else if (p_nh_mpls_info->protection_path &&
                            p_nh_mpls_info->protection_path->p_mpls_tunnel &&
                        CTC_FLAG_ISSET(p_nh_mpls_info->protection_path->p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
                        {
                            p_nhinfo->oam_aps_group_id =  p_nh_mpls_info->protection_path->p_mpls_tunnel->gport;
                            p_nhinfo->mep_on_tunnel = 0;
                            p_nhinfo->re_route = 1;  /*means multi-level aps for mpls nexthop*/
                        }
                        if (p_nhinfo->protection_path)
                        {
                            p_nhinfo->dsnh_offset = p_nh_com_info->hdr.dsfwd_info.dsnh_offset + 1*(1 + p_nhinfo->nexthop_ext);
                        }
                    }
                    else /* PW no APS, LSP may be have APS*/
                    {
                        p_nhinfo->mep_on_tunnel = 0;
                        p_nhinfo->oam_aps_group_id = (p_nh_mpls_info->working_path.p_mpls_tunnel)?
                            p_nh_mpls_info->working_path.p_mpls_tunnel->gport : 0;    /* gport is LSP APS group actually*/
                    }
                }
                else /*LSP nexthop*/
                {
                     if ((NULL == p_nh_mpls_info->protection_path)/* LSP no APS and SPME have APS*/
                        && p_nh_mpls_info->working_path.p_mpls_tunnel
                        && CTC_FLAG_ISSET(p_nh_mpls_info->working_path.p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_EXSIT_SPME)
                        && CTC_FLAG_ISSET(p_nh_mpls_info->working_path.p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS)
                        && !CTC_FLAG_ISSET(p_nh_mpls_info->working_path.p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS))
                     {
                         p_nhinfo->mep_on_tunnel = 0;
                         p_nhinfo->oam_aps_group_id  =  p_nh_mpls_info->working_path.p_mpls_tunnel->gport;
                     }
                     else
                     {
                         p_nhinfo->mep_on_tunnel = p_nhinfo->aps_en;
                         p_nhinfo->oam_aps_group_id =  p_nh_mpls_info->aps_group_id;
                     }
                }
            }
        }
        break;
    case SYS_NH_TYPE_ECMP:
        {
            sys_nh_info_ecmp_t* p_nh_ecmp_info = (sys_nh_info_ecmp_t*)p_nh_com_info;
            p_nhinfo->ecmp_valid = TRUE;
            p_nhinfo->ecmp_group_id = p_nh_ecmp_info->ecmp_group_id;
            p_nhinfo->ecmp_num = p_nh_ecmp_info->ecmp_cnt;
            p_nhinfo->ecmp_cnt = p_nh_ecmp_info->ecmp_cnt;
            p_nhinfo->valid_cnt = p_nh_ecmp_info->valid_cnt;
            p_nhinfo->merge_dsfwd  = 0;
              sal_memcpy(&p_nhinfo->nh_array[0], &p_nh_ecmp_info->nh_array[0], p_nh_ecmp_info->ecmp_cnt * sizeof(uint32));
       }
       break;
    case SYS_NH_TYPE_IPUC:
        {
            sys_nh_info_ipuc_t* p_nh_ipuc_info = (sys_nh_info_ipuc_t*)p_nh_com_info;
            p_nhinfo->have_l2edit = CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
            if (!p_nhinfo->aps_en)
            {
                p_nhinfo->dest_chipid  = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_ipuc_info->gport);
                p_nhinfo->dest_id     = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_nh_ipuc_info->gport);
                p_nhinfo->gport       = p_nh_ipuc_info->gport;
            }
            else
            {
                p_nhinfo->dest_chipid = gchip;
                p_nhinfo->dest_id = (p_nh_ipuc_info->gport);
            }
            p_nhinfo->merge_dsfwd = CTC_FLAG_ISSET(p_nh_ipuc_info->flag,SYS_NH_IPUC_FLAG_MERGE_DSFWD)?1:0;
        }
        break;

    case SYS_NH_TYPE_IP_TUNNEL:
        {
            sys_nh_info_ip_tunnel_t* p_nh_ip_tunnel_info = (sys_nh_info_ip_tunnel_t*)p_nh_com_info;

            p_nhinfo->dest_chipid   = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_ip_tunnel_info->gport);
            p_nhinfo->dest_id       = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_nh_ip_tunnel_info->gport);
			p_nhinfo->gport       = p_nh_ip_tunnel_info->gport;
            p_nhinfo->is_ivi = CTC_FLAG_ISSET(p_nh_ip_tunnel_info->flag, SYS_NH_IP_TUNNEL_FLAG_IVI_6TO4) ||
                               CTC_FLAG_ISSET(p_nh_ip_tunnel_info->flag, SYS_NH_IP_TUNNEL_FLAG_IVI_4TO6);
            p_nhinfo->re_route = CTC_FLAG_ISSET(p_nh_ip_tunnel_info->flag, SYS_NH_IP_TUNNEL_REROUTE);
            if (CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_ECMP_IF))
            {
                p_nhinfo->ecmp_valid = FALSE;
                p_nhinfo->is_ecmp_intf = TRUE;
                 p_nhinfo->merge_dsfwd = 1;
                sys_goldengate_l3if_get_ecmp_if_info(lchip, p_nh_ip_tunnel_info->ecmp_if_id, &p_ecmp_if);
                p_nhinfo->ecmp_group_id = p_ecmp_if->hw_group_id;
            }
        }
        break;

    case SYS_NH_TYPE_TRILL:
        {
            sys_nh_info_trill_t* p_nh_trill_info = (sys_nh_info_trill_t*)p_nh_com_info;
            p_nhinfo->gport       = p_nh_trill_info->gport;
        }
        break;

    case SYS_NH_TYPE_DROP:
        {
            p_nhinfo->dsfwd_valid = TRUE;
            p_nhinfo->drop_pkt = TRUE;
		    p_nhinfo->dest_chipid = gchip;
		    p_nhinfo->dest_id     = SYS_RSV_PORT_DROP_ID;
	   }
        break;

    case SYS_NH_TYPE_TOCPU:
        {
            p_nhinfo->dsfwd_valid = TRUE;
        }
        break;
    case SYS_NH_TYPE_MISC:
        {
            sys_nh_info_misc_t* p_nh_misc_info = (sys_nh_info_misc_t*)p_nh_com_info;
            p_nhinfo->dest_chipid   = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_misc_info->gport);
            p_nhinfo->dest_id       = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_nh_misc_info->gport);
            p_nhinfo->gport       = p_nh_misc_info->gport;
        }
        break;

    case SYS_NH_TYPE_ILOOP:
        {
            sys_nh_info_special_t* p_nh_iloop_info = (sys_nh_info_special_t *)p_nh_com_info;
            p_nhinfo->dest_chipid   = gchip;
            p_nhinfo->dest_id       = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_nh_iloop_info->dest_gport);
            p_nhinfo->dsfwd_valid   = CTC_FLAG_ISSET(p_nh_iloop_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
            p_nhinfo->nexthop_ext   = CTC_FLAG_ISSET(p_nh_iloop_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
            p_nhinfo->aps_en        = CTC_FLAG_ISSET(p_nh_iloop_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN);
        }
        break;

    case SYS_NH_TYPE_RSPAN:
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }
	if(CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE))
	{
	   p_nhinfo->merge_dsfwd  = 2; /* must need dsfwd*/
	}
	return CTC_E_NONE;

}

int32
sys_goldengate_nh_mapping_to_nhinfo(uint8 lchip, sys_nh_info_com_t* p_nh_com_info, sys_nh_info_dsnh_t* p_nhinfo)
{


    sys_l3if_ecmp_if_t* p_ecmp_if = NULL;
	uint8   gchip ;


 sys_goldengate_get_gchip_id(lchip, &gchip);

    p_nhinfo->nh_entry_type = p_nh_com_info->hdr.nh_entry_type;
    p_nhinfo->aps_en        = CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN);
    p_nhinfo->drop_pkt      = CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
    p_nhinfo->dsnh_offset   = p_nh_com_info->hdr.dsfwd_info.dsnh_offset;
    p_nhinfo->nexthop_ext   = CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
    p_nhinfo->dsfwd_valid   = CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD)
                               || (p_nh_com_info->hdr.nh_entry_flags & SYS_NH_INFO_FLAG_HAVE_RSV_DSFWD);

    if(CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LEN_ADJUST_EN))
   	{
    	p_nhinfo->adjust_len = p_nh_com_info->hdr.adjust_len;
   	}
    if(!p_nhinfo->dsfwd_valid)
	{
        /*Use DsFwd Mode*/
         /*sys_goldengate_nh_get_dsfwd_mode(lchip, &p_nhinfo->merge_dsfwd);*/
        p_nhinfo->merge_dsfwd =1;
        if (CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_ECMP_IF))
        {
            p_nhinfo->merge_dsfwd = 1;
        }
	}
	else
	{
	  p_nhinfo->dsfwd_offset = p_nh_com_info->hdr.dsfwd_info.dsfwd_offset;
	  p_nhinfo->merge_dsfwd = 0;
	}

    switch (p_nh_com_info->hdr.nh_entry_type)
    {
    case SYS_NH_TYPE_MCAST:
        {
            sys_nh_info_mcast_t * p_nh_mcast_info = (sys_nh_info_mcast_t*)p_nh_com_info;
            p_nhinfo->is_mcast = 1;
            p_nhinfo->dest_chipid = gchip;
            p_nhinfo->dest_id = 0xFFFF & (p_nh_mcast_info->basic_met_offset/2);

        }
        break;
    case SYS_NH_TYPE_BRGUC:
        {
			sys_nh_info_brguc_t* p_nh_brguc_info = (sys_nh_info_brguc_t*)p_nh_com_info;
		    if (!p_nhinfo->aps_en)
            {
                p_nhinfo->dest_chipid  = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_brguc_info->dest_gport);
                p_nhinfo->dest_id     = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_nh_brguc_info->dest_gport);
				p_nhinfo->gport       = p_nh_brguc_info->dest_gport;
            }
            else
            {
                p_nhinfo->dest_chipid = gchip;
                p_nhinfo->dest_id = (p_nh_brguc_info->dest_gport);
            }
			if(!p_nhinfo->dsfwd_valid)
			{
		       p_nhinfo->merge_dsfwd  = 1; /*L2Uc Nexthop don't need update,so it can use merge DsFwd mode*/
			}

        }
        break;

    case SYS_NH_TYPE_MPLS:
        {
            sys_nh_info_mpls_t* p_nh_mpls_info = (sys_nh_info_mpls_t*)p_nh_com_info;
            if (!p_nhinfo->aps_en)
            {
                p_nhinfo->dest_chipid  = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_mpls_info->gport);
                p_nhinfo->dest_id     = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_nh_mpls_info->gport);
                p_nhinfo->gport       = p_nh_mpls_info->gport;
            }
            else
            {
                p_nhinfo->dest_chipid = gchip;
                p_nhinfo->dest_id = p_nh_mpls_info->aps_group_id;
            }

            if(CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
            { /*PW nexthop*/
                p_nhinfo->mep_on_tunnel = 0;
                if(!p_nhinfo->protection_path)
                {
                    if(p_nh_mpls_info->working_path.p_mpls_tunnel &&
                            CTC_FLAG_ISSET(p_nh_mpls_info->working_path.p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
                    {
                        p_nhinfo->oam_aps_group_id  =  p_nh_mpls_info->working_path.p_mpls_tunnel->gport;
                        p_nhinfo->oam_aps_en = 1;
                    }
                }
                else
                {
                    if(p_nh_mpls_info->protection_path &&
                            p_nh_mpls_info->protection_path->p_mpls_tunnel &&
                            CTC_FLAG_ISSET(p_nh_mpls_info->protection_path->p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
                    {
                        p_nhinfo->oam_aps_group_id =  p_nh_mpls_info->protection_path->p_mpls_tunnel->gport;
                        p_nhinfo->oam_aps_en = 1;
                    }
                }
            }
            else
            {
                p_nhinfo->mep_on_tunnel = 1;
                p_nhinfo->oam_aps_group_id =  p_nh_mpls_info->aps_group_id;
                p_nhinfo->oam_aps_en = p_nhinfo->aps_en;
            }

            if (p_nhinfo->protection_path)
            {
                p_nhinfo->dsnh_offset = p_nh_com_info->hdr.dsfwd_info.dsnh_offset + 1*(1 + p_nhinfo->nexthop_ext);
            }
            else
            {
                p_nhinfo->dsnh_offset = p_nh_com_info->hdr.dsfwd_info.dsnh_offset;
            }

        }
        break;
    case SYS_NH_TYPE_ECMP:
        {
			sys_nh_info_ecmp_t* p_nh_ecmp_info = (sys_nh_info_ecmp_t*)p_nh_com_info;
            p_nhinfo->ecmp_valid = TRUE;
            p_nhinfo->ecmp_group_id = p_nh_ecmp_info->ecmp_group_id;
            p_nhinfo->ecmp_num = p_nh_ecmp_info->ecmp_cnt;
            p_nhinfo->ecmp_cnt = p_nh_ecmp_info->ecmp_cnt;
            p_nhinfo->valid_cnt = p_nh_ecmp_info->valid_cnt;
            p_nhinfo->merge_dsfwd  = 1;
            sal_memcpy(&p_nhinfo->nh_array[0], &p_nh_ecmp_info->nh_array[0], p_nh_ecmp_info->ecmp_cnt * sizeof(uint32));
       }
       break;
    case SYS_NH_TYPE_IPUC:
        {
            sys_nh_info_ipuc_t* p_nh_ipuc_info = (sys_nh_info_ipuc_t*)p_nh_com_info;
            p_nhinfo->have_l2edit = CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
            if (!p_nhinfo->aps_en)
            {
                p_nhinfo->dest_chipid  = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_ipuc_info->gport);
                p_nhinfo->dest_id     = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_nh_ipuc_info->gport);
				p_nhinfo->gport       = p_nh_ipuc_info->gport;
            }
            else
            {
                p_nhinfo->dest_chipid = gchip;
                p_nhinfo->dest_id = (p_nh_ipuc_info->gport);
            }
        }
        break;

    case SYS_NH_TYPE_IP_TUNNEL:
        {
            sys_nh_info_ip_tunnel_t* p_nh_ip_tunnel_info = (sys_nh_info_ip_tunnel_t*)p_nh_com_info;

            p_nhinfo->dest_chipid   = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_ip_tunnel_info->gport);
            p_nhinfo->dest_id       = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_nh_ip_tunnel_info->gport);
			p_nhinfo->gport       = p_nh_ip_tunnel_info->gport;
            p_nhinfo->is_ivi = CTC_FLAG_ISSET(p_nh_ip_tunnel_info->flag, SYS_NH_IP_TUNNEL_FLAG_IVI_6TO4) ||
                               CTC_FLAG_ISSET(p_nh_ip_tunnel_info->flag, SYS_NH_IP_TUNNEL_FLAG_IVI_4TO6);
            p_nhinfo->re_route = CTC_FLAG_ISSET(p_nh_ip_tunnel_info->flag, SYS_NH_IP_TUNNEL_REROUTE);
            if (CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_ECMP_IF))
            {
                p_nhinfo->ecmp_valid = FALSE;
                p_nhinfo->is_ecmp_intf = TRUE;
                sys_goldengate_l3if_get_ecmp_if_info(lchip, p_nh_ip_tunnel_info->ecmp_if_id, &p_ecmp_if);
                p_nhinfo->ecmp_group_id = p_ecmp_if->hw_group_id;
            }

        }
        break;

    case SYS_NH_TYPE_TRILL:
        {
            sys_nh_info_trill_t* p_nh_trill_info = (sys_nh_info_trill_t*)p_nh_com_info;
            p_nhinfo->gport       = p_nh_trill_info->gport;
        }
        break;

    case SYS_NH_TYPE_DROP:
        {
                  p_nhinfo->dsfwd_valid = TRUE;
		      p_nhinfo->drop_pkt = TRUE;
			p_nhinfo->dest_chipid = gchip;
			p_nhinfo->dest_id        = SYS_RSV_PORT_DROP_ID;
		}
        break;

    case SYS_NH_TYPE_TOCPU:
        {
            p_nhinfo->dsfwd_valid = TRUE;
        }
        break;
    case SYS_NH_TYPE_MISC:
        {
            sys_nh_info_misc_t* p_nh_misc_info = (sys_nh_info_misc_t*)p_nh_com_info;
            p_nhinfo->dest_chipid   = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_misc_info->gport);
            p_nhinfo->dest_id       = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_nh_misc_info->gport);
            p_nhinfo->gport       = p_nh_misc_info->gport;
        }
        break;

    case SYS_NH_TYPE_ILOOP:
        {
            sys_nh_info_special_t* p_nh_iloop_info = (sys_nh_info_special_t *)p_nh_com_info;
            p_nhinfo->dest_chipid   = gchip;
            p_nhinfo->dest_id       = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_nh_iloop_info->dest_gport);
            p_nhinfo->dsfwd_valid   = CTC_FLAG_ISSET(p_nh_iloop_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
            p_nhinfo->nexthop_ext   = CTC_FLAG_ISSET(p_nh_iloop_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
            p_nhinfo->aps_en        = CTC_FLAG_ISSET(p_nh_iloop_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN);
	   }
        break;

    case SYS_NH_TYPE_RSPAN:
    default:
        return CTC_E_INVALID_PARAM;
    }

	return CTC_E_NONE;

}
int32
sys_goldengate_l2_get_ucast_nh(uint8 lchip, uint16 gport, ctc_nh_param_brguc_sub_type_t nh_type, uint32* nhid)
{
    int32 ret = CTC_E_NONE;
    sys_hbnh_brguc_node_info_t* p_brguc_nhid_info = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = %d\n", gport);

    SYS_NH_INIT_CHECK(lchip);
    CTC_GLOBAL_PORT_CHECK(gport);
    if (nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_UNTAGGED
        || nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_RAW_PACKET_ELOG_CPU
        || nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_SERVICE_QUEUE)
    {
        return CTC_E_NOT_SUPPORT;
    }


    SYS_NH_LOCK(p_gg_nh_api_master[lchip]->brguc_info.brguc_mutex);

    p_brguc_nhid_info = ctc_vector_get(p_gg_nh_api_master[lchip]->brguc_info.brguc_vector, GPORT_TO_INDEX(gport));
    if (NULL == p_brguc_nhid_info)
    {
        SYS_NH_UNLOCK(p_gg_nh_api_master[lchip]->brguc_info.brguc_mutex);
        return CTC_E_NH_NOT_EXIST;
    }

    switch (nh_type)
    {
    case CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC:
        if (p_brguc_nhid_info->nhid_brguc != SYS_NH_INVALID_NHID)
        {
            *nhid = p_brguc_nhid_info->nhid_brguc;
        }
        else
        {
            ret  = CTC_E_NH_NOT_EXIST;
        }

        break;

    case CTC_NH_PARAM_BRGUC_SUB_TYPE_BYPASS:
        if (p_brguc_nhid_info->nhid_bypass != SYS_NH_INVALID_NHID)
        {
            *nhid = p_brguc_nhid_info->nhid_bypass;
        }
        else
        {
            ret  = CTC_E_NH_NOT_EXIST;
        }

        break;

    default:
        SYS_NH_UNLOCK(p_gg_nh_api_master[lchip]->brguc_info.brguc_mutex);
        return CTC_E_INVALID_PARAM;
    }

    SYS_NH_UNLOCK(p_gg_nh_api_master[lchip]->brguc_info.brguc_mutex);
    return ret;
}


/**
 @brief This function is to create egress vlan edit nexthop

 @param[in] nhid, nexthop id of this nexthop

 @param[in] gport, global port id

 @param[in] p_vlan_info, vlan edit information

 @param[in] dsnh_offset, nexthop offset of this nexthop

 @return CTC_E_XXX
 */
int32
sys_goldengate_egress_vlan_edit_nh_create(uint8 lchip, uint32 nhid, uint16 gport,
                                         ctc_vlan_egress_edit_info_t* p_vlan_info, uint32 dsnh_offset,
                                         ctc_vlan_edit_nh_param_t* p_nh_param)
{
    sys_nh_param_brguc_t nh_brg;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d, gport = %d, dsnh_offset = %d",
                   nhid, gport, dsnh_offset);

    SYS_NH_INIT_CHECK(lchip);
    CTC_GLOBAL_PORT_CHECK(gport);
    CTC_PTR_VALID_CHECK(p_vlan_info);
    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID))
    {
        CTC_VLAN_RANGE_CHECK(p_vlan_info->output_cvid);
    }

    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))
    {
        CTC_VLAN_RANGE_CHECK(p_vlan_info->output_svid);
    }
    if(CTC_FLAG_ISSET(p_vlan_info->flag, CTC_VLAN_NH_MTU_CHECK_EN))
    {
        CTC_MAX_VALUE_CHECK(p_vlan_info->mtu_size, SYS_MTU_MAX_SIZE);
    }

    CTC_MAX_VALUE_CHECK(p_vlan_info->user_vlanptr,0x1fff);

    /* CTC_ERROR_RETURN(sys_goldengate_nh_check_max_glb_nh_offset(lchip, dsnh_offset));*/
    sal_memset(&nh_brg, 0, sizeof(sys_nh_param_brguc_t));

    nh_brg.hdr.nh_param_type = SYS_NH_TYPE_BRGUC;
    nh_brg.hdr.is_internal_nh = FALSE;
    nh_brg.hdr.nhid = nhid;
    nh_brg.nh_sub_type = SYS_NH_PARAM_BRGUC_SUB_TYPE_VLAN_EDIT;
    nh_brg.gport = gport;
    nh_brg.p_vlan_edit_info = p_vlan_info;
    nh_brg.dsnh_offset = dsnh_offset;
    nh_brg.loop_nhid = p_vlan_info->loop_nhid;
    nh_brg.p_vlan_edit_nh_param = p_nh_param;

    CTC_ERROR_RETURN(sys_goldengate_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_brg)));

    return CTC_E_NONE;
}

/**
 @brief This function is to delete egress vlan edit nexthop

 @param[in] nhid, nexthop id of this nexthop

 @return CTC_E_XXX
 */
int32
sys_goldengate_egress_vlan_edit_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_nh_api_delete(lchip, nhid, SYS_NH_TYPE_BRGUC));
    return CTC_E_NONE;
}

/**
 @brief This function is to create aps egress vlan edit nexthop

 @param[in] nhid, nexthop id of this nexthop

 @param[in] gport, global port id

 @param[in] p_vlan_info, vlan edit information

 @param[in] dsnh_offset, nexthop offset of this nexthop

 @return CTC_E_XXX
 */
int32
sys_goldengate_aps_egress_vlan_edit_nh_create(uint8 lchip, uint32 nhid,
                                             uint32 dsnh_offset, uint16 aps_bridge_id,
                                             ctc_vlan_egress_edit_info_t* p_vlan_info_working_path,
                                             ctc_vlan_egress_edit_info_t* p_vlan_info_protection_path)
{
    sys_nh_param_brguc_t nh_brg;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d, apsBridgeId = %d, dsnh_offset = %d",
                   nhid, aps_bridge_id, dsnh_offset);

    /*CTC_GLOBAL_PORT_CHECK(gport); Check APS bridgeId ?*/
    SYS_NH_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_info_working_path);
    CTC_PTR_VALID_CHECK(p_vlan_info_protection_path);
    if (CTC_FLAG_ISSET(p_vlan_info_working_path->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID))
    {
        CTC_VLAN_RANGE_CHECK(p_vlan_info_working_path->output_cvid);
    }

    if (CTC_FLAG_ISSET(p_vlan_info_working_path->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))
    {
        CTC_VLAN_RANGE_CHECK(p_vlan_info_working_path->output_svid);
    }

    if (CTC_FLAG_ISSET(p_vlan_info_protection_path->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID))
    {
        CTC_VLAN_RANGE_CHECK(p_vlan_info_protection_path->output_cvid);
    }

    if (CTC_FLAG_ISSET(p_vlan_info_protection_path->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))
    {
        CTC_VLAN_RANGE_CHECK(p_vlan_info_protection_path->output_svid);
    }

    sal_memset(&nh_brg, 0, sizeof(sys_nh_param_brguc_t));

    nh_brg.hdr.nh_param_type = SYS_NH_TYPE_BRGUC;
    nh_brg.hdr.is_internal_nh = FALSE;
    nh_brg.hdr.nhid = nhid;
    nh_brg.nh_sub_type = SYS_NH_PARAM_BRGUC_SUB_TYPE_APS_VLAN_EDIT;
    nh_brg.gport = aps_bridge_id;
    nh_brg.p_vlan_edit_info = p_vlan_info_working_path;
    nh_brg.p_vlan_edit_info_prot_path = p_vlan_info_protection_path;
    nh_brg.dsnh_offset = dsnh_offset;

    CTC_ERROR_RETURN(sys_goldengate_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_brg)));

    return CTC_E_NONE;
}

/**
 @brief This function is to delete egress vlan edit nexthop

 @param[in] nhid, nexthop id of this nexthop

 @return CTC_E_XXX
 */
int32
sys_goldengate_aps_egress_vlan_edit_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_nh_api_delete(lchip, nhid, SYS_NH_TYPE_BRGUC));

    return CTC_E_NONE;
}

/**
 @brief This function is to create ipuc nexthop

 @param[in] nhid, nexthop ID

 @param[in] p_member_list, member list to be added to this nexthop

 @return CTC_E_XXX
 */
int32
sys_goldengate_ipuc_nh_create(uint8 lchip, uint32 nhid, ctc_ip_nh_param_t* p_nh_param)
{
    sys_nh_param_ipuc_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_NH_INIT_CHECK(lchip);
    sal_memset(&nh_param, 0, sizeof(sys_nh_param_ipuc_t));

    if (CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_STRIP_MAC))
    {
        return  CTC_E_FEATURE_NOT_SUPPORT;
    }

    if(!CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_UNROV))
    {
        CTC_PTR_VALID_CHECK(&(p_nh_param->oif));
        CTC_GLOBAL_PORT_CHECK(p_nh_param->oif.gport);
        CTC_MAX_VALUE_CHECK(p_nh_param->oif.vid, CTC_MAX_VLAN_ID);
    }

    if(p_nh_param->aps_en)
    {
        bool protect_en = FALSE;
        CTC_PTR_VALID_CHECK(&(p_nh_param->p_oif));
        CTC_GLOBAL_PORT_CHECK(p_nh_param->p_oif.gport);
        CTC_MAX_VALUE_CHECK(p_nh_param->p_oif.vid, CTC_MAX_VLAN_ID);
        CTC_ERROR_RETURN(sys_goldengate_aps_get_protection(lchip, p_nh_param->aps_bridge_group_id, &protect_en));
        if(0 != sal_memcmp(nh_param.mac_sa, p_nh_param->mac_sa, sizeof(mac_addr_t)))
        {
            return  CTC_E_FEATURE_NOT_SUPPORT;
        }
    }

    /*TC_ERROR_RETURN(sys_goldengate_nh_check_max_glb_nh_offset(lchip, p_nh_param->dsnh_offset));*/
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d, dsnhoffset = %d, gport = %d, vid = %d, mac = %02x:%02x:%02x:%02x:%02x:%02x\n",
                   nhid, p_nh_param->dsnh_offset, p_nh_param->oif.gport, p_nh_param->oif.vid, p_nh_param->mac[0], p_nh_param->mac[1],
                   p_nh_param->mac[2], p_nh_param->mac[3], p_nh_param->mac[4], p_nh_param->mac[5]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "protection_gport = %d, protection_vid = %d, protection_mac = %02x:%02x:%02x:%02x:%02x:%02x\n",
                   p_nh_param->p_oif.gport, p_nh_param->p_oif.vid, p_nh_param->p_mac[0], p_nh_param->p_mac[1],
                   p_nh_param->p_mac[2], p_nh_param->p_mac[3], p_nh_param->p_mac[4], p_nh_param->p_mac[5]);

    nh_param.hdr.nh_param_type = SYS_NH_TYPE_IPUC;
    nh_param.hdr.is_internal_nh = FALSE;
    nh_param.hdr.nhid = nhid;
    nh_param.dsnh_offset = p_nh_param->dsnh_offset;
    nh_param.arp_id      = p_nh_param->arp_id;
    nh_param.adjust_length = p_nh_param->adjust_length;
    sal_memcpy(&nh_param.oif, &(p_nh_param->oif), sizeof(ctc_nh_oif_info_t));
    sal_memcpy(nh_param.mac, (p_nh_param->mac), sizeof(mac_addr_t));
    sal_memcpy(nh_param.mac_sa, (p_nh_param->mac_sa), sizeof(mac_addr_t));

    nh_param.aps_en = p_nh_param->aps_en;
    nh_param.merge_dsfwd = CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_MERGE_DSFWD);
    if(nh_param.aps_en)
    {
        nh_param.aps_bridge_group_id = p_nh_param->aps_bridge_group_id;
        sal_memcpy(&nh_param.p_oif, &(p_nh_param->p_oif), sizeof(ctc_nh_oif_info_t));
        sal_memcpy(nh_param.p_mac, (p_nh_param->p_mac), sizeof(mac_addr_t));
        nh_param.p_arp_id      = p_nh_param->p_arp_id;
    }
    else
    {
        nh_param.is_unrov_nh = CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_UNROV);  /* CTC_IP_NH_FLAG_UNROV*/
        nh_param.ttl_no_dec  = CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_TTL_NO_DEC);
    }
    nh_param.fpma = CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_FPMA);
    nh_param.mtu_no_chk = p_nh_param->mtu_no_chk;
    CTC_ERROR_RETURN(sys_goldengate_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}

/**
 @brief This function is to remove member from multicast bridge nexthop

 @param[in] nhid, nexthop id

 @return CTC_E_XXX
 */
int32
sys_goldengate_ipuc_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_nh_api_delete(lchip, nhid, SYS_NH_TYPE_IPUC));
    return CTC_E_NONE;
}

/**
 @brief This function is to update ipuc nexthop

 @param[in] nhid, nexthop id
 @param[in] vid, vlan  id
 @param[in] gport, global destination port
 @param[in] mac, nexthop mac address
 @param[in] update_type, nexthop update type

 @return CTC_E_XXX
 */
int32
sys_goldengate_ipuc_nh_update(uint8 lchip, uint32 nhid, ctc_ip_nh_param_t* p_nh_param,
                             sys_nh_entry_change_type_t update_type)
{
    sys_nh_param_ipuc_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    sal_memset(&nh_param, 0, sizeof(sys_nh_param_ipuc_t));
    nh_param.hdr.nhid = nhid;
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_IPUC;
    nh_param.hdr.change_type = update_type;
    sal_memcpy(&nh_param.oif, &p_nh_param->oif, sizeof(ctc_nh_oif_info_t));
    sal_memcpy(&nh_param.mac, &p_nh_param->mac, sizeof(mac_addr_t));
    sal_memcpy(nh_param.mac_sa, (p_nh_param->mac_sa), sizeof(mac_addr_t));
    nh_param.fpma = CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_FPMA);
    nh_param.mtu_no_chk = p_nh_param->mtu_no_chk;
    nh_param.arp_id      = p_nh_param->arp_id;
    nh_param.aps_en = p_nh_param->aps_en;
    nh_param.merge_dsfwd = CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_MERGE_DSFWD);
    nh_param.adjust_length = p_nh_param->adjust_length;

    if(nh_param.aps_en)
    {
        nh_param.aps_bridge_group_id = p_nh_param->aps_bridge_group_id;
        sal_memcpy(&nh_param.p_oif, &(p_nh_param->p_oif), sizeof(ctc_nh_oif_info_t));
        sal_memcpy(nh_param.p_mac, (p_nh_param->p_mac), sizeof(mac_addr_t));
        nh_param.p_arp_id      = p_nh_param->p_arp_id;
    }
    else
    {
        nh_param.is_unrov_nh = CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_UNROV);  /* CTC_IP_NH_FLAG_UNROV*/
        nh_param.ttl_no_dec  = CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_TTL_NO_DEC);
    }

    CTC_ERROR_RETURN(sys_goldengate_nh_api_update(lchip, nhid, (sys_nh_param_com_t*)(&nh_param)));
    return CTC_E_NONE;
}

int32
sys_goldengate_nh_bind_dsfwd_cb(uint8 lchip, uint32 nhid, sys_nh_update_dsnh_param_t* p_update_dsnh)
{

    sys_nh_info_com_t* p_nh_info = NULL;

    SYS_NH_INIT_CHECK(lchip);
    sys_goldengate_nh_get_nhinfo_by_nhid(lchip, nhid, (sys_nh_info_com_t**)&p_nh_info);

    if(!p_nh_info )
    {
        if(!p_update_dsnh->updateAd)
        {
            return CTC_E_NONE;
        }
        else
        {
              return CTC_E_NH_NOT_EXIST;
        }
    }

    switch(p_nh_info->hdr.nh_entry_type )
    {
    case SYS_NH_TYPE_IPUC:
		{
          sys_nh_info_ipuc_t* p_ipuc_info = (sys_nh_info_ipuc_t*)p_nh_info;

          if (p_update_dsnh->updateAd && p_ipuc_info->updateAd)
          {
              return CTC_E_NH_HR_NH_IN_USE;
          }
           p_ipuc_info->updateAd  = p_update_dsnh->updateAd;
           p_ipuc_info->data      = p_update_dsnh->data;

    	}
        break;
    case SYS_NH_TYPE_IP_TUNNEL:
		{
           sys_nh_info_ip_tunnel_t* p_ip_tunnel_info = (sys_nh_info_ip_tunnel_t*)p_nh_info;

           if (p_update_dsnh->updateAd && p_ip_tunnel_info->updateAd)
           {
               return CTC_E_NH_HR_NH_IN_USE;
           }
            p_ip_tunnel_info->updateAd  = p_update_dsnh->updateAd;
            p_ip_tunnel_info->data      = p_update_dsnh->data;

    	}
        break;
    case SYS_NH_TYPE_MPLS:
		{
           sys_nh_info_mpls_t* p_mpls_info = (sys_nh_info_mpls_t*)p_nh_info;

           if (p_update_dsnh->updateAd && p_mpls_info->updateAd)
           {
               return CTC_E_NH_HR_NH_IN_USE;
           }

            p_mpls_info->updateAd  = p_update_dsnh->updateAd;
            p_mpls_info->data      = p_update_dsnh->data;

    	}
        break;
    case SYS_NH_TYPE_TRILL:
		{
          sys_nh_info_trill_t* p_trill_info = (sys_nh_info_trill_t*)p_nh_info;

          if (p_update_dsnh->updateAd && p_trill_info->updateAd)
          {
              return CTC_E_NH_HR_NH_IN_USE;
          }

          p_trill_info->updateAd  = p_update_dsnh->updateAd;
          p_trill_info->data      = p_update_dsnh->data;

    	}
        break;
    case SYS_NH_TYPE_MISC:
		{
          sys_nh_info_misc_t* p_misc_info = (sys_nh_info_misc_t*)p_nh_info;

          if (p_update_dsnh->updateAd && p_misc_info->updateAd)
          {
              return CTC_E_NH_HR_NH_IN_USE;
          }

          p_misc_info->updateAd  = p_update_dsnh->updateAd;
          p_misc_info->data      = p_update_dsnh->data;

    	}
        break;
	default:
		break;
    }
    return CTC_E_NONE;
}

/**
 @brief This function is to create mcast nexthop

 @param[in] groupid, basic met offset of this multicast nexthop

 @param[in] p_nh_mcast_group,  group and member information this multicast nexthop

 @return CTC_E_XXX
 */

int32
sys_goldengate_mcast_nh_create(uint8 lchip, uint32 groupid, sys_nh_param_mcast_group_t* p_nh_mcast_group)
{
    sys_nh_param_mcast_t nh_mcast;
	sys_nh_info_mcast_t* p_mcast_info = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "groupid = %d\n", groupid);

    /*Sanity Check*/
    SYS_NH_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_nh_mcast_group);

    CTC_ERROR_RETURN(sys_goldengate_nh_check_max_glb_met_offset(lchip, groupid*2));
    SYS_LOCAL_CHIPID_CHECK(lchip);

    sal_memset(&nh_mcast, 0, sizeof(sys_nh_param_mcast_t));
    nh_mcast.hdr.nh_param_type = SYS_NH_TYPE_MCAST;
    if (p_nh_mcast_group->is_nhid_valid)
    {
        nh_mcast.hdr.nhid = p_nh_mcast_group->nhid;
        nh_mcast.hdr.is_internal_nh = FALSE;
    }
    else
    {   nh_mcast.hdr.nhid = SYS_NH_INVALID_NHID;
        nh_mcast.hdr.is_internal_nh = TRUE;
    }
    if ((p_nh_mcast_group->is_mirror) || (p_nh_mcast_group->stats_valid))
    {
        nh_mcast.hdr.have_dsfwd = TRUE;
    }

    nh_mcast.hdr.stats_valid = p_nh_mcast_group->stats_valid;
    nh_mcast.hdr.stats_id = p_nh_mcast_group->stats_id;

    nh_mcast.groupid = groupid;
    nh_mcast.p_member = &(p_nh_mcast_group->mem_info);
    nh_mcast.opcode = p_nh_mcast_group->opcode;
    nh_mcast.is_mirror = p_nh_mcast_group->is_mirror;

    CTC_ERROR_RETURN(sys_goldengate_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_mcast)));
    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo_by_nhid(lchip, nh_mcast.hdr.nhid,
                                           (sys_nh_info_com_t**)&p_mcast_info));
    p_nh_mcast_group->fwd_offset = p_mcast_info->hdr.dsfwd_info.dsfwd_offset;
    p_nh_mcast_group->nhid = nh_mcast.hdr.nhid;

    return CTC_E_NONE;
}

/**
 @brief This function is to update mcast nexthop

 @param[in] nhid,  nexthopid

 @return CTC_E_XXX
 */

int32
sys_goldengate_mcast_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_nh_api_delete(lchip, nhid, SYS_NH_TYPE_MCAST));
    return CTC_E_NONE;
}

/**
 @brief This function is to update mcast nexthop

 @param[in] p_nh_mcast_group,  group and member information this multicast nexthop

 @return CTC_E_XXX
 */

int32
sys_goldengate_mcast_nh_update(uint8 lchip, sys_nh_param_mcast_group_t* p_nh_mcast_group)
{
    sys_nh_param_mcast_t nh_mcast;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_NH_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_nh_mcast_group);
    SYS_LOCAL_CHIPID_CHECK(lchip);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", p_nh_mcast_group->nhid);

    sal_memset(&nh_mcast, 0, sizeof(sys_nh_param_mcast_t));
    nh_mcast.hdr.nh_param_type = SYS_NH_TYPE_MCAST;
    nh_mcast.p_member = &(p_nh_mcast_group->mem_info);
    nh_mcast.hdr.nhid = p_nh_mcast_group->nhid;
    nh_mcast.opcode = p_nh_mcast_group->opcode;

    CTC_ERROR_RETURN(sys_goldengate_nh_api_update(lchip, p_nh_mcast_group->nhid,
                                                 (sys_nh_param_com_t*)(&nh_mcast)));

    return CTC_E_NONE;
}

/**
 @brief This function is to create mpls nexthop

 @param[in] nhid  nexthop ID

 @param[in] dsnh_offset  DsNexthop offset used by this nexthop

 @param[in] p_nh_param  Nexthop param used to create this nexthop

 @return CTC_E_XXX
 */
int32
sys_goldengate_mpls_nh_create(uint8 lchip, uint32 nhid,   ctc_mpls_nexthop_param_t* p_nh_param)
{
    sys_nh_param_mpls_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_NH_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_nh_param);
     /*CTC_ERROR_RETURN(sys_goldengate_nh_check_max_glb_nh_offset(lchip, p_nh_param->dsnh_offset));*/

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d, dsnhoffset = %d\n", nhid, p_nh_param->dsnh_offset);
    sal_memset(&nh_param, 0, sizeof(sys_nh_param_mpls_t));
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_MPLS;
    nh_param.hdr.is_internal_nh = FALSE;
    nh_param.hdr.nhid = nhid;
    nh_param.p_mpls_nh_param = p_nh_param;


    CTC_ERROR_RETURN(sys_goldengate_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}

/**
 @brief This function is to remove mpls nexthop by nexthop id

 @param[in] nhid nexthop id

 @return CTC_E_XXX
 */
int32
sys_goldengate_mpls_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_nh_api_delete(lchip, nhid, SYS_NH_TYPE_MPLS));
    return CTC_E_NONE;
}

/**
 @brief This function is to update mpls nexthop

 @param[in] nhid, nexthop id

 @param[in] p_nh_param nexthop paramter

 @return CTC_E_XXX
 */
int32
sys_goldengate_mpls_nh_update(uint8 lchip, uint32 nhid, ctc_mpls_nexthop_param_t* p_nh_param,
                             sys_nh_entry_change_type_t change_type)
{
    sys_nh_param_mpls_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    sal_memset(&nh_param, 0, sizeof(sys_nh_param_mpls_t));
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_MPLS;
    nh_param.hdr.nhid = nhid;
    nh_param.p_mpls_nh_param = p_nh_param;
    nh_param.hdr.change_type = change_type;
    CTC_ERROR_RETURN(sys_goldengate_nh_api_update(lchip, nhid, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}

/**
 @brief This function is to create mist nexthop, include drop nh, tocpu nh and unresolve nh

 @param[in] nhid, nexthop id

 @param[in] nh_param_type, misc nexthop type

 @return CTC_E_XXX
 */
STATIC int32
_sys_goldengate_misc_nh_create(uint8 lchip, uint32 nhid, sys_goldengate_nh_type_t nh_param_type)
{
    sys_nh_param_special_t nh_para;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    switch (nh_param_type)
    {
    case SYS_NH_TYPE_DROP:
    case SYS_NH_TYPE_TOCPU:
    case SYS_NH_TYPE_UNROV:
        break;

    default:
        CTC_ERROR_RETURN(CTC_E_INVALID_PARAM);
    }

    sal_memset(&nh_para, 0, sizeof(sys_nh_param_special_t));
    nh_para.hdr.nhid = nhid;
    nh_para.hdr.nh_param_type = nh_param_type;
    nh_para.hdr.is_internal_nh = FALSE;
	nh_para.hdr.have_dsfwd = TRUE;

    CTC_ERROR_RETURN(sys_goldengate_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_para)));
    return CTC_E_NONE;
}

int32
sys_goldengate_ecmp_nh_create(uint8 lchip, uint32 nhid, ctc_nh_ecmp_nh_param_t* p_nh_parama)
{
    sys_nh_param_ecmp_t nh_param;

    SYS_NH_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_nh_parama);

    sal_memset(&nh_param, 0, sizeof(sys_nh_param_ecmp_t));

    nh_param.hdr.nh_param_type = SYS_NH_TYPE_ECMP;
    nh_param.hdr.is_internal_nh = FALSE;
    nh_param.p_ecmp_param = p_nh_parama;
    nh_param.hdr.nhid = nhid;

    CTC_ERROR_RETURN(sys_goldengate_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}

int32
sys_goldengate_ecmp_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_nh_api_delete(lchip, nhid, SYS_NH_TYPE_ECMP));
    return CTC_E_NONE;
}

int32
sys_goldengate_ecmp_nh_update(uint8 lchip, uint32 nhid, ctc_nh_ecmp_nh_param_t* p_nh_param)
{
    sys_nh_param_ecmp_t nh_param;
    ctc_nh_ecmp_nh_param_t ecmp_param;
    uint8 loop = 0;

    CTC_PTR_VALID_CHECK(p_nh_param);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    sal_memset(&nh_param, 0, sizeof(sys_nh_param_ecmp_t));
    sal_memset(&ecmp_param, 0, sizeof(ctc_nh_ecmp_nh_param_t));

    sal_memcpy(&ecmp_param, p_nh_param, sizeof(ctc_nh_ecmp_nh_param_t));
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_ECMP;
    nh_param.hdr.nhid = nhid;
    nh_param.p_ecmp_param = &ecmp_param;

    for (loop = 0; loop < p_nh_param->nh_num; loop++)
    {
        nh_param.p_ecmp_param->nh_num = 1;
        nh_param.p_ecmp_param->nhid[0] = p_nh_param->nhid[loop];
        CTC_ERROR_RETURN(sys_goldengate_nh_api_update(lchip, nhid, (sys_nh_param_com_t*)(&nh_param)));
    }

    return CTC_E_NONE;
}

/**
 @brief This function is to create IPE Loopback nexthop
 */
int32
sys_goldengate_iloop_nh_create(uint8 lchip, uint32 nhid, ctc_loopback_nexthop_param_t* p_nh_param)
{
    sys_nh_param_iloop_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_NH_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_nh_param);
    sal_memset(&nh_param, 0, sizeof(sys_nh_param_iloop_t));

    nh_param.hdr.nh_param_type = SYS_NH_TYPE_ILOOP;
    nh_param.hdr.is_internal_nh = FALSE;
    nh_param.p_iloop_param = p_nh_param;
    nh_param.hdr.nhid = nhid;
    CTC_ERROR_RETURN(sys_goldengate_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}

int32
sys_goldengate_iloop_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_nh_api_delete(lchip, nhid, SYS_NH_TYPE_ILOOP));
    return CTC_E_NONE;
}

/**
 @brief This function is to create rspan  nexthop
 */
int32
sys_goldengate_rspan_nh_create(uint8 lchip, uint32* nhid, uint32 dsnh_offset, ctc_rspan_nexthop_param_t* p_nh_param)
{
    sys_nh_param_rspan_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_NH_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_nh_param);
    CTC_PTR_VALID_CHECK(nhid);

    sal_memset(&nh_param, 0, sizeof(sys_nh_param_rspan_t));

    nh_param.hdr.nh_param_type = SYS_NH_TYPE_RSPAN;
    nh_param.hdr.is_internal_nh = (*nhid == CTC_MAX_UINT32_VALUE) ? TRUE : FALSE;
    nh_param.p_rspan_param = p_nh_param;
    nh_param.hdr.nhid = *nhid;
    nh_param.dsnh_offset = dsnh_offset;
    CTC_ERROR_RETURN(sys_goldengate_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_param)));

    if (nh_param.hdr.is_internal_nh)
    {
        *nhid = nh_param.hdr.nhid;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_rspan_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_nh_api_delete(lchip, nhid, SYS_NH_TYPE_RSPAN));
    return CTC_E_NONE;
}

int32
sys_goldengate_ip_tunnel_nh_create(uint8 lchip, uint32 nhid, ctc_ip_tunnel_nh_param_t* p_nh_param)
{
    sys_nh_param_ip_tunnel_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_NH_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_nh_param);
    CTC_GLOBAL_PORT_CHECK(p_nh_param->oif.gport);

    /* CTC_ERROR_RETURN(sys_goldengate_nh_check_max_glb_nh_offset(lchip, p_nh_param->dsnh_offset));*/
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d, dsnhoffset = %d, gport = %d\n",
                   nhid, p_nh_param->dsnh_offset, p_nh_param->oif.gport);

    sal_memset(&nh_param, 0, sizeof(nh_param));
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_IP_TUNNEL;
    nh_param.hdr.is_internal_nh = FALSE;
    nh_param.hdr.nhid = nhid;
    nh_param.p_ip_nh_param = p_nh_param;
    CTC_ERROR_RETURN(sys_goldengate_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}

int32
sys_goldengate_ip_tunnel_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_nh_api_delete(lchip, nhid, SYS_NH_TYPE_IP_TUNNEL));
    return CTC_E_NONE;
}

int32
sys_goldengate_ip_tunnel_nh_update(uint8 lchip, uint32 nhid, ctc_ip_tunnel_nh_param_t* p_nh_param)
{
    sys_nh_param_ip_tunnel_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    sal_memset(&nh_param, 0, sizeof(sys_nh_param_ip_tunnel_t));
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_IP_TUNNEL;
    nh_param.hdr.nhid = nhid;
    nh_param.p_ip_nh_param = p_nh_param;

    if (p_nh_param->upd_type == CTC_NH_UPD_FWD_ATTR)
    {
        nh_param.hdr.change_type = SYS_NH_CHANGE_TYPE_UPDATE_FWD_ATTR;
    }
    else if (p_nh_param->upd_type == CTC_NH_UPD_UNRSV_TO_FWD)
    {
        nh_param.hdr.change_type = SYS_NH_CHANGE_TYPE_UNROV_TO_FWD;
    }
    else
    {
        nh_param.hdr.change_type = SYS_NH_CHANGE_TYPE_FWD_TO_UNROV;
    }

    CTC_ERROR_RETURN(sys_goldengate_nh_api_update(lchip, nhid, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}

/**
 @brief The function is to add misc nexthop

*/

int32
sys_goldengate_nh_add_misc(uint8 lchip, uint32* nhid, ctc_misc_nh_param_t* p_nh_param, bool is_internal_nh)
{

    sys_nh_param_misc_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_NH_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_nh_param);

    if (p_nh_param->is_oif)
    {
        CTC_GLOBAL_PORT_CHECK(p_nh_param->oif.gport);
        CTC_MAX_VALUE_CHECK(p_nh_param->oif.vid, CTC_MAX_VLAN_ID);
    }
    else
    {
        CTC_GLOBAL_PORT_CHECK(p_nh_param->gport);
    }

    /* CTC_ERROR_RETURN(sys_goldengate_nh_check_max_glb_nh_offset(lchip, p_nh_param->dsnh_offset));*/
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d, dsnhoffset = %d, gport = %d\n",
                   *nhid, p_nh_param->dsnh_offset, p_nh_param->gport);

    sal_memset(&nh_param, 0, sizeof(nh_param));
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_MISC;
    nh_param.hdr.is_internal_nh = is_internal_nh;

    if (FALSE == is_internal_nh)
    {
        nh_param.hdr.nhid = *nhid;
    }

    nh_param.p_misc_param = p_nh_param;
    CTC_ERROR_RETURN(sys_goldengate_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_param)));

    if (TRUE == is_internal_nh)
    {
        *nhid = nh_param.hdr.nhid;
    }

    return CTC_E_NONE;
}

/**
 @brief The function is to remove misc nexthop

*/
int32
sys_goldengate_nh_remove_misc(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_nh_api_delete(lchip, nhid, SYS_NH_TYPE_MISC));
    return CTC_E_NONE;
}

/**
 @brief This function is to update misc nexthop
*/
int32
sys_goldengate_nh_update_misc(uint8 lchip, uint32 nhid, ctc_misc_nh_param_t* p_nh_param,
                             sys_nh_entry_change_type_t update_type)
{
    sys_nh_param_misc_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_nh_param);

     if (p_nh_param->is_oif)
    {
        CTC_GLOBAL_PORT_CHECK(p_nh_param->oif.gport);
        CTC_MAX_VALUE_CHECK(p_nh_param->oif.vid, CTC_MAX_VLAN_ID);
    }
    else
    {
        CTC_GLOBAL_PORT_CHECK(p_nh_param->gport);
    }

    sal_memset(&nh_param, 0, sizeof(sys_nh_param_misc_t));

    nh_param.hdr.nhid = nhid;
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_MISC;
    nh_param.hdr.change_type = update_type;
    nh_param.p_misc_param = p_nh_param;

    CTC_ERROR_RETURN(sys_goldengate_nh_api_update(lchip, nhid, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}


int32
sys_goldengate_nh_trill_create(uint8 lchip, uint32 nhid, ctc_nh_trill_param_t* p_nh_param)
{
    sys_nh_param_trill_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_NH_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_nh_param);
    CTC_GLOBAL_PORT_CHECK(p_nh_param->oif.gport);

    /* CTC_ERROR_RETURN(sys_goldengate_nh_check_max_glb_nh_offset(lchip, p_nh_param->dsnh_offset));*/
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d, dsnhoffset = %d, gport = %d\n",
                   nhid, p_nh_param->dsnh_offset, p_nh_param->oif.gport);

    sal_memset(&nh_param, 0, sizeof(nh_param));
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_TRILL;
    nh_param.hdr.is_internal_nh = FALSE;
    nh_param.hdr.nhid = nhid;
    nh_param.p_trill_nh_param = p_nh_param;
    CTC_ERROR_RETURN(sys_goldengate_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_trill_remove(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_nh_api_delete(lchip, nhid, SYS_NH_TYPE_TRILL));
    return CTC_E_NONE;
}

int32
sys_goldengate_nh_trill_update(uint8 lchip, uint32 nhid, ctc_nh_trill_param_t* p_nh_param)
{
    sys_nh_param_trill_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    sal_memset(&nh_param, 0, sizeof(sys_nh_param_trill_t));
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_TRILL;
    nh_param.hdr.nhid = nhid;
    nh_param.p_trill_nh_param = p_nh_param;

    if (p_nh_param->upd_type == CTC_NH_UPD_FWD_ATTR)
    {
        nh_param.hdr.change_type = SYS_NH_CHANGE_TYPE_UPDATE_FWD_ATTR;
    }
    else if (p_nh_param->upd_type == CTC_NH_UPD_UNRSV_TO_FWD)
    {
        nh_param.hdr.change_type = SYS_NH_CHANGE_TYPE_UNROV_TO_FWD;
    }
    else
    {
        nh_param.hdr.change_type = SYS_NH_CHANGE_TYPE_FWD_TO_UNROV;
    }

    CTC_ERROR_RETURN(sys_goldengate_nh_api_update(lchip, nhid, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}


bool
sys_goldengate_nh_is_ipmc_logic_rep_enable(uint8 lchip)
{
    int32 ret = 0;

    SYS_NH_INIT_CHECK(lchip);
    SYS_NH_LOCK(p_gg_nh_api_master[lchip]->brguc_info.brguc_mutex);
    ret = _sys_goldengate_nh_is_ipmc_logic_rep_enable(lchip);
    SYS_NH_UNLOCK(p_gg_nh_api_master[lchip]->brguc_info.brguc_mutex);

    return ret ? TRUE : FALSE;
}


int32
sys_goldengate_nh_set_ipmc_logic_replication(uint8 lchip, uint8 enable)
{
    SYS_NH_INIT_CHECK(lchip);
    SYS_NH_LOCK(p_gg_nh_api_master[lchip]->brguc_info.brguc_mutex);
    CTC_ERROR_RETURN_WITH_UNLOCK(_sys_goldengate_nh_set_ipmc_logic_replication(lchip, enable), p_gg_nh_api_master[lchip]->brguc_info.brguc_mutex);
    SYS_NH_UNLOCK(p_gg_nh_api_master[lchip]->brguc_info.brguc_mutex);

    return CTC_E_NONE;
}



