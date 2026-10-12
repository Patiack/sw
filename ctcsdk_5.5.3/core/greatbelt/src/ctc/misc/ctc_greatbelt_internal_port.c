/**
 @file ctc_greatbelt_internal_port.c

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2010-03-29

 @version v2.0

   The file apply APIs to initialize,allocation,release internal port.
*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_error.h"

#include "ctc_greatbelt_internal_port.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_internal_port.h"

/****************************************************************************
 *
 * Function
 *
 *****************************************************************************/

/**
 @brief     Set internal port for special usage

 @param[in] type  Internal port usage type
 @param[in] gchip  Global chip of the system
 @param[in] inter_port   Internal port (range128~255 in service queue mode and range56~255 in basic queue mode)

 @return    CTC_E_XXX

*/
int32
ctc_greatbelt_set_internal_port(uint8 lchip, ctc_internal_port_assign_para_t* port_assign)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PORT);
    CTC_PTR_VALID_CHECK(port_assign);
    SYS_MAP_GCHIP_TO_LCHIP(port_assign->gchip, lchip)
    CTC_ERROR_RETURN(sys_greatbelt_internal_port_set(lchip, port_assign));

    return CTC_E_NONE;
}

/**
 @brief     Allocate internal port for special usage

 @param[in] type  Internal port usage type
 @param[in] gchip  Global chip of the system
 @param[out] p_inter_port   Internal port (range128~255 in service queue mode and range56~255 in basic queue mode)

 @return    CTC_E_XXX

*/
int32
ctc_greatbelt_alloc_internal_port(uint8 lchip, ctc_internal_port_assign_para_t* port_assign)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PORT);
    CTC_PTR_VALID_CHECK(port_assign);

    SYS_MAP_GCHIP_TO_LCHIP(port_assign->gchip, lchip)
    CTC_ERROR_RETURN(sys_greatbelt_internal_port_allocate(lchip, port_assign));

    return CTC_E_NONE;
}

/**
 @brief     release internal port.

 @param[in] type  Internal port usage type
 @param[in] gchip  Global chip of the system
 @param[in] inter_port   Internal port

 @return    CTC_E_XXX

*/
int32
ctc_greatbelt_free_internal_port(uint8 lchip, ctc_internal_port_assign_para_t* port_assign)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PORT);
    CTC_PTR_VALID_CHECK(port_assign);
    SYS_MAP_GCHIP_TO_LCHIP(port_assign->gchip, lchip)
    CTC_ERROR_RETURN(sys_greatbelt_internal_port_release(lchip, port_assign));

    return CTC_E_NONE;
}

/**
 @brief     Initialize internal port.

 @return    CTC_E_XXX

*/
int32
ctc_greatbelt_internal_port_init(uint8 lchip, void* p_global_cfg)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PORT);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_internal_port_init(lchip));
    }

    return CTC_E_NONE;
}

int32
ctc_greatbelt_internal_port_deinit(uint8 lchip)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PORT);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_internal_port_deinit(lchip));
    }

    return CTC_E_NONE;
}

