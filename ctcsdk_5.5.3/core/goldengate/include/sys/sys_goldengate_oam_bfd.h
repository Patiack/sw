/**
 @file sys_goldengate_oam_bfd.h

 @date 2013-01-16

 @version v2.0

  The file defines Macro, stored data structure for BFD OAM module
*/
#ifndef _SYS_GOLDENGATE_OAM_BFD_H
#define _SYS_GOLDENGATE_OAM_BFD_H
#ifdef __cplusplus
extern "C" {
#endif
/****************************************************************************
 *
 * Header Files
 *
****************************************************************************/
#include "sys_goldengate_oam_db.h"
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
sys_goldengate_oam_tp_bfd_init(uint8 lchip, ctc_oam_global_t* p_oam_glb);

int32
sys_goldengate_oam_ip_bfd_init(uint8 lchip, ctc_oam_global_t* p_oam_glb);

int32
sys_goldengate_oam_mpls_bfd_init(uint8 lchip, ctc_oam_global_t* p_oam_glb);

#ifdef __cplusplus
}
#endif

#endif
