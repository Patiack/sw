/**
 @file sys_goldengate_queue_drop.h

 @date 2010-01-13

 @version v2.0

 The file defines macro, data structure, and function for queue dropping
*/

#ifndef _SYS_GOLDENGATE_QUEUE_DROP_H_
#define _SYS_GOLDENGATE_QUEUE_DROP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "ctc_queue.h"

/*********************************************************************
 *
 * Macro
 *
 *********************************************************************/

#define SYS_DROP_COUNT_GE       0x38
#define SYS_DROP_DELTA_GE       0x4
#define SYS_DROP_COUNT_XGE      0x40
#define SYS_DROP_DELTA_XGE      0x10

/*********************************************************************
 *
 * Data Structure
 *
 *********************************************************************/

/**
 @brief Default queue drop profiles, these default profiles will persistently exist and won't be remove.
*/

#define SYS_DEFAULT_TAIL_DROP_PROFILE 0
#define SYS_DEFAULT_GRP_DROP_PROFILE  0

/*********************************************************************
 *
 * Function Declaration
 *
 *********************************************************************/

/**
 @brief Get default queue drop.
*/
extern int32
sys_goldengate_queue_set_default_drop(uint8 lchip,
                                     uint16 queue_id,
                                     uint8 profile_id);

extern int32
sys_goldengate_queue_set_drop(uint8 lchip, ctc_qos_drop_t* p_drop);

extern int32
sys_goldengate_queue_get_drop(uint8 lchip, ctc_qos_drop_t* p_drop);

extern int32
sys_goldengate_queue_set_cpu_queue_egs_pool_classify(uint8 lchip, uint16 reason_id, uint8 pool);

extern int32
sys_goldengate_queue_set_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc);
extern int32
sys_goldengate_queue_get_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc);

extern int32
sys_goldengate_queue_query_pool_stats(uint8 lchip, ctc_qos_resrc_pool_stats_t* p_stats);

extern int32
sys_goldengate_qos_resrc_mgr_en(uint8 lchip, uint8 enable);

/**
 @brief QoS queue drop component initialization.
*/
extern int32
sys_goldengate_queue_drop_init(uint8 lchip, void *p_glb_parm);

#ifdef __cplusplus
}
#endif

#endif

