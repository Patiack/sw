/**
 @file sys_goldengate_oam_bfd_db.h

 @date 2013-1-16

 @version v2.0

  The file defines Macro, stored data structure for  Ethernet OAM module
*/
#ifndef _SYS_GOLDENGATE_OAM_BFD_DB_H
#define _SYS_GOLDENGATE_OAM_BFD_DB_H
#ifdef __cplusplus
extern "C" {
#endif
/****************************************************************************
 *
 * Header Files
 *
****************************************************************************/

#include "ctc_oam.h"

#include "sys_goldengate_oam.h"
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
#define CHAN_H ""

int32
_sys_goldengate_ip_bfd_chan_lkup_cmp(uint8 lchip, void* p_cmp);

int32
_sys_goldengate_bfd_add_chan(uint8 lchip, void* p_chan, void** p_sys_chan);

int32
_sys_goldengate_bfd_remove_chan(uint8 lchip, void* p_chan);

int32
_sys_goldengate_bfd_update_chan(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep_bfd, bool is_add);

int32
_sys_goldengate_bfd_chan_update_reomte(uint8 lchip, ctc_oam_lmep_t* p_lmep, sys_oam_chan_tp_t* p_sys_chan_tp, bool is_add);

sys_oam_chan_com_t*
_sys_goldengate_bfd_chan_lkup(uint8 lchip, ctc_oam_key_t* p_key_parm);


#define LMEP_H ""

sys_oam_lmep_bfd_t*
_sys_goldengate_bfd_lmep_lkup(uint8 lchip, sys_oam_chan_com_t* p_sys_chan);

int32
_sys_goldengate_bfd_lmep_lkup_cmp(uint8 lchip, void* p_cmp);

sys_oam_lmep_bfd_t*
_sys_goldengate_bfd_lmep_build_node(uint8 lchip, ctc_oam_lmep_t* p_lmep_param);

int32
_sys_goldengate_bfd_lmep_free_node(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep);

int32
_sys_goldengate_bfd_lmep_build_index(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep);

int32
_sys_goldengate_bfd_lmep_free_index(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep);

int32
_sys_goldengate_bfd_lmep_add_to_db(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep);

int32
_sys_goldengate_bfd_lmep_del_from_db(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep);

int32
_sys_goldengate_bfd_lmep_add_to_asic(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep);

int32
_sys_goldengate_bfd_lmep_del_from_asic(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep);

int32
_sys_goldengate_bfd_lmep_update_asic(uint8 lchip, ctc_oam_update_t* p_lmep_param, sys_oam_lmep_bfd_t* p_sys_lmep);

int32
_sys_goldengate_bfd_update_lmep_lm(uint8 lchip, ctc_oam_update_t* p_lmep_param, sys_oam_lmep_bfd_t* p_sys_lmep);


#define RMEP_H ""

sys_oam_rmep_bfd_t*
_sys_goldengate_bfd_rmep_lkup(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep);

int32
_sys_goldengate_bfd_rmep_lkup_cmp(uint8 lchip, void* p_cmp);

sys_oam_rmep_bfd_t*
_sys_goldengate_bfd_rmep_build_node(uint8 lchip, ctc_oam_rmep_t* p_rmep_param);

int32
_sys_goldengate_bfd_rmep_free_node(uint8 lchip, sys_oam_rmep_bfd_t* p_sys_rmep_bfd);

int32
_sys_goldengate_bfd_rmep_build_index(uint8 lchip, sys_oam_rmep_bfd_t* p_sys_rmep_bfd);

int32
_sys_goldengate_bfd_rmep_free_index(uint8 lchip, sys_oam_rmep_bfd_t* p_sys_rmep_bfd);

int32
_sys_goldengate_bfd_rmep_add_to_db(uint8 lchip, sys_oam_rmep_bfd_t* p_sys_rmep_bfd);

int32
_sys_goldengate_bfd_rmep_del_from_db(uint8 lchip, sys_oam_rmep_bfd_t* p_sys_rmep_bfd);

int32
_sys_goldengate_bfd_rmep_add_to_asic(uint8 lchip, sys_oam_rmep_bfd_t* p_sys_rmep_bfd);

int32
_sys_goldengate_bfd_rmep_del_from_asic(uint8 lchip, sys_oam_rmep_bfd_t* p_sys_rmep_bfd);

int32
_sys_goldengate_bfd_rmep_update_asic(uint8 lchip, ctc_oam_update_t* p_rmep_param, sys_oam_rmep_bfd_t* p_sys_rmep);

#define GLOBAL_H ""

int32
_sys_goldengate_bfd_set_slow_interval(uint8 lchip, uint32 interval);

int32
_sys_goldengate_bfd_set_timer_neg_interval(uint8 lchip, ctc_oam_bfd_timer_neg_t* interval);

#define INIT_H ""

int32
_sys_goldengate_bfd_mpls_register_init(uint8 lchip);

int32
_sys_goldengate_bfd_ip_register_init(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif
