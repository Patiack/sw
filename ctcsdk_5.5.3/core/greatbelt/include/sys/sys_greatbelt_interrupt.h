/**
 @file sys_greatbelt_interrupt.h

 @date 2012-10-23

 @version v2.0

*/
#ifndef _SYS_GREATBELT_INTERRUPT_H
#define _SYS_GREATBELT_INTERRUPT_H
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

#define INTR_TYPE_CHECK(intr) \
    do { \
        if ((intr) >= CTC_INTR_GB_MAX) \
        { \
            return CTC_E_INTR_INVALID_TYPE; \
        } \
    } while (0)

#define INTR_SUB_TYPE_CHECK(intr, max) \
    do { \
        if ((intr) >= (max)) \
        { \
            return CTC_E_INTR_INVALID_SUB_TYPE; \
        } \
    } while (0)

#define INTR_SUP_FUNC_TYPE_CHECK(intr) \
    do { \
        if ((intr) < SYS_INTR_GB_SUP_FUNC_BIT_BEGIN || (intr) > SYS_INTR_GB_SUP_FUNC_BIT_END) \
        { \
            return CTC_E_INTR_INVALID_TYPE; \
        } \
    } while (0)

#define SYS_INTETRUPT_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(interrupt, interrupt, INTERRUPT_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

#define INTR_INDEX_VAL_SET      0
#define INTR_INDEX_VAL_RESET    1
#define INTR_INDEX_MASK_SET     2
#define INTR_INDEX_MASK_RESET   3
#define INTR_INDEX_MAX          4

/* different count number about sys_interrupt_type_gb_t and ctc_interrupt_type_t */
#define INTR_ADDTINAL_COUNT     11


/* same as original from ctc_interrupt_type_gb_e */
enum sys_interrupt_type_gb_e
{
    /* ASIC Internal Interrupt */
    SYS_INTR_GB_CHIP_FATAL = 0,             /**< [GB] sup fatal exception appear, sub-intr [0,47] */
    SYS_INTR_GB_CHIP_NORMAL,                /**< [GB] sup normal exception appear, sub-intr [0,85] */

    /* Functional Interrupts */
    SYS_INTR_GB_FUNC_PTP_TS_CAPTURE,         /**< [GB] PTP RX timestamp capture, when 1PPS or SyncPulse received */
    SYS_INTR_GB_FUNC_PTP_MAC_TX_TS_CAPTURE,  /**< [GB] PTP TX timestamp capture, for 2-step */
    SYS_INTR_GB_FUNC_PTP_TOD_PULSE_IN,       /**< [GB] PTP 1PPS received */
    SYS_INTR_GB_FUNC_PTP_TOD_CODE_IN_RDY,    /**< [GB] PTP TOD input code is ready */
    SYS_INTR_GB_FUNC_PTP_SYNC_PULSE_IN,      /**< [GB] PTP SyncPulse received */
    SYS_INTR_GB_FUNC_PTP_SYNC_CODE_IN_RDY,   /**< [GB] PTP Sync Interface input code is ready */
    SYS_INTR_GB_FUNC_OAM_TX_PKT_STATS,       /**< [GB] OAM Auto TX to notify packet number exceed threshold */
    SYS_INTR_GB_FUNC_OAM_TX_OCTET_STATS,     /**< [GB] OAM Auto TX to notify packet octets exceed threshold */
    SYS_INTR_GB_FUNC_OAM_RX_PKT_STATS,       /**< [GB] OAM Auto RX to notify packet number exceed threshold */
    SYS_INTR_GB_FUNC_OAM_RX_OCTET_STATS,     /**< [GB] OAM Auto RX to notify packet octets exceed threshold */
    SYS_INTR_GB_FUNC_OAM_DEFECT_CACHE,       /**< [GB] OAM defect cache to notify OAM status change */
    SYS_INTR_GB_FUNC_OAM_CLEAR_EN_3,         /**< [GB] OAM Auto TX to notify session 3 TX number reach */
    SYS_INTR_GB_FUNC_OAM_CLEAR_EN_2,         /**< [GB] OAM Auto TX to notify session 2 TX number reach */
    SYS_INTR_GB_FUNC_OAM_CLEAR_EN_1,         /**< [GB] OAM Auto TX to notify session 1 TX number reach */
    SYS_INTR_GB_FUNC_OAM_CLEAR_EN_0,         /**< [GB] OAM Auto TX to notify session 0 TX number reach */
    SYS_INTR_GB_FUNC_MDIO_XG_CHANGE,         /**< [GB] MDIO detects any status changes of XG PHYs */
    SYS_INTR_GB_FUNC_MDIO_1G_CHANGE,         /**< [GB] MDIO detects any status changes of 1G PHYs */
    SYS_INTR_GB_FUNC_LINKAGG_FAILOVER,       /**< [GB] Linkagg protect switch done */
    SYS_INTR_GB_FUNC_IPE_LEARN_CACHE,        /**< [GB] Learning cache to notify SW learning */
    SYS_INTR_GB_FUNC_IPE_FIB_LEARN_FIFO,     /**< [GB] HW learning or aging */
    SYS_INTR_GB_FUNC_IPE_AGING_FIFO,         /**< [GB] Aging FIFO to notify SW aging */
    SYS_INTR_GB_FUNC_STATS_FIFO,             /**< [GB] Statistics exceed the threshold */
    SYS_INTR_GB_FUNC_APS_FAILOVER,           /**< [GB] APS protect switch done */

    /* DMA Interrupt */
    SYS_INTR_GB_DMA_FUNC,                    /**< [GB] DMA channel need to handle, sub-intr [0,16] */
    SYS_INTR_GB_DMA_NORMAL,                  /**< [GB] DMA channel exception appear, sub-intr [0,47] */

    /* PCIe Interrupt */
    SYS_INTR_GB_PCIE_SECOND,                 /**< [GB] PCIe secondary exception appear */
    SYS_INTR_GB_PCIE_PRIMARY,                /**< [GB] PCIe primary exception appear */

    SYS_INTR_GB_MAX
};
typedef enum sys_interrupt_type_gb_e sys_interrupt_type_gb_t;


/**
 @brief [GB] CTC_INTR_GB_CHIP_FATAL interrupt sub-type
*/
enum sys_interrupt_type_gb_sub_fatal_e
{
    SYS_INTR_GB_SUB_FATAL_ELOOP = 0,
    SYS_INTR_GB_SUB_FATAL_MAC_MUX,
    SYS_INTR_GB_SUB_FATAL_NET_RX,
    SYS_INTR_GB_SUB_FATAL_OOB_FC,
    SYS_INTR_GB_SUB_FATAL_PTP_ENGINE,
    SYS_INTR_GB_SUB_FATAL_EPE_ACL_QOS,
    SYS_INTR_GB_SUB_FATAL_EPE_CLA,
    SYS_INTR_GB_SUB_FATAL_EPE_HDR_ADJ,
    SYS_INTR_GB_SUB_FATAL_EPE_HDR_EDIT,
    SYS_INTR_GB_SUB_FATAL_EPE_HDR_PROC,
    SYS_INTR_GB_SUB_FATAL_EPE_NEXT_HOP,
    SYS_INTR_GB_SUB_FATAL_EPE_OAM,
    SYS_INTR_GB_SUB_FATAL_GLOBAL_STATS,
    SYS_INTR_GB_SUB_FATAL_IPE_FORWARD,
    SYS_INTR_GB_SUB_FATAL_IPE_HDR_ADJ,
    SYS_INTR_GB_SUB_FATAL_IPE_INTF_MAP,
    SYS_INTR_GB_SUB_FATAL_IPE_LKUP_MGR,
    SYS_INTR_GB_SUB_FATAL_IPE_PKT_PROC,
    SYS_INTR_GB_SUB_FATAL_PARSER,
    SYS_INTR_GB_SUB_FATAL_POLICING,
    SYS_INTR_GB_SUB_FATAL_DYNAMIC_DS,
    SYS_INTR_GB_SUB_FATAL_HASH_DS,
    SYS_INTR_GB_SUB_FATAL_IPE_FIB,
    SYS_INTR_GB_SUB_FATAL_NET_TX,
    SYS_INTR_GB_SUB_FATAL_OAM_FWD,
    SYS_INTR_GB_SUB_FATAL_OAM_PARSER,
    SYS_INTR_GB_SUB_FATAL_OAM_PROC,
    SYS_INTR_GB_SUB_FATAL_TCAM_CTL_INT,
    SYS_INTR_GB_SUB_FATAL_TCAM_DS,
    SYS_INTR_GB_SUB_FATAL_BUF_RETRV,
    SYS_INTR_GB_SUB_FATAL_BUF_STORE,
    SYS_INTR_GB_SUB_FATAL_MET_FIFO,
    SYS_INTR_GB_SUB_FATAL_PB_CTL,
    SYS_INTR_GB_SUB_FATAL_Q_MGR_DEQ,
    SYS_INTR_GB_SUB_FATAL_Q_MGR_ENQ,
    SYS_INTR_GB_SUB_FATAL_Q_MGR_QUE_ENTRY,
    SYS_INTR_GB_SUB_FATAL_INT_LK_INTF,
    SYS_INTR_GB_SUB_FATAL_MON_CFG_PCI04_SERR,
    SYS_INTR_GB_SUB_FATAL_UTL_PCI04_CMPL_UR,
    SYS_INTR_GB_SUB_FATAL_UTL_PCI04_REC_CMPL_ABORT,
    SYS_INTR_GB_SUB_FATAL_UTL_PCI04_SIG_CMPL_ABORT,
    SYS_INTR_GB_SUB_FATAL_UTL_EC08_MALFORMED_TLP,
    SYS_INTR_GB_SUB_FATAL_UTL_EC08_UN_EXPECTED_CMPL,
    SYS_INTR_GB_SUB_FATAL_UTL_AER04_ECRC_ERROR,
    SYS_INTR_GB_SUB_FATAL_UTL_EC08_UR,
    SYS_INTR_GB_SUB_FATAL_UTL_EC08_CMPL_TIMEOUT,
    SYS_INTR_GB_SUB_FATAL_UTL_PCI04_REC_POISONED_TLP,
    SYS_INTR_GB_SUB_FATAL_UTL_PCI04_SIG_POISONED_TLP,
    SYS_INTR_GB_SUB_FATAL_MAX
};
typedef enum sys_interrupt_type_gb_sub_fatal_e sys_interrupt_type_gb_sub_fatal_t;

/**
 @brief [GB] CTC_INTR_GB_CHIP_NORMAL interrupt sub-type
*/
enum sys_interrupt_type_gb_sub_normal_e
{
    SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII0 = 0,
    SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII1,
    SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII2,
    SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII3,
    SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII4,
    SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII5,
    SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII6,
    SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII7,
    SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII8,
    SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII9,
    SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII10,
    SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII11,
    SYS_INTR_GB_SUB_NORMAL_QUAD_PCS0,
    SYS_INTR_GB_SUB_NORMAL_QUAD_PCS1,
    SYS_INTR_GB_SUB_NORMAL_QUAD_PCS2,
    SYS_INTR_GB_SUB_NORMAL_QUAD_PCS3,
    SYS_INTR_GB_SUB_NORMAL_QUAD_PCS4,
    SYS_INTR_GB_SUB_NORMAL_QUAD_PCS5,
    SYS_INTR_GB_SUB_NORMAL_SGMAC0,
    SYS_INTR_GB_SUB_NORMAL_SGMAC1,
    SYS_INTR_GB_SUB_NORMAL_SGMAC2,
    SYS_INTR_GB_SUB_NORMAL_SGMAC3,
    SYS_INTR_GB_SUB_NORMAL_SGMAC4,
    SYS_INTR_GB_SUB_NORMAL_SGMAC5,
    SYS_INTR_GB_SUB_NORMAL_SGMAC6,
    SYS_INTR_GB_SUB_NORMAL_SGMAC7,
    SYS_INTR_GB_SUB_NORMAL_SGMAC8,
    SYS_INTR_GB_SUB_NORMAL_SGMAC9,
    SYS_INTR_GB_SUB_NORMAL_SGMAC10,
    SYS_INTR_GB_SUB_NORMAL_SGMAC11,
    SYS_INTR_GB_SUB_NORMAL_QUAD_MAC0,
    SYS_INTR_GB_SUB_NORMAL_QUAD_MAC1,

