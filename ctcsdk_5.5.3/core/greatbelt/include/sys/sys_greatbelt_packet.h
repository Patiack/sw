/**
 @file sys_greatbelt_packet.h

 @date 2012-10-23

 @version v2.0

*/
#ifndef _SYS_GREATBELT_PACKET_H
#define _SYS_GREATBELT_PACKET_H
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

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
typedef int32 (* SYS_PKT_RX_CALLBACK)(uint8 lchip, ctc_pkt_rx_t* p_pkt_rx);

struct sys_pkt_tx_info_s
{
    uint8 lchip;
    uint8 priority;
    uint16 header_len;
    uint32 data_len;
    uint8* data;
    uint8* header;
    uint8 is_async;
    uint8 user_flag; /*data buf pointer from with ctc_pkt_tx_t->data*/
    uint8 mode;
    ctc_pkt_tx_t* p_pkt_tx;
    ctc_pkt_callback callback;  /**< [GB.GG] Packet Tx async callback function */
    void*           user_data;
    uint8 flags;
};
typedef struct sys_pkt_tx_info_s sys_pkt_tx_info_t;


extern int32
sys_greatbelt_packet_swap_payload(uint8 lchip, uint32 len, uint8* p_payload);

extern int32
sys_greatbelt_packet_swap32(uint8 lchip, uint32* data, int32 len, uint32 hton);

/**
 @brief packet RX in DMA mode, called by DMA RX function
*/
extern int32
sys_greatbelt_packet_rx(uint8 lchip, ctc_pkt_rx_t* p_pkt_rx);

/**
 @brief packet TX in DMA mode, call DMA TX function
*/
extern int32
sys_greatbelt_packet_tx(uint8 lchip, ctc_pkt_tx_t* p_pkt_tx);

/**
 @brief encapsulate CPUMAC Header (in ETH mode) and Packet Header for TX packet
*/
extern int32
sys_greatbelt_packet_encap(uint8 lchip, ctc_pkt_tx_t* p_pkt_tx);

/**
 @brief decapsulate CPUMAC Header (in ETH mode) and Packet Header for RX packet
*/
extern int32
sys_greatbelt_packet_decap(uint8 lchip, ctc_pkt_rx_t* p_pkt_rx);

/**
 @brief register internal RX callback
*/
extern int32
sys_greatbelt_packet_register_internal_rx_cb(uint8 lchip, SYS_PKT_RX_CALLBACK internal_rx_cb);

/**
 @brief Initialize packet module
*/
extern int32
sys_greatbelt_packet_init(uint8 lchip, void* p_global_cfg);

/**
 @brief De-Initialize packet module
*/
extern int32
sys_greatbelt_packet_deinit(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif

