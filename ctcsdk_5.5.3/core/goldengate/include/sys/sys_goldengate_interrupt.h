/**
 @file sys_goldengate_interrupt.h

 @date 2012-10-23

 @version v2.0

*/
#ifndef _SYS_GOLDENGATE_INTERRUPT_H
#define _SYS_GOLDENGATE_INTERRUPT_H
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

#define SYS_INTR_MAX_INTR       32              /**< max number of sup-level interrupts */

/**
 @brief [GG] SYS_INTR_GB_CHIP_FATAL interrupt sub-type
*/
enum sys_interrupt_type_gg_sub_fatal_e
{
    SYS_INTR_GG_SUB_FATAL_RLM_AD_0 = 0,
    SYS_INTR_GG_SUB_FATAL_RLM_AD_1,
    SYS_INTR_GG_SUB_FATAL_RLM_HASH_0,
    SYS_INTR_GG_SUB_FATAL_RLM_HASH_1,
    SYS_INTR_GG_SUB_FATAL_RLM_EPE_0,
    SYS_INTR_GG_SUB_FATAL_RLM_EPE_1,
    SYS_INTR_GG_SUB_FATAL_RLM_IPE_0,
    SYS_INTR_GG_SUB_FATAL_RLM_IPE_1,
    SYS_INTR_GG_SUB_FATAL_RLM_NET_0,
    SYS_INTR_GG_SUB_FATAL_RLM_NET_1,
    SYS_INTR_GG_SUB_FATAL_RLM_SHARE_TCAM,
    SYS_INTR_GG_SUB_FATAL_RLM_SHARE_DS,
    SYS_INTR_GG_SUB_FATAL_RLM_BR_PB,
    SYS_INTR_GG_SUB_FATAL_RLM_BS,
    SYS_INTR_GG_SUB_FATAL_RLM_ENQ,
    SYS_INTR_GG_SUB_FATAL_RLM_DEQ,
    SYS_INTR_GG_SUB_FATAL_REQ_PCIE_0_FIFO_OVERRUN,
    SYS_INTR_GG_SUB_FATAL_REQ_PCIE_1_FIFO_OVERRUN,
    SYS_INTR_GG_SUB_FATAL_REQ_DMA_0_FIFO_OVERRUN,
    SYS_INTR_GG_SUB_FATAL_REQ_DMA_1_FIFO_OVERRUN,
    SYS_INTR_GG_SUB_FATAL_REQ_TRACK_FIFO_OVERRUN,
    SYS_INTR_GG_SUB_FATAL_REQ_TRACK_FIFO_UNDERRUN,
    SYS_INTR_GG_SUB_FATAL_RES_CROSS_FIFO_OVERRUN,
    SYS_INTR_GG_SUB_FATAL_REQ_CROSS_FIFO_OVERRUN,
    SYS_INTR_GG_SUB_FATAL_REQ_I2C_FIFO_OVERRUN,
    SYS_INTR_GG_SUB_FATAL_PCIE_INTF,
    SYS_INTR_GG_SUB_FATAL_DMA_0,
    SYS_INTR_GG_SUB_FATAL_DMA_1,

    SYS_INTR_GG_SUB_FATAL_MAX
};
typedef enum sys_interrupt_type_gg_sub_fatal_e sys_interrupt_type_gg_sub_fatal_t;

/**
 @brief [GB] SYS_INTR_GB_CHIP_NORMAL interrupt sub-type
*/
enum sys_interrupt_type_gg_sub_normal_e
{
    SYS_INTR_GG_SUB_NORMAL_RLM_AD_0 = 0,
    SYS_INTR_GG_SUB_NORMAL_RLM_AD_1,
    SYS_INTR_GG_SUB_NORMAL_RLM_HASH_0,
    SYS_INTR_GG_SUB_NORMAL_RLM_HASH_1,
    SYS_INTR_GG_SUB_NORMAL_RLM_EPE_0,
    SYS_INTR_GG_SUB_NORMAL_RLM_EPE_1,
    SYS_INTR_GG_SUB_NORMAL_RLM_IPE_0,
    SYS_INTR_GG_SUB_NORMAL_RLM_IPE_1,
    SYS_INTR_GG_SUB_NORMAL_RLM_CS_0,
    SYS_INTR_GG_SUB_NORMAL_RLM_CS_1,
    SYS_INTR_GG_SUB_NORMAL_RLM_HS_0,
    SYS_INTR_GG_SUB_NORMAL_RLM_HS_1,
    SYS_INTR_GG_SUB_NORMAL_RLM_NET_0,
    SYS_INTR_GG_SUB_NORMAL_RLM_NET_1,
    SYS_INTR_GG_SUB_NORMAL_RLM_SHARE_TCAM,
    SYS_INTR_GG_SUB_NORMAL_RLM_SHARE_DS,
    SYS_INTR_GG_SUB_NORMAL_RLM_BR_PB,
    SYS_INTR_GG_SUB_NORMAL_RLM_BS,
    SYS_INTR_GG_SUB_NORMAL_RLM_ENQ,
    SYS_INTR_GG_SUB_NORMAL_RLM_DEQ,
    SYS_INTR_GG_SUB_NORMAL_I2C_MASTER_0,
    SYS_INTR_GG_SUB_NORMAL_I2C_MASTER_1,
    SYS_INTR_GG_SUB_NORMAL_DMA_0,
    SYS_INTR_GG_SUB_NORMAL_DMA_1,
    SYS_INTR_GG_SUB_NORMAL_PCIE_INTF,

    SYS_INTR_GG_SUB_NORMAL_MAX
};
typedef enum sys_interrupt_type_gg_sub_normal_e sys_interrupt_type_gg_sub_normal_t;

/**
 @brief [GG] SYS_INTR_GB_DMA_FUNC interrupt sub-type
*/
enum sys_interrupt_type_sub_dma_func_e
{
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_0 = 0,
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_1,
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_2,
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_3,
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_4,
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_5,
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_6,
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_7,
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_8,
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_9,
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_10,
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_11,
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_12,
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_13,
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_14,
    SYS_INTR_GG_SUB_DMA_FUNC_CHAN_15,
    SYS_INTR_GG_SUB_DMA_FUNC_MAX
};
typedef enum sys_interrupt_type_sub_dma_func_e  sys_interrupt_type_sub_dma_func_t;

enum sys_interrupt_type_gg_sub_pcie_e
{
    SYS_INTR_GG_SUB_PCIE_INTF_0_REG_BURST_DONE = 0,
    SYS_INTR_GG_SUB_PCIE_INTF_1_REG_BURST_DONE,

    SYS_INTR_GG_SUB_PCIE_INTF_MAX
};
typedef enum sys_interrupt_type_gg_sub_pcie_e sys_interrupt_type_gg_sub_pcie_t;

enum sys_interrupt_type_gg_e
{
    /* ASIC Internal Interrupt */
    SYS_INTR_GG_CHIP_FATAL                  = 0,    /* SupIntrFatal */
    SYS_INTR_GG_CHIP_NORMAL                 = 1,    /* SupIntrNormal */

    /* Functional Interrupts */
    SYS_INTR_GG_PCS_LINK_31_0               = 2,    /* RlmHsCtlInterruptFunc0 */
    SYS_INTR_GG_PCS_LINK_47_32              = 3,    /* RlmCsCtlInterruptFunc0 */
    SYS_INTR_GG_PCS_LINK_79_48              = 4,    /* RlmHsCtlInterruptFunc1 */
    SYS_INTR_GG_PCS_LINK_95_80              = 5,    /* RlmCsCtlInterruptFunc1 */
    SYS_INTR_GG_FUNC_IPE_LEARN_CACHE_0      = 6,    /* RlmIpeCtlInterruptFunc0 */
    SYS_INTR_GG_FUNC_IPE_LEARN_CACHE_1      = 7,    /* RlmIpeCtlInterruptFunc1 */
    SYS_INTR_GG_FUNC_CHAN_LINKDOWN_SCAN     = 8,    /* RlmEnqCtlInterruptFunc */
    SYS_INTR_GG_FUNC_MDIO_CHANGE_0          = 9,    /* RlmNetCtlInterruptFunc0 NO_DRV */
    SYS_INTR_GG_FUNC_MDIO_CHANGE_1          = 10,   /* RlmNetCtlInterruptFunc1 NO_DRV */
    SYS_INTR_GG_FUNC_IPFIX_USEAGE_OVERFLOW     = 11,  /*RlmShareTcamCtlInterruptFunc*/
    SYS_INTR_GG_FUNC_AGING_FIFO             = 12,   /* RlmShareDsCtlInterruptFunc */
    SYS_INTR_GG_FUNC_STATS_FIFO,
    SYS_INTR_GG_FUNC_PORT_LOG_STATS_FIFO,
    SYS_INTR_GG_FUNC_PTP_TOD_CODE_IN_RDY,
    SYS_INTR_GG_FUNC_PTP_SYNC_CODE_IN_RDY,
    SYS_INTR_GG_FUNC_PTP_SYNC_CODE_IN_ACC,
    SYS_INTR_GG_FUNC_PTP_MAC_TX_TS_CAPTURE,
    SYS_INTR_GG_FUNC_PTP_TX_TS_CAPTURE,

