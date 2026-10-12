#ifndef _CTC_GOLDENGATE_DKIT_DRV_H_
#define _CTC_GOLDENGATE_DKIT_DRV_H_


#ifdef __cplusplus
extern "C" {
#endif


#include "sal.h"


#define DKIT_BUS_GET_INFOPTR(bus_id)        (&gg_dkit_bus_list[bus_id])
#define DKIT_BUS_GET_INFO(bus_id)           (gg_dkit_bus_list[bus_id])
#define DKIT_BUS_GET_NAME(bus_id)           (DKIT_BUS_GET_INFO(bus_id).bus_name)
#define DKIT_BUS_GET_FIFO_ID(bus_id)        (DKIT_BUS_GET_INFO(bus_id).bus_fifo_id)


#define DKIT_MODULE_GET_INFOPTR(mod_id)        (&gg_dkit_modules_list[mod_id])
#define DKIT_MODULE_GET_INFO(mod_id)           (gg_dkit_modules_list[mod_id])

struct dkit_bus_s
{
    char *bus_name;
    tbls_id_t bus_fifo_id;
    uint32 max_index;

    uint16 entry_size;
    uint16 num_fields;
    fields_t *per_fields;
};
typedef struct dkit_bus_s dkit_bus_t;

/* module data stucture */
struct dkit_modules_s
{
    char        *master_name;
    char        *module_name;
    uint32      inst_num;
    tbls_id_t    *reg_list_id;
    uint32      id_num;
    tbls_id_t    *reg_list_id_def;
    uint32      id_num_def;
};
typedef struct dkit_modules_s dkit_modules_t;

typedef enum dkit_fld_id_e {
    DbgDlbEngineInfo_rebalance_f                                = 0,
    DbgDlbEngineInfo_rebalanceCount_f                           = 1,
    DbgDlbEngineInfo_allSelectLinkDown_f                        = 2,
    DbgDlbEngineInfo_dlbEn_f                                    = 3,
    DbgDlbEngineInfo_resilientHashEn_f                          = 4,
    DbgDlbEngineInfo_rrEn_f                                     = 5,
    DbgDlbEngineInfo_staticEcmpPathOffset_f                     = 6,
    DbgDlbEngineInfo_flowId_f                                   = 7,
    DbgDlbEngineInfo_ecmpGroupId_f                              = 8,
    DbgDlbEngineInfo_dsFwdPtrValid_f                            = 9,
    DbgDlbEngineInfo_dsFwdPtr_f                                 = 10,
    DbgDlbEngineInfo_lookupHit_f                                = 11,
    DbgDlbEngineInfo_hashCollision_f                            = 12,
    DbgDlbEngineInfo_hitEntryOffset_f                           = 13,
    DbgDlbEngineInfo_freeOffset_f                               = 14,
    DbgDlbEngineInfo_isUpdateOperation_f                        = 15,
    DbgDlbEngineInfo_isInsertOperation_f                        = 16,
    DbgDlbEngineInfo_memberSelChangeForceReb_f                  = 17,
    DbgDlbEngineInfo_disableRebalance_f                         = 18,
    DbgDlbEngineInfo_selectedPathIndex_f                        = 19,
    DbgDlbEngineInfo_rebalanceReady_f                           = 20,
    DbgDlbEngineInfo_oldChanHisByteCount_f                      = 21,
    DbgDlbEngineInfo_flowByteCount_f                            = 22,
    DbgDlbEngineInfo_valid_f                                    = 23,

    DbgEfdEngineInfo_flowId_f                                   = 0,
    DbgEfdEngineInfo_efdHash0_f                                 = 1,
    DbgEfdEngineInfo_efdHash1_f                                 = 2,
    DbgEfdEngineInfo_efdHash2_f                                 = 3,
    DbgEfdEngineInfo_efdHash3_f                                 = 4,
    DbgEfdEngineInfo_packetLength_f                             = 5,
    DbgEfdEngineInfo_isNewElephant_f                            = 6,
    DbgEfdEngineInfo_dsFwdPtr_f                                 = 7,
    DbgEfdEngineInfo_isElephant_f                               = 8,
    DbgEfdEngineInfo_hit_f                                      = 9,
    DbgEfdEngineInfo_hashConflict_f                             = 10,
    DbgEfdEngineInfo_bypassDetectProcess_f                      = 11,
    DbgEfdEngineInfo_doInsert_f                                 = 12,
    DbgEfdEngineInfo_valid_f                                    = 13,

    DbgEgrXcOamHash0EngineFromEpeNhpInfo_lookupResultValid0_f   = 0,
    DbgEgrXcOamHash0EngineFromEpeNhpInfo_keyIndex0_f            = 1,
    DbgEgrXcOamHash0EngineFromEpeNhpInfo_lookupResultValid1_f   = 2,
    DbgEgrXcOamHash0EngineFromEpeNhpInfo_keyIndex1_f            = 3,
    DbgEgrXcOamHash0EngineFromEpeNhpInfo_resultValid_f          = 4,
    DbgEgrXcOamHash0EngineFromEpeNhpInfo_defaultEntryValid_f    = 5,
    DbgEgrXcOamHash0EngineFromEpeNhpInfo_egressXcOamHashConflict_f= 6,
    DbgEgrXcOamHash0EngineFromEpeNhpInfo_keyIndex_f             = 7,
    DbgEgrXcOamHash0EngineFromEpeNhpInfo_valid_f                = 8,

    DbgEgrXcOamHash1EngineFromEpeNhpInfo_lookupResultValid0_f   = 0,
    DbgEgrXcOamHash1EngineFromEpeNhpInfo_keyIndex0_f            = 1,
    DbgEgrXcOamHash1EngineFromEpeNhpInfo_lookupResultValid1_f   = 2,
    DbgEgrXcOamHash1EngineFromEpeNhpInfo_keyIndex1_f            = 3,
    DbgEgrXcOamHash1EngineFromEpeNhpInfo_resultValid_f          = 4,
    DbgEgrXcOamHash1EngineFromEpeNhpInfo_defaultEntryValid_f    = 5,
    DbgEgrXcOamHash1EngineFromEpeNhpInfo_egressXcOamHashConflict_f= 6,
    DbgEgrXcOamHash1EngineFromEpeNhpInfo_keyIndex_f             = 7,
    DbgEgrXcOamHash1EngineFromEpeNhpInfo_valid_f                = 8,

    DbgEgrXcOamHashEngineFromEpeOam0Info_lookupResultValid0_f   = 0,
    DbgEgrXcOamHashEngineFromEpeOam0Info_keyIndex0_f            = 1,
    DbgEgrXcOamHashEngineFromEpeOam0Info_lookupResultValid1_f   = 2,
    DbgEgrXcOamHashEngineFromEpeOam0Info_keyIndex1_f            = 3,
    DbgEgrXcOamHashEngineFromEpeOam0Info_resultValid_f          = 4,
    DbgEgrXcOamHashEngineFromEpeOam0Info_defaultEntryValid_f    = 5,
    DbgEgrXcOamHashEngineFromEpeOam0Info_egressXcOamHashConflict_f= 6,
    DbgEgrXcOamHashEngineFromEpeOam0Info_keyIndex_f             = 7,
    DbgEgrXcOamHashEngineFromEpeOam0Info_valid_f                = 8,

    DbgEgrXcOamHashEngineFromEpeOam1Info_lookupResultValid0_f   = 0,
    DbgEgrXcOamHashEngineFromEpeOam1Info_keyIndex0_f            = 1,
    DbgEgrXcOamHashEngineFromEpeOam1Info_lookupResultValid1_f   = 2,
    DbgEgrXcOamHashEngineFromEpeOam1Info_keyIndex1_f            = 3,
    DbgEgrXcOamHashEngineFromEpeOam1Info_resultValid_f          = 4,
    DbgEgrXcOamHashEngineFromEpeOam1Info_defaultEntryValid_f    = 5,
    DbgEgrXcOamHashEngineFromEpeOam1Info_egressXcOamHashConflict_f= 6,
    DbgEgrXcOamHashEngineFromEpeOam1Info_keyIndex_f             = 7,
    DbgEgrXcOamHashEngineFromEpeOam1Info_valid_f                = 8,

    DbgEgrXcOamHashEngineFromEpeOam2Info_lookupResultValid0_f   = 0,
    DbgEgrXcOamHashEngineFromEpeOam2Info_keyIndex0_f            = 1,
    DbgEgrXcOamHashEngineFromEpeOam2Info_lookupResultValid1_f   = 2,
    DbgEgrXcOamHashEngineFromEpeOam2Info_keyIndex1_f            = 3,
    DbgEgrXcOamHashEngineFromEpeOam2Info_resultValid_f          = 4,
    DbgEgrXcOamHashEngineFromEpeOam2Info_defaultEntryValid_f    = 5,
    DbgEgrXcOamHashEngineFromEpeOam2Info_egressXcOamHashConflict_f= 6,
    DbgEgrXcOamHashEngineFromEpeOam2Info_keyIndex_f             = 7,
    DbgEgrXcOamHashEngineFromEpeOam2Info_valid_f                = 8,

    DbgEgrXcOamHashEngineFromIpeOam0Info_lookupResultValid0_f   = 0,
    DbgEgrXcOamHashEngineFromIpeOam0Info_keyIndex0_f            = 1,
    DbgEgrXcOamHashEngineFromIpeOam0Info_lookupResultValid1_f   = 2,
    DbgEgrXcOamHashEngineFromIpeOam0Info_keyIndex1_f            = 3,
    DbgEgrXcOamHashEngineFromIpeOam0Info_resultValid_f          = 4,
    DbgEgrXcOamHashEngineFromIpeOam0Info_defaultEntryValid_f    = 5,
    DbgEgrXcOamHashEngineFromIpeOam0Info_egressXcOamHashConflict_f= 6,
    DbgEgrXcOamHashEngineFromIpeOam0Info_keyIndex_f             = 7,
    DbgEgrXcOamHashEngineFromIpeOam0Info_valid_f                = 8,

    DbgEgrXcOamHashEngineFromIpeOam1Info_lookupResultValid0_f   = 0,
    DbgEgrXcOamHashEngineFromIpeOam1Info_keyIndex0_f            = 1,
    DbgEgrXcOamHashEngineFromIpeOam1Info_lookupResultValid1_f   = 2,
    DbgEgrXcOamHashEngineFromIpeOam1Info_keyIndex1_f            = 3,
    DbgEgrXcOamHashEngineFromIpeOam1Info_resultValid_f          = 4,
    DbgEgrXcOamHashEngineFromIpeOam1Info_defaultEntryValid_f    = 5,
    DbgEgrXcOamHashEngineFromIpeOam1Info_egressXcOamHashConflict_f= 6,
    DbgEgrXcOamHashEngineFromIpeOam1Info_keyIndex_f             = 7,
    DbgEgrXcOamHashEngineFromIpeOam1Info_valid_f                = 8,

    DbgEgrXcOamHashEngineFromIpeOam2Info_lookupResultValid0_f   = 0,
    DbgEgrXcOamHashEngineFromIpeOam2Info_keyIndex0_f            = 1,
    DbgEgrXcOamHashEngineFromIpeOam2Info_lookupResultValid1_f   = 2,
    DbgEgrXcOamHashEngineFromIpeOam2Info_keyIndex1_f            = 3,
    DbgEgrXcOamHashEngineFromIpeOam2Info_resultValid_f          = 4,
    DbgEgrXcOamHashEngineFromIpeOam2Info_defaultEntryValid_f    = 5,
    DbgEgrXcOamHashEngineFromIpeOam2Info_egressXcOamHashConflict_f= 6,
    DbgEgrXcOamHashEngineFromIpeOam2Info_keyIndex_f             = 7,
    DbgEgrXcOamHashEngineFromIpeOam2Info_valid_f                = 8,

    DbgEpeAclInfo_discard_f                                     = 0,
    DbgEpeAclInfo_discardType_f                                 = 1,
    DbgEpeAclInfo_exceptionEn_f                                 = 2,
    DbgEpeAclInfo_exceptionIndex_f                              = 3,
    DbgEpeAclInfo_mplsLmEn0_f                                   = 4,
    DbgEpeAclInfo_mplsLmEn1_f                                   = 5,
    DbgEpeAclInfo_mplsLmEn2_f                                   = 6,
    DbgEpeAclInfo_mplsSectionLmValid_f                          = 7,
    DbgEpeAclInfo_rxOamType_f                                   = 8,
    DbgEpeAclInfo_ipfixFlowLookupEn_f                           = 9,
    DbgEpeAclInfo_portFlowHashType_f                            = 10,
    DbgEpeAclInfo_ipfixFlowLkpDisable_f                         = 11,
    DbgEpeAclInfo_aclEn_f                                       = 12,
    DbgEpeAclInfo_aclType_f                                     = 13,
    DbgEpeAclInfo_aclLabel_f                                    = 14,
    DbgEpeAclInfo_aclLookupType_f                               = 15,
    DbgEpeAclInfo_sclKeyType_f                                  = 16,
    DbgEpeAclInfo_flowHasKeyType_f                              = 17,
    DbgEpeAclInfo_flowHashFieldSel_f                            = 18,
    DbgEpeAclInfo_egressXcOamKeyType0_f                         = 19,
    DbgEpeAclInfo_egressXcOamKeyType1_f                         = 20,
    DbgEpeAclInfo_egressXcOamKeyType2_f                         = 21,
    DbgEpeAclInfo_resultValid_f                                 = 22,
    DbgEpeAclInfo_valid_f                                       = 23,

    DbgEpeClassificationInfo_discard_f                          = 0,
    DbgEpeClassificationInfo_discardType_f                      = 1,
    DbgEpeClassificationInfo_exceptionEn_f                      = 2,
    DbgEpeClassificationInfo_exceptionIndex_f                   = 3,
    DbgEpeClassificationInfo_color_f                            = 4,
    DbgEpeClassificationInfo_policerValid0_f                    = 5,
    DbgEpeClassificationInfo_policerPtr0_f                      = 6,
    DbgEpeClassificationInfo_policerValid1_f                    = 7,
    DbgEpeClassificationInfo_policerPtr1_f                      = 8,
    DbgEpeClassificationInfo_markDrop_f                         = 9,
    DbgEpeClassificationInfo_valid_f                            = 10,

    DbgEpeEgressEditInfo_discard_f                              = 0,
    DbgEpeEgressEditInfo_discardType_f                          = 1,
    DbgEpeEgressEditInfo_exceptionEn_f                          = 2,
    DbgEpeEgressEditInfo_exceptionIndex_f                       = 3,
    DbgEpeEgressEditInfo_nhpOuterEditLocation_f                 = 4,
    DbgEpeEgressEditInfo_innerEditExist_f                       = 5,
    DbgEpeEgressEditInfo_innerEditPtrType_f                     = 6,
    DbgEpeEgressEditInfo_nhpInnerEditPtr_f                      = 7,
    DbgEpeEgressEditInfo_outerEditPipe1Exist_f                  = 8,
    DbgEpeEgressEditInfo_outerEditPipe1Type_f                   = 9,
    DbgEpeEgressEditInfo_nhpOuterEditPtr_f                      = 10,
    DbgEpeEgressEditInfo_outerEditPipe2Exist_f                  = 11,
    DbgEpeEgressEditInfo_outerEditPipe3Exist_f                  = 12,
    DbgEpeEgressEditInfo_outerEditPipe3Type_f                   = 13,
    DbgEpeEgressEditInfo_l3EditDiscard_f                        = 14,
    DbgEpeEgressEditInfo_l3EditDiscardType_f                    = 15,
    DbgEpeEgressEditInfo_l3RewriteType_f                        = 16,
    DbgEpeEgressEditInfo_l3EditMplsPwExist_f                    = 17,
    DbgEpeEgressEditInfo_l3EditMplsLspExist_f                   = 18,
    DbgEpeEgressEditInfo_l3EditMplsSpmeExist_f                  = 19,
    DbgEpeEgressEditInfo_l3EditDsTypeViolate_f                  = 20,
    DbgEpeEgressEditInfo_innerL2EditDiscard_f                   = 21,
    DbgEpeEgressEditInfo_innerL2EditDiscardType_f               = 22,
    DbgEpeEgressEditInfo_innerL2RewriteType_f                   = 23,
    DbgEpeEgressEditInfo_innerL2EditDsTypeViolate_f             = 24,
    DbgEpeEgressEditInfo_l2EditDiscard_f                        = 25,
    DbgEpeEgressEditInfo_l2EditDiscardType_f                    = 26,
    DbgEpeEgressEditInfo_l2RewriteType_f                        = 27,
    DbgEpeEgressEditInfo_l2EditDsTypeViolate_f                  = 28,
    DbgEpeEgressEditInfo_valid_f                                = 29,

    DbgEpeHdrAdjInfo_channelId_f                                = 0,
    DbgEpeHdrAdjInfo_packetHeaderEn_f                           = 1,
    DbgEpeHdrAdjInfo_packetHeaderEnEgress_f                     = 2,
    DbgEpeHdrAdjInfo_bypassAll_f                                = 3,
    DbgEpeHdrAdjInfo_discard_f                                  = 4,
    DbgEpeHdrAdjInfo_discardType_f                              = 5,
    DbgEpeHdrAdjInfo_microBurstValid_f                          = 6,
    DbgEpeHdrAdjInfo_isSendtoCpuEn_f                            = 7,
    DbgEpeHdrAdjInfo_packetLengthIsTrue_f                       = 8,
    DbgEpeHdrAdjInfo_localPhyPort_f                             = 9,
    DbgEpeHdrAdjInfo_nextHopPtr_f                               = 10,
    DbgEpeHdrAdjInfo_parserOffset_f                             = 11,
    DbgEpeHdrAdjInfo_packetLengthAdjustType_f                   = 12,
    DbgEpeHdrAdjInfo_packetLengthAdjust_f                       = 13,
    DbgEpeHdrAdjInfo_sTagAction_f                               = 14,
    DbgEpeHdrAdjInfo_cTagAction_f                               = 15,
    DbgEpeHdrAdjInfo_packetOffset_f                             = 16,
    DbgEpeHdrAdjInfo_aclDscpValid_f                             = 17,
    DbgEpeHdrAdjInfo_aclDscp_f                                  = 18,
    DbgEpeHdrAdjInfo_newIpChecksum_f                            = 19,
    DbgEpeHdrAdjInfo_isidValid_f                                = 20,
    DbgEpeHdrAdjInfo_muxLengthType_f                            = 21,
    DbgEpeHdrAdjInfo_cvlanTagOperationValid_f                   = 22,
    DbgEpeHdrAdjInfo_svlanTagOperationValid_f                   = 23,
    DbgEpeHdrAdjInfo_debugOpType_f                              = 24,
    DbgEpeHdrAdjInfo_valid_f                                    = 25,

    DbgEpeHdrEditInfo_discard_f                                 = 0,
    DbgEpeHdrEditInfo_discardType_f                             = 1,
    DbgEpeHdrEditInfo_exceptionEn_f                             = 2,
    DbgEpeHdrEditInfo_exceptionIndex_f                          = 3,
    DbgEpeHdrEditInfo_flexEditEn_f                              = 4,
    DbgEpeHdrEditInfo_shareType_f                               = 5,
    DbgEpeHdrEditInfo_txDmEn_f                                  = 6,
    DbgEpeHdrEditInfo_congestionValid_f                         = 7,
    DbgEpeHdrEditInfo_newTtlPacketType_f                        = 8,
    DbgEpeHdrEditInfo_newTtlValid_f                             = 9,
    DbgEpeHdrEditInfo_newDscpValid_f                            = 10,
    DbgEpeHdrEditInfo_newIpChecksumValid_f                      = 11,
    DbgEpeHdrEditInfo_newIpDaValid_f                            = 12,
    DbgEpeHdrEditInfo_newL4DestPortValid_f                      = 13,
    DbgEpeHdrEditInfo_newL4ChecksumValid_f                      = 14,
    DbgEpeHdrEditInfo_stripOffset_f                             = 15,
    DbgEpeHdrEditInfo_packetHeaderEn_f                          = 16,
    DbgEpeHdrEditInfo_fromFabric_f                              = 17,
    DbgEpeHdrEditInfo_loopbackEn_f                              = 18,
    DbgEpeHdrEditInfo_destMuxPortType_f                         = 19,
    DbgEpeHdrEditInfo_ipfixFlowLookupEn_f                       = 20,
    DbgEpeHdrEditInfo_newIpSaValid_f                            = 21,
    DbgEpeHdrEditInfo_newL4SourcePortValid_f                    = 22,
    DbgEpeHdrEditInfo_bypassEdit_f                              = 23,
    DbgEpeHdrEditInfo_l3NewHeaderLen_f                          = 24,
    DbgEpeHdrEditInfo_l2NewHeaderLen_f                          = 25,
    DbgEpeHdrEditInfo_sgmacStripHeader_f                        = 26,
    DbgEpeHdrEditInfo_netTxDiscard_f                            = 27,
    DbgEpeHdrEditInfo_microBurstValid_f                         = 28,
    DbgEpeHdrEditInfo_exceptionVector_f                         = 29,
    DbgEpeHdrEditInfo_completeDiscard_f                         = 30,
    DbgEpeHdrEditInfo_valid_f                                   = 31,

    DbgEpeInnerL2EditInfo_discard_f                             = 0,
    DbgEpeInnerL2EditInfo_discardType_f                         = 1,
    DbgEpeInnerL2EditInfo_exceptionEn_f                         = 2,
    DbgEpeInnerL2EditInfo_exceptionIndex_f                      = 3,
    DbgEpeInnerL2EditInfo_svlanTagOperation_f                   = 4,
    DbgEpeInnerL2EditInfo_l2NewSvlanTag_f                       = 5,
    DbgEpeInnerL2EditInfo_newIpDaValid_f                        = 6,
    DbgEpeInnerL2EditInfo_newL4DestPortValid_f                  = 7,
    DbgEpeInnerL2EditInfo_newMacDaValid_f                       = 8,
    DbgEpeInnerL2EditInfo_isInnerL2EthSwapOp_f                  = 9,
    DbgEpeInnerL2EditInfo_isInnerL2EthAddOp_f                   = 10,
    DbgEpeInnerL2EditInfo_isInnerDsLiteOp_f                     = 11,
    DbgEpeInnerL2EditInfo_bypassInnerEdit_f                     = 12,
    DbgEpeInnerL2EditInfo_valid_f                               = 13,

    DbgEpeL3EditInfo_discard_f                                  = 0,
    DbgEpeL3EditInfo_discardType_f                              = 1,
    DbgEpeL3EditInfo_exceptionEn_f                              = 2,
    DbgEpeL3EditInfo_exceptionIndex_f                           = 3,
    DbgEpeL3EditInfo_l3EditDisable_f                            = 4,
    DbgEpeL3EditInfo_loopbackEn_f                               = 5,
    DbgEpeL3EditInfo_l3RewriteType_f                            = 6,
    DbgEpeL3EditInfo_label0Valid_f                              = 7,
    DbgEpeL3EditInfo_label1Valid_f                              = 8,
    DbgEpeL3EditInfo_label2Valid_f                              = 9,
    DbgEpeL3EditInfo_isLspLabelEntropy0_f                       = 10,
    DbgEpeL3EditInfo_isLspLabelEntropy1_f                       = 11,
    DbgEpeL3EditInfo_isLspLabelEntropy2_f                       = 12,
    DbgEpeL3EditInfo_isPwLabelEntropy0_f                        = 13,
    DbgEpeL3EditInfo_isPwLabelEntropy1_f                        = 14,
    DbgEpeL3EditInfo_isPwLabelEntropy2_f                        = 15,
    DbgEpeL3EditInfo_cwLabelValid_f                             = 16,
    DbgEpeL3EditInfo_flexEditL3Type_f                           = 17,
    DbgEpeL3EditInfo_flexEditL4Type_f                           = 18,
    DbgEpeL3EditInfo_isEnd_f                                    = 19,
    DbgEpeL3EditInfo_len_f                                      = 20,
    DbgEpeL3EditInfo_valid_f                                    = 21,

    DbgEpeNextHopInfo_discard_f                                 = 0,
    DbgEpeNextHopInfo_discardType_f                             = 1,
    DbgEpeNextHopInfo_payloadOperation_f                        = 2,
    DbgEpeNextHopInfo_editBypass_f                              = 3,
    DbgEpeNextHopInfo_innerEditHashEn_f                         = 4,
    DbgEpeNextHopInfo_outerEditHashEn_f                         = 5,
    DbgEpeNextHopInfo_etherOamDiscard_f                         = 6,
    DbgEpeNextHopInfo_isVxlanRouteOp_f                          = 7,
    DbgEpeNextHopInfo_exceptionEn_f                             = 8,
    DbgEpeNextHopInfo_exceptionIndex_f                          = 9,
    DbgEpeNextHopInfo_portReflectiveDiscard_f                   = 10,
    DbgEpeNextHopInfo_interfaceId_f                             = 11,
    DbgEpeNextHopInfo_destVlanPtr_f                             = 12,
    DbgEpeNextHopInfo_mcast_f                                   = 13,
    DbgEpeNextHopInfo_routeNoL2Edit_f                           = 14,
    DbgEpeNextHopInfo_outputSvlanIdValid_f                      = 15,
    DbgEpeNextHopInfo_outputSvlanId_f                           = 16,
    DbgEpeNextHopInfo_logicDestPort_f                           = 17,
    DbgEpeNextHopInfo_outputCvlanIdValid_f                      = 18,
    DbgEpeNextHopInfo_outputCvlanId_f                           = 19,
    DbgEpeNextHopInfo__priority_f                               = 20,
    DbgEpeNextHopInfo_color_f                                   = 21,
    DbgEpeNextHopInfo_l3EditDisable_f                           = 22,
    DbgEpeNextHopInfo_l2EditDisable_f                           = 23,
    DbgEpeNextHopInfo_isEnd_f                                   = 24,
    DbgEpeNextHopInfo_bypassAll_f                               = 25,
    DbgEpeNextHopInfo_nhpIs8w_f                                 = 26,
    DbgEpeNextHopInfo_portIsolateValid_f                        = 27,
    DbgEpeNextHopInfo_lookup1Valid_f                            = 28,
    DbgEpeNextHopInfo_vlanHash1Type_f                           = 29,
    DbgEpeNextHopInfo_lookup2Valid_f                            = 30,
    DbgEpeNextHopInfo_vlanHash2Type_f                           = 31,
    DbgEpeNextHopInfo_vlanXlateResult1Valid_f                   = 32,
    DbgEpeNextHopInfo_vlanXlateResult2Valid_f                   = 33,
    DbgEpeNextHopInfo_hashPort_f                                = 34,
    DbgEpeNextHopInfo_isLabel_f                                 = 35,
    DbgEpeNextHopInfo_svlanId_f                                 = 36,
    DbgEpeNextHopInfo_stagCos_f                                 = 37,
    DbgEpeNextHopInfo_cvlanId_f                                 = 38,
    DbgEpeNextHopInfo_ctagCos_f                                 = 39,
    DbgEpeNextHopInfo_vlanXlateVlanRangeValid_f                 = 40,
    DbgEpeNextHopInfo_vlanXlateVlanRangeMax_f                   = 41,
    DbgEpeNextHopInfo_aclVlanRangeValid_f                       = 42,
    DbgEpeNextHopInfo_aclVlanRangeMax_f                         = 43,
    DbgEpeNextHopInfo_innerEditPtrOffset_f                      = 44,
    DbgEpeNextHopInfo_outerEditPtrOffset_f                      = 45,
    DbgEpeNextHopInfo_qosDomain_f                               = 46,
    DbgEpeNextHopInfo_valid_f                                   = 47,

    DbgEpeOamInfo_discard_f                                     = 0,
    DbgEpeOamInfo_discardType_f                                 = 1,
    DbgEpeOamInfo_exceptionEn_f                                 = 2,
    DbgEpeOamInfo_exceptionIndex_f                              = 3,
    DbgEpeOamInfo_oamResultValid_f                              = 4,
    DbgEpeOamInfo_oamHashConflict_f                             = 5,
    DbgEpeOamInfo_etherLmValid_f                                = 6,
    DbgEpeOamInfo_mplsLmValid_f                                 = 7,
    DbgEpeOamInfo_lmStatsEn0_f                                  = 8,
    DbgEpeOamInfo_lmStatsEn1_f                                  = 9,
    DbgEpeOamInfo_lmStatsEn2_f                                  = 10,
    DbgEpeOamInfo_lmStatsIndex_f                                = 11,
    DbgEpeOamInfo_lmStatsPtr0_f                                 = 12,
    DbgEpeOamInfo_lmStatsPtr1_f                                 = 13,
    DbgEpeOamInfo_lmStatsPtr2_f                                 = 14,
    DbgEpeOamInfo_oamDestChipId_f                               = 15,
    DbgEpeOamInfo_mepIndex_f                                    = 16,
    DbgEpeOamInfo_mepEn_f                                       = 17,
    DbgEpeOamInfo_mipEn_f                                       = 18,
    DbgEpeOamInfo_tempRxOamType_f                               = 19,
    DbgEpeOamInfo_valid_f                                       = 20,

    DbgEpeOuterL2EditInfo_discard_f                             = 0,
    DbgEpeOuterL2EditInfo_discardType_f                         = 1,
    DbgEpeOuterL2EditInfo_exceptionEn_f                         = 2,
    DbgEpeOuterL2EditInfo_exceptionIndex_f                      = 3,
    DbgEpeOuterL2EditInfo_l2EditDisable_f                       = 4,
    DbgEpeOuterL2EditInfo_routeNoL2Edit_f                       = 5,
    DbgEpeOuterL2EditInfo_destMuxPortType_f                     = 6,
    DbgEpeOuterL2EditInfo_newMacSaValid_f                       = 7,
    DbgEpeOuterL2EditInfo_newMacDaValid_f                       = 8,
    DbgEpeOuterL2EditInfo_portMacSaEn_f                         = 9,
    DbgEpeOuterL2EditInfo_loopbackEn_f                          = 10,
    DbgEpeOuterL2EditInfo_svlanTagOperation_f                   = 11,
    DbgEpeOuterL2EditInfo_cvlanTagOperation_f                   = 12,
    DbgEpeOuterL2EditInfo_l2RewriteType_f                       = 13,
    DbgEpeOuterL2EditInfo_isEnd_f                               = 14,
    DbgEpeOuterL2EditInfo_len_f                                 = 15,
    DbgEpeOuterL2EditInfo_valid_f                               = 16,

    DbgEpePayLoadInfo_discard_f                                 = 0,
    DbgEpePayLoadInfo_discardType_f                             = 1,
    DbgEpePayLoadInfo_exceptionEn_f                             = 2,
    DbgEpePayLoadInfo_exceptionIndex_f                          = 3,
    DbgEpePayLoadInfo_shareType_f                               = 4,
    DbgEpePayLoadInfo_fid_f                                     = 5,
    DbgEpePayLoadInfo_payloadOperation_f                        = 6,
    DbgEpePayLoadInfo_packetLengthAdjustType_f                  = 7,
    DbgEpePayLoadInfo_packetLengthAdjust_f                      = 8,
    DbgEpePayLoadInfo_newTtlValid_f                             = 9,
    DbgEpePayLoadInfo_newTtl_f                                  = 10,
    DbgEpePayLoadInfo_newDscpValid_f                            = 11,
    DbgEpePayLoadInfo_replaceDscp_f                             = 12,
    DbgEpePayLoadInfo_useOamTtlOrExp_f                          = 13,
    DbgEpePayLoadInfo_copyDscp_f                                = 14,
    DbgEpePayLoadInfo_newDscp_f                                 = 15,
    DbgEpePayLoadInfo_vlanXlateMode_f                           = 16,
    DbgEpePayLoadInfo_svlanTagOperation_f                       = 17,
    DbgEpePayLoadInfo_l2NewSvlanTag_f                           = 18,
    DbgEpePayLoadInfo_cvlanTagOperation_f                       = 19,
    DbgEpePayLoadInfo_l2NewCvlanTag_f                           = 20,
    DbgEpePayLoadInfo_stripOffset_f                             = 21,
    DbgEpePayLoadInfo_mirroredPacket_f                          = 22,
    DbgEpePayLoadInfo_isL2Ptp_f                                 = 23,
    DbgEpePayLoadInfo_isL4Ptp_f                                 = 24,
    DbgEpePayLoadInfo_valid_f                                   = 25,

    DbgFibLkpEngineMacDaHashInfo_blackHoleMacDaResultValid_f    = 0,
    DbgFibLkpEngineMacDaHashInfo_blackHoleHitMacDaKeyIndex_f    = 1,
    DbgFibLkpEngineMacDaHashInfo_macDaHashResultValid_f         = 2,
    DbgFibLkpEngineMacDaHashInfo_macDaHashLookupConflict_f      = 3,
    DbgFibLkpEngineMacDaHashInfo_pending_f                      = 4,
    DbgFibLkpEngineMacDaHashInfo_hitHashKeyIndexMacDa_f         = 5,
    DbgFibLkpEngineMacDaHashInfo_valid_f                        = 6,

    DbgFibLkpEngineMacSaHashInfo_blackHoleMacSaResultValid_f    = 0,
    DbgFibLkpEngineMacSaHashInfo_blackHoleHitMacSaKeyIndex_f    = 1,
    DbgFibLkpEngineMacSaHashInfo_macSaHashResultValid_f         = 2,
    DbgFibLkpEngineMacSaHashInfo_macSaHashLookupConflict_f      = 3,
    DbgFibLkpEngineMacSaHashInfo_pending_f                      = 4,
    DbgFibLkpEngineMacSaHashInfo_hitHashKeyIndexMacSa_f         = 5,
    DbgFibLkpEngineMacSaHashInfo_valid_f                        = 6,

    DbgFibLkpEnginePbrNatTcamInfo_natPbrTcamResultValid_f       = 0,
    DbgFibLkpEnginePbrNatTcamInfo_natPbrResultTcamEntryIndex_f  = 1,
    DbgFibLkpEnginePbrNatTcamInfo_valid_f                       = 2,

    DbgFibLkpEnginel3DaHashInfo_l3DaHashResultValid_f           = 0,
    DbgFibLkpEnginel3DaHashInfo_hitHashKeyIndexl3Da_f           = 1,
    DbgFibLkpEnginel3DaHashInfo_valid_f                         = 2,

    DbgFibLkpEnginel3DaLpmInfo_l3DaTcamResultValid_f            = 0,
    DbgFibLkpEnginel3DaLpmInfo_l3DaResultTcamEntryIndex_f       = 1,
    DbgFibLkpEnginel3DaLpmInfo_ipDaPipeline1DebugIndex_f        = 2,
    DbgFibLkpEnginel3DaLpmInfo_ipDaPipeline2DebugIndex_f        = 3,
    DbgFibLkpEnginel3DaLpmInfo_valid_f                          = 4,

    DbgFibLkpEnginel3SaHashInfo_l3SaHashResultValid_f           = 0,
    DbgFibLkpEnginel3SaHashInfo_hitHashKeyIndexl3Sa_f           = 1,
    DbgFibLkpEnginel3SaHashInfo_valid_f                         = 2,

    DbgFibLkpEnginel3SaLpmInfo_l3SaTcamResultValid_f            = 0,
    DbgFibLkpEnginel3SaLpmInfo_l3SaResultTcamEntryIndex_f       = 1,
    DbgFibLkpEnginel3SaLpmInfo_ipSaPipeline1DebugIndex_f        = 2,
    DbgFibLkpEnginel3SaLpmInfo_ipSaPipeline2DebugIndex_f        = 3,
    DbgFibLkpEnginel3SaLpmInfo_valid_f                          = 4,

    DbgFlowHashEngineInfo_hashKeyType_f                         = 0,
    DbgFlowHashEngineInfo_flowFieldSel_f                        = 1,
    DbgFlowHashEngineInfo_flowL2KeyUseCvlan_f                   = 2,
    DbgFlowHashEngineInfo_lookupResultValid_f                   = 3,
    DbgFlowHashEngineInfo_hashConflict_f                        = 4,
    DbgFlowHashEngineInfo_keyIndex_f                            = 5,
    DbgFlowHashEngineInfo_dsAdIndex_f                           = 6,
    DbgFlowHashEngineInfo_valid_f                               = 7,

    DbgFlowTcamEngineEpeAclInfo_egressAcl0TcamResultValid_f     = 0,
    DbgFlowTcamEngineEpeAclInfo_egressAcl0TcamIndex_f           = 1,
    DbgFlowTcamEngineEpeAclInfo_valid_f                         = 2,

    DbgFlowTcamEngineIpeAclInfo_ingressAcl0TcamResultValid_f    = 0,
    DbgFlowTcamEngineIpeAclInfo_ingressAcl0TcamIndex_f          = 1,
    DbgFlowTcamEngineIpeAclInfo_ingressAcl1TcamResultValid_f    = 2,
    DbgFlowTcamEngineIpeAclInfo_ingressAcl1TcamIndex_f          = 3,
    DbgFlowTcamEngineIpeAclInfo_ingressAcl2TcamResultValid_f    = 4,
    DbgFlowTcamEngineIpeAclInfo_ingressAcl2TcamIndex_f          = 5,
    DbgFlowTcamEngineIpeAclInfo_ingressAcl3TcamResultValid_f    = 6,
    DbgFlowTcamEngineIpeAclInfo_ingressAcl3TcamIndex_f          = 7,
    DbgFlowTcamEngineIpeAclInfo_valid_f                         = 8,

    DbgFlowTcamEngineUserIdInfo_userId0TcamResultValid_f        = 0,
    DbgFlowTcamEngineUserIdInfo_userId0TcamIndex_f              = 1,
    DbgFlowTcamEngineUserIdInfo_userId1TcamResultValid_f        = 2,
    DbgFlowTcamEngineUserIdInfo_userId1TcamIndex_f              = 3,
    DbgFlowTcamEngineUserIdInfo_valid_f                         = 4,

    DbgFwdBufRetrvInfo_destSelect_f                             = 0,
    DbgFwdBufRetrvInfo_microburstValid_f                        = 1,
    DbgFwdBufRetrvInfo_valid_f                                  = 2,
    DbgFwdBufRetrvInfo_u1_nat_l4SourcePort_f                    = 3,
    DbgFwdBufRetrvInfo_u1_nat_l4SourcePortValid_f               = 4,
    DbgFwdBufRetrvInfo_u1_nat_ipSa_f                            = 5,
    DbgFwdBufRetrvInfo_u1_nat_ipSaValid_f                       = 6,
    DbgFwdBufRetrvInfo_u1_nat_srcAddressMode_f                  = 7,
    DbgFwdBufRetrvInfo_u1_nat_ipv4SrcEmbededMode_f              = 8,
    DbgFwdBufRetrvInfo_u1_nat_ptEnable_f                        = 9,
    DbgFwdBufRetrvInfo_u1_nat_ipSaMode_f                        = 10,
    DbgFwdBufRetrvInfo_u1_nat_ipSaPrefixLength_f                = 11,
    DbgFwdBufRetrvInfo_u1_nat_isEloopPkt_f                      = 12,
    DbgFwdBufRetrvInfo_u1_oam_rxOam_f                           = 13,
    DbgFwdBufRetrvInfo_u1_oam_mepIndex_f                        = 14,
    DbgFwdBufRetrvInfo_u1_oam_oamType_f                         = 15,
    DbgFwdBufRetrvInfo_u1_oam_linkOam_f                         = 16,
    DbgFwdBufRetrvInfo_u1_oam_localPhyPort_f                    = 17,
    DbgFwdBufRetrvInfo_u1_oam_galExist_f                        = 18,
    DbgFwdBufRetrvInfo_u1_oam_entropyLabelExist_f               = 19,
    DbgFwdBufRetrvInfo_u1_oam_isUp_f                            = 20,
    DbgFwdBufRetrvInfo_u1_oam_dmEn_f                            = 21,
    DbgFwdBufRetrvInfo_u1_oam_lmReceivedPacket_f                = 22,
    DbgFwdBufRetrvInfo_u1_oam_rxFcb_f                           = 23,
    DbgFwdBufRetrvInfo_u1_oam_rxtxFcl_f                         = 24,
    DbgFwdBufRetrvInfo_u1_oam_isEloopPkt_f                      = 25,
    DbgFwdBufRetrvInfo_u1_dmtx_dmOffset_f                       = 26,
    DbgFwdBufRetrvInfo_u1_dmtx_isEloopPkt_f                     = 27,
    DbgFwdBufRetrvInfo_u1_lmtx_lmPacketType_f                   = 28,
    DbgFwdBufRetrvInfo_u1_lmtx_txFcb_f                          = 29,
    DbgFwdBufRetrvInfo_u1_lmtx_rxFcb_f                          = 30,
    DbgFwdBufRetrvInfo_u1_lmtx_rxtxFcl_f                        = 31,
    DbgFwdBufRetrvInfo_u1_ptp_ptpSequenceId_f                   = 32,
    DbgFwdBufRetrvInfo_u1_ptp_ptpDeepParser_f                   = 33,
    DbgFwdBufRetrvInfo_u1_ptp_isL2Ptp_f                         = 34,
    DbgFwdBufRetrvInfo_u1_ptp_isL4Ptp_f                         = 35,
    DbgFwdBufRetrvInfo_u1_ptp_ptpCaptureTimestamp_f             = 36,
    DbgFwdBufRetrvInfo_u1_ptp_ptpReplaceTimestamp_f             = 37,
    DbgFwdBufRetrvInfo_u1_ptp_ptpApplyEgressAsymmetryDelay_f    = 38,
    DbgFwdBufRetrvInfo_u1_ptp_ptpUpdateResidenceTime_f          = 39,
    DbgFwdBufRetrvInfo_u1_ptp_ptpOperationMode_f                = 40,
    DbgFwdBufRetrvInfo_u1_ptp_ptpOffset_f                       = 41,
    DbgFwdBufRetrvInfo_u1_ptp_isEloopPkt_f                      = 42,
    DbgFwdBufRetrvInfo_u1_c2c_sgmacStripHeader_f                = 43,
    DbgFwdBufRetrvInfo_u1_c2c_c2cCheckDisable_f                 = 44,
    DbgFwdBufRetrvInfo_u1_c2c_isEloopPkt_f                      = 45,
    DbgFwdBufRetrvInfo_u1_c2c_stackingSrcPort_f                 = 46,
    DbgFwdBufRetrvInfo_u1_normal_layer3Offset_f                 = 47,
    DbgFwdBufRetrvInfo_u1_normal_ecmpHash_f                     = 48,
    DbgFwdBufRetrvInfo_u1_normal_aclDscpValid_f                 = 49,
    DbgFwdBufRetrvInfo_u1_normal_isIpv4_f                       = 50,
    DbgFwdBufRetrvInfo_u1_normal_aclDscp_f                      = 51,
    DbgFwdBufRetrvInfo_u1_normal_ecnAware_f                     = 52,
    DbgFwdBufRetrvInfo_u1_normal_ecnEn_f                        = 53,
    DbgFwdBufRetrvInfo_u1_normal_pbbCheckDiscard_f              = 54,
    DbgFwdBufRetrvInfo_u1_normal_cTagAction_f                   = 55,
    DbgFwdBufRetrvInfo_u1_normal_srcCtagCos_f                   = 56,
    DbgFwdBufRetrvInfo_u1_normal_srcCtagCfi_f                   = 57,
    DbgFwdBufRetrvInfo_u1_normal_srcCtagOffsetType_f            = 58,
    DbgFwdBufRetrvInfo_u1_normal_pbbSrcPortType_f               = 59,
    DbgFwdBufRetrvInfo_u1_normal_sourcePortIsolateId_f          = 60,
    DbgFwdBufRetrvInfo_u1_normal_srcCvlanId_f                   = 61,
    DbgFwdBufRetrvInfo_u1_normal_isidValid_f                    = 62,
    DbgFwdBufRetrvInfo_u1_normal_cvlanTagOperationValid_f       = 63,
    DbgFwdBufRetrvInfo_u1_normal_congestionValid_f              = 64,
    DbgFwdBufRetrvInfo_u1_normal_isEloopPkt_f                   = 65,
    DbgFwdBufRetrvInfo_u1_normal_metadataType_f                 = 66,
    DbgFwdBufRetrvInfo_u1_normal_metadata_f                     = 67,
    DbgFwdBufRetrvInfo_u2_oam_mipEn_f                           = 68,
    DbgFwdBufRetrvInfo_u2_oam_oamDestChipId_f                   = 69,
    DbgFwdBufRetrvInfo_u2_other_srcDscp_f                       = 70,
    DbgFwdBufRetrvInfo_timestamp_f                              = 71,
    DbgFwdBufRetrvInfo_u3_other_timestamp_f                     = 72,
    DbgFwdBufRetrvInfo_u3_oam_rxtxFcl_f                         = 73,
    DbgFwdBufRetrvInfo_u3_lmtx_rxtxFcl_f                        = 74,
    DbgFwdBufRetrvInfo_operationType_f                          = 75,
    DbgFwdBufRetrvInfo_ttl_f                                    = 76,
    DbgFwdBufRetrvInfo_logicSrcPort_f                           = 77,
    DbgFwdBufRetrvInfo_fid_f                                    = 78,
    DbgFwdBufRetrvInfo_oamUseFid_f                              = 79,
    DbgFwdBufRetrvInfo_macKnown_f                               = 80,
    DbgFwdBufRetrvInfo_stagAction_f                             = 81,
    DbgFwdBufRetrvInfo_keepOldHeader_f                          = 82,
    DbgFwdBufRetrvInfo_debugSessionEn_f                         = 83,
    DbgFwdBufRetrvInfo_macLearningEn_f                          = 84,
    DbgFwdBufRetrvInfo_bridgeOperation_f                        = 85,
    DbgFwdBufRetrvInfo_oamTunnelEn_f                            = 86,
    DbgFwdBufRetrvInfo_muxLengthType_f                          = 87,
    DbgFwdBufRetrvInfo_srcVlanPtr_f                             = 88,
    DbgFwdBufRetrvInfo_svlanTpidIndex_f                         = 89,
    DbgFwdBufRetrvInfo_fromCpuOrOam_f                           = 90,
    DbgFwdBufRetrvInfo_outerVlanIsCVlan_f                       = 91,
    DbgFwdBufRetrvInfo_ingressEditEn_f                          = 92,
    DbgFwdBufRetrvInfo_fromCpuLmDownDisable_f                   = 93,
    DbgFwdBufRetrvInfo_dsNextHopBypassAll_f                     = 94,
    DbgFwdBufRetrvInfo_bypassIngressEdit_f                      = 95,
    DbgFwdBufRetrvInfo_fromFabric_f                             = 96,
    DbgFwdBufRetrvInfo_sourcePortExtender_f                     = 97,
    DbgFwdBufRetrvInfo_svlanTagOperationValid_f                 = 98,
    DbgFwdBufRetrvInfo_fromCpuLmUpDisable_f                     = 99,
    DbgFwdBufRetrvInfo_portMacSaEn_f                            = 100,
    DbgFwdBufRetrvInfo_nextHopExt_f                             = 101,
    DbgFwdBufRetrvInfo_sourceCfi_f                              = 102,
    DbgFwdBufRetrvInfo_isLeaf_f                                 = 103,
    DbgFwdBufRetrvInfo_criticalPacket_f                         = 104,
    DbgFwdBufRetrvInfo_lengthAdjustType_f                       = 105,
    DbgFwdBufRetrvInfo_nextHopPtr_f                             = 106,
    DbgFwdBufRetrvInfo_color_f                                  = 107,
    DbgFwdBufRetrvInfo_srcVlanId_f                              = 108,
    DbgFwdBufRetrvInfo_sourcePort_f                             = 109,
    DbgFwdBufRetrvInfo_logicPortType_f                          = 110,
    DbgFwdBufRetrvInfo_headerHash_f                             = 111,
    DbgFwdBufRetrvInfo_sourceCos_f                              = 112,
    DbgFwdBufRetrvInfo_packetType_f                             = 113,
    DbgFwdBufRetrvInfo__priority_f                              = 114,
    DbgFwdBufRetrvInfo_destMap_f                                = 115,
    DbgFwdBufRetrvInfo_packetOffset_f                           = 116,

    DbgFwdBufStoreInfo_discard_f                                = 0,
    DbgFwdBufStoreInfo_bsFromFabric_f                           = 1,
    DbgFwdBufStoreInfo_egressException_f                        = 2,
    DbgFwdBufStoreInfo_exceptionVector_f                        = 3,
    DbgFwdBufStoreInfo_localSwitching_f                         = 4,
    DbgFwdBufStoreInfo_destIdDiscard_f                          = 5,
    DbgFwdBufStoreInfo_metFifoSelect_f                          = 6,
    DbgFwdBufStoreInfo_valid_f                                  = 7,
    DbgFwdBufStoreInfo_u1_nat_l4SourcePort_f                    = 8,
    DbgFwdBufStoreInfo_u1_nat_l4SourcePortValid_f               = 9,
    DbgFwdBufStoreInfo_u1_nat_ipSa_f                            = 10,
    DbgFwdBufStoreInfo_u1_nat_ipSaValid_f                       = 11,
    DbgFwdBufStoreInfo_u1_nat_srcAddressMode_f                  = 12,
    DbgFwdBufStoreInfo_u1_nat_ipv4SrcEmbededMode_f              = 13,
    DbgFwdBufStoreInfo_u1_nat_ptEnable_f                        = 14,
    DbgFwdBufStoreInfo_u1_nat_ipSaMode_f                        = 15,
    DbgFwdBufStoreInfo_u1_nat_ipSaPrefixLength_f                = 16,
    DbgFwdBufStoreInfo_u1_nat_isEloopPkt_f                      = 17,
    DbgFwdBufStoreInfo_u1_oam_rxOam_f                           = 18,
    DbgFwdBufStoreInfo_u1_oam_mepIndex_f                        = 19,
    DbgFwdBufStoreInfo_u1_oam_oamType_f                         = 20,
    DbgFwdBufStoreInfo_u1_oam_linkOam_f                         = 21,
    DbgFwdBufStoreInfo_u1_oam_localPhyPort_f                    = 22,
    DbgFwdBufStoreInfo_u1_oam_galExist_f                        = 23,
    DbgFwdBufStoreInfo_u1_oam_entropyLabelExist_f               = 24,
    DbgFwdBufStoreInfo_u1_oam_isUp_f                            = 25,
    DbgFwdBufStoreInfo_u1_oam_dmEn_f                            = 26,
    DbgFwdBufStoreInfo_u1_oam_lmReceivedPacket_f                = 27,
    DbgFwdBufStoreInfo_u1_oam_rxFcb_f                           = 28,
    DbgFwdBufStoreInfo_u1_oam_rxtxFcl_f                         = 29,
    DbgFwdBufStoreInfo_u1_oam_isEloopPkt_f                      = 30,
    DbgFwdBufStoreInfo_u1_dmtx_dmOffset_f                       = 31,
    DbgFwdBufStoreInfo_u1_dmtx_isEloopPkt_f                     = 32,
    DbgFwdBufStoreInfo_u1_lmtx_lmPacketType_f                   = 33,
    DbgFwdBufStoreInfo_u1_lmtx_txFcb_f                          = 34,
    DbgFwdBufStoreInfo_u1_lmtx_rxFcb_f                          = 35,
    DbgFwdBufStoreInfo_u1_lmtx_rxtxFcl_f                        = 36,
    DbgFwdBufStoreInfo_u1_ptp_ptpSequenceId_f                   = 37,
    DbgFwdBufStoreInfo_u1_ptp_ptpDeepParser_f                   = 38,
    DbgFwdBufStoreInfo_u1_ptp_isL2Ptp_f                         = 39,
    DbgFwdBufStoreInfo_u1_ptp_isL4Ptp_f                         = 40,
    DbgFwdBufStoreInfo_u1_ptp_ptpCaptureTimestamp_f             = 41,
    DbgFwdBufStoreInfo_u1_ptp_ptpReplaceTimestamp_f             = 42,
    DbgFwdBufStoreInfo_u1_ptp_ptpApplyEgressAsymmetryDelay_f    = 43,
    DbgFwdBufStoreInfo_u1_ptp_ptpUpdateResidenceTime_f          = 44,
    DbgFwdBufStoreInfo_u1_ptp_ptpOperationMode_f                = 45,
    DbgFwdBufStoreInfo_u1_ptp_ptpOffset_f                       = 46,
    DbgFwdBufStoreInfo_u1_ptp_isEloopPkt_f                      = 47,
    DbgFwdBufStoreInfo_u1_c2c_sgmacStripHeader_f                = 48,
    DbgFwdBufStoreInfo_u1_c2c_c2cCheckDisable_f                 = 49,
    DbgFwdBufStoreInfo_u1_c2c_isEloopPkt_f                      = 50,
    DbgFwdBufStoreInfo_u1_c2c_stackingSrcPort_f                 = 51,
    DbgFwdBufStoreInfo_u1_normal_layer3Offset_f                 = 52,
    DbgFwdBufStoreInfo_u1_normal_ecmpHash_f                     = 53,
    DbgFwdBufStoreInfo_u1_normal_aclDscpValid_f                 = 54,
    DbgFwdBufStoreInfo_u1_normal_isIpv4_f                       = 55,
    DbgFwdBufStoreInfo_u1_normal_aclDscp_f                      = 56,
    DbgFwdBufStoreInfo_u1_normal_ecnAware_f                     = 57,
    DbgFwdBufStoreInfo_u1_normal_ecnEn_f                        = 58,
    DbgFwdBufStoreInfo_u1_normal_pbbCheckDiscard_f              = 59,
    DbgFwdBufStoreInfo_u1_normal_cTagAction_f                   = 60,
    DbgFwdBufStoreInfo_u1_normal_srcCtagCos_f                   = 61,
    DbgFwdBufStoreInfo_u1_normal_srcCtagCfi_f                   = 62,
    DbgFwdBufStoreInfo_u1_normal_srcCtagOffsetType_f            = 63,
    DbgFwdBufStoreInfo_u1_normal_pbbSrcPortType_f               = 64,
    DbgFwdBufStoreInfo_u1_normal_sourcePortIsolateId_f          = 65,
    DbgFwdBufStoreInfo_u1_normal_srcCvlanId_f                   = 66,
    DbgFwdBufStoreInfo_u1_normal_isidValid_f                    = 67,
    DbgFwdBufStoreInfo_u1_normal_cvlanTagOperationValid_f       = 68,
    DbgFwdBufStoreInfo_u1_normal_congestionValid_f              = 69,
    DbgFwdBufStoreInfo_u1_normal_isEloopPkt_f                   = 70,
    DbgFwdBufStoreInfo_u1_normal_metadataType_f                 = 71,
    DbgFwdBufStoreInfo_u1_normal_metadata_f                     = 72,
    DbgFwdBufStoreInfo_u2_oam_mipEn_f                           = 73,
    DbgFwdBufStoreInfo_u2_oam_oamDestChipId_f                   = 74,
    DbgFwdBufStoreInfo_u2_other_srcDscp_f                       = 75,
    DbgFwdBufStoreInfo_timestamp_f                              = 76,
    DbgFwdBufStoreInfo_u3_other_timestamp_f                     = 77,
    DbgFwdBufStoreInfo_u3_oam_rxtxFcl_f                         = 78,
    DbgFwdBufStoreInfo_u3_lmtx_rxtxFcl_f                        = 79,
    DbgFwdBufStoreInfo_operationType_f                          = 80,
    DbgFwdBufStoreInfo_ttl_f                                    = 81,
    DbgFwdBufStoreInfo_logicSrcPort_f                           = 82,
    DbgFwdBufStoreInfo_fid_f                                    = 83,
    DbgFwdBufStoreInfo_oamUseFid_f                              = 84,
    DbgFwdBufStoreInfo_macKnown_f                               = 85,
    DbgFwdBufStoreInfo_stagAction_f                             = 86,
    DbgFwdBufStoreInfo_keepOldHeader_f                          = 87,
    DbgFwdBufStoreInfo_debugSessionEn_f                         = 88,
    DbgFwdBufStoreInfo_macLearningEn_f                          = 89,
    DbgFwdBufStoreInfo_bridgeOperation_f                        = 90,
    DbgFwdBufStoreInfo_oamTunnelEn_f                            = 91,
    DbgFwdBufStoreInfo_muxLengthType_f                          = 92,
    DbgFwdBufStoreInfo_srcVlanPtr_f                             = 93,
    DbgFwdBufStoreInfo_svlanTpidIndex_f                         = 94,
    DbgFwdBufStoreInfo_fromCpuOrOam_f                           = 95,
    DbgFwdBufStoreInfo_outerVlanIsCVlan_f                       = 96,
    DbgFwdBufStoreInfo_ingressEditEn_f                          = 97,
    DbgFwdBufStoreInfo_fromCpuLmDownDisable_f                   = 98,
    DbgFwdBufStoreInfo_dsNextHopBypassAll_f                     = 99,
    DbgFwdBufStoreInfo_bypassIngressEdit_f                      = 100,
    DbgFwdBufStoreInfo_fromFabric_f                             = 101,
    DbgFwdBufStoreInfo_sourcePortExtender_f                     = 102,
    DbgFwdBufStoreInfo_svlanTagOperationValid_f                 = 103,
    DbgFwdBufStoreInfo_fromCpuLmUpDisable_f                     = 104,
    DbgFwdBufStoreInfo_portMacSaEn_f                            = 105,
    DbgFwdBufStoreInfo_nextHopExt_f                             = 106,
    DbgFwdBufStoreInfo_sourceCfi_f                              = 107,
    DbgFwdBufStoreInfo_isLeaf_f                                 = 108,
    DbgFwdBufStoreInfo_criticalPacket_f                         = 109,
    DbgFwdBufStoreInfo_lengthAdjustType_f                       = 110,
    DbgFwdBufStoreInfo_nextHopPtr_f                             = 111,
    DbgFwdBufStoreInfo_color_f                                  = 112,
    DbgFwdBufStoreInfo_srcVlanId_f                              = 113,
    DbgFwdBufStoreInfo_sourcePort_f                             = 114,
    DbgFwdBufStoreInfo_logicPortType_f                          = 115,
    DbgFwdBufStoreInfo_headerHash_f                             = 116,
    DbgFwdBufStoreInfo_sourceCos_f                              = 117,
    DbgFwdBufStoreInfo_packetType_f                             = 118,
    DbgFwdBufStoreInfo__priority_f                              = 119,
    DbgFwdBufStoreInfo_destMap_f                                = 120,
    DbgFwdBufStoreInfo_packetOffset_f                           = 121,

    DbgFwdMetFifoInfo_exceptionVector_f                         = 0,
    DbgFwdMetFifoInfo_rxOam_f                                   = 1,
    DbgFwdMetFifoInfo_doMet_f                                   = 2,
    DbgFwdMetFifoInfo_enqueueDiscard_f                          = 3,
    DbgFwdMetFifoInfo_lastEnqueue_f                             = 4,
    DbgFwdMetFifoInfo_rcd_f                                     = 5,
    DbgFwdMetFifoInfo_remainingRcd_f                            = 6,
    DbgFwdMetFifoInfo_valid_f                                   = 7,

    DbgFwdQueMgrInfo_noLinkaggSwitch_f                          = 0,
    DbgFwdQueMgrInfo_translateLinkAggregation_f                 = 1,
    DbgFwdQueMgrInfo_rrEn_f                                     = 2,
    DbgFwdQueMgrInfo_noLinkAggregationMemberDiscard_f           = 3,
    DbgFwdQueMgrInfo_linkAggPortRemapEn_f                       = 4,
    DbgFwdQueMgrInfo_sgmacSelectEn_f                            = 5,
    DbgFwdQueMgrInfo_queueNumGenCtl_f                           = 6,
    DbgFwdQueMgrInfo_shiftQueueNum_f                            = 7,
    DbgFwdQueMgrInfo_mcastLinkAggregationDiscard_f              = 8,
    DbgFwdQueMgrInfo_stackingDiscard_f                          = 9,
    DbgFwdQueMgrInfo_replicationCtl_f                           = 10,
    DbgFwdQueMgrInfo_valid_f                                    = 11,

    DbgIpeAclLkpInfo_secondParserForEcmp_f                      = 0,
    DbgIpeAclLkpInfo_innerVtagCheckMode_f                       = 1,
    DbgIpeAclLkpInfo_isDecap_f                                  = 2,
    DbgIpeAclLkpInfo_isMplsSwitched_f                           = 3,
    DbgIpeAclLkpInfo_ptpIndex_f                                 = 4,
    DbgIpeAclLkpInfo_innerVsiIdValid_f                          = 5,
    DbgIpeAclLkpInfo_innerLogicSrcPortValid_f                   = 6,
    DbgIpeAclLkpInfo_bridgePacket_f                             = 7,
    DbgIpeAclLkpInfo_ipRoutedPacket_f                           = 8,
    DbgIpeAclLkpInfo_serviceAclQosEn_f                          = 9,
    DbgIpeAclLkpInfo_aclUseBitmap_f                             = 10,
    DbgIpeAclLkpInfo_overwritePortAclFlowTcam_f                 = 11,
    DbgIpeAclLkpInfo_aclFlowTcamEn_f                            = 12,
    DbgIpeAclLkpInfo_aclQosUseOuterInfo_f                       = 13,
    DbgIpeAclLkpInfo_isNvgre_f                                  = 14,
    DbgIpeAclLkpInfo_aclUsePIVlan_f                             = 15,
    DbgIpeAclLkpInfo_aclFlowHashType_f                          = 16,
    DbgIpeAclLkpInfo_aclFlowHashFieldSel_f                      = 17,
    DbgIpeAclLkpInfo_ipfixHashType_f                            = 18,
    DbgIpeAclLkpInfo_ipfixHashFieldSel_f                        = 19,
    DbgIpeAclLkpInfo_aclEn0_f                                   = 20,
    DbgIpeAclLkpInfo_aclLookupType0_f                           = 21,
    DbgIpeAclLkpInfo_aclLabel0_f                                = 22,
    DbgIpeAclLkpInfo_aclEn1_f                                   = 23,
    DbgIpeAclLkpInfo_aclLookupType1_f                           = 24,
    DbgIpeAclLkpInfo_aclLabel1_f                                = 25,
    DbgIpeAclLkpInfo_aclEn2_f                                   = 26,
    DbgIpeAclLkpInfo_aclLookupType2_f                           = 27,
    DbgIpeAclLkpInfo_aclLabel2_f                                = 28,
    DbgIpeAclLkpInfo_aclEn3_f                                   = 29,
    DbgIpeAclLkpInfo_aclLookupType3_f                           = 30,
    DbgIpeAclLkpInfo_aclLabel3_f                                = 31,
    DbgIpeAclLkpInfo_valid_f                                    = 32,

    DbgIpeAclProcInfo_qosPolicy_f                               = 0,
    DbgIpeAclProcInfo_userPriorityValid_f                       = 1,
    DbgIpeAclProcInfo_payloadOffset_f                           = 2,
    DbgIpeAclProcInfo_dsFwdPtrValid_f                           = 3,
    DbgIpeAclProcInfo_nextHopPtrValid_f                         = 4,
    DbgIpeAclProcInfo_ecmpGroupId_f                             = 5,
    DbgIpeAclProcInfo_discard_f                                 = 6,
    DbgIpeAclProcInfo_discardType_f                             = 7,
    DbgIpeAclProcInfo_dsFwdPtr_f                                = 8,
    DbgIpeAclProcInfo_exceptionEn_f                             = 9,
    DbgIpeAclProcInfo_exceptionIndex_f                          = 10,
    DbgIpeAclProcInfo_exceptionSubIndex_f                       = 11,
    DbgIpeAclProcInfo_denyBridge_f                              = 12,
    DbgIpeAclProcInfo_denyLearning_f                            = 13,
    DbgIpeAclProcInfo_denyRoute_f                               = 14,
    DbgIpeAclProcInfo_flowForceBridge_f                         = 15,
    DbgIpeAclProcInfo_flowForceLearning_f                       = 16,
    DbgIpeAclProcInfo_flowForceRoute_f                          = 17,
    DbgIpeAclProcInfo_postcardEn_f                              = 18,
    DbgIpeAclProcInfo_forceBackEn_f                             = 19,
    DbgIpeAclProcInfo_flowPolicerPtr_f                          = 20,
    DbgIpeAclProcInfo_aggFlowPolicerPtr_f                       = 21,
    DbgIpeAclProcInfo_aclLogEn0_f                               = 22,
    DbgIpeAclProcInfo_aclLogId0_f                               = 23,
    DbgIpeAclProcInfo_aclLogEn1_f                               = 24,
    DbgIpeAclProcInfo_aclLogId1_f                               = 25,
    DbgIpeAclProcInfo_aclLogEn2_f                               = 26,
    DbgIpeAclProcInfo_aclLogId2_f                               = 27,
    DbgIpeAclProcInfo_aclLogEn3_f                               = 28,
    DbgIpeAclProcInfo_aclLogId3_f                               = 29,
    DbgIpeAclProcInfo_svlanTagOperationValid_f                  = 30,
    DbgIpeAclProcInfo_cvlanTagOperationValid_f                  = 31,
    DbgIpeAclProcInfo_metadataType_f                            = 32,
    DbgIpeAclProcInfo_metadata_f                                = 33,
    DbgIpeAclProcInfo_flowPriorityValid_f                       = 34,
    DbgIpeAclProcInfo_flowColorValid_f                          = 35,
    DbgIpeAclProcInfo_valid_f                                   = 36,

    DbgIpeClsInfo_aggFlowPolicerValid_f                         = 0,
    DbgIpeClsInfo_flowPolicerValid_f                            = 1,
    DbgIpeClsInfo_servicePolicerValid_f                         = 2,
    DbgIpeClsInfo_markDrop_f                                    = 3,
    DbgIpeClsInfo_newColor_f                                    = 4,
    DbgIpeClsInfo_inPortPolicer_f                               = 5,
    DbgIpeClsInfo_valid_f                                       = 6,

    DbgIpeEcmpProcInfo_efdDsFwdPtrValid_f                       = 0,
    DbgIpeEcmpProcInfo_fwdStatsValid_f                          = 1,
    DbgIpeEcmpProcInfo_dsFwdPtrValid_f                          = 2,
    DbgIpeEcmpProcInfo_nextHopPtrValid_f                        = 3,
    DbgIpeEcmpProcInfo_dsFwdPtr_f                               = 4,
    DbgIpeEcmpProcInfo_ecmpGroupId_f                            = 5,
    DbgIpeEcmpProcInfo_ecmpEn_f                                 = 6,
    DbgIpeEcmpProcInfo_accessDlbEngine_f                        = 7,
    DbgIpeEcmpProcInfo_rrEn_f                                   = 8,
    DbgIpeEcmpProcInfo_equalCostPathSel_f                       = 9,
    DbgIpeEcmpProcInfo_valid_f                                  = 10,

    DbgIpeFcoeInfo_l3DaResultValid_f                            = 0,
    DbgIpeFcoeInfo_l3SaResultValid_f                            = 1,
    DbgIpeFcoeInfo_isFcoe_f                                     = 2,
    DbgIpeFcoeInfo_isFcoeRpf_f                                  = 3,
    DbgIpeFcoeInfo_l3DaLookupEn_f                               = 4,
    DbgIpeFcoeInfo_l3SaLookupEn_f                               = 5,
    DbgIpeFcoeInfo_dsFwdPtrValid_f                              = 6,
    DbgIpeFcoeInfo_ecmpGroupId_f                                = 7,
    DbgIpeFcoeInfo_nextHopPtrValid_f                            = 8,
    DbgIpeFcoeInfo_dsFwdPtr_f                                   = 9,
    DbgIpeFcoeInfo_valid_f                                      = 10,

    DbgIpeForwardingInfo_fwdStatsValid_f                        = 0,
    DbgIpeForwardingInfo_ingressHeaderValid_f                   = 1,
    DbgIpeForwardingInfo_timestampValid_f                       = 2,
    DbgIpeForwardingInfo_isL2Ptp_f                              = 3,
    DbgIpeForwardingInfo_isL4Ptp_f                              = 4,
    DbgIpeForwardingInfo_ptpUpdateResidenceTime_f               = 5,
    DbgIpeForwardingInfo_muxDestinationValid_f                  = 6,
    DbgIpeForwardingInfo_isNewElephant_f                        = 7,
    DbgIpeForwardingInfo_srcPortStatsEn_f                       = 8,
    DbgIpeForwardingInfo_acl0StatsValid_f                       = 9,
    DbgIpeForwardingInfo_acl1StatsValid_f                       = 10,
    DbgIpeForwardingInfo_acl2StatsValid_f                       = 11,
    DbgIpeForwardingInfo_acl3StatsValid_f                       = 12,
    DbgIpeForwardingInfo_ifStatsValid_f                         = 13,
    DbgIpeForwardingInfo_ipfixFlowLookupEn_f                    = 14,
    DbgIpeForwardingInfo_shareType_f                            = 15,
    DbgIpeForwardingInfo_discardType_f                          = 16,
    DbgIpeForwardingInfo_exceptionEn_f                          = 17,
    DbgIpeForwardingInfo_exceptionIndex_f                       = 18,
    DbgIpeForwardingInfo_exceptionSubIndex_f                    = 19,
    DbgIpeForwardingInfo_fatalExceptionValid_f                  = 20,
    DbgIpeForwardingInfo_fatalException_f                       = 21,
    DbgIpeForwardingInfo_apsSelectValid0_f                      = 22,
    DbgIpeForwardingInfo_apsSelectGroupId0_f                    = 23,
    DbgIpeForwardingInfo_apsSelectValid1_f                      = 24,
    DbgIpeForwardingInfo_apsSelectGroupId1_f                    = 25,
    DbgIpeForwardingInfo_discard_f                              = 26,
    DbgIpeForwardingInfo_destMap_f                              = 27,
    DbgIpeForwardingInfo_apsBridgeEn_f                          = 28,
    DbgIpeForwardingInfo_nextHopExt_f                           = 29,
    DbgIpeForwardingInfo_nextHopIndex_f                         = 30,
    DbgIpeForwardingInfo_headerHash_f                           = 31,
    DbgIpeForwardingInfo_valid_f                                = 32,

    DbgIpeIntfMapperInfo_vlanId_f                               = 0,
    DbgIpeIntfMapperInfo_isLoop_f                               = 1,
    DbgIpeIntfMapperInfo_isPortMac_f                            = 2,
    DbgIpeIntfMapperInfo_sourceCos_f                            = 3,
    DbgIpeIntfMapperInfo_sourceCfi_f                            = 4,
    DbgIpeIntfMapperInfo_ctagCos_f                              = 5,
    DbgIpeIntfMapperInfo_ctagCfi_f                              = 6,
    DbgIpeIntfMapperInfo_svlanTagOperationValid_f               = 7,
    DbgIpeIntfMapperInfo_cvlanTagOperationValid_f               = 8,
    DbgIpeIntfMapperInfo_classifyStagCos_f                      = 9,
    DbgIpeIntfMapperInfo_classifyStagCfi_f                      = 10,
    DbgIpeIntfMapperInfo_classifyCtagCos_f                      = 11,
    DbgIpeIntfMapperInfo_classifyCtagCfi_f                      = 12,
    DbgIpeIntfMapperInfo_vlanPtr_f                              = 13,
    DbgIpeIntfMapperInfo_interfaceId_f                          = 14,
    DbgIpeIntfMapperInfo_isRouterMac_f                          = 15,
    DbgIpeIntfMapperInfo_discard_f                              = 16,
    DbgIpeIntfMapperInfo_discardType_f                          = 17,
    DbgIpeIntfMapperInfo_bypassAll_f                            = 18,
    DbgIpeIntfMapperInfo_exceptionEn_f                          = 19,
    DbgIpeIntfMapperInfo_exceptionIndex_f                       = 20,
    DbgIpeIntfMapperInfo_exceptionSubIndex_f                    = 21,
    DbgIpeIntfMapperInfo_dsFwdPtrValid_f                        = 22,
    DbgIpeIntfMapperInfo_dsFwdPtr_f                             = 23,
    DbgIpeIntfMapperInfo_protocolVlanEn_f                       = 24,
    DbgIpeIntfMapperInfo_tempBypassAll_f                        = 25,
    DbgIpeIntfMapperInfo_routeAllPacket_f                       = 26,
    DbgIpeIntfMapperInfo_valid_f                                = 27,

    DbgIpeIpRoutingInfo_l3DaResultValid_f                       = 0,
    DbgIpeIpRoutingInfo_l3SaResultValid_f                       = 1,
    DbgIpeIpRoutingInfo_l3DaLookupEn_f                          = 2,
    DbgIpeIpRoutingInfo_l3SaLookupEn_f                          = 3,
    DbgIpeIpRoutingInfo_flowForceRouteUseIpSa_f                 = 4,
    DbgIpeIpRoutingInfo_ipMartianAddress_f                      = 5,
    DbgIpeIpRoutingInfo_isIpv4Ucast_f                           = 6,
    DbgIpeIpRoutingInfo_isIpv4Mcast_f                           = 7,
    DbgIpeIpRoutingInfo_isIpv4UcastRpf_f                        = 8,
    DbgIpeIpRoutingInfo_isIpv4UcastNat_f                        = 9,
    DbgIpeIpRoutingInfo_isIpv4Pbr_f                             = 10,
    DbgIpeIpRoutingInfo_isIpv6Ucast_f                           = 11,
    DbgIpeIpRoutingInfo_isIpv6Mcast_f                           = 12,
    DbgIpeIpRoutingInfo_isIpv6UcastRpf_f                        = 13,
    DbgIpeIpRoutingInfo_isIpv6UcastNat_f                        = 14,
    DbgIpeIpRoutingInfo_isIpv6Pbr_f                             = 15,
    DbgIpeIpRoutingInfo_ipLinkScopeAddress_f                    = 16,
    DbgIpeIpRoutingInfo_packetTtl_f                             = 17,
    DbgIpeIpRoutingInfo_isIcmp_f                                = 18,
    DbgIpeIpRoutingInfo_shareType_f                             = 19,
    DbgIpeIpRoutingInfo_interfaceId_f                           = 20,
    DbgIpeIpRoutingInfo_layer3Exception_f                       = 21,
    DbgIpeIpRoutingInfo_discard_f                               = 22,
    DbgIpeIpRoutingInfo_discardType_f                           = 23,
    DbgIpeIpRoutingInfo_exceptionEn_f                           = 24,
    DbgIpeIpRoutingInfo_exceptionIndex_f                        = 25,
    DbgIpeIpRoutingInfo_exceptionSubIndex_f                     = 26,
    DbgIpeIpRoutingInfo_fatalExceptionValid_f                   = 27,
    DbgIpeIpRoutingInfo_fatalException_f                        = 28,
    DbgIpeIpRoutingInfo_dsFwdPtrValid_f                         = 29,
    DbgIpeIpRoutingInfo_ecmpGroupId_f                           = 30,
    DbgIpeIpRoutingInfo_nextHopPtrValid_f                       = 31,
    DbgIpeIpRoutingInfo_payloadPacketType_f                     = 32,
    DbgIpeIpRoutingInfo_dsFwdPtr_f                              = 33,
    DbgIpeIpRoutingInfo_valid_f                                 = 34,

    DbgIpeLkpMgrInfo_discardType_f                              = 0,
    DbgIpeLkpMgrInfo_exceptionIndex_f                           = 1,
    DbgIpeLkpMgrInfo_exceptionSubIndex_f                        = 2,
    DbgIpeLkpMgrInfo_ttlUpdate_f                                = 3,
    DbgIpeLkpMgrInfo_outerTtlCheck_f                            = 4,
    DbgIpeLkpMgrInfo_stpState_f                                 = 5,
    DbgIpeLkpMgrInfo_oamUseFid_f                                = 6,
    DbgIpeLkpMgrInfo_flowForceRoute_f                           = 7,
    DbgIpeLkpMgrInfo_flowForceRouteUseIpSa_f                    = 8,
    DbgIpeLkpMgrInfo_denyRoute_f                                = 9,
    DbgIpeLkpMgrInfo_metadataType_f                             = 10,
    DbgIpeLkpMgrInfo_routeLookupMode_f                          = 11,
    DbgIpeLkpMgrInfo_isIpv4Ucast_f                              = 12,
    DbgIpeLkpMgrInfo_isIpv4Mcast_f                              = 13,
    DbgIpeLkpMgrInfo_isIpv4UcastRpf_f                           = 14,
    DbgIpeLkpMgrInfo_isIpv4UcastNat_f                           = 15,
    DbgIpeLkpMgrInfo_isIpv4Pbr_f                                = 16,
    DbgIpeLkpMgrInfo_isIpv6Ucast_f                              = 17,
    DbgIpeLkpMgrInfo_isIpv6Mcast_f                              = 18,
    DbgIpeLkpMgrInfo_isIpv6UcastRpf_f                           = 19,
    DbgIpeLkpMgrInfo_isIpv6UcastNat_f                           = 20,
    DbgIpeLkpMgrInfo_isIpv6Pbr_f                                = 21,
    DbgIpeLkpMgrInfo_isFcoe_f                                   = 22,
    DbgIpeLkpMgrInfo_isFcoeRpf_f                                = 23,
    DbgIpeLkpMgrInfo_isTrillUcast_f                             = 24,
    DbgIpeLkpMgrInfo_isTrillMcast_f                             = 25,
    DbgIpeLkpMgrInfo_macDaLookupEn_f                            = 26,
    DbgIpeLkpMgrInfo_flowForceBridge_f                          = 27,
    DbgIpeLkpMgrInfo_macSaLookupEn_f                            = 28,
    DbgIpeLkpMgrInfo_isInnerMacSaLookup_f                       = 29,
    DbgIpeLkpMgrInfo_mplsLmValid_f                              = 30,
    DbgIpeLkpMgrInfo_mplsSectionLmValid_f                       = 31,
    DbgIpeLkpMgrInfo_useDefaultVlanTag_f                        = 32,
    DbgIpeLkpMgrInfo_discard_f                                  = 33,
    DbgIpeLkpMgrInfo_exceptionEn_f                              = 34,
    DbgIpeLkpMgrInfo_ingressOamKeyType0_f                       = 35,
    DbgIpeLkpMgrInfo_ingressOamKeyType1_f                       = 36,
    DbgIpeLkpMgrInfo_ingressOamKeyType2_f                       = 37,
    DbgIpeLkpMgrInfo_valid_f                                    = 38,

    DbgIpeMacBridgingInfo_macDaResultValid_f                    = 0,
    DbgIpeMacBridgingInfo_macDaDefaultEntryValid_f              = 1,
    DbgIpeMacBridgingInfo_macDaLookupEn_f                       = 2,
    DbgIpeMacBridgingInfo_macDaIsPortMac_f                      = 3,
    DbgIpeMacBridgingInfo_pbbSrcPortType_f                      = 4,
    DbgIpeMacBridgingInfo_layer3Exception_f                     = 5,
    DbgIpeMacBridgingInfo_vsiMcastMacAddress_f                  = 6,
    DbgIpeMacBridgingInfo_vsiBcastMacAddress_f                  = 7,
    DbgIpeMacBridgingInfo_muxLengthType_f                       = 8,
    DbgIpeMacBridgingInfo_dsFwdPtrValid_f                       = 9,
    DbgIpeMacBridgingInfo_ecmpGroupId_f                         = 10,
    DbgIpeMacBridgingInfo_nextHopPtrValid_f                     = 11,
    DbgIpeMacBridgingInfo_discard_f                             = 12,
    DbgIpeMacBridgingInfo_discardType_f                         = 13,
    DbgIpeMacBridgingInfo_dsFwdPtr_f                            = 14,
    DbgIpeMacBridgingInfo_exceptionEn_f                         = 15,
    DbgIpeMacBridgingInfo_exceptionIndex_f                      = 16,
    DbgIpeMacBridgingInfo_exceptionSubIndex_f                   = 17,
    DbgIpeMacBridgingInfo_bridgePacket_f                        = 18,
    DbgIpeMacBridgingInfo_bridgeEscape_f                        = 19,
    DbgIpeMacBridgingInfo_stormCtlEn_f                          = 20,
    DbgIpeMacBridgingInfo_valid_f                               = 21,

    DbgIpeMacLearningInfo_macSaResultPending_f                  = 0,
    DbgIpeMacLearningInfo_macSaDefaultEntryValid_f              = 1,
    DbgIpeMacLearningInfo_macSaHashConflict_f                   = 2,
    DbgIpeMacLearningInfo_macSaResultValid_f                    = 3,
    DbgIpeMacLearningInfo_macSaLookupEn_f                       = 4,
    DbgIpeMacLearningInfo_macSaIsPortMac_f                      = 5,
    DbgIpeMacLearningInfo_fastLearningEn_f                      = 6,
    DbgIpeMacLearningInfo_stackingLearningEn_f                  = 7,
    DbgIpeMacLearningInfo_discard_f                             = 8,
    DbgIpeMacLearningInfo_discardType_f                         = 9,
    DbgIpeMacLearningInfo_exceptionEn_f                         = 10,
    DbgIpeMacLearningInfo_exceptionIndex_f                      = 11,
    DbgIpeMacLearningInfo_exceptionSubIndex_f                   = 12,
    DbgIpeMacLearningInfo_isInnerMacSaLookup_f                  = 13,
    DbgIpeMacLearningInfo_fid_f                                 = 14,
    DbgIpeMacLearningInfo_learningSrcPort_f                     = 15,
    DbgIpeMacLearningInfo_isGlobalSrcPort_f                     = 16,
    DbgIpeMacLearningInfo_macSecurityDiscard_f                  = 17,
    DbgIpeMacLearningInfo_learningEn_f                          = 18,
    DbgIpeMacLearningInfo_srcMismatchDiscard_f                  = 19,
    DbgIpeMacLearningInfo_portSecurityDiscard_f                 = 20,
    DbgIpeMacLearningInfo_vsiSecurityDiscard_f                  = 21,
    DbgIpeMacLearningInfo_systemSecurityDiscard_f               = 22,
    DbgIpeMacLearningInfo_profileMaxDiscard_f                   = 23,
    DbgIpeMacLearningInfo_portMacCheckDiscard_f                 = 24,
    DbgIpeMacLearningInfo_valid_f                               = 25,

    DbgIpeMplsDecapInfo_tunnelDecap1_f                          = 0,
    DbgIpeMplsDecapInfo_tunnelDecap2_f                          = 1,
    DbgIpeMplsDecapInfo_ipMartianAddress_f                      = 2,
    DbgIpeMplsDecapInfo_labelNum_f                              = 3,
    DbgIpeMplsDecapInfo_isMplsSwitched_f                        = 4,
    DbgIpeMplsDecapInfo_contextLabelExist_f                     = 5,
    DbgIpeMplsDecapInfo_innerPacketLookup_f                     = 6,
    DbgIpeMplsDecapInfo_forceParser_f                           = 7,
    DbgIpeMplsDecapInfo_secondParserForEcmp_f                   = 8,
    DbgIpeMplsDecapInfo_isDecap_f                               = 9,
    DbgIpeMplsDecapInfo_srcDscp_f                               = 10,
    DbgIpeMplsDecapInfo_mplsLabelOutOfRange_f                   = 11,
    DbgIpeMplsDecapInfo_useEntropyLabelHash_f                   = 12,
    DbgIpeMplsDecapInfo_rxOamType_f                             = 13,
    DbgIpeMplsDecapInfo_packetTtl_f                             = 14,
    DbgIpeMplsDecapInfo_isLeaf_f                                = 15,
    DbgIpeMplsDecapInfo_logicPortType_f                         = 16,
    DbgIpeMplsDecapInfo_serviceAclQosEn_f                       = 17,
    DbgIpeMplsDecapInfo_payloadPacketType_f                     = 18,
    DbgIpeMplsDecapInfo_dsFwdPtrValid_f                         = 19,
    DbgIpeMplsDecapInfo_nextHopPtrValid_f                       = 20,
    DbgIpeMplsDecapInfo_isPtpTunnel_f                           = 21,
    DbgIpeMplsDecapInfo_ptpDeepParser_f                         = 22,
    DbgIpeMplsDecapInfo_ptpExtraLabelNum_f                      = 23,
    DbgIpeMplsDecapInfo_dsFwdPtr_f                              = 24,
    DbgIpeMplsDecapInfo_discard_f                               = 25,
    DbgIpeMplsDecapInfo_discardType_f                           = 26,
    DbgIpeMplsDecapInfo_bypassAll_f                             = 27,
    DbgIpeMplsDecapInfo_exceptionEn_f                           = 28,
    DbgIpeMplsDecapInfo_exceptionIndex_f                        = 29,
    DbgIpeMplsDecapInfo_exceptionSubIndex_f                     = 30,
    DbgIpeMplsDecapInfo_fatalExceptionValid_f                   = 31,
    DbgIpeMplsDecapInfo_fatalException_f                        = 32,
    DbgIpeMplsDecapInfo_innerParserOffset_f                     = 33,
    DbgIpeMplsDecapInfo_innerParserPacketType_f                 = 34,
    DbgIpeMplsDecapInfo_mplsLmEn0_f                             = 35,
    DbgIpeMplsDecapInfo_mplsLmEn1_f                             = 36,
    DbgIpeMplsDecapInfo_mplsLmEn2_f                             = 37,
    DbgIpeMplsDecapInfo_firstLabel_f                            = 38,
    DbgIpeMplsDecapInfo_valid_f                                 = 39,

    DbgIpeOamInfo_oamLookupEn_f                                 = 0,
    DbgIpeOamInfo_lmStatsEn0_f                                  = 1,
    DbgIpeOamInfo_lmStatsEn1_f                                  = 2,
    DbgIpeOamInfo_lmStatsEn2_f                                  = 3,
    DbgIpeOamInfo_lmStatsIndex_f                                = 4,
    DbgIpeOamInfo_lmStatsPtr0_f                                 = 5,
    DbgIpeOamInfo_lmStatsPtr1_f                                 = 6,
    DbgIpeOamInfo_lmStatsPtr2_f                                 = 7,
    DbgIpeOamInfo_etherLmValid_f                                = 8,
    DbgIpeOamInfo_mplsLmValid_f                                 = 9,
    DbgIpeOamInfo_oamDestChipId_f                               = 10,
    DbgIpeOamInfo_mepIndex_f                                    = 11,
    DbgIpeOamInfo_mepEn_f                                       = 12,
    DbgIpeOamInfo_mipEn_f                                       = 13,
    DbgIpeOamInfo_tempRxOamType_f                               = 14,
    DbgIpeOamInfo_valid_f                                       = 15,

    DbgIpeTrillInfo_l3DaResultValid_f                           = 0,
    DbgIpeTrillInfo_isTrillUcast_f                              = 1,
    DbgIpeTrillInfo_isTrillMcast_f                              = 2,
    DbgIpeTrillInfo_isAllRbridgesMac_f                          = 3,
    DbgIpeTrillInfo_trillVersionMatch_f                         = 4,
    DbgIpeTrillInfo_dsFwdPtrValid_f                             = 5,
    DbgIpeTrillInfo_ecmpGroupId_f                               = 6,
    DbgIpeTrillInfo_nextHopPtrValid_f                           = 7,
    DbgIpeTrillInfo_dsFwdPtr_f                                  = 8,
    DbgIpeTrillInfo_valid_f                                     = 9,

    DbgIpeUserIdInfo_localPhyPort_f                             = 0,
    DbgIpeUserIdInfo_globalSrcPort_f                            = 1,
    DbgIpeUserIdInfo_packetType_f                               = 2,
    DbgIpeUserIdInfo_timestampValid_f                           = 3,
    DbgIpeUserIdInfo_timestamp_f                                = 4,
    DbgIpeUserIdInfo_outerVlanIsCVlan_f                         = 5,
    DbgIpeUserIdInfo_fwdHashGenDis_f                            = 6,
    DbgIpeUserIdInfo_vlanRangeValid_f                           = 7,
    DbgIpeUserIdInfo_tunnelLookupResult1Valid_f                 = 8,
    DbgIpeUserIdInfo_tunnelLookupResult2Valid_f                 = 9,
    DbgIpeUserIdInfo_discard_f                                  = 10,
    DbgIpeUserIdInfo_discardType_f                              = 11,
    DbgIpeUserIdInfo_bypassAll_f                                = 12,
    DbgIpeUserIdInfo_exceptionEn_f                              = 13,
    DbgIpeUserIdInfo_exceptionIndex_f                           = 14,
    DbgIpeUserIdInfo_exceptionSubIndex_f                        = 15,
    DbgIpeUserIdInfo_dsFwdPtrValid_f                            = 16,
    DbgIpeUserIdInfo_dsFwdPtr_f                                 = 17,
    DbgIpeUserIdInfo_userIdHash1Type_f                          = 18,
    DbgIpeUserIdInfo_userIdHash2Type_f                          = 19,
    DbgIpeUserIdInfo_flowFieldSel_f                             = 20,
    DbgIpeUserIdInfo_sclKeyType1_f                              = 21,
    DbgIpeUserIdInfo_sclKeyType2_f                              = 22,
    DbgIpeUserIdInfo_userIdTcam1En_f                            = 23,
    DbgIpeUserIdInfo_userIdTcam2En_f                            = 24,
    DbgIpeUserIdInfo_userIdTcam1Type_f                          = 25,
    DbgIpeUserIdInfo_userIdTcam2Type_f                          = 26,
    DbgIpeUserIdInfo_sclFlowHashEn_f                            = 27,
    DbgIpeUserIdInfo_valid_f                                    = 28,

    DbgIpfixAccEgrInfo_sliceId_f                                = 0,
    DbgIpfixAccEgrInfo_isLayer2Mcast_f                          = 1,
    DbgIpfixAccEgrInfo_isLayer3Mcast_f                          = 2,
    DbgIpfixAccEgrInfo_isBcast_f                                = 3,
    DbgIpfixAccEgrInfo_isUnknownPkt_f                           = 4,
    DbgIpfixAccEgrInfo_packetLength_f                           = 5,
    DbgIpfixAccEgrInfo_destMap_f                                = 6,
    DbgIpfixAccEgrInfo_discard_f                                = 7,
    DbgIpfixAccEgrInfo_useApsGroup_f                            = 8,
    DbgIpfixAccEgrInfo_useEcmpGroup_f                           = 9,
    DbgIpfixAccEgrInfo_ipfixDisable_f                           = 10,
    DbgIpfixAccEgrInfo_resultValid_f                            = 11,
    DbgIpfixAccEgrInfo_hashConflict_f                           = 12,
    DbgIpfixAccEgrInfo_keyIndex_f                               = 13,
    DbgIpfixAccEgrInfo_hashKeyType_f                            = 14,
    DbgIpfixAccEgrInfo_localPhyPort_f                           = 15,
    DbgIpfixAccEgrInfo_samplingEn_f                             = 16,
    DbgIpfixAccEgrInfo_isSamplingPkt_f                          = 17,
    DbgIpfixAccEgrInfo_byPassIpfixProcess_f                     = 18,
    DbgIpfixAccEgrInfo_isUpdateOperation_f                      = 19,
    DbgIpfixAccEgrInfo_isAddOperation_f                         = 20,
    DbgIpfixAccEgrInfo_valid_f                                  = 21,

    DbgIpfixAccIngInfo_isLayer2Mcast_f                          = 0,
    DbgIpfixAccIngInfo_isLayer3Mcast_f                          = 1,
    DbgIpfixAccIngInfo_isBcast_f                                = 2,
    DbgIpfixAccIngInfo_isUnknownPkt_f                           = 3,
    DbgIpfixAccIngInfo_packetLength_f                           = 4,
    DbgIpfixAccIngInfo_destMap_f                                = 5,
    DbgIpfixAccIngInfo_discard_f                                = 6,
    DbgIpfixAccIngInfo_useApsGroup_f                            = 7,
    DbgIpfixAccIngInfo_useEcmpGroup_f                           = 8,
    DbgIpfixAccIngInfo_ipfixDisable_f                           = 9,
    DbgIpfixAccIngInfo_resultValid_f                            = 10,
    DbgIpfixAccIngInfo_hashConflict_f                           = 11,
    DbgIpfixAccIngInfo_keyIndex_f                               = 12,
    DbgIpfixAccIngInfo_hashKeyType_f                            = 13,
    DbgIpfixAccIngInfo_localPhyPort_f                           = 14,
    DbgIpfixAccIngInfo_samplingEn_f                             = 15,
    DbgIpfixAccIngInfo_isSamplingPkt_f                          = 16,
    DbgIpfixAccIngInfo_byPassIpfixProcess_f                     = 17,
    DbgIpfixAccIngInfo_isUpdateOperation_f                      = 18,
    DbgIpfixAccIngInfo_isAddOperation_f                         = 19,
    DbgIpfixAccIngInfo_valid_f                                  = 20,

    DbgLearningEngineInfo_fastLearningEn_f                      = 0,
    DbgLearningEngineInfo_learningSrcPort_f                     = 1,
    DbgLearningEngineInfo_isGlobalSrcPort_f                     = 2,
    DbgLearningEngineInfo_vsiId_f                               = 3,
    DbgLearningEngineInfo_macSa_f                               = 4,
    DbgLearningEngineInfo_isEtherOam_f                          = 5,
    DbgLearningEngineInfo_etherOamLevel_f                       = 6,
    DbgLearningEngineInfo_newSvlanId_f                          = 7,
    DbgLearningEngineInfo_newCvlanId_f                          = 8,
    DbgLearningEngineInfo_learningCacheMiss_f                   = 9,
    DbgLearningEngineInfo_learningCacheFull_f                   = 10,
    DbgLearningEngineInfo_learningCacheInt_f                    = 11,
    DbgLearningEngineInfo_valid_f                               = 12,

    DbgOamDefectMsg_rmepIndex_f                                 = 0,
    DbgOamDefectMsg_signalFail_f                                = 1,
    DbgOamDefectMsg_mepIndex_f                                  = 2,
    DbgOamDefectMsg_valid_f                                     = 3,

    DbgOamDefectProc_defectType_f                               = 0,
    DbgOamDefectProc_defectSubType_f                            = 1,
    DbgOamDefectProc_rmepIndex_f                                = 2,
    DbgOamDefectProc_mepIndex_f                                 = 3,
    DbgOamDefectProc_valid_f                                    = 4,

    DbgOamHdrAdj_bridgeHeader_f                                 = 0,
    DbgOamHdrAdj_mepIndex_f                                     = 1,
    DbgOamHdrAdj_valid_f                                        = 2,

    DbgOamHdrEdit_mepIndex_f                                    = 0,
    DbgOamHdrEdit_exception_f                                   = 1,
    DbgOamHdrEdit_bridgeHeader_f                                = 2,
    DbgOamHdrEdit_valid_f                                       = 3,

    DbgOamRxProc_exception_f                                    = 0,
    DbgOamRxProc_rmepIndex_f                                    = 1,
    DbgOamRxProc_mepIndex_f                                     = 2,
    DbgOamRxProc_valid_f                                        = 3,

    DbgPolicerEngineEgrInfo_sliceId_f                           = 0,
    DbgPolicerEngineEgrInfo_color_f                             = 1,
    DbgPolicerEngineEgrInfo_policerValid0_f                     = 2,
    DbgPolicerEngineEgrInfo_policerPtr0_f                       = 3,
    DbgPolicerEngineEgrInfo_policerValid1_f                     = 4,
    DbgPolicerEngineEgrInfo_policerPtr1_f                       = 5,
    DbgPolicerEngineEgrInfo_newColor0_f                         = 6,
    DbgPolicerEngineEgrInfo_newColor1_f                         = 7,
    DbgPolicerEngineEgrInfo_markDrop0_f                         = 8,
    DbgPolicerEngineEgrInfo_markDrop1_f                         = 9,
    DbgPolicerEngineEgrInfo_valid_f                             = 10,

    DbgPolicerEngineIngInfo_sliceId_f                           = 0,
    DbgPolicerEngineIngInfo_color_f                             = 1,
    DbgPolicerEngineIngInfo_policerValid0_f                     = 2,
    DbgPolicerEngineIngInfo_policerPtr0_f                       = 3,
    DbgPolicerEngineIngInfo_policerValid1_f                     = 4,
    DbgPolicerEngineIngInfo_policerPtr1_f                       = 5,
    DbgPolicerEngineIngInfo_newColor0_f                         = 6,
    DbgPolicerEngineIngInfo_newColor1_f                         = 7,
    DbgPolicerEngineIngInfo_markDrop0_f                         = 8,
    DbgPolicerEngineIngInfo_markDrop1_f                         = 9,
    DbgPolicerEngineIngInfo_valid_f                             = 10,

    DbgQMgrEnqInfo_queId_f                                      = 0,
    DbgQMgrEnqInfo_basedOnCellCnt_f                             = 1,
    DbgQMgrEnqInfo_repCntBase_f                                 = 2,
    DbgQMgrEnqInfo_replicationCnt_f                             = 3,
    DbgQMgrEnqInfo_c2cPacket_f                                  = 4,
    DbgQMgrEnqInfo_criticalPacket_f                             = 5,
    DbgQMgrEnqInfo_wredDropMode_f                               = 6,
    DbgQMgrEnqInfo_needEcnProc_f                                = 7,
    DbgQMgrEnqInfo_dropPri_f                                    = 8,
    DbgQMgrEnqInfo_netPktAdjIndex_f                             = 9,
    DbgQMgrEnqInfo_statsUpdEn_f                                 = 10,
    DbgQMgrEnqInfo_resvCh0_f                                    = 11,
    DbgQMgrEnqInfo_resvCh1_f                                    = 12,
    DbgQMgrEnqInfo_forceNoDrop_f                                = 13,
    DbgQMgrEnqInfo_mappedTc_f                                   = 14,
    DbgQMgrEnqInfo_mappedSc_f                                   = 15,
    DbgQMgrEnqInfo_queMinProfId_f                               = 16,
    DbgQMgrEnqInfo_queDropProfExt_f                             = 17,
    DbgQMgrEnqInfo_chanIdIsNet_f                                = 18,
    DbgQMgrEnqInfo_cond1_f                                      = 19,
    DbgQMgrEnqInfo_cond2_f                                      = 20,
    DbgQMgrEnqInfo_cond3_f                                      = 21,
    DbgQMgrEnqInfo_cond4_f                                      = 22,
    DbgQMgrEnqInfo_cond5_f                                      = 23,
    DbgQMgrEnqInfo_cond6_f                                      = 24,
    DbgQMgrEnqInfo_cond7_f                                      = 25,
    DbgQMgrEnqInfo_cond8_f                                      = 26,
    DbgQMgrEnqInfo_cond9_f                                      = 27,
    DbgQMgrEnqInfo_cond10_f                                     = 28,
    DbgQMgrEnqInfo_currentCongest_f                             = 29,
    DbgQMgrEnqInfo_isLowTc_f                                    = 30,
    DbgQMgrEnqInfo_isHighCongest_f                              = 31,
    DbgQMgrEnqInfo_cond11_f                                     = 32,
    DbgQMgrEnqInfo_discardingDrop_f                             = 33,
    DbgQMgrEnqInfo_rsvDrop_f                                    = 34,
    DbgQMgrEnqInfo_enqueueDrop_f                                = 35,
    DbgQMgrEnqInfo_linkListDrop_f                               = 36,
    DbgQMgrEnqInfo_rcdUpdateEn_f                                = 37,
    DbgQMgrEnqInfo_pktReleaseEn_f                               = 38,
    DbgQMgrEnqInfo_resrcCntUpd_f                                = 39,
    DbgQMgrEnqInfo_bufCell_f                                    = 40,
    DbgQMgrEnqInfo_skipEnqueue_f                                = 41,
    DbgQMgrEnqInfo_adjustPktLen_f                               = 42,
    DbgQMgrEnqInfo_valid_f                                      = 43,

    DbgStormCtlEngineInfo_portStormCtlEn_f                      = 0,
    DbgStormCtlEngineInfo_dsPortStormCtlPtr_f                   = 1,
    DbgStormCtlEngineInfo_portStormCtlDrop_f                    = 2,
    DbgStormCtlEngineInfo_portStormCtlThresholdNotFull_f        = 3,
    DbgStormCtlEngineInfo_vlanStormCtlEn_f                      = 4,
    DbgStormCtlEngineInfo_dsVlanStormCtlPtr_f                   = 5,
    DbgStormCtlEngineInfo_vlanStormCtlDrop_f                    = 6,
    DbgStormCtlEngineInfo_vlanStormCtlThresholdNotFull_f        = 7,
    DbgStormCtlEngineInfo_stormCtlExceptionEn_f                 = 8,
    DbgStormCtlEngineInfo_valid_f                               = 9,

    DbgUserIdHashEngineForMpls0Info_lookupResultValid_f         = 0,
    DbgUserIdHashEngineForMpls0Info_defaultEntryValid_f         = 1,
    DbgUserIdHashEngineForMpls0Info_keyIndex_f                  = 2,
    DbgUserIdHashEngineForMpls0Info_adIndex_f                   = 3,
    DbgUserIdHashEngineForMpls0Info_hashConflict_f              = 4,
    DbgUserIdHashEngineForMpls0Info_lookupResultValid0_f        = 5,
    DbgUserIdHashEngineForMpls0Info_keyIndex0_f                 = 6,
    DbgUserIdHashEngineForMpls0Info_lookupResultValid1_f        = 7,
    DbgUserIdHashEngineForMpls0Info_keyIndex1_f                 = 8,
    DbgUserIdHashEngineForMpls0Info_camLookupResultValid_f      = 9,
    DbgUserIdHashEngineForMpls0Info_keyIndexCam_f               = 10,
    DbgUserIdHashEngineForMpls0Info_valid_f                     = 11,

    DbgUserIdHashEngineForMpls1Info_lookupResultValid_f         = 0,
    DbgUserIdHashEngineForMpls1Info_defaultEntryValid_f         = 1,
    DbgUserIdHashEngineForMpls1Info_keyIndex_f                  = 2,
    DbgUserIdHashEngineForMpls1Info_adIndex_f                   = 3,
    DbgUserIdHashEngineForMpls1Info_hashConflict_f              = 4,
    DbgUserIdHashEngineForMpls1Info_lookupResultValid0_f        = 5,
    DbgUserIdHashEngineForMpls1Info_keyIndex0_f                 = 6,
    DbgUserIdHashEngineForMpls1Info_lookupResultValid1_f        = 7,
    DbgUserIdHashEngineForMpls1Info_keyIndex1_f                 = 8,
    DbgUserIdHashEngineForMpls1Info_camLookupResultValid_f      = 9,
    DbgUserIdHashEngineForMpls1Info_keyIndexCam_f               = 10,
    DbgUserIdHashEngineForMpls1Info_valid_f                     = 11,

    DbgUserIdHashEngineForMpls2Info_lookupResultValid_f         = 0,
    DbgUserIdHashEngineForMpls2Info_defaultEntryValid_f         = 1,
    DbgUserIdHashEngineForMpls2Info_keyIndex_f                  = 2,
    DbgUserIdHashEngineForMpls2Info_adIndex_f                   = 3,
    DbgUserIdHashEngineForMpls2Info_hashConflict_f              = 4,
    DbgUserIdHashEngineForMpls2Info_lookupResultValid0_f        = 5,
    DbgUserIdHashEngineForMpls2Info_keyIndex0_f                 = 6,
    DbgUserIdHashEngineForMpls2Info_lookupResultValid1_f        = 7,
    DbgUserIdHashEngineForMpls2Info_keyIndex1_f                 = 8,
    DbgUserIdHashEngineForMpls2Info_camLookupResultValid_f      = 9,
    DbgUserIdHashEngineForMpls2Info_keyIndexCam_f               = 10,
    DbgUserIdHashEngineForMpls2Info_valid_f                     = 11,

    DbgUserIdHashEngineForUserId0Info_lookupResultValid_f       = 0,
    DbgUserIdHashEngineForUserId0Info_defaultEntryValid_f       = 1,
    DbgUserIdHashEngineForUserId0Info_keyIndex_f                = 2,
    DbgUserIdHashEngineForUserId0Info_adIndex_f                 = 3,
    DbgUserIdHashEngineForUserId0Info_hashConflict_f            = 4,
    DbgUserIdHashEngineForUserId0Info_lookupResultValid0_f      = 5,
    DbgUserIdHashEngineForUserId0Info_keyIndex0_f               = 6,
    DbgUserIdHashEngineForUserId0Info_lookupResultValid1_f      = 7,
    DbgUserIdHashEngineForUserId0Info_keyIndex1_f               = 8,
    DbgUserIdHashEngineForUserId0Info_camLookupResultValid_f    = 9,
    DbgUserIdHashEngineForUserId0Info_keyIndexCam_f             = 10,
    DbgUserIdHashEngineForUserId0Info_valid_f                   = 11,

    DbgUserIdHashEngineForUserId1Info_lookupResultValid_f       = 0,
    DbgUserIdHashEngineForUserId1Info_defaultEntryValid_f       = 1,
    DbgUserIdHashEngineForUserId1Info_keyIndex_f                = 2,
    DbgUserIdHashEngineForUserId1Info_adIndex_f                 = 3,
    DbgUserIdHashEngineForUserId1Info_hashConflict_f            = 4,
    DbgUserIdHashEngineForUserId1Info_lookupResultValid0_f      = 5,
    DbgUserIdHashEngineForUserId1Info_keyIndex0_f               = 6,
    DbgUserIdHashEngineForUserId1Info_lookupResultValid1_f      = 7,
    DbgUserIdHashEngineForUserId1Info_keyIndex1_f               = 8,
    DbgUserIdHashEngineForUserId1Info_camLookupResultValid_f    = 9,
    DbgUserIdHashEngineForUserId1Info_keyIndexCam_f             = 10,
    DbgUserIdHashEngineForUserId1Info_valid_f                   = 11
} dkit_fld_id_t;

#if 1
/* module id check macro define */
enum dkit_modules_id_e
{
   BufRetrv_mod,                                                  /*       0 */
   BufStore_mod,                                                  /*       1 */
   CgPcs_mod,                                                     /*       2 */
   Cgmac_mod,                                                     /*       3 */
   DSANDTCAM_mod,                                                 /*       4 */
   DmaCtl_mod,                                                    /*       5 */
   DsAging_mod,                                                   /*       6 */
   DynamicDsAd_mod,                                               /*       7 */
   DynamicDsHash_mod,                                             /*       8 */
   DynamicDsShare_mod,                                            /*       9 */
   EgrOamHash_mod,                                                /*      10 */
   EpeAclOam_mod,                                                 /*      11 */
   EpeHdrAdj_mod,                                                 /*      12 */
   EpeHdrEdit_mod,                                                /*      13 */
   EpeHdrProc_mod,                                                /*      14 */
   EpeNextHop_mod,                                                /*      15 */
   FibAcc_mod,                                                    /*      16 */
   FibEngine_mod,                                                 /*      17 */
   FlowAcc_mod,                                                   /*      18 */
   FlowHash_mod,                                                  /*      19 */
   FlowTcam_mod,                                                  /*      20 */
   GlobalStats_mod,                                               /*      21 */
   Hss15GUnitWrapper_mod,                                         /*      22 */
   Hss28GUnitWrapper_mod,                                         /*      23 */
   HssAnethWrapper_mod,                                           /*      24 */
   I2CMaster_mod,                                                 /*      25 */
   IpeForward_mod,                                                /*      26 */
   IpeHdrAdj_mod,                                                 /*      27 */
   IpeIntfMap_mod,                                                /*      28 */
   IpeLkupMgr_mod,                                                /*      29 */
   IpePktProc_mod,                                                /*      30 */
   LpmTcamIp_mod,                                                 /*      31 */
   LpmTcamNat_mod,                                                /*      32 */
   MacLed_mod,                                                    /*      33 */
   Mdio_mod,                                                      /*      34 */
   MetFifo_mod,                                                   /*      35 */
   NetRx_mod,                                                     /*      36 */
   NetTx_mod,                                                     /*      37 */
   OamAutoGenPkt_mod,                                             /*      38 */
   OamFwd_mod,                                                    /*      39 */
   OamParser_mod,                                                 /*      40 */
   OamProc_mod,                                                   /*      41 */
   OobFc_mod,                                                     /*      42 */
   Parser_mod,                                                    /*      43 */
   PbCtl_mod,                                                     /*      44 */
   PcieIntf_mod,                                                  /*      45 */
   PktProcDs_mod,                                                 /*      46 */
   Policing_mod,                                                  /*      47 */
   QMgrDeqSlice_mod,                                              /*      48 */
   QMgrEnq_mod,                                                   /*      49 */
   QMgrMsgStore_mod,                                              /*      50 */
   RefTrigger_mod,                                                /*      51 */
   RlmAdCtl_mod,                                                  /*      52 */
   RlmBrPbCtl_mod,                                                /*      53 */
   RlmBsCtl_mod,                                                  /*      54 */
   RlmCsCtl_mod,                                                  /*      55 */
   RlmDeqCtl_mod,                                                 /*      56 */
   RlmEnqCtl_mod,                                                 /*      57 */
   RlmEpeCtl_mod,                                                 /*      58 */
   RlmHashCtl_mod,                                                /*      59 */
   RlmHsCtl_mod,                                                  /*      60 */
   RlmIpeCtl_mod,                                                 /*      61 */
   RlmNetCtl_mod,                                                 /*      62 */
   RlmShareDsCtl_mod,                                             /*      63 */
   RlmShareTcamCtl_mod,                                           /*      64 */
   ShareDlb_mod,                                                  /*      65 */
   ShareEfd_mod,                                                  /*      66 */
   ShareStormCtl_mod,                                             /*      67 */
   SharedPcs_mod,                                                 /*      68 */
   SupClockTree_mod,                                              /*      69 */
   SupCtl_mod,                                                    /*      70 */
   SupMacro_mod,                                                  /*      71 */
   TsEngineRef_mod,                                               /*      72 */
   TsEngine_mod,                                                  /*      73 */
   UserIdHash_mod,                                                /*      74 */
   Xqmac_mod,                                                     /*      75 */
   _DynamicDsAd_mod,                                              /*      76 */
   _DynamicDsHash_mod,                                            /*      77 */
   _DynamicDsShare_mod,                                           /*      78 */
   _FlowTcam_mod,                                                 /*      79 */
   _LpmTcamIp_mod,                                                /*      80 */
   _LpmTcamNat_mod,                                               /*      81 */
   _SdkCmodelBus_mod,                                             /*      82 */
   Mod_IPE,                                                      /*      83 */
   Mod_EPE,                                                      /*      84 */
   Mod_BSR,                                                      /*      85 */
   Mod_SHARE,                                                    /*      86 */
   Mod_MISC,                                                     /*      87 */
   Mod_RLM,                                                      /*      88 */
   Mod_MAC,                                                      /*      89 */
   Mod_OAM,                                                      /*      90 */
   Mod_ALL,                                                      /*      91 */

   MaxModId_m
};
typedef enum dkit_modules_id_e dkit_mod_id_t;
#endif


#if (HOST_IS_LE == 1)
struct parser_result_s {
   uint32 u_l4_user_data_g_ptp_udp_ptp_message_type                        :4;
   uint32 u_l4_user_data_g_ptp_udp_ptp_version                             :2;
   uint32 u_l4_user_data_g_ptp_udp_ptp_correction_field25_0              :26;

   uint32 u_l4_user_data_g_ptp_udp_ptp_correction_field57_26             :32;

   uint32 u_l4_user_data_g_ptp_udp_ptp_correction_field63_58             :6;
   uint32 u_l4_dest_g_port_l4_dest_port                                    :16;
   uint32 u_l4_source_g_port_l4_source_port9_0                           :10;

   uint32 u_l4_source_g_gre_gre_flags15_10                               :6;
   uint32 u_l3_tos_g_mpls_label_num                                        :4;
   uint32 u_l3_tos_g_mpls_ip_over_mpls                                     :1;
   uint32 u_l3_tos_g_mpls_mpls_inner_hdr_hash_value                        :8;
   uint32 u_l3_source_g_ipv6_ip_sa12_0                                   :13;

   uint32 u_l3_source_g_ipv6_ip_sa44_13                                  :32;

   uint32 u_l3_source_g_ipv6_ip_sa76_45                                  :32;

   uint32 u_l3_source_g_ipv6_ip_sa108_77                                 :32;

   uint32 u_l3_source_g_ipv6_ip_sa127_109                                :19;
   uint32 u_l3_dest_g_ipv6_ip_da12_0                                     :13;

   uint32 u_l3_dest_g_ipv6_ip_da44_13                                    :32;

   uint32 u_l3_dest_g_ipv6_ip_da76_45                                    :32;

   uint32 u_l3_dest_g_ipv6_ip_da108_77                                   :32;

   uint32 u_l3_dest_g_ipv6_ip_da127_109                                  :19;
   uint32 is_isatap_interface                                              :1;
   uint32 ttl                                                              :8;
   uint32 l4_error_bits3_0                                               :4;

   uint32 l4_error_bits4                                                 :1;
   uint32 ip_length                                                        :14;
   uint32 cvlan_id                                                         :12;
   uint32 svlan_id4_0                                                    :5;

   uint32 svlan_id11_5                                                   :7;
   uint32 cn_flow_id                                                       :16;
   uint32 udp_ptp_timestamp8_0                                           :9;

   uint32 udp_ptp_timestamp40_9                                          :32;

   uint32 udp_ptp_timestamp63_41                                         :23;
   uint32 ip_identification8_0                                           :9;

   uint32 ip_identification15_9                                          :7;
   uint32 frag_offset                                                      :13;
   uint32 layer4_user_type                                                 :4;
   uint32 layer4_checksum7_0                                             :8;

   uint32 layer4_checksum15_8                                            :8;
   uint32 l4_info_mapped                                                   :12;
   uint32 is_udp                                                           :1;
   uint32 is_tcp                                                           :1;
   uint32 more_frag                                                        :1;
   uint32 layer3_header_protocol                                           :8;
   uint32 layer4_offset0                                                 :1;

   uint32 layer4_offset7_1                                               :7;
   uint32 frag_info                                                        :2;
   uint32 dont_frag                                                        :1;
   uint32 ip_options                                                       :1;
   uint32 ip_header_error                                                  :1;
   uint32 ipv6_flow_label                                                  :20;

   uint32 cn_tag_valid                                                     :1;
   uint32 layer3_offset                                                    :8;
   uint32 ether_type                                                       :16;
   uint32 ctag_cfi                                                         :1;
   uint32 stag_cfi                                                         :1;
   uint32 ctag_cos                                                         :3;
   uint32 stag_cos1_0                                                    :2;

   uint32 stag_cos2                                                      :1;
   uint32 cvlan_id_valid                                                   :1;
   uint32 svlan_id_valid                                                   :1;
   uint32 vlan_num                                                         :2;
   uint32 mac_sa26_0                                                     :27;

   uint32 mac_sa47_27                                                    :21;
   uint32 mac_da10_0                                                     :11;

   uint32 mac_da42_11                                                    :32;

   uint32 mac_da47_43                                                    :5;
   uint32 parser_length_error                                              :1;
   uint32 layer4_type                                                      :4;
   uint32 layer3_type                                                      :4;
   uint32 layer2_type                                                      :2;
   uint32 udf15_0                                                        :16;

   uint32 udf31_16                                                       :16;
   uint32 udf_type                                                         :2;
   uint32 local_phy_port                                                   :8;
   uint32 rsv_0                                                            :6;
};
typedef struct parser_result_s parser_result_t;

struct dkits_tcam_lookup_info_s
{
    uint32 acl_lookup_type  :2;
    uint32 acl_label        :8;
    uint32 trill_key_type   :3;
    uint32 scl_key_type     :2;
    uint32 acl_type         :1;
    uint32 acl_en           :1;
    uint32 from_userid      :1;
    uint32 rsv              :14;
};
typedef struct dkits_tcam_lookup_info_s dkits_tcam_lookup_info_t;

struct dkits_tcam_capture_pr_s
{
    uint32 u_l3_tos_7_0                :8;
    uint32 u_l4_dest_15_0              :16;
    uint32 u_l4_source_7_0             :8;

    uint32 u_l4_source_15_8            :8;
    uint32 u_l4_user_data_23_0         :24;

    uint32 u_l4_user_data_31_24        :8;
    uint32 u_l3_source_23_0_7_0        :8;
    uint32 rsv1                        :16;

    uint32 u_l3_source_23_0_23_8       :16;
    uint32 u_l3_source_55_24_39_24     :16;

    uint32 u_l3_source_55_24_55_40     :16;
    uint32 u_l3_source_87_56_71_56     :16;

    uint32 u_l3_source_87_56_87_72     :16;
    uint32 rsv2                        :16;

    uint32 u_l3_source_119_88          :32;

    uint32 u_l3_source_127_120         :8;
    uint32 u_l3_dest_23_0              :24;

    uint32 u_l3_dest_55_24_39_24       :16;
    uint32 rsv4                        :16;

    uint32 u_l3_dest_55_24_55_40       :16;
    uint32 u_l3_dest_87_56_71_56       :16;

    uint32 u_l3_dest_87_56_87_72       :16;
    uint32 u_l3_dest_119_88_103_88     :16;

    uint32 u_l3_dest_119_88_119_104    :16;
    uint32 rsv5                        :16;

    uint32 u_l3_dest_127_120           :8;
    uint32 ipv6_flow_label_7_0         :8;
    uint32 ipv6_flow_label_19_8        :12;
    uint32 ip_options                  :1;
    uint32 l4_info_mapped_2_0          :3;

    uint32 l4_info_mapped_11_3         :9;
    uint32 udf_22_0                    :23;

    uint32 udf_31_23                   :9;
    uint32 frag_info                   :2;
    uint32 ip_header_error             :1;
    uint32 ttl_3_0                     :4;
    uint32 rsv6                        :16;

    uint32 ttl_7_4                     :4;
    uint32 layer4_user_type            :4;
    uint32 layer4_type                 :4;
    uint32 layer3_type                 :4;
    uint32 layer2_type                 :2;
    uint32 ether_type_7_0              :8;
    uint32 ether_type_15_8_13_8        :6;

    uint32 ether_type_15_8_15_14       :2;
    uint32 ctag_cfi                    :1;
    uint32 ctag_cos                    :3;
    uint32 cvlan_id_9_0                :10;
    uint32 cvlan_id_11_10              :2;
    uint32 cvlan_id_valid              :1;
    uint32 stag_cfi                    :1;
    uint32 stag_cos                    :3;
    uint32 svlan_id_2_0                :3;
    uint32 svlan_id_11_3_8_3           :6;

    uint32 svlan_id_11_3_11_9          :3;
    uint32 svlan_id_valid              :1;
    uint32 mac_sa_11_0                 :12;
    uint32 rsv7                        :16;

    uint32 mac_sa_43_12                :32;

    uint32 mac_sa_47_44                :4;
    uint32 mac_da_27_0_11_0            :12;
    uint32 mac_da_27_0_27_12           :16;

    uint32 mac_da_47_28_43_28          :16;
    uint32 rsv8                        :16;

    uint32 mac_da_47_28_47_44          :4;
    uint32 tcp_flag                    :3;
    uint32 layer3_header_protocol      :8;
    uint32 rsv9                        :17;
};
typedef struct dkits_tcam_capture_pr_s dkits_tcam_capture_pr_t;

#else

struct parser_result_s {
   uint32 u_l4_user_data_g_ptp_udp_ptp_correction_field25_0              :26;
   uint32 u_l4_user_data_g_ptp_udp_ptp_version                             :2;
   uint32 u_l4_user_data_g_ptp_udp_ptp_message_type                        :4;

   uint32 u_l4_user_data_g_ptp_udp_ptp_correction_field57_26             :32;

   uint32 u_l4_source_g_port_l4_source_port9_0                           :10;
   uint32 u_l4_dest_g_port_l4_dest_port                                    :16;
   uint32 u_l4_user_data_g_ptp_udp_ptp_correction_field63_58             :6;

   uint32 u_l3_source_g_ipv6_ip_sa12_0                                   :13;
   uint32 u_l3_tos_g_mpls_mpls_inner_hdr_hash_value                        :8;
   uint32 u_l3_tos_g_mpls_ip_over_mpls                                     :1;
   uint32 u_l3_tos_g_mpls_label_num                                        :4;
   uint32 u_l4_source_g_gre_gre_flags15_10                               :6;

   uint32 u_l3_source_g_ipv6_ip_sa44_13                                  :32;

   uint32 u_l3_source_g_ipv6_ip_sa76_45                                  :32;

   uint32 u_l3_source_g_ipv6_ip_sa108_77                                 :32;

   uint32 u_l3_dest_g_ipv6_ip_da12_0                                     :13;
   uint32 u_l3_source_g_ipv6_ip_sa127_109                                :19;

   uint32 u_l3_dest_g_ipv6_ip_da44_13                                    :32;

   uint32 u_l3_dest_g_ipv6_ip_da76_45                                    :32;

   uint32 u_l3_dest_g_ipv6_ip_da108_77                                   :32;

   uint32 l4_error_bits3_0                                               :4;
   uint32 ttl                                                              :8;
   uint32 is_isatap_interface                                              :1;
   uint32 u_l3_dest_g_ipv6_ip_da127_109                                  :19;

   uint32 svlan_id4_0                                                    :5;
   uint32 cvlan_id                                                         :12;
   uint32 ip_length                                                        :14;
   uint32 l4_error_bits4                                                 :1;

   uint32 udp_ptp_timestamp8_0                                           :9;
   uint32 cn_flow_id                                                       :16;
   uint32 svlan_id11_5                                                   :7;

   uint32 udp_ptp_timestamp40_9                                          :32;

   uint32 ip_identification8_0                                           :9;
   uint32 udp_ptp_timestamp63_41                                         :23;

   uint32 layer4_checksum7_0                                             :8;
   uint32 layer4_user_type                                                 :4;
   uint32 frag_offset                                                      :13;
   uint32 ip_identification15_9                                          :7;

   uint32 layer4_offset0                                                 :1;
   uint32 layer3_header_protocol                                           :8;
   uint32 more_frag                                                        :1;
   uint32 is_tcp                                                           :1;
   uint32 is_udp                                                           :1;
   uint32 l4_info_mapped                                                   :12;
   uint32 layer4_checksum15_8                                            :8;

   uint32 ipv6_flow_label                                                  :20;
   uint32 ip_header_error                                                  :1;
   uint32 ip_options                                                       :1;
   uint32 dont_frag                                                        :1;
   uint32 frag_info                                                        :2;
   uint32 layer4_offset7_1                                               :7;

   uint32 stag_cos1_0                                                    :2;
   uint32 ctag_cos                                                         :3;
   uint32 stag_cfi                                                         :1;
   uint32 ctag_cfi                                                         :1;
   uint32 ether_type                                                       :16;
   uint32 layer3_offset                                                    :8;
   uint32 cn_tag_valid                                                     :1;

   uint32 mac_sa26_0                                                     :27;
   uint32 vlan_num                                                         :2;
   uint32 svlan_id_valid                                                   :1;
   uint32 cvlan_id_valid                                                   :1;
   uint32 stag_cos2                                                      :1;

   uint32 mac_da10_0                                                     :11;
   uint32 mac_sa47_27                                                    :21;

   uint32 mac_da42_11                                                    :32;

   uint32 udf15_0                                                        :16;
   uint32 layer2_type                                                      :2;
   uint32 layer3_type                                                      :4;
   uint32 layer4_type                                                      :4;
   uint32 parser_length_error                                              :1;
   uint32 mac_da47_43                                                    :5;

   uint32 rsv_0                                                            :6;
   uint32 local_phy_port                                                   :8;
   uint32 udf_type                                                         :2;
   uint32 udf31_16                                                       :16;
};
typedef struct parser_result_s parser_result_t;

struct dkits_tcam_lookup_info_s
{
    uint32 rsv              :14;
    uint32 from_userid      :1;
    uint32 acl_en           :1;
    uint32 acl_type         :1;
    uint32 scl_key_type     :2;
    uint32 trill_key_type   :3;
    uint32 acl_label        :8;
    uint32 acl_lookup_type  :2;
};
typedef struct dkits_tcam_lookup_info_s dkits_tcam_lookup_info_t;

struct dkits_tcam_capture_pr_s
{
    uint32 u_l4_source_7_0             :8;
    uint32 u_l4_dest_15_0              :16;
    uint32 u_l3_tos_7_0                :8;

    uint32 u_l4_user_data_23_0         :24;
    uint32 u_l4_source_15_8            :8;

    uint32 rsv1                        :16;
    uint32 u_l3_source_23_0_7_0        :8;
    uint32 u_l4_user_data_31_24        :8;

    uint32 u_l3_source_55_24_39_24     :16;
    uint32 u_l3_source_23_0_23_8       :16;

    uint32 u_l3_source_87_56_71_56     :16;
    uint32 u_l3_source_55_24_55_40     :16;

    uint32 rsv2                        :16;
    uint32 u_l3_source_87_56_87_72     :16;

    uint32 u_l3_source_119_88          :32;

    uint32 u_l3_dest_23_0              :24;
    uint32 u_l3_source_127_120         :8;

    uint32 rsv4                        :16;
    uint32 u_l3_dest_55_24_39_24       :16;

    uint32 u_l3_dest_87_56_71_56       :16;
    uint32 u_l3_dest_55_24_55_40       :16;

    uint32 u_l3_dest_119_88_103_88     :16;
    uint32 u_l3_dest_87_56_87_72       :16;

    uint32 rsv5                        :16;
    uint32 u_l3_dest_119_88_119_104    :16;

    uint32 l4_info_mapped_2_0          :3;
    uint32 ip_options                  :1;
    uint32 ipv6_flow_label_19_8        :12;
    uint32 ipv6_flow_label_7_0         :8;
    uint32 u_l3_dest_127_120           :8;

    uint32 udf_22_0                    :23;
    uint32 l4_info_mapped_11_3         :9;

    uint32 rsv6                        :16;
    uint32 ttl_3_0                     :4;
    uint32 ip_header_error             :1;
    uint32 frag_info                   :2;
    uint32 udf_31_23                   :9;

    uint32 ether_type_15_8_13_8        :6;
    uint32 ether_type_7_0              :8;
    uint32 layer2_type                 :2;
    uint32 layer3_type                 :4;
    uint32 layer4_type                 :4;
    uint32 layer4_user_type            :4;
    uint32 ttl_7_4                     :4;

    uint32 svlan_id_11_3_8_3           :6;
    uint32 svlan_id_2_0                :3;
    uint32 stag_cos                    :3;
    uint32 stag_cfi                    :1;
    uint32 cvlan_id_valid              :1;
    uint32 cvlan_id_11_10              :2;
    uint32 cvlan_id_9_0                :10;
    uint32 ctag_cos                    :3;
    uint32 ctag_cfi                    :1;
    uint32 ether_type_15_8_15_14       :2;

    uint32 rsv7                        :16;
    uint32 mac_sa_11_0                 :12;
    uint32 svlan_id_valid              :1;
    uint32 svlan_id_11_3_11_9          :3;

    uint32 mac_sa_43_12                :32;

    uint32 mac_da_27_0_27_12           :16;
    uint32 mac_da_27_0_11_0            :12;
    uint32 mac_sa_47_44                :4;

    uint32 rsv8                        :16;
    uint32 mac_da_47_28_43_28          :16;

    uint32 rsv9                        :17;
    uint32 layer3_header_protocol      :8;
    uint32 tcp_flag                    :3;
    uint32 mac_da_47_28_47_44          :4;
};
typedef struct dkits_tcam_capture_pr_s dkits_tcam_capture_pr_t;

#endif

extern dkit_bus_t gg_dkit_bus_list[];
extern dkit_modules_t gg_dkit_modules_list[MaxModId_m];

extern int32
ctc_goldengate_dkit_drv_register(void);

extern int32
ctc_goldengate_dkit_drv_read_bus_field(uint8 lchip, char* field_name, uint32 bus_id, uint32 index, void* value);

extern uint32
ctc_goldengate_dkit_drv_get_bus_list_num(void);

extern bool
ctc_goldengate_dkit_drv_get_tbl_id_by_bus_name(char *str, uint32 *tbl_id);

extern bool
ctc_goldengate_dkit_drv_get_module_id_by_string(char *str, uint32 *id);

#ifdef __cplusplus
}
#endif

#endif


