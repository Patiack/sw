/**
 @file ctc_greatbelt_dma.c

 @author  Copyright (C) 2012 Centec Networks Inc.  All rights reserved.

 @date 2012-11-30

 @version v2.0

 This file define ctc functions

*/

/****************************************************************************
*
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_packet.h"
#include "ctc_learning_aging.h"
#include "ctc_greatbelt_dma.h"
#include "sys_greatbelt_dma.h"
#include "sys_greatbelt_chip.h"

/****************************************************************************
*
* Defines and Macros
*
*****************************************************************************/

/****************************************************************************
*
* Global and Declaration
*
*****************************************************************************/

/****************************************************************************
*
* Function
*
*****************************************************************************/

/**
 @brief Init dma module

 @param[in] dma_global_cfg      packet DMA, table DMA, learning DMA and stats DMA information

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_dma_init(uint8 lchip, ctc_dma_global_cfg_t* dma_global_cfg)
{
    ctc_dma_global_cfg_t dma_gbl_cfg;
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_DMA);
    LCHIP_CHECK(lchip);

    if (NULL == dma_global_cfg)
    {
        sal_memset(&dma_gbl_cfg, 0, sizeof(ctc_dma_global_cfg_t));
        dma_gbl_cfg.stats.desc_num          = 16;
        dma_gbl_cfg.stats.priority          = SAL_TASK_PRIO_DEF;
        dma_gbl_cfg.learning.desc_num       = (1024*4);
        dma_gbl_cfg.learning.priority       = SAL_TASK_PRIO_DEF;
        dma_gbl_cfg.pkt_rx[0].desc_num      = 64;
        dma_gbl_cfg.pkt_rx[0].priority      = SAL_TASK_PRIO_NICE_HIGH;
        dma_gbl_cfg.pkt_rx[1].desc_num      = 64;
        dma_gbl_cfg.pkt_rx[1].priority      = SAL_TASK_PRIO_DEF;
        dma_gbl_cfg.pkt_rx[2].desc_num      = 64;
        dma_gbl_cfg.pkt_rx[2].priority      = SAL_TASK_PRIO_DEF;
        dma_gbl_cfg.pkt_rx[3].desc_num      = 64;
        dma_gbl_cfg.pkt_rx[3].priority      = SAL_TASK_PRIO_DEF;
        dma_gbl_cfg.pkt_rx_chan_num         = 4;
        dma_gbl_cfg.pkt_tx_desc_num         = 16;
        dma_gbl_cfg.table_r_desc_num        = 1;
        dma_gbl_cfg.table_w_desc_num        = 1;
        dma_gbl_cfg.pkt_rx_size_per_desc    = 256;
        dma_gbl_cfg.learning_proc_func      = NULL;
        dma_gbl_cfg.func_en_bitmap          = 0;
        dma_gbl_cfg.hw_learning_sync_en = 0;
        CTC_BIT_SET(dma_gbl_cfg.func_en_bitmap, CTC_DMA_FUNC_PACKET_RX);
        CTC_BIT_SET(dma_gbl_cfg.func_en_bitmap, CTC_DMA_FUNC_PACKET_TX);
        CTC_BIT_SET(dma_gbl_cfg.func_en_bitmap, CTC_DMA_FUNC_TABLE_W);
        CTC_BIT_SET(dma_gbl_cfg.func_en_bitmap, CTC_DMA_FUNC_TABLE_R);
        CTC_BIT_SET(dma_gbl_cfg.func_en_bitmap, CTC_DMA_FUNC_HW_LEARNING);
        CTC_BIT_SET(dma_gbl_cfg.func_en_bitmap, CTC_DMA_FUNC_STATS);
        dma_global_cfg = &dma_gbl_cfg;
    }

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_dma_init(lchip, dma_global_cfg));
    }

    return CTC_E_NONE;
}

int32
ctc_greatbelt_dma_deinit(uint8 lchip)
{
    uint8 lchip_start       = 0;
    uint8 lchip_end         = 0;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_DMA);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_dma_deinit(lchip));
    }

    return CTC_E_NONE;
}

/**
 @brief send pkt

 @param[in] tx_cfg              packet DMA TX, system buffer, buffer len and bridge header

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_dma_tx_pkt(uint8 lchip, ctc_pkt_tx_t* tx_cfg)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_DMA);
    CTC_ERROR_RETURN(sys_greatbelt_dma_pkt_tx(lchip, tx_cfg));
    return CTC_E_NONE;
}

/**
 @brief read or write table

 @param[in] tbl_cfg             table DMA, system buffer, buffer len and table address

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_dma_rw_table(uint8 lchip, ctc_dma_tbl_rw_t* tbl_cfg)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_DMA);
    CTC_ERROR_RETURN(sys_greatbelt_dma_rw_table(lchip, tbl_cfg));
    return CTC_E_NONE;
}

