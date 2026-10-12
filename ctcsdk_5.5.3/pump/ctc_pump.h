/**
 @file ctc_pump.h

 @date 2019-6-6

 @version v1.0

 This file define the pump structures and APIs
*/

#ifndef _CTC_PUMP_H
#define _CTC_PUMP_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"

extern char g_pump_log_en;

extern void
ctc_pump_log_post(int lvl, char* pre_str, char* format, ...);

#define PUMP_LOG(LVL, X, ...)  \
    do { \
        if (g_pump_log_en){\
            char _pre_str[100] = {0};\
            snprintf(_pre_str, 100, "[FUNC:%s][LINE:%d]", __func__, __LINE__);\
        ctc_pump_log_post(LVL,_pre_str,X, ##__VA_ARGS__);}\
    } while (0)


enum ctc_pump_log_level_e
{
    CTC_PUMP_LOG_ERROR,
    CTC_PUMP_LOG_INFO,

    CTC_PUMP_LOG_MAX
};


typedef enum ctc_pump_func_type_e
{
    CTC_PUMP_FUNC_QUERY_TEMPERATURE = 0,
    CTC_PUMP_FUNC_QUERY_VOLTAGE,
    CTC_PUMP_FUNC_QUERY_MAC_STATS,
    CTC_PUMP_FUNC_QUERY_MBURST_STATS,
    CTC_PUMP_FUNC_QUERY_BUFFER_WATERMARK,
    CTC_PUMP_FUNC_QUERY_LATENCY_WATERMARK,

    CTC_PUMP_FUNC_REPORT_BUFFER_STATS,
    CTC_PUMP_FUNC_REPORT_LATENCY_STATS,
    CTC_PUMP_FUNC_REPORT_BUFFER_EVENT,
    CTC_PUMP_FUNC_REPORT_LATENCY_EVENT,
    CTC_PUMP_FUNC_REPORT_MBURST_EVENT,
    CTC_PUMP_FUNC_REPORT_DROP_INFO,
    CTC_PUMP_FUNC_REPORT_EFD,

    CTC_PUMP_FUNC_MAX
}ctc_pump_func_type_t;


typedef struct ctc_pump_mac_stats_s
{
    uint32 port;
    /*Total stats*/
    uint64 rx_pkts;
    uint64 rx_bytes;
    uint64 tx_pkts;
    uint64 tx_bytes;

    /*Error stats*/
    uint64 rx_fcs_error_pkts;
    uint64 rx_mac_overrun_pkts;
    uint64 rx_bad_63_pkts;
    uint64 rx_bad_1519_pkts;
    uint64 rx_bad_jumbo_pkts;
    uint64 tx_pkts_63;
    uint64 tx_jumbo_pkts;
    uint64 tx_mac_underrun_pkts;
    uint64 tx_fcs_error_pkts;
}ctc_pump_mac_stats_t;

typedef enum ctc_pump_buffer_type_e
{
    CTC_PUMP_BUFFER_TOTAL = 0,
    CTC_PUMP_BUFFER_POOL,
    CTC_PUMP_BUFFER_QUEUE,
    CTC_PUMP_BUFFER_PORT,
    CTC_PUMP_BUFFER_C2C,

    CTC_PUMP_BUFFER_MAX
}ctc_pump_buffer_type_t;

#define CTC_PUMP_DIR_IGS  0
#define CTC_PUMP_DIR_EGS  1

#define CTC_PUMP_POOL_DEFAULT      "Default"
#define CTC_PUMP_POOL_NONE_DROP    "NoneDrop"
#define CTC_PUMP_POOL_SPAN         "Span"
#define CTC_PUMP_POOL_CONTROL      "Control"
#define CTC_PUMP_POOL_MIN          "Min"
#define CTC_PUMP_POOL_C2C          "C2C"



typedef enum ctc_pump_buffer_mask_e
{
    CTC_PUMP_BUF_TYPE = 0x1,
    CTC_PUMP_BUF_DIR  = 0x2,
    CTC_PUMP_BUF_PORT = 0x4,
    CTC_PUMP_BUF_SUB_QUEUE = 0x8,
    CTC_PUMP_BUF_QUEUE  = 0x10,
    CTC_PUMP_BUF_POOL = 0x20,
    CTC_PUMP_BUF_TOTAL_BYTES = 0x40,

    CTC_PUMP_BUF_MAX
}ctc_pump_buffer_mask_t;

typedef struct ctc_pump_buffer_s
{
    uint32 type;          /*ctc_pump_buffer_type_t*/
    uint8  dir;     /* "Ingress" "Egress" */
    uint32 port;
    uint8  sub_queue;
    uint32 queue;
    uint8  pool;
    uint64 total_bytes;

    uint32 masks; /*ctc_pump_buffer_mask_t*/
}ctc_pump_buffer_t;

#define CTC_PUMP_LATENCY_LEVEL 8

typedef enum ctc_pump_latency_mask_e
{
    CTC_PUMP_LAT_PORT   = 0x1,
    CTC_PUMP_LAT_LEVEL  = 0x2,
    CTC_PUMP_LAT_NS     = 0x4,
    CTC_PUMP_LAT_THRD   = 0x8,
    CTC_PUMP_LAT_COUNT  = 0x10,

    CTC_PUMP_LAT_MAX
}ctc_pump_latency_mask_t;

struct ctc_pump_latency_s
{
    uint32 port;
    uint8  level;
    uint64 nanoseconds;
    uint32 thrd[CTC_PUMP_LATENCY_LEVEL];
    uint32 count[CTC_PUMP_LATENCY_LEVEL]; /*8 level*/

    uint32 masks; /*ctc_pump_latency_mask_t*/
};
typedef struct  ctc_pump_latency_s ctc_pump_latency_t;
typedef struct  ctc_pump_latency_s ctc_pump_mburst_t;

enum ctc_pump_pkt_info_mask_e
{
    CTC_PUMP_PKT_MASK_PROTO  = 0x1,
    CTC_PUMP_PKT_MASK_MACDA  = 0x2,
    CTC_PUMP_PKT_MASK_MACSA  = 0x4,
    CTC_PUMP_PKT_MASK_IPDA   = 0x8,
    CTC_PUMP_PKT_MASK_IPSA   = 0x10,
    CTC_PUMP_PKT_MASK_IP6DA  = 0x20,
    CTC_PUMP_PKT_MASK_IP6SA  = 0x40,
    CTC_PUMP_PKT_MASK_SRCPORT  = 0x80,
    CTC_PUMP_PKT_MASK_DSTPORT  = 0x100
};

typedef struct ctc_pump_pkt_info_s
{
    uint16 protocol;
    mac_addr_t mac_da;
    mac_addr_t mac_sa;
    union
    {
        ip_addr_t  v4;
        ipv6_addr_t  v6;
    }ip_da;
    union
    {
        ip_addr_t  v4;
        ipv6_addr_t  v6;
    }ip_sa;
    uint16 src_port;
    uint16 dst_port;
    uint32 masks; /*CTC_PUMP_PKT_MASK_XXX*/
}ctc_pump_pkt_info_t;

typedef struct ctc_pump_drop_info_s
{
    char   reason[128];
    uint32 port;
    ctc_pump_pkt_info_t info;
    uint32 pkt_id;
    uint32 pkt_len;
    uint8* pkt_buf;
}ctc_pump_drop_info_t;

typedef struct ctc_pump_efd_info_s
{
    uint32 port;
    ctc_pump_pkt_info_t info;
    uint32 pkt_len;
    uint8* pkt_buf;
}ctc_pump_efd_info_t;


typedef struct ctc_pump_func_data_s
{
    uint32 type;
    uint32 switch_id;
    sal_time_t timestamp;
    uint32 count;
    void*  data;
}ctc_pump_func_data_t;
typedef int32 (*ctc_pump_push_cb)(ctc_pump_func_data_t* data);

#define CTC_PUMP_FUNCTION_BMP_LEN ((CTC_PUMP_FUNC_MAX + 31) / 32)

typedef struct ctc_pump_config_s
{
    uint32 func_bmp[CTC_PUMP_FUNCTION_BMP_LEN]; /* bitmap refer to ctc_pump_func_type_t */
    ctc_pump_push_cb push_func;
    uint32 interval[CTC_PUMP_FUNC_MAX];
}ctc_pump_config_t;

typedef void (*ctc_pump_func)(uint8 lchip);

extern int32
ctc_pump_init(uint8 lchip, ctc_pump_config_t* cfg);

extern int32
ctc_pump_deinit(uint8 lchip);

extern bool
ctc_pump_func_is_enable(uint8 lchip, ctc_pump_func_type_t func_type);

extern ctc_pump_push_cb
ctc_pump_push_get_cb(uint8 lchip);

extern uint32
ctc_pump_func_interval(uint8 lchip, ctc_pump_func_type_t func_type);

#ifdef __cplusplus
}
#endif

#endif


