/**
 @file ctc_greatbelt_interrupt.c

 @author  Copyright (C) 2012 Centec Networks Inc.  All rights reserved.

 @date 2012-10-23

 @version v2.0

 This file define sys functions

*/

/****************************************************************************
*
* Header Files
*
****************************************************************************/
#include "sal.h"
#include "dal.h"
#include "ctc_packet.h"
#include "ctc_l2.h"
#include "ctc_greatbelt_interrupt.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_interrupt.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_packet.h"
#include "sys_greatbelt_dma.h"
#include "sys_greatbelt_stats.h"
#include "sys_greatbelt_dma_priv.h"
#include "sys_greatbelt_interrupt.h"
#include "sys_greatbelt_l2_fdb.h"
#include "sys_greatbelt_l2_fdb.h"
#include "sys_greatbelt_stats.h"
#include "sys_greatbelt_register.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_packet.h"
#include "greatbelt/include/drv_chip_ctrl.h"
/****************************************************************************
*
* Defines and Macros
*
*****************************************************************************/

#define SYS_DMA_BFD_DESC_LEN  256

/****************************************************************************
*
* Global and Declaration
*
*****************************************************************************/
sys_dma_master_t* p_gb_dma_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
extern dal_op_t g_dal_op;
static uint32 g_sys_stats_desc_len[SYS_DMA_MAX_STATS_CNT] =
{
    SYS_DMA_QUADMAC_STATS_LEN,
    SYS_DMA_SGMAC_STATS_LEN,
    SYS_DMA_INBONDFLOW_STATS_LEN,
    SYS_DMA_EEE_STATS_LEN,
    SYS_DMA_USER_STATS_LEN
};
uint16 g_sys_cur_learning_cnt[SYS_DMA_MAX_LEARNING_BLOCK] = {0, 0, 0, 0};

extern sal_mutex_t* p_gb_entry_mutex[MAX_LOCAL_CHIP_NUM];
extern uint8
drv_greatbelt_get_host_type(void);
/****************************************************************************
*
* Function
*
*****************************************************************************/

/* get WORD in chip */
STATIC int32
_sys_greatbelt_dma_rw_get_words_in_chip(uint8 lchip, uint16 entry_words, uint8* words_in_chip)
{
    if (1 == entry_words)
    {
        *words_in_chip = 1;
    }
    else if (2 == entry_words)
    {
        *words_in_chip = 2;
    }
    else if ((3 <= entry_words) && (entry_words <= 4))
    {
        *words_in_chip = 4;
    }
    else if ((5 <= entry_words) && (entry_words <= 8))
    {
        *words_in_chip = 8;
    }
    else if ((9 <= entry_words) && (entry_words <= 16))
    {
        *words_in_chip = 16;
    }
    else if ((17 <= entry_words) && (entry_words <= 32))
    {
        *words_in_chip = 32;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC uint16
_sys_greatbelt_dma_get_write_table_buf(uint8 lchip, void *data)
{
    uint8  words_in_chip = 0;
    uint16 words_num = 0;
    uint16 entry_num = 0;
    uint16 entry_len = 0;
    uint16 buf_size  = 0;

    entry_len = TABLE_ENTRY_SIZE(DsLpmLookupKey0_t);
    words_num = (entry_len / SYS_DMA_WORD_LEN);
    entry_num = 256;
    _sys_greatbelt_dma_rw_get_words_in_chip(lchip, words_num, &words_in_chip);
    buf_size = entry_num * words_in_chip * SYS_DMA_WORD_LEN;

    return buf_size;
}

#define _STUB_FUNC
#define _DMA_FUNCTION_INTERFACE

int32
sys_greatbelt_dma_sync_pkt_stats(uint8 lchip)
{
    uint32 cmd = 0;
    dma_pkt_stats_t pkt_stats;

    sal_memset(&pkt_stats, 0, sizeof(dma_pkt_stats_t));
    cmd = DRV_IOR(DmaPktStats_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pkt_stats));

    p_gb_dma_master[lchip]->dma_stats.rx_good_cnt    += pkt_stats.dma_rx_good_frames_cnt;
    p_gb_dma_master[lchip]->dma_stats.rx_good_byte   += pkt_stats.dma_rx_good_bytes_cnt;
    p_gb_dma_master[lchip]->dma_stats.rx_bad_cnt     += pkt_stats.dma_rx_bad_frames_cnt;
    p_gb_dma_master[lchip]->dma_stats.rx_bad_byte    += pkt_stats.dma_rx_bad_bytes_cnt;

    p_gb_dma_master[lchip]->dma_stats.tx_total_cnt   += pkt_stats.dma_tx_total_frames_cnt;
    p_gb_dma_master[lchip]->dma_stats.tx_total_byte  += pkt_stats.dma_tx_total_bytes_cnt;
    p_gb_dma_master[lchip]->dma_stats.tx_error_cnt   += pkt_stats.dma_tx_error_frames_cnt;

    return CTC_E_NONE;
}

/**
 @brief DMA mapping learning information
*/
void
sys_greatbelt_dma_learn_info_mapping(uint8 lchip, sys_dma_fib_t* p_learn_fifo_result, ctc_learn_aging_info_t* p_learn_info)
{

    p_learn_info->key_index   = (p_learn_fifo_result->key_index17_4 << 4) | p_learn_fifo_result->key_index3_0;
    p_learn_info->damac_index = (p_learn_fifo_result->ds_ad_index14_3 << 3) \
        | (p_learn_fifo_result->ds_ad_index2_2 << 2) \
        | (p_learn_fifo_result->ds_ad_index1_0);
    p_learn_info->is_aging = (p_learn_fifo_result->key_type)?FALSE:TRUE;
    p_learn_info->is_mac_hash = p_learn_fifo_result->is_mac_hash;
    p_learn_info->valid       = p_learn_fifo_result->valid;
    p_learn_info->vsi_id      = (p_learn_fifo_result->vsi_id13_3 << 3) | p_learn_fifo_result->vsi_id2_0;
    p_learn_info->mac[5] = (p_learn_fifo_result->mapped_mac17_0) & 0xFF;
    p_learn_info->mac[4] = (p_learn_fifo_result->mapped_mac17_0 >> 8) & 0xFF;
    p_learn_info->mac[3] = ((p_learn_fifo_result->mapped_mac17_0 >> 16) |
        (p_learn_fifo_result->mapped_mac18 << 2) | (p_learn_fifo_result->mapped_mac31_19 << 3)) & 0xFF;
    p_learn_info->mac[2] = (p_learn_fifo_result->mapped_mac31_19 >> 5) & 0xFF;
    p_learn_info->mac[1] = (p_learn_fifo_result->mapped_mac47_32) & 0xFF;
    p_learn_info->mac[0] = (p_learn_fifo_result->mapped_mac47_32 >> 8) & 0xFF;
}

/**
 @brief table DMA TX
*/
int32
_sys_greatbelt_dma_read_table(uint8 lchip, volatile pci_exp_desc_mem_t* p_tbl_desc)
{
    sys_dma_chan_t* p_dma_chan = &p_gb_dma_master[lchip]->sys_table_rd_dma;
    uint32 rd_cnt = 0;
    bool  done = FALSE;
    int32 ret = 0;
    drv_work_platform_type_t  platform_type;
    uint32 value_tmp = 0;

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "Current descriptor, desc_index=%d,done:%d error:%d sop:%d eop:%d desc_len:%d !\n",
                    p_dma_chan->current_index,
                    p_tbl_desc->desc_done,
                    p_tbl_desc->desc_error,
                    p_tbl_desc->desc_sop,
                    p_tbl_desc->desc_eop,
                    p_tbl_desc->desc_len);

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "desc_mem_addr_low =0x%x,desc_cfg_addr:0x%x  desc_words:%d !\n",
                    p_tbl_desc->desc_mem_addr_low,
                    p_tbl_desc->desc_cfg_addr,
                    p_tbl_desc->desc_words);

    sal_mutex_lock(p_gb_entry_mutex[0]);

    /* table DMA  valid num */
    value_tmp = 1;
    drv_greatbelt_chip_write(lchip, (0x000044a0+4*SYS_DMA_WTABLE_CHANNEL), value_tmp);/*DMA always use chip0*/

    do
    {
        rd_cnt++;
        if (rd_cnt > SYS_DMA_TBL_COUNT)
        {
            break;
        }
        /*inval dma before read*/
        if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_tbl_desc), sizeof(pci_exp_desc_mem_t));
        }
    }while (0 == p_tbl_desc->desc_done);
    if (p_tbl_desc->desc_done)
    {
        done  = TRUE;
    }

    ret = drv_greatbelt_get_platform_type(&platform_type);
    if (platform_type == HW_PLATFORM)
    {
        if (done == FALSE)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " desc not done!!!\n");
            ret  = CTC_E_DMA_TABLE_READ_FAILED;
        }
    }

    /* next descriptor, tbl_desc_index: 0~table_desc_num-1*/
    p_dma_chan->current_index =
        ((p_dma_chan->current_index + 1) == p_dma_chan->desc_depth) ? 0 : (p_dma_chan->current_index + 1);

    sal_mutex_unlock(p_gb_entry_mutex[0]);

    return ret;
}

/**
 @brief table DMA RX
*/
int32
_sys_greatbelt_dma_write_table(uint8 lchip, volatile pci_exp_desc_mem_t* p_tbl_desc)
{
    sys_dma_chan_t* p_dma_chan = &p_gb_dma_master[lchip]->sys_table_wr_dma;
    uint32 wr_cnt = 0;
    bool  done = FALSE;
    int32 ret = 0;
    drv_work_platform_type_t  platform_type;
    uint32 value_tmp = 0;

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "Current descriptor, desc_index=%d,done:%d error:%d sop:%d eop:%d desc_len:%d !\n",
                    p_dma_chan->current_index,
                    p_tbl_desc->desc_done,
                    p_tbl_desc->desc_error,
                    p_tbl_desc->desc_sop,
                    p_tbl_desc->desc_eop,
                    p_tbl_desc->desc_len);

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "desc_mem_addr_low =0x%x,desc_cfg_addr:0x%x  desc_words:%d !\n",
                    p_tbl_desc->desc_mem_addr_low,
                    p_tbl_desc->desc_cfg_addr,
                    p_tbl_desc->desc_words);

    sal_mutex_lock(p_gb_entry_mutex[0]);

    /* table DMA  valid num */
    value_tmp = 1;
    drv_greatbelt_chip_write(lchip, (0x00004440+4*SYS_DMA_WTABLE_CHANNEL), value_tmp);/*DMA always use chip0*/

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Current descriptor, desc_index=%d,done:%d error:%d sop:%d eop:%d desc_len:%d !\n",
                    p_dma_chan->current_index,
                    p_tbl_desc->desc_done,
                    p_tbl_desc->desc_error,
                    p_tbl_desc->desc_sop,
                    p_tbl_desc->desc_eop,
                    p_tbl_desc->desc_len);

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "desc_mem_addr_low =0x%x,desc_cfg_addr:0x%x  desc_words:%d !\n",
                    p_tbl_desc->desc_mem_addr_low,
                    p_tbl_desc->desc_cfg_addr,
                    p_tbl_desc->desc_words);

    do
    {
        wr_cnt++;
        if (wr_cnt > SYS_DMA_TBL_COUNT)
        {
            break;
        }
        /*inval dma before read*/
        if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_tbl_desc), sizeof(pci_exp_desc_mem_t));
        }
    }while (0 == p_tbl_desc->desc_done);
    if (p_tbl_desc->desc_done)
    {
        done  = TRUE;
    }

    ret = drv_greatbelt_get_platform_type(&platform_type);
    if (platform_type == HW_PLATFORM)
    {
        if (done)
        {
            for (wr_cnt = 0; wr_cnt < SYS_DMA_TBL_COUNT; wr_cnt++)
            {
                drv_greatbelt_chip_read(lchip, 0x000041c4, &value_tmp);
                if (((value_tmp >> 6) & 0x03) == 0)
                {
                    break;
                }
            }

            if (wr_cnt >= SYS_DMA_TBL_COUNT)
            {
                done = FALSE;
            }
        }
        if (done == FALSE)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " desc not done!!!\n");
            ret  = CTC_E_DMA_TABLE_WRITE_FAILED;
        }
    }

    /* next descriptor, tbl_desc_index: 0~table_desc_num-1*/
    p_dma_chan->current_index =
        ((p_dma_chan->current_index + 1) == p_dma_chan->desc_depth) ? 0 : (p_dma_chan->current_index + 1);

    sal_mutex_unlock(p_gb_entry_mutex[0]);

    return ret;
}

