/**
 @file sys_goldengate_queue_sch.h

 @date 2010-01-13

 @version v2.0

 The file defines macro, data structure, and function for queue scheduling
*/

#ifndef _SYS_GOLDENGATE_QUEUE_SCH_H_
#define _SYS_GOLDENGATE_QUEUE_SCH_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "ctc_qos.h"

/*********************************************************************
 *
 * Macro
 *
 *********************************************************************/
#define SYS_QUEUE_MAX_DRR_WEIGHT        0xFFFFFF

/*********************************************************************
 *
 * Data Structure
 *
 *********************************************************************/

/*********************************************************************
 *
 * Function Declaration
 *
 *********************************************************************/

extern int32
_sys_goldengate_qos_set_sched(uint8 lchip, ctc_qos_sched_t* p_sched);

extern int32
_sys_goldengate_qos_get_sched(uint8 lchip, ctc_qos_sched_t* p_sched);

extern int32
sys_goldengate_queue_sch_get_group_sched(uint8 lchip, uint16 sub_grp,ctc_qos_sched_group_t* p_sched);
extern int32
sys_goldengate_queue_sch_get_queue_sched(uint8 lchip, uint16 queue_id ,ctc_qos_sched_queue_t* p_sched);

extern int32
sys_goldengate_qos_set_sch_wrr_enable(uint8 lchip, uint8 enable);

extern int32
sys_goldengate_queue_sch_set_c2c_group_sched(uint8 lchip, uint8 c2c_group, uint8 class_priority);



/**
 @brief Queue scheduler initialization.
*/
extern int32
sys_goldengate_queue_sch_init(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif

