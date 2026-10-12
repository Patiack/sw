/**
 @file ctc_greatbelt_register.h

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2010-1-7

 @version v2.0

Initialize driver, and write default value to common table/register

*/

#ifndef _CTC_GREATBELT_REGISTER
#define _CTC_GREATBELT_REGISTER
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_register.h"

/**
 @brief  Set global control info

 @param[in] lchip    local chip id

 @param[in] type    a type of global control

 @param[in] value   the value to be set

 @remark   Set global control info.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_global_ctl_set(uint8 lchip, ctc_global_control_type_t type, void* value);

/**
 @brief  Get global control info

 @param[in] lchip    local chip id

 @param[in] type    a type of global control

 @param[out] value   the value to be get

 @remark    Get global control info

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_global_ctl_get(uint8 lchip, ctc_global_control_type_t type, void* value);

/**
 @brief    Initialize driver ,and  write default value to common table/register

 @param[in] lchip    local chip id

 @param[in] p_global_cfg    point to global configure

 @remark    N/A

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_register_init(uint8 lchip, void* p_global_cfg);

/**
 @brief De-Initialize register module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the register configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_register_deinit(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif

