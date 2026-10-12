#include "sal.h"
#include "ctc_cli.h"
#include "greatbelt/include/drv_lib.h"
#include "ctc_greatbelt_dkit_drv.h"
#include "ctc_greatbelt_dkit.h"

#define BUSINFO___________________________________________

static fields_t bufRetrvPktBufTrack_field[] = {
    { 6,  0, 25, "channelId"                             },  /* bufRetrvPktBufTrack_channelId_f*/
    { 1,  0, 24, "noBufError"                            },  /* bufRetrvPktBufTrack_noBufError_f*/
    { 1,  0, 23, "error"                                 },  /* bufRetrvPktBufTrack_error_f*/
    { 1,  0, 22, "sop"                                   },  /* bufRetrvPktBufTrack_sop_f*/
    { 1,  0, 21, "eop"                                   },  /* bufRetrvPktBufTrack_eop_f*/
    { 1,  0, 20, "full"                                  },  /* bufRetrvPktBufTrack_full_f*/
    { 2,  0, 18, "validWords"                            },  /* bufRetrvPktBufTrack_validWords_f*/
    { 6,  0, 12, "bufCnt"                                },  /* bufRetrvPktBufTrack_bufCnt_f*/
    {12,  0,  0, "tailBufPtr_1"                          },  /* bufRetrvPktBufTrack_tailBufPtr_1_f*/
    { 2,  1, 30, "tailBufPtr_0"                          },  /* bufRetrvPktBufTrack_tailBufPtr_0_f*/
    { 2,  1, 28, "headBufOffset"                         },  /* bufRetrvPktBufTrack_headBufOffset_f*/
    {14,  1, 14, "headBufferPtr"                         },  /* bufRetrvPktBufTrack_headBufferPtr_f*/
    { 9,  1,  5, "resourceGroupId"                       },  /* bufRetrvPktBufTrack_resourceGroupId_f*/
    { 1,  1,  4, "releasePacket"                         },  /* bufRetrvPktBufTrack_releasePacket_f*/
    { 1,  1,  3, "mcastRcd"                              },  /* bufRetrvPktBufTrack_mcastRcd_f*/
    { 3,  1,  0, "rcd_1"                                 },  /* bufRetrvPktBufTrack_rcd_1_f*/
    { 4,  2, 28, "rcd_0"                                 },  /* bufRetrvPktBufTrack_rcd_0_f*/
    { 2,  2, 26, "subGrpSel"                             },  /* bufRetrvPktBufTrack_subGrpSel_f*/
    {14,  2, 12, "packetLength"                          },  /* bufRetrvPktBufTrack_packetLength_f*/
    { 2,  2, 10, "chanPri"                               },  /* bufRetrvPktBufTrack_chanPri_f*/
    { 8,  2,  2, "grpId"                                 },  /* bufRetrvPktBufTrack_grpId_f*/
    { 2,  2,  0, "queueId_1"                             },  /* bufRetrvPktBufTrack_queueId_1_f*/
    { 8,  3, 24, "queueId_0"                             },  /* bufRetrvPktBufTrack_queueId_0_f*/
    { 1,  3, 23, "queUseCirDeficit"                      },  /* bufRetrvPktBufTrack_queUseCirDeficit_f*/
    {10,  3, 13, "grpBucketUpdVec"                       },  /* bufRetrvPktBufTrack_grpBucketUpdVec_f*/
    { 1,  3, 12, "congestionValid"                       },  /* bufRetrvPktBufTrack_congestionValid_f*/
    { 1,  3, 11, "discard"                               },  /* bufRetrvPktBufTrack_discard_f*/
    { 1,  3, 10, "destSelect"                            },  /* bufRetrvPktBufTrack_destSelect_f*/
    { 1,  3,  9, "lengthAdjustType"                      },  /* bufRetrvPktBufTrack_lengthAdjustType_f*/
    { 1,  3,  8, "ptEnable"                              },  /* bufRetrvPktBufTrack_ptEnable_f*/
    { 1,  3,  7, "nextHopExt"                            },  /* bufRetrvPktBufTrack_nextHopExt_f*/
    { 1,  3,  6, "inProfile"                             },  /* bufRetrvPktBufTrack_inProfile_f*/
    { 6,  3,  0, "replicationCtl_1"                      },  /* bufRetrvPktBufTrack_replicationCtl_1_f*/
    {10,  4, 22, "replicationCtl_0"                      },  /* bufRetrvPktBufTrack_replicationCtl_0_f*/
    {22,  4,  0, "destMap"                               },  /* bufRetrvPktBufTrack_destMap_f*/
};

static fields_t BufStore_MetFifo_Msg_Ipe_field[] = {
    { 3,  0, 16, "msgType"                               },  /* BufStoreMetFifoMsgIpe_msgType_f*/
    { 6,  0, 10, "bufferCount"                           },  /* BufStoreMetFifoMsgIpe_bufferCount_f*/
    { 2,  0,  8, "headBufOffset"                         },  /* BufStoreMetFifoMsgIpe_headBufOffset_f*/
    { 8,  0,  0, "headBufferPtr_1"                       },  /* BufStoreMetFifoMsgIpe_headBufferPtr_1_f*/
    { 6,  1, 26, "headBufferPtr_0"                       },  /* BufStoreMetFifoMsgIpe_headBufferPtr_0_f*/
    { 2,  1, 24, "metFifoSelect"                         },  /* BufStoreMetFifoMsgIpe_metFifoSelect_f*/
    { 9,  1, 15, "resourceGroupId"                       },  /* BufStoreMetFifoMsgIpe_resourceGroupId_f*/
    {14,  1,  1, "tailBufferPtr"                         },  /* BufStoreMetFifoMsgIpe_tailBufferPtr_f*/
    { 1,  1,  0, "mcastRcd"                              },  /* BufStoreMetFifoMsgIpe_mcastRcd_f*/
    {14,  2, 18, "packetLength"                          },  /* BufStoreMetFifoMsgIpe_packetLength_f*/
    { 1,  2, 17, "logicPortType"                         },  /* BufStoreMetFifoMsgIpe_logicPortType_f*/
    { 8,  2,  9, "payloadOffset"                         },  /* BufStoreMetFifoMsgIpe_payloadOffset_f*/
    { 9,  2,  0, "fid_1"                                 },  /* BufStoreMetFifoMsgIpe_fid_1_f*/
    { 5,  3, 27, "fid_0"                                 },  /* BufStoreMetFifoMsgIpe_fid_0_f*/
    { 1,  3, 26, "srcQueueSelect"                        },  /* BufStoreMetFifoMsgIpe_srcQueueSelect_f*/
    { 1,  3, 25, "isLeaf"                                },  /* BufStoreMetFifoMsgIpe_isLeaf_f*/
    { 1,  3, 24, "fromFabric"                            },  /* BufStoreMetFifoMsgIpe_fromFabric_f*/
    { 1,  3, 23, "lengthAdjustType"                      },  /* BufStoreMetFifoMsgIpe_lengthAdjustType_f*/
    { 1,  3, 22, "criticalPacket"                        },  /* BufStoreMetFifoMsgIpe_criticalPacket_f*/
    { 1,  3, 21, "destIdDiscard"                         },  /* BufStoreMetFifoMsgIpe_destIdDiscard_f*/
    { 1,  3, 20, "localSwitching"                        },  /* BufStoreMetFifoMsgIpe_localSwitching_f*/
    { 1,  3, 19, "egressException"                       },  /* BufStoreMetFifoMsgIpe_egressException_f*/
    { 3,  3, 16, "exceptionPacketType"                   },  /* BufStoreMetFifoMsgIpe_exceptionPacketType_f*/
    { 4,  3, 12, "exceptionSubIndex_5_2"                 },  /* BufStoreMetFifoMsgIpe_exceptionSubIndex_5_2_f*/
    { 1,  3, 11, "ecnEn"                                 },  /* BufStoreMetFifoMsgIpe_ecnEn_f*/
    { 1,  3, 10, "rxOam"                                 },  /* BufStoreMetFifoMsgIpe_rxOam_f*/
    { 1,  3,  9, "nextHopExt"                            },  /* BufStoreMetFifoMsgIpe_nextHopExt_f*/
    { 1,  3,  8, "ecnAware"                              },  /* BufStoreMetFifoMsgIpe_ecnAware_f*/
    { 2,  3,  6, "color"                                 },  /* BufStoreMetFifoMsgIpe_color_f*/
    { 2,  3,  4, "rxOamType_3_2"                         },  /* BufStoreMetFifoMsgIpe_rxOamType_3_2_f*/
    { 2,  3,  2, "l2SpanId"                              },  /* BufStoreMetFifoMsgIpe_l2SpanId_f*/
    { 2,  3,  0, "l3SpanId"                              },  /* BufStoreMetFifoMsgIpe_l3SpanId_f*/
    { 2,  4, 30, "aclLogId3"                             },  /* BufStoreMetFifoMsgIpe_aclLogId3_f*/
    { 2,  4, 28, "aclLogId2"                             },  /* BufStoreMetFifoMsgIpe_aclLogId2_f*/
    { 8,  4, 20, "headerHash"                            },  /* BufStoreMetFifoMsgIpe_headerHash_f*/
    { 2,  4, 18, "aclLogId1"                             },  /* BufStoreMetFifoMsgIpe_aclLogId1_f*/
    {18,  4,  0, "oldDestMap_1"                          },  /* BufStoreMetFifoMsgIpe_oldDestMap_1_f*/
    { 4,  5, 28, "oldDestMap_0"                          },  /* BufStoreMetFifoMsgIpe_oldDestMap_0_f*/
    { 2,  5, 26, "exceptionSubIndex_1_0"                 },  /* BufStoreMetFifoMsgIpe_exceptionSubIndex_1_0_f*/
    { 6,  5, 20, "priority"                              },  /* BufStoreMetFifoMsgIpe_priority_f*/
    { 5,  5, 15, "oamDestChipId"                         },  /* BufStoreMetFifoMsgIpe_oamDestChipId_f*/
    { 3,  5, 12, "operationType"                         },  /* BufStoreMetFifoMsgIpe_operationType_f*/
    { 2,  5, 10, "rxOamType_1_0"                         },  /* BufStoreMetFifoMsgIpe_rxOamType_1_0_f*/
    {10,  5,  0, "serviceId_1"                           },  /* BufStoreMetFifoMsgIpe_serviceId_1_f*/
    { 4,  6, 28, "serviceId_0"                           },  /* BufStoreMetFifoMsgIpe_serviceId_0_f*/
    {16,  6, 12, "sourcePort"                            },  /* BufStoreMetFifoMsgIpe_sourcePort_f*/
    { 4,  6,  8, "flowId"                                },  /* BufStoreMetFifoMsgIpe_flowId_f*/
    { 1,  6,  7, "c2cCheckDisable"                       },  /* BufStoreMetFifoMsgIpe_c2cCheckDisable_f*/
    { 3,  6,  4, "exceptionNumber"                       },  /* BufStoreMetFifoMsgIpe_exceptionNumber_f*/
    { 4,  6,  0, "exceptionVector8_1_1"                  },  /* BufStoreMetFifoMsgIpe_exceptionVector8_1_1_f*/
    { 4,  7, 28, "exceptionVector8_1_0"                  },  /* BufStoreMetFifoMsgIpe_exceptionVector8_1_0_f*/
    { 1,  7, 27, "exceptionVector0_0"                    },  /* BufStoreMetFifoMsgIpe_exceptionVector0_0_f*/
    { 1,  7, 26, "exceptionFromSgmac"                    },  /* BufStoreMetFifoMsgIpe_exceptionFromSgmac_f*/
    { 6,  7, 20, "srcSgmacGroupId"                       },  /* BufStoreMetFifoMsgIpe_srcSgmacGroupId_f*/
    { 2,  7, 18, "aclLogId0"                             },  /* BufStoreMetFifoMsgIpe_aclLogId0_f*/
    {18,  7,  0, "nextHopPtr"                            },  /* BufStoreMetFifoMsgIpe_nextHopPtr_f*/
};

static fields_t BufStore_MetFifo_Msg_Pkt_field[] = {
    { 3,  0, 16, "msgType"                               },  /* BufStoreMetFifoMsgPkt_msgType_f*/
    { 6,  0, 10, "bufferCount"                           },  /* BufStoreMetFifoMsgPkt_bufferCount_f*/
    { 2,  0,  8, "headBufOffset"                         },  /* BufStoreMetFifoMsgPkt_headBufOffset_f*/
    { 8,  0,  0, "headBufferPtr_1"                       },  /* BufStoreMetFifoMsgPkt_headBufferPtr_1_f*/
    { 6,  1, 26, "headBufferPtr_0"                       },  /* BufStoreMetFifoMsgPkt_headBufferPtr_0_f*/
    { 2,  1, 24, "metFifoSelect"                         },  /* BufStoreMetFifoMsgPkt_metFifoSelect_f*/
    { 9,  1, 15, "resourceGroupId"                       },  /* BufStoreMetFifoMsgPkt_resourceGroupId_f*/
    {14,  1,  1, "tailBufferPtr"                         },  /* BufStoreMetFifoMsgPkt_tailBufferPtr_f*/
    { 1,  1,  0, "mcastRcd"                              },  /* BufStoreMetFifoMsgPkt_mcastRcd_f*/
    {14,  2, 18, "packetLength"                          },  /* BufStoreMetFifoMsgPkt_packetLength_f*/
    { 1,  2, 17, "logicPortType"                         },  /* BufStoreMetFifoMsgPkt_logicPortType_f*/
    { 8,  2,  9, "payloadOffset"                         },  /* BufStoreMetFifoMsgPkt_payloadOffset_f*/
    { 9,  2,  0, "fid_1"                                 },  /* BufStoreMetFifoMsgPkt_fid_1_f*/
    { 5,  3, 27, "fid_0"                                 },  /* BufStoreMetFifoMsgPkt_fid_0_f*/
    { 1,  3, 26, "srcQueueSelect"                        },  /* BufStoreMetFifoMsgPkt_srcQueueSelect_f*/
    { 1,  3, 25, "isLeaf"                                },  /* BufStoreMetFifoMsgPkt_isLeaf_f*/
    { 1,  3, 24, "fromFabric"                            },  /* BufStoreMetFifoMsgPkt_fromFabric_f*/
    { 1,  3, 23, "lengthAdjustType"                      },  /* BufStoreMetFifoMsgPkt_lengthAdjustType_f*/
    { 1,  3, 22, "criticalPacket"                        },  /* BufStoreMetFifoMsgPkt_criticalPacket_f*/
    { 1,  3, 21, "destIdDiscard"                         },  /* BufStoreMetFifoMsgPkt_destIdDiscard_f*/
    { 1,  3, 20, "localSwitching"                        },  /* BufStoreMetFifoMsgPkt_localSwitching_f*/
    { 1,  3, 19, "egressException"                       },  /* BufStoreMetFifoMsgPkt_egressException_f*/
    { 3,  3, 16, "exceptionPacketType"                   },  /* BufStoreMetFifoMsgPkt_exceptionPacketType_f*/
    { 4,  3, 12, "exceptionSubIndex_5_2"                 },  /* BufStoreMetFifoMsgPkt_exceptionSubIndex_5_2_f*/
    { 1,  3, 11, "ecnEn"                                 },  /* BufStoreMetFifoMsgPkt_ecnEn_f*/
    { 1,  3, 10, "rxOam"                                 },  /* BufStoreMetFifoMsgPkt_rxOam_f*/
    { 1,  3,  9, "nextHopExt"                            },  /* BufStoreMetFifoMsgPkt_nextHopExt_f*/
    { 1,  3,  8, "ecnAware"                              },  /* BufStoreMetFifoMsgPkt_ecnAware_f*/
    { 2,  3,  6, "color"                                 },  /* BufStoreMetFifoMsgPkt_color_f*/
    { 2,  3,  4, "rxOamType_3_2"                         },  /* BufStoreMetFifoMsgPkt_rxOamType_3_2_f*/
    { 2,  3,  2, "l2SpanId"                              },  /* BufStoreMetFifoMsgPkt_l2SpanId_f*/
    { 2,  3,  0, "l3SpanId"                              },  /* BufStoreMetFifoMsgPkt_l3SpanId_f*/
    { 2,  4, 30, "aclLogId3"                             },  /* BufStoreMetFifoMsgPkt_aclLogId3_f*/
    { 2,  4, 28, "aclLogId2"                             },  /* BufStoreMetFifoMsgPkt_aclLogId2_f*/
    { 8,  4, 20, "headerHash"                            },  /* BufStoreMetFifoMsgPkt_headerHash_f*/
    { 2,  4, 18, "aclLogId1"                             },  /* BufStoreMetFifoMsgPkt_aclLogId1_f*/
    {18,  4,  0, "oldDestMap_1"                          },  /* BufStoreMetFifoMsgPkt_oldDestMap_1_f*/
    { 4,  5, 28, "oldDestMap_0"                          },  /* BufStoreMetFifoMsgPkt_oldDestMap_0_f*/
    { 2,  5, 26, "exceptionSubIndex_1_0"                 },  /* BufStoreMetFifoMsgPkt_exceptionSubIndex_1_0_f*/
    { 6,  5, 20, "priority"                              },  /* BufStoreMetFifoMsgPkt_priority_f*/
    { 5,  5, 15, "oamDestChipId"                         },  /* BufStoreMetFifoMsgPkt_oamDestChipId_f*/
    { 3,  5, 12, "operationType"                         },  /* BufStoreMetFifoMsgPkt_operationType_f*/
    { 2,  5, 10, "rxOamType_1_0"                         },  /* BufStoreMetFifoMsgPkt_rxOamType_1_0_f*/
    {10,  5,  0, "serviceId_1"                           },  /* BufStoreMetFifoMsgPkt_serviceId_1_f*/
    { 4,  6, 28, "serviceId_0"                           },  /* BufStoreMetFifoMsgPkt_serviceId_0_f*/
    {16,  6, 12, "sourcePort"                            },  /* BufStoreMetFifoMsgPkt_sourcePort_f*/
    { 4,  6,  8, "flowId"                                },  /* BufStoreMetFifoMsgPkt_flowId_f*/
    { 1,  6,  7, "c2cCheckDisable"                       },  /* BufStoreMetFifoMsgPkt_c2cCheckDisable_f*/
    { 3,  6,  4, "exceptionNumber"                       },  /* BufStoreMetFifoMsgPkt_exceptionNumber_f*/
    { 4,  6,  0, "exceptionVector8_1_1"                  },  /* BufStoreMetFifoMsgPkt_exceptionVector8_1_1_f*/
    { 4,  7, 28, "exceptionVector8_1_0"                  },  /* BufStoreMetFifoMsgPkt_exceptionVector8_1_0_f*/
    { 1,  7, 27, "exceptionVector0_0"                    },  /* BufStoreMetFifoMsgPkt_exceptionVector0_0_f*/
    { 1,  7, 26, "exceptionFromSgmac"                    },  /* BufStoreMetFifoMsgPkt_exceptionFromSgmac_f*/
    { 6,  7, 20, "srcSgmacGroupId"                       },  /* BufStoreMetFifoMsgPkt_srcSgmacGroupId_f*/
    { 2,  7, 18, "aclLogId0"                             },  /* BufStoreMetFifoMsgPkt_aclLogId0_f*/
    {18,  7,  0, "nextHopPtr"                            },  /* BufStoreMetFifoMsgPkt_nextHopPtr_f*/
};

static fields_t Epe_HdrProc_AclQos_PI_field[] = {
    { 8,  0, 21, "piSeq"                                 },  /* EpeHdrProcAclQosPI_piSeq_f*/
    { 4,  0, 17, "piRxOamType"                           },  /* EpeHdrProcAclQosPI_piRxOamType_f*/
    { 1,  0, 16, "piOamUseFid"                           },  /* EpeHdrProcAclQosPI_piOamUseFid_f*/
    { 1,  0, 15, "piFromCpuOrOam"                        },  /* EpeHdrProcAclQosPI_piFromCpuOrOam_f*/
    { 1,  0, 14, "piDiscard"                             },  /* EpeHdrProcAclQosPI_piDiscard_f*/
    { 6,  0,  8, "piDiscardType"                         },  /* EpeHdrProcAclQosPI_piDiscardType_f*/
    { 1,  0,  7, "piHardError"                           },  /* EpeHdrProcAclQosPI_piHardError_f*/
    { 7,  0,  0, "piLocalPhyPort"                        },  /* EpeHdrProcAclQosPI_piLocalPhyPort_f*/
    { 3,  1, 29, "piPayloadOperation"                    },  /* EpeHdrProcAclQosPI_piPayloadOperation_f*/
    { 1,  1, 28, "piServiceAclQosEn"                     },  /* EpeHdrProcAclQosPI_piServiceAclQosEn_f*/
    {14,  1, 14, "piLogicDestPort"                       },  /* EpeHdrProcAclQosPI_piLogicDestPort_f*/
    { 1,  1, 13, "piL2AclEn0"                            },  /* EpeHdrProcAclQosPI_piL2AclEn0_f*/
    { 1,  1, 12, "piL2AclEn1"                            },  /* EpeHdrProcAclQosPI_piL2AclEn1_f*/
    { 1,  1, 11, "piL2AclEn2"                            },  /* EpeHdrProcAclQosPI_piL2AclEn2_f*/
    { 1,  1, 10, "piL2AclEn3"                            },  /* EpeHdrProcAclQosPI_piL2AclEn3_f*/
    {10,  1,  0, "piL2AclLabel"                          },  /* EpeHdrProcAclQosPI_piL2AclLabel_f*/
    { 3,  2, 29, "piDefaultPcp"                          },  /* EpeHdrProcAclQosPI_piDefaultPcp_f*/
    { 1,  2, 28, "piMplsKeyUseLabel"                     },  /* EpeHdrProcAclQosPI_piMplsKeyUseLabel_f*/
    { 6,  2, 22, "piAclPortNum"                          },  /* EpeHdrProcAclQosPI_piAclPortNum_f*/
    { 1,  2, 21, "piMacKeyUseLabel"                      },  /* EpeHdrProcAclQosPI_piMacKeyUseLabel_f*/
    { 1,  2, 20, "piIpv6KeyUseLabel"                     },  /* EpeHdrProcAclQosPI_piIpv6KeyUseLabel_f*/
    { 1,  2, 19, "piL2Ipv6AclEn0"                        },  /* EpeHdrProcAclQosPI_piL2Ipv6AclEn0_f*/
    { 1,  2, 18, "piL2Ipv6AclEn1"                        },  /* EpeHdrProcAclQosPI_piL2Ipv6AclEn1_f*/
    { 1,  2, 17, "piForceIpv4ToMacKey"                   },  /* EpeHdrProcAclQosPI_piForceIpv4ToMacKey_f*/
    { 1,  2, 16, "piForceIpv6ToMacKey"                   },  /* EpeHdrProcAclQosPI_piForceIpv6ToMacKey_f*/
    { 1,  2, 15, "piL3AclRoutedOnly"                     },  /* EpeHdrProcAclQosPI_piL3AclRoutedOnly_f*/
    { 1,  2, 14, "piL3AclEn0"                            },  /* EpeHdrProcAclQosPI_piL3AclEn0_f*/
    { 1,  2, 13, "piL3AclEn1"                            },  /* EpeHdrProcAclQosPI_piL3AclEn1_f*/
    { 1,  2, 12, "piL3AclEn2"                            },  /* EpeHdrProcAclQosPI_piL3AclEn2_f*/
    { 1,  2, 11, "piL3AclEn3"                            },  /* EpeHdrProcAclQosPI_piL3AclEn3_f*/
    {10,  2,  1, "piL3AclLabel"                          },  /* EpeHdrProcAclQosPI_piL3AclLabel_f*/
    { 1,  2,  0, "piL3Ipv6AclEn0"                        },  /* EpeHdrProcAclQosPI_piL3Ipv6AclEn0_f*/
    { 1,  3, 31, "piL3Ipv6AclEn1"                        },  /* EpeHdrProcAclQosPI_piL3Ipv6AclEn1_f*/
    {10,  3, 21, "piInterfaceId"                         },  /* EpeHdrProcAclQosPI_piInterfaceId_f*/
    { 1,  3, 20, "piMplsSectionLmEn"                     },  /* EpeHdrProcAclQosPI_piMplsSectionLmEn_f*/
    { 1,  3, 19, "piFlowStats1Valid"                     },  /* EpeHdrProcAclQosPI_piFlowStats1Valid_f*/
    {16,  3,  3, "piFlowStats1Ptr"                       },  /* EpeHdrProcAclQosPI_piFlowStats1Ptr_f*/
    { 1,  3,  2, "piIpv4KeyUseLabel"                     },  /* EpeHdrProcAclQosPI_piIpv4KeyUseLabel_f*/
    { 2,  3,  0, "piSectionLmExp_1"                      },  /* EpeHdrProcAclQosPI_piSectionLmExp_1_f*/
    { 1,  4, 31, "piSectionLmExp_0"                      },  /* EpeHdrProcAclQosPI_piSectionLmExp_0_f*/
    { 1,  4, 30, "piMplsLmValid2"                        },  /* EpeHdrProcAclQosPI_piMplsLmValid2_f*/
    { 3,  4, 27, "piMplsLmExp2"                          },  /* EpeHdrProcAclQosPI_piMplsLmExp2_f*/
    {20,  4,  7, "piMplsLmLabel2"                        },  /* EpeHdrProcAclQosPI_piMplsLmLabel2_f*/
    { 1,  4,  6, "piMplsLmValid1"                        },  /* EpeHdrProcAclQosPI_piMplsLmValid1_f*/
    { 3,  4,  3, "piMplsLmExp1"                          },  /* EpeHdrProcAclQosPI_piMplsLmExp1_f*/
    { 3,  4,  0, "piMplsLmLabel1_1"                      },  /* EpeHdrProcAclQosPI_piMplsLmLabel1_1_f*/
    {17,  5, 15, "piMplsLmLabel1_0"                      },  /* EpeHdrProcAclQosPI_piMplsLmLabel1_0_f*/
    { 1,  5, 14, "piMplsLmValid0"                        },  /* EpeHdrProcAclQosPI_piMplsLmValid0_f*/
    { 3,  5, 11, "piMplsLmExp0"                          },  /* EpeHdrProcAclQosPI_piMplsLmExp0_f*/
    {11,  5,  0, "piMplsLmLabel0_1"                      },  /* EpeHdrProcAclQosPI_piMplsLmLabel0_1_f*/
    { 9,  6, 23, "piMplsLmLabel0_0"                      },  /* EpeHdrProcAclQosPI_piMplsLmLabel0_0_f*/
    { 8,  6, 15, "piMplsLabelSpace"                      },  /* EpeHdrProcAclQosPI_piMplsLabelSpace_f*/
    { 1,  6, 14, "piPacketLengthAdjustType"              },  /* EpeHdrProcAclQosPI_piPacketLengthAdjustType_f*/
    { 8,  6,  6, "piPacketLengthAdjust"                  },  /* EpeHdrProcAclQosPI_piPacketLengthAdjust_f*/
    { 6,  6,  0, "piChannelId"                           },  /* EpeHdrProcAclQosPI_piChannelId_f*/
    {14,  7, 18, "piPacketLength"                        },  /* EpeHdrProcAclQosPI_piPacketLength_f*/
    { 2,  7, 16, "piColor"                               },  /* EpeHdrProcAclQosPI_piColor_f*/
    { 6,  7, 10, "piPriority"                            },  /* EpeHdrProcAclQosPI_piPriority_f*/
    { 1,  7,  9, "piServicePolicerValid"                 },  /* EpeHdrProcAclQosPI_piServicePolicerValid_f*/
    { 9,  7,  0, "piDestVlanPtr_1"                       },  /* EpeHdrProcAclQosPI_piDestVlanPtr_1_f*/
    { 4,  8, 28, "piDestVlanPtr_0"                       },  /* EpeHdrProcAclQosPI_piDestVlanPtr_0_f*/
    { 1,  8, 27, "piPortPolicerValid"                    },  /* EpeHdrProcAclQosPI_piPortPolicerValid_f*/
    { 2,  8, 25, "piIpgIndex"                            },  /* EpeHdrProcAclQosPI_piIpgIndex_f*/
    {14,  8, 11, "piGlobalDestPort"                      },  /* EpeHdrProcAclQosPI_piGlobalDestPort_f*/
    {11,  8,  0, "piFid_1"                               },  /* EpeHdrProcAclQosPI_piFid_1_f*/
    { 3,  9, 29, "piFid_0"                               },  /* EpeHdrProcAclQosPI_piFid_0_f*/
    { 2,  9, 27, "piOamLookupNum"                        },  /* EpeHdrProcAclQosPI_piOamLookupNum_f*/
    { 1,  9, 26, "piEtherLmValid"                        },  /* EpeHdrProcAclQosPI_piEtherLmValid_f*/
    { 1,  9, 25, "piPacketHeaderEn"                      },  /* EpeHdrProcAclQosPI_piPacketHeaderEn_f*/
    { 1,  9, 24, "piPacketHeaderEnEgress"                },  /* EpeHdrProcAclQosPI_piPacketHeaderEnEgress_f*/
    { 1,  9, 23, "piForceIpv6Key"                        },  /* EpeHdrProcAclQosPI_piForceIpv6Key_f*/
    { 1,  9, 22, "piLinkOrSectionOam"                    },  /* EpeHdrProcAclQosPI_piLinkOrSectionOam_f*/
    {12,  9, 10, "piOamVlan"                             },  /* EpeHdrProcAclQosPI_piOamVlan_f*/
    { 4,  9,  6, "prLayer3Type"                          },  /* EpeHdrProcAclQosPI_prLayer3Type_f*/
    { 4,  9,  2, "prLayer4Type"                          },  /* EpeHdrProcAclQosPI_prLayer4Type_f*/
    { 2,  9,  0, "prLayer2Type_1"                        },  /* EpeHdrProcAclQosPI_prLayer2Type_1_f*/
    { 2, 10, 30, "prLayer2Type_0"                        },  /* EpeHdrProcAclQosPI_prLayer2Type_0_f*/
    { 1, 10, 29, "prSvlanIdValid"                        },  /* EpeHdrProcAclQosPI_prSvlanIdValid_f*/
    { 1, 10, 28, "prCvlanIdValid"                        },  /* EpeHdrProcAclQosPI_prCvlanIdValid_f*/
    {12, 10, 16, "prSvlanId"                             },  /* EpeHdrProcAclQosPI_prSvlanId_f*/
    {12, 10,  4, "prCvlanId"                             },  /* EpeHdrProcAclQosPI_prCvlanId_f*/
    { 4, 10,  0, "prLayer2HeaderProtocol_1"              },  /* EpeHdrProcAclQosPI_prLayer2HeaderProtocol_1_f*/
    {12, 11, 20, "prLayer2HeaderProtocol_0"              },  /* EpeHdrProcAclQosPI_prLayer2HeaderProtocol_0_f*/
    { 3, 11, 17, "prStagCos"                             },  /* EpeHdrProcAclQosPI_prStagCos_f*/
    { 1, 11, 16, "prStagCfi"                             },  /* EpeHdrProcAclQosPI_prStagCfi_f*/
    { 3, 11, 13, "prCtagCos"                             },  /* EpeHdrProcAclQosPI_prCtagCos_f*/
    { 1, 11, 12, "prCtagCfi"                             },  /* EpeHdrProcAclQosPI_prCtagCfi_f*/
    {12, 11,  0, "prL4SourcePort_1"                      },  /* EpeHdrProcAclQosPI_prL4SourcePort_1_f*/
    { 4, 12, 28, "prL4SourcePort_0"                      },  /* EpeHdrProcAclQosPI_prL4SourcePort_0_f*/
    {16, 12, 12, "prL4DestPort"                          },  /* EpeHdrProcAclQosPI_prL4DestPort_f*/
    {12, 12,  0, "prL4InfoMapped"                        },  /* EpeHdrProcAclQosPI_prL4InfoMapped_f*/
    {32, 13,  0, "prIpDa_3"                              },  /* EpeHdrProcAclQosPI_prIpDa_3_f*/
    {32, 14,  0, "prIpDa_2"                              },  /* EpeHdrProcAclQosPI_prIpDa_2_f*/
    {32, 15,  0, "prIpDa_1"                              },  /* EpeHdrProcAclQosPI_prIpDa_1_f*/
    {32, 16,  0, "prIpDa_0"                              },  /* EpeHdrProcAclQosPI_prIpDa_0_f*/
    {32, 17,  0, "prIpSa_3"                              },  /* EpeHdrProcAclQosPI_prIpSa_3_f*/
    {32, 18,  0, "prIpSa_2"                              },  /* EpeHdrProcAclQosPI_prIpSa_2_f*/
    {32, 19,  0, "prIpSa_1"                              },  /* EpeHdrProcAclQosPI_prIpSa_1_f*/
    {32, 20,  0, "prIpSa_0"                              },  /* EpeHdrProcAclQosPI_prIpSa_0_f*/
    {20, 21, 12, "prIpv6FlowLabel"                       },  /* EpeHdrProcAclQosPI_prIpv6FlowLabel_f*/
    {12, 21,  0, "prMacDa_2"                             },  /* EpeHdrProcAclQosPI_prMacDa_2_f*/
    {32, 22,  0, "prMacDa_1"                             },  /* EpeHdrProcAclQosPI_prMacDa_1_f*/
    { 4, 23, 28, "prMacDa_0"                             },  /* EpeHdrProcAclQosPI_prMacDa_0_f*/
    {28, 23,  0, "prMacSa_1"                             },  /* EpeHdrProcAclQosPI_prMacSa_1_f*/
    {20, 24, 12, "prMacSa_0"                             },  /* EpeHdrProcAclQosPI_prMacSa_0_f*/
    { 1, 24, 11, "prIpHeaderError"                       },  /* EpeHdrProcAclQosPI_prIpHeaderError_f*/
    { 1, 24, 10, "prIsTcp"                               },  /* EpeHdrProcAclQosPI_prIsTcp_f*/
    { 1, 24,  9, "prIsUdp"                               },  /* EpeHdrProcAclQosPI_prIsUdp_f*/
    { 1, 24,  8, "prIpOptions"                           },  /* EpeHdrProcAclQosPI_prIpOptions_f*/
    { 2, 24,  6, "prFragInfo"                            },  /* EpeHdrProcAclQosPI_prFragInfo_f*/
    { 6, 24,  0, "prDscp"                                },  /* EpeHdrProcAclQosPI_prDscp_f*/
};

static fields_t Epe_Acl_Tcam_Ack_field[] = {
    { 1,  0, 23, "error0"                                },  /* EpeAclTcamAck_error0_f*/
    { 1,  0, 22, "error1"                                },  /* EpeAclTcamAck_error1_f*/
    { 1,  0, 21, "error2"                                },  /* EpeAclTcamAck_error2_f*/
    { 1,  0, 20, "error3"                                },  /* EpeAclTcamAck_error3_f*/
    { 1,  0, 19, "resultValid0"                          },  /* EpeAclTcamAck_resultValid0_f*/
    { 1,  0, 18, "resultValid1"                          },  /* EpeAclTcamAck_resultValid1_f*/
    { 1,  0, 17, "resultValid2"                          },  /* EpeAclTcamAck_resultValid2_f*/
    { 1,  0, 16, "resultValid3"                          },  /* EpeAclTcamAck_resultValid3_f*/
    { 8,  0,  8, "seq"                                   },  /* EpeAclTcamAck_seq_f*/
    { 8,  0,  0, "result0_2"                             },  /* EpeAclTcamAck_result0_2_f*/
    {32,  1,  0, "result0_1"                             },  /* EpeAclTcamAck_result0_1_f*/
    {10,  2, 22, "result0_0"                             },  /* EpeAclTcamAck_result0_0_f*/
    {22,  2,  0, "result1_1"                             },  /* EpeAclTcamAck_result1_1_f*/
    {28,  3,  4, "result1_0"                             },  /* EpeAclTcamAck_result1_0_f*/
    { 4,  3,  0, "result2_2"                             },  /* EpeAclTcamAck_result2_2_f*/
    {32,  4,  0, "result2_1"                             },  /* EpeAclTcamAck_result2_1_f*/
    {14,  5, 18, "result2_0"                             },  /* EpeAclTcamAck_result2_0_f*/
    {18,  5,  0, "result3_1"                             },  /* EpeAclTcamAck_result3_1_f*/
    {32,  6,  0, "result3_0"                             },  /* EpeAclTcamAck_result3_0_f*/
};

static fields_t Epe_Cla_Policer_field[] = {
    { 8,  0, 14, "piPktSeq"                              },  /* EpeClaPolicer_piPktSeq_f*/
    { 1,  0, 13, "policingEnable"                        },  /* EpeClaPolicer_policingEnable_f*/
    { 4,  0,  9, "piRxOamType"                           },  /* EpeClaPolicer_piRxOamType_f*/
    { 2,  0,  7, "piColor"                               },  /* EpeClaPolicer_piColor_f*/
    { 1,  0,  6, "piFromCpuOrOam"                        },  /* EpeClaPolicer_piFromCpuOrOam_f*/
    { 1,  0,  5, "piDiscard"                             },  /* EpeClaPolicer_piDiscard_f*/
    { 5,  0,  0, "piDiscardType_1"                       },  /* EpeClaPolicer_piDiscardType_1_f*/
    { 1,  1, 31, "piDiscardType_0"                       },  /* EpeClaPolicer_piDiscardType_0_f*/
    { 7,  1, 24, "piLocalPhyPort"                        },  /* EpeClaPolicer_piLocalPhyPort_f*/
    { 2,  1, 22, "piLmLookupType"                        },  /* EpeClaPolicer_piLmLookupType_f*/
    { 1,  1, 21, "piOamLookupEn"                         },  /* EpeClaPolicer_piOamLookupEn_f*/
    { 1,  1, 20, "piLmLookupEn0"                         },  /* EpeClaPolicer_piLmLookupEn0_f*/
    { 3,  1, 17, "piPacketCos0"                          },  /* EpeClaPolicer_piPacketCos0_f*/
    { 2,  1, 15, "piOamLookupNum"                        },  /* EpeClaPolicer_piOamLookupNum_f*/
    { 1,  1, 14, "piLmLookupEn1"                         },  /* EpeClaPolicer_piLmLookupEn1_f*/
    { 1,  1, 13, "piLmLookupEn2"                         },  /* EpeClaPolicer_piLmLookupEn2_f*/
    { 3,  1, 10, "piPacketCos1"                          },  /* EpeClaPolicer_piPacketCos1_f*/
    { 3,  1,  7, "piPacketCos2"                          },  /* EpeClaPolicer_piPacketCos2_f*/
    { 6,  1,  1, "piPriority"                            },  /* EpeClaPolicer_piPriority_f*/
    { 1,  1,  0, "piPacketLength_1"                      },  /* EpeClaPolicer_piPacketLength_1_f*/
    {13,  2, 19, "piPacketLength_0"                      },  /* EpeClaPolicer_piPacketLength_0_f*/
    { 1,  2, 18, "piHardError"                           },  /* EpeClaPolicer_piHardError_f*/
    {14,  2,  4, "piGlobalDestPort"                      },  /* EpeClaPolicer_piGlobalDestPort_f*/
    { 1,  2,  3, "piFlowStats1Valid"                     },  /* EpeClaPolicer_piFlowStats1Valid_f*/
    { 3,  2,  0, "piFlowStats1Ptr_1"                     },  /* EpeClaPolicer_piFlowStats1Ptr_1_f*/
    {13,  3, 19, "piFlowStats1Ptr_0"                     },  /* EpeClaPolicer_piFlowStats1Ptr_0_f*/
    { 1,  3, 18, "piFlowStats0Valid"                     },  /* EpeClaPolicer_piFlowStats0Valid_f*/
    {16,  3,  2, "piFlowStats0Ptr"                       },  /* EpeClaPolicer_piFlowStats0Ptr_f*/
    { 1,  3,  1, "piAclLogEn0"                           },  /* EpeClaPolicer_piAclLogEn0_f*/
    { 1,  3,  0, "piAclLogId0_1"                         },  /* EpeClaPolicer_piAclLogId0_1_f*/
    { 1,  4, 31, "piAclLogId0_0"                         },  /* EpeClaPolicer_piAclLogId0_0_f*/
    { 1,  4, 30, "piAclLogEn1"                           },  /* EpeClaPolicer_piAclLogEn1_f*/
    { 2,  4, 28, "piAclLogId1"                           },  /* EpeClaPolicer_piAclLogId1_f*/
    { 1,  4, 27, "piAclLogEn2"                           },  /* EpeClaPolicer_piAclLogEn2_f*/
    { 2,  4, 25, "piAclLogId2"                           },  /* EpeClaPolicer_piAclLogId2_f*/
    { 1,  4, 24, "piAclLogEn3"                           },  /* EpeClaPolicer_piAclLogEn3_f*/
    { 2,  4, 22, "piAclLogId3"                           },  /* EpeClaPolicer_piAclLogId3_f*/
    {14,  4,  8, "piFid"                                 },  /* EpeClaPolicer_piFid_f*/
    { 8,  4,  0, "piDestVlanPtr_1"                       },  /* EpeClaPolicer_piDestVlanPtr_1_f*/
    { 5,  5, 27, "piDestVlanPtr_0"                       },  /* EpeClaPolicer_piDestVlanPtr_0_f*/
    { 1,  5, 26, "piIsTcp"                               },  /* EpeClaPolicer_piIsTcp_f*/
    { 4,  5, 22, "prLayer3Type"                          },  /* EpeClaPolicer_prLayer3Type_f*/
    { 4,  5, 18, "prLayer4Type"                          },  /* EpeClaPolicer_prLayer4Type_f*/
    { 6,  5, 12, "piChannelId"                           },  /* EpeClaPolicer_piChannelId_f*/
    { 8,  5,  4, "piPktSeq"                              },  /* EpeClaPolicer_piPktSeq_f*/
    { 1,  5,  3, "policingEnable"                        },  /* EpeClaPolicer_policingEnable_f*/
    { 1,  5,  2, "piIsUdp"                               },  /* EpeClaPolicer_piIsUdp_f*/
    { 1,  5,  1, "piPacketHeaderEn"                      },  /* EpeClaPolicer_piPacketHeaderEn_f*/
    { 1,  5,  0, "piPacketHeaderEnEgress"                },  /* EpeClaPolicer_piPacketHeaderEnEgress_f*/
};

static fields_t Br_EpeHdrAdj_Brg_field[] = {
    {14,  0,  8, "bufRetrvEpePktLen"                     },  /* BrEpeHdrAdjBrg_bufRetrvEpePktLen_f*/
    { 1,  0,  7, "bufRetrvEpeDestIdDiscard"              },  /* BrEpeHdrAdjBrg_bufRetrvEpeDestIdDiscard_f*/
    { 1,  0,  6, "bufRetrvEpeSop"                        },  /* BrEpeHdrAdjBrg_bufRetrvEpeSop_f*/
    { 6,  0,  0, "bufRetrvEpeChan"                       },  /* BrEpeHdrAdjBrg_bufRetrvEpeChan_f*/
    { 2,  1, 30, "sourcePort15_14"                       },  /* BrEpeHdrAdjBrg_sourcePort15_14_f*/
    { 8,  1, 22, "packetOffset"                          },  /* BrEpeHdrAdjBrg_packetOffset_f*/
    {22,  1,  0, "destMap"                               },  /* BrEpeHdrAdjBrg_destMap_f*/
    { 6,  2, 26, "priority"                              },  /* BrEpeHdrAdjBrg_priority_f*/
    { 3,  2, 23, "packetType"                            },  /* BrEpeHdrAdjBrg_packetType_f*/
    { 3,  2, 20, "sourceCos"                             },  /* BrEpeHdrAdjBrg_sourceCos_f*/
    { 1,  2, 19, "srcQueueSelect"                        },  /* BrEpeHdrAdjBrg_srcQueueSelect_f*/
    { 3,  2, 16, "headerHash2_0"                         },  /* BrEpeHdrAdjBrg_headerHash2_0_f*/
    { 1,  2, 15, "logicPortType"                         },  /* BrEpeHdrAdjBrg_logicPortType_f*/
    { 1,  2, 14, "srcCtagOffsetType"                     },  /* BrEpeHdrAdjBrg_srcCtagOffsetType_f*/
    {14,  2,  0, "sourcePort"                            },  /* BrEpeHdrAdjBrg_sourcePort_f*/
    {12,  3, 20, "srcVlanId"                             },  /* BrEpeHdrAdjBrg_srcVlanId_f*/
    { 2,  3, 18, "color"                                 },  /* BrEpeHdrAdjBrg_color_f*/
    { 1,  3, 17, "bridgeOperation"                       },  /* BrEpeHdrAdjBrg_bridgeOperation_f*/
    {17,  3,  0, "nextHopPtr"                            },  /* BrEpeHdrAdjBrg_nextHopPtr_f*/
    { 1,  4, 31, "lengthAdjustType"                      },  /* BrEpeHdrAdjBrg_lengthAdjustType_f*/
    { 1,  4, 30, "criticalPacket"                        },  /* BrEpeHdrAdjBrg_criticalPacket_f*/
    { 6,  4, 24, "rxtxFcl22_17"                          },  /* BrEpeHdrAdjBrg_rxtxFcl22_17_f*/
    { 8,  4, 16, "flow"                                  },  /* BrEpeHdrAdjBrg_flow_f*/
    { 8,  4,  8, "ttl"                                   },  /* BrEpeHdrAdjBrg_ttl_f*/
    { 1,  4,  7, "fromFabric"                            },  /* BrEpeHdrAdjBrg_fromFabric_f*/
    { 1,  4,  6, "bypassIngressEdit"                     },  /* BrEpeHdrAdjBrg_bypassIngressEdit_f*/
    { 1,  4,  5, "sourcePortExtender"                    },  /* BrEpeHdrAdjBrg_sourcePortExtender_f*/
    { 1,  4,  4, "loopbackDiscard"                       },  /* BrEpeHdrAdjBrg_loopbackDiscard_f*/
    { 4,  4,  0, "headerCrc"                             },  /* BrEpeHdrAdjBrg_headerCrc_f*/
    { 6,  5, 26, "sourcePortIsolateId"                   },  /* BrEpeHdrAdjBrg_sourcePortIsolateId_f*/
    { 3,  5, 23, "pbbSrcPortType"                        },  /* BrEpeHdrAdjBrg_pbbSrcPortType_f*/
    { 1,  5, 22, "svlanTagOperationValid"                },  /* BrEpeHdrAdjBrg_svlanTagOperationValid_f*/
    { 1,  5, 21, "sourceCfi"                             },  /* BrEpeHdrAdjBrg_sourceCfi_f*/
    { 1,  5, 20, "nextHopExt"                            },  /* BrEpeHdrAdjBrg_nextHopExt_f*/
    { 1,  5, 19, "nonCrc"                                },  /* BrEpeHdrAdjBrg_nonCrc_f*/
    { 1,  5, 18, "fromCpuOrOam"                          },  /* BrEpeHdrAdjBrg_fromCpuOrOam_f*/
    { 2,  5, 16, "svlanTpidIndex"                        },  /* BrEpeHdrAdjBrg_svlanTpidIndex_f*/
    { 2,  5, 14, "stagAction"                            },  /* BrEpeHdrAdjBrg_stagAction_f*/
    { 1,  5, 13, "srcSvlanIdValid"                       },  /* BrEpeHdrAdjBrg_srcSvlanIdValid_f*/
    { 1,  5, 12, "srcCvlanIdValid"                       },  /* BrEpeHdrAdjBrg_srcCvlanIdValid_f*/
    {12,  5,  0, "srcCvlanId"                            },  /* BrEpeHdrAdjBrg_srcCvlanId_f*/
    {16,  6, 16, "srcVlanPtr"                            },  /* BrEpeHdrAdjBrg_srcVlanPtr_f*/
    {16,  6,  0, "fid"                                   },  /* BrEpeHdrAdjBrg_fid_f*/
    {16,  7, 16, "logicSrcPort"                          },  /* BrEpeHdrAdjBrg_logicSrcPort_f*/
    { 1,  7, 15, "rxtxFcl3"                              },  /* BrEpeHdrAdjBrg_rxtxFcl3_f*/
    { 1,  7, 14, "cutThrough"                            },  /* BrEpeHdrAdjBrg_cutThrough_f*/
    { 2,  7, 12, "rxtxFcl2_1"                            },  /* BrEpeHdrAdjBrg_rxtxFcl2_1_f*/
    { 2,  7, 10, "muxLengthType"                         },  /* BrEpeHdrAdjBrg_muxLengthType_f*/
    { 1,  7,  9, "oamTunnelEn"                           },  /* BrEpeHdrAdjBrg_oamTunnelEn_f*/
    { 1,  7,  8, "rxtxFcl0"                              },  /* BrEpeHdrAdjBrg_rxtxFcl0_f*/
    { 3,  7,  5, "operationType"                         },  /* BrEpeHdrAdjBrg_operationType_f*/
    { 5,  7,  0, "headerHash7_3"                         },  /* BrEpeHdrAdjBrg_headerHash7_3_f*/
    {32,  8,  0, "ipSa"                                  },  /* BrEpeHdrAdjBrg_ipSa_f*/
};

static fields_t Epe_HdrProc_HdrEdit_PI_field[] = {
    { 8,  8, 14, "hdrProcHdrEditPISeq"                   },  /* EpeHdrProcHdrEditPI_hdrProcHdrEditPISeq_f*/
    { 6,  8,  8, "piSourcePortIsolateId"                 },  /* EpeHdrProcHdrEditPI_piSourcePortIsolateId_f*/
    { 8,  8,  0, "piDestMap_1"                           },  /* EpeHdrProcHdrEditPI_piDestMap_1_f*/
    {14,  9, 18, "piDestMap_0"                           },  /* EpeHdrProcHdrEditPI_piDestMap_0_f*/
    { 2,  9, 16, "piRoamingState"                        },  /* EpeHdrProcHdrEditPI_piRoamingState_f*/
    {14,  9,  2, "piMcastId"                             },  /* EpeHdrProcHdrEditPI_piMcastId_f*/
    { 2,  9,  0, "piSourcePort_1"                        },  /* EpeHdrProcHdrEditPI_piSourcePort_1_f*/
    {14, 10, 18, "piSourcePort_0"                        },  /* EpeHdrProcHdrEditPI_piSourcePort_0_f*/
    {17, 10,  1, "piNextHopPtr"                          },  /* EpeHdrProcHdrEditPI_piNextHopPtr_f*/
    { 1, 10,  0, "piNextHopExt"                          },  /* EpeHdrProcHdrEditPI_piNextHopExt_f*/
    { 1, 11, 31, "piCongestionValid"                     },  /* EpeHdrProcHdrEditPI_piCongestionValid_f*/
    { 1, 11, 30, "piFlowStats2Valid"                     },  /* EpeHdrProcHdrEditPI_piFlowStats2Valid_f*/
    {16, 11, 14, "piFlowStats2Ptr"                       },  /* EpeHdrProcHdrEditPI_piFlowStats2Ptr_f*/
    { 3, 11, 11, "piDestMuxPortType"                     },  /* EpeHdrProcHdrEditPI_piDestMuxPortType_f*/
    { 1, 11, 10, "piPortLogEn"                           },  /* EpeHdrProcHdrEditPI_piPortLogEn_f*/
    { 1, 11,  9, "piEvbDefaultLocalPhyPortValid"         },  /* EpeHdrProcHdrEditPI_piEvbDefaultLocalPhyPortValid_f*/
    { 1, 11,  8, "piEvbDefaultLogicPortValid"            },  /* EpeHdrProcHdrEditPI_piEvbDefaultLogicPortValid_f*/
    { 2, 11,  6, "piL2SpanId"                            },  /* EpeHdrProcHdrEditPI_piL2SpanId_f*/
    { 1, 11,  5, "piL2SpanEn"                            },  /* EpeHdrProcHdrEditPI_piL2SpanEn_f*/
    { 1, 11,  4, "piRandomLogEn"                         },  /* EpeHdrProcHdrEditPI_piRandomLogEn_f*/
    { 1, 11,  3, "piNewItagValid"                        },  /* EpeHdrProcHdrEditPI_piNewItagValid_f*/
    { 3, 11,  0, "piNewItag_1"                           },  /* EpeHdrProcHdrEditPI_piNewItag_1_f*/
    {21, 12, 11, "piNewItag_0"                           },  /* EpeHdrProcHdrEditPI_piNewItag_0_f*/
    { 1, 12, 10, "piL3SpanEn"                            },  /* EpeHdrProcHdrEditPI_piL3SpanEn_f*/
    { 2, 12,  8, "piL3SpanId"                            },  /* EpeHdrProcHdrEditPI_piL3SpanId_f*/
    { 1, 12,  7, "piMappedCfi"                           },  /* EpeHdrProcHdrEditPI_piMappedCfi_f*/
    { 1, 12,  6, "piUdpPtp"                              },  /* EpeHdrProcHdrEditPI_piUdpPtp_f*/
    { 2, 12,  4, "piPtpOffsetType"                       },  /* EpeHdrProcHdrEditPI_piPtpOffsetType_f*/
    { 1, 12,  3, "piNewL4CheckSumValid"                  },  /* EpeHdrProcHdrEditPI_piNewL4CheckSumValid_f*/
    { 3, 12,  0, "piNewCos"                              },  /* EpeHdrProcHdrEditPI_piNewCos_f*/
    { 1, 13, 31, "piNewCfi"                              },  /* EpeHdrProcHdrEditPI_piNewCfi_f*/
    { 8, 13, 23, "piStripOffset"                         },  /* EpeHdrProcHdrEditPI_piStripOffset_f*/
    { 8, 13, 15, "piNewTtl"                              },  /* EpeHdrProcHdrEditPI_piNewTtl_f*/
    { 1, 13, 14, "piNewTtlValid"                         },  /* EpeHdrProcHdrEditPI_piNewTtlValid_f*/
    { 2, 13, 12, "piNewTtlPacketType"                    },  /* EpeHdrProcHdrEditPI_piNewTtlPacketType_f*/
    { 1, 13, 11, "piNewIpDaValid"                        },  /* EpeHdrProcHdrEditPI_piNewIpDaValid_f*/
    { 1, 13, 10, "piNewL4DestPortValid"                  },  /* EpeHdrProcHdrEditPI_piNewL4DestPortValid_f*/
    {10, 13,  0, "piNewL4DestPort_1"                     },  /* EpeHdrProcHdrEditPI_piNewL4DestPort_1_f*/
    { 6, 14, 26, "piNewL4DestPort_0"                     },  /* EpeHdrProcHdrEditPI_piNewL4DestPort_0_f*/
    { 1, 14, 25, "piLoopbackEn"                          },  /* EpeHdrProcHdrEditPI_piLoopbackEn_f*/
    { 1, 14, 24, "piITagOffsetType"                      },  /* EpeHdrProcHdrEditPI_piITagOffsetType_f*/
    { 1, 14, 23, "piNewMacDaValid"                       },  /* EpeHdrProcHdrEditPI_piNewMacDaValid_f*/
    {23, 14,  0, "piNewMacDa_1"                          },  /* EpeHdrProcHdrEditPI_piNewMacDa_1_f*/
    {25, 15,  7, "piNewMacDa_0"                          },  /* EpeHdrProcHdrEditPI_piNewMacDa_0_f*/
    { 1, 15,  6, "piNewMacSaValid"                       },  /* EpeHdrProcHdrEditPI_piNewMacSaValid_f*/
    { 6, 15,  0, "piNewMacSa_2"                          },  /* EpeHdrProcHdrEditPI_piNewMacSa_2_f*/
    {23, 16,  0, "piNewMacSa_1"                          },  /* EpeHdrProcHdrEditPI_piNewMacSa_1_f*/
    {19, 17, 13, "piNewMacSa_0"                          },  /* EpeHdrProcHdrEditPI_piNewMacSa_0_f*/
    {13, 17,  0, "piNewIpDa_1"                           },  /* EpeHdrProcHdrEditPI_piNewIpDa_1_f*/
    {19, 18, 13, "piNewIpDa_0"                           },  /* EpeHdrProcHdrEditPI_piNewIpDa_0_f*/
    {13, 18,  0, "piNewL4CheckSum_1"                     },  /* EpeHdrProcHdrEditPI_piNewL4CheckSum_1_f*/
    { 3, 19, 29, "piNewL4CheckSum_0"                     },  /* EpeHdrProcHdrEditPI_piNewL4CheckSum_0_f*/
    { 5, 19, 24, "piL2NewHdrLen"                         },  /* EpeHdrProcHdrEditPI_piL2NewHdrLen_f*/
    { 7, 19, 17, "piL3NewHdrLen"                         },  /* EpeHdrProcHdrEditPI_piL3NewHdrLen_f*/
    { 1, 19, 16, "piSourcePortExtender"                  },  /* EpeHdrProcHdrEditPI_piSourcePortExtender_f*/
    {14, 19,  2, "piLogicDestPort"                       },  /* EpeHdrProcHdrEditPI_piLogicDestPort_f*/
    { 1, 19,  1, "piUseLogicPort"                        },  /* EpeHdrProcHdrEditPI_piUseLogicPort_f*/
    { 1, 19,  0, "piFragInfo"                            },  /* EpeHdrProcHdrEditPI_piFragInfo_f*/
    { 1, 20, 31, "piDestChannelLinkAggregateEn"          },  /* EpeHdrProcHdrEditPI_piDestChannelLinkAggregateEn_f*/
    { 1, 20, 30, "piSourceChannelLinkAggregateEn"        },  /* EpeHdrProcHdrEditPI_piSourceChannelLinkAggregateEn_f*/
    { 7, 20, 23, "piDestChannelLinkAggregate"            },  /* EpeHdrProcHdrEditPI_piDestChannelLinkAggregate_f*/
    { 7, 20, 16, "piSourceChannelLinkAggregate"          },  /* EpeHdrProcHdrEditPI_piSourceChannelLinkAggregate_f*/
    {16, 20,  0, "prLayer4CheckSum"                      },  /* EpeHdrProcHdrEditPI_prLayer4CheckSum_f*/
    { 2, 21, 30, "piSvlanTagOperation"                   },  /* EpeHdrProcHdrEditPI_piSvlanTagOperation_f*/
    {30, 21,  0, "piL2NewSvlanTag_1"                     },  /* EpeHdrProcHdrEditPI_piL2NewSvlanTag_1_f*/
    { 2, 22, 30, "piL2NewSvlanTag_0"                     },  /* EpeHdrProcHdrEditPI_piL2NewSvlanTag_0_f*/
    { 2, 22, 28, "piCvlanTagOperation"                   },  /* EpeHdrProcHdrEditPI_piCvlanTagOperation_f*/
    {28, 22,  0, "piL2NewCvlanTag_1"                     },  /* EpeHdrProcHdrEditPI_piL2NewCvlanTag_1_f*/
    { 4, 23, 28, "piL2NewCvlanTag_0"                     },  /* EpeHdrProcHdrEditPI_piL2NewCvlanTag_0_f*/
    { 1, 23, 27, "piCvlanIdOffsetType"                   },  /* EpeHdrProcHdrEditPI_piCvlanIdOffsetType_f*/
    { 1, 23, 26, "piNewIpCheckSumValid"                  },  /* EpeHdrProcHdrEditPI_piNewIpCheckSumValid_f*/
    {16, 23, 10, "piNewIpCheckSum"                       },  /* EpeHdrProcHdrEditPI_piNewIpCheckSum_f*/
    { 1, 23,  9, "piNewDscpValid"                        },  /* EpeHdrProcHdrEditPI_piNewDscpValid_f*/
    { 6, 23,  3, "piNewDscp"                             },  /* EpeHdrProcHdrEditPI_piNewDscp_f*/
    { 3,  7,  0, "piPortLoopbackIndex"                   },  /* EpeHdrProcHdrEditPI_piPortLoopbackIndex_f*/
};

static fields_t Epe_Oam_HdrEdit_PI_field[] = {
    { 8,  0, 18, "oamHdrEditSeq"                         },  /* EpeOamHdrEditPI_oamHdrEditSeq_f*/
    { 3,  0, 15, "piShareType"                           },  /* EpeOamHdrEditPI_piShareType_f*/
    {15,  0,  0, "piShareFields_3"                       },  /* EpeOamHdrEditPI_piShareFields_3_f*/
    {32,  1,  0, "piShareFields_2"                       },  /* EpeOamHdrEditPI_piShareFields_2_f*/
    {32,  2,  0, "piShareFields_1"                       },  /* EpeOamHdrEditPI_piShareFields_1_f*/
    {20,  3, 12, "piShareFields_0"                       },  /* EpeOamHdrEditPI_piShareFields_0_f*/
    {12,  3,  0, "piLogicSrcPort_1"                      },  /* EpeOamHdrEditPI_piLogicSrcPort_1_f*/
    { 2,  4, 30, "piLogicSrcPort_0"                      },  /* EpeOamHdrEditPI_piLogicSrcPort_0_f*/
    {14,  4, 16, "piFid"                                 },  /* EpeOamHdrEditPI_piFid_f*/
    { 6,  4, 10, "piPriority"                            },  /* EpeOamHdrEditPI_piPriority_f*/
    { 2,  4,  8, "piColor"                               },  /* EpeOamHdrEditPI_piColor_f*/
    { 1,  4,  7, "piDiscard"                             },  /* EpeOamHdrEditPI_piDiscard_f*/
    { 6,  4,  1, "piDiscardType"                         },  /* EpeOamHdrEditPI_piDiscardType_f*/
    { 1,  4,  0, "piLocalPhyPort_1"                      },  /* EpeOamHdrEditPI_piLocalPhyPort_1_f*/
    { 6,  5, 26, "piLocalPhyPort_0"                      },  /* EpeOamHdrEditPI_piLocalPhyPort_0_f*/
    {14,  5, 12, "piGlobalDestPort"                      },  /* EpeOamHdrEditPI_piGlobalDestPort_f*/
    { 1,  5, 11, "piExceptionEn"                         },  /* EpeOamHdrEditPI_piExceptionEn_f*/
    { 3,  5,  8, "piExceptionIndex"                      },  /* EpeOamHdrEditPI_piExceptionIndex_f*/
    { 1,  5,  7, "piFlowStats1Valid"                     },  /* EpeOamHdrEditPI_piFlowStats1Valid_f*/
    { 7,  5,  0, "piFlowStats1Ptr_1"                     },  /* EpeOamHdrEditPI_piFlowStats1Ptr_1_f*/
    { 9,  6, 23, "piFlowStats1Ptr_0"                     },  /* EpeOamHdrEditPI_piFlowStats1Ptr_0_f*/
    { 1,  6, 22, "piIsTcp"                               },  /* EpeOamHdrEditPI_piIsTcp_f*/
    { 8,  6, 14, "piLayer3Offset"                        },  /* EpeOamHdrEditPI_piLayer3Offset_f*/
    { 8,  6,  6, "piLayer4Offset"                        },  /* EpeOamHdrEditPI_piLayer4Offset_f*/
    { 1,  6,  5, "piFlowStats0Valid"                     },  /* EpeOamHdrEditPI_piFlowStats0Valid_f*/
    { 5,  6,  0, "piFlowStats0Ptr_1"                     },  /* EpeOamHdrEditPI_piFlowStats0Ptr_1_f*/
    {11,  7, 21, "piFlowStats0Ptr_0"                     },  /* EpeOamHdrEditPI_piFlowStats0Ptr_0_f*/
    { 1,  7, 20, "piAclLogEn0"                           },  /* EpeOamHdrEditPI_piAclLogEn0_f*/
    { 2,  7, 18, "piAclLogId0"                           },  /* EpeOamHdrEditPI_piAclLogId0_f*/
    { 1,  7, 17, "piAclLogEn1"                           },  /* EpeOamHdrEditPI_piAclLogEn1_f*/
    { 2,  7, 15, "piAclLogId1"                           },  /* EpeOamHdrEditPI_piAclLogId1_f*/
    { 1,  7, 14, "piAclLogEn2"                           },  /* EpeOamHdrEditPI_piAclLogEn2_f*/
    { 2,  7, 12, "piAclLogId2"                           },  /* EpeOamHdrEditPI_piAclLogId2_f*/
    { 1,  7, 11, "piAclLogEn3"                           },  /* EpeOamHdrEditPI_piAclLogEn3_f*/
    { 2,  7,  9, "piAclLogId3"                           },  /* EpeOamHdrEditPI_piAclLogId3_f*/
    { 1,  7,  8, "piHardError"                           },  /* EpeOamHdrEditPI_piHardError_f*/
    { 1,  7,  7, "piIsUdp"                               },  /* EpeOamHdrEditPI_piIsUdp_f*/
    { 1,  7,  6, "piFromCpuOrOam"                        },  /* EpeOamHdrEditPI_piFromCpuOrOam_f*/
    { 1,  7,  5, "piPacketHeaderEn"                      },  /* EpeOamHdrEditPI_piPacketHeaderEn_f*/
    { 1,  7,  4, "piPacketHeaderEnEgress"                },  /* EpeOamHdrEditPI_piPacketHeaderEnEgress_f*/
    { 4,  7,  0, "piPacketLength_1"                      },  /* EpeOamHdrEditPI_piPacketLength_1_f*/
    {10,  8, 22, "piPacketLength_0"                      },  /* EpeOamHdrEditPI_piPacketLength_0_f*/
    {13,  8,  9, "piDestVlanPtr"                         },  /* EpeOamHdrEditPI_piDestVlanPtr_f*/
    { 6,  8,  3, "piChannelId"                           },  /* EpeOamHdrEditPI_piChannelId_f*/
    { 1,  8,  2, "piFromCpuLmUpDisable"                  },  /* EpeOamHdrEditPI_piFromCpuLmUpDisable_f*/
    { 1,  8,  1, "piFromCpuLmDownDisable"                },  /* EpeOamHdrEditPI_piFromCpuLmDownDisable_f*/
    { 1,  8,  0, "piIsUp"                                },  /* EpeOamHdrEditPI_piIsUp_f*/
};

static fields_t EPE_HdrAdj_To_HdrProc_field[] = {
    { 1,  0, 17, "piFromCpuLmUpDisable"                  },  /* EPEHdrAdjToHdrProc_piFromCpuLmUpDisable_f*/
    { 1,  0, 16, "piFromCpuLmDownDisable"                },  /* EPEHdrAdjToHdrProc_piFromCpuLmDownDisable_f*/
    { 1,  0, 15, "piOamUseFid"                           },  /* EPEHdrAdjToHdrProc_piOamUseFid_f*/
    { 1,  0, 14, "piCongestionValid"                     },  /* EPEHdrAdjToHdrProc_piCongestionValid_f*/
    { 1,  0, 13, "piTxDmEn"                              },  /* EPEHdrAdjToHdrProc_piTxDmEn_f*/
    { 8,  0,  5, "piHeaderHash"                          },  /* EPEHdrAdjToHdrProc_piHeaderHash_f*/
    { 1,  0,  4, "piPortMacSaEn"                         },  /* EPEHdrAdjToHdrProc_piPortMacSaEn_f*/
    { 2,  0,  2, "piRoamingState"                        },  /* EPEHdrAdjToHdrProc_piRoamingState_f*/
    { 2,  0,  0, "piMcastId_1"                           },  /* EPEHdrAdjToHdrProc_piMcastId_1_f*/
    {12,  1, 20, "piMcastId_0"                           },  /* EPEHdrAdjToHdrProc_piMcastId_0_f*/
    { 1,  1, 19, "piNonCrc"                              },  /* EPEHdrAdjToHdrProc_piNonCrc_f*/
    { 1,  1, 18, "piCwAdded"                             },  /* EPEHdrAdjToHdrProc_piCwAdded_f*/
    { 4,  1, 14, "piMplsLabelDisable"                    },  /* EPEHdrAdjToHdrProc_piMplsLabelDisable_f*/
    { 1,  1, 13, "piSourcePortExtender"                  },  /* EPEHdrAdjToHdrProc_piSourcePortExtender_f*/
    {13,  1,  0, "piFid_1"                               },  /* EPEHdrAdjToHdrProc_piFid_1_f*/
    { 1,  2, 31, "piFid_0"                               },  /* EPEHdrAdjToHdrProc_piFid_0_f*/
    {14,  2, 17, "piPacketLength"                        },  /* EPEHdrAdjToHdrProc_piPacketLength_f*/
    {17,  2,  0, "piNextHopPtr"                          },  /* EPEHdrAdjToHdrProc_piNextHopPtr_f*/
};

static fields_t EPE_NextHop_To_HdrProc_field[] = {
    {16,  0, 13, "prLayer2HeaderProtocol"                },  /* EPENextHopToHdrProc_prLayer2HeaderProtocol_f*/
    {13,  0,  0, "prIpv6FlowLabel_1"                     },  /* EPENextHopToHdrProc_prIpv6FlowLabel_1_f*/
    { 7,  1, 25, "prIpv6FlowLabel_0"                     },  /* EPENextHopToHdrProc_prIpv6FlowLabel_0_f*/
    { 1,  1, 24, "prIpHeaderError"                       },  /* EPENextHopToHdrProc_prIpHeaderError_f*/
    { 1,  1, 23, "prIpOptions"                           },  /* EPENextHopToHdrProc_prIpOptions_f*/
    { 1,  1, 22, "prDontFrag"                            },  /* EPENextHopToHdrProc_prDontFrag_f*/
    { 2,  1, 20, "prFragInfo"                            },  /* EPENextHopToHdrProc_prFragInfo_f*/
    { 1,  1, 19, "prIsTcp"                               },  /* EPENextHopToHdrProc_prIsTcp_f*/
    { 1,  1, 18, "prIsUdp"                               },  /* EPENextHopToHdrProc_prIsUdp_f*/
    {12,  1,  6, "prLayer4InfoMapped"                    },  /* EPENextHopToHdrProc_prLayer4InfoMapped_f*/
    { 6,  1,  0, "prL4SourcePort_1"                      },  /* EPENextHopToHdrProc_prL4SourcePort_1_f*/
    {10,  2, 22, "prL4SourcePort_0"                      },  /* EPENextHopToHdrProc_prL4SourcePort_0_f*/
    {16,  2,  6, "prL4DestPort"                          },  /* EPENextHopToHdrProc_prL4DestPort_f*/
    { 6,  2,  0, "prLayer3HeaderProtocol_1"              },  /* EPENextHopToHdrProc_prLayer3HeaderProtocol_1_f*/
    { 2,  3, 30, "prLayer3HeaderProtocol_0"              },  /* EPENextHopToHdrProc_prLayer3HeaderProtocol_0_f*/
    {16,  3, 14, "prIpIdentification"                    },  /* EPENextHopToHdrProc_prIpIdentification_f*/
    { 1,  3, 13, "prMoreFrag"                            },  /* EPENextHopToHdrProc_prMoreFrag_f*/
    {13,  3,  0, "prFragOffset"                          },  /* EPENextHopToHdrProc_prFragOffset_f*/
};

static fields_t EPE_HdrProc_PP2L3_Bus_field[] = {
    { 8,  0, 17, "piSeq"                                 },  /* EPEHdrProcPP2L3Bus_piSeq_f*/
    { 1,  0, 16, "piHardError"                           },  /* EPEHdrProcPP2L3Bus_piHardError_f*/
    { 6,  0, 10, "piSourcePortIsolateId"                 },  /* EPEHdrProcPP2L3Bus_piSourcePortIsolateId_f*/
    {10,  0,  0, "piDestMap_1"                           },  /* EPEHdrProcPP2L3Bus_piDestMap_1_f*/
    {12,  1, 20, "piDestMap_0"                           },  /* EPEHdrProcPP2L3Bus_piDestMap_0_f*/
    { 4,  1, 16, "piRxOamType"                           },  /* EPEHdrProcPP2L3Bus_piRxOamType_f*/
    { 6,  1, 10, "piPriority"                            },  /* EPEHdrProcPP2L3Bus_piPriority_f*/
    { 2,  1,  8, "piColor"                               },  /* EPEHdrProcPP2L3Bus_piColor_f*/
    { 8,  1,  0, "piSourcePort_1"                        },  /* EPEHdrProcPP2L3Bus_piSourcePort_1_f*/
    { 8,  2, 24, "piSourcePort_0"                        },  /* EPEHdrProcPP2L3Bus_piSourcePort_0_f*/
    { 3,  2, 21, "piSourceCos"                           },  /* EPEHdrProcPP2L3Bus_piSourceCos_f*/
    { 3,  2, 18, "piPacketType"                          },  /* EPEHdrProcPP2L3Bus_piPacketType_f*/
    { 1,  2, 17, "piLogicPortType"                       },  /* EPEHdrProcPP2L3Bus_piLogicPortType_f*/
    { 1,  2, 16, "piNextHopExt"                          },  /* EPEHdrProcPP2L3Bus_piNextHopExt_f*/
    { 1,  2, 15, "piFromCpuOrOam"                        },  /* EPEHdrProcPP2L3Bus_piFromCpuOrOam_f*/
    { 1,  2, 14, "piDiscard"                             },  /* EPEHdrProcPP2L3Bus_piDiscard_f*/
    { 6,  2,  8, "piDiscardType"                         },  /* EPEHdrProcPP2L3Bus_piDiscardType_f*/
    { 1,  2,  7, "piSrcLeaf"                             },  /* EPEHdrProcPP2L3Bus_piSrcLeaf_f*/
    { 7,  2,  0, "piPacketTtl_1"                         },  /* EPEHdrProcPP2L3Bus_piPacketTtl_1_f*/
    { 1,  3, 31, "piPacketTtl_0"                         },  /* EPEHdrProcPP2L3Bus_piPacketTtl_0_f*/
    { 6,  3, 25, "piSrcDscp"                             },  /* EPEHdrProcPP2L3Bus_piSrcDscp_f*/
    { 1,  3, 24, "piPacketLengthAdjustType"              },  /* EPEHdrProcPP2L3Bus_piPacketLengthAdjustType_f*/
    { 8,  3, 16, "piPacketLengthAdjust"                  },  /* EPEHdrProcPP2L3Bus_piPacketLengthAdjust_f*/
    { 7,  3,  9, "piLocalPhyPort"                        },  /* EPEHdrProcPP2L3Bus_piLocalPhyPort_f*/
    { 3,  3,  6, "piPayloadOperation"                    },  /* EPEHdrProcPP2L3Bus_piPayloadOperation_f*/
    { 6,  3,  0, "piMac_2"                               },  /* EPEHdrProcPP2L3Bus_piMac_2_f*/
    {32,  4,  0, "piMac_1"                               },  /* EPEHdrProcPP2L3Bus_piMac_1_f*/
    {10,  5, 22, "piMac_0"                               },  /* EPEHdrProcPP2L3Bus_piMac_0_f*/
    { 1,  5, 21, "piRouteNoL2Edit"                       },  /* EPEHdrProcPP2L3Bus_piRouteNoL2Edit_f*/
    { 1,  5, 20, "piReplaceDscp"                         },  /* EPEHdrProcPP2L3Bus_piReplaceDscp_f*/
    { 1,  5, 19, "piServicePolicerValid"                 },  /* EPEHdrProcPP2L3Bus_piServicePolicerValid_f*/
    {13,  5,  6, "piDestVlanPtr"                         },  /* EPEHdrProcPP2L3Bus_piDestVlanPtr_f*/
    { 1,  5,  5, "piServiceAclQosEn"                     },  /* EPEHdrProcPP2L3Bus_piServiceAclQosEn_f*/
    { 1,  5,  4, "piReplaceCtagCos"                      },  /* EPEHdrProcPP2L3Bus_piReplaceCtagCos_f*/
    { 1,  5,  3, "piReplaceStagCos"                      },  /* EPEHdrProcPP2L3Bus_piReplaceStagCos_f*/
    { 1,  5,  2, "piLogicPortCheck"                      },  /* EPEHdrProcPP2L3Bus_piLogicPortCheck_f*/
    { 2,  5,  0, "piLogicDestPort_1"                     },  /* EPEHdrProcPP2L3Bus_piLogicDestPort_1_f*/
    {12,  6, 20, "piLogicDestPort_0"                     },  /* EPEHdrProcPP2L3Bus_piLogicDestPort_0_f*/
    {12,  6,  8, "piOutputSvlanId"                       },  /* EPEHdrProcPP2L3Bus_piOutputSvlanId_f*/
    { 8,  6,  0, "piOutputCvlanId_1"                     },  /* EPEHdrProcPP2L3Bus_piOutputCvlanId_1_f*/
    { 4,  7, 28, "piOutputCvlanId_0"                     },  /* EPEHdrProcPP2L3Bus_piOutputCvlanId_0_f*/
    { 3,  7, 25, "piDestMuxPortType"                     },  /* EPEHdrProcPP2L3Bus_piDestMuxPortType_f*/
    { 1,  7, 24, "piPortLogEn"                           },  /* EPEHdrProcPP2L3Bus_piPortLogEn_f*/
    { 2,  7, 22, "piSvlanTpidIndex"                      },  /* EPEHdrProcPP2L3Bus_piSvlanTpidIndex_f*/
    { 1,  7, 21, "piEvbDefaultLocalPhyPortValid"         },  /* EPEHdrProcPP2L3Bus_piEvbDefaultLocalPhyPortValid_f*/
    { 2,  7, 19, "piL2SpanId"                            },  /* EPEHdrProcPP2L3Bus_piL2SpanId_f*/
    { 1,  7, 18, "piL2SpanEn"                            },  /* EPEHdrProcPP2L3Bus_piL2SpanEn_f*/
    {14,  7,  4, "piGlobalDestPort"                      },  /* EPEHdrProcPP2L3Bus_piGlobalDestPort_f*/
    { 1,  7,  3, "piRandomLogEn"                         },  /* EPEHdrProcPP2L3Bus_piRandomLogEn_f*/
    { 1,  7,  2, "piUntagDefaultVlanId"                  },  /* EPEHdrProcPP2L3Bus_piUntagDefaultVlanId_f*/
    { 1,  7,  1, "piPortPolicerValid"                    },  /* EPEHdrProcPP2L3Bus_piPortPolicerValid_f*/
    { 1,  7,  0, "piL2AclEn0"                            },  /* EPEHdrProcPP2L3Bus_piL2AclEn0_f*/
    { 1,  8, 31, "piL2AclEn1"                            },  /* EPEHdrProcPP2L3Bus_piL2AclEn1_f*/
    { 1,  8, 30, "piL2AclEn2"                            },  /* EPEHdrProcPP2L3Bus_piL2AclEn2_f*/
    { 1,  8, 29, "piL2AclEn3"                            },  /* EPEHdrProcPP2L3Bus_piL2AclEn3_f*/
    { 1,  8, 28, "piUntagDefaultSvlan"                   },  /* EPEHdrProcPP2L3Bus_piUntagDefaultSvlan_f*/
    { 2,  8, 26, "piDot1QEn"                             },  /* EPEHdrProcPP2L3Bus_piDot1QEn_f*/
    { 2,  8, 24, "piIpgIndex"                            },  /* EPEHdrProcPP2L3Bus_piIpgIndex_f*/
    { 8,  8, 16, "piPortMacSa"                           },  /* EPEHdrProcPP2L3Bus_piPortMacSa_f*/
    {10,  8,  6, "piL2AclLabel"                          },  /* EPEHdrProcPP2L3Bus_piL2AclLabel_f*/
    { 6,  8,  0, "piFcoeOuiIndex_1"                      },  /* EPEHdrProcPP2L3Bus_piFcoeOuiIndex_1_f*/
    { 2,  9, 30, "piFcoeOuiIndex_0"                      },  /* EPEHdrProcPP2L3Bus_piFcoeOuiIndex_0_f*/
    { 1,  9, 29, "piCtagDeiEn"                           },  /* EPEHdrProcPP2L3Bus_piCtagDeiEn_f*/
    { 1,  9, 28, "piPortMacSaType"                       },  /* EPEHdrProcPP2L3Bus_piPortMacSaType_f*/
    { 1,  9, 27, "piEtherOamEdgePort"                    },  /* EPEHdrProcPP2L3Bus_piEtherOamEdgePort_f*/
    { 3,  9, 24, "piDefaultPcp"                          },  /* EPEHdrProcPP2L3Bus_piDefaultPcp_f*/
    { 1,  9, 23, "piMplsKeyUseLabel"                     },  /* EPEHdrProcPP2L3Bus_piMplsKeyUseLabel_f*/
    { 6,  9, 17, "piAclPortNum"                          },  /* EPEHdrProcPP2L3Bus_piAclPortNum_f*/
    { 1,  9, 16, "piMacKeyUseLabel"                      },  /* EPEHdrProcPP2L3Bus_piMacKeyUseLabel_f*/
    { 1,  9, 15, "piIpv4KeyUseLabel"                     },  /* EPEHdrProcPP2L3Bus_piIpv4KeyUseLabel_f*/
    { 1,  9, 14, "piIpv6KeyUseLabel"                     },  /* EPEHdrProcPP2L3Bus_piIpv6KeyUseLabel_f*/
    { 1,  9, 13, "piL2Ipv6AclEn0"                        },  /* EPEHdrProcPP2L3Bus_piL2Ipv6AclEn0_f*/
    { 1,  9, 12, "piL2Ipv6AclEn1"                        },  /* EPEHdrProcPP2L3Bus_piL2Ipv6AclEn1_f*/
    { 1,  9, 11, "piForceIpv4ToMacKey"                   },  /* EPEHdrProcPP2L3Bus_piForceIpv4ToMacKey_f*/
    { 1,  9, 10, "piForceIpv6ToMacKey"                   },  /* EPEHdrProcPP2L3Bus_piForceIpv6ToMacKey_f*/
    { 1,  9,  9, "piNewItagValid"                        },  /* EPEHdrProcPP2L3Bus_piNewItagValid_f*/
    { 9,  9,  0, "piNewItag_1"                           },  /* EPEHdrProcPP2L3Bus_piNewItag_1_f*/
    {15, 10, 17, "piNewItag_0"                           },  /* EPEHdrProcPP2L3Bus_piNewItag_0_f*/
    { 1, 10, 16, "piL3AclRoutedOnly"                     },  /* EPEHdrProcPP2L3Bus_piL3AclRoutedOnly_f*/
    { 1, 10, 15, "piL3AclEn0"                            },  /* EPEHdrProcPP2L3Bus_piL3AclEn0_f*/
    { 1, 10, 14, "piL3AclEn1"                            },  /* EPEHdrProcPP2L3Bus_piL3AclEn1_f*/
    { 1, 10, 13, "piL3AclEn2"                            },  /* EPEHdrProcPP2L3Bus_piL3AclEn2_f*/
    { 1, 10, 12, "piL3AclEn3"                            },  /* EPEHdrProcPP2L3Bus_piL3AclEn3_f*/
    { 1, 10, 11, "piL3SpanEn"                            },  /* EPEHdrProcPP2L3Bus_piL3SpanEn_f*/
    { 2, 10,  9, "piL3SpanId"                            },  /* EPEHdrProcPP2L3Bus_piL3SpanId_f*/
    { 9, 10,  0, "piL3AclLabel_1"                        },  /* EPEHdrProcPP2L3Bus_piL3AclLabel_1_f*/
    { 1, 11, 31, "piL3AclLabel_0"                        },  /* EPEHdrProcPP2L3Bus_piL3AclLabel_0_f*/
    { 1, 11, 30, "piL3Ipv6AclEn0"                        },  /* EPEHdrProcPP2L3Bus_piL3Ipv6AclEn0_f*/
    { 1, 11, 29, "piL3Ipv6AclEn1"                        },  /* EPEHdrProcPP2L3Bus_piL3Ipv6AclEn1_f*/
    {10, 11, 19, "piInterfaceId"                         },  /* EPEHdrProcPP2L3Bus_piInterfaceId_f*/
    { 1, 11, 18, "piInterfaceVlanIdEn"                   },  /* EPEHdrProcPP2L3Bus_piInterfaceVlanIdEn_f*/
    { 1, 11, 17, "piMtuExceptionEn"                      },  /* EPEHdrProcPP2L3Bus_piMtuExceptionEn_f*/
    { 2, 11, 15, "piMacSaType"                           },  /* EPEHdrProcPP2L3Bus_piMacSaType_f*/
    { 8, 11,  7, "piMacSa"                               },  /* EPEHdrProcPP2L3Bus_piMacSa_f*/
    { 1, 11,  6, "piMplsSectionLmEn"                     },  /* EPEHdrProcPP2L3Bus_piMplsSectionLmEn_f*/
    { 1, 11,  5, "piInterfaceSvlanTagged"                },  /* EPEHdrProcPP2L3Bus_piInterfaceSvlanTagged_f*/
    { 5, 11,  0, "piInterfaceVlanId_1"                   },  /* EPEHdrProcPP2L3Bus_piInterfaceVlanId_1_f*/
    { 7, 12, 25, "piInterfaceVlanId_0"                   },  /* EPEHdrProcPP2L3Bus_piInterfaceVlanId_0_f*/
    { 1, 12, 24, "piMcast"                               },  /* EPEHdrProcPP2L3Bus_piMcast_f*/
    { 1, 12, 23, "piL3EditPtrBit0"                       },  /* EPEHdrProcPP2L3Bus_piL3EditPtrBit0_f*/
    { 1, 12, 22, "piL2EditPtrBit0"                       },  /* EPEHdrProcPP2L3Bus_piL2EditPtrBit0_f*/
    { 6, 12, 16, "piMappedDscp"                          },  /* EPEHdrProcPP2L3Bus_piMappedDscp_f*/
    { 3, 12, 13, "piMappedCos"                           },  /* EPEHdrProcPP2L3Bus_piMappedCos_f*/
    { 3, 12, 10, "piMappedExp"                           },  /* EPEHdrProcPP2L3Bus_piMappedExp_f*/
    { 1, 12,  9, "piMappedCfi"                           },  /* EPEHdrProcPP2L3Bus_piMappedCfi_f*/
    { 1, 12,  8, "piL2Match"                             },  /* EPEHdrProcPP2L3Bus_piL2Match_f*/
    { 1, 12,  7, "piPortReflectiveDiscard"               },  /* EPEHdrProcPP2L3Bus_piPortReflectiveDiscard_f*/
    { 7, 12,  0, "piPtpOffset_1"                         },  /* EPEHdrProcPP2L3Bus_piPtpOffset_1_f*/
    { 1, 13, 31, "piPtpOffset_0"                         },  /* EPEHdrProcPP2L3Bus_piPtpOffset_0_f*/
    { 1, 13, 30, "piUdpPtp"                              },  /* EPEHdrProcPP2L3Bus_piUdpPtp_f*/
    { 1, 13, 29, "piExceptionEn"                         },  /* EPEHdrProcPP2L3Bus_piExceptionEn_f*/
    { 3, 13, 26, "piExceptionIndex"                      },  /* EPEHdrProcPP2L3Bus_piExceptionIndex_f*/
    { 1, 13, 25, "piEtherOamDiscard"                     },  /* EPEHdrProcPP2L3Bus_piEtherOamDiscard_f*/
    { 6, 13, 19, "piNewDscp"                             },  /* EPEHdrProcPP2L3Bus_piNewDscp_f*/
    { 3, 13, 16, "piNewCos"                              },  /* EPEHdrProcPP2L3Bus_piNewCos_f*/
    { 1, 13, 15, "piNewCfi"                              },  /* EPEHdrProcPP2L3Bus_piNewCfi_f*/
    { 8, 13,  7, "piStripOffset"                         },  /* EPEHdrProcPP2L3Bus_piStripOffset_f*/
    { 7, 13,  0, "piNewTtl_1"                            },  /* EPEHdrProcPP2L3Bus_piNewTtl_1_f*/
    { 1, 14, 31, "piNewTtl_0"                            },  /* EPEHdrProcPP2L3Bus_piNewTtl_0_f*/
    { 1, 14, 30, "piNewTtlValid"                         },  /* EPEHdrProcPP2L3Bus_piNewTtlValid_f*/
    { 2, 14, 28, "piNewTtlPacketType"                    },  /* EPEHdrProcPP2L3Bus_piNewTtlPacketType_f*/
    { 1, 14, 27, "piNewDscpValid"                        },  /* EPEHdrProcPP2L3Bus_piNewDscpValid_f*/
    { 1, 14, 26, "piCvlanIdOffsetType"                   },  /* EPEHdrProcPP2L3Bus_piCvlanIdOffsetType_f*/
    { 2, 14, 24, "piSvlanTagOperation"                   },  /* EPEHdrProcPP2L3Bus_piSvlanTagOperation_f*/
    {24, 14,  0, "piL2NewSvlanTag_1"                     },  /* EPEHdrProcPP2L3Bus_piL2NewSvlanTag_1_f*/
    { 8, 15, 24, "piL2NewSvlanTag_0"                     },  /* EPEHdrProcPP2L3Bus_piL2NewSvlanTag_0_f*/
    { 2, 15, 22, "piCvlanTagOperation"                   },  /* EPEHdrProcPP2L3Bus_piCvlanTagOperation_f*/
    {22, 15,  0, "piL2NewCvlanTag_1"                     },  /* EPEHdrProcPP2L3Bus_piL2NewCvlanTag_1_f*/
    {10, 16, 22, "piL2NewCvlanTag_0"                     },  /* EPEHdrProcPP2L3Bus_piL2NewCvlanTag_0_f*/
    { 1, 16, 21, "piTunnelMtuCheck"                      },  /* EPEHdrProcPP2L3Bus_piTunnelMtuCheck_f*/
    {14, 16,  7, "piTunnelMtuSize"                       },  /* EPEHdrProcPP2L3Bus_piTunnelMtuSize_f*/
    { 3, 16,  4, "piLinkLmCos"                           },  /* EPEHdrProcPP2L3Bus_piLinkLmCos_f*/
    { 4, 16,  0, "piDefaultVlanId_1"                     },  /* EPEHdrProcPP2L3Bus_piDefaultVlanId_1_f*/
    { 8, 17, 24, "piDefaultVlanId_0"                     },  /* EPEHdrProcPP2L3Bus_piDefaultVlanId_0_f*/
    { 8, 17, 16, "piMplsLabelSpace"                      },  /* EPEHdrProcPP2L3Bus_piMplsLabelSpace_f*/
    { 2, 17, 14, "piPtpOffsetType"                       },  /* EPEHdrProcPP2L3Bus_piPtpOffsetType_f*/
    { 1, 17, 13, "piNewL4CheckSumValid"                  },  /* EPEHdrProcPP2L3Bus_piNewL4CheckSumValid_f*/
    {13, 17,  0, "piNewL4CheckSum_1"                     },  /* EPEHdrProcPP2L3Bus_piNewL4CheckSum_1_f*/
    { 3, 18, 29, "piNewL4CheckSum_0"                     },  /* EPEHdrProcPP2L3Bus_piNewL4CheckSum_0_f*/
    { 6, 18, 23, "piChannelId"                           },  /* EPEHdrProcPP2L3Bus_piChannelId_f*/
    { 1, 18, 22, "piFlowStats2Valid"                     },  /* EPEHdrProcPP2L3Bus_piFlowStats2Valid_f*/
    {16, 18,  6, "piFlowStats2Ptr"                       },  /* EPEHdrProcPP2L3Bus_piFlowStats2Ptr_f*/
    { 1, 18,  5, "piSourceCfi"                           },  /* EPEHdrProcPP2L3Bus_piSourceCfi_f*/
    { 1, 18,  4, "piDsL3EditExist"                       },  /* EPEHdrProcPP2L3Bus_piDsL3EditExist_f*/
    { 1, 18,  3, "piDsL2EditExist"                       },  /* EPEHdrProcPP2L3Bus_piDsL2EditExist_f*/
    { 1, 18,  2, "piEvbDefaultLogicPortValid"            },  /* EPEHdrProcPP2L3Bus_piEvbDefaultLogicPortValid_f*/
    { 2, 18,  0, "piLinkLmType"                          },  /* EPEHdrProcPP2L3Bus_piLinkLmType_f*/
    { 2, 19, 30, "piLinkLmCosType"                       },  /* EPEHdrProcPP2L3Bus_piLinkLmCosType_f*/
    {14, 19, 16, "piLinkLmIndexBase"                     },  /* EPEHdrProcPP2L3Bus_piLinkLmIndexBase_f*/
    { 1, 19, 15, "piITagOffsetType"                      },  /* EPEHdrProcPP2L3Bus_piITagOffsetType_f*/
    { 1, 19, 14, "piMtuCheckEn"                          },  /* EPEHdrProcPP2L3Bus_piMtuCheckEn_f*/
    { 1, 19, 13, "piUseLogicPort"                        },  /* EPEHdrProcPP2L3Bus_piUseLogicPort_f*/
    { 1, 19, 12, "piEtherLmValid"                        },  /* EPEHdrProcPP2L3Bus_piEtherLmValid_f*/
    {12, 19,  0, "piMtuSize_1"                           },  /* EPEHdrProcPP2L3Bus_piMtuSize_1_f*/
    { 2, 20, 30, "piMtuSize_0"                           },  /* EPEHdrProcPP2L3Bus_piMtuSize_0_f*/
    { 1, 20, 29, "piForceIpv6Key"                        },  /* EPEHdrProcPP2L3Bus_piForceIpv6Key_f*/
    { 1, 20, 28, "piLinkOrSectionOam"                    },  /* EPEHdrProcPP2L3Bus_piLinkOrSectionOam_f*/
    { 1, 20, 27, "piIsUp"                                },  /* EPEHdrProcPP2L3Bus_piIsUp_f*/
    { 1, 20, 26, "piMacDaMcastMode"                      },  /* EPEHdrProcPP2L3Bus_piMacDaMcastMode_f*/
    { 1, 20, 25, "piBypassAll"                           },  /* EPEHdrProcPP2L3Bus_piBypassAll_f*/
    { 1, 20, 24, "piEntropyLabelExist"                   },  /* EPEHdrProcPP2L3Bus_piEntropyLabelExist_f*/
    { 1, 20, 23, "piGalExist"                            },  /* EPEHdrProcPP2L3Bus_piGalExist_f*/
    { 3, 20, 20, "piShareType"                           },  /* EPEHdrProcPP2L3Bus_piShareType_f*/
    { 1, 20, 19, "piBridgeOperation"                     },  /* EPEHdrProcPP2L3Bus_piBridgeOperation_f*/
    {12, 20,  7, "piOamVlan"                             },  /* EPEHdrProcPP2L3Bus_piOamVlan_f*/
    { 7, 20,  0, "piShareFields_3"                       },  /* EPEHdrProcPP2L3Bus_piShareFields_3_f*/
    {32, 21,  0, "piShareFields_2"                       },  /* EPEHdrProcPP2L3Bus_piShareFields_2_f*/
    {32, 22,  0, "piShareFields_1"                       },  /* EPEHdrProcPP2L3Bus_piShareFields_1_f*/
    {28, 23,  4, "piShareFields_0"                       },  /* EPEHdrProcPP2L3Bus_piShareFields_0_f*/
    { 1, 23,  3, "piMirroredPacket"                      },  /* EPEHdrProcPP2L3Bus_piMirroredPacket_f*/
    { 1, 23,  2, "piFromFabric"                          },  /* EPEHdrProcPP2L3Bus_piFromFabric_f*/
    { 1, 23,  1, "piPacketHeaderEn"                      },  /* EPEHdrProcPP2L3Bus_piPacketHeaderEn_f*/
    { 1, 23,  0, "piPacketHeaderEnEgress"                },  /* EPEHdrProcPP2L3Bus_piPacketHeaderEnEgress_f*/
    {14, 24, 18, "piLogicSrcPort"                        },  /* EPEHdrProcPP2L3Bus_piLogicSrcPort_f*/
    { 3, 24, 15, "piPortLoopbackIndex"                   },  /* EPEHdrProcPP2L3Bus_piPortLoopbackIndex_f*/
    { 3, 24, 12, "piSectionLmExp"                        },  /* EPEHdrProcPP2L3Bus_piSectionLmExp_f*/
    { 1, 24, 11, "piDsNextHopDotBypass"                  },  /* EPEHdrProcPP2L3Bus_piDsNextHopDotBypass_f*/
    {11, 24,  0, "prSvlanId_1"                           },  /* EPEHdrProcPP2L3Bus_prSvlanId_1_f*/
    { 1, 25, 31, "prSvlanId_0"                           },  /* EPEHdrProcPP2L3Bus_prSvlanId_0_f*/
    {12, 25, 19, "prCvlanId"                             },  /* EPEHdrProcPP2L3Bus_prCvlanId_f*/
    { 3, 25, 16, "prStagCos"                             },  /* EPEHdrProcPP2L3Bus_prStagCos_f*/
    { 3, 25, 13, "prCtagCos"                             },  /* EPEHdrProcPP2L3Bus_prCtagCos_f*/
    { 1, 25, 12, "prStagCfi"                             },  /* EPEHdrProcPP2L3Bus_prStagCfi_f*/
    { 1, 25, 11, "prCtagCfi"                             },  /* EPEHdrProcPP2L3Bus_prCtagCfi_f*/
    { 1, 25, 10, "prSvlanIdValid"                        },  /* EPEHdrProcPP2L3Bus_prSvlanIdValid_f*/
    { 1, 25,  9, "prCvlanIdValid"                        },  /* EPEHdrProcPP2L3Bus_prCvlanIdValid_f*/
    { 9, 25,  0, "prMacDa_2"                             },  /* EPEHdrProcPP2L3Bus_prMacDa_2_f*/
    {32, 26,  0, "prMacDa_1"                             },  /* EPEHdrProcPP2L3Bus_prMacDa_1_f*/
    { 7, 27, 25, "prMacDa_0"                             },  /* EPEHdrProcPP2L3Bus_prMacDa_0_f*/
    { 4, 27, 21, "prLayer3Type"                          },  /* EPEHdrProcPP2L3Bus_prLayer3Type_f*/
    { 8, 27, 13, "prLayer3Offset"                        },  /* EPEHdrProcPP2L3Bus_prLayer3Offset_f*/
    { 4, 27,  9, "prLayer4UserType"                      },  /* EPEHdrProcPP2L3Bus_prLayer4UserType_f*/
    { 8, 27,  1, "prLayer4Offset"                        },  /* EPEHdrProcPP2L3Bus_prLayer4Offset_f*/
    { 1, 27,  0, "prIpDa_4"                              },  /* EPEHdrProcPP2L3Bus_prIpDa_4_f*/
    {32, 28,  0, "prIpDa_3"                              },  /* EPEHdrProcPP2L3Bus_prIpDa_3_f*/
    {32, 29,  0, "prIpDa_2"                              },  /* EPEHdrProcPP2L3Bus_prIpDa_2_f*/
    {32, 30,  0, "prIpDa_1"                              },  /* EPEHdrProcPP2L3Bus_prIpDa_1_f*/
    {31, 31,  1, "prIpDa_0"                              },  /* EPEHdrProcPP2L3Bus_prIpDa_0_f*/
    { 1, 31,  0, "prLayer4Type_1"                        },  /* EPEHdrProcPP2L3Bus_prLayer4Type_1_f*/
    { 3, 32, 29, "prLayer4Type_0"                        },  /* EPEHdrProcPP2L3Bus_prLayer4Type_0_f*/
    { 4, 32, 25, "prLayer2Type"                          },  /* EPEHdrProcPP2L3Bus_prLayer2Type_f*/
    {25, 32,  0, "prMacSa_1"                             },  /* EPEHdrProcPP2L3Bus_prMacSa_1_f*/
    {23, 33,  9, "prMacSa_0"                             },  /* EPEHdrProcPP2L3Bus_prMacSa_0_f*/
    { 9, 33,  0, "prIpSa_4"                              },  /* EPEHdrProcPP2L3Bus_prIpSa_4_f*/
    {32, 34,  0, "prIpSa_3"                              },  /* EPEHdrProcPP2L3Bus_prIpSa_3_f*/
    {32, 35,  0, "prIpSa_2"                              },  /* EPEHdrProcPP2L3Bus_prIpSa_2_f*/
    {32, 36,  0, "prIpSa_1"                              },  /* EPEHdrProcPP2L3Bus_prIpSa_1_f*/
    {23, 37,  9, "prIpSa_0"                              },  /* EPEHdrProcPP2L3Bus_prIpSa_0_f*/
    { 8, 37,  1, "prTos"                                 },  /* EPEHdrProcPP2L3Bus_prTos_f*/
    { 1, 37,  0, "prLayer4CheckSum_1"                    },  /* EPEHdrProcPP2L3Bus_prLayer4CheckSum_1_f*/
    {15, 38, 17, "prLayer4CheckSum_0"                    },  /* EPEHdrProcPP2L3Bus_prLayer4CheckSum_0_f*/
    { 8, 38,  9, "prTtl"                                 },  /* EPEHdrProcPP2L3Bus_prTtl_f*/
    { 8, 38,  1, "prY1731OamOpCode"                      },  /* EPEHdrProcPP2L3Bus_prY1731OamOpCode_f*/
    { 1, 38,  0, "newDscpUpdate"                         },  /* EPEHdrProcPP2L3Bus_newDscpUpdate_f*/
};

static fields_t Epe_HdrAdj_NextHop_PI_field[] = {
    { 7,  8,  0, "hdrAdjNextHopPiSeq_1"                  },  /* EpeHdrAdjNextHopPI_hdrAdjNextHopPiSeq_1_f*/
    { 1,  9, 31, "hdrAdjNextHopPiSeq_0"                  },  /* EpeHdrAdjNextHopPI_hdrAdjNextHopPiSeq_0_f*/
    { 6,  9, 25, "piSourcePortIsolateId"                 },  /* EpeHdrAdjNextHopPI_piSourcePortIsolateId_f*/
    { 1,  9, 24, "piOamTunnelEn"                         },  /* EpeHdrAdjNextHopPI_piOamTunnelEn_f*/
    { 1,  9, 23, "piIsUp"                                },  /* EpeHdrAdjNextHopPI_piIsUp_f*/
    {16,  9,  7, "piSourcePort"                          },  /* EpeHdrAdjNextHopPI_piSourcePort_f*/
    { 3,  9,  4, "piSourceCos"                           },  /* EpeHdrAdjNextHopPI_piSourceCos_f*/
    { 1,  9,  3, "piLogicPortType"                       },  /* EpeHdrAdjNextHopPI_piLogicPortType_f*/
    { 1,  9,  2, "piSrcSvlanIdValid"                     },  /* EpeHdrAdjNextHopPI_piSrcSvlanIdValid_f*/
    { 1,  9,  1, "piSrcCvlanIdValid"                     },  /* EpeHdrAdjNextHopPI_piSrcCvlanIdValid_f*/
    { 1,  9,  0, "piCTagAction_1"                        },  /* EpeHdrAdjNextHopPI_piCTagAction_1_f*/
    { 1, 10, 31, "piCTagAction_0"                        },  /* EpeHdrAdjNextHopPI_piCTagAction_0_f*/
    { 6, 10, 25, "piDiscardType"                         },  /* EpeHdrAdjNextHopPI_piDiscardType_f*/
    { 1, 10, 24, "piSrcLeaf"                             },  /* EpeHdrAdjNextHopPI_piSrcLeaf_f*/
    { 8, 10, 16, "piPacketTtl"                           },  /* EpeHdrAdjNextHopPI_piPacketTtl_f*/
    { 6, 10, 10, "piSrcDscp"                             },  /* EpeHdrAdjNextHopPI_piSrcDscp_f*/
    { 1, 10,  9, "piPacketLengthAdjustType"              },  /* EpeHdrAdjNextHopPI_piPacketLengthAdjustType_f*/
    { 8, 10,  1, "piPacketLengthAdjust"                  },  /* EpeHdrAdjNextHopPI_piPacketLengthAdjust_f*/
    { 1, 10,  0, "piMacKnown"                            },  /* EpeHdrAdjNextHopPI_piMacKnown_f*/
    { 2, 11, 30, "piSTagAction"                          },  /* EpeHdrAdjNextHopPI_piSTagAction_f*/
    { 7, 11, 23, "piLocalPhyPort"                        },  /* EpeHdrAdjNextHopPI_piLocalPhyPort_f*/
    { 1, 11, 22, "piHardError"                           },  /* EpeHdrAdjNextHopPI_piHardError_f*/
    { 1, 11, 21, "piSourceCfi"                           },  /* EpeHdrAdjNextHopPI_piSourceCfi_f*/
    { 3, 11, 18, "piParserOffset"                        },  /* EpeHdrAdjNextHopPI_piParserOffset_f*/
    { 1, 11, 17, "piPbbCheckDiscard"                     },  /* EpeHdrAdjNextHopPI_piPbbCheckDiscard_f*/
    { 1, 11, 16, "piGalExist"                            },  /* EpeHdrAdjNextHopPI_piGalExist_f*/
    { 1, 11, 15, "piEntropyLabelExist"                   },  /* EpeHdrAdjNextHopPI_piEntropyLabelExist_f*/
    { 1, 11, 14, "piLinkOrSectionOam"                    },  /* EpeHdrAdjNextHopPI_piLinkOrSectionOam_f*/
    { 1, 11, 13, "piAclDscpValid"                        },  /* EpeHdrAdjNextHopPI_piAclDscpValid_f*/
    { 2, 11, 11, "piSvlanTpidIndex"                      },  /* EpeHdrAdjNextHopPI_piSvlanTpidIndex_f*/
    { 8, 11,  3, "piPtpOffset"                           },  /* EpeHdrAdjNextHopPI_piPtpOffset_f*/
    { 3, 11,  0, "piShareType"                           },  /* EpeHdrAdjNextHopPI_piShareType_f*/
    {32, 12,  0, "piShareFields_3"                       },  /* EpeHdrAdjNextHopPI_piShareFields_3_f*/
    {32, 13,  0, "piChop1Rsvd_2"                         },  /* EpeHdrAdjNextHopPI_piChop1Rsvd_2_f*/
    {32, 14,  0, "piChop1Rsvd_1"                         },  /* EpeHdrAdjNextHopPI_piChop1Rsvd_1_f*/
    {32, 15,  0, "piChop1Rsvd_0"                         },  /* EpeHdrAdjNextHopPI_piChop1Rsvd_0_f*/
    { 7,  0,  0, "piShareFields_2"                       },  /* EpeHdrAdjNextHopPI_piShareFields_2_f*/
    {32,  1,  0, "piShareFields_1"                       },  /* EpeHdrAdjNextHopPI_piShareFields_1_f*/
    {28,  2,  4, "piShareFields_0"                       },  /* EpeHdrAdjNextHopPI_piShareFields_0_f*/
    { 4,  2,  0, "piLogicSrcPort_1"                      },  /* EpeHdrAdjNextHopPI_piLogicSrcPort_1_f*/
    {10,  3, 22, "piLogicSrcPort_0"                      },  /* EpeHdrAdjNextHopPI_piLogicSrcPort_0_f*/
    { 6,  3, 16, "piChannelId"                           },  /* EpeHdrAdjNextHopPI_piChannelId_f*/
    { 6,  3, 10, "piPriority"                            },  /* EpeHdrAdjNextHopPI_piPriority_f*/
    { 2,  3,  8, "piColor"                               },  /* EpeHdrAdjNextHopPI_piColor_f*/
    { 1,  3,  7, "piDsNextHopDotBypass"                  },  /* EpeHdrAdjNextHopPI_piDsNextHopDotBypass_f*/
    { 1,  3,  6, "piBypassAll"                           },  /* EpeHdrAdjNextHopPI_piBypassAll_f*/
    { 1,  3,  5, "piFromCpuOrOam"                        },  /* EpeHdrAdjNextHopPI_piFromCpuOrOam_f*/
    { 4,  3,  1, "piRxOamType"                           },  /* EpeHdrAdjNextHopPI_piRxOamType_f*/
    { 1,  3,  0, "piDiscard"                             },  /* EpeHdrAdjNextHopPI_piDiscard_f*/
    { 1,  4, 31, "piIngressEditEn"                       },  /* EpeHdrAdjNextHopPI_piIngressEditEn_f*/
    { 1,  4, 30, "piPacketHeaderEnEgress"                },  /* EpeHdrAdjNextHopPI_piPacketHeaderEnEgress_f*/
    { 1,  4, 29, "piFromFabric"                          },  /* EpeHdrAdjNextHopPI_piFromFabric_f*/
    { 1,  4, 28, "piIngressEditNextHopBypassAll"         },  /* EpeHdrAdjNextHopPI_piIngressEditNextHopBypassAll_f*/
    { 1,  4, 27, "piPacketHeaderEn"                      },  /* EpeHdrAdjNextHopPI_piPacketHeaderEn_f*/
    { 1,  4, 26, "piBridgeOperation"                     },  /* EpeHdrAdjNextHopPI_piBridgeOperation_f*/
    { 3,  4, 23, "piPacketType"                          },  /* EpeHdrAdjNextHopPI_piPacketType_f*/
    { 1,  4, 22, "piNextHopExt"                          },  /* EpeHdrAdjNextHopPI_piNextHopExt_f*/
    {22,  4,  0, "piDestMap"                             },  /* EpeHdrAdjNextHopPI_piDestMap_f*/
    {32,  5,  0, "piChop0Rsvd_2"                         },  /* EpeHdrAdjNextHopPI_piChop0Rsvd_2_f*/
    {32,  6,  0, "piChop0Rsvd_1"                         },  /* EpeHdrAdjNextHopPI_piChop0Rsvd_1_f*/
    {32,  7,  0, "piChop0Rsvd_0"                         },  /* EpeHdrAdjNextHopPI_piChop0Rsvd_0_f*/
};

static fields_t Epe_Parser_NextHop_PR_field[] = {
    { 8, 16,  1, "parserEpeSeq"                          },  /* EpeParserNextHopPR_parserEpeSeq_f*/
    { 1, 16,  0, "prParserLengthError"                   },  /* EpeParserNextHopPR_prParserLengthError_f*/
    { 4, 17, 28, "prLayer2Type"                          },  /* EpeParserNextHopPR_prLayer2Type_f*/
    {28, 17,  0, "prMacDa_1"                             },  /* EpeParserNextHopPR_prMacDa_1_f*/
    {20, 18, 12, "prMacDa_0"                             },  /* EpeParserNextHopPR_prMacDa_0_f*/
    { 1, 18, 11, "prSvlanIdValid"                        },  /* EpeParserNextHopPR_prSvlanIdValid_f*/
    { 1, 18, 10, "prCvlanIdValid"                        },  /* EpeParserNextHopPR_prCvlanIdValid_f*/
    {10, 18,  0, "prSvlanId_1"                           },  /* EpeParserNextHopPR_prSvlanId_1_f*/
    { 2, 19, 30, "prSvlanId_0"                           },  /* EpeParserNextHopPR_prSvlanId_0_f*/
    {12, 19, 18, "prCvlanId"                             },  /* EpeParserNextHopPR_prCvlanId_f*/
    { 3, 19, 15, "prStagCos"                             },  /* EpeParserNextHopPR_prStagCos_f*/
    { 3, 19, 12, "prCtagCos"                             },  /* EpeParserNextHopPR_prCtagCos_f*/
    { 1, 19, 11, "prStagCfi"                             },  /* EpeParserNextHopPR_prStagCfi_f*/
    { 1, 19, 10, "prCtagCfi"                             },  /* EpeParserNextHopPR_prCtagCfi_f*/
    { 8, 19,  2, "prLayer3Offset"                        },  /* EpeParserNextHopPR_prLayer3Offset_f*/
    { 2, 19,  0, "prLayer3Type_1"                        },  /* EpeParserNextHopPR_prLayer3Type_1_f*/
    { 2, 20, 30, "prLayer3Type_0"                        },  /* EpeParserNextHopPR_prLayer3Type_0_f*/
    {30, 20,  0, "prIpDa_4"                              },  /* EpeParserNextHopPR_prIpDa_4_f*/
    {32, 21,  0, "prIpDa_3"                              },  /* EpeParserNextHopPR_prIpDa_3_f*/
    {32, 22,  0, "prIpDa_2"                              },  /* EpeParserNextHopPR_prIpDa_2_f*/
    {32, 23,  0, "prIpDa_1"                              },  /* EpeParserNextHopPR_prIpDa_1_f*/
    { 2, 24, 30, "prIpDa_0"                              },  /* EpeParserNextHopPR_prIpDa_0_f*/
    {30, 24,  0, "prIpSa_4"                              },  /* EpeParserNextHopPR_prIpSa_4_f*/
    {32, 25,  0, "prIpSa_3"                              },  /* EpeParserNextHopPR_prIpSa_3_f*/
    {32, 26,  0, "piChop1Rsvd_5"                         },  /* EpeParserNextHopPR_piChop1Rsvd_5_f*/
    {32, 27,  0, "piChop1Rsvd_4"                         },  /* EpeParserNextHopPR_piChop1Rsvd_4_f*/
    {32, 28,  0, "piChop1Rsvd_3"                         },  /* EpeParserNextHopPR_piChop1Rsvd_3_f*/
    {32, 29,  0, "piChop1Rsvd_2"                         },  /* EpeParserNextHopPR_piChop1Rsvd_2_f*/
    {32, 30,  0, "piChop1Rsvd_1"                         },  /* EpeParserNextHopPR_piChop1Rsvd_1_f*/
    {32, 31,  0, "piChop1Rsvd_0"                         },  /* EpeParserNextHopPR_piChop1Rsvd_0_f*/
    { 9,  0,  0, "prIpSa_2"                              },  /* EpeParserNextHopPR_prIpSa_2_f*/
    {32,  1,  0, "prIpSa_1"                              },  /* EpeParserNextHopPR_prIpSa_1_f*/
    {25,  2,  7, "prIpSa_0"                              },  /* EpeParserNextHopPR_prIpSa_0_f*/
    { 7,  2,  0, "prTos_1"                               },  /* EpeParserNextHopPR_prTos_1_f*/
    { 1,  3, 31, "prTos_0"                               },  /* EpeParserNextHopPR_prTos_0_f*/
    { 8,  3, 23, "prTtl"                                 },  /* EpeParserNextHopPR_prTtl_f*/
    { 8,  3, 15, "prLayer4Offset"                        },  /* EpeParserNextHopPR_prLayer4Offset_f*/
    { 1,  3, 14, "prIsIsatapInterface"                   },  /* EpeParserNextHopPR_prIsIsatapInterface_f*/
    { 5,  3,  9, "prRid"                                 },  /* EpeParserNextHopPR_prRid_f*/
    { 9,  3,  0, "prGreKey_1"                            },  /* EpeParserNextHopPR_prGreKey_1_f*/
    {23,  4,  9, "prGreKey_0"                            },  /* EpeParserNextHopPR_prGreKey_0_f*/
    { 9,  4,  0, "prUdpPtpCorrectionField_1"             },  /* EpeParserNextHopPR_prUdpPtpCorrectionField_1_f*/
    {23,  5,  9, "prUdpPtpCorrectionField_0"             },  /* EpeParserNextHopPR_prUdpPtpCorrectionField_0_f*/
    { 9,  5,  0, "prUdpPtpTimestamp_2"                   },  /* EpeParserNextHopPR_prUdpPtpTimestamp_2_f*/
    {32,  6,  0, "prUdpPtpTimestamp_1"                   },  /* EpeParserNextHopPR_prUdpPtpTimestamp_1_f*/
    {23,  7,  9, "prUdpPtpTimestamp_0"                   },  /* EpeParserNextHopPR_prUdpPtpTimestamp_0_f*/
    { 4,  7,  5, "prLayer4Type"                          },  /* EpeParserNextHopPR_prLayer4Type_f*/
    { 4,  7,  1, "prLayer4UserType"                      },  /* EpeParserNextHopPR_prLayer4UserType_f*/
    { 1,  7,  0, "prLayer4CheckSum_1"                    },  /* EpeParserNextHopPR_prLayer4CheckSum_1_f*/
    {15,  8, 17, "prLayer4CheckSum_0"                    },  /* EpeParserNextHopPR_prLayer4CheckSum_0_f*/
    {17,  8,  0, "prMacSa_1"                             },  /* EpeParserNextHopPR_prMacSa_1_f*/
    {31,  9,  1, "prMacSa_0"                             },  /* EpeParserNextHopPR_prMacSa_0_f*/
    { 1,  9,  0, "prFragInfo_1"                          },  /* EpeParserNextHopPR_prFragInfo_1_f*/
    {32, 10,  0, "piChop0Rsvd_5"                         },  /* EpeParserNextHopPR_piChop0Rsvd_5_f*/
    {32, 11,  0, "piChop0Rsvd_4"                         },  /* EpeParserNextHopPR_piChop0Rsvd_4_f*/
    {32, 12,  0, "piChop0Rsvd_3"                         },  /* EpeParserNextHopPR_piChop0Rsvd_3_f*/
    {32, 13,  0, "piChop0Rsvd_2"                         },  /* EpeParserNextHopPR_piChop0Rsvd_2_f*/
    {32, 14,  0, "piChop0Rsvd_1"                         },  /* EpeParserNextHopPR_piChop0Rsvd_1_f*/
    {32, 15,  0, "piChop0Rsvd_0"                         },  /* EpeParserNextHopPR_piChop0Rsvd_0_f*/
};

static fields_t Epe_HdrProc_Oam_PI_field[] = {
    { 3,  8, 12, "piShareType"                           },  /* EpeHdrProcOamPI_piShareType_f*/
    {12,  8,  0, "piShareFields_3"                       },  /* EpeHdrProcOamPI_piShareFields_3_f*/
    {32,  9,  0, "piShareFields_2"                       },  /* EpeHdrProcOamPI_piShareFields_2_f*/
    {32, 10,  0, "piShareFields_1"                       },  /* EpeHdrProcOamPI_piShareFields_1_f*/
    {23, 11,  9, "piShareFields_0"                       },  /* EpeHdrProcOamPI_piShareFields_0_f*/
    { 1, 11,  8, "piEtherOamEdgePort"                    },  /* EpeHdrProcOamPI_piEtherOamEdgePort_f*/
    { 1, 11,  7, "piL2Match"                             },  /* EpeHdrProcOamPI_piL2Match_f*/
    { 1, 11,  6, "piExceptionEn"                         },  /* EpeHdrProcOamPI_piExceptionEn_f*/
    { 3, 11,  3, "piExceptionIndex"                      },  /* EpeHdrProcOamPI_piExceptionIndex_f*/
    { 1, 11,  2, "piEtherOamDiscard"                     },  /* EpeHdrProcOamPI_piEtherOamDiscard_f*/
    { 1, 11,  1, "piIsPortMac"                           },  /* EpeHdrProcOamPI_piIsPortMac_f*/
    { 1, 11,  0, "piLayer3Offset_1"                      },  /* EpeHdrProcOamPI_piLayer3Offset_1_f*/
    { 7, 12, 25, "piLayer3Offset_0"                      },  /* EpeHdrProcOamPI_piLayer3Offset_0_f*/
    { 8, 12, 17, "piLayer4Offset"                        },  /* EpeHdrProcOamPI_piLayer4Offset_f*/
    {14, 12,  3, "piLogicSrcPort"                        },  /* EpeHdrProcOamPI_piLogicSrcPort_f*/
    { 2, 12,  1, "piLinkLmType"                          },  /* EpeHdrProcOamPI_piLinkLmType_f*/
    { 1, 12,  0, "piLinkLmIndexBase_1"                   },  /* EpeHdrProcOamPI_piLinkLmIndexBase_1_f*/
    {32, 13,  0, "piChop1Rsvd_2"                         },  /* EpeHdrProcOamPI_piChop1Rsvd_2_f*/
    {32, 14,  0, "piChop1Rsvd_1"                         },  /* EpeHdrProcOamPI_piChop1Rsvd_1_f*/
    {32, 15,  0, "piChop1Rsvd_0"                         },  /* EpeHdrProcOamPI_piChop1Rsvd_0_f*/
    {13,  0,  2, "piLinkLmIndexBase_0"                   },  /* EpeHdrProcOamPI_piLinkLmIndexBase_0_f*/
    { 2,  0,  0, "piLinkLmCosType"                       },  /* EpeHdrProcOamPI_piLinkLmCosType_f*/
    { 3,  1, 29, "piLinkLmCos"                           },  /* EpeHdrProcOamPI_piLinkLmCos_f*/
    { 1,  1, 28, "piIsUp"                                },  /* EpeHdrProcOamPI_piIsUp_f*/
    { 1,  1, 27, "piGalExist"                            },  /* EpeHdrProcOamPI_piGalExist_f*/
    { 1,  1, 26, "piEntropyLabelExist"                   },  /* EpeHdrProcOamPI_piEntropyLabelExist_f*/
    { 1,  1, 25, "piLinkOrSectionOam"                    },  /* EpeHdrProcOamPI_piLinkOrSectionOam_f*/
    { 1,  1, 24, "piFromCpuLmUpDisable"                  },  /* EpeHdrProcOamPI_piFromCpuLmUpDisable_f*/
    { 1,  1, 23, "piFromCpuLmDownDisable"                },  /* EpeHdrProcOamPI_piFromCpuLmDownDisable_f*/
    { 4,  1, 19, "prLayer4UserType"                      },  /* EpeHdrProcOamPI_prLayer4UserType_f*/
    { 3,  1, 16, "prEtherOamLevel"                       },  /* EpeHdrProcOamPI_prEtherOamLevel_f*/
    { 8,  1,  8, "prEtherOamOpCode"                      },  /* EpeHdrProcOamPI_prEtherOamOpCode_f*/
    { 8,  1,  0, "prY1731OamOpCode"                      },  /* EpeHdrProcOamPI_prY1731OamOpCode_f*/
    {32,  2,  0, "prTxFcf"                               },  /* EpeHdrProcOamPI_prTxFcf_f*/
    {32,  3,  0, "prRxFcb"                               },  /* EpeHdrProcOamPI_prRxFcb_f*/
    {32,  4,  0, "prTxFcb"                               },  /* EpeHdrProcOamPI_prTxFcb_f*/
    {32,  5,  0, "piChop0Rsvd_2"                         },  /* EpeHdrProcOamPI_piChop0Rsvd_2_f*/
    {32,  6,  0, "piChop0Rsvd_1"                         },  /* EpeHdrProcOamPI_piChop0Rsvd_1_f*/
    {32,  7,  0, "piChop0Rsvd_0"                         },  /* EpeHdrProcOamPI_piChop0Rsvd_0_f*/
};

static fields_t Epe_Cla_Oam_PI_field[] = {
    { 8,  0,  4, "piSeq"                                 },  /* EpeClaOamPI_piSeq_f*/
    { 1,  0,  3, "piIsUdp"                               },  /* EpeClaOamPI_piIsUdp_f*/
    { 1,  0,  2, "piPacketHeaderEn"                      },  /* EpeClaOamPI_piPacketHeaderEn_f*/
    { 1,  0,  1, "piPacketHeaderEnEgress"                },  /* EpeClaOamPI_piPacketHeaderEnEgress_f*/
    { 1,  0,  0, "piChannelId_1"                         },  /* EpeClaOamPI_piChannelId_1_f*/
    { 5,  1, 27, "piChannelId_0"                         },  /* EpeClaOamPI_piChannelId_0_f*/
    {14,  1, 13, "piPacketLength"                        },  /* EpeClaOamPI_piPacketLength_f*/
    { 4,  1,  9, "piRxOamType"                           },  /* EpeClaOamPI_piRxOamType_f*/
    { 2,  1,  7, "piColor"                               },  /* EpeClaOamPI_piColor_f*/
    { 1,  1,  6, "piFromCpuOrOam"                        },  /* EpeClaOamPI_piFromCpuOrOam_f*/
    { 1,  1,  5, "piDiscard"                             },  /* EpeClaOamPI_piDiscard_f*/
    { 5,  1,  0, "piDiscardType_1"                       },  /* EpeClaOamPI_piDiscardType_1_f*/
    { 1,  2, 31, "piDiscardType_0"                       },  /* EpeClaOamPI_piDiscardType_0_f*/
    { 7,  2, 24, "piLocalPhyPort"                        },  /* EpeClaOamPI_piLocalPhyPort_f*/
    { 2,  2, 22, "piLmLookupType"                        },  /* EpeClaOamPI_piLmLookupType_f*/
    { 1,  2, 21, "piOamLookupEn"                         },  /* EpeClaOamPI_piOamLookupEn_f*/
    { 1,  2, 20, "piLmLookupEn0"                         },  /* EpeClaOamPI_piLmLookupEn0_f*/
    { 3,  2, 17, "piPacketCos0"                          },  /* EpeClaOamPI_piPacketCos0_f*/
    { 2,  2, 15, "piOamLookupNum"                        },  /* EpeClaOamPI_piOamLookupNum_f*/
    { 1,  2, 14, "piLmLookupEn1"                         },  /* EpeClaOamPI_piLmLookupEn1_f*/
    { 1,  2, 13, "piLmLookupEn2"                         },  /* EpeClaOamPI_piLmLookupEn2_f*/
    { 3,  2, 10, "piPacketCos1"                          },  /* EpeClaOamPI_piPacketCos1_f*/
    { 3,  2,  7, "piPacketCos2"                          },  /* EpeClaOamPI_piPacketCos2_f*/
    { 6,  2,  1, "piPriority"                            },  /* EpeClaOamPI_piPriority_f*/
    { 1,  2,  0, "piHardError"                           },  /* EpeClaOamPI_piHardError_f*/
    {14,  3, 18, "piGlobalDestPort"                      },  /* EpeClaOamPI_piGlobalDestPort_f*/
    { 1,  3, 17, "piFlowStats1Valid"                     },  /* EpeClaOamPI_piFlowStats1Valid_f*/
    {16,  3,  1, "piFlowStats1Ptr"                       },  /* EpeClaOamPI_piFlowStats1Ptr_f*/
    { 1,  3,  0, "piFlowStats0Valid"                     },  /* EpeClaOamPI_piFlowStats0Valid_f*/
    {16,  4, 16, "piFlowStats0Ptr"                       },  /* EpeClaOamPI_piFlowStats0Ptr_f*/
    { 1,  4, 15, "piAclLogEn0"                           },  /* EpeClaOamPI_piAclLogEn0_f*/
    { 2,  4, 13, "piAclLogId0"                           },  /* EpeClaOamPI_piAclLogId0_f*/
    { 1,  4, 12, "piAclLogEn1"                           },  /* EpeClaOamPI_piAclLogEn1_f*/
    { 2,  4, 10, "piAclLogId1"                           },  /* EpeClaOamPI_piAclLogId1_f*/
    { 1,  4,  9, "piAclLogEn2"                           },  /* EpeClaOamPI_piAclLogEn2_f*/
    { 2,  4,  7, "piAclLogId2"                           },  /* EpeClaOamPI_piAclLogId2_f*/
    { 1,  4,  6, "piAclLogEn3"                           },  /* EpeClaOamPI_piAclLogEn3_f*/
    { 2,  4,  4, "piAclLogId3"                           },  /* EpeClaOamPI_piAclLogId3_f*/
    { 4,  4,  0, "piFid_1"                               },  /* EpeClaOamPI_piFid_1_f*/
    {10,  5, 22, "piFid_0"                               },  /* EpeClaOamPI_piFid_0_f*/
    {13,  5,  9, "piDestVlanPtr"                         },  /* EpeClaOamPI_piDestVlanPtr_f*/
    { 1,  5,  8, "piIsTcp"                               },  /* EpeClaOamPI_piIsTcp_f*/
    { 4,  5,  4, "prLayer3Type"                          },  /* EpeClaOamPI_prLayer3Type_f*/
    { 4,  5,  0, "prLayer4Type"                          },  /* EpeClaOamPI_prLayer4Type_f*/
};

static fields_t key0LkupTrackFifo_field[] = {
    { 3,  0,  0, "key0LkupPtr_1"                         },  /* key0LkupTrackFifo_key0LkupPtr_1_f*/
    { 9,  1, 23, "key0LkupPtr_0"                         },  /* key0LkupTrackFifo_key0LkupPtr_0_f*/
    { 7,  1, 16, "localPhyPort"                          },  /* key0LkupTrackFifo_localPhyPort_f*/
    { 1,  1, 15, "keyTcamEn"                             },  /* key0LkupTrackFifo_keyTcamEn_f*/
    { 3,  1, 12, "keySrcIntfNum"                         },  /* key0LkupTrackFifo_keySrcIntfNum_f*/
    { 8,  1,  4, "keySeq"                                },  /* key0LkupTrackFifo_keySeq_f*/
    { 1,  1,  3, "keyLengthMode"                         },  /* key0LkupTrackFifo_keyLengthMode_f*/
    { 3,  1,  0, "keyType_1"                             },  /* key0LkupTrackFifo_keyType_1_f*/
    { 2,  2, 30, "keyType_0"                             },  /* key0LkupTrackFifo_keyType_0_f*/
    {30,  2,  0, "hashKey_4"                             },  /* key0LkupTrackFifo_hashKey_4_f*/
    {32,  3,  0, "hashKey_3"                             },  /* key0LkupTrackFifo_hashKey_3_f*/
    {32,  4,  0, "hashKey_2"                             },  /* key0LkupTrackFifo_hashKey_2_f*/
    {32,  5,  0, "hashKey_1"                             },  /* key0LkupTrackFifo_hashKey_1_f*/
    {32,  6,  0, "hashKey_0"                             },  /* key0LkupTrackFifo_hashKey_0_f*/
};

static fields_t FibHashAdReqFifo_field[] = {
    { 1,  0,  5, "Fib_Hash_Ad_Req"                       },  /* FibHashAdReqFifo_Fib_Hash_Ad_Req_f*/
    { 1,  0,  4, "freeValid"                             },  /* FibHashAdReqFifo_freeValid_f*/
    { 1,  0,  3, "resultValid"                           },  /* FibHashAdReqFifo_resultValid_f*/
    { 1,  0,  2, "error"                                 },  /* FibHashAdReqFifo_error_f*/
    { 2,  0,  0, "dsAdIndex_1"                           },  /* FibHashAdReqFifo_dsAdIndex_1_f*/
    {14,  1, 18, "dsAdIndex_0"                           },  /* FibHashAdReqFifo_dsAdIndex_0_f*/
    {18,  1,  0, "result"                                },  /* FibHashAdReqFifo_result_f*/
};

static fields_t FibKeyTrackFifo_field[] = {
    { 1,  0, 27, "Fib_Key_Track"                         },  /* FibKeyTrackFifo_Fib_Key_Track_f*/
    { 5,  0, 22, "keyType"                               },  /* FibKeyTrackFifo_keyType_f*/
    { 8,  0, 14, "Seq"                                   },  /* FibKeyTrackFifo_Seq_f*/
    {14,  0,  0, "vsiId"                                 },  /* FibKeyTrackFifo_vsiId_f*/
};

static fields_t Fib_LkupMgr_ReqFifo_field[] = {
    { 1,  0, 27, "Fib_LkupMgr_Req"                       },  /* FibLkupMgrReqFifo_Fib_LkupMgr_Req_f*/
    { 5,  0, 22, "keyType"                               },  /* FibLkupMgrReqFifo_keyType_f*/
    { 8,  0, 14, "seq"                                   },  /* FibLkupMgrReqFifo_seq_f*/
    {14,  0,  0, "key_8"                                 },  /* FibLkupMgrReqFifo_key_8_f*/
    {32,  1,  0, "key_7"                                 },  /* FibLkupMgrReqFifo_key_7_f*/
    {32,  2,  0, "key_6"                                 },  /* FibLkupMgrReqFifo_key_6_f*/
    {32,  3,  0, "key_5"                                 },  /* FibLkupMgrReqFifo_key_5_f*/
    {32,  4,  0, "key_4"                                 },  /* FibLkupMgrReqFifo_key_4_f*/
    {32,  5,  0, "key_3"                                 },  /* FibLkupMgrReqFifo_key_3_f*/
    {32,  6,  0, "key_2"                                 },  /* FibLkupMgrReqFifo_key_2_f*/
    {32,  7,  0, "key_1"                                 },  /* FibLkupMgrReqFifo_key_1_f*/
    {32,  8,  0, "key_0"                                 },  /* FibLkupMgrReqFifo_key_0_f*/
};

static fields_t LkupMgr_LpmKey_ReqFifo_field[] = {
    { 1,  0, 10, "LkupMgr_Lpm_Key_Req"                   },  /* LkupMgrLpmKeyReqFifo_LkupMgr_Lpm_Key_Req_f*/
    { 1,  0,  9, "ipv6NatDaLookupEn"                     },  /* LkupMgrLpmKeyReqFifo_ipv6NatDaLookupEn_f*/
    { 1,  0,  8, "ipv4NatDaLookupEn"                     },  /* LkupMgrLpmKeyReqFifo_ipv4NatDaLookupEn_f*/
    { 1,  0,  7, "lpmIpv4Pipeline3En"                    },  /* LkupMgrLpmKeyReqFifo_lpmIpv4Pipeline3En_f*/
    { 4,  0,  3, "lpmHashMode"                           },  /* LkupMgrLpmKeyReqFifo_lpmHashMode_f*/
    { 1,  0,  2, "camHit"                                },  /* LkupMgrLpmKeyReqFifo_camHit_f*/
    { 2,  0,  0, "camPrefixLen_1"                        },  /* LkupMgrLpmKeyReqFifo_camPrefixLen_1_f*/
    { 4,  1, 28, "camPrefixLen_0"                        },  /* LkupMgrLpmKeyReqFifo_camPrefixLen_0_f*/
    { 8,  1, 20, "camIndexMask"                          },  /* LkupMgrLpmKeyReqFifo_camIndexMask_f*/
    {16,  1,  4, "camNexthop"                            },  /* LkupMgrLpmKeyReqFifo_camNexthop_f*/
    { 4,  1,  0, "camPtr_1"                              },  /* LkupMgrLpmKeyReqFifo_camPtr_1_f*/
    {14,  2, 18, "camPtr_0"                              },  /* LkupMgrLpmKeyReqFifo_camPtr_0_f*/
    { 4,  2, 14, "keyType"                               },  /* LkupMgrLpmKeyReqFifo_keyType_f*/
    {14,  2,  0, "key_8"                                 },  /* LkupMgrLpmKeyReqFifo_key_8_f*/
    {32,  3,  0, "key_7"                                 },  /* LkupMgrLpmKeyReqFifo_key_7_f*/
    {32,  4,  0, "key_6"                                 },  /* LkupMgrLpmKeyReqFifo_key_6_f*/
    {32,  5,  0, "key_5"                                 },  /* LkupMgrLpmKeyReqFifo_key_5_f*/
    {32,  6,  0, "key_4"                                 },  /* LkupMgrLpmKeyReqFifo_key_4_f*/
    {32,  7,  0, "key_3"                                 },  /* LkupMgrLpmKeyReqFifo_key_3_f*/
    {32,  8,  0, "key_2"                                 },  /* LkupMgrLpmKeyReqFifo_key_2_f*/
    {32,  9,  0, "key_1"                                 },  /* LkupMgrLpmKeyReqFifo_key_1_f*/
    {32, 10,  0, "key_0"                                 },  /* LkupMgrLpmKeyReqFifo_key_0_f*/
};

static fields_t LpmAdReqFifo_field[] = {
    { 1,  0,  5, "Lpm_Ad_Req"                            },  /* LpmAdReqFifo_Lpm_Ad_Req_f*/
    { 1,  0,  4, "agingEn"                               },  /* LpmAdReqFifo_agingEn_f*/
    { 1,  0,  3, "resultValid"                           },  /* LpmAdReqFifo_resultValid_f*/
    { 1,  0,  2, "error"                                 },  /* LpmAdReqFifo_error_f*/
    { 2,  0,  0, "nexthop_1"                             },  /* LpmAdReqFifo_nexthop_1_f*/
    {14,  1, 18, "nexthop_0"                             },  /* LpmAdReqFifo_nexthop_0_f*/
    {18,  1,  0, "result"                                },  /* LpmAdReqFifo_result_f*/
};

static fields_t LpmFinalTrackFifo_field[] = {
    { 1,  0,  3, "Lpm_Final_Track"                       },  /* LpmFinalTrackFifo_Lpm_Final_Track_f*/
    { 1,  0,  2, "err"                                   },  /* LpmFinalTrackFifo_err_f*/
    { 1,  0,  1, "hash2ndReqVld"                         },  /* LpmFinalTrackFifo_hash2ndReqVld_f*/
    { 1,  0,  0, "agingEn"                               },  /* LpmFinalTrackFifo_agingEn_f*/
    {16,  1, 16, "result"                                },  /* LpmFinalTrackFifo_result_f*/
    {16,  1,  0, "nexthop"                               },  /* LpmFinalTrackFifo_nexthop_f*/
};

static fields_t LpmHashKeyTrackFifo_field[] = {
    { 1,  0, 17, "Lpm_Hash_Key_Track"                    },  /* LpmHashKeyTrackFifo_Lpm_Hash_Key_Track_f*/
    { 1,  0, 16, "cpuReq"                                },  /* LpmHashKeyTrackFifo_cpuReq_f*/
    { 4,  0, 12, "hashType"                              },  /* LpmHashKeyTrackFifo_hashType_f*/
    {12,  0,  0, "bucket_1"                              },  /* LpmHashKeyTrackFifo_bucket_1_f*/
    { 2,  1, 30, "bucket_0"                              },  /* LpmHashKeyTrackFifo_bucket_0_f*/
    {30,  1,  0, "key_4"                                 },  /* LpmHashKeyTrackFifo_key_4_f*/
    {32,  2,  0, "key_3"                                 },  /* LpmHashKeyTrackFifo_key_3_f*/
    {32,  3,  0, "key_2"                                 },  /* LpmHashKeyTrackFifo_key_2_f*/
    {32,  4,  0, "key_1"                                 },  /* LpmHashKeyTrackFifo_key_1_f*/
    {32,  5,  0, "key_0"                                 },  /* LpmHashKeyTrackFifo_key_0_f*/
};

static fields_t LpmHashResultFifo_field[] = {
    { 1,  0, 26, "Lpm_Hash_Result"                       },  /* LpmHashResultFifo_Lpm_Hash_Result_f*/
    { 1,  0, 25, "error"                                 },  /* LpmHashResultFifo_error_f*/
    { 8,  0, 17, "indexMask"                             },  /* LpmHashResultFifo_indexMask_f*/
    {15,  0,  2, "result"                                },  /* LpmHashResultFifo_result_f*/
    { 2,  0,  0, "pointer_1"                             },  /* LpmHashResultFifo_pointer_1_f*/
    {16,  1, 16, "pointer_0"                             },  /* LpmHashResultFifo_pointer_0_f*/
    {16,  1,  0, "nexthop"                               },  /* LpmHashResultFifo_nexthop_f*/
};

static fields_t LpmPipe0ReqFifo_field[] = {
    { 1,  0, 28, "Lpm_Pipe0_Req"                         },  /* LpmPipe0ReqFifo_Lpm_Pipe0_Req_f*/
    { 1,  0, 27, "lpmLuValid"                            },  /* LpmPipe0ReqFifo_lpmLuValid_f*/
    { 1,  0, 26, "hashErr"                               },  /* LpmPipe0ReqFifo_hashErr_f*/
    { 8,  0, 18, "indexMask"                             },  /* LpmPipe0ReqFifo_indexMask_f*/
    {18,  0,  0, "ptr"                                   },  /* LpmPipe0ReqFifo_ptr_f*/
    {32,  1,  0, "ip"                                    },  /* LpmPipe0ReqFifo_ip_f*/
};

static fields_t LpmPipe1ReqFifo_field[] = {
    { 1,  0,  4, "Lpm_Pipe1_Req"                         },  /* LpmPipe1ReqFifo_Lpm_Pipe1_Req_f*/
    { 1,  0,  3, "lpmLuValid"                            },  /* LpmPipe1ReqFifo_lpmLuValid_f*/
    { 1,  0,  2, "err"                                   },  /* LpmPipe1ReqFifo_err_f*/
    { 2,  0,  0, "indexMask_1"                           },  /* LpmPipe1ReqFifo_indexMask_1_f*/
    { 6,  1, 26, "indexMask_0"                           },  /* LpmPipe1ReqFifo_indexMask_0_f*/
    {18,  1,  8, "ptr"                                   },  /* LpmPipe1ReqFifo_ptr_f*/
    { 8,  1,  0, "nexthop_1"                             },  /* LpmPipe1ReqFifo_nexthop_1_f*/
    { 8,  2, 24, "nexthop_0"                             },  /* LpmPipe1ReqFifo_nexthop_0_f*/
    {24,  2,  0, "ip"                                    },  /* LpmPipe1ReqFifo_ip_f*/
};

static fields_t LpmPipe2ReqFifo_field[] = {
    { 1,  0, 28, "Lpm_Pipe2_Req"                         },  /* LpmPipe2ReqFifo_Lpm_Pipe2_Req_f*/
    { 1,  0, 27, "lpmLuValid"                            },  /* LpmPipe2ReqFifo_lpmLuValid_f*/
    { 1,  0, 26, "err"                                   },  /* LpmPipe2ReqFifo_err_f*/
    { 8,  0, 18, "indexMask"                             },  /* LpmPipe2ReqFifo_indexMask_f*/
    {18,  0,  0, "ptr"                                   },  /* LpmPipe2ReqFifo_ptr_f*/
    {16,  1, 16, "nexthop"                               },  /* LpmPipe2ReqFifo_nexthop_f*/
    {16,  1,  0, "ip"                                    },  /* LpmPipe2ReqFifo_ip_f*/
};

static fields_t LpmPipe3ReqFifo_field[] = {
    { 1,  0, 20, "Lpm_Pipe3_Req"                         },  /* LpmPipe3ReqFifo_Lpm_Pipe3_Req_f*/
    { 1,  0, 19, "lpmLuValid"                            },  /* LpmPipe3ReqFifo_lpmLuValid_f*/
    { 1,  0, 18, "err"                                   },  /* LpmPipe3ReqFifo_err_f*/
    { 8,  0, 10, "indexMask"                             },  /* LpmPipe3ReqFifo_indexMask_f*/
    {10,  0,  0, "ptr_1"                                 },  /* LpmPipe3ReqFifo_ptr_1_f*/
    { 8,  1, 24, "ptr_0"                                 },  /* LpmPipe3ReqFifo_ptr_0_f*/
    {16,  1,  8, "nexthop"                               },  /* LpmPipe3ReqFifo_nexthop_f*/
    { 8,  1,  0, "ip"                                    },  /* LpmPipe3ReqFifo_ip_f*/
};

static fields_t LpmTcamKeyReqFifo_field[] = {
    { 1,  0,  1, "Lpm_Tcam_Key_Req"                      },  /* LpmTcamKeyReqFifo_Lpm_Tcam_Key_Req_f*/
    { 1,  0,  0, "keySize"                               },  /* LpmTcamKeyReqFifo_keySize_f*/
    {32,  1,  0, "key_4"                                 },  /* LpmTcamKeyReqFifo_key_4_f*/
    {32,  2,  0, "key_3"                                 },  /* LpmTcamKeyReqFifo_key_3_f*/
    {32,  3,  0, "key_2"                                 },  /* LpmTcamKeyReqFifo_key_2_f*/
    {32,  4,  0, "key_1"                                 },  /* LpmTcamKeyReqFifo_key_1_f*/
    {32,  5,  0, "key_0"                                 },  /* LpmTcamKeyReqFifo_key_0_f*/
};

static fields_t LpmTcamResultFifo_field[] = {
    { 1,  0, 29, "Lpm_Tcam_Result"                       },  /* LpmTcamResultFifo_Lpm_Tcam_Result_f*/
    { 1,  0, 28, "hit"                                   },  /* LpmTcamResultFifo_hit_f*/
    { 8,  0, 20, "index"                                 },  /* LpmTcamResultFifo_index_f*/
    { 1,  0, 19, "agingEn"                               },  /* LpmTcamResultFifo_agingEn_f*/
    { 8,  0, 11, "indexMask"                             },  /* LpmTcamResultFifo_indexMask_f*/
    { 9,  0,  2, "prefixLen"                             },  /* LpmTcamResultFifo_prefixLen_f*/
    { 2,  0,  0, "nexthop_1"                             },  /* LpmTcamResultFifo_nexthop_1_f*/
    {14,  1, 18, "nexthop_0"                             },  /* LpmTcamResultFifo_nexthop_0_f*/
    {18,  1,  0, "pointer"                               },  /* LpmTcamResultFifo_pointer_f*/
};

static fields_t LpmTrackFifo_field[] = {
    { 1,  0,  4, "Lpm_Track"                             },  /* LpmTrackFifo_Lpm_Track_f*/
    { 1,  0,  3, "lpmLuValid"                            },  /* LpmTrackFifo_lpmLuValid_f*/
    { 1,  0,  2, "agingEn"                               },  /* LpmTrackFifo_agingEn_f*/
    { 2,  0,  0, "ipv6Mid_1"                             },  /* LpmTrackFifo_ipv6Mid_1_f*/
    {14,  1, 18, "ipv6Mid_0"                             },  /* LpmTrackFifo_ipv6Mid_0_f*/
    {16,  1,  2, "result"                                },  /* LpmTrackFifo_result_f*/
    { 2,  1,  0, "nexthop_1"                             },  /* LpmTrackFifo_nexthop_1_f*/
    {14,  2, 18, "nexthop_0"                             },  /* LpmTrackFifo_nexthop_0_f*/
    {18,  2,  0, "ptr"                                   },  /* LpmTrackFifo_ptr_f*/
};

static fields_t Ipe_Learn_Fwd_PI1_field[] = {
    { 8,  0,  0, "piLogicSrcPort_1"                      },  /* IpeLearnFwdPI1_piLogicSrcPort_1_f*/
    { 6,  1, 26, "piLogicSrcPort_0"                      },  /* IpeLearnFwdPI1_piLogicSrcPort_0_f*/
    { 3,  1, 23, "piPbbSrcPortType"                      },  /* IpeLearnFwdPI1_piPbbSrcPortType_f*/
    { 3,  1, 20, "piPayloadPacketType"                   },  /* IpeLearnFwdPI1_piPayloadPacketType_f*/
    {12,  1,  8, "piSvlanId"                             },  /* IpeLearnFwdPI1_piSvlanId_f*/
    { 8,  1,  0, "piCvlanId_1"                           },  /* IpeLearnFwdPI1_piCvlanId_1_f*/
    { 4,  2, 28, "piCvlanId_0"                           },  /* IpeLearnFwdPI1_piCvlanId_0_f*/
    {14,  2, 14, "piFid"                                 },  /* IpeLearnFwdPI1_piFid_f*/
    { 1,  2, 13, "piMacKnown"                            },  /* IpeLearnFwdPI1_piMacKnown_f*/
    { 8,  2,  5, "piHeaderHash"                          },  /* IpeLearnFwdPI1_piHeaderHash_f*/
    { 1,  2,  4, "piPtpTransparentClockEn"               },  /* IpeLearnFwdPI1_piPtpTransparentClockEn_f*/
    { 4,  2,  0, "piQcnPortId"                           },  /* IpeLearnFwdPI1_piQcnPortId_f*/
};

static fields_t Ipe_Learn_Fwd_PI0_field[] = {
    { 8,  0, 17, "learn2FwdSeq"                          },  /* IpeLearnFwdPI0_learn2FwdSeq_f*/
    { 1,  0, 16, "piMuxDestinationValid"                 },  /* IpeLearnFwdPI0_piMuxDestinationValid_f*/
    { 1,  0, 15, "piFatalExceptionValid"                 },  /* IpeLearnFwdPI0_piFatalExceptionValid_f*/
    { 4,  0, 11, "piFatalException"                      },  /* IpeLearnFwdPI0_piFatalException_f*/
    { 1,  0, 10, "piDsFwdPtrValid"                       },  /* IpeLearnFwdPI0_piDsFwdPtrValid_f*/
    { 1,  0,  9, "piDsFwdPtrIsMaximum"                   },  /* IpeLearnFwdPI0_piDsFwdPtrIsMaximum_f*/
    { 9,  0,  0, "piGlobalSrcPort_1"                     },  /* IpeLearnFwdPI0_piGlobalSrcPort_1_f*/
    { 5,  1, 27, "piGlobalSrcPort_0"                     },  /* IpeLearnFwdPI0_piGlobalSrcPort_0_f*/
    { 1,  1, 26, "piApsSelectProtectingPath0"            },  /* IpeLearnFwdPI0_piApsSelectProtectingPath0_f*/
    { 1,  1, 25, "piApsSelectValid0"                     },  /* IpeLearnFwdPI0_piApsSelectValid0_f*/
    {10,  1, 15, "piApsSelectGroupId0"                   },  /* IpeLearnFwdPI0_piApsSelectGroupId0_f*/
    { 1,  1, 14, "piApsSelectValid1"                     },  /* IpeLearnFwdPI0_piApsSelectValid1_f*/
    {10,  1,  4, "piApsSelectGroupId1"                   },  /* IpeLearnFwdPI0_piApsSelectGroupId1_f*/
    { 1,  1,  3, "piApsSelectProtectingPath1"            },  /* IpeLearnFwdPI0_piApsSelectProtectingPath1_f*/
    { 1,  1,  2, "piApsSelectValid2"                     },  /* IpeLearnFwdPI0_piApsSelectValid2_f*/
    { 2,  1,  0, "piApsSelectGroupId2_1"                 },  /* IpeLearnFwdPI0_piApsSelectGroupId2_1_f*/
    { 8,  2, 24, "piApsSelectGroupId2_0"                 },  /* IpeLearnFwdPI0_piApsSelectGroupId2_0_f*/
    { 1,  2, 23, "piApsSelectProtectingPath2"            },  /* IpeLearnFwdPI0_piApsSelectProtectingPath2_f*/
    { 1,  2, 22, "piAdLengthAdjustType"                  },  /* IpeLearnFwdPI0_piAdLengthAdjustType_f*/
    { 1,  2, 21, "piAdCriticalPacket"                    },  /* IpeLearnFwdPI0_piAdCriticalPacket_f*/
    { 1,  2, 20, "piAdNextHopExt"                        },  /* IpeLearnFwdPI0_piAdNextHopExt_f*/
    { 1,  2, 19, "piAdSendLocalPhyPort"                  },  /* IpeLearnFwdPI0_piAdSendLocalPhyPort_f*/
    { 2,  2, 17, "piAdApsType"                           },  /* IpeLearnFwdPI0_piAdApsType_f*/
    { 2,  2, 15, "piAdSpeed"                             },  /* IpeLearnFwdPI0_piAdSpeed_f*/
    {15,  2,  0, "piAdDestMap_1"                         },  /* IpeLearnFwdPI0_piAdDestMap_1_f*/
    { 7,  3, 25, "piAdDestMap_0"                         },  /* IpeLearnFwdPI0_piAdDestMap_0_f*/
    {16,  3,  9, "piAdNextHopPtr"                        },  /* IpeLearnFwdPI0_piAdNextHopPtr_f*/
    { 1,  3,  8, "piNextHopPtrValid"                     },  /* IpeLearnFwdPI0_piNextHopPtrValid_f*/
    { 1,  3,  7, "piStormCtlExceptionEn"                 },  /* IpeLearnFwdPI0_piStormCtlExceptionEn_f*/
    { 6,  3,  1, "piSourcePortIsolateId"                 },  /* IpeLearnFwdPI0_piSourcePortIsolateId_f*/
    { 1,  3,  0, "piIngressHdrValid"                     },  /* IpeLearnFwdPI0_piIngressHdrValid_f*/
};

static fields_t Ipe_Route_Fwd_PI_field[] = {
    { 1,  4,  5, "piNonCrc"                              },  /* IpeRouteFwdPI_piNonCrc_f*/
    { 5,  4,  0, "piSrcDscp_1"                           },  /* IpeRouteFwdPI_piSrcDscp_1_f*/
    { 1,  5, 31, "piSrcDscp_0"                           },  /* IpeRouteFwdPI_piSrcDscp_0_f*/
    { 3,  5, 28, "piPacketType"                          },  /* IpeRouteFwdPI_piPacketType_f*/
    { 1,  5, 27, "piWlanTunnelValid"                   },  /* IpeRouteFwdPI_piWlanTunnelValid_f*/
    { 1,  5, 26, "piLogicPortType"                       },  /* IpeRouteFwdPI_piLogicPortType_f*/
    { 2,  5, 24, "piRoamingState"                        },  /* IpeRouteFwdPI_piRoamingState_f*/
    { 1,  5, 23, "piWlanTunnelType"                    },  /* IpeRouteFwdPI_piWlanTunnelType_f*/
    { 1,  5, 22, "piOuterVlanIsCvlan"                    },  /* IpeRouteFwdPI_piOuterVlanIsCvlan_f*/
    { 2,  5, 20, "piSvlanTpidIndex"                      },  /* IpeRouteFwdPI_piSvlanTpidIndex_f*/
    { 1,  5, 19, "piL2SpanEn"                            },  /* IpeRouteFwdPI_piL2SpanEn_f*/
    { 2,  5, 17, "piL2SpanId"                            },  /* IpeRouteFwdPI_piL2SpanId_f*/
    { 1,  5, 16, "piPortLogEn"                           },  /* IpeRouteFwdPI_piPortLogEn_f*/
    { 1,  5, 15, "piOamTunnelEn"                         },  /* IpeRouteFwdPI_piOamTunnelEn_f*/
    { 1,  5, 14, "piSrcQueueSelect"                      },  /* IpeRouteFwdPI_piSrcQueueSelect_f*/
    { 2,  5, 12, "piStagAction"                          },  /* IpeRouteFwdPI_piStagAction_f*/
    { 2,  5, 10, "piCtagAction"                          },  /* IpeRouteFwdPI_piCtagAction_f*/
    { 1,  5,  9, "piIsLeaf"                              },  /* IpeRouteFwdPI_piIsLeaf_f*/
    { 2,  5,  7, "piSpeed"                               },  /* IpeRouteFwdPI_piSpeed_f*/
    { 1,  5,  6, "piIsEsadi"                             },  /* IpeRouteFwdPI_piIsEsadi_f*/
    { 3,  5,  3, "piCtagCos"                             },  /* IpeRouteFwdPI_piCtagCos_f*/
    { 1,  5,  2, "piCtagCfi"                             },  /* IpeRouteFwdPI_piCtagCfi_f*/
    { 1,  5,  1, "piSvlanIdValid"                        },  /* IpeRouteFwdPI_piSvlanIdValid_f*/
    { 1,  5,  0, "piCvlanIdValid"                        },  /* IpeRouteFwdPI_piCvlanIdValid_f*/
    { 1,  6, 31, "piSrcCtagOffsetType"                   },  /* IpeRouteFwdPI_piSrcCtagOffsetType_f*/
    { 1,  6, 30, "piL3SpanEn"                            },  /* IpeRouteFwdPI_piL3SpanEn_f*/
    { 2,  6, 28, "piL3SpanId"                            },  /* IpeRouteFwdPI_piL3SpanId_f*/
    {24,  6,  4, "piNewIsid"                             },  /* IpeRouteFwdPI_piNewIsid_f*/
    { 1,  6,  3, "piPbbCheckDiscard"                     },  /* IpeRouteFwdPI_piPbbCheckDiscard_f*/
    { 3,  6,  0, "piEcmpHash_1"                          },  /* IpeRouteFwdPI_piEcmpHash_1_f*/
    {32,  7,  0, "piChop1Rsvd_0"                         },  /* IpeRouteFwdPI_piChop1Rsvd_0_f*/
    { 5,  0,  2, "piEcmpHash_0"                          },  /* IpeRouteFwdPI_piEcmpHash_0_f*/
    { 1,  0,  1, "piSourcePortExtender"                  },  /* IpeRouteFwdPI_piSourcePortExtender_f*/
    { 1,  0,  0, "piVlanPtr_1"                           },  /* IpeRouteFwdPI_piVlanPtr_1_f*/
    {12,  1, 20, "piVlanPtr_0"                           },  /* IpeRouteFwdPI_piVlanPtr_0_f*/
    { 4,  1, 16, "piFlowId"                              },  /* IpeRouteFwdPI_piFlowId_f*/
    { 6,  1, 10, "piAclDscp"                             },  /* IpeRouteFwdPI_piAclDscp_f*/
    { 1,  1,  9, "piAclLogEn0"                           },  /* IpeRouteFwdPI_piAclLogEn0_f*/
    { 2,  1,  7, "piAclLogId0"                           },  /* IpeRouteFwdPI_piAclLogId0_f*/
    { 1,  1,  6, "piAclLogEn1"                           },  /* IpeRouteFwdPI_piAclLogEn1_f*/
    { 2,  1,  4, "piAclLogId1"                           },  /* IpeRouteFwdPI_piAclLogId1_f*/
    { 1,  1,  3, "piAclLogEn2"                           },  /* IpeRouteFwdPI_piAclLogEn2_f*/
    { 2,  1,  1, "piAclLogId2"                           },  /* IpeRouteFwdPI_piAclLogId2_f*/
    { 1,  1,  0, "piAclLogEn3"                           },  /* IpeRouteFwdPI_piAclLogEn3_f*/
    { 2,  2, 30, "piAclLogId3"                           },  /* IpeRouteFwdPI_piAclLogId3_f*/
    { 1,  2, 29, "piEgressOamUseFid"                     },  /* IpeRouteFwdPI_piEgressOamUseFid_f*/
    { 1,  2, 28, "piEcnEn"                               },  /* IpeRouteFwdPI_piEcnEn_f*/
    { 1,  2, 27, "piEcnPriorityEn"                       },  /* IpeRouteFwdPI_piEcnPriorityEn_f*/
    { 1,  2, 26, "piEcnAware"                            },  /* IpeRouteFwdPI_piEcnAware_f*/
    { 1,  2, 25, "piSourceCfi"                           },  /* IpeRouteFwdPI_piSourceCfi_f*/
    { 8,  2, 17, "piPacketTtl"                           },  /* IpeRouteFwdPI_piPacketTtl_f*/
    { 1,  2, 16, "prCnTagValid"                          },  /* IpeRouteFwdPI_prCnTagValid_f*/
    {16,  2,  0, "prCnFlowId"                            },  /* IpeRouteFwdPI_prCnFlowId_f*/
    {32,  3,  0, "piChop0Rsvd_0"                         },  /* IpeRouteFwdPI_piChop0Rsvd_0_f*/
};

static fields_t Ipe_HdrAdj_IntfMap_PI_field[] = {
    { 8,  0, 14, "piSeq"                                 },  /* IpeHdrAdjIntfMapPI_piSeq_f*/
    { 1,  0, 13, "piNonCrc"                              },  /* IpeHdrAdjIntfMapPI_piNonCrc_f*/
    { 2,  0, 11, "piMuxLengthType"                       },  /* IpeHdrAdjIntfMapPI_piMuxLengthType_f*/
    { 7,  0,  4, "piLocalPhyPort"                        },  /* IpeHdrAdjIntfMapPI_piLocalPhyPort_f*/
    { 3,  0,  1, "piPacketType"                          },  /* IpeHdrAdjIntfMapPI_piPacketType_f*/
    { 1,  0,  0, "piOuterPriority_1"                     },  /* IpeHdrAdjIntfMapPI_piOuterPriority_1_f*/
    { 5,  1, 27, "piOuterPriority_0"                     },  /* IpeHdrAdjIntfMapPI_piOuterPriority_0_f*/
    { 2,  1, 25, "piOuterColor"                          },  /* IpeHdrAdjIntfMapPI_piOuterColor_f*/
    { 1,  1, 24, "piFromCpuOrOam"                        },  /* IpeHdrAdjIntfMapPI_piFromCpuOrOam_f*/
    { 1,  1, 23, "piFromSgmac"                           },  /* IpeHdrAdjIntfMapPI_piFromSgmac_f*/
    { 8,  1, 15, "piOuterTtl"                            },  /* IpeHdrAdjIntfMapPI_piOuterTtl_f*/
    { 1,  1, 14, "piCustomerIdValid"                     },  /* IpeHdrAdjIntfMapPI_piCustomerIdValid_f*/
    {14,  1,  0, "piCustomerId_1"                        },  /* IpeHdrAdjIntfMapPI_piCustomerId_1_f*/
    {18,  2, 14, "piCustomerId_0"                        },  /* IpeHdrAdjIntfMapPI_piCustomerId_0_f*/
    { 6,  2,  8, "piSourcePortIsolateId"                 },  /* IpeHdrAdjIntfMapPI_piSourcePortIsolateId_f*/
    { 1,  2,  7, "piIsLoop"                              },  /* IpeHdrAdjIntfMapPI_piIsLoop_f*/
    { 1,  2,  6, "piUserVlanPtrValid"                    },  /* IpeHdrAdjIntfMapPI_piUserVlanPtrValid_f*/
    { 6,  2,  0, "piUserVlanPtr_1"                       },  /* IpeHdrAdjIntfMapPI_piUserVlanPtr_1_f*/
    { 7,  3, 25, "piUserVlanPtr_0"                       },  /* IpeHdrAdjIntfMapPI_piUserVlanPtr_0_f*/
    {14,  3, 11, "piLogicSrcPort"                        },  /* IpeHdrAdjIntfMapPI_piLogicSrcPort_f*/
    { 1,  3, 10, "piLogicSrcPortValid"                   },  /* IpeHdrAdjIntfMapPI_piLogicSrcPortValid_f*/
    { 1,  3,  9, "piSrcDscpValid"                        },  /* IpeHdrAdjIntfMapPI_piSrcDscpValid_f*/
    { 6,  3,  3, "piSrcDscp"                             },  /* IpeHdrAdjIntfMapPI_piSrcDscp_f*/
    { 1,  3,  2, "piOamUseFid"                           },  /* IpeHdrAdjIntfMapPI_piOamUseFid_f*/
    { 1,  3,  1, "piWlanTunnelValid"                   },  /* IpeHdrAdjIntfMapPI_piWlanTunnelValid_f*/
    { 1,  3,  0, "piWlanTunnelType"                    },  /* IpeHdrAdjIntfMapPI_piWlanTunnelType_f*/
    { 2,  4, 30, "piRoamingState"                        },  /* IpeHdrAdjIntfMapPI_piRoamingState_f*/
    { 1,  4, 29, "piLogicPortType"                       },  /* IpeHdrAdjIntfMapPI_piLogicPortType_f*/
    { 1,  4, 28, "piUserDefaultVlanTagValid"             },  /* IpeHdrAdjIntfMapPI_piUserDefaultVlanTagValid_f*/
    {12,  4, 16, "piUserDefaultVlanId"                   },  /* IpeHdrAdjIntfMapPI_piUserDefaultVlanId_f*/
    { 3,  4, 13, "piUserDefaultCos"                      },  /* IpeHdrAdjIntfMapPI_piUserDefaultCos_f*/
    { 1,  4, 12, "piUserDefaultCfi"                      },  /* IpeHdrAdjIntfMapPI_piUserDefaultCfi_f*/
    { 4,  4,  8, "piRxOamType"                           },  /* IpeHdrAdjIntfMapPI_piRxOamType_f*/
    { 1,  4,  7, "piDiscard"                             },  /* IpeHdrAdjIntfMapPI_piDiscard_f*/
    { 6,  4,  1, "piDiscardType"                         },  /* IpeHdrAdjIntfMapPI_piDiscardType_f*/
    { 1,  4,  0, "piPacketLength_1"                      },  /* IpeHdrAdjIntfMapPI_piPacketLength_1_f*/
    {13,  5, 19, "piPacketLength_0"                      },  /* IpeHdrAdjIntfMapPI_piPacketLength_0_f*/
    { 1,  5, 18, "piBypassAll"                           },  /* IpeHdrAdjIntfMapPI_piBypassAll_f*/
    { 8,  5, 10, "piPayloadOffset"                       },  /* IpeHdrAdjIntfMapPI_piPayloadOffset_f*/
    { 8,  5,  2, "piPacketLengthAdjust"                  },  /* IpeHdrAdjIntfMapPI_piPacketLengthAdjust_f*/
    { 2,  5,  0, "piMuxDestination_1"                    },  /* IpeHdrAdjIntfMapPI_piMuxDestination_1_f*/
    {14,  6, 18, "piMuxDestination_0"                    },  /* IpeHdrAdjIntfMapPI_piMuxDestination_0_f*/
    { 1,  6, 17, "piMuxDestinationType"                  },  /* IpeHdrAdjIntfMapPI_piMuxDestinationType_f*/
    { 1,  6, 16, "piMuxDestinationValid"                 },  /* IpeHdrAdjIntfMapPI_piMuxDestinationValid_f*/
    { 1,  6, 15, "piHardError"                           },  /* IpeHdrAdjIntfMapPI_piHardError_f*/
    { 1,  6, 14, "piOuterVlanIsCvlan"                    },  /* IpeHdrAdjIntfMapPI_piOuterVlanIsCvlan_f*/
    { 3,  6, 11, "piPbbSrcPortType"                      },  /* IpeHdrAdjIntfMapPI_piPbbSrcPortType_f*/
    { 2,  6,  9, "piSvlanTpidIndex"                      },  /* IpeHdrAdjIntfMapPI_piSvlanTpidIndex_f*/
    { 1,  6,  8, "piTimeStampValid"                      },  /* IpeHdrAdjIntfMapPI_piTimeStampValid_f*/
    { 8,  6,  0, "piTimeStamp_2"                         },  /* IpeHdrAdjIntfMapPI_piTimeStamp_2_f*/
    {32,  7,  0, "piTimeStamp_1"                         },  /* IpeHdrAdjIntfMapPI_piTimeStamp_1_f*/
    {22,  8, 10, "piTimeStamp_0"                         },  /* IpeHdrAdjIntfMapPI_piTimeStamp_0_f*/
    { 8,  8,  2, "piChannelId"                           },  /* IpeHdrAdjIntfMapPI_piChannelId_f*/
    { 1,  8,  1, "piMplsLabelSpaceValid"                 },  /* IpeHdrAdjIntfMapPI_piMplsLabelSpaceValid_f*/
    { 1,  8,  0, "piFid_1"                               },  /* IpeHdrAdjIntfMapPI_piFid_1_f*/
    {13,  9, 19, "piFid_0"                               },  /* IpeHdrAdjIntfMapPI_piFid_0_f*/
    { 1,  9, 18, "piSourcePortExtender"                  },  /* IpeHdrAdjIntfMapPI_piSourcePortExtender_f*/
    { 1,  9, 17, "piTxDmEn"                              },  /* IpeHdrAdjIntfMapPI_piTxDmEn_f*/
    { 1,  9, 16, "piPortMacType"                         },  /* IpeHdrAdjIntfMapPI_piPortMacType_f*/
    { 8,  9,  8, "piPortMacLabel"                        },  /* IpeHdrAdjIntfMapPI_piPortMacLabel_f*/
    { 1,  9,  7, "piVlanRangeType"                       },  /* IpeHdrAdjIntfMapPI_piVlanRangeType_f*/
    { 6,  9,  1, "piVlanRangeProfile"                    },  /* IpeHdrAdjIntfMapPI_piVlanRangeProfile_f*/
    { 1,  9,  0, "piVlanRangeProfileEn"                  },  /* IpeHdrAdjIntfMapPI_piVlanRangeProfileEn_f*/
};

static fields_t Ipe_Parser_IntfMap_PR_field[] = {
    { 1,  0, 29, "prError"                               },  /* IpeParserIntfMapPR_prError_f*/
    { 8,  0, 21, "prSeq"                                 },  /* IpeParserIntfMapPR_prSeq_f*/
    { 1,  0, 20, "prAppDataValid0"                       },  /* IpeParserIntfMapPR_prAppDataValid0_f*/
    { 1,  0, 19, "prAppDataValid1"                       },  /* IpeParserIntfMapPR_prAppDataValid1_f*/
    {19,  0,  0, "prAppData_1"                           },  /* IpeParserIntfMapPR_prAppData_1_f*/
    {13,  1, 19, "prAppData_0"                           },  /* IpeParserIntfMapPR_prAppData_0_f*/
    {14,  1,  5, "prIpLength"                            },  /* IpeParserIntfMapPR_prIpLength_f*/
    { 5,  1,  0, "prL4ErrorBits"                         },  /* IpeParserIntfMapPR_prL4ErrorBits_f*/
    { 1,  2, 31, "prParserLengthError"                   },  /* IpeParserIntfMapPR_prParserLengthError_f*/
    { 8,  2, 23, "prLayer2Offset"                        },  /* IpeParserIntfMapPR_prLayer2Offset_f*/
    { 8,  2, 15, "prLayer3Offset"                        },  /* IpeParserIntfMapPR_prLayer3Offset_f*/
    { 8,  2,  7, "prLayer4Offset"                        },  /* IpeParserIntfMapPR_prLayer4Offset_f*/
    { 4,  2,  3, "prLayer4Type"                          },  /* IpeParserIntfMapPR_prLayer4Type_f*/
    { 3,  2,  0, "prLayer4UserType_1"                    },  /* IpeParserIntfMapPR_prLayer4UserType_1_f*/
    { 1,  3, 31, "prLayer4UserType_0"                    },  /* IpeParserIntfMapPR_prLayer4UserType_0_f*/
    {31,  3,  0, "prGreKey_1"                            },  /* IpeParserIntfMapPR_prGreKey_1_f*/
    { 1,  4, 31, "prGreKey_0"                            },  /* IpeParserIntfMapPR_prGreKey_0_f*/
    { 8,  4, 23, "prTtl"                                 },  /* IpeParserIntfMapPR_prTtl_f*/
    { 1,  4, 22, "prIsIsatapInterface"                   },  /* IpeParserIntfMapPR_prIsIsatapInterface_f*/
    { 5,  4, 17, "prRid"                                 },  /* IpeParserIntfMapPR_prRid_f*/
    { 8,  4,  9, "prMacEcmpHash"                         },  /* IpeParserIntfMapPR_prMacEcmpHash_f*/
    { 8,  4,  1, "prIpEcmpHash"                          },  /* IpeParserIntfMapPR_prIpEcmpHash_f*/
    { 1,  4,  0, "prUdpPtpCorrectionField_1"             },  /* IpeParserIntfMapPR_prUdpPtpCorrectionField_1_f*/
    {31,  5,  1, "prUdpPtpCorrectionField_0"             },  /* IpeParserIntfMapPR_prUdpPtpCorrectionField_0_f*/
    { 1,  5,  0, "prCnTagValid"                          },  /* IpeParserIntfMapPR_prCnTagValid_f*/
    {16,  6, 16, "prCnFlowId"                            },  /* IpeParserIntfMapPR_prCnFlowId_f*/
    {16,  6,  0, "prLayer2HeaderProtocol"                },  /* IpeParserIntfMapPR_prLayer2HeaderProtocol_f*/
    { 8,  7, 24, "prLayer3HeaderProtocol"                },  /* IpeParserIntfMapPR_prLayer3HeaderProtocol_f*/
    { 3,  7, 21, "prStagCos"                             },  /* IpeParserIntfMapPR_prStagCos_f*/
    { 1,  7, 20, "prStagCfi"                             },  /* IpeParserIntfMapPR_prStagCfi_f*/
    { 3,  7, 17, "prCtagCos"                             },  /* IpeParserIntfMapPR_prCtagCos_f*/
    { 1,  7, 16, "prCtagCfi"                             },  /* IpeParserIntfMapPR_prCtagCfi_f*/
    { 4,  7, 12, "prLayer3Type"                          },  /* IpeParserIntfMapPR_prLayer3Type_f*/
    {12,  7,  0, "prMacDa_2"                             },  /* IpeParserIntfMapPR_prMacDa_2_f*/
    {32,  8,  0, "prMacDa_1"                             },  /* IpeParserIntfMapPR_prMacDa_1_f*/
    { 4,  9, 28, "prMacDa_0"                             },  /* IpeParserIntfMapPR_prMacDa_0_f*/
    { 4,  9, 24, "prLayer2Type"                          },  /* IpeParserIntfMapPR_prLayer2Type_f*/
    {24,  9,  0, "prMacSa_1"                             },  /* IpeParserIntfMapPR_prMacSa_1_f*/
    {24, 10,  8, "prMacSa_0"                             },  /* IpeParserIntfMapPR_prMacSa_0_f*/
    { 1, 10,  7, "prIpHeaderError"                       },  /* IpeParserIntfMapPR_prIpHeaderError_f*/
    { 1, 10,  6, "prIsTcp"                               },  /* IpeParserIntfMapPR_prIsTcp_f*/
    { 1, 10,  5, "prIsUdp"                               },  /* IpeParserIntfMapPR_prIsUdp_f*/
    { 1, 10,  4, "prIpOptions"                           },  /* IpeParserIntfMapPR_prIpOptions_f*/
    { 2, 10,  2, "prFragInfo"                            },  /* IpeParserIntfMapPR_prFragInfo_f*/
    { 2, 10,  0, "prTos_1"                               },  /* IpeParserIntfMapPR_prTos_1_f*/
    { 6, 11, 26, "prTos_0"                               },  /* IpeParserIntfMapPR_prTos_0_f*/
    {12, 11, 14, "prL4InfoMapped"                        },  /* IpeParserIntfMapPR_prL4InfoMapped_f*/
    {14, 11,  0, "prL4DestPort_1"                        },  /* IpeParserIntfMapPR_prL4DestPort_1_f*/
    { 2, 12, 30, "prL4DestPort_0"                        },  /* IpeParserIntfMapPR_prL4DestPort_0_f*/
    {16, 12, 14, "prL4SourcePort"                        },  /* IpeParserIntfMapPR_prL4SourcePort_f*/
    {14, 12,  0, "prIpv6FlowLabel_1"                     },  /* IpeParserIntfMapPR_prIpv6FlowLabel_1_f*/
    { 6, 13, 26, "prIpv6FlowLabel_0"                     },  /* IpeParserIntfMapPR_prIpv6FlowLabel_0_f*/
    {26, 13,  0, "prIpDa_4"                              },  /* IpeParserIntfMapPR_prIpDa_4_f*/
    {32, 14,  0, "prIpDa_3"                              },  /* IpeParserIntfMapPR_prIpDa_3_f*/
    {32, 15,  0, "prIpDa_2"                              },  /* IpeParserIntfMapPR_prIpDa_2_f*/
    {32, 16,  0, "prIpDa_1"                              },  /* IpeParserIntfMapPR_prIpDa_1_f*/
    { 6, 17, 26, "prIpDa_0"                              },  /* IpeParserIntfMapPR_prIpDa_0_f*/
    {26, 17,  0, "prIpSa_4"                              },  /* IpeParserIntfMapPR_prIpSa_4_f*/
    {32, 18,  0, "prIpSa_3"                              },  /* IpeParserIntfMapPR_prIpSa_3_f*/
    {32, 19,  0, "prIpSa_2"                              },  /* IpeParserIntfMapPR_prIpSa_2_f*/
    {32, 20,  0, "prIpSa_1"                              },  /* IpeParserIntfMapPR_prIpSa_1_f*/
    { 6, 21, 26, "prIpSa_0"                              },  /* IpeParserIntfMapPR_prIpSa_0_f*/
    {12, 21, 14, "prCvlanId"                             },  /* IpeParserIntfMapPR_prCvlanId_f*/
    {12, 21,  2, "prSvlanId"                             },  /* IpeParserIntfMapPR_prSvlanId_f*/
    { 1, 21,  1, "prCvlanIdValid"                        },  /* IpeParserIntfMapPR_prCvlanIdValid_f*/
    { 1, 21,  0, "prSvlanIdValid"                        },  /* IpeParserIntfMapPR_prSvlanIdValid_f*/
};

static fields_t Ipe_IntfMap_LkupMgr_PI_field[] = {
    { 8,  0, 12, "piSeq"                                 },  /* IpeIntfMapLkupMgrPI_piSeq_f*/
    { 8,  0,  4, "piChannelId"                           },  /* IpeIntfMapLkupMgrPI_piChannelId_f*/
    { 1,  0,  3, "piMplsSectionLmEn"                     },  /* IpeIntfMapLkupMgrPI_piMplsSectionLmEn_f*/
    { 3,  0,  0, "piLocalPhyPort_1"                      },  /* IpeIntfMapLkupMgrPI_piLocalPhyPort_1_f*/
    { 4,  1, 28, "piLocalPhyPort_0"                      },  /* IpeIntfMapLkupMgrPI_piLocalPhyPort_0_f*/
    { 1,  1, 27, "piFromCpuOrOam"                        },  /* IpeIntfMapLkupMgrPI_piFromCpuOrOam_f*/
    {14,  1, 13, "piLogicSrcPort"                        },  /* IpeIntfMapLkupMgrPI_piLogicSrcPort_f*/
    { 8,  1,  5, "piPayloadOffset"                       },  /* IpeIntfMapLkupMgrPI_piPayloadOffset_f*/
    { 4,  1,  1, "piRxOamType"                           },  /* IpeIntfMapLkupMgrPI_piRxOamType_f*/
    { 1,  1,  0, "piOamUseFid"                           },  /* IpeIntfMapLkupMgrPI_piOamUseFid_f*/
    {12,  2, 20, "piUserDefaultVlanId"                   },  /* IpeIntfMapLkupMgrPI_piUserDefaultVlanId_f*/
    { 3,  2, 17, "piUserDefaultCos"                      },  /* IpeIntfMapLkupMgrPI_piUserDefaultCos_f*/
    { 1,  2, 16, "piUserDefaultCfi"                      },  /* IpeIntfMapLkupMgrPI_piUserDefaultCfi_f*/
    { 1,  2, 15, "piDiscard"                             },  /* IpeIntfMapLkupMgrPI_piDiscard_f*/
    { 6,  2,  9, "piDiscardType"                         },  /* IpeIntfMapLkupMgrPI_piDiscardType_f*/
    { 1,  2,  8, "piHardError"                           },  /* IpeIntfMapLkupMgrPI_piHardError_f*/
    { 3,  2,  5, "piPbbSrcPortType"                      },  /* IpeIntfMapLkupMgrPI_piPbbSrcPortType_f*/
    { 1,  2,  4, "piExceptionEn"                         },  /* IpeIntfMapLkupMgrPI_piExceptionEn_f*/
    { 3,  2,  1, "piExceptionIndex"                      },  /* IpeIntfMapLkupMgrPI_piExceptionIndex_f*/
    { 1,  2,  0, "piExceptionSubIndex_1"                 },  /* IpeIntfMapLkupMgrPI_piExceptionSubIndex_1_f*/
    { 5,  3, 27, "piExceptionSubIndex_0"                 },  /* IpeIntfMapLkupMgrPI_piExceptionSubIndex_0_f*/
    { 3,  3, 24, "piDefaultPcp"                          },  /* IpeIntfMapLkupMgrPI_piDefaultPcp_f*/
    { 1,  3, 23, "piDefaultDei"                          },  /* IpeIntfMapLkupMgrPI_piDefaultDei_f*/
    { 3,  3, 20, "piClassifyStagCos"                     },  /* IpeIntfMapLkupMgrPI_piClassifyStagCos_f*/
    { 1,  3, 19, "piClassifyStagCfi"                     },  /* IpeIntfMapLkupMgrPI_piClassifyStagCfi_f*/
    { 3,  3, 16, "piClassifyCtagCos"                     },  /* IpeIntfMapLkupMgrPI_piClassifyCtagCos_f*/
    { 1,  3, 15, "piClassifyCtagCfi"                     },  /* IpeIntfMapLkupMgrPI_piClassifyCtagCfi_f*/
    {14,  3,  1, "piGlobalSrcPort"                       },  /* IpeIntfMapLkupMgrPI_piGlobalSrcPort_f*/
    { 1,  3,  0, "piDenyBridge"                          },  /* IpeIntfMapLkupMgrPI_piDenyBridge_f*/
    { 1,  4, 31, "piRouteDisable"                        },  /* IpeIntfMapLkupMgrPI_piRouteDisable_f*/
    { 3,  4, 28, "piPayloadPacketType"                   },  /* IpeIntfMapLkupMgrPI_piPayloadPacketType_f*/
    { 1,  4, 27, "piLearningDisable"                     },  /* IpeIntfMapLkupMgrPI_piLearningDisable_f*/
    { 1,  4, 26, "piServiceAclQosEn"                     },  /* IpeIntfMapLkupMgrPI_piServiceAclQosEn_f*/
    {13,  4, 13, "piVlanPtr"                             },  /* IpeIntfMapLkupMgrPI_piVlanPtr_f*/
    {13,  4,  0, "piVsiId_1"                             },  /* IpeIntfMapLkupMgrPI_piVsiId_1_f*/
    { 1,  5, 31, "piVsiId_0"                             },  /* IpeIntfMapLkupMgrPI_piVsiId_0_f*/
    { 1,  5, 30, "piL2AclEn0"                            },  /* IpeIntfMapLkupMgrPI_piL2AclEn0_f*/
    { 1,  5, 29, "piL2AclEn1"                            },  /* IpeIntfMapLkupMgrPI_piL2AclEn1_f*/
    { 1,  5, 28, "piL2AclEn2"                            },  /* IpeIntfMapLkupMgrPI_piL2AclEn2_f*/
    { 1,  5, 27, "piL2AclEn3"                            },  /* IpeIntfMapLkupMgrPI_piL2AclEn3_f*/
    { 1,  5, 26, "piBridgeEn"                            },  /* IpeIntfMapLkupMgrPI_piBridgeEn_f*/
    {10,  5, 16, "piInterfaceId"                         },  /* IpeIntfMapLkupMgrPI_piInterfaceId_f*/
    {10,  5,  6, "piL2AclLabel"                          },  /* IpeIntfMapLkupMgrPI_piL2AclLabel_f*/
    { 1,  5,  5, "piRoutedPort"                          },  /* IpeIntfMapLkupMgrPI_piRoutedPort_f*/
    { 1,  5,  4, "piTtlUpdate"                           },  /* IpeIntfMapLkupMgrPI_piTtlUpdate_f*/
    { 1,  5,  3, "piForceIpv4Lookup"                     },  /* IpeIntfMapLkupMgrPI_piForceIpv4Lookup_f*/
    { 1,  5,  2, "piForceIpv6Lookup"                     },  /* IpeIntfMapLkupMgrPI_piForceIpv6Lookup_f*/
    { 1,  5,  1, "piForceAclQosIpv4ToMacKey"             },  /* IpeIntfMapLkupMgrPI_piForceAclQosIpv4ToMacKey_f*/
    { 1,  5,  0, "piForceAclQosIpv6ToMacKey"             },  /* IpeIntfMapLkupMgrPI_piForceAclQosIpv6ToMacKey_f*/
    { 1,  6, 31, "piTrillEn"                             },  /* IpeIntfMapLkupMgrPI_piTrillEn_f*/
    { 1,  6, 30, "piFcoeEn"                              },  /* IpeIntfMapLkupMgrPI_piFcoeEn_f*/
    { 1,  6, 29, "piFcoeRpfEn"                           },  /* IpeIntfMapLkupMgrPI_piFcoeRpfEn_f*/
    { 6,  6, 23, "piAclPortNum"                          },  /* IpeIntfMapLkupMgrPI_piAclPortNum_f*/
    { 1,  6, 22, "piL2Ipv6AclEn0"                        },  /* IpeIntfMapLkupMgrPI_piL2Ipv6AclEn0_f*/
    { 1,  6, 21, "piL2Ipv6AclEn1"                        },  /* IpeIntfMapLkupMgrPI_piL2Ipv6AclEn1_f*/
    { 1,  6, 20, "piMacKeyUseLabel"                      },  /* IpeIntfMapLkupMgrPI_piMacKeyUseLabel_f*/
    { 1,  6, 19, "piIpv4KeyUseLabel"                     },  /* IpeIntfMapLkupMgrPI_piIpv4KeyUseLabel_f*/
    { 1,  6, 18, "piIpv6KeyUseLabel"                     },  /* IpeIntfMapLkupMgrPI_piIpv6KeyUseLabel_f*/
    { 1,  6, 17, "piMplsKeyUseLabel"                     },  /* IpeIntfMapLkupMgrPI_piMplsKeyUseLabel_f*/
    { 1,  6, 16, "piBcastMacAddress"                     },  /* IpeIntfMapLkupMgrPI_piBcastMacAddress_f*/
    { 1,  6, 15, "piIsPortMac"                           },  /* IpeIntfMapLkupMgrPI_piIsPortMac_f*/
    { 3,  6, 12, "piSourceCos"                           },  /* IpeIntfMapLkupMgrPI_piSourceCos_f*/
    { 1,  6, 11, "piSourceCfi"                           },  /* IpeIntfMapLkupMgrPI_piSourceCfi_f*/
    {11,  6,  0, "piSvlanId_1"                           },  /* IpeIntfMapLkupMgrPI_piSvlanId_1_f*/
    { 1,  7, 31, "piSvlanId_0"                           },  /* IpeIntfMapLkupMgrPI_piSvlanId_0_f*/
    {12,  7, 19, "piCvlanId"                             },  /* IpeIntfMapLkupMgrPI_piCvlanId_f*/
    {10,  7,  9, "piL3AclLabel"                          },  /* IpeIntfMapLkupMgrPI_piL3AclLabel_f*/
    { 1,  7,  8, "piPtpEn"                               },  /* IpeIntfMapLkupMgrPI_piPtpEn_f*/
    { 1,  7,  7, "piL3AclRoutedOnly"                     },  /* IpeIntfMapLkupMgrPI_piL3AclRoutedOnly_f*/
    { 1,  7,  6, "piL3AclEn0"                            },  /* IpeIntfMapLkupMgrPI_piL3AclEn0_f*/
    { 1,  7,  5, "piL3AclEn1"                            },  /* IpeIntfMapLkupMgrPI_piL3AclEn1_f*/
    { 1,  7,  4, "piL3AclEn2"                            },  /* IpeIntfMapLkupMgrPI_piL3AclEn2_f*/
    { 1,  7,  3, "piL3AclEn3"                            },  /* IpeIntfMapLkupMgrPI_piL3AclEn3_f*/
    { 1,  7,  2, "piL3Ipv6AclEn0"                        },  /* IpeIntfMapLkupMgrPI_piL3Ipv6AclEn0_f*/
    { 1,  7,  1, "piL3Ipv6AclEn1"                        },  /* IpeIntfMapLkupMgrPI_piL3Ipv6AclEn1_f*/
    { 1,  7,  0, "piVrfId_1"                             },  /* IpeIntfMapLkupMgrPI_piVrfId_1_f*/
    {13,  8, 19, "piVrfId_0"                             },  /* IpeIntfMapLkupMgrPI_piVrfId_0_f*/
    { 1,  8, 18, "piL3IfType"                            },  /* IpeIntfMapLkupMgrPI_piL3IfType_f*/
    { 2,  8, 16, "piV4UcastSaType"                       },  /* IpeIntfMapLkupMgrPI_piV4UcastSaType_f*/
    { 1,  8, 15, "piV4UcastEn"                           },  /* IpeIntfMapLkupMgrPI_piV4UcastEn_f*/
    { 1,  8, 14, "piV4McastEn"                           },  /* IpeIntfMapLkupMgrPI_piV4McastEn_f*/
    { 1,  8, 13, "piV6UcastEn"                           },  /* IpeIntfMapLkupMgrPI_piV6UcastEn_f*/
    { 1,  8, 12, "piV6McastEn"                           },  /* IpeIntfMapLkupMgrPI_piV6McastEn_f*/
    { 2,  8, 10, "piV6UcastSaType"                       },  /* IpeIntfMapLkupMgrPI_piV6UcastSaType_f*/
    { 6,  8,  4, "piPbrLabel"                            },  /* IpeIntfMapLkupMgrPI_piPbrLabel_f*/
    { 1,  8,  3, "piEtherLmValid"                        },  /* IpeIntfMapLkupMgrPI_piEtherLmValid_f*/
    { 1,  8,  2, "piEtherOamDiscard"                     },  /* IpeIntfMapLkupMgrPI_piEtherOamDiscard_f*/
    { 2,  8,  0, "piStpState"                            },  /* IpeIntfMapLkupMgrPI_piStpState_f*/
    { 1,  9, 31, "piIsRouterMac"                         },  /* IpeIntfMapLkupMgrPI_piIsRouterMac_f*/
    { 1,  9, 30, "piIsMplsSwitched"                      },  /* IpeIntfMapLkupMgrPI_piIsMplsSwitched_f*/
    { 1,  9, 29, "piIsDecap"                             },  /* IpeIntfMapLkupMgrPI_piIsDecap_f*/
    { 1,  9, 28, "piInnerPacketLookup"                   },  /* IpeIntfMapLkupMgrPI_piInnerPacketLookup_f*/
    { 1,  9, 27, "piVsiLearningDisable"                  },  /* IpeIntfMapLkupMgrPI_piVsiLearningDisable_f*/
    { 1,  9, 26, "piTunnelPtpEn"                         },  /* IpeIntfMapLkupMgrPI_piTunnelPtpEn_f*/
    { 1,  9, 25, "piAclQosUseOuterInfo"                  },  /* IpeIntfMapLkupMgrPI_piAclQosUseOuterInfo_f*/
    { 1,  9, 24, "piUseDefaultVlanTag"                   },  /* IpeIntfMapLkupMgrPI_piUseDefaultVlanTag_f*/
    { 1,  9, 23, "piLinkOrSectionOam"                    },  /* IpeIntfMapLkupMgrPI_piLinkOrSectionOam_f*/
    { 1,  9, 22, "piRouteLookupMode"                     },  /* IpeIntfMapLkupMgrPI_piRouteLookupMode_f*/
    { 1,  9, 21, "piSecondParser"                        },  /* IpeIntfMapLkupMgrPI_piSecondParser_f*/
    { 1,  9, 20, "piPipBypassLearning"                   },  /* IpeIntfMapLkupMgrPI_piPipBypassLearning_f*/
    { 1,  9, 19, "piMplsMepCheck"                        },  /* IpeIntfMapLkupMgrPI_piMplsMepCheck_f*/
    { 1,  9, 18, "piInnerVsiIdValid"                     },  /* IpeIntfMapLkupMgrPI_piInnerVsiIdValid_f*/
    {14,  9,  4, "piInnerVsiId"                          },  /* IpeIntfMapLkupMgrPI_piInnerVsiId_f*/
    { 1,  9,  3, "piMcastMacAddress"                     },  /* IpeIntfMapLkupMgrPI_piMcastMacAddress_f*/
    { 1,  9,  2, "piForceParser"                         },  /* IpeIntfMapLkupMgrPI_piForceParser_f*/
    { 2,  9,  0, "piPacketTtl_1"                         },  /* IpeIntfMapLkupMgrPI_piPacketTtl_1_f*/
    { 6, 10, 26, "piPacketTtl_0"                         },  /* IpeIntfMapLkupMgrPI_piPacketTtl_0_f*/
    { 3, 10, 23, "piPacketCos0"                          },  /* IpeIntfMapLkupMgrPI_piPacketCos0_f*/
    { 1, 10, 22, "piLmTypeIsNot0"                        },  /* IpeIntfMapLkupMgrPI_piLmTypeIsNot0_f*/
    { 1, 10, 21, "piSvlanIdValid"                        },  /* IpeIntfMapLkupMgrPI_piSvlanIdValid_f*/
    { 1, 10, 20, "piCvlanIdValid"                        },  /* IpeIntfMapLkupMgrPI_piCvlanIdValid_f*/
    { 1, 10, 19, "piInnerLogicSrcPortValid"              },  /* IpeIntfMapLkupMgrPI_piInnerLogicSrcPortValid_f*/
    {14, 10,  5, "piInnerLogicSrcPort"                   },  /* IpeIntfMapLkupMgrPI_piInnerLogicSrcPort_f*/
    { 1, 10,  4, "piFidType"                             },  /* IpeIntfMapLkupMgrPI_piFidType_f*/
    { 1, 10,  3, "piFatalExceptionValid"                 },  /* IpeIntfMapLkupMgrPI_piFatalExceptionValid_f*/
    { 1, 10,  2, "piForceIpv6Key"                        },  /* IpeIntfMapLkupMgrPI_piForceIpv6Key_f*/
    { 1, 10,  1, "piAppDataValid0"                       },  /* IpeIntfMapLkupMgrPI_piAppDataValid0_f*/
    { 1, 10,  0, "piAppDataValid1"                       },  /* IpeIntfMapLkupMgrPI_piAppDataValid1_f*/
    {32, 11,  0, "piAppData"                             },  /* IpeIntfMapLkupMgrPI_piAppData_f*/
    { 1, 12, 31, "prParserLengthError"                   },  /* IpeIntfMapLkupMgrPI_prParserLengthError_f*/
    { 8, 12, 23, "prLayer2Offset"                        },  /* IpeIntfMapLkupMgrPI_prLayer2Offset_f*/
    { 8, 12, 15, "prLayer3Offset"                        },  /* IpeIntfMapLkupMgrPI_prLayer3Offset_f*/
    { 8, 12,  7, "prLayer4Offset"                        },  /* IpeIntfMapLkupMgrPI_prLayer4Offset_f*/
    { 4, 12,  3, "prLayer4Type"                          },  /* IpeIntfMapLkupMgrPI_prLayer4Type_f*/
    { 3, 12,  0, "prLayer4UserType_1"                    },  /* IpeIntfMapLkupMgrPI_prLayer4UserType_1_f*/
    { 1, 13, 31, "prLayer4UserType_0"                    },  /* IpeIntfMapLkupMgrPI_prLayer4UserType_0_f*/
    {31, 13,  0, "prGreKey_1"                            },  /* IpeIntfMapLkupMgrPI_prGreKey_1_f*/
    { 1, 14, 31, "prGreKey_0"                            },  /* IpeIntfMapLkupMgrPI_prGreKey_0_f*/
    { 8, 14, 23, "prTtl"                                 },  /* IpeIntfMapLkupMgrPI_prTtl_f*/
    { 1, 14, 22, "prIsIsatapInterface"                   },  /* IpeIntfMapLkupMgrPI_prIsIsatapInterface_f*/
    { 5, 14, 17, "prRid"                                 },  /* IpeIntfMapLkupMgrPI_prRid_f*/
    { 8, 14,  9, "prMacEcmpHash"                         },  /* IpeIntfMapLkupMgrPI_prMacEcmpHash_f*/
    { 8, 14,  1, "prIpEcmpHash"                          },  /* IpeIntfMapLkupMgrPI_prIpEcmpHash_f*/
    { 1, 14,  0, "prUdpPtpCorrectionField_1"             },  /* IpeIntfMapLkupMgrPI_prUdpPtpCorrectionField_1_f*/
    {31, 15,  1, "prUdpPtpCorrectionField_0"             },  /* IpeIntfMapLkupMgrPI_prUdpPtpCorrectionField_0_f*/
    { 1, 15,  0, "prCnTagValid"                          },  /* IpeIntfMapLkupMgrPI_prCnTagValid_f*/
    {16, 16, 16, "prCnFlowId"                            },  /* IpeIntfMapLkupMgrPI_prCnFlowId_f*/
    {16, 16,  0, "prLayer2HeaderProtocol"                },  /* IpeIntfMapLkupMgrPI_prLayer2HeaderProtocol_f*/
    { 8, 17, 24, "prLayer3HeaderProtocol"                },  /* IpeIntfMapLkupMgrPI_prLayer3HeaderProtocol_f*/
    { 3, 17, 21, "prStagCos"                             },  /* IpeIntfMapLkupMgrPI_prStagCos_f*/
    { 1, 17, 20, "prStagCfi"                             },  /* IpeIntfMapLkupMgrPI_prStagCfi_f*/
    { 3, 17, 17, "prCtagCos"                             },  /* IpeIntfMapLkupMgrPI_prCtagCos_f*/
    { 1, 17, 16, "prCtagCfi"                             },  /* IpeIntfMapLkupMgrPI_prCtagCfi_f*/
    { 4, 17, 12, "prLayer3Type"                          },  /* IpeIntfMapLkupMgrPI_prLayer3Type_f*/
    {12, 17,  0, "prMacDa_2"                             },  /* IpeIntfMapLkupMgrPI_prMacDa_2_f*/
    {32, 18,  0, "prMacDa_1"                             },  /* IpeIntfMapLkupMgrPI_prMacDa_1_f*/
    { 4, 19, 28, "prMacDa_0"                             },  /* IpeIntfMapLkupMgrPI_prMacDa_0_f*/
    { 4, 19, 24, "prLayer2Type"                          },  /* IpeIntfMapLkupMgrPI_prLayer2Type_f*/
    {24, 19,  0, "prMacSa_1"                             },  /* IpeIntfMapLkupMgrPI_prMacSa_1_f*/
    {24, 20,  8, "prMacSa_0"                             },  /* IpeIntfMapLkupMgrPI_prMacSa_0_f*/
    { 1, 20,  7, "prIpHeaderError"                       },  /* IpeIntfMapLkupMgrPI_prIpHeaderError_f*/
    { 1, 20,  6, "prIsTcp"                               },  /* IpeIntfMapLkupMgrPI_prIsTcp_f*/
    { 1, 20,  5, "prIsUdp"                               },  /* IpeIntfMapLkupMgrPI_prIsUdp_f*/
    { 1, 20,  4, "prIpOptions"                           },  /* IpeIntfMapLkupMgrPI_prIpOptions_f*/
    { 2, 20,  2, "prFragInfo"                            },  /* IpeIntfMapLkupMgrPI_prFragInfo_f*/
    { 2, 20,  0, "prTos_1"                               },  /* IpeIntfMapLkupMgrPI_prTos_1_f*/
    { 6, 21, 26, "prTos_0"                               },  /* IpeIntfMapLkupMgrPI_prTos_0_f*/
    {12, 21, 14, "prL4InfoMapped"                        },  /* IpeIntfMapLkupMgrPI_prL4InfoMapped_f*/
    {14, 21,  0, "prL4DestPort_1"                        },  /* IpeIntfMapLkupMgrPI_prL4DestPort_1_f*/
    { 2, 22, 30, "prL4DestPort_0"                        },  /* IpeIntfMapLkupMgrPI_prL4DestPort_0_f*/
    {16, 22, 14, "prL4SourcePort"                        },  /* IpeIntfMapLkupMgrPI_prL4SourcePort_f*/
    {14, 22,  0, "prIpv6FlowLabel_1"                     },  /* IpeIntfMapLkupMgrPI_prIpv6FlowLabel_1_f*/
    { 6, 23, 26, "prIpv6FlowLabel_0"                     },  /* IpeIntfMapLkupMgrPI_prIpv6FlowLabel_0_f*/
    {26, 23,  0, "prIpDa_4"                              },  /* IpeIntfMapLkupMgrPI_prIpDa_4_f*/
    {32, 24,  0, "prIpDa_3"                              },  /* IpeIntfMapLkupMgrPI_prIpDa_3_f*/
    {32, 25,  0, "prIpDa_2"                              },  /* IpeIntfMapLkupMgrPI_prIpDa_2_f*/
    {32, 26,  0, "prIpDa_1"                              },  /* IpeIntfMapLkupMgrPI_prIpDa_1_f*/
    { 6, 27, 26, "prIpDa_0"                              },  /* IpeIntfMapLkupMgrPI_prIpDa_0_f*/
    {26, 27,  0, "prIpSa_4"                              },  /* IpeIntfMapLkupMgrPI_prIpSa_4_f*/
    {32, 28,  0, "prIpSa_3"                              },  /* IpeIntfMapLkupMgrPI_prIpSa_3_f*/
    {32, 29,  0, "prIpSa_2"                              },  /* IpeIntfMapLkupMgrPI_prIpSa_2_f*/
    {32, 30,  0, "prIpSa_1"                              },  /* IpeIntfMapLkupMgrPI_prIpSa_1_f*/
    { 6, 31, 26, "prIpSa_0"                              },  /* IpeIntfMapLkupMgrPI_prIpSa_0_f*/
    {12, 31, 14, "prCvlanId"                             },  /* IpeIntfMapLkupMgrPI_prCvlanId_f*/
    {12, 31,  2, "prSvlanId"                             },  /* IpeIntfMapLkupMgrPI_prSvlanId_f*/
    { 1, 31,  1, "prCvlanIdValid"                        },  /* IpeIntfMapLkupMgrPI_prCvlanIdValid_f*/
    { 1, 31,  0, "prSvlanIdValid"                        },  /* IpeIntfMapLkupMgrPI_prSvlanIdValid_f*/
};

static fields_t Ipe_Parser_LkupMgr_PI_field[] = {
    { 8,  0,  0, "prSeq"                                 },  /* IpeParserLkupMgrPI_prSeq_f*/
    { 1,  1, 31, "prParserLengthError"                   },  /* IpeParserLkupMgrPI_prParserLengthError_f*/
    { 8,  1, 23, "prLayer2Offset"                        },  /* IpeParserLkupMgrPI_prLayer2Offset_f*/
    { 8,  1, 15, "prLayer3Offset"                        },  /* IpeParserLkupMgrPI_prLayer3Offset_f*/
    { 8,  1,  7, "prLayer4Offset"                        },  /* IpeParserLkupMgrPI_prLayer4Offset_f*/
    { 4,  1,  3, "prLayer4Type"                          },  /* IpeParserLkupMgrPI_prLayer4Type_f*/
    { 3,  1,  0, "prLayer4UserType_1"                    },  /* IpeParserLkupMgrPI_prLayer4UserType_1_f*/
    { 1,  2, 31, "prLayer4UserType_0"                    },  /* IpeParserLkupMgrPI_prLayer4UserType_0_f*/
    {31,  2,  0, "prGreKey_1"                            },  /* IpeParserLkupMgrPI_prGreKey_1_f*/
    { 1,  3, 31, "prGreKey_0"                            },  /* IpeParserLkupMgrPI_prGreKey_0_f*/
    { 8,  3, 23, "prTtl"                                 },  /* IpeParserLkupMgrPI_prTtl_f*/
    { 1,  3, 22, "prIsIsatapInterface"                   },  /* IpeParserLkupMgrPI_prIsIsatapInterface_f*/
    { 5,  3, 17, "prRid"                                 },  /* IpeParserLkupMgrPI_prRid_f*/
    { 8,  3,  9, "prMacEcmpHash"                         },  /* IpeParserLkupMgrPI_prMacEcmpHash_f*/
    { 8,  3,  1, "prIpEcmpHash"                          },  /* IpeParserLkupMgrPI_prIpEcmpHash_f*/
    { 1,  3,  0, "prUdpPtpCorrectionField_1"             },  /* IpeParserLkupMgrPI_prUdpPtpCorrectionField_1_f*/
    {31,  4,  1, "prUdpPtpCorrectionField_0"             },  /* IpeParserLkupMgrPI_prUdpPtpCorrectionField_0_f*/
    { 1,  4,  0, "prCnTagValid"                          },  /* IpeParserLkupMgrPI_prCnTagValid_f*/
    {16,  5, 16, "prCnFlowId"                            },  /* IpeParserLkupMgrPI_prCnFlowId_f*/
    {16,  5,  0, "prLayer2HeaderProtocol"                },  /* IpeParserLkupMgrPI_prLayer2HeaderProtocol_f*/
    { 8,  6, 24, "prLayer3HeaderProtocol"                },  /* IpeParserLkupMgrPI_prLayer3HeaderProtocol_f*/
    { 3,  6, 21, "prStagCos"                             },  /* IpeParserLkupMgrPI_prStagCos_f*/
    { 1,  6, 20, "prStagCfi"                             },  /* IpeParserLkupMgrPI_prStagCfi_f*/
    { 3,  6, 17, "prCtagCos"                             },  /* IpeParserLkupMgrPI_prCtagCos_f*/
    { 1,  6, 16, "prCtagCfi"                             },  /* IpeParserLkupMgrPI_prCtagCfi_f*/
    { 4,  6, 12, "prLayer3Type"                          },  /* IpeParserLkupMgrPI_prLayer3Type_f*/
    {12,  6,  0, "prMacDa_2"                             },  /* IpeParserLkupMgrPI_prMacDa_2_f*/
    {32,  7,  0, "prMacDa_1"                             },  /* IpeParserLkupMgrPI_prMacDa_1_f*/
    { 4,  8, 28, "prMacDa_0"                             },  /* IpeParserLkupMgrPI_prMacDa_0_f*/
    { 4,  8, 24, "prLayer2Type"                          },  /* IpeParserLkupMgrPI_prLayer2Type_f*/
    {24,  8,  0, "prMacSa_1"                             },  /* IpeParserLkupMgrPI_prMacSa_1_f*/
    {24,  9,  8, "prMacSa_0"                             },  /* IpeParserLkupMgrPI_prMacSa_0_f*/
    { 1,  9,  7, "prIpHeaderError"                       },  /* IpeParserLkupMgrPI_prIpHeaderError_f*/
    { 1,  9,  6, "prIsTcp"                               },  /* IpeParserLkupMgrPI_prIsTcp_f*/
    { 1,  9,  5, "prIsUdp"                               },  /* IpeParserLkupMgrPI_prIsUdp_f*/
    { 1,  9,  4, "prIpOptions"                           },  /* IpeParserLkupMgrPI_prIpOptions_f*/
    { 2,  9,  2, "prFragInfo"                            },  /* IpeParserLkupMgrPI_prFragInfo_f*/
    { 2,  9,  0, "prTos_1"                               },  /* IpeParserLkupMgrPI_prTos_1_f*/
    { 6, 10, 26, "prTos_0"                               },  /* IpeParserLkupMgrPI_prTos_0_f*/
    {12, 10, 14, "prL4InfoMapped"                        },  /* IpeParserLkupMgrPI_prL4InfoMapped_f*/
    {14, 10,  0, "prL4DestPort_1"                        },  /* IpeParserLkupMgrPI_prL4DestPort_1_f*/
    { 2, 11, 30, "prL4DestPort_0"                        },  /* IpeParserLkupMgrPI_prL4DestPort_0_f*/
    {16, 11, 14, "prL4SourcePort"                        },  /* IpeParserLkupMgrPI_prL4SourcePort_f*/
    {14, 11,  0, "prIpv6FlowLabel_1"                     },  /* IpeParserLkupMgrPI_prIpv6FlowLabel_1_f*/
    { 6, 12, 26, "prIpv6FlowLabel_0"                     },  /* IpeParserLkupMgrPI_prIpv6FlowLabel_0_f*/
    {26, 12,  0, "prIpDa_4"                              },  /* IpeParserLkupMgrPI_prIpDa_4_f*/
    {32, 13,  0, "prIpDa_3"                              },  /* IpeParserLkupMgrPI_prIpDa_3_f*/
    {32, 14,  0, "prIpDa_2"                              },  /* IpeParserLkupMgrPI_prIpDa_2_f*/
    {32, 15,  0, "prIpDa_1"                              },  /* IpeParserLkupMgrPI_prIpDa_1_f*/
    { 6, 16, 26, "prIpDa_0"                              },  /* IpeParserLkupMgrPI_prIpDa_0_f*/
    {26, 16,  0, "prIpSa_4"                              },  /* IpeParserLkupMgrPI_prIpSa_4_f*/
    {32, 17,  0, "prIpSa_3"                              },  /* IpeParserLkupMgrPI_prIpSa_3_f*/
    {32, 18,  0, "prIpSa_2"                              },  /* IpeParserLkupMgrPI_prIpSa_2_f*/
    {32, 19,  0, "prIpSa_1"                              },  /* IpeParserLkupMgrPI_prIpSa_1_f*/
    { 6, 20, 26, "prIpSa_0"                              },  /* IpeParserLkupMgrPI_prIpSa_0_f*/
    {12, 20, 14, "prCvlanId"                             },  /* IpeParserLkupMgrPI_prCvlanId_f*/
    {12, 20,  2, "prSvlanId"                             },  /* IpeParserLkupMgrPI_prSvlanId_f*/
    { 1, 20,  1, "prCvlanIdValid"                        },  /* IpeParserLkupMgrPI_prCvlanIdValid_f*/
    { 1, 20,  0, "prSvlanIdValid"                        },  /* IpeParserLkupMgrPI_prSvlanIdValid_f*/
};

static fields_t PP_OAM_ACK_FIFO_field[] = {
    { 8,  0,  1, "dsOamLmStatsSeq"                       },  /* PPOAMACKFIFO_dsOamLmStatsSeq_f*/
    { 1,  0,  0, "dsOamLmStatsError"                     },  /* PPOAMACKFIFO_dsOamLmStatsError_f*/
    {32,  1,  0, "dsOamLmStatsResult_2"                  },  /* PPOAMACKFIFO_dsOamLmStatsResult_2_f*/
    {32,  2,  0, "dsOamLmStatsResult_1"                  },  /* PPOAMACKFIFO_dsOamLmStatsResult_1_f*/
    {32,  3,  0, "dsOamLmStatsResult_0"                  },  /* PPOAMACKFIFO_dsOamLmStatsResult_0_f*/
};

static fields_t Ipe_IntfMap_Oam_PI_field[] = {
    { 1,  0, 25, "piMplsLabelSpaceValid"                 },  /* IpeIntfMapOamPI_piMplsLabelSpaceValid_f*/
    { 8,  0, 17, "piMplsLabelSpace"                      },  /* IpeIntfMapOamPI_piMplsLabelSpace_f*/
    { 3,  0, 14, "piMplsExtraPayloadOffset"              },  /* IpeIntfMapOamPI_piMplsExtraPayloadOffset_f*/
    { 1,  0, 13, "piIsMplsTtlOne"                        },  /* IpeIntfMapOamPI_piIsMplsTtlOne_f*/
    { 1,  0, 12, "piMplsMepCheck"                        },  /* IpeIntfMapOamPI_piMplsMepCheck_f*/
    {12,  0,  0, "piMplsMepIndex_1"                      },  /* IpeIntfMapOamPI_piMplsMepIndex_1_f*/
    { 1,  1, 31, "piMplsMepIndex_0"                      },  /* IpeIntfMapOamPI_piMplsMepIndex_0_f*/
    { 2,  1, 29, "piLinkLmCosType"                       },  /* IpeIntfMapOamPI_piLinkLmCosType_f*/
    { 2,  1, 27, "piLinkLmType"                          },  /* IpeIntfMapOamPI_piLinkLmType_f*/
    {14,  1, 13, "piLinkLmIndexBase"                     },  /* IpeIntfMapOamPI_piLinkLmIndexBase_f*/
    {13,  1,  0, "piLinkOamMepIndex"                     },  /* IpeIntfMapOamPI_piLinkOamMepIndex_f*/
    { 2,  2, 30, "piLmType0"                             },  /* IpeIntfMapOamPI_piLmType0_f*/
    { 2,  2, 28, "piLmType1"                             },  /* IpeIntfMapOamPI_piLmType1_f*/
    { 2,  2, 26, "piLmType2"                             },  /* IpeIntfMapOamPI_piLmType2_f*/
    { 2,  2, 24, "piMplsLmCosType0"                      },  /* IpeIntfMapOamPI_piMplsLmCosType0_f*/
    { 2,  2, 22, "piMplsLmCosType1"                      },  /* IpeIntfMapOamPI_piMplsLmCosType1_f*/
    { 2,  2, 20, "piMplsLmCosType2"                      },  /* IpeIntfMapOamPI_piMplsLmCosType2_f*/
    { 3,  2, 17, "piMplsLmCos0"                          },  /* IpeIntfMapOamPI_piMplsLmCos0_f*/
    { 3,  2, 14, "piMplsLmCos1"                          },  /* IpeIntfMapOamPI_piMplsLmCos1_f*/
    { 3,  2, 11, "piMplsLmCos2"                          },  /* IpeIntfMapOamPI_piMplsLmCos2_f*/
    {11,  2,  0, "piMplsLmBase0_1"                       },  /* IpeIntfMapOamPI_piMplsLmBase0_1_f*/
    { 3,  3, 29, "piMplsLmBase0_0"                       },  /* IpeIntfMapOamPI_piMplsLmBase0_0_f*/
    {14,  3, 15, "piMplsLmBase1"                         },  /* IpeIntfMapOamPI_piMplsLmBase1_f*/
    {14,  3,  1, "piMplsLmBase2"                         },  /* IpeIntfMapOamPI_piMplsLmBase2_f*/
    { 1,  3,  0, "piPacketCos1_1"                        },  /* IpeIntfMapOamPI_piPacketCos1_1_f*/
    { 2,  4, 30, "piPacketCos1_0"                        },  /* IpeIntfMapOamPI_piPacketCos1_0_f*/
    { 3,  4, 27, "piPacketCos2"                          },  /* IpeIntfMapOamPI_piPacketCos2_f*/
    { 1,  4, 26, "piMplsLmType0"                         },  /* IpeIntfMapOamPI_piMplsLmType0_f*/
    { 1,  4, 25, "piMplsLmType1"                         },  /* IpeIntfMapOamPI_piMplsLmType1_f*/
    { 1,  4, 24, "piMplsLmType2"                         },  /* IpeIntfMapOamPI_piMplsLmType2_f*/
    { 5,  4, 19, "piMplsOamDestChipId"                   },  /* IpeIntfMapOamPI_piMplsOamDestChipId_f*/
    { 3,  4, 16, "piLinkLmCos"                           },  /* IpeIntfMapOamPI_piLinkLmCos_f*/
    { 2,  4, 14, "piOamLookupNum"                        },  /* IpeIntfMapOamPI_piOamLookupNum_f*/
    { 2,  4, 12, "piApsLevelForOam"                      },  /* IpeIntfMapOamPI_piApsLevelForOam_f*/
    { 6,  4,  6, "piMacIsolatedGroupId"                  },  /* IpeIntfMapOamPI_piMacIsolatedGroupId_f*/
    { 1,  4,  5, "piEtherOamEdgePort"                    },  /* IpeIntfMapOamPI_piEtherOamEdgePort_f*/
    { 1,  4,  4, "piGalExist"                            },  /* IpeIntfMapOamPI_piGalExist_f*/
    { 1,  4,  3, "piEntropyLabelExist"                   },  /* IpeIntfMapOamPI_piEntropyLabelExist_f*/
    { 1,  4,  2, "piException2LmEn"                      },  /* IpeIntfMapOamPI_piException2LmEn_f*/
    { 1,  4,  1, "piException2LmUpEn"                    },  /* IpeIntfMapOamPI_piException2LmUpEn_f*/
    { 1,  4,  0, "piException2LmDownEn"                  },  /* IpeIntfMapOamPI_piException2LmDownEn_f*/
};

static fields_t Ipe_LkupMgr_Oam_PI_field[] = {
    { 2,  0, 14, "piLmLookupType"                        },  /* IpeLkupMgrOamPI_piLmLookupType_f*/
    { 1,  0, 13, "piBfdSingleHopTtlMatch"                },  /* IpeLkupMgrOamPI_piBfdSingleHopTtlMatch_f*/
    { 1,  0, 12, "piIsMplsPacket"                        },  /* IpeLkupMgrOamPI_piIsMplsPacket_f*/
    { 3,  0,  9, "piPacketCos0"                          },  /* IpeLkupMgrOamPI_piPacketCos0_f*/
    { 1,  0,  8, "piIsPortMac"                           },  /* IpeLkupMgrOamPI_piIsPortMac_f*/
    { 4,  0,  4, "prLayer4Type"                          },  /* IpeLkupMgrOamPI_prLayer4Type_f*/
    { 4,  0,  0, "prLayer4UserType"                      },  /* IpeLkupMgrOamPI_prLayer4UserType_f*/
    { 8,  1, 24, "prEtherOamOpcode"                      },  /* IpeLkupMgrOamPI_prEtherOamOpcode_f*/
    { 8,  1, 16, "prY1731OamOpcode"                      },  /* IpeLkupMgrOamPI_prY1731OamOpcode_f*/
    {16,  1,  0, "prIpSa95To48_1"                        },  /* IpeLkupMgrOamPI_prIpSa95To48_1_f*/
    {32,  2,  0, "prIpSa95To48_0"                        },  /* IpeLkupMgrOamPI_prIpSa95To48_0_f*/
};

static fields_t PP_FIB_FIFO_field[] = {
    { 1,  0,  9, "fibDefaultEntry"                       },  /* PPFIBFIFO_fibDefaultEntry_f*/
    { 1,  0,  8, "fibHashConflict"                       },  /* PPFIBFIFO_fibHashConflict_f*/
    { 1,  0,  7, "fibResultValid"                        },  /* PPFIBFIFO_fibResultValid_f*/
    { 1,  0,  6, "fibError"                              },  /* PPFIBFIFO_fibError_f*/
    { 6,  0,  0, "fibSeq_1"                              },  /* PPFIBFIFO_fibSeq_1_f*/
    { 2,  1, 30, "fibSeq_0"                              },  /* PPFIBFIFO_fibSeq_0_f*/
    {30,  1,  0, "fibResult_4"                           },  /* PPFIBFIFO_fibResult_4_f*/
    {32,  2,  0, "fibResult_3"                           },  /* PPFIBFIFO_fibResult_3_f*/
    {32,  3,  0, "fibResult_2"                           },  /* PPFIBFIFO_fibResult_2_f*/
    {32,  4,  0, "fibResult_1"                           },  /* PPFIBFIFO_fibResult_1_f*/
    {32,  5,  0, "fibResult_0"                           },  /* PPFIBFIFO_fibResult_0_f*/
};

static fields_t PP_HASH_DS_FIFO_field[] = {
    { 1,  0, 21, "hashDsConflict"                        },  /* PPHASHDSFIFO_hashDsConflict_f*/
    { 1,  0, 20, "hashDsResultValid"                     },  /* PPHASHDSFIFO_hashDsResultValid_f*/
    { 1,  0, 19, "hashDsError"                           },  /* PPHASHDSFIFO_hashDsError_f*/
    { 8,  0, 11, "hashDsSeq"                             },  /* PPHASHDSFIFO_hashDsSeq_f*/
    {11,  0,  0, "hashDsOamInfo_1"                       },  /* PPHASHDSFIFO_hashDsOamInfo_1_f*/
    {17,  1, 15, "hashDsOamInfo_0"                       },  /* PPHASHDSFIFO_hashDsOamInfo_0_f*/
    {15,  1,  0, "hashDsResult_2"                        },  /* PPHASHDSFIFO_hashDsResult_2_f*/
    {32,  2,  0, "hashDsResult_1"                        },  /* PPHASHDSFIFO_hashDsResult_1_f*/
    {32,  3,  0, "hashDsResult_0"                        },  /* PPHASHDSFIFO_hashDsResult_0_f*/
};

static fields_t Ipe_IntfMap_PktProc_PI_field[] = {
    { 1,  8,  6, "piNextHopPtrValid"                     },  /* IpeIntfMapPktProcPI_piNextHopPtrValid_f*/
    { 6,  8,  0, "piAdNextHopPtr_1"                      },  /* IpeIntfMapPktProcPI_piAdNextHopPtr_1_f*/
    {10,  9, 22, "piAdNextHopPtr_0"                      },  /* IpeIntfMapPktProcPI_piAdNextHopPtr_0_f*/
    {22,  9,  0, "piAdDestMap"                           },  /* IpeIntfMapPktProcPI_piAdDestMap_f*/
    { 1, 10, 31, "piAdLengthAdjustType"                  },  /* IpeIntfMapPktProcPI_piAdLengthAdjustType_f*/
    { 1, 10, 30, "piAdCriticalPacket"                    },  /* IpeIntfMapPktProcPI_piAdCriticalPacket_f*/
    { 1, 10, 29, "piAdNextHopExt"                        },  /* IpeIntfMapPktProcPI_piAdNextHopExt_f*/
    { 1, 10, 28, "piAdSendLocalPhyPort"                  },  /* IpeIntfMapPktProcPI_piAdSendLocalPhyPort_f*/
    { 2, 10, 26, "piAdApsType"                           },  /* IpeIntfMapPktProcPI_piAdApsType_f*/
    { 2, 10, 24, "piAdSpeed"                             },  /* IpeIntfMapPktProcPI_piAdSpeed_f*/
    { 1, 10, 23, "piFastLearningEn"                      },  /* IpeIntfMapPktProcPI_piFastLearningEn_f*/
    { 1, 10, 22, "piRpfPermitDefault"                    },  /* IpeIntfMapPktProcPI_piRpfPermitDefault_f*/
    { 1, 10, 21, "piPbbOuterLearningEnable"              },  /* IpeIntfMapPktProcPI_piPbbOuterLearningEnable_f*/
    { 1, 10, 20, "piPbbOuterLearningDisable"             },  /* IpeIntfMapPktProcPI_piPbbOuterLearningDisable_f*/
    { 1, 10, 19, "piNonCrc"                              },  /* IpeIntfMapPktProcPI_piNonCrc_f*/
    { 1, 10, 18, "piSrcDscpValid"                        },  /* IpeIntfMapPktProcPI_piSrcDscpValid_f*/
    { 6, 10, 12, "piSrcDscp"                             },  /* IpeIntfMapPktProcPI_piSrcDscp_f*/
    { 3, 10,  9, "piPacketType"                          },  /* IpeIntfMapPktProcPI_piPacketType_f*/
    { 1, 10,  8, "piWlanTunnelValid"                   },  /* IpeIntfMapPktProcPI_piWlanTunnelValid_f*/
    { 1, 10,  7, "piLogicPortType"                       },  /* IpeIntfMapPktProcPI_piLogicPortType_f*/
    { 2, 10,  5, "piRoamingState"                        },  /* IpeIntfMapPktProcPI_piRoamingState_f*/
    { 1, 10,  4, "piWlanTunnelType"                    },  /* IpeIntfMapPktProcPI_piWlanTunnelType_f*/
    { 1, 10,  3, "piOuterVlanIsCvlan"                    },  /* IpeIntfMapPktProcPI_piOuterVlanIsCvlan_f*/
    { 2, 10,  1, "piSvlanTpidIndex"                      },  /* IpeIntfMapPktProcPI_piSvlanTpidIndex_f*/
    { 1, 10,  0, "piL2SpanEn"                            },  /* IpeIntfMapPktProcPI_piL2SpanEn_f*/
    { 2, 11, 30, "piL2SpanId"                            },  /* IpeIntfMapPktProcPI_piL2SpanId_f*/
    { 1, 11, 29, "piRandomLogEn"                         },  /* IpeIntfMapPktProcPI_piRandomLogEn_f*/
    { 1, 11, 28, "piPortLogEn"                           },  /* IpeIntfMapPktProcPI_piPortLogEn_f*/
    { 1, 11, 27, "piDsFwdPtrValid"                       },  /* IpeIntfMapPktProcPI_piDsFwdPtrValid_f*/
    {16, 11, 11, "piDsFwdPtr"                            },  /* IpeIntfMapPktProcPI_piDsFwdPtr_f*/
    {11, 11,  0, "piDefaultVlanId_1"                     },  /* IpeIntfMapPktProcPI_piDefaultVlanId_1_f*/
    { 1, 12, 31, "piDefaultVlanId_0"                     },  /* IpeIntfMapPktProcPI_piDefaultVlanId_0_f*/
    { 1, 12, 30, "piDenyLearning"                        },  /* IpeIntfMapPktProcPI_piDenyLearning_f*/
    { 1, 12, 29, "piOamTunnelEn"                         },  /* IpeIntfMapPktProcPI_piOamTunnelEn_f*/
    { 1, 12, 28, "piSvlanTagOperationValid"              },  /* IpeIntfMapPktProcPI_piSvlanTagOperationValid_f*/
    { 1, 12, 27, "piCvlanTagOperationValid"              },  /* IpeIntfMapPktProcPI_piCvlanTagOperationValid_f*/
    { 1, 12, 26, "piFlowStats2Valid"                     },  /* IpeIntfMapPktProcPI_piFlowStats2Valid_f*/
    { 1, 12, 25, "piUserPriorityValid"                   },  /* IpeIntfMapPktProcPI_piUserPriorityValid_f*/
    { 1, 12, 24, "piSrcQueueSelect"                      },  /* IpeIntfMapPktProcPI_piSrcQueueSelect_f*/
    { 6, 12, 18, "piUserPriority"                        },  /* IpeIntfMapPktProcPI_piUserPriority_f*/
    { 2, 12, 16, "piUserColor"                           },  /* IpeIntfMapPktProcPI_piUserColor_f*/
    { 2, 12, 14, "piStagAction"                          },  /* IpeIntfMapPktProcPI_piStagAction_f*/
    { 2, 12, 12, "piCtagAction"                          },  /* IpeIntfMapPktProcPI_piCtagAction_f*/
    {12, 12,  0, "piFlowStats2Ptr_1"                     },  /* IpeIntfMapPktProcPI_piFlowStats2Ptr_1_f*/
    { 4, 13, 28, "piFlowStats2Ptr_0"                     },  /* IpeIntfMapPktProcPI_piFlowStats2Ptr_0_f*/
    { 1, 13, 27, "piApsSelectValid0"                     },  /* IpeIntfMapPktProcPI_piApsSelectValid0_f*/
    {11, 13, 16, "piApsSelectGroupId0"                   },  /* IpeIntfMapPktProcPI_piApsSelectGroupId0_f*/
    { 1, 13, 15, "piIgmpSnoopEn"                         },  /* IpeIntfMapPktProcPI_piIgmpSnoopEn_f*/
    { 1, 13, 14, "piMacSecurityVsiDiscard"               },  /* IpeIntfMapPktProcPI_piMacSecurityVsiDiscard_f*/
    { 1, 13, 13, "piServicePolicerValid"                 },  /* IpeIntfMapPktProcPI_piServicePolicerValid_f*/
    { 1, 13, 12, "piServicePolicerMode"                  },  /* IpeIntfMapPktProcPI_piServicePolicerMode_f*/
    {12, 13,  0, "piServicePolicerPtr_1"                 },  /* IpeIntfMapPktProcPI_piServicePolicerPtr_1_f*/
    { 1, 14, 31, "piServicePolicerPtr_0"                 },  /* IpeIntfMapPktProcPI_piServicePolicerPtr_0_f*/
    { 1, 14, 30, "piFlowPolicerValid"                    },  /* IpeIntfMapPktProcPI_piFlowPolicerValid_f*/
    {13, 14, 17, "piFlowPolicerPtr"                      },  /* IpeIntfMapPktProcPI_piFlowPolicerPtr_f*/
    { 1, 14, 16, "piIsLeaf"                              },  /* IpeIntfMapPktProcPI_piIsLeaf_f*/
    { 1, 14, 15, "piMacSecurityDiscard"                  },  /* IpeIntfMapPktProcPI_piMacSecurityDiscard_f*/
    { 1, 14, 14, "piPortPolicerValid"                    },  /* IpeIntfMapPktProcPI_piPortPolicerValid_f*/
    { 3, 14, 11, "piQosPolicy"                           },  /* IpeIntfMapPktProcPI_piQosPolicy_f*/
    { 2, 14,  9, "piIpgIndex"                            },  /* IpeIntfMapPktProcPI_piIpgIndex_f*/
    { 3, 14,  6, "piQosDomain"                           },  /* IpeIntfMapPktProcPI_piQosDomain_f*/
    { 1, 14,  5, "piPortSecurityEn"                      },  /* IpeIntfMapPktProcPI_piPortSecurityEn_f*/
    { 1, 14,  4, "piPortCheckEn"                         },  /* IpeIntfMapPktProcPI_piPortCheckEn_f*/
    { 2, 14,  2, "piSpeed"                               },  /* IpeIntfMapPktProcPI_piSpeed_f*/
    { 2, 14,  0, "piPortStormCtlPtr_1"                   },  /* IpeIntfMapPktProcPI_piPortStormCtlPtr_1_f*/
    {32, 15,  0, "piChop1Rsvd_0"                         },  /* IpeIntfMapPktProcPI_piChop1Rsvd_0_f*/
    { 4,  0,  4, "piPortStormCtlPtr_0"                   },  /* IpeIntfMapPktProcPI_piPortStormCtlPtr_0_f*/
    { 1,  0,  3, "piPortStormCtlEn"                      },  /* IpeIntfMapPktProcPI_piPortStormCtlEn_f*/
    { 1,  0,  2, "piMcastMacAddress"                     },  /* IpeIntfMapPktProcPI_piMcastMacAddress_f*/
    { 1,  0,  1, "piMcastDiscard"                        },  /* IpeIntfMapPktProcPI_piMcastDiscard_f*/
    { 1,  0,  0, "piUcastDiscard"                        },  /* IpeIntfMapPktProcPI_piUcastDiscard_f*/
    { 1,  1, 31, "piIsEsadi"                             },  /* IpeIntfMapPktProcPI_piIsEsadi_f*/
    { 3,  1, 28, "piCtagCos"                             },  /* IpeIntfMapPktProcPI_piCtagCos_f*/
    { 1,  1, 27, "piCtagCfi"                             },  /* IpeIntfMapPktProcPI_piCtagCfi_f*/
    { 1,  1, 26, "piSvlanIdValid"                        },  /* IpeIntfMapPktProcPI_piSvlanIdValid_f*/
    { 1,  1, 25, "piCvlanIdValid"                        },  /* IpeIntfMapPktProcPI_piCvlanIdValid_f*/
    { 1,  1, 24, "piSrcCtagOffsetType"                   },  /* IpeIntfMapPktProcPI_piSrcCtagOffsetType_f*/
    { 1,  1, 23, "piMacSecurityVlanDiscard"              },  /* IpeIntfMapPktProcPI_piMacSecurityVlanDiscard_f*/
    { 6,  1, 17, "piVlanStormCtlPtr"                     },  /* IpeIntfMapPktProcPI_piVlanStormCtlPtr_f*/
    { 1,  1, 16, "piVlanStormCtlEn"                      },  /* IpeIntfMapPktProcPI_piVlanStormCtlEn_f*/
    { 1,  1, 15, "piL3SpanEn"                            },  /* IpeIntfMapPktProcPI_piL3SpanEn_f*/
    { 2,  1, 13, "piL3SpanId"                            },  /* IpeIntfMapPktProcPI_piL3SpanId_f*/
    { 1,  1, 12, "piRpfType"                             },  /* IpeIntfMapPktProcPI_piRpfType_f*/
    { 1,  1, 11, "piIsidTranslateEn"                     },  /* IpeIntfMapPktProcPI_piIsidTranslateEn_f*/
    {11,  1,  0, "piNewIsid_1"                           },  /* IpeIntfMapPktProcPI_piNewIsid_1_f*/
    {13,  2, 19, "piNewIsid_0"                           },  /* IpeIntfMapPktProcPI_piNewIsid_0_f*/
    { 4,  2, 15, "piFatalException"                      },  /* IpeIntfMapPktProcPI_piFatalException_f*/
    { 1,  2, 14, "piFlowStats1Valid"                     },  /* IpeIntfMapPktProcPI_piFlowStats1Valid_f*/
    {14,  2,  0, "piFlowStats1Ptr_1"                     },  /* IpeIntfMapPktProcPI_piFlowStats1Ptr_1_f*/
    { 2,  3, 30, "piFlowStats1Ptr_0"                     },  /* IpeIntfMapPktProcPI_piFlowStats1Ptr_0_f*/
    { 1,  3, 29, "piApsSelectValid1"                     },  /* IpeIntfMapPktProcPI_piApsSelectValid1_f*/
    {11,  3, 18, "piApsSelectGroupId1"                   },  /* IpeIntfMapPktProcPI_piApsSelectGroupId1_f*/
    { 1,  3, 17, "piApsSelectValid2"                     },  /* IpeIntfMapPktProcPI_piApsSelectValid2_f*/
    {11,  3,  6, "piApsSelectGroupId2"                   },  /* IpeIntfMapPktProcPI_piApsSelectGroupId2_f*/
    { 1,  3,  5, "piMplsOverwritePriority"               },  /* IpeIntfMapPktProcPI_piMplsOverwritePriority_f*/
    { 2,  3,  3, "piColor"                               },  /* IpeIntfMapPktProcPI_piColor_f*/
    { 3,  3,  0, "piPriority_1"                          },  /* IpeIntfMapPktProcPI_piPriority_1_f*/
    { 3,  4, 29, "piPriority_0"                          },  /* IpeIntfMapPktProcPI_piPriority_0_f*/
    { 1,  4, 28, "piApsSelectProtectingPath0"            },  /* IpeIntfMapPktProcPI_piApsSelectProtectingPath0_f*/
    { 1,  4, 27, "piApsSelectProtectingPath1"            },  /* IpeIntfMapPktProcPI_piApsSelectProtectingPath1_f*/
    { 1,  4, 26, "piApsSelectProtectingPath2"            },  /* IpeIntfMapPktProcPI_piApsSelectProtectingPath2_f*/
    { 1,  4, 25, "piPbbCheckDiscard"                     },  /* IpeIntfMapPktProcPI_piPbbCheckDiscard_f*/
    { 6,  4, 19, "piSourcePortIsolateId"                 },  /* IpeIntfMapPktProcPI_piSourcePortIsolateId_f*/
    { 8,  4, 11, "piEcmpHash"                            },  /* IpeIntfMapPktProcPI_piEcmpHash_f*/
    { 2,  4,  9, "piPtpExtraOffset"                      },  /* IpeIntfMapPktProcPI_piPtpExtraOffset_f*/
    { 1,  4,  8, "piPortSecurityExceptionEn"             },  /* IpeIntfMapPktProcPI_piPortSecurityExceptionEn_f*/
    { 8,  4,  0, "prOuterMacSa_2"                        },  /* IpeIntfMapPktProcPI_prOuterMacSa_2_f*/
    {32,  5,  0, "prOuterMacSa_1"                        },  /* IpeIntfMapPktProcPI_prOuterMacSa_1_f*/
    { 8,  6, 24, "prOuterMacSa_0"                        },  /* IpeIntfMapPktProcPI_prOuterMacSa_0_f*/
    {12,  6, 12, "prOuterSvlanId"                        },  /* IpeIntfMapPktProcPI_prOuterSvlanId_f*/
    {12,  6,  0, "prOuterCvlanId"                        },  /* IpeIntfMapPktProcPI_prOuterCvlanId_f*/
    {32,  7,  0, "piChop0Rsvd_0"                         },  /* IpeIntfMapPktProcPI_piChop0Rsvd_0_f*/
};

static fields_t Ipe_IntfMap_PktProc_PI_UserId_field[] = {
    { 1,  0, 26, "piTimeStampValid"                      },  /* IpeIntfMapPktProcPIUserId_piTimeStampValid_f*/
    {26,  0,  0, "piTimeStamp_2"                         },  /* IpeIntfMapPktProcPIUserId_piTimeStamp_2_f*/
    {32,  1,  0, "piTimeStamp_1"                         },  /* IpeIntfMapPktProcPIUserId_piTimeStamp_1_f*/
    { 4,  2, 28, "piTimeStamp_0"                         },  /* IpeIntfMapPktProcPIUserId_piTimeStamp_0_f*/
    { 1,  2, 27, "piSourcePortExtender"                  },  /* IpeIntfMapPktProcPIUserId_piSourcePortExtender_f*/
    { 1,  2, 26, "piTxDmEn"                              },  /* IpeIntfMapPktProcPIUserId_piTxDmEn_f*/
    { 8,  2, 18, "piHeaderHash"                          },  /* IpeIntfMapPktProcPIUserId_piHeaderHash_f*/
    { 6,  2, 12, "piOuterPriority"                       },  /* IpeIntfMapPktProcPIUserId_piOuterPriority_f*/
    { 2,  2, 10, "piOuterColor"                          },  /* IpeIntfMapPktProcPIUserId_piOuterColor_f*/
    { 2,  2,  8, "piMuxLengthType"                       },  /* IpeIntfMapPktProcPIUserId_piMuxLengthType_f*/
    { 8,  2,  0, "piPacketLengthAdjust"                  },  /* IpeIntfMapPktProcPIUserId_piPacketLengthAdjust_f*/
    {16,  3, 16, "piMuxDestination"                      },  /* IpeIntfMapPktProcPIUserId_piMuxDestination_f*/
    { 1,  3, 15, "piMuxDestinationType"                  },  /* IpeIntfMapPktProcPIUserId_piMuxDestinationType_f*/
    { 1,  3, 14, "piMuxDestinationValid"                 },  /* IpeIntfMapPktProcPIUserId_piMuxDestinationValid_f*/
    {14,  3,  0, "piPacketLength"                        },  /* IpeIntfMapPktProcPIUserId_piPacketLength_f*/
};

static fields_t Ipe_LkupMgr_PktProc_PI_field[] = {
    { 8, 16, 20, "piSeq"                                 },  /* IpeLkupMgrPktProcPI_piSeq_f*/
    { 8, 16, 12, "piChannelId"                           },  /* IpeLkupMgrPktProcPI_piChannelId_f*/
    { 7, 16,  5, "piLocalPhyPort"                        },  /* IpeLkupMgrPktProcPI_piLocalPhyPort_f*/
    { 1, 16,  4, "piFromCpuOrOam"                        },  /* IpeLkupMgrPktProcPI_piFromCpuOrOam_f*/
    { 4, 16,  0, "piLogicSrcPort_1"                      },  /* IpeLkupMgrPktProcPI_piLogicSrcPort_1_f*/
    {10, 17, 22, "piLogicSrcPort_0"                      },  /* IpeLkupMgrPktProcPI_piLogicSrcPort_0_f*/
    { 1, 17, 21, "piDiscard"                             },  /* IpeLkupMgrPktProcPI_piDiscard_f*/
    { 6, 17, 15, "piDiscardType"                         },  /* IpeLkupMgrPktProcPI_piDiscardType_f*/
    { 1, 17, 14, "piHardError"                           },  /* IpeLkupMgrPktProcPI_piHardError_f*/
    { 3, 17, 11, "piPbbSrcPortType"                      },  /* IpeLkupMgrPktProcPI_piPbbSrcPortType_f*/
    { 1, 17, 10, "piExceptionEn"                         },  /* IpeLkupMgrPktProcPI_piExceptionEn_f*/
    { 3, 17,  7, "piExceptionIndex"                      },  /* IpeLkupMgrPktProcPI_piExceptionIndex_f*/
    { 6, 17,  1, "piExceptionSubIndex"                   },  /* IpeLkupMgrPktProcPI_piExceptionSubIndex_f*/
    { 1, 17,  0, "piDefaultPcp_1"                        },  /* IpeLkupMgrPktProcPI_piDefaultPcp_1_f*/
    { 2, 18, 30, "piDefaultPcp_0"                        },  /* IpeLkupMgrPktProcPI_piDefaultPcp_0_f*/
    { 1, 18, 29, "piDefaultDei"                          },  /* IpeLkupMgrPktProcPI_piDefaultDei_f*/
    { 3, 18, 26, "piClassifyStagCos"                     },  /* IpeLkupMgrPktProcPI_piClassifyStagCos_f*/
    { 1, 18, 25, "piClassifyStagCfi"                     },  /* IpeLkupMgrPktProcPI_piClassifyStagCfi_f*/
    { 3, 18, 22, "piClassifyCtagCos"                     },  /* IpeLkupMgrPktProcPI_piClassifyCtagCos_f*/
    { 1, 18, 21, "piClassifyCtagCfi"                     },  /* IpeLkupMgrPktProcPI_piClassifyCtagCfi_f*/
    {14, 18,  7, "piGlobalSrcPort"                       },  /* IpeLkupMgrPktProcPI_piGlobalSrcPort_f*/
    { 1, 18,  6, "piDenyBridge"                          },  /* IpeLkupMgrPktProcPI_piDenyBridge_f*/
    { 3, 18,  3, "piPayloadPacketType"                   },  /* IpeLkupMgrPktProcPI_piPayloadPacketType_f*/
    { 3, 18,  0, "piVlanPtr_1"                           },  /* IpeLkupMgrPktProcPI_piVlanPtr_1_f*/
    {10, 19, 22, "piVlanPtr_0"                           },  /* IpeLkupMgrPktProcPI_piVlanPtr_0_f*/
    {14, 19,  8, "piOuterVsiId"                          },  /* IpeLkupMgrPktProcPI_piOuterVsiId_f*/
    { 8, 19,  0, "piInterfaceId_1"                       },  /* IpeLkupMgrPktProcPI_piInterfaceId_1_f*/
    { 2, 20, 30, "piInterfaceId_0"                       },  /* IpeLkupMgrPktProcPI_piInterfaceId_0_f*/
    { 1, 20, 29, "piBcastMacAddress"                     },  /* IpeLkupMgrPktProcPI_piBcastMacAddress_f*/
    {12, 20, 17, "piCvlanId"                             },  /* IpeLkupMgrPktProcPI_piCvlanId_f*/
    { 1, 20, 16, "piSelfAddress"                         },  /* IpeLkupMgrPktProcPI_piSelfAddress_f*/
    { 1, 20, 15, "piL3IfType"                            },  /* IpeLkupMgrPktProcPI_piL3IfType_f*/
    { 1, 20, 14, "piEtherOamDiscard"                     },  /* IpeLkupMgrPktProcPI_piEtherOamDiscard_f*/
    { 2, 20, 12, "piStpState"                            },  /* IpeLkupMgrPktProcPI_piStpState_f*/
    { 1, 20, 11, "piIsMplsSwitched"                      },  /* IpeLkupMgrPktProcPI_piIsMplsSwitched_f*/
    { 1, 20, 10, "piIsDecap"                             },  /* IpeLkupMgrPktProcPI_piIsDecap_f*/
    { 1, 20,  9, "piInnerPacketLookup"                   },  /* IpeLkupMgrPktProcPI_piInnerPacketLookup_f*/
    { 1, 20,  8, "piLinkOrSectionOam"                    },  /* IpeLkupMgrPktProcPI_piLinkOrSectionOam_f*/
    { 8, 20,  0, "piFid_1"                               },  /* IpeLkupMgrPktProcPI_piFid_1_f*/
    { 6, 21, 26, "piFid_0"                               },  /* IpeLkupMgrPktProcPI_piFid_0_f*/
    { 1, 21, 25, "piPtpEn"                               },  /* IpeLkupMgrPktProcPI_piPtpEn_f*/
    { 1, 21, 24, "piForceBridge"                         },  /* IpeLkupMgrPktProcPI_piForceBridge_f*/
    { 1, 21, 23, "piBridgePacket"                        },  /* IpeLkupMgrPktProcPI_piBridgePacket_f*/
    { 1, 21, 22, "piIsatapCheckOk"                       },  /* IpeLkupMgrPktProcPI_piIsatapCheckOk_f*/
    { 1, 21, 21, "piIpHeaderError"                       },  /* IpeLkupMgrPktProcPI_piIpHeaderError_f*/
    { 1, 21, 20, "piIgmpPacket"                          },  /* IpeLkupMgrPktProcPI_piIgmpPacket_f*/
    { 1, 21, 19, "piIpv4McastAddress"                    },  /* IpeLkupMgrPktProcPI_piIpv4McastAddress_f*/
    { 1, 21, 18, "piIpv6McastAddress"                    },  /* IpeLkupMgrPktProcPI_piIpv6McastAddress_f*/
    { 8, 21, 10, "piCMacEcmpHash"                        },  /* IpeLkupMgrPktProcPI_piCMacEcmpHash_f*/
    { 8, 21,  2, "piCMacHeaderHash"                      },  /* IpeLkupMgrPktProcPI_piCMacHeaderHash_f*/
    { 2, 21,  0, "piMacEcmpHash_1"                       },  /* IpeLkupMgrPktProcPI_piMacEcmpHash_1_f*/
    { 6, 22, 26, "piMacEcmpHash_0"                       },  /* IpeLkupMgrPktProcPI_piMacEcmpHash_0_f*/
    { 1, 22, 25, "piEcnEn"                               },  /* IpeLkupMgrPktProcPI_piEcnEn_f*/
    { 1, 22, 24, "piEcnPriorityEn"                       },  /* IpeLkupMgrPktProcPI_piEcnPriorityEn_f*/
    { 1, 22, 23, "piEcnAware"                            },  /* IpeLkupMgrPktProcPI_piEcnAware_f*/
    { 1, 22, 22, "piTrillVersionMatch"                   },  /* IpeLkupMgrPktProcPI_piTrillVersionMatch_f*/
    { 1, 22, 21, "piIpMartianAddress"                    },  /* IpeLkupMgrPktProcPI_piIpMartianAddress_f*/
    { 1, 22, 20, "piIpLinkScopeAddress"                  },  /* IpeLkupMgrPktProcPI_piIpLinkScopeAddress_f*/
    { 1, 22, 19, "piIsIpv4IcmpErrMsg"                    },  /* IpeLkupMgrPktProcPI_piIsIpv4IcmpErrMsg_f*/
    { 1, 22, 18, "piIsIpv6IcmpErrMsg"                    },  /* IpeLkupMgrPktProcPI_piIsIpv6IcmpErrMsg_f*/
    { 1, 22, 17, "piIpv4McastAddressCheckFailure"        },  /* IpeLkupMgrPktProcPI_piIpv4McastAddressCheckFailure_f*/
    { 1, 22, 16, "piIpv6McastAddressCheckFailure"        },  /* IpeLkupMgrPktProcPI_piIpv6McastAddressCheckFailure_f*/
    { 1, 22, 15, "piUnknownGreProtocol"                  },  /* IpeLkupMgrPktProcPI_piUnknownGreProtocol_f*/
    { 3, 22, 12, "piGrePayloadPacketType"                },  /* IpeLkupMgrPktProcPI_piGrePayloadPacketType_f*/
    { 8, 22,  4, "piClassifyTos"                         },  /* IpeLkupMgrPktProcPI_piClassifyTos_f*/
    { 4, 22,  0, "piClassifyLayer3Type"                  },  /* IpeLkupMgrPktProcPI_piClassifyLayer3Type_f*/
    { 3, 23, 29, "piClassifySourceCos"                   },  /* IpeLkupMgrPktProcPI_piClassifySourceCos_f*/
    { 1, 23, 28, "piClassifySourceCfi"                   },  /* IpeLkupMgrPktProcPI_piClassifySourceCfi_f*/
    { 1, 23, 27, "piLayer3Exception"                     },  /* IpeLkupMgrPktProcPI_piLayer3Exception_f*/
    { 6, 23, 21, "piLayer3ExceptionSubIndex"             },  /* IpeLkupMgrPktProcPI_piLayer3ExceptionSubIndex_f*/
    { 8, 23, 13, "piPayloadOffset"                       },  /* IpeLkupMgrPktProcPI_piPayloadOffset_f*/
    { 4, 23,  9, "piRxOamType"                           },  /* IpeLkupMgrPktProcPI_piRxOamType_f*/
    { 9, 23,  0, "piSvlanId_1"                           },  /* IpeLkupMgrPktProcPI_piSvlanId_1_f*/
    { 3, 24, 29, "piSvlanId_0"                           },  /* IpeLkupMgrPktProcPI_piSvlanId_0_f*/
    { 3, 24, 26, "piSourceCos"                           },  /* IpeLkupMgrPktProcPI_piSourceCos_f*/
    { 1, 24, 25, "piSourceCfi"                           },  /* IpeLkupMgrPktProcPI_piSourceCfi_f*/
    { 8, 24, 17, "piPacketTtl"                           },  /* IpeLkupMgrPktProcPI_piPacketTtl_f*/
    {14, 24,  3, "piOuterLearningLogicSrcPort"           },  /* IpeLkupMgrPktProcPI_piOuterLearningLogicSrcPort_f*/
    { 1, 24,  2, "piIsIcmp"                              },  /* IpeLkupMgrPktProcPI_piIsIcmp_f*/
    { 1, 24,  1, "piIsAllRbridgesMac"                    },  /* IpeLkupMgrPktProcPI_piIsAllRbridgesMac_f*/
    { 1, 24,  0, "piFatalExceptionValid"                 },  /* IpeLkupMgrPktProcPI_piFatalExceptionValid_f*/
    {32, 25,  0, "piChop1Rsvd_6"                         },  /* IpeLkupMgrPktProcPI_piChop1Rsvd_6_f*/
    {32, 26,  0, "piChop1Rsvd_5"                         },  /* IpeLkupMgrPktProcPI_piChop1Rsvd_5_f*/
    {32, 27,  0, "piChop1Rsvd_4"                         },  /* IpeLkupMgrPktProcPI_piChop1Rsvd_4_f*/
    {32, 28,  0, "piChop1Rsvd_3"                         },  /* IpeLkupMgrPktProcPI_piChop1Rsvd_3_f*/
    {32, 29,  0, "piChop1Rsvd_2"                         },  /* IpeLkupMgrPktProcPI_piChop1Rsvd_2_f*/
    {32, 30,  0, "piChop1Rsvd_1"                         },  /* IpeLkupMgrPktProcPI_piChop1Rsvd_1_f*/
    {32, 31,  0, "piChop1Rsvd_0"                         },  /* IpeLkupMgrPktProcPI_piChop1Rsvd_0_f*/
    { 8,  0, 21, "prLayer2Offset"                        },  /* IpeLkupMgrPktProcPI_prLayer2Offset_f*/
    { 8,  0, 13, "prLayer3Offset"                        },  /* IpeLkupMgrPktProcPI_prLayer3Offset_f*/
    { 8,  0,  5, "prLayer4Offset"                        },  /* IpeLkupMgrPktProcPI_prLayer4Offset_f*/
    { 1,  0,  4, "prSvlanIdValid"                        },  /* IpeLkupMgrPktProcPI_prSvlanIdValid_f*/
    { 1,  0,  3, "prCvlanIdValid"                        },  /* IpeLkupMgrPktProcPI_prCvlanIdValid_f*/
    { 3,  0,  0, "prEtherOamLevel"                       },  /* IpeLkupMgrPktProcPI_prEtherOamLevel_f*/
    { 6,  1, 26, "prTos7To2"                             },  /* IpeLkupMgrPktProcPI_prTos7To2_f*/
    { 1,  1, 25, "prIsIsatapInterface"                   },  /* IpeLkupMgrPktProcPI_prIsIsatapInterface_f*/
    { 8,  1, 17, "prIpEcmpHash"                          },  /* IpeLkupMgrPktProcPI_prIpEcmpHash_f*/
    { 8,  1,  9, "prMacEcmpHash"                         },  /* IpeLkupMgrPktProcPI_prMacEcmpHash_f*/
    { 3,  1,  6, "prStagCos"                             },  /* IpeLkupMgrPktProcPI_prStagCos_f*/
    { 1,  1,  5, "prStagCfi"                             },  /* IpeLkupMgrPktProcPI_prStagCfi_f*/
    { 3,  1,  2, "prCtagCos"                             },  /* IpeLkupMgrPktProcPI_prCtagCos_f*/
    { 1,  1,  1, "prCtagCfi"                             },  /* IpeLkupMgrPktProcPI_prCtagCfi_f*/
    { 1,  1,  0, "prLayer3Type_1"                        },  /* IpeLkupMgrPktProcPI_prLayer3Type_1_f*/
    { 3,  2, 29, "prLayer3Type_0"                        },  /* IpeLkupMgrPktProcPI_prLayer3Type_0_f*/
    { 1,  2, 28, "prMacDaBit40"                          },  /* IpeLkupMgrPktProcPI_prMacDaBit40_f*/
    {28,  2,  0, "prMacSa_1"                             },  /* IpeLkupMgrPktProcPI_prMacSa_1_f*/
    {20,  3, 12, "prMacSa_0"                             },  /* IpeLkupMgrPktProcPI_prMacSa_0_f*/
    { 1,  3, 11, "prIpOptions"                           },  /* IpeLkupMgrPktProcPI_prIpOptions_f*/
    {11,  3,  0, "prCvlanId_1"                           },  /* IpeLkupMgrPktProcPI_prCvlanId_1_f*/
    { 1,  4, 31, "prCvlanId_0"                           },  /* IpeLkupMgrPktProcPI_prCvlanId_0_f*/
    {12,  4, 19, "prSvlanId"                             },  /* IpeLkupMgrPktProcPI_prSvlanId_f*/
    { 1,  4, 18, "prCnTagValid"                          },  /* IpeLkupMgrPktProcPI_prCnTagValid_f*/
    {16,  4,  2, "prCnFlowId"                            },  /* IpeLkupMgrPktProcPI_prCnFlowId_f*/
    { 1,  4,  1, "piAppDataValid"                        },  /* IpeLkupMgrPktProcPI_piAppDataValid_f*/
    { 1,  4,  0, "prAppData_1"                           },  /* IpeLkupMgrPktProcPI_prAppData_1_f*/
    {31,  5,  1, "prAppData_0"                           },  /* IpeLkupMgrPktProcPI_prAppData_0_f*/
    { 1,  5,  0, "prIpSa47To0_2"                         },  /* IpeLkupMgrPktProcPI_prIpSa47To0_2_f*/
    {32,  6,  0, "prIpSa47To0_1"                         },  /* IpeLkupMgrPktProcPI_prIpSa47To0_1_f*/
    {15,  7, 17, "prIpSa47To0_0"                         },  /* IpeLkupMgrPktProcPI_prIpSa47To0_0_f*/
    { 4,  7, 13, "prGreOptions"                          },  /* IpeLkupMgrPktProcPI_prGreOptions_f*/
    { 1,  7, 12, "prIsEsadi"                             },  /* IpeLkupMgrPktProcPI_prIsEsadi_f*/
    { 1,  7, 11, "prIsTrillChannel"                      },  /* IpeLkupMgrPktProcPI_prIsTrillChannel_f*/
    { 2,  7,  9, "prPtpVersion"                          },  /* IpeLkupMgrPktProcPI_prPtpVersion_f*/
    { 2,  7,  7, "prUdpPtpVersion"                       },  /* IpeLkupMgrPktProcPI_prUdpPtpVersion_f*/
    { 4,  7,  3, "prPtpMessageType"                      },  /* IpeLkupMgrPktProcPI_prPtpMessageType_f*/
    { 3,  7,  0, "prUdpPtpMessageType_1"                 },  /* IpeLkupMgrPktProcPI_prUdpPtpMessageType_1_f*/
    { 1,  8, 31, "prUdpPtpMessageType_0"                 },  /* IpeLkupMgrPktProcPI_prUdpPtpMessageType_0_f*/
    { 1,  8, 30, "piAclEn0"                              },  /* IpeLkupMgrPktProcPI_piAclEn0_f*/
    { 1,  8, 29, "piAclEn1"                              },  /* IpeLkupMgrPktProcPI_piAclEn1_f*/
    { 1,  8, 28, "piAclEn2"                              },  /* IpeLkupMgrPktProcPI_piAclEn2_f*/
    { 1,  8, 27, "piAclEn3"                              },  /* IpeLkupMgrPktProcPI_piAclEn3_f*/
    { 1,  8, 26, "piAclHashLookupEn"                     },  /* IpeLkupMgrPktProcPI_piAclHashLookupEn_f*/
    { 2,  8, 24, "piLayer3DaLookupMode"                  },  /* IpeLkupMgrPktProcPI_piLayer3DaLookupMode_f*/
    { 1,  8, 23, "piIsIpv4Ucast"                         },  /* IpeLkupMgrPktProcPI_piIsIpv4Ucast_f*/
    { 1,  8, 22, "piIsIpv4Mcast"                         },  /* IpeLkupMgrPktProcPI_piIsIpv4Mcast_f*/
    { 1,  8, 21, "piIsIpv6Ucast"                         },  /* IpeLkupMgrPktProcPI_piIsIpv6Ucast_f*/
    { 1,  8, 20, "piIsIpv6Mcast"                         },  /* IpeLkupMgrPktProcPI_piIsIpv6Mcast_f*/
    { 1,  8, 19, "piIsFcoe"                              },  /* IpeLkupMgrPktProcPI_piIsFcoe_f*/
    { 1,  8, 18, "piIsTrillUcast"                        },  /* IpeLkupMgrPktProcPI_piIsTrillUcast_f*/
    { 1,  8, 17, "piIsTrillMcast"                        },  /* IpeLkupMgrPktProcPI_piIsTrillMcast_f*/
    { 1,  8, 16, "piIsIpv4UcastRpf"                      },  /* IpeLkupMgrPktProcPI_piIsIpv4UcastRpf_f*/
    { 1,  8, 15, "piIsIpv6UcastRpf"                      },  /* IpeLkupMgrPktProcPI_piIsIpv6UcastRpf_f*/
    { 1,  8, 14, "piIsIpv4UcastNat"                      },  /* IpeLkupMgrPktProcPI_piIsIpv4UcastNat_f*/
    { 1,  8, 13, "piIsIpv6UcastNat"                      },  /* IpeLkupMgrPktProcPI_piIsIpv6UcastNat_f*/
    { 1,  8, 12, "piIsIpv4Pbr"                           },  /* IpeLkupMgrPktProcPI_piIsIpv4Pbr_f*/
    { 1,  8, 11, "piIsIpv6Pbr"                           },  /* IpeLkupMgrPktProcPI_piIsIpv6Pbr_f*/
    { 1,  8, 10, "piIsFcoeRpf"                           },  /* IpeLkupMgrPktProcPI_piIsFcoeRpf_f*/
    { 2,  8,  8, "piLayer3SaLookupMode"                  },  /* IpeLkupMgrPktProcPI_piLayer3SaLookupMode_f*/
    { 1,  8,  7, "piLmLookupEn0"                         },  /* IpeLkupMgrPktProcPI_piLmLookupEn0_f*/
    { 1,  8,  6, "piMacIpLookupEn"                       },  /* IpeLkupMgrPktProcPI_piMacIpLookupEn_f*/
    { 1,  8,  5, "piMacDaLookupEn"                       },  /* IpeLkupMgrPktProcPI_piMacDaLookupEn_f*/
    { 1,  8,  4, "piMacSaLookupEn"                       },  /* IpeLkupMgrPktProcPI_piMacSaLookupEn_f*/
    { 1,  8,  3, "piMacTcamLookupEn"                     },  /* IpeLkupMgrPktProcPI_piMacTcamLookupEn_f*/
    { 1,  8,  2, "piMacHashLookupEn"                     },  /* IpeLkupMgrPktProcPI_piMacHashLookupEn_f*/
    { 1,  8,  1, "piOamLookupEn"                         },  /* IpeLkupMgrPktProcPI_piOamLookupEn_f*/
    { 1,  8,  0, "piOuterMacSaLookupEn"                  },  /* IpeLkupMgrPktProcPI_piOuterMacSaLookupEn_f*/
    {32,  9,  0, "piChop0Rsvd_6"                         },  /* IpeLkupMgrPktProcPI_piChop0Rsvd_6_f*/
    {32, 10,  0, "piChop0Rsvd_5"                         },  /* IpeLkupMgrPktProcPI_piChop0Rsvd_5_f*/
    {32, 11,  0, "piChop0Rsvd_4"                         },  /* IpeLkupMgrPktProcPI_piChop0Rsvd_4_f*/
    {32, 12,  0, "piChop0Rsvd_3"                         },  /* IpeLkupMgrPktProcPI_piChop0Rsvd_3_f*/
    {32, 13,  0, "piChop0Rsvd_2"                         },  /* IpeLkupMgrPktProcPI_piChop0Rsvd_2_f*/
    {32, 14,  0, "piChop0Rsvd_1"                         },  /* IpeLkupMgrPktProcPI_piChop0Rsvd_1_f*/
    {32, 15,  0, "piChop0Rsvd_0"                         },  /* IpeLkupMgrPktProcPI_piChop0Rsvd_0_f*/
};

static fields_t PP_TCAM_DS_FIFO0_field[] = {
    { 1,  0,  7, "tcamDs0ResultValid"                    },  /* PPTCAMDSFIFO0_tcamDs0ResultValid_f*/
    { 1,  0,  6, "tcamDs0Error"                          },  /* PPTCAMDSFIFO0_tcamDs0Error_f*/
    { 6,  0,  0, "tcamDs0Seq_1"                          },  /* PPTCAMDSFIFO0_tcamDs0Seq_1_f*/
    { 2,  1, 30, "tcamDs0Seq_0"                          },  /* PPTCAMDSFIFO0_tcamDs0Seq_0_f*/
    {30,  1,  0, "tcamDs0Result_4"                       },  /* PPTCAMDSFIFO0_tcamDs0Result_4_f*/
    {32,  2,  0, "tcamDs0Result_3"                       },  /* PPTCAMDSFIFO0_tcamDs0Result_3_f*/
    {32,  3,  0, "tcamDs0Result_2"                       },  /* PPTCAMDSFIFO0_tcamDs0Result_2_f*/
    {32,  4,  0, "tcamDs0Result_1"                       },  /* PPTCAMDSFIFO0_tcamDs0Result_1_f*/
    {32,  5,  0, "tcamDs0Result_0"                       },  /* PPTCAMDSFIFO0_tcamDs0Result_0_f*/
};

static fields_t PP_TCAM_DS_FIFO1_field[] = {
    { 1,  0, 31, "tcamDs1ResultValid"                    },  /* PPTCAMDSFIFO1_tcamDs1ResultValid_f*/
    { 1,  0, 30, "tcamDs1Error"                          },  /* PPTCAMDSFIFO1_tcamDs1Error_f*/
    {30,  0,  0, "tcamDs1Result_4"                       },  /* PPTCAMDSFIFO1_tcamDs1Result_4_f*/
    {32,  1,  0, "tcamDs1Result_3"                       },  /* PPTCAMDSFIFO1_tcamDs1Result_3_f*/
    {32,  2,  0, "tcamDs1Result_2"                       },  /* PPTCAMDSFIFO1_tcamDs1Result_2_f*/
    {32,  3,  0, "tcamDs1Result_1"                       },  /* PPTCAMDSFIFO1_tcamDs1Result_1_f*/
    {32,  4,  0, "tcamDs1Result_0"                       },  /* PPTCAMDSFIFO1_tcamDs1Result_0_f*/
};

static fields_t PP_TCAM_DS_FIFO2_field[] = {
    { 1,  0, 31, "tcamDs2ResultValid"                    },  /* PPTCAMDSFIFO2_tcamDs2ResultValid_f*/
    { 1,  0, 30, "tcamDs2Error"                          },  /* PPTCAMDSFIFO2_tcamDs2Error_f*/
    {30,  0,  0, "tcamDs2Result_4"                       },  /* PPTCAMDSFIFO2_tcamDs2Result_4_f*/
    {32,  1,  0, "tcamDs2Result_3"                       },  /* PPTCAMDSFIFO2_tcamDs2Result_3_f*/
    {32,  2,  0, "tcamDs2Result_2"                       },  /* PPTCAMDSFIFO2_tcamDs2Result_2_f*/
    {32,  3,  0, "tcamDs2Result_1"                       },  /* PPTCAMDSFIFO2_tcamDs2Result_1_f*/
    {32,  4,  0, "tcamDs2Result_0"                       },  /* PPTCAMDSFIFO2_tcamDs2Result_0_f*/
};

static fields_t PP_TCAM_DS_FIFO3_field[] = {
    { 1,  0, 31, "tcamDs3ResultValid"                    },  /* PPTCAMDSFIFO3_tcamDs3ResultValid_f*/
    { 1,  0, 30, "tcamDs3Error"                          },  /* PPTCAMDSFIFO3_tcamDs3Error_f*/
    {30,  0,  0, "tcamDs3Result_4"                       },  /* PPTCAMDSFIFO3_tcamDs3Result_4_f*/
    {32,  1,  0, "tcamDs3Result_3"                       },  /* PPTCAMDSFIFO3_tcamDs3Result_3_f*/
    {32,  2,  0, "tcamDs3Result_2"                       },  /* PPTCAMDSFIFO3_tcamDs3Result_2_f*/
    {32,  3,  0, "tcamDs3Result_1"                       },  /* PPTCAMDSFIFO3_tcamDs3Result_1_f*/
    {32,  4,  0, "tcamDs3Result_0"                       },  /* PPTCAMDSFIFO3_tcamDs3Result_0_f*/
};

static fields_t Fwd_BufRetrv_RcdUpdate_Bus_field[] = {
    { 1,  0, 29, "noBufError"                            },  /* FwdBufRetrvRcdUpdateBus_noBufError_f*/
    { 1,  0, 28, "c2cPacket"                             },  /* FwdBufRetrvRcdUpdateBus_c2cPacket_f*/
    { 1,  0, 27, "criticalPacket"                        },  /* FwdBufRetrvRcdUpdateBus_criticalPacket_f*/
    { 2,  0, 25, "color"                                 },  /* FwdBufRetrvRcdUpdateBus_color_f*/
    { 6,  0, 19, "priority"                              },  /* FwdBufRetrvRcdUpdateBus_priority_f*/
    {14,  0,  5, "headBufferPtr"                         },  /* FwdBufRetrvRcdUpdateBus_headBufferPtr_f*/
    { 5,  0,  0, "tailBufferPtr_1"                       },  /* FwdBufRetrvRcdUpdateBus_tailBufferPtr_1_f*/
    { 9,  1, 23, "tailBufferPtr_0"                       },  /* FwdBufRetrvRcdUpdateBus_tailBufferPtr_0_f*/
    { 6,  1, 17, "bufferCount"                           },  /* FwdBufRetrvRcdUpdateBus_bufferCount_f*/
    { 9,  1,  8, "resourceGroupId"                       },  /* FwdBufRetrvRcdUpdateBus_resourceGroupId_f*/
    { 1,  1,  7, "mcastRcd"                              },  /* FwdBufRetrvRcdUpdateBus_mcastRcd_f*/
    { 7,  1,  0, "rcd"                                   },  /* FwdBufRetrvRcdUpdateBus_rcd_f*/
};

static fields_t Fwd_MetFifo_MsMetFifo_Bus_field[] = {
    { 3,  0, 16, "msgType"                               },  /* FwdMetFifoMsMetFifoBus_msgType_f*/
    { 6,  0, 10, "bufferCount"                           },  /* FwdMetFifoMsMetFifoBus_bufferCount_f*/
    { 2,  0,  8, "headBufOffset"                         },  /* FwdMetFifoMsMetFifoBus_headBufOffset_f*/
    { 8,  0,  0, "headBufferPtr_1"                       },  /* FwdMetFifoMsMetFifoBus_headBufferPtr_1_f*/
    { 6,  1, 26, "headBufferPtr_0"                       },  /* FwdMetFifoMsMetFifoBus_headBufferPtr_0_f*/
    { 2,  1, 24, "metFifoSelect"                         },  /* FwdMetFifoMsMetFifoBus_metFifoSelect_f*/
    { 9,  1, 15, "resourceGroupId"                       },  /* FwdMetFifoMsMetFifoBus_resourceGroupId_f*/
    {14,  1,  1, "tailBufferPtr"                         },  /* FwdMetFifoMsMetFifoBus_tailBufferPtr_f*/
    { 1,  1,  0, "mcastRcd"                              },  /* FwdMetFifoMsMetFifoBus_mcastRcd_f*/
    {14,  2, 18, "packetLength"                          },  /* FwdMetFifoMsMetFifoBus_packetLength_f*/
    { 1,  2, 17, "logicPortType"                         },  /* FwdMetFifoMsMetFifoBus_logicPortType_f*/
    { 8,  2,  9, "payloadOffset"                         },  /* FwdMetFifoMsMetFifoBus_payloadOffset_f*/
    { 9,  2,  0, "fid_1"                                 },  /* FwdMetFifoMsMetFifoBus_fid_1_f*/
    { 5,  3, 27, "fid_0"                                 },  /* FwdMetFifoMsMetFifoBus_fid_0_f*/
    { 1,  3, 26, "srcQueueSelect"                        },  /* FwdMetFifoMsMetFifoBus_srcQueueSelect_f*/
    { 1,  3, 25, "isLeaf"                                },  /* FwdMetFifoMsMetFifoBus_isLeaf_f*/
    { 1,  3, 24, "fromFabric"                            },  /* FwdMetFifoMsMetFifoBus_fromFabric_f*/
    { 1,  3, 23, "lengthAdjustType"                      },  /* FwdMetFifoMsMetFifoBus_lengthAdjustType_f*/
    { 1,  3, 22, "criticalPacket"                        },  /* FwdMetFifoMsMetFifoBus_criticalPacket_f*/
    { 1,  3, 21, "destIdDiscard"                         },  /* FwdMetFifoMsMetFifoBus_destIdDiscard_f*/
    { 1,  3, 20, "localSwitching"                        },  /* FwdMetFifoMsMetFifoBus_localSwitching_f*/
    { 1,  3, 19, "egressException"                       },  /* FwdMetFifoMsMetFifoBus_egressException_f*/
    { 3,  3, 16, "exceptionPacketType"                   },  /* FwdMetFifoMsMetFifoBus_exceptionPacketType_f*/
    { 4,  3, 12, "exceptionSubIndex_5_2"                 },  /* FwdMetFifoMsMetFifoBus_exceptionSubIndex_5_2_f*/
    { 1,  3, 11, "ecnEn"                                 },  /* FwdMetFifoMsMetFifoBus_ecnEn_f*/
    { 1,  3, 10, "rxOam"                                 },  /* FwdMetFifoMsMetFifoBus_rxOam_f*/
    { 1,  3,  9, "nextHopExt"                            },  /* FwdMetFifoMsMetFifoBus_nextHopExt_f*/
    { 1,  3,  8, "ecnAware"                              },  /* FwdMetFifoMsMetFifoBus_ecnAware_f*/
    { 2,  3,  6, "color"                                 },  /* FwdMetFifoMsMetFifoBus_color_f*/
    { 2,  3,  4, "rxOamType_3_2"                         },  /* FwdMetFifoMsMetFifoBus_rxOamType_3_2_f*/
    { 2,  3,  2, "l2SpanId"                              },  /* FwdMetFifoMsMetFifoBus_l2SpanId_f*/
    { 2,  3,  0, "l3SpanId"                              },  /* FwdMetFifoMsMetFifoBus_l3SpanId_f*/
    { 2,  4, 30, "aclLogId3"                             },  /* FwdMetFifoMsMetFifoBus_aclLogId3_f*/
    { 2,  4, 28, "aclLogId2"                             },  /* FwdMetFifoMsMetFifoBus_aclLogId2_f*/
    { 8,  4, 20, "headerHash"                            },  /* FwdMetFifoMsMetFifoBus_headerHash_f*/
    { 2,  4, 18, "aclLogId1"                             },  /* FwdMetFifoMsMetFifoBus_aclLogId1_f*/
    {18,  4,  0, "oldDestMap_1"                          },  /* FwdMetFifoMsMetFifoBus_oldDestMap_1_f*/
    { 4,  5, 28, "oldDestMap_0"                          },  /* FwdMetFifoMsMetFifoBus_oldDestMap_0_f*/
    { 2,  5, 26, "exceptionSubIndex_1_0"                 },  /* FwdMetFifoMsMetFifoBus_exceptionSubIndex_1_0_f*/
    { 6,  5, 20, "priority"                              },  /* FwdMetFifoMsMetFifoBus_priority_f*/
    { 5,  5, 15, "oamDestChipId"                         },  /* FwdMetFifoMsMetFifoBus_oamDestChipId_f*/
    { 3,  5, 12, "operationType"                         },  /* FwdMetFifoMsMetFifoBus_operationType_f*/
    { 2,  5, 10, "rxOamType_1_0"                         },  /* FwdMetFifoMsMetFifoBus_rxOamType_1_0_f*/
    {10,  5,  0, "serviceId_1"                           },  /* FwdMetFifoMsMetFifoBus_serviceId_1_f*/
    { 4,  6, 28, "serviceId_0"                           },  /* FwdMetFifoMsMetFifoBus_serviceId_0_f*/
    {16,  6, 12, "sourcePort"                            },  /* FwdMetFifoMsMetFifoBus_sourcePort_f*/
    { 4,  6,  8, "flowId"                                },  /* FwdMetFifoMsMetFifoBus_flowId_f*/
    { 1,  6,  7, "c2cCheckDisable"                       },  /* FwdMetFifoMsMetFifoBus_c2cCheckDisable_f*/
    { 3,  6,  4, "exceptionNumber"                       },  /* FwdMetFifoMsMetFifoBus_exceptionNumber_f*/
    { 4,  6,  0, "exceptionVector8_1_1"                  },  /* FwdMetFifoMsMetFifoBus_exceptionVector8_1_1_f*/
    { 4,  7, 28, "exceptionVector8_1_0"                  },  /* FwdMetFifoMsMetFifoBus_exceptionVector8_1_0_f*/
    { 1,  7, 27, "exceptionVector0_0"                    },  /* FwdMetFifoMsMetFifoBus_exceptionVector0_0_f*/
    { 1,  7, 26, "exceptionFromSgmac"                    },  /* FwdMetFifoMsMetFifoBus_exceptionFromSgmac_f*/
    { 6,  7, 20, "srcSgmacGroupId"                       },  /* FwdMetFifoMsMetFifoBus_srcSgmacGroupId_f*/
    { 2,  7, 18, "aclLogId0"                             },  /* FwdMetFifoMsMetFifoBus_aclLogId0_f*/
    {18,  7,  0, "nextHopPtr"                            },  /* FwdMetFifoMsMetFifoBus_nextHopPtr_f*/
};

static fields_t Fwd_QMgr_RcdUpdate_Bus_field[] = {
    { 1,  0, 30, "pktReleaseEn"                          },  /* FwdQMgrRcdUpdateBus_pktReleaseEn_f*/
    { 1,  0, 29, "rcdUpdateEn"                           },  /* FwdQMgrRcdUpdateBus_rcdUpdateEn_f*/
    { 1,  0, 28, "c2cPacket"                             },  /* FwdQMgrRcdUpdateBus_c2cPacket_f*/
    { 1,  0, 27, "criticalPacket"                        },  /* FwdQMgrRcdUpdateBus_criticalPacket_f*/
    { 2,  0, 25, "color"                                 },  /* FwdQMgrRcdUpdateBus_color_f*/
    { 6,  0, 19, "priority"                              },  /* FwdQMgrRcdUpdateBus_priority_f*/
    {14,  0,  5, "headBufferPtr"                         },  /* FwdQMgrRcdUpdateBus_headBufferPtr_f*/
    { 5,  0,  0, "tailBufferPtr_1"                       },  /* FwdQMgrRcdUpdateBus_tailBufferPtr_1_f*/
    { 9,  1, 23, "tailBufferPtr_0"                       },  /* FwdQMgrRcdUpdateBus_tailBufferPtr_0_f*/
    { 6,  1, 17, "bufferCount"                           },  /* FwdQMgrRcdUpdateBus_bufferCount_f*/
    { 9,  1,  8, "resourceGroupId"                       },  /* FwdQMgrRcdUpdateBus_resourceGroupId_f*/
    { 1,  1,  7, "mcastRcd"                              },  /* FwdQMgrRcdUpdateBus_mcastRcd_f*/
    { 7,  1,  0, "rcd"                                   },  /* FwdQMgrRcdUpdateBus_rcd_f*/
};

static fields_t Oam_HdrEdit_PI_field[] = {
    { 7,  0,  0, "piInputPacketOffset"                   },  /* OamHdrEditPI_piInputPacketOffset_f*/
    { 7,  1, 25, "piPacketOffset"                        },  /* OamHdrEditPI_piPacketOffset_f*/
    { 5,  1, 20, "piOamType"                             },  /* OamHdrEditPI_piOamType_f*/
    { 1,  1, 19, "piDiscard"                             },  /* OamHdrEditPI_piDiscard_f*/
    { 4,  1, 15, "piRxOamType"                           },  /* OamHdrEditPI_piRxOamType_f*/
    {14,  1,  1, "piSrcVlanPtr"                          },  /* OamHdrEditPI_piSrcVlanPtr_f*/
    { 1,  1,  0, "piIngressSrcPort_1"                    },  /* OamHdrEditPI_piIngressSrcPort_1_f*/
    {13,  2, 19, "piIngressSrcPort_0"                    },  /* OamHdrEditPI_piIngressSrcPort_0_f*/
    { 8,  2, 11, "piLocalPhyPort"                        },  /* OamHdrEditPI_piLocalPhyPort_f*/
    { 1,  2, 10, "piRelayAllToCpu"                       },  /* OamHdrEditPI_piRelayAllToCpu_f*/
    { 1,  2,  9, "piBypassAll"                           },  /* OamHdrEditPI_piBypassAll_f*/
    { 9,  2,  0, "piException_1"                         },  /* OamHdrEditPI_piException_1_f*/
    {23,  3,  9, "piException_0"                         },  /* OamHdrEditPI_piException_0_f*/
    { 9,  3,  0, "piMepIndex_1"                          },  /* OamHdrEditPI_piMepIndex_1_f*/
    { 5,  4, 27, "piMepIndex_0"                          },  /* OamHdrEditPI_piMepIndex_0_f*/
    {14,  4, 13, "piRmepIndex"                           },  /* OamHdrEditPI_piRmepIndex_f*/
    { 1,  4, 12, "piIsMepHit"                            },  /* OamHdrEditPI_piIsMepHit_f*/
    { 1,  4, 11, "piIsL2EthOam"                          },  /* OamHdrEditPI_piIsL2EthOam_f*/
    { 1,  4, 10, "piIsDefectFree"                        },  /* OamHdrEditPI_piIsDefectFree_f*/
    { 1,  4,  9, "piIsUp"                                },  /* OamHdrEditPI_piIsUp_f*/
    { 1,  4,  8, "piEqualDm"                             },  /* OamHdrEditPI_piEqualDm_f*/
    { 1,  4,  7, "piEqualLb"                             },  /* OamHdrEditPI_piEqualLb_f*/
    { 7,  4,  0, "piBridgeMac_2"                         },  /* OamHdrEditPI_piBridgeMac_2_f*/
    {32,  5,  0, "piBridgeMac_1"                         },  /* OamHdrEditPI_piBridgeMac_1_f*/
    { 9,  6, 23, "piBridgeMac_0"                         },  /* OamHdrEditPI_piBridgeMac_0_f*/
    {23,  6,  0, "piPortMac_1"                           },  /* OamHdrEditPI_piPortMac_1_f*/
    {25,  7,  7, "piPortMac_0"                           },  /* OamHdrEditPI_piPortMac_0_f*/
    { 1,  7,  6, "piEqualLm"                             },  /* OamHdrEditPI_piEqualLm_f*/
    { 1,  7,  5, "piOamUseFid"                           },  /* OamHdrEditPI_piOamUseFid_f*/
    { 5,  7,  0, "piNextHopPtr_1"                        },  /* OamHdrEditPI_piNextHopPtr_1_f*/
    {13,  8, 19, "piNextHopPtr_0"                        },  /* OamHdrEditPI_piNextHopPtr_0_f*/
    { 4,  8, 15, "piFlags"                               },  /* OamHdrEditPI_piFlags_f*/
    { 4,  8, 11, "piVersion"                             },  /* OamHdrEditPI_piVersion_f*/
    { 8,  8,  3, "piControlcode"                         },  /* OamHdrEditPI_piControlcode_f*/
    { 3,  8,  0, "piDFlags_1"                            },  /* OamHdrEditPI_piDFlags_1_f*/
    { 1,  9, 31, "piDFlags_0"                            },  /* OamHdrEditPI_piDFlags_0_f*/
    {31,  9,  0, "piTimestamp1_2"                        },  /* OamHdrEditPI_piTimestamp1_2_f*/
    {32, 10,  0, "piTimestamp1_1"                        },  /* OamHdrEditPI_piTimestamp1_1_f*/
    { 1, 11, 31, "piTimestamp1_0"                        },  /* OamHdrEditPI_piTimestamp1_0_f*/
    {31, 11,  0, "piCounter1_2"                          },  /* OamHdrEditPI_piCounter1_2_f*/
    {32, 12,  0, "piCounter1_1"                          },  /* OamHdrEditPI_piCounter1_1_f*/
    { 1, 13, 31, "piCounter1_0"                          },  /* OamHdrEditPI_piCounter1_0_f*/
    { 1, 13, 30, "piIfStatusValid"                       },  /* OamHdrEditPI_piIfStatusValid_f*/
    { 1, 13, 29, "piLmReceivedPacket"                    },  /* OamHdrEditPI_piLmReceivedPacket_f*/
    { 2, 13, 27, "piLabelNum"                            },  /* OamHdrEditPI_piLabelNum_f*/
    { 1, 13, 26, "piShareMacEn"                          },  /* OamHdrEditPI_piShareMacEn_f*/
    { 1, 13, 25, "piMipEn"                               },  /* OamHdrEditPI_piMipEn_f*/
    {20, 13,  5, "piMplsTpEntropyLabel"                  },  /* OamHdrEditPI_piMplsTpEntropyLabel_f*/
    { 4, 13,  1, "piMplsLabelDisable"                    },  /* OamHdrEditPI_piMplsLabelDisable_f*/
    { 1, 13,  0, "piLinkOam"                             },  /* OamHdrEditPI_piLinkOam_f*/
};

static fields_t Oam_RxProc_PI_field[] = {
    { 1,  0, 19, "discard"                               },  /* OamRxProcPI_discard_f*/
    {14,  0,  5, "packetLength"                          },  /* OamRxProcPI_packetLength_f*/
    { 4,  0,  1, "rxOamType"                             },  /* OamRxProcPI_rxOamType_f*/
    { 1,  0,  0, "mipEn"                                 },  /* OamRxProcPI_mipEn_f*/
    { 1,  1, 31, "linkOam"                               },  /* OamRxProcPI_linkOam_f*/
    { 1,  1, 30, "isUp"                                  },  /* OamRxProcPI_isUp_f*/
    { 7,  1, 23, "inputPacketOffset"                     },  /* OamRxProcPI_inputPacketOffset_f*/
    {14,  1,  9, "mepIndex"                              },  /* OamRxProcPI_mepIndex_f*/
    { 9,  1,  0, "globalSrcPort_1"                       },  /* OamRxProcPI_globalSrcPort_1_f*/
    { 5,  2, 27, "globalSrcPort_0"                       },  /* OamRxProcPI_globalSrcPort_0_f*/
    {14,  2, 13, "ingressSrcPort"                        },  /* OamRxProcPI_ingressSrcPort_f*/
    { 8,  2,  5, "localPhyPort"                          },  /* OamRxProcPI_localPhyPort_f*/
    { 1,  2,  4, "bypassAll"                             },  /* OamRxProcPI_bypassAll_f*/
    { 1,  2,  3, "relayAllToCpu"                         },  /* OamRxProcPI_relayAllToCpu_f*/
    { 1,  2,  2, "lmReceivedPacket"                      },  /* OamRxProcPI_lmReceivedPacket_f*/
    { 2,  2,  0, "labelNum"                              },  /* OamRxProcPI_labelNum_f*/
};

static fields_t Oam_RxProc_PR_field[] = {
    { 8,  0,  1, "pktSeq"                                },  /* OamRxProcPR_pktSeq_f*/
    { 1,  0,  0, "tlvOptions"                            },  /* OamRxProcPR_tlvOptions_f*/
    { 3,  1, 29, "mdLvl"                                 },  /* OamRxProcPR_mdLvl_f*/
    {29,  1,  0, "maId_12"                               },  /* OamRxProcPR_maId_12_f*/
    {32,  2,  0, "maId_11"                               },  /* OamRxProcPR_maId_11_f*/
    {32,  3,  0, "maId_10"                               },  /* OamRxProcPR_maId_10_f*/
    {32,  4,  0, "maId_9"                                },  /* OamRxProcPR_maId_9_f*/
    {32,  5,  0, "maId_8"                                },  /* OamRxProcPR_maId_8_f*/
    {32,  6,  0, "maId_7"                                },  /* OamRxProcPR_maId_7_f*/
    {32,  7,  0, "maId_6"                                },  /* OamRxProcPR_maId_6_f*/
    {32,  8,  0, "maId_5"                                },  /* OamRxProcPR_maId_5_f*/
    {32,  9,  0, "maId_4"                                },  /* OamRxProcPR_maId_4_f*/
    {32, 10,  0, "maId_3"                                },  /* OamRxProcPR_maId_3_f*/
    {32, 11,  0, "maId_2"                                },  /* OamRxProcPR_maId_2_f*/
    {32, 12,  0, "maId_1"                                },  /* OamRxProcPR_maId_1_f*/
    { 3, 13, 29, "maId_0"                                },  /* OamRxProcPR_maId_0_f*/
    { 8, 13, 21, "maIdLength"                            },  /* OamRxProcPR_maIdLength_f*/
    { 1, 13, 20, "presentTraffic"                        },  /* OamRxProcPR_presentTraffic_f*/
    {20, 13,  0, "macSa_1"                               },  /* OamRxProcPR_macSa_1_f*/
    {28, 14,  4, "macSa_0"                               },  /* OamRxProcPR_macSa_0_f*/
    { 4, 14,  0, "macDa_2"                               },  /* OamRxProcPR_macDa_2_f*/
    {32, 15,  0, "macDa_1"                               },  /* OamRxProcPR_macDa_1_f*/
    {12, 16, 20, "macDa_0"                               },  /* OamRxProcPR_macDa_0_f*/
    { 1, 16, 19, "rdi"                                   },  /* OamRxProcPR_rdi_f*/
    {19, 16,  0, "ccmSeqNum_1"                           },  /* OamRxProcPR_ccmSeqNum_1_f*/
    {13, 17, 19, "ccmSeqNum_0"                           },  /* OamRxProcPR_ccmSeqNum_0_f*/
    { 1, 17, 18, "portStatusValid"                       },  /* OamRxProcPR_portStatusValid_f*/
    { 1, 17, 17, "ifStatusValid"                         },  /* OamRxProcPR_ifStatusValid_f*/
    { 8, 17,  9, "portStatusValue"                       },  /* OamRxProcPR_portStatusValue_f*/
    { 8, 17,  1, "ifStatusValue"                         },  /* OamRxProcPR_ifStatusValue_f*/
    { 1, 17,  0, "packetOffset_1"                        },  /* OamRxProcPR_packetOffset_1_f*/
    { 6, 18, 26, "packetOffset_0"                        },  /* OamRxProcPR_packetOffset_0_f*/
    { 1, 18, 25, "isL2EthOam"                            },  /* OamRxProcPR_isL2EthOam_f*/
    { 1, 18, 24, "highVersionOam"                        },  /* OamRxProcPR_highVersionOam_f*/
    { 5, 18, 19, "oamType"                               },  /* OamRxProcPR_oamType_f*/
    { 3, 18, 16, "ccmInterval"                           },  /* OamRxProcPR_ccmInterval_f*/
    { 1, 18, 15, "oamPduInvalid"                         },  /* OamRxProcPR_oamPduInvalid_f*/
    { 1, 18, 14, "bfdOamPduInvalid"                      },  /* OamRxProcPR_bfdOamPduInvalid_f*/
    {13, 18,  1, "rmepId"                                },  /* OamRxProcPR_rmepId_f*/
    { 1, 18,  0, "mplslbmPduInvalid"                     },  /* OamRxProcPR_mplslbmPduInvalid_f*/
};

static fields_t BrRtnFifo_field[] = {
    { 6,  0, 24, "chanId"                                },  /* BrRtnFifo_chanId_f*/
    {10,  0, 14, "queueId"                               },  /* BrRtnFifo_queueId_f*/
    {14,  0,  0, "pktLen"                                },  /* BrRtnFifo_pktLen_f*/
};

static fields_t Ms_EnqMsg_FIFO_field[] = {
    { 1,  0, 19, "c2cPacket"                             },  /* MsEnqMsgFIFO_c2cPacket_f*/
    { 2,  0, 17, "color"                                 },  /* MsEnqMsgFIFO_color_f*/
    { 6,  0, 11, "priority"                              },  /* MsEnqMsgFIFO_priority_f*/
    { 1,  0, 10, "enqMsgBypass"                          },  /* MsEnqMsgFIFO_enqMsgBypass_f*/
    { 1,  0,  9, "ecnEn"                                 },  /* MsEnqMsgFIFO_ecnEn_f*/
    { 1,  0,  8, "ecnAware"                              },  /* MsEnqMsgFIFO_ecnAware_f*/
    { 7,  0,  1, "payloadOffset"                         },  /* MsEnqMsgFIFO_payloadOffset_f*/
    { 1,  0,  0, "msgType_1"                             },  /* MsEnqMsgFIFO_msgType_1_f*/
    { 2,  1, 30, "msgType_0"                             },  /* MsEnqMsgFIFO_msgType_0_f*/
    { 1,  1, 29, "enqueueDiscard"                        },  /* MsEnqMsgFIFO_enqueueDiscard_f*/
    { 1,  1, 28, "lastEnqueue"                           },  /* MsEnqMsgFIFO_lastEnqueue_f*/
    { 1,  1, 27, "mcastRcd"                              },  /* MsEnqMsgFIFO_mcastRcd_f*/
    { 1,  1, 26, "nextHopExt"                            },  /* MsEnqMsgFIFO_nextHopExt_f*/
    { 1,  1, 25, "destSelect"                            },  /* MsEnqMsgFIFO_destSelect_f*/
    { 2,  1, 23, "headPtrBankOffset"                     },  /* MsEnqMsgFIFO_headPtrBankOffset_f*/
    { 1,  1, 22, "ptEnable"                              },  /* MsEnqMsgFIFO_ptEnable_f*/
    {14,  1,  8, "headBufferPtr"                         },  /* MsEnqMsgFIFO_headBufferPtr_f*/
    { 8,  1,  0, "tailBufferPtr_1"                       },  /* MsEnqMsgFIFO_tailBufferPtr_1_f*/
    { 6,  2, 26, "tailBufferPtr_0"                       },  /* MsEnqMsgFIFO_tailBufferPtr_0_f*/
    { 9,  2, 17, "resourceGroupId"                       },  /* MsEnqMsgFIFO_resourceGroupId_f*/
    { 6,  2, 11, "bufferCount"                           },  /* MsEnqMsgFIFO_bufferCount_f*/
    { 7,  2,  4, "rcd"                                   },  /* MsEnqMsgFIFO_rcd_f*/
    { 1,  2,  3, "lengthAdjustType"                      },  /* MsEnqMsgFIFO_lengthAdjustType_f*/
    { 1,  2,  2, "criticalPacket"                        },  /* MsEnqMsgFIFO_criticalPacket_f*/
    { 2,  2,  0, "packetLength_1"                        },  /* MsEnqMsgFIFO_packetLength_1_f*/
    {12,  3, 20, "packetLength_0"                        },  /* MsEnqMsgFIFO_packetLength_0_f*/
    {20,  3,  0, "replicationCtl"                        },  /* MsEnqMsgFIFO_replicationCtl_f*/
};

static fields_t Que_Num_Info_FIFO_field[] = {
    {22,  0,  9, "oldDestMap"                            },  /* QueNumInfoFIFO_oldDestMap_f*/
    { 6,  0,  3, "srcSgmacGroupId"                       },  /* QueNumInfoFIFO_srcSgmacGroupId_f*/
    { 3,  0,  0, "destSgmacGroupId_1"                    },  /* QueNumInfoFIFO_destSgmacGroupId_1_f*/
    { 3,  1, 29, "destSgmacGroupId_0"                    },  /* QueNumInfoFIFO_destSgmacGroupId_0_f*/
    { 3,  1, 26, "operationType"                         },  /* QueNumInfoFIFO_operationType_f*/
    { 1,  1, 25, "c2cCheckDisable"                       },  /* QueNumInfoFIFO_c2cCheckDisable_f*/
    { 5,  1, 20, "sourceChipId"                          },  /* QueNumInfoFIFO_sourceChipId_f*/
    { 8,  1, 12, "headerHash"                            },  /* QueNumInfoFIFO_headerHash_f*/
    { 5,  1,  7, "oamDestChipId"                         },  /* QueNumInfoFIFO_oamDestChipId_f*/
    { 1,  1,  6, "replicatedMet"                         },  /* QueNumInfoFIFO_replicatedMet_f*/
    { 1,  1,  5, "fromFabric"                            },  /* QueNumInfoFIFO_fromFabric_f*/
    { 1,  1,  4, "srcQueueSelect"                        },  /* QueNumInfoFIFO_srcQueueSelect_f*/
    { 1,  1,  3, "queueOnChipId"                         },  /* QueNumInfoFIFO_queueOnChipId_f*/
    { 3,  1,  0, "flowId_1"                              },  /* QueNumInfoFIFO_flowId_1_f*/
    { 1,  2, 31, "flowId_0"                              },  /* QueNumInfoFIFO_flowId_0_f*/
    { 4,  2, 27, "rxOamType"                             },  /* QueNumInfoFIFO_rxOamType_f*/
    { 1,  2, 26, "rxOam"                                 },  /* QueNumInfoFIFO_rxOam_f*/
    {22,  2,  4, "destMap"                               },  /* QueNumInfoFIFO_destMap_f*/
    { 4,  2,  0, "priority_1"                            },  /* QueNumInfoFIFO_priority_1_f*/
    { 2,  3, 30, "priority_0"                            },  /* QueNumInfoFIFO_priority_0_f*/
    { 2,  3, 28, "color"                                 },  /* QueNumInfoFIFO_color_f*/
    {14,  3, 14, "serviceId"                             },  /* QueNumInfoFIFO_serviceId_f*/
    {14,  3,  0, "fid"                                   },  /* QueNumInfoFIFO_fid_f*/
};

#if 0
static fields_t QueNumOutFifo_field[] = {
    { 1,  0,  0, "egrResecCtlRdData_1"                   },  /* QueNumOutFifo_egrResecCtlRdData_1_f*/
    {29,  1,  3, "egrResecCtlRdData_0"                   },  /* QueNumOutFifo_egrResecCtlRdData_0_f*/
    { 1,  1,  2, "enqDiscard"                            },  /* QueNumOutFifo_enqDiscard_f*/
    { 2,  1,  0, "queueId_1"                             },  /* QueNumOutFifo_queueId_1_f*/
    { 8,  2, 24, "queueId_0"                             },  /* QueNumOutFifo_queueId_0_f*/
    {22,  2,  2, "destMap"                               },  /* QueNumOutFifo_destMap_f*/
    { 2,  2,  0, "dropProcedence"                        },  /* QueNumOutFifo_dropProcedence_f*/
};
#endif

static fields_t DeqRtnFifo_field[] = {
    { 8,  0, 22, "grpId"                                 },  /* DeqRtnFifo_grpId_f*/
    { 6,  0, 16, "bufCnt"                                },  /* DeqRtnFifo_bufCnt_f*/
    {10,  0,  6, "queueId"                               },  /* DeqRtnFifo_queueId_f*/
    { 6,  0,  0, "chanId"                                },  /* DeqRtnFifo_chanId_f*/
};

dkit_bus_t gb_dkit_bus_list[] = {
    {"ipe",  "learning2fwd",                       2, {{"learning to forward-header",             IpeFwdLearnThroughFifo_t,       17,  12,  sizeof(Ipe_Learn_Fwd_PI1_field) / sizeof(fields_t),               Ipe_Learn_Fwd_PI1_field},              {"learning to forward-fwd",         IpeFwdLearnTrackFifo_t,        8, 16, sizeof(Ipe_Learn_Fwd_PI0_field) / sizeof(fields_t), Ipe_Learn_Fwd_PI0_field}}},
    {"ipe",  "pkt-process2fwd",                    1, {{"packet-process to forward",              IpeFwdRoutePiFifo_t,            24,  32,  sizeof(Ipe_Route_Fwd_PI_field) / sizeof(fields_t),                Ipe_Route_Fwd_PI_field}}},
    {"ipe",  "hdr-adjust2inf-mapper",              1, {{"header-adjust to interface-mapper",      InputPIFifo_t,                  6,   40,  sizeof(Ipe_HdrAdj_IntfMap_PI_field) / sizeof(fields_t),           Ipe_HdrAdj_IntfMap_PI_field}}},
    {"ipe",  "parser2inf-mapper",                  1, {{"parser to interface-mapper",             InputPRFifo_t,                  3,   88,  sizeof(Ipe_Parser_IntfMap_PR_field) / sizeof(fields_t),           Ipe_Parser_IntfMap_PR_field}}},
    {"ipe",  "inf-mapper2lkp-mgr",                  1, {{"interface-mapper to lookup-manager",     IpeLkupMgrPIFifo_t,             4,   128, sizeof(Ipe_IntfMap_LkupMgr_PI_field) / sizeof(fields_t),          Ipe_IntfMap_LkupMgr_PI_field}}},
    {"ipe",  "parser2lkp-mgr",                     1, {{"parser to lookup-manager",               IpeLkupMgrPRFifo_t,             4,   84,  sizeof(Ipe_Parser_LkupMgr_PI_field) / sizeof(fields_t),           Ipe_Parser_LkupMgr_PI_field}}},
    {"ipe",  "pkt-process-ack2oam",                1, {{"packet-process-ack to oam",              IpeOamAckFifo_t,                6,   16,  sizeof(PP_OAM_ACK_FIFO_field) / sizeof(fields_t),                 PP_OAM_ACK_FIFO_field}}},
    {"ipe",  "inf-mapper2oam",                     1, {{"interface-mapper to oam",                IpeOamFrImInfoFifo_t,           48,  20,  sizeof(Ipe_IntfMap_Oam_PI_field) / sizeof(fields_t),              Ipe_IntfMap_Oam_PI_field}}},
    {"ipe",  "lkp-mgr2oam",                        1, {{"lookup-manager to oam",                  IpeOamFrLmInfoFifo_t,           48,  12,  sizeof(Ipe_LkupMgr_Oam_PI_field) / sizeof(fields_t),              Ipe_LkupMgr_Oam_PI_field}}},
    {"ipe",  "inf-mapper2pkt-process",              1, {{"interface-mapper to packet-process",     IpePktProcPiFrImFifo_t,         32,  64,  sizeof(Ipe_IntfMap_PktProc_PI_field) / sizeof(fields_t),          Ipe_IntfMap_PktProc_PI_field}}},
    {"ipe",  "scl2pkt-process",                    1, {{"scl to packet-process",                  IpePktProcPiFrUiFifo_t,         64,  16,  sizeof(Ipe_IntfMap_PktProc_PI_UserId_field) / sizeof(fields_t),   Ipe_IntfMap_PktProc_PI_UserId_field}}},
    {"ipe",  "lkp-mgr2pkt-process",                1, {{"lookup-manager to packet-process",       IpePktProcPktInfoFifo_t,        32,  128, sizeof(Ipe_LkupMgr_PktProc_PI_field) / sizeof(fields_t),          Ipe_LkupMgr_PktProc_PI_field}}},
    {"bsr",  "bufretrieve2pkt-buffer-track",       1, {{"buffer-retrieve to pkt-buffer-track",    BufRetrvPktBufTrackFifo_t,      16,  20,  sizeof(bufRetrvPktBufTrack_field) / sizeof(fields_t),             bufRetrvPktBufTrack_field   }}},
    {"bsr",  "ipe-hdr2metfifo",                    1, {{"ipe-header to met-fifo",                 IpeHdrMsgFifo_t,                32,  32,  sizeof(BufStore_MetFifo_Msg_Ipe_field) / sizeof(fields_t),        BufStore_MetFifo_Msg_Ipe_field}}},
    {"bsr",  "bufstore2metfifo",                   2, {{"no cutthrough or hdr latter than eop",   PktMsgFifo_t,                   64,  32,  sizeof(BufStore_MetFifo_Msg_Pkt_field) / sizeof(fields_t),        BufStore_MetFifo_Msg_Pkt_field},      {"buffer-store to met-fifo",          MetFifoMsgRam_t,                768, 32,  sizeof(Fwd_MetFifo_MsMetFifo_Bus_field) / sizeof(fields_t),       Fwd_MetFifo_MsMetFifo_Bus_field}}},
    {"bsr",  "bufretrieve2metfifo",                2, {{"buffer-retrieve to met-fifo update rcd", MetFifoBrRcdUpdFifo_t,          256, 8,   sizeof(Fwd_BufRetrv_RcdUpdate_Bus_field) / sizeof(fields_t),      Fwd_BufRetrv_RcdUpdate_Bus_field},    {"buffer-retrieve to met-fifo",       QMgrEnqBrRtnFifo_t,             2,   4,   sizeof(BrRtnFifo_field) / sizeof(fields_t),                       BrRtnFifo_field             }}},
    {"bsr",  "qmgr2metfifo",                       1, {{"queue-manager to met-fifo",              MetFifoQMgrRcdUpdFifo_t,        64,  8,   sizeof(Fwd_QMgr_RcdUpdate_Bus_field) / sizeof(fields_t),          Fwd_QMgr_RcdUpdate_Bus_field}}},
    {"bsr",  "enqueue-msg",                        2, {{"enqueue data related msg",               QMgrEnqMsgFifo_t,               16,  16,  sizeof(Ms_EnqMsg_FIFO_field) / sizeof(fields_t),                  Ms_EnqMsg_FIFO_field        },        {"enqueue control related msg",            QMgrEnqQueNumInFifo_t,          16,  16,  sizeof(Que_Num_Info_FIFO_field) / sizeof(fields_t),               Que_Num_Info_FIFO_field     }}},
    {"bsr",  "dequeue-msg2enqueue-msg",            1, {{"dequeue to enqueue msg",                 QMgrEnqSchFifo_t,               6,   4,   sizeof(DeqRtnFifo_field) / sizeof(fields_t),                      DeqRtnFifo_field            }}},
    {"epe",  "hdr-process2acl-qos",                1, {{"header-process to acl-qos",              EpeAclInfoFifo_t,               3,   100, sizeof(Epe_HdrProc_AclQos_PI_field) / sizeof(fields_t),           Epe_HdrProc_AclQos_PI_field }}},
    {"epe",  "tcam-ack2acl",                       1, {{"tcam-ack to acl",                        EpeAclTcamAckFifo_t,            3,   28,  sizeof(Epe_Acl_Tcam_Ack_field) / sizeof(fields_t),                Epe_Acl_Tcam_Ack_field      }}},
    {"epe",  "classfiy2policer",                   1, {{"classification to policer",              EpeClaPolicerTrackFifo_t,       9,   24,  sizeof(Epe_Cla_Policer_field) / sizeof(fields_t),                 Epe_Cla_Policer_field       }}},
    {"epe",  "bufretrieve2hdr-adjust",             1, {{"buffer-retrieve to header-adjust",       EpeHdrAdjBrgHdrFifo_t,          32,  36,  sizeof(Br_EpeHdrAdj_Brg_field) / sizeof(fields_t),                Br_EpeHdrAdj_Brg_field      }}},
    {"epe",  "hdr-process2hdr-edit",               1, {{"header-process to header-edit",          EpeHdrEditPktInfoHdrProcFifo_t, 32,  64,  sizeof(Epe_HdrProc_HdrEdit_PI_field) / sizeof(fields_t),          Epe_HdrProc_HdrEdit_PI_field}}},
    {"epe",  "oam2hdr-edit",                       1, {{"oam to header-edit",                     EpeHdrEditPktInfoOamFifo_t,     3,   36,  sizeof(Epe_Oam_HdrEdit_PI_field) / sizeof(fields_t),              Epe_Oam_HdrEdit_PI_field    }}},
    {"epe",  "hdr-adjust2hdr-process",             1, {{"header-adjust to header-process",        EpeHPHdrAdjPiFifo_t,            32,  12,  sizeof(EPE_HdrAdj_To_HdrProc_field) / sizeof(fields_t),           EPE_HdrAdj_To_HdrProc_field }}},
    {"epe",  "nh-mapper2hdr-process",              1, {{"nexthop-mapper to header-process",       EpeHPNextHopPrFifo_t,           32,  16,  sizeof(EPE_NextHop_To_HdrProc_field) / sizeof(fields_t),          EPE_NextHop_To_HdrProc_field}}},
    {"epe",  "payload-process2layer3-edit",        1, {{"payload-process to layer3-edit",         EpeHPPayloadInfoFifo_t,         5,   156, sizeof(EPE_HdrProc_PP2L3_Bus_field) / sizeof(fields_t),           EPE_HdrProc_PP2L3_Bus_field }}},
    {"epe",  "hdr-adjust2nh-mapper",               1, {{"header-adjust to nexthop-mapper",        NextHopPiFifo_t,                24,  64,  sizeof(Epe_HdrAdj_NextHop_PI_field) / sizeof(fields_t),           Epe_HdrAdj_NextHop_PI_field }}},
    {"epe",  "parser2nh-mapper",                   1, {{"parser to nexthop-mapper",               NextHopPrFifo_t,                24,  128, sizeof(Epe_Parser_NextHop_PR_field) / sizeof(fields_t),           Epe_Parser_NextHop_PR_field }}},
    {"epe",  "hdr-process2oam",                    1, {{"header-process to oam",                  EpeOamHdrProcInfoFifo_t,        24,  64,  sizeof(Epe_HdrProc_Oam_PI_field) / sizeof(fields_t),              Epe_HdrProc_Oam_PI_field    }}},
    {"epe",  "classfiy2oam",                       1, {{"classification to oam",                  EpeOamInfoFifo_t,               3,   24,  sizeof(Epe_Cla_Oam_PI_field) / sizeof(fields_t),                  Epe_Cla_Oam_PI_field        }}},
    {"fib",  "lkpmgr-lkp-key-input",               1, {{"lookup-manager request-fifo",            FibLkupMgrReqFifo_t,            5,   36,  sizeof(Fib_LkupMgr_ReqFifo_field) / sizeof(fields_t),             Fib_LkupMgr_ReqFifo_field   }}},
    {"fib",  "pkt-process-lkp-key-output",         2, {{"fib hash key lookup result",             IpePktProcFibFifo_t,            32,  24,  sizeof(PP_FIB_FIFO_field) / sizeof(fields_t),                     PP_FIB_FIFO_field           },        {"scl hash key lookup result",        IpePktProcHashDsFifo_t,         5,   16,  sizeof(PP_HASH_DS_FIFO_field) / sizeof(fields_t),                 PP_HASH_DS_FIFO_field       }}},
    {"fib",  "fib-ad-output",                      1, {{"fib-hash-ad request-fifo",               FibHashAdReqFifo_t,             64,  8,   sizeof(FibHashAdReqFifo_field) / sizeof(fields_t),                FibHashAdReqFifo_field      }}},
    {"fib",  "track-fifo",                         1, {{"fib",                                    HashDsKey0LkupTrackFifo_t,      8,   28,  sizeof(key0LkupTrackFifo_field) / sizeof(fields_t),               key0LkupTrackFifo_field     },        {"scl",                               FibKeyTrackFifo_t,              64,  4,   sizeof(FibKeyTrackFifo_field) / sizeof(fields_t),                 FibKeyTrackFifo_field       }}},
    {"lpm",  "lkp-mgr-key-input",                  1, {{"lookup-manager request-fifo",            LkupMgrLpmKeyReqFifo_t,         4,   44,  sizeof(LkupMgr_LpmKey_ReqFifo_field) / sizeof(fields_t),          LkupMgr_LpmKey_ReqFifo_field}}},
    {"lpm",  "conflict-tcam-key-input",            1, {{"tcam-key request-fifo",                  LpmTcamKeyReqFifo_t,            4,   24,  sizeof(LpmTcamKeyReqFifo_field) / sizeof(fields_t),               LpmTcamKeyReqFifo_field     }}},
    {"lpm",  "hash-key-output",                    1, {{"hash result-fifo",                       LpmHashResultFifo_t,            12,  8,   sizeof(LpmHashResultFifo_field) / sizeof(fields_t),               LpmHashResultFifo_field     }}},
    {"lpm",  "conflict-tcam-key-output",           1, {{"tcam result-fifo",                       LpmTcamResultFifo_t,            12,  8,   sizeof(LpmTcamResultFifo_field) / sizeof(fields_t),               LpmTcamResultFifo_field     }}},
    {"lpm",  "pipeline-request-fifo",              4, {{"pipeline0 request-fifo",                 LpmPipe0ReqFifo_t,              10,  8,   sizeof(LpmPipe0ReqFifo_field) / sizeof(fields_t),                 LpmPipe0ReqFifo_field       },        {"pipeline1 request-fifo",            LpmPipe1ReqFifo_t,              10,  12,  sizeof(LpmPipe1ReqFifo_field) / sizeof(fields_t),                 LpmPipe1ReqFifo_field       }, {"pipeline2 request-fifo",                LpmPipe2ReqFifo_t,              10,  8,   sizeof(LpmPipe2ReqFifo_field) / sizeof(fields_t),                 LpmPipe2ReqFifo_field       }, {"pipeline3 request-fifo",                LpmPipe3ReqFifo_t,              10,  8,   sizeof(LpmPipe3ReqFifo_field) / sizeof(fields_t),                 LpmPipe3ReqFifo_field       }}},
    {"lpm",  "ad-output",                          1, {{"ad request-fifo",                        LpmAdReqFifo_t,                 64,  8,   sizeof(LpmAdReqFifo_field) / sizeof(fields_t),                    LpmAdReqFifo_field          }}},
    {"lpm",  "track",                              3, {{"lpm track-fifo",                         LpmTrackFifo_t,                 48,  12,  sizeof(LpmTrackFifo_field) / sizeof(fields_t),                    LpmTrackFifo_field          },        {"final track-fifo",                  LpmFinalTrackFifo_t,            15,  8,   sizeof(LpmFinalTrackFifo_field) / sizeof(fields_t),               LpmFinalTrackFifo_field     }, {"hash-key track-fifo",                   LpmHashKeyTrackFifo_t,          13,  24,  sizeof(LpmHashKeyTrackFifo_field) / sizeof(fields_t),             LpmHashKeyTrackFifo_field   }}},
    {"tcam", "fifo",                               4, {{"fifo0",                                  IpePktProcTcamDsFifo0_t,        12,  24,  sizeof(PP_TCAM_DS_FIFO0_field) / sizeof(fields_t),                PP_TCAM_DS_FIFO0_field      },        {"fifo1",                             IpePktProcTcamDsFifo1_t,        4,   20,  sizeof(PP_TCAM_DS_FIFO1_field) / sizeof(fields_t),                PP_TCAM_DS_FIFO1_field      }, {"fifo2",                                 IpePktProcTcamDsFifo2_t,        4,   20,  sizeof(PP_TCAM_DS_FIFO2_field) / sizeof(fields_t),                PP_TCAM_DS_FIFO2_field      }, {"fifo3",                                 IpePktProcTcamDsFifo3_t,        4,   20,  sizeof(PP_TCAM_DS_FIFO3_field) / sizeof(fields_t),                PP_TCAM_DS_FIFO3_field      }}},
    {"oam",  "hdr-edit",                           1, {{"header edit info",                       OamHdrEditPIInFifo_t,           4,   56,  sizeof(Oam_HdrEdit_PI_field) / sizeof(fields_t),                  Oam_HdrEdit_PI_field        }}},
    {"oam",  "rx-proc",                            2, {{"rx proc pi",                             OamRxProcPIInFifo_t,            2,   12,  sizeof(Oam_RxProc_PI_field) / sizeof(fields_t),                   Oam_RxProc_PI_field         },        {"rx proc pr",                        OamRxProcPRInFifo_t,            2,   76,  sizeof(Oam_RxProc_PR_field) / sizeof(fields_t),                   Oam_RxProc_PR_field         }}},

};

#define MODINFO___________________________________________

/* MOD regs*/
/* MOD regs*/
static tbls_id_t dbg_bufretrv_tbl_id_list[]  = {
    BufRetrvBufConfigMem_t,
    BufRetrvBufCreditConfigMem_t,
    BufRetrvBufCreditMem_t,
    BufRetrvBufPtrMem_t,
    BufRetrvBufStatusMem_t,
    BufRetrvCreditConfigMem_t,
    BufRetrvCreditMem_t,
    BufRetrvInterruptFatal_t,
    BufRetrvInterruptNormal_t,
    BufRetrvMsgParkMem_t,
    BufRetrvPktBufTrackFifo_t,
    BufRetrvPktConfigMem_t,
    BufRetrvPktMsgMem_t,
    BufRetrvPktStatusMem_t,
    DsBufRetrvColorMap_t,
    DsBufRetrvExcp_t,
    DsBufRetrvPriorityCtl_t,
    BufRetrvBufCreditConfig_t,
    BufRetrvBufPtrMem_RegRam_RamChkRec_t,
    BufRetrvBufPtrSlotConfig_t,
    BufRetrvBufWeightConfig_t,
    BufRetrvChanIdCfg_t,
    BufRetrvCreditConfig_t,
    BufRetrvCreditModuleEnableConfig_t,
    BufRetrvDrainEnable_t,
    BufRetrvFlowCtlInfoDebug_t,
    BufRetrvInit_t,
    BufRetrvInitDone_t,
    BufRetrvInputDebugStats_t,
    BufRetrvIntfFifoConfigCredit_t,
    BufRetrvIntfMemAddrConfig_t,
    BufRetrvIntfMemAddrDebug_t,
    BufRetrvIntfMemParityRecord_t,
    BufRetrvMiscConfig_t,
    BufRetrvMiscDebugStats_t,
    BufRetrvMsgParkMem_RegRam_RamChkRec_t,
    BufRetrvOutputPktDebugStats_t,
    BufRetrvOverrunPort_t,
    BufRetrvParityEnable_t,
    BufRetrvPktBufModuleConfigCredit_t,
    BufRetrvPktBufReqFifo_FifoAlmostFullThrd_t,
    BufRetrvPktBufTrackFifo_FifoAlmostFullThrd_t,
    BufRetrvPktMsgMem_RegRam_RamChkRec_t,
    BufRetrvPktWeightConfig_t,
    BufRetrvStackingEn_t,
    BufferRetrieveCtl_t,
    DsBufRetrvExcp_RegRam_RamChkRec_t,
};
static tbls_id_t dbg_bufstore_tbl_id_list[]  = {
    BufPtrMem_t,
    BufStoreInterruptFatal_t,
    BufStoreInterruptNormal_t,
    ChanInfoRam_t,
    DsIgrCondDisProfId_t,
    DsIgrPortCnt_t,
    DsIgrPortTcCnt_t,
    DsIgrPortTcMinProfId_t,
    DsIgrPortTcPriMap_t,
    DsIgrPortTcThrdProfId_t,
    DsIgrPortTcThrdProfile_t,
    DsIgrPortThrdProfId_t,
    DsIgrPortThrdProfile_t,
    DsIgrPortToTcProfId_t,
    DsIgrPriToTcMap_t,
    DsSrcSgmacGroup_t,
    IpeHdrErrStatsMem_t,
    IpeHdrMsgFifo_t,
    MetFifoPriorityMapTable_t,
    PktErrStatsMem_t,
    PktMsgFifo_t,
    PktResrcErrStats_t,
    BufStoreChanInfoCtl_t,
    BufStoreCpuMacPauseRecord_t,
    BufStoreCpuMacPauseReqCtl_t,
    BufStoreCreditDebug_t,
    BufStoreCreditThd_t,
    BufStoreEccCtl_t,
    BufStoreEccErrorDebugStats_t,
    BufStoreErrStatsMemCtl_t,
    BufStoreFifoCtl_t,
    BufStoreFreeListCtl_t,
    BufStoreGcnCtl_t,
    BufStoreInputDebugStats_t,
    BufStoreInputFifoArbCtl_t,
    BufStoreIntLkResrcCtl_t,
    BufStoreIntLkStallInfo_t,
    BufStoreInternalStallCtl_t,
    BufStoreLinkListSlot_t,
    BufStoreLinkListTableCtl_t,
    BufStoreMetFifoStallCtl_t,
    BufStoreMiscCtl_t,
    BufStoreMsgDropCtl_t,
    BufStoreNetNormalPauseRecord_t,
    BufStoreNetPfcRecord_t,
    BufStoreNetPfcReqCtl_t,
    BufStoreOobFcCtl_t,
    BufStoreOutputDebugStats_t,
    BufStoreParityFailRecord_t,
    BufStorePbCreditRunOutDebugStats_t,
    BufStoreResrcRamCtl_t,
    BufStoreSgmacCtl_t,
    BufStoreStallDropDebugStats_t,
    BufStoreStatsCtl_t,
    BufStoreTotalResrcInfo_t,
    BufferStoreCtl_t,
    BufferStoreForceLocalCtl_t,
    DsIgrCondDisProfId_RegRam_RamChkRec_t,
    DsIgrPortCnt_RegRam_RamChkRec_t,
    DsIgrPortTcCnt_RegRam_RamChkRec_t,
    DsIgrPortTcMinProfId_RegRam_RamChkRec_t,
    DsIgrPortTcPriMap_RegRam_RamChkRec_t,
    DsIgrPortTcThrdProfId_RegRam_RamChkRec_t,
    DsIgrPortTcThrdProfile_RegRam_RamChkRec_t,
    DsIgrPortThrdProfile_RegRam_RamChkRec_t,
    DsIgrPriToTcMap_RegRam_RamChkRec_t,
    IgrCondDisProfile_t,
    IgrCongestLevelThrd_t,
    IgrGlbDropCondCtl_t,
    IgrPortTcMinProfile_t,
    IgrResrcMgrMiscCtl_t,
    IgrScCnt_t,
    IgrScThrd_t,
    IgrTcCnt_t,
    IgrTcThrd_t,
    NetLocalPhyPortFcRecord_t,
};
static tbls_id_t dbg_cpumac_tbl_id_list[]  = {
    CpuMacInterruptNormal_t,
    CpuMacPriorityMap_t,
    CpuMacCreditCfg_t,
    CpuMacCreditCtl_t,
    CpuMacCtl_t,
    CpuMacDaCfg_t,
    CpuMacDebugStats_t,
    CpuMacInitCtl_t,
    CpuMacMiscCtl_t,
    CpuMacPauseCtl_t,
    CpuMacPcsAnegCfg_t,
    CpuMacPcsAnegStatus_t,
    CpuMacPcsCfg_t,
    CpuMacPcsCodeErrCnt_t,
    CpuMacPcsLinkTimerCtl_t,
    CpuMacPcsStatus_t,
    CpuMacResetCtl_t,
    CpuMacRxCtl_t,
    CpuMacSaCfg_t,
    CpuMacStats_t,
    CpuMacStatsUpdateCtl_t,
    CpuMacTxCtl_t,
    EgressDataFifo_FifoAlmostFullThrd_t,
};
static tbls_id_t dbg_dsandtcam_tbl_id_list[]  = {
    LpmTcamKey_t,
    LpmTcamMask_t,
    TcamKey_t,
    TcamMask_t,
};
static tbls_id_t dbg_dsaging_tbl_id_list[]  = {
    DsAging_t,
    DsAgingInterruptNormal_t,
    DsAgingPtr_t,
    DsAgingDebugStats_t,
    DsAgingDebugStatus_t,
    DsAgingFifoAfullThrd_t,
    DsAgingInit_t,
    DsAgingInitDone_t,
    DsAgingPtrFifoDepth_t,
    IpeAgingCtl_t,
};
static tbls_id_t dbg_dynamicds_tbl_id_list[]  = {
    DynamicDsEdram4W_t,
    DynamicDsEdram8W_t,
    DynamicDsFdbLookupIndexCam_t,
    DynamicDsInterruptFatal_t,
    DynamicDsInterruptNormal_t,
    DynamicDsLmStatsRam0_t,
    DynamicDsLmStatsRam1_t,
    DynamicDsMacKeyReqFifo_t,
    DynamicDsOamLmDram0ReqFifo_t,
    DynamicDsOamLmSram0ReqFifo_t,
    DynamicDsOamLmSram1ReqFifo_t,
    DynamicDsCreditConfig_t,
    DynamicDsCreditUsed_t,
    DynamicDsDebugInfo_t,
    DynamicDsDebugStats_t,
    DynamicDsDsEditIndexCam_t,
    DynamicDsDsMacIpIndexCam_t,
    DynamicDsDsMetIndexCam_t,
    DynamicDsDsMplsIndexCam_t,
    DynamicDsDsNextHopIndexCam_t,
    DynamicDsDsStatsIndexCam_t,
    DynamicDsDsUserIdHashIndexCam_t,
    DynamicDsDsUserIdIndexCam_t,
    DynamicDsEccCtl_t,
    DynamicDsEdramEccErrorRecord_t,
    DynamicDsEdramInitCtl_t,
    DynamicDsEdramSelect_t,
    DynamicDsFdbHashCtl_t,
    DynamicDsFdbHashIndexCam_t,
    DynamicDsFdbKeyResultDebug_t,
    DynamicDsFwdIndexCam_t,
    DynamicDsLmRdReqTrackFifo_FifoAlmostFullThrd_t,
    DynamicDsLmStatsIndexCam_t,
    DynamicDsLmStatsRam0_RegRam_RamChkRec_t,
    DynamicDsLmStatsRam1_RegRam_RamChkRec_t,
    DynamicDsLpmIndexCam0_t,
    DynamicDsLpmIndexCam1_t,
    DynamicDsLpmIndexCam2_t,
    DynamicDsLpmIndexCam3_t,
    DynamicDsLpmIndexCam4_t,
    DynamicDsMacHashIndexCam_t,
    DynamicDsOamIndexCam_t,
    DynamicDsRefInterval_t,
    DynamicDsSramInit_t,
    DynamicDsUserIdBase_t,
    DynamicDsWrrCreditCfg_t,
};
static tbls_id_t dbg_eloop_tbl_id_list[]  = {
    ELoopInterruptFatal_t,
    ELoopInterruptNormal_t,
    ELoopCreditState_t,
    ELoopCtl_t,
    ELoopDebugStats_t,
    ELoopDrainEnable_t,
    ELoopLoopHdrInfo_t,
    ELoopParityEn_t,
    ELoopParityFailRecord_t,
};
static tbls_id_t dbg_epeaclqos_tbl_id_list[]  = {
    EpeAclInfoFifo_t,
    EpeAclQosInterruptFatal_t,
    EpeAclQosInterruptNormal_t,
    EpeAclTcamAckFifo_t,
    EpeAclQosCtl_t,
    EpeAclQosDebugStats_t,
    EpeAclQosDrainCtl_t,
    EpeAclQosLfsr_t,
    EpeAclQosMiscCtl_t,
    ToClaCreditDebug_t,
};
static tbls_id_t dbg_epecla_tbl_id_list[]  = {
    EpeClaEopInfoFifo_t,
    EpeClaInterruptFatal_t,
    EpeClaInterruptNormal_t,
    EpeClaPolicerAckFifo_t,
    EpeClaPolicerTrackFifo_t,
    EpeClaPolicingInfoFifo_t,
    EpeClaPtrRam_t,
    EpeClaCreditCtl_t,
    EpeClaDebugStats_t,
    EpeClaEopInfoFifo_FifoAlmostFullThrd_t,
    EpeClaInitCtl_t,
    EpeClaMiscCtl_t,
    EpeClaPtrRam_RegRam_RamChkRec_t,
    EpeClassificationCtl_t,
    EpeClassificationPhbOffset_t,
    EpeIpgCtl_t,
};
static tbls_id_t dbg_epehdradj_tbl_id_list[]  = {
    EpeHdrAdjBrgHdrFifo_t,
    EpeHdrAdjDataFifo_t,
    EpeHdrAdjInterruptFatal_t,
    EpeHdrAdjInterruptNormal_t,
    EpeHeaderAdjustPhyPortMap_t,
    EpeHdrAdjBrgHdrFifo_FifoAlmostFullThrd_t,
    EpeHdrAdjCreditConfig_t,
    EpeHdrAdjDataFifo_FifoAlmostFullThrd_t,
    EpeHdrAdjDebugCtl_t,
    EpeHdrAdjDebugStats_t,
    EpeHdrAdjDisableCrcChk_t,
    EpeHdrAdjDrainEnable_t,
    EpeHdrAdjInit_t,
    EpeHdrAdjInitDone_t,
    EpeHdrAdjParityEnable_t,
    EpeHdrAdjRcvUnitCntFifo_FifoAlmostFullThrd_t,
    EpeHdrAdjRunningCredit_t,
    EpeHdrAdjStallInfo_t,
    EpeHdrAdjustCtl_t,
    EpeHdrAdjustMiscCtl_t,
    EpeNextHopPtrCam_t,
    EpePortExtenderDownlink_t,
};
static tbls_id_t dbg_epehdredit_tbl_id_list[]  = {
    DsDestPortLoopback_t,
    DsPacketHeaderEditTunnel_t,
    EpeHdrEditDiscardTypeStats_t,
    EpeHdrEditIngressHdrFifo_t,
    EpeHdrEditInterruptFatal_t,
    EpeHdrEditInterruptNormal_t,
    EpeHdrEditNewHdrFifo_t,
    EpeHdrEditOuterHdrDataFifo_t,
    EpeHdrEditPktCtlFifo_t,
    EpeHdrEditPktDataFifo_t,
    EpeHdrEditPktInfoHdrProcFifo_t,
    EpeHdrEditPktInfoOamFifo_t,
    EpeHdrEditPktInfoOuterHdrFifo_t,
    EpeHeaderEditPhyPortMap_t,
    DsPacketHeaderEditTunnel_RegRam_RamChkRec_t,
    EpeHdrEditDataCmptEopFifo_FifoAlmostFullThrd_t,
    EpeHdrEditDataCmptSopFifo_FifoAlmostFullThrd_t,
    EpeHdrEditDebugStats_t,
    EpeHdrEditDrainEnable_t,
    EpeHdrEditEccCtl_t,
    EpeHdrEditExcepInfo_t,
    EpeHdrEditHardDiscardEn_t,
    EpeHdrEditInit_t,
    EpeHdrEditInitDone_t,
    EpeHdrEditL2LoopBackFifo_FifoAlmostFullThrd_t,
    EpeHdrEditNewHdrFifo_FifoAlmostEmptyThrd_t,
    EpeHdrEditNewHdrFifo_FifoAlmostFullThrd_t,
    EpeHdrEditOuterHdrDataFifo_FifoAlmostFullThrd_t,
    EpeHdrEditParityEn_t,
    EpeHdrEditPktCtlFifo_FifoAlmostFullThrd_t,
    EpeHdrEditPktDataFifo_FifoAlmostFullThrd_t,
    EpeHdrEditPktInfoOuterHdrFifo_FifoAlmostFullThrd_t,
    EpeHdrEditReservedCreditCnt_t,
    EpeHdrEditState_t,
    EpeHdrEditToNetTxBrgHdrFifo_FifoAlmostFullThrd_t,
    EpeHeaderEditCtl_t,
    EpeHeaderEditMuxCtl_t,
    EpePktHdrCtl_t,
    EpePriorityToStatsCos_t,
};
static tbls_id_t dbg_epehdrproc_tbl_id_list[]  = {
    DsIpv6NatPrefix_t,
    DsL3TunnelV4IpSa_t,
    DsL3TunnelV6IpSa_t,
    DsPortLinkAgg_t,
    EpeHPHdrAdjPiFifo_t,
    EpeHPL2EditFifo_t,
    EpeHPL3EditFifo_t,
    EpeHPNextHopPrFifo_t,
    EpeHPPayloadInfoFifo_t,
    EpeHdrProcInterruptFatal_t,
    EpeHdrProcInterruptNormal_t,
    EpeHdrProcPhyPortMap_t,
    DsIpv6NatPrefix_RegRam_RamChkRec_t,
    EpeHdrProcDebugStats_t,
    EpeHdrProcFlowCtl_t,
    EpeHdrProcFragCtl_t,
    EpeHdrProcInit_t,
    EpeHdrProcParityEn_t,
    EpeL2EditCtl_t,
    EpeL2EtherType_t,
    EpeL2PortMacSa_t,
    EpeL2RouterMacSa_t,
    EpeL2SnapCtl_t,
    EpeL2TpidCtl_t,
    EpeL3EditMplsTtl_t,
    EpeL3IpIdentification_t,
    EpeMirrorEscapeCam_t,
    EpePbbCtl_t,
    EpePktProcCtl_t,
    EpePktProcMuxCtl_t,
};
static tbls_id_t dbg_epenexthop_tbl_id_list[]  = {
    DsDestChannel_t,
    DsDestInterface_t,
    DsDestPort_t,
    DsEgressVlanRangeProfile_t,
    EpeEditPriorityMap_t,
    EpeNextHopInternal_t,
    EpeNextHopInterruptFatal_t,
    EpeNextHopInterruptNormal_t,
    NextHopDsVlanRangeProfFifo_t,
    NextHopHashAckFifo_t,
    NextHopPiFifo_t,
    NextHopPrFifo_t,
    NextHopPrKeyFifo_t,
    DsDestChannel_RegRam_RamChkRec_t,
    DsDestInterface_RegRam_RamChkRec_t,
    DsDestPort_RegRam_RamChkRec_t,
    DsEgressVlanRangeProfile_RegRam_RamChkRec_t,
    EpeEditPriorityMap_RegRam_RamChkRec_t,
    EpeNextHopCreditCtl_t,
    EpeNextHopCtl_t,
    EpeNextHopDebugStats_t,
    EpeNextHopDebugStatus_t,
    EpeNextHopMiscCtl_t,
    EpeNextHopRandomValueGen_t,
};
static tbls_id_t dbg_epegb_oam_tbl_id_list[]  = {
    EpeOamHashDsFifo_t,
    EpeOamHdrProcInfoFifo_t,
    EpeOamInfoFifo_t,
    EpeOamInterruptFatal_t,
    EpeOamInterruptNormal_t,
    EpeOamConfig_t,
    EpeOamCreditDebug_t,
    EpeOamCtl_t,
    EpeOamDebugStats_t,
};
static tbls_id_t dbg_globalstats_tbl_id_list[]  = {
    DsStats_t,
    EEEStatsRam_t,
    GlobalStatsInterruptFatal_t,
    GlobalStatsInterruptNormal_t,
    GlobalStatsSatuAddr_t,
    RxInbdStatsRam_t,
    TxInbdStatsEpeRam_t,
    TxInbdStatsPauseRam_t,
    EEEStatsMem_RamChkRec_t,
    GlobalStatsByteCntThreshold_t,
    GlobalStatsCreditCtl_t,
    GlobalStatsCtl_t,
    GlobalStatsDbgStatus_t,
    GlobalStatsEEECalendar_t,
    GlobalStatsEEECtl_t,
    GlobalStatsEpeBaseAddr_t,
    GlobalStatsEpeReqDropCnt_t,
    GlobalStatsIpeBaseAddr_t,
    GlobalStatsIpeReqDropCnt_t,
    GlobalStatsParityFail_t,
    GlobalStatsPktCntThreshold_t,
    GlobalStatsPolicingBaseAddr_t,
    GlobalStatsPolicingReqDropCnt_t,
    GlobalStatsQMgrBaseAddr_t,
    GlobalStatsQMgrReqDropCnt_t,
    GlobalStatsRamInitCtl_t,
    GlobalStatsRxLpiState_t,
    GlobalStatsSatuAddrFifoDepth_t,
    GlobalStatsSatuAddrFifoDepthThreshold_t,
    GlobalStatsWrrCfg_t,
    RxInbdStatsMem_RamChkRec_t,
    TxInbdStatsEpeMem_RamChkRec_t,
    TxInbdStatsPauseMem_RamChkRec_t,
};
static tbls_id_t dbg_greatbeltsup_tbl_id_list[]  = {
    GbSupInterruptFatal_t,
    GbSupInterruptNormal_t,
    TraceHdrMem_t,
    AuxClkSelCfg_t,
    ClkDivAuxCfg_t,
    ClkDivCore0Cfg_t,
    ClkDivCore1Cfg_t,
    ClkDivCore2Cfg_t,
    ClkDivCore3Cfg_t,
    ClkDivIntf0Cfg_t,
    ClkDivIntf1Cfg_t,
    ClkDivIntf2Cfg_t,
    ClkDivIntf3Cfg_t,
    ClkDivRstCtl_t,
    CorePllCfg_t,
    DeviceId_t,
    GbSensorCtl_t,
    GbSupInterruptFunction_t,
    GlobalGatedClkCtl_t,
    Hss0AdaptEqCfg_t,
    Hss0SpiStatus_t,
    Hss12G0Cfg_t,
    Hss12G0PrbsTest_t,
    Hss12G1Cfg_t,
    Hss12G1PrbsTest_t,
    Hss12G2Cfg_t,
    Hss12G2PrbsTest_t,
    Hss12G3Cfg_t,
    Hss12G3PrbsTest_t,
    Hss1AdaptEqCfg_t,
    Hss1SpiStatus_t,
    Hss2AdaptEqCfg_t,
    Hss2SpiStatus_t,
    Hss3AdaptEqCfg_t,
    Hss3SpiStatus_t,
    Hss6GLaneACtl_t,
    HssAccessCtl_t,
    HssAcessParameter_t,
    HssBitOrderCfg_t,
    HssModeCfg_t,
    HssPllResetCfg_t,
    HssReadData_t,
    HssTxGearBoxRstCtl_t,
    HssWriteData_t,
    IntLkIntfCfg_t,
    InterruptEnable_t,
    InterruptMapCtl_t,
    LedClockDiv_t,
    LinkTimerCtl_t,
    MdioClockCfg_t,
    ModuleGatedClkCtl_t,
    OOBFCClockDiv_t,
    PcieBar0Cfg_t,
    PcieBar1Cfg_t,
    PcieCfgIpCfg_t,
    PcieDlIntError_t,
    PcieErrorInjectCtl_t,
    PcieErrorStatus_t,
    PcieInbdStatus_t,
    PcieIntfCfg_t,
    PciePerfMonCtl_t,
    PciePerfMonStatus_t,
    PciePmCfg_t,
    PcieProtocolCfg_t,
    PcieStatusReg_t,
    PcieTlDlpIpCfg_t,
    PcieTraceCtl_t,
    PcieUtlIntError_t,
    PllLockErrorCnt_t,
    PllLockOut_t,
    PtpTodClkDivCfg_t,
    QsgmiiHssSelCfg_t,
    ResetIntRelated_t,
    SerdesRefClkDivCfg_t,
    SgmacModeCfg_t,
    SupDebugClkRstCtl_t,
    SupGpioCtl_t,
    SupIntrMaskCfg_t,
    SyncEClkDivCfg_t,
    SyncEthernetClkCfg_t,
    SyncEthernetMon_t,
    SyncEthernetSelect_t,
    TankPllCfg_t,
    TimeOutInfo_t,
};
static tbls_id_t dbg_hashds_tbl_id_list[]  = {
    HashDsInterruptFatal_t,
    HashDsKey0LkupTrackFifo_t,
    HashDsAdReqWtCfg_t,
    HashDsCreditConfig_t,
    HashDsCreditUsed_t,
    HashDsDebugStats_t,
    HashDsKey0LkupTrackFifo_FifoAlmostFullThrd_t,
    HashDsKey1LkupTrackFifo_FifoAlmostFullThrd_t,
    HashDsKeyLkupResultDebugInfo_t,
    HashDsKeyReqWtCfg_t,
    UserIdHashLookupCtl_t,
    UserIdResultCtl_t,
};
static tbls_id_t dbg_i2cmaster_tbl_id_list[]  = {
    I2CMasterDataRam_t,
    I2CMasterInterruptNormal_t,
    I2CMasterBmpCfg_t,
    I2CMasterCfg_t,
    I2CMasterPollingCfg_t,
    I2CMasterReadCfg_t,
    I2CMasterReadCtl_t,
    I2CMasterReadStatus_t,
    I2CMasterStatus_t,
    I2CMasterWrCfg_t,
};
static tbls_id_t dbg_intlkintf_tbl_id_list[]  = {
    IntLkInterruptFatal_t,
    IntLkRxStatsMem_t,
    IntLkTxStatsMem_t,
    IntLkBurstCtl_t,
    IntLkCalPtrCtl_t,
    IntLkCreditCtl_t,
    IntLkCreditDebug_t,
    IntLkFcChanCtl_t,
    IntLkFifoThdCtl_t,
    IntLkFlCtlCal_t,
    IntLkInDataMemOverFlowInfo_t,
    IntLkInDataMem_RamChkRec_t,
    IntLkLaneRxCtl_t,
    IntLkLaneRxDebugState_t,
    IntLkLaneRxStats_t,
    IntLkLaneSwapEn_t,
    IntLkLaneTxCtl_t,
    IntLkLaneTxState_t,
    IntLkMemInitCtl_t,
    IntLkMetaFrameCtl_t,
    IntLkMiscCtl_t,
    IntLkParityCtl_t,
    IntLkPktCrcCtl_t,
    IntLkPktProcOutBufMem_RamChkRec_t,
    IntLkPktProcResWordMem_RamChkRec_t,
    IntLkPktProcStallInfo_t,
    IntLkPktStats_t,
    IntLkRateMatchCtl_t,
    IntLkRxAlignCtl_t,
    IntLkSoftReset_t,
    IntLkTestCtl_t,
};
static tbls_id_t dbg_ipefib_tbl_id_list[]  = {
    FibHashAdReqFifo_t,
    FibKeyTrackFifo_t,
    FibLearnFifoResult_t,
    FibLkupMgrReqFifo_t,
    IpeFibInterruptFatal_t,
    IpeFibInterruptNormal_t,
    LkupMgrLpmKeyReqFifo_t,
    LpmAdReqFifo_t,
    LpmFinalTrackFifo_t,
    LpmHashKeyTrackFifo_t,
    LpmHashResultFifo_t,
    LpmPipe0ReqFifo_t,
    LpmPipe1ReqFifo_t,
    LpmPipe2ReqFifo_t,
    LpmPipe3ReqFifo_t,
    LpmTcamAdMem_t,
    LpmTcamAdReqFifo_t,
    LpmTcamKeyReqFifo_t,
    LpmTcamResultFifo_t,
    LpmTrackFifo_t,
    FibAdRdTrackFifo_FifoAlmostFullThrd_t,
    FibCreditCtl_t,
    FibCreditDebug_t,
    FibDebugStats_t,
    FibEccCtl_t,
    FibEngineHashCtl_t,
    FibEngineLookupCtl_t,
    FibEngineLookupResultCtl_t,
    FibEngineLookupResultCtl1_t,
    FibHashKeyCpuReq_t,
    FibHashKeyCpuResult_t,
    FibInitCtl_t,
    FibKeyTrackFifo_FifoAlmostFullThrd_t,
    FibLearnFifoCtl_t,
    FibLearnFifoInfo_t,
    FibLrnKeyWrFifo_FifoAlmostFullThrd_t,
    FibMacHashTrackFifo_FifoAlmostFullThrd_t,
    FibParityCtl_t,
    FibTcamCpuAccess_t,
    FibTcamInitCtl_t,
    FibTcamReadData_t,
    FibTcamWriteData_t,
    FibTcamWriteMask_t,
    LkupMgrLpmKeyReqFifo_FifoAlmostFullThrd_t,
    LpmHash2ndTrackFifo_FifoAlmostFullThrd_t,
    LpmHashKeyTrackFifo_FifoAlmostFullThrd_t,
    LpmHashLuTrackFifo_FifoAlmostFullThrd_t,
    LpmIpv6Hash32HighKeyCam_t,
    LpmPipe0ReqFifo_FifoAlmostFullThrd_t,
    LpmPipe3TrackFifo_FifoAlmostFullThrd_t,
    LpmTcamAdMem_RegRam_RamChkRec_t,
    LpmTrackFifo_FifoAlmostFullThrd_t,
};
static tbls_id_t dbg_ipeforward_tbl_id_list[]  = {
    DsApsSelect_t,
    DsQcn_t,
    IpeFwdDiscardTypeStats_t,
    IpeFwdHdrAdjInfoFifo_t,
    IpeFwdInterruptFatal_t,
    IpeFwdInterruptNormal_t,
    IpeFwdLearnThroughFifo_t,
    IpeFwdLearnTrackFifo_t,
    IpeFwdOamFifo_t,
    IpeFwdQcnMapTable_t,
    IpeFwdRoutePiFifo_t,
    DsApsBridgeTrackFifo_FifoAlmostFullThrd_t,
    IpeFwdBridgeHdrRec_t,
    IpeFwdCfg_t,
    IpeFwdCtl_t,
    IpeFwdCtl1_t,
    IpeFwdDebugStats_t,
    IpeFwdDsQcnRam_RamChkRec_t,
    IpeFwdEopMsgFifo_FifoAlmostFullThrd_t,
    IpeFwdExcpRec_t,
    IpeFwdHdrAdjInfoFifo_FifoAlmostFullThrd_t,
    IpeFwdQcnCtl_t,
    IpeFwdSopMsgRam_RamChkRec_t,
    IpePriorityToStatsCos_t,
};
static tbls_id_t dbg_ipehdradj_tbl_id_list[]  = {
    DsPhyPort_t,
    IpeHdrAdjInterruptFatal_t,
    IpeHdrAdjInterruptNormal_t,
    IpeHdrAdjLpbkPdFifo_t,
    IpeHdrAdjLpbkPiFifo_t,
    IpeHdrAdjNetPdFifo_t,
    IpeHeaderAdjustPhyPortMap_t,
    IpeMuxHeaderCosMap_t,
    DsPhyPort_RegRam_RamChkRec_t,
    IpeHdrAdjCfg_t,
    IpeHdrAdjChanIdCfg_t,
    IpeHdrAdjCreditCtl_t,
    IpeHdrAdjCreditUsed_t,
    IpeHdrAdjDebugStats_t,
    IpeHdrAdjEccCtl_t,
    IpeHdrAdjEccStats_t,
    IpeHdrAdjLpbkPdFifo_FifoAlmostFullThrd_t,
    IpeHdrAdjLpbkPiFifo_FifoAlmostFullThrd_t,
    IpeHdrAdjNetPdFifo_FifoAlmostFullThrd_t,
    IpeHdrAdjWrrWeight_t,
    IpeHeaderAdjustCtl_t,
    IpeHeaderAdjustExpMap_t,
    IpeHeaderAdjustModeCtl_t,
    IpeLoopbackHeaderAdjustCtl_t,
    IpeMuxHeaderAdjustCtl_t,
    IpePhyPortMuxCtl_t,
    IpePortMacCtl1_t,
    IpeSgmacHeaderAdjustCtl_t,
};
static tbls_id_t dbg_ipeintfmap_tbl_id_list[]  = {
    DsMplsCtl_t,
    DsMplsResFifo_t,
    DsPhyPortExt_t,
    DsRouterMac_t,
    DsSrcInterface_t,
    DsSrcPort_t,
    DsVlanActionProfile_t,
    DsVlanRangeProfile_t,
    HashDsResFifo_t,
    InputPIFifo_t,
    InputPRFifo_t,
    IpeBpduProtocolEscapeCam3_t,
    IpeBpduProtocolEscapeCamResult3_t,
    IpeBpduProtocolEscapeCamResult4_t,
    IpeIntfMapInterruptFatal_t,
    IpeIntfMapInterruptNormal_t,
    IpeIntfMapperClaPathMap_t,
    TcamDsResFifo_t,
    DsMplsCtl_RegRam_RamChkRec_t,
    DsPhyPortExt_RegRam_RamChkRec_t,
    DsProtocolVlan_t,
    DsRouterMac_RegRam_RamChkRec_t,
    DsSrcInterface_RegRam_RamChkRec_t,
    DsSrcPort_RegRam_RamChkRec_t,
    DsVlanActionProfile_RegRam_RamChkRec_t,
    DsVlanRangeProfile_RegRam_RamChkRec_t,
    ExpInfoFifo_FifoAlmostFullThrd_t,
    ImPktDataFifo_FifoAlmostFullThrd_t,
    IpeBpduEscapeCtl_t,
    IpeBpduProtocolEscapeCam_t,
    IpeBpduProtocolEscapeCam2_t,
    IpeBpduProtocolEscapeCam4_t,
    IpeBpduProtocolEscapeCamResult_t,
    IpeBpduProtocolEscapeCamResult2_t,
    IpeDsVlanCtl_t,
    IpeIntfMapCreditCtl_t,
    IpeIntfMapDsStpStateStats_t,
    IpeIntfMapDsVlanProfStats_t,
    IpeIntfMapDsVlanStats_t,
    IpeIntfMapInfoOutStats_t,
    IpeIntfMapInitCtl_t,
    IpeIntfMapMiscCtl_t,
    IpeIntfMapRandomSeedCtl_t,
    IpeIntfMapStats1_t,
    IpeIntfMapStats2_t,
    IpeIntfMapStats3_t,
    IpeIntfMapStats4_t,
    IpeIntfMapperCtl_t,
    IpeMplsCtl_t,
    IpeMplsExpMap_t,
    IpeMplsTtlThrd_t,
    IpePortMacCtl_t,
    IpeRouterMacCtl_t,
    IpeTunnelDecapCtl_t,
    IpeTunnelIdCtl_t,
    IpeUserIdCtl_t,
    IpeUserIdSgmacCtl_t,
    UserIdImFifo_FifoAlmostFullThrd_t,
    UserIdKeyInfoFifo_FifoAlmostFullThrd_t,
};
static tbls_id_t dbg_ipelkupmgr_tbl_id_list[]  = {
    IpeLkupMgrInterruptFatal_t,
    IpeLkupMgrInterruptNormal_t,
    IpeLkupMgrPIFifo_t,
    IpeLkupMgrPRFifo_t,
    IpeException3Cam_t,
    IpeException3Cam2_t,
    IpeException3Ctl_t,
    IpeIpv4McastForceRoute_t,
    IpeIpv6McastForceRoute_t,
    IpeLkupMgrCredit_t,
    IpeLkupMgrDebugStats_t,
    IpeLkupMgrDrainEnable_t,
    IpeLkupMgrFsmState_t,
    IpeLookupCtl_t,
    IpeLookupPbrCtl_t,
    IpeLookupRouteCtl_t,
    IpeRouteMartianAddr_t,
};
static tbls_id_t dbg_ipepktproc_tbl_id_list[]  = {
    DsEcmpGroup_t,
    DsEcmpState_t,
    DsRpf_t,
    DsSrcChannel_t,
    DsStormCtl_t,
    IpeClassificationCosMap_t,
    IpeClassificationDscpMap_t,
    IpeClassificationPathMap_t,
    IpeClassificationPhbOffset_t,
    IpeClassificationPrecedenceMap_t,
    IpeLearningCache_t,
    IpeOamAckFifo_t,
    IpeOamFrImInfoFifo_t,
    IpeOamFrLmInfoFifo_t,
    IpePktProcFibFifo_t,
    IpePktProcHashDsFifo_t,
    IpePktProcInterruptFatal_t,
    IpePktProcInterruptNormal_t,
    IpePktProcPiFrImFifo_t,
    IpePktProcPiFrUiFifo_t,
    IpePktProcPktInfoFifo_t,
    IpePktProcTcamDsFifo0_t,
    IpePktProcTcamDsFifo1_t,
    IpePktProcTcamDsFifo2_t,
    IpePktProcTcamDsFifo3_t,
    DsEcmpGroup_RegRam_RamChkRec_t,
    DsEcmpStateMem_RamChkRec_t,
    DsRpf_RegRam_RamChkRec_t,
    DsSrcChannel_RegRam_RamChkRec_t,
    DsStormMemPart0_RamChkRec_t,
    DsStormMemPart1_RamChkRec_t,
    IpeAclAppCam_t,
    IpeAclAppCamResult_t,
    IpeAclQosCtl_t,
    IpeBridgeCtl_t,
    IpeBridgeEopMsgFifo_FifoAlmostFullThrd_t,
    IpeBridgeStormCtl_t,
    IpeClaEopMsgFifo_FifoAlmostFullThrd_t,
    IpeClassificationCtl_t,
    IpeClassificationDscpMap_RegRam_RamChkRec_t,
    IpeDsFwdCtl_t,
    IpeEcmpChannelState_t,
    IpeEcmpCtl_t,
    IpeEcmpTimerCtl_t,
    IpeFcoeCtl_t,
    IpeIpgCtl_t,
    IpeLearningCacheValid_t,
    IpeLearningCtl_t,
    IpeOamCtl_t,
    IpeOuterLearningCtl_t,
    IpePktProcCfg_t,
    IpePktProcCreditCtl_t,
    IpePktProcCreditUsed_t,
    IpePktProcDebugStats_t,
    IpePktProcDsFwdPtrDebug_t,
    IpePktProcEccCtl_t,
    IpePktProcEccStats_t,
    IpePktProcEopMsgFifoDepthRecord_t,
    IpePktProcInitCtl_t,
    IpePktProcRandSeedLoad_t,
    IpePtpCtl_t,
    IpeRouteCtl_t,
    IpeTrillCtl_t,
};
static tbls_id_t dbg_macleddriver_tbl_id_list[]  = {
    MacLedCfgPortMode_t,
    MacLedCfgPortSeqMap_t,
    MacLedDriverInterruptNormal_t,
    MacLedBlinkCfg_t,
    MacLedCfgAsyncFifoThr_t,
    MacLedCfgCalCtl_t,
    MacLedPolarityCfg_t,
    MacLedPortRange_t,
    MacLedRawStatusCfg_t,
    MacLedRefreshInterval_t,
    MacLedSampleInterval_t,
};
static tbls_id_t dbg_macmux_tbl_id_list[]  = {
    MacMuxInterruptFatal_t,
    MacMuxInterruptNormal_t,
    MacMuxCal_t,
    MacMuxDebugStats_t,
    MacMuxParityEnable_t,
    MacMuxStatSel_t,
    MacMuxWalker_t,
    MacMuxWrrCfg0_t,
    MacMuxWrrCfg1_t,
    MacMuxWrrCfg2_t,
    MacMuxWrrCfg3_t,
};
static tbls_id_t dbg_mdio_tbl_id_list[]  = {
    PortMap_t,
    MdioCfg_t,
    MdioCmd1G_t,
    MdioCmdXG_t,
    MdioLinkDownDetecEn_t,
    MdioLinkStatus_t,
    MdioScanCtl_t,
    MdioSpeciCfg_t,
    MdioSpecifiedStatus_t,
    MdioStatus1G_t,
    MdioStatusXG_t,
    MdioUsePhy_t,
    MdioXgPortChanIdWithOutPhy_t,
};
static tbls_id_t dbg_metfifo_tbl_id_list[]  = {
    DsApsBridge_t,
    DsApsChannelMap_t,
    DsMetFifoExcp_t,
    MetFifoBrRcdUpdFifo_t,
    MetFifoInterruptFatal_t,
    MetFifoInterruptNormal_t,
    MetFifoMsgRam_t,
    MetFifoQMgrRcdUpdFifo_t,
    MetFifoRcdRam_t,
    DsApsBridge_RegRam_RamChkRec_t,
    MetFifoApsInit_t,
    MetFifoCtl_t,
    MetFifoDebugStats_t,
    MetFifoDrainEnable_t,
    MetFifoEccCtl_t,
    MetFifoEnQueCredit_t,
    MetFifoEndPtr_t,
    MetFifoExcpTabParityFailRecord_t,
    MetFifoInit_t,
    MetFifoInitDone_t,
    MetFifoInputFifoDepth_t,
    MetFifoInputFifoThreshold_t,
    MetFifoLinkDownStateRecord_t,
    MetFifoLinkScanDoneState_t,
    MetFifoMaxInitCnt_t,
    MetFifoMcastEnable_t,
    MetFifoMsgCnt_t,
    MetFifoMsgTypeCnt_t,
    MetFifoParityEn_t,
    MetFifoPendingMcastCnt_t,
    MetFifoQMgrCredit_t,
    MetFifoQWriteRcdUpdFifoThrd_t,
    MetFifoRdCurStateMachine_t,
    MetFifoRdPtr_t,
    MetFifoStartPtr_t,
    MetFifoTbInfoArbCredit_t,
    MetFifoUpdateRcdErrorSpot_t,
    MetFifoWrPtr_t,
    MetFifoWrrCfg_t,
    MetFifoWrrWeightCnt_t,
    MetFifoWrrWt_t,
    MetFifoWrrWtCfg_t,
};
static tbls_id_t dbg_netrx_tbl_id_list[]  = {
    DsChannelizeIngFc_t,
    DsChannelizeMode_t,
    NetRxChanInfoRam_t,
    NetRxChannelMap_t,
    NetRxInterruptFatal_t,
    NetRxInterruptNormal_t,
    NetRxLinkListTable_t,
    NetRxPauseTimerMem_t,
    NetRxPktBufRam_t,
    NetRxPktInfoFifo_t,
    DsChannelizeIngFc_RegRam_RamChkRec_t,
    DsChannelizeMode_RegRam_RamChkRec_t,
    NetRxBpduStats_t,
    NetRxBufferCount_t,
    NetRxBufferFullThreshold_t,
    NetRxCtl_t,
    NetRxDebugStats_t,
    NetRxDrainEnable_t,
    NetRxEccCtl_t,
    NetRxEccErrorStats_t,
    NetRxFreeListCtl_t,
    NetRxIntLkPktSizeCheck_t,
    NetRxIntlkChanEn_t,
    NetRxIpeStallRecord_t,
    NetRxMemInit_t,
    NetRxParityFailRecord_t,
    NetRxPauseCtl_t,
    NetRxPauseType_t,
    NetRxPreFetchFifoDepth_t,
    NetRxReservedMacDaCtl_t,
    NetRxState_t,
    NetRxStatsEn_t,
    NetRxWrongPriorityRecord_t,
};
static tbls_id_t dbg_nettx_tbl_id_list[]  = {
    DsDestPtpChan_t,
    DsEgrIbLppInfo_t,
    DsQcnPortMac_t,
    InbdFcReqTab_t,
    NetTxCalBak_t,
    NetTxCalendarCtl_t,
    NetTxChDynamicInfo_t,
    NetTxChStaticInfo_t,
    NetTxChanIdMap_t,
    NetTxEeeTimerTab_t,
    NetTxHdrBuf_t,
    NetTxInterruptFatal_t,
    NetTxInterruptNormal_t,
    NetTxPauseTab_t,
    NetTxPktBuf_t,
    NetTxPortIdMap_t,
    NetTxPortModeTab_t,
    NetTxSgmacPriorityMapTable_t,
    PauseReqTab_t,
    DsEgrIbLppInfo_RegRam_RamChkRec_t,
    InbdFcLogReqFifo_FifoAlmostFullThrd_t,
    NetTxCalCtl_t,
    NetTxCfgChanId_t,
    NetTxChanCreditThrd_t,
    NetTxChanCreditUsed_t,
    NetTxChanTxDisCfg_t,
    NetTxChannelEn_t,
    NetTxChannelTxEn_t,
    NetTxCreditClearCtl_t,
    NetTxDebugStats_t,
    NetTxEEECfg_t,
    NetTxEEESleepTimerCfg_t,
    NetTxEEEWakeupTimerCfg_t,
    NetTxELoopStallRecord_t,
    NetTxEpeStallCtl_t,
    NetTxForceTxCfg_t,
    NetTxInbandFcCtl_t,
    NetTxInit_t,
    NetTxInitDone_t,
    NetTxIntLkCtl_t,
    NetTxMiscCtl_t,
    NetTxParityEccCtl_t,
    NetTxParityFailRecord_t,
    NetTxPauseQuantaCfg_t,
    NetTxPtpEnCtl_t,
    NetTxQcnCtl_t,
    NetTxStatSel_t,
    NetTxTxThrdCfg_t,
};
static tbls_id_t dbg_oamautogenpkt_tbl_id_list[]  = {
    AutoGenPktInterruptNormal_t,
    AutoGenPktPktCfg_t,
    AutoGenPktPktHdr_t,
    AutoGenPktRxPktStats_t,
    AutoGenPktTxPktStats_t,
    AutoGenPktCreditCtl_t,
    AutoGenPktCtl_t,
    AutoGenPktDebugStats_t,
    AutoGenPktEccCtl_t,
    AutoGenPktEccStats_t,
    AutoGenPktPrbsCtl_t,
};
static tbls_id_t dbg_oamfwd_tbl_id_list[]  = {
    DsOamExcp_t,
    OamFwdInterruptFatal_t,
    OamFwdInterruptNormal_t,
    OamHdrEditPDInFifo_t,
    OamHdrEditPIInFifo_t,
    OamFwdCreditCtl_t,
    OamFwdDebugStats_t,
    OamHdrEditCreditCtl_t,
    OamHdrEditDebugStats_t,
    OamHdrEditDrainEnable_t,
    OamHdrEditParityEnable_t,
    OamHeaderEditCtl_t,
};
static tbls_id_t dbg_oamparser_tbl_id_list[]  = {
    OamParserInterruptFatal_t,
    OamParserInterruptNormal_t,
    OamParserPktFifo_t,
    OamHeaderAdjustCtl_t,
    OamParserCtl_t,
    OamParserDebugCnt_t,
    OamParserEtherCtl_t,
    OamParserFlowCtl_t,
    OamParserLayer2ProtocolCam_t,
    OamParserLayer2ProtocolCamValid_t,
    OamParserPktFifo_FifoAlmostFullThrd_t,
};
static tbls_id_t dbg_oamproc_tbl_id_list[]  = {
    DsMp_t,
    DsOamDefectStatus_t,
    DsPortProperty_t,
    DsPriorityMap_t,
    OamDefectCache_t,
    OamProcInterruptFatal_t,
    OamProcInterruptNormal_t,
    OamRxProcPIInFifo_t,
    OamRxProcPRInFifo_t,
    OamDefectDebug_t,
    OamDsMpDataMask_t,
    OamErrorDefectCtl_t,
    OamEtherCciCtl_t,
    OamEtherSendId_t,
    OamEtherTxCtl_t,
    OamEtherTxMac_t,
    OamProcCtl_t,
    OamProcDebugStats_t,
    OamProcEccCtl_t,
    OamProcEccStats_t,
    OamRxProcEtherCtl_t,
    OamTblAddrCtl_t,
    OamUpdateApsCtl_t,
    OamUpdateCtl_t,
    OamUpdateStatus_t,
};
static tbls_id_t dbg_oobfc_tbl_id_list[]  = {
    DsOobFcCalendar_t,
    DsOobFcGrpMap_t,
    OobFcInterruptFatal_t,
    OobFcInterruptNormal_t,
    OobFcCfgChanNum_t,
    OobFcCfgFlowCtl_t,
    OobFcCfgIngsMode_t,
    OobFcCfgPortNum_t,
    OobFcCfgRxProc_t,
    OobFcCfgSpiMode_t,
    OobFcDebugStats_t,
    OobFcEgsVecReg_t,
    OobFcErrorStats_t,
    OobFcFrameGapNum_t,
    OobFcFrameUpdateState_t,
    OobFcIngsVecReg_t,
    OobFcInit_t,
    OobFcInitDone_t,
    OobFcParityEn_t,
    OobFcRxRcvEn_t,
    OobFcTxFifoAFullThrd_t,
};
static tbls_id_t dbg_parser_tbl_id_list[]  = {
    ParserInterruptFatal_t,
    IpeIntfMapParserCreditThrd_t,
    ParserEthernetCtl_t,
    ParserFcoeCtl_t,
    ParserIpHashCtl_t,
    ParserLayer2FlexCtl_t,
    ParserLayer2ProtocolCam_t,
    ParserLayer2ProtocolCamValid_t,
    ParserLayer3FlexCtl_t,
    ParserLayer3HashCtl_t,
    ParserLayer3ProtocolCam_t,
    ParserLayer3ProtocolCamValid_t,
    ParserLayer3ProtocolCtl_t,
    ParserLayer4AchCtl_t,
    ParserLayer4AppCtl_t,
    ParserLayer4AppDataCtl_t,
    ParserLayer4FlagOpCtl_t,
    ParserLayer4FlexCtl_t,
    ParserLayer4HashCtl_t,
    ParserLayer4PortOpCtl_t,
    ParserLayer4PortOpSel_t,
    ParserMplsCtl_t,
    ParserPacketTypeMap_t,
    ParserParityEn_t,
    ParserPbbCtl_t,
    ParserTrillCtl_t,
};
static tbls_id_t dbg_pbctl_tbl_id_list[]  = {
    HdrWrReqFifo_t,
    PbCtlHdrBuf_t,
    PbCtlInterruptFatal_t,
    PbCtlInterruptNormal_t,
    PbCtlPktBuf_t,
    PktWrReqFifo_t,
    PbCtlDebugStats_t,
    PbCtlEccCtl_t,
    PbCtlInit_t,
    PbCtlInitDone_t,
    PbCtlParityCtl_t,
    PbCtlParityFailRecord_t,
    PbCtlRdCreditCtl_t,
    PbCtlRefCtl_t,
    PbCtlReqHoldStats_t,
    PbCtlWeightCfg_t,
};
static tbls_id_t dbg_pciexpcore_tbl_id_list[]  = {
    DescRxMemBase_t,
    DescRxMemDepth_t,
    DescRxVldNum_t,
    DescTxMemBase_t,
    DescTxMemDepth_t,
    DescTxVldNum_t,
    DescWrbkFifo_t,
    DmaChanMap_t,
    DmaRxPtrTab_t,
    DmaTxPtrTab_t,
    GbifRegMem_t,
    LearnInfoFifo_t,
    PciExpCoreInterruptNormal_t,
    PciExpDescMem_t,
    PktTxInfoFifo_t,
    RegRdInfoFifo_t,
    SysMemBaseTab_t,
    TabWrInfoFifo_t,
    DescWrbkFifo_FifoAlmostFullThrd_t,
    DmaChanCfg_t,
    DmaDebugStats_t,
    DmaDescErrorRec_t,
    DmaEccCtl_t,
    DmaEndianCfg_t,
    DmaFifoDepth_t,
    DmaFifoThrdCfg_t,
    DmaGenIntrCtl_t,
    DmaInitCtl_t,
    DmaLearnMemCtl_t,
    DmaLearnMiscCtl_t,
    DmaLearnValidCtl_t,
    DmaMiscCtl_t,
    DmaPktMiscCtl_t,
    DmaPktStats_t,
    DmaPktTimerCtl_t,
    DmaPktTxDebugStats_t,
    DmaRdTimerCtl_t,
    DmaStateDebug_t,
    DmaStatsBmpCfg_t,
    DmaStatsEntryCfg_t,
    DmaStatsIntervalCfg_t,
    DmaStatsOffsetCfg_t,
    DmaStatsUserCfg_t,
    DmaTagStats_t,
    DmaWrTimerCtl_t,
    PciExpDescMem_RegRam_RamChkRec_t,
    PcieFuncIntr_t,
    PcieIntrCfg_t,
    PcieOutboundStats_t,
    PcieTagCtl_t,
    PktRxDataFifo_FifoAlmostFullThrd_t,
    PktTxDataFifo_FifoAlmostFullThrd_t,
    PktTxInfoFifo_FifoAlmostFullThrd_t,
    RegRdInfoFifo_FifoAlmostFullThrd_t,
    TabWrDataFifo_FifoAlmostFullThrd_t,
    TabWrInfoFifo_FifoAlmostFullThrd_t,
};
static tbls_id_t dbg_policing_tbl_id_list[]  = {
    DsPolicerControl_t,
    DsPolicerCount_t,
    DsPolicerProfile_t,
    PolicingEpeFifo_t,
    PolicingInterruptFatal_t,
    PolicingInterruptNormal_t,
    PolicingIpeFifo_t,
    DsPolicerControl_RamChkRec_t,
    DsPolicerCount_RamChkRec_t,
    DsPolicerProfileRamChkRec_t,
    IpePolicingCtl_t,
    PolicingDebugStats_t,
    PolicingEccCtl_t,
    PolicingEpeFifo_FifoAlmostFullThrd_t,
    PolicingInitCtl_t,
    PolicingIpeFifo_FifoAlmostFullThrd_t,
    PolicingMiscCtl_t,
    UpdateReqIgnoreRecord_t,
};
static tbls_id_t dbg_ptpengine_tbl_id_list[]  = {
    PtpCapturedAdjFrc_t,
    PtpEngineInterruptFatal_t,
    PtpEngineInterruptNormal_t,
    PtpMacTxCaptureTs_t,
    PtpSyncIntfHalfPeriod_t,
    PtpSyncIntfToggle_t,
    PtpTodTow_t,
    PtpEngineFifoDepthRecord_t,
    PtpFrcQuanta_t,
    PtpMacTxCaptureTsCtrl_t,
    PtpParityEn_t,
    PtpSyncIntfCfg_t,
    PtpSyncIntfInputCode_t,
    PtpSyncIntfInputTs_t,
    PtpTodCfg_t,
    PtpTodCodeCfg_t,
    PtpTodInputCode_t,
    PtpTodInputTs_t,
    PtpTsCaptureCtrl_t,
    PtpTsCaptureDropCnt_t,
};
static tbls_id_t dbg_ptpfrc_tbl_id_list[]  = {
    PtpDriftRateAdjust_t,
    PtpOffsetAdjust_t,
    PtpRefFrc_t,
    PtpFrcCtl_t,
};
static tbls_id_t dbg_qmgrdeq_tbl_id_list[]  = {
    DsChanActiveLink_t,
    DsChanBackupLink_t,
    DsChanCredit_t,
    DsChanShpOldToken_t,
    DsChanShpProfile_t,
    DsChanShpToken_t,
    DsChanState_t,
    DsDlbTokenThrd_t,
    DsGrpFwdLinkList0_t,
    DsGrpFwdLinkList1_t,
    DsGrpFwdLinkList2_t,
    DsGrpFwdLinkList3_t,
    DsGrpNextLinkList0_t,
    DsGrpNextLinkList1_t,
    DsGrpNextLinkList2_t,
    DsGrpNextLinkList3_t,
    DsGrpShpProfile_t,
    DsGrpShpState_t,
    DsGrpShpToken_t,
    DsGrpShpWfqCtl_t,
    DsGrpState_t,
    DsGrpWfqDeficit_t,
    DsGrpWfqState_t,
    DsOobFcPriState_t,
    DsPacketLinkList_t,
    DsPacketLinkState_t,
    DsQueEntryAging_t,
    DsQueMap_t,
    DsQueShpCtl_t,
    DsQueShpProfile_t,
    DsQueShpState_t,
    DsQueShpToken_t,
    DsQueShpWfqCtl_t,
    DsQueState_t,
    DsQueWfqDeficit_t,
    DsQueWfqState_t,
    QMgrDeqInterruptFatal_t,
    QMgrDeqInterruptNormal_t,
    QMgrNetworkWtCfgMem_t,
    QMgrNetworkWtMem_t,
    DsChanActiveLink_RegRam_RamChkRec_t,
    DsChanBackupLink_RegRam_RamChkRec_t,
    DsChanCredit_RegRam_RamChkRec_t,
    DsChanShpOldToken_RegRam_RamChkRec_t,
    DsChanShpProfile_RegRam_RamChkRec_t,
    DsChanShpToken_RegRam_RamChkRec_t,
    DsGrpFwdLinkList0_RegRam_RamChkRec_t,
    DsGrpFwdLinkList1_RegRam_RamChkRec_t,
    DsGrpFwdLinkList2_RegRam_RamChkRec_t,
    DsGrpFwdLinkList3_RegRam_RamChkRec_t,
    DsGrpNextLinkList0_RegRam_RamChkRec_t,
    DsGrpNextLinkList1_RegRam_RamChkRec_t,
    DsGrpNextLinkList2_RegRam_RamChkRec_t,
    DsGrpNextLinkList3_RegRam_RamChkRec_t,
    DsGrpShpProfile_RegRam_RamChkRec_t,
    DsGrpShpState_RegRam_RamChkRec_t,
    DsGrpShpToken_RegRam_RamChkRec_t,
    DsGrpShpWfqCtl_RegRam_RamChkRec_t,
    DsGrpState_RegRam_RamChkRec_t,
    DsGrpWfqDeficit_RegRam_RamChkRec_t,
    DsOobFcPriState_RegRam_RamChkRec_t,
    DsPacketLinkList_RegRam_RamChkRec_t,
    DsPacketLinkState_RegRam_RamChkRec_t,
    DsQueMap_RegRam_RamChkRec_t,
    DsQueShpCtl_RegRam_RamChkRec_t,
    DsQueShpProfile_RegRam_RamChkRec_t,
    DsQueShpState_RegRam_RamChkRec_t,
    DsQueShpToken_RegRam_RamChkRec_t,
    DsQueShpWfqCtl_RegRam_RamChkRec_t,
    DsQueState_RegRam_RamChkRec_t,
    DsQueWfqDeficit_RegRam_RamChkRec_t,
    DsQueWfqState_RegRam_RamChkRec_t,
    QDlbChanSpeedModeCtl_t,
    QMgrAgingCtl_t,
    QMgrAgingMemInitCtl_t,
    QMgrChanFlushCtl_t,
    QMgrChanShapeCtl_t,
    QMgrChanShapeState_t,
    QMgrDeqDebugStats_t,
    QMgrDeqEccCtl_t,
    QMgrDeqFifoDepth_t,
    QMgrDeqFreePktListCtl_t,
    QMgrDeqInitCtl_t,
    QMgrDeqParityEnable_t,
    QMgrDeqPktLinkListInitCtl_t,
    QMgrDeqPriToCosMap_t,
    QMgrDeqSchCtl_t,
    QMgrDeqStallInitCtl_t,
    QMgrDlbCtl_t,
    QMgrDrainCtl_t,
    QMgrFreeListFifoCreditCtl_t,
    QMgrGrpShapeCtl_t,
    QMgrIntLkStallCtl_t,
    QMgrIntfWrrWtCtl_t,
    QMgrMiscIntfWrrWtCtl_t,
    QMgrOobFcCtl_t,
    QMgrOobFcStatus_t,
    QMgrQueEntryCreditCtl_t,
    QMgrQueShapeCtl_t,
};
static tbls_id_t dbg_qmgrenq_tbl_id_list[]  = {
    DsEgrPortCnt_t,
    DsEgrPortDropProfile_t,
    DsEgrPortFcState_t,
    DsEgrResrcCtl_t,
    DsGrpCnt_t,
    DsGrpDropProfile_t,
    DsHeadHashMod_t,
    DsLinkAggregateChannel_t,
    DsLinkAggregateChannelGroup_t,
    DsLinkAggregateChannelMember_t,
    DsLinkAggregateChannelMemberSet_t,
    DsLinkAggregateGroup_t,
    DsLinkAggregateMember_t,
    DsLinkAggregateMemberSet_t,
    DsLinkAggregateSgmacGroup_t,
    DsLinkAggregateSgmacMember_t,
    DsLinkAggregateSgmacMemberSet_t,
    DsLinkAggregationPort_t,
    DsQcnProfile_t,
    DsQcnProfileId_t,
    DsQcnQueDepth_t,
    DsQcnQueIdBase_t,
    DsQueCnt_t,
    DsQueThrdProfile_t,
    DsQueueHashKey_t,
    DsQueueNumGenCtl_t,
    DsQueueSelectMap_t,
    DsSgmacHeadHashMod_t,
    DsSgmacMap_t,
    DsUniformRand_t,
    QMgrEnqBrRtnFifo_t,
    QMgrEnqChanClass_t,
    QMgrEnqFreeListFifo_t,
    QMgrEnqInterruptFatal_t,
    QMgrEnqInterruptNormal_t,
    QMgrEnqMsgFifo_t,
    QMgrEnqQueNumInFifo_t,
    QMgrEnqQueNumOutFifo_t,
    QMgrEnqSchFifo_t,
    QMgrEnqWredIndex_t,
    DsEgrResrcCtl_RegRam_RamChkRec_t,
    DsGrpCnt_RegRam_RamChkRec_t,
    DsHeadHashMod_RegRam_RamChkRec_t,
    DsLinkAggregateChannelMember_RegRam_RamChkRec_t,
    DsLinkAggregateMemberSet_RegRam_RamChkRec_t,
    DsLinkAggregateMember_RegRam_RamChkRec_t,
    DsLinkAggregateSgmacMember_RegRam_RamChkRec_t,
    DsLinkAggregationPort_RegRam_RamChkRec_t,
    DsQcnQueDepth_RegRam_RamChkRec_t,
    DsQueCnt_RegRam_RamChkRec_t,
    DsQueThrdProfile_RegRam_RamChkRec_t,
    DsQueueHashKey_RegRam_RamChkRec_t,
    DsQueueNumGenCtl_RegRam_RamChkRec_t,
    DsQueueSelectMap_RegRam_RamChkRec_t,
    DsSgmacHeadHashMod_RegRam_RamChkRec_t,
    EgrCondDisProfile_t,
    EgrCongestLevelThrd_t,
    EgrResrcMgrCtl_t,
    EgrScCnt_t,
    EgrScThrd_t,
    EgrTcCnt_t,
    EgrTcThrd_t,
    EgrTotalCnt_t,
    QHashCamCtl_t,
    QLinkAggTimerCtl0_t,
    QLinkAggTimerCtl1_t,
    QLinkAggTimerCtl2_t,
    QMgrEnqCreditConfig_t,
    QMgrEnqCreditUsed_t,
    QMgrEnqCtl_t,
    QMgrEnqDebugFsm_t,
    QMgrEnqDebugStats_t,
    QMgrEnqDrainEnable_t,
    QMgrEnqDropCtl_t,
    QMgrEnqEccCtl_t,
    QMgrEnqInit_t,
    QMgrEnqInitDone_t,
    QMgrEnqLengthAdjust_t,
    QMgrEnqLinkDownScanState_t,
    QMgrEnqLinkState_t,
    QMgrEnqRandSeedLoadForceDrop_t,
    QMgrEnqScanLinkDownChanRecord_t,
    QMgrEnqTrackFifo_FifoAlmostFullThrd_t,
    QMgrNetPktAdj_t,
    QMgrQueDeqStatsBase_t,
    QMgrQueEnqStatsBase_t,
    QMgrQueueIdMon_t,
    QMgrRandSeedLoad_t,
    QMgrReservedChannelRange_t,
    QWriteCtl_t,
    QWriteSgmacCtl_t,
    QueMinProfile_t,
};
static tbls_id_t dbg_qmgrqueentry_tbl_id_list[]  = {
    DsQueueEntry_t,
    QMgrQueEntryInterruptFatal_t,
    QMgrQueEntryInterruptNormal_t,
    QMgrQueEntryDsCheckFailRecord_t,
    QMgrQueEntryEccCtl_t,
    QMgrQueEntryInitCtl_t,
    QMgrQueEntryRefreshCtl_t,
    QMgrQueEntryStats_t,
};
static tbls_id_t dbg_qsgmii_tbl_id_list[]  = {
    QsgmiiInterruptNormal0_t,
    QsgmiiPcs0AnegCfg0_t,
    QsgmiiPcs0AnegStatus0_t,
    QsgmiiPcs1AnegCfg0_t,
    QsgmiiPcs1AnegStatus0_t,
    QsgmiiPcs2AnegCfg0_t,
    QsgmiiPcs2AnegStatus0_t,
    QsgmiiPcs3AnegCfg0_t,
    QsgmiiPcs3AnegStatus0_t,
    QsgmiiPcsCfg0_t,
    QsgmiiPcsCodeErrCnt0_t,
    QsgmiiPcsSoftRst0_t,
    QsgmiiPcsStatus0_t,
};
static tbls_id_t dbg_quadmac_tbl_id_list[]  = {
    QuadMacInterruptNormal0_t,
    QuadMacStatsRam0_t,
    QuadMacDataErrCtl0_t,
    QuadMacDebugStats0_t,
    QuadMacGmac0Mode0_t,
    QuadMacGmac0PtpCfg0_t,
    QuadMacGmac0PtpStatus0_t,
    QuadMacGmac0RxCtrl0_t,
    QuadMacGmac0SoftRst0_t,
    QuadMacGmac0TxCtrl0_t,
    QuadMacGmac1Mode0_t,
    QuadMacGmac1PtpCfg0_t,
    QuadMacGmac1PtpStatus0_t,
    QuadMacGmac1RxCtrl0_t,
    QuadMacGmac1SoftRst0_t,
    QuadMacGmac1TxCtrl0_t,
    QuadMacGmac2Mode0_t,
    QuadMacGmac2PtpCfg0_t,
    QuadMacGmac2PtpStatus0_t,
    QuadMacGmac2RxCtrl0_t,
    QuadMacGmac2SoftRst0_t,
    QuadMacGmac2TxCtrl0_t,
    QuadMacGmac3Mode0_t,
    QuadMacGmac3PtpCfg0_t,
    QuadMacGmac3PtpStatus0_t,
    QuadMacGmac3RxCtrl0_t,
    QuadMacGmac3SoftRst0_t,
    QuadMacGmac3TxCtrl0_t,
    QuadMacInit0_t,
    QuadMacInitDone0_t,
    QuadMacStatsCfg0_t,
    QuadMacStatusOverWrite0_t,
};
static tbls_id_t dbg_quadpcs_tbl_id_list[]  = {
    QuadPcsInterruptNormal0_t,
    QuadPcsPcs0AnegCfg0_t,
    QuadPcsPcs0AnegStatus0_t,
    QuadPcsPcs0Cfg0_t,
    QuadPcsPcs0ErrCnt0_t,
    QuadPcsPcs0SoftRst0_t,
    QuadPcsPcs0Status0_t,
    QuadPcsPcs1AnegCfg0_t,
    QuadPcsPcs1AnegStatus0_t,
    QuadPcsPcs1Cfg0_t,
    QuadPcsPcs1ErrCnt0_t,
    QuadPcsPcs1SoftRst0_t,
    QuadPcsPcs1Status0_t,
    QuadPcsPcs2AnegCfg0_t,
    QuadPcsPcs2AnegStatus0_t,
    QuadPcsPcs2Cfg0_t,
    QuadPcsPcs2ErrCnt0_t,
    QuadPcsPcs2SoftRst0_t,
    QuadPcsPcs2Status0_t,
    QuadPcsPcs3AnegCfg0_t,
    QuadPcsPcs3AnegStatus0_t,
    QuadPcsPcs3Cfg0_t,
    QuadPcsPcs3ErrCnt0_t,
    QuadPcsPcs3SoftRst0_t,
    QuadPcsPcs3Status0_t,
};
static tbls_id_t dbg_sgmac_tbl_id_list[]  = {
    SgmacInterruptNormal0_t,
    SgmacStatsRam0_t,
    SgmacCfg0_t,
    SgmacCtl0_t,
    SgmacDebugStatus0_t,
    SgmacPauseCfg0_t,
    SgmacPcsAnegCfg0_t,
    SgmacPcsAnegStatus0_t,
    SgmacPcsCfg0_t,
    SgmacPcsErrCnt0_t,
    SgmacPcsStatus0_t,
    SgmacPtpStatus0_t,
    SgmacSoftRst0_t,
    SgmacStatsCfg0_t,
    SgmacStatsInit0_t,
    SgmacStatsInitDone0_t,
    SgmacXauiCfg0_t,
    SgmacXfiDebugStats0_t,
    SgmacXfiTest0_t,
    SgmacXfiTestPatSeed0_t,
};
static tbls_id_t dbg_sharedds_tbl_id_list[]  = {
    DsStpState_t,
    DsVlan_t,
    DsVlanProfile_t,
    SharedDsInterruptNormal_t,
    DsStpState_RegRam_RamChkRec_t,
    DsVlanProfile_RegRam_RamChkRec_t,
    DsVlan_RegRam_RamChkRec_t,
    SharedDsEccCtl_t,
    SharedDsEccErrStats_t,
    SharedDsInit_t,
    SharedDsInitDone_t,
};
static tbls_id_t dbg_tcamctlint_tbl_id_list[]  = {
    TcamCtlIntCpuRequestMem_t,
    TcamCtlIntCpuResultMem_t,
    TcamCtlIntInterruptFatal_t,
    TcamCtlIntInterruptNormal_t,
    TcamCtlIntBistCtl_t,
    TcamCtlIntBistPointers_t,
    TcamCtlIntCaptureResult_t,
    TcamCtlIntCpuAccessCmd_t,
    TcamCtlIntCpuRdData_t,
    TcamCtlIntCpuWrData_t,
    TcamCtlIntCpuWrMask_t,
    TcamCtlIntDebugStats_t,
    TcamCtlIntInitCtrl_t,
    TcamCtlIntKeySizeCfg_t,
    TcamCtlIntKeyTypeCfg_t,
    TcamCtlIntMiscCtrl_t,
    TcamCtlIntState_t,
    TcamCtlIntWrapSetting_t,
};
static tbls_id_t dbg_tcamds_tbl_id_list[]  = {
    TcamDsInterruptFatal_t,
    TcamDsInterruptNormal_t,
    TcamDsRam4WBus_t,
    TcamDsRam8WBus_t,
    TcamDsArbWeight_t,
    TcamDsCreditCtl_t,
    TcamDsDebugStats_t,
    TcamDsMemAllocationFail_t,
    TcamDsMiscCtl_t,
    TcamDsRamInitCtl_t,
    TcamDsRamParityFail_t,
    TcamEngineLookupResultCtl0_t,
    TcamEngineLookupResultCtl1_t,
    TcamEngineLookupResultCtl2_t,
    TcamEngineLookupResultCtl3_t,
};
static tbls_id_t dbg__dsfib_tbl_id_list[]  = {
    DsFibUserId160Key_t,
    DsFibUserId80Key_t,
    DsIpv4HashKey_t,
    DsIpv6HashKey_t,
    DsIpv6McastHashKey_t,
    DsLpmIpv4Hash16Key_t,
    DsLpmIpv4Hash8Key_t,
    DsLpmIpv4McastHash32Key_t,
    DsLpmIpv4McastHash64Key_t,
    DsLpmIpv4NatDaPortHashKey_t,
    DsLpmIpv4NatSaHashKey_t,
    DsLpmIpv4NatSaPortHashKey_t,
    DsLpmIpv6Hash32HighKey_t,
    DsLpmIpv6Hash32LowKey_t,
    DsLpmIpv6Hash32MidKey_t,
    DsLpmIpv6McastHash0Key_t,
    DsLpmIpv6McastHash1Key_t,
    DsLpmIpv6NatDaPortHashKey_t,
    DsLpmIpv6NatSaHashKey_t,
    DsLpmIpv6NatSaPortHashKey_t,
    DsLpmLookupKey_t,
    DsLpmLookupKey0_t,
    DsLpmLookupKey1_t,
    DsLpmLookupKey2_t,
    DsLpmLookupKey3_t,
    DsLpmTcam160Key_t,
    DsLpmTcam80Key_t,
    DsLpmTcamAd_t,
    DsMacFibKey_t,
    DsMacIpFibKey_t,
    DsMacIpv6FibKey_t,
    FibLearnFifoData_t,
};
static tbls_id_t dbg__dynamicdsmem_tbl_id_list[]  = {
    DsDestPtpChannel_t,
    DsDlbChanIdleLevel_t,
    DsOamLmStats_t,
    DsOamLmStats0_t,
    DsOamLmStats1_t,
    DsAcl_t,
    DsAclQosIpv4HashKey_t,
    DsAclQosMacHashKey_t,
    DsBfdMep_t,
    DsBfdRmep_t,
    DsEthMep_t,
    DsEthOamChan_t,
    DsEthRmep_t,
    DsEthRmepChan_t,
    DsEthRmepChanConflictTcam_t,
    DsFcoeDa_t,
    DsFcoeDaTcam_t,
    DsFcoeHashKey_t,
    DsFcoeRpfHashKey_t,
    DsFcoeSa_t,
    DsFcoeSaTcam_t,
    DsFwd_t,
    DsIpDa_t,
    DsIpSaNat_t,
    DsIpv4Acl0Tcam_t,
    DsIpv4Acl1Tcam_t,
    DsIpv4Acl2Tcam_t,
    DsIpv4Acl3Tcam_t,
    DsIpv4McastDaTcam_t,
    DsIpv4SaNatTcam_t,
    DsIpv4UcastDaTcam_t,
    DsIpv4UcastPbrDualDaTcam_t,
    DsIpv6Acl0Tcam_t,
    DsIpv6Acl1Tcam_t,
    DsIpv6McastDaTcam_t,
    DsIpv6SaNatTcam_t,
    DsIpv6UcastDaTcam_t,
    DsIpv6UcastPbrDualDaTcam_t,
    DsL2EditEth4W_t,
    DsL2EditEth8W_t,
    DsL2EditFlex8W_t,
    DsL2EditLoopback_t,
    DsL2EditPbb4W_t,
    DsL2EditPbb8W_t,
    DsL2EditSwap_t,
    DsL3EditFlex_t,
    DsL3EditMpls4W_t,
    DsL3EditMpls8W_t,
    DsL3EditNat4W_t,
    DsL3EditNat8W_t,
    DsL3EditTunnelV4_t,
    DsL3EditTunnelV6_t,
    DsMa_t,
    DsMaName_t,
    DsMac_t,
    DsMacAcl0Tcam_t,
    DsMacAcl1Tcam_t,
    DsMacAcl2Tcam_t,
    DsMacAcl3Tcam_t,
    DsMacHashKey_t,
    DsMacIpv4Hash32Key_t,
    DsMacIpv4Hash64Key_t,
    DsMacIpv4Tcam_t,
    DsMacIpv6Tcam_t,
    DsMacTcam_t,
    DsMetEntry_t,
    DsMpls_t,
    DsNextHop4W_t,
    DsNextHop8W_t,
    DsTrillDa_t,
    DsTrillDaMcastTcam_t,
    DsTrillDaUcastTcam_t,
    DsTrillMcastHashKey_t,
    DsTrillMcastVlanHashKey_t,
    DsTrillUcastHashKey_t,
    DsUserIdEgressDefault_t,
    DsVlanXlate_t,
    DsVlanXlateConflictTcam_t,
};
static tbls_id_t dbg__fwdmessage_tbl_id_list[]  = {
    MsDequeue_t,
    MsEnqueue_t,
    MsExcpInfo_t,
    MsMetFifo_t,
    MsPacketHeader_t,
    MsPacketRelease_t,
    MsRcdUpdate_t,
    MsStatsUpdate_t,
    PacketHeaderOuter_t,
};
static tbls_id_t dbg__hashdsmem_tbl_id_list[]  = {
    DsBfdOamChanTcam_t,
    DsBfdOamHashKey_t,
    DsEthOamHashKey_t,
    DsEthOamRmepHashKey_t,
    DsEthOamTcamChan_t,
    DsEthOamTcamChanConflictTcam_t,
    DsMplsOamChanTcam_t,
    DsMplsOamLabelHashKey_t,
    DsMplsPbtBfdOamChan_t,
    DsPbtOamChanTcam_t,
    DsPbtOamHashKey_t,
    DsTunnelId_t,
    DsTunnelIdCapwapHashKey_t,
    DsTunnelIdCapwapTcam_t,
    DsTunnelIdConflictTcam_t,
    DsTunnelIdDefault_t,
    DsTunnelIdIpv4HashKey_t,
    DsTunnelIdIpv4RpfHashKey_t,
    DsTunnelIdIpv4Tcam_t,
    DsTunnelIdIpv6Tcam_t,
    DsTunnelIdPbbHashKey_t,
    DsTunnelIdPbbTcam_t,
    DsTunnelIdTrillMcAdjCheckHashKey_t,
    DsTunnelIdTrillMcDecapHashKey_t,
    DsTunnelIdTrillMcRpfHashKey_t,
    DsTunnelIdTrillTcam_t,
    DsTunnelIdTrillUcDecapHashKey_t,
    DsTunnelIdTrillUcRpfHashKey_t,
    DsUserId_t,
    DsUserIdConflictTcam_t,
    DsUserIdCvlanCosHashKey_t,
    DsUserIdCvlanHashKey_t,
    DsUserIdDoubleVlanHashKey_t,
    DsUserIdIngressDefault_t,
    DsUserIdIpv4HashKey_t,
    DsUserIdIpv4PortHashKey_t,
    DsUserIdIpv4Tcam_t,
    DsUserIdIpv6HashKey_t,
    DsUserIdIpv6Tcam_t,
    DsUserIdL2HashKey_t,
    DsUserIdMacHashKey_t,
    DsUserIdMacPortHashKey_t,
    DsUserIdMacTcam_t,
    DsUserIdPortCrossHashKey_t,
    DsUserIdPortHashKey_t,
    DsUserIdPortVlanCrossHashKey_t,
    DsUserIdSvlanCosHashKey_t,
    DsUserIdSvlanHashKey_t,
    DsUserIdVlanTcam_t,
};
static tbls_id_t dbg__tcamdsmem_tbl_id_list[]  = {
    DsAclIpv4Key0_t,
    DsAclIpv4Key1_t,
    DsAclIpv4Key2_t,
    DsAclIpv4Key3_t,
    DsAclIpv6Key0_t,
    DsAclIpv6Key1_t,
    DsAclMacKey0_t,
    DsAclMacKey1_t,
    DsAclMacKey2_t,
    DsAclMacKey3_t,
    DsAclMplsKey0_t,
    DsAclMplsKey1_t,
    DsAclMplsKey2_t,
    DsAclMplsKey3_t,
    DsAclQosIpv4Key_t,
    DsAclQosIpv6Key_t,
    DsAclQosMacKey_t,
    DsAclQosMplsKey_t,
    DsFcoeRouteKey_t,
    DsFcoeRpfKey_t,
    DsIpv4McastRouteKey_t,
    DsIpv4NatKey_t,
    DsIpv4PbrKey_t,
    DsIpv4RouteKey_t,
    DsIpv4RpfKey_t,
    DsIpv4UcastRouteKey_t,
    DsIpv6McastRouteKey_t,
    DsIpv6NatKey_t,
    DsIpv6PbrKey_t,
    DsIpv6RouteKey_t,
    DsIpv6RpfKey_t,
    DsIpv6UcastRouteKey_t,
    DsMacBridgeKey_t,
    DsMacIpv4Key_t,
    DsMacIpv6Key_t,
    DsMacLearningKey_t,
    DsTrillMcastRouteKey_t,
    DsTrillRouteKey_t,
    DsTrillUcastRouteKey_t,
    DsTunnelIdCapwapKey_t,
    DsTunnelIdIpv4Key_t,
    DsTunnelIdIpv6Key_t,
    DsTunnelIdPbbKey_t,
    DsTunnelIdTrillKey_t,
    DsUserIdIpv4Key_t,
    DsUserIdIpv6Key_t,
    DsUserIdMacKey_t,
    DsUserIdVlanKey_t,
};

dkit_modules_t gb_dkit_modules_list[MaxModId_m]= {
    {"bsr",   "BufRetrv",              1,   1,  dbg_bufretrv_tbl_id_list,        sizeof(dbg_bufretrv_tbl_id_list) / sizeof(tbls_id_t)              },
    {"bsr",   "BufStore",              1,   1,  dbg_bufstore_tbl_id_list,        sizeof(dbg_bufstore_tbl_id_list) / sizeof(tbls_id_t)              },
    {"mac",   "CpuMac",                1,   1,  dbg_cpumac_tbl_id_list,          sizeof(dbg_cpumac_tbl_id_list) / sizeof(tbls_id_t)                },
    {"share", "Ds&TCAM",               1,   1,  dbg_dsandtcam_tbl_id_list,       sizeof(dbg_dsandtcam_tbl_id_list) / sizeof(tbls_id_t)             },
    {"share", "DsAging",               1,   1,  dbg_dsaging_tbl_id_list,         sizeof(dbg_dsaging_tbl_id_list) / sizeof(tbls_id_t)               },
    {"share", "DynamicDs",             1,   1,  dbg_dynamicds_tbl_id_list,       sizeof(dbg_dynamicds_tbl_id_list) / sizeof(tbls_id_t)             },
    {"epe",   "ELoop",                 1,   1,  dbg_eloop_tbl_id_list,           sizeof(dbg_eloop_tbl_id_list) / sizeof(tbls_id_t)                 },
    {"epe",   "EpeAclQos",             1,   1,  dbg_epeaclqos_tbl_id_list,       sizeof(dbg_epeaclqos_tbl_id_list) / sizeof(tbls_id_t)             },
    {"epe",   "EpeCla",                1,   1,  dbg_epecla_tbl_id_list,          sizeof(dbg_epecla_tbl_id_list) / sizeof(tbls_id_t)                },
    {"epe",   "EpeHdrAdj",             1,   1,  dbg_epehdradj_tbl_id_list,       sizeof(dbg_epehdradj_tbl_id_list) / sizeof(tbls_id_t)             },
    {"epe",   "EpeHdrEdit",            1,   1,  dbg_epehdredit_tbl_id_list,      sizeof(dbg_epehdredit_tbl_id_list) / sizeof(tbls_id_t)            },
    {"epe",   "EpeHdrProc",            1,   1,  dbg_epehdrproc_tbl_id_list,      sizeof(dbg_epehdrproc_tbl_id_list) / sizeof(tbls_id_t)            },
    {"epe",   "EpeNextHop",            1,   1,  dbg_epenexthop_tbl_id_list,      sizeof(dbg_epenexthop_tbl_id_list) / sizeof(tbls_id_t)            },
    {"epe",   "EpeOam",                1,   1,  dbg_epegb_oam_tbl_id_list,          sizeof(dbg_epegb_oam_tbl_id_list) / sizeof(tbls_id_t)                },
    {"misc",  "GlobalStats",           1,   1,  dbg_globalstats_tbl_id_list,     sizeof(dbg_globalstats_tbl_id_list) / sizeof(tbls_id_t)           },
    {"misc",  "GreatbeltSup",          1,   1,  dbg_greatbeltsup_tbl_id_list,    sizeof(dbg_greatbeltsup_tbl_id_list) / sizeof(tbls_id_t)          },
    {"share", "HashDs",                1,   1,  dbg_hashds_tbl_id_list,          sizeof(dbg_hashds_tbl_id_list) / sizeof(tbls_id_t)                },
    {"misc",  "I2CMaster",             1,   1,  dbg_i2cmaster_tbl_id_list,       sizeof(dbg_i2cmaster_tbl_id_list) / sizeof(tbls_id_t)             },
    {"misc",  "IntLkIntf",             1,   1,  dbg_intlkintf_tbl_id_list,       sizeof(dbg_intlkintf_tbl_id_list) / sizeof(tbls_id_t)             },
    {"ipe",   "IpeFib",                1,   1,  dbg_ipefib_tbl_id_list,          sizeof(dbg_ipefib_tbl_id_list) / sizeof(tbls_id_t)                },
    {"ipe",   "IpeForward",            1,   1,  dbg_ipeforward_tbl_id_list,      sizeof(dbg_ipeforward_tbl_id_list) / sizeof(tbls_id_t)            },
    {"ipe",   "IpeHdrAdj",             1,   1,  dbg_ipehdradj_tbl_id_list,       sizeof(dbg_ipehdradj_tbl_id_list) / sizeof(tbls_id_t)             },
    {"ipe",   "IpeIntfMap",            1,   1,  dbg_ipeintfmap_tbl_id_list,      sizeof(dbg_ipeintfmap_tbl_id_list) / sizeof(tbls_id_t)            },
    {"ipe",   "IpeLkupMgr",            1,   1,  dbg_ipelkupmgr_tbl_id_list,      sizeof(dbg_ipelkupmgr_tbl_id_list) / sizeof(tbls_id_t)            },
    {"ipe",   "IpePktProc",            1,   1,  dbg_ipepktproc_tbl_id_list,      sizeof(dbg_ipepktproc_tbl_id_list) / sizeof(tbls_id_t)            },
    {"misc",  "MacLedDriver",          1,   1,  dbg_macleddriver_tbl_id_list,    sizeof(dbg_macleddriver_tbl_id_list) / sizeof(tbls_id_t)          },
    {"mac",   "MacMux",                1,   1,  dbg_macmux_tbl_id_list,          sizeof(dbg_macmux_tbl_id_list) / sizeof(tbls_id_t)                },
    {"misc",  "Mdio",                  1,   1,  dbg_mdio_tbl_id_list,            sizeof(dbg_mdio_tbl_id_list) / sizeof(tbls_id_t)                  },
    {"bsr",   "MetFifo",               1,   1,  dbg_metfifo_tbl_id_list,         sizeof(dbg_metfifo_tbl_id_list) / sizeof(tbls_id_t)               },
    {"ipe",   "NetRx",                 1,   1,  dbg_netrx_tbl_id_list,           sizeof(dbg_netrx_tbl_id_list) / sizeof(tbls_id_t)                 },
    {"epe",   "NetTx",                 1,   1,  dbg_nettx_tbl_id_list,           sizeof(dbg_nettx_tbl_id_list) / sizeof(tbls_id_t)                 },
    {"oam",   "OamAutoGenPkt",         1,   1,  dbg_oamautogenpkt_tbl_id_list,   sizeof(dbg_oamautogenpkt_tbl_id_list) / sizeof(tbls_id_t)         },
    {"oam",   "OamFwd",                1,   1,  dbg_oamfwd_tbl_id_list,          sizeof(dbg_oamfwd_tbl_id_list) / sizeof(tbls_id_t)                },
    {"oam",   "OamParser",             1,   1,  dbg_oamparser_tbl_id_list,       sizeof(dbg_oamparser_tbl_id_list) / sizeof(tbls_id_t)             },
    {"oam",   "OamProc",               1,   1,  dbg_oamproc_tbl_id_list,         sizeof(dbg_oamproc_tbl_id_list) / sizeof(tbls_id_t)               },
    {"misc",  "OobFc",                 1,   1,  dbg_oobfc_tbl_id_list,           sizeof(dbg_oobfc_tbl_id_list) / sizeof(tbls_id_t)                 },
    {"share", "Parser",                1,   1,  dbg_parser_tbl_id_list,          sizeof(dbg_parser_tbl_id_list) / sizeof(tbls_id_t)                },
    {"misc",  "PbCtl",                 1,   1,  dbg_pbctl_tbl_id_list,           sizeof(dbg_pbctl_tbl_id_list) / sizeof(tbls_id_t)                 },
    {"misc",  "PciExpCore",            1,   1,  dbg_pciexpcore_tbl_id_list,      sizeof(dbg_pciexpcore_tbl_id_list) / sizeof(tbls_id_t)            },
    {"share", "Policing",              1,   1,  dbg_policing_tbl_id_list,        sizeof(dbg_policing_tbl_id_list) / sizeof(tbls_id_t)              },
    {"misc",  "PtpEngine",             1,   1,  dbg_ptpengine_tbl_id_list,       sizeof(dbg_ptpengine_tbl_id_list) / sizeof(tbls_id_t)             },
    {"misc",  "PtpFrc",                1,   1,  dbg_ptpfrc_tbl_id_list,          sizeof(dbg_ptpfrc_tbl_id_list) / sizeof(tbls_id_t)                },
    {"bsr",   "QMgrDeq",               1,   1,  dbg_qmgrdeq_tbl_id_list,         sizeof(dbg_qmgrdeq_tbl_id_list) / sizeof(tbls_id_t)               },
    {"bsr",   "QMgrEnq",               1,   1,  dbg_qmgrenq_tbl_id_list,         sizeof(dbg_qmgrenq_tbl_id_list) / sizeof(tbls_id_t)               },
    {"bsr",   "QMgrQueEntry",          1,   1,  dbg_qmgrqueentry_tbl_id_list,    sizeof(dbg_qmgrqueentry_tbl_id_list) / sizeof(tbls_id_t)          },
    {"misc",  "Qsgmii",                12,  1,  dbg_qsgmii_tbl_id_list,          sizeof(dbg_qsgmii_tbl_id_list) / sizeof(tbls_id_t)                },
    {"mac",   "QuadMac",               12,  4,  dbg_quadmac_tbl_id_list,         sizeof(dbg_quadmac_tbl_id_list) / sizeof(tbls_id_t)               },
    {"misc",  "QuadPcs",               6,   1,  dbg_quadpcs_tbl_id_list,         sizeof(dbg_quadpcs_tbl_id_list) / sizeof(tbls_id_t)               },
    {"mac",   "Sgmac",                 12,  1,  dbg_sgmac_tbl_id_list,           sizeof(dbg_sgmac_tbl_id_list) / sizeof(tbls_id_t)                 },
    {"share", "SharedDs",              1,   1,  dbg_sharedds_tbl_id_list,        sizeof(dbg_sharedds_tbl_id_list) / sizeof(tbls_id_t)              },
    {"share", "TcamCtlInt",            1,   1,  dbg_tcamctlint_tbl_id_list,      sizeof(dbg_tcamctlint_tbl_id_list) / sizeof(tbls_id_t)            },
    {"share", "TcamDs",                1,   1,  dbg_tcamds_tbl_id_list,          sizeof(dbg_tcamds_tbl_id_list) / sizeof(tbls_id_t)                },
    {"share", "DsFib",                 1,   1,  dbg__dsfib_tbl_id_list,          sizeof(dbg__dsfib_tbl_id_list) / sizeof(tbls_id_t)                },
    {"share", "DynamicDsMem",          1,   1,  dbg__dynamicdsmem_tbl_id_list,   sizeof(dbg__dynamicdsmem_tbl_id_list) / sizeof(tbls_id_t)         },
    {"bsr",   "FwdMsg",                1,   1,  dbg__fwdmessage_tbl_id_list,     sizeof(dbg__fwdmessage_tbl_id_list) / sizeof(tbls_id_t)           },
    {"share", "HashDsMem",             1,   1,  dbg__hashdsmem_tbl_id_list,      sizeof(dbg__hashdsmem_tbl_id_list) / sizeof(tbls_id_t)            },
    {"share", "TcamDsMem",             1,   1,  dbg__tcamdsmem_tbl_id_list,      sizeof(dbg__tcamdsmem_tbl_id_list) / sizeof(tbls_id_t)            },
};

 /*#debug stats*/
dkit_debug_stats_t dkit_debug_stats[] = {
    {"bsr",   "BufRetrv",       "BufRetrvFlowCtlInfoDebug"},
    {"bsr",   "BufRetrv",       "BufRetrvInputDebugStats"},
    {"bsr",   "BufRetrv",       "BufRetrvIntfMemAddrDebug"},
    {"bsr",   "BufRetrv",       "BufRetrvMiscDebugStats"},
    {"bsr",   "BufRetrv",       "BufRetrvOutputPktDebugStats"},
    {"bsr",   "BufStore",       "BufStoreCreditDebug"},
    {"bsr",   "BufStore",       "BufStoreEccErrorDebugStats"},
    {"bsr",   "BufStore",       "BufStoreInputDebugStats"},
    {"bsr",   "BufStore",       "BufStoreOutputDebugStats"},
    {"bsr",   "BufStore",       "BufStorePbCreditRunOutDebugStats"},
    {"bsr",   "BufStore",       "BufStoreStallDropDebugStats"},
    {"mac",   "CpuMac",         "CpuMacDebugStats"},
    {"mac",   "CpuMac",         "CpuMacStats"},
    {"share", "DsAging",        "DsAgingDebugStats"},
    {"share", "DsAging",        "DsAgingDebugStatus"},
    {"share", "DynamicDs",      "DynamicDsCreditUsed"},
    {"share", "DynamicDs",      "DynamicDsDebugInfo"},
    {"share", "DynamicDs",      "DynamicDsDebugStats"},
    {"epe",   "ELoop",          "ELoopDebugStats"},
    {"epe",   "EpeAclQos",      "EpeAclQosDebugStats"},
    {"epe",   "EpeAclQos",      "ToClaCreditDebug"},
    {"epe",   "EpeCla",         "EpeClaDebugStats"},
    {"epe",   "EpeHdrAdj",      "EpeHdrAdjDebugStats"},
    {"epe",   "EpeHdrAdj",      "EpeHdrAdjRunningCredit"},
    {"epe",   "EpeHdrAdj",      "EpeHdrAdjStallInfo"},
    {"epe",   "EpeHdrEdit",     "EpeHdrEditDebugStats"},
    {"epe",   "EpeHdrEdit",     "EpeHdrEditExcepInfo"},
    {"epe",   "EpeHdrEdit",     "EpeHdrEditState"},
    {"epe",   "EpeHdrProc",     "EpeHdrProcDebugStats"},
    {"epe",   "EpeNextHop",     "EpeNextHopDebugStats"},
    {"epe",   "EpeNextHop",     "EpeNextHopDebugStatus"},
    {"epe",   "EpeOam",         "EpeOamCreditDebug"},
    {"epe",   "EpeOam",         "EpeOamDebugStats"},
    {"misc",  "GlobalStats",    "GlobalStatsDbgStatus"},
    {"misc",  "GlobalStats",    "GlobalStatsEpeReqDropCnt"},
    {"misc",  "GlobalStats",    "GlobalStatsIpeReqDropCnt"},
    {"misc",  "GlobalStats",    "GlobalStatsPolicingReqDropCnt"},
    {"misc",  "GlobalStats",    "GlobalStatsQMgrReqDropCnt"},
    {"share", "HashDs",         "HashDsCreditUsed"},
    {"share", "HashDs",         "HashDsDebugStats"},
    {"share", "HashDs",         "HashDsKeyLkupResultDebugInfo"},
    {"misc",  "IntLkIntf",      "IntLkCreditDebug"},
    {"misc",  "IntLkIntf",      "IntLkLaneRxDebugState"},
    {"misc",  "IntLkIntf",      "IntLkLaneRxStats"},
    {"misc",  "IntLkIntf",      "IntLkLaneTxState"},
    {"misc",  "IntLkIntf",      "IntLkPktStats"},
    {"share", "IpeFib",         "FibCreditDebug"},
    {"share", "IpeFib",         "FibDebugStats"},
    {"ipe",   "IpeForward",     "IpeFwdDebugStats"},
    {"ipe",   "IpeHdrAdj",      "IpeHdrAdjCreditUsed"},
    {"ipe",   "IpeHdrAdj",      "IpeHdrAdjDebugStats"},
    {"ipe",   "IpeHdrAdj",      "IpeHdrAdjEccStats"},
    {"ipe",   "IpeIntfMap",     "DsMplsCtl_RegRam_RamChkRec"},
    {"ipe",   "IpeIntfMap",     "DsPhyPortExt_RegRam_RamChkRec"},
    {"ipe",   "IpeIntfMap",     "DsRouterMac_RegRam_RamChkRec"},
    {"ipe",   "IpeIntfMap",     "DsSrcInterface_RegRam_RamChkRec"},
    {"ipe",   "IpeIntfMap",     "DsSrcPort_RegRam_RamChkRec"},
    {"ipe",   "IpeIntfMap",     "DsVlanActionProfile_RegRam_RamChkRec"},
    {"ipe",   "IpeIntfMap",     "DsVlanRangeProfile_RegRam_RamChkRec"},
    {"ipe",   "IpeIntfMap",     "ExpInfoFifo_FifoAlmostFullThrd"},
    {"ipe",   "IpeIntfMap",     "ImPktDataFifo_FifoAlmostFullThrd"},
    {"ipe",   "IpeIntfMap",     "IpeIntfMapDsStpStateStats"},
    {"ipe",   "IpeIntfMap",     "IpeIntfMapDsVlanProfStats"},
    {"ipe",   "IpeIntfMap",     "IpeIntfMapDsVlanStats"},
    {"ipe",   "IpeIntfMap",     "IpeIntfMapInfoOutStats"},
    {"ipe",   "IpeIntfMap",     "IpeIntfMapStats1"},
    {"ipe",   "IpeIntfMap",     "IpeIntfMapStats2"},
    {"ipe",   "IpeIntfMap",     "IpeIntfMapStats3"},
    {"ipe",   "IpeIntfMap",     "IpeIntfMapStats4"},
    {"ipe",   "IpeLkupMgr",     "IpeLkupMgrDebugStats"},
    {"ipe",   "IpePktProc",     "IpePktProcCreditUsed"},
    {"ipe",   "IpePktProc",     "IpePktProcDebugStats"},
    {"ipe",   "IpePktProc",     "IpePktProcDsFwdPtrDebug"},
    {"ipe",   "IpePktProc",     "IpePktProcEccStats"},
    {"mac",   "MacMux",         "MacMuxDebugStats"},
    {"bsr",   "MetFifo",        "MetFifoDebugStats"},
    {"bsr",   "MetFifo",        "MetFifoExcpTabParityFailRecord"},
    {"bsr",   "MetFifo",        "MetFifoLinkDownStateRecord"},
    {"bsr",   "MetFifo",        "MetFifoLinkScanDoneState"},
    {"bsr",   "MetFifo",        "MetFifoMsgTypeCnt"},
    {"bsr",   "MetFifo",        "MetFifoRdCurStateMachine"},
    {"bsr",   "MetFifo",        "MetFifoUpdateRcdErrorSpot"},
    {"ipe",   "NetRx",          "NetRxBpduStats"},
    {"ipe",   "NetRx",          "NetRxBufferCount"},
    {"ipe",   "NetRx",          "NetRxDebugStats"},
    {"ipe",   "NetRx",          "NetRxEccErrorStats"},
    {"ipe",   "NetRx",          "NetRxIpeStallRecord"},
    {"ipe",   "NetRx",          "NetRxPreFetchFifoDepth"},
    {"ipe",   "NetRx",          "NetRxState"},
    {"epe",   "NetTx",          "NetTxChanCreditUsed"},
    {"epe",   "NetTx",          "NetTxDebugStats"},
    {"epe",   "NetTx",          "NetTxELoopStallRecord"},
    {"oam",   "OamAutoGenPkt",  "AutoGenPktDebugStats"},
    {"oam",   "OamAutoGenPkt",  "AutoGenPktEccStats"},
    {"oam",   "OamFwd",         "OamFwdDebugStats"},
    {"oam",   "OamFwd",         "OamHdrEditDebugStats"},
    {"oam",   "OamParser",      "OamParserDebugCnt"},
    {"oam",   "OamProc",        "DsOamDefectStatus"},
    {"oam",   "OamProc",        "OamProcDebugStats"},
    {"oam",   "OamProc",        "OamProcEccStats"},
    {"oam",   "OamProc",        "OamUpdateStatus"},
    {"misc",  "OobFc",          "OobFcDebugStats"},
    {"misc",  "OobFc",          "OobFcErrorStats"},
    {"misc",  "OobFc",          "OobFcFrameUpdateState"},
    {"misc",  "PbCtl",          "PbCtlDebugStats"},
    {"misc",  "PbCtl",          "PbCtlParityFailRecord"},
    {"misc",  "PbCtl",          "PbCtlReqHoldStats"},
    {"share", "Policing",       "DsPolicerControl_RamChkRec"},
    {"share", "Policing",       "PolicingDebugStats"},
    {"share", "Policing",       "UpdateReqIgnoreRecord"},
    {"misc",  "PtpEngine",      "PtpTsCaptureDropCnt"},
    {"bsr",   "QMgrDeq",        "QMgrChanShapeState"},
    {"bsr",   "QMgrDeq",        "QMgrDeqDebugStats"},
    {"bsr",   "QMgrEnq",        "QMgrEnqCreditUsed"},
    {"bsr",   "QMgrEnq",        "QMgrEnqDebugFsm"},
    {"bsr",   "QMgrEnq",        "QMgrEnqDebugStats"},
    {"bsr",   "QMgrEnq",        "QMgrQueueIdMon"},
    {"bsr",   "QMgrQueEntry",   "QMgrQueEntryStats"},
    {"mac",   "QuadMac",        "QuadMacStatsRam0"},
    {"mac",   "QuadMac",        "QuadMacDebugStats0"},
    {"mac",   "QuadMac",        "QuadMacGmac0PtpStatus0"},
    {"mac",   "QuadMac",        "QuadMacGmac1PtpStatus0"},
    {"mac",   "QuadMac",        "QuadMacGmac2PtpStatus0"},
    {"mac",   "QuadMac",        "QuadMacGmac3PtpStatus0"},
    {"mac",   "QuadMac",        "QuadMacStatusOverWrite0"},
    {"mac",   "Sgmac",          "SgmacStatsRam0"},
    {"mac",   "Sgmac",          "SgmacDebugStatus0"},
    {"mac",   "Sgmac",          "SgmacPtpStatus0"},
    {"mac",   "Sgmac",          "SgmacXfiDebugStats0"},
    {"mac",   "SharedDs",       "SharedDsEccErrStats"},
    {"share", "TcamCtlInt",     "TcamCtlIntDebugStats"},
    {"share", "TcamCtlInt",     "TcamCtlIntState"},
    {"share", "TcamDs",         "TcamDsDebugStats"},
};

uint32
ctc_greatbelt_dkit_drv_get_bus_list_num(void)
{
   return sizeof(gb_dkit_bus_list)/sizeof(dkit_bus_t);
}

bool
ctc_greatbelt_dkit_drv_get_tbl_id_by_bus_name(char *str, uint32 *tbl_id)
{
    uint32 cn = 0;
    uint32 bus_cnt = 0;

    if ((NULL == str) || (NULL == tbl_id))
    {
        return FALSE;
    }

    bus_cnt = ctc_greatbelt_dkit_drv_get_bus_list_num();

    for (cn = 0 ; cn < bus_cnt; cn++)
    {
        if (0 == sal_strcasecmp(str, gb_dkit_bus_list[cn].bus_name))
        {
            *tbl_id = DKIT_BUS_GET_FIFO_ID(cn, 0);

            return TRUE;
        }
    }

    return FALSE;
}


STATIC int32
_ctc_greatbelt_dkit_drv_bus_register_init(void)
{
    uint32 cn = 0;
    uint32 bus_num = 0;
    uint32 cluter_num = 0;
    tables_info_t* ptr = NULL;
    cluster_t* p_cluster_node = NULL;

    bus_num  = ctc_greatbelt_dkit_drv_get_bus_list_num();

    for (cn = 0; cn < bus_num; cn++)
    {
        for (cluter_num = 0; cluter_num < DKIT_BUS_GET_CLUSTER_NUM(cn); cluter_num++)
        {
            p_cluster_node = DKIT_BUS_GET_INFOPTR(cn, cluter_num);

            ptr = TABLE_INFO_PTR(p_cluster_node->bus_fifo_id);

            ptr->entry_size = p_cluster_node->entry_size;
            ptr->max_index_num = p_cluster_node->max_index;

            ptr->num_fields = p_cluster_node->num_fields;
            ptr->ptr_fields = p_cluster_node->per_fields;
        }
    }

    return DRV_E_NONE;
}

bool
ctc_greatbelt_dkit_drv_get_module_id_by_string(char *str, uint32 *id)
{
    uint32 cn = 0;
    if((NULL == str)||(NULL == id))
    {
        return FALSE;
    }

    for(cn =0 ; cn < Mod_IPE; cn++)
    {
        if(0 == sal_strcasecmp(str, gb_dkit_modules_list[cn].module_name))
        {
            *id = cn;
            return TRUE;
        }
    }

    return FALSE;
}







int32
_ctc_greatbelt_dkit_drv_register_stat_info(tbls_id_t id)
{
    tables_info_t* ptr = NULL;
    tbls_ext_info_t* new_node = NULL, * temp_node = NULL;

    DRV_TBL_ID_VALID_CHECK(id);
    new_node = sal_malloc(sizeof(tbls_ext_info_t));
    if (NULL == new_node)
    {
        return DRV_E_NO_MEMORY;
    }

    ptr = TABLE_INFO_PTR(id);

    new_node->ext_content_type = EXT_INFO_TYPE_DBG;
    new_node->ptr_ext_content = NULL;
    new_node->ptr_next = NULL;

    if (NULL == ptr->ptr_ext_info)
    {
        ptr->ptr_ext_info = new_node;
    }
    else
    {
        temp_node = ptr->ptr_ext_info;

        while (temp_node->ptr_next)
        {
            temp_node = temp_node->ptr_next;
        }

        temp_node->ptr_next = new_node;
    }

    return DRV_E_NONE;
}


int32
_ctc_greatbelt_dkit_drv_stat_info_register(char* module_name, char* tbl_reg_name)
{
    dkit_mod_id_t module_id = MaxModId_m;
    tbls_id_t tbl_reg_id_base = MaxTblId_t;
    tbls_id_t tbl_reg_id = MaxTblId_t;
    dkit_modules_t* ptr_module = NULL;
    uint32 inst_num = 0;
    uint32 cn = 0;

    if ((NULL == module_name) || (NULL == tbl_reg_name))
    {
        return CLI_ERROR;
    }

    if (!ctc_greatbelt_dkit_drv_get_module_id_by_string(module_name, &module_id))
    {
        return CLI_ERROR;
    }
    if (module_id == MaxModId_m)
    {
        return CLI_ERROR;
    }
    if (DRV_E_NONE != drv_greatbelt_get_tbl_id_by_string(&tbl_reg_id_base, tbl_reg_name))
    {
        return CLI_ERROR;
    }

    ptr_module  = DKIT_MODULE_GET_INFOPTR(module_id);
    inst_num    = ptr_module->inst_num;

    for (cn = 0; cn < inst_num; cn++)
    {
        tbl_reg_id = tbl_reg_id_base + cn;
        _ctc_greatbelt_dkit_drv_register_stat_info(tbl_reg_id);
    }

    return CLI_SUCCESS;
}


STATIC int32
_ctc_greatbelt_dkit_drv_module_register_init(void)
{
    int32 num = 0;
    int32 i = 0;
    num = sizeof(dkit_debug_stats) / sizeof(dkit_debug_stats_t);

    for (i = 0; i < num; i++)
    {
        _ctc_greatbelt_dkit_drv_stat_info_register(dkit_debug_stats[i].module_name, dkit_debug_stats[i].tbl_name);
    }

    return DRV_E_NONE;
}

int32
ctc_greatbelt_dkit_drv_register(void)
{
    _ctc_greatbelt_dkit_drv_module_register_init();
    _ctc_greatbelt_dkit_drv_bus_register_init();

    return CLI_SUCCESS;
}


