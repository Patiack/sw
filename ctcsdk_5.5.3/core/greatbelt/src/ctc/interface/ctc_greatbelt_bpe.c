/**
 @file ctc_greatbelt_bpe.c

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2013-05-28

 @version v2.0

 This file contains bpe function interface.
*/

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_debug.h"

#include "ctc_greatbelt_bpe.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_bpe.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

/**
 @brief The function is to init the bpe module

 @param[in] bpe_global_cfg bpe global configure

 @return CTC_E_XXX

*/
 int32
ctc_greatbelt_bpe_init(uint8 lchip, void* bpe_global_cfg)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    ctc_bpe_global_cfg_t bpe_cfg;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_BPE);
    LCHIP_CHECK(lchip);

    if (NULL == bpe_global_cfg)
    {
        /*set default value*/
        sal_memset(&bpe_cfg, 0, sizeof(ctc_bpe_global_cfg_t));
        bpe_cfg.intlk_mode = CTC_BPE_INTLK_PKT_MODE;
        bpe_global_cfg = &bpe_cfg;
    }

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_bpe_init(lchip, bpe_global_cfg));
    }

    return CTC_E_NONE;
}

int32
ctc_greatbelt_bpe_deinit(uint8 lchip)
{
    uint8 lchip_start       = 0;
    uint8 lchip_end         = 0;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_BPE);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_bpe_deinit(lchip));
    }

    return CTC_E_NONE;
}

/**
 @brief The interface is to enable interlaken function

 @param[in] enable enable Interlaken or not

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_bpe_set_intlk_en(uint8 lchip, bool enable)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_BPE);
    CTC_ERROR_RETURN(sys_greatbelt_bpe_set_intlk_en(lchip, enable));

    return CTC_E_NONE;
}

/**
 @brief The interface is to get enable interlaken function

 @param[out] enable enable Interlaken or not

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_bpe_get_intlk_en(uint8 lchip, bool* enable)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_BPE);
    CTC_ERROR_RETURN(sys_greatbelt_bpe_get_intlk_en(lchip, enable));

    return CTC_E_NONE;
}

/**
 @brief The interface is to set port externder function

 @param[in] gport global port to enable/disable extender function

 @param[in] p_extender port extend information for extender function

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_bpe_set_port_extender(uint8 lchip, uint32 gport, ctc_bpe_extender_t* p_extender)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    int32 ret = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_BPE);
    LCHIP_CHECK(lchip);

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
        {
            CTC_FOREACH_ERROR_RETURN(CTC_E_NONE,
                                     ret,
                                     sys_greatbelt_bpe_set_port_extender(lchip, gport, p_extender));
        }
    }
    else
    {
        SYS_MAP_GPORT_TO_LCHIP(gport, lchip);
        ret = sys_greatbelt_bpe_set_port_extender(lchip, gport, p_extender);
    }

    return ret;
}

/**
 @brief The interface is to get extender enable

 @param[in] gport global port to enable/disable extender function

 @param[out] enable enable or disable

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_bpe_get_port_extender(uint8 lchip, uint32 gport, bool* enable)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_BPE);
    LCHIP_CHECK(lchip);
    SYS_MAP_GPORT_TO_LCHIP(gport, lchip);

    CTC_ERROR_RETURN(sys_greatbelt_bpe_get_port_extender(lchip, gport, enable));

    return CTC_E_NONE;
}

