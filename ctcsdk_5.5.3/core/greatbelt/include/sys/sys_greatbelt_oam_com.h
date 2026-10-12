/**
 @file sys_greatbelt_oam_com.h

 @date 2010-1-19

 @version v2.0

  The file defines Macro, stored data structure for  Ethernet OAM module
*/
#ifndef _SYS_GREATBELT_OAM_COM_H
#define _SYS_GREATBELT_OAM_COM_H
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
_sys_greatbelt_oam_com_add_maid(uint8 lchip, void* p_maid);

int32
_sys_greatbelt_oam_com_remove_maid(uint8 lchip, void* p_maid);


int32
_sys_greatbelt_tp_add_chan(uint8 lchip, void* p_chan, void** p_sys_chan);

int32
_sys_greatbelt_tp_remove_chan(uint8 lchip, void* p_chan);

sys_oam_chan_tp_t*
_sys_greatbelt_tp_chan_lkup(uint8 lchip, ctc_oam_key_t* p_key_parm);

int32
_sys_greatbelt_tp_chan_lkup_cmp(uint8 lchip, void* p_cmp);


#ifdef __cplusplus
}
#endif

#endif

