/**
 @file sys_greatbelt_queue_enq.h

 @date 2010-01-13

 @version v2.0

 The file defines macro, data structure, and function for en-queue
*/

#ifndef _SYS_GREATBELT_QUEUE_ENQ_H_
#define _SYS_GREATBELT_QUEUE_ENQ_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "ctc_debug.h"
#include "ctc_const.h"
#include "ctc_stats.h"

#include "ctc_vector.h"
#include "ctc_spool.h"
#include "ctc_hash.h"
#include "ctc_qos.h"
#include "ctc_packet.h"
#include "greatbelt/include/drv_lib.h"

#include "sys_greatbelt_common.h"

/*********************************************************************
 *
 * Macro
 *
 *********************************************************************/
#define SYS_QUEUE_INIT_CHECK(lchip)         \
    do {                                    \
        SYS_LCHIP_CHECK_ACTIVE(lchip);           \
        if (NULL == p_gb_queue_master[lchip]){ \
            return CTC_E_NOT_INIT; }        \
    } while (0)

#define SYS_QUEUE_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(qos, queue, QOS_QUE_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0);

#define SYS_QUEUE_DBG_FUNC()           SYS_QUEUE_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__)
#define SYS_QUEUE_DBG_INFO(FMT, ...)  SYS_QUEUE_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ##__VA_ARGS__)
#define SYS_QUEUE_DBG_ERROR(FMT, ...) SYS_QUEUE_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, FMT, ##__VA_ARGS__)
#define SYS_QUEUE_DBG_PARAM(FMT, ...) SYS_QUEUE_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ##__VA_ARGS__)
#define SYS_QUEUE_DBG_DUMP(FMT, ...)  SYS_QUEUE_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__)

#define SYS_MAX_QUEUE_NUM               1024
#define SYS_MAX_QUEUE_GROUP_NUM         256
#define SYS_MAX_QUEUE_ENTRY_NUM         12288

#define SYS_NETWORK_PORT_NUM    (SYS_MAX_GMAC_CHANNEL_NUM + SYS_MAX_XGMAC_CHANNEL_NUM)

/*at lease one packet's mtu*/
#define SYS_WFQ_DEFAULT_WEIGHT    800
#define SYS_WFQ_DEFAULT_SHIFT    2

#define SYS_SHAPE_BUCKET_CIR_PASS0    4
#define SYS_SHAPE_BUCKET_CIR_PASS1    16
#define SYS_SHAPE_BUCKET_PIR_PASS0    7
#define SYS_SHAPE_BUCKET_PIR_PASS1    15
#define SYS_SHAPE_BUCKET_CIR_FAIL0    7
#define SYS_SHAPE_BUCKET_CIR_FAIL1    0x1F

#define SYS_RESRC_IGS_MAX_PORT_MIN_PROFILE   8
#define SYS_RESRC_PROFILE_QUEUE 0
#define SYS_RESRC_PROFILE_FC 1
#define SYS_RESRC_PROFILE_PFC 2
#define SYS_RESRC_PROFILE_MAX 3

#define SYS_SHAPE_MAX_TOKEN_THRD_BPS ((0xFF << 0xF)*8/1000)
#define SYS_SHAPE_MAX_TOKEN_THRD_PPS ((0xFF << 0xF)/1000)

#define SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd) {\
   if (is_pps)\
   {\
      token_thrd = (token_thrd > SYS_SHAPE_MAX_TOKEN_THRD_PPS) ? SYS_SHAPE_MAX_TOKEN_THRD_PPS: token_thrd; /*pps*/ \
   }\
   else\
   {\
      token_thrd = (token_thrd > SYS_SHAPE_MAX_TOKEN_THRD_BPS) ? SYS_SHAPE_MAX_TOKEN_THRD_BPS: token_thrd; /*kb*/ \
   }\
}
#define SYS_QOS_CLASS_PRIORITY_MAX (p_gb_queue_master[lchip]->priority_mode ? 15 : 63)
enum sys_queue_type_e
{
    SYS_QUEUE_TYPE_NORMAL,     /**< normal en queue */
    SYS_QUEUE_TYPE_STACKING,   /**< stacking en queue */
    SYS_QUEUE_TYPE_EXCP,   /**< stacking en queue */
    SYS_QUEUE_TYPE_SERVICE,   /**< service en queue */

    SYS_QUEUE_TYPE_INTERNAL,
    SYS_QUEUE_TYPE_CPU_TX,
    SYS_QUEUE_TYPE_C2C,

    SYS_QUEUE_TYPE_MAX
};
typedef enum sys_queue_type_e sys_queue_type_t;


enum sys_shape_type_e
{
    SYS_SHAPE_TYPE_PORT,
    SYS_SHAPE_TYPE_QUEUE,
    SYS_SHAPE_TYPE_MAX
};
typedef enum sys_shape_type_e sys_shape_type_t;


#define SYS_QOS_SHAPE_TYPE_CHAN 0
#define SYS_QOS_SHAPE_TYPE_QUEUE 1
#define SYS_QOS_SHAPE_TYPE_MAX 2


/**
@brief igr resr Misc configurations.
*/
struct sys_qos_fc_prop_s
{
    uint16 gport;
    uint8  cos;
    uint8  is_pfc;
    uint8  enable;
};
typedef struct sys_qos_fc_prop_s sys_qos_fc_prop_t;

/**
 @brief En-queue configurations.
*/
struct sys_queue_num_ctl_s
{
    uint8 queue_num;
};
typedef struct sys_queue_num_ctl_s sys_queue_num_ctl_t;

struct sys_queue_info_s
{
    uint8 queue_type;
    uint8 queue_num;
    uint16 service_id;

    uint8 offset;
    uint8 channel;
    uint16 dest_queue;

    uint16 queue_base;
    uint16 group_id;

    uint8 grp_bonding_en;
    uint16 bond_group;
    uint8  ref_cnt;                 /*for service queue*/

    uint8 stacking_grp_en;  /*for alloc queue*/
    uint8 stacking_trunk_ref_cnt;
    uint8 rsv[2];
};
typedef struct sys_queue_info_s sys_queue_info_t;

struct sys_qos_logicport_s
{
    ctc_slistnode_t head;
    uint16 logic_port;
};
typedef struct sys_qos_logicport_s sys_qos_logicport_t;

struct sys_qos_destport_s
{
    ctc_slistnode_t head;
    uint8 lchip;
    uint8 lport;          /*dest port list*/
};
typedef struct sys_qos_destport_s sys_qos_destport_t;

struct sys_service_node_s
{
    uint16 service_id;               /*service id*/
    uint16 rsv;
    ctc_slist_t* p_logic_port_list; /*source logic port list*/
    ctc_slist_t* p_dest_port_list;  /*dest port list*/
};
typedef struct sys_service_node_s sys_service_node_t;


struct sys_queue_channel_s
{
    sys_queue_info_t queue_info;
    ctc_slist_t* p_group_list;

    uint8  shp_en;
    uint8  rsv0[3];
    uint32   pir;
    uint32   pbs;

};
typedef struct sys_queue_channel_s sys_queue_channel_t;

struct sys_queue_reason_s
{
    sys_queue_info_t queue_info;
    uint8   dest_type;
    uint8   is_user_define;
    uint8   lport_en;
    uint8   rsv;
    uint32  dsfwd_offset;
    uint32  dest_port;
    uint32  dest_map;
};
typedef struct sys_queue_reason_s sys_queue_reason_t;

struct sys_queue_shp_profile_s
{
    uint8 profile_id;
    uint8 is_cpu_que_prof;
    uint16 rsv;
    ds_que_shp_profile_t profile;
};
typedef struct sys_queue_shp_profile_s sys_queue_shp_profile_t;

struct sys_group_shp_profile_s
{
    uint8 profile_id;
    ds_grp_shp_profile_t profile;
};
typedef struct sys_group_shp_profile_s sys_group_shp_profile_t;

/**
 @brief Queue drop profile
*/
struct sys_queue_drop_profile_s
{
    uint8 profile_id;
    uint8 rsv[3];
    ds_que_thrd_profile_t profile[CTC_RESRC_MAX_CONGEST_LEVEL_NUM];
};
typedef struct sys_queue_drop_profile_s sys_queue_drop_profile_t;

struct sys_queue_node_s
{
    ctc_slistnode_t head;

    uint8  type;
    uint8  queue_class;
    uint8  channel;
    uint8  wred_weight;

    uint8  offset;
    uint8  force_random_drop;
    uint8  stats_en;
    uint8  shp_en;

    uint8 confirm_class;
    uint8 exceed_class;
    uint16 confirm_weight;
    uint16 exceed_weight;

    uint16 queue_id;
    uint16 group_id;

    uint32   cir;
    uint32   cbs;
    uint32   pir;
    uint32   pbs;

    uint32 wdrr_weight;

    sys_queue_drop_profile_t* p_drop_profile;
    sys_queue_shp_profile_t* p_queue_shp_profile;

};
typedef struct sys_queue_node_s sys_queue_node_t;

struct sys_queue_group_node_s
{
    ctc_slistnode_t head;
    ctc_slist_t* p_queue_list;

    uint16 start_queue_id;
    uint8 resv0;
    uint8 shp_en;

    uint16 group_id;
    uint16 bond_group;

    uint8 group_bond_en;
    uint8 resv1;
    uint16 shp_bitmap;


    uint32   pir;
    uint32   pbs;

    sys_group_shp_profile_t* p_group_shp_profile;


};
typedef struct sys_queue_group_node_s sys_queue_group_node_t;

/**
 @brief  qos policer granularity for rate range
*/
struct sys_qos_shape_granularity_range_s
{
    uint32 min_rate;        /* unit is Mbps */
    uint32 max_rate;        /* unit is Mbps */
    uint32 granularity;     /* unit is Kbps */
    uint8  is_valid;
    uint8  rsv[2];
};
typedef struct sys_qos_shape_granularity_range_s sys_qos_shape_granularity_range_t;

#define SYS_MAX_SHAPE_GRANULARITY_RANGE_NUM       5
#define SYS_MAX_SHAPE_SUPPORTED_FREQ_NUM          5

/**
 @brief  qos policer granularity
*/
struct sys_qos_shape_granularity_s
{
    uint16 core_frequency;
    uint16 tick_gen_interval;
    uint16 min_shape_ptr[SYS_SHAPE_TYPE_MAX];
    uint16 max_shape_ptr[SYS_SHAPE_TYPE_MAX];
    uint8  min_ts_tick_shift;
    uint8  factor[SYS_SHAPE_TYPE_MAX];
    uint8  exponet[SYS_SHAPE_TYPE_MAX];
    sys_qos_shape_granularity_range_t range[SYS_MAX_SHAPE_GRANULARITY_RANGE_NUM];
};
typedef struct sys_qos_shape_granularity_s sys_qos_shape_granularity_t;

struct sys_qos_resc_egs_pool_s
{
   uint8 egs_congest_level_num;
   uint8 default_profile_id;
};
typedef struct sys_qos_resc_egs_pool_s sys_qos_resc_egs_pool_t;

struct sys_qos_resc_igs_port_min_s
{
   uint16 ref;
   uint16 min;
};
typedef struct sys_qos_resc_igs_port_min_s sys_qos_resc_igs_port_min_t;

struct sys_qos_profile_head_s
{
   uint16 profile_id;
   uint16 profile_len;
};
typedef struct sys_qos_profile_head_s sys_qos_profile_head_t;

struct sys_qos_fc_profile_s
{
    sys_qos_profile_head_t head;
    uint16 xon;
    uint16 xoff;
    uint16 thrd;
};
typedef struct sys_qos_fc_profile_s sys_qos_fc_profile_t;

