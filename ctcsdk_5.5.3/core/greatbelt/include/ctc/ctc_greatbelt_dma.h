/**
    @file ctc_greatbelt_dma.h

    @author  Copyright (C) 2012 Centec Networks Inc.  All rights reserved.

    @date 2009-10-28

    @version v2.0

 \p DMA represents Direct Memory Access, which is used as block memory access by DMA controller.
 \p
 \p The DMA module provides functions such as PacketTx, PacketRx, Port Statistic, Hw Learning, Table Read,
    Table Write.
 \p
 \d    PactetRx: For receiving data (CHIP-CPU), the packet is read from Chip buffer which come from ingress port and
        writes the packet data into PCI memory for the CPU to process.
 \d    PactetTx: For transmitting data (CPU-CHIP), the device performs a DMA Read from PCI memory to obtain the packet data for
        transmission. The packet can be written directly into buffer memory for the desired egress port. In the later case,
        the forwarding decision will be based upon the results of the forwarding logic.
\d     Port Statistic: For Statistic function, DMA read statistic from port periodicity and writes data into PCI memory for CPU to
        process, which can prevent statistic overflow.
\d     Hw Learning: For Hw learning function, DMA performs operation to pop data from learning fib and write data into PCI memory
        for CPU process if have software table to synchronize.
\d     Table Read: For Table Read function, DMA performs read operation to get chip data and write data into PCI memory for CPU to
        process, notice the table address must continuous in physical.
\d     Table Write: For Table Write function, DMA read from PCI memory to obtain the data which is used for write into chip table and
        performs write operation, notice the table address must continuous in physical.
\p
    In addition, the chip supports four DMA channels that can be as PacketRx function, for packet taking different COS will be mapping
    to different Rx channel. The DMA controller transfers the packets based on a 32-bit boundary. Regardless of the byte count for each
    cell, the controller does a read/write to the PCI memory in 32-bit chunks.
\p
*/


#ifndef _CTC_GREATBELT_DMA_H
#define _CTC_GREATBELT_DMA_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_dma.h"

/**
 @brief Init dma module

 @param[in] lchip    local chip id

 @param[in] dma_global_cfg      packet DMA, table DMA, learning DMA and stats DMA information

 @remark   init dma module
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_dma_init(uint8 lchip, ctc_dma_global_cfg_t* dma_global_cfg);

/**
 @brief De-Initialize dma module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the dma configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_dma_deinit(uint8 lchip);

/**
 @brief send pkt

 @param[in] lchip    local chip id

 @param[in] tx_cfg              packet DMA TX, system buffer, buffer len and bridge header

 @remark    transmit packet by dma
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_dma_tx_pkt(uint8 lchip, ctc_pkt_tx_t* tx_cfg);

/**
 @brief read or write table

 @param[in] lchip    local chip id

 @param[in] tbl_cfg             table DMA, system buffer, buffer len and table address

 @remark   write table by dma
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_dma_rw_table(uint8 lchip, ctc_dma_tbl_rw_t* tbl_cfg);

#ifdef __cplusplus
}
#endif

#endif /*end of _CTC_GREATBELT_DMA_H*/

