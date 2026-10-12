/**
 @file sys_greatbelt_qos_policer.h

 @date 2009-11-30

 @version v2.0

 The file defines macro, data structure, and function for qos policer component
*/

#ifndef _SYS_GREATBELT_QOS_POLICER_H_
#define _SYS_GREATBELT_QOS_POLICER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "ctc_qos.h"
/*********************************************************************
  *
  * macro definition
  *
  *********************************************************************/
#define SYS_QOS_POLICER_FLOW_ID_DROP        0xFFFF
#define SYS_QOS_POLICER_FLOW_ID_RATE_MAX    0xFFFE

#define SYS_QOS_POLICER_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip);\
        if (NULL == p_gb_qos_policer_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

/*********************************************************************
  * function declaration
  *********************************************************************/
extern int32
sys_greatbelt_qos_policer_set(uint8 lchip, ctc_qos_policer_t* p_policer);

extern int32
sys_greatbelt_qos_policer_get(uint8 lchip, ctc_qos_policer_t* p_policer);

extern int32
sys_greatbelt_qos_policer_index_get(uint8 lchip,
                                    ctc_qos_policer_type_t type,
                                    uint16 plc_id,
                                    uint16* p_index);
extern int32
sys_greatbelt_qos_policer_reserv(uint8 lchip);

extern int32
sys_greatbelt_qos_set_policer_update_enable(uint8 lchip, uint8 enable);

extern int32
sys_greatbelt_qos_set_phb(uint8 lchip, ctc_qos_phb_map_t *p_phb_map);

extern int32
sys_greatbelt_qos_set_policer_flow_first(uint8 lchip, uint8 dir, uint8 enable);

extern int32
sys_greatbelt_qos_set_policer_sequential_enable(uint8 lchip, uint8 enable);

extern int32
sys_greatbelt_qos_set_policer_ipg_enable(uint8 lchip, uint8 enable);

extern int32
sys_greatbelt_qos_set_policer_hbwp_share_enable(uint8 lchip, uint8 enable);

extern int32
sys_greatbelt_qos_set_policer_stats_enable(uint8 lchip, uint8 enable);

extern int32
sys_greatbelt_qos_policer_stats_query(uint8 lchip, ctc_qos_policer_stats_t* p_stats);

extern int32
sys_greatbelt_qos_policer_stats_clear(uint8 lchip, ctc_qos_policer_stats_t* p_stats);

/**
 @brief qos policer component initialization
*/
extern int32
sys_greatbelt_qos_policer_init(uint8 lchip);

extern int32
sys_greatbelt_qos_policer_deinit(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif

