/**
 @file sys_goldengate_qos_domain.h

 @date 2009-11-30

 @version v2.0

 The file defines macro, data structure, and function for qos classification component
*/

#ifndef _SYS_GOLDENGATE_QOS_DOMAIN_H_
#define _SYS_GOLDENGATE_QOS_DOMAIN_H_
#ifdef __cplusplus
extern "C" {
#endif

#if 0
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
sys_goldengate_qos_domain_set_map(uint8 lchip, ctc_qos_domain_map_t* p_domain_map);

extern int32
sys_goldengate_qos_domain_get_map(uint8 lchip, ctc_qos_domain_map_t* p_domain_map);

/**
 @brief init qos domain mapping tables
*/
extern int32
sys_goldengate_qos_domain_init(uint8 lchip);

#endif

#ifdef __cplusplus
}
#endif

#endif