struct sys_queue_master_s
{
    sys_queue_num_ctl_t  queue_num_ctl[SYS_QUEUE_TYPE_MAX];
    sys_queue_channel_t channel[SYS_MAX_LOCAL_PORT]; /* info about local phy port actually */

    ctc_vector_t* reason_vec;
    ctc_vector_t* queue_vec;
    ctc_vector_t* group_vec;
    ctc_spool_t* p_queue_profile_pool;
    ctc_spool_t* p_drop_profile_pool;
    ctc_spool_t* p_group_profile_pool;
    ctc_spool_t* p_resrc_profile_pool[SYS_RESRC_PROFILE_MAX];
    sys_qos_fc_profile_t *p_fc_profile[SYS_MAX_CHANNEL_NUM];
    sys_qos_fc_profile_t *p_pfc_profile[SYS_MAX_CHANNEL_NUM][8];

    ctc_hash_t* p_service_hash;
    ctc_hash_t* p_queue_hash;
    uint8 queue_pktadj_len[7];
    uint8 shp_pps_en;
    uint8 cpu_reason_grp_que_num;
    uint8 enq_mode;  /* 1: 8+8 mode*/
    uint8 c2c_group_base; /* c2c group base for CPU queue*/
    uint8 cpu_que_shp_prof_num; /* Reserved queue shape profile for CPU queue*/
    uint8 c2c_enq_mode;
    uint8 priority_mode;


    sys_qos_shape_granularity_t granularity;

    uint16 wrr_weight_mtu;
    uint16 reason_grp_start;
    sys_qos_resc_egs_pool_t egs_pool[CTC_QOS_EGS_RESRC_POOL_MAX];

    sal_mutex_t* mutex;
};
typedef struct sys_queue_master_s sys_queue_master_t;

#define SYS_QOS_QUEUE_CREATE_LOCK(lchip)                         \
    do                                                              \
    {                                                               \
        sal_mutex_create(&p_gb_queue_master[lchip]->mutex);  \
        if (NULL == p_gb_queue_master[lchip]->mutex)         \
        {                                                           \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);              \
        }                                                           \
    } while (0)

#define SYS_QOS_QUEUE_LOCK(lchip) \
    sal_mutex_lock(p_gb_queue_master[lchip]->mutex)

#define SYS_QOS_QUEUE_UNLOCK(lchip) \
    sal_mutex_unlock(p_gb_queue_master[lchip]->mutex)

#define CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gb_queue_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

#define CTC_MAX_VALUE_CHECK_QOS_QUEUE_UNLOCK(var, max_value) \
    { \
        if ((var) > (max_value)){\
            sal_mutex_unlock(p_gb_queue_master[lchip]->mutex);\
            return CTC_E_INVALID_PARAM; } \
    }

#define CTC_VALUE_RANGE_CHECK_QOS_QUEUE_UNLOCK(var, min_value, max_value)   \
    { \
        if ((var) < (min_value) || (var) > (max_value)){\
            sal_mutex_unlock(p_gb_queue_master[lchip]->mutex);\
            return CTC_E_INVALID_PARAM; } \
    }
extern sys_queue_master_t* p_gb_queue_master[CTC_MAX_LOCAL_CHIP_NUM];
/*********************************************************************
 *
 * Data Structure
 *
 *********************************************************************/

/**
 @brief Allocation of queue select type.
*/
enum sys_queue_select_type_e
{
    SYS_QSEL_TYPE_REGULAR           = 0,
    SYS_QSEL_TYPE_INTERNAL_PORT     = 0,
    SYS_QSEL_TYPE_EXCP_CPU          = 0x01,
    SYS_QSEL_TYPE_SERVICE           = 0x03,
    SYS_QSEL_TYPE_MIRROR            = 0x04,
    SYS_QSEL_TYPE_CPU_TX            = 0x05,
    SYS_QSEL_TYPE_C2C               = 0x06,
    SYS_QSEL_TYPE_MCAST             = 0x1F
};
typedef enum sys_queue_select_type_e sys_queue_select_type_t;