    SYS_INTR_GG_FUNC_MET_LINK_SCAN_DONE     = 20,   /* RlmBsCtlInterruptFunc */
    SYS_INTR_GG_FUNC_OAM_CLEAR_EN,
    SYS_INTR_GG_FUNC_OAM_DEFECT_CACHE,
    SYS_INTR_GG_FUNC_OAM_STATS,
    SYS_INTR_GG_FUNC_BSR_CONGESTION_1,
    SYS_INTR_GG_FUNC_BSR_CONGESTION_0,
    SYS_INTR_GG_FUNC_RESERVED1,
    SYS_INTR_GG_FUNC_RESERVED2,

    /* PCIe Interrupt */
    SYS_INTR_GG_PCIE_BURST_DONE             = 28,   /* PcieIntfInterruptFunc */

    /* DMA Interrupt */
    SYS_INTR_GG_DMA_0                       = 29,   /* DmaCtlIntrFunc0 */
    SYS_INTR_GG_DMA_1                       = 30,   /* DmaCtlIntrFunc1 */

    SYS_INTR_GG_MAX
};
typedef enum sys_interrupt_type_gg_e sys_interrupt_type_gg_t;

/**
 @brief [GG] Define struct for sys inter type
*/
struct sys_intr_type_s
{
    uint32  intr;                   /**<[GG] sup-level interrupt type defined in ctc_interrupt_type_CHIP_t */
    uint32  sub_intr;            /**<[GG] sub-level interrupt type defined in ctc_interrupt_type_CHIP_sub_SUP_t */
    uint32  low_intr;            /**<[GG] low-level interrupt type, used only for fatal and normal interrupt  */
    uint32  slice_id;             /**<[GG]0:slice0, 1:slice1, 2: slice0+slice1 */
};
typedef struct sys_intr_type_s sys_intr_type_t;

#define INTR_TYPE_CHECK(intr) \
    do { \
        if (((intr) >= SYS_INTR_GG_MAX) \
            || (SYS_INTR_GG_FUNC_RESERVED1 == (intr)) \
            || (SYS_INTR_GG_FUNC_RESERVED2 == (intr))) \
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
        if ((intr) < SYS_INTR_GG_PCS_LINK_31_0 || (intr) >= SYS_INTR_GG_PCIE_BURST_DONE) \
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
    ctc_intr_group_t    group[SYS_GG_MAX_INTR_GROUP];  /**< array of groups */
    sys_intr_mapping_t intr[SYS_INTR_MAX_INTR];    /**< array of interrupts */
    uint8                intr_mode;                  /**< interrupt mode, 0: interrupt pin, 1: msi*/
    uint8                rsv[3];
};
typedef struct sys_intr_global_cfg_s sys_intr_global_cfg_t;

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
/**
 @brief Initialize interrupt module
*/
extern int32
sys_goldengate_interrupt_init(uint8 lchip, void* intr_global_cfg);

/**
 @brief De-Initialize interrupt module
*/
extern int32
sys_goldengate_interrupt_deinit(uint8 lchip);
/**
 @brief Clear interrupt status
*/
extern int32
sys_goldengate_interrupt_clear_status(uint8 lchip, ctc_intr_type_t* p_type, uint32 with_sup);

/**
 @brief Get interrupt status
*/
extern int32
sys_goldengate_interrupt_get_status(uint8 lchip, ctc_intr_type_t* p_type, ctc_intr_status_t* p_status);

/**
 @brief Set interrupt enable
*/
extern int32
sys_goldengate_interrupt_set_en(uint8 lchip, ctc_intr_type_t* p_type, uint32 enable);

/**
 @brief Set interrupt enable
*/
extern int32
sys_goldengate_interrupt_set_en_internal(uint8 lchip, sys_intr_type_t* p_type, uint32 enable);

/**
 @brief Get interrupt enable
*/
extern int32
sys_goldengate_interrupt_get_en(uint8 lchip, ctc_intr_type_t* p_type, uint32* p_enable);

/**
 @brief Register event callback function
*/
extern int32
sys_goldengate_interrupt_register_event_cb(uint8 lchip, ctc_interrupt_event_t event, CTC_INTERRUPT_EVENT_FUNC cb);

extern int32
sys_goldengate_interrupt_get_tbl_id(uint8 lchip, ctc_intr_type_t* type);
#ifdef __cplusplus
}
#endif

#endif

