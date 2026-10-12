/**
 @file sys_greatbelt_oam_cfm.h

 @date 2010-1-19

 @version v2.0

  The file defines Macro, stored data structure for  Ethernet OAM module
*/
#ifndef _SYS_GREATBELT_OAM_CFM_H
#define _SYS_GREATBELT_OAM_CFM_H
#ifdef __cplusplus
extern "C" {
#endif
/****************************************************************************
 *
 * Header Files
 *
****************************************************************************/
#include "sys_greatbelt_oam_db.h"
/****************************************************************************
*
* Defines and Macros
*
****************************************************************************/

/****************************************************************************
*
* Global and Declaration
*
*****************************************************************************/

/****************************************************************************
 *
* Function
*
****************************************************************************/

int32
sys_greatbelt_cfm_init(uint8 lchip, ctc_oam_global_t* p_eth_glb);

int32
_sys_greatbelt_cfm_get_property(uint8 lchip, void* p_prop);

#ifdef __cplusplus
}
#endif

#endif

