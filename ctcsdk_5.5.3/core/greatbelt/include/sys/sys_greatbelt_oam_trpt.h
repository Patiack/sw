/**
 @file sys_greatbelt_oam_throughput.h

 @date 2010-1-19

 @version v2.0

  The file defines Macro, stored data structure for  Ethernet OAM module
*/
#ifndef _SYS_GREATBELT_OAM_TRPT_H
#define _SYS_GREATBELT_OAM_TRPT_H
#ifdef __cplusplus
extern "C" {
#endif

#include "ctc_debug.h"
#include "ctc_const.h"
#include "greatbelt/include/drv_io.h"
#include "sys_greatbelt_chip_global.h"

#define ENTRY_NUM_PER_SESSION 12
#define BRIDGE_HEADER_WORD_NUM 8
#define TRPT_CYCLE_1024_MID 536
#define TRPT_CYCLE_9600_MAX 5000
#define TRPT_REPARE_RATE 1

#define TRPT_FIRST_INTERVAL_PKT_LEN   512
#define TRPT_SECOND_INTERVAL_PKT_LEN   2048
#define TRPT_THIRD_INTERVAL_PKT_LEN   9600

#define TRPT_SESSION_NUM_USED 4

struct sys_oam_trpt_data_s
{
    uint32 inter;
    uint32 residue;
    uint32 base;
};
typedef struct sys_oam_trpt_data_s sys_oam_trpt_data_t;

struct sys_oam_trpt_session_info_s
{
    uint16 act_cycle;                       /* actual used cycle */
    uint16 ticks_cnt;                        /* ticks cfg */
    uint16 ticks_frace;                     /* ticks frace cfg */
    uint16 lmep_index;                    /* lmep index */
    uint8  is_over_1g;
    uint8  rsv[3];
};
typedef struct sys_oam_trpt_session_info_s sys_oam_trpt_session_info_t;

struct sys_oam_trpt_overflow_rec_s
{
    uint32 tx_oct_overflow_cnt;
    uint32 tx_pkt_overflow_cnt;
    uint32 rx_oct_overflow_cnt;
    uint32 rx_pkt_overflow_cnt;
};
typedef struct sys_oam_trpt_overflow_rec_s sys_oam_trpt_overflow_rec_t;

struct sys_oam_trpt_master_s
{
    sys_oam_trpt_session_info_t session_info[CTC_OAM_TRPT_SESSION_NUM];
    sal_task_t* trpt_stats_thread;
    ctc_oam_trpt_stats_t session_stats[CTC_OAM_TRPT_SESSION_NUM];
    sys_oam_trpt_overflow_rec_t overflow_rec[CTC_OAM_TRPT_SESSION_NUM];
    sal_mutex_t* p_trpt_mutex;
};
typedef struct sys_oam_trpt_master_s sys_oam_trpt_master_t;

#define SYS_OAM_TRPT_INIT_CHECK(lchip) \
do { \
    SYS_LCHIP_CHECK_ACTIVE(lchip);     \
    if (NULL == p_gb_trpt_master[lchip]) \
    { \
        return CTC_E_OAM_TRPT_NOT_INIT; \
    } \
} while (0)

#define OAM_TRPT_LOCK \
    do { \
        if (p_gb_trpt_master[lchip]->p_trpt_mutex){ \
            sal_mutex_lock(p_gb_trpt_master[lchip]->p_trpt_mutex); } \
    } while (0)

#define OAM_TRPT_UNLOCK \
   do { \
        if (p_gb_trpt_master[lchip]->p_trpt_mutex){ \
            sal_mutex_unlock(p_gb_trpt_master[lchip]->p_trpt_mutex); } \
    } while (0)

extern int32
sys_greatbelt_oam_trpt_glb_cfg_check(uint8 lchip, ctc_oam_trpt_t* p_trpt);

extern int32
sys_greatbelt_oam_trpt_set_bridgr_hdr(uint8 lchip, ctc_oam_trpt_t* p_trpt,  ms_packet_header_t* p_bridge_hdr);

extern int32
sys_greatbelt_oam_trpt_set_pkthd(uint8 lchip, uint8 session_id, void* p_header, uint8 header_length);

extern int32
sys_greatbelt_oam_trpt_set_rate(uint8 lchip, uint8 session_id, ctc_oam_trpt_t* p_rate);

extern int32
sys_greatbelt_oam_trpt_set_enable(uint8 lchip, uint8 session_id, uint8 enable);

extern int32
sys_greatbelt_oam_trpt_get_stats(uint8 lchip, uint8 session_id, ctc_oam_trpt_stats_t* p_stat_info);

extern int32
sys_greatbelt_oam_trpt_clear_stats(uint8 lchip, uint8 session_id);

extern int32
sys_greatbelt_oam_trpt_init(uint8 lchip);

extern int32
sys_greatbelt_oam_trpt_deinit(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif

