/**
 @file sys_greatbelt_dma_debug.c

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
#include "ctc_greatbelt_interrupt.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_interrupt.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_packet.h"
#include "greatbelt/include/drv_io.h"
#include "sys_greatbelt_dma.h"
#include "sys_greatbelt_dma_priv.h"

extern sys_dma_master_t* p_gb_dma_master[CTC_MAX_LOCAL_CHIP_NUM];
extern dal_op_t g_dal_op;

uint16 cur_desc_index = 0;
uint8 cur_mem_index = 0;
extern uint16 g_sys_cur_learning_cnt[SYS_DMA_MAX_LEARNING_BLOCK];
extern int32
sys_greatbelt_dma_sync_pkt_stats(uint8 lchip);

STATIC void
_sys_greatbelt_show_desc_info(uint8 lchip, uint32 index, uint32* p_show_banner, sys_dma_desc_t* p_desc)
{
    pci_exp_desc_mem_t* p_desc_mem = NULL;
    uint64 phy_addr = 0;

    if (NULL == p_desc)
    {
        return;
    }
    p_desc_mem = &(p_desc->pci_exp_desc);

    if (*p_show_banner)
    {
        *p_show_banner = FALSE;
        SYS_DMA_DUMP("%-8s : %-20s\n", "IDX",   "Index");
        SYS_DMA_DUMP("%-8s : %-20s\n", "LAddr", "Logic Address");
        SYS_DMA_DUMP("%-8s : %-20s\n", "PAddr", "Physical Address");
        SYS_DMA_DUMP("%-8s : %-20s\n", "AddrLow", "desc_mem_addr_low");
        SYS_DMA_DUMP("%-8s : %-20s\n", "CfgAddr", "desc_cfg_addr");
        SYS_DMA_DUMP("%-8s : %-20s\n", "DON",   "desc_done");
        SYS_DMA_DUMP("%-8s : %-20s\n", "ER",    "desc_error");
        SYS_DMA_DUMP("%-8s : %-20s\n", "TMO",   "desc_timeout");
        SYS_DMA_DUMP("%-8s : %-20s\n", "DER",   "desc_data_error");
        SYS_DMA_DUMP("%-8s : %-20s\n", "WDS",   "desc_words");
        SYS_DMA_DUMP("%-8s : %-20s\n", "CRC",   "desc_crc_valid");
        SYS_DMA_DUMP("%-8s : %-20s\n", "NCRC",  "desc_non_crc");
        SYS_DMA_DUMP("%-8s : %-20s\n", "EOP",   "desc_eop");
        SYS_DMA_DUMP("%-8s : %-20s\n", "SOP",   "desc_sop");
        SYS_DMA_DUMP("%-8s : %-20s\n", "LEN",   "desc_len");
        SYS_DMA_DUMP("%-4s %-8s %-8s %-8s %-8s %-3s %-2s %-3s %-3s %-3s %-3s %-4s %-3s %-3s %-3s\n",
            "IDX", "LAddr", "PAddr", "AddrLow", "CfgAddr", "DON", "ER", "TMO", "DER", "WDS", "CRC", "NCRC", "EOP", "SOP", "LEN");
        SYS_DMA_DUMP("--------------------------------------------------------------------------------\n");
    }

    COMBINE_64BITS_DATA(p_gb_dma_master[lchip]->dma_high_addr, p_desc_mem->desc_mem_addr_low<<4, phy_addr);
    SYS_DMA_DUMP("%-4d %p 0x%"PRIx64" %p %08X %-3d %-2d %-3d %-3d %-3d %-3d %-4d %-3d %-3d %-3d\n",
        index,
        (void *)p_desc,
        g_dal_op.logic_to_phy(lchip, p_desc),
        (void *)g_dal_op.phy_to_logic(lchip, phy_addr),
        p_desc_mem->desc_cfg_addr,
        p_desc_mem->desc_done,
        p_desc_mem->desc_error,
        p_desc_mem->desc_timeout,
        p_desc_mem->desc_data_error,
        p_desc_mem->desc_words,
        p_desc_mem->desc_crc_valid,
        p_desc_mem->desc_non_crc,
        p_desc_mem->desc_eop,
        p_desc_mem->desc_sop,
        p_desc_mem->desc_len
        );

    return;
}

STATIC int32
_sys_greatbelt_dma_show_desc_used(uint8 lchip, sys_dma_chan_t* p_dma)
{
    sys_dma_desc_used_t* p_desc_used = &(p_dma->desc_used);
    sys_dma_desc_used_info_t* p_entry = NULL;
    uint32 index = 0;

    SYS_LCHIP_CHECK_ACTIVE(lchip);

    if (0 == p_desc_used->entry_num)
    {
        return CTC_E_NONE;
    }

    SYS_DMA_DUMP("Used Desc Count   :   %d\n", p_desc_used->count);
    if (0 == p_desc_used->count)
    {
        return CTC_E_NONE;
    }

    SYS_DMA_DUMP("%-5s %-8s\n", "Index", "MemAddr");
    SYS_DMA_DUMP("--------------\n");
    for (index = 0; index < p_desc_used->entry_num; index++)
    {
        p_entry = &(p_desc_used->p_array[index]);
        if (p_entry)
        {
            SYS_DMA_DUMP("%-5d %p\n", index, p_entry->p_mem);
        }
    }

    return CTC_E_NONE;
}

char*
sys_greatbelt_dma_dir_str(uint8 lchip, ctc_dma_direction_t dir)
{
    switch (dir)
    {
    case CTC_DMA_DIR_RX:
        return "RX";

    case CTC_DMA_DIR_TX:
        return "TX";

    default:
        return "Invalid";
    }
}

STATIC int32
_sys_greatbelt_dma_show_chan(uint8 lchip, sys_dma_chan_t* p_dma, uint32* p_show_banner, uint32 detail)
{
    dma_tx_ptr_tab_t tx_ptr;
    dma_rx_ptr_tab_t rx_ptr;
    uint32 ptr = 0;
    uint32 index = 0;
    uint32 cmd = 0;
    sys_dma_desc_t* p_dma_desc = NULL;

    SYS_LCHIP_CHECK_ACTIVE(lchip);

    if (CTC_DMA_DIR_TX == p_dma->direction)
    {
        cmd = DRV_IOR(DmaRxPtrTab_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, p_dma->channel_id, cmd, &rx_ptr));
        ptr = rx_ptr.ptr;
    }
    else
    {
        cmd = DRV_IOR(DmaTxPtrTab_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, p_dma->channel_id, cmd, &tx_ptr));
        ptr = tx_ptr.ptr;
    }

    if (!p_dma)
    {
        return CTC_E_INVALID_PTR;
    }

    if (detail)
    {
        SYS_DMA_DUMP("Channel ID    :   %d\n", p_dma->channel_id);
        SYS_DMA_DUMP("Direction     :   %s\n", sys_greatbelt_dma_dir_str(lchip, p_dma->direction));
        SYS_DMA_DUMP("Current Index :   %d\n", p_dma->current_index);
        SYS_DMA_DUMP("Chip Index    :   %d\n", ptr);
        SYS_DMA_DUMP("Desc Number   :   %d\n", p_dma->desc_num);
        SYS_DMA_DUMP("Desc Depth    :   %d\n", p_dma->desc_depth);
        SYS_DMA_DUMP("Memory Base   :   %p\n", (void*)p_dma->mem_base);

        for (index = 0; index < p_dma->desc_num; index++)
        {
            p_dma_desc = &p_dma->p_desc[index];
            _sys_greatbelt_show_desc_info(lchip, index, p_show_banner, p_dma_desc);
        }
        _sys_greatbelt_dma_show_desc_used(lchip, p_dma);
    }
    else
    {
        if (*p_show_banner)
        {
            *p_show_banner = FALSE;
            SYS_DMA_DUMP("%-4s %-4s %-8s %-8s %-8s %-8s\n",
                "Chan", "DIR", "DescNum", "DescDep", "CurIndex", "MemBase");
            SYS_DMA_DUMP("--------------------------------------------------------------------------------\n");
        }

        SYS_DMA_DUMP("%-4d %-4s %-8d %-8d %-8d %p\n",
            p_dma->channel_id,
            sys_greatbelt_dma_dir_str(lchip, p_dma->direction),
            p_dma->desc_num,
            p_dma->desc_depth,
            p_dma->current_index,
            (void*)p_dma->mem_base
            );
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_dma_show_state(uint8 lchip, uint32 detail)
{
    uint32 show_banner = TRUE;
    uint8 index = 0;
    SYS_DMA_INIT_CHECK(lchip);

    /*1. for Packet Rx */
    SYS_DMA_DUMP("\n*********** Packet RX *************\n");
    if (CTC_IS_BIT_SET(p_gb_dma_master[lchip]->dma_en_flag, CTC_DMA_FUNC_PACKET_RX))
    {
        SYS_DMA_DUMP("Packet RX Channel Num     :   %d\n", p_gb_dma_master[lchip]->pkt_rx_chan_num);
        SYS_DMA_DUMP("Packet Size Per Desc      :   %d\n", p_gb_dma_master[lchip]->pkt_rx_size_per_desc);

        for (index = 0; index < p_gb_dma_master[lchip]->pkt_rx_chan_num; index++)
        {
            _sys_greatbelt_dma_show_chan(lchip, &(p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_PACKET_RX][index]), &show_banner, detail);
        }
    }
    else
    {
        SYS_DMA_DUMP("\nDisabled!!!\n");
    }

    /*2. for Packet Tx */
    show_banner = TRUE;
    SYS_DMA_DUMP("\n*********** Packet TX *************\n");
    if (CTC_IS_BIT_SET(p_gb_dma_master[lchip]->dma_en_flag, CTC_DMA_FUNC_PACKET_TX))
    {
        for (index = 0; index < 3; index++)
        {
            _sys_greatbelt_dma_show_chan(lchip, &(p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_PACKET_TX][index]), &show_banner, detail);
        }
    }
    else
    {
        SYS_DMA_DUMP("\nDisabled!!!\n");
    }

    /*3. for Table Read */
    show_banner = TRUE;
    SYS_DMA_DUMP("\n*********** Table Read *************\n");
    if (CTC_IS_BIT_SET(p_gb_dma_master[lchip]->dma_en_flag, CTC_DMA_FUNC_TABLE_R))
    {
        _sys_greatbelt_dma_show_chan(lchip, p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_TABLE_R], &show_banner, detail);
    }
    else
    {
        SYS_DMA_DUMP("\nDisabled!!!\n");
    }

    /*4. for Table Write */
    show_banner = TRUE;
    SYS_DMA_DUMP("\n*********** Table Write *************\n");
    if (CTC_IS_BIT_SET(p_gb_dma_master[lchip]->dma_en_flag, CTC_DMA_FUNC_TABLE_W))
    {
        _sys_greatbelt_dma_show_chan(lchip, p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_TABLE_W], &show_banner, detail);
    }
    else
    {
        SYS_DMA_DUMP("\nDisabled!!!\n");
    }

    /*5. for stats */
    show_banner = TRUE;
    SYS_DMA_DUMP("\n*********** Statistics *************\n");
    if (CTC_IS_BIT_SET(p_gb_dma_master[lchip]->dma_en_flag, CTC_DMA_FUNC_STATS))
    {
        _sys_greatbelt_dma_show_chan(lchip, p_gb_dma_master[lchip]->p_dma_chan[CTC_DMA_FUNC_STATS], &show_banner, detail);
    }
    else
    {
        SYS_DMA_DUMP("\nDisabled!!!\n");
    }

    /*6. for HW Learning */
    show_banner = TRUE;
    SYS_DMA_DUMP("\n*********** HW learning *************\n");
    if (CTC_IS_BIT_SET(p_gb_dma_master[lchip]->dma_en_flag, CTC_DMA_FUNC_HW_LEARNING))
    {

        for (index = 0; index < 4; index++)
        {
            SYS_DMA_DUMP("ChanId:0x%x, Mem Block Index:0x%x, threshold:0x%x, max_learning_entry:0x%x, Cur_learning cnt:0x%x\n",
                       p_gb_dma_master[lchip]->sys_learning_dma.channel_id, index, SYS_DMA_LEARNING_THRESHOLD,
                       p_gb_dma_master[lchip]->sys_learning_dma.max_mem0_learning_num, g_sys_cur_learning_cnt[index]);
        }
        SYS_DMA_DUMP("membase0:%p, membase1:%p, membase2:%p, membase3:%p \n",
                       p_gb_dma_master[lchip]->sys_learning_dma.p_mem0_base, p_gb_dma_master[lchip]->sys_learning_dma.p_mem1_base,
                       p_gb_dma_master[lchip]->sys_learning_dma.p_mem2_base, p_gb_dma_master[lchip]->sys_learning_dma.p_mem3_base);

        SYS_DMA_DUMP("Current memory block index: 0x%x \n",
                       p_gb_dma_master[lchip]->sys_learning_dma.cur_block_idx);
    }
    else
    {
        SYS_DMA_DUMP("\nDisabled!!!\n");
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_dma_show_desc(uint8 lchip, ctc_dma_func_type_t type, uint8 chan_id)
{
    uint32 show_banner = TRUE;
    sys_dma_chan_t* p_dma_chan = NULL;
    SYS_DMA_INIT_CHECK(lchip);

    switch(type)
    {
    case CTC_DMA_FUNC_PACKET_RX:
        if (!CTC_IS_BIT_SET(p_gb_dma_master[lchip]->dma_en_flag, type))
        {
            SYS_DMA_DUMP("DMA function is not enable!\n");
            return CTC_E_NONE;
        }
        if (chan_id >= p_gb_dma_master[lchip]->pkt_rx_chan_num)
        {
            return CTC_E_DMA_INVALID_CHAN_ID;
        }
        p_dma_chan = &(p_gb_dma_master[lchip]->p_dma_chan[type][chan_id]);
        break;

    case CTC_DMA_FUNC_PACKET_TX:
        if (!CTC_IS_BIT_SET(p_gb_dma_master[lchip]->dma_en_flag, type))
        {
            SYS_DMA_DUMP("DMA function is not enable!\n");
            return 0;
        }
        p_dma_chan = &(p_gb_dma_master[lchip]->p_dma_chan[type][chan_id]);
        break;

    case CTC_DMA_FUNC_TABLE_R:
    case CTC_DMA_FUNC_TABLE_W:
    case CTC_DMA_FUNC_STATS:
        if (!CTC_IS_BIT_SET(p_gb_dma_master[lchip]->dma_en_flag, type))
        {
            SYS_DMA_DUMP("DMA function is not enable!\n");
            return 0;
        }
        p_dma_chan = p_gb_dma_master[lchip]->p_dma_chan[type];
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return _sys_greatbelt_dma_show_chan(lchip, p_dma_chan, &show_banner, TRUE);
}

extern int32
_sys_greatbelt_dma_isr_func_tx(uint8 lchip, uint8 chan);

extern int32
_sys_greatbelt_dma_stats_func(uint8 lchip, uint8 chan);

#if 0
void dma_stats_thread(void* para)
{
    uint8 lchip = 0;
    sys_dma_chan_t* p_stats_cfg;
    uint16 desc_index = 0;

    p_stats_cfg = &p_gb_dma_master[lchip]->sys_stats_dma;

    while(1)
    {
    #if 1
        /* scan all dma stas desc */
        if (0 == p_stats_cfg->p_desc[desc_index%2].pci_exp_desc.desc_done)
        {
            desc_index++;
            continue;
        }
        else
        {
            desc_index++;
            _sys_greatbelt_dma_stats_func(lchip,6);
        }
   #endif
        sal_task_sleep(10);
    }
}

extern int32
_sys_greatbelt_dma_pkt_rx_func(uint8 lchip, uint8 chan);

void dma_pktrx_thread(void* para)
{
    sys_dma_chan_t* p_pkt_rx_cfg;
    uint16 desc_index = 0;
    uint8 lchip = 0;
    p_pkt_rx_cfg = &p_gb_dma_master[lchip]->sys_packet_rx_dma[0];

    while(1)
    {
    #if 1
        /* scan all dma stas desc */
        if (0 == p_pkt_rx_cfg->p_desc[desc_index%16].pci_exp_desc.desc_done)
        {
            desc_index++;
            continue;
        }
        else
        {
             /*-SYS_DMA_DUMP("DMA Receive Packet!!desc_index:0x%x\n", desc_index);*/
            desc_index++;
            _sys_greatbelt_dma_pkt_rx_func(lchip,1);
            /*- _sys_greatbelt_dma_stats_func(lchip,6);*/
        }
   #endif
        sal_task_sleep(10);
    }
}
#endif
int32
sys_greatbelt_dma_clear_pkt_stats(uint8 lchip, uint8 para)
{
    SYS_DMA_INIT_CHECK(lchip);

    CTC_ERROR_RETURN(sys_greatbelt_dma_sync_pkt_stats(lchip));

    if (para == 0)    /*clear all*/
    {
        sal_memset(&(p_gb_dma_master[lchip]->dma_stats), 0, sizeof(sys_dma_stats_t));
    }
    else if (para == 1)  /*clear rx*/
    {
        p_gb_dma_master[lchip]->dma_stats.rx_good_cnt   = 0;
        p_gb_dma_master[lchip]->dma_stats.rx_good_byte  = 0;
        p_gb_dma_master[lchip]->dma_stats.rx_bad_cnt    = 0;
        p_gb_dma_master[lchip]->dma_stats.rx_bad_byte   = 0;
    }
    else            /*clear tx*/
    {
        p_gb_dma_master[lchip]->dma_stats.tx_total_cnt  = 0;
        p_gb_dma_master[lchip]->dma_stats.tx_total_byte = 0;
        p_gb_dma_master[lchip]->dma_stats.tx_error_cnt  = 0;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_dma_show_stats(uint8 lchip)
{

    SYS_DMA_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_dma_sync_pkt_stats(lchip));

    SYS_DMA_DUMP("\n");
    SYS_DMA_DUMP("Tx:\n");
    SYS_DMA_DUMP("    Good packets:%-20"PRIu64"\n", p_gb_dma_master[lchip]->dma_stats.rx_good_cnt);
    SYS_DMA_DUMP("    Good   bytes:%-20"PRIu64"\n", p_gb_dma_master[lchip]->dma_stats.rx_good_byte);
    SYS_DMA_DUMP("    Bad  packets:%-20"PRIu64"\n", p_gb_dma_master[lchip]->dma_stats.rx_bad_cnt);
    SYS_DMA_DUMP("    Bad    bytes:%-20"PRIu64"\n", p_gb_dma_master[lchip]->dma_stats.rx_bad_byte);
    SYS_DMA_DUMP("\n");
    SYS_DMA_DUMP("Rx:\n");
    SYS_DMA_DUMP("    Total packets:%-20"PRIu64"\n", p_gb_dma_master[lchip]->dma_stats.tx_total_cnt);
    SYS_DMA_DUMP("    Total   bytes:%-20"PRIu64"\n", p_gb_dma_master[lchip]->dma_stats.tx_total_byte);
    SYS_DMA_DUMP("    Error packets:%-20"PRIu64"\n", p_gb_dma_master[lchip]->dma_stats.tx_error_cnt);
    SYS_DMA_DUMP("\n");

    return CTC_E_NONE;
}
