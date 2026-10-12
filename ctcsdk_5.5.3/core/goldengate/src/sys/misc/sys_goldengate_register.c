/**
 @file sys_goldengate_register.c

 @date 2009-11-6

 @version v2.0


*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_error.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_pdu.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_pdu.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_stp.h"
#include "sys_goldengate_parser.h"
#include "sys_goldengate_datapath.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_dma.h"
#include "sys_goldengate_nexthop_api.h"

#include "ctc_warmboot.h"
#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"

#define SYS_REG_CLKSLOW_FREQ   125

sys_register_master_t* p_gg_register_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};


/****************************************************************************
 *
 * Function
 *
 *****************************************************************************/

extern int32
sys_goldengate_interrupt_set_group_en(uint8 lchip, uint8 enable);
extern int32
sys_goldengate_interrupt_get_group_en(uint8 lchip, uint8* enable);

/* mode 0: txThrd0=3, txThrd1=4; mode 1: txThrd0=5, txThrd1=11; if cut through enable, default use mode 1 */
int32 sys_goldengate_net_tx_threshold_cfg(uint8 lchip, uint8 thrd0, uint8 thrd1)
{
    uint32 cmd = 0;
    NetTxTxThrdCfg0_m net_tx_cfg;

    cmd = DRV_IOR(NetTxTxThrdCfg0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_tx_cfg));
    SetNetTxTxThrdCfg0(V, txThrd0_f, &net_tx_cfg, thrd0);
    SetNetTxTxThrdCfg0(V, txThrd1_f, &net_tx_cfg, thrd1);
    cmd = DRV_IOW(NetTxTxThrdCfg0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_tx_cfg));

    cmd = DRV_IOR(NetTxTxThrdCfg1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_tx_cfg));
    SetNetTxTxThrdCfg0(V, txThrd0_f, &net_tx_cfg, thrd0);
    SetNetTxTxThrdCfg0(V, txThrd1_f, &net_tx_cfg, thrd1);
    cmd = DRV_IOW(NetTxTxThrdCfg1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_tx_cfg));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_lkp_ctl_init(uint8 lchip)
{
	uint32 cmd      = 0;
    uint32 field    = 0;
    CTC_ERROR_RETURN(sys_goldengate_ftm_lkp_register_init(lchip));

    /*metal fix 101220*/
    field = 0;
    cmd = DRV_IOR(IpeLkupMgrReserved2_t, IpeLkupMgrReserved2_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field));
    CTC_BIT_SET(field, 4);
    CTC_BIT_SET(field, 5);
    CTC_BIT_SET(field, 6);
    cmd = DRV_IOW(IpeLkupMgrReserved2_t, IpeLkupMgrReserved2_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field));

    return CTC_E_NONE;
}

int32
sys_goldengate_lkup_ttl_index(uint8 lchip, uint8 ttl, uint32* ttl_index)
{

    uint32 cmd;
    uint8 index = 0;
    uint32 mpls_ttl = 0;
    uint8 bfind = 0;

    for (index = 0; index < 16; index++)
    {
        cmd = DRV_IOR(EpeL3EditMplsTtl_t, EpeL3EditMplsTtl_array_0_ttl_f +  index);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mpls_ttl));

        if (mpls_ttl >= ttl)
        {
            *ttl_index = index;
            bfind = 1;
            break;
        }
    }

    if (bfind)
    {
        return CTC_E_NONE;
    }

    return CTC_E_INVALID_TTL;

}

int32
_sys_goldengate_misc_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    ds_t ds;

    uint8 lgchip = 0;
    uint32 field_val = 0;
    uint16 i = 0;

    sys_goldengate_get_gchip_id(lchip, &lgchip);

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeHeaderAdjustCtl(V, packetHeaderBypassAll_f, ds, 0);
    cmd = DRV_IOW(IpeHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeUserIdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeUserIdCtl(V, parserLengthErrorMode_f, ds, 0);
    SetIpeUserIdCtl(V, layer2ProtocolTunnelLmDisable_f, ds, 1);

    /*
    randomLogShift is 5, so random log percent 0~100 % and step is 0.0031%
    */
    SetIpeUserIdCtl(V, randomLogShift_f, ds, 5);
    cmd = DRV_IOW(IpeUserIdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeIntfMapperCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeIntfMapperCtl(V, allowMcastMacSa_f,                ds, 0);
    SetIpeIntfMapperCtl(V, arpBroadcastRoutedPortDiscard_f,  ds, 1);
    SetIpeIntfMapperCtl(V, arpCheckExceptionEn_f,            ds, 0);
    SetIpeIntfMapperCtl(V, arpDestMacCheckEn_f,              ds, 0);
    SetIpeIntfMapperCtl(V, arpExceptionSubIndex_f,           ds, CTC_L3PDU_ACTION_INDEX_ARP);
    SetIpeIntfMapperCtl(V, arpIpCheckEn_f,                   ds, 0);
    SetIpeIntfMapperCtl(V, arpSrcMacCheckEn_f,               ds, 0);
    SetIpeIntfMapperCtl(V, arpUnicastDiscard_f,              ds, 0);
    SetIpeIntfMapperCtl(V, arpUnicastExceptionDisable_f,     ds, 1);
    SetIpeIntfMapperCtl(V, bypassAllDisableLearning_f,       ds, 0);
    SetIpeIntfMapperCtl(V, bypassPortCrossConnectDisable_f,  ds, 0);
    SetIpeIntfMapperCtl(V, dhcpBroadcastRoutedPortDiscard_f, ds, 0);
    SetIpeIntfMapperCtl(V, dhcpExceptionSubIndex_f,          ds, CTC_L3PDU_ACTION_INDEX_DHCP);
    SetIpeIntfMapperCtl(V, dhcpUnicastDiscard_f,             ds, 0);
    SetIpeIntfMapperCtl(V, dhcpUnicastExceptionDisable_f,    ds, 1);
    SetIpeIntfMapperCtl(V, discardSameIpAddr_f,              ds, 0);
    SetIpeIntfMapperCtl(V, discardSameMacAddr_f,             ds, 1);
    SetIpeIntfMapperCtl(V, ipHeaderErrorDiscard_f,           ds, 1);
    SetIpeIntfMapperCtl(V, fipExceptionSubIndex_f,           ds, CTC_L2PDU_ACTION_INDEX_FIP);

    cmd = DRV_IOW(IpeIntfMapperCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeMplsCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeMplsCtl(V, minInterfaceLabel_f, ds, 0);
     /*        SetIpeMplsCtl(V, mplsTtlLimit_f, ds, 1);*/
    SetIpeMplsCtl(V, oamAlertLabel0_f, ds, 14);
    SetIpeMplsCtl(V, oamAlertLabel1_f, ds, 13);
    SetIpeMplsCtl(V, mplsOffsetBytesShift_f, ds, 2);
    SetIpeMplsCtl(V, useFirstLabelExp_f, ds, 1);
    SetIpeMplsCtl(V, useFirstLabelTtl_f, ds, 0);
    SetIpeMplsCtl(V, galSbitCheckEn_f, ds, 1);
     /*        SetIpeMplsCtl(V, entropyLabelSbitCheckEn_f, ds, 1);*/
    SetIpeMplsCtl(V, galTtlCheckEn_f, ds, 1);
    SetIpeMplsCtl(V, entropyLabelTtlCheckEn_f, ds, 1);
    cmd = DRV_IOW(IpeMplsCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeTunnelIdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeTunnelIdCtl(V, routeObeyStp_f, ds, 1);
    SetIpeTunnelIdCtl(V, tunnelforceSecondParserEn_f, ds, 0x3ff);
    cmd = DRV_IOW(IpeTunnelIdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeLookupCtl(V, globalVrfIdLookupEn_f, ds, 1);
    SetIpeLookupCtl(V, pimSnoopingEn_f, ds, 1);
    SetIpeLookupCtl(V, stpBlockBridgeDisable_f, ds, 0);
    SetIpeLookupCtl(V, noIpMcastMacLookup_f, ds, 0);
    SetIpeLookupCtl(V, stpBlockLayer3_f, ds, 0);
    SetIpeLookupCtl(V, trillMbitCheckDisable_f, ds, 0);
    SetIpeLookupCtl(V, routedPortDisableBcastBridge_f, ds, 0);
    SetIpeLookupCtl(V, greFlexProtocol_f, ds, 0);
    SetIpeLookupCtl(V, greFlexPayloadPacketType_f, ds, 0);
    SetIpeLookupCtl(V, trillInnerVlanCheck_f, ds, 1);
    SetIpeLookupCtl(V, trillVersion_f, ds, 0);
    SetIpeLookupCtl(V, trillUseInnerVlan_f, ds, 1);
    SetIpeLookupCtl(V, metadataSendFid_f, ds, 1);
    SetIpeLookupCtl(V, upMepMacDaLkupMode_f, ds, 1);
    SetIpeLookupCtl(V, ecnAware_f, ds, 2); /*only TCP ECN Aware*/
    SetIpeLookupCtl(V, arpExceptionSubIndex_f, ds, CTC_L3PDU_ACTION_INDEX_ARP);
    cmd = DRV_IOW(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeAclLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeAclLookupCtl(V, routeObeyStp_f, ds, 1);
    SetIpeAclLookupCtl(V, ipv4McastForceUnicastEn_f, ds, 1);
    SetIpeAclLookupCtl(V, ipv6McastForceUnicastEn_f, ds, 1);
    SetIpeAclLookupCtl(V, ipv4McastForceBridgeEn_f, ds, 1);
    SetIpeAclLookupCtl(V, ipv6McastForceBridgeEn_f, ds, 1);
    SetIpeAclLookupCtl(V, flowHashConsiderOuter_f, ds, 1);
    SetIpeAclLookupCtl(V, trillMcastMac_f, ds, 1);
    SetIpeAclLookupCtl(V, globalVrfIdLkpEn_f, ds, 1);
    SetIpeAclLookupCtl(V, vxlanFlowKeyL4UseOuter_f, ds, 1);
    SetIpeAclLookupCtl(V, nvgreFlowKeyL4UseOuter_f, ds, 1);
    SetIpeAclLookupCtl(V, l2greFlowKeyL4UseOuter_f, ds, 1);

    cmd = DRV_IOW(IpeAclLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeAclQosCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
     /*      SetIpeAclQosCtl(V, timestampPacketDiscard_f, ds, 1);*/
    SetIpeAclQosCtl(V, oamObeyAclDiscard_f, ds, 1);
    SetIpeAclQosCtl(V, useAclFlowPriority_f, ds, 0);
    SetIpeAclQosCtl(V, elephantFlowColor_f, ds, 3);    /* default is CTC_QOS_COLOR_GREEN */
    cmd = DRV_IOW(IpeAclQosCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeRouteCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeRouteCtl(V, rpfTypeForMcast_f, ds, 0);
    SetIpeRouteCtl(V, exception2Discard_f, ds, 0);
    SetIpeRouteCtl(V, greOption2CheckEn_f, ds, 0);
    SetIpeRouteCtl(V, exception3DiscardDisable_f, ds, 0);
    SetIpeRouteCtl(V, mcastEscapeToCpu_f, ds, 0);
     /*        SetIpeRouteCtl(V, mcastRpfFailCpuEn_f, ds, 1); comment by sdk.*/
    SetIpeRouteCtl(V, mcastAddressMatchCheckDisable_f, ds, 0);
    SetIpeRouteCtl(V, ipOptionsEscapeDisable_f, ds, 0);
    SetIpeRouteCtl(V, routeOffsetByteShift_f, ds, 0);
    SetIpeRouteCtl(V, ipTtlLimit_f, ds, 1);
    SetIpeRouteCtl(V, igmpPacketDecideMode_f, ds, 1);

    cmd = DRV_IOW(IpeRouteCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeLookupRouteCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeLookupRouteCtl(V, martianCheckEn_f, ds, 0xF);
    SetIpeLookupRouteCtl(V, martianAddressCheckDisable_f, ds, 0);
    cmd = DRV_IOW(IpeLookupRouteCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeRouteMartianAddr_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeRouteMartianAddr(V, flex0Mask_f, ds, 0);
    SetIpeRouteMartianAddr(V, flex0Value_f, ds, 0);
    SetIpeRouteMartianAddr(V, flex1Mask_f, ds, 0);
    SetIpeRouteMartianAddr(V, flex1Value_f, ds, 0);
    cmd = DRV_IOW(IpeRouteMartianAddr_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeBridgeStormCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeBridgeStormCtl(V, ipgEn_f, ds, 0);
    SetIpeBridgeStormCtl(V, oamObeyStormCtl_f, ds, 0);
    cmd = DRV_IOW(IpeBridgeStormCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeClassificationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeClassificationCtl(V, portPolicerBase_f, ds, 0);
    cmd = DRV_IOW(IpeClassificationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeFwdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeFwdCtl(V, stackingEn_f, ds, 0);
    SetIpeFwdCtl(V, ecnPriorityCritical_f, ds, 0);
    SetIpeFwdCtl(V, chipId_f, ds, lgchip);
    SetIpeFwdCtl(V, discardFatal_f, ds, 0);
    SetIpeFwdCtl(V, exception2PriorityEn_f, ds, 0);
    SetIpeFwdCtl(V, forceExceptLocalPhyPort_f, ds, 1);
    SetIpeFwdCtl(V, headerHashMode_f, ds, 0);
    SetIpeFwdCtl(V, linkOamColor_f, ds, 0);
    SetIpeFwdCtl(V, linkOamPriority_f, ds, 0);
    SetIpeFwdCtl(V, logOnDiscard_f, ds, 0x7F);
    SetIpeFwdCtl(V, oamBypassPolicingDiscard_f, ds, 0);
    SetIpeFwdCtl(V, rxEtherOamCritical_f, ds, 0);
    SetIpeFwdCtl(V, portExtenderMcastEn_f, ds, 0);
    cmd = DRV_IOW(IpeFwdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetEpeHdrAdjustCtl(V, portExtenderMcastEn_f, ds, 0);
    cmd = DRV_IOW(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeTunnelDecapCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeTunnelDecapCtl(V, greFlexPayloadPacketType_f, ds, 0);
    SetIpeTunnelDecapCtl(V, greFlexProtocol_f, ds, 0x0800);
    SetIpeTunnelDecapCtl(V, greOption2CheckEn_f, ds, 1);
    SetIpeTunnelDecapCtl(V, ipTtlLimit_f, ds, 1);
    SetIpeTunnelDecapCtl(V, offsetByteShift_f, ds, 1);
    SetIpeTunnelDecapCtl(V, tunnelMartianAddressCheckEn_f, ds, 1);
    SetIpeTunnelDecapCtl(V, mcastAddressMatchCheckDisable_f, ds, 0);
    SetIpeTunnelDecapCtl(V, trillTtl_f, ds, 1);
    SetIpeTunnelDecapCtl(V, trillversionCheckEn_f, ds, 1);
    SetIpeTunnelDecapCtl(V, trillVersion_f, ds, 0);
    SetIpeTunnelDecapCtl(V, trillBfdCheckEn_f, ds, 1);
    SetIpeTunnelDecapCtl(V, trillBfdHopCount_f, ds, 0x30);
    cmd = DRV_IOW(IpeTunnelDecapCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeLearningCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
     /*        SetIpeLearningCtl(V, vlanSecurityExceptionEn_f, ds, 1);*/
    SetIpeLearningCtl(V, vsiSecurityExceptionEn_f, ds, 1);
    SetIpeLearningCtl(V, pendingEntrySecurityDisable_f, ds, 0);
    cmd = DRV_IOW(IpeLearningCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeTrillCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeTrillCtl(V, versionCheckEn_f, ds, 1);
    SetIpeTrillCtl(V, trillBypassDenyRoute_f, ds, 0);
    cmd = DRV_IOW(IpeTrillCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetMetFifoCtl(V, chipId_f, ds, lgchip);
    SetMetFifoCtl(V, portCheckEn_f, ds, 1);
    SetMetFifoCtl(V, discardMetLoop_f, ds, 1);
    SetMetFifoCtl(V, portBitmapNextHopPtr_f, ds, 1);
    cmd = DRV_IOW(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(EpeL2EtherType_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetEpeL2EtherType(V, array_0_etherType_f, ds, 0);
    SetEpeL2EtherType(V, array_1_etherType_f, ds, 0);
    cmd = DRV_IOW(EpeL2EtherType_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    /*Temple disable hyrid port*/
    SetEpeNextHopCtl(V, tagPortBitMapEn_f, ds, 1);
    SetEpeNextHopCtl(V, discardReflectiveBridge_f, ds, 1);
    SetEpeNextHopCtl(V, forceBridgeL3Match_f, ds, 1);
    SetEpeNextHopCtl(V, discardLogicTunnelMatch_f, ds, 1);
    SetEpeNextHopCtl(V, routeObeyIsolate_f, ds, 0);
    SetEpeNextHopCtl(V, mirrorObeyDiscard_f, ds, 0);
    SetEpeNextHopCtl(V, denyDuplicateMirror_f, ds, 1);
    SetEpeNextHopCtl(V, parserErrorIgnore_f, ds, 0);
    SetEpeNextHopCtl(V, discardSameIpAddr_f, ds, 0);
    SetEpeNextHopCtl(V, discardSameMacAddr_f, ds, 0);
    SetEpeNextHopCtl(V, cbpTciRes2CheckEn_f, ds, 0);
    SetEpeNextHopCtl(V, pbbBsiOamOnEgsCbpEn_f, ds, 0);
    SetEpeNextHopCtl(V, terminatePbbBvOamOnEgsPip_f, ds, 0);
    SetEpeNextHopCtl(V, pbbBvOamOnEgsPipEn_f, ds, 0);
    SetEpeNextHopCtl(V, stackingEn_f, ds, 0);
    SetEpeNextHopCtl(V, routeObeyStp_f, ds, 1);
    SetEpeNextHopCtl(V, parserLengthErrorMode_f, ds, 3);
    SetEpeNextHopCtl(V, oamIgnorePayloadOperation_f, ds, 1);
    SetEpeNextHopCtl(V, etherOamUsePayloadOperation_f, ds, 1);
    /*
    randomLogShift is 5, so random log percent 0~100 % and step is 0.0031%
    */
    SetEpeNextHopCtl(V, randomLogShift_f, ds, 5);
    cmd = DRV_IOW(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(EpeHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetEpeHeaderEditCtl(V, logOnDiscard_f, ds, 0x7F);
    SetEpeHeaderEditCtl(V, logPortDiscard_f, ds, 0);
    SetEpeHeaderEditCtl(V, rxEtherOamCritical_f, ds, 0);
    SetEpeHeaderEditCtl(V, stackingEn_f, ds, 0);
    SetEpeHeaderEditCtl(V, evbTpid_f, ds, 0);
    SetEpeHeaderEditCtl(V, muxLengthTypeEn_f, ds, 1);
    SetEpeHeaderEditCtl(V, loopbackUseSourcePort_f, ds, 0);
    SetEpeHeaderEditCtl(V, aclDiscardStatsEn_f, ds, 1);
    SetEpeHeaderEditCtl(V, srcVlanInfoEn_f, ds, 1);
    SetEpeHeaderEditCtl(V, chipId_f, ds, lgchip);
    SetEpeHeaderEditCtl(V, discardPacketBypassEdit_f, ds, 1);
    SetEpeHeaderEditCtl(V, discardPacketLengthRecover_f, ds, 1);

    cmd = DRV_IOW(EpeHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(EpePktProcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetEpePktProcCtl(V, discardRouteTtl0_f, ds, 1);
    SetEpePktProcCtl(V, discardMplsTtl0_f, ds, 1);
    SetEpePktProcCtl(V, discardMplsTagTtl0_f, ds, 1);
    SetEpePktProcCtl(V, discardTunnelTtl0_f, ds, 1);
    SetEpePktProcCtl(V, discardTrillTtl0_f, ds, 1);
    SetEpePktProcCtl(V, useGlobalCtagCos_f, ds, 0);
    SetEpePktProcCtl(V, mirrorEscapeCamEn_f, ds, 0);
    SetEpePktProcCtl(V, icmpErrMsgCheckEn_f, ds, 1);
    SetEpePktProcCtl(V, globalCtagCos_f, ds, 0);
    SetEpePktProcCtl(V, ptMulticastAddressEn_f, ds, 0);
    SetEpePktProcCtl(V, ptUdpChecksumZeroDiscard_f, ds, 1);
    SetEpePktProcCtl(V, nextHopStagCtlEn_f, ds, 1);
    SetEpePktProcCtl(V, mplsStatsMode_f, ds, 2);
    SetEpePktProcCtl(V, ipIdIncreaseType_f, ds, 1);
    SetEpePktProcCtl(V, ecnIgnoreCheck_f, ds, 0); /*ECN mark enable*/
    SetEpePktProcCtl(V, ecnAware_f, ds, 2); /*only TCP ECN Aware*/
    SetEpePktProcCtl(V, ucastTtlFailExceptionEn_f, ds, 1);
    SetEpePktProcCtl(V, mcastTtlFailExceptionEn_f, ds, 1);
    cmd = DRV_IOW(EpePktProcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(EpeL3IpIdentification_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, ds));/*slice1*/
    SetEpeL3IpIdentification(V, array_0_ipIdentification_f, ds, 1);
    SetEpeL3IpIdentification(V, array_1_ipIdentification_f, ds, 1);
    cmd = DRV_IOW(EpeL3IpIdentification_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(DsEgressPortMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetDsEgressPortMac(V, portMac_f, ds, 0);
    cmd = DRV_IOW(DsEgressPortMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(EpeClassificationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetEpeClassificationCtl(V, portPolicerBase_f, ds, 0);
    cmd = DRV_IOW(EpeClassificationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    field_val = 0x3F;
    cmd = DRV_IOW(EpeGlobalChannelMap_t, EpeGlobalChannelMap_maxNetworkPortChannel_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(EpeAclQosCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
     /*SetEpeAclQosCtl(V, serviceAclQoSEn_f, ds, 0x0);*/
    SetEpeAclQosCtl(V, oamObeyAclDiscard_f, ds, 0x1);
    cmd = DRV_IOW(EpeAclQosCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetFibEngineLookupResultCtl(V, gMacSaLookupResultCtl_defaultEntryEn_f, ds, 1);
    SetFibEngineLookupResultCtl(V, gMacSaLookupResultCtl_defaultEntryBase_f, ds, 0);
    SetFibEngineLookupResultCtl(V, gMacDaLookupResultCtl_defaultEntryEn_f, ds, 1);
    SetFibEngineLookupResultCtl(V, gMacDaLookupResultCtl_defaultEntryBase_f, ds, 0);
    SetFibEngineLookupResultCtl(V, gIpv4UcastLookupResultCtl_defaultEntryEn_f, ds, 1);
    SetFibEngineLookupResultCtl(V, gIpv4UcastLookupResultCtl_defaultEntryBase_f, ds, 12);
    SetFibEngineLookupResultCtl(V, gIpv4McastLookupResultCtl_defaultEntryEn_f, ds, 1);
    SetFibEngineLookupResultCtl(V, gIpv4McastLookupResultCtl_defaultEntryBase_f, ds, 13);
    SetFibEngineLookupResultCtl(V, gIpv6UcastLookupResultCtl_defaultEntryEn_f, ds, 1);
    SetFibEngineLookupResultCtl(V, gIpv6UcastLookupResultCtl_defaultEntryBase_f, ds, 14);
    SetFibEngineLookupResultCtl(V, gIpv6McastLookupResultCtl_defaultEntryEn_f, ds, 1);
    SetFibEngineLookupResultCtl(V, gIpv6McastLookupResultCtl_defaultEntryBase_f, ds, 15);
    SetFibEngineLookupResultCtl(V, gIpv4RpfLookupResultCtl_defaultEntryEn_f, ds, 1);
    SetFibEngineLookupResultCtl(V, gIpv4RpfLookupResultCtl_defaultEntryBase_f, ds, 12);
    SetFibEngineLookupResultCtl(V, gIpv6RpfLookupResultCtl_defaultEntryEn_f, ds, 1);
    SetFibEngineLookupResultCtl(V, gIpv6RpfLookupResultCtl_defaultEntryBase_f, ds, 14);
    SetFibEngineLookupResultCtl(V, gFcoeDaLookupResultCtl_defaultEntryEn_f, ds, 1);
    SetFibEngineLookupResultCtl(V, gFcoeDaLookupResultCtl_defaultEntryPtr_f, ds, 1536);
    SetFibEngineLookupResultCtl(V, gFcoeSaLookupResultCtl_defaultEntryEn_f, ds, 1);
    SetFibEngineLookupResultCtl(V, gFcoeSaLookupResultCtl_defaultEntryPtr_f, ds, 1600);
    SetFibEngineLookupResultCtl(V, gTrillUcastLookupResultCtl_defaultEntryEn_f, ds, 1);
    SetFibEngineLookupResultCtl(V, gTrillUcastLookupResultCtl_defaultEntryPtr_f, ds, 1792);
    SetFibEngineLookupResultCtl(V, gTrillMcastLookupResultCtl_defaultEntryEn_f, ds, 1);
    SetFibEngineLookupResultCtl(V, gTrillMcastLookupResultCtl_defaultEntryPtr_f, ds, 1856);
    cmd = DRV_IOW(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(BufferStoreCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetBufferStoreCtl(V, chipIdCheckDisable_f, ds, 0);
    SetBufferStoreCtl(V, chipId_f, ds, lgchip);
     /*        SetBufferStoreCtl(V, resrcIdUseLocalPhyPort_f, ds, 0);*/
    SetBufferStoreCtl(V, localSwitchingDisable_f, ds, 0);
    SetBufferStoreCtl(V, mcastMetFifoEnable_f, ds, 1);
    SetBufferStoreCtl(V, cpuRxExceptionEn0_f, ds, 0);
    SetBufferStoreCtl(V, cpuRxExceptionEn1_f, ds, 0);
    SetBufferStoreCtl(V, cpuTxExceptionEn_f, ds, 0);
    cmd = DRV_IOW(BufferStoreCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(BufferStoreForceLocalCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetBufferStoreForceLocalCtl(V, destMapMask0_f, ds, 0x3FFFF);
    SetBufferStoreForceLocalCtl(V, destMapValue0_f, ds, 0x3FFFF);
    SetBufferStoreForceLocalCtl(V, destMapMask1_f, ds, 0x3FFFF);
    SetBufferStoreForceLocalCtl(V, destMapValue1_f, ds, 0x3FFFF);
    SetBufferStoreForceLocalCtl(V, destMapMask2_f, ds, 0x3FFFF);
    SetBufferStoreForceLocalCtl(V, destMapValue2_f, ds, 0x3FFFF);
    SetBufferStoreForceLocalCtl(V, destMapMask3_f, ds, 0x3FFFF);
    SetBufferStoreForceLocalCtl(V, destMapValue3_f, ds, 0x3FFFF);
    cmd = DRV_IOW(BufferStoreForceLocalCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetBufferRetrieveCtl(V, colorMapEn_f, ds, 0);
    SetBufferRetrieveCtl(V, chipId_f, ds, lgchip);
    cmd = DRV_IOW(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetMetFifoCtl(V, chipId_f, ds, lgchip);
    SetMetFifoCtl(V, portCheckEn_f, ds, 1);
    SetMetFifoCtl(V, discardMetLoop_f, ds, 1);
    cmd = DRV_IOW(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(QWriteCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetQWriteCtl(V, chipId_f, ds, lgchip);
    cmd = DRV_IOW(QWriteCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0xFF, sizeof(ds));
    cmd = DRV_IOW(MetFifoPortStatusCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    field_val = 0;
    cmd = DRV_IOW(MetFifoPortStatusCtl_t, MetFifoPortStatusCtl_bitmapPortStatusCheckEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sal_memset(ds, 0xFF, sizeof(ds));
    SetQMgrEnqLfsrCtl(V, wredRandSeed0_f, &ds, 100);
    SetQMgrEnqLfsrCtl(V, wredRandSeed1_f, &ds, 100);
    cmd = DRV_IOW(QMgrEnqLfsrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    /*bug32120*/
    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOW(OamUpdateCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOW(DsBfdV6Addr_t, DRV_ENTRY_FLAG);
    field_val = TABLE_MAX_INDEX(DsBfdV6Addr_t);
    for (i = 0; i < TABLE_MAX_INDEX(DsBfdV6Addr_t); i++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, i, cmd, ds));
    }

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOW(DsPortProperty_t, DRV_ENTRY_FLAG);
    field_val = TABLE_MAX_INDEX(DsPortProperty_t);
    for (i = 0; i < TABLE_MAX_INDEX(DsPortProperty_t); i++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, i, cmd, ds));
    }

    field_val = 1;
    cmd = DRV_IOW(OamHeaderAdjustCtl_t, OamHeaderAdjustCtl_relayAllToCpu_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_ctl_init(uint8 lchip)
{

    uint32 cmd = 0;
    ds_t ds;

    cmd = DRV_IOW(EpeL3EditMplsTtl_t, DRV_ENTRY_FLAG);
    sal_memset(ds, 0, sizeof(ds));

    SetEpeL3EditMplsTtl(V, array_0_ttl_f, ds, 0);
    SetEpeL3EditMplsTtl(V, array_1_ttl_f, ds, 1);
    SetEpeL3EditMplsTtl(V, array_2_ttl_f, ds, 2);
    SetEpeL3EditMplsTtl(V, array_3_ttl_f, ds, 8);
    SetEpeL3EditMplsTtl(V, array_4_ttl_f, ds, 15);
    SetEpeL3EditMplsTtl(V, array_5_ttl_f, ds, 16);
    SetEpeL3EditMplsTtl(V, array_6_ttl_f, ds, 31);
    SetEpeL3EditMplsTtl(V, array_7_ttl_f, ds, 32);
    SetEpeL3EditMplsTtl(V, array_8_ttl_f, ds, 60);
    SetEpeL3EditMplsTtl(V, array_9_ttl_f, ds, 63);
    SetEpeL3EditMplsTtl(V, array_10_ttl_f, ds, 64);
    SetEpeL3EditMplsTtl(V, array_11_ttl_f, ds, 65);
    SetEpeL3EditMplsTtl(V, array_12_ttl_f, ds, 127);
    SetEpeL3EditMplsTtl(V, array_13_ttl_f, ds, 128);
    SetEpeL3EditMplsTtl(V, array_14_ttl_f, ds, 254);
    SetEpeL3EditMplsTtl(V, array_15_ttl_f, ds, 255);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_nexthop_ctl_init(uint8 lchip)
{
#ifdef NEVER
    /* POP DS */
    uint8 lchip_num = 0;

    uint32 cmd = 0;
    uint32 field_val = 0;

    lchip_num = sys_goldengate_get_local_chip_num(lchip);

    field_val = 1;
    cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_MacDaBit40Mode_f);

    if(TRUE)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    return CTC_E_NONE;

#endif /* never */
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_excp_ctl_init(uint8 lchip)
{

	/* POP DS */
    uint32 cmd = 0;
    uint32 excp_idx = 0;
    uint8 gchip  = 0;
    ds_t ds;
	sys_goldengate_get_gchip_id(lchip, &gchip);
    for (excp_idx = 0; excp_idx < 256; excp_idx++)
    {
        sal_memset(ds, 0, sizeof(ds));
        SetDsMetFifoExcp(V, destMap_f, ds, SYS_ENCODE_DESTMAP(gchip, SYS_RSV_PORT_DROP_ID));
        cmd = DRV_IOW(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, ds));;
    }

    return CTC_E_NONE;

}

int32
sys_goldengate_tcam_edram_init(uint8 lchip)
{
#ifdef NEVER
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint8 lchip_num = 0;


    lchip_num = sys_goldengate_get_local_chip_num(lchip);

    if(TRUE)
    {

        /*init edram*/
        field_val = 1;
        cmd = DRV_IOW(DynamicDsEdramInitCtl_t, DynamicDsEdramInitCtl_EDramInitEnable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*init sram*/
        field_val = 1;
        cmd = DRV_IOW(DynamicDsSramInit_t, DynamicDsSramInit_Init_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*init big tcam*/
        field_val = 0;
        cmd = DRV_IOW(TcamCtlIntInitCtrl_t, TcamCtlIntInitCtrl_CfgInitStartAddr_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        field_val = 0x1FFF;  /*Tcam support 8K*/
        cmd = DRV_IOW(TcamCtlIntInitCtrl_t, TcamCtlIntInitCtrl_CfgInitEndAddr_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        field_val = 1;
        cmd = DRV_IOW(TcamCtlIntInitCtrl_t, TcamCtlIntInitCtrl_CfgInitEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*init big tcamAd*/
        field_val = 1;
        cmd = DRV_IOW(TcamDsRamInitCtl_t, TcamDsRamInitCtl_DsRamInit_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*init lpm tcam*/
        field_val = 1;
        cmd = DRV_IOW(FibTcamInitCtl_t, FibTcamInitCtl_TcamInitEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*init lpm tcamAd*/
        field_val = 1;
        cmd = DRV_IOW(FibInitCtl_t, FibInitCtl_Init_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    return CTC_E_NONE;

#endif /* never */
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_net_rx_tx_init(uint8 lchip)
{
#ifdef NEVER
    uint8 lchip_num = 0;

    uint32 cmd = 0;
    uint16 chan_id = 0;
    net_rx_channel_map_t net_rx_channel_map;
    net_tx_port_id_map_t net_tx_port_map;
    net_tx_chan_id_map_t net_tx_channel_map;
    buf_retrv_chan_id_cfg_t buf_retrv_chan_id_cfg;

    sal_memset(&net_rx_channel_map, 0, sizeof(net_rx_channel_map_t));
    sal_memset(&net_tx_port_map, 0, sizeof(net_tx_port_id_map_t));
    sal_memset(&net_tx_channel_map, 0, sizeof(net_tx_chan_id_map_t));

    sal_memset(&buf_retrv_chan_id_cfg, 0, sizeof(buf_retrv_chan_id_cfg_t));
    buf_retrv_chan_id_cfg.cfg_int_lk_chan_en_int     = 1;
    buf_retrv_chan_id_cfg.cfg_int_lk_chan_id_int     = 56;
    buf_retrv_chan_id_cfg.cfg_i_loop_chan_en_int     = 1;
    buf_retrv_chan_id_cfg.cfg_i_loop_chan_id_int     = 57;
    buf_retrv_chan_id_cfg.cfg_cpu_chan_en_int        = 1;
    buf_retrv_chan_id_cfg.cfg_cpu_chan_id_int        = 58;
    buf_retrv_chan_id_cfg.cfg_dma_chan_en_int        = 1;
    buf_retrv_chan_id_cfg.cfg_dma_chan_id_int        = 59;
    buf_retrv_chan_id_cfg.cfg_oam_chan_en_int        = 1;
    buf_retrv_chan_id_cfg.cfg_oam_chan_id_int        = 60;
    buf_retrv_chan_id_cfg.cfg_e_loop_chan_en_int     = 1;
    buf_retrv_chan_id_cfg.cfg_e_loop_chan_id_int     = 61;
    buf_retrv_chan_id_cfg.cfg_net1_g_chan_en63_to32  = 0xFFFF;
    buf_retrv_chan_id_cfg.cfg_net1_g_chan_en31_to0   = 0xFFFFFFFF;
    buf_retrv_chan_id_cfg.cfg_net_s_g_chan_en63_to32 = 0x00FF0000;
    buf_retrv_chan_id_cfg.cfg_net_s_g_chan_en31_to0  = 0x0;

    lchip_num = sys_goldengate_get_local_chip_num(lchip);

    if(TRUE)
    {
        for (chan_id = 0; chan_id < 60; chan_id++)
        {
            if (chan_id <= 59 && chan_id >= 56)
            {
                net_rx_channel_map.chan_id = chan_id - 2;
            }
            else
            {
                net_rx_channel_map.chan_id = chan_id;
            }

            cmd = DRV_IOW(NetRxChannelMap_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &net_rx_channel_map));

            net_tx_port_map.chan_id = chan_id;
            cmd = DRV_IOW(NetTxPortIdMap_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &net_tx_port_map));

            net_tx_channel_map.port_id = chan_id;
            cmd = DRV_IOW(NetTxChanIdMap_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &net_tx_channel_map));
        }

        cmd = DRV_IOW(BufRetrvChanIdCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_retrv_chan_id_cfg));

    }

    return CTC_E_NONE;

#endif /* never */
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mux_ctl_init(uint8 lchip)
{
#ifdef NEVER
    uint8 lchip_num = 0;

    uint32 cmd = 0;
    IpePhyPortMuxCtl_m ipe_phy_port_mux_ctl;

    sal_memset(&ipe_phy_port_mux_ctl, 0, sizeof(IpePhyPortMuxCtl_m));

    lchip_num = sys_goldengate_get_local_chip_num(lchip);

    if(TRUE)
    {
        cmd = DRV_IOW(IpePhyPortMuxCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_phy_port_mux_ctl));
    }

    return CTC_E_NONE;

#endif /* never */
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipe_loopback_hdr_adjust_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    ds_t ds_data;

    sal_memset(&ds_data, 0, sizeof(ds_t));

    SetIpeLoopbackHeaderAdjustCtl(V, loopbackFromFabricEn_f, ds_data, 0);
    SetIpeLoopbackHeaderAdjustCtl(V, alwaysUseHeaderLogicPort_f, ds_data, 1);
    cmd = DRV_IOW(IpeLoopbackHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_data));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipe_hdr_adjust_ctl_init(uint8 lchip)
{

    uint32 cmd = 0;

    IpePhyPortMuxCtl_m ipe_phy_port_mux_ctl;
    IpeHeaderAdjustCtl_m ipe_header_adjust_ctl;
    IpeMuxHeaderAdjustCtl_m ipe_mux_header_adjust_ctl;

    sal_memset(&ipe_mux_header_adjust_ctl, 0, sizeof(IpeMuxHeaderAdjustCtl_m));
    cmd = DRV_IOW(IpeMuxHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_mux_header_adjust_ctl));

    sal_memset(&ipe_phy_port_mux_ctl, 0, sizeof(IpePhyPortMuxCtl_m));
    cmd = DRV_IOW(IpePhyPortMuxCtl_t , DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_phy_port_mux_ctl));

    sal_memset(&ipe_header_adjust_ctl, 0, sizeof(IpeHeaderAdjustCtl_m));
    cmd = DRV_IOW(IpeHeaderAdjustCtl_t , DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_header_adjust_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipe_ds_vlan_ctl(uint8 lchip)
{
#ifdef NEVER
    uint8 lchip_num = 0;

    uint32 cmd = 0;
    IpeDsVlanCtl_m ipe_ds_vlan_ctl;

    sal_memset(&ipe_ds_vlan_ctl, 0, sizeof(IpeDsVlanCtl_m));

    lchip_num = sys_goldengate_get_local_chip_num(lchip);

    if(TRUE)
    {
        cmd = DRV_IOR(IpeDsVlanCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ds_vlan_ctl));

        ipe_ds_vlan_ctl.DsStpState_mhift = 0x3;

        cmd = DRV_IOW(IpeDsVlanCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ds_vlan_ctl));
    }

    return CTC_E_NONE;

#endif /* never */
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipe_usrid_ctl_init(uint8 lchip)
{
#ifdef NEVER
    uint8 lchip_num = 0;

    uint32 cmd = 0;
    IpeUserIdCtl_m ipe_user_id_ctl;

    sal_memset(&ipe_user_id_ctl, 0, sizeof(IpeUserIdCtl_m));
    ipe_user_id_ctl.user_id_mac_key_svlan_first = 1;
    ipe_user_id_ctl.arp_force_ipv4              = 1;
    ipe_user_id_ctl.parser_length_error_mode    = 0;
    ipe_user_id_ctl.layer2_protocol_tunnel_lm_disable = 1;

    lchip_num = sys_goldengate_get_local_chip_num(lchip);

    if(TRUE)
    {
        cmd = DRV_IOW(IpeUserIdCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_user_id_ctl));
    }

    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_ipe_route_mac_ctl_init(uint8 lchip)
{
#ifdef NEVER
    uint8 lchip_num = 0;

    uint32 cmd = 0;

    IpeRouterMacCtl_m ipe_router_mac_ctl;

    sal_memset(&ipe_router_mac_ctl, 0, sizeof(IpeRouterMacCtl_m));
    ipe_router_mac_ctl.router_mac_type0_47_40 = 0;
    ipe_router_mac_ctl.router_mac_type0_39_8  = 0x000AFFFF;
    ipe_router_mac_ctl.router_mac_type1_47_40 = 0;
    ipe_router_mac_ctl.router_mac_type1_39_8  = 0x005E0001;
    ipe_router_mac_ctl.router_mac_type2_47_40 = 0;
    ipe_router_mac_ctl.router_mac_type2_39_8  = 0x005E0002;

    lchip_num = sys_goldengate_get_local_chip_num(lchip);

    if(TRUE)
    {
        cmd = DRV_IOW(IpeRouterMacCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_router_mac_ctl));
    }

    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipe_mpls_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 value = 1;
    cmd = DRV_IOW(IpeMplsTtlThrd_t, IpeMplsTtlThrd_array_0_ttlThreshold_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipe_mpls_exp_map_init(uint8 lchip)
{
#ifdef NEVER
    uint8 lchip_num = 0;

    uint32 cmd = 0;
    IpeMplsExpMap_m ipe_mpls_exp_map;

    sal_memset(&ipe_mpls_exp_map, 0, sizeof(IpeMplsExpMap_m));
    ipe_mpls_exp_map.priority0 = 0;
    ipe_mpls_exp_map.color0    = 3;
    ipe_mpls_exp_map.priority1 = 8;
    ipe_mpls_exp_map.color1    = 3;
    ipe_mpls_exp_map.priority2 = 16;
    ipe_mpls_exp_map.color2    = 3;
    ipe_mpls_exp_map.priority3 = 24;
    ipe_mpls_exp_map.color3    = 3;
    ipe_mpls_exp_map.priority4 = 32;
    ipe_mpls_exp_map.color4    = 3;
    ipe_mpls_exp_map.priority5 = 40;
    ipe_mpls_exp_map.color5    = 3;
    ipe_mpls_exp_map.priority6 = 48;
    ipe_mpls_exp_map.color6    = 3;
    ipe_mpls_exp_map.priority7 = 56;
    ipe_mpls_exp_map.color7    = 3;

    lchip_num = sys_goldengate_get_local_chip_num(lchip);

    if(TRUE)
    {
        cmd = DRV_IOW(IpeMplsExpMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_mpls_exp_map));
    }

    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipe_tunnel_id_ctl_init(uint8 lchip)
{
#ifdef NEVER
    uint8 lchip_num = 0;

    uint32 cmd = 0;
    IpeTunnelIdCtl_m ipe_tunnel_id_ctl;

    sal_memset(&ipe_tunnel_id_ctl, 0, sizeof(IpeTunnelIdCtl_m));
    ipe_tunnel_id_ctl.wlan_dtls_exception_en = 0;
    ipe_tunnel_id_ctl.route_obey_stp           = 1;

    lchip_num = sys_goldengate_get_local_chip_num(lchip);

    if(TRUE)
    {
        cmd = DRV_IOW(IpeTunnelIdCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_tunnel_id_ctl));
    }

    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipe_mcast_force_route_init(uint8 lchip)
{
#ifdef NEVER
    uint8 lchip_num = 0;

    uint32 cmd = 0;
    IpeIpv4McastForceRoute_m ipe_ipv4_mcast_force_route;
    IpeIpv6McastForceRoute_m ipe_ipv6_mcast_force_route;

    sal_memset(&ipe_ipv4_mcast_force_route, 0, sizeof(IpeIpv4McastForceRoute_m));
    ipe_ipv4_mcast_force_route.addr0_value  = 0;
    ipe_ipv4_mcast_force_route.addr0_mask   = 0xFFFFFFFF;
    ipe_ipv4_mcast_force_route.addr1_value  = 0;
    ipe_ipv4_mcast_force_route.addr1_mask   = 0xFFFFFFFF;

    sal_memset(&ipe_ipv6_mcast_force_route, 0, sizeof(IpeIpv6McastForceRoute_m));
    ipe_ipv6_mcast_force_route.addr0_value0 = 0;
    ipe_ipv6_mcast_force_route.addr0_value1 = 0;
    ipe_ipv6_mcast_force_route.addr0_value2 = 0;
    ipe_ipv6_mcast_force_route.addr0_value3 = 0;
    ipe_ipv6_mcast_force_route.addr0_mask0  = 0xFFFFFFFF;
    ipe_ipv6_mcast_force_route.addr0_mask1  = 0xFFFFFFFF;
    ipe_ipv6_mcast_force_route.addr0_mask2  = 0xFFFFFFFF;
    ipe_ipv6_mcast_force_route.addr0_mask3  = 0xFFFFFFFF;
    ipe_ipv6_mcast_force_route.addr1_value0 = 0;
    ipe_ipv6_mcast_force_route.addr1_value1 = 0;
    ipe_ipv6_mcast_force_route.addr1_value2 = 0;
    ipe_ipv6_mcast_force_route.addr1_value3 = 0;
    ipe_ipv6_mcast_force_route.addr1_mask0  = 0xFFFFFFFF;
    ipe_ipv6_mcast_force_route.addr1_mask1  = 0xFFFFFFFF;
    ipe_ipv6_mcast_force_route.addr1_mask2  = 0xFFFFFFFF;
    ipe_ipv6_mcast_force_route.addr1_mask3  = 0xFFFFFFFF;

    lchip_num = sys_goldengate_get_local_chip_num(lchip);

    if(TRUE)
    {
        cmd = DRV_IOW(IpeIpv4McastForceRoute_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ipv4_mcast_force_route));

        cmd = DRV_IOW(IpeIpv6McastForceRoute_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ipv6_mcast_force_route));
    }

    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipe_route_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    ds_t ipe_lookup_route_ctl;
    ds_t ipe_route_martian_addr;
    ds_t ipe_route_ctl;
    ParserIpChecksumCtl_m checksum_ctl;

    sal_memset(&ipe_route_ctl, 0, sizeof(ipe_route_ctl));
    SetIpeRouteCtl(V, rpfTypeForMcast_f, ipe_route_ctl, 0);
    SetIpeRouteCtl(V, exception2Discard_f, ipe_route_ctl, 0);
    SetIpeRouteCtl(V, greOption2CheckEn_f, ipe_route_ctl, 0);
    SetIpeRouteCtl(V, exception3DiscardDisable_f, ipe_route_ctl, 1);
    SetIpeRouteCtl(V, exceptionSubIndex_f, ipe_route_ctl, 1);
    SetIpeRouteCtl(V, mcastEscapeToCpu_f, ipe_route_ctl, 0);
    SetIpeRouteCtl(V, mcastAddressMatchCheckDisable_f, ipe_route_ctl, 0);
    SetIpeRouteCtl(V, ipOptionsEscapeDisable_f, ipe_route_ctl, 0);
    SetIpeRouteCtl(V, routeOffsetByteShift_f, ipe_route_ctl, 0);
    SetIpeRouteCtl(V, ipTtlLimit_f, ipe_route_ctl, 2);
    SetIpeRouteCtl(V, icmpCheckRpfEn_f, ipe_route_ctl, 1);
    SetIpeRouteCtl(V, iviUseDaInfo_f, ipe_route_ctl, 1);
    SetIpeRouteCtl(V, ptIcmpEscape_f, ipe_route_ctl, 1);
    SetIpeRouteCtl(V, igmpPacketDecideMode_f, ipe_route_ctl, 1);

    sal_memset(&ipe_lookup_route_ctl, 0, sizeof(IpeLookupRouteCtl_m));
    SetIpeLookupRouteCtl(V, martianCheckEn_f, ipe_lookup_route_ctl, 0xF);
    SetIpeLookupRouteCtl(V, martianAddressCheckDisable_f, ipe_lookup_route_ctl, 0);

    sal_memset(&ipe_route_martian_addr, 0, sizeof(IpeRouteMartianAddr_m));

    sal_memset(&checksum_ctl, 0, sizeof(checksum_ctl));
    SetParserIpChecksumCtl(V, ipChecksumCheckWithF_f, &checksum_ctl, 1);
    SetParserIpChecksumCtl(V, ipChecksumCheckWithZero_f, &checksum_ctl, 1);


    cmd = DRV_IOW(IpeRouteCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_route_ctl));

    cmd = DRV_IOW(IpeLookupRouteCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_lookup_route_ctl));

    cmd = DRV_IOW(IpeRouteMartianAddr_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_route_martian_addr));

    cmd = DRV_IOW(ParserIpChecksumCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &checksum_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipe_brg_ctl_init(uint8 lchip)
{

    int32 step = 0;
    uint8 idx = 0;
    uint32 cmd = 0;
    IpeBridgeCtl_m ipe_bridge_ctl;

    sal_memset(&ipe_bridge_ctl, 0, sizeof(IpeBridgeCtl_m));

    step = IpeBridgeCtl_array_1_protocolExceptionSubIndex_f
           - IpeBridgeCtl_array_0_protocolExceptionSubIndex_f;

    for (idx = 0; idx < 16; idx++)
    {
        DRV_SET_FIELD_V(IpeBridgeCtl_t, IpeBridgeCtl_array_0_protocolExceptionSubIndex_f + idx * step,
                        &ipe_bridge_ctl, 32);
    }

    SetIpeBridgeCtl(V, bridgeOffsetByteShift_f, &ipe_bridge_ctl, 0);
    SetIpeBridgeCtl(V, discardForceBridge_f, &ipe_bridge_ctl, 0);
    SetIpeBridgeCtl(V, macDaIsPortMacException_f, &ipe_bridge_ctl, 0);
    SetIpeBridgeCtl(V, multicastStormControlMode_f, &ipe_bridge_ctl, 1);
    SetIpeBridgeCtl(V, unicastStormControlMode_f, &ipe_bridge_ctl, 1);
    SetIpeBridgeCtl(V, pbbMode_f, &ipe_bridge_ctl, 0);
    SetIpeBridgeCtl(V, protocolException_f, &ipe_bridge_ctl, 0x1440);
    SetIpeBridgeCtl(V, discardForceBridge_f, &ipe_bridge_ctl, 0);

    cmd = DRV_IOW(IpeBridgeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_bridge_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipe_cos_map_init(uint8 lchip)
{

    uint32 cmd = 0;
    ds_t ds;
    uint16 index = 0;
    uint8 priority0 = 0;
    uint8 priority1 = 0;
    uint8 priority2 = 0;
    uint8 priority3 = 0;
    uint32  field_val = 0;

    for (index = 0; index < 32; index++)
    {
        priority0 = (index % 4) * 8;
        priority1 = priority0;
        priority2 = (index % 4 + 1) * 8;
        priority3 = priority2;
        sal_memset(ds, 0, sizeof(ds));
        cmd = DRV_IOR(IpeClassificationCosMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
        SetIpeClassificationCosMap(V, array_0__priority_f, ds, priority0);
        SetIpeClassificationCosMap(V, array_0_color_f, ds, 3);
        SetIpeClassificationCosMap(V, array_1__priority_f, ds, priority1);
        SetIpeClassificationCosMap(V, array_1_color_f, ds, 3);
        SetIpeClassificationCosMap(V, array_2__priority_f, ds, priority2);
        SetIpeClassificationCosMap(V, array_2_color_f, ds, 3);
        SetIpeClassificationCosMap(V, array_3__priority_f, ds, priority3);
        SetIpeClassificationCosMap(V, array_3_color_f, ds, 3);
        cmd = DRV_IOW(IpeClassificationCosMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));


    }

    for (index = 0; index < 16; index++)
    {
        priority0 = (index % 2) * 8;
        priority1 = (index % 2 + 1) * 8;
        priority2 = (index % 2 + 2) * 8;
        priority3 = (index % 2 + 3) * 8;
        cmd = DRV_IOR(IpeClassificationPrecedenceMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));
        SetIpeClassificationPrecedenceMap(V, array_0__priority_f, ds, priority0);
        SetIpeClassificationPrecedenceMap(V, array_0_color_f, ds, 3);
        SetIpeClassificationPrecedenceMap(V, array_1__priority_f, ds, priority1);
        SetIpeClassificationPrecedenceMap(V, array_1_color_f, ds, 3);
        SetIpeClassificationPrecedenceMap(V, array_2__priority_f, ds, priority2);
        SetIpeClassificationPrecedenceMap(V, array_2_color_f, ds, 3);
        SetIpeClassificationPrecedenceMap(V, array_3__priority_f, ds, priority3);
        SetIpeClassificationPrecedenceMap(V, array_3_color_f, ds, 3);
        cmd = DRV_IOW(IpeClassificationPrecedenceMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));
    }

    for (index = 0; index < 512; index++)
    {
        priority0 = index % 64;
        priority1 = priority0;
        priority2 = priority0;
        priority3 = priority0;
        sal_memset(ds, 0, sizeof(ds));
        cmd = DRV_IOR(IpeClassificationDscpMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));
        SetIpeClassificationDscpMap(V, array_0__priority_f, ds, priority0);
        SetIpeClassificationDscpMap(V, array_0_color_f, ds, 3);
        SetIpeClassificationDscpMap(V, array_1__priority_f, ds, priority1);
        SetIpeClassificationDscpMap(V, array_1_color_f, ds, 3);
        SetIpeClassificationDscpMap(V, array_2__priority_f, ds, priority2);
        SetIpeClassificationDscpMap(V, array_2_color_f, ds, 3);
        SetIpeClassificationDscpMap(V, array_3__priority_f, ds, priority3);
        SetIpeClassificationDscpMap(V, array_3_color_f, ds, 3);
        cmd = DRV_IOW(IpeClassificationDscpMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));

    }

    /* enable policer on iloop port*/
    cmd = DRV_IOR(IpePktProcReserved2_t, IpePktProcReserved2_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    CTC_BIT_SET(field_val, 3);
    cmd = DRV_IOW(IpePktProcReserved2_t, IpePktProcReserved2_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipe_fwd_ctl_init(uint8 lchip)
{
    uint8 lgchip = 0;
    uint32 cmd = 0;
    uint8 cut_through_en = 0;
    uint32 critical_packet_en[8] = {0};
    uint16 exception_index = 0;
    IpeFwdCtl_m ipe_fwd_ctl;
    IpeFwdCtl1_m ipe_fwd_ctl1;

    cut_through_en = sys_goldengate_get_cut_through_en(lchip);

    sal_memset(&ipe_fwd_ctl, 0, sizeof(IpeFwdCtl_m));
    sal_memset(&ipe_fwd_ctl1, 0, sizeof(IpeFwdCtl1_m));
    sal_memset(&critical_packet_en, 0xFF, sizeof(critical_packet_en));

    if(cut_through_en)
    {
        sys_goldengate_net_tx_threshold_cfg(lchip, 5, 11);
    }

    cmd = DRV_IOR(IpeFwdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_fwd_ctl));

    SetIpeFwdCtl(V, acl0DiscardStatsEn_f, &ipe_fwd_ctl, 1);
    SetIpeFwdCtl(V, acl1DiscardStatsEn_f, &ipe_fwd_ctl, 1);
    SetIpeFwdCtl(V, acl2DiscardStatsEn_f, &ipe_fwd_ctl, 1);
    SetIpeFwdCtl(V, acl3DiscardStatsEn_f, &ipe_fwd_ctl, 1);
    SetIpeFwdCtl(V, ifDiscardStatsEn_f, &ipe_fwd_ctl, 1);
    SetIpeFwdCtl(V, fwdDiscardStatsEn_f, &ipe_fwd_ctl, 1);
    SetIpeFwdCtl(V, acl0SdcStatsBase_f, &ipe_fwd_ctl, 0x3FFF);
    SetIpeFwdCtl(V, fwdStatsForOpenFlow_f, &ipe_fwd_ctl, 1);

    SetIpeFwdCtl(V, cutThroughEn_f, &ipe_fwd_ctl, cut_through_en);

    sys_goldengate_get_gchip_id(lchip, &lgchip);
    SetIpeFwdCtl(V, chipId_f, &ipe_fwd_ctl, lgchip);

    cmd = DRV_IOW(IpeFwdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_fwd_ctl));

    for (exception_index = 0; exception_index < 256; exception_index++)
    {
        if ((exception_index < 24) || (exception_index == 213) || (exception_index == 215) || (exception_index == 31) || (exception_index == 224)/*isIngressLog*/
            || ((exception_index > 31) && (exception_index < 44)) || (exception_index == 48) || (exception_index == 46) || (exception_index == 52) || (exception_index == 27)/*isEgressLog*/
            || (exception_index == 26 ) || ( exception_index == 45 )/*isCpuLog*/
            || ( exception_index == (192 + 19))) /*IPEEXPIDX7SUBIDX_FIND_NEW_ELEPHANT_FLOW*/
            {
               CTC_BIT_UNSET(critical_packet_en[exception_index/32], exception_index%32); /*not critical for ingress resource*/
            }
    }

    SetIpeFwdCtl1(A, criticalPacketEn_f, &ipe_fwd_ctl1, critical_packet_en);
    cmd = DRV_IOW(IpeFwdCtl1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_fwd_ctl1));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipe_tunnell_ctl_init(uint8 lchip)
{
#ifdef NEVER
    uint8 lchip_num = 0;

    uint32 cmd = 0;

    IpeTunnelDecapCtl_m ipe_tunnel_decap_ctl;

    sal_memset(&ipe_tunnel_decap_ctl, 0, sizeof(IpeTunnelDecapCtl_m));
    ipe_tunnel_decap_ctl.gre_flex_payload_packet_type      = 0;
    ipe_tunnel_decap_ctl.gre_flex_protocol                 = 0;
    ipe_tunnel_decap_ctl.gre_option2_check_en              = 1;
    ipe_tunnel_decap_ctl.ip_ttl_limit                      = 1;
    ipe_tunnel_decap_ctl.offset_byte_shift                 = 0;
    ipe_tunnel_decap_ctl.tunnel_martian_address_check_en   = 1;
    ipe_tunnel_decap_ctl.mcast_address_match_check_disable = 0;
    ipe_tunnel_decap_ctl.trill_ttl                         = 1;
    ipe_tunnel_decap_ctl.trillversion_check_en             = 1;
    ipe_tunnel_decap_ctl.trill_version                     = 0;
    ipe_tunnel_decap_ctl.trill_bfd_check_en                = 1;
    ipe_tunnel_decap_ctl.trill_bfd_hop_count               = 0x30;

    lchip_num = sys_goldengate_get_local_chip_num(lchip);

    if(TRUE)
    {
        cmd = DRV_IOW(IpeTunnelDecapCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_tunnel_decap_ctl));
    }

    return CTC_E_NONE;

#endif /* never */
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipe_trill_ctl_init(uint8 lchip)
{
#ifdef NEVER
    uint8 lchip_num = 0;

    uint32 cmd = 0;

    parser_trill_ctl_t parser_trill_ctl;
    IpeTrillCtl_m ipe_trill_ctl;

    sal_memset(&parser_trill_ctl, 0, sizeof(parser_trill_ctl_t));
    parser_trill_ctl.trill_inner_tpid                = 0x8200;
    parser_trill_ctl.inner_vlan_tpid_mode            = 1;
    parser_trill_ctl.r_bridge_channel_ether_type     = 0x22F5;
    parser_trill_ctl.trill_bfd_oam_channel_protocol0 = 2;
    parser_trill_ctl.trill_bfd_echo_channel_protocol = 3;
    parser_trill_ctl.trill_bfd_oam_channel_protocol1 = 0xFFF;

    sal_memset(&ipe_trill_ctl, 0, sizeof(IpeTrillCtl_m));
    ipe_trill_ctl.version_check_en        = 1;
    ipe_trill_ctl.trill_bypass_deny_route = 0;

    lchip_num = sys_goldengate_get_local_chip_num(lchip);

    if(TRUE)
    {
        cmd = DRV_IOW(ParserTrillCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &parser_trill_ctl));

        cmd = DRV_IOW(IpeTrillCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_trill_ctl));
    }

    return CTC_E_NONE;

#endif /* never */
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_epe_nexthop_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    EpeNextHopCtl_m epe_next_hop_ctl;

    sal_memset(&epe_next_hop_ctl, 0, sizeof(EpeNextHopCtl_m));
    cmd = DRV_IOR(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_next_hop_ctl));

    SetEpeNextHopCtl(V, discardSameMacAddr_f, &epe_next_hop_ctl, 1)
    cmd = DRV_IOW(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_next_hop_ctl));

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_epe_hdr_edit_ctl_init(uint8 lchip)
{
    uint32 cmd           = 0;
    uint32 tbl_id        = 0;
    uint32 field_val     = 0;
    EpeHdrEditLogicCtl2_m epe_edit;
    ctc_chip_device_info_t dev_info;

    /* cfg EpeHdrEditLogicCtl2, let pkt discard in epe */
    sal_memset(&epe_edit, 0, sizeof(EpeHdrEditLogicCtl0_m));
    tbl_id = EpeHdrEditLogicCtl2_t;  /* two slice */
    SetEpeHdrEditLogicCtl0(V, hardDiscardEn_f, &epe_edit, 1);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_edit));

    /* bit[6]=1, total 16bits */
    sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));
    sys_goldengate_chip_get_device_info(lchip, &dev_info);
    if (dev_info.version_id > 1)
    {
        cmd = DRV_IOR(EpeHdrEditReserved2_t, EpeHdrEditReserved2_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        CTC_BIT_SET(field_val, 6);
        cmd = DRV_IOW(EpeHdrEditReserved2_t, EpeHdrEditReserved2_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_epe_pkt_proc_ctl_init(uint8 lchip)
{
#ifdef NEVER
    uint8 lchip_num = 0;

    uint32 cmd = 0;

    epe_pkt_proc_ctl_t epe_pkt_proc_ctl;

    sal_memset(&epe_pkt_proc_ctl, 0, sizeof(epe_pkt_proc_ctl_t));
    epe_pkt_proc_ctl.discard_route_ttl0           = 1;
    epe_pkt_proc_ctl.discard_mpls_ttl0            = 1;
    epe_pkt_proc_ctl.discard_mpls_tag_ttl0        = 1;
    epe_pkt_proc_ctl.discard_tunnel_ttl0          = 1;
    epe_pkt_proc_ctl.discard_trill_ttl0           = 1;
    epe_pkt_proc_ctl.use_global_ctag_cos          = 0;
    epe_pkt_proc_ctl.mirror_escape_cam_en         = 0;
    epe_pkt_proc_ctl.icmp_err_msg_check_en        = 1;
    epe_pkt_proc_ctl.global_ctag_cos              = 0;
    epe_pkt_proc_ctl.wlan_roaming_state_bits    = 0;
    epe_pkt_proc_ctl.pt_multicast_address_en      = 0;
    epe_pkt_proc_ctl.pt_udp_checksum_zero_discard = 1;
    epe_pkt_proc_ctl.next_hop_stag_ctl_en         = 1;

    lchip_num = sys_goldengate_get_local_chip_num(lchip);

    if(TRUE)
    {
        cmd = DRV_IOW(EpePktProcCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_proc_ctl));
    }

    return CTC_E_NONE;

#endif /* never */
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_epe_port_mac_ctl_init(uint8 lchip)
{
#ifdef NEVER
    uint8 lchip_num = 0;

    uint32 cmd = 0;

    epe_l2_port_mac_sa_t epe_l2_port_mac_sa;

    sal_memset(&epe_l2_port_mac_sa, 0, sizeof(epe_l2_port_mac_sa_t));
    lchip_num = sys_goldengate_get_local_chip_num(lchip);

    if(TRUE)
    {
        cmd = DRV_IOW(EpeL2PortMacSa_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_l2_port_mac_sa));
    }

    return CTC_E_NONE;

#endif /* never */
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_epe_cos_map_init(uint8 lchip)
{

    uint32 cmd = 0;
    uint16 index = 0;
    uint8 dscp = 0;
    uint8 exp = 0;
    uint8 cos = 0;
    uint8 i_value = 0;
    ds_t ds;

    for (index = 0; index < 2048; index++)
    {
        if (0 == i_value % 256)
        {
            i_value = 0;
        }

        dscp = i_value / 4;
        exp  = i_value / 32;
        cos  = i_value / 32;
        i_value++;

        sal_memset(ds, 0, sizeof(ds));
        cmd = DRV_IOR(EpeEditPriorityMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));
        SetEpeEditPriorityMap(V, exp_f, ds, exp);
        SetEpeEditPriorityMap(V, dscp_f, ds, dscp);
        SetEpeEditPriorityMap(V, cfi_f, ds, 0);
        SetEpeEditPriorityMap(V, cos_f, ds, cos);
        cmd = DRV_IOW(EpeEditPriorityMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_buffer_store_ctl_init(uint8 lchip)
{

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_buffer_retrieve_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    BufRetrvCreditCtl0_m cred;

    /* enable dma channel */
    tbl_id = BufRetrvCreditCtl0_t;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cred));
    SetBufRetrvCreditCtl0(V,bufRetrvDma0CreditThrd_f, &cred, 4);
    SetBufRetrvCreditCtl0(V,bufRetrvDma1CreditThrd_f, &cred, 4);
    SetBufRetrvCreditCtl0(V,bufRetrvDma2CreditThrd_f, &cred, 4);
    SetBufRetrvCreditCtl0(V,bufRetrvDma3CreditThrd_f, &cred, 4);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cred));

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_qmgr_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    QMgrEnqLengthAdjust_m  length_adjust;
    QWriteSgmacCtl_m      qwrite_sgmac_ctl;

    cmd = DRV_IOR(QMgrEnqLengthAdjust_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &length_adjust));
    SetQMgrEnqLengthAdjust(V, adjustLength0_f , &length_adjust, 0);
    SetQMgrEnqLengthAdjust(V, adjustLength1_f , &length_adjust, 4);
    SetQMgrEnqLengthAdjust(V, adjustLength2_f , &length_adjust, 8);
    SetQMgrEnqLengthAdjust(V, adjustLength3_f , &length_adjust, 12);
    SetQMgrEnqLengthAdjust(V, adjustLength4_f , &length_adjust, 14);
    SetQMgrEnqLengthAdjust(V, adjustLength5_f , &length_adjust, 16);
    SetQMgrEnqLengthAdjust(V, adjustLength6_f , &length_adjust, 18);
    SetQMgrEnqLengthAdjust(V, adjustLength7_f , &length_adjust, 22);

    cmd = DRV_IOW(QMgrEnqLengthAdjust_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &length_adjust));

    /*Qwrite Sgmac Ctl init*/
    sal_memset(&qwrite_sgmac_ctl, 0, sizeof(qwrite_sgmac_ctl));
    cmd = DRV_IOR(QWriteSgmacCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qwrite_sgmac_ctl));
    SetQWriteSgmacCtl(V, sgmacEn_f, &qwrite_sgmac_ctl, 1);
    SetQWriteSgmacCtl(V, discardUnkownSgmacGroup_f, &qwrite_sgmac_ctl, 3);
    cmd = DRV_IOW(QWriteSgmacCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qwrite_sgmac_ctl));

    return CTC_E_NONE;
}

int32
sys_goldengate_lkup_adjust_len_index(uint8 lchip, uint8 adjust_len, uint8* len_index)
{
    uint32 cmd;
    uint8 index = 0;
    uint32 length = 0;
    uint8 bfind = 0;

    for (index = 0; index < 8; index++)
    {
        cmd = DRV_IOR(QMgrEnqLengthAdjust_t, QMgrEnqLengthAdjust_adjustLength0_f + index);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &length));

        if (length >= adjust_len)
        {
            *len_index = index;
            bfind = 1;
            break;
        }
    }
	if(!bfind)
	{
	   *len_index  = 7;
	}
    return CTC_E_NONE;

}

/* add default entry adindex=0 for ipuc */
int32
_sys_goldengate_mac_default_entry_init(uint8 lchip)
{
#ifdef NEVER
    ds_mac_t macda;
    uint32 cmd = 0;
    uint8 chip_num = 0;
    uint8 chip_id = 0;

    sal_memset(&macda, 0, sizeof(ds_mac_t));

    macda.ds_fwd_ptr = 0xFFFF;

    /* write default entry */
    chip_num = sys_goldengate_get_local_chip_num(lchip);

    for (chip_id = 0; chip_id < chip_num; chip_id++)
    {
        /* build DsMac entry */

        cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &macda));
    }

    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_timestamp_engine_init(uint8 lchip)
{
#ifdef NEVER
    uint8 lchip_num = 0;

    uint32 cmd = 0;
    ptp_frc_ctl_t ptp_frc_ctl;

    sal_memset(&ptp_frc_ctl, 0, sizeof(ptp_frc_ctl));
    lchip_num = sys_goldengate_get_local_chip_num(lchip);

    if(TRUE)
    {

        ptp_frc_ctl.frc_en_ref = 1;
        cmd = DRV_IOW(PtpFrcCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_frc_ctl));
    }

    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_ipg_init(uint8 lchip)
{

    uint32 cmd = 0;
    IpeIpgCtl_m  ipe_ipg_ctl;
    EpeIpgCtl_m  epe_ipg_ctl;

    sal_memset(&ipe_ipg_ctl, 0, sizeof(ipe_ipg_ctl));
    sal_memset(&epe_ipg_ctl, 0, sizeof(epe_ipg_ctl));

    SetIpeIpgCtl(V, array_0_ipg_f, &ipe_ipg_ctl, 20);
    SetIpeIpgCtl(V, array_1_ipg_f, &ipe_ipg_ctl, 20);
    SetIpeIpgCtl(V, array_2_ipg_f, &ipe_ipg_ctl, 20);
    SetIpeIpgCtl(V, array_3_ipg_f, &ipe_ipg_ctl, 20);

    SetEpeIpgCtl(V, array_0_ipg_f, &epe_ipg_ctl, 20);
    SetEpeIpgCtl(V, array_1_ipg_f, &epe_ipg_ctl, 20);
    SetEpeIpgCtl(V, array_2_ipg_f, &epe_ipg_ctl, 20);
    SetEpeIpgCtl(V, array_3_ipg_f, &epe_ipg_ctl, 20);

    cmd = DRV_IOW(IpeIpgCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ipg_ctl));

    cmd = DRV_IOW(EpeIpgCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_ipg_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mec_led_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 field_value = 0;

    uint32 core_freq = 0;
    uint32 div = 0;

    core_freq = sys_goldengate_get_core_freq(lchip, 1);

    cmd = DRV_IOR(SupDskCfg_t, SupDskCfg_cfgClkDividerClkOobFc_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &div));

    /*
        #2, Toggle the clockOobfc divider reset
        Cfg SupDskCfg.cfgClkResetClkOobFc = 1'b1;
        Cfg SupDskCfg.cfgClkResetClkOobFc = 1'b0;
    */
    field_value = 1;
    cmd = DRV_IOW(SupDskCfg_t, SupDskCfg_cfgClkResetClkOobFc_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_value = 0;
    cmd = DRV_IOW(SupDskCfg_t, SupDskCfg_cfgClkResetClkOobFc_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /*
        #3, Cfg interface MACLED clock Frequency=25MHz,@clockSlow=125MHz
        Cfg SupMiscCfg.cfgClkLedDiv[15:0] = 16'd5;
    */
    field_value = (core_freq / (div + 1) / SYS_CHIP_MAC_LED_CLK);
    cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgClkLedDiv_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /*
        #4, Toggle the clockLed divider reset
        Cfg SupMiscCfg.cfgResetClkLedDiv = 1'b1;
        Cfg SupMiscCfg.cfgResetClkLedDiv = 1'b0;
    */
    field_value = 1;
    cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgResetClkLedDiv_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_value = 0;
    cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgResetClkLedDiv_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_value = 0x014fb180;
    cmd = DRV_IOW(MacLedBlinkCfg0_t, MacLedBlinkCfg0_blinkOffInterval_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    cmd = DRV_IOW(MacLedBlinkCfg0_t, MacLedBlinkCfg0_blinkOnInterval_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    cmd = DRV_IOW(MacLedBlinkCfg1_t, MacLedBlinkCfg1_blinkOffInterval_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    cmd = DRV_IOW(MacLedBlinkCfg1_t, MacLedBlinkCfg1_blinkOnInterval_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_i2c_init(uint8 lchip)
{
#ifdef  EMULATION_PLATFORM
    return 0;
#endif

    uint32 cmd = 0;
    uint32 field_value = 0;

    /*
        #1, Cfg I2C interface MSCL clock Frequency=400KHz, supper super freq @100MHz
    */

    field_value = 0xFA0;
    cmd = DRV_IOW(I2CMasterCfg0_t, I2CMasterCfg0_clkDiv_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_value = 0xFA0;
    cmd = DRV_IOW(I2CMasterCfg1_t, I2CMasterCfg0_clkDiv_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_value = 1;
    cmd = DRV_IOW(I2CMasterInit0_t, I2CMasterInit0_init_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_value = 1;
    cmd = DRV_IOW(I2CMasterInit1_t, I2CMasterInit1_init_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));



    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_peri_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 field_value = 0;
    uint32 core_freq = 0;


    core_freq = sys_goldengate_get_core_freq(lchip, 1);

    /*
        #1, Cfg clockSlow Frequency=125MHz at core PLLOUTB Frequency=500MHz
        Cfg SupDskCfg.cfgClkDividerClkOobFc[4:0] = 5'd3;
    */
    field_value = (core_freq / SYS_REG_CLKSLOW_FREQ) - 1;
    cmd = DRV_IOW(SupDskCfg_t, SupDskCfg_cfgClkDividerClkOobFc_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    CTC_ERROR_RETURN(_sys_goldengate_mec_led_init(lchip));
    CTC_ERROR_RETURN(_sys_goldengate_i2c_init(lchip));
    CTC_ERROR_RETURN(sys_goldengate_mdio_init(lchip));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_chip_capability_init(uint8 lchip)
{
    uint32 size = 0;
    uint8 index = 0;

    /* init to invalid value 0xFFFFFFFF */
    for (index = 0; index < CTC_GLOBAL_CAPABILITY_MAX; index++)
    {
        p_gg_register_master[lchip]->chip_capability[index] = 0xFFFFFFFF;
    }

    CTC_ERROR_RETURN(sys_goldengate_ftm_get_dyn_entry_size(lchip, SYS_FTM_DYN_ENTRY_GLB_MET, &size));

    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_TUNNEL_ENTRY_NUM] = SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_TUNNEL);
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_SCL_HASH_ENTRY_NUM] = TABLE_MAX_INDEX(DsUserIdPortHashKey_t)/2;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_SCL_TCAM_ENTRY_NUM] = TABLE_MAX_INDEX(DsUserId0MacKey160_t)/2;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_SCL1_TCAM_ENTRY_NUM] = TABLE_MAX_INDEX(DsUserId1MacKey160_t)/2;

    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ECMP_DLB_FLOW_NUM] = 2*TABLE_MAX_INDEX(DsDlbFlowStateLeft_t);
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_MCAST_GROUP_NUM] = size/2;

    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L2PDU_L2HDR_PROTO_ENTRY_NUM] = MAX_SYS_L2PDU_BASED_L2HDR_PTL_ENTRY;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L2PDU_MACDA_ENTRY_NUM] = MAX_SYS_L2PDU_BASED_MACDA_ENTRY;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L2PDU_MACDA_LOW24_ENTRY_NUM] = MAX_SYS_L2PDU_BASED_MACDA_LOW24_ENTRY;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L2PDU_L2CP_MAX_ACTION_INDEX] = SYS_MAX_L2PDU_PER_PORT_ACTION_INDEX;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L3PDU_L3HDR_PROTO_ENTRY_NUM] = MAX_SYS_L3PDU_BASED_L3HDR_PROTO;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L3PDU_L4PORT_ENTRY_NUM] = MAX_SYS_L3PDU_BASED_PORT;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L3PDU_IPDA_ENTRY_NUM] = MAX_SYS_L3PDU_BASED_IPDA;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L3PDU_MAX_ACTION_INDEX] = SYS_MAX_L3PDU_ACTION_INDEX;

    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_EFD_FLOW_ENTRY_NUM] = TABLE_MAX_INDEX(DsElephantFlowState_t);
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_IPFIX_ENTRY_NUM] = TABLE_MAX_INDEX(DsIpfixL2HashKey_t);
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL_HASH_ENTRY_NUM] = TABLE_MAX_INDEX(DsFlowL2HashKey_t);
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL0_IGS_TCAM_ENTRY_NUM] = TABLE_MAX_INDEX(DsAclQosL3Key160Ingr0_t);
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL1_IGS_TCAM_ENTRY_NUM] = TABLE_MAX_INDEX(DsAclQosL3Key160Ingr1_t);
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL2_IGS_TCAM_ENTRY_NUM] = TABLE_MAX_INDEX(DsAclQosL3Key160Ingr2_t);
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL3_IGS_TCAM_ENTRY_NUM] = TABLE_MAX_INDEX(DsAclQosL3Key160Ingr3_t);
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL0_EGS_TCAM_ENTRY_NUM] = TABLE_MAX_INDEX(DsAclQosL3Key160Egr0_t);

    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_VLAN_NUM] = CTC_MAX_VLAN_ID+1;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_VLAN_RANGE_GROUP_NUM] = TABLE_MAX_INDEX(DsVlanRangeProfile_t);

    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_MPLS_ENTRY_NUM] = SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_MPLS);

    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_OAM_SESSION_NUM] = TABLE_MAX_INDEX(DsEthMep_t) / 2;

    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_APS_GROUP_NUM] = TABLE_MAX_INDEX(DsApsBridge_t);
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_NPM_SESSION_NUM] = 4;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_MAC_ENTRY_NUM] = SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_MAC);
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_IPMC_ENTRY_NUM] = SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_IPMC);
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ROUTE_MAC_ENTRY_NUM] = 128;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L3IF_NUM] = SYS_GG_MAX_CTC_L3IF_ID+1; /*0 is used to disable L3if in asic*/

    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_HOST_ROUTE_ENTRY_NUM] = SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_IPUC_HOST);
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_LPM_ROUTE_ENTRY_NUM] = TABLE_MAX_INDEX(DsLpmLookupKey_t);

    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_QUEUE_STATS_NUM] = SYS_ENQUEUE_STATS_SIZE+SYS_DEQUEUE_STATS_SIZE;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_POLICER_STATS_NUM] = SYS_POLICER_STATS_SIZE-1;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_SHARE2_STATS_NUM] = SYS_IPE_IF_STATS_SIZE-1;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL0_IGS_STATS_NUM] = SYS_ACL0_STATS_SIZE-1;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL1_IGS_STATS_NUM] = SYS_ACL1_STATS_SIZE-1;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL2_IGS_STATS_NUM] = SYS_ACL2_STATS_SIZE-1;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL3_IGS_STATS_NUM] = SYS_ACL3_STATS_SIZE-1;
    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL0_EGS_STATS_NUM] = SYS_EGS_ACL0_STATS_SIZE-1;

    p_gg_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_MAX_CHIP_NUM] = 32;

    return CTC_E_NONE;
}

int32
sys_goldengate_port_base_init(uint8 lchip)
{
    ds_t ds;
    uint32 cmd = 0;

    uint16 port_base = 0x100;
    uint8 index = 1;

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(UserIdHashLookupPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index , cmd, ds));
    SetUserIdHashLookupPortBaseCtl(V, localPhyPortBase_f, ds, port_base);
    cmd = DRV_IOW(UserIdHashLookupPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index,  cmd, ds));


    cmd = DRV_IOR(EpeAclQosPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));
    SetEpeAclQosPortBaseCtl(V, localPhyPortBase_f, ds, port_base);
    cmd = DRV_IOW(EpeAclQosPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));


    cmd = DRV_IOR(EpeHdrProcPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));
    SetEpeHdrProcPortBaseCtl(V, localPhyPortBase_f, ds, port_base);
    cmd = DRV_IOW(EpeHdrProcPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));

    cmd = DRV_IOR(EpeHeaderEditPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));
    SetEpeHeaderEditPortBaseCtl(V, localPhyPortBase_f, ds, port_base);
    cmd = DRV_IOW(EpeHeaderEditPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));

    cmd = DRV_IOR(EpeNextHopPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));
    SetEpeNextHopPortBaseCtl(V, localPhyPortBase_f, ds, port_base);
    cmd = DRV_IOW(EpeNextHopPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));

    cmd = DRV_IOR(IpeFwdPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));
    SetIpeFwdPortBaseCtl(V, localPhyPortBase_f, ds, port_base);
    cmd = DRV_IOW(IpeFwdPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));

    cmd = DRV_IOR(IpeLookupPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));
    SetIpeLookupPortBaseCtl(V, localPhyPortBase_f, ds, port_base);
    cmd = DRV_IOW(IpeLookupPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));

    cmd = DRV_IOR(IpeUserIdPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));
    SetIpeUserIdPortBaseCtl(V, userIdLocalPhyPortBase_f, ds, port_base);
    cmd = DRV_IOW(IpeUserIdPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_register_oam_init(uint8 lchip)/*for c2c*/
{
    uint32 cpu_excp[2]  =  {0xFFFFFFFF, 0xFFFFFFFF};
    uint32 field_val = 0;
    uint32 cmd = 0;
    uint8 gchip_id = 0;

    cmd = DRV_IOW(OamHeaderEditCtl_t, OamHeaderEditCtl_cpuExceptionEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cpu_excp));

    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip_id));
    field_val = gchip_id;              /*global chipid*/
    cmd = DRV_IOW(IpeOamCtl_t, IpeOamCtl_linkOamDestChipId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_set_buf_misc_init(uint8 lchip, uint32 value)
{
    uint32 cmd = 0;
    BufRetrvMiscConfig0_m buf_misc_cfg0;
    BufRetrvMiscConfig1_m buf_misc_cfg1;
    EpeHdrEditExtraCreditConfig0_m epe_extra_credit0;
    EpeHdrEditExtraCreditConfig1_m epe_extra_credit1;
    sal_memset(&buf_misc_cfg0, 0, sizeof(BufRetrvMiscConfig0_m));
    sal_memset(&buf_misc_cfg1, 0, sizeof(BufRetrvMiscConfig1_m));
    sal_memset(&epe_extra_credit0, 0, sizeof(EpeHdrEditExtraCreditConfig0_m));
    sal_memset(&epe_extra_credit1, 0, sizeof(EpeHdrEditExtraCreditConfig1_m));

    cmd = DRV_IOR(BufRetrvMiscConfig0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_misc_cfg0));

    SetBufRetrvMiscConfig0(V, extraCreditUsed_f, &buf_misc_cfg0, value);
    SetBufRetrvMiscConfig0(V, extraCreditStackingUsed_f, &buf_misc_cfg0, value);

    cmd = DRV_IOW(BufRetrvMiscConfig0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_misc_cfg0));

    cmd = DRV_IOR(BufRetrvMiscConfig1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_misc_cfg1));

    SetBufRetrvMiscConfig1(V, extraCreditUsed_f, &buf_misc_cfg1, value);
    SetBufRetrvMiscConfig1(V, extraCreditStackingUsed_f, &buf_misc_cfg1, value);

    cmd = DRV_IOW(BufRetrvMiscConfig1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_misc_cfg1));

    cmd = DRV_IOR(EpeHdrEditExtraCreditConfig0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_extra_credit0));

    SetEpeHdrEditExtraCreditConfig0(V, extraCredit_f, &epe_extra_credit0, value);
    SetEpeHdrEditExtraCreditConfig0(V, extraCreditUplink_f, &epe_extra_credit0, value);

    cmd = DRV_IOW(EpeHdrEditExtraCreditConfig0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_extra_credit0));

    cmd = DRV_IOR(EpeHdrEditExtraCreditConfig1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_extra_credit1));

    SetEpeHdrEditExtraCreditConfig1(V, extraCredit_f, &epe_extra_credit1, value);
    SetEpeHdrEditExtraCreditConfig1(V, extraCreditUplink_f, &epe_extra_credit1, value);

    cmd = DRV_IOW(EpeHdrEditExtraCreditConfig1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_extra_credit1));

    return CTC_E_NONE;
}

int32
sys_goldengate_register_init(uint8 lchip)
{

    p_gg_register_master[lchip] = mem_malloc(MEM_SYSTEM_MODULE, sizeof(sys_register_master_t));
    if (NULL == p_gg_register_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_register_master[lchip], 0, sizeof(sys_register_master_t));

    p_gg_register_master[lchip]->wb_sync_cb[CTC_FEATURE_CHIP] = sys_goldengate_datapath_wb_sync;

#if 1 /*For Co-Sim fast*/
      /*edram and sram clear */
    CTC_ERROR_RETURN(sys_goldengate_tcam_edram_init(lchip));
#endif

    CTC_ERROR_RETURN(_sys_goldengate_lkp_ctl_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_misc_ctl_init(lchip));

    /* mpls */
    CTC_ERROR_RETURN(_sys_goldengate_mpls_ctl_init(lchip));

    /* nexthop */
    CTC_ERROR_RETURN(_sys_goldengate_nexthop_ctl_init(lchip));

    /* exception */
    CTC_ERROR_RETURN(_sys_goldengate_excp_ctl_init(lchip));

    /* net tx and rx */
    CTC_ERROR_RETURN(_sys_goldengate_net_rx_tx_init(lchip));

    /* mux ctl */
    CTC_ERROR_RETURN(_sys_goldengate_mux_ctl_init(lchip));

    /* ipe hdr adjust ctl */
    CTC_ERROR_RETURN(_sys_goldengate_ipe_hdr_adjust_ctl_init(lchip));

    /* ipe loopback hdr adjust ctl */
    CTC_ERROR_RETURN(_sys_goldengate_ipe_loopback_hdr_adjust_ctl_init(lchip));

    /* ipe dsvlan ctl */
    CTC_ERROR_RETURN(_sys_goldengate_ipe_ds_vlan_ctl(lchip));

    /* ipe userid ctl */
    CTC_ERROR_RETURN(_sys_goldengate_ipe_usrid_ctl_init(lchip));

    /* ipe route mac ctl */
    CTC_ERROR_RETURN(_sys_goldengate_ipe_route_mac_ctl_init(lchip));

    /* ipe mpls ctl */
    CTC_ERROR_RETURN(_sys_goldengate_ipe_mpls_ctl_init(lchip));

    /* ipe mpls exp map */
    CTC_ERROR_RETURN(_sys_goldengate_ipe_mpls_exp_map_init(lchip));

    /* ipe tunnel id ctl */
    CTC_ERROR_RETURN(_sys_goldengate_ipe_tunnel_id_ctl_init(lchip));

    /* ipe mcast force route ctl */
    CTC_ERROR_RETURN(_sys_goldengate_ipe_mcast_force_route_init(lchip));

    /* ipe route ctl */
    CTC_ERROR_RETURN(_sys_goldengate_ipe_route_ctl_init(lchip));

    /* ipe brige ctl */
    CTC_ERROR_RETURN(_sys_goldengate_ipe_brg_ctl_init(lchip));

    /* ipe cos mapping */
    CTC_ERROR_RETURN(_sys_goldengate_ipe_cos_map_init(lchip));

    /* ipe fwd ctl */
    CTC_ERROR_RETURN(_sys_goldengate_ipe_fwd_ctl_init(lchip));

    /* ipe tunnel ctl */
    CTC_ERROR_RETURN(_sys_goldengate_ipe_tunnell_ctl_init(lchip));

    /* ipe trill ctl */
    CTC_ERROR_RETURN(_sys_goldengate_ipe_trill_ctl_init(lchip));

    /* epe nexthop ctl */
    CTC_ERROR_RETURN(_sys_goldengate_epe_nexthop_ctl_init(lchip));

    /* epe hdr edit ctl */
    CTC_ERROR_RETURN(_sys_goldengate_epe_hdr_edit_ctl_init(lchip));

    /* epe pkt proc ctl */
    CTC_ERROR_RETURN(_sys_goldengate_epe_pkt_proc_ctl_init(lchip));

    /* epe port mac ctl */
    CTC_ERROR_RETURN(_sys_goldengate_epe_port_mac_ctl_init(lchip));

    /* epe cos mapping ctl */
    CTC_ERROR_RETURN(_sys_goldengate_epe_cos_map_init(lchip));


    /* buff store ctl */
    CTC_ERROR_RETURN(_sys_goldengate_buffer_store_ctl_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_mac_default_entry_init(lchip));

    /* buf retrive ctl , now only process slice0 TODO slice1*/
    CTC_ERROR_RETURN(_sys_goldengate_buffer_retrieve_ctl_init(lchip));

    /* Timestamp engine */
    CTC_ERROR_RETURN(_sys_goldengate_timestamp_engine_init(lchip));

    /* Ipg init */
    CTC_ERROR_RETURN(_sys_goldengate_ipg_init(lchip));

    /* peri io init */
    CTC_ERROR_RETURN(_sys_goldengate_peri_init(lchip));

    /*init port base for two slice*/
    CTC_ERROR_RETURN(sys_goldengate_port_base_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_qmgr_ctl_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_chip_capability_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_register_oam_init(lchip));

    /*init BufRetrvMiscConfig credit*/
    CTC_ERROR_RETURN(_sys_goldengate_set_buf_misc_init(lchip, 1));

    return CTC_E_NONE;
}