/**
 @brief table DMA TX & RX
*/
int32
sys_greatbelt_dma_rw_table(uint8 lchip, ctc_dma_tbl_rw_t* tbl_cfg)
{
    int ret = CTC_E_NONE;
    volatile pci_exp_desc_mem_t* p_tbl_desc = NULL;
    uint32* p_tbl_buff = NULL;
    uint32 low_phy_addr = 0;
    uint8 words_in_chip = 0;
    uint16 words_num = 0;
    uint16 entry_num = 0;
    uint16 tbl_buffer_len = 0;
    uint16 write_tbl_buf  = 0;
    uint16 idx = 0;
    uint32* p_src = NULL;
    uint32* p_dst = NULL;
    uint32* p = NULL;
    uint64 phy_addr = 0;

    SYS_DMA_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(tbl_cfg);

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "operate(1:read, 0:write): %d, table_addr: 0x%x, entry_num: %d, entry_len: %d\n",
                    tbl_cfg->rflag, tbl_cfg->tbl_addr, tbl_cfg->entry_num, tbl_cfg->entry_len);

    words_num = (tbl_cfg->entry_len / SYS_DMA_WORD_LEN);
    entry_num = tbl_cfg->entry_num;

    _sys_greatbelt_dma_rw_get_words_in_chip(lchip, words_num, &words_in_chip);
    tbl_buffer_len = entry_num * words_in_chip * SYS_DMA_WORD_LEN;

    if (tbl_cfg->rflag)
    {
        if (g_dal_op.dma_alloc)
        {
            p_tbl_buff = g_dal_op.dma_alloc(lchip, tbl_buffer_len, 0);
        }
        if (NULL == p_tbl_buff)
        {
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_tbl_buff, 0, tbl_buffer_len);


        p_tbl_desc = (pci_exp_desc_mem_t*)&(p_gb_dma_master[lchip]->sys_table_rd_dma.p_desc->pci_exp_desc);
        p_tbl_desc->desc_cfg_addr = ((tbl_cfg->tbl_addr) >> 2);

        if (g_dal_op.logic_to_phy)
        {
            p_tbl_desc->desc_mem_addr_low = ((uint32)(g_dal_op.logic_to_phy(lchip, (void*)p_tbl_buff))) >> 4;
        }

        p_tbl_desc->desc_words = words_num;
        /* desc_len is num of entries for table DMA */
        p_tbl_desc->desc_len = entry_num;
        p_tbl_desc->desc_done = 0;
        p_tbl_desc->desc_sop  = 1;
        p_tbl_desc->desc_eop  = 1;
        /*flush dma after write*/
        if ((NULL != g_dal_op.dma_cache_flush) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_flush(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_tbl_desc), sizeof(pci_exp_desc_mem_t));
        }

        /* read */
        ret = _sys_greatbelt_dma_read_table(lchip, p_tbl_desc);
        if (ret < 0)
        {
            goto error;
        }

        /* get read result */
        for (idx = 0; idx < entry_num; idx++)
        {
            p_src = p_tbl_buff + idx * words_in_chip;
            p_dst = tbl_cfg->buffer + idx * words_num;
            /*inval dma before read*/
            if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
            {
                g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_src), words_num*SYS_DMA_WORD_LEN);
            }

            sal_memcpy(p_dst, p_src, words_num*SYS_DMA_WORD_LEN);
        }

        /*for debug show result */
        for (idx = 0; idx < entry_num * words_num; idx++)
        {
            p = (uint32*)(tbl_cfg->buffer) + idx;
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Read table(word %d) dma value: 0x%x \n", idx, *p);
        }
    }
    else
    {
        /* for debug show orginal data */
        for (idx = 0; idx < entry_num * words_num; idx++)
        {
            p = (uint32*)(tbl_cfg->buffer) + idx;
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Write table(word %d) ogrinal value: 0x%x \n", idx, *p);
        }

        p_tbl_desc = (pci_exp_desc_mem_t*)&(p_gb_dma_master[lchip]->sys_table_wr_dma.p_desc->pci_exp_desc);
        p_tbl_desc->desc_cfg_addr = ((tbl_cfg->tbl_addr) >> 2);

        /* check buf size, get data mem address */
        write_tbl_buf = _sys_greatbelt_dma_get_write_table_buf(lchip, NULL);
        if (tbl_buffer_len > write_tbl_buf)
        {
            return CTC_E_EXCEED_MAX_SIZE;
        }
        low_phy_addr = (p_tbl_desc->desc_mem_addr_low << 4);
        COMBINE_64BITS_DATA(p_gb_dma_master[lchip]->dma_high_addr, low_phy_addr, phy_addr);
        if (g_dal_op.phy_to_logic)
        {
            p_tbl_buff = (uint32*)g_dal_op.phy_to_logic(lchip, (phy_addr));
        }

        for (idx = 0; idx < entry_num; idx++)
        {
            p_dst = p_tbl_buff + idx * words_in_chip;
            p_src = tbl_cfg->buffer + idx * words_num;

            sal_memcpy(p_dst, p_src, words_num*SYS_DMA_WORD_LEN);
        }

        for (idx = 0; idx < entry_num * words_in_chip; idx++)
        {
            p = (uint32*)(p_tbl_buff) + idx;
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Write table(word %d) dma value: 0x%x \n", idx, *p);
        }

        if (g_dal_op.logic_to_phy)
        {
            p_tbl_desc->desc_mem_addr_low = ((uint32)(g_dal_op.logic_to_phy(lchip, p_tbl_buff))) >> 4;
        }
        /*flush dma after write*/
        if ((NULL != g_dal_op.dma_cache_flush) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_flush(lchip, phy_addr, tbl_buffer_len);
        }

        p_tbl_desc->desc_words = words_num;
        /* desc_len is num of entries for table DMA */
        p_tbl_desc->desc_len = entry_num;
        p_tbl_desc->desc_done = 0;
        p_tbl_desc->desc_sop  = 1;
        p_tbl_desc->desc_eop  = 1;
        /*flush dma after write*/
        if ((NULL != g_dal_op.dma_cache_flush) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_flush(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_tbl_desc), sizeof(pci_exp_desc_mem_t));
        }

        /* write */
        ret = _sys_greatbelt_dma_write_table(lchip, p_tbl_desc);
        return ret;
    }

error:
    if (g_dal_op.dma_free)
    {
        g_dal_op.dma_free(lchip, p_tbl_buff);
    }

    return ret;

}

int32
sys_greatbelt_dma_add_used(uint8 lchip, sys_dma_chan_t* p_dma_chan, uint32 index, void* p_mem)
{
    sys_dma_desc_used_t* p_desc_used = &(p_dma_chan->desc_used);

    if (0 == p_desc_used->entry_num)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (index >= p_dma_chan->desc_num)
    {
        return CTC_E_INVALID_PARAM;
    }

    p_desc_used->p_array[index].p_mem = p_mem;
    p_desc_used->count++;

    return CTC_E_NONE;
}

int32
sys_greatbelt_dma_del_used(uint8 lchip, sys_dma_chan_t* p_dma_chan, uint32 index, void* p_mem)
{
    sys_dma_desc_used_t* p_desc_used = &(p_dma_chan->desc_used);

    if (0 == p_desc_used->entry_num)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (index >= p_dma_chan->desc_num)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (g_dal_op.dma_free)
    {
        g_dal_op.dma_free(lchip, p_desc_used->p_array[index].p_mem);
    }
    p_desc_used->p_array[index].p_mem = NULL;
    p_desc_used->count--;

    return CTC_E_NONE;
}

/**
 @brief packet DMA TX, p_pkt_tx pointer to sys_pkt_tx_info_t
*/
int32
sys_greatbelt_dma_pkt_tx(uint8 lchip, ctc_pkt_tx_t* p_pkt_tx)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint32 pkt_len = 0;
    bool done = FALSE;
    uint8* tx_pkt_addr = NULL;
    volatile pci_exp_desc_mem_t* p_tx_desc_mem = NULL;
    sys_dma_chan_t* p_dma_chan = NULL;
    desc_rx_vld_num_t desc_rx_vld_num;
    uint32 low_phy_addr = 0;
    uint64 phy_addr = 0;
    uint32 cnt = 0;
    uint8 chan_idx = 0;
    sal_mutex_t* p_mutex = NULL;
    uint32 pkt_buf_len = 0;
    uint8 hdr_len = 0;
    uint8 user_flag = 0;
    sys_pkt_tx_info_t* p_pkt_tx_info = (sys_pkt_tx_info_t*)p_pkt_tx;

    SYS_DMA_INIT_CHECK(lchip);
    user_flag = p_pkt_tx_info->user_flag;
    if (user_flag)
    {
        hdr_len = p_pkt_tx_info->header_len;
    }
    /* packet length check */
    SYS_DMA_PKT_LEN_CHECK(p_pkt_tx_info->data_len + hdr_len);

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "data len: %d\n", p_pkt_tx_info->data_len+hdr_len);

    chan_idx = (p_pkt_tx_info->priority > ((p_gb_queue_master[lchip]->priority_mode)? 10:40))?1:0;

    if (p_pkt_tx_info->priority == SYS_QOS_CLASS_PRIORITY_MAX + 1)
    {
        /*special user for dying gasp*/
        chan_idx = 2;
    }

    p_dma_chan = &(p_gb_dma_master[lchip]->sys_packet_tx_dma[chan_idx]);
    p_mutex = p_dma_chan->p_mutex;


    DMA_LOCK(p_mutex);

    p_tx_desc_mem = &(p_dma_chan->p_desc[p_dma_chan->current_index].pci_exp_desc);
    if (p_tx_desc_mem == NULL)
    {
        goto error;
    }
    if (p_dma_chan->p_desc_used[p_dma_chan->current_index])
    {
        while(cnt < 10)
        {
            /*inval dma before read*/
            if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
            {
                g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_tx_desc_mem), sizeof(pci_exp_desc_mem_t));
            }
            if (p_tx_desc_mem->desc_done)
            {
                done = TRUE;
                break;
                /* last transmit is done */
            }

            sal_task_sleep(1);
            cnt++;
        }

        if (!done)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "last transmit is not done,%d\n", p_dma_chan->current_index);
            p_dma_chan->p_desc_used[p_dma_chan->current_index] = 0;
            ret = CTC_E_DMA_TX_FAILED;
            goto error;
        }
    }
    /* data + pkt CRC */
    pkt_len = p_pkt_tx_info->data_len + CTC_DMA_PKT_CRC_LEN + hdr_len;

    if(((pkt_len % 4)!=0)&&(p_gb_dma_master[lchip]->byte_order))
    {
        pkt_buf_len = pkt_len + 4 - (pkt_len % 4);
    }
    else
    {
        pkt_buf_len = pkt_len;
    }
    low_phy_addr = (p_tx_desc_mem->desc_mem_addr_low << 4);
    COMBINE_64BITS_DATA(p_gb_dma_master[lchip]->dma_high_addr, low_phy_addr, phy_addr);

    if (g_dal_op.phy_to_logic)
    {
        tx_pkt_addr = (uint8*)g_dal_op.phy_to_logic(lchip, (phy_addr));
    }

    if (user_flag)
    {
        sal_memcpy((uint8*)tx_pkt_addr, p_pkt_tx_info->header, hdr_len);
        sal_memcpy((uint8*)tx_pkt_addr + hdr_len, p_pkt_tx_info->data, pkt_len - hdr_len);
    }
    else
    {
        sal_memcpy((uint8*)tx_pkt_addr + hdr_len, p_pkt_tx_info->data, pkt_buf_len);
    }
    sys_greatbelt_packet_swap_payload(lchip, pkt_len - CTC_DMA_PKT_CRC_LEN - 32, ((uint8*)tx_pkt_addr + 32));


    /*flush dma after write*/
    if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
    {
        g_dal_op.dma_cache_inval(lchip, phy_addr, pkt_buf_len);
    }

    p_tx_desc_mem->desc_non_crc   = FALSE;
    p_tx_desc_mem->desc_crc_valid = FALSE;
    p_tx_desc_mem->desc_sop = TRUE;
    p_tx_desc_mem->desc_eop = TRUE;
    p_tx_desc_mem->desc_len = pkt_len;
    /* set desc done 0 before transmiting */
    p_tx_desc_mem->desc_done = 0;
    /*flush dma after write*/
    if ((NULL != g_dal_op.dma_cache_flush) && (NULL != g_dal_op.logic_to_phy))
    {
        g_dal_op.dma_cache_flush(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_tx_desc_mem), sizeof(pci_exp_desc_mem_t));
    }

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Current TX descriptor, desc_index=%d,done:%d error:%d sop:%d eop:%d desc_len:%d !\n",
                    p_dma_chan->current_index, p_tx_desc_mem->desc_done, p_tx_desc_mem->desc_error, p_tx_desc_mem->desc_sop, p_tx_desc_mem->desc_eop, p_tx_desc_mem->desc_len);

    /* tx valid num */
    sal_memset(&desc_rx_vld_num, 0, sizeof(desc_tx_vld_num_t));
    desc_rx_vld_num.data = 1;
    cmd = DRV_IOW(DescRxVldNum_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &desc_rx_vld_num), \
                   ret, error);
    /*update desc used info */
    p_dma_chan->p_desc_used[p_dma_chan->current_index] = 1;

    /* next descriptor, tx_desc_index: 0~tx_desc_num-1*/
    p_dma_chan->current_index =
        (p_dma_chan->current_index == (p_dma_chan->desc_num - 1)) ? 0 : (p_dma_chan->current_index + 1);

    if ((p_gb_dma_master[lchip]->dma_stats.tx_sync_cnt > 200)
            && (p_gb_dma_master[lchip]->dma_stats.sync_flag == 0))
    {
        p_gb_dma_master[lchip]->dma_stats.sync_flag = 1;
        sys_greatbelt_dma_sync_pkt_stats(lchip);
        p_gb_dma_master[lchip]->dma_stats.sync_flag = 0;
        p_gb_dma_master[lchip]->dma_stats.tx_sync_cnt = 0;
    }
    else
    {
        p_gb_dma_master[lchip]->dma_stats.tx_sync_cnt++;
    }

error:

    DMA_UNLOCK(p_mutex);

    return ret;
}
/**
 @brief Dma register callback function
*/
int32
sys_greatbelt_dma_register_cb(uint8 lchip, sys_dma_func_type_t type, DMA_LEARN_CB_FUN_P cb)
{
    if (NULL == p_gb_dma_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    switch(type)
    {
        case DMA_HW_LEARNING_FUNCTION:
            p_gb_dma_master[lchip]->sys_learning_dma.fdb_cb_func = cb;
            break;

        default:
                return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/**
 @brief Dma callback
*/
int32
sys_greatbelt_dma_callback(uint8 lchip, void* p_learn_info)
{
    int32 ret = 0;
    uint32 key_index = 0;
    ctc_hw_learn_aging_fifo_t* p_dma_learn = (ctc_hw_learn_aging_fifo_t*)p_learn_info;
    ctc_l2_addr_t* learn_rst = NULL;
    ctc_l2_addr_t* learn_rst_tmp = NULL;
    ctc_l2_sync_msg_t sync_rst[16];
    ctc_l2dflt_addr_t l2_def;
    uint8 index = 0;

    if (NULL == p_gb_dma_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    learn_rst = (ctc_l2_addr_t*)mem_malloc(MEM_DMA_MODULE, 16 * sizeof(ctc_l2_addr_t));
    if (NULL == learn_rst)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(learn_rst, 0, sizeof(ctc_l2_addr_t)*16);
    sal_memset(&sync_rst, 0, sizeof(ctc_l2_sync_msg_t)*16);
    sal_memset(&l2_def, 0 ,sizeof(l2_def));

    {
        for (index = 0; index < p_dma_learn->entry_num; index++)
        {
            learn_rst_tmp = (ctc_l2_addr_t*)learn_rst + index;
            key_index = p_dma_learn->learn_info[index].key_index;
            sync_rst[index].index = key_index;
            if (p_dma_learn->learn_info[index].is_aging)
            {
                CTC_SET_FLAG(sync_rst[index].flag, CTC_L2_SYNC_MSG_FLAG_IS_AGING);
            }
            else
            {
                sys_greatbelt_l2_get_fdb_by_index(lchip, key_index, 1, learn_rst_tmp, NULL);
                sal_memcpy(&sync_rst[index].mac, learn_rst_tmp->mac, sizeof(mac_addr_t));
                sync_rst[index].fid   = learn_rst_tmp->fid;
                l2_def.fid            = learn_rst_tmp->fid;
                sys_greatbelt_l2_get_default_entry_features(lchip, &l2_def);

                if (CTC_FLAG_ISSET(l2_def.flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT))
                {
                    sync_rst[index].gport = learn_rst_tmp->gport;
                    CTC_SET_FLAG(sync_rst[index].flag, CTC_L2_SYNC_MSG_FLAG_IS_LOGIC_PORT);
                }
                else
                {
                    sync_rst[index].gport = learn_rst_tmp->gport;
                }
            }

            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"mac:%.2x%.2x.%.2x%.2x.%.2x%.2x, gport: 0x%x fid:0x%x \n",
               learn_rst_tmp->mac[0], learn_rst_tmp->mac[1], learn_rst_tmp->mac[2],
               learn_rst_tmp->mac[3], learn_rst_tmp->mac[4], learn_rst_tmp->mac[5],
               learn_rst_tmp->gport, learn_rst_tmp->fid);
        }

        /* call back for fdb module */
        if (NULL != p_gb_dma_master[lchip]->sys_learning_dma.fdb_cb_func)
        {
            ret = p_gb_dma_master[lchip]->sys_learning_dma.fdb_cb_func(lchip, p_learn_info);
            if (ret < 0)
            {
                SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Call back for fdb module error!! \n");
            }
        }

        /* call back for system */
        if (NULL != p_gb_dma_master[lchip]->sys_learning_dma.learning_proc_func)
        {
            ret = p_gb_dma_master[lchip]->sys_learning_dma.learning_proc_func(lchip, (void*)&sync_rst, p_dma_learn->entry_num);
            if (ret < 0)
            {
                SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Call back for system error!! \n");
            }
        }
    }

    mem_free(learn_rst);

    return ret;
}

/**
 @brief set interval: s
*/
int32
sys_greatbelt_dma_set_stats_interval(uint8 lchip, uint32  stats_interval)
{
    uint32 cmd = 0;
    dma_stats_interval_cfg_t dma_stats_intval;
    uint32 timer_l = 0;
    uint32 timer_h = 0;
    uint32 timer_full = 0;

    sal_memset(&dma_stats_intval, 0, sizeof(dma_stats_interval_cfg_t));

    timer_full = 0xffffffff/(SYS_SUP_CLOCK*1000000);
    if (timer_full >= stats_interval)
    {
        timer_l = stats_interval*(SYS_SUP_CLOCK*1000000);
        timer_h = 0;
    }
    else
    {
        timer_l = 0xffffffff;
        timer_h = stats_interval/timer_full;
    }

    dma_stats_intval.cfg_stats_interval_low = timer_l;
    dma_stats_intval.cfg_stats_interval_high= timer_h;

    cmd = DRV_IOW(DmaStatsIntervalCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_stats_intval));


    return CTC_E_NONE;
}

#define _DMA_ISR_BEGIN

STATIC int32
_sys_greatbelt_dma_clear_pkt_desc(uint8 lchip, sys_dma_chan_t* p_dma_chan, uint32 sop_index, uint32 buf_count)
{
    uint32 i = 0;
    uint32 desc_index = 0;
    sys_dma_desc_t* p_base_desc = p_dma_chan->p_desc;
    pci_exp_desc_mem_t* p_desc = NULL;
    uint16 desc_len = 0;
    drv_work_platform_type_t platform_type;
    desc_tx_vld_num_t tx_vld_num;
    uint32 cmd = 0;

    drv_greatbelt_get_platform_type(&platform_type) ;

    if(SYS_DMA_OAM_BFD_CHANNEL == p_dma_chan->channel_id)
    {
        desc_len = SYS_DMA_BFD_DESC_LEN;
    }
    else
    {
        desc_len = p_gb_dma_master[lchip]->pkt_rx_size_per_desc;
    }

    desc_index = sop_index;
    for (i = 0; i < buf_count; i++)
    {
        p_desc = &(p_base_desc[desc_index].pci_exp_desc);

        SYS_DMA_SET_DEFAULT_RX_DESC(p_desc, desc_len);
        /*flush dma after write*/
        if ((NULL != g_dal_op.dma_cache_flush) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_flush(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_desc), sizeof(pci_exp_desc_mem_t));
        }

        desc_index++;
        if (desc_index >= p_dma_chan->desc_num)
        {
            desc_index = 0;
        }
    }

    if (platform_type == HW_PLATFORM)
    {
        tx_vld_num.data = buf_count;
    }
    else
    {
        tx_vld_num.data = p_dma_chan->desc_num;
    }

    /*for 2 chips, just using lchip 0 DmaCtl*/
    cmd = DRV_IOW(DescTxVldNum_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &tx_vld_num);

    return CTC_E_NONE;
}

/**
@brief  DMA packet rx function process
*/
int32
_sys_greatbelt_dma_pkt_rx_func(uint8 lchip, uint8 chan)
{
    ctc_pkt_buf_t pkt_buf[64];
    ctc_pkt_rx_t pkt_rx;
    ctc_pkt_rx_t* p_pkt_rx = &pkt_rx;
    sys_dma_chan_t* p_dma_chan = NULL;
    sys_dma_desc_t* p_base_desc = NULL;
    volatile pci_exp_desc_mem_t* p_desc = NULL;
    uint32 cur_index = 0;
    uint32 sop_index = 0;
    uint32 buf_count = 0;
    uint32 low_phy_addr = 0;
    uint64 phy_addr = 0;
    uint32 process_cnt = 0;
    uint32 index = 0;
    int32 chan_index = chan - SYS_DMA_RX_MIN_CHANNEL;
    uint8 need_eop = 0;
    uint32 wait_cnt = 0;
    int32 ret = 0;

    sal_memset(p_pkt_rx, 0, sizeof(ctc_pkt_rx_t));
    p_pkt_rx->mode = CTC_PKT_MODE_DMA;
    p_pkt_rx->pkt_buf = pkt_buf;

    if (chan == SYS_DMA_OAM_BFD_CHANNEL)
    {
        p_dma_chan = &p_gb_dma_master[lchip]->sys_oam_bfd_dma;

    }
    else
    {
        p_dma_chan = &p_gb_dma_master[lchip]->sys_packet_rx_dma[chan_index];
    }

    p_base_desc = p_dma_chan->p_desc;
    cur_index = p_dma_chan->current_index;
    for (;; cur_index++)
    {
        if (cur_index >= p_dma_chan->desc_num)
        {
            cur_index = 0;
        }
        ret = sys_greatbelt_chip_check_active(lchip);
        if (ret < 0)
        {
            break;
        }
        p_desc = &(p_base_desc[cur_index].pci_exp_desc);
        /*inval dma before read*/
        if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_desc), sizeof(pci_exp_desc_mem_t));
        }
        if (0 == p_desc->desc_done)
        {
            if (need_eop)
            {
                SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Desc not done, But need eop!!desc index %d\n", cur_index);

                while(wait_cnt < 0xffff)
                {
                    /*inval dma before read*/
                    if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
                    {
                        g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_desc), sizeof(pci_exp_desc_mem_t));
                    }
                    if (p_desc->desc_done)
                    {
                        break;
                    }
                    wait_cnt++;
                }

                /* Cannot get EOP, means no EOP packet error, just clear desc*/
                if (wait_cnt >= 0xffff)
                {
                    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "No EOP, desc index %d, buf_count %d\n", cur_index, buf_count);
                   _sys_greatbelt_dma_clear_pkt_desc(lchip, p_dma_chan, sop_index, buf_count);
                   buf_count = 0;
                   need_eop = 0;
                   break;
                }
                wait_cnt = 0;
            }
            else
            {
                SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "No desc is not done, processed %d desc index %d\n", process_cnt, cur_index);
                break;
            }
        }

        /*Before get EOP, next packet SOP come, no EOP packet error, drop error packet */
        if (need_eop && p_desc->desc_sop)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "No EOP Detect, desc index %d, buf_count %d\n", sop_index, buf_count);
            _sys_greatbelt_dma_clear_pkt_desc(lchip, p_dma_chan, sop_index, buf_count);
            buf_count = 0;
            need_eop = 0;
        }

        /* Cannot get SOP, means no SOP packet error, just clear desc*/
        if (0 == buf_count)
        {
            if (0 == p_desc->desc_sop)
            {
                SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,
                                "[DMA] PKT RX error, lchip %d chan %d first is not SOP desc index %d\n", lchip, chan, cur_index);
                _sys_greatbelt_dma_clear_pkt_desc(lchip, p_dma_chan, cur_index, 1);
                 goto error;
            }
        }

        if (p_desc->desc_sop)
        {
            sop_index = cur_index;
            p_pkt_rx->pkt_len = 0;
            need_eop = 1;
        }
        low_phy_addr = (p_desc->desc_mem_addr_low << 4);

        COMBINE_64BITS_DATA(p_gb_dma_master[lchip]->dma_high_addr, low_phy_addr, phy_addr);
        /*inval dma before read*/
        if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_inval(lchip, phy_addr, sizeof(pci_exp_desc_mem_t));
        }
        p_pkt_rx->pkt_buf[buf_count].data = (uint8*)g_dal_op.phy_to_logic(lchip, phy_addr);
        p_pkt_rx->pkt_buf[buf_count].len = p_desc->desc_len;

        buf_count++;

        if (p_desc->desc_eop)
        {
            p_pkt_rx->buf_count = buf_count;
            p_pkt_rx->dma_chan = chan;

            for (index = 0; index < buf_count; index++)
            {
                p_pkt_rx->pkt_len += p_pkt_rx->pkt_buf[index].len;
            }

            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                            "[DMA] PKT RX lchip %d dma_chan %d buf_count %d pkt_len %d curr_index %d\n",
                            lchip, p_pkt_rx->dma_chan, p_pkt_rx->buf_count, p_pkt_rx->pkt_len,
                            ((p_dma_chan->current_index + process_cnt)%(p_dma_chan->desc_num)));

            sys_greatbelt_packet_rx(lchip, p_pkt_rx);

            process_cnt += buf_count;
            _sys_greatbelt_dma_clear_pkt_desc(lchip, p_dma_chan, sop_index, buf_count);

            buf_count = 0;
            need_eop = 0;
        }

error:
        /* process 1000 packets needs release cpu */
        if ((process_cnt+1) >= 1000)
        {
            process_cnt = 0;
            sal_task_sleep(1);
        }
    }

    p_dma_chan->current_index = cur_index;

    return CTC_E_NONE;
}

/**
@brief  DMA learning function process
*/
STATIC int32
_sys_greatbelt_dma_learning_func(uint8 lchip, uint32 chan)
{
    uint32  cmd = 0;
    uint32  mem_valid = 0;
    sys_dma_fib_t* p_learn_fifo_result = NULL;
    uint16  loop = 0;
    uint16  max_learning_num = 0;
    void*   p_mem_base = NULL;
    uint32   valid_field = 0;
    ctc_hw_learn_aging_fifo_t dma_learn;
    uint8 mem_index = 0;
    sys_dma_learning_t* p_dma_learning_info;
    dma_learn_mem_ctl_t mem_ctl;
    uint16 process_cnt = 0;
    uint8 max_block = 0;
    uint16 learning_cnt = 0;
    uint16 integrity_cnt = 0;

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[DMA] ISR TX lchip %d chan %d\n", lchip, chan);

    sal_memset(&dma_learn, 0, sizeof(dma_learn));
    sal_memset(&mem_ctl, 0, sizeof(dma_learn_mem_ctl_t));

    /* init check */
    if (NULL == p_gb_dma_master[lchip])
    {
        return CTC_E_DMA_NOT_INIT;
    }

    p_dma_learning_info = &(p_gb_dma_master[lchip]->sys_learning_dma);

    max_block = ((p_dma_learning_info->hw_learning_sync_en)?1:4);

    /* check whether use sw table */
    if ((p_dma_learning_info->fdb_cb_func == NULL) && (p_dma_learning_info->learning_proc_func == NULL))
    {
        /* just return all block to available */
        cmd = DRV_IOR(DmaLearnMemCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &mem_ctl);
        mem_ctl.learn_mem0_avail = 1;
        mem_ctl.learn_mem1_avail = 1;
        mem_ctl.learn_mem2_avail = 1;
        mem_ctl.learn_mem3_avail = 1;
        cmd = DRV_IOW(DmaLearnMemCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &mem_ctl);

        return CTC_E_NONE;
    }

    mem_index = p_dma_learning_info->cur_block_idx;

    while (1)
    {
        switch (mem_index)
        {
        case 0:
            p_mem_base  = p_gb_dma_master[lchip]->sys_learning_dma.p_mem0_base;
            max_learning_num  = p_gb_dma_master[lchip]->sys_learning_dma.max_mem0_learning_num;
            valid_field = DmaLearnMemCtl_LearnMem0Avail_f;
            break;

        case 1:
            p_mem_base  = p_gb_dma_master[lchip]->sys_learning_dma.p_mem1_base;
            max_learning_num  = p_gb_dma_master[lchip]->sys_learning_dma.max_mem1_learning_num;
            valid_field = DmaLearnMemCtl_LearnMem1Avail_f;
            break;

        case 2:
            p_mem_base  = p_gb_dma_master[lchip]->sys_learning_dma.p_mem2_base;
            max_learning_num  = p_gb_dma_master[lchip]->sys_learning_dma.max_mem2_learning_num;
            valid_field = DmaLearnMemCtl_LearnMem2Avail_f;
            break;

        case 3:
            p_mem_base  = p_gb_dma_master[lchip]->sys_learning_dma.p_mem3_base;
            max_learning_num  = p_gb_dma_master[lchip]->sys_learning_dma.max_mem3_learning_num;
            valid_field = DmaLearnMemCtl_LearnMem3Avail_f;
            break;

        default:
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "mem index invalid!!!mem_index:0x%x \n", mem_index);
            break;

        }

        /* add learn_node to learn_list */
        loop = 0;

        for(;;)
        {
            while((loop + g_sys_cur_learning_cnt[mem_index]) < max_learning_num)
            {
                p_learn_fifo_result = (sys_dma_fib_t*)(p_mem_base) + (loop + g_sys_cur_learning_cnt[mem_index]);
                if (p_learn_fifo_result->entry_vld)
                {
                    sys_greatbelt_dma_learn_info_mapping(lchip, p_learn_fifo_result, &dma_learn.learn_info[learning_cnt%CTC_DMA_LEARN_PROC_NUM]);
                    if (0==((learning_cnt+1)%CTC_DMA_LEARN_PROC_NUM))
                    {
                        /* process learning entry */
                        dma_learn.entry_num  = CTC_DMA_LEARN_PROC_NUM;
                        sys_greatbelt_dma_callback(lchip, (void*)&dma_learn);
                        integrity_cnt++;
                    }
                    loop++;
                    /* used for data callback */
                    learning_cnt++;
                    /* used for task_delay */
                    process_cnt++;
                    p_learn_fifo_result->entry_vld = 0;
                }
                else
                {
                    /* no valid entry, quit */
                    break;
                }
            }

            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "\nmem %d received %d entries\n", mem_index, loop);

            /* process left learning entry */
            if ((learning_cnt - integrity_cnt*16) > 0)
            {
                dma_learn.entry_num  = (learning_cnt - integrity_cnt*16);
                sys_greatbelt_dma_callback(lchip, &dma_learn);
            }

            learning_cnt = 0;
            integrity_cnt = 0;

            if ((((process_cnt+1) % 1000) == 0) &&(chan != SYS_DMA_OAM_BFD_CHANNEL))
            {
                sal_task_sleep(1);
            }

            /* havn't process one block */
            if ((loop + g_sys_cur_learning_cnt[mem_index]) < max_learning_num)
            {
                /* get next entry */
                p_learn_fifo_result = (sys_dma_fib_t*)(p_mem_base) + (loop + g_sys_cur_learning_cnt[mem_index]);
                if (p_learn_fifo_result->entry_vld)
                {
                    /* continue process */
                    continue;
                }
                else
                {
                    break;
                }
            }
            else
            {
                /* block is full */
                break;
            }
        }

        g_sys_cur_learning_cnt[mem_index] += loop;

        /* already processed all learning entry in mem_index, so reset mem_index available  */
        if (g_sys_cur_learning_cnt[mem_index] >= max_learning_num)
        {
            /* set the mem block is valid, and can be used by DMA again */
            mem_valid = 1;
            cmd = DRV_IOW(DmaLearnMemCtl_t, valid_field);
            DRV_IOCTL(lchip, 0, cmd, &mem_valid);

            /* update current block index */
            p_dma_learning_info->cur_block_idx = ((mem_index + 1) >= max_block) ? 0 : (mem_index + 1);

            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mem %d is full and processed \n", mem_index);

            loop = 0;
            /* next memory , mem0->mem1, mem1->mem2 mem2->mem3 mem3->mem0 */
            mem_index = ((mem_index + 1) >= max_block) ? 0 : (mem_index + 1);
            g_sys_cur_learning_cnt[mem_index] = 0;
        }
        else
        {
            break;
        }
    }

    return CTC_E_NONE;
}

/**
@brief DMA stats function process
*/
int32
_sys_greatbelt_dma_stats_func(uint8 lchip, uint32 chan)
{
    uint16 desc_index = 0;
    uint8 process_num = 0;
    sys_dma_chan_t* p_stats_cfg;
    uint32 low_phy_addr = 0;
    uint64 phy_addr = 0;
    uintptr logic_addr = 0;
    uint32 cmd = 0;
    dma_stats_bmp_cfg_t dma_stats_bmp;
    sys_stats_dma_sync_t dma_sync;
    desc_tx_vld_num_t desc_tx_vld_num;

    sal_memset(&desc_tx_vld_num, 0, sizeof(desc_tx_vld_num_t));

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[DMA] ISR TX lchip %d chan %d\n", lchip, chan);
    p_stats_cfg = &p_gb_dma_master[lchip]->sys_stats_dma;

    cmd = DRV_IOR(DmaStatsBmpCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_stats_bmp));

    /* get current desp, check desp done */
    desc_index = p_stats_cfg->current_index;

    sal_memset(&dma_sync, 0, sizeof(sys_stats_dma_sync_t));

    while (process_num < p_stats_cfg->desc_num)
    {
        SYS_LCHIP_CHECK_ACTIVE(lchip);
        if (desc_index >= p_stats_cfg->desc_num)
        {
            desc_index = 0;
        }

        /*inval dma before read*/
        if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)&p_stats_cfg->p_desc[desc_index]), sizeof(sys_dma_desc_t));
        }
        if (0 == p_stats_cfg->p_desc[desc_index].pci_exp_desc.desc_done)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "stats desc not done! index:0x%x \n", desc_index);
            break;
        }

        /* get Stats */
        low_phy_addr = (p_stats_cfg->p_desc[desc_index].pci_exp_desc.desc_mem_addr_low << 4);
        COMBINE_64BITS_DATA(p_gb_dma_master[lchip]->dma_high_addr, low_phy_addr, phy_addr);

        if (g_dal_op.phy_to_logic)
        {
            logic_addr = (uintptr)g_dal_op.phy_to_logic(lchip, (phy_addr));
        }

        /* do quadmac stats */
        if (dma_stats_bmp.qmac_stats_en_bmp && ((0 == (desc_index&0x01)) || (dma_stats_bmp.sgmac_stats_en_bmp == 0)))
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "***do quadmac stats**** \n");

            dma_sync.stats_type = CTC_STATS_TYPE_GMAC;
            dma_sync.p_base_addr = (void*)logic_addr;
            sys_greatbelt_stats_sync_dma_mac_stats(lchip, &dma_sync, dma_stats_bmp.qmac_stats_en_bmp);

            /* reset desc to default */
            SYS_DMA_SET_DEFAULT_RX_DESC((&(p_stats_cfg->p_desc[desc_index].pci_exp_desc)), g_sys_stats_desc_len[0]);
            desc_tx_vld_num.data = 1;
            cmd = DRV_IOW(DescTxVldNum_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_stats_cfg->channel_id, cmd, &desc_tx_vld_num));
        }

        /* do sgmac stats */
        if ((dma_stats_bmp.sgmac_stats_en_bmp) && (1 == (desc_index&0x01)))
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "***do sgmac stats**** \n");

            dma_sync.stats_type = CTC_STATS_TYPE_SGMAC;
            dma_sync.p_base_addr = (void*)logic_addr;
            sys_greatbelt_stats_sync_dma_mac_stats(lchip, &dma_sync, dma_stats_bmp.sgmac_stats_en_bmp);

            /* reset desc to default */
            SYS_DMA_SET_DEFAULT_RX_DESC((&(p_stats_cfg->p_desc[desc_index].pci_exp_desc)), g_sys_stats_desc_len[1]);
            desc_tx_vld_num.data = 1;
            cmd = DRV_IOW(DescTxVldNum_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_stats_cfg->channel_id, cmd, &desc_tx_vld_num));
        }

        process_num++;
        desc_index++;
    }

    /* update desc index */
    p_stats_cfg->current_index = ((p_stats_cfg->current_index + process_num) % (p_stats_cfg->desc_num));

    return CTC_E_NONE;
}

/**
@brief DMA packet rx thread for packet rx channel
*/
STATIC void
_sys_greatbelt_dma_pkt_rx_thread(void* param)
{
    int32 ret = 0;
    uint8 lchip = 0;
    uint32 chan_id = 0;
    uint32 chan_index = 0;
    ctc_intr_type_t type;
    uint32 para = (uintptr)param;
    sys_dma_thread_t* p_thread_info = (sys_dma_thread_t*)&para;

    chan_id = p_thread_info->chan_id;
    lchip = p_thread_info->lchip;
    chan_index = chan_id - SYS_DMA_RX_MIN_CHANNEL;

    type.intr = CTC_INTR_GB_DMA_FUNC;
    type.sub_intr = SYS_INTR_GB_SUB_DMA_FUNC_TX_CHAN_0 + chan_id;

    while (1)
    {
        ret = sal_sem_take(p_gb_dma_master[lchip]->packet_rx_sync_sem[chan_index], SAL_SEM_FOREVER);
        if (0 != ret)
        {
            continue;
        }
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);

        _sys_greatbelt_dma_pkt_rx_func(lchip, chan_id);

        /* release mask channel isr */
        sys_greatbelt_interrupt_set_en(lchip, &type, TRUE);

        if ((p_gb_dma_master[lchip]->dma_stats.rx_sync_cnt > 1000)
            && (p_gb_dma_master[lchip]->dma_stats.sync_flag == 0))
        {
            p_gb_dma_master[lchip]->dma_stats.sync_flag = 1;
            sys_greatbelt_dma_sync_pkt_stats(lchip);
            p_gb_dma_master[lchip]->dma_stats.sync_flag = 0;
            p_gb_dma_master[lchip]->dma_stats.rx_sync_cnt = 0;
        }
        else
        {
            p_gb_dma_master[lchip]->dma_stats.rx_sync_cnt++;
        }

    }

    return;
}

/**
@brief DMA learning thread for learning channel
*/
STATIC void
_sys_greatbelt_dma_learning_thread(void* param)
{
    int32 ret = 0;
    uint8 lchip = 0;
    ctc_intr_type_t type;
    uint32 chan_id = 0;
    uint32 para = (uintptr)param;
    sys_dma_thread_t* p_thread_info = (sys_dma_thread_t*)&para;


    chan_id = p_thread_info->chan_id;
    lchip = p_thread_info->lchip;

    type.intr = CTC_INTR_GB_DMA_FUNC;
    type.sub_intr = SYS_INTR_GB_SUB_DMA_FUNC_TX_CHAN_0 + chan_id;

    while (1)
    {
        ret = sal_sem_take(p_gb_dma_master[lchip]->learning_sync_sem, SAL_SEM_FOREVER);
        if (0 != ret)
        {
            continue;
        }
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);

        _sys_greatbelt_dma_learning_func(lchip, chan_id);

        /* release mask channel isr */
        sys_greatbelt_interrupt_set_en(lchip, &type, TRUE);

    }

    return;
}

/**
@brief DMA stats thread for stats channel
*/
STATIC void
_sys_greatbelt_dma_stats_thread(void* param)
{
    int32 ret = 0;
    uint8 lchip = 0;
    ctc_intr_type_t type;
    uint32 cmd = 0;
    uint32 chan_id = 0;
    uint32 dma_func = (1 << (SYS_INTR_GB_SUB_DMA_FUNC_TX_CHAN_0+chan_id));
    uint32 para = (uintptr)param;
    sys_dma_thread_t* p_thread_info = (sys_dma_thread_t*)&para;

    chan_id = p_thread_info->chan_id;
    lchip = p_thread_info->lchip;

    type.intr = CTC_INTR_GB_DMA_FUNC;
    type.sub_intr = SYS_INTR_GB_SUB_DMA_FUNC_TX_CHAN_0+chan_id;

    while (1)
    {
        ret = sal_sem_take(p_gb_dma_master[lchip]->stats_sync_sem, SAL_SEM_FOREVER);
        if (0 != ret)
        {
            continue;
        }
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);

        _sys_greatbelt_dma_stats_func(lchip, chan_id);

        /* clear interrupt */
        cmd = DRV_IOW(PcieFuncIntr_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, &dma_func);

        /* release mask channel isr */
        sys_greatbelt_interrupt_set_en(lchip, &type, TRUE);

    }

    return;
}

/**
@brief DMA TX interrupt serve routing
*/
STATIC int32
_sys_greatbelt_dma_isr_func_tx(uint8 lchip, uint8 chan)
{
    ctc_intr_type_t type;

    type.intr = CTC_INTR_GB_DMA_FUNC;
    type.sub_intr = SYS_INTR_GB_SUB_DMA_FUNC_TX_CHAN_0+chan;

    /* DMA TX funtion1 : Pactet RX */
    if ((SYS_DMA_RX_MIN_CHANNEL <= chan) && (chan <= (SYS_DMA_RX_MAX_CHANNEL)))
    {
        sys_greatbelt_interrupt_set_en(lchip, &type, FALSE);
        /* push sync sem */
        CTC_ERROR_RETURN(sal_sem_give(p_gb_dma_master[lchip]->packet_rx_sync_sem[chan - SYS_DMA_RX_MIN_CHANNEL]));
    }

    /* for bfd function */
    if (SYS_DMA_OAM_BFD_CHANNEL == chan)
    {
        sys_greatbelt_interrupt_set_en(lchip, &type, FALSE);
        /* push sync sem */
        CTC_ERROR_RETURN(sal_sem_give(p_gb_dma_master[lchip]->packet_rx_sync_sem[SYS_DMA_RX_CHAN_NUM]));
    }

    /* DMA TX function2: HW learning */
    if (SYS_DMA_LEARN_CHANNEL == chan)
    {
        sys_greatbelt_interrupt_set_en(lchip, &type, FALSE);
        /* push sync sem */
        CTC_ERROR_RETURN(sal_sem_give(p_gb_dma_master[lchip]->learning_sync_sem));
    }

    /* DMA TX function3: Stats DMA */
    if (SYS_DMA_STATS_CHANNEL == chan)
    {
        sys_greatbelt_interrupt_set_en(lchip, &type, FALSE);
        /* push sync sem */
        CTC_ERROR_RETURN(sal_sem_give(p_gb_dma_master[lchip]->stats_sync_sem));
    }

    /* DMA TX function4: BAT Read Table */
    /* no need TODO */

    return CTC_E_NONE;
}

/**
@brief DMA RX interrupt serve routing
*/
STATIC int32
_sys_greatbelt_dma_isr_func_rx(uint8 lchip, uint8 chan)
{
    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[DMA] ISR TX lchip %d chan %d\n", lchip, chan);

    /* DMA RX function1: Packet Tx */
    if (SYS_DMA_TX_CHANNEL == chan)
    {
 /*        _sys_greatbelt_dma_isr_func_pkt_tx(lchip, chan);*/
    }

    /* DMA RX function2: Table Write */
    if (SYS_DMA_WTABLE_CHANNEL == chan)
    {
 /*        _sys_greatbelt_dma_isr_func_tbl_write(lchip, chan);*/
    }

    return CTC_E_NONE;
}

/**
@brief DMA function interrupt serve routing
*/
int32
sys_greatbelt_dma_isr_func(uint8 lchip, uint32 intr, void* p_data)
{
    uint8 chan = 0;
    uint8 i = 0;
    uint32* p_dma_func = (uint32*)p_data;   /* pcie_func_intr_t */

    SYS_DMA_INIT_CHECK(lchip);
    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[DMA] Func ISR lchip %d intr %d sub_intr 0x%08X\n", lchip, intr, p_dma_func[0]);

    for (i = 0; i < SYS_INTR_GB_SUB_DMA_FUNC_MAX; i++)
    {
        if (CTC_BMP_ISSET(p_dma_func, i))
        {
            if (i <= SYS_INTR_GB_SUB_DMA_FUNC_RX_CHAN_7)
            {
                chan = i;
                _sys_greatbelt_dma_isr_func_rx(lchip, chan);
            }
            else if (i <= SYS_INTR_GB_SUB_DMA_FUNC_TX_CHAN_7)
            {
                chan = i - SYS_INTR_GB_SUB_DMA_FUNC_TX_CHAN_0;
                _sys_greatbelt_dma_isr_func_tx(lchip, chan);
            }
            else
            {
                return CTC_E_INTR_NOT_SUPPORT;
            }
        }
    }

    return CTC_E_NONE;
}

/**
@brief DMA normal interrupt server routing
*/
int32
sys_greatbelt_dma_isr_normal(uint8 lchip, uint32 intr, void* p_data)
{
    uint32* p_dma_normal = (uint32*)p_data;   /* pci_exp_core_interrupt_normal_t */
    ctc_intr_type_t type;
    ctc_intr_status_t status;
    uint32 idx = 0;
    uint32 offset = 0;
    uint32 i = 0;

    sal_memset(&type, 0, sizeof(ctc_intr_type_t));
    sal_memset(&status, 0, sizeof(ctc_intr_status_t));
    SYS_DMA_INIT_CHECK(lchip);
    /* 2. get & clear sub interrupt status */
    for (i = 0; i < SYS_INTR_GB_SUB_NORMAL_MAX; i++)
    {
        idx = i / CTC_UINT32_BITS;
        offset = i % CTC_UINT32_BITS;

        if (0 == p_dma_normal[idx])
        {
            /* bypass 32Bit 0 */
            i = (idx + 1) * CTC_UINT32_BITS;
            continue;
        }

        if (CTC_IS_BIT_SET(p_dma_normal[idx], offset))
        {
            type.sub_intr = i;
            /* get sub interrupt status */
            sys_greatbelt_interrupt_get_status(lchip, &type, &status);

            /* clear sub interrupt status */
            sys_greatbelt_interrupt_clear_status(lchip, &type, FALSE);
        }
    }

    return CTC_E_NONE;
}

#define _DMA_INIT_BEGIN

/**
@brief DMA sync mechanism init for function interrupt
*/
int32
_sys_greatbelt_dma_sync_init(uint8 lchip, uint32 func_type)
{
    int32 ret = 0;
    uint8 index = 0;
    uintptr chan_index = 0;
    uintptr chan_id = 0;
    int32 prio = 0;
    uint32 special_channel_num = 0;
    char buffer[SAL_TASK_MAX_NAME_LEN];
    uint32 para = 0;
    sys_dma_thread_t* p_thread_info = (sys_dma_thread_t*)&para;


    special_channel_num = SYS_DMA_BFD_CHAN_USE_NUM;
    p_thread_info->lchip = lchip;

    switch (func_type)
    {
    case CTC_DMA_FUNC_PACKET_RX:
        for (index = 0; index < (p_gb_dma_master[lchip]->pkt_rx_chan_num+special_channel_num); index++)
        {
            /* create sync sem for packet rx */
            if (index < p_gb_dma_master[lchip]->pkt_rx_chan_num)
            {
                chan_id = SYS_DMA_RX_MIN_CHANNEL + index;
            }
            else
            {
                chan_id = SYS_DMA_OAM_BFD_CHANNEL;
            }

            chan_index = chan_id - SYS_DMA_RX_MIN_CHANNEL;
            ret = sal_sem_create(&p_gb_dma_master[lchip]->packet_rx_sync_sem[chan_index], 0);
            if (ret < 0)
            {
                return CTC_E_NOT_INIT;
            }

	        sal_sprintf(buffer, "ctcPktRx%d-%d", index, lchip);
            prio = p_gb_dma_master[lchip]->packet_rx_priority[chan_index];
            /* create packet rx thread */

            p_thread_info->chan_id = chan_id;
            ret = sal_task_create(&p_gb_dma_master[lchip]->dma_packet_rx_thread[chan_index], buffer,
                                  SAL_DEF_TASK_STACK_SIZE, prio, _sys_greatbelt_dma_pkt_rx_thread, (void*)(uintptr)para);
            if (ret < 0)
            {
                return CTC_E_NOT_INIT;
            }
        }
        break;

    case CTC_DMA_FUNC_PACKET_TX:
        /*no need TODO */
        break;

    case CTC_DMA_FUNC_TABLE_W:
        /*no need TODO */
        break;

    case CTC_DMA_FUNC_TABLE_R:
        /*no need TODO */
        break;

    case CTC_DMA_FUNC_STATS:
        /* create sync sem for dma stats */
        ret = sal_sem_create(&p_gb_dma_master[lchip]->stats_sync_sem, 0);
        if (ret < 0)
        {
            return CTC_E_NOT_INIT;
        }

        chan_id = SYS_DMA_STATS_CHANNEL;

        prio = p_gb_dma_master[lchip]->stats_priority;

        /* create dma stats thread */
        p_thread_info->chan_id = chan_id;
        sal_sprintf(buffer, "DmaStats-%d", lchip);
        ret = sal_task_create(&p_gb_dma_master[lchip]->dma_stats_thread, buffer,
                              SAL_DEF_TASK_STACK_SIZE, prio, _sys_greatbelt_dma_stats_thread, (void*)(uintptr)para);
        if (ret < 0)
        {
            return CTC_E_NOT_INIT;
        }

        break;

    case CTC_DMA_FUNC_HW_LEARNING:
        /* create sync sem for learning */
        ret = sal_sem_create(&p_gb_dma_master[lchip]->learning_sync_sem, 0);
        if (ret < 0)
        {
            return CTC_E_NOT_INIT;
        }

        chan_id = SYS_DMA_LEARN_CHANNEL;

        prio = p_gb_dma_master[lchip]->learning_priority;

        /* create dma learning thread */
        p_thread_info->chan_id = chan_id;
        sal_sprintf(buffer, "DmaLearning-%d", lchip);
        ret = sal_task_create(&p_gb_dma_master[lchip]->dma_learning_thread, buffer,
                              SAL_DEF_TASK_STACK_SIZE, prio, _sys_greatbelt_dma_learning_thread, (void*)(uintptr)para);
        if (ret < 0)
        {
            return CTC_E_NOT_INIT;
        }

        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/**
@brief init dma function for OAM bfd (DMA--->CPU)
*/
int32
_sys_greatbelt_dma_oam_bfd_init(uint8 lchip, sys_dma_chan_t* p_bfd_chan)
{
    sys_dma_desc_t* p_sys_desc_pad = NULL;
    uint16 desc_index = 0;
    uint16 desc_num = 0;
    uint32 phy_addr = 0;
    uint32 cmd = 0;
    pci_exp_desc_mem_t* p_desc = NULL;
    desc_tx_mem_base_t desc_tx_mem_base;
    desc_tx_mem_depth_t desc_tx_mem_depth;
    desc_tx_vld_num_t     desc_tx_vld_num;
    dma_chan_map_t   dma_chan_map;
    dma_misc_ctl_t dma_misc_ctl;
    void*  p_mem_addr = NULL;

    desc_num = p_bfd_chan->desc_num;
    p_bfd_chan->direction = CTC_DMA_DIR_RX;

    p_sys_desc_pad = (sys_dma_desc_t*)g_dal_op.dma_alloc(lchip, (desc_num) * sizeof(sys_dma_desc_t), 0);
    if (NULL == p_sys_desc_pad)
    {
        return CTC_E_NO_MEMORY;
    }

    p_bfd_chan->p_desc = p_sys_desc_pad;
    p_bfd_chan->mem_base = (uintptr)p_sys_desc_pad;

    sal_memset(p_sys_desc_pad, 0, (desc_num) * sizeof(sys_dma_desc_t));

    /* alloc memory for every descriptor */
    for (desc_index = 0; desc_index < desc_num; desc_index++)
    {
        p_desc = &(p_sys_desc_pad[desc_index].pci_exp_desc);

        p_mem_addr = g_dal_op.dma_alloc(lchip, SYS_DMA_BFD_DESC_LEN, 0);
        if (NULL == p_mem_addr)
        {
            return CTC_E_NO_MEMORY;
        }

        phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_mem_addr);
        p_desc->desc_mem_addr_low = (phy_addr >> 4);
        p_desc->desc_len = SYS_DMA_BFD_DESC_LEN;
    }

    /* TxMemBase */
    sal_memset(&desc_tx_mem_base, 0, sizeof(desc_tx_mem_base));
    phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_sys_desc_pad);
    desc_tx_mem_base.data = (phy_addr >> 4);
    cmd = DRV_IOW(DescTxMemBase_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_bfd_chan->channel_id, cmd, &desc_tx_mem_base));

    /* TxMemDepth */
    sal_memset(&desc_tx_mem_depth, 0, sizeof(desc_tx_mem_depth));
    desc_tx_mem_depth.data = desc_num;
    cmd = DRV_IOW(DescTxMemDepth_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_bfd_chan->channel_id, cmd, &desc_tx_mem_depth));

    /* TxVldNum */
    sal_memset(&desc_tx_vld_num, 0, sizeof(desc_tx_vld_num));
    desc_tx_vld_num.data = desc_num;
    cmd = DRV_IOW(DescTxVldNum_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_bfd_chan->channel_id, cmd, &desc_tx_vld_num));

    /* mapping cos 63 to bfd channel */
    cmd = DRV_IOW(DmaChanMap_t, DRV_ENTRY_FLAG);
    dma_chan_map.cos = p_bfd_chan->channel_id;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_DMA_BFD_USE_COS, cmd, &dma_chan_map));

    /* enable dma packet tx function */
    cmd = DRV_IOR(DmaMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_misc_ctl));
    dma_misc_ctl.cfg_dma_en = 1;
    dma_misc_ctl.dma_tx_chan_en |= (1 << p_bfd_chan->channel_id);
    cmd = DRV_IOW(DmaMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_misc_ctl));

    return CTC_E_NONE;
}

/**
@brief init dma packet TX(CPU--->DMA)
*/
int32
_sys_greatbelt_dma_packet_tx_init(uint8 lchip, sys_dma_chan_t* p_dma_chan_base)
{
    uint16 desc_num = 0;
    desc_rx_mem_base_t desc_rx_mem_base;
    desc_rx_mem_depth_t desc_rx_mem_depth;
    desc_rx_vld_num_t desc_rx_vld_num;
    dma_misc_ctl_t dma_misc_ctl;
    uint32 phy_addr = 0;
    uint32 cmd = 0;
    uint32 dma_func = 0;
    uint8 dma_chan_index = 0;
    sys_dma_chan_t* p_dma_chan = NULL;
    uint16 desc_index = 0;
    pci_exp_desc_mem_t* p_desc = NULL;
    void*  p_mem_addr = NULL;

    for (dma_chan_index = 0; dma_chan_index < SYS_DMA_PKT_TX_CHAN_NUM; dma_chan_index++)
    {
        p_dma_chan = &(p_dma_chan_base[dma_chan_index]);

        desc_num = p_dma_chan->desc_num;
        p_dma_chan->direction = CTC_DMA_DIR_TX;
        p_dma_chan->p_desc = (sys_dma_desc_t*)g_dal_op.dma_alloc(lchip, (desc_num) * sizeof(sys_dma_desc_t), 0);
        if (NULL == p_dma_chan->p_desc)
        {
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_dma_chan->p_desc, 0, (desc_num) * sizeof(sys_dma_desc_t));

        p_dma_chan->mem_base = (uintptr)p_dma_chan->p_desc;

        p_dma_chan->desc_used.entry_num = desc_num;
        p_dma_chan->desc_used.p_array = (sys_dma_desc_used_info_t*)mem_malloc(MEM_DMA_MODULE, (desc_num) * sizeof(sys_dma_desc_used_info_t));
        if (NULL == p_dma_chan->desc_used.p_array)
        {
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_dma_chan->desc_used.p_array, 0, (desc_num) * sizeof(sys_dma_desc_used_info_t));

        p_dma_chan->p_desc_used = (uint8*)mem_malloc(MEM_DMA_MODULE, p_dma_chan->desc_num);
        if (NULL == p_dma_chan->p_desc_used)
        {
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_dma_chan->p_desc_used, 0, p_dma_chan->desc_num);

        /* alloc memory for every descriptor */
        for (desc_index = 0; desc_index < desc_num; desc_index++)
        {
            p_desc = &(p_dma_chan->p_desc[desc_index].pci_exp_desc);

            p_mem_addr = g_dal_op.dma_alloc(lchip, DMA_PKT_DEFAULT_SIZE, 0);
            if (NULL == p_mem_addr)
            {
                return CTC_E_NO_MEMORY;
            }

            phy_addr = g_dal_op.logic_to_phy(lchip, p_mem_addr);
            p_desc->desc_mem_addr_low = (uint32)(phy_addr >> 4);
            p_desc->desc_len = DMA_PKT_DEFAULT_SIZE;
        }


        /* RxMemBase */
        sal_memset(&desc_rx_mem_base, 0, sizeof(desc_rx_mem_base_t));
        phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_dma_chan->p_desc);
        desc_rx_mem_base.data = (phy_addr >> 4);
        cmd = DRV_IOW(DescRxMemBase_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &desc_rx_mem_base));

        /* RxValidNum must set to 0 during init */
        sal_memset(&desc_rx_vld_num, 0, sizeof(desc_tx_vld_num_t));
        desc_rx_vld_num.data = 0;
        cmd = DRV_IOW(DescRxVldNum_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &desc_rx_vld_num));

        /* RxMemDepth */
        sal_memset(&desc_rx_mem_depth, 0, sizeof(desc_rx_mem_depth));
        desc_rx_mem_depth.data = desc_num;
        cmd = DRV_IOW(DescRxMemDepth_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &desc_rx_mem_depth));

        /* enable dma packet tx function */
        cmd = DRV_IOR(DmaMiscCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_misc_ctl));
        dma_misc_ctl.cfg_dma_en = 1;
        dma_misc_ctl.dma_rx_chan_en |= (1 << p_dma_chan->channel_id);
        cmd = DRV_IOW(DmaMiscCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_misc_ctl));

        /* mask packet tx interrupt */
        cmd = DRV_IOR(PcieFuncIntr_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &dma_func));
        dma_func |= (1 << SYS_DMA_TX_CHANNEL);
        cmd = DRV_IOW(PcieFuncIntr_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &dma_func));
    }

    return CTC_E_NONE;
}

