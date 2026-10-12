
/**
 @file sys_goldengate_dma_priv.h

 @date 2012-11-30

 @version v2.0

*/
#ifndef _SYS_GOLDENGATE_DMA_PRIV_H
#define _SYS_GOLDENGATE_DMA_PRIV_H
#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_debug.h"
#include "ctc_const.h"
#include "ctc_vector.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
#define SYS_DMA_DUMP(FMT, ...)                  \
    do                                          \
    {                                           \
        CTC_DEBUG_OUT(dma, dma, DMA_SYS, CTC_DEBUG_LEVEL_DUMP, FMT, ## __VA_ARGS__); \
    } while (0)

#define SYS_DMA_CHAN_MAPPING(chan_id)  ((chan_id) < 4) ? 0: (chan_id)

#define SYS_DMA_DIR_TX  0
#define SYS_DMA_DIR_RX 1

/* just support quadmac stats and sgmac stats */
#define SYS_DMA_STATS_COUNT 2

/* dma learning cache for gen interrupt */
#define SYS_DMA_LEARNING_THRESHOLD  17
#define SYS_DMA_LEARNING_TIMER 1000

#define SYS_DMA_TX_COUNT            1000
#define SYS_DMA_TBL_COUNT           40000
#define SYS_DMA_INIT_COUNT          4000

#define SYS_DMA_PKT_LEN_60          60
#define SYS_DMA_WORD_LEN            4

#define CRCPOLY_LE                  0xedb88320
#define CRCPOLY_BE                  0x04c11db7

#define SYS_DMA_MAX_STATS_CNT 5
#define SYS_DMA_QUADMAC_STATS_LEN (12 * 144 * (3 + 1)*4)
#define SYS_DMA_SGMAC_STATS_LEN (12 * 40 * 4*4)
#define SYS_DMA_INBONDFLOW_STATS_LEN  0
#define SYS_DMA_EEE_STATS_LEN  0
#define SYS_DMA_USER_STATS_LEN 0

#define SYS_DMA_MAX_LEARNING_BLOCK 4
#define DMA_STATS_PRI_HIGH 1
#define DMA_TABLE_PRI_HIGH 0

#define SYS_DMA_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(dma, dma, DMA_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

#ifndef PACKET_TX_USE_SPINLOCK
#define DMA_LOCK(mutex) \
    if (mutex) sal_mutex_lock(mutex)
#define DMA_UNLOCK(mutex) \
    if (mutex) sal_mutex_unlock(mutex)
#else
#define DMA_LOCK(mutex) \
    if (mutex) sal_spinlock_lock((sal_spinlock_t*)mutex)
#define DMA_UNLOCK(mutex) \
    if (mutex) sal_spinlock_unlock((sal_spinlock_t*)mutex)
#endif

#define SYS_DMA_PKT_LEN_CHECK(len) \
    do { \
        if ((len) > (CTC_PKT_MTU)) \
        { \
            return CTC_E_DMA_INVALID_PKT_LEN; \
        } \
    } while (0)

enum swap_direction
{
    HOST_TO_NETWORK,
    NETWORK_TO_HOST
};

struct sys_dma_stats_s
{
    /* packet rx statistics */
    uint64          rx0_pkt_cnt;        /**< total number of packets for channel 0*/
    uint64          rx0_byte_cnt;      /**< total number of byte cnt for channel 0*/
    uint64          rx0_drop_cnt;    /**< total number of drop packets for channel0*/
    uint64          rx0_error_cnt;    /**< total number of error packets for channel0*/

    uint64          rx1_pkt_cnt;        /**< total number of packets for channel 1*/
    uint64          rx1_byte_cnt;      /**< total number of byte cnt for channel 1*/
    uint64          rx1_drop_cnt;    /**< total number of drop packets for channel1*/
    uint64          rx1_error_cnt;    /**< total number of error packets for channel1*/

    uint64          rx2_pkt_cnt;        /**< total number of packets for channel 2*/
    uint64          rx2_byte_cnt;      /**< total number of byte cnt for channel 2*/
    uint64          rx2_drop_cnt;    /**< total number of drop packets for channel2*/
    uint64          rx2_error_cnt;    /**< total number of error packets for channel2*/

    uint64          rx3_pkt_cnt;        /**< total number of packets for channel 3*/
    uint64          rx3_byte_cnt;      /**< total number of byte cnt for channel 3*/
    uint64          rx3_drop_cnt;    /**< total number of drop packets for channel3*/
    uint64          rx3_error_cnt;    /**< total number of error packets for channel3*/

    /* packet tx statistics */
    uint64          tx0_bad_byte;        /**< total number of bad packets byte for channel 0*/
    uint64          tx0_good_byte;      /**< total number of good packets byte for channel 0*/
    uint64          tx0_bad_cnt;           /**< total number of bad packets for channel 0*/
    uint64          tx0_good_cnt;         /**< total number of good packets for channel 0*/

    uint64          tx1_bad_byte;        /**< total number of bad packets byte for channel 0*/
    uint64          tx1_good_byte;      /**< total number of good packets byte for channel 0*/
    uint64          tx1_bad_cnt;           /**< total number of bad packets for channel 0*/
    uint64          tx1_good_cnt;         /**< total number of good packets for channel 0*/

    uint16          rx_sync_cnt;
    uint16          tx_sync_cnt;
};
typedef struct sys_dma_stats_s sys_dma_stats_t;

struct sys_dma_desc_used_info_s
{
    void* p_mem;
};
typedef struct sys_dma_desc_used_info_s sys_dma_desc_used_info_t;

struct sys_dma_desc_used_s
{
    uint32  count;
    uint32  entry_num;
    sys_dma_desc_used_info_t*   p_array;
};
typedef struct sys_dma_desc_used_s sys_dma_desc_used_t;

struct sys_dma_chan_s
{
    uint8 chan_en;
    uint8 sync_chan;
    uint8 req_timeout_en;        /*for info learning and ipfix need using*/
    uint8 sync_en;

    uint8 channel_id;
    uint8 func_type;
    uint8 weight;
    uint8 dmasel;                                              /**< 0:DmaCtl0, 1:DmaCtl1 */

    uint32 current_index;
    uint32 desc_num;                                       /**< init desc number for channel */
    uint32 desc_depth;
    uintptr mem_base;

    uint16 data_size;                                        /**< data size per desc, used for alloc memory */
    uint16 cfg_size;                                          /**< desc cfg size used for config desc, for packet/reg uint is Byte , for InfoDma uint is Entry */

    sys_dma_desc_t* p_desc;                               /**< descriptors used by packet(TX) DMA */
    uint8* p_desc_used;                                     /**< indicate desc is used ot not , used for packet tx>*/

    sal_mutex_t* p_mutex;    /**< channel mutex  */
    uint8 in_process;
    uint8 rsv[3];
    sys_dma_desc_info_t* p_desc_info;
};
typedef struct sys_dma_chan_s sys_dma_chan_t;

struct sys_dma_master_s
{
    sys_dma_chan_t dma_chan_info[SYS_DMA_MAX_CHAN_NUM];
    sys_dma_stats_t dma_stats;
    uint16 dma_thread_pri[SYS_DMA_MAX_CHAN_NUM];
    uint16 packet_rx_chan_num;
    ctc_vector_t* p_thread_vector; /* store sync thread info */

    DMA_CB_FUN_P dma_cb[SYS_DMA_CB_MAX_TYPE];
    uint16      pkt_rx_size_per_desc;   /**< size of received pkt for every descriptor */
    uint16      dma_en_flag;                /**< flag indate which dma channel is enable, ont bit present one channel */
    sal_task_t* p_polling_task;
    uint32 dma_high_addr;

    uint32* g_tcam_wr_buffer[10];
    uint8  g_drain_en[3];
    uint8  hw_learning_sync;
};
typedef struct sys_dma_master_s sys_dma_master_t;

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

#ifdef __cplusplus
}
#endif

#endif

