/**
 @file ctc_goldengate_interrupt.c

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
#include "ctc_port.h"
#include "ctc_goldengate_interrupt.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_packet.h"
#include "sys_goldengate_dma.h"
#include "sys_goldengate_dma_priv.h"
#include "sys_goldengate_interrupt.h"
#include "sys_goldengate_queue_enq.h"

#include "ctc_warmboot.h"
#include "goldengate/include/drv_lib.h"
#include "goldengate/include/drv_chip_agent.h"
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
sys_dma_master_t* p_gg_dma_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
extern dal_op_t g_dal_op;
extern uint8 drv_goldengate_get_host_type(void);

#define SYS_GOLDENGATE_DMA_STATS_WORD 4
#define SYS_GOLDENGATE_STATS_SIZE 0x240
/*all xqmacram plus cgmacram(96+4) for dma sync*/
#define SYS_GOLDENGATE_DMA_MAC_STATS_NUM  100


/* temply only process slice0 mac */
#define SYS_GOLDENGATE_GET_MAC_ADDR(id, type, addr)\
{\
    if (type == 2)\
    {\
        if (id < 40)\
        {\
            addr = (0x20010000+(id/16)*0x10000+((id%16)/4)*0x2000+0x240*(id%4));\
        }\
        else\
        {\
            addr = (0x20040000+((id-40)/4)*0x2000+0x240*((id-40)%4));\
        }\
    }\
    else \
    {\
        addr = (0x20010000+(id/4)*0x2000);\
    }\
}

#define __STUB__

/****************************************************************************
*
* Function
*
*****************************************************************************/
STATIC int32
_sys_goldengate_dma_get_mac_address(uint8 lchip, uint16 mac_id, uint8 type, uint32* p_addr)
{
    uint32 start_addr = 0;
    uint32 tbl_id = 0;
    uint32 index = 0;
    uint8 slice_id = 0;
    uint8 slice_mac = 0;

    slice_id = (mac_id >= 64)?1:0;
    slice_mac = mac_id - 64*slice_id;

    if (slice_mac >= 48)
    {
        slice_mac = slice_mac - 8;
    }

    switch(type)
    {
        case CTC_PORT_SPEED_1G:
        case CTC_PORT_SPEED_10G:
        case CTC_PORT_SPEED_2G5:
            tbl_id = XqmacStatsRam0_t + (slice_mac + slice_id * 48)/4;
            index = (slice_mac%4)*36;
            CTC_ERROR_RETURN(drv_goldengate_table_get_hw_addr(tbl_id, index, &start_addr, FALSE));

            *p_addr = start_addr;
            break;

        case CTC_PORT_SPEED_20G:
        case CTC_PORT_SPEED_40G:
            if (slice_mac%4)
            {
                return CTC_E_INVALID_PARAM;
            }

            tbl_id = XqmacStatsRam0_t + (slice_mac + slice_id * 48)/4;
            index = 0;
            CTC_ERROR_RETURN(drv_goldengate_table_get_hw_addr(tbl_id, index, &start_addr, FALSE));
            *p_addr = start_addr;
            break;

        case CTC_PORT_SPEED_100G:
            if (slice_mac == 36)
            {
                tbl_id = CgmacStatsRam0_t + 2*slice_id;
            }
            else if (slice_mac == 44)
            {
                tbl_id = CgmacStatsRam1_t + 2*slice_id;
            }

            index = 0;
            CTC_ERROR_RETURN(drv_goldengate_table_get_hw_addr(tbl_id, index, &start_addr, FALSE));
            *p_addr = start_addr;

            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}
int32
sys_goldengate_dma_sync_pkt_rx_stats(uint8 lchip, uint8 dmasel)
{
     uint32 cmd = 0;
     uint32 tbl_id = 0;
     DmaPktRxStats0_m p_stats;

     sal_memset(&p_stats, 0, sizeof(DmaPktRxStats0_m));
     tbl_id = DmaPktRxStats0_t + dmasel;
     cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
     CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &p_stats));
     p_gg_dma_master[lchip]->dma_stats.rx0_pkt_cnt      += GetDmaPktRxStats0(V, dmaPktRx0FrameCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx0_byte_cnt     += GetDmaPktRxStats0(V, dmaPktRx0ByteCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx0_drop_cnt     += GetDmaPktRxStats0(V, dmaPktRx0DropCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx0_error_cnt    += GetDmaPktRxStats0(V, dmaPktRx0ErrorCnt_f, &p_stats);

     p_gg_dma_master[lchip]->dma_stats.rx1_pkt_cnt      += GetDmaPktRxStats0(V, dmaPktRx1FrameCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx1_byte_cnt     += GetDmaPktRxStats0(V, dmaPktRx1ByteCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx1_drop_cnt     += GetDmaPktRxStats0(V, dmaPktRx1DropCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx1_error_cnt    += GetDmaPktRxStats0(V, dmaPktRx1ErrorCnt_f, &p_stats);

     p_gg_dma_master[lchip]->dma_stats.rx2_pkt_cnt      += GetDmaPktRxStats0(V, dmaPktRx2FrameCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx2_byte_cnt     += GetDmaPktRxStats0(V, dmaPktRx2ByteCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx2_drop_cnt     += GetDmaPktRxStats0(V, dmaPktRx2DropCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx2_error_cnt    += GetDmaPktRxStats0(V, dmaPktRx2ErrorCnt_f, &p_stats);

     p_gg_dma_master[lchip]->dma_stats.rx3_pkt_cnt      += GetDmaPktRxStats0(V, dmaPktRx3FrameCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx3_byte_cnt     += GetDmaPktRxStats0(V, dmaPktRx3ByteCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx3_drop_cnt     += GetDmaPktRxStats0(V, dmaPktRx3DropCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx3_error_cnt    += GetDmaPktRxStats0(V, dmaPktRx3ErrorCnt_f, &p_stats);

    return CTC_E_NONE;
}

int32
sys_goldengate_dma_sync_pkt_tx_stats(uint8 lchip, uint8 dmasel)
{
     uint32 cmd = 0;
     uint32 tbl_id = 0;
     DmaPktTxStats0_m p_stats;

     sal_memset(&p_stats, 0, sizeof(DmaPktTxStats0_m));
     tbl_id = DmaPktTxStats0_t + dmasel;
     cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
     CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &p_stats));
     p_gg_dma_master[lchip]->dma_stats.tx0_bad_byte   += GetDmaPktTxStats0(V, dmaPktTx0BadByteCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.tx0_bad_cnt    += GetDmaPktTxStats0(V, dmaPktTx0BadFrameCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.tx0_good_byte  += GetDmaPktTxStats0(V, dmaPktTx0GoodByteCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.tx0_good_cnt   += GetDmaPktTxStats0(V, dmaPktTx0GoodFrameCnt_f, &p_stats);

     p_gg_dma_master[lchip]->dma_stats.tx1_bad_byte   += GetDmaPktTxStats0(V, dmaPktTx1BadByteCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.tx1_bad_cnt    += GetDmaPktTxStats0(V, dmaPktTx1BadFrameCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.tx1_good_byte  += GetDmaPktTxStats0(V, dmaPktTx1GoodByteCnt_f, &p_stats);
     p_gg_dma_master[lchip]->dma_stats.tx1_good_cnt   += GetDmaPktTxStats0(V, dmaPktTx1GoodFrameCnt_f, &p_stats);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_dma_sync_pkt_stats(uint8 lchip, void* p_addr)
{
    DmaPktRxStats0_m* p_stats = (DmaPktRxStats0_m*)p_addr;

     p_gg_dma_master[lchip]->dma_stats.rx0_pkt_cnt += GetDmaPktRxStats0(V, dmaPktRx0FrameCnt_f, p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx0_byte_cnt += GetDmaPktRxStats0(V, dmaPktRx0ByteCnt_f, p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx0_drop_cnt += GetDmaPktRxStats0(V, dmaPktRx0DropCnt_f, p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx0_error_cnt += GetDmaPktRxStats0(V, dmaPktRx0ErrorCnt_f, p_stats);

     p_gg_dma_master[lchip]->dma_stats.rx1_pkt_cnt += GetDmaPktRxStats0(V, dmaPktRx1FrameCnt_f, p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx1_byte_cnt += GetDmaPktRxStats0(V, dmaPktRx1ByteCnt_f, p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx1_drop_cnt += GetDmaPktRxStats0(V, dmaPktRx1DropCnt_f, p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx1_error_cnt += GetDmaPktRxStats0(V, dmaPktRx1ErrorCnt_f, p_stats);

     p_gg_dma_master[lchip]->dma_stats.rx2_pkt_cnt += GetDmaPktRxStats0(V, dmaPktRx2FrameCnt_f, p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx2_byte_cnt += GetDmaPktRxStats0(V, dmaPktRx2ByteCnt_f, p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx2_drop_cnt += GetDmaPktRxStats0(V, dmaPktRx2DropCnt_f, p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx2_error_cnt += GetDmaPktRxStats0(V, dmaPktRx2ErrorCnt_f, p_stats);

      p_gg_dma_master[lchip]->dma_stats.rx3_pkt_cnt += GetDmaPktRxStats0(V, dmaPktRx3FrameCnt_f, p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx3_byte_cnt += GetDmaPktRxStats0(V, dmaPktRx3ByteCnt_f, p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx3_drop_cnt += GetDmaPktRxStats0(V, dmaPktRx3DropCnt_f, p_stats);
     p_gg_dma_master[lchip]->dma_stats.rx3_error_cnt += GetDmaPktRxStats0(V, dmaPktRx3ErrorCnt_f, p_stats);

    return CTC_E_NONE;
}

/* get WORD in chip */
STATIC int32
_sys_goldengate_dma_rw_get_words_in_chip(uint8 lchip, uint16 entry_words, uint32* words_in_chip)
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
    else if ((33 <= entry_words) && (entry_words <= 64))
    {
        *words_in_chip = 64;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_dma_clear_desc(uint8 lchip, sys_dma_chan_t* p_dma_chan, uint32 sop_index, uint32 buf_count)
{
    uint32 i = 0;
    uint32 desc_index = 0;
    sys_dma_desc_t* p_base_desc = p_dma_chan->p_desc;
    DsDesc_m* p_desc = NULL;
    uint32 desc_len = 0;
    uint32 tbl_id = 0;
    drv_work_platform_type_t platform_type;
    DmaCtlTab0_m ctl_tab;
    uint32 cmd = 0;

    drv_goldengate_get_platform_type(&platform_type);

    desc_len = p_gg_dma_master[lchip]->dma_chan_info[p_dma_chan->channel_id].cfg_size;

    desc_index = sop_index;
    for (i = 0; i < buf_count; i++)
    {
        p_desc = &(p_base_desc[desc_index].desc_info);

        SetDsDesc(V, done_f, p_desc, 0);
        SetDsDesc(V, u1_pkt_sop_f, p_desc, 0);
        SetDsDesc(V, u1_pkt_eop_f, p_desc, 0);
        SetDsDesc(V, cfgSize_f, p_desc, desc_len);
        if (p_dma_chan->req_timeout_en)
        {
            SetDsDesc(V, realSize_f, p_desc, 0);
        }
        /*flush dma after write*/
        if ((NULL != g_dal_op.dma_cache_flush) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_flush(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_desc), sizeof(DsDesc_m));
        }

        desc_index++;
        if (desc_index >= p_dma_chan->desc_depth)
        {
            desc_index = 0;
        }
    }

    if (p_dma_chan->channel_id != SYS_DMA_PACKET_TX0_CHAN_ID && p_dma_chan->channel_id != SYS_DMA_PACKET_TX1_CHAN_ID)
    {
        /*return desc resource to DmaCtl */
        tbl_id = DmaCtlTab0_t + p_dma_chan->dmasel;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &ctl_tab);
        if (platform_type == HW_PLATFORM)
        {
            SetDmaCtlTab0(V, vldNum_f, &ctl_tab, buf_count);
        }
        else
        {
            SetDmaCtlTab0(V, vldNum_f, &ctl_tab, p_dma_chan->desc_depth);
        }
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, p_dma_chan->channel_id, cmd, &ctl_tab);
    }

    return CTC_E_NONE;
}

#define _DMA_FUNCTION_INTERFACE

/**
 @brief table DMA TX
*/
int32
_sys_goldengate_dma_read_table(uint8 lchip, DsDesc_m* p_tbl_desc)
{
    sys_dma_chan_t* p_dma_chan = &(p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_TBL_RD_CHAN_ID]);
    uint32 cmd = 0;
    uint16 rd_cnt = 0;
    bool  done = FALSE;
    uint32 i  = 0;
    int32 ret = 0;
    drv_work_platform_type_t  platform_type;
    uint32 tbl_id = 0;
    DmaCtlTab0_m ctl_tab;

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "Current descriptor, desc_index=%d,done:%d error:%d sop:%d eop:%d desc_len:%d !\n",
                    p_dma_chan->current_index,
                    GetDsDesc(V, done_f, p_tbl_desc),
                    GetDsDesc(V, error_f, p_tbl_desc),
                    GetDsDesc(V, u1_pkt_sop_f, p_tbl_desc),
                    GetDsDesc(V, u1_pkt_eop_f, p_tbl_desc),
                    GetDsDesc(V, cfgSize_f, p_tbl_desc));

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "desc_mem_addr_low =0x%x,desc_cfg_addr:0x%x  desc_words:%d !\n",
                    GetDsDesc(V, memAddr_f, p_tbl_desc),
                    GetDsDesc(V, chipAddr_f, p_tbl_desc),
                    GetDsDesc(V, dataStruct_f , p_tbl_desc));

    /* table DMA  valid num */
    tbl_id = DmaCtlTab0_t + p_dma_chan->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_DMA_TBL_RD_CHAN_ID, cmd, &ctl_tab));
    SetDmaCtlTab0(V, vldNum_f, &ctl_tab, 1);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_DMA_TBL_RD_CHAN_ID, cmd, &ctl_tab));

    /* wait for desc done */
    while (rd_cnt < SYS_DMA_TBL_COUNT)
    {
        rd_cnt++;

        /*inval dma before read*/
        if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_tbl_desc), sizeof(DsDesc_m));
        }
        /* transmit failed */
        if (1 == GetDsDesc(V, done_f, p_tbl_desc))
        {
            done  = TRUE;
            break;
        }

        if ((i++) >= 1000)
        {
#ifndef PACKET_TX_USE_SPINLOCK
            sal_task_sleep(1);
#else
            sal_udelay(1000);
#endif
             i = 0;
        }

         /*SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "DMA TX wait i = %d!!!!!!!!!!\n", i);*/
    }

    ret = drv_goldengate_get_platform_type(&platform_type);
    if (1)
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

    return ret;
}

/**
 @brief table DMA RX
*/
int32
_sys_goldengate_dma_write_table(uint8 lchip, DsDesc_m* p_tbl_desc)
{
    sys_dma_chan_t* p_dma_chan = &(p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_TBL_WR_CHAN_ID]);
    uint32 cmd = 0;
    uint16 wr_cnt = 0;
    bool  done = FALSE;
    int32 ret = 0;
    drv_work_platform_type_t  platform_type = MAX_WORK_PLATFORM;
    uint32 i  = 0;
    uint32 tbl_id = 0;
    DmaCtlTab0_m ctl_tab;

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "Current descriptor, desc_index=%d,done:%d error:%d sop:%d eop:%d desc_len:%d !\n",
                    p_dma_chan->current_index,
                    GetDsDesc(V, done_f, p_tbl_desc),
                    GetDsDesc(V, error_f, p_tbl_desc),
                    GetDsDesc(V, u1_pkt_sop_f, p_tbl_desc),
                    GetDsDesc(V, u1_pkt_eop_f, p_tbl_desc),
                    GetDsDesc(V, cfgSize_f, p_tbl_desc));

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "desc_mem_addr_low =0x%x,desc_cfg_addr:0x%x  desc_words:%d !\n",
                    GetDsDesc(V, memAddr_f, p_tbl_desc),
                    GetDsDesc(V, chipAddr_f, p_tbl_desc),
                    GetDsDesc(V, dataStruct_f , p_tbl_desc));

    /* table DMA  valid num */
    tbl_id = DmaCtlTab0_t + p_dma_chan->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_DMA_TBL_WR_CHAN_ID, cmd, &ctl_tab));
    SetDmaCtlTab0(V, vldNum_f, &ctl_tab, 1);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_DMA_TBL_WR_CHAN_ID, cmd, &ctl_tab));

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Current descriptor, desc_index=%d,done:%d error:%d sop:%d eop:%d cfg_size:%d !\n",
                    p_dma_chan->current_index,
                    GetDsDesc(V, done_f, p_tbl_desc),
                    GetDsDesc(V, error_f, p_tbl_desc),
                    GetDsDesc(V, u1_pkt_sop_f, p_tbl_desc),
                    GetDsDesc(V, u1_pkt_eop_f, p_tbl_desc),
                    GetDsDesc(V, cfgSize_f, p_tbl_desc));

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "desc_mem_addr_low =0x%x,desc_cfg_addr:0x%x  desc_words:%d !\n",
                    GetDsDesc(V, memAddr_f, p_tbl_desc),
                    GetDsDesc(V, chipAddr_f, p_tbl_desc),
                    GetDsDesc(V, dataStruct_f , p_tbl_desc));

    /* wait table write done */
    while (wr_cnt < SYS_DMA_TBL_COUNT)
    {
        wr_cnt++;
        /*inval dma before read*/
        if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_tbl_desc), sizeof(DsDesc_m));
        }
        /* transmit failed */
        if (1 == GetDsDesc(V, done_f, p_tbl_desc))
        {
            done  = TRUE;
            break;
        }

        if ((i++) >= 1000)
        {
#ifndef PACKET_TX_USE_SPINLOCK
            sal_task_sleep(1);
#else
            sal_udelay(1000);
#endif
            i = 0;
        }

        /* SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "DMA TX wait i = %d!!!!!!!!!!\n", i);*/
    }

    ret = drv_goldengate_get_platform_type(&platform_type);
    if (platform_type == HW_PLATFORM)
    {
        if (done == FALSE)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " desc not done!!!\n");
            ret  = CTC_E_DMA_TABLE_WRITE_FAILED;
        }
    }

    /* next descriptor, tbl_desc_index: 0~table_desc_num-1*/
    p_dma_chan->current_index =
        ((p_dma_chan->current_index + 1) == p_dma_chan->desc_depth) ? 0 : (p_dma_chan->current_index + 1);

    return ret;
}

int32
sys_goldengate_dma_function_pause(uint8 lchip, uint8 chan_id, uint8 en)
{
    DmaCtlDrainEnable0_m dma_drain;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 value = (en)?0:1;
    uint8 pcie_sel = 0;

    SYS_DMA_INIT_CHECK(lchip);
    if (chan_id > SYS_DMA_MONITOR_CHAN_ID)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &pcie_sel));
    tbl_id = DmaCtlDrainEnable0_t + pcie_sel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_drain));
    switch(chan_id)
    {
        case SYS_DMA_LEARNING_CHAN_ID:
            SetDmaCtlDrainEnable0(V,dmaInfo0DrainEn_f, &dma_drain, value);
            break;

        case SYS_DMA_HASHKEY_CHAN_ID:
            SetDmaCtlDrainEnable0(V,dmaInfo1DrainEn_f, &dma_drain, value);
            break;

        case SYS_DMA_IPFIX_CHAN_ID:
            SetDmaCtlDrainEnable0(V,dmaInfo2DrainEn_f, &dma_drain, value);
            break;

        case SYS_DMA_SDC_CHAN_ID:
            SetDmaCtlDrainEnable0(V,dmaInfo3DrainEn_f, &dma_drain, value);
            break;

        case SYS_DMA_MONITOR_CHAN_ID:
            SetDmaCtlDrainEnable0(V,dmaInfo4DrainEn_f, &dma_drain, value);
            break;

        case SYS_DMA_PACKET_RX0_CHAN_ID:
        case SYS_DMA_PACKET_RX1_CHAN_ID:
        case SYS_DMA_PACKET_RX2_CHAN_ID:
        case SYS_DMA_PACKET_RX3_CHAN_ID:
            value =  GetDmaCtlDrainEnable0(V,dmaPktRxDrainEn_f, &dma_drain);
            if (en)
            {
                value &= ~(1 << (chan_id - SYS_DMA_PACKET_RX0_CHAN_ID));
            }
            else
            {
                value |= (1 << (chan_id - SYS_DMA_PACKET_RX0_CHAN_ID));
            }
            SetDmaCtlDrainEnable0(V,dmaPktRxDrainEn_f, &dma_drain, value);
            break;

       case SYS_DMA_PACKET_TX0_CHAN_ID:
       case SYS_DMA_PACKET_TX1_CHAN_ID:
            value =  GetDmaCtlDrainEnable0(V,dmaPktTxDrainEn_f, &dma_drain);
            if (en)
            {
                value &= ~(1 << (chan_id - SYS_DMA_PACKET_TX0_CHAN_ID));
            }
            else
            {
                value |= (1 << (chan_id - SYS_DMA_PACKET_TX0_CHAN_ID));
            }
            SetDmaCtlDrainEnable0(V,dmaPktTxDrainEn_f, &dma_drain, value);
            break;

        case SYS_DMA_TBL_RD_CHAN_ID:
        case SYS_DMA_PORT_STATS_CHAN_ID:
        case SYS_DMA_PKT_STATS_CHAN_ID:
        case SYS_DMA_REG_MAX_CHAN_ID:
            value =  GetDmaCtlDrainEnable0(V,dmaRegRdDrainEn_f, &dma_drain);
            if (en)
            {
                value &= ~(1 << (chan_id - SYS_DMA_TBL_RD_CHAN_ID));
            }
            else
            {
                value |= (1 << (chan_id - SYS_DMA_TBL_RD_CHAN_ID));
            }
            SetDmaCtlDrainEnable0(V,dmaRegRdDrainEn_f, &dma_drain, value);
            break;

        case SYS_DMA_TBL_WR_CHAN_ID:
            SetDmaCtlDrainEnable0(V,dmaRegWrDrainEn_f, &dma_drain, value);
            break;

        default:
            break;
    }

    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_drain));

    return 0;
}

 /*-sal_systime_t g_tv0;*/

/*just using for add tcam key*/
int32
sys_goldengate_dma_write_multi_table(uint8 lchip, sys_dma_tbl_rw_t* tbl_cfg, uint8 num)
{
    int ret = CTC_E_NONE;
    DsDesc_m* p_tbl_desc = NULL;
    uint32 words_num = 1;
    uint16 entry_num = 0;
    uint16 idx = 0;
    uint32* p_src = NULL;
    uint32* p_dst = NULL;
    sys_dma_chan_t* p_dma_chan = NULL;
    uint32 phy_addr = 0;
    uint8 dma_en = 0;
    uint8 tb_idx = 0;
    sys_dma_tbl_rw_t* p_cfg = NULL;
    uint16 desc_idx = 0;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    DmaCtlTab0_m ctl_tab;
    uint8 desc_num = 0;
    uint16 last_desc = 0;
    sal_mutex_t* p_mutex = NULL;
    uint32 desc_done = 0;
    uint32 wait_cnt = 0;
    DmaCtlDrainEnable0_m dma_drain;

   if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
   {
      return CTC_E_NONE;
   }

    SYS_DMA_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(tbl_cfg);

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_GOTO(sys_goldengate_dma_get_chan_en(lchip, SYS_DMA_TBL_WR_CHAN_ID, &dma_en), ret, error);
    if (dma_en == 0)
    {
        SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Dma Write Function is not enabled!!!\n");
        ret = CTC_E_NOT_SUPPORT;
        return ret;
    }

    p_dma_chan = (sys_dma_chan_t*)&(p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_TBL_WR_CHAN_ID]);

    p_mutex = p_dma_chan->p_mutex;
    DMA_LOCK(p_mutex);

    if (num > p_dma_chan->desc_depth)
    {
        DMA_UNLOCK(p_mutex);
        return CTC_E_EXCEED_MAX_SIZE;
    }

    tbl_id = DmaCtlDrainEnable0_t + p_dma_chan->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &dma_drain);
    SetDmaCtlDrainEnable0(V,dmaInfo0DrainEn_f, &dma_drain, 0);
    SetDmaCtlDrainEnable0(V,dmaInfo1DrainEn_f, &dma_drain, 0);
    SetDmaCtlDrainEnable0(V,dmaInfo2DrainEn_f, &dma_drain, 0);
    SetDmaCtlDrainEnable0(V,dmaInfo3DrainEn_f, &dma_drain, 0);
    SetDmaCtlDrainEnable0(V,dmaInfo4DrainEn_f, &dma_drain, 0);
    SetDmaCtlDrainEnable0(V,dmaPktRxDrainEn_f, &dma_drain, 0);
    SetDmaCtlDrainEnable0(V,dmaPktTxDrainEn_f, &dma_drain, 0);
    SetDmaCtlDrainEnable0(V,dmaRegRdDrainEn_f, &dma_drain, 0);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &dma_drain);

    for (tb_idx = 0; tb_idx < num; tb_idx++)
    {
        p_cfg = (sys_dma_tbl_rw_t*)tbl_cfg + tb_idx;

        entry_num = p_cfg->entry_num;
        desc_idx = (p_dma_chan->current_index + tb_idx)%(p_dma_chan->desc_depth);
        p_tbl_desc = &(p_dma_chan->p_desc[desc_idx].desc_info);
        if (p_tbl_desc == NULL)
        {
            ret =  CTC_E_DMA_DESC_INVALID;
            goto error;
        }

        for (idx = 0; idx < entry_num; idx++)
        {
            p_dst = p_gg_dma_master[lchip]->g_tcam_wr_buffer[tb_idx] + idx * 4;
            p_src = p_cfg->buffer + idx * words_num;

            sal_memcpy(p_dst, p_src, words_num*SYS_DMA_WORD_LEN);
        }

        if (g_dal_op.logic_to_phy)
        {
            phy_addr = ((uint32)(g_dal_op.logic_to_phy(lchip, p_gg_dma_master[lchip]->g_tcam_wr_buffer[tb_idx]))) >> 4;
        }

        sal_memset(p_tbl_desc, 0, sizeof(DsDesc_m));

        if (p_cfg->is_pause)
        {
            SetDsDesc(V, pause_f, p_tbl_desc, 1);
        }
        else
        {
            SetDsDesc(V, pause_f, p_tbl_desc, 0);
        }

        SetDsDesc(V, memAddr_f, p_tbl_desc, (phy_addr));
        SetDsDesc(V, cfgSize_f, p_tbl_desc, 4);
        SetDsDesc(V, chipAddr_f, p_tbl_desc, (p_cfg->tbl_addr));
        SetDsDesc(V, dataStruct_f, p_tbl_desc, 1);
        /*flush dma after write*/
        if ((NULL != g_dal_op.dma_cache_flush) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_flush(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_tbl_desc), sizeof(DsDesc_m));
        }
        desc_num++;
        last_desc = desc_idx;
    }

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Dma Write using desc num:%d \n", desc_num);

    tbl_id = DmaCtlTab0_t + p_dma_chan->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, SYS_DMA_TBL_WR_CHAN_ID, cmd, &ctl_tab);
    SetDmaCtlTab0(V, vldNum_f, &ctl_tab, desc_num);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, SYS_DMA_TBL_WR_CHAN_ID, cmd, &ctl_tab);

    /*wait last desc done */
    p_tbl_desc = &(p_dma_chan->p_desc[last_desc].desc_info);

    do
    {
        /*inval dma before read*/
        if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_tbl_desc), sizeof(DsDesc_m));
        }
        desc_done = GetDsDesc(V, done_f, p_tbl_desc);
        if (desc_done)
        {
            break;
        }

        if (wait_cnt && ((wait_cnt%1000) == 0))
        {
#ifndef PACKET_TX_USE_SPINLOCK
            sal_task_sleep(1);
#else
            sal_udelay(1000);
#endif
        }
        wait_cnt++;

    }while(wait_cnt < 0xfffffff);

    if (!GetDsDesc(V, done_f, p_tbl_desc))
    {
        ret = CTC_E_DMA_DESC_NOT_DONE;
    }

    /*clear desc */
    for (tb_idx = 0; tb_idx < desc_num; tb_idx++)
    {
        desc_idx = (p_dma_chan->current_index + tb_idx)%(p_dma_chan->desc_depth);
        p_tbl_desc = &(p_dma_chan->p_desc[desc_idx].desc_info);
        SetDsDesc(V, pause_f, p_tbl_desc, 0);
        SetDsDesc(V, done_f, p_tbl_desc, 0);
        /*flush dma after write*/
        if ((NULL != g_dal_op.dma_cache_flush) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_flush(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_tbl_desc), sizeof(DsDesc_m));
        }
    }
error:

    tbl_id = DmaCtlDrainEnable0_t + p_dma_chan->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &dma_drain);
    SetDmaCtlDrainEnable0(V,dmaInfo0DrainEn_f, &dma_drain, 1);
    SetDmaCtlDrainEnable0(V,dmaInfo1DrainEn_f, &dma_drain, 1);
    SetDmaCtlDrainEnable0(V,dmaInfo2DrainEn_f, &dma_drain, 1);
    SetDmaCtlDrainEnable0(V,dmaInfo3DrainEn_f, &dma_drain, 1);
    SetDmaCtlDrainEnable0(V,dmaInfo4DrainEn_f, &dma_drain, 1);
    SetDmaCtlDrainEnable0(V,dmaPktRxDrainEn_f, &dma_drain, 0xf);
    SetDmaCtlDrainEnable0(V,dmaPktTxDrainEn_f, &dma_drain, 3);
    SetDmaCtlDrainEnable0(V,dmaRegRdDrainEn_f, &dma_drain, 0xf);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &dma_drain);

    p_dma_chan->current_index = (p_dma_chan->current_index + desc_num) % (p_dma_chan->desc_depth);

    DMA_UNLOCK(p_mutex);


    return ret;

}

/**
 @brief table DMA TX & RX
*/
int32
sys_goldengate_dma_rw_table(uint8 lchip, sys_dma_tbl_rw_t* tbl_cfg)
{
    int ret = CTC_E_NONE;
    DsDesc_m* p_tbl_desc = NULL;
    uint32* p_tbl_buff = NULL;
    uint32 words_in_chip = 0;
    uint32 words_num = 0;
    uint16 entry_num = 0;
    uint32 tbl_buffer_len = 0;
    uint32 tbl_addr = 0;
    uint16 idx = 0;
    uint32* p_src = NULL;
    uint32* p_dst = NULL;
    uint32* p = NULL;
    sys_dma_chan_t* p_dma_chan = NULL;
    uint32 phy_addr = 0;
    uint8 dma_en = 0;
    sal_mutex_t* p_mutex = NULL;

    SYS_DMA_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(tbl_cfg);

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        return CTC_E_NONE;
    }

    if (p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_EADP_DUMP])
    {
        chip_io_para_t para;
        para.op = CHIP_IO_OP_DMA_RW;
        para.u.dma_rw.chip_id = lchip;
        para.u.dma_rw.val = (void*)tbl_cfg;
        return p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_EADP_DUMP](lchip, &para);
    }

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "operate(1:read, 0:write): %d, table_addr: 0x%x, entry_num: %d, entry_len: %d lchip:%d\n",
                    tbl_cfg->rflag, tbl_cfg->tbl_addr, tbl_cfg->entry_num, tbl_cfg->entry_len, lchip);


    /* mask bit(1:0) */
    tbl_addr = tbl_cfg->tbl_addr;
    tbl_addr &= 0xfffffffc;

    words_num = (tbl_cfg->entry_len / SYS_DMA_WORD_LEN);
    entry_num = tbl_cfg->entry_num;

    ret = _sys_goldengate_dma_rw_get_words_in_chip(lchip, words_num, &words_in_chip);
    if (ret < 0)
    {
        return ret;
    }

    tbl_buffer_len = entry_num * words_in_chip * SYS_DMA_WORD_LEN;

    /* check data size should smaller than desc's cfg MAX size */
    if (tbl_buffer_len > 0xffff)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (NULL == g_dal_op.dma_alloc)
    {
        return CTC_E_DRV_FAIL;
    }
    p_tbl_buff = g_dal_op.dma_alloc(lchip, tbl_buffer_len, 0);
    if (NULL == p_tbl_buff)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_tbl_buff, 0, tbl_buffer_len);

    if (tbl_cfg->rflag)
    {
        CTC_ERROR_GOTO(sys_goldengate_dma_get_chan_en(lchip, SYS_DMA_TBL_RD_CHAN_ID, &dma_en), ret, error);
        if (dma_en == 0)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Dma Read Function is not enabled!!!\n");
            ret = CTC_E_NOT_SUPPORT;
            goto error;
        }

        p_dma_chan = (sys_dma_chan_t*)&(p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_TBL_RD_CHAN_ID]);
        p_mutex = p_dma_chan->p_mutex;
        DMA_LOCK(p_mutex);

        p_tbl_desc = &(p_dma_chan->p_desc[p_dma_chan->current_index].desc_info);
        if (p_tbl_desc == NULL){
            ret = CTC_E_DMA_DESC_INVALID;
            DMA_UNLOCK(p_mutex);
            goto error;
        }


        if (g_dal_op.logic_to_phy)
        {
           phy_addr = ((uint32)(g_dal_op.logic_to_phy(lchip, (void*)p_tbl_buff))) >> 4;
        }

        sal_memset(p_tbl_desc, 0, sizeof(DsDesc_m));

        SetDsDesc(V, memAddr_f, p_tbl_desc, (phy_addr));
        SetDsDesc(V, cfgSize_f, p_tbl_desc, tbl_buffer_len);
        SetDsDesc(V, chipAddr_f, p_tbl_desc, (tbl_addr));
        SetDsDesc(V, dataStruct_f, p_tbl_desc, ((words_num == 64)?0:words_num));
        /*flush dma after write*/
        if ((NULL != g_dal_op.dma_cache_flush) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_flush(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_tbl_desc), sizeof(DsDesc_m));
        }

        /* read */
        ret = _sys_goldengate_dma_read_table(lchip, p_tbl_desc);
        if (ret < 0)
        {
            DMA_UNLOCK(p_mutex);
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
                g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_src), sizeof(DsDesc_m));
            }

            sal_memcpy(p_dst, p_src, words_num*SYS_DMA_WORD_LEN);
        }

        /*for debug show result */
        for (idx = 0; idx < entry_num * words_num; idx++)
        {
            p = (uint32*)(tbl_cfg->buffer) + idx;
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Read table(word %d) dma value: 0x%x \n", idx, *p);
        }
        DMA_UNLOCK(p_mutex);
    }
    else
    {
        CTC_ERROR_GOTO(sys_goldengate_dma_get_chan_en(lchip, SYS_DMA_TBL_WR_CHAN_ID, &dma_en), ret, error);
        if (dma_en == 0)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Dma Write Function is not enabled!!!\n");
            ret = CTC_E_NOT_SUPPORT;
            goto error;
        }

        p_dma_chan = (sys_dma_chan_t*)&(p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_TBL_WR_CHAN_ID]);
        p_mutex = p_dma_chan->p_mutex;
        DMA_LOCK(p_mutex);

        p_tbl_desc = &(p_dma_chan->p_desc[p_dma_chan->current_index].desc_info);
        if (p_tbl_desc == NULL)
        {
            ret =  CTC_E_DMA_DESC_INVALID;
            DMA_UNLOCK(p_mutex);
            goto error;
        }

        /* for debug show orginal data */
        for (idx = 0; idx < entry_num * words_num; idx++)
        {
            p = (uint32*)(tbl_cfg->buffer) + idx;
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Write table(word %d) ogrinal value: 0x%x \n", idx, *p);
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
            phy_addr = ((uint32)(g_dal_op.logic_to_phy(lchip, p_tbl_buff))) >> 4;
        }
        /*flush dma after write*/
        if ((NULL != g_dal_op.dma_cache_flush) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_flush(lchip, phy_addr, tbl_buffer_len);
        }
        sal_memset(p_tbl_desc, 0, sizeof(DsDesc_m));
        SetDsDesc(V, memAddr_f, p_tbl_desc, (phy_addr));
        SetDsDesc(V, cfgSize_f, p_tbl_desc, (tbl_buffer_len));
        SetDsDesc(V, chipAddr_f, p_tbl_desc, (tbl_addr));
        SetDsDesc(V, dataStruct_f, p_tbl_desc, words_in_chip);
        /*flush dma after write*/
        if ((NULL != g_dal_op.dma_cache_flush) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_flush(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_tbl_desc), sizeof(DsDesc_m));
        }

        /* write */
        ret = _sys_goldengate_dma_write_table(lchip, p_tbl_desc);
        if (ret < 0)
        {
            DMA_UNLOCK(p_mutex);
            goto error;
        }

        DMA_UNLOCK(p_mutex);
    }

error:

    if (g_dal_op.dma_free)
    {
        g_dal_op.dma_free(lchip, p_tbl_buff);
    }

    return ret;

}

/**
 @brief packet DMA TX, p_pkt_tx pointer to sys_pkt_tx_info_t
*/
int32
sys_goldengate_dma_pkt_tx(ctc_pkt_tx_t* p_pkt_tx)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint32 pkt_len = 0;
    bool done = FALSE;
    uint8* tx_pkt_addr = NULL;
    DsDesc_m* p_tx_desc_mem = NULL;
    sys_dma_chan_t* p_dma_chan = NULL;
    DmaCtlTab0_m ctl_tab;
    uint64 phy_addr = 0;
    uint32 cnt = 0;
    uint32 tbl_id = 0;
    drv_work_platform_type_t platform_type;
    uint32 valid_cnt = 0;
    uint8 dma_en_tx0 = 0;
    uint8 dma_en_tx1 = 0;
    uint8 chan_idx = 0;
    sal_mutex_t* p_mutex = NULL;
    uint8 lchip = 0;
    uint8 hdr_len = 0;
    uint8 user_flag = 0;
    sys_pkt_tx_info_t* p_pkt_tx_info = (sys_pkt_tx_info_t*)p_pkt_tx;

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        return CTC_E_NONE;
    }

    lchip = p_pkt_tx_info->lchip;
    user_flag = p_pkt_tx_info->user_flag;
    if (user_flag)
    {
        hdr_len = p_pkt_tx_info->header_len;
    }
    SYS_DMA_INIT_CHECK(lchip);

    /* packet length check */
    SYS_DMA_PKT_LEN_CHECK(p_pkt_tx_info->data_len + hdr_len);

    CTC_ERROR_RETURN(sys_goldengate_dma_get_chan_en(lchip, SYS_DMA_PACKET_TX0_CHAN_ID, &dma_en_tx0));
    CTC_ERROR_RETURN(sys_goldengate_dma_get_chan_en(lchip, SYS_DMA_PACKET_TX1_CHAN_ID, &dma_en_tx1));
    if (dma_en_tx0 == 0 || dma_en_tx1 == 0)
    {
        SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Dma Packet Tx  Function is not enabled!!!\n");
        return CTC_E_NOT_SUPPORT;
    }

    drv_goldengate_get_platform_type(&platform_type);

    /* use which channel should by the packet's priority */
    chan_idx = (p_pkt_tx_info->tx_info.priority > ((p_gg_queue_master[lchip]->priority_mode)? 10:40))?1:0;
    p_dma_chan = (sys_dma_chan_t*)&(p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_PACKET_TX0_CHAN_ID + chan_idx]);

    CTC_PTR_VALID_CHECK(p_dma_chan->p_desc_used);

    p_mutex = p_dma_chan->p_mutex;
    DMA_LOCK(p_mutex);

    p_tx_desc_mem = &(p_dma_chan->p_desc[p_dma_chan->current_index].desc_info);
    if (p_tx_desc_mem == NULL)
    {
        DMA_UNLOCK(p_mutex);
        return CTC_E_DMA_DESC_INVALID;
    }

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "data len: %d\n", p_pkt_tx_info->data_len);

    /* check last transmit is done */
    if (p_dma_chan->p_desc_used[p_dma_chan->current_index])
    {
        while(cnt < 100)
        {
            /*inval dma before read*/
            if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
            {
                g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_tx_desc_mem), sizeof(DsDesc_m));
            }
            if (GetDsDesc(V, done_f, p_tx_desc_mem))
            {
                done = TRUE;
                break;
                /* last transmit is done */
            }

#ifndef PACKET_TX_USE_SPINLOCK
            sal_task_sleep(1);
#else
            sal_udelay(1000);
#endif
            cnt++;
        }

        if (!done)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "last transmit is not done,%d\n", p_dma_chan->current_index);
            p_dma_chan->p_desc_used[p_dma_chan->current_index] = 0;
            ret = CTC_E_DMA_DESC_NOT_DONE;
            goto error;
        }
    }

    _sys_goldengate_dma_clear_desc(lchip, p_dma_chan, p_dma_chan->current_index, 1);

    /* data */
    pkt_len = p_pkt_tx_info->data_len;

    COMBINE_64BITS_DATA(p_gg_dma_master[lchip]->dma_high_addr,             \
                        (GetDsDesc(V, memAddr_f, p_tx_desc_mem)<<4), phy_addr);

    if (g_dal_op.phy_to_logic)
    {
        tx_pkt_addr = (uint8*)g_dal_op.phy_to_logic(lchip, (phy_addr));
    }

    if (user_flag)
    {
        sal_memcpy((uint8*)tx_pkt_addr, p_pkt_tx_info->header, hdr_len);
    }
    sal_memcpy((uint8*)tx_pkt_addr + hdr_len, p_pkt_tx_info->data, p_pkt_tx_info->data_len);
    /*flush dma after write*/
    if ((NULL != g_dal_op.dma_cache_flush) && (NULL != g_dal_op.logic_to_phy))
    {
        g_dal_op.dma_cache_flush(lchip, phy_addr, pkt_len);
    }

    SetDsDesc(V, u1_pkt_eop_f, p_tx_desc_mem, 1);
    SetDsDesc(V, u1_pkt_sop_f, p_tx_desc_mem, 1);
    SetDsDesc(V, cfgSize_f, p_tx_desc_mem, pkt_len + hdr_len);
    /*flush dma after write*/
    if ((NULL != g_dal_op.dma_cache_flush) && (NULL != g_dal_op.logic_to_phy))
    {
        g_dal_op.dma_cache_flush(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_tx_desc_mem), sizeof(DsDesc_m));
    }

    /* tx valid num */
    tbl_id = DmaCtlTab0_t + p_dma_chan->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, SYS_DMA_PACKET_TX0_CHAN_ID + chan_idx, cmd, &ctl_tab), ret, error);
    if (platform_type == HW_PLATFORM)
    {
        valid_cnt = 1;
    }
    else
    {
        valid_cnt = GetDmaCtlTab0(V, vldNum_f, &ctl_tab);
        valid_cnt += 1;
    }
    SetDmaCtlTab0(V, vldNum_f, &ctl_tab, valid_cnt);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, SYS_DMA_PACKET_TX0_CHAN_ID + chan_idx, cmd, &ctl_tab), ret, error);

    /*update desc used info */
    p_dma_chan->p_desc_used[p_dma_chan->current_index] = 1;

    /* next descriptor, tx_desc_index: 0~tx_desc_num-1*/
    p_dma_chan->current_index =
        (p_dma_chan->current_index == (p_dma_chan->desc_depth - 1)) ? 0 : (p_dma_chan->current_index + 1);

    if (p_gg_dma_master[lchip]->dma_stats.tx_sync_cnt > 1000)
    {
        sys_goldengate_dma_sync_pkt_tx_stats(lchip, p_dma_chan->dmasel);
        p_gg_dma_master[lchip]->dma_stats.tx_sync_cnt = 0;
    }
    else
    {
        p_gg_dma_master[lchip]->dma_stats.tx_sync_cnt++;
    }
error:

    DMA_UNLOCK(p_mutex);
    return ret;
}

