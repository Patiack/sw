/**
 @file ctc_greatbelt_qos.h

 @date 2012-09-03

 @version v2.0

    Differentiated Service (DiffServ) is an Internet Engineering Task Force (IETF) standard to
    bring scalable, quantifiable Quality of Service (QoS) by reusing the IP header's Type of Service (ToS) field.
    DiffServ enhancements are intended to enable QoS levels without the need for per-flow state and signaling at
    every router hop. A variety of network services can be offered by changing the IP header's DiffServ
    CodePoint (DSCP) field. An example of such network services is conditioning the marked packets at
    the network boundaries according to predetermined service agreements.
\p
    Greatbelt support traffic classification, policing, and conditioning.
    Thus, different levels of service can be provided within the network in support of the DiffServ scheme.

\p
   GreatBelt supports hierarchical shaping and scheduling.
\p
   The three level traffic regulation and scheduling features:
\p
    1) CoS Level
\p
\d	Up to 1K queues
\d	4/8/16 queues per group depend on group bonding
\d	WRED
\d	Shaper
\p
    2) Group Level
\p
\d	Up to 256 groups (subscribers)
\d	4/8 queues per group basically£¬ each queue can be configured to select an arbitrary commit shaper or excess shaper.
    All queues should pass through the PIR shaper.
\d	SP + WFQ scheduling,8 WFQ schedulers in a group and  8 WFQ schedulers are chained into a single SP scheduler
    that WFQ7 the highest, and WFQ0 the lowest priority.
\d	Group shapers mark traffic as green (confirm), yellow (excess), and red (violate) for each queue.
    The green/yellow/red traffic priority are independently mapped to 0-8,
    where 8 means pause, 0-7 indicates WFQ0 - WFQ7. Flexibly mapping of any group to any physical port.
    A single physical port may comprise of multiple groups,	8 group priorities propagation to 4 port priorities.
\d	ETS support: three traffic classes (IPC, SAN, Ethernet)
\p
   3) Physical Port Level
\p
\d Up to 62 physical ports
\d Configurable WFQ weight between different groups in the same priority
\d SP + WFQ scheduling, 4 WFQ schedulers in a physical port, 4 WFQ schedulers
   are chained into a single SP scheduler that WFQ3 the highest, and WFQ0 the lowest priority.

\p
   Policer max bucket size is based on the policer rate as following:
 \p
 \t  |   rate(Mbps)  | max bucket size(Kb) |
 \t  |---------------|---------------------|
 \t  |  rate <= 2    |        4193         |
 \t  |---------------|---------------------|
 \t  |  rate <= 100  |        16773        |
 \t  |---------------|---------------------|
 \t  |  rate <= 1000 |        33546        |
 \t  |---------------|---------------------|
 \t  |  rate <= 2000 |        67092        |
 \t  |---------------|---------------------|
 \t  |  rate <= 4000 |        134184       |
 \t  |---------------|---------------------|
 \t  |  rate <= 10000|        268369       |
 \t  |---------------|---------------------|

*/

#ifndef _CTC_GREATBELT_QOS_H
#define _CTC_GREATBELT_QOS_H
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

/**
 @addtogroup qos QOS
 @{
*/

/**
 @brief     Init qos module.

 @param[in] lchip    local chip id

 @param[in] p_glb_parm      Init parameter

 @remark    Init qos module.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_init(uint8 lchip, void* p_glb_parm);

/**
 @brief De-Initialize qos module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the qos configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_deinit(uint8 lchip);

/**
 @brief     set qos global configure.

 @param[in] lchip    local chip id

 @param[in] p_glb_cfg    point to  global configure

 @remark    Qos global configure such as resource manger, policer, shaping global enable

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_set_global_config(uint8 lchip, ctc_qos_glb_cfg_t* p_glb_cfg);

/**
 @brief     get qos global configure.

 @param[in] lchip    local chip id

 @param[in] p_glb_cfg    point to  global configure

 @remark    get global configure such as resource manger, policer, shaping global enable

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_get_global_config(uint8 lchip, ctc_qos_glb_cfg_t* p_glb_cfg);

/**
 @brief     set qos domain mapping

 @param[in] lchip    local chip id

 @param[in] p_domain_map    point to  domain mapping

 @remark   include ingress mapping and egress mapping

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_set_domain_map(uint8 lchip, ctc_qos_domain_map_t* p_domain_map);

/**
 @brief     get qos domain mapping

 @param[in] lchip    local chip id

 @param[in] p_domain_map    point to  domain mapping

 @remark   include ingress mapping and egress mapping

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_get_domain_map(uint8 lchip, ctc_qos_domain_map_t* p_domain_map);

/**
 @brief     set qos policer

 @param[in] lchip    local chip id

 @param[in] p_policer    point to  policer

 @remark   include port, flow and hbwp policer, hbwp policer no suppport now.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_set_policer(uint8 lchip, ctc_qos_policer_t* p_policer);

/**
 @brief     get qos policer

 @param[in] lchip    local chip id

 @param[in] p_policer    point to  policer

 @remark   include port, flow and hbwp policer confiure

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_get_policer(uint8 lchip, ctc_qos_policer_t* p_policer);

/**
 @brief     set qos queue configure

 @param[in] lchip    local chip id

 @param[in] p_que_cfg    point to  queue configure

 @remark   include queue number, service id, priority mapping

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_set_queue(uint8 lchip, ctc_qos_queue_cfg_t* p_que_cfg);

/**
 @brief     get qos queue configure

 @param[in] lchip    local chip id

 @param[in] p_que_cfg    point to  queue configure

 @remark   include queue number, service id, priority mapping

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_get_queue(uint8 lchip, ctc_qos_queue_cfg_t* p_que_cfg);

/**
 @brief     set qos shaping

 @param[in] lchip    local chip id

 @param[in] p_shape    point to  shaping configure

 @remark   include port, queue, group shaping configure

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_set_shape(uint8 lchip, ctc_qos_shape_t* p_shape);

/**
 @brief     get qos shaping

 @param[in] lchip    local chip id

 @param[in] p_shape    point to  shaping configure

 @remark   include port, queue, group shaping

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_get_shape(uint8 lchip, ctc_qos_shape_t* p_shape);

/**
 @brief     set qos schedule

 @param[in] lchip    local chip id

 @param[in] p_sched    point to  schedule configure

 @remark   include queue , group, sub-group level schedule

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_set_sched(uint8 lchip, ctc_qos_sched_t* p_sched);

/**
 @brief     get qos schedule

 @param[in] lchip    local chip id

 @param[in] p_sched    point to  schedule configure

 @remark   include queue , group, sub-group level schedule

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_get_sched(uint8 lchip, ctc_qos_sched_t* p_sched);

/**
 @brief     set qos drop scheme

 @param[in] lchip    local chip id

 @param[in] p_drop    point to  drop configure

 @remark   include WTD,WRED

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_set_drop_scheme(uint8 lchip, ctc_qos_drop_t* p_drop);

/**
 @brief     get qos drop scheme

 @param[in] lchip    local chip id

 @param[in] p_drop    point to  drop configure

 @remark   include WTD,WRED

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_get_drop_scheme(uint8 lchip, ctc_qos_drop_t* p_drop);

/**
 @brief     set qos resource management

 @param[in] lchip    local chip id

 @param[in] p_resrc    point to  resource configure

 @remark  Configure  resource management parameters

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_set_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc);

/**
 @brief     get qos resource management

 @param[in] lchip    local chip id

 @param[in] p_resrc    point to  resource configure

 @remark  Get  resource management parameters

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_get_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc);

/**
 @brief     query resource pool stats

 @param[in] lchip    local chip id

 @param[in|out] p_stats    point to pool stats

 @remark  Get resource pool stats

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_query_pool_stats(uint8 lchip, ctc_qos_resrc_pool_stats_t* p_stats);

/**
 @brief     query queue stats

 @param[in] lchip    local chip id

 @param[in|out] p_queue_stats    point to  queue stats

 @remark   get queue stats include dequeue and drop stats

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_query_queue_stats(uint8 lchip, ctc_qos_queue_stats_t* p_queue_stats);

/**
 @brief     clear queue stats

 @param[in] lchip    local chip id

 @param[in] p_queue_stats    point to  queue stats

 @remark   clear the queue stats and reset to zero

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_clear_queue_stats(uint8 lchip, ctc_qos_queue_stats_t* p_queue_stats);

/**
 @brief     query policer stats

 @param[in] lchip    local chip id

 @param[in|out] p_policer_stats    point to  policer stats

 @remark   get stats of green,yellow,red color stats

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_query_policer_stats(uint8 lchip, ctc_qos_policer_stats_t* p_policer_stats);

/**
 @brief     clear policer stats

 @param[in] lchip    local chip id

 @param[in] p_policer_stats    point to  policer stats

 @remark   reset the policers to zero

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_qos_clear_policer_stats(uint8 lchip, ctc_qos_policer_stats_t* p_policer_stats);

/**@} end of @addtogroup qos QOS*/

#ifdef __cplusplus
}
#endif

#endif

