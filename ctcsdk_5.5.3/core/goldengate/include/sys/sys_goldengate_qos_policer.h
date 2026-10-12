/**
 @file sys_goldengate_qos_policer.h

 @date 2009-11-30

 @version v2.0

 The file defines macro, data structure, and function for qos policer component
*/

#ifndef _SYS_GOLDENGATE_QOS_POLICER_H_
#define _SYS_GOLDENGATE_QOS_POLICER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "ctc_qos.h"
/*********************************************************************
  *
  * macro definition
  *
  *********************************************************************/

/*********************************************************************
  * function declaration
  *********************************************************************/
extern int32
sys_goldengate_qos_policer_set_gran_mode(uint8 lchip, uint8 mode);

extern int32
sys_goldengate_qos_policer_set(uint8 lchip, ctc_qos_policer_t* p_policer);

extern int32
sys_goldengate_qos_policer_get(uint8 lchip, ctc_qos_policer_t* p_policer);

extern int32
sys_goldengate_qos_policer_index_get(uint8 lchip, uint16 plc_id,uint16* p_policer_ptr,
                                    uint8 *is_bwp,uint8 *triple_play);

extern int32
sys_goldengate_qos_set_policer_update_enable(uint8 lchip, uint8 enable);

extern int32
sys_goldengate_qos_set_phb(uint8 lchip, ctc_qos_phb_map_t *p_phb_map);

extern int32
sys_goldengate_qos_set_policer_flow_first(uint8 lchip, uint8 dir, uint8 enable);

extern int32
sys_goldengate_qos_set_policer_sequential_enable(uint8 lchip, uint8 enable);

extern int32
sys_goldengate_qos_set_policer_ipg_enable(uint8 lchip, uint8 enable);

extern int32
sys_goldengate_qos_set_policer_mark_ecn_enable(uint8 lchip, uint8 enable);

extern int32
sys_goldengate_qos_set_policer_hbwp_share_enable(uint8 lchip, uint8 enable);

extern int32
sys_goldengate_qos_set_policer_stats_enable(uint8 lchip, uint8 enable);

extern int32
sys_goldengate_qos_policer_stats_query(uint8 lchip, ctc_qos_policer_stats_t* p_stats);

extern int32
sys_goldengate_qos_policer_stats_clear(uint8 lchip, ctc_qos_policer_stats_t* p_stats);

/**
 @brief qos policer component initialization
*/
extern int32
sys_goldengate_qos_policer_init(uint8 lchip, ctc_qos_global_cfg_t *p_glb_parm);

extern int32
sys_goldengate_qos_policer_deinit(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif

