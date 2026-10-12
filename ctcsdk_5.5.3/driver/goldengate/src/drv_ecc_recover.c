/****************************************************************************
 * @date 2015-03-10  The file contains all ecc error fatal interrupt resume realization.
 *
 * Copyright:    (c)2015 Centec Networks Inc.  All rights reserved.
 *
 * Modify History:
 * Revision:     v0.1
 * Date:         2015-03-10
 * Reason:       Create for Goldengate v3.0
 ****************************************************************************/

#include "goldengate/include/drv_lib.h"
#include "goldengate/include/drv_ftm.h"
#include "goldengate/include/drv_ecc_recover.h"

#define DRV_ECC_LOCK \
    if (g_gg_ecc_recover_master && g_gg_ecc_recover_master->p_ecc_mutex) sal_mutex_lock(g_gg_ecc_recover_master->p_ecc_mutex)
#define DRV_ECC_UNLOCK \
    if (g_gg_ecc_recover_master && g_gg_ecc_recover_master->p_ecc_mutex) sal_mutex_unlock(g_gg_ecc_recover_master->p_ecc_mutex)

#define DRV_UINT32_BITS  32

#define _DRV_BMP_OP(_bmp, _offset, _op)     \
    (_bmp[(_offset) / DRV_UINT32_BITS] _op(1U << ((_offset) % DRV_UINT32_BITS)))

#define DRV_BMP_ISSET(bmp, offset)          \
    _DRV_BMP_OP((bmp), (offset), &)

#define DRV_BMP_SET(bmp, offset)            \
    _DRV_BMP_OP((bmp), (offset), |= )

#define IS_ACC_KEY(tblid) ((DsFibHost0FcoeHashKey_t == tblid)           \
                          || (DsFibHost0FcoeHashKey_t == tblid)         \
                          || (DsFibHost0Ipv4HashKey_t == tblid)         \
                          || (DsFibHost0Ipv6McastHashKey_t == tblid)    \
                          || (DsFibHost0Ipv6UcastHashKey_t == tblid)    \
                          || (DsFibHost0MacHashKey_t == tblid)          \
                          || (DsFibHost0MacIpv6McastHashKey_t == tblid) \
                          || (DsFibHost0TrillHashKey_t == tblid))

#define FIB0_REQ_HDR_WD    5
#define CHIP_MAX_MEM_WORDS 24

enum drv_ecc_err_type_e
{
   DRV_ECC_ERR_TYPE_SBE,
   DRV_ECC_ERR_TYPE_MBE,
   DRV_ECC_ERR_TYPE_PARITY_ERROR,
   DRV_ECC_ERR_TYPE_TCAM_ERROR,
   DRV_ECC_ERR_TYPE_OTHER,
   DRV_ECC_ERR_TYPE_NUM
};
typedef enum drv_ecc_err_type_e drv_ecc_err_type_t;

enum drv_ecc_tbl_type_e
{
    DRV_ECC_TBL_TYPE_STATIC,
    DRV_ECC_TBL_TYPE_DYNAMIC,
    DRV_ECC_TBL_TYPE_IGNORE,
    DRV_ECC_TBL_TYPE_OTHER,
    DRV_ECC_TBL_TYPE_NUM
};
typedef enum drv_ecc_tbl_type_e drv_ecc_tbl_type_t;

enum drv_ecc_tcam_block_type_e
{
    DRV_ECC_TCAM_BLOCK_TYPE_IGS_ACL0,
    DRV_ECC_TCAM_BLOCK_TYPE_IGS_ACL1,
    DRV_ECC_TCAM_BLOCK_TYPE_IGS_ACL2,
    DRV_ECC_TCAM_BLOCK_TYPE_IGS_ACL3,

    DRV_ECC_TCAM_BLOCK_TYPE_EGS_ACL0,
    DRV_ECC_TCAM_BLOCK_TYPE_IGS_USERID0,
    DRV_ECC_TCAM_BLOCK_TYPE_IGS_USERID1,

    DRV_ECC_TCAM_BLOCK_TYPE_IGS_LPM0,
    DRV_ECC_TCAM_BLOCK_TYPE_IGS_LPM1,

    DRV_ECC_TCAM_BLOCK_TYPE_NUM
};
typedef enum drv_ecc_tcam_block_type_e drv_ecc_tcam_block_type_t;

enum drv_ecc_action_e
{
    DRV_ECC_ACTION_NULL       = 0,
    DRV_ECC_ACTION_LOG,
    DRV_ECC_ACTION_RESET_CHIP,
    DRV_ECC_ACTION_RESET_PORT
};
typedef enum drv_ecc_action_e drv_ecc_action_t;

/* memory mapping table */
struct drv_ecc_mem_s
{
    uintptr start_addr[MAX_LOCAL_CHIP_NUM]; /* start address of one memory mapping table */
    uint32  size[MAX_LOCAL_CHIP_NUM];
    uint32  recover_cnt[MAX_LOCAL_CHIP_NUM];
};
typedef struct drv_ecc_mem_s drv_ecc_mem_t;

struct drv_scan_thread_s
{
    uint16      prio;
    sal_task_t* p_scan_task;
};
typedef struct drv_scan_thread_s drv_scan_thread_t;

/* memory mapping control */
struct drv_ecc_recover_master_s
{
    drv_ecc_mem_t          ecc_mem[DRV_ECC_MEM_NUM];
    uint32                 ignore_cnt[MAX_LOCAL_CHIP_NUM];
    uint32                 sbe_cnt[MAX_LOCAL_CHIP_NUM];
    uint32                 cache_mem_size;
    uint32                 cache_tcam_size;
    sal_mutex_t*           p_ecc_mutex;
    drv_ecc_event_fn       cb;

    uint32                 sbe_scan_en    :1;
    uint32                 tcam_scan_en   :1;
    uint32                 hw_learning_en :1;
    uint32                 ecc_recover_en :1;
    uint32                 rsv            :28;

    uint32                 tcam_scan_burst_interval;             /* unit is ms */
    uint32                 tcam_scan_burst_entry_num;            /* acl/scl/ip unit is 160bit, nat/pbr unit is 40bit */
    uint32                 scan_interval[DRV_ECC_SCAN_TYPE_NUM]; /* unit is ms, 0 means disable scan thread*/
    uint8                  scan_continue[DRV_ECC_SCAN_TYPE_NUM];

    drv_scan_thread_t      scan_thread[DRV_ECC_SCAN_TYPE_NUM];
};
typedef struct drv_ecc_recover_master_s drv_ecc_recover_master_t;

static drv_ecc_recover_master_t* g_gg_ecc_recover_master = NULL;

struct drv_ecc_sbe_cnt_s
{
    tbls_id_t tblid;
    char*     p_tbl_name;
    tbls_id_t rec;
    uint32    fld;
};
typedef struct drv_ecc_sbe_cnt_s drv_ecc_sbe_cnt_t;

struct drv_err_intr_fld_s
{
    uint8 fld_id;
    uint8 ecc_or_pariry;
    uint8 action;
};
typedef struct drv_err_intr_fld_s drv_err_intr_fld_t;

struct drv_err_intr_tbl_s
{
    tbls_id_t           tblid;
    uint8               num;
    drv_err_intr_fld_t* intr_fld;
};
typedef struct drv_err_intr_tbl_s drv_err_intr_tbl_t;