    SYS_INTR_GB_SUB_NORMAL_QUAD_MAC2,
    SYS_INTR_GB_SUB_NORMAL_QUAD_MAC3,
    SYS_INTR_GB_SUB_NORMAL_QUAD_MAC4,
    SYS_INTR_GB_SUB_NORMAL_QUAD_MAC5,
    SYS_INTR_GB_SUB_NORMAL_QUAD_MAC6,
    SYS_INTR_GB_SUB_NORMAL_QUAD_MAC7,
    SYS_INTR_GB_SUB_NORMAL_QUAD_MAC8,
    SYS_INTR_GB_SUB_NORMAL_QUAD_MAC9,
    SYS_INTR_GB_SUB_NORMAL_QUAD_MAC10,
    SYS_INTR_GB_SUB_NORMAL_QUAD_MAC11,
    SYS_INTR_GB_SUB_NORMAL_CPU_MAC,
    SYS_INTR_GB_SUB_NORMAL_HSS0_EYE_QUALITY,
    SYS_INTR_GB_SUB_NORMAL_HSS1_EYE_QUALITY,
    SYS_INTR_GB_SUB_NORMAL_HSS2_EYE_QUALITY,
    SYS_INTR_GB_SUB_NORMAL_HSS3_EYE_QUALITY,
    SYS_INTR_GB_SUB_NORMAL_E_LOOP,
    SYS_INTR_GB_SUB_NORMAL_MAC_MUX,
    SYS_INTR_GB_SUB_NORMAL_NET_RX,
    SYS_INTR_GB_SUB_NORMAL_NET_TX,
    SYS_INTR_GB_SUB_NORMAL_OOB_FC,
    SYS_INTR_GB_SUB_NORMAL_PTP_ENGINE,
    SYS_INTR_GB_SUB_NORMAL_EPE_ACL_QOS,
    SYS_INTR_GB_SUB_NORMAL_EPE_CLA,
    SYS_INTR_GB_SUB_NORMAL_EPE_HDR_ADJ,
    SYS_INTR_GB_SUB_NORMAL_EPE_HDR_EDIT,
    SYS_INTR_GB_SUB_NORMAL_EPE_HDR_PROC,
    SYS_INTR_GB_SUB_NORMAL_EPE_NEXT_HOP,
    SYS_INTR_GB_SUB_NORMAL_GLOBAL_STATS,
    SYS_INTR_GB_SUB_NORMAL_IPE_FORWARD,
    SYS_INTR_GB_SUB_NORMAL_IPE_HDR_ADJ,
    SYS_INTR_GB_SUB_NORMAL_IPE_INTF_MAP,
    SYS_INTR_GB_SUB_NORMAL_IPE_LKUP_MGR,

    SYS_INTR_GB_SUB_NORMAL_IPE_PKT_PROC,
    SYS_INTR_GB_SUB_NORMAL_I2_C_MASTER,
    SYS_INTR_GB_SUB_NORMAL_POLICING,
    SYS_INTR_GB_SUB_NORMAL_SHARED_DS,
    SYS_INTR_GB_SUB_NORMAL_DS_AGING,
    SYS_INTR_GB_SUB_NORMAL_DYNAMIC_DS,
    SYS_INTR_GB_SUB_NORMAL_IPE_FIB,
    SYS_INTR_GB_SUB_NORMAL_OAM_AUTO_GEN_PKT,
    SYS_INTR_GB_SUB_NORMAL_OAM_FWD,
    SYS_INTR_GB_SUB_NORMAL_OAM_PROC,
    SYS_INTR_GB_SUB_NORMAL_TCAM_CTL_INT,
    SYS_INTR_GB_SUB_NORMAL_TCAM_DS,
    SYS_INTR_GB_SUB_NORMAL_BUF_RETRV,
    SYS_INTR_GB_SUB_NORMAL_BUF_STORE,
    SYS_INTR_GB_SUB_NORMAL_MET_FIFO,
    SYS_INTR_GB_SUB_NORMAL_PB_CTL,
    SYS_INTR_GB_SUB_NORMAL_Q_MGR_DEQ,
    SYS_INTR_GB_SUB_NORMAL_Q_MGR_ENQ,
    SYS_INTR_GB_SUB_NORMAL_Q_MGR_QUE_ENTRY,
    SYS_INTR_GB_SUB_NORMAL_MAC_LED_DRIVER,
    SYS_INTR_GB_SUB_NORMAL_EPE_OAM,
    SYS_INTR_GB_SUB_NORMAL_OAM_PARSER,
    SYS_INTR_GB_SUB_NORMAL_MAX
};
typedef enum sys_interrupt_type_gb_sub_normal_e sys_interrupt_type_gb_sub_normal_t;

/**
 @brief [GB] CTC_INTR_GB_DMA_FUNC interrupt sub-type
*/
enum sys_interrupt_type_gb_sub_dma_func_e
{
    SYS_INTR_GB_SUB_DMA_FUNC_RX_CHAN_0 = 0,
    SYS_INTR_GB_SUB_DMA_FUNC_RX_CHAN_1,
    SYS_INTR_GB_SUB_DMA_FUNC_RX_CHAN_2,
    SYS_INTR_GB_SUB_DMA_FUNC_RX_CHAN_3,
    SYS_INTR_GB_SUB_DMA_FUNC_RX_CHAN_4,
    SYS_INTR_GB_SUB_DMA_FUNC_RX_CHAN_5,
    SYS_INTR_GB_SUB_DMA_FUNC_RX_CHAN_6,
    SYS_INTR_GB_SUB_DMA_FUNC_RX_CHAN_7,
    SYS_INTR_GB_SUB_DMA_FUNC_TX_CHAN_0,
    SYS_INTR_GB_SUB_DMA_FUNC_TX_CHAN_1,
    SYS_INTR_GB_SUB_DMA_FUNC_TX_CHAN_2,
    SYS_INTR_GB_SUB_DMA_FUNC_TX_CHAN_3,
    SYS_INTR_GB_SUB_DMA_FUNC_TX_CHAN_4,
    SYS_INTR_GB_SUB_DMA_FUNC_TX_CHAN_5,
    SYS_INTR_GB_SUB_DMA_FUNC_TX_CHAN_6,
    SYS_INTR_GB_SUB_DMA_FUNC_TX_CHAN_7,
    SYS_INTR_GB_SUB_DMA_FUNC_BURST,
    SYS_INTR_GB_SUB_DMA_FUNC_MAX
};
typedef enum sys_interrupt_type_gb_sub_dma_func_e sys_interrupt_type_gb_sub_dma_func_t;

