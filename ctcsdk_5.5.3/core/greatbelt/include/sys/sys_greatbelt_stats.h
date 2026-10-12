/**
 @file sys_greatbelt_stats.h

 @date 2009-12-15

 @version v2.0

*/
#ifndef _SYS_GREATBELT_STATS_H
#define _SYS_GREATBELT_STATS_H
#ifdef __cplusplus
extern "C" {
#endif
/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_const.h"
#include "ctc_stats.h"
#include "ctc_linklist.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
#define SYS_STATS_DBG_INFO(FMT, ...)                          \
    do                                                     \
    {                                                      \
        CTC_DEBUG_OUT_INFO(stats, stats, STATS_SYS, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_STATS_DBG_FUNC()                          \
    do \
    { \
        CTC_DEBUG_OUT_FUNC(stats, stats, STATS_SYS); \
    } while (0)

#define STATS_LOCK   \
    if (gb_stats_master[lchip]->p_stats_mutex) sal_mutex_lock(gb_stats_master[lchip]->p_stats_mutex)

#define STATS_UNLOCK \
    if (gb_stats_master[lchip]->p_stats_mutex) sal_mutex_unlock(gb_stats_master[lchip]->p_stats_mutex)

#define CTC_ERROR_RETURN_WITH_STATS_UNLOCK(op) \
    do { \
        int32 rv; \
        if ((rv = (op)) < 0) \
        { \
            sal_mutex_unlock(gb_stats_master[lchip]->p_stats_mutex); \
            return (rv); \
        } \
    } while (0)

#define MAC_BASED_STATS_GMAC_RAM_DEPTH      144u
#define MAC_BASED_STATS_SGMAC_RAM_DEPTH     40u
#define MAC_BASED_STATS_CPU_RAM_DEPTH       1u
#define GMAC_STATS_PORT_MAX                 4u
#define GMAC_STATS_RAM_MAX                  12u
#define SGMAC_STATS_RAM_MAX                 12u
#define SYS_STATS_MTU1_PKT_DFT_LENGTH       1518u
#define SYS_STATS_MTU2_PKT_DFT_LENGTH       1536u

enum sys_stats_mac_ram_e
{
    SYS_STATS_MAC_STATS_RAM0,
    SYS_STATS_MAC_STATS_RAM1,
    SYS_STATS_MAC_STATS_RAM2,
    SYS_STATS_MAC_STATS_RAM3,
    SYS_STATS_MAC_STATS_RAM4,
    SYS_STATS_MAC_STATS_RAM5,
    SYS_STATS_MAC_STATS_RAM6,
    SYS_STATS_MAC_STATS_RAM7,
    SYS_STATS_MAC_STATS_RAM8,
    SYS_STATS_MAC_STATS_RAM9,
    SYS_STATS_MAC_STATS_RAM10,
    SYS_STATS_MAC_STATS_RAM11,

    SYS_STATS_SGMAC_STATS_RAM0,
    SYS_STATS_SGMAC_STATS_RAM1,
    SYS_STATS_SGMAC_STATS_RAM2,
    SYS_STATS_SGMAC_STATS_RAM3,
    SYS_STATS_SGMAC_STATS_RAM4,
    SYS_STATS_SGMAC_STATS_RAM5,
    SYS_STATS_SGMAC_STATS_RAM6,
    SYS_STATS_SGMAC_STATS_RAM7,
    SYS_STATS_SGMAC_STATS_RAM8,
    SYS_STATS_SGMAC_STATS_RAM9,
    SYS_STATS_SGMAC_STATS_RAM10,
    SYS_STATS_SGMAC_STATS_RAM11,

    SYS_STATS_CPUMAC_STATS_RAM,

    SYS_STATS_MAC_STATS_RAM_MAX
};
typedef enum sys_stats_mac_ram_e sys_stats_mac_ram_t;

/*mac statistics type*/
enum sys_stats_mac_rec_stats_type_e
{
    SYS_STATS_MAC_RCV_GOOD_UCAST,
    SYS_STATS_MAC_RCV_GOOD_MCAST,
    SYS_STATS_MAC_RCV_GOOD_BCAST,
    SYS_STATS_MAC_RCV_GOOD_NORMAL_PAUSE,
    SYS_STATS_MAC_RCV_GOOD_PFC_PAUSE,
    SYS_STATS_MAC_RCV_GOOD_CONTROL,
    SYS_STATS_MAC_RCV_FCS_ERROR,
    SYS_STATS_MAC_RCV_MAC_OVERRUN,
    SYS_STATS_MAC_RCV_GOOD_63B,
    SYS_STATS_MAC_RCV_BAD_63B,
    SYS_STATS_MAC_RCV_GOOD_1519B,
    SYS_STATS_MAC_RCV_BAD_1519B,
    SYS_STATS_MAC_RCV_GOOD_JUMBO,
    SYS_STATS_MAC_RCV_BAD_JUMBO,
    SYS_STATS_MAC_RCV_64B,
    SYS_STATS_MAC_RCV_127B,
    SYS_STATS_MAC_RCV_255B,
    SYS_STATS_MAC_RCV_511B,
    SYS_STATS_MAC_RCV_1023B,
    SYS_STATS_MAC_RCV_1518B,

    SYS_STATS_MAC_RCV_MAX,
    SYS_STATS_MAC_RCV_NUM = SYS_STATS_MAC_RCV_MAX
};
typedef enum sys_stats_mac_rec_stats_type_e sys_stats_mac_rec_stats_type_t;

/* the value defined should be consistent with ctc_stats.h */
enum sys_stats_map_mac_rx_type_e
{
    SYS_STATS_MAP_MAC_RX_GOOD_UC_PKT = 0,
    SYS_STATS_MAP_MAC_RX_GOOD_UC_BYTE = 1,
    SYS_STATS_MAP_MAC_RX_GOOD_MC_PKT = 2,
    SYS_STATS_MAP_MAC_RX_GOOD_MC_BYTE = 3,
    SYS_STATS_MAP_MAC_RX_GOOD_BC_PKT = 4,
    SYS_STATS_MAP_MAC_RX_GOOD_BC_BYTE = 5,
    SYS_STATS_MAP_MAC_RX_GOOD_NORMAL_PAUSE_PKT = 8,
    SYS_STATS_MAP_MAC_RX_GOOD_NORMAL_PAUSE_BYTE = 9,
    SYS_STATS_MAP_MAC_RX_GOOD_PFC_PAUSE_PKT = 10,
    SYS_STATS_MAP_MAC_RX_GOOD_PFC_PAUSE_BYTE = 11,
    SYS_STATS_MAP_MAC_RX_GOOD_CONTROL_PKT = 12,
    SYS_STATS_MAP_MAC_RX_GOOD_CONTROL_BYTE = 13,
    SYS_STATS_MAP_MAC_RX_FCS_ERROR_PKT = 14,
    SYS_STATS_MAP_MAC_RX_FCS_ERROR_BYTE = 15,
    SYS_STATS_MAP_MAC_RX_MAC_OVERRUN_PKT = 16,
    SYS_STATS_MAP_MAC_RX_MAC_OVERRUN_BYTE = 17,
    SYS_STATS_MAP_MAC_RX_GOOD_63B_PKT = 22,
    SYS_STATS_MAP_MAC_RX_GOOD_63B_BYTE = 23,
    SYS_STATS_MAP_MAC_RX_BAD_63B_PKT = 24,
    SYS_STATS_MAP_MAC_RX_BAD_63B_BYTE = 25,
    SYS_STATS_MAP_MAC_RX_GOOD_1519B_PKT = 26,
    SYS_STATS_MAP_MAC_RX_GOOD_1519B_BYTE = 27,
    SYS_STATS_MAP_MAC_RX_BAD_1519B_PKT = 28,
    SYS_STATS_MAP_MAC_RX_BAD_1519B_BYTE = 29,
    SYS_STATS_MAP_MAC_RX_GOOD_JUMBO_PKT = 30,
    SYS_STATS_MAP_MAC_RX_GOOD_JUMBO_BYTE = 31,
    SYS_STATS_MAP_MAC_RX_BAD_JUMBO_PKT = 32,
    SYS_STATS_MAP_MAC_RX_BAD_JUMBO_BYTE = 33,
    SYS_STATS_MAP_MAC_RX_64B_PKT = 34,
    SYS_STATS_MAP_MAC_RX_64B_BYTE = 35,
    SYS_STATS_MAP_MAC_RX_127B_PKT = 36,
    SYS_STATS_MAP_MAC_RX_127B_BYTE = 37,
    SYS_STATS_MAP_MAC_RX_255B_PKT = 38,
    SYS_STATS_MAP_MAC_RX_255B_BYTE = 39,
    SYS_STATS_MAP_MAC_RX_511B_PKT = 40,
    SYS_STATS_MAP_MAC_RX_511B_BYTE = 41,
    SYS_STATS_MAP_MAC_RX_1023B_PKT = 42,
    SYS_STATS_MAP_MAC_RX_1023B_BYTE = 43,
    SYS_STATS_MAP_MAC_RX_1518B_PKT = 44,
    SYS_STATS_MAP_MAC_RX_1518B_BYTE = 45,

    SYS_STATS_MAP_MAC_RX_TYPE_NUM = 46
};
typedef enum sys_stats_map_mac_rx_type_e sys_stats_map_mac_rx_type_t;

enum sys_stats_mac_snd_stats_type_e
{
    SYS_STATS_MAC_SEND_UCAST = 0x14,
    SYS_STATS_MAC_SEND_MCAST,
    SYS_STATS_MAC_SEND_BCAST,
    SYS_STATS_MAC_SEND_PAUSE,
    SYS_STATS_MAC_SEND_CONTROL,
    SYS_STATS_MAC_SEND_FCS_ERROR,
    SYS_STATS_MAC_SEND_MAC_UNDERRUN,
    SYS_STATS_MAC_SEND_63B,
    SYS_STATS_MAC_SEND_64B,
    SYS_STATS_MAC_SEND_127B,
    SYS_STATS_MAC_SEND_255B,
    SYS_STATS_MAC_SEND_511B,
    SYS_STATS_MAC_SEND_1023B,
    SYS_STATS_MAC_SEND_1518B,
    SYS_STATS_MAC_SEND_1519B,
    SYS_STATS_MAC_SEND_JUMBO,

    SYS_STATS_MAC_SEND_MAX,
    SYS_STATS_MAC_STATS_TYPE_NUM = SYS_STATS_MAC_SEND_MAX,
    SYS_STATS_MAC_SEND_NUM = 16
};
typedef enum sys_stats_mac_snd_stats_type_e sys_stats_mac_snd_stats_type_t;

/* the value defined should be consistent with ctc_stats.h */
enum sys_stats_map_mac_tx_type_e
{
    SYS_STATS_MAP_MAC_TX_GOOD_UC_PKT = 0,
    SYS_STATS_MAP_MAC_TX_GOOD_UC_BYTE = 1,
    SYS_STATS_MAP_MAC_TX_GOOD_MC_PKT = 2,
    SYS_STATS_MAP_MAC_TX_GOOD_MC_BYTE = 3,
    SYS_STATS_MAP_MAC_TX_GOOD_BC_PKT = 4,
    SYS_STATS_MAP_MAC_TX_GOOD_BC_BYTE = 5,
    SYS_STATS_MAP_MAC_TX_GOOD_PAUSE_PKT = 6,
    SYS_STATS_MAP_MAC_TX_GOOD_PAUSE_BYTE = 7,
    SYS_STATS_MAP_MAC_TX_GOOD_CONTROL_PKT = 8,
    SYS_STATS_MAP_MAC_TX_GOOD_CONTROL_BYTE = 9,
    SYS_STATS_MAP_MAC_TX_FCS_ERROR_PKT = 30,
    SYS_STATS_MAP_MAC_TX_FCS_ERROR_BYTE = 31,
    SYS_STATS_MAP_MAC_TX_MAC_UNDERRUN_PKT = 28,
    SYS_STATS_MAP_MAC_TX_MAC_UNDERRUN_BYTE = 29,
    SYS_STATS_MAP_MAC_TX_63B_PKT = 10,
    SYS_STATS_MAP_MAC_TX_63B_BYTE = 11,
    SYS_STATS_MAP_MAC_TX_64B_PKT = 12,
    SYS_STATS_MAP_MAC_TX_64B_BYTE = 13,
    SYS_STATS_MAP_MAC_TX_127B_PKT = 14,
    SYS_STATS_MAP_MAC_TX_127B_BYTE = 15,
    SYS_STATS_MAP_MAC_TX_225B_PKT = 16,
    SYS_STATS_MAP_MAC_TX_225B_BYTE = 17,
    SYS_STATS_MAP_MAC_TX_511B_PKT = 18,
    SYS_STATS_MAP_MAC_TX_511B_BYTE = 19,
    SYS_STATS_MAP_MAC_TX_1023B_PKT = 20,
    SYS_STATS_MAP_MAC_TX_1023B_BYTE = 21,
    SYS_STATS_MAP_MAC_TX_1518B_PKT = 22,
    SYS_STATS_MAP_MAC_TX_1518B_BYTE = 23,
    SYS_STATS_MAP_MAC_TX_1519B_PKT = 24,
    SYS_STATS_MAP_MAC_TX_1519B_BYTE = 25,
    SYS_STATS_MAP_MAC_TX_JUMBO_PKT = 26,
    SYS_STATS_MAP_MAC_TX_JUMBO_BYTE = 27,

    SYS_STATS_MAP_MAC_TX_TYPE_NUM
};
typedef enum sys_stats_map_mac_tx_type_e sys_stats_map_mac_tx_type_t;


enum sys_stats_cpu_mac_stats_type_e
{
    SYS_STATS_CPU_MAC_RCV_GOOD_PKT,
    SYS_STATS_CPU_MAC_RCV_GOOD_BYTE,
    SYS_STATS_CPU_MAC_RCV_BAD_PKT,
    SYS_STATS_CPU_MAC_RCV_BAD_BYTE,
    SYS_STATS_CPU_MAC_RCV_FCS_ERROR,
    SYS_STATS_CPU_MAC_RCV_FRAGMENT,
    SYS_STATS_CPU_MAC_RCV_OVERRUN,
    SYS_STATS_CPU_MAC_SEND_TOTAL_PKT,
    SYS_STATS_CPU_MAC_SEND_TOTAL_BYTE,
    SYS_STATS_CPU_MAC_SEND_FCS_ERROR,
    SYS_STATS_CPU_MAC_SEND_UNDERRUN,
    SYS_STATS_CPU_MAC_MAX
};
typedef enum sys_stats_cpu_mac_stats_type_e sys_stats_cpu_mac_stats_type_t;

/* port statistics type */
enum sys_stats_port_based_mac_stats_type_e
{
    SYS_STATS_PORT_UCAST,
    SYS_STATS_PORT_ROUTED_MAC,
    SYS_STATS_PORT_MCAST,
    SYS_STATS_PORT_BCAST,

    SYS_STATS_PORT_BASED_MAC_MAX
};
typedef enum sys_stats_port_based_mac_stats_type_e sys_stats_port_based_mac_stats_type_t;

enum sys_stats_port_based_protocol_stats_type_e
{
    SYS_STATS_PORT_IPV4,
    SYS_STATS_PORT_IPV6,
    SYS_STATS_PORT_MPLS,
    SYS_STATS_PORT_PROTOCOL_DEFAULT,

    SYS_STATS_PORT_BASED_PROTOCOL_MAX
};
typedef enum sys_stats_port_based_protocol_stats_type_e sys_stats_port_based_protocol_stats_type_t;

/*ingress phb type*/
enum sys_stats_igs_phb_stats_type_e
{
    SYS_STATS_INGRESS_PHB_IN,
    SYS_STATS_INGRESS_PHB_OUT,
    SYS_STATS_INGRESS_PHB_MARKDOWN,
    SYS_STATS_INGRESS_PHB_MARKDROP,

    SYS_STATS_INGRESS_PHB_MAX
};
typedef enum sys_stats_igs_phb_stats_type_e sys_stats_igs_phb_stats_type_t;

/*egress phb type*/
enum sys_stats_egs_phb_stats_type_e
{
    SYS_STATS_EGRESS_PHB_OUT,
    SYS_STATS_EGRESS_PHB_MARKDOWN,

    SYS_STATS_EGRESS_PHB_MAX
};
typedef enum sys_stats_egs_phb_stats_type_e sys_stats_egs_phb_stats_type_t;

/* ingress global forwarding stats type */
enum sys_stats_igs_global_fwd_stats_type_e
{
    SYS_STATS_IPV4_UCAST_ROUTED,
    SYS_STATS_IPV4_MCAST_ROUTED,
    SYS_STATS_IPV6_UCAST_ROUTED,
    SYS_STATS_IPV6_MCAST_ROUTED,
    SYS_STATS_IPV4_UCAST_ROUTE_ESCAPE,
    SYS_STATS_IPV4_MCAST_ROUTE_ESCAPE,
    SYS_STATS_IPV6_UCAST_ROUTE_ESCAPE,
    SYS_STATS_IPV6_MCAST_ROUTE_ESCAPE,
    SYS_STATS_MPLS_UCAST_SWITCHED,
    SYS_STATS_MPLS_MCAST_SWITCHED,
    SYS_STATS_MPLS_UCAST_SWITCH_FATAL,
    SYS_STATS_MPLS_MCAST_SWITCH_FATAL,
    SYS_STATS_BRIDGE_BCAST,
    SYS_STATS_BRIDGE_MCAST,
    SYS_STATS_BRIDGE_UCAST,
    SYS_STATS_BRIDGE_ESCAPE,

    SYS_STATS_INGRESS_GLOBAL_FWD_MAX
};
typedef enum sys_stats_igs_global_fwd_stats_type_e sys_stats_igs_global_fwd_stats_type_t;

/* egress global forwarding stats type */
enum sys_stats_egs_global_fwd_stats_type_e
{
    SYS_STATS_PAYLOAD_IPV4_UCAST_ROUTED,
    SYS_STATS_PAYLOAD_IPV4_MCAST_ROUTED,
    SYS_STATS_PAYLOAD_IPV6_UCAST_ROUTED,
    SYS_STATS_PAYLOAD_IPV6_MCAST_ROUTED,
    SYS_STATS_PAYLOAD_BRIDGE_UCAST,
    SYS_STATS_PAYLOAD_BRIDGE_MCAST,
    SYS_STATS_PAYLOAD_BRIDGE_BCAST,
    SYS_STATS_PAYLOAD_NO_OP,
    SYS_STATS_L3EDIT_NO_OP,
    SYS_STATS_L3EDIT_MPLS,
    SYS_STATS_L3EDIT_NAT,
    SYS_STATS_L3EDIT_TUNNEL,
    SYS_STATS_L3EDIT_FLEX,
    SYS_STATS_L2EDIT_NO_OP,
    SYS_STATS_L2EDIT_ETH,
    SYS_STATS_L2EDIT_FLEX,

    SYS_STATS_EGRESS_GLOBAL_FWD_MAX
};
typedef enum sys_stats_egs_global_fwd_stats_type_e sys_stats_egs_global_fwd_stats_type_t;

/* flow stats type */
enum sys_stats_flow_type_e
{
    SYS_STATS_FLOW_TYPE_ACL,
    SYS_STATS_FLOW_TYPE_DSFWD,
    SYS_STATS_FLOW_TYPE_MPLS,
    SYS_STATS_FLOW_TYPE_IP_TUNNEL,
    SYS_STATS_FLOW_TYPE_SCL,
    SYS_STATS_FLOW_TYPE_NEXTHOP,
    SYS_STATS_FLOW_TYPE_L3EDIT_MPLS,

    SYS_STATS_FLOW_TYPE_MAX
};
typedef enum sys_stats_flow_type_e sys_stats_flow_type_t;

enum sys_stats_mac_dir_e
{
    SYS_STATS_MAC_DIR_RX,
    SYS_STATS_MAC_DIR_TX,
    SYS_STATS_MAC_DIR_NUM
};
typedef enum sys_stats_mac_dir_e sys_stats_mac_dir_t;

enum sys_stats_map_dir_e
{
    SYS_STATS_MAP_SYS,
    SYS_STATS_MAP_CTC,
    SYS_STATS_MAP_NUM
};
typedef enum sys_stats_map_dir_e sys_stats_map_dir_t;

enum sys_stats_mac_cnt_type_e
{
    SYS_STATS_MAC_CNT_TYPE_PKT,
    SYS_STATS_MAC_CNT_TYPE_BYTE,
    SYS_STATS_MAC_CNT_TYPE_NUM
};
typedef enum sys_stats_mac_cnt_type_e sys_stats_mac_cnt_type_t;

/*policing storage structor*/
struct sys_stats_policing_s
{
    uint64 policing_confirm_pkts;
    uint64 policing_confirm_bytes;
    uint64 policing_exceed_pkts;
    uint64 policing_exceed_bytes;
    uint64 policing_violate_pkts;
    uint64 policing_violate_bytes;
};
typedef struct sys_stats_policing_s sys_stats_policing_t;

/*queue storage structor*/
struct sys_stats_queue_s
{
    uint64 queue_drop_pkts;
    uint64 queue_drop_bytes;
    uint64 queue_deq_pkts;
    uint64 queue_deq_bytes;
};
typedef struct sys_stats_queue_s sys_stats_queue_t;

struct sys_stats_fwd_stats_s
{
    uint16 stats_ptr;
    uint64 packet_count;
    uint64 byte_count;
};
typedef struct sys_stats_fwd_stats_s sys_stats_fwd_stats_t;

struct sys_stats_dma_sync_s
{
    uint32 stats_type;
    void*  p_base_addr;
};
typedef struct sys_stats_dma_sync_s sys_stats_dma_sync_t;

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
extern int32
sys_greatbelt_stats_init(uint8 lchip, ctc_stats_global_cfg_t* stats_global_cfg);

/**
 @brief De-Initialize stats module
*/
extern int32
sys_greatbelt_stats_deinit(uint8 lchip);

/*Mac Based Stats*/
extern int32
sys_greatbelt_stats_set_mac_packet_length_mtu1(uint8 lchip, uint16 gport, uint16 length);
extern int32
sys_greatbelt_stats_get_mac_packet_length_mtu1(uint8 lchip, uint16 gport, uint16* p_length);

extern int32
sys_greatbelt_stats_set_mac_packet_length_mtu2(uint8 lchip, uint16 gport, uint16 length);
extern int32
sys_greatbelt_stats_get_mac_packet_length_mtu2(uint8 lchip, uint16 gport, uint16* p_length);

extern int32
sys_greatbelt_stats_get_mac_rx_stats(uint8 lchip, uint16 gport, ctc_mac_stats_t* p_stats);
extern int32
sys_greatbelt_stats_clear_mac_rx_stats(uint8 lchip, uint16 gport);
extern int32
sys_greatbelt_stats_get_mac_tx_stats(uint8 lchip, uint16 gport, ctc_mac_stats_t* p_stats);
extern int32
sys_greatbelt_stats_clear_mac_tx_stats(uint8 lchip, uint16 gport);
extern int32
sys_greatbelt_stats_get_cpu_mac_stats(uint8 lchip, uint16 gport, ctc_cpu_mac_stats_t* p_stats);
extern int32
sys_greatbelt_stats_clear_cpu_mac_stats(uint8 lchip, uint16 gport);
/*Port log*/
extern int32
sys_greatbelt_stats_set_drop_packet_stats_en(uint8 lchip, ctc_stats_discard_t bitmap, bool enable);
extern int32
sys_greatbelt_stats_get_port_log_discard_stats_enable(uint8 lchip, ctc_stats_discard_t bitmap, bool* enable);
extern int32
sys_greatbelt_stats_get_igs_port_log_stats(uint8 lchip, uint16 gport, ctc_stats_basic_t* p_stats);
extern int32
sys_greatbelt_stats_clear_igs_port_log_stats(uint8 lchip, uint16 gport);
extern int32
sys_greatbelt_stats_get_egs_port_log_stats(uint8 lchip, uint16 gport, ctc_stats_basic_t* p_stats);
extern int32
sys_greatbelt_stats_clear_egs_port_log_stats(uint8 lchip, uint16 gport);

/*Forwarding Stats*/
extern int32
sys_greatbelt_stats_alloc_flow_stats_ptr(uint8 lchip, sys_stats_flow_type_t flow_stats_type, uint16* p_stats_ptr);
extern int32
sys_greatbelt_stats_free_flow_stats_ptr(uint8 lchip, sys_stats_flow_type_t flow_stats_type, uint16 stats_ptr);
extern int32
sys_greatbelt_stats_get_flow_stats(uint8 lchip,
                                   uint16 stats_ptr,
                                   ctc_stats_phb_t* phb,
                                   ctc_stats_basic_t* p_stats);
extern int32
sys_greatbelt_stats_clear_flow_stats(uint8 lchip,
                                     uint16 stats_ptr,
                                     ctc_stats_phb_t* phb);

extern int32
sys_greatbelt_stats_set_policing_en(uint8 lchip, uint8 enable);
extern int32
sys_greatbelt_stats_get_policing_en(uint8 lchip, uint8* p_enable);
extern int32
sys_greatbelt_stats_get_policing_stats(uint8 lchip, uint16 stats_ptr, sys_stats_policing_t* p_stats);
extern int32
sys_greatbelt_stats_clear_policing_stats(uint8 lchip, uint16 stats_ptr);
extern int32
sys_greatbelt_stats_set_queue_en(uint8 lchip, uint8 enable);
extern int32
sys_greatbelt_stats_get_queue_en(uint8 lchip, uint8* p_enable);
extern int32
sys_greatbelt_stats_get_queue_stats(uint8 lchip, uint16 stats_ptr, sys_stats_queue_t* p_stats);
extern int32
sys_greatbelt_stats_clear_queue_stats(uint8 lchip, uint16 stats_ptr);
extern int32
sys_greatbelt_stats_get_vlan_stats(uint8 lchip, uint16 vlan_id, ctc_direction_t dir, ctc_stats_basic_t* p_stats);
extern int32
sys_greatbelt_stats_clear_vlan_stats(uint8 lchip, uint16 vlan_id, ctc_direction_t dir);
extern int32
sys_greatbelt_stats_get_vrf_stats(uint8 lchip, uint16 vrfid, ctc_stats_basic_t* p_stats);
extern int32
sys_greatbelt_stats_clear_vrf_stats(uint8 lchip, uint16 vrfid);
extern int32
sys_greatbelt_stats_get_vrf_stats_global_en(uint8 lchip, uint8* enable);
extern int32
sys_greatbelt_stats_get_vrf_statsptr(uint8 lchip, uint16 vrfid, uint16* stats_ptr);

extern int32
sys_greatbelt_stats_set_saturate_en(uint8 lchip, ctc_stats_type_t stats_type, bool enable);
extern int32
sys_greatbelt_stats_get_saturate_en(uint8 lchip, ctc_stats_type_t stats_type, bool* p_enable);
extern int32
sys_greatbelt_stats_set_hold_en(uint8 lchip, ctc_stats_type_t stats_type, bool enable);
extern int32
sys_greatbelt_stats_get_hold_en(uint8 lchip, ctc_stats_type_t stats_type, bool* p_enable);
extern int32
sys_greatbelt_stats_set_clear_after_read_en(uint8 lchip, ctc_stats_type_t stats_type, bool enable);
extern int32
sys_greatbelt_stats_get_clear_after_read_en(uint8 lchip, ctc_stats_type_t stats_type, bool* p_enable);
extern int32
sys_greatbelt_stats_set_pkt_cnt_threshold(uint8 lchip, uint16 threshold);
extern int32
sys_greatbelt_stats_get_pkt_cnt_threshold(uint8 lchip, uint16* p_threshold);
extern int32
sys_greatbelt_stats_set_byte_cnt_threshold(uint8 lchip, uint16 threshold);
extern int32
sys_greatbelt_stats_get_byte_cnt_threshold(uint8 lchip, uint16* p_threshold);
extern int32
sys_greatbelt_stats_set_fifo_depth_threshold(uint8 lchip, uint8 threshold);
extern int32
sys_greatbelt_stats_get_fifo_depth_threshold(uint8 lchip, uint8* p_threshold);
extern int32
sys_greatbelt_stats_get_statsptr(uint8 lchip, uint32 stats_id, uint16* stats_ptr);
extern int32
sys_greatbelt_stats_create_statsid(uint8 lchip, ctc_stats_statsid_t* statsid);
extern int32
sys_greatbelt_stats_destroy_statsid(uint8 lchip, uint32 stats_id);
extern int32
sys_greatbelt_stats_get_stats(uint8 lchip, uint32 stats_id, ctc_stats_basic_t* p_stats);
extern int32
sys_greatbelt_stats_clear_stats(uint8 lchip, uint32 stats_id);
extern int32
sys_greatbelt_stats_intr_callback_func(uint8 lchip, uint8* p_gchip);
extern int32
sys_greatbelt_stats_isr(uint8 lchip, uint32 intr, void* p_data);
extern int32
sys_greatbelt_stats_sync_dma_mac_stats(uint8 lchip, sys_stats_dma_sync_t* p_dma_sync, uint16 bitmap);
extern int32
sys_greatbelt_stats_register_cb(uint8 lchip, ctc_stats_sync_fn_t cb, void* userdata);
extern int32
sys_greatbelt_stats_set_stats_interval(uint8 lchip, uint32 stats_interval);
#ifdef __cplusplus
}
#endif

#endif

