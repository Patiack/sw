/**
 @file ctc_goldengate_interrupt.c

 @author  Copyright (C) 2012 Centec Networks Inc.  All rights reserved.

 @date 2012-10-23

 @version v2.0

 This file define ctc functions

*/

/****************************************************************************
*
* Header Files
*
****************************************************************************/
#include "ctc_error.h"
#include "ctc_goldengate_interrupt.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_interrupt.h"

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
int32
ctc_goldengate_interrupt_init(uint8 lchip, void* p_global_cfg)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_INTERRUPT);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_interrupt_init(lchip, p_global_cfg));
    }

    return CTC_E_NONE;
}


int32
ctc_goldengate_interrupt_deinit(uint8 lchip)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_INTERRUPT);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_interrupt_deinit(lchip));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_interrupt_clear_status(uint8 lchip, ctc_intr_type_t* p_type)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_INTERRUPT);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_interrupt_clear_status(lchip, p_type, TRUE));

    return CTC_E_NONE;
}

int32
ctc_goldengate_interrupt_get_status(uint8 lchip, ctc_intr_type_t* p_type, ctc_intr_status_t* p_status)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_INTERRUPT);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_interrupt_get_status(lchip, p_type, p_status));

    return CTC_E_NONE;
}

int32
ctc_goldengate_interrupt_set_en(uint8 lchip, ctc_intr_type_t* p_type, uint32 enable)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_INTERRUPT);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_interrupt_set_en(lchip, p_type, enable));

    return CTC_E_NONE;
}

int32
ctc_goldengate_interrupt_get_en(uint8 lchip, ctc_intr_type_t* p_type, uint32* p_enable)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_INTERRUPT);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_interrupt_get_en(lchip, p_type, p_enable));

    return CTC_E_NONE;
}

int32
ctc_goldengate_interrupt_register_event_cb(uint8 lchip, ctc_interrupt_event_t event, CTC_INTERRUPT_EVENT_FUNC cb)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_INTERRUPT);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_interrupt_register_event_cb(lchip, event, cb));
    }

    return CTC_E_NONE;
}