/**
 @brief [GB] CTC_INTR_GB_DMA_NORMAL interrupt sub-type
*/
enum sys_interrupt_type_gb_sub_dma_normal_e
{
    SYS_INTR_GB_SUB_DMA_NORMAL_PKT_RX_ASYNC_FIFO_OVERRUN = 0,
    SYS_INTR_GB_SUB_DMA_NORMAL_PKT_TX_ASYNC_FIFO_OVERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_LEARN_ASYNC_FIFO_OVERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_LEARN_DONE_FIFO_OVERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_TAB_WR_DATA_FIFO_UNDERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_PKT_TX_INFO_FIFO_OVERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_PKT_TX_DATA_FIFO_OVERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_PKT_RX_DATA_FIFO_OVERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_REG_RD_DATA_FIFO_OVERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_REG_RD_INFO_FIFO_OVERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_TAB_WR_DATA_FIFO_OVERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_DESC_WRBK_FIFO_OVERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_LEARN_INFO_FIFO_OVERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_TAB_WR_INFO_FIFO_OVERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_TAB_RD_REQ_FIFO_OVERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_PCI_EXP_DESC_MEM_ECC_ERROR,
    SYS_INTR_GB_SUB_DMA_NORMAL_PKT_TX_DATA_FIFO_PARITY_ERROR,
    SYS_INTR_GB_SUB_DMA_NORMAL_PKT_RX_DATA_FIFO_PARITY_ERROR,
    SYS_INTR_GB_SUB_DMA_NORMAL_REG_RD_DATA_FIFO_PARITY_ERROR,
    SYS_INTR_GB_SUB_DMA_NORMAL_TAB_WR_DATA_FIFO_PARITY_ERROR,
    SYS_INTR_GB_SUB_DMA_NORMAL_OUTBOUND_POISONED,
    SYS_INTR_GB_SUB_DMA_NORMAL_OUTBOUND_ECC_ERROR,
    SYS_INTR_GB_SUB_DMA_NORMAL_OUT_RD_DONE_ERROR,
    SYS_INTR_GB_SUB_DMA_NORMAL_OUT_RD_DONE_ERROR_UR,
    SYS_INTR_GB_SUB_DMA_NORMAL_OUT_RD_DONE_RETRY,
    SYS_INTR_GB_SUB_DMA_NORMAL_STATS_DMA_OVERLAP,
    SYS_INTR_GB_SUB_DMA_NORMAL_PKT_TX_DESC_EMPTY_INTR,
    SYS_INTR_GB_SUB_DMA_NORMAL_TAB_RD_DESC_EMPTY_INTR,
    SYS_INTR_GB_SUB_DMA_NORMAL_PKT_RX_DESC_EMPTY_INTR,
    SYS_INTR_GB_SUB_DMA_NORMAL_TAB_WR_DESC_EMPTY_INTR,
    SYS_INTR_GB_SUB_DMA_NORMAL_STATS_DESC_EMPTY_INTR,
    SYS_INTR_GB_SUB_DMA_NORMAL_LEARN_MEM_EMPTY_INTR,

    SYS_INTR_GB_SUB_DMA_NORMAL_TAB_WR_TIMEOUT_INTR,
    SYS_INTR_GB_SUB_DMA_NORMAL_TAB_RD_TIMEOUT_INTR,
    SYS_INTR_GB_SUB_DMA_NORMAL_DESC_FETCH_ERROR_INTR,
    SYS_INTR_GB_SUB_DMA_NORMAL_TAB_WR_ERROR_INTR,
    SYS_INTR_GB_SUB_DMA_NORMAL_PKT_RX_ERROR_INTR,
    SYS_INTR_GB_SUB_DMA_NORMAL_DESC_MEM_RD_ERROR_INTR,
    SYS_INTR_GB_SUB_DMA_NORMAL_PKT_TX_INFO_TIMEOUT,
    SYS_INTR_GB_SUB_DMA_NORMAL_PKT_RX_INFO_TIMEOUT,
    SYS_INTR_GB_SUB_DMA_NORMAL_DESC_WRBK_FIFO_UNDERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_LEARN_INFO_FIFO_UNDERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_PKT_RX_DATA_FIFO_UNDERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_PKT_TX_DATA_FIFO_UNDERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_PKT_TX_INFO_FIFO_UNDERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_REG_RD_DATA_FIFO_UNDERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_REG_RD_INFO_FIFO_UNDERRUN,
    SYS_INTR_GB_SUB_DMA_NORMAL_TAB_WR_INFO_FIFO_UNDERRUN,

