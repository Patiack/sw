/**
 @file ctc_greatbelt_nexthop.c

 @date 2009-11-18

 @version v2.0


*/
/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_nexthop.h"
#include "ctc_greatbelt_nexthop.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_port.h"

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
/****************************************************************************
 *
* Function
*
*****************************************************************************/
/**
 @brief SDK nexthop module initilize

 @param[in] nh_cfg  nexthop module global config

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nexthop_init(uint8 lchip, ctc_nh_global_cfg_t* p_nh_cfg)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    ctc_nh_global_cfg_t  nh_cfg;

    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    if (p_nh_cfg == NULL)
    {
        sal_memset(&nh_cfg, 0, sizeof(ctc_nh_global_cfg_t));
        nh_cfg.max_external_nhid = CTC_NH_DEFAULT_MAX_EXTERNAL_NEXTHOP_NUM;
        nh_cfg.max_tunnel_id = CTC_NH_DEFAULT_MAX_MPLS_TUNNEL_NUM;
        nh_cfg.nh_edit_mode = 0;
        nh_cfg.max_ecmp = SYS_GB_MAX_DEFAULT_ECMP_NUM;

    }
    else
    {
        sal_memcpy(&nh_cfg, p_nh_cfg, sizeof(ctc_nh_global_cfg_t));
    }

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_api_init(lchip, &nh_cfg));
    }

    return CTC_E_NONE;
}

int32
ctc_greatbelt_nexthop_deinit(uint8 lchip)
{
    uint8 lchip_start = 0;
    uint8 lchip_end   = 0;

    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_api_deinit(lchip));
    }

    return CTC_E_NONE;
}

/**
 @brief  Create normal ucast bridge nexthop entry

 @param[in] gport   Global port id

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_add_l2uc(uint8 lchip, uint32 gport,  ctc_nh_param_brguc_sub_type_t nh_type)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_EXIST,
                                 ret,
                                 sys_greatbelt_brguc_nh_create(lchip, gport, nh_type));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_greatbelt_brguc_nh_delete(lchip, gport);
    }

    return ret;
}

/**
 @brief Delete normal ucast bridge nexthop entry

 @param[in] gport   Global port id

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_remove_l2uc(uint8 lchip, uint32 gport)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_brguc_nh_delete(lchip, gport));
    }

    return ret;
}

/**
 @brief Get ucast nhid by type

 @param[in] gport global port of the system

 @param[in] nhid nexthop ID to get

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_get_l2uc(uint8 lchip, uint32 gport, ctc_nh_param_brguc_sub_type_t nh_type, uint32* nhid)
{
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_ERROR_RETURN(sys_greatbelt_l2_get_ucast_nh(lchip, gport, nh_type, nhid));

    return CTC_E_NONE;
}

/**
 @brief Create IPUC nexthop

 @param[in] nhid nexthop ID to be created

 @param[in] nexthop parameter used to create this ipuc nexthop

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_add_ipuc(uint8 lchip, uint32 nhid, ctc_ip_nh_param_t* p_nh_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_EXIST,
                                 ret,
                                 sys_greatbelt_ipuc_nh_create(lchip, nhid, p_nh_param));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_greatbelt_ipuc_nh_delete(lchip, nhid);
    }

    return ret;
}

/**
 @brief Remove IPUC nexthop

 @param[in] nhid, nexthop ID to be created

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_remove_ipuc(uint8 lchip, uint32 nhid)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_ipuc_nh_delete(lchip, nhid));
    }

    return ret;
}

/**
 @brief Update IPUC nexthop

 @param[in] nhid nexthop ID to be updated

 @param[in] nexthop parameter used to update this ipuc nexthop

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_update_ipuc(uint8 lchip, uint32 nhid, ctc_ip_nh_param_t* p_nh_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    sys_nh_entry_change_type_t update_type = SYS_NH_CHANGE_TYPE_NULL;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_nh_param);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        switch (p_nh_param->upd_type)
        {
            case CTC_NH_UPD_UNRSV_TO_FWD:
                update_type = SYS_NH_CHANGE_TYPE_UNROV_TO_FWD;
                break;
            case CTC_NH_UPD_FWD_TO_UNRSV:
                update_type = SYS_NH_CHANGE_TYPE_FWD_TO_UNROV;
                break;

            case CTC_NH_UPD_FWD_ATTR:
                update_type = SYS_NH_CHANGE_TYPE_UPDATE_FWD_ATTR;
                break;
            default:
                return CTC_E_INVALID_PARAM;
        }

        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_ipuc_nh_update(lchip, nhid, p_nh_param, update_type));

    }

    return ret;
}

/**********************************************************************************
                      Define ip tunnel nexthop functions
***********************************************************************************/
/**
 @brief Create ip tunnel nexthop

 @param[in] nhid nexthop ID to be created

 @param[in] p_nh_param nexthop parameter used to create this ip tunnel nexthop

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_add_ip_tunnel(uint8 lchip, uint32 nhid, ctc_ip_tunnel_nh_param_t* p_nh_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_EXIST,
                                 ret,
                                 sys_greatbelt_ip_tunnel_nh_create(lchip, nhid, p_nh_param));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_greatbelt_ip_tunnel_nh_delete(lchip, nhid);
    }

    return ret;
}

/**
 @brief Remove ip tunnel nexthop

 @param[in] nhid Nexthop ID to be removed

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_remove_ip_tunnel(uint8 lchip, uint32 nhid)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_ip_tunnel_nh_delete(lchip, nhid));
    }

    return ret;
}

/**
 @brief Update ip tunnel nexthop

 @param[in] nhid nexthop ID to be updated

 @param[in] p_nh_param nexthop parameter used to update this ip tunnel nexthop

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_update_ip_tunnel(uint8 lchip, uint32 nhid, ctc_ip_tunnel_nh_param_t* p_nh_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_ip_tunnel_nh_update(lchip, nhid, p_nh_param));
    }

    return ret;
}

/**
 @brief Create a mpls tunnel label

 @param[in] p_nh_param   tunnel label parameter

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_add_mpls_tunnel_label(uint8 lchip, uint16 tunnel_id, ctc_mpls_nexthop_tunnel_param_t* p_nh_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_EXIST,
                                 ret,
                                 sys_greatbelt_mpls_nh_create_tunnel_label(lchip, tunnel_id, p_nh_param));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_greatbelt_mpls_nh_remove_tunnel_label(lchip, tunnel_id);
    }

    return ret;
}

/**
 @brief remove a mpls tunnel label


 @param[in] p_nh_param   tunnel label parameter

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_remove_mpls_tunnel_label(uint8 lchip, uint16 tunnel_id)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_mpls_nh_remove_tunnel_label(lchip, tunnel_id));
    }

    return ret;
}

/**
 @brief update a mpls tunnel label

 @param[in] p_old_param   tunnel label parameter

  @param[in] p_new_param  tunnel label parameter

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_update_mpls_tunnel_label(uint8 lchip, uint16 tunnel_id, ctc_mpls_nexthop_tunnel_param_t* p_new_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_mpls_nh_update_tunnel_label(lchip, tunnel_id, p_new_param));
    }

    return ret;
}

/**
 @brief swap 2 mpls tunnel label


 @param[in] p_nh_param   tunnel label parameter

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_swap_mpls_tunnel_label(uint8 lchip, uint16 old_tunnel_id,uint16 new_tunnel_id)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_mpls_nh_swap_tunnel(lchip, old_tunnel_id,new_tunnel_id));
    }

    return ret;
}

/**
 @brief Create a mpls nexthop

 @param[in] nhid nexthop ID to be created

 @param[in] p_nh_param   nexthop parameter used to create this nexthop

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_add_mpls(uint8 lchip, uint32 nhid, ctc_mpls_nexthop_param_t* p_nh_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    ctc_mpls_nexthop_param_t nh_param = {0};

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_nh_param);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        sal_memset(&nh_param, 0, sizeof(ctc_mpls_nexthop_param_t));
        sal_memcpy(&nh_param, (uint8* )p_nh_param, sizeof(ctc_mpls_nexthop_param_t));
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_EXIST,
                                 ret,
                                 sys_greatbelt_mpls_nh_create(lchip, nhid, &nh_param));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_greatbelt_mpls_nh_delete(lchip, nhid);
    }

    return ret;
}

/**
 @brief Remove mpls nexthop

 @param[in] nhid nexthop ID to be removed

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_remove_mpls(uint8 lchip, uint32 nhid)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_mpls_nh_delete(lchip, nhid));
    }

    return ret;
}

/**
 @brief Update a mpls unresolved nexthop to forwarded mpls push nexthop

 @param[in] nhid nexthop ID to be updated

 @param[in] p_nh_param nexthop parameter used to update this nexthop

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_update_mpls(uint8 lchip, uint32 nhid, ctc_mpls_nexthop_param_t* p_nh_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 update_type              = 0;
    CTC_PTR_VALID_CHECK(p_nh_param);


    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    if (p_nh_param->upd_type == CTC_NH_UPD_FWD_ATTR)
    {
        update_type = SYS_NH_CHANGE_TYPE_UPDATE_FWD_ATTR;
    }
    else if (p_nh_param->upd_type == CTC_NH_UPD_UNRSV_TO_FWD)
    {
        update_type = SYS_NH_CHANGE_TYPE_UNROV_TO_FWD;
    }
    else
    {
        update_type = SYS_NH_CHANGE_TYPE_FWD_TO_UNROV;
    }

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_mpls_nh_update(lchip, nhid, p_nh_param, update_type));
    }

    return ret;
}

/**
 @brief Create a ipe loopback nexthop

 @param[in] nhid nexthop ID to be created

 @param[in] p_nh_param loopback nexthop parameters

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_add_iloop(uint8 lchip, uint32 nhid, ctc_loopback_nexthop_param_t* p_nh_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_EXIST,
                                 ret,
                                 sys_greatbelt_iloop_nh_create(lchip, nhid, p_nh_param));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_greatbelt_iloop_nh_delete(lchip, nhid);
    }

    return ret;
}

/**
 @brief Remove ipe loopback nexthop

 @param[in] nhid nexthop ID to be removed

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_remove_iloop(uint8 lchip, uint32 nhid)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_iloop_nh_delete(lchip, nhid));
    }

    return ret;
}

/**
 @brief Create a rspan(remote mirror) nexthop

 @param[in] nhid nexthop ID to be created

 @param[in] p_nh_param remote mirror nexthop parameters

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_add_rspan(uint8 lchip, uint32 nhid, ctc_rspan_nexthop_param_t* p_nh_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_EXIST,
                                 ret,
                                 sys_greatbelt_rspan_nh_create(lchip, &nhid, p_nh_param->dsnh_offset, p_nh_param));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_greatbelt_rspan_nh_delete(lchip, nhid);
    }

    return ret;
}

/**
 @brief Remove rspan(remote mirror) nexthop

 @param[in] nhid nexthop ID to be removed

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_remove_rspan(uint8 lchip, uint32 nhid)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_rspan_nh_delete(lchip, nhid));
    }

    return ret;
}

/**
 @brief Create a ECMP nexthop

 @param[in] pdata Create data

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_add_ecmp(uint8 lchip, uint32 nhid, ctc_nh_ecmp_nh_param_t* p_nh_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_EXIST,
                                 ret,
                                 sys_greatbelt_ecmp_nh_create(lchip, nhid, p_nh_param));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_greatbelt_ecmp_nh_delete(lchip, nhid);
    }

    return ret;
}

/**
 @brief Delete a ECMP nexthop

 @param[in] nhid nexthop ID of ECMP to be removed

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_remove_ecmp(uint8 lchip, uint32 nhid)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_ecmp_nh_delete(lchip, nhid));
    }

    return ret;
}

/**
 @brief Update a ECMP nexthop

 @param[in] nhid nexthop ID of ECMP to be updated

 @param[in] subnhid member nexthop ID of ECMP

 @param[in] op_type Add or Remove nexthop member nexthop into/from ECMP group

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_update_ecmp(uint8 lchip, uint32 nhid, ctc_nh_ecmp_nh_param_t* p_nh_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_ecmp_nh_update(lchip, nhid, p_nh_param));
    }

    return ret;
}

/**
 @brief Set the maximum ECMP paths allowed for a route

 @param[out] max_ecmp the maximum ECMP paths allowed for a route

 @remark  Get the maximum ECMP paths allowed for a route ,the default value is CTC_DEFAULT_MAX_ECMP_NUM

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_set_max_ecmp(uint8 lchip, uint16 max_ecmp)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_set_max_ecmp(lchip, max_ecmp));
    }

    return CTC_E_NONE;
}

/**
 @brief Get the maximum ECMP paths allowed for a route

 @param[out] max_ecmp the maximum ECMP paths allowed for a route

 @remark  Get the maximum ECMP paths allowed for a route

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_get_max_ecmp(uint8 lchip, uint16* max_ecmp)
{
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_max_ecmp(lchip, max_ecmp));

    return CTC_E_NONE;
}

/**
 @brief The function is to create Egress Vlan Editing nexthop or APS Egress Vlan Editing nexthop

 @param[in] nhid nexthop ID to be created

 @param[in] nh_param        nexthop parameter used to create this nexthop

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_add_xlate(uint8 lchip, uint32 nhid, ctc_vlan_edit_nh_param_t* p_nh_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint32 dsnh_offset             = 0;
    uint32 gport_or_aps_bridge_id  = 0;
    ctc_vlan_egress_edit_info_t* p_vlan_edit_info = NULL;
    ctc_vlan_egress_edit_info_t* p_vlan_edit_info_p = NULL;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_nh_param);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        if (p_nh_param->aps_en)
        {
            dsnh_offset = p_nh_param->dsnh_offset;
            gport_or_aps_bridge_id = p_nh_param->gport_or_aps_bridge_id;
            p_vlan_edit_info = &(p_nh_param->vlan_edit_info);
            p_vlan_edit_info_p = &(p_nh_param->vlan_edit_info_p);
            CTC_FOREACH_ERROR_RETURN(CTC_E_NH_EXIST,
                                     ret,
                                     sys_greatbelt_aps_egress_vlan_edit_nh_create(lchip, nhid, dsnh_offset, gport_or_aps_bridge_id,
                                     p_vlan_edit_info, p_vlan_edit_info_p));
        }
        else
        {
            dsnh_offset = p_nh_param->dsnh_offset;
            gport_or_aps_bridge_id = p_nh_param->gport_or_aps_bridge_id;
            p_vlan_edit_info = &(p_nh_param->vlan_edit_info);
            CTC_FOREACH_ERROR_RETURN(CTC_E_NH_EXIST,
                                     ret,
                                     sys_greatbelt_egress_vlan_edit_nh_create(lchip, nhid, gport_or_aps_bridge_id,
                                                                       p_vlan_edit_info, dsnh_offset, p_nh_param));
        }

    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_greatbelt_egress_vlan_edit_nh_delete(lchip, nhid);
    }

    return ret;
}

/**
 @brief The function is to remove Egress Vlan Editing nexthop or APS Egress Vlan Editing nexthop

 @param[in] nhid            Egress vlan Editing nexthop id or APS Egress vlan Editing nexthop id

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_remove_xlate(uint8 lchip, uint32 nhid)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_egress_vlan_edit_nh_delete(lchip, nhid));
    }

    return ret;
}

/**
 @brief This function is to get mcast nexthop id

 @param[in] group_id   mcast group id

 @param[out] p_nhid   get nhid

 @remark  get mcast nexthop id by mcast group id

 @return CTC_E_XXX
*/
int32
ctc_greatbelt_nh_get_mcast_nh(uint8 lchip, uint32 group_id, uint32* p_nhid)
{
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_ERROR_RETURN(sys_greatbelt_mcast_nh_get_nhid(lchip, group_id, p_nhid));

    return CTC_E_NONE;
}


/**
 @brief This function is to create mcast nexthop

 @param[in] nhid   nexthop ID to be created

 @param[in] p_nh_mcast_group   nexthop parameter used to create this mcast nexthop

 @return CTC_E_XXX
 */
int32
ctc_greatbelt_nh_add_mcast(uint8 lchip, uint32 nhid, ctc_mcast_nh_param_group_t* p_nh_mcast_group)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    sys_nh_param_mcast_group_t nh_mcast_group;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    sal_memset(&nh_mcast_group, 0, sizeof(nh_mcast_group));

    nh_mcast_group.nhid = nhid;
    nh_mcast_group.is_nhid_valid = 1;
    nh_mcast_group.dsfwd_valid = 0;
    nh_mcast_group.is_mirror = p_nh_mcast_group->is_mirror;
    nh_mcast_group.stats_valid = p_nh_mcast_group->stats_en;
    nh_mcast_group.stats_id    = p_nh_mcast_group->stats_id;

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_EXIST,
                                 ret,
                                 sys_greatbelt_mcast_nh_create(lchip, p_nh_mcast_group->mc_grp_id, &nh_mcast_group));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_greatbelt_mcast_nh_delete(lchip, nhid);
    }

    return ret;
}

/**
 @brief This function is to delete mcast nexthop

 @param[in] nhid   nexthopid

 @return CTC_E_XXX
 */
int32
ctc_greatbelt_nh_remove_mcast(uint8 lchip, uint32 nhid)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_mcast_nh_delete(lchip, nhid));
    }

    return ret;
}

/**
 @brief This function is to update mcast nexthop

 @param[in] p_nh_mcast_group,  nexthop parameter used to add/remove  mcast member

 @return CTC_E_XXX
 */
int32
ctc_greatbelt_nh_update_mcast(uint8 lchip, uint32 nhid, ctc_mcast_nh_param_group_t* p_nh_mcast_group)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    uint8 all_lchip                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 aps_brg_en               = 0;
    uint16 dest_id                 = 0;
    uint8 idx                      = 0;
    uint8 pid                      = 0;
    uint8 lport                    = 0;
    sys_nh_param_mcast_group_t nh_mcast_group;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_nh_mcast_group);
    sal_memset(&nh_mcast_group, 0, sizeof(sys_nh_param_mcast_group_t));

    nh_mcast_group.nhid = nhid;

    if (CTC_NH_PARAM_MCAST_ADD_MEMBER == p_nh_mcast_group->opcode)
    {
        nh_mcast_group.opcode = SYS_NH_PARAM_MCAST_ADD_MEMBER;
    }
    else if (CTC_NH_PARAM_MCAST_DEL_MEMBER == p_nh_mcast_group->opcode)
    {
        nh_mcast_group.opcode = SYS_NH_PARAM_MCAST_DEL_MEMBER;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((p_nh_mcast_group->mem_info.member_type != CTC_NH_PARAM_MEM_BRGMC_LOCAL)
        && (p_nh_mcast_group->mem_info.member_type != CTC_NH_PARAM_MEM_IPMC_LOCAL)
        && p_nh_mcast_group->mem_info.port_bmp_en)
        {
            return CTC_E_INVALID_PARAM;
        }

    switch (p_nh_mcast_group->mem_info.member_type)
    {
    case CTC_NH_PARAM_MEM_BRGMC_LOCAL:
        {
            nh_mcast_group.mem_info.member_type = SYS_NH_PARAM_BRGMC_MEM_LOCAL;
			nh_mcast_group.mem_info.is_reflective = p_nh_mcast_group->mem_info.is_source_check_dis;
            if (!p_nh_mcast_group->mem_info.port_bmp_en)
            {
                nh_mcast_group.mem_info.destid = CTC_MAP_GPORT_TO_LPORT(p_nh_mcast_group->mem_info.destid);
                nh_mcast_group.mem_info.is_linkagg = CTC_IS_LINKAGG_PORT(p_nh_mcast_group->mem_info.destid);
                if (nh_mcast_group.mem_info.is_linkagg)
                {
                    all_lchip = 1;
                }
                else
                {
                    SYS_MAP_GPORT_TO_LCHIP(p_nh_mcast_group->mem_info.destid, lchip)
                    if (!sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_mcast_group->mem_info.destid)))
                    {
                        return CTC_E_INVALID_CHIP_ID;
                    }
                }
            }
        }
        break;

    case CTC_NH_PARAM_MEM_IPMC_LOCAL:
        {
            nh_mcast_group.mem_info.member_type =
            p_nh_mcast_group->mem_info.is_vlan_port ? SYS_NH_PARAM_BRGMC_MEM_LOCAL : SYS_NH_PARAM_IPMC_MEM_LOCAL;
            nh_mcast_group.mem_info.l3if_type = p_nh_mcast_group->mem_info.l3if_type;
            nh_mcast_group.mem_info.vid  = p_nh_mcast_group->mem_info.vid;
			nh_mcast_group.mem_info.is_reflective = p_nh_mcast_group->mem_info.is_source_check_dis;
            if (!p_nh_mcast_group->mem_info.port_bmp_en)
            {
                CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, p_nh_mcast_group->mem_info.destid));
                nh_mcast_group.mem_info.destid = CTC_MAP_GPORT_TO_LPORT(p_nh_mcast_group->mem_info.destid);
                nh_mcast_group.mem_info.l3if_type = p_nh_mcast_group->mem_info.l3if_type;
                nh_mcast_group.mem_info.vid  = p_nh_mcast_group->mem_info.vid;
                nh_mcast_group.mem_info.is_linkagg = CTC_IS_LINKAGG_PORT(p_nh_mcast_group->mem_info.destid);
                if (nh_mcast_group.mem_info.is_linkagg)
                {
                    all_lchip = 1;
                }
                else
                {
                    SYS_MAP_GPORT_TO_LCHIP(p_nh_mcast_group->mem_info.destid, lchip)
                    if (!sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_mcast_group->mem_info.destid)))
                    {
                        return CTC_E_INVALID_CHIP_ID;
                    }
                }
            }
        }
        break;

    case CTC_NH_PARAM_MEM_LOCAL_WITH_NH:
        {
            sys_nh_info_com_t* p_nh_com_info;

            nh_mcast_group.mem_info.ref_nhid = p_nh_mcast_group->mem_info.ref_nhid;
            nh_mcast_group.mem_info.is_reflective = p_nh_mcast_group->mem_info.is_source_check_dis;

            CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, p_nh_mcast_group->mem_info.ref_nhid, (sys_nh_info_com_t**)&p_nh_com_info));
            if (p_nh_com_info->hdr.nh_entry_type == SYS_NH_TYPE_RSPAN)
            {
                dest_id = p_nh_mcast_group->mem_info.destid;
                nh_mcast_group.mem_info.member_type = SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH;
            }
            else if(CTC_FLAG_ISSET(p_nh_mcast_group->mem_info.flag, CTC_MCAST_NH_FLAG_ASSIGN_PORT))
            {
                dest_id = p_nh_mcast_group->mem_info.destid;
                nh_mcast_group.mem_info.member_type = SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH_DESTID;
            }
            else
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_get_port(lchip, p_nh_mcast_group->mem_info.ref_nhid, &aps_brg_en, &dest_id));
                nh_mcast_group.mem_info.member_type = aps_brg_en ? SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE \
                    : SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH;
            }

            if (aps_brg_en)
            {
                nh_mcast_group.mem_info.destid = dest_id;

            }
            else
            {
                CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, dest_id));
                nh_mcast_group.mem_info.destid = CTC_MAP_GPORT_TO_LPORT(dest_id);
            }

                if (aps_brg_en || CTC_IS_LINKAGG_PORT(dest_id))
                {
                    nh_mcast_group.mem_info.is_linkagg  = !aps_brg_en;
                    all_lchip = 1;
                }
                else
                {
                    nh_mcast_group.mem_info.is_linkagg = 0;
                    SYS_MAP_GPORT_TO_LCHIP(dest_id, lchip)
                    if (!sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(dest_id)))
                    {
                        return CTC_E_INVALID_CHIP_ID;
                    }
                }
         }
            break;

    case CTC_NH_PARAM_MEM_REMOTE:
        nh_mcast_group.mem_info.member_type = SYS_NH_PARAM_MCAST_MEM_REMOTE;
        nh_mcast_group.mem_info.destid = p_nh_mcast_group->mem_info.destid & 0xFF;/*profile id*/
           SYS_MAP_GCHIP_TO_LCHIP(((p_nh_mcast_group->mem_info.destid >> 8) & 0xFF), lchip);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }


    lchip_start = lchip;
    lchip_end = lchip + 1;

    if (p_nh_mcast_group->mem_info.port_bmp_en)
    {
        if (p_nh_mcast_group->mem_info.gchip_id == CTC_LINKAGG_CHIPID)
        {
            all_lchip = 1;
            nh_mcast_group.mem_info.is_linkagg = 1;
        }
        else
        {
            SYS_MAP_GCHIP_TO_LCHIP(p_nh_mcast_group->mem_info.gchip_id, lchip);
        }
        CTC_FOREACH_LCHIP(lchip_start, lchip_end, all_lchip)
        {
            for (idx = 0; idx < sizeof(p_nh_mcast_group->mem_info.port_bmp) / 4; idx++)
            {
                if (p_nh_mcast_group->mem_info.port_bmp[idx] == 0)
                {
                    continue;
                }
                for (pid = 0; pid < 32; pid++)
                {
                    if (CTC_IS_BIT_SET(p_nh_mcast_group->mem_info.port_bmp[idx], pid))
                    {
                        lport = pid + idx * 32;
                        nh_mcast_group.mem_info.destid= lport;

                        ret = sys_greatbelt_mcast_nh_update(lchip, &nh_mcast_group);
                        if ((ret < 0) && (nh_mcast_group.opcode == SYS_NH_PARAM_MCAST_ADD_MEMBER))
                        {
                            p_nh_mcast_group->opcode = CTC_NH_PARAM_MCAST_DEL_MEMBER;
                            goto error;
                        }
                    }
                }
            }
        }
    }
    else
    {
        CTC_FOREACH_LCHIP(lchip_start, lchip_end, all_lchip)
        {
            CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                     ret,
                                     sys_greatbelt_mcast_nh_update(lchip, &nh_mcast_group));
        }
    }


    return ret;

    error:
        ctc_greatbelt_nh_update_mcast(lchip, nhid, p_nh_mcast_group);
        return ret;
}

/**
 @brief The function is to create misc nexthop

 @param[in] nhid nexthop ID to be created

 @param[in] p_nh_param  nexthop parameter used to create this nexthop

 @return CTC_E_XXX

*/

int32
ctc_greatbelt_nh_add_misc(uint8 lchip, uint32 nhid, ctc_misc_nh_param_t* p_nh_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_EXIST,
                                 ret,
                                 sys_greatbelt_nh_add_misc(lchip, &nhid, p_nh_param, FALSE));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_greatbelt_nh_remove_misc(lchip, nhid);
    }

    return ret;
}

/**
 @brief The function is to remove misc nexthop

 @param[in] nhid     nexthop ID to be created

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_remove_misc(uint8 lchip, uint32 nhid)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_nh_remove_misc(lchip, nhid));
    }

    return ret;

}

/**
 @brief The function is to get resolved dsnh_offset  by type

 @param[in] type  the type of reserved dsnh_offset

 @param[out] p_offset  Retrieve reserved dsnh_offset


 @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_get_resolved_dsnh_offset(uint8 lchip, ctc_nh_reserved_dsnh_offset_type_t type, uint32* p_offset)
{
    sys_greatbelt_nh_res_offset_type_t nh_type;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    switch (type)
    {
    case CTC_NH_RES_DSNH_OFFSET_TYPE_BRIDGE_NH:   /**< [HB.GB] Reserved L2Uc Nexthop */
        nh_type = SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH;
        break;

    case CTC_NH_RES_DSNH_OFFSET_TYPE_BYPASS_NH:        /**< [HB.GB] Reserved bypass Nexthop */
        nh_type = SYS_NH_RES_OFFSET_TYPE_BYPASS_NH;
        break;

    case CTC_NH_RES_DSNH_OFFSET_TYPE_MIRROR_NH:         /**< [HB.GB] Reserved mirror Nexthop*/
        nh_type = SYS_NH_RES_OFFSET_TYPE_MIRROR_NH;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_resolved_offset(lchip, nh_type, p_offset));

    return CTC_E_NONE;
}

/**
 @brief The function is to get  dsnh_offset by nhid

 @param[in] nhid  nexthop ID to be created

 @param[out] p_nh_info  Retrieve Nexthop information @return CTC_E_XXX

*/
int32
ctc_greatbelt_nh_get_nh_info(uint8 lchip, uint32 nhid, ctc_nh_info_t* p_nh_info)
{
    sys_nh_info_dsnh_t nhinfo;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    sal_memset(&nhinfo, 0, sizeof(sys_nh_info_dsnh_t));
    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, nhid, &nhinfo));

    p_nh_info->dsnh_offset[0] = nhinfo.dsnh_offset;

    if(nhinfo.is_mcast)
    {
        p_nh_info->gport =  nhinfo.dest_id;
        if (0 ==p_nh_info->buffer_len )
        {
            return CTC_E_INTR_INVALID_PARAM;
        }

        CTC_ERROR_RETURN(sys_greatbelt_nh_get_mcast_member(lchip, nhid, p_nh_info));
    }
    else
    {
        p_nh_info->gport = (nhinfo.dest_chipid << 8) | (nhinfo.dest_id & 0x7F);
    }

    if(nhinfo.aps_en)
    {
        CTC_SET_FLAG(p_nh_info->flag, CTC_NH_INFO_FLAG_IS_APS_EN);
        p_nh_info->gport = nhinfo.dest_id;
    }

    if(nhinfo.nexthop_ext)
    {
        CTC_SET_FLAG(p_nh_info->flag, CTC_NH_INFO_FLAG_IS_DSNH8W);
    }

    if(nhinfo.drop_pkt)
    {
        CTC_SET_FLAG(p_nh_info->flag, CTC_NH_INFO_FLAG_IS_UNROV);
    }

    if(nhinfo.is_mcast)
    {
        CTC_SET_FLAG(p_nh_info->flag, CTC_NH_INFO_FLAG_IS_MCAST);
    }

    if(nhinfo.ecmp_valid)
    {
        CTC_SET_FLAG(p_nh_info->flag, CTC_NH_INFO_FLAG_IS_ECMP);
        p_nh_info->ecmp_cnt  = nhinfo.ecmp_cnt;
        p_nh_info->valid_ecmp_cnt = nhinfo.valid_cnt;
        if(nhinfo.ecmp_cnt)
        {
            sal_memcpy(&p_nh_info->ecmp_mem_nh[0], &nhinfo.nh_array[0], nhinfo.ecmp_cnt*sizeof(uint32));
        }
    }

    return CTC_E_NONE;

}

/**
 @brief The function is to set nhop drop packet nhid

 @param[in] nhid  nexthop ID to be created

 @param[out] p_nh_drop  Retrieve Nexthop drop iformation
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_set_nh_drop(uint8 lchip, uint32 nhid, ctc_nh_drop_info_t* p_nh_drop)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_set_nh_drop(lchip, nhid, p_nh_drop));
    }

    return ret;

}



/**
 @brief Add Next Hop Router Mac

 @param[in] p_param nexthop MAC information

 @remark  Add Next Hop Router Mac
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_add_nexthop_mac(uint8 lchip, uint16 arp_id, ctc_nh_nexthop_mac_param_t* p_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_EXIST,
                                 ret,
                                 sys_greatbelt_nh_add_nexthop_mac(lchip,  arp_id, p_param));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start, lchip_end)
    {
        sys_greatbelt_nh_remove_nexthop_mac(lchip, arp_id);
    }

    return ret;

}

/**
 @brief Remove Next Hop Router Mac

 @param[in] p_param nexthop MAC information

 @remark  Remove Next Hop Router Mac
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_remove_nexthop_mac(uint8 lchip, uint16 arp_id)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_NEXTHOP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_nh_remove_nexthop_mac(lchip, arp_id));
    }

    return ret;

}

/**
 @brief Update Next Hop Router Mac

 @param[in] p_old_param   Old nexthop MAC information
 @param[in] p_new_param   New nexthop MAC information

 @remark  Update Next Hop Router Mac
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_update_nexthop_mac(uint8 lchip, uint16 arp_id, ctc_nh_nexthop_mac_param_t* p_new_param)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NH_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_nh_update_nexthop_mac(lchip, arp_id, p_new_param));
    }

    return ret;
}



