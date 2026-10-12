/**
 @file sys_greatbelt_qos_class.h

 @date 2009-11-30

 @version v2.0

 The file defines macro, data structure, and function for qos classification component
*/

#ifndef _SYS_GREATBELT_QOS_CLASS_H_
#define _SYS_GREATBELT_QOS_CLASS_H_
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
  *
  * data structure definition
  *
  *********************************************************************/

extern int32
sys_greatbelt_qos_domain_map_set(uint8 lchip, ctc_qos_domain_map_t* p_domain_map);

extern int32
sys_greatbelt_qos_domain_map_get(uint8 lchip, ctc_qos_domain_map_t* p_domain_map);

/**
 @brief init qos classification mapping tables
*/
extern int32
sys_greatbelt_qos_class_init(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif

