/**
 @file sys_goldengate_stats.c

 @date 2009-12-22

 @version v2.0

*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_error.h"
#include "ctc_macro.h"
#include "ctc_stats.h"
#include "ctc_hash.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_datapath.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_l3if.h"
#include "sys_goldengate_dma.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_wb_common.h"
#include "goldengate/include/drv_lib.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
#define ____________MAC_STATS____________

#define SYS_STATS_MTU_PKT_MIN_LENGTH 1024
#define SYS_STATS_MTU_PKT_MAX_LENGTH 16383
#define SYS_STATS_MAC_BASED_STATS_XQMAC_RAM_DEPTH     144u
#define SYS_STATS_XQMAC_PORT_MAX               4u
#define SYS_STATS_XQMAC_RAM_MAX               24u
#define SYS_STATS_CGMAC_RAM_MAX                4u
#define SYS_STATS_MTU1_PKT_DFT_LENGTH       1518u
#define SYS_STATS_MTU2_PKT_DFT_LENGTH       1536u

#define SYS_STATS_ENQUEUE_STATS_SIZE    2048
#define SYS_STATS_DEQUEUE_STATS_SIZE    2048
#define SYS_STATS_POLICER_STATS_SIZE    4096
#define SYS_STATS_IPE_IF_STATS_SIZE     8192
#define SYS_STATS_IPE_FWD_STATS_SIZE    8192
#define SYS_STATS_ACL0_STATS_SIZE       4096
#define SYS_STATS_ACL1_STATS_SIZE       512
#define SYS_STATS_ACL2_STATS_SIZE       256
#define SYS_STATS_ACL3_STATS_SIZE       256
#define SYS_STATS_EGS_ACL0_STATS_SIZE   1024

#define SYS_STATS_IS_XQMAC_STATS(mac_ram_type) \
          ((SYS_STATS_XQMAC_STATS_RAM0  == mac_ram_type)  || (SYS_STATS_XQMAC_STATS_RAM1  == mac_ram_type)\
        || (SYS_STATS_XQMAC_STATS_RAM2  == mac_ram_type)  || (SYS_STATS_XQMAC_STATS_RAM3  == mac_ram_type)\
        || (SYS_STATS_XQMAC_STATS_RAM4  == mac_ram_type)  || (SYS_STATS_XQMAC_STATS_RAM5  == mac_ram_type)\
        || (SYS_STATS_XQMAC_STATS_RAM6  == mac_ram_type)  || (SYS_STATS_XQMAC_STATS_RAM7  == mac_ram_type)\
        || (SYS_STATS_XQMAC_STATS_RAM8  == mac_ram_type)  || (SYS_STATS_XQMAC_STATS_RAM9  == mac_ram_type)\
        || (SYS_STATS_XQMAC_STATS_RAM10 == mac_ram_type)  || (SYS_STATS_XQMAC_STATS_RAM11 == mac_ram_type)\
        || (SYS_STATS_XQMAC_STATS_RAM12 == mac_ram_type)  || (SYS_STATS_XQMAC_STATS_RAM13 == mac_ram_type)\
        || (SYS_STATS_XQMAC_STATS_RAM14 == mac_ram_type)  || (SYS_STATS_XQMAC_STATS_RAM15 == mac_ram_type)\
        || (SYS_STATS_XQMAC_STATS_RAM16 == mac_ram_type)  || (SYS_STATS_XQMAC_STATS_RAM17 == mac_ram_type)\
        || (SYS_STATS_XQMAC_STATS_RAM18 == mac_ram_type)  || (SYS_STATS_XQMAC_STATS_RAM19 == mac_ram_type)\
        || (SYS_STATS_XQMAC_STATS_RAM20 == mac_ram_type)  || (SYS_STATS_XQMAC_STATS_RAM21 == mac_ram_type)\
        || (SYS_STATS_XQMAC_STATS_RAM22 == mac_ram_type)  || (SYS_STATS_XQMAC_STATS_RAM23 == mac_ram_type))

#define SYS_STATS_IS_CGMAC_STATS(mac_ram_type)\
          ((SYS_STATS_CGMAC_STATS_RAM0 == mac_ram_type) || (SYS_STATS_CGMAC_STATS_RAM1 == mac_ram_type)\
        || (SYS_STATS_CGMAC_STATS_RAM2 == mac_ram_type) || (SYS_STATS_CGMAC_STATS_RAM3 == mac_ram_type))

#define SYS_STATS_TYPE_CHECK(type) \
    if ((type < CTC_STATS_TYPE_FWD) \
        || (type >= CTC_STATS_TYPE_MAX)) \
        return CTC_E_FEATURE_NOT_SUPPORT

enum sys_mac_stats_property_e
{
    SYS_MAC_STATS_PROPERTY_CLEAR,
    SYS_MAC_STATS_PROPERTY_HOLD,
    SYS_MAC_STATS_PROPERTY_SATURATE,
    SYS_MAC_STATS_PROPERTY_MTU1,
    SYS_MAC_STATS_PROPERTY_MTU2,
    SYS_MAC_STATS_PROPERTY_NUM
};
typedef enum sys_mac_stats_property_e sys_mac_stats_property_t;

enum sys_stats_mac_ram_e
{
    SYS_STATS_XQMAC_STATS_RAM0,
    SYS_STATS_XQMAC_STATS_RAM1,
    SYS_STATS_XQMAC_STATS_RAM2,
    SYS_STATS_XQMAC_STATS_RAM3,
    SYS_STATS_XQMAC_STATS_RAM4,
    SYS_STATS_XQMAC_STATS_RAM5,
    SYS_STATS_XQMAC_STATS_RAM6,
    SYS_STATS_XQMAC_STATS_RAM7,
    SYS_STATS_XQMAC_STATS_RAM8,
    SYS_STATS_XQMAC_STATS_RAM9,
    SYS_STATS_XQMAC_STATS_RAM10,
    SYS_STATS_XQMAC_STATS_RAM11,
    SYS_STATS_XQMAC_STATS_RAM12,
    SYS_STATS_XQMAC_STATS_RAM13,
    SYS_STATS_XQMAC_STATS_RAM14,
    SYS_STATS_XQMAC_STATS_RAM15,
    SYS_STATS_XQMAC_STATS_RAM16,
    SYS_STATS_XQMAC_STATS_RAM17,
    SYS_STATS_XQMAC_STATS_RAM18,
    SYS_STATS_XQMAC_STATS_RAM19,
    SYS_STATS_XQMAC_STATS_RAM20,
    SYS_STATS_XQMAC_STATS_RAM21,
    SYS_STATS_XQMAC_STATS_RAM22,
    SYS_STATS_XQMAC_STATS_RAM23,

    SYS_STATS_CGMAC_STATS_RAM0,
    SYS_STATS_CGMAC_STATS_RAM1,
    SYS_STATS_CGMAC_STATS_RAM2,
    SYS_STATS_CGMAC_STATS_RAM3,

    SYS_STATS_MAC_STATS_RAM_MAX
};
typedef enum sys_stats_mac_ram_e sys_stats_mac_ram_t;

/*mac statistics type*/
enum sys_stats_mac_rec_stats_type_e
{
    SYS_STATS_MAC_RCV_GOOD_UCAST,
    SYS_STATS_MAC_RCV_GOOD_MCAST,
    SYS_STATS_MAC_RCV_GOOD_BCAST,
    SYS_STATS_MAC_RCV_GOOD_NORMAL_PAUSE,
    SYS_STATS_MAC_RCV_GOOD_PFC_PAUSE,
    SYS_STATS_MAC_RCV_GOOD_CONTROL,
    SYS_STATS_MAC_RCV_FCS_ERROR,
    SYS_STATS_MAC_RCV_MAC_OVERRUN,
    SYS_STATS_MAC_RCV_GOOD_63B,
    SYS_STATS_MAC_RCV_BAD_63B,
    SYS_STATS_MAC_RCV_GOOD_1519B,
    SYS_STATS_MAC_RCV_BAD_1519B,
    SYS_STATS_MAC_RCV_GOOD_JUMBO,
    SYS_STATS_MAC_RCV_BAD_JUMBO,
    SYS_STATS_MAC_RCV_64B,
    SYS_STATS_MAC_RCV_127B,
    SYS_STATS_MAC_RCV_255B,
    SYS_STATS_MAC_RCV_511B,
    SYS_STATS_MAC_RCV_1023B,
    SYS_STATS_MAC_RCV_1518B,

    SYS_STATS_MAC_RCV_MAX,
    SYS_STATS_MAC_RCV_NUM = SYS_STATS_MAC_RCV_MAX
};
typedef enum sys_stats_mac_rec_stats_type_e sys_stats_mac_rec_stats_type_t;

enum sys_stats_mac_snd_stats_type_e
{
    SYS_STATS_MAC_SEND_UCAST = 0x14,
    SYS_STATS_MAC_SEND_MCAST,
    SYS_STATS_MAC_SEND_BCAST,
    SYS_STATS_MAC_SEND_PAUSE,
    SYS_STATS_MAC_SEND_CONTROL,
    SYS_STATS_MAC_SEND_FCS_ERROR,
    SYS_STATS_MAC_SEND_MAC_UNDERRUN,
    SYS_STATS_MAC_SEND_63B,
    SYS_STATS_MAC_SEND_64B,
    SYS_STATS_MAC_SEND_127B,
    SYS_STATS_MAC_SEND_255B,
    SYS_STATS_MAC_SEND_511B,
    SYS_STATS_MAC_SEND_1023B,
    SYS_STATS_MAC_SEND_1518B,
    SYS_STATS_MAC_SEND_1519B,
    SYS_STATS_MAC_SEND_JUMBO,

    SYS_STATS_MAC_SEND_MAX,
    SYS_STATS_MAC_STATS_TYPE_NUM = SYS_STATS_MAC_SEND_MAX,
    SYS_STATS_MAC_SEND_NUM = 16
};
typedef enum sys_stats_mac_snd_stats_type_e sys_stats_mac_snd_stats_type_t;

/* the value defined should be consistent with ctc_stats.h */
enum sys_stats_map_mac_rx_type_e
{
    SYS_STATS_MAP_MAC_RX_GOOD_UC_PKT = 0,
    SYS_STATS_MAP_MAC_RX_GOOD_UC_BYTE = 1,
    SYS_STATS_MAP_MAC_RX_GOOD_MC_PKT = 2,
    SYS_STATS_MAP_MAC_RX_GOOD_MC_BYTE = 3,
    SYS_STATS_MAP_MAC_RX_GOOD_BC_PKT = 4,
    SYS_STATS_MAP_MAC_RX_GOOD_BC_BYTE = 5,
    SYS_STATS_MAP_MAC_RX_GOOD_NORMAL_PAUSE_PKT = 8,
    SYS_STATS_MAP_MAC_RX_GOOD_NORMAL_PAUSE_BYTE = 9,
    SYS_STATS_MAP_MAC_RX_GOOD_PFC_PAUSE_PKT = 10,
    SYS_STATS_MAP_MAC_RX_GOOD_PFC_PAUSE_BYTE = 11,
    SYS_STATS_MAP_MAC_RX_GOOD_CONTROL_PKT = 12,
    SYS_STATS_MAP_MAC_RX_GOOD_CONTROL_BYTE = 13,
    SYS_STATS_MAP_MAC_RX_FCS_ERROR_PKT = 14,
    SYS_STATS_MAP_MAC_RX_FCS_ERROR_BYTE = 15,
    SYS_STATS_MAP_MAC_RX_MAC_OVERRUN_PKT = 16,
    SYS_STATS_MAP_MAC_RX_MAC_OVERRUN_BYTE = 17,
    SYS_STATS_MAP_MAC_RX_GOOD_63B_PKT = 22,
    SYS_STATS_MAP_MAC_RX_GOOD_63B_BYTE = 23,
    SYS_STATS_MAP_MAC_RX_BAD_63B_PKT = 24,
    SYS_STATS_MAP_MAC_RX_BAD_63B_BYTE = 25,
    SYS_STATS_MAP_MAC_RX_GOOD_1519B_PKT = 26,
    SYS_STATS_MAP_MAC_RX_GOOD_1519B_BYTE = 27,
    SYS_STATS_MAP_MAC_RX_BAD_1519B_PKT = 28,
    SYS_STATS_MAP_MAC_RX_BAD_1519B_BYTE = 29,
    SYS_STATS_MAP_MAC_RX_GOOD_JUMBO_PKT = 30,
    SYS_STATS_MAP_MAC_RX_GOOD_JUMBO_BYTE = 31,
    SYS_STATS_MAP_MAC_RX_BAD_JUMBO_PKT = 32,
    SYS_STATS_MAP_MAC_RX_BAD_JUMBO_BYTE = 33,
    SYS_STATS_MAP_MAC_RX_64B_PKT = 34,
    SYS_STATS_MAP_MAC_RX_64B_BYTE = 35,
    SYS_STATS_MAP_MAC_RX_127B_PKT = 36,
    SYS_STATS_MAP_MAC_RX_127B_BYTE = 37,
    SYS_STATS_MAP_MAC_RX_255B_PKT = 38,
    SYS_STATS_MAP_MAC_RX_255B_BYTE = 39,
    SYS_STATS_MAP_MAC_RX_511B_PKT = 40,
    SYS_STATS_MAP_MAC_RX_511B_BYTE = 41,
    SYS_STATS_MAP_MAC_RX_1023B_PKT = 42,
    SYS_STATS_MAP_MAC_RX_1023B_BYTE = 43,
    SYS_STATS_MAP_MAC_RX_1518B_PKT = 44,
    SYS_STATS_MAP_MAC_RX_1518B_BYTE = 45,

    SYS_STATS_MAP_MAC_RX_TYPE_NUM = 46
};
typedef enum sys_stats_map_mac_rx_type_e sys_stats_map_mac_rx_type_t;

/* the value defined should be consistent with ctc_stats.h */
enum sys_stats_map_mac_tx_type_e
{
    SYS_STATS_MAP_MAC_TX_GOOD_UC_PKT = 0,
    SYS_STATS_MAP_MAC_TX_GOOD_UC_BYTE = 1,
    SYS_STATS_MAP_MAC_TX_GOOD_MC_PKT = 2,
    SYS_STATS_MAP_MAC_TX_GOOD_MC_BYTE = 3,
    SYS_STATS_MAP_MAC_TX_GOOD_BC_PKT = 4,
    SYS_STATS_MAP_MAC_TX_GOOD_BC_BYTE = 5,
    SYS_STATS_MAP_MAC_TX_GOOD_PAUSE_PKT = 6,
    SYS_STATS_MAP_MAC_TX_GOOD_PAUSE_BYTE = 7,
    SYS_STATS_MAP_MAC_TX_GOOD_CONTROL_PKT = 8,
    SYS_STATS_MAP_MAC_TX_GOOD_CONTROL_BYTE = 9,
    SYS_STATS_MAP_MAC_TX_FCS_ERROR_PKT = 30,
    SYS_STATS_MAP_MAC_TX_FCS_ERROR_BYTE = 31,
    SYS_STATS_MAP_MAC_TX_MAC_UNDERRUN_PKT = 28,
    SYS_STATS_MAP_MAC_TX_MAC_UNDERRUN_BYTE = 29,
    SYS_STATS_MAP_MAC_TX_63B_PKT = 10,
    SYS_STATS_MAP_MAC_TX_63B_BYTE = 11,
    SYS_STATS_MAP_MAC_TX_64B_PKT = 12,
    SYS_STATS_MAP_MAC_TX_64B_BYTE = 13,
    SYS_STATS_MAP_MAC_TX_127B_PKT = 14,
    SYS_STATS_MAP_MAC_TX_127B_BYTE = 15,
    SYS_STATS_MAP_MAC_TX_225B_PKT = 16,
    SYS_STATS_MAP_MAC_TX_225B_BYTE = 17,
    SYS_STATS_MAP_MAC_TX_511B_PKT = 18,
    SYS_STATS_MAP_MAC_TX_511B_BYTE = 19,
    SYS_STATS_MAP_MAC_TX_1023B_PKT = 20,
    SYS_STATS_MAP_MAC_TX_1023B_BYTE = 21,
    SYS_STATS_MAP_MAC_TX_1518B_PKT = 22,
    SYS_STATS_MAP_MAC_TX_1518B_BYTE = 23,
    SYS_STATS_MAP_MAC_TX_1519B_PKT = 24,
    SYS_STATS_MAP_MAC_TX_1519B_BYTE = 25,
    SYS_STATS_MAP_MAC_TX_JUMBO_PKT = 26,
    SYS_STATS_MAP_MAC_TX_JUMBO_BYTE = 27,

    SYS_STATS_MAP_MAC_TX_TYPE_NUM
};
typedef enum sys_stats_map_mac_tx_type_e sys_stats_map_mac_tx_type_t;

enum sys_stats_mac_cnt_type_e
{
    SYS_STATS_MAC_CNT_TYPE_PKT,
    SYS_STATS_MAC_CNT_TYPE_BYTE,
    SYS_STATS_MAC_CNT_TYPE_NUM
};
typedef enum sys_stats_mac_cnt_type_e sys_stats_mac_cnt_type_t;

union sys_macstats_u
{
    XqmacStatsRam0_m xgmac_stats;
    CgmacStatsRam0_m cgmac_stats;
};
typedef union sys_macstats_u sys_macstats_t;

struct sys_mac_stats_rx_s
{
    uint64 mac_stats_rx_bytes[SYS_STATS_MAC_RCV_MAX];
    uint64 mac_stats_rx_pkts[SYS_STATS_MAC_RCV_MAX];
};
typedef  struct sys_mac_stats_rx_s sys_mac_stats_rx_t;

struct sys_mac_stats_tx_s
{
    uint64 mac_stats_tx_bytes[SYS_STATS_MAC_SEND_MAX];
    uint64 mac_stats_tx_pkts[SYS_STATS_MAC_SEND_MAX];
};
typedef struct sys_mac_stats_tx_s sys_mac_stats_tx_t;

struct sys_xqmac_stats_s
{
    sys_mac_stats_rx_t mac_stats_rx[SYS_STATS_XQMAC_PORT_MAX];
    sys_mac_stats_tx_t mac_stats_tx[SYS_STATS_XQMAC_PORT_MAX];
};
typedef struct sys_xqmac_stats_s sys_xqmac_stats_t;

struct sys_cgmac_stats_s
{
    sys_mac_stats_rx_t mac_stats_rx;
    sys_mac_stats_tx_t mac_stats_tx;
};
typedef struct sys_cgmac_stats_s sys_cgmac_stats_t;

#define ___________FLOW_STATS____________

#define SYS_STATS_DEFAULT_FIFO_DEPTH_THRESHOLD      15
#define SYS_STATS_DEFAULT_BYTE_THRESHOLD 0x1ff
#define SYS_STATS_DEFAULT_PACKET_THRESHOLD          0x1ff
#define SYS_STATS_DEFAULT_BYTE_THRESHOLD_HI 0x2ff
#define SYS_STATS_DEFAULT_PACKET_THRESHOLD_HI          0x2ff

#define SYS_STATS_MAX_FIFO_DEPTH                    32
#define SYS_STATS_MAX_BYTE_CNT                      0x2ff
#define SYS_STATS_MAX_PKT_CNT                       0x2ff
#define SYS_STATS_ECMP_RESERVE_SIZE                 7168   /* from 7168-8191, total 1K */
#define SYS_STATS_FLOW_ENTRY_HASH_BLOCK_SIZE        256


/* Define no cache mode opf index type */
enum sys_stats_no_cache_mode_opf_type_e
{
    SYS_STATS_NO_CACHE_MODE_13BIT_IPE_IF_EPE_FWD_POLICER,
    SYS_STATS_NO_CACHE_MODE_14BIT_IPE_IF_EPE_FWD_POLICER,
    SYS_STATS_NO_CACHE_MODE_13BIT_EPE_IF_IPE_FWD_ACL,
    SYS_STATS_NO_CACHE_MODE_14BIT_EPE_IF_ACL,   /* ipe fwd no 14 bit */

    SYS_STATS_NO_CACHE_MODE_MAX
};
typedef enum sys_stats_no_cache_mode_opf_type_e sys_stats_no_cache_mode_opf_type_t;

#define ___________FLOW_STATS_SRTUCT____________

struct sys_stats_fwd_stats_s
{
    uint16 stats_ptr;
    uint64 packet_count;
    uint64 byte_count;
};
typedef struct sys_stats_fwd_stats_s sys_stats_fwd_stats_t;

struct sys_stats_mac_throughput_s
{
    uint64 bytes[SYS_GG_MAX_PORT_NUM_PER_CHIP][CTC_STATS_MAC_STATS_MAX];
    sal_systime_t  timestamp[SYS_GG_MAX_PORT_NUM_PER_CHIP];
};
typedef struct sys_stats_mac_throughput_s sys_stats_mac_throughput_t;

#define ___________STATS_COMMON___________

#define SYS_STATS_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == gg_stats_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define STATS_LOCK   \
    if (gg_stats_master[lchip]->p_stats_mutex) sal_mutex_lock(gg_stats_master[lchip]->p_stats_mutex)

#define STATS_UNLOCK \
    if (gg_stats_master[lchip]->p_stats_mutex) sal_mutex_unlock(gg_stats_master[lchip]->p_stats_mutex)

#define CTC_ERROR_RETURN_WITH_STATS_UNLOCK(op) \
    do { \
        int32 rv; \
        if ((rv = (op)) < 0) \
        { \
            sal_mutex_unlock(gg_stats_master[lchip]->p_stats_mutex); \
            return (rv); \
        } \
    } while (0)


#define SYS_STATS_DBG_OUT(level, FMT, ...)                              \
    {                                                                       \
        CTC_DEBUG_OUT(stats, stats, STATS_SYS, level, FMT, ##__VA_ARGS__);  \
    }

#define SYS_STATS_DBG_DUMP(FMT, ...)               \
    {                                                 \
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__);\
    }

#define SYS_STATS_DBG_FUNC()                          \
    { \
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n",__FUNCTION__);\
    }

#define SYS_STATS_DBG_ERROR(FMT, ...) \
    { \
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, FMT, ##__VA_ARGS__);\
    }

#define SYS_STATS_DBG_INFO(FMT, ...) \
    { \
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ##__VA_ARGS__);\
    }
struct sys_stats_master_s
{
    sal_mutex_t* p_stats_mutex;

    uint32 stats_bitmap;
    uint8  stats_mode;
    uint8  query_mode;

    uint32 policer_stats_base;

    uint16 dequeue_stats_base;
    uint16 enqueue_stats_base;

    /* with cache base */
    uint16 ipe_if_epe_fwd_base;
    uint16 epe_if_ipe_fwd_base;
    uint16 ipe_acl0_base;
    uint16 ipe_acl1_base;
    uint16 ipe_acl2_base;
    uint16 ipe_acl3_base;
    uint16 epe_acl0_base;

    /* no cache base */
    uint16 len_13bit_ipe_if_base;   /* 13bit ipe intf stats & epe fwd stats & policer */
    uint16 len_14bit_ipe_if_base;   /* 14bit ipe intf stats & epe fwd stats & policer */
    uint16 len_13bit_epe_if_base;   /* 13bit ipe fwd stats & epe intf stats & acl stats */
    uint16 len_14bit_epe_if_base;   /* 14bit epe intf stats & acl stats */

    uint8 saturate_en[CTC_STATS_TYPE_MAX];
    uint8 hold_en[CTC_STATS_TYPE_MAX];
    uint8 clear_read_en[CTC_STATS_TYPE_MAX];
    uint8 cache_mode;           /* 0 -- use cache, 1 -- no cache */

    uint8 stats_stored_in_sw;   /* only for mac stats */
    uint8 fifo_depth_threshold;
    uint16 pkt_cnt_threshold;
    uint16 byte_cnt_threshold;

    void* userdata;

    sys_xqmac_stats_t xqmac_stats_table[SYS_STATS_XQMAC_RAM_MAX];
    sys_cgmac_stats_t cgmac_stats_table[SYS_STATS_CGMAC_RAM_MAX];
    ctc_stats_sync_fn_t dam_sync_mac_stats;

    uint16 used_count[SYS_STATS_CACHE_MODE_MAX];
#define CTC_STATS_TYPE_USED_COUNT_MAX           (CTC_STATS_STATSID_TYPE_MAX + 5)
    uint16 type_used_count[CTC_STATS_TYPE_USED_COUNT_MAX];   /* +4 for l3if egress, fid egress, mpls vc label, tunnel egress */

    ctc_hash_t* flow_stats_hash;
    ctc_hash_t* stats_statsid_hash;


    ctc_vector_t *port_log_vec;

};
typedef struct sys_stats_master_s sys_stats_master_t;

sys_stats_master_t* gg_stats_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

static sys_stats_mac_throughput_t mac_throughput[CTC_MAX_LOCAL_CHIP_NUM];

/****************************************************************************
 *
* Function
*
*****************************************************************************/
#define ___________MAC_STATS_FUNCTION________________________

STATIC int32
_sys_goldengate_stats_get_sys_stats_info(uint8 lchip, sys_stats_statsid_t* sw_stats_statsid, uint8* pool_index, uint8* dir, uint32* stats_base);

STATIC int32
_sys_goldengate_stats_get_stats_ctl(uint8 lchip, uint16 lport, uint32* p_tbl_id)
{
    int8 tbl_step = 0;
    sys_datapath_lport_attr_t* p_port_cap = NULL;
    uint8 block_idx = 0;

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport), &p_port_cap));
    if (p_port_cap->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    sys_goldengate_common_get_block_mac_index(lchip, p_port_cap->slice_id, p_port_cap->mac_id, &block_idx);

    if (CTC_PORT_SPEED_10G >= p_port_cap->speed_mode)
    {
        tbl_step = XqmacStatsCfg1_t - XqmacStatsCfg0_t;
        *p_tbl_id = XqmacStatsCfg0_t + block_idx * tbl_step;
    }
    else if ((CTC_PORT_SPEED_40G == p_port_cap->speed_mode)||(CTC_PORT_SPEED_20G == p_port_cap->speed_mode))
    {
        tbl_step = XqmacStatsCfg1_t - XqmacStatsCfg0_t;
        *p_tbl_id = XqmacStatsCfg0_t + block_idx * tbl_step;
    }
    else if (CTC_PORT_SPEED_100G == p_port_cap->speed_mode)
    {
        tbl_step = CgmacStatsCfg1_t - CgmacStatsCfg0_t;
        *p_tbl_id = CgmacStatsCfg0_t + block_idx * tbl_step;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_get_stats_field(uint8 lchip, uint16 lport, sys_mac_stats_property_t property, uint32* p_filed_id)
{
#define MAC_STATS_PROPERTY(type, property) (type << 4 | property)
    uint32 mac_type = 0;
    uint8 mac_id = 0;
    sys_datapath_lport_attr_t* p_port = NULL;

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport), &p_port));
    if (p_port->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    mac_id = p_port->mac_id+64*p_port->slice_id;
    mac_type = (sys_goldengate_common_get_mac_type(lchip, mac_id));
    if (0xff == mac_type)
    {
        return CTC_E_MAC_NOT_USED;
    }


    switch (MAC_STATS_PROPERTY(mac_type, property))
    {
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_1G, SYS_MAC_STATS_PROPERTY_CLEAR):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_10M, SYS_MAC_STATS_PROPERTY_CLEAR):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_100M, SYS_MAC_STATS_PROPERTY_CLEAR):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_2G5, SYS_MAC_STATS_PROPERTY_CLEAR):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_10G, SYS_MAC_STATS_PROPERTY_CLEAR):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_40G, SYS_MAC_STATS_PROPERTY_CLEAR):
        *p_filed_id = XqmacStatsCfg0_clearOnRead_f;
        break;

    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_100G, SYS_MAC_STATS_PROPERTY_CLEAR):
        *p_filed_id = CgmacStatsCfg0_clearOnRead_f;
        break;

    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_10M, SYS_MAC_STATS_PROPERTY_HOLD):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_100M, SYS_MAC_STATS_PROPERTY_HOLD):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_2G5, SYS_MAC_STATS_PROPERTY_HOLD):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_1G, SYS_MAC_STATS_PROPERTY_HOLD):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_10G, SYS_MAC_STATS_PROPERTY_HOLD):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_40G, SYS_MAC_STATS_PROPERTY_HOLD):
        *p_filed_id = XqmacStatsCfg0_incrHold_f;
        break;

    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_100G, SYS_MAC_STATS_PROPERTY_HOLD):
        *p_filed_id = CgmacStatsCfg0_incrHold_f;
        break;

    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_10M, SYS_MAC_STATS_PROPERTY_SATURATE):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_100M, SYS_MAC_STATS_PROPERTY_SATURATE):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_2G5, SYS_MAC_STATS_PROPERTY_SATURATE):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_1G, SYS_MAC_STATS_PROPERTY_SATURATE):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_10G, SYS_MAC_STATS_PROPERTY_SATURATE):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_40G, SYS_MAC_STATS_PROPERTY_SATURATE):
        *p_filed_id = XqmacStatsCfg0_incrSaturate_f;
        break;

    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_100G, SYS_MAC_STATS_PROPERTY_SATURATE):
        *p_filed_id = CgmacStatsCfg0_incrSaturate_f;
        break;

    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_10M, SYS_MAC_STATS_PROPERTY_MTU1):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_100M, SYS_MAC_STATS_PROPERTY_MTU1):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_2G5, SYS_MAC_STATS_PROPERTY_MTU1):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_1G, SYS_MAC_STATS_PROPERTY_MTU1):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_10G, SYS_MAC_STATS_PROPERTY_MTU1):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_40G, SYS_MAC_STATS_PROPERTY_MTU1):
        *p_filed_id = XqmacStatsCfg0_packetLenMtu1_f;
        break;

    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_100G, SYS_MAC_STATS_PROPERTY_MTU1):
        *p_filed_id = CgmacStatsCfg0_packetLenMtu1_f;
        break;

    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_10M, SYS_MAC_STATS_PROPERTY_MTU2):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_100M, SYS_MAC_STATS_PROPERTY_MTU2):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_2G5, SYS_MAC_STATS_PROPERTY_MTU2):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_1G, SYS_MAC_STATS_PROPERTY_MTU2):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_10G, SYS_MAC_STATS_PROPERTY_MTU2):
    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_40G, SYS_MAC_STATS_PROPERTY_MTU2):
        *p_filed_id = XqmacStatsCfg0_packetLenMtu2_f;
        break;

    case MAC_STATS_PROPERTY(CTC_PORT_SPEED_100G, SYS_MAC_STATS_PROPERTY_MTU2):
        *p_filed_id = CgmacStatsCfg0_packetLenMtu2_f;
        break;

    default:
        return CTC_E_INVALID_PARAM;

    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_set_stats_property(uint8 lchip, uint16 gport, sys_mac_stats_property_t property, uint32 value)
{

    uint16 lport = 0;
    uint32 tbl_id = 0;
    uint32 field_id = 0;
    uint32 cmd = 0;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_ERROR_RETURN(_sys_goldengate_stats_get_stats_ctl(lchip, lport, &tbl_id));
    CTC_ERROR_RETURN(_sys_goldengate_stats_get_stats_field(lchip, lport, property, &field_id));

    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_get_stats_property(uint8 lchip, uint16 gport, sys_mac_stats_property_t property, uint32* p_value)
{

    uint16 lport = 0;
    uint32 tbl_id = 0;
    uint32 field_id = 0;
    uint32 cmd = 0;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_ERROR_RETURN(_sys_goldengate_stats_get_stats_ctl(lchip, lport, &tbl_id));
    CTC_ERROR_RETURN(_sys_goldengate_stats_get_stats_field(lchip, lport, property, &field_id));

    cmd = DRV_IOR(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, p_value));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_get_mac_ram_type(uint8 lchip, uint16 gport, uint8* p_ram_type, uint8* p_port_type)
{

    uint16 lport = 0;
    sys_datapath_lport_attr_t* p_port_cap = NULL;
    uint16 mac_id = 0;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, CTC_MAP_GPORT_TO_LPORT(gport), &p_port_cap));
    *p_port_type = p_port_cap->speed_mode;
    if (p_port_cap->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    if(p_port_cap->mac_id > 39)
    {
        mac_id = p_port_cap->mac_id - 8;
    }
    else
    {
        mac_id = p_port_cap->mac_id;
    }

    if ((CTC_PORT_SPEED_10M == p_port_cap->speed_mode) || (CTC_PORT_SPEED_100M == p_port_cap->speed_mode)
       || (CTC_PORT_SPEED_1G == p_port_cap->speed_mode) || (CTC_PORT_SPEED_2G5 == p_port_cap->speed_mode)
       || (CTC_PORT_SPEED_10G == p_port_cap->speed_mode))
    {
        *p_ram_type = SYS_STATS_XQMAC_STATS_RAM0 + (p_port_cap->slice_id * SYS_XQMAC_PER_SLICE_NUM)\
                      + (mac_id / SYS_SUBMAC_PER_XQMAC_NUM);
    }
    else if ((CTC_PORT_SPEED_40G == p_port_cap->speed_mode)||(CTC_PORT_SPEED_20G == p_port_cap->speed_mode))
    {
        *p_ram_type = SYS_STATS_XQMAC_STATS_RAM0 + (p_port_cap->slice_id * SYS_XQMAC_PER_SLICE_NUM)\
                      + (mac_id / SYS_SUBMAC_PER_XQMAC_NUM);
    }
    else if (CTC_PORT_SPEED_100G == p_port_cap->speed_mode)
    {
        *p_ram_type = SYS_STATS_CGMAC_STATS_RAM0 + (p_port_cap->slice_id * SYS_CGMAC_PER_SLICE_NUM)\
                      + ((p_port_cap->mac_id == 36)?0:1);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_set_mac_packet_length_mtu1(uint8 lchip, uint16 gport, uint16 length)
{
    uint32 mtu2_len = 0;
    uint32 tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);
    tmp = length;

    CTC_MIN_VALUE_CHECK(tmp, SYS_STATS_MTU_PKT_MIN_LENGTH);
    CTC_MAX_VALUE_CHECK(tmp, SYS_STATS_MTU_PKT_MAX_LENGTH);

    CTC_ERROR_RETURN(_sys_goldengate_stats_get_stats_property(lchip, gport, SYS_MAC_STATS_PROPERTY_MTU2, &mtu2_len));
    if ((tmp >= mtu2_len) && (0 != mtu2_len))
    {
        return CTC_E_STATS_MTU1_GREATER_MTU2;
    }
    CTC_ERROR_RETURN(_sys_goldengate_stats_set_stats_property(lchip, gport, SYS_MAC_STATS_PROPERTY_MTU1, length));

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_get_mac_packet_length_mtu1(uint8 lchip, uint16 gport, uint16* p_length)
{
    uint32 value = 0;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_length);

    CTC_ERROR_RETURN(_sys_goldengate_stats_get_stats_property(lchip, gport, SYS_MAC_STATS_PROPERTY_MTU1, &value));
    *p_length = value;

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_set_mac_packet_length_mtu2(uint8 lchip, uint16 gport, uint16 length)
{
    uint32 mtu1_len = 0;
    uint32 tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);
    tmp = length;

    CTC_MIN_VALUE_CHECK(tmp, SYS_STATS_MTU_PKT_MIN_LENGTH);
    CTC_MAX_VALUE_CHECK(tmp, SYS_STATS_MTU_PKT_MAX_LENGTH);

    /*mtu2 length must greater than mtu1 length*/
    CTC_ERROR_RETURN(_sys_goldengate_stats_get_stats_property(lchip, gport, SYS_MAC_STATS_PROPERTY_MTU1, &mtu1_len));
    if (tmp <= mtu1_len)
    {
        return CTC_E_STATS_MTU2_LESS_MTU1;
    }

    CTC_MAX_VALUE_CHECK(tmp, SYS_STATS_MTU_PKT_MAX_LENGTH);
    CTC_ERROR_RETURN(_sys_goldengate_stats_set_stats_property(lchip, gport, SYS_MAC_STATS_PROPERTY_MTU2, length));

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_get_mac_packet_length_mtu2(uint8 lchip, uint16 gport, uint16* p_length)
{
    uint32 value = 0;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_length);

    CTC_ERROR_RETURN(_sys_goldengate_stats_get_stats_property(lchip, gport, SYS_MAC_STATS_PROPERTY_MTU2, &value));
    *p_length = value;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_mac_stats_to_basic(uint8 lchip, bool is_xqmac, void* p_stats_ram, ctc_stats_basic_t* p_basic_stats)
{
    uint64 tmp = 0;
    XqmacStatsRam0_m* p_xqmac_stats_ram = NULL;
    CgmacStatsRam0_m* p_cgmac_stats_ram = NULL;


    CTC_PTR_VALID_CHECK(p_basic_stats);

    /*judge xgmac or cgmac through mac ram type*/
    if (TRUE == is_xqmac)
    {
        p_xqmac_stats_ram = (XqmacStatsRam0_m*)p_stats_ram;

        tmp = GetXqmacStatsRam0(V, frameCntDataHi_f, p_xqmac_stats_ram);
        tmp <<= 32;
        tmp |= GetXqmacStatsRam0(V, frameCntDataLo_f, p_xqmac_stats_ram);

        p_basic_stats->packet_count = tmp;

        tmp = GetXqmacStatsRam0(V, byteCntDataHi_f, p_xqmac_stats_ram);
        tmp <<= 32;
        tmp |= GetXqmacStatsRam0(V, byteCntDataLo_f, p_xqmac_stats_ram);

        p_basic_stats->byte_count = tmp;
    }
    else
    {
        p_cgmac_stats_ram = (CgmacStatsRam0_m*)p_stats_ram;

        tmp = GetCgmacStatsRam0(V, frameCntDataHi_f, p_cgmac_stats_ram);
        tmp <<= 32;
        tmp |= GetCgmacStatsRam0(V, frameCntDataLo_f, p_cgmac_stats_ram);

        p_basic_stats->packet_count = tmp;

        tmp = GetCgmacStatsRam0(V, byteCntDataHi_f, p_cgmac_stats_ram);
        tmp <<= 32;
        tmp |= GetCgmacStatsRam0(V, byteCntDataLo_f, p_cgmac_stats_ram);

        p_basic_stats->byte_count = tmp;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_get_mac_stats_offset(uint8 lchip, uint8 sys_type, sys_stats_mac_cnt_type_t cnt_type, uint8* p_offset)
{
    /* The table mapper offset value against ctc_stats_mac_rec_t variable address based on stats_type and direction. */
    uint8 rx_mac_stats[SYS_STATS_MAC_RCV_NUM][SYS_STATS_MAC_CNT_TYPE_NUM] =
    {
        {SYS_STATS_MAP_MAC_RX_GOOD_UC_PKT,           SYS_STATS_MAP_MAC_RX_GOOD_UC_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_MC_PKT,           SYS_STATS_MAP_MAC_RX_GOOD_MC_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_BC_PKT,           SYS_STATS_MAP_MAC_RX_GOOD_BC_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_NORMAL_PAUSE_PKT, SYS_STATS_MAP_MAC_RX_GOOD_NORMAL_PAUSE_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_PFC_PAUSE_PKT,    SYS_STATS_MAP_MAC_RX_GOOD_PFC_PAUSE_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_CONTROL_PKT,      SYS_STATS_MAP_MAC_RX_GOOD_CONTROL_BYTE},
        {SYS_STATS_MAP_MAC_RX_FCS_ERROR_PKT,         SYS_STATS_MAP_MAC_RX_FCS_ERROR_BYTE},
        {SYS_STATS_MAP_MAC_RX_MAC_OVERRUN_PKT,       SYS_STATS_MAP_MAC_RX_MAC_OVERRUN_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_63B_PKT,          SYS_STATS_MAP_MAC_RX_GOOD_63B_BYTE},
        {SYS_STATS_MAP_MAC_RX_BAD_63B_PKT,           SYS_STATS_MAP_MAC_RX_BAD_63B_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_1519B_PKT,        SYS_STATS_MAP_MAC_RX_GOOD_1519B_BYTE},
        {SYS_STATS_MAP_MAC_RX_BAD_1519B_PKT,         SYS_STATS_MAP_MAC_RX_BAD_1519B_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_JUMBO_PKT,        SYS_STATS_MAP_MAC_RX_GOOD_JUMBO_BYTE},
        {SYS_STATS_MAP_MAC_RX_BAD_JUMBO_PKT,         SYS_STATS_MAP_MAC_RX_BAD_JUMBO_BYTE},
        {SYS_STATS_MAP_MAC_RX_64B_PKT,               SYS_STATS_MAP_MAC_RX_64B_BYTE},
        {SYS_STATS_MAP_MAC_RX_127B_PKT,              SYS_STATS_MAP_MAC_RX_127B_BYTE},
        {SYS_STATS_MAP_MAC_RX_255B_PKT,              SYS_STATS_MAP_MAC_RX_255B_BYTE},
        {SYS_STATS_MAP_MAC_RX_511B_PKT,              SYS_STATS_MAP_MAC_RX_511B_BYTE},
        {SYS_STATS_MAP_MAC_RX_1023B_PKT,             SYS_STATS_MAP_MAC_RX_1023B_BYTE},
        {SYS_STATS_MAP_MAC_RX_1518B_PKT,             SYS_STATS_MAP_MAC_RX_1518B_BYTE}
    };

    /* The table mapper offset value against ctc_stats_mac_snd_t variable address based on stats_type and direction. */
    uint8 tx_mac_stats[SYS_STATS_MAC_SEND_NUM][SYS_STATS_MAC_CNT_TYPE_NUM] =
    {
        {SYS_STATS_MAP_MAC_TX_GOOD_UC_PKT,      SYS_STATS_MAP_MAC_TX_GOOD_UC_BYTE},
        {SYS_STATS_MAP_MAC_TX_GOOD_MC_PKT,      SYS_STATS_MAP_MAC_TX_GOOD_MC_BYTE},
        {SYS_STATS_MAP_MAC_TX_GOOD_BC_PKT,      SYS_STATS_MAP_MAC_TX_GOOD_BC_BYTE},
        {SYS_STATS_MAP_MAC_TX_GOOD_PAUSE_PKT,   SYS_STATS_MAP_MAC_TX_GOOD_PAUSE_BYTE},
        {SYS_STATS_MAP_MAC_TX_GOOD_CONTROL_PKT, SYS_STATS_MAP_MAC_TX_GOOD_CONTROL_BYTE},
        {SYS_STATS_MAP_MAC_TX_FCS_ERROR_PKT,    SYS_STATS_MAP_MAC_TX_FCS_ERROR_BYTE},
        {SYS_STATS_MAP_MAC_TX_MAC_UNDERRUN_PKT, SYS_STATS_MAP_MAC_TX_MAC_UNDERRUN_BYTE},
        {SYS_STATS_MAP_MAC_TX_63B_PKT,          SYS_STATS_MAP_MAC_TX_63B_BYTE},
        {SYS_STATS_MAP_MAC_TX_64B_PKT,          SYS_STATS_MAP_MAC_TX_64B_BYTE},
        {SYS_STATS_MAP_MAC_TX_127B_PKT,         SYS_STATS_MAP_MAC_TX_127B_BYTE},
        {SYS_STATS_MAP_MAC_TX_225B_PKT,         SYS_STATS_MAP_MAC_TX_225B_BYTE},
        {SYS_STATS_MAP_MAC_TX_511B_PKT,         SYS_STATS_MAP_MAC_TX_511B_BYTE},
        {SYS_STATS_MAP_MAC_TX_1023B_PKT,        SYS_STATS_MAP_MAC_TX_1023B_BYTE},
        {SYS_STATS_MAP_MAC_TX_1518B_PKT,        SYS_STATS_MAP_MAC_TX_1518B_BYTE},
        {SYS_STATS_MAP_MAC_TX_1519B_PKT,        SYS_STATS_MAP_MAC_TX_1519B_BYTE},
        {SYS_STATS_MAP_MAC_TX_JUMBO_PKT,        SYS_STATS_MAP_MAC_TX_JUMBO_BYTE}
    };

    if (sys_type < SYS_STATS_MAC_RCV_MAX)
    {
        *p_offset = rx_mac_stats[sys_type][cnt_type];
    }
    else
    {
        *p_offset = tx_mac_stats[sys_type - SYS_STATS_MAC_SEND_UCAST][cnt_type];
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_get_mac_stats_tbl(uint8 lchip, uint8 mac_type, uint8 stats_type, uint16 gport, void** pp_mac_stats)
{
    if (SYS_STATS_IS_XQMAC_STATS(mac_type))
    {
        if (stats_type < SYS_STATS_MAC_RCV_MAX)
        {
            *pp_mac_stats = &gg_stats_master[lchip]->xqmac_stats_table[mac_type].mac_stats_rx[SYS_GET_MAC_ID(lchip, gport) % 4];
        }
        else
        {
            *pp_mac_stats = &gg_stats_master[lchip]->xqmac_stats_table[mac_type].mac_stats_tx[SYS_GET_MAC_ID(lchip, gport) % 4];
        }
    }
    else if (SYS_STATS_IS_CGMAC_STATS(mac_type))
    {
        if (stats_type < SYS_STATS_MAC_RCV_MAX)
        {
            *pp_mac_stats = &gg_stats_master[lchip]->cgmac_stats_table[mac_type - SYS_STATS_XQMAC_RAM_MAX].mac_stats_rx;
        }
        else
        {
            *pp_mac_stats = &gg_stats_master[lchip]->cgmac_stats_table[mac_type - SYS_STATS_XQMAC_RAM_MAX].mac_stats_tx;
        }
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_write_sys_mac_stats(uint8 lchip, uint8 stats_type, void* p_mac_stats, ctc_stats_basic_t* p_stats_basic)
{
    sys_mac_stats_rx_t* p_mac_stats_rx = NULL;
    sys_mac_stats_tx_t* p_mac_stats_tx = NULL;

    if (stats_type < SYS_STATS_MAC_RCV_MAX)
    {
        p_mac_stats_rx = (sys_mac_stats_rx_t*)p_mac_stats;

        p_mac_stats_rx->mac_stats_rx_pkts[stats_type] += p_stats_basic->packet_count;
        p_mac_stats_rx->mac_stats_rx_bytes[stats_type] += p_stats_basic->byte_count;
    }
    else
    {
        p_mac_stats_tx = (sys_mac_stats_tx_t*)p_mac_stats;
        p_mac_stats_tx->mac_stats_tx_pkts[stats_type - SYS_STATS_MAC_SEND_UCAST] += p_stats_basic->packet_count;
        p_mac_stats_tx->mac_stats_tx_bytes[stats_type - SYS_STATS_MAC_SEND_UCAST] += p_stats_basic->byte_count;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_write_ctc_mac_stats(uint8 lchip, uint8 sys_type, void* p_ctc_stats, ctc_stats_basic_t* p_stats_basic)
{
    uint8  offset = 0;
    uint8  cnt_type = 0;
    uint64* p_val[SYS_STATS_MAC_CNT_TYPE_NUM] = {&p_stats_basic->packet_count, &p_stats_basic->byte_count};

    for (cnt_type = 0; cnt_type < SYS_STATS_MAC_CNT_TYPE_NUM; cnt_type++)
    {
        _sys_goldengate_stats_get_mac_stats_offset(lchip, sys_type, cnt_type, &offset);
        ((uint64*)p_ctc_stats)[offset] = *(p_val[cnt_type]);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_sync_ctc_mac_stats(uint8 lchip, uint8 stats_type, void* p_ctc_stats, void* p_mac_stats)
{
    ctc_stats_basic_t stats_basic;

    sal_memset(&stats_basic, 0, sizeof(ctc_stats_basic_t));
    if (stats_type < SYS_STATS_MAC_RCV_MAX)
    {
        stats_basic.packet_count = ((sys_mac_stats_rx_t*)p_mac_stats)->mac_stats_rx_pkts[stats_type];
        stats_basic.byte_count = ((sys_mac_stats_rx_t*)p_mac_stats)->mac_stats_rx_bytes[stats_type];
    }
    else
    {
        stats_basic.packet_count = ((sys_mac_stats_tx_t*)p_mac_stats)->mac_stats_tx_pkts[stats_type - SYS_STATS_MAC_RCV_MAX];
        stats_basic.byte_count = ((sys_mac_stats_tx_t*)p_mac_stats)->mac_stats_tx_bytes[stats_type - SYS_STATS_MAC_RCV_MAX];
    }
    CTC_ERROR_RETURN(_sys_goldengate_stats_write_ctc_mac_stats(lchip, stats_type, p_ctc_stats, &stats_basic));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_get_stats_tbl_info(uint8 lchip, uint16 gport, uint32* p_tbl_id, uint8* p_tbl_base, uint8* p_mac_ram_type)
{
    int32  tbl_step = 0;
    uint8  port_type = 0;

    /*get mac index, channel of lport from function to justify mac ram*/
    CTC_ERROR_RETURN(_sys_goldengate_stats_get_mac_ram_type(lchip, gport, p_mac_ram_type, &port_type));
    if (SYS_STATS_IS_XQMAC_STATS(*p_mac_ram_type))
    {
        tbl_step = XqmacStatsRam1_t - XqmacStatsRam0_t;
        *p_tbl_id = XqmacStatsRam0_t + (*p_mac_ram_type - SYS_STATS_XQMAC_STATS_RAM0) * tbl_step;
    }
    else if (SYS_STATS_IS_CGMAC_STATS(*p_mac_ram_type))
    {
        tbl_step = CgmacStatsRam1_t - CgmacStatsRam0_t;
        *p_tbl_id = CgmacStatsRam0_t + (*p_mac_ram_type - SYS_STATS_CGMAC_STATS_RAM0) * tbl_step;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((CTC_PORT_SPEED_10M == port_type) || (CTC_PORT_SPEED_100M == port_type)
       || (CTC_PORT_SPEED_1G == port_type) || (CTC_PORT_SPEED_2G5 == port_type)
       || (CTC_PORT_SPEED_10G == port_type))
    {
        *p_tbl_base = (SYS_GET_MAC_ID(lchip, gport) & 0x3) * (SYS_STATS_MAC_BASED_STATS_XQMAC_RAM_DEPTH / 4);
    }
    else
    {
        *p_tbl_base = 0;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_get_mac_rx_stats(uint8 lchip, uint16 gport, ctc_mac_stats_t* p_stats)
{
    bool   is_xqmac = FALSE;
    void*  p_mac_stats = NULL;

    uint8  stats_type = 0;
    uint16 lport = 0;
    uint8  tbl_base = 0;
    uint32 cmdr = 0;
    uint32 cmdw = 0;
    uint32 tbl_id = 0;
    uint8  mac_ram_type = 0;
    sys_macstats_t mac_stats;
    ctc_stats_basic_t stats_basic;
    XqmacStatsRam0_m xqmac_stats_ram;
    drv_work_platform_type_t platform_type = MAX_WORK_PLATFORM;
    ctc_stats_mac_rec_t stats_mac_rec;
    ctc_stats_mac_rec_t* p_stats_mac_rec = NULL;
    ctc_stats_mac_rec_plus_t* p_rx_stats_plus = NULL;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    sal_memset(&mac_stats, 0, sizeof(sys_macstats_t));
    sal_memset(&stats_mac_rec, 0, sizeof(ctc_stats_mac_rec_t));
    sal_memset(&xqmac_stats_ram, 0, sizeof(XqmacStatsRam0_m));

    STATS_LOCK;

    sal_memset(&p_stats->u, 0, sizeof(p_stats->u));
    p_stats_mac_rec = &(p_stats->u.stats_detail.stats.rx_stats);
    p_rx_stats_plus = &(p_stats->u.stats_plus.stats.rx_stats_plus);

    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_get_stats_tbl_info(lchip, gport, &tbl_id, &tbl_base, &mac_ram_type));

    cmdr = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);

    SYS_STATS_DBG_FUNC();
    SYS_STATS_DBG_INFO("mac_ram_type:%d, drv_port:%d, base:%d\n", mac_ram_type, lport, tbl_base);

    is_xqmac = ((tbl_id >= XqmacStatsRam0_t) && (tbl_id <= XqmacStatsRam23_t)) ? 1 : 0;

    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(drv_goldengate_get_platform_type(&platform_type));

    for (stats_type = SYS_STATS_MAC_RCV_GOOD_UCAST; stats_type < SYS_STATS_MAC_RCV_MAX; stats_type++)
    {
        sal_memset(&mac_stats, 0, sizeof(sys_macstats_t));
        sal_memset(&stats_basic, 0, sizeof(ctc_stats_basic_t));

        SYS_STATS_DBG_INFO("read mac stats tbl %s[%d] \n", TABLE_NAME(tbl_id), stats_type + tbl_base);
        CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, stats_type + tbl_base, cmdr, &mac_stats));

        /* for uml, add this code to support clear after read */
        if (SW_SIM_PLATFORM == platform_type)
        {
            SYS_STATS_DBG_INFO("clear %s[%d] by software in uml\n", TABLE_NAME(tbl_id), stats_type + tbl_base);
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, stats_type + tbl_base, cmdw, &xqmac_stats_ram));
        }

        if (TRUE == gg_stats_master[lchip]->stats_stored_in_sw)
        {
            if (CTC_STATS_QUERY_MODE_POLL == gg_stats_master[lchip]->query_mode)
            {
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_get_mac_stats_tbl(lchip, mac_ram_type, stats_type, gport, &p_mac_stats));
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_sync_ctc_mac_stats(lchip, stats_type, &stats_mac_rec, p_mac_stats));

                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_mac_stats_to_basic(lchip, is_xqmac, &mac_stats, &stats_basic));
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_get_mac_stats_tbl(lchip, mac_ram_type, stats_type, gport, &p_mac_stats));
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_write_sys_mac_stats(lchip, stats_type, p_mac_stats, &stats_basic));
            }
            else if  (CTC_STATS_QUERY_MODE_IO == gg_stats_master[lchip]->query_mode)
            {
                /*
                 * SDK maintain mac stats db and use dma callback,
                 * user app do not need db to stroe every io result.
                 */
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_mac_stats_to_basic(lchip, is_xqmac, &mac_stats, &stats_basic));
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_get_mac_stats_tbl(lchip, mac_ram_type, stats_type, gport, &p_mac_stats));
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_write_sys_mac_stats(lchip, stats_type, p_mac_stats, &stats_basic));
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_sync_ctc_mac_stats(lchip, stats_type, &stats_mac_rec, p_mac_stats));
            }
        }
        else
        {
           /*
            *  SDK do not maintain mac stats db and no dma callback,
            *  the internal time of user app read stats should less than counter overflow.
            */
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_mac_stats_to_basic(lchip, is_xqmac, &mac_stats, &stats_basic));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_write_ctc_mac_stats(lchip, stats_type, &stats_mac_rec, &stats_basic));
        }
    }

    /*plus*/
    p_rx_stats_plus->ucast_pkts = stats_mac_rec.good_ucast_pkts;
    p_rx_stats_plus->all_octets = stats_mac_rec.good_63_bytes + \
                                  stats_mac_rec.bad_63_bytes + \
                                  stats_mac_rec.bytes_64 + \
                                  stats_mac_rec.bytes_65_to_127 + \
                                  stats_mac_rec.bytes_128_to_255 + \
                                  stats_mac_rec.bytes_256_to_511 + \
                                  stats_mac_rec.bytes_512_to_1023 + \
                                  stats_mac_rec.bytes_1024_to_1518 + \
                                  stats_mac_rec.good_1519_bytes + \
                                  stats_mac_rec.bad_1519_bytes + \
                                  stats_mac_rec.good_jumbo_bytes + \
                                  stats_mac_rec.bad_jumbo_bytes;
    p_rx_stats_plus->all_pkts = stats_mac_rec.good_63_pkts + \
                                stats_mac_rec.bad_63_pkts + \
                                stats_mac_rec.pkts_64 + \
                                stats_mac_rec.pkts_65_to_127 + \
                                stats_mac_rec.pkts_128_to_255 + \
                                stats_mac_rec.pkts_256_to_511 + \
                                stats_mac_rec.pkts_512_to_1023 + \
                                stats_mac_rec.pkts_1024_to_1518 + \
                                stats_mac_rec.good_1519_pkts + \
                                stats_mac_rec.bad_1519_pkts + \
                                stats_mac_rec.good_jumbo_pkts + \
                                stats_mac_rec.bad_jumbo_pkts;
    p_rx_stats_plus->bcast_pkts = stats_mac_rec.good_bcast_pkts;
    p_rx_stats_plus->crc_pkts = stats_mac_rec.fcs_error_pkts;
    p_rx_stats_plus->drop_events = stats_mac_rec.mac_overrun_pkts;
    p_rx_stats_plus->error_pkts = stats_mac_rec.fcs_error_pkts + \
                                  stats_mac_rec.mac_overrun_pkts;
    p_rx_stats_plus->fragments_pkts = stats_mac_rec.good_63_pkts + stats_mac_rec.bad_63_pkts;
    p_rx_stats_plus->giants_pkts = stats_mac_rec.good_1519_pkts+ stats_mac_rec.good_jumbo_pkts;
    p_rx_stats_plus->jumbo_events = stats_mac_rec.good_jumbo_pkts + stats_mac_rec.bad_jumbo_pkts;
    p_rx_stats_plus->mcast_pkts = stats_mac_rec.good_mcast_pkts;
    p_rx_stats_plus->overrun_pkts = stats_mac_rec.mac_overrun_pkts;
    p_rx_stats_plus->pause_pkts = stats_mac_rec.good_normal_pause_pkts + stats_mac_rec.good_pfc_pause_pkts;
    p_rx_stats_plus->runts_pkts = stats_mac_rec.good_63_pkts;

    /*detail*/
    sal_memcpy(p_stats_mac_rec, &stats_mac_rec, sizeof(ctc_stats_mac_rec_t));
    p_stats_mac_rec->good_oversize_pkts = p_stats_mac_rec->good_1519_pkts + p_stats_mac_rec->good_jumbo_pkts;
    p_stats_mac_rec->good_oversize_bytes = p_stats_mac_rec->good_1519_bytes + p_stats_mac_rec->good_jumbo_bytes;
    p_stats_mac_rec->good_undersize_pkts = p_stats_mac_rec->good_63_pkts;
    p_stats_mac_rec->good_undersize_bytes = p_stats_mac_rec->good_63_bytes;
    p_stats_mac_rec->good_pause_pkts = p_stats_mac_rec->good_normal_pause_pkts + p_stats_mac_rec->good_pfc_pause_pkts;
    p_stats_mac_rec->good_pause_bytes = p_stats_mac_rec->good_normal_pause_bytes + p_stats_mac_rec->good_pfc_pause_bytes;

    STATS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_clear_mac_rx_stats(uint8 lchip, uint16 gport)
{

    uint8  mac_ram_type = 0;
    uint8  port_type = 0;
    int32  base = 0;
    uint16 lport = 0;
    uint16 index = 0;
    uint32 cmd = 0;
    uint32 reg_id = 0;
    uint32 reg_step = 0;
    XqmacStatsRam0_m xqmac_stats_ram;
    CgmacStatsRam0_m cgmac_stats_ram;
    drv_work_platform_type_t platform_type = MAX_WORK_PLATFORM;

    SYS_STATS_INIT_CHECK(lchip);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    sal_memset(&xqmac_stats_ram, 0, sizeof(XqmacStatsRam0_m));
    sal_memset(&cgmac_stats_ram, 0, sizeof(CgmacStatsRam0_m));

    CTC_ERROR_RETURN(drv_goldengate_get_platform_type(&platform_type));
    STATS_LOCK;
    SYS_STATS_DBG_FUNC();

    /*get mac index ,port id of lport from function to justify mac ram*/
    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_get_mac_ram_type(lchip, gport, &mac_ram_type, &port_type));

    if (SYS_STATS_IS_XQMAC_STATS(mac_ram_type))
    {
        reg_step = XqmacStatsRam1_t - XqmacStatsRam0_t;
        reg_id = XqmacStatsRam0_t + (mac_ram_type - SYS_STATS_XQMAC_STATS_RAM0) * reg_step;
        if (SW_SIM_PLATFORM == platform_type)
        {
            cmd = DRV_IOW(reg_id, DRV_ENTRY_FLAG);
        }
        else
        {
            cmd = DRV_IOR(reg_id, DRV_ENTRY_FLAG);
        }

        if ((CTC_PORT_SPEED_10M == port_type) || (CTC_PORT_SPEED_100M == port_type)
           || (CTC_PORT_SPEED_1G == port_type) || (CTC_PORT_SPEED_2G5 == port_type)
           || (CTC_PORT_SPEED_10G == port_type))
        {
            base = (SYS_GET_MAC_ID(lchip, gport) & 0x3) * (SYS_STATS_MAC_BASED_STATS_XQMAC_RAM_DEPTH / 4);
        }
        else
        {
            base = 0;
        }

        SYS_STATS_DBG_INFO("mac_ram_type:%d, drv_port:%d, base:%d\n", mac_ram_type, lport, base);

        for (index = SYS_STATS_MAC_RCV_GOOD_UCAST + base; index < SYS_STATS_MAC_RCV_MAX + base; index++)
        {
            if (CTC_STATS_QUERY_MODE_IO == gg_stats_master[lchip]->query_mode)
            {
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, index, cmd, &xqmac_stats_ram));
            }
        }
        sal_memset(&gg_stats_master[lchip]->xqmac_stats_table[mac_ram_type].mac_stats_rx[SYS_GET_MAC_ID(lchip, gport) % 4], 0, sizeof(sys_mac_stats_rx_t));
    }
    else if (SYS_STATS_IS_CGMAC_STATS(mac_ram_type))
    {
        reg_step = CgmacStatsRam1_t - CgmacStatsRam0_t;
        reg_id = CgmacStatsRam0_t + (mac_ram_type - SYS_STATS_CGMAC_STATS_RAM0) * reg_step;
        if (SW_SIM_PLATFORM == platform_type)
        {
            cmd = DRV_IOW(reg_id, DRV_ENTRY_FLAG);
        }
        else
        {
            cmd = DRV_IOR(reg_id, DRV_ENTRY_FLAG);
        }

        SYS_STATS_DBG_INFO("mac_ram_type:%d, drv_port:%d, base:%d\n", mac_ram_type, lport, 0);

        for (index = SYS_STATS_MAC_RCV_GOOD_UCAST; index < SYS_STATS_MAC_RCV_MAX; index++)
        {
            if (CTC_STATS_QUERY_MODE_IO == gg_stats_master[lchip]->query_mode)
            {
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, index, cmd, &cgmac_stats_ram));
            }
        }
        sal_memset(&gg_stats_master[lchip]->cgmac_stats_table[mac_ram_type - SYS_STATS_XQMAC_RAM_MAX].mac_stats_rx, 0, sizeof(sys_mac_stats_rx_t));
    }
    else
    {
        STATS_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }

    STATS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_get_mac_tx_stats(uint8 lchip, uint16 gport, ctc_mac_stats_t* p_stats)
{
    bool   is_xqmac = FALSE;
    void*  p_mac_stats = NULL;

    uint8  stats_type = 0;
    uint16 lport = 0;
    uint8  tbl_base = 0;
    uint32 cmdr = 0;
    uint32 cmdw = 0;
    uint32 tbl_id = 0;
    uint8  mac_ram_type = 0;
    sys_macstats_t mac_stats;
    ctc_stats_basic_t stats_basic;
    XqmacStatsRam0_m xqmac_stats_ram;
    drv_work_platform_type_t platform_type = MAX_WORK_PLATFORM;
    ctc_stats_mac_snd_t stats_mac_snd;
    ctc_stats_mac_snd_t* p_stats_mac_snd = NULL;
    ctc_stats_mac_snd_plus_t* p_tx_stats_plus = NULL;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    sal_memset(&mac_stats, 0, sizeof(sys_macstats_t));
    sal_memset(&stats_mac_snd, 0, sizeof(ctc_stats_mac_snd_t));
    sal_memset(&xqmac_stats_ram, 0, sizeof(XqmacStatsRam0_m));

    STATS_LOCK;
    sal_memset(&p_stats->u, 0, sizeof(p_stats->u));
    p_stats_mac_snd = &(p_stats->u.stats_detail.stats.tx_stats);
    p_tx_stats_plus = &(p_stats->u.stats_plus.stats.tx_stats_plus);

    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_get_stats_tbl_info(lchip, gport, &tbl_id, &tbl_base, &mac_ram_type));

    cmdr = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);

    SYS_STATS_DBG_FUNC();
    SYS_STATS_DBG_INFO("mac_ram_type:%d, drv_port:%d, base:%d\n", mac_ram_type, lport, tbl_base);

    is_xqmac = ((tbl_id >= XqmacStatsRam0_t) && (tbl_id <= XqmacStatsRam23_t)) ? 1 : 0;
    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(drv_goldengate_get_platform_type(&platform_type));

    for (stats_type = SYS_STATS_MAC_SEND_UCAST; stats_type < SYS_STATS_MAC_SEND_MAX; stats_type++)
    {
        sal_memset(&mac_stats, 0, sizeof(sys_macstats_t));
        sal_memset(&stats_basic, 0, sizeof(ctc_stats_basic_t));

        SYS_STATS_DBG_INFO("read mac stats tbl %s[%d] \n", TABLE_NAME(tbl_id), stats_type + tbl_base);
        CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, stats_type + tbl_base, cmdr, &mac_stats));

        /* for uml, add this code to support clear after read */
        if (SW_SIM_PLATFORM == platform_type)
        {
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, stats_type + tbl_base, cmdw, &xqmac_stats_ram));
            SYS_STATS_DBG_INFO("clear %s[%d] by software in uml\n", TABLE_NAME(tbl_id),  stats_type + tbl_base);
        }

        if (TRUE == gg_stats_master[lchip]->stats_stored_in_sw)
        {
            if (CTC_STATS_QUERY_MODE_POLL == gg_stats_master[lchip]->query_mode)
            {
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_get_mac_stats_tbl(lchip, mac_ram_type, stats_type, gport, &p_mac_stats));
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_sync_ctc_mac_stats(lchip, stats_type, &stats_mac_snd, p_mac_stats));

                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_mac_stats_to_basic(lchip, is_xqmac, &mac_stats, &stats_basic));
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_get_mac_stats_tbl(lchip, mac_ram_type, stats_type, gport, &p_mac_stats));
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_write_sys_mac_stats(lchip, stats_type, p_mac_stats, &stats_basic));
            }
            else if  (CTC_STATS_QUERY_MODE_IO == gg_stats_master[lchip]->query_mode)
            {
                /*
                 * SDK maintain mac stats db and use dma callback,
                 * user app do not need db to stroe every io result.
                 */
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_mac_stats_to_basic(lchip, is_xqmac, &mac_stats, &stats_basic));
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_get_mac_stats_tbl(lchip, mac_ram_type, stats_type, gport, &p_mac_stats));
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_write_sys_mac_stats(lchip, stats_type, p_mac_stats, &stats_basic));
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_sync_ctc_mac_stats(lchip, stats_type, &stats_mac_snd, p_mac_stats));
            }
        }
        else
        {
           /*
            *  SDK do not maintain mac stats db and no dma callback,
            *  the internal time of user app read stats should less than counter overflow.
            */
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_mac_stats_to_basic(lchip, is_xqmac, &mac_stats, &stats_basic));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_write_ctc_mac_stats(lchip, stats_type, &stats_mac_snd, &stats_basic));
        }
    }

    /*plus*/
    p_tx_stats_plus->all_octets = stats_mac_snd.bytes_63 + \
                                  stats_mac_snd.bytes_64 + \
                                  stats_mac_snd.bytes_65_to_127 + \
                                  stats_mac_snd.bytes_128_to_255 + \
                                  stats_mac_snd.bytes_256_to_511 + \
                                  stats_mac_snd.bytes_512_to_1023 + \
                                  stats_mac_snd.bytes_1024_to_1518 + \
                                  stats_mac_snd.bytes_1519 + \
                                  stats_mac_snd.jumbo_bytes;
    p_tx_stats_plus->all_pkts = stats_mac_snd.pkts_63 + \
                                stats_mac_snd.pkts_64 + \
                                stats_mac_snd.pkts_65_to_127 + \
                                stats_mac_snd.pkts_128_to_255 + \
                                stats_mac_snd.pkts_256_to_511 + \
                                stats_mac_snd.pkts_512_to_1023 + \
                                stats_mac_snd.pkts_1024_to_1518 + \
                                stats_mac_snd.pkts_1519 + \
                                stats_mac_snd.jumbo_pkts;
    p_tx_stats_plus->bcast_pkts = stats_mac_snd.good_bcast_pkts;
    p_tx_stats_plus->error_pkts = stats_mac_snd.fcs_error_pkts;
    p_tx_stats_plus->jumbo_events = stats_mac_snd.jumbo_pkts;
    p_tx_stats_plus->mcast_pkts = stats_mac_snd.good_mcast_pkts;
    p_tx_stats_plus->ucast_pkts = stats_mac_snd.good_ucast_pkts;
    p_tx_stats_plus->underruns_pkts = stats_mac_snd.mac_underrun_pkts;

    /*detail*/
    sal_memcpy(p_stats_mac_snd, &stats_mac_snd, sizeof(ctc_stats_mac_snd_t));
    /* some indirect plus or minus*/

    STATS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_clear_mac_tx_stats(uint8 lchip, uint16 gport)
{

    uint8  port_type = 0;
    uint8  mac_ram_type = 0;
    int32  base = 0;
    uint16 lport = 0;
    uint16 index = 0;
    uint32 cmd = 0;
    uint32 reg_id = 0;
    uint32 reg_step = 0;
    XqmacStatsRam0_m xqmac_stats_ram;
    CgmacStatsRam0_m cgmac_stats_ram;
    drv_work_platform_type_t platform_type = MAX_WORK_PLATFORM;

    SYS_STATS_INIT_CHECK(lchip);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    sal_memset(&xqmac_stats_ram, 0, sizeof(XqmacStatsRam0_m));
    sal_memset(&cgmac_stats_ram, 0, sizeof(CgmacStatsRam0_m));
    SYS_STATS_DBG_FUNC();

    CTC_ERROR_RETURN(drv_goldengate_get_platform_type(&platform_type));
    STATS_LOCK;
    /*get mac index ,channel of lport from function to justify mac ram*/
    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_get_mac_ram_type(lchip, gport, &mac_ram_type, &port_type));
    /* check mac is disable */

    if (SYS_STATS_IS_XQMAC_STATS(mac_ram_type))
    {
        reg_step = XqmacStatsRam1_t - XqmacStatsRam0_t;
        reg_id = XqmacStatsRam0_t + (mac_ram_type - SYS_STATS_XQMAC_STATS_RAM0) * reg_step;
        if (SW_SIM_PLATFORM == platform_type)
        {
            cmd = DRV_IOW(reg_id, DRV_ENTRY_FLAG);
        }
        else
        {
            cmd = DRV_IOR(reg_id, DRV_ENTRY_FLAG);
        }

        if ((CTC_PORT_SPEED_10M == port_type) || (CTC_PORT_SPEED_100M == port_type)
           || (CTC_PORT_SPEED_1G == port_type) || (CTC_PORT_SPEED_2G5 == port_type)
           || (CTC_PORT_SPEED_10G == port_type))
        {
            base = (SYS_GET_MAC_ID(lchip, gport) & 0x3) * (SYS_STATS_MAC_BASED_STATS_XQMAC_RAM_DEPTH / 4);
        }
        else
        {
            base = 0;
        }

        SYS_STATS_DBG_INFO("mac_ram_type:%d, drv_port:%d, base:%d\n", mac_ram_type, lport, base);

        for (index = SYS_STATS_MAC_SEND_UCAST + base; index < SYS_STATS_MAC_SEND_MAX + base; index++)
        {
            if (CTC_STATS_QUERY_MODE_IO == gg_stats_master[lchip]->query_mode)
            {
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, index, cmd, &xqmac_stats_ram));
            }
        }
        sal_memset(&gg_stats_master[lchip]->xqmac_stats_table[mac_ram_type].mac_stats_tx[SYS_GET_MAC_ID(lchip, gport) % 4], 0, sizeof(sys_mac_stats_tx_t));
    }
    else if (SYS_STATS_IS_CGMAC_STATS(mac_ram_type))
    {
        reg_step = CgmacStatsRam1_t - CgmacStatsRam0_t;
        reg_id = CgmacStatsRam0_t + (mac_ram_type - SYS_STATS_CGMAC_STATS_RAM0) * reg_step;
        if (SW_SIM_PLATFORM == platform_type)
        {
            cmd = DRV_IOW(reg_id, DRV_ENTRY_FLAG);
        }
        else
        {
            cmd = DRV_IOR(reg_id, DRV_ENTRY_FLAG);
        }

        SYS_STATS_DBG_INFO("mac_ram_type:%d, drv_port:%d, base:%d\n", mac_ram_type, lport, 0);

        for (index = SYS_STATS_MAC_SEND_UCAST; index < SYS_STATS_MAC_SEND_MAX; index++)
        {
            if (CTC_STATS_QUERY_MODE_IO == gg_stats_master[lchip]->query_mode)
            {
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, index, cmd, &cgmac_stats_ram));
            }
        }
        sal_memset(&gg_stats_master[lchip]->cgmac_stats_table[mac_ram_type - SYS_STATS_XQMAC_RAM_MAX].mac_stats_tx, 0, sizeof(sys_mac_stats_tx_t));
    }
    else
    {
        STATS_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }
    STATS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_get_cpu_mac_stats(uint8 lchip, uint16 gport, ctc_cpu_mac_stats_t* p_cpu_stats)
{

    uint16 lport = 0;
    ctc_stats_cpu_mac_t* p_cpu_mac = NULL;
    ctc_mac_stats_t mac_stats;

    CTC_PTR_VALID_CHECK(p_cpu_stats);
    SYS_STATS_INIT_CHECK(lchip);
    SYS_STATS_DBG_FUNC();
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    p_cpu_mac = &(p_cpu_stats->cpu_mac_stats);
    sal_memset(p_cpu_mac, 0, sizeof(ctc_stats_cpu_mac_t));

    gport = CTC_MAP_LPORT_TO_GPORT(CTC_MAP_GPORT_TO_GCHIP(gport), CTC_LPORT_CPU);

    sal_memset(&mac_stats, 0, sizeof(ctc_mac_stats_t));
    mac_stats.stats_mode = CTC_STATS_MODE_DETAIL;
    CTC_ERROR_RETURN(sys_goldengate_stats_get_mac_rx_stats(lchip, gport, &mac_stats));

    p_cpu_mac->cpu_mac_rx_good_pkts = mac_stats.u.stats_detail.stats.rx_stats.good_normal_pause_pkts \
                                      + mac_stats.u.stats_detail.stats.rx_stats.good_pfc_pause_pkts \
                                      + mac_stats.u.stats_detail.stats.rx_stats.good_control_pkts \
                                      + mac_stats.u.stats_detail.stats.rx_stats.good_bcast_pkts \
                                      + mac_stats.u.stats_detail.stats.rx_stats.good_mcast_pkts \
                                      + mac_stats.u.stats_detail.stats.rx_stats.good_ucast_pkts;

    p_cpu_mac->cpu_mac_rx_good_bytes = mac_stats.u.stats_detail.stats.rx_stats.good_normal_pause_bytes \
                                       + mac_stats.u.stats_detail.stats.rx_stats.good_pfc_pause_bytes \
                                       + mac_stats.u.stats_detail.stats.rx_stats.good_control_bytes \
                                       + mac_stats.u.stats_detail.stats.rx_stats.good_bcast_bytes   \
                                       + mac_stats.u.stats_detail.stats.rx_stats.good_mcast_bytes \
                                       + mac_stats.u.stats_detail.stats.rx_stats.good_ucast_bytes;

    p_cpu_mac->cpu_mac_rx_bad_pkts = mac_stats.u.stats_detail.stats.rx_stats.good_63_pkts \
                                     + mac_stats.u.stats_detail.stats.rx_stats.bad_63_pkts \
                                     + mac_stats.u.stats_detail.stats.rx_stats.pkts_64 \
                                     + mac_stats.u.stats_detail.stats.rx_stats.pkts_65_to_127 \
                                     + mac_stats.u.stats_detail.stats.rx_stats.pkts_128_to_255 \
                                     + mac_stats.u.stats_detail.stats.rx_stats.pkts_256_to_511 \
                                     + mac_stats.u.stats_detail.stats.rx_stats.pkts_512_to_1023 \
                                     + mac_stats.u.stats_detail.stats.rx_stats.pkts_1024_to_1518 \
                                     + mac_stats.u.stats_detail.stats.rx_stats.good_1519_pkts \
                                     + mac_stats.u.stats_detail.stats.rx_stats.bad_1519_pkts \
                                     + mac_stats.u.stats_detail.stats.rx_stats.good_jumbo_pkts \
                                     + mac_stats.u.stats_detail.stats.rx_stats.bad_jumbo_pkts \
                                     - p_cpu_mac->cpu_mac_rx_good_pkts;

    p_cpu_mac->cpu_mac_rx_bad_bytes = mac_stats.u.stats_detail.stats.rx_stats.good_63_bytes \
                                     + mac_stats.u.stats_detail.stats.rx_stats.bad_63_bytes \
                                     + mac_stats.u.stats_detail.stats.rx_stats.bytes_64 \
                                     + mac_stats.u.stats_detail.stats.rx_stats.bytes_65_to_127 \
                                     + mac_stats.u.stats_detail.stats.rx_stats.bytes_128_to_255 \
                                     + mac_stats.u.stats_detail.stats.rx_stats.bytes_256_to_511 \
                                     + mac_stats.u.stats_detail.stats.rx_stats.bytes_512_to_1023 \
                                     + mac_stats.u.stats_detail.stats.rx_stats.bytes_1024_to_1518 \
                                     + mac_stats.u.stats_detail.stats.rx_stats.good_1519_bytes \
                                     + mac_stats.u.stats_detail.stats.rx_stats.bad_1519_bytes \
                                     + mac_stats.u.stats_detail.stats.rx_stats.good_jumbo_bytes \
                                     + mac_stats.u.stats_detail.stats.rx_stats.bad_jumbo_bytes \
                                     - p_cpu_mac->cpu_mac_rx_good_bytes;

    p_cpu_mac->cpu_mac_rx_fcs_error_pkts = mac_stats.u.stats_detail.stats.rx_stats.fcs_error_pkts;
    p_cpu_mac->cpu_mac_rx_fragment_pkts = mac_stats.u.stats_detail.stats.rx_stats.bad_63_pkts
                                          + mac_stats.u.stats_detail.stats.rx_stats.good_63_pkts;

    p_cpu_mac->cpu_mac_rx_overrun_pkts = mac_stats.u.stats_detail.stats.rx_stats.mac_overrun_pkts;

    sal_memset(&mac_stats, 0, sizeof(ctc_mac_stats_t));
    mac_stats.stats_mode = CTC_STATS_MODE_DETAIL;
    CTC_ERROR_RETURN(sys_goldengate_stats_get_mac_tx_stats(lchip, gport, &mac_stats));

    p_cpu_mac->cpu_mac_tx_total_pkts = mac_stats.u.stats_detail.stats.tx_stats.pkts_63 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.pkts_64 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.pkts_65_to_127 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.pkts_128_to_255 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.pkts_256_to_511 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.pkts_512_to_1023 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.pkts_1024_to_1518 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.pkts_1519 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.jumbo_pkts;

    p_cpu_mac->cpu_mac_tx_total_bytes = mac_stats.u.stats_detail.stats.tx_stats.bytes_63 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.bytes_64 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.bytes_65_to_127 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.bytes_128_to_255 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.bytes_256_to_511 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.bytes_512_to_1023 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.bytes_1024_to_1518 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.bytes_1519 \
                                       + mac_stats.u.stats_detail.stats.tx_stats.jumbo_bytes;

    p_cpu_mac->cpu_mac_tx_fcs_error_pkts = mac_stats.u.stats_detail.stats.tx_stats.fcs_error_pkts;

    p_cpu_mac->cpu_mac_tx_underrun_pkts = mac_stats.u.stats_detail.stats.tx_stats.mac_underrun_pkts;

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_clear_cpu_mac_stats(uint8 lchip, uint16 gport)
{
    uint16 lport = 0;

    SYS_STATS_INIT_CHECK(lchip);
    SYS_STATS_DBG_FUNC();
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    gport = CTC_MAP_LPORT_TO_GPORT(CTC_MAP_GPORT_TO_GCHIP(gport), CTC_LPORT_CPU);
    CTC_ERROR_RETURN(sys_goldengate_stats_clear_mac_rx_stats(lchip, gport));
    CTC_ERROR_RETURN(sys_goldengate_stats_clear_mac_tx_stats(lchip, gport));

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_register_cb(uint8 lchip, ctc_stats_sync_fn_t cb, void* userdata)
{
    gg_stats_master[lchip]->dam_sync_mac_stats = cb;
    gg_stats_master[lchip]->userdata = userdata;

    return CTC_E_NONE;
}

int32
_sys_goldengate_stats_get_dam_sync_mac_stats(uint8 lchip, uint8 mac_type, uint8 stats_type, ctc_mac_stats_t* sys_dma_stats_sync, ctc_stats_basic_t* p_stats_basic)
{
    ctc_stats_mac_rec_t* p_rx_mac_stats = NULL;
    ctc_stats_mac_snd_t* p_tx_mac_stats = NULL;

    if (stats_type < SYS_STATS_MAC_RCV_MAX)
    {
        p_rx_mac_stats = &(sys_dma_stats_sync->u.stats_detail.stats.rx_stats);
        CTC_ERROR_RETURN(_sys_goldengate_stats_write_ctc_mac_stats(lchip, stats_type, p_rx_mac_stats, p_stats_basic));
    }
    else
    {
        p_tx_mac_stats = &(sys_dma_stats_sync->u.stats_detail.stats.tx_stats);
        CTC_ERROR_RETURN(_sys_goldengate_stats_write_ctc_mac_stats(lchip, stats_type, p_tx_mac_stats, p_stats_basic));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_get_throughput(uint8 lchip, uint16 gport, uint64* p_throughput, sal_systime_t* p_systime)
{

    uint16 drv_lport = 0, sys_lport = 0;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, drv_lport);
    sys_lport = SYS_MAP_DRV_LPORT_TO_SYS_LPORT(drv_lport);

    p_throughput[CTC_STATS_MAC_STATS_RX] = mac_throughput[lchip].bytes[sys_lport][CTC_STATS_MAC_STATS_RX];
    p_throughput[CTC_STATS_MAC_STATS_TX] = mac_throughput[lchip].bytes[sys_lport][CTC_STATS_MAC_STATS_TX];
    sal_memcpy(p_systime, &(mac_throughput[lchip].timestamp[sys_lport]), sizeof(sal_systime_t));

    SYS_STATS_DBG_INFO("%s %d, tv_sec:%u, tv_usec:%u ms.\n",
                       __FUNCTION__, __LINE__,
                       mac_throughput[lchip].timestamp[sys_lport].tv_sec,
                       mac_throughput[lchip].timestamp[sys_lport].tv_usec);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_sync_throughput(uint8 lchip, uint16 gport, uint8* p_addr)
{
    uint16 sys_lport = 0, drv_lport = 0;
    bool   is_xqmac = FALSE;
    void*  p_mac_stats = NULL;

    uint8  stats_type = 0;
    uint16 stats_offset = 0;
    uint8  tbl_base = 0;
    uint32 cmdr = 0;
    uint32 tbl_id = 0;
    uint8  mac_ram_type = 0;
    uint32  mac_ipg = 0;
    int32 ret = CTC_E_NONE;
    ctc_mac_stats_dir_t dir = CTC_STATS_MAC_STATS_RX;
    sys_macstats_t mac_stats;
    ctc_stats_basic_t stats_basic;
    drv_work_platform_type_t platform_type = MAX_WORK_PLATFORM;

    SYS_STATS_INIT_CHECK(lchip);

    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_get_stats_tbl_info(lchip, gport, &tbl_id, &tbl_base, &mac_ram_type));
    cmdr = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);

    SYS_STATS_DBG_FUNC();
    SYS_STATS_DBG_INFO("mac_ram_type:%d, drv_port:%d, base:%d\n", mac_ram_type, sys_lport, tbl_base);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, drv_lport);
    sys_lport = SYS_MAP_DRV_LPORT_TO_SYS_LPORT(drv_lport);

    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(drv_goldengate_get_platform_type(&platform_type));
    if (SW_SIM_PLATFORM == platform_type)
    {
         /*return;*/
    }

    STATS_LOCK;

    is_xqmac = ((tbl_id >= XqmacStatsRam0_t) && (tbl_id <= XqmacStatsRam23_t)) ? 1 : 0;
    for (stats_type = SYS_STATS_MAC_RCV_GOOD_UCAST; stats_type < SYS_STATS_MAC_STATS_TYPE_NUM; stats_type++)
    {
        if (!((SYS_STATS_MAC_RCV_GOOD_UCAST == stats_type) || (SYS_STATS_MAC_RCV_GOOD_MCAST == stats_type)
           || (SYS_STATS_MAC_RCV_GOOD_BCAST == stats_type) || (SYS_STATS_MAC_SEND_UCAST == stats_type)
           || (SYS_STATS_MAC_SEND_MCAST == stats_type) || (SYS_STATS_MAC_SEND_BCAST == stats_type)))
        {
            continue;
        }

        sal_memset(&mac_stats, 0, sizeof(sys_macstats_t));
        sal_memset(&stats_basic, 0, sizeof(ctc_stats_basic_t));

        if (NULL != p_addr)
        {
            stats_offset = stats_type * DRV_ADDR_BYTES_PER_ENTRY;
            sal_memcpy(&mac_stats, (uint8*)p_addr + stats_offset, sizeof(mac_stats));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_mac_stats_to_basic(lchip, is_xqmac, &mac_stats, &stats_basic));
        }
        else
        {
            SYS_STATS_DBG_INFO("read mac stats tbl %s[%d] \n", TABLE_NAME(tbl_id), stats_type + tbl_base);
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, stats_type + tbl_base, cmdr, &mac_stats));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_mac_stats_to_basic(lchip, is_xqmac, &mac_stats, &stats_basic));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_get_mac_stats_tbl(lchip, mac_ram_type, stats_type, gport, &p_mac_stats));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_write_sys_mac_stats(lchip, stats_type, p_mac_stats, &stats_basic));
        }

        dir = (stats_type < SYS_STATS_MAC_SEND_UCAST) ? CTC_STATS_MAC_STATS_RX : CTC_STATS_MAC_STATS_TX;
        ret = sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_MAC_TX_IPG, &mac_ipg);
        if (ret < 0)
        {
            mac_ipg = 12;
        }
        mac_throughput[lchip].bytes[sys_lport][dir] += (stats_basic.byte_count + stats_basic.packet_count*(8+mac_ipg));
    }
    sal_gettime(&(mac_throughput[lchip].timestamp[sys_lport]));

    STATS_UNLOCK;

    return CTC_E_NONE;
}

