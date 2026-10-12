/**
 @file sys_goldengate_qos.h

 @date 2010-01-13

 @version v2.0

 The file defines queue api
*/

#ifndef _SYS_GOLDENGATE_QOS_H
#define _SYS_GOLDENGATE_QOS_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_const.h"
#include "ctc_parser.h"
#include "ctc_qos.h"

/*init*/
extern int32
sys_goldengate_qos_init(uint8 lchip, void* p_gbl_parm);

/**
 @brief De-Initialize qos module
*/
extern int32
sys_goldengate_qos_deinit(uint8 lchip);

/*policer*/
extern int32
sys_goldengate_qos_set_policer(uint8 lchip, ctc_qos_policer_t* p_policer);

extern int32
sys_goldengate_qos_get_policer(uint8 lchip, ctc_qos_policer_t* p_policer);

extern int32
sys_goldengate_qos_set_global_config(uint8 lchip, ctc_qos_glb_cfg_t* p_glb_cfg);
extern int32
sys_goldengate_qos_get_global_config(uint8 lchip, ctc_qos_glb_cfg_t* p_glb_cfg);

/*mapping*/
extern int32
sys_goldengate_qos_set_domain_map(uint8 lchip, ctc_qos_domain_map_t* p_domain_map);
extern int32
sys_goldengate_qos_get_domain_map(uint8 lchip, ctc_qos_domain_map_t* p_domain_map);

/*queue*/
extern int32
sys_goldengate_qos_set_queue(uint8 lchip, ctc_qos_queue_cfg_t* p_que_cfg);

/*shape*/
extern int32
sys_goldengate_qos_set_shape(uint8 lchip, ctc_qos_shape_t* p_shape);

/*schedule*/
extern int32
sys_goldengate_qos_set_sched(uint8 lchip, ctc_qos_sched_t* p_sched);

extern int32
sys_goldengate_qos_get_sched(uint8 lchip, ctc_qos_sched_t* p_sched);

/*drop*/
extern int32
sys_goldengate_qos_set_drop_scheme(uint8 lchip, ctc_qos_drop_t* p_drop);
extern int32
sys_goldengate_qos_get_drop_scheme(uint8 lchip, ctc_qos_drop_t* p_drop);

/*Resrc*/
extern int32
sys_goldengate_qos_set_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc);
extern int32
sys_goldengate_qos_get_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc);
extern int32
sys_goldengate_qos_query_pool_stats(uint8 lchip, ctc_qos_resrc_pool_stats_t* p_stats);

/*stats*/
extern int32
sys_goldengate_qos_query_queue_stats(uint8 lchip, ctc_qos_queue_stats_t* p_queue_stats);

extern int32
sys_goldengate_qos_clear_queue_stats(uint8 lchip, ctc_qos_queue_stats_t* p_queue_stats);

extern int32
sys_goldengate_qos_query_policer_stats(uint8 lchip, ctc_qos_policer_stats_t* p_policer_stats);

extern int32
sys_goldengate_qos_clear_policer_stats(uint8 lchip, ctc_qos_policer_stats_t* p_policer_stats);

extern int32
sys_goldengate_qos_set_port_to_statcking_port(uint8 lchip, uint16 gport,uint8 enable);

#ifdef __cplusplus
}
#endif

#endif

