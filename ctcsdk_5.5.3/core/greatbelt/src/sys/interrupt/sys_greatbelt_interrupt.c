/**
 @file ctc_greatbelt_interrupt.c

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
#include "ctc_error.h"
#include "ctc_macro.h"
#include "ctc_interrupt.h"
#include "ctc_greatbelt_interrupt.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_interrupt.h"
#include "sys_greatbelt_interrupt_priv.h"
#include "sys_greatbelt_learning_aging.h"
#include "sys_greatbelt_ptp.h"
#include "sys_greatbelt_oam.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_dma.h"
#include "greatbelt/include/drv_lib.h"
#include "greatbelt/include/drv_chip_ctrl.h"
#include "greatbelt/include/drv_enum.h"
/****************************************************************************
*
* Defines and Macros
*
*****************************************************************************/
/* register is GbSupInterruptFatal */
#define SYS_INTR_GB_SUP_FATAL_BITS      CTC_INTR_T2B(CTC_INTR_GB_CHIP_FATAL)

/* register is GbSupInterruptNormal */
#define SYS_INTR_GB_SUP_NORMAL_BITS     CTC_INTR_T2B(CTC_INTR_GB_CHIP_NORMAL)

/* register is GbSupInterruptFunction */
#define SYS_INTR_GB_SUP_FUNC_BITS \
    (CTC_INTR_T2B(SYS_INTR_GB_FUNC_PTP_TS_CAPTURE) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_PTP_MAC_TX_TS_CAPTURE) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_PTP_TOD_PULSE_IN) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_PTP_TOD_CODE_IN_RDY) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_PTP_SYNC_PULSE_IN) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_PTP_SYNC_CODE_IN_RDY) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_OAM_TX_PKT_STATS) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_OAM_TX_OCTET_STATS) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_OAM_RX_PKT_STATS) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_OAM_RX_OCTET_STATS) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_OAM_DEFECT_CACHE) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_OAM_CLEAR_EN_3) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_OAM_CLEAR_EN_2) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_OAM_CLEAR_EN_1) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_OAM_CLEAR_EN_0) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_MDIO_XG_CHANGE) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_MDIO_1G_CHANGE) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_LINKAGG_FAILOVER) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_IPE_LEARN_CACHE) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_IPE_FIB_LEARN_FIFO) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_IPE_AGING_FIFO) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_STATS_FIFO) \
     | CTC_INTR_T2B(SYS_INTR_GB_FUNC_APS_FAILOVER))



/* register is PcieFuncIntr */
#define SYS_INTR_GB_DMA_FUNC_BITS       CTC_INTR_T2B(SYS_INTR_GB_DMA_FUNC)

/* register is PciExpCoreInterruptNormal */
#define SYS_INTR_GB_DMA_NORMAL_BITS     CTC_INTR_T2B(SYS_INTR_GB_DMA_NORMAL)

#define SYS_INTR_GB_PCIE_SEC_BITS       CTC_INTR_T2B(SYS_INTR_GB_PCIE_SECOND)

#define SYS_INTR_GB_PCIE_PRI_BITS       CTC_INTR_T2B(SYS_INTR_GB_PCIE_PRIMARY)

#define SYS_INTERRUPT_INIT_CHECK(lchip)     \
    do {                                    \
        SYS_LCHIP_CHECK_ACTIVE(lchip);           \
        if (NULL == p_gb_intr_master[lchip]) {  \
            return CTC_E_NOT_INIT; }        \
    } while(0)

/* interrupt register type in GB */
typedef enum sys_interrupt_gb_reg_e
{
    SYS_INTR_GB_REG_SUP_FATAL = 0,      /* GbSupInterruptFatal */
    SYS_INTR_GB_REG_SUP_NORMAL,         /* GbSupInterruptNormal */
    SYS_INTR_GB_REG_SUP_FUNC,           /* GbSupInterruptFunction */
    SYS_INTR_GB_REG_DMA_FUNC,           /* PcieFuncIntr */
    SYS_INTR_GB_REG_DMA_NORMAL,         /* PciExpCoreInterruptNormal */
    SYS_INTR_GB_REG_PCIE_SEC,
    SYS_INTR_GB_REG_PCIE_PRI,
    SYS_INTR_GB_REG_MAX
} sys_interrupt_gb_reg_t;


/* interrupt register bit in GB */
#define SYS_INTR_GB_REG_BIT_SUP_FATAL   CTC_INTR_T2B(SYS_INTR_GB_REG_SUP_FATAL)
#define SYS_INTR_GB_REG_BIT_SUP_NORMAL  CTC_INTR_T2B(SYS_INTR_GB_REG_SUP_NORMAL)
#define SYS_INTR_GB_REG_BIT_SUP_FUNC    CTC_INTR_T2B(SYS_INTR_GB_REG_SUP_FUNC)
#define SYS_INTR_GB_REG_BIT_DMA_FUNC    CTC_INTR_T2B(SYS_INTR_GB_REG_DMA_FUNC)
#define SYS_INTR_GB_REG_BIT_DMA_NORMAL  CTC_INTR_T2B(SYS_INTR_GB_REG_DMA_NORMAL)
#define SYS_INTR_GB_REG_BIT_PCIE_SEC    CTC_INTR_T2B(SYS_INTR_GB_REG_PCIE_SEC)
#define SYS_INTR_GB_REG_BIT_PCIE_PRI    CTC_INTR_T2B(SYS_INTR_GB_REG_PCIE_PRI)

/* sup-level function bit range */
#define SYS_INTR_GB_SUP_FUNC_BIT_BEGIN  SYS_INTR_GB_FUNC_PTP_TS_CAPTURE     /* GbSupInterruptFunction is begin with 2 */
#define SYS_INTR_GB_SUP_FUNC_BIT_END    SYS_INTR_GB_FUNC_APS_FAILOVER       /* GbSupInterruptFunction is end with 24 */

/* sub registers for SYS_INTR_GB_REG_SUP_FATAL */
static sys_intr_sub_reg_t g_intr_sub_reg_sup_fatal[SYS_INTR_GB_SUB_FATAL_MAX] =
{
    { ELoopInterruptFatal_t },
    { MacMuxInterruptFatal_t },
    { NetRxInterruptFatal_t },
    { OobFcInterruptFatal_t },
    { PtpEngineInterruptFatal_t },
    { EpeAclQosInterruptFatal_t },
    { EpeClaInterruptFatal_t },
    { EpeHdrAdjInterruptFatal_t },
    { EpeHdrEditInterruptFatal_t },
    { EpeHdrProcInterruptFatal_t },
    { EpeNextHopInterruptFatal_t },
    { EpeOamInterruptFatal_t },
    { GlobalStatsInterruptFatal_t },
    { IpeFwdInterruptFatal_t },
    { IpeHdrAdjInterruptFatal_t },
    { IpeIntfMapInterruptFatal_t },
    { IpeLkupMgrInterruptFatal_t },
    { IpePktProcInterruptFatal_t },
    { ParserInterruptFatal_t },
    { PolicingInterruptFatal_t },
    { DynamicDsInterruptFatal_t },
    { HashDsInterruptFatal_t },
    { IpeFibInterruptFatal_t },
    { NetTxInterruptFatal_t },
    { OamFwdInterruptFatal_t },
    { OamParserInterruptFatal_t },
    { OamProcInterruptFatal_t },
    { TcamCtlIntInterruptFatal_t },
    { TcamDsInterruptFatal_t },
    { BufRetrvInterruptFatal_t },
    { BufStoreInterruptFatal_t },
    { MetFifoInterruptFatal_t },
    { PbCtlInterruptFatal_t },
    { QMgrDeqInterruptFatal_t },
    { QMgrEnqInterruptFatal_t },
    { QMgrQueEntryInterruptFatal_t },
    { IntLkInterruptFatal_t },
    { -1, }, /* CTC_INTR_GB_SUB_FATAL_MON_CFG_PCI04_SERR, PCI reg 0x04 */
    { -1, }, /* CTC_INTR_GB_SUB_FATAL_UTL_PCI04_CMPL_UR, PCI reg 0x04 */
    { -1, }, /* CTC_INTR_GB_SUB_FATAL_UTL_PCI04_REC_CMPL_ABORT, PCI reg 0x04 */
    { -1, }, /* CTC_INTR_GB_SUB_FATAL_UTL_PCI04_SIG_CMPL_ABORT, PCI reg 0x04 */
    { -1, }, /* CTC_INTR_GB_SUB_FATAL_UTL_EC08_MALFORMED_TLP, PCI reg 0x68 */
    { -1, }, /* CTC_INTR_GB_SUB_FATAL_UTL_EC08_UN_EXPECTED_CMPL, PCI reg 0x68 */
    { -1, }, /* CTC_INTR_GB_SUB_FATAL_UTL_AER04_ECRC_ERROR, PCI reg 0x104 */
    { -1, }, /* CTC_INTR_GB_SUB_FATAL_UTL_EC08_UR, PCI reg 0x68 */
    { -1, }, /* CTC_INTR_GB_SUB_FATAL_UTL_EC08_CMPL_TIMEOUT, PCI reg 0x68 */
    { -1, }, /* CTC_INTR_GB_SUB_FATAL_UTL_PCI04_REC_POISONED_TLP, PCI reg 0x04 */
    { -1, }, /* CTC_INTR_GB_SUB_FATAL_UTL_PCI04_SIG_POISONED_TLP, PCI reg 0x04 */
};

/* sub registers for SYS_INTR_GB_REG_SUP_NORMAL */
sys_intr_sub_reg_t g_intr_sub_reg_sup_normal[SYS_INTR_GB_SUB_NORMAL_MAX] =
{
    { QsgmiiInterruptNormal0_t },
    { QsgmiiInterruptNormal1_t },
    { QsgmiiInterruptNormal2_t },
    { QsgmiiInterruptNormal3_t },
    { QsgmiiInterruptNormal4_t },
    { QsgmiiInterruptNormal5_t },
    { QsgmiiInterruptNormal6_t },
    { QsgmiiInterruptNormal7_t },
    { QsgmiiInterruptNormal8_t },
    { QsgmiiInterruptNormal9_t },
    { QsgmiiInterruptNormal10_t },
    { QsgmiiInterruptNormal11_t },
    { QuadPcsInterruptNormal0_t },
    { QuadPcsInterruptNormal1_t },
    { QuadPcsInterruptNormal2_t },
    { QuadPcsInterruptNormal3_t },
    { QuadPcsInterruptNormal4_t },
    { QuadPcsInterruptNormal5_t },
    { SgmacInterruptNormal0_t },
    { SgmacInterruptNormal1_t },
    { SgmacInterruptNormal2_t },
    { SgmacInterruptNormal3_t },
    { SgmacInterruptNormal4_t },
    { SgmacInterruptNormal5_t },
    { SgmacInterruptNormal6_t },
    { SgmacInterruptNormal7_t },
    { SgmacInterruptNormal8_t },
    { SgmacInterruptNormal9_t },
    { SgmacInterruptNormal10_t },
    { SgmacInterruptNormal11_t },
    { QuadMacInterruptNormal0_t },
    { QuadMacInterruptNormal1_t },
    { QuadMacInterruptNormal2_t },
    { QuadMacInterruptNormal3_t },
    { QuadMacInterruptNormal4_t },
    { QuadMacInterruptNormal5_t },
    { QuadMacInterruptNormal6_t },
    { QuadMacInterruptNormal7_t },
    { QuadMacInterruptNormal8_t },
    { QuadMacInterruptNormal9_t },
    { QuadMacInterruptNormal10_t },
    { QuadMacInterruptNormal11_t },
    { CpuMacInterruptNormal_t },
    { -1 }, /* CTC_INTR_GB_SUB_NORMAL_HSS0_EYE_QUALITY, Receiver Internal Status in HSS, refer to P207 of SA15-6338-HSS12G-Cu45-DBK */
    { -1 }, /* CTC_INTR_GB_SUB_NORMAL_HSS1_EYE_QUALITY, Receiver Internal Status in HSS, refer to P207 of SA15-6338-HSS12G-Cu45-DBK */
    { -1 }, /* CTC_INTR_GB_SUB_NORMAL_HSS2_EYE_QUALITY, Receiver Internal Status in HSS, refer to P207 of SA15-6338-HSS12G-Cu45-DBK */
    { -1 }, /* CTC_INTR_GB_SUB_NORMAL_HSS3_EYE_QUALITY, Receiver Internal Status in HSS, refer to P207 of SA15-6338-HSS12G-Cu45-DBK */
    { ELoopInterruptNormal_t },
    { MacMuxInterruptNormal_t },
    { NetRxInterruptNormal_t },
    { NetTxInterruptNormal_t },
    { OobFcInterruptNormal_t },
    { PtpEngineInterruptNormal_t },
    { EpeAclQosInterruptNormal_t },
    { EpeClaInterruptNormal_t },
    { EpeHdrAdjInterruptNormal_t },
    { EpeHdrEditInterruptNormal_t },
    { EpeHdrProcInterruptNormal_t },
    { EpeNextHopInterruptNormal_t },
    { GlobalStatsInterruptNormal_t },
    { IpeFwdInterruptNormal_t },
    { IpeHdrAdjInterruptNormal_t },
    { IpeIntfMapInterruptNormal_t },
    { IpeLkupMgrInterruptNormal_t },
    { IpePktProcInterruptNormal_t },
    { I2CMasterInterruptNormal_t },
    { PolicingInterruptNormal_t },
    { SharedDsInterruptNormal_t },  /* 2 bits (3,7) reserved */
    { DsAgingInterruptNormal_t },   /* 1 bits (0) reserved */
    { DynamicDsInterruptNormal_t },
    { IpeFibInterruptNormal_t },
    { AutoGenPktInterruptNormal_t },
    { OamFwdInterruptNormal_t },
    { OamProcInterruptNormal_t },
    { TcamCtlIntInterruptNormal_t },
    { TcamDsInterruptNormal_t },
    { BufRetrvInterruptNormal_t },
    { BufStoreInterruptNormal_t },
    { MetFifoInterruptNormal_t },
    { PbCtlInterruptNormal_t },
    { QMgrDeqInterruptNormal_t },
    { QMgrEnqInterruptNormal_t },
    { QMgrQueEntryInterruptNormal_t },
    { MacLedDriverInterruptNormal_t },
    { EpeOamInterruptNormal_t },
    { OamParserInterruptNormal_t },
};

/****************************************************************************
*
* Global and Declaration
*
*****************************************************************************/
sys_intr_master_t* p_gb_intr_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

extern dal_op_t g_dal_op;
/****************************************************************************
*
* Function
*
*****************************************************************************/

/**
 @brief mapping from CTC_INTR_GB_CHIP_XXX to SYS_INTR_GB_FUNC_XXX
*/
STATIC int32
_sys_greatbelt_interrupt_type_mapping(uint8 lchip, uint32 intr, uint32 sub_intr, uint32* sys_intr)
{
    int32 ret = CTC_E_NONE;

    switch(intr)
    {
        case CTC_INTR_GB_CHIP_FATAL:
            *sys_intr = SYS_INTR_GB_CHIP_FATAL;
            break;
        case CTC_INTR_GB_CHIP_NORMAL:
            *sys_intr = SYS_INTR_GB_CHIP_NORMAL;
            break;

        /* ptp */
        case CTC_INTR_GB_FUNC_PTP_TS_CAPTURE:
            *sys_intr = SYS_INTR_GB_FUNC_PTP_TS_CAPTURE;
            break;
        case CTC_INTR_GB_FUNC_PTP_MAC_TX_TS_CAPTURE:
            *sys_intr = SYS_INTR_GB_FUNC_PTP_MAC_TX_TS_CAPTURE;
            break;
        case CTC_INTR_GB_FUNC_PTP_TOD_PULSE_IN:
            *sys_intr = SYS_INTR_GB_FUNC_PTP_TOD_PULSE_IN;
            break;
        case CTC_INTR_GB_FUNC_PTP_TOD_CODE_IN_RDY:
            *sys_intr = SYS_INTR_GB_FUNC_PTP_TOD_CODE_IN_RDY;
            break;
        case CTC_INTR_GB_FUNC_PTP_SYNC_PULSE_IN:
            *sys_intr = SYS_INTR_GB_FUNC_PTP_SYNC_PULSE_IN;
            break;
        case CTC_INTR_GB_FUNC_PTP_SYNC_CODE_IN_RDY:
            *sys_intr = SYS_INTR_GB_FUNC_PTP_SYNC_CODE_IN_RDY;
            break;

        case CTC_INTR_GB_FUNC_OAM_DEFECT_CACHE:
            *sys_intr = SYS_INTR_GB_FUNC_OAM_DEFECT_CACHE;
            break;

        /* mdio */
        case CTC_INTR_GB_FUNC_MDIO_CHANGE:
            *sys_intr = SYS_INTR_GB_FUNC_MDIO_XG_CHANGE;
            break;
        case CTC_INTR_GB_FUNC_MDIO_1G_CHANGE:
            *sys_intr = SYS_INTR_GB_FUNC_MDIO_1G_CHANGE;
            break;
        /* linkagg failover */
        case CTC_INTR_GB_FUNC_LINKAGG_FAILOVER:
            *sys_intr = SYS_INTR_GB_FUNC_LINKAGG_FAILOVER;
            break;

        /* learning aging */
        case CTC_INTR_GB_FUNC_IPE_LEARN_CACHE:
            *sys_intr = SYS_INTR_GB_FUNC_IPE_LEARN_CACHE;
            break;
        case CTC_INTR_GB_FUNC_IPE_AGING_FIFO:
            *sys_intr = SYS_INTR_GB_FUNC_IPE_AGING_FIFO;
            break;

        /* flow stats */
        case CTC_INTR_GB_FUNC_STATS_FIFO:
            *sys_intr = SYS_INTR_GB_FUNC_STATS_FIFO;
            break;

        /* aps failover */
        case CTC_INTR_GB_FUNC_APS_FAILOVER:
            *sys_intr = SYS_INTR_GB_FUNC_APS_FAILOVER;
            break;

        /* DMA */
        case CTC_INTR_GB_DMA_FUNC:
            *sys_intr = SYS_INTR_GB_DMA_FUNC;
            break;
        case CTC_INTR_GB_DMA_NORMAL:
            *sys_intr = SYS_INTR_GB_DMA_NORMAL;
            break;
        default:
            ret = CTC_E_INTR_INVALID_TYPE;
            break;
    }

    return ret;
}

/**
 @brief mapping from SYS_INTR_GB_FUNC_XXX to CTC_INTR_GB_CHIP_XXX
*/
int32
sys_greatbelt_interrupt_type_unmapping(uint8 lchip, uint32 sys_intr, uint32* intr)
{

    int32 ret = CTC_E_NONE;

    switch(sys_intr)
    {
        case SYS_INTR_GB_CHIP_FATAL:
            *intr = CTC_INTR_GB_CHIP_FATAL;
            break;
        case SYS_INTR_GB_CHIP_NORMAL:
            *intr = CTC_INTR_GB_CHIP_NORMAL;
            break;

        /* ptp */
        case SYS_INTR_GB_FUNC_PTP_TS_CAPTURE:
            *intr = CTC_INTR_GB_FUNC_PTP_TS_CAPTURE;
            break;
        case SYS_INTR_GB_FUNC_PTP_MAC_TX_TS_CAPTURE:
            *intr = CTC_INTR_GB_FUNC_PTP_MAC_TX_TS_CAPTURE;
            break;
        case SYS_INTR_GB_FUNC_PTP_TOD_PULSE_IN:
            *intr = CTC_INTR_GB_FUNC_PTP_TOD_PULSE_IN;
            break;
        case SYS_INTR_GB_FUNC_PTP_TOD_CODE_IN_RDY :
            *intr = CTC_INTR_GB_FUNC_PTP_TOD_CODE_IN_RDY;
            break;
        case SYS_INTR_GB_FUNC_PTP_SYNC_PULSE_IN:
            *intr = CTC_INTR_GB_FUNC_PTP_SYNC_PULSE_IN;
            break;
        case SYS_INTR_GB_FUNC_PTP_SYNC_CODE_IN_RDY:
            *intr = CTC_INTR_GB_FUNC_PTP_SYNC_CODE_IN_RDY;
            break;
        case SYS_INTR_GB_FUNC_OAM_DEFECT_CACHE:
            *intr = CTC_INTR_GB_FUNC_OAM_DEFECT_CACHE;
            break;

/* dont care OAM clear interrupt
case SYS_INTR_GB_FUNC_OAM_CLEAR_EN_3:
case SYS_INTR_GB_FUNC_OAM_CLEAR_EN_2:
case SYS_INTR_GB_FUNC_OAM_CLEAR_EN_1:
case SYS_INTR_GB_FUNC_OAM_CLEAR_EN_0:
    *intr = ;
    break;
*/

        /* mdio */
        case SYS_INTR_GB_FUNC_MDIO_XG_CHANGE:
            *intr = CTC_INTR_GB_FUNC_MDIO_CHANGE;
            break;
        case SYS_INTR_GB_FUNC_MDIO_1G_CHANGE:
            *intr = CTC_INTR_GB_FUNC_MDIO_1G_CHANGE;
            break;
        /* linkagg failover */
        case SYS_INTR_GB_FUNC_LINKAGG_FAILOVER:
            *intr = CTC_INTR_GB_FUNC_LINKAGG_FAILOVER;
            break;

        /* learning */
        case SYS_INTR_GB_FUNC_IPE_LEARN_CACHE:
            *intr = CTC_INTR_GB_FUNC_IPE_LEARN_CACHE;
            break;

/* dont care about fib learning
case SYS_INTR_GB_FUNC_IPE_FIB_LEARN_FIFO:
    *intr = ;
    break;
*/
        /* aging */
        case SYS_INTR_GB_FUNC_IPE_AGING_FIFO:
            *intr = CTC_INTR_GB_FUNC_IPE_AGING_FIFO;
            break;

        /* flow stats */
        case SYS_INTR_GB_FUNC_STATS_FIFO:
            *intr = CTC_INTR_GB_FUNC_STATS_FIFO;
            break;

        /* aps failover */
        case SYS_INTR_GB_FUNC_APS_FAILOVER:
            *intr = CTC_INTR_GB_FUNC_APS_FAILOVER;
            break;

        /* DMA */
        case SYS_INTR_GB_DMA_FUNC:
            *intr = CTC_INTR_GB_DMA_FUNC;
            break;
        case SYS_INTR_GB_DMA_NORMAL:
            *intr = CTC_INTR_GB_DMA_NORMAL;
            break;
/* dont care about pcie interrupt
case SYS_INTR_GB_PCIE_SECOND:
    *intr = ;
    break;
case SYS_INTR_GB_PCIE_PRIMARY:
    *intr = ;
    break;
*/
        default:
            ret = CTC_E_INTR_INVALID_TYPE;
            break;
    }

    return ret;
}


#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /* simulation */
int32
sys_greatbelt_interrupt_model_sim_value(uint8 lchip, uint32 tbl_id, uint32 bit_offset, uint32 enable)
{
    uint32 cmd = 0;
    uint32 value[CTC_INTR_STAT_SIZE];
    uint32 value_set[CTC_INTR_STAT_SIZE];
    uint32 value_reset[CTC_INTR_STAT_SIZE];

    if (drv_greatbelt_io_is_asic())
    {
        return 0;
    }

    CTC_BMP_INIT(value);
    CTC_BMP_INIT(value_set);
    CTC_BMP_INIT(value_reset);

    if (enable)
    {
        /* trigger/set */
    }
    else
    {
        /* clear */
        INTR_LOCK;

        /* 1. read value */
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, value_set), INTR_MUTEX);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, value_reset), INTR_MUTEX);

        /* 2. do reset action */
        if (CTC_INTR_ALL != bit_offset)
        {
            CTC_BMP_SET(value, bit_offset);
            value_reset[0] &= value[0];
            value_reset[1] &= value[1];
            value_reset[2] &= value[2];
        }

        value_set[0] &= ~value_reset[0];
        value_set[1] &= ~value_reset[1];
        value_set[2] &= ~value_reset[2];

        /* 3. write value */
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, value_set), INTR_MUTEX);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, value_set), INTR_MUTEX);

        INTR_UNLOCK;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_interrupt_model_sim_mask(uint8 lchip, uint32 tbl_id, uint32 bit_offset, uint32 enable)
{
    uint32 cmd = 0;
    uint32 mask[CTC_INTR_STAT_SIZE];
    uint32 mask_set[CTC_INTR_STAT_SIZE];
    uint32 mask_reset[CTC_INTR_STAT_SIZE];

    if (drv_greatbelt_io_is_asic())
    {
        return 0;
    }

    CTC_BMP_INIT(mask);
    CTC_BMP_INIT(mask_set);
    CTC_BMP_INIT(mask_reset);

    INTR_LOCK;
    if (enable)
    {
        /* 1. read value */
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask_set), INTR_MUTEX);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, mask_reset), INTR_MUTEX);

        /* 2. do reset action */
        if (CTC_INTR_ALL != bit_offset)
        {
            CTC_BMP_SET(mask, bit_offset);
            mask_reset[0] &= mask[0];
            mask_reset[1] &= mask[1];
            mask_reset[2] &= mask[2];
        }

        mask_set[0] &= ~mask_reset[0];
        mask_set[1] &= ~mask_reset[1];
        mask_set[2] &= ~mask_reset[2];

        /* 3. write value */
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask_set), INTR_MUTEX);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, mask_set), INTR_MUTEX);
    }
    else
    {
        /* 1. read value from set */
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask_set), INTR_MUTEX);

        /* 2. sync value to reset */
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, mask_set), INTR_MUTEX);
    }

    INTR_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_interrupt_model_sim_trigger(uint8 lchip, ctc_intr_type_t* p_type)
{
    uint32 index = 0;
    uint32 cmd = 0;
    uint32 bit_offset = 0;
    uint32 value[CTC_INTR_STAT_SIZE];
    uint32 tbl_id = 0;
    uint32 sys_intr = 0;

    SYS_INTERRUPT_INIT_CHECK(lchip);
    CTC_BMP_INIT(value);

    CTC_ERROR_RETURN(_sys_greatbelt_interrupt_type_mapping(lchip, p_type->intr, p_type->sub_intr, &sys_intr));

    if (CTC_IS_BIT_SET(SYS_INTR_GB_SUP_FUNC_BITS, sys_intr))
    {
        tbl_id = GbSupInterruptFunction_t;
        bit_offset = p_type->intr - SYS_INTR_GB_SUP_FUNC_BIT_BEGIN;
    }
    else if (CTC_IS_BIT_SET(SYS_INTR_GB_SUP_FATAL_BITS, sys_intr))
    {
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_FATAL_MAX);
        tbl_id = GbSupInterruptFatal_t;
        bit_offset = p_type->sub_intr;
    }
    else if (CTC_IS_BIT_SET(SYS_INTR_GB_SUP_NORMAL_BITS, sys_intr))
    {
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_NORMAL_MAX);
        tbl_id = GbSupInterruptNormal_t;
        bit_offset = p_type->sub_intr;
    }
    else if (CTC_IS_BIT_SET(SYS_INTR_GB_DMA_FUNC_BITS, sys_intr))
    {
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_DMA_FUNC_MAX);
        tbl_id = PcieFuncIntr_t;
        bit_offset = p_type->sub_intr;
    }
    else if (CTC_IS_BIT_SET(SYS_INTR_GB_DMA_NORMAL_BITS, sys_intr))
    {
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_DMA_NORMAL_MAX);
        tbl_id = PciExpCoreInterruptNormal_t;
        bit_offset = p_type->sub_intr;
    }
    else
    {
        return CTC_E_INTR_INVALID_TYPE;
    }

    INTR_LOCK;

    /* get value set */
    index = INTR_INDEX_VAL_SET;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, index, cmd, value), INTR_MUTEX);

    /* set bit */
    CTC_BMP_SET(value, bit_offset);

    /* write value set */
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, index, cmd, value), INTR_MUTEX);

    /* sync value set to reset */
    index = INTR_INDEX_VAL_RESET;
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, index, cmd, value), INTR_MUTEX);

    INTR_UNLOCK;

    return CTC_E_NONE;
}

#endif /* (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1) */

char*
_sys_greatbelt_interrupt_index_str(uint8 lchip, uint32 index)
{
    switch (index)
    {
    case INTR_INDEX_VAL_SET:
        return "ValueSet";

    case INTR_INDEX_VAL_RESET:
        return "ValueReset";

    case INTR_INDEX_MASK_SET:
        return "MaskSet";

    case INTR_INDEX_MASK_RESET:
        return "MaskReset";

    default:
        return "Invalid";
    }
}

STATIC int32
_sys_greatbelt_interrupt_calc_reg_bmp(uint8 lchip, sys_intr_group_t* p_group)
{
    p_group->reg_bmp = 0;

    if (p_group->intr_bmp & SYS_INTR_GB_SUP_FATAL_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GB_REG_SUP_FATAL);
    }

    if (p_group->intr_bmp & SYS_INTR_GB_SUP_NORMAL_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GB_REG_SUP_NORMAL);
    }

    if (p_group->intr_bmp & SYS_INTR_GB_SUP_FUNC_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GB_REG_SUP_FUNC);
    }

    if (p_group->intr_bmp & SYS_INTR_GB_DMA_FUNC_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GB_REG_DMA_FUNC);
    }

    if (p_group->intr_bmp & SYS_INTR_GB_DMA_NORMAL_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GB_REG_DMA_NORMAL);
    }

    if (p_group->intr_bmp & SYS_INTR_GB_PCIE_SEC_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GB_REG_PCIE_SEC);
    }

    if (p_group->intr_bmp & SYS_INTR_GB_PCIE_PRI_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GB_REG_PCIE_PRI);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_dump_intr(uint8 lchip, sys_intr_t* p_intr)
{
    SYS_INTR_DUMP("%-9d %-9d %-9d %-10p %-10s\n", p_intr->group, p_intr->intr, p_intr->occur_count, (void*)p_intr->isr, p_intr->desc);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_dump_group(uint8 lchip, sys_intr_group_t* p_group)
{
    sys_intr_t* p_intr = NULL;
    uint32 i = 0;

    if (p_group->group < 0)
    {
        return CTC_E_NONE;
    }

    SYS_INTR_DUMP("Group %d, IRQ %d, Priority %d, %s\n", p_group->group, p_group->irq, p_group->prio, p_group->desc);
    SYS_INTR_DUMP("Interrupt Count:     %d\n", p_group->intr_count);
    SYS_INTR_DUMP("Occur Count:         %d\n", p_group->occur_count);
    SYS_INTR_DUMP("Interrupt Bitmap:    %08X\n", p_group->intr_bmp);

    SYS_INTR_DUMP("%-9s %-9s %-9s %-10s %-10s\n", "Group", "Interrupt", "Count", "ISR", "Desc");
    SYS_INTR_DUMP("----------------------------------------------------\n");

    for (i = 0; i < CTC_UINT32_BITS; i++)
    {
        if (CTC_IS_BIT_SET(p_group->intr_bmp, i))
        {
            p_intr = &(p_gb_intr_master[lchip]->intr[i]);
            if (p_intr->valid)
            {
                _sys_greatbelt_interrupt_dump_intr(lchip, p_intr);
            }
        }
    }

    SYS_INTR_DUMP("\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_dump_group_reg(uint8 lchip)
{
    interrupt_map_ctl_t map_ctl;
    sup_intr_mask_cfg_t intr_mask;
    uint32 cmd = 0;
    uint32 tbl_id = 0;

    if (!p_gb_intr_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    sal_memset(&map_ctl, 0, sizeof(map_ctl));
    sal_memset(&intr_mask, 0, sizeof(intr_mask));

    tbl_id = InterruptMapCtl_t;
    cmd = DRV_IOR(InterruptMapCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &map_ctl));
    SYS_INTR_DUMP("Group - Interrupt Mapping reg (%d, %s)\n", tbl_id, TABLE_NAME(tbl_id));
    SYS_INTR_DUMP("%-10s %-8s\n", "Index", "Value");
    SYS_INTR_DUMP("--------------------------------------------------------\n");
    SYS_INTR_DUMP("%-10s %08X\n", "Valid", 0x1FFFFFFF);
    SYS_INTR_DUMP("%-10s %08X\n", "Group0", map_ctl.sup_intr_map0);
    SYS_INTR_DUMP("%-10s %08X\n", "Group1", map_ctl.sup_intr_map1);
    SYS_INTR_DUMP("%-10s %08X\n", "Group2", map_ctl.sup_intr_map2);
    SYS_INTR_DUMP("%-10s %08X\n", "Group3", map_ctl.sup_intr_map3);
    SYS_INTR_DUMP("%-10s %08X\n", "Group4", map_ctl.sup_intr_map4);
    SYS_INTR_DUMP("%-10s %08X\n", "Group5", map_ctl.sup_intr_map5);

    SYS_INTR_DUMP("\n");

    tbl_id = SupIntrMaskCfg_t;
    cmd = DRV_IOR(SupIntrMaskCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intr_mask));

    SYS_INTR_DUMP("Group reg (%d, %s)\n", tbl_id, TABLE_NAME(tbl_id));
    SYS_INTR_DUMP("%-10s %-8s\n", "Index", "Value");
    SYS_INTR_DUMP("--------------------------------------------------------\n");
    SYS_INTR_DUMP("%-10s %08X\n", "Valid", 0x3F);
    SYS_INTR_DUMP("%-10s %08X\n", "Value", intr_mask.sup_intr_value);
    SYS_INTR_DUMP("%-10s %08X\n", "MaskSet", intr_mask.sup_intr_mask_set);
    SYS_INTR_DUMP("%-10s %08X\n", "MaskReset", intr_mask.sup_intr_mask_reset);

    SYS_INTR_DUMP("\n");
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_dump_one_reg(uint8 lchip, uint32 intr, uint32 tbl_id)
{
    uint32 cmd = 0;
    uint32 index = 0;
    uint32 func_intr = 0;
    uint32 value[INTR_INDEX_MAX][CTC_INTR_STAT_SIZE];
    uint32 valid[CTC_INTR_STAT_SIZE];
    sys_intr_t* p_intr = NULL;
    tables_info_t* p_tbl = NULL;
    fields_t* p_fld = NULL;
    uint32 fld_id = 0;
    uint32 i = 0;
    uint32 bits = 0;

    sal_memset(valid, 0, sizeof(valid));
    sal_memset(value, 0, sizeof(value));

    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, value[INTR_INDEX_VAL_SET]));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, value[INTR_INDEX_VAL_RESET]));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, value[INTR_INDEX_MASK_SET]));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, value[INTR_INDEX_MASK_RESET]));

    if (SYS_INTR_GB_SUP_FUNC_BITS & CTC_INTR_T2B(intr))
    {
        func_intr = SYS_INTR_GB_FUNC_PTP_TS_CAPTURE;
        p_intr = &p_gb_intr_master[lchip]->intr[func_intr];
        SYS_INTR_DUMP("Intr (%d, %s) reg (%d, %s)\n", func_intr, p_intr->desc, tbl_id, TABLE_NAME(tbl_id));

        SYS_INTR_DUMP("......\n");

        func_intr = SYS_INTR_GB_FUNC_APS_FAILOVER;
        p_intr = &p_gb_intr_master[lchip]->intr[func_intr];
        SYS_INTR_DUMP("Intr (%d, %s) reg (%d, %s)\n", func_intr, p_intr->desc, tbl_id, TABLE_NAME(tbl_id));
    }
    else
    {
        p_intr = &p_gb_intr_master[lchip]->intr[intr];
        SYS_INTR_DUMP("Intr (%d, %s) reg (%d, %s)\n", intr, p_intr->desc, tbl_id, TABLE_NAME(tbl_id));
    }

    p_tbl = TABLE_INFO_PTR(tbl_id);

    for (fld_id = 0; fld_id < p_tbl->num_fields; fld_id++)
    {
        p_fld = &(p_tbl->ptr_fields[fld_id]);

        for (i = 0; i < p_fld->len; i++)
        {
            CTC_BIT_SET(valid[p_fld->word_offset], (p_fld->bit_offset + i));
            bits++;
        }
    }


    SYS_INTR_DUMP("%-10s %-8s   %s\n", "Index", "Address", "Value");
    SYS_INTR_DUMP("--------------------------------------------------------\n");

    for (i = 0; i < TABLE_ENTRY_SIZE(tbl_id)/4; i++)
    {
        /* Valid */
        SYS_INTR_DUMP("%-10s %-8d   %08X\n", "Valid", bits, valid[i]);

        /* Value */
        for (index = 0; index < INTR_INDEX_MAX; index++)
        {
            SYS_INTR_DUMP("%-10s %08X   %08X\n", _sys_greatbelt_interrupt_index_str(lchip, index),
                          TABLE_DATA_BASE(tbl_id) + (i * 16) + (index * 4),
                          value[index][i]);
        }

        SYS_INTR_DUMP("\n");
    }

    if (SYS_INTR_GB_SUP_FUNC_BITS & CTC_INTR_T2B(intr))
    {
        for (i = SYS_INTR_GB_SUP_FUNC_BIT_BEGIN; i <= SYS_INTR_GB_SUP_FUNC_BIT_END; i++)
        {
            if (CTC_BMP_ISSET(value[INTR_INDEX_VAL_SET], (i - SYS_INTR_GB_SUP_FUNC_BIT_BEGIN)))
            {
                p_intr = &(p_gb_intr_master[lchip]->intr[i]);
                if (p_intr)
                {
                    SYS_INTR_DUMP("Interrupt (%d, %s)\n", i, p_intr->desc);
                }
            }
        }
        SYS_INTR_DUMP("\n");
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_clear_status_common(uint8 lchip, uint32 bit_offset, uint32 tbl_id)
{
    uint32 cmd = 0;
    uint32 index = INTR_INDEX_VAL_RESET;
    uint32 value[CTC_INTR_STAT_SIZE];

    CTC_BMP_INIT(value);
    if (CTC_INTR_ALL != bit_offset)
    {
        CTC_BMP_SET(value, bit_offset);
    }
    else
    {
        sal_memset(value, 0xFF, TABLE_ENTRY_SIZE(tbl_id));
    }

    INTR_LOCK;
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, index, cmd, value), INTR_MUTEX);
    INTR_UNLOCK;

#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /* simulation */
    sys_greatbelt_interrupt_model_sim_value(lchip, tbl_id, bit_offset, FALSE);
#endif

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_clear_status_sup_fatal(uint8 lchip, sys_intr_type_t* p_type, uint32 with_sup)
{
    uint32 tbl_id = GbSupInterruptFatal_t;
    uint32 cmd = 0;
    uint32 value[CTC_INTR_STAT_SIZE];    /* max is int_lk_interrupt_fatal_t */
    int32 ret = CTC_E_NONE;

    CTC_BMP_INIT(value);

    if (CTC_INTR_ALL == p_type->sub_intr)
    {
        /* not support clear all sub interrupt */
        return CTC_E_INTR_INVALID_SUB_TYPE;
    }

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_FATAL_MAX);

    if (SYS_INTR_GB_SUB_FATAL_MON_CFG_PCI04_SERR > p_type->sub_intr)
    {
        tbl_id = g_intr_sub_reg_sup_fatal[p_type->sub_intr].tbl_id;
        if (tbl_id < 0)
        {
            return CTC_E_INTR_INVALID_PARAM;
        }

        INTR_LOCK;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, value), INTR_MUTEX);
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, value), INTR_MUTEX);
        INTR_UNLOCK;
    }
    else
    {
        /* CTC_INTR_GB_SUB_FATAL_UTL_* */
        return CTC_E_INTR_NOT_SUPPORT;
    }

    if (with_sup)
    {
        ret = _sys_greatbelt_interrupt_clear_status_common(lchip, p_type->sub_intr, tbl_id);
    }

    return ret;
}

STATIC int32
_sys_greatbelt_interrupt_clear_status_sup_normal(uint8 lchip, sys_intr_type_t* p_type, uint32 with_sup)
{
    uint32 tbl_id = GbSupInterruptNormal_t;
    uint32 cmd = 0;
    uint32 value[CTC_INTR_STAT_SIZE];    /* max is int_lk_interrupt_fatal_t */
    int32 ret = CTC_E_NONE;

    CTC_BMP_INIT(value);

    if (CTC_INTR_ALL == p_type->sub_intr)
    {
        /* not support clear all sub interrupt */
        return CTC_E_INTR_INVALID_SUB_TYPE;
    }

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_NORMAL_MAX);

    if ((SYS_INTR_GB_SUB_NORMAL_HSS0_EYE_QUALITY > p_type->sub_intr)
        || (SYS_INTR_GB_SUB_NORMAL_HSS3_EYE_QUALITY < p_type->sub_intr))
    {
        tbl_id = g_intr_sub_reg_sup_normal[p_type->sub_intr].tbl_id;
        if (tbl_id < 0)
        {
            return CTC_E_INTR_INVALID_PARAM;
        }

        INTR_LOCK;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, value), INTR_MUTEX);
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, value), INTR_MUTEX);
        INTR_UNLOCK;
    }
    else
    {
        /* CTC_INTR_GB_SUB_FATAL_UTL_* */
        return CTC_E_INTR_NOT_SUPPORT;
    }

    if (with_sup)
    {
        ret = _sys_greatbelt_interrupt_clear_status_common(lchip, p_type->sub_intr, tbl_id);
    }

    return ret;
}

STATIC int32
_sys_greatbelt_interrupt_clear_status_sup_func(uint8 lchip, sys_intr_type_t* p_type, uint32 with_sup)
{
    uint32 tbl_id = GbSupInterruptFunction_t;
    uint32 bit_offset = 0;

    INTR_SUP_FUNC_TYPE_CHECK(p_type->intr);
    bit_offset = p_type->intr - SYS_INTR_GB_SUP_FUNC_BIT_BEGIN;
    return _sys_greatbelt_interrupt_clear_status_common(lchip, bit_offset, tbl_id);
}

STATIC int32
_sys_greatbelt_interrupt_clear_status_dma_func(uint8 lchip, sys_intr_type_t* p_type, uint32 with_sup)
{
    uint32 tbl_id = PcieFuncIntr_t;

    if (CTC_INTR_ALL == p_type->sub_intr)
    {
        /* not support clear all sub interrupt */
        return CTC_E_INTR_INVALID_SUB_TYPE;
    }

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_DMA_FUNC_MAX);
    return _sys_greatbelt_interrupt_clear_status_common(lchip, p_type->sub_intr, tbl_id);
}

STATIC int32
_sys_greatbelt_interrupt_clear_status_dma_normal(uint8 lchip, sys_intr_type_t* p_type, uint32 with_sup)
{
    uint32 tbl_id = PciExpCoreInterruptNormal_t;

    if (CTC_INTR_ALL == p_type->sub_intr)
    {
        /* not support clear all sub interrupt */
        return CTC_E_INTR_INVALID_SUB_TYPE;
    }

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_DMA_NORMAL_MAX);
    return _sys_greatbelt_interrupt_clear_status_common(lchip, p_type->sub_intr, tbl_id);
}

STATIC int32
_sys_greatbelt_interrupt_clear_status_pcie_sec(uint8 lchip, sys_intr_type_t* p_type, uint32 with_sup)
{
    return CTC_E_INTR_NOT_SUPPORT;
}

STATIC int32
_sys_greatbelt_interrupt_clear_status_pcie_pri(uint8 lchip, sys_intr_type_t* p_type, uint32 with_sup)
{
    return CTC_E_INTR_NOT_SUPPORT;
}

STATIC int32
_sys_greatbelt_interrupt_get_status_sup_level(uint8 lchip, sys_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    uint32 cmd = 0;
    uint32 index = INTR_INDEX_VAL_SET;
    uint32 value[CTC_INTR_STAT_SIZE];

    CTC_BMP_INIT(p_status->bmp);

    INTR_LOCK;
    /* 1. get gb_sup_interrupt_fatal_t */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(GbSupInterruptFatal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, index, cmd, &value), INTR_MUTEX);
    if (value[0] || value[1])
    {
        CTC_BMP_SET(p_status->bmp, CTC_INTR_GB_CHIP_FATAL);
    }

    /* 2. get gb_sup_interrupt_normal_t */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(GbSupInterruptNormal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, index, cmd, &value), INTR_MUTEX);
    if (value[0] || value[1] || value[2])
    {
        CTC_BMP_SET(p_status->bmp, CTC_INTR_GB_CHIP_NORMAL);
    }

    /* 3. get gb_sup_interrupt_function_t */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(GbSupInterruptFunction_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, index, cmd, &value), INTR_MUTEX);
    if (value[0])
    {
        p_status->bmp[0] |= (value[0] << SYS_INTR_GB_SUP_FUNC_BIT_BEGIN);
    }

    /* 4. get pcie_func_intr_t */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(PcieFuncIntr_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, index, cmd, &value), INTR_MUTEX);
    if (value[0])
    {
        CTC_BMP_SET(p_status->bmp, CTC_INTR_GB_DMA_FUNC);
    }

    /* 5. get pci_exp_core_interrupt_normal_t */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(PciExpCoreInterruptNormal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, index, cmd, &value), INTR_MUTEX);
    if (value[0] || value[1])
    {
        CTC_BMP_SET(p_status->bmp, CTC_INTR_GB_DMA_NORMAL);
    }

    /* PCIe Pri/Sec is TODO */

    INTR_UNLOCK;

    p_status->bit_count = CTC_INTR_GB_MAX;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_get_status_common(uint8 lchip, uint32 tbl_id, uint32 bit_count, ctc_intr_status_t* p_status)
{
    uint32 cmd = 0;
    uint32 mask[CTC_INTR_STAT_SIZE] = {0};

    INTR_LOCK;
    CTC_BMP_INIT(p_status->bmp);
    p_status->bit_count = 0;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, p_status->bmp), INTR_MUTEX);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask), INTR_MUTEX);
    p_status->bmp[0] &= (~mask[0]);
    p_status->bmp[1] &= (~mask[1]);
    p_status->bmp[2] &= (~mask[2]);
    p_status->bit_count = bit_count;
    INTR_UNLOCK;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_get_status_sup_fatal(uint8 lchip, sys_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    uint32 cmd = 0;
    int32 tbl_id = 0;
    uint32  mask[CTC_INTR_STAT_SIZE] = {0};

    CTC_BMP_INIT(p_status->bmp);

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        /* get low-level status */
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_FATAL_MAX);

        if (SYS_INTR_GB_SUB_FATAL_MON_CFG_PCI04_SERR > p_type->sub_intr)
        {
            tbl_id = g_intr_sub_reg_sup_fatal[p_type->sub_intr].tbl_id;
            if (tbl_id < 0)
            {
                return CTC_E_INTR_INVALID_PARAM;
            }

            INTR_LOCK;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, p_status->bmp), INTR_MUTEX);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask), INTR_MUTEX);
            p_status->bmp[0] &= (~mask[0]);
            p_status->bmp[1] &= (~mask[1]);
            p_status->bmp[2] &= (~mask[2]);
            p_status->bit_count = TABLE_FIELD_NUM(tbl_id);
            INTR_UNLOCK;
        }
        else
        {
            /* CTC_INTR_GB_SUB_FATAL_UTL_* */
            return CTC_E_INTR_NOT_SUPPORT;
        }

        return CTC_E_NONE;
    }
    else
    {
        /* get sub-level status */
        return _sys_greatbelt_interrupt_get_status_common(lchip, GbSupInterruptFatal_t,
                                                          SYS_INTR_GB_SUB_FATAL_MAX, p_status);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_get_status_sup_normal(uint8 lchip, sys_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    uint32 cmd = 0;
    uint32 mask[CTC_INTR_STAT_SIZE] = {0};
    int32 tbl_id = 0;

    CTC_BMP_INIT(p_status->bmp);

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        /* get low-level status */
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_NORMAL_MAX);

        if ((SYS_INTR_GB_SUB_NORMAL_HSS0_EYE_QUALITY > p_type->sub_intr)
            || (SYS_INTR_GB_SUB_NORMAL_HSS3_EYE_QUALITY < p_type->sub_intr))
        {
            tbl_id = g_intr_sub_reg_sup_normal[p_type->sub_intr].tbl_id;
            if (tbl_id < 0)
            {
                return CTC_E_INTR_INVALID_PARAM;
            }

            INTR_LOCK;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, p_status->bmp), INTR_MUTEX);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask), INTR_MUTEX);
            p_status->bmp[0] &= (~mask[0]);
            p_status->bmp[1] &= (~mask[1]);
            p_status->bmp[2] &= (~mask[2]);
            p_status->bit_count = TABLE_FIELD_NUM(tbl_id);
            if (SharedDsInterruptNormal_t == tbl_id)
            {
                p_status->bit_count += 2;   /* 2 bits (3,7) reserved */
            }
            else if (DsAgingInterruptNormal_t == tbl_id)
            {
                p_status->bit_count += 1;   /* 1 bits (0) reserved */
            }

            INTR_UNLOCK;
        }
        else
        {
            /* CTC_INTR_GB_SUB_NORMAL_HSS*_EYE_QUALITY */
            return CTC_E_INTR_NOT_SUPPORT;
        }

        return CTC_E_NONE;
    }
    else
    {
        /* get sub-level status */
        return _sys_greatbelt_interrupt_get_status_common(lchip, GbSupInterruptNormal_t,
                                                          SYS_INTR_GB_SUB_NORMAL_MAX, p_status);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_get_status_sup_func(uint8 lchip, sys_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    uint32 tbl_id = GbSupInterruptFunction_t;
    int32 ret = CTC_E_NONE;

    ret = _sys_greatbelt_interrupt_get_status_common(lchip, tbl_id, SYS_INTR_GB_SUP_FUNC_BIT_END, p_status);
    p_status->bmp[0] = CTC_INTR_T2B(p_type->intr) & (p_status->bmp[0] << SYS_INTR_GB_SUP_FUNC_BIT_BEGIN);
    return ret;
}

STATIC int32
_sys_greatbelt_interrupt_get_status_dma_func(uint8 lchip, sys_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    uint32 tbl_id = PcieFuncIntr_t;

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        /* no low-level status */
        return CTC_E_INTR_INVALID_SUB_TYPE;
    }

    return _sys_greatbelt_interrupt_get_status_common(lchip, tbl_id, SYS_INTR_GB_SUB_DMA_FUNC_MAX, p_status);
}

STATIC int32
_sys_greatbelt_interrupt_get_status_dma_normal(uint8 lchip, sys_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    uint32 tbl_id = PciExpCoreInterruptNormal_t;

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        /* no low-level status */
        return CTC_E_INTR_INVALID_SUB_TYPE;
    }

    return _sys_greatbelt_interrupt_get_status_common(lchip, tbl_id, SYS_INTR_GB_SUB_DMA_NORMAL_MAX, p_status);
}

STATIC int32
_sys_greatbelt_interrupt_get_status_pcie_sec(uint8 lchip, sys_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    return CTC_E_INTR_NOT_SUPPORT;
}

STATIC int32
_sys_greatbelt_interrupt_get_status_pcie_pri(uint8 lchip, sys_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    return CTC_E_INTR_NOT_SUPPORT;
}

STATIC int32
_sys_greatbelt_interrupt_set_en_common(uint8 lchip, uint32 bit_offset, uint32 enable, uint32 tbl_id)
{
    uint32 cmd = 0;
    uint32 index = 0;
    uint32 mask[CTC_INTR_STAT_SIZE];

    CTC_BMP_INIT(mask);

    index = (enable) ? INTR_INDEX_MASK_RESET : INTR_INDEX_MASK_SET;

#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /* simulation */
    INTR_LOCK;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, index, cmd, mask), INTR_MUTEX);
    INTR_UNLOCK;
#endif

    if (CTC_INTR_ALL != bit_offset)
    {
        CTC_BMP_SET(mask, bit_offset);
    }
    else
    {
        sal_memset(mask, 0xFF, TABLE_ENTRY_SIZE(tbl_id));
    }

    INTR_LOCK;
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, index, cmd, mask), INTR_MUTEX);
    INTR_UNLOCK;

#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /* simulation */
    sys_greatbelt_interrupt_model_sim_mask(lchip, tbl_id, bit_offset, enable);
#endif

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_set_en_sup_fatal(uint8 lchip, sys_intr_type_t* p_type, uint32 enable)
{
    uint32 tbl_id = GbSupInterruptFatal_t;

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_FATAL_MAX);
    }

    return _sys_greatbelt_interrupt_set_en_common(lchip, p_type->sub_intr, enable, tbl_id);
}

STATIC int32
_sys_greatbelt_interrupt_set_en_sup_normal(uint8 lchip, sys_intr_type_t* p_type, uint32 enable)
{
    uint32 tbl_id = GbSupInterruptNormal_t;

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_NORMAL_MAX);
    }

    return _sys_greatbelt_interrupt_set_en_common(lchip, p_type->sub_intr, enable, tbl_id);
}

STATIC int32
_sys_greatbelt_interrupt_set_en_sup_func(uint8 lchip, sys_intr_type_t* p_type, uint32 enable)
{
    uint32 tbl_id = GbSupInterruptFunction_t;
    uint32 bit_offset = 0;

    INTR_SUP_FUNC_TYPE_CHECK(p_type->intr);
    bit_offset = p_type->intr - SYS_INTR_GB_SUP_FUNC_BIT_BEGIN;
    return _sys_greatbelt_interrupt_set_en_common(lchip, bit_offset, enable, tbl_id);
}

STATIC int32
_sys_greatbelt_interrupt_set_en_dma_func(uint8 lchip, sys_intr_type_t* p_type, uint32 enable)
{
    uint32 tbl_id = PcieFuncIntr_t;

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_DMA_FUNC_MAX);
    }

    return _sys_greatbelt_interrupt_set_en_common(lchip, p_type->sub_intr, enable, tbl_id);
}

STATIC int32
_sys_greatbelt_interrupt_set_en_dma_normal(uint8 lchip, sys_intr_type_t* p_type, uint32 enable)
{
    uint32 tbl_id = PciExpCoreInterruptNormal_t;

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_DMA_NORMAL_MAX);
    }

    return _sys_greatbelt_interrupt_set_en_common(lchip, p_type->sub_intr, enable, tbl_id);
}

STATIC int32
_sys_greatbelt_interrupt_set_en_pcie_sec(uint8 lchip, sys_intr_type_t* p_type, uint32 enable)
{
    return CTC_E_INTR_NOT_SUPPORT;
}

STATIC int32
_sys_greatbelt_interrupt_set_en_pcie_pri(uint8 lchip, sys_intr_type_t* p_type, uint32 enable)
{
    return CTC_E_INTR_NOT_SUPPORT;
}

STATIC int32
_sys_greatbelt_interrupt_get_en_common(uint8 lchip, uint32 bit_offset, uint32 tbl_id, uint32* p_enable)
{
    uint32 cmd = 0;
    uint32 index = INTR_INDEX_MASK_SET;
    uint32 mask[CTC_INTR_STAT_SIZE];

    CTC_BMP_INIT(mask);

    INTR_LOCK;
    *p_enable = 0;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, index, cmd, mask), INTR_MUTEX);
    *p_enable = (CTC_BMP_ISSET(mask, bit_offset)) ? FALSE : TRUE;
    INTR_UNLOCK;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_get_en_sup_fatal(uint8 lchip, sys_intr_type_t* p_type, uint32* p_enable)
{
    uint32 tbl_id = GbSupInterruptFatal_t;

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_FATAL_MAX);
    return _sys_greatbelt_interrupt_get_en_common(lchip, p_type->sub_intr, tbl_id, p_enable);
}

STATIC int32
_sys_greatbelt_interrupt_get_en_sup_normal(uint8 lchip, sys_intr_type_t* p_type, uint32* p_enable)
{
    uint32 tbl_id = GbSupInterruptNormal_t;

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_NORMAL_MAX);
    return _sys_greatbelt_interrupt_get_en_common(lchip, p_type->sub_intr, tbl_id, p_enable);
}

STATIC int32
_sys_greatbelt_interrupt_get_en_sup_func(uint8 lchip, sys_intr_type_t* p_type, uint32* p_enable)
{
    uint32 tbl_id = PcieFuncIntr_t;
    uint32 bit_offset = 0;

    INTR_SUP_FUNC_TYPE_CHECK(p_type->intr);
    bit_offset = p_type->intr - SYS_INTR_GB_SUP_FUNC_BIT_BEGIN;
    return _sys_greatbelt_interrupt_get_en_common(lchip, bit_offset, tbl_id, p_enable);
}

STATIC int32
_sys_greatbelt_interrupt_get_en_dma_func(uint8 lchip, sys_intr_type_t* p_type, uint32* p_enable)
{
    uint32 tbl_id = PcieFuncIntr_t;

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_DMA_FUNC_MAX);
    return _sys_greatbelt_interrupt_get_en_common(lchip, p_type->sub_intr, tbl_id, p_enable);
}

STATIC int32
_sys_greatbelt_interrupt_get_en_dma_normal(uint8 lchip, sys_intr_type_t* p_type, uint32* p_enable)
{
    uint32 tbl_id = PciExpCoreInterruptNormal_t;

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GB_SUB_DMA_NORMAL_MAX);
    return _sys_greatbelt_interrupt_get_en_common(lchip, p_type->sub_intr, tbl_id, p_enable);
}

STATIC int32
_sys_greatbelt_interrupt_get_en_pcie_sec(uint8 lchip, sys_intr_type_t* p_type, uint32* p_enable)
{
    return CTC_E_INTR_NOT_SUPPORT;
}

STATIC int32
_sys_greatbelt_interrupt_get_en_pcie_pri(uint8 lchip, sys_intr_type_t* p_type, uint32* p_enable)
{
    return CTC_E_INTR_NOT_SUPPORT;
}

STATIC int32
_sys_greatbelt_interrupt_dispatch_sup_fatal(uint8 lchip, sys_intr_group_t* p_group)
{
    uint32 sup_fatal[2];   /* gb_sup_interrupt_fatal_t */
    uint32 fatal_mask[2];   /* gb_sup_interrupt_fatal_t */
    sys_intr_type_t type;
    sys_intr_t* p_intr = NULL;
    uint32 cmd = 0;
    uint32 tbl_id = GbSupInterruptFatal_t;

    sal_memset(sup_fatal, 0, sizeof(sup_fatal));

    /* 1. get sup interrupt status */
    INTR_LOCK;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, &sup_fatal), INTR_MUTEX);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &fatal_mask), INTR_MUTEX);
    INTR_UNLOCK;

    sup_fatal[0] &= (~fatal_mask[0]);
    sup_fatal[1] &= (~fatal_mask[1]);

    if ((0 == sup_fatal[0]) && (0 == sup_fatal[1]))
    {
        /* no interrupt */
        return CTC_E_NONE;
    }

    /* 2. call ISR */
    type.intr = SYS_INTR_GB_CHIP_FATAL;
    p_intr = &(p_gb_intr_master[lchip]->intr[type.intr]);
    if (p_intr->valid)
    {
        p_intr->occur_count++;
        if (p_intr->isr)
        {
            SYS_INTERRUPT_DBG_INFO("Dispatch interrupt intr: %d, count: %d\n", type.intr, p_intr->occur_count);
            p_intr->isr(lchip, type.intr, sup_fatal);
        }
    }

    /* 3. clear sup interrupt status */
    INTR_LOCK;
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, sup_fatal), INTR_MUTEX);
    INTR_UNLOCK;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_dispatch_sup_normal(uint8 lchip, sys_intr_group_t* p_group)
{
    uint32 sup_normal[CTC_INTR_STAT_SIZE];   /* gb_sup_interrupt_normal_t */
    uint32 sup_normal_mask[CTC_INTR_STAT_SIZE];   /* gb_sup_interrupt_normal_t */
    sys_intr_type_t type;
    sys_intr_t* p_intr = NULL;
    uint32 cmd = 0;
    uint32 tbl_id = GbSupInterruptNormal_t;

    CTC_BMP_INIT(sup_normal);

    /* 1. get sup interrupt status */
    INTR_LOCK;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, &sup_normal), INTR_MUTEX);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &sup_normal_mask), INTR_MUTEX);
    INTR_UNLOCK;

    sup_normal[0] &= (~sup_normal_mask[0]);
    sup_normal[1] &= (~sup_normal_mask[1]);
    sup_normal[2] &= (~sup_normal_mask[2]);

    if ((0 == sup_normal[0]) && (0 == sup_normal[1]) && (0 == sup_normal[2]))
    {
        /* no interrupt */
        return CTC_E_NONE;
    }

    /* 2. call ISR */
    type.intr = SYS_INTR_GB_CHIP_NORMAL;
    p_intr = &(p_gb_intr_master[lchip]->intr[type.intr]);
    if (p_intr->valid)
    {
        p_intr->occur_count++;
        if (p_intr->isr)
        {
            SYS_INTERRUPT_DBG_INFO("Dispatch interrupt intr: %d, count: %d\n", type.intr, p_intr->occur_count);
            p_intr->isr(lchip, type.intr, sup_normal);
        }
    }

    /* 3. clear sup interrupt status */
    INTR_LOCK;
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, sup_normal), INTR_MUTEX);
    INTR_UNLOCK;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_dispatch_sup_func(uint8 lchip, sys_intr_group_t* p_group)
{
    gb_sup_interrupt_function_t sup_func;
    gb_sup_interrupt_function_t func_mask;
    sys_intr_t* p_intr = NULL;
    uint32 i = 0;
    uint32 intr = 0;
    uint32 cmd = 0;
    uint32 group_mask = 0;
    uint32 tbl_id = GbSupInterruptFunction_t;

    group_mask = (p_group->intr_bmp & SYS_INTR_GB_SUP_FUNC_BITS) >> SYS_INTR_GB_SUP_FUNC_BIT_BEGIN;

    sup_func.func_intr = 0;

    /* 1. get interrupt status */
    INTR_LOCK;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, &sup_func), INTR_MUTEX);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &func_mask), INTR_MUTEX);
    INTR_UNLOCK;

    /* 2. get masked group's interrupt status */
    sup_func.func_intr = sup_func.func_intr & group_mask;
    sup_func.func_intr &= ~func_mask.func_intr;

    if (0 == sup_func.func_intr)
    {
        /* no interrupt */
        return CTC_E_NONE;
    }

    /* 3. call ISRs */
    for (i = 0; i <= SYS_INTR_GB_SUP_FUNC_BIT_END - SYS_INTR_GB_SUP_FUNC_BIT_BEGIN; i++)
    {
        if (CTC_IS_BIT_SET(sup_func.func_intr, i))
        {
            intr = i + SYS_INTR_GB_SUP_FUNC_BIT_BEGIN;
            p_intr = &(p_gb_intr_master[lchip]->intr[intr]);
            if (p_intr->valid)
            {
                p_intr->occur_count++;
                if (p_intr->isr)
                {
                    SYS_INTERRUPT_DBG_INFO("Dispatch interrupt intr: %d, count: %d\n", intr, p_intr->occur_count);
                    p_intr->isr(lchip, intr, NULL);
                }
            }
        }
    }

    /* 4. clear interrupt status */
    INTR_LOCK;
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, &sup_func), INTR_MUTEX);
    INTR_UNLOCK;
#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /* simulation */
    sys_greatbelt_interrupt_model_sim_value(lchip, tbl_id, CTC_INTR_ALL, FALSE);
#endif
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_dispatch_dma_func(uint8 lchip, sys_intr_group_t* p_group)
{
    uint32 dma_func[1];   /* pcie_func_intr_t */
    uint32 func_mask[1];   /* pcie_func_intr_t */

    sys_intr_type_t type;
    sys_intr_t* p_intr = NULL;
    uint32 intr = 0;
    uint32 cmd = 0;
    uint32 tbl_id = PcieFuncIntr_t;

    sal_memset(dma_func, 0, sizeof(dma_func));

    /* 1. get interrupt status */
    INTR_LOCK;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, &dma_func), INTR_MUTEX);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &func_mask), INTR_MUTEX);
    INTR_UNLOCK;

    if (0 == dma_func[0])
    {
        /* no interrupt */
        return CTC_E_NONE;
    }

    INTR_LOCK;
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, &dma_func), INTR_MUTEX);
    INTR_UNLOCK;

    /* 2. call ISR */
    type.intr = SYS_INTR_GB_DMA_FUNC;
    p_intr = &(p_gb_intr_master[lchip]->intr[type.intr]);
    if (p_intr->valid)
    {
        p_intr->occur_count++;
        if (p_intr->isr)
        {
            SYS_INTERRUPT_DBG_INFO("Dispatch interrupt DMA sub_intr: 0x%08X, count: %d\n", dma_func[0], p_intr->occur_count);
            p_intr->isr(lchip, intr, dma_func);
        }
    }

#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /* simulation */
    sys_greatbelt_interrupt_model_sim_value(lchip, tbl_id, CTC_INTR_ALL, FALSE);
#endif

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_dispatch_dma_normal(uint8 lchip, sys_intr_group_t* p_group)
{
    uint32 dma_normal[2];   /* pci_exp_core_interrupt_normal_t */
    uint32 dma_mask[2];   /* pci_exp_core_interrupt_normal_t */
    sys_intr_type_t type;
    sys_intr_t* p_intr = NULL;
    uint32 cmd = 0;
    uint32 tbl_id = PciExpCoreInterruptNormal_t;

    sal_memset(dma_normal, 0, sizeof(dma_normal));

    /* 1. get dma interrupt status */
    INTR_LOCK;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, &dma_normal), INTR_MUTEX);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &dma_mask), INTR_MUTEX);
    INTR_UNLOCK;

    dma_normal[0] &= ~dma_mask[0];
    dma_normal[1] &= ~dma_mask[1];

    if ((0 == dma_normal[0]) && (0 == dma_normal[1]))
    {
        /* no interrupt */
        return CTC_E_NONE;
    }

    type.intr = SYS_INTR_GB_DMA_NORMAL;
    p_intr = &(p_gb_intr_master[lchip]->intr[type.intr]);
    if (p_intr->valid)
    {
        p_intr->occur_count++;
        if (p_intr->isr)
        {
            p_intr->isr(lchip, type.intr, dma_normal);
        }
    }

    /* 3. clear sup interrupt status */
    INTR_LOCK;
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, dma_normal), INTR_MUTEX);
    INTR_UNLOCK;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_dispatch_pcie_sec(uint8 lchip, sys_intr_group_t* p_group)
{
    /* TODO */
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_dispatch_pcie_pri(uint8 lchip, sys_intr_group_t* p_group)
{
    /* TODO */
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_interrupt_sup_intr_mask(uint8 lchip, uint8 grp_id, bool is_mask)
{
    uint32 cmd = 0;
    sup_intr_mask_cfg_t intr_mask;

    if (grp_id >= SYS_GB_MAX_INTR_GROUP)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    sal_memset(&intr_mask, 0, sizeof(sup_intr_mask_cfg_t));
    cmd = DRV_IOR(SupIntrMaskCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intr_mask));

    if (is_mask)
    {
        intr_mask.sup_intr_mask_set |= (1 << grp_id);
    }
    else
    {
        intr_mask.sup_intr_mask_reset |= (1 << grp_id);
    }

    cmd = DRV_IOW(SupIntrMaskCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intr_mask));

    return CTC_E_NONE;
}


/* get which chip occur intrrrupt by IRQ */
STATIC int
_sys_greatbelt_interrupt_get_chip(uint32 irq, uint8 lchip_num, uint8* chip_array)
{
    uint8 lchip_idx = 0;
    uint8 group_idx = 0;

    /* get which chip occur intrrupt, and get the group_id */
    for (lchip_idx=0; lchip_idx<lchip_num; lchip_idx++)
    {
        if (NULL == p_gb_intr_master[lchip_idx])
        {
            continue;
        }

        for(group_idx=0; group_idx<p_gb_intr_master[lchip_idx]->group_count; group_idx++)
        {
            if(irq == p_gb_intr_master[lchip_idx]->group[group_idx].irq)
            {
                *(chip_array+lchip_idx) = lchip_idx;
            }
        }
    }

    return CTC_E_NONE;
}



/**
 @brief Dispatch interrupt of a group
*/
void
sys_greatbelt_interrupt_dispatch(void* p_data)
{
    uint8 lchip = 0;
    uint8 occur_intr_lchip[CTC_MAX_LOCAL_CHIP_NUM] = {0};
    uint32 irq = *((uint32*)p_data);
    uint8 group_idx = 0;
    uint8 lchip_idx = 0;
    uint32 group = 0;
    sys_intr_group_t* p_group = NULL;
    uint8 chip_num = sys_greatbelt_get_local_chip_num(lchip);

    /* init lchip=0xff, 0xff is invalid value */
    for (lchip_idx=0; lchip_idx<CTC_MAX_LOCAL_CHIP_NUM; lchip_idx++)
    {
        occur_intr_lchip[lchip_idx] = 0xff;
    }

    /* get which chip occur interrupt */
    _sys_greatbelt_interrupt_get_chip(irq, chip_num, occur_intr_lchip);

    /* dispatch by chip */
    for(lchip_idx=0; lchip_idx < chip_num; lchip_idx++)
    {
        if(0xff == occur_intr_lchip[lchip_idx])
        {
            continue;
        }

        lchip = occur_intr_lchip[lchip_idx];
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);

        for (group_idx = 0; group_idx < p_gb_intr_master[lchip]->group_count; group_idx++)
        {
            if (irq == p_gb_intr_master[lchip]->group[group_idx].irq)
            {
                group = group_idx;  /* one irq correspond with one group*/
            }
        }

        if (group >= SYS_GB_MAX_INTR_GROUP)
        {
            return;
        }

        p_group = &(p_gb_intr_master[lchip]->group[group]);
        if (!p_group)
        {
            return;
        }

        SYS_INTERRUPT_DBG_INFO("Dispatch interrupt group: %d, IRQ: %d\n", group, p_group->irq);
        p_group->occur_count++;

        /* mask sup interrupt */
        _sys_greatbelt_interrupt_sup_intr_mask(lchip, p_group->group, TRUE);

        if (CTC_IS_BIT_SET(p_group->reg_bmp, SYS_INTR_GB_REG_SUP_FUNC))
        {
            _sys_greatbelt_interrupt_dispatch_sup_func(lchip, p_group);
        }

        if (CTC_IS_BIT_SET(p_group->reg_bmp, SYS_INTR_GB_REG_DMA_FUNC))
        {
            _sys_greatbelt_interrupt_dispatch_dma_func(lchip, p_group);
        }

        if (CTC_IS_BIT_SET(p_group->reg_bmp, SYS_INTR_GB_REG_SUP_FATAL))
        {
            _sys_greatbelt_interrupt_dispatch_sup_fatal(lchip, p_group);
        }

        if (CTC_IS_BIT_SET(p_group->reg_bmp, SYS_INTR_GB_REG_SUP_NORMAL))
        {
            _sys_greatbelt_interrupt_dispatch_sup_normal(lchip, p_group);
        }

        if (CTC_IS_BIT_SET(p_group->reg_bmp, SYS_INTR_GB_REG_DMA_NORMAL))
        {
            _sys_greatbelt_interrupt_dispatch_dma_normal(lchip, p_group);
        }

        if (CTC_IS_BIT_SET(p_group->reg_bmp, SYS_INTR_GB_REG_PCIE_SEC))
        {
            _sys_greatbelt_interrupt_dispatch_pcie_sec(lchip, p_group);
        }

        if (CTC_IS_BIT_SET(p_group->reg_bmp, SYS_INTR_GB_REG_PCIE_PRI))
        {
            _sys_greatbelt_interrupt_dispatch_pcie_pri(lchip, p_group);
        }

    }


    if (g_dal_op.interrupt_set_en)
    {
        g_dal_op.interrupt_set_en(p_group->irq, TRUE);
    }

    /* release sup interrupt */
    for (lchip_idx = 0; lchip_idx < chip_num; lchip_idx++)
    {
        if(0xff == occur_intr_lchip[lchip_idx])
        {
            continue;
        }
        lchip = occur_intr_lchip[lchip_idx];
        _sys_greatbelt_interrupt_sup_intr_mask(lchip, p_group->group, FALSE);
    }

    return;
}


void
sys_greatbelt_interrupt_dispatch_chip_agent(void* p_data)
{
    uint8 lchip = 0;
    uint32 group = *((uint32*)p_data);
    sys_intr_group_t* p_group = NULL;

    if (group >= SYS_GB_MAX_INTR_GROUP)
    {
        return;
    }

    p_group = &(p_gb_intr_master[lchip]->group[group]);
    if (!p_group)
    {
        return;
    }

    if (CTC_IS_BIT_SET(p_group->reg_bmp, SYS_INTR_GB_REG_DMA_FUNC))
    {
        /* DMA interrupt processed in server */
        sys_greatbelt_interrupt_dispatch(p_data);
        return;
    }

    sal_task_sleep(30);
    if (g_dal_op.interrupt_set_en)
    {
        g_dal_op.interrupt_set_en(p_group->irq, TRUE);
    }
    return;
}

sys_intr_op_t g_gb_intr_op[SYS_INTR_GB_REG_MAX] =
{
    {
        _sys_greatbelt_interrupt_clear_status_sup_fatal,
        _sys_greatbelt_interrupt_get_status_sup_fatal,
        _sys_greatbelt_interrupt_set_en_sup_fatal,
        _sys_greatbelt_interrupt_get_en_sup_fatal,
    },
    {
        _sys_greatbelt_interrupt_clear_status_sup_normal,
        _sys_greatbelt_interrupt_get_status_sup_normal,
        _sys_greatbelt_interrupt_set_en_sup_normal,
        _sys_greatbelt_interrupt_get_en_sup_normal,
    },
    {
        _sys_greatbelt_interrupt_clear_status_sup_func,
        _sys_greatbelt_interrupt_get_status_sup_func,
        _sys_greatbelt_interrupt_set_en_sup_func,
        _sys_greatbelt_interrupt_get_en_sup_func,
    },
    {
        _sys_greatbelt_interrupt_clear_status_dma_func,
        _sys_greatbelt_interrupt_get_status_dma_func,
        _sys_greatbelt_interrupt_set_en_dma_func,
        _sys_greatbelt_interrupt_get_en_dma_func,
    },
    {
        _sys_greatbelt_interrupt_clear_status_dma_normal,
        _sys_greatbelt_interrupt_get_status_dma_normal,
        _sys_greatbelt_interrupt_set_en_dma_normal,
        _sys_greatbelt_interrupt_get_en_dma_normal,
    },
    {
        _sys_greatbelt_interrupt_clear_status_pcie_sec,
        _sys_greatbelt_interrupt_get_status_pcie_sec,
        _sys_greatbelt_interrupt_set_en_pcie_sec,
        _sys_greatbelt_interrupt_get_en_pcie_sec,
    },
    {
        _sys_greatbelt_interrupt_clear_status_pcie_pri,
        _sys_greatbelt_interrupt_get_status_pcie_pri,
        _sys_greatbelt_interrupt_set_en_pcie_pri,
        _sys_greatbelt_interrupt_get_en_pcie_pri,
    },
};

STATIC int32
_sys_greatbelt_interrupt_init_ops(uint8 lchip)
{
    sys_intr_op_t** p_op = p_gb_intr_master[lchip]->op;

    p_op[SYS_INTR_GB_CHIP_FATAL]                 = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FATAL]);
    p_op[SYS_INTR_GB_CHIP_NORMAL]                = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_NORMAL]);

    p_op[SYS_INTR_GB_FUNC_PTP_TS_CAPTURE]        = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_PTP_MAC_TX_TS_CAPTURE] = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_PTP_TOD_PULSE_IN]      = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_PTP_TOD_CODE_IN_RDY]   = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_PTP_SYNC_PULSE_IN]     = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_PTP_SYNC_CODE_IN_RDY]  = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_OAM_TX_PKT_STATS]      = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_OAM_TX_OCTET_STATS]    = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_OAM_RX_PKT_STATS]      = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_OAM_RX_OCTET_STATS]    = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_OAM_DEFECT_CACHE]      = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_OAM_CLEAR_EN_3]        = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_OAM_CLEAR_EN_2]        = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_OAM_CLEAR_EN_1]        = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_OAM_CLEAR_EN_0]        = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_MDIO_XG_CHANGE]        = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_MDIO_1G_CHANGE]        = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_LINKAGG_FAILOVER]      = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_IPE_LEARN_CACHE]       = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_IPE_FIB_LEARN_FIFO]    = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_IPE_AGING_FIFO]        = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_STATS_FIFO]            = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);
    p_op[SYS_INTR_GB_FUNC_APS_FAILOVER]          = &(g_gb_intr_op[SYS_INTR_GB_REG_SUP_FUNC]);

    p_op[SYS_INTR_GB_DMA_FUNC]                   = &(g_gb_intr_op[SYS_INTR_GB_REG_DMA_FUNC]);
    p_op[SYS_INTR_GB_DMA_NORMAL]                 = &(g_gb_intr_op[SYS_INTR_GB_REG_DMA_NORMAL]);

    p_op[SYS_INTR_GB_PCIE_SECOND]                = &(g_gb_intr_op[SYS_INTR_GB_REG_PCIE_SEC]);
    p_op[SYS_INTR_GB_PCIE_PRIMARY]               = &(g_gb_intr_op[SYS_INTR_GB_REG_PCIE_PRI]);

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_interrupt_init_reg(uint8 lchip)
{
    interrupt_map_ctl_t map_ctl;
    sup_intr_mask_cfg_t intr_mask;
    uint32 mask_set = 0, addr = 0;
    uint32 mask_set_array[CTC_INTR_STAT_SIZE];
    uint8 ecc_recover_en = sys_greatbelt_chip_get_ecc_recover_en(lchip);
    uint32 cmd = 0;
    uint32 i = 0;
    sys_intr_group_t* p_group = NULL;
    sys_intr_t* p_intr = NULL;
    drv_work_platform_type_t platform_type;
    ipe_pkt_proc_interrupt_fatal_t fatal;

    if (!p_gb_intr_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    sal_memset(&map_ctl, 0, sizeof(map_ctl));
    sal_memset(&intr_mask, 0, sizeof(intr_mask));
    sal_memset(&fatal, 0, sizeof(fatal));

    mask_set = 0x3F;

    for (i = 0; i < SYS_GB_MAX_INTR_GROUP; i++)
    {
        p_group = &(p_gb_intr_master[lchip]->group[i]);
        if (p_group->group < 0)
        {
            continue;
        }

        if (p_group->group >= 0)
        {
            CTC_BIT_UNSET(mask_set, p_group->group);
        }

        switch (p_group->group)
        {
        case 0:
            map_ctl.sup_intr_map0 = p_group->intr_bmp;
            break;

        case 1:
            map_ctl.sup_intr_map1 = p_group->intr_bmp;
            break;

        case 2:
            map_ctl.sup_intr_map2 = p_group->intr_bmp;
            break;

        case 3:
            map_ctl.sup_intr_map3 = p_group->intr_bmp;
            break;

        case 4:
            map_ctl.sup_intr_map4 = p_group->intr_bmp;
            break;

        case 5:
            map_ctl.sup_intr_map5 = p_group->intr_bmp;
            break;

        default:
            break;
        }
    }

    intr_mask.sup_intr_mask_set = mask_set;

    cmd = DRV_IOW(InterruptMapCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &map_ctl));

    cmd = DRV_IOW(SupIntrMaskCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intr_mask));

    /* set Mask of GbSupInterruptFunction */
    mask_set = 0;
    for (i = 0; i < CTC_INTR_MAX_INTR; i++)
    {
        p_intr = &(p_gb_intr_master[lchip]->intr[i]);
        if (p_intr->valid)
        {
            if ((p_intr->intr >= SYS_INTR_GB_SUP_FUNC_BIT_BEGIN) && (p_intr->intr <= SYS_INTR_GB_SUP_FUNC_BIT_END))
            {
                CTC_BIT_SET(mask_set, (p_intr->intr - SYS_INTR_GB_SUP_FUNC_BIT_BEGIN));
            }
        }
    }

    cmd = DRV_IOW(GbSupInterruptFunction_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, &mask_set));

    CTC_ERROR_RETURN(drv_greatbelt_get_platform_type(&platform_type));
    if (platform_type == HW_PLATFORM)
    {
        /* set Mask of GbSupInterruptNormal[1], 0x00007800 is hss0EyeQualityNormalIntr - hss3EyeQualityNormalIntr */

        drv_greatbelt_chip_write(lchip, 0x0000008C, 0xFFFFFFFF);
        drv_greatbelt_chip_write(lchip, 0x0000009C, 0xFFFF87FF);
        drv_greatbelt_chip_write(lchip, 0x000000AC, 0xFFFFFFFF);

        drv_greatbelt_chip_write(lchip, 0x0000004C, 0xFFFFFFFF);
        drv_greatbelt_chip_write(lchip, 0x0000005C, 0xFFFFEFFF);

        /* mask utlPci04SigPoisonedTlp */
        drv_greatbelt_chip_write(lchip, 0x00000058, 0x00008000);



        drv_greatbelt_chip_write(lchip, 0x00000084, 0xFFFFFFFF);
        drv_greatbelt_chip_write(lchip, 0x00000094, 0xFFFFFFFF);
        drv_greatbelt_chip_write(lchip, 0x000000A4, 0xFFFFFFFF);

        drv_greatbelt_chip_write(lchip, 0x00000044, 0xFFFFFFFF);
        drv_greatbelt_chip_write(lchip, 0x00000054, 0xFFFFFFFF);


        if (1 == ecc_recover_en)
        {
            i = INTR_INDEX_MASK_RESET;

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_IgrCondDisProfId;
            mask_set |= 1 << DRV_ECC_INTR_IgrPortTcMinProfId;
            mask_set |= 1 << DRV_ECC_INTR_IgrPortTcPriMap;
            mask_set |= 1 << DRV_ECC_INTR_IgrPortTcThrdProfId;
            mask_set |= 1 << DRV_ECC_INTR_IgrPortTcThrdProfile;
            mask_set |= 1 << DRV_ECC_INTR_IgrPortThrdProfile;
            mask_set |= 1 << DRV_ECC_INTR_IgrPriToTcMap;
            addr = TABLE_DATA_BASE(BufStoreInterruptNormal_t) + (TABLE_ENTRY_SIZE(BufStoreInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_Edram0;
            mask_set |= 1 << DRV_ECC_INTR_Edram1;
            mask_set |= 1 << DRV_ECC_INTR_Edram2;
            mask_set |= 1 << DRV_ECC_INTR_Edram3;
            mask_set |= 1 << DRV_ECC_INTR_Edram4;
            mask_set |= 1 << DRV_ECC_INTR_Edram5;
            mask_set |= 1 << DRV_ECC_INTR_Edram6;
            mask_set |= 1 << DRV_ECC_INTR_Edram7;
            mask_set |= 1 << DRV_ECC_INTR_Edram8;
            addr = TABLE_DATA_BASE(DynamicDsInterruptNormal_t)  + (TABLE_ENTRY_SIZE(DynamicDsInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_PacketHeaderEditTunnel;
            addr = TABLE_DATA_BASE(EpeHdrEditInterruptNormal_t) + (TABLE_ENTRY_SIZE(EpeHdrEditInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_Ipv6NatPrefix;
            addr = TABLE_DATA_BASE(EpeHdrProcInterruptNormal_t) + (TABLE_ENTRY_SIZE(EpeHdrProcInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_DestChannel;
            mask_set |= 1 << DRV_ECC_INTR_DestInterface;
            mask_set |= 1 << DRV_ECC_INTR_DestPort;
            mask_set |= 1 << DRV_ECC_INTR_EgressVlanRangeProfile;
            mask_set |= 1 << DRV_ECC_INTR_EpeEditPriorityMap;
            addr = TABLE_DATA_BASE(EpeNextHopInterruptNormal_t) + (TABLE_ENTRY_SIZE(EpeNextHopInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_IpeFwdDsQcnRam;
            addr =  TABLE_DATA_BASE(IpeFwdInterruptNormal_t) + (TABLE_ENTRY_SIZE(IpeFwdInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_PhyPort;
            addr = TABLE_DATA_BASE(IpeHdrAdjInterruptNormal_t)  + (TABLE_ENTRY_SIZE(IpeHdrAdjInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_MplsCtl;
            mask_set |= 1 << DRV_ECC_INTR_PhyPortExt;
            mask_set |= 1 << DRV_ECC_INTR_RouterMac;
            mask_set |= 1 << DRV_ECC_INTR_SrcInterface;
            mask_set |= 1 << DRV_ECC_INTR_SrcPort;
            mask_set |= 1 << DRV_ECC_INTR_VlanActionProfile;
            mask_set |= 1 << DRV_ECC_INTR_VlanRangeProfile;
            addr = TABLE_DATA_BASE(IpeIntfMapInterruptNormal_t) + (TABLE_ENTRY_SIZE(IpeIntfMapInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_EcmpGroup;
            mask_set |= 1 << DRV_ECC_INTR_Rpf;
            mask_set |= 1 << DRV_ECC_INTR_SrcChannel;
            mask_set |= 1 << DRV_ECC_INTR_IpeClassificationDscpMap;
            addr = TABLE_DATA_BASE(IpePktProcInterruptNormal_t) + (TABLE_ENTRY_SIZE(IpePktProcInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_ApsBridge;
            addr = TABLE_DATA_BASE(MetFifoInterruptNormal_t) + (TABLE_ENTRY_SIZE(MetFifoInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_ChannelizeIngFc;
            mask_set |= 1 << DRV_ECC_INTR_ChannelizeMode;
            mask_set |= 1 << DRV_ECC_INTR_PauseTimerMemRd;
            addr = TABLE_DATA_BASE(NetRxInterruptNormal_t) + (TABLE_ENTRY_SIZE(NetRxInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_EgrResrcCtl;
            mask_set |= 1 << DRV_ECC_INTR_HeadHashMod;
            mask_set |= 1 << DRV_ECC_INTR_LinkAggregateMember;
            mask_set |= 1 << DRV_ECC_INTR_LinkAggregateMemberSet;
            mask_set |= 1 << DRV_ECC_INTR_LinkAggregateSgmacMember;
            mask_set |= 1 << DRV_ECC_INTR_LinkAggregationPort;
            mask_set |= 1 << DRV_ECC_INTR_QueThrdProfile;
            mask_set |= 1 << DRV_ECC_INTR_QueueHashKey;
            mask_set |= 1 << DRV_ECC_INTR_QueueNumGenCtl;
            mask_set |= 1 << DRV_ECC_INTR_QueueSelectMap;
            mask_set |= 1 << DRV_ECC_INTR_SgmacHeadHashMod;
            addr = TABLE_DATA_BASE(QMgrEnqInterruptNormal_t) + (TABLE_ENTRY_SIZE(QMgrEnqInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_StpState;
            mask_set |= 1 << DRV_ECC_INTR_Vlan;
            mask_set |= 1 << DRV_ECC_INTR_VlanProfile;
            addr = TABLE_DATA_BASE(SharedDsInterruptNormal_t) + (TABLE_ENTRY_SIZE(SharedDsInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_PolicerControl;
            mask_set |= 1 << DRV_ECC_INTR_PolicerProfile0;
            mask_set |= 1 << DRV_ECC_INTR_PolicerProfile1;
            mask_set |= 1 << DRV_ECC_INTR_PolicerProfile2;
            mask_set |= 1 << DRV_ECC_INTR_PolicerProfile3;
            addr = TABLE_DATA_BASE(PolicingInterruptNormal_t) + (TABLE_ENTRY_SIZE(PolicingInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_lpmTcamAdMem;
            addr = TABLE_DATA_BASE(IpeFibInterruptNormal_t) + (TABLE_ENTRY_SIZE(IpeFibInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_ChanShpProfile;
            mask_set |= 1 << DRV_ECC_INTR_GrpShpProfile;
            mask_set |= 1 << DRV_ECC_INTR_QueMap;
            mask_set |= 1 << DRV_ECC_INTR_QueShpCtl;
            mask_set |= 1 << DRV_ECC_INTR_QueShpProfile;
            mask_set |= 1 << DRV_ECC_INTR_QueShpWfqCtl;
            addr = TABLE_DATA_BASE(QMgrDeqInterruptNormal_t) + (TABLE_ENTRY_SIZE(QMgrDeqInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << (DRV_ECC_INTR_GrpShpWfqCtl - 32);
            addr = TABLE_DATA_BASE(QMgrDeqInterruptNormal_t) + (TABLE_ENTRY_SIZE(QMgrDeqInterruptNormal_t) * i) + 4;
            drv_greatbelt_chip_write(lchip, addr, mask_set);

            mask_set = 0;
            mask_set |= 1 << DRV_ECC_INTR_BufRetrvExcp;
            addr = TABLE_DATA_BASE(BufRetrvInterruptNormal_t) + (TABLE_ENTRY_SIZE(BufRetrvInterruptNormal_t) * i);
            drv_greatbelt_chip_write(lchip, addr, mask_set);

        }
    }

    /* set Mask of PcieFuncIntr, 0x10000 is not enabled */
    CTC_BMP_INIT(mask_set_array);
    mask_set_array[0] = 0x0000FFFF;

    cmd = DRV_IOW(PcieFuncIntr_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, mask_set_array));

    /* set Mask of ipeBridgeEopMsgFifoOverrun */
    fatal.ipe_bridge_eop_msg_fifo_overrun = 1;
    cmd = DRV_IOW(IpePktProcInterruptFatal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, &fatal));

    return CTC_E_NONE;
}

/**
 @brief Clear interrupt status
*/
int32
sys_greatbelt_interrupt_clear_status(uint8 lchip, ctc_intr_type_t* p_type, uint32 with_sup)
{
    uint32 sys_intr = 0;
    sys_intr_type_t sys_intr_type;

    SYS_INTERRUPT_INIT_CHECK(lchip);
    INTR_TYPE_CHECK(p_type->intr);


    _sys_greatbelt_interrupt_type_mapping(lchip, p_type->intr, p_type->sub_intr, &sys_intr);
    sys_intr_type.intr     = sys_intr;
    sys_intr_type.sub_intr = p_type->sub_intr;

    if (p_gb_intr_master[lchip]->op[sys_intr]->clear_status)
    {
        return p_gb_intr_master[lchip]->op[sys_intr]->clear_status(lchip, &sys_intr_type, with_sup);
    }

    return CTC_E_NONE;
}

/**
 @brief Get interrupt status
*/
int32
sys_greatbelt_interrupt_get_status(uint8 lchip, ctc_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    uint32 sys_intr = 0;
    sys_intr_type_t sys_intr_type;

    SYS_INTERRUPT_INIT_CHECK(lchip);

    _sys_greatbelt_interrupt_type_mapping(lchip, p_type->intr, p_type->sub_intr, &sys_intr);
    sys_intr_type.intr     = sys_intr;
    sys_intr_type.sub_intr = p_type->sub_intr;

    if (CTC_INTR_ALL == p_type->intr)
    {
        /* get sup-level status */
        return _sys_greatbelt_interrupt_get_status_sup_level(lchip, &sys_intr_type, p_status);
    }
    else
    {
        INTR_TYPE_CHECK(p_type->intr);

        if (p_gb_intr_master[lchip]->op[sys_intr]->get_status)
        {
            return p_gb_intr_master[lchip]->op[sys_intr]->get_status(lchip, &sys_intr_type, p_status);
        }
    }

    return CTC_E_NONE;
}

/**
 @brief Set interrupt enable
*/
int32
sys_greatbelt_interrupt_set_en(uint8 lchip, ctc_intr_type_t* p_type, uint32 enable)
{
    uint32 sys_intr = 0;
    sys_intr_type_t sys_intr_type;

    SYS_INTERRUPT_INIT_CHECK(lchip);
    INTR_TYPE_CHECK(p_type->intr);

    _sys_greatbelt_interrupt_type_mapping(lchip, p_type->intr, p_type->sub_intr, &sys_intr);
	sys_intr_type.intr     = sys_intr;
    sys_intr_type.sub_intr = p_type->sub_intr;

    if (p_gb_intr_master[lchip]->op[sys_intr]->set_en)
    {
        return p_gb_intr_master[lchip]->op[sys_intr]->set_en(lchip, &sys_intr_type, enable);
    }

    return CTC_E_NONE;
}

/**
 @brief Get interrupt enable
*/
int32
sys_greatbelt_interrupt_get_en(uint8 lchip, ctc_intr_type_t* p_type, uint32* p_enable)
{
    uint32 sys_intr = 0;
    sys_intr_type_t sys_intr_type;

    SYS_INTERRUPT_INIT_CHECK(lchip);
    INTR_TYPE_CHECK(p_type->intr);

    _sys_greatbelt_interrupt_type_mapping(lchip, p_type->intr, p_type->sub_intr, &sys_intr);

    sys_intr_type.intr     = sys_intr;
    sys_intr_type.sub_intr = p_type->sub_intr;

    if (p_gb_intr_master[lchip]->op[sys_intr]->get_en)
    {
        return p_gb_intr_master[lchip]->op[sys_intr]->get_en(lchip, &sys_intr_type, p_enable);
    }

    return CTC_E_NONE;
}

/**
 @brief Set abnormal interrupt callback function
*/
int32
sys_greatbelt_interrupt_set_abnormal_intr_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb)
{
    p_gb_intr_master[lchip]->abnormal_intr_cb = cb;
    return CTC_E_NONE;
}

/**
 @brief Register event callback function
*/
int32
sys_greatbelt_interrupt_register_event_cb(uint8 lchip, ctc_interrupt_event_t event, CTC_INTERRUPT_EVENT_FUNC cb)
{
    switch (event)
    {
    case CTC_EVENT_PTP_TX_TS_CAPUTRE:
        CTC_ERROR_RETURN(sys_greatbelt_ptp_set_tx_ts_captured_cb(lchip, cb));
        break;

    case CTC_EVENT_PTP_INPUT_CODE_READY:
        CTC_ERROR_RETURN(sys_greatbelt_ptp_set_input_code_ready_cb(lchip, cb));
        break;

    case CTC_EVENT_OAM_STATUS_UPDATE:
        CTC_ERROR_RETURN(sys_greatbelt_oam_set_defect_cb(lchip, cb));
        break;

    case CTC_EVENT_L2_SW_LEARNING:
        CTC_ERROR_RETURN(sys_greatbelt_learning_set_event_cb(lchip, cb));
        break;

    case CTC_EVENT_L2_SW_AGING:
        CTC_ERROR_RETURN(sys_greatbelt_aging_set_event_cb(lchip, cb));
        break;

    case CTC_EVENT_PORT_LINK_CHANGE:
        CTC_ERROR_RETURN(sys_greatbelt_port_set_link_status_event_cb(lchip, cb));
        break;

    case CTC_EVENT_ABNORMAL_INTR:
        CTC_ERROR_RETURN(sys_greatbelt_interrupt_set_abnormal_intr_cb(lchip, cb));
        break;

    case CTC_EVENT_ECC_ERROR:
        CTC_ERROR_RETURN(drv_greatbelt_ecc_recover_register_event_cb(cb));
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/**
 @brief Display interrupt register values in chip
*/
int32
sys_greatbelt_interrupt_dump_reg(uint8 lchip, uint32 intr)
{
    SYS_INTERRUPT_INIT_CHECK(lchip);

    if (CTC_IS_BIT_SET(intr, CTC_INTR_GB_MAX))
    {
        _sys_greatbelt_interrupt_dump_group_reg(lchip);
    }

    if (CTC_IS_BIT_SET(intr, CTC_INTR_GB_CHIP_FATAL))
    {
        _sys_greatbelt_interrupt_dump_one_reg(lchip, SYS_INTR_GB_CHIP_FATAL, GbSupInterruptFatal_t);
    }

    if (CTC_IS_BIT_SET(intr, CTC_INTR_GB_CHIP_NORMAL))
    {
        _sys_greatbelt_interrupt_dump_one_reg(lchip, SYS_INTR_GB_CHIP_NORMAL, GbSupInterruptNormal_t);
    }

    if (CTC_IS_BIT_SET(intr, CTC_INTR_GB_FUNC_PTP_TS_CAPTURE))
    {
        _sys_greatbelt_interrupt_dump_one_reg(lchip, SYS_INTR_GB_FUNC_PTP_TS_CAPTURE, GbSupInterruptFunction_t);
    }

    if (CTC_IS_BIT_SET(intr, CTC_INTR_GB_DMA_FUNC))
    {
        _sys_greatbelt_interrupt_dump_one_reg(lchip, SYS_INTR_GB_DMA_FUNC, PcieFuncIntr_t);
    }

    if (CTC_IS_BIT_SET(intr, CTC_INTR_GB_DMA_NORMAL))
    {
        _sys_greatbelt_interrupt_dump_one_reg(lchip, SYS_INTR_GB_DMA_NORMAL, PciExpCoreInterruptNormal_t);
    }

    return CTC_E_NONE;
}
int32
sys_greatbelt_interrupt_parser_type(uint8 lchip, ctc_intr_type_t type)
{

    LCHIP_CHECK(lchip);

    if (type.intr == SYS_INTR_GB_CHIP_FATAL)
    {
        if(type.sub_intr >= SYS_INTR_GB_SUB_FATAL_MAX || g_intr_sub_reg_sup_fatal[type.sub_intr].tbl_id == -1)
        {
            return CTC_E_NONE;
        }

        SYS_INTR_DUMP("Fatal Interrupr:\n    sub-intr-table:  %s\n", TABLE_NAME(g_intr_sub_reg_sup_fatal[type.sub_intr].tbl_id));
    }
    else if (type.intr == SYS_INTR_GB_CHIP_NORMAL)
    {
        if(type.sub_intr >= SYS_INTR_GB_SUB_NORMAL_MAX || g_intr_sub_reg_sup_normal[type.sub_intr].tbl_id == -1)
        {
            return CTC_E_NONE;
        }

        SYS_INTR_DUMP("Normal Interrupr:\n    sub-intr-table:  %s\n", TABLE_NAME(g_intr_sub_reg_sup_normal[type.sub_intr].tbl_id));
    }
    else
    {
        SYS_INTR_DUMP(" API or some feature is not supported \n");
			return CTC_E_NOT_SUPPORT;

    }

    return CTC_E_NONE;
}
/**
 @brief Display interrupt database values in software
*/
int32
sys_greatbelt_interrupt_dump_db(uint8 lchip)
{
    uint32 unbind = FALSE;
    uint32 i = 0;

    SYS_INTERRUPT_INIT_CHECK(lchip);
    SYS_INTR_DUMP("Using %s global configuration\n", p_gb_intr_master[lchip]->is_default ? "default" : "parameter");
    SYS_INTR_DUMP("Group Count:      %d\n", p_gb_intr_master[lchip]->group_count);
    SYS_INTR_DUMP("Interrupt Count:  %d\n", p_gb_intr_master[lchip]->intr_count);
    SYS_INTR_DUMP("Interrupt Mode:  %s\n", p_gb_intr_master[lchip]->intr_mode ? "Msi":"Pin");

    SYS_INTR_DUMP("\n");
    SYS_INTR_DUMP("##############################\n");
    SYS_INTR_DUMP("Registered Interrupts\n");
    SYS_INTR_DUMP("##############################\n");

    for (i = 0; i < SYS_GB_MAX_INTR_GROUP; i++)
    {
        _sys_greatbelt_interrupt_dump_group(lchip, &(p_gb_intr_master[lchip]->group[i]));
    }

    SYS_INTR_DUMP("##############################\n");
    SYS_INTR_DUMP("Unregistered Interrupts\n");
    SYS_INTR_DUMP("##############################\n");

    for (i = 0; i < CTC_INTR_MAX_INTR; i++)
    {
        if (!p_gb_intr_master[lchip]->intr[i].valid)
        {
            if (!unbind)
            {
                unbind = TRUE;
                SYS_INTR_DUMP("%-9s %-9s %-9s %-10s %-10s\n", "Group", "Interrupt", "Count", "ISR", "Desc");
                SYS_INTR_DUMP("----------------------------------------------------\n");
            }

            if (INVG != p_gb_intr_master[lchip]->intr[i].intr)
            {
                _sys_greatbelt_interrupt_dump_intr(lchip, &(p_gb_intr_master[lchip]->intr[i]));
            }
        }
    }

    if (!unbind)
    {
        SYS_INTR_DUMP("None\n");
    }

    return CTC_E_NONE;
}


char*
sys_greatbelt_interrupt_abnormal_action_str(uint8 lchip, ctc_interrupt_fatal_intr_action_t action)
{
    switch (action)
    {
    case CTC_INTERRUPT_FATAL_INTR_NULL:
        return "Ignore";
    case CTC_INTERRUPT_FATAL_INTR_LOG:
        return "Log";
    case CTC_INTERRUPT_FATAL_INTR_RESET:
        return "Reset Chip";
    default:
        return "Unknown";
    }
}

int32
sys_greatbelt_interrupt_chip_abnormal_isr(uint8 lchip, void* p_data)
{
    ctc_interrupt_abnormal_status_t* p_status = NULL;
    char action_str[20];

    sal_memset(action_str, 0, sizeof(action_str));
    CTC_PTR_VALID_CHECK(p_data);
    p_status = (ctc_interrupt_abnormal_status_t*)p_data;

    SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"### Detect Chip Abnormal Interrupt ###\n");
    SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Type          :   %d\n", p_status->type.intr);
    SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Sub Type      :   %d\n", p_status->type.sub_intr);
    SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Status Bits   :   %d\n", p_status->status.bit_count);
    SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Status        :   %08X %08X %08X \n",
        p_status->status.bmp[0], p_status->status.bmp[1], p_status->status.bmp[2]);
    SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Action        :   %s\n", sys_greatbelt_interrupt_abnormal_action_str(lchip, p_status->action));
    p_gb_intr_master[lchip]->occur_count[p_status->action]++;

    sal_task_sleep(100);

    return CTC_E_NONE;
}

/**
 @brief Initialize interrupt module
*/
int32
sys_greatbelt_interrupt_init(uint8 lchip, void* p_global_cfg)
{
    ctc_intr_global_cfg_t* p_global_cfg_param = (ctc_intr_global_cfg_t*)p_global_cfg;
    sys_intr_global_cfg_t* p_intr_cfg = NULL;
    uint32 group_count = 0;
    uint32 intr_count = 0;
    uintptr i = 0;
    uintptr irq_index = 0;
    uint32 j = 0;
    uint32 intr = 0;
#ifndef _SAL_LINUX_KM
    uint32 mode = 0;
#endif
    int32 group_id = 0;
    sys_intr_group_t* p_group = NULL;
    sys_intr_t* p_intr = NULL;
    int32 ret = CTC_E_NONE;
    uint32 intr_idx = 0;
    uint32 sub_intr_idx = 0;
    uint32 sys_intr = 0;
    ctc_intr_mapping_t* p_intr_mapping = NULL;
    uint32 ctc_intr_idx = 0;
    uint8 irq_base = 0; /* used for msi */

    LCHIP_CHECK(lchip);
    p_intr_cfg = (sys_intr_global_cfg_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(sys_intr_global_cfg_t));
    if (NULL == p_intr_cfg)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_intr_cfg, 0, sizeof(sys_intr_global_cfg_t));

    /* get default interrupt cfg */
    CTC_ERROR_GOTO(sys_greatbelt_interrupt_get_default_global_cfg(lchip, p_intr_cfg), ret, end);

    /* 1. get global configuration */
    if (p_global_cfg_param)
    {
        if (NULL == p_global_cfg_param->p_group)
        {
            ret =
CTC_E_INVALID_PTR;
            goto end;
        }
        if (NULL == p_global_cfg_param->p_intr)
        {
            ret =
CTC_E_INVALID_PTR;
            goto end;
        }

        group_count = p_global_cfg_param->group_count;
        intr_count = ((p_global_cfg_param->intr_count) + INTR_ADDTINAL_COUNT);

        /* mapping user define interrupt group */
        sal_memset(p_intr_cfg->group, 0, SYS_GB_MAX_INTR_GROUP*sizeof(ctc_intr_group_t));
        sal_memcpy(p_intr_cfg->group, p_global_cfg_param->p_group, group_count*sizeof(ctc_intr_group_t));

        /* init intr_cfg.intr[].group */
        for (sys_intr = SYS_INTR_GB_CHIP_FATAL; sys_intr < SYS_INTR_GB_MAX; sys_intr++)
        {
            p_intr_cfg->intr[sys_intr].group = INVG;
        }

        /* mapping user define interrupt */
        for (intr_idx = 0; intr_idx < p_global_cfg_param->intr_count; intr_idx++)
        {
            p_intr_mapping = (ctc_intr_mapping_t*)(p_global_cfg_param->p_intr+intr_idx);
            ctc_intr_idx = p_intr_mapping->intr;
            ret = _sys_greatbelt_interrupt_type_mapping(lchip, ctc_intr_idx, sub_intr_idx, &sys_intr);
            /* interrupt is not used */
            if (ret < 0)
            {
                intr_count--;
                continue;
            }

            p_intr_cfg->intr[sys_intr].group = p_intr_mapping->group;
        }

        p_intr_cfg->group_count = group_count;
        p_intr_cfg->intr_count = intr_count;

        p_intr_cfg->intr_mode = p_global_cfg_param->intr_mode;

        if ((group_count > SYS_GB_MAX_INTR_GROUP) || (group_count > CTC_INTR_GB_MAX_GROUP))
        {
            ret = CTC_E_INVALID_PARAM;
            goto end;
        }

        if ((intr_count > CTC_INTR_MAX_INTR) || (intr_count > CTC_INTR_GB_MAX_INTR))
        {
            ret =  CTC_E_INVALID_PARAM;
            goto end;
        }
    }

    /* 2. allocate interrupt master */
    if (p_gb_intr_master[lchip])
    {
        ret = CTC_E_NONE;
        goto end;
    }

    p_gb_intr_master[lchip] = (sys_intr_master_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(sys_intr_master_t));
    if (NULL == p_gb_intr_master[lchip])
    {
        ret = CTC_E_NO_MEMORY;
        goto end;
    }

    sal_memset(p_gb_intr_master[lchip], 0, sizeof(sys_intr_master_t));

    /* 3. create mutex for chip module */
    ret = sal_mutex_create(&(p_gb_intr_master[lchip]->p_intr_mutex));
    if (ret || (!p_gb_intr_master[lchip]->p_intr_mutex))
    {
        mem_free(p_gb_intr_master[lchip]);
        ret = CTC_E_FAIL_CREATE_MUTEX;
        goto end;
    }

    /* 4. init interrupt & group field of master */
    for (intr = 0; intr < CTC_INTR_MAX_INTR; intr++)
    {
        p_gb_intr_master[lchip]->intr[intr].group = INVG;
        p_gb_intr_master[lchip]->intr[intr].intr = INVG;
    }

    for (group_id = 0; group_id < SYS_GB_MAX_INTR_GROUP; group_id++)
    {
        p_gb_intr_master[lchip]->group[group_id].group = INVG;
    }

    /* interrupt info */
    /* 5. init interrupt field of master , intr_cfg.intr is still from sys_greatbelt_intr_default, i:sys_interrupt_type_gb_t */
    for (i = 0; i < p_intr_cfg->intr_count; i++)
    {
        intr = p_intr_cfg->intr[i].intr;
        p_intr = &(p_gb_intr_master[lchip]->intr[intr]);
        p_intr->group = p_intr_cfg->intr[i].group;

        p_intr->intr = p_intr_cfg->intr[i].intr;
        p_intr->isr = p_intr_cfg->intr[i].isr;
        sal_strncpy(p_intr->desc, p_intr_cfg->intr[i].desc, CTC_INTR_DESC_LEN);

    }

    /* group info, i:ctc_intr_group_config */
    for (i = 0; i < p_intr_cfg->group_count; i++)
    {
        group_id = p_intr_cfg->group[i].group;
        if (group_id >= SYS_GB_MAX_INTR_GROUP || group_id < 0)
        {
            continue;
        }
        p_group = &(p_gb_intr_master[lchip]->group[group_id]);
        p_group->group = p_intr_cfg->group[i].group;
        p_group->irq = p_intr_cfg->group[i].irq;
        p_group->prio = p_intr_cfg->group[i].prio;
        sal_strncpy(p_group->desc, p_intr_cfg->group[i].desc, CTC_INTR_DESC_LEN);
        p_group->intr_count = 0;

        /* register interrupts to group */
        for (j = 0; j < CTC_INTR_MAX_INTR; j++)
        {
            if (p_gb_intr_master[lchip]->intr[j].group == p_group->group)
            {
                p_gb_intr_master[lchip]->intr[j].valid = TRUE;
                p_group->intr_count++;

                CTC_BIT_SET(p_group->intr_bmp, p_gb_intr_master[lchip]->intr[j].intr);
            }
        }

        /* calcuate reg bitmap by intr bitmap */
        _sys_greatbelt_interrupt_calc_reg_bmp(lchip, p_group);
        p_gb_intr_master[lchip]->group_count++;
    }

    /* 6. init other field of master */
    p_gb_intr_master[lchip]->is_default = (p_global_cfg_param) ? FALSE : TRUE;
    p_gb_intr_master[lchip]->intr_count = p_intr_cfg->intr_count;

    /* 7. init other field of master */
    CTC_ERROR_GOTO(_sys_greatbelt_interrupt_init_reg(lchip), ret, end);

    /* 8. init ops */
    CTC_ERROR_GOTO(_sys_greatbelt_interrupt_init_ops(lchip), ret, end);

    if (p_intr_cfg->intr_mode)
    {
        p_gb_intr_master[lchip]->intr_mode = 1;
        if (g_dal_op.interrupt_set_msi_cap)
        {

                /* Note, when use msi mode, one chip only support one IRQ */
                /* so p_gb_intr_master[lchip]->group_count == 1 */
                ret = g_dal_op.interrupt_set_msi_cap(lchip,TRUE, p_gb_intr_master[lchip]->group_count);
                if (ret)
                {
                    SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "enable msi failed!!\n");
                    goto end;
                }
        }

        /* Note, get the real irq for msi */
        if(g_dal_op.interrupt_get_msi_info)
        {
            ret = g_dal_op.interrupt_get_msi_info(lchip, &irq_base);
            if (ret)
            {
                SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "get msi info failed!!\n");
                goto end;
            }

                /* Note, updata sw */
                if (p_gb_intr_master[lchip]->group[0].group < 0)
                {
                    ret = CTC_E_UNEXPECT;
                    goto end;
                }
            p_gb_intr_master[lchip]->group[0].irq = p_gb_intr_master[lchip]->group[0].irq + irq_base;

            /* Note, when use msi mode, one chip only support one IRQ */
            p_group = &(p_gb_intr_master[lchip]->group[0]);

            ret = g_dal_op.interrupt_register(p_group->irq, p_group->prio, sys_greatbelt_interrupt_dispatch, (void*)irq_index);
            if (ret < 0)
            {
                SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "register interrupt failed!! irq:%d \n", p_group->irq);
                ret = CTC_E_UNEXPECT;
                goto end;
            }
        }
    }

    /* 9. call DAL function to register IRQ */
    if ((g_dal_op.interrupt_register)&&(!p_intr_cfg->intr_mode))
    {
        for (i = 0; i < SYS_GB_MAX_INTR_GROUP; i++)
        {
            p_group = &(p_gb_intr_master[lchip]->group[i]);
            if (p_group->group < 0)
            {
                continue;
            }
#ifndef _SAL_LINUX_KM
            if (2 == mode)  /* CHIP_AGT_MODE_SERVER */
            {
                ret = g_dal_op.interrupt_register(p_group->irq, p_group->prio, sys_greatbelt_interrupt_dispatch_chip_agent, (void*)i);
                if (ret < 0)
                {
                    SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "register interrupt failed!! irq:%d \n", p_group->irq);
                    ret = CTC_E_UNEXPECT;
                    goto end;
                }
            }
            else
#endif
            {
                ret = g_dal_op.interrupt_register(p_group->irq, p_group->prio, sys_greatbelt_interrupt_dispatch, (void*)i);
                if (ret < 0)
                {
                    SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "register interrupt failed!! irq:%d \n", p_group->irq);
                    ret = CTC_E_UNEXPECT;
                    goto end;
                }
            }
        }
    }

    if (NULL == p_gb_intr_master[lchip]->abnormal_intr_cb)
    {
        sys_greatbelt_interrupt_set_abnormal_intr_cb(lchip, sys_greatbelt_interrupt_chip_abnormal_isr);
    }
end:
    mem_free(p_intr_cfg);

    return CTC_E_NONE;
}

int32
sys_greatbelt_interrupt_deinit(uint8 lchip)
{
    uint32 i = 0;
    sys_intr_group_t* p_group = NULL;
    int32 ret = 0;

    LCHIP_CHECK(lchip);
    if (NULL == p_gb_intr_master[lchip])
    {
        return CTC_E_NONE;
    }

    /* free interrupt */
    if (g_dal_op.interrupt_unregister)
    {
        for (i = 0; i < SYS_GB_MAX_INTR_GROUP; i++)
        {
            p_group = &(p_gb_intr_master[lchip]->group[i]);
            if (p_group->group < 0)
            {
                continue;
            }
            ret = g_dal_op.interrupt_unregister(p_group->irq);
            if (ret < 0)
            {
                return ret;
            }
        }
    }

    if (p_gb_intr_master[lchip]->intr_mode)
    {
            if (g_dal_op.interrupt_set_msi_cap)
            {
                ret = g_dal_op.interrupt_set_msi_cap(lchip, FALSE, 0);
                if (ret)
                {
                    SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "disable msi failed!!\n");
                    return ret;
                }
            }
    }

    sal_mutex_destroy(p_gb_intr_master[lchip]->p_intr_mutex);
    mem_free(p_gb_intr_master[lchip]);

    return CTC_E_NONE;
}