void
sys_goldengate_stats_get_port_rate(uint8 lchip, uint16 gport, uint64* p_rate)
{

    int32       ret = CTC_E_NONE;
    uint32      link_up = 0;
    uint64      pre_throughput[2] = {0}, cur_throughput[2] = {0};
    uint64      intv = 0;
    sal_systime_t  pre_timestamp, cur_timestamp;

    sal_memset(&pre_timestamp,  0, sizeof(sal_systime_t));
    sal_memset(&cur_timestamp,  0, sizeof(sal_systime_t));

    p_rate[CTC_STATS_MAC_STATS_RX] = 0;
    p_rate[CTC_STATS_MAC_STATS_TX] = 0;

    ret = _sys_goldengate_stats_get_throughput(lchip, gport, pre_throughput, &pre_timestamp);

    ret = ret ? ret :_sys_goldengate_stats_sync_throughput(lchip, gport, NULL);

    ret = ret ? ret : _sys_goldengate_stats_get_throughput(lchip, gport, cur_throughput, &cur_timestamp);

    sys_goldengate_port_get_mac_link_up(lchip, gport, &link_up, 0);
    intv = ((cur_timestamp.tv_sec * 1000000 + cur_timestamp.tv_usec)
           - (pre_timestamp.tv_sec * 1000000 + pre_timestamp.tv_usec)) / 1000;

    if ((CTC_E_NONE != ret) || (0 == link_up) || (0 == intv))
    {
        SYS_STATS_DBG_INFO("%s %d, ret:%d, enable:%u, intv:%"PRIu64" ms.\n", __FUNCTION__, __LINE__, ret, link_up, intv);
        return;
    }

    p_rate[CTC_STATS_MAC_STATS_RX] = ((cur_throughput[CTC_STATS_MAC_STATS_RX] - pre_throughput[CTC_STATS_MAC_STATS_RX])
                                      / intv) * 1000;
    p_rate[CTC_STATS_MAC_STATS_TX] = ((cur_throughput[CTC_STATS_MAC_STATS_TX] - pre_throughput[CTC_STATS_MAC_STATS_TX])
                                      / intv) * 1000;

    SYS_STATS_DBG_INFO("%s gport:0x%04X, time-interval:%"PRIu64" ms.\n", __FUNCTION__, gport, intv);

    return;
}

