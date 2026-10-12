#ifndef _CTC_GREATBELT_DKIT_PATH_H_
#define _CTC_GREATBELT_DKIT_PATH_H_


#ifdef __cplusplus
extern "C" {
#endif


#include "sal.h"
#include "greatbelt/include/drv_tbl_reg.h"

struct ctc_dkit_path_stats_info_s
{
    net_rx_debug_stats_t net_rx_stats;
    ipe_hdr_adj_debug_stats_t ipe_headadj_stats;
    buf_store_input_debug_stats_t bufstr_input_stats;
    met_fifo_debug_stats_t  met_fifo_stats;
    buf_retrv_input_debug_stats_t bufrtv_input_stats;
    buf_retrv_output_pkt_debug_stats_t bufrtv_output_stats;
    net_tx_debug_stats_t net_tx_stats;
};
typedef struct  ctc_dkit_path_stats_info_s  ctc_dkit_path_stats_info_t;

extern int32
ctc_greatbelt_dkit_path_process(void* p_para);


#ifdef __cplusplus
}
#endif

#endif

