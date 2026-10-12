/**
 @file sys_goldengate_queue_enq.h

 @date 2010-01-13

 @version v2.0

 The file defines macro, data structure, and function for en-queue
*/

#ifndef _SYS_GOLDENGATE_QUEUE_ENQ_H_
#define _SYS_GOLDENGATE_QUEUE_ENQ_H_
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

#include "sys_goldengate_common.h"

/*********************************************************************
 *
 * Macro
 *
 *********************************************************************/

#define SYS_QUEUE_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(qos, queue, QOS_QUE_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0);

#define SYS_QUEUE_DBG_FUNC()           SYS_QUEUE_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__)
#define SYS_QUEUE_DBG_INFO(FMT, ...)  SYS_QUEUE_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ##__VA_ARGS__)
#define SYS_QUEUE_DBG_ERROR(FMT, ...) SYS_QUEUE_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, FMT, ##__VA_ARGS__)
#define SYS_QUEUE_DBG_PARAM(FMT, ...) SYS_QUEUE_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ##__VA_ARGS__)
#define SYS_QUEUE_DBG_DUMP(FMT, ...)  SYS_QUEUE_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__)




#define SYS_MAX_QUEUE_NUM           1024
#define SYS_MAX_QUEUE_GROUP_NUM     256

#define SYS_MAX_QUEUE_SUPER_GRP_NUM     256


#define SYS_MIN_QUEUE_NUM_PER_CHAN   8
#define SYS_LCPU_QUEUE_NUM           ((p_gg_queue_master[lchip]->max_dma_rx_num == 1) ? 64 : 128)
#define SYS_QUEUE_DESTID_ENQ(lchip)  (2 == p_gg_queue_master[lchip]->enq_mode)

#define SYS_OAM_QUEUE_NUM            8
#define SYS_STACKING_PORT_NUM        4
#define SYS_QUEUE_NUM_PER_GROUP      8
#define SYS_SUB_GROUP_NUM_PER_GROUP  2

#define SYS_QUEUE_BUCKET_MAX      16
#define SYS_QUEUE_PIR_BUCKET      8

#define SYS_INTERNAL_PORT_START      48
#define SYS_INTERNAL_PORT_NUM        200
#define SYS_GRP_SHP_CBUCKET_NUM       3

/*at lease one packet's mtu*/
#define SYS_WFQ_DEFAULT_WEIGHT    800
#define SYS_WFQ_DEFAULT_SHIFT      2

#define SYS_SHP_GRAN_RANAGE_NUM   7

#define SYS_SHAPE_TOKEN_RATE_BIT_WIDTH 256
#define SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH 4
#define SYS_SHAPE_MAX_TOKEN_THRD    ((1 << 8) -1)

#define SYS_MAP_GROUPID_TO_SLICE(group_id)    (group_id >= 128)
#define SYS_MAP_QUEUEID_TO_SLICE(queue_id)    (queue_id >= 1024)

#define SYS_RESRC_IGS_MAX_PORT_MIN_PROFILE   8

#define SYS_RESRC_PROFILE_QUEUE 0
#define SYS_RESRC_PROFILE_FC 1
#define SYS_RESRC_PROFILE_PFC 2
#define SYS_RESRC_PROFILE_MAX 3

#define SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd) {\
   if (is_pps)\
   {\
      token_thrd = token_thrd/1000; /*pps*/ \
   }\
   else\
   {\
      token_thrd = token_thrd/125; /*kb*/ \
   }\
}
#define SYS_QOS_CLASS_PRIORITY_MAX (p_gg_queue_master[lchip]->priority_mode ? 15 : 63)
enum sys_queue_type_e
{
    SYS_QUEUE_TYPE_NORMAL,     /**< normal en queue */
    SYS_QUEUE_TYPE_EXCP,       /**< exception to cpu by DMA*/
	SYS_QUEUE_TYPE_RSV_PORT,   /**< reserved port */
    SYS_QUEUE_TYPE_EXTENDER_WITH_Q,
   	SYS_QUEUE_TYPE_OAM,
    SYS_QUEUE_TYPE_EXCP_BY_ETH,       /**< exception to cpu by network port*/

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

enum sys_queue_grp_shp_cir_type_e
{
    SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_FAIL,
    SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_PASS,
    SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_USER,
    SYS_QUEUE_GRP_SHP_CIR_TYPE_MAX
};
typedef enum sys_queue_grp_shp_cir_type_e sys_queue_grp_shp_cir_type_t;

struct sys_queue_shp_grp_info_s
{
    uint32   rate;
    uint16   thrd;
	uint8    cir_type;
	uint8    bucket_sel;
};
typedef struct sys_queue_shp_grp_info_s sys_queue_shp_grp_info_t;


/**
 @brief En-queue configurations.
*/
struct sys_enqueue_info_s
{
    uint8  queue_num;
    uint8  subq_num;
    uint8  queue_type;
    uint8  ucast;
    uint8  mirror;
    uint16 queue_base;
    uint16 dest_base;
    uint16 dest_num;
    uint8 stacking;
    uint8 cpu_tx;
    uint8 iloop_to_cpu;
    uint8 is_c2c;

};
typedef struct sys_enqueue_info_s sys_enqueue_info_t;


struct sys_cpu_reason_s
{
    uint8    dest_type;
	uint8    is_user_define;
    uint8    lport_en;
	uint8    rsv;
    uint32   dsfwd_offset;
	uint16   sub_queue_id;
    uint32   dest_port;
    uint16   dest_map;
};
typedef struct sys_cpu_reason_s sys_cpu_reason_t;

struct sys_queue_shp_profile_s
{
    uint8 profile_id;
    uint8 is_cpu_que_prof;
    uint16  bucket_thrd;  /*[11:4] rate : [3:0] thrd_shift */
    uint32  bucket_rate; /*[30:8] rate : [7:0] rate_frac */

};
typedef struct sys_queue_shp_profile_s sys_queue_shp_profile_t;

struct sys_group_shp_profile_s
{
    uint8 profile_id;
    uint8 exact_match;
    uint16  bucket_thrd;
    uint32  bucket_rate;

    sys_queue_shp_grp_info_t c_bucket[SYS_GRP_SHP_CBUCKET_NUM];

};
typedef struct sys_group_shp_profile_s sys_group_shp_profile_t;


/**
 @brief Queue drop profile
*/
struct sys_queue_drop_profile_data_s
{
   uint32    ecn_mark_thrd;
   uint32    wred_max_thrd3;
   uint32    wred_max_thrd2;

   uint32    wred_max_thrd1;
   uint32    wred_max_thrd0;

   uint8     factor0;
   uint8     factor1;
   uint8     factor2;
   uint8     factor3;

};
typedef struct sys_queue_drop_profile_data_s sys_queue_drop_profile_data_t;

struct sys_queue_drop_profile_s
{
    uint8    profile_id;
    uint8    rsv[3];

    sys_queue_drop_profile_data_t profile[CTC_RESRC_MAX_CONGEST_LEVEL_NUM];
};
typedef struct sys_queue_drop_profile_s sys_queue_drop_profile_t;

struct sys_queue_node_s
{
    uint8  type;
    uint8  offset;
    uint8  shp_en;
    uint8  rsv;

	uint16 queue_id;   /*0~2047*/

    sys_queue_drop_profile_t* p_drop_profile;
    sys_queue_shp_profile_t* p_shp_profile;

};
typedef struct sys_queue_node_s sys_queue_node_t;


struct sys_queue_group_shp_node_s
{
    uint16   shp_bitmap;
    sys_queue_shp_grp_info_t c_bucket[SYS_QUEUE_NUM_PER_GROUP];

    sys_group_shp_profile_t* p_shp_profile;
};
typedef struct sys_queue_group_shp_node_s sys_queue_group_shp_node_t;

struct sys_queue_group_node_s
{
    uint16   group_id;      /*super group; 0~255 ,include slice info.*/
    uint8    chan_id;       /*-0~127*/
    uint16   sub_group_id;    /*0, 8 queue mode; 1, 4 queue mode.*/
    sys_queue_group_shp_node_t grp_shp[SYS_SUB_GROUP_NUM_PER_GROUP];
};
typedef struct sys_queue_group_node_s sys_queue_group_node_t;

struct sys_qos_rate_granularity_s
{
   uint32 max_rate;        /* unit is Mbps */
   uint32 granularity;     /* unit is Kbps */
};
typedef struct sys_qos_rate_granularity_s sys_qos_rate_granularity_t;

struct sys_qos_resc_igs_port_min_s
{
   uint16 ref;
   uint16 min;
};
typedef struct sys_qos_resc_igs_port_min_s sys_qos_resc_igs_port_min_t;

struct sys_qos_resc_egs_pool_s
{
   uint8 egs_congest_level_num;
   uint8 default_profile_id;
};
typedef struct sys_qos_resc_egs_pool_s sys_qos_resc_egs_pool_t;


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
    uint16  queue_base[SYS_QUEUE_TYPE_MAX];
    sys_cpu_reason_t  cpu_reason[CTC_PKT_CPU_REASON_MAX_COUNT];

    ctc_vector_t*  queue_vec;
    ctc_vector_t*  group_vec;
    ctc_spool_t* p_queue_profile_pool[SYS_MAX_LOCAL_SLICE_NUM];
    ctc_spool_t* p_drop_profile_pool;
    ctc_spool_t* p_group_profile_pool[SYS_MAX_LOCAL_SLICE_NUM];

    ctc_spool_t* p_resrc_profile_pool[SYS_RESRC_PROFILE_MAX];
    sys_qos_fc_profile_t *p_fc_profile[SYS_MAX_CHANNEL_ID*2];
    sys_qos_fc_profile_t *p_pfc_profile[SYS_MAX_CHANNEL_ID*2][8];

    uint8 igs_resrc_mode;
    uint8 egs_resrc_mode;

    uint8 store_chan_shp_en;
    uint8 store_que_shp_en;

    uint8 queue_pktadj_len[7];
    uint8 shp_pps_en;
    uint8 have_lcpu_by_eth;
    uint8 queue_num_per_chanel;
    uint8 max_chan_per_slice;
    uint8 cpu_eth_chan_id;
    uint8 max_dma_rx_num;
    uint8 monitor_drop_en;
    uint32 chan_shp_update_freq;
    uint32 que_shp_update_freq;
    sys_qos_rate_granularity_t que_shp_gran[SYS_SHP_GRAN_RANAGE_NUM];
    uint8 support_stacking;
    uint8 queue_num_for_cpu_reason;
    uint16 wrr_weight_mtu;
    sys_qos_resc_igs_port_min_t igs_port_min[SYS_RESRC_IGS_MAX_PORT_MIN_PROFILE];
    sys_qos_resc_egs_pool_t egs_pool[CTC_QOS_EGS_RESRC_POOL_MAX];
    uint8 enq_mode;  /* 0: 8+8 mode, channel based; 1: 8+4+1 mode, channel based; 2: bpe mode, port based*/
    uint8 c2c_group_base; /* c2c group base for CPU queue*/
    uint8 cpu_que_shp_prof_num; /* Reserved queue shape profile for CPU queue*/
    uint8 group_mode; /*For 6 cir mode*/
    uint8 priority_mode;   /**< Qos priority mode.0:SUPPORT 0-63 priority;1:SUPPORT 0-15 priority */
    uint8 stacking_queue_mode;   /**< 0:only C2C packet use stacking C2C queue 1: C2C packet and remote cpu packet use stacking C2C queue */
    uint8 rsv[2];

    sal_mutex_t* mutex;
};
typedef struct sys_queue_master_s sys_queue_master_t;

#define SYS_QOS_QUEUE_INIT_CHECK(lchip)       \
do{                                             \
    SYS_LCHIP_CHECK_ACTIVE(lchip);              \
    if (p_gg_queue_master[lchip] == NULL) \
    {return CTC_E_NOT_INIT;}                    \
}while (0)