#define DRV_ERR_INTR_D(tbl_id, num) \
{0xFF, 0}};\
drv_err_intr_fld_t tbl_id##_intr_fld[] =\
{
#define DRV_ERR_INTR_F(tbl_id, fld_id, ecc_or_pariry, action) \
    {fld_id, ecc_or_pariry, action},

drv_err_intr_fld_t drv_err_intr_fld_1st[] = {
#include "goldengate/include/drv_ecc_db.h"
};

#undef DRV_ERR_INTR_D
#undef DRV_ERR_INTR_F

#define DRV_ERR_INTR_D(tbl_id, num) {tbl_id, num, tbl_id##_intr_fld},
#define DRV_ERR_INTR_F(tbl_id, fld_id, ecc_or_pariry, action)

drv_err_intr_tbl_t drv_err_intr_tbl[] =
{
#include "goldengate/include/drv_ecc_db.h"
};

static drv_ecc_sbe_cnt_t ecc_sbe_cnt[] =
{
    {BufRetrvBufPtrMem0_t,             NULL,                              BufRetrvParityStatus0_t,      BufRetrvParityStatus0_bufRetrvBufPtrMemSbeCnt_f},
    {BufRetrvBufPtrMem1_t,             NULL,                              BufRetrvParityStatus1_t,      BufRetrvParityStatus1_bufRetrvBufPtrMemSbeCnt_f},
    {BufRetrvMsgParkMem0_t,            NULL,                              BufRetrvParityStatus0_t,      BufRetrvParityStatus0_bufRetrvMsgParkMemSbeCnt_f},
    {BufRetrvMsgParkMem1_t,            NULL,                              BufRetrvParityStatus1_t,      BufRetrvParityStatus1_bufRetrvMsgParkMemSbeCnt_f},
    {BufRetrvPktMsgMem0_t,             NULL,                              BufRetrvParityStatus0_t,      BufRetrvParityStatus0_bufRetrvPktMsgMemSbeCnt_f},
    {BufRetrvPktMsgMem1_t,             NULL,                              BufRetrvParityStatus1_t,      BufRetrvParityStatus1_bufRetrvPktMsgMemSbeCnt_f},
    {MaxTblId_t,                       "MiscIntfDataMem0",                BufRetrvParityStatus0_t,      BufRetrvParityStatus0_miscIntfDataMemSbeCnt_f},
    {MaxTblId_t,                       "MiscIntfDataMem1",                BufRetrvParityStatus1_t,      BufRetrvParityStatus1_miscIntfDataMemSbeCnt_f},

    {BufStoreFreeListHRam_t,           NULL,                              BufStoreParityStatus_t,       BufStoreParityStatus_bufStoreFreeListHRamSbeCnt_f},
    {BufStoreFreeListLRam_t,           NULL,                              BufStoreParityStatus_t,       BufStoreParityStatus_bufStoreFreeListLRamSbeCnt_f},
    {MaxTblId_t,                       "BufStoreLinkListH",               BufStoreParityStatus_t,       BufStoreParityStatus_bufStoreLinkListHSbeCnt_f},
    {MaxTblId_t,                       "BufStoreLinkListL",               BufStoreParityStatus_t,       BufStoreParityStatus_bufStoreLinkListLSbeCnt_f},
    {MaxTblId_t,                       "IntfCmbDataMemSlice0",            BufStoreParityStatus_t,       BufStoreParityStatus_intfCmbDataMemSlice0SbeCnt_f},
    {MaxTblId_t,                       "IntfCmbDataMemSlice1",            BufStoreParityStatus_t,       BufStoreParityStatus_intfCmbDataMemSlice1SbeCnt_f},

    {DmaDescCache0_t,                  NULL,                              DmaCtlParityStatus0_t,        DmaCtlParityStatus0_dmaDescCacheSbeCnt_f},
    {DmaDescCache1_t,                  NULL,                              DmaCtlParityStatus1_t,        DmaCtlParityStatus1_dmaDescCacheSbeCnt_f},
    {DmaPktRxMem0_t,                   NULL,                              DmaCtlParityStatus0_t,        DmaCtlParityStatus0_dmaPktRxMemSbeCnt_f},
    {DmaPktRxMem1_t,                   NULL,                              DmaCtlParityStatus1_t,        DmaCtlParityStatus1_dmaPktRxMemSbeCnt_f},

    {DmaRegRdMem0_t,                   NULL,                              DmaCtlParityStatus0_t,        DmaCtlParityStatus0_dmaRegRdMemSbeCnt_f},
    {DmaRegRdMem1_t,                   NULL,                              DmaCtlParityStatus1_t,        DmaCtlParityStatus1_dmaRegRdMemSbeCnt_f},
    {DmaRegWrMem0_t,                   NULL,                              DmaCtlParityStatus0_t,        DmaCtlParityStatus0_dmaRegWrMemSbeCnt_f},
    {DmaRegWrMem1_t,                   NULL,                              DmaCtlParityStatus1_t,        DmaCtlParityStatus1_dmaRegWrMemSbeCnt_f},

    {DsAging_t,                        NULL,                              DsAgingParityStatus_t,        DsAgingParityStatus_dsAgingSbeCnt_f},
    {DsAgingStatusFib_t,               NULL,                              DsAgingParityStatus_t,        DsAgingParityStatus_dsAgingStatusFibSbeCnt_f},
    {DsAgingStatusTcam_t,              NULL,                              DsAgingParityStatus_t,        DsAgingParityStatus_dsAgingStatusTcamSbeCnt_f},

    {MaxTblId_t,                       "DsIpMacRam0Slice0",               DynamicDsAdParityStatus0_t,   DynamicDsAdParityStatus0_dsIpMacRam0SbeCnt_f},
    {MaxTblId_t,                       "DsIpMacRam0Slice1",               DynamicDsAdParityStatus1_t,   DynamicDsAdParityStatus1_dsIpMacRam0SbeCnt_f},

    {MaxTblId_t,                       "DsIpMacRam1Slice0",               DynamicDsAdParityStatus0_t,   DynamicDsAdParityStatus0_dsIpMacRam1SbeCnt_f},
    {MaxTblId_t,                       "DsIpMacRam1Slice1",               DynamicDsAdParityStatus1_t,   DynamicDsAdParityStatus1_dsIpMacRam1SbeCnt_f},

    {MaxTblId_t,                       "DsIpMacRam2Slice0",               DynamicDsAdParityStatus0_t,   DynamicDsAdParityStatus0_dsIpMacRam2SbeCnt_f},
    {MaxTblId_t,                       "DsIpMacRam2Slice1",               DynamicDsAdParityStatus1_t,   DynamicDsAdParityStatus1_dsIpMacRam2SbeCnt_f},

    {MaxTblId_t,                       "DsIpMacRam3Slice0",               DynamicDsAdParityStatus0_t,   DynamicDsAdParityStatus0_dsIpMacRam3SbeCnt_f},
    {MaxTblId_t,                       "DsIpMacRam3Slice1",               DynamicDsAdParityStatus1_t,   DynamicDsAdParityStatus1_dsIpMacRam3SbeCnt_f},

    {MaxTblId_t,                       "NextHopMetRam0Slice0",            DynamicDsAdParityStatus0_t,   DynamicDsAdParityStatus0_nextHopMetRam0SbeCnt_f},
    {MaxTblId_t,                       "NextHopMetRam0Slice1",            DynamicDsAdParityStatus1_t,   DynamicDsAdParityStatus1_nextHopMetRam0SbeCnt_f},

    {MaxTblId_t,                       "NextHopMetRam1Slice0",            DynamicDsAdParityStatus0_t,   DynamicDsAdParityStatus0_nextHopMetRam1SbeCnt_f},
    {MaxTblId_t,                       "NextHopMetRam1Slice1",            DynamicDsAdParityStatus1_t,   DynamicDsAdParityStatus1_nextHopMetRam1SbeCnt_f},

    {DsFwd_t,                          NULL,                              DynamicDsShareParityStatus_t, DynamicDsShareParityStatus_dsFwdSbeCnt_f},
    {MaxTblId_t,                       "DsL2L3EditRam0",                  DynamicDsShareParityStatus_t, DynamicDsShareParityStatus_dsL2L3EditRam0SbeCnt_f},
    {MaxTblId_t,                       "DsL2L3EditRam1",                  DynamicDsShareParityStatus_t, DynamicDsShareParityStatus_dsL2L3EditRam1SbeCnt_f},

    {DsVlanXlateDefault_t,             "DsVlanXlateDefaultSlice0",        EgrOamHashParityStatus0_t,    EgrOamHashParityStatus0_dsVlanXlateDefaultSbeCnt_f},
    {DsVlanXlateDefault_t,             "DsVlanXlateDefaultSlice1",        EgrOamHashParityStatus1_t,    EgrOamHashParityStatus1_dsVlanXlateDefaultSbeCnt_f},

    {MaxTblId_t,                       "EpeClaEopInfoFifoSlice0",         EpeAclOamParityStatus0_t,     EpeAclOamParityStatus0_epeClaEopInfoFifoSbeCnt_f},
    {MaxTblId_t,                       "EpeClaEopInfoFifoSlice1",         EpeAclOamParityStatus1_t,     EpeAclOamParityStatus1_epeClaEopInfoFifoSbeCnt_f},
    {MaxTblId_t,                       "EpeClaPolicingSopInfoRamSlice0",  EpeAclOamParityStatus0_t,     EpeAclOamParityStatus0_epeClaPolicingSopInfoRamSbeCnt_f},
    {MaxTblId_t,                       "EpeClaPolicingSopInfoRamSlice1",  EpeAclOamParityStatus1_t,     EpeAclOamParityStatus1_epeClaPolicingSopInfoRamSbeCnt_f},

    {MaxTblId_t,                       "EpeHdrEditChBufRamSlice0",        EpeHdrEditParityStatus0_t,    EpeHdrEditParityStatus0_epeHdrEditPktCtlFifoSbeCnt_f},
    {MaxTblId_t,                       "EpeHdrEditChBufRamSlice1",        EpeHdrEditParityStatus1_t,    EpeHdrEditParityStatus1_epeHdrEditPktCtlFifoSbeCnt_f},

    {DsEgressPortMac_t,                "DsEgressPortMacSlice0",           EpeHdrProcParityStatus0_t,    EpeHdrProcParityStatus0_dsEgressPortMacSbeCnt_f},
    {DsEgressPortMac_t,                "DsEgressPortMacSlice1",           EpeHdrProcParityStatus1_t,    EpeHdrProcParityStatus1_dsEgressPortMacSbeCnt_f},
    {DsEgressVsi_t,                    "DsEgressVsiSlice0",               EpeHdrProcParityStatus0_t,    EpeHdrProcParityStatus0_dsEgressVsiSbeCnt_f},
    {DsEgressVsi_t,                    "DsEgressVsiSlice1",               EpeHdrProcParityStatus1_t,    EpeHdrProcParityStatus1_dsEgressVsiSbeCnt_f},
    {DsIpv6NatPrefix_t,                "DsIpv6NatPrefixSlice0",           EpeHdrProcParityStatus0_t,    EpeHdrProcParityStatus0_dsIpv6NatPrefixSbeCnt_f},
    {DsIpv6NatPrefix_t,                "DsIpv6NatPrefixSlice1",           EpeHdrProcParityStatus1_t,    EpeHdrProcParityStatus1_dsIpv6NatPrefixSbeCnt_f},
    {DsL2Edit6WOuter_t,                "DsL2Edit6WOuterSlice0",           EpeHdrProcParityStatus0_t,    EpeHdrProcParityStatus0_dsL2Edit6WOuterSbeCnt_f},
    {DsL2Edit6WOuter_t,                "DsL2Edit6WOuterSlice1",           EpeHdrProcParityStatus1_t,    EpeHdrProcParityStatus1_dsL2Edit6WOuterSbeCnt_f},
    {DsL3Edit3W3rd_t,                  "DsL3Edit3W3rdSlice0",             EpeHdrProcParityStatus0_t,    EpeHdrProcParityStatus0_dsL3Edit3W3rdSbeCnt_f},
    {DsL3Edit3W3rd_t,                  "DsL3Edit3W3rdSlice1",             EpeHdrProcParityStatus1_t,    EpeHdrProcParityStatus1_dsL3Edit3W3rdSbeCnt_f},
    {DsLatencyMonCnt_t,                "DsLatencyMonCntSlice0",           EpeHdrProcParityStatus0_t,    EpeHdrProcParityStatus0_dsLatencyMonCntSbeCnt_f},
    {DsLatencyMonCnt_t,                "DsLatencyMonCntSlice1",           EpeHdrProcParityStatus1_t,    EpeHdrProcParityStatus1_dsLatencyMonCntSbeCnt_f},

    {DsLatencyWatermark_t,             "DsLatencyWatermarkSlice0",        EpeHdrProcParityStatus0_t,    EpeHdrProcParityStatus0_dsLatencyWatermarkSbeCnt_f},
    {DsLatencyWatermark_t,             "DsLatencyWatermarkSlice1",        EpeHdrProcParityStatus1_t,    EpeHdrProcParityStatus1_dsLatencyWatermarkSbeCnt_f},

    {DsPortLinkAgg_t,                  "DsPortLinkAggSlice0",             EpeHdrProcParityStatus0_t,    EpeHdrProcParityStatus0_dsPortLinkAggSbeCnt_f},
    {DsPortLinkAgg_t,                  "DsPortLinkAggSlice1",             EpeHdrProcParityStatus1_t,    EpeHdrProcParityStatus1_dsPortLinkAggSbeCnt_f},

    {DsDestInterface_t,                "DsDestInterfaceSlice0",           EpeNextHopParityStatus0_t,    EpeNextHopParityStatus0_dsDestInterfaceSbeCnt_f},
    {DsDestInterface_t,                "DsDestInterfaceSlice1",           EpeNextHopParityStatus1_t,    EpeNextHopParityStatus1_dsDestInterfaceSbeCnt_f},

    {DsDestPort_t,                     "DsDestPortSlice0",                EpeNextHopParityStatus0_t,    EpeNextHopParityStatus0_dsDestPortSbeCnt_f},
    {DsDestPort_t,                     "DsDestPortSlice1",                EpeNextHopParityStatus1_t,    EpeNextHopParityStatus1_dsDestPortSbeCnt_f},

    {DsEgressVlanRangeProfile_t,       "DsEgressVlanRangeProfileSlice0",  EpeNextHopParityStatus0_t,    EpeNextHopParityStatus0_dsEgressVlanRangeProfileSbeCnt_f},
    {DsEgressVlanRangeProfile_t,       "DsEgressVlanRangeProfileSlice1",  EpeNextHopParityStatus1_t,    EpeNextHopParityStatus1_dsEgressVlanRangeProfileSbeCnt_f},

    {DsPortIsolation_t,                "DsPortIsolationSlice0",           EpeNextHopParityStatus0_t,    EpeNextHopParityStatus0_dsPortIsolationSbeCnt_f},
    {DsPortIsolation_t,                "DsPortIsolationSlice0",           EpeNextHopParityStatus1_t,    EpeNextHopParityStatus1_dsPortIsolationSbeCnt_f},

    {DsVlanTagBitMap_t,                "DsVlanTagBitMapSlice0",           EpeNextHopParityStatus0_t,    EpeNextHopParityStatus0_dsVlanTagBitMapSbeCnt_f},
    {DsVlanTagBitMap_t,                "DsVlanTagBitMapSlice1",           EpeNextHopParityStatus1_t,    EpeNextHopParityStatus1_dsVlanTagBitMapSbeCnt_f},

    {EpeEditPriorityMap_t,             "EpeEditPriorityMapSlice0",        EpeNextHopParityStatus0_t,    EpeNextHopParityStatus0_epeEditPriorityMapSbeCnt_f},
    {EpeEditPriorityMap_t,             "EpeEditPriorityMapSlice1",        EpeNextHopParityStatus1_t,    EpeNextHopParityStatus1_epeEditPriorityMapSbeCnt_f},

    {DsMacLimitCount_t,                NULL,                              FibAccParityStatus_t,         FibAccParityStatus_dsMacLimitCountSbeCnt_f},
    {DsMacLimitThreshold_t,            NULL,                              FibAccParityStatus_t,         FibAccParityStatus_dsMacLimitThresholdSbeCnt_f},
    {MacLimitThreshold_t,              NULL,                              FibAccParityStatus_t,         FibAccParityStatus_macLimitThresholdSbeCnt_f},

    {DsIpfixEgrPortCfg_t,              "DsIpfixEgrPortCfg",               FlowAccParityStatus_t,        FlowAccParityStatus_dsIpfixEgrPortCfgSbeCnt_f},
    {DsIpfixEgrPortSamplingCount_t,    "DsIpfixEgrPortSamplingCount",     FlowAccParityStatus_t,        FlowAccParityStatus_dsIpfixEgrPortSamplingCountSbeCnt_f},
    {DsIpfixIngPortCfg_t,              "DsIpfixIngPortCfg",               FlowAccParityStatus_t,        FlowAccParityStatus_dsIpfixIngPortCfgSbeCnt_f},
    {DsIpfixIngPortSamplingCount_t,    "DsIpfixIngPortSamplingCount",     FlowAccParityStatus_t,        FlowAccParityStatus_dsIpfixIngPortSamplingCountSbeCnt_f},

    {MaxTblId_t,                       "FlowTcamAd0",                     FlowTcamParityStatus_t,       FlowTcamParityStatus_flowTcamAd0SbeCnt_f},
    {MaxTblId_t,                       "FlowTcamAd1",                     FlowTcamParityStatus_t,       FlowTcamParityStatus_flowTcamAd1SbeCnt_f},
    {MaxTblId_t,                       "FlowTcamAd2",                     FlowTcamParityStatus_t,       FlowTcamParityStatus_flowTcamAd2SbeCnt_f},
    {MaxTblId_t,                       "FlowTcamAd3",                     FlowTcamParityStatus_t,       FlowTcamParityStatus_flowTcamAd3SbeCnt_f},
    {MaxTblId_t,                       "FlowTcamAd4",                     FlowTcamParityStatus_t,       FlowTcamParityStatus_flowTcamAd4SbeCnt_f},
    {MaxTblId_t,                       "FlowTcamAd5",                     FlowTcamParityStatus_t,       FlowTcamParityStatus_flowTcamAd5SbeCnt_f},
    {MaxTblId_t,                       "FlowTcamAd6",                     FlowTcamParityStatus_t,       FlowTcamParityStatus_flowTcamAd6SbeCnt_f},

    {DsEgressPortLogStats_t,           "DsEgressPortLogStatsSlice0",      GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsEgressPortLogStatsSlice0SbeCnt_f},
    {DsEgressPortLogStats_t,           "DsEgressPortLogStatsSlice1",      GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsEgressPortLogStatsSlice1SbeCnt_f},
    {DsIngressPortLogStats_t,          "DsIngressPortLogStatsSlice0",     GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsIngressPortLogStatsSlice0SbeCnt_f},
    {DsIngressPortLogStats_t,          "DsIngressPortLogStatsSlice1",     GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsIngressPortLogStatsSlice1SbeCnt_f},
    {DsStats_t,                        NULL,                              GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsSbeCnt_f},

    {DsStatsRamCacheDequeue_t,         "DsStatsRamCacheDequeueSlice0",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheDequeueSlice0SbeCnt_f},
    {DsStatsRamCacheDequeue_t,         "DsStatsRamCacheDequeueSlice1",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheDequeueSlice1SbeCnt_f},

    {DsStatsRamCacheEnqueue_t,         "DsStatsRamCacheEnqueueSlice0",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheEnqueueSlice0SbeCnt_f},
    {DsStatsRamCacheEnqueue_t,         "DsStatsRamCacheEnqueueSlice1",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheEnqueueSlice1SbeCnt_f},

    {DsStatsRamCacheEpeAcl_t,          "DsStatsRamCacheEpeAclSlice0",     GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheEpeAclSlice0SbeCnt_f},
    {DsStatsRamCacheEpeAcl_t,          "DsStatsRamCacheEpeAclSlice1",     GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheEpeAclSlice1SbeCnt_f},
    {DsStatsRamCacheEpeFwd_t,          "DsStatsRamCacheEpeFwdSlice0",     GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheEpeFwdSlice0SbeCnt_f},
    {DsStatsRamCacheEpeFwd_t,          "DsStatsRamCacheEpeFwdSlice1",     GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheEpeFwdSlice1SbeCnt_f},
    {DsStatsRamCacheEpeIntf_t,         "DsStatsRamCacheEpeIntfSlice0",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheEpeIntfSlice0SbeCnt_f},
    {DsStatsRamCacheEpeIntf_t,         "DsStatsRamCacheEpeIntfSlice1",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheEpeIntfSlice1SbeCnt_f},
    {DsStatsRamCacheIpeAcl0_t,         "DsStatsRamCacheIpeAcl0Slice0",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheIpeAcl0Slice0SbeCnt_f},
    {DsStatsRamCacheIpeAcl0_t,         "DsStatsRamCacheIpeAcl0Slice1",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheIpeAcl0Slice1SbeCnt_f},
    {DsStatsRamCacheIpeAcl1_t,         "DsStatsRamCacheIpeAcl1Slice0",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheIpeAcl1Slice0SbeCnt_f},
    {DsStatsRamCacheIpeAcl1_t,         "DsStatsRamCacheIpeAcl1Slice1",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheIpeAcl1Slice1SbeCnt_f},
    {DsStatsRamCacheIpeAcl2_t,         "DsStatsRamCacheIpeAcl2Slice0",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheIpeAcl2Slice0SbeCnt_f},
    {DsStatsRamCacheIpeAcl2_t,         "DsStatsRamCacheIpeAcl2Slice1",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheIpeAcl2Slice1SbeCnt_f},
    {DsStatsRamCacheIpeAcl3_t,         "DsStatsRamCacheIpeAcl3Slice0",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheIpeAcl3Slice0SbeCnt_f},
    {DsStatsRamCacheIpeAcl3_t,         "DsStatsRamCacheIpeAcl3Slice1",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheIpeAcl3Slice1SbeCnt_f},
    {DsStatsRamCacheIpeFwd_t,          "DsStatsRamCacheIpeFwdSlice0",     GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheIpeFwdSlice0SbeCnt_f},
    {DsStatsRamCacheIpeFwd_t,          "DsStatsRamCacheIpeFwdSlice1",     GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheIpeFwdSlice1SbeCnt_f},
    {DsStatsRamCacheIpeIntf_t,         "DsStatsRamCacheIpeIntfSlice0",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheIpeIntfSlice0SbeCnt_f},
    {DsStatsRamCacheIpeIntf_t,         "DsStatsRamCacheIpeIntfSlice1",    GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCacheIpeIntfSlice1SbeCnt_f},
    {DsStatsRamCachePolicing_t,        "DsStatsRamCachePolicingSlice0",   GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCachePolicingSlice0SbeCnt_f},
    {DsStatsRamCachePolicing_t,        "DsStatsRamCachePolicingSlice1",   GlobalStatsParityStatus_t,    GlobalStatsParityStatus_dsStatsRamCachePolicingSlice1SbeCnt_f},

    {DsQcn_t,                          "DsQcnSlice0",                     IpeForwardParityStatus0_t,    IpeForwardParityStatus0_dsQcnSbeCnt_f},
    {DsQcn_t,                          "DsQcnSlice1",                     IpeForwardParityStatus1_t,    IpeForwardParityStatus1_dsQcnSbeCnt_f},

    {DsPhyPort_t,                      "DsPhyPortSlice0",                 IpeHdrAdjParityStatus0_t,     IpeHdrAdjParityStatus0_dsPhyPortSbeCnt_f},
    {DsPhyPort_t,                      "DsPhyPortSlice1",                 IpeHdrAdjParityStatus1_t,     IpeHdrAdjParityStatus1_dsPhyPortSbeCnt_f},

    {DsPhyPortExt_t,                   "DsPhyPortExtSlice0",              IpeIntfMapParityStatus0_t,    IpeIntfMapParityStatus0_dsPhyPortExtSbeCnt_f},
    {DsPhyPortExt_t,                   "DsPhyPortExtSlice1",              IpeIntfMapParityStatus1_t,    IpeIntfMapParityStatus1_dsPhyPortExtSbeCnt_f},
    {DsRouterMac_t,                    "DsRouterMacSlice0",               IpeIntfMapParityStatus0_t,    IpeIntfMapParityStatus0_dsRouterMacSbeCnt_f},
    {DsRouterMac_t,                    "DsRouterMacSlice1",               IpeIntfMapParityStatus1_t,    IpeIntfMapParityStatus1_dsRouterMacSbeCnt_f},
    {DsSrcInterface_t,                 "DsSrcInterfaceSlice0",            IpeIntfMapParityStatus0_t,    IpeIntfMapParityStatus0_dsSrcInterfaceSbeCnt_f},
    {DsSrcInterface_t,                 "DsSrcInterfaceSlice1",            IpeIntfMapParityStatus1_t,    IpeIntfMapParityStatus1_dsSrcInterfaceSbeCnt_f},
    {DsSrcPort_t,                      "DsSrcPortSlice0",                 IpeIntfMapParityStatus0_t,    IpeIntfMapParityStatus0_dsSrcPortSbeCnt_f},
    {DsSrcPort_t,                      "DsSrcPortSlice1",                 IpeIntfMapParityStatus1_t,    IpeIntfMapParityStatus1_dsSrcPortSbeCnt_f},
    {DsVlan2Ptr_t,                     "DsVlan2PtrSlice0",                IpeIntfMapParityStatus0_t,    IpeIntfMapParityStatus0_dsVlan2PtrSbeCnt_f},
    {DsVlan2Ptr_t,                     "DsVlan2PtrSlice1",                IpeIntfMapParityStatus1_t,    IpeIntfMapParityStatus1_dsVlan2PtrSbeCnt_f},
    {DsVlanActionProfile_t,            "DsVlanActionProfileSlice0",       IpeIntfMapParityStatus0_t,    IpeIntfMapParityStatus0_dsVlanActionProfileSbeCnt_f},
    {DsVlanActionProfile_t,            "DsVlanActionProfileSlice1",       IpeIntfMapParityStatus1_t,    IpeIntfMapParityStatus1_dsVlanActionProfileSbeCnt_f},
    {DsVlanRangeProfile_t,             "DsVlanRangeProfileSlice0",        IpeIntfMapParityStatus0_t,    IpeIntfMapParityStatus0_dsVlanRangeProfileSbeCnt_f},
    {DsVlanRangeProfile_t,             "DsVlanRangeProfileSlice1",        IpeIntfMapParityStatus1_t,    IpeIntfMapParityStatus1_dsVlanRangeProfileSbeCnt_f},

    {DsAclVlanActionProfile_t,         "DsAclVlanActionProfileSlice0",    IpeLkupMgrParityStatus0_t,    IpeLkupMgrParityStatus0_dsAclVlanActionProfileSbeCnt_f},
    {DsAclVlanActionProfile_t,         "DsAclVlanActionProfileSlice1",    IpeLkupMgrParityStatus1_t,    IpeLkupMgrParityStatus1_dsAclVlanActionProfileSbeCnt_f},
    {IpeClassificationDscpMap_t,       "IpeClassificationDscpMapSlice0",  IpeLkupMgrParityStatus0_t,    IpeLkupMgrParityStatus0_ipeClassificationDscpMapSbeCnt_f},
    {IpeClassificationDscpMap_t,       "IpeClassificationDscpMapSlice1",  IpeLkupMgrParityStatus1_t,    IpeLkupMgrParityStatus1_ipeClassificationDscpMapSbeCnt_f},

    {DsBidiPimGroup_t,                 "DsBidiPimGroupSlice0",            IpePktProcParityStatus0_t,    IpePktProcParityStatus0_dsBidiPimGroupSbeCnt_f},
    {DsBidiPimGroup_t,                 "DsBidiPimGroupSlice1",            IpePktProcParityStatus1_t,    IpePktProcParityStatus1_dsBidiPimGroupSbeCnt_f},
    {DsEcmpGroup_t,                    "DsEcmpGroupSlice0",               IpePktProcParityStatus0_t,    IpePktProcParityStatus0_dsEcmpGroupSbeCnt_f},
    {DsEcmpGroup_t,                    "DsEcmpGroupSlice1",               IpePktProcParityStatus1_t,    IpePktProcParityStatus1_dsEcmpGroupSbeCnt_f},

    {DsRpf_t,                          "DsRpfSlice0",                     IpePktProcParityStatus0_t,    IpePktProcParityStatus0_dsRpfSbeCnt_f},
    {DsRpf_t,                          "DsRpfSlice1",                     IpePktProcParityStatus1_t,    IpePktProcParityStatus1_dsRpfSbeCnt_f},

    {DsVrf_t,                          "DsVrfSlice0",                     IpePktProcParityStatus0_t,    IpePktProcParityStatus0_dsVrfSbeCnt_f},
    {DsVrf_t,                          "DsVrfSlice1",                     IpePktProcParityStatus1_t,    IpePktProcParityStatus1_dsVrfSbeCnt_f},

    {DsVsi_t,                          "DsVsiSlice0",                     IpePktProcParityStatus0_t,    IpePktProcParityStatus0_dsVsiSbeCnt_f},
    {DsVsi_t,                          "DsVsiSlice1",                     IpePktProcParityStatus1_t,    IpePktProcParityStatus1_dsVsiSbeCnt_f},

    {LpmTcamIpAdRam_t,                 NULL,                              LpmTcamIpParityStatus_t,      LpmTcamIpParityStatus_lpmTcamIpAdRamSbeCnt_f},

    {LpmTcamNatAdRam_t,                "LpmTcamNatAdSlice0",              LpmTcamNatParityStatus0_t,    LpmTcamNatParityStatus0_lpmTcamNatAdRamSbeCnt_f},
    {LpmTcamNatAdRam_t,                "LpmTcamNatAdSlice1",              LpmTcamNatParityStatus1_t,    LpmTcamNatParityStatus1_lpmTcamNatAdRamSbeCnt_f},

    {DsMetFifoExcp_t,                  NULL,                              MetFifoParityStatus_t,        MetFifoParityStatus_dsMetFifoExcpSbeCnt_f},
    {DsMetLinkAggregateBitmap_t,       NULL,                              MetFifoParityStatus_t,        MetFifoParityStatus_dsMetLinkAggregateBitmapSbeCnt_f},
    {DsMetLinkAggregatePort_t,         NULL,                              MetFifoParityStatus_t,        MetFifoParityStatus_dsMetLinkAggregatePortSbeCnt_f},
    {MetMsgMem_t,                      NULL,                              MetFifoParityStatus_t,        MetFifoParityStatus_metMsgMemSbeCnt_f},
    {MetRcdMem_t,                      NULL,                              MetFifoParityStatus_t,        MetFifoParityStatus_metRcdMemSbeCnt_f},

    {DsChannelizeIngFc0_t,             NULL,                              NetRxParityStatus0_t,         NetRxParityStatus0_dsChannelizeIngFcSbeCnt_f},
    {DsChannelizeIngFc1_t,             NULL,                              NetRxParityStatus1_t,         NetRxParityStatus1_dsChannelizeIngFcSbeCnt_f},

    {DsChannelizeMode0_t,              NULL,                              NetRxParityStatus0_t,         NetRxParityStatus0_dsChannelizeModeSbeCnt_f},
    {DsChannelizeMode1_t,              NULL,                              NetRxParityStatus1_t,         NetRxParityStatus1_dsChannelizeModeSbeCnt_f},

    {NetTxDsIbLppInfo0_t,              NULL,                              NetTxParityStatus0_t,         NetTxParityStatus0_netTxDsIbLppInfoSbeCnt_f},
    {NetTxDsIbLppInfo1_t,              NULL,                              NetTxParityStatus1_t,         NetTxParityStatus1_netTxDsIbLppInfoSbeCnt_f},

    {DsEthLmProfile_t,                 "DsEthLmProfileSlice0",            PktProcDsParityStatus0_t,     PktProcDsParityStatus0_dsEthLmProfileSbeCnt_f},
    {DsEthLmProfile_t,                 "DsEthLmProfileSlice1",            PktProcDsParityStatus1_t,     PktProcDsParityStatus1_dsEthLmProfileSbeCnt_f},

    {DsStpState_t,                     "DsStpStateSlice0",                PktProcDsParityStatus0_t,     PktProcDsParityStatus0_dsStpStateSbeCnt_f},
    {DsStpState_t,                     "DsStpStateSlice1",                PktProcDsParityStatus1_t,     PktProcDsParityStatus1_dsStpStateSbeCnt_f},

    {DsVlan_t,                         "DsVlanSlice0",                    PktProcDsParityStatus0_t,     PktProcDsParityStatus0_dsVlanSbeCnt_f},
    {DsVlan_t,                         "DsVlanSlice1",                    PktProcDsParityStatus1_t,     PktProcDsParityStatus1_dsVlanSbeCnt_f},

    {DsVlanProfile_t,                  "DsVlanProfileSlice0",             PktProcDsParityStatus0_t,     PktProcDsParityStatus0_dsVlanProfileSbeCnt_f},
    {DsVlanProfile_t,                  "DsVlanProfileSlice1",             PktProcDsParityStatus1_t,     PktProcDsParityStatus1_dsVlanProfileSbeCnt_f},

    {DsVlanStatus_t,                   "DsVlanStatusSlice0",              PktProcDsParityStatus0_t,     PktProcDsParityStatus0_dsVlanStatusSbeCnt_f},
    {DsVlanStatus_t,                   "DsVlanStatusSlice1",              PktProcDsParityStatus1_t,     PktProcDsParityStatus1_dsVlanStatusSbeCnt_f},

    {DsPolicerControl_t,               NULL,                              PolicingParityStatus_t,       PolicingParityStatus_dsPolicerControlSbeCnt_f},
    {DsPolicerCountCommit_t,           NULL,                              PolicingParityStatus_t,       PolicingParityStatus_dsPolicerCountCommitSbeCnt_f},
    {DsPolicerCountExcess_t,           NULL,                              PolicingParityStatus_t,       PolicingParityStatus_dsPolicerCountExcessSbeCnt_f},

    {DsChanShpProfile_t,               "DsChanShpProfileSlice0",          QMgrDeqSliceParityStatus0_t,  QMgrDeqSliceParityStatus0_dsChanShpProfileSbeCnt_f},
    {DsChanShpProfile_t,               "DsChanShpProfileSlice1",          QMgrDeqSliceParityStatus1_t,  QMgrDeqSliceParityStatus1_dsChanShpProfileSbeCnt_f},

    {DsGrpShpProfile_t,                "DsGrpShpProfileSlice0",           QMgrDeqSliceParityStatus0_t,  QMgrDeqSliceParityStatus0_dsGrpShpProfileSbeCnt_f},
    {DsGrpShpProfile_t,                "DsGrpShpProfileSlice1",           QMgrDeqSliceParityStatus1_t,  QMgrDeqSliceParityStatus1_dsGrpShpProfileSbeCnt_f},

    {DsQueShpProfId_t,                 "DsQueShpProfIdSlice0",            QMgrDeqSliceParityStatus0_t,  QMgrDeqSliceParityStatus0_dsQueShpProfIdSbeCnt_f},
    {DsQueShpProfId_t,                 "DsQueShpProfIdSlice1",            QMgrDeqSliceParityStatus1_t,  QMgrDeqSliceParityStatus1_dsQueShpProfIdSbeCnt_f},

    {DsQueShpProfile_t,                "DsQueShpProfileSlice0",           QMgrDeqSliceParityStatus0_t,  QMgrDeqSliceParityStatus0_dsQueShpProfileSbeCnt_f},
    {DsQueShpProfile_t,                "DsQueShpProfileSlice1",           QMgrDeqSliceParityStatus1_t,  QMgrDeqSliceParityStatus1_dsQueShpProfileSbeCnt_f},

    {DsEgrResrcCtl_t,                  NULL,                              QMgrEnqParityStatus_t,        QMgrEnqParityStatus_dsEgrResrcCtlSbeCnt_f},
    {DsLinkAggregateChannelMember_t,   NULL,                              QMgrEnqParityStatus_t,        QMgrEnqParityStatus_dsLinkAggregateChannelMemberSetSbeCnt_f},
    {DsLinkAggregateMemberSet_t,       NULL,                              QMgrEnqParityStatus_t,        QMgrEnqParityStatus_dsLinkAggregateMemberSetSbeCnt_f},
    {DsLinkAggregationPort_t,          NULL,                              QMgrEnqParityStatus_t,        QMgrEnqParityStatus_dsLinkAggregationPortSbeCnt_f},
    {DsQueThrdProfile_t,               NULL,                              QMgrEnqParityStatus_t,        QMgrEnqParityStatus_dsQueThrdProfileSbeCnt_f},
    {DsQueueNumGenCtl_t,               NULL,                              QMgrEnqParityStatus_t,        QMgrEnqParityStatus_dsQueueNumGenCtlSbeCnt_f},
    {DsQueueSelectMap_t,               NULL,                              QMgrEnqParityStatus_t,        QMgrEnqParityStatus_dsQueueSelectMapSbeCnt_f},

    {DsMsgLinkTail0_t,                 NULL,                              QMgrMsgStoreParityStatus_t,   QMgrMsgStoreParityStatus_dsMsgLinkTail0SbeCnt_f},
    {DsMsgUsedList_t,                  NULL,                              QMgrMsgStoreParityStatus_t,   QMgrMsgStoreParityStatus_dsMsgUsedList0SbeCnt_f},
    {DsMsgUsedList_t,                  NULL,                              QMgrMsgStoreParityStatus_t,   QMgrMsgStoreParityStatus_dsMsgUsedList1SbeCnt_f},

    {DsElephantFlowDetect0_t,          NULL,                              ShareEfdParityStatus_t,       ShareEfdParityStatus_dsElephantFlowDetect0SbeCnt_f},
    {DsElephantFlowDetect1_t,          NULL,                              ShareEfdParityStatus_t,       ShareEfdParityStatus_dsElephantFlowDetect1SbeCnt_f},
    {DsElephantFlowDetect2_t,          NULL,                              ShareEfdParityStatus_t,       ShareEfdParityStatus_dsElephantFlowDetect2SbeCnt_f},
    {DsElephantFlowDetect3_t,          NULL,                              ShareEfdParityStatus_t,       ShareEfdParityStatus_dsElephantFlowDetect3SbeCnt_f},
    {DsElephantFlowState_t,            NULL,                              ShareEfdParityStatus_t,       ShareEfdParityStatus_dsElephantFlowStateSbeCnt_f},

    {DsPortStormCount_t,               NULL,                              ShareStormCtlParityStatus_t,  ShareStormCtlParityStatus_dsPortStormCountSbeCnt_f},
    {DsVlanStormCount_t,               NULL,                              ShareStormCtlParityStatus_t,  ShareStormCtlParityStatus_dsVlanStormCountSbeCnt_f},
    {DsPortStormCtl_t,                 NULL,                              ShareStormCtlParityStatus_t,  ShareStormCtlParityStatus_dsVlanStormCtlSbeCnt_f},

    {AutoGenPktPktHdr_t,               NULL,                              OamAutoGenPktParityStatus_t,  OamAutoGenPktParityStatus_autoGenPktPktHdrSbeCnt_f},

    {MaxTblId_t,                       NULL,                              MaxTblId_t,                   0}
};

static tbls_id_t drv_tcam_scan_tbl_inf[DRV_ECC_TCAM_BLOCK_TYPE_NUM][4] =
{
    {DsAclQosL3Key160Ingr0_t,  DsAclQosL3Key320Ingr0_t, DsAclQosIpv6Key640Ingr0_t, MaxTblId_t},
    {DsAclQosL3Key160Ingr1_t,  DsAclQosL3Key320Ingr1_t, DsAclQosIpv6Key640Ingr1_t, MaxTblId_t},
    {DsAclQosL3Key160Ingr2_t,  DsAclQosL3Key320Ingr2_t, DsAclQosIpv6Key640Ingr2_t, MaxTblId_t},
    {DsAclQosL3Key160Ingr3_t,  DsAclQosL3Key320Ingr3_t, DsAclQosIpv6Key640Ingr3_t, MaxTblId_t},
    {DsAclQosMacKey160Egr0_t,  DsAclQosL3Key320Egr0_t,  DsAclQosIpv6Key640Egr0_t, MaxTblId_t},
    {DsUserId0MacKey160_t,     DsUserId0L3Key320_t,     DsUserId0Ipv6Key640_t, MaxTblId_t},
    {DsUserId1MacKey160_t,     DsUserId1L3Key320_t,     DsUserId1Ipv6Key640_t, MaxTblId_t},
    {DsLpmTcamIpv440Key_t,     DsLpmTcamIpv6160Key0_t,   MaxTblId_t},
    {DsLpmTcamIpv4NAT160Key_t, DsLpmTcamIpv4Pbr160Key_t, DsLpmTcamIpv6160Key1_t,  DsLpmTcamIpv6320Key_t}
};

static char* ecc_mem_desc[] = {
    /* Normal interrupt */
    /* BufStoreInterruptNormal */
    "DsIgrPortTcPriMap",
    "DsIgrCondDisProfId",
    "DsIgrPortTcMinProfId",
    "DsIgrPortTcThrdProfId",
    "DsIgrPortTcThrdProfile",
    "DsIgrPortThrdProfile",
    "DsIgrPriToTcMap",

    /* EgrOamHashInterruptNormal */
    "DsVlanXlateDefault",

    /* EpeHdrEditInterruptNormal */
    "DsPacketHeaderEditTunnel",

    /* EpeHdrProcInterruptNormal */
    "DsEgressPortMac",
    "DsL3Edit3W3rd",
    "DsPortLinkAgg",
    "DsEgressVsi",
    "DsIpv6NatPrefix",
    "DsL2Edit6WOuter",

    /* FibAccInterruptNormal */
    "MacLimitThreshold",
    "DsMacLimitThreshold",

    /* FlowAccInterruptNormal */
    "DsIpfixEgrPortCfg",
    "DsIpfixIngPortCfg",

    /* IpeForwardInterruptNormal */
    "DsQcn",
    "DsSrcChannel",

    /* IpeHdrAdjInterruptNormal */
    "DsPhyPort",

    /* IpeIntfMapInterruptNormal */
    "DsPhyPortExt",
    "DsRouterMac",
    "DsSrcInterface",
    "DsSrcPort",
    "DsVlanActionProfile",
    "DsVlanRangeProfile",
    "DsVlan2Ptr",

    /* IpeLkupMgrInterruptNormal */
    "IpeClassificationDscpMap",
    "DsAclVlanActionProfile",

    /* IpePktProcInterruptNormal */
    "DsBidiPimGroup",
    "DsEcmpGroup",
    "DsEcmpMember",
    "DsVrf",
    "DsVsi",

    /* MetFifoInterruptNormal */
    "DsMetLinkAggregateBitmap",
    "DsMetLinkAggregatePort",
    "DsMetFifoExcp",

    /* NetRxInterruptNormal */
    "ChannelizeIngFc0",
    "ChannelizeIngFc1",

    "ChannelizeMode0",
    "ChannelizeMode1",

    /* PktProcDsInterruptNormal */
    "DsStpState",
    "DsVlanStatus",
    "DsVlan",
    "DsVlanProfile",
    "DsEthLmProfile",

    /* PolicingInterruptNormal */
    "DsPolicerControl",
    "DsPolicerProfile",

    /* QMgrDeqSliceInterruptNormal */
    "DsChanShpProfile",
    "DsQueShpProfId",
    "DsQueShpProfile",
    "DsGrpShpProfile",
    "DsSchServiceProfile",

    /* QMgrEnqInterruptNormal */
    "DsEgrResrcCtl",
    "DsLinkAggregateMember",
    "DsLinkAggregateMemberSet",
    "DsLinkAggregationPort",
    "DsQueueNumGenCtl",
    "DsQueueSelectMap",
    "DsQueThrdProfile",
    "DsLinkAggregateChannelMemberSet",

    /* Fatal interrupt */
    /* DsAgingInterruptFatal */
    "DsAging",

    /* DynamicDsShareInterruptFatal */
    "DsFwd",

    /* EpeNextHopInterruptFatal */
    "DsDestInterface",
    "DsEgressVlanRangeProfile",
    "EpeEditPriorityMap",
    "DsPortIsolation",
    "DsDestPort",
    "DsVlanTagBitMap",

    /* ShareStormCtlInterruptFatal */
    "VlanStormCtl",
    "PortStormCtl",

    /* OamProcInterruptNormal */
    "BfdV6Addr",
    "PortProperty",

    /* OamAutoGenPktInterruptFatal */
    "AutoGenPktPktHdr",

    /* DynamicDsHashInterruptFatal */
    "SharedRam0",
    "SharedRam1",
    "SharedRam2",
    "SharedRam3",
    "SharedRam4",
    "SharedRam5",
    "SharedRam6",

    /* DynamicDsAdInterruptFatal */
    "IpMacRam0",
    "IpMacRam1",
    "IpMacRam2",
    "IpMacRam3",

    /* DynamicDsHashInterruptFatal */
    "UserIdHashRam0",
    "UserIdHashRam1",
    "UserIdAdRam",

    /* DynamicDsShareInterruptFatal */
    "L2L3EditRam0",
    "L2L3EditRam1",

    /* DynamicDsAdInterruptFatal */
    "NextHopMetRam0",
    "NextHopMetRam1",

    "TcamKey0",
    "TcamKey1",
    "TcamKey2",
    "TcamKey3",
    "TcamKey4",
    "TcamKey5",
    "TcamKey6",
    "TcamKey7",
    "TcamKey8",

    /* FlowTcamInterruptNormal */
    "FlowTcamAd0",
    "FlowTcamAd1",
    "FlowTcamAd2",
    "FlowTcamAd3",
    "FlowTcamAd4",
    "FlowTcamAd5",
    "FlowTcamAd6",

    /* LpmTcamIpInterruptNormal */
    "LpmTcamIpAdRam",

    /* LpmTcamNatInterruptNormal */
    "LpmTcamNatAdRam",

    NULL
};

static uint32 dynamic_ram_entry_num[] = {
    DRV_SHARE_RAM0_MAX_ENTRY_NUM,
    DRV_SHARE_RAM1_MAX_ENTRY_NUM,
    DRV_SHARE_RAM2_MAX_ENTRY_NUM,
    DRV_SHARE_RAM3_MAX_ENTRY_NUM,
    DRV_SHARE_RAM4_MAX_ENTRY_NUM,
    DRV_SHARE_RAM5_MAX_ENTRY_NUM,
    DRV_SHARE_RAM6_MAX_ENTRY_NUM,

    DRV_DS_IPMAC_RAM0_MAX_ENTRY_NUM,
    DRV_DS_IPMAC_RAM1_MAX_ENTRY_NUM,
    DRV_DS_IPMAC_RAM2_MAX_ENTRY_NUM,
    DRV_DS_IPMAC_RAM3_MAX_ENTRY_NUM,

    DRV_USERIDHASHKEY_RAM0_MAX_ENTRY_NUM,
    DRV_USERIDHASHKEY_RAM1_MAX_ENTRY_NUM,

    DRV_USERIDHASHAD_RAM_MAX_ENTRY_NUM,

    DRV_L23EDITRAM0_MAX_ENTRY_NUM,
    DRV_L23EDITRAM1_MAX_ENTRY_NUM,

    DRV_NEXTHOPMET_RAM0_MAX_ENTRY_NUM,
    DRV_NEXTHOPMET_RAM1_MAX_ENTRY_NUM,

    DRV_TCAM_KEY0_MAX_ENTRY_NUM,
    DRV_TCAM_KEY1_MAX_ENTRY_NUM,
    DRV_TCAM_KEY2_MAX_ENTRY_NUM,
    DRV_TCAM_KEY3_MAX_ENTRY_NUM,
    DRV_TCAM_KEY4_MAX_ENTRY_NUM,
    DRV_TCAM_KEY5_MAX_ENTRY_NUM,
    DRV_TCAM_KEY6_MAX_ENTRY_NUM,

    DRV_LPM_TCAM_KEY0_MAX_ENTRY_NUM,
    DRV_LPM_TCAM_KEY1_MAX_ENTRY_NUM,

    DRV_TCAM_AD0_MAX_ENTRY_NUM,
    DRV_TCAM_AD1_MAX_ENTRY_NUM,
    DRV_TCAM_AD2_MAX_ENTRY_NUM,
    DRV_TCAM_AD3_MAX_ENTRY_NUM,
    DRV_TCAM_AD4_MAX_ENTRY_NUM,
    DRV_TCAM_AD5_MAX_ENTRY_NUM,
    DRV_TCAM_AD6_MAX_ENTRY_NUM,

    DRV_LPM_TCAM_AD0_MAX_ENTRY_NUM,
    DRV_LPM_TCAM_AD1_MAX_ENTRY_NUM
};

static uint32 tcam_ad_base[] = {DRV_TCAM_AD0_BASE_4W, DRV_TCAM_AD1_BASE_4W,
                                DRV_TCAM_AD2_BASE_4W, DRV_TCAM_AD3_BASE_4W,
                                DRV_TCAM_AD4_BASE_4W, DRV_TCAM_AD5_BASE_4W,
                                DRV_TCAM_AD6_BASE_4W,
                                DRV_LPM_TCAM_AD_ASIC_BASE0,
                                DRV_LPM_TCAM_AD_ASIC_BASE1};

struct drv_ecc_cb_info_s
{
   uint8  type;
   uint32 tbl_id;
   uint32 tbl_index;
   uint8  action;
   uint8  recover;
};
typedef struct drv_ecc_cb_info_s drv_ecc_cb_info_t;

STATIC void
_drv_goldengate_ecc_recover_error_info(uint8 lchip, drv_ecc_cb_info_t* p_ecc_cb_info)
{
    uint8 chip_id = 0;

    chip_id = lchip + drv_gg_init_chip_info.drv_init_chipid_base;

    if (NULL != g_gg_ecc_recover_master->cb)
    {
        g_gg_ecc_recover_master->cb(chip_id, p_ecc_cb_info);
    }

    return;
}

#define ___ECC_RECOVER_MAPPING___

STATIC void
_drv_goldengate_ecc_recover_mem2tbl(drv_ecc_mem_type_t mem_type, tbls_id_t* p_tblid)
{
    switch (mem_type)
    {
        /* Normal interrupt */
        /* BufStoreInterruptNormal */
        case DRV_ECC_MEM_IgrPortTcPriMap:
            *p_tblid = DsIgrPortTcPriMap_t;
            break;
        case DRV_ECC_MEM_IgrCondDisProfId:
            *p_tblid = DsIgrCondDisProfId_t;
            break;
        case DRV_ECC_MEM_IgrPortTcMinProfId:
            *p_tblid = DsIgrPortTcMinProfId_t;
            break;
        case DRV_ECC_MEM_IgrPortTcThrdProfId:
            *p_tblid = DsIgrPortTcThrdProfId_t;
            break;
        case DRV_ECC_MEM_IgrPortTcThrdProfile:
            *p_tblid = DsIgrPortTcThrdProfile_t;
            break;
        case DRV_ECC_MEM_IgrPortThrdProfile:
            *p_tblid = DsIgrPortThrdProfile_t;
            break;
        case DRV_ECC_MEM_IgrPriToTcMap:
            *p_tblid = DsIgrPriToTcMap_t;
            break;

        /* EgrOamHashInterruptNormal */
        case DRV_ECC_MEM_VlanXlateDefault:
            *p_tblid = DsVlanXlateDefault_t;
            break;

        /* EpeHdrEditInterruptNormal */
        case DRV_ECC_MEM_PacketHeaderEditTunnel:
            *p_tblid = DsPacketHeaderEditTunnel_t;
            break;

        /* EpeHdrProcInterruptNormal */
        case DRV_ECC_MEM_EgressPortMac:
            *p_tblid = DsEgressPortMac_t;
            break;
        case DRV_ECC_MEM_L3Edit3W3rd:
            *p_tblid = DsL3Edit3W3rd_t;
            break;
        case DRV_ECC_MEM_PortLinkAgg:
            *p_tblid = DsPortLinkAgg_t;
            break;
        case DRV_ECC_MEM_EgressVsi:
            *p_tblid = DsEgressVsi_t;
            break;
        case DRV_ECC_MEM_Ipv6NatPrefix:
            *p_tblid = DsIpv6NatPrefix_t;
            break;
        case DRV_ECC_MEM_L2Edit6WOuter:
            *p_tblid = DsL2Edit6WOuter_t;
            break;

        /* FibAccInterruptNormal */
        case DRV_ECC_MEM_MacLimitThresholdCnt:
            *p_tblid = MacLimitThreshold_t;
            break;

        case DRV_ECC_MEM_MacLimitThresholdIdx:
            *p_tblid = DsMacLimitThreshold_t;
            break;

        /* FlowAccInterruptNormal */
        case DRV_ECC_MEM_IpfixEgrPortCfg:
            *p_tblid = DsIpfixEgrPortCfg_t;
            break;

        case DRV_ECC_MEM_IpfixIngPortCfg:
            *p_tblid = DsIpfixIngPortCfg_t;
            break;

        /* IpeForwardInterruptNormal */
        case DRV_ECC_MEM_Qcn:
            *p_tblid = DsQcn_t;
            break;

        case DRV_ECC_MEM_SrcChannel:
            *p_tblid = DsSrcChannel_t;
            break;

        /* IpeHdrAdjInterruptNormal */
        case DRV_ECC_MEM_PhyPort:
            *p_tblid = DsPhyPort_t;
            break;

        /* IpeIntfMapInterruptNormal */
        case DRV_ECC_MEM_PhyPortExt:
            *p_tblid = DsPhyPortExt_t;
            break;

        case DRV_ECC_MEM_RouterMac:
            *p_tblid = DsRouterMac_t;
            break;

        case DRV_ECC_MEM_SrcInterface:
            *p_tblid = DsSrcInterface_t;
            break;

        case DRV_ECC_MEM_SrcPort:
            *p_tblid = DsSrcPort_t;
            break;

        case DRV_ECC_MEM_VlanActionProfile:
            *p_tblid = DsVlanActionProfile_t;
            break;

        case DRV_ECC_MEM_VlanRangeProfile:
            *p_tblid = DsVlanRangeProfile_t;
            break;

        case DRV_ECC_MEM_Vlan2Ptr:
            *p_tblid = DsVlan2Ptr_t;
            break;

        /* IpeLkupMgrInterruptNormal */
        case DRV_ECC_MEM_IpeClassificationDscpMap:
            *p_tblid = IpeClassificationDscpMap_t;
            break;

        case DRV_ECC_MEM_AclVlanActionProfile:
            *p_tblid = DsAclVlanActionProfile_t;
            break;

        /* IpePktProcInterruptNormal */
        case DRV_ECC_MEM_BidiPimGroup:
            *p_tblid = DsBidiPimGroup_t;
            break;

        case DRV_ECC_MEM_EcmpGroup:
            *p_tblid = DsEcmpGroup_t;
            break;

        case DRV_ECC_MEM_EcmpMember:
            *p_tblid = DsEcmpMember_t;
            break;

        case DRV_ECC_MEM_Vrf:
            *p_tblid = DsVrf_t;
            break;

        case DRV_ECC_MEM_Vsi:
            *p_tblid = DsVsi_t;
            break;

        /* MetFifoInterruptNormal */
        case DRV_ECC_MEM_MetLinkAggregateBitmap:
            *p_tblid = DsMetLinkAggregateBitmap_t;
            break;

        case DRV_ECC_MEM_MetLinkAggregatePort:
            *p_tblid = DsMetLinkAggregatePort_t;
            break;

        case DRV_ECC_MEM_MetFifoExcp:
            *p_tblid = DsMetFifoExcp_t;
            break;

        /* NetRxInterruptNormal */
        case DRV_ECC_MEM_ChannelizeIngFc0:
            *p_tblid = DsChannelizeIngFc0_t;
            break;

        case DRV_ECC_MEM_ChannelizeIngFc1:
            *p_tblid = DsChannelizeIngFc1_t;
            break;

        case DRV_ECC_MEM_ChannelizeMode0:
            *p_tblid = DsChannelizeMode0_t;
            break;

        case DRV_ECC_MEM_ChannelizeMode1:
            *p_tblid = DsChannelizeMode1_t;
            break;

        /* PktProcDsInterruptNormal */
        case DRV_ECC_MEM_StpState:
            *p_tblid = DsStpState_t;
            break;

        case DRV_ECC_MEM_VlanStatus:
            *p_tblid = DsVlanStatus_t;
            break;

        case DRV_ECC_MEM_Vlan:
            *p_tblid = DsVlan_t;
            break;

        case DRV_ECC_MEM_VlanProfile:
            *p_tblid = DsVlanProfile_t;
            break;

        case DRV_ECC_MEM_EthLmProfile:
            *p_tblid = DsEthLmProfile_t;
            break;

        /* PolicingInterruptNormal */
        case DRV_ECC_MEM_PolicerControl:
            *p_tblid = DsPolicerControl_t;
            break;

        case DRV_ECC_MEM_PolicerProfile:
            *p_tblid = DsPolicerProfile_t;
            break;

        /* QMgrDeqSliceInterruptNormal */
        case DRV_ECC_MEM_ChanShpProfile:
            *p_tblid = DsChanShpProfile_t;
            break;

        case DRV_ECC_MEM_QueShpProfId:
            *p_tblid = DsQueShpProfId_t;
            break;

        case DRV_ECC_MEM_QueShpProfile:
            *p_tblid = DsQueShpProfile_t;
            break;

        case DRV_ECC_MEM_GrpShpProfile:
            *p_tblid = DsGrpShpProfile_t;
            break;

        case DRV_ECC_MEM_SchServiceProfile:
            *p_tblid = DsSchServiceProfile_t;
            break;

        /* QMgrEnqInterruptNormal */
        case DRV_ECC_MEM_EgrResrcCtl:
            *p_tblid = DsEgrResrcCtl_t;
            break;

        case DRV_ECC_MEM_LinkAggregateMember:
            *p_tblid = DsLinkAggregateMember_t;
            break;

        case DRV_ECC_MEM_LinkAggregateMemberSet:
            *p_tblid = DsLinkAggregateMemberSet_t;
            break;

        case DRV_ECC_MEM_LinkAggregationPort:
            *p_tblid = DsLinkAggregationPort_t;
            break;

        case DRV_ECC_MEM_QueueNumGenCtl:
            *p_tblid = DsQueueNumGenCtl_t;
            break;

        case DRV_ECC_MEM_QueueSelectMap:
            *p_tblid = DsQueueSelectMap_t;
            break;

        case DRV_ECC_MEM_QueThrdProfile:
            *p_tblid = DsQueThrdProfile_t;
            break;

        case DRV_ECC_MEM_LinkAggregateChannelMemberSet:
            *p_tblid = DsLinkAggregateChannelMemberSet_t;
            break;

        /* Fatal interrupt */
        /* DsAgingInterruptFatal */
        case DRV_ECC_MEM_Aging:
            *p_tblid = DsAging_t;
            break;

        /* DynamicDsShareInterruptFatal */
        case DRV_ECC_MEM_Fwd:
            *p_tblid = DsFwd_t;
            break;

        /* EpeNextHopInterruptFatal */
        case DRV_ECC_MEM_DestInterface:
            *p_tblid = DsDestInterface_t;
            break;

        case DRV_ECC_MEM_EgressVlanRangeProfile:
            *p_tblid = DsEgressVlanRangeProfile_t;
            break;

        case DRV_ECC_MEM_EpeEditPriorityMap:
            *p_tblid = EpeEditPriorityMap_t;
            break;

        case DRV_ECC_MEM_PortIsolation:
            *p_tblid = DsPortIsolation_t;
            break;

        case DRV_ECC_MEM_DestPort:
            *p_tblid = DsDestPort_t;
            break;

        case DRV_ECC_MEM_VlanTagBitMap:
            *p_tblid = DsVlanTagBitMap_t;
            break;

        /* ShareStormCtlInterruptFatal */
        case DRV_ECC_MEM_VlanStormCtl:
            *p_tblid = DsVlanStormCtl_t;
            break;

        case DRV_ECC_MEM_PortStormCtl:
            *p_tblid = DsPortStormCtl_t;
            break;

        case DRV_ECC_MEM_BfdV6Addr:
            *p_tblid = DsBfdV6Addr_t;
            break;

        case DRV_ECC_MEM_PortProperty:
            *p_tblid = DsPortProperty_t;
            break;

        case DRV_ECC_MEM_AutoGenPktPktHdr:
            *p_tblid = AutoGenPktPktHdr_t;
            break;

        /* DynamicDsHashInterruptFatal */
        case DRV_ECC_MEM_SharedRam0:
        case DRV_ECC_MEM_SharedRam1:
        case DRV_ECC_MEM_SharedRam2:
        case DRV_ECC_MEM_SharedRam3:
        case DRV_ECC_MEM_SharedRam4:
        case DRV_ECC_MEM_SharedRam5:
        case DRV_ECC_MEM_SharedRam6:

        /* No ecc error intrrrupt */
        case DRV_ECC_MEM_TCAM_KEY0:
        case DRV_ECC_MEM_TCAM_KEY1:
        case DRV_ECC_MEM_TCAM_KEY2:
        case DRV_ECC_MEM_TCAM_KEY3:
        case DRV_ECC_MEM_TCAM_KEY4:
        case DRV_ECC_MEM_TCAM_KEY5:
        case DRV_ECC_MEM_TCAM_KEY6:

        case DRV_ECC_MEM_TCAM_KEY7:
        case DRV_ECC_MEM_TCAM_KEY8:

        /* DynamicDsAdInterruptFatal */
        case DRV_ECC_MEM_IpMacRam0:
        case DRV_ECC_MEM_IpMacRam1:
        case DRV_ECC_MEM_IpMacRam2:
        case DRV_ECC_MEM_IpMacRam3:

        /* DynamicDsHashInterruptFatal */
        case DRV_ECC_MEM_UserIdHashRam0:
        case DRV_ECC_MEM_UserIdHashRam1:
        case DRV_ECC_MEM_UserIdAdRam:

        /* DynamicDsShareInterruptFatal */
        case DRV_ECC_MEM_L2L3EditRam0:
        case DRV_ECC_MEM_L2L3EditRam1:

        /* DynamicDsAdInterruptFatal */
        case DRV_ECC_MEM_NextHopMetRam0:
        case DRV_ECC_MEM_NextHopMetRam1:

        /* FlowTcamInterruptNormal */
        case DRV_ECC_MEM_FlowTcamAd0:
        case DRV_ECC_MEM_FlowTcamAd1:
        case DRV_ECC_MEM_FlowTcamAd2:
        case DRV_ECC_MEM_FlowTcamAd3:
        case DRV_ECC_MEM_FlowTcamAd4:
        case DRV_ECC_MEM_FlowTcamAd5:
        case DRV_ECC_MEM_FlowTcamAd6:
        /* LpmTcamIpInterruptNormal */
        case DRV_ECC_MEM_LpmTcamIpAdRam:
        /* LpmTcamNatInterruptNormal */
        case DRV_ECC_MEM_LpmTcamNatAdRam:

        default:
            *p_tblid = MaxTblId_t;
    }

    return;
}

STATIC int32
_drv_goldengate_ecc_recover_ram2mem(tbls_id_t tblid, uint32 tblidx, drv_ecc_mem_type_t* p_mem_type, uint32* p_offset)
{
    uint8   recover = 0, mem_id = 0, chip_id = 0;
    uint32  per_entry_bytes = 0, per_entry_addr = 0, data_base = 0;
    uint32  blknum = 0, local_idx = 0, is_sec_half = 0;
    drv_ecc_mem_type_t mem_type;

    if (TABLE_INFO_PTR(tblid) && TABLE_EXT_INFO_PTR(tblid)
       && (EXT_INFO_TYPE_DYNAMIC == TABLE_EXT_INFO_TYPE(tblid)))
    {
        for (mem_type = DRV_ECC_MEM_SharedRam0; mem_type <= DRV_ECC_MEM_NextHopMetRam1; mem_type++)
        {
            mem_id = mem_type - DRV_ECC_MEM_SharedRam0;

            if ((IS_BIT_SET(DYNAMIC_BITMAP(tblid), mem_id))
               && (tblidx >= DYNAMIC_START_INDEX(tblid, mem_id))
               && (tblidx <= DYNAMIC_END_INDEX(tblid, mem_id)))
            {
                *p_offset = tblidx - DYNAMIC_START_INDEX(tblid, mem_id);
                *p_mem_type = mem_type;
                break;
            }
        }

        if (DRV_ECC_MEM_INVALID != *p_mem_type)
        {
            drv_goldengate_ftm_check_tbl_recover(mem_id, *p_offset, &recover, &tblid);
            if (0 == recover)
            {
                *p_mem_type = DRV_ECC_MEM_INVALID;
            }
            *p_offset = (*p_offset) * DRV_BYTES_PER_ENTRY;
        }
    }
    else if (TABLE_INFO_PTR(tblid) && TABLE_EXT_INFO_PTR(tblid)
            && (EXT_INFO_TYPE_TCAM_AD == TABLE_EXT_INFO_TYPE(tblid)))
    {
        DRV_IF_ERROR_RETURN(drv_goldengate_chip_flow_tcam_get_blknum_index(chip_id, tblid, tblidx, &blknum, &local_idx, &is_sec_half));

        *p_mem_type = blknum + DRV_ECC_MEM_FlowTcamAd0;
        *p_offset = local_idx * TABLE_ENTRY_SIZE(tblid);
    }
    else if ((TABLE_INFO_PTR(tblid) && TABLE_EXT_INFO_PTR(tblid))
            && ((EXT_INFO_TYPE_TCAM_NAT_AD == TABLE_EXT_INFO_TYPE(tblid))
            || (EXT_INFO_TYPE_TCAM_LPM_AD == TABLE_EXT_INFO_TYPE(tblid))))
    {
        if (EXT_INFO_TYPE_TCAM_LPM_AD == TABLE_EXT_INFO_TYPE(tblid))
        {
            per_entry_bytes = DRV_LPM_AD0_BYTE_PER_ENTRY;
            per_entry_addr = DRV_LPM_AD0_BYTE_PER_ENTRY;
        }
        else if (EXT_INFO_TYPE_TCAM_NAT_AD == TABLE_EXT_INFO_TYPE(tblid))
        {
            per_entry_bytes = DRV_LPM_AD1_BYTE_PER_ENTRY;
            per_entry_addr = DRV_LPM_AD1_BYTE_PER_ENTRY;
        }

        for (mem_type = DRV_ECC_MEM_LpmTcamIpAdRam;
             mem_type <= DRV_ECC_MEM_LpmTcamNatAdRam; mem_type++)
        {
            data_base = TABLE_DATA_BASE(tblid, 0);

            if (((DRV_ECC_MEM_LpmTcamIpAdRam == mem_type)
               && ((DsLpmTcamIpv4Route40Ad_t == tblid)
               || (DsLpmTcamIpv4Route160Ad_t == tblid)))
               || ((DRV_ECC_MEM_LpmTcamNatAdRam == mem_type)
               && ((DsLpmTcamIpv4Pbr160Ad_t == tblid)
               || (DsLpmTcamIpv6Pbr320Ad_t == tblid)
               || (DsLpmTcamIpv4Nat160Ad_t == tblid)
               || (DsLpmTcamIpv6Nat160Ad_t == tblid))))
            {
                *p_offset = (((data_base + ((TABLE_ENTRY_SIZE(tblid)
                            / per_entry_bytes) * per_entry_addr) * tblidx)
                            - tcam_ad_base[mem_type - DRV_ECC_MEM_FlowTcamAd0])
                            / per_entry_addr) * per_entry_bytes;
                *p_mem_type = mem_type;
                break;
            }
        }
    }

    return DRV_E_NONE;
}

STATIC void
_drv_goldengate_ecc_recover_tbl2mem(tbls_id_t tbl_id, uint32 tbl_idx, drv_ecc_mem_type_t* p_mem_type, uint32* p_offset)
{
    *p_offset = TABLE_ENTRY_SIZE(tbl_id) * tbl_idx;
    *p_mem_type = DRV_ECC_MEM_INVALID;

    switch (tbl_id)
    {
        /* Normal interrupt */
        /* BufStoreInterruptNormal */
        case DsIgrPortTcPriMap_t:
            *p_mem_type = DRV_ECC_MEM_IgrPortTcPriMap;
            break;
        case DsIgrCondDisProfId_t:
            *p_mem_type = DRV_ECC_MEM_IgrCondDisProfId;
            break;
        case DsIgrPortTcMinProfId_t:
            *p_mem_type = DRV_ECC_MEM_IgrPortTcMinProfId;
            break;
        case DsIgrPortTcThrdProfId_t:
            *p_mem_type = DRV_ECC_MEM_IgrPortTcThrdProfId;
            break;
        case DsIgrPortTcThrdProfile_t:
            *p_mem_type = DRV_ECC_MEM_IgrPortTcThrdProfile;
            break;
        case DsIgrPortThrdProfile_t:
            *p_mem_type = DRV_ECC_MEM_IgrPortThrdProfile;
            break;
        case DsIgrPriToTcMap_t:
            *p_mem_type = DRV_ECC_MEM_IgrPriToTcMap;
            break;

        /* EgrOamHashInterruptNormal */
        case DsVlanXlateDefault_t:
            *p_mem_type = DRV_ECC_MEM_VlanXlateDefault;
            break;

        /* EpeHdrEditInterruptNormal */
        case DsPacketHeaderEditTunnel_t:
            *p_mem_type = DRV_ECC_MEM_PacketHeaderEditTunnel;
            break;

        /* EpeHdrProcInterruptNormal */
        case DsEgressPortMac_t:
            *p_mem_type = DRV_ECC_MEM_EgressPortMac;
            break;

        case DsL3Edit3W3rd_t:
            *p_mem_type = DRV_ECC_MEM_L3Edit3W3rd;
            break;

        case DsPortLinkAgg_t:
            *p_mem_type = DRV_ECC_MEM_PortLinkAgg;
            break;

        case DsEgressVsi_t:
            *p_mem_type = DRV_ECC_MEM_EgressVsi;
            break;

        case DsIpv6NatPrefix_t:
            *p_mem_type = DRV_ECC_MEM_Ipv6NatPrefix;
            break;

        case DsL2Edit6WOuter_t:
            *p_mem_type = DRV_ECC_MEM_L2Edit6WOuter;
            break;

        /* FibAccInterruptNormal */
        case MacLimitThreshold_t:
            *p_mem_type = DRV_ECC_MEM_MacLimitThresholdCnt;
            break;

        case DsMacLimitThreshold_t:
            *p_mem_type = DRV_ECC_MEM_MacLimitThresholdIdx;
            break;

        /* FlowAccInterruptNormal */
        case DsIpfixEgrPortCfg_t:
            *p_mem_type = DRV_ECC_MEM_IpfixEgrPortCfg;
            break;

        case DsIpfixIngPortCfg_t:
            *p_mem_type = DRV_ECC_MEM_IpfixIngPortCfg;
            break;

        /* IpeForwardInterruptNormal */
        case DsQcn_t:
            *p_mem_type = DRV_ECC_MEM_Qcn;
            break;

        case DsSrcChannel_t:
            *p_mem_type = DRV_ECC_MEM_SrcChannel;
            break;

        /* IpeHdrAdjInterruptNormal */
        case DsPhyPort_t:
            *p_mem_type = DRV_ECC_MEM_PhyPort;
            break;

        /* IpeIntfMapInterruptNormal */
        case DsPhyPortExt_t:
            *p_mem_type = DRV_ECC_MEM_PhyPortExt;
            break;

        case DsRouterMac_t:
            *p_mem_type = DRV_ECC_MEM_RouterMac;
            break;

        case DsSrcInterface_t:
            *p_mem_type = DRV_ECC_MEM_SrcInterface;
            break;

        case DsSrcPort_t:
            *p_mem_type = DRV_ECC_MEM_SrcPort;
            break;

        case DsVlanActionProfile_t:
            *p_mem_type = DRV_ECC_MEM_VlanActionProfile;
            break;

        case DsVlanRangeProfile_t:
            *p_mem_type = DRV_ECC_MEM_VlanRangeProfile;
            break;

        case DsVlan2Ptr_t:
            *p_mem_type = DRV_ECC_MEM_Vlan2Ptr;
            break;

        /* IpeLkupMgrInterruptNormal */
        case IpeClassificationDscpMap_t:
            *p_mem_type = DRV_ECC_MEM_IpeClassificationDscpMap;
            break;

        case DsAclVlanActionProfile_t:
            *p_mem_type = DRV_ECC_MEM_AclVlanActionProfile;
            break;

        /* IpePktProcInterruptNormal */
        case DsBidiPimGroup_t:
            *p_mem_type = DRV_ECC_MEM_BidiPimGroup;
            break;

        case DsEcmpGroup_t:
            *p_mem_type = DRV_ECC_MEM_EcmpGroup;
            break;

        case DsEcmpMember_t:
            *p_mem_type = DRV_ECC_MEM_EcmpMember;
            break;

        case DsVrf_t:
            *p_mem_type = DRV_ECC_MEM_Vrf;
            break;

        case DsVsi_t:
            *p_mem_type = DRV_ECC_MEM_Vsi;
            break;

        /* LpmTcamIpInterruptNormal */
        case LpmTcamIpAdRam_t:
            *p_mem_type = DRV_ECC_MEM_LpmTcamIpAdRam;
            break;

        /* LpmTcamNatInterruptNormal */
        case LpmTcamNatAdRam_t:
            *p_mem_type = DRV_ECC_MEM_LpmTcamNatAdRam;
            break;

        /* MetFifoInterruptNormal */
        case DsMetLinkAggregateBitmap_t:
            *p_mem_type = DRV_ECC_MEM_MetLinkAggregateBitmap;
            break;

        case DsMetLinkAggregatePort_t:
            *p_mem_type = DRV_ECC_MEM_MetLinkAggregatePort;
            break;

        case DsMetFifoExcp_t:
            *p_mem_type = DRV_ECC_MEM_MetFifoExcp;
            break;

        /* NetRxInterruptNormal */
        case DsChannelizeIngFc0_t:
            *p_mem_type = DRV_ECC_MEM_ChannelizeIngFc0;
            break;

        case DsChannelizeIngFc1_t:
            *p_mem_type = DRV_ECC_MEM_ChannelizeIngFc1;
            break;

        case DsChannelizeMode0_t:
            *p_mem_type = DRV_ECC_MEM_ChannelizeMode0;
            break;

        case DsChannelizeMode1_t:
            *p_mem_type = DRV_ECC_MEM_ChannelizeMode1;
            break;

        /* PktProcDsInterruptNormal */
        case DsStpState_t:
            *p_mem_type = DRV_ECC_MEM_StpState;
            break;

        case DsVlanStatus_t:
            *p_mem_type = DRV_ECC_MEM_VlanStatus;
            break;

        case DsVlan_t:
            *p_mem_type = DRV_ECC_MEM_Vlan;
            break;

        case DsVlanProfile_t:
            *p_mem_type = DRV_ECC_MEM_VlanProfile;
            break;

        case DsEthLmProfile_t:
            *p_mem_type = DRV_ECC_MEM_EthLmProfile;
            break;

        /* PolicingInterruptNormal */
        case DsPolicerControl_t:
            *p_mem_type = DRV_ECC_MEM_PolicerControl;
            break;

        case DsPolicerProfile_t:
            *p_mem_type = DRV_ECC_MEM_PolicerProfile;
            break;

        /* QMgrDeqSliceInterruptNormal */
        case DsChanShpProfile_t:
            *p_mem_type = DRV_ECC_MEM_ChanShpProfile;
            break;

        case DsQueShpProfId_t:
            *p_mem_type = DRV_ECC_MEM_QueShpProfId;
            break;

        case DsQueShpProfile_t:
            *p_mem_type = DRV_ECC_MEM_QueShpProfile;
            break;

        case DsGrpShpProfile_t:
            *p_mem_type = DRV_ECC_MEM_GrpShpProfile;
            break;

        case DsSchServiceProfile_t:
            *p_mem_type = DRV_ECC_MEM_SchServiceProfile;
            break;

        /* QMgrEnqInterruptNormal */
        case DsEgrResrcCtl_t:
            *p_mem_type = DRV_ECC_MEM_EgrResrcCtl;
            break;

        case DsLinkAggregateMember_t:
            *p_mem_type = DRV_ECC_MEM_LinkAggregateMember;
            break;

        case DsLinkAggregateMemberSet_t:
            *p_mem_type = DRV_ECC_MEM_LinkAggregateMemberSet;
            break;

        case DsLinkAggregationPort_t:
            *p_mem_type = DRV_ECC_MEM_LinkAggregationPort;
            break;

        case DsQueueNumGenCtl_t:
            *p_mem_type = DRV_ECC_MEM_QueueNumGenCtl;
            break;

        case DsQueueSelectMap_t:
            *p_mem_type = DRV_ECC_MEM_QueueSelectMap;
            break;

        case DsQueThrdProfile_t:
            *p_mem_type = DRV_ECC_MEM_QueThrdProfile;
            break;

        case DsLinkAggregateChannelMemberSet_t:
            *p_mem_type = DRV_ECC_MEM_LinkAggregateChannelMemberSet;
            break;

        /* Fatal interrupt */
        /* DsAgingInterruptFatal */
        case DsAging_t:
            *p_mem_type = DRV_ECC_MEM_Aging;
            break;

        /* DynamicDsShareInterruptFatal */
        case DsFwd_t:
            *p_mem_type = DRV_ECC_MEM_Fwd;
            break;

        /* EpeNextHopInterruptFatal */
        case DsDestInterface_t:
            *p_mem_type = DRV_ECC_MEM_DestInterface;
            break;

        case DsEgressVlanRangeProfile_t:
            *p_mem_type = DRV_ECC_MEM_EgressVlanRangeProfile;
            break;

        case EpeEditPriorityMap_t:
            *p_mem_type = DRV_ECC_MEM_EpeEditPriorityMap;
            break;

        case DsPortIsolation_t:
            *p_mem_type = DRV_ECC_MEM_PortIsolation;
            break;

        case DsDestPort_t:
            *p_mem_type = DRV_ECC_MEM_DestPort;
            break;

        case DsVlanTagBitMap_t:
            *p_mem_type = DRV_ECC_MEM_VlanTagBitMap;
            break;

        /* ShareStormCtlInterruptFatal */
        case DsVlanStormCtl_t:
            *p_mem_type = DRV_ECC_MEM_VlanStormCtl;
            break;

        case DsPortStormCtl_t:
            *p_mem_type = DRV_ECC_MEM_PortStormCtl;
            break;

        /* Tcam key */
        case DsAclQosMacKey160Ingr0_t:
        case DsAclQosL3Key160Ingr0_t:
        case DsAclQosL3Key320Ingr0_t:
        case DsAclQosIpv6Key640Ingr0_t:

        case DsAclQosL3Key320Ingr1_t:
        case DsAclQosIpv6Key640Ingr1_t:
        case DsAclQosMacKey160Ingr1_t:
        case DsAclQosL3Key160Ingr1_t:

        case DsAclQosL3Key320Ingr2_t:
        case DsAclQosIpv6Key640Ingr2_t:
        case DsAclQosMacKey160Ingr2_t:
        case DsAclQosL3Key160Ingr2_t:

        case DsAclQosMacKey160Ingr3_t:
        case DsAclQosL3Key160Ingr3_t:
        case DsAclQosL3Key320Ingr3_t:
        case DsAclQosIpv6Key640Ingr3_t:

        case DsAclQosMacKey160Egr0_t:
        case DsAclQosL3Key160Egr0_t:
        case DsAclQosL3Key320Egr0_t:
        case DsAclQosIpv6Key640Egr0_t:

        case DsUserId0MacKey160_t:
        case DsUserId0L3Key160_t:
        case DsUserId0L3Key320_t:
        case DsUserId0Ipv6Key640_t:

        case DsUserId1MacKey160_t:
        case DsUserId1L3Key160_t:
        case DsUserId1L3Key320_t:
        case DsUserId1Ipv6Key640_t:

        case DsLpmTcamIpv440Key_t:
        case DsLpmTcamIpv6160Key0_t:

        case DsLpmTcamIpv4Pbr160Key_t:
        case DsLpmTcamIpv6320Key_t:
        case DsLpmTcamIpv4NAT160Key_t:
        case DsLpmTcamIpv6160Key1_t:
            break;

        default:
            _drv_goldengate_ecc_recover_ram2mem(tbl_id, tbl_idx, p_mem_type, p_offset);
            break;
    }

    return;
}

STATIC void
_drv_goldengate_ecc_recover_read_tcam_cache(uint8 lchip, drv_ecc_mem_type_t mem_type,
                                 uint32 entry_offset, tbl_entry_t* p_tbl_entry,
                                 uint32* p_entry_dw)
{
    uint32 offset = 0, len = 0;
    uintptr  start_addr = 0;
    uintptr start_sw_data_addr = 0;

    *p_entry_dw = 0;

    /* flow tcam */
    /* lpm nat/pbr tcam */
    if (((mem_type >= DRV_ECC_MEM_TCAM_KEY0) && (mem_type <= DRV_ECC_MEM_TCAM_KEY6))
       || (DRV_ECC_MEM_TCAM_KEY8 == mem_type))
    {
        offset = entry_offset * (2 * 2 * DRV_BYTES_PER_ENTRY + 1);
        len = 2 * DRV_BYTES_PER_ENTRY;
    }
    /* lpm prefix tcam */
    else if (DRV_ECC_MEM_TCAM_KEY7 == mem_type)
    {
        offset = entry_offset * (2 * DRV_LPM_KEY_BYTES_PER_ENTRY + 1);
        len = DRV_LPM_KEY_BYTES_PER_ENTRY;
    }
    else
    {
        DRV_DBG_INFO("Ecc strore error tcam block id:%u.\n",
                      mem_type - DRV_ECC_MEM_TCAM_KEY0);
        return;
    }

    start_addr = g_gg_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip];

    start_sw_data_addr = start_addr + offset;

    if (0xFF == *((uint8*)start_sw_data_addr))
    {
        p_tbl_entry->data_entry = NULL;
        p_tbl_entry->mask_entry = NULL;
    }
    else
    {
        p_tbl_entry->data_entry = (uint32*)(start_sw_data_addr + 1);
        p_tbl_entry->mask_entry = (uint32*)(start_sw_data_addr + len + 1);
    }

    *p_entry_dw = len / sizeof(uint32);

    return;
}

#define ___ECC_RECOVER_INTERRUPT____

STATIC int32
_drv_goldengate_ecc_recover_proc_intr(uint8 lchip, tbls_id_t intr_tblid, uint8 intr_bit, tbls_id_t* p_tblid, uint32* p_offset, drv_ecc_err_type_t* p_err_type, drv_ecc_tbl_type_t* p_tbl_type)
{
    uint8     chip_id = 0;
    uint32    fail = 0, sid = 0;
    uint32    cmd = 0;
    tbls_id_t err_rec = MaxTblId_t;
    uint32    data[DRV_PE_MAX_ENTRY_WORD] = {0};
    uint32    rec_idx_f = 0, rec_fail_f = 0;
    tbls_id_t ignore_tbl = MaxTblId_t;
    drv_ecc_err_type_t err_type = DRV_ECC_ERR_TYPE_MBE;

    *p_tblid = MaxTblId_t;
    *p_offset = 0xFFFFFFFF;

    /* 1. mapping ecc fail rec, err tblid and casecade tbl indix offset */
    switch (intr_tblid)
    {
        case BufStoreInterruptNormal_t:
            err_rec = BufStoreRamChkRec_t;
            if (DRV_ECC_INTR_pktErrStatsMemSlice0 == intr_bit)
            {
                ignore_tbl = PktErrStatsMem_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_pktErrStatsMemSlice0ParityFailAddr_f ;
                rec_fail_f = BufStoreRamChkRec_pktErrStatsMemSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_pktErrStatsMemSlice1 == intr_bit)
            {
                ignore_tbl = PktErrStatsMem_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_pktErrStatsMemSlice1ParityFailAddr_f ;
                rec_fail_f = BufStoreRamChkRec_pktErrStatsMemSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_IgrPortTcPriMapSlice0 == intr_bit)
            {
                *p_tblid = DsIgrPortTcPriMap_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_dsIgrPortTcPriMapSlice0ParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_dsIgrPortTcPriMapSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_IgrPortTcPriMapSlice1 == intr_bit)
            {
                sid = 1;
                *p_tblid = DsIgrPortTcPriMap_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_dsIgrPortTcPriMapSlice1ParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_dsIgrPortTcPriMapSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_IgrCondDisProfIdSlice0 == intr_bit)
            {
                *p_tblid = DsIgrCondDisProfId_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_dsIgrCondDisProfIdSlice0ParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_dsIgrCondDisProfIdSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_IgrCondDisProfIdSlice1 == intr_bit)
            {
                sid = 1;
                *p_tblid = DsIgrCondDisProfId_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_dsIgrCondDisProfIdSlice1ParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_dsIgrCondDisProfIdSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_IgrPortTcMinProfIdSlice0 == intr_bit)
            {
                *p_tblid = DsIgrPortTcMinProfId_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_dsIgrPortTcMinProfIdSlice0ParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_dsIgrPortTcMinProfIdSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_IgrPortTcMinProfIdSlice1 == intr_bit)
            {
                sid = 1;
                *p_tblid = DsIgrPortTcMinProfId_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_dsIgrPortTcMinProfIdSlice1ParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_dsIgrPortTcMinProfIdSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_IgrPortTcThrdProfIdSlice0 == intr_bit)
            {
                *p_tblid = DsIgrPortTcThrdProfId_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_dsIgrPortTcThrdProfIdSlice0ParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_dsIgrPortTcThrdProfIdSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_IgrPortTcThrdProfIdSlice1 == intr_bit)
            {
                sid = 1;
                *p_tblid = DsIgrPortTcThrdProfId_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_dsIgrPortTcThrdProfIdSlice1ParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_dsIgrPortTcThrdProfIdSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_IgrPortCntSlice0 == intr_bit)
            {
                ignore_tbl = DsIgrPortCnt_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_dsIgrPortCntSlice0ParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_dsIgrPortCntSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_IgrPortCntSlice1 == intr_bit)
            {
                ignore_tbl = DsIgrPortCnt_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_dsIgrPortCntSlice1ParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_dsIgrPortCntSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_IgrPortTcCntSlice0 == intr_bit)
            {
                ignore_tbl = DsIgrPortTcCnt_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_dsIgrPortTcCntSlice0ParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_dsIgrPortTcCntSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_IgrPortTcCntSlice1 == intr_bit)
            {
                ignore_tbl = DsIgrPortTcCnt_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_dsIgrPortTcCntSlice1ParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_dsIgrPortTcCntSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_bsChanInfoSlice0 == intr_bit)
            {
                ignore_tbl = BsChanInfoSlice0_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_bsChanInfoSlice0ParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_bsChanInfoSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_bsChanInfoSlice1 == intr_bit)
            {
                ignore_tbl = BsChanInfoSlice1_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_bsChanInfoSlice1ParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_bsChanInfoSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_IgrPortTcThrdProfile == intr_bit)
            {
                *p_tblid = DsIgrPortTcThrdProfile_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_dsIgrPortTcThrdProfileParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_dsIgrPortTcThrdProfileParityFail_f;
            }
            else if (DRV_ECC_INTR_IgrPortThrdProfile == intr_bit)
            {
                *p_tblid = DsIgrPortThrdProfile_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_dsIgrPortThrdProfileParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_dsIgrPortThrdProfileParityFail_f;
            }
            else if (DRV_ECC_INTR_IgrPriToTcMap == intr_bit)
            {
                *p_tblid = DsIgrPriToTcMap_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = BufStoreRamChkRec_dsIgrPriToTcMapParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_dsIgrPriToTcMapParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case DmaCtlInterruptNormal0_t:
        case DmaCtlInterruptNormal1_t:
            sid = intr_tblid - DmaCtlInterruptNormal0_t;
            err_rec = DmaCtlRamChkRec0_t + sid;
            if (DRV_ECC_INTR_dmaPktRxMem == intr_bit)
            {
                ignore_tbl = DmaPktRxMem0_t + sid;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = DmaCtlRamChkRec0_dmaPktRxMemParityFailAddr_f;
                rec_fail_f = DmaCtlRamChkRec0_dmaPktRxMemParityFail_f;
            }
            else if (DRV_ECC_INTR_dmaDescCache == intr_bit)
            {
                ignore_tbl = DmaDescCache0_t + sid;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = DmaCtlRamChkRec0_dmaDescCacheParityFailAddr_f;
                rec_fail_f = DmaCtlRamChkRec0_dmaDescCacheParityFail_f;
            }
            else if (DRV_ECC_INTR_dmaRegRdMem == intr_bit)
            {
                ignore_tbl = DmaRegRdMem0_t + sid;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = DmaCtlRamChkRec0_dmaRegRdMemParityFailAddr_f;
                rec_fail_f = DmaCtlRamChkRec0_dmaRegRdMemParityFail_f;
            }
            else if (DRV_ECC_INTR_dmaRegWrMem == intr_bit)
            {
                ignore_tbl = DmaRegWrMem0_t + sid;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = DmaCtlRamChkRec0_dmaRegWrMemParityFailAddr_f;
                rec_fail_f = DmaCtlRamChkRec0_dmaRegWrMemParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case EgrOamHashInterruptNormal0_t:
        case EgrOamHashInterruptNormal1_t:
            sid = intr_tblid - EgrOamHashInterruptNormal0_t;
            if (DRV_ECC_INTR_VlanXlateDefault == intr_bit)
            {
                *p_tblid = DsVlanXlateDefault_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                err_rec = EgrOamHashRamChkRec0_t + sid;
                rec_idx_f = EgrOamHashRamChkRec0_dsVlanXlateDefaultParityFailAddr_f;
                rec_fail_f = EgrOamHashRamChkRec0_dsVlanXlateDefaultParityFail_f;
            }
            break;

        case EpeHdrEditInterruptNormal0_t:
        case EpeHdrEditInterruptNormal1_t:
            sid = intr_tblid - EpeHdrEditInterruptNormal0_t;
            if (DRV_ECC_INTR_PacketHeaderEditTunnel == intr_bit)
            {
                *p_tblid = DsPacketHeaderEditTunnel_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                err_rec = EpeHdrEditRamChkRec0_t + sid;
                rec_idx_f = EgrOamHashRamChkRec0_dsVlanXlateDefaultParityFailAddr_f;
                rec_fail_f = EgrOamHashRamChkRec0_dsVlanXlateDefaultParityFail_f;
            }
            break;

        case EpeHdrProcInterruptNormal0_t:
        case EpeHdrProcInterruptNormal1_t:
            sid = intr_tblid - EpeHdrProcInterruptNormal0_t;
            err_rec = EpeHdrProcRamChkRec0_t + sid;
            if (DRV_ECC_INTR_EgressPortMac == intr_bit)
            {
                *p_tblid = DsEgressPortMac_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = EpeHdrProcRamChkRec0_dsEgressPortMacParityFailAddr_f;
                rec_fail_f = EpeHdrProcRamChkRec0_dsEgressPortMacParityFail_f;
            }
            else if (DRV_ECC_INTR_L3Edit3W3rd == intr_bit)
            {
                *p_tblid = DsL3Edit3W3rd_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = EpeHdrProcRamChkRec0_dsL3Edit3W3rdParityFailAddr_f;
                rec_fail_f = EpeHdrProcRamChkRec0_dsL3Edit3W3rdParityFail_f;
            }
            else if (DRV_ECC_INTR_PortLinkAgg == intr_bit)
            {
                *p_tblid = DsPortLinkAgg_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = EpeHdrProcRamChkRec0_dsPortLinkAggParityFailAddr_f;
                rec_fail_f = EpeHdrProcRamChkRec0_dsPortLinkAggParityFail_f;
            }
            else if (DRV_ECC_INTR_EgressVsi == intr_bit)
            {
                *p_tblid = DsEgressVsi_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = EpeHdrProcRamChkRec0_dsEgressVsiParityFailAddr_f;
                rec_fail_f = EpeHdrProcRamChkRec0_dsEgressVsiParityFail_f;
            }
            else if (DRV_ECC_INTR_Ipv6NatPrefix == intr_bit)
            {
                *p_tblid = DsIpv6NatPrefix_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = EpeHdrProcRamChkRec0_dsIpv6NatPrefixParityFailAddr_f;
                rec_fail_f = EpeHdrProcRamChkRec0_dsIpv6NatPrefixParityFail_f;
            }
            else if (DRV_ECC_INTR_L2Edit6WOuter == intr_bit)
            {
                *p_tblid = DsL2Edit6WOuter_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = EpeHdrProcRamChkRec0_dsL2Edit6WOuterParityFailAddr_f;
                rec_fail_f = EpeHdrProcRamChkRec0_dsL2Edit6WOuterParityFail_f;
            }
            else if (DRV_ECC_INTR_LatencyMonCnt == intr_bit)
            {
                ignore_tbl = DsLatencyMonCnt_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = EpeHdrProcRamChkRec0_dsLatencyMonCntParityFailAddr_f;
                rec_fail_f = EpeHdrProcRamChkRec0_dsLatencyMonCntParityFail_f;
            }
            else if (DRV_ECC_INTR_LatencyWatermark == intr_bit)
            {
                ignore_tbl = DsLatencyWatermark_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = EpeHdrProcRamChkRec0_dsLatencyWatermarkParityFailAddr_f;
                rec_fail_f = EpeHdrProcRamChkRec0_dsLatencyWatermarkParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case FibAccInterruptNormal_t:
            err_rec = FibAccRamChkRec_t;
            if (DRV_ECC_INTR_MacLimitThresholdCnt == intr_bit)
            {
                *p_tblid = MacLimitThreshold_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = FibAccRamChkRec_macLimitThresholdParityFailAddr_f;
                rec_fail_f = FibAccRamChkRec_macLimitThresholdParityFail_f;
            }
            else if (DRV_ECC_INTR_MacLimitCount == intr_bit)
            {
                ignore_tbl = DsMacLimitCount_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = FibAccRamChkRec_dsMacLimitCountParityFailAddr_f;
                rec_fail_f = FibAccRamChkRec_dsMacLimitCountParityFail_f;
            }
            else if (DRV_ECC_INTR_MacLimitThresholdIdx == intr_bit)
            {
                *p_tblid = DsMacLimitThreshold_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = FibAccRamChkRec_dsMacLimitCountParityFailAddr_f;
                rec_fail_f = FibAccRamChkRec_dsMacLimitCountParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

         case FlowAccInterruptNormal_t:
            err_rec = FlowAccRamChkRec_t;
            if (DRV_ECC_INTR_IpfixEgrPortCfg == intr_bit)
            {
                *p_tblid = DsIpfixEgrPortCfg_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = FlowAccRamChkRec_dsIpfixEgrPortCfgParityFailAddr_f;
                rec_fail_f = FlowAccRamChkRec_dsIpfixEgrPortCfgParityFail_f;
            }
            else if (DRV_ECC_INTR_IpfixEgrPortSamplingCount == intr_bit)
            {
                ignore_tbl = DsIpfixEgrPortSamplingCount_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = FlowAccRamChkRec_dsIpfixEgrPortSamplingCountParityFailAddr_f;
                rec_fail_f = FlowAccRamChkRec_dsIpfixEgrPortSamplingCountParityFail_f;
            }
            else if (DRV_ECC_INTR_IpfixIngPortCfg == intr_bit)
            {
                *p_tblid = DsIpfixIngPortCfg_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = FlowAccRamChkRec_dsIpfixIngPortCfgParityFailAddr_f;
                rec_fail_f = FlowAccRamChkRec_dsIpfixIngPortCfgParityFail_f;
            }
            else if (DRV_ECC_INTR_IpfixIngPortSamplingCount == intr_bit)
            {
                ignore_tbl = DsIpfixIngPortSamplingCount_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = FlowAccRamChkRec_dsIpfixIngPortSamplingCountParityFailAddr_f;
                rec_fail_f = FlowAccRamChkRec_dsIpfixIngPortSamplingCountParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

         case FlowTcamInterruptNormal_t:
            err_rec = FlowTcamRamChkRec_t;
            err_type = DRV_ECC_ERR_TYPE_MBE;
            if (DRV_ECC_INTR_FlowTcamAd0 == intr_bit)
            {
                rec_idx_f = FlowTcamRamChkRec_flowTcamAd0ParityFailAddr_f;
                rec_fail_f = FlowTcamRamChkRec_flowTcamAd0ParityFail_f;
            }
            else if (DRV_ECC_INTR_FlowTcamAd1 == intr_bit)
            {
                rec_idx_f = FlowTcamRamChkRec_flowTcamAd1ParityFailAddr_f;
                rec_fail_f = FlowTcamRamChkRec_flowTcamAd1ParityFail_f;
            }
            else if (DRV_ECC_INTR_FlowTcamAd2 == intr_bit)
            {
                rec_idx_f = FlowTcamRamChkRec_flowTcamAd2ParityFailAddr_f;
                rec_fail_f = FlowTcamRamChkRec_flowTcamAd2ParityFail_f;
            }
            else if (DRV_ECC_INTR_FlowTcamAd3 == intr_bit)
            {
                rec_idx_f = FlowTcamRamChkRec_flowTcamAd3ParityFailAddr_f;
                rec_fail_f = FlowTcamRamChkRec_flowTcamAd3ParityFail_f;
            }
            else if (DRV_ECC_INTR_FlowTcamAd4 == intr_bit)
            {
                rec_idx_f = FlowTcamRamChkRec_flowTcamAd4ParityFailAddr_f;
                rec_fail_f = FlowTcamRamChkRec_flowTcamAd4ParityFail_f;
            }
            else if (DRV_ECC_INTR_FlowTcamAd5 == intr_bit)
            {
                rec_idx_f = FlowTcamRamChkRec_flowTcamAd5ParityFailAddr_f;
                rec_fail_f = FlowTcamRamChkRec_flowTcamAd5ParityFail_f;
            }
            else if (DRV_ECC_INTR_FlowTcamAd6 == intr_bit)
            {
                rec_idx_f = FlowTcamRamChkRec_flowTcamAd6ParityFailAddr_f;
                rec_fail_f = FlowTcamRamChkRec_flowTcamAd6ParityFail_f;
            }
            else if (DRV_ECC_INTR_flowTcamBistReqMem == intr_bit)
            {
                ignore_tbl = FlowTcamBistReqMem_t;
                rec_idx_f = FlowTcamRamChkRec_flowTcamBistReqMemParityFailAddr_f;
                rec_fail_f = FlowTcamRamChkRec_flowTcamBistReqMemParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case GlobalStatsInterruptNormal_t:
            err_rec = GlobalStatsRamChkRec_t;
            if (DRV_ECC_INTR_Stats == intr_bit)
            {
                ignore_tbl = DsStats_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsParityFail_f;
            }
            else if (DRV_ECC_INTR_eEEStatsSlice0Ram == intr_bit)
            {
                ignore_tbl = EEEStatsRam_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = GlobalStatsRamChkRec_eEEStatsSlice0RamParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_eEEStatsSlice0RamParityFail_f;
            }
            else if (DRV_ECC_INTR_eEEStatsSlice1Ram == intr_bit)
            {
                ignore_tbl = EEEStatsRam_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = GlobalStatsRamChkRec_eEEStatsSlice1RamParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_eEEStatsSlice1RamParityFail_f;
            }
            else if (DRV_ECC_INTR_rxInbdStatsSlice0Ram == intr_bit)
            {
                ignore_tbl = RxInbdStatsRam_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = GlobalStatsRamChkRec_rxInbdStatsSlice0RamParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_rxInbdStatsSlice0RamParityFail_f;
            }
            else if (DRV_ECC_INTR_rxInbdStatsSlice1Ram == intr_bit)
            {
                ignore_tbl = RxInbdStatsRam_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = GlobalStatsRamChkRec_rxInbdStatsSlice1RamParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_rxInbdStatsSlice1RamParityFail_f;
            }
            else if (DRV_ECC_INTR_txInbdStatsEpeSlice0Ram == intr_bit)
            {
                ignore_tbl = TxInbdStatsEpeRam_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = GlobalStatsRamChkRec_txInbdStatsEpeSlice0RamParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_txInbdStatsEpeSlice0RamParityFail_f;
            }
            else if (DRV_ECC_INTR_txInbdStatsEpeSlice1Ram == intr_bit)
            {
                ignore_tbl = TxInbdStatsEpeRam_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = GlobalStatsRamChkRec_txInbdStatsEpeSlice1RamParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_txInbdStatsEpeSlice1RamParityFail_f;
            }
            else if (DRV_ECC_INTR_txInbdStatsPauseSlice0Ram == intr_bit)
            {
                ignore_tbl = TxInbdStatsPauseRam_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = GlobalStatsRamChkRec_txInbdStatsPauseSlice0RamParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_txInbdStatsPauseSlice0RamParityFail_f;
            }
            else if (DRV_ECC_INTR_txInbdStatsPauseSlice1Ram == intr_bit)
            {
                ignore_tbl = TxInbdStatsPauseRam_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = GlobalStatsRamChkRec_txInbdStatsPauseSlice1RamParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_txInbdStatsPauseSlice1RamParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheDequeueSlice0 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheDequeue_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheDequeueSlice0ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheDequeueSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheDequeueSlice1 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheDequeue_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheDequeueSlice1ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheDequeueSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheEnqueueSlice0 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheEnqueue_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheEnqueueSlice0ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheEnqueueSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheEnqueueSlice1 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheEnqueue_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheEnqueueSlice1ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheEnqueueSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheEpeAclSlice0 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheEpeAcl_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheEpeAclSlice0ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheEpeAclSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheEpeAclSlice1 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheEpeAcl_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheEpeAclSlice1ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheEpeAclSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheEpeFwdSlice0 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheEpeFwd_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheEpeFwdSlice0ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheEpeFwdSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheEpeFwdSlice1 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheEpeFwd_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheEpeFwdSlice1ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheEpeFwdSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheEpeIntfSlice0 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheEpeIntf_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheEpeIntfSlice0ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheEpeIntfSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheEpeIntfSlice1 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheEpeIntf_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheEpeIntfSlice1ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheEpeIntfSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_EgressPortLogStatsSlice0 == intr_bit)
            {
                ignore_tbl = DsEgressPortLogStats_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsEgressPortLogStatsSlice0ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsEgressPortLogStatsSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_EgressPortLogStatsSlice1 == intr_bit)
            {
                ignore_tbl = DsEgressPortLogStats_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsEgressPortLogStatsSlice1ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsEgressPortLogStatsSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheIpeAcl0Slice0 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheIpeAcl0_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl0Slice0ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl0Slice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheIpeAcl0Slice1 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheIpeAcl0_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl0Slice1ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl0Slice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheIpeAcl1Slice0 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheIpeAcl1_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl1Slice0ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl1Slice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheIpeAcl1Slice1 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheIpeAcl1_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl1Slice1ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl1Slice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheIpeAcl2Slice0 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheIpeAcl2_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl2Slice0ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl2Slice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheIpeAcl2Slice1 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheIpeAcl2_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl2Slice1ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl2Slice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheIpeAcl3Slice0 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheIpeAcl3_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl3Slice0ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl3Slice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheIpeAcl3Slice1 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheIpeAcl3_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl3Slice1ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeAcl3Slice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheIpeFwdSlice0 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheIpeFwd_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeFwdSlice0ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeFwdSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheIpeFwdSlice1 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheIpeFwd_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeFwdSlice1ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeFwdSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheIpeIntfSlice0 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheIpeIntf_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeIntfSlice0ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeIntfSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCacheIpeIntfSlice1 == intr_bit)
            {
                ignore_tbl = DsStatsRamCacheIpeIntf_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeIntfSlice1ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCacheIpeIntfSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_IngressPortLogStatsSlice0 == intr_bit)
            {
                ignore_tbl = DsIngressPortLogStats_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsIngressPortLogStatsSlice0ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsIngressPortLogStatsSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_IngressPortLogStatsSlice1 == intr_bit)
            {
                ignore_tbl = DsIngressPortLogStats_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsIngressPortLogStatsSlice1ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsIngressPortLogStatsSlice1ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCachePolicingSlice0 == intr_bit)
            {
                ignore_tbl = DsStatsRamCachePolicing_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCachePolicingSlice0ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCachePolicingSlice0ParityFail_f;
            }
            else if (DRV_ECC_INTR_StatsRamCachePolicingSlice1 == intr_bit)
            {
                ignore_tbl = DsStatsRamCachePolicing_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = GlobalStatsRamChkRec_dsStatsRamCachePolicingSlice1ParityFailAddr_f;
                rec_fail_f = GlobalStatsRamChkRec_dsStatsRamCachePolicingSlice1ParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case I2CMasterInterruptNormal0_t:
        case I2CMasterInterruptNormal1_t:
            sid = intr_tblid - I2CMasterInterruptNormal0_t;
            if (DRV_ECC_INTR_i2CMasterDataMem == intr_bit)
            {
                err_rec = I2CMasterRamChkRec0_t + sid;
                ignore_tbl = I2CMasterDataMem0_t + sid;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = I2CMasterRamChkRec0_i2CMasterDataMemParityFailAddr_f;
                rec_fail_f = I2CMasterRamChkRec0_i2CMasterDataMemParityFail_f;
            }
            break;

        case IpeForwardInterruptNormal0_t:
        case IpeForwardInterruptNormal1_t:
            sid = intr_tblid - IpeForwardInterruptNormal0_t;
            if (DRV_ECC_INTR_QCN == intr_bit)
            {
                err_rec = IpeForwardRamChkRec0_t + sid;
                *p_tblid = DsQcn_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpeForwardRamChkRec0_dsQcnParityFailAddr_f;
                rec_fail_f = IpeForwardRamChkRec0_dsQcnParityFail_f;
            }
            else if (DRV_ECC_INTR_SrcChannel == intr_bit)
            {
                err_rec = IpeForwardRamChkRec0_t + sid;
                *p_tblid = DsSrcChannel_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = IpeForwardRamChkRec0_dsSrcChannelParityFailAddr_f;
                rec_fail_f = IpeForwardRamChkRec0_dsSrcChannelParityFail_f;
            }
            break;

        case IpeHdrAdjInterruptNormal0_t:
        case IpeHdrAdjInterruptNormal1_t:
            sid = intr_tblid - IpeHdrAdjInterruptNormal0_t;
            if (DRV_ECC_INTR_PhyPort == intr_bit)
            {
                err_rec = IpeHdrAdjRamChkRec0_t + sid;
                *p_tblid = DsPhyPort_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpeHdrAdjRamChkRec0_dsPhyPortParityFailAddr_f;
                rec_fail_f = IpeHdrAdjRamChkRec0_dsPhyPortParityFail_f;
            }
            break;

        case IpeIntfMapInterruptNormal0_t:
        case IpeIntfMapInterruptNormal1_t:
            sid = intr_tblid - IpeIntfMapInterruptNormal0_t;
            err_rec = IpeIntfMapRamChkRec0_t + sid;
            if (DRV_ECC_INTR_PhyPortExt == intr_bit)
            {
                *p_tblid = DsPhyPortExt_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpeIntfMapRamChkRec0_dsPhyPortExtParityFailAddr_f;
                rec_fail_f = IpeIntfMapRamChkRec0_dsPhyPortExtParityFail_f;
            }
            else if (DRV_ECC_INTR_RouterMac == intr_bit)
            {
                *p_tblid = DsRouterMac_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpeIntfMapRamChkRec0_dsRouterMacParityFailAddr_f;
                rec_fail_f = IpeIntfMapRamChkRec0_dsRouterMacParityFail_f;
            }
            else if (DRV_ECC_INTR_SrcInterface == intr_bit)
            {
                *p_tblid = DsSrcInterface_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpeIntfMapRamChkRec0_dsSrcInterfaceParityFailAddr_f;
                rec_fail_f = IpeIntfMapRamChkRec0_dsSrcInterfaceParityFail_f;
            }
            else if (DRV_ECC_INTR_SrcPort == intr_bit)
            {
                *p_tblid = DsSrcPort_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpeIntfMapRamChkRec0_dsSrcPortParityFailAddr_f;
                rec_fail_f = IpeIntfMapRamChkRec0_dsSrcPortParityFail_f;
            }
            else if (DRV_ECC_INTR_VlanActionProfile == intr_bit)
            {
                *p_tblid = DsVlanActionProfile_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpeIntfMapRamChkRec0_dsVlanActionProfileParityFailAddr_f;
                rec_fail_f = IpeIntfMapRamChkRec0_dsVlanActionProfileParityFail_f;
            }
            else if (DRV_ECC_INTR_VlanRangeProfile == intr_bit)
            {
                *p_tblid = DsVlanRangeProfile_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpeIntfMapRamChkRec0_dsVlanRangeProfileParityFailAddr_f;
                rec_fail_f = IpeIntfMapRamChkRec0_dsVlanRangeProfileParityFail_f;
            }
            else if (DRV_ECC_INTR_Vlan2Ptr == intr_bit)
            {
                *p_tblid = DsVlan2Ptr_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpeIntfMapRamChkRec0_dsVlan2PtrParityFailAddr_f;
                rec_fail_f = IpeIntfMapRamChkRec0_dsVlan2PtrParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case IpeLkupMgrInterruptNormal0_t:
        case IpeLkupMgrInterruptNormal1_t:
            sid = intr_tblid - IpeLkupMgrInterruptNormal0_t;
            if (DRV_ECC_INTR_IpeClassificationDscpMap == intr_bit)
            {
                err_rec = IpeLkupMgrRamChkRec0_t + sid;
                *p_tblid = IpeClassificationDscpMap_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpeLkupMgrRamChkRec0_ipeClassificationDscpMapParityFailAddr_f;
                rec_fail_f = IpeLkupMgrRamChkRec0_ipeClassificationDscpMapParityFail_f;
            }
            else if (DRV_ECC_INTR_AclVlanActionProfile == intr_bit)
            {
                err_rec = IpeLkupMgrRamChkRec0_t + sid;
                *p_tblid = DsAclVlanActionProfile_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpeLkupMgrRamChkRec0_dsAclVlanActionProfileParityFailAddr_f;
                rec_fail_f = IpeLkupMgrRamChkRec0_dsAclVlanActionProfileParityFail_f;
            }
            break;

        case IpePktProcInterruptNormal0_t:
        case IpePktProcInterruptNormal1_t:
            sid = intr_tblid - IpePktProcInterruptNormal0_t;
            err_rec = IpePktProcRamChkRec0_t + sid;
            if (DRV_ECC_INTR_BidiPimGroup == intr_bit)
            {
                *p_tblid = DsBidiPimGroup_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpePktProcRamChkRec0_dsBidiPimGroupParityFailAddr_f;
                rec_fail_f = IpePktProcRamChkRec0_dsBidiPimGroupParityFail_f;
            }
            else if (DRV_ECC_INTR_EcmpGroup == intr_bit)
            {
                *p_tblid = DsEcmpGroup_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpePktProcRamChkRec0_dsEcmpGroupParityFailAddr_f;
                rec_fail_f = IpePktProcRamChkRec0_dsEcmpGroupParityFail_f;
            }
            else if (DRV_ECC_INTR_EcmpMember == intr_bit)
            {
                *p_tblid = DsEcmpMember_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpePktProcRamChkRec0_dsEcmpMemberParityFailAddr_f;
                rec_fail_f = IpePktProcRamChkRec0_dsEcmpMemberParityFail_f;
            }
            else if (DRV_ECC_INTR_Rpf == intr_bit)
            {
                ignore_tbl = DsRpf_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpePktProcRamChkRec0_dsRpfParityFailAddr_f;
                rec_fail_f = IpePktProcRamChkRec0_dsRpfParityFail_f;
            }
            else if (DRV_ECC_INTR_Vrf == intr_bit)
            {
                *p_tblid = DsVrf_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpePktProcRamChkRec0_dsVrfParityFailAddr_f;
                rec_fail_f = IpePktProcRamChkRec0_dsVrfParityFail_f;
            }
            else if (DRV_ECC_INTR_Vsi == intr_bit)
            {
                *p_tblid = DsVsi_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = IpePktProcRamChkRec0_dsVsiParityFailAddr_f;
                rec_fail_f = IpePktProcRamChkRec0_dsVsiParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case LpmTcamIpInterruptNormal_t:
            if (DRV_ECC_INTR_LpmTcamIpAdRam == intr_bit)
            {
                err_rec = LpmTcamIpRamChkRec_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = LpmTcamIpRamChkRec_lpmTcamIpAdRamParityFailAddr_f;
                rec_fail_f = LpmTcamIpRamChkRec_lpmTcamIpAdRamParityFail_f;
            }
            break;

        case LpmTcamNatInterruptNormal0_t:
        case LpmTcamNatInterruptNormal1_t:
            sid = intr_tblid - LpmTcamNatInterruptNormal0_t;
            if (DRV_ECC_INTR_LpmTcamNatAdRam == intr_bit)
            {
                err_rec = LpmTcamNatRamChkRec0_t + sid;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = LpmTcamNatRamChkRec0_lpmTcamNatAdRamParityFailAddr_f;
                rec_fail_f = LpmTcamNatRamChkRec0_lpmTcamNatAdRamParityFail_f;
            }
            else if (DRV_ECC_INTR_lpmTcamNatBistReqMem == intr_bit)
            {
                err_rec = LpmTcamNatRamChkRec0_t + sid;
                ignore_tbl = LpmTcamNatBistReqMem0_t + sid;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = LpmTcamNatRamChkRec0_lpmTcamNatBistReqMemParityFailAddr_f;
                rec_fail_f = LpmTcamNatRamChkRec0_lpmTcamNatBistReqMemParityFail_f;
            }
            break;

        case MetFifoInterruptNormal_t:
            err_rec = MetFifoRamChkRec_t;
            if (DRV_ECC_INTR_MetLinkAggregateBitmap == intr_bit)
            {
                *p_tblid = DsMetLinkAggregateBitmap_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = MetFifoRamChkRec_dsMetLinkAggregateBitmapParityFailAddr_f;
                rec_fail_f = MetFifoRamChkRec_dsMetLinkAggregateBitmapParityFail_f;
            }
            else if (DRV_ECC_INTR_MetLinkAggregatePort == intr_bit)
            {
                *p_tblid = DsMetLinkAggregatePort_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = MetFifoRamChkRec_dsMetLinkAggregatePortParityFailAddr_f;
                rec_fail_f = MetFifoRamChkRec_dsMetLinkAggregatePortParityFail_f;
            }
            else if (DRV_ECC_INTR_MetFifoExcp == intr_bit)
            {
                *p_tblid = DsMetFifoExcp_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = MetFifoRamChkRec_dsMetFifoExcpParityFailAddr_f;
                rec_fail_f = MetFifoRamChkRec_dsMetFifoExcpParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case NetRxInterruptNormal0_t:
            if (DRV_ECC_INTR_ChannelizeIngFc == intr_bit)
            {
                err_rec = NetRxRamChkRec0_t;
                *p_tblid = DsChannelizeIngFc0_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = NetRxRamChkRec0_dsChannelizeIngFcParityFailAddr_f;
                rec_fail_f = NetRxRamChkRec0_dsChannelizeIngFcParityFail_f;
            }
            else if (DRV_ECC_INTR_ChannelizeMode == intr_bit)
            {
                err_rec = NetRxRamChkRec0_t;
                *p_tblid = DsChannelizeMode0_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = NetRxRamChkRec0_dsChannelizeModeParityFailAddr_f;
                rec_fail_f = NetRxRamChkRec0_dsChannelizeModeParityFail_f;
            }
            break;

        case NetRxInterruptNormal1_t:
            if (DRV_ECC_INTR_ChannelizeIngFc == intr_bit)
            {
                err_rec = NetRxRamChkRec1_t;
                *p_tblid = DsChannelizeIngFc1_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = NetRxRamChkRec1_dsChannelizeIngFcParityFailAddr_f;
                rec_fail_f = NetRxRamChkRec1_dsChannelizeIngFcParityFail_f;
            }
            else if (DRV_ECC_INTR_ChannelizeMode == intr_bit)
            {
                err_rec = NetRxRamChkRec1_t;
                *p_tblid = DsChannelizeMode1_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = NetRxRamChkRec1_dsChannelizeModeParityFailAddr_f;
                rec_fail_f = NetRxRamChkRec1_dsChannelizeModeParityFail_f;
            }
            break;

        case OobFcInterruptNormal0_t:
        case OobFcInterruptNormal1_t:
            sid = intr_tblid - OobFcInterruptNormal0_t;
            if (DRV_ECC_INTR_oobFcCal == intr_bit)
            {
                err_rec = OobFcRamChkRec0_t + sid;
                ignore_tbl = OobFcCal0_t + sid;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = OobFcRamChkRec0_oobFcCalParityFailAddr_f;
                rec_fail_f = OobFcRamChkRec0_oobFcCalParityFail_f;
            }
            else if (DRV_ECC_INTR_oobFcGrpMap == intr_bit)
            {
                err_rec = OobFcRamChkRec0_t + sid;
                ignore_tbl = OobFcGrpMap0_t + sid;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = OobFcRamChkRec0_oobFcGrpMapParityFailAddr_f;
                rec_fail_f = OobFcRamChkRec0_oobFcGrpMapParityFail_f;
            }
            break;

        case PktProcDsInterruptNormal0_t:
        case PktProcDsInterruptNormal1_t:
            sid = intr_tblid - PktProcDsInterruptNormal0_t;
            err_rec = PktProcDsRamChkRec0_t + sid;
            if (DRV_ECC_INTR_StpState == intr_bit)
            {
                *p_tblid = DsStpState_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = PktProcDsRamChkRec0_dsStpStateParityFailAddr_f;
                rec_fail_f = PktProcDsRamChkRec0_dsStpStateParityFail_f;
            }
            else if (DRV_ECC_INTR_VlanStatus == intr_bit)
            {
                *p_tblid = DsVlanStatus_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = PktProcDsRamChkRec0_dsVlanStatusParityFailAddr_f;
                rec_fail_f = PktProcDsRamChkRec0_dsVlanStatusParityFail_f;
            }
            else if (DRV_ECC_INTR_Vlan == intr_bit)
            {
                *p_tblid = DsVlan_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = PktProcDsRamChkRec0_dsVlanParityFailAddr_f;
                rec_fail_f = PktProcDsRamChkRec0_dsVlanParityFail_f;
            }
            else if (DRV_ECC_INTR_VlanProfile == intr_bit)
            {
                *p_tblid = DsVlanProfile_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = PktProcDsRamChkRec0_dsVlanProfileParityFailAddr_f;
                rec_fail_f = PktProcDsRamChkRec0_dsVlanProfileParityFail_f;
            }
            else if (DRV_ECC_INTR_EthLmProfile == intr_bit)
            {
                *p_tblid = DsEthLmProfile_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = PktProcDsRamChkRec0_dsEthLmProfileParityFailAddr_f;
                rec_fail_f = PktProcDsRamChkRec0_dsEthLmProfileParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case PolicingInterruptNormal_t:
            err_rec = PolicingRamChkRec_t;
            if (DRV_ECC_INTR_PolicerCountCommit == intr_bit)
            {
                ignore_tbl = DsPolicerCountCommit_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = PolicingRamChkRec_dsPolicerCountCommitParityFailAddr_f;
                rec_fail_f = PolicingRamChkRec_dsPolicerCountCommitParityFail_f;
            }
            else if (DRV_ECC_INTR_PolicerCountExcess == intr_bit)
            {
                ignore_tbl = DsPolicerCountExcess_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = PolicingRamChkRec_dsPolicerCountExcessParityFailAddr_f;
                rec_fail_f = PolicingRamChkRec_dsPolicerCountExcessParityFail_f;
            }
            else if (DRV_ECC_INTR_PolicerControl == intr_bit)
            {
                *p_tblid = DsPolicerControl_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = PolicingRamChkRec_dsPolicerControlParityFailAddr_f;
                rec_fail_f = PolicingRamChkRec_dsPolicerControlParityFail_f;
            }
            else if (DRV_ECC_INTR_PolicerProfile == intr_bit)
            {
                *p_tblid = DsPolicerProfile_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = PolicingRamChkRec_dsPolicerProfileParityFailAddr_f;
                rec_fail_f = PolicingRamChkRec_dsPolicerProfileParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case QMgrDeqSliceInterruptNormal0_t:
        case QMgrDeqSliceInterruptNormal1_t:
            sid = intr_tblid - QMgrDeqSliceInterruptNormal0_t;
            err_rec = QMgrDeqSliceRamChkRec0_t + sid;
            if (DRV_ECC_INTR_ChanShpProfile == intr_bit)
            {
                *p_tblid = DsChanShpProfile_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrDeqSliceRamChkRec0_dsChanShpProfileParityFailAddr_f;
                rec_fail_f = QMgrDeqSliceRamChkRec0_dsChanShpProfileParityFail_f;
            }
            else if (DRV_ECC_INTR_QueShpProfId == intr_bit)
            {
                *p_tblid = DsQueShpProfId_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrDeqSliceRamChkRec0_dsQueShpProfIdParityFailAddr_f;
                rec_fail_f = QMgrDeqSliceRamChkRec0_dsQueShpProfIdParityFail_f;
            }
            else if (DRV_ECC_INTR_QueShpProfile == intr_bit)
            {
                *p_tblid = DsQueShpProfile_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrDeqSliceRamChkRec0_dsQueShpProfileParityFailAddr_f;
                rec_fail_f = QMgrDeqSliceRamChkRec0_dsQueShpProfileParityFail_f;
            }
            else if (DRV_ECC_INTR_GrpShpProfile == intr_bit)
            {
                *p_tblid = DsGrpShpProfile_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrDeqSliceRamChkRec0_dsGrpShpProfileParityFailAddr_f;
                rec_fail_f = QMgrDeqSliceRamChkRec0_dsGrpShpProfileParityFail_f;
            }
            else if (DRV_ECC_INTR_SchServiceProfile0 == intr_bit)
            {
                *p_tblid = DsSchServiceProfile_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = QMgrDeqSliceRamChkRec0_dsSchServiceProfile0ParityFailAddr_f;
                rec_fail_f = QMgrDeqSliceRamChkRec0_dsSchServiceProfile0ParityFail_f;
            }
            else if (DRV_ECC_INTR_SchServiceProfile1 == intr_bit)
            {
                sid = 1;
                *p_tblid = DsSchServiceProfile_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = QMgrDeqSliceRamChkRec0_dsSchServiceProfile0ParityFailAddr_f;
                rec_fail_f = QMgrDeqSliceRamChkRec0_dsSchServiceProfile0ParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case QMgrEnqInterruptNormal_t:
            err_rec = QMgrEnqRamChkRec_t;
            if (DRV_ECC_INTR_EgrResrcCtl == intr_bit)
            {
                *p_tblid = DsEgrResrcCtl_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrEnqRamChkRec_dsEgrResrcCtlParityFailAddr_f;
                rec_fail_f = QMgrEnqRamChkRec_dsEgrResrcCtlParityFail_f;
            }
            else if (DRV_ECC_INTR_LinkAggregateMember == intr_bit)
            {
                *p_tblid = DsLinkAggregateMember_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = QMgrEnqRamChkRec_dsLinkAggregateMemberParityFailAddr_f;
                rec_fail_f = QMgrEnqRamChkRec_dsLinkAggregateMemberParityFail_f;
            }
            else if (DRV_ECC_INTR_LinkAggregateMemberSet == intr_bit)
            {
                *p_tblid = DsLinkAggregateMemberSet_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrEnqRamChkRec_dsLinkAggregateMemberSetParityFailAddr_f;
                rec_fail_f = QMgrEnqRamChkRec_dsLinkAggregateMemberSetParityFail_f;
            }
            else if (DRV_ECC_INTR_LinkAggregationPort == intr_bit)
            {
                *p_tblid = DsLinkAggregationPort_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrEnqRamChkRec_dsLinkAggregationPortParityFailAddr_f;
                rec_fail_f = QMgrEnqRamChkRec_dsLinkAggregationPortParityFail_f;
            }
            else if (DRV_ECC_INTR_QueueNumGenCtl == intr_bit)
            {
                *p_tblid = DsQueueNumGenCtl_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrEnqRamChkRec_dsQueueNumGenCtlParityFailAddr_f;
                rec_fail_f = QMgrEnqRamChkRec_dsQueueNumGenCtlParityFail_f;
            }
            else if (DRV_ECC_INTR_QueueSelectMap == intr_bit)
            {
                *p_tblid = DsQueueSelectMap_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrEnqRamChkRec_dsQueueSelectMapParityFailAddr_f;
                rec_fail_f = QMgrEnqRamChkRec_dsQueueSelectMapParityFail_f;
            }
            else if (DRV_ECC_INTR_QueThrdProfile == intr_bit)
            {
                *p_tblid = DsQueThrdProfile_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrEnqRamChkRec_dsQueThrdProfileParityFailAddr_f;
                rec_fail_f = QMgrEnqRamChkRec_dsQueThrdProfileParityFail_f;
            }
            else if (DRV_ECC_INTR_LinkAggregateChannelMemberSet == intr_bit)
            {
                *p_tblid = DsLinkAggregateChannelMemberSet_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrEnqRamChkRec_dsLinkAggregateChannelMemberSetParityFailAddr_f;
                rec_fail_f = QMgrEnqRamChkRec_dsLinkAggregateChannelMemberSetParityFail_f;
            }
            else if (DRV_ECC_INTR_LinkAggregateChannelMember == intr_bit)
            {
                ignore_tbl = DsLinkAggregateChannelMember_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = QMgrEnqRamChkRec_dsLinkAggregateChannelMemberParityFailAddr_f;
                rec_fail_f = QMgrEnqRamChkRec_dsLinkAggregateChannelMemberParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case ShareDlbInterruptNormal_t:
            if (DRV_ECC_INTR_DlbFlowStateRight == intr_bit)
            {
                err_rec = ShareDlbRamChkRec_t;
                ignore_tbl = DsDlbFlowStateRight_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = ShareDlbRamChkRec_dsDlbFlowStateRightParityFailAddr_f;
                rec_fail_f = ShareDlbRamChkRec_dsDlbFlowStateRightParityFail_f;
            }
            if (DRV_ECC_INTR_DlbFlowStateLeft == intr_bit)
            {
                err_rec = ShareDlbRamChkRec_t;
                ignore_tbl = DsDlbFlowStateLeft_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = ShareDlbRamChkRec_dsDlbFlowStateLeftParityFailAddr_f;
                rec_fail_f = ShareDlbRamChkRec_dsDlbFlowStateLeftParityFail_f;
            }
            break;

        case BufRetrvInterruptFatal0_t:
        case BufRetrvInterruptFatal1_t:
            sid = intr_tblid - BufRetrvInterruptFatal0_t;
            err_rec = BufRetrvRamChkRec0_t + sid;
            if (DRV_ECC_INTR_bufRetrvBufPtrMem == intr_bit)
            {
                ignore_tbl = BufRetrvBufPtrMem0_t + sid;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = BufRetrvRamChkRec0_bufRetrvBufPtrMemParityFailAddr_f;
                rec_fail_f = BufRetrvRamChkRec0_bufRetrvBufPtrMemParityFail_f;
            }
            else if (DRV_ECC_INTR_bufRetrvPktMsgMem == intr_bit)
            {
                ignore_tbl = BufRetrvPktMsgMem0_t + sid;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = BufRetrvRamChkRec0_bufRetrvPktMsgMemParityFailAddr_f;
                rec_fail_f = BufRetrvRamChkRec0_bufRetrvPktMsgMemParityFail_f;
            }
            else if (DRV_ECC_INTR_bufRetrvMsgParkMem == intr_bit)
            {
                ignore_tbl = BufRetrvMsgParkMem0_t + sid;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = BufRetrvRamChkRec0_bufRetrvMsgParkMemParityFailAddr_f;
                rec_fail_f = BufRetrvRamChkRec0_bufRetrvMsgParkMemParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

         case BufStoreInterruptFatal_t:
            if (DRV_ECC_INTR_bufStoreFreeListLRam == intr_bit)
            {
                err_rec = BufStoreRamChkRec_t;
                ignore_tbl = BufStoreFreeListLRam_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = BufStoreRamChkRec_bufStoreFreeListLRamParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_bufStoreFreeListLRamParityFail_f;
            }
            else if (DRV_ECC_INTR_bufStoreFreeListHRam == intr_bit)
            {
                err_rec = BufStoreRamChkRec_t;
                ignore_tbl = BufStoreFreeListHRam_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = BufStoreRamChkRec_bufStoreFreeListHRamParityFailAddr_f;
                rec_fail_f = BufStoreRamChkRec_bufStoreFreeListHRamParityFail_f;
            }
            break;

        /* Fatal interrupt */
        case DsAgingInterruptFatal_t:
            err_rec = DsAgingRamChkRec_t;
            if (DRV_ECC_INTR_AgingStatusTcam == intr_bit)
            {
                ignore_tbl = DsAgingStatusTcam_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = DsAgingRamChkRec_dsAgingStatusTcamParityFailAddr_f;
                rec_fail_f = DsAgingRamChkRec_dsAgingStatusTcamParityFail_f;
            }
            else if (DRV_ECC_INTR_AgingStatusFib == intr_bit)
            {
                ignore_tbl = DsAgingStatusFib_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = DsAgingRamChkRec_dsAgingStatusFibParityFailAddr_f;
                rec_fail_f = DsAgingRamChkRec_dsAgingStatusFibParityFail_f;
            }
            else if (DRV_ECC_INTR_Aging == intr_bit)
            {
                *p_tblid = DsAging_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = DsAgingRamChkRec_dsAgingParityFailAddr_f;
                rec_fail_f = DsAgingRamChkRec_dsAgingParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case DynamicDsAdInterruptFatal0_t:
        case DynamicDsAdInterruptFatal1_t:
            sid = intr_tblid - DynamicDsAdInterruptFatal0_t;
            err_rec = DynamicDsAdRamChkRec0_t + sid;
            err_type = DRV_ECC_ERR_TYPE_MBE;
            if (DRV_ECC_INTR_IpMacRam0 == intr_bit)
            {
                rec_idx_f = DynamicDsAdRamChkRec0_dsIpMacRam0ParityFailAddr_f;
                rec_fail_f = DynamicDsAdRamChkRec0_dsIpMacRam0ParityFail_f;
            }
            else if (DRV_ECC_INTR_IpMacRam1 == intr_bit)
            {
                rec_idx_f = DynamicDsAdRamChkRec0_dsIpMacRam2ParityFailAddr_f;
                rec_fail_f = DynamicDsAdRamChkRec0_dsIpMacRam2ParityFail_f;
            }
            else if (DRV_ECC_INTR_IpMacRam2 == intr_bit)
            {
                rec_idx_f = DynamicDsAdRamChkRec0_dsIpMacRam2ParityFailAddr_f;
                rec_fail_f = DynamicDsAdRamChkRec0_dsIpMacRam2ParityFail_f;
            }
            else if (DRV_ECC_INTR_IpMacRam3 == intr_bit)
            {
                rec_idx_f = DynamicDsAdRamChkRec0_dsIpMacRam3ParityFailAddr_f;
                rec_fail_f = DynamicDsAdRamChkRec0_dsIpMacRam3ParityFail_f;
            }
            else if (DRV_ECC_INTR_NextHopMetRam0 == intr_bit)
            {
                rec_idx_f = DynamicDsAdRamChkRec0_nextHopMetRam0ParityFailAddr_f;
                rec_fail_f = DynamicDsAdRamChkRec0_nextHopMetRam0ParityFail_f;
            }
            else if (DRV_ECC_INTR_NextHopMetRam1 == intr_bit)
            {
                rec_idx_f = DynamicDsAdRamChkRec0_nextHopMetRam1ParityFailAddr_f;
                rec_fail_f = DynamicDsAdRamChkRec0_nextHopMetRam1ParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case DynamicDsHashInterruptFatal0_t:
        case DynamicDsHashInterruptFatal1_t:
            sid = intr_tblid - DynamicDsHashInterruptFatal0_t;
            err_rec = DynamicDsHashRamChkRec0_t + sid;
            err_type = DRV_ECC_ERR_TYPE_MBE;
            if (DRV_ECC_INTR_SharedRam0 == intr_bit)
            {
                rec_idx_f = DynamicDsHashRamChkRec0_sharedRam0ParityFailAddr_f;
                rec_fail_f = DynamicDsHashRamChkRec0_sharedRam0ParityFail_f;
            }
            else if (DRV_ECC_INTR_SharedRam1 == intr_bit)
            {
                rec_idx_f = DynamicDsHashRamChkRec0_sharedRam1ParityFailAddr_f;
                rec_fail_f = DynamicDsHashRamChkRec0_sharedRam1ParityFail_f;
            }
            else if (DRV_ECC_INTR_SharedRam2 == intr_bit)
            {
                rec_idx_f = DynamicDsHashRamChkRec0_sharedRam2ParityFailAddr_f;
                rec_fail_f = DynamicDsHashRamChkRec0_sharedRam2ParityFail_f;
            }
            else if (DRV_ECC_INTR_SharedRam3 == intr_bit)
            {
                rec_idx_f = DynamicDsHashRamChkRec0_sharedRam3ParityFailAddr_f;
                rec_fail_f = DynamicDsHashRamChkRec0_sharedRam3ParityFail_f;
            }
            else if (DRV_ECC_INTR_SharedRam4 == intr_bit)
            {
                rec_idx_f = DynamicDsHashRamChkRec0_sharedRam4ParityFailAddr_f;
                rec_fail_f = DynamicDsHashRamChkRec0_sharedRam4ParityFail_f;
            }
            else if (DRV_ECC_INTR_SharedRam5 == intr_bit)
            {
                rec_idx_f = DynamicDsHashRamChkRec0_sharedRam5ParityFailAddr_f;
                rec_fail_f = DynamicDsHashRamChkRec0_sharedRam5ParityFail_f;
            }
            else if (DRV_ECC_INTR_SharedRam6 == intr_bit)
            {
                rec_idx_f = DynamicDsHashRamChkRec0_sharedRam6ParityFailAddr_f;
                rec_fail_f = DynamicDsHashRamChkRec0_sharedRam6ParityFail_f;
            }
            else if (DRV_ECC_INTR_UserIdHashRam0 == intr_bit)
            {
                rec_idx_f = DynamicDsHashRamChkRec0_userIdHashRam0ParityFailAddr_f;
                rec_fail_f = DynamicDsHashRamChkRec0_userIdHashRam0ParityFail_f;
            }
            else if (DRV_ECC_INTR_UserIdHashRam1 == intr_bit)
            {
                rec_idx_f = DynamicDsHashRamChkRec0_userIdHashRam1ParityFailAddr_f;
                rec_fail_f = DynamicDsHashRamChkRec0_userIdHashRam1ParityFail_f;
            }
            else if (DRV_ECC_INTR_UserIdAdRam == intr_bit)
            {
                rec_idx_f = DynamicDsHashRamChkRec0_userIdAdRamParityFailAddr_f;
                rec_fail_f = DynamicDsHashRamChkRec0_userIdAdRamParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case DynamicDsShareInterruptFatal_t:
            if (DRV_ECC_INTR_L2L3EditRam0 == intr_bit)
            {
                err_rec = DynamicDsShareRamChkRec_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = DynamicDsShareRamChkRec_dsL2L3EditRam0ParityFailAddr_f;
                rec_fail_f = DynamicDsShareRamChkRec_dsL2L3EditRam0ParityFail_f;
            }
            else if (DRV_ECC_INTR_L2L3EditRam1 == intr_bit)
            {
                err_rec = DynamicDsShareRamChkRec_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = DynamicDsShareRamChkRec_dsL2L3EditRam1ParityFailAddr_f;
                rec_fail_f = DynamicDsShareRamChkRec_dsL2L3EditRam1ParityFail_f;
            }
            else if (DRV_ECC_INTR_Fwd == intr_bit)
            {
                err_rec = DynamicDsShareRamChkRec_t;
                *p_tblid = DsFwd_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = DynamicDsShareRamChkRec_dsFwdParityFailAddr_f;
                rec_fail_f = DynamicDsShareRamChkRec_dsFwdParityFail_f;
            }
            break;

        case EpeNextHopInterruptFatal0_t:
        case EpeNextHopInterruptFatal1_t:
            sid = intr_tblid - EpeNextHopInterruptFatal0_t;
            err_rec = EpeNextHopRamChkRec0_t + sid;
            if (DRV_ECC_INTR_DestInterface == intr_bit)
            {
                *p_tblid = DsDestInterface_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = EpeNextHopRamChkRec0_dsDestInterfaceParityFailAddr_f;
                rec_fail_f = EpeNextHopRamChkRec0_dsDestInterfaceParityFail_f;
            }
            else if (DRV_ECC_INTR_EgressVlanRangeProfile == intr_bit)
            {
                *p_tblid = DsEgressVlanRangeProfile_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = EpeNextHopRamChkRec0_dsEgressVlanRangeProfileParityFailAddr_f;
                rec_fail_f = EpeNextHopRamChkRec0_dsEgressVlanRangeProfileParityFail_f;
            }
            else if (DRV_ECC_INTR_EpeEditPriorityMap == intr_bit)
            {
                *p_tblid = EpeEditPriorityMap_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = EpeNextHopRamChkRec0_epeEditPriorityMapParityFailAddr_f;
                rec_fail_f = EpeNextHopRamChkRec0_epeEditPriorityMapParityFail_f;
            }
            else if (DRV_ECC_INTR_PortIsolation == intr_bit)
            {
                *p_tblid = DsPortIsolation_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = EpeNextHopRamChkRec0_dsPortIsolationParityFailAddr_f;
                rec_fail_f = EpeNextHopRamChkRec0_dsPortIsolationParityFail_f;
            }
            else if (DRV_ECC_INTR_DestPort == intr_bit)
            {
                *p_tblid = DsDestPort_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = EpeNextHopRamChkRec0_dsDestPortParityFailAddr_f;
                rec_fail_f = EpeNextHopRamChkRec0_dsDestPortParityFail_f;
            }
            else if (DRV_ECC_INTR_VlanTagBitMap == intr_bit)
            {
                *p_tblid = DsVlanTagBitMap_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = EpeNextHopRamChkRec0_dsVlanTagBitMapParityFailAddr_f;
                rec_fail_f = EpeNextHopRamChkRec0_dsVlanTagBitMapParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case MetFifoInterruptFatal_t:
            if (DRV_ECC_INTR_metMsgMem == intr_bit)
            {
                err_rec = MetFifoRamChkRec_t;
                ignore_tbl = MetMsgMem_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = MetFifoRamChkRec_metMsgMemParityFailAddr_f;
                rec_fail_f = MetFifoRamChkRec_metMsgMemParityFail_f;
            }
            else if (DRV_ECC_INTR_metRcdMem == intr_bit)
            {
                err_rec = MetFifoRamChkRec_t;
                ignore_tbl = MetRcdMem_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = MetFifoRamChkRec_metRcdMemParityFailAddr_f;
                rec_fail_f = MetFifoRamChkRec_metRcdMemParityFail_f;
            }
            break;

        case QMgrMsgStoreInterruptFatal_t:
            err_rec = QMgrMsgStoreRamChkRec_t;
            if (DRV_ECC_INTR_MsgFreePtr == intr_bit)
            {
                ignore_tbl = DsMsgFreePtr_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrMsgStoreRamChkRec_dsMsgFreePtrParityFailAddr_f;
                rec_fail_f = QMgrMsgStoreRamChkRec_dsMsgFreePtrParityFail_f;
            }
            else if (DRV_ECC_INTR_MsgLinkTail0 == intr_bit)
            {
                ignore_tbl = DsMsgLinkTail0_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrMsgStoreRamChkRec_dsMsgLinkTail0ParityFailAddr_f;
                rec_fail_f = QMgrMsgStoreRamChkRec_dsMsgLinkTail0ParityFail_f;
            }
            else if (DRV_ECC_INTR_MsgLinkTail1 == intr_bit)
            {
                ignore_tbl = DsMsgLinkTail1_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrMsgStoreRamChkRec_dsMsgLinkTail1ParityFailAddr_f;
                rec_fail_f = QMgrMsgStoreRamChkRec_dsMsgLinkTail1ParityFail_f;
            }
            else if (DRV_ECC_INTR_MsgUsedList0 == intr_bit)
            {
                ignore_tbl = DsMsgUsedList_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrMsgStoreRamChkRec_dsMsgUsedList0ParityFailAddr_f;
                rec_fail_f = QMgrMsgStoreRamChkRec_dsMsgUsedList0ParityFail_f;
            }
            else if (DRV_ECC_INTR_MsgUsedList1 == intr_bit)
            {
                ignore_tbl = DsMsgUsedList_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = QMgrMsgStoreRamChkRec_dsMsgUsedList1ParityFailAddr_f;
                rec_fail_f = QMgrMsgStoreRamChkRec_dsMsgUsedList1ParityFail_f;
            }
            else if (DRV_ECC_INTR_MsgLinkHead0 == intr_bit)
            {
                ignore_tbl = DsMsgLinkHead0_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = QMgrMsgStoreRamChkRec_dsMsgLinkHead0ParityFailAddr_f;
                rec_fail_f = QMgrMsgStoreRamChkRec_dsMsgLinkHead0ParityFail_f;
            }
            else if (DRV_ECC_INTR_MsgLinkHead1 == intr_bit)
            {
                ignore_tbl = DsMsgLinkHead1_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = QMgrMsgStoreRamChkRec_dsMsgLinkHead1ParityFailAddr_f;
                rec_fail_f = QMgrMsgStoreRamChkRec_dsMsgLinkHead1ParityFail_f;
            }
            else if (DRV_ECC_INTR_MsgLinkCache == intr_bit)
            {
                ignore_tbl = DsMsgLinkCache_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                rec_idx_f = QMgrMsgStoreRamChkRec_dsMsgLinkCacheParityFailAddr_f;
                rec_fail_f = QMgrMsgStoreRamChkRec_dsMsgLinkCacheParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case ShareEfdInterruptFatal_t:
            err_rec = ShareEfdRamChkRec_t;
            if (DRV_ECC_INTR_ElephantFlowDetect0 == intr_bit)
            {
                ignore_tbl = DsElephantFlowDetect0_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = ShareEfdRamChkRec_dsElephantFlowDetect0ParityFailAddr_f;
                rec_fail_f = ShareEfdRamChkRec_dsElephantFlowDetect0ParityFail_f;
            }
            else if (DRV_ECC_INTR_ElephantFlowDetect1 == intr_bit)
            {
                ignore_tbl = DsElephantFlowDetect1_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = ShareEfdRamChkRec_dsElephantFlowDetect1ParityFailAddr_f;
                rec_fail_f = ShareEfdRamChkRec_dsElephantFlowDetect1ParityFail_f;
            }
            else if (DRV_ECC_INTR_ElephantFlowDetect2 == intr_bit)
            {
                ignore_tbl = DsElephantFlowDetect2_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = ShareEfdRamChkRec_dsElephantFlowDetect2ParityFailAddr_f;
                rec_fail_f = ShareEfdRamChkRec_dsElephantFlowDetect2ParityFail_f;
            }
            else if (DRV_ECC_INTR_ElephantFlowDetect3 == intr_bit)
            {
                ignore_tbl = DsElephantFlowDetect3_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = ShareEfdRamChkRec_dsElephantFlowDetect3ParityFailAddr_f;
                rec_fail_f = ShareEfdRamChkRec_dsElephantFlowDetect3ParityFail_f;
            }
            else if (DRV_ECC_INTR_ElephantFlowState == intr_bit)
            {
                ignore_tbl = DsElephantFlowState_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = ShareEfdRamChkRec_dsElephantFlowStateParityFailAddr_f;
                rec_fail_f = ShareEfdRamChkRec_dsElephantFlowStateParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case ShareStormCtlInterruptFatal_t:
            err_rec = ShareStormCtlRamChkRec_t;
            if (DRV_ECC_INTR_VlanStormCtl == intr_bit)
            {
                *p_tblid = DsVlanStormCtl_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = ShareStormCtlRamChkRec_dsVlanStormCtlParityFailAddr_f;
                rec_fail_f = ShareStormCtlRamChkRec_dsVlanStormCtlParityFail_f;
            }
            else if (DRV_ECC_INTR_PortStormCtl == intr_bit)
            {
                *p_tblid = DsPortStormCtl_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = ShareStormCtlRamChkRec_dsPortStormCtlParityFailAddr_f;
                rec_fail_f = ShareStormCtlRamChkRec_dsPortStormCtlParityFail_f;
            }
            else if (DRV_ECC_INTR_VlanStormCount == intr_bit)
            {
                ignore_tbl = DsVlanStormCount_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = ShareStormCtlRamChkRec_dsVlanStormCountParityFailAddr_f;
                rec_fail_f = ShareStormCtlRamChkRec_dsVlanStormCountParityFail_f;
            }
            else if (DRV_ECC_INTR_PortStormCount == intr_bit)
            {
                ignore_tbl = DsPortStormCount_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                rec_idx_f = ShareStormCtlRamChkRec_dsPortStormCountParityFailAddr_f;
                rec_fail_f = ShareStormCtlRamChkRec_dsPortStormCountParityFail_f;
            }
            else
            {
                err_rec = MaxTblId_t;
            }
            break;

        case OamProcInterruptNormal_t:
            if (DRV_ECC_INTR_BfdV6Addr == intr_bit)
            {
                *p_tblid = DsBfdV6Addr_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                err_rec = OamProcRamChkRec_t;
                rec_idx_f = OamProcRamChkRec_dsBfdV6AddrParityFailAddr_f;
                rec_fail_f = OamProcRamChkRec_dsBfdV6AddrParityFail_f;
            }
            else if (DRV_ECC_INTR_PortProperty == intr_bit)
            {
                *p_tblid = DsPortProperty_t;
                err_type = DRV_ECC_ERR_TYPE_PARITY_ERROR;
                err_rec = OamProcRamChkRec_t;
                rec_idx_f = OamProcRamChkRec_dsPortPropertyParityFailAddr_f;
                rec_fail_f = OamProcRamChkRec_dsPortPropertyParityFail_f;
            }
            break;

        case OamAutoGenPktInterruptFatal_t:
            if (DRV_ECC_INTR_autoGenPktPktHdr == intr_bit)
            {
                *p_tblid = AutoGenPktPktHdr_t;
                err_type = DRV_ECC_ERR_TYPE_MBE;
                err_rec = OamAutoGenPktRamChkRec_t;
                rec_idx_f = OamAutoGenPktRamChkRec_autoGenPktPktHdrParityFailAddr_f;
                rec_fail_f = OamAutoGenPktRamChkRec_autoGenPktPktHdrParityFail_f;
            }
            break;

         default:
            break;
    }

    /* The interrupt is other. */
    if (MaxTblId_t == err_rec)
    {
        *p_tbl_type = DRV_ECC_TBL_TYPE_OTHER;
        return DRV_E_NONE;
    }

    /* 2. Read ecc fail rec to get fail valid bit and ecc error tbl idx. */
    chip_id = lchip + drv_gg_init_chip_info.drv_init_chipid_base;
    cmd = DRV_IOR(err_rec, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, data));
    DRV_IF_ERROR_RETURN(DRV_GET_FIELD(err_rec, rec_fail_f, data, &fail));

    if (!fail)
    {
        DRV_DBG_INFO("%s's intr ecc err bit[%u] mismatch with %s's fail bit \n",
                     TABLE_NAME(intr_tblid), intr_bit, TABLE_NAME(err_rec));
        return DRV_E_FATAL_EXCEP;
    }

    DRV_IF_ERROR_RETURN(DRV_GET_FIELD(err_rec, rec_idx_f, data, p_offset));

    *p_err_type = err_type;

    if ((MaxTblId_t != ignore_tbl) || (MaxTblId_t != *p_tblid))
    {
        *p_tbl_type = (MaxTblId_t != ignore_tbl) ? DRV_ECC_TBL_TYPE_IGNORE : DRV_ECC_TBL_TYPE_STATIC;
        *p_tblid    = (MaxTblId_t != ignore_tbl) ? ignore_tbl : *p_tblid;

        if (TABLE_INFO_PTR(*p_tblid)
           && (SLICE_Cascade == TABLE_ENTRY_TYPE(*p_tblid)))
        {
            *p_offset += (TABLE_MAX_INDEX(*p_tblid) / 2) * sid;
        }
    }
    else
    {
        *p_tbl_type = DRV_ECC_TBL_TYPE_DYNAMIC;
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_ecc_recover_static_tbl(uint8 lchip, tbls_id_t tblid, uint32 tblix)
{
    uint32  entry_size = 0, offset = 0;
    uint32 start_hw_data_addr = 0;
    uintptr start_sw_data_addr = 0;
    drv_ecc_mem_type_t mem_type = DRV_ECC_MEM_INVALID;

    _drv_goldengate_ecc_recover_tbl2mem(tblid, tblix, &mem_type, &offset);

    if ((mem_type >= DRV_ECC_MEM_IgrPortTcPriMap) && (mem_type <= DRV_ECC_MEM_AutoGenPktPktHdr))
    {
        entry_size = TABLE_ENTRY_SIZE(tblid);

        DRV_IF_ERROR_RETURN(drv_goldengate_table_get_hw_addr(tblid, tblix, &start_hw_data_addr, FALSE));
        start_sw_data_addr = g_gg_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip] + tblix * entry_size;

        DRV_IF_ERROR_RETURN(drv_goldengate_chip_write_sram_entry(lchip, start_hw_data_addr,
                                                      (uint32*)start_sw_data_addr, entry_size));

        g_gg_ecc_recover_master->ecc_mem[mem_type].recover_cnt[lchip]++;
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_ecc_recover_dynamic_tbl(uint8 lchip, tbls_id_t intr_tblid, uint32 intr_bit, uint32 offset, drv_ecc_err_type_t err_type, drv_ecc_cb_info_t* p_ecc_cb_info)
{
    uint8 recover = 1, step = 0, i = 0, j = 0, valid = 0, req_type = 0, mem_blk_id = 0, chip_id = 0;
    uint8 per_entry_bytes = 0, per_entry_addr = 0, hash_type = 0, req_valid = 0, tbl_size = 0;
    tbls_id_t tblid = MaxTblId_t;
    uint32 start_hw_data_addr = 0, tbl_base_offset = 0, err_addr_offset = 0, err_offset_entry = 0;
    uint32 blknum = 0, hw_base = 0, tbl_idx = 0, local_idx = 0, is_sec_half = 0;
    uintptr start_sw_data_addr = 0;
    drv_ecc_mem_type_t mem_type = DRV_ECC_MEM_INVALID;
    FibHashKeyCpuResult_m fib_hash_key_cpu_result;
    FibHashKeyCpuReq_m fib_hash_key_cpu_req;
    uint32* p_fib_hash_key_cpu_req = NULL;
    drv_fib_acc_rw_data_t rw_data = {0};
    hw_mac_addr_t hw_mac = {0};
    tbls_id_t key_tbl[] = {DsFibHost0FcoeHashKey_t,      DsFibHost0Ipv4HashKey_t,
                           DsFibHost0Ipv6McastHashKey_t, DsFibHost0Ipv6UcastHashKey_t,
                           DsFibHost0MacHashKey_t,       DsFibHost0MacIpv6McastHashKey_t,
                           DsFibHost0TrillHashKey_t};

    uint32 dynamic_ram_base[18][3] = {
        {DRV_SHARE_RAM0_BASE_4W        ,DRV_SHARE_RAM0_BASE_6W        ,DRV_SHARE_RAM0_BASE_12W        },
        {DRV_SHARE_RAM1_BASE_4W        ,DRV_SHARE_RAM1_BASE_6W        ,DRV_SHARE_RAM1_BASE_12W        },
        {DRV_SHARE_RAM2_BASE_4W        ,DRV_SHARE_RAM2_BASE_6W        ,DRV_SHARE_RAM2_BASE_12W        },
        {DRV_SHARE_RAM3_BASE_4W        ,DRV_SHARE_RAM3_BASE_6W        ,DRV_SHARE_RAM3_BASE_12W        },
        {DRV_SHARE_RAM4_BASE_4W        ,DRV_SHARE_RAM4_BASE_6W        ,DRV_SHARE_RAM4_BASE_12W        },
        {DRV_SHARE_RAM5_BASE_4W        ,DRV_SHARE_RAM5_BASE_6W        ,DRV_SHARE_RAM5_BASE_12W        },
        {DRV_SHARE_RAM6_BASE_4W        ,DRV_SHARE_RAM6_BASE_6W        ,DRV_SHARE_RAM6_BASE_12W        },
        {DRV_DS_IPMAC_RAM0_BASE_4W     ,DRV_DS_IPMAC_RAM0_BASE_6W     ,DRV_DS_IPMAC_RAM0_BASE_12W     },
        {DRV_DS_IPMAC_RAM1_BASE_4W     ,DRV_DS_IPMAC_RAM1_BASE_6W     ,DRV_DS_IPMAC_RAM1_BASE_12W     },
        {DRV_DS_IPMAC_RAM2_BASE_4W     ,DRV_DS_IPMAC_RAM2_BASE_6W     ,DRV_DS_IPMAC_RAM2_BASE_12W     },
        {DRV_DS_IPMAC_RAM3_BASE_4W     ,DRV_DS_IPMAC_RAM3_BASE_6W     ,DRV_DS_IPMAC_RAM3_BASE_12W     },
        {DRV_USERIDHASHKEY_RAM0_BASE_4W,DRV_USERIDHASHKEY_RAM0_BASE_6W,DRV_USERIDHASHKEY_RAM0_BASE_12W},
        {DRV_USERIDHASHKEY_RAM1_BASE_4W,DRV_USERIDHASHKEY_RAM1_BASE_6W,DRV_USERIDHASHKEY_RAM1_BASE_12W},
        {DRV_USERIDHASHAD_RAM_BASE_4W  ,DRV_USERIDHASHAD_RAM_BASE_6W  ,DRV_USERIDHASHAD_RAM_BASE_12W  },
        {DRV_L23EDITRAM0_BASE_4W       ,DRV_L23EDITRAM0_BASE_6W       ,DRV_L23EDITRAM0_BASE_12W       },
        {DRV_L23EDITRAM1_BASE_4W       ,DRV_L23EDITRAM1_BASE_6W       ,DRV_L23EDITRAM1_BASE_12W       },
        {DRV_NEXTHOPMET_RAM0_BASE_4W   ,DRV_NEXTHOPMET_RAM0_BASE_6W   ,DRV_NEXTHOPMET_RAM0_BASE_12W   },
        {DRV_NEXTHOPMET_RAM1_BASE_4W   ,DRV_NEXTHOPMET_RAM1_BASE_6W   ,DRV_NEXTHOPMET_RAM1_BASE_12W   }
    };

    chip_id = lchip + drv_gg_init_chip_info.drv_init_chipid_base;

    if ((DynamicDsAdInterruptFatal0_t == intr_tblid)
       || (DynamicDsAdInterruptFatal1_t == intr_tblid))
    {
        switch (intr_bit)
        {
            case DRV_ECC_INTR_IpMacRam0:
                mem_type = DRV_ECC_MEM_IpMacRam0;
                break;
            case DRV_ECC_INTR_IpMacRam1:
                mem_type = DRV_ECC_MEM_IpMacRam1;
                break;
            case DRV_ECC_INTR_IpMacRam2:
                mem_type = DRV_ECC_MEM_IpMacRam2;
                break;
            case DRV_ECC_INTR_IpMacRam3:
                mem_type = DRV_ECC_MEM_IpMacRam3;
                break;
            case DRV_ECC_INTR_NextHopMetRam0:
                mem_type = DRV_ECC_MEM_NextHopMetRam0;
                break;
            case DRV_ECC_INTR_NextHopMetRam1:
                mem_type = DRV_ECC_MEM_NextHopMetRam0;
                break;
            default:
                break;
        }
    }
    else if ((DynamicDsHashInterruptFatal0_t == intr_tblid)
            || (DynamicDsHashInterruptFatal1_t == intr_tblid))
    {
        switch (intr_bit)
        {
            case DRV_ECC_INTR_SharedRam0:
                mem_type = DRV_ECC_MEM_SharedRam0;
                break;
            case DRV_ECC_INTR_SharedRam1:
                mem_type = DRV_ECC_MEM_SharedRam1;
                break;
            case DRV_ECC_INTR_SharedRam2:
                mem_type = DRV_ECC_MEM_SharedRam2;
                break;
            case DRV_ECC_INTR_SharedRam3:
                mem_type = DRV_ECC_MEM_SharedRam3;
                break;
            case DRV_ECC_INTR_SharedRam4:
                mem_type = DRV_ECC_MEM_SharedRam4;
                break;
            case DRV_ECC_INTR_SharedRam5:
                mem_type = DRV_ECC_MEM_SharedRam5;
                break;
            case DRV_ECC_INTR_SharedRam6:
                mem_type = DRV_ECC_MEM_SharedRam6;
                break;
            case DRV_ECC_INTR_UserIdHashRam0:
                mem_type = DRV_ECC_MEM_UserIdHashRam0;
                break;
            case DRV_ECC_INTR_UserIdHashRam1:
                mem_type = DRV_ECC_MEM_UserIdHashRam1;
                break;
            case DRV_ECC_INTR_UserIdAdRam:
                mem_type = DRV_ECC_MEM_UserIdAdRam;
                break;
            default:
                break;
        }
    }
    else if (DynamicDsShareInterruptFatal_t == intr_tblid)
    {
        switch (intr_bit)
        {
            case DRV_ECC_INTR_L2L3EditRam0:
                mem_type = DRV_ECC_MEM_L2L3EditRam0;
                break;
            case DRV_ECC_INTR_L2L3EditRam1:
                mem_type = DRV_ECC_MEM_L2L3EditRam1;
                break;
            default:
                break;
        }
    }
    else if ((LpmTcamIpInterruptNormal_t == intr_tblid)
            && (DRV_ECC_INTR_LpmTcamIpAdRam == intr_bit))
    {
        mem_type = DRV_ECC_MEM_LpmTcamIpAdRam;
    }
    else if ((LpmTcamNatInterruptNormal0_t == intr_tblid)
            || (LpmTcamNatInterruptNormal1_t == intr_tblid))
    {
        mem_type = DRV_ECC_MEM_LpmTcamNatAdRam;
    }
    else if (FlowTcamInterruptNormal_t == intr_tblid)
    {
        switch (intr_bit)
        {
            case DRV_ECC_INTR_FlowTcamAd0:
                mem_type = DRV_ECC_MEM_FlowTcamAd0;
                break;
            case DRV_ECC_INTR_FlowTcamAd1:
                mem_type = DRV_ECC_MEM_FlowTcamAd1;
                break;
            case DRV_ECC_INTR_FlowTcamAd2:
                mem_type = DRV_ECC_MEM_FlowTcamAd2;
                break;
            case DRV_ECC_INTR_FlowTcamAd3:
                mem_type = DRV_ECC_MEM_FlowTcamAd3;
                break;
            case DRV_ECC_INTR_FlowTcamAd4:
                mem_type = DRV_ECC_MEM_FlowTcamAd4;
                break;
            case DRV_ECC_INTR_FlowTcamAd5:
                mem_type = DRV_ECC_MEM_FlowTcamAd5;
                break;
            case DRV_ECC_INTR_FlowTcamAd6:
                mem_type = DRV_ECC_MEM_FlowTcamAd6;
                break;
            default:
                break;
        }
    }

    if (DRV_ECC_MEM_INVALID == mem_type)
    {
        return DRV_E_FATAL_EXCEP;
    }

    if (DRV_ECC_MEM_LpmTcamIpAdRam == mem_type)
    {
        per_entry_bytes = DRV_LPM_AD0_BYTE_PER_ENTRY;
        per_entry_addr = DRV_LPM_AD0_BYTE_PER_ENTRY;
    }
    else if (DRV_ECC_MEM_LpmTcamNatAdRam == mem_type)
    {
        per_entry_bytes = DRV_LPM_AD1_BYTE_PER_ENTRY;
        per_entry_addr = DRV_LPM_AD1_BYTE_PER_ENTRY;
    }
    else
    {
        per_entry_bytes = DRV_BYTES_PER_ENTRY;
        per_entry_addr = DRV_ADDR_BYTES_PER_ENTRY;
    }

    if (((mem_type >= DRV_ECC_MEM_FlowTcamAd0)
       && (mem_type <= DRV_ECC_MEM_FlowTcamAd6))
       || (DRV_ECC_MEM_UserIdAdRam == mem_type))
    {
        step = 2;
    }
    else if ((DRV_ECC_MEM_LpmTcamIpAdRam == mem_type)
            || (DRV_ECC_MEM_LpmTcamNatAdRam == mem_type))
    {
        step = 1;
    }
    else
    {
        step = 4;
    }

    if ((mem_type >= DRV_ECC_MEM_FlowTcamAd0) && (mem_type <= DRV_ECC_MEM_LpmTcamNatAdRam))
    {
        mem_blk_id = 29 + mem_type - DRV_ECC_MEM_FlowTcamAd0;
    }
    else
    {
        mem_blk_id = mem_type - DRV_ECC_MEM_SharedRam0;
    }

    drv_goldengate_ftm_check_tbl_recover(mem_blk_id, offset * step, &recover, &tblid);

    if (tblid == MaxTblId_t)
    {
        return DRV_E_INVALID_TBL;
    }
    if (1 == recover)
    {
        if (IS_ACC_KEY(tblid))
        {
            per_entry_bytes = sizeof(FibHashKeyCpuReq_m);

            start_sw_data_addr = g_gg_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip]
                                 + offset * (per_entry_bytes * step);

            for (i = 0; i < step;)
            {
                p_fib_hash_key_cpu_req = (uint32*)((uint8*)start_sw_data_addr + i * per_entry_bytes);
                req_valid = GetFibHashKeyCpuReq(V, hashKeyCpuReqValid_f, p_fib_hash_key_cpu_req);

                tbl_idx = DYNAMIC_START_INDEX(DsFibHost0MacHashKey_t, mem_type - DRV_ECC_MEM_SharedRam0)
                         + (offset * step) + i;

                if (!req_valid)
                {
                    if (0 != g_gg_ecc_recover_master->hw_learning_en)
                    {
                        /* hw learning should reset error memory using empty data */
                        sal_memset(&fib_hash_key_cpu_req, 0, sizeof(FibHashKeyCpuReq_m));
                        /* DRV_FIB_ACC_DEL_MAC_BY_IDX */
                        SetFibHashKeyCpuReq(V, hashKeyCpuReqType_f, &fib_hash_key_cpu_req, 2);
                        SetFibHashKeyCpuReq(A, cpuKeyMac_f, &fib_hash_key_cpu_req, hw_mac);
                        SetFibHashKeyCpuReq(V, cpuKeyVrfId_f, &fib_hash_key_cpu_req, 0);
                        SetFibHashKeyCpuReq(V, cpuKeyDsAdIndex_f, &fib_hash_key_cpu_req, 0);
                        SetFibHashKeyCpuReq(A, writeData_f, &fib_hash_key_cpu_req, rw_data);
                        SetFibHashKeyCpuReq(V, cpuKeyIndex_f, &fib_hash_key_cpu_req, tbl_idx + FIB_HOST0_CAM_NUM);
                        drv_goldengate_chip_fib_acc_process(lchip, p_fib_hash_key_cpu_req,
                                                (uint32*)&fib_hash_key_cpu_result, 0);
                    }
                    i++;
                    continue;
                }

                req_type = GetFibHashKeyCpuReq(V, hashKeyCpuReqType_f, p_fib_hash_key_cpu_req);

                /* DRV_FIB_ACC_WRITE_MAC_BY_KEY */
                /* DRV_FIB_ACC_DEL_MAC_BY_KEY   */
                /* DRV_FIB_ACC_WRITE_MAC_BY_IDX */
                if ((1 == req_type) || (3 == req_type) || (0 == req_type))
                {
                    tblid = DsFibHost0MacHashKey_t;

                    /* DRV_FIB_ACC_DEL_MAC_BY_KEY */
                    if (3 == req_type)
                    {
                        /* DRV_FIB_ACC_DEL_MAC_BY_IDX */
                        SetFibHashKeyCpuReq(V, hashKeyCpuReqType_f, p_fib_hash_key_cpu_req, 2);
                        SetFibHashKeyCpuReq(A, cpuKeyMac_f, p_fib_hash_key_cpu_req, hw_mac);
                        SetFibHashKeyCpuReq(V, cpuKeyVrfId_f, p_fib_hash_key_cpu_req, 0);
                        SetFibHashKeyCpuReq(V, cpuKeyDsAdIndex_f, p_fib_hash_key_cpu_req, 0);
                        SetFibHashKeyCpuReq(A, writeData_f, p_fib_hash_key_cpu_req, rw_data);
                    }
                    /* DRV_FIB_ACC_WRITE_MAC_BY_KEY */
                    else
                    {
                        /* DRV_FIB_ACC_WRITE_MAC_BY_IDX */
                        SetFibHashKeyCpuReq(V, hashKeyCpuReqType_f, p_fib_hash_key_cpu_req, 0);
                    }
                    drv_goldengate_chip_fib_acc_process(lchip, p_fib_hash_key_cpu_req,
                                            (uint32*)&fib_hash_key_cpu_result, 0);
                    i++;
                }
                else
                {
                    hash_type = GetDsFibHost0Ipv4HashKey(V, hashType_f, p_fib_hash_key_cpu_req);

                    if (hash_type > FIBHOST0PRIMARYHASHTYPE_TRILL)
                    {
                        DRV_DBG_INFO("Fib host0 error hash type!\n");
                        return DRV_E_FATAL_EXCEP;
                    }

                    tblid = key_tbl[hash_type];
                    valid = GetDsFibHost0MacHashKey(V, valid_f, p_fib_hash_key_cpu_req);

                    /****************************************************************/
                    /*           +----------++----------++----------++----------+   */
                    /*           |    0     ||     1    ||     2    ||     3    |   */
                    /*           +----------++----------++----------++----------+   */
                    /*                                                              */
                    /* 1  Del    +----------------------------------------------+   */
                    /*                                                              */
                    /*    Add                +----------+            +----------+   */
                    /*                                                              */
                    /* 2  Add    +----------------------------------------------+   */
                    /*                                                              */
                    /*    Del                +----------+            +----------+   */
                    /*                                                              */
                    /****************************************************************/

                    if (0 == valid)
                    {
                        /* case 1 */
                        i++;
                        SetFibHashKeyCpuReq(A, writeData_f, p_fib_hash_key_cpu_req, rw_data);
                    }
                    else
                    {
                        /* case 2 */
                        i += TABLE_ENTRY_SIZE(tblid) / DRV_BYTES_PER_ENTRY;
                    }
                    /* DRV_FIB_ACC_WRITE_FIB0_BY_IDX */
                    SetFibHashKeyCpuReq(V, hashKeyCpuReqType_f, p_fib_hash_key_cpu_req, 9);
                    SetFibHashKeyCpuReq(V, cpuKeyMac_f, p_fib_hash_key_cpu_req, tbl_idx + FIB_HOST0_CAM_NUM);
                    drv_goldengate_chip_fib_acc_process(lchip, p_fib_hash_key_cpu_req,
                                             (uint32*)&fib_hash_key_cpu_result, 0);
                }
            }
        }
        else
        {
            err_addr_offset = offset * (per_entry_addr * step);

            if ((mem_type >= DRV_ECC_MEM_SharedRam0) && (mem_type <= DRV_ECC_MEM_NextHopMetRam1))
            {
                start_sw_data_addr = g_gg_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip]
                                     + offset * (per_entry_bytes * step);

                if (TABLE_ENTRY_SIZE(tblid) > (step * DRV_BYTES_PER_ENTRY))
                {
                    DRV_DBG_INFO("%s invalid tbl entry size:%u\n", TABLE_NAME(tblid), TABLE_ENTRY_SIZE(tblid));
                    return DRV_E_FATAL_EXCEP;
                }

                mem_blk_id = mem_type - DRV_ECC_MEM_SharedRam0;
                /* 0:3w, 1:6w, 2:12w */
                tbl_size = (TABLE_ENTRY_SIZE(tblid) / DRV_BYTES_PER_ENTRY) / 2;
                hw_base = dynamic_ram_base[mem_blk_id][tbl_size];
                tbl_idx = DYNAMIC_START_INDEX(tblid, mem_blk_id);
                tbl_base_offset = DYNAMIC_DATA_BASE(tblid, mem_blk_id, 0) - hw_base;
                err_offset_entry = (err_addr_offset - tbl_base_offset) / per_entry_addr;
                tbl_idx += err_offset_entry;

                j = 0;
                for (i = 0; i < step; i += (TABLE_ENTRY_SIZE(tblid) / DRV_BYTES_PER_ENTRY))
                {
                    DRV_IF_ERROR_RETURN(drv_goldengate_table_get_hw_addr(tblid, tbl_idx + i, &start_hw_data_addr, FALSE));
                    DRV_IF_ERROR_RETURN(drv_goldengate_chip_write_sram_entry(lchip, (uint32)start_hw_data_addr,
                                       (uint32*)((uint8*)start_sw_data_addr + (TABLE_ENTRY_SIZE(tblid) * j)),
                                        TABLE_ENTRY_SIZE(tblid)));
                    j++;
                }
            }
            else
            {
                mem_blk_id = mem_type - DRV_ECC_MEM_FlowTcamAd0;
                if ((mem_type >= DRV_ECC_MEM_FlowTcamAd0) && (mem_type <= DRV_ECC_MEM_FlowTcamAd6))
                {

                    tbl_idx = TCAM_START_INDEX(tblid, mem_blk_id);
                    tbl_base_offset = TCAM_DATA_BASE(tblid, mem_blk_id, 0) - tcam_ad_base[mem_blk_id];
                    err_offset_entry = (err_addr_offset - tbl_base_offset) / per_entry_addr;
                    tbl_idx += err_offset_entry / (TABLE_ENTRY_SIZE(FlowTcamAdMem_t) / per_entry_bytes);
                    DRV_IF_ERROR_RETURN(drv_goldengate_chip_flow_tcam_get_blknum_index(chip_id, tblid, tbl_idx, &blknum, &local_idx, &is_sec_half));

                    if (mem_blk_id != blknum)
                    {
                        DRV_DBG_INFO("Intr tcam ad blkid:%u mismatch with driver blkid:%u\n", mem_blk_id, blknum);
                        return DRV_E_FATAL_EXCEP;
                    }

                    start_sw_data_addr = g_gg_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip] + local_idx * TABLE_ENTRY_SIZE(tblid);
                    DRV_IF_ERROR_RETURN(drv_goldengate_chip_write_flow_tcam_ad_entry(chip_id, blknum, local_idx, (uint32*)start_sw_data_addr));
                }
                else
                {
                    start_sw_data_addr = g_gg_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip]
                                         + offset * (per_entry_bytes * step);

                    if (DRV_ECC_MEM_LpmTcamIpAdRam == mem_type)
                    {
                        tbl_base_offset = TABLE_DATA_BASE(tblid, 0) - tcam_ad_base[mem_blk_id];
                        err_offset_entry = (err_addr_offset - tbl_base_offset) / per_entry_addr;
                        tbl_idx = err_offset_entry / (TABLE_ENTRY_SIZE(DsLpmTcamIpv4Route40Ad_t) / per_entry_bytes);
                    }
                    else if (DRV_ECC_MEM_LpmTcamNatAdRam == mem_type)
                    {
                        tbl_base_offset = TABLE_DATA_BASE(tblid, 0) - tcam_ad_base[mem_blk_id];
                        err_offset_entry = (err_addr_offset - tbl_base_offset) / per_entry_addr;
                        tbl_idx = err_offset_entry / (TABLE_ENTRY_SIZE(DsLpmTcamIpv4Nat160Ad_t) / per_entry_bytes);
                    }
                    else
                    {
                        DRV_DBG_INFO("%s invalid tcam ad tbl\n", TABLE_NAME(tblid));
                        return DRV_E_FATAL_EXCEP;
                    }

                    DRV_IF_ERROR_RETURN(drv_goldengate_table_get_hw_addr(tblid, tbl_idx, &start_hw_data_addr, FALSE));
                    DRV_IF_ERROR_RETURN(drv_goldengate_chip_write_sram_entry(lchip, (uint32)start_hw_data_addr,
                                       (uint32*)start_sw_data_addr, per_entry_bytes * step));
                }
            }
        }
        g_gg_ecc_recover_master->ecc_mem[mem_type].recover_cnt[lchip]++;
    }
    else
    {
        g_gg_ecc_recover_master->ignore_cnt[lchip]++;
    }

    p_ecc_cb_info->recover = (0 != recover) ? 1 : 0;

    if ((mem_type >= DRV_ECC_MEM_SharedRam0)
       && (mem_type <= DRV_ECC_MEM_NextHopMetRam1))
    {
        p_ecc_cb_info->tbl_id = 0xFF000000 + mem_type - DRV_ECC_MEM_SharedRam0;
    }
    else if ((mem_type >= DRV_ECC_MEM_FlowTcamAd0)
            && (mem_type <= DRV_ECC_MEM_LpmTcamNatAdRam))
    {
        p_ecc_cb_info->tbl_id = 0xFD000000 + mem_type - DRV_ECC_MEM_FlowTcamAd0;
    }

    p_ecc_cb_info->tbl_id |= ((step * per_entry_bytes) << 16);
    p_ecc_cb_info->type = err_type;
    p_ecc_cb_info->tbl_index = offset * step * per_entry_addr;

    return DRV_E_NONE;
}

#define ___ECC_RECOVER_SCAN___

STATIC void
_drv_goldengate_ecc_recover_scan_sbe_thread(void* param)
{
    uint8 lchip = 0, chip_id = 0;
    drv_ecc_cb_info_t  ecc_cb_info;
    uint32 i = 0, cmd = 0, val = 0;
    uint32 data[DRV_PE_MAX_ENTRY_WORD] = {0};
    drv_scan_thread_t* p_thread_info = (drv_scan_thread_t*)param;
    char* p_time_str = NULL;
    sal_time_t tm;

    sal_task_set_priority(p_thread_info->prio);

    sal_time(&tm);
    p_time_str = sal_ctime(&tm);
    DRV_DBG_INFO("\nStart single bit error scan thread time: %s", p_time_str);

    do
    {
        for (i = 0; (MaxTblId_t != ecc_sbe_cnt[i].tblid) || (NULL != ecc_sbe_cnt[i].p_tbl_name); i++)
        {
            /* driver error */
            if (DmaCtlParityStatus1_t == ecc_sbe_cnt[i].rec)
            {
                continue;
            }

            for (lchip = 0; lchip < drv_gg_init_chip_info.drv_init_chip_num; lchip++)
            {
                chip_id = lchip + drv_gg_init_chip_info.drv_init_chipid_base;
                cmd = DRV_IOR(ecc_sbe_cnt[i].rec, DRV_ENTRY_FLAG);
                DRV_IOCTL(chip_id, 0, cmd, data);
                DRV_GET_FIELD(ecc_sbe_cnt[i].rec, ecc_sbe_cnt[i].fld, data, &val);

                if (0 != val)
                {
                    ecc_cb_info.tbl_id = ecc_sbe_cnt[i].tblid;
                    ecc_cb_info.type = DRV_ECC_ERR_TYPE_SBE;
                    ecc_cb_info.tbl_index = 0xFFFFFFFF;
                    ecc_cb_info.recover = 1;
                    ecc_cb_info.action = DRV_ECC_ACTION_LOG;
                    _drv_goldengate_ecc_recover_error_info(lchip, &ecc_cb_info);
                    g_gg_ecc_recover_master->sbe_cnt[lchip]++;
                }
            }
        }
        if (g_gg_ecc_recover_master->scan_continue[DRV_ECC_SCAN_TYPE_SBE])
        {
            sal_task_sleep(g_gg_ecc_recover_master->scan_interval[DRV_ECC_SCAN_TYPE_SBE]);
        }
    }while(g_gg_ecc_recover_master->scan_continue[DRV_ECC_SCAN_TYPE_SBE]);
    g_gg_ecc_recover_master->scan_interval[DRV_ECC_SCAN_TYPE_SBE] = 0;

    sal_time(&tm);
    p_time_str = sal_ctime(&tm);
    DRV_DBG_INFO("\nSbe scan thread exist time: %s", p_time_str);

    return;
}

STATIC void
_drv_goldengate_ecc_recover_scan_tcam_thread(void* param)
{
    uint8  lchip_num = 0, lchip = 0, chip_id = 0;
    uint32 i = 0, j = 0, key_index = 0;
    uint32 cmd = 0, per_entry_bytes = 0;
    uint32 entries_interval = 0;
    uint32 data[CHIP_MAX_MEM_WORDS] = {0};
    uint32 mask[CHIP_MAX_MEM_WORDS] = {0};
    tbl_entry_t tcam_key;
    char* p_time_str = NULL;
    sal_time_t tm;

    tcam_key.data_entry = data;
    tcam_key.mask_entry = mask;
    drv_goldengate_get_chipnum(&lchip_num);

    sal_time(&tm);
    p_time_str = sal_ctime(&tm);
    DRV_DBG_INFO("\nStart tcam scan thread time: %s", p_time_str);

    do
    {
        entries_interval = 0;
        for (i = 0; i < DRV_ECC_TCAM_BLOCK_TYPE_NUM; i++)
        {
            per_entry_bytes = (DRV_ECC_TCAM_BLOCK_TYPE_IGS_LPM0 == i)
                              ? DRV_LPM_KEY_BYTES_PER_ENTRY : (DRV_BYTES_PER_ENTRY * 2);

            for (j = 0; (j < 4) && (MaxTblId_t != drv_tcam_scan_tbl_inf[i][j]); j++)
            {
                cmd = DRV_IOR(drv_tcam_scan_tbl_inf[i][j], DRV_ENTRY_FLAG);

                for (key_index = 0; key_index < TABLE_MAX_INDEX(drv_tcam_scan_tbl_inf[i][j]); key_index++)
                {
                    for (lchip = 0; lchip < lchip_num; lchip++)
                    {
                        chip_id = lchip + drv_gg_init_chip_info.drv_init_chipid_base;
                        DRV_IOCTL(chip_id, key_index, cmd, &tcam_key);

                        entries_interval += TCAM_KEY_SIZE(drv_tcam_scan_tbl_inf[i][j]) / per_entry_bytes;
                        if (entries_interval >= g_gg_ecc_recover_master->tcam_scan_burst_entry_num)
                        {
                            sal_task_sleep(g_gg_ecc_recover_master->tcam_scan_burst_interval);
                            entries_interval = 0;
                        }
                    }
                }
            }
        }
        if (g_gg_ecc_recover_master->scan_continue[DRV_ECC_SCAN_TYPE_TCAM])
        {
            sal_task_sleep(g_gg_ecc_recover_master->scan_interval[DRV_ECC_SCAN_TYPE_TCAM]);
        }
    }while(g_gg_ecc_recover_master->scan_continue[DRV_ECC_SCAN_TYPE_TCAM]);
    g_gg_ecc_recover_master->scan_interval[DRV_ECC_SCAN_TYPE_TCAM] = 0;

    sal_time(&tm);
    p_time_str = sal_ctime(&tm);
    DRV_DBG_INFO("\nTcam scan thread exist time: %s", p_time_str);

    return;
}

int32
drv_goldengate_ecc_recover_set_mem_scan_mode(drv_ecc_scan_type_t type, uint8 mode, uint32 scan_interval)
{
    if ((NULL == g_gg_ecc_recover_master)
       || ((DRV_ECC_SCAN_TYPE_TCAM == type) && (0 == g_gg_ecc_recover_master->tcam_scan_en))
       || ((DRV_ECC_SCAN_TYPE_SBE == type) && (0 == g_gg_ecc_recover_master->sbe_scan_en)))
    {
        return DRV_E_NONE;
    }

    if (mode > 2)
    {
        return DRV_E_INVALID_PARAMETER;
    }

    if (mode != 1)
    {
        scan_interval = (mode == 0) ?1:0;
    }

    g_gg_ecc_recover_master->scan_continue[type] = (mode == 1) ?1:0;

    if (scan_interval && (g_gg_ecc_recover_master->scan_interval[type] == 0 ))
    {
        g_gg_ecc_recover_master->scan_interval[type] = scan_interval * 60 *1000;
        sal_task_create(&g_gg_ecc_recover_master->scan_thread[type].p_scan_task,
                        ((DRV_ECC_SCAN_TYPE_TCAM == type) ? "DrvTcamScan" : "DrvSbeScan"),
                        SAL_DEF_TASK_STACK_SIZE,
                        SAL_TASK_PRIO_DEF,
                        ((DRV_ECC_SCAN_TYPE_TCAM == type) ? _drv_goldengate_ecc_recover_scan_tcam_thread
                        : _drv_goldengate_ecc_recover_scan_sbe_thread),
                        (void*)&g_gg_ecc_recover_master->scan_thread[type]);
    }

    return DRV_E_NONE;
}

int32
drv_goldengate_ecc_recover_get_mem_scan_mode(drv_ecc_scan_type_t type, uint8* p_mode, uint32* p_scan_interval)
{
    DRV_PTR_VALID_CHECK(p_mode);
    DRV_PTR_VALID_CHECK(p_scan_interval);

    *p_mode = 0xFF;
    if (NULL == g_gg_ecc_recover_master)
    {
        return DRV_E_FATAL_EXCEP;
    }

    if (DRV_ECC_SCAN_TYPE_TCAM == type)
    {
        *p_scan_interval = g_gg_ecc_recover_master->scan_interval[DRV_ECC_SCAN_TYPE_TCAM] / (60 * 1000);
        *p_mode = g_gg_ecc_recover_master->scan_continue[DRV_ECC_SCAN_TYPE_TCAM];
    }

    if (DRV_ECC_SCAN_TYPE_SBE == type)
    {
        *p_scan_interval = g_gg_ecc_recover_master->scan_interval[DRV_ECC_SCAN_TYPE_SBE] / (60 * 1000);
        *p_mode = g_gg_ecc_recover_master->scan_continue[DRV_ECC_SCAN_TYPE_SBE];
    }

    return DRV_E_NONE;
}

#define ___ECC_RECOVER_INTERFACE___

/**
 @brief The function get interrupt bits's field id and action type
*/
int32
drv_goldengate_ecc_recover_get_intr_info(tbls_id_t tblid, uint8 intr_bit, uint8* p_fldid, uint8* p_action_type, uint8* p_ecc_or_parity)
{
    tbls_id_t sub_tbl = MaxTblId_t;
    uint32 intr_tbl = 0, bit_idx = 0;
    uint32 intr_tbl_num = sizeof(drv_err_intr_tbl) / sizeof(drv_err_intr_tbl_t);
    drv_err_intr_fld_t* p_intr_fld = NULL;

    DRV_PTR_VALID_CHECK(p_fldid);
    DRV_PTR_VALID_CHECK(p_action_type);
    DRV_PTR_VALID_CHECK(p_ecc_or_parity);

    *p_fldid = 0xFF;
    *p_action_type = DRV_ECC_ACTION_NULL;
    *p_ecc_or_parity = 0;

    for (intr_tbl = 0; intr_tbl < intr_tbl_num; intr_tbl++)
    {
        for (sub_tbl = 0; sub_tbl < drv_err_intr_tbl[intr_tbl].num; sub_tbl++)
        {
            if (tblid != drv_err_intr_tbl[intr_tbl].tblid + sub_tbl)
            {
                continue;
            }

            p_intr_fld = drv_err_intr_tbl[intr_tbl].intr_fld;

            for (bit_idx = 0; 0xFF != p_intr_fld[bit_idx].fld_id; bit_idx++)
            {
                if (bit_idx == intr_bit)
                {
                    *p_fldid = p_intr_fld[bit_idx].fld_id;
                    *p_ecc_or_parity = p_intr_fld[bit_idx].ecc_or_pariry;
                    *p_action_type = p_intr_fld[bit_idx].action;
                    return DRV_E_NONE;
                }
            }
        }
    }

    return DRV_E_INVALID_PARAMETER;
}

/**
 @brief The function compare cached tcam data/mask with data/mask read from hw,
        if compare failed, recover from cached data/mask and send tcam error event log.
*/
int32
drv_goldengate_ecc_recover_tcam(uint8 chip_id, drv_ecc_mem_type_t mem_type,
                     uint32 entry_offset, tbl_entry_t* p_tbl_entry,
                     drv_ecc_recover_action_t* p_recover_action)
{
    uint32 entry_dw = 0, dw = 0;
    char   errstr[80 + CHIP_MAX_MEM_WORDS * 19] = {0};
    uint8  lchip = 0;
    tbl_entry_t tbl_entry;
    drv_ecc_cb_info_t ecc_cb_info;

    DRV_CHIP_ID_VALID_CHECK(chip_id);
    DRV_PTR_VALID_CHECK(p_tbl_entry);
    DRV_PTR_VALID_CHECK(p_recover_action);

    /* mutex exist in tcam read io */
    if ((NULL == g_gg_ecc_recover_master)
       || (0 == g_gg_ecc_recover_master->tcam_scan_en)
       || (0 == g_gg_ecc_recover_master->scan_interval[DRV_ECC_SCAN_TYPE_TCAM]))
    {
        *p_recover_action = DRV_ECC_RECOVER_ACTION_NONE;
        return DRV_E_NONE;
    }

    sal_memset(&tbl_entry, 0, sizeof(tbl_entry_t));


    lchip = chip_id - drv_gg_init_chip_info.drv_init_chipid_base;

    *p_recover_action = DRV_ECC_RECOVER_ACTION_NONE;

    if ((mem_type >= DRV_ECC_MEM_TCAM_KEY0) && (mem_type < DRV_ECC_MEM_TCAM_KEY5))
    {
        entry_offset = (entry_offset & 0xFFF) + (((1U << 12) & entry_offset) ? (DRV_TCAM_KEY0_MAX_ENTRY_NUM / 2) : 0);
    }
    else if ((mem_type >= DRV_ECC_MEM_TCAM_KEY5) && (mem_type <= DRV_ECC_MEM_TCAM_KEY6))
    {
        entry_offset = (entry_offset & 0xFFF) + (((1U << 12) & entry_offset) ? (DRV_TCAM_KEY5_MAX_ENTRY_NUM / 2) : 0);
    }

    if ((mem_type < DRV_ECC_MEM_TCAM_KEY0) && (mem_type > DRV_ECC_MEM_TCAM_KEY8))
    {
        DRV_DBG_INFO("Error recover tcam key block id:%u\n", mem_type - DRV_ECC_MEM_TCAM_KEY0);
        return DRV_E_INVALID_PARAMETER;
    }

    _drv_goldengate_ecc_recover_read_tcam_cache(lchip, mem_type, entry_offset, &tbl_entry, &entry_dw);

    if ((NULL == tbl_entry.data_entry) && (NULL == tbl_entry.mask_entry))
    {
        if ((NULL == p_tbl_entry->data_entry) && (NULL == p_tbl_entry->mask_entry))
        {
            *p_recover_action = DRV_ECC_RECOVER_ACTION_NONE;
            return DRV_E_NONE;
        }
        else
        {
            *p_recover_action = DRV_ECC_RECOVER_ACTION_REMOVE;

            DRV_DBG_INFO("\n    Recovered by remove error tcam entry\n");
            goto DRV_ECC_RECOVER_TCAM_EVENT_LOG;
        }
    }

    if ((NULL == p_tbl_entry->data_entry) || (NULL == p_tbl_entry->mask_entry))
    {
        DRV_DBG_INFO("Error:tcam key cache data is damaged, mem_type:%u, entry_offset:%u!\n", mem_type, entry_offset);
        return DRV_E_NONE;
    }

    for (dw = 0; dw < entry_dw; dw++)
    {
        if ((p_tbl_entry->data_entry[dw] & tbl_entry.mask_entry[dw])
            != (tbl_entry.data_entry[dw] & tbl_entry.mask_entry[dw]))
        {
            break;
        }
    }

    if (dw < entry_dw)
    {
        DRV_DBG_INFO("\nMemory error detected on chip %d in tcam block:%u, entry word:%u\n",
                     chip_id, mem_type - DRV_ECC_MEM_TCAM_KEY0, entry_dw);

        errstr[0] = 0;
        for (dw = 0; dw < entry_dw; dw++)
        {
            sal_sprintf(errstr + sal_strlen(errstr),
                        "  %08x %08x",
                        tbl_entry.data_entry[dw],
                        tbl_entry.mask_entry[dw]);
            if (0 == ((dw + 1) % 3))
            {
                sal_sprintf(errstr + sal_strlen(errstr),
                            "%s", "\n        ");
            }
        }
        DRV_DBG_INFO("    WAS:%s\n", errstr);

        errstr[0] = 0;
        for (dw = 0; dw < entry_dw; dw++)
        {
            sal_sprintf(errstr + sal_strlen(errstr),
                        "  %08x %08x",
                        p_tbl_entry->data_entry[dw],
                        p_tbl_entry->mask_entry[dw]);
            if (0 == ((dw + 1) % 3))
            {
                sal_sprintf(errstr + sal_strlen(errstr),
                            "%s", "\n        ");
            }
        }
        DRV_DBG_INFO("    BAD:%s\n", errstr);

        *p_recover_action = DRV_ECC_RECOVER_ACTION_OVERWRITE;
        sal_memcpy(p_tbl_entry->data_entry, tbl_entry.data_entry, entry_dw * sizeof(uint32));
        sal_memcpy(p_tbl_entry->mask_entry, tbl_entry.mask_entry, entry_dw * sizeof(uint32));

        DRV_DBG_INFO("\n    Recovered by writing back cached tcam entry\n");
        goto DRV_ECC_RECOVER_TCAM_EVENT_LOG;
    }
    return DRV_E_NONE;

DRV_ECC_RECOVER_TCAM_EVENT_LOG:

    ecc_cb_info.type = DRV_ECC_ERR_TYPE_TCAM_ERROR;
    ecc_cb_info.tbl_id = 0xFE000000 + mem_type - DRV_ECC_MEM_TCAM_KEY0;
    if (DRV_ECC_MEM_TCAM_KEY7 == mem_type)
    {
        ecc_cb_info.tbl_index = entry_offset * DRV_LPM_KEY_BYTES_PER_ENTRY;
        ecc_cb_info.tbl_id |= (DRV_LPM_KEY_BYTES_PER_ENTRY / sizeof(uint32)) << 20;
    }
    else
    {
        ecc_cb_info.tbl_index = entry_offset * DRV_ADDR_BYTES_PER_ENTRY * 2;
        ecc_cb_info.tbl_id |= ((DRV_ADDR_BYTES_PER_ENTRY * 2) / sizeof(uint32)) << 20;
    }
    ecc_cb_info.recover = 1;
    ecc_cb_info.action = DRV_ECC_ACTION_LOG;
    _drv_goldengate_ecc_recover_error_info(lchip, &ecc_cb_info);
    g_gg_ecc_recover_master->ecc_mem[mem_type].recover_cnt[lchip]++;

    return DRV_E_NONE;
}

/**
 @brief The function write tbl data to ecc error recover memory for resume
*/
int32
drv_goldengate_ecc_recover_store(uint8 chip_id, uint32 mem_id, uint32 tbl_idx, tbl_entry_t* p_tbl_entry)
{
    uintptr  start_addr = 0;
    uintptr start_sw_data_addr = 0;
    uint32  offset = 0, len = 0;
    uint32  data[DRV_PE_MAX_ENTRY_WORD] = {0};
    drv_ecc_mem_type_t mem_type;
    uint8   lchip = 0;

    DRV_CHIP_ID_VALID_CHECK(chip_id);
    DRV_PTR_VALID_CHECK(p_tbl_entry);

    if (NULL == g_gg_ecc_recover_master)
    {
        return DRV_E_NONE;
    }

    DRV_ECC_LOCK;

    lchip = chip_id - drv_gg_init_chip_info.drv_init_chipid_base;

    if (NULL != p_tbl_entry->mask_entry)
    {
        if (0 == g_gg_ecc_recover_master->tcam_scan_en)
        {
            DRV_ECC_UNLOCK;
            return DRV_E_NONE;
        }

        mem_type = mem_id;

        if ((mem_type >= DRV_ECC_MEM_TCAM_KEY0) && (mem_type < DRV_ECC_MEM_TCAM_KEY5))
        {
            tbl_idx = (tbl_idx & 0xFFF) + (((1U << 12) & tbl_idx) ? (DRV_TCAM_KEY0_MAX_ENTRY_NUM / 2) : 0);
        }
        else if ((mem_type >= DRV_ECC_MEM_TCAM_KEY5) && (mem_type <= DRV_ECC_MEM_TCAM_KEY6))
        {
            tbl_idx = (tbl_idx & 0xFFF) + (((1U << 12) & tbl_idx) ? (DRV_TCAM_KEY5_MAX_ENTRY_NUM / 2) : 0);
        }

        /* flow tcam or lpm nat/pbr tcam */
        if (((mem_type >= DRV_ECC_MEM_TCAM_KEY0) && (mem_type <= DRV_ECC_MEM_TCAM_KEY6))
           || (DRV_ECC_MEM_TCAM_KEY8 == mem_type))
        {
            offset = tbl_idx * (2 * 2 * DRV_BYTES_PER_ENTRY + 1);
            len = 2 * DRV_BYTES_PER_ENTRY;
        }
        /* lpm prefix tcam */
        else if (DRV_ECC_MEM_TCAM_KEY7 == mem_type)
        {
            offset = tbl_idx * (2 * DRV_LPM_KEY_BYTES_PER_ENTRY + 1);
            len = DRV_LPM_KEY_BYTES_PER_ENTRY;
        }
        else
        {
            DRV_DBG_INFO("Ecc strore error tcam type:%u.\n",
                         mem_id - DRV_ECC_MEM_TCAM_KEY0);
            DRV_ECC_UNLOCK;
            return DRV_E_INVALID_PARAMETER;
        }

        start_addr = g_gg_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip];
        start_sw_data_addr = start_addr + offset;

        /* removed tcam entry */
        if (NULL == p_tbl_entry->data_entry)
        {
            /* if first byte value is 1, means entry removed */
            *((uint8*)start_sw_data_addr) = 0xFF;
        }
        else
        {
            *((uint8*)start_sw_data_addr) = 0;
            /* data */
            sal_memcpy((void*)((uint8*)start_sw_data_addr + 1),       p_tbl_entry->data_entry, len);
            /* mask */
            sal_memcpy((void*)((uint8*)start_sw_data_addr + 1 + len), p_tbl_entry->mask_entry, len);
        }
    }
    else
    {
        if (0 == g_gg_ecc_recover_master->ecc_recover_en)
        {
            DRV_ECC_UNLOCK;
            return DRV_E_NONE;
        }

        if (IS_ACC_KEY(mem_id))
        {
            if ((0 == TABLE_MAX_INDEX(DsFibHost0MacHashKey_t))
               || (1 == g_gg_ecc_recover_master->hw_learning_en))
            {
                DRV_ECC_UNLOCK;
                return DRV_E_NONE;
            }
            _drv_goldengate_ecc_recover_ram2mem(mem_id, tbl_idx, &mem_type, &offset);
            if (DRV_ECC_MEM_INVALID != mem_type)
            {
                offset = (offset / DRV_BYTES_PER_ENTRY) * sizeof(FibHashKeyCpuReq_m);
                start_addr = g_gg_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip];
                start_sw_data_addr = start_addr + offset;
                sal_memcpy((void*)start_sw_data_addr, p_tbl_entry->data_entry, sizeof(FibHashKeyCpuReq_m));
                SetFibHashKeyCpuReq(V, hashKeyCpuReqValid_f, (uint8*)start_sw_data_addr, 1);
                SetFibHashKeyCpuReq(V, cpuKeyIndex_f, (uint8*)start_sw_data_addr, tbl_idx + FIB_HOST0_CAM_NUM);
            }
        }
        else
        {
            _drv_goldengate_ecc_recover_tbl2mem(mem_id, tbl_idx, &mem_type, &offset);
            len = TABLE_ENTRY_SIZE(mem_id);

            if (mem_type >= DRV_ECC_MEM_NUM)
            {
                DRV_ECC_UNLOCK;
                return DRV_E_NONE;
            }
            start_addr = g_gg_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip];
            start_sw_data_addr = start_addr + offset;
            p_tbl_entry->data_entry = (NULL == p_tbl_entry->data_entry)
                                      ? data : p_tbl_entry->data_entry;
            sal_memcpy((void*)start_sw_data_addr, p_tbl_entry->data_entry, len);
        }
    }
    DRV_ECC_UNLOCK;
    return DRV_E_NONE;
}

/**
 @brief The function recover the ecc error memory with handle info
*/
int32
drv_goldengate_ecc_recover_restore(uint8 chip_id, drv_ecc_intr_param_t* p_intr_param)
{
    int32     ret = DRV_E_NONE;
    uint8     intr_bit = 0, valid_cnt = 0, action_type = 0, lchip = 0, fld_id = 0, ecc_or_parity = 0;
    uint32    offset = 0;
    tbls_id_t tblid = MaxTblId_t;
    drv_ecc_err_type_t err_type = 0;
    drv_ecc_tbl_type_t tbl_type = 0;
    drv_ecc_cb_info_t  ecc_cb_info;

    DRV_CHIP_ID_VALID_CHECK(chip_id);
    DRV_PTR_VALID_CHECK(p_intr_param);

    if ((NULL == g_gg_ecc_recover_master)
       || (0 == g_gg_ecc_recover_master->ecc_recover_en))
    {
        return DRV_E_NONE;
    }

    DRV_ECC_LOCK;
    lchip = chip_id - drv_gg_init_chip_info.drv_init_chipid_base;

    for (intr_bit = 0; (valid_cnt < p_intr_param->valid_bit_count) && (intr_bit < p_intr_param->total_bit_num); intr_bit++)
    {
        if (DRV_BMP_ISSET(p_intr_param->p_bmp, intr_bit))
        {
            ret = _drv_goldengate_ecc_recover_proc_intr(lchip, p_intr_param->intr_tblid, intr_bit, &tblid, &offset, &err_type, &tbl_type);
            valid_cnt++;

            if (DRV_E_NONE != ret)
            {
                continue;
            }

            drv_goldengate_ecc_recover_get_intr_info(p_intr_param->intr_tblid, intr_bit, &fld_id, &action_type, &ecc_or_parity);
            ecc_cb_info.action = action_type;

            if (DRV_ECC_TBL_TYPE_DYNAMIC == tbl_type)
            {
                _drv_goldengate_ecc_recover_dynamic_tbl(lchip, p_intr_param->intr_tblid, intr_bit, offset, err_type, &ecc_cb_info);
            }
            else
            {
                if (DRV_ECC_TBL_TYPE_STATIC == tbl_type)
                {
                    if (offset < TABLE_MAX_INDEX(tblid))
                    {
                        _drv_goldengate_ecc_recover_static_tbl(lchip, tblid, offset);
                        ecc_cb_info.recover = 1;
                        ecc_cb_info.tbl_id = tblid;
                        ecc_cb_info.type = err_type;
                        ecc_cb_info.tbl_index = offset;
                    }
                    else
                    {
                        DRV_DBG_INFO("Error: %s invalid index:%u\n",
                                     TABLE_NAME(tblid), offset);
                        return DRV_E_NONE;
                    }
                }
                else if (DRV_ECC_TBL_TYPE_IGNORE == tbl_type)
                {
                    if (offset >= TABLE_MAX_INDEX(tblid))
                    {
                        DRV_DBG_INFO("Error: %s invalid index:%u\n",
                                     TABLE_NAME(tblid), offset);
                        return DRV_E_NONE;
                    }
                    else
                    {
                        ecc_cb_info.recover = 0;
                        ecc_cb_info.tbl_id = tblid;
                        ecc_cb_info.type = err_type;
                        ecc_cb_info.tbl_index = offset;
                        g_gg_ecc_recover_master->ignore_cnt[lchip]++;
                    }
                }
                else
                {
                    ecc_cb_info.recover = 0;
                    ecc_cb_info.tbl_id = p_intr_param->intr_tblid | 0xFC000000;
                    ecc_cb_info.type = DRV_ECC_ERR_TYPE_OTHER;
                    ecc_cb_info.tbl_index = fld_id;
                }
            }
            _drv_goldengate_ecc_recover_error_info(lchip, &ecc_cb_info);
        }
    }
    DRV_ECC_UNLOCK;

    return DRV_E_NONE;
}

#define ___ECC_RECOVER_CFG___

/**
 @brief Set scan burst interval entry num and time(ms)
*/
int32
drv_goldengate_ecc_recover_set_tcam_scan_cfg(uint32 burst_entry_num, uint32 burst_interval)
{
    if (!g_gg_ecc_recover_master)
    {
        return DRV_E_FATAL_EXCEP;
    }

    if (burst_interval && (0 == burst_entry_num))
    {
        return DRV_E_INVALID_PARAMETER;
    }

    g_gg_ecc_recover_master->tcam_scan_burst_entry_num = burst_interval ? burst_entry_num : 0xFFFFFFFF;
    g_gg_ecc_recover_master->tcam_scan_burst_interval = burst_interval;

    return DRV_E_NONE;
}

/**
 @brief The function set mac hw learning
*/
void
drv_goldengate_ecc_recover_set_hw_learning(uint8 enable)
{
    if (!g_gg_ecc_recover_master)
    {
        return;
    }
    g_gg_ecc_recover_master->hw_learning_en = enable;

    return;
}

/**
 @brief The function get ecc recover status
*/
uint8
drv_goldengate_ecc_recover_get_enable(void)
{
    return ((NULL != g_gg_ecc_recover_master)
           && (g_gg_ecc_recover_master->ecc_recover_en));
}

#define ___ECC_RECOVER_STATS___

int32
drv_goldengate_ecc_recover_register_event_cb(drv_ecc_event_fn cb)
{
    if (NULL == g_gg_ecc_recover_master)
    {
        /* DRV_DBG_INFO("\nERROR: Ecc/parity error recover is not enable.\n"); */
        return DRV_E_NONE;
    }

    g_gg_ecc_recover_master->cb = cb;

    return DRV_E_NONE;
}

/**
 @brief The function get ecc error recover/ignore restore memory/tbl count
*/
int32
drv_goldengate_ecc_recover_get_status(uint8 chip_id, drv_ecc_mem_type_t mem_type, uint32* p_cnt)
{
    uint8 lchip = 0;

    DRV_CHIP_ID_VALID_CHECK(chip_id);
    DRV_PTR_VALID_CHECK(p_cnt);

    *p_cnt = 0;

    if (mem_type > DRV_ECC_MEM_SBE)
    {
        DRV_DBG_INFO("\nERROR: Ecc recover get status invalid ecc memory type:%u.\n",
                     mem_type);
        return DRV_E_INVAILD_TYPE;
    }

    if (NULL == g_gg_ecc_recover_master)
    {
        return DRV_E_NONE;
    }

    if ((0 == g_gg_ecc_recover_master->ecc_recover_en)
       && (((mem_type >= DRV_ECC_MEM_IgrPortTcPriMap)
       && (mem_type <= DRV_ECC_MEM_NextHopMetRam1))
       || ((mem_type >= DRV_ECC_MEM_FlowTcamAd0)
       && (mem_type <= DRV_ECC_MEM_LpmTcamNatAdRam))))
    {
        return DRV_E_NONE;
    }

    if ((0 == g_gg_ecc_recover_master->tcam_scan_en)
       && ((mem_type >= DRV_ECC_MEM_TCAM_KEY0)
       && (mem_type <= DRV_ECC_MEM_TCAM_KEY8)))
    {
        return DRV_E_NONE;
    }

    lchip = chip_id - drv_gg_init_chip_info.drv_init_chipid_base;

    if (DRV_ECC_MEM_IGNORE == mem_type)
    {
        *p_cnt = g_gg_ecc_recover_master->ignore_cnt[lchip];
    }
    else if (DRV_ECC_MEM_SBE == mem_type)
    {
        *p_cnt = g_gg_ecc_recover_master->sbe_cnt[lchip];
    }
    else if (mem_type < DRV_ECC_MEM_NUM)
    {
        *p_cnt = g_gg_ecc_recover_master->ecc_mem[mem_type].recover_cnt[lchip];
    }

    return DRV_E_NONE;
}

/**
 @brief The function show ecc error recover restore memory count
*/
int32
drv_goldengate_ecc_recover_show_status(uint8 chip_id, uint32 is_all)
{
    uint32 type = 0, lchip = 0;
    char str[50] = {0};

    DRV_CHIP_ID_VALID_CHECK(chip_id);

    lchip = chip_id - drv_gg_init_chip_info.drv_init_chipid_base;

    DRV_DBG_INFO("\n");
    DRV_DBG_INFO(" %-23s%-10s%s\n", "RecoverOption", "Enable", "MemSize(MB)");
    DRV_DBG_INFO(" --------------------------------------------\n");

    if ((NULL == g_gg_ecc_recover_master)
       || (0 == g_gg_ecc_recover_master->cache_mem_size))
    {
        sal_sprintf(str, "%s", "0");
    }
    else
    {
        sal_sprintf(str, "%u.%u",
        ((g_gg_ecc_recover_master->cache_mem_size * 100) / (1024 * 1024)) / 100,
        ((g_gg_ecc_recover_master->cache_mem_size * 100) / (1024 * 1024)) % 100);
    }

    DRV_DBG_INFO(" %-23s%-10s%s\n", "EccMultiBit/Parity",
                 (NULL == g_gg_ecc_recover_master) ? "No" :
                 (g_gg_ecc_recover_master->ecc_recover_en ? "Yes" : "No"), str);

    if ((NULL == g_gg_ecc_recover_master)
       || (0 == g_gg_ecc_recover_master->cache_tcam_size))
    {
        sal_sprintf(str, "%s", "0");
    }
    else
    {
        sal_sprintf(str, "0.%u",
        ((g_gg_ecc_recover_master->cache_tcam_size * 1000) / (1024 * 1024)));
    }

    DRV_DBG_INFO(" %-23s%-10s%s\n", "TcamScan",
                 (NULL == g_gg_ecc_recover_master) ? "No" :
                 (g_gg_ecc_recover_master->tcam_scan_en ? "Yes" : "No"), str);

    DRV_DBG_INFO(" --------------------------------------------\n\n");

    if (NULL == g_gg_ecc_recover_master)
    {
        return DRV_E_NONE;
    }

    DRV_DBG_INFO(" %-33s%s\n", "RecoverMemory", "RestoreCount");
    DRV_DBG_INFO(" ---------------------------------------------\n");

    for (type = 0; type < DRV_ECC_MEM_NUM; type++)
    {
        if ((0 == g_gg_ecc_recover_master->ecc_recover_en)
           && (((type >= DRV_ECC_MEM_IgrPortTcPriMap)
           && (type <= DRV_ECC_MEM_NextHopMetRam1))
           || ((type >= DRV_ECC_MEM_FlowTcamAd0)
           && (type <= DRV_ECC_MEM_LpmTcamNatAdRam))))
        {
            continue;
        }

        if ((0 == g_gg_ecc_recover_master->tcam_scan_en)
           && ((type >= DRV_ECC_MEM_TCAM_KEY0)
           && (type <= DRV_ECC_MEM_TCAM_KEY8)))
        {
            continue;
        }

        if (0 == is_all)
        {
            if (0 != g_gg_ecc_recover_master->ecc_mem[type].recover_cnt[lchip])
            {
                DRV_DBG_INFO(" %-33s%u\n", ecc_mem_desc[type], g_gg_ecc_recover_master->ecc_mem[type].recover_cnt[lchip]);
            }
        }
        else
        {
            DRV_DBG_INFO(" %-33s%u\n", ecc_mem_desc[type], g_gg_ecc_recover_master->ecc_mem[type].recover_cnt[lchip]);
        }
    }

    DRV_DBG_INFO(" %-33s%u\n", "OtherIgnore",
                 g_gg_ecc_recover_master->ecc_recover_en ?
                 g_gg_ecc_recover_master->ignore_cnt[lchip] : 0);

    DRV_DBG_INFO(" ---------------------------------------------\n");

    DRV_DBG_INFO("\n");

    return DRV_E_NONE;
}

#define ___ECC_RECOVER_INIT___

/**
 @brief The function init chip's mapping memory for ecc error recover
*/
int32
drv_goldengate_ecc_recover_init(drv_ecc_recover_init_t* p_init)
{
    uint8     lchip = 0, lchip_num = 0;
    uint32    size = 0, couple_mode = 0, entry_num = 0, type = 0, per_entry_bytes = 0;
    /* uint32    data[DRV_PE_MAX_ENTRY_WORD] = {0}; */
    uintptr   start_addr = 0;
    tbls_id_t tblid = 0;
    int32     ret = DRV_E_NONE;
    void*     ptr = NULL;
    /* tbls_id_t datapath_tbl[] = {}; */

    DRV_PTR_VALID_CHECK(p_init);

    drv_goldengate_get_chipnum(&lchip_num);
    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));

   if ((NULL != g_gg_ecc_recover_master) || ((0 == p_init->ecc_recover_en)
      && (0 == p_init->tcam_scan_en) && (0 == p_init->sbe_scan_en)))
    {
        return DRV_E_NONE;
    }

    g_gg_ecc_recover_master = (drv_ecc_recover_master_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(drv_ecc_recover_master_t));
    if (NULL == g_gg_ecc_recover_master)
    {
        goto DRV_ECC_RECOVER_INIT_ERROR;
    }
    sal_memset(g_gg_ecc_recover_master, 0, sizeof(drv_ecc_recover_master_t));

    ret = sal_mutex_create(&(g_gg_ecc_recover_master->p_ecc_mutex));
    if (ret || !(g_gg_ecc_recover_master->p_ecc_mutex))
    {
        goto DRV_ECC_RECOVER_INIT_ERROR;
    }

    g_gg_ecc_recover_master->ecc_recover_en = p_init->ecc_recover_en;
    g_gg_ecc_recover_master->tcam_scan_en = p_init->tcam_scan_en;
    g_gg_ecc_recover_master->sbe_scan_en = p_init->sbe_scan_en;
    g_gg_ecc_recover_master->scan_interval[DRV_ECC_SCAN_TYPE_TCAM] = p_init->tcam_scan_interval;
    g_gg_ecc_recover_master->scan_interval[DRV_ECC_SCAN_TYPE_SBE] = p_init->sbe_scan_interval;
    g_gg_ecc_recover_master->scan_continue[DRV_ECC_SCAN_TYPE_TCAM] = 0;
    g_gg_ecc_recover_master->scan_continue[DRV_ECC_SCAN_TYPE_SBE] = 0;
    g_gg_ecc_recover_master->tcam_scan_burst_interval = 5 * 1000;
    g_gg_ecc_recover_master->tcam_scan_burst_entry_num = (0 == p_init->tcam_scan_burst_entry_num)
                                                      ? 4096 : p_init->tcam_scan_burst_entry_num;

    for (lchip = 0; lchip < lchip_num; lchip++)
    {
        if (g_gg_ecc_recover_master->ecc_recover_en)
        {
            for (type = DRV_ECC_MEM_IgrPortTcPriMap; type <= DRV_ECC_MEM_AutoGenPktPktHdr; type++)
            {
                _drv_goldengate_ecc_recover_mem2tbl(type, &tblid);

                if (MaxTblId_t != tblid)
                {
                    size = TABLE_MAX_INDEX(tblid) * TABLE_ENTRY_SIZE(tblid);
                    g_gg_ecc_recover_master->cache_mem_size += size;

                    start_addr = (uintptr)mem_malloc(MEM_SYSTEM_MODULE, size);
                    if (0 == start_addr)
                    {
                        goto DRV_ECC_RECOVER_INIT_ERROR;
                    }
                    sal_memset((uintptr*)start_addr, 0, size);

                    g_gg_ecc_recover_master->ecc_mem[type].start_addr[lchip] = start_addr;
                    g_gg_ecc_recover_master->ecc_mem[type].size[lchip] = size;
                }
            }
        }

        for (type = DRV_ECC_MEM_SharedRam0; type < DRV_ECC_MEM_NUM; type++)
        {
            if ((0 == g_gg_ecc_recover_master->ecc_recover_en)
               && (((type >= DRV_ECC_MEM_IgrPortTcPriMap)
               && (type <= DRV_ECC_MEM_NextHopMetRam1))
               || ((type >= DRV_ECC_MEM_FlowTcamAd0)
               && (type <= DRV_ECC_MEM_LpmTcamNatAdRam))))
            {
                continue;
            }

            if ((0 == g_gg_ecc_recover_master->tcam_scan_en)
               && ((type >= DRV_ECC_MEM_TCAM_KEY0)
               && (type <= DRV_ECC_MEM_TCAM_KEY8)))
            {
                continue;
            }

            if ((type <= DRV_ECC_MEM_SharedRam6)
               && (NULL != TABLE_INFO_PTR(DsFibHost0MacHashKey_t))
               && (NULL != TABLE_EXT_INFO_PTR(DsFibHost0MacHashKey_t))
               && (IS_BIT_SET(DYNAMIC_BITMAP(DsFibHost0MacHashKey_t),
                  (type - DRV_ECC_MEM_SharedRam0))))
            {
                per_entry_bytes = sizeof(FibHashKeyCpuReq_m);
            }
            else if (DRV_ECC_MEM_LpmTcamIpAdRam == type)
            {
                per_entry_bytes = DRV_LPM_AD0_BYTE_PER_ENTRY;
            }
            else if (DRV_ECC_MEM_LpmTcamNatAdRam == type)
            {
                per_entry_bytes = DRV_LPM_AD1_BYTE_PER_ENTRY;
            }
            else if (DRV_ECC_MEM_TCAM_KEY7 == type)
            {
                per_entry_bytes = DRV_LPM_KEY_BYTES_PER_ENTRY;
            }
            else
            {
                per_entry_bytes = DRV_BYTES_PER_ENTRY;
            }

            if (((type >= DRV_ECC_MEM_TCAM_KEY0)
               && (type <= DRV_ECC_MEM_TCAM_KEY6))
               || (DRV_ECC_MEM_TCAM_KEY8 == type))
            {
                /* 160 bit */
                entry_num = dynamic_ram_entry_num[type - DRV_ECC_MEM_SharedRam0] / 2;
            }
            /* lpm prefix tcam */
            else if (DRV_ECC_MEM_TCAM_KEY7 == type)
            {
                entry_num = dynamic_ram_entry_num[type - DRV_ECC_MEM_SharedRam0];
            }

            size = dynamic_ram_entry_num[type - DRV_ECC_MEM_SharedRam0] * per_entry_bytes;

            if ((type <= DRV_ECC_MEM_IpMacRam3)
               && (((NULL != TABLE_INFO_PTR(DsIpfixL2HashKey_t))
               && (NULL != TABLE_EXT_INFO_PTR(DsIpfixL2HashKey_t))
               && IS_BIT_SET(DYNAMIC_BITMAP(DsIpfixL2HashKey_t),
                  (type - DRV_ECC_MEM_SharedRam0)))
               || ((NULL != TABLE_INFO_PTR(DsIpfixSessionRecord_t))
               && (NULL != TABLE_EXT_INFO_PTR(DsIpfixSessionRecord_t))
               && IS_BIT_SET(DYNAMIC_BITMAP(DsIpfixSessionRecord_t),
                  (type - DRV_ECC_MEM_SharedRam0)))))
            {
                continue;
            }

            if ((couple_mode) && (DRV_ECC_MEM_LpmTcamNatAdRam != type))
            {
                size = size * 2;
                entry_num *= 2;
            }

            if ((type >= DRV_ECC_MEM_TCAM_KEY0)
               && (type <= DRV_ECC_MEM_TCAM_KEY8))
            {
                /* 1 byte indicate remove or no, since store the data/mask instead of X/Y */
                size = size * 2 + entry_num;
                g_gg_ecc_recover_master->cache_tcam_size += size;
            }
            else
            {
                g_gg_ecc_recover_master->cache_mem_size += size;
            }

            start_addr = (uintptr)mem_malloc(MEM_SYSTEM_MODULE, size);
            if (!start_addr)
            {
                goto DRV_ECC_RECOVER_INIT_ERROR;
            }
            if ((type >= DRV_ECC_MEM_TCAM_KEY0)
               && (type <= DRV_ECC_MEM_TCAM_KEY8))
            {
                sal_memset((uintptr*)start_addr, 0xFF, size);
            }
            else
            {
                sal_memset((uintptr*)start_addr, 0, size);
            }
            g_gg_ecc_recover_master->ecc_mem[type].start_addr[lchip] = start_addr;
            g_gg_ecc_recover_master->ecc_mem[type].size[lchip] = size;
        }
    }

    if (g_gg_ecc_recover_master->sbe_scan_en
        && g_gg_ecc_recover_master->scan_interval[DRV_ECC_SCAN_TYPE_SBE])
    {
        drv_goldengate_ecc_recover_set_mem_scan_mode(DRV_ECC_SCAN_TYPE_SBE,
                                          g_gg_ecc_recover_master->scan_continue[DRV_ECC_SCAN_TYPE_SBE],
                                          g_gg_ecc_recover_master->scan_interval[DRV_ECC_SCAN_TYPE_SBE]);
    }

    if (g_gg_ecc_recover_master->tcam_scan_en
       && g_gg_ecc_recover_master->scan_interval[DRV_ECC_SCAN_TYPE_TCAM])
    {
        drv_goldengate_ecc_recover_set_mem_scan_mode(DRV_ECC_SCAN_TYPE_TCAM,
                                          g_gg_ecc_recover_master->scan_continue[DRV_ECC_SCAN_TYPE_TCAM],
                                          g_gg_ecc_recover_master->scan_interval[DRV_ECC_SCAN_TYPE_TCAM]);
    }

    return DRV_E_NONE;

DRV_ECC_RECOVER_INIT_ERROR:

    for (lchip = 0; ((NULL != g_gg_ecc_recover_master) && (lchip <lchip_num)); lchip++)
    {
        for (type = 0; type < DRV_ECC_MEM_NUM; type++)
        {
            if (0 != g_gg_ecc_recover_master->ecc_mem[type].size[lchip])
            {
                ptr = (void*)g_gg_ecc_recover_master->ecc_mem[type].start_addr[lchip];
                mem_free(ptr);
                g_gg_ecc_recover_master->ecc_mem[type].start_addr[lchip] = 0;
                g_gg_ecc_recover_master->ecc_mem[type].size[lchip] = 0;
            }
        }
    }

    if (NULL != g_gg_ecc_recover_master->p_ecc_mutex)
    {
        sal_mutex_destroy(g_gg_ecc_recover_master->p_ecc_mutex);
    }

    if (NULL != g_gg_ecc_recover_master->scan_thread[DRV_ECC_SCAN_TYPE_SBE].p_scan_task)
    {
        sal_task_destroy(g_gg_ecc_recover_master->scan_thread[DRV_ECC_SCAN_TYPE_SBE].p_scan_task);
    }

    if (NULL != g_gg_ecc_recover_master->scan_thread[DRV_ECC_SCAN_TYPE_TCAM].p_scan_task)
    {
        sal_task_destroy(g_gg_ecc_recover_master->scan_thread[DRV_ECC_SCAN_TYPE_TCAM].p_scan_task);
    }

    if (NULL != g_gg_ecc_recover_master)
    {
        mem_free(g_gg_ecc_recover_master);
    }

    return DRV_E_INIT_FAILED;
}

