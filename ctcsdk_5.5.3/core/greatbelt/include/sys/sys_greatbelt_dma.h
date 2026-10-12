/**
 @file sys_greatbelt_dma.h

 @date 2012-3-10

 @version v2.0

*/

#ifndef _SYS_GREATBELT_DMA_H
#define _SYS_GREATBELT_DMA_H
#ifdef __cplusplus
extern "C" {
#endif
/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
/* commone */
#include "sal.h"
#include "ctc_const.h"
#include "ctc_linklist.h"
#include "ctc_debug.h"
#include "ctc_error.h"
#include "ctc_learning_aging.h"
#include "ctc_dma.h"
#include "ctc_packet.h"
#include "greatbelt/include/drv_io.h"

/****************************************************************
*
* Functions
*
****************************************************************/
/* channel allocation, will be get from global configure later */
#define SYS_DMA_STATS_CHANNEL       0
#define SYS_DMA_LEARN_CHANNEL       1
#define SYS_DMA_WTABLE_CHANNEL      2
#define SYS_DMA_RTABLE_CHANNEL      2
#define SYS_DMA_TX_CHANNEL          3
#define SYS_DMA_RX_MIN_CHANNEL      3
#define SYS_DMA_RX_MAX_CHANNEL      6
#define SYS_DMA_OAM_BFD_CHANNEL     7
#define SYS_DMA_RX_CHAN_NUM         4
#define SYS_DMA_PKT_TX_CHAN_NUM         3
#define SYS_DMA_TX_CHAN_NUM         8

#define SYS_DMA_STATS_INTERVAL   (5*60)    /* second */
#define SYS_SUP_CLOCK   250    /* 250M */

#define SYS_DMA_BFD_MAX_DESC_NUM  1023
#define SYS_DMA_BFD_CHAN_USE_NUM 1
#define SYS_DMA_BFD_USE_COS  63

#define SYS_DMA_MAX_DESC_NUM  1023

typedef int32 (* DMA_LEARN_CB_FUN_P)  (uint8 lchip, void* p_learn_info);  /* ctc_hw_learn_aging_fifo_t */

#define GET_HIGH_32BITS(addr, high) \
    { \
        (high) = ((addr) >> 32); \
    } \

#define COMBINE_64BITS_DATA(high, low, data) \
    {  \
        (data) = high; \
        (data) = (data <<32) | low; \
    }

struct sys_dma_thread_s
{
    uint8 chan_id;
    uint8 lchip;
    uint8 rsv[2];
};
typedef struct sys_dma_thread_s sys_dma_thread_t;


/**
 @brief dma stats information
*/
struct sys_dma_desc_s
{
    pci_exp_desc_mem_t pci_exp_desc;
    uint32   desc_padding;
};
typedef struct sys_dma_desc_s sys_dma_desc_t;

enum sys_dma_func_type_e
{
    DMA_STATS_FUNCTION,
    DMA_PACKET_RX_FUNCTION,
    DMA_PACKET_TX_FUNCTION,
    DMA_HW_LEARNING_FUNCTION
};
typedef  enum sys_dma_func_type_e sys_dma_func_type_t;

/**
 @brief init dma module and allocate necessary memory
*/
extern int32
sys_greatbelt_dma_init(uint8 lchip, ctc_dma_global_cfg_t* dma_global_cfg);

/**
 @brief De-Initialize dma module
*/
extern int32
sys_greatbelt_dma_deinit(uint8 lchip);

/**
 @brief packet DMA TX
*/
extern int32
sys_greatbelt_dma_pkt_tx(uint8 lchip, ctc_pkt_tx_t* p_pkt_tx);

/**
 @brief table DMA TX & RX
*/
extern int32
sys_greatbelt_dma_rw_table(uint8 lchip, ctc_dma_tbl_rw_t* tbl_cfg);

/**
 @brief packet DMA RX stats test
*/
extern int32
sys_greatbelt_dma_show_stats(uint8 lchip);

extern int32
sys_greatbelt_dma_register_cb(uint8 lchip, sys_dma_func_type_t type, DMA_LEARN_CB_FUN_P cb);

extern int32
sys_greatbelt_dma_set_stats_interval(uint8 lchip, uint32  stats_interval);
#ifdef __cplusplus
}
#endif

#endif