/*********************************************************************
 *
 * Function Declaration
 *
 *********************************************************************/
 extern int32
 sys_greatbelt_qos_bind_service_logic_port(uint8 lchip, uint16 service_id,
                                           uint16 logic_port);
 extern int32
sys_greatbelt_qos_unbind_service_logic_port(uint8 lchip, uint16 service_id,
                                             uint16 logic_port);


extern sys_queue_info_t*
sys_greatbelt_queue_info_lookup(uint8 lchip,
                             sys_queue_info_t* p_queue_info);


extern int32
 _sys_greatbelt_queue_get_queue_id(uint8 lchip,
                                   ctc_qos_queue_id_t* p_queue,
                                   uint16* queue_id);

extern int32
sys_greatbelt_qos_queue_set(uint8 lchip, ctc_qos_queue_cfg_t* p_que_cfg);

extern int32
sys_greatbelt_qos_queue_get(uint8 lchip, ctc_qos_queue_cfg_t* p_que_cfg);

extern int32
sys_greatbelt_set_local_switch_enable(uint8 lchip, bool enable);

extern int32
sys_greatbelt_get_local_switch_enable(uint8 lchip, bool* p_enable);

extern int32
sys_greatbelt_port_queue_add(uint8 lchip,
                             uint8 queue_type,
                             uint8 lport,
                             uint8 channel);

extern int32
sys_greatbelt_port_queue_add_for_stacking(uint8 lchip,
                             uint8 queue_type,
                             uint8 lport,
                             uint8 channel);

extern int32
sys_greatbelt_port_queue_remove(uint8 lchip,
                                uint8 queue_type,
                                uint8 lport);
extern int32
sys_greatbelt_port_queue_swap(uint8 lchip, uint8 lport1, uint8 lport2);

extern int32
sys_greatbelt_port_queue_flush(uint8 lchip, uint8 lport, bool enable);


extern int32
sys_greatbelt_queue_add_to_channel(uint8 lchip,
                                   uint16 queue_id,
                                   uint8 channel);

extern int32
sys_greatbelt_queue_remove_from_channel(uint8 lchip,
                                        uint16 queue_id,
                                        uint8 channel);

extern int32
sys_greatbelt_add_port_to_channel(uint8 lchip, uint16 gport, uint8 channel);

extern int32
sys_greatbelt_remove_port_from_channel(uint8 lchip, uint16 gport, uint8 channel);

extern int32
sys_greatbelt_queue_add_to_group(uint8 lchip,
                                 uint16 queue_id,
                                 uint16 group_id);

extern int32
sys_greatbelt_queue_remove_from_group(uint8 lchip,
                                      uint16 queue_id,
                                      uint16 group_id);
extern int32
sys_greatbelt_set_priority_queue_select(uint8 lchip,
                                        uint8 priority,
                                        uint8 queue_select);

extern int32
sys_greatbelt_queue_stats_query(uint8 lchip, ctc_qos_queue_stats_t* p_stats);

extern int32
sys_greatbelt_queue_stats_clear(uint8 lchip, ctc_qos_queue_stats_t* p_stats);

extern int32
sys_greatbelt_qos_resrc_mgr_en(uint8 lchip, uint8 enable);

extern int32
sys_greatbelt_qos_flow_ctl_profile(uint8 lchip, sys_qos_fc_prop_t *fc_prop);

extern int32
sys_greatbelt_queue_get_queue_num(uint8 lchip, sys_queue_type_t queue_type);

extern uint8
sys_greatbelt_queue_get_enqueue_mode(uint8 lchip);

extern int32
sys_greatbelt_queue_get_c2c_queue_base(uint8 lchip, uint16* c2c_queue_base);

extern int32
sys_greatbelt_queue_cpu_reason_init(uint8 lchip);

extern int32
sys_greatbelt_queue_set_c2c_queue_mode(uint8 lchip, uint8 mode);

extern int32
sys_greatbelt_queue_enq_init(uint8 lchip, void *p_glb_parm);

extern int32
sys_greatbelt_queue_deinit(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif

