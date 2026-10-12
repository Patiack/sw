/**
 @file ctc_greatbelt_mpls.c

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
#include "sys_greatbelt_scl.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_mpls.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_chip.h"
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

 @param[in] p_mpls_info Data of the mpls initialization

 @return CTC_E_XXX

*/

int32
ctc_greatbelt_mpls_init(uint8 lchip, ctc_mpls_init_t* p_mpls_info)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    ctc_mpls_init_t mpls_init;

    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_MPLS);
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
        CTC_ERROR_RETURN( sys_greatbelt_mpls_init(lchip, p_mpls_info));
    }

    return  CTC_E_NONE;
}

int32
ctc_greatbelt_mpls_deinit(uint8 lchip)
{
    uint8 lchip_start = 0;
    uint8 lchip_end   = 0;

    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_mpls_deinit(lchip));
    }

    return CTC_E_NONE;
}

/**
 @brief Add a mpls entry

 @param[in] p_mpls_ilm, parameters used to add mpls ilm entry

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_mpls_add_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    int32 ret = CTC_E_NONE;
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NONE,
                                 ret,
                                 sys_greatbelt_mpls_add_ilm(lchip, p_mpls_ilm));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start, lchip_end)
    {
        sys_greatbelt_mpls_del_ilm(lchip, p_mpls_ilm);
    }

    return ret;
}

/**
 @brief update a mpls entry

 @param[in] p_mpls_ilm, parameters used to update mpls ilm entry

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_mpls_update_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_mpls_update_ilm(lchip, p_mpls_ilm));
    }

    return CTC_E_NONE;
}

/**
 @brief set a mpls entry property

 @param[in] p_mpls_ilm Data of the mpls entry

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_mpls_set_ilm_property(uint8 lchip, ctc_mpls_property_t* p_mpls_pro)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_mpls_set_ilm_property(lchip, p_mpls_pro));
    }
    return  CTC_E_NONE;
}

/**
 @brief Remove a mpls entry

 @param[in] p_mpls_ilm Data of the mpls entry

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_mpls_del_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_mpls_del_ilm(lchip, p_mpls_ilm));
    }

    return  CTC_E_NONE;
}

/**
 @brief Get information of a mpls ilm entry, inlucde nexthop IDs

 @param[out] nh_id Nexthop id array of the mpls ilm entry,the entrie info of the mpls ilm entry

 @param[in] p_mpls_ilm Index of the mpls ilm entry

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_mpls_get_ilm(uint8 lchip, uint32* nh_id, ctc_mpls_ilm_t* p_mpls_ilm)
{
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);
    return sys_greatbelt_mpls_get_ilm(lchip, nh_id, p_mpls_ilm);
}

/**
 @brief add mpls stats

 @param[in] stats_index index of the mpls labe stats to be add

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_mpls_add_stats(uint8 lchip, ctc_mpls_stats_index_t* stats_index)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_MPLS);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_mpls_add_stats(lchip, stats_index));
    }

    return CTC_E_NONE;
}

/**
 @brief delete mpls stats

 @param[in] stats_index index of the mpls labe stats to be delete

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_mpls_remove_stats(uint8 lchip, ctc_mpls_stats_index_t* stats_index)
{
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_MPLS);
    return sys_greatbelt_mpls_del_stats(lchip, stats_index);
}

/**
 @brief Add the l2vpn pw entry

 @param[in] p_mpls_pw Information of l2vpn pw entry

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_mpls_add_l2vpn_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw)
{
    int32 ret = CTC_E_NONE;
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_EXIST,
                                 ret,
                                 sys_greatbelt_mpls_add_l2vpn_pw(lchip, p_mpls_pw));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start, lchip_end)
    {
        sys_greatbelt_mpls_del_l2vpn_pw(lchip, p_mpls_pw);
    }


    return  ret;
}

/**
 @brief Remove the l2vpn pw entry

 @param[in] label VC label of the l2vpn pw entry

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_mpls_del_l2vpn_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_MPLS);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_mpls_del_l2vpn_pw(lchip, p_mpls_pw));
    }

    return  CTC_E_NONE;
}


