/****************************************************************************
 * @date 2015-01-30  The file contains all parity error fatal interrupt resume realization.
 *
 * Copyright:    (c)2015 Centec Networks Inc.  All rights reserved.
 *
 * Modify History:
 * Revision:     v0.1
 * Date:         2015-01-30
 * Reason:       Create for GreatBelt v2.5.x
 ****************************************************************************/

#include "greatbelt/include/drv_lib.h"
#include "greatbelt/include/drv_ecc_recover.h"

#define ECC_LOCK \
    if (g_ecc_recover_master->p_ecc_mutex) sal_mutex_lock(g_ecc_recover_master->p_ecc_mutex)
#define ECC_UNLOCK \
    if (g_ecc_recover_master->p_ecc_mutex) sal_mutex_unlock(g_ecc_recover_master->p_ecc_mutex)

#define  TCAM_AD_BLOCK_NUM     4
#define  IS_DYN_8W_TBL(tblid)  (DRV_BYTES_PER_ENTRY == TABLE_ENTRY_SIZE(tblid))

#define IS_TCAM_AD(tblid)  ((DsMacTcam_t == tblid)                  || (DsFcoeDaTcam_t == tblid)                \
                           || (DsFcoeSaTcam_t == tblid)             || (DsTrillDaUcastTcam_t == tblid)          \
                           || (DsTrillDaMcastTcam_t == tblid)       || (DsIpv4UcastDaTcam_t == tblid)           \
                           || (DsIpv6UcastDaTcam_t == tblid)        || (DsUserIdMacTcam_t == tblid)             \
                           || (DsUserIdIpv6Tcam_t == tblid)         || (DsUserIdIpv4Tcam_t == tblid)            \
                           || (DsUserIdVlanTcam_t == tblid)         || (DsIpv4McastDaTcam_t == tblid)           \
                           || (DsMacIpv4Tcam_t == tblid)            || (DsIpv6McastDaTcam_t == tblid)           \
                           || (DsMacIpv6Tcam_t == tblid)            || (DsIpv4SaNatTcam_t == tblid)             \
                           || (DsIpv6SaNatTcam_t == tblid)          || (DsIpv4UcastPbrDualDaTcam_t == tblid)    \
                           || (DsIpv6UcastPbrDualDaTcam_t == tblid) || (DsTunnelIdIpv4Tcam_t == tblid)          \
                           || (DsTunnelIdIpv6Tcam_t == tblid)       || (DsTunnelIdPbbTcam_t == tblid)           \
                           || (DsTunnelIdCapwapTcam_t == tblid)     || (DsTunnelIdTrillTcam_t == tblid)         \
                           || (DsMacAcl0Tcam_t == tblid)            || (DsIpv4Acl0Tcam_t == tblid)              \
                           || (DsMacAcl1Tcam_t == tblid)            || (DsIpv4Acl1Tcam_t == tblid)              \
                           || (DsMacAcl2Tcam_t == tblid)            || (DsIpv4Acl2Tcam_t == tblid)              \
                           || (DsMacAcl3Tcam_t == tblid)            || (DsIpv4Acl3Tcam_t == tblid)              \
                           || (DsIpv6Acl0Tcam_t == tblid)           || (DsIpv6Acl1Tcam_t == tblid))

#define DRV_UINT32_BITS  32

#define _DRV_BMP_OP(_bmp, _offset, _op)     \
    (_bmp[(_offset) / DRV_UINT32_BITS] _op(1U << ((_offset) % DRV_UINT32_BITS)))

#define DRV_BMP_ISSET(bmp, offset)          \
    _DRV_BMP_OP((bmp), (offset), &)

#define DRV_ERROR_RETURN_WITH_ECC_UNLOCK(op) \
    do { \
        int32 rv; \
        if ((rv = (op)) < 0) \
        { \
            sal_mutex_unlock(g_ecc_recover_master->p_ecc_mutex); \
            return (rv); \
        } \
    } while (0)

/* memory mapping type */
enum drv_ecc_mem_type_e
{
    /* BufStoreInterruptNormal */
    DRV_ECC_MEM_IgrPriToTcMap,
    DRV_ECC_MEM_IgrPortTcPriMap,
    DRV_ECC_MEM_IgrPortTcMinProfId,
    DRV_ECC_MEM_IgrPortTcThrdProfId,
    DRV_ECC_MEM_IgrPortThrdProfile,
    DRV_ECC_MEM_IgrPortTcThrdProfile,
    DRV_ECC_MEM_IgrCondDisProfId,

    /* EpeHdrEditInterruptNormal */
    DRV_ECC_MEM_PacketHeaderEditTunnel,

    /* EpeHdrProcInterruptNormal */
    DRV_ECC_MEM_Ipv6NatPrefix,

    /* EpeNextHopInterruptNormal */
    DRV_ECC_MEM_EpeEditPriorityMap,
    DRV_ECC_MEM_DestChannel,
    DRV_ECC_MEM_DestPort,
    DRV_ECC_MEM_EgressVlanRangeProfile,
    DRV_ECC_MEM_DestInterface,

    /* IpeFwdInterruptNormal */
    DRV_ECC_MEM_Qcn,

    /* IpeHdrAdjInterruptNormal */
    DRV_ECC_MEM_PhyPort,

    /* IpeIntfMapInterruptNormal */
    DRV_ECC_MEM_PhyPortExt,
    DRV_ECC_MEM_RouterMac,
    DRV_ECC_MEM_SrcInterface,
    DRV_ECC_MEM_SrcPort,
    DRV_ECC_MEM_VlanActionProfile,
    DRV_ECC_MEM_VlanRangeProfile,
    DRV_ECC_MEM_MplsCtl,

    /* IpePktProcInterruptNormal */
    DRV_ECC_MEM_Rpf,
    DRV_ECC_MEM_IpeClassificationDscpMap,
    DRV_ECC_MEM_SrcChannel,
    DRV_ECC_MEM_EcmpGroup,

    /* MetFifoInterruptNormal */
    DRV_ECC_MEM_ApsBridge,

    /* NetRxInterruptNormal */
    DRV_ECC_MEM_NetRxPauseTimerMem,
    DRV_ECC_MEM_ChannelizeIngFc,
    DRV_ECC_MEM_ChannelizeMode,

    /* QMgrEnqInterruptNormal */
    DRV_ECC_MEM_HeadHashMod,
    DRV_ECC_MEM_LinkAggregateMemberSet,
    DRV_ECC_MEM_QueueNumGenCtl,
    DRV_ECC_MEM_QueThrdProfile,
    DRV_ECC_MEM_QueueSelectMap,
    DRV_ECC_MEM_LinkAggregateMember,
    DRV_ECC_MEM_LinkAggregateSgmacMember,
    DRV_ECC_MEM_SgmacHeadHashMod,
    DRV_ECC_MEM_EgrResrcCtl,
    DRV_ECC_MEM_QueueHashKey,
    DRV_ECC_MEM_LinkAggregationPort,

    /* SharedDsInterruptNormal */
    DRV_ECC_MEM_StpState,
    DRV_ECC_MEM_VlanProfile,
    DRV_ECC_MEM_Vlan,

    /* IpeFibInterruptNormal */
    DRV_ECC_MEM_LpmTcamAdMem,

    /* QMgrDeqInterruptNormal */
    DRV_ECC_MEM_QueShpWfqCtl,
    DRV_ECC_MEM_QueShpProfile,
    DRV_ECC_MEM_QueMap,
    DRV_ECC_MEM_GrpShpProfile,
    DRV_ECC_MEM_ChanShpProfile,
    DRV_ECC_MEM_QueShpCtl,
    DRV_ECC_MEM_GrpShpWfqCtl,

    /* BufRetrvInterruptNormal */
    DRV_ECC_MEM_BufRetrvExcp,

    /* PolicingInterruptNormal */
    DRV_ECC_MEM_PolicerProfile0,
    DRV_ECC_MEM_PolicerProfile1,
    DRV_ECC_MEM_PolicerProfile2,
    DRV_ECC_MEM_PolicerProfile3,
    DRV_ECC_MEM_PolicerControl,

    /* DynamicDsInterruptNormal */
    DRV_ECC_MEM_Edram0,
    DRV_ECC_MEM_Edram1,
    DRV_ECC_MEM_Edram2,
    DRV_ECC_MEM_Edram3,
    DRV_ECC_MEM_Edram4,
    DRV_ECC_MEM_Edram5,
    DRV_ECC_MEM_Edram6,
    DRV_ECC_MEM_Edram7,
    DRV_ECC_MEM_Edram8,

    /* TcamDsInterruptNormal */
    DRV_ECC_MEM_TcamAd0,
    DRV_ECC_MEM_TcamAd1,
    DRV_ECC_MEM_TcamAd2,
    DRV_ECC_MEM_TcamAd3,

    DRV_ECC_MEM_NUM,

    DRV_ECC_MEM_INVALID = DRV_ECC_MEM_NUM
};
typedef enum drv_ecc_mem_type_e drv_ecc_mem_type_t;

/* memory mapping table */
struct drv_ecc_mem_s
{
    uintptr start_addr[MAX_LOCAL_CHIP_NUM]; /* start address of one memory mapping table */
    uint32  size[MAX_LOCAL_CHIP_NUM];
    uint32  recover_cnt[MAX_LOCAL_CHIP_NUM];
};
typedef struct drv_ecc_mem_s drv_ecc_mem_t;

/* memory mapping control */
struct drv_ecc_recover_master_s
{
    drv_ecc_mem_t ecc_mem[DRV_ECC_MEM_NUM];
    drv_ftm_check_fn ftm_check_cb;
    sal_mutex_t* p_ecc_mutex;
    uint32 mem_size;
    drv_ecc_event_fn cb;
};
typedef struct drv_ecc_recover_master_s drv_ecc_recover_master_t;

static drv_ecc_recover_master_t* g_ecc_recover_master = NULL;

static int32 edram_entry_num[] = {DRV_MEMORY0_MAX_ENTRY_NUM, DRV_MEMORY1_MAX_ENTRY_NUM, DRV_MEMORY2_MAX_ENTRY_NUM,
                                  DRV_MEMORY3_MAX_ENTRY_NUM, DRV_MEMORY4_MAX_ENTRY_NUM, DRV_MEMORY5_MAX_ENTRY_NUM,
                                  DRV_MEMORY6_MAX_ENTRY_NUM, DRV_MEMORY7_MAX_ENTRY_NUM, DRV_MEMORY8_MAX_ENTRY_NUM};

static uint32 edram_offset[MAX_MEMORY_BLOCK_NUM] = {0};

static char* ecc_mem_desc[] = {
    /* BufStoreInterruptNormal */
    "DsIgrPriToTcMap",
    "DsIgrPortTcPriMap",
    "DsIgrPortTcMinProfId",
    "DsIgrPortTcThrdProfId",
    "DsIgrPortThrdProfile",
    "DsIgrPortTcThrdProfile",
    "DsIgrCondDisProfId",

    /* EpeHdrEditInterruptNormal */
    "DsPacketHeaderEditTunnel",

    /* EpeHdrProcInterruptNormal */
    "DsIpv6NatPrefix",

    /* EpeNextHopInterruptNormal */
    "EpeEditPriorityMap",
    "DsDestChannel",
    "DsDestPort",
    "DsEgressVlanRangeProfile",
    "DsDestInterface",

    /* IpeFwdInterruptNormal */
    "DsQcn",

    /* IpeHdrAdjInterruptNormal */
    "DsPhyPort",

    /* IpeIntfMapInterruptNormal */
    "DsPhyPortExt",
    "DsRouterMac",
    "DsSrcInterface",
    "DsSrcPort",
    "DsVlanActionProfile",
    "DsVlanRangeProfile",
    "DsMplsCtl",

    /* IpePktProcInterruptNormal */
    "DsRpf",
    "IpeClassificationDscpMap",
    "DsSrcChannel",
    "DsEcmpGroup",

    /* MetFifoInterruptNormal */
    "DsApsBridge",

    /* NetRxInterruptNormal */
    "NetRxPauseTimerMem",
    "DsChannelizeIngFc",
    "DsChannelizeMode",

    /* QMgrEnqInterruptNormal */
    "DsHeadHashMod",
    "DsLinkAggregateMemberSet",
    "DsQueueNumGenCtl",
    "DsQueThrdProfile",
    "DsQueueSelectMap",
    "DsLinkAggregateMember",
    "DsLinkAggregateSgmacMember",
    "DsSgmacHeadHashMod",
    "DsEgrResrcCtl",
    "DsQueueHashKey",
    "DsLinkAggregationPort",

    /* SharedDsInterruptNormal */
    "DsStpState",
    "DsVlanProfile",
    "DsVlan",

    /* IpeFibInterruptNormal */
    "LpmTcamAdMem",

    /* QMgrDeqInterruptNormal */
    "DsQueShpWfqCtl",
    "DsQueShpProfile",
    "DsQueMap",
    "DsGrpShpProfile",
    "DsChanShpProfile",
    "DsQueShpCtl",
    "DsGrpShpWfqCtl",

    /* BufRetrvInterruptNormal */
    "DsBufRetrvExcp",

    /* PolicingInterruptNormal */
    "DsPolicerProfile0",
    "DsPolicerProfile1",
    "DsPolicerProfile2",
    "DsPolicerProfile3",
    "DsPolicerControl",

    /* DynamicDsInterruptNormal */
    "DynamicDsEdram0",
    "DynamicDsEdram1",
    "DynamicDsEdram2",
    "DynamicDsEdram3",
    "DynamicDsEdram4",
    "DynamicDsEdram5",
    "DynamicDsEdram6",
    "DynamicDsEdram7",
    "DynamicDsEdram8",

    /* TcamDsInterruptNormal */
    "DsTcamAd0",
    "DsTcamAd1",
    "DsTcamAd2",
    "DsTcamAd3",

    NULL
};

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
_drv_greatbelt_ecc_recover_error_info(uint8 lchip, drv_ecc_cb_info_t* p_ecc_cb_info)
{
    uint8 chip_id = 0;

    chip_id = lchip + drv_gb_init_chip_info.drv_init_chipid_base;

    if (NULL != g_ecc_recover_master->cb)
    {
        g_ecc_recover_master->cb(chip_id, p_ecc_cb_info);
    }

    return;
}

int32
drv_greatbelt_ecc_recover_register_event_cb(drv_ecc_event_fn cb)
{
    if (NULL == g_ecc_recover_master)
    {
        /* DRV_DBG_INFO("\nERROR: Ecc/parity error recover is not enable.\n"); */
        return DRV_E_NONE;
    }

    g_ecc_recover_master->cb = cb;

    return DRV_E_NONE;
}

STATIC void
_drv_greatbelt_ecc_recover_mem2tbl(drv_ecc_mem_type_t mem_type, tbls_id_t* p_tblid)
{
    switch (mem_type)
    {
        case DRV_ECC_MEM_IgrPriToTcMap:
            *p_tblid = DsIgrPriToTcMap_t;
            break;
        case DRV_ECC_MEM_IgrPortTcPriMap:
            *p_tblid = DsIgrPortTcPriMap_t;
            break;
        case DRV_ECC_MEM_IgrPortTcMinProfId:
            *p_tblid = DsIgrPortTcMinProfId_t;
            break;
        case DRV_ECC_MEM_IgrPortTcThrdProfId:
            *p_tblid = DsIgrPortTcThrdProfId_t;
            break;
        case DRV_ECC_MEM_IgrPortThrdProfile:
            *p_tblid = DsIgrPortThrdProfile_t;
            break;
        case DRV_ECC_MEM_IgrPortTcThrdProfile:
            *p_tblid = DsIgrPortTcThrdProfile_t;
            break;
        case DRV_ECC_MEM_IgrCondDisProfId:
            *p_tblid = DsIgrCondDisProfId_t;
            break;

        case DRV_ECC_MEM_Edram0:
        case DRV_ECC_MEM_Edram1:
        case DRV_ECC_MEM_Edram2:
        case DRV_ECC_MEM_Edram3:
        case DRV_ECC_MEM_Edram4:
        case DRV_ECC_MEM_Edram5:
        case DRV_ECC_MEM_Edram6:
        case DRV_ECC_MEM_Edram7:
        case DRV_ECC_MEM_Edram8:
            *p_tblid = MaxTblId_t;
            break;

        case DRV_ECC_MEM_PacketHeaderEditTunnel:
            *p_tblid = DsPacketHeaderEditTunnel_t;
            break;

        case DRV_ECC_MEM_Ipv6NatPrefix:
            *p_tblid = DsIpv6NatPrefix_t;
            break;

        case DRV_ECC_MEM_EpeEditPriorityMap:
            *p_tblid = EpeEditPriorityMap_t;
            break;
        case DRV_ECC_MEM_DestChannel:
            *p_tblid = DsDestChannel_t;
            break;
        case DRV_ECC_MEM_DestPort:
            *p_tblid = DsDestPort_t;
            break;
        case DRV_ECC_MEM_EgressVlanRangeProfile:
            *p_tblid = DsEgressVlanRangeProfile_t;
            break;
        case DRV_ECC_MEM_DestInterface:
            *p_tblid = DsDestInterface_t;
            break;

        case DRV_ECC_MEM_Qcn:
            *p_tblid = DsQcn_t;
            break;

        case DRV_ECC_MEM_PhyPort:
            *p_tblid = DsPhyPort_t;
            break;

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
        case DRV_ECC_MEM_MplsCtl:
            *p_tblid = DsMplsCtl_t;
            break;

        case DRV_ECC_MEM_Rpf:
            *p_tblid = DsRpf_t;
            break;
        case DRV_ECC_MEM_IpeClassificationDscpMap:
            *p_tblid = IpeClassificationDscpMap_t;
            break;
        case DRV_ECC_MEM_SrcChannel:
            *p_tblid = DsSrcChannel_t;
            break;
        case DRV_ECC_MEM_EcmpGroup:
            *p_tblid = DsEcmpGroup_t;
            break;

        case DRV_ECC_MEM_ApsBridge:
            *p_tblid = DsApsBridge_t;
            break;

        case DRV_ECC_MEM_NetRxPauseTimerMem:
            *p_tblid = NetRxPauseTimerMem_t;
            break;
        case DRV_ECC_MEM_ChannelizeIngFc:
            *p_tblid = DsChannelizeIngFc_t;
            break;
        case DRV_ECC_MEM_ChannelizeMode:
            *p_tblid = DsChannelizeMode_t;
            break;

        case DRV_ECC_MEM_HeadHashMod:
            *p_tblid = DsHeadHashMod_t;
            break;
        case DRV_ECC_MEM_LinkAggregateMemberSet:
            *p_tblid = DsLinkAggregateMemberSet_t;
            break;
        case DRV_ECC_MEM_QueueNumGenCtl:
            *p_tblid = DsQueueNumGenCtl_t;
            break;
        case DRV_ECC_MEM_QueThrdProfile:
            *p_tblid = DsQueThrdProfile_t;
            break;
        case DRV_ECC_MEM_QueueSelectMap:
            *p_tblid = DsQueueSelectMap_t;
            break;
        case DRV_ECC_MEM_LinkAggregateMember:
            *p_tblid = DsLinkAggregateMember_t;
            break;
        case DRV_ECC_MEM_LinkAggregateSgmacMember:
            *p_tblid = DsLinkAggregateSgmacMember_t;
            break;
        case DRV_ECC_MEM_SgmacHeadHashMod:
            *p_tblid = DsSgmacHeadHashMod_t;
            break;
        case DRV_ECC_MEM_EgrResrcCtl:
            *p_tblid = DsEgrResrcCtl_t;
            break;
        case DRV_ECC_MEM_QueueHashKey:
            *p_tblid = DsQueueHashKey_t;
            break;
        case DRV_ECC_MEM_LinkAggregationPort:
            *p_tblid = DsLinkAggregationPort_t;
            break;

        case DRV_ECC_MEM_StpState:
            *p_tblid = DsStpState_t;
            break;
        case DRV_ECC_MEM_VlanProfile:
            *p_tblid = DsVlanProfile_t;
            break;
        case DRV_ECC_MEM_Vlan:
            *p_tblid = DsVlan_t;
            break;

        case DRV_ECC_MEM_TcamAd0:
        case DRV_ECC_MEM_TcamAd1:
        case DRV_ECC_MEM_TcamAd2:
        case DRV_ECC_MEM_TcamAd3:
            *p_tblid = MaxTblId_t;
            break;

        case DRV_ECC_MEM_PolicerProfile0:
        case DRV_ECC_MEM_PolicerProfile1:
        case DRV_ECC_MEM_PolicerProfile2:
        case DRV_ECC_MEM_PolicerProfile3:
            *p_tblid = DsPolicerProfile_t;
            break;
        case DRV_ECC_MEM_PolicerControl:
            *p_tblid = DsPolicerControl_t;
            break;

        case DRV_ECC_MEM_LpmTcamAdMem:
            *p_tblid = LpmTcamAdMem_t;
            break;

        case DRV_ECC_MEM_QueShpWfqCtl:
            *p_tblid = DsQueShpWfqCtl_t;
            break;
        case DRV_ECC_MEM_QueShpProfile:
            *p_tblid = DsQueShpProfile_t;
            break;
        case DRV_ECC_MEM_QueMap:
            *p_tblid = DsQueMap_t;
            break;
        case DRV_ECC_MEM_GrpShpProfile:
            *p_tblid = DsGrpShpProfile_t;
            break;
        case DRV_ECC_MEM_ChanShpProfile:
            *p_tblid = DsChanShpProfile_t;
            break;
        case DRV_ECC_MEM_QueShpCtl:
            *p_tblid = DsQueShpCtl_t;
             break;
        case DRV_ECC_MEM_GrpShpWfqCtl:
            *p_tblid = DsGrpShpWfqCtl_t;
            break;

        case DRV_ECC_MEM_BufRetrvExcp:
            *p_tblid = DsBufRetrvExcp_t;
            break;

        default:
            *p_tblid = MaxTblId_t;
    }

    return;
}

