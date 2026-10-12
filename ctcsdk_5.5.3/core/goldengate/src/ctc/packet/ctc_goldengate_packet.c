/**
 @file ctc_goldengate_packet.c

 @author  Copyright (C) 2012 Centec Networks Inc.  All rights reserved.

 @date 2012-11-25

 @version v2.0

 This file define ctc functions

 \p The packet module is used for communication protocol packets between CPU and Greatbelt.
 \p There are two transport mode for packet: DMA or Ethernet.
 \p Goldengate will encapsulate CPUMAC (20B) + Packet (40B) for Ethernet mode.
 \p Goldengate will encapsulate Packet (40B) for DMA mode.

*/
/****************************************************************************
*
* Header Files
*
****************************************************************************/
#include "ctc_error.h"
#include "ctc_packet.h"
#include "ctc_goldengate_packet.h"
#include "sys_goldengate_packet.h"
#include "sys_goldengate_chip.h"

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

int32
ctc_goldengate_packet_tx(uint8 lchip, ctc_pkt_tx_t* p_pkt_tx)
{
    CTC_PTR_VALID_CHECK(p_pkt_tx);
    CTC_SET_API_LCHIP(lchip, p_pkt_tx->lchip);

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_PACKET);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_packet_tx(lchip, p_pkt_tx));

    return CTC_E_NONE;
}

int32
ctc_goldengate_packet_encap(uint8 lchip, ctc_pkt_tx_t* p_pkt_tx)
{


    CTC_PTR_VALID_CHECK(p_pkt_tx);
    CTC_SET_API_LCHIP(lchip, p_pkt_tx->lchip);
    FEATURE_SUPPORT_CHECK(CTC_FEATURE_PACKET);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_packet_encap(lchip, p_pkt_tx));

    return CTC_E_NONE;
}

int32
ctc_goldengate_packet_decap(uint8 lchip, ctc_pkt_rx_t* p_pkt_rx)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_PACKET);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_packet_decap(lchip, p_pkt_rx));

    return CTC_E_NONE;
}

int32
ctc_goldengate_packet_init(uint8 lchip, void* p_global_cfg)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_PACKET);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_packet_init(lchip, p_global_cfg));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_packet_deinit(uint8 lchip)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_PACKET);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_packet_deinit(lchip));
    }

    return CTC_E_NONE;
}

