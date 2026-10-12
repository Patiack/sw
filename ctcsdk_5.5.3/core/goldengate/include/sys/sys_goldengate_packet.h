/**
 @file sys_goldengate_packet.h

 @date 2012-10-23

 @version v2.0

*/
#ifndef _SYS_GOLDENGATE_PACKET_H
#define _SYS_GOLDENGATE_PACKET_H
#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_packet.h"
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
    uint16 header_len;
    uint32 data_len;
    uint8* data;
    uint8* header;
    uint8 is_async;
    uint8 user_flag; /*data buf pointer from with ctc_pkt_tx_t->data*/
    ctc_pkt_info_t tx_info;
    ctc_pkt_tx_t* p_pkt_tx;
    ctc_pkt_callback callback;  /**< [GB.GG] Packet Tx async callback function */
    void*           user_data;
};
typedef struct sys_pkt_tx_info_s sys_pkt_tx_info_t;

enum sys_packet_tx_type_e
{
    SYS_PKT_TX_TYPE_TPID,
    SYS_PKT_TX_TYPE_STACKING_EN,
    SYS_PKT_TX_TYPE_C2C_SUB_QUEUE_ID,
    SYS_PKT_TX_TYPE_PTP_ADJUST_OFFSET,
    SYS_PKT_TX_TYPE_VLAN_PTR,
    SYS_PKT_TX_MAX_TYPE
};
typedef enum sys_packet_tx_type_e sys_packet_tx_type_t;
/**
 @brief packet RX in DMA mode, called by DMA RX function
*/
extern int32
sys_goldengate_packet_rx(uint8 lchip, ctc_pkt_rx_t* p_pkt_rx);

/**
 @brief packet TX in DMA mode, call DMA TX function
*/
extern int32
sys_goldengate_packet_tx(uint8 lchip, ctc_pkt_tx_t* p_pkt_tx);

/**
 @brief encapsulate CPUMAC Header (in ETH mode) and Packet Header for TX packet
*/
extern int32
sys_goldengate_packet_encap(uint8 lchip, ctc_pkt_tx_t* p_pkt_tx);

/**
 @brief decapsulate CPUMAC Header (in ETH mode) and Packet Header for RX packet
*/
extern int32
sys_goldengate_packet_decap(uint8 lchip, ctc_pkt_rx_t* p_pkt_rx);

/**
 @brief register internal RX callback
*/
int32
sys_goldengate_packet_register_internal_rx_cb(uint8 lchip, SYS_PKT_RX_CALLBACK oam_rx_cb);

int32
sys_goldengate_set_pkt_rx_cb(uint8 lchip, CTC_PKT_RX_CALLBACK cb);

int32
sys_goldengate_get_pkt_rx_cb(uint8 lchip, void** cb);

int32
sys_goldengate_packet_register_tx_cb(uint8 lchip, CTC_PKT_TX_CALLBACK tx_cb);

int32
sys_goldengate_packet_tx_set_property(uint8 lchip, uint16 type, uint32 value1, uint32 value2);

/**
 @brief Initialize packet module
*/
extern int32
sys_goldengate_packet_init(uint8 lchip, void* p_global_cfg);

/**
 @brief De-Initialize packet module
*/
extern int32
sys_goldengate_packet_deinit(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif

