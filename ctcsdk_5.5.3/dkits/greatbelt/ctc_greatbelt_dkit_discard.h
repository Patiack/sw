#ifndef _CTC_DKIT_GREATBELT_DISCARD_H_
#define _CTC_DKIT_GREATBELT_DISCARD_H_


#ifdef __cplusplus
extern "C" {
#endif


#include "sal.h"
#include "ctc_greatbelt_dkit_discard_type.h"

#define CTC_DKIT_IPE_DISCARD_TYPE_NUM     64
#define CTC_DKIT_EPE_DISCARD_TYPE_NUM     64
#define CTC_DKIT_INVALID_PORT              0xFFFF
#define CTC_DKIT_REASON_LEN                            64
#define CTC_DKIT_CHANNEL_NUM                         64
#define CTC_DKIT_SLICE_NUM                             1


struct ctc_dkit_discard_stats_ipe_hw_s
{
    uint8 total;
    uint8 har_adj;
    uint8 intf_map;
    uint8 lkup_mgr;

    uint8 pkt_proc;
    uint8 rsv[3];
};
typedef struct ctc_dkit_discard_stats_ipe_hw_s ctc_dkit_discard_stats_ipe_hw_t;

struct ctc_dkit_discard_stats_ipe_s
{
    uint8 future_discard[CTC_DKIT_IPE_DISCARD_TYPE_NUM];
    ctc_dkit_discard_stats_ipe_hw_t hw_discard;
};
typedef struct ctc_dkit_discard_stats_ipe_s ctc_dkit_discard_stats_ipe_t;

struct ctc_dkit_discard_stats_epe_hw_s
{
    uint8 total;
    uint8 har_adj;
    uint8 next_hop;
    uint8 acl_qos;

    uint8 oam;
    uint8 rsv[3];
};
typedef struct ctc_dkit_discard_stats_epe_hw_s ctc_dkit_discard_stats_epe_hw_t;

struct ctc_dkit_discard_stats_epe_s
{
    uint8 future_discard[CTC_DKIT_EPE_DISCARD_TYPE_NUM];
    ctc_dkit_discard_stats_epe_hw_t hw_discard;
};
typedef struct ctc_dkit_discard_stats_epe_s ctc_dkit_discard_stats_epe_t;

struct ctc_dkit_discard_stats_bsr_bufstore_s
{
    uint16 abort_total;
    uint16 abort_data_error;
    uint16 abort_frame_error;
    uint16 abort_over_len_error;
    uint16 abort_under_len;
    uint16 abort_met_fifo_drop;
    uint16 abort_no_buf;

    uint16 silent_total;
    uint16 silent_data_error;
    uint16 silent_chip_mismatch;
    uint16 silent_no_buf;
    uint16 silent_hard_discard;
};
typedef struct ctc_dkit_discard_stats_bsr_bufstore_s ctc_dkit_discard_stats_bsr_bufstore_t;

struct ctc_dkit_discard_stats_bsr_qmgr_s
{
    uint32 total;

    uint8 enq_discard;
    uint8 egr_resrc_mgr;
    uint8 critical_pkt;
    uint8 c2c_pkt;
};
typedef struct ctc_dkit_discard_stats_bsr_qmgr_s ctc_dkit_discard_stats_bsr_qmgr_t;

struct ctc_dkit_discard_stats_bsr_s
{
    ctc_dkit_discard_stats_bsr_bufstore_t bufstore[CTC_DKIT_CHANNEL_NUM];
    ctc_dkit_discard_stats_bsr_qmgr_t qmgr;
};
typedef struct ctc_dkit_discard_stats_bsr_s ctc_dkit_discard_stats_bsr_t;

struct ctc_dkit_discard_stats_oam_s
{
    uint8 hw_error;
    uint8 rsv[3];
};
typedef struct ctc_dkit_discard_stats_oam_s ctc_dkit_discard_stats_oam_t;


struct ctc_dkit_discard_stats_net_rx_s
{
    uint8 data_err;
    uint8 no_buf;
    uint8 sop_err;
    uint8 eop_err;

    uint8 over_len;
    uint8 under_len;
    uint8 pkt_drop;
    uint8 rsv;
};
typedef struct ctc_dkit_discard_stats_net_rx_s ctc_dkit_discard_stats_net_rx_t;

struct ctc_dkit_discard_stats_net_tx_s
{
    uint8 epe_min_len_drop;
    uint8 info_drop;
    uint8 epe_no_buf_drop;
    uint8 epe_no_eop_drop;

    uint8 epe_no_sop_drop;
    uint8 rsv[3];
};
typedef struct ctc_dkit_discard_stats_net_tx_s ctc_dkit_discard_stats_net_tx_t;

struct ctc_dkit_discard_stats_s
{
    ctc_dkit_discard_stats_ipe_t ipe[CTC_DKIT_SLICE_NUM];
    ctc_dkit_discard_stats_epe_t epe[CTC_DKIT_SLICE_NUM];
    ctc_dkit_discard_stats_bsr_t bsr;
    ctc_dkit_discard_stats_oam_t oam;
    ctc_dkit_discard_stats_net_rx_t netrx;
    ctc_dkit_discard_stats_net_tx_t nettx[CTC_DKIT_SLICE_NUM];
};
typedef struct ctc_dkit_discard_stats_s ctc_dkit_discard_stats_t;



enum ctc_dkit_discard_flag_e
{
    CTC_DKIT_DISCARD_FLAG_NONE = 0x00,
    CTC_DKIT_DISCARD_FLAG_IPE=0x01,
    CTC_DKIT_DISCARD_FLAG_EPE=0x02,
    CTC_DKIT_DISCARD_FLAG_BSR=0x04,
    CTC_DKIT_DISCARD_FLAG_OAM=0x08,
    CTC_DKIT_DISCARD_FLAG_NET_RX=0x10,
    CTC_DKIT_DISCARD_FLAG_NET_TX=0x20
} ;
typedef enum ctc_dkit_discard_flag_e ctc_dkit_discard_flag_t;


struct ctc_dkit_discard_reason_s
{
    uint8 module;/*indicate which module discard packet, only one module valid, refer to ctc_dkit_discard_flag_t*/
    uint8 rsv;
    uint16 port;

    uint32 reason_id;
};
typedef struct ctc_dkit_discard_reason_s ctc_dkit_discard_reason_t;


enum ctc_dkit_discard_value_type_e
{
    CTC_DKIT_DISCARD_VALUE_TYPE_NONE = 0,
    CTC_DKIT_DISCARD_VALUE_TYPE_1,    /*1 valid value*/
    CTC_DKIT_DISCARD_VALUE_TYPE_2,    /*2 valid value*/
    CTC_DKIT_DISCARD_VALUE_TYPE_3,    /*3 valid value*/

} ;
typedef enum ctc_dkit_discard_value_type_e ctc_dkit_discard_value_type_t;

struct ctc_dkit_discard_reason_bus_s
{
    uint32 reason_id;

    uint16 gport;/*ctc global port*/
    uint8 value_type;  /*ctc_dkit_discard_value_type_t*/
    uint8 valid;

    uint32 value[4];/*some value used for show reason*/
};
typedef struct ctc_dkit_discard_reason_bus_s ctc_dkit_discard_reason_bus_t;


struct ctc_dkit_discard_reason_captured_s
{
    uint32 reason_id;

    uint8 value_type;  /*ctc_dkit_discard_value_type_t*/
    uint8 rsv[3];

    uint32 value[4];/*some value used for show reason*/
};
typedef struct ctc_dkit_discard_reason_captured_s ctc_dkit_discard_reason_captured_t;


#define CTC_DKIT_DISCARD_REASON_NUM 64
struct ctc_dkit_discard_info_s
{
      char systime_str[50];

      uint8 discard_flag; /*indicate which module discard packet, refer to ctc_dkit_discard_flag_t*/
      uint8 discard_count; /*if discard_count>1, can not use bus info*/
      uint8 slice_ipe; /*only for read bus*/
      uint8 slice_epe;

      ctc_dkit_discard_reason_t discard_reason[CTC_DKIT_DISCARD_REASON_NUM];
      ctc_dkit_discard_reason_bus_t discard_reason_bus;
      ctc_dkit_discard_reason_captured_t discard_reason_captured;
};
typedef struct ctc_dkit_discard_info_s ctc_dkit_discard_info_t;

#define CTC_DKIT_DISCARD_HISTORY_NUM  8
struct ctc_dkit_discard_history_s
{
    ctc_dkit_discard_info_t discard[CTC_DKIT_DISCARD_HISTORY_NUM];
    uint8 front;
    uint8 rear;/*the position is empty*/
};
typedef struct ctc_dkit_discard_history_s ctc_dkit_discard_history_t;


extern int32
ctc_greatbelt_dkit_discard_process(void* p_para);

extern int32
ctc_greatbelt_dkit_discard_show_type(void* p_para);

extern int32
ctc_greatbelt_dkit_discard_display_en(void* p_para);

#ifdef __cplusplus
}
#endif

#endif








