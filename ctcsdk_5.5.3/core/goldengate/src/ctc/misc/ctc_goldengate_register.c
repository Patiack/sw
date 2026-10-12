/**
 @file ctc_goldengate_register.c

 @date 2009-11-6

 @version v2.0

 The file apply APIs to initialize common register of humber
*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_error.h"
#include "ctc_goldengate_register.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_chip.h"

/****************************************************************************
 *
 * Function
 *
 *****************************************************************************/

/**
 @brief  Set global control info

 @param[in] type    a type of global control

 @param[in] value   the value to be set

 @return CTC_E_XXX

*/
int32
ctc_goldengate_global_ctl_set(uint8 lchip, ctc_global_control_type_t type, void* value)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_CHIP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_global_ctl_set(lchip, type, value));
    }

    return CTC_E_NONE;
}

/**
 @brief  Get global control info

 @param[in] type    a type of global control

 @param[out] value   the value to be get

 @return CTC_E_XXX

*/
int32
ctc_goldengate_global_ctl_get(uint8 lchip, ctc_global_control_type_t type, void* value)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_CHIP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_global_ctl_get(lchip, type, value));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_register_init(uint8 lchip, void* p_global_cfg)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_CHIP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_register_init(lchip));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_register_deinit(uint8 lchip)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_CHIP);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_register_deinit(lchip));
    }

    return CTC_E_NONE;
}

