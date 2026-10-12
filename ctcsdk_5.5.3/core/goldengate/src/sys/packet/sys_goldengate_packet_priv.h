/**
 @file sys_goldengate_packet_priv.h

 @date 2012-10-31

 @version v2.0

*/
#ifndef _SYS_GOLDENGATE_PACKET_PRIV_H
#define _SYS_GOLDENGATE_PACKET_PRIV_H
#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/

#include "ctc_packet.h"

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
        sal_spinlock_create(&p_gg_pkt_master[lchip]->tx_spinlock); \
        if (NULL == p_gg_pkt_master[lchip]->tx_spinlock)  \
        { \
            mem_free(p_gg_pkt_master[lchip]); \
            CTC_ERROR_RETURN(CTC_E_NO_RESOURCE); \
        } \
    } while (0)

#define SYS_PKT_TX_LOCK(lchip) \
    sal_spinlock_lock(p_gg_pkt_master[lchip]->tx_spinlock)

#define SYS_PKT_TX_UNLOCK(lchip) \
    sal_spinlock_unlock(p_gg_pkt_master[lchip]->tx_spinlock)

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

struct sys_pkt_tx_hdr_info_s
{
    uint16 tp_id[4];
    uint32 offset_ns;   /*ptp adjust offset ns*/
    uint32 offset_s;    /*ptp adjust offset s*/
    uint16 c2c_sub_queue_id;
    uint16 vlanptr[CTC_MAX_VLAN_ID];
    uint8 stacking_en;
};
typedef struct sys_pkt_tx_hdr_info_s sys_pkt_tx_hdr_info_t;

struct sys_pkt_master_s
{
    ctc_pkt_global_cfg_t    cfg;
    sys_pkt_stats_t         stats;
    mac_addr_t              cpu_mac_sa;
    mac_addr_t              cpu_mac_da;
    sys_pkt_buf_t           pkt_buf[SYS_PKT_BUF_MAX];
    sys_pkt_tx_buf_t        pkt_tx_buf[SYS_PKT_BUF_MAX];
    uint8                   buf_id;
    uint8                   buf_id_tx;
    uint8    header_en[CTC_PKT_CPU_REASON_MAX_COUNT];      /*just using for debug packet*/
    uint32  reason_bm[STS_PKT_REASON_BLOCK_NUM];

    SYS_PKT_RX_CALLBACK oam_rx_cb;

    sys_pkt_buf_t           diag_pkt_buf[SYS_PKT_BUF_MAX];
    uint8                   diag_buf_id;
    ctc_linklist_t* async_tx_pkt_list;
    sal_task_t* p_async_tx_task;
    sal_spinlock_t* tx_spinlock;
    sal_sem_t* tx_sem;
    sys_pkt_tx_hdr_info_t tx_hdr_info;
};
typedef struct sys_pkt_master_s sys_pkt_master_t;

struct sys_goldengate_cpumac_header_s
{
    mac_addr_t macda;       /* destination eth addr */
    mac_addr_t macsa;       /* source ether addr */
    uint16 vlan_tpid;
    uint16 vlan_vid;
    uint16 type;            /* packet type ID field */
};
typedef struct sys_goldengate_cpumac_header_s sys_goldengate_cpumac_header_t;

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

#ifdef __cplusplus
}
#endif

#endif