STATIC void
_drv_greatbelt_ecc_recover_ram2mem(uint8 lchip, tbls_id_t tbl_id, uint32 tbl_idx, drv_ecc_mem_type_t* p_mem_type, uint32* p_offset)
{
    uint8   i = 0, recover = 0;
    uintptr hw_data_addr, drv_memory_base = 0;

    *p_mem_type = DRV_ECC_MEM_INVALID;
    if ((NULL != TABLE_EXT_INFO_PTR(tbl_id)) && (EXT_INFO_TYPE_DYNAMIC == TABLE_EXT_INFO_TYPE(tbl_id)))
    {
        drv_memory_base = ((TABLE_ENTRY_SIZE(tbl_id) == (DRV_BYTES_PER_ENTRY))
                          ? DRV_MEMORY0_BASE_4W : DRV_MEMORY0_BASE_8W);

        for (i = 0; i < MAX_MEMORY_BLOCK_NUM; i++)
        {
            if ((IS_BIT_SET(DYNAMIC_BITMAP(tbl_id), i))
               && (tbl_idx >= DYNAMIC_START_INDEX(tbl_id, i)) && (tbl_idx <= DYNAMIC_END_INDEX(tbl_id, i)))
            {
                *p_offset = ((DYNAMIC_DATA_BASE(tbl_id, i)
                            + ((TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY) * DRV_ADDR_BYTES_PER_ENTRY)
                            * (tbl_idx - DYNAMIC_START_INDEX(tbl_id, i))) - edram_offset[i])
                            / DRV_ADDR_BYTES_PER_ENTRY;
                *p_mem_type = DRV_ECC_MEM_Edram0 + i;
                break;
            }
        }

        if (DRV_ECC_MEM_INVALID != *p_mem_type)
        {
            g_ecc_recover_master->ftm_check_cb(lchip, (*p_mem_type - DRV_ECC_MEM_Edram0), *p_offset, &recover);
            if (0 == recover)
            {
                *p_mem_type = DRV_ECC_MEM_INVALID;
            }
            *p_offset = *p_offset * DRV_BYTES_PER_ENTRY;
        }
    }
    else if (IS_TCAM_AD(tbl_id))
    {
        hw_data_addr = TABLE_DATA_BASE(tbl_id)
                       + ((TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY) * DRV_ADDR_BYTES_PER_ENTRY
                       * tbl_idx);

        drv_memory_base = DRV_ADDR_IN_TCAMAD_SRAM_4W_RANGE(hw_data_addr)
                          ? DRV_INT_TCAM_AD_MEM_4W_BASE : DRV_INT_TCAM_AD_MEM_8W_BASE;

        for (i = 0; i < TCAM_AD_BLOCK_NUM; i++)
        {
            if (i > 0)
            {
                drv_memory_base += (DRV_INT_TCAM_AD_MAX_ENTRY_NUM/4) * DRV_ADDR_BYTES_PER_ENTRY;
            }

            if ((DRV_ADDR_IN_TCAMAD_SRAM_4W_RANGE(hw_data_addr)
               && (hw_data_addr >= DRV_INT_TCAM_AD_MEM_4W_BASE
                   + (i * (DRV_INT_TCAM_AD_MAX_ENTRY_NUM/4)) * DRV_ADDR_BYTES_PER_ENTRY)
               && (hw_data_addr < DRV_INT_TCAM_AD_MEM_4W_BASE
                   + ((i + 1) * (DRV_INT_TCAM_AD_MAX_ENTRY_NUM/4)) * DRV_ADDR_BYTES_PER_ENTRY))
               || (DRV_ADDR_IN_TCAMAD_SRAM_8W_RANGE(hw_data_addr)
               && (hw_data_addr >= DRV_INT_TCAM_AD_MEM_8W_BASE
                   + (i * (DRV_INT_TCAM_AD_MAX_ENTRY_NUM/4)) * DRV_ADDR_BYTES_PER_ENTRY)
               && (hw_data_addr < DRV_INT_TCAM_AD_MEM_8W_BASE
                   + ((i + 1) * (DRV_INT_TCAM_AD_MAX_ENTRY_NUM/4)) * DRV_ADDR_BYTES_PER_ENTRY)))
            {
                *p_mem_type = DRV_ECC_MEM_TcamAd0 + i;
                *p_offset = ((hw_data_addr - drv_memory_base) / DRV_ADDR_BYTES_PER_ENTRY) * DRV_BYTES_PER_ENTRY;
                break;
            }
        }
    }

    return;
}

STATIC void
_drv_greatbelt_ecc_recover_tbl2mem(uint8 lchip, tbls_id_t tbl_id, uint32 tbl_idx, drv_ecc_mem_type_t* p_mem_type, uint32* p_offset)
{
    *p_offset = TABLE_ENTRY_SIZE(tbl_id) * tbl_idx;

    switch (tbl_id)
    {
        case DsIgrPriToTcMap_t:
            *p_mem_type = DRV_ECC_MEM_IgrPriToTcMap;
            break;
        case DsIgrPortTcPriMap_t:
            *p_mem_type = DRV_ECC_MEM_IgrPortTcPriMap;
            break;
        case DsIgrPortTcMinProfId_t:
            *p_mem_type = DRV_ECC_MEM_IgrPortTcMinProfId;
            break;
        case DsIgrPortTcThrdProfId_t:
            *p_mem_type = DRV_ECC_MEM_IgrPortTcThrdProfId;
            break;
        case DsIgrPortThrdProfile_t:
            *p_mem_type = DRV_ECC_MEM_IgrPortThrdProfile;
            break;
        case DsIgrPortTcThrdProfile_t:
            *p_mem_type = DRV_ECC_MEM_IgrPortTcThrdProfile;
            break;
        case DsIgrCondDisProfId_t:
            *p_mem_type = DRV_ECC_MEM_IgrCondDisProfId;
            break;

        case DsPacketHeaderEditTunnel_t:
            *p_mem_type = DRV_ECC_MEM_PacketHeaderEditTunnel;
            break;

        case DsIpv6NatPrefix_t:
            *p_mem_type = DRV_ECC_MEM_Ipv6NatPrefix;
            break;

        case EpeEditPriorityMap_t:
            *p_mem_type = DRV_ECC_MEM_EpeEditPriorityMap;
            break;
        case DsDestChannel_t:
            *p_mem_type = DRV_ECC_MEM_DestChannel;
            break;
        case DsDestPort_t:
            *p_mem_type = DRV_ECC_MEM_DestPort;
            break;
        case DsEgressVlanRangeProfile_t:
            *p_mem_type = DRV_ECC_MEM_EgressVlanRangeProfile;
            break;
        case DsDestInterface_t:
            *p_mem_type = DRV_ECC_MEM_DestInterface;
            break;

        case DsQcn_t:
            *p_mem_type = DRV_ECC_MEM_Qcn;
            break;

        case DsPhyPort_t:
            *p_mem_type = DRV_ECC_MEM_PhyPort;
            break;

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
        case DsMplsCtl_t:
            *p_mem_type = DRV_ECC_MEM_MplsCtl;
            break;

        case DsRpf_t:
            *p_mem_type = DRV_ECC_MEM_Rpf;
            break;
        case IpeClassificationDscpMap_t:
            *p_mem_type = DRV_ECC_MEM_IpeClassificationDscpMap;
            break;
        case DsSrcChannel_t:
            *p_mem_type = DRV_ECC_MEM_SrcChannel;
            break;
        case DsEcmpGroup_t:
            *p_mem_type = DRV_ECC_MEM_EcmpGroup;
            break;

        case DsApsBridge_t:
            *p_mem_type = DRV_ECC_MEM_ApsBridge;
            break;

        case NetRxPauseTimerMem_t:
            *p_mem_type = DRV_ECC_MEM_NetRxPauseTimerMem;
            break;
        case DsChannelizeIngFc_t:
            *p_mem_type = DRV_ECC_MEM_ChannelizeIngFc;
            break;
        case DsChannelizeMode_t:
            *p_mem_type = DRV_ECC_MEM_ChannelizeMode;
            break;

        case DsHeadHashMod_t:
            *p_mem_type = DRV_ECC_MEM_HeadHashMod;
            break;
        case DsQueueNumGenCtl_t:
            *p_mem_type = DRV_ECC_MEM_QueueNumGenCtl;
            break;
        case DsQueThrdProfile_t:
            *p_mem_type = DRV_ECC_MEM_QueThrdProfile;
            break;
        case DsQueueSelectMap_t:
            *p_mem_type = DRV_ECC_MEM_QueueSelectMap;
            break;
        case DsLinkAggregateMember_t:
            *p_mem_type = DRV_ECC_MEM_LinkAggregateMember;
            break;
        case DsLinkAggregateSgmacMember_t:
            *p_mem_type = DRV_ECC_MEM_LinkAggregateSgmacMember;
            break;
        case DsSgmacHeadHashMod_t:
            *p_mem_type = DRV_ECC_MEM_SgmacHeadHashMod;
            break;
        case DsEgrResrcCtl_t:
            *p_mem_type = DRV_ECC_MEM_EgrResrcCtl;
            break;
        case DsQueueHashKey_t:
            *p_mem_type = DRV_ECC_MEM_QueueHashKey;
            break;
        case DsLinkAggregationPort_t:
            *p_mem_type = DRV_ECC_MEM_LinkAggregationPort;
            break;

        case DsStpState_t:
            *p_mem_type = DRV_ECC_MEM_StpState;
            break;
        case DsVlanProfile_t:
            *p_mem_type = DRV_ECC_MEM_VlanProfile;
            break;
        case DsVlan_t:
            *p_mem_type = DRV_ECC_MEM_Vlan;
            break;

        case DsPolicerProfile_t:
            *p_mem_type = DRV_ECC_MEM_PolicerProfile0 + (tbl_idx / TABLE_MAX_INDEX(tbl_id));
            break;
        case DsPolicerControl_t:
            *p_mem_type = DRV_ECC_MEM_PolicerControl;
            break;

        case LpmTcamAdMem_t:
            *p_mem_type = DRV_ECC_MEM_LpmTcamAdMem;
            break;

        case DsQueShpWfqCtl_t:
            *p_mem_type = DRV_ECC_MEM_QueShpWfqCtl;
            break;
        case DsQueShpProfile_t:
            *p_mem_type = DRV_ECC_MEM_QueShpProfile;
            break;
        case DsQueMap_t:
            *p_mem_type = DRV_ECC_MEM_QueMap;
            break;
        case DsGrpShpProfile_t:
            *p_mem_type = DRV_ECC_MEM_GrpShpProfile;
            break;
        case DsChanShpProfile_t:
            *p_mem_type = DRV_ECC_MEM_ChanShpProfile;
            break;
        case DsQueShpCtl_t:
            *p_mem_type = DRV_ECC_MEM_QueShpCtl;
            break;
        case DsGrpShpWfqCtl_t:
            *p_mem_type = DRV_ECC_MEM_GrpShpWfqCtl;
            break;

        case DsBufRetrvExcp_t:
            *p_mem_type = DRV_ECC_MEM_BufRetrvExcp;
            break;

        default:
            _drv_greatbelt_ecc_recover_ram2mem(lchip, tbl_id, tbl_idx, p_mem_type, p_offset);
    }

    return;
}

STATIC int32
_drv_greatbelt_ecc_recover_proc_intr(uint8 lchip, tbls_id_t intr_tblid, uint8 intr_bit, tbls_id_t* p_tblid, uint32* p_offset)
{
    uint8     fail = 0, idx = 0;
    int32     ret = DRV_E_NONE;
    uint32    cmd = 0;
    tbls_id_t err_rec = MaxTblId_t;
    uint32    data[DRV_PE_MAX_ENTRY_WORD] = {0};

    /* 1. mapping ecc fail rec and err tblid */
    switch (intr_tblid)
    {
        case BufStoreInterruptNormal_t:
            if (DRV_ECC_INTR_IgrPriToTcMap == intr_bit)
            {
                err_rec = DsIgrPriToTcMap_RegRam_RamChkRec_t;
                *p_tblid = DsIgrPriToTcMap_t;
            }
            else if (DRV_ECC_INTR_IgrPortTcPriMap == intr_bit)
            {
                err_rec = DsIgrPortTcPriMap_RegRam_RamChkRec_t;
                *p_tblid = DsIgrPortTcPriMap_t;
            }
            else if (DRV_ECC_INTR_IgrPortTcMinProfId == intr_bit)
            {
                err_rec = DsIgrPortTcMinProfId_RegRam_RamChkRec_t;
                *p_tblid = DsIgrPortTcMinProfId_t;
            }
            else if (DRV_ECC_INTR_IgrPortTcThrdProfId == intr_bit)
            {
                err_rec = DsIgrPortTcThrdProfId_RegRam_RamChkRec_t;
                *p_tblid = DsIgrPortTcThrdProfId_t;
            }
            else if (DRV_ECC_INTR_IgrPortThrdProfile == intr_bit)
            {
                err_rec = DsIgrPortThrdProfile_RegRam_RamChkRec_t;
                *p_tblid = DsIgrPortThrdProfile_t;
            }
            else if (DRV_ECC_INTR_IgrPortTcThrdProfile == intr_bit)
            {
                err_rec = DsIgrPortTcThrdProfile_RegRam_RamChkRec_t;
                *p_tblid = DsIgrPortTcThrdProfile_t;
            }
            else if (DRV_ECC_INTR_IgrCondDisProfId == intr_bit)
            {
                err_rec = DsIgrCondDisProfId_RegRam_RamChkRec_t;
                *p_tblid = DsIgrCondDisProfId_t;
            }
            break;

        case DynamicDsInterruptNormal_t:
            if ((DRV_ECC_INTR_Edram0 == intr_bit)
               || (DRV_ECC_INTR_Edram1 == intr_bit)
               || (DRV_ECC_INTR_Edram2 == intr_bit)
               || (DRV_ECC_INTR_Edram3 == intr_bit)
               || (DRV_ECC_INTR_Edram4 == intr_bit)
               || (DRV_ECC_INTR_Edram5 == intr_bit)
               || (DRV_ECC_INTR_Edram6 == intr_bit)
               || (DRV_ECC_INTR_Edram7 == intr_bit)
               || (DRV_ECC_INTR_Edram8 == intr_bit))
            {
                err_rec = DynamicDsEdramEccErrorRecord_t;
                *p_tblid = MaxTblId_t;
            }
            break;

        case EpeHdrEditInterruptNormal_t:
            if (DRV_ECC_INTR_PacketHeaderEditTunnel == intr_bit)
            {
                err_rec = DsPacketHeaderEditTunnel_RegRam_RamChkRec_t;
                *p_tblid = DsPacketHeaderEditTunnel_t;
            }
            break;

        case EpeHdrProcInterruptNormal_t:
            if (DRV_ECC_INTR_Ipv6NatPrefix == intr_bit)
            {
                err_rec = DsIpv6NatPrefix_RegRam_RamChkRec_t;
                *p_tblid = DsIpv6NatPrefix_t;
            }
            break;

        case EpeNextHopInterruptNormal_t:
            if (DRV_ECC_INTR_EpeEditPriorityMap == intr_bit)
            {
                err_rec = EpeEditPriorityMap_RegRam_RamChkRec_t;
                *p_tblid = EpeEditPriorityMap_t;
            }
            else if (DRV_ECC_INTR_DestChannel == intr_bit)
            {
                err_rec = DsDestChannel_RegRam_RamChkRec_t;
                *p_tblid = DsDestChannel_t;
            }
            else if (DRV_ECC_INTR_DestPort == intr_bit)
            {
                err_rec = DsDestPort_RegRam_RamChkRec_t;
                *p_tblid = DsDestPort_t;
            }
            else if (DRV_ECC_INTR_EgressVlanRangeProfile == intr_bit)
            {
                err_rec = DsEgressVlanRangeProfile_RegRam_RamChkRec_t;
                *p_tblid = DsEgressVlanRangeProfile_t;
            }
            else if (DRV_ECC_INTR_DestInterface == intr_bit)
            {
                err_rec = DsDestInterface_RegRam_RamChkRec_t;
                *p_tblid = DsDestInterface_t;
            }
            break;

         case IpeFwdInterruptNormal_t:
            if (DRV_ECC_INTR_IpeFwdDsQcnRam == intr_bit)
            {
                err_rec = IpeFwdDsQcnRam_RamChkRec_t;
                *p_tblid  = DsQcn_t;
            }
            break;

         case IpeHdrAdjInterruptNormal_t:
            if (DRV_ECC_INTR_PhyPort == intr_bit)
            {
                err_rec = DsPhyPort_RegRam_RamChkRec_t;
                *p_tblid = DsPhyPort_t;
            }
            break;

        case IpeIntfMapInterruptNormal_t:
            if (DRV_ECC_INTR_PhyPortExt == intr_bit)
            {
                err_rec = DsPhyPortExt_RegRam_RamChkRec_t;
                *p_tblid = DsPhyPortExt_t;
            }
            else if (DRV_ECC_INTR_RouterMac == intr_bit)
            {
                err_rec = DsRouterMac_RegRam_RamChkRec_t;
                *p_tblid = DsRouterMac_t;
            }
            else if (DRV_ECC_INTR_SrcInterface == intr_bit)
            {
                err_rec = DsSrcInterface_RegRam_RamChkRec_t;
                *p_tblid = DsSrcInterface_t;
            }
            else if (DRV_ECC_INTR_SrcPort == intr_bit)
            {
                err_rec = DsSrcPort_RegRam_RamChkRec_t;
                *p_tblid = DsSrcPort_t;
            }
            else if (DRV_ECC_INTR_VlanActionProfile == intr_bit)
            {
                err_rec = DsVlanActionProfile_RegRam_RamChkRec_t;
                *p_tblid = DsVlanActionProfile_t;
            }
            else if (DRV_ECC_INTR_VlanRangeProfile == intr_bit)
            {
                err_rec = DsVlanRangeProfile_RegRam_RamChkRec_t;
                *p_tblid = DsVlanRangeProfile_t;
            }
            else if (DRV_ECC_INTR_MplsCtl == intr_bit)
            {
                err_rec = DsMplsCtl_RegRam_RamChkRec_t;
                *p_tblid = DsMplsCtl_t;
            }
            break;

        case IpePktProcInterruptNormal_t:
            if (DRV_ECC_INTR_Rpf == intr_bit)
            {
                err_rec = DsRpf_RegRam_RamChkRec_t;
                *p_tblid = DsRpf_t;
            }
            else if (DRV_ECC_INTR_IpeClassificationDscpMap == intr_bit)
            {
                err_rec = IpeClassificationDscpMap_RegRam_RamChkRec_t;
                *p_tblid = IpeClassificationDscpMap_t;
            }
            else if (DRV_ECC_INTR_SrcChannel == intr_bit)
            {
                err_rec = DsSrcChannel_RegRam_RamChkRec_t;
                *p_tblid = DsSrcChannel_t;
            }
            else if (DRV_ECC_INTR_EcmpGroup == intr_bit)
            {
                err_rec = DsEcmpGroup_RegRam_RamChkRec_t;
                *p_tblid = DsEcmpGroup_t;
            }
            break;

        case MetFifoInterruptNormal_t:
            if (DRV_ECC_INTR_ApsBridge == intr_bit)
            {
                err_rec = DsApsBridge_RegRam_RamChkRec_t;
                *p_tblid = DsApsBridge_t;
            }
            break;

        case NetRxInterruptNormal_t:
            if (DRV_ECC_INTR_PauseTimerMemRd == intr_bit)
            {
                err_rec = NetRxParityFailRecord_t;
                *p_tblid = NetRxPauseTimerMem_t;
            }
            else if (DRV_ECC_INTR_ChannelizeIngFc == intr_bit)
            {
                err_rec = DsChannelizeIngFc_RegRam_RamChkRec_t;
                *p_tblid = DsChannelizeIngFc_t;
            }
            else if (DRV_ECC_INTR_ChannelizeMode == intr_bit)
            {
                err_rec = DsChannelizeMode_RegRam_RamChkRec_t;
                *p_tblid = DsChannelizeMode_t;
            }
            break;

        case QMgrEnqInterruptNormal_t:
            if (DRV_ECC_INTR_EgrResrcCtl == intr_bit)
            {
                err_rec = DsEgrResrcCtl_RegRam_RamChkRec_t;
                *p_tblid = DsEgrResrcCtl_t;
            }
            else if (DRV_ECC_INTR_HeadHashMod == intr_bit)
            {
                err_rec = DsHeadHashMod_RegRam_RamChkRec_t;
                *p_tblid = DsHeadHashMod_t;
            }
            else if (DRV_ECC_INTR_LinkAggregateMember == intr_bit)
            {
                err_rec = DsLinkAggregateMember_RegRam_RamChkRec_t;
                *p_tblid = DsLinkAggregateMember_t;
            }
            else if (DRV_ECC_INTR_LinkAggregateMemberSet == intr_bit)
            {
                err_rec = DsLinkAggregateMemberSet_RegRam_RamChkRec_t;
                *p_tblid = DsLinkAggregateMemberSet_t;
            }
            else if (DRV_ECC_INTR_LinkAggregateSgmacMember == intr_bit)
            {
                err_rec = DsLinkAggregateSgmacMember_RegRam_RamChkRec_t;
                *p_tblid = DsLinkAggregateSgmacMember_t;
            }
            else if (DRV_ECC_INTR_LinkAggregationPort == intr_bit)
            {
                err_rec = DsLinkAggregationPort_RegRam_RamChkRec_t;
                *p_tblid = DsLinkAggregationPort_t;
            }
            else if (DRV_ECC_INTR_QueThrdProfile == intr_bit)
            {
                err_rec = DsQueThrdProfile_RegRam_RamChkRec_t;
                *p_tblid = DsQueThrdProfile_t;
            }
            else if (DRV_ECC_INTR_QueueHashKey == intr_bit)
            {
                err_rec = DsQueueHashKey_RegRam_RamChkRec_t;
                *p_tblid = DsQueueHashKey_t;
            }
            else if (DRV_ECC_INTR_QueueNumGenCtl == intr_bit)
            {
                err_rec = DsQueueNumGenCtl_RegRam_RamChkRec_t;
                *p_tblid = DsQueueNumGenCtl_t;
            }
            else if (DRV_ECC_INTR_QueueSelectMap == intr_bit)
            {
                err_rec = DsQueueSelectMap_RegRam_RamChkRec_t;
                *p_tblid = DsQueueSelectMap_t;
            }
            else if (DRV_ECC_INTR_SgmacHeadHashMod == intr_bit)
            {
                err_rec = DsSgmacHeadHashMod_RegRam_RamChkRec_t;
                *p_tblid = DsSgmacHeadHashMod_t;
            }
            break;

        case SharedDsInterruptNormal_t:
            if (DRV_ECC_INTR_StpState == intr_bit)
            {
                err_rec = DsStpState_RegRam_RamChkRec_t;
                *p_tblid = DsStpState_t;
            }
            else if (DRV_ECC_INTR_VlanProfile == intr_bit)
            {
                err_rec = DsVlanProfile_RegRam_RamChkRec_t;
                *p_tblid = DsVlanProfile_t;
            }
            else if (DRV_ECC_INTR_Vlan == intr_bit)
            {
                err_rec = DsVlan_RegRam_RamChkRec_t;
                *p_tblid = DsVlan_t;
            }
            break;

        case TcamDsInterruptNormal_t:
            if ((DRV_ECC_INTR_DsTcamAd0 == intr_bit)
               || (DRV_ECC_INTR_DsTcamAd1 == intr_bit)
               || (DRV_ECC_INTR_DsTcamAd2 == intr_bit)
               || (DRV_ECC_INTR_DsTcamAd3 == intr_bit))
            {
                err_rec = TcamDsRamParityFail_t;
                *p_tblid = MaxTblId_t;
            }
            break;

        case PolicingInterruptNormal_t:
            if (DRV_ECC_INTR_PolicerControl == intr_bit)
            {
                err_rec = DsPolicerControl_RamChkRec_t;
                *p_tblid = DsPolicerControl_t;
            }
            else if ((DRV_ECC_INTR_PolicerProfile0 == intr_bit)
                    || (DRV_ECC_INTR_PolicerProfile1 == intr_bit)
                    || (DRV_ECC_INTR_PolicerProfile2 == intr_bit)
                    || (DRV_ECC_INTR_PolicerProfile3 == intr_bit))
            {
                err_rec = DsPolicerProfileRamChkRec_t;
                *p_tblid = DsPolicerProfile_t;
            }
            break;

        case IpeFibInterruptNormal_t:
            if (DRV_ECC_INTR_lpmTcamAdMem == intr_bit)
            {
                err_rec = LpmTcamAdMem_RegRam_RamChkRec_t;
                *p_tblid = LpmTcamAdMem_t;
            }
            break;

        case QMgrDeqInterruptNormal_t:
            if (DRV_ECC_INTR_QueShpWfqCtl == intr_bit)
            {
                err_rec = DsQueShpWfqCtl_RegRam_RamChkRec_t;
                *p_tblid = DsQueShpWfqCtl_t;
            }
            else if (DRV_ECC_INTR_QueShpProfile == intr_bit)
            {
                err_rec = DsQueShpProfile_RegRam_RamChkRec_t;
                *p_tblid = DsQueShpProfile_t;
            }
            else if (DRV_ECC_INTR_QueMap == intr_bit)
            {
                err_rec = DsQueMap_RegRam_RamChkRec_t;
                *p_tblid = DsQueMap_t;
            }
            else if (DRV_ECC_INTR_GrpShpProfile == intr_bit)
            {
                err_rec = DsGrpShpProfile_RegRam_RamChkRec_t;
                *p_tblid = DsGrpShpProfile_t;
            }
            else if (DRV_ECC_INTR_ChanShpProfile == intr_bit)
            {
                err_rec = DsChanShpProfile_RegRam_RamChkRec_t;
                *p_tblid = DsChanShpProfile_t;
            }
            else if (DRV_ECC_INTR_QueShpCtl == intr_bit)
            {
                err_rec = DsQueShpCtl_RegRam_RamChkRec_t;
                *p_tblid = DsQueShpCtl_t;
            }
            else if (DRV_ECC_INTR_GrpShpWfqCtl == intr_bit)
            {
                err_rec = DsGrpShpWfqCtl_RegRam_RamChkRec_t;
                *p_tblid = DsGrpShpWfqCtl_t;
            }
            break;

         case BufRetrvInterruptNormal_t:
            if (DRV_ECC_INTR_BufRetrvExcp == intr_bit)
            {
                err_rec = DsBufRetrvExcp_RegRam_RamChkRec_t;
                *p_tblid = DsBufRetrvExcp_t;
            }

         default:
            return DRV_E_INVALID_PARAMETER;
    }

    /* 2. read ecc fail rec to get fail valid bit and ecc error tbl idx */
    cmd = DRV_IOR(err_rec, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, data));

    switch (err_rec)
    {
        /* BufStoreInterruptNormal */
        case DsIgrPriToTcMap_RegRam_RamChkRec_t:
            fail = ((ds_igr_pri_to_tc_map__reg_ram__ram_chk_rec_t*)data)->ds_igr_pri_to_tc_map_parity_fail;
            *p_offset = ((ds_igr_pri_to_tc_map__reg_ram__ram_chk_rec_t*)data)->ds_igr_pri_to_tc_map_parity_fail_addr;
            break;
        case DsIgrPortTcPriMap_RegRam_RamChkRec_t:
            fail = ((ds_igr_port_tc_pri_map__reg_ram__ram_chk_rec_t*)data)->ds_igr_port_tc_pri_map_parity_fail;
            *p_offset = ((ds_igr_port_tc_pri_map__reg_ram__ram_chk_rec_t*)data)->ds_igr_port_tc_pri_map_parity_fail_addr;
            break;
        case DsIgrPortTcMinProfId_RegRam_RamChkRec_t:
            fail = ((ds_igr_port_tc_min_prof_id__reg_ram__ram_chk_rec_t*)data)->ds_igr_port_tc_min_prof_id_parity_fail;
            *p_offset = ((ds_igr_port_tc_min_prof_id__reg_ram__ram_chk_rec_t*)data)->ds_igr_port_tc_min_prof_id_parity_fail_addr;
            break;
        case DsIgrPortTcThrdProfId_RegRam_RamChkRec_t:
            fail = ((ds_igr_port_tc_thrd_prof_id__reg_ram__ram_chk_rec_t*)data)->ds_igr_port_tc_thrd_prof_id_parity_fail;
            *p_offset = ((ds_igr_port_tc_thrd_prof_id__reg_ram__ram_chk_rec_t*)data)->ds_igr_port_tc_thrd_prof_id_parity_fail_addr;
            break;
        case DsIgrPortThrdProfile_RegRam_RamChkRec_t:
            fail = ((ds_igr_port_thrd_profile__reg_ram__ram_chk_rec_t*)data)->ds_igr_port_thrd_profile_parity_fail;
            *p_offset = ((ds_igr_port_thrd_profile__reg_ram__ram_chk_rec_t*)data)->ds_igr_port_thrd_profile_parity_fail_addr;
            break;
        case DsIgrPortTcThrdProfile_RegRam_RamChkRec_t:
            fail = ((ds_igr_port_tc_thrd_profile__reg_ram__ram_chk_rec_t*)data)->ds_igr_port_tc_thrd_profile_parity_fail;
            *p_offset = ((ds_igr_port_tc_thrd_profile__reg_ram__ram_chk_rec_t*)data)->ds_igr_port_tc_thrd_profile_parity_fail_addr;
            break;
        case DsIgrCondDisProfId_RegRam_RamChkRec_t:
            fail = ((ds_igr_cond_dis_prof_id__reg_ram__ram_chk_rec_t*)data)->ds_igr_cond_dis_prof_id_parity_fail;
            *p_offset = ((ds_igr_cond_dis_prof_id__reg_ram__ram_chk_rec_t*)data)->ds_igr_cond_dis_prof_id_parity_fail_addr;
            break;

        /* DynamicDsInterruptNormal */
        case DynamicDsEdramEccErrorRecord_t:
            if (DRV_ECC_INTR_Edram0 == intr_bit)
            {
                fail = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram0_ecc_fail;
                *p_offset = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram0_ecc_fail_addr;
            }
            else if (DRV_ECC_INTR_Edram1 == intr_bit)
            {
                fail = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram1_ecc_fail;
                *p_offset = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram1_ecc_fail_addr;
            }
            else if (DRV_ECC_INTR_Edram2 == intr_bit)
            {
                fail = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram2_ecc_fail;
                *p_offset = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram2_ecc_fail_addr;
            }
            else if (DRV_ECC_INTR_Edram3 == intr_bit)
            {
                fail = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram3_ecc_fail;
                *p_offset = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram3_ecc_fail_addr;
            }
            else if (DRV_ECC_INTR_Edram4 == intr_bit)
            {
                fail = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram4_ecc_fail;
                *p_offset = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram4_ecc_fail_addr;
            }
            else if (DRV_ECC_INTR_Edram5 == intr_bit)
            {
                fail = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram5_ecc_fail;
                *p_offset = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram5_ecc_fail_addr;
            }
            else if (DRV_ECC_INTR_Edram6 == intr_bit)
            {
                fail = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram6_ecc_fail;
                *p_offset = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram6_ecc_fail_addr;
            }
            else if (DRV_ECC_INTR_Edram7 == intr_bit)
            {
                fail = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram7_ecc_fail;
                *p_offset = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram7_ecc_fail_addr;
            }
            else if (DRV_ECC_INTR_Edram8 == intr_bit)
            {
                fail = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram8_ecc_fail;
                *p_offset = ((dynamic_ds_edram_ecc_error_record_t*)data)->e_dram8_ecc_fail_addr;
            }
            break;

        /* EpeHdrEditInterruptNormal */
        case DsPacketHeaderEditTunnel_RegRam_RamChkRec_t:
            fail = ((ds_packet_header_edit_tunnel__reg_ram__ram_chk_rec_t*)data)->ds_packet_header_edit_tunnel_parity_fail;
            *p_offset = ((ds_packet_header_edit_tunnel__reg_ram__ram_chk_rec_t*)data)->ds_packet_header_edit_tunnel_parity_fail_addr;
            break;

        /* EpeHdrProcInterruptNormal */
        case DsIpv6NatPrefix_RegRam_RamChkRec_t:
            fail = ((ds_ipv6_nat_prefix__reg_ram__ram_chk_rec_t*)data)->ds_ipv6_nat_prefix_parity_fail;
            *p_offset = ((ds_ipv6_nat_prefix__reg_ram__ram_chk_rec_t*)data)->ds_ipv6_nat_prefix_parity_fail_addr;
            break;

        /* EpeNextHopInterruptNormal */
        case EpeEditPriorityMap_RegRam_RamChkRec_t:
            fail = ((epe_edit_priority_map__reg_ram__ram_chk_rec_t*)data)->epe_edit_priority_map_parity_fail;
            *p_offset = ((epe_edit_priority_map__reg_ram__ram_chk_rec_t*)data)->epe_edit_priority_map_parity_fail_addr;
            break;
        case DsDestChannel_RegRam_RamChkRec_t:
            fail = ((ds_dest_channel__reg_ram__ram_chk_rec_t*)data)->ds_dest_channel_parity_fail;
            *p_offset = ((ds_dest_channel__reg_ram__ram_chk_rec_t*)data)->ds_dest_channel_parity_fail_addr;
            break;
        case DsDestPort_RegRam_RamChkRec_t:
            fail = ((ds_dest_port__reg_ram__ram_chk_rec_t*)data)->ds_dest_port_parity_fail;
            *p_offset = ((ds_dest_port__reg_ram__ram_chk_rec_t*)data)->ds_dest_port_parity_fail_addr;
            break;
        case DsEgressVlanRangeProfile_RegRam_RamChkRec_t:
            fail = ((ds_egress_vlan_range_profile__reg_ram__ram_chk_rec_t*)data)->ds_egress_vlan_range_profile_parity_fail;
            *p_offset = ((ds_egress_vlan_range_profile__reg_ram__ram_chk_rec_t*)data)->ds_egress_vlan_range_profile_parity_fail_addr;
            break;
        case DsDestInterface_RegRam_RamChkRec_t:
            fail = ((ds_dest_interface__reg_ram__ram_chk_rec_t*)data)->ds_dest_interface_parity_fail;
            *p_offset = ((ds_dest_interface__reg_ram__ram_chk_rec_t*)data)->ds_dest_interface_parity_fail_addr;
            break;

        /* IpeFwdInterruptNormal */
        case IpeFwdDsQcnRam_RamChkRec_t:
            fail = ((ipe_fwd_ds_qcn_ram__ram_chk_rec_t*)data)->ipe_fwd_ds_qcn_ram_parity_fail;
            *p_offset = ((ipe_fwd_ds_qcn_ram__ram_chk_rec_t*)data)->ipe_fwd_ds_qcn_ram_parity_fail_addr;
            break;

        /* IpeHdrAdjInterruptNormal */
        case DsPhyPort_RegRam_RamChkRec_t:
            fail = ((ds_phy_port__reg_ram__ram_chk_rec_t*)data)->ds_phy_port_parity_fail;
            *p_offset = ((ds_phy_port__reg_ram__ram_chk_rec_t*)data)->ds_phy_port_parity_fail_addr;
            break;

        /* IpeIntfMapInterruptNormal */
        case DsPhyPortExt_RegRam_RamChkRec_t:
            fail = ((ds_phy_port_ext__reg_ram__ram_chk_rec_t*)data)->ds_phy_port_ext_parity_fail;
            *p_offset = ((ds_phy_port_ext__reg_ram__ram_chk_rec_t*)data)->ds_phy_port_ext_parity_fail_addr;
            break;
        case DsRouterMac_RegRam_RamChkRec_t:
            fail = ((ds_router_mac__reg_ram__ram_chk_rec_t*)data)->ds_router_mac_parity_fail;
            *p_offset = ((ds_router_mac__reg_ram__ram_chk_rec_t*)data)->ds_router_mac_parity_fail_addr;
            break;
        case DsSrcInterface_RegRam_RamChkRec_t:
            fail = ((ds_src_interface__reg_ram__ram_chk_rec_t*)data)->ds_src_interface_parity_fail;
            *p_offset = ((ds_src_interface__reg_ram__ram_chk_rec_t*)data)->ds_src_interface_parity_fail_addr;
            break;
        case DsSrcPort_RegRam_RamChkRec_t:
            fail = ((ds_src_port__reg_ram__ram_chk_rec_t*)data)->ds_src_port_parity_fail;
            *p_offset = ((ds_src_port__reg_ram__ram_chk_rec_t*)data)->ds_src_port_parity_fail_addr;
            break;
        case DsVlanActionProfile_RegRam_RamChkRec_t:
            fail = ((ds_vlan_action_profile__reg_ram__ram_chk_rec_t*)data)->ds_vlan_action_profile_parity_fail;
            *p_offset = ((ds_vlan_action_profile__reg_ram__ram_chk_rec_t*)data)->ds_vlan_action_profile_parity_fail_addr;
            break;
        case DsVlanRangeProfile_RegRam_RamChkRec_t:
            fail = ((ds_vlan_range_profile__reg_ram__ram_chk_rec_t*)data)->ds_vlan_range_profile_parity_fail;
            *p_offset = ((ds_vlan_range_profile__reg_ram__ram_chk_rec_t*)data)->ds_vlan_range_profile_parity_fail_addr;
            break;
        case DsMplsCtl_RegRam_RamChkRec_t:
            fail = ((ds_mpls_ctl__reg_ram__ram_chk_rec_t*)data)->ds_mpls_ctl_parity_fail;
            *p_offset = ((ds_mpls_ctl__reg_ram__ram_chk_rec_t*)data)->ds_mpls_ctl_parity_fail_addr;
            break;

        /* IpePktProcInterruptNormal */
        case DsRpf_RegRam_RamChkRec_t:
            fail = ((ds_rpf__reg_ram__ram_chk_rec_t*)data)->ds_rpf_parity_fail;
            *p_offset = ((ds_rpf__reg_ram__ram_chk_rec_t*)data)->ds_rpf_parity_fail_addr;
            break;
        case IpeClassificationDscpMap_RegRam_RamChkRec_t:
            fail = ((ipe_classification_dscp_map__reg_ram__ram_chk_rec_t*)data)->ipe_classification_dscp_map_parity_fail;
            *p_offset = ((ipe_classification_dscp_map__reg_ram__ram_chk_rec_t*)data)->ipe_classification_dscp_map_parity_fail_addr;
            break;
        case DsSrcChannel_RegRam_RamChkRec_t:
            fail = ((ds_src_channel__reg_ram__ram_chk_rec_t*)data)->ds_src_channel_parity_fail;
            *p_offset = ((ds_src_channel__reg_ram__ram_chk_rec_t*)data)->ds_src_channel_parity_fail_addr;
            break;
        case DsEcmpGroup_RegRam_RamChkRec_t:
            fail = ((ds_ecmp_group__reg_ram__ram_chk_rec_t*)data)->ds_ecmp_group_parity_fail;
            *p_offset = ((ds_ecmp_group__reg_ram__ram_chk_rec_t*)data)->ds_ecmp_group_parity_fail_addr;
            break;

        /* MetFifoInterruptNormal */
        case DsApsBridge_RegRam_RamChkRec_t:
            fail = ((ds_aps_bridge__reg_ram__ram_chk_rec_t*)data)->ds_aps_bridge_parity_fail;
            *p_offset = ((ds_aps_bridge__reg_ram__ram_chk_rec_t*)data)->ds_aps_bridge_parity_fail_addr;
            break;

        /* NetRxInterruptNormal */
        case NetRxParityFailRecord_t:
            fail = ((net_rx_parity_fail_record_t*)data)->pause_timer_memory_parity_fail;
            *p_offset = ((net_rx_parity_fail_record_t*)data)->pause_timer_memory_parity_fail_addr;
            break;
        case DsChannelizeIngFc_RegRam_RamChkRec_t:
            fail = ((ds_channelize_ing_fc__reg_ram__ram_chk_rec_t*)data)->ds_channelize_ing_fc_parity_fail;
            *p_offset = ((ds_channelize_ing_fc__reg_ram__ram_chk_rec_t*)data)->ds_channelize_ing_fc_parity_fail_addr;
            break;
        case DsChannelizeMode_RegRam_RamChkRec_t:
            fail = ((ds_channelize_mode__reg_ram__ram_chk_rec_t*)data)->ds_channelize_mode_parity_fail;
            *p_offset = ((ds_channelize_mode__reg_ram__ram_chk_rec_t*)data)->ds_channelize_mode_parity_fail_addr;
            break;

        /* QMgrEnqInterruptNormal */
        case DsHeadHashMod_RegRam_RamChkRec_t:
            fail = ((ds_head_hash_mod__reg_ram__ram_chk_rec_t*)data)->ds_head_hash_mod_parity_fail;
            *p_offset = ((ds_head_hash_mod__reg_ram__ram_chk_rec_t*)data)->ds_head_hash_mod_parity_fail_addr;
            break;
        case DsLinkAggregateMemberSet_RegRam_RamChkRec_t:
            fail = ((ds_link_aggregate_member_set__reg_ram__ram_chk_rec_t*)data)->ds_link_aggregate_member_set_parity_fail;
            *p_offset = ((ds_link_aggregate_member_set__reg_ram__ram_chk_rec_t*)data)->ds_link_aggregate_member_set_parity_fail_addr;
            break;
        case DsQueueNumGenCtl_RegRam_RamChkRec_t:
            fail = ((ds_queue_num_gen_ctl__reg_ram__ram_chk_rec_t*)data)->ds_queue_num_gen_ctl_parity_fail;
            *p_offset = ((ds_queue_num_gen_ctl__reg_ram__ram_chk_rec_t*)data)->ds_queue_num_gen_ctl_parity_fail_addr;
            break;
        case DsQueThrdProfile_RegRam_RamChkRec_t:
            fail = ((ds_que_thrd_profile__reg_ram__ram_chk_rec_t*)data)->ds_que_thrd_profile_parity_fail;
            *p_offset = ((ds_que_thrd_profile__reg_ram__ram_chk_rec_t*)data)->ds_que_thrd_profile_parity_fail_addr;
            break;
        case DsQueueSelectMap_RegRam_RamChkRec_t:
            fail = ((ds_queue_select_map__reg_ram__ram_chk_rec_t*)data)->ds_queue_select_map_parity_fail;
            *p_offset = ((ds_queue_select_map__reg_ram__ram_chk_rec_t*)data)->ds_queue_select_map_parity_fail_addr;
            break;
        case DsLinkAggregateMember_RegRam_RamChkRec_t:
            fail = ((ds_link_aggregate_member_set__reg_ram__ram_chk_rec_t*)data)->ds_link_aggregate_member_set_parity_fail;
            *p_offset = ((ds_link_aggregate_member_set__reg_ram__ram_chk_rec_t*)data)->ds_link_aggregate_member_set_parity_fail_addr;
            break;
        case DsLinkAggregateSgmacMember_RegRam_RamChkRec_t:
            fail = ((ds_link_aggregate_sgmac_member__reg_ram__ram_chk_rec_t*)data)->ds_link_aggregate_sgmac_member_parity_fail;
            *p_offset = ((ds_link_aggregate_sgmac_member__reg_ram__ram_chk_rec_t*)data)->ds_link_aggregate_sgmac_member_parity_fail_addr;
            break;
        case DsSgmacHeadHashMod_RegRam_RamChkRec_t:
            fail = ((ds_sgmac_head_hash_mod__reg_ram__ram_chk_rec_t*)data)->ds_sgmac_head_hash_mod_parity_fail;
            *p_offset = ((ds_sgmac_head_hash_mod__reg_ram__ram_chk_rec_t*)data)->ds_sgmac_head_hash_mod_parity_fail_addr;
            break;
        case DsEgrResrcCtl_RegRam_RamChkRec_t:
            fail = ((ds_egr_resrc_ctl__reg_ram__ram_chk_rec_t*)data)->ds_egr_resrc_ctl_parity_fail;
            *p_offset = ((ds_egr_resrc_ctl__reg_ram__ram_chk_rec_t*)data)->ds_egr_resrc_ctl_parity_fail_addr;
            break;
        case DsQueueHashKey_RegRam_RamChkRec_t:
            fail = ((ds_queue_hash_key__reg_ram__ram_chk_rec_t*)data)->ds_queue_hash_key_parity_fail;
            *p_offset = ((ds_queue_hash_key__reg_ram__ram_chk_rec_t*)data)->ds_queue_hash_key_parity_fail_addr;
            break;
        case DsLinkAggregationPort_RegRam_RamChkRec_t:
            fail = ((ds_link_aggregation_port__reg_ram__ram_chk_rec_t*)data)->ds_link_aggregation_port_parity_fail;
            *p_offset = ((ds_link_aggregation_port__reg_ram__ram_chk_rec_t*)data)->ds_link_aggregation_port_parity_fail_addr;
            break;

        /* SharedDsInterruptNormal */
        case DsStpState_RegRam_RamChkRec_t:
            fail = ((ds_stp_state__reg_ram__ram_chk_rec_t*)data)->ds_stp_state_parity_fail;
            *p_offset = ((ds_stp_state__reg_ram__ram_chk_rec_t*)data)->ds_stp_state_parity_fail_addr;
            break;
        case DsVlanProfile_RegRam_RamChkRec_t:
            fail = ((ds_vlan_profile__reg_ram__ram_chk_rec_t*)data)->ds_vlan_profile_parity_fail;
            *p_offset = ((ds_vlan_profile__reg_ram__ram_chk_rec_t*)data)->ds_vlan_profile_parity_fail_addr;
            break;
        case DsVlan_RegRam_RamChkRec_t:
            fail = ((ds_vlan__reg_ram__ram_chk_rec_t*)data)->ds_vlan_parity_fail;
            *p_offset = ((ds_vlan__reg_ram__ram_chk_rec_t*)data)->ds_vlan_parity_fail_addr;
            break;

        /* TcamDsInterruptNormal  */
        case TcamDsRamParityFail_t:
            if (DRV_ECC_INTR_DsTcamAd0 == intr_bit)
            {
                fail = ((tcam_ds_ram_parity_fail_t*)data)->ds_ram_parity_fail0;
                *p_offset = ((tcam_ds_ram_parity_fail_t*)data)->ds_ram_parity_fail0_addr;
            }
            else if (DRV_ECC_INTR_DsTcamAd1 == intr_bit)
            {
                fail = ((tcam_ds_ram_parity_fail_t*)data)->ds_ram_parity_fail1;
                *p_offset = ((tcam_ds_ram_parity_fail_t*)data)->ds_ram_parity_fail1_addr;
            }
            else if (DRV_ECC_INTR_DsTcamAd2 == intr_bit)
            {
                fail = ((tcam_ds_ram_parity_fail_t*)data)->ds_ram_parity_fail2;
                *p_offset = ((tcam_ds_ram_parity_fail_t*)data)->ds_ram_parity_fail2_addr;
            }
            else if (DRV_ECC_INTR_DsTcamAd3 == intr_bit)
            {
                fail = ((tcam_ds_ram_parity_fail_t*)data)->ds_ram_parity_fail3;
                *p_offset = ((tcam_ds_ram_parity_fail_t*)data)->ds_ram_parity_fail3_addr;
            }
            break;

        /* PolicingInterruptNormal */
        case DsPolicerControl_RamChkRec_t:
            fail = ((ds_policer_control__ram_chk_rec_t*)data)->ds_policer_control_parity_fail;
            *p_offset = ((ds_policer_control__ram_chk_rec_t*)data)->ds_policer_control_parity_fail_addr;
            break;
        case DsPolicerProfileRamChkRec_t:
            fail = ((ds_policer_profile_ram_chk_rec_t*)data)->ds_policer_profile_parity_fail;
            idx = ((ds_policer_profile_ram_chk_rec_t*)data)->ds_policer_profile_parity_fail_mem;
            if (0 == idx)
            {
                *p_offset = ((ds_policer_profile_ram_chk_rec_t*)data)->ds_policer_profile0_parity_fail_addr;
            }
            else if (1 == idx)
            {
                *p_offset = ((ds_policer_profile_ram_chk_rec_t*)data)->ds_policer_profile1_parity_fail_addr;
            }
            else if (2 == idx)
            {
                *p_offset = ((ds_policer_profile_ram_chk_rec_t*)data)->ds_policer_profile2_parity_fail_addr;
            }
            else if (3 == idx)
            {
                *p_offset = ((ds_policer_profile_ram_chk_rec_t*)data)->ds_policer_profile3_parity_fail_addr;
            }
            break;

        /* IpeFibInterruptNormal */
        case LpmTcamAdMem_RegRam_RamChkRec_t:
            fail = ((lpm_tcam_ad_mem__reg_ram__ram_chk_rec_t*)data)->lpm_tcam_ad_mem_parity_fail;
            *p_offset = ((lpm_tcam_ad_mem__reg_ram__ram_chk_rec_t*)data)->lpm_tcam_ad_mem_parity_fail_addr;
            break;

        /* QMgrDeqInterruptNormal */
        case DsQueShpWfqCtl_RegRam_RamChkRec_t:
            fail = ((ds_que_shp_wfq_ctl__reg_ram__ram_chk_rec_t*)data)->ds_que_shp_wfq_ctl_parity_fail;
            *p_offset = ((ds_que_shp_wfq_ctl__reg_ram__ram_chk_rec_t*)data)->ds_que_shp_wfq_ctl_parity_fail_addr;
            break;
        case DsQueShpProfile_RegRam_RamChkRec_t:
            fail = ((ds_que_shp_profile__reg_ram__ram_chk_rec_t*)data)->ds_que_shp_profile_parity_fail;
            *p_offset = ((ds_que_shp_profile__reg_ram__ram_chk_rec_t*)data)->ds_que_shp_profile_parity_fail_addr;
            break;
        case DsQueMap_RegRam_RamChkRec_t:
            fail = ((ds_que_map__reg_ram__ram_chk_rec_t*)data)->ds_que_map_parity_fail;
            *p_offset = ((ds_que_map__reg_ram__ram_chk_rec_t*)data)->ds_que_map_parity_fail_addr;
            break;
        case DsGrpShpProfile_RegRam_RamChkRec_t:
            fail = ((ds_grp_shp_profile__reg_ram__ram_chk_rec_t*)data)->ds_grp_shp_profile_parity_fail;
            *p_offset = ((ds_grp_shp_profile__reg_ram__ram_chk_rec_t*)data)->ds_grp_shp_profile_parity_fail_addr;
            break;
        case DsChanShpProfile_RegRam_RamChkRec_t:
            fail = ((ds_chan_shp_profile__reg_ram__ram_chk_rec_t*)data)->ds_chan_shp_profile_parity_fail;
            *p_offset = ((ds_chan_shp_profile__reg_ram__ram_chk_rec_t*)data)->ds_chan_shp_profile_parity_fail_addr;
            break;
        case DsQueShpCtl_RegRam_RamChkRec_t:
            fail = ((ds_que_shp_ctl__reg_ram__ram_chk_rec_t*)data)->ds_que_shp_ctl_parity_fail;
            *p_offset = ((ds_que_shp_ctl__reg_ram__ram_chk_rec_t*)data)->ds_que_shp_ctl_parity_fail_addr;
            break;
        case DsGrpShpWfqCtl_RegRam_RamChkRec_t:
            fail = ((ds_grp_shp_wfq_ctl__reg_ram__ram_chk_rec_t*)data)->ds_grp_shp_wfq_ctl_parity_fail;
            *p_offset = ((ds_grp_shp_wfq_ctl__reg_ram__ram_chk_rec_t*)data)->ds_grp_shp_wfq_ctl_parity_fail_addr;
            break;

        /* BufRetrvInterruptNormal */
        case DsBufRetrvExcp_RegRam_RamChkRec_t:
            fail = ((ds_buf_retrv_excp__reg_ram__ram_chk_rec_t*)data)->ds_buf_retrv_excp_parity_fail;
            *p_offset = ((ds_buf_retrv_excp__reg_ram__ram_chk_rec_t*)data)->ds_buf_retrv_excp_parity_fail_addr;
            break;

        default:
            return DRV_E_FATAL_EXCEP;
    }

    if (!fail)
    {
        ret = DRV_E_FATAL_EXCEP;
        DRV_DBG_INFO("%s's intr ecc err bit[%u] mismatch with %s's fail bit \n",
                     TABLE_NAME(intr_tblid), intr_bit, TABLE_NAME(err_rec));
    }

    return ret;
}

STATIC int32
_drv_greatbelt_ecc_recover_static_tbl(uint8 lchip, tbls_id_t tblid, uint32 tblidx)
{
    uint32  entry_size = 0, offset = 0;
    uintptr start_hw_data_addr = 0, start_sw_data_addr = 0;
    drv_ecc_mem_type_t mem_type = DRV_ECC_MEM_INVALID;

    _drv_greatbelt_ecc_recover_tbl2mem(lchip, tblid, tblidx, &mem_type, &offset);

    if (mem_type <= DRV_ECC_MEM_BufRetrvExcp)
    {
        entry_size = TABLE_ENTRY_SIZE(tblid);

        ECC_LOCK;
        start_hw_data_addr = TABLE_DATA_BASE(tblid) + tblidx * entry_size;
        start_sw_data_addr = g_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip] + tblidx * entry_size;
        DRV_ERROR_RETURN_WITH_ECC_UNLOCK(drv_greatbelt_chip_write_sram_entry(lchip, (uint32)start_hw_data_addr,
                                                                   (uint32*)start_sw_data_addr, entry_size));
        ECC_UNLOCK;
        g_ecc_recover_master->ecc_mem[mem_type].recover_cnt[lchip]++;
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_greatbelt_ecc_recover_dynamic_tbl(uint8 lchip, tbls_id_t intr_tblid, uint32 intr_bit, uint32 offset)
{
    uint8      recover = 0, step = 0;
    uintptr    start_hw_data_addr = 0, start_sw_data_addr = 0;
    drv_ecc_mem_type_t mem_type = DRV_ECC_MEM_INVALID;

    if (DynamicDsInterruptNormal_t == intr_tblid)
    {
        switch (intr_bit)
        {
            case DRV_ECC_INTR_Edram0:
                mem_type = DRV_ECC_MEM_Edram0;
                break;
            case DRV_ECC_INTR_Edram1:
                mem_type = DRV_ECC_MEM_Edram1;
                break;
            case DRV_ECC_INTR_Edram2:
                mem_type = DRV_ECC_MEM_Edram2;
                break;
            case DRV_ECC_INTR_Edram3:
                mem_type = DRV_ECC_MEM_Edram3;
                break;
            case DRV_ECC_INTR_Edram4:
                mem_type = DRV_ECC_MEM_Edram4;
                break;
            case DRV_ECC_INTR_Edram5:
                mem_type = DRV_ECC_MEM_Edram5;
                break;
            case DRV_ECC_INTR_Edram6:
                mem_type = DRV_ECC_MEM_Edram6;
                break;
            case DRV_ECC_INTR_Edram7:
                mem_type = DRV_ECC_MEM_Edram7;
                break;
            case DRV_ECC_INTR_Edram8:
                mem_type = DRV_ECC_MEM_Edram8;
                break;
            default:
                return DRV_E_FATAL_EXCEP;
        }

        start_hw_data_addr = DYNAMIC_DS_EDRAM8_W_OFFSET + edram_offset[mem_type - DRV_ECC_MEM_Edram0];
        step = (7 == (mem_type - DRV_ECC_MEM_Edram0)) ? 4 : 2;

        g_ecc_recover_master->ftm_check_cb(lchip, (mem_type - DRV_ECC_MEM_Edram0), offset * step, &recover);
        if (1 == recover)
        {
            start_hw_data_addr = start_hw_data_addr + offset * (DRV_ADDR_BYTES_PER_ENTRY * step);
            ECC_LOCK;
            start_sw_data_addr = g_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip]
                                 + offset * (DRV_BYTES_PER_ENTRY * step);

            DRV_ERROR_RETURN_WITH_ECC_UNLOCK(drv_greatbelt_chip_write_sram_entry(lchip, (uint32)start_hw_data_addr,
                                                (uint32*)start_sw_data_addr, DRV_BYTES_PER_ENTRY * step));
            ECC_UNLOCK;
            g_ecc_recover_master->ecc_mem[mem_type].recover_cnt[lchip]++;
        }
    }
    else if (TcamDsRamParityFail_t == intr_tblid)
    {
        switch (intr_bit)
        {
            case DRV_ECC_INTR_DsTcamAd0:
                mem_type = DRV_ECC_MEM_TcamAd0;
                break;
            case DRV_ECC_INTR_DsTcamAd1:
                mem_type = DRV_ECC_MEM_TcamAd1;
                break;
            case DRV_ECC_INTR_DsTcamAd2:
                mem_type = DRV_ECC_MEM_TcamAd2;
                break;
            case DRV_ECC_INTR_DsTcamAd3:
                mem_type = DRV_ECC_MEM_TcamAd3;
                break;
            default:
                return DRV_E_FATAL_EXCEP;
        }

        start_hw_data_addr = DRV_INT_TCAM_AD_MEM_8W_BASE
                             + ((mem_type - DRV_ECC_MEM_TcamAd0) * (DRV_INT_TCAM_AD_MAX_ENTRY_NUM/4))
                             + offset * (DRV_ADDR_BYTES_PER_ENTRY * 2);
        ECC_LOCK;
        start_sw_data_addr = g_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip]
                             + offset * (DRV_BYTES_PER_ENTRY * 2);
        DRV_ERROR_RETURN_WITH_ECC_UNLOCK(drv_greatbelt_chip_write_sram_entry(lchip, (uint32)start_hw_data_addr,
                                         (uint32*)start_sw_data_addr, DRV_BYTES_PER_ENTRY * 2));
        ECC_UNLOCK;
        g_ecc_recover_master->ecc_mem[mem_type].recover_cnt[lchip]++;
    }
    else
    {
        return DRV_E_FATAL_EXCEP;
    }

    return DRV_E_NONE;
}

/**
 @brief The function write tbl data to ecc error recover memory for resume
*/
void
drv_greatbelt_ecc_recover_store(uint8 lchip, tbls_id_t tbl_id, uint32 tbl_idx, uint32* p_data)
{
    uintptr  start_addr = 0;
    uintptr start_sw_data_addr = 0;
    uint32  offset = 0;
    drv_ecc_mem_type_t mem_type;

    if (lchip >= drv_gb_init_chip_info.drv_init_chip_num)
    {
        return;
    }

    if (NULL == g_ecc_recover_master)
    {
        return;
    }

    if ((PolicingMiscCtl_t == tbl_id) || (DsPolicerProfile_t == tbl_id))
    {
        return;
    }

    _drv_greatbelt_ecc_recover_tbl2mem(lchip, tbl_id, tbl_idx, &mem_type, &offset);

    if (mem_type >= DRV_ECC_MEM_NUM)
    {
        return;
    }

    start_addr = g_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip];

    ECC_LOCK;
    start_sw_data_addr = start_addr + offset;
    sal_memcpy((uint32*)start_sw_data_addr, p_data, TABLE_ENTRY_SIZE(tbl_id));
    ECC_UNLOCK;

    return;
}

/**
 @brief The function recover the ecc error memory with handle info
*/
int32
drv_greatbelt_ecc_recover_restore(uint8 lchip, drv_ecc_intr_param_t* p_intr_param)
{
    uint8     intr_bit = 0, valid_cnt = 0;
    uint32    offset = 0;
    tbls_id_t tblid = MaxTblId_t;
    drv_ecc_cb_info_t ecc_cb_info;

    DRV_CHIP_ID_VALID_CHECK(lchip + drv_gb_init_chip_info.drv_init_chipid_base);
    DRV_PTR_VALID_CHECK(p_intr_param);

    if (NULL == g_ecc_recover_master)
    {
        return DRV_E_NONE;
    }

    for (intr_bit = 0; (valid_cnt < p_intr_param->valid_bit_count) && (intr_bit < p_intr_param->total_bit_num); intr_bit++)
    {
        if (DRV_BMP_ISSET(p_intr_param->p_bmp, intr_bit))
        {
            DRV_IF_ERROR_RETURN(_drv_greatbelt_ecc_recover_proc_intr(lchip, p_intr_param->intr_tblid, intr_bit, &tblid, &offset));
            valid_cnt++;

            if ((DynamicDsInterruptNormal_t == p_intr_param->intr_tblid)
               || (TcamDsRamParityFail_t == p_intr_param->intr_tblid))
            {
                DRV_IF_ERROR_RETURN(_drv_greatbelt_ecc_recover_dynamic_tbl(lchip, p_intr_param->intr_tblid, intr_bit, offset));
            }
            else
            {
                DRV_IF_ERROR_RETURN(_drv_greatbelt_ecc_recover_static_tbl(lchip, tblid, offset));
            }
            ecc_cb_info.action = 1;
            ecc_cb_info.recover = 1;
            ecc_cb_info.tbl_id = tblid;
            ecc_cb_info.type = 2;
            ecc_cb_info.tbl_index = offset;
            _drv_greatbelt_ecc_recover_error_info(lchip, &ecc_cb_info);
        }
    }

    return DRV_E_NONE;
}

/**
 @brief The function show parity error recover store memory value
*/
int32
drv_greatbelt_ecc_recover_show_status(uint8 lchip, uint32 is_all)
{
#define DRV_ECC_HEX_FORMAT(S, F, V, W)                        \
                          ((sal_sprintf(F, "0x%%0%dX", W))   \
                           ? ((sal_sprintf(S, F, V)) ? S : NULL) : NULL)
    uint8 type = 0;
    uint32 rcv_cnt = 0;

    DRV_CHIP_ID_VALID_CHECK(lchip + drv_gb_init_chip_info.drv_init_chipid_base);

    DRV_DBG_INFO("\n");
    DRV_DBG_INFO(" EccParityReocver:%s\n", (NULL != g_ecc_recover_master) ? "Enable" : "Disable");
    DRV_DBG_INFO(" ------------------------------------\n");

    if (NULL != g_ecc_recover_master)
    {
        for (type = 0; type < DRV_ECC_MEM_NUM; type++)
        {
            rcv_cnt += g_ecc_recover_master->ecc_mem[type].recover_cnt[lchip];
        }
    }

    DRV_DBG_INFO(" %-15s%-8s%-16s\n", "Type", "Count", "RecoverStatus");
    DRV_DBG_INFO(" ------------------------------------\n");
    DRV_DBG_INFO(" %-15s%-8u%-16s\n", "RecoverError", rcv_cnt, "SDK Recover");
    DRV_DBG_INFO(" ------------------------------------\n");

    if ((NULL == g_ecc_recover_master) || (!is_all))
    {
        return DRV_E_NONE;
    }

    DRV_DBG_INFO("\n");
    DRV_DBG_INFO(" Ecc recover memory size:%u.%u(MB)\n",
                ((g_ecc_recover_master->mem_size * 100) / (1024 * 1024)) / 100,
                ((g_ecc_recover_master->mem_size * 100) / (1024 * 1024)) % 100);
    DRV_DBG_INFO(" ---------------------------------------\n");

    DRV_DBG_INFO(" %-29s%s\n", "EccMem", "RecoverCnt");
    DRV_DBG_INFO(" ---------------------------------------\n");

    for (type = 0; type < DRV_ECC_MEM_NUM; type++)
    {
        if (0 == is_all)
        {
            if (0 != g_ecc_recover_master->ecc_mem[type].recover_cnt[lchip])
            {
                DRV_DBG_INFO(" %-29s%u\n", ecc_mem_desc[type], g_ecc_recover_master->ecc_mem[type].recover_cnt[lchip]);
            }
        }
        else
        {
            DRV_DBG_INFO(" %-29s%u\n", ecc_mem_desc[type], g_ecc_recover_master->ecc_mem[type].recover_cnt[lchip]);
        }
    }
    DRV_DBG_INFO("\n");

    return DRV_E_NONE;
}