int32
sys_goldengate_register_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_register_master[lchip])
    {
        return CTC_E_NONE;
    }

    mem_free(p_gg_register_master[lchip]);

    return CTC_E_NONE;
}

int32
_sys_goldengate_set_warmboot_sync(uint8 lchip)
{
    uint8 i;
    /*sync up all data to memory */

    for (i = 0; i < CTC_FEATURE_MAX; i++)
    {
        if (p_gg_register_master[lchip]->wb_sync_cb[i])
        {
            CTC_ERROR_RETURN(p_gg_register_master[lchip]->wb_sync_cb[i](lchip));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_global_get_chip_capability(uint8 lchip, uint32* p_capability)
{
    CTC_PTR_VALID_CHECK(p_capability);

    sal_memcpy(p_capability, p_gg_register_master[lchip]->chip_capability, CTC_GLOBAL_CAPABILITY_MAX*sizeof(uint32));

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_global_ctl_l4_check_en(uint8 lchip, ctc_global_control_type_t type, uint32* value, uint8 is_set)
{
    uint32 field_val = 0;
    uint32 cmd = 0;
    uint8 bit = 0;

    switch (type)
    {
        case CTC_GLOBAL_DISCARD_TCP_SYN_0_PKT:
            bit = 0;
            break;
        case CTC_GLOBAL_DISCARD_TCP_NULL_PKT:
            bit = 1;
            break;
        case CTC_GLOBAL_DISCARD_TCP_XMAS_PKT:
            bit = 2;
            break;
        case CTC_GLOBAL_DISCARD_TCP_SYN_FIN_PKT:
            bit = 3;
            break;
        case CTC_GLOBAL_DISCARD_SAME_L4_PORT_PKT:
            bit = 4;
            break;
        case CTC_GLOBAL_DISCARD_ICMP_FRAG_PKT:
            bit = 5;
            break;
        default:
            return CTC_E_NOT_SUPPORT;
    }

    cmd = DRV_IOR(IpeIntfMapperCtl_t, IpeIntfMapperCtl_layer4SecurityCheckEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    if (is_set)
    {
        if (*value)
        {
            CTC_BIT_SET(field_val, bit);
        }
        else
        {
            CTC_BIT_UNSET(field_val, bit);
        }
        cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_layer4SecurityCheckEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }
    else
    {
        *value = CTC_IS_BIT_SET(field_val, bit)? 1 : 0;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_global_ctl_set_arp_check_en(uint8 lchip, ctc_global_control_type_t type, uint32* value)
{
    uint32 cmd = 0;
    switch (type)
    {
        case CTC_GLOBAL_ARP_MACDA_CHECK_EN:
            cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_arpDestMacCheckEn_f);
            break;
        case CTC_GLOBAL_ARP_MACSA_CHECK_EN:
            cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_arpSrcMacCheckEn_f);
            break;
        case CTC_GLOBAL_ARP_IP_CHECK_EN:
            cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_arpIpCheckEn_f);
            break;
        case CTC_GLOBAL_ARP_CHECK_FAIL_TO_CPU:
            cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_arpCheckExceptionEn_f);
            break;
        default:
            return CTC_E_NOT_SUPPORT;
    }
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, value));

    return CTC_E_NONE;

}


STATIC int32
_sys_goldengate_global_ctl_get_arp_check_en(uint8 lchip, ctc_global_control_type_t type, uint32* value)
{
    uint32 cmd = 0;

    switch (type)
    {
        case CTC_GLOBAL_ARP_MACDA_CHECK_EN:
            cmd = DRV_IOR(IpeIntfMapperCtl_t, IpeIntfMapperCtl_arpDestMacCheckEn_f);
            break;
        case CTC_GLOBAL_ARP_MACSA_CHECK_EN:
            cmd = DRV_IOR(IpeIntfMapperCtl_t, IpeIntfMapperCtl_arpSrcMacCheckEn_f);
            break;
        case CTC_GLOBAL_ARP_IP_CHECK_EN:
            cmd = DRV_IOR(IpeIntfMapperCtl_t, IpeIntfMapperCtl_arpIpCheckEn_f);
            break;
        case CTC_GLOBAL_ARP_CHECK_FAIL_TO_CPU:
            cmd = DRV_IOR(IpeIntfMapperCtl_t, IpeIntfMapperCtl_arpCheckExceptionEn_f);
            break;
        default:
            return CTC_E_NOT_SUPPORT;
    }
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, value));

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_set_glb_acl_property(uint8 lchip, ctc_global_acl_property_t* p_acl_property)
{
    uint32 cmd = 0;
    uint32 igr_en = 0;
    uint32 egr_en = 0;

    CTC_PTR_VALID_CHECK(p_acl_property);
    CTC_MAX_VALUE_CHECK(p_acl_property->dir,CTC_BOTH_DIRECTION);

    CTC_MAX_VALUE_CHECK(p_acl_property->discard_pkt_lkup_en,1);

    if(p_acl_property->dir == CTC_INGRESS || p_acl_property->dir == CTC_BOTH_DIRECTION )
    {
        igr_en = p_acl_property->discard_pkt_lkup_en;
        cmd = DRV_IOW(IpeAclQosCtl_t, IpeAclQosCtl_discardPacketFollowAcl_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_en));

        igr_en = p_acl_property->l2_type_as_vlan_num ? 1:0;
        cmd = DRV_IOW(IpeAclLookupCtl_t, IpeAclLookupCtl_layer2TypeUsedAsVlanNum_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_en));
    }
    if(p_acl_property->dir == CTC_EGRESS || p_acl_property->dir == CTC_BOTH_DIRECTION)
    {
        egr_en = p_acl_property->discard_pkt_lkup_en;
        cmd = DRV_IOW(EpeAclQosCtl_t, EpeAclQosCtl_discardPacketFollowAcl_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egr_en));
        egr_en = p_acl_property->l2_type_as_vlan_num ? 1:0;
        cmd = DRV_IOW(EpeAclQosCtl_t, EpeAclQosCtl_layer2TypeUsedAsVlanNum_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egr_en));
    }
    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_get_glb_acl_property(uint8 lchip, ctc_global_acl_property_t* p_acl_property)
{
    uint32 cmd = 0;
    uint32 igr_en = 0;
    uint32 egr_en = 0;

    CTC_PTR_VALID_CHECK(p_acl_property);

    if(p_acl_property->dir == CTC_INGRESS)
    {
        cmd = DRV_IOR(IpeAclQosCtl_t, IpeAclQosCtl_discardPacketFollowAcl_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_en));
        p_acl_property->discard_pkt_lkup_en = igr_en;
        cmd = DRV_IOR(IpeAclLookupCtl_t, IpeAclLookupCtl_layer2TypeUsedAsVlanNum_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_en));
        p_acl_property->l2_type_as_vlan_num = igr_en?1:0;
    }
    else if(p_acl_property->dir == CTC_EGRESS)
    {
        cmd = DRV_IOR(EpeAclQosCtl_t, EpeAclQosCtl_discardPacketFollowAcl_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egr_en));
        p_acl_property->discard_pkt_lkup_en = egr_en;
        cmd = DRV_IOR(EpeAclQosCtl_t, EpeAclQosCtl_layer2TypeUsedAsVlanNum_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egr_en));
        p_acl_property->l2_type_as_vlan_num = egr_en ?1:0;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_set_glb_ipmc_property(uint8 lchip, ctc_global_ipmc_property_t* pipmc_property)
{
    uint32 cmd = 0;
    uint32 value = 0;
    CTC_PTR_VALID_CHECK(pipmc_property);
    CTC_MAX_VALUE_CHECK(pipmc_property->vrf_mode, 1);
    value = pipmc_property->vrf_mode;
    cmd = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_ipmcUseVlan_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_get_glb_ipmc_property(uint8 lchip, ctc_global_ipmc_property_t* pipmc_property)
{
    uint32 cmd = 0;
    uint32 value = 0;
    CTC_PTR_VALID_CHECK(pipmc_property);
    cmd = DRV_IOR(IpeLookupCtl_t, IpeLookupCtl_ipmcUseVlan_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    pipmc_property->vrf_mode = value? 1 : 0;
    return CTC_E_NONE;
}


int32
sys_goldengate_global_ctl_set(uint8 lchip, ctc_global_control_type_t type, void* value)
{
    uint32 field_val = 0;
    uint32 cmd = 0;
    uint8 enable = 0;
    uint8 chan_id = 0;
    ds_t ds;
    ctc_chip_device_info_t dev_info;
    ctc_global_overlay_decap_mode_t* p_decap_mode = (ctc_global_overlay_decap_mode_t*)value;

    CTC_PTR_VALID_CHECK(value);
    switch(type)
    {
    case CTC_GLOBAL_DISCARD_SAME_MACDASA_PKT:
        field_val = *(bool *)value ? 1 : 0;

        cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_discardSameMacAddr_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_discardSameMacAddr_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_GLOBAL_DISCARD_SAME_IPDASA_PKT:
        field_val = *(bool *)value ? 1 : 0;

        cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_discardSameIpAddr_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_discardSameIpAddr_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_GLOBAL_DISCARD_TTL_0_PKT:
        field_val = *(bool *)value ? 1 : 0;

        sal_memset(ds, 0, sizeof(ds));
        cmd = DRV_IOR(EpePktProcCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
        SetEpePktProcCtl(V, discardMplsTagTtl0_f, ds, field_val);
        SetEpePktProcCtl(V, discardMplsTtl0_f, ds, field_val);
        SetEpePktProcCtl(V, discardRouteTtl0_f, ds, field_val);
        SetEpePktProcCtl(V, discardTrillTtl0_f, ds, field_val);
        SetEpePktProcCtl(V, discardTunnelTtl0_f, ds, field_val);
        SetEpePktProcCtl(V, ofTtlFailDiscard_f, ds, field_val);
        cmd = DRV_IOW(EpePktProcCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
        break;

    case CTC_GLOBAL_DISCARD_MCAST_SA_PKT:
        field_val = *(bool *)value ? 0 : 1;

        cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_allowMcastMacSa_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        break;

    case CTC_GLOBAL_ACL_CHANGE_COS_ONLY:
        field_val = *(bool *)value ? 1 : 0;
        cmd = DRV_IOW(IpeAclQosCtl_t, IpeAclQosCtl_aclMergeVlanAction_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_GLOBAL_ECMP_DLB_MODE:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, CTC_GLOBAL_ECMP_DLB_MODE_MAX - 1);

        if (CTC_GLOBAL_ECMP_DLB_MODE_ELEPHANT == *(uint32*)value)
        {
            field_val = 1;
        }
        else if (CTC_GLOBAL_ECMP_DLB_MODE_TCP == *(uint32*)value)
        {
            field_val = 3;
        }
        else if (CTC_GLOBAL_ECMP_DLB_MODE_ALL == *(uint32*)value)
        {
            field_val = 2;
        }

        cmd = DRV_IOW(IpeEcmpCtl_t, IpeEcmpCtl_dlbEnableMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_GLOBAL_ECMP_REBALANCE_MODE:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, CTC_GLOBAL_ECMP_REBALANCE_MODE_MAX - 1);

        if (CTC_GLOBAL_ECMP_REBALANCE_MODE_NORMAL == *(uint32*)value)
        {
            field_val = 2;
        }
        else if (CTC_GLOBAL_ECMP_REBALANCE_MODE_FIRST == *(uint32*)value)
        {
            field_val = 0;
        }
        else if (CTC_GLOBAL_ECMP_REBALANCE_MODE_PACKET == *(uint32*)value)
        {
            field_val = 1;
        }

        cmd = DRV_IOW(DlbEngineCtl_t, DlbEngineCtl_rebalanceMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

     case CTC_GLOBAL_ECMP_FLOW_AGING_INTERVAL:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 4294967);
        field_val = ((*(uint32*)value) * 250) & 0x3FFFFFFF;
        cmd = DRV_IOW(DlbEngineTimerCtl_t, DlbEngineTimerCtl_cfgRefDivAgingRstPulse_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_GLOBAL_ECMP_FLOW_INACTIVE_INTERVAL:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 0xFFFFFFFF);
        field_val = ((*(uint32*)value) >> 3) & 0x3FFFFFFF;
        cmd = DRV_IOW(DlbEngineTimerCtl_t, DlbEngineTimerCtl_cfgRefDivInactiveRstPulse_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_GLOBAL_ECMP_FLOW_PKT_INTERVAL:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 0xFFFF);

        if((*(uint32*)value) <= 256)        /* every 256 packets */
        {
            field_val = 1;
        }
        else if((*(uint32*)value) <= 1024)  /* every 1024 packets */
        {
            field_val = 0;
        }
        else if((*(uint32*)value) <= 8192)  /* every 8192 packets */
        {
            field_val = 2;
        }
        else                                 /* every 32768 packets */
        {
            field_val = 3;
        }

        cmd = DRV_IOW(DlbEngineCtl_t, DlbEngineCtl_reorderPktIntervalMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_GLOBAL_LINKAGG_FLOW_INACTIVE_INTERVAL:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 0xFF);
        field_val = (*(uint32*)value);
        cmd = DRV_IOW(QLinkAggTimerCtl0_t, QLinkAggTimerCtl0_tsThreshold0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_GLOBAL_STACKING_TRUNK_FLOW_INACTIVE_INTERVAL:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 0xFF);
        field_val = (*(uint32*)value);
        cmd = DRV_IOW(QLinkAggTimerCtl1_t, QLinkAggTimerCtl1_tsThreshold1_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_GLOBAL_DISCARD_TCP_SYN_0_PKT:
    case CTC_GLOBAL_DISCARD_TCP_NULL_PKT:
    case CTC_GLOBAL_DISCARD_TCP_XMAS_PKT:
    case CTC_GLOBAL_DISCARD_TCP_SYN_FIN_PKT:
    case CTC_GLOBAL_DISCARD_SAME_L4_PORT_PKT:
    case CTC_GLOBAL_DISCARD_ICMP_FRAG_PKT:
        CTC_ERROR_RETURN(_sys_goldengate_global_ctl_l4_check_en(lchip, type, ((uint32*)value), 1));
        break;

    case CTC_GLOBAL_ARP_MACDA_CHECK_EN:
    case CTC_GLOBAL_ARP_MACSA_CHECK_EN:
    case CTC_GLOBAL_ARP_IP_CHECK_EN:
    case CTC_GLOBAL_ARP_CHECK_FAIL_TO_CPU:
        CTC_ERROR_RETURN(_sys_goldengate_global_ctl_set_arp_check_en(lchip, type, ((uint32*)value)));
        break;

     case CTC_GLOBAL_WARMBOOT_STATUS:               /**< [GG] warmboot status */
        drv_goldengate_set_warmboot_status(lchip, *(uint32*)value);
        if (*(uint32*)value == CTC_WB_STATUS_DONE)
        {
            /*clear resetDmaCtl0 */
            field_val = 0;
            cmd = DRV_IOW(SupResetCtl_t, SupResetCtl_resetDmaCtl0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        }
        else if (*(uint32*)value == CTC_WB_STATUS_SYNC)
        {
            CTC_ERROR_RETURN(_sys_goldengate_set_warmboot_sync(lchip));
        }
        break;

    case CTC_GLOBAL_WARMBOOT_CPU_RX_EN:
        enable = *(bool *)value ? 1 : 0;

        for(chan_id = 0; chan_id < SYS_DMA_MAX_CHAN_NUM; chan_id++)
        {
            CTC_ERROR_RETURN(sys_goldengate_dma_set_chan_en(lchip, chan_id, enable));
        }

        CTC_ERROR_RETURN(sys_goldengate_interrupt_set_group_en(lchip, enable));
        break;

    case CTC_GLOBAL_FLOW_PROPERTY:
        CTC_MAX_VALUE_CHECK(((ctc_global_flow_property_t*)value)->igs_vlan_range_mode, CTC_GLOBAL_VLAN_RANGE_MODE_MAX-1);
        CTC_MAX_VALUE_CHECK(((ctc_global_flow_property_t*)value)->egs_vlan_range_mode, CTC_GLOBAL_VLAN_RANGE_MODE_MAX-1);

        if (CTC_GLOBAL_VLAN_RANGE_MODE_SCL == ((ctc_global_flow_property_t*)value)->igs_vlan_range_mode)
        {
            field_val = 0;
        }
        else if (CTC_GLOBAL_VLAN_RANGE_MODE_ACL == ((ctc_global_flow_property_t*)value)->igs_vlan_range_mode)
        {
            field_val = 1;
        }
        else if (CTC_GLOBAL_VLAN_RANGE_MODE_SHARE == ((ctc_global_flow_property_t*)value)->igs_vlan_range_mode)
        {
            field_val = 2;
        }

        cmd = DRV_IOW(IpeUserIdCtl_t, IpeUserIdCtl_vlanRangeMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        if (CTC_GLOBAL_VLAN_RANGE_MODE_SCL == ((ctc_global_flow_property_t*)value)->egs_vlan_range_mode)
        {
            field_val = 0;
        }
        else if (CTC_GLOBAL_VLAN_RANGE_MODE_ACL == ((ctc_global_flow_property_t*)value)->egs_vlan_range_mode)
        {
            field_val = 1;
        }
        else if (CTC_GLOBAL_VLAN_RANGE_MODE_SHARE == ((ctc_global_flow_property_t*)value)->egs_vlan_range_mode)
        {
            field_val = 2;
        }

        cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_vlanRangeMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_GLOBAL_ELOOP_USE_LOGIC_DESTPORT:
        /* set bit[1], total 16bits */
        sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));
        sys_goldengate_chip_get_device_info(lchip, &dev_info);
        if (dev_info.version_id > 1)
        {
            cmd = DRV_IOR(EpeHdrEditReserved2_t, EpeHdrEditReserved2_reserved_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
            if (TRUE == *(bool *)value)
            {
                CTC_BIT_SET(field_val, 1);
            }
            else
            {
                CTC_BIT_UNSET(field_val, 1);
            }

            cmd = DRV_IOW(EpeHdrEditReserved2_t, EpeHdrEditReserved2_reserved_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        }
        break;

    case CTC_GLOBAL_ACL_PROPERTY:
        CTC_ERROR_RETURN(_sys_goldengate_set_glb_acl_property(lchip, (ctc_global_acl_property_t*)value));
        break;

    case CTC_GLOBAL_PIM_SNOOPING_MODE:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 1);
        field_val = *(uint32 *)value ? 0 : 1;
        cmd = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_pimSnoopingEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;
    case CTC_GLOBAL_IPMC_PROPERTY:
        CTC_ERROR_RETURN(_sys_goldengate_set_glb_ipmc_property(lchip, (ctc_global_ipmc_property_t*)value));
        break;

    case CTC_GLOBAL_VXLAN_UDP_DEST_PORT:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 0xFFFF);
        field_val = *(uint32*)value;
        if (0 == field_val)
        {
            field_val = 4789;
        }
        cmd = DRV_IOW(EpePktProcCtl_t, EpePktProcCtl_vxlanV4UdpDestPort_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        cmd = DRV_IOW(EpePktProcCtl_t, EpePktProcCtl_vxlanV6UdpDestPort_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        cmd = DRV_IOW(ParserLayer4AppCtl_t, ParserLayer4AppCtl_vxlanIpv4UdpDestPort_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        cmd = DRV_IOW(ParserLayer4AppCtl_t, ParserLayer4AppCtl_vxlanIpv6UdpDestPort_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;
    case CTC_GLOBAL_GENEVE_UDP_DEST_PORT:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 0xFFFF);
        field_val = *(uint32*)value;
        if (0 == field_val)
        {
            field_val = 6081;
        }
        cmd = DRV_IOW(EpePktProcCtl_t, EpePktProcCtl_geneveV4UdpDestPort_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        cmd = DRV_IOW(EpePktProcCtl_t, EpePktProcCtl_geneveV6UdpDestPort_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        cmd = DRV_IOW(ParserLayer4AppCtl_t, ParserLayer4AppCtl_geneveVxlanUdpDestPort_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;
    case CTC_GLOBAL_NH_FORCE_BRIDGE_DISABLE:
        field_val = (*(uint32*)value)?0:1;
        cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_forceBridgeL3Match_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;
    case CTC_GLOBAL_NH_MCAST_LOGIC_REP_EN:
        enable = *(bool *)value ? 1 : 0;
        CTC_ERROR_RETURN(sys_goldengate_nh_set_ipmc_logic_replication(lchip, enable));
        break;
    case CTC_GLOBAL_ARP_VLAN_CLASS_EN:
        field_val = (*(uint32*)value)? 1 : 0;
        cmd = DRV_IOW(IpeUserIdCtl_t, IpeUserIdCtl_arpForceIpv4_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;
    case CTC_GLOBAL_IGS_RANDOM_LOG_SHIFT:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 7);
        field_val = *(uint32*)value;
        cmd = DRV_IOW(IpeUserIdCtl_t, IpeUserIdCtl_randomLogShift_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;
    case CTC_GLOBAL_EGS_RANDOM_LOG_SHIFT:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 7);
        field_val = *(uint32*)value;
        cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_randomLogShift_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;
    case CTC_GLOBAL_OVERLAY_DECAP_MODE:
        CTC_MAX_VALUE_CHECK(p_decap_mode->scl_id, 1);
        cmd = DRV_IOW(IpeUserIdCtl_t, (p_decap_mode->scl_id)? IpeUserIdCtl_vxlanTunnelHash2LkpMode_f: IpeUserIdCtl_vxlanTunnelHash1LkpMode_f);
        field_val = p_decap_mode->vxlan_mode ? 0:1;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        cmd = DRV_IOW(IpeUserIdCtl_t, (p_decap_mode->scl_id)? IpeUserIdCtl_nvgreTunnelHash2LkpMode_f: IpeUserIdCtl_nvgreTunnelHash1LkpMode_f);
        field_val = p_decap_mode->nvgre_mode ? 0:1;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;
    case CTC_GLOBAL_EGS_STK_ACL_DIS:
        cmd = DRV_IOW(EpeAclQosCtl_t, EpeAclQosCtl_stackingDisableAcl_f);
        field_val = (*(bool*)value)? 1:0;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;
    default:
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_global_get_panel_ports(uint8 lchip, ctc_global_panel_ports_t* phy_info)
{
    uint16 lport = 0;
    uint8 count = 0;
    int32 ret = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    lchip = phy_info->lchip;
    for (lport = 0; lport < SYS_GG_MAX_PORT_NUM_PER_CHIP; lport++)
    {
        ret = sys_goldengate_common_get_port_capability(lchip, lport, &port_attr);
        if (ret < 0)
        {
            continue;
        }

        if (port_attr->port_type !=SYS_DATAPATH_NETWORK_PORT)
        {
            continue;
        }

        phy_info->lport[count++] = lport;
    }
    phy_info->count = count;

    return CTC_E_NONE;

}

int32
sys_goldengate_global_ctl_get(uint8 lchip, ctc_global_control_type_t type, void* value)
{
    uint32 field_val = 0;
    uint8 enable = 0;
    uint8 chan_id = 0;
    uint32 cmd = 0;
    ctc_chip_device_info_t dev_info;
    ctc_global_overlay_decap_mode_t* p_decap_mode = (ctc_global_overlay_decap_mode_t*)value;

    CTC_PTR_VALID_CHECK(value);

    switch(type)
    {
    case CTC_GLOBAL_DISCARD_SAME_MACDASA_PKT:
        cmd = DRV_IOR(IpeIntfMapperCtl_t, IpeIntfMapperCtl_discardSameMacAddr_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(bool *)value = field_val?TRUE:FALSE;
        break;

    case CTC_GLOBAL_DISCARD_SAME_IPDASA_PKT:
        cmd = DRV_IOR(IpeIntfMapperCtl_t, IpeIntfMapperCtl_discardSameIpAddr_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(bool *)value = field_val?TRUE:FALSE;
        break;

    case CTC_GLOBAL_DISCARD_TTL_0_PKT:
        cmd = DRV_IOR(EpePktProcCtl_t, EpePktProcCtl_discardMplsTagTtl0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(bool *)value = field_val?TRUE:FALSE;
        break;

    case CTC_GLOBAL_DISCARD_MCAST_SA_PKT:
        cmd = DRV_IOR(IpeIntfMapperCtl_t, IpeIntfMapperCtl_allowMcastMacSa_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(bool *)value = field_val?FALSE:TRUE;
        break;

    case CTC_GLOBAL_ACL_CHANGE_COS_ONLY:
        cmd = DRV_IOR(IpeAclQosCtl_t, IpeAclQosCtl_aclMergeVlanAction_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(bool *)value = field_val ? TRUE:FALSE;
        break;

    case CTC_GLOBAL_ECMP_DLB_MODE:
        cmd = DRV_IOR(IpeEcmpCtl_t, IpeEcmpCtl_dlbEnableMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        if (1 == field_val)
        {
            *(uint32*)value = CTC_GLOBAL_ECMP_DLB_MODE_ELEPHANT;
        }
        else if (3 == field_val)
        {
            *(uint32*)value = CTC_GLOBAL_ECMP_DLB_MODE_TCP;
        }
        else if (2 == field_val)
        {
            *(uint32*)value = CTC_GLOBAL_ECMP_DLB_MODE_ALL;
        }
        break;

    case CTC_GLOBAL_ECMP_REBALANCE_MODE:
        cmd = DRV_IOR(DlbEngineCtl_t, DlbEngineCtl_rebalanceMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        if (0 == field_val)
        {
            *(uint32*)value = CTC_GLOBAL_ECMP_REBALANCE_MODE_FIRST;
        }
        else if (1 == field_val)
        {
            *(uint32*)value = CTC_GLOBAL_ECMP_REBALANCE_MODE_PACKET;
        }
        else if (2 == field_val)
        {
            *(uint32*)value = CTC_GLOBAL_ECMP_REBALANCE_MODE_NORMAL;
        }
        break;

     case CTC_GLOBAL_ECMP_FLOW_AGING_INTERVAL:
        cmd = DRV_IOR(DlbEngineTimerCtl_t, DlbEngineTimerCtl_cfgRefDivAgingRstPulse_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32*)value = field_val / 250;
        break;

    case CTC_GLOBAL_ECMP_FLOW_INACTIVE_INTERVAL:
        cmd = DRV_IOR(DlbEngineTimerCtl_t, DlbEngineTimerCtl_cfgRefDivInactiveRstPulse_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32*)value = field_val << 3;
        break;

    case CTC_GLOBAL_ECMP_FLOW_PKT_INTERVAL:
        cmd = DRV_IOR(DlbEngineCtl_t, DlbEngineCtl_reorderPktIntervalMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        if (0 == field_val)
        {
            *(uint32*)value = 1024;
        }
        else if (1 == field_val)
        {
            *(uint32*)value = 256;
        }
        else if (2 == field_val)
        {
            *(uint32*)value = 8192;
        }
        else if (3 == field_val)
        {
            *(uint32*)value = 32768;
        }
        break;

    case CTC_GLOBAL_LINKAGG_FLOW_INACTIVE_INTERVAL:
        cmd = DRV_IOR(QLinkAggTimerCtl0_t, QLinkAggTimerCtl0_tsThreshold0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32*)value = field_val;
        break;

    case CTC_GLOBAL_STACKING_TRUNK_FLOW_INACTIVE_INTERVAL:
        cmd = DRV_IOR(QLinkAggTimerCtl1_t, QLinkAggTimerCtl1_tsThreshold1_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32*)value = field_val;
        break;

    case CTC_GLOBAL_DISCARD_TCP_SYN_0_PKT:
    case CTC_GLOBAL_DISCARD_TCP_NULL_PKT:
    case CTC_GLOBAL_DISCARD_TCP_XMAS_PKT:
    case CTC_GLOBAL_DISCARD_TCP_SYN_FIN_PKT:
    case CTC_GLOBAL_DISCARD_SAME_L4_PORT_PKT:
    case CTC_GLOBAL_DISCARD_ICMP_FRAG_PKT:
        CTC_ERROR_RETURN(_sys_goldengate_global_ctl_l4_check_en(lchip, type, ((uint32*)value), 0));
        break;

    case CTC_GLOBAL_ARP_MACDA_CHECK_EN:
    case CTC_GLOBAL_ARP_MACSA_CHECK_EN:
    case CTC_GLOBAL_ARP_IP_CHECK_EN:
    case CTC_GLOBAL_ARP_CHECK_FAIL_TO_CPU:
        CTC_ERROR_RETURN(_sys_goldengate_global_ctl_get_arp_check_en(lchip, type, ((uint32*)value)));
        break;

    case CTC_GLOBAL_WARMBOOT_CPU_RX_EN:
        for(chan_id = 0; chan_id < SYS_DMA_MAX_CHAN_NUM; chan_id++)
        {
            CTC_ERROR_RETURN(sys_goldengate_dma_get_chan_en(lchip, chan_id, &enable));
            if (enable)
            {
                field_val = 1;
                break;
            }
        }

        CTC_ERROR_RETURN(sys_goldengate_interrupt_get_group_en(lchip, &enable));
        if (enable)
        {
            field_val = 1;
        }

        if (0 == field_val)
        {
            *(bool *)value = FALSE;
        }
        else
        {
            *(bool *)value = TRUE;
        }
        break;

    case CTC_GLOBAL_FLOW_PROPERTY:
        cmd = DRV_IOR(IpeUserIdCtl_t, IpeUserIdCtl_vlanRangeMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        if (0 == field_val)
        {
            ((ctc_global_flow_property_t*)value)->igs_vlan_range_mode = CTC_GLOBAL_VLAN_RANGE_MODE_SCL;
        }
        else if (1 == field_val)
        {
            ((ctc_global_flow_property_t*)value)->igs_vlan_range_mode = CTC_GLOBAL_VLAN_RANGE_MODE_ACL;
        }
        else if (2 == field_val)
        {
            ((ctc_global_flow_property_t*)value)->igs_vlan_range_mode = CTC_GLOBAL_VLAN_RANGE_MODE_SHARE;
        }

        cmd = DRV_IOR(EpeNextHopCtl_t, EpeNextHopCtl_vlanRangeMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        if (0 == field_val)
        {
            ((ctc_global_flow_property_t*)value)->egs_vlan_range_mode = CTC_GLOBAL_VLAN_RANGE_MODE_SCL;
        }
        else if (1 == field_val)
        {
            ((ctc_global_flow_property_t*)value)->egs_vlan_range_mode = CTC_GLOBAL_VLAN_RANGE_MODE_ACL;
        }
        else if (2 == field_val)
        {
            ((ctc_global_flow_property_t*)value)->egs_vlan_range_mode = CTC_GLOBAL_VLAN_RANGE_MODE_SHARE;
        }

        break;

    case CTC_GLOBAL_CHIP_CAPABILITY:
        CTC_ERROR_RETURN(_sys_goldengate_global_get_chip_capability(lchip, (uint32*)value));
        break;

    case CTC_GLOBAL_ELOOP_USE_LOGIC_DESTPORT:
        /* get bit[1], total 16bits */
        sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));
        sys_goldengate_chip_get_device_info(lchip, &dev_info);
        if (dev_info.version_id > 1)
        {
            cmd = DRV_IOR(EpeHdrEditReserved2_t, EpeHdrEditReserved2_reserved_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        }
        *(bool *)value = CTC_IS_BIT_SET(field_val, 1)?TRUE:FALSE;
        break;

    case CTC_GLOBAL_ACL_PROPERTY:
        CTC_ERROR_RETURN(_sys_goldengate_get_glb_acl_property(lchip, (ctc_global_acl_property_t*)value));
        break;

    case CTC_GLOBAL_PIM_SNOOPING_MODE:

        cmd = DRV_IOR(IpeLookupCtl_t, IpeLookupCtl_pimSnoopingEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32 *)value = field_val ? 0 : 1;
        break;
    case CTC_GLOBAL_IPMC_PROPERTY:
        CTC_ERROR_RETURN(_sys_goldengate_get_glb_ipmc_property(lchip, (ctc_global_ipmc_property_t*)value));
        break;

    case CTC_GLOBAL_VXLAN_UDP_DEST_PORT:
        cmd = DRV_IOR(ParserLayer4AppCtl_t, ParserLayer4AppCtl_vxlanIpv4UdpDestPort_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32*)value = field_val;
        break;
    case CTC_GLOBAL_GENEVE_UDP_DEST_PORT:
        cmd = DRV_IOR(ParserLayer4AppCtl_t, ParserLayer4AppCtl_geneveVxlanUdpDestPort_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32*)value = field_val;
        break;

    case CTC_GLOBAL_PANEL_PORTS:
        _sys_goldengate_global_get_panel_ports(lchip,(ctc_global_panel_ports_t*)value);
        break;
    case CTC_GLOBAL_NH_FORCE_BRIDGE_DISABLE:
        cmd = DRV_IOR(EpeNextHopCtl_t, EpeNextHopCtl_forceBridgeL3Match_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(bool *)value = (field_val)?FALSE:TRUE;
        break;
    case CTC_GLOBAL_NH_MCAST_LOGIC_REP_EN:
        *(bool *)value = sys_goldengate_nh_is_ipmc_logic_rep_enable(lchip);
        break;
    case CTC_GLOBAL_ARP_VLAN_CLASS_EN:
        cmd = DRV_IOR(IpeUserIdCtl_t, IpeUserIdCtl_arpForceIpv4_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32*)value = field_val;
        break;
    case CTC_GLOBAL_IGS_RANDOM_LOG_SHIFT:
        cmd = DRV_IOR(IpeUserIdCtl_t, IpeUserIdCtl_randomLogShift_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32*)value = field_val;
        break;
    case CTC_GLOBAL_EGS_RANDOM_LOG_SHIFT:
        cmd = DRV_IOR(EpeNextHopCtl_t, EpeNextHopCtl_randomLogShift_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32*)value = field_val;
        break;
    case CTC_GLOBAL_OVERLAY_DECAP_MODE:
        CTC_MAX_VALUE_CHECK(p_decap_mode->scl_id, 1);
        cmd = DRV_IOR(IpeUserIdCtl_t, (p_decap_mode->scl_id)? IpeUserIdCtl_vxlanTunnelHash2LkpMode_f: IpeUserIdCtl_vxlanTunnelHash1LkpMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        p_decap_mode->vxlan_mode = field_val ? 0:1;
        cmd = DRV_IOR(IpeUserIdCtl_t, (p_decap_mode->scl_id)? IpeUserIdCtl_nvgreTunnelHash2LkpMode_f: IpeUserIdCtl_nvgreTunnelHash1LkpMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        p_decap_mode->nvgre_mode = field_val ? 0:1;
        break;
    case CTC_GLOBAL_EGS_STK_ACL_DIS:
        cmd = DRV_IOR(EpeAclQosCtl_t, EpeAclQosCtl_stackingDisableAcl_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(bool*)value = field_val;
        break;
    default:
        return CTC_E_NOT_SUPPORT;

    }

    return CTC_E_NONE;
}

int32 sys_goldengate_wb_sync_register_cb(uint8 lchip, uint8 module, sys_wb_sync_fn fn)
{
    if (NULL == p_gg_register_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    CTC_MAX_VALUE_CHECK(module, CTC_FEATURE_MAX - 1);
    p_gg_register_master[lchip]->wb_sync_cb[module] = fn;

    return CTC_E_NONE;
}


int32
sys_goldengate_global_set_chip_capability(uint8 lchip, ctc_global_capability_type_t type, uint32 value)
{

    CTC_MAX_VALUE_CHECK(type, CTC_GLOBAL_CAPABILITY_MAX - 1);
    p_gg_register_master[lchip]->chip_capability[type] = value;

    return CTC_E_NONE;
}

int32
sys_goldengate_global_set_oversub_pdu(uint8 lchip, mac_addr_t macda, mac_addr_t macda_mask, uint16 eth_type, uint16 eth_type_mask, uint8 with_vlan)
{
    uint32 field_val = 0;
    uint32 cmd = 0;
    uint8 index = 0;
    uint8 pdu_en = 0;
    hw_mac_addr_t hw_mac = { 0 };
    mac_addr_t macda_mask0 = { 0 };

    NetRxBpduCfg0_m bpdu_cfg;
    NetRxBpduCheckCtl0_m bpdu_check_ctl;


    sal_memset(&bpdu_cfg, 0, sizeof(NetRxBpduCfg0_m));
    sal_memset(&bpdu_check_ctl, 0, sizeof(NetRxBpduCheckCtl0_m));

    /* Set NetRxBpduCheckCtl */
    if (sal_memcmp(macda_mask, macda_mask0, CTC_ETH_ADDR_LEN))
    {
        pdu_en = 1;
        SYS_GOLDENGATE_SET_HW_MAC(hw_mac, macda);
        SetNetRxBpduCheckCtl0(V, reservedMacDaValue0Low_f, &bpdu_check_ctl, hw_mac[0]);  /*mac low 32bit*/
        SetNetRxBpduCheckCtl0(V, reservedMacDaValue0High_f, &bpdu_check_ctl, hw_mac[1]);  /*mac high 16bit*/

        for (index = 0; index < 6; index++)
        {
            macda_mask0[index] = ~(macda_mask[index]);
        }

        SYS_GOLDENGATE_SET_HW_MAC(hw_mac, macda_mask0);
        SetNetRxBpduCheckCtl0(V, reservedMacDaMask0Low_f, &bpdu_check_ctl, hw_mac[0]);
        SetNetRxBpduCheckCtl0(V, reservedMacDaMask0High_f, &bpdu_check_ctl, hw_mac[1]);

        SetNetRxBpduCheckCtl0(V, reservedMacDaValid0_f, &bpdu_check_ctl, 1);

        SetNetRxBpduCfg0(V, macDaChkEn_f, &bpdu_cfg, 1);
    }
    else
    {
        SetNetRxBpduCfg0(V, macDaChkEn_f, &bpdu_cfg, 0);
    }

    if (0 != eth_type_mask)
    {
        pdu_en = 1;
        field_val = eth_type;
        SetNetRxBpduCheckCtl0(V, reservedEthTypeValue0_f, &bpdu_check_ctl, field_val);
        field_val = ~eth_type_mask;
        SetNetRxBpduCheckCtl0(V, reservedEthTypeMask0_f, &bpdu_check_ctl, field_val);

        SetNetRxBpduCheckCtl0(V, reservedEthTypeValid0_f, &bpdu_check_ctl, 1);
        SetNetRxBpduCfg0(V, ethTypeChkEn_f, &bpdu_cfg, 1);
    }
    else
    {
        SetNetRxBpduCfg0(V, ethTypeChkEn_f, &bpdu_cfg, 0);
    }

    cmd = DRV_IOW(NetRxBpduCheckCtl0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &bpdu_check_ctl));

    cmd = DRV_IOW(NetRxBpduCheckCtl1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &bpdu_check_ctl));

    /* Set NetRxBpduCfg */
    field_val = with_vlan ? 1 : 0;
    SetNetRxBpduCfg0(V, vlanTagEn_f, &bpdu_cfg, field_val);

    for (index = 0; index < 64; index++)
    {
        cmd = DRV_IOW(NetRxBpduCfg0_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &bpdu_cfg));

        cmd = DRV_IOW(NetRxBpduCfg1_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &bpdu_cfg));
    }

    /* Set NetRxCtl */
    field_val = with_vlan ? 0 : 1;
    cmd = DRV_IOW(NetRxCtl0_t, NetRxCtl0_cfgBpdu64Bytes_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(NetRxCtl1_t, NetRxCtl1_cfgBpdu64Bytes_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = pdu_en;
    cmd = DRV_IOW(NetRxCtl0_t, NetRxCtl0_bpduChkEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(NetRxCtl1_t, NetRxCtl1_bpduChkEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_goldengate_global_check_acl_memory(uint8 lchip, ctc_direction_t dir, uint32 value)
{
    uint32 cmd = 0;
    int32 ret = CTC_E_NONE;
    uint32 field_value = 0;
    uint32 field_id[4] = {FlowTcamLookupCtl_ingressAcl0Bitmap_f,FlowTcamLookupCtl_ingressAcl1Bitmap_f,FlowTcamLookupCtl_ingressAcl2Bitmap_f,FlowTcamLookupCtl_ingressAcl3Bitmap_f};
    uint8 acl_id = 0;

    if (CTC_EGRESS == dir)
    {
        cmd = DRV_IOR(FlowTcamLookupCtl_t, FlowTcamLookupCtl_egressAcl0Bitmap_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        if (CTC_IS_BIT_SET(value, 0) && (0 == field_value))
        {
            ret = CTC_E_INVALID_CONFIG;
        }
    }
    else
    {
        for (acl_id = 0; acl_id < 4; acl_id++)
        {
            cmd = DRV_IOR(FlowTcamLookupCtl_t, field_id[acl_id]);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
            if (CTC_IS_BIT_SET(value, acl_id) && (0 == field_value))
            {
                ret = CTC_E_INVALID_CONFIG;
                break;
            }
        }
    }

    return ret;
}


