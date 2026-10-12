/**
 @file ctc_goldengate_mpls.c

 @date 2010-03-16

 @version v2.0


*/
/****************************************************************************
*
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_mpls.h"
#include "ctc_vector.h"
#include "sys_goldengate_scl.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_mpls.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_chip.h"
#include "ctc_stats.h"

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
 @brief SDK mpls module initilize

 @param[]

 @return CTC_E_XXX

*/

int32
ctc_goldengate_mpls_init(uint8 lchip, ctc_mpls_init_t* p_mpls_info)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    ctc_mpls_init_t mpls_init;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);
    if (NULL == p_mpls_info)
    {
        sal_memset(&mpls_init, 0, sizeof(ctc_mpls_init_t));
        mpls_init.space_info[0].enable = TRUE;
        mpls_init.space_info[0].sizetype = CTC_MPLS_LABEL_NUM_2K_TYPE;
        p_mpls_info = &mpls_init;
    }

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN( sys_goldengate_mpls_init(lchip, p_mpls_info));
    }

    return  CTC_E_NONE;
}

int32
ctc_goldengate_mpls_deinit(uint8 lchip)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN( sys_goldengate_mpls_deinit(lchip));
    }

    return CTC_E_NONE;
}

/**
 @brief

 @param[in] p_mpls_ilm, parameters used to add mpls ilm entry

 @return CTC_E_XXX

*/
int32
ctc_goldengate_mpls_add_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    int32 ret = CTC_E_NONE;
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NONE,
                                 ret,
                                 sys_goldengate_mpls_add_ilm(lchip, p_mpls_ilm));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start, lchip_end)
    {
        sys_goldengate_mpls_del_ilm(lchip, p_mpls_ilm);
    }

    return ret;
}

/**
@brief update a mpls entry

@param[in] p_mpls_ilm Data of the mpls entry

@remark  Update all properties of a MPLS ilm.

@return CTC_E_XXX

*/
extern int32
ctc_goldengate_mpls_update_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_mpls_update_ilm(lchip, p_mpls_ilm));
    }

    return CTC_E_NONE;
}

/**
 @brief

 @param[in]  p_mpls_ilm of the ilm entry

 @return CTC_E_XXX

*/
int32
ctc_goldengate_mpls_del_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_mpls_del_ilm(lchip, p_mpls_ilm));
    }

    return  CTC_E_NONE;
}

/**
 @brief

 @param[in] p_mpls_ilm index of the mpls ilm entry

 @param[out] p_mpls_ilm data of the mpls ilm entry

 @param[out] nh_id nexthop id array of the mpls ilm entry

 @return CTC_E_XXX

*/
int32
ctc_goldengate_mpls_get_ilm(uint8 lchip, uint32* nh_id, ctc_mpls_ilm_t* p_mpls_ilm)
{
    FEATURE_SUPPORT_CHECK(CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);
    return sys_goldengate_mpls_get_ilm(lchip, nh_id, p_mpls_ilm);
}


STATIC int32
_ctc_goldengate_mpls_map_property(ctc_mpls_property_t* p_mpls_pro,
                                                ctc_mpls_property_t* p_mpls_pro_out)
{
    int32 ret = CTC_E_NONE;
    uint32 ctc_type = 0;

    CTC_PTR_VALID_CHECK(p_mpls_pro);

    p_mpls_pro_out->label = p_mpls_pro->label;
    p_mpls_pro_out->space_id = p_mpls_pro->space_id;
    p_mpls_pro_out->value = p_mpls_pro->value;

    switch (p_mpls_pro->property_type)
    {
        case CTC_MPLS_ILM_QOS_DOMAIN:
            ctc_type = SYS_MPLS_ILM_QOS_DOMAIN;
            break;
        case CTC_MPLS_ILM_APS_SELECT:
            ctc_type = SYS_MPLS_ILM_APS_SELECT;
            break;
        case CTC_MPLS_ILM_OAM_TOCPU:
            ctc_type = SYS_MPLS_ILM_OAM_CHK;
            if(1 == *((uint32*)p_mpls_pro->value))
            {
                *((uint32*)p_mpls_pro_out->value) = 0x4;
            }
            else
            {
                *((uint32*)p_mpls_pro_out->value) = 0;
            }
            break;
        case CTC_MPLS_ILM_DATA_DISCARD:
            ctc_type = SYS_MPLS_ILM_OAM_CHK;
            if(1 == *((uint32*)p_mpls_pro->value))
            {
                *((uint32*)p_mpls_pro_out->value) = 0x5;
            }
            else
            {
                *((uint32*)p_mpls_pro_out->value) = 0;
            }
            break;
        case CTC_MPLS_ILM_ROUTE_MAC:
            ctc_type = SYS_MPLS_ILM_ROUTE_MAC;
            break;

        case CTC_MPLS_ILM_LLSP:
            ctc_type = SYS_MPLS_ILM_LLSP;
            break;
        case CTC_MPLS_ILM_OAM_MP_CHK_TYPE:
            ctc_type = SYS_MPLS_ILM_OAM_CHK;
            if ( *((uint32*)p_mpls_pro_out->value) >= CTC_MPLS_ILM_OAM_MP_CHK_TYPE_MAX)
            {
                 ret = CTC_E_INVALID_PARAM;
            }
            break;
        default:
            ret = CTC_E_INVALID_PARAM;
            break;
    }
    p_mpls_pro_out->property_type = ctc_type;

    return ret;
}

/**
 @brief set a mpls entry property

 @param[in] p_mpls_ilm Data of the mpls entry

 @return CTC_E_XXX

*/
int32
ctc_goldengate_mpls_set_ilm_property(uint8 lchip, ctc_mpls_property_t* p_mpls_pro)
{
    ctc_mpls_property_t mpls_pro;
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);
    sal_memset(&mpls_pro, 0, sizeof(ctc_mpls_property_t));

    CTC_ERROR_RETURN(_ctc_goldengate_mpls_map_property(p_mpls_pro, &mpls_pro));

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_mpls_set_ilm_property(lchip, &mpls_pro));
    }

    return  CTC_E_NONE;
}

/**
 @brief Add the l2vpn pw entry

 @param[in] p_mpls_pw Information of l2vpn pw entry

 @return CTC_E_XXX

*/
int32
ctc_goldengate_mpls_add_l2vpn_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw)
{
    int32 ret = CTC_E_NONE;
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_EXIST,
                                 ret,
                                 sys_goldengate_mpls_add_l2vpn_pw(lchip, p_mpls_pw));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start, lchip_end)
    {
        sys_goldengate_mpls_del_l2vpn_pw(lchip, p_mpls_pw);
    }


    return  CTC_E_NONE;
}

/**
 @brief Remove the l2vpn pw entry

 @param[in] label VC label of the l2vpn pw entry

 @return CTC_E_XXX

*/
int32
ctc_goldengate_mpls_del_l2vpn_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_mpls_del_l2vpn_pw(lchip, p_mpls_pw));
    }

    return  CTC_E_NONE;
}