/**
@brief init dma packet RX(DMA--->CPU)
*/
int32
_sys_greatbelt_dma_packet_rx_init(uint8 lchip, sys_dma_chan_t* p_dma_chan_base)
{
    sys_dma_chan_t* p_dma_chan = NULL;
    uint8  dma_chan_index = 0;
    pci_exp_desc_mem_t* p_desc = NULL;
    sys_dma_desc_t* p_sys_desc_pad = NULL;
    uint16 desc_index = 0;
    uint16 desc_num = 0;
    uint32 phy_addr = 0;
    uint32 cmd = 0;
    uint8  cos = 0;
    uint8  cos_num = 0;
    uint8  cos_per_chan = 0;
    desc_tx_mem_base_t desc_tx_mem_base;
    desc_tx_mem_depth_t desc_tx_mem_depth;
    desc_tx_vld_num_t     desc_tx_vld_num;
    dma_chan_map_t   dma_chan_map;
    dma_misc_ctl_t dma_misc_ctl;
    void*  p_mem_addr = NULL;

    for (dma_chan_index = 0; dma_chan_index < p_gb_dma_master[lchip]->pkt_rx_chan_num; dma_chan_index++)
    {
        p_dma_chan = &(p_dma_chan_base[dma_chan_index]);

		if (p_dma_chan == NULL)
		{
			return CTC_E_DMA_EXCEED_MAX_DESC_NUM;
		}

        if (p_dma_chan->desc_num > SYS_DMA_MAX_DESC_NUM)
        {
            return CTC_E_DMA_EXCEED_MAX_DESC_NUM;
        }

        desc_num = p_dma_chan->desc_num;
        p_dma_chan->direction = CTC_DMA_DIR_RX;

        p_sys_desc_pad = (sys_dma_desc_t*)g_dal_op.dma_alloc(lchip, (desc_num) * sizeof(sys_dma_desc_t), 0);
        if (NULL == p_sys_desc_pad)
        {
            return CTC_E_NO_MEMORY;
        }

        p_dma_chan->p_desc = p_sys_desc_pad;
        p_dma_chan->mem_base = (uintptr)p_sys_desc_pad;

        sal_memset(p_sys_desc_pad, 0, (desc_num) * sizeof(sys_dma_desc_t));

        /* alloc memory for every descriptor */
        for (desc_index = 0; desc_index < desc_num; desc_index++)
        {
            p_desc = &(p_sys_desc_pad[desc_index].pci_exp_desc);

            p_mem_addr = g_dal_op.dma_alloc(lchip, p_gb_dma_master[lchip]->pkt_rx_size_per_desc, 0);
            if (NULL == p_mem_addr)
            {
                return CTC_E_NO_MEMORY;
            }

            phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_mem_addr);
            p_desc->desc_mem_addr_low = (phy_addr >> 4);
            p_desc->desc_len = p_gb_dma_master[lchip]->pkt_rx_size_per_desc;
        }

        p_dma_chan->current_index = 0;

        /* TxMemBase */
        sal_memset(&desc_tx_mem_base, 0, sizeof(desc_tx_mem_base));
        phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_sys_desc_pad);
        desc_tx_mem_base.data = (phy_addr >> 4);
        cmd = DRV_IOW(DescTxMemBase_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &desc_tx_mem_base));

        /* TxMemDepth */
        sal_memset(&desc_tx_mem_depth, 0, sizeof(desc_tx_mem_depth));
        desc_tx_mem_depth.data = desc_num;
        cmd = DRV_IOW(DescTxMemDepth_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &desc_tx_mem_depth));

        /* TxVldNum */
        sal_memset(&desc_tx_vld_num, 0, sizeof(desc_tx_vld_num));
        desc_tx_vld_num.data = desc_num;
        cmd = DRV_IOW(DescTxVldNum_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &desc_tx_vld_num));

        /* enable dma packet tx function */
        cmd = DRV_IOR(DmaMiscCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_misc_ctl));
        dma_misc_ctl.cfg_dma_en = 1;
        dma_misc_ctl.dma_tx_chan_en |= (1 << p_dma_chan->channel_id);
        cmd = DRV_IOW(DmaMiscCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_misc_ctl));
    }

    dma_chan_index = 0;
    if (p_gb_dma_master[lchip]->pkt_rx_chan_num)
    {
        /*config dma chan map,current only support one  rx dma  chanel */
        dma_chan_map.cos = p_dma_chan->channel_id;
        cmd = DRV_IOW(DmaChanMap_t, DRV_ENTRY_FLAG);
        cos_num = 64;
        cos_per_chan = cos_num / p_gb_dma_master[lchip]->pkt_rx_chan_num;
        for (cos = 0; cos < cos_num; cos++)
        {
            if (cos && ((cos % cos_per_chan) == 0))
            {
                if (dma_chan_index < p_gb_dma_master[lchip]->pkt_rx_chan_num - 1)
                {
                    dma_chan_index++;
                }
            }
            p_dma_chan = &(p_dma_chan_base[dma_chan_index]);
            dma_chan_map.cos = p_dma_chan->channel_id;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, cos, cmd, &dma_chan_map));
        }
    }

    return CTC_E_NONE;
}

/**
@brief init dma write table (CPU--->DMA)
*/
int32
_sys_greatbelt_dma_wr_tbl_init(uint8 lchip, sys_dma_chan_t* p_dma_chan)
{
    uint32 cmd = 0;
    uint32 phy_addr = 0;
    uint16 desc_num = 0;
    desc_rx_mem_base_t desc_tbl_mem_base;
    desc_rx_mem_depth_t desc_tbl_mem_depth;
    dma_misc_ctl_t dma_misc_ctl;
    dma_chan_cfg_t dma_chan_cfg;
    pci_exp_desc_mem_t* p_desc = NULL;
    uint16 tbl_buffer_len = 0;
    void*  p_mem_addr = NULL;
    desc_num = p_dma_chan->desc_num;
    p_dma_chan->direction = CTC_DMA_DIR_TX;

    /* alloc channel */
    cmd = DRV_IOR(DmaChanCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_chan_cfg));
    dma_chan_cfg.cfg_dma_tab_wr_chan = SYS_DMA_WTABLE_CHANNEL;
    cmd = DRV_IOW(DmaChanCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_chan_cfg));

    /* alloc table descriptor */
    p_dma_chan->p_desc = (sys_dma_desc_t*)g_dal_op.dma_alloc(lchip, sizeof(sys_dma_desc_t) * desc_num, 0);
    if (NULL == p_dma_chan->p_desc)
    {
        return CTC_E_NO_MEMORY;
    }

    p_dma_chan->mem_base = (uintptr)p_dma_chan->p_desc;

    sal_memset(p_dma_chan->p_desc, 0, sizeof(sys_dma_desc_t));

    /* malloc mem for data */
    tbl_buffer_len = _sys_greatbelt_dma_get_write_table_buf(lchip, NULL);

    p_desc = &(p_dma_chan->p_desc[0].pci_exp_desc);
    p_mem_addr = g_dal_op.dma_alloc(lchip, tbl_buffer_len, 0);
    if (NULL == p_mem_addr)
    {
        return CTC_E_NO_MEMORY;
    }
    phy_addr = g_dal_op.logic_to_phy(lchip, p_mem_addr);
    p_desc->desc_mem_addr_low = (uint32)(phy_addr >> 4);

    sal_memset(&desc_tbl_mem_base, 0, sizeof(desc_rx_mem_base_t));
    phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_dma_chan->p_desc);
    desc_tbl_mem_base.data = (phy_addr >> 4);

    cmd = DRV_IOW(DescRxMemBase_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &desc_tbl_mem_base));

    /* table desc deepth, max  num of table descriptors */
    sal_memset(&desc_tbl_mem_depth, 0, sizeof(desc_rx_mem_depth_t));
    desc_tbl_mem_depth.data = desc_num;
    cmd = DRV_IOW(DescRxMemDepth_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &desc_tbl_mem_depth));

    /* enable dma table function */
    cmd = DRV_IOR(DmaMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_misc_ctl));
    dma_misc_ctl.cfg_dma_en = 1;
    dma_misc_ctl.cfg_dma_tab_wr_en = 1;
    dma_misc_ctl.dma_rx_chan_en |= (1 << p_dma_chan->channel_id);
    cmd = DRV_IOW(DmaMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_misc_ctl));

    return CTC_E_NONE;
}

/**
@brief init dma read table (DMA--->CPU)
*/
int32
_sys_greatbelt_dma_rd_tbl_init(uint8 lchip, sys_dma_chan_t* p_dma_chan)
{
    uint32 cmd = 0;
    uint32 phy_addr = 0;
    uint16 desc_num = 0;
    desc_tx_mem_base_t desc_tbl_mem_base;
    desc_tx_mem_depth_t desc_tbl_mem_depth;
    dma_misc_ctl_t dma_misc_ctl;
    dma_chan_cfg_t dma_chan_cfg;

    desc_num = p_dma_chan->desc_num;
    p_dma_chan->direction = CTC_DMA_DIR_RX;

    /* alloc channel */
    cmd = DRV_IOR(DmaChanCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_chan_cfg));
    dma_chan_cfg.cfg_dma_tab_rd_chan = SYS_DMA_RTABLE_CHANNEL;
    cmd = DRV_IOW(DmaChanCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_chan_cfg));

    /* alloc table descriptor */
    p_dma_chan->p_desc = (sys_dma_desc_t*)g_dal_op.dma_alloc(lchip, sizeof(sys_dma_desc_t) * desc_num, 0);
    if (NULL == p_dma_chan->p_desc)
    {
        return CTC_E_NO_MEMORY;
    }

    p_dma_chan->mem_base = (uintptr)p_dma_chan->p_desc;

    /* alloc desc memory and config desc valid num should do during read table interface */
    sal_memset(p_dma_chan->p_desc, 0, sizeof(sys_dma_desc_t) * desc_num);
    sal_memset(&desc_tbl_mem_base, 0, sizeof(desc_tx_mem_base_t));
    phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_dma_chan->p_desc);
    desc_tbl_mem_base.data = (phy_addr >> 4);
    cmd = DRV_IOW(DescTxMemBase_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &desc_tbl_mem_base));

    /* table desc deepth, max  num of table descriptors */
    sal_memset(&desc_tbl_mem_depth, 0, sizeof(desc_tx_mem_depth_t));
    desc_tbl_mem_depth.data = desc_num;
    cmd = DRV_IOW(DescTxMemDepth_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &desc_tbl_mem_depth));

    /* enable dma table function */
    cmd = DRV_IOR(DmaMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_misc_ctl));
    dma_misc_ctl.cfg_dma_en = 1;
    dma_misc_ctl.cfg_dma_tab_rd_en = 1;
    dma_misc_ctl.dma_tx_chan_en |= (1 << p_dma_chan->channel_id);
    cmd = DRV_IOW(DmaMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_misc_ctl));

    /* config interrupt gen method */
    /* TODO */
    return CTC_E_NONE;
}

/**
@brief init dma stats (DMA--->CPU)
*/
int32
_sys_greatbelt_dma_stats_init(uint8 lchip, sys_dma_chan_t* p_dma_chan)
{
    uint32 cmd = 0;
    dma_misc_ctl_t dma_misc_ctl;
    dma_chan_cfg_t dma_chan_cfg;
    desc_tx_mem_base_t desc_tx_mem_base;
    desc_tx_mem_depth_t desc_tx_mem_depth;
    desc_tx_vld_num_t     desc_tx_vld_num;
    uint16 desc_num = 0;
    uint16 index  = 0;
    uint32 phy_addr = 0;
    void*  p_mem_addr = NULL;
    dma_stats_bmp_cfg_t dma_stats_bmp;
    dma_stats_interval_cfg_t dma_stats_intval;
    pci_exp_desc_mem_t* p_stats_pcie_desc = NULL;
    uint32 timer_l = 0;
    uint32 timer_h = 0;
    uint32 timer_full = 0;
    uint32 sup_clock = SYS_SUP_CLOCK;
    uint8 mac_id = 0;
    uint8 quad_idx = 0;

    /* alloc channel */
    cmd = DRV_IOR(DmaChanCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_chan_cfg));
    dma_chan_cfg.cfg_dma_stats_chan  = SYS_DMA_STATS_CHANNEL;
    cmd = DRV_IOW(DmaChanCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_chan_cfg));

    /* alloc desc memory */
    desc_num = p_dma_chan->desc_num;
    p_dma_chan->direction = CTC_DMA_DIR_RX;

    p_dma_chan->p_desc = (sys_dma_desc_t*)g_dal_op.dma_alloc(lchip, sizeof(sys_dma_desc_t) * desc_num, 0);
    if (NULL == p_dma_chan->p_desc)
    {
        return CTC_E_NO_MEMORY;
    }

    p_dma_chan->mem_base = (uintptr)p_dma_chan->p_desc;

    sal_memset(p_dma_chan->p_desc, 0, sizeof(sys_dma_desc_t) * desc_num);

    /* alloc memory for every desc */
    for (index = 0; index < desc_num; index++)
    {
        p_stats_pcie_desc = (pci_exp_desc_mem_t*)&(p_dma_chan->p_desc[index].pci_exp_desc);

        if (g_sys_stats_desc_len[index % SYS_DMA_STATS_COUNT])
        {
            p_mem_addr = g_dal_op.dma_alloc(lchip, g_sys_stats_desc_len[index % SYS_DMA_STATS_COUNT], 0);
            if (NULL == p_mem_addr)
            {
                return CTC_E_NO_MEMORY;
            }

            sal_memset(p_mem_addr, 0, g_sys_stats_desc_len[index % SYS_DMA_STATS_COUNT]);

            phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_mem_addr);
            p_stats_pcie_desc->desc_mem_addr_low = (phy_addr >> 4);
            p_stats_pcie_desc->desc_len = g_sys_stats_desc_len[index % SYS_DMA_STATS_COUNT];
        }
        else
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "DMA stats desc len is ZERO! \n");
            return CTC_E_DMA_DESC_INVALID;
        }
    }

    /* config TxMemBase */
    sal_memset(&desc_tx_mem_base, 0, sizeof(desc_tx_mem_base_t));
    phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_dma_chan->p_desc);
    desc_tx_mem_base.data = (phy_addr >> 4);
    cmd = DRV_IOW(DescTxMemBase_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &desc_tx_mem_base));

    /* config TxMemDepth */
    sal_memset(&desc_tx_mem_depth, 0, sizeof(desc_tx_mem_depth));
    desc_tx_mem_depth.data = desc_num;
    cmd = DRV_IOW(DescTxMemDepth_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &desc_tx_mem_depth));

    /* config TxVldNum */
    sal_memset(&desc_tx_vld_num, 0, sizeof(desc_tx_vld_num));
    desc_tx_vld_num.data = desc_num;
    cmd = DRV_IOW(DescTxVldNum_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &desc_tx_vld_num));

    /*config interval */
    sal_memset(&dma_stats_intval, 0, sizeof(dma_stats_interval_cfg_t));

    timer_full = 0xffffffff/(sup_clock*1000000);
    if (timer_full >= SYS_DMA_STATS_INTERVAL)
    {
        timer_l = SYS_DMA_STATS_INTERVAL*(sup_clock*1000000);
        timer_h = 0;
    }
    else
    {
        timer_l = 0xffffffff;
        timer_h = SYS_DMA_STATS_INTERVAL/timer_full;
    }

    dma_stats_intval.cfg_stats_interval_low = timer_l;
    dma_stats_intval.cfg_stats_interval_high= timer_h;

    cmd = DRV_IOW(DmaStatsIntervalCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_stats_intval));

    /* just quadmac stats and sgmac stats use dma process */
    sal_memset(&dma_stats_bmp, 0, sizeof(dma_stats_bmp_cfg_t));
    /*
    dma_stats_bmp.qmac_stats_en_bmp = 0xFFF;
    dma_stats_bmp.sgmac_stats_en_bmp = 0xFFF;
    */
    for (quad_idx = 0; quad_idx < 12; quad_idx++)
    {
        for (mac_id = 0; mac_id < 4; mac_id++)
        {
            if (TRUE == SYS_MAC_IS_VALID(lchip, (quad_idx*4 + mac_id)))
            {
                /* enable quad mac stats */
                CTC_BIT_SET(dma_stats_bmp.qmac_stats_en_bmp, quad_idx);
            }
        }
    }


    for (mac_id = 0; mac_id < SYS_MAX_SGMAC_PORT_NUM; mac_id++)
    {
        if (TRUE == SYS_MAC_IS_VALID(lchip, (SYS_MAX_GMAC_PORT_NUM + mac_id)))
        {
            /* enable quad mac stats */
            CTC_BIT_SET(dma_stats_bmp.sgmac_stats_en_bmp, mac_id);
        }
    }

    cmd = DRV_IOW(DmaStatsBmpCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_stats_bmp));

    /* enable dma stats */
    cmd = DRV_IOR(DmaMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_misc_ctl));
    dma_misc_ctl.cfg_dma_en = 1;
    dma_misc_ctl.cfg_stats_priority = DMA_TABLE_PRI_HIGH;
    dma_misc_ctl.cfg_dma_stats_en = 1;
    dma_misc_ctl.clear_on_read = 1;
    dma_misc_ctl.dma_tx_chan_en |= (1 << p_dma_chan->channel_id);
    cmd = DRV_IOW(DmaMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_misc_ctl));

    return CTC_E_NONE;
}

