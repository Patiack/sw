/**
 @file ctc_goldengate_interrupt.c

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
#include "sys_goldengate_chip.h"
#include "sys_goldengate_learning_aging.h"
#include "sys_goldengate_ptp.h"
#include "sys_goldengate_oam.h"
#include "sys_goldengate_port.h"
#include "ctc_goldengate_interrupt.h"
#include "sys_goldengate_interrupt.h"
#include "sys_goldengate_interrupt_priv.h"

#include "goldengate/include/drv_lib.h"
#include "goldengate/include/drv_chip_ctrl.h"
/****************************************************************************
*
* Defines and Macros
*
*****************************************************************************/

/* register is SupIntrFatal */
#define SYS_INTR_GG_SUP_FATAL_BITS          CTC_INTR_T2B(SYS_INTR_GG_CHIP_FATAL)

/* register is SupIntrNormal */
#define SYS_INTR_GG_SUP_NORMAL_BITS         CTC_INTR_T2B(SYS_INTR_GG_CHIP_NORMAL)

/* register is RlmHsCtlInterruptFunc0 */
#define SYS_INTR_GG_RLM_HS_CTL_0_BITS       CTC_INTR_T2B(SYS_INTR_GG_PCS_LINK_31_0)

/* register is RlmCsCtlInterruptFunc0 */
#define SYS_INTR_GG_RLM_CS_CTL_0_BITS       CTC_INTR_T2B(SYS_INTR_GG_PCS_LINK_47_32)

/* register is RlmHsCtlInterruptFunc1 */
#define SYS_INTR_GG_RLM_HS_CTL_1_BITS       CTC_INTR_T2B(SYS_INTR_GG_PCS_LINK_79_48)

/* register is RlmCsCtlInterruptFunc1 */
#define SYS_INTR_GG_RLM_CS_CTL_1_BITS       CTC_INTR_T2B(SYS_INTR_GG_PCS_LINK_95_80)

/* register is RlmIpeCtlInterruptFunc0 */
#define SYS_INTR_GG_RLM_IPE_CTL_0_BITS      CTC_INTR_T2B(SYS_INTR_GG_FUNC_IPE_LEARN_CACHE_0)

/* register is RlmIpeCtlInterruptFunc1 */
#define SYS_INTR_GG_RLM_IPE_CTL_1_BITS      CTC_INTR_T2B(SYS_INTR_GG_FUNC_IPE_LEARN_CACHE_1)

/* register is RlmEnqCtlInterruptFunc */
#define SYS_INTR_GG_RLM_ENQ_CTL_BITS        CTC_INTR_T2B(SYS_INTR_GG_FUNC_CHAN_LINKDOWN_SCAN)

/* register is RlmNetCtlInterruptFunc0 */
#define SYS_INTR_GG_MDIO_CHANGE_0_BITS      CTC_INTR_T2B(SYS_INTR_GG_FUNC_MDIO_CHANGE_0)

/* register is RlmNetCtlInterruptFunc1 */
#define SYS_INTR_GG_MDIO_CHANGE_1_BITS      CTC_INTR_T2B(SYS_INTR_GG_FUNC_MDIO_CHANGE_1)

/* register is RlmShareTcamCtlInterruptFunc */
#define SYS_INTR_GG_RLM_SHARE_TCAM_CTL_BITS      CTC_INTR_T2B(SYS_INTR_GG_FUNC_IPFIX_USEAGE_OVERFLOW)

/* register is RlmShareDsCtlInterruptFunc */
#define SYS_INTR_GG_RLM_SHARE_DS_BITS \
    (CTC_INTR_T2B(SYS_INTR_GG_FUNC_AGING_FIFO) \
     | CTC_INTR_T2B(SYS_INTR_GG_FUNC_STATS_FIFO) \
     | CTC_INTR_T2B(SYS_INTR_GG_FUNC_PORT_LOG_STATS_FIFO) \
     | CTC_INTR_T2B(SYS_INTR_GG_FUNC_PTP_TOD_CODE_IN_RDY) \
     | CTC_INTR_T2B(SYS_INTR_GG_FUNC_PTP_SYNC_CODE_IN_RDY) \
     | CTC_INTR_T2B(SYS_INTR_GG_FUNC_PTP_SYNC_CODE_IN_ACC) \
     | CTC_INTR_T2B(SYS_INTR_GG_FUNC_PTP_MAC_TX_TS_CAPTURE) \
     | CTC_INTR_T2B(SYS_INTR_GG_FUNC_PTP_TX_TS_CAPTURE))

/* register is RlmBsCtlInterruptFunc */
#define SYS_INTR_GG_RLM_BS_CTL_BITS \
    (CTC_INTR_T2B(SYS_INTR_GG_FUNC_MET_LINK_SCAN_DONE) \
     | CTC_INTR_T2B(SYS_INTR_GG_FUNC_OAM_CLEAR_EN) \
     | CTC_INTR_T2B(SYS_INTR_GG_FUNC_OAM_DEFECT_CACHE) \
     | CTC_INTR_T2B(SYS_INTR_GG_FUNC_OAM_STATS) \
     | CTC_INTR_T2B(SYS_INTR_GG_FUNC_BSR_CONGESTION_1) \
     | CTC_INTR_T2B(SYS_INTR_GG_FUNC_BSR_CONGESTION_0) \
     | CTC_INTR_T2B(SYS_INTR_GG_FUNC_RESERVED1) \
     | CTC_INTR_T2B(SYS_INTR_GG_FUNC_RESERVED2))

/* register is PcieIntfInterruptFunc */
#define SYS_INTR_GG_PCIE_BITS       CTC_INTR_T2B(SYS_INTR_GG_PCIE_BURST_DONE)

/* register is DmaCtlIntrFunc0 */
#define SYS_INTR_GG_DMA_0_BITS      CTC_INTR_T2B(SYS_INTR_GG_DMA_0)

/* register is DmaCtlIntrFunc1 */
#define SYS_INTR_GG_DMA_1_BITS      CTC_INTR_T2B(SYS_INTR_GG_DMA_1)

#define SYS_INTR_GG_SUP_FUNC_BITS \
    (  SYS_INTR_GG_RLM_HS_CTL_0_BITS \
     | SYS_INTR_GG_RLM_CS_CTL_0_BITS \
     | SYS_INTR_GG_RLM_HS_CTL_1_BITS \
     | SYS_INTR_GG_RLM_CS_CTL_1_BITS \
     | SYS_INTR_GG_RLM_IPE_CTL_0_BITS \
     | SYS_INTR_GG_RLM_IPE_CTL_1_BITS \
     | SYS_INTR_GG_RLM_ENQ_CTL_BITS \
     | SYS_INTR_GG_MDIO_CHANGE_0_BITS \
     | SYS_INTR_GG_MDIO_CHANGE_1_BITS \
     | SYS_INTR_GG_RLM_SHARE_TCAM_CTL_BITS \
     | SYS_INTR_GG_RLM_SHARE_DS_BITS \
     | SYS_INTR_GG_RLM_BS_CTL_BITS )

#define SYS_INTR_GG_LOW_FATAL_MAX  9
#define SYS_INTR_GG_LOW_NORMAL_MAX  40

#define SYS_INTERRUPT_INIT_CHECK(lchip)     \
    do {                                    \
        SYS_LCHIP_CHECK_ACTIVE(lchip);           \
        if (NULL == p_gg_intr_master[lchip]) {  \
            return CTC_E_NOT_INIT; }        \
    } while(0)


/* interrupt register type in GG */
typedef enum sys_interrupt_gg_reg_e
{
    SYS_INTR_GG_REG_SUP_FATAL = 0,      /* SupIntrFatal */
    SYS_INTR_GG_REG_SUP_NORMAL,         /* SupIntrNormal */
    SYS_INTR_GG_REG_RLM_HS_CTL_0,       /* RlmHsCtlInterruptFunc0 */
    SYS_INTR_GG_REG_RLM_CS_CTL_0,       /* RlmCsCtlInterruptFunc0 */
    SYS_INTR_GG_REG_RLM_HS_CTL_1,       /* RlmHsCtlInterruptFunc1 */
    SYS_INTR_GG_REG_RLM_CS_CTL_1,       /* RlmCsCtlInterruptFunc1 */
    SYS_INTR_GG_REG_RLM_IPE_CTL_0,      /* RlmIpeCtlInterruptFunc0 */
    SYS_INTR_GG_REG_RLM_IPE_CTL_1,      /* RlmIpeCtlInterruptFunc1 */
    SYS_INTR_GG_REG_RLM_ENQ_CTL,        /* RlmEnqCtlInterruptFunc */
    SYS_INTR_GG_REG_RLM_NET_CTL_0,      /* RlmNetCtlInterruptFunc0 NO_DRV */
    SYS_INTR_GG_REG_RLM_NET_CTL_1,      /* RlmNetCtlInterruptFunc1 NO_DRV */
    SYS_INTR_GG_REG_RLM_TCAM_CTL,
    SYS_INTR_GG_REG_RLM_SHARE_CTL,      /* RlmShareDsCtlInterruptFunc */
    SYS_INTR_GG_REG_RLM_BS_CTL,         /* RlmBsCtlInterruptFunc */
    SYS_INTR_GG_REG_PCIE_INTF,          /* PcieIntfInterruptFunc */
    SYS_INTR_GG_REG_DMA_CTL_0,          /* DmaCtlIntrFunc0 */
    SYS_INTR_GG_REG_DMA_CTL_1,          /* DmaCtlIntrFunc1 */
    SYS_INTR_GG_REG_MAX
} sys_interrupt_gg_reg_t;

/* interrupt register type in GG */
typedef enum sys_interrupt_gg_reg_type_e
{
    SYS_INTR_GG_REG_TYPE_SUP_FATAL = 0,
    SYS_INTR_GG_REG_TYPE_SUP_NORMAL,
    SYS_INTR_GG_REG_TYPE_FUNC,
    SYS_INTR_GG_REG_TYPE_PCIE,
    SYS_INTR_GG_REG_TYPE_DMA,
    SYS_INTR_GG_REG_TYPE_MAX
} sys_interrupt_gg_reg_type_t;

/****************************************************************************
*
* Global and Declaration
*
*****************************************************************************/
sys_intr_master_t* p_gg_intr_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
extern dal_op_t g_dal_op;



sys_intr_reg_t g_intr_reg[SYS_INTR_GG_REG_MAX] =
{
    {SYS_INTR_GG_CHIP_FATAL,                1,  SupIntrFatal_t,                 "Chip Super Fatal"},
    {SYS_INTR_GG_CHIP_NORMAL,               1,  SupIntrNormal_t,                "Chip Super Normal"},
    {SYS_INTR_GG_PCS_LINK_31_0,             1,  RlmHsCtlInterruptFunc0_t,       "HS 0 (PCS link 31_0)"},
    {SYS_INTR_GG_PCS_LINK_47_32,            1,  RlmCsCtlInterruptFunc0_t,       "CS 0 (PCS link 47_32)"},
    {SYS_INTR_GG_PCS_LINK_79_48,            1,  RlmHsCtlInterruptFunc1_t,       "HS 1 (PCS link 79_48)"},
    {SYS_INTR_GG_PCS_LINK_95_80,            1,  RlmCsCtlInterruptFunc1_t,       "CS 1 (PCS link 95_80)"},
    {SYS_INTR_GG_FUNC_IPE_LEARN_CACHE_0,    1,  RlmIpeCtlInterruptFunc0_t,      "IPE 0 (Learning)"},
    {SYS_INTR_GG_FUNC_IPE_LEARN_CACHE_1,    1,  RlmIpeCtlInterruptFunc1_t,      "IPE 1 (Learning)"},
    {SYS_INTR_GG_FUNC_CHAN_LINKDOWN_SCAN,   1,  RlmEnqCtlInterruptFunc_t,       "Enqueue (Linkdown Scan)"},
    {SYS_INTR_GG_FUNC_MDIO_CHANGE_0,        1,  RlmNetCtlInterruptFunc0_t,      "Net 0 (MDIO Change)"},
    {SYS_INTR_GG_FUNC_MDIO_CHANGE_1,        1,  RlmNetCtlInterruptFunc1_t,      "Net 1 (MDIO Change)"},
    {SYS_INTR_GG_FUNC_IPFIX_USEAGE_OVERFLOW, 1, RlmShareTcamCtlInterruptFunc_t, "Ipfix Entry Usage Overflow"},
    {SYS_INTR_GG_FUNC_AGING_FIFO,           8,  RlmShareDsCtlInterruptFunc_t,   "Shared (PTP/Aging/Stats)"},
    {SYS_INTR_GG_FUNC_MET_LINK_SCAN_DONE,   8,  RlmBsCtlInterruptFunc_t,        "BSR (Congestion/OAM)"},
    {SYS_INTR_GG_PCIE_BURST_DONE,           1,  PcieIntfInterruptFunc_t,        "PCIEe Interface"},
    {SYS_INTR_GG_DMA_0,                     1,  DmaCtlIntrFunc0_t,              "DMA 0"},
    {SYS_INTR_GG_DMA_1,                     1,  DmaCtlIntrFunc1_t,              "DMA 1"}
};

sys_intr_info_t g_intr_info[SYS_INTR_GG_MAX] =
{
    /*  0 */ {TRUE,  0,   28,    SupIntrFatal_t},
    /*  1 */ {TRUE,  0,   25,    SupIntrNormal_t},
    /*  2 */ {TRUE,  0,   32,    RlmHsCtlInterruptFunc0_t},
    /*  3 */ {TRUE,  0,   18,    RlmCsCtlInterruptFunc0_t},
    /*  4 */ {TRUE,  0,   32,    RlmHsCtlInterruptFunc1_t},
    /*  5 */ {TRUE,  0,   18,    RlmCsCtlInterruptFunc1_t},
    /*  6 */ {TRUE,  0,   1,     RlmIpeCtlInterruptFunc0_t},
    /*  7 */ {TRUE,  0,   1,     RlmIpeCtlInterruptFunc1_t},
    /*  8 */ {TRUE,  0,   1,     RlmEnqCtlInterruptFunc_t},
    /*  9 */ {TRUE,  0,   2,     RlmNetCtlInterruptFunc0_t},
    /* 10 */ {TRUE,  0,   2,     RlmNetCtlInterruptFunc1_t},
    /* 11 */ {TRUE,  0,   2,     RlmShareTcamCtlInterruptFunc_t},     /* SYS_INTR_GG_FUNC_IPFIX_USEAGE_OVERFLOW */
    /* 12 */ {TRUE,  0,   1,     RlmShareDsCtlInterruptFunc_t},         /* SYS_INTR_GG_FUNC_AGING_FIFO */
    /* 13 */ {TRUE,  1,   1,     RlmShareDsCtlInterruptFunc_t},         /* SYS_INTR_GG_FUNC_STATS_FIFO */
    /* 14 */ {TRUE,  2,   4,     RlmShareDsCtlInterruptFunc_t},         /* SYS_INTR_GG_FUNC_PORT_LOG_STATS_FIFO */
    /* 15 */ {TRUE,  8,   1,     RlmShareDsCtlInterruptFunc_t},         /* SYS_INTR_GG_FUNC_PTP_TOD_CODE_IN_RDY */
    /* 16 */ {TRUE, 10,   1,     RlmShareDsCtlInterruptFunc_t},        /* SYS_INTR_GG_FUNC_PTP_SYNC_CODE_IN_RDY */
    /* 17 */ {TRUE, 11,   1,     RlmShareDsCtlInterruptFunc_t},        /* SYS_INTR_GG_FUNC_PTP_SYNC_CODE_IN_ACC */
    /* 18 */ {TRUE, 12,   1,     RlmShareDsCtlInterruptFunc_t},   /* SYS_INTR_GG_FUNC_PTP_MAC_TX_TS_CAPTURE */
    /* 19 */ {TRUE,  6,    1,     RlmShareDsCtlInterruptFunc_t},          /* SYS_INTR_GG_FUNC_PTP_TX_TS_CAPTURE */
    /* 20 */ {TRUE,  19,  1,     RlmBsCtlInterruptFunc_t},   /* SYS_INTR_GG_FUNC_MET_LINK_SCAN_DONE */
    /* 21 */ {TRUE,  0,   4,     RlmBsCtlInterruptFunc_t},   /* SYS_INTR_GG_FUNC_OAM_CLEAR_EN */
    /* 22 */ {TRUE,  8,   1,     RlmBsCtlInterruptFunc_t},   /* SYS_INTR_GG_FUNC_OAM_DEFECT_CACHE */
    /* 23 */ {TRUE,  4,   4,     RlmBsCtlInterruptFunc_t},   /* SYS_INTR_GG_FUNC_OAM_STATS */
    /* 24 */ {TRUE,  9,   5,     RlmBsCtlInterruptFunc_t},   /* SYS_INTR_GG_FUNC_BSR_CONGESTION_1 */
    /* 25 */ {TRUE,  14,  5,     RlmBsCtlInterruptFunc_t},   /* SYS_INTR_GG_FUNC_BSR_CONGESTION_0 */
    /* 26 */ {FALSE, 0,  0,      0},    /* reserved function */
    /* 27 */ {FALSE, 0,   0,     0},    /* reserved function */
    /* 28 */ {TRUE,  0,   2,     PcieIntfInterruptFunc_t},
    /* 29 */ {TRUE,  0,  16,     DmaCtlIntrFunc0_t},
    /* 30 */ {TRUE,  0,  16,     DmaCtlIntrFunc1_t},
};

/* sub registers for SYS_INTR_GG_REG_SUP_FATAL */
static sys_intr_sub_reg_t g_intr_sub_reg_sup_fatal[SYS_INTR_GG_SUB_FATAL_MAX] =
{
    { RlmAdCtlInterruptFatal0_t },        /*0*/
    { RlmAdCtlInterruptFatal1_t },        /*1*/
    { RlmHashCtlInterruptFatal0_t },     /*2*/
    { RlmHashCtlInterruptFatal1_t },     /*3*/
    { RlmEpeCtlInterruptFatal0_t },       /*4*/
    { RlmEpeCtlInterruptFatal1_t },       /*5*/
    { RlmIpeCtlInterruptFatal0_t },        /*6*/
    { RlmIpeCtlInterruptFatal1_t },        /*7*/
    { RlmNetCtlInterruptFatal0_t },       /*8*/
    { RlmNetCtlInterruptFatal1_t },       /*9*/
    { RlmShareTcamCtlInterruptFatal_t },  /*10*/
    { RlmShareDsCtlInterruptFatal_t },      /*11*/
    { RlmBrPbCtlInterruptFatal_t },           /*12*/
    { RlmBsCtlInterruptFatal_t },              /*13*/
    { RlmEnqCtlInterruptFatal_t },            /*14*/
    { RlmDeqCtlInterruptFatal_t },            /*15*/
    { -1 },    /*TODO reqPcie0FifoOverrun_t 16 */
    { -1 },    /*TODO reqPcie1FifoOverrun_t 17 */
    { -1 },    /*TODO reqDma0FifoOverrun_t 18 */
    { -1 },    /*TODO reqDma1FifoOverrun_t 19 */
    { -1 },    /*TODO reqTrackFifoOverrun_t 20 */
    { -1 },    /*TODO reqTrackFifoUnderrun_t  21 */
    { -1 },    /*TODO resCrossFifoOverrun_t  22 */
    { -1 },    /*TODO reqCrossFifoOverrun_t  23 */
    { -1 },    /*TODO reqI2CFifoOverrun_t  24 */
    { PcieIntfInterruptFatal_t },        /* 25 */
    { DmaCtlInterruptFatal0_t },       /* 26 */
    { DmaCtlInterruptFatal1_t },       /* 27 */
};

/* low level registers for SYS_INTR_GG_REG_SUP_FATAL, pcie and dma have no low level */
static uint32 g_intr_low_reg_sub_fatal[SYS_INTR_GG_SUB_FATAL_PCIE_INTF][SYS_INTR_GG_LOW_FATAL_MAX] =
{
    /*0*/{DynamicDsAdInterruptFatal0_t, EgrOamHashInterruptFatal0_t, -1, -1, -1, -1, -1, -1, -1},
    /*1*/{DynamicDsAdInterruptFatal1_t, EgrOamHashInterruptFatal1_t, -1, -1, -1, -1, -1, -1, -1},
    /*2*/{DynamicDsHashInterruptFatal0_t, FibEngineInterruptFatal0_t, UserIdHashInterruptFatal0_t, -1, -1, -1, -1, -1, -1},
    /*3*/{DynamicDsHashInterruptFatal1_t, FibEngineInterruptFatal1_t, UserIdHashInterruptFatal1_t, -1, -1, -1, -1, -1, -1},
    /*4*/{EpeAclOamInterruptFatal0_t, EpeHdrAdjInterruptFatal0_t, EpeHdrEditInterruptFatal0_t, EpeHdrProcInterruptFatal0_t, EpeNextHopInterruptFatal0_t, -1, -1, -1, -1},
    /*5*/{EpeAclOamInterruptFatal1_t, EpeHdrAdjInterruptFatal1_t, EpeHdrEditInterruptFatal1_t, EpeHdrProcInterruptFatal1_t, EpeNextHopInterruptFatal1_t, -1, -1, -1, -1},
    /*6*/{IpeForwardInterruptFatal0_t, IpeHdrAdjInterruptFatal0_t, IpeIntfMapInterruptFatal0_t, IpeLkupMgrInterruptFatal0_t, IpePktProcInterruptFatal0_t, FlowHashInterruptFatal0_t, -1, -1, -1},
    /*7*/{IpeForwardInterruptFatal1_t, IpeHdrAdjInterruptFatal1_t, IpeIntfMapInterruptFatal1_t, IpeLkupMgrInterruptFatal1_t, IpePktProcInterruptFatal1_t, FlowHashInterruptFatal1_t, -1, -1, -1},
    /*8*/{NetRxInterruptFatal0_t, NetTxInterruptFatal0_t, -1, -1, -1, -1, -1, -1, -1},
    /*9*/{NetRxInterruptFatal1_t, NetTxInterruptFatal1_t, -1, -1, -1, -1, -1, -1, -1},
    /*10*/{FlowAccInterruptFatal_t, FlowTcamInterruptFatal_t, LpmTcamIpInterruptFatal_t,  -1, -1, -1, -1, -1, -1},
    /*11*/{DsAgingInterruptFatal_t, DynamicDsShareInterruptFatal_t, FibAccInterruptFatal_t, GlobalStatsInterruptFatal_t, PolicingInterruptFatal_t, ShareDlbInterruptFatal_t, ShareEfdInterruptFatal_t, ShareStormCtlInterruptFatal_t, TsEngineInterruptFatal_t},
    /*12*/{BufRetrvInterruptFatal0_t, BufRetrvInterruptFatal1_t, PbCtlInterruptFatal_t, -1, -1, -1, -1, -1, -1},
    /*13*/{BufStoreInterruptFatal_t, MetFifoInterruptFatal_t, OamAutoGenPktInterruptFatal_t, OamFwdInterruptFatal_t, OamParserInterruptFatal_t, OamProcInterruptFatal_t, -1, -1, -1},
    /*14*/{QMgrEnqInterruptFatal_t,  -1, -1, -1, -1, -1, -1, -1, -1},
    /*15*/{QMgrDeqSliceInterruptFatal0_t, QMgrDeqSliceInterruptFatal1_t, OobFcInterruptFatal0_t, OobFcInterruptFatal1_t, QMgrMsgStoreInterruptFatal_t, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1},

};

/* sub registers for SYS_INTR_GG_REG_SUP_NORMAL */
static sys_intr_sub_reg_t g_intr_sub_reg_sup_normal[SYS_INTR_GG_SUB_NORMAL_MAX] =
{
    {RlmAdCtlInterruptNormal0_t},     /*0*/
    {RlmAdCtlInterruptNormal1_t},     /*1*/
    {RlmHashCtlInterruptNormal0_t},   /*2*/
    {RlmHashCtlInterruptNormal1_t},   /*3*/
    {RlmEpeCtlInterruptNormal0_t},    /*4*/
    {RlmEpeCtlInterruptNormal1_t},    /*5*/
    {RlmIpeCtlInterruptNormal0_t},    /*6*/
    {RlmIpeCtlInterruptNormal1_t},    /*7*/
    {RlmCsCtlInterruptNormal0_t},     /*8*/
    {RlmCsCtlInterruptNormal1_t},     /*9*/
    {RlmHsCtlInterruptNormal0_t},     /*10*/
    {RlmHsCtlInterruptNormal1_t},     /*11*/
    {RlmNetCtlInterruptNormal0_t},   /*12*/
    {RlmNetCtlInterruptNormal1_t},   /*13*/
    {RlmShareTcamCtlInterruptNormal_t},   /*14*/
    {RlmShareDsCtlInterruptNormal_t},   /*15*/
    {RlmBrPbCtlInterruptNormal_t},    /*16*/
    {RlmBsCtlInterruptNormal_t},       /*17*/
    {RlmEnqCtlInterruptNormal_t},     /*18*/
    {RlmDeqCtlInterruptNormal_t},     /*19*/
    {I2CMasterInterruptNormal0_t},   /*20*/
    {I2CMasterInterruptNormal1_t},   /*21*/
    {DmaCtlInterruptNormal0_t},       /*22*/
    {-1},       /*23*/
    {PcieIntfInterruptNormal_t},        /*24*/
};

/* low level registers for SYS_INTR_GG_REG_SUP_NORMAL*/
static uint32 g_intr_low_reg_sub_normal[SYS_INTR_GG_SUB_NORMAL_MAX][SYS_INTR_GG_LOW_NORMAL_MAX] =
{
    /*0*/{DynamicDsAdInterruptNormal0_t, EgrOamHashInterruptNormal0_t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
             -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*1*/{DynamicDsAdInterruptNormal1_t, EgrOamHashInterruptNormal1_t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
             -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*2*/{DynamicDsHashInterruptNormal0_t, FibEngineInterruptNormal0_t, LpmTcamNatInterruptNormal0_t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
             -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*3*/{DynamicDsHashInterruptNormal1_t, FibEngineInterruptNormal1_t, LpmTcamNatInterruptNormal1_t, -1, -1, -1, -1, -1,-1, -1, -1, -1, -1, -1, -1, -1,
             -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*4*/{EpeAclOamInterruptNormal0_t, EpeHdrAdjInterruptNormal0_t, EpeHdrEditInterruptNormal0_t, EpeHdrProcInterruptNormal0_t, EpeNextHopInterruptNormal0_t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
             -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*5*/{EpeAclOamInterruptNormal1_t, EpeHdrAdjInterruptNormal1_t, EpeHdrEditInterruptNormal1_t, EpeHdrProcInterruptNormal1_t, EpeNextHopInterruptNormal1_t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
             -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*6*/{IpeForwardInterruptNormal0_t, IpeHdrAdjInterruptNormal0_t, IpeIntfMapInterruptNormal0_t, IpeLkupMgrInterruptNormal0_t, IpePktProcInterruptNormal0_t, PktProcDsInterruptNormal0_t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*7*/{IpeForwardInterruptNormal1_t, IpeHdrAdjInterruptNormal1_t, IpeIntfMapInterruptNormal1_t, IpeLkupMgrInterruptNormal1_t, IpePktProcInterruptNormal1_t, PktProcDsInterruptNormal1_t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*8*/{HssAnethWrapperInterruptNormal32_t, HssAnethWrapperInterruptNormal33_t, HssAnethWrapperInterruptNormal34_t, HssAnethWrapperInterruptNormal35_t, HssAnethWrapperInterruptNormal36_t,
              HssAnethWrapperInterruptNormal37_t, HssAnethWrapperInterruptNormal38_t, HssAnethWrapperInterruptNormal39_t, HssAnethWrapperInterruptNormal40_t, HssAnethWrapperInterruptNormal41_t,
              HssAnethWrapperInterruptNormal42_t, HssAnethWrapperInterruptNormal43_t, HssAnethWrapperInterruptNormal44_t, HssAnethWrapperInterruptNormal45_t, HssAnethWrapperInterruptNormal46_t,
              HssAnethWrapperInterruptNormal47_t, SharedPcsInterruptNormal8_t, SharedPcsInterruptNormal9_t, SharedPcsInterruptNormal10_t, SharedPcsInterruptNormal11_t, CgPcsInterruptNormal0_t,
              CgPcsInterruptNormal1_t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*9*/{HssAnethWrapperInterruptNormal80_t, HssAnethWrapperInterruptNormal81_t, HssAnethWrapperInterruptNormal82_t, HssAnethWrapperInterruptNormal83_t, HssAnethWrapperInterruptNormal84_t,
              HssAnethWrapperInterruptNormal85_t, HssAnethWrapperInterruptNormal86_t, HssAnethWrapperInterruptNormal87_t, HssAnethWrapperInterruptNormal88_t, HssAnethWrapperInterruptNormal89_t,
              HssAnethWrapperInterruptNormal90_t, HssAnethWrapperInterruptNormal91_t, HssAnethWrapperInterruptNormal92_t, HssAnethWrapperInterruptNormal93_t, HssAnethWrapperInterruptNormal94_t,
              HssAnethWrapperInterruptNormal95_t, SharedPcsInterruptNormal20_t, SharedPcsInterruptNormal21_t, SharedPcsInterruptNormal22_t, SharedPcsInterruptNormal23_t, CgPcsInterruptNormal2_t,
              CgPcsInterruptNormal3_t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*10*/{HssAnethWrapperInterruptNormal0_t, HssAnethWrapperInterruptNormal1_t, HssAnethWrapperInterruptNormal2_t,  HssAnethWrapperInterruptNormal3_t, HssAnethWrapperInterruptNormal4_t,
               HssAnethWrapperInterruptNormal5_t, HssAnethWrapperInterruptNormal6_t, HssAnethWrapperInterruptNormal7_t, HssAnethWrapperInterruptNormal8_t, HssAnethWrapperInterruptNormal9_t,
               HssAnethWrapperInterruptNormal10_t, HssAnethWrapperInterruptNormal11_t, HssAnethWrapperInterruptNormal12_t, HssAnethWrapperInterruptNormal13_t, HssAnethWrapperInterruptNormal14_t,
               HssAnethWrapperInterruptNormal15_t, HssAnethWrapperInterruptNormal16_t, HssAnethWrapperInterruptNormal17_t, HssAnethWrapperInterruptNormal18_t, HssAnethWrapperInterruptNormal19_t,
               HssAnethWrapperInterruptNormal20_t, HssAnethWrapperInterruptNormal21_t, HssAnethWrapperInterruptNormal22_t, HssAnethWrapperInterruptNormal23_t, HssAnethWrapperInterruptNormal24_t,
               HssAnethWrapperInterruptNormal25_t, HssAnethWrapperInterruptNormal26_t, HssAnethWrapperInterruptNormal27_t, HssAnethWrapperInterruptNormal28_t, HssAnethWrapperInterruptNormal29_t,
               HssAnethWrapperInterruptNormal30_t, HssAnethWrapperInterruptNormal31_t, SharedPcsInterruptNormal0_t, SharedPcsInterruptNormal1_t, SharedPcsInterruptNormal2_t, SharedPcsInterruptNormal3_t,
               SharedPcsInterruptNormal4_t, SharedPcsInterruptNormal5_t, SharedPcsInterruptNormal6_t, SharedPcsInterruptNormal7_t},  /*no low level*/
    /*11*/{HssAnethWrapperInterruptNormal48_t, HssAnethWrapperInterruptNormal49_t, HssAnethWrapperInterruptNormal50_t,  HssAnethWrapperInterruptNormal51_t, HssAnethWrapperInterruptNormal52_t,
               HssAnethWrapperInterruptNormal53_t, HssAnethWrapperInterruptNormal54_t, HssAnethWrapperInterruptNormal55_t, HssAnethWrapperInterruptNormal56_t, HssAnethWrapperInterruptNormal57_t,
               HssAnethWrapperInterruptNormal58_t, HssAnethWrapperInterruptNormal59_t, HssAnethWrapperInterruptNormal60_t, HssAnethWrapperInterruptNormal61_t, HssAnethWrapperInterruptNormal62_t,
               HssAnethWrapperInterruptNormal63_t, HssAnethWrapperInterruptNormal64_t, HssAnethWrapperInterruptNormal65_t, HssAnethWrapperInterruptNormal66_t, HssAnethWrapperInterruptNormal67_t,
               HssAnethWrapperInterruptNormal68_t, HssAnethWrapperInterruptNormal69_t, HssAnethWrapperInterruptNormal70_t, HssAnethWrapperInterruptNormal71_t, HssAnethWrapperInterruptNormal72_t,
               HssAnethWrapperInterruptNormal73_t, HssAnethWrapperInterruptNormal74_t, HssAnethWrapperInterruptNormal75_t, HssAnethWrapperInterruptNormal76_t, HssAnethWrapperInterruptNormal77_t,
               HssAnethWrapperInterruptNormal78_t, HssAnethWrapperInterruptNormal79_t, SharedPcsInterruptNormal12_t, SharedPcsInterruptNormal13_t, SharedPcsInterruptNormal14_t, SharedPcsInterruptNormal15_t,
               SharedPcsInterruptNormal16_t, SharedPcsInterruptNormal17_t, SharedPcsInterruptNormal18_t, SharedPcsInterruptNormal19_t},  /*no low level*/
    /*12*/{XqmacInterruptNormal0_t, XqmacInterruptNormal1_t, XqmacInterruptNormal2_t, XqmacInterruptNormal3_t, XqmacInterruptNormal4_t, XqmacInterruptNormal5_t, XqmacInterruptNormal6_t,
               XqmacInterruptNormal7_t,  XqmacInterruptNormal8_t, XqmacInterruptNormal9_t, XqmacInterruptNormal10_t, XqmacInterruptNormal11_t, CgmacInterruptNormal0_t, CgmacInterruptNormal1_t,
               NetRxInterruptNormal0_t, NetTxInterruptNormal0_t, MacLedInterruptNormal0_t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*13*/{XqmacInterruptNormal12_t, XqmacInterruptNormal13_t, XqmacInterruptNormal14_t, XqmacInterruptNormal15_t, XqmacInterruptNormal16_t, XqmacInterruptNormal17_t,
               XqmacInterruptNormal18_t, XqmacInterruptNormal19_t,  XqmacInterruptNormal20_t, XqmacInterruptNormal21_t, XqmacInterruptNormal22_t, XqmacInterruptNormal23_t,
               CgmacInterruptNormal2_t, CgmacInterruptNormal3_t, NetRxInterruptNormal1_t, NetTxInterruptNormal1_t, MacLedInterruptNormal1_t, -1, -1, -1, -1, -1, -1, -1, -1, -1,
               -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*14*/{FlowAccInterruptNormal_t, FlowTcamInterruptNormal_t, LpmTcamIpInterruptNormal_t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
               -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*15*/{DsAgingInterruptNormal_t, DynamicDsShareInterruptNormal_t, FibAccInterruptNormal_t, GlobalStatsInterruptNormal_t, PolicingInterruptNormal_t, ShareDlbInterruptNormal_t, ShareEfdInterruptNormal_t, TsEngineInterruptNormal_t, -1, -1, -1, -1, -1, -1, -1, -1,
              -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*16*/{BufRetrvInterruptNormal0_t, BufRetrvInterruptNormal1_t, PbCtlInterruptNormal_t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
              -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*17*/{BufStoreInterruptNormal_t, MetFifoInterruptNormal_t, OamFwdInterruptNormal_t, OamProcInterruptNormal_t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
              -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*18*/{QMgrEnqInterruptNormal_t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
              -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*19*/{QMgrDeqSliceInterruptNormal0_t, QMgrDeqSliceInterruptNormal1_t, OobFcInterruptNormal0_t, OobFcInterruptNormal1_t, QMgrMsgStoreInterruptNormal_t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
              -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    /*20*/{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},  /*no low level*/
    /*21*/{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},  /*no low level*/
    /*22*/{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},  /*no low level*/
    /*23*/{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, /*no low level*/
    /*24*/{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, /*no low level*/

};
/****************************************************************************
*
* Function
*
*****************************************************************************/
extern uint8 drv_goldengate_get_host_type(void);
/****************************************************************************
*
* Function
*
*****************************************************************************/

/**
 @brief mapping from ctc parameter to sys level
*/
STATIC int32
_sys_goldengate_interrupt_type_mapping(uint8 lchip, ctc_intr_type_t* p_type, sys_intr_type_t* p_sys_intr)
{
    int32 ret = CTC_E_NONE;

    switch(p_type->intr)
    {
        case CTC_INTR_CHIP_FATAL:
            p_sys_intr->intr = SYS_INTR_GG_CHIP_FATAL;
            p_sys_intr->sub_intr = p_type->sub_intr;
            p_sys_intr->low_intr = p_type->low_intr;
            break;
        case CTC_INTR_CHIP_NORMAL:
            p_sys_intr->intr = SYS_INTR_GG_CHIP_NORMAL;
            p_sys_intr->sub_intr = p_type->sub_intr;
            p_sys_intr->low_intr = p_type->low_intr;
            break;

        /* ptp */
        case CTC_INTR_FUNC_PTP_TS_CAPTURE:
            p_sys_intr->intr = SYS_INTR_GG_FUNC_PTP_TX_TS_CAPTURE;
            p_sys_intr->slice_id = 0;
            break;
        case CTC_INTR_FUNC_PTP_MAC_TX_TS_CAPTURE:
            p_sys_intr->intr = SYS_INTR_GG_FUNC_PTP_MAC_TX_TS_CAPTURE;
            p_sys_intr->slice_id = 0;
            break;
        case CTC_INTR_FUNC_PTP_TOD_PULSE_IN:
            p_sys_intr->intr = SYS_INTR_GG_FUNC_PTP_TOD_CODE_IN_RDY;
            p_sys_intr->slice_id = 0;
            break;
        case CTC_INTR_FUNC_PTP_TOD_CODE_IN_RDY:
            p_sys_intr->intr = SYS_INTR_GG_FUNC_PTP_TOD_CODE_IN_RDY;
            p_sys_intr->slice_id = 0;
            break;
        case CTC_INTR_FUNC_PTP_SYNC_PULSE_IN:
            p_sys_intr->intr = SYS_INTR_GG_FUNC_PTP_SYNC_CODE_IN_ACC;
            p_sys_intr->slice_id = 0;
            break;
        case CTC_INTR_FUNC_PTP_SYNC_CODE_IN_RDY:
            p_sys_intr->intr = SYS_INTR_GG_FUNC_PTP_SYNC_CODE_IN_RDY;
            p_sys_intr->slice_id = 0;
            break;

        case CTC_INTR_FUNC_OAM_DEFECT_CACHE:
            p_sys_intr->intr = SYS_INTR_GG_FUNC_OAM_DEFECT_CACHE;
            break;

        /* Notice: for mdio interrupt BOTH using  SYS_INTR_GG_FUNC_MDIO_CHANGE_0 present,
           1x or Xg using sub_idx present */
        case CTC_INTR_FUNC_MDIO_CHANGE:  /*for xg*/
            p_sys_intr->intr = SYS_INTR_GG_FUNC_MDIO_CHANGE_0;
            p_sys_intr->sub_intr = 1;
            p_sys_intr->slice_id = 2;
            break;
#if 0
        case CTC_INTR_FUNC_MDIO_1G_CHANGE: /*for 1g*/
            p_sys_intr->intr = SYS_INTR_GG_FUNC_MDIO_CHANGE_0;
            p_sys_intr->sub_intr = 0;
            p_sys_intr->slice_id = 2;
            break;
#endif

        /* linkagg failover */
        case CTC_INTR_FUNC_LINKAGG_FAILOVER:
            p_sys_intr->intr = SYS_INTR_GG_FUNC_CHAN_LINKDOWN_SCAN;
            p_sys_intr->slice_id = 0;
            break;

        /* aps failover */
        case CTC_INTR_FUNC_APS_FAILOVER:
            p_sys_intr->intr = SYS_INTR_GG_FUNC_MET_LINK_SCAN_DONE;
            p_sys_intr->slice_id = 0;
            break;

        /* learning aging */
        case CTC_INTR_FUNC_IPE_LEARN_CACHE:
            p_sys_intr->intr = SYS_INTR_GG_FUNC_IPE_LEARN_CACHE_0;
            p_sys_intr->slice_id = 2;
            break;
        case CTC_INTR_FUNC_IPE_AGING_FIFO:
            p_sys_intr->intr = SYS_INTR_GG_FUNC_AGING_FIFO;
            p_sys_intr->slice_id = 0;
            break;

        /* flow stats */
        case CTC_INTR_FUNC_STATS_FIFO:
            p_sys_intr->intr = SYS_INTR_GG_FUNC_STATS_FIFO;
            p_sys_intr->slice_id = 0;
            break;

        /* DMA */
        case CTC_INTR_DMA_FUNC:
            p_sys_intr->intr = SYS_INTR_GG_DMA_0;
            p_sys_intr->sub_intr = p_type->sub_intr;
            p_sys_intr->slice_id = 2;
            break;

        case CTC_INTR_FUNC_LINK_CHAGNE:
            p_sys_intr->intr = SYS_INTR_GG_PCS_LINK_31_0;
            break;

        default:
            ret = CTC_E_INTR_INVALID_TYPE;
            break;
    }

    return ret;
}

#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /* simulation */
int32
sys_goldengate_interrupt_model_sim_value(uint8 lchip, uint32 tbl_id, uint32 intr, uint32* bmp, uint32 enable)
{
    uint32 cmd = 0;
    uint32 value_set[CTC_INTR_STAT_SIZE];
    uint32 value_reset[CTC_INTR_STAT_SIZE];
    uint32 intr_vec[CTC_INTR_STAT_SIZE];

    if (drv_goldengate_io_is_asic())
    {
        return 0;
    }

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
        CTC_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_ioctl(lchip, INTR_INDEX_VAL_SET, cmd, value_set), INTR_MUTEX);
        CTC_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_ioctl(lchip, INTR_INDEX_VAL_RESET, cmd, value_reset), INTR_MUTEX);

        /* 2. do reset action */
        value_reset[0] &= bmp[0];
        value_reset[1] &= bmp[1];
        value_reset[2] &= bmp[2];

        value_set[0] &= ~value_reset[0];
        value_set[1] &= ~value_reset[1];
        value_set[2] &= ~value_reset[2];

        /* 3. write value */
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_ioctl(lchip, INTR_INDEX_VAL_SET, cmd, value_set), INTR_MUTEX);
        CTC_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_ioctl(lchip, INTR_INDEX_VAL_RESET, cmd, value_reset), INTR_MUTEX);

        INTR_UNLOCK;
    }

    if (intr < SYS_INTR_GG_MAX)
    {
        CTC_BMP_INIT(intr_vec);
        cmd = DRV_IOR(SupCtlIntrVec_t, DRV_ENTRY_FLAG);
        drv_goldengate_ioctl(lchip, 0, cmd, &intr_vec);
        CTC_BMP_UNSET(intr_vec, intr);
        cmd = DRV_IOW(SupCtlIntrVec_t, DRV_ENTRY_FLAG);
        drv_goldengate_ioctl(lchip, 0, cmd, &intr_vec);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_interrupt_model_sim_mask(uint8 lchip, uint32 tbl_id, uint32 bit_offset, uint32 bit_count, uint32 enable)
{
    uint32 cmd = 0;
    uint32 i = 0;
    uint32 mask[CTC_INTR_STAT_SIZE];
    uint32 mask_set[CTC_INTR_STAT_SIZE];
    uint32 mask_reset[CTC_INTR_STAT_SIZE];

    if (drv_goldengate_io_is_asic())
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
        CTC_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_ioctl(lchip, INTR_INDEX_MASK_SET, cmd, mask_set), INTR_MUTEX);
        CTC_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_ioctl(lchip, INTR_INDEX_MASK_RESET, cmd, mask_reset), INTR_MUTEX);

        /* 2. do reset action */
        if (CTC_INTR_ALL != bit_offset)
        {
            for (i = 0; i < bit_count; i++)
            {
                CTC_BMP_SET(mask, bit_offset+i);
            }
            mask_reset[0] &= mask[0];
            mask_reset[1] &= mask[1];
            mask_reset[2] &= mask[2];
        }

        mask_set[0] &= ~mask_reset[0];
        mask_set[1] &= ~mask_reset[1];
        mask_set[2] &= ~mask_reset[2];

        /* 3. write value */
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_ioctl(lchip, INTR_INDEX_MASK_SET, cmd, mask_set), INTR_MUTEX);
        CTC_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_ioctl(lchip, INTR_INDEX_MASK_RESET, cmd, mask_set), INTR_MUTEX);
    }
    else
    {
        /* 1. read value from set */
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_ioctl(lchip, INTR_INDEX_MASK_SET, cmd, mask_set), INTR_MUTEX);

        /* 2. sync value to reset */
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_ioctl(lchip, INTR_INDEX_MASK_RESET, cmd, mask_set), INTR_MUTEX);
    }

    INTR_UNLOCK;

    return CTC_E_NONE;
}

#endif /* (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1) */

int32
sys_goldengate_interrupt_get_tbl_id(uint8 lchip, ctc_intr_type_t* type)
{
    uint32 tbl_id_low = 0;

    if((type->sub_intr >= SYS_INTR_GG_SUB_NORMAL_MAX)||(type->low_intr >= SYS_INTR_GG_LOW_NORMAL_MAX))
    {
        return CTC_E_INVALID_PARAM;
    }

    tbl_id_low = g_intr_low_reg_sub_normal[type->sub_intr][type->low_intr];

    return tbl_id_low;
}
char*
_sys_goldengate_interrupt_index_str(uint8 lchip, uint32 index)
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
_sys_goldengate_interrupt_calc_reg_bmp(uint8 lchip, sys_intr_group_t* p_group)
{
    p_group->reg_bmp = 0;

    if (p_group->intr_bmp & SYS_INTR_GG_SUP_FATAL_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_SUP_FATAL);
    }

    if (p_group->intr_bmp & SYS_INTR_GG_SUP_NORMAL_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_SUP_NORMAL);
    }


    if (p_group->intr_bmp & SYS_INTR_GG_RLM_HS_CTL_0_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_RLM_HS_CTL_0);
    }

    if (p_group->intr_bmp & SYS_INTR_GG_RLM_CS_CTL_0_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_RLM_CS_CTL_0);
    }

    if (p_group->intr_bmp & SYS_INTR_GG_RLM_HS_CTL_1_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_RLM_HS_CTL_1);
    }

    if (p_group->intr_bmp & SYS_INTR_GG_RLM_CS_CTL_1_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_RLM_CS_CTL_1);
    }

    if (p_group->intr_bmp & SYS_INTR_GG_RLM_IPE_CTL_0_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_RLM_IPE_CTL_0);
    }

    if (p_group->intr_bmp & SYS_INTR_GG_RLM_IPE_CTL_1_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_RLM_IPE_CTL_1);
    }

    if (p_group->intr_bmp & SYS_INTR_GG_RLM_ENQ_CTL_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_RLM_ENQ_CTL);
    }

    if (p_group->intr_bmp & SYS_INTR_GG_MDIO_CHANGE_0_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_RLM_NET_CTL_0);
    }

    if (p_group->intr_bmp & SYS_INTR_GG_MDIO_CHANGE_1_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_RLM_NET_CTL_1);
    }

    if (p_group->intr_bmp & SYS_INTR_GG_RLM_SHARE_DS_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_RLM_SHARE_CTL);
    }

    if (p_group->intr_bmp & SYS_INTR_GG_RLM_BS_CTL_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_RLM_BS_CTL);
    }

    if (p_group->intr_bmp & SYS_INTR_GG_PCIE_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_PCIE_INTF);
    }

    if (p_group->intr_bmp & SYS_INTR_GG_DMA_0_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_DMA_CTL_0);
    }

    if (p_group->intr_bmp & SYS_INTR_GG_DMA_1_BITS)
    {
        CTC_BIT_SET(p_group->reg_bmp, SYS_INTR_GG_REG_DMA_CTL_1);
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_dump_intr(uint8 lchip, sys_intr_t* p_intr)
{
    SYS_INTR_DUMP("%-9d %-9d %-9d %-10p %-10s\n", p_intr->group, p_intr->intr, p_intr->occur_count, p_intr->isr, p_intr->desc);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_dump_group(uint8 lchip, sys_intr_group_t* p_group, uint8 is_detail)
{
    sys_intr_t* p_intr = NULL;
    uint32 i = 0;

    if (p_group->group < 0)
    {
        return CTC_E_NONE;
    }

    SYS_INTR_DUMP("Group %u: \n", p_group->group);
    SYS_INTR_DUMP("%-30s:%-6u \n", "--Group IRQ", p_group->irq);
    SYS_INTR_DUMP("%-30s:%-6u \n", "--ISR Priority", p_group->prio);
    SYS_INTR_DUMP("%-30s:%-6u \n", "--Occur Count", p_group->occur_count);
    SYS_INTR_DUMP("%-30s:%-6u \n", "--Interrupt Count", p_group->intr_count);

    if (is_detail)
    {
        SYS_INTR_DUMP("\n%-9s %-9s %-9s %-10s %-10s\n", "Group", "Interrupt", "Count", "ISR", "Desc");
        SYS_INTR_DUMP("----------------------------------------------------\n");

        for (i = 0; i < CTC_UINT32_BITS; i++)
        {
            if (CTC_IS_BIT_SET(p_group->intr_bmp, i))
            {
                p_intr = &(p_gg_intr_master[lchip]->intr[i]);
                if (p_intr->valid)
                {
                    _sys_goldengate_interrupt_dump_intr(lchip, p_intr);
                }
            }
        }
    }

    SYS_INTR_DUMP("\n");

    return CTC_E_NONE;
}

int32
_sys_goldengate_interrupt_dump_group_reg(uint8 lchip)
{
    ds_t map_ctl;
    ds_t gen_ctl;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 value = 0;

    if (!p_gg_intr_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    sal_memset(&map_ctl, 0, sizeof(map_ctl));
    sal_memset(&gen_ctl, 0, sizeof(gen_ctl));

    tbl_id = SupIntrMapCtl_t;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &map_ctl));
    SYS_INTR_DUMP("Group - Interrupt Mapping reg (%d, %s)\n", tbl_id, TABLE_NAME(tbl_id));
    SYS_INTR_DUMP("%-10s %-8s\n", "Index", "Value");
    SYS_INTR_DUMP("--------------------------------------------------------\n");
    SYS_INTR_DUMP("%-10s %08X\n", "Valid", 0x3FFFFFFF);

    GetSupIntrMapCtl(A, supIntrMap0_f, &map_ctl, &value);
    SYS_INTR_DUMP("%-10s %08X\n", "Group0", value);
    GetSupIntrMapCtl(A, supIntrMap1_f, &map_ctl, &value);
    SYS_INTR_DUMP("%-10s %08X\n", "Group1", value);
    GetSupIntrMapCtl(A, supIntrMap2_f, &map_ctl, &value);
    SYS_INTR_DUMP("%-10s %08X\n", "Group2", value);
    GetSupIntrMapCtl(A, supIntrMap3_f, &map_ctl, &value);
    SYS_INTR_DUMP("%-10s %08X\n", "Group3", value);
#if 0
    GetSupIntrMapCtl(A, supIntrMap4_f, &map_ctl, &value);
    SYS_INTR_DUMP("%-10s %08X\n", "Group4", value);
    GetSupIntrMapCtl(A, supIntrMap5_f, &map_ctl, &value);
    SYS_INTR_DUMP("%-10s %08X\n", "Group5", value);
    GetSupIntrMapCtl(A, supIntrMap6_f, &map_ctl, &value);
    SYS_INTR_DUMP("%-10s %08X\n", "Group6", value);
    GetSupIntrMapCtl(A, supIntrMap7_f, &map_ctl, &value);
    SYS_INTR_DUMP("%-10s %08X\n", "Group7", value);
#endif
    SYS_INTR_DUMP("\n");

    tbl_id = SupIntrGenCtl_t;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &gen_ctl));
    SYS_INTR_DUMP("Group reg (%d, %s)\n", tbl_id, TABLE_NAME(tbl_id));
    SYS_INTR_DUMP("%-10s %-8s\n", "Index", "Value");
    SYS_INTR_DUMP("--------------------------------------------------------\n");
    SYS_INTR_DUMP("%-10s %08X\n", "Valid", 0xFF);
    GetSupIntrGenCtl(A, supIntrValueSet_f, &gen_ctl, &value);
    SYS_INTR_DUMP("%-10s %08X\n", "ValueSet", value);
    GetSupIntrGenCtl(A, supIntrValueClr_f, &gen_ctl, &value);
    SYS_INTR_DUMP("%-10s %08X\n", "ValueReset", value);
    GetSupIntrGenCtl(A, supIntrMaskSet_f, &gen_ctl, &value);
    SYS_INTR_DUMP("%-10s %08X\n", "MaskSet", value);
    GetSupIntrGenCtl(A, supIntrMaskClr_f, &gen_ctl, &value);
    SYS_INTR_DUMP("%-10s %08X\n", "MaskReset", value);

    /* TODO : Pcie0IntrLogCtl_t / Pcie1IntrLogCtl_t / SupIntrCtl_t / SupCtlVec_t */

    SYS_INTR_DUMP("\n");
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_dump_one_reg(uint8 lchip, sys_interrupt_gg_reg_t reg)
{
    uint32 cmd = 0;
    uint32 index = 0;
    uint32 value[INTR_INDEX_MAX][CTC_INTR_STAT_SIZE];
    uint32 valid[CTC_INTR_STAT_SIZE];
    tables_info_t* p_tbl = NULL;
    fields_t* p_fld = NULL;
    uint32 fld_id = 0;
    uint32 i = 0;
    uint32 bits = 0;
    uint32 addr_offset = 0;
    sys_intr_reg_t *p_reg = &g_intr_reg[reg];
    uint32 tbl_id = p_reg->tbl_id;

    sal_memset(valid, 0, sizeof(valid));
    sal_memset(value, 0, sizeof(value));

    SYS_INTR_DUMP("reg (%d, %s) desc %s intr [%d, %d]\n", tbl_id, TABLE_NAME(tbl_id), p_reg->desc,
        p_reg->intr_start, (p_reg->intr_start+ p_reg->intr_count - 1));

    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, value[INTR_INDEX_VAL_SET]));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, value[INTR_INDEX_VAL_RESET]));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, value[INTR_INDEX_MASK_SET]));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, value[INTR_INDEX_MASK_RESET]));

    p_tbl = TABLE_INFO_PTR(tbl_id);

    for (fld_id = 0; fld_id < p_tbl->field_num; fld_id++)
    {
        p_fld = &(p_tbl->ptr_fields[fld_id]);
        if (p_fld->seg_num)
        {
            for (i = 0; i < p_fld->ptr_seg[0].bits; i++)
            {
                CTC_BIT_SET(valid[p_fld->ptr_seg[0].word_offset], (p_fld->ptr_seg[0].start + i));
                bits++;
            }
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
            SYS_INTR_DUMP("%-10s %08X   %08X\n", _sys_goldengate_interrupt_index_str(lchip, index),
                          (uint32)(TABLE_DATA_BASE(tbl_id, addr_offset) + (i * 16) + (index * 4)),
                          value[index][i]);
        }

        SYS_INTR_DUMP("\n");
    }

#if 0
    if (SYS_INTR_GG_SUP_FUNC_BITS & CTC_INTR_T2B(intr))
    {
        for (i = SYS_INTR_GB_SUP_FUNC_BIT_BEGIN; i <= SYS_INTR_GB_SUP_FUNC_BIT_END; i++)
        {
            if (CTC_BMP_ISSET(value[INTR_INDEX_VAL_SET], (i - SYS_INTR_GB_SUP_FUNC_BIT_BEGIN)))
            {
                p_intr = &(p_gg_intr_master[lchip]->intr[i]);
                if (p_intr)
                {
                    SYS_INTR_DUMP("Interrupt (%d, %s)\n", i, p_intr->desc);
                }
            }
        }
        SYS_INTR_DUMP("\n");
    }
#endif
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_clear_status_common(uint8 lchip, uint32 intr, uint32* bmp, uint32 tbl_id)
{
    uint32 cmd = 0;
    uint32 index = INTR_INDEX_VAL_RESET;

    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, bmp));

#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /* simulation */
    sys_goldengate_interrupt_model_sim_value(lchip, tbl_id, intr, bmp, FALSE);
#endif

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_clear_status_sup_fatal(uint8 lchip, sys_intr_type_t* p_type, uint32* p_bmp)
{
    uint32 tbl_id = 0;
    uint32 tbl_id_low = 0;
    uint32 cmd = 0;
    uint32 value[CTC_INTR_STAT_SIZE];    /* max is int_lk_interrupt_fatal_t */
    int32 ret = CTC_E_NONE;
    uint32 value_a = 0;

    CTC_BMP_INIT(value);

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_FATAL_MAX);
    INTR_SUB_TYPE_CHECK(p_type->low_intr, SYS_INTR_GG_LOW_FATAL_MAX);

    tbl_id = g_intr_sub_reg_sup_fatal[p_type->sub_intr].tbl_id;
    tbl_id_low =  g_intr_low_reg_sub_fatal[p_type->sub_intr][p_type->low_intr];

    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, value));

    if (tbl_id_low != -1)
    {
        /* clear low-level interrupt */
        _sys_goldengate_interrupt_clear_status_common(lchip, p_type->intr, p_bmp, tbl_id_low);
    }

    /* clear sub-level interrupt */
    _sys_goldengate_interrupt_clear_status_common(lchip, p_type->intr, value, tbl_id);

    /* clear sup-level interrupt */
    value_a = (1<<p_type->sub_intr);
    cmd = DRV_IOW(SupIntrFatal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, &value_a));

    return ret;

}

STATIC int32
_sys_goldengate_interrupt_clear_status_sup_normal(uint8 lchip, sys_intr_type_t* p_type, uint32* p_bmp)
{
    uint32 tbl_id = 0;
    uint32 tbl_id_low = 0;
    uint32 cmd = 0;
    uint32 value[CTC_INTR_STAT_SIZE];    /* max is int_lk_interrupt_fatal_t */
    int32 ret = CTC_E_NONE;
    uint32 value_a = 0;
    uint32 lp_status[2] = {0};
    uint32 err_rec = 0;

    CTC_BMP_INIT(value);

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_NORMAL_MAX);
    INTR_SUB_TYPE_CHECK(p_type->low_intr, SYS_INTR_GG_LOW_NORMAL_MAX);

    tbl_id = g_intr_sub_reg_sup_normal[p_type->sub_intr].tbl_id;
    tbl_id_low =  g_intr_low_reg_sub_normal[p_type->sub_intr][p_type->low_intr];

    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, value));

    if (tbl_id == PcieIntfInterruptNormal_t)
    {
        if (value[0] & 0x4000)
        {
            cmd = DRV_IOR(Pcie0IpStatus_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, lp_status);
            err_rec = GetPcie0IpStatus(V, pcie0TldlpErrorRec_f,lp_status);
            SYS_INTR_DUMP("Pcie0TldIpErrRec:0x%x\n", err_rec);
            err_rec = 0;
            SetPcie0IpStatus(V, pcie0TldlpErrorRec_f,lp_status, err_rec);
            cmd = DRV_IOW(Pcie0IpStatus_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, lp_status);
        }

        if (value[0] & 0x8000)
        {
            cmd = DRV_IOR(Pcie1IpStatus_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, lp_status);
            err_rec = GetPcie1IpStatus(V, pcie1TldlpErrorRec_f,lp_status);
            SYS_INTR_DUMP("Pcie1TldIpErrRec:0x%x\n", err_rec);
            err_rec = 0;
            SetPcie1IpStatus(V, pcie1TldlpErrorRec_f,lp_status, err_rec);
            cmd = DRV_IOW(Pcie1IpStatus_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, lp_status);
        }
    }

    if (tbl_id_low != -1)
    {
        /* clear low-level interrupt */
        if (p_bmp[0] || p_bmp[1] || p_bmp[2])
        {
            _sys_goldengate_interrupt_clear_status_common(lchip, p_type->intr, p_bmp, tbl_id_low);
        }
    }

    /* clear sub-level interrupt */
    _sys_goldengate_interrupt_clear_status_common(lchip, p_type->intr, value, tbl_id);

    /* clear sup-level interrupt */
    value_a = (1<<p_type->sub_intr);
    cmd = DRV_IOW(SupIntrNormal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, &value_a));

    return ret;
}

STATIC int32
_sys_goldengate_interrupt_clear_status_sup_func(uint8 lchip, sys_intr_type_t* p_type, uint32* p_bmp)
{
    sys_intr_info_t *p_info = NULL;

    INTR_SUP_FUNC_TYPE_CHECK(p_type->intr);

    p_info = &g_intr_info[p_type->intr];
    if (!p_info)
    {
        return CTC_E_INTR_NOT_SUPPORT;
    }

    return _sys_goldengate_interrupt_clear_status_common(lchip, p_type->intr, p_bmp, p_info->tbl_id);
}

STATIC int32
_sys_goldengate_interrupt_clear_status_pcie(uint8 lchip, sys_intr_type_t* p_type, uint32* p_bmp)
{
    uint32 tbl_id = PcieIntfInterruptFunc_t;

    if (CTC_INTR_ALL == p_type->sub_intr)
    {
        /* not support clear all sub interrupt */
        return CTC_E_INTR_INVALID_SUB_TYPE;
    }

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_PCIE_INTF_MAX);
    return _sys_goldengate_interrupt_clear_status_common(lchip, p_type->intr, p_bmp, tbl_id);
}

STATIC int32
_sys_goldengate_interrupt_clear_status_dma_func(uint8 lchip, sys_intr_type_t* p_type, uint32* p_bmp)
{
    uint32 tbl_id = (p_type->intr == SYS_INTR_GG_DMA_0) ? DmaCtlIntrFunc0_t : DmaCtlIntrFunc1_t;

    if (CTC_INTR_ALL == p_type->sub_intr)
    {
        /* not support clear all sub interrupt */
        return CTC_E_INTR_INVALID_SUB_TYPE;
    }

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_DMA_FUNC_MAX);
    return _sys_goldengate_interrupt_clear_status_common(lchip, p_type->intr, p_bmp, tbl_id);
}

STATIC int32
_sys_goldengate_interrupt_get_status_sup_level(uint8 lchip, sys_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    uint32 cmd = 0;
    uint32 index = INTR_INDEX_VAL_SET;
    uint32 value[CTC_INTR_STAT_SIZE];

    CTC_BMP_INIT(p_status->bmp);

    /* 1. get SupIntrFatal [27:0] */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(SupIntrFatal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (value[0])
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_CHIP_FATAL);
    }

    /* 2. get SupIntrNormal [24:0] */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(SupIntrNormal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (value[0])
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_CHIP_NORMAL);
    }

    /* 3. get RlmHsCtlInterruptFunc0 [31:0] */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(RlmHsCtlInterruptFunc0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (value[0])
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_PCS_LINK_31_0);
    }

    /* 4. get RlmCsCtlInterruptFunc0 [17:0] */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(RlmCsCtlInterruptFunc0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (value[0])
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_PCS_LINK_47_32);
    }

    /* 5. get RlmHsCtlInterruptFunc1 [31:0] */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(RlmHsCtlInterruptFunc1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (value[0])
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_PCS_LINK_79_48);
    }

    /* 6. get RlmCsCtlInterruptFunc1 [17:0] */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(RlmCsCtlInterruptFunc1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (value[0])
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_PCS_LINK_95_80);
    }

    /* 7. get RlmIpeCtlInterruptFunc0 [0:0] */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(RlmIpeCtlInterruptFunc0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (value[0])
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_IPE_LEARN_CACHE_0);
    }

    /* 8. get RlmIpeCtlInterruptFunc1 [0:0] */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(RlmIpeCtlInterruptFunc1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (value[0])
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_IPE_LEARN_CACHE_1);
    }

    /* 9. get RlmEnqCtlInterruptFunc [0:0] */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(RlmEnqCtlInterruptFunc_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (value[0])
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_CHAN_LINKDOWN_SCAN);
    }

    /* 10. get RlmNetCtlInterruptFunc0 [1:0] */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(RlmNetCtlInterruptFunc0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (value[0])
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_MDIO_CHANGE_0);
    }

    /* 11. get RlmNetCtlInterruptFunc1 [1:0] */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(RlmNetCtlInterruptFunc1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (value[0])
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_MDIO_CHANGE_1);
    }

    /* 12. get RlmShareDsCtlInterruptFunc [12:0] */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(RlmShareDsCtlInterruptFunc_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (CTC_BMP_ISSET(value, 0))
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_AGING_FIFO);
    }
    if (CTC_BMP_ISSET(value, 1))
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_STATS_FIFO);
    }
    if (CTC_BMP_ISSET(value, 2) || CTC_BMP_ISSET(value, 3) || CTC_BMP_ISSET(value, 4) || CTC_BMP_ISSET(value, 5))
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_PORT_LOG_STATS_FIFO);
    }
    if (CTC_BMP_ISSET(value, 8))
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_PTP_TOD_CODE_IN_RDY);
    }
    if (CTC_BMP_ISSET(value, 10))
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_PTP_SYNC_CODE_IN_RDY);
    }
    if (CTC_BMP_ISSET(value, 11))
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_PTP_SYNC_CODE_IN_ACC);
    }
    if (CTC_BMP_ISSET(value, 12))
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_PTP_TX_TS_CAPTURE);
    }

    /* 13. get RlmBsCtlInterruptFunc [18:0] */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(RlmBsCtlInterruptFunc_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (CTC_BMP_ISSET(value, 0))
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_MET_LINK_SCAN_DONE);
    }
    if (CTC_BMP_ISSET(value, 1) || CTC_BMP_ISSET(value, 2) || CTC_BMP_ISSET(value, 3) || CTC_BMP_ISSET(value, 4))
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_OAM_CLEAR_EN);
    }
    if (CTC_BMP_ISSET(value, 5))
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_OAM_DEFECT_CACHE);
    }
    if (CTC_BMP_ISSET(value, 6) || CTC_BMP_ISSET(value, 7) || CTC_BMP_ISSET(value, 8))
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_OAM_STATS);
    }
    if (CTC_BMP_ISSET(value, 9) || CTC_BMP_ISSET(value, 10) || CTC_BMP_ISSET(value, 11)
        || CTC_BMP_ISSET(value, 12) || CTC_BMP_ISSET(value, 13))
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_BSR_CONGESTION_1);
    }
    if (CTC_BMP_ISSET(value, 14) || CTC_BMP_ISSET(value, 15) || CTC_BMP_ISSET(value, 16)
        || CTC_BMP_ISSET(value, 17) || CTC_BMP_ISSET(value, 18))
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_FUNC_BSR_CONGESTION_0);
    }

    /* 14. get PcieIntfInterruptFunc [1:0] */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(PcieIntfInterruptFunc_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (value[0])
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_PCIE_BURST_DONE);
    }

    /* 15. get DmaCtlIntrFunc0 / DmaCtlIntrFunc1 [15:0] */
    CTC_BMP_INIT(value);
    cmd = DRV_IOR(DmaCtlIntrFunc0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (value[0])
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_DMA_0);
    }

    CTC_BMP_INIT(value);
    cmd = DRV_IOR(DmaCtlIntrFunc1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    if (value[0])
    {
        CTC_BMP_SET(p_status->bmp, SYS_INTR_GG_DMA_1);
    }

    p_status->bit_count = SYS_INTR_GG_MAX;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_get_status_common(uint8 lchip, uint32 tbl_id, uint32 bit_count, ctc_intr_status_t* p_status)
{
    uint32 cmd = 0;
    uint32 mask[CTC_INTR_STAT_SIZE] = {0};

    CTC_BMP_INIT(p_status->bmp);
    p_status->bit_count = 0;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, p_status->bmp));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask));
    p_status->bmp[0] &= (~mask[0]);
    p_status->bmp[1] &= (~mask[1]);
    p_status->bmp[2] &= (~mask[2]);
    p_status->bit_count = bit_count;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_get_status_sup_fatal(uint8 lchip, sys_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    uint32 cmd = 0;
    uint32 index = INTR_INDEX_VAL_SET;
    uint32 value[CTC_INTR_STAT_SIZE];    /* max is int_lk_interrupt_fatal_t */
    uint32 mask[CTC_INTR_STAT_SIZE];
    int32 tbl_id = 0;
    int32 sub_tbl_id = 0;
    uint32 low_idx = 0;

    CTC_BMP_INIT(value);

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        /* get sub-level status */
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_FATAL_MAX);

        /* Notice: For CTC_INTR_GG_SUB_FATAL_REQ_PCIE_0_FIFO_OVERRUN ~CTC_INTR_GG_SUB_FATAL_REQ_I2C_FIFO_OVERRUN
            have only one level, no sub level, no low level*/
        if ((p_type->sub_intr >= SYS_INTR_GG_SUB_FATAL_REQ_PCIE_0_FIFO_OVERRUN) &&
            (p_type->sub_intr <= SYS_INTR_GG_SUB_FATAL_REQ_I2C_FIFO_OVERRUN))
        {
            return CTC_E_NONE;
        }

        tbl_id = g_intr_sub_reg_sup_fatal[p_type->sub_intr].tbl_id;
        if (tbl_id < 0)
        {
            return CTC_E_INTR_INVALID_PARAM;
        }

        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, value));

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask));
        value[0] = value[0]&(~mask[0]);
        value[1] = value[1]&(~mask[1]);
        value[2] = value[2]&(~mask[2]);
        /*Notice: For pcie and dma fatal interrupt, have no low-level */
        if ((tbl_id == PcieIntfInterruptFatal_t) ||
            (tbl_id == DmaCtlInterruptFatal0_t) ||
            (tbl_id == DmaCtlInterruptFatal1_t))
        {
            sal_memcpy(p_status->bmp, value, sizeof(value));
            p_status->bit_count = TABLE_FIELD_NUM(tbl_id);
            return CTC_E_NONE;
        }

        /*get low-level status */
        if (TABLE_FIELD_NUM(tbl_id) > SYS_INTR_GG_LOW_FATAL_MAX)
        {
            SYS_INTR_DUMP("Sub interrupt bits exceed, tbl_id:%d, field_num:%d\n", tbl_id, TABLE_FIELD_NUM(tbl_id));
        }

        for (low_idx = 0;  low_idx < TABLE_FIELD_NUM(tbl_id); low_idx++)
        {
            if (CTC_BMP_ISSET(value, low_idx))
            {
                sub_tbl_id = g_intr_low_reg_sub_fatal[p_type->sub_intr][low_idx];
                if (sub_tbl_id < 0)
                {
                    return CTC_E_INTR_INVALID_PARAM;
                }

                cmd = DRV_IOR(sub_tbl_id, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, p_status->bmp));
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask));
                p_status->bmp[0] &= (~mask[0]);
                p_status->bmp[1] &= (~mask[1]);
                p_status->bmp[2] &= (~mask[2]);
                p_status->bit_count = TABLE_FIELD_NUM(sub_tbl_id);
                p_status->low_intr = low_idx;
                p_type->low_intr = low_idx;
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_get_status_sup_normal(uint8 lchip, sys_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    uint32 cmd = 0;
    uint32 index = INTR_INDEX_VAL_SET;
    uint32 value[CTC_INTR_STAT_SIZE];    /* gb_sup_interrupt_normal_t */
    uint32 mask[CTC_INTR_STAT_SIZE];
    int32 tbl_id = 0;
    int32 sub_tbl_id = 0;
    uint16 low_idx = 0;
    CTC_BMP_INIT(value);

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        /* get low-level status */
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_NORMAL_MAX);

        tbl_id = g_intr_sub_reg_sup_normal[p_type->sub_intr].tbl_id;
        if (tbl_id < 0)
        {
            return CTC_E_INTR_INVALID_PARAM;
        }

        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, value));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask));
        value[0] = value[0]&(~mask[0]);
        value[1] = value[1]&(~mask[1]);
        value[2] = value[2]&(~mask[2]);

        /*Notice: For pcie and dma normal interrupt, have no low-level */
        if ((tbl_id == DmaCtlInterruptNormal0_t) ||
            (tbl_id == DmaCtlInterruptNormal1_t) ||
            (tbl_id == PcieIntfInterruptNormal_t) ||
            (tbl_id == I2CMasterInterruptNormal0_t) ||
            (tbl_id == I2CMasterInterruptNormal1_t))
        {
            sal_memcpy(p_status->bmp, value, sizeof(value));
            p_status->low_intr = 0;
            p_type->low_intr = 0;
            p_status->bit_count = TABLE_FIELD_NUM(tbl_id);
            return CTC_E_NONE;
        }
        /*get low-level status */
        if (TABLE_FIELD_NUM(tbl_id) > SYS_INTR_GG_LOW_NORMAL_MAX)
        {
            SYS_INTR_DUMP("Sub interrupt bits exceed, tbl_id:%d, field_num:%d\n", tbl_id, TABLE_FIELD_NUM(tbl_id));
        }

        for (low_idx = 0;  low_idx < TABLE_FIELD_NUM(tbl_id); low_idx++)
        {
            if (CTC_BMP_ISSET(value, low_idx))
            {
                sub_tbl_id = g_intr_low_reg_sub_normal[p_type->sub_intr][low_idx];
                if (sub_tbl_id < 0)
                {
                    return CTC_E_INTR_INVALID_PARAM;
                }

                cmd = DRV_IOR(sub_tbl_id, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, p_status->bmp));
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask));
                p_status->bmp[0] &= (~mask[0]);
                p_status->bmp[1] &= (~mask[1]);
                p_status->bmp[2] &= (~mask[2]);
                p_status->bit_count = TABLE_FIELD_NUM(sub_tbl_id);
                p_status->low_intr = low_idx;
                p_type->low_intr = low_idx;
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_get_status_sup_func(uint8 lchip, sys_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    sys_intr_info_t *p_info = NULL;
    uint32 tmp[CTC_INTR_STAT_SIZE];
    uint32 i = 0;
    int32 ret = CTC_E_NONE;

    INTR_SUP_FUNC_TYPE_CHECK(p_type->intr);

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        return CTC_E_INTR_INVALID_SUB_TYPE;
    }

    p_info = &g_intr_info[p_type->intr];
    if (!p_info)
    {
        return CTC_E_INTR_NOT_SUPPORT;
    }

    ret = _sys_goldengate_interrupt_get_status_common(lchip, p_info->tbl_id, 32, p_status);
    sal_memset(tmp, 0, sizeof(tmp));
    for (i = 0; i < p_info->bit_count; i++)
    {
        if (CTC_BMP_ISSET(p_status->bmp, p_info->bit_offset+i))
        {
            CTC_BMP_SET(tmp, i);
        }
    }
    sal_memcpy(p_status->bmp, tmp, CTC_INTR_STAT_SIZE);
    p_status->bit_count = p_info->bit_count;

    return ret;
}

STATIC int32
_sys_goldengate_interrupt_get_status_pcie(uint8 lchip, sys_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    uint32 tbl_id = PcieIntfInterruptFunc_t;

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        /* no low-level status */
        return CTC_E_INTR_INVALID_SUB_TYPE;
    }

    return _sys_goldengate_interrupt_get_status_common(lchip, tbl_id, SYS_INTR_GG_SUB_PCIE_INTF_MAX, p_status);
}

STATIC int32
_sys_goldengate_interrupt_get_status_dma_func(uint8 lchip, sys_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    uint32 tbl_id = (p_type->intr == SYS_INTR_GG_DMA_0) ? DmaCtlIntrFunc0_t : DmaCtlIntrFunc1_t;

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        /* no low-level status */
        return CTC_E_INTR_INVALID_SUB_TYPE;
    }

    return _sys_goldengate_interrupt_get_status_common(lchip, tbl_id, SYS_INTR_GG_SUB_DMA_FUNC_MAX, p_status);
}

STATIC int32
_sys_goldengate_interrupt_set_en_common(uint8 lchip, uint32 bit_offset, uint32 bit_count, uint32 tbl_id, uint32 enable)
{
    uint32 cmd = 0;
    uint32 i = 0;
    uint32 index = 0;
    uint32 mask[CTC_INTR_STAT_SIZE];

    CTC_BMP_INIT(mask);

    index = (enable) ? INTR_INDEX_MASK_RESET : INTR_INDEX_MASK_SET;

#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /* simulation */
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, mask));
#endif

    if (CTC_INTR_ALL != bit_offset)
    {
        for (i = 0; i < bit_count; i++)
        {
            CTC_BMP_SET(mask, bit_offset+i);
        }
    }
    else
    {
        sal_memset(mask, 0xFF, TABLE_ENTRY_SIZE(tbl_id));
    }

    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, mask));

#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /* simulation */
    sys_goldengate_interrupt_model_sim_mask(lchip, tbl_id, bit_offset, bit_count, enable);
#endif

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_set_en_sup_fatal(uint8 lchip, sys_intr_type_t* p_type, uint32 enable)
{
    uint32 tbl_id = SupIntrFatal_t;

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_FATAL_MAX);
    }

    return _sys_goldengate_interrupt_set_en_common(lchip, p_type->sub_intr, 1, tbl_id, enable);
}

STATIC int32
_sys_goldengate_interrupt_set_en_sup_normal(uint8 lchip, sys_intr_type_t* p_type, uint32 enable)
{
    uint32 tbl_id = SupIntrNormal_t;

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_NORMAL_MAX);
    }

    return _sys_goldengate_interrupt_set_en_common(lchip, p_type->sub_intr, 1, tbl_id, enable);
}

STATIC int32
_sys_goldengate_interrupt_set_en_sup_func(uint8 lchip, sys_intr_type_t* p_type, uint32 enable)
{
    sys_intr_info_t *p_info = NULL;

    INTR_SUP_FUNC_TYPE_CHECK(p_type->intr);

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        return CTC_E_INTR_INVALID_SUB_TYPE;
    }

    p_info = &g_intr_info[p_type->intr];
    if (!p_info)
    {
        return CTC_E_INTR_NOT_SUPPORT;
    }

    return _sys_goldengate_interrupt_set_en_common(lchip, p_info->bit_offset, p_info->bit_count, p_info->tbl_id, enable);
}

STATIC int32
_sys_goldengate_interrupt_set_en_pcie(uint8 lchip, sys_intr_type_t* p_type, uint32 enable)
{
    uint32 tbl_id = PcieIntfInterruptFunc_t;

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_PCIE_INTF_MAX);
    }

    return _sys_goldengate_interrupt_set_en_common(lchip, p_type->sub_intr, 1, tbl_id, enable);
}

STATIC int32
_sys_goldengate_interrupt_set_en_dma_func(uint8 lchip, sys_intr_type_t* p_type, uint32 enable)
{
    uint32 tbl_id = (p_type->intr == SYS_INTR_GG_DMA_0) ? DmaCtlIntrFunc0_t : DmaCtlIntrFunc1_t;

    if (CTC_INTR_ALL != p_type->sub_intr)
    {
        INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_DMA_FUNC_MAX);
    }

    return _sys_goldengate_interrupt_set_en_common(lchip, p_type->sub_intr, 1, tbl_id, enable);
}

STATIC int32
_sys_goldengate_interrupt_get_en_common(uint8 lchip, uint32 bit_offset, uint32 bit_count, uint32 tbl_id, uint32* p_enable)
{
    uint32 cmd = 0;
    uint32 i = 0;
    uint32 index = INTR_INDEX_MASK_SET;
    uint32 mask[CTC_INTR_STAT_SIZE];

    CTC_BMP_INIT(mask);

    *p_enable = 0;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, mask));

    if (1 == bit_count)
    {
        *p_enable = (CTC_BMP_ISSET(mask, bit_offset)) ? FALSE : TRUE;
    }
    else
    {
        for (i = 0; i < bit_count; i++)
        {
            if (CTC_BMP_ISSET(mask, bit_offset+i))
            {
                CTC_BMP_UNSET(p_enable, i);
            }
            else
            {
                CTC_BMP_SET(p_enable, i);
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_get_en_sup_fatal(uint8 lchip, sys_intr_type_t* p_type, uint32* p_enable)
{
    uint32 tbl_id = SupIntrFatal_t;

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_FATAL_MAX);
    return _sys_goldengate_interrupt_get_en_common(lchip, p_type->sub_intr, 1, tbl_id, p_enable);
}

STATIC int32
_sys_goldengate_interrupt_get_en_sup_normal(uint8 lchip, sys_intr_type_t* p_type, uint32* p_enable)
{
    uint32 tbl_id = SupIntrNormal_t;

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_NORMAL_MAX);
    return _sys_goldengate_interrupt_get_en_common(lchip, p_type->sub_intr, 1, tbl_id, p_enable);
}

STATIC int32
_sys_goldengate_interrupt_get_en_sup_func(uint8 lchip, sys_intr_type_t* p_type, uint32* p_enable)
{
    sys_intr_info_t *p_info = NULL;

    INTR_SUP_FUNC_TYPE_CHECK(p_type->intr);
    p_info = &g_intr_info[p_type->intr];
    if (!p_info)
    {
        return CTC_E_INTR_NOT_SUPPORT;
    }

    return _sys_goldengate_interrupt_get_en_common(lchip, p_info->bit_offset, p_info->bit_count, p_info->tbl_id, p_enable);
}

STATIC int32
_sys_goldengate_interrupt_get_en_pcie(uint8 lchip, sys_intr_type_t* p_type, uint32* p_enable)
{
    uint32 tbl_id = PcieIntfInterruptFunc_t;

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_PCIE_INTF_MAX);
    return _sys_goldengate_interrupt_get_en_common(lchip, p_type->sub_intr, 1, tbl_id, p_enable);
}

STATIC int32
_sys_goldengate_interrupt_get_en_dma_func(uint8 lchip, sys_intr_type_t* p_type, uint32* p_enable)
{
    uint32 tbl_id = (p_type->intr == SYS_INTR_GG_DMA_0) ? DmaCtlIntrFunc0_t : DmaCtlIntrFunc1_t;

    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_DMA_FUNC_MAX);
    return _sys_goldengate_interrupt_get_en_common(lchip, p_type->sub_intr, 1, tbl_id, p_enable);
}

STATIC int32
_sys_goldengate_interrupt_set_sup_op(uint8 lchip, uint8 grp_id, uint8 act_idx)
{
    uint32 cmd = 0;
    SupIntrGenCtl_m sup_ctl;
    uint32 value = 0;

    value = (1 << grp_id);

    cmd = DRV_IOR(SupIntrGenCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sup_ctl));

    if (INTR_INDEX_VAL_SET == act_idx)
    {
        SetSupIntrGenCtl(V, supIntrValueSet_f, &sup_ctl, value);
    }
    else if (INTR_INDEX_VAL_RESET == act_idx)
    {
        SetSupIntrGenCtl(V, supIntrValueClr_f, &sup_ctl, value);
    }
    else if (INTR_INDEX_MASK_SET == act_idx)
    {
        SetSupIntrGenCtl(V, supIntrMaskSet_f, &sup_ctl, value);
    }
    else
    {
        SetSupIntrGenCtl(V, supIntrMaskClr_f, &sup_ctl, value);
    }

    cmd = DRV_IOW(SupIntrGenCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sup_ctl));

    return CTC_E_NONE;
}

/*used to check interrupt whether is event interrupt*/
STATIC uint32
_sys_goldengate_interrupt_check_event_intr(uint8 lchip, sys_intr_type_t* p_type)
{
    uint32 is_event = 0;

    switch (p_type->intr)
    {
        case  SYS_INTR_GG_PCS_LINK_31_0:
        case  SYS_INTR_GG_PCS_LINK_47_32:
        case  SYS_INTR_GG_PCS_LINK_79_48:
        case  SYS_INTR_GG_PCS_LINK_95_80:
            is_event = 1;
            break;
        case SYS_INTR_GG_FUNC_CHAN_LINKDOWN_SCAN:
            is_event = 1;
            break;

        default:
            is_event = 0;
            break;
    }

    return ((is_event)?TRUE:FALSE);
}

STATIC int32
_sys_goldengate_interrupt_dispatch_dma_func(uint8 lchip, sys_intr_group_t* p_group, uint32 reg_index)
{
    uint32 dma_func[1];
    uint32 mask[1];
    sys_intr_type_t type;
    sys_intr_t* p_intr = NULL;
    uint32 cmd = 0;
    uint32 tbl_id = g_intr_reg[reg_index].tbl_id;
    uint32 intr = g_intr_reg[reg_index].intr_start;

    sal_memset(dma_func, 0, sizeof(dma_func));

    /* 1. get interrupt status */
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, &dma_func));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &mask));

    dma_func[0] &= ~mask[0];
    if (0 == dma_func[0])
    {
        /* no interrupt */
        return CTC_E_NONE;
    }

    /* 2. clear sup interrupt status */
    /* for DMA interrupt is trigger by event but not state, so need to clear status before call ISR */
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, &dma_func));

    /* 3. call ISR */
    type.intr = intr;
    p_intr = &(p_gg_intr_master[lchip]->intr[type.intr]);
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
    sys_goldengate_interrupt_model_sim_value(lchip, tbl_id, intr, dma_func, FALSE);
#endif

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_dispatch_sup(uint8 lchip, sys_intr_group_t* p_group, uint32 reg_index)
{
    uint32 status[CTC_INTR_STAT_SIZE];
    uint32 mask_status[CTC_INTR_STAT_SIZE];
    sys_intr_type_t type;
    sys_intr_t* p_intr = NULL;
    uint32 cmd = 0;
    uint32 tbl_id = g_intr_reg[reg_index].tbl_id;
    uint32 intr = g_intr_reg[reg_index].intr_start;

    CTC_BMP_INIT(status);

    /* 1. get sup interrupt status */
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, &status));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, &mask_status));

    status[0] &= (~mask_status[0]);

    if (0 == status[0])
    {
        /* no interrupt */
        return CTC_E_NONE;
    }

    /* 2. call ISR */
    type.intr = intr;
    p_intr = &(p_gg_intr_master[lchip]->intr[type.intr]);
    if (p_intr->valid)
    {
        p_intr->occur_count++;
        if (p_intr->isr)
        {
            SYS_INTERRUPT_DBG_INFO("Dispatch interrupt intr: %d, count: %d\n", type.intr, p_intr->occur_count);
            p_intr->isr(lchip, type.intr, status);
        }
    }

    /* 3. clear sup interrupt status */
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, status));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_dispatch_func_bs(uint8 lchip, sys_intr_group_t* p_group, uint32 reg_index)
{
    uint32 status[CTC_INTR_STAT_SIZE];
    uint32 mask[CTC_INTR_STAT_SIZE];
    sys_intr_type_t type;
    sys_intr_t* p_intr = NULL;
    uint32 cmd = 0;
    uint32 tbl_id = g_intr_reg[reg_index].tbl_id;
    uint8 index = 0;
    uint8 sub_idx = 0;
    sys_intr_info_t* p_info = NULL;
    uint32 intr_map = 0;

    CTC_BMP_INIT(status);

    /* 1. get sup interrupt status */
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, &status));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &mask));

    status[0] &= ~mask[0];

    if (0 == status[0])
    {
        /* no interrupt */
        return CTC_E_NONE;
    }

    /* 2. call ISR */
    for (index = SYS_INTR_GG_FUNC_MET_LINK_SCAN_DONE; index <= SYS_INTR_GG_FUNC_RESERVED1; index++)
    {
        p_info = &g_intr_info[index];
        if (p_info->valid)
        {
            intr_map = 0;
            for (sub_idx = 0; sub_idx < p_info->bit_count; sub_idx++)
            {
                CTC_BIT_SET(intr_map, p_info->bit_offset+sub_idx);
            }

            if (status[0] & intr_map)
            {
                type.intr = index;
                p_intr = &(p_gg_intr_master[lchip]->intr[type.intr]);
                if (p_intr->valid)
                {
                    p_intr->occur_count++;
                    if (p_intr->isr)
                    {
                        SYS_INTERRUPT_DBG_INFO("Dispatch interrupt intr: %d, count: %d\n", type.intr, p_intr->occur_count);
                        p_intr->isr(lchip, type.intr, status);
                    }
                }

                /* 3. clear sup interrupt status */
                _sys_goldengate_interrupt_clear_status_sup_func(lchip, &type, status);
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_dispatch_func_shareds(uint8 lchip, sys_intr_group_t* p_group, uint32 reg_index)
{
    uint32 status[CTC_INTR_STAT_SIZE];
    uint32 mask[CTC_INTR_STAT_SIZE];
    sys_intr_type_t type;
    sys_intr_t* p_intr = NULL;
    uint32 cmd = 0;
    uint32 tbl_id = g_intr_reg[reg_index].tbl_id;
    uint8 index = 0;
    uint8 sub_idx = 0;
    sys_intr_info_t* p_info = NULL;
    uint32 intr_map = 0;

    CTC_BMP_INIT(status);

    /* 1. get sup interrupt status */
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, &status));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &mask));

    status[0] &= ~mask[0];

    if (0 == status[0])
    {
        /* no interrupt */
        return CTC_E_NONE;
    }

    /* 2. call ISR */
    for (index = SYS_INTR_GG_FUNC_AGING_FIFO; index <= SYS_INTR_GG_FUNC_PTP_TX_TS_CAPTURE; index++)
    {
        p_info = &g_intr_info[index];
        if (p_info->valid)
        {
            intr_map = 0;
            for (sub_idx = 0; sub_idx < p_info->bit_count; sub_idx++)
            {
                CTC_BIT_SET(intr_map, p_info->bit_offset+sub_idx);
            }

            if (status[0] & intr_map)
            {
                type.intr = index;
                p_intr = &(p_gg_intr_master[lchip]->intr[type.intr]);
                if (p_intr->valid)
                {
                    p_intr->occur_count++;
                    if (p_intr->isr)
                    {
                        SYS_INTERRUPT_DBG_INFO("Dispatch interrupt intr: %d, count: %d\n", type.intr, p_intr->occur_count);
                        p_intr->isr(lchip, type.intr, status);
                    }
                }

                /* 3. clear sup interrupt status */
                _sys_goldengate_interrupt_clear_status_sup_func(lchip, &type, status);
            }
        }
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_dispatch_func_normal(uint8 lchip, sys_intr_group_t* p_group, uint32 reg_index)
{
    uint32 status[CTC_INTR_STAT_SIZE];
    uint32 mask[CTC_INTR_STAT_SIZE];
    sys_intr_type_t type;
    sys_intr_t* p_intr = NULL;
    uint32 cmd = 0;
    uint32 tbl_id = g_intr_reg[reg_index].tbl_id;
    uint32 intr = g_intr_reg[reg_index].intr_start;
    uint32 is_event = 0;

    CTC_BMP_INIT(status);

    /* 1. get sup interrupt status */
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, &status));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &mask));

    status[0] &= ~mask[0];
    if (0 == status[0])
    {
        /* no interrupt */
        return CTC_E_NONE;
    }

    /* 2. call ISR */
    type.intr = intr;

    is_event = _sys_goldengate_interrupt_check_event_intr(lchip, &type);
    if (is_event)
    {
        /* 3. clear sup interrupt status */
        _sys_goldengate_interrupt_clear_status_sup_func(lchip, &type, status);
    }

    p_intr = &(p_gg_intr_master[lchip]->intr[type.intr]);
    if (p_intr->valid)
    {
        p_intr->occur_count++;
        if (p_intr->isr)
        {
            SYS_INTERRUPT_DBG_INFO("Dispatch interrupt intr: %d, count: %d\n", type.intr, p_intr->occur_count);
            p_intr->isr(lchip, type.intr, status);
        }
    }

    if (!is_event)
    {
        /* 3. clear sup interrupt status */
        _sys_goldengate_interrupt_clear_status_sup_func(lchip, &type, status);
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_interrupt_sup_intr_mask(uint8 lchip, uint8 grp_id, bool is_mask)
{
    uint32 cmd = 0;
    SupIntrGenCtl_m intr_mask;
    uint32 value = 0;

    if (grp_id >= SYS_GG_MAX_INTR_GROUP)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    sal_memset(&intr_mask, 0, sizeof(SupIntrGenCtl_m));
    cmd = DRV_IOR(SupIntrGenCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intr_mask));


    if (is_mask)
    {
        value = GetSupIntrGenCtl(V, supIntrMaskSet_f, &intr_mask);
        value = value | (1<<grp_id);
        SetSupIntrGenCtl(V, supIntrMaskSet_f, &intr_mask, value);
        SetSupIntrGenCtl(V, supIntrMaskClr_f, &intr_mask, 0);
    }
    else
    {
        value = GetSupIntrGenCtl(V, supIntrMaskClr_f, &intr_mask);
        value = value | (1<<grp_id);
        SetSupIntrGenCtl(V, supIntrMaskClr_f, &intr_mask, value);
    }

    cmd = DRV_IOW(SupIntrGenCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intr_mask));

    if ((!is_mask) && (p_gg_intr_master[lchip]->intr_mode == 1))
    {
        uint32 table_id = 0;
        uint8  pcie_sel = 0;
        CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &pcie_sel));
        table_id = pcie_sel ? Pcie1IntrLogCtl_t : Pcie0IntrLogCtl_t;
        value = 0;
        cmd = DRV_IOR(table_id, Pcie0IntrLogCtl_pcie0IntrLogVec_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        CTC_BIT_UNSET(value, grp_id);
        cmd = DRV_IOW(table_id, Pcie0IntrLogCtl_pcie0IntrLogVec_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_set_mapping(uint8 lchip, sys_intr_group_t* p_group, bool enable)
{
    ds_t map_ctl;
    uint32 cmd = 0;
    uint32 intr_bmp = 0;

    cmd = DRV_IOR(SupIntrMapCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &map_ctl));

    switch (p_group->irq_idx)
    {
        case 0:
            intr_bmp = GetSupIntrMapCtl(V, supIntrMap0_f, &map_ctl);
            intr_bmp = (enable)?(intr_bmp|p_group->intr_bmp):(intr_bmp&(~p_group->intr_bmp));
            SetSupIntrMapCtl(V, supIntrMap0_f, &map_ctl, intr_bmp);
            break;

        case 1:
            intr_bmp = GetSupIntrMapCtl(V, supIntrMap1_f, &map_ctl);
            intr_bmp = (enable)?(intr_bmp|p_group->intr_bmp):(intr_bmp&(~p_group->intr_bmp));
            SetSupIntrMapCtl(V, supIntrMap1_f, &map_ctl, intr_bmp);
            break;

        case 2:
            intr_bmp = GetSupIntrMapCtl(V, supIntrMap2_f, &map_ctl);
            intr_bmp = (enable)?(intr_bmp|p_group->intr_bmp):(intr_bmp&(~p_group->intr_bmp));
            SetSupIntrMapCtl(V, supIntrMap2_f, &map_ctl, intr_bmp);
            break;

        case 3:
            intr_bmp = GetSupIntrMapCtl(V, supIntrMap3_f, &map_ctl);
            intr_bmp = (enable)?(intr_bmp|p_group->intr_bmp):(intr_bmp&(~p_group->intr_bmp));
            SetSupIntrMapCtl(V, supIntrMap3_f, &map_ctl, intr_bmp);
            break;

        default:
            break;
    }

    cmd = DRV_IOW(SupIntrMapCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &map_ctl));

    return CTC_E_NONE;
}

STATIC int
_sys_goldengate_interrupt_get_group_id(uint8 lchip, uint32 irq, uint8* group_cnt, uint8* group_arry)
{
    uint8  i   = 0;
    uint8  cnt = 0;
    uint8  group_id = 0;

    /* get irq's group info */
    for(i=0; i<p_gg_intr_master[lchip]->group_count; i++)
    {
        if(irq == p_gg_intr_master[lchip]->group[i].irq)
        {
            /* get which group occur interrupt */
            group_id = i;
            *(group_arry+cnt) = group_id;
            cnt++;
        }
    }

    *group_cnt = cnt;

    return CTC_E_NONE;
}

/* get which chip occur intrrrupt by IRQ */
STATIC int
_sys_goldengate_interrupt_get_chip(uint32 irq, uint8 lchip_num, uint8* chip_num, uint8* chip_array)
{
    uint8 lchip_idx = 0;
    uint8 group_idx = 0;
    int32 ret = 0;

    /* get which chip occur intrrupt, and get the group_id */
    *chip_num = 0;
    for (lchip_idx=0; lchip_idx<lchip_num; lchip_idx++)
    {
        ret = sys_goldengate_chip_check_active(lchip_idx);
        if (ret < 0)
        {
            continue;
        }

        if (NULL == p_gg_intr_master[lchip_idx])
        {
            continue;
        }

        for(group_idx=0; group_idx<p_gg_intr_master[lchip_idx]->group_count; group_idx++)
        {
            if(irq == p_gg_intr_master[lchip_idx]->group[group_idx].irq)
            {
                *(chip_array+lchip_idx) = lchip_idx;
                (*chip_num)++;
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_second_dispatch(uint8 lchip, sys_intr_group_t* p_group)
{
    _sys_goldengate_interrupt_set_mapping(lchip, p_group, FALSE);
    _sys_goldengate_interrupt_dispatch_dma_func(lchip, p_group, SYS_INTR_GG_REG_DMA_CTL_0);
    CTC_ERROR_RETURN(sal_sem_give(p_group->p_sync_sem));
    return CTC_E_NONE;
}

/**
 @brief Dispatch interrupt of a group
*/
void
sys_goldengate_interrupt_dispatch(void* p_data)
{
    uint8 lchip = 0;
    uint8 occur_intr_lchip[CTC_MAX_LOCAL_CHIP_NUM] = {0};
    uint32 irq = *((uint32*)p_data);
    uint32 group = 0;
    sys_intr_group_t* p_group = NULL;
    uint32 cmd = 0;
    uint32 status[CTC_INTR_STAT_SIZE];
    uint8 lchip_idx = 0;
    uint8 lchip_num = 0;
    uint8 group_cnt = 0;
    uint8  occur_intr_group_id[SYS_GG_MAX_INTR_GROUP] = {0};
    uint8 i = 0;
    uint8 irq_index = 0;
    uint8 chip_num = 0;

    SYS_INTERRUPT_DBG_FUNC();
    /* get lchip num */
    lchip_num = sys_goldengate_get_local_chip_num();

    /* init lchip=0xff, 0xff is invalid value */
    for (lchip_idx = 0; lchip_idx < CTC_MAX_LOCAL_CHIP_NUM; lchip_idx++)
    {
        occur_intr_lchip[lchip_idx] = 0xff;
    }

    /* get which chip occur interrupt */
    _sys_goldengate_interrupt_get_chip(irq, lchip_num, &chip_num, occur_intr_lchip);

    /* dispatch by chip */
    for(lchip_idx=0; lchip_idx<lchip_num; lchip_idx++)
    {
        if(0xff == occur_intr_lchip[lchip_idx])
        {
            continue;
        }

        lchip = occur_intr_lchip[lchip_idx];
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);

        /* get which group coour interrupt */
        _sys_goldengate_interrupt_get_group_id(lchip, irq, &group_cnt, occur_intr_group_id);

        /* mask sup interrupt, one irq maybe include mulit gruops, use occur_intr_group_id[0] get irq index */
        group = occur_intr_group_id[0];
        p_group = &(p_gg_intr_master[lchip]->group[group]);
        irq_index = p_group->irq_idx;
        _sys_goldengate_interrupt_sup_intr_mask(lchip, irq_index, TRUE);

        for(i=0; i<group_cnt; i++)
        {
            group = occur_intr_group_id[i];
            p_group = &(p_gg_intr_master[lchip]->group[group]);
            if (!p_group)
            {
                return;
            }

            SYS_INTERRUPT_DBG_INFO("Dispatch interrupt group: %d, IRQ: %d\n", group, p_group->irq);

            p_group->occur_count++;
            /* TODO this process need to change after fjia defined SupCtlVec.intrVec[29:0] */
            if(!p_group->p_sync_task)
            {
                /* 1. get sup interrupt status */
                cmd = DRV_IOR(SupCtlIntrVec_t, DRV_ENTRY_FLAG);
                (DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, &status));

                /* get groups mask */
                status[0] = (status[0] & p_group->intr_bmp);
                if (status[0] & SYS_INTR_GG_RLM_BS_CTL_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_func_bs(lchip, p_group, SYS_INTR_GG_REG_RLM_BS_CTL);
                }

                if (status[0] & SYS_INTR_GG_RLM_SHARE_DS_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_func_shareds(lchip, p_group, SYS_INTR_GG_REG_RLM_SHARE_CTL);
                }

                if (status[0] & SYS_INTR_GG_RLM_HS_CTL_0_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_HS_CTL_0);
                }

                if (status[0] & SYS_INTR_GG_RLM_CS_CTL_0_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_CS_CTL_0);
                }

                if (status[0] & SYS_INTR_GG_RLM_HS_CTL_1_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_HS_CTL_1);
                }

                if (status[0] & SYS_INTR_GG_RLM_CS_CTL_1_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_CS_CTL_1);
                }

                if (status[0] & SYS_INTR_GG_RLM_IPE_CTL_0_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_IPE_CTL_0);
                }

                if (status[0] & SYS_INTR_GG_RLM_SHARE_TCAM_CTL_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_TCAM_CTL);
                }

                if (status[0] & SYS_INTR_GG_RLM_IPE_CTL_1_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_IPE_CTL_1);
                }

                if (status[0] & SYS_INTR_GG_RLM_ENQ_CTL_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_ENQ_CTL);
                }

                if (status[0] & SYS_INTR_GG_MDIO_CHANGE_0_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_NET_CTL_0);
                }

                if (status[0] & SYS_INTR_GG_MDIO_CHANGE_1_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_NET_CTL_1);
                }

                if (status[0] & SYS_INTR_GG_DMA_0_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_dma_func(lchip, p_group, SYS_INTR_GG_REG_DMA_CTL_0);
                }

                if (status[0] & SYS_INTR_GG_DMA_1_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_dma_func(lchip, p_group, SYS_INTR_GG_REG_DMA_CTL_1);
                }

                if (status[0] & SYS_INTR_GG_PCIE_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_PCIE_INTF);
                }

                if (status[0] & SYS_INTR_GG_SUP_FATAL_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_sup(lchip, p_group, SYS_INTR_GG_REG_SUP_FATAL);
                }

                if (status[0] & SYS_INTR_GG_SUP_NORMAL_BITS)
                {
                    _sys_goldengate_interrupt_dispatch_sup(lchip, p_group, SYS_INTR_GG_REG_SUP_NORMAL);
                }
            }
            else if(p_group->p_sync_task) /* second dispatch */
            {
                _sys_goldengate_interrupt_second_dispatch(lchip, p_group);
            }
        }
    }

    /* before release cpu interrupt, reset sup interrupt */
    for(lchip_idx=0; lchip_idx<lchip_num; lchip_idx++)
    {
        if(0xff == occur_intr_lchip[lchip_idx])
        {
            continue;
        }
        lchip = occur_intr_lchip[lchip_idx];
        /* get which group coour interrupt */
        _sys_goldengate_interrupt_get_group_id(lchip, irq, &group_cnt, occur_intr_group_id);

        /* clear sup interrupt */
        group = occur_intr_group_id[0];
        p_group = &(p_gg_intr_master[lchip]->group[group]);
        irq_index = p_group->irq_idx;
        _sys_goldengate_interrupt_set_sup_op(lchip, irq_index, INTR_INDEX_VAL_RESET);
    }

    if (g_dal_op.interrupt_set_en)
    {
        g_dal_op.interrupt_set_en(irq, TRUE);
    }

    for(lchip_idx=0; lchip_idx<lchip_num; lchip_idx++)
    {
        if(0xff == occur_intr_lchip[lchip_idx])
        {
            continue;
        }

        lchip = occur_intr_lchip[lchip_idx];
        /* get which group coour interrupt */
        _sys_goldengate_interrupt_get_group_id(lchip, irq, &group_cnt, occur_intr_group_id);

        /* mask sup interrupt */
        group = occur_intr_group_id[0];
        p_group = &(p_gg_intr_master[lchip]->group[group]);
        irq_index = p_group->irq_idx;
        /* mask sup interrupt */
        _sys_goldengate_interrupt_sup_intr_mask(lchip, irq_index, FALSE);
    }

    return;
}


sys_intr_op_t g_gg_intr_op[SYS_INTR_GG_REG_TYPE_MAX] =
{
    {
        _sys_goldengate_interrupt_clear_status_sup_fatal,
        _sys_goldengate_interrupt_get_status_sup_fatal,
        _sys_goldengate_interrupt_set_en_sup_fatal,
        _sys_goldengate_interrupt_get_en_sup_fatal,
    },
    {
        _sys_goldengate_interrupt_clear_status_sup_normal,
        _sys_goldengate_interrupt_get_status_sup_normal,
        _sys_goldengate_interrupt_set_en_sup_normal,
        _sys_goldengate_interrupt_get_en_sup_normal,
    },
    {
        _sys_goldengate_interrupt_clear_status_sup_func,
        _sys_goldengate_interrupt_get_status_sup_func,
        _sys_goldengate_interrupt_set_en_sup_func,
        _sys_goldengate_interrupt_get_en_sup_func,
    },
    {
        _sys_goldengate_interrupt_clear_status_pcie,
        _sys_goldengate_interrupt_get_status_pcie,
        _sys_goldengate_interrupt_set_en_pcie,
        _sys_goldengate_interrupt_get_en_pcie,
    },
    {
        _sys_goldengate_interrupt_clear_status_dma_func,
        _sys_goldengate_interrupt_get_status_dma_func,
        _sys_goldengate_interrupt_set_en_dma_func,
        _sys_goldengate_interrupt_get_en_dma_func,
    },
};

STATIC int32
_sys_goldengate_interrupt_release_fatal(uint8 lchip, ctc_intr_type_t* p_type, uint32 enable)
{
    uint32 tbl_id = 0;
    uint32 tbl_id_low = 0;
    uint32 cmd = 0;
    int32 ret = CTC_E_NONE;
    uint32 value_a = 0;
    uint32 mask[CTC_INTR_STAT_SIZE];

    CTC_BMP_INIT(mask);

    /* relase mask of all fatal interrupt */
    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_FATAL_MAX);
    INTR_SUB_TYPE_CHECK(p_type->low_intr, SYS_INTR_GG_LOW_FATAL_MAX);

    /* now only support mask */
    if (!enable)
    {
        ret = CTC_E_NOT_SUPPORT;
        return ret;
    }

    tbl_id = g_intr_sub_reg_sup_fatal[p_type->sub_intr].tbl_id;
    tbl_id_low =  g_intr_low_reg_sub_fatal[p_type->sub_intr][p_type->low_intr];

    if ((tbl_id == -1) || (tbl_id_low == -1))
    {
        return ret;
    }

    /* clear mask of low-level interrupt */
    cmd = DRV_IOR(tbl_id_low, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask));
    cmd = DRV_IOW(tbl_id_low, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, mask));

    /* clear mask of sub-level interrupt */
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask));

    if (RlmShareDsCtlInterruptFatal_t == tbl_id)
    {
        mask[0] &= 0x17f;
    }

    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, mask));

    /* clear mask of sup-level interrupt */
    value_a = (1<<p_type->sub_intr);
    cmd = DRV_IOW(SupIntrFatal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, &value_a));

    return CTC_E_NONE;
}


/* if flag==1, id is table_id. if flag==0, id is serdes_id */
STATIC uint32
_sys_goldengate_interrupt_serdes_is_used(uint8 lchip, uint32 id, uint8 type)
{
    uint16 serdes_id = 0;
    uint8 flag = 0;
    sys_datapath_serdes_info_t* p_serdes = NULL;

    if(1 == type)
    {
        serdes_id = id - HssAnethWrapperInterruptNormal0_t;
    }
    else if(0 == type)
    {
        serdes_id = id;
    }

    sys_goldengate_datapath_get_serdes_info(lchip, serdes_id, &p_serdes);
    if(NULL != p_serdes)
    {
        if(CTC_CHIP_SERDES_NONE_MODE == p_serdes->mode)
        {
            flag = 0;
        }
        else
        {
            flag = 1;
        }
    }

    return flag;
}


STATIC int32
_sys_goldengate_interrupt_release_normal(uint8 lchip, ctc_intr_type_t* p_type, uint32 enable)
{
    uint32 tbl_id = 0;
    uint32 tbl_id_low = 0;
    uint32 cmd = 0;
    int32  ret = CTC_E_NONE;
    uint32 value_a = 0;
    uint32 serdes_used  = 0;
    uint32 serdes_id    = 0;
    uint32 mask[CTC_INTR_STAT_SIZE];
    uint8  i = 0;

    CTC_BMP_INIT(mask);

    /* relase mask of all fatal interrupt */
    INTR_SUB_TYPE_CHECK(p_type->sub_intr, SYS_INTR_GG_SUB_NORMAL_MAX);
    INTR_SUB_TYPE_CHECK(p_type->low_intr, SYS_INTR_GG_LOW_NORMAL_MAX);

    /* now only support mask */
    if (!enable)
    {
        ret = CTC_E_NOT_SUPPORT;
        return ret;
    }

    tbl_id = g_intr_sub_reg_sup_normal[p_type->sub_intr].tbl_id;
    tbl_id_low =  g_intr_low_reg_sub_normal[p_type->sub_intr][p_type->low_intr];

    if (tbl_id == -1)
    {
        return ret;
    }

    /* clear mask of low-level interrupt */
    if (tbl_id_low != -1)
    {
        if ((tbl_id_low >= HssAnethWrapperInterruptNormal0_t) && (tbl_id_low <= HssAnethWrapperInterruptNormal95_t))
        {
            serdes_used = _sys_goldengate_interrupt_serdes_is_used(lchip, tbl_id_low, 1);
            if (!serdes_used)
            {
                /*serdes not used, just skip*/
                return CTC_E_NONE;
            }

            /* clear random value */
            mask[0] = 0x3f;
            cmd = DRV_IOW(tbl_id_low, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask));
            cmd = DRV_IOW(tbl_id_low, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, mask));

            mask[0] = 0x11;
        }
        else
        {
            cmd = DRV_IOR(tbl_id_low, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask));
            if ((tbl_id_low >= SharedPcsInterruptNormal0_t) && (tbl_id_low <= SharedPcsInterruptNormal23_t))
            {
                mask[0] = 0xffffffff;
                mask[1] = 0xffffffff;
                cmd = DRV_IOW(tbl_id_low, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask));
                cmd = DRV_IOW(tbl_id_low, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, mask));

                mask[0] = 0;
                mask[1] = 0;
            }

            if ((tbl_id_low == QMgrDeqSliceInterruptNormal0_t) || (tbl_id_low == QMgrDeqSliceInterruptNormal1_t))
            {
                mask[0] &= ~0xe0;
            }

            if (ShareDlbInterruptNormal_t == tbl_id_low)
            {
                mask[0] = 0x3;
            }

            /*mask bsNetRxPauseReqTypeMismatch */
            if ((tbl_id_low == NetTxInterruptNormal0_t) || (tbl_id_low == NetTxInterruptNormal1_t))
            {
                mask[0] &= ~0x8000;
            }
            if ((tbl_id_low == CgPcsInterruptNormal0_t) || (tbl_id_low == CgPcsInterruptNormal1_t) ||
                (tbl_id_low == CgPcsInterruptNormal2_t) || (tbl_id_low == CgPcsInterruptNormal3_t))
            {
                mask[0] = 0;
            }

            if (tbl_id_low == DynamicDsShareInterruptNormal_t)
            {
                mask[0] &= ~0x02;
            }
        }

        cmd = DRV_IOW(tbl_id_low, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, mask));
    }

    /* clear mask of sub-level interrupt */
    mask[0] = 0xffffffff;
    mask[1] = 0xffffffff;
    mask[2] = 0xffffffff;

    if (tbl_id == DmaCtlInterruptNormal0_t)
    {
        mask[1] &= ~0x07;
        mask[0] &= ~0xc000f280;
    }

    if (tbl_id == PcieIntfInterruptNormal_t)
    {
        mask[0] &= ~0x3000;
    }

    if ((tbl_id == RlmHsCtlInterruptNormal0_t) || (tbl_id == RlmHsCtlInterruptNormal1_t))
    {
        mask[0] = 0xffffffff;
        mask[1] = 0xff;

        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask));
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, mask));

        /*serdes not used, keep mask*/
        mask[0] = 0xffffffff;
        mask[1] = 0x0;

        for(i=0; i<32; i++)
        {
            if(tbl_id == RlmHsCtlInterruptNormal1_t)
            {
                serdes_id = i+48;
            }
            else
            {
                serdes_id = i;
            }
            serdes_used = _sys_goldengate_interrupt_serdes_is_used(lchip, serdes_id, 0);
            if (!serdes_used)
            {
                mask[0] &= (~(1<<i));
            }
        }
    }

    if ((tbl_id == RlmCsCtlInterruptNormal0_t) || (tbl_id == RlmCsCtlInterruptNormal1_t))
    {
        mask[0] = 0xffffffff;

        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, mask));
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, mask));

        /*serdes not used, keep mask*/
        mask[0] = 0xffff;
        for(i=0; i<16; i++)
        {
            if(tbl_id == RlmCsCtlInterruptNormal1_t)
            {
                serdes_id = i+80;
            }
            else
            {
                serdes_id = i+32;
            }
            serdes_used = _sys_goldengate_interrupt_serdes_is_used(lchip, serdes_id, 0);
            if (!serdes_used)
            {
                mask[0] &= (~(1<<i));
            }
        }
    }

    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, mask));

    /* clear mask of sup-level interrupt */
    value_a = (1<<p_type->sub_intr);
    cmd = DRV_IOW(SupIntrNormal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, &value_a));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_init_ops(uint8 lchip)
{
    sys_intr_op_t** p_op = p_gg_intr_master[lchip]->op;

    p_op[SYS_INTR_GG_CHIP_FATAL]                    = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_SUP_FATAL]);
    p_op[SYS_INTR_GG_CHIP_NORMAL]                   = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_SUP_NORMAL]);
    p_op[SYS_INTR_GG_PCS_LINK_31_0]                 = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_PCS_LINK_47_32]                = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_PCS_LINK_79_48]                = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_PCS_LINK_95_80]                = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_IPE_LEARN_CACHE_0]        = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_IPE_LEARN_CACHE_1]        = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_CHAN_LINKDOWN_SCAN]       = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_MDIO_CHANGE_0]            = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_MDIO_CHANGE_1]            = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_IPFIX_USEAGE_OVERFLOW]  = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_AGING_FIFO]               = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_STATS_FIFO]               = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_PORT_LOG_STATS_FIFO]      = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_PTP_TOD_CODE_IN_RDY]      = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_PTP_SYNC_CODE_IN_RDY]     = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_PTP_SYNC_CODE_IN_ACC]     = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_PTP_TX_TS_CAPTURE]        = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_MET_LINK_SCAN_DONE]       = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_OAM_CLEAR_EN]             = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_OAM_DEFECT_CACHE]         = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_OAM_STATS]                = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_BSR_CONGESTION_1]         = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_FUNC_BSR_CONGESTION_0]         = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_FUNC]);
    p_op[SYS_INTR_GG_PCIE_BURST_DONE]               = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_PCIE]);
    p_op[SYS_INTR_GG_DMA_0]                         = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_DMA]);
    p_op[SYS_INTR_GG_DMA_1]                         = &(g_gg_intr_op[SYS_INTR_GG_REG_TYPE_DMA]);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_set_interrupt_en(uint8 lchip, uint32 value)
{
    uint32 cmd = 0;
    SupIntrCtl_m intr_ctl;

    cmd = DRV_IOR(SupIntrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intr_ctl));
    if (value)
    {
        SetSupIntrCtl(V, funcIntrEn_f, &intr_ctl, 1);
        SetSupIntrCtl(V, interruptEn_f, &intr_ctl, 1);
        if(p_gg_intr_master[lchip]->intr_mode)
        {
            SetSupIntrCtl(V, padIntrEn_f, &intr_ctl, 0);
            SetSupIntrCtl(V, pcie0IntrEn_f, &intr_ctl, 1);
            SetSupIntrCtl(V, pcie1IntrEn_f, &intr_ctl, 1);
        }
        else
        {
            SetSupIntrCtl(V, padIntrEn_f, &intr_ctl, 1);
            SetSupIntrCtl(V, pcie0IntrEn_f, &intr_ctl, 0);
            SetSupIntrCtl(V, pcie1IntrEn_f, &intr_ctl, 0);
        }
    }
    else
    {
        SetSupIntrCtl(V, funcIntrEn_f, &intr_ctl, 0);
        SetSupIntrCtl(V, interruptEn_f, &intr_ctl, 0);
        SetSupIntrCtl(V, padIntrEn_f, &intr_ctl, 0);
        SetSupIntrCtl(V, pcie0IntrEn_f, &intr_ctl, 0);
        SetSupIntrCtl(V, pcie1IntrEn_f, &intr_ctl, 0);
    }
    cmd = DRV_IOW(SupIntrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intr_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_interrupt_init_reg(uint8 lchip)
{
    sys_intr_group_t* p_group = NULL;
    ds_t map_ctl;
    SupIntrGenCtl_m gen_ctl;
    uint32 cmd = 0;
    uint32 i = 0;
    uint32 j = 0;
    uint32 index = 0;
    uint32 mask_set = 0;
    uint32 unmask_set = 0;
    sys_intr_info_t* p_intr = NULL;
    RlmShareDsCtlInterruptFuncMap_m share_map;
    RlmBsCtlInterruptFuncMap_m bs_map;
    DmaCtlIntrFunc0_m intr_ctl;
    uint32 value_set = 0;
    uint32 cmd_idx = 0;
    ctc_intr_type_t type;
    drv_work_platform_type_t platform_type;
    uint32 lp_status[2] = {0};
    host_type_t byte_order;
    Pcie0IntrGenCtl_m pcie_intr_ctl;
    uint32 intr_bmp = 0;
    uint8  pcie_sel = 0;
    if (!p_gg_intr_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    sal_memset(map_ctl, 0, sizeof(map_ctl));
    sal_memset(&gen_ctl, 0, sizeof(gen_ctl));
    sal_memset(&bs_map, 0, sizeof(bs_map));
    sal_memset(&share_map, 0, sizeof(share_map));

    mask_set = 0xFF;

    CTC_ERROR_RETURN(drv_goldengate_get_platform_type(&platform_type));
    CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &pcie_sel));
    for (i = 0; i < SYS_GG_MAX_INTR_GROUP; i++)
    {
        p_group = &(p_gg_intr_master[lchip]->group[i]);
        if (p_group->group < 0)
        {
            continue;
        }

        if (p_group->group >= 0)
        {
            CTC_BIT_UNSET(mask_set, p_group->irq_idx);
        }

        switch (p_group->irq_idx)
        {
        case 0:
            intr_bmp = GetSupIntrMapCtl(V, supIntrMap0_f, &map_ctl);
            intr_bmp |= p_group->intr_bmp;
            SetSupIntrMapCtl(V, supIntrMap0_f, &map_ctl, intr_bmp);
            break;

        case 1:
            intr_bmp = GetSupIntrMapCtl(V, supIntrMap1_f, &map_ctl);
            intr_bmp |= p_group->intr_bmp;
            SetSupIntrMapCtl(V, supIntrMap1_f, &map_ctl, intr_bmp);
            break;

        case 2:
            intr_bmp = GetSupIntrMapCtl(V, supIntrMap2_f, &map_ctl);
            intr_bmp |= p_group->intr_bmp;
            SetSupIntrMapCtl(V, supIntrMap2_f, &map_ctl, intr_bmp);
            break;

        case 3:
            intr_bmp = GetSupIntrMapCtl(V, supIntrMap3_f, &map_ctl);
            intr_bmp |= p_group->intr_bmp;
            SetSupIntrMapCtl(V, supIntrMap3_f, &map_ctl, intr_bmp);
            break;

        default:
            break;
        }
    }

    unmask_set = (~mask_set)&0xff;

    SetSupIntrGenCtl(V, supIntrMaskSet_f, &gen_ctl, mask_set);
    SetSupIntrGenCtl(V, supIntrMaskClr_f, &gen_ctl, unmask_set);


    cmd = DRV_IOW(SupIntrMapCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &map_ctl));

    cmd = DRV_IOW(SupIntrGenCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &gen_ctl));


    /* config mapping RlmShareDsCtlInterruptFuncMap */
    for (i = SYS_INTR_GG_FUNC_AGING_FIFO; i <= SYS_INTR_GG_FUNC_PTP_TX_TS_CAPTURE; i++)
    {
        p_intr = &g_intr_info[i];
        if (FALSE == p_intr->valid)
        {
            continue;
        }

        value_set = 0;

        cmd_idx = i - SYS_INTR_GG_FUNC_AGING_FIFO;
        if (p_intr->bit_count)
        {
            for (index = 0; index < p_intr->bit_count; index++)
            {
                CTC_BIT_SET(value_set, p_intr->bit_offset+index);
            }
        }

        cmd = DRV_IOW(RlmShareDsCtlInterruptFuncMap_t, DRV_ENTRY_FLAG);
        SetRlmShareDsCtlInterruptFuncMap(V, funcIntrMap_f, &share_map, value_set);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, cmd_idx, cmd, &share_map));

        cmd = DRV_IOW(RlmShareDsCtlInterruptFunc_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, &value_set));
    }

    /* config mapping RlmBsCtlInterruptFuncMap */
    for (i = SYS_INTR_GG_FUNC_MET_LINK_SCAN_DONE; i <= SYS_INTR_GG_FUNC_RESERVED1; i++)
    {
        p_intr = &g_intr_info[i];
        if (FALSE == p_intr->valid)
        {
            continue;
        }

        value_set = 0;

        cmd_idx = i - SYS_INTR_GG_FUNC_MET_LINK_SCAN_DONE;
        if (p_intr->bit_count)
        {
            for (index = 0; index < p_intr->bit_count; index++)
            {
                CTC_BIT_SET(value_set, p_intr->bit_offset+index);
            }
        }

        cmd = DRV_IOW(RlmBsCtlInterruptFuncMap_t, DRV_ENTRY_FLAG);
        SetRlmBsCtlInterruptFuncMap(V, funcIntrMap_f, &bs_map, value_set);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, cmd_idx, cmd, &bs_map));

/* TODO Confirm how to process these interrupt */
value_set &= 0xfff03fff;
        cmd = DRV_IOW(RlmBsCtlInterruptFunc_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_RESET, cmd, &value_set));
    }

    CTC_ERROR_RETURN(_sys_goldengate_set_interrupt_en(lchip, 0));

    /* enable msi byte order for little endian CPU */
    if(p_gg_intr_master[lchip]->intr_mode)
    {
        byte_order = drv_goldengate_get_host_type();
        if (byte_order == HOST_LE)
        {
            cmd = DRV_IOR(Pcie0IntrGenCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pcie_intr_ctl));

            SetPcie0IntrGenCtl(V, cfgMsiByteOrder_f, &pcie_intr_ctl, 1);

            cmd = DRV_IOW(Pcie0IntrGenCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pcie_intr_ctl));
        }
    }

    /*clear some interrupt */
    cmd = DRV_IOR(Pcie0IpStatus_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, lp_status);
    SetPcie0IpStatus(V, pcie0TldlpErrorRec_f,lp_status, 0);
    cmd = DRV_IOW(Pcie0IpStatus_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, lp_status);

    cmd = DRV_IOR(Pcie1IpStatus_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, lp_status);
    SetPcie1IpStatus(V, pcie1TldlpErrorRec_f,lp_status, 0);
    cmd = DRV_IOW(Pcie1IpStatus_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, lp_status);

    cmd = DRV_IOR(PcieIntfInterruptNormal_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &value_set);
    cmd = DRV_IOW(PcieIntfInterruptNormal_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 1, cmd, &value_set);

    cmd = DRV_IOR(PcieIntfInterruptFatal_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &value_set);
    cmd = DRV_IOW(PcieIntfInterruptFatal_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 1, cmd, &value_set);

    /* release interrupt mask */
    {
        mask_set = 0xe90f;
        SetDmaCtlIntrFunc0(V, dmaIntrValidVec_f, &intr_ctl, mask_set);
        cmd = DRV_IOW(DmaCtlIntrFunc0_t+pcie_sel, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 3, cmd, &intr_ctl));
    }

    mask_set = 0x01;
    cmd = DRV_IOW(RlmShareTcamCtlInterruptFunc_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 3, cmd, &mask_set));

    mask_set = 0x01;
    cmd = DRV_IOW(RlmEnqCtlInterruptFunc_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 3, cmd, &mask_set));

    /* release fatal interrupt mask */
    for (i = 0; i < SYS_INTR_GG_SUB_FATAL_PCIE_INTF; i++)
    {
        for (j = 0; j < SYS_INTR_GG_LOW_FATAL_MAX; j++)
        {
            type.intr = 0;
            type.sub_intr = i;
            type.low_intr = j;
            _sys_goldengate_interrupt_release_fatal(lchip, &type, 1);
        }
    }

    /* release normal interrupt mask */
    for (i = 0; i < SYS_INTR_GG_SUB_NORMAL_MAX; i++)
    {
        for (j = 0; j < SYS_INTR_GG_LOW_NORMAL_MAX; j++)
        {
            type.intr = 0;
            type.sub_intr = i;
            type.low_intr = j;
            _sys_goldengate_interrupt_release_normal(lchip, &type, 1);
        }
    }

    /*mask CgmacInterruptNormal rxfiforun*/
    mask_set = 0x04;
    cmd = DRV_IOW(CgmacInterruptNormal0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &mask_set));
    mask_set = 0x04;
    cmd = DRV_IOW(CgmacInterruptNormal1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &mask_set));
    mask_set = 0x04;
    cmd = DRV_IOW(CgmacInterruptNormal2_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &mask_set));
    mask_set = 0x04;
    cmd = DRV_IOW(CgmacInterruptNormal3_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &mask_set));

    if (platform_type == HW_PLATFORM)
    {
        /*for pcie/dma0/dma1 fatal interrupt special release mask*/
        drv_goldengate_chip_write(lchip, 0x0000ac0c, 0x03);   /*PcieIntfInterruptFatal*/
        drv_goldengate_chip_write(lchip, (pcie_sel?0x0001e080:0x00016098), 0xffffffff);   /*DmaCtlInterruptFatal0*/

        drv_goldengate_chip_write(lchip, 0x0001609c, 0x000003ff);

        drv_goldengate_chip_write(lchip, 0x0002001c, 0xc000007);   /*SupIntrFatal*/

        /*clear some interrupt do clear dring init*/
        drv_goldengate_chip_write(lchip, 0x00020014, 0x02000000);   /*SupIntrFatal*/
    }

    return CTC_E_NONE;
}

/**
 @brief Clear interrupt status
*/
int32
_sys_goldengate_interrupt_clear_status_entry(uint8 lchip, sys_intr_type_t* p_type, uint32* p_bmp)
{
    SYS_INTERRUPT_INIT_CHECK(lchip);
    INTR_TYPE_CHECK(p_type->intr);

    if (p_gg_intr_master[lchip]->op[p_type->intr]->clear_status)
    {
        return p_gg_intr_master[lchip]->op[p_type->intr]->clear_status(lchip, p_type, p_bmp);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_interrupt_clear_status(uint8 lchip, ctc_intr_type_t* p_type, uint32 with_sup)
{
    ctc_intr_status_t intr_status;
    sys_intr_type_t sys_intr;

    SYS_INTERRUPT_INIT_CHECK(lchip);
    sal_memset(&sys_intr, 0, sizeof(sys_intr_type_t));

    _sys_goldengate_interrupt_type_mapping(lchip, p_type, &sys_intr);

    if (p_gg_intr_master[lchip]->op[p_type->intr]->get_status)
    {
         p_gg_intr_master[lchip]->op[p_type->intr]->get_status(lchip, &sys_intr, &intr_status);
    }

    _sys_goldengate_interrupt_clear_status_entry(lchip, &sys_intr, intr_status.bmp);

    return CTC_E_NONE;
}

/**
 @brief Get interrupt status
*/
int32
sys_goldengate_interrupt_get_status(uint8 lchip, ctc_intr_type_t* p_type, ctc_intr_status_t* p_status)
{
    int32 ret = 0;
    sys_intr_type_t sys_intr;
    SYS_INTERRUPT_INIT_CHECK(lchip);


    sal_memset(&sys_intr, 0, sizeof(sys_intr_type_t));

    _sys_goldengate_interrupt_type_mapping(lchip, p_type, &sys_intr);

    if (CTC_INTR_ALL == p_type->intr)
    {
        /* get sup-level status */
        return _sys_goldengate_interrupt_get_status_sup_level(lchip, &sys_intr, p_status);
    }
    else
    {
        INTR_TYPE_CHECK(sys_intr.intr);
        if (p_gg_intr_master[lchip]->op[sys_intr.intr]->get_status)
        {
            ret = p_gg_intr_master[lchip]->op[sys_intr.intr]->get_status(lchip, &sys_intr, p_status);
            p_type->low_intr = sys_intr.low_intr;
            return ret;
        }
    }

    return CTC_E_NONE;
}

/**
 @brief Set interrupt enable
 Notice: Sdk sys level for enable interrupt should use this interface
*/
int32
sys_goldengate_interrupt_set_en_internal(uint8 lchip, sys_intr_type_t* p_type, uint32 enable)
{
    SYS_INTERRUPT_INIT_CHECK(lchip);
    if (p_gg_intr_master[lchip]->op[p_type->intr]->set_en)
    {
        return p_gg_intr_master[lchip]->op[p_type->intr]->set_en(lchip, p_type, enable);
    }

    return CTC_E_NONE;
}

/**
 @brief Set interrupt enable
  Notice: Sdk ctc level for enable interrupt should use this interface
*/
int32
sys_goldengate_interrupt_set_en(uint8 lchip, ctc_intr_type_t* p_type, uint32 enable)
{
    sys_intr_type_t sys_intr;

    SYS_INTERRUPT_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_type);

    sal_memset(&sys_intr, 0, sizeof(sys_intr_type_t));

    CTC_ERROR_RETURN(_sys_goldengate_interrupt_type_mapping(lchip, p_type, &sys_intr));

    INTR_TYPE_CHECK(sys_intr.intr);

    CTC_ERROR_RETURN(sys_goldengate_interrupt_set_en_internal(lchip, &sys_intr, enable));

    return CTC_E_NONE;
}

/**
 @brief Get interrupt enable, need confirm
*/
int32
sys_goldengate_interrupt_get_en(uint8 lchip, ctc_intr_type_t* p_type, uint32* p_enable)
{
    sys_intr_type_t sys_intr;

    SYS_INTERRUPT_INIT_CHECK(lchip);

    sal_memset(&sys_intr, 0, sizeof(sys_intr_type_t));

    _sys_goldengate_interrupt_type_mapping(lchip, p_type, &sys_intr);

    INTR_TYPE_CHECK(sys_intr.intr);

    if (p_gg_intr_master[lchip]->op[sys_intr.intr]->get_en)
    {
        return p_gg_intr_master[lchip]->op[sys_intr.intr]->get_en(lchip, &sys_intr, p_enable);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_interrupt_get_info(uint8 lchip, ctc_intr_type_t* p_ctc_type, uint32 intr_bit, tbls_id_t* p_intr_tbl, uint8* p_action_type, uint8* p_ecc_or_parity)
{
    uint8  fldid = 0;

    SYS_INTERRUPT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_ctc_type);
    CTC_PTR_VALID_CHECK(p_intr_tbl);
    CTC_PTR_VALID_CHECK(p_action_type);
    CTC_PTR_VALID_CHECK(p_ecc_or_parity);

    *p_ecc_or_parity = 0;

    if (CTC_INTR_CHIP_NORMAL == p_ctc_type->intr)
    {
        if ((p_ctc_type->sub_intr < SYS_INTR_GG_SUB_NORMAL_MAX)
            && (p_ctc_type->low_intr < SYS_INTR_GG_LOW_NORMAL_MAX))
        {
            if (0xFFFFFFFF == g_intr_low_reg_sub_normal[p_ctc_type->sub_intr][p_ctc_type->low_intr])
            {
                *p_intr_tbl = g_intr_sub_reg_sup_normal[p_ctc_type->sub_intr].tbl_id;
                if (0xFFFFFFFF == *p_intr_tbl)
                {
                    *p_intr_tbl = MaxTblId_t;
                }
            }
            else
            {
                *p_intr_tbl = g_intr_low_reg_sub_normal[p_ctc_type->sub_intr][p_ctc_type->low_intr];
            }
        }
    }
    else if (CTC_INTR_CHIP_FATAL == p_ctc_type->intr)
    {
        if ((p_ctc_type->sub_intr < SYS_INTR_GG_SUB_FATAL_PCIE_INTF)
           && (p_ctc_type->low_intr < SYS_INTR_GG_LOW_FATAL_MAX))
        {
            if (0xFFFFFFFF == g_intr_low_reg_sub_fatal[p_ctc_type->sub_intr][p_ctc_type->low_intr])
            {
                *p_intr_tbl = g_intr_sub_reg_sup_fatal[p_ctc_type->sub_intr].tbl_id;
                if (0xFFFFFFFF == *p_intr_tbl)
                {
                    *p_intr_tbl = MaxTblId_t;
                }
            }
            else
            {
                *p_intr_tbl = g_intr_low_reg_sub_fatal[p_ctc_type->sub_intr][p_ctc_type->low_intr];
            }
        }
    }

    if (MaxTblId_t == *p_intr_tbl)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(drv_goldengate_ecc_recover_get_intr_info(*p_intr_tbl, intr_bit, &fldid, p_action_type, p_ecc_or_parity));

    return CTC_E_NONE;
}

int32
sys_goldengate_interrupt_proc_ecc_or_parity(uint8 lchip, tbls_id_t intr_tbl, uint32 intr_bit)
{
    drv_ecc_intr_param_t intr_param;
    uint32 bmp[CTC_INTR_STAT_SIZE] = {0};

    sal_memset(&intr_param, 0, sizeof(drv_ecc_intr_param_t));

    CTC_BMP_SET(bmp, intr_bit);
    intr_param.p_bmp = bmp;
    intr_param.intr_tblid = intr_tbl;
    intr_param.total_bit_num = CTC_INTR_STAT_SIZE * CTC_UINT32_BITS;
    intr_param.valid_bit_count = 1;
    CTC_ERROR_RETURN(drv_goldengate_ecc_recover_restore(lchip + drv_gg_init_chip_info.drv_init_chipid_base, &intr_param));

    return CTC_E_NONE;
}

/**
 @brief Set abnormal interrupt callback function
*/
int32
sys_goldengate_interrupt_set_abnormal_intr_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb)
{
    p_gg_intr_master[lchip]->abnormal_intr_cb = cb;
    return CTC_E_NONE;
}

/**
 @brief Register event callback function
*/
int32
sys_goldengate_interrupt_register_event_cb(uint8 lchip, ctc_interrupt_event_t event, CTC_INTERRUPT_EVENT_FUNC cb)
{
    switch (event)
    {
    case CTC_EVENT_PTP_TX_TS_CAPUTRE:
        CTC_ERROR_RETURN(sys_goldengate_ptp_set_tx_ts_captured_cb(lchip, cb));
        break;

    case CTC_EVENT_PTP_INPUT_CODE_READY:
        CTC_ERROR_RETURN(sys_goldengate_ptp_set_input_code_ready_cb(lchip, cb));
        break;

    case CTC_EVENT_OAM_STATUS_UPDATE:
        CTC_ERROR_RETURN(sys_goldengate_oam_set_defect_cb(lchip, cb));
        break;

    case CTC_EVENT_L2_SW_LEARNING:
        CTC_ERROR_RETURN(sys_goldengate_learning_set_event_cb(lchip, cb));
        break;

    case CTC_EVENT_L2_SW_AGING:
        CTC_ERROR_RETURN(sys_goldengate_aging_set_event_cb(lchip, cb));
        break;

    case CTC_EVENT_PORT_LINK_CHANGE:
        CTC_ERROR_RETURN(sys_goldengate_port_set_link_status_event_cb(lchip, cb));
        break;

    case CTC_EVENT_ABNORMAL_INTR:
        CTC_ERROR_RETURN(sys_goldengate_interrupt_set_abnormal_intr_cb(lchip, cb));
        break;

    case CTC_EVENT_ECC_ERROR:
        CTC_ERROR_RETURN(drv_goldengate_ecc_recover_register_event_cb(cb));
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
sys_goldengate_interrupt_dump_reg(uint8 lchip, uint32 intr)
{
    SYS_INTERRUPT_INIT_CHECK(lchip);

    if (CTC_IS_BIT_SET(intr, SYS_INTR_GG_MAX))
    {
        _sys_goldengate_interrupt_dump_group_reg(lchip);
    }

    if (CTC_IS_BIT_SET(intr, SYS_INTR_GG_CHIP_FATAL))
    {
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_SUP_FATAL);
    }

    if (CTC_IS_BIT_SET(intr, SYS_INTR_GG_CHIP_NORMAL))
    {
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_SUP_NORMAL);
    }

    if (CTC_IS_BIT_SET(intr, SYS_INTR_GG_PCS_LINK_31_0))
    {
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_RLM_HS_CTL_0);
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_RLM_CS_CTL_0);
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_RLM_HS_CTL_1);
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_RLM_CS_CTL_1);
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_RLM_IPE_CTL_0);
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_RLM_IPE_CTL_1);
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_RLM_ENQ_CTL);
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_RLM_NET_CTL_0);
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_RLM_NET_CTL_1);
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_RLM_SHARE_CTL);
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_RLM_BS_CTL);
    }

    if (CTC_IS_BIT_SET(intr, SYS_INTR_GG_PCIE_BURST_DONE))
    {
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_PCIE_INTF);
    }

    if (CTC_IS_BIT_SET(intr, SYS_INTR_GG_DMA_0))
    {
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_DMA_CTL_0);
        _sys_goldengate_interrupt_dump_one_reg(lchip, SYS_INTR_GG_REG_DMA_CTL_1);
    }

    return CTC_E_NONE;
}

/**
 @brief Display interrupt database values in software
*/
int32
sys_goldengate_interrupt_dump_db(uint8 lchip)
{
    uint32 unbind = FALSE;
    uint32 i = 0;

    SYS_INTERRUPT_INIT_CHECK(lchip);

    SYS_INTR_DUMP("Using %s global configuration\n", p_gg_intr_master[lchip]->is_default ? "default" : "parameter");
    SYS_INTR_DUMP("Group Count:      %d\n", p_gg_intr_master[lchip]->group_count);
    SYS_INTR_DUMP("Interrupt Count:  %d\n", p_gg_intr_master[lchip]->intr_count);

    SYS_INTR_DUMP("\n");
    SYS_INTR_DUMP("Registered Interrupts:\n");

    for (i = 0; i < SYS_GG_MAX_INTR_GROUP; i++)
    {
        _sys_goldengate_interrupt_dump_group(lchip, &(p_gg_intr_master[lchip]->group[i]), 1);
    }

    SYS_INTR_DUMP("Unregistered Interrupts:\n");

    for (i = 0; i < SYS_INTR_MAX_INTR; i++)
    {
        if (!p_gg_intr_master[lchip]->intr[i].valid)
        {
            if (!unbind)
            {
                unbind = TRUE;
                SYS_INTR_DUMP("%-9s %-9s %-9s %-10s %-10s\n", "Group", "Interrupt", "Count", "ISR", "Desc");
                SYS_INTR_DUMP("----------------------------------------------------\n");
            }

            if (INVG != p_gg_intr_master[lchip]->intr[i].intr)
            {
                _sys_goldengate_interrupt_dump_intr(lchip, &(p_gg_intr_master[lchip]->intr[i]));
            }
        }
    }

    if (!unbind)
    {
        SYS_INTR_DUMP("None\n");
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_interrupt_parser_type(uint8 lchip, ctc_intr_type_t type)
{
    char* str[9] =
    {
        "REQ_PCIE0_FIFO_OVERRUN","REQ_PCIE1_FIFO_OVERRUN",
        "REQ_DMA0_FIFO_OVERRUN", "REQ_DMA1_FIFO_OVERRUN",
        "REQ_TRACK_FIFO_OVERRUN", "REQ_TRACK_FIFO_UNDERRUN",
        "RES_CROSS_FIFO_OVERRUN", "REQ_CROSS_FIFO_OVERRUN",
        "REQ_I2C_FIFO_OVERRUN"
    };

    SYS_INTERRUPT_INIT_CHECK(lchip);

    if (type.intr == SYS_INTR_GG_CHIP_FATAL)
    {
        if (type.sub_intr >= SYS_INTR_GG_SUB_FATAL_MAX)
        {
            return CTC_E_INVALID_PARAM;
        }

        if ((type.sub_intr >= SYS_INTR_GG_SUB_FATAL_REQ_PCIE_0_FIFO_OVERRUN) &&
            (type.sub_intr <= SYS_INTR_GG_SUB_FATAL_REQ_I2C_FIFO_OVERRUN))
        {
                SYS_INTR_DUMP("%s\n", str[type.sub_intr-SYS_INTR_GG_SUB_FATAL_REQ_PCIE_0_FIFO_OVERRUN]);
        }
        else
        {
            if ((type.sub_intr >= SYS_INTR_GG_SUB_FATAL_PCIE_INTF) && (type.sub_intr <= SYS_INTR_GG_SUB_FATAL_DMA_1))
            {
                SYS_INTR_DUMP("%s\n", TABLE_NAME(g_intr_sub_reg_sup_fatal[type.sub_intr].tbl_id));
            }
            else
            {
                if (g_intr_low_reg_sub_fatal[type.sub_intr][type.low_intr] == -1)
                {
                    return CTC_E_INVALID_PARAM;
                }

                SYS_INTR_DUMP("%s\n", TABLE_NAME(g_intr_low_reg_sub_fatal[type.sub_intr][type.low_intr]));
            }
        }
    }
    else if (type.intr == SYS_INTR_GG_CHIP_NORMAL)
    {
        if (type.sub_intr >= SYS_INTR_GG_SUB_NORMAL_MAX)
        {
            return CTC_E_INVALID_PARAM;
        }

        if (type.sub_intr == SYS_INTR_GG_SUB_NORMAL_DMA_1)
        {
                SYS_INTR_DUMP("DMA CTL1!\n");
        }
        else
        {
            if (g_intr_low_reg_sub_normal[type.sub_intr][type.low_intr] == -1)
            {
                return CTC_E_INVALID_PARAM;
            }

            SYS_INTR_DUMP("%s\n", TABLE_NAME(g_intr_low_reg_sub_normal[type.sub_intr][type.low_intr]));
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_interrupt_show_status(uint8 lchip)
{
    uint8 i = 0;

    SYS_INTERRUPT_INIT_CHECK(lchip);

    SYS_INTR_DUMP("\n-------------------------Work Mode---------------------\n");

    SYS_INTR_DUMP("%-30s:%-6s \n", "Interrupt Mode", p_gg_intr_master[lchip]->intr_mode?"Msi":"Pin");

    SYS_INTR_DUMP("\n");
    SYS_INTR_DUMP("-------------------------Group Status---------------------\n");

    for (i = 0; i < SYS_GG_MAX_INTR_GROUP; i++)
    {
        _sys_goldengate_interrupt_dump_group(lchip, &(p_gg_intr_master[lchip]->group[i]), 0);
    }

    return CTC_E_NONE;
}

#define __ADD_FOR_SECOND_DISPATCH__
STATIC int32
_sys_goldengate_interrupt_get_irq_info(uint8 lchip, uint8* irq_cnt, uint8* irq_array, uint32* irq_prio_array, uint8* irq_idx_array)
{
    uint8 i   = 0;
    uint8 j   = 0;
    uint8 cnt = 0;
    uint8 irq_tmp = 0;
    uint8 irq_tmp_array[SYS_GG_MAX_INTR_GROUP] = {0};

    if (!p_gg_intr_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }
    cnt = p_gg_intr_master[lchip]->group_count;

    for(i=0; i<cnt; i++)
    {
        irq_tmp_array[i] = p_gg_intr_master[lchip]->group[i].irq;
    }

    /* 1. get irq value, and irq count */
    for(i=0; i<cnt; i++)
    {
        for(j=0; j<(cnt-1-i); j++)
        {
            if(irq_tmp_array[j] > irq_tmp_array[j+1])
            {
                irq_tmp = irq_tmp_array[j];
                irq_tmp_array[j] = irq_tmp_array[j+1];
                irq_tmp_array[j+1] = irq_tmp;
            }
        }
    }

    *irq_cnt=1;
    *irq_array = irq_tmp_array[0];
    for(i=1; i<cnt; i++)
    {
        if(irq_tmp_array[i]!= irq_tmp_array[i-1])
        {
            *(irq_array+(*irq_cnt)) = irq_tmp_array[i];
            (*irq_cnt)++;
        }
    }

    /* 2. get irq prio and get irq_index */
    for(i=0; i<(*irq_cnt); i++)
    {
        for(j=0; j<p_gg_intr_master[lchip]->group_count; j++)
        {
            if(p_gg_intr_master[lchip]->group[j].irq == (*(irq_array+i)))
            {
                *(irq_prio_array+i) = p_gg_intr_master[lchip]->group[j].prio;
                break;
            }
        }

        for(j=0; j<p_gg_intr_master[lchip]->group_count; j++)
        {
            if(p_gg_intr_master[lchip]->group[j].irq == (*(irq_array+i)))
            {
                if(p_gg_intr_master[lchip]->group[j].prio < (*(irq_prio_array+i)))
                {
                    *(irq_prio_array+i) = p_gg_intr_master[lchip]->group[j].prio;
                }

                *(irq_idx_array+i) = p_gg_intr_master[lchip]->group[j].irq_idx;
            }
        }
    }

    return CTC_E_NONE;
}

STATIC void
_sys_goldengate_interrupt_sync_thread(void* param)
{
    uint8 lchip = 0;
    int32 ret = 0;
    int32 prio = 0;
    uint32 cmd = 0;
    uint32 status[CTC_INTR_STAT_SIZE];
    sys_intr_group_t* p_group = (sys_intr_group_t*)param;

    prio = p_group->prio;
    lchip = p_group->lchip;
    sal_task_set_priority(prio);

    while(1)
    {
        ret = sal_sem_take(p_group->p_sync_sem, SAL_SEM_FOREVER);
        if (0 != ret)
        {
            continue;
        }

        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);

        /* 1. get sup interrupt status */
        cmd = DRV_IOR(SupCtlIntrVec_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, &status);

        /* 2. get groups mask */
        status[0] = (status[0] & p_group->intr_bmp);
        if(status[0])
        {
            p_group->occur_count++;
        }

        /* 3. process interrupt */
        if (status[0] & SYS_INTR_GG_RLM_BS_CTL_BITS)
        {
            _sys_goldengate_interrupt_dispatch_func_bs(lchip, p_group, SYS_INTR_GG_REG_RLM_BS_CTL);
        }

        if (status[0] & SYS_INTR_GG_RLM_SHARE_DS_BITS)
        {
            _sys_goldengate_interrupt_dispatch_func_shareds(lchip, p_group, SYS_INTR_GG_REG_RLM_SHARE_CTL);
        }

        if (status[0] & SYS_INTR_GG_RLM_HS_CTL_0_BITS)
        {
            _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_HS_CTL_0);
        }

        if (status[0] & SYS_INTR_GG_RLM_CS_CTL_0_BITS)
        {
            _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_CS_CTL_0);
        }

        if (status[0] & SYS_INTR_GG_RLM_HS_CTL_1_BITS)
        {
            _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_HS_CTL_1);
        }

        if (status[0] & SYS_INTR_GG_RLM_CS_CTL_1_BITS)
        {
            _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_CS_CTL_1);
        }

        if (status[0] & SYS_INTR_GG_RLM_IPE_CTL_0_BITS)
        {
            _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_IPE_CTL_0);
        }

        if (status[0] & SYS_INTR_GG_RLM_SHARE_TCAM_CTL_BITS)
        {
            _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_TCAM_CTL);
        }

        if (status[0] & SYS_INTR_GG_RLM_IPE_CTL_1_BITS)
        {
            _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_IPE_CTL_1);
        }

        if (status[0] & SYS_INTR_GG_RLM_ENQ_CTL_BITS)
        {
            _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_ENQ_CTL);
        }

        if (status[0] & SYS_INTR_GG_MDIO_CHANGE_0_BITS)
        {
            _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_NET_CTL_0);
        }

        if (status[0] & SYS_INTR_GG_MDIO_CHANGE_1_BITS)
        {
            _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_RLM_NET_CTL_1);
        }

        if (status[0] & SYS_INTR_GG_PCIE_BITS)
        {
            _sys_goldengate_interrupt_dispatch_func_normal(lchip, p_group, SYS_INTR_GG_REG_PCIE_INTF);
        }

        if (status[0] & SYS_INTR_GG_SUP_FATAL_BITS)
        {
            _sys_goldengate_interrupt_dispatch_sup(lchip, p_group, SYS_INTR_GG_REG_SUP_FATAL);
        }

        if (status[0] & SYS_INTR_GG_SUP_NORMAL_BITS)
        {
            _sys_goldengate_interrupt_dispatch_sup(lchip, p_group, SYS_INTR_GG_REG_SUP_NORMAL);
        }

        /* 4. mask sup interrupt */
        _sys_goldengate_interrupt_set_mapping(lchip, p_group, TRUE);

    }


    return;
}

STATIC int32
_sys_goldengate_interrupt_init_thread(uint8 lchip, sys_intr_group_t* p_group)
{
    int32 ret = 0;
    char buffer[SAL_TASK_MAX_NAME_LEN] = {0};
    uint8 group_id = 0;
    uint16 prio = 0;

    group_id = p_group->group;
    prio     = p_group->irq;
    sal_sprintf(buffer, "interrupt_group%d-%d", group_id, lchip);

    ret = sal_task_create(&p_group->p_sync_task, buffer,
                           SAL_DEF_TASK_STACK_SIZE, prio, _sys_goldengate_interrupt_sync_thread, (void*)p_group);
    if (ret < 0)
    {
        return CTC_E_NOT_INIT;
    }

    sal_memcpy(p_group->thread_desc, buffer, 32 * sizeof(char));

    return CTC_E_NONE;
}

int32
sys_goldengate_interrupt_set_group_en(uint8 lchip, uint8 enable)
{
    uint8 i = 0;
    uint32 value = enable ? 0 : 1;

    for(i = 0; i < p_gg_intr_master[lchip]->group_count; i++)
    {
        _sys_goldengate_interrupt_sup_intr_mask(lchip, i, value);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_interrupt_get_group_en(uint8 lchip, uint8* enable)
{
    uint8 i = 0;
    uint32 value = 0;
    SupIntrGenCtl_m intr_mask;
    uint32 cmd = 0;
    uint8 group_en = 0;

    sal_memset(&intr_mask, 0, sizeof(SupIntrGenCtl_m));
    cmd = DRV_IOR(SupIntrGenCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intr_mask));

    value = GetSupIntrGenCtl(V, supIntrMaskSet_f, &intr_mask);

    for(i = 0; i < p_gg_intr_master[lchip]->group_count; i++)
    {
        if (!(value & (1<<i)))
        {
            group_en = 1;
            break;
        }
    }

    *enable = group_en;

    return CTC_E_NONE;
}


#ifdef _SAL_LINUX_UM
typedef void (* DRV_EADP_INTR_DISPATCH_CB)(void* p_data);
extern int32
drv_goldengate_register_intr_dispatch_cb(DRV_EADP_INTR_DISPATCH_CB cb);
#endif
/**
 @brief Initialize interrupt module
*/
int32
sys_goldengate_interrupt_init(uint8 lchip, void* p_global_cfg)
{
    ctc_intr_global_cfg_t* p_global_cfg_param = (ctc_intr_global_cfg_t*)p_global_cfg;
    static sys_intr_global_cfg_t intr_cfg;
    uint32 group_count = 0;
    uint32 intr_count = 0;
    uintptr i = 0;
    uint32 j = 0;
    uint32 intr = 0;
    int32 group_id = 0;
    sys_intr_group_t* p_group = NULL;
    uint8 irq_count = 0;
    uintptr irq_index = 0;
    uint8 irq_array[SYS_GG_MAX_INTR_GROUP] = {0};
    uint32 irq_prio_array[SYS_GG_MAX_INTR_GROUP] = {0};
    uint8 irq_idx_array[SYS_GG_MAX_INTR_GROUP] = {0};
    sys_intr_t* p_intr = NULL;
    int32 ret = CTC_E_NONE;
    ctc_intr_mapping_t* p_intr_mapping = NULL;
    uint32 intr_idx = 0;
    static ctc_intr_type_t ctc_intr;
    static sys_intr_type_t sys_intr;
    uint8 irq_base = 0; /* used for msi */
    Pcie0IntrLogCtl_m pcie_log_ctl;
    uint32 value_set = 0;
    uint32 cmd = 0;

    LCHIP_CHECK(lchip);
    if (p_gg_intr_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_memset(&intr_cfg, 0, sizeof(intr_cfg));
    sal_memset(&ctc_intr, 0, sizeof(ctc_intr_type_t));

    /* get default interrupt cfg */
    CTC_ERROR_RETURN(sys_goldengate_interrupt_get_default_global_cfg(lchip, &intr_cfg));
    /* 1. get global configuration */
    if (p_global_cfg_param)
    {
        CTC_PTR_VALID_CHECK(p_global_cfg_param->p_group);
        CTC_PTR_VALID_CHECK(p_global_cfg_param->p_intr);

        group_count = p_global_cfg_param->group_count;
        intr_count = SYS_INTR_GG_MAX;

        if (group_count > SYS_GG_MAX_INTR_GROUP)
        {
            return CTC_E_INVALID_PARAM;
        }

        /* mapping user define interrupt group */
        sal_memset(intr_cfg.group, 0, SYS_GG_MAX_INTR_GROUP*sizeof(ctc_intr_group_t));
        sal_memcpy(intr_cfg.group, p_global_cfg_param->p_group, group_count*sizeof(ctc_intr_group_t));

        /* init intr_cfg.intr[].group */
        for (intr_idx = SYS_INTR_GG_CHIP_FATAL; intr_idx < SYS_INTR_GG_MAX; intr_idx++)
        {
            intr_cfg.intr[intr_idx].group = INVG;
        }

        /* mapping user define interrupt */
        for (intr_idx = 0; intr_idx < p_global_cfg_param->intr_count; intr_idx++)
        {
            p_intr_mapping = (ctc_intr_mapping_t*)(p_global_cfg_param->p_intr+intr_idx);
            ctc_intr.intr = p_intr_mapping->intr;
            sal_memset(&sys_intr, 0, sizeof(sys_intr_type_t));
            ret = _sys_goldengate_interrupt_type_mapping(lchip, &ctc_intr, &sys_intr);
            /* interrupt is not used */
            if (ret < 0)
            {
                continue;
            }

            if (sys_intr.intr == SYS_INTR_GG_PCS_LINK_31_0)
            {
                intr_cfg.intr[SYS_INTR_GG_PCS_LINK_31_0].group = p_intr_mapping->group;
                intr_cfg.intr[SYS_INTR_GG_PCS_LINK_47_32].group = p_intr_mapping->group;
                intr_cfg.intr[SYS_INTR_GG_PCS_LINK_79_48].group = p_intr_mapping->group;
                intr_cfg.intr[SYS_INTR_GG_PCS_LINK_95_80].group = p_intr_mapping->group;
            }
            else
            {
                intr_cfg.intr[sys_intr.intr].group = p_intr_mapping->group;
            }

            if (sys_intr.slice_id == 2)
            {
                intr_cfg.intr[sys_intr.intr+1].group = p_intr_mapping->group;
            }
        }

        intr_cfg.group_count = group_count;
        intr_cfg.intr_count = intr_count;

        if (intr_count > SYS_INTR_MAX_INTR)
        {
            return CTC_E_INVALID_PARAM;
        }

        /* get interrupt mode */
        intr_cfg.intr_mode = p_global_cfg_param->intr_mode;
    }

    p_gg_intr_master[lchip] = (sys_intr_master_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(sys_intr_master_t));
    if (NULL == p_gg_intr_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_intr_master[lchip], 0, sizeof(sys_intr_master_t));

    /* 3. create mutex for intr module */
    ret = sal_mutex_create(&(p_gg_intr_master[lchip]->p_intr_mutex));
    if (ret || (!p_gg_intr_master[lchip]->p_intr_mutex))
    {
        mem_free(p_gg_intr_master[lchip]);
        ret = CTC_E_FAIL_CREATE_MUTEX;
        return ret;
    }

    /* 4. init interrupt & group field of master */
    for (intr = 0; intr < SYS_INTR_MAX_INTR; intr++)
    {
        p_gg_intr_master[lchip]->intr[intr].group = INVG;
        p_gg_intr_master[lchip]->intr[intr].intr = INVG;
    }

    for (group_id = 0; group_id < SYS_GG_MAX_INTR_GROUP; group_id++)
    {
        p_gg_intr_master[lchip]->group[group_id].group = INVG;
    }

    /* 5. init interrupt field of master */
    for (i = 0; i < intr_cfg.intr_count; i++)
    {
        intr = intr_cfg.intr[i].intr;

        p_intr = &(p_gg_intr_master[lchip]->intr[intr]);
        p_intr->group = intr_cfg.intr[i].group;
        p_intr->intr = intr_cfg.intr[i].intr; /*sys_goldengate_intr_default[i].intr*/
        p_intr->isr = intr_cfg.intr[i].isr;   /*sys_goldengate_intr_default[i].isr*/
        sal_strncpy(p_intr->desc, intr_cfg.intr[i].desc, CTC_INTR_DESC_LEN);
    }

    for (i = 0; i < intr_cfg.group_count; i++)
    {
        group_id = intr_cfg.group[i].group;
        if (group_id >= SYS_GG_MAX_INTR_GROUP || group_id < 0)
        {
            continue;
        }
        p_group = &(p_gg_intr_master[lchip]->group[i]);
        p_group->group = i;
        p_group->irq_idx = intr_cfg.group[i].group;
        p_group->irq = intr_cfg.group[i].irq;
        p_group->prio = intr_cfg.group[i].prio;
        p_group->lchip = lchip;
        sal_strncpy(p_group->desc, intr_cfg.group[i].desc, CTC_INTR_DESC_LEN);
        p_group->intr_count = 0;

        /* register interrupts to group */
        for (j = 0; j < SYS_INTR_MAX_INTR; j++)
        {
            if (p_gg_intr_master[lchip]->intr[j].group == p_group->group)
            {
                p_gg_intr_master[lchip]->intr[j].valid = TRUE;
                p_group->intr_count++;
                CTC_BIT_SET(p_group->intr_bmp, p_gg_intr_master[lchip]->intr[j].intr);
            }
        }

        /* calcuate reg bitmap by intr bitmap */
        _sys_goldengate_interrupt_calc_reg_bmp(lchip, p_group);
    }

    /* 6. init other field of master */
    p_gg_intr_master[lchip]->group_count = intr_cfg.group_count;
    p_gg_intr_master[lchip]->is_default = (p_global_cfg_param) ? FALSE : TRUE;
    p_gg_intr_master[lchip]->intr_count = intr_cfg.intr_count;
    p_gg_intr_master[lchip]->intr_mode = intr_cfg.intr_mode;

    /* get irq info */
    _sys_goldengate_interrupt_get_irq_info(lchip, &irq_count, irq_array, irq_prio_array, irq_idx_array);

    /* 7. init other field of master */
    CTC_ERROR_RETURN(_sys_goldengate_interrupt_init_reg(lchip));

    /* 8. init ops */
    CTC_ERROR_RETURN(_sys_goldengate_interrupt_init_ops(lchip));

    if (intr_cfg.intr_mode)
    {
        p_gg_intr_master[lchip]->intr_mode = 1;
        /* for msi interrupt */
        if (g_dal_op.interrupt_set_msi_cap)
        {
            ret = g_dal_op.interrupt_set_msi_cap(lchip, TRUE, irq_count);
            if (ret)
            {
                SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "enable msi failed!!\n");
                return ret;
            }
        }

        /* Note, get the real irq for msi */
        if(g_dal_op.interrupt_get_msi_info)
        {
            ret = g_dal_op.interrupt_get_msi_info(lchip, &irq_base);
            if (ret)
            {
                SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "get msi info failed!!\n");
                return ret;
            }
        }
        /* Note, updata group and irq_info sw */
        for (i = 0; i < p_gg_intr_master[lchip]->group_count; i++)
        {
            p_gg_intr_master[lchip]->group[i].irq = irq_base;
            irq_array[0] = irq_base;
        }
    }

    /* init thread per group. but if the irq have only one group, should not init thread */
    for(i = 0; i < p_gg_intr_master[lchip]->group_count; i++)
    {
        p_group = &(p_gg_intr_master[lchip]->group[i]);
        for(j = 0; j < p_gg_intr_master[lchip]->group_count; j++)
        {
            if((j!=i)&&(p_gg_intr_master[lchip]->group[j].irq == p_gg_intr_master[lchip]->group[i].irq))
            {
                if(!p_group->p_sync_sem)
                {
                    ret = sal_sem_create(&(p_group->p_sync_sem), 0);
                    if (ret < 0)
                    {
                        return CTC_E_NOT_INIT;
                    }
                    CTC_ERROR_RETURN(_sys_goldengate_interrupt_init_thread(lchip, p_group));
                }
            }
        }
    }

    /* 9. call DAL function to register IRQ */
    if (g_dal_op.interrupt_register)
    {
        for (i = 0; i < irq_count; i++)
        {
            irq_index = irq_idx_array[i];
            ret = g_dal_op.interrupt_register(irq_array[i], irq_prio_array[i], sys_goldengate_interrupt_dispatch, (void*)irq_index);
            if (ret < 0)
            {
                SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "register interrupt failed!! irq:%d \n", p_group->irq);
                 /*return CTC_E_INTR_REGISTER_FAIL;*/
            }
        }
    }

    CTC_ERROR_RETURN(_sys_goldengate_set_interrupt_en(lchip, 1));

    /* clear dma intr for msi */
    if(intr_cfg.intr_mode)
    {
        uint8 pcie_sel = 0;
        CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &pcie_sel));
        cmd = DRV_IOR((pcie_sel?Pcie1IntrLogCtl_t:Pcie0IntrLogCtl_t), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &pcie_log_ctl);

        if((GetPcie0IntrLogCtl(V, pcie0IntrLogVec_f, &pcie_log_ctl))&&(GetPcie0IntrLogCtl(V, pcie0IntrReqVec_f, &pcie_log_ctl)))
        {
            /* clear dma fun intr */
            cmd = DRV_IOR((pcie_sel?DmaCtlIntrFunc1_t:DmaCtlIntrFunc0_t), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &value_set);
            cmd = DRV_IOW((pcie_sel?DmaCtlIntrFunc1_t:DmaCtlIntrFunc0_t), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 1, cmd, &value_set);

            /* clear log, trigger intr */
            SetPcie0IntrLogCtl(V, pcie0IntrLogVec_f, &pcie_log_ctl, 0);
            cmd = DRV_IOW((pcie_sel?Pcie1IntrLogCtl_t:Pcie0IntrLogCtl_t), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &pcie_log_ctl);
        }
    }

#ifdef _SAL_LINUX_UM
    drv_goldengate_register_intr_dispatch_cb(sys_goldengate_interrupt_dispatch);
#endif

    return CTC_E_NONE;
}


int32
sys_goldengate_interrupt_deinit(uint8 lchip)
{
    uint32 i = 0;
    int32 ret = 0;
    uint8 irq_cnt = 0;
    uint8 irq_array[SYS_GG_MAX_INTR_GROUP] = {0};
    uint32 irq_prio_array[SYS_GG_MAX_INTR_GROUP] = {0};
    uint8 irq_idx_array[SYS_GG_MAX_INTR_GROUP] = {0};
    sys_intr_group_t* p_group = NULL;

    LCHIP_CHECK(lchip);
    if (NULL == p_gg_intr_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*mask group interrupt*/
    sys_goldengate_interrupt_set_group_en(lchip, FALSE);


    for (i=0; i< p_gg_intr_master[lchip]->group_count; i++)
    {
        p_group = &(p_gg_intr_master[lchip]->group[i]);
        if (NULL != p_group->p_sync_sem)
        {
            sal_sem_give(p_group->p_sync_sem);
            sal_task_destroy(p_group->p_sync_task);
            sal_sem_destroy(p_group->p_sync_sem);
        }
    }
    /* free interrupt */
    if (g_dal_op.interrupt_unregister)
    {
        _sys_goldengate_interrupt_get_irq_info(lchip, &irq_cnt, irq_array, irq_prio_array, irq_idx_array);
        for (i = 0; i < irq_cnt; i++)
        {
            ret = g_dal_op.interrupt_unregister(irq_array[i]);
            if (ret < 0)
            {
                return ret;
            }
        }

        /* for msi interrupt */
        if(1 == p_gg_intr_master[lchip]->intr_mode)
        {
            if (g_dal_op.interrupt_set_msi_cap)
            {
                ret = g_dal_op.interrupt_set_msi_cap(lchip, FALSE, irq_cnt);
                if (ret)
                {
                    SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "disable msi failed!!\n");
                    return ret;
                }
            }
        }
    }

    sal_mutex_destroy(p_gg_intr_master[lchip]->p_intr_mutex);
    mem_free(p_gg_intr_master[lchip]);

    return CTC_E_NONE;
}

