
#ifndef _SYS_USW_DIAG_H
#define _SYS_USW_DIAG_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_diag.h"
#include "sys_usw_common.h"


#define SYS_DIAG_DROP_REASON_BASE_IPE 0
#define SYS_DIAG_DROP_REASON_BASE_EPE 64
#define SYS_DIAG_LIST_MAX_NUM   9600

#define SYS_DIAG_DROP_COUNT_SYSTEM 0xFFFF

typedef struct sys_usw_diag_s
{
    ctc_slist_t* drop_pkt_list;  /* refer to sys_usw_diag_drop_pkt_node_t */
    ctc_hash_t*  drop_hash;      /* node refer to sys_usw_diag_drop_info_t*/
    sal_mutex_t* diag_mutex;

    ctc_diag_drop_pkt_report_cb drop_pkt_report_cb;
    uint32 drop_pkt_store_cnt;   /* determine the pkt_list count */
    uint32 drop_pkt_store_len;
    uint16 drop_pkt_hash_len;
    uint8  drop_pkt_overwrite;/* when set, if store drop packet count exceed drop_pkt_store_cnt, overwite the oldest buffer */
    uint8  rsv;
    ctc_hash_t* lb_dist;
    uint32 drop_pkt_id;  /* 0 means invalid, <1-0xFFFFFFFF>*/
	uint32 cur_discard_type_map ;   /* (pos << 16) | discard_type */
    uint32 cur_exception_map ;  /* (excp_index << 16) | excp_sub_index */
}sys_usw_diag_t;

extern sys_usw_diag_t* p_usw_diag_master[CTC_MAX_LOCAL_CHIP_NUM];

extern int32
sys_usw_diag_trigger_pkt_trace(uint8 lchip, ctc_diag_pkt_trace_t* p_trace);

extern int32
sys_usw_diag_get_pkt_trace(uint8 lchip, ctc_diag_pkt_trace_result_t* p_rslt);

extern int32
sys_usw_diag_get_drop_info(uint8 lchip, ctc_diag_drop_t* p_drop);

extern int32
sys_usw_diag_init(uint8 lchip, void* init_cfg);

extern int32
sys_usw_diag_deinit(uint8 lchip);

extern int32
sys_usw_diag_set_property(uint8 lchip, ctc_diag_property_t prop, void* p_value);

extern int32
sys_usw_diag_get_property(uint8 lchip, ctc_diag_property_t prop, void* p_value);

extern int32
sys_usw_diag_tbl_control(uint8 lchip, ctc_diag_tbl_t* p_para);

extern int32
sys_usw_diag_set_lb_distribution(uint8 lchip, ctc_diag_lb_dist_t* p_para);

extern int32
sys_usw_diag_get_lb_distribution(uint8 lchip, ctc_diag_lb_dist_t* p_para);

extern int32
sys_usw_diag_get_mem_usage(uint8 lchip, ctc_diag_mem_usage_t* p_para);
#ifdef __cplusplus
}
#endif

#endif  /*_SYS_USW_DIAG_H*/
