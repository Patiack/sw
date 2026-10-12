
/**
 @file sys_greatbelt_dma_priv.h

 @date 2012-11-30

 @version v2.0

*/
#ifndef _SYS_GREATBELT_DMA_PRIV_H
#define _SYS_GREATBELT_DMA_PRIV_H
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
#include "greatbelt/include/drv_io.h"

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

#define SYS_DMA_DIR_TX  0
#define SYS_DMA_DIR_RX 1

/* just support quadmac stats and sgmac stats */
#define SYS_DMA_STATS_COUNT 2

/* dma learning cache for gen interrupt */
#define SYS_DMA_LEARNING_THRESHOLD  17

/* 16 entry threshold, per entry 100 cycle */
#define SYS_DMA_LEARNING_TIMER (16*100)

#define SYS_DMA_LEARNING_BLOCK_NUM 4
#define SYS_DMA_LEARNING_BLOCK_LIMIT 1

#define SYS_DMA_LEARNING_DEPTH 1024
#define SYS_DMA_LEARNING_DEPTH_LIMIT 16

#define SYS_DMA_TX_COUNT            1000
#define SYS_DMA_TBL_COUNT           0xfffff
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

#define DMA_PKT_DEFAULT_SIZE (9600+CTC_PKT_HEADER_LEN+4)

#define SYS_DMA_INIT_CHECK(lchip) \
    do { \
        if (NULL == p_gb_dma_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define SYS_DMA_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(dma, dma, DMA_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

/* recover rx descriptor  */
#define SYS_DMA_SET_DEFAULT_RX_DESC(rx_desc, len) \
    { \
        rx_desc->desc_done = 0; \
        rx_desc->desc_sop  = 0; \
        rx_desc->desc_eop  = 0; \
        rx_desc->desc_len  = len; \
    }

#define SYS_DMA_PKT_LEN_CHECK(len) \
    do { \
        if ((len) > (DMA_PKT_DEFAULT_SIZE-4)) \
        { \
            return CTC_E_DMA_INVALID_PKT_LEN; \
        } \
    } while (0)

#define DMA_LOCK(mutex) \
    if (mutex) sal_mutex_lock(mutex)
#define DMA_UNLOCK(mutex) \
    if (mutex) sal_mutex_unlock(mutex)


enum swap_direction
{
    HOST_TO_NETWORK,
    NETWORK_TO_HOST
};

struct sys_dma_stats_s
{
    /* packet rx statistics */
    uint64          rx_good_cnt;        /**< total number of packets */
    uint64          rx_good_byte;   /**< total number of dropped packets*/
    uint64          rx_bad_cnt;   /**< total number of received packets*/
    uint64          rx_bad_byte;   /**< total number of received packets*/

    /* packet tx statistics */
    uint64          tx_total_cnt;
    uint64          tx_total_byte;
    uint64          tx_error_cnt;

    uint16          rx_sync_cnt;
    uint8           sync_flag;
    uint8           tx_sync_cnt;
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
    uint8 channel_id;
    uint8 direction;
    uint16 current_index;
    uint16 desc_num;
    uint16 desc_depth;
    uintptr mem_base;
    sys_dma_desc_t* p_desc;                               /**< descriptors used by packet(TX) DMA */
    sys_dma_desc_used_t desc_used;
    uint8* p_desc_used;                                     /**< indicate desc is used ot not , used for packet tx>*/
    sal_mutex_t* p_mutex;    /**<only useful for dir:CPU->DMA */
};
typedef struct sys_dma_chan_s sys_dma_chan_t;

struct sys_dma_learning_s
{
    uint8 channel_id;
    uint8 hw_learning_sync_en;
    uint16 cur_block_idx;

    uint16 max_mem0_learning_num;               /**< max learning entry of 1st memory */
    uint16 max_mem1_learning_num;               /**< max learning entry of 2nd memory */
    uint16 max_mem2_learning_num;               /**< max learning entry of 3rd memory */
    uint16 max_mem3_learning_num;               /**< max learning entry of 4th memory */

    void* p_mem0_base;                                 /**< BAR of 1st memory, learning DMA */
    void* p_mem1_base;                                 /**< BAR of 2nd memory, learning DMA */
    void* p_mem2_base;                                 /**< BAR of 3rd memory, learning DMA */
    void* p_mem3_base;                                 /**< BAR of 4th memory, learning DMA */

    CTC_CALLBACK_LEARN_FUN_P learning_proc_func;             /**< learning callback function for system */
    DMA_LEARN_CB_FUN_P fdb_cb_func;       /**< learning callback function for fdb module */
};
typedef struct sys_dma_learning_s sys_dma_learning_t;

struct sys_dma_master_s
{
    sys_dma_chan_t* p_dma_chan[CTC_DMA_FUNC_MAX];
    sys_dma_chan_t sys_packet_tx_dma[SYS_DMA_TX_CHAN_NUM];
    sys_dma_chan_t sys_table_wr_dma;
    sys_dma_chan_t sys_packet_rx_dma[SYS_DMA_RX_CHAN_NUM];
    sys_dma_chan_t sys_stats_dma;
    sys_dma_chan_t sys_table_rd_dma;
    sys_dma_chan_t sys_oam_bfd_dma;
    sys_dma_learning_t sys_learning_dma;

    sys_dma_stats_t          dma_stats;

    uint16      pkt_rx_size_per_desc;   /**< size of received pkt for every descriptor */
    uint8       pkt_rx_chan_num;
    uint8       dma_en_flag;           /**< flag indate which dma function is enable, please refer to ctc_dma_en_t*/
    uint8       byte_order;    /**0:big_end, 1:little_end, only usefull for hw platform*/
    uint8       rsv;

    sal_sem_t*  learning_sync_sem;
    sal_task_t* dma_learning_thread;

    sal_sem_t*  packet_rx_sync_sem[SYS_DMA_RX_CHAN_NUM+1];     /**<the last one is special for oam bfd */
    sal_task_t* dma_packet_rx_thread[SYS_DMA_RX_CHAN_NUM+1];   /**<the last one is special for oam bfd */

    int32       packet_rx_priority[SYS_DMA_RX_CHAN_NUM+1];              /**<the last one is special for oam bfd */
    int32       stats_priority;
    int32       learning_priority;
    sal_sem_t*  stats_sync_sem;
    sal_task_t* dma_stats_thread;

    uint32   dma_high_addr;
};
typedef struct sys_dma_master_s sys_dma_master_t;

#if (HOST_IS_LE == 1)
struct sys_dma_fib_s
{
    uint32 key_index17_4                    :14;
    uint32 key_type                             :1;
    uint32 rsv_4                                   :1;
    uint32 entry_vld                             :1;
    uint32 rsv_3                                   :15;

    uint32 vsi_id13_3                        :11;
    uint32 ds_ad_index1_0                :2;
    uint32 ds_ad_index2_2                   :1;
    uint32 is_mac_hash                        :1;
    uint32 valid                                    :1;
    uint32 ds_ad_index14_3                 :12;
    uint32 key_index3_0                      :4;

    uint32 mapped_mac31_19             :13;
    uint32 mapped_mac47_32             :16;
    uint32 vsi_id2_0                        :3;

    uint32 mapped_mac17_0               :18;
    uint32 rsv_1                                  :6;
    uint32 mapped_mac18                   :1;
    uint32 rsv_2                                  :7;
 };
typedef struct sys_dma_fib_s sys_dma_fib_t;

#else
struct sys_dma_fib_s
{
    uint32 rsv_3                                   :15;
    uint32 entry_vld                             :1;
    uint32 rsv_4                                   :1;
    uint32 key_type                             :1;
    uint32 key_index17_4                    :14;

    uint32 key_index3_0                      :4;
    uint32 ds_ad_index14_3                 :12;
    uint32 valid                                    :1;
    uint32 is_mac_hash                        :1;
    uint32 ds_ad_index2_2                   :1;
    uint32 ds_ad_index1_0                :2;
    uint32 vsi_id13_3                        :11;


    uint32 vsi_id2_0                        :3;
    uint32 mapped_mac47_32             :16;
    uint32 mapped_mac31_19             :13;

    uint32 rsv_2                                  :7;
    uint32 mapped_mac18                   :1;
    uint32 rsv_1                                  :6;
    uint32 mapped_mac17_0               :18;

 };
typedef struct sys_dma_fib_s sys_dma_fib_t;
#endif

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

#ifdef __cplusplus
}
#endif

#endif

