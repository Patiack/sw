/**
 @file sys_greatbelt_linkagg.h

 @date 2009-10-19

 @version v2.0

 The file define APIs and types use in sys layer
*/
#ifndef _SYS_GREATBELT_LINKAGG_H
#define _SYS_GREATBELT_LINKAGG_H
#ifdef __cplusplus
extern "C" {
#endif
/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_const.h"
#include "ctc_vector.h"

#include "ctc_linkagg.h"
/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
/* member num in all linkAgg groups */
#define SYS_LINKAGG_ALL_MEM_NUM 1024

#define SYS_MAX_DLB_MEMBER_NUM 8
#define DOWN_FRE_RATE  1
#define SYS_DLB_TS_THRES 200
#define SYS_DLB_MAX_PTR 9999
#define SYS_DLB_UPDATE_THRES 10000
#define SYS_DLB_TIMER_INTERVAL 3

/**
 @brief linkagg member number
*/
enum sys_linkagg_mem_num_e
{
    SYS_LINKAGG_MEM_NUM_1 = 8,  /**< 8  members in linkAgg group */
    SYS_LINKAGG_MEM_NUM_2 = 16, /**< 16 members in linkAgg group */
    SYS_LINKAGG_MEM_NUM_3 = 32, /**< 32 members in linkAgg group */
    SYS_LINKAGG_MEM_NUM_4 = 64  /**< 64 members in linkAgg group */
};
typedef enum sys_linkagg_mem_num_e sys_linkagg_mem_num_t;

/* member port info */
struct sys_linkagg_port_s
{
    uint8 valid;  /**< 1: member port is in linkAgg group */
    uint8 pad_flag;  /**< member is padding memeber*/
    uint16 gport; /**< member port */
};
typedef struct sys_linkagg_port_s sys_linkagg_port_t;

/* linkAgg group info */
struct sys_linkagg_s
{
    uint8 tid;      /**< linkAggId */
    uint8 port_cnt; /**< member num of linkAgg group */
    uint8 linkagg_mode; /**< ctc_linkagg_group_mode_t */
    uint8 need_pad;
    sys_linkagg_port_t* port;
    uint16 flag;
};
typedef struct sys_linkagg_s sys_linkagg_t;

struct sys_linkagg_master_s
{
    ctc_vector_t* p_linkagg_vector; /**< store sys_linkagg_t,tid is the index */
    sal_mutex_t* p_linkagg_mutex;
    uint8* mem_port_num;            /**< port num of linkagg group */
    uint8 linkagg_num;              /**< linkagg group num */
    uint8 linkagg_mode;             /**< ctc_linkagg_mode_t */
    uint8 rsv[2];
};
typedef struct sys_linkagg_master_s sys_linkagg_master_t;

#define SYS_LINKAGG_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(linkagg, linkagg, LINKAGG_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0);

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
extern int32
sys_greatbelt_linkagg_init(uint8 lchip, void* linkagg_global_cfg);

/**
 @brief De-Initialize linkagg module
*/
extern int32
sys_greatbelt_linkagg_deinit(uint8 lchip);

extern int32
sys_greatbelt_linkagg_create(uint8 lchip, ctc_linkagg_group_t* p_linkagg_grp);

extern int32
sys_greatbelt_linkagg_destroy(uint8 lchip, uint8 tid);

extern int32
sys_greatbelt_linkagg_add_port(uint8 lchip, uint8 tid, uint16 gport);

extern int32
sys_greatbelt_linkagg_remove_port(uint8 lchip, uint8 tid, uint16 gport);

extern int32
sys_greatbelt_linkagg_get_1st_local_port(uint8 lchip, uint8 tid, uint32* p_gport, uint16* local_cnt);

extern int32
sys_greatbelt_linkagg_show_ports(uint8 lchip, uint8 tid, uint32* p_gports, uint16* cnt);

extern int32
sys_greatbelt_linkagg_set_psc(uint8 lchip, ctc_linkagg_psc_t* psc);

extern int32
sys_greatbelt_linkagg_get_psc(uint8 lchip, ctc_linkagg_psc_t* psc);

extern int32
sys_greatbelt_linkagg_show_all_member(uint8 lchip, uint8 tid, sys_linkagg_port_t** p_linkagg_port, uint8* dyn_mode);

extern int32
sys_greatbelt_linkagg_get_max_mem_num(uint8 lchip, uint16* max_num);

extern int32
sys_greatbelt_mdio_scan_init(uint8 lchip);
extern int32
sys_greatbelt_mdio_scan_bit_cfg(uint8 lchip, uint8 lport, uint8 flag);

extern int32
sys_greatbelt_linkagg_set_flow_inactive_interval(uint8 lchip, uint16 max_ptr, uint32 update_cycle, uint8 inactive_round_num);

extern bool
sys_greatbelt_linkagg_port_is_lag_member(uint8 lchip, uint16 gport, uint16* agg_gport);

extern int32
sys_greatbelt_linkagg_get_mem_num_in_vlan(uint8 lchip, uint16 vlan_ptr, uint16 agg_gport);

extern int32
sys_greatbelt_linkagg_replace_ports(uint8 lchip, uint8 tid, uint32* gport, uint16 mem_num);
#ifdef __cplusplus
}
#endif

#endif