/**
 @brief Dma register callback function
*/
int32
sys_goldengate_dma_register_cb(uint8 lchip, sys_dma_cb_type_t type, DMA_CB_FUN_P cb)
{
    if (NULL == p_gg_dma_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    if (type >= SYS_DMA_CB_MAX_TYPE)
    {
        return CTC_E_INVALID_PARAM;
    }

     p_gg_dma_master[lchip]->dma_cb[type] = cb;

    return CTC_E_NONE;
}

/*
Notice:this interface is used to wait dmactl to finish dma op and write back desc to memory
Before using this interface must config dmactl already begin process data and not finish
Now only used for hashdump
*/
int32
sys_goldengate_dma_wait_desc_done(uint8 lchip, uint8 chan_id)
{
    sys_dma_chan_t* p_dma_chan = NULL;
    sys_dma_desc_t* p_base_desc = NULL;
    DsDesc_m* p_desc = NULL;
    uint32 cur_index = 0;
    uint32 desc_done = 0;
    uint32 wait_cnt = 0;

    if (p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_EADP_DUMP])
    {
        return CTC_E_NONE;
    }

    p_dma_chan = &p_gg_dma_master[lchip]->dma_chan_info[chan_id];
    p_base_desc = p_dma_chan->p_desc;
    cur_index = p_dma_chan->current_index;

    p_desc = &p_base_desc[cur_index].desc_info;

    do
    {
        /*inval dma before read*/
        if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_desc), sizeof(DsDesc_m));
        }
        desc_done = GetDsDesc(V, done_f, p_desc);
        if (desc_done)
        {
            break;
        }

#ifndef PACKET_TX_USE_SPINLOCK
        sal_task_sleep(1);
#else
        sal_udelay(1000);
#endif
        wait_cnt++;

    }while(wait_cnt < 0x1000);

    if (desc_done)
    {
        return CTC_E_NONE;
    }
    else
    {
        return CTC_E_DMA_DESC_NOT_DONE;
    }

}


int32
sys_goldengate_dma_clear_chan_data(uint8 lchip, uint8 chan_id)
{
    sys_dma_chan_t* p_dma_chan = NULL;
    sys_dma_desc_t* p_base_desc = NULL;
    DsDesc_m* p_desc = NULL;
    uint32 cur_index = 0;
    int32 ret = 0;

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (SYS_DMA_MAX_CHAN_NUM <= chan_id)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_EADP_DUMP])
    {
        chip_io_para_t para;
        para.op = CHIP_IO_OP_DMA_DUMP;
        para.u.dma_dump.chip_id = lchip;
        para.u.dma_dump.threshold = 0;
        para.u.dma_dump.val = NULL;
        return p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_EADP_DUMP](lchip, &para);
    }

    p_dma_chan = &p_gg_dma_master[lchip]->dma_chan_info[chan_id];
    p_base_desc = p_dma_chan->p_desc;
    cur_index = p_dma_chan->current_index;

    ret = sys_goldengate_dma_wait_desc_done(lchip, chan_id);
    if (ret == CTC_E_NONE)
    {
        for(;; cur_index++)
        {
            if (cur_index >= p_dma_chan->desc_depth)
            {
                cur_index = 0;
            }

            p_desc = &p_base_desc[cur_index].desc_info;
            /*inval dma before read*/
            if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
            {
                g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_desc), sizeof(DsDesc_m));
            }
            if (GetDsDesc(V, done_f, p_desc))
            {
                /* clear Desc and return Desc to DmaCtl*/
               _sys_goldengate_dma_clear_desc(lchip, p_dma_chan, cur_index, 1);
            }
            else
            {
                break;
            }
        }

        p_dma_chan->current_index = cur_index;
    }

    return ret;
}

int32
sys_goldengate_dma_set_chan_en(uint8 lchip, uint8 chan_id, uint8 chan_en)
{
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    sys_dma_chan_t* p_chan_info = NULL;
    DmaStaticInfo0_m static_info;

    SYS_DMA_INIT_CHECK(lchip);
    p_chan_info = (sys_dma_chan_t*)&p_gg_dma_master[lchip]->dma_chan_info[chan_id];
    sal_memset(&static_info, 0, sizeof(DmaStaticInfo0_m));

    if (!CTC_IS_BIT_SET(p_gg_dma_master[lchip]->dma_en_flag, chan_id))
    {
        return CTC_E_NONE;
    }

    tbl_id = DmaStaticInfo0_t + p_chan_info->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &static_info));
    SetDmaStaticInfo0(V, chanEn_f, &static_info, chan_en);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &static_info));

    return CTC_E_NONE;
}

int32
sys_goldengate_dma_get_chan_en(uint8 lchip, uint8 chan_id, uint8* chan_en)
{
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    sys_dma_chan_t* p_chan_info = NULL;
    DmaStaticInfo0_m static_info;

    SYS_DMA_INIT_CHECK(lchip);

    p_chan_info = (sys_dma_chan_t*)&p_gg_dma_master[lchip]->dma_chan_info[chan_id];
    sal_memset(&static_info, 0, sizeof(DmaStaticInfo0_m));

    tbl_id = DmaStaticInfo0_t + p_chan_info->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &static_info));

    *chan_en = GetDmaStaticInfo0(V, chanEn_f, &static_info);

    return CTC_E_NONE;
}

int32
sys_goldengate_dma_sync_hash_dump(uint8 lchip, sys_dma_info_t* p_dma_info, uint16 threshold, uint32 cnt, uint8 fifo_full)
{
    sys_dma_chan_t* p_dma_chan = NULL;
    sys_dma_desc_t* p_base_desc = NULL;
    DsDesc_m* p_desc = NULL;
    DmaFibDumpFifo_m* p_dump = NULL;
    DmaFibDumpFifo_m* p_addr_start = (DmaFibDumpFifo_m*)p_dma_info->p_data;
    uint32 cur_index = 0;
    uint8 process_cnt = 0;
    uint32 desc_done = 0;
    uint16 wait_cnt = 0;
    uint8 dma_done = 0;
    uint32 real_size = 0;
    /*uint32 cfg_size = 0;*/
    uint64 phy_addr = 0;
    uint32 entry_num = 0;
    int32 ret = 0;
    uint8 end_flag = 0;

    SYS_LCHIP_CHECK_ACTIVE(lchip);
    if (p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_EADP_DUMP])
    {
        chip_io_para_t para;
        para.op = CHIP_IO_OP_DMA_DUMP;
        para.u.dma_dump.chip_id = lchip;
        para.u.dma_dump.threshold = threshold;
        para.u.dma_dump.val = (void*)p_dma_info;
        return p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_EADP_DUMP](lchip, &para);
    }

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    p_dma_chan = &p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_HASHKEY_CHAN_ID];
    p_base_desc = p_dma_chan->p_desc;
    cur_index = p_dma_chan->current_index;

    for(;; cur_index++)
    {
        dma_done = 0;
        wait_cnt = 0;
        if (cur_index >= p_dma_chan->desc_depth)
        {
            cur_index = 0;
        }

        p_desc = &p_base_desc[cur_index].desc_info;

        do
        {
            /*inval dma before read*/
            if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
            {
                g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_desc), sizeof(DsDesc_m));
            }
            desc_done = GetDsDesc(V, done_f, p_desc);
            if (desc_done)
            {
                dma_done = 1;
                break;
            }

#ifndef PACKET_TX_USE_SPINLOCK
            sal_task_sleep(1);
#else
            sal_udelay(1000);
#endif
            wait_cnt++;

        }while(wait_cnt < 0xffff);

        if (dma_done == 0)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Desc is not done!! \n");
            ret = CTC_E_DMA_DESC_NOT_DONE;
            /* dma not done,  need end current dma operate */
            goto end;
        }

        process_cnt++;

        /* get current desc real size */
        real_size = GetDsDesc(V, realSize_f, p_desc);

        /*cfg_size = GetDsDesc(V, cfgSize_f, p_desc);*/
        COMBINE_64BITS_DATA(p_gg_dma_master[lchip]->dma_high_addr,             \
                                (GetDsDesc(V, memAddr_f, p_desc)<<4), phy_addr);

        p_dump = (DmaFibDumpFifo_m*)g_dal_op.phy_to_logic(lchip, phy_addr);

        entry_num += real_size;


        if (entry_num > threshold)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Exceed, entry_num:%d, threshold:%d real_size:%d!! \n", entry_num, threshold, real_size);
            ret = CTC_E_EXCEED_MAX_SIZE;
           _sys_goldengate_dma_clear_desc(lchip, p_dma_chan, cur_index, 1);
            goto end;
        }

        /* check the last one */
        if (GetDmaFibDumpFifo(V, isLastEntry_f, &p_dump[real_size-1]))
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "is lastEntry! \n");

            if (GetDmaFibDumpFifo(V, isMac_f, &p_dump[real_size-1]))
            {
                SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Last entry is Mac entry! \n");
                /* query is end */
                p_dma_info->entry_num = entry_num;
                sal_memcpy((uint8*)p_addr_start, (uint8*)p_dump, sizeof(DmaFibDumpFifo_m)*real_size);
            }
            else
            {
                SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Last entry isnot Mac entry! \n");

                /* query is end, but last is invalid */
                p_dma_info->entry_num = entry_num-1;
                sal_memcpy((uint8*)p_addr_start, (uint8*)p_dump, sizeof(DmaFibDumpFifo_m)*(real_size-1));
            }

            end_flag = 1;
        }
        else
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Not Last entry but is Mac entry! \n");

            /* process next desc */
            sal_memcpy((uint8*)p_addr_start, (uint8*)p_dump, sizeof(DmaFibDumpFifo_m)*real_size);
            p_addr_start += real_size;
        }

        if (cnt && (!end_flag) && (cnt == entry_num) && fifo_full)
        {
            p_dma_info->entry_num = entry_num;
            end_flag = 1;
        }
        /* clear desc */
       _sys_goldengate_dma_clear_desc(lchip, p_dma_chan, cur_index, 1);


        if (end_flag == 1)
        {
            break;
        }
    }
    end:
    p_dma_chan->current_index = ((p_dma_chan->current_index + process_cnt) % (p_dma_chan->desc_depth));


    return ret;
}

int32
sys_goldengate_dma_get_packet_rx_chan(uint8 lchip, uint16* p_num)
{
    SYS_DMA_INIT_CHECK(lchip);
    *p_num = p_gg_dma_master[lchip]->packet_rx_chan_num;
    return CTC_E_NONE;
}

int32
sys_goldengate_dma_get_hw_learning_sync(uint8 lchip, uint8* b_sync)
{
    SYS_DMA_INIT_CHECK(lchip);
    *b_sync = p_gg_dma_master[lchip]->hw_learning_sync;
    return CTC_E_NONE;
}

/*
    interval: uint is us
*/
int32
sys_goldengate_dma_set_tcam_interval(uint8 lchip, uint32 interval)
{
    uint8 pcie_sel = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    DmaRegWrTrigCfg0_m wr_trigger;
    uint32 interval_ns = 0;
    uint32 interval_s = 0;

    interval_s = interval/1000000;
    interval_ns = (interval%1000000)*1000;
    SYS_DMA_INIT_CHECK(lchip);
    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set Tcam Interval:s-%d,ns-%d\n", interval_s, interval_ns);

    CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &pcie_sel));
    tbl_id = DmaRegWrTrigCfg0_t + pcie_sel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &wr_trigger));
    SetDmaRegWrTrigCfg0(V, cfgRegWrTrigNanoSec_f, &wr_trigger, interval_ns);
    SetDmaRegWrTrigCfg0(V, cfgRegWrTrigSecond_f, &wr_trigger, interval_s);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &wr_trigger));


    return CTC_E_NONE;
}

int32
sys_goldengate_dma_tcam_write(uint8 lchip, uint32 trig)
{
    sys_dma_tbl_rw_t tbl_cfg[13];
    uint8 num = 0;
    uint8 i = 0;
    uint32 value[3] = {0,2,6};
    uint32 value1[6] = {1,3,7};
    uint32 null_value = 0;
    uint32 addr[3] = {0x8032e2b8,
                      0x8030a950,
                      0x804053c4};
    num = 13;
    sal_memset(tbl_cfg, 0, sizeof(tbl_cfg));

    i = 0;

    /*Step No.1*/
    /*DMA NULL SupCtlFifoCtl value*/
    tbl_cfg[i].tbl_addr = 0xf0000000;
    tbl_cfg[i].buffer = &null_value;
    tbl_cfg[i].entry_num = 1;
    tbl_cfg[i].entry_len = 4;
    tbl_cfg[i].rflag = 0;
    tbl_cfg[i].is_pause = 1;
    i++;

    /*Step No.2*/
    /*DMA NULL SupCtlFifoCtl value*/
    tbl_cfg[i].tbl_addr = 0xf0000000;
    tbl_cfg[i].buffer = &null_value;
    tbl_cfg[i].entry_num = 1;
    tbl_cfg[i].entry_len = 4;
    tbl_cfg[i].rflag = 0;
    tbl_cfg[i].is_pause = 1;
    i++;

    /*Step No.3*/
    if (p_gg_dma_master[lchip]->g_drain_en[0])
    {
        /*IntfMap Drain disable*/
        tbl_cfg[i].tbl_addr = addr[0];
        tbl_cfg[i].buffer =  &value[0];
        tbl_cfg[i].entry_num = 1;
        tbl_cfg[i].entry_len = 4;
        tbl_cfg[i].rflag = 0;
        tbl_cfg[i].is_pause = 1;
        i++;
    }

    /*Step No.4*/
    if (p_gg_dma_master[lchip]->g_drain_en[2])
    {
        /*EpeAclOam Drain disable*/
        tbl_cfg[i].tbl_addr = addr[2];
        tbl_cfg[i].buffer =  &value[2];
        tbl_cfg[i].entry_num = 1;
        tbl_cfg[i].entry_len = 4;
        tbl_cfg[i].rflag = 0;
        tbl_cfg[i].is_pause = 0;
        i++;
    }

    /*Step No.5*/
    if (p_gg_dma_master[lchip]->g_drain_en[1])
    {
        /*IpeACLkupMgr Drain disable*/
        tbl_cfg[i].tbl_addr = addr[1];
        tbl_cfg[i].buffer =  &value[1];
        tbl_cfg[i].entry_num = 1;
        tbl_cfg[i].entry_len = 4;
        tbl_cfg[i].rflag = 0;
        tbl_cfg[i].is_pause = 0;
        i++;
    }

    /*Step No.6*/
    /*TCAM Write Trig*/
    tbl_cfg[i].tbl_addr = 0x004c1be8;
    tbl_cfg[i].buffer = &trig;
    tbl_cfg[i].entry_num = 1;
    tbl_cfg[i].entry_len = 4;
    tbl_cfg[i].rflag = 0;
    tbl_cfg[i].is_pause = 0;
    i++;


    /*Step No.7*/
    /*DMA NULL SupCtlFifoCtl value*/
    tbl_cfg[i].tbl_addr = 0xf0000000;
    tbl_cfg[i].buffer = &null_value;
    tbl_cfg[i].entry_num = 1;
    tbl_cfg[i].entry_len = 4;
    tbl_cfg[i].rflag = 0;
    tbl_cfg[i].is_pause = 0;
    i++;

    /*Step No.8*/
    if (p_gg_dma_master[lchip]->g_drain_en[0])
    {
        /*IntfMap Drain enable*/
        tbl_cfg[i].tbl_addr = addr[0];
        tbl_cfg[i].buffer =  &value1[0];
        tbl_cfg[i].entry_num = 1;
        tbl_cfg[i].entry_len = 4;
        tbl_cfg[i].rflag = 0;
        tbl_cfg[i].is_pause = 0;
        i++;
    }

    /*Step No.9*/
    if (p_gg_dma_master[lchip]->g_drain_en[2])
    {
        /*EpeAclOam Drain enable*/
        tbl_cfg[i].tbl_addr = addr[2];
        tbl_cfg[i].buffer =  &value1[2];
        tbl_cfg[i].entry_num = 1;
        tbl_cfg[i].entry_len = 4;
        tbl_cfg[i].rflag = 0;
        tbl_cfg[i].is_pause = 0;
        i++;
    }

    /*Step No.10*/
    if (p_gg_dma_master[lchip]->g_drain_en[1])
    {
        /*IpeLkupMgr Drain enable*/
        tbl_cfg[i].tbl_addr = addr[1];
        tbl_cfg[i].buffer =  &value1[1];
        tbl_cfg[i].entry_num = 1;
        tbl_cfg[i].entry_len = 4;
        tbl_cfg[i].rflag = 0;
        tbl_cfg[i].is_pause = 0;
        i++;
    }

    num = i;

    DRV_IF_ERROR_RETURN(sys_goldengate_dma_write_multi_table(lchip, tbl_cfg, num));


    return DRV_E_NONE;
}

/*timer uinit is ms, 0 means disable*/
int32
sys_goldengate_dma_set_monitor_timer(uint8 lchip, uint32 timer)
{
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    DmaInfoTimerCfg0_m global_timer_ctl;
    DmaInfo4TimerCfg0_m info4_timer;
    uint32 time_s = 0;
    uint32 time_ns = 0;
    uint8 pcie_sel = 0;

    CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &pcie_sel));
    tbl_id = DmaInfo4TimerCfg0_t + pcie_sel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &info4_timer));


    tbl_id = DmaInfoTimerCfg0_t + pcie_sel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &global_timer_ctl));

    if (timer)
    {
        time_s = timer/1000;
        time_ns = (timer%1000)*1000000;

        SetDmaInfoTimerCfg0(V, cfgInfo4DescTimerChk_f, &global_timer_ctl, 1);/*monitor*/

        SetDmaInfo4TimerCfg0(V, cfgInfo4TimerSecond_f, &info4_timer, time_s);
        SetDmaInfo4TimerCfg0(V, cfgInfo4TimerNanoSec_f, &info4_timer, time_ns);

        tbl_id = DmaInfo4TimerCfg0_t + pcie_sel;
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &info4_timer));
    }
    else
    {
        SetDmaInfoTimerCfg0(V, cfgInfo4DescTimerChk_f, &global_timer_ctl, 0);/*monitor*/
    }

    tbl_id = DmaInfoTimerCfg0_t + pcie_sel;
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &global_timer_ctl));

    return DRV_E_NONE;
}

/**
@brief  Set DMA packet rx priority, high-low:3-0
*/
int32
sys_goldengate_dma_set_pkt_rx_priority(uint8 lchip, uint8 chan_id, uint8 prio)
{
    uint32 cmd = 0;
    uint8  index = 0;
    uint8 prio_cfg = 0;
    uint8 pcie_sel = 0;
    uint32 tbl_id = 0;
    DmaPktRxPriCfg0_m pktrx_prio;

    SYS_DMA_INIT_CHECK(lchip);
    if ((chan_id >= CTC_DMA_PKT_RX_CHAN_NUM) || (prio >= CTC_DMA_PKT_RX_CHAN_NUM))
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set packet-rx priority:chan-%d,prio-%d\n", chan_id, prio);
    CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &pcie_sel));
    tbl_id = DmaPktRxPriCfg0_t + pcie_sel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pktrx_prio));
    for (index = 0; index < 4; index++)
    {
        prio_cfg = GetDmaPktRxPriCfg0(V, dmaPktRxPri0Cfg_f+index, &pktrx_prio);
        if (index == prio)
        {
            CTC_BIT_SET(prio_cfg, chan_id);
        }
        else
        {
            CTC_BIT_UNSET(prio_cfg, chan_id);
        }
        SetDmaPktRxPriCfg0(V, dmaPktRxPri0Cfg_f+index, &pktrx_prio, prio_cfg);
    }

    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pktrx_prio));

    return CTC_E_NONE;
}

/**
@brief  Get DMA packet rx priority, high-low:3-0
*/
int32
sys_goldengate_dma_get_pkt_rx_priority(uint8 lchip, uint8 chan_id, uint8* prio)
{
    uint32 cmd = 0;
    uint8  index = 0;
    uint8 prio_cfg = 0;
    uint8 pcie_sel = 0;
    uint32 tbl_id = 0;
    DmaPktRxPriCfg0_m pktrx_prio;

    SYS_DMA_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(prio);
    if (chan_id >= CTC_DMA_PKT_RX_CHAN_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &pcie_sel));
    tbl_id = DmaPktRxPriCfg0_t + pcie_sel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pktrx_prio));
    for (index = 0; index < 4; index++)
    {
        prio_cfg = GetDmaPktRxPriCfg0(V, dmaPktRxPri0Cfg_f+index, &pktrx_prio);
        if (CTC_IS_BIT_SET(prio_cfg, chan_id))
        {
            *prio = index;
            break;
        }

    }

    return CTC_E_NONE;
}

#define _DMA_ISR_BEGIN

/**
@brief  DMA packet rx function process
*/
int32
_sys_goldengate_dma_pkt_rx_func(uint8 lchip, uint8 chan, uint8 dmasel)
{
    ctc_pkt_buf_t pkt_buf[64];
    ctc_pkt_rx_t pkt_rx;
    ctc_pkt_rx_t* p_pkt_rx = &pkt_rx;
    sys_dma_chan_t* p_dma_chan = NULL;
    sys_dma_desc_t* p_base_desc = NULL;
    DsDesc_m* p_desc = NULL;
    uint32 cur_index = 0;
    uint32 sop_index = 0;
    uint32 buf_count = 0;
    uint64 phy_addr = 0;
    uint32 process_cnt = 0;
    uint32 index = 0;
    uint32 is_sop = 0;
    uint32 is_eop = 0;
    uint32 desc_done = 0;
    uint8 need_eop = 0;
    uint32 wait_cnt = 0;
    int32 ret = 0;

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%s(), %d, %d\n", __FUNCTION__, chan, dmasel);

    sal_memset(p_pkt_rx, 0, sizeof(ctc_pkt_rx_t));
    p_pkt_rx->mode = CTC_PKT_MODE_DMA;
    p_pkt_rx->pkt_buf = pkt_buf;

    /* init check */
    SYS_DMA_INIT_CHECK(lchip);

    p_dma_chan = &p_gg_dma_master[lchip]->dma_chan_info[chan];
    p_base_desc = p_dma_chan->p_desc;
    cur_index = p_dma_chan->current_index;
    for (;; cur_index++)
    {
        if (cur_index >= p_dma_chan->desc_depth)
        {
            cur_index = 0;
        }
        ret = sys_goldengate_chip_check_active(lchip);
        if (ret < 0)
        {
            break;
        }
        p_desc = &(p_base_desc[cur_index].desc_info);
        /*inval dma before read*/
        if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_desc), sizeof(DsDesc_m));
        }
        desc_done = GetDsDesc(V, done_f, p_desc);

        if (0 == desc_done)
        {
            if (need_eop)
            {
                 SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Desc not done, But need eop!!desc index %d\n", cur_index);

                while(wait_cnt < 0xffff)
                {
                    /*inval dma before read*/
                    if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
                    {
                        g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_desc), sizeof(DsDesc_m));
                    }
                    if (GetDsDesc(V, done_f, p_desc))
                    {
                        break;
                    }
                    wait_cnt++;
                }

                /* Cannot get EOP, means no EOP packet error, just clear desc*/
                if (wait_cnt >= 0xffff)
                {
                    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "No EOP, desc index %d, buf_count %d\n", cur_index, buf_count);
                   _sys_goldengate_dma_clear_desc(lchip, p_dma_chan, sop_index, buf_count);
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

        is_sop = GetDsDesc(V, u1_pkt_sop_f, p_desc);
        is_eop = GetDsDesc(V, u1_pkt_eop_f, p_desc);

        /*Before get EOP, next packet SOP come, no EOP packet error, drop error packet */
        if (need_eop && is_sop)
        {
            _sys_goldengate_dma_clear_desc(lchip, p_dma_chan, sop_index, buf_count);
            buf_count = 0;
            need_eop = 0;
        }

        /* Cannot get SOP, means no SOP packet error, just clear desc*/
        if (0 == buf_count)
        {
            if (0 == is_sop)
            {
                SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,
                                "[DMA] PKT RX error, lchip %d chan %d index %d first is not SOP\n", lchip, chan, cur_index);
                _sys_goldengate_dma_clear_desc(lchip, p_dma_chan, cur_index, 1);
                goto error;
            }
        }

        if (is_sop)
        {
            sop_index = cur_index;
            p_pkt_rx->pkt_len = 0;
            need_eop = 1;
        }

        COMBINE_64BITS_DATA(p_gg_dma_master[lchip]->dma_high_addr,             \
                        (GetDsDesc(V, memAddr_f, p_desc)<<4), phy_addr);

        /*Max desc num for one packet is 64*/
        if (buf_count < 64)
        {
            p_pkt_rx->pkt_buf[buf_count].data = (uint8*)g_dal_op.phy_to_logic(lchip, phy_addr);
            p_pkt_rx->pkt_buf[buf_count].len = GetDsDesc(V, realSize_f, p_desc);
        }

        /*inval dma before read*/
        if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_inval(lchip, phy_addr, p_pkt_rx->pkt_buf[index].len);
        }
        buf_count++;

        if (is_eop)
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
                            cur_index);

            sys_goldengate_packet_rx(lchip, p_pkt_rx);

            process_cnt += buf_count;
           _sys_goldengate_dma_clear_desc(lchip, p_dma_chan, sop_index, buf_count);

            buf_count = 0;
            need_eop = 0;
        }

error:
        /* one interrupt process desc_depth max, for other channel using same sync channel to be processed in time */
        if ((process_cnt >= p_dma_chan->desc_depth) && (!need_eop))
        {
            cur_index++;
            break;
        }
    }

    p_dma_chan->current_index = (cur_index>=p_dma_chan->desc_depth)?(cur_index%p_dma_chan->desc_depth):cur_index;
    return CTC_E_NONE;

}

#if 0
uint32 learn_cnt = 0;
sal_systime_t tv0;
sal_systime_t tv1;
#endif

/**
@brief  DMA info function process
*/
STATIC int32
_sys_goldengate_dma_info_func(uint8 lchip, uint8 chan, uint8 dmasel)
{
    sys_dma_chan_t* p_dma_chan = NULL;
    sys_dma_desc_t* p_base_desc = NULL;
    DsDesc_m* p_desc = NULL;
    uint32 cur_index = 0;
    uint32 desc_done = 0;
    uint32 process_cnt = 0;
    uint64 phy_addr = 0;
    uint32 real_size = 0;
    uint32 record_size = 0;
    sys_dma_info_t dma_info;
    uint8 index = 0;
    uint32* logic_addr = NULL;
    sys_dma_desc_ts_t mon_ts;
    uint64 mon_ns = 0;
    uint8 need_process = 0;
    uint32 record_index = 0;
    uint32 temp_cnt = 0;
    sys_dma_desc_info_t* p_desc_info = NULL;


    DmaSdcFifo_m* sdc_info = NULL;

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[DMA] ISR TX lchip %d chan %d dmasel:%d\n", lchip, chan, dmasel);

    /* init check */
    SYS_DMA_INIT_CHECK(lchip);

    sal_memset(&dma_info, 0, sizeof(sys_dma_info_t));
    sal_memset(&mon_ts, 0, sizeof(sys_dma_desc_ts_t));

    p_dma_chan = &p_gg_dma_master[lchip]->dma_chan_info[chan];

    p_base_desc = p_dma_chan->p_desc;
    cur_index = p_dma_chan->current_index;
    p_desc_info = p_dma_chan->p_desc_info;

    while(1)
    {
        if (cur_index >= p_gg_dma_master[lchip]->dma_chan_info[chan].desc_depth)
        {
            cur_index = 0;
        }

        temp_cnt = 0;
        p_desc = &(p_base_desc[cur_index].desc_info);
        /*inval dma before read*/
        if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_desc), sizeof(DsDesc_m));
        }

        desc_done = GetDsDesc(V, done_f, p_desc);

        /* get realsize from desc */
        real_size = GetDsDesc(V, realSize_f, p_desc);
        record_index = (p_desc_info)?p_desc_info[cur_index].value0:0;
        record_size = (p_desc_info)?p_desc_info[cur_index].value1:0;

        if (p_dma_chan->req_timeout_en)
        {
            /*Using req timeout check real size*/
            if (real_size > record_size)
            {
                need_process = 1;
            }
            else
            {
                need_process = 0;
            }
        }
        else
        {
            if (1 == desc_done)
            {
                need_process = 1;
            }
            else
            {
                need_process = 0;
            }
        }

        if (!need_process)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "No desc is not done, processed %d desc\n", process_cnt);
            break;
        }

        mon_ts.ts_ns = GetDsDesc(V, timestamp_ns_f, p_desc);
        mon_ts.ts_s = GetDsDesc(V, timestamp_s_f, p_desc);

        COMBINE_64BITS_DATA(p_gg_dma_master[lchip]->dma_high_addr,             \
                            (GetDsDesc(V, memAddr_f, p_desc)<<4), phy_addr);

        logic_addr = g_dal_op.phy_to_logic(lchip, phy_addr);

        if (real_size > p_dma_chan->cfg_size)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Info realsize error real:%d cfg:%d\n", real_size, p_dma_chan->cfg_size);
            return CTC_E_EXCEED_MIN_SIZE;
        }

        switch(p_dma_chan->func_type)
        {
            case  CTC_DMA_FUNC_HW_LEARNING:
#if 0
if (learn_cnt == 0)
{
    sal_gettime(&tv0);
}
#endif
                for (index = 0; index < (real_size-record_size)/SYS_DMA_LEARN_AGING_SYNC_CNT; index++)
                {
                    dma_info.entry_num = SYS_DMA_LEARN_AGING_SYNC_CNT;
                    dma_info.p_data = (void*)((sys_dma_learning_info_t*)logic_addr + record_index + index*SYS_DMA_LEARN_AGING_SYNC_CNT);
 /*learn_cnt += SYS_DMA_LEARN_AGING_SYNC_CNT;*/
                    process_cnt += SYS_DMA_LEARN_AGING_SYNC_CNT;

                    if ( p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_LERNING])
                    {
                        p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_LERNING](lchip, (void*)&dma_info);
                    }
                }

                if ((real_size-record_size)%SYS_DMA_LEARN_AGING_SYNC_CNT)
                {
                    dma_info.entry_num = (real_size-record_size)%SYS_DMA_LEARN_AGING_SYNC_CNT;
                    temp_cnt = (real_size-record_size)%SYS_DMA_LEARN_AGING_SYNC_CNT;
                    dma_info.p_data = (void*)((sys_dma_learning_info_t*)logic_addr + record_index + index*SYS_DMA_LEARN_AGING_SYNC_CNT);

 /*learn_cnt += dma_info.entry_num;*/

                    process_cnt += dma_info.entry_num;

                    if ( p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_LERNING])
                    {
                        p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_LERNING](lchip, (void*)&dma_info);
                    }
                }

                record_index +=  (temp_cnt + index*SYS_DMA_LEARN_AGING_SYNC_CNT);
                if (p_desc_info)
                {
                    p_desc_info[cur_index].value0 = record_index;
                }

                break;

            case  CTC_DMA_FUNC_IPFIX:
                for (index = 0; index < (real_size-record_size)/SYS_DMA_IPFIX_SYNC_CNT; index++)
                {
                    dma_info.entry_num = SYS_DMA_IPFIX_SYNC_CNT;
                    dma_info.p_data = (void*)((DmaIpfixAccFifo_m*)logic_addr + record_index +index*SYS_DMA_IPFIX_SYNC_CNT);

                    process_cnt += SYS_DMA_IPFIX_SYNC_CNT;

                    if ( p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_IPFIX])
                    {
                        p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_IPFIX](lchip, (void*)&dma_info);
                    }
                }

                if ((real_size-record_size)%SYS_DMA_IPFIX_SYNC_CNT)
                {
                    dma_info.entry_num = (real_size-record_size)%SYS_DMA_IPFIX_SYNC_CNT;
                    temp_cnt = (real_size-record_size)%SYS_DMA_LEARN_AGING_SYNC_CNT;
                    dma_info.p_data = (void*)((DmaIpfixAccFifo_m*)logic_addr + record_index + index*SYS_DMA_IPFIX_SYNC_CNT);

                    process_cnt +=  dma_info.entry_num;

                    if ( p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_IPFIX])
                    {
                        p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_IPFIX](lchip, (void*)&dma_info);
                    }
                }

                record_index +=  (temp_cnt + index*SYS_DMA_LEARN_AGING_SYNC_CNT);
                if (p_desc_info)
                {
                    p_desc_info[cur_index].value0 = record_index;
                }
                break;

            case CTC_DMA_FUNC_SDC:
                sdc_info = (DmaSdcFifo_m*)logic_addr;
                SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Stats_ptr:0x%x \n", GetDmaSdcFifo(V,slice0AclSdcPtr_f, sdc_info));
                SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Pkt Len:0x%x \n", GetDmaSdcFifo(V,slice0AclSdcPktLen_f, sdc_info));

                break;
            case CTC_DMA_FUNC_MONITOR:

                mon_ns = mon_ts.ts_s;
                mon_ns = mon_ns*1000000000+mon_ts.ts_ns;

                for (index = 0; index < real_size/SYS_DMA_MONITOR_SYNC_CNT; index++)
                {
                    dma_info.entry_num = SYS_DMA_MONITOR_SYNC_CNT;
                    dma_info.p_ext = (void*)&mon_ns;
                    dma_info.p_data = (void*)((DmaActMonFifo_m*)logic_addr + (index)*SYS_DMA_MONITOR_SYNC_CNT);
                    process_cnt +=  SYS_DMA_MONITOR_SYNC_CNT;

                    if ( p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_MONITOR])
                    {
                        p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_MONITOR](lchip, (void*)&dma_info);
                    }
                }

                if (real_size%SYS_DMA_MONITOR_SYNC_CNT)
                {
                    dma_info.entry_num = real_size%SYS_DMA_MONITOR_SYNC_CNT;
                    dma_info.p_ext = (void*)&mon_ns;
                    dma_info.p_data = (void*)((DmaActMonFifo_m*)logic_addr + (index)*SYS_DMA_MONITOR_SYNC_CNT);
                    process_cnt +=  dma_info.entry_num;

                    if ( p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_MONITOR])
                    {
                        p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_MONITOR](lchip, (void*)&dma_info);
                    }
                }

                break;
            default:
                break;
        }

#if 0
if (learn_cnt == 49152)
{
sal_gettime(&tv1);
}
#endif
        if ((desc_done) || (real_size == p_dma_chan->cfg_size))
        {
            _sys_goldengate_dma_clear_desc(lchip, p_dma_chan, cur_index, 1);
            if (p_desc_info)
            {
                p_desc_info[cur_index].value1 = 0;
                p_desc_info[cur_index].value0 = 0;
            }
            cur_index++;
        }
        else
        {
            if (p_desc_info)
            {
                p_desc_info[cur_index].value1 = real_size;  /*record size*/
            }
        }

        /* one interrupt process 1000 entry, for other channel using same sync channel to be processed in time */
        if (process_cnt >= 1000)
        {
            break;
        }
    }

    p_dma_chan->current_index = (cur_index>=p_dma_chan->desc_depth)?(cur_index%p_dma_chan->desc_depth):cur_index;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_dma_get_mac_info_with_mac_index(uint8 lchip, uint8 mac_index, uint8* mac_id, uint8* mac_type)
{
    uint8 cg_mac_id[4] = {36, 52, 100, 116};

    if (mac_index < 40)
    {
        *mac_id = mac_index;
        *mac_type = CTC_PORT_SPEED_1G;
    }
    else if ((mac_index<48) && (mac_index>=40))
    {
        *mac_id = mac_index+8;
        *mac_type = CTC_PORT_SPEED_1G;
    }
    else if ((mac_index<88) && (mac_index>=48))
    {
        *mac_id = mac_index+16;
        *mac_type = CTC_PORT_SPEED_1G;
    }
    else if ((mac_index<96) && (mac_index>=88))
    {
        *mac_id = mac_index+24;
        *mac_type = CTC_PORT_SPEED_1G;
    }
    else
    {
        *mac_id = cg_mac_id[mac_index-96];
        *mac_type = CTC_PORT_SPEED_100G;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_dma_mac_is_valid(uint8 lchip, uint8 mac_id, uint8 mac_type)
{
    uint16 lport = 0;
    uint8 mac_type_real = 0;
    sys_datapath_lport_attr_t*  p_port_attr = NULL;
    lport = sys_goldengate_common_get_lport_with_mac(lchip, mac_id);
    if (SYS_COMMON_USELESS_MAC == lport)
    {
        return CTC_E_MAC_NOT_USED;
    }
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &p_port_attr));
    if (p_port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    mac_type_real = sys_goldengate_common_get_mac_type(lchip, mac_id);
    if (((mac_type_real != CTC_PORT_SPEED_100G) && (mac_type == CTC_PORT_SPEED_100G))
        || ((mac_type_real == CTC_PORT_SPEED_100G) && (mac_type != CTC_PORT_SPEED_100G)))
    {
        return CTC_E_MAC_NOT_USED;
    }

    return CTC_E_NONE;
}

/**
@brief DMA stats function process
*/
int32
_sys_goldengate_dma_stats_func(uint8 lchip, uint8 chan, uint8 dmasel)
{
    sys_dma_chan_t* p_dma_chan = NULL;
    sys_dma_desc_t* p_base_desc = NULL;
    DsDesc_m* p_desc = NULL;
    uint32 cur_index = 0;
    uint32 desc_done = 0;
    uint32 process_cnt = 0;
    uint64 phy_addr = 0;
    uint32 real_size = 0;
    uint32* logic_addr = NULL;
    uint8 mac_id = 0;
    uint8 mac_type = 0;
    sys_dma_reg_t dma_reg;
    uint8 slice_id = 0;
    int32 ret = CTC_E_NONE;
    sys_dma_desc_info_t* p_desc_info = NULL;

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[DMA] ISR TX lchip %d chan %d dmasel:%d\n", lchip, chan, dmasel);

    /* init check */
    SYS_DMA_INIT_CHECK(lchip);

    p_dma_chan = &p_gg_dma_master[lchip]->dma_chan_info[chan];

    p_base_desc = p_dma_chan->p_desc;
    p_desc_info = p_dma_chan->p_desc_info;

    /* gg dma stats process desc from index 0 to max_desc*/
    for (cur_index = p_dma_chan->current_index; cur_index < p_dma_chan->desc_depth; cur_index++)
    {
        p_desc = &(p_base_desc[cur_index].desc_info);
        /*inval dma before read*/
        if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
        {
            g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_desc), sizeof(DsDesc_m));
        }
        desc_done = GetDsDesc(V, done_f, p_desc);

        if (0 == desc_done)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "No desc is not done, processed %d desc\n", process_cnt);
            break;
        }

        process_cnt++;

        if (chan == SYS_DMA_PORT_STATS_CHAN_ID)
        {
            if (!p_desc_info)
            {
                SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "desc info not init, cannot get mac id\n");
                break;
            }
            mac_id = p_desc_info[cur_index].value0;
            mac_type = (p_desc_info[cur_index].value0>>8)&0x00FF;
            ret = _sys_goldengate_dma_mac_is_valid(lchip, mac_id, mac_type);
            if (ret)
            {
                _sys_goldengate_dma_clear_desc(lchip, p_dma_chan, cur_index, 1);
                continue;
            }
        }

        COMBINE_64BITS_DATA(p_gg_dma_master[lchip]->dma_high_addr,             \
                            (GetDsDesc(V, memAddr_f, p_desc)<<4), phy_addr);

        logic_addr = g_dal_op.phy_to_logic(lchip, phy_addr);
        dma_reg.p_data = logic_addr;

        /* get realsize from desc */
        real_size = GetDsDesc(V, realSize_f, p_desc);
        if (real_size != p_dma_chan->cfg_size)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "stats realsize error real:%d cfg:%d\n", real_size, p_dma_chan->cfg_size);
             /*return CTC_E_EXCEED_MIN_SIZE;*/
        }

        real_size = GetDsDesc(V, realSize_f, p_desc);
        if (real_size != p_dma_chan->cfg_size)
        {
            SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ag stats realsize error real:%d cfg:%d\n", real_size, p_dma_chan->cfg_size);
            return CTC_E_EXCEED_MIN_SIZE;
        }

        if (chan == SYS_DMA_PORT_STATS_CHAN_ID)
        {
            dma_reg.p_ext = &mac_id;
            if ( p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_PORT_STATS])
            {
                p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_PORT_STATS](lchip, (void*)&dma_reg);
            }
        }
        else if (chan == SYS_DMA_PKT_STATS_CHAN_ID)
        {
            _sys_goldengate_dma_sync_pkt_stats(lchip, logic_addr);
        }
        else
        {
            slice_id = (cur_index%2);
            dma_reg.p_ext = &slice_id;
            if ( p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_QUEUE_STATS])
            {
                p_gg_dma_master[lchip]->dma_cb[SYS_DMA_CB_TYPE_QUEUE_STATS](lchip, (void*)&dma_reg);
            }
        }

        _sys_goldengate_dma_clear_desc(lchip, p_dma_chan, cur_index, 1);

    }

    p_dma_chan->current_index = ((p_dma_chan->current_index + process_cnt) % (p_dma_chan->desc_depth));


    return CTC_E_NONE;
}

/**
@brief DMA packet rx thread for packet rx channel
*/
STATIC void
_sys_goldengate_dma_pkt_rx_thread(void* param)
{
    sys_dma_thread_t* p_thread_info = (sys_dma_thread_t*)param;
    int32 prio = 0;
    int32 ret = 0;
    uint8 dmasel = 0;
    sys_intr_type_t type;
    uint8 act_chan = 0;
    sys_dma_chan_t* p_dma_chan = NULL;
    DsDesc_m* p_desc = NULL;
    uint32 cur_index = 0;
    uint8 index = 0;
    uint8 lchip = 0;

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    prio = p_thread_info->prio;
    lchip = p_thread_info->lchip;
    sal_task_set_priority(prio);

    while (1)
    {
        ret = sal_sem_take(p_thread_info->p_sync_sem, SAL_SEM_FOREVER);
        if (0 != ret)
        {
            continue;
        }
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);


        /* scan all channel using same sync channel, process one by one */
        for(index = 0; index < p_thread_info->chan_num; index++)
        {
            act_chan = p_thread_info->chan_id[index];

            /* check channel is enable or not */
            if (p_gg_dma_master[lchip]->dma_chan_info[act_chan].chan_en == 0)
            {
                continue;
            }

            dmasel = p_gg_dma_master[lchip]->dma_chan_info[act_chan].dmasel;
            p_dma_chan = &p_gg_dma_master[lchip]->dma_chan_info[act_chan];
            cur_index = p_dma_chan->current_index;
            p_desc = &(p_dma_chan->p_desc[cur_index].desc_info);
            type.intr = SYS_INTR_GG_DMA_0+dmasel;
            type.sub_intr = SYS_INTR_GG_SUB_DMA_FUNC_CHAN_0+act_chan;
            /*inval dma before read*/
            if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
            {
                g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)p_desc), sizeof(DsDesc_m));
            }

            if (GetDsDesc(V, done_f, p_desc) == 0)
            {
                /* cur channel have no desc to process */
                /* just release mask channel isr */
                SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Interrupt Occur, But no desc need process!!!chan:%d \n", act_chan);
                sys_goldengate_interrupt_set_en_internal(lchip, &type, TRUE);
                continue;
            }

            p_dma_chan->in_process = 1;

            _sys_goldengate_dma_pkt_rx_func(lchip, act_chan, dmasel);

            /* release mask channel isr */
            sys_goldengate_interrupt_set_en_internal(lchip, &type, TRUE);

            p_dma_chan->in_process = 0;

        }
        if (p_gg_dma_master[lchip]->dma_stats.rx_sync_cnt > 1000)
        {
            sys_goldengate_dma_sync_pkt_rx_stats(lchip, dmasel);
            p_gg_dma_master[lchip]->dma_stats.rx_sync_cnt = 0;
        }
        else
        {
            p_gg_dma_master[lchip]->dma_stats.rx_sync_cnt++;
        }

    }
    return;
}

/**
@brief DMA stats thread for stats channel
*/
STATIC void
_sys_goldengate_dma_stats_thread(void* param)
{
    int32 prio = 0;
    int32 ret = 0;
    uint8 dmasel = 0;
    uint8 act_chan = 0;
    sys_intr_type_t type;
    sys_dma_thread_t* p_thread_info = (sys_dma_thread_t*)param;
    uint8 index = 0;
    uint8 lchip = 0;

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    prio = p_thread_info->prio;
    lchip = p_thread_info->lchip;

    sal_task_set_priority(prio);

    while (1)
    {
        ret = sal_sem_take(p_thread_info->p_sync_sem, SAL_SEM_FOREVER);
        if (0 != ret)
        {
            continue;
        }
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);

        /* scan all channel using same sync channel, process one by one */
        for(index = 0; index < p_thread_info->chan_num; index++)
        {
            act_chan = p_thread_info->chan_id[index];

            /* check channel is enable or not */
            if (p_gg_dma_master[lchip]->dma_chan_info[act_chan].chan_en == 0)
            {
                continue;
            }

            dmasel = p_gg_dma_master[lchip]->dma_chan_info[act_chan].dmasel;

            /* interrupt should be sync channel interrupt */
            type.intr = SYS_INTR_GG_DMA_0+dmasel;
            type.sub_intr = SYS_INTR_GG_SUB_DMA_FUNC_CHAN_0+act_chan;

            _sys_goldengate_dma_stats_func(lchip, act_chan, dmasel);

            /* release mask channel isr */
            sys_goldengate_interrupt_set_en_internal(lchip, &type, TRUE);

        }

    }

    return;
}

STATIC void
_sys_goldengate_dma_info_thread(void* param)
{
    int32 prio = 0;
    int32 ret = 0;
    sys_dma_thread_t* p_thread_info = (sys_dma_thread_t*)param;
    uint8 index = 0;
    uint8 dmasel = 0;
    sys_intr_type_t type;
    uint8 act_chan = 0;
    sys_dma_chan_t* p_dma_chan = NULL;
    /*DsDesc_m* p_desc = NULL;*/
    /*uint32 cur_index = 0;*/
    uint32 cmd = 0;
    DmaCtlIntrFunc0_m intr_ctl;
    uint8 lchip = 0;
    uint32 tbl_id = 0;

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    prio = p_thread_info->prio;
    lchip = p_thread_info->lchip;

    sal_task_set_priority(prio);

    while (1)
    {
        ret = sal_sem_take(p_thread_info->p_sync_sem, SAL_SEM_FOREVER);
        if (0 != ret)
        {
            continue;
        }
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);

        /* scan all channel using same sync channel, process one by one */
        for(index = 0; index < p_thread_info->chan_num; index++)
        {
            act_chan = p_thread_info->chan_id[index];

            /* check channel is enable or not */
            if (p_gg_dma_master[lchip]->dma_chan_info[act_chan].chan_en == 0)
            {
                continue;
            }

            dmasel = p_gg_dma_master[lchip]->dma_chan_info[act_chan].dmasel;

            p_dma_chan = &p_gg_dma_master[lchip]->dma_chan_info[act_chan];
            /*cur_index = p_dma_chan->current_index;*/
            /*p_desc = &(p_dma_chan->p_desc[cur_index].desc_info);*/

            /* interrupt should be sync channel interrupt */
            type.intr = SYS_INTR_GG_DMA_0+dmasel;
            type.sub_intr = SYS_INTR_GG_SUB_DMA_FUNC_CHAN_0+act_chan;

#if 0
            if (GetDsDesc(V, done_f, p_desc) == 0)
            {
                SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Interrupt Occur, But no desc need process!!!chan:%d \n", act_chan);
                /* cur channel have no desc to process */
                /* just release mask channel isr, process next channel */
                sys_goldengate_interrupt_set_en_internal(lchip, 0, &type, TRUE);
                continue;
            }
#endif
            _sys_goldengate_dma_info_func(lchip, act_chan, dmasel);

            /* release mask channel isr */
            sys_goldengate_interrupt_set_en_internal(lchip, &type, TRUE);

            /*inval dma before read*/
            if ((NULL != g_dal_op.dma_cache_inval) && (NULL != g_dal_op.logic_to_phy))
            {
                g_dal_op.dma_cache_inval(lchip, g_dal_op.logic_to_phy(lchip, (void*)&(p_dma_chan->p_desc[p_dma_chan->current_index].desc_info)), sizeof(DsDesc_m));
            }
            if (GetDsDesc(V, done_f, &(p_dma_chan->p_desc[p_dma_chan->current_index].desc_info)))
            {
                SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Dma Trigger interrupt chan:%d \n", act_chan);
                tbl_id = DmaCtlIntrFunc0_t + dmasel;
                cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
                (DRV_IOCTL(lchip, 0, cmd, &intr_ctl));
                SetDmaCtlIntrFunc0(V, dmaIntrValidVec_f, &intr_ctl, (1<<act_chan));
                cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
                (DRV_IOCTL(lchip, 0, cmd, &intr_ctl));
            }
        }
    }

    return;
}

/**
@brief DMA function interrupt serve routing
*/
int32
sys_goldengate_dma_isr_func(uint8 lchip, uint32 intr, void* p_data)
{
    uint8 chan = 0;
    uint8 sync_chan = 0;
    uint32* p_dma_func = (uint32*)p_data;
    sys_intr_type_t type;
    sys_dma_thread_t* p_thread_info = NULL;

    SYS_DMA_INIT_CHECK(lchip);
    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[DMA Ctl%d] Func ISR lchip %d intr %d sub_intr 0x%08X\n",
                    (intr==SYS_INTR_GG_DMA_0)?0:1, lchip, intr, p_dma_func[0]);

    for (chan = 0; chan < SYS_INTR_GG_SUB_DMA_FUNC_MAX; chan++)
    {
        if (CTC_BMP_ISSET(p_dma_func, chan))
        {
            type.intr = SYS_INTR_GG_DMA_0 + p_gg_dma_master[lchip]->dma_chan_info[chan].dmasel;
            type.sub_intr = SYS_INTR_GG_SUB_DMA_FUNC_CHAN_0+chan;

            sync_chan = p_gg_dma_master[lchip]->dma_chan_info[chan].sync_chan;
            sys_goldengate_interrupt_set_en_internal(lchip, &type, FALSE);

            p_thread_info = ctc_vector_get(p_gg_dma_master[lchip]->p_thread_vector, sync_chan);
            if (!p_thread_info)
            {
                /*means no need to create sync thread*/
                SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Dma init wrong please check!!chan:%d \n", chan);
                continue;
            }

            /* push sync sem */
            CTC_ERROR_RETURN(sal_sem_give(p_thread_info->p_sync_sem));
        }
    }

    return CTC_E_NONE;
}


#define _DMA_INIT_BEGIN

/**
@brief DMA sync mechanism init for function interrupt
*/
int32
_sys_goldengate_dma_sync_init(uint8 lchip, uintptr chan)
{
    int32 ret = 0;
    int32 prio = 0;
    sys_dma_thread_t* p_thread_info = NULL;
    char buffer[SAL_TASK_MAX_NAME_LEN]={0};

    switch (chan)
    {
        case SYS_DMA_PACKET_RX0_CHAN_ID:
        case SYS_DMA_PACKET_RX1_CHAN_ID:
        case SYS_DMA_PACKET_RX2_CHAN_ID:
        case SYS_DMA_PACKET_RX3_CHAN_ID:

            prio = p_gg_dma_master[lchip]->dma_thread_pri[chan];

            p_thread_info = ctc_vector_get(p_gg_dma_master[lchip]->p_thread_vector, chan);
            if (!p_thread_info)
            {
                /*means no need to create sync thread*/
                return CTC_E_NONE;
            }
            else
            {
    	        sal_sprintf(buffer, "ctcPktRx%d-%d", (uint8)chan, lchip);

                ret = sal_task_create(&p_thread_info->p_sync_task, buffer,
                                      SAL_DEF_TASK_STACK_SIZE, prio, _sys_goldengate_dma_pkt_rx_thread, (void*)p_thread_info);
                if (ret < 0)
                {
                    return CTC_E_NOT_INIT;
                }

                sal_memcpy(p_thread_info->desc, buffer, 32 * sizeof(char));
            }
            break;

        case SYS_DMA_PACKET_TX0_CHAN_ID:
        case SYS_DMA_PACKET_TX1_CHAN_ID:
            /*no need TODO */
        case SYS_DMA_TBL_WR_CHAN_ID:
            /*no need TODO */
        case SYS_DMA_TBL_RD_CHAN_ID:
            /*no need TODO */
            break;

        case SYS_DMA_PORT_STATS_CHAN_ID:
        case SYS_DMA_PKT_STATS_CHAN_ID:
        case SYS_DMA_REG_MAX_CHAN_ID:

            prio = p_gg_dma_master[lchip]->dma_thread_pri[chan];
            p_thread_info = ctc_vector_get(p_gg_dma_master[lchip]->p_thread_vector, chan);
            if (!p_thread_info)
            {
                /*means no need to create sync thread*/
                return CTC_E_NONE;
            }
            else
            {
    	        sal_sprintf(buffer, "DmaStats%d-%d", (uint8)(chan-SYS_DMA_PORT_STATS_CHAN_ID), lchip);

                /* create dma stats thread */
                ret = sal_task_create(&p_thread_info->p_sync_task, buffer,
                                      SAL_DEF_TASK_STACK_SIZE, prio, _sys_goldengate_dma_stats_thread, (void*)p_thread_info);
                if (ret < 0)
                {
                    return CTC_E_NOT_INIT;
                }

                sal_memcpy(p_thread_info->desc, buffer, 32 * sizeof(char));
            }
            break;

    case SYS_DMA_HASHKEY_CHAN_ID:
            break;

    case SYS_DMA_LEARNING_CHAN_ID:
    case SYS_DMA_IPFIX_CHAN_ID:
    case SYS_DMA_SDC_CHAN_ID:
    case SYS_DMA_MONITOR_CHAN_ID:
        prio = p_gg_dma_master[lchip]->dma_thread_pri[chan];
        p_thread_info = ctc_vector_get(p_gg_dma_master[lchip]->p_thread_vector, chan);
        if (!p_thread_info)
        {
            /*means no need to create sync thread*/
            return CTC_E_NONE;
        }
        else
        {
            sal_sprintf(buffer, "DmaInfo%d-%d", (uint8)(chan-SYS_DMA_LEARNING_CHAN_ID), lchip);

            /* create dma learning thread */
            ret = sal_task_create(&p_thread_info->p_sync_task, buffer,
                                  SAL_DEF_TASK_STACK_SIZE, prio, _sys_goldengate_dma_info_thread, (void*)p_thread_info);
            if (ret < 0)
            {
                return CTC_E_NOT_INIT;
            }

            sal_memcpy(p_thread_info->desc, buffer, 32 * sizeof(char));
        }
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/**
 @brief free memory resource when dma init failed
*/
void
_sys_goldengate_dma_reclaim_resource(uint8 lchip)
{
    /* note: reclaim for dma need dal provide interface to free kernel memory */
    /* dma using seperate memory space, so need not free by g_dal_op.dma_free, it is useless */

    sys_dma_chan_t* p_dma_chan = NULL;
    uint32  desc_index = 0;
    uint32  dma_chan_index = 0;
    void* logic_addr = 0;
    uint32 low_phy_addr = 0;
    uint64 phy_addr = 0;
    DsDesc_m* p_desc = NULL;

    for (dma_chan_index = 0; dma_chan_index < SYS_DMA_MAX_CHAN_NUM; dma_chan_index++)
    {
        p_dma_chan = &p_gg_dma_master[lchip]->dma_chan_info[dma_chan_index];
        if (0 == p_dma_chan->chan_en)
        {
            continue;
        }

        if ((p_dma_chan->channel_id == SYS_DMA_PACKET_TX0_CHAN_ID) || (p_dma_chan->channel_id == SYS_DMA_PACKET_TX1_CHAN_ID))
        {
            mem_free(p_dma_chan->p_desc_used);
        }

        if (p_dma_chan->data_size)
        {
            for (desc_index = 0; desc_index < p_dma_chan->desc_num; desc_index++)
            {
                p_desc = &p_dma_chan->p_desc[desc_index].desc_info;
                low_phy_addr = GetDsDesc(V, memAddr_f, p_desc);
                COMBINE_64BITS_DATA(p_gg_dma_master[lchip]->dma_high_addr, low_phy_addr<<4, phy_addr);
                logic_addr = (void*)g_dal_op.phy_to_logic(lchip, phy_addr);
                if (NULL != logic_addr)
                {
                    g_dal_op.dma_free(lchip, (void*)logic_addr);
                }
            }
        }
        if (NULL != p_dma_chan->p_desc)
        {
            g_dal_op.dma_free(lchip, p_dma_chan->p_desc);
            p_dma_chan->p_desc = NULL;
        }
        if(p_dma_chan->p_desc_info)
        {
            mem_free(p_dma_chan->p_desc_info);
        }
    }

    return;
}

/**
@brief DMA init for pkt dma
*/
STATIC int32
_sys_goldengate_dma_pkt_init(uint8 lchip, sys_dma_chan_t* p_chan_info)
{
    uint32 desc_num = 0;
    sys_dma_desc_t* p_sys_desc_pad = NULL;
    DsDesc_m* p_desc = NULL;
    uint32 phy_addr = 0;
    DmaStaticInfo0_m static_info;
    DmaCtlTab0_m  tab_ctl;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_id = 0;
    void*  p_mem_addr = NULL;
    DmaWeightCfg0_m dma_weight;
    uint32 valid_cnt = 0;
    uint8 ret;

    CTC_PTR_VALID_CHECK(p_chan_info);

    desc_num  = p_chan_info->desc_depth;
    /* cfg desc num */
    p_sys_desc_pad = (sys_dma_desc_t*)g_dal_op.dma_alloc(lchip, (desc_num) * sizeof(sys_dma_desc_t), 0);
    if (NULL == p_sys_desc_pad)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_sys_desc_pad, 0, sizeof(sys_dma_desc_t)*desc_num);

    /* cfg per desc data */
    if (p_chan_info->data_size)
    {
        for (desc_num = 0; desc_num < p_chan_info->desc_num; desc_num++)
        {
            p_desc = (DsDesc_m*)&(p_sys_desc_pad[desc_num].desc_info);

            p_mem_addr = g_dal_op.dma_alloc(lchip, p_chan_info->data_size, 0);
            if (NULL == p_mem_addr)
            {
                return CTC_E_NO_MEMORY;
            }

            sal_memset(p_mem_addr, 0, p_chan_info->data_size);

            phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_mem_addr);
            SetDsDesc(V, memAddr_f, p_desc, (phy_addr >> 4));
            SetDsDesc(V, cfgSize_f, p_desc, p_chan_info->cfg_size);
        }
    }

    /* channel mutex, only use fot DMA Tx */
#ifndef PACKET_TX_USE_SPINLOCK
    ret = sal_mutex_create(&(p_chan_info->p_mutex));
    if (ret || !(p_chan_info->p_mutex))
    {
        return CTC_E_NO_RESOURCE;
    }
#else
    ret = sal_spinlock_create((sal_spinlock_t**)&(p_chan_info->p_mutex));
    if (ret || !(p_chan_info->p_mutex))
    {
        return CTC_E_NO_RESOURCE;
    }
#endif

    /* cfg static infor for dmc channel:MemBase, ring depth */
    sal_memset(&static_info, 0, sizeof(DmaStaticInfo0_m));
    phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_sys_desc_pad);
    tbl_id = DmaStaticInfo0_t + p_chan_info->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &static_info));
    SetDmaStaticInfo0(V, highBase_f, &static_info, p_gg_dma_master[lchip]->dma_high_addr);
    SetDmaStaticInfo0(V, ringBase_f, &static_info, (phy_addr >> 4));
    SetDmaStaticInfo0(V, ringDepth_f, &static_info, p_chan_info->desc_depth);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &static_info));

    /* cfg DmaCtlTab for VldNum */
    if ((p_chan_info->channel_id == SYS_DMA_PACKET_TX0_CHAN_ID) || (p_chan_info->channel_id == SYS_DMA_PACKET_TX1_CHAN_ID))
    {
        valid_cnt = 0;
    }
    else
    {
        valid_cnt = p_chan_info->desc_num;
    }

    sal_memset(&tab_ctl, 0, sizeof(DmaCtlTab0_m));
    tbl_id = DmaCtlTab0_t + p_chan_info->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &tab_ctl));

    SetDmaCtlTab0(V, vldNum_f, &tab_ctl, valid_cnt);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &tab_ctl));

    /* cfg weight */
    sal_memset(&dma_weight, 0, sizeof(DmaWeightCfg0_m));
    tbl_id = DmaWeightCfg0_t + p_chan_info->dmasel;
    field_id = DmaWeightCfg0_cfgChan0Weight_f + p_chan_info->channel_id;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_weight));
    DRV_SET_FIELD_V(tbl_id, field_id, &dma_weight, (p_chan_info->weight));
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_weight));

    if ((p_chan_info->channel_id == SYS_DMA_PACKET_TX0_CHAN_ID) || (p_chan_info->channel_id == SYS_DMA_PACKET_TX1_CHAN_ID))
    {
        /*packet tx need allocate memory for record desc used state */
        p_chan_info->p_desc_used = (uint8*)mem_malloc(MEM_DMA_MODULE, p_chan_info->desc_num);
        if (NULL == p_chan_info->p_desc_used)
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_chan_info->p_desc_used, 0, p_chan_info->desc_num);
    }

    p_gg_dma_master[lchip]->dma_chan_info[p_chan_info->channel_id].p_desc = p_sys_desc_pad;
    p_gg_dma_master[lchip]->dma_chan_info[p_chan_info->channel_id].mem_base = (uintptr)p_sys_desc_pad;
    p_gg_dma_master[lchip]->dma_chan_info[p_chan_info->channel_id].p_desc_used = p_chan_info->p_desc_used;
    p_gg_dma_master[lchip]->dma_chan_info[p_chan_info->channel_id].p_mutex = p_chan_info->p_mutex;

    return CTC_E_NONE;
}

/**
@brief DMA init for reg dma
*/
STATIC int32
_sys_goldengate_dma_reg_init(uint8 lchip, sys_dma_chan_t* p_chan_info)
{
    uint32 desc_num = 0;
    sys_dma_desc_t* p_sys_desc_pad = NULL;
    DsDesc_m* p_desc = NULL;
    uint32 phy_addr = 0;
    DmaStaticInfo0_m static_info;
    DmaCtlTab0_m  tab_ctl;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_id = 0;
    void*  p_mem_addr = NULL;
    DmaWeightCfg0_m dma_weight;
    uint32 valid_cnt = 0;
    uint8 mac_index = 0;
    uint8 mac_id = 0;
    uint8 mac_type = 0;
    uint32 cfg_addr = 0;
    DmaPktStatsCfg0_m stats_cfg;
    uint8 index = 0;
    int32 ret = 0;
    sys_dma_desc_info_t* p_sys_desc_info = NULL;

    CTC_PTR_VALID_CHECK(p_chan_info);

    desc_num  = p_chan_info->desc_depth;

    /* cfg desc num */
    p_sys_desc_pad = (sys_dma_desc_t*)g_dal_op.dma_alloc(lchip, (desc_num) * sizeof(sys_dma_desc_t), 0);
    if (NULL == p_sys_desc_pad)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_sys_desc_pad, 0, sizeof(sys_dma_desc_t)*desc_num);
    if (p_chan_info->channel_id == SYS_DMA_PORT_STATS_CHAN_ID)
    {
        p_sys_desc_info = (sys_dma_desc_info_t*)mem_malloc(MEM_DMA_MODULE, (desc_num)*sizeof(sys_dma_desc_info_t));
        if (!p_sys_desc_info)
        {
            g_dal_op.dma_free(lchip, p_sys_desc_pad);
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_sys_desc_info, 0, sizeof(sys_dma_desc_info_t)*desc_num);
        for (mac_index = 0; mac_index < SYS_GOLDENGATE_DMA_MAC_STATS_NUM; mac_index++)
        {
            /* TODO all mac valid */

            _sys_goldengate_dma_get_mac_info_with_mac_index(lchip, mac_index, &mac_id, &mac_type);

            p_sys_desc_info[mac_index].value0 = mac_id|(mac_type<<8);
            p_desc = (DsDesc_m*)&(p_sys_desc_pad[mac_index].desc_info);

            p_mem_addr = g_dal_op.dma_alloc(lchip, p_chan_info->data_size, 0);
            if (NULL == p_mem_addr)
            {
                return CTC_E_NO_MEMORY;
            }

            sal_memset(p_mem_addr, 0, p_chan_info->data_size);

            _sys_goldengate_dma_get_mac_address(lchip, mac_id, mac_type, &cfg_addr);

            phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_mem_addr);
            SetDsDesc(V, memAddr_f, p_desc, (phy_addr >> 4));
            SetDsDesc(V, cfgSize_f, p_desc, p_chan_info->cfg_size);
            SetDsDesc(V, chipAddr_f, p_desc, cfg_addr);
            SetDsDesc(V, dataStruct_f, p_desc, SYS_GOLDENGATE_DMA_STATS_WORD);
            if (mac_index == 0)
            {
                /*first desc should cfg pause*/
                SetDsDesc(V, pause_f, p_desc, 1);
            }
        }
    }
    else if (p_chan_info->channel_id == SYS_DMA_PKT_STATS_CHAN_ID)
    {
        p_desc = (DsDesc_m*)&(p_sys_desc_pad[lchip].desc_info);

        p_mem_addr = g_dal_op.dma_alloc(lchip, p_chan_info->data_size, 0);
        if (NULL == p_mem_addr)
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_mem_addr, 0, p_chan_info->data_size);
        cfg_addr = DMA_PKT_RX_STATS0_ADDR;
        phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_mem_addr);
        SetDsDesc(V, memAddr_f, p_desc, (phy_addr >> 4));
        SetDsDesc(V, cfgSize_f, p_desc, p_chan_info->cfg_size);
        SetDsDesc(V, chipAddr_f, p_desc, cfg_addr);
        SetDsDesc(V, dataStruct_f, p_desc, 12);
        SetDsDesc(V, pause_f, p_desc, 1);
    }
    else if (p_chan_info->channel_id == SYS_DMA_REG_MAX_CHAN_ID)
    {
        for (index = 0; index < p_chan_info->desc_depth; index++)
        {
            p_desc = (DsDesc_m*)&(p_sys_desc_pad[index].desc_info);

            p_mem_addr = g_dal_op.dma_alloc(lchip, p_chan_info->data_size, 0);
            if (NULL == p_mem_addr)
            {
                return CTC_E_NO_MEMORY;
            }

            sal_memset(p_mem_addr, 0, p_chan_info->data_size);
            phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_mem_addr);
            cfg_addr = RA_QUE_CNT_ADDR+(index%2)*0x1000;
            SetDsDesc(V, memAddr_f, p_desc, (phy_addr >> 4));
            SetDsDesc(V, cfgSize_f, p_desc, p_chan_info->cfg_size);
            SetDsDesc(V, chipAddr_f, p_desc, cfg_addr);
            SetDsDesc(V, dataStruct_f, p_desc, 1);
            if ((index%2) == 0)
            {
                /*first desc should cfg pause*/
                SetDsDesc(V, pause_f, p_desc, 1);
            }
        }
    }
    else if (p_chan_info->channel_id == SYS_DMA_TBL_WR_CHAN_ID || p_chan_info->channel_id == SYS_DMA_TBL_RD_CHAN_ID)
    {
        /*for write channel, create mux*/
#ifndef PACKET_TX_USE_SPINLOCK
        ret = sal_mutex_create(&(p_chan_info->p_mutex));
        if (ret || !(p_chan_info->p_mutex))
        {
            return CTC_E_NO_RESOURCE;
        }
#else
        ret = sal_spinlock_create((sal_spinlock_t**)&(p_chan_info->p_mutex));
        if (ret || !(p_chan_info->p_mutex))
        {
            return CTC_E_NO_RESOURCE;
        }
#endif
    }

    /* cfg static infor for dmc channel:MemBase, ring depth */
    sal_memset(&static_info, 0, sizeof(DmaStaticInfo0_m));
    phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_sys_desc_pad);
    tbl_id = DmaStaticInfo0_t + p_chan_info->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &static_info));
    SetDmaStaticInfo0(V, highBase_f, &static_info, p_gg_dma_master[lchip]->dma_high_addr);
    SetDmaStaticInfo0(V, ringBase_f, &static_info, (phy_addr >> 4));
    SetDmaStaticInfo0(V, ringDepth_f, &static_info, p_chan_info->desc_depth);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &static_info));

    /* cfg DmaCtlTab for VldNum */
    valid_cnt = p_chan_info->desc_num;

    sal_memset(&tab_ctl, 0, sizeof(DmaCtlTab0_m));
    tbl_id = DmaCtlTab0_t + p_chan_info->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &tab_ctl));

    SetDmaCtlTab0(V, vldNum_f, &tab_ctl, valid_cnt);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &tab_ctl));

    /* cfg weight */
    sal_memset(&dma_weight, 0, sizeof(DmaWeightCfg0_m));
    tbl_id = DmaWeightCfg0_t + p_chan_info->dmasel;
    field_id = DmaWeightCfg0_cfgChan0Weight_f + p_chan_info->channel_id;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_weight));
    DRV_SET_FIELD_V(tbl_id, field_id, &dma_weight, (p_chan_info->weight));
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_weight));

    /* cfg clear on read */
    tbl_id = DmaPktStatsCfg0_t + p_chan_info->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &stats_cfg));
    SetDmaPktStatsCfg0(V, clearOnRead_f, &stats_cfg, 1);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &stats_cfg));

    p_gg_dma_master[lchip]->dma_chan_info[p_chan_info->channel_id].p_desc = p_sys_desc_pad;
    p_gg_dma_master[lchip]->dma_chan_info[p_chan_info->channel_id].mem_base = (uintptr)p_sys_desc_pad;
    p_gg_dma_master[lchip]->dma_chan_info[p_chan_info->channel_id].p_mutex = p_chan_info->p_mutex;
    p_gg_dma_master[lchip]->dma_chan_info[p_chan_info->channel_id].p_desc_info = p_sys_desc_info;

    return CTC_E_NONE;
}

/**
@brief DMA init for info dma
*/
STATIC int32
_sys_goldengate_dma_info_init(uint8 lchip, sys_dma_chan_t* p_chan_info)
{
    uint32 desc_num = 0;
    uint8 ctl_idx = 0;
    sys_dma_desc_t* p_sys_desc_pad = NULL;
    DsDesc_m* p_desc = NULL;
    uint32 phy_addr = 0;
    DmaStaticInfo0_m static_info;
    DmaCtlTab0_m  tab_ctl;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_id = 0;
    uint32 ctl_sel = 0;
    void*  p_mem_addr = NULL;
    DmaWeightCfg0_m dma_weight;
    DmaMiscCfg0_m misc_cfg;
    uint32 valid_cnt = 0;
    sys_dma_desc_info_t* p_sys_desc_info = NULL;
    ctc_chip_device_info_t dev_info;

    CTC_PTR_VALID_CHECK(p_chan_info);

    desc_num  = p_chan_info->desc_depth;

    /* cfg desc num */
    p_sys_desc_pad = (sys_dma_desc_t*)g_dal_op.dma_alloc(lchip, (desc_num) * sizeof(sys_dma_desc_t), 0);
    if (NULL == p_sys_desc_pad)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));
    sys_goldengate_chip_get_device_info(lchip, &dev_info);
    if(dev_info.version_id == 1)
    {
        p_sys_desc_info = (sys_dma_desc_info_t*)mem_malloc(MEM_DMA_MODULE, (desc_num)*sizeof(sys_dma_desc_info_t));
        if (!p_sys_desc_info)
        {
            g_dal_op.dma_free(lchip, p_sys_desc_pad);
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_sys_desc_info, 0, sizeof(sys_dma_desc_info_t)*desc_num);
    }

    sal_memset(p_sys_desc_pad, 0, sizeof(sys_dma_desc_t)*desc_num);

    /* cfg per desc data */
    for (desc_num = 0; desc_num < p_chan_info->desc_num; desc_num++)
    {
        p_desc = (DsDesc_m*)&(p_sys_desc_pad[desc_num].desc_info);

         p_mem_addr = g_dal_op.dma_alloc(lchip, p_chan_info->data_size, 0);
        if (NULL == p_mem_addr)
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_mem_addr, 0, p_chan_info->data_size);

        phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_mem_addr);
        SetDsDesc(V, memAddr_f, p_desc, (phy_addr >> 4));
        SetDsDesc(V, cfgSize_f, p_desc, p_chan_info->cfg_size);
    }

    /* cfg static infor for dmc channel:MemBase, ring depth */
    sal_memset(&static_info, 0, sizeof(DmaStaticInfo0_m));
    phy_addr = (uint32)g_dal_op.logic_to_phy(lchip, p_sys_desc_pad);
    tbl_id = DmaStaticInfo0_t + p_chan_info->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &static_info));
    SetDmaStaticInfo0(V, highBase_f, &static_info, p_gg_dma_master[lchip]->dma_high_addr);
    SetDmaStaticInfo0(V, ringBase_f, &static_info, (phy_addr >> 4));
    SetDmaStaticInfo0(V, ringDepth_f, &static_info, p_chan_info->desc_depth);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &static_info));

    /* cfg DmaCtlTab for VldNum */
    valid_cnt = p_chan_info->desc_num;

    sal_memset(&tab_ctl, 0, sizeof(DmaCtlTab0_m));
    tbl_id = DmaCtlTab0_t + p_chan_info->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &tab_ctl));

    SetDmaCtlTab0(V, vldNum_f, &tab_ctl, valid_cnt);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &tab_ctl));

    /* cfg weight */
    sal_memset(&dma_weight, 0, sizeof(DmaWeightCfg0_m));
    tbl_id = DmaWeightCfg0_t + p_chan_info->dmasel;
    field_id = DmaWeightCfg0_cfgChan0Weight_f + p_chan_info->channel_id;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_weight));
    DRV_SET_FIELD_V(tbl_id, field_id, &dma_weight, (p_chan_info->weight));
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_weight));

    /* cfg dmactl select for info Dma */
    for (ctl_idx = 0; ctl_idx < SYS_DMA_CTL_USED_NUM; ctl_idx++)
    {
        if (SYS_DMA_IS_INFO_DMA(p_chan_info->channel_id))
        {
            tbl_id = DmaMiscCfg0_t + ctl_idx;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &misc_cfg));

            ctl_sel = GetDmaMiscCfg0(V,cfgDmaIntfSel_f, &misc_cfg);
            if (p_chan_info->dmasel)
            {
                CTC_BIT_SET(ctl_sel, (p_chan_info->channel_id-SYS_DMA_LEARNING_CHAN_ID));
            }
            else
            {
                CTC_BIT_UNSET(ctl_sel, (p_chan_info->channel_id-SYS_DMA_LEARNING_CHAN_ID));
            }
            SetDmaMiscCfg0(V, cfgDmaIntfSel_f, &misc_cfg, ctl_sel);

            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &misc_cfg));
        }
    }

    p_gg_dma_master[lchip]->dma_chan_info[p_chan_info->channel_id].p_desc = p_sys_desc_pad;
    p_gg_dma_master[lchip]->dma_chan_info[p_chan_info->channel_id].mem_base = (uintptr)p_sys_desc_pad;
    p_gg_dma_master[lchip]->dma_chan_info[p_chan_info->channel_id].p_mutex = p_chan_info->p_mutex;
    p_gg_dma_master[lchip]->dma_chan_info[p_chan_info->channel_id].p_desc_info = p_sys_desc_info;

    return CTC_E_NONE;
}

/**
@brief DMA common init
*/
STATIC int32
_sys_goldengate_dma_common_init(uint8 lchip, sys_dma_chan_t* p_chan_info)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    DmaStaticInfo0_m static_info;
    uint32 chan_en = 0;

    /*dma info process*/
    if (SYS_DMA_IS_INFO_DMA(p_chan_info->channel_id))
    {
        CTC_ERROR_RETURN(_sys_goldengate_dma_info_init(lchip, p_chan_info));
        chan_en = 1;
    }

    /*dma packet process*/
    if (SYS_DMA_IS_PKT_DMA(p_chan_info->channel_id))
    {
        CTC_ERROR_RETURN(_sys_goldengate_dma_pkt_init(lchip, p_chan_info));
        chan_en = 1;
    }

    /*dma reg process*/
    if (SYS_DMA_IS_REG_DMA(p_chan_info->channel_id))
    {
        CTC_ERROR_RETURN(_sys_goldengate_dma_reg_init(lchip, p_chan_info));
        chan_en = (SYS_DMA_REG_MAX_CHAN_ID == p_chan_info->channel_id)?0:1;
    }

    /* enable dma channel */
    if (SYS_DMA_PKT_STATS_CHAN_ID == p_chan_info->channel_id)
    {
         /*-return CTC_E_NONE;*/
    }

    tbl_id = DmaStaticInfo0_t + p_chan_info->dmasel;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &static_info));
    SetDmaStaticInfo0(V, chanEn_f, &static_info, chan_en);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chan_info->channel_id, cmd, &static_info));

    return CTC_E_NONE;
}


/**
@brief DMA intr init
   packet rx: per EOP
   Packet tx: no need
   RegRd:  no need
   RegWr: no need
   port stats: desc cnt
   Info: timer + per desc
*/
STATIC int32
_sys_goldengate_dma_intr_init(uint8 lchip, uint8 dma_idx, ctc_dma_global_cfg_t* p_global_cfg)
{
    DmaPktIntrEnCfg0_m pkt_intr;
    DmaRegIntrEnCfg0_m reg_intr;
    DmaPktIntrCntCfg0_m reg_intr_cnt;
    DmaInfoIntrEnCfg0_m info_intr;
    DmaInfoIntrCntCfg0_m intr_cnt;
    uint32 mask_set = 0;
    DmaCtlIntrFunc0_m intr_ctl;
    uint32 tbl_id = 0;
    uint32 cmd = 0;

    /* cfg intr for packet rx, only using slice0 dmactl for dma packet, slice1 bufretr forwarding to dmactl0  */
    sal_memset(&pkt_intr, 0, sizeof(DmaPktIntrEnCfg0_m));
    tbl_id = DmaPktIntrEnCfg0_t + dma_idx;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pkt_intr));
    SetDmaPktIntrEnCfg0(V, cfgPktRx0EopIntrEn_f, &pkt_intr, 1);
    SetDmaPktIntrEnCfg0(V, cfgPktRx1EopIntrEn_f, &pkt_intr, 1);
    SetDmaPktIntrEnCfg0(V, cfgPktRx2EopIntrEn_f, &pkt_intr, 1);
    SetDmaPktIntrEnCfg0(V, cfgPktRx3EopIntrEn_f, &pkt_intr, 1);

    SetDmaPktIntrEnCfg0(V, cfgPktRx0DmaIntrEn_f, &pkt_intr, 1);
    SetDmaPktIntrEnCfg0(V, cfgPktRx1DmaIntrEn_f, &pkt_intr, 1);
    SetDmaPktIntrEnCfg0(V, cfgPktRx2DmaIntrEn_f, &pkt_intr, 1);
    SetDmaPktIntrEnCfg0(V, cfgPktRx3DmaIntrEn_f, &pkt_intr, 1);
    SetDmaPktIntrEnCfg0(V, cfgPktTx0EopIntrEn_f, &pkt_intr, 0);
    SetDmaPktIntrEnCfg0(V, cfgPktTx0DmaIntrEn_f, &pkt_intr, 0);

    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pkt_intr));

    /* cfg intr for Reg Dma(only port stats and dma pkt stats ) */
    sal_memset(&reg_intr, 0, sizeof(DmaRegIntrEnCfg0_m));
    sal_memset(&reg_intr_cnt, 0, sizeof(DmaPktIntrCntCfg0_m));
    tbl_id = DmaRegIntrEnCfg0_t + dma_idx;
    SetDmaRegIntrEnCfg0(V, cfgRegRd0DescIntrEn_f, &reg_intr, 0);
    SetDmaRegIntrEnCfg0(V, cfgRegRd0DmaIntrEn_f, &reg_intr, 0);
    SetDmaRegIntrEnCfg0(V, cfgRegRd1DmaIntrEn_f, &reg_intr, 1);
    SetDmaRegIntrEnCfg0(V, cfgRegRd2DmaIntrEn_f, &reg_intr, 1);
    SetDmaRegIntrEnCfg0(V, cfgRegRd3DmaIntrEn_f, &reg_intr, 1);
    SetDmaRegIntrEnCfg0(V, cfgRegWrDmaIntrEn_f, &reg_intr, 0);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &reg_intr));

    tbl_id = DmaRegRdIntrCntCfg0_t + dma_idx;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &reg_intr_cnt));
    SetDmaRegRdIntrCntCfg0(V, cfgRegRd1IntrCnt_f, &reg_intr_cnt, SYS_GOLDENGATE_DMA_MAC_STATS_NUM);
    SetDmaRegRdIntrCntCfg0(V, cfgRegRd2IntrCnt_f, &reg_intr_cnt, 1);
    SetDmaRegRdIntrCntCfg0(V, cfgRegRd3IntrCnt_f, &reg_intr_cnt, 20);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &reg_intr_cnt));

    /* cfg intr for Info Dma */
    sal_memset(&info_intr, 0, sizeof(DmaInfoIntrEnCfg0_m));
    tbl_id = DmaInfoIntrEnCfg0_t + dma_idx;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &info_intr));
    SetDmaInfoIntrEnCfg0(V, cfgInfo0DescIntrEn_f, &info_intr, 1);
    SetDmaInfoIntrEnCfg0(V, cfgInfo1DescIntrEn_f, &info_intr, 1);
    SetDmaInfoIntrEnCfg0(V, cfgInfo2DescIntrEn_f, &info_intr, 1);
    SetDmaInfoIntrEnCfg0(V, cfgInfo3DescIntrEn_f, &info_intr, 1);
    SetDmaInfoIntrEnCfg0(V, cfgInfo4DescIntrEn_f, &info_intr, 0);

    SetDmaInfoIntrEnCfg0(V, cfgInfo0DmaIntrEn_f, &info_intr, 1);
    SetDmaInfoIntrEnCfg0(V, cfgInfo1DmaIntrEn_f, &info_intr, 0);
    SetDmaInfoIntrEnCfg0(V, cfgInfo2DmaIntrEn_f, &info_intr, 1);
    SetDmaInfoIntrEnCfg0(V, cfgInfo3DmaIntrEn_f, &info_intr, 1);
    SetDmaInfoIntrEnCfg0(V, cfgInfo4DmaIntrEn_f, &info_intr, 1);

    SetDmaInfoIntrEnCfg0(V, cfgInfo0TimerIntrEn_f, &info_intr, 1);
    SetDmaInfoIntrEnCfg0(V, cfgInfo1TimerIntrEn_f, &info_intr, 1);
    SetDmaInfoIntrEnCfg0(V, cfgInfo2TimerIntrEn_f, &info_intr, 1);
    SetDmaInfoIntrEnCfg0(V, cfgInfo3TimerIntrEn_f, &info_intr, 1);
    SetDmaInfoIntrEnCfg0(V, cfgInfo4TimerIntrEn_f, &info_intr, 1);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &info_intr));

    tbl_id = DmaInfoIntrCntCfg0_t + dma_idx;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intr_cnt));
    SetDmaInfoIntrCntCfg0(V, cfgInfo4IntrCnt_f, &intr_cnt, 10);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intr_cnt));

    mask_set = 0xe90f;
    tbl_id = DmaCtlIntrFunc0_t + dma_idx;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 3, cmd, &intr_ctl));
    SetDmaCtlIntrFunc0(V, dmaIntrValidVec_f, &intr_ctl, mask_set);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 3, cmd, &intr_ctl));

    return CTC_E_NONE;
}

/**
@brief DMA timer init only usefull for InfoDma and port stats
  Info0:learning/aging, Info1:HashKey, Info2:Ipfix
  Info3:SDC,
  Info4:Monitor:Do not using timeout interrupt, desc full interrupt
  port stats: reg1
  table read: reg0

*/
STATIC int32
_sys_goldengate_dma_timer_init(uint8 lchip, uint8 dma_idx)
{
    DmaInfoTimerCfg0_m global_timer_ctl;
    DmaInfo0TimerCfg0_m info0_timer;
    DmaInfo1TimerCfg0_m info1_timer;
    DmaInfo4TimerCfg0_m info4_timer;
    DmaInfo2TimerCfg0_m info2_timer;
    DmaRegRd1TrigCfg0_m trigger1_timer;
    DmaRegRd1TrigCfg0_m trigger2_timer;
    DmaRegRd1TrigCfg0_m trigger3_timer;
    DmaRegWrTrigCfg0_m wr_trigger;
    DmaRegTrigEnCfg0_m trigger_ctl;
    DmaInfoThrdCfg0_m thrd_cfg;
    uint32 cmd = 0;
    uint32 tbl_id = 0;

    ctc_chip_device_info_t dev_info;

    sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));

    sys_goldengate_chip_get_device_info(lchip, &dev_info);

    sal_memset(&global_timer_ctl, 0, sizeof(global_timer_ctl));
    sal_memset(&info0_timer, 0, sizeof(info0_timer));
    sal_memset(&info1_timer, 0, sizeof(info1_timer));
    sal_memset(&info4_timer, 0, sizeof(info4_timer));
    sal_memset(&trigger1_timer, 0, sizeof(trigger1_timer));
    sal_memset(&trigger_ctl, 0, sizeof(trigger_ctl));

    tbl_id = DmaInfoThrdCfg0_t + dma_idx;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &thrd_cfg));

    if (dev_info.version_id == 1)
    {
        SetDmaInfoThrdCfg0(V, cfgInfo0DmaThrd_f, &thrd_cfg, 17); /*learning max is 16 */
        SetDmaInfoThrdCfg0(V, cfgInfo2DmaThrd_f, &thrd_cfg, 9); /*ipfix max is 4 */
    }
    else
    {
        SetDmaInfoThrdCfg0(V, cfgInfo0DmaThrd_f, &thrd_cfg, 1); /*learning max is 8 */
        SetDmaInfoThrdCfg0(V, cfgInfo2DmaThrd_f, &thrd_cfg, 1); /*ipfix max is 4 */
    }

    SetDmaInfoThrdCfg0(V, cfgInfo1DmaThrd_f, &thrd_cfg, 1);
    SetDmaInfoThrdCfg0(V, cfgInfo3DmaThrd_f, &thrd_cfg, 1);
    SetDmaInfoThrdCfg0(V, cfgInfo4DmaThrd_f, &thrd_cfg, 1);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &thrd_cfg));

    /* for hashdump dump 1 entry need 16 cycles, cfg timer out is 72k hashram
         16*72*1024*1.67ns = 2ms */
    tbl_id = DmaInfo1TimerCfg0_t + dma_idx;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &info1_timer));
    SetDmaInfo1TimerCfg0(V, cfgInfo1TimerSecond_f, &info1_timer, 0);
    SetDmaInfo1TimerCfg0(V, cfgInfo1TimerNanoSec_f, &info1_timer, 2000000);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &info1_timer));

    /*for monitor set 1S*/
    tbl_id = DmaInfo4TimerCfg0_t + dma_idx;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &info4_timer));
    SetDmaInfo4TimerCfg0(V, cfgInfo4TimerSecond_f, &info4_timer, 0);
    SetDmaInfo4TimerCfg0(V, cfgInfo4TimerNanoSec_f, &info4_timer, 1000000);
    tbl_id = DmaInfo4TimerCfg0_t + dma_idx;
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &info4_timer));

    if (dev_info.version_id == 1)
    {
        /*for learning set 10us*/
        tbl_id = DmaInfo0TimerCfg0_t + dma_idx;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &info0_timer));
        SetDmaInfo0TimerCfg0(V, cfgInfo0TimerSecond_f, &info0_timer, 0);
        SetDmaInfo0TimerCfg0(V, cfgInfo0TimerNanoSec_f, &info0_timer, 10000);
        tbl_id = DmaInfo0TimerCfg0_t + dma_idx;
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &info0_timer));

        /*for ipfix set 1us*/
        tbl_id = DmaInfo2TimerCfg0_t + dma_idx;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &info2_timer));
        SetDmaInfo2TimerCfg0(V, cfgInfo2TimerSecond_f, &info2_timer, 0);
        SetDmaInfo2TimerCfg0(V, cfgInfo2TimerNanoSec_f, &info2_timer, 1000);
        tbl_id = DmaInfo2TimerCfg0_t + dma_idx;
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &info2_timer));
    }
    else
    {
        /*learning using default*/

        /*for ipfix set 1s*/
        tbl_id = DmaInfo2TimerCfg0_t + dma_idx;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &info2_timer));
        SetDmaInfo2TimerCfg0(V, cfgInfo2TimerSecond_f, &info2_timer, 1);
        SetDmaInfo2TimerCfg0(V, cfgInfo2TimerNanoSec_f, &info2_timer, 0);
        tbl_id = DmaInfo2TimerCfg0_t + dma_idx;
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &info2_timer));
    }

    tbl_id = DmaInfoTimerCfg0_t + dma_idx;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &global_timer_ctl));

    if (dev_info.version_id == 1)
    {
        SetDmaInfoTimerCfg0(V, cfgInfo0DescTimerChk_f, &global_timer_ctl, 0);  /*learning*/
        SetDmaInfoTimerCfg0(V, cfgInfo0ReqTimerChk_f, &global_timer_ctl, 1);
        SetDmaInfoTimerCfg0(V, cfgInfo0TimerEnd_f, &global_timer_ctl, 0);

        SetDmaInfoTimerCfg0(V, cfgInfo2DescTimerChk_f, &global_timer_ctl, 0);  /*ipfix*/
        SetDmaInfoTimerCfg0(V, cfgInfo2ReqTimerChk_f, &global_timer_ctl, 1);
        SetDmaInfoTimerCfg0(V, cfgInfo2TimerEnd_f, &global_timer_ctl, 0);
    }
    else
    {
        SetDmaInfoTimerCfg0(V, cfgInfo0DescTimerChk_f, &global_timer_ctl, 1);  /*learning*/
        SetDmaInfoTimerCfg0(V, cfgInfo0ReqTimerChk_f, &global_timer_ctl, 0);
        SetDmaInfoTimerCfg0(V, cfgInfo0TimerEnd_f, &global_timer_ctl, 1);

        SetDmaInfoTimerCfg0(V, cfgInfo2DescTimerChk_f, &global_timer_ctl, 1);  /*ipfix*/
        SetDmaInfoTimerCfg0(V, cfgInfo2ReqTimerChk_f, &global_timer_ctl, 0);
        SetDmaInfoTimerCfg0(V, cfgInfo2TimerEnd_f, &global_timer_ctl, 1);
    }

    SetDmaInfoTimerCfg0(V, cfgInfo1DescTimerChk_f, &global_timer_ctl, 1); /*hashdump*/
    SetDmaInfoTimerCfg0(V, cfgInfo1ReqTimerChk_f, &global_timer_ctl, 0);
    SetDmaInfoTimerCfg0(V, cfgInfo1TimerEnd_f, &global_timer_ctl, 1);

    SetDmaInfoTimerCfg0(V, cfgInfo3DescTimerChk_f, &global_timer_ctl, 1);  /*sdc*/
    SetDmaInfoTimerCfg0(V, cfgInfo3ReqTimerChk_f, &global_timer_ctl, 0);
    SetDmaInfoTimerCfg0(V, cfgInfo3TimerEnd_f, &global_timer_ctl, 1);

    SetDmaInfoTimerCfg0(V, cfgInfo4DescTimerChk_f, &global_timer_ctl, 1);/*monitor*/
    SetDmaInfoTimerCfg0(V, cfgInfo4ReqTimerChk_f, &global_timer_ctl, 0);
    SetDmaInfoTimerCfg0(V, cfgInfo4TimerEnd_f, &global_timer_ctl, 1);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &global_timer_ctl));

    /* cfg port stats trigger function */
    tbl_id = DmaRegRd1TrigCfg0_t + dma_idx;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &trigger1_timer));
    SetDmaRegRd1TrigCfg0(V, cfgRegRd1TrigNanoSec_f, &trigger1_timer, 0);
    SetDmaRegRd1TrigCfg0(V, cfgRegRd1TrigSecond_f, &trigger1_timer, 1*60);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &trigger1_timer));

    /* cfg dma wr trigger function for add tcam key */
    tbl_id = DmaRegWrTrigCfg0_t + dma_idx;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &wr_trigger));
    SetDmaRegWrTrigCfg0(V, cfgRegWrTrigNanoSec_f, &wr_trigger, 10000);
    SetDmaRegWrTrigCfg0(V, cfgRegWrTrigSecond_f, &wr_trigger, 0);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &wr_trigger));

    /* cfg dma pkt stats trigger function */
    tbl_id = DmaRegRd2TrigCfg0_t + dma_idx;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &trigger2_timer));
    SetDmaRegRd2TrigCfg0(V, cfgRegRd2TrigNanoSec_f, &trigger2_timer, 0);
    SetDmaRegRd2TrigCfg0(V, cfgRegRd2TrigSecond_f, &trigger2_timer, 5*60);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &trigger2_timer));

    /* cfg dma RaQueCnt stats trigger function */
    tbl_id = DmaRegRd3TrigCfg0_t + dma_idx;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &trigger3_timer));
    SetDmaRegRd3TrigCfg0(V, cfgRegRd3TrigNanoSec_f, &trigger3_timer, 60000);
    SetDmaRegRd3TrigCfg0(V, cfgRegRd3TrigSecond_f, &trigger3_timer, 0);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &trigger3_timer));

    tbl_id = DmaRegTrigEnCfg0_t + dma_idx;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &trigger_ctl));
    SetDmaRegTrigEnCfg0(V, cfgRegRd1TrigEn_f, &trigger_ctl, 1);
    SetDmaRegTrigEnCfg0(V, cfgRegRd2TrigEn_f, &trigger_ctl, 1);
    SetDmaRegTrigEnCfg0(V, cfgRegRd3TrigEn_f, &trigger_ctl, 1);
    SetDmaRegTrigEnCfg0(V, cfgRegWrTrigEn_f, &trigger_ctl, 1);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &trigger_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_dma_crc_init(uint8 lchip, uint8 dma_idx)
{
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    DmaPktRxCrcCfg0_m rx_crc;
    DmaPktTxCrcCfg0_m tx_crc;

    sal_memset(&rx_crc, 0, sizeof(DmaPktRxCrcCfg0_m));
    sal_memset(&tx_crc, 0, sizeof(DmaPktTxCrcCfg0_m));

    SetDmaPktRxCrcCfg0(V,cfgPktRx0CrcValid_f, &rx_crc, 0);
    SetDmaPktRxCrcCfg0(V,cfgPktRx0CrcPadEn_f, &rx_crc, 0);
    SetDmaPktRxCrcCfg0(V,cfgPktRx1CrcValid_f, &rx_crc, 0);
    SetDmaPktRxCrcCfg0(V,cfgPktRx1CrcPadEn_f, &rx_crc, 0);
    SetDmaPktRxCrcCfg0(V,cfgPktRx2CrcValid_f, &rx_crc, 0);
    SetDmaPktRxCrcCfg0(V,cfgPktRx2CrcPadEn_f, &rx_crc, 0);
    SetDmaPktRxCrcCfg0(V,cfgPktRx3CrcValid_f, &rx_crc, 0);
    SetDmaPktRxCrcCfg0(V,cfgPktRx3CrcPadEn_f, &rx_crc, 0);

    SetDmaPktTxCrcCfg0(V, cfgPktTxCrcValid_f, &tx_crc, 0);
    SetDmaPktTxCrcCfg0(V, cfgPktTxCrcChkEn_f, &tx_crc, 0);
    SetDmaPktTxCrcCfg0(V, cfgPktTxCrcPadEn_f, &tx_crc, 1);

    tbl_id = DmaPktRxCrcCfg0_t + dma_idx;
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rx_crc));

    tbl_id = DmaPktTxCrcCfg0_t + dma_idx;
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tx_crc));

    return CTC_E_NONE;
}

/**
@brief Get Dma channel config
*/
STATIC int32
_sys_goldengate_dma_get_chan_cfg(uint8 lchip, uint8 chan_id, uint8 dma_id, ctc_dma_global_cfg_t* ctc_cfg, sys_dma_chan_t* sys_cfg)
{
    uint32 desc_size = 0;
    uint32 desc_num = 0;
    ctc_chip_device_info_t dev_info;

    sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));

    sys_goldengate_chip_get_device_info(lchip, &dev_info);

    switch(chan_id)
    {
        case SYS_DMA_PACKET_RX0_CHAN_ID:
        case SYS_DMA_PACKET_RX1_CHAN_ID:
        case SYS_DMA_PACKET_RX2_CHAN_ID:
        case SYS_DMA_PACKET_RX3_CHAN_ID:
            /* dma pcket rx using 256bytes as one block for one transfer */
            desc_size = (ctc_cfg->pkt_rx[chan_id].data < 256)?256:(ctc_cfg->pkt_rx[chan_id].data);
            desc_num = (ctc_cfg->pkt_rx[chan_id].desc_num)?(ctc_cfg->pkt_rx[chan_id].desc_num):64;
            desc_num = (desc_num > SYS_DMA_MAX_PACKET_RX_DESC_NUM)?SYS_DMA_MAX_PACKET_RX_DESC_NUM:desc_num;

            sys_cfg->chan_en = 1;
            sys_cfg->channel_id = SYS_DMA_PACKET_RX0_CHAN_ID+chan_id;
            sys_cfg->current_index = 0;
            sys_cfg->cfg_size = desc_size;
            sys_cfg->data_size = desc_size;
            sys_cfg->desc_depth = desc_num;
            sys_cfg->desc_num = desc_num;
            sys_cfg->func_type = CTC_DMA_FUNC_PACKET_RX;
            sys_cfg->dmasel = dma_id;  /*only support dmactl0 for packet */
            sys_cfg->sync_chan = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_chan;
            sys_cfg->sync_en = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_en;

            /* for 4 dma rx channel, channel 0 is high priority */
            if (chan_id == 0)
            {
                sys_cfg->weight = SYS_DMA_HIGH_WEIGHT + 2;
            }
            else
            {
                sys_cfg->weight = SYS_DMA_HIGH_WEIGHT;
            }

            sal_memcpy(&p_gg_dma_master[lchip]->dma_chan_info[chan_id], sys_cfg, sizeof(sys_dma_chan_t));
            break;

        case SYS_DMA_PACKET_TX0_CHAN_ID:
        case SYS_DMA_PACKET_TX1_CHAN_ID:
            desc_num = (ctc_cfg->pkt_tx.desc_num)?(ctc_cfg->pkt_tx.desc_num):8;
            desc_num = (desc_num > SYS_DMA_MAX_PACKET_TX_DESC_NUM)?SYS_DMA_MAX_PACKET_TX_DESC_NUM:desc_num;

            sys_cfg->chan_en = 1;
            sys_cfg->channel_id = chan_id;
            sys_cfg->current_index = 0;
            sys_cfg->cfg_size = SYS_DMA_MTU;
            sys_cfg->data_size = SYS_DMA_MTU;
            sys_cfg->desc_depth = desc_num;
            sys_cfg->desc_num = desc_num;
            sys_cfg->func_type = CTC_DMA_FUNC_PACKET_TX;
            sys_cfg->dmasel = dma_id;  /*only support dmactl0 for packet */
            sys_cfg->sync_chan = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_chan;
            sys_cfg->sync_en = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_en;

            /* for 2 dma tx channel, channel tx1 is high priority */
            if (chan_id == SYS_DMA_PACKET_TX1_CHAN_ID)
            {
                sys_cfg->weight = SYS_DMA_HIGH_WEIGHT + 2;
            }
            else
            {
                sys_cfg->weight = SYS_DMA_HIGH_WEIGHT;
            }

            sal_memcpy(&p_gg_dma_master[lchip]->dma_chan_info[chan_id], sys_cfg, sizeof(sys_dma_chan_t));
            break;

        case SYS_DMA_TBL_RD_CHAN_ID:
            sys_cfg->chan_en = 1;
            sys_cfg->channel_id = SYS_DMA_TBL_RD_CHAN_ID;
            sys_cfg->current_index = 0;
            sys_cfg->cfg_size = 0;
            sys_cfg->data_size = 0;
            sys_cfg->desc_depth = 8;
            sys_cfg->desc_num = 0;
            sys_cfg->func_type = CTC_DMA_FUNC_TABLE_R;
            sys_cfg->dmasel = dma_id;
            sys_cfg->weight = SYS_DMA_LOW_WEIGHT;
            sys_cfg->sync_chan = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_chan;
            sys_cfg->sync_en = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_en;

            sal_memcpy(&p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_TBL_RD_CHAN_ID], sys_cfg, sizeof(sys_dma_chan_t));
            break;

        case SYS_DMA_TBL_WR_CHAN_ID:
            sys_cfg->chan_en = 1;
            sys_cfg->channel_id = SYS_DMA_TBL_WR_CHAN_ID;
            sys_cfg->current_index = 0;
            sys_cfg->cfg_size = 0;
            sys_cfg->data_size = 0;
            sys_cfg->desc_depth = 100;
            sys_cfg->desc_num = 0;
            sys_cfg->func_type = CTC_DMA_FUNC_TABLE_W;
            sys_cfg->dmasel = dma_id;
            sys_cfg->weight = SYS_DMA_LOW_WEIGHT;
            sys_cfg->sync_chan = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_chan;
            sys_cfg->sync_en = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_en;

            sal_memcpy(&p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_TBL_WR_CHAN_ID], sys_cfg, sizeof(sys_dma_chan_t));
            break;

        case SYS_DMA_PORT_STATS_CHAN_ID:
            sys_cfg->chan_en = 1;
            sys_cfg->channel_id = SYS_DMA_PORT_STATS_CHAN_ID;
            sys_cfg->current_index = 0;
            sys_cfg->cfg_size = 36*4*4;
            sys_cfg->data_size = 36*4*4;
            sys_cfg->desc_depth = SYS_GOLDENGATE_DMA_MAC_STATS_NUM;
            sys_cfg->desc_num = SYS_GOLDENGATE_DMA_MAC_STATS_NUM;
            sys_cfg->func_type = CTC_DMA_FUNC_STATS;
            sys_cfg->dmasel = dma_id;
            sys_cfg->weight = SYS_DMA_LOW_WEIGHT;
            sys_cfg->sync_chan = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_chan;
            sys_cfg->sync_en = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_en;

            sal_memcpy(&p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_PORT_STATS_CHAN_ID], sys_cfg, sizeof(sys_dma_chan_t));
            break;

        case SYS_DMA_LEARNING_CHAN_ID:
            desc_num = (ctc_cfg->learning.desc_num)?(ctc_cfg->learning.desc_num):64;
            desc_num = (desc_num > SYS_DMA_MAX_LEARNING_DESC_NUM)?SYS_DMA_MAX_LEARNING_DESC_NUM:desc_num;

            sys_cfg->chan_en = 1;
            sys_cfg->channel_id = SYS_DMA_LEARNING_CHAN_ID;

            if (dev_info.version_id == 1 )
            {
                sys_cfg->req_timeout_en = 1;
            }
            else
            {
                sys_cfg->req_timeout_en = 0;
            }
            sys_cfg->current_index = 0;
            sys_cfg->cfg_size = 256;
            /* need confirm later for performance */
            sys_cfg->data_size = 256*sizeof(sys_dma_learning_info_t);
            sys_cfg->desc_depth = desc_num;
            sys_cfg->desc_num = desc_num;
            sys_cfg->func_type = CTC_DMA_FUNC_HW_LEARNING;
            sys_cfg->dmasel = dma_id;
            sys_cfg->weight = SYS_DMA_MID_WEIGHT;
            sys_cfg->sync_chan = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_chan;
            sys_cfg->sync_en = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_en;

            sal_memcpy(&p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_LEARNING_CHAN_ID], sys_cfg, sizeof(sys_dma_chan_t));
            break;

        case SYS_DMA_IPFIX_CHAN_ID:
            desc_num = (ctc_cfg->ipfix.desc_num)?(ctc_cfg->ipfix.desc_num):64;
            desc_num = (desc_num > SYS_DMA_MAX_IPFIX_DESC_NUM)?SYS_DMA_MAX_IPFIX_DESC_NUM:desc_num;

            sys_cfg->chan_en = 1;
            sys_cfg->channel_id = SYS_DMA_IPFIX_CHAN_ID;

            if (dev_info.version_id == 1 )
            {
                sys_cfg->req_timeout_en = 1;
            }
            else
            {
                sys_cfg->req_timeout_en = 0;
            }
            sys_cfg->current_index = 0;
            sys_cfg->cfg_size = 512;
            sys_cfg->data_size = 512*sizeof(DmaIpfixAccFifo_m);
            sys_cfg->desc_depth = desc_num;
            sys_cfg->desc_num = desc_num;
            sys_cfg->func_type = CTC_DMA_FUNC_IPFIX;
            sys_cfg->dmasel = dma_id;
            sys_cfg->weight = SYS_DMA_MID_WEIGHT;
            sys_cfg->sync_chan = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_chan;
            sys_cfg->sync_en = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_en;

            sal_memcpy(&p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_IPFIX_CHAN_ID], sys_cfg, sizeof(sys_dma_chan_t));
            break;

        case SYS_DMA_SDC_CHAN_ID:
            sys_cfg->chan_en = 1;
            sys_cfg->channel_id = SYS_DMA_SDC_CHAN_ID;
            sys_cfg->current_index = 0;
            sys_cfg->cfg_size = 8;
            sys_cfg->data_size = 8*sizeof(DmaSdcFifo_m);
            sys_cfg->desc_depth = 64;
            sys_cfg->desc_num = 64;
            sys_cfg->func_type = CTC_DMA_FUNC_SDC;
            sys_cfg->dmasel = dma_id;
            sys_cfg->weight = SYS_DMA_MID_WEIGHT;
            sys_cfg->sync_chan = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_chan;
            sys_cfg->sync_en = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_en;

            sal_memcpy(&p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_SDC_CHAN_ID], sys_cfg, sizeof(sys_dma_chan_t));
            break;

        case SYS_DMA_MONITOR_CHAN_ID:
            /*for monitor function per monitor interval process 128 entries, Dma allocate 1k entry data memory,
                So entry desc consume time is :1024/128*interval
             */
            sys_cfg->chan_en = 1;
            sys_cfg->channel_id = SYS_DMA_MONITOR_CHAN_ID;
            sys_cfg->current_index = 0;
            sys_cfg->cfg_size = 1024;
            sys_cfg->data_size = 1024*sizeof(DmaActMonFifo_m);
            sys_cfg->desc_depth = 64;
            sys_cfg->desc_num = 64;
            sys_cfg->func_type = CTC_DMA_FUNC_MONITOR;
            sys_cfg->dmasel = dma_id;
            sys_cfg->weight = SYS_DMA_LOW_WEIGHT;
            sys_cfg->sync_chan = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_chan;
            sys_cfg->sync_en = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_en;

            sal_memcpy(&p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_MONITOR_CHAN_ID], sys_cfg, sizeof(sys_dma_chan_t));
            break;

        case SYS_DMA_HASHKEY_CHAN_ID:
            sys_cfg->chan_en = 1;
            sys_cfg->channel_id = SYS_DMA_HASHKEY_CHAN_ID;
            sys_cfg->current_index = 0;
            sys_cfg->cfg_size = 256;   /* for optimal should eq dump threshold */
            sys_cfg->data_size = 256*sizeof(DmaFibDumpFifo_m);
            sys_cfg->desc_depth = 64;
            sys_cfg->desc_num = 64;
            sys_cfg->func_type = 0;
            sys_cfg->dmasel = dma_id;
            sys_cfg->weight = SYS_DMA_MID_WEIGHT;
            sys_cfg->sync_chan = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_chan;
            sys_cfg->sync_en = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_en;

            sal_memcpy(&p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_HASHKEY_CHAN_ID], sys_cfg, sizeof(sys_dma_chan_t));
            break;

        case SYS_DMA_PKT_STATS_CHAN_ID:
            sys_cfg->chan_en = 1;
            sys_cfg->channel_id = SYS_DMA_PKT_STATS_CHAN_ID;
            sys_cfg->current_index = 0;
            sys_cfg->cfg_size = 64;  /*Notice:for reg dma, size must eq ds(2 n)*entry num */
            sys_cfg->data_size = 64;
            sys_cfg->desc_depth = 1;
            sys_cfg->desc_num = 1;
            sys_cfg->func_type = 0;
            sys_cfg->dmasel = dma_id;
            sys_cfg->weight = SYS_DMA_LOW_WEIGHT;
            sys_cfg->sync_chan = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_chan;
            sys_cfg->sync_en = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_en;

            sal_memcpy(&p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_PKT_STATS_CHAN_ID], sys_cfg, sizeof(sys_dma_chan_t));
            break;

        case SYS_DMA_REG_MAX_CHAN_ID:
            sys_cfg->chan_en = 1;
            sys_cfg->channel_id = SYS_DMA_REG_MAX_CHAN_ID;
            sys_cfg->current_index = 0;
            sys_cfg->cfg_size = 768*4;  /*Notice:for reg dma, size must eq ds(2 n)*entry num, 48 ports, 16 queue num per port */
            sys_cfg->data_size = 768*4;
            sys_cfg->desc_depth = 64;
            sys_cfg->desc_num = 64;
            sys_cfg->func_type = 0;
            sys_cfg->dmasel = dma_id;
            sys_cfg->weight = SYS_DMA_LOW_WEIGHT;
            sys_cfg->sync_chan = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_chan;
            sys_cfg->sync_en = p_gg_dma_master[lchip]->dma_chan_info[chan_id].sync_en;

            sal_memcpy(&p_gg_dma_master[lchip]->dma_chan_info[SYS_DMA_REG_MAX_CHAN_ID], sys_cfg, sizeof(sys_dma_chan_t));
            break;

        default:
            return CTC_E_INVALID_PARAM;

    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldgendate_dma_init_thread(uint8 lchip, uint8 start_chan, uint8 cur_chan, uint16 prio)
{
    uint8 temp_idx = 0;
    uint8 need_merge = 0;
    sys_dma_thread_t* p_thread_info = NULL;
    int32 ret = 0;

    for (temp_idx = start_chan; temp_idx < cur_chan; temp_idx++)
    {
        if (p_gg_dma_master[lchip]->dma_thread_pri[temp_idx] == prio)
        {
            need_merge = 1;
            break;
        }
    }

    if (need_merge)
    {
        p_thread_info = ctc_vector_get(p_gg_dma_master[lchip]->p_thread_vector, temp_idx);
        if (!p_thread_info)
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        p_thread_info->lchip = lchip;
        p_thread_info->chan_num++;
        p_thread_info->chan_id[p_thread_info->chan_num-1] = cur_chan;
        p_gg_dma_master[lchip]->dma_chan_info[cur_chan].sync_chan = temp_idx;
        p_gg_dma_master[lchip]->dma_chan_info[cur_chan].sync_en = 1;
    }
    else
    {
        p_thread_info = ctc_vector_get(p_gg_dma_master[lchip]->p_thread_vector, cur_chan);
        if (p_thread_info)
        {
            return CTC_E_ENTRY_EXIST;
        }

        /*create new thread info*/
        p_thread_info = (sys_dma_thread_t*)mem_malloc(MEM_DMA_MODULE, sizeof(sys_dma_thread_t));
        if (!p_thread_info)
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_thread_info, 0, sizeof(sys_dma_thread_t));

        p_thread_info->chan_num = 1;
        p_thread_info->chan_id[0] = cur_chan;
        p_thread_info->prio = prio;
        p_thread_info->lchip = lchip;

        ret = sal_sem_create(&p_thread_info->p_sync_sem, 0);
        if (ret < 0)
        {
            return CTC_E_NOT_INIT;
        }
        ctc_vector_add(p_gg_dma_master[lchip]->p_thread_vector, cur_chan, (void*)p_thread_info);

        p_gg_dma_master[lchip]->dma_chan_info[cur_chan].sync_chan = cur_chan;
        p_gg_dma_master[lchip]->dma_chan_info[cur_chan].sync_en = 1;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_dma_init_db(uint8 lchip, ctc_dma_global_cfg_t* p_cfg)
{
    /* rx channel num should get from enq module, TODO */
    uint8 rx_chan_num = p_cfg->pkt_rx_chan_num;
    uint8 rx_chan_idx = 0;
    uint16 pri = SAL_TASK_PRIO_DEF;

    if (rx_chan_num > SYS_DMA_RX_CHAN_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    p_gg_dma_master[lchip]->packet_rx_chan_num = rx_chan_num;

    /* default enable these function */
    CTC_BIT_SET(p_gg_dma_master[lchip]->dma_en_flag, SYS_DMA_HASHKEY_CHAN_ID);
    CTC_BIT_SET(p_gg_dma_master[lchip]->dma_en_flag, SYS_DMA_TBL_RD_CHAN_ID);
    CTC_BIT_SET(p_gg_dma_master[lchip]->dma_en_flag, SYS_DMA_TBL_WR_CHAN_ID);

    /* init packet rx */
    if (CTC_IS_BIT_SET(p_cfg->func_en_bitmap, CTC_DMA_FUNC_PACKET_RX))
    {
        for (rx_chan_idx = 0; rx_chan_idx < rx_chan_num; rx_chan_idx++)
        {
            SYS_DMA_DMACTL_CHECK(p_cfg->pkt_rx[rx_chan_idx].dmasel);
            pri = p_cfg->pkt_rx[rx_chan_idx].priority;
            if (pri == 0)
            {
                pri = SAL_TASK_PRIO_DEF;
            }

            CTC_BIT_SET(p_gg_dma_master[lchip]->dma_en_flag, rx_chan_idx);
            p_gg_dma_master[lchip]->dma_thread_pri[rx_chan_idx] = pri;
            CTC_ERROR_RETURN(_sys_goldgendate_dma_init_thread(lchip, 0, rx_chan_idx, pri));
        }
    }

    /* init packet tx */
    if (CTC_IS_BIT_SET(p_cfg->func_en_bitmap, CTC_DMA_FUNC_PACKET_TX))
    {

        SYS_DMA_DMACTL_CHECK(p_cfg->pkt_tx.dmasel);
        CTC_BIT_SET(p_gg_dma_master[lchip]->dma_en_flag, SYS_DMA_PACKET_TX0_CHAN_ID);
        CTC_BIT_SET(p_gg_dma_master[lchip]->dma_en_flag, SYS_DMA_PACKET_TX0_CHAN_ID + 1);
    }

    /* init learning */
    if (CTC_IS_BIT_SET(p_cfg->func_en_bitmap, CTC_DMA_FUNC_HW_LEARNING))
    {
        SYS_DMA_DMACTL_CHECK(p_cfg->learning.dmasel);
        pri = p_cfg->learning.priority;
        if (pri == 0)
        {
            pri = SAL_TASK_PRIO_DEF;
        }

        CTC_BIT_SET(p_gg_dma_master[lchip]->dma_en_flag, SYS_DMA_LEARNING_CHAN_ID);
        p_gg_dma_master[lchip]->dma_thread_pri[SYS_DMA_LEARNING_CHAN_ID] = pri;
       CTC_ERROR_RETURN(_sys_goldgendate_dma_init_thread(lchip, SYS_DMA_LEARNING_CHAN_ID,
                SYS_DMA_LEARNING_CHAN_ID, pri));
    }

    p_gg_dma_master[lchip]->hw_learning_sync = p_cfg->hw_learning_sync_en;


    /* init ipfix */
    if (CTC_IS_BIT_SET(p_cfg->func_en_bitmap, CTC_DMA_FUNC_IPFIX))
    {
        SYS_DMA_DMACTL_CHECK(p_cfg->ipfix.dmasel);
        pri = p_cfg->ipfix.priority;
        if (pri == 0)
        {
            pri = SAL_TASK_PRIO_DEF;
        }
        CTC_BIT_SET(p_gg_dma_master[lchip]->dma_en_flag, SYS_DMA_IPFIX_CHAN_ID);
        p_gg_dma_master[lchip]->dma_thread_pri[SYS_DMA_IPFIX_CHAN_ID] = pri;
        CTC_ERROR_RETURN(_sys_goldgendate_dma_init_thread(lchip, SYS_DMA_LEARNING_CHAN_ID, SYS_DMA_IPFIX_CHAN_ID, pri));
    }

    /* init sdc */
    if (CTC_IS_BIT_SET(p_cfg->func_en_bitmap, CTC_DMA_FUNC_SDC))
    {
        CTC_BIT_SET(p_gg_dma_master[lchip]->dma_en_flag, SYS_DMA_SDC_CHAN_ID);
        p_gg_dma_master[lchip]->dma_thread_pri[SYS_DMA_SDC_CHAN_ID] = SAL_TASK_PRIO_DEF;
        CTC_ERROR_RETURN(_sys_goldgendate_dma_init_thread(lchip, SYS_DMA_LEARNING_CHAN_ID, SYS_DMA_SDC_CHAN_ID,
            SAL_TASK_PRIO_DEF));
    }

    /* init monitor */
    if (CTC_IS_BIT_SET(p_cfg->func_en_bitmap, CTC_DMA_FUNC_MONITOR))
    {
        CTC_BIT_SET(p_gg_dma_master[lchip]->dma_en_flag, SYS_DMA_MONITOR_CHAN_ID);
        p_gg_dma_master[lchip]->dma_thread_pri[SYS_DMA_MONITOR_CHAN_ID] = SAL_TASK_PRIO_NICE_LOW;
        CTC_ERROR_RETURN(_sys_goldgendate_dma_init_thread(lchip, SYS_DMA_LEARNING_CHAN_ID, SYS_DMA_MONITOR_CHAN_ID,
             SAL_TASK_PRIO_DEF));
    }

    /* init port stats*/
    if (CTC_IS_BIT_SET(p_cfg->func_en_bitmap, CTC_DMA_FUNC_STATS))
    {
        CTC_BIT_SET(p_gg_dma_master[lchip]->dma_en_flag, SYS_DMA_PORT_STATS_CHAN_ID);
        p_gg_dma_master[lchip]->dma_thread_pri[SYS_DMA_PORT_STATS_CHAN_ID] = SAL_TASK_PRIO_NICE_LOW;
        CTC_ERROR_RETURN(_sys_goldgendate_dma_init_thread(lchip, SYS_DMA_PORT_STATS_CHAN_ID,
             SYS_DMA_PORT_STATS_CHAN_ID, SAL_TASK_PRIO_NICE_LOW));
    }

    /*internel using for debug queue*/
    /* CTC_BIT_SET(p_gg_dma_master[lchip]->dma_en_flag, SYS_DMA_REG_MAX_CHAN_ID);*/
    p_gg_dma_master[lchip]->dma_thread_pri[SYS_DMA_REG_MAX_CHAN_ID] = SAL_TASK_PRIO_NICE_LOW;
    CTC_ERROR_RETURN(_sys_goldgendate_dma_init_thread(lchip, SYS_DMA_PORT_STATS_CHAN_ID,
         SYS_DMA_REG_MAX_CHAN_ID, SAL_TASK_PRIO_NICE_LOW));

    return CTC_E_NONE;
}

/**
 @brief init dma module and allocate necessary memory
*/
int32
sys_goldengate_dma_init(uint8 lchip, ctc_dma_global_cfg_t* dma_global_cfg)
{
    int32 ret = 0;
    uint8 index = 0;
    sys_dma_chan_t dma_chan_info;
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint32 init_done = FALSE;
    uint8 dma_id = 0;
    drv_work_platform_type_t platform_type;
    DmaCtlDrainEnable0_m dma_drain;
    uint16 init_cnt = 0;
    uint32 tbl_id1 = 0;
    uint32 tbl_id2 = 0;
    dal_dma_info_t dma_info;
    sys_dma_tbl_rw_t dma_wr;
    uint32 pad_value[8] = {0};
    DmaMiscCfg0_m misc_cfg;
    host_type_t byte_order;
    ctc_chip_device_info_t dev_info;
    DmaCtlFifoCtl0_m dma_fifo;

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&dma_drain, 0, sizeof(DmaCtlDrainEnable0_m));

    if (!CTC_WB_ENABLE && p_gg_dma_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (NULL == p_gg_dma_master[lchip])
    {
        p_gg_dma_master[lchip] = (sys_dma_master_t*)mem_malloc(MEM_DMA_MODULE, sizeof(sys_dma_master_t));
        if (NULL == p_gg_dma_master[lchip])
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_gg_dma_master[lchip], 0, sizeof(sys_dma_master_t));
    }

    if ((CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING))
    {
        return CTC_E_NONE;
    }

    p_gg_dma_master[lchip]->g_drain_en[0] = 1;
    p_gg_dma_master[lchip]->g_drain_en[1] = 1;
    p_gg_dma_master[lchip]->g_drain_en[2] = 1;

    p_gg_dma_master[lchip]->p_thread_vector = ctc_vector_init(4, SYS_DMA_MAX_CHAN_NUM / 4);
    if (NULL == p_gg_dma_master[lchip]->p_thread_vector)
    {
        mem_free(p_gg_dma_master[lchip]);
        return CTC_E_NO_MEMORY;
    }

    sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));

    sys_goldengate_chip_get_device_info(lchip, &dev_info);
    sys_goldengate_chip_get_pcie_select(lchip, &dma_id);

    _sys_goldengate_dma_init_db(lchip, dma_global_cfg);

    ret = drv_goldengate_get_platform_type(&platform_type);

    byte_order = drv_goldengate_get_host_type();

    sal_memset(&dma_info, 0 ,sizeof(dal_dma_info_t));
    dal_get_dma_info(lchip, &dma_info);
    p_gg_dma_master[lchip]->dma_high_addr = dma_info.phy_base_hi;
     /*-GET_HIGH_32BITS(dma_info.phy_base, p_gg_dma_master[lchip]->dma_high_addr);*/
    if (p_gg_dma_master[lchip]->dma_high_addr)
    {
         /*-SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "DMA Memory exceed 4G!!!!!!!!!\n");*/
    }

    /*for multi-chip mem alloc fail*/
    if(DRV_CHIP_AGT_MODE_CLIENT == drv_goldengate_chip_agent_mode())
            return CTC_E_NONE;

    /*vxworks TLP set 128*/
#ifdef _SAL_VXWORKS
    field_val = 0;
    cmd = DRV_IOW(Pcie0SysCfg_t, Pcie0SysCfg_pcie0PcieMaxRdSize_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &field_val), ret, error);
#endif

    if(TRUE)
    {
        /* 1. public config, need config 2 dmactl */
        for (index = dma_id; index <= dma_id; index++)
        {
            field_val = 1;
            tbl_id1 = DmaCtlInit0_t + index;
            tbl_id2 = DmaCtlInitDone0_t + index;
            cmd = DRV_IOW(tbl_id1, DmaCtlInit0_dmaInit_f);
            CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &field_val), ret, error);
            if (platform_type == HW_PLATFORM)
            {
                /* wait for init done */
                while (init_cnt < SYS_DMA_INIT_COUNT)
                {
                    cmd = DRV_IOR(tbl_id2, DmaCtlInitDone0_dmaInitDone_f);
                    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &field_val), ret, error);

                    /* init failed */
                    if (field_val)
                    {
                        init_done = TRUE;
                        break;
                    }

                    init_cnt++;
                }

                if (init_done == FALSE)
                {
                    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "DMACtl %d init falied!\n", index);
                    return CTC_E_NOT_INIT;
                }
            }

            tbl_id1 = DmaMiscCfg0_t + index;
            cmd = DRV_IOR(tbl_id1, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &misc_cfg));
            if (byte_order == HOST_LE)
            {
                SetDmaMiscCfg0(V, cfgDmaRegRdEndian_f, &misc_cfg, 1);
                SetDmaMiscCfg0(V, cfgDmaRegWrEndian_f, &misc_cfg, 1);
                SetDmaMiscCfg0(V, cfgDmaInfoEndian_f, &misc_cfg, 1);
                SetDmaMiscCfg0(V, cfgToCpuDescEndian_f, &misc_cfg, 1);
                SetDmaMiscCfg0(V, cfgFrCpuDescEndian_f, &misc_cfg, 1);
            }
            /*cfxMaxPayloadSize 5, desc and data will follow one channel */
            SetDmaMiscCfg0(V, cfgMaxPayloadSize_f, &misc_cfg, 5);
            cmd = DRV_IOW(tbl_id1, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &misc_cfg));

            tbl_id1 = DmaCtlFifoCtl0_t + index;
            cmd = DRV_IOR(tbl_id1, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &dma_fifo);
            SetDmaCtlFifoCtl0(V,dmaPktRx0ReqFifoAFullThrd_f, &dma_fifo, 4);
            SetDmaCtlFifoCtl0(V,dmaPktRx1ReqFifoAFullThrd_f, &dma_fifo, 4);
            SetDmaCtlFifoCtl0(V,dmaPktRx2ReqFifoAFullThrd_f, &dma_fifo, 4);
            SetDmaCtlFifoCtl0(V,dmaPktRx3ReqFifoAFullThrd_f, &dma_fifo, 4);
            cmd = DRV_IOW(tbl_id1, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &dma_fifo);

            /* init dma endian, using default config  */
             CTC_ERROR_GOTO(_sys_goldengate_dma_intr_init(lchip, index, dma_global_cfg), ret, error);

            /* cfg Info Dma timer */
             CTC_ERROR_GOTO(_sys_goldengate_dma_timer_init(lchip, index), ret, error);

           /* cfg packet crc */
            CTC_ERROR_GOTO(_sys_goldengate_dma_crc_init(lchip, index), ret, error);


        }

        /* 2.  per channel config, just config the dmactl which have the function  */
        for (index = 0; index < SYS_DMA_MAX_CHAN_NUM; index++)
        {
            if (CTC_IS_BIT_SET(p_gg_dma_master[lchip]->dma_en_flag, index))
            {
                sal_memset(&dma_chan_info, 0, sizeof(sys_dma_chan_t));

                CTC_ERROR_GOTO(_sys_goldengate_dma_get_chan_cfg(lchip, index, dma_id, dma_global_cfg, &dma_chan_info), ret, error);
                CTC_ERROR_GOTO(_sys_goldengate_dma_common_init(lchip, &dma_chan_info), ret, error);
                CTC_ERROR_GOTO(_sys_goldengate_dma_sync_init(lchip, index), ret, error);

            }
        }

        if (dev_info.version_id == 1)
        {
            sal_memset(&dma_wr, 0, sizeof(sys_dma_tbl_rw_t));
            dma_wr.tbl_addr = 0xf0000000;
            dma_wr.buffer = pad_value;
            dma_wr.entry_num = 1;
            dma_wr.entry_len = 32;
            dma_wr.rflag = 0;
            sys_goldengate_dma_rw_table(lchip, &dma_wr);

            for (index = 0; index < 10; index++)
            {
                p_gg_dma_master[lchip]->g_tcam_wr_buffer[index] = g_dal_op.dma_alloc(lchip, 4, 0);
            }

            drv_goldengate_chip_register_tcam_write_cb(sys_goldengate_dma_tcam_write);

            drv_goldengate_chip_set_write_tcam_mode(0, 1);
            drv_goldengate_chip_set_write_tcam_mode(1, 1);

            sal_task_sleep(1);
        }

        /*just for uml */
        if (platform_type == SW_SIM_PLATFORM)
        {
            cmd = DRV_IOR(DmaCtlDrainEnable0_t+dma_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dma_drain));
            SetDmaCtlDrainEnable0(V, dmaInfo0DrainEn_f, &dma_drain, 1);
            SetDmaCtlDrainEnable0(V, dmaInfo1DrainEn_f, &dma_drain, 1);
            SetDmaCtlDrainEnable0(V, dmaInfo2DrainEn_f, &dma_drain, 1);
            SetDmaCtlDrainEnable0(V, dmaInfo3DrainEn_f, &dma_drain, 1);
            SetDmaCtlDrainEnable0(V, dmaInfo4DrainEn_f, &dma_drain, 1);
            SetDmaCtlDrainEnable0(V, dmaPktRxDrainEn_f, &dma_drain, 1);
            SetDmaCtlDrainEnable0(V, dmaPktTxDrainEn_f, &dma_drain, 1);
            SetDmaCtlDrainEnable0(V, dmaRegRdDrainEn_f, &dma_drain, 1);
            SetDmaCtlDrainEnable0(V, dmaRegWrDrainEn_f, &dma_drain, 1);
            cmd = DRV_IOW(DmaCtlDrainEnable0_t+dma_id, DRV_ENTRY_FLAG);
            CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &dma_drain), ret, error);
        }
    }

    return CTC_E_NONE;

error:
    _sys_goldengate_dma_reclaim_resource(lchip);
    if (NULL != p_gg_dma_master[lchip])
    {
        mem_free(p_gg_dma_master[lchip]);
    }
    return ret;

}

STATIC int32
_sys_goldengate_dma_free_node_data(void* node_data, void* user_data)
{
    uint8 type = 0;
    sys_dma_thread_t* p_thread_info = NULL;

    if (user_data)
    {
        type = *(uint8*)user_data;
        if (0 == type)
        {
            p_thread_info = (sys_dma_thread_t*)node_data;
            if (p_thread_info->p_sync_task)
            {
                sal_sem_give(p_thread_info->p_sync_sem);
                sal_task_destroy(p_thread_info->p_sync_task);
                sal_sem_destroy(p_thread_info->p_sync_sem);
            }
        }
    }
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_dma_deinit(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint8 type = 0;
    uint8 index = 0;
    uint8 pcie_sel = 0;

    SYS_DMA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    LCHIP_CHECK(lchip);
    if (NULL == p_gg_dma_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*1. disable fibacc  send to dma */
    field_val = 0;
    cmd = DRV_IOW(FibAccelerationCtl_t, FibAccelerationCtl_dmaForFastAging_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(FibAccelerationCtl_t, FibAccelerationCtl_dmaForFastLearning_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(IpfixEngineCtl_t, IpfixEngineCtl_denySendDmaForDebug_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(QMgrEnqCtl_t, QMgrEnqCtl_activeBufMonEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(EpePktProcCtl_t, EpePktProcCtl_latencyInformEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    CTC_ERROR_RETURN(sys_goldengate_set_dma_channel_drop_en(lchip, TRUE));

    CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &pcie_sel));
    /*2. reset dmactl, resetDmaCtl0*/
    field_val = 1;
    cmd = DRV_IOW(SupResetCtl_t, SupResetCtl_resetDmaCtl0_f+pcie_sel);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*3. clear credit*/
    field_val = 0;
    cmd = DRV_IOW(FibAccCreditStatus_t, FibAccCreditStatus_fibAccDmaCreditUsed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(FlowAccCreditStatus_t, FlowAccCreditStatus_flowAccDmaCreditUsed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(EpeHdrProcCreditStatus0_t, EpeHdrProcCreditStatus0_dmaCreditUsed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(EpeHdrProcCreditStatus1_t, EpeHdrProcCreditStatus1_dmaCreditUsed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(QMgrEnqCreditStatus_t, QMgrEnqCreditStatus_dmaQMgrCreditUsed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(BufRetrvCreditStatus0_t, BufRetrvCreditStatus0_bufRetrvDma0CreditUsed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(BufRetrvCreditStatus0_t, BufRetrvCreditStatus0_bufRetrvDma1CreditUsed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(BufRetrvCreditStatus0_t, BufRetrvCreditStatus0_bufRetrvDma2CreditUsed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(BufRetrvCreditStatus0_t, BufRetrvCreditStatus0_bufRetrvDma3CreditUsed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(BufRetrvCreditStatus1_t, BufRetrvCreditStatus1_bufRetrvDma0CreditUsed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(BufRetrvCreditStatus1_t, BufRetrvCreditStatus1_bufRetrvDma1CreditUsed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(BufRetrvCreditStatus1_t, BufRetrvCreditStatus1_bufRetrvDma2CreditUsed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(BufRetrvCreditStatus1_t, BufRetrvCreditStatus1_bufRetrvDma3CreditUsed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*4. sleep */
    sal_task_sleep(1);

    /*5. enable send to dma*/
    field_val = 1;
    cmd = DRV_IOW(FibAccelerationCtl_t, FibAccelerationCtl_dmaForFastAging_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 1;
    cmd = DRV_IOW(FibAccelerationCtl_t, FibAccelerationCtl_dmaForFastLearning_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 1;
    cmd = DRV_IOW(IpfixEngineCtl_t, IpfixEngineCtl_denySendDmaForDebug_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 1;
    cmd = DRV_IOW(QMgrEnqCtl_t, QMgrEnqCtl_activeBufMonEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 1;
    cmd = DRV_IOW(EpePktProcCtl_t, EpePktProcCtl_latencyInformEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    for (index = 0; index < 10; index++)
    {
        if (p_gg_dma_master[lchip]->g_tcam_wr_buffer[index])
        {
            g_dal_op.dma_free(lchip, p_gg_dma_master[lchip]->g_tcam_wr_buffer[index]);
        }
    }

    sal_task_destroy(p_gg_dma_master[lchip]->p_polling_task);

    /*free thread vector*/
    ctc_vector_traverse(p_gg_dma_master[lchip]->p_thread_vector, (vector_traversal_fn)_sys_goldengate_dma_free_node_data, &type);
    ctc_vector_release(p_gg_dma_master[lchip]->p_thread_vector);
    _sys_goldengate_dma_reclaim_resource(lchip);

    mem_free(p_gg_dma_master[lchip]);

    return CTC_E_NONE;
}

