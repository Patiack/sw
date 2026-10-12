/**
 @file sys_greatbelt_queue_drop.h

 @date 2010-01-13

 @version v2.0

 The file defines macro, data structure, and function for queue dropping
*/

#ifndef _SYS_GREATBELT_QUEUE_DROP_H_
#define _SYS_GREATBELT_QUEUE_DROP_H_
#ifdef __cplusplus
extern "C" {
#endif


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
#define SYS_MAX_TAIL_DROP_PROFILE 7
#define SYS_DEFAULT_GRP_DROP_PROFILE  0


enum sys_queue_drop_type_e
{
    SYS_QUEUE_DROP_TYPE_PROFILE = 0x1,    /**< Only use profile to drop */
    SYS_QUEUE_DROP_TYPE_CHANNEL = 0x2,    /**< Only use channel to drop */
    SYS_QUEUE_DROP_TYPE_ALL     = 0x3     /**< Use both channel and profile to drop*/
};
typedef enum sys_queue_drop_type_e sys_queue_drop_type_t;

/*********************************************************************
 *
 * Function Declaration
 *
 *********************************************************************/

/**
 @brief Get default queue drop.
*/
extern int32
sys_greatbelt_queue_set_default_drop(uint8 lchip,
                                     uint16 queue_id,
                                     uint8 profile_id);

extern int32
sys_greatbelt_queue_set_drop(uint8 lchip, ctc_qos_drop_t* p_drop);

extern int32
sys_greatbelt_queue_get_drop(uint8 lchip, ctc_qos_drop_t* p_drop);

extern int32
sys_greatbelt_queue_set_queue_resrc(uint8 lchip, uint16 queue_id, uint8 pool);

extern int32
sys_greatbelt_queue_set_cpu_queue_egs_pool_classify(uint8 lchip, uint16 reason_id, uint8 pool);

extern int32
sys_greatbelt_queue_set_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc);

extern int32
sys_greatbelt_queue_get_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc);

extern int32
sys_greatbelt_queue_query_pool_stats(uint8 lchip, ctc_qos_resrc_pool_stats_t* p_stats);

extern int32
sys_greatbelt_queue_get_port_depth(uint8 lchip, uint16 gport, uint32* p_depth);

extern int32
sys_greatbelt_queue_set_port_drop_en(uint8 lchip, uint16 gport, bool enable, uint32 drop_type);
/**
 @brief QoS queue drop component initialization.
*/
extern int32
sys_greatbelt_queue_drop_init(uint8 lchip, void *p_glb_parm);

#ifdef __cplusplus
}
#endif

#endif