/**
@brief init dma learning (DMA--->CPU)
*/
int32
_sys_greatbelt_dma_learning_init(uint8 lchip, sys_dma_learning_t* p_learning_cfg)
{
    uint32 cmd = 0;
    uint32 phy_addr = 0;
    dma_learn_misc_ctl_t dma_learn_misc_ctl;
    dma_learn_mem_ctl_t dma_learn_mem_ctl;
    dma_misc_ctl_t dma_misc_ctl;
    dma_chan_cfg_t dma_chan_cfg;
    dma_gen_intr_ctl_t gen_intr;
    uint32 field_val = 0;
    ctc_chip_device_info_t device_info;

    /* alloc channel */
    cmd = DRV_IOR(DmaChanCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_chan_cfg));
    dma_chan_cfg.cfg_dma_learn_chan  = SYS_DMA_LEARN_CHANNEL;
    cmd = DRV_IOW(DmaChanCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_chan_cfg));

    cmd = DRV_IOR(DmaGenIntrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &gen_intr));
    gen_intr.cfg_dma_intr_desc_cnt1 = 3;
    gen_intr.cfg_dma_rx_intr_cnt_sel1 = 1;
    cmd = DRV_IOW(DmaGenIntrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &gen_intr));

    sys_greatbelt_chip_get_device_info(lchip, &device_info);
    if((1 == device_info.version_id) || ((device_info.version_id >= 2)&&(p_learning_cfg->hw_learning_sync_en)))
    {
        field_val = 1;
        cmd = DRV_IOW(FibLearnFifoCtl_t, FibLearnFifoCtl_LearnFifoPopMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        cmd = DRV_IOW(FibCreditCtl_t, FibCreditCtl_LearnInfoDmaCreditThrd_f);
        field_val = 8;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }
    else
    {

    }

    /* 1 - mem0, 2 - mem0, and mem1, 3 - mem0, mem1, and mem2, 4 - mem0, mem1, mem2, and mem3 */
    sal_memset(&dma_learn_misc_ctl, 0, sizeof(dma_learn_misc_ctl_t));
    if (p_learning_cfg->hw_learning_sync_en)
    {
        dma_learn_misc_ctl.cfg_dma_learn_mem_num = SYS_DMA_LEARNING_BLOCK_LIMIT;
        dma_learn_misc_ctl.cfg_dma_learn_entry   = SYS_DMA_LEARNING_THRESHOLD;
        dma_learn_misc_ctl.cfg_dma_learn_timer   = ((1<<31) | SYS_DMA_LEARNING_TIMER);
    }
    else
    {
        dma_learn_misc_ctl.cfg_dma_learn_mem_num = SYS_DMA_LEARNING_BLOCK_NUM;

        /* hardware learning do not using timer, memory full gen interrupt */
        dma_learn_misc_ctl.cfg_dma_learn_entry   = 1;
        dma_learn_misc_ctl.cfg_dma_learn_timer   = ((0<<31) | SYS_DMA_LEARNING_TIMER);
    }

    cmd = DRV_IOW(DmaLearnMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_learn_misc_ctl));

    sal_memset(&dma_learn_mem_ctl, 0, sizeof(dma_learn_mem_ctl_t));

    /* init mem0 mem1 mem2 mem3*/
    p_learning_cfg->p_mem0_base = g_dal_op.dma_alloc(lchip, p_learning_cfg->max_mem0_learning_num * sizeof(sys_dma_fib_t), 0);
    sal_memset(p_learning_cfg->p_mem0_base, 0, p_learning_cfg->max_mem0_learning_num * sizeof(sys_dma_fib_t));
    phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_learning_cfg->p_mem0_base);
    dma_learn_mem_ctl.learn_mem0_addr_low = (phy_addr >> 4);
    dma_learn_mem_ctl.learn_mem0_depth    = p_learning_cfg->max_mem0_learning_num;
    dma_learn_mem_ctl.learn_mem0_avail     = 1;

    if (p_learning_cfg->hw_learning_sync_en == 0)
    {
        p_learning_cfg->p_mem1_base = g_dal_op.dma_alloc(lchip, p_learning_cfg->max_mem1_learning_num * sizeof(sys_dma_fib_t), 0);
        sal_memset(p_learning_cfg->p_mem1_base, 0, p_learning_cfg->max_mem1_learning_num * sizeof(sys_dma_fib_t));
        phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_learning_cfg->p_mem1_base);
        dma_learn_mem_ctl.learn_mem1_addr_low = (phy_addr >> 4);
        dma_learn_mem_ctl.learn_mem1_depth    = p_learning_cfg->max_mem1_learning_num;
        dma_learn_mem_ctl.learn_mem1_avail     = 1;


        p_learning_cfg->p_mem2_base = g_dal_op.dma_alloc(lchip, p_learning_cfg->max_mem2_learning_num * sizeof(sys_dma_fib_t), 0);
        sal_memset(p_learning_cfg->p_mem2_base, 0, p_learning_cfg->max_mem2_learning_num * sizeof(sys_dma_fib_t));
        phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_learning_cfg->p_mem2_base);
        dma_learn_mem_ctl.learn_mem2_addr_low = (phy_addr >> 4);
        dma_learn_mem_ctl.learn_mem2_depth    = p_learning_cfg->max_mem2_learning_num;
        dma_learn_mem_ctl.learn_mem2_avail     = 1;

        p_learning_cfg->p_mem3_base = g_dal_op.dma_alloc(lchip, p_learning_cfg->max_mem3_learning_num * sizeof(sys_dma_fib_t), 0);
        sal_memset(p_learning_cfg->p_mem3_base, 0, p_learning_cfg->max_mem3_learning_num * sizeof(sys_dma_fib_t));
        phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_learning_cfg->p_mem3_base);
        dma_learn_mem_ctl.learn_mem3_addr_low = (phy_addr >> 4);
        dma_learn_mem_ctl.learn_mem3_depth    = p_learning_cfg->max_mem3_learning_num;
        dma_learn_mem_ctl.learn_mem3_avail     = 1;
    }

    cmd = DRV_IOW(DmaLearnMemCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_learn_mem_ctl));

    /* enable dma learning function */
    cmd = DRV_IOR(DmaMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_misc_ctl));
    dma_misc_ctl.cfg_dma_en = 1;
    dma_misc_ctl.cfg_dma_learn_en = 1;
    dma_misc_ctl.dma_tx_chan_en |= (1 << p_learning_cfg->channel_id);
    cmd = DRV_IOW(DmaMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_misc_ctl));
    return CTC_E_NONE;
}

/**
 @brief free memory resource when dma init failed
*/
void
_sys_greatbelt_dma_reclaim_resource(uint8 lchip)
{
    sys_dma_chan_t* p_dma_chan = NULL;
    ctc_dma_func_type_t dma_func = 0;
    uint32  desc_index = 0;
    uint8  dma_chan_index = 0;
    void* logic_addr = 0;
    uint32 low_phy_addr = 0;
    uint64 phy_addr = 0;

    /*1. free HW learning dma resource */
    if (NULL != p_gb_dma_master[lchip]->sys_learning_dma.p_mem0_base)
    {
        g_dal_op.dma_free(lchip, p_gb_dma_master[lchip]->sys_learning_dma.p_mem0_base);
    }

    if (NULL != p_gb_dma_master[lchip]->sys_learning_dma.p_mem1_base)
    {
        g_dal_op.dma_free(lchip, p_gb_dma_master[lchip]->sys_learning_dma.p_mem0_base);
    }

    if (NULL != p_gb_dma_master[lchip]->sys_learning_dma.p_mem2_base)
    {
        g_dal_op.dma_free(lchip, p_gb_dma_master[lchip]->sys_learning_dma.p_mem0_base);
    }

    if (NULL != p_gb_dma_master[lchip]->sys_learning_dma.p_mem3_base)
    {
        g_dal_op.dma_free(lchip, p_gb_dma_master[lchip]->sys_learning_dma.p_mem0_base);
    }

    p_dma_chan = &p_gb_dma_master[lchip]->sys_oam_bfd_dma;
    if (NULL != p_dma_chan->p_desc)
    {
        for (desc_index = 0; desc_index < p_dma_chan->desc_num; desc_index++)
        {
            low_phy_addr = p_dma_chan->p_desc[desc_index].pci_exp_desc.desc_mem_addr_low<<4;
            COMBINE_64BITS_DATA(p_gb_dma_master[lchip]->dma_high_addr, low_phy_addr, phy_addr);
            logic_addr = (void*)g_dal_op.phy_to_logic(lchip, phy_addr);
            if (NULL != logic_addr)
            {
                g_dal_op.dma_free(lchip, (void*)logic_addr);
            }
        }

        g_dal_op.dma_free(lchip, p_dma_chan->p_desc);
        p_dma_chan->p_desc = NULL;
    }
    p_dma_chan = p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_PACKET_RX];
    /*2. free packet rx DMA resource */
    for (dma_chan_index = 0; dma_chan_index < p_gb_dma_master[lchip]->pkt_rx_chan_num; dma_chan_index++)
    {
        if (NULL != p_dma_chan[dma_chan_index].p_desc)
        {
            for (desc_index = 0; desc_index < p_dma_chan[dma_chan_index].desc_num; desc_index++)
            {
                low_phy_addr = p_dma_chan[dma_chan_index].p_desc[desc_index].pci_exp_desc.desc_mem_addr_low<<4;
                COMBINE_64BITS_DATA(p_gb_dma_master[lchip]->dma_high_addr, low_phy_addr, phy_addr);
                logic_addr = (void*)g_dal_op.phy_to_logic(lchip, phy_addr);
                if (NULL != logic_addr)
                {
                    g_dal_op.dma_free(lchip, (void*)logic_addr);
                }
            }
            g_dal_op.dma_free(lchip, p_dma_chan[dma_chan_index].p_desc);
            p_dma_chan[dma_chan_index].p_desc = NULL;
        }
    }

    p_dma_chan = p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_PACKET_TX];
    for (dma_chan_index = 0; dma_chan_index < SYS_DMA_PKT_TX_CHAN_NUM; dma_chan_index++)
    {
        mem_free(p_dma_chan[dma_chan_index].desc_used.p_array);
        mem_free(p_dma_chan[dma_chan_index].p_desc_used);
        if (NULL != p_dma_chan[dma_chan_index].p_desc)
        {
            for (desc_index = 0; desc_index < p_dma_chan[dma_chan_index].desc_num; desc_index++)
            {
                low_phy_addr = p_dma_chan[dma_chan_index].p_desc[desc_index].pci_exp_desc.desc_mem_addr_low<<4;
                COMBINE_64BITS_DATA(p_gb_dma_master[lchip]->dma_high_addr, low_phy_addr, phy_addr);
                logic_addr = (void*)g_dal_op.phy_to_logic(lchip, phy_addr);
                if (NULL != logic_addr)
                {
                    g_dal_op.dma_free(lchip, (void*)logic_addr);
                }
            }
            g_dal_op.dma_free(lchip, p_dma_chan[dma_chan_index].p_desc);
            p_dma_chan[dma_chan_index].p_desc = NULL;
        }
    }

    /*3. free Other DMA resource */
    for (dma_func = CTC_DMA_FUNC_TABLE_R; dma_func < CTC_DMA_FUNC_HW_LEARNING; dma_func++)
    {
        if (NULL != p_gb_dma_master[lchip]->p_dma_chan[dma_func]->p_desc)
        {
            if (CTC_DMA_FUNC_STATS == dma_func)
            {
                for (desc_index = 0; desc_index < p_gb_dma_master[lchip]->p_dma_chan[dma_func]->desc_num; desc_index++)
                {
                    low_phy_addr = p_gb_dma_master[lchip]->p_dma_chan[dma_func]->p_desc[desc_index].pci_exp_desc.desc_mem_addr_low<<4;
                    COMBINE_64BITS_DATA(p_gb_dma_master[lchip]->dma_high_addr, low_phy_addr, phy_addr);
                    logic_addr = (void*)g_dal_op.phy_to_logic(lchip, phy_addr);
                    if (NULL != logic_addr)
                    {
                        g_dal_op.dma_free(lchip, (void*)logic_addr);
                    }
                }
            }
            g_dal_op.dma_free(lchip, p_gb_dma_master[lchip]->p_dma_chan[dma_func]->p_desc);
            p_gb_dma_master[lchip]->p_dma_chan[dma_func]->p_desc = NULL;
        }
    }

    return;
}

/**
 @brief init dma module and allocate necessary memory
*/
int32
sys_greatbelt_dma_init(uint8 lchip, ctc_dma_global_cfg_t* dma_global_cfg)
{
    int32  ret = CTC_E_NONE;
    uint8  index = 0;
    uint16 init_cnt = 0;
    uint32 cmd = 0;
    uint32 init_done = FALSE;
    uint32 field_value = 0;
    dma_init_ctl_t dma_init_ctl;
    sys_mem_base_tab_t sys_mem_base_tab;
    dma_pkt_misc_ctl_t dma_pkt_misc_ctl;
    pcie_intr_cfg_t pcie_intr_cfg;
    drv_work_platform_type_t platform_type;
    sys_dma_chan_t* p_dma_chan = NULL;
    uint16 learning_entry_num = 0;
    dma_endian_cfg_t dma_endian;
    host_type_t byte_order;
    dal_dma_info_t dma_info;
    ctc_chip_device_info_t device_info;

    CTC_PTR_VALID_CHECK(dma_global_cfg);

    if (NULL != p_gb_dma_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (dma_global_cfg->pkt_rx_chan_num > SYS_DMA_RX_CHAN_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (dma_global_cfg->pkt_rx_size_per_desc % 256)
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((g_dal_op.dma_alloc == NULL) || (g_dal_op.dma_free == NULL)
        || (g_dal_op.logic_to_phy == NULL) || (g_dal_op.phy_to_logic == NULL))
    {
        return CTC_E_INVALID_PTR;
    }

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%s()\n", __FUNCTION__);

    p_gb_dma_master[lchip] = (sys_dma_master_t*)mem_malloc(MEM_DMA_MODULE, sizeof(sys_dma_master_t));
    if (NULL == p_gb_dma_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_dma_master[lchip], 0, sizeof(sys_dma_master_t));

    p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_PACKET_RX]    = &(p_gb_dma_master[lchip]->sys_packet_rx_dma[0]);
    p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_PACKET_TX]    = &(p_gb_dma_master[lchip]->sys_packet_tx_dma[0]);
    p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_TABLE_R]      = &(p_gb_dma_master[lchip]->sys_table_rd_dma);
    p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_TABLE_W]      = &(p_gb_dma_master[lchip]->sys_table_wr_dma);
    p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_STATS]        = &(p_gb_dma_master[lchip]->sys_stats_dma);
    p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_HW_LEARNING]  = NULL;


    p_gb_dma_master[lchip]->dma_en_flag = dma_global_cfg->func_en_bitmap;
    p_gb_dma_master[lchip]->pkt_rx_size_per_desc  = dma_global_cfg->pkt_rx_size_per_desc;
    p_gb_dma_master[lchip]->pkt_rx_chan_num  = dma_global_cfg->pkt_rx_chan_num;

    for (index = 0; index < p_gb_dma_master[lchip]->pkt_rx_chan_num; index++)
    {
        p_dma_chan = &(p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_PACKET_RX][index]);
        p_dma_chan->channel_id = index + SYS_DMA_RX_MIN_CHANNEL;
        p_dma_chan->desc_num = dma_global_cfg->pkt_rx[index].desc_num;
        p_dma_chan->desc_depth = dma_global_cfg->pkt_rx[index].desc_num;
        p_gb_dma_master[lchip]->packet_rx_priority[index] = dma_global_cfg->pkt_rx[index].priority;
    }
    /* init BFD task priority */
    p_gb_dma_master[lchip]->packet_rx_priority[SYS_DMA_RX_CHAN_NUM] = SAL_TASK_PRIO_DEF;

    for (index = 0; index < SYS_DMA_PKT_TX_CHAN_NUM; index++)
    {
        p_dma_chan = &(p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_PACKET_TX][index]);
        p_dma_chan->channel_id = SYS_DMA_TX_CHANNEL+index;
        p_dma_chan->desc_num = dma_global_cfg->pkt_tx_desc_num;
        p_dma_chan->desc_depth = dma_global_cfg->pkt_tx_desc_num;
        ret = sal_mutex_create(&(p_dma_chan->p_mutex));
        if (ret || !(p_dma_chan->p_mutex))
        {
            return CTC_E_NO_RESOURCE;
        }
    }

    p_dma_chan = p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_TABLE_R];
    p_dma_chan->channel_id = SYS_DMA_RTABLE_CHANNEL;
    p_dma_chan->desc_num = dma_global_cfg->table_r_desc_num;
    p_dma_chan->desc_depth = dma_global_cfg->table_r_desc_num;

    p_dma_chan = p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_TABLE_W];
    p_dma_chan->channel_id = SYS_DMA_WTABLE_CHANNEL;
    p_dma_chan->desc_num = dma_global_cfg->table_w_desc_num;
    p_dma_chan->desc_depth = dma_global_cfg->table_w_desc_num;

    p_dma_chan = p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_STATS];
    p_dma_chan->channel_id = SYS_DMA_STATS_CHANNEL;
    p_dma_chan->desc_num = dma_global_cfg->stats.desc_num;
    p_dma_chan->desc_depth = dma_global_cfg->stats.desc_num;
    p_gb_dma_master[lchip]->stats_priority = dma_global_cfg->stats.priority;

    if (dma_global_cfg->hw_learning_sync_en)
    {
        /* only support 1 block, 16 entry */
        p_gb_dma_master[lchip]->sys_learning_dma.max_mem0_learning_num = SYS_DMA_LEARNING_DEPTH_LIMIT;
        p_gb_dma_master[lchip]->sys_learning_dma.learning_proc_func = dma_global_cfg->learning_proc_func;
        p_gb_dma_master[lchip]->sys_learning_dma.fdb_cb_func = sys_greatbelt_l2_sync_hw_learn_aging_info;

    }
    else
    {
        learning_entry_num = dma_global_cfg->learning.desc_num;
        if (dma_global_cfg->learning.desc_num < 1000)
        {
            learning_entry_num = 1000;
        }

        p_gb_dma_master[lchip]->sys_learning_dma.max_mem0_learning_num = learning_entry_num / 4;
        p_gb_dma_master[lchip]->sys_learning_dma.max_mem1_learning_num = learning_entry_num / 4;
        p_gb_dma_master[lchip]->sys_learning_dma.max_mem2_learning_num = learning_entry_num / 4;
        p_gb_dma_master[lchip]->sys_learning_dma.max_mem3_learning_num = learning_entry_num / 4;
    }

    p_gb_dma_master[lchip]->sys_learning_dma.channel_id = SYS_DMA_LEARN_CHANNEL;

    p_gb_dma_master[lchip]->learning_priority = dma_global_cfg->learning.priority;
    p_gb_dma_master[lchip]->sys_learning_dma.hw_learning_sync_en = dma_global_cfg->hw_learning_sync_en;

    /* special for oam bfd function */
    p_gb_dma_master[lchip]->sys_oam_bfd_dma.channel_id = SYS_DMA_OAM_BFD_CHANNEL;
    p_gb_dma_master[lchip]->sys_oam_bfd_dma.current_index = 0;
    p_gb_dma_master[lchip]->sys_oam_bfd_dma.desc_depth = SYS_DMA_BFD_MAX_DESC_NUM;
    p_gb_dma_master[lchip]->sys_oam_bfd_dma.desc_num = SYS_DMA_BFD_MAX_DESC_NUM;

    /***************** init DMA public tables *****************/
    sal_memset(&dma_init_ctl, 0, sizeof(dma_init_ctl_t));


    cmd = DRV_IOW(DmaInitCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &dma_init_ctl), ret, error);
    dma_init_ctl.init = 1;
    dma_init_ctl.init_dma_en = 1;
    cmd = DRV_IOW(DmaInitCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &dma_init_ctl), ret, error);

    ret = drv_greatbelt_get_platform_type(&platform_type);

    if (platform_type == HW_PLATFORM)
    {
        /* wait for init */
        while (init_cnt < SYS_DMA_INIT_COUNT)
        {
            cmd = DRV_IOR(DmaInitCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &dma_init_ctl), ret, error);

            /* init failed */
            if (dma_init_ctl.init_done && dma_init_ctl.init_dma_done)
            {
                init_done = TRUE;
                break;
            }

            init_cnt++;
        }

        if (init_done == FALSE)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "DMA init falied ,%d @ %d\n", dma_init_ctl.init_done, dma_init_ctl.init_dma_done);
            return CTC_E_NOT_INIT;
        }
    }


    sal_memset(&sys_mem_base_tab, 0, sizeof(sys_mem_base_tab_t));
    sal_memset(&dma_info, 0, sizeof(dma_info));
    dal_get_dma_info(lchip, &dma_info);
     /*-GET_HIGH_32BITS(dma_info.phy_base, p_gb_dma_master[lchip]->dma_high_addr);*/
    p_gb_dma_master[lchip]->dma_high_addr = dma_info.phy_base_hi;

    /* high 32 bit for 64 bit system memory */
    sys_mem_base_tab.data = p_gb_dma_master[lchip]->dma_high_addr;
    cmd = DRV_IOW(SysMemBaseTab_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &sys_mem_base_tab), ret, error);


    sal_memset(&dma_pkt_misc_ctl, 0, sizeof(dma_pkt_misc_ctl));
    dma_pkt_misc_ctl.cfg_pkt_truncat_en = FALSE;
    dma_pkt_misc_ctl.cfg_pkt_tx_crc_chk_en = TRUE;
    dma_pkt_misc_ctl.cfg_pad_tx_crc_en = FALSE;
    dma_pkt_misc_ctl.cfg_pad_rx_crc_en = TRUE;

    cmd = DRV_IOW(DmaPktMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &dma_pkt_misc_ctl), ret, error);


    byte_order = drv_greatbelt_get_host_type();
    p_gb_dma_master[lchip]->byte_order = ((byte_order==HOST_LE)&&(platform_type == HW_PLATFORM))?1:0;
    dma_endian.cfg_dma_endian = (byte_order==HOST_LE)?1:0;
    cmd = DRV_IOW(DmaEndianCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &dma_endian), ret, error);


    /* for Packet RX /TX channel use EOP to trigger interrupt */
    sal_memset(&pcie_intr_cfg, 0, sizeof(pcie_intr_cfg));
    pcie_intr_cfg.cfg_pkt_eop_intr_en |= (1 << SYS_DMA_TX_CHANNEL);

    for (index = 0; index < SYS_DMA_RX_CHAN_NUM; index++)
    {
        pcie_intr_cfg.cfg_pkt_eop_intr_en |= (1 << (SYS_DMA_RX_MIN_CHANNEL + index));
    }

    cmd = DRV_IOW(PcieIntrCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &pcie_intr_cfg), ret, error);

    sys_greatbelt_chip_get_device_info(lchip, &device_info);
    if (device_info.version_id > 2)
    {
        field_value = 1;
        cmd = DRV_IOW(DmaGenIntrCtl_t, DmaGenIntrCtl_CfgDmaIntrDescCnt6_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        cmd = DRV_IOW(DmaGenIntrCtl_t, DmaGenIntrCtl_CfgDmaIntrDescCnt7_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    if (CTC_IS_BIT_SET(dma_global_cfg->func_en_bitmap, CTC_DMA_FUNC_PACKET_RX))
    {
        CTC_ERROR_GOTO(_sys_greatbelt_dma_packet_rx_init(lchip, p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_PACKET_RX]), ret, error);

        CTC_ERROR_GOTO(_sys_greatbelt_dma_sync_init(lchip, CTC_DMA_FUNC_PACKET_RX), ret, error);
    }

    if (CTC_IS_BIT_SET(dma_global_cfg->func_en_bitmap, CTC_DMA_FUNC_PACKET_TX))
    {
        CTC_ERROR_GOTO(_sys_greatbelt_dma_packet_tx_init(lchip, p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_PACKET_TX]), ret, error);
        CTC_ERROR_GOTO(_sys_greatbelt_dma_sync_init(lchip, CTC_DMA_FUNC_PACKET_TX), ret, error);
    }

    if (CTC_IS_BIT_SET(dma_global_cfg->func_en_bitmap, CTC_DMA_FUNC_TABLE_R))
    {
        CTC_ERROR_GOTO(_sys_greatbelt_dma_rd_tbl_init(lchip, p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_TABLE_R]), ret, error);
    }

    if (CTC_IS_BIT_SET(dma_global_cfg->func_en_bitmap, CTC_DMA_FUNC_TABLE_W))
    {
        CTC_ERROR_GOTO(_sys_greatbelt_dma_wr_tbl_init(lchip, p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_TABLE_W]), ret, error);
    }

    if (CTC_IS_BIT_SET(dma_global_cfg->func_en_bitmap, CTC_DMA_FUNC_STATS))
    {
        CTC_ERROR_GOTO(_sys_greatbelt_dma_stats_init(lchip, p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_STATS]), ret, error);
        CTC_ERROR_GOTO(_sys_greatbelt_dma_sync_init(lchip, CTC_DMA_FUNC_STATS), ret, error);
    }

    if (CTC_IS_BIT_SET(dma_global_cfg->func_en_bitmap, CTC_DMA_FUNC_HW_LEARNING))
    {
        CTC_ERROR_GOTO(_sys_greatbelt_dma_learning_init(lchip, &(p_gb_dma_master[lchip]->sys_learning_dma)), ret, error);
        CTC_ERROR_GOTO(_sys_greatbelt_dma_sync_init(lchip, CTC_DMA_FUNC_HW_LEARNING), ret, error);
    }

    /* special init for oam bfd function */
    CTC_ERROR_GOTO(_sys_greatbelt_dma_oam_bfd_init(lchip, &(p_gb_dma_master[lchip]->sys_oam_bfd_dma)), ret, error);

    /* check device */
    CTC_ERROR_RETURN(sys_greatbelt_chip_device_check(lchip));

    /* disable some features according different device */
    CTC_ERROR_RETURN(sys_greatbelt_device_feature_init(lchip));



    return CTC_E_NONE;

error:

    /* reclaim resource when fail */
    _sys_greatbelt_dma_reclaim_resource(lchip);
    if (NULL != p_gb_dma_master[lchip])
    {
        mem_free(p_gb_dma_master[lchip]);
    }
    /* deinit DMA module */
    sal_memset(&dma_init_ctl, 0, sizeof(dma_init_ctl_t));
    dma_init_ctl.init = 0;
    dma_init_ctl.init_dma_en = 0;
    cmd = DRV_IOW(DmaInitCtl_t, DRV_ENTRY_FLAG);

    DRV_IOCTL(lchip, 0, cmd, &dma_init_ctl);


    return CTC_E_NOT_INIT;
}

int32
sys_greatbelt_dma_deinit(uint8 lchip)
{
    uint8 index = 0;
    uint32 cmd = 0;
    dma_init_ctl_t dma_init_ctl;

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    LCHIP_CHECK(lchip);
    if (NULL == p_gb_dma_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (p_gb_dma_master[lchip]->dma_learning_thread)
    {
        sal_sem_give(p_gb_dma_master[lchip]->learning_sync_sem);
        sal_task_destroy(p_gb_dma_master[lchip]->dma_learning_thread);
        sal_sem_destroy(p_gb_dma_master[lchip]->learning_sync_sem);
    }

    for (index=0; index< SYS_DMA_RX_CHAN_NUM+1; index++)
    {
        if (p_gb_dma_master[lchip]->dma_packet_rx_thread[index])
        {
            if (p_gb_dma_master[lchip]->dma_packet_rx_thread[index])
            {
                sal_sem_give(p_gb_dma_master[lchip]->packet_rx_sync_sem[index]);
                sal_task_destroy(p_gb_dma_master[lchip]->dma_packet_rx_thread[index]);
                sal_sem_destroy(p_gb_dma_master[lchip]->packet_rx_sync_sem[index]);
            }
        }
    }

    if (p_gb_dma_master[lchip]->dma_stats_thread)
    {
        sal_sem_give(p_gb_dma_master[lchip]->stats_sync_sem);
        sal_task_destroy(p_gb_dma_master[lchip]->dma_stats_thread);
        sal_sem_destroy(p_gb_dma_master[lchip]->stats_sync_sem);
    }

    _sys_greatbelt_dma_reclaim_resource(lchip);

    /* deinit DMA module */
    sal_memset(&dma_init_ctl, 0, sizeof(dma_init_ctl_t));
    dma_init_ctl.init = 0;
    dma_init_ctl.init_dma_en = 0;
    cmd = DRV_IOW(DmaInitCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &dma_init_ctl);

    mem_free(p_gb_dma_master[lchip]);

    return CTC_E_NONE;
}

