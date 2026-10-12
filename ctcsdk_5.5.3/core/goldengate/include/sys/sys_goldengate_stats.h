/**
 @file sys_goldengate_stats.h

 @date 2009-12-15

 @version v2.0

*/
#ifndef _SYS_GOLDENGATE_STATS_H
#define _SYS_GOLDENGATE_STATS_H
#ifdef __cplusplus
extern "C" {
#endif
/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_const.h"
#include "ctc_stats.h"
#include "ctc_linklist.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/

/* Define cache mode opf index type */
enum sys_stats_cache_mode_opf_type_e
{
    SYS_STATS_CACHE_MODE_POLICER,
    SYS_STATS_CACHE_MODE_IPE_IF_EPE_FWD,    /* for ipe if & epe fwd stats */
    SYS_STATS_CACHE_MODE_EPE_IF_IPE_FWD,    /* for ipe fwd & epe if stats */
    SYS_STATS_CACHE_MODE_IPE_ACL0,
    SYS_STATS_CACHE_MODE_IPE_ACL1,
    SYS_STATS_CACHE_MODE_IPE_ACL2,
    SYS_STATS_CACHE_MODE_IPE_ACL3,
    SYS_STATS_CACHE_MODE_EPE_ACL0,

    SYS_STATS_CACHE_MODE_MAX
};
typedef enum sys_stats_cache_mode_opf_type_e sys_stats_cache_mode_opf_type_t;

/*policing storage structor*/
struct sys_stats_policing_s
{
    uint64 policing_confirm_pkts;
    uint64 policing_confirm_bytes;
    uint64 policing_exceed_pkts;
    uint64 policing_exceed_bytes;
    uint64 policing_violate_pkts;
    uint64 policing_violate_bytes;
};
typedef struct sys_stats_policing_s sys_stats_policing_t;

/*queue storage structor*/
struct sys_stats_queue_s
{
    uint64 queue_drop_pkts;
    uint64 queue_drop_bytes;
    uint64 queue_deq_pkts;
    uint64 queue_deq_bytes;
};
typedef struct sys_stats_queue_s sys_stats_queue_t;

struct sys_stats_statsid_s
{
    uint32 stats_id;
    uint16 stats_ptr;
    uint8  stats_id_type;
    uint8  dir;
    uint8  acl_priority;
    uint8  is_vc_label;
};
typedef struct sys_stats_statsid_s sys_stats_statsid_t;

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
extern int32
sys_goldengate_stats_init(uint8 lchip, ctc_stats_global_cfg_t* stats_global_cfg);

/**
 @brief De-Initialize stats module
*/
extern int32
sys_goldengate_stats_deinit(uint8 lchip);
/*Mac Based Stats*/
extern int32
sys_goldengate_stats_set_mac_packet_length_mtu1(uint8 lchip, uint16 gport, uint16 length);
extern int32
sys_goldengate_stats_get_mac_packet_length_mtu1(uint8 lchip, uint16 gport, uint16* p_length);
extern int32
sys_goldengate_stats_set_mac_packet_length_mtu2(uint8 lchip, uint16 gport, uint16 length);
extern int32
sys_goldengate_stats_get_mac_packet_length_mtu2(uint8 lchip, uint16 gport, uint16* p_length);
extern int32
sys_goldengate_stats_get_mac_rx_stats(uint8 lchip, uint16 gport, ctc_mac_stats_t* p_stats);
extern int32
sys_goldengate_stats_clear_mac_rx_stats(uint8 lchip, uint16 gport);
extern int32
sys_goldengate_stats_get_mac_tx_stats(uint8 lchip, uint16 gport, ctc_mac_stats_t* p_stats);
extern int32
sys_goldengate_stats_clear_mac_tx_stats(uint8 lchip, uint16 gport);
extern int32
sys_goldengate_stats_get_cpu_mac_stats(uint8 lchip, uint16 gport, ctc_cpu_mac_stats_t* p_stats);
extern int32
sys_goldengate_stats_clear_cpu_mac_stats(uint8 lchip, uint16 gport);
/*Port log*/
extern int32
sys_goldengate_stats_set_drop_packet_stats_en(uint8 lchip, ctc_stats_discard_t bitmap, bool enable);
extern int32
sys_goldengate_stats_get_port_log_discard_stats_enable(uint8 lchip, ctc_stats_discard_t bitmap, bool* enable);
extern int32
sys_goldengate_stats_get_igs_port_log_stats(uint8 lchip, uint16 gport, ctc_stats_basic_t* p_stats);
extern int32
sys_goldengate_stats_clear_igs_port_log_stats(uint8 lchip, uint16 gport);
extern int32
sys_goldengate_stats_get_egs_port_log_stats(uint8 lchip, uint16 gport, ctc_stats_basic_t* p_stats);
extern int32
sys_goldengate_stats_clear_egs_port_log_stats(uint8 lchip, uint16 gport);
/*Forwarding Stats*/
extern int32
sys_goldengate_stats_get_flow_stats(uint8 lchip, uint16 stats_ptr, ctc_stats_basic_t* p_stats);
extern int32
sys_goldengate_stats_clear_flow_stats(uint8 lchip, uint16 stats_ptr);
extern int32
sys_goldengate_stats_set_policing_en(uint8 lchip, uint8 enable);
extern int32
sys_goldengate_stats_alloc_policing_statsptr(uint8 lchip, uint16* stats_ptr);
extern int32
sys_goldengate_stats_free_policing_statsptr(uint8 lchip, uint16 stats_ptr);
extern int32
sys_goldengate_stats_get_policing_stats(uint8 lchip, uint16 stats_ptr, sys_stats_policing_t* p_stats);
extern int32
sys_goldengate_stats_clear_policing_stats(uint8 lchip, uint16 stats_ptr);
extern int32
sys_goldengate_stats_set_queue_en(uint8 lchip, uint8 enable);
extern int32
sys_goldengate_stats_get_queue_en(uint8 lchip, uint8* p_enable);
extern int32
sys_goldengate_stats_get_queue_stats(uint8 lchip, uint16 stats_ptr, sys_stats_queue_t* p_stats);
extern int32
sys_goldengate_stats_clear_queue_stats(uint8 lchip, uint16 stats_ptr);
extern int32
sys_goldengate_stats_set_saturate_en(uint8 lchip, ctc_stats_type_t stats_type, bool enable);
extern int32
sys_goldengate_stats_get_saturate_en(uint8 lchip, ctc_stats_type_t stats_type, bool* p_enable);
extern int32
sys_goldengate_stats_set_hold_en(uint8 lchip, ctc_stats_type_t stats_type, bool enable);
extern int32
sys_goldengate_stats_get_hold_en(uint8 lchip, ctc_stats_type_t stats_type, bool* p_enable);
extern int32
sys_goldengate_stats_set_clear_after_read_en(uint8 lchip, ctc_stats_type_t stats_type, bool enable);
extern int32
sys_goldengate_stats_get_clear_after_read_en(uint8 lchip, ctc_stats_type_t stats_type, bool* p_enable);
extern int32
sys_goldengate_stats_get_statsptr(uint8 lchip, uint32 stats_id, uint8 stats_type, uint16* stats_ptr);
extern int32
sys_goldengate_stats_create_statsid(uint8 lchip, ctc_stats_statsid_t* statsid);
extern int32
sys_goldengate_stats_destroy_statsid(uint8 lchip, uint32 stats_id);
extern int32
sys_goldengate_stats_get_stats(uint8 lchip, uint32 stats_id, ctc_stats_basic_t* p_stats);
extern int32
sys_goldengate_stats_clear_stats(uint8 lchip, uint32 stats_id);
extern int32
sys_goldengate_stats_intr_callback_func(uint8 lchip, uint8* p_gchip);
extern int32
sys_goldengate_stats_isr(uint8 lchip, uint32 intr, void* p_data);
extern int32
sys_goldengate_stats_sync_dma_mac_stats(uint8 lchip, void* p_data);
extern void
sys_goldengate_stats_get_port_rate(uint8 lchip, uint16 gport, uint64* p_throughput);
extern int32
sys_goldengate_stats_get_type_by_statsid(uint8 lchip, uint32 stats_id, sys_stats_statsid_t* p_stats_type);


#ifdef __cplusplus
}
#endif

#endif