#define SYS_QOS_QUEUE_CREATE_LOCK(lchip)                         \
    do                                                              \
    {                                                               \
        sal_mutex_create(&p_gg_queue_master[lchip]->mutex);  \
        if (NULL == p_gg_queue_master[lchip]->mutex)         \
        {                                                           \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);              \
        }                                                           \
    } while (0)

#define SYS_QOS_QUEUE_LOCK(lchip) \
    sal_mutex_lock(p_gg_queue_master[lchip]->mutex)

#define SYS_QOS_QUEUE_UNLOCK(lchip) \
    sal_mutex_unlock(p_gg_queue_master[lchip]->mutex)

#define CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gg_queue_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

#define  CTC_MAX_VALUE_CHECK_QOS_QUEUE_UNLOCK(var, max_value) \
    { \
        if ((var) > (max_value)){\
            sal_mutex_unlock(p_gg_queue_master[lchip]->mutex);\
            return CTC_E_INVALID_PARAM; } \
    }
extern sys_queue_master_t* p_gg_queue_master[CTC_MAX_LOCAL_CHIP_NUM];
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
    SYS_QSEL_TYPE_REGULAR             = 0,
    SYS_QSEL_TYPE_EXCP_CPU           = 0x01,
    SYS_QSEL_TYPE_RSV_PORT           = 0x02,  /*only used in Queue module*/
    SYS_QSEL_TYPE_EXTERN_PORT_WITH_Q = 0x03,  /*only used in Queue module*/
    SYS_QSEL_TYPE_MIRROR             = 0x04,
    SYS_QSEL_TYPE_EXCP_CPU_BY_ETH    = 0x05,
    SYS_QSEL_TYPE_CPU_TX    = SYS_QSEL_TYPE_EXTERN_PORT_WITH_Q,
    SYS_QSEL_TYPE_C2C    = 0x06,
    SYS_QSEL_TYPE_ILOOP    = 0x07,   /* for packet to CPU by iloop*/
};
typedef enum sys_queue_select_type_e sys_queue_select_type_t;

/*********************************************************************
 *
 * Function Declaration
 *
 *********************************************************************/
extern int32
sys_goldengate_queue_get_grp_mode(uint8 lchip, uint16 group_id, uint8 *mode);


extern int32
sys_goldengate_queue_set_port_drop_en(uint8 lchip, uint16 gport, bool enable);

extern int32
sys_goldengate_queue_get_port_depth(uint8 lchip, uint16 gport, uint32* p_depth);


extern int32
sys_goldengate_qos_flow_ctl_profile(uint8 lchip, uint16 gport, uint8 cos, uint8 pfc_class, uint8 is_pfc,  uint8 enable);

extern int32
sys_goldengate_qos_set_fc_default_profile(uint8 lchip, uint32 gport);

 extern int32
sys_goldengate_queue_get_queue_id(uint8 lchip, ctc_qos_queue_id_t* p_queue,
                                   uint16* queue_id);

extern int32
sys_goldengate_queue_get_enqueue_info(uint8 lchip, uint16* port_num, uint8 *destid_mode);

extern int32
sys_goldengate_queue_get_c2c_queue_base(uint8 lchip, uint16* c2c_queue_base);

extern int32
sys_goldengate_qos_queue_set(uint8 lchip, ctc_qos_queue_cfg_t* p_que_cfg);

extern int32
sys_goldengate_set_local_switch_enable(uint8 lchip, bool enable);

extern int32
sys_goldengate_get_local_switch_enable(uint8 lchip, bool* p_enable);

extern int32
sys_goldengate_port_queue_add(uint8 lchip,
                             uint8 queue_type,
                             uint8 lport);

extern int32
sys_goldengate_port_queue_remove(uint8 lchip,
                                uint8 queue_type,
                                uint8 lport);

extern int32
sys_goldengate_qos_add_group_to_channel(uint8 lchip,  uint16 queue_id,
                                   uint8 channel,
                                   uint8 start_group);

extern int32
sys_goldengate_qos_remove_group_to_channel(uint8 lchip,   uint16 queue_id,
                                        uint8 channel);

extern int32
sys_goldengate_add_port_to_channel(uint8 lchip, uint16 gport, uint8 channel);

extern int32
sys_goldengate_get_channel_agg_group(uint8 lchip, uint16 chan_id, uint8* chanagg_id);
extern int32
sys_goldengate_add_port_to_channel_agg(uint8 lchip, uint16 lport, uint8 channel_agg);
extern int32
sys_goldengate_remove_port_from_channel_agg(uint8 lchip, uint16 lport);

extern int32
sys_goldengate_remove_port_from_channel(uint8 lchip, uint16 gport, uint8 channel);

extern int32
sys_goldengate_add_port_range_to_channel(uint8 lchip, uint16 lport_start, uint16 lport_end, uint8 channel);

extern int32
sys_goldengate_remove_port_range_to_channel(uint8 lchip, uint16 lport_start, uint16 lport_end, uint8 channel);

extern int32
sys_goldengate_set_priority_queue_select(uint8 lchip,
                                        uint8 priority,
                                        uint8 queue_select);

extern int32
sys_goldengate_queue_stats_query(uint8 lchip, ctc_qos_queue_stats_t* p_stats);

extern int32
sys_goldengate_queue_stats_clear(uint8 lchip, ctc_qos_queue_stats_t* p_stats);

extern int32
sys_goldengate_qos_set_monitor_drop_queue_id(uint8 lchip, ctc_qos_queue_drop_monitor_t* drop_monitor);
extern int32
sys_goldengate_qos_get_monitor_drop_queue_id(uint8 lchip, ctc_qos_queue_drop_monitor_t* drop_monitor);

extern int32
sys_goldengate_set_dma_channel_drop_en(uint8 lchip, bool enable);

extern int32
sys_goldengate_queue_init_c2c_queue(uint8 lchip, uint8 c2c_group_base, uint8 enq_mode, uint8 queue_type);

extern int32
sys_goldengate_queue_set_c2c_queue_mode(uint8 lchip, uint8 enq_mode);

extern int32
sys_goldengate_queue_enq_init(uint8 lchip, void* p_glb_parm);

extern int32
sys_goldengate_queue_deinit(uint8 lchip);

extern int32
sys_goldengate_get_channel_by_port(uint8 lchip, uint16 gport, uint8 *channel);
extern int32
sys_goldengate_queue_set_to_stacking_port(uint8 lchip, uint16 gport,uint8 enable);

extern int32
sys_goldengate_qos_map_token_rate_hw_to_user(uint8 lchip, uint8 is_pps,
                                            uint32 hw_rate,
                                            uint32 *user_rate,
                                            uint16 bit_with,
                                            uint32 upd_freq);
extern int32
sys_goldengate_qos_map_token_rate_user_to_hw(uint8 lchip, uint8 is_pps,
                                            uint32 user_rate, /*kb/s*/
                                            uint32 *hw_rate,
                                            uint16 bit_with,
                                            uint32 gran,
                                            uint32 upd_freq );
extern int32
sys_goldengate_qos_map_token_thrd_hw_to_user(uint8 lchip, uint32  *user_bucket_thrd,
                                            uint16 hw_bucket_thrd,
                                            uint8 shift_bits);
extern int32
sys_goldengate_qos_map_token_thrd_user_to_hw(uint8 lchip, uint32  user_bucket_thrd,
                                            uint32 *hw_bucket_thrd,
                                            uint8 shift_bits,
                                            uint32 max_thrd);

extern bool
sys_goldengate_queue_shp_get_channel_pps_enbale(uint8 lchip, uint8 chand_id);

extern int32
sys_goldengate_qos_queue_dump_status(uint8 lchip);

extern int32
sys_goldengate_qos_sched_dump_status(uint8 lchip);

extern int32
sys_goldengate_qos_policer_dump_status(uint8 lchip);

extern int32
sys_goldengate_qos_drop_dump_status(uint8 lchip);
extern int32
sys_goldengate_qos_shape_dump_status(uint8 lchip);
extern int
sys_godengate_qos_set_shape_gran(uint8 lchip,uint8 mode);

#ifdef __cplusplus
}
#endif

#endif