    SYS_INTR_GB_SUB_DMA_NORMAL_MAX
};
typedef enum sys_interrupt_type_gb_sub_dma_normal_e sys_interrupt_type_gb_sub_dma_normal_t;




typedef struct sys_intr_sub_reg_s
{
    int32               tbl_id; /* sup fatal/normal' sub interrupt type to register mapping */
} sys_intr_sub_reg_t;

/**
 @brief [GB] Define parameters of a sup-level interrupt
*/
struct sys_intr_mapping_s
{
    int32               group;                      /**< group ID of this interrupt */
    uint32              intr;                       /**< type/ID of this interrupt, refer to ctc_interrupt_type_CHIP_t */
    char                desc[CTC_INTR_DESC_LEN];    /**< description of this interrupt */
    CTC_INTERRUPT_FUNC  isr;                        /**< callback function of this interrupt */
};
typedef struct sys_intr_mapping_s sys_intr_mapping_t;

/**
 @brief [GB] Define global configure parameters to initialize interrupt module
*/
struct sys_intr_global_cfg_s
{
    uint32              group_count;                /**< count of groups */
    uint32              intr_count;                 /**< count of interrupts */
    ctc_intr_group_t    group[SYS_GB_MAX_INTR_GROUP];  /**< array of groups */
    sys_intr_mapping_t intr[CTC_INTR_MAX_INTR];    /**< array of interrupts */
    uint8                intr_mode;                  /**< interrupt mode, 0: interrupt pin, 1: msi*/
    uint8                rsv[3];
};
typedef struct sys_intr_global_cfg_s sys_intr_global_cfg_t;

struct sys_intr_type_s
{
    uint32  intr;                   /**< sup-level interrupt type defined in sys_interrupt_type_gb_t */
    uint32  sub_intr;               /**< sub-level interrupt type defined in sys_interrupt_type_gb_sub_SUP_t */
};
typedef struct sys_intr_type_s sys_intr_type_t;


/**
 @brief [GB] Define the interrupts in sub normal interrupt which need to reset the chip
*/
struct sys_intr_sub_normal_bmp_s
{
    uint32              sub_intr;             /**< sys_interrupt_type_gb_sub_normal_t */
    uint32              reset_intr_bmp;              /**< bitmap in every sub normal interrupt, all interrupts in sub normal need to reset the chip */
};
typedef struct sys_intr_sub_normal_bmp_s sys_intr_sub_normal_bmp_t;


/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
/**
 @brief Initialize interrupt module
*/
extern int32
sys_greatbelt_interrupt_init(uint8 lchip, void* intr_global_cfg);

/**
 @brief De-Initialize interrupt module
*/
extern int32
sys_greatbelt_interrupt_deinit(uint8 lchip);
/**
 @brief Clear interrupt status
*/
extern int32
sys_greatbelt_interrupt_clear_status(uint8 lchip, ctc_intr_type_t* p_type, uint32 with_sup);

/**
 @brief Get interrupt status
*/
extern int32
sys_greatbelt_interrupt_get_status(uint8 lchip, ctc_intr_type_t* p_type, ctc_intr_status_t* p_status);

/**
 @brief Set interrupt enable
*/
extern int32
sys_greatbelt_interrupt_set_en(uint8 lchip, ctc_intr_type_t* p_type, uint32 enable);

/**
 @brief Get interrupt enable
*/
extern int32
sys_greatbelt_interrupt_get_en(uint8 lchip, ctc_intr_type_t* p_type, uint32* p_enable);

/**
 @brief Register event callback function
*/
extern int32
sys_greatbelt_interrupt_register_event_cb(uint8 lchip, ctc_interrupt_event_t event, CTC_INTERRUPT_EVENT_FUNC cb);

/**
 @brief mapping from SYS_INTR_GB_FUNC_XXX to CTC_INTR_GB_CHIP_XXX
*/
extern int32
sys_greatbelt_interrupt_type_unmapping(uint8 lchip, uint32 sys_intr, uint32* intr);

#ifdef __cplusplus
}
#endif

#endif

