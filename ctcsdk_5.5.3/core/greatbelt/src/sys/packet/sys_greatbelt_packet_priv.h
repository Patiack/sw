/**
 @file sys_greatbelt_packet_priv.h

 @date 2012-10-31

 @version v2.0

*/
#ifndef _SYS_GREATBELT_PACKET_PRIV_H
#define _SYS_GREATBELT_PACKET_PRIV_H
#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/

#define SYS_PKT_DUMP(FMT, ...)      \
    do                                  \
    {                                   \
        CTC_DEBUG_OUT(packet, packet, PACKET_SYS, CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_PKT_DBG_INFO(FMT, ...)  \
    do                                  \
    {                                   \
        CTC_DEBUG_OUT_INFO(packet, packet, PACKET_SYS, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_PKT_DBG_FUNC()          \
    do                                  \
    {                                   \
        CTC_DEBUG_OUT_FUNC(packet, packet, PACKET_SYS); \
    } while (0)

#define SYS_PKT_BUF_PKT_LEN 256 /*refer to DMA default store packet pkt*/
#define SYS_PKT_BUF_MAX 32
#define STS_PKT_REASON_BLOCK_NUM (CTC_PKT_CPU_REASON_MAX_COUNT+31)/32

#define SYS_PKT_CREAT_TX_LOCK(lchip)                   \
    do                                            \
    {                                             \
        sal_spinlock_create(&p_gb_pkt_master[lchip]->tx_spinlock); \
        if (NULL == p_gb_pkt_master[lchip]->tx_spinlock)  \
        { \
            mem_free(p_gb_pkt_master[lchip]); \
            CTC_ERROR_RETURN(CTC_E_NO_RESOURCE); \
        } \
    } while (0)

#define SYS_PKT_TX_LOCK(lchip) \
    sal_spinlock_lock(p_gb_pkt_master[lchip]->tx_spinlock)

#define SYS_PKT_TX_UNLOCK(lchip) \
    sal_spinlock_unlock(p_gb_pkt_master[lchip]->tx_spinlock)

struct sys_pkt_stats_s
{
    uint64 encap;
    uint64 decap;
    uint64 uc_tx;
    uint64 mc_tx;
    uint64 rx[CTC_PKT_CPU_REASON_MAX_COUNT];
};
typedef struct sys_pkt_stats_s sys_pkt_stats_t;

struct sys_pkt_buf_s
{
   uint16 pkt_len;
   uint8  pkt_data[SYS_PKT_BUF_PKT_LEN];
   sal_time_t tm;

};
typedef struct sys_pkt_buf_s sys_pkt_buf_t;

struct sys_pkt_tx_buf_s
{
   uint16 pkt_len;
   uint8  pkt_data[SYS_PKT_BUF_PKT_LEN];
   ctc_pkt_info_t  tx_info;
   uint8 mode;
   sal_time_t tm;

};
typedef struct sys_pkt_tx_buf_s sys_pkt_tx_buf_t;

struct sys_pkt_master_s
{
    ctc_pkt_global_cfg_t    cfg;
    SYS_PKT_RX_CALLBACK     internal_rx_cb; /* for SDK's internal process */
    sys_pkt_stats_t         stats;
    mac_addr_t              cpu_mac_sa;
    mac_addr_t              cpu_mac_da;
    sys_pkt_buf_t           pkt_buf[SYS_PKT_BUF_MAX];
    sys_pkt_tx_buf_t        pkt_tx_buf[SYS_PKT_BUF_MAX];
    uint8                   buf_id;
    uint8                   buf_id_tx;
    uint8    header_en[CTC_PKT_CPU_REASON_MAX_COUNT];      /*just using for debug packet*/
    uint32  reason_bm[STS_PKT_REASON_BLOCK_NUM];
    ctc_linklist_t* async_tx_pkt_list;
    sal_task_t* p_async_tx_task;
    sal_spinlock_t* tx_spinlock;
    sal_sem_t* tx_sem;
    uint16 tp_id[4];
};
typedef struct sys_pkt_master_s sys_pkt_master_t;

struct sys_greatbelt_cpumac_header_s
{
    mac_addr_t macda;       /* destination eth addr */
    mac_addr_t macsa;       /* source ether addr */
    uint16 vlan_tpid;
    uint16 vlan_vid;
    uint16 type;            /* packet type ID field */
    uint16 reserved;
};
typedef struct sys_greatbelt_cpumac_header_s sys_greatbelt_cpumac_header_t;

#if (HOST_IS_LE == 0)

union sys_greatbelt_pkt_hdr_ip_sa_u
{

    struct
    {
        uint32 mac_known                 :1;
        uint32 src_dscp                  :6;
        uint32 rev_0                     :5;
        uint32 is_ipv4                   :1;
        uint32 cvlan_tag_operation_valid :1;
        uint32 acl_dscp                  :6;
        uint32 isid_valid                :1;
        uint32 ecn_aware                 :1;
        uint32 congestion_valid          :1;
        uint32 ecn_en                    :1;
        uint32 layer3_offset             :8;
    }normal;

    struct
    {
        uint32 rx_oam                    : 1;   /* TX: set 0 ;  RX: equal to 1 */
        uint32 mip_en_or_cw_added        : 1;   /* TX: cw_added, 1 for TP PW-OAM, 0 for others ; RX: 1 if is MIP */
        uint32 mpls_label_disable        : 4;   /* TX: disable label RX: mep_index[13,10] */
        uint32 mep_index_9_0             : 10;  /* RX: mep_index[9,0] */
        uint32 link_oam                  : 1;   /* TX: set 1 for Level-0 CFM/TP Section OAM ; RX: 1 if is link OAM */
        uint32 gal_exist                 : 1;   /* TX: set 1 ;  RX: 1 if has GAL */
        uint32 entropy_label_exist       : 1;   /* Don't care */
        uint32 oam_type                  : 4;   /* TX: need set ; RX: need ; refer to ctc_oam_type_t */
        uint32 dm_en                     : 1;   /* TX: need set 1 for 1DM, DMM, DMR, DLMDM ; RX: 1 if header has timestamp */
        uint32 rev_1                     : 1;   /* Don't care */
        uint32 local_phy_port            : 7;   /* Don't care */
    } oam;

    uint32 ip_sa                         : 32;
};
typedef union sys_greatbelt_pkt_hdr_ip_sa_u sys_greatbelt_pkt_hdr_ip_sa_t;

#else

union sys_greatbelt_pkt_hdr_ip_sa_u
{
    struct
    {
        uint32 layer3_offset             :8;
        uint32 ecn_en                    :1;
        uint32 congestion_valid          :1;
        uint32 ecn_aware                 :1;
        uint32 isid_valid                :1;
        uint32 acl_dscp                  :6;
        uint32 cvlan_tag_operation_valid :1;
        uint32 is_ipv4                   :1;
        uint32 rev_0                     :5;
        uint32 src_dscp                  :6;
        uint32 mac_known                 :1;
    }normal;

    struct
    {
        uint32 local_phy_port            : 7;
        uint32 rev_1                     : 1;
        uint32 dm_en                     : 1;
        uint32 oam_type                  : 4;
        uint32 entropy_label_exist       : 1;
        uint32 gal_exist                 : 1;
        uint32 link_oam                  : 1;
        uint32 mep_index_9_0             : 10;
        uint32 mpls_label_disable        : 4;
        uint32 mip_en_or_cw_added        : 1;
        uint32 rx_oam                    : 1;
    } oam;

    uint32 ip_sa                         : 32;
};
typedef union sys_greatbelt_pkt_hdr_ip_sa_u sys_greatbelt_pkt_hdr_ip_sa_t;

#endif

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

#ifdef __cplusplus
}
#endif

#endif

