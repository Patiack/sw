/**
 @file sys_greatbelt_queue_shape.h

 @date 2010-01-13

 @version v2.0

 The file defines macro, data structure, and function for queue shaping.
*/

#ifndef _SYS_GREATBELT_QUEUE_SHAPE_H_
#define _SYS_GREATBELT_QUEUE_SHAPE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "ctc_qos.h"
/*********************************************************************
 *
 * Macro
 *
 *********************************************************************/

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
_sys_greatbelt_qos_set_shape(uint8 lchip, ctc_qos_shape_t* p_shape);

extern int32
_sys_greatbelt_qos_get_shape(uint8 lchip, ctc_qos_shape_t* p_shape);

extern int32
sys_greatbelt_qos_set_port_shape_enable(uint8 lchip, uint8 enable);

extern int32
sys_greatbelt_qos_set_queue_shape_enable(uint8 lchip, uint8 enable);

extern int32
sys_greatbelt_qos_set_group_shape_enable(uint8 lchip, uint8 enable);

extern int32
sys_greatbelt_qos_set_shape_ipg_enable(uint8 lchip, uint8 enable);

extern int32
sys_greatbelt_qos_set_reason_shp_base_pkt_en(uint8 lchip, uint8 enable);

int32
sys_greatbelt_queue_set_queue_shape(uint8 lchip, ctc_qos_shape_queue_t* p_shape);
/**
 @brief Queue shaper initialization.
*/
extern int32
sys_greatbelt_queue_shape_init(uint8 lchip, void* p_glb_parm);

#ifdef __cplusplus
}
#endif

#endif