STATIC int32
_sys_goldengate_stats_mac_throughput_init(uint8 lchip)
{
    int32  ret = CTC_E_NONE;
    uint16 gport = 0, lport = 0;
    uint8  slice = 0, gchip = 0;
    sys_datapath_lport_attr_t* p_port_cap = NULL;

    sys_goldengate_get_gchip_id(lchip, &gchip);

    sal_memset(mac_throughput, 0, sizeof(mac_throughput));

    for (slice = 0; slice < 2; slice++)
    {
        for (lport = 0; lport < SYS_CHIP_PER_SLICE_PORT_NUM; lport++)
        {
            ret = (sys_goldengate_common_get_port_capability(lchip, SYS_MAP_DRV_LPORT_TO_SYS_LPORT((slice * 256) + lport),
                        &p_port_cap));
            if (ret == CTC_E_MAC_NOT_USED)
            {
                continue;
            }

            if ((NULL == p_port_cap) || (SYS_DATAPATH_NETWORK_PORT != p_port_cap->port_type))
            {
                continue;
            }
            gport = CTC_MAP_LPORT_TO_GPORT(gchip, SYS_MAP_DRV_LPORT_TO_SYS_LPORT((slice * 256) + lport));
            CTC_ERROR_RETURN(_sys_goldengate_stats_sync_throughput(lchip, gport, NULL));
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_sync_dma_mac_stats(uint8 lchip, void* p_data)
{
    void*  p_mac_stats = NULL;
    uint8  mac_ram_type = 0;
    uint8  stats_type = 0;
    uint16 stats_offset = 0;
    uint16 lport = 0;
    uint16 gport = 0;
    uint32 tbl_id = 0;
    uint8  gchip_id = 0;
    uint8  tbl_base = 0;
    uint8  is_xqmac = 0;
    uint8  mac_id = 0;
    uint8* p_addr = NULL;
    XqmacStatsRam0_m stats_ram;
    ctc_stats_basic_t stats_basic;
    sys_dma_reg_t* p_dma_reg = (sys_dma_reg_t*)p_data;
    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_data);

    mac_id = *((uint8*)p_dma_reg->p_ext);
    p_addr = p_dma_reg->p_data;

    STATS_LOCK;

    sys_goldengate_get_gchip_id(lchip, &gchip_id);
    lport = sys_goldengate_common_get_lport_with_mac(lchip, mac_id);
    if (SYS_COMMON_USELESS_MAC == lport)
    {
        STATS_UNLOCK;
        return CTC_E_NONE;
    }
    gport = CTC_MAP_LPORT_TO_GPORT(gchip_id, lport);
    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_get_stats_tbl_info(lchip, gport, &tbl_id, &tbl_base, &mac_ram_type));

    is_xqmac = ((tbl_id >= XqmacStatsRam0_t) && (tbl_id <= XqmacStatsRam23_t)) ? 1 : 0;
    sal_memset(&stats_ram, 0, sizeof(XqmacStatsRam0_m));

    for (stats_type = 0; stats_type < SYS_STATS_MAC_STATS_TYPE_NUM; stats_type++)
    {
        stats_offset = stats_type * DRV_ADDR_BYTES_PER_ENTRY;

        sal_memcpy(&stats_ram, (uint8*)p_addr + stats_offset, sizeof(stats_ram));
        sal_memset(&stats_basic, 0, sizeof(ctc_stats_basic_t));

        _sys_goldengate_stats_mac_stats_to_basic(lchip, is_xqmac, &stats_ram, &stats_basic);

#if 0
        SYS_STATS_DBG_INFO("Sync DMA stats mac-id %d, %s[%d] frame-num %lu, byte-num %lu.\n", mac_id, TABLE_NAME(tbl_id), stats_type + tbl_base, stats_basic.packet_count, stats_basic.byte_count);
#endif

        CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_get_mac_stats_tbl(lchip, mac_ram_type, stats_type, gport, &p_mac_stats));
        CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_goldengate_stats_write_sys_mac_stats(lchip, stats_type, p_mac_stats, &stats_basic));
    }

    STATS_UNLOCK;

    CTC_ERROR_RETURN(_sys_goldengate_stats_sync_throughput(lchip, gport, p_addr));

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_set_stats_interval(uint8 lchip, uint32  stats_interval)
{
    SYS_STATS_INIT_CHECK(lchip);
#if 0
    CTC_ERROR_RETURN(sys_goldengate_dma_set_stats_interval(lchip, stats_interval));
#endif
    return CTC_E_NONE;
}

#define ___________FLOW_STATS_FUNCTION________________________
#define __1_STATS_DB__
STATIC uint32
_sys_goldengate_stats_hash_key_make(sys_stats_fwd_stats_t* p_flow_hash)
{
    uint32 size;
    uint8*  k;

    CTC_PTR_VALID_CHECK(p_flow_hash);

    size = 2;
    k = (uint8*)p_flow_hash;

    return ctc_hash_caculate(size, k);
}

STATIC int32
_sys_goldengate_stats_hash_key_cmp(sys_stats_fwd_stats_t* p_flow_hash1, sys_stats_fwd_stats_t* p_flow_hash)
{
    CTC_PTR_VALID_CHECK(p_flow_hash1);
    CTC_PTR_VALID_CHECK(p_flow_hash);

    if (p_flow_hash1->stats_ptr != p_flow_hash->stats_ptr)
    {
        return FALSE;
    }

    return TRUE;
}

STATIC int32
_sys_goldengate_stats_ds_stats_to_basic(uint8 lchip, DsStats_m ds_stats, ctc_stats_basic_t* basic_stats)
{
    uint64 tmp = 0;
    uint32 count[2] = {0};

    CTC_PTR_VALID_CHECK(basic_stats);

    tmp = GetDsStats(V, packetCount_f, &ds_stats);

    basic_stats->packet_count = tmp;

    tmp = GetDsStats(A, byteCount_f, &ds_stats, count);
    tmp |= count[1];
    tmp <<= 32;
    tmp |= count[0];
    basic_stats->byte_count = tmp;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_fwd_stats_entry_create(uint8 lchip, uint16 stats_ptr)
{
    sys_stats_fwd_stats_t flow_stats;
    sys_stats_fwd_stats_t* p_flow_stats;

    sal_memset(&flow_stats, 0, sizeof(flow_stats));

    flow_stats.stats_ptr = stats_ptr;
    p_flow_stats = ctc_hash_lookup(gg_stats_master[lchip]->flow_stats_hash, &flow_stats);
    if (p_flow_stats)
    {
        return CTC_E_NONE;
    }

    p_flow_stats = (sys_stats_fwd_stats_t*)mem_malloc(MEM_STATS_MODULE, sizeof(sys_stats_fwd_stats_t));
    if (NULL == p_flow_stats)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_flow_stats, 0, sizeof(sys_stats_fwd_stats_t));
    p_flow_stats->stats_ptr = stats_ptr;

    /* add it to fwd stats hash */
    ctc_hash_insert(gg_stats_master[lchip]->flow_stats_hash, p_flow_stats);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_fwd_stats_entry_delete(uint8 lchip, uint16 stats_ptr)
{
    sys_stats_fwd_stats_t flow_stats;
    sys_stats_fwd_stats_t* p_flow_stats = NULL;

    sal_memset(&flow_stats, 0, sizeof(flow_stats));

    flow_stats.stats_ptr = stats_ptr;
    p_flow_stats = ctc_hash_lookup(gg_stats_master[lchip]->flow_stats_hash, &flow_stats);
    if (p_flow_stats)
    {
        ctc_hash_remove(gg_stats_master[lchip]->flow_stats_hash, p_flow_stats);
        mem_free(p_flow_stats);
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_flow_stats_entry_lookup(uint8 lchip, uint16 stats_ptr, sys_stats_fwd_stats_t** pp_fwd_stats)
{
    sys_stats_fwd_stats_t flow_stats;
    sys_stats_fwd_stats_t* p_flow_stats = NULL;

    CTC_PTR_VALID_CHECK(pp_fwd_stats);
    sal_memset(&flow_stats, 0, sizeof(flow_stats));

    *pp_fwd_stats = NULL;

    flow_stats.stats_ptr = stats_ptr;
    p_flow_stats = ctc_hash_lookup(gg_stats_master[lchip]->flow_stats_hash, &flow_stats);
    if (p_flow_stats)
    {
        *pp_fwd_stats = p_flow_stats;
    }

    return CTC_E_NONE;
}

#define __2_PORT_LOG_STATS__
int32
sys_goldengate_stats_set_drop_packet_stats_en(uint8 lchip, ctc_stats_discard_t bitmap, bool enable)
{
    uint32 cmd = 0, tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);

    if (0 != (bitmap & CTC_STATS_RANDOM_LOG_DISCARD_STATS))
    {
        tmp = (enable) ? 1 : 0;

        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_logPortDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_logPortDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
    }

    tmp = 0;
    if (0 != (bitmap & CTC_STATS_FLOW_DISCARD_STATS))
    {
        tmp = (enable) ? 1 : 0;

        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_acl0DiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_acl1DiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_acl2DiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_acl3DiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_ifDiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_fwdDiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_aclDiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_ifDiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_fwdDiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_get_port_log_discard_stats_enable(uint8 lchip, ctc_stats_discard_t bitmap, bool* enable)
{
    uint32 cmd = 0, tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(enable);

    *enable = FALSE;

    switch (bitmap & (CTC_STATS_RANDOM_LOG_DISCARD_STATS | CTC_STATS_FLOW_DISCARD_STATS))
    {
    case CTC_STATS_RANDOM_LOG_DISCARD_STATS:
        cmd = DRV_IOR(IpeFwdCtl_t, IpeFwdCtl_logPortDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        *enable = (tmp) ? TRUE : FALSE;
        break;

    case CTC_STATS_FLOW_DISCARD_STATS:
        cmd = DRV_IOR(IpeFwdCtl_t, IpeFwdCtl_acl0DiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        *enable = (tmp) ? TRUE : FALSE;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_update_port_log_stats(uint8 lchip, uint16 port, ctc_stats_basic_t* p_stats)
{
    uint16 index = 0;
    uint32 cmd = 0;
    DsIngressPortLogStats_m port_log_stats;
    ctc_stats_basic_t *p_stats_db;
    uint32 cmd_w = 0;
    drv_work_platform_type_t platform_type;

    CTC_PTR_VALID_CHECK(p_stats);
    sal_memset(&port_log_stats, 0, sizeof(port_log_stats));
    sal_memset(p_stats, 0, sizeof(ctc_stats_basic_t));

    if (port >= SYS_GG_MAX_PORT_NUM_PER_CHIP)
    {
        cmd = DRV_IOR(DsEgressPortLogStats_t, DRV_ENTRY_FLAG);
        index = port - SYS_GG_MAX_PORT_NUM_PER_CHIP;
        cmd_w =  DRV_IOW(DsEgressPortLogStats_t, DRV_ENTRY_FLAG);
    }
    else
    {
        cmd = DRV_IOR(DsIngressPortLogStats_t, DRV_ENTRY_FLAG);
        index = port;
        cmd_w =  DRV_IOW(DsIngressPortLogStats_t, DRV_ENTRY_FLAG);
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &port_log_stats));

    p_stats->packet_count = GetDsIngressPortLogStats(V, packetCount_f, &port_log_stats);
    GetDsIngressPortLogStats(A, byteCount_f, &port_log_stats, &(p_stats->byte_count));
#if (0 == SDK_WORK_PLATFORM)
    {
        uint64 temp_bytecount = p_stats->byte_count;
        SYS_UINT64_SET(p_stats->byte_count, ((uint32*)&temp_bytecount));
    }
#endif

    /* for uml, add this code to support clear after read */
    CTC_ERROR_RETURN(drv_goldengate_get_platform_type(&platform_type));
    if (platform_type == SW_SIM_PLATFORM)
    {
        sal_memset(&port_log_stats, 0, sizeof(port_log_stats));
        DRV_IOCTL(lchip, index, cmd_w, &port_log_stats);
        SYS_STATS_DBG_INFO("clear port log by software in uml\n");
    }

    /* need store static stats to sw db to avoid overflow */
    p_stats_db = ctc_vector_get(gg_stats_master[lchip]->port_log_vec,  port);
    if (NULL == p_stats_db)
    {
        p_stats_db = (ctc_stats_basic_t*)mem_malloc(MEM_STATS_MODULE, sizeof(ctc_stats_basic_t));
        if (NULL == p_stats_db)
        {
            return CTC_E_NO_MEMORY;
        }

        ctc_vector_add(gg_stats_master[lchip]->port_log_vec, port, p_stats_db);
        sal_memset(p_stats_db, 0, sizeof(ctc_stats_basic_t));
    }

    p_stats_db->packet_count += p_stats->packet_count;
	p_stats_db->byte_count += p_stats->byte_count;

    p_stats->packet_count = p_stats_db->packet_count;
    p_stats->byte_count = p_stats_db->byte_count;

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_clear_port_log_stats(uint8 lchip, uint16 port)
{
    uint16 index = 0;
    uint32 cmd = 0;
    DsIngressPortLogStats_m port_log_stats;
    ctc_stats_basic_t *p_stats_db;

    sal_memset(&port_log_stats, 0, sizeof(port_log_stats));

    if (port >= SYS_GG_MAX_PORT_NUM_PER_CHIP)
    {
        cmd = DRV_IOW(DsEgressPortLogStats_t, DRV_ENTRY_FLAG);
        index = port - SYS_GG_MAX_PORT_NUM_PER_CHIP;
    }
    else
    {
        cmd = DRV_IOW(DsIngressPortLogStats_t, DRV_ENTRY_FLAG);
        index = port;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &port_log_stats));

    /* need store static stats to sw db to avoid overflow */
    p_stats_db = ctc_vector_get(gg_stats_master[lchip]->port_log_vec ,  port);
    if (NULL == p_stats_db)
    {
        return CTC_E_NONE;
    }

    p_stats_db->packet_count = 0;
    p_stats_db->byte_count = 0;

    return CTC_E_NONE;
}


int32
sys_goldengate_stats_get_igs_port_log_stats(uint8 lchip, uint16 gport, ctc_stats_basic_t* p_stats)
{
    uint16 lport = 0;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_ERROR_RETURN(sys_goldengate_stats_update_port_log_stats(lchip, lport, p_stats));

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_clear_igs_port_log_stats(uint8 lchip, uint16 gport)
{
    uint16 lport = 0;

    SYS_STATS_INIT_CHECK(lchip);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_ERROR_RETURN(sys_goldengate_stats_clear_port_log_stats(lchip, lport));

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_get_egs_port_log_stats(uint8 lchip, uint16 gport, ctc_stats_basic_t* p_stats)
{
    uint16 lport = 0;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_ERROR_RETURN(sys_goldengate_stats_update_port_log_stats(lchip, lport + SYS_GG_MAX_PORT_NUM_PER_CHIP , p_stats));
    return CTC_E_NONE;
}

int32
sys_goldengate_stats_clear_egs_port_log_stats(uint8 lchip, uint16 gport)
{
    uint16 lport = 0;

    SYS_STATS_INIT_CHECK(lchip);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_ERROR_RETURN(sys_goldengate_stats_clear_port_log_stats(lchip, lport + SYS_GG_MAX_PORT_NUM_PER_CHIP));

    return CTC_E_NONE;
}
#define __3_FLOW_STATS__

int32
sys_goldengate_stats_get_flow_stats(uint8 lchip,
                                    uint16 stats_ptr,
                                    ctc_stats_basic_t* p_stats)
{
    uint32 cmd = 0;
    DsStats_m ds_stats;
    sys_stats_fwd_stats_t* fwd_stats;
    drv_work_platform_type_t platform_type = MAX_WORK_PLATFORM;

    SYS_STATS_DBG_FUNC();
    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    fwd_stats = NULL;

    sal_memset(&ds_stats, 0, sizeof(ds_stats));
    sal_memset(p_stats, 0, sizeof(ctc_stats_basic_t));

    cmd = DRV_IOR(DsStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, stats_ptr, cmd, &ds_stats));
    _sys_goldengate_stats_ds_stats_to_basic(lchip, ds_stats, p_stats);

    /* for uml, add this code to support clear after read */
    CTC_ERROR_RETURN(drv_goldengate_get_platform_type(&platform_type));
    if (platform_type == SW_SIM_PLATFORM)
    {
        sal_memset(&ds_stats, 0, sizeof(ds_stats));
        cmd = DRV_IOW(DsStats_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, stats_ptr, cmd, &ds_stats);
        SYS_STATS_DBG_INFO("clear stats:%d by software in uml\n", stats_ptr);
    }

    CTC_ERROR_RETURN(_sys_goldengate_stats_flow_stats_entry_lookup(lchip, stats_ptr, &fwd_stats));
    if (NULL != fwd_stats)
    {
        /*add to db*/
        if (gg_stats_master[lchip]->clear_read_en[CTC_STATS_TYPE_FWD])
        {
            fwd_stats->packet_count += p_stats->packet_count;
            fwd_stats->byte_count += p_stats->byte_count;

            p_stats->packet_count = fwd_stats->packet_count;
            p_stats->byte_count = fwd_stats->byte_count;
        }
        else
        {
            fwd_stats->packet_count = p_stats->packet_count;
            fwd_stats->byte_count = p_stats->byte_count;
        }
    }
    else
    {
        fwd_stats = (sys_stats_fwd_stats_t*)mem_malloc(MEM_STATS_MODULE, sizeof(sys_stats_fwd_stats_t));
        if (!fwd_stats)
        {
            return CTC_E_NO_MEMORY;
        }

        fwd_stats->stats_ptr = stats_ptr;
        fwd_stats->packet_count = p_stats->packet_count;
        fwd_stats->byte_count = p_stats->byte_count;

        /* add it to flow stats hash */
        ctc_hash_insert(gg_stats_master[lchip]->flow_stats_hash, fwd_stats);
    }

    SYS_STATS_DBG_INFO("get flow stats stats_ptr:%d, packets: %"PRId64", bytes: %"PRId64"\n", stats_ptr, p_stats->packet_count, p_stats->byte_count);

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_clear_flow_stats(uint8 lchip, uint16 stats_ptr)
{

    uint32 cmd = 0;
    DsStats_m ds_stats;
    sys_stats_fwd_stats_t* fwd_stats;

    SYS_STATS_DBG_FUNC();
    SYS_STATS_INIT_CHECK(lchip);

    SYS_STATS_DBG_INFO("clear flow stats stats_ptr:%d\n", stats_ptr);

    sal_memset(&ds_stats, 0, sizeof(DsStats_m));

    cmd = DRV_IOW(DsStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, stats_ptr, cmd, &ds_stats));

    CTC_ERROR_RETURN(_sys_goldengate_stats_flow_stats_entry_lookup(lchip, stats_ptr, &fwd_stats));
    if(NULL != fwd_stats)
    {
        fwd_stats->packet_count = 0;
        fwd_stats->byte_count = 0;
    }

    return CTC_E_NONE;
}

#define __4_POLICER_STATS__
int32
sys_goldengate_stats_set_policing_en(uint8 lchip, uint8 enable)
{
	PolicingCtl_m  policing_ctl;
    uint32 cmd = 0;

    sal_memset(&policing_ctl, 0, sizeof(policing_ctl));

    cmd = DRV_IOR(PolicingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &policing_ctl))

    SetPolicingCtl(V,statsEnViolate_f ,&policing_ctl,enable);
    SetPolicingCtl(V,statsEnConfirm_f ,&policing_ctl,enable);
    SetPolicingCtl(V,statsEnNotConfirm_f ,&policing_ctl,enable);
    SetPolicingCtl(V,sdcStatsBase_f ,&policing_ctl,SYS_STATS_POLICER_STATS_SIZE-1);

	cmd = DRV_IOW(PolicingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &policing_ctl));

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_alloc_policing_statsptr(uint8 lchip, uint16* stats_ptr)
{
    uint32 offset = 0;
    sys_goldengate_opf_t opf;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(stats_ptr);

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    opf.pool_type = FLOW_STATS_SRAM;
    opf.pool_index = SYS_STATS_CACHE_MODE_POLICER;
    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 3, &offset));    /* for three color */
    gg_stats_master[lchip]->used_count[opf.pool_index] += 3;

    *stats_ptr = offset;

    /* add to fwd stats list */
    _sys_goldengate_stats_fwd_stats_entry_create(lchip, gg_stats_master[lchip]->policer_stats_base+offset);
    _sys_goldengate_stats_fwd_stats_entry_create(lchip, gg_stats_master[lchip]->policer_stats_base+offset+1);
    _sys_goldengate_stats_fwd_stats_entry_create(lchip, gg_stats_master[lchip]->policer_stats_base+offset+2);

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_free_policing_statsptr(uint8 lchip, uint16 stats_ptr)
{
    sys_goldengate_opf_t opf;

    SYS_STATS_INIT_CHECK(lchip);

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    opf.pool_type = FLOW_STATS_SRAM;
    opf.pool_index = SYS_STATS_CACHE_MODE_POLICER;
    CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 3, stats_ptr));    /* for three color */
    gg_stats_master[lchip]->used_count[opf.pool_index] -= 3;

    /* add to fwd stats list */
    _sys_goldengate_stats_fwd_stats_entry_delete(lchip, gg_stats_master[lchip]->policer_stats_base+stats_ptr);
    _sys_goldengate_stats_fwd_stats_entry_delete(lchip, gg_stats_master[lchip]->policer_stats_base+stats_ptr+1);
    _sys_goldengate_stats_fwd_stats_entry_delete(lchip, gg_stats_master[lchip]->policer_stats_base+stats_ptr+2);

    return CTC_E_NONE;
}


int32
sys_goldengate_stats_get_policing_stats(uint8 lchip, uint16 stats_ptr, sys_stats_policing_t* p_stats)
{
    uint32 index = 0;
    ctc_stats_basic_t basic_stats;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    sal_memset(&basic_stats, 0, sizeof(ctc_stats_basic_t));
    sal_memset(p_stats, 0, sizeof(sys_stats_policing_t));

    /*green*/
    index = stats_ptr + gg_stats_master[lchip]->policer_stats_base;
    CTC_ERROR_RETURN(sys_goldengate_stats_get_flow_stats(lchip, index, &basic_stats));
    p_stats->policing_confirm_pkts = basic_stats.packet_count;
    p_stats->policing_confirm_bytes = basic_stats.byte_count;

    /*yellow*/
    index = stats_ptr + gg_stats_master[lchip]->policer_stats_base + 1;
    CTC_ERROR_RETURN(sys_goldengate_stats_get_flow_stats(lchip, index, &basic_stats));
    p_stats->policing_exceed_pkts = basic_stats.packet_count;
    p_stats->policing_exceed_bytes = basic_stats.byte_count;

    /*red*/
    index = stats_ptr + gg_stats_master[lchip]->policer_stats_base + 2;
    CTC_ERROR_RETURN(sys_goldengate_stats_get_flow_stats(lchip, index, &basic_stats));
    p_stats->policing_violate_pkts = basic_stats.packet_count;
    p_stats->policing_violate_bytes = basic_stats.byte_count;

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_clear_policing_stats(uint8 lchip, uint16 stats_ptr)
{
    uint32 index = 0;

    SYS_STATS_INIT_CHECK(lchip);

    /*green*/
    index = stats_ptr + gg_stats_master[lchip]->policer_stats_base;
    CTC_ERROR_RETURN(sys_goldengate_stats_clear_flow_stats(lchip, index));

    /*yellow*/
    index = stats_ptr + gg_stats_master[lchip]->policer_stats_base + 1;
    CTC_ERROR_RETURN(sys_goldengate_stats_clear_flow_stats(lchip, index));

    /*red*/
    index = stats_ptr + gg_stats_master[lchip]->policer_stats_base + 2;
    CTC_ERROR_RETURN(sys_goldengate_stats_clear_flow_stats(lchip, index));

    return CTC_E_NONE;
}

#define __5_QUEUE_STATS__
int32
sys_goldengate_stats_set_queue_en(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    uint8 value = 0;
    QMgrStatsUpdCtl_m q_mgr_stats_upd_ctl;

    value = enable?0xFF:0;
    sal_memset(&q_mgr_stats_upd_ctl, value, sizeof(q_mgr_stats_upd_ctl));

    cmd = DRV_IOW(QMgrStatsUpdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_stats_upd_ctl));
    return CTC_E_NONE;
}

int32
sys_goldengate_stats_get_queue_en(uint8 lchip, uint8* p_enable)
{
    uint32 cmd = 0;
    QMgrStatsUpdCtl_m q_mgr_stats_upd_ctl;

    sal_memset(&q_mgr_stats_upd_ctl, 0, sizeof(q_mgr_stats_upd_ctl));
    cmd = DRV_IOW(QMgrStatsUpdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_stats_upd_ctl));

    return CTC_E_NONE;

}

int32
sys_goldengate_stats_get_queue_stats(uint8 lchip, uint16 stats_ptr, sys_stats_queue_t* p_stats)
{
    uint32 index = 0;
    uint32 cmd = 0;
    uint32 que_cnt = 0;
    ctc_stats_basic_t enq_stats;
    ctc_stats_basic_t deq_stats;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    sal_memset(&enq_stats, 0, sizeof(ctc_stats_basic_t));
    sal_memset(&deq_stats, 0, sizeof(ctc_stats_basic_t));
    sal_memset(p_stats, 0, sizeof(sys_stats_queue_t));

    /* enq queue */
    index = stats_ptr + gg_stats_master[lchip]->enqueue_stats_base;
    CTC_ERROR_RETURN(sys_goldengate_stats_get_flow_stats(lchip, index, &enq_stats));

    /* dequeue */
    index = stats_ptr + gg_stats_master[lchip]->dequeue_stats_base;
    CTC_ERROR_RETURN(sys_goldengate_stats_get_flow_stats(lchip, index, &deq_stats));
    p_stats->queue_deq_pkts = deq_stats.packet_count;
    p_stats->queue_deq_bytes = deq_stats.byte_count;

    cmd = DRV_IOR(RaQueCnt_t, RaQueCnt_queInstCnt_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, stats_ptr, cmd, &que_cnt));

    if (enq_stats.packet_count >= deq_stats.packet_count + que_cnt)
    {
        p_stats->queue_drop_pkts = enq_stats.packet_count - deq_stats.packet_count;
        p_stats->queue_drop_bytes = enq_stats.byte_count -  deq_stats.byte_count;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_clear_queue_stats(uint8 lchip, uint16 stats_ptr)
{
    uint32 index = 0;
    SYS_STATS_INIT_CHECK(lchip);

    /* dequeue */
    index = stats_ptr + gg_stats_master[lchip]->dequeue_stats_base;
    CTC_ERROR_RETURN(sys_goldengate_stats_clear_flow_stats(lchip, index));

    /* enqueue */
    index = stats_ptr + gg_stats_master[lchip]->enqueue_stats_base;
    CTC_ERROR_RETURN(sys_goldengate_stats_clear_flow_stats(lchip, index));

    return CTC_E_NONE;
}

#define __6_ECMP_STATS__
int32
sys_goldengate_stats_add_ecmp_stats(uint8 lchip, uint32 stats_id, uint16 ecmp_group_id)
{
    sys_goldengate_opf_t opf;
    uint32 offset = 0;
    uint8 dir = 0;
    uint32 stats_base = 0;
    sys_stats_statsid_t sys_stats_id_lk;
    sys_stats_statsid_t* sys_stats_id_rs;
    uint32 ret = CTC_E_NONE;

    SYS_STATS_INIT_CHECK(lchip);

    sal_memset(&sys_stats_id_lk, 0, sizeof(sys_stats_id_lk));
    sal_memset(&opf, 0, sizeof(opf));

    sys_stats_id_lk.stats_id = stats_id;
    sys_stats_id_rs = ctc_hash_lookup(gg_stats_master[lchip]->stats_statsid_hash, &sys_stats_id_lk);
    if (sys_stats_id_rs == NULL)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (sys_stats_id_rs->stats_id_type != CTC_STATS_STATSID_TYPE_ECMP)
    {
        return CTC_E_STATS_STATSID_TYPE_MISMATCH;
    }

    if (sys_stats_id_rs->stats_ptr)
    {
        return CTC_E_STATS_STATSID_ALREADY_IN_USE;
    }

    sys_stats_id_rs->stats_ptr = ecmp_group_id;
    offset = ecmp_group_id;
    CTC_ERROR_RETURN(_sys_goldengate_stats_get_sys_stats_info(lchip, sys_stats_id_rs, &(opf.pool_index), &dir, &stats_base));

    SYS_STATS_DBG_INFO("add ecmp stats stats_ptr:%d\n", offset);

    /* add to fwd stats list */
    _sys_goldengate_stats_fwd_stats_entry_create(lchip, stats_base+offset);

    return ret;
}

int32
sys_goldengate_stats_remove_ecmp_stats(uint8 lchip, uint32 stats_id, uint16 ecmp_group_id)
{
    sys_goldengate_opf_t opf;
    uint32 offset = 0;
    uint8 dir = 0;
    uint32 stats_base = 0;
    sys_stats_statsid_t sys_stats_id_lk;
    sys_stats_statsid_t* sys_stats_id_rs;
    uint32 ret = CTC_E_NONE;

    SYS_STATS_INIT_CHECK(lchip);

    sal_memset(&sys_stats_id_lk, 0, sizeof(sys_stats_id_lk));
    sal_memset(&opf, 0, sizeof(opf));

    sys_stats_id_lk.stats_id = stats_id;
    sys_stats_id_rs = ctc_hash_lookup(gg_stats_master[lchip]->stats_statsid_hash, &sys_stats_id_lk);
    if (sys_stats_id_rs == NULL)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (sys_stats_id_rs->stats_id_type != CTC_STATS_STATSID_TYPE_ECMP)
    {
        return CTC_E_STATS_STATSID_TYPE_MISMATCH;
    }

    sys_stats_id_rs->stats_ptr = 0;
    offset = ecmp_group_id;
    CTC_ERROR_RETURN(_sys_goldengate_stats_get_sys_stats_info(lchip, sys_stats_id_rs, &(opf.pool_index), &dir, &stats_base));

    SYS_STATS_DBG_INFO("delete ecmp stats stats_ptr:%d\n", offset);

    /* add to fwd stats list */
    _sys_goldengate_stats_fwd_stats_entry_delete(lchip, stats_base+offset);

    return ret;
}

#define __7_STATS_GLOBAL_CONFIG__
STATIC int32
_sys_goldengate_stats_set_mac_stats_global_property(uint8 lchip, sys_mac_stats_property_t property, uint32 value)
{

    uint8  gchip = 0;
    uint8  idx1 = 0;
    uint16  idx2 = 0;
    uint16 gport = 0;
    int32 ret = 0;
    sys_datapath_lport_attr_t* p_port = NULL;

    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));

    for (idx1 = 0; idx1 < SYS_MAX_LOCAL_SLICE_NUM; idx1++)
    {
        for (idx2 = 0; idx2 < 256; idx2++)
        {
            ret = sys_goldengate_common_get_port_capability(lchip, SYS_MAP_DRV_LPORT_TO_SYS_LPORT((idx1 * 256) + idx2), &p_port);
            if (ret < 0)
            {
                continue;
            }

            if (p_port->port_type != SYS_DATAPATH_NETWORK_PORT)
            {
                continue;
            }

            gport = CTC_MAP_LPORT_TO_GPORT(gchip, SYS_MAP_DRV_LPORT_TO_SYS_LPORT((idx1 * 256) + idx2));
            _sys_goldengate_stats_set_stats_property(lchip, gport, property, value);
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_set_saturate_en(uint8 lchip, ctc_stats_type_t stats_type, bool enable)
{

    uint32 cmd = 0;
    uint32 tmp = 0;
    SYS_STATS_INIT_CHECK(lchip);
    SYS_STATS_TYPE_CHECK(stats_type);

    tmp = (FALSE == enable) ? 0 : 1;
    SYS_STATS_DBG_FUNC();
    SYS_STATS_DBG_INFO("enable:%d\n", enable);

    switch (stats_type)
    {
    case CTC_STATS_TYPE_FWD:
        /*DS STATS*/
        cmd = DRV_IOW(GlobalStatsCtl_t, GlobalStatsCtl_saturateEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        break;

    case CTC_STATS_TYPE_GMAC:
    case CTC_STATS_TYPE_SGMAC:
    case CTC_STATS_TYPE_XQMAC:
    case CTC_STATS_TYPE_CGMAC:
        PORT_LOCK;
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(\
        _sys_goldengate_stats_set_mac_stats_global_property(lchip, SYS_MAC_STATS_PROPERTY_SATURATE, enable));
        PORT_UNLOCK;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    gg_stats_master[lchip]->saturate_en[stats_type] = tmp;

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_get_saturate_en(uint8 lchip, ctc_stats_type_t stats_type, bool* p_enable)
{
    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);
    SYS_STATS_TYPE_CHECK(stats_type);

    *p_enable = gg_stats_master[lchip]->saturate_en[stats_type];

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_set_hold_en(uint8 lchip, ctc_stats_type_t stats_type, bool enable)
{
    uint32 cmd = 0;
    uint32 tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);
    SYS_STATS_TYPE_CHECK(stats_type);

    tmp = (FALSE == enable) ? 0 : 1;

    SYS_STATS_DBG_FUNC();

    PORT_LOCK;  /* must use port lock to protect from mac operation */

    switch (stats_type)
    {
    case CTC_STATS_TYPE_FWD:
        /*DS STATS*/
        cmd = DRV_IOW(GlobalStatsCtl_t, GlobalStatsCtl_statsHold_f);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &tmp));
        break;

    case CTC_STATS_TYPE_GMAC:
    case CTC_STATS_TYPE_SGMAC:
    case CTC_STATS_TYPE_XQMAC:
    case CTC_STATS_TYPE_CGMAC:
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(\
        _sys_goldengate_stats_set_mac_stats_global_property(lchip, SYS_MAC_STATS_PROPERTY_HOLD, enable));
        break;

    default:
        PORT_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }

    gg_stats_master[lchip]->hold_en[stats_type] = tmp;
    PORT_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_get_hold_en(uint8 lchip, ctc_stats_type_t stats_type, bool* p_enable)
{
    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);
    SYS_STATS_TYPE_CHECK(stats_type);

    *p_enable = gg_stats_master[lchip]->hold_en[stats_type];

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_set_clear_after_read_en(uint8 lchip, ctc_stats_type_t stats_type, bool enable)
{

    uint32 cmd = 0;
    uint32 tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);
    SYS_STATS_TYPE_CHECK(stats_type);

    tmp = (FALSE == enable) ? 0 : 1;

    SYS_STATS_DBG_FUNC();
    SYS_STATS_DBG_INFO("enable:%d\n", enable);

    switch (stats_type)
    {
    case CTC_STATS_TYPE_FWD:
        /*DS STATS*/
        cmd = DRV_IOW(GlobalStatsCtl_t, GlobalStatsCtl_clearOnRead_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        break;

    case CTC_STATS_TYPE_GMAC:
    case CTC_STATS_TYPE_SGMAC:
    case CTC_STATS_TYPE_XQMAC:
    case CTC_STATS_TYPE_CGMAC:
        PORT_LOCK;
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(\
        _sys_goldengate_stats_set_mac_stats_global_property(lchip, SYS_MAC_STATS_PROPERTY_CLEAR, enable));
        PORT_UNLOCK;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    gg_stats_master[lchip]->clear_read_en[stats_type] = tmp;

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_get_clear_after_read_en(uint8 lchip, ctc_stats_type_t stats_type, bool* p_enable)
{
    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);
    SYS_STATS_TYPE_CHECK(stats_type);

    *p_enable = gg_stats_master[lchip]->clear_read_en[stats_type];

    return CTC_E_NONE;
}


#define __8_STATS_INTERRUPT__
int32
_sys_goldengate_stats_get_flow_stats(uint8 lchip, uint16 stats_ptr, ctc_stats_basic_t* p_stats)
{
    uint32 cmd = 0;
    DsStats_m ds_stats;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    sal_memset(&ds_stats, 0, sizeof(ds_stats));
    sal_memset(p_stats, 0, sizeof(ctc_stats_basic_t));

    cmd = DRV_IOR(DsStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, stats_ptr, cmd, &ds_stats));
    _sys_goldengate_stats_ds_stats_to_basic(lchip, ds_stats, p_stats);

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_isr(uint8 lchip, uint32 intr, void* p_data)
{
    uint8  i = 0;
    uint32 cmd = 0;
    uint32 depth = 0;
    uint32 stats_ptr = 0;
    ctc_stats_basic_t stats;
    sys_stats_fwd_stats_t* fwd_stats = NULL;

    SYS_STATS_INIT_CHECK(lchip);
    cmd = DRV_IOR(GlobalStatsSatuAddrFifoDepth_t, GlobalStatsSatuAddrFifoDepth_dsStatsSatuAddrFifoFifoDepth_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &depth));

    SYS_STATS_DBG_INFO("stats isr occur, depth:%d\n", depth);

    /*get stats ptr from fifo*/
    for (i = 0; i < depth; i++)
    {
        cmd = DRV_IOR(GlobalStatsDsStatsSatuAddr_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &stats_ptr));

        SYS_STATS_DBG_INFO("get stats:%d to db\n", stats_ptr);

        /*get stats from stats ptr*/
        CTC_ERROR_RETURN(_sys_goldengate_stats_get_flow_stats(lchip, stats_ptr, &stats));

        CTC_ERROR_RETURN(_sys_goldengate_stats_flow_stats_entry_lookup(lchip, stats_ptr, &fwd_stats));
        if (NULL == fwd_stats)
        {
            fwd_stats = (sys_stats_fwd_stats_t*)mem_malloc(MEM_STATS_MODULE, sizeof(sys_stats_fwd_stats_t));
            if (!fwd_stats)
            {
                return CTC_E_NO_MEMORY;
            }

            fwd_stats->stats_ptr = stats_ptr;
            fwd_stats->packet_count = stats.packet_count;
            fwd_stats->byte_count = stats.byte_count;

            /* add it to flow stats hash */
            ctc_hash_insert(gg_stats_master[lchip]->flow_stats_hash, fwd_stats);
        }
        else
        {
            /*add to db*/
            if (gg_stats_master[lchip]->clear_read_en[CTC_STATS_TYPE_FWD])
            {
                fwd_stats->packet_count += stats.packet_count;
                fwd_stats->byte_count += stats.byte_count;
            }
            else
            {
                fwd_stats->packet_count = stats.packet_count;
                fwd_stats->byte_count = stats.byte_count;
            }
        }
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_stats_log_isr(uint8 lchip, uint32 intr, void* p_data)
{
   uint16 tbl_id = 0;
   uint32 index = 0;
   uint32 cmd  = 0;
   uint16 port = 0;
   uint8 i = 0;
   ctc_stats_basic_t  stats;
   uint16 tbl_array[4] =
   {
        GlobalStatsIpePortLog0StatsSatuAddr_t,
        GlobalStatsIpePortLog1StatsSatuAddr_t,
        GlobalStatsEpePortLog0StatsSatuAddr_t,
        GlobalStatsEpePortLog1StatsSatuAddr_t
   };

   SYS_STATS_INIT_CHECK(lchip);

   /*get stats ptr from fifo*/
   for (i = 0; i < 4; i++)
   {
       tbl_id = tbl_array[i];

       cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
       CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &index));
       port = index + 256*i;

	   CTC_ERROR_RETURN(sys_goldengate_stats_update_port_log_stats(lchip, port, &stats));
   }

   return CTC_E_NONE;
}


int32
sys_goldengate_stats_intr_callback_func(uint8 lchip, uint8* p_gchip)
{
    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_gchip);

    if (TRUE != sys_goldengate_chip_is_local(lchip, *p_gchip))
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_goldengate_stats_isr(lchip, 0, NULL));

    return CTC_E_NONE;
}

#define __9_STATS_STATSID__
STATIC int32
_sys_goldengate_stats_hash_make_statsid(sys_stats_statsid_t* statsid)
{
    return statsid->stats_id;
}

STATIC int32
_sys_goldengate_stats_hash_key_cmp_statsid(sys_stats_statsid_t* statsid_0, sys_stats_statsid_t* statsid_1)
{
    return (statsid_0->stats_id == statsid_1->stats_id);
}

STATIC int32
_sys_goldengate_stats_get_sys_stats_info(uint8 lchip, sys_stats_statsid_t* sw_stats_statsid, uint8* pool_index, uint8* dir, uint32* stats_base)
{
    uint8 tmp_dir = 0;
    uint8 tmp_pool_index = 0;
    uint32 tmp_stats_base = 0;

    if (gg_stats_master[lchip]->cache_mode == 0)
    {
        switch (sw_stats_statsid->stats_id_type)
        {
        case CTC_STATS_STATSID_TYPE_VRF:
            tmp_pool_index = SYS_STATS_CACHE_MODE_EPE_IF_IPE_FWD;
            tmp_dir = 0;
            tmp_stats_base = gg_stats_master[lchip]->epe_if_ipe_fwd_base;
            break;

        case CTC_STATS_STATSID_TYPE_ACL:
            if (sw_stats_statsid->dir == CTC_INGRESS)
            {
                if (sw_stats_statsid->acl_priority == 0)
                {
                    tmp_stats_base = gg_stats_master[lchip]->ipe_acl0_base;
                }
                else if (sw_stats_statsid->acl_priority == 1)
                {
                    tmp_stats_base = gg_stats_master[lchip]->ipe_acl1_base;
                }
                else if (sw_stats_statsid->acl_priority == 2)
                {
                    tmp_stats_base = gg_stats_master[lchip]->ipe_acl2_base;
                }
                else if (sw_stats_statsid->acl_priority == 3)
                {
                    tmp_stats_base = gg_stats_master[lchip]->ipe_acl3_base;
                }
                else
                {
                    return CTC_E_INTR_INVALID_PARAM;
                }
                tmp_pool_index = SYS_STATS_CACHE_MODE_IPE_ACL0 + sw_stats_statsid->acl_priority;
            }
            else if (sw_stats_statsid->dir == CTC_EGRESS)
            {
                tmp_pool_index = SYS_STATS_CACHE_MODE_EPE_ACL0;
                tmp_stats_base = gg_stats_master[lchip]->epe_acl0_base;
            }
            else
            {
                return CTC_E_INTR_INVALID_PARAM;
            }
            tmp_dir = 0;
            break;

        case CTC_STATS_STATSID_TYPE_IPMC:
            tmp_pool_index = SYS_STATS_CACHE_MODE_EPE_IF_IPE_FWD;
            tmp_dir = 1;
            tmp_stats_base = gg_stats_master[lchip]->epe_if_ipe_fwd_base;
            break;

        case CTC_STATS_STATSID_TYPE_MPLS:
            if (sw_stats_statsid->is_vc_label)
            {
                tmp_pool_index = SYS_STATS_CACHE_MODE_EPE_IF_IPE_FWD;
                tmp_dir = 0;
                tmp_stats_base = gg_stats_master[lchip]->epe_if_ipe_fwd_base;
            }
            else
            {
                tmp_pool_index = SYS_STATS_CACHE_MODE_IPE_IF_EPE_FWD;
                tmp_dir = 0;
                tmp_stats_base = gg_stats_master[lchip]->ipe_if_epe_fwd_base;
            }
            break;

        case CTC_STATS_STATSID_TYPE_TUNNEL:
            if (CTC_INGRESS == sw_stats_statsid->dir)
            {
                tmp_pool_index = SYS_STATS_CACHE_MODE_IPE_IF_EPE_FWD;
                tmp_stats_base = gg_stats_master[lchip]->ipe_if_epe_fwd_base;
                tmp_dir = 0;
            }
            else if (CTC_EGRESS == sw_stats_statsid->dir)
            {
                tmp_pool_index = SYS_STATS_CACHE_MODE_EPE_IF_IPE_FWD;
                tmp_stats_base = gg_stats_master[lchip]->epe_if_ipe_fwd_base;
                tmp_dir = 0;
            }
            break;

        case CTC_STATS_STATSID_TYPE_SCL:
            if (CTC_INGRESS == sw_stats_statsid->dir)
            {
                tmp_pool_index = SYS_STATS_CACHE_MODE_IPE_IF_EPE_FWD;
                tmp_dir = 0;
                tmp_stats_base = gg_stats_master[lchip]->ipe_if_epe_fwd_base;
            }
            else if (CTC_EGRESS == sw_stats_statsid->dir)
            {
                tmp_pool_index = SYS_STATS_CACHE_MODE_EPE_IF_IPE_FWD;
                tmp_dir = 0;
                tmp_stats_base = gg_stats_master[lchip]->epe_if_ipe_fwd_base;
            }
            break;

        case CTC_STATS_STATSID_TYPE_NEXTHOP:
            tmp_pool_index = SYS_STATS_CACHE_MODE_EPE_IF_IPE_FWD;
            tmp_dir = 0;
            tmp_stats_base = gg_stats_master[lchip]->epe_if_ipe_fwd_base;
            break;

        case CTC_STATS_STATSID_TYPE_NEXTHOP_MPLS_PW:
            tmp_pool_index = SYS_STATS_CACHE_MODE_EPE_IF_IPE_FWD;
            tmp_dir = 0;
            tmp_stats_base = gg_stats_master[lchip]->epe_if_ipe_fwd_base;
            break;

        case CTC_STATS_STATSID_TYPE_NEXTHOP_MPLS_LSP:
            tmp_pool_index = SYS_STATS_CACHE_MODE_IPE_IF_EPE_FWD;
            tmp_dir = 0;
            tmp_stats_base = gg_stats_master[lchip]->ipe_if_epe_fwd_base;
            break;

        case CTC_STATS_STATSID_TYPE_NEXTHOP_MCAST:
            tmp_pool_index = SYS_STATS_CACHE_MODE_EPE_IF_IPE_FWD;
            tmp_dir = 0;
            tmp_stats_base = gg_stats_master[lchip]->epe_if_ipe_fwd_base;
            break;

        case CTC_STATS_STATSID_TYPE_L3IF:
            if (sw_stats_statsid->dir == CTC_INGRESS)
            {
                tmp_pool_index = SYS_STATS_CACHE_MODE_IPE_IF_EPE_FWD;
                tmp_stats_base = gg_stats_master[lchip]->ipe_if_epe_fwd_base;
            }
            else if (sw_stats_statsid->dir == CTC_EGRESS)
            {
                tmp_pool_index = SYS_STATS_CACHE_MODE_EPE_IF_IPE_FWD;
                tmp_stats_base = gg_stats_master[lchip]->epe_if_ipe_fwd_base;
            }
            else
            {
                return CTC_E_INTR_INVALID_PARAM;
            }
            tmp_dir = 0;
            break;

        case CTC_STATS_STATSID_TYPE_FID:
            if (sw_stats_statsid->dir == CTC_INGRESS)
            {
                tmp_pool_index = SYS_STATS_CACHE_MODE_EPE_IF_IPE_FWD;
                tmp_stats_base = gg_stats_master[lchip]->epe_if_ipe_fwd_base;
                tmp_dir = 1;
            }
            else if (sw_stats_statsid->dir == CTC_EGRESS)
            {
                tmp_pool_index = SYS_STATS_CACHE_MODE_IPE_IF_EPE_FWD;
                tmp_stats_base = gg_stats_master[lchip]->ipe_if_epe_fwd_base;
                tmp_dir = 0;
            }
            else
            {
                return CTC_E_INTR_INVALID_PARAM;
            }
            break;

        case CTC_STATS_STATSID_TYPE_ECMP:
            tmp_stats_base = gg_stats_master[lchip]->epe_if_ipe_fwd_base + SYS_STATS_ECMP_RESERVE_SIZE;
            tmp_pool_index = SYS_STATS_CACHE_MODE_MAX;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        /* If use this mode, default use 14bit opf, if offset alloc fail, not use 13 bit temply.
           Because this mode is not open now. */
        switch (sw_stats_statsid->stats_id_type)
        {
        case CTC_STATS_STATSID_TYPE_VRF:
            tmp_pool_index = SYS_STATS_NO_CACHE_MODE_13BIT_EPE_IF_IPE_FWD_ACL;
            tmp_dir = 0;
            tmp_stats_base = gg_stats_master[lchip]->len_13bit_epe_if_base;
            break;

        case CTC_STATS_STATSID_TYPE_ACL:
            if (sw_stats_statsid->dir == CTC_INGRESS)
            {
                tmp_pool_index = SYS_STATS_NO_CACHE_MODE_14BIT_EPE_IF_ACL;
                tmp_stats_base = gg_stats_master[lchip]->len_14bit_epe_if_base;
                tmp_dir = 0;
            }
            else if (sw_stats_statsid->dir == CTC_EGRESS)   /* epe acl stats only have 10bit and may not support no cache */
            {
                tmp_pool_index = SYS_STATS_NO_CACHE_MODE_13BIT_EPE_IF_IPE_FWD_ACL;
                tmp_stats_base = gg_stats_master[lchip]->len_13bit_epe_if_base;
                tmp_dir = 0;
            }
            else
            {
                return CTC_E_INTR_INVALID_PARAM;
            }
            break;

        case CTC_STATS_STATSID_TYPE_IPMC:
            tmp_pool_index = SYS_STATS_NO_CACHE_MODE_13BIT_EPE_IF_IPE_FWD_ACL;
            tmp_dir = 1;
            tmp_stats_base = gg_stats_master[lchip]->len_13bit_epe_if_base;
            break;

        case CTC_STATS_STATSID_TYPE_MPLS:
            tmp_pool_index = SYS_STATS_NO_CACHE_MODE_14BIT_IPE_IF_EPE_FWD_POLICER;
            tmp_dir = 0;
            tmp_stats_base = gg_stats_master[lchip]->len_14bit_ipe_if_base;
            break;

        case CTC_STATS_STATSID_TYPE_TUNNEL:
            tmp_pool_index = SYS_STATS_NO_CACHE_MODE_14BIT_IPE_IF_EPE_FWD_POLICER;
            tmp_dir = 0;
            tmp_stats_base = gg_stats_master[lchip]->len_14bit_ipe_if_base;
            break;

        case CTC_STATS_STATSID_TYPE_SCL:
            tmp_pool_index = SYS_STATS_NO_CACHE_MODE_14BIT_IPE_IF_EPE_FWD_POLICER;
            tmp_dir = 0;
            tmp_stats_base = gg_stats_master[lchip]->len_14bit_ipe_if_base;
            break;

        case CTC_STATS_STATSID_TYPE_NEXTHOP:
            tmp_pool_index = SYS_STATS_NO_CACHE_MODE_14BIT_EPE_IF_ACL;
            tmp_dir = 0;
            tmp_stats_base = gg_stats_master[lchip]->len_14bit_epe_if_base;
            break;

        case CTC_STATS_STATSID_TYPE_NEXTHOP_MPLS_PW:
            tmp_pool_index = SYS_STATS_NO_CACHE_MODE_14BIT_EPE_IF_ACL;
            tmp_dir = 0;
            tmp_stats_base = gg_stats_master[lchip]->len_14bit_epe_if_base;
            break;

        case CTC_STATS_STATSID_TYPE_NEXTHOP_MPLS_LSP:
            tmp_pool_index = SYS_STATS_NO_CACHE_MODE_14BIT_IPE_IF_EPE_FWD_POLICER;
            tmp_dir = 0;
            tmp_stats_base = gg_stats_master[lchip]->len_14bit_ipe_if_base;
            break;

        case CTC_STATS_STATSID_TYPE_L3IF:
            if (sw_stats_statsid->dir == CTC_INGRESS)
            {
                tmp_pool_index = SYS_STATS_NO_CACHE_MODE_13BIT_IPE_IF_EPE_FWD_POLICER;
                tmp_stats_base = gg_stats_master[lchip]->len_13bit_ipe_if_base;
            }
            else if (sw_stats_statsid->dir == CTC_EGRESS)
            {
                tmp_pool_index = SYS_STATS_NO_CACHE_MODE_13BIT_EPE_IF_IPE_FWD_ACL;
                tmp_stats_base = gg_stats_master[lchip]->len_13bit_epe_if_base;
            }
            else
            {
                return CTC_E_INTR_INVALID_PARAM;
            }
            tmp_dir = 1;
            break;

        case CTC_STATS_STATSID_TYPE_FID:
            if (sw_stats_statsid->dir == CTC_INGRESS)
            {
                tmp_pool_index = SYS_STATS_NO_CACHE_MODE_13BIT_EPE_IF_IPE_FWD_ACL;
                tmp_stats_base = gg_stats_master[lchip]->len_13bit_epe_if_base;
            }
            else if (sw_stats_statsid->dir == CTC_EGRESS)
            {
                tmp_pool_index = SYS_STATS_NO_CACHE_MODE_13BIT_IPE_IF_EPE_FWD_POLICER;
                tmp_stats_base = gg_stats_master[lchip]->len_13bit_ipe_if_base;
            }
            else
            {
                return CTC_E_INTR_INVALID_PARAM;
            }
            tmp_dir = 1;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
    }

    if (pool_index)
    {
        *pool_index = tmp_pool_index;
    }

    if (dir)
    {
        *dir = tmp_dir;
    }

    if (stats_base)
    {
        *stats_base = tmp_stats_base;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_stats_alloc_statsptr_by_statsid(uint8 lchip, ctc_stats_statsid_t statsid, sys_stats_statsid_t* sw_stats_statsid)
{
    sys_goldengate_opf_t opf;
    uint32 offset = 0;
    uint8 dir = 0;
    uint8 type = 0;
    uint32 stats_base = 0;
    uint32 ret = CTC_E_NONE;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    sw_stats_statsid->stats_id = statsid.stats_id;
    sw_stats_statsid->stats_id_type = statsid.type;
    sw_stats_statsid->dir = statsid.dir;
    sw_stats_statsid->acl_priority = statsid.statsid.acl_priority;
    sw_stats_statsid->is_vc_label = statsid.statsid.is_vc_label;

    if (statsid.type == CTC_STATS_STATSID_TYPE_ECMP)
    {
        /* stats_ptr from sys_goldengate_stats_add_ecmp_stats which called by nexthop */
        return CTC_E_NONE;
    }

    opf.pool_type = FLOW_STATS_SRAM;

    CTC_ERROR_RETURN(_sys_goldengate_stats_get_sys_stats_info(lchip, sw_stats_statsid, &(opf.pool_index), &dir, &stats_base));

    opf.reverse = dir;
    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &offset));
    sw_stats_statsid->stats_ptr = offset;
    gg_stats_master[lchip]->used_count[opf.pool_index] ++;
    type = sw_stats_statsid->stats_id_type;
    if (type == CTC_STATS_STATSID_TYPE_L3IF)
    {
        type = (sw_stats_statsid->dir == CTC_EGRESS) ? CTC_STATS_STATSID_TYPE_MAX : type;
    }
    else if (type == CTC_STATS_STATSID_TYPE_SCL)
    {
        type = (sw_stats_statsid->dir == CTC_EGRESS) ? (CTC_STATS_STATSID_TYPE_MAX+4) : type;
    }
    else if (type == CTC_STATS_STATSID_TYPE_FID)
    {
        type = (sw_stats_statsid->dir == CTC_EGRESS) ? (CTC_STATS_STATSID_TYPE_MAX+1) : type;
    }
    else if (type == CTC_STATS_STATSID_TYPE_MPLS)
    {
        type = sw_stats_statsid->is_vc_label ? (CTC_STATS_STATSID_TYPE_MAX+2) : type;
    }
    else if (type == CTC_STATS_STATSID_TYPE_TUNNEL)
    {
        type = (CTC_EGRESS == sw_stats_statsid->dir) ? (CTC_STATS_STATSID_TYPE_MAX+3) : type;
    }
    gg_stats_master[lchip]->type_used_count[type] ++;

    SYS_STATS_DBG_INFO("alloc flow stats stats_ptr:%d\n", stats_base+offset);

    /* add to fwd stats list */
    _sys_goldengate_stats_fwd_stats_entry_create(lchip, stats_base+offset);

    return ret;
}

STATIC int32
_sys_goldengate_stats_free_statsptr_by_statsid(uint8 lchip, sys_stats_statsid_t* sw_stats_statsid)
{
    uint32 offset = 0;
    uint32 stats_base = 0;
    uint32 cmd = 0;
    uint8 type = 0;
    sys_goldengate_opf_t opf;
    DsStats_m ds_stats;
    uint32 ret = CTC_E_NONE;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    sal_memset(&ds_stats, 0, sizeof(ds_stats));

    offset = sw_stats_statsid->stats_ptr;
    opf.pool_type = FLOW_STATS_SRAM;

    if (offset == 0)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_goldengate_stats_get_sys_stats_info(lchip, sw_stats_statsid, &(opf.pool_index), NULL, &stats_base));

    if (opf.pool_index != SYS_STATS_CACHE_MODE_MAX)
    {
        SYS_STATS_DBG_INFO("free flow stats stats_ptr:%d\n", offset);
        CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, offset));
        gg_stats_master[lchip]->used_count[opf.pool_index] --;
        type = sw_stats_statsid->stats_id_type;
        if (type == CTC_STATS_STATSID_TYPE_L3IF)
        {
            type = (sw_stats_statsid->dir == CTC_EGRESS) ? CTC_STATS_STATSID_TYPE_MAX : type;
        }
        else if (type == CTC_STATS_STATSID_TYPE_SCL)
        {
            type = (sw_stats_statsid->dir == CTC_EGRESS) ? (CTC_STATS_STATSID_TYPE_MAX+4) : type;
        }
        else if (type == CTC_STATS_STATSID_TYPE_FID)
        {
            type = (sw_stats_statsid->dir == CTC_EGRESS) ? (CTC_STATS_STATSID_TYPE_MAX+1) : type;
        }
        else if (type == CTC_STATS_STATSID_TYPE_MPLS)
        {
            type = sw_stats_statsid->is_vc_label ? (CTC_STATS_STATSID_TYPE_MAX+2) : type;
        }
        else if (type == CTC_STATS_STATSID_TYPE_TUNNEL)
        {
            type = (CTC_EGRESS == sw_stats_statsid->dir) ? (CTC_STATS_STATSID_TYPE_MAX+3) : type;
        }
        gg_stats_master[lchip]->type_used_count[type] --;
    }

    /*  remove from fwd stats list */
    _sys_goldengate_stats_fwd_stats_entry_delete(lchip, stats_base+offset);

    /* clear hardware stats */
    cmd = DRV_IOW(DsStats_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, stats_base+offset, cmd, &ds_stats);

    return ret;
}

int32
sys_goldengate_stats_get_statsptr(uint8 lchip, uint32 stats_id, uint8 stats_type, uint16* stats_ptr)
{
    sys_stats_statsid_t sys_stats_id_lk;
    sys_stats_statsid_t* sys_stats_id_rs;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(stats_ptr);

    sal_memset(&sys_stats_id_lk, 0, sizeof(sys_stats_id_lk));
    sys_stats_id_lk.stats_id = stats_id;
    sys_stats_id_rs = ctc_hash_lookup(gg_stats_master[lchip]->stats_statsid_hash, &sys_stats_id_lk);
    if (sys_stats_id_rs == NULL)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (sys_stats_id_rs->stats_id_type != stats_type)
    {
        return CTC_E_STATS_STATSID_TYPE_MISMATCH;
    }

    *stats_ptr = sys_stats_id_rs->stats_ptr;
    return CTC_E_NONE;
}
int32
sys_goldengate_stats_create_statsid(uint8 lchip, ctc_stats_statsid_t* statsid)
{
    ctc_stats_statsid_t sys_stats_id_user;
    sys_stats_statsid_t sys_stats_id_lk;
    sys_stats_statsid_t* sys_stats_id_rs;
    sys_stats_statsid_t* sys_stats_id_insert;
    sys_goldengate_opf_t opf;
    int32 ret = CTC_E_NONE;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(statsid);

    if (!(gg_stats_master[lchip]->stats_bitmap & CTC_STATS_ECMP_STATS)
        && statsid->type == CTC_STATS_STATSID_TYPE_ECMP)
    {
        return CTC_E_STATS_NOT_ENABLE;
    }

    sal_memset(&sys_stats_id_lk, 0, sizeof(sys_stats_id_lk));

    sys_stats_id_insert = mem_malloc(MEM_STATS_MODULE,  sizeof(sys_stats_statsid_t));
    if (sys_stats_id_insert == NULL)
    {
        return CTC_E_NO_MEMORY;
    }

    /*statsid maybe alloc*/
    if (gg_stats_master[lchip]->stats_mode == CTC_STATS_MODE_USER)
    {
        if (statsid->stats_id < 1)
        {
            mem_free(sys_stats_id_insert);
            return CTC_E_INVALID_PARAM;
        }
        sys_stats_id_lk.stats_id = statsid->stats_id;
        sys_stats_id_rs = ctc_hash_lookup(gg_stats_master[lchip]->stats_statsid_hash, &sys_stats_id_lk);
        if (sys_stats_id_rs != NULL)
        {
            mem_free(sys_stats_id_insert);
            return CTC_E_ENTRY_EXIST;
        }
    }
    else
    {
        sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
        opf.pool_index = 0;
        opf.pool_type  = OPF_STATS_STATSID;

        ret = sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &(statsid->stats_id));
        if (ret != CTC_E_NONE)
        {
            mem_free(sys_stats_id_insert);
            return ret;
        }
        sys_stats_id_lk.stats_id = statsid->stats_id;
    }

    /*stats alloc by statsid*/
    sal_memcpy(&sys_stats_id_user, statsid, sizeof(ctc_stats_statsid_t));
    ret = _sys_goldengate_stats_alloc_statsptr_by_statsid(lchip, sys_stats_id_user, &sys_stats_id_lk);
    if (ret != CTC_E_NONE)
    {
        if (gg_stats_master[lchip]->stats_mode == CTC_STATS_MODE_DEFINE)
        {
            sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
            opf.pool_index = 0;
            opf.pool_type  = OPF_STATS_STATSID;

            sys_goldengate_opf_free_offset(lchip, &opf, 1, statsid->stats_id);
        }
        mem_free(sys_stats_id_insert);
        return ret;
    }

    /*statsid and stats sw*/
    sal_memcpy(sys_stats_id_insert, &sys_stats_id_lk, sizeof(sys_stats_statsid_t));
    ctc_hash_insert(gg_stats_master[lchip]->stats_statsid_hash, sys_stats_id_insert);

    return ret;
}

int32
sys_goldengate_stats_destroy_statsid(uint8 lchip, uint32 stats_id)
{
    sys_goldengate_opf_t opf;
    sys_stats_statsid_t sys_stats_id_lk;
    sys_stats_statsid_t* sys_stats_id_rs;
    uint32 ret = CTC_E_NONE;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_MIN_VALUE_CHECK(stats_id, 1);

    sal_memset(&sys_stats_id_lk, 0, sizeof(sys_stats_id_lk));
    sys_stats_id_lk.stats_id = stats_id;
    sys_stats_id_rs = ctc_hash_lookup(gg_stats_master[lchip]->stats_statsid_hash, &sys_stats_id_lk);
    if (sys_stats_id_rs == NULL)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    ctc_hash_remove(gg_stats_master[lchip]->stats_statsid_hash, sys_stats_id_rs);

    _sys_goldengate_stats_free_statsptr_by_statsid(lchip, sys_stats_id_rs);

    mem_free(sys_stats_id_rs);

    if (gg_stats_master[lchip]->stats_mode == CTC_STATS_MODE_DEFINE)
    {
        sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
        opf.pool_index = 0;
        opf.pool_type  = OPF_STATS_STATSID;

        CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, stats_id));
    }

    return ret;
}

int32
sys_goldengate_stats_get_type_by_statsid(uint8 lchip, uint32 stats_id, sys_stats_statsid_t* p_stats_type)
{
    sys_stats_statsid_t sys_stats_id_lk;
    sys_stats_statsid_t* sys_stats_id_rs;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_MIN_VALUE_CHECK(stats_id, 1);
    CTC_PTR_VALID_CHECK(p_stats_type);

    sal_memset(&sys_stats_id_lk, 0, sizeof(sys_stats_id_lk));
    sys_stats_id_lk.stats_id = stats_id;
    sys_stats_id_rs = ctc_hash_lookup(gg_stats_master[lchip]->stats_statsid_hash, &sys_stats_id_lk);
    if (sys_stats_id_rs == NULL)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    sal_memcpy(p_stats_type, sys_stats_id_rs, sizeof(sys_stats_statsid_t));

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_get_stats(uint8 lchip, uint32 stats_id, ctc_stats_basic_t* p_stats)
{
    sys_stats_statsid_t sys_stats_id_lk;
    sys_stats_statsid_t* sys_stats_id_rs;
    ctc_stats_basic_t stats;
    uint32 stats_base = 0;
    uint32 ret = CTC_E_NONE;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);
    CTC_MIN_VALUE_CHECK(stats_id, 1);
    sal_memset(p_stats, 0, sizeof(ctc_stats_basic_t));
    sal_memset(&stats, 0, sizeof(ctc_stats_basic_t));
    sal_memset(&sys_stats_id_lk, 0, sizeof(sys_stats_id_lk));

    sys_stats_id_lk.stats_id = stats_id;
    sys_stats_id_rs = ctc_hash_lookup(gg_stats_master[lchip]->stats_statsid_hash, &sys_stats_id_lk);
    if (sys_stats_id_rs == NULL)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    CTC_ERROR_RETURN(_sys_goldengate_stats_get_sys_stats_info(lchip, sys_stats_id_rs, NULL, NULL, &stats_base));

    CTC_ERROR_RETURN(sys_goldengate_stats_get_flow_stats(lchip, stats_base+sys_stats_id_rs->stats_ptr, &stats));
    p_stats->byte_count += stats.byte_count;
    p_stats->packet_count += stats.packet_count;

    return ret;
}

int32
sys_goldengate_stats_clear_stats(uint8 lchip, uint32 stats_id)
{
    sys_stats_statsid_t sys_stats_id_lk;
    sys_stats_statsid_t* sys_stats_id_rs;
    uint32 stats_base = 0;
    uint32 ret = CTC_E_NONE;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_MIN_VALUE_CHECK(stats_id, 1);

    sal_memset(&sys_stats_id_lk, 0, sizeof(sys_stats_id_lk));
    sys_stats_id_lk.stats_id = stats_id;
    sys_stats_id_rs = ctc_hash_lookup(gg_stats_master[lchip]->stats_statsid_hash, &sys_stats_id_lk);
    if (sys_stats_id_rs == NULL)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    CTC_ERROR_RETURN(_sys_goldengate_stats_get_sys_stats_info(lchip, sys_stats_id_rs, NULL, NULL, &stats_base));

    CTC_ERROR_RETURN(sys_goldengate_stats_clear_flow_stats(lchip, stats_base+sys_stats_id_rs->stats_ptr));

    return ret;
}

#define __10_STATS_DEBUG_SHOW__
int32
sys_goldengate_stats_show_status(uint8 lchip)
{
    uint16 ecmp_stats_num = 0;

    SYS_STATS_INIT_CHECK(lchip);

    if (gg_stats_master[lchip]->stats_bitmap & CTC_STATS_ECMP_STATS)
    {
        ecmp_stats_num = 1024;
    }

    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------Flow Stats Resource-----------------\n");
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%14s     %4s       %s\n", "Stats Type", " ", "Size", "Allocated");
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------------------------------------------\n");
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%19s     %4d       %s\n", "Queue", " ", SYS_STATS_DEQUEUE_STATS_SIZE+SYS_STATS_ENQUEUE_STATS_SIZE, "-");
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%17s     %4d       %d\n", "Policer", " ", SYS_STATS_POLICER_STATS_SIZE-1, gg_stats_master[lchip]->used_count[SYS_STATS_CACHE_MODE_POLICER]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%20s     %4d       %s\n", "ECMP", " ", ecmp_stats_num-1, "-");
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%12s     %4d       %d\n", "ACL0 Ingress", " ", SYS_STATS_ACL0_STATS_SIZE-1, gg_stats_master[lchip]->used_count[SYS_STATS_CACHE_MODE_IPE_ACL0]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%12s     %4d       %d\n", "ACL1 Ingress", " ", SYS_STATS_ACL1_STATS_SIZE-1, gg_stats_master[lchip]->used_count[SYS_STATS_CACHE_MODE_IPE_ACL1]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%12s     %4d       %d\n", "ACL2 Ingress", " ", SYS_STATS_ACL2_STATS_SIZE-1, gg_stats_master[lchip]->used_count[SYS_STATS_CACHE_MODE_IPE_ACL2]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%12s     %4d       %d\n", "ACL3 Ingress", " ", SYS_STATS_ACL3_STATS_SIZE-1, gg_stats_master[lchip]->used_count[SYS_STATS_CACHE_MODE_IPE_ACL3]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%13s     %4d       %d\n", "ACL0 Egress", " ", SYS_STATS_EGS_ACL0_STATS_SIZE-1, gg_stats_master[lchip]->used_count[SYS_STATS_CACHE_MODE_EPE_ACL0]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------Stats Share Pool1(size %04d)--------\n", SYS_STATS_IPE_FWD_STATS_SIZE-ecmp_stats_num-1);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%21s     %4s       %d\n", "VRF", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_VRF]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%20s     %4s       %d\n", "IPMC", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_IPMC]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%11s     %4s       %d\n", "MPLS VC label", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_MAX+2]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%11s     %4s       %d\n", "TUNNEL Egress", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_MAX+3]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%14s     %4s       %d\n", "SCL Egress", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_MAX+4]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%17s     %4s       %d\n", "Nexthop", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_NEXTHOP]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%9s     %4s       %d\n", "Nexthop MPLS PW", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_NEXTHOP_MPLS_PW]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%11s     %4s       %d\n", "Nexthop Mcast", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_NEXTHOP_MCAST]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%13s     %4s       %d\n", "L3IF Egress", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_MAX]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%13s     %4s       %d\n", "FID Ingress", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_FID]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------Stats Share Pool2(size %04d)--------\n", SYS_STATS_IPE_IF_STATS_SIZE-1);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%7s     %4s       %d\n", "MPLS TUNNEL Label", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_MPLS]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%10s     %4s       %d\n", "TUNNEL Ingress", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_TUNNEL]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%13s     %4s       %d\n", "SCL Ingress", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_SCL]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%8s     %4s       %d\n", "Nexthop MPLS LSP", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_NEXTHOP_MPLS_LSP]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%12s     %4s       %d\n", "L3IF Ingress", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_L3IF]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s%14s     %4s       %d\n", "FID Egress", " ", " ", gg_stats_master[lchip]->type_used_count[CTC_STATS_STATSID_TYPE_MAX+1]);
    SYS_STATS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------------------------------------------\n");

    return CTC_E_NONE;
}


#define ___________STATS_INIT________________________

STATIC int32
_sys_goldengate_stats_init_start(uint8 lchip)
{
    /* Now, datapath init the register GlobalStatsInit and GlobalStatsInitDone*/
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_deinit_start(uint8 lchip)
{
    /* no need to do deinit */
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stats_init_done(uint8 lchip)
{
    drv_work_platform_type_t platform_type = MAX_WORK_PLATFORM;
    uint32 cmd = 0;
    uint32 tmp = 0;

    drv_goldengate_get_platform_type(&platform_type);

    if (platform_type == HW_PLATFORM)
    {
        /* GMAC to decide whether init has done */

        /* SGMAC to decide whether init has done */

        /* global stats: read  GlobalStatsInitDone_t initdone to decide whether init has done */
        cmd = DRV_IOR(GlobalStatsInitDone_t, GlobalStatsInitDone_edramInitDone_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        if (0 == tmp)
        {
            return CTC_E_NOT_INIT;
        }

        cmd = DRV_IOR(GlobalStatsInitDone_t, GlobalStatsInitDone_initDone_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        if (0 == tmp)
        {
            return CTC_E_NOT_INIT;
        }
    }

    return CTC_E_NONE;
}

STATIC void
_sys_goldengate_stats_mac_stats_init(uint8 lchip)
{
    uint32 cmd = 0, idx = 0;
    tbls_id_t tblid = MaxTblId_t;
    XqmacStatsCfg0_m XqmacStatsCfg;
    CgmacStatsCfg0_m CgmacStatsCfg;

    sal_memset(&XqmacStatsCfg, 0, sizeof(XqmacStatsCfg0_m));
    SetXqmacStatsCfg0(V, clearOnRead_f,          &XqmacStatsCfg, 1);
    SetXqmacStatsCfg0(V, ge64BPktHiPri_f,        &XqmacStatsCfg, 1);
    SetXqmacStatsCfg0(V, incrHold_f,             &XqmacStatsCfg, 0);
    SetXqmacStatsCfg0(V, incrSaturate_f,         &XqmacStatsCfg, 1);
    SetXqmacStatsCfg0(V, packetLenMtu1_f,        &XqmacStatsCfg, 0x5EE);
    SetXqmacStatsCfg0(V, packetLenMtu2_f,        &XqmacStatsCfg, 0x600);
    SetXqmacStatsCfg0(V, statsOverWriteEnable_f, &XqmacStatsCfg, 0);

    for (tblid = XqmacStatsCfg0_t; tblid <= XqmacStatsCfg23_t; tblid++)
    {
        cmd = DRV_IOW(tblid, DRV_ENTRY_FLAG);
        for (idx = 0; idx < TABLE_MAX_INDEX(tblid); idx++)
        {
            DRV_IOCTL(lchip, idx, cmd, &XqmacStatsCfg);
        }
    }

    sal_memset(&CgmacStatsCfg, 0, sizeof(CgmacStatsCfg0_m));
    SetCgmacStatsCfg0(V, clearOnRead_f,          &CgmacStatsCfg, 1);
    SetCgmacStatsCfg0(V, ge64BPktHiPri_f,        &CgmacStatsCfg, 1);
    SetCgmacStatsCfg0(V, incrHold_f,             &CgmacStatsCfg, 0);
    SetCgmacStatsCfg0(V, incrSaturate_f,         &CgmacStatsCfg, 1);
    SetCgmacStatsCfg0(V, packetLenMtu1_f,        &CgmacStatsCfg, 0x5EE);
    SetCgmacStatsCfg0(V, packetLenMtu2_f,        &CgmacStatsCfg, 0x600);
    SetCgmacStatsCfg0(V, statsOverWriteEnable_f, &CgmacStatsCfg, 0);

    for (tblid = CgmacStatsCfg0_t; tblid <= CgmacStatsCfg3_t; tblid++)
    {
        cmd = DRV_IOW(tblid, DRV_ENTRY_FLAG);
        for (idx = 0; idx < TABLE_MAX_INDEX(tblid); idx++)
        {
            DRV_IOCTL(lchip, idx, cmd, &CgmacStatsCfg);
        }
    }

    return;
}

STATIC int32
_sys_goldengate_stats_statsid_init(uint8 lchip)
{
    uint32 max_stats_index = 0;
    sys_goldengate_opf_t opf;
    uint32 start_offset = 0;
    uint32 entry_num    = 0;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    sys_goldengate_ftm_query_table_entry_num(lchip, DsStats_t,  &max_stats_index);
    if (0 == max_stats_index)
    {
        return CTC_E_NO_RESOURCE;
    }
    #define SYS_STATS_HASH_BLOCK_SIZE           1024
    gg_stats_master[lchip]->stats_statsid_hash = ctc_hash_create(1,
                                        SYS_STATS_HASH_BLOCK_SIZE,
                                        (hash_key_fn) _sys_goldengate_stats_hash_make_statsid,
                                        (hash_cmp_fn) _sys_goldengate_stats_hash_key_cmp_statsid);

    if (gg_stats_master[lchip]->stats_mode == CTC_STATS_MODE_DEFINE)
    {
        opf.pool_type = OPF_STATS_STATSID;
        start_offset  = 1;
        entry_num     = CTC_STATS_MAX_STATSID;
        CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_STATS_STATSID, 1));
        CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, start_offset, (entry_num-1)));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_stats_wb_mapping_statsid(uint8 lchip, sys_wb_stats_statsid_t *p_wb_statsid, sys_stats_statsid_t *p_statsid, uint8 sync)
{
    uint32 stats_base = 0;
    int32 ret = CTC_E_NONE;
    sys_goldengate_opf_t opf;

    if (sync)
    {
        p_wb_statsid->stats_id = p_statsid->stats_id;
        p_wb_statsid->stats_ptr = p_statsid->stats_ptr;
        p_wb_statsid->stats_id_type = p_statsid->stats_id_type;
        p_wb_statsid->dir = p_statsid->dir;
        p_wb_statsid->acl_priority = p_statsid->acl_priority;
        p_wb_statsid->is_vc_label = p_statsid->is_vc_label;
    }
    else
    {
        p_statsid->stats_id = p_wb_statsid->stats_id;
        p_statsid->stats_ptr = p_wb_statsid->stats_ptr;
        p_statsid->stats_id_type = p_wb_statsid->stats_id_type;
        p_statsid->dir = p_wb_statsid->dir;
        p_statsid->acl_priority = p_wb_statsid->acl_priority;
        p_statsid->is_vc_label = p_wb_statsid->is_vc_label;

        /*statsid maybe alloc*/
        if (gg_stats_master[lchip]->stats_mode != CTC_STATS_MODE_USER)
        {
            sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
            opf.pool_index = 0;
            opf.pool_type  = OPF_STATS_STATSID;

            ret = sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, p_statsid->stats_id);
            if (ret != CTC_E_NONE)
            {
                return ret;
            }
        }

        if (p_statsid->stats_id_type == CTC_STATS_STATSID_TYPE_ECMP)
        {
            /* stats_ptr from sys_goldengate_stats_add_ecmp_stats which called by nexthop */
            return CTC_E_NONE;
        }

        sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
        opf.pool_type = FLOW_STATS_SRAM;

        CTC_ERROR_RETURN(_sys_goldengate_stats_get_sys_stats_info(lchip, p_statsid, &(opf.pool_index), NULL, &stats_base));

        CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, p_statsid->stats_ptr));

        /* add to fwd stats list */
        _sys_goldengate_stats_fwd_stats_entry_create(lchip, stats_base + p_statsid->stats_ptr);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_stats_wb_sync_statsid(sys_stats_statsid_t *p_statsid, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_wb_stats_statsid_t  *p_wb_statsid;
    sys_traverse_t *data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)(data->data);
    uint8 lchip = (uint8)(data->value1);

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_statsid = (sys_wb_stats_statsid_t *)wb_data->buffer + wb_data->valid_cnt;
    sal_memset(p_wb_statsid, 0, sizeof(sys_wb_stats_statsid_t));
    CTC_ERROR_RETURN(_sys_goldengate_stats_wb_mapping_statsid(lchip, p_wb_statsid, p_statsid, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_wb_sync(uint8 lchip)
{
    uint32 loop = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_traverse_t user_data;
    sys_wb_stats_master_t  *p_wb_gg_stats_master;

    /*syncup  stats_matser*/
    wb_data.buffer = mem_malloc(MEM_STATS_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_stats_master_t, CTC_FEATURE_STATS, SYS_WB_APPID_STATS_SUBID_MASTER);

    p_wb_gg_stats_master = (sys_wb_stats_master_t  *)wb_data.buffer;

    p_wb_gg_stats_master->lchip = lchip;

    for ( loop = 0; loop < CTC_STATS_TYPE_MAX; loop++)
    {
        p_wb_gg_stats_master->saturate_en[loop] = gg_stats_master[lchip]->saturate_en[loop];
        p_wb_gg_stats_master->hold_en[loop] = gg_stats_master[lchip]->hold_en[loop];
    }

    for ( loop = 0; loop < SYS_STATS_CACHE_MODE_MAX; loop++)
    {
        p_wb_gg_stats_master->used_count[loop] = gg_stats_master[lchip]->used_count[loop];
    }

    for ( loop = 0; loop < CTC_STATS_TYPE_USED_COUNT_MAX; loop++)
    {
        p_wb_gg_stats_master->type_used_count[loop] = gg_stats_master[lchip]->type_used_count[loop];
    }

    wb_data.valid_cnt = 1;
    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);

    /*syncup  statsid*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_stats_statsid_t, CTC_FEATURE_STATS, SYS_WB_APPID_STATS_SUBID_STATSID);
    user_data.data = &wb_data;
    user_data.value1 = lchip;

    wb_data.valid_cnt = 0;
    CTC_ERROR_GOTO(ctc_hash_traverse(gg_stats_master[lchip]->stats_statsid_hash, (hash_traversal_fn) _sys_goldengate_stats_wb_sync_statsid, (void *)&user_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }

    return ret;
}

int32
sys_goldengate_stats_wb_restore(uint8 lchip)
{
    uint32 loop = 0;
    uint16 entry_cnt = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_query_t wb_query;
    sys_stats_statsid_t  *p_statsid;
    sys_wb_stats_statsid_t  *p_wb_statsid;
    sys_wb_stats_master_t  *p_wb_gg_stats_master;

    sal_memset(&wb_query, 0, sizeof(ctc_wb_query_t));
    wb_query.buffer = mem_malloc(MEM_STATS_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_stats_master_t, CTC_FEATURE_STATS, SYS_WB_APPID_STATS_SUBID_MASTER);

    CTC_ERROR_GOTO(ctc_wb_query_entry(&wb_query), ret, done);

    /*restore  gg_stats_master*/
    if (wb_query.valid_cnt != 1 || wb_query.is_end != 1)
    {
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "query stats master error! valid_cnt: %d, is_end: %d.\n", wb_query.valid_cnt, wb_query.is_end);
        goto done;
    }

    p_wb_gg_stats_master = (sys_wb_stats_master_t *)wb_query.buffer;

    for ( loop = 0; loop < CTC_STATS_TYPE_MAX; loop++)
    {
        gg_stats_master[lchip]->saturate_en[loop] = p_wb_gg_stats_master->saturate_en[loop];
        gg_stats_master[lchip]->hold_en[loop] = p_wb_gg_stats_master->hold_en[loop];
    }

    for ( loop = 0; loop < SYS_STATS_CACHE_MODE_MAX; loop++)
    {
        gg_stats_master[lchip]->used_count[loop] = p_wb_gg_stats_master->used_count[loop];
    }

    for ( loop = 0; loop < CTC_STATS_TYPE_USED_COUNT_MAX; loop++)
    {
        gg_stats_master[lchip]->type_used_count[loop] = p_wb_gg_stats_master->type_used_count[loop];
    }

    /*restore  stats_statsid*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_stats_statsid_t, CTC_FEATURE_STATS, SYS_WB_APPID_STATS_SUBID_STATSID);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_statsid = (sys_wb_stats_statsid_t *)wb_query.buffer + entry_cnt++;

        p_statsid = mem_malloc(MEM_STATS_MODULE, sizeof(sys_stats_statsid_t));
        if (NULL == p_statsid)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_statsid, 0, sizeof(sys_stats_statsid_t));

        ret = _sys_goldengate_stats_wb_mapping_statsid(lchip, p_wb_statsid, p_statsid, 0);
        if (ret)
        {
            continue;
        }

        /*add to soft table*/
        ctc_hash_insert(gg_stats_master[lchip]->stats_statsid_hash, p_statsid);
    CTC_WB_QUERY_ENTRY_END((&wb_query));

done:
    if (wb_query.key)
    {
        mem_free(wb_query.key);
    }

    if (wb_query.buffer)
    {
        mem_free(wb_query.buffer);
    }

    return ret;
}

int32
sys_goldengate_stats_init(uint8 lchip, ctc_stats_global_cfg_t* stats_global_cfg)
{
    uint32 cmd = 0;
    int32 ret = CTC_E_NONE;
    sys_goldengate_opf_t opf;
    uint32 size = 0;
    uint32 tmp_size = 0;
    uint32 max_stats_index = 0;
    uint32 step = 8192;
    uint32 value = 0;
    uint16 ecmp_stats_num = 0;
    GlobalStatsSatuInterruptThreshold_m threshold;

    LCHIP_CHECK(lchip);

    sal_memset(&threshold, 0, sizeof(threshold));

    /*init global variable*/
    if (NULL != gg_stats_master[lchip])
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_goldengate_stats_init_start(lchip));

    sys_goldengate_ftm_query_table_entry_num(lchip, DsStats_t,  &max_stats_index);

    if (0 == max_stats_index)
    {
        return CTC_E_NO_RESOURCE;
    }

    MALLOC_POINTER(sys_stats_master_t, gg_stats_master[lchip]);
    if (NULL == gg_stats_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(gg_stats_master[lchip], 0, sizeof(sys_stats_master_t));

    sal_mutex_create(&(gg_stats_master[lchip]->p_stats_mutex));
    gg_stats_master[lchip]->stats_stored_in_sw = 1;   /* default use db */
    gg_stats_master[lchip]->cache_mode = 0;   /* default use cache */
    gg_stats_master[lchip]->stats_bitmap = stats_global_cfg->stats_bitmap;
    gg_stats_master[lchip]->stats_mode = stats_global_cfg->stats_mode;
    gg_stats_master[lchip]->query_mode = stats_global_cfg->query_mode;

    /* flow stats hash init */
    gg_stats_master[lchip]->flow_stats_hash = ctc_hash_create(1, SYS_STATS_FLOW_ENTRY_HASH_BLOCK_SIZE,
                                           (hash_key_fn)_sys_goldengate_stats_hash_key_make,
                                           (hash_cmp_fn)_sys_goldengate_stats_hash_key_cmp);


    gg_stats_master[lchip]->port_log_vec = ctc_vector_init((SYS_GG_MAX_PORT_NUM_PER_CHIP*2)/64, 64);

    /* default stats interrupt configuration */
    SetGlobalStatsSatuInterruptThreshold(V, satuAddrFifoDepthThreshold_f, &threshold, SYS_STATS_DEFAULT_FIFO_DEPTH_THRESHOLD);
    SetGlobalStatsSatuInterruptThreshold(V, byteCntThresholdHi_f, &threshold, SYS_STATS_DEFAULT_BYTE_THRESHOLD_HI);
    SetGlobalStatsSatuInterruptThreshold(V, pktCntThresholdHi_f, &threshold, SYS_STATS_DEFAULT_PACKET_THRESHOLD_HI);
    SetGlobalStatsSatuInterruptThreshold(V, byteCntThresholdLo_f, &threshold, SYS_STATS_DEFAULT_BYTE_THRESHOLD);
    SetGlobalStatsSatuInterruptThreshold(V, pktCntThresholdLo_f, &threshold, SYS_STATS_DEFAULT_PACKET_THRESHOLD);


    cmd = DRV_IOW(GlobalStatsSatuInterruptThreshold_t, DRV_ENTRY_FLAG);
    if ((ret = DRV_IOCTL(lchip, 0, cmd, &threshold)) < 0)
    {
        goto error;
    }

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = FLOW_STATS_SRAM;

    if (gg_stats_master[lchip]->cache_mode == 0)  /* cache mode */
    {
        if ((ret = sys_goldengate_opf_init(lchip, FLOW_STATS_SRAM,  SYS_STATS_CACHE_MODE_MAX)) < 0)
        {
            goto error;
        }

        /* global stats pool init */
        size = 0;

        /* Dequeue, Drop queue 4K */
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_dequeueStatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }

        gg_stats_master[lchip]->dequeue_stats_base = size;
        size += SYS_STATS_DEQUEUE_STATS_SIZE;

        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_enqueueStatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        gg_stats_master[lchip]->enqueue_stats_base = size;
        size += SYS_STATS_ENQUEUE_STATS_SIZE;

        /* Policer 4K */
        gg_stats_master[lchip]->policer_stats_base = size;
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_policingStatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        size += SYS_STATS_POLICER_STATS_SIZE;

        opf.pool_index = SYS_STATS_CACHE_MODE_POLICER;
        if ((ret = sys_goldengate_opf_init_offset(lchip, &opf, 1,  SYS_STATS_POLICER_STATS_SIZE-1)) < 0)
        {
            goto error;
        }

        /* ipe intf stats & epe fwd stats 8K */
        gg_stats_master[lchip]->ipe_if_epe_fwd_base = size;
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_ipeIntfStatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_epeFwdStatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        size += SYS_STATS_IPE_IF_STATS_SIZE;

        opf.pool_index = SYS_STATS_CACHE_MODE_IPE_IF_EPE_FWD;
        if ((ret = sys_goldengate_opf_init_offset(lchip, &opf, 1,  SYS_STATS_IPE_IF_STATS_SIZE-1)) < 0)
        {
            goto error;
        }

        /* ipe fwd stats & epe intf stats 8K */
        gg_stats_master[lchip]->epe_if_ipe_fwd_base = size;
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_ipeFwdStatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_epeIntfStatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        size += SYS_STATS_IPE_FWD_STATS_SIZE;

        if (stats_global_cfg->stats_bitmap & CTC_STATS_ECMP_STATS)  /* reserve 1K stats for ecmp */
        {
            ecmp_stats_num = 1023;
            tmp_size = SYS_STATS_IPE_FWD_STATS_SIZE - 1024 - 1;
            cmd = DRV_IOR(IpePktProcReserved2_t, IpePktProcReserved2_reserved_f);
            if ((ret = DRV_IOCTL(lchip, 0, cmd, &value)) < 0)
            {
                goto error;
            }
            value |= 0x7;
            cmd = DRV_IOW(IpePktProcReserved2_t, IpePktProcReserved2_reserved_f);
            if ((ret = DRV_IOCTL(lchip, 0, cmd, &value)) < 0)
            {
                goto error;
            }
        }
        else
        {
            tmp_size = SYS_STATS_IPE_FWD_STATS_SIZE-1;
        }

        opf.pool_index = SYS_STATS_CACHE_MODE_EPE_IF_IPE_FWD;
        if ((ret = sys_goldengate_opf_init_offset(lchip, &opf, 1,  tmp_size)) < 0)
        {
            goto error;
        }

        /* acl0 4K */
        gg_stats_master[lchip]->ipe_acl0_base = size;
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_ipeAcl0StatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        size += SYS_STATS_ACL0_STATS_SIZE;

        opf.pool_index = SYS_STATS_CACHE_MODE_IPE_ACL0;
        if ((ret = sys_goldengate_opf_init_offset(lchip, &opf, 1,  SYS_STATS_ACL0_STATS_SIZE-1)) < 0)
        {
            goto error;
        }

        /* acl1 512 */
        gg_stats_master[lchip]->ipe_acl1_base = size;
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_ipeAcl1StatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        size += SYS_STATS_ACL1_STATS_SIZE;

        opf.pool_index = SYS_STATS_CACHE_MODE_IPE_ACL1;
        if ((ret = sys_goldengate_opf_init_offset(lchip, &opf, 1,  SYS_STATS_ACL1_STATS_SIZE-1)) < 0)
        {
            goto error;
        }

        /* acl2 256 */
        gg_stats_master[lchip]->ipe_acl2_base = size;
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_ipeAcl2StatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        size += SYS_STATS_ACL2_STATS_SIZE;

        opf.pool_index = SYS_STATS_CACHE_MODE_IPE_ACL2;
        if ((ret = sys_goldengate_opf_init_offset(lchip, &opf, 1,  SYS_STATS_ACL2_STATS_SIZE-1)) < 0)
        {
            goto error;
        }

        /* acl3 256 */
        gg_stats_master[lchip]->ipe_acl3_base = size;
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_ipeAcl3StatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        size += SYS_STATS_ACL3_STATS_SIZE;

        opf.pool_index = SYS_STATS_CACHE_MODE_IPE_ACL3;
        if ((ret = sys_goldengate_opf_init_offset(lchip, &opf, 1,  SYS_STATS_ACL3_STATS_SIZE-1)) < 0)
        {
            goto error;
        }

        /* epe acl0 1K */
        gg_stats_master[lchip]->epe_acl0_base = size;
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_epeAclStatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        size += SYS_STATS_EGS_ACL0_STATS_SIZE;

        opf.pool_index = SYS_STATS_CACHE_MODE_EPE_ACL0;
        if ((ret = sys_goldengate_opf_init_offset(lchip, &opf, 1,  SYS_STATS_EGS_ACL0_STATS_SIZE-1)) < 0)
        {
            goto error;
        }
    }
    else    /* no cache mode is not support in gg for can not do line speed stats */
    {
        if ((ret = sys_goldengate_opf_init(lchip, FLOW_STATS_SRAM,  SYS_STATS_NO_CACHE_MODE_MAX)) < 0)
        {
            goto error;
        }

        /* global stats pool init */
        size = 0;

        /* Dequeue, Drop queue 4K */
        if ((stats_global_cfg->stats_bitmap & CTC_STATS_QUEUE_DEQ_STATS)
            || (stats_global_cfg->stats_bitmap & CTC_STATS_QUEUE_DROP_STATS))
        {
            cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_dequeueStatsBasePtr_f);
            if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
            {
                goto error;
            }

            gg_stats_master[lchip]->dequeue_stats_base = size;
            size += 2048;

            cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_enqueueStatsBasePtr_f);
            if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
            {
                goto error;
            }
            gg_stats_master[lchip]->enqueue_stats_base = size;
            size += 2048;
            step = 4096;
        }

        /* 13bit ipe intf stats & epe fwd stats & policer 8K */
        gg_stats_master[lchip]->len_13bit_ipe_if_base = size;
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_ipeIntfStatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_epeFwdStatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_policingStatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        size += 8192;

        opf.pool_index = SYS_STATS_NO_CACHE_MODE_13BIT_IPE_IF_EPE_FWD_POLICER;
        if ((ret = sys_goldengate_opf_init_offset(lchip, &opf, 1,  8191)) < 0)
        {
            goto error;
        }

        /* 14bit ipe intf stats & epe fwd stats & policer 4K or 8K */
        gg_stats_master[lchip]->len_14bit_ipe_if_base = gg_stats_master[lchip]->len_13bit_ipe_if_base;
        opf.pool_index = SYS_STATS_NO_CACHE_MODE_14BIT_IPE_IF_EPE_FWD_POLICER;
        if ((ret = sys_goldengate_opf_init_offset(lchip, &opf, 8192,  step)) < 0)
        {
            goto error;
        }
        size += step;

        /* 13bit ipe fwd stats & epe intf stats & acl stats 8K */
        gg_stats_master[lchip]->len_13bit_epe_if_base = size;
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_ipeFwdStatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_epeIntfStatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_ipeAcl0StatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_ipeAcl1StatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }

        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_ipeAcl2StatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }

        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_ipeAcl3StatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        cmd = DRV_IOW(StatsCacheBase_t, StatsCacheBase_epeAclStatsBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &size)) < 0)
        {
            goto error;
        }
        size += 8192;

        opf.pool_index = SYS_STATS_NO_CACHE_MODE_13BIT_EPE_IF_IPE_FWD_ACL;
        if ((ret = sys_goldengate_opf_init_offset(lchip, &opf, 1,  8191)) < 0)
        {
            goto error;
        }

        /* 14bit epe intf stats & acl stats 8K */
        gg_stats_master[lchip]->len_14bit_epe_if_base = gg_stats_master[lchip]->len_13bit_epe_if_base;
        opf.pool_index = SYS_STATS_NO_CACHE_MODE_14BIT_EPE_IF_ACL;
        if ((ret = sys_goldengate_opf_init_offset(lchip, &opf, 8192,  8192)) < 0)
        {
            goto error;
        }
    }

    /* mac stats */
    _sys_goldengate_stats_mac_stats_init(lchip);

    if ((sys_goldengate_stats_set_saturate_en(lchip, CTC_STATS_TYPE_FWD, TRUE)) < 0)
    {
        goto error;
    }

    if ((sys_goldengate_stats_set_clear_after_read_en(lchip, CTC_STATS_TYPE_FWD, TRUE)) < 0)
    {
        goto error;
    }

    if ((ret = _sys_goldengate_stats_init_done(lchip)) < 0)
    {
        goto error;
    }

    if ((ret = _sys_goldengate_stats_statsid_init(lchip)) < 0)
    {
        goto error;
    }

    if ((ret = _sys_goldengate_stats_mac_throughput_init(lchip)) < 0)
    {
        goto error;
    }

    /* enable stats update */
    cmd = DRV_IOW(GlobalStatsTimerCtl_t, GlobalStatsTimerCtl_updateEn_f);
    size = 1;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &size));

    CTC_ERROR_RETURN(sys_goldengate_dma_register_cb(lchip, SYS_DMA_CB_TYPE_PORT_STATS,
        sys_goldengate_stats_sync_dma_mac_stats));

    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_STATS, sys_goldengate_stats_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_stats_wb_restore(lchip));
    }

    /* set chip_capability */
    value = SYS_STATS_ENQUEUE_STATS_SIZE+SYS_STATS_DEQUEUE_STATS_SIZE+(SYS_STATS_POLICER_STATS_SIZE-1)+(SYS_STATS_IPE_IF_STATS_SIZE-1)
        +tmp_size+(SYS_STATS_ACL0_STATS_SIZE-1)+(SYS_STATS_ACL1_STATS_SIZE-1)
        +(SYS_STATS_ACL2_STATS_SIZE-1)+(SYS_STATS_ACL3_STATS_SIZE-1)+(SYS_STATS_EGS_ACL0_STATS_SIZE-1) + ecmp_stats_num;
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_TOTAL_STATS_NUM, value));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_SHARE1_STATS_NUM, tmp_size));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_ECMP_STATS_NUM, ecmp_stats_num));
    /*************************************************
    *init queue stats base
    *************************************************/
    CTC_ERROR_RETURN(sys_goldengate_stats_set_queue_en(lchip, 0));
    return CTC_E_NONE;

error:
    _sys_goldengate_stats_deinit_start(lchip);

    return ret;
}

STATIC int32
_sys_goldengate_stats_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_stats_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == gg_stats_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_goldengate_opf_deinit(lchip, FLOW_STATS_SRAM);

    if (gg_stats_master[lchip]->stats_mode == CTC_STATS_MODE_DEFINE)
    {
        sys_goldengate_opf_deinit(lchip, OPF_STATS_STATSID);
    }

    /*free stats statsid*/
    ctc_hash_traverse(gg_stats_master[lchip]->stats_statsid_hash, (hash_traversal_fn)_sys_goldengate_stats_free_node_data, NULL);
    ctc_hash_free(gg_stats_master[lchip]->stats_statsid_hash);

    /*free port log vector*/
    ctc_vector_traverse(gg_stats_master[lchip]->port_log_vec, (hash_traversal_fn)_sys_goldengate_stats_free_node_data, NULL);
    ctc_vector_release(gg_stats_master[lchip]->port_log_vec);

    /*free flow stats*/
    ctc_hash_traverse(gg_stats_master[lchip]->flow_stats_hash, (hash_traversal_fn)_sys_goldengate_stats_free_node_data, NULL);
    ctc_hash_free(gg_stats_master[lchip]->flow_stats_hash);

    sal_mutex_destroy(gg_stats_master[lchip]->p_stats_mutex);
    mem_free(gg_stats_master[lchip]);

    return CTC_E_NONE;
}

