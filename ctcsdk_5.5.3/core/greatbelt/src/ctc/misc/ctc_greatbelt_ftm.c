/**
 @file ctc_greatbelt_ftm.c

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2009-11-11

 @version v2.0

 memory alloc init
*/

#include "sal.h"
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_alloc.h"
#include "ctc_debug.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_chip.h"

/**
 @brief

 @param[in] ctc_profile_info  allocation profile information

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_ftm_mem_alloc(uint8 lchip, ctc_ftm_profile_info_t* ctc_profile_info)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_ftm_mem_alloc(lchip, ctc_profile_info));
    }

    return CTC_E_NONE;
}

int32
ctc_greatbelt_ftm_mem_free(uint8 lchip)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_ftm_mem_free(lchip));
    }

    return CTC_E_NONE;
}