/**
 @brief The function init chip's mapping memory for ecc error recover
*/
int32
drv_greatbelt_ecc_recover_init(uint8 lchip, drv_ecc_recover_global_cfg_t* p_global_cfg)
{
    uint8     mem_type = 0;
    uint32    size = 0, i = 0, j = 0, cmd = 0, offset = 0;
    uint32    data[DRV_PE_MAX_ENTRY_WORD] = {0};
    uintptr   start_addr = 0;
    tbls_id_t tblid = 0;
    int32     ret = DRV_E_NONE;
    void*     ptr = NULL;
    tbls_id_t datapath_tbl[] = {DsGrpShpWfqCtl_t, DsHeadHashMod_t, DsQueShpWfqCtl_t,
                                DsQueThrdProfile_t, DsSgmacHeadHashMod_t, MaxTblId_t};

    DRV_PTR_VALID_CHECK(p_global_cfg);
    DRV_PTR_VALID_CHECK(p_global_cfg->ftm_check_cb);


    if (!g_ecc_recover_master)
    {
        g_ecc_recover_master = (drv_ecc_recover_master_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(drv_ecc_recover_master_t));
        if (NULL == g_ecc_recover_master)
        {
            goto DRV_ECC_RECOVER_INIT_ERROR;
        }
        sal_memset(g_ecc_recover_master, 0, sizeof(drv_ecc_recover_master_t));

        ret = sal_mutex_create(&(g_ecc_recover_master->p_ecc_mutex));
        if (ret || !(g_ecc_recover_master->p_ecc_mutex))
        {
            goto DRV_ECC_RECOVER_INIT_ERROR;
        }

        g_ecc_recover_master->ftm_check_cb = p_global_cfg->ftm_check_cb;
    }

        for (mem_type = DRV_ECC_MEM_IgrPriToTcMap; mem_type < DRV_ECC_MEM_NUM; mem_type++)
        {
            _drv_greatbelt_ecc_recover_mem2tbl(mem_type, &tblid);

            if (MaxTblId_t != tblid)
            {
                if (DsPolicerProfile_t == tblid)
                {
                    size = TABLE_MAX_INDEX(tblid) * TABLE_ENTRY_SIZE(tblid) * 4;
                }
                else
                {
                    size = TABLE_MAX_INDEX(tblid) * TABLE_ENTRY_SIZE(tblid);
                }
                g_ecc_recover_master->mem_size += size;

                start_addr = (uintptr)mem_malloc(MEM_SYSTEM_MODULE, size);
                if (0 == start_addr)
                {
                    goto DRV_ECC_RECOVER_INIT_ERROR;
                }
                sal_memset((uintptr*)start_addr, 0, size);

                g_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip] = start_addr;
                g_ecc_recover_master->ecc_mem[mem_type].size[lchip] = size;
            }
        }

        for (mem_type = DRV_ECC_MEM_Edram0; mem_type <= DRV_ECC_MEM_Edram8; mem_type++)
        {
            offset += edram_entry_num[mem_type - DRV_ECC_MEM_Edram0];
            if (mem_type > DRV_ECC_MEM_Edram0)
            {
                edram_offset[mem_type - DRV_ECC_MEM_Edram0] = offset * DRV_ADDR_BYTES_PER_ENTRY;
            }
            size = edram_entry_num[mem_type - DRV_ECC_MEM_Edram0] * DRV_BYTES_PER_ENTRY;
            g_ecc_recover_master->mem_size += size;
            start_addr = (uintptr)mem_malloc(MEM_SYSTEM_MODULE, size);
            if (!start_addr)
            {
                goto DRV_ECC_RECOVER_INIT_ERROR;
            }
            sal_memset((uintptr*)start_addr, 0, size);
            g_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip] = start_addr;
            g_ecc_recover_master->ecc_mem[mem_type].size[lchip] = size;
        }

        size = (DRV_MAX_TCAM_BLOCK_NUM * DRV_ENTRYS_PER_TCAM_BLOCK * DRV_BYTES_PER_ENTRY) / TCAM_AD_BLOCK_NUM;
        for (mem_type = DRV_ECC_MEM_TcamAd0; mem_type <= DRV_ECC_MEM_TcamAd3; mem_type++)
        {
            g_ecc_recover_master->mem_size += size;
            start_addr = (uintptr)mem_malloc(MEM_SYSTEM_MODULE, size);
            g_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip] = start_addr;
            g_ecc_recover_master->ecc_mem[mem_type].size[lchip] = size;
        }

        for (i = 0; MaxTblId_t != datapath_tbl[i]; i++)
        {
            for (j = 0; j < TABLE_MAX_INDEX(datapath_tbl[i]); j++)
            {
                cmd = DRV_IOR(datapath_tbl[i], DRV_ENTRY_FLAG);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, j, cmd, data));
                drv_greatbelt_ecc_recover_store(lchip, datapath_tbl[i], j, data);
            }
        }

    return DRV_E_NONE;

DRV_ECC_RECOVER_INIT_ERROR:

        for (mem_type = 0; mem_type < DRV_ECC_MEM_NUM; mem_type++)
        {
            if (g_ecc_recover_master && (0 != g_ecc_recover_master->ecc_mem[mem_type].size[lchip]))
            {
                ptr = (void*)g_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip];
                mem_free(ptr);
                g_ecc_recover_master->ecc_mem[mem_type].start_addr[lchip] = 0;
                g_ecc_recover_master->ecc_mem[mem_type].size[lchip] = 0;
            }
        }

    if (NULL != g_ecc_recover_master->p_ecc_mutex)
    {
        sal_mutex_destroy(g_ecc_recover_master->p_ecc_mutex);
    }

    if (NULL != g_ecc_recover_master)
    {
        mem_free(g_ecc_recover_master);
    }

    return DRV_E_INIT_FAILED;
}

