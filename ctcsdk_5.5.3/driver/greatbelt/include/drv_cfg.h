/*
 * Revision      : $Revision: 1.56 $
 * Spec version  : v5.27
 * Last Upadated : $Date: 2012/05/25 06:57:06 $
 * Modified by   : $Author: fengy $
 * Exported      : $Exp$
 *
 * Copyright (C) Centec Networks, Inc. 2005-2011
 * Proprietary and Confidential, all Rights Reserved
*/

/**
 ##################################################################################
 It is automatically generated with RegAutoGen, developed by Samuel Hu
 ##################################################################################
*/

#ifndef _DRV_CFG_H_
#define _DRV_CFG_H_

#define C_MODEL_BASE        0x0

/**********************************************************************************
              Define Typedef, define and Data Structure
***********************************************************************************/
#define BUF_RETRV_BUF_CONFIG_MEM_MAX_INDEX                           64
#define BUF_RETRV_BUF_CREDIT_CONFIG_MEM_MAX_INDEX                    64
#define BUF_RETRV_BUF_CREDIT_MEM_MAX_INDEX                           64
#define BUF_RETRV_BUF_PTR_MEM_MAX_INDEX                              320
#define BUF_RETRV_BUF_STATUS_MEM_MAX_INDEX                           64
#define BUF_RETRV_CREDIT_CONFIG_MEM_MAX_INDEX                        64
#define BUF_RETRV_CREDIT_MEM_MAX_INDEX                               64
#define BUF_RETRV_INTERRUPT_FATAL_MAX_INDEX                          4
#define BUF_RETRV_INTERRUPT_NORMAL_MAX_INDEX                         4
#define BUF_RETRV_MSG_PARK_MEM_MAX_INDEX                             64
#define BUF_RETRV_PKT_BUF_TRACK_FIFO_MAX_INDEX                       16
#define BUF_RETRV_PKT_CONFIG_MEM_MAX_INDEX                           64
#define BUF_RETRV_PKT_MSG_MEM_MAX_INDEX                              320
#define BUF_RETRV_PKT_STATUS_MEM_MAX_INDEX                           64
#define DS_BUF_RETRV_COLOR_MAP_MAX_INDEX                             128
#define DS_BUF_RETRV_EXCP_MAX_INDEX                                  256
#define DS_BUF_RETRV_PRIORITY_CTL_MAX_INDEX                          256
#define BUF_RETRV_BUF_CREDIT_CONFIG_MAX_INDEX                        1
#define BUF_RETRV_BUF_PTR_MEM__REG_RAM__RAM_CHK_REC_MAX_INDEX        1
#define BUF_RETRV_BUF_PTR_SLOT_CONFIG_MAX_INDEX                      1
#define BUF_RETRV_BUF_WEIGHT_CONFIG_MAX_INDEX                        1
#define BUF_RETRV_CHAN_ID_CFG_MAX_INDEX                              1
#define BUF_RETRV_CREDIT_CONFIG_MAX_INDEX                            1
#define BUF_RETRV_CREDIT_MODULE_ENABLE_CONFIG_MAX_INDEX              1
#define BUF_RETRV_DRAIN_ENABLE_MAX_INDEX                             1
#define BUF_RETRV_FLOW_CTL_INFO_DEBUG_MAX_INDEX                      1
#define BUF_RETRV_INIT_MAX_INDEX                                     1
#define BUF_RETRV_INIT_DONE_MAX_INDEX                                1
#define BUF_RETRV_INPUT_DEBUG_STATS_MAX_INDEX                        1
#define BUF_RETRV_INTF_FIFO_CONFIG_CREDIT_MAX_INDEX                  1
#define BUF_RETRV_INTF_MEM_ADDR_CONFIG_MAX_INDEX                     1
#define BUF_RETRV_INTF_MEM_ADDR_DEBUG_MAX_INDEX                      1
#define BUF_RETRV_INTF_MEM_PARITY_RECORD_MAX_INDEX                   1
#define BUF_RETRV_MISC_CONFIG_MAX_INDEX                              1
#define BUF_RETRV_MISC_DEBUG_STATS_MAX_INDEX                         1
#define BUF_RETRV_MSG_PARK_MEM__REG_RAM__RAM_CHK_REC_MAX_INDEX       1
#define BUF_RETRV_OUTPUT_PKT_DEBUG_STATS_MAX_INDEX                   1
#define BUF_RETRV_OVERRUN_PORT_MAX_INDEX                             1
#define BUF_RETRV_PARITY_ENABLE_MAX_INDEX                            1
#define BUF_RETRV_PKT_BUF_MODULE_CONFIG_CREDIT_MAX_INDEX             1
#define BUF_RETRV_PKT_BUF_REQ_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX  1
#define BUF_RETRV_PKT_BUF_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX 1
#define BUF_RETRV_PKT_MSG_MEM__REG_RAM__RAM_CHK_REC_MAX_INDEX        1
#define BUF_RETRV_PKT_WEIGHT_CONFIG_MAX_INDEX                        1
#define BUF_RETRV_STACKING_EN_MAX_INDEX                              1
#define BUFFER_RETRIEVE_CTL_MAX_INDEX                                1
#define DS_BUF_RETRV_EXCP__REG_RAM__RAM_CHK_REC_MAX_INDEX            1
#define BUF_PTR_MEM_MAX_INDEX                                        12288
#define BUF_STORE_INTERRUPT_FATAL_MAX_INDEX                          4
#define BUF_STORE_INTERRUPT_NORMAL_MAX_INDEX                         4
#define CHAN_INFO_RAM_MAX_INDEX                                      128
#define DS_IGR_COND_DIS_PROF_ID_MAX_INDEX                            96
#define DS_IGR_PORT_CNT_MAX_INDEX                                    192
#define DS_IGR_PORT_TC_CNT_MAX_INDEX                                 768
#define DS_IGR_PORT_TC_MIN_PROF_ID_MAX_INDEX                         96
#define DS_IGR_PORT_TC_PRI_MAP_MAX_INDEX                             64
#define DS_IGR_PORT_TC_THRD_PROF_ID_MAX_INDEX                        192
#define DS_IGR_PORT_TC_THRD_PROFILE_MAX_INDEX                        256
#define DS_IGR_PORT_THRD_PROF_ID_MAX_INDEX                           24
#define DS_IGR_PORT_THRD_PROFILE_MAX_INDEX                           64
#define DS_IGR_PORT_TO_TC_PROF_ID_MAX_INDEX                          24
#define DS_IGR_PRI_TO_TC_MAP_MAX_INDEX                               256
#define DS_SRC_SGMAC_GROUP_MAX_INDEX                                 64
#define IPE_HDR_ERR_STATS_MEM_MAX_INDEX                              64
#define IPE_HDR_MSG_FIFO_MAX_INDEX                                   32
#define MET_FIFO_PRIORITY_MAP_TABLE_MAX_INDEX                        256
#define PKT_ERR_STATS_MEM_MAX_INDEX                                  128
#define PKT_MSG_FIFO_MAX_INDEX                                       64
#define PKT_RESRC_ERR_STATS_MAX_INDEX                                128
#define BUF_STORE_CHAN_INFO_CTL_MAX_INDEX                            1
#define BUF_STORE_CPU_MAC_PAUSE_RECORD_MAX_INDEX                     1
#define BUF_STORE_CPU_MAC_PAUSE_REQ_CTL_MAX_INDEX                    1
#define BUF_STORE_CREDIT_DEBUG_MAX_INDEX                             1
#define BUF_STORE_CREDIT_THD_MAX_INDEX                               1
#define BUF_STORE_ECC_CTL_MAX_INDEX                                  1
#define BUF_STORE_ECC_ERROR_DEBUG_STATS_MAX_INDEX                    1
#define BUF_STORE_ERR_STATS_MEM_CTL_MAX_INDEX                        1
#define BUF_STORE_FIFO_CTL_MAX_INDEX                                 1
#define BUF_STORE_FREE_LIST_CTL_MAX_INDEX                            1
#define BUF_STORE_GCN_CTL_MAX_INDEX                                  1
#define BUF_STORE_INPUT_DEBUG_STATS_MAX_INDEX                        1
#define BUF_STORE_INPUT_FIFO_ARB_CTL_MAX_INDEX                       1
#define BUF_STORE_INT_LK_RESRC_CTL_MAX_INDEX                         1
#define BUF_STORE_INT_LK_STALL_INFO_MAX_INDEX                        1
#define BUF_STORE_INTERNAL_STALL_CTL_MAX_INDEX                       1
#define BUF_STORE_LINK_LIST_SLOT_MAX_INDEX                           1
#define BUF_STORE_LINK_LIST_TABLE_CTL_MAX_INDEX                      1
#define BUF_STORE_MET_FIFO_STALL_CTL_MAX_INDEX                       1
#define BUF_STORE_MISC_CTL_MAX_INDEX                                 1
#define BUF_STORE_MSG_DROP_CTL_MAX_INDEX                             1
#define BUF_STORE_NET_NORMAL_PAUSE_RECORD_MAX_INDEX                  1
#define BUF_STORE_NET_PFC_RECORD_MAX_INDEX                           1
#define BUF_STORE_NET_PFC_REQ_CTL_MAX_INDEX                          1
#define BUF_STORE_OOB_FC_CTL_MAX_INDEX                               1
#define BUF_STORE_OUTPUT_DEBUG_STATS_MAX_INDEX                       1
#define BUF_STORE_PARITY_FAIL_RECORD_MAX_INDEX                       1
#define BUF_STORE_PB_CREDIT_RUN_OUT_DEBUG_STATS_MAX_INDEX            1
#define BUF_STORE_RESRC_RAM_CTL_MAX_INDEX                            1
#define BUF_STORE_SGMAC_CTL_MAX_INDEX                                1
#define BUF_STORE_STALL_DROP_DEBUG_STATS_MAX_INDEX                   1
#define BUF_STORE_STATS_CTL_MAX_INDEX                                1
#define BUF_STORE_TOTAL_RESRC_INFO_MAX_INDEX                         1
#define BUFFER_STORE_CTL_MAX_INDEX                                   1
#define BUFFER_STORE_FORCE_LOCAL_CTL_MAX_INDEX                       1
#define DS_IGR_COND_DIS_PROF_ID__REG_RAM__RAM_CHK_REC_MAX_INDEX      1
#define DS_IGR_PORT_CNT__REG_RAM__RAM_CHK_REC_MAX_INDEX              1
#define DS_IGR_PORT_TC_CNT__REG_RAM__RAM_CHK_REC_MAX_INDEX           1
#define DS_IGR_PORT_TC_MIN_PROF_ID__REG_RAM__RAM_CHK_REC_MAX_INDEX   1
#define DS_IGR_PORT_TC_PRI_MAP__REG_RAM__RAM_CHK_REC_MAX_INDEX       1
#define DS_IGR_PORT_TC_THRD_PROF_ID__REG_RAM__RAM_CHK_REC_MAX_INDEX  1
#define DS_IGR_PORT_TC_THRD_PROFILE__REG_RAM__RAM_CHK_REC_MAX_INDEX  1
#define DS_IGR_PORT_THRD_PROFILE__REG_RAM__RAM_CHK_REC_MAX_INDEX     1
#define DS_IGR_PRI_TO_TC_MAP__REG_RAM__RAM_CHK_REC_MAX_INDEX         1
#define IGR_COND_DIS_PROFILE_MAX_INDEX                               1
#define IGR_CONGEST_LEVEL_THRD_MAX_INDEX                             1
#define IGR_GLB_DROP_COND_CTL_MAX_INDEX                              1
#define IGR_PORT_TC_MIN_PROFILE_MAX_INDEX                            1
#define IGR_RESRC_MGR_MISC_CTL_MAX_INDEX                             1
#define IGR_SC_CNT_MAX_INDEX                                         1
#define IGR_SC_THRD_MAX_INDEX                                        1
#define IGR_TC_CNT_MAX_INDEX                                         1
#define IGR_TC_THRD_MAX_INDEX                                        1
#define NET_LOCAL_PHY_PORT_FC_RECORD_MAX_INDEX                       1
#define CPU_MAC_INTERRUPT_NORMAL_MAX_INDEX                           4
#define CPU_MAC_PRIORITY_MAP_MAX_INDEX                               64
#define CPU_MAC_CREDIT_CFG_MAX_INDEX                                 1
#define CPU_MAC_CREDIT_CTL_MAX_INDEX                                 1
#define CPU_MAC_CTL_MAX_INDEX                                        1
#define CPU_MAC_DA_CFG_MAX_INDEX                                     1
#define CPU_MAC_DEBUG_STATS_MAX_INDEX                                1
#define CPU_MAC_INIT_CTL_MAX_INDEX                                   1
#define CPU_MAC_MISC_CTL_MAX_INDEX                                   1
#define CPU_MAC_PAUSE_CTL_MAX_INDEX                                  1
#define CPU_MAC_PCS_ANEG_CFG_MAX_INDEX                               1
#define CPU_MAC_PCS_ANEG_STATUS_MAX_INDEX                            1
#define CPU_MAC_PCS_CFG_MAX_INDEX                                    1
#define CPU_MAC_PCS_CODE_ERR_CNT_MAX_INDEX                           1
#define CPU_MAC_PCS_LINK_TIMER_CTL_MAX_INDEX                         1
#define CPU_MAC_PCS_STATUS_MAX_INDEX                                 1
#define CPU_MAC_RESET_CTL_MAX_INDEX                                  1
#define CPU_MAC_RX_CTL_MAX_INDEX                                     1
#define CPU_MAC_SA_CFG_MAX_INDEX                                     1
#define CPU_MAC_STATS_MAX_INDEX                                      1
#define CPU_MAC_STATS_UPDATE_CTL_MAX_INDEX                           1
#define CPU_MAC_TX_CTL_MAX_INDEX                                     1
#define EGRESS_DATA_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX            1
#define LPM_TCAM_KEY_MAX_INDEX                                       256
#define LPM_TCAM_MASK_MAX_INDEX                                      256
#define TCAM_KEY_MAX_INDEX                                           8192
#define TCAM_MASK_MAX_INDEX                                          8192
#define DS_AGING_MAX_INDEX                                           6177
#define DS_AGING_INTERRUPT_NORMAL_MAX_INDEX                          4
#define DS_AGING_PTR_MAX_INDEX                                       1
#define DS_AGING_DEBUG_STATS_MAX_INDEX                               1
#define DS_AGING_DEBUG_STATUS_MAX_INDEX                              1
#define DS_AGING_FIFO_AFULL_THRD_MAX_INDEX                           1
#define DS_AGING_INIT_MAX_INDEX                                      1
#define DS_AGING_INIT_DONE_MAX_INDEX                                 1
#define DS_AGING_PTR_FIFO_DEPTH_MAX_INDEX                            1
#define IPE_AGING_CTL_MAX_INDEX                                      1
#define DYNAMIC_DS_EDRAM4_W_MAX_INDEX                                253952
#define DYNAMIC_DS_EDRAM8_W_MAX_INDEX                                126976
#define DYNAMIC_DS_FDB_LOOKUP_INDEX_CAM_MAX_INDEX                    8
#define DYNAMIC_DS_INTERRUPT_FATAL_MAX_INDEX                         4
#define DYNAMIC_DS_INTERRUPT_NORMAL_MAX_INDEX                        4
#define DYNAMIC_DS_LM_STATS_RAM0_MAX_INDEX                           128
#define DYNAMIC_DS_LM_STATS_RAM1_MAX_INDEX                           128
#define DYNAMIC_DS_MAC_KEY_REQ_FIFO_MAX_INDEX                        5
#define DYNAMIC_DS_OAM_LM_DRAM0_REQ_FIFO_MAX_INDEX                   3
#define DYNAMIC_DS_OAM_LM_SRAM0_REQ_FIFO_MAX_INDEX                   3
#define DYNAMIC_DS_OAM_LM_SRAM1_REQ_FIFO_MAX_INDEX                   3
#define DYNAMIC_DS_CREDIT_CONFIG_MAX_INDEX                           1
#define DYNAMIC_DS_CREDIT_USED_MAX_INDEX                             1
#define DYNAMIC_DS_DEBUG_INFO_MAX_INDEX                              1
#define DYNAMIC_DS_DEBUG_STATS_MAX_INDEX                             1
#define DYNAMIC_DS_DS_EDIT_INDEX_CAM_MAX_INDEX                       1
#define DYNAMIC_DS_DS_MAC_IP_INDEX_CAM_MAX_INDEX                     1
#define DYNAMIC_DS_DS_MET_INDEX_CAM_MAX_INDEX                        1
#define DYNAMIC_DS_DS_MPLS_INDEX_CAM_MAX_INDEX                       1
#define DYNAMIC_DS_DS_NEXT_HOP_INDEX_CAM_MAX_INDEX                   1
#define DYNAMIC_DS_DS_STATS_INDEX_CAM_MAX_INDEX                      1
#define DYNAMIC_DS_DS_USER_ID_HASH_INDEX_CAM_MAX_INDEX               1
#define DYNAMIC_DS_DS_USER_ID_INDEX_CAM_MAX_INDEX                    1
#define DYNAMIC_DS_ECC_CTL_MAX_INDEX                                 1
#define DYNAMIC_DS_EDRAM_ECC_ERROR_RECORD_MAX_INDEX                  1
#define DYNAMIC_DS_EDRAM_INIT_CTL_MAX_INDEX                          1
#define DYNAMIC_DS_EDRAM_SELECT_MAX_INDEX                            1
#define DYNAMIC_DS_FDB_HASH_CTL_MAX_INDEX                            1
#define DYNAMIC_DS_FDB_HASH_INDEX_CAM_MAX_INDEX                      1
#define DYNAMIC_DS_FDB_KEY_RESULT_DEBUG_MAX_INDEX                    1
#define DYNAMIC_DS_FWD_INDEX_CAM_MAX_INDEX                           1
#define DYNAMIC_DS_LM_RD_REQ_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX 1
#define DYNAMIC_DS_LM_STATS_INDEX_CAM_MAX_INDEX                      1
#define DYNAMIC_DS_LM_STATS_RAM0__REG_RAM__RAM_CHK_REC_MAX_INDEX     1
#define DYNAMIC_DS_LM_STATS_RAM1__REG_RAM__RAM_CHK_REC_MAX_INDEX     1
#define DYNAMIC_DS_LPM_INDEX_CAM0_MAX_INDEX                          1
#define DYNAMIC_DS_LPM_INDEX_CAM1_MAX_INDEX                          1
#define DYNAMIC_DS_LPM_INDEX_CAM2_MAX_INDEX                          1
#define DYNAMIC_DS_LPM_INDEX_CAM3_MAX_INDEX                          1
#define DYNAMIC_DS_LPM_INDEX_CAM4_MAX_INDEX                          1
#define DYNAMIC_DS_MAC_HASH_INDEX_CAM_MAX_INDEX                      1
#define DYNAMIC_DS_OAM_INDEX_CAM_MAX_INDEX                           1
#define DYNAMIC_DS_REF_INTERVAL_MAX_INDEX                            1
#define DYNAMIC_DS_SRAM_INIT_MAX_INDEX                               1
#define DYNAMIC_DS_USER_ID_BASE_MAX_INDEX                            1
#define DYNAMIC_DS_WRR_CREDIT_CFG_MAX_INDEX                          1
#define E_LOOP_INTERRUPT_FATAL_MAX_INDEX                             4
#define E_LOOP_INTERRUPT_NORMAL_MAX_INDEX                            4
#define E_LOOP_CREDIT_STATE_MAX_INDEX                                1
#define E_LOOP_CTL_MAX_INDEX                                         1
#define E_LOOP_DEBUG_STATS_MAX_INDEX                                 1
#define E_LOOP_DRAIN_ENABLE_MAX_INDEX                                1
#define E_LOOP_LOOP_HDR_INFO_MAX_INDEX                               1
#define E_LOOP_PARITY_EN_MAX_INDEX                                   1
#define E_LOOP_PARITY_FAIL_RECORD_MAX_INDEX                          1
#define EPE_ACL_INFO_FIFO_MAX_INDEX                                  3
#define EPE_ACL_QOS_INTERRUPT_FATAL_MAX_INDEX                        4
#define EPE_ACL_QOS_INTERRUPT_NORMAL_MAX_INDEX                       4
#define EPE_ACL_TCAM_ACK_FIFO_MAX_INDEX                              3
#define EPE_ACL_QOS_CTL_MAX_INDEX                                    1
#define EPE_ACL_QOS_DEBUG_STATS_MAX_INDEX                            1
#define EPE_ACL_QOS_DRAIN_CTL_MAX_INDEX                              1
#define EPE_ACL_QOS_LFSR_MAX_INDEX                                   1
#define EPE_ACL_QOS_MISC_CTL_MAX_INDEX                               1
#define TO_CLA_CREDIT_DEBUG_MAX_INDEX                                1
#define EPE_CLA_EOP_INFO_FIFO_MAX_INDEX                              128
#define EPE_CLA_INTERRUPT_FATAL_MAX_INDEX                            4
#define EPE_CLA_INTERRUPT_NORMAL_MAX_INDEX                           4
#define EPE_CLA_POLICER_ACK_FIFO_MAX_INDEX                           9
#define EPE_CLA_POLICER_TRACK_FIFO_MAX_INDEX                         9
#define EPE_CLA_POLICING_INFO_FIFO_MAX_INDEX                         9
#define EPE_CLA_PTR_RAM_MAX_INDEX                                    64
#define EPE_CLA_CREDIT_CTL_MAX_INDEX                                 1
#define EPE_CLA_DEBUG_STATS_MAX_INDEX                                1
#define EPE_CLA_EOP_INFO_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX       1
#define EPE_CLA_INIT_CTL_MAX_INDEX                                   1
#define EPE_CLA_MISC_CTL_MAX_INDEX                                   1
#define EPE_CLA_PTR_RAM__REG_RAM__RAM_CHK_REC_MAX_INDEX              1
#define EPE_CLASSIFICATION_CTL_MAX_INDEX                             1
#define EPE_CLASSIFICATION_PHB_OFFSET_MAX_INDEX                      1
#define EPE_IPG_CTL_MAX_INDEX                                        1
#define EPE_HDR_ADJ_BRG_HDR_FIFO_MAX_INDEX                           32
#define EPE_HDR_ADJ_DATA_FIFO_MAX_INDEX                              32
#define EPE_HDR_ADJ_INTERRUPT_FATAL_MAX_INDEX                        4
#define EPE_HDR_ADJ_INTERRUPT_NORMAL_MAX_INDEX                       4
#define EPE_HEADER_ADJUST_PHY_PORT_MAP_MAX_INDEX                     64
#define EPE_HDR_ADJ_BRG_HDR_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX    1
#define EPE_HDR_ADJ_CREDIT_CONFIG_MAX_INDEX                          1
#define EPE_HDR_ADJ_DATA_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX       1
#define EPE_HDR_ADJ_DEBUG_CTL_MAX_INDEX                              1
#define EPE_HDR_ADJ_DEBUG_STATS_MAX_INDEX                            1
#define EPE_HDR_ADJ_DISABLE_CRC_CHK_MAX_INDEX                        1
#define EPE_HDR_ADJ_DRAIN_ENABLE_MAX_INDEX                           1
#define EPE_HDR_ADJ_INIT_MAX_INDEX                                   1
#define EPE_HDR_ADJ_INIT_DONE_MAX_INDEX                              1
#define EPE_HDR_ADJ_PARITY_ENABLE_MAX_INDEX                          1
#define EPE_HDR_ADJ_RCV_UNIT_CNT_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX 1
#define EPE_HDR_ADJ_RUNNING_CREDIT_MAX_INDEX                         1
#define EPE_HDR_ADJ_STALL_INFO_MAX_INDEX                             1
#define EPE_HDR_ADJUST_CTL_MAX_INDEX                                 1
#define EPE_HDR_ADJUST_MISC_CTL_MAX_INDEX                            1
#define EPE_NEXT_HOP_PTR_CAM_MAX_INDEX                               1
#define EPE_PORT_EXTENDER_DOWNLINK_MAX_INDEX                         1
#define DS_DEST_PORT_LOOPBACK_MAX_INDEX                              8
#define DS_PACKET_HEADER_EDIT_TUNNEL_MAX_INDEX                       64
#define EPE_HDR_EDIT_DISCARD_TYPE_STATS_MAX_INDEX                    64
#define EPE_HDR_EDIT_INGRESS_HDR_FIFO_MAX_INDEX                      128
#define EPE_HDR_EDIT_INTERRUPT_FATAL_MAX_INDEX                       4
#define EPE_HDR_EDIT_INTERRUPT_NORMAL_MAX_INDEX                      4
#define EPE_HDR_EDIT_NEW_HDR_FIFO_MAX_INDEX                          8
#define EPE_HDR_EDIT_OUTER_HDR_DATA_FIFO_MAX_INDEX                   8
#define EPE_HDR_EDIT_PKT_CTL_FIFO_MAX_INDEX                          64
#define EPE_HDR_EDIT_PKT_DATA_FIFO_MAX_INDEX                         128
#define EPE_HDR_EDIT_PKT_INFO_HDR_PROC_FIFO_MAX_INDEX                64
#define EPE_HDR_EDIT_PKT_INFO_OAM_FIFO_MAX_INDEX                     3
#define EPE_HDR_EDIT_PKT_INFO_OUTER_HDR_FIFO_MAX_INDEX               3
#define EPE_HEADER_EDIT_PHY_PORT_MAP_MAX_INDEX                       64
#define DS_PACKET_HEADER_EDIT_TUNNEL__REG_RAM__RAM_CHK_REC_MAX_INDEX 1
#define EPE_HDR_EDIT_DATA_CMPT_EOP_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX 1
#define EPE_HDR_EDIT_DATA_CMPT_SOP_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX 1
#define EPE_HDR_EDIT_DEBUG_STATS_MAX_INDEX                           1
#define EPE_HDR_EDIT_DRAIN_ENABLE_MAX_INDEX                          1
#define EPE_HDR_EDIT_ECC_CTL_MAX_INDEX                               1
#define EPE_HDR_EDIT_EXCEP_INFO_MAX_INDEX                            1
#define EPE_HDR_EDIT_HARD_DISCARD_EN_MAX_INDEX                       1
#define EPE_HDR_EDIT_INIT_MAX_INDEX                                  1
#define EPE_HDR_EDIT_INIT_DONE_MAX_INDEX                             1
#define EPE_HDR_EDIT_L2_LOOP_BACK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX 1
#define EPE_HDR_EDIT_NEW_HDR_FIFO__FIFO_ALMOST_EMPTY_THRD_MAX_INDEX  1
#define EPE_HDR_EDIT_NEW_HDR_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX   1
#define EPE_HDR_EDIT_OUTER_HDR_DATA_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX 1
#define EPE_HDR_EDIT_PARITY_EN_MAX_INDEX                             1
#define EPE_HDR_EDIT_PKT_CTL_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX   1
#define EPE_HDR_EDIT_PKT_DATA_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX  1
#define EPE_HDR_EDIT_PKT_INFO_OUTER_HDR_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX 1
#define EPE_HDR_EDIT_RESERVED_CREDIT_CNT_MAX_INDEX                   1
#define EPE_HDR_EDIT_STATE_MAX_INDEX                                 1
#define EPE_HDR_EDIT_TO_NET_TX_BRG_HDR_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX 1
#define EPE_HEADER_EDIT_CTL_MAX_INDEX                                1
#define EPE_HEADER_EDIT_MUX_CTL_MAX_INDEX                            1
#define EPE_PKT_HDR_CTL_MAX_INDEX                                    1
#define EPE_PRIORITY_TO_STATS_COS_MAX_INDEX                          1
#define DS_IPV6_NAT_PREFIX_MAX_INDEX                                 256
#define DS_L3_TUNNEL_V4_IP_SA_MAX_INDEX                              8
#define DS_L3_TUNNEL_V6_IP_SA_MAX_INDEX                              8
#define DS_PORT_LINK_AGG_MAX_INDEX                                   128
#define EPE_H_P_HDR_ADJ_PI_FIFO_MAX_INDEX                            32
#define EPE_H_P_L2_EDIT_FIFO_MAX_INDEX                               5
#define EPE_H_P_L3_EDIT_FIFO_MAX_INDEX                               5
#define EPE_H_P_NEXT_HOP_PR_FIFO_MAX_INDEX                           32
#define EPE_H_P_PAYLOAD_INFO_FIFO_MAX_INDEX                          5
#define EPE_HDR_PROC_INTERRUPT_FATAL_MAX_INDEX                       4
#define EPE_HDR_PROC_INTERRUPT_NORMAL_MAX_INDEX                      4
#define EPE_HDR_PROC_PHY_PORT_MAP_MAX_INDEX                          64
#define DS_IPV6_NAT_PREFIX__REG_RAM__RAM_CHK_REC_MAX_INDEX           1
#define EPE_HDR_PROC_DEBUG_STATS_MAX_INDEX                           1
#define EPE_HDR_PROC_FLOW_CTL_MAX_INDEX                              1
#define EPE_HDR_PROC_FRAG_CTL_MAX_INDEX                              1
#define EPE_HDR_PROC_INIT_MAX_INDEX                                  1
#define EPE_HDR_PROC_PARITY_EN_MAX_INDEX                             1
#define EPE_L2_EDIT_CTL_MAX_INDEX                                    1
#define EPE_L2_ETHER_TYPE_MAX_INDEX                                  1
#define EPE_L2_PORT_MAC_SA_MAX_INDEX                                 1
#define EPE_L2_ROUTER_MAC_SA_MAX_INDEX                               1
#define EPE_L2_SNAP_CTL_MAX_INDEX                                    1
#define EPE_L2_TPID_CTL_MAX_INDEX                                    1
#define EPE_L3_EDIT_MPLS_TTL_MAX_INDEX                               1
#define EPE_L3_IP_IDENTIFICATION_MAX_INDEX                           1
#define EPE_MIRROR_ESCAPE_CAM_MAX_INDEX                              1
#define EPE_PBB_CTL_MAX_INDEX                                        1
#define EPE_PKT_PROC_CTL_MAX_INDEX                                   1
#define EPE_PKT_PROC_MUX_CTL_MAX_INDEX                               1
#define DS_DEST_CHANNEL_MAX_INDEX                                    64
#define DS_DEST_INTERFACE_MAX_INDEX                                  1024
#define DS_DEST_PORT_MAX_INDEX                                       128
#define DS_EGRESS_VLAN_RANGE_PROFILE_MAX_INDEX                       64
#define EPE_EDIT_PRIORITY_MAP_MAX_INDEX                              2048
#define EPE_NEXT_HOP_INTERNAL_MAX_INDEX                              2
#define EPE_NEXT_HOP_INTERRUPT_FATAL_MAX_INDEX                       4
#define EPE_NEXT_HOP_INTERRUPT_NORMAL_MAX_INDEX                      4
#define NEXT_HOP_DS_VLAN_RANGE_PROF_FIFO_MAX_INDEX                   7
#define NEXT_HOP_HASH_ACK_FIFO_MAX_INDEX                             5
#define NEXT_HOP_PI_FIFO_MAX_INDEX                                   48
#define NEXT_HOP_PR_FIFO_MAX_INDEX                                   48
#define NEXT_HOP_PR_KEY_FIFO_MAX_INDEX                               7
#define DS_DEST_CHANNEL__REG_RAM__RAM_CHK_REC_MAX_INDEX              1
#define DS_DEST_INTERFACE__REG_RAM__RAM_CHK_REC_MAX_INDEX            1
#define DS_DEST_PORT__REG_RAM__RAM_CHK_REC_MAX_INDEX                 1
#define DS_EGRESS_VLAN_RANGE_PROFILE__REG_RAM__RAM_CHK_REC_MAX_INDEX 1
#define EPE_EDIT_PRIORITY_MAP__REG_RAM__RAM_CHK_REC_MAX_INDEX        1
#define EPE_NEXT_HOP_CREDIT_CTL_MAX_INDEX                            1
#define EPE_NEXT_HOP_CTL_MAX_INDEX                                   1
#define EPE_NEXT_HOP_DEBUG_STATS_MAX_INDEX                           1
#define EPE_NEXT_HOP_DEBUG_STATUS_MAX_INDEX                          1
#define EPE_NEXT_HOP_MISC_CTL_MAX_INDEX                              1
#define EPE_NEXT_HOP_RANDOM_VALUE_GEN_MAX_INDEX                      1
#define EPE_OAM_HASH_DS_FIFO_MAX_INDEX                               15
#define EPE_OAM_HDR_PROC_INFO_FIFO_MAX_INDEX                         48
#define EPE_OAM_INFO_FIFO_MAX_INDEX                                  3
#define EPE_OAM_INTERRUPT_FATAL_MAX_INDEX                            4
#define EPE_OAM_INTERRUPT_NORMAL_MAX_INDEX                           4
#define EPE_OAM_CONFIG_MAX_INDEX                                     1
#define EPE_OAM_CREDIT_DEBUG_MAX_INDEX                               1
#define EPE_OAM_CTL_MAX_INDEX                                        1
#define EPE_OAM_DEBUG_STATS_MAX_INDEX                                1
#define DS_STATS_MAX_INDEX                                           0
#define E_E_E_STATS_RAM_MAX_INDEX                                    60
#define GLOBAL_STATS_INTERRUPT_FATAL_MAX_INDEX                       4
#define GLOBAL_STATS_INTERRUPT_NORMAL_MAX_INDEX                      4
#define GLOBAL_STATS_SATU_ADDR_MAX_INDEX                             1
#define RX_INBD_STATS_RAM_MAX_INDEX                                  128
#define TX_INBD_STATS_EPE_RAM_MAX_INDEX                              128
#define TX_INBD_STATS_PAUSE_RAM_MAX_INDEX                            128
#define E_E_E_STATS_MEM__RAM_CHK_REC_MAX_INDEX                       1
#define GLOBAL_STATS_BYTE_CNT_THRESHOLD_MAX_INDEX                    1
#define GLOBAL_STATS_CREDIT_CTL_MAX_INDEX                            1
#define GLOBAL_STATS_CTL_MAX_INDEX                                   1
#define GLOBAL_STATS_DBG_STATUS_MAX_INDEX                            1
#define GLOBAL_STATS_E_E_E_CALENDAR_MAX_INDEX                        1
#define GLOBAL_STATS_E_E_E_CTL_MAX_INDEX                             1
#define GLOBAL_STATS_EPE_BASE_ADDR_MAX_INDEX                         1
#define GLOBAL_STATS_EPE_REQ_DROP_CNT_MAX_INDEX                      1
#define GLOBAL_STATS_IPE_BASE_ADDR_MAX_INDEX                         1
#define GLOBAL_STATS_IPE_REQ_DROP_CNT_MAX_INDEX                      1
#define GLOBAL_STATS_PARITY_FAIL_MAX_INDEX                           1
#define GLOBAL_STATS_PKT_CNT_THRESHOLD_MAX_INDEX                     1
#define GLOBAL_STATS_POLICING_BASE_ADDR_MAX_INDEX                    1
#define GLOBAL_STATS_POLICING_REQ_DROP_CNT_MAX_INDEX                 1
#define GLOBAL_STATS_Q_MGR_BASE_ADDR_MAX_INDEX                       1
#define GLOBAL_STATS_Q_MGR_REQ_DROP_CNT_MAX_INDEX                    1
#define GLOBAL_STATS_RAM_INIT_CTL_MAX_INDEX                          1
#define GLOBAL_STATS_RX_LPI_STATE_MAX_INDEX                          1
#define GLOBAL_STATS_SATU_ADDR_FIFO_DEPTH_MAX_INDEX                  1
#define GLOBAL_STATS_SATU_ADDR_FIFO_DEPTH_THRESHOLD_MAX_INDEX        1
#define GLOBAL_STATS_WRR_CFG_MAX_INDEX                               1
#define RX_INBD_STATS_MEM__RAM_CHK_REC_MAX_INDEX                     1
#define TX_INBD_STATS_EPE_MEM__RAM_CHK_REC_MAX_INDEX                 1
#define TX_INBD_STATS_PAUSE_MEM__RAM_CHK_REC_MAX_INDEX               1
#define GB_SUP_INTERRUPT_FATAL_MAX_INDEX                             4
#define GB_SUP_INTERRUPT_FUNCTION_MAX_INDEX                          4
#define GB_SUP_INTERRUPT_NORMAL_MAX_INDEX                            4
#define TRACE_HDR_MEM_MAX_INDEX                                      64
#define AUX_CLK_SEL_CFG_MAX_INDEX                                    1
#define CLK_DIV_AUX_CFG_MAX_INDEX                                    1
#define CLK_DIV_CORE0_CFG_MAX_INDEX                                  1
#define CLK_DIV_CORE1_CFG_MAX_INDEX                                  1
#define CLK_DIV_CORE2_CFG_MAX_INDEX                                  1
#define CLK_DIV_CORE3_CFG_MAX_INDEX                                  1
#define CLK_DIV_INTF0_CFG_MAX_INDEX                                  1
#define CLK_DIV_INTF1_CFG_MAX_INDEX                                  1
#define CLK_DIV_INTF2_CFG_MAX_INDEX                                  1
#define CLK_DIV_INTF3_CFG_MAX_INDEX                                  1
#define CLK_DIV_RST_CTL_MAX_INDEX                                    1
#define CORE_PLL_CFG_MAX_INDEX                                       1
#define DEVICE_ID_MAX_INDEX                                          1
#define GB_SENSOR_CTL_MAX_INDEX                                      1
#define GLOBAL_GATED_CLK_CTL_MAX_INDEX                               1
#define HSS0_ADAPT_EQ_CFG_MAX_INDEX                                  1
#define HSS0_SPI_STATUS_MAX_INDEX                                    1
#define HSS12_G0_CFG_MAX_INDEX                                       1
#define HSS12_G0_PRBS_TEST_MAX_INDEX                                 1
#define HSS12_G1_CFG_MAX_INDEX                                       1
#define HSS12_G1_PRBS_TEST_MAX_INDEX                                 1
#define HSS12_G2_CFG_MAX_INDEX                                       1
#define HSS12_G2_PRBS_TEST_MAX_INDEX                                 1
#define HSS12_G3_CFG_MAX_INDEX                                       1
#define HSS12_G3_PRBS_TEST_MAX_INDEX                                 1
#define HSS1_ADAPT_EQ_CFG_MAX_INDEX                                  1
#define HSS1_SPI_STATUS_MAX_INDEX                                    1
#define HSS2_ADAPT_EQ_CFG_MAX_INDEX                                  1
#define HSS2_SPI_STATUS_MAX_INDEX                                    1
#define HSS3_ADAPT_EQ_CFG_MAX_INDEX                                  1
#define HSS3_SPI_STATUS_MAX_INDEX                                    1
#define HSS6_G_LANE_A_CTL_MAX_INDEX                                  1
#define HSS_ACCESS_CTL_MAX_INDEX                                     1
#define HSS_ACESS_PARAMETER_MAX_INDEX                                1
#define HSS_BIT_ORDER_CFG_MAX_INDEX                                  1
#define HSS_MODE_CFG_MAX_INDEX                                       1
#define HSS_PLL_RESET_CFG_MAX_INDEX                                  1
#define HSS_READ_DATA_MAX_INDEX                                      1
#define HSS_TX_GEAR_BOX_RST_CTL_MAX_INDEX                            1
#define HSS_WRITE_DATA_MAX_INDEX                                     1
#define INT_LK_INTF_CFG_MAX_INDEX                                    1
#define INTERRUPT_ENABLE_MAX_INDEX                                   1
#define INTERRUPT_MAP_CTL_MAX_INDEX                                  1
#define LED_CLOCK_DIV_MAX_INDEX                                      1
#define LINK_TIMER_CTL_MAX_INDEX                                     1
#define MDIO_CLOCK_CFG_MAX_INDEX                                     1
#define MODULE_GATED_CLK_CTL_MAX_INDEX                               1
#define O_O_B_F_C_CLOCK_DIV_MAX_INDEX                                1
#define PCIE_BAR0_CFG_MAX_INDEX                                      1
#define PCIE_BAR1_CFG_MAX_INDEX                                      1
#define PCIE_CFG_IP_CFG_MAX_INDEX                                    1
#define PCIE_DL_INT_ERROR_MAX_INDEX                                  1
#define PCIE_ERROR_INJECT_CTL_MAX_INDEX                              1
#define PCIE_ERROR_STATUS_MAX_INDEX                                  1
#define PCIE_INBD_STATUS_MAX_INDEX                                   1
#define PCIE_INTF_CFG_MAX_INDEX                                      1
#define PCIE_PERF_MON_CTL_MAX_INDEX                                  1
#define PCIE_PERF_MON_STATUS_MAX_INDEX                               1
#define PCIE_PM_CFG_MAX_INDEX                                        1
#define PCIE_PROTOCOL_CFG_MAX_INDEX                                  1
#define PCIE_STATUS_REG_MAX_INDEX                                    1
#define PCIE_TL_DLP_IP_CFG_MAX_INDEX                                 1
#define PCIE_TRACE_CTL_MAX_INDEX                                     1
#define PCIE_UTL_INT_ERROR_MAX_INDEX                                 1
#define PLL_LOCK_ERROR_CNT_MAX_INDEX                                 1
#define PLL_LOCK_OUT_MAX_INDEX                                       1
#define PTP_TOD_CLK_DIV_CFG_MAX_INDEX                                1
#define QSGMII_HSS_SEL_CFG_MAX_INDEX                                 1
#define RESET_INT_RELATED_MAX_INDEX                                  1
#define SERDES_REF_CLK_DIV_CFG_MAX_INDEX                             1
#define SGMAC_MODE_CFG_MAX_INDEX                                     1
#define SUP_DEBUG_CLK_RST_CTL_MAX_INDEX                              1
#define SUP_GPIO_CTL_MAX_INDEX                                       1
#define SUP_INTR_MASK_CFG_MAX_INDEX                                  1
#define SYNC_E_CLK_DIV_CFG_MAX_INDEX                                 1
#define SYNC_ETHERNET_CLK_CFG_MAX_INDEX                              1
#define SYNC_ETHERNET_MON_MAX_INDEX                                  1
#define SYNC_ETHERNET_SELECT_MAX_INDEX                               1
#define TANK_PLL_CFG_MAX_INDEX                                       1
#define TIME_OUT_INFO_MAX_INDEX                                      1
#define HASH_DS_INTERRUPT_FATAL_MAX_INDEX                            4
#define HASH_DS_KEY0_LKUP_TRACK_FIFO_MAX_INDEX                       8
#define HASH_DS_AD_REQ_WT_CFG_MAX_INDEX                              1
#define HASH_DS_CREDIT_CONFIG_MAX_INDEX                              1
#define HASH_DS_CREDIT_USED_MAX_INDEX                                1
#define HASH_DS_DEBUG_STATS_MAX_INDEX                                1
#define HASH_DS_KEY0_LKUP_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX 1
#define HASH_DS_KEY1_LKUP_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX 1
#define HASH_DS_KEY_LKUP_RESULT_DEBUG_INFO_MAX_INDEX                 1
#define HASH_DS_KEY_REQ_WT_CFG_MAX_INDEX                             1
#define USER_ID_HASH_LOOKUP_CTL_MAX_INDEX                            1
#define USER_ID_RESULT_CTL_MAX_INDEX                                 1
#define I2_C_MASTER_DATA_RAM_MAX_INDEX                               256
#define I2_C_MASTER_INTERRUPT_NORMAL_MAX_INDEX                       4
#define I2_C_MASTER_BMP_CFG_MAX_INDEX                                1
#define I2_C_MASTER_CFG_MAX_INDEX                                    1
#define I2_C_MASTER_POLLING_CFG_MAX_INDEX                            1
#define I2_C_MASTER_READ_CFG_MAX_INDEX                               1
#define I2_C_MASTER_READ_CTL_MAX_INDEX                               1
#define I2_C_MASTER_READ_STATUS_MAX_INDEX                            1
#define I2_C_MASTER_STATUS_MAX_INDEX                                 1
#define I2_C_MASTER_WR_CFG_MAX_INDEX                                 1
#define INT_LK_INTERRUPT_FATAL_MAX_INDEX                             4
#define INT_LK_RX_STATS_MEM_MAX_INDEX                                64
#define INT_LK_TX_STATS_MEM_MAX_INDEX                                64
#define INT_LK_BURST_CTL_MAX_INDEX                                   1
#define INT_LK_CAL_PTR_CTL_MAX_INDEX                                 1
#define INT_LK_CREDIT_CTL_MAX_INDEX                                  1
#define INT_LK_CREDIT_DEBUG_MAX_INDEX                                1
#define INT_LK_FC_CHAN_CTL_MAX_INDEX                                 1
#define INT_LK_FIFO_THD_CTL_MAX_INDEX                                1
#define INT_LK_FL_CTL_CAL_MAX_INDEX                                  1
#define INT_LK_IN_DATA_MEM_OVER_FLOW_INFO_MAX_INDEX                  1
#define INT_LK_IN_DATA_MEM__RAM_CHK_REC_MAX_INDEX                    1
#define INT_LK_LANE_RX_CTL_MAX_INDEX                                 1
#define INT_LK_LANE_RX_DEBUG_STATE_MAX_INDEX                         1
#define INT_LK_LANE_RX_STATS_MAX_INDEX                               1
#define INT_LK_LANE_SWAP_EN_MAX_INDEX                                1
#define INT_LK_LANE_TX_CTL_MAX_INDEX                                 1
#define INT_LK_LANE_TX_STATE_MAX_INDEX                               1
#define INT_LK_MEM_INIT_CTL_MAX_INDEX                                1
#define INT_LK_META_FRAME_CTL_MAX_INDEX                              1
#define INT_LK_MISC_CTL_MAX_INDEX                                    1
#define INT_LK_PARITY_CTL_MAX_INDEX                                  1
#define INT_LK_PKT_CRC_CTL_MAX_INDEX                                 1
#define INT_LK_PKT_PROC_OUT_BUF_MEM__RAM_CHK_REC_MAX_INDEX           1
#define INT_LK_PKT_PROC_RES_WORD_MEM__RAM_CHK_REC_MAX_INDEX          1
#define INT_LK_PKT_PROC_STALL_INFO_MAX_INDEX                         1
#define INT_LK_PKT_STATS_MAX_INDEX                                   1
#define INT_LK_RATE_MATCH_CTL_MAX_INDEX                              1
#define INT_LK_RX_ALIGN_CTL_MAX_INDEX                                1
#define INT_LK_SOFT_RESET_MAX_INDEX                                  1
#define INT_LK_TEST_CTL_MAX_INDEX                                    1
#define FIB_HASH_AD_REQ_FIFO_MAX_INDEX                               64
#define FIB_KEY_TRACK_FIFO_MAX_INDEX                                 64
#define FIB_LEARN_FIFO_RESULT_MAX_INDEX                              1
#define FIB_LKUP_MGR_REQ_FIFO_MAX_INDEX                              5
#define IPE_FIB_INTERRUPT_FATAL_MAX_INDEX                            4
#define IPE_FIB_INTERRUPT_NORMAL_MAX_INDEX                           4
#define LKUP_MGR_LPM_KEY_REQ_FIFO_MAX_INDEX                          4
#define LPM_AD_REQ_FIFO_MAX_INDEX                                    64
#define LPM_FINAL_TRACK_FIFO_MAX_INDEX                               15
#define LPM_HASH_KEY_TRACK_FIFO_MAX_INDEX                            13
#define LPM_HASH_RESULT_FIFO_MAX_INDEX                               12
#define LPM_PIPE0_REQ_FIFO_MAX_INDEX                                 10
#define LPM_PIPE1_REQ_FIFO_MAX_INDEX                                 10
#define LPM_PIPE2_REQ_FIFO_MAX_INDEX                                 10
#define LPM_PIPE3_REQ_FIFO_MAX_INDEX                                 10
#define LPM_TCAM_AD_MEM_MAX_INDEX                                    256
#define LPM_TCAM_AD_REQ_FIFO_MAX_INDEX                               5
#define LPM_TCAM_KEY_REQ_FIFO_MAX_INDEX                              4
#define LPM_TCAM_RESULT_FIFO_MAX_INDEX                               12
#define LPM_TRACK_FIFO_MAX_INDEX                                     48
#define FIB_AD_RD_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX        1
#define FIB_CREDIT_CTL_MAX_INDEX                                     1
#define FIB_CREDIT_DEBUG_MAX_INDEX                                   1
#define FIB_DEBUG_STATS_MAX_INDEX                                    1
#define FIB_ECC_CTL_MAX_INDEX                                        1
#define FIB_ENGINE_HASH_CTL_MAX_INDEX                                1
#define FIB_ENGINE_LOOKUP_CTL_MAX_INDEX                              1
#define FIB_ENGINE_LOOKUP_RESULT_CTL_MAX_INDEX                       1
#define FIB_ENGINE_LOOKUP_RESULT_CTL1_MAX_INDEX                      1
#define FIB_HASH_KEY_CPU_REQ_MAX_INDEX                               1
#define FIB_HASH_KEY_CPU_RESULT_MAX_INDEX                            1
#define FIB_INIT_CTL_MAX_INDEX                                       1
#define FIB_KEY_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX          1
#define FIB_LEARN_FIFO_CTL_MAX_INDEX                                 1
#define FIB_LEARN_FIFO_INFO_MAX_INDEX                                1
#define FIB_LRN_KEY_WR_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX         1
#define FIB_MAC_HASH_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX     1
#define FIB_PARITY_CTL_MAX_INDEX                                     1
#define FIB_TCAM_CPU_ACCESS_MAX_INDEX                                1
#define FIB_TCAM_INIT_CTL_MAX_INDEX                                  1
#define FIB_TCAM_READ_DATA_MAX_INDEX                                 1
#define FIB_TCAM_WRITE_DATA_MAX_INDEX                                1
#define FIB_TCAM_WRITE_MASK_MAX_INDEX                                1
#define LKUP_MGR_LPM_KEY_REQ_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX   1
#define LPM_HASH2ND_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX      1
#define LPM_HASH_KEY_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX     1
#define LPM_HASH_LU_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX      1
#define LPM_IPV6_HASH32_HIGH_KEY_CAM_MAX_INDEX                       1
#define LPM_PIPE0_REQ_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX          1
#define LPM_PIPE3_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX        1
#define LPM_TCAM_AD_MEM__REG_RAM__RAM_CHK_REC_MAX_INDEX              1
#define LPM_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX              1
#define DS_APS_SELECT_MAX_INDEX                                      32
#define DS_QCN_MAX_INDEX                                             96
#define IPE_FWD_DISCARD_TYPE_STATS_MAX_INDEX                         64
#define IPE_FWD_HDR_ADJ_INFO_FIFO_MAX_INDEX                          128
#define IPE_FWD_INTERRUPT_FATAL_MAX_INDEX                            4
#define IPE_FWD_INTERRUPT_NORMAL_MAX_INDEX                           4
#define IPE_FWD_LEARN_THROUGH_FIFO_MAX_INDEX                         17
#define IPE_FWD_LEARN_TRACK_FIFO_MAX_INDEX                           8
#define IPE_FWD_OAM_FIFO_MAX_INDEX                                   7
#define IPE_FWD_QCN_MAP_TABLE_MAX_INDEX                              64
#define IPE_FWD_ROUTE_PI_FIFO_MAX_INDEX                              48
#define DS_APS_BRIDGE_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX    1
#define IPE_FWD_BRIDGE_HDR_REC_MAX_INDEX                             1
#define IPE_FWD_CFG_MAX_INDEX                                        1
#define IPE_FWD_CTL_MAX_INDEX                                        1
#define IPE_FWD_CTL1_MAX_INDEX                                       1
#define IPE_FWD_DEBUG_STATS_MAX_INDEX                                1
#define IPE_FWD_DS_QCN_RAM__RAM_CHK_REC_MAX_INDEX                    1
#define IPE_FWD_EOP_MSG_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX        1
#define IPE_FWD_EXCP_REC_MAX_INDEX                                   1
#define IPE_FWD_HDR_ADJ_INFO_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX   1
#define IPE_FWD_QCN_CTL_MAX_INDEX                                    1
#define IPE_FWD_SOP_MSG_RAM__RAM_CHK_REC_MAX_INDEX                   1
#define IPE_PRIORITY_TO_STATS_COS_MAX_INDEX                          1
#define DS_PHY_PORT_MAX_INDEX                                        128
#define IPE_HDR_ADJ_INTERRUPT_FATAL_MAX_INDEX                        4
#define IPE_HDR_ADJ_INTERRUPT_NORMAL_MAX_INDEX                       4
#define IPE_HDR_ADJ_LPBK_PD_FIFO_MAX_INDEX                           32
#define IPE_HDR_ADJ_LPBK_PI_FIFO_MAX_INDEX                           8
#define IPE_HDR_ADJ_NET_PD_FIFO_MAX_INDEX                            32
#define IPE_HEADER_ADJUST_PHY_PORT_MAP_MAX_INDEX                     64
#define IPE_MUX_HEADER_COS_MAP_MAX_INDEX                             4
#define DS_PHY_PORT__REG_RAM__RAM_CHK_REC_MAX_INDEX                  1
#define IPE_HDR_ADJ_CFG_MAX_INDEX                                    1
#define IPE_HDR_ADJ_CHAN_ID_CFG_MAX_INDEX                            1
#define IPE_HDR_ADJ_CREDIT_CTL_MAX_INDEX                             1
#define IPE_HDR_ADJ_CREDIT_USED_MAX_INDEX                            1
#define IPE_HDR_ADJ_DEBUG_STATS_MAX_INDEX                            1
#define IPE_HDR_ADJ_ECC_CTL_MAX_INDEX                                1
#define IPE_HDR_ADJ_ECC_STATS_MAX_INDEX                              1
#define IPE_HDR_ADJ_LPBK_PD_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX    1
#define IPE_HDR_ADJ_LPBK_PI_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX    1
#define IPE_HDR_ADJ_NET_PD_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX     1
#define IPE_HDR_ADJ_WRR_WEIGHT_MAX_INDEX                             1
#define IPE_HEADER_ADJUST_CTL_MAX_INDEX                              1
#define IPE_HEADER_ADJUST_EXP_MAP_MAX_INDEX                          1
#define IPE_HEADER_ADJUST_MODE_CTL_MAX_INDEX                         1
#define IPE_LOOPBACK_HEADER_ADJUST_CTL_MAX_INDEX                     1
#define IPE_MUX_HEADER_ADJUST_CTL_MAX_INDEX                          1
#define IPE_PHY_PORT_MUX_CTL_MAX_INDEX                               1
#define IPE_PORT_MAC_CTL1_MAX_INDEX                                  1
#define IPE_SGMAC_HEADER_ADJUST_CTL_MAX_INDEX                        1
#define DS_MPLS_CTL_MAX_INDEX                                        256
#define DS_MPLS_RES_FIFO_MAX_INDEX                                   32
#define DS_PHY_PORT_EXT_MAX_INDEX                                    128
#define DS_ROUTER_MAC_MAX_INDEX                                      256
#define DS_SRC_INTERFACE_MAX_INDEX                                   1024
#define DS_SRC_PORT_MAX_INDEX                                        128
#define DS_VLAN_ACTION_PROFILE_MAX_INDEX                             256
#define DS_VLAN_RANGE_PROFILE_MAX_INDEX                              64
#define HASH_DS_RES_FIFO_MAX_INDEX                                   10
#define INPUT_P_I_FIFO_MAX_INDEX                                     6
#define INPUT_P_R_FIFO_MAX_INDEX                                     3
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM3_MAX_INDEX                      16
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM_RESULT3_MAX_INDEX               16
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM_RESULT4_MAX_INDEX               8
#define IPE_INTF_MAP_INTERRUPT_FATAL_MAX_INDEX                       4
#define IPE_INTF_MAP_INTERRUPT_NORMAL_MAX_INDEX                      4
#define IPE_INTF_MAPPER_CLA_PATH_MAP_MAX_INDEX                       8
#define TCAM_DS_RES_FIFO_MAX_INDEX                                   8
#define DS_MPLS_CTL__REG_RAM__RAM_CHK_REC_MAX_INDEX                  1
#define DS_PHY_PORT_EXT__REG_RAM__RAM_CHK_REC_MAX_INDEX              1
#define DS_PROTOCOL_VLAN_MAX_INDEX                                   1
#define DS_ROUTER_MAC__REG_RAM__RAM_CHK_REC_MAX_INDEX                1
#define DS_SRC_INTERFACE__REG_RAM__RAM_CHK_REC_MAX_INDEX             1
#define DS_SRC_PORT__REG_RAM__RAM_CHK_REC_MAX_INDEX                  1
#define DS_VLAN_ACTION_PROFILE__REG_RAM__RAM_CHK_REC_MAX_INDEX       1
#define DS_VLAN_RANGE_PROFILE__REG_RAM__RAM_CHK_REC_MAX_INDEX        1
#define EXP_INFO_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX               1
#define IM_PKT_DATA_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX            1
#define IPE_BPDU_ESCAPE_CTL_MAX_INDEX                                1
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM_MAX_INDEX                       1
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM2_MAX_INDEX                      1
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM4_MAX_INDEX                      1
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM_RESULT_MAX_INDEX                1
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM_RESULT2_MAX_INDEX               1
#define IPE_DS_VLAN_CTL_MAX_INDEX                                    1
#define IPE_INTF_MAP_CREDIT_CTL_MAX_INDEX                            1
#define IPE_INTF_MAP_DS_STP_STATE_STATS_MAX_INDEX                    1
#define IPE_INTF_MAP_DS_VLAN_PROF_STATS_MAX_INDEX                    1
#define IPE_INTF_MAP_DS_VLAN_STATS_MAX_INDEX                         1
#define IPE_INTF_MAP_INFO_OUT_STATS_MAX_INDEX                        1
#define IPE_INTF_MAP_INIT_CTL_MAX_INDEX                              1
#define IPE_INTF_MAP_MISC_CTL_MAX_INDEX                              1
#define IPE_INTF_MAP_RANDOM_SEED_CTL_MAX_INDEX                       1
#define IPE_INTF_MAP_STATS1_MAX_INDEX                                1
#define IPE_INTF_MAP_STATS2_MAX_INDEX                                1
#define IPE_INTF_MAP_STATS3_MAX_INDEX                                1
#define IPE_INTF_MAP_STATS4_MAX_INDEX                                1
#define IPE_INTF_MAPPER_CTL_MAX_INDEX                                1
#define IPE_MPLS_CTL_MAX_INDEX                                       1
#define IPE_MPLS_EXP_MAP_MAX_INDEX                                   1
#define IPE_MPLS_TTL_THRD_MAX_INDEX                                  1
#define IPE_PORT_MAC_CTL_MAX_INDEX                                   1
#define IPE_ROUTER_MAC_CTL_MAX_INDEX                                 1
#define IPE_TUNNEL_DECAP_CTL_MAX_INDEX                               1
#define IPE_TUNNEL_ID_CTL_MAX_INDEX                                  1
#define IPE_USER_ID_CTL_MAX_INDEX                                    1
#define IPE_USER_ID_SGMAC_CTL_MAX_INDEX                              1
#define USER_ID_IM_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX             1
#define USER_ID_KEY_INFO_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX       1
#define IPE_LKUP_MGR_INTERRUPT_FATAL_MAX_INDEX                       4
#define IPE_LKUP_MGR_INTERRUPT_NORMAL_MAX_INDEX                      4
#define IPE_LKUP_MGR_P_I_FIFO_MAX_INDEX                              4
#define IPE_LKUP_MGR_P_R_FIFO_MAX_INDEX                              4
#define IPE_EXCEPTION3_CAM_MAX_INDEX                                 1
#define IPE_EXCEPTION3_CAM2_MAX_INDEX                                1
#define IPE_EXCEPTION3_CTL_MAX_INDEX                                 1
#define IPE_IPV4_MCAST_FORCE_ROUTE_MAX_INDEX                         1
#define IPE_IPV6_MCAST_FORCE_ROUTE_MAX_INDEX                         1
#define IPE_LKUP_MGR_CREDIT_MAX_INDEX                                1
#define IPE_LKUP_MGR_DEBUG_STATS_MAX_INDEX                           1
#define IPE_LKUP_MGR_DRAIN_ENABLE_MAX_INDEX                          1
#define IPE_LKUP_MGR_FSM_STATE_MAX_INDEX                             1
#define IPE_LOOKUP_CTL_MAX_INDEX                                     1
#define IPE_LOOKUP_PBR_CTL_MAX_INDEX                                 1
#define IPE_LOOKUP_ROUTE_CTL_MAX_INDEX                               1
#define IPE_ROUTE_MARTIAN_ADDR_MAX_INDEX                             1
#define DS_ECMP_GROUP_MAX_INDEX                                      16
#define DS_ECMP_STATE_MAX_INDEX                                      1024
#define DS_RPF_MAX_INDEX                                             64
#define DS_SRC_CHANNEL_MAX_INDEX                                     64
#define DS_STORM_CTL_MAX_INDEX                                       320
#define IPE_CLASSIFICATION_COS_MAP_MAX_INDEX                         32
#define IPE_CLASSIFICATION_DSCP_MAP_MAX_INDEX                        512
#define IPE_CLASSIFICATION_PATH_MAP_MAX_INDEX                        8
#define IPE_CLASSIFICATION_PHB_OFFSET_MAX_INDEX                      4
#define IPE_CLASSIFICATION_PRECEDENCE_MAP_MAX_INDEX                  16
#define IPE_LEARNING_CACHE_MAX_INDEX                                 16
#define IPE_OAM_ACK_FIFO_MAX_INDEX                                   6
#define IPE_OAM_FR_IM_INFO_FIFO_MAX_INDEX                            48
#define IPE_OAM_FR_LM_INFO_FIFO_MAX_INDEX                            48
#define IPE_PKT_PROC_FIB_FIFO_MAX_INDEX                              32
#define IPE_PKT_PROC_HASH_DS_FIFO_MAX_INDEX                          5
#define IPE_PKT_PROC_INTERRUPT_FATAL_MAX_INDEX                       4
#define IPE_PKT_PROC_INTERRUPT_NORMAL_MAX_INDEX                      4
#define IPE_PKT_PROC_PI_FR_IM_FIFO_MAX_INDEX                         64
#define IPE_PKT_PROC_PI_FR_UI_FIFO_MAX_INDEX                         64
#define IPE_PKT_PROC_PKT_INFO_FIFO_MAX_INDEX                         64
#define IPE_PKT_PROC_TCAM_DS_FIFO0_MAX_INDEX                         12
#define IPE_PKT_PROC_TCAM_DS_FIFO1_MAX_INDEX                         4
#define IPE_PKT_PROC_TCAM_DS_FIFO2_MAX_INDEX                         4
#define IPE_PKT_PROC_TCAM_DS_FIFO3_MAX_INDEX                         4
#define DS_ECMP_GROUP__REG_RAM__RAM_CHK_REC_MAX_INDEX                1
#define DS_ECMP_STATE_MEM__RAM_CHK_REC_MAX_INDEX                     1
#define DS_RPF__REG_RAM__RAM_CHK_REC_MAX_INDEX                       1
#define DS_SRC_CHANNEL__REG_RAM__RAM_CHK_REC_MAX_INDEX               1
#define DS_STORM_MEM_PART0__RAM_CHK_REC_MAX_INDEX                    1
#define DS_STORM_MEM_PART1__RAM_CHK_REC_MAX_INDEX                    1
#define IPE_ACL_APP_CAM_MAX_INDEX                                    1
#define IPE_ACL_APP_CAM_RESULT_MAX_INDEX                             1
#define IPE_ACL_QOS_CTL_MAX_INDEX                                    1
#define IPE_BRIDGE_CTL_MAX_INDEX                                     1
#define IPE_BRIDGE_EOP_MSG_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX     1
#define IPE_BRIDGE_STORM_CTL_MAX_INDEX                               1
#define IPE_CLA_EOP_MSG_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX        1
#define IPE_CLASSIFICATION_CTL_MAX_INDEX                             1
#define IPE_CLASSIFICATION_DSCP_MAP__REG_RAM__RAM_CHK_REC_MAX_INDEX  1
#define IPE_DS_FWD_CTL_MAX_INDEX                                     1
#define IPE_ECMP_CHANNEL_STATE_MAX_INDEX                             1
#define IPE_ECMP_CTL_MAX_INDEX                                       1
#define IPE_ECMP_TIMER_CTL_MAX_INDEX                                 1
#define IPE_FCOE_CTL_MAX_INDEX                                       1
#define IPE_IPG_CTL_MAX_INDEX                                        1
#define IPE_LEARNING_CACHE_VALID_MAX_INDEX                           1
#define IPE_LEARNING_CTL_MAX_INDEX                                   1
#define IPE_OAM_CTL_MAX_INDEX                                        1
#define IPE_OUTER_LEARNING_CTL_MAX_INDEX                             1
#define IPE_PKT_PROC_CFG_MAX_INDEX                                   1
#define IPE_PKT_PROC_CREDIT_CTL_MAX_INDEX                            1
#define IPE_PKT_PROC_CREDIT_USED_MAX_INDEX                           1
#define IPE_PKT_PROC_DEBUG_STATS_MAX_INDEX                           1
#define IPE_PKT_PROC_DS_FWD_PTR_DEBUG_MAX_INDEX                      1
#define IPE_PKT_PROC_ECC_CTL_MAX_INDEX                               1
#define IPE_PKT_PROC_ECC_STATS_MAX_INDEX                             1
#define IPE_PKT_PROC_EOP_MSG_FIFO_DEPTH_RECORD_MAX_INDEX             1
#define IPE_PKT_PROC_INIT_CTL_MAX_INDEX                              1
#define IPE_PKT_PROC_RAND_SEED_LOAD_MAX_INDEX                        1
#define IPE_PTP_CTL_MAX_INDEX                                        1
#define IPE_ROUTE_CTL_MAX_INDEX                                      1
#define IPE_TRILL_CTL_MAX_INDEX                                      1
#define MAC_LED_CFG_PORT_MODE_MAX_INDEX                              61
#define MAC_LED_CFG_PORT_SEQ_MAP_MAX_INDEX                           61
#define MAC_LED_DRIVER_INTERRUPT_NORMAL_MAX_INDEX                    4
#define MAC_LED_BLINK_CFG_MAX_INDEX                                  1
#define MAC_LED_CFG_ASYNC_FIFO_THR_MAX_INDEX                         1
#define MAC_LED_CFG_CAL_CTL_MAX_INDEX                                1
#define MAC_LED_POLARITY_CFG_MAX_INDEX                               1
#define MAC_LED_PORT_RANGE_MAX_INDEX                                 1
#define MAC_LED_RAW_STATUS_CFG_MAX_INDEX                             1
#define MAC_LED_REFRESH_INTERVAL_MAX_INDEX                           1
#define MAC_LED_SAMPLE_INTERVAL_MAX_INDEX                            1
#define MAC_MUX_INTERRUPT_FATAL_MAX_INDEX                            4
#define MAC_MUX_INTERRUPT_NORMAL_MAX_INDEX                           4
#define MAC_MUX_CAL_MAX_INDEX                                        1
#define MAC_MUX_DEBUG_STATS_MAX_INDEX                                1
#define MAC_MUX_PARITY_ENABLE_MAX_INDEX                              1
#define MAC_MUX_STAT_SEL_MAX_INDEX                                   1
#define MAC_MUX_WALKER_MAX_INDEX                                     1
#define MAC_MUX_WRR_CFG0_MAX_INDEX                                   1
#define MAC_MUX_WRR_CFG1_MAX_INDEX                                   1
#define MAC_MUX_WRR_CFG2_MAX_INDEX                                   1
#define MAC_MUX_WRR_CFG3_MAX_INDEX                                   1
#define PORT_MAP_MAX_INDEX                                           88
#define MDIO_CFG_MAX_INDEX                                           1
#define MDIO_CMD1_G_MAX_INDEX                                        1
#define MDIO_CMD_X_G_MAX_INDEX                                       1
#define MDIO_LINK_DOWN_DETEC_EN_MAX_INDEX                            1
#define MDIO_LINK_STATUS_MAX_INDEX                                   1
#define MDIO_SCAN_CTL_MAX_INDEX                                      1
#define MDIO_SPECI_CFG_MAX_INDEX                                     1
#define MDIO_SPECIFIED_STATUS_MAX_INDEX                              1
#define MDIO_STATUS1_G_MAX_INDEX                                     1
#define MDIO_STATUS_X_G_MAX_INDEX                                    1
#define MDIO_USE_PHY_MAX_INDEX                                       1
#define MDIO_XG_PORT_CHAN_ID_WITH_OUT_PHY_MAX_INDEX                  1
#define DS_APS_BRIDGE_MAX_INDEX                                      1024
#define DS_APS_CHANNEL_MAP_MAX_INDEX                                 64
#define DS_MET_FIFO_EXCP_MAX_INDEX                                   256
#define MET_FIFO_BR_RCD_UPD_FIFO_MAX_INDEX                           256
#define MET_FIFO_INTERRUPT_FATAL_MAX_INDEX                           4
#define MET_FIFO_INTERRUPT_NORMAL_MAX_INDEX                          4
#define MET_FIFO_MSG_RAM_MAX_INDEX                                   768
#define MET_FIFO_Q_MGR_RCD_UPD_FIFO_MAX_INDEX                        64
#define MET_FIFO_RCD_RAM_MAX_INDEX                                   3072
#define DS_APS_BRIDGE__REG_RAM__RAM_CHK_REC_MAX_INDEX                1
#define MET_FIFO_APS_INIT_MAX_INDEX                                  1
#define MET_FIFO_CTL_MAX_INDEX                                       1
#define MET_FIFO_DEBUG_STATS_MAX_INDEX                               1
#define MET_FIFO_DRAIN_ENABLE_MAX_INDEX                              1
#define MET_FIFO_ECC_CTL_MAX_INDEX                                   1
#define MET_FIFO_EN_QUE_CREDIT_MAX_INDEX                             1
#define MET_FIFO_END_PTR_MAX_INDEX                                   1
#define MET_FIFO_EXCP_TAB_PARITY_FAIL_RECORD_MAX_INDEX               1
#define MET_FIFO_INIT_MAX_INDEX                                      1
#define MET_FIFO_INIT_DONE_MAX_INDEX                                 1
#define MET_FIFO_INPUT_FIFO_DEPTH_MAX_INDEX                          1
#define MET_FIFO_INPUT_FIFO_THRESHOLD_MAX_INDEX                      1
#define MET_FIFO_LINK_DOWN_STATE_RECORD_MAX_INDEX                    1
#define MET_FIFO_LINK_SCAN_DONE_STATE_MAX_INDEX                      1
#define MET_FIFO_MAX_INIT_CNT_MAX_INDEX                              1
#define MET_FIFO_MCAST_ENABLE_MAX_INDEX                              1
#define MET_FIFO_MSG_CNT_MAX_INDEX                                   1
#define MET_FIFO_MSG_TYPE_CNT_MAX_INDEX                              1
#define MET_FIFO_PARITY_EN_MAX_INDEX                                 1
#define MET_FIFO_PENDING_MCAST_CNT_MAX_INDEX                         1
#define MET_FIFO_Q_MGR_CREDIT_MAX_INDEX                              1
#define MET_FIFO_Q_WRITE_RCD_UPD_FIFO_THRD_MAX_INDEX                 1
#define MET_FIFO_RD_CUR_STATE_MACHINE_MAX_INDEX                      1
#define MET_FIFO_RD_PTR_MAX_INDEX                                    1
#define MET_FIFO_START_PTR_MAX_INDEX                                 1
#define MET_FIFO_TB_INFO_ARB_CREDIT_MAX_INDEX                        1
#define MET_FIFO_UPDATE_RCD_ERROR_SPOT_MAX_INDEX                     1
#define MET_FIFO_WR_PTR_MAX_INDEX                                    1
#define MET_FIFO_WRR_CFG_MAX_INDEX                                   1
#define MET_FIFO_WRR_WEIGHT_CNT_MAX_INDEX                            1
#define MET_FIFO_WRR_WT_MAX_INDEX                                    1
#define MET_FIFO_WRR_WT_CFG_MAX_INDEX                                1
#define DS_CHANNELIZE_ING_FC_MAX_INDEX                               128
#define DS_CHANNELIZE_MODE_MAX_INDEX                                 60
#define NET_RX_CHAN_INFO_RAM_MAX_INDEX                               64
#define NET_RX_CHANNEL_MAP_MAX_INDEX                                 60
#define NET_RX_INTERRUPT_FATAL_MAX_INDEX                             4
#define NET_RX_INTERRUPT_NORMAL_MAX_INDEX                            4
#define NET_RX_LINK_LIST_TABLE_MAX_INDEX                             256
#define NET_RX_PAUSE_TIMER_MEM_MAX_INDEX                             60
#define NET_RX_PKT_BUF_RAM_MAX_INDEX                                 512
#define NET_RX_PKT_INFO_FIFO_MAX_INDEX                               320
#define DS_CHANNELIZE_ING_FC__REG_RAM__RAM_CHK_REC_MAX_INDEX         1
#define DS_CHANNELIZE_MODE__REG_RAM__RAM_CHK_REC_MAX_INDEX           1
#define NET_RX_BPDU_STATS_MAX_INDEX                                  1
#define NET_RX_BUFFER_COUNT_MAX_INDEX                                1
#define NET_RX_BUFFER_FULL_THRESHOLD_MAX_INDEX                       1
#define NET_RX_CTL_MAX_INDEX                                         1
#define NET_RX_DEBUG_STATS_MAX_INDEX                                 1
#define NET_RX_DRAIN_ENABLE_MAX_INDEX                                1
#define NET_RX_ECC_CTL_MAX_INDEX                                     1
#define NET_RX_ECC_ERROR_STATS_MAX_INDEX                             1
#define NET_RX_FREE_LIST_CTL_MAX_INDEX                               1
#define NET_RX_INT_LK_PKT_SIZE_CHECK_MAX_INDEX                       1
#define NET_RX_INTLK_CHAN_EN_MAX_INDEX                               1
#define NET_RX_IPE_STALL_RECORD_MAX_INDEX                            1
#define NET_RX_MEM_INIT_MAX_INDEX                                    1
#define NET_RX_PARITY_FAIL_RECORD_MAX_INDEX                          1
#define NET_RX_PAUSE_CTL_MAX_INDEX                                   1
#define NET_RX_PAUSE_TYPE_MAX_INDEX                                  1
#define NET_RX_PRE_FETCH_FIFO_DEPTH_MAX_INDEX                        1
#define NET_RX_RESERVED_MAC_DA_CTL_MAX_INDEX                         1
#define NET_RX_STATE_MAX_INDEX                                       1
#define NET_RX_STATS_EN_MAX_INDEX                                    1
#define NET_RX_WRONG_PRIORITY_RECORD_MAX_INDEX                       1
#define DS_DEST_PTP_CHAN_MAX_INDEX                                   60
#define DS_EGR_IB_LPP_INFO_MAX_INDEX                                 128
#define DS_QCN_PORT_MAC_MAX_INDEX                                    60
#define INBD_FC_REQ_TAB_MAX_INDEX                                    32
#define NET_TX_CAL_BAK_MAX_INDEX                                     160
#define NET_TX_CALENDAR_CTL_MAX_INDEX                                160
#define NET_TX_CH_DYNAMIC_INFO_MAX_INDEX                             62
#define NET_TX_CH_STATIC_INFO_MAX_INDEX                              62
#define NET_TX_CHAN_ID_MAP_MAX_INDEX                                 64
#define NET_TX_EEE_TIMER_TAB_MAX_INDEX                               60
#define NET_TX_HDR_BUF_MAX_INDEX                                     1792
#define NET_TX_INTERRUPT_FATAL_MAX_INDEX                             4
#define NET_TX_INTERRUPT_NORMAL_MAX_INDEX                            4
#define NET_TX_PAUSE_TAB_MAX_INDEX                                   60
#define NET_TX_PKT_BUF_MAX_INDEX                                     3584
#define NET_TX_PORT_ID_MAP_MAX_INDEX                                 64
#define NET_TX_PORT_MODE_TAB_MAX_INDEX                               60
#define NET_TX_SGMAC_PRIORITY_MAP_TABLE_MAX_INDEX                    256
#define PAUSE_REQ_TAB_MAX_INDEX                                      60
#define DS_EGR_IB_LPP_INFO__REG_RAM__RAM_CHK_REC_MAX_INDEX           1
#define INBD_FC_LOG_REQ_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX        1
#define NET_TX_CAL_CTL_MAX_INDEX                                     1
#define NET_TX_CFG_CHAN_ID_MAX_INDEX                                 1
#define NET_TX_CHAN_CREDIT_THRD_MAX_INDEX                            1
#define NET_TX_CHAN_CREDIT_USED_MAX_INDEX                            1
#define NET_TX_CHAN_TX_DIS_CFG_MAX_INDEX                             1
#define NET_TX_CHANNEL_EN_MAX_INDEX                                  1
#define NET_TX_CHANNEL_TX_EN_MAX_INDEX                               1
#define NET_TX_CREDIT_CLEAR_CTL_MAX_INDEX                            1
#define NET_TX_DEBUG_STATS_MAX_INDEX                                 1
#define NET_TX_E_E_E_CFG_MAX_INDEX                                   1
#define NET_TX_E_E_E_SLEEP_TIMER_CFG_MAX_INDEX                       1
#define NET_TX_E_E_E_WAKEUP_TIMER_CFG_MAX_INDEX                      1
#define NET_TX_E_LOOP_STALL_RECORD_MAX_INDEX                         1
#define NET_TX_EPE_STALL_CTL_MAX_INDEX                               1
#define NET_TX_FORCE_TX_CFG_MAX_INDEX                                1
#define NET_TX_INBAND_FC_CTL_MAX_INDEX                               1
#define NET_TX_INIT_MAX_INDEX                                        1
#define NET_TX_INIT_DONE_MAX_INDEX                                   1
#define NET_TX_INT_LK_CTL_MAX_INDEX                                  1
#define NET_TX_MISC_CTL_MAX_INDEX                                    1
#define NET_TX_PARITY_ECC_CTL_MAX_INDEX                              1
#define NET_TX_PARITY_FAIL_RECORD_MAX_INDEX                          1
#define NET_TX_PAUSE_QUANTA_CFG_MAX_INDEX                            1
#define NET_TX_PTP_EN_CTL_MAX_INDEX                                  1
#define NET_TX_QCN_CTL_MAX_INDEX                                     1
#define NET_TX_STAT_SEL_MAX_INDEX                                    1
#define NET_TX_TX_THRD_CFG_MAX_INDEX                                 1
#define AUTO_GEN_PKT_INTERRUPT_NORMAL_MAX_INDEX                      4
#define AUTO_GEN_PKT_PKT_CFG_MAX_INDEX                               4
#define AUTO_GEN_PKT_PKT_HDR_MAX_INDEX                               48
#define AUTO_GEN_PKT_RX_PKT_STATS_MAX_INDEX                          4
#define AUTO_GEN_PKT_TX_PKT_STATS_MAX_INDEX                          4
#define AUTO_GEN_PKT_CREDIT_CTL_MAX_INDEX                            1
#define AUTO_GEN_PKT_CTL_MAX_INDEX                                   1
#define AUTO_GEN_PKT_DEBUG_STATS_MAX_INDEX                           1
#define AUTO_GEN_PKT_ECC_CTL_MAX_INDEX                               1
#define AUTO_GEN_PKT_ECC_STATS_MAX_INDEX                             1
#define AUTO_GEN_PKT_PRBS_CTL_MAX_INDEX                              1
#define DS_OAM_EXCP_MAX_INDEX                                        32
#define OAM_FWD_INTERRUPT_FATAL_MAX_INDEX                            4
#define OAM_FWD_INTERRUPT_NORMAL_MAX_INDEX                           4
#define OAM_HDR_EDIT_P_D_IN_FIFO_MAX_INDEX                           64
#define OAM_HDR_EDIT_P_I_IN_FIFO_MAX_INDEX                           4
#define OAM_FWD_CREDIT_CTL_MAX_INDEX                                 1
#define OAM_FWD_DEBUG_STATS_MAX_INDEX                                1
#define OAM_HDR_EDIT_CREDIT_CTL_MAX_INDEX                            1
#define OAM_HDR_EDIT_DEBUG_STATS_MAX_INDEX                           1
#define OAM_HDR_EDIT_DRAIN_ENABLE_MAX_INDEX                          1
#define OAM_HDR_EDIT_PARITY_ENABLE_MAX_INDEX                         1
#define OAM_HEADER_EDIT_CTL_MAX_INDEX                                1
#define OAM_PARSER_INTERRUPT_FATAL_MAX_INDEX                         4
#define OAM_PARSER_INTERRUPT_NORMAL_MAX_INDEX                        4
#define OAM_PARSER_PKT_FIFO_MAX_INDEX                                32
#define OAM_HEADER_ADJUST_CTL_MAX_INDEX                              1
#define OAM_PARSER_CTL_MAX_INDEX                                     1
#define OAM_PARSER_DEBUG_CNT_MAX_INDEX                               1
#define OAM_PARSER_ETHER_CTL_MAX_INDEX                               1
#define OAM_PARSER_FLOW_CTL_MAX_INDEX                                1
#define OAM_PARSER_LAYER2_PROTOCOL_CAM_MAX_INDEX                     1
#define OAM_PARSER_LAYER2_PROTOCOL_CAM_VALID_MAX_INDEX               1
#define OAM_PARSER_PKT_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX         1
#define DS_MP_MAX_INDEX                                              16384
#define DS_OAM_DEFECT_STATUS_MAX_INDEX                               256
#define DS_PORT_PROPERTY_MAX_INDEX                                   192
#define DS_PRIORITY_MAP_MAX_INDEX                                    8
#define OAM_DEFECT_CACHE_MAX_INDEX                                   16
#define OAM_PROC_INTERRUPT_FATAL_MAX_INDEX                           4
#define OAM_PROC_INTERRUPT_NORMAL_MAX_INDEX                          4
#define OAM_RX_PROC_P_I_IN_FIFO_MAX_INDEX                            2
#define OAM_RX_PROC_P_R_IN_FIFO_MAX_INDEX                            2
#define OAM_DEFECT_DEBUG_MAX_INDEX                                   1
#define OAM_DS_MP_DATA_MASK_MAX_INDEX                                1
#define OAM_ERROR_DEFECT_CTL_MAX_INDEX                               1
#define OAM_ETHER_CCI_CTL_MAX_INDEX                                  1
#define OAM_ETHER_SEND_ID_MAX_INDEX                                  1
#define OAM_ETHER_TX_CTL_MAX_INDEX                                   1
#define OAM_ETHER_TX_MAC_MAX_INDEX                                   1
#define OAM_PROC_CTL_MAX_INDEX                                       1
#define OAM_PROC_DEBUG_STATS_MAX_INDEX                               1
#define OAM_PROC_ECC_CTL_MAX_INDEX                                   1
#define OAM_PROC_ECC_STATS_MAX_INDEX                                 1
#define OAM_RX_PROC_ETHER_CTL_MAX_INDEX                              1
#define OAM_TBL_ADDR_CTL_MAX_INDEX                                   1
#define OAM_UPDATE_APS_CTL_MAX_INDEX                                 1
#define OAM_UPDATE_CTL_MAX_INDEX                                     1
#define OAM_UPDATE_STATUS_MAX_INDEX                                  1
#define DS_OOB_FC_CALENDAR_MAX_INDEX                                 96
#define DS_OOB_FC_GRP_MAP_MAX_INDEX                                  96
#define OOB_FC_INTERRUPT_FATAL_MAX_INDEX                             4
#define OOB_FC_INTERRUPT_NORMAL_MAX_INDEX                            4
#define OOB_FC_CFG_CHAN_NUM_MAX_INDEX                                1
#define OOB_FC_CFG_FLOW_CTL_MAX_INDEX                                1
#define OOB_FC_CFG_INGS_MODE_MAX_INDEX                               1
#define OOB_FC_CFG_PORT_NUM_MAX_INDEX                                1
#define OOB_FC_CFG_RX_PROC_MAX_INDEX                                 1
#define OOB_FC_CFG_SPI_MODE_MAX_INDEX                                1
#define OOB_FC_DEBUG_STATS_MAX_INDEX                                 1
#define OOB_FC_EGS_VEC_REG_MAX_INDEX                                 1
#define OOB_FC_ERROR_STATS_MAX_INDEX                                 1
#define OOB_FC_FRAME_GAP_NUM_MAX_INDEX                               1
#define OOB_FC_FRAME_UPDATE_STATE_MAX_INDEX                          1
#define OOB_FC_INGS_VEC_REG_MAX_INDEX                                1
#define OOB_FC_INIT_MAX_INDEX                                        1
#define OOB_FC_INIT_DONE_MAX_INDEX                                   1
#define OOB_FC_PARITY_EN_MAX_INDEX                                   1
#define OOB_FC_RX_RCV_EN_MAX_INDEX                                   1
#define OOB_FC_TX_FIFO_A_FULL_THRD_MAX_INDEX                         1
#define PARSER_INTERRUPT_FATAL_MAX_INDEX                             4
#define IPE_INTF_MAP_PARSER_CREDIT_THRD_MAX_INDEX                    1
#define PARSER_ETHERNET_CTL_MAX_INDEX                                1
#define PARSER_FCOE_CTL_MAX_INDEX                                    1
#define PARSER_IP_HASH_CTL_MAX_INDEX                                 1
#define PARSER_LAYER2_FLEX_CTL_MAX_INDEX                             1
#define PARSER_LAYER2_PROTOCOL_CAM_MAX_INDEX                         1
#define PARSER_LAYER2_PROTOCOL_CAM_VALID_MAX_INDEX                   1
#define PARSER_LAYER3_FLEX_CTL_MAX_INDEX                             1
#define PARSER_LAYER3_HASH_CTL_MAX_INDEX                             1
#define PARSER_LAYER3_PROTOCOL_CAM_MAX_INDEX                         1
#define PARSER_LAYER3_PROTOCOL_CAM_VALID_MAX_INDEX                   1
#define PARSER_LAYER3_PROTOCOL_CTL_MAX_INDEX                         1
#define PARSER_LAYER4_ACH_CTL_MAX_INDEX                              1
#define PARSER_LAYER4_APP_CTL_MAX_INDEX                              1
#define PARSER_LAYER4_APP_DATA_CTL_MAX_INDEX                         1
#define PARSER_LAYER4_FLAG_OP_CTL_MAX_INDEX                          1
#define PARSER_LAYER4_FLEX_CTL_MAX_INDEX                             1
#define PARSER_LAYER4_HASH_CTL_MAX_INDEX                             1
#define PARSER_LAYER4_PORT_OP_CTL_MAX_INDEX                          1
#define PARSER_LAYER4_PORT_OP_SEL_MAX_INDEX                          1
#define PARSER_MPLS_CTL_MAX_INDEX                                    1
#define PARSER_PACKET_TYPE_MAP_MAX_INDEX                             1
#define PARSER_PARITY_EN_MAX_INDEX                                   1
#define PARSER_PBB_CTL_MAX_INDEX                                     1
#define PARSER_TRILL_CTL_MAX_INDEX                                   1
#define HDR_WR_REQ_FIFO_MAX_INDEX                                    32
#define PB_CTL_HDR_BUF_MAX_INDEX                                     12288
#define PB_CTL_INTERRUPT_FATAL_MAX_INDEX                             4
#define PB_CTL_INTERRUPT_NORMAL_MAX_INDEX                            4
#define PB_CTL_PKT_BUF_MAX_INDEX                                     49152
#define PKT_WR_REQ_FIFO_MAX_INDEX                                    32
#define PB_CTL_DEBUG_STATS_MAX_INDEX                                 1
#define PB_CTL_ECC_CTL_MAX_INDEX                                     1
#define PB_CTL_INIT_MAX_INDEX                                        1
#define PB_CTL_INIT_DONE_MAX_INDEX                                   1
#define PB_CTL_PARITY_CTL_MAX_INDEX                                  1
#define PB_CTL_PARITY_FAIL_RECORD_MAX_INDEX                          1
#define PB_CTL_RD_CREDIT_CTL_MAX_INDEX                               1
#define PB_CTL_REF_CTL_MAX_INDEX                                     1
#define PB_CTL_REQ_HOLD_STATS_MAX_INDEX                              1
#define PB_CTL_WEIGHT_CFG_MAX_INDEX                                  1
#define DESC_RX_MEM_BASE_MAX_INDEX                                   8
#define DESC_RX_MEM_DEPTH_MAX_INDEX                                  8
#define DESC_RX_VLD_NUM_MAX_INDEX                                    8
#define DESC_TX_MEM_BASE_MAX_INDEX                                   8
#define DESC_TX_MEM_DEPTH_MAX_INDEX                                  8
#define DESC_TX_VLD_NUM_MAX_INDEX                                    8
#define DESC_WRBK_FIFO_MAX_INDEX                                     4
#define DMA_CHAN_MAP_MAX_INDEX                                       64
#define DMA_RX_PTR_TAB_MAX_INDEX                                     8
#define DMA_TX_PTR_TAB_MAX_INDEX                                     8
#define GBIF_REG_MEM_MAX_INDEX                                       64
#define LEARN_INFO_FIFO_MAX_INDEX                                    16
#define PCI_EXP_CORE_INTERRUPT_NORMAL_MAX_INDEX                      4
#define PCI_EXP_DESC_MEM_MAX_INDEX                                   128
#define PCIE_FUNC_INTR_MAX_INDEX                                     4
#define PKT_TX_INFO_FIFO_MAX_INDEX                                   8
#define REG_RD_INFO_FIFO_MAX_INDEX                                   2
#define SYS_MEM_BASE_TAB_MAX_INDEX                                   8
#define TAB_WR_INFO_FIFO_MAX_INDEX                                   2
#define DESC_WRBK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX              1
#define DMA_CHAN_CFG_MAX_INDEX                                       1
#define DMA_DEBUG_STATS_MAX_INDEX                                    1
#define DMA_DESC_ERROR_REC_MAX_INDEX                                 1
#define DMA_ECC_CTL_MAX_INDEX                                        1
#define DMA_ENDIAN_CFG_MAX_INDEX                                     1
#define DMA_FIFO_DEPTH_MAX_INDEX                                     1
#define DMA_FIFO_THRD_CFG_MAX_INDEX                                  1
#define DMA_GEN_INTR_CTL_MAX_INDEX                                   1
#define DMA_INIT_CTL_MAX_INDEX                                       1
#define DMA_LEARN_MEM_CTL_MAX_INDEX                                  1
#define DMA_LEARN_MISC_CTL_MAX_INDEX                                 1
#define DMA_LEARN_VALID_CTL_MAX_INDEX                                1
#define DMA_MISC_CTL_MAX_INDEX                                       1
#define DMA_PKT_MISC_CTL_MAX_INDEX                                   1
#define DMA_PKT_STATS_MAX_INDEX                                      1
#define DMA_PKT_TIMER_CTL_MAX_INDEX                                  1
#define DMA_PKT_TX_DEBUG_STATS_MAX_INDEX                             1
#define DMA_RD_TIMER_CTL_MAX_INDEX                                   1
#define DMA_STATE_DEBUG_MAX_INDEX                                    1
#define DMA_STATS_BMP_CFG_MAX_INDEX                                  1
#define DMA_STATS_ENTRY_CFG_MAX_INDEX                                1
#define DMA_STATS_INTERVAL_CFG_MAX_INDEX                             1
#define DMA_STATS_OFFSET_CFG_MAX_INDEX                               1
#define DMA_STATS_USER_CFG_MAX_INDEX                                 1
#define DMA_TAG_STATS_MAX_INDEX                                      1
#define DMA_WR_TIMER_CTL_MAX_INDEX                                   1
#define PCI_EXP_DESC_MEM__REG_RAM__RAM_CHK_REC_MAX_INDEX             1
#define PCIE_INTR_CFG_MAX_INDEX                                      1
#define PCIE_OUTBOUND_STATS_MAX_INDEX                                1
#define PCIE_TAG_CTL_MAX_INDEX                                       1
#define PKT_RX_DATA_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX            1
#define PKT_TX_DATA_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX            1
#define PKT_TX_INFO_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX            1
#define REG_RD_INFO_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX            1
#define TAB_WR_DATA_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX            1
#define TAB_WR_INFO_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX            1
#define DS_POLICER_CONTROL_MAX_INDEX                                 2048
#define DS_POLICER_COUNT_MAX_INDEX                                   2048
#define DS_POLICER_PROFILE_MAX_INDEX                                 256
#define POLICING_EPE_FIFO_MAX_INDEX                                  16
#define POLICING_INTERRUPT_FATAL_MAX_INDEX                           4
#define POLICING_INTERRUPT_NORMAL_MAX_INDEX                          4
#define POLICING_IPE_FIFO_MAX_INDEX                                  16
#define DS_POLICER_CONTROL__RAM_CHK_REC_MAX_INDEX                    1
#define DS_POLICER_COUNT__RAM_CHK_REC_MAX_INDEX                      1
#define DS_POLICER_PROFILE_RAM_CHK_REC_MAX_INDEX                     1
#define IPE_POLICING_CTL_MAX_INDEX                                   1
#define POLICING_DEBUG_STATS_MAX_INDEX                               1
#define POLICING_ECC_CTL_MAX_INDEX                                   1
#define POLICING_EPE_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX           1
#define POLICING_INIT_CTL_MAX_INDEX                                  1
#define POLICING_IPE_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX           1
#define POLICING_MISC_CTL_MAX_INDEX                                  1
#define UPDATE_REQ_IGNORE_RECORD_MAX_INDEX                           1
#define PTP_CAPTURED_ADJ_FRC_MAX_INDEX                               1
#define PTP_ENGINE_INTERRUPT_FATAL_MAX_INDEX                         4
#define PTP_ENGINE_INTERRUPT_NORMAL_MAX_INDEX                        4
#define PTP_MAC_TX_CAPTURE_TS_MAX_INDEX                              1
#define PTP_SYNC_INTF_HALF_PERIOD_MAX_INDEX                          1
#define PTP_SYNC_INTF_TOGGLE_MAX_INDEX                               1
#define PTP_TOD_TOW_MAX_INDEX                                        1
#define PTP_ENGINE_FIFO_DEPTH_RECORD_MAX_INDEX                       1
#define PTP_FRC_QUANTA_MAX_INDEX                                     1
#define PTP_MAC_TX_CAPTURE_TS_CTRL_MAX_INDEX                         1
#define PTP_PARITY_EN_MAX_INDEX                                      1
#define PTP_SYNC_INTF_CFG_MAX_INDEX                                  1
#define PTP_SYNC_INTF_INPUT_CODE_MAX_INDEX                           1
#define PTP_SYNC_INTF_INPUT_TS_MAX_INDEX                             1
#define PTP_TOD_CFG_MAX_INDEX                                        1
#define PTP_TOD_CODE_CFG_MAX_INDEX                                   1
#define PTP_TOD_INPUT_CODE_MAX_INDEX                                 1
#define PTP_TOD_INPUT_TS_MAX_INDEX                                   1
#define PTP_TS_CAPTURE_CTRL_MAX_INDEX                                1
#define PTP_TS_CAPTURE_DROP_CNT_MAX_INDEX                            1
#define PTP_DRIFT_RATE_ADJUST_MAX_INDEX                              1
#define PTP_OFFSET_ADJUST_MAX_INDEX                                  1
#define PTP_REF_FRC_MAX_INDEX                                        1
#define PTP_FRC_CTL_MAX_INDEX                                        1
#define DS_CHAN_ACTIVE_LINK_MAX_INDEX                                64
#define DS_CHAN_BACKUP_LINK_MAX_INDEX                                64
#define DS_CHAN_CREDIT_MAX_INDEX                                     64
#define DS_CHAN_SHP_OLD_TOKEN_MAX_INDEX                              64
#define DS_CHAN_SHP_PROFILE_MAX_INDEX                                64
#define DS_CHAN_SHP_TOKEN_MAX_INDEX                                  64
#define DS_CHAN_STATE_MAX_INDEX                                      64
#define DS_DLB_TOKEN_THRD_MAX_INDEX                                  4
#define DS_GRP_FWD_LINK_LIST0_MAX_INDEX                              256
#define DS_GRP_FWD_LINK_LIST1_MAX_INDEX                              256
#define DS_GRP_FWD_LINK_LIST2_MAX_INDEX                              256
#define DS_GRP_FWD_LINK_LIST3_MAX_INDEX                              256
#define DS_GRP_NEXT_LINK_LIST0_MAX_INDEX                             256
#define DS_GRP_NEXT_LINK_LIST1_MAX_INDEX                             256
#define DS_GRP_NEXT_LINK_LIST2_MAX_INDEX                             256
#define DS_GRP_NEXT_LINK_LIST3_MAX_INDEX                             256
#define DS_GRP_SHP_PROFILE_MAX_INDEX                                 32
#define DS_GRP_SHP_STATE_MAX_INDEX                                   128
#define DS_GRP_SHP_TOKEN_MAX_INDEX                                   128
#define DS_GRP_SHP_WFQ_CTL_MAX_INDEX                                 256
#define DS_GRP_STATE_MAX_INDEX                                       256
#define DS_GRP_WFQ_DEFICIT_MAX_INDEX                                 256
#define DS_GRP_WFQ_STATE_MAX_INDEX                                   256
#define DS_OOB_FC_PRI_STATE_MAX_INDEX                                256
#define DS_PACKET_LINK_LIST_MAX_INDEX                                12288
#define DS_PACKET_LINK_STATE_MAX_INDEX                               1024
#define DS_QUE_ENTRY_AGING_MAX_INDEX                                 768
#define DS_QUE_MAP_MAX_INDEX                                         1024
#define DS_QUE_SHP_CTL_MAX_INDEX                                     256
#define DS_QUE_SHP_PROFILE_MAX_INDEX                                 64
#define DS_QUE_SHP_STATE_MAX_INDEX                                   256
#define DS_QUE_SHP_TOKEN_MAX_INDEX                                   1024
#define DS_QUE_SHP_WFQ_CTL_MAX_INDEX                                 1024
#define DS_QUE_STATE_MAX_INDEX                                       256
#define DS_QUE_WFQ_DEFICIT_MAX_INDEX                                 1024
#define DS_QUE_WFQ_STATE_MAX_INDEX                                   256
#define Q_MGR_DEQ_INTERRUPT_FATAL_MAX_INDEX                          4
#define Q_MGR_DEQ_INTERRUPT_NORMAL_MAX_INDEX                         4
#define Q_MGR_NETWORK_WT_CFG_MEM_MAX_INDEX                           64
#define Q_MGR_NETWORK_WT_MEM_MAX_INDEX                               64
#define DS_CHAN_ACTIVE_LINK__REG_RAM__RAM_CHK_REC_MAX_INDEX          1
#define DS_CHAN_BACKUP_LINK__REG_RAM__RAM_CHK_REC_MAX_INDEX          1
#define DS_CHAN_CREDIT__REG_RAM__RAM_CHK_REC_MAX_INDEX               1
#define DS_CHAN_SHP_OLD_TOKEN__REG_RAM__RAM_CHK_REC_MAX_INDEX        1
#define DS_CHAN_SHP_PROFILE__REG_RAM__RAM_CHK_REC_MAX_INDEX          1
#define DS_CHAN_SHP_TOKEN__REG_RAM__RAM_CHK_REC_MAX_INDEX            1
#define DS_GRP_FWD_LINK_LIST0__REG_RAM__RAM_CHK_REC_MAX_INDEX        1
#define DS_GRP_FWD_LINK_LIST1__REG_RAM__RAM_CHK_REC_MAX_INDEX        1
#define DS_GRP_FWD_LINK_LIST2__REG_RAM__RAM_CHK_REC_MAX_INDEX        1
#define DS_GRP_FWD_LINK_LIST3__REG_RAM__RAM_CHK_REC_MAX_INDEX        1
#define DS_GRP_NEXT_LINK_LIST0__REG_RAM__RAM_CHK_REC_MAX_INDEX       1
#define DS_GRP_NEXT_LINK_LIST1__REG_RAM__RAM_CHK_REC_MAX_INDEX       1
#define DS_GRP_NEXT_LINK_LIST2__REG_RAM__RAM_CHK_REC_MAX_INDEX       1
#define DS_GRP_NEXT_LINK_LIST3__REG_RAM__RAM_CHK_REC_MAX_INDEX       1
#define DS_GRP_SHP_PROFILE__REG_RAM__RAM_CHK_REC_MAX_INDEX           1
#define DS_GRP_SHP_STATE__REG_RAM__RAM_CHK_REC_MAX_INDEX             1
#define DS_GRP_SHP_TOKEN__REG_RAM__RAM_CHK_REC_MAX_INDEX             1
#define DS_GRP_SHP_WFQ_CTL__REG_RAM__RAM_CHK_REC_MAX_INDEX           1
#define DS_GRP_STATE__REG_RAM__RAM_CHK_REC_MAX_INDEX                 1
#define DS_GRP_WFQ_DEFICIT__REG_RAM__RAM_CHK_REC_MAX_INDEX           1
#define DS_OOB_FC_PRI_STATE__REG_RAM__RAM_CHK_REC_MAX_INDEX          1
#define DS_PACKET_LINK_LIST__REG_RAM__RAM_CHK_REC_MAX_INDEX          1
#define DS_PACKET_LINK_STATE__REG_RAM__RAM_CHK_REC_MAX_INDEX         1
#define DS_QUE_MAP__REG_RAM__RAM_CHK_REC_MAX_INDEX                   1
#define DS_QUE_SHP_CTL__REG_RAM__RAM_CHK_REC_MAX_INDEX               1
#define DS_QUE_SHP_PROFILE__REG_RAM__RAM_CHK_REC_MAX_INDEX           1
#define DS_QUE_SHP_STATE__REG_RAM__RAM_CHK_REC_MAX_INDEX             1
#define DS_QUE_SHP_TOKEN__REG_RAM__RAM_CHK_REC_MAX_INDEX             1
#define DS_QUE_SHP_WFQ_CTL__REG_RAM__RAM_CHK_REC_MAX_INDEX           1
#define DS_QUE_STATE__REG_RAM__RAM_CHK_REC_MAX_INDEX                 1
#define DS_QUE_WFQ_DEFICIT__REG_RAM__RAM_CHK_REC_MAX_INDEX           1
#define DS_QUE_WFQ_STATE__REG_RAM__RAM_CHK_REC_MAX_INDEX             1
#define Q_DLB_CHAN_SPEED_MODE_CTL_MAX_INDEX                          1
#define Q_MGR_AGING_CTL_MAX_INDEX                                    1
#define Q_MGR_AGING_MEM_INIT_CTL_MAX_INDEX                           1
#define Q_MGR_CHAN_FLUSH_CTL_MAX_INDEX                               1
#define Q_MGR_CHAN_SHAPE_CTL_MAX_INDEX                               1
#define Q_MGR_CHAN_SHAPE_STATE_MAX_INDEX                             1
#define Q_MGR_DEQ_DEBUG_STATS_MAX_INDEX                              1
#define Q_MGR_DEQ_ECC_CTL_MAX_INDEX                                  1
#define Q_MGR_DEQ_FIFO_DEPTH_MAX_INDEX                               1
#define Q_MGR_DEQ_FREE_PKT_LIST_CTL_MAX_INDEX                        1
#define Q_MGR_DEQ_INIT_CTL_MAX_INDEX                                 1
#define Q_MGR_DEQ_PARITY_ENABLE_MAX_INDEX                            1
#define Q_MGR_DEQ_PKT_LINK_LIST_INIT_CTL_MAX_INDEX                   1
#define Q_MGR_DEQ_PRI_TO_COS_MAP_MAX_INDEX                           1
#define Q_MGR_DEQ_SCH_CTL_MAX_INDEX                                  1
#define Q_MGR_DEQ_STALL_INIT_CTL_MAX_INDEX                           1
#define Q_MGR_DLB_CTL_MAX_INDEX                                      1
#define Q_MGR_DRAIN_CTL_MAX_INDEX                                    1
#define Q_MGR_FREE_LIST_FIFO_CREDIT_CTL_MAX_INDEX                    1
#define Q_MGR_GRP_SHAPE_CTL_MAX_INDEX                                1
#define Q_MGR_INT_LK_STALL_CTL_MAX_INDEX                             1
#define Q_MGR_INTF_WRR_WT_CTL_MAX_INDEX                              1
#define Q_MGR_MISC_INTF_WRR_WT_CTL_MAX_INDEX                         1
#define Q_MGR_OOB_FC_CTL_MAX_INDEX                                   1
#define Q_MGR_OOB_FC_STATUS_MAX_INDEX                                1
#define Q_MGR_QUE_ENTRY_CREDIT_CTL_MAX_INDEX                         1
#define Q_MGR_QUE_SHAPE_CTL_MAX_INDEX                                1
#define DS_EGR_PORT_CNT_MAX_INDEX                                    64
#define DS_EGR_PORT_DROP_PROFILE_MAX_INDEX                           64
#define DS_EGR_PORT_FC_STATE_MAX_INDEX                               64
#define DS_EGR_RESRC_CTL_MAX_INDEX                                   1024
#define DS_GRP_CNT_MAX_INDEX                                         256
#define DS_GRP_DROP_PROFILE_MAX_INDEX                                64
#define DS_HEAD_HASH_MOD_MAX_INDEX                                   256
#define DS_LINK_AGGREGATE_CHANNEL_MAX_INDEX                          64
#define DS_LINK_AGGREGATE_CHANNEL_GROUP_MAX_INDEX                    16
#define DS_LINK_AGGREGATE_CHANNEL_MEMBER_MAX_INDEX                   512
#define DS_LINK_AGGREGATE_CHANNEL_MEMBER_SET_MAX_INDEX               16
#define DS_LINK_AGGREGATE_GROUP_MAX_INDEX                            64
#define DS_LINK_AGGREGATE_MEMBER_MAX_INDEX                           1024
#define DS_LINK_AGGREGATE_MEMBER_SET_MAX_INDEX                       32
#define DS_LINK_AGGREGATE_SGMAC_GROUP_MAX_INDEX                      64
#define DS_LINK_AGGREGATE_SGMAC_MEMBER_MAX_INDEX                     512
#define DS_LINK_AGGREGATE_SGMAC_MEMBER_SET_MAX_INDEX                 32
#define DS_LINK_AGGREGATION_PORT_MAX_INDEX                           128
#define DS_QCN_PROFILE_MAX_INDEX                                     16
#define DS_QCN_PROFILE_ID_MAX_INDEX                                  24
#define DS_QCN_QUE_DEPTH_MAX_INDEX                                   96
#define DS_QCN_QUE_ID_BASE_MAX_INDEX                                 64
#define DS_QUE_CNT_MAX_INDEX                                         1024
#define DS_QUE_THRD_PROFILE_MAX_INDEX                                64
#define DS_QUEUE_HASH_KEY_MAX_INDEX                                  128
#define DS_QUEUE_NUM_GEN_CTL_MAX_INDEX                               256
#define DS_QUEUE_SELECT_MAP_MAX_INDEX                                256
#define DS_SGMAC_HEAD_HASH_MOD_MAX_INDEX                             256
#define DS_SGMAC_MAP_MAX_INDEX                                       32
#define DS_UNIFORM_RAND_MAX_INDEX                                    128
#define Q_MGR_ENQ_BR_RTN_FIFO_MAX_INDEX                              2
#define Q_MGR_ENQ_CHAN_CLASS_MAX_INDEX                               64
#define Q_MGR_ENQ_FREE_LIST_FIFO_MAX_INDEX                           4
#define Q_MGR_ENQ_INTERRUPT_FATAL_MAX_INDEX                          4
#define Q_MGR_ENQ_INTERRUPT_NORMAL_MAX_INDEX                         4
#define Q_MGR_ENQ_MSG_FIFO_MAX_INDEX                                 16
#define Q_MGR_ENQ_QUE_NUM_IN_FIFO_MAX_INDEX                          16
#define Q_MGR_ENQ_QUE_NUM_OUT_FIFO_MAX_INDEX                         12
#define Q_MGR_ENQ_SCH_FIFO_MAX_INDEX                                 6
#define Q_MGR_ENQ_WRED_INDEX_MAX_INDEX                               1024
#define DS_EGR_RESRC_CTL__REG_RAM__RAM_CHK_REC_MAX_INDEX             1
#define DS_GRP_CNT__REG_RAM__RAM_CHK_REC_MAX_INDEX                   1
#define DS_HEAD_HASH_MOD__REG_RAM__RAM_CHK_REC_MAX_INDEX             1
#define DS_LINK_AGGREGATE_CHANNEL_MEMBER__REG_RAM__RAM_CHK_REC_MAX_INDEX 1
#define DS_LINK_AGGREGATE_MEMBER_SET__REG_RAM__RAM_CHK_REC_MAX_INDEX 1
#define DS_LINK_AGGREGATE_MEMBER__REG_RAM__RAM_CHK_REC_MAX_INDEX     1
#define DS_LINK_AGGREGATE_SGMAC_MEMBER__REG_RAM__RAM_CHK_REC_MAX_INDEX 1
#define DS_LINK_AGGREGATION_PORT__REG_RAM__RAM_CHK_REC_MAX_INDEX     1
#define DS_QCN_QUE_DEPTH__REG_RAM__RAM_CHK_REC_MAX_INDEX             1
#define DS_QUE_CNT__REG_RAM__RAM_CHK_REC_MAX_INDEX                   1
#define DS_QUE_THRD_PROFILE__REG_RAM__RAM_CHK_REC_MAX_INDEX          1
#define DS_QUEUE_HASH_KEY__REG_RAM__RAM_CHK_REC_MAX_INDEX            1
#define DS_QUEUE_NUM_GEN_CTL__REG_RAM__RAM_CHK_REC_MAX_INDEX         1
#define DS_QUEUE_SELECT_MAP__REG_RAM__RAM_CHK_REC_MAX_INDEX          1
#define DS_SGMAC_HEAD_HASH_MOD__REG_RAM__RAM_CHK_REC_MAX_INDEX       1
#define EGR_COND_DIS_PROFILE_MAX_INDEX                               1
#define EGR_CONGEST_LEVEL_THRD_MAX_INDEX                             1
#define EGR_RESRC_MGR_CTL_MAX_INDEX                                  1
#define EGR_SC_CNT_MAX_INDEX                                         1
#define EGR_SC_THRD_MAX_INDEX                                        1
#define EGR_TC_CNT_MAX_INDEX                                         1
#define EGR_TC_THRD_MAX_INDEX                                        1
#define EGR_TOTAL_CNT_MAX_INDEX                                      1
#define Q_HASH_CAM_CTL_MAX_INDEX                                     1
#define Q_LINK_AGG_TIMER_CTL0_MAX_INDEX                              1
#define Q_LINK_AGG_TIMER_CTL1_MAX_INDEX                              1
#define Q_LINK_AGG_TIMER_CTL2_MAX_INDEX                              1
#define Q_MGR_ENQ_CREDIT_CONFIG_MAX_INDEX                            1
#define Q_MGR_ENQ_CREDIT_USED_MAX_INDEX                              1
#define Q_MGR_ENQ_CTL_MAX_INDEX                                      1
#define Q_MGR_ENQ_DEBUG_FSM_MAX_INDEX                                1
#define Q_MGR_ENQ_DEBUG_STATS_MAX_INDEX                              1
#define Q_MGR_ENQ_DRAIN_ENABLE_MAX_INDEX                             1
#define Q_MGR_ENQ_DROP_CTL_MAX_INDEX                                 1
#define Q_MGR_ENQ_ECC_CTL_MAX_INDEX                                  1
#define Q_MGR_ENQ_INIT_MAX_INDEX                                     1
#define Q_MGR_ENQ_INIT_DONE_MAX_INDEX                                1
#define Q_MGR_ENQ_LENGTH_ADJUST_MAX_INDEX                            1
#define Q_MGR_ENQ_LINK_DOWN_SCAN_STATE_MAX_INDEX                     1
#define Q_MGR_ENQ_LINK_STATE_MAX_INDEX                               1
#define Q_MGR_ENQ_RAND_SEED_LOAD_FORCE_DROP_MAX_INDEX                1
#define Q_MGR_ENQ_SCAN_LINK_DOWN_CHAN_RECORD_MAX_INDEX               1
#define Q_MGR_ENQ_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_MAX_INDEX        1
#define Q_MGR_NET_PKT_ADJ_MAX_INDEX                                  1
#define Q_MGR_QUE_DEQ_STATS_BASE_MAX_INDEX                           1
#define Q_MGR_QUE_ENQ_STATS_BASE_MAX_INDEX                           1
#define Q_MGR_QUEUE_ID_MON_MAX_INDEX                                 1
#define Q_MGR_RAND_SEED_LOAD_MAX_INDEX                               1
#define Q_MGR_RESERVED_CHANNEL_RANGE_MAX_INDEX                       1
#define Q_WRITE_CTL_MAX_INDEX                                        1
#define Q_WRITE_SGMAC_CTL_MAX_INDEX                                  1
#define QUE_MIN_PROFILE_MAX_INDEX                                    1
#define DS_QUEUE_ENTRY_MAX_INDEX                                     12288
#define Q_MGR_QUE_ENTRY_INTERRUPT_FATAL_MAX_INDEX                    4
#define Q_MGR_QUE_ENTRY_INTERRUPT_NORMAL_MAX_INDEX                   4
#define Q_MGR_QUE_ENTRY_DS_CHECK_FAIL_RECORD_MAX_INDEX               1
#define Q_MGR_QUE_ENTRY_ECC_CTL_MAX_INDEX                            1
#define Q_MGR_QUE_ENTRY_INIT_CTL_MAX_INDEX                           1
#define Q_MGR_QUE_ENTRY_REFRESH_CTL_MAX_INDEX                        1
#define Q_MGR_QUE_ENTRY_STATS_MAX_INDEX                              1
#define QSGMII_INTERRUPT_NORMAL_MAX_INDEX                            4
#define QSGMII_PCS0_ANEG_CFG_MAX_INDEX                               1
#define QSGMII_PCS0_ANEG_STATUS_MAX_INDEX                            1
#define QSGMII_PCS1_ANEG_CFG_MAX_INDEX                               1
#define QSGMII_PCS1_ANEG_STATUS_MAX_INDEX                            1
#define QSGMII_PCS2_ANEG_CFG_MAX_INDEX                               1
#define QSGMII_PCS2_ANEG_STATUS_MAX_INDEX                            1
#define QSGMII_PCS3_ANEG_CFG_MAX_INDEX                               1
#define QSGMII_PCS3_ANEG_STATUS_MAX_INDEX                            1
#define QSGMII_PCS_CFG_MAX_INDEX                                     1
#define QSGMII_PCS_CODE_ERR_CNT_MAX_INDEX                            1
#define QSGMII_PCS_SOFT_RST_MAX_INDEX                                1
#define QSGMII_PCS_STATUS_MAX_INDEX                                  1
#define QUAD_MAC_INTERRUPT_NORMAL_MAX_INDEX                          4
#define QUAD_MAC_STATS_RAM_MAX_INDEX                                 144
#define QUAD_MAC_DATA_ERR_CTL_MAX_INDEX                              1
#define QUAD_MAC_DEBUG_STATS_MAX_INDEX                               1
#define QUAD_MAC_GMAC0_MODE_MAX_INDEX                                1
#define QUAD_MAC_GMAC0_PTP_CFG_MAX_INDEX                             1
#define QUAD_MAC_GMAC0_PTP_STATUS_MAX_INDEX                          1
#define QUAD_MAC_GMAC0_RX_CTRL_MAX_INDEX                             1
#define QUAD_MAC_GMAC0_SOFT_RST_MAX_INDEX                            1
#define QUAD_MAC_GMAC0_TX_CTRL_MAX_INDEX                             1
#define QUAD_MAC_GMAC1_MODE_MAX_INDEX                                1
#define QUAD_MAC_GMAC1_PTP_CFG_MAX_INDEX                             1
#define QUAD_MAC_GMAC1_PTP_STATUS_MAX_INDEX                          1
#define QUAD_MAC_GMAC1_RX_CTRL_MAX_INDEX                             1
#define QUAD_MAC_GMAC1_SOFT_RST_MAX_INDEX                            1
#define QUAD_MAC_GMAC1_TX_CTRL_MAX_INDEX                             1
#define QUAD_MAC_GMAC2_MODE_MAX_INDEX                                1
#define QUAD_MAC_GMAC2_PTP_CFG_MAX_INDEX                             1
#define QUAD_MAC_GMAC2_PTP_STATUS_MAX_INDEX                          1
#define QUAD_MAC_GMAC2_RX_CTRL_MAX_INDEX                             1
#define QUAD_MAC_GMAC2_SOFT_RST_MAX_INDEX                            1
#define QUAD_MAC_GMAC2_TX_CTRL_MAX_INDEX                             1
#define QUAD_MAC_GMAC3_MODE_MAX_INDEX                                1
#define QUAD_MAC_GMAC3_PTP_CFG_MAX_INDEX                             1
#define QUAD_MAC_GMAC3_PTP_STATUS_MAX_INDEX                          1
#define QUAD_MAC_GMAC3_RX_CTRL_MAX_INDEX                             1
#define QUAD_MAC_GMAC3_SOFT_RST_MAX_INDEX                            1
#define QUAD_MAC_GMAC3_TX_CTRL_MAX_INDEX                             1
#define QUAD_MAC_INIT_MAX_INDEX                                      1
#define QUAD_MAC_INIT_DONE_MAX_INDEX                                 1
#define QUAD_MAC_STATS_CFG_MAX_INDEX                                 1
#define QUAD_MAC_STATUS_OVER_WRITE_MAX_INDEX                         1
#define QUAD_PCS_INTERRUPT_NORMAL_MAX_INDEX                          4
#define QUAD_PCS_PCS0_ANEG_CFG_MAX_INDEX                             1
#define QUAD_PCS_PCS0_ANEG_STATUS_MAX_INDEX                          1
#define QUAD_PCS_PCS0_CFG_MAX_INDEX                                  1
#define QUAD_PCS_PCS0_ERR_CNT_MAX_INDEX                              1
#define QUAD_PCS_PCS0_SOFT_RST_MAX_INDEX                             1
#define QUAD_PCS_PCS0_STATUS_MAX_INDEX                               1
#define QUAD_PCS_PCS1_ANEG_CFG_MAX_INDEX                             1
#define QUAD_PCS_PCS1_ANEG_STATUS_MAX_INDEX                          1
#define QUAD_PCS_PCS1_CFG_MAX_INDEX                                  1
#define QUAD_PCS_PCS1_ERR_CNT_MAX_INDEX                              1
#define QUAD_PCS_PCS1_SOFT_RST_MAX_INDEX                             1
#define QUAD_PCS_PCS1_STATUS_MAX_INDEX                               1
#define QUAD_PCS_PCS2_ANEG_CFG_MAX_INDEX                             1
#define QUAD_PCS_PCS2_ANEG_STATUS_MAX_INDEX                          1
#define QUAD_PCS_PCS2_CFG_MAX_INDEX                                  1
#define QUAD_PCS_PCS2_ERR_CNT_MAX_INDEX                              1
#define QUAD_PCS_PCS2_SOFT_RST_MAX_INDEX                             1
#define QUAD_PCS_PCS2_STATUS_MAX_INDEX                               1
#define QUAD_PCS_PCS3_ANEG_CFG_MAX_INDEX                             1
#define QUAD_PCS_PCS3_ANEG_STATUS_MAX_INDEX                          1
#define QUAD_PCS_PCS3_CFG_MAX_INDEX                                  1
#define QUAD_PCS_PCS3_ERR_CNT_MAX_INDEX                              1
#define QUAD_PCS_PCS3_SOFT_RST_MAX_INDEX                             1
#define QUAD_PCS_PCS3_STATUS_MAX_INDEX                               1
#define SGMAC_INTERRUPT_NORMAL_MAX_INDEX                             4
#define SGMAC_STATS_RAM_MAX_INDEX                                    40
#define SGMAC_CFG_MAX_INDEX                                          1
#define SGMAC_CTL_MAX_INDEX                                          1
#define SGMAC_DEBUG_STATUS_MAX_INDEX                                 1
#define SGMAC_PAUSE_CFG_MAX_INDEX                                    1
#define SGMAC_PCS_ANEG_CFG_MAX_INDEX                                 1
#define SGMAC_PCS_ANEG_STATUS_MAX_INDEX                              1
#define SGMAC_PCS_CFG_MAX_INDEX                                      1
#define SGMAC_PCS_ERR_CNT_MAX_INDEX                                  1
#define SGMAC_PCS_STATUS_MAX_INDEX                                   1
#define SGMAC_PTP_STATUS_MAX_INDEX                                   1
#define SGMAC_SOFT_RST_MAX_INDEX                                     1
#define SGMAC_STATS_CFG_MAX_INDEX                                    1
#define SGMAC_STATS_INIT_MAX_INDEX                                   1
#define SGMAC_STATS_INIT_DONE_MAX_INDEX                              1
#define SGMAC_XAUI_CFG_MAX_INDEX                                     1
#define SGMAC_XFI_DEBUG_STATS_MAX_INDEX                              1
#define SGMAC_XFI_TEST_MAX_INDEX                                     1
#define SGMAC_XFI_TEST_PAT_SEED_MAX_INDEX                            1
#define DS_STP_STATE_MAX_INDEX                                       512
#define DS_VLAN_MAX_INDEX                                            4096
#define DS_VLAN_PROFILE_MAX_INDEX                                    512
#define SHARED_DS_INTERRUPT_NORMAL_MAX_INDEX                         4
#define DS_STP_STATE__REG_RAM__RAM_CHK_REC_MAX_INDEX                 1
#define DS_VLAN_PROFILE__REG_RAM__RAM_CHK_REC_MAX_INDEX              1
#define DS_VLAN__REG_RAM__RAM_CHK_REC_MAX_INDEX                      1
#define SHARED_DS_ECC_CTL_MAX_INDEX                                  1
#define SHARED_DS_ECC_ERR_STATS_MAX_INDEX                            1
#define SHARED_DS_INIT_MAX_INDEX                                     1
#define SHARED_DS_INIT_DONE_MAX_INDEX                                1
#define TCAM_CTL_INT_CPU_REQUEST_MEM_MAX_INDEX                       64
#define TCAM_CTL_INT_CPU_RESULT_MEM_MAX_INDEX                        128
#define TCAM_CTL_INT_INTERRUPT_FATAL_MAX_INDEX                       4
#define TCAM_CTL_INT_INTERRUPT_NORMAL_MAX_INDEX                      4
#define TCAM_CTL_INT_BIST_CTL_MAX_INDEX                              1
#define TCAM_CTL_INT_BIST_POINTERS_MAX_INDEX                         1
#define TCAM_CTL_INT_CAPTURE_RESULT_MAX_INDEX                        1
#define TCAM_CTL_INT_CPU_ACCESS_CMD_MAX_INDEX                        1
#define TCAM_CTL_INT_CPU_RD_DATA_MAX_INDEX                           1
#define TCAM_CTL_INT_CPU_WR_DATA_MAX_INDEX                           1
#define TCAM_CTL_INT_CPU_WR_MASK_MAX_INDEX                           1
#define TCAM_CTL_INT_DEBUG_STATS_MAX_INDEX                           1
#define TCAM_CTL_INT_INIT_CTRL_MAX_INDEX                             1
#define TCAM_CTL_INT_KEY_SIZE_CFG_MAX_INDEX                          1
#define TCAM_CTL_INT_KEY_TYPE_CFG_MAX_INDEX                          1
#define TCAM_CTL_INT_MISC_CTRL_MAX_INDEX                             1
#define TCAM_CTL_INT_STATE_MAX_INDEX                                 1
#define TCAM_CTL_INT_WRAP_SETTING_MAX_INDEX                          1
#define TCAM_DS_INTERRUPT_FATAL_MAX_INDEX                            4
#define TCAM_DS_INTERRUPT_NORMAL_MAX_INDEX                           4
#define TCAM_DS_RAM4_W_BUS_MAX_INDEX                                 8192
#define TCAM_DS_RAM8_W_BUS_MAX_INDEX                                 4096
#define TCAM_DS_ARB_WEIGHT_MAX_INDEX                                 1
#define TCAM_DS_CREDIT_CTL_MAX_INDEX                                 1
#define TCAM_DS_DEBUG_STATS_MAX_INDEX                                1
#define TCAM_DS_MEM_ALLOCATION_FAIL_MAX_INDEX                        1
#define TCAM_DS_MISC_CTL_MAX_INDEX                                   1
#define TCAM_DS_RAM_INIT_CTL_MAX_INDEX                               1
#define TCAM_DS_RAM_PARITY_FAIL_MAX_INDEX                            1
#define TCAM_ENGINE_LOOKUP_RESULT_CTL0_MAX_INDEX                     1
#define TCAM_ENGINE_LOOKUP_RESULT_CTL1_MAX_INDEX                     1
#define TCAM_ENGINE_LOOKUP_RESULT_CTL2_MAX_INDEX                     1
#define TCAM_ENGINE_LOOKUP_RESULT_CTL3_MAX_INDEX                     1
#define DS_FIB_USER_ID160_KEY_MAX_INDEX                              0
#define DS_FIB_USER_ID80_KEY_MAX_INDEX                               0
#define DS_IPV4_HASH_KEY_MAX_INDEX                                   0
#define DS_IPV6_HASH_KEY_MAX_INDEX                                   0
#define DS_IPV6_MCAST_HASH_KEY_MAX_INDEX                             0
#define DS_LPM_IPV4_HASH16_KEY_MAX_INDEX                             0
#define DS_LPM_IPV4_HASH8_KEY_MAX_INDEX                              0
#define DS_LPM_IPV4_MCAST_HASH32_KEY_MAX_INDEX                       0
#define DS_LPM_IPV4_MCAST_HASH64_KEY_MAX_INDEX                       0
#define DS_LPM_IPV4_NAT_DA_PORT_HASH_KEY_MAX_INDEX                   0
#define DS_LPM_IPV4_NAT_SA_HASH_KEY_MAX_INDEX                        0
#define DS_LPM_IPV4_NAT_SA_PORT_HASH_KEY_MAX_INDEX                   0
#define DS_LPM_IPV6_HASH32_HIGH_KEY_MAX_INDEX                        0
#define DS_LPM_IPV6_HASH32_LOW_KEY_MAX_INDEX                         0
#define DS_LPM_IPV6_HASH32_MID_KEY_MAX_INDEX                         0
#define DS_LPM_IPV6_MCAST_HASH0_KEY_MAX_INDEX                        0
#define DS_LPM_IPV6_MCAST_HASH1_KEY_MAX_INDEX                        0
#define DS_LPM_IPV6_NAT_DA_PORT_HASH_KEY_MAX_INDEX                   0
#define DS_LPM_IPV6_NAT_SA_HASH_KEY_MAX_INDEX                        0
#define DS_LPM_IPV6_NAT_SA_PORT_HASH_KEY_MAX_INDEX                   0
#define DS_LPM_LOOKUP_KEY_MAX_INDEX                                  0
#define DS_LPM_LOOKUP_KEY0_MAX_INDEX                                 0
#define DS_LPM_LOOKUP_KEY1_MAX_INDEX                                 0
#define DS_LPM_LOOKUP_KEY2_MAX_INDEX                                 0
#define DS_LPM_LOOKUP_KEY3_MAX_INDEX                                 0
#define DS_LPM_TCAM160_KEY_MAX_INDEX                                 0
#define DS_LPM_TCAM80_KEY_MAX_INDEX                                  0
#define DS_LPM_TCAM_AD_MAX_INDEX                                     0
#define DS_MAC_FIB_KEY_MAX_INDEX                                     0
#define DS_MAC_IP_FIB_KEY_MAX_INDEX                                  0
#define DS_MAC_IPV6_FIB_KEY_MAX_INDEX                                0
#define FIB_LEARN_FIFO_DATA_MAX_INDEX                                0
#define DS_DEST_PTP_CHANNEL_MAX_INDEX                                64
#define DS_DLB_CHAN_IDLE_LEVEL_MAX_INDEX                             64
#define DS_OAM_LM_STATS_MAX_INDEX                                    0
#define DS_OAM_LM_STATS0_MAX_INDEX                                   128
#define DS_OAM_LM_STATS1_MAX_INDEX                                   128
#define DS_ACL_MAX_INDEX                                             0
#define DS_ACL_QOS_IPV4_HASH_KEY_MAX_INDEX                           0
#define DS_ACL_QOS_MAC_HASH_KEY_MAX_INDEX                            0
#define DS_BFD_MEP_MAX_INDEX                                         0
#define DS_BFD_RMEP_MAX_INDEX                                        0
#define DS_ETH_MEP_MAX_INDEX                                         0
#define DS_ETH_OAM_CHAN_MAX_INDEX                                    0
#define DS_ETH_RMEP_MAX_INDEX                                        0
#define DS_ETH_RMEP_CHAN_MAX_INDEX                                   0
#define DS_ETH_RMEP_CHAN_CONFLICT_TCAM_MAX_INDEX                     0
#define DS_FCOE_DA_MAX_INDEX                                         0
#define DS_FCOE_DA_TCAM_MAX_INDEX                                    0
#define DS_FCOE_HASH_KEY_MAX_INDEX                                   0
#define DS_FCOE_RPF_HASH_KEY_MAX_INDEX                               0
#define DS_FCOE_SA_MAX_INDEX                                         0
#define DS_FCOE_SA_TCAM_MAX_INDEX                                    0
#define DS_FWD_MAX_INDEX                                             0
#define DS_IP_DA_MAX_INDEX                                           0
#define DS_IP_SA_NAT_MAX_INDEX                                       0
#define DS_IPV4_ACL0_TCAM_MAX_INDEX                                  0
#define DS_IPV4_ACL1_TCAM_MAX_INDEX                                  0
#define DS_IPV4_ACL2_TCAM_MAX_INDEX                                  0
#define DS_IPV4_ACL3_TCAM_MAX_INDEX                                  0
#define DS_IPV4_MCAST_DA_TCAM_MAX_INDEX                              0
#define DS_IPV4_SA_NAT_TCAM_MAX_INDEX                                0
#define DS_IPV4_UCAST_DA_TCAM_MAX_INDEX                              0
#define DS_IPV4_UCAST_PBR_DUAL_DA_TCAM_MAX_INDEX                     0
#define DS_IPV6_ACL0_TCAM_MAX_INDEX                                  0
#define DS_IPV6_ACL1_TCAM_MAX_INDEX                                  0
#define DS_IPV6_MCAST_DA_TCAM_MAX_INDEX                              0
#define DS_IPV6_SA_NAT_TCAM_MAX_INDEX                                0
#define DS_IPV6_UCAST_DA_TCAM_MAX_INDEX                              0
#define DS_IPV6_UCAST_PBR_DUAL_DA_TCAM_MAX_INDEX                     0
#define DS_L2_EDIT_ETH4_W_MAX_INDEX                                  0
#define DS_L2_EDIT_ETH8_W_MAX_INDEX                                  0
#define DS_L2_EDIT_FLEX8_W_MAX_INDEX                                 0
#define DS_L2_EDIT_LOOPBACK_MAX_INDEX                                0
#define DS_L2_EDIT_PBB4_W_MAX_INDEX                                  0
#define DS_L2_EDIT_PBB8_W_MAX_INDEX                                  0
#define DS_L2_EDIT_SWAP_MAX_INDEX                                    0
#define DS_L3_EDIT_FLEX_MAX_INDEX                                    0
#define DS_L3_EDIT_MPLS4_W_MAX_INDEX                                 0
#define DS_L3_EDIT_MPLS8_W_MAX_INDEX                                 0
#define DS_L3_EDIT_NAT4_W_MAX_INDEX                                  0
#define DS_L3_EDIT_NAT8_W_MAX_INDEX                                  0
#define DS_L3_EDIT_TUNNEL_V4_MAX_INDEX                               0
#define DS_L3_EDIT_TUNNEL_V6_MAX_INDEX                               0
#define DS_MA_MAX_INDEX                                              0
#define DS_MA_NAME_MAX_INDEX                                         0
#define DS_MAC_MAX_INDEX                                             0
#define DS_MAC_ACL0_TCAM_MAX_INDEX                                   0
#define DS_MAC_ACL1_TCAM_MAX_INDEX                                   0
#define DS_MAC_ACL2_TCAM_MAX_INDEX                                   0
#define DS_MAC_ACL3_TCAM_MAX_INDEX                                   0
#define DS_MAC_HASH_KEY_MAX_INDEX                                    0
#define DS_MAC_IPV4_HASH32_KEY_MAX_INDEX                             0
#define DS_MAC_IPV4_HASH64_KEY_MAX_INDEX                             0
#define DS_MAC_IPV4_TCAM_MAX_INDEX                                   0
#define DS_MAC_IPV6_TCAM_MAX_INDEX                                   0
#define DS_MAC_TCAM_MAX_INDEX                                        0
#define DS_MET_ENTRY_MAX_INDEX                                       0
#define DS_MPLS_MAX_INDEX                                            0
#define DS_NEXT_HOP4_W_MAX_INDEX                                     0
#define DS_NEXT_HOP8_W_MAX_INDEX                                     0
#define DS_TRILL_DA_MAX_INDEX                                        0
#define DS_TRILL_DA_MCAST_TCAM_MAX_INDEX                             0
#define DS_TRILL_DA_UCAST_TCAM_MAX_INDEX                             0
#define DS_TRILL_MCAST_HASH_KEY_MAX_INDEX                            0
#define DS_TRILL_MCAST_VLAN_HASH_KEY_MAX_INDEX                       0
#define DS_TRILL_UCAST_HASH_KEY_MAX_INDEX                            0
#define DS_USER_ID_EGRESS_DEFAULT_MAX_INDEX                          0
#define DS_VLAN_XLATE_MAX_INDEX                                      0
#define DS_VLAN_XLATE_CONFLICT_TCAM_MAX_INDEX                        0
#define MS_DEQUEUE_MAX_INDEX                                         0
#define MS_ENQUEUE_MAX_INDEX                                         0
#define MS_EXCP_INFO_MAX_INDEX                                       0
#define MS_MET_FIFO_MAX_INDEX                                        0
#define MS_PACKET_HEADER_MAX_INDEX                                   1
#define MS_PACKET_RELEASE_MAX_INDEX                                  0
#define MS_RCD_UPDATE_MAX_INDEX                                      0
#define MS_STATS_UPDATE_MAX_INDEX                                    0
#define PACKET_HEADER_OUTER_MAX_INDEX                                1
#define DS_BFD_OAM_CHAN_TCAM_MAX_INDEX                               0
#define DS_BFD_OAM_HASH_KEY_MAX_INDEX                                0
#define DS_ETH_OAM_HASH_KEY_MAX_INDEX                                0
#define DS_ETH_OAM_RMEP_HASH_KEY_MAX_INDEX                           0
#define DS_ETH_OAM_TCAM_CHAN_MAX_INDEX                               0
#define DS_ETH_OAM_TCAM_CHAN_CONFLICT_TCAM_MAX_INDEX                 0
#define DS_MPLS_OAM_CHAN_TCAM_MAX_INDEX                              0
#define DS_MPLS_OAM_LABEL_HASH_KEY_MAX_INDEX                         0
#define DS_MPLS_PBT_BFD_OAM_CHAN_MAX_INDEX                           0
#define DS_PBT_OAM_CHAN_TCAM_MAX_INDEX                               0
#define DS_PBT_OAM_HASH_KEY_MAX_INDEX                                0
#define DS_TUNNEL_ID_MAX_INDEX                                       0
#define DS_TUNNEL_ID_CAPWAP_HASH_KEY_MAX_INDEX                       0
#define DS_TUNNEL_ID_CAPWAP_TCAM_MAX_INDEX                           0
#define DS_TUNNEL_ID_CONFLICT_TCAM_MAX_INDEX                         0
#define DS_TUNNEL_ID_DEFAULT_MAX_INDEX                               0
#define DS_TUNNEL_ID_IPV4_HASH_KEY_MAX_INDEX                         0
#define DS_TUNNEL_ID_IPV4_RPF_HASH_KEY_MAX_INDEX                     0
#define DS_TUNNEL_ID_IPV4_TCAM_MAX_INDEX                             0
#define DS_TUNNEL_ID_IPV6_TCAM_MAX_INDEX                             0
#define DS_TUNNEL_ID_PBB_HASH_KEY_MAX_INDEX                          0
#define DS_TUNNEL_ID_PBB_TCAM_MAX_INDEX                              0
#define DS_TUNNEL_ID_TRILL_MC_ADJ_CHECK_HASH_KEY_MAX_INDEX           0
#define DS_TUNNEL_ID_TRILL_MC_DECAP_HASH_KEY_MAX_INDEX               0
#define DS_TUNNEL_ID_TRILL_MC_RPF_HASH_KEY_MAX_INDEX                 0
#define DS_TUNNEL_ID_TRILL_TCAM_MAX_INDEX                            0
#define DS_TUNNEL_ID_TRILL_UC_DECAP_HASH_KEY_MAX_INDEX               0
#define DS_TUNNEL_ID_TRILL_UC_RPF_HASH_KEY_MAX_INDEX                 0
#define DS_USER_ID_MAX_INDEX                                         0
#define DS_USER_ID_CONFLICT_TCAM_MAX_INDEX                           0
#define DS_USER_ID_CVLAN_COS_HASH_KEY_MAX_INDEX                      0
#define DS_USER_ID_CVLAN_HASH_KEY_MAX_INDEX                          0
#define DS_USER_ID_DOUBLE_VLAN_HASH_KEY_MAX_INDEX                    0
#define DS_USER_ID_INGRESS_DEFAULT_MAX_INDEX                         0
#define DS_USER_ID_IPV4_HASH_KEY_MAX_INDEX                           0
#define DS_USER_ID_IPV4_PORT_HASH_KEY_MAX_INDEX                      0
#define DS_USER_ID_IPV4_TCAM_MAX_INDEX                               0
#define DS_USER_ID_IPV6_HASH_KEY_MAX_INDEX                           0
#define DS_USER_ID_IPV6_TCAM_MAX_INDEX                               0
#define DS_USER_ID_L2_HASH_KEY_MAX_INDEX                             0
#define DS_USER_ID_MAC_HASH_KEY_MAX_INDEX                            0
#define DS_USER_ID_MAC_PORT_HASH_KEY_MAX_INDEX                       0
#define DS_USER_ID_MAC_TCAM_MAX_INDEX                                0
#define DS_USER_ID_PORT_CROSS_HASH_KEY_MAX_INDEX                     0
#define DS_USER_ID_PORT_HASH_KEY_MAX_INDEX                           0
#define DS_USER_ID_PORT_VLAN_CROSS_HASH_KEY_MAX_INDEX                0
#define DS_USER_ID_SVLAN_COS_HASH_KEY_MAX_INDEX                      0
#define DS_USER_ID_SVLAN_HASH_KEY_MAX_INDEX                          0
#define DS_USER_ID_VLAN_TCAM_MAX_INDEX                               0
#define DS_ACL_IPV4_KEY0_MAX_INDEX                                   0
#define DS_ACL_IPV4_KEY1_MAX_INDEX                                   0
#define DS_ACL_IPV4_KEY2_MAX_INDEX                                   0
#define DS_ACL_IPV4_KEY3_MAX_INDEX                                   0
#define DS_ACL_IPV6_KEY0_MAX_INDEX                                   0
#define DS_ACL_IPV6_KEY1_MAX_INDEX                                   0
#define DS_ACL_MAC_KEY0_MAX_INDEX                                    0
#define DS_ACL_MAC_KEY1_MAX_INDEX                                    0
#define DS_ACL_MAC_KEY2_MAX_INDEX                                    0
#define DS_ACL_MAC_KEY3_MAX_INDEX                                    0
#define DS_ACL_MPLS_KEY0_MAX_INDEX                                   0
#define DS_ACL_MPLS_KEY1_MAX_INDEX                                   0
#define DS_ACL_MPLS_KEY2_MAX_INDEX                                   0
#define DS_ACL_MPLS_KEY3_MAX_INDEX                                   0
#define DS_ACL_QOS_IPV4_KEY_MAX_INDEX                                0
#define DS_ACL_QOS_IPV6_KEY_MAX_INDEX                                0
#define DS_ACL_QOS_MAC_KEY_MAX_INDEX                                 0
#define DS_ACL_QOS_MPLS_KEY_MAX_INDEX                                0
#define DS_FCOE_ROUTE_KEY_MAX_INDEX                                  0
#define DS_FCOE_RPF_KEY_MAX_INDEX                                    0
#define DS_IPV4_MCAST_ROUTE_KEY_MAX_INDEX                            0
#define DS_IPV4_NAT_KEY_MAX_INDEX                                    0
#define DS_IPV4_PBR_KEY_MAX_INDEX                                    0
#define DS_IPV4_ROUTE_KEY_MAX_INDEX                                  0
#define DS_IPV4_RPF_KEY_MAX_INDEX                                    0
#define DS_IPV4_UCAST_ROUTE_KEY_MAX_INDEX                            0
#define DS_IPV6_MCAST_ROUTE_KEY_MAX_INDEX                            0
#define DS_IPV6_NAT_KEY_MAX_INDEX                                    0
#define DS_IPV6_PBR_KEY_MAX_INDEX                                    0
#define DS_IPV6_ROUTE_KEY_MAX_INDEX                                  0
#define DS_IPV6_RPF_KEY_MAX_INDEX                                    0
#define DS_IPV6_UCAST_ROUTE_KEY_MAX_INDEX                            0
#define DS_MAC_BRIDGE_KEY_MAX_INDEX                                  0
#define DS_MAC_IPV4_KEY_MAX_INDEX                                    0
#define DS_MAC_IPV6_KEY_MAX_INDEX                                    0
#define DS_MAC_LEARNING_KEY_MAX_INDEX                                0
#define DS_TRILL_MCAST_ROUTE_KEY_MAX_INDEX                           0
#define DS_TRILL_ROUTE_KEY_MAX_INDEX                                 0
#define DS_TRILL_UCAST_ROUTE_KEY_MAX_INDEX                           0
#define DS_TUNNEL_ID_CAPWAP_KEY_MAX_INDEX                            0
#define DS_TUNNEL_ID_IPV4_KEY_MAX_INDEX                              0
#define DS_TUNNEL_ID_IPV6_KEY_MAX_INDEX                              0
#define DS_TUNNEL_ID_PBB_KEY_MAX_INDEX                               0
#define DS_TUNNEL_ID_TRILL_KEY_MAX_INDEX                             0
#define DS_USER_ID_IPV4_KEY_MAX_INDEX                                0
#define DS_USER_ID_IPV6_KEY_MAX_INDEX                                0
#define DS_USER_ID_MAC_KEY_MAX_INDEX                                 0
#define DS_USER_ID_VLAN_KEY_MAX_INDEX                                0

#define GB_SUP_INTERRUPT_FATAL_OFFSET                                (C_MODEL_BASE + 0x00000040)
#define GB_SUP_INTERRUPT_FUNCTION_OFFSET                             (C_MODEL_BASE + 0x00000060)
#define GB_SUP_INTERRUPT_NORMAL_OFFSET                               (C_MODEL_BASE + 0x00000080)
#define TRACE_HDR_MEM_OFFSET                                         (C_MODEL_BASE + 0x00001000)
#define AUX_CLK_SEL_CFG_OFFSET                                       (C_MODEL_BASE + 0x00000268)
#define CLK_DIV_AUX_CFG_OFFSET                                       (C_MODEL_BASE + 0x00000260)
#define CLK_DIV_CORE0_CFG_OFFSET                                     (C_MODEL_BASE + 0x00000244)
#define CLK_DIV_CORE1_CFG_OFFSET                                     (C_MODEL_BASE + 0x0000024c)
#define CLK_DIV_CORE2_CFG_OFFSET                                     (C_MODEL_BASE + 0x00000254)
#define CLK_DIV_CORE3_CFG_OFFSET                                     (C_MODEL_BASE + 0x0000025c)
#define CLK_DIV_INTF0_CFG_OFFSET                                     (C_MODEL_BASE + 0x00000240)
#define CLK_DIV_INTF1_CFG_OFFSET                                     (C_MODEL_BASE + 0x00000248)
#define CLK_DIV_INTF2_CFG_OFFSET                                     (C_MODEL_BASE + 0x00000250)
#define CLK_DIV_INTF3_CFG_OFFSET                                     (C_MODEL_BASE + 0x00000258)
#define CLK_DIV_RST_CTL_OFFSET                                       (C_MODEL_BASE + 0x00000264)
#define CORE_PLL_CFG_OFFSET                                          (C_MODEL_BASE + 0x00000380)
#define DEVICE_ID_OFFSET                                             (C_MODEL_BASE + 0x00000000)
#define GB_SENSOR_CTL_OFFSET                                         (C_MODEL_BASE + 0x00000018)
#define GLOBAL_GATED_CLK_CTL_OFFSET                                  (C_MODEL_BASE + 0x000000d0)
#define HSS0_ADAPT_EQ_CFG_OFFSET                                     (C_MODEL_BASE + 0x00000300)
#define HSS0_SPI_STATUS_OFFSET                                       (C_MODEL_BASE + 0x00000400)
#define HSS12_G0_CFG_OFFSET                                          (C_MODEL_BASE + 0x00000100)
#define HSS12_G0_PRBS_TEST_OFFSET                                    (C_MODEL_BASE + 0x00000108)
#define HSS12_G1_CFG_OFFSET                                          (C_MODEL_BASE + 0x00000110)
#define HSS12_G1_PRBS_TEST_OFFSET                                    (C_MODEL_BASE + 0x00000118)
#define HSS12_G2_CFG_OFFSET                                          (C_MODEL_BASE + 0x00000120)
#define HSS12_G2_PRBS_TEST_OFFSET                                    (C_MODEL_BASE + 0x00000128)
#define HSS12_G3_CFG_OFFSET                                          (C_MODEL_BASE + 0x00000130)
#define HSS12_G3_PRBS_TEST_OFFSET                                    (C_MODEL_BASE + 0x00000138)
#define HSS1_ADAPT_EQ_CFG_OFFSET                                     (C_MODEL_BASE + 0x00000320)
#define HSS1_SPI_STATUS_OFFSET                                       (C_MODEL_BASE + 0x00000410)
#define HSS2_ADAPT_EQ_CFG_OFFSET                                     (C_MODEL_BASE + 0x00000340)
#define HSS2_SPI_STATUS_OFFSET                                       (C_MODEL_BASE + 0x00000420)
#define HSS3_ADAPT_EQ_CFG_OFFSET                                     (C_MODEL_BASE + 0x00000360)
#define HSS3_SPI_STATUS_OFFSET                                       (C_MODEL_BASE + 0x00000430)
#define HSS6_G_LANE_A_CTL_OFFSET                                     (C_MODEL_BASE + 0x00000190)
#define HSS_ACCESS_CTL_OFFSET                                        (C_MODEL_BASE + 0x000000e4)
#define HSS_ACESS_PARAMETER_OFFSET                                   (C_MODEL_BASE + 0x000000e0)
#define HSS_BIT_ORDER_CFG_OFFSET                                     (C_MODEL_BASE + 0x000001c8)
#define HSS_MODE_CFG_OFFSET                                          (C_MODEL_BASE + 0x000001d0)
#define HSS_PLL_RESET_CFG_OFFSET                                     (C_MODEL_BASE + 0x00000008)
#define HSS_READ_DATA_OFFSET                                         (C_MODEL_BASE + 0x000000ec)
#define HSS_TX_GEAR_BOX_RST_CTL_OFFSET                               (C_MODEL_BASE + 0x00000140)
#define HSS_WRITE_DATA_OFFSET                                        (C_MODEL_BASE + 0x000000e8)
#define INT_LK_INTF_CFG_OFFSET                                       (C_MODEL_BASE + 0x00000188)
#define INTERRUPT_ENABLE_OFFSET                                      (C_MODEL_BASE + 0x00000004)
#define INTERRUPT_MAP_CTL_OFFSET                                     (C_MODEL_BASE + 0x00000160)
#define LED_CLOCK_DIV_OFFSET                                         (C_MODEL_BASE + 0x00000204)
#define LINK_TIMER_CTL_OFFSET                                        (C_MODEL_BASE + 0x000000f0)
#define MDIO_CLOCK_CFG_OFFSET                                        (C_MODEL_BASE + 0x00000200)
#define MODULE_GATED_CLK_CTL_OFFSET                                  (C_MODEL_BASE + 0x000000c0)
#define O_O_B_F_C_CLOCK_DIV_OFFSET                                   (C_MODEL_BASE + 0x00000208)
#define PCIE_BAR0_CFG_OFFSET                                         (C_MODEL_BASE + 0x00000800)
#define PCIE_BAR1_CFG_OFFSET                                         (C_MODEL_BASE + 0x00000804)
#define PCIE_CFG_IP_CFG_OFFSET                                       (C_MODEL_BASE + 0x00000818)
#define PCIE_DL_INT_ERROR_OFFSET                                     (C_MODEL_BASE + 0x00000830)
#define PCIE_ERROR_INJECT_CTL_OFFSET                                 (C_MODEL_BASE + 0x000002b8)
#define PCIE_ERROR_STATUS_OFFSET                                     (C_MODEL_BASE + 0x00000844)
#define PCIE_INBD_STATUS_OFFSET                                      (C_MODEL_BASE + 0x00000848)
#define PCIE_INTF_CFG_OFFSET                                         (C_MODEL_BASE + 0x000002b0)
#define PCIE_PERF_MON_CTL_OFFSET                                     (C_MODEL_BASE + 0x0000085c)
#define PCIE_PERF_MON_STATUS_OFFSET                                  (C_MODEL_BASE + 0x00000860)
#define PCIE_PM_CFG_OFFSET                                           (C_MODEL_BASE + 0x00000850)
#define PCIE_PROTOCOL_CFG_OFFSET                                     (C_MODEL_BASE + 0x00000808)
#define PCIE_STATUS_REG_OFFSET                                       (C_MODEL_BASE + 0x00000840)
#define PCIE_TL_DLP_IP_CFG_OFFSET                                    (C_MODEL_BASE + 0x00000810)
#define PCIE_TRACE_CTL_OFFSET                                        (C_MODEL_BASE + 0x0000080c)
#define PCIE_UTL_INT_ERROR_OFFSET                                    (C_MODEL_BASE + 0x00000820)
#define PLL_LOCK_ERROR_CNT_OFFSET                                    (C_MODEL_BASE + 0x00000010)
#define PLL_LOCK_OUT_OFFSET                                          (C_MODEL_BASE + 0x0000000c)
#define PTP_TOD_CLK_DIV_CFG_OFFSET                                   (C_MODEL_BASE + 0x00000210)
#define QSGMII_HSS_SEL_CFG_OFFSET                                    (C_MODEL_BASE + 0x000003c0)
#define RESET_INT_RELATED_OFFSET                                     (C_MODEL_BASE + 0x00000020)
#define SERDES_REF_CLK_DIV_CFG_OFFSET                                (C_MODEL_BASE + 0x00000214)
#define SGMAC_MODE_CFG_OFFSET                                        (C_MODEL_BASE + 0x000003c8)
#define SUP_DEBUG_CLK_RST_CTL_OFFSET                                 (C_MODEL_BASE + 0x00000144)
#define SUP_GPIO_CTL_OFFSET                                          (C_MODEL_BASE + 0x000001a0)
#define SUP_INTR_MASK_CFG_OFFSET                                     (C_MODEL_BASE + 0x00000180)
#define SYNC_E_CLK_DIV_CFG_OFFSET                                    (C_MODEL_BASE + 0x00000220)
#define SYNC_ETHERNET_CLK_CFG_OFFSET                                 (C_MODEL_BASE + 0x00000280)
#define SYNC_ETHERNET_MON_OFFSET                                     (C_MODEL_BASE + 0x000002a0)
#define SYNC_ETHERNET_SELECT_OFFSET                                  (C_MODEL_BASE + 0x00000290)
#define TANK_PLL_CFG_OFFSET                                          (C_MODEL_BASE + 0x000000f8)
#define TIME_OUT_INFO_OFFSET                                         (C_MODEL_BASE + 0x000000d8)
#define DESC_RX_MEM_BASE_OFFSET                                      (C_MODEL_BASE + 0x00004400)
#define DESC_RX_MEM_DEPTH_OFFSET                                     (C_MODEL_BASE + 0x00004420)
#define DESC_RX_VLD_NUM_OFFSET                                       (C_MODEL_BASE + 0x00004440)
#define DESC_TX_MEM_BASE_OFFSET                                      (C_MODEL_BASE + 0x00004460)
#define DESC_TX_MEM_DEPTH_OFFSET                                     (C_MODEL_BASE + 0x00004480)
#define DESC_TX_VLD_NUM_OFFSET                                       (C_MODEL_BASE + 0x000044a0)
#define DESC_WRBK_FIFO_OFFSET                                        (C_MODEL_BASE + 0x00004680)
#define DMA_CHAN_MAP_OFFSET                                          (C_MODEL_BASE + 0x00004500)
#define DMA_RX_PTR_TAB_OFFSET                                        (C_MODEL_BASE + 0x000044c0)
#define DMA_TX_PTR_TAB_OFFSET                                        (C_MODEL_BASE + 0x000044e0)
#define GBIF_REG_MEM_OFFSET                                          (C_MODEL_BASE + 0x00004700)
#define LEARN_INFO_FIFO_OFFSET                                       (C_MODEL_BASE + 0x00004300)
#define PCI_EXP_CORE_INTERRUPT_NORMAL_OFFSET                         (C_MODEL_BASE + 0x00004000)
#define PCI_EXP_DESC_MEM_OFFSET                                      (C_MODEL_BASE + 0x00004800)
#define PCIE_FUNC_INTR_OFFSET                                        (C_MODEL_BASE + 0x00004020)
#define PKT_TX_INFO_FIFO_OFFSET                                      (C_MODEL_BASE + 0x000046c0)
#define REG_RD_INFO_FIFO_OFFSET                                      (C_MODEL_BASE + 0x000046f0)
#define SYS_MEM_BASE_TAB_OFFSET                                      (C_MODEL_BASE + 0x00004600)
#define TAB_WR_INFO_FIFO_OFFSET                                      (C_MODEL_BASE + 0x000046e0)
#define DESC_WRBK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET                 (C_MODEL_BASE + 0x00004164)
#define DMA_CHAN_CFG_OFFSET                                          (C_MODEL_BASE + 0x00004050)
#define DMA_DEBUG_STATS_OFFSET                                       (C_MODEL_BASE + 0x00004180)
#define DMA_DESC_ERROR_REC_OFFSET                                    (C_MODEL_BASE + 0x00004030)
#define DMA_ECC_CTL_OFFSET                                           (C_MODEL_BASE + 0x00004054)
#define DMA_ENDIAN_CFG_OFFSET                                        (C_MODEL_BASE + 0x00004068)
#define DMA_FIFO_DEPTH_OFFSET                                        (C_MODEL_BASE + 0x000041c4)
#define DMA_FIFO_THRD_CFG_OFFSET                                     (C_MODEL_BASE + 0x0000406c)
#define DMA_GEN_INTR_CTL_OFFSET                                      (C_MODEL_BASE + 0x00004080)
#define DMA_INIT_CTL_OFFSET                                          (C_MODEL_BASE + 0x00004058)
#define DMA_LEARN_MEM_CTL_OFFSET                                     (C_MODEL_BASE + 0x000040c0)
#define DMA_LEARN_MISC_CTL_OFFSET                                    (C_MODEL_BASE + 0x000040b8)
#define DMA_LEARN_VALID_CTL_OFFSET                                   (C_MODEL_BASE + 0x000040b0)
#define DMA_MISC_CTL_OFFSET                                          (C_MODEL_BASE + 0x0000405c)
#define DMA_PKT_MISC_CTL_OFFSET                                      (C_MODEL_BASE + 0x00004074)
#define DMA_PKT_STATS_OFFSET                                         (C_MODEL_BASE + 0x000041a0)
#define DMA_PKT_TIMER_CTL_OFFSET                                     (C_MODEL_BASE + 0x00004078)
#define DMA_PKT_TX_DEBUG_STATS_OFFSET                                (C_MODEL_BASE + 0x000041c8)
#define DMA_RD_TIMER_CTL_OFFSET                                      (C_MODEL_BASE + 0x000040e0)
#define DMA_STATE_DEBUG_OFFSET                                       (C_MODEL_BASE + 0x000041c0)
#define DMA_STATS_BMP_CFG_OFFSET                                     (C_MODEL_BASE + 0x00004090)
#define DMA_STATS_ENTRY_CFG_OFFSET                                   (C_MODEL_BASE + 0x00004094)
#define DMA_STATS_INTERVAL_CFG_OFFSET                                (C_MODEL_BASE + 0x00004198)
#define DMA_STATS_OFFSET_CFG_OFFSET                                  (C_MODEL_BASE + 0x000040a0)
#define DMA_STATS_USER_CFG_OFFSET                                    (C_MODEL_BASE + 0x000040a8)
#define DMA_TAG_STATS_OFFSET                                         (C_MODEL_BASE + 0x00004140)
#define DMA_WR_TIMER_CTL_OFFSET                                      (C_MODEL_BASE + 0x000040e4)
#define PCI_EXP_DESC_MEM__REG_RAM__RAM_CHK_REC_OFFSET                (C_MODEL_BASE + 0x00004148)
#define PCIE_INTR_CFG_OFFSET                                         (C_MODEL_BASE + 0x00004044)
#define PCIE_OUTBOUND_STATS_OFFSET                                   (C_MODEL_BASE + 0x00004188)
#define PCIE_TAG_CTL_OFFSET                                          (C_MODEL_BASE + 0x00004040)
#define PKT_RX_DATA_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET               (C_MODEL_BASE + 0x00004158)
#define PKT_TX_DATA_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET               (C_MODEL_BASE + 0x00004154)
#define PKT_TX_INFO_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET               (C_MODEL_BASE + 0x00004150)
#define REG_RD_INFO_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET               (C_MODEL_BASE + 0x00004160)
#define TAB_WR_DATA_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET               (C_MODEL_BASE + 0x00004168)
#define TAB_WR_INFO_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET               (C_MODEL_BASE + 0x0000416c)
#define I2_C_MASTER_DATA_RAM_OFFSET                                  (C_MODEL_BASE + 0x00008400)
#define I2_C_MASTER_INTERRUPT_NORMAL_OFFSET                          (C_MODEL_BASE + 0x00008000)
#define I2_C_MASTER_BMP_CFG_OFFSET                                   (C_MODEL_BASE + 0x00008010)
#define I2_C_MASTER_CFG_OFFSET                                       (C_MODEL_BASE + 0x00008060)
#define I2_C_MASTER_POLLING_CFG_OFFSET                               (C_MODEL_BASE + 0x00008020)
#define I2_C_MASTER_READ_CFG_OFFSET                                  (C_MODEL_BASE + 0x00008040)
#define I2_C_MASTER_READ_CTL_OFFSET                                  (C_MODEL_BASE + 0x00008050)
#define I2_C_MASTER_READ_STATUS_OFFSET                               (C_MODEL_BASE + 0x00008080)
#define I2_C_MASTER_STATUS_OFFSET                                    (C_MODEL_BASE + 0x00008030)
#define I2_C_MASTER_WR_CFG_OFFSET                                    (C_MODEL_BASE + 0x00008070)
#define QUAD_MAC_INTERRUPT_NORMAL_OFFSET0                            (C_MODEL_BASE + 0x00100010)
#define QUAD_MAC_STATS_RAM_OFFSET0                                   (C_MODEL_BASE + 0x00102000)
#define QUAD_MAC_DATA_ERR_CTL_OFFSET0                                (C_MODEL_BASE + 0x00100048)
#define QUAD_MAC_DEBUG_STATS_OFFSET0                                 (C_MODEL_BASE + 0x00100090)
#define QUAD_MAC_GMAC0_MODE_OFFSET0                                  (C_MODEL_BASE + 0x001000c0)
#define QUAD_MAC_GMAC0_PTP_CFG_OFFSET0                               (C_MODEL_BASE + 0x001000d0)
#define QUAD_MAC_GMAC0_PTP_STATUS_OFFSET0                            (C_MODEL_BASE + 0x001000d4)
#define QUAD_MAC_GMAC0_RX_CTRL_OFFSET0                               (C_MODEL_BASE + 0x001000c8)
#define QUAD_MAC_GMAC0_SOFT_RST_OFFSET0                              (C_MODEL_BASE + 0x001000d8)
#define QUAD_MAC_GMAC0_TX_CTRL_OFFSET0                               (C_MODEL_BASE + 0x001000c4)
#define QUAD_MAC_GMAC1_MODE_OFFSET0                                  (C_MODEL_BASE + 0x001000e0)
#define QUAD_MAC_GMAC1_PTP_CFG_OFFSET0                               (C_MODEL_BASE + 0x001000f0)
#define QUAD_MAC_GMAC1_PTP_STATUS_OFFSET0                            (C_MODEL_BASE + 0x001000f4)
#define QUAD_MAC_GMAC1_RX_CTRL_OFFSET0                               (C_MODEL_BASE + 0x001000e8)
#define QUAD_MAC_GMAC1_SOFT_RST_OFFSET0                              (C_MODEL_BASE + 0x001000f8)
#define QUAD_MAC_GMAC1_TX_CTRL_OFFSET0                               (C_MODEL_BASE + 0x001000e4)
#define QUAD_MAC_GMAC2_MODE_OFFSET0                                  (C_MODEL_BASE + 0x00100100)
#define QUAD_MAC_GMAC2_PTP_CFG_OFFSET0                               (C_MODEL_BASE + 0x00100110)
#define QUAD_MAC_GMAC2_PTP_STATUS_OFFSET0                            (C_MODEL_BASE + 0x00100114)
#define QUAD_MAC_GMAC2_RX_CTRL_OFFSET0                               (C_MODEL_BASE + 0x00100108)
#define QUAD_MAC_GMAC2_SOFT_RST_OFFSET0                              (C_MODEL_BASE + 0x00100118)
#define QUAD_MAC_GMAC2_TX_CTRL_OFFSET0                               (C_MODEL_BASE + 0x00100104)
#define QUAD_MAC_GMAC3_MODE_OFFSET0                                  (C_MODEL_BASE + 0x00100120)
#define QUAD_MAC_GMAC3_PTP_CFG_OFFSET0                               (C_MODEL_BASE + 0x00100130)
#define QUAD_MAC_GMAC3_PTP_STATUS_OFFSET0                            (C_MODEL_BASE + 0x00100134)
#define QUAD_MAC_GMAC3_RX_CTRL_OFFSET0                               (C_MODEL_BASE + 0x00100128)
#define QUAD_MAC_GMAC3_SOFT_RST_OFFSET0                              (C_MODEL_BASE + 0x00100138)
#define QUAD_MAC_GMAC3_TX_CTRL_OFFSET0                               (C_MODEL_BASE + 0x00100124)
#define QUAD_MAC_INIT_OFFSET0                                        (C_MODEL_BASE + 0x00100024)
#define QUAD_MAC_INIT_DONE_OFFSET0                                   (C_MODEL_BASE + 0x00100028)
#define QUAD_MAC_STATS_CFG_OFFSET0                                   (C_MODEL_BASE + 0x00100040)
#define QUAD_MAC_STATUS_OVER_WRITE_OFFSET0                           (C_MODEL_BASE + 0x00100030)
#define QUAD_MAC_INTERRUPT_NORMAL_OFFSET1                            (C_MODEL_BASE + 0x00110010)
#define QUAD_MAC_STATS_RAM_OFFSET1                                   (C_MODEL_BASE + 0x00112000)
#define QUAD_MAC_DATA_ERR_CTL_OFFSET1                                (C_MODEL_BASE + 0x00110048)
#define QUAD_MAC_DEBUG_STATS_OFFSET1                                 (C_MODEL_BASE + 0x00110090)
#define QUAD_MAC_GMAC0_MODE_OFFSET1                                  (C_MODEL_BASE + 0x001100c0)
#define QUAD_MAC_GMAC0_PTP_CFG_OFFSET1                               (C_MODEL_BASE + 0x001100d0)
#define QUAD_MAC_GMAC0_PTP_STATUS_OFFSET1                            (C_MODEL_BASE + 0x001100d4)
#define QUAD_MAC_GMAC0_RX_CTRL_OFFSET1                               (C_MODEL_BASE + 0x001100c8)
#define QUAD_MAC_GMAC0_SOFT_RST_OFFSET1                              (C_MODEL_BASE + 0x001100d8)
#define QUAD_MAC_GMAC0_TX_CTRL_OFFSET1                               (C_MODEL_BASE + 0x001100c4)
#define QUAD_MAC_GMAC1_MODE_OFFSET1                                  (C_MODEL_BASE + 0x001100e0)
#define QUAD_MAC_GMAC1_PTP_CFG_OFFSET1                               (C_MODEL_BASE + 0x001100f0)
#define QUAD_MAC_GMAC1_PTP_STATUS_OFFSET1                            (C_MODEL_BASE + 0x001100f4)
#define QUAD_MAC_GMAC1_RX_CTRL_OFFSET1                               (C_MODEL_BASE + 0x001100e8)
#define QUAD_MAC_GMAC1_SOFT_RST_OFFSET1                              (C_MODEL_BASE + 0x001100f8)
#define QUAD_MAC_GMAC1_TX_CTRL_OFFSET1                               (C_MODEL_BASE + 0x001100e4)
#define QUAD_MAC_GMAC2_MODE_OFFSET1                                  (C_MODEL_BASE + 0x00110100)
#define QUAD_MAC_GMAC2_PTP_CFG_OFFSET1                               (C_MODEL_BASE + 0x00110110)
#define QUAD_MAC_GMAC2_PTP_STATUS_OFFSET1                            (C_MODEL_BASE + 0x00110114)
#define QUAD_MAC_GMAC2_RX_CTRL_OFFSET1                               (C_MODEL_BASE + 0x00110108)
#define QUAD_MAC_GMAC2_SOFT_RST_OFFSET1                              (C_MODEL_BASE + 0x00110118)
#define QUAD_MAC_GMAC2_TX_CTRL_OFFSET1                               (C_MODEL_BASE + 0x00110104)
#define QUAD_MAC_GMAC3_MODE_OFFSET1                                  (C_MODEL_BASE + 0x00110120)
#define QUAD_MAC_GMAC3_PTP_CFG_OFFSET1                               (C_MODEL_BASE + 0x00110130)
#define QUAD_MAC_GMAC3_PTP_STATUS_OFFSET1                            (C_MODEL_BASE + 0x00110134)
#define QUAD_MAC_GMAC3_RX_CTRL_OFFSET1                               (C_MODEL_BASE + 0x00110128)
#define QUAD_MAC_GMAC3_SOFT_RST_OFFSET1                              (C_MODEL_BASE + 0x00110138)
#define QUAD_MAC_GMAC3_TX_CTRL_OFFSET1                               (C_MODEL_BASE + 0x00110124)
#define QUAD_MAC_INIT_OFFSET1                                        (C_MODEL_BASE + 0x00110024)
#define QUAD_MAC_INIT_DONE_OFFSET1                                   (C_MODEL_BASE + 0x00110028)
#define QUAD_MAC_STATS_CFG_OFFSET1                                   (C_MODEL_BASE + 0x00110040)
#define QUAD_MAC_STATUS_OVER_WRITE_OFFSET1                           (C_MODEL_BASE + 0x00110030)
#define QUAD_MAC_INTERRUPT_NORMAL_OFFSET2                            (C_MODEL_BASE + 0x00120010)
#define QUAD_MAC_STATS_RAM_OFFSET2                                   (C_MODEL_BASE + 0x00122000)
#define QUAD_MAC_DATA_ERR_CTL_OFFSET2                                (C_MODEL_BASE + 0x00120048)
#define QUAD_MAC_DEBUG_STATS_OFFSET2                                 (C_MODEL_BASE + 0x00120090)
#define QUAD_MAC_GMAC0_MODE_OFFSET2                                  (C_MODEL_BASE + 0x001200c0)
#define QUAD_MAC_GMAC0_PTP_CFG_OFFSET2                               (C_MODEL_BASE + 0x001200d0)
#define QUAD_MAC_GMAC0_PTP_STATUS_OFFSET2                            (C_MODEL_BASE + 0x001200d4)
#define QUAD_MAC_GMAC0_RX_CTRL_OFFSET2                               (C_MODEL_BASE + 0x001200c8)
#define QUAD_MAC_GMAC0_SOFT_RST_OFFSET2                              (C_MODEL_BASE + 0x001200d8)
#define QUAD_MAC_GMAC0_TX_CTRL_OFFSET2                               (C_MODEL_BASE + 0x001200c4)
#define QUAD_MAC_GMAC1_MODE_OFFSET2                                  (C_MODEL_BASE + 0x001200e0)
#define QUAD_MAC_GMAC1_PTP_CFG_OFFSET2                               (C_MODEL_BASE + 0x001200f0)
#define QUAD_MAC_GMAC1_PTP_STATUS_OFFSET2                            (C_MODEL_BASE + 0x001200f4)
#define QUAD_MAC_GMAC1_RX_CTRL_OFFSET2                               (C_MODEL_BASE + 0x001200e8)
#define QUAD_MAC_GMAC1_SOFT_RST_OFFSET2                              (C_MODEL_BASE + 0x001200f8)
#define QUAD_MAC_GMAC1_TX_CTRL_OFFSET2                               (C_MODEL_BASE + 0x001200e4)
#define QUAD_MAC_GMAC2_MODE_OFFSET2                                  (C_MODEL_BASE + 0x00120100)
#define QUAD_MAC_GMAC2_PTP_CFG_OFFSET2                               (C_MODEL_BASE + 0x00120110)
#define QUAD_MAC_GMAC2_PTP_STATUS_OFFSET2                            (C_MODEL_BASE + 0x00120114)
#define QUAD_MAC_GMAC2_RX_CTRL_OFFSET2                               (C_MODEL_BASE + 0x00120108)
#define QUAD_MAC_GMAC2_SOFT_RST_OFFSET2                              (C_MODEL_BASE + 0x00120118)
#define QUAD_MAC_GMAC2_TX_CTRL_OFFSET2                               (C_MODEL_BASE + 0x00120104)
#define QUAD_MAC_GMAC3_MODE_OFFSET2                                  (C_MODEL_BASE + 0x00120120)
#define QUAD_MAC_GMAC3_PTP_CFG_OFFSET2                               (C_MODEL_BASE + 0x00120130)
#define QUAD_MAC_GMAC3_PTP_STATUS_OFFSET2                            (C_MODEL_BASE + 0x00120134)
#define QUAD_MAC_GMAC3_RX_CTRL_OFFSET2                               (C_MODEL_BASE + 0x00120128)
#define QUAD_MAC_GMAC3_SOFT_RST_OFFSET2                              (C_MODEL_BASE + 0x00120138)
#define QUAD_MAC_GMAC3_TX_CTRL_OFFSET2                               (C_MODEL_BASE + 0x00120124)
#define QUAD_MAC_INIT_OFFSET2                                        (C_MODEL_BASE + 0x00120024)
#define QUAD_MAC_INIT_DONE_OFFSET2                                   (C_MODEL_BASE + 0x00120028)
#define QUAD_MAC_STATS_CFG_OFFSET2                                   (C_MODEL_BASE + 0x00120040)
#define QUAD_MAC_STATUS_OVER_WRITE_OFFSET2                           (C_MODEL_BASE + 0x00120030)
#define QUAD_MAC_INTERRUPT_NORMAL_OFFSET3                            (C_MODEL_BASE + 0x00130010)
#define QUAD_MAC_STATS_RAM_OFFSET3                                   (C_MODEL_BASE + 0x00132000)
#define QUAD_MAC_DATA_ERR_CTL_OFFSET3                                (C_MODEL_BASE + 0x00130048)
#define QUAD_MAC_DEBUG_STATS_OFFSET3                                 (C_MODEL_BASE + 0x00130090)
#define QUAD_MAC_GMAC0_MODE_OFFSET3                                  (C_MODEL_BASE + 0x001300c0)
#define QUAD_MAC_GMAC0_PTP_CFG_OFFSET3                               (C_MODEL_BASE + 0x001300d0)
#define QUAD_MAC_GMAC0_PTP_STATUS_OFFSET3                            (C_MODEL_BASE + 0x001300d4)
#define QUAD_MAC_GMAC0_RX_CTRL_OFFSET3                               (C_MODEL_BASE + 0x001300c8)
#define QUAD_MAC_GMAC0_SOFT_RST_OFFSET3                              (C_MODEL_BASE + 0x001300d8)
#define QUAD_MAC_GMAC0_TX_CTRL_OFFSET3                               (C_MODEL_BASE + 0x001300c4)
#define QUAD_MAC_GMAC1_MODE_OFFSET3                                  (C_MODEL_BASE + 0x001300e0)
#define QUAD_MAC_GMAC1_PTP_CFG_OFFSET3                               (C_MODEL_BASE + 0x001300f0)
#define QUAD_MAC_GMAC1_PTP_STATUS_OFFSET3                            (C_MODEL_BASE + 0x001300f4)
#define QUAD_MAC_GMAC1_RX_CTRL_OFFSET3                               (C_MODEL_BASE + 0x001300e8)
#define QUAD_MAC_GMAC1_SOFT_RST_OFFSET3                              (C_MODEL_BASE + 0x001300f8)
#define QUAD_MAC_GMAC1_TX_CTRL_OFFSET3                               (C_MODEL_BASE + 0x001300e4)
#define QUAD_MAC_GMAC2_MODE_OFFSET3                                  (C_MODEL_BASE + 0x00130100)
#define QUAD_MAC_GMAC2_PTP_CFG_OFFSET3                               (C_MODEL_BASE + 0x00130110)
#define QUAD_MAC_GMAC2_PTP_STATUS_OFFSET3                            (C_MODEL_BASE + 0x00130114)
#define QUAD_MAC_GMAC2_RX_CTRL_OFFSET3                               (C_MODEL_BASE + 0x00130108)
#define QUAD_MAC_GMAC2_SOFT_RST_OFFSET3                              (C_MODEL_BASE + 0x00130118)
#define QUAD_MAC_GMAC2_TX_CTRL_OFFSET3                               (C_MODEL_BASE + 0x00130104)
#define QUAD_MAC_GMAC3_MODE_OFFSET3                                  (C_MODEL_BASE + 0x00130120)
#define QUAD_MAC_GMAC3_PTP_CFG_OFFSET3                               (C_MODEL_BASE + 0x00130130)
#define QUAD_MAC_GMAC3_PTP_STATUS_OFFSET3                            (C_MODEL_BASE + 0x00130134)
#define QUAD_MAC_GMAC3_RX_CTRL_OFFSET3                               (C_MODEL_BASE + 0x00130128)
#define QUAD_MAC_GMAC3_SOFT_RST_OFFSET3                              (C_MODEL_BASE + 0x00130138)
#define QUAD_MAC_GMAC3_TX_CTRL_OFFSET3                               (C_MODEL_BASE + 0x00130124)
#define QUAD_MAC_INIT_OFFSET3                                        (C_MODEL_BASE + 0x00130024)
#define QUAD_MAC_INIT_DONE_OFFSET3                                   (C_MODEL_BASE + 0x00130028)
#define QUAD_MAC_STATS_CFG_OFFSET3                                   (C_MODEL_BASE + 0x00130040)
#define QUAD_MAC_STATUS_OVER_WRITE_OFFSET3                           (C_MODEL_BASE + 0x00130030)
#define QUAD_MAC_INTERRUPT_NORMAL_OFFSET4                            (C_MODEL_BASE + 0x00140010)
#define QUAD_MAC_STATS_RAM_OFFSET4                                   (C_MODEL_BASE + 0x00142000)
#define QUAD_MAC_DATA_ERR_CTL_OFFSET4                                (C_MODEL_BASE + 0x00140048)
#define QUAD_MAC_DEBUG_STATS_OFFSET4                                 (C_MODEL_BASE + 0x00140090)
#define QUAD_MAC_GMAC0_MODE_OFFSET4                                  (C_MODEL_BASE + 0x001400c0)
#define QUAD_MAC_GMAC0_PTP_CFG_OFFSET4                               (C_MODEL_BASE + 0x001400d0)
#define QUAD_MAC_GMAC0_PTP_STATUS_OFFSET4                            (C_MODEL_BASE + 0x001400d4)
#define QUAD_MAC_GMAC0_RX_CTRL_OFFSET4                               (C_MODEL_BASE + 0x001400c8)
#define QUAD_MAC_GMAC0_SOFT_RST_OFFSET4                              (C_MODEL_BASE + 0x001400d8)
#define QUAD_MAC_GMAC0_TX_CTRL_OFFSET4                               (C_MODEL_BASE + 0x001400c4)
#define QUAD_MAC_GMAC1_MODE_OFFSET4                                  (C_MODEL_BASE + 0x001400e0)
#define QUAD_MAC_GMAC1_PTP_CFG_OFFSET4                               (C_MODEL_BASE + 0x001400f0)
#define QUAD_MAC_GMAC1_PTP_STATUS_OFFSET4                            (C_MODEL_BASE + 0x001400f4)
#define QUAD_MAC_GMAC1_RX_CTRL_OFFSET4                               (C_MODEL_BASE + 0x001400e8)
#define QUAD_MAC_GMAC1_SOFT_RST_OFFSET4                              (C_MODEL_BASE + 0x001400f8)
#define QUAD_MAC_GMAC1_TX_CTRL_OFFSET4                               (C_MODEL_BASE + 0x001400e4)
#define QUAD_MAC_GMAC2_MODE_OFFSET4                                  (C_MODEL_BASE + 0x00140100)
#define QUAD_MAC_GMAC2_PTP_CFG_OFFSET4                               (C_MODEL_BASE + 0x00140110)
#define QUAD_MAC_GMAC2_PTP_STATUS_OFFSET4                            (C_MODEL_BASE + 0x00140114)
#define QUAD_MAC_GMAC2_RX_CTRL_OFFSET4                               (C_MODEL_BASE + 0x00140108)
#define QUAD_MAC_GMAC2_SOFT_RST_OFFSET4                              (C_MODEL_BASE + 0x00140118)
#define QUAD_MAC_GMAC2_TX_CTRL_OFFSET4                               (C_MODEL_BASE + 0x00140104)
#define QUAD_MAC_GMAC3_MODE_OFFSET4                                  (C_MODEL_BASE + 0x00140120)
#define QUAD_MAC_GMAC3_PTP_CFG_OFFSET4                               (C_MODEL_BASE + 0x00140130)
#define QUAD_MAC_GMAC3_PTP_STATUS_OFFSET4                            (C_MODEL_BASE + 0x00140134)
#define QUAD_MAC_GMAC3_RX_CTRL_OFFSET4                               (C_MODEL_BASE + 0x00140128)
#define QUAD_MAC_GMAC3_SOFT_RST_OFFSET4                              (C_MODEL_BASE + 0x00140138)
#define QUAD_MAC_GMAC3_TX_CTRL_OFFSET4                               (C_MODEL_BASE + 0x00140124)
#define QUAD_MAC_INIT_OFFSET4                                        (C_MODEL_BASE + 0x00140024)
#define QUAD_MAC_INIT_DONE_OFFSET4                                   (C_MODEL_BASE + 0x00140028)
#define QUAD_MAC_STATS_CFG_OFFSET4                                   (C_MODEL_BASE + 0x00140040)
#define QUAD_MAC_STATUS_OVER_WRITE_OFFSET4                           (C_MODEL_BASE + 0x00140030)
#define QUAD_MAC_INTERRUPT_NORMAL_OFFSET5                            (C_MODEL_BASE + 0x00150010)
#define QUAD_MAC_STATS_RAM_OFFSET5                                   (C_MODEL_BASE + 0x00152000)
#define QUAD_MAC_DATA_ERR_CTL_OFFSET5                                (C_MODEL_BASE + 0x00150048)
#define QUAD_MAC_DEBUG_STATS_OFFSET5                                 (C_MODEL_BASE + 0x00150090)
#define QUAD_MAC_GMAC0_MODE_OFFSET5                                  (C_MODEL_BASE + 0x001500c0)
#define QUAD_MAC_GMAC0_PTP_CFG_OFFSET5                               (C_MODEL_BASE + 0x001500d0)
#define QUAD_MAC_GMAC0_PTP_STATUS_OFFSET5                            (C_MODEL_BASE + 0x001500d4)
#define QUAD_MAC_GMAC0_RX_CTRL_OFFSET5                               (C_MODEL_BASE + 0x001500c8)
#define QUAD_MAC_GMAC0_SOFT_RST_OFFSET5                              (C_MODEL_BASE + 0x001500d8)
#define QUAD_MAC_GMAC0_TX_CTRL_OFFSET5                               (C_MODEL_BASE + 0x001500c4)
#define QUAD_MAC_GMAC1_MODE_OFFSET5                                  (C_MODEL_BASE + 0x001500e0)
#define QUAD_MAC_GMAC1_PTP_CFG_OFFSET5                               (C_MODEL_BASE + 0x001500f0)
#define QUAD_MAC_GMAC1_PTP_STATUS_OFFSET5                            (C_MODEL_BASE + 0x001500f4)
#define QUAD_MAC_GMAC1_RX_CTRL_OFFSET5                               (C_MODEL_BASE + 0x001500e8)
#define QUAD_MAC_GMAC1_SOFT_RST_OFFSET5                              (C_MODEL_BASE + 0x001500f8)
#define QUAD_MAC_GMAC1_TX_CTRL_OFFSET5                               (C_MODEL_BASE + 0x001500e4)
#define QUAD_MAC_GMAC2_MODE_OFFSET5                                  (C_MODEL_BASE + 0x00150100)
#define QUAD_MAC_GMAC2_PTP_CFG_OFFSET5                               (C_MODEL_BASE + 0x00150110)
#define QUAD_MAC_GMAC2_PTP_STATUS_OFFSET5                            (C_MODEL_BASE + 0x00150114)
#define QUAD_MAC_GMAC2_RX_CTRL_OFFSET5                               (C_MODEL_BASE + 0x00150108)
#define QUAD_MAC_GMAC2_SOFT_RST_OFFSET5                              (C_MODEL_BASE + 0x00150118)
#define QUAD_MAC_GMAC2_TX_CTRL_OFFSET5                               (C_MODEL_BASE + 0x00150104)
#define QUAD_MAC_GMAC3_MODE_OFFSET5                                  (C_MODEL_BASE + 0x00150120)
#define QUAD_MAC_GMAC3_PTP_CFG_OFFSET5                               (C_MODEL_BASE + 0x00150130)
#define QUAD_MAC_GMAC3_PTP_STATUS_OFFSET5                            (C_MODEL_BASE + 0x00150134)
#define QUAD_MAC_GMAC3_RX_CTRL_OFFSET5                               (C_MODEL_BASE + 0x00150128)
#define QUAD_MAC_GMAC3_SOFT_RST_OFFSET5                              (C_MODEL_BASE + 0x00150138)
#define QUAD_MAC_GMAC3_TX_CTRL_OFFSET5                               (C_MODEL_BASE + 0x00150124)
#define QUAD_MAC_INIT_OFFSET5                                        (C_MODEL_BASE + 0x00150024)
#define QUAD_MAC_INIT_DONE_OFFSET5                                   (C_MODEL_BASE + 0x00150028)
#define QUAD_MAC_STATS_CFG_OFFSET5                                   (C_MODEL_BASE + 0x00150040)
#define QUAD_MAC_STATUS_OVER_WRITE_OFFSET5                           (C_MODEL_BASE + 0x00150030)
#define QUAD_MAC_INTERRUPT_NORMAL_OFFSET6                            (C_MODEL_BASE + 0x00160010)
#define QUAD_MAC_STATS_RAM_OFFSET6                                   (C_MODEL_BASE + 0x00162000)
#define QUAD_MAC_DATA_ERR_CTL_OFFSET6                                (C_MODEL_BASE + 0x00160048)
#define QUAD_MAC_DEBUG_STATS_OFFSET6                                 (C_MODEL_BASE + 0x00160090)
#define QUAD_MAC_GMAC0_MODE_OFFSET6                                  (C_MODEL_BASE + 0x001600c0)
#define QUAD_MAC_GMAC0_PTP_CFG_OFFSET6                               (C_MODEL_BASE + 0x001600d0)
#define QUAD_MAC_GMAC0_PTP_STATUS_OFFSET6                            (C_MODEL_BASE + 0x001600d4)
#define QUAD_MAC_GMAC0_RX_CTRL_OFFSET6                               (C_MODEL_BASE + 0x001600c8)
#define QUAD_MAC_GMAC0_SOFT_RST_OFFSET6                              (C_MODEL_BASE + 0x001600d8)
#define QUAD_MAC_GMAC0_TX_CTRL_OFFSET6                               (C_MODEL_BASE + 0x001600c4)
#define QUAD_MAC_GMAC1_MODE_OFFSET6                                  (C_MODEL_BASE + 0x001600e0)
#define QUAD_MAC_GMAC1_PTP_CFG_OFFSET6                               (C_MODEL_BASE + 0x001600f0)
#define QUAD_MAC_GMAC1_PTP_STATUS_OFFSET6                            (C_MODEL_BASE + 0x001600f4)
#define QUAD_MAC_GMAC1_RX_CTRL_OFFSET6                               (C_MODEL_BASE + 0x001600e8)
#define QUAD_MAC_GMAC1_SOFT_RST_OFFSET6                              (C_MODEL_BASE + 0x001600f8)
#define QUAD_MAC_GMAC1_TX_CTRL_OFFSET6                               (C_MODEL_BASE + 0x001600e4)
#define QUAD_MAC_GMAC2_MODE_OFFSET6                                  (C_MODEL_BASE + 0x00160100)
#define QUAD_MAC_GMAC2_PTP_CFG_OFFSET6                               (C_MODEL_BASE + 0x00160110)
#define QUAD_MAC_GMAC2_PTP_STATUS_OFFSET6                            (C_MODEL_BASE + 0x00160114)
#define QUAD_MAC_GMAC2_RX_CTRL_OFFSET6                               (C_MODEL_BASE + 0x00160108)
#define QUAD_MAC_GMAC2_SOFT_RST_OFFSET6                              (C_MODEL_BASE + 0x00160118)
#define QUAD_MAC_GMAC2_TX_CTRL_OFFSET6                               (C_MODEL_BASE + 0x00160104)
#define QUAD_MAC_GMAC3_MODE_OFFSET6                                  (C_MODEL_BASE + 0x00160120)
#define QUAD_MAC_GMAC3_PTP_CFG_OFFSET6                               (C_MODEL_BASE + 0x00160130)
#define QUAD_MAC_GMAC3_PTP_STATUS_OFFSET6                            (C_MODEL_BASE + 0x00160134)
#define QUAD_MAC_GMAC3_RX_CTRL_OFFSET6                               (C_MODEL_BASE + 0x00160128)
#define QUAD_MAC_GMAC3_SOFT_RST_OFFSET6                              (C_MODEL_BASE + 0x00160138)
#define QUAD_MAC_GMAC3_TX_CTRL_OFFSET6                               (C_MODEL_BASE + 0x00160124)
#define QUAD_MAC_INIT_OFFSET6                                        (C_MODEL_BASE + 0x00160024)
#define QUAD_MAC_INIT_DONE_OFFSET6                                   (C_MODEL_BASE + 0x00160028)
#define QUAD_MAC_STATS_CFG_OFFSET6                                   (C_MODEL_BASE + 0x00160040)
#define QUAD_MAC_STATUS_OVER_WRITE_OFFSET6                           (C_MODEL_BASE + 0x00160030)
#define QUAD_MAC_INTERRUPT_NORMAL_OFFSET7                            (C_MODEL_BASE + 0x00170010)
#define QUAD_MAC_STATS_RAM_OFFSET7                                   (C_MODEL_BASE + 0x00172000)
#define QUAD_MAC_DATA_ERR_CTL_OFFSET7                                (C_MODEL_BASE + 0x00170048)
#define QUAD_MAC_DEBUG_STATS_OFFSET7                                 (C_MODEL_BASE + 0x00170090)
#define QUAD_MAC_GMAC0_MODE_OFFSET7                                  (C_MODEL_BASE + 0x001700c0)
#define QUAD_MAC_GMAC0_PTP_CFG_OFFSET7                               (C_MODEL_BASE + 0x001700d0)
#define QUAD_MAC_GMAC0_PTP_STATUS_OFFSET7                            (C_MODEL_BASE + 0x001700d4)
#define QUAD_MAC_GMAC0_RX_CTRL_OFFSET7                               (C_MODEL_BASE + 0x001700c8)
#define QUAD_MAC_GMAC0_SOFT_RST_OFFSET7                              (C_MODEL_BASE + 0x001700d8)
#define QUAD_MAC_GMAC0_TX_CTRL_OFFSET7                               (C_MODEL_BASE + 0x001700c4)
#define QUAD_MAC_GMAC1_MODE_OFFSET7                                  (C_MODEL_BASE + 0x001700e0)
#define QUAD_MAC_GMAC1_PTP_CFG_OFFSET7                               (C_MODEL_BASE + 0x001700f0)
#define QUAD_MAC_GMAC1_PTP_STATUS_OFFSET7                            (C_MODEL_BASE + 0x001700f4)
#define QUAD_MAC_GMAC1_RX_CTRL_OFFSET7                               (C_MODEL_BASE + 0x001700e8)
#define QUAD_MAC_GMAC1_SOFT_RST_OFFSET7                              (C_MODEL_BASE + 0x001700f8)
#define QUAD_MAC_GMAC1_TX_CTRL_OFFSET7                               (C_MODEL_BASE + 0x001700e4)
#define QUAD_MAC_GMAC2_MODE_OFFSET7                                  (C_MODEL_BASE + 0x00170100)
#define QUAD_MAC_GMAC2_PTP_CFG_OFFSET7                               (C_MODEL_BASE + 0x00170110)
#define QUAD_MAC_GMAC2_PTP_STATUS_OFFSET7                            (C_MODEL_BASE + 0x00170114)
#define QUAD_MAC_GMAC2_RX_CTRL_OFFSET7                               (C_MODEL_BASE + 0x00170108)
#define QUAD_MAC_GMAC2_SOFT_RST_OFFSET7                              (C_MODEL_BASE + 0x00170118)
#define QUAD_MAC_GMAC2_TX_CTRL_OFFSET7                               (C_MODEL_BASE + 0x00170104)
#define QUAD_MAC_GMAC3_MODE_OFFSET7                                  (C_MODEL_BASE + 0x00170120)
#define QUAD_MAC_GMAC3_PTP_CFG_OFFSET7                               (C_MODEL_BASE + 0x00170130)
#define QUAD_MAC_GMAC3_PTP_STATUS_OFFSET7                            (C_MODEL_BASE + 0x00170134)
#define QUAD_MAC_GMAC3_RX_CTRL_OFFSET7                               (C_MODEL_BASE + 0x00170128)
#define QUAD_MAC_GMAC3_SOFT_RST_OFFSET7                              (C_MODEL_BASE + 0x00170138)
#define QUAD_MAC_GMAC3_TX_CTRL_OFFSET7                               (C_MODEL_BASE + 0x00170124)
#define QUAD_MAC_INIT_OFFSET7                                        (C_MODEL_BASE + 0x00170024)
#define QUAD_MAC_INIT_DONE_OFFSET7                                   (C_MODEL_BASE + 0x00170028)
#define QUAD_MAC_STATS_CFG_OFFSET7                                   (C_MODEL_BASE + 0x00170040)
#define QUAD_MAC_STATUS_OVER_WRITE_OFFSET7                           (C_MODEL_BASE + 0x00170030)
#define QUAD_MAC_INTERRUPT_NORMAL_OFFSET8                            (C_MODEL_BASE + 0x00180010)
#define QUAD_MAC_STATS_RAM_OFFSET8                                   (C_MODEL_BASE + 0x00182000)
#define QUAD_MAC_DATA_ERR_CTL_OFFSET8                                (C_MODEL_BASE + 0x00180048)
#define QUAD_MAC_DEBUG_STATS_OFFSET8                                 (C_MODEL_BASE + 0x00180090)
#define QUAD_MAC_GMAC0_MODE_OFFSET8                                  (C_MODEL_BASE + 0x001800c0)
#define QUAD_MAC_GMAC0_PTP_CFG_OFFSET8                               (C_MODEL_BASE + 0x001800d0)
#define QUAD_MAC_GMAC0_PTP_STATUS_OFFSET8                            (C_MODEL_BASE + 0x001800d4)
#define QUAD_MAC_GMAC0_RX_CTRL_OFFSET8                               (C_MODEL_BASE + 0x001800c8)
#define QUAD_MAC_GMAC0_SOFT_RST_OFFSET8                              (C_MODEL_BASE + 0x001800d8)
#define QUAD_MAC_GMAC0_TX_CTRL_OFFSET8                               (C_MODEL_BASE + 0x001800c4)
#define QUAD_MAC_GMAC1_MODE_OFFSET8                                  (C_MODEL_BASE + 0x001800e0)
#define QUAD_MAC_GMAC1_PTP_CFG_OFFSET8                               (C_MODEL_BASE + 0x001800f0)
#define QUAD_MAC_GMAC1_PTP_STATUS_OFFSET8                            (C_MODEL_BASE + 0x001800f4)
#define QUAD_MAC_GMAC1_RX_CTRL_OFFSET8                               (C_MODEL_BASE + 0x001800e8)
#define QUAD_MAC_GMAC1_SOFT_RST_OFFSET8                              (C_MODEL_BASE + 0x001800f8)
#define QUAD_MAC_GMAC1_TX_CTRL_OFFSET8                               (C_MODEL_BASE + 0x001800e4)
#define QUAD_MAC_GMAC2_MODE_OFFSET8                                  (C_MODEL_BASE + 0x00180100)
#define QUAD_MAC_GMAC2_PTP_CFG_OFFSET8                               (C_MODEL_BASE + 0x00180110)
#define QUAD_MAC_GMAC2_PTP_STATUS_OFFSET8                            (C_MODEL_BASE + 0x00180114)
#define QUAD_MAC_GMAC2_RX_CTRL_OFFSET8                               (C_MODEL_BASE + 0x00180108)
#define QUAD_MAC_GMAC2_SOFT_RST_OFFSET8                              (C_MODEL_BASE + 0x00180118)
#define QUAD_MAC_GMAC2_TX_CTRL_OFFSET8                               (C_MODEL_BASE + 0x00180104)
#define QUAD_MAC_GMAC3_MODE_OFFSET8                                  (C_MODEL_BASE + 0x00180120)
#define QUAD_MAC_GMAC3_PTP_CFG_OFFSET8                               (C_MODEL_BASE + 0x00180130)
#define QUAD_MAC_GMAC3_PTP_STATUS_OFFSET8                            (C_MODEL_BASE + 0x00180134)
#define QUAD_MAC_GMAC3_RX_CTRL_OFFSET8                               (C_MODEL_BASE + 0x00180128)
#define QUAD_MAC_GMAC3_SOFT_RST_OFFSET8                              (C_MODEL_BASE + 0x00180138)
#define QUAD_MAC_GMAC3_TX_CTRL_OFFSET8                               (C_MODEL_BASE + 0x00180124)
#define QUAD_MAC_INIT_OFFSET8                                        (C_MODEL_BASE + 0x00180024)
#define QUAD_MAC_INIT_DONE_OFFSET8                                   (C_MODEL_BASE + 0x00180028)
#define QUAD_MAC_STATS_CFG_OFFSET8                                   (C_MODEL_BASE + 0x00180040)
#define QUAD_MAC_STATUS_OVER_WRITE_OFFSET8                           (C_MODEL_BASE + 0x00180030)
#define QUAD_MAC_INTERRUPT_NORMAL_OFFSET9                            (C_MODEL_BASE + 0x00190010)
#define QUAD_MAC_STATS_RAM_OFFSET9                                   (C_MODEL_BASE + 0x00192000)
#define QUAD_MAC_DATA_ERR_CTL_OFFSET9                                (C_MODEL_BASE + 0x00190048)
#define QUAD_MAC_DEBUG_STATS_OFFSET9                                 (C_MODEL_BASE + 0x00190090)
#define QUAD_MAC_GMAC0_MODE_OFFSET9                                  (C_MODEL_BASE + 0x001900c0)
#define QUAD_MAC_GMAC0_PTP_CFG_OFFSET9                               (C_MODEL_BASE + 0x001900d0)
#define QUAD_MAC_GMAC0_PTP_STATUS_OFFSET9                            (C_MODEL_BASE + 0x001900d4)
#define QUAD_MAC_GMAC0_RX_CTRL_OFFSET9                               (C_MODEL_BASE + 0x001900c8)
#define QUAD_MAC_GMAC0_SOFT_RST_OFFSET9                              (C_MODEL_BASE + 0x001900d8)
#define QUAD_MAC_GMAC0_TX_CTRL_OFFSET9                               (C_MODEL_BASE + 0x001900c4)
#define QUAD_MAC_GMAC1_MODE_OFFSET9                                  (C_MODEL_BASE + 0x001900e0)
#define QUAD_MAC_GMAC1_PTP_CFG_OFFSET9                               (C_MODEL_BASE + 0x001900f0)
#define QUAD_MAC_GMAC1_PTP_STATUS_OFFSET9                            (C_MODEL_BASE + 0x001900f4)
#define QUAD_MAC_GMAC1_RX_CTRL_OFFSET9                               (C_MODEL_BASE + 0x001900e8)
#define QUAD_MAC_GMAC1_SOFT_RST_OFFSET9                              (C_MODEL_BASE + 0x001900f8)
#define QUAD_MAC_GMAC1_TX_CTRL_OFFSET9                               (C_MODEL_BASE + 0x001900e4)
#define QUAD_MAC_GMAC2_MODE_OFFSET9                                  (C_MODEL_BASE + 0x00190100)
#define QUAD_MAC_GMAC2_PTP_CFG_OFFSET9                               (C_MODEL_BASE + 0x00190110)
#define QUAD_MAC_GMAC2_PTP_STATUS_OFFSET9                            (C_MODEL_BASE + 0x00190114)
#define QUAD_MAC_GMAC2_RX_CTRL_OFFSET9                               (C_MODEL_BASE + 0x00190108)
#define QUAD_MAC_GMAC2_SOFT_RST_OFFSET9                              (C_MODEL_BASE + 0x00190118)
#define QUAD_MAC_GMAC2_TX_CTRL_OFFSET9                               (C_MODEL_BASE + 0x00190104)
#define QUAD_MAC_GMAC3_MODE_OFFSET9                                  (C_MODEL_BASE + 0x00190120)
#define QUAD_MAC_GMAC3_PTP_CFG_OFFSET9                               (C_MODEL_BASE + 0x00190130)
#define QUAD_MAC_GMAC3_PTP_STATUS_OFFSET9                            (C_MODEL_BASE + 0x00190134)
#define QUAD_MAC_GMAC3_RX_CTRL_OFFSET9                               (C_MODEL_BASE + 0x00190128)
#define QUAD_MAC_GMAC3_SOFT_RST_OFFSET9                              (C_MODEL_BASE + 0x00190138)
#define QUAD_MAC_GMAC3_TX_CTRL_OFFSET9                               (C_MODEL_BASE + 0x00190124)
#define QUAD_MAC_INIT_OFFSET9                                        (C_MODEL_BASE + 0x00190024)
#define QUAD_MAC_INIT_DONE_OFFSET9                                   (C_MODEL_BASE + 0x00190028)
#define QUAD_MAC_STATS_CFG_OFFSET9                                   (C_MODEL_BASE + 0x00190040)
#define QUAD_MAC_STATUS_OVER_WRITE_OFFSET9                           (C_MODEL_BASE + 0x00190030)
#define QUAD_MAC_INTERRUPT_NORMAL_OFFSET10                           (C_MODEL_BASE + 0x001a0010)
#define QUAD_MAC_STATS_RAM_OFFSET10                                  (C_MODEL_BASE + 0x001a2000)
#define QUAD_MAC_DATA_ERR_CTL_OFFSET10                               (C_MODEL_BASE + 0x001a0048)
#define QUAD_MAC_DEBUG_STATS_OFFSET10                                (C_MODEL_BASE + 0x001a0090)
#define QUAD_MAC_GMAC0_MODE_OFFSET10                                 (C_MODEL_BASE + 0x001a00c0)
#define QUAD_MAC_GMAC0_PTP_CFG_OFFSET10                              (C_MODEL_BASE + 0x001a00d0)
#define QUAD_MAC_GMAC0_PTP_STATUS_OFFSET10                           (C_MODEL_BASE + 0x001a00d4)
#define QUAD_MAC_GMAC0_RX_CTRL_OFFSET10                              (C_MODEL_BASE + 0x001a00c8)
#define QUAD_MAC_GMAC0_SOFT_RST_OFFSET10                             (C_MODEL_BASE + 0x001a00d8)
#define QUAD_MAC_GMAC0_TX_CTRL_OFFSET10                              (C_MODEL_BASE + 0x001a00c4)
#define QUAD_MAC_GMAC1_MODE_OFFSET10                                 (C_MODEL_BASE + 0x001a00e0)
#define QUAD_MAC_GMAC1_PTP_CFG_OFFSET10                              (C_MODEL_BASE + 0x001a00f0)
#define QUAD_MAC_GMAC1_PTP_STATUS_OFFSET10                           (C_MODEL_BASE + 0x001a00f4)
#define QUAD_MAC_GMAC1_RX_CTRL_OFFSET10                              (C_MODEL_BASE + 0x001a00e8)
#define QUAD_MAC_GMAC1_SOFT_RST_OFFSET10                             (C_MODEL_BASE + 0x001a00f8)
#define QUAD_MAC_GMAC1_TX_CTRL_OFFSET10                              (C_MODEL_BASE + 0x001a00e4)
#define QUAD_MAC_GMAC2_MODE_OFFSET10                                 (C_MODEL_BASE + 0x001a0100)
#define QUAD_MAC_GMAC2_PTP_CFG_OFFSET10                              (C_MODEL_BASE + 0x001a0110)
#define QUAD_MAC_GMAC2_PTP_STATUS_OFFSET10                           (C_MODEL_BASE + 0x001a0114)
#define QUAD_MAC_GMAC2_RX_CTRL_OFFSET10                              (C_MODEL_BASE + 0x001a0108)
#define QUAD_MAC_GMAC2_SOFT_RST_OFFSET10                             (C_MODEL_BASE + 0x001a0118)
#define QUAD_MAC_GMAC2_TX_CTRL_OFFSET10                              (C_MODEL_BASE + 0x001a0104)
#define QUAD_MAC_GMAC3_MODE_OFFSET10                                 (C_MODEL_BASE + 0x001a0120)
#define QUAD_MAC_GMAC3_PTP_CFG_OFFSET10                              (C_MODEL_BASE + 0x001a0130)
#define QUAD_MAC_GMAC3_PTP_STATUS_OFFSET10                           (C_MODEL_BASE + 0x001a0134)
#define QUAD_MAC_GMAC3_RX_CTRL_OFFSET10                              (C_MODEL_BASE + 0x001a0128)
#define QUAD_MAC_GMAC3_SOFT_RST_OFFSET10                             (C_MODEL_BASE + 0x001a0138)
#define QUAD_MAC_GMAC3_TX_CTRL_OFFSET10                              (C_MODEL_BASE + 0x001a0124)
#define QUAD_MAC_INIT_OFFSET10                                       (C_MODEL_BASE + 0x001a0024)
#define QUAD_MAC_INIT_DONE_OFFSET10                                  (C_MODEL_BASE + 0x001a0028)
#define QUAD_MAC_STATS_CFG_OFFSET10                                  (C_MODEL_BASE + 0x001a0040)
#define QUAD_MAC_STATUS_OVER_WRITE_OFFSET10                          (C_MODEL_BASE + 0x001a0030)
#define QUAD_MAC_INTERRUPT_NORMAL_OFFSET11                           (C_MODEL_BASE + 0x001b0010)
#define QUAD_MAC_STATS_RAM_OFFSET11                                  (C_MODEL_BASE + 0x001b2000)
#define QUAD_MAC_DATA_ERR_CTL_OFFSET11                               (C_MODEL_BASE + 0x001b0048)
#define QUAD_MAC_DEBUG_STATS_OFFSET11                                (C_MODEL_BASE + 0x001b0090)
#define QUAD_MAC_GMAC0_MODE_OFFSET11                                 (C_MODEL_BASE + 0x001b00c0)
#define QUAD_MAC_GMAC0_PTP_CFG_OFFSET11                              (C_MODEL_BASE + 0x001b00d0)
#define QUAD_MAC_GMAC0_PTP_STATUS_OFFSET11                           (C_MODEL_BASE + 0x001b00d4)
#define QUAD_MAC_GMAC0_RX_CTRL_OFFSET11                              (C_MODEL_BASE + 0x001b00c8)
#define QUAD_MAC_GMAC0_SOFT_RST_OFFSET11                             (C_MODEL_BASE + 0x001b00d8)
#define QUAD_MAC_GMAC0_TX_CTRL_OFFSET11                              (C_MODEL_BASE + 0x001b00c4)
#define QUAD_MAC_GMAC1_MODE_OFFSET11                                 (C_MODEL_BASE + 0x001b00e0)
#define QUAD_MAC_GMAC1_PTP_CFG_OFFSET11                              (C_MODEL_BASE + 0x001b00f0)
#define QUAD_MAC_GMAC1_PTP_STATUS_OFFSET11                           (C_MODEL_BASE + 0x001b00f4)
#define QUAD_MAC_GMAC1_RX_CTRL_OFFSET11                              (C_MODEL_BASE + 0x001b00e8)
#define QUAD_MAC_GMAC1_SOFT_RST_OFFSET11                             (C_MODEL_BASE + 0x001b00f8)
#define QUAD_MAC_GMAC1_TX_CTRL_OFFSET11                              (C_MODEL_BASE + 0x001b00e4)
#define QUAD_MAC_GMAC2_MODE_OFFSET11                                 (C_MODEL_BASE + 0x001b0100)
#define QUAD_MAC_GMAC2_PTP_CFG_OFFSET11                              (C_MODEL_BASE + 0x001b0110)
#define QUAD_MAC_GMAC2_PTP_STATUS_OFFSET11                           (C_MODEL_BASE + 0x001b0114)
#define QUAD_MAC_GMAC2_RX_CTRL_OFFSET11                              (C_MODEL_BASE + 0x001b0108)
#define QUAD_MAC_GMAC2_SOFT_RST_OFFSET11                             (C_MODEL_BASE + 0x001b0118)
#define QUAD_MAC_GMAC2_TX_CTRL_OFFSET11                              (C_MODEL_BASE + 0x001b0104)
#define QUAD_MAC_GMAC3_MODE_OFFSET11                                 (C_MODEL_BASE + 0x001b0120)
#define QUAD_MAC_GMAC3_PTP_CFG_OFFSET11                              (C_MODEL_BASE + 0x001b0130)
#define QUAD_MAC_GMAC3_PTP_STATUS_OFFSET11                           (C_MODEL_BASE + 0x001b0134)
#define QUAD_MAC_GMAC3_RX_CTRL_OFFSET11                              (C_MODEL_BASE + 0x001b0128)
#define QUAD_MAC_GMAC3_SOFT_RST_OFFSET11                             (C_MODEL_BASE + 0x001b0138)
#define QUAD_MAC_GMAC3_TX_CTRL_OFFSET11                              (C_MODEL_BASE + 0x001b0124)
#define QUAD_MAC_INIT_OFFSET11                                       (C_MODEL_BASE + 0x001b0024)
#define QUAD_MAC_INIT_DONE_OFFSET11                                  (C_MODEL_BASE + 0x001b0028)
#define QUAD_MAC_STATS_CFG_OFFSET11                                  (C_MODEL_BASE + 0x001b0040)
#define QUAD_MAC_STATUS_OVER_WRITE_OFFSET11                          (C_MODEL_BASE + 0x001b0030)
#define QSGMII_INTERRUPT_NORMAL_OFFSET0                              (C_MODEL_BASE + 0x00200010)
#define QSGMII_PCS0_ANEG_CFG_OFFSET0                                 (C_MODEL_BASE + 0x00200060)
#define QSGMII_PCS0_ANEG_STATUS_OFFSET0                              (C_MODEL_BASE + 0x00200064)
#define QSGMII_PCS1_ANEG_CFG_OFFSET0                                 (C_MODEL_BASE + 0x00200068)
#define QSGMII_PCS1_ANEG_STATUS_OFFSET0                              (C_MODEL_BASE + 0x0020006c)
#define QSGMII_PCS2_ANEG_CFG_OFFSET0                                 (C_MODEL_BASE + 0x00200070)
#define QSGMII_PCS2_ANEG_STATUS_OFFSET0                              (C_MODEL_BASE + 0x00200074)
#define QSGMII_PCS3_ANEG_CFG_OFFSET0                                 (C_MODEL_BASE + 0x00200078)
#define QSGMII_PCS3_ANEG_STATUS_OFFSET0                              (C_MODEL_BASE + 0x0020007c)
#define QSGMII_PCS_CFG_OFFSET0                                       (C_MODEL_BASE + 0x00200020)
#define QSGMII_PCS_CODE_ERR_CNT_OFFSET0                              (C_MODEL_BASE + 0x00200024)
#define QSGMII_PCS_SOFT_RST_OFFSET0                                  (C_MODEL_BASE + 0x0020002c)
#define QSGMII_PCS_STATUS_OFFSET0                                    (C_MODEL_BASE + 0x00200028)
#define QSGMII_INTERRUPT_NORMAL_OFFSET1                              (C_MODEL_BASE + 0x00210010)
#define QSGMII_PCS0_ANEG_CFG_OFFSET1                                 (C_MODEL_BASE + 0x00210060)
#define QSGMII_PCS0_ANEG_STATUS_OFFSET1                              (C_MODEL_BASE + 0x00210064)
#define QSGMII_PCS1_ANEG_CFG_OFFSET1                                 (C_MODEL_BASE + 0x00210068)
#define QSGMII_PCS1_ANEG_STATUS_OFFSET1                              (C_MODEL_BASE + 0x0021006c)
#define QSGMII_PCS2_ANEG_CFG_OFFSET1                                 (C_MODEL_BASE + 0x00210070)
#define QSGMII_PCS2_ANEG_STATUS_OFFSET1                              (C_MODEL_BASE + 0x00210074)
#define QSGMII_PCS3_ANEG_CFG_OFFSET1                                 (C_MODEL_BASE + 0x00210078)
#define QSGMII_PCS3_ANEG_STATUS_OFFSET1                              (C_MODEL_BASE + 0x0021007c)
#define QSGMII_PCS_CFG_OFFSET1                                       (C_MODEL_BASE + 0x00210020)
#define QSGMII_PCS_CODE_ERR_CNT_OFFSET1                              (C_MODEL_BASE + 0x00210024)
#define QSGMII_PCS_SOFT_RST_OFFSET1                                  (C_MODEL_BASE + 0x0021002c)
#define QSGMII_PCS_STATUS_OFFSET1                                    (C_MODEL_BASE + 0x00210028)
#define QSGMII_INTERRUPT_NORMAL_OFFSET2                              (C_MODEL_BASE + 0x00220010)
#define QSGMII_PCS0_ANEG_CFG_OFFSET2                                 (C_MODEL_BASE + 0x00220060)
#define QSGMII_PCS0_ANEG_STATUS_OFFSET2                              (C_MODEL_BASE + 0x00220064)
#define QSGMII_PCS1_ANEG_CFG_OFFSET2                                 (C_MODEL_BASE + 0x00220068)
#define QSGMII_PCS1_ANEG_STATUS_OFFSET2                              (C_MODEL_BASE + 0x0022006c)
#define QSGMII_PCS2_ANEG_CFG_OFFSET2                                 (C_MODEL_BASE + 0x00220070)
#define QSGMII_PCS2_ANEG_STATUS_OFFSET2                              (C_MODEL_BASE + 0x00220074)
#define QSGMII_PCS3_ANEG_CFG_OFFSET2                                 (C_MODEL_BASE + 0x00220078)
#define QSGMII_PCS3_ANEG_STATUS_OFFSET2                              (C_MODEL_BASE + 0x0022007c)
#define QSGMII_PCS_CFG_OFFSET2                                       (C_MODEL_BASE + 0x00220020)
#define QSGMII_PCS_CODE_ERR_CNT_OFFSET2                              (C_MODEL_BASE + 0x00220024)
#define QSGMII_PCS_SOFT_RST_OFFSET2                                  (C_MODEL_BASE + 0x0022002c)
#define QSGMII_PCS_STATUS_OFFSET2                                    (C_MODEL_BASE + 0x00220028)
#define QSGMII_INTERRUPT_NORMAL_OFFSET3                              (C_MODEL_BASE + 0x00230010)
#define QSGMII_PCS0_ANEG_CFG_OFFSET3                                 (C_MODEL_BASE + 0x00230060)
#define QSGMII_PCS0_ANEG_STATUS_OFFSET3                              (C_MODEL_BASE + 0x00230064)
#define QSGMII_PCS1_ANEG_CFG_OFFSET3                                 (C_MODEL_BASE + 0x00230068)
#define QSGMII_PCS1_ANEG_STATUS_OFFSET3                              (C_MODEL_BASE + 0x0023006c)
#define QSGMII_PCS2_ANEG_CFG_OFFSET3                                 (C_MODEL_BASE + 0x00230070)
#define QSGMII_PCS2_ANEG_STATUS_OFFSET3                              (C_MODEL_BASE + 0x00230074)
#define QSGMII_PCS3_ANEG_CFG_OFFSET3                                 (C_MODEL_BASE + 0x00230078)
#define QSGMII_PCS3_ANEG_STATUS_OFFSET3                              (C_MODEL_BASE + 0x0023007c)
#define QSGMII_PCS_CFG_OFFSET3                                       (C_MODEL_BASE + 0x00230020)
#define QSGMII_PCS_CODE_ERR_CNT_OFFSET3                              (C_MODEL_BASE + 0x00230024)
#define QSGMII_PCS_SOFT_RST_OFFSET3                                  (C_MODEL_BASE + 0x0023002c)
#define QSGMII_PCS_STATUS_OFFSET3                                    (C_MODEL_BASE + 0x00230028)
#define QSGMII_INTERRUPT_NORMAL_OFFSET4                              (C_MODEL_BASE + 0x00240010)
#define QSGMII_PCS0_ANEG_CFG_OFFSET4                                 (C_MODEL_BASE + 0x00240060)
#define QSGMII_PCS0_ANEG_STATUS_OFFSET4                              (C_MODEL_BASE + 0x00240064)
#define QSGMII_PCS1_ANEG_CFG_OFFSET4                                 (C_MODEL_BASE + 0x00240068)
#define QSGMII_PCS1_ANEG_STATUS_OFFSET4                              (C_MODEL_BASE + 0x0024006c)
#define QSGMII_PCS2_ANEG_CFG_OFFSET4                                 (C_MODEL_BASE + 0x00240070)
#define QSGMII_PCS2_ANEG_STATUS_OFFSET4                              (C_MODEL_BASE + 0x00240074)
#define QSGMII_PCS3_ANEG_CFG_OFFSET4                                 (C_MODEL_BASE + 0x00240078)
#define QSGMII_PCS3_ANEG_STATUS_OFFSET4                              (C_MODEL_BASE + 0x0024007c)
#define QSGMII_PCS_CFG_OFFSET4                                       (C_MODEL_BASE + 0x00240020)
#define QSGMII_PCS_CODE_ERR_CNT_OFFSET4                              (C_MODEL_BASE + 0x00240024)
#define QSGMII_PCS_SOFT_RST_OFFSET4                                  (C_MODEL_BASE + 0x0024002c)
#define QSGMII_PCS_STATUS_OFFSET4                                    (C_MODEL_BASE + 0x00240028)
#define QSGMII_INTERRUPT_NORMAL_OFFSET5                              (C_MODEL_BASE + 0x00250010)
#define QSGMII_PCS0_ANEG_CFG_OFFSET5                                 (C_MODEL_BASE + 0x00250060)
#define QSGMII_PCS0_ANEG_STATUS_OFFSET5                              (C_MODEL_BASE + 0x00250064)
#define QSGMII_PCS1_ANEG_CFG_OFFSET5                                 (C_MODEL_BASE + 0x00250068)
#define QSGMII_PCS1_ANEG_STATUS_OFFSET5                              (C_MODEL_BASE + 0x0025006c)
#define QSGMII_PCS2_ANEG_CFG_OFFSET5                                 (C_MODEL_BASE + 0x00250070)
#define QSGMII_PCS2_ANEG_STATUS_OFFSET5                              (C_MODEL_BASE + 0x00250074)
#define QSGMII_PCS3_ANEG_CFG_OFFSET5                                 (C_MODEL_BASE + 0x00250078)
#define QSGMII_PCS3_ANEG_STATUS_OFFSET5                              (C_MODEL_BASE + 0x0025007c)
#define QSGMII_PCS_CFG_OFFSET5                                       (C_MODEL_BASE + 0x00250020)
#define QSGMII_PCS_CODE_ERR_CNT_OFFSET5                              (C_MODEL_BASE + 0x00250024)
#define QSGMII_PCS_SOFT_RST_OFFSET5                                  (C_MODEL_BASE + 0x0025002c)
#define QSGMII_PCS_STATUS_OFFSET5                                    (C_MODEL_BASE + 0x00250028)
#define QSGMII_INTERRUPT_NORMAL_OFFSET6                              (C_MODEL_BASE + 0x00260010)
#define QSGMII_PCS0_ANEG_CFG_OFFSET6                                 (C_MODEL_BASE + 0x00260060)
#define QSGMII_PCS0_ANEG_STATUS_OFFSET6                              (C_MODEL_BASE + 0x00260064)
#define QSGMII_PCS1_ANEG_CFG_OFFSET6                                 (C_MODEL_BASE + 0x00260068)
#define QSGMII_PCS1_ANEG_STATUS_OFFSET6                              (C_MODEL_BASE + 0x0026006c)
#define QSGMII_PCS2_ANEG_CFG_OFFSET6                                 (C_MODEL_BASE + 0x00260070)
#define QSGMII_PCS2_ANEG_STATUS_OFFSET6                              (C_MODEL_BASE + 0x00260074)
#define QSGMII_PCS3_ANEG_CFG_OFFSET6                                 (C_MODEL_BASE + 0x00260078)
#define QSGMII_PCS3_ANEG_STATUS_OFFSET6                              (C_MODEL_BASE + 0x0026007c)
#define QSGMII_PCS_CFG_OFFSET6                                       (C_MODEL_BASE + 0x00260020)
#define QSGMII_PCS_CODE_ERR_CNT_OFFSET6                              (C_MODEL_BASE + 0x00260024)
#define QSGMII_PCS_SOFT_RST_OFFSET6                                  (C_MODEL_BASE + 0x0026002c)
#define QSGMII_PCS_STATUS_OFFSET6                                    (C_MODEL_BASE + 0x00260028)
#define QSGMII_INTERRUPT_NORMAL_OFFSET7                              (C_MODEL_BASE + 0x00270010)
#define QSGMII_PCS0_ANEG_CFG_OFFSET7                                 (C_MODEL_BASE + 0x00270060)
#define QSGMII_PCS0_ANEG_STATUS_OFFSET7                              (C_MODEL_BASE + 0x00270064)
#define QSGMII_PCS1_ANEG_CFG_OFFSET7                                 (C_MODEL_BASE + 0x00270068)
#define QSGMII_PCS1_ANEG_STATUS_OFFSET7                              (C_MODEL_BASE + 0x0027006c)
#define QSGMII_PCS2_ANEG_CFG_OFFSET7                                 (C_MODEL_BASE + 0x00270070)
#define QSGMII_PCS2_ANEG_STATUS_OFFSET7                              (C_MODEL_BASE + 0x00270074)
#define QSGMII_PCS3_ANEG_CFG_OFFSET7                                 (C_MODEL_BASE + 0x00270078)
#define QSGMII_PCS3_ANEG_STATUS_OFFSET7                              (C_MODEL_BASE + 0x0027007c)
#define QSGMII_PCS_CFG_OFFSET7                                       (C_MODEL_BASE + 0x00270020)
#define QSGMII_PCS_CODE_ERR_CNT_OFFSET7                              (C_MODEL_BASE + 0x00270024)
#define QSGMII_PCS_SOFT_RST_OFFSET7                                  (C_MODEL_BASE + 0x0027002c)
#define QSGMII_PCS_STATUS_OFFSET7                                    (C_MODEL_BASE + 0x00270028)
#define QSGMII_INTERRUPT_NORMAL_OFFSET8                              (C_MODEL_BASE + 0x00280010)
#define QSGMII_PCS0_ANEG_CFG_OFFSET8                                 (C_MODEL_BASE + 0x00280060)
#define QSGMII_PCS0_ANEG_STATUS_OFFSET8                              (C_MODEL_BASE + 0x00280064)
#define QSGMII_PCS1_ANEG_CFG_OFFSET8                                 (C_MODEL_BASE + 0x00280068)
#define QSGMII_PCS1_ANEG_STATUS_OFFSET8                              (C_MODEL_BASE + 0x0028006c)
#define QSGMII_PCS2_ANEG_CFG_OFFSET8                                 (C_MODEL_BASE + 0x00280070)
#define QSGMII_PCS2_ANEG_STATUS_OFFSET8                              (C_MODEL_BASE + 0x00280074)
#define QSGMII_PCS3_ANEG_CFG_OFFSET8                                 (C_MODEL_BASE + 0x00280078)
#define QSGMII_PCS3_ANEG_STATUS_OFFSET8                              (C_MODEL_BASE + 0x0028007c)
#define QSGMII_PCS_CFG_OFFSET8                                       (C_MODEL_BASE + 0x00280020)
#define QSGMII_PCS_CODE_ERR_CNT_OFFSET8                              (C_MODEL_BASE + 0x00280024)
#define QSGMII_PCS_SOFT_RST_OFFSET8                                  (C_MODEL_BASE + 0x0028002c)
#define QSGMII_PCS_STATUS_OFFSET8                                    (C_MODEL_BASE + 0x00280028)
#define QSGMII_INTERRUPT_NORMAL_OFFSET9                              (C_MODEL_BASE + 0x00290010)
#define QSGMII_PCS0_ANEG_CFG_OFFSET9                                 (C_MODEL_BASE + 0x00290060)
#define QSGMII_PCS0_ANEG_STATUS_OFFSET9                              (C_MODEL_BASE + 0x00290064)
#define QSGMII_PCS1_ANEG_CFG_OFFSET9                                 (C_MODEL_BASE + 0x00290068)
#define QSGMII_PCS1_ANEG_STATUS_OFFSET9                              (C_MODEL_BASE + 0x0029006c)
#define QSGMII_PCS2_ANEG_CFG_OFFSET9                                 (C_MODEL_BASE + 0x00290070)
#define QSGMII_PCS2_ANEG_STATUS_OFFSET9                              (C_MODEL_BASE + 0x00290074)
#define QSGMII_PCS3_ANEG_CFG_OFFSET9                                 (C_MODEL_BASE + 0x00290078)
#define QSGMII_PCS3_ANEG_STATUS_OFFSET9                              (C_MODEL_BASE + 0x0029007c)
#define QSGMII_PCS_CFG_OFFSET9                                       (C_MODEL_BASE + 0x00290020)
#define QSGMII_PCS_CODE_ERR_CNT_OFFSET9                              (C_MODEL_BASE + 0x00290024)
#define QSGMII_PCS_SOFT_RST_OFFSET9                                  (C_MODEL_BASE + 0x0029002c)
#define QSGMII_PCS_STATUS_OFFSET9                                    (C_MODEL_BASE + 0x00290028)
#define QSGMII_INTERRUPT_NORMAL_OFFSET10                             (C_MODEL_BASE + 0x002a0010)
#define QSGMII_PCS0_ANEG_CFG_OFFSET10                                (C_MODEL_BASE + 0x002a0060)
#define QSGMII_PCS0_ANEG_STATUS_OFFSET10                             (C_MODEL_BASE + 0x002a0064)
#define QSGMII_PCS1_ANEG_CFG_OFFSET10                                (C_MODEL_BASE + 0x002a0068)
#define QSGMII_PCS1_ANEG_STATUS_OFFSET10                             (C_MODEL_BASE + 0x002a006c)
#define QSGMII_PCS2_ANEG_CFG_OFFSET10                                (C_MODEL_BASE + 0x002a0070)
#define QSGMII_PCS2_ANEG_STATUS_OFFSET10                             (C_MODEL_BASE + 0x002a0074)
#define QSGMII_PCS3_ANEG_CFG_OFFSET10                                (C_MODEL_BASE + 0x002a0078)
#define QSGMII_PCS3_ANEG_STATUS_OFFSET10                             (C_MODEL_BASE + 0x002a007c)
#define QSGMII_PCS_CFG_OFFSET10                                      (C_MODEL_BASE + 0x002a0020)
#define QSGMII_PCS_CODE_ERR_CNT_OFFSET10                             (C_MODEL_BASE + 0x002a0024)
#define QSGMII_PCS_SOFT_RST_OFFSET10                                 (C_MODEL_BASE + 0x002a002c)
#define QSGMII_PCS_STATUS_OFFSET10                                   (C_MODEL_BASE + 0x002a0028)
#define QSGMII_INTERRUPT_NORMAL_OFFSET11                             (C_MODEL_BASE + 0x002b0010)
#define QSGMII_PCS0_ANEG_CFG_OFFSET11                                (C_MODEL_BASE + 0x002b0060)
#define QSGMII_PCS0_ANEG_STATUS_OFFSET11                             (C_MODEL_BASE + 0x002b0064)
#define QSGMII_PCS1_ANEG_CFG_OFFSET11                                (C_MODEL_BASE + 0x002b0068)
#define QSGMII_PCS1_ANEG_STATUS_OFFSET11                             (C_MODEL_BASE + 0x002b006c)
#define QSGMII_PCS2_ANEG_CFG_OFFSET11                                (C_MODEL_BASE + 0x002b0070)
#define QSGMII_PCS2_ANEG_STATUS_OFFSET11                             (C_MODEL_BASE + 0x002b0074)
#define QSGMII_PCS3_ANEG_CFG_OFFSET11                                (C_MODEL_BASE + 0x002b0078)
#define QSGMII_PCS3_ANEG_STATUS_OFFSET11                             (C_MODEL_BASE + 0x002b007c)
#define QSGMII_PCS_CFG_OFFSET11                                      (C_MODEL_BASE + 0x002b0020)
#define QSGMII_PCS_CODE_ERR_CNT_OFFSET11                             (C_MODEL_BASE + 0x002b0024)
#define QSGMII_PCS_SOFT_RST_OFFSET11                                 (C_MODEL_BASE + 0x002b002c)
#define QSGMII_PCS_STATUS_OFFSET11                                   (C_MODEL_BASE + 0x002b0028)
#define QUAD_PCS_INTERRUPT_NORMAL_OFFSET0                            (C_MODEL_BASE + 0x00300010)
#define QUAD_PCS_PCS0_ANEG_CFG_OFFSET0                               (C_MODEL_BASE + 0x00300060)
#define QUAD_PCS_PCS0_ANEG_STATUS_OFFSET0                            (C_MODEL_BASE + 0x00300064)
#define QUAD_PCS_PCS0_CFG_OFFSET0                                    (C_MODEL_BASE + 0x00300020)
#define QUAD_PCS_PCS0_ERR_CNT_OFFSET0                                (C_MODEL_BASE + 0x00300024)
#define QUAD_PCS_PCS0_SOFT_RST_OFFSET0                               (C_MODEL_BASE + 0x0030002c)
#define QUAD_PCS_PCS0_STATUS_OFFSET0                                 (C_MODEL_BASE + 0x00300028)
#define QUAD_PCS_PCS1_ANEG_CFG_OFFSET0                               (C_MODEL_BASE + 0x00300068)
#define QUAD_PCS_PCS1_ANEG_STATUS_OFFSET0                            (C_MODEL_BASE + 0x0030006c)
#define QUAD_PCS_PCS1_CFG_OFFSET0                                    (C_MODEL_BASE + 0x00300030)
#define QUAD_PCS_PCS1_ERR_CNT_OFFSET0                                (C_MODEL_BASE + 0x00300034)
#define QUAD_PCS_PCS1_SOFT_RST_OFFSET0                               (C_MODEL_BASE + 0x0030003c)
#define QUAD_PCS_PCS1_STATUS_OFFSET0                                 (C_MODEL_BASE + 0x00300038)
#define QUAD_PCS_PCS2_ANEG_CFG_OFFSET0                               (C_MODEL_BASE + 0x00300070)
#define QUAD_PCS_PCS2_ANEG_STATUS_OFFSET0                            (C_MODEL_BASE + 0x00300074)
#define QUAD_PCS_PCS2_CFG_OFFSET0                                    (C_MODEL_BASE + 0x00300040)
#define QUAD_PCS_PCS2_ERR_CNT_OFFSET0                                (C_MODEL_BASE + 0x00300044)
#define QUAD_PCS_PCS2_SOFT_RST_OFFSET0                               (C_MODEL_BASE + 0x0030004c)
#define QUAD_PCS_PCS2_STATUS_OFFSET0                                 (C_MODEL_BASE + 0x00300048)
#define QUAD_PCS_PCS3_ANEG_CFG_OFFSET0                               (C_MODEL_BASE + 0x00300078)
#define QUAD_PCS_PCS3_ANEG_STATUS_OFFSET0                            (C_MODEL_BASE + 0x0030007c)
#define QUAD_PCS_PCS3_CFG_OFFSET0                                    (C_MODEL_BASE + 0x00300050)
#define QUAD_PCS_PCS3_ERR_CNT_OFFSET0                                (C_MODEL_BASE + 0x00300054)
#define QUAD_PCS_PCS3_SOFT_RST_OFFSET0                               (C_MODEL_BASE + 0x0030005c)
#define QUAD_PCS_PCS3_STATUS_OFFSET0                                 (C_MODEL_BASE + 0x00300058)
#define QUAD_PCS_INTERRUPT_NORMAL_OFFSET1                            (C_MODEL_BASE + 0x00310010)
#define QUAD_PCS_PCS0_ANEG_CFG_OFFSET1                               (C_MODEL_BASE + 0x00310060)
#define QUAD_PCS_PCS0_ANEG_STATUS_OFFSET1                            (C_MODEL_BASE + 0x00310064)
#define QUAD_PCS_PCS0_CFG_OFFSET1                                    (C_MODEL_BASE + 0x00310020)
#define QUAD_PCS_PCS0_ERR_CNT_OFFSET1                                (C_MODEL_BASE + 0x00310024)
#define QUAD_PCS_PCS0_SOFT_RST_OFFSET1                               (C_MODEL_BASE + 0x0031002c)
#define QUAD_PCS_PCS0_STATUS_OFFSET1                                 (C_MODEL_BASE + 0x00310028)
#define QUAD_PCS_PCS1_ANEG_CFG_OFFSET1                               (C_MODEL_BASE + 0x00310068)
#define QUAD_PCS_PCS1_ANEG_STATUS_OFFSET1                            (C_MODEL_BASE + 0x0031006c)
#define QUAD_PCS_PCS1_CFG_OFFSET1                                    (C_MODEL_BASE + 0x00310030)
#define QUAD_PCS_PCS1_ERR_CNT_OFFSET1                                (C_MODEL_BASE + 0x00310034)
#define QUAD_PCS_PCS1_SOFT_RST_OFFSET1                               (C_MODEL_BASE + 0x0031003c)
#define QUAD_PCS_PCS1_STATUS_OFFSET1                                 (C_MODEL_BASE + 0x00310038)
#define QUAD_PCS_PCS2_ANEG_CFG_OFFSET1                               (C_MODEL_BASE + 0x00310070)
#define QUAD_PCS_PCS2_ANEG_STATUS_OFFSET1                            (C_MODEL_BASE + 0x00310074)
#define QUAD_PCS_PCS2_CFG_OFFSET1                                    (C_MODEL_BASE + 0x00310040)
#define QUAD_PCS_PCS2_ERR_CNT_OFFSET1                                (C_MODEL_BASE + 0x00310044)
#define QUAD_PCS_PCS2_SOFT_RST_OFFSET1                               (C_MODEL_BASE + 0x0031004c)
#define QUAD_PCS_PCS2_STATUS_OFFSET1                                 (C_MODEL_BASE + 0x00310048)
#define QUAD_PCS_PCS3_ANEG_CFG_OFFSET1                               (C_MODEL_BASE + 0x00310078)
#define QUAD_PCS_PCS3_ANEG_STATUS_OFFSET1                            (C_MODEL_BASE + 0x0031007c)
#define QUAD_PCS_PCS3_CFG_OFFSET1                                    (C_MODEL_BASE + 0x00310050)
#define QUAD_PCS_PCS3_ERR_CNT_OFFSET1                                (C_MODEL_BASE + 0x00310054)
#define QUAD_PCS_PCS3_SOFT_RST_OFFSET1                               (C_MODEL_BASE + 0x0031005c)
#define QUAD_PCS_PCS3_STATUS_OFFSET1                                 (C_MODEL_BASE + 0x00310058)
#define QUAD_PCS_INTERRUPT_NORMAL_OFFSET2                            (C_MODEL_BASE + 0x00320010)
#define QUAD_PCS_PCS0_ANEG_CFG_OFFSET2                               (C_MODEL_BASE + 0x00320060)
#define QUAD_PCS_PCS0_ANEG_STATUS_OFFSET2                            (C_MODEL_BASE + 0x00320064)
#define QUAD_PCS_PCS0_CFG_OFFSET2                                    (C_MODEL_BASE + 0x00320020)
#define QUAD_PCS_PCS0_ERR_CNT_OFFSET2                                (C_MODEL_BASE + 0x00320024)
#define QUAD_PCS_PCS0_SOFT_RST_OFFSET2                               (C_MODEL_BASE + 0x0032002c)
#define QUAD_PCS_PCS0_STATUS_OFFSET2                                 (C_MODEL_BASE + 0x00320028)
#define QUAD_PCS_PCS1_ANEG_CFG_OFFSET2                               (C_MODEL_BASE + 0x00320068)
#define QUAD_PCS_PCS1_ANEG_STATUS_OFFSET2                            (C_MODEL_BASE + 0x0032006c)
#define QUAD_PCS_PCS1_CFG_OFFSET2                                    (C_MODEL_BASE + 0x00320030)
#define QUAD_PCS_PCS1_ERR_CNT_OFFSET2                                (C_MODEL_BASE + 0x00320034)
#define QUAD_PCS_PCS1_SOFT_RST_OFFSET2                               (C_MODEL_BASE + 0x0032003c)
#define QUAD_PCS_PCS1_STATUS_OFFSET2                                 (C_MODEL_BASE + 0x00320038)
#define QUAD_PCS_PCS2_ANEG_CFG_OFFSET2                               (C_MODEL_BASE + 0x00320070)
#define QUAD_PCS_PCS2_ANEG_STATUS_OFFSET2                            (C_MODEL_BASE + 0x00320074)
#define QUAD_PCS_PCS2_CFG_OFFSET2                                    (C_MODEL_BASE + 0x00320040)
#define QUAD_PCS_PCS2_ERR_CNT_OFFSET2                                (C_MODEL_BASE + 0x00320044)
#define QUAD_PCS_PCS2_SOFT_RST_OFFSET2                               (C_MODEL_BASE + 0x0032004c)
#define QUAD_PCS_PCS2_STATUS_OFFSET2                                 (C_MODEL_BASE + 0x00320048)
#define QUAD_PCS_PCS3_ANEG_CFG_OFFSET2                               (C_MODEL_BASE + 0x00320078)
#define QUAD_PCS_PCS3_ANEG_STATUS_OFFSET2                            (C_MODEL_BASE + 0x0032007c)
#define QUAD_PCS_PCS3_CFG_OFFSET2                                    (C_MODEL_BASE + 0x00320050)
#define QUAD_PCS_PCS3_ERR_CNT_OFFSET2                                (C_MODEL_BASE + 0x00320054)
#define QUAD_PCS_PCS3_SOFT_RST_OFFSET2                               (C_MODEL_BASE + 0x0032005c)
#define QUAD_PCS_PCS3_STATUS_OFFSET2                                 (C_MODEL_BASE + 0x00320058)
#define QUAD_PCS_INTERRUPT_NORMAL_OFFSET3                            (C_MODEL_BASE + 0x00330010)
#define QUAD_PCS_PCS0_ANEG_CFG_OFFSET3                               (C_MODEL_BASE + 0x00330060)
#define QUAD_PCS_PCS0_ANEG_STATUS_OFFSET3                            (C_MODEL_BASE + 0x00330064)
#define QUAD_PCS_PCS0_CFG_OFFSET3                                    (C_MODEL_BASE + 0x00330020)
#define QUAD_PCS_PCS0_ERR_CNT_OFFSET3                                (C_MODEL_BASE + 0x00330024)
#define QUAD_PCS_PCS0_SOFT_RST_OFFSET3                               (C_MODEL_BASE + 0x0033002c)
#define QUAD_PCS_PCS0_STATUS_OFFSET3                                 (C_MODEL_BASE + 0x00330028)
#define QUAD_PCS_PCS1_ANEG_CFG_OFFSET3                               (C_MODEL_BASE + 0x00330068)
#define QUAD_PCS_PCS1_ANEG_STATUS_OFFSET3                            (C_MODEL_BASE + 0x0033006c)
#define QUAD_PCS_PCS1_CFG_OFFSET3                                    (C_MODEL_BASE + 0x00330030)
#define QUAD_PCS_PCS1_ERR_CNT_OFFSET3                                (C_MODEL_BASE + 0x00330034)
#define QUAD_PCS_PCS1_SOFT_RST_OFFSET3                               (C_MODEL_BASE + 0x0033003c)
#define QUAD_PCS_PCS1_STATUS_OFFSET3                                 (C_MODEL_BASE + 0x00330038)
#define QUAD_PCS_PCS2_ANEG_CFG_OFFSET3                               (C_MODEL_BASE + 0x00330070)
#define QUAD_PCS_PCS2_ANEG_STATUS_OFFSET3                            (C_MODEL_BASE + 0x00330074)
#define QUAD_PCS_PCS2_CFG_OFFSET3                                    (C_MODEL_BASE + 0x00330040)
#define QUAD_PCS_PCS2_ERR_CNT_OFFSET3                                (C_MODEL_BASE + 0x00330044)
#define QUAD_PCS_PCS2_SOFT_RST_OFFSET3                               (C_MODEL_BASE + 0x0033004c)
#define QUAD_PCS_PCS2_STATUS_OFFSET3                                 (C_MODEL_BASE + 0x00330048)
#define QUAD_PCS_PCS3_ANEG_CFG_OFFSET3                               (C_MODEL_BASE + 0x00330078)
#define QUAD_PCS_PCS3_ANEG_STATUS_OFFSET3                            (C_MODEL_BASE + 0x0033007c)
#define QUAD_PCS_PCS3_CFG_OFFSET3                                    (C_MODEL_BASE + 0x00330050)
#define QUAD_PCS_PCS3_ERR_CNT_OFFSET3                                (C_MODEL_BASE + 0x00330054)
#define QUAD_PCS_PCS3_SOFT_RST_OFFSET3                               (C_MODEL_BASE + 0x0033005c)
#define QUAD_PCS_PCS3_STATUS_OFFSET3                                 (C_MODEL_BASE + 0x00330058)
#define QUAD_PCS_INTERRUPT_NORMAL_OFFSET4                            (C_MODEL_BASE + 0x00340010)
#define QUAD_PCS_PCS0_ANEG_CFG_OFFSET4                               (C_MODEL_BASE + 0x00340060)
#define QUAD_PCS_PCS0_ANEG_STATUS_OFFSET4                            (C_MODEL_BASE + 0x00340064)
#define QUAD_PCS_PCS0_CFG_OFFSET4                                    (C_MODEL_BASE + 0x00340020)
#define QUAD_PCS_PCS0_ERR_CNT_OFFSET4                                (C_MODEL_BASE + 0x00340024)
#define QUAD_PCS_PCS0_SOFT_RST_OFFSET4                               (C_MODEL_BASE + 0x0034002c)
#define QUAD_PCS_PCS0_STATUS_OFFSET4                                 (C_MODEL_BASE + 0x00340028)
#define QUAD_PCS_PCS1_ANEG_CFG_OFFSET4                               (C_MODEL_BASE + 0x00340068)
#define QUAD_PCS_PCS1_ANEG_STATUS_OFFSET4                            (C_MODEL_BASE + 0x0034006c)
#define QUAD_PCS_PCS1_CFG_OFFSET4                                    (C_MODEL_BASE + 0x00340030)
#define QUAD_PCS_PCS1_ERR_CNT_OFFSET4                                (C_MODEL_BASE + 0x00340034)
#define QUAD_PCS_PCS1_SOFT_RST_OFFSET4                               (C_MODEL_BASE + 0x0034003c)
#define QUAD_PCS_PCS1_STATUS_OFFSET4                                 (C_MODEL_BASE + 0x00340038)
#define QUAD_PCS_PCS2_ANEG_CFG_OFFSET4                               (C_MODEL_BASE + 0x00340070)
#define QUAD_PCS_PCS2_ANEG_STATUS_OFFSET4                            (C_MODEL_BASE + 0x00340074)
#define QUAD_PCS_PCS2_CFG_OFFSET4                                    (C_MODEL_BASE + 0x00340040)
#define QUAD_PCS_PCS2_ERR_CNT_OFFSET4                                (C_MODEL_BASE + 0x00340044)
#define QUAD_PCS_PCS2_SOFT_RST_OFFSET4                               (C_MODEL_BASE + 0x0034004c)
#define QUAD_PCS_PCS2_STATUS_OFFSET4                                 (C_MODEL_BASE + 0x00340048)
#define QUAD_PCS_PCS3_ANEG_CFG_OFFSET4                               (C_MODEL_BASE + 0x00340078)
#define QUAD_PCS_PCS3_ANEG_STATUS_OFFSET4                            (C_MODEL_BASE + 0x0034007c)
#define QUAD_PCS_PCS3_CFG_OFFSET4                                    (C_MODEL_BASE + 0x00340050)
#define QUAD_PCS_PCS3_ERR_CNT_OFFSET4                                (C_MODEL_BASE + 0x00340054)
#define QUAD_PCS_PCS3_SOFT_RST_OFFSET4                               (C_MODEL_BASE + 0x0034005c)
#define QUAD_PCS_PCS3_STATUS_OFFSET4                                 (C_MODEL_BASE + 0x00340058)
#define QUAD_PCS_INTERRUPT_NORMAL_OFFSET5                            (C_MODEL_BASE + 0x00350010)
#define QUAD_PCS_PCS0_ANEG_CFG_OFFSET5                               (C_MODEL_BASE + 0x00350060)
#define QUAD_PCS_PCS0_ANEG_STATUS_OFFSET5                            (C_MODEL_BASE + 0x00350064)
#define QUAD_PCS_PCS0_CFG_OFFSET5                                    (C_MODEL_BASE + 0x00350020)
#define QUAD_PCS_PCS0_ERR_CNT_OFFSET5                                (C_MODEL_BASE + 0x00350024)
#define QUAD_PCS_PCS0_SOFT_RST_OFFSET5                               (C_MODEL_BASE + 0x0035002c)
#define QUAD_PCS_PCS0_STATUS_OFFSET5                                 (C_MODEL_BASE + 0x00350028)
#define QUAD_PCS_PCS1_ANEG_CFG_OFFSET5                               (C_MODEL_BASE + 0x00350068)
#define QUAD_PCS_PCS1_ANEG_STATUS_OFFSET5                            (C_MODEL_BASE + 0x0035006c)
#define QUAD_PCS_PCS1_CFG_OFFSET5                                    (C_MODEL_BASE + 0x00350030)
#define QUAD_PCS_PCS1_ERR_CNT_OFFSET5                                (C_MODEL_BASE + 0x00350034)
#define QUAD_PCS_PCS1_SOFT_RST_OFFSET5                               (C_MODEL_BASE + 0x0035003c)
#define QUAD_PCS_PCS1_STATUS_OFFSET5                                 (C_MODEL_BASE + 0x00350038)
#define QUAD_PCS_PCS2_ANEG_CFG_OFFSET5                               (C_MODEL_BASE + 0x00350070)
#define QUAD_PCS_PCS2_ANEG_STATUS_OFFSET5                            (C_MODEL_BASE + 0x00350074)
#define QUAD_PCS_PCS2_CFG_OFFSET5                                    (C_MODEL_BASE + 0x00350040)
#define QUAD_PCS_PCS2_ERR_CNT_OFFSET5                                (C_MODEL_BASE + 0x00350044)
#define QUAD_PCS_PCS2_SOFT_RST_OFFSET5                               (C_MODEL_BASE + 0x0035004c)
#define QUAD_PCS_PCS2_STATUS_OFFSET5                                 (C_MODEL_BASE + 0x00350048)
#define QUAD_PCS_PCS3_ANEG_CFG_OFFSET5                               (C_MODEL_BASE + 0x00350078)
#define QUAD_PCS_PCS3_ANEG_STATUS_OFFSET5                            (C_MODEL_BASE + 0x0035007c)
#define QUAD_PCS_PCS3_CFG_OFFSET5                                    (C_MODEL_BASE + 0x00350050)
#define QUAD_PCS_PCS3_ERR_CNT_OFFSET5                                (C_MODEL_BASE + 0x00350054)
#define QUAD_PCS_PCS3_SOFT_RST_OFFSET5                               (C_MODEL_BASE + 0x0035005c)
#define QUAD_PCS_PCS3_STATUS_OFFSET5                                 (C_MODEL_BASE + 0x00350058)
#define MAC_LED_CFG_PORT_MODE_OFFSET                                 (C_MODEL_BASE + 0x00360100)
#define MAC_LED_CFG_PORT_SEQ_MAP_OFFSET                              (C_MODEL_BASE + 0x00360000)
#define MAC_LED_DRIVER_INTERRUPT_NORMAL_OFFSET                       (C_MODEL_BASE + 0x00360200)
#define MAC_LED_BLINK_CFG_OFFSET                                     (C_MODEL_BASE + 0x00360218)
#define MAC_LED_CFG_ASYNC_FIFO_THR_OFFSET                            (C_MODEL_BASE + 0x00360230)
#define MAC_LED_CFG_CAL_CTL_OFFSET                                   (C_MODEL_BASE + 0x00360234)
#define MAC_LED_POLARITY_CFG_OFFSET                                  (C_MODEL_BASE + 0x0036022c)
#define MAC_LED_PORT_RANGE_OFFSET                                    (C_MODEL_BASE + 0x00360238)
#define MAC_LED_RAW_STATUS_CFG_OFFSET                                (C_MODEL_BASE + 0x00360228)
#define MAC_LED_REFRESH_INTERVAL_OFFSET                              (C_MODEL_BASE + 0x00360210)
#define MAC_LED_SAMPLE_INTERVAL_OFFSET                               (C_MODEL_BASE + 0x00360220)
#define SGMAC_INTERRUPT_NORMAL_OFFSET0                               (C_MODEL_BASE + 0x00400010)
#define SGMAC_STATS_RAM_OFFSET0                                      (C_MODEL_BASE + 0x00400400)
#define SGMAC_CFG_OFFSET0                                            (C_MODEL_BASE + 0x00400020)
#define SGMAC_CTL_OFFSET0                                            (C_MODEL_BASE + 0x004001d0)
#define SGMAC_DEBUG_STATUS_OFFSET0                                   (C_MODEL_BASE + 0x00400080)
#define SGMAC_PAUSE_CFG_OFFSET0                                      (C_MODEL_BASE + 0x00400040)
#define SGMAC_PCS_ANEG_CFG_OFFSET0                                   (C_MODEL_BASE + 0x004000e8)
#define SGMAC_PCS_ANEG_STATUS_OFFSET0                                (C_MODEL_BASE + 0x004000ec)
#define SGMAC_PCS_CFG_OFFSET0                                        (C_MODEL_BASE + 0x004000d8)
#define SGMAC_PCS_ERR_CNT_OFFSET0                                    (C_MODEL_BASE + 0x004000dc)
#define SGMAC_PCS_STATUS_OFFSET0                                     (C_MODEL_BASE + 0x004000e0)
#define SGMAC_PTP_STATUS_OFFSET0                                     (C_MODEL_BASE + 0x00400090)
#define SGMAC_SOFT_RST_OFFSET0                                       (C_MODEL_BASE + 0x0040002c)
#define SGMAC_STATS_CFG_OFFSET0                                      (C_MODEL_BASE + 0x004000c0)
#define SGMAC_STATS_INIT_OFFSET0                                     (C_MODEL_BASE + 0x004000c8)
#define SGMAC_STATS_INIT_DONE_OFFSET0                                (C_MODEL_BASE + 0x004000cc)
#define SGMAC_XAUI_CFG_OFFSET0                                       (C_MODEL_BASE + 0x00400030)
#define SGMAC_XFI_DEBUG_STATS_OFFSET0                                (C_MODEL_BASE + 0x004001a0)
#define SGMAC_XFI_TEST_OFFSET0                                       (C_MODEL_BASE + 0x00400180)
#define SGMAC_XFI_TEST_PAT_SEED_OFFSET0                              (C_MODEL_BASE + 0x00400190)
#define SGMAC_INTERRUPT_NORMAL_OFFSET1                               (C_MODEL_BASE + 0x00410010)
#define SGMAC_STATS_RAM_OFFSET1                                      (C_MODEL_BASE + 0x00410400)
#define SGMAC_CFG_OFFSET1                                            (C_MODEL_BASE + 0x00410020)
#define SGMAC_CTL_OFFSET1                                            (C_MODEL_BASE + 0x004101d0)
#define SGMAC_DEBUG_STATUS_OFFSET1                                   (C_MODEL_BASE + 0x00410080)
#define SGMAC_PAUSE_CFG_OFFSET1                                      (C_MODEL_BASE + 0x00410040)
#define SGMAC_PCS_ANEG_CFG_OFFSET1                                   (C_MODEL_BASE + 0x004100e8)
#define SGMAC_PCS_ANEG_STATUS_OFFSET1                                (C_MODEL_BASE + 0x004100ec)
#define SGMAC_PCS_CFG_OFFSET1                                        (C_MODEL_BASE + 0x004100d8)
#define SGMAC_PCS_ERR_CNT_OFFSET1                                    (C_MODEL_BASE + 0x004100dc)
#define SGMAC_PCS_STATUS_OFFSET1                                     (C_MODEL_BASE + 0x004100e0)
#define SGMAC_PTP_STATUS_OFFSET1                                     (C_MODEL_BASE + 0x00410090)
#define SGMAC_SOFT_RST_OFFSET1                                       (C_MODEL_BASE + 0x0041002c)
#define SGMAC_STATS_CFG_OFFSET1                                      (C_MODEL_BASE + 0x004100c0)
#define SGMAC_STATS_INIT_OFFSET1                                     (C_MODEL_BASE + 0x004100c8)
#define SGMAC_STATS_INIT_DONE_OFFSET1                                (C_MODEL_BASE + 0x004100cc)
#define SGMAC_XAUI_CFG_OFFSET1                                       (C_MODEL_BASE + 0x00410030)
#define SGMAC_XFI_DEBUG_STATS_OFFSET1                                (C_MODEL_BASE + 0x004101a0)
#define SGMAC_XFI_TEST_OFFSET1                                       (C_MODEL_BASE + 0x00410180)
#define SGMAC_XFI_TEST_PAT_SEED_OFFSET1                              (C_MODEL_BASE + 0x00410190)
#define SGMAC_INTERRUPT_NORMAL_OFFSET2                               (C_MODEL_BASE + 0x00420010)
#define SGMAC_STATS_RAM_OFFSET2                                      (C_MODEL_BASE + 0x00420400)
#define SGMAC_CFG_OFFSET2                                            (C_MODEL_BASE + 0x00420020)
#define SGMAC_CTL_OFFSET2                                            (C_MODEL_BASE + 0x004201d0)
#define SGMAC_DEBUG_STATUS_OFFSET2                                   (C_MODEL_BASE + 0x00420080)
#define SGMAC_PAUSE_CFG_OFFSET2                                      (C_MODEL_BASE + 0x00420040)
#define SGMAC_PCS_ANEG_CFG_OFFSET2                                   (C_MODEL_BASE + 0x004200e8)
#define SGMAC_PCS_ANEG_STATUS_OFFSET2                                (C_MODEL_BASE + 0x004200ec)
#define SGMAC_PCS_CFG_OFFSET2                                        (C_MODEL_BASE + 0x004200d8)
#define SGMAC_PCS_ERR_CNT_OFFSET2                                    (C_MODEL_BASE + 0x004200dc)
#define SGMAC_PCS_STATUS_OFFSET2                                     (C_MODEL_BASE + 0x004200e0)
#define SGMAC_PTP_STATUS_OFFSET2                                     (C_MODEL_BASE + 0x00420090)
#define SGMAC_SOFT_RST_OFFSET2                                       (C_MODEL_BASE + 0x0042002c)
#define SGMAC_STATS_CFG_OFFSET2                                      (C_MODEL_BASE + 0x004200c0)
#define SGMAC_STATS_INIT_OFFSET2                                     (C_MODEL_BASE + 0x004200c8)
#define SGMAC_STATS_INIT_DONE_OFFSET2                                (C_MODEL_BASE + 0x004200cc)
#define SGMAC_XAUI_CFG_OFFSET2                                       (C_MODEL_BASE + 0x00420030)
#define SGMAC_XFI_DEBUG_STATS_OFFSET2                                (C_MODEL_BASE + 0x004201a0)
#define SGMAC_XFI_TEST_OFFSET2                                       (C_MODEL_BASE + 0x00420180)
#define SGMAC_XFI_TEST_PAT_SEED_OFFSET2                              (C_MODEL_BASE + 0x00420190)
#define SGMAC_INTERRUPT_NORMAL_OFFSET3                               (C_MODEL_BASE + 0x00430010)
#define SGMAC_STATS_RAM_OFFSET3                                      (C_MODEL_BASE + 0x00430400)
#define SGMAC_CFG_OFFSET3                                            (C_MODEL_BASE + 0x00430020)
#define SGMAC_CTL_OFFSET3                                            (C_MODEL_BASE + 0x004301d0)
#define SGMAC_DEBUG_STATUS_OFFSET3                                   (C_MODEL_BASE + 0x00430080)
#define SGMAC_PAUSE_CFG_OFFSET3                                      (C_MODEL_BASE + 0x00430040)
#define SGMAC_PCS_ANEG_CFG_OFFSET3                                   (C_MODEL_BASE + 0x004300e8)
#define SGMAC_PCS_ANEG_STATUS_OFFSET3                                (C_MODEL_BASE + 0x004300ec)
#define SGMAC_PCS_CFG_OFFSET3                                        (C_MODEL_BASE + 0x004300d8)
#define SGMAC_PCS_ERR_CNT_OFFSET3                                    (C_MODEL_BASE + 0x004300dc)
#define SGMAC_PCS_STATUS_OFFSET3                                     (C_MODEL_BASE + 0x004300e0)
#define SGMAC_PTP_STATUS_OFFSET3                                     (C_MODEL_BASE + 0x00430090)
#define SGMAC_SOFT_RST_OFFSET3                                       (C_MODEL_BASE + 0x0043002c)
#define SGMAC_STATS_CFG_OFFSET3                                      (C_MODEL_BASE + 0x004300c0)
#define SGMAC_STATS_INIT_OFFSET3                                     (C_MODEL_BASE + 0x004300c8)
#define SGMAC_STATS_INIT_DONE_OFFSET3                                (C_MODEL_BASE + 0x004300cc)
#define SGMAC_XAUI_CFG_OFFSET3                                       (C_MODEL_BASE + 0x00430030)
#define SGMAC_XFI_DEBUG_STATS_OFFSET3                                (C_MODEL_BASE + 0x004301a0)
#define SGMAC_XFI_TEST_OFFSET3                                       (C_MODEL_BASE + 0x00430180)
#define SGMAC_XFI_TEST_PAT_SEED_OFFSET3                              (C_MODEL_BASE + 0x00430190)
#define SGMAC_INTERRUPT_NORMAL_OFFSET4                               (C_MODEL_BASE + 0x00440010)
#define SGMAC_STATS_RAM_OFFSET4                                      (C_MODEL_BASE + 0x00440400)
#define SGMAC_CFG_OFFSET4                                            (C_MODEL_BASE + 0x00440020)
#define SGMAC_CTL_OFFSET4                                            (C_MODEL_BASE + 0x004401d0)
#define SGMAC_DEBUG_STATUS_OFFSET4                                   (C_MODEL_BASE + 0x00440080)
#define SGMAC_PAUSE_CFG_OFFSET4                                      (C_MODEL_BASE + 0x00440040)
#define SGMAC_PCS_ANEG_CFG_OFFSET4                                   (C_MODEL_BASE + 0x004400e8)
#define SGMAC_PCS_ANEG_STATUS_OFFSET4                                (C_MODEL_BASE + 0x004400ec)
#define SGMAC_PCS_CFG_OFFSET4                                        (C_MODEL_BASE + 0x004400d8)
#define SGMAC_PCS_ERR_CNT_OFFSET4                                    (C_MODEL_BASE + 0x004400dc)
#define SGMAC_PCS_STATUS_OFFSET4                                     (C_MODEL_BASE + 0x004400e0)
#define SGMAC_PTP_STATUS_OFFSET4                                     (C_MODEL_BASE + 0x00440090)
#define SGMAC_SOFT_RST_OFFSET4                                       (C_MODEL_BASE + 0x0044002c)
#define SGMAC_STATS_CFG_OFFSET4                                      (C_MODEL_BASE + 0x004400c0)
#define SGMAC_STATS_INIT_OFFSET4                                     (C_MODEL_BASE + 0x004400c8)
#define SGMAC_STATS_INIT_DONE_OFFSET4                                (C_MODEL_BASE + 0x004400cc)
#define SGMAC_XAUI_CFG_OFFSET4                                       (C_MODEL_BASE + 0x00440030)
#define SGMAC_XFI_DEBUG_STATS_OFFSET4                                (C_MODEL_BASE + 0x004401a0)
#define SGMAC_XFI_TEST_OFFSET4                                       (C_MODEL_BASE + 0x00440180)
#define SGMAC_XFI_TEST_PAT_SEED_OFFSET4                              (C_MODEL_BASE + 0x00440190)
#define SGMAC_INTERRUPT_NORMAL_OFFSET5                               (C_MODEL_BASE + 0x00450010)
#define SGMAC_STATS_RAM_OFFSET5                                      (C_MODEL_BASE + 0x00450400)
#define SGMAC_CFG_OFFSET5                                            (C_MODEL_BASE + 0x00450020)
#define SGMAC_CTL_OFFSET5                                            (C_MODEL_BASE + 0x004501d0)
#define SGMAC_DEBUG_STATUS_OFFSET5                                   (C_MODEL_BASE + 0x00450080)
#define SGMAC_PAUSE_CFG_OFFSET5                                      (C_MODEL_BASE + 0x00450040)
#define SGMAC_PCS_ANEG_CFG_OFFSET5                                   (C_MODEL_BASE + 0x004500e8)
#define SGMAC_PCS_ANEG_STATUS_OFFSET5                                (C_MODEL_BASE + 0x004500ec)
#define SGMAC_PCS_CFG_OFFSET5                                        (C_MODEL_BASE + 0x004500d8)
#define SGMAC_PCS_ERR_CNT_OFFSET5                                    (C_MODEL_BASE + 0x004500dc)
#define SGMAC_PCS_STATUS_OFFSET5                                     (C_MODEL_BASE + 0x004500e0)
#define SGMAC_PTP_STATUS_OFFSET5                                     (C_MODEL_BASE + 0x00450090)
#define SGMAC_SOFT_RST_OFFSET5                                       (C_MODEL_BASE + 0x0045002c)
#define SGMAC_STATS_CFG_OFFSET5                                      (C_MODEL_BASE + 0x004500c0)
#define SGMAC_STATS_INIT_OFFSET5                                     (C_MODEL_BASE + 0x004500c8)
#define SGMAC_STATS_INIT_DONE_OFFSET5                                (C_MODEL_BASE + 0x004500cc)
#define SGMAC_XAUI_CFG_OFFSET5                                       (C_MODEL_BASE + 0x00450030)
#define SGMAC_XFI_DEBUG_STATS_OFFSET5                                (C_MODEL_BASE + 0x004501a0)
#define SGMAC_XFI_TEST_OFFSET5                                       (C_MODEL_BASE + 0x00450180)
#define SGMAC_XFI_TEST_PAT_SEED_OFFSET5                              (C_MODEL_BASE + 0x00450190)
#define SGMAC_INTERRUPT_NORMAL_OFFSET6                               (C_MODEL_BASE + 0x00460010)
#define SGMAC_STATS_RAM_OFFSET6                                      (C_MODEL_BASE + 0x00460400)
#define SGMAC_CFG_OFFSET6                                            (C_MODEL_BASE + 0x00460020)
#define SGMAC_CTL_OFFSET6                                            (C_MODEL_BASE + 0x004601d0)
#define SGMAC_DEBUG_STATUS_OFFSET6                                   (C_MODEL_BASE + 0x00460080)
#define SGMAC_PAUSE_CFG_OFFSET6                                      (C_MODEL_BASE + 0x00460040)
#define SGMAC_PCS_ANEG_CFG_OFFSET6                                   (C_MODEL_BASE + 0x004600e8)
#define SGMAC_PCS_ANEG_STATUS_OFFSET6                                (C_MODEL_BASE + 0x004600ec)
#define SGMAC_PCS_CFG_OFFSET6                                        (C_MODEL_BASE + 0x004600d8)
#define SGMAC_PCS_ERR_CNT_OFFSET6                                    (C_MODEL_BASE + 0x004600dc)
#define SGMAC_PCS_STATUS_OFFSET6                                     (C_MODEL_BASE + 0x004600e0)
#define SGMAC_PTP_STATUS_OFFSET6                                     (C_MODEL_BASE + 0x00460090)
#define SGMAC_SOFT_RST_OFFSET6                                       (C_MODEL_BASE + 0x0046002c)
#define SGMAC_STATS_CFG_OFFSET6                                      (C_MODEL_BASE + 0x004600c0)
#define SGMAC_STATS_INIT_OFFSET6                                     (C_MODEL_BASE + 0x004600c8)
#define SGMAC_STATS_INIT_DONE_OFFSET6                                (C_MODEL_BASE + 0x004600cc)
#define SGMAC_XAUI_CFG_OFFSET6                                       (C_MODEL_BASE + 0x00460030)
#define SGMAC_XFI_DEBUG_STATS_OFFSET6                                (C_MODEL_BASE + 0x004601a0)
#define SGMAC_XFI_TEST_OFFSET6                                       (C_MODEL_BASE + 0x00460180)
#define SGMAC_XFI_TEST_PAT_SEED_OFFSET6                              (C_MODEL_BASE + 0x00460190)
#define SGMAC_INTERRUPT_NORMAL_OFFSET7                               (C_MODEL_BASE + 0x00470010)
#define SGMAC_STATS_RAM_OFFSET7                                      (C_MODEL_BASE + 0x00470400)
#define SGMAC_CFG_OFFSET7                                            (C_MODEL_BASE + 0x00470020)
#define SGMAC_CTL_OFFSET7                                            (C_MODEL_BASE + 0x004701d0)
#define SGMAC_DEBUG_STATUS_OFFSET7                                   (C_MODEL_BASE + 0x00470080)
#define SGMAC_PAUSE_CFG_OFFSET7                                      (C_MODEL_BASE + 0x00470040)
#define SGMAC_PCS_ANEG_CFG_OFFSET7                                   (C_MODEL_BASE + 0x004700e8)
#define SGMAC_PCS_ANEG_STATUS_OFFSET7                                (C_MODEL_BASE + 0x004700ec)
#define SGMAC_PCS_CFG_OFFSET7                                        (C_MODEL_BASE + 0x004700d8)
#define SGMAC_PCS_ERR_CNT_OFFSET7                                    (C_MODEL_BASE + 0x004700dc)
#define SGMAC_PCS_STATUS_OFFSET7                                     (C_MODEL_BASE + 0x004700e0)
#define SGMAC_PTP_STATUS_OFFSET7                                     (C_MODEL_BASE + 0x00470090)
#define SGMAC_SOFT_RST_OFFSET7                                       (C_MODEL_BASE + 0x0047002c)
#define SGMAC_STATS_CFG_OFFSET7                                      (C_MODEL_BASE + 0x004700c0)
#define SGMAC_STATS_INIT_OFFSET7                                     (C_MODEL_BASE + 0x004700c8)
#define SGMAC_STATS_INIT_DONE_OFFSET7                                (C_MODEL_BASE + 0x004700cc)
#define SGMAC_XAUI_CFG_OFFSET7                                       (C_MODEL_BASE + 0x00470030)
#define SGMAC_XFI_DEBUG_STATS_OFFSET7                                (C_MODEL_BASE + 0x004701a0)
#define SGMAC_XFI_TEST_OFFSET7                                       (C_MODEL_BASE + 0x00470180)
#define SGMAC_XFI_TEST_PAT_SEED_OFFSET7                              (C_MODEL_BASE + 0x00470190)
#define SGMAC_INTERRUPT_NORMAL_OFFSET8                               (C_MODEL_BASE + 0x00480010)
#define SGMAC_STATS_RAM_OFFSET8                                      (C_MODEL_BASE + 0x00480400)
#define SGMAC_CFG_OFFSET8                                            (C_MODEL_BASE + 0x00480020)
#define SGMAC_CTL_OFFSET8                                            (C_MODEL_BASE + 0x004801d0)
#define SGMAC_DEBUG_STATUS_OFFSET8                                   (C_MODEL_BASE + 0x00480080)
#define SGMAC_PAUSE_CFG_OFFSET8                                      (C_MODEL_BASE + 0x00480040)
#define SGMAC_PCS_ANEG_CFG_OFFSET8                                   (C_MODEL_BASE + 0x004800e8)
#define SGMAC_PCS_ANEG_STATUS_OFFSET8                                (C_MODEL_BASE + 0x004800ec)
#define SGMAC_PCS_CFG_OFFSET8                                        (C_MODEL_BASE + 0x004800d8)
#define SGMAC_PCS_ERR_CNT_OFFSET8                                    (C_MODEL_BASE + 0x004800dc)
#define SGMAC_PCS_STATUS_OFFSET8                                     (C_MODEL_BASE + 0x004800e0)
#define SGMAC_PTP_STATUS_OFFSET8                                     (C_MODEL_BASE + 0x00480090)
#define SGMAC_SOFT_RST_OFFSET8                                       (C_MODEL_BASE + 0x0048002c)
#define SGMAC_STATS_CFG_OFFSET8                                      (C_MODEL_BASE + 0x004800c0)
#define SGMAC_STATS_INIT_OFFSET8                                     (C_MODEL_BASE + 0x004800c8)
#define SGMAC_STATS_INIT_DONE_OFFSET8                                (C_MODEL_BASE + 0x004800cc)
#define SGMAC_XAUI_CFG_OFFSET8                                       (C_MODEL_BASE + 0x00480030)
#define SGMAC_XFI_DEBUG_STATS_OFFSET8                                (C_MODEL_BASE + 0x004801a0)
#define SGMAC_XFI_TEST_OFFSET8                                       (C_MODEL_BASE + 0x00480180)
#define SGMAC_XFI_TEST_PAT_SEED_OFFSET8                              (C_MODEL_BASE + 0x00480190)
#define SGMAC_INTERRUPT_NORMAL_OFFSET9                               (C_MODEL_BASE + 0x00490010)
#define SGMAC_STATS_RAM_OFFSET9                                      (C_MODEL_BASE + 0x00490400)
#define SGMAC_CFG_OFFSET9                                            (C_MODEL_BASE + 0x00490020)
#define SGMAC_CTL_OFFSET9                                            (C_MODEL_BASE + 0x004901d0)
#define SGMAC_DEBUG_STATUS_OFFSET9                                   (C_MODEL_BASE + 0x00490080)
#define SGMAC_PAUSE_CFG_OFFSET9                                      (C_MODEL_BASE + 0x00490040)
#define SGMAC_PCS_ANEG_CFG_OFFSET9                                   (C_MODEL_BASE + 0x004900e8)
#define SGMAC_PCS_ANEG_STATUS_OFFSET9                                (C_MODEL_BASE + 0x004900ec)
#define SGMAC_PCS_CFG_OFFSET9                                        (C_MODEL_BASE + 0x004900d8)
#define SGMAC_PCS_ERR_CNT_OFFSET9                                    (C_MODEL_BASE + 0x004900dc)
#define SGMAC_PCS_STATUS_OFFSET9                                     (C_MODEL_BASE + 0x004900e0)
#define SGMAC_PTP_STATUS_OFFSET9                                     (C_MODEL_BASE + 0x00490090)
#define SGMAC_SOFT_RST_OFFSET9                                       (C_MODEL_BASE + 0x0049002c)
#define SGMAC_STATS_CFG_OFFSET9                                      (C_MODEL_BASE + 0x004900c0)
#define SGMAC_STATS_INIT_OFFSET9                                     (C_MODEL_BASE + 0x004900c8)
#define SGMAC_STATS_INIT_DONE_OFFSET9                                (C_MODEL_BASE + 0x004900cc)
#define SGMAC_XAUI_CFG_OFFSET9                                       (C_MODEL_BASE + 0x00490030)
#define SGMAC_XFI_DEBUG_STATS_OFFSET9                                (C_MODEL_BASE + 0x004901a0)
#define SGMAC_XFI_TEST_OFFSET9                                       (C_MODEL_BASE + 0x00490180)
#define SGMAC_XFI_TEST_PAT_SEED_OFFSET9                              (C_MODEL_BASE + 0x00490190)
#define SGMAC_INTERRUPT_NORMAL_OFFSET10                              (C_MODEL_BASE + 0x004a0010)
#define SGMAC_STATS_RAM_OFFSET10                                     (C_MODEL_BASE + 0x004a0400)
#define SGMAC_CFG_OFFSET10                                           (C_MODEL_BASE + 0x004a0020)
#define SGMAC_CTL_OFFSET10                                           (C_MODEL_BASE + 0x004a01d0)
#define SGMAC_DEBUG_STATUS_OFFSET10                                  (C_MODEL_BASE + 0x004a0080)
#define SGMAC_PAUSE_CFG_OFFSET10                                     (C_MODEL_BASE + 0x004a0040)
#define SGMAC_PCS_ANEG_CFG_OFFSET10                                  (C_MODEL_BASE + 0x004a00e8)
#define SGMAC_PCS_ANEG_STATUS_OFFSET10                               (C_MODEL_BASE + 0x004a00ec)
#define SGMAC_PCS_CFG_OFFSET10                                       (C_MODEL_BASE + 0x004a00d8)
#define SGMAC_PCS_ERR_CNT_OFFSET10                                   (C_MODEL_BASE + 0x004a00dc)
#define SGMAC_PCS_STATUS_OFFSET10                                    (C_MODEL_BASE + 0x004a00e0)
#define SGMAC_PTP_STATUS_OFFSET10                                    (C_MODEL_BASE + 0x004a0090)
#define SGMAC_SOFT_RST_OFFSET10                                      (C_MODEL_BASE + 0x004a002c)
#define SGMAC_STATS_CFG_OFFSET10                                     (C_MODEL_BASE + 0x004a00c0)
#define SGMAC_STATS_INIT_OFFSET10                                    (C_MODEL_BASE + 0x004a00c8)
#define SGMAC_STATS_INIT_DONE_OFFSET10                               (C_MODEL_BASE + 0x004a00cc)
#define SGMAC_XAUI_CFG_OFFSET10                                      (C_MODEL_BASE + 0x004a0030)
#define SGMAC_XFI_DEBUG_STATS_OFFSET10                               (C_MODEL_BASE + 0x004a01a0)
#define SGMAC_XFI_TEST_OFFSET10                                      (C_MODEL_BASE + 0x004a0180)
#define SGMAC_XFI_TEST_PAT_SEED_OFFSET10                             (C_MODEL_BASE + 0x004a0190)
#define SGMAC_INTERRUPT_NORMAL_OFFSET11                              (C_MODEL_BASE + 0x004b0010)
#define SGMAC_STATS_RAM_OFFSET11                                     (C_MODEL_BASE + 0x004b0400)
#define SGMAC_CFG_OFFSET11                                           (C_MODEL_BASE + 0x004b0020)
#define SGMAC_CTL_OFFSET11                                           (C_MODEL_BASE + 0x004b01d0)
#define SGMAC_DEBUG_STATUS_OFFSET11                                  (C_MODEL_BASE + 0x004b0080)
#define SGMAC_PAUSE_CFG_OFFSET11                                     (C_MODEL_BASE + 0x004b0040)
#define SGMAC_PCS_ANEG_CFG_OFFSET11                                  (C_MODEL_BASE + 0x004b00e8)
#define SGMAC_PCS_ANEG_STATUS_OFFSET11                               (C_MODEL_BASE + 0x004b00ec)
#define SGMAC_PCS_CFG_OFFSET11                                       (C_MODEL_BASE + 0x004b00d8)
#define SGMAC_PCS_ERR_CNT_OFFSET11                                   (C_MODEL_BASE + 0x004b00dc)
#define SGMAC_PCS_STATUS_OFFSET11                                    (C_MODEL_BASE + 0x004b00e0)
#define SGMAC_PTP_STATUS_OFFSET11                                    (C_MODEL_BASE + 0x004b0090)
#define SGMAC_SOFT_RST_OFFSET11                                      (C_MODEL_BASE + 0x004b002c)
#define SGMAC_STATS_CFG_OFFSET11                                     (C_MODEL_BASE + 0x004b00c0)
#define SGMAC_STATS_INIT_OFFSET11                                    (C_MODEL_BASE + 0x004b00c8)
#define SGMAC_STATS_INIT_DONE_OFFSET11                               (C_MODEL_BASE + 0x004b00cc)
#define SGMAC_XAUI_CFG_OFFSET11                                      (C_MODEL_BASE + 0x004b0030)
#define SGMAC_XFI_DEBUG_STATS_OFFSET11                               (C_MODEL_BASE + 0x004b01a0)
#define SGMAC_XFI_TEST_OFFSET11                                      (C_MODEL_BASE + 0x004b0180)
#define SGMAC_XFI_TEST_PAT_SEED_OFFSET11                             (C_MODEL_BASE + 0x004b0190)
#define DS_PHY_PORT_OFFSET                                           (C_MODEL_BASE + 0x00801800)
#define IPE_HDR_ADJ_INTERRUPT_FATAL_OFFSET                           (C_MODEL_BASE + 0x00801cd0)
#define IPE_HDR_ADJ_INTERRUPT_NORMAL_OFFSET                          (C_MODEL_BASE + 0x00801ca0)
#define IPE_HDR_ADJ_LPBK_PD_FIFO_OFFSET                              (C_MODEL_BASE + 0x00801000)
#define IPE_HDR_ADJ_LPBK_PI_FIFO_OFFSET                              (C_MODEL_BASE + 0x00801b00)
#define IPE_HDR_ADJ_NET_PD_FIFO_OFFSET                               (C_MODEL_BASE + 0x00800000)
#define IPE_HEADER_ADJUST_PHY_PORT_MAP_OFFSET                        (C_MODEL_BASE + 0x00801a00)
#define IPE_MUX_HEADER_COS_MAP_OFFSET                                (C_MODEL_BASE + 0x00801cb0)
#define DS_PHY_PORT__REG_RAM__RAM_CHK_REC_OFFSET                     (C_MODEL_BASE + 0x00801d00)
#define IPE_HDR_ADJ_CFG_OFFSET                                       (C_MODEL_BASE + 0x00801c80)
#define IPE_HDR_ADJ_CHAN_ID_CFG_OFFSET                               (C_MODEL_BASE + 0x00801cfc)
#define IPE_HDR_ADJ_CREDIT_CTL_OFFSET                                (C_MODEL_BASE + 0x00801cf8)
#define IPE_HDR_ADJ_CREDIT_USED_OFFSET                               (C_MODEL_BASE + 0x00801d04)
#define IPE_HDR_ADJ_DEBUG_STATS_OFFSET                               (C_MODEL_BASE + 0x00801c90)
#define IPE_HDR_ADJ_ECC_CTL_OFFSET                                   (C_MODEL_BASE + 0x00801d08)
#define IPE_HDR_ADJ_ECC_STATS_OFFSET                                 (C_MODEL_BASE + 0x00801d14)
#define IPE_HDR_ADJ_LPBK_PD_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET       (C_MODEL_BASE + 0x00801d10)
#define IPE_HDR_ADJ_LPBK_PI_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET       (C_MODEL_BASE + 0x00801cf4)
#define IPE_HDR_ADJ_NET_PD_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET        (C_MODEL_BASE + 0x00801d0c)
#define IPE_HDR_ADJ_WRR_WEIGHT_OFFSET                                (C_MODEL_BASE + 0x00801d18)
#define IPE_HEADER_ADJUST_CTL_OFFSET                                 (C_MODEL_BASE + 0x00801cc0)
#define IPE_HEADER_ADJUST_EXP_MAP_OFFSET                             (C_MODEL_BASE + 0x00801ce0)
#define IPE_HEADER_ADJUST_MODE_CTL_OFFSET                            (C_MODEL_BASE + 0x00801cf0)
#define IPE_LOOPBACK_HEADER_ADJUST_CTL_OFFSET                        (C_MODEL_BASE + 0x00801ce8)
#define IPE_MUX_HEADER_ADJUST_CTL_OFFSET                             (C_MODEL_BASE + 0x00801c00)
#define IPE_PHY_PORT_MUX_CTL_OFFSET                                  (C_MODEL_BASE + 0x00801c40)
#define IPE_PORT_MAC_CTL1_OFFSET                                     (C_MODEL_BASE + 0x00801c60)
#define IPE_SGMAC_HEADER_ADJUST_CTL_OFFSET                           (C_MODEL_BASE + 0x00801cec)
#define DS_MPLS_CTL_OFFSET                                           (C_MODEL_BASE + 0x00817400)
#define DS_MPLS_RES_FIFO_OFFSET                                      (C_MODEL_BASE + 0x00812400)
#define DS_PHY_PORT_EXT_OFFSET                                       (C_MODEL_BASE + 0x00815000)
#define DS_ROUTER_MAC_OFFSET                                         (C_MODEL_BASE + 0x00816800)
#define DS_SRC_INTERFACE_OFFSET                                      (C_MODEL_BASE + 0x00810000)
#define DS_SRC_PORT_OFFSET                                           (C_MODEL_BASE + 0x00814000)
#define DS_VLAN_ACTION_PROFILE_OFFSET                                (C_MODEL_BASE + 0x00817000)
#define DS_VLAN_RANGE_PROFILE_OFFSET                                 (C_MODEL_BASE + 0x00816000)
#define HASH_DS_RES_FIFO_OFFSET                                      (C_MODEL_BASE + 0x00812000)
#define INPUT_P_I_FIFO_OFFSET                                        (C_MODEL_BASE + 0x00817800)
#define INPUT_P_R_FIFO_OFFSET                                        (C_MODEL_BASE + 0x00817a00)
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM3_OFFSET                         (C_MODEL_BASE + 0x00817c40)
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM_RESULT3_OFFSET                  (C_MODEL_BASE + 0x00817c00)
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM_RESULT4_OFFSET                  (C_MODEL_BASE + 0x00817dc0)
#define IPE_INTF_MAP_INTERRUPT_FATAL_OFFSET                          (C_MODEL_BASE + 0x00817e00)
#define IPE_INTF_MAP_INTERRUPT_NORMAL_OFFSET                         (C_MODEL_BASE + 0x00817e80)
#define IPE_INTF_MAPPER_CLA_PATH_MAP_OFFSET                          (C_MODEL_BASE + 0x00817e40)
#define TCAM_DS_RES_FIFO_OFFSET                                      (C_MODEL_BASE + 0x00812800)
#define DS_MPLS_CTL__REG_RAM__RAM_CHK_REC_OFFSET                     (C_MODEL_BASE + 0x00817f20)
#define DS_PHY_PORT_EXT__REG_RAM__RAM_CHK_REC_OFFSET                 (C_MODEL_BASE + 0x00817ee8)
#define DS_PROTOCOL_VLAN_OFFSET                                      (C_MODEL_BASE + 0x00817cc0)
#define DS_ROUTER_MAC__REG_RAM__RAM_CHK_REC_OFFSET                   (C_MODEL_BASE + 0x00817f24)
#define DS_SRC_INTERFACE__REG_RAM__RAM_CHK_REC_OFFSET                (C_MODEL_BASE + 0x00817f1c)
#define DS_SRC_PORT__REG_RAM__RAM_CHK_REC_OFFSET                     (C_MODEL_BASE + 0x00817ef0)
#define DS_VLAN_ACTION_PROFILE__REG_RAM__RAM_CHK_REC_OFFSET          (C_MODEL_BASE + 0x00817f18)
#define DS_VLAN_RANGE_PROFILE__REG_RAM__RAM_CHK_REC_OFFSET           (C_MODEL_BASE + 0x00817eec)
#define EXP_INFO_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET                  (C_MODEL_BASE + 0x00817f10)
#define IM_PKT_DATA_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET               (C_MODEL_BASE + 0x00817f14)
#define IPE_BPDU_ESCAPE_CTL_OFFSET                                   (C_MODEL_BASE + 0x00817eb0)
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM_OFFSET                          (C_MODEL_BASE + 0x00817de0)
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM2_OFFSET                         (C_MODEL_BASE + 0x00817ea0)
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM4_OFFSET                         (C_MODEL_BASE + 0x00817d80)
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM_RESULT_OFFSET                   (C_MODEL_BASE + 0x00817ec0)
#define IPE_BPDU_PROTOCOL_ESCAPE_CAM_RESULT2_OFFSET                  (C_MODEL_BASE + 0x00817ee0)
#define IPE_DS_VLAN_CTL_OFFSET                                       (C_MODEL_BASE + 0x00817f28)
#define IPE_INTF_MAP_CREDIT_CTL_OFFSET                               (C_MODEL_BASE + 0x00817e60)
#define IPE_INTF_MAP_DS_STP_STATE_STATS_OFFSET                       (C_MODEL_BASE + 0x00817f08)
#define IPE_INTF_MAP_DS_VLAN_PROF_STATS_OFFSET                       (C_MODEL_BASE + 0x00817f2c)
#define IPE_INTF_MAP_DS_VLAN_STATS_OFFSET                            (C_MODEL_BASE + 0x00817f44)
#define IPE_INTF_MAP_INFO_OUT_STATS_OFFSET                           (C_MODEL_BASE + 0x00817f40)
#define IPE_INTF_MAP_INIT_CTL_OFFSET                                 (C_MODEL_BASE + 0x00817ed0)
#define IPE_INTF_MAP_MISC_CTL_OFFSET                                 (C_MODEL_BASE + 0x00817f3c)
#define IPE_INTF_MAP_RANDOM_SEED_CTL_OFFSET                          (C_MODEL_BASE + 0x00817f30)
#define IPE_INTF_MAP_STATS1_OFFSET                                   (C_MODEL_BASE + 0x00817f34)
#define IPE_INTF_MAP_STATS2_OFFSET                                   (C_MODEL_BASE + 0x00817f38)
#define IPE_INTF_MAP_STATS3_OFFSET                                   (C_MODEL_BASE + 0x00817f4c)
#define IPE_INTF_MAP_STATS4_OFFSET                                   (C_MODEL_BASE + 0x00817efc)
#define IPE_INTF_MAPPER_CTL_OFFSET                                   (C_MODEL_BASE + 0x00817e20)
#define IPE_MPLS_CTL_OFFSET                                          (C_MODEL_BASE + 0x00817d40)
#define IPE_MPLS_EXP_MAP_OFFSET                                      (C_MODEL_BASE + 0x00817d00)
#define IPE_MPLS_TTL_THRD_OFFSET                                     (C_MODEL_BASE + 0x00817e70)
#define IPE_PORT_MAC_CTL_OFFSET                                      (C_MODEL_BASE + 0x00817e90)
#define IPE_ROUTER_MAC_CTL_OFFSET                                    (C_MODEL_BASE + 0x00817da0)
#define IPE_TUNNEL_DECAP_CTL_OFFSET                                  (C_MODEL_BASE + 0x00817ed8)
#define IPE_TUNNEL_ID_CTL_OFFSET                                     (C_MODEL_BASE + 0x00817f00)
#define IPE_USER_ID_CTL_OFFSET                                       (C_MODEL_BASE + 0x00817c80)
#define IPE_USER_ID_SGMAC_CTL_OFFSET                                 (C_MODEL_BASE + 0x00817ef8)
#define USER_ID_IM_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET                (C_MODEL_BASE + 0x00817f04)
#define USER_ID_KEY_INFO_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET          (C_MODEL_BASE + 0x00817ef4)
#define IPE_LKUP_MGR_INTERRUPT_FATAL_OFFSET                          (C_MODEL_BASE + 0x00820500)
#define IPE_LKUP_MGR_INTERRUPT_NORMAL_OFFSET                         (C_MODEL_BASE + 0x00820510)
#define IPE_LKUP_MGR_P_I_FIFO_OFFSET                                 (C_MODEL_BASE + 0x00820000)
#define IPE_LKUP_MGR_P_R_FIFO_OFFSET                                 (C_MODEL_BASE + 0x00820200)
#define IPE_EXCEPTION3_CAM_OFFSET                                    (C_MODEL_BASE + 0x008204c0)
#define IPE_EXCEPTION3_CAM2_OFFSET                                   (C_MODEL_BASE + 0x008204e0)
#define IPE_EXCEPTION3_CTL_OFFSET                                    (C_MODEL_BASE + 0x00820530)
#define IPE_IPV4_MCAST_FORCE_ROUTE_OFFSET                            (C_MODEL_BASE + 0x00820540)
#define IPE_IPV6_MCAST_FORCE_ROUTE_OFFSET                            (C_MODEL_BASE + 0x00820480)
#define IPE_LKUP_MGR_CREDIT_OFFSET                                   (C_MODEL_BASE + 0x00820550)
#define IPE_LKUP_MGR_DEBUG_STATS_OFFSET                              (C_MODEL_BASE + 0x00820520)
#define IPE_LKUP_MGR_DRAIN_ENABLE_OFFSET                             (C_MODEL_BASE + 0x00820578)
#define IPE_LKUP_MGR_FSM_STATE_OFFSET                                (C_MODEL_BASE + 0x00820570)
#define IPE_LOOKUP_CTL_OFFSET                                        (C_MODEL_BASE + 0x00820400)
#define IPE_LOOKUP_PBR_CTL_OFFSET                                    (C_MODEL_BASE + 0x00820580)
#define IPE_LOOKUP_ROUTE_CTL_OFFSET                                  (C_MODEL_BASE + 0x0082057c)
#define IPE_ROUTE_MARTIAN_ADDR_OFFSET                                (C_MODEL_BASE + 0x00820560)
#define DS_ECMP_GROUP_OFFSET                                         (C_MODEL_BASE + 0x00836000)
#define DS_ECMP_STATE_OFFSET                                         (C_MODEL_BASE + 0x00832000)
#define DS_RPF_OFFSET                                                (C_MODEL_BASE + 0x00835000)
#define DS_SRC_CHANNEL_OFFSET                                        (C_MODEL_BASE + 0x00835800)
#define DS_STORM_CTL_OFFSET                                          (C_MODEL_BASE + 0x00830000)
#define IPE_CLASSIFICATION_COS_MAP_OFFSET                            (C_MODEL_BASE + 0x00836400)
#define IPE_CLASSIFICATION_DSCP_MAP_OFFSET                           (C_MODEL_BASE + 0x00834000)
#define IPE_CLASSIFICATION_PATH_MAP_OFFSET                           (C_MODEL_BASE + 0x00836500)
#define IPE_CLASSIFICATION_PHB_OFFSET_OFFSET                         (C_MODEL_BASE + 0x008365e0)
#define IPE_CLASSIFICATION_PRECEDENCE_MAP_OFFSET                     (C_MODEL_BASE + 0x00836480)
#define IPE_LEARNING_CACHE_OFFSET                                    (C_MODEL_BASE + 0x00836200)
#define IPE_OAM_ACK_FIFO_OFFSET                                      (C_MODEL_BASE + 0x0083e000)
#define IPE_OAM_FR_IM_INFO_FIFO_OFFSET                               (C_MODEL_BASE + 0x00837800)
#define IPE_OAM_FR_LM_INFO_FIFO_OFFSET                               (C_MODEL_BASE + 0x00836800)
#define IPE_PKT_PROC_FIB_FIFO_OFFSET                                 (C_MODEL_BASE + 0x00839000)
#define IPE_PKT_PROC_HASH_DS_FIFO_OFFSET                             (C_MODEL_BASE + 0x0083a000)
#define IPE_PKT_PROC_INTERRUPT_FATAL_OFFSET                          (C_MODEL_BASE + 0x008365d0)
#define IPE_PKT_PROC_INTERRUPT_NORMAL_OFFSET                         (C_MODEL_BASE + 0x008365b0)
#define IPE_PKT_PROC_PI_FR_IM_FIFO_OFFSET                            (C_MODEL_BASE + 0x00834800)
#define IPE_PKT_PROC_PI_FR_UI_FIFO_OFFSET                            (C_MODEL_BASE + 0x00835c00)
#define IPE_PKT_PROC_PKT_INFO_FIFO_OFFSET                            (C_MODEL_BASE + 0x00833000)
#define IPE_PKT_PROC_TCAM_DS_FIFO0_OFFSET                            (C_MODEL_BASE + 0x00838000)
#define IPE_PKT_PROC_TCAM_DS_FIFO1_OFFSET                            (C_MODEL_BASE + 0x0083b000)
#define IPE_PKT_PROC_TCAM_DS_FIFO2_OFFSET                            (C_MODEL_BASE + 0x0083c000)
#define IPE_PKT_PROC_TCAM_DS_FIFO3_OFFSET                            (C_MODEL_BASE + 0x0083d000)
#define DS_ECMP_GROUP__REG_RAM__RAM_CHK_REC_OFFSET                   (C_MODEL_BASE + 0x0083665c)
#define DS_ECMP_STATE_MEM__RAM_CHK_REC_OFFSET                        (C_MODEL_BASE + 0x00836650)
#define DS_RPF__REG_RAM__RAM_CHK_REC_OFFSET                          (C_MODEL_BASE + 0x00836660)
#define DS_SRC_CHANNEL__REG_RAM__RAM_CHK_REC_OFFSET                  (C_MODEL_BASE + 0x00836698)
#define DS_STORM_MEM_PART0__RAM_CHK_REC_OFFSET                       (C_MODEL_BASE + 0x0083666c)
#define DS_STORM_MEM_PART1__RAM_CHK_REC_OFFSET                       (C_MODEL_BASE + 0x00836670)
#define IPE_ACL_APP_CAM_OFFSET                                       (C_MODEL_BASE + 0x008364c0)
#define IPE_ACL_APP_CAM_RESULT_OFFSET                                (C_MODEL_BASE + 0x00836580)
#define IPE_ACL_QOS_CTL_OFFSET                                       (C_MODEL_BASE + 0x0083667c)
#define IPE_BRIDGE_CTL_OFFSET                                        (C_MODEL_BASE + 0x00836560)
#define IPE_BRIDGE_EOP_MSG_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET        (C_MODEL_BASE + 0x00836688)
#define IPE_BRIDGE_STORM_CTL_OFFSET                                  (C_MODEL_BASE + 0x00836520)
#define IPE_CLA_EOP_MSG_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET           (C_MODEL_BASE + 0x00836678)
#define IPE_CLASSIFICATION_CTL_OFFSET                                (C_MODEL_BASE + 0x00836600)
#define IPE_CLASSIFICATION_DSCP_MAP__REG_RAM__RAM_CHK_REC_OFFSET     (C_MODEL_BASE + 0x00836674)
#define IPE_DS_FWD_CTL_OFFSET                                        (C_MODEL_BASE + 0x00836668)
#define IPE_ECMP_CHANNEL_STATE_OFFSET                                (C_MODEL_BASE + 0x00836540)
#define IPE_ECMP_CTL_OFFSET                                          (C_MODEL_BASE + 0x00836680)
#define IPE_ECMP_TIMER_CTL_OFFSET                                    (C_MODEL_BASE + 0x008365a0)
#define IPE_FCOE_CTL_OFFSET                                          (C_MODEL_BASE + 0x00836684)
#define IPE_IPG_CTL_OFFSET                                           (C_MODEL_BASE + 0x008365c0)
#define IPE_LEARNING_CACHE_VALID_OFFSET                              (C_MODEL_BASE + 0x00836694)
#define IPE_LEARNING_CTL_OFFSET                                      (C_MODEL_BASE + 0x00836690)
#define IPE_OAM_CTL_OFFSET                                           (C_MODEL_BASE + 0x00837660)
#define IPE_OUTER_LEARNING_CTL_OFFSET                                (C_MODEL_BASE + 0x0083668c)
#define IPE_PKT_PROC_CFG_OFFSET                                      (C_MODEL_BASE + 0x0083669c)
#define IPE_PKT_PROC_CREDIT_CTL_OFFSET                               (C_MODEL_BASE + 0x00836620)
#define IPE_PKT_PROC_CREDIT_USED_OFFSET                              (C_MODEL_BASE + 0x00836630)
#define IPE_PKT_PROC_DEBUG_STATS_OFFSET                              (C_MODEL_BASE + 0x00836610)
#define IPE_PKT_PROC_DS_FWD_PTR_DEBUG_OFFSET                         (C_MODEL_BASE + 0x0083c080)
#define IPE_PKT_PROC_ECC_CTL_OFFSET                                  (C_MODEL_BASE + 0x0083664c)
#define IPE_PKT_PROC_ECC_STATS_OFFSET                                (C_MODEL_BASE + 0x00836640)
#define IPE_PKT_PROC_EOP_MSG_FIFO_DEPTH_RECORD_OFFSET                (C_MODEL_BASE + 0x00836648)
#define IPE_PKT_PROC_INIT_CTL_OFFSET                                 (C_MODEL_BASE + 0x00836628)
#define IPE_PKT_PROC_RAND_SEED_LOAD_OFFSET                           (C_MODEL_BASE + 0x008365f0)
#define IPE_PTP_CTL_OFFSET                                           (C_MODEL_BASE + 0x00836658)
#define IPE_ROUTE_CTL_OFFSET                                         (C_MODEL_BASE + 0x00836638)
#define IPE_TRILL_CTL_OFFSET                                         (C_MODEL_BASE + 0x00836654)
#define DS_APS_SELECT_OFFSET                                         (C_MODEL_BASE + 0x00841c80)
#define DS_QCN_OFFSET                                                (C_MODEL_BASE + 0x00841000)
#define IPE_FWD_DISCARD_TYPE_STATS_OFFSET                            (C_MODEL_BASE + 0x00841b00)
#define IPE_FWD_HDR_ADJ_INFO_FIFO_OFFSET                             (C_MODEL_BASE + 0x00840000)
#define IPE_FWD_INTERRUPT_FATAL_OFFSET                               (C_MODEL_BASE + 0x00841d80)
#define IPE_FWD_INTERRUPT_NORMAL_OFFSET                              (C_MODEL_BASE + 0x00841d90)
#define IPE_FWD_LEARN_THROUGH_FIFO_OFFSET                            (C_MODEL_BASE + 0x00841e00)
#define IPE_FWD_LEARN_TRACK_FIFO_OFFSET                              (C_MODEL_BASE + 0x00841c00)
#define IPE_FWD_OAM_FIFO_OFFSET                                      (C_MODEL_BASE + 0x00841800)
#define IPE_FWD_QCN_MAP_TABLE_OFFSET                                 (C_MODEL_BASE + 0x00841a00)
#define IPE_FWD_ROUTE_PI_FIFO_OFFSET                                 (C_MODEL_BASE + 0x00841400)
#define DS_APS_BRIDGE_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET       (C_MODEL_BASE + 0x00841dc4)
#define IPE_FWD_BRIDGE_HDR_REC_OFFSET                                (C_MODEL_BASE + 0x00841de0)
#define IPE_FWD_CFG_OFFSET                                           (C_MODEL_BASE + 0x00841dbc)
#define IPE_FWD_CTL_OFFSET                                           (C_MODEL_BASE + 0x00841d00)
#define IPE_FWD_CTL1_OFFSET                                          (C_MODEL_BASE + 0x00841d40)
#define IPE_FWD_DEBUG_STATS_OFFSET                                   (C_MODEL_BASE + 0x00841db0)
#define IPE_FWD_DS_QCN_RAM__RAM_CHK_REC_OFFSET                       (C_MODEL_BASE + 0x00841dc0)
#define IPE_FWD_EOP_MSG_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET           (C_MODEL_BASE + 0x00841dc8)
#define IPE_FWD_EXCP_REC_OFFSET                                      (C_MODEL_BASE + 0x00841da8)
#define IPE_FWD_HDR_ADJ_INFO_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET      (C_MODEL_BASE + 0x00841dcc)
#define IPE_FWD_QCN_CTL_OFFSET                                       (C_MODEL_BASE + 0x00841da0)
#define IPE_FWD_SOP_MSG_RAM__RAM_CHK_REC_OFFSET                      (C_MODEL_BASE + 0x00841db8)
#define IPE_PRIORITY_TO_STATS_COS_OFFSET                             (C_MODEL_BASE + 0x00841d60)
#define EPE_HDR_ADJ_BRG_HDR_FIFO_OFFSET                              (C_MODEL_BASE + 0x00881800)
#define EPE_HDR_ADJ_DATA_FIFO_OFFSET                                 (C_MODEL_BASE + 0x00881000)
#define EPE_HDR_ADJ_INTERRUPT_FATAL_OFFSET                           (C_MODEL_BASE + 0x00880940)
#define EPE_HDR_ADJ_INTERRUPT_NORMAL_OFFSET                          (C_MODEL_BASE + 0x00880950)
#define EPE_HEADER_ADJUST_PHY_PORT_MAP_OFFSET                        (C_MODEL_BASE + 0x00880800)
#define EPE_HDR_ADJ_BRG_HDR_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET       (C_MODEL_BASE + 0x008809b4)
#define EPE_HDR_ADJ_CREDIT_CONFIG_OFFSET                             (C_MODEL_BASE + 0x00880970)
#define EPE_HDR_ADJ_DATA_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET          (C_MODEL_BASE + 0x008809a8)
#define EPE_HDR_ADJ_DEBUG_CTL_OFFSET                                 (C_MODEL_BASE + 0x008809b0)
#define EPE_HDR_ADJ_DEBUG_STATS_OFFSET                               (C_MODEL_BASE + 0x00880920)
#define EPE_HDR_ADJ_DISABLE_CRC_CHK_OFFSET                           (C_MODEL_BASE + 0x00880980)
#define EPE_HDR_ADJ_DRAIN_ENABLE_OFFSET                              (C_MODEL_BASE + 0x008809a4)
#define EPE_HDR_ADJ_INIT_OFFSET                                      (C_MODEL_BASE + 0x008809b8)
#define EPE_HDR_ADJ_INIT_DONE_OFFSET                                 (C_MODEL_BASE + 0x008809a0)
#define EPE_HDR_ADJ_PARITY_ENABLE_OFFSET                             (C_MODEL_BASE + 0x008809bc)
#define EPE_HDR_ADJ_RCV_UNIT_CNT_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET  (C_MODEL_BASE + 0x008809ac)
#define EPE_HDR_ADJ_RUNNING_CREDIT_OFFSET                            (C_MODEL_BASE + 0x00880990)
#define EPE_HDR_ADJ_STALL_INFO_OFFSET                                (C_MODEL_BASE + 0x00880998)
#define EPE_HDR_ADJUST_CTL_OFFSET                                    (C_MODEL_BASE + 0x00880900)
#define EPE_HDR_ADJUST_MISC_CTL_OFFSET                               (C_MODEL_BASE + 0x00880994)
#define EPE_NEXT_HOP_PTR_CAM_OFFSET                                  (C_MODEL_BASE + 0x00880960)
#define EPE_PORT_EXTENDER_DOWNLINK_OFFSET                            (C_MODEL_BASE + 0x00880988)
#define DS_DEST_CHANNEL_OFFSET                                       (C_MODEL_BASE + 0x00897000)
#define DS_DEST_INTERFACE_OFFSET                                     (C_MODEL_BASE + 0x00892000)
#define DS_DEST_PORT_OFFSET                                          (C_MODEL_BASE + 0x00894000)
#define DS_EGRESS_VLAN_RANGE_PROFILE_OFFSET                          (C_MODEL_BASE + 0x00896000)
#define EPE_EDIT_PRIORITY_MAP_OFFSET                                 (C_MODEL_BASE + 0x00890000)
#define EPE_NEXT_HOP_INTERNAL_OFFSET                                 (C_MODEL_BASE + 0x008972c0)
#define EPE_NEXT_HOP_INTERRUPT_FATAL_OFFSET                          (C_MODEL_BASE + 0x00897300)
#define EPE_NEXT_HOP_INTERRUPT_NORMAL_OFFSET                         (C_MODEL_BASE + 0x00897310)
#define NEXT_HOP_DS_VLAN_RANGE_PROF_FIFO_OFFSET                      (C_MODEL_BASE + 0x00897200)
#define NEXT_HOP_HASH_ACK_FIFO_OFFSET                                (C_MODEL_BASE + 0x00897800)
#define NEXT_HOP_PI_FIFO_OFFSET                                      (C_MODEL_BASE + 0x00896800)
#define NEXT_HOP_PR_FIFO_OFFSET                                      (C_MODEL_BASE + 0x00895000)
#define NEXT_HOP_PR_KEY_FIFO_OFFSET                                  (C_MODEL_BASE + 0x00897280)
#define DS_DEST_CHANNEL__REG_RAM__RAM_CHK_REC_OFFSET                 (C_MODEL_BASE + 0x00897348)
#define DS_DEST_INTERFACE__REG_RAM__RAM_CHK_REC_OFFSET               (C_MODEL_BASE + 0x00897344)
#define DS_DEST_PORT__REG_RAM__RAM_CHK_REC_OFFSET                    (C_MODEL_BASE + 0x00897340)
#define DS_EGRESS_VLAN_RANGE_PROFILE__REG_RAM__RAM_CHK_REC_OFFSET    (C_MODEL_BASE + 0x0089734c)
#define EPE_EDIT_PRIORITY_MAP__REG_RAM__RAM_CHK_REC_OFFSET           (C_MODEL_BASE + 0x00897358)
#define EPE_NEXT_HOP_CREDIT_CTL_OFFSET                               (C_MODEL_BASE + 0x00897354)
#define EPE_NEXT_HOP_CTL_OFFSET                                      (C_MODEL_BASE + 0x00897320)
#define EPE_NEXT_HOP_DEBUG_STATS_OFFSET                              (C_MODEL_BASE + 0x00897330)
#define EPE_NEXT_HOP_DEBUG_STATUS_OFFSET                             (C_MODEL_BASE + 0x0089735c)
#define EPE_NEXT_HOP_MISC_CTL_OFFSET                                 (C_MODEL_BASE + 0x00897350)
#define EPE_NEXT_HOP_RANDOM_VALUE_GEN_OFFSET                         (C_MODEL_BASE + 0x00897360)
#define DS_IPV6_NAT_PREFIX_OFFSET                                    (C_MODEL_BASE + 0x008a0000)
#define DS_L3_TUNNEL_V4_IP_SA_OFFSET                                 (C_MODEL_BASE + 0x008a1380)
#define DS_L3_TUNNEL_V6_IP_SA_OFFSET                                 (C_MODEL_BASE + 0x008a1300)
#define DS_PORT_LINK_AGG_OFFSET                                      (C_MODEL_BASE + 0x008a1000)
#define EPE_H_P_HDR_ADJ_PI_FIFO_OFFSET                               (C_MODEL_BASE + 0x008a2200)
#define EPE_H_P_L2_EDIT_FIFO_OFFSET                                  (C_MODEL_BASE + 0x008a1500)
#define EPE_H_P_L3_EDIT_FIFO_OFFSET                                  (C_MODEL_BASE + 0x008a1600)
#define EPE_H_P_NEXT_HOP_PR_FIFO_OFFSET                              (C_MODEL_BASE + 0x008a2000)
#define EPE_H_P_PAYLOAD_INFO_FIFO_OFFSET                             (C_MODEL_BASE + 0x008a1800)
#define EPE_HDR_PROC_INTERRUPT_FATAL_OFFSET                          (C_MODEL_BASE + 0x008a1400)
#define EPE_HDR_PROC_INTERRUPT_NORMAL_OFFSET                         (C_MODEL_BASE + 0x008a1420)
#define EPE_HDR_PROC_PHY_PORT_MAP_OFFSET                             (C_MODEL_BASE + 0x008a1200)
#define DS_IPV6_NAT_PREFIX__REG_RAM__RAM_CHK_REC_OFFSET              (C_MODEL_BASE + 0x008a1460)
#define EPE_HDR_PROC_DEBUG_STATS_OFFSET                              (C_MODEL_BASE + 0x008a1468)
#define EPE_HDR_PROC_FLOW_CTL_OFFSET                                 (C_MODEL_BASE + 0x008a1430)
#define EPE_HDR_PROC_FRAG_CTL_OFFSET                                 (C_MODEL_BASE + 0x008a2400)
#define EPE_HDR_PROC_INIT_OFFSET                                     (C_MODEL_BASE + 0x008a147c)
#define EPE_HDR_PROC_PARITY_EN_OFFSET                                (C_MODEL_BASE + 0x008a146c)
#define EPE_L2_EDIT_CTL_OFFSET                                       (C_MODEL_BASE + 0x008a1458)
#define EPE_L2_ETHER_TYPE_OFFSET                                     (C_MODEL_BASE + 0x008a1470)
#define EPE_L2_PORT_MAC_SA_OFFSET                                    (C_MODEL_BASE + 0x008a13e0)
#define EPE_L2_ROUTER_MAC_SA_OFFSET                                  (C_MODEL_BASE + 0x008a13c0)
#define EPE_L2_SNAP_CTL_OFFSET                                       (C_MODEL_BASE + 0x008a1478)
#define EPE_L2_TPID_CTL_OFFSET                                       (C_MODEL_BASE + 0x008a1440)
#define EPE_L3_EDIT_MPLS_TTL_OFFSET                                  (C_MODEL_BASE + 0x008a13f0)
#define EPE_L3_IP_IDENTIFICATION_OFFSET                              (C_MODEL_BASE + 0x008a1474)
#define EPE_MIRROR_ESCAPE_CAM_OFFSET                                 (C_MODEL_BASE + 0x008a13a0)
#define EPE_PBB_CTL_OFFSET                                           (C_MODEL_BASE + 0x008a1450)
#define EPE_PKT_PROC_CTL_OFFSET                                      (C_MODEL_BASE + 0x008a1410)
#define EPE_PKT_PROC_MUX_CTL_OFFSET                                  (C_MODEL_BASE + 0x008a1464)
#define EPE_ACL_INFO_FIFO_OFFSET                                     (C_MODEL_BASE + 0x008b0000)
#define EPE_ACL_QOS_INTERRUPT_FATAL_OFFSET                           (C_MODEL_BASE + 0x008b0250)
#define EPE_ACL_QOS_INTERRUPT_NORMAL_OFFSET                          (C_MODEL_BASE + 0x008b0260)
#define EPE_ACL_TCAM_ACK_FIFO_OFFSET                                 (C_MODEL_BASE + 0x008b0300)
#define EPE_ACL_QOS_CTL_OFFSET                                       (C_MODEL_BASE + 0x008b0200)
#define EPE_ACL_QOS_DEBUG_STATS_OFFSET                               (C_MODEL_BASE + 0x008b0270)
#define EPE_ACL_QOS_DRAIN_CTL_OFFSET                                 (C_MODEL_BASE + 0x008b0280)
#define EPE_ACL_QOS_LFSR_OFFSET                                      (C_MODEL_BASE + 0x008b0240)
#define EPE_ACL_QOS_MISC_CTL_OFFSET                                  (C_MODEL_BASE + 0x008b0220)
#define TO_CLA_CREDIT_DEBUG_OFFSET                                   (C_MODEL_BASE + 0x008b0230)
#define EPE_CLA_EOP_INFO_FIFO_OFFSET                                 (C_MODEL_BASE + 0x008c0400)
#define EPE_CLA_INTERRUPT_FATAL_OFFSET                               (C_MODEL_BASE + 0x008c0750)
#define EPE_CLA_INTERRUPT_NORMAL_OFFSET                              (C_MODEL_BASE + 0x008c0740)
#define EPE_CLA_POLICER_ACK_FIFO_OFFSET                              (C_MODEL_BASE + 0x008c0700)
#define EPE_CLA_POLICER_TRACK_FIFO_OFFSET                            (C_MODEL_BASE + 0x008c0000)
#define EPE_CLA_POLICING_INFO_FIFO_OFFSET                            (C_MODEL_BASE + 0x008c0600)
#define EPE_CLA_PTR_RAM_OFFSET                                       (C_MODEL_BASE + 0x008c0200)
#define EPE_CLA_CREDIT_CTL_OFFSET                                    (C_MODEL_BASE + 0x008c0790)
#define EPE_CLA_DEBUG_STATS_OFFSET                                   (C_MODEL_BASE + 0x008c07a0)
#define EPE_CLA_EOP_INFO_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET          (C_MODEL_BASE + 0x008c07a4)
#define EPE_CLA_INIT_CTL_OFFSET                                      (C_MODEL_BASE + 0x008c07a8)
#define EPE_CLA_MISC_CTL_OFFSET                                      (C_MODEL_BASE + 0x008c079c)
#define EPE_CLA_PTR_RAM__REG_RAM__RAM_CHK_REC_OFFSET                 (C_MODEL_BASE + 0x008c0798)
#define EPE_CLASSIFICATION_CTL_OFFSET                                (C_MODEL_BASE + 0x008c0780)
#define EPE_CLASSIFICATION_PHB_OFFSET_OFFSET                         (C_MODEL_BASE + 0x008c0770)
#define EPE_IPG_CTL_OFFSET                                           (C_MODEL_BASE + 0x008c0760)
#define EPE_OAM_HASH_DS_FIFO_OFFSET                                  (C_MODEL_BASE + 0x008d0800)
#define EPE_OAM_HDR_PROC_INFO_FIFO_OFFSET                            (C_MODEL_BASE + 0x008d0000)
#define EPE_OAM_INFO_FIFO_OFFSET                                     (C_MODEL_BASE + 0x008d0900)
#define EPE_OAM_INTERRUPT_FATAL_OFFSET                               (C_MODEL_BASE + 0x008d0980)
#define EPE_OAM_INTERRUPT_NORMAL_OFFSET                              (C_MODEL_BASE + 0x008d0990)
#define EPE_OAM_CONFIG_OFFSET                                        (C_MODEL_BASE + 0x008d09a8)
#define EPE_OAM_CREDIT_DEBUG_OFFSET                                  (C_MODEL_BASE + 0x008d09a0)
#define EPE_OAM_CTL_OFFSET                                           (C_MODEL_BASE + 0x008d09c0)
#define EPE_OAM_DEBUG_STATS_OFFSET                                   (C_MODEL_BASE + 0x008d09a4)
#define DS_DEST_PORT_LOOPBACK_OFFSET                                 (C_MODEL_BASE + 0x008e2e80)
#define DS_PACKET_HEADER_EDIT_TUNNEL_OFFSET                          (C_MODEL_BASE + 0x008e1800)
#define EPE_HDR_EDIT_DISCARD_TYPE_STATS_OFFSET                       (C_MODEL_BASE + 0x008e2d00)
#define EPE_HDR_EDIT_INGRESS_HDR_FIFO_OFFSET                         (C_MODEL_BASE + 0x008e1000)
#define EPE_HDR_EDIT_INTERRUPT_FATAL_OFFSET                          (C_MODEL_BASE + 0x008e2ee0)
#define EPE_HDR_EDIT_INTERRUPT_NORMAL_OFFSET                         (C_MODEL_BASE + 0x008e2f20)
#define EPE_HDR_EDIT_NEW_HDR_FIFO_OFFSET                             (C_MODEL_BASE + 0x008e2c00)
#define EPE_HDR_EDIT_OUTER_HDR_DATA_FIFO_OFFSET                      (C_MODEL_BASE + 0x008e2800)
#define EPE_HDR_EDIT_PKT_CTL_FIFO_OFFSET                             (C_MODEL_BASE + 0x008e2900)
#define EPE_HDR_EDIT_PKT_DATA_FIFO_OFFSET                            (C_MODEL_BASE + 0x008e0000)
#define EPE_HDR_EDIT_PKT_INFO_HDR_PROC_FIFO_OFFSET                   (C_MODEL_BASE + 0x008e2000)
#define EPE_HDR_EDIT_PKT_INFO_OAM_FIFO_OFFSET                        (C_MODEL_BASE + 0x008e2a00)
#define EPE_HDR_EDIT_PKT_INFO_OUTER_HDR_FIFO_OFFSET                  (C_MODEL_BASE + 0x008e2e00)
#define EPE_HEADER_EDIT_PHY_PORT_MAP_OFFSET                          (C_MODEL_BASE + 0x008e2b00)
#define DS_PACKET_HEADER_EDIT_TUNNEL__REG_RAM__RAM_CHK_REC_OFFSET    (C_MODEL_BASE + 0x008e2f50)
#define EPE_HDR_EDIT_DATA_CMPT_EOP_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET (C_MODEL_BASE + 0x008e2f48)
#define EPE_HDR_EDIT_DATA_CMPT_SOP_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET (C_MODEL_BASE + 0x008e2f44)
#define EPE_HDR_EDIT_DEBUG_STATS_OFFSET                              (C_MODEL_BASE + 0x008e2f30)
#define EPE_HDR_EDIT_DRAIN_ENABLE_OFFSET                             (C_MODEL_BASE + 0x008e2f88)
#define EPE_HDR_EDIT_ECC_CTL_OFFSET                                  (C_MODEL_BASE + 0x008e2f70)
#define EPE_HDR_EDIT_EXCEP_INFO_OFFSET                               (C_MODEL_BASE + 0x008e2f80)
#define EPE_HDR_EDIT_HARD_DISCARD_EN_OFFSET                          (C_MODEL_BASE + 0x008e2f74)
#define EPE_HDR_EDIT_INIT_OFFSET                                     (C_MODEL_BASE + 0x008e2f6c)
#define EPE_HDR_EDIT_INIT_DONE_OFFSET                                (C_MODEL_BASE + 0x008e2f64)
#define EPE_HDR_EDIT_L2_LOOP_BACK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET (C_MODEL_BASE + 0x008e2f68)
#define EPE_HDR_EDIT_NEW_HDR_FIFO__FIFO_ALMOST_EMPTY_THRD_OFFSET     (C_MODEL_BASE + 0x008e2f78)
#define EPE_HDR_EDIT_NEW_HDR_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET      (C_MODEL_BASE + 0x008e2f7c)
#define EPE_HDR_EDIT_OUTER_HDR_DATA_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET (C_MODEL_BASE + 0x008e2f8c)
#define EPE_HDR_EDIT_PARITY_EN_OFFSET                                (C_MODEL_BASE + 0x008e2f84)
#define EPE_HDR_EDIT_PKT_CTL_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET      (C_MODEL_BASE + 0x008e2f60)
#define EPE_HDR_EDIT_PKT_DATA_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET     (C_MODEL_BASE + 0x008e2f90)
#define EPE_HDR_EDIT_PKT_INFO_OUTER_HDR_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET (C_MODEL_BASE + 0x008e2f40)
#define EPE_HDR_EDIT_RESERVED_CREDIT_CNT_OFFSET                      (C_MODEL_BASE + 0x008e2f5c)
#define EPE_HDR_EDIT_STATE_OFFSET                                    (C_MODEL_BASE + 0x008e2f58)
#define EPE_HDR_EDIT_TO_NET_TX_BRG_HDR_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET (C_MODEL_BASE + 0x008e2f4c)
#define EPE_HEADER_EDIT_CTL_OFFSET                                   (C_MODEL_BASE + 0x008e2ec0)
#define EPE_HEADER_EDIT_MUX_CTL_OFFSET                               (C_MODEL_BASE + 0x008e2f54)
#define EPE_PKT_HDR_CTL_OFFSET                                       (C_MODEL_BASE + 0x008e2e40)
#define EPE_PRIORITY_TO_STATS_COS_OFFSET                             (C_MODEL_BASE + 0x008e2f00)
#define INT_LK_INTERRUPT_FATAL_OFFSET                                (C_MODEL_BASE + 0x00500000)
#define INT_LK_RX_STATS_MEM_OFFSET                                   (C_MODEL_BASE + 0x00508800)
#define INT_LK_TX_STATS_MEM_OFFSET                                   (C_MODEL_BASE + 0x00508000)
#define INT_LK_BURST_CTL_OFFSET                                      (C_MODEL_BASE + 0x00500040)
#define INT_LK_CAL_PTR_CTL_OFFSET                                    (C_MODEL_BASE + 0x00500044)
#define INT_LK_CREDIT_CTL_OFFSET                                     (C_MODEL_BASE + 0x00500210)
#define INT_LK_CREDIT_DEBUG_OFFSET                                   (C_MODEL_BASE + 0x00500214)
#define INT_LK_FC_CHAN_CTL_OFFSET                                    (C_MODEL_BASE + 0x005001c0)
#define INT_LK_FIFO_THD_CTL_OFFSET                                   (C_MODEL_BASE + 0x00500080)
#define INT_LK_FL_CTL_CAL_OFFSET                                     (C_MODEL_BASE + 0x00500180)
#define INT_LK_IN_DATA_MEM_OVER_FLOW_INFO_OFFSET                     (C_MODEL_BASE + 0x005000dc)
#define INT_LK_IN_DATA_MEM__RAM_CHK_REC_OFFSET                       (C_MODEL_BASE + 0x00500058)
#define INT_LK_LANE_RX_CTL_OFFSET                                    (C_MODEL_BASE + 0x005000e0)
#define INT_LK_LANE_RX_DEBUG_STATE_OFFSET                            (C_MODEL_BASE + 0x00500100)
#define INT_LK_LANE_RX_STATS_OFFSET                                  (C_MODEL_BASE + 0x00500140)
#define INT_LK_LANE_SWAP_EN_OFFSET                                   (C_MODEL_BASE + 0x0050033c)
#define INT_LK_LANE_TX_CTL_OFFSET                                    (C_MODEL_BASE + 0x005000b0)
#define INT_LK_LANE_TX_STATE_OFFSET                                  (C_MODEL_BASE + 0x00500090)
#define INT_LK_MEM_INIT_CTL_OFFSET                                   (C_MODEL_BASE + 0x005000d0)
#define INT_LK_META_FRAME_CTL_OFFSET                                 (C_MODEL_BASE + 0x0050004c)
#define INT_LK_MISC_CTL_OFFSET                                       (C_MODEL_BASE + 0x00500050)
#define INT_LK_PARITY_CTL_OFFSET                                     (C_MODEL_BASE + 0x0050007c)
#define INT_LK_PKT_CRC_CTL_OFFSET                                    (C_MODEL_BASE + 0x00500200)
#define INT_LK_PKT_PROC_OUT_BUF_MEM__RAM_CHK_REC_OFFSET              (C_MODEL_BASE + 0x0050005c)
#define INT_LK_PKT_PROC_RES_WORD_MEM__RAM_CHK_REC_OFFSET             (C_MODEL_BASE + 0x00500310)
#define INT_LK_PKT_PROC_STALL_INFO_OFFSET                            (C_MODEL_BASE + 0x00500220)
#define INT_LK_PKT_STATS_OFFSET                                      (C_MODEL_BASE + 0x005001e0)
#define INT_LK_RATE_MATCH_CTL_OFFSET                                 (C_MODEL_BASE + 0x005000a0)
#define INT_LK_RX_ALIGN_CTL_OFFSET                                   (C_MODEL_BASE + 0x00500300)
#define INT_LK_SOFT_RESET_OFFSET                                     (C_MODEL_BASE + 0x00500060)
#define INT_LK_TEST_CTL_OFFSET                                       (C_MODEL_BASE + 0x00500120)
#define DS_CHANNELIZE_ING_FC_OFFSET                                  (C_MODEL_BASE + 0x00512800)
#define DS_CHANNELIZE_MODE_OFFSET                                    (C_MODEL_BASE + 0x00512200)
#define NET_RX_CHAN_INFO_RAM_OFFSET                                  (C_MODEL_BASE + 0x00510200)
#define NET_RX_CHANNEL_MAP_OFFSET                                    (C_MODEL_BASE + 0x00512a00)
#define NET_RX_INTERRUPT_FATAL_OFFSET                                (C_MODEL_BASE + 0x00510000)
#define NET_RX_INTERRUPT_NORMAL_OFFSET                               (C_MODEL_BASE + 0x00510010)
#define NET_RX_LINK_LIST_TABLE_OFFSET                                (C_MODEL_BASE + 0x00510400)
#define NET_RX_PAUSE_TIMER_MEM_OFFSET                                (C_MODEL_BASE + 0x00511000)
#define NET_RX_PKT_BUF_RAM_OFFSET                                    (C_MODEL_BASE + 0x00518000)
#define NET_RX_PKT_INFO_FIFO_OFFSET                                  (C_MODEL_BASE + 0x00510800)
#define DS_CHANNELIZE_ING_FC__REG_RAM__RAM_CHK_REC_OFFSET            (C_MODEL_BASE + 0x00510088)
#define DS_CHANNELIZE_MODE__REG_RAM__RAM_CHK_REC_OFFSET              (C_MODEL_BASE + 0x00510084)
#define NET_RX_BPDU_STATS_OFFSET                                     (C_MODEL_BASE + 0x005100a0)
#define NET_RX_BUFFER_COUNT_OFFSET                                   (C_MODEL_BASE + 0x0051002c)
#define NET_RX_BUFFER_FULL_THRESHOLD_OFFSET                          (C_MODEL_BASE + 0x00510034)
#define NET_RX_CTL_OFFSET                                            (C_MODEL_BASE + 0x005100a4)
#define NET_RX_DEBUG_STATS_OFFSET                                    (C_MODEL_BASE + 0x00510050)
#define NET_RX_DRAIN_ENABLE_OFFSET                                   (C_MODEL_BASE + 0x005100a8)
#define NET_RX_ECC_CTL_OFFSET                                        (C_MODEL_BASE + 0x00510038)
#define NET_RX_ECC_ERROR_STATS_OFFSET                                (C_MODEL_BASE + 0x00510040)
#define NET_RX_FREE_LIST_CTL_OFFSET                                  (C_MODEL_BASE + 0x00510028)
#define NET_RX_INT_LK_PKT_SIZE_CHECK_OFFSET                          (C_MODEL_BASE + 0x005100b0)
#define NET_RX_INTLK_CHAN_EN_OFFSET                                  (C_MODEL_BASE + 0x0051008c)
#define NET_RX_IPE_STALL_RECORD_OFFSET                               (C_MODEL_BASE + 0x0051003c)
#define NET_RX_MEM_INIT_OFFSET                                       (C_MODEL_BASE + 0x00510020)
#define NET_RX_PARITY_FAIL_RECORD_OFFSET                             (C_MODEL_BASE + 0x00510060)
#define NET_RX_PAUSE_CTL_OFFSET                                      (C_MODEL_BASE + 0x00510048)
#define NET_RX_PAUSE_TYPE_OFFSET                                     (C_MODEL_BASE + 0x00510090)
#define NET_RX_PRE_FETCH_FIFO_DEPTH_OFFSET                           (C_MODEL_BASE + 0x005100ac)
#define NET_RX_RESERVED_MAC_DA_CTL_OFFSET                            (C_MODEL_BASE + 0x00510070)
#define NET_RX_STATE_OFFSET                                          (C_MODEL_BASE + 0x0051004c)
#define NET_RX_STATS_EN_OFFSET                                       (C_MODEL_BASE + 0x00510098)
#define NET_RX_WRONG_PRIORITY_RECORD_OFFSET                          (C_MODEL_BASE + 0x00510080)
#define E_LOOP_INTERRUPT_FATAL_OFFSET                                (C_MODEL_BASE + 0x00520000)
#define E_LOOP_INTERRUPT_NORMAL_OFFSET                               (C_MODEL_BASE + 0x00520010)
#define E_LOOP_CREDIT_STATE_OFFSET                                   (C_MODEL_BASE + 0x00520040)
#define E_LOOP_CTL_OFFSET                                            (C_MODEL_BASE + 0x00520080)
#define E_LOOP_DEBUG_STATS_OFFSET                                    (C_MODEL_BASE + 0x00520030)
#define E_LOOP_DRAIN_ENABLE_OFFSET                                   (C_MODEL_BASE + 0x00520020)
#define E_LOOP_LOOP_HDR_INFO_OFFSET                                  (C_MODEL_BASE + 0x00520060)
#define E_LOOP_PARITY_EN_OFFSET                                      (C_MODEL_BASE + 0x00520070)
#define E_LOOP_PARITY_FAIL_RECORD_OFFSET                             (C_MODEL_BASE + 0x00520050)
#define MAC_MUX_INTERRUPT_FATAL_OFFSET                               (C_MODEL_BASE + 0x00530000)
#define MAC_MUX_INTERRUPT_NORMAL_OFFSET                              (C_MODEL_BASE + 0x00530010)
#define MAC_MUX_CAL_OFFSET                                           (C_MODEL_BASE + 0x00530030)
#define MAC_MUX_DEBUG_STATS_OFFSET                                   (C_MODEL_BASE + 0x00530028)
#define MAC_MUX_PARITY_ENABLE_OFFSET                                 (C_MODEL_BASE + 0x00530044)
#define MAC_MUX_STAT_SEL_OFFSET                                      (C_MODEL_BASE + 0x00530020)
#define MAC_MUX_WALKER_OFFSET                                        (C_MODEL_BASE + 0x0053002c)
#define MAC_MUX_WRR_CFG0_OFFSET                                      (C_MODEL_BASE + 0x00530048)
#define MAC_MUX_WRR_CFG1_OFFSET                                      (C_MODEL_BASE + 0x0053004c)
#define MAC_MUX_WRR_CFG2_OFFSET                                      (C_MODEL_BASE + 0x00530050)
#define MAC_MUX_WRR_CFG3_OFFSET                                      (C_MODEL_BASE + 0x00530054)
#define PTP_CAPTURED_ADJ_FRC_OFFSET                                  (C_MODEL_BASE + 0x00540050)
#define PTP_ENGINE_INTERRUPT_FATAL_OFFSET                            (C_MODEL_BASE + 0x005400a0)
#define PTP_ENGINE_INTERRUPT_NORMAL_OFFSET                           (C_MODEL_BASE + 0x005400b0)
#define PTP_MAC_TX_CAPTURE_TS_OFFSET                                 (C_MODEL_BASE + 0x00540000)
#define PTP_SYNC_INTF_HALF_PERIOD_OFFSET                             (C_MODEL_BASE + 0x005400d8)
#define PTP_SYNC_INTF_TOGGLE_OFFSET                                  (C_MODEL_BASE + 0x00540040)
#define PTP_TOD_TOW_OFFSET                                           (C_MODEL_BASE + 0x005400c8)
#define PTP_ENGINE_FIFO_DEPTH_RECORD_OFFSET                          (C_MODEL_BASE + 0x005400e0)
#define PTP_FRC_QUANTA_OFFSET                                        (C_MODEL_BASE + 0x005400f4)
#define PTP_MAC_TX_CAPTURE_TS_CTRL_OFFSET                            (C_MODEL_BASE + 0x005400f0)
#define PTP_PARITY_EN_OFFSET                                         (C_MODEL_BASE + 0x005400ec)
#define PTP_SYNC_INTF_CFG_OFFSET                                     (C_MODEL_BASE + 0x00540080)
#define PTP_SYNC_INTF_INPUT_CODE_OFFSET                              (C_MODEL_BASE + 0x00540090)
#define PTP_SYNC_INTF_INPUT_TS_OFFSET                                (C_MODEL_BASE + 0x005400d0)
#define PTP_TOD_CFG_OFFSET                                           (C_MODEL_BASE + 0x00540060)
#define PTP_TOD_CODE_CFG_OFFSET                                      (C_MODEL_BASE + 0x00540070)
#define PTP_TOD_INPUT_CODE_OFFSET                                    (C_MODEL_BASE + 0x00540020)
#define PTP_TOD_INPUT_TS_OFFSET                                      (C_MODEL_BASE + 0x005400c0)
#define PTP_TS_CAPTURE_CTRL_OFFSET                                   (C_MODEL_BASE + 0x005400e8)
#define PTP_TS_CAPTURE_DROP_CNT_OFFSET                               (C_MODEL_BASE + 0x005400e4)
#define PTP_DRIFT_RATE_ADJUST_OFFSET                                 (C_MODEL_BASE + 0x0055001c)
#define PTP_OFFSET_ADJUST_OFFSET                                     (C_MODEL_BASE + 0x00550010)
#define PTP_REF_FRC_OFFSET                                           (C_MODEL_BASE + 0x00550000)
#define PTP_FRC_CTL_OFFSET                                           (C_MODEL_BASE + 0x00550018)
#define DS_OOB_FC_CALENDAR_OFFSET                                    (C_MODEL_BASE + 0x00560000)
#define DS_OOB_FC_GRP_MAP_OFFSET                                     (C_MODEL_BASE + 0x00560200)
#define OOB_FC_INTERRUPT_FATAL_OFFSET                                (C_MODEL_BASE + 0x00560410)
#define OOB_FC_INTERRUPT_NORMAL_OFFSET                               (C_MODEL_BASE + 0x00560400)
#define OOB_FC_CFG_CHAN_NUM_OFFSET                                   (C_MODEL_BASE + 0x00560440)
#define OOB_FC_CFG_FLOW_CTL_OFFSET                                   (C_MODEL_BASE + 0x0056043c)
#define OOB_FC_CFG_INGS_MODE_OFFSET                                  (C_MODEL_BASE + 0x00560444)
#define OOB_FC_CFG_PORT_NUM_OFFSET                                   (C_MODEL_BASE + 0x00560448)
#define OOB_FC_CFG_RX_PROC_OFFSET                                    (C_MODEL_BASE + 0x00560450)
#define OOB_FC_CFG_SPI_MODE_OFFSET                                   (C_MODEL_BASE + 0x0056044c)
#define OOB_FC_DEBUG_STATS_OFFSET                                    (C_MODEL_BASE + 0x00560438)
#define OOB_FC_EGS_VEC_REG_OFFSET                                    (C_MODEL_BASE + 0x00560500)
#define OOB_FC_ERROR_STATS_OFFSET                                    (C_MODEL_BASE + 0x00560458)
#define OOB_FC_FRAME_GAP_NUM_OFFSET                                  (C_MODEL_BASE + 0x00560454)
#define OOB_FC_FRAME_UPDATE_STATE_OFFSET                             (C_MODEL_BASE + 0x00560434)
#define OOB_FC_INGS_VEC_REG_OFFSET                                   (C_MODEL_BASE + 0x005604c0)
#define OOB_FC_INIT_OFFSET                                           (C_MODEL_BASE + 0x00560420)
#define OOB_FC_INIT_DONE_OFFSET                                      (C_MODEL_BASE + 0x00560424)
#define OOB_FC_PARITY_EN_OFFSET                                      (C_MODEL_BASE + 0x00560428)
#define OOB_FC_RX_RCV_EN_OFFSET                                      (C_MODEL_BASE + 0x00560430)
#define OOB_FC_TX_FIFO_A_FULL_THRD_OFFSET                            (C_MODEL_BASE + 0x0056042c)
#define DS_DEST_PTP_CHAN_OFFSET                                      (C_MODEL_BASE + 0x00602200)
#define DS_EGR_IB_LPP_INFO_OFFSET                                    (C_MODEL_BASE + 0x00603800)
#define DS_QCN_PORT_MAC_OFFSET                                       (C_MODEL_BASE + 0x00602500)
#define INBD_FC_REQ_TAB_OFFSET                                       (C_MODEL_BASE + 0x00603600)
#define NET_TX_CAL_BAK_OFFSET                                        (C_MODEL_BASE + 0x00604400)
#define NET_TX_CALENDAR_CTL_OFFSET                                   (C_MODEL_BASE + 0x00601400)
#define NET_TX_CH_DYNAMIC_INFO_OFFSET                                (C_MODEL_BASE + 0x00601200)
#define NET_TX_CH_STATIC_INFO_OFFSET                                 (C_MODEL_BASE + 0x00601000)
#define NET_TX_CHAN_ID_MAP_OFFSET                                    (C_MODEL_BASE + 0x00602000)
#define NET_TX_EEE_TIMER_TAB_OFFSET                                  (C_MODEL_BASE + 0x00603200)
#define NET_TX_HDR_BUF_OFFSET                                        (C_MODEL_BASE + 0x00620000)
#define NET_TX_INTERRUPT_FATAL_OFFSET                                (C_MODEL_BASE + 0x00600080)
#define NET_TX_INTERRUPT_NORMAL_OFFSET                               (C_MODEL_BASE + 0x00600000)
#define NET_TX_PAUSE_TAB_OFFSET                                      (C_MODEL_BASE + 0x00603000)
#define NET_TX_PKT_BUF_OFFSET                                        (C_MODEL_BASE + 0x00640000)
#define NET_TX_PORT_ID_MAP_OFFSET                                    (C_MODEL_BASE + 0x00602100)
#define NET_TX_PORT_MODE_TAB_OFFSET                                  (C_MODEL_BASE + 0x00603300)
#define NET_TX_SGMAC_PRIORITY_MAP_TABLE_OFFSET                       (C_MODEL_BASE + 0x00603c00)
#define PAUSE_REQ_TAB_OFFSET                                         (C_MODEL_BASE + 0x00603400)
#define DS_EGR_IB_LPP_INFO__REG_RAM__RAM_CHK_REC_OFFSET              (C_MODEL_BASE + 0x006001a4)
#define INBD_FC_LOG_REQ_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET           (C_MODEL_BASE + 0x006001a0)
#define NET_TX_CAL_CTL_OFFSET                                        (C_MODEL_BASE + 0x00600028)
#define NET_TX_CFG_CHAN_ID_OFFSET                                    (C_MODEL_BASE + 0x0060002c)
#define NET_TX_CHAN_CREDIT_THRD_OFFSET                               (C_MODEL_BASE + 0x00600128)
#define NET_TX_CHAN_CREDIT_USED_OFFSET                               (C_MODEL_BASE + 0x00600140)
#define NET_TX_CHAN_TX_DIS_CFG_OFFSET                                (C_MODEL_BASE + 0x006000f8)
#define NET_TX_CHANNEL_EN_OFFSET                                     (C_MODEL_BASE + 0x00600010)
#define NET_TX_CHANNEL_TX_EN_OFFSET                                  (C_MODEL_BASE + 0x00600018)
#define NET_TX_CREDIT_CLEAR_CTL_OFFSET                               (C_MODEL_BASE + 0x006000f0)
#define NET_TX_DEBUG_STATS_OFFSET                                    (C_MODEL_BASE + 0x00600200)
#define NET_TX_E_E_E_CFG_OFFSET                                      (C_MODEL_BASE + 0x00600188)
#define NET_TX_E_E_E_SLEEP_TIMER_CFG_OFFSET                          (C_MODEL_BASE + 0x006000e0)
#define NET_TX_E_E_E_WAKEUP_TIMER_CFG_OFFSET                         (C_MODEL_BASE + 0x006000d0)
#define NET_TX_E_LOOP_STALL_RECORD_OFFSET                            (C_MODEL_BASE + 0x0060004c)
#define NET_TX_EPE_STALL_CTL_OFFSET                                  (C_MODEL_BASE + 0x00600038)
#define NET_TX_FORCE_TX_CFG_OFFSET                                   (C_MODEL_BASE + 0x006000e8)
#define NET_TX_INBAND_FC_CTL_OFFSET                                  (C_MODEL_BASE + 0x00600190)
#define NET_TX_INIT_OFFSET                                           (C_MODEL_BASE + 0x00600020)
#define NET_TX_INIT_DONE_OFFSET                                      (C_MODEL_BASE + 0x00600024)
#define NET_TX_INT_LK_CTL_OFFSET                                     (C_MODEL_BASE + 0x00600120)
#define NET_TX_MISC_CTL_OFFSET                                       (C_MODEL_BASE + 0x00600060)
#define NET_TX_PARITY_ECC_CTL_OFFSET                                 (C_MODEL_BASE + 0x00600034)
#define NET_TX_PARITY_FAIL_RECORD_OFFSET                             (C_MODEL_BASE + 0x00600050)
#define NET_TX_PAUSE_QUANTA_CFG_OFFSET                               (C_MODEL_BASE + 0x006000c0)
#define NET_TX_PTP_EN_CTL_OFFSET                                     (C_MODEL_BASE + 0x00600068)
#define NET_TX_QCN_CTL_OFFSET                                        (C_MODEL_BASE + 0x00600130)
#define NET_TX_STAT_SEL_OFFSET                                       (C_MODEL_BASE + 0x00600040)
#define NET_TX_TX_THRD_CFG_OFFSET                                    (C_MODEL_BASE + 0x00600030)
#define PARSER_INTERRUPT_FATAL_OFFSET                                (C_MODEL_BASE + 0x009000e0)
#define IPE_INTF_MAP_PARSER_CREDIT_THRD_OFFSET                       (C_MODEL_BASE + 0x0090013c)
#define PARSER_ETHERNET_CTL_OFFSET                                   (C_MODEL_BASE + 0x00900060)
#define PARSER_FCOE_CTL_OFFSET                                       (C_MODEL_BASE + 0x0090012c)
#define PARSER_IP_HASH_CTL_OFFSET                                    (C_MODEL_BASE + 0x00900128)
#define PARSER_LAYER2_FLEX_CTL_OFFSET                                (C_MODEL_BASE + 0x009000f0)
#define PARSER_LAYER2_PROTOCOL_CAM_OFFSET                            (C_MODEL_BASE + 0x00900020)
#define PARSER_LAYER2_PROTOCOL_CAM_VALID_OFFSET                      (C_MODEL_BASE + 0x00900124)
#define PARSER_LAYER3_FLEX_CTL_OFFSET                                (C_MODEL_BASE + 0x009000c0)
#define PARSER_LAYER3_HASH_CTL_OFFSET                                (C_MODEL_BASE + 0x00900130)
#define PARSER_LAYER3_PROTOCOL_CAM_OFFSET                            (C_MODEL_BASE + 0x00900080)
#define PARSER_LAYER3_PROTOCOL_CAM_VALID_OFFSET                      (C_MODEL_BASE + 0x00900134)
#define PARSER_LAYER3_PROTOCOL_CTL_OFFSET                            (C_MODEL_BASE + 0x00900120)
#define PARSER_LAYER4_ACH_CTL_OFFSET                                 (C_MODEL_BASE + 0x009000b0)
#define PARSER_LAYER4_APP_CTL_OFFSET                                 (C_MODEL_BASE + 0x009000a0)
#define PARSER_LAYER4_APP_DATA_CTL_OFFSET                            (C_MODEL_BASE + 0x00900040)
#define PARSER_LAYER4_FLAG_OP_CTL_OFFSET                             (C_MODEL_BASE + 0x00900090)
#define PARSER_LAYER4_FLEX_CTL_OFFSET                                (C_MODEL_BASE + 0x00900138)
#define PARSER_LAYER4_HASH_CTL_OFFSET                                (C_MODEL_BASE + 0x00900140)
#define PARSER_LAYER4_PORT_OP_CTL_OFFSET                             (C_MODEL_BASE + 0x00900000)
#define PARSER_LAYER4_PORT_OP_SEL_OFFSET                             (C_MODEL_BASE + 0x00900118)
#define PARSER_MPLS_CTL_OFFSET                                       (C_MODEL_BASE + 0x00900100)
#define PARSER_PACKET_TYPE_MAP_OFFSET                                (C_MODEL_BASE + 0x00900110)
#define PARSER_PARITY_EN_OFFSET                                      (C_MODEL_BASE + 0x0090011c)
#define PARSER_PBB_CTL_OFFSET                                        (C_MODEL_BASE + 0x00900108)
#define PARSER_TRILL_CTL_OFFSET                                      (C_MODEL_BASE + 0x009000d0)
#define DS_POLICER_CONTROL_OFFSET                                    (C_MODEL_BASE + 0x00988000)
#define DS_POLICER_COUNT_OFFSET                                      (C_MODEL_BASE + 0x00990000)
#define DS_POLICER_PROFILE_OFFSET                                    (C_MODEL_BASE + 0x00981000)
#define POLICING_EPE_FIFO_OFFSET                                     (C_MODEL_BASE + 0x00980200)
#define POLICING_INTERRUPT_FATAL_OFFSET                              (C_MODEL_BASE + 0x00980000)
#define POLICING_INTERRUPT_NORMAL_OFFSET                             (C_MODEL_BASE + 0x00980010)
#define POLICING_IPE_FIFO_OFFSET                                     (C_MODEL_BASE + 0x00980100)
#define DS_POLICER_CONTROL__RAM_CHK_REC_OFFSET                       (C_MODEL_BASE + 0x00980054)
#define DS_POLICER_COUNT__RAM_CHK_REC_OFFSET                         (C_MODEL_BASE + 0x00980050)
#define DS_POLICER_PROFILE_RAM_CHK_REC_OFFSET                        (C_MODEL_BASE + 0x00980058)
#define IPE_POLICING_CTL_OFFSET                                      (C_MODEL_BASE + 0x00980020)
#define POLICING_DEBUG_STATS_OFFSET                                  (C_MODEL_BASE + 0x00980084)
#define POLICING_ECC_CTL_OFFSET                                      (C_MODEL_BASE + 0x00980048)
#define POLICING_EPE_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET              (C_MODEL_BASE + 0x00980044)
#define POLICING_INIT_CTL_OFFSET                                     (C_MODEL_BASE + 0x00980038)
#define POLICING_IPE_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET              (C_MODEL_BASE + 0x00980040)
#define POLICING_MISC_CTL_OFFSET                                     (C_MODEL_BASE + 0x00980030)
#define UPDATE_REQ_IGNORE_RECORD_OFFSET                              (C_MODEL_BASE + 0x00980060)
#define DS_STP_STATE_OFFSET                                          (C_MODEL_BASE + 0x00a21000)
#define DS_VLAN_OFFSET                                               (C_MODEL_BASE + 0x00a00000)
#define DS_VLAN_PROFILE_OFFSET                                       (C_MODEL_BASE + 0x00a20000)
#define SHARED_DS_INTERRUPT_NORMAL_OFFSET                            (C_MODEL_BASE + 0x00a21800)
#define DS_STP_STATE__REG_RAM__RAM_CHK_REC_OFFSET                    (C_MODEL_BASE + 0x00a21820)
#define DS_VLAN_PROFILE__REG_RAM__RAM_CHK_REC_OFFSET                 (C_MODEL_BASE + 0x00a21824)
#define DS_VLAN__REG_RAM__RAM_CHK_REC_OFFSET                         (C_MODEL_BASE + 0x00a2181c)
#define SHARED_DS_ECC_CTL_OFFSET                                     (C_MODEL_BASE + 0x00a21828)
#define SHARED_DS_ECC_ERR_STATS_OFFSET                               (C_MODEL_BASE + 0x00a21818)
#define SHARED_DS_INIT_OFFSET                                        (C_MODEL_BASE + 0x00a21810)
#define SHARED_DS_INIT_DONE_OFFSET                                   (C_MODEL_BASE + 0x00a21814)
#define BUF_PTR_MEM_OFFSET                                           (C_MODEL_BASE + 0x00c00000)
#define BUF_STORE_INTERRUPT_FATAL_OFFSET                             (C_MODEL_BASE + 0x00c15a00)
#define BUF_STORE_INTERRUPT_NORMAL_OFFSET                            (C_MODEL_BASE + 0x00c15a30)
#define CHAN_INFO_RAM_OFFSET                                         (C_MODEL_BASE + 0x00c11000)
#define DS_IGR_COND_DIS_PROF_ID_OFFSET                               (C_MODEL_BASE + 0x00c15200)
#define DS_IGR_PORT_CNT_OFFSET                                       (C_MODEL_BASE + 0x00c13800)
#define DS_IGR_PORT_TC_CNT_OFFSET                                    (C_MODEL_BASE + 0x00c10000)
#define DS_IGR_PORT_TC_MIN_PROF_ID_OFFSET                            (C_MODEL_BASE + 0x00c14800)
#define DS_IGR_PORT_TC_PRI_MAP_OFFSET                                (C_MODEL_BASE + 0x00c14a00)
#define DS_IGR_PORT_TC_THRD_PROF_ID_OFFSET                           (C_MODEL_BASE + 0x00c14000)
#define DS_IGR_PORT_TC_THRD_PROFILE_OFFSET                           (C_MODEL_BASE + 0x00c13000)
#define DS_IGR_PORT_THRD_PROF_ID_OFFSET                              (C_MODEL_BASE + 0x00c15780)
#define DS_IGR_PORT_THRD_PROFILE_OFFSET                              (C_MODEL_BASE + 0x00c15000)
#define DS_IGR_PORT_TO_TC_PROF_ID_OFFSET                             (C_MODEL_BASE + 0x00c15800)
#define DS_IGR_PRI_TO_TC_MAP_OFFSET                                  (C_MODEL_BASE + 0x00c12000)
#define DS_SRC_SGMAC_GROUP_OFFSET                                    (C_MODEL_BASE + 0x00c15600)
#define IPE_HDR_ERR_STATS_MEM_OFFSET                                 (C_MODEL_BASE + 0x00c15500)
#define IPE_HDR_MSG_FIFO_OFFSET                                      (C_MODEL_BASE + 0x00c15c00)
#define MET_FIFO_PRIORITY_MAP_TABLE_OFFSET                           (C_MODEL_BASE + 0x00c13c00)
#define PKT_ERR_STATS_MEM_OFFSET                                     (C_MODEL_BASE + 0x00c14400)
#define PKT_MSG_FIFO_OFFSET                                          (C_MODEL_BASE + 0x00c12800)
#define PKT_RESRC_ERR_STATS_OFFSET                                   (C_MODEL_BASE + 0x00c14e00)
#define BUF_STORE_CHAN_INFO_CTL_OFFSET                               (C_MODEL_BASE + 0x00c15aa8)
#define BUF_STORE_CPU_MAC_PAUSE_RECORD_OFFSET                        (C_MODEL_BASE + 0x00c15b00)
#define BUF_STORE_CPU_MAC_PAUSE_REQ_CTL_OFFSET                       (C_MODEL_BASE + 0x00c15b04)
#define BUF_STORE_CREDIT_DEBUG_OFFSET                                (C_MODEL_BASE + 0x00c15a80)
#define BUF_STORE_CREDIT_THD_OFFSET                                  (C_MODEL_BASE + 0x00c15a98)
#define BUF_STORE_ECC_CTL_OFFSET                                     (C_MODEL_BASE + 0x00c15af8)
#define BUF_STORE_ECC_ERROR_DEBUG_STATS_OFFSET                       (C_MODEL_BASE + 0x00c15ab0)
#define BUF_STORE_ERR_STATS_MEM_CTL_OFFSET                           (C_MODEL_BASE + 0x00c15ac8)
#define BUF_STORE_FIFO_CTL_OFFSET                                    (C_MODEL_BASE + 0x00c15900)
#define BUF_STORE_FREE_LIST_CTL_OFFSET                               (C_MODEL_BASE + 0x00c15980)
#define BUF_STORE_GCN_CTL_OFFSET                                     (C_MODEL_BASE + 0x00c15aa0)
#define BUF_STORE_INPUT_DEBUG_STATS_OFFSET                           (C_MODEL_BASE + 0x00c15960)
#define BUF_STORE_INPUT_FIFO_ARB_CTL_OFFSET                          (C_MODEL_BASE + 0x00c15b34)
#define BUF_STORE_INT_LK_RESRC_CTL_OFFSET                            (C_MODEL_BASE + 0x00c15b38)
#define BUF_STORE_INT_LK_STALL_INFO_OFFSET                           (C_MODEL_BASE + 0x00c15ae8)
#define BUF_STORE_INTERNAL_STALL_CTL_OFFSET                          (C_MODEL_BASE + 0x00c15b3c)
#define BUF_STORE_LINK_LIST_SLOT_OFFSET                              (C_MODEL_BASE + 0x00c15b14)
#define BUF_STORE_LINK_LIST_TABLE_CTL_OFFSET                         (C_MODEL_BASE + 0x00c15a90)
#define BUF_STORE_MET_FIFO_STALL_CTL_OFFSET                          (C_MODEL_BASE + 0x00c15b18)
#define BUF_STORE_MISC_CTL_OFFSET                                    (C_MODEL_BASE + 0x00c15a78)
#define BUF_STORE_MSG_DROP_CTL_OFFSET                                (C_MODEL_BASE + 0x00c15b10)
#define BUF_STORE_NET_NORMAL_PAUSE_RECORD_OFFSET                     (C_MODEL_BASE + 0x00c15ad0)
#define BUF_STORE_NET_PFC_RECORD_OFFSET                              (C_MODEL_BASE + 0x00c15ab8)
#define BUF_STORE_NET_PFC_REQ_CTL_OFFSET                             (C_MODEL_BASE + 0x00c15700)
#define BUF_STORE_OOB_FC_CTL_OFFSET                                  (C_MODEL_BASE + 0x00c15a20)
#define BUF_STORE_OUTPUT_DEBUG_STATS_OFFSET                          (C_MODEL_BASE + 0x00c15a60)
#define BUF_STORE_PARITY_FAIL_RECORD_OFFSET                          (C_MODEL_BASE + 0x00c15a50)
#define BUF_STORE_PB_CREDIT_RUN_OUT_DEBUG_STATS_OFFSET               (C_MODEL_BASE + 0x00c15ae0)
#define BUF_STORE_RESRC_RAM_CTL_OFFSET                               (C_MODEL_BASE + 0x00c15ac0)
#define BUF_STORE_SGMAC_CTL_OFFSET                                   (C_MODEL_BASE + 0x00c15b48)
#define BUF_STORE_STALL_DROP_DEBUG_STATS_OFFSET                      (C_MODEL_BASE + 0x00c158c0)
#define BUF_STORE_STATS_CTL_OFFSET                                   (C_MODEL_BASE + 0x00c15b0c)
#define BUF_STORE_TOTAL_RESRC_INFO_OFFSET                            (C_MODEL_BASE + 0x00c15a40)
#define BUFFER_STORE_CTL_OFFSET                                      (C_MODEL_BASE + 0x00c15940)
#define BUFFER_STORE_FORCE_LOCAL_CTL_OFFSET                          (C_MODEL_BASE + 0x00c159c0)
#define DS_IGR_COND_DIS_PROF_ID__REG_RAM__RAM_CHK_REC_OFFSET         (C_MODEL_BASE + 0x00c15b08)
#define DS_IGR_PORT_CNT__REG_RAM__RAM_CHK_REC_OFFSET                 (C_MODEL_BASE + 0x00c15b1c)
#define DS_IGR_PORT_TC_CNT__REG_RAM__RAM_CHK_REC_OFFSET              (C_MODEL_BASE + 0x00c15b20)
#define DS_IGR_PORT_TC_MIN_PROF_ID__REG_RAM__RAM_CHK_REC_OFFSET      (C_MODEL_BASE + 0x00c15afc)
#define DS_IGR_PORT_TC_PRI_MAP__REG_RAM__RAM_CHK_REC_OFFSET          (C_MODEL_BASE + 0x00c15b30)
#define DS_IGR_PORT_TC_THRD_PROF_ID__REG_RAM__RAM_CHK_REC_OFFSET     (C_MODEL_BASE + 0x00c15b24)
#define DS_IGR_PORT_TC_THRD_PROFILE__REG_RAM__RAM_CHK_REC_OFFSET     (C_MODEL_BASE + 0x00c15b28)
#define DS_IGR_PORT_THRD_PROFILE__REG_RAM__RAM_CHK_REC_OFFSET        (C_MODEL_BASE + 0x00c15b2c)
#define DS_IGR_PRI_TO_TC_MAP__REG_RAM__RAM_CHK_REC_OFFSET            (C_MODEL_BASE + 0x00c15b40)
#define IGR_COND_DIS_PROFILE_OFFSET                                  (C_MODEL_BASE + 0x00c15a88)
#define IGR_CONGEST_LEVEL_THRD_OFFSET                                (C_MODEL_BASE + 0x00c15880)
#define IGR_GLB_DROP_COND_CTL_OFFSET                                 (C_MODEL_BASE + 0x00c15a70)
#define IGR_PORT_TC_MIN_PROFILE_OFFSET                               (C_MODEL_BASE + 0x00c159f0)
#define IGR_RESRC_MGR_MISC_CTL_OFFSET                                (C_MODEL_BASE + 0x00c15af0)
#define IGR_SC_CNT_OFFSET                                            (C_MODEL_BASE + 0x00c159e0)
#define IGR_SC_THRD_OFFSET                                           (C_MODEL_BASE + 0x00c15ad8)
#define IGR_TC_CNT_OFFSET                                            (C_MODEL_BASE + 0x00c159a0)
#define IGR_TC_THRD_OFFSET                                           (C_MODEL_BASE + 0x00c15a10)
#define NET_LOCAL_PHY_PORT_FC_RECORD_OFFSET                          (C_MODEL_BASE + 0x00c15400)
#define DS_APS_BRIDGE_OFFSET                                         (C_MODEL_BASE + 0x00c70000)
#define DS_APS_CHANNEL_MAP_OFFSET                                    (C_MODEL_BASE + 0x00c47e00)
#define DS_MET_FIFO_EXCP_OFFSET                                      (C_MODEL_BASE + 0x00c4f400)
#define MET_FIFO_BR_RCD_UPD_FIFO_OFFSET                              (C_MODEL_BASE + 0x00c4e800)
#define MET_FIFO_INTERRUPT_FATAL_OFFSET                              (C_MODEL_BASE + 0x00c40000)
#define MET_FIFO_INTERRUPT_NORMAL_OFFSET                             (C_MODEL_BASE + 0x00c404f0)
#define MET_FIFO_MSG_RAM_OFFSET                                      (C_MODEL_BASE + 0x00c58000)
#define MET_FIFO_Q_MGR_RCD_UPD_FIFO_OFFSET                           (C_MODEL_BASE + 0x00c4fc00)
#define MET_FIFO_RCD_RAM_OFFSET                                      (C_MODEL_BASE + 0x00c60000)
#define DS_APS_BRIDGE__REG_RAM__RAM_CHK_REC_OFFSET                   (C_MODEL_BASE + 0x00c40464)
#define MET_FIFO_APS_INIT_OFFSET                                     (C_MODEL_BASE + 0x00c40480)
#define MET_FIFO_CTL_OFFSET                                          (C_MODEL_BASE + 0x00c40500)
#define MET_FIFO_DEBUG_STATS_OFFSET                                  (C_MODEL_BASE + 0x00c40420)
#define MET_FIFO_DRAIN_ENABLE_OFFSET                                 (C_MODEL_BASE + 0x00c40094)
#define MET_FIFO_ECC_CTL_OFFSET                                      (C_MODEL_BASE + 0x00c40448)
#define MET_FIFO_EN_QUE_CREDIT_OFFSET                                (C_MODEL_BASE + 0x00c40080)
#define MET_FIFO_END_PTR_OFFSET                                      (C_MODEL_BASE + 0x00c40028)
#define MET_FIFO_EXCP_TAB_PARITY_FAIL_RECORD_OFFSET                  (C_MODEL_BASE + 0x00c4006c)
#define MET_FIFO_INIT_OFFSET                                         (C_MODEL_BASE + 0x00c40038)
#define MET_FIFO_INIT_DONE_OFFSET                                    (C_MODEL_BASE + 0x00c4003c)
#define MET_FIFO_INPUT_FIFO_DEPTH_OFFSET                             (C_MODEL_BASE + 0x00c40030)
#define MET_FIFO_INPUT_FIFO_THRESHOLD_OFFSET                         (C_MODEL_BASE + 0x00c40118)
#define MET_FIFO_LINK_DOWN_STATE_RECORD_OFFSET                       (C_MODEL_BASE + 0x00c404e8)
#define MET_FIFO_LINK_SCAN_DONE_STATE_OFFSET                         (C_MODEL_BASE + 0x00c40468)
#define MET_FIFO_MAX_INIT_CNT_OFFSET                                 (C_MODEL_BASE + 0x00c40140)
#define MET_FIFO_MCAST_ENABLE_OFFSET                                 (C_MODEL_BASE + 0x00c40144)
#define MET_FIFO_MSG_CNT_OFFSET                                      (C_MODEL_BASE + 0x00c40048)
#define MET_FIFO_MSG_TYPE_CNT_OFFSET                                 (C_MODEL_BASE + 0x00c40470)
#define MET_FIFO_PARITY_EN_OFFSET                                    (C_MODEL_BASE + 0x00c4008c)
#define MET_FIFO_PENDING_MCAST_CNT_OFFSET                            (C_MODEL_BASE + 0x00c40068)
#define MET_FIFO_Q_MGR_CREDIT_OFFSET                                 (C_MODEL_BASE + 0x00c40088)
#define MET_FIFO_Q_WRITE_RCD_UPD_FIFO_THRD_OFFSET                    (C_MODEL_BASE + 0x00c40090)
#define MET_FIFO_RD_CUR_STATE_MACHINE_OFFSET                         (C_MODEL_BASE + 0x00c400a0)
#define MET_FIFO_RD_PTR_OFFSET                                       (C_MODEL_BASE + 0x00c40058)
#define MET_FIFO_START_PTR_OFFSET                                    (C_MODEL_BASE + 0x00c40020)
#define MET_FIFO_TB_INFO_ARB_CREDIT_OFFSET                           (C_MODEL_BASE + 0x00c40084)
#define MET_FIFO_UPDATE_RCD_ERROR_SPOT_OFFSET                        (C_MODEL_BASE + 0x00c40070)
#define MET_FIFO_WR_PTR_OFFSET                                       (C_MODEL_BASE + 0x00c40050)
#define MET_FIFO_WRR_CFG_OFFSET                                      (C_MODEL_BASE + 0x00c40060)
#define MET_FIFO_WRR_WEIGHT_CNT_OFFSET                               (C_MODEL_BASE + 0x00c40064)
#define MET_FIFO_WRR_WT_OFFSET                                       (C_MODEL_BASE + 0x00c4009c)
#define MET_FIFO_WRR_WT_CFG_OFFSET                                   (C_MODEL_BASE + 0x00c40098)
#define BUF_RETRV_BUF_CONFIG_MEM_OFFSET                              (C_MODEL_BASE + 0x00c87600)
#define BUF_RETRV_BUF_CREDIT_CONFIG_MEM_OFFSET                       (C_MODEL_BASE + 0x00c87700)
#define BUF_RETRV_BUF_CREDIT_MEM_OFFSET                              (C_MODEL_BASE + 0x00c87400)
#define BUF_RETRV_BUF_PTR_MEM_OFFSET                                 (C_MODEL_BASE + 0x00c80000)
#define BUF_RETRV_BUF_STATUS_MEM_OFFSET                              (C_MODEL_BASE + 0x00c87300)
#define BUF_RETRV_CREDIT_CONFIG_MEM_OFFSET                           (C_MODEL_BASE + 0x00c87000)
#define BUF_RETRV_CREDIT_MEM_OFFSET                                  (C_MODEL_BASE + 0x00c87100)
#define BUF_RETRV_INTERRUPT_FATAL_OFFSET                             (C_MODEL_BASE + 0x00c878c0)
#define BUF_RETRV_INTERRUPT_NORMAL_OFFSET                            (C_MODEL_BASE + 0x00c878e0)
#define BUF_RETRV_MSG_PARK_MEM_OFFSET                                (C_MODEL_BASE + 0x00c86000)
#define BUF_RETRV_PKT_BUF_TRACK_FIFO_OFFSET                          (C_MODEL_BASE + 0x00c86e00)
#define BUF_RETRV_PKT_CONFIG_MEM_OFFSET                              (C_MODEL_BASE + 0x00c87200)
#define BUF_RETRV_PKT_MSG_MEM_OFFSET                                 (C_MODEL_BASE + 0x00c84000)
#define BUF_RETRV_PKT_STATUS_MEM_OFFSET                              (C_MODEL_BASE + 0x00c87500)
#define DS_BUF_RETRV_COLOR_MAP_OFFSET                                (C_MODEL_BASE + 0x00c86c00)
#define DS_BUF_RETRV_EXCP_OFFSET                                     (C_MODEL_BASE + 0x00c86800)
#define DS_BUF_RETRV_PRIORITY_CTL_OFFSET                             (C_MODEL_BASE + 0x00c87c00)
#define BUF_RETRV_BUF_CREDIT_CONFIG_OFFSET                           (C_MODEL_BASE + 0x00c8793c)
#define BUF_RETRV_BUF_PTR_MEM__REG_RAM__RAM_CHK_REC_OFFSET           (C_MODEL_BASE + 0x00c87940)
#define BUF_RETRV_BUF_PTR_SLOT_CONFIG_OFFSET                         (C_MODEL_BASE + 0x00c87928)
#define BUF_RETRV_BUF_WEIGHT_CONFIG_OFFSET                           (C_MODEL_BASE + 0x00c87934)
#define BUF_RETRV_CHAN_ID_CFG_OFFSET                                 (C_MODEL_BASE + 0x00c87800)
#define BUF_RETRV_CREDIT_CONFIG_OFFSET                               (C_MODEL_BASE + 0x00c87920)
#define BUF_RETRV_CREDIT_MODULE_ENABLE_CONFIG_OFFSET                 (C_MODEL_BASE + 0x00c87968)
#define BUF_RETRV_DRAIN_ENABLE_OFFSET                                (C_MODEL_BASE + 0x00c87958)
#define BUF_RETRV_FLOW_CTL_INFO_DEBUG_OFFSET                         (C_MODEL_BASE + 0x00c878a0)
#define BUF_RETRV_INIT_OFFSET                                        (C_MODEL_BASE + 0x00c87950)
#define BUF_RETRV_INIT_DONE_OFFSET                                   (C_MODEL_BASE + 0x00c87954)
#define BUF_RETRV_INPUT_DEBUG_STATS_OFFSET                           (C_MODEL_BASE + 0x00c878d0)
#define BUF_RETRV_INTF_FIFO_CONFIG_CREDIT_OFFSET                     (C_MODEL_BASE + 0x00c87918)
#define BUF_RETRV_INTF_MEM_ADDR_CONFIG_OFFSET                        (C_MODEL_BASE + 0x00c87880)
#define BUF_RETRV_INTF_MEM_ADDR_DEBUG_OFFSET                         (C_MODEL_BASE + 0x00c87840)
#define BUF_RETRV_INTF_MEM_PARITY_RECORD_OFFSET                      (C_MODEL_BASE + 0x00c87910)
#define BUF_RETRV_MISC_CONFIG_OFFSET                                 (C_MODEL_BASE + 0x00c87908)
#define BUF_RETRV_MISC_DEBUG_STATS_OFFSET                            (C_MODEL_BASE + 0x00c878f0)
#define BUF_RETRV_MSG_PARK_MEM__REG_RAM__RAM_CHK_REC_OFFSET          (C_MODEL_BASE + 0x00c8794c)
#define BUF_RETRV_OUTPUT_PKT_DEBUG_STATS_OFFSET                      (C_MODEL_BASE + 0x00c87860)
#define BUF_RETRV_OVERRUN_PORT_OFFSET                                (C_MODEL_BASE + 0x00c8795c)
#define BUF_RETRV_PARITY_ENABLE_OFFSET                               (C_MODEL_BASE + 0x00c87948)
#define BUF_RETRV_PKT_BUF_MODULE_CONFIG_CREDIT_OFFSET                (C_MODEL_BASE + 0x00c87900)
#define BUF_RETRV_PKT_BUF_REQ_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET     (C_MODEL_BASE + 0x00c87964)
#define BUF_RETRV_PKT_BUF_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET   (C_MODEL_BASE + 0x00c87960)
#define BUF_RETRV_PKT_MSG_MEM__REG_RAM__RAM_CHK_REC_OFFSET           (C_MODEL_BASE + 0x00c8792c)
#define BUF_RETRV_PKT_WEIGHT_CONFIG_OFFSET                           (C_MODEL_BASE + 0x00c87930)
#define BUF_RETRV_STACKING_EN_OFFSET                                 (C_MODEL_BASE + 0x00c878f8)
#define BUFFER_RETRIEVE_CTL_OFFSET                                   (C_MODEL_BASE + 0x00c87ac0)
#define DS_BUF_RETRV_EXCP__REG_RAM__RAM_CHK_REC_OFFSET               (C_MODEL_BASE + 0x00c87938)
#define CPU_MAC_INTERRUPT_NORMAL_OFFSET                              (C_MODEL_BASE + 0x00ca0000)
#define CPU_MAC_PRIORITY_MAP_OFFSET                                  (C_MODEL_BASE + 0x00ca1000)
#define CPU_MAC_CREDIT_CFG_OFFSET                                    (C_MODEL_BASE + 0x00ca0024)
#define CPU_MAC_CREDIT_CTL_OFFSET                                    (C_MODEL_BASE + 0x00ca00a0)
#define CPU_MAC_CTL_OFFSET                                           (C_MODEL_BASE + 0x00ca00c0)
#define CPU_MAC_DA_CFG_OFFSET                                        (C_MODEL_BASE + 0x00ca0040)
#define CPU_MAC_DEBUG_STATS_OFFSET                                   (C_MODEL_BASE + 0x00ca0070)
#define CPU_MAC_INIT_CTL_OFFSET                                      (C_MODEL_BASE + 0x00ca0018)
#define CPU_MAC_MISC_CTL_OFFSET                                      (C_MODEL_BASE + 0x00ca0014)
#define CPU_MAC_PAUSE_CTL_OFFSET                                     (C_MODEL_BASE + 0x00ca0028)
#define CPU_MAC_PCS_ANEG_CFG_OFFSET                                  (C_MODEL_BASE + 0x00ca0078)
#define CPU_MAC_PCS_ANEG_STATUS_OFFSET                               (C_MODEL_BASE + 0x00ca007c)
#define CPU_MAC_PCS_CFG_OFFSET                                       (C_MODEL_BASE + 0x00ca0060)
#define CPU_MAC_PCS_CODE_ERR_CNT_OFFSET                              (C_MODEL_BASE + 0x00ca0064)
#define CPU_MAC_PCS_LINK_TIMER_CTL_OFFSET                            (C_MODEL_BASE + 0x00ca00a4)
#define CPU_MAC_PCS_STATUS_OFFSET                                    (C_MODEL_BASE + 0x00ca0068)
#define CPU_MAC_RESET_CTL_OFFSET                                     (C_MODEL_BASE + 0x00ca0010)
#define CPU_MAC_RX_CTL_OFFSET                                        (C_MODEL_BASE + 0x00ca0020)
#define CPU_MAC_SA_CFG_OFFSET                                        (C_MODEL_BASE + 0x00ca0038)
#define CPU_MAC_STATS_OFFSET                                         (C_MODEL_BASE + 0x00ca0080)
#define CPU_MAC_STATS_UPDATE_CTL_OFFSET                              (C_MODEL_BASE + 0x00ca0030)
#define CPU_MAC_TX_CTL_OFFSET                                        (C_MODEL_BASE + 0x00ca001c)
#define EGRESS_DATA_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET               (C_MODEL_BASE + 0x00ca006c)
#define DS_EGR_PORT_CNT_OFFSET                                       (C_MODEL_BASE + 0x00d0ae00)
#define DS_EGR_PORT_DROP_PROFILE_OFFSET                              (C_MODEL_BASE + 0x00d0b900)
#define DS_EGR_PORT_FC_STATE_OFFSET                                  (C_MODEL_BASE + 0x00d0b100)
#define DS_EGR_RESRC_CTL_OFFSET                                      (C_MODEL_BASE + 0x00d00000)
#define DS_GRP_CNT_OFFSET                                            (C_MODEL_BASE + 0x00d0a000)
#define DS_GRP_DROP_PROFILE_OFFSET                                   (C_MODEL_BASE + 0x00d0b800)
#define DS_HEAD_HASH_MOD_OFFSET                                      (C_MODEL_BASE + 0x00d09c00)
#define DS_LINK_AGGREGATE_CHANNEL_OFFSET                             (C_MODEL_BASE + 0x00d0b700)
#define DS_LINK_AGGREGATE_CHANNEL_GROUP_OFFSET                       (C_MODEL_BASE + 0x00d0bbc0)
#define DS_LINK_AGGREGATE_CHANNEL_MEMBER_OFFSET                      (C_MODEL_BASE + 0x00d09000)
#define DS_LINK_AGGREGATE_CHANNEL_MEMBER_SET_OFFSET                  (C_MODEL_BASE + 0x00d0b600)
#define DS_LINK_AGGREGATE_GROUP_OFFSET                               (C_MODEL_BASE + 0x00d0b500)
#define DS_LINK_AGGREGATE_MEMBER_OFFSET                              (C_MODEL_BASE + 0x00d06000)
#define DS_LINK_AGGREGATE_MEMBER_SET_OFFSET                          (C_MODEL_BASE + 0x00d0aa00)
#define DS_LINK_AGGREGATE_SGMAC_GROUP_OFFSET                         (C_MODEL_BASE + 0x00d0b400)
#define DS_LINK_AGGREGATE_SGMAC_MEMBER_OFFSET                        (C_MODEL_BASE + 0x00d08000)
#define DS_LINK_AGGREGATE_SGMAC_MEMBER_SET_OFFSET                    (C_MODEL_BASE + 0x00d0af00)
#define DS_LINK_AGGREGATION_PORT_OFFSET                              (C_MODEL_BASE + 0x00d0a800)
#define DS_QCN_PROFILE_OFFSET                                        (C_MODEL_BASE + 0x00d0b300)
#define DS_QCN_PROFILE_ID_OFFSET                                     (C_MODEL_BASE + 0x00d0ba00)
#define DS_QCN_QUE_DEPTH_OFFSET                                      (C_MODEL_BASE + 0x00d0ac00)
#define DS_QCN_QUE_ID_BASE_OFFSET                                    (C_MODEL_BASE + 0x00d0be00)
#define DS_QUE_CNT_OFFSET                                            (C_MODEL_BASE + 0x00d07000)
#define DS_QUE_THRD_PROFILE_OFFSET                                   (C_MODEL_BASE + 0x00d08800)
#define DS_QUEUE_HASH_KEY_OFFSET                                     (C_MODEL_BASE + 0x00d02000)
#define DS_QUEUE_NUM_GEN_CTL_OFFSET                                  (C_MODEL_BASE + 0x00d04000)
#define DS_QUEUE_SELECT_MAP_OFFSET                                   (C_MODEL_BASE + 0x00d09800)
#define DS_SGMAC_HEAD_HASH_MOD_OFFSET                                (C_MODEL_BASE + 0x00d0a400)
#define DS_SGMAC_MAP_OFFSET                                          (C_MODEL_BASE + 0x00d0ba80)
#define DS_UNIFORM_RAND_OFFSET                                       (C_MODEL_BASE + 0x00d0c200)
#define Q_MGR_ENQ_BR_RTN_FIFO_OFFSET                                 (C_MODEL_BASE + 0x00d0c140)
#define Q_MGR_ENQ_CHAN_CLASS_OFFSET                                  (C_MODEL_BASE + 0x00d0e000)
#define Q_MGR_ENQ_FREE_LIST_FIFO_OFFSET                              (C_MODEL_BASE + 0x00d0c100)
#define Q_MGR_ENQ_INTERRUPT_FATAL_OFFSET                             (C_MODEL_BASE + 0x00d0bcd0)
#define Q_MGR_ENQ_INTERRUPT_NORMAL_OFFSET                            (C_MODEL_BASE + 0x00d0bcb0)
#define Q_MGR_ENQ_MSG_FIFO_OFFSET                                    (C_MODEL_BASE + 0x00d0b200)
#define Q_MGR_ENQ_QUE_NUM_IN_FIFO_OFFSET                             (C_MODEL_BASE + 0x00d0b000)
#define Q_MGR_ENQ_QUE_NUM_OUT_FIFO_OFFSET                            (C_MODEL_BASE + 0x00d0c000)
#define Q_MGR_ENQ_SCH_FIFO_OFFSET                                    (C_MODEL_BASE + 0x00d0c120)
#define Q_MGR_ENQ_WRED_INDEX_OFFSET                                  (C_MODEL_BASE + 0x00d0d000)
#define DS_EGR_RESRC_CTL__REG_RAM__RAM_CHK_REC_OFFSET                (C_MODEL_BASE + 0x00d0bdb0)
#define DS_GRP_CNT__REG_RAM__RAM_CHK_REC_OFFSET                      (C_MODEL_BASE + 0x00d0bdb4)
#define DS_HEAD_HASH_MOD__REG_RAM__RAM_CHK_REC_OFFSET                (C_MODEL_BASE + 0x00d0bdb8)
#define DS_LINK_AGGREGATE_CHANNEL_MEMBER__REG_RAM__RAM_CHK_REC_OFFSET (C_MODEL_BASE + 0x00d0bdac)
#define DS_LINK_AGGREGATE_MEMBER_SET__REG_RAM__RAM_CHK_REC_OFFSET    (C_MODEL_BASE + 0x00d0bda8)
#define DS_LINK_AGGREGATE_MEMBER__REG_RAM__RAM_CHK_REC_OFFSET        (C_MODEL_BASE + 0x00d0bd98)
#define DS_LINK_AGGREGATE_SGMAC_MEMBER__REG_RAM__RAM_CHK_REC_OFFSET  (C_MODEL_BASE + 0x00d0bd9c)
#define DS_LINK_AGGREGATION_PORT__REG_RAM__RAM_CHK_REC_OFFSET        (C_MODEL_BASE + 0x00d0bda0)
#define DS_QCN_QUE_DEPTH__REG_RAM__RAM_CHK_REC_OFFSET                (C_MODEL_BASE + 0x00d0bda4)
#define DS_QUE_CNT__REG_RAM__RAM_CHK_REC_OFFSET                      (C_MODEL_BASE + 0x00d0bdbc)
#define DS_QUE_THRD_PROFILE__REG_RAM__RAM_CHK_REC_OFFSET             (C_MODEL_BASE + 0x00d0bdc0)
#define DS_QUEUE_HASH_KEY__REG_RAM__RAM_CHK_REC_OFFSET               (C_MODEL_BASE + 0x00d0bddc)
#define DS_QUEUE_NUM_GEN_CTL__REG_RAM__RAM_CHK_REC_OFFSET            (C_MODEL_BASE + 0x00d0bde0)
#define DS_QUEUE_SELECT_MAP__REG_RAM__RAM_CHK_REC_OFFSET             (C_MODEL_BASE + 0x00d0bde4)
#define DS_SGMAC_HEAD_HASH_MOD__REG_RAM__RAM_CHK_REC_OFFSET          (C_MODEL_BASE + 0x00d0bd94)
#define EGR_COND_DIS_PROFILE_OFFSET                                  (C_MODEL_BASE + 0x00d0bdd8)
#define EGR_CONGEST_LEVEL_THRD_OFFSET                                (C_MODEL_BASE + 0x00d0bc40)
#define EGR_RESRC_MGR_CTL_OFFSET                                     (C_MODEL_BASE + 0x00d0bdd4)
#define EGR_SC_CNT_OFFSET                                            (C_MODEL_BASE + 0x00d0bd50)
#define EGR_SC_THRD_OFFSET                                           (C_MODEL_BASE + 0x00d0bd30)
#define EGR_TC_CNT_OFFSET                                            (C_MODEL_BASE + 0x00d0bc80)
#define EGR_TC_THRD_OFFSET                                           (C_MODEL_BASE + 0x00d0bc90)
#define EGR_TOTAL_CNT_OFFSET                                         (C_MODEL_BASE + 0x00d0bdc4)
#define Q_HASH_CAM_CTL_OFFSET                                        (C_MODEL_BASE + 0x00d0bb00)
#define Q_LINK_AGG_TIMER_CTL0_OFFSET                                 (C_MODEL_BASE + 0x00d0bd00)
#define Q_LINK_AGG_TIMER_CTL1_OFFSET                                 (C_MODEL_BASE + 0x00d0bd10)
#define Q_LINK_AGG_TIMER_CTL2_OFFSET                                 (C_MODEL_BASE + 0x00d0bd20)
#define Q_MGR_ENQ_CREDIT_CONFIG_OFFSET                               (C_MODEL_BASE + 0x00d0bca0)
#define Q_MGR_ENQ_CREDIT_USED_OFFSET                                 (C_MODEL_BASE + 0x00d0bcf0)
#define Q_MGR_ENQ_CTL_OFFSET                                         (C_MODEL_BASE + 0x00d0bdc8)
#define Q_MGR_ENQ_DEBUG_FSM_OFFSET                                   (C_MODEL_BASE + 0x00d0bdcc)
#define Q_MGR_ENQ_DEBUG_STATS_OFFSET                                 (C_MODEL_BASE + 0x00d0bc00)
#define Q_MGR_ENQ_DRAIN_ENABLE_OFFSET                                (C_MODEL_BASE + 0x00d0bdd0)
#define Q_MGR_ENQ_DROP_CTL_OFFSET                                    (C_MODEL_BASE + 0x00d0bde8)
#define Q_MGR_ENQ_ECC_CTL_OFFSET                                     (C_MODEL_BASE + 0x00d0bd88)
#define Q_MGR_ENQ_INIT_OFFSET                                        (C_MODEL_BASE + 0x00d0bd78)
#define Q_MGR_ENQ_INIT_DONE_OFFSET                                   (C_MODEL_BASE + 0x00d0bd74)
#define Q_MGR_ENQ_LENGTH_ADJUST_OFFSET                               (C_MODEL_BASE + 0x00d0bd38)
#define Q_MGR_ENQ_LINK_DOWN_SCAN_STATE_OFFSET                        (C_MODEL_BASE + 0x00d0bd68)
#define Q_MGR_ENQ_LINK_STATE_OFFSET                                  (C_MODEL_BASE + 0x00d0bd60)
#define Q_MGR_ENQ_RAND_SEED_LOAD_FORCE_DROP_OFFSET                   (C_MODEL_BASE + 0x00d0bd90)
#define Q_MGR_ENQ_SCAN_LINK_DOWN_CHAN_RECORD_OFFSET                  (C_MODEL_BASE + 0x00d0bd40)
#define Q_MGR_ENQ_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET           (C_MODEL_BASE + 0x00d0bd84)
#define Q_MGR_NET_PKT_ADJ_OFFSET                                     (C_MODEL_BASE + 0x00d0bd58)
#define Q_MGR_QUE_DEQ_STATS_BASE_OFFSET                              (C_MODEL_BASE + 0x00d0bd8c)
#define Q_MGR_QUE_ENQ_STATS_BASE_OFFSET                              (C_MODEL_BASE + 0x00d0bd80)
#define Q_MGR_QUEUE_ID_MON_OFFSET                                    (C_MODEL_BASE + 0x00d0bce0)
#define Q_MGR_RAND_SEED_LOAD_OFFSET                                  (C_MODEL_BASE + 0x00d0bd7c)
#define Q_MGR_RESERVED_CHANNEL_RANGE_OFFSET                          (C_MODEL_BASE + 0x00d0bd48)
#define Q_WRITE_CTL_OFFSET                                           (C_MODEL_BASE + 0x00d0bb80)
#define Q_WRITE_SGMAC_CTL_OFFSET                                     (C_MODEL_BASE + 0x00d0bd70)
#define QUE_MIN_PROFILE_OFFSET                                       (C_MODEL_BASE + 0x00d0bcc0)
#define DS_CHAN_ACTIVE_LINK_OFFSET                                   (C_MODEL_BASE + 0x00db5000)
#define DS_CHAN_BACKUP_LINK_OFFSET                                   (C_MODEL_BASE + 0x00db5800)
#define DS_CHAN_CREDIT_OFFSET                                        (C_MODEL_BASE + 0x00dba900)
#define DS_CHAN_SHP_OLD_TOKEN_OFFSET                                 (C_MODEL_BASE + 0x00dba700)
#define DS_CHAN_SHP_PROFILE_OFFSET                                   (C_MODEL_BASE + 0x00dba000)
#define DS_CHAN_SHP_TOKEN_OFFSET                                     (C_MODEL_BASE + 0x00dbaa00)
#define DS_CHAN_STATE_OFFSET                                         (C_MODEL_BASE + 0x00dbab00)
#define DS_DLB_TOKEN_THRD_OFFSET                                     (C_MODEL_BASE + 0x00dbac00)
#define DS_GRP_FWD_LINK_LIST0_OFFSET                                 (C_MODEL_BASE + 0x00db9c00)
#define DS_GRP_FWD_LINK_LIST1_OFFSET                                 (C_MODEL_BASE + 0x00db9800)
#define DS_GRP_FWD_LINK_LIST2_OFFSET                                 (C_MODEL_BASE + 0x00db9400)
#define DS_GRP_FWD_LINK_LIST3_OFFSET                                 (C_MODEL_BASE + 0x00db9000)
#define DS_GRP_NEXT_LINK_LIST0_OFFSET                                (C_MODEL_BASE + 0x00db8400)
#define DS_GRP_NEXT_LINK_LIST1_OFFSET                                (C_MODEL_BASE + 0x00db7400)
#define DS_GRP_NEXT_LINK_LIST2_OFFSET                                (C_MODEL_BASE + 0x00db6c00)
#define DS_GRP_NEXT_LINK_LIST3_OFFSET                                (C_MODEL_BASE + 0x00db6800)
#define DS_GRP_SHP_PROFILE_OFFSET                                    (C_MODEL_BASE + 0x00db3000)
#define DS_GRP_SHP_STATE_OFFSET                                      (C_MODEL_BASE + 0x00dba400)
#define DS_GRP_SHP_TOKEN_OFFSET                                      (C_MODEL_BASE + 0x00dac000)
#define DS_GRP_SHP_WFQ_CTL_OFFSET                                    (C_MODEL_BASE + 0x00da0000)
#define DS_GRP_STATE_OFFSET                                          (C_MODEL_BASE + 0x00db8c00)
#define DS_GRP_WFQ_DEFICIT_OFFSET                                    (C_MODEL_BASE + 0x00db0000)
#define DS_GRP_WFQ_STATE_OFFSET                                      (C_MODEL_BASE + 0x00db7000)
#define DS_OOB_FC_PRI_STATE_OFFSET                                   (C_MODEL_BASE + 0x00db8800)
#define DS_PACKET_LINK_LIST_OFFSET                                   (C_MODEL_BASE + 0x00d80000)
#define DS_PACKET_LINK_STATE_OFFSET                                  (C_MODEL_BASE + 0x00da4000)
#define DS_QUE_ENTRY_AGING_OFFSET                                    (C_MODEL_BASE + 0x00dbb000)
#define DS_QUE_MAP_OFFSET                                            (C_MODEL_BASE + 0x00db4000)
#define DS_QUE_SHP_CTL_OFFSET                                        (C_MODEL_BASE + 0x00db7800)
#define DS_QUE_SHP_PROFILE_OFFSET                                    (C_MODEL_BASE + 0x00dba200)
#define DS_QUE_SHP_STATE_OFFSET                                      (C_MODEL_BASE + 0x00db8000)
#define DS_QUE_SHP_TOKEN_OFFSET                                      (C_MODEL_BASE + 0x00db2000)
#define DS_QUE_SHP_WFQ_CTL_OFFSET                                    (C_MODEL_BASE + 0x00da8000)
#define DS_QUE_STATE_OFFSET                                          (C_MODEL_BASE + 0x00db7c00)
#define DS_QUE_WFQ_DEFICIT_OFFSET                                    (C_MODEL_BASE + 0x00dae000)
#define DS_QUE_WFQ_STATE_OFFSET                                      (C_MODEL_BASE + 0x00db6000)
#define Q_MGR_DEQ_INTERRUPT_FATAL_OFFSET                             (C_MODEL_BASE + 0x00dbadc0)
#define Q_MGR_DEQ_INTERRUPT_NORMAL_OFFSET                            (C_MODEL_BASE + 0x00dbad20)
#define Q_MGR_NETWORK_WT_CFG_MEM_OFFSET                              (C_MODEL_BASE + 0x00dba600)
#define Q_MGR_NETWORK_WT_MEM_OFFSET                                  (C_MODEL_BASE + 0x00dba800)
#define DS_CHAN_ACTIVE_LINK__REG_RAM__RAM_CHK_REC_OFFSET             (C_MODEL_BASE + 0x00dbaeb0)
#define DS_CHAN_BACKUP_LINK__REG_RAM__RAM_CHK_REC_OFFSET             (C_MODEL_BASE + 0x00dbaea0)
#define DS_CHAN_CREDIT__REG_RAM__RAM_CHK_REC_OFFSET                  (C_MODEL_BASE + 0x00dbaebc)
#define DS_CHAN_SHP_OLD_TOKEN__REG_RAM__RAM_CHK_REC_OFFSET           (C_MODEL_BASE + 0x00dbae70)
#define DS_CHAN_SHP_PROFILE__REG_RAM__RAM_CHK_REC_OFFSET             (C_MODEL_BASE + 0x00dbae5c)
#define DS_CHAN_SHP_TOKEN__REG_RAM__RAM_CHK_REC_OFFSET               (C_MODEL_BASE + 0x00dbae80)
#define DS_GRP_FWD_LINK_LIST0__REG_RAM__RAM_CHK_REC_OFFSET           (C_MODEL_BASE + 0x00dbae7c)
#define DS_GRP_FWD_LINK_LIST1__REG_RAM__RAM_CHK_REC_OFFSET           (C_MODEL_BASE + 0x00dbae84)
#define DS_GRP_FWD_LINK_LIST2__REG_RAM__RAM_CHK_REC_OFFSET           (C_MODEL_BASE + 0x00dbae88)
#define DS_GRP_FWD_LINK_LIST3__REG_RAM__RAM_CHK_REC_OFFSET           (C_MODEL_BASE + 0x00dbae90)
#define DS_GRP_NEXT_LINK_LIST0__REG_RAM__RAM_CHK_REC_OFFSET          (C_MODEL_BASE + 0x00dbae8c)
#define DS_GRP_NEXT_LINK_LIST1__REG_RAM__RAM_CHK_REC_OFFSET          (C_MODEL_BASE + 0x00dbae78)
#define DS_GRP_NEXT_LINK_LIST2__REG_RAM__RAM_CHK_REC_OFFSET          (C_MODEL_BASE + 0x00dbae74)
#define DS_GRP_NEXT_LINK_LIST3__REG_RAM__RAM_CHK_REC_OFFSET          (C_MODEL_BASE + 0x00dbae60)
#define DS_GRP_SHP_PROFILE__REG_RAM__RAM_CHK_REC_OFFSET              (C_MODEL_BASE + 0x00dbc000)
#define DS_GRP_SHP_STATE__REG_RAM__RAM_CHK_REC_OFFSET                (C_MODEL_BASE + 0x00dbae64)
#define DS_GRP_SHP_TOKEN__REG_RAM__RAM_CHK_REC_OFFSET                (C_MODEL_BASE + 0x00dbae68)
#define DS_GRP_SHP_WFQ_CTL__REG_RAM__RAM_CHK_REC_OFFSET              (C_MODEL_BASE + 0x00dbae6c)
#define DS_GRP_STATE__REG_RAM__RAM_CHK_REC_OFFSET                    (C_MODEL_BASE + 0x00dbae94)
#define DS_GRP_WFQ_DEFICIT__REG_RAM__RAM_CHK_REC_OFFSET              (C_MODEL_BASE + 0x00dbae98)
#define DS_OOB_FC_PRI_STATE__REG_RAM__RAM_CHK_REC_OFFSET             (C_MODEL_BASE + 0x00dbaec0)
#define DS_PACKET_LINK_LIST__REG_RAM__RAM_CHK_REC_OFFSET             (C_MODEL_BASE + 0x00dbaec4)
#define DS_PACKET_LINK_STATE__REG_RAM__RAM_CHK_REC_OFFSET            (C_MODEL_BASE + 0x00dbaec8)
#define DS_QUE_MAP__REG_RAM__RAM_CHK_REC_OFFSET                      (C_MODEL_BASE + 0x00dbaecc)
#define DS_QUE_SHP_CTL__REG_RAM__RAM_CHK_REC_OFFSET                  (C_MODEL_BASE + 0x00dbaeb8)
#define DS_QUE_SHP_PROFILE__REG_RAM__RAM_CHK_REC_OFFSET              (C_MODEL_BASE + 0x00dbae58)
#define DS_QUE_SHP_STATE__REG_RAM__RAM_CHK_REC_OFFSET                (C_MODEL_BASE + 0x00dbaeb4)
#define DS_QUE_SHP_TOKEN__REG_RAM__RAM_CHK_REC_OFFSET                (C_MODEL_BASE + 0x00dbae9c)
#define DS_QUE_SHP_WFQ_CTL__REG_RAM__RAM_CHK_REC_OFFSET              (C_MODEL_BASE + 0x00dbaea4)
#define DS_QUE_STATE__REG_RAM__RAM_CHK_REC_OFFSET                    (C_MODEL_BASE + 0x00dbaea8)
#define DS_QUE_WFQ_DEFICIT__REG_RAM__RAM_CHK_REC_OFFSET              (C_MODEL_BASE + 0x00dbaeac)
#define DS_QUE_WFQ_STATE__REG_RAM__RAM_CHK_REC_OFFSET                (C_MODEL_BASE + 0x00dbaed0)
#define Q_DLB_CHAN_SPEED_MODE_CTL_OFFSET                             (C_MODEL_BASE + 0x00dbad80)
#define Q_MGR_AGING_CTL_OFFSET                                       (C_MODEL_BASE + 0x00dbadb0)
#define Q_MGR_AGING_MEM_INIT_CTL_OFFSET                              (C_MODEL_BASE + 0x00dbae18)
#define Q_MGR_CHAN_FLUSH_CTL_OFFSET                                  (C_MODEL_BASE + 0x00dbae20)
#define Q_MGR_CHAN_SHAPE_CTL_OFFSET                                  (C_MODEL_BASE + 0x00dbad40)
#define Q_MGR_CHAN_SHAPE_STATE_OFFSET                                (C_MODEL_BASE + 0x00dbadf0)
#define Q_MGR_DEQ_DEBUG_STATS_OFFSET                                 (C_MODEL_BASE + 0x00dbad00)
#define Q_MGR_DEQ_ECC_CTL_OFFSET                                     (C_MODEL_BASE + 0x00dbae54)
#define Q_MGR_DEQ_FIFO_DEPTH_OFFSET                                  (C_MODEL_BASE + 0x00dbae4c)
#define Q_MGR_DEQ_FREE_PKT_LIST_CTL_OFFSET                           (C_MODEL_BASE + 0x00dbae30)
#define Q_MGR_DEQ_INIT_CTL_OFFSET                                    (C_MODEL_BASE + 0x00dbae00)
#define Q_MGR_DEQ_PARITY_ENABLE_OFFSET                               (C_MODEL_BASE + 0x00dbae38)
#define Q_MGR_DEQ_PKT_LINK_LIST_INIT_CTL_OFFSET                      (C_MODEL_BASE + 0x00dbae10)
#define Q_MGR_DEQ_PRI_TO_COS_MAP_OFFSET                              (C_MODEL_BASE + 0x00dbae50)
#define Q_MGR_DEQ_SCH_CTL_OFFSET                                     (C_MODEL_BASE + 0x00dbae48)
#define Q_MGR_DEQ_STALL_INIT_CTL_OFFSET                              (C_MODEL_BASE + 0x00dbae08)
#define Q_MGR_DLB_CTL_OFFSET                                         (C_MODEL_BASE + 0x00dbae44)
#define Q_MGR_DRAIN_CTL_OFFSET                                       (C_MODEL_BASE + 0x00dbad60)
#define Q_MGR_FREE_LIST_FIFO_CREDIT_CTL_OFFSET                       (C_MODEL_BASE + 0x00dbadf8)
#define Q_MGR_GRP_SHAPE_CTL_OFFSET                                   (C_MODEL_BASE + 0x00dbac80)
#define Q_MGR_INT_LK_STALL_CTL_OFFSET                                (C_MODEL_BASE + 0x00dbae3c)
#define Q_MGR_INTF_WRR_WT_CTL_OFFSET                                 (C_MODEL_BASE + 0x00dbadd0)
#define Q_MGR_MISC_INTF_WRR_WT_CTL_OFFSET                            (C_MODEL_BASE + 0x00dbade0)
#define Q_MGR_OOB_FC_CTL_OFFSET                                      (C_MODEL_BASE + 0x00dbae40)
#define Q_MGR_OOB_FC_STATUS_OFFSET                                   (C_MODEL_BASE + 0x00dbacc0)
#define Q_MGR_QUE_ENTRY_CREDIT_CTL_OFFSET                            (C_MODEL_BASE + 0x00dbae28)
#define Q_MGR_QUE_SHAPE_CTL_OFFSET                                   (C_MODEL_BASE + 0x00dbada0)
#define DS_QUEUE_ENTRY_OFFSET                                        (C_MODEL_BASE + 0x00e00000)
#define Q_MGR_QUE_ENTRY_INTERRUPT_FATAL_OFFSET                       (C_MODEL_BASE + 0x00e40000)
#define Q_MGR_QUE_ENTRY_INTERRUPT_NORMAL_OFFSET                      (C_MODEL_BASE + 0x00e40010)
#define Q_MGR_QUE_ENTRY_DS_CHECK_FAIL_RECORD_OFFSET                  (C_MODEL_BASE + 0x00e40030)
#define Q_MGR_QUE_ENTRY_ECC_CTL_OFFSET                               (C_MODEL_BASE + 0x00e40034)
#define Q_MGR_QUE_ENTRY_INIT_CTL_OFFSET                              (C_MODEL_BASE + 0x00e40020)
#define Q_MGR_QUE_ENTRY_REFRESH_CTL_OFFSET                           (C_MODEL_BASE + 0x00e4002c)
#define Q_MGR_QUE_ENTRY_STATS_OFFSET                                 (C_MODEL_BASE + 0x00e40028)
#define DS_MP_OFFSET                                                 (C_MODEL_BASE + 0x01000000)
#define DS_OAM_DEFECT_STATUS_OFFSET                                  (C_MODEL_BASE + 0x01080400)
#define DS_PORT_PROPERTY_OFFSET                                      (C_MODEL_BASE + 0x01080000)
#define DS_PRIORITY_MAP_OFFSET                                       (C_MODEL_BASE + 0x01080a00)
#define OAM_DEFECT_CACHE_OFFSET                                      (C_MODEL_BASE + 0x01080940)
#define OAM_PROC_INTERRUPT_FATAL_OFFSET                              (C_MODEL_BASE + 0x01080a60)
#define OAM_PROC_INTERRUPT_NORMAL_OFFSET                             (C_MODEL_BASE + 0x01080aa0)
#define OAM_RX_PROC_P_I_IN_FIFO_OFFSET                               (C_MODEL_BASE + 0x01080a20)
#define OAM_RX_PROC_P_R_IN_FIFO_OFFSET                               (C_MODEL_BASE + 0x01080800)
#define OAM_DEFECT_DEBUG_OFFSET                                      (C_MODEL_BASE + 0x01080c00)
#define OAM_DS_MP_DATA_MASK_OFFSET                                   (C_MODEL_BASE + 0x01080980)
#define OAM_ERROR_DEFECT_CTL_OFFSET                                  (C_MODEL_BASE + 0x010809a0)
#define OAM_ETHER_CCI_CTL_OFFSET                                     (C_MODEL_BASE + 0x01080a80)
#define OAM_ETHER_SEND_ID_OFFSET                                     (C_MODEL_BASE + 0x01080a90)
#define OAM_ETHER_TX_CTL_OFFSET                                      (C_MODEL_BASE + 0x01080900)
#define OAM_ETHER_TX_MAC_OFFSET                                      (C_MODEL_BASE + 0x01080ac8)
#define OAM_PROC_CTL_OFFSET                                          (C_MODEL_BASE + 0x01080ab8)
#define OAM_PROC_DEBUG_STATS_OFFSET                                  (C_MODEL_BASE + 0x01080a70)
#define OAM_PROC_ECC_CTL_OFFSET                                      (C_MODEL_BASE + 0x01080ad0)
#define OAM_PROC_ECC_STATS_OFFSET                                    (C_MODEL_BASE + 0x01080acc)
#define OAM_RX_PROC_ETHER_CTL_OFFSET                                 (C_MODEL_BASE + 0x010809c0)
#define OAM_TBL_ADDR_CTL_OFFSET                                      (C_MODEL_BASE + 0x01080ac0)
#define OAM_UPDATE_APS_CTL_OFFSET                                    (C_MODEL_BASE + 0x010809e0)
#define OAM_UPDATE_CTL_OFFSET                                        (C_MODEL_BASE + 0x01080a40)
#define OAM_UPDATE_STATUS_OFFSET                                     (C_MODEL_BASE + 0x01080ab0)
#define TCAM_DS_INTERRUPT_FATAL_OFFSET                               (C_MODEL_BASE + 0x011401c0)
#define TCAM_DS_INTERRUPT_NORMAL_OFFSET                              (C_MODEL_BASE + 0x011401d0)
#define TCAM_DS_RAM4_W_BUS_OFFSET                                    (C_MODEL_BASE + 0x01120000)
#define TCAM_DS_RAM8_W_BUS_OFFSET                                    (C_MODEL_BASE + 0x01100000)
#define TCAM_DS_ARB_WEIGHT_OFFSET                                    (C_MODEL_BASE + 0x01140204)
#define TCAM_DS_CREDIT_CTL_OFFSET                                    (C_MODEL_BASE + 0x011401e8)
#define TCAM_DS_DEBUG_STATS_OFFSET                                   (C_MODEL_BASE + 0x011401f8)
#define TCAM_DS_MEM_ALLOCATION_FAIL_OFFSET                           (C_MODEL_BASE + 0x011401f0)
#define TCAM_DS_MISC_CTL_OFFSET                                      (C_MODEL_BASE + 0x01140200)
#define TCAM_DS_RAM_INIT_CTL_OFFSET                                  (C_MODEL_BASE + 0x01140208)
#define TCAM_DS_RAM_PARITY_FAIL_OFFSET                               (C_MODEL_BASE + 0x011401e0)
#define TCAM_ENGINE_LOOKUP_RESULT_CTL0_OFFSET                        (C_MODEL_BASE + 0x01140080)
#define TCAM_ENGINE_LOOKUP_RESULT_CTL1_OFFSET                        (C_MODEL_BASE + 0x01140000)
#define TCAM_ENGINE_LOOKUP_RESULT_CTL2_OFFSET                        (C_MODEL_BASE + 0x01140100)
#define TCAM_ENGINE_LOOKUP_RESULT_CTL3_OFFSET                        (C_MODEL_BASE + 0x01140180)
#define TCAM_CTL_INT_CPU_REQUEST_MEM_OFFSET                          (C_MODEL_BASE + 0x01180000)
#define TCAM_CTL_INT_CPU_RESULT_MEM_OFFSET                           (C_MODEL_BASE + 0x01180800)
#define TCAM_CTL_INT_INTERRUPT_FATAL_OFFSET                          (C_MODEL_BASE + 0x01180c30)
#define TCAM_CTL_INT_INTERRUPT_NORMAL_OFFSET                         (C_MODEL_BASE + 0x01180c10)
#define TCAM_CTL_INT_BIST_CTL_OFFSET                                 (C_MODEL_BASE + 0x01180c80)
#define TCAM_CTL_INT_BIST_POINTERS_OFFSET                            (C_MODEL_BASE + 0x01180c78)
#define TCAM_CTL_INT_CAPTURE_RESULT_OFFSET                           (C_MODEL_BASE + 0x01180c7c)
#define TCAM_CTL_INT_CPU_ACCESS_CMD_OFFSET                           (C_MODEL_BASE + 0x01180c88)
#define TCAM_CTL_INT_CPU_RD_DATA_OFFSET                              (C_MODEL_BASE + 0x01180c50)
#define TCAM_CTL_INT_CPU_WR_DATA_OFFSET                              (C_MODEL_BASE + 0x01180c00)
#define TCAM_CTL_INT_CPU_WR_MASK_OFFSET                              (C_MODEL_BASE + 0x01180c40)
#define TCAM_CTL_INT_DEBUG_STATS_OFFSET                              (C_MODEL_BASE + 0x01180c84)
#define TCAM_CTL_INT_INIT_CTRL_OFFSET                                (C_MODEL_BASE + 0x01180c60)
#define TCAM_CTL_INT_KEY_SIZE_CFG_OFFSET                             (C_MODEL_BASE + 0x01180c8c)
#define TCAM_CTL_INT_KEY_TYPE_CFG_OFFSET                             (C_MODEL_BASE + 0x01180c68)
#define TCAM_CTL_INT_MISC_CTRL_OFFSET                                (C_MODEL_BASE + 0x01180c70)
#define TCAM_CTL_INT_STATE_OFFSET                                    (C_MODEL_BASE + 0x01180c74)
#define TCAM_CTL_INT_WRAP_SETTING_OFFSET                             (C_MODEL_BASE + 0x01180c20)
#define DS_AGING_OFFSET                                              (C_MODEL_BASE + 0x01190000)
#define DS_AGING_INTERRUPT_NORMAL_OFFSET                             (C_MODEL_BASE + 0x01198040)
#define DS_AGING_PTR_OFFSET                                          (C_MODEL_BASE + 0x01198054)
#define DS_AGING_DEBUG_STATS_OFFSET                                  (C_MODEL_BASE + 0x0119805c)
#define DS_AGING_DEBUG_STATUS_OFFSET                                 (C_MODEL_BASE + 0x01198060)
#define DS_AGING_FIFO_AFULL_THRD_OFFSET                              (C_MODEL_BASE + 0x01198064)
#define DS_AGING_INIT_OFFSET                                         (C_MODEL_BASE + 0x01198058)
#define DS_AGING_INIT_DONE_OFFSET                                    (C_MODEL_BASE + 0x01198068)
#define DS_AGING_PTR_FIFO_DEPTH_OFFSET                               (C_MODEL_BASE + 0x01198050)
#define IPE_AGING_CTL_OFFSET                                         (C_MODEL_BASE + 0x01198000)
#define HASH_DS_INTERRUPT_FATAL_OFFSET                               (C_MODEL_BASE + 0x011a0070)
#define HASH_DS_KEY0_LKUP_TRACK_FIFO_OFFSET                          (C_MODEL_BASE + 0x011a0100)
#define HASH_DS_AD_REQ_WT_CFG_OFFSET                                 (C_MODEL_BASE + 0x011a0080)
#define HASH_DS_CREDIT_CONFIG_OFFSET                                 (C_MODEL_BASE + 0x011a0040)
#define HASH_DS_CREDIT_USED_OFFSET                                   (C_MODEL_BASE + 0x011a0020)
#define HASH_DS_DEBUG_STATS_OFFSET                                   (C_MODEL_BASE + 0x011a0000)
#define HASH_DS_KEY0_LKUP_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET   (C_MODEL_BASE + 0x011a0090)
#define HASH_DS_KEY1_LKUP_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET   (C_MODEL_BASE + 0x011a008c)
#define HASH_DS_KEY_LKUP_RESULT_DEBUG_INFO_OFFSET                    (C_MODEL_BASE + 0x011a00a0)
#define HASH_DS_KEY_REQ_WT_CFG_OFFSET                                (C_MODEL_BASE + 0x011a0088)
#define USER_ID_HASH_LOOKUP_CTL_OFFSET                               (C_MODEL_BASE + 0x011a0084)
#define USER_ID_RESULT_CTL_OFFSET                                    (C_MODEL_BASE + 0x011a0060)
#define OAM_PARSER_INTERRUPT_FATAL_OFFSET                            (C_MODEL_BASE + 0x011b0060)
#define OAM_PARSER_INTERRUPT_NORMAL_OFFSET                           (C_MODEL_BASE + 0x011b0090)
#define OAM_PARSER_PKT_FIFO_OFFSET                                   (C_MODEL_BASE + 0x011b0200)
#define OAM_HEADER_ADJUST_CTL_OFFSET                                 (C_MODEL_BASE + 0x011b008c)
#define OAM_PARSER_CTL_OFFSET                                        (C_MODEL_BASE + 0x011b0088)
#define OAM_PARSER_DEBUG_CNT_OFFSET                                  (C_MODEL_BASE + 0x011b0078)
#define OAM_PARSER_ETHER_CTL_OFFSET                                  (C_MODEL_BASE + 0x011b0000)
#define OAM_PARSER_FLOW_CTL_OFFSET                                   (C_MODEL_BASE + 0x011b0070)
#define OAM_PARSER_LAYER2_PROTOCOL_CAM_OFFSET                        (C_MODEL_BASE + 0x011b0040)
#define OAM_PARSER_LAYER2_PROTOCOL_CAM_VALID_OFFSET                  (C_MODEL_BASE + 0x011b0084)
#define OAM_PARSER_PKT_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET            (C_MODEL_BASE + 0x011b0080)
#define DS_OAM_EXCP_OFFSET                                           (C_MODEL_BASE + 0x011c0500)
#define OAM_FWD_INTERRUPT_FATAL_OFFSET                               (C_MODEL_BASE + 0x011c05e0)
#define OAM_FWD_INTERRUPT_NORMAL_OFFSET                              (C_MODEL_BASE + 0x011c05c0)
#define OAM_HDR_EDIT_P_D_IN_FIFO_OFFSET                              (C_MODEL_BASE + 0x011c0000)
#define OAM_HDR_EDIT_P_I_IN_FIFO_OFFSET                              (C_MODEL_BASE + 0x011c0400)
#define OAM_FWD_CREDIT_CTL_OFFSET                                    (C_MODEL_BASE + 0x011c05f0)
#define OAM_FWD_DEBUG_STATS_OFFSET                                   (C_MODEL_BASE + 0x011c0580)
#define OAM_HDR_EDIT_CREDIT_CTL_OFFSET                               (C_MODEL_BASE + 0x011c05f8)
#define OAM_HDR_EDIT_DEBUG_STATS_OFFSET                              (C_MODEL_BASE + 0x011c05d0)
#define OAM_HDR_EDIT_DRAIN_ENABLE_OFFSET                             (C_MODEL_BASE + 0x011c05fc)
#define OAM_HDR_EDIT_PARITY_ENABLE_OFFSET                            (C_MODEL_BASE + 0x011c05f4)
#define OAM_HEADER_EDIT_CTL_OFFSET                                   (C_MODEL_BASE + 0x011c05a0)
#define AUTO_GEN_PKT_INTERRUPT_NORMAL_OFFSET                         (C_MODEL_BASE + 0x011d0300)
#define AUTO_GEN_PKT_PKT_CFG_OFFSET                                  (C_MODEL_BASE + 0x011d0200)
#define AUTO_GEN_PKT_PKT_HDR_OFFSET                                  (C_MODEL_BASE + 0x011d0000)
#define AUTO_GEN_PKT_RX_PKT_STATS_OFFSET                             (C_MODEL_BASE + 0x011d0280)
#define AUTO_GEN_PKT_TX_PKT_STATS_OFFSET                             (C_MODEL_BASE + 0x011d02c0)
#define AUTO_GEN_PKT_CREDIT_CTL_OFFSET                               (C_MODEL_BASE + 0x011d0338)
#define AUTO_GEN_PKT_CTL_OFFSET                                      (C_MODEL_BASE + 0x011d0320)
#define AUTO_GEN_PKT_DEBUG_STATS_OFFSET                              (C_MODEL_BASE + 0x011d0328)
#define AUTO_GEN_PKT_ECC_CTL_OFFSET                                  (C_MODEL_BASE + 0x011d032c)
#define AUTO_GEN_PKT_ECC_STATS_OFFSET                                (C_MODEL_BASE + 0x011d0334)
#define AUTO_GEN_PKT_PRBS_CTL_OFFSET                                 (C_MODEL_BASE + 0x011d0330)
#define FIB_HASH_AD_REQ_FIFO_OFFSET                                  (C_MODEL_BASE + 0x011e3400)
#define FIB_KEY_TRACK_FIFO_OFFSET                                    (C_MODEL_BASE + 0x011e3800)
#define FIB_LEARN_FIFO_RESULT_OFFSET                                 (C_MODEL_BASE + 0x011e21a0)
#define FIB_LKUP_MGR_REQ_FIFO_OFFSET                                 (C_MODEL_BASE + 0x011e2400)
#define IPE_FIB_INTERRUPT_FATAL_OFFSET                               (C_MODEL_BASE + 0x011e21e0)
#define IPE_FIB_INTERRUPT_NORMAL_OFFSET                              (C_MODEL_BASE + 0x011e21f0)
#define LKUP_MGR_LPM_KEY_REQ_FIFO_OFFSET                             (C_MODEL_BASE + 0x011e2600)
#define LPM_AD_REQ_FIFO_OFFSET                                       (C_MODEL_BASE + 0x011e3600)
#define LPM_FINAL_TRACK_FIFO_OFFSET                                  (C_MODEL_BASE + 0x011e3300)
#define LPM_HASH_KEY_TRACK_FIFO_OFFSET                               (C_MODEL_BASE + 0x011e2800)
#define LPM_HASH_RESULT_FIFO_OFFSET                                  (C_MODEL_BASE + 0x011e2a00)
#define LPM_PIPE0_REQ_FIFO_OFFSET                                    (C_MODEL_BASE + 0x011e3000)
#define LPM_PIPE1_REQ_FIFO_OFFSET                                    (C_MODEL_BASE + 0x011e3100)
#define LPM_PIPE2_REQ_FIFO_OFFSET                                    (C_MODEL_BASE + 0x011e3200)
#define LPM_PIPE3_REQ_FIFO_OFFSET                                    (C_MODEL_BASE + 0x011e3280)
#define LPM_TCAM_AD_MEM_OFFSET                                       (C_MODEL_BASE + 0x011e0000)
#define LPM_TCAM_AD_REQ_FIFO_OFFSET                                  (C_MODEL_BASE + 0x011e2380)
#define LPM_TCAM_KEY_REQ_FIFO_OFFSET                                 (C_MODEL_BASE + 0x011e2300)
#define LPM_TCAM_RESULT_FIFO_OFFSET                                  (C_MODEL_BASE + 0x011e2a80)
#define LPM_TRACK_FIFO_OFFSET                                        (C_MODEL_BASE + 0x011e2c00)
#define FIB_AD_RD_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET           (C_MODEL_BASE + 0x011e2284)
#define FIB_CREDIT_CTL_OFFSET                                        (C_MODEL_BASE + 0x011e2080)
#define FIB_CREDIT_DEBUG_OFFSET                                      (C_MODEL_BASE + 0x011e2160)
#define FIB_DEBUG_STATS_OFFSET                                       (C_MODEL_BASE + 0x011e2210)
#define FIB_ECC_CTL_OFFSET                                           (C_MODEL_BASE + 0x011e2274)
#define FIB_ENGINE_HASH_CTL_OFFSET                                   (C_MODEL_BASE + 0x011e2270)
#define FIB_ENGINE_LOOKUP_CTL_OFFSET                                 (C_MODEL_BASE + 0x011e2180)
#define FIB_ENGINE_LOOKUP_RESULT_CTL_OFFSET                          (C_MODEL_BASE + 0x011e2000)
#define FIB_ENGINE_LOOKUP_RESULT_CTL1_OFFSET                         (C_MODEL_BASE + 0x011e2238)
#define FIB_HASH_KEY_CPU_REQ_OFFSET                                  (C_MODEL_BASE + 0x011e2140)
#define FIB_HASH_KEY_CPU_RESULT_OFFSET                               (C_MODEL_BASE + 0x011e2200)
#define FIB_INIT_CTL_OFFSET                                          (C_MODEL_BASE + 0x011e2230)
#define FIB_KEY_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET             (C_MODEL_BASE + 0x011e226c)
#define FIB_LEARN_FIFO_CTL_OFFSET                                    (C_MODEL_BASE + 0x011e2228)
#define FIB_LEARN_FIFO_INFO_OFFSET                                   (C_MODEL_BASE + 0x011e2268)
#define FIB_LRN_KEY_WR_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET            (C_MODEL_BASE + 0x011e2278)
#define FIB_MAC_HASH_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET        (C_MODEL_BASE + 0x011e227c)
#define FIB_PARITY_CTL_OFFSET                                        (C_MODEL_BASE + 0x011e2288)
#define FIB_TCAM_CPU_ACCESS_OFFSET                                   (C_MODEL_BASE + 0x011e2264)
#define FIB_TCAM_INIT_CTL_OFFSET                                     (C_MODEL_BASE + 0x011e2220)
#define FIB_TCAM_READ_DATA_OFFSET                                    (C_MODEL_BASE + 0x011e21b0)
#define FIB_TCAM_WRITE_DATA_OFFSET                                   (C_MODEL_BASE + 0x011e21d0)
#define FIB_TCAM_WRITE_MASK_OFFSET                                   (C_MODEL_BASE + 0x011e21c0)
#define LKUP_MGR_LPM_KEY_REQ_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET      (C_MODEL_BASE + 0x011e2280)
#define LPM_HASH2ND_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET         (C_MODEL_BASE + 0x011e228c)
#define LPM_HASH_KEY_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET        (C_MODEL_BASE + 0x011e224c)
#define LPM_HASH_LU_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET         (C_MODEL_BASE + 0x011e2248)
#define LPM_IPV6_HASH32_HIGH_KEY_CAM_OFFSET                          (C_MODEL_BASE + 0x011e2100)
#define LPM_PIPE0_REQ_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET             (C_MODEL_BASE + 0x011e2254)
#define LPM_PIPE3_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET           (C_MODEL_BASE + 0x011e225c)
#define LPM_TCAM_AD_MEM__REG_RAM__RAM_CHK_REC_OFFSET                 (C_MODEL_BASE + 0x011e2240)
#define LPM_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET                 (C_MODEL_BASE + 0x011e2244)
#define PORT_MAP_OFFSET                                              (C_MODEL_BASE + 0x01200200)
#define MDIO_CFG_OFFSET                                              (C_MODEL_BASE + 0x01200028)
#define MDIO_CMD1_G_OFFSET                                           (C_MODEL_BASE + 0x01200010)
#define MDIO_CMD_X_G_OFFSET                                          (C_MODEL_BASE + 0x01200018)
#define MDIO_LINK_DOWN_DETEC_EN_OFFSET                               (C_MODEL_BASE + 0x012000d0)
#define MDIO_LINK_STATUS_OFFSET                                      (C_MODEL_BASE + 0x01200040)
#define MDIO_SCAN_CTL_OFFSET                                         (C_MODEL_BASE + 0x01200030)
#define MDIO_SPECI_CFG_OFFSET                                        (C_MODEL_BASE + 0x012000c0)
#define MDIO_SPECIFIED_STATUS_OFFSET                                 (C_MODEL_BASE + 0x01200080)
#define MDIO_STATUS1_G_OFFSET                                        (C_MODEL_BASE + 0x01200020)
#define MDIO_STATUS_X_G_OFFSET                                       (C_MODEL_BASE + 0x01200024)
#define MDIO_USE_PHY_OFFSET                                          (C_MODEL_BASE + 0x012000c8)
#define MDIO_XG_PORT_CHAN_ID_WITH_OUT_PHY_OFFSET                     (C_MODEL_BASE + 0x012000e0)
#define DS_STATS_OFFSET                                              (C_MODEL_BASE + 0x01600000)
#define E_E_E_STATS_RAM_OFFSET                                       (C_MODEL_BASE + 0x01502000)
#define GLOBAL_STATS_INTERRUPT_FATAL_OFFSET                          (C_MODEL_BASE + 0x01400000)
#define GLOBAL_STATS_INTERRUPT_NORMAL_OFFSET                         (C_MODEL_BASE + 0x01400010)
#define GLOBAL_STATS_SATU_ADDR_OFFSET                                (C_MODEL_BASE + 0x014000a8)
#define RX_INBD_STATS_RAM_OFFSET                                     (C_MODEL_BASE + 0x01500000)
#define TX_INBD_STATS_EPE_RAM_OFFSET                                 (C_MODEL_BASE + 0x01501000)
#define TX_INBD_STATS_PAUSE_RAM_OFFSET                               (C_MODEL_BASE + 0x01501800)
#define E_E_E_STATS_MEM__RAM_CHK_REC_OFFSET                          (C_MODEL_BASE + 0x014000e8)
#define GLOBAL_STATS_BYTE_CNT_THRESHOLD_OFFSET                       (C_MODEL_BASE + 0x014000d4)
#define GLOBAL_STATS_CREDIT_CTL_OFFSET                               (C_MODEL_BASE + 0x01400090)
#define GLOBAL_STATS_CTL_OFFSET                                      (C_MODEL_BASE + 0x014000d0)
#define GLOBAL_STATS_DBG_STATUS_OFFSET                               (C_MODEL_BASE + 0x014000cc)
#define GLOBAL_STATS_E_E_E_CALENDAR_OFFSET                           (C_MODEL_BASE + 0x01400040)
#define GLOBAL_STATS_E_E_E_CTL_OFFSET                                (C_MODEL_BASE + 0x01400080)
#define GLOBAL_STATS_EPE_BASE_ADDR_OFFSET                            (C_MODEL_BASE + 0x014000c8)
#define GLOBAL_STATS_EPE_REQ_DROP_CNT_OFFSET                         (C_MODEL_BASE + 0x014000d8)
#define GLOBAL_STATS_IPE_BASE_ADDR_OFFSET                            (C_MODEL_BASE + 0x014000dc)
#define GLOBAL_STATS_IPE_REQ_DROP_CNT_OFFSET                         (C_MODEL_BASE + 0x014000f0)
#define GLOBAL_STATS_PARITY_FAIL_OFFSET                              (C_MODEL_BASE + 0x014000ec)
#define GLOBAL_STATS_PKT_CNT_THRESHOLD_OFFSET                        (C_MODEL_BASE + 0x014000c0)
#define GLOBAL_STATS_POLICING_BASE_ADDR_OFFSET                       (C_MODEL_BASE + 0x014000e0)
#define GLOBAL_STATS_POLICING_REQ_DROP_CNT_OFFSET                    (C_MODEL_BASE + 0x014000e4)
#define GLOBAL_STATS_Q_MGR_BASE_ADDR_OFFSET                          (C_MODEL_BASE + 0x014000f4)
#define GLOBAL_STATS_Q_MGR_REQ_DROP_CNT_OFFSET                       (C_MODEL_BASE + 0x014000b8)
#define GLOBAL_STATS_RAM_INIT_CTL_OFFSET                             (C_MODEL_BASE + 0x014000c4)
#define GLOBAL_STATS_RX_LPI_STATE_OFFSET                             (C_MODEL_BASE + 0x01400098)
#define GLOBAL_STATS_SATU_ADDR_FIFO_DEPTH_OFFSET                     (C_MODEL_BASE + 0x014000a0)
#define GLOBAL_STATS_SATU_ADDR_FIFO_DEPTH_THRESHOLD_OFFSET           (C_MODEL_BASE + 0x014000a4)
#define GLOBAL_STATS_WRR_CFG_OFFSET                                  (C_MODEL_BASE + 0x014000bc)
#define RX_INBD_STATS_MEM__RAM_CHK_REC_OFFSET                        (C_MODEL_BASE + 0x014000ac)
#define TX_INBD_STATS_EPE_MEM__RAM_CHK_REC_OFFSET                    (C_MODEL_BASE + 0x014000b4)
#define TX_INBD_STATS_PAUSE_MEM__RAM_CHK_REC_OFFSET                  (C_MODEL_BASE + 0x014000b0)
#define HDR_WR_REQ_FIFO_OFFSET                                       (C_MODEL_BASE + 0x02002000)
#define PB_CTL_HDR_BUF_OFFSET                                        (C_MODEL_BASE + 0x02100000)
#define PB_CTL_INTERRUPT_FATAL_OFFSET                                (C_MODEL_BASE + 0x02000000)
#define PB_CTL_INTERRUPT_NORMAL_OFFSET                               (C_MODEL_BASE + 0x02000010)
#define PB_CTL_PKT_BUF_OFFSET                                        (C_MODEL_BASE + 0x02400000)
#define PKT_WR_REQ_FIFO_OFFSET                                       (C_MODEL_BASE + 0x02001000)
#define PB_CTL_DEBUG_STATS_OFFSET                                    (C_MODEL_BASE + 0x02000030)
#define PB_CTL_ECC_CTL_OFFSET                                        (C_MODEL_BASE + 0x02000024)
#define PB_CTL_INIT_OFFSET                                           (C_MODEL_BASE + 0x02000050)
#define PB_CTL_INIT_DONE_OFFSET                                      (C_MODEL_BASE + 0x02000054)
#define PB_CTL_PARITY_CTL_OFFSET                                     (C_MODEL_BASE + 0x02000028)
#define PB_CTL_PARITY_FAIL_RECORD_OFFSET                             (C_MODEL_BASE + 0x02000048)
#define PB_CTL_RD_CREDIT_CTL_OFFSET                                  (C_MODEL_BASE + 0x0200002c)
#define PB_CTL_REF_CTL_OFFSET                                        (C_MODEL_BASE + 0x0200005c)
#define PB_CTL_REQ_HOLD_STATS_OFFSET                                 (C_MODEL_BASE + 0x02000038)
#define PB_CTL_WEIGHT_CFG_OFFSET                                     (C_MODEL_BASE + 0x02000020)
#define DYNAMIC_DS_EDRAM4_W_OFFSET                                   (C_MODEL_BASE + 0x03000000)
#define DYNAMIC_DS_EDRAM8_W_OFFSET                                   (C_MODEL_BASE + 0x03400000)
#define DYNAMIC_DS_FDB_LOOKUP_INDEX_CAM_OFFSET                       (C_MODEL_BASE + 0x03802800)
#define DYNAMIC_DS_INTERRUPT_FATAL_OFFSET                            (C_MODEL_BASE + 0x03802140)
#define DYNAMIC_DS_INTERRUPT_NORMAL_OFFSET                           (C_MODEL_BASE + 0x03802170)
#define DYNAMIC_DS_LM_STATS_RAM0_OFFSET                              (C_MODEL_BASE + 0x03801000)
#define DYNAMIC_DS_LM_STATS_RAM1_OFFSET                              (C_MODEL_BASE + 0x03800000)
#define DYNAMIC_DS_MAC_KEY_REQ_FIFO_OFFSET                           (C_MODEL_BASE + 0x03802300)
#define DYNAMIC_DS_OAM_LM_DRAM0_REQ_FIFO_OFFSET                      (C_MODEL_BASE + 0x03802600)
#define DYNAMIC_DS_OAM_LM_SRAM0_REQ_FIFO_OFFSET                      (C_MODEL_BASE + 0x03802400)
#define DYNAMIC_DS_OAM_LM_SRAM1_REQ_FIFO_OFFSET                      (C_MODEL_BASE + 0x03802500)
#define DYNAMIC_DS_CREDIT_CONFIG_OFFSET                              (C_MODEL_BASE + 0x03802240)
#define DYNAMIC_DS_CREDIT_USED_OFFSET                                (C_MODEL_BASE + 0x03802248)
#define DYNAMIC_DS_DEBUG_INFO_OFFSET                                 (C_MODEL_BASE + 0x03808010)
#define DYNAMIC_DS_DEBUG_STATS_OFFSET                                (C_MODEL_BASE + 0x038020e0)
#define DYNAMIC_DS_DS_EDIT_INDEX_CAM_OFFSET                          (C_MODEL_BASE + 0x038021d0)
#define DYNAMIC_DS_DS_MAC_IP_INDEX_CAM_OFFSET                        (C_MODEL_BASE + 0x038021e0)
#define DYNAMIC_DS_DS_MET_INDEX_CAM_OFFSET                           (C_MODEL_BASE + 0x038021c0)
#define DYNAMIC_DS_DS_MPLS_INDEX_CAM_OFFSET                          (C_MODEL_BASE + 0x03802210)
#define DYNAMIC_DS_DS_NEXT_HOP_INDEX_CAM_OFFSET                      (C_MODEL_BASE + 0x038021a0)
#define DYNAMIC_DS_DS_STATS_INDEX_CAM_OFFSET                         (C_MODEL_BASE + 0x03802180)
#define DYNAMIC_DS_DS_USER_ID_HASH_INDEX_CAM_OFFSET                  (C_MODEL_BASE + 0x03802274)
#define DYNAMIC_DS_DS_USER_ID_INDEX_CAM_OFFSET                       (C_MODEL_BASE + 0x03802200)
#define DYNAMIC_DS_ECC_CTL_OFFSET                                    (C_MODEL_BASE + 0x03802160)
#define DYNAMIC_DS_EDRAM_ECC_ERROR_RECORD_OFFSET                     (C_MODEL_BASE + 0x03802080)
#define DYNAMIC_DS_EDRAM_INIT_CTL_OFFSET                             (C_MODEL_BASE + 0x03802250)
#define DYNAMIC_DS_EDRAM_SELECT_OFFSET                               (C_MODEL_BASE + 0x038021f0)
#define DYNAMIC_DS_FDB_HASH_CTL_OFFSET                               (C_MODEL_BASE + 0x03802270)
#define DYNAMIC_DS_FDB_HASH_INDEX_CAM_OFFSET                         (C_MODEL_BASE + 0x038020c0)
#define DYNAMIC_DS_FDB_KEY_RESULT_DEBUG_OFFSET                       (C_MODEL_BASE + 0x03802700)
#define DYNAMIC_DS_FWD_INDEX_CAM_OFFSET                              (C_MODEL_BASE + 0x038021b0)
#define DYNAMIC_DS_LM_RD_REQ_TRACK_FIFO__FIFO_ALMOST_FULL_THRD_OFFSET (C_MODEL_BASE + 0x03802278)
#define DYNAMIC_DS_LM_STATS_INDEX_CAM_OFFSET                         (C_MODEL_BASE + 0x0380226c)
#define DYNAMIC_DS_LM_STATS_RAM0__REG_RAM__RAM_CHK_REC_OFFSET        (C_MODEL_BASE + 0x0380227c)
#define DYNAMIC_DS_LM_STATS_RAM1__REG_RAM__RAM_CHK_REC_OFFSET        (C_MODEL_BASE + 0x03802280)
#define DYNAMIC_DS_LPM_INDEX_CAM0_OFFSET                             (C_MODEL_BASE + 0x03802220)
#define DYNAMIC_DS_LPM_INDEX_CAM1_OFFSET                             (C_MODEL_BASE + 0x03802228)
#define DYNAMIC_DS_LPM_INDEX_CAM2_OFFSET                             (C_MODEL_BASE + 0x03802230)
#define DYNAMIC_DS_LPM_INDEX_CAM3_OFFSET                             (C_MODEL_BASE + 0x03802238)
#define DYNAMIC_DS_LPM_INDEX_CAM4_OFFSET                             (C_MODEL_BASE + 0x03802260)
#define DYNAMIC_DS_MAC_HASH_INDEX_CAM_OFFSET                         (C_MODEL_BASE + 0x03802100)
#define DYNAMIC_DS_OAM_INDEX_CAM_OFFSET                              (C_MODEL_BASE + 0x03802190)
#define DYNAMIC_DS_REF_INTERVAL_OFFSET                               (C_MODEL_BASE + 0x03802268)
#define DYNAMIC_DS_SRAM_INIT_OFFSET                                  (C_MODEL_BASE + 0x03802258)
#define DYNAMIC_DS_USER_ID_BASE_OFFSET                               (C_MODEL_BASE + 0x03808000)
#define DYNAMIC_DS_WRR_CREDIT_CFG_OFFSET                             (C_MODEL_BASE + 0x03802120)
#define LPM_TCAM_KEY_OFFSET                                          (C_MODEL_BASE + 0x11000000)
#define LPM_TCAM_MASK_OFFSET                                         (C_MODEL_BASE + 0x12000000)
#define TCAM_KEY_OFFSET                                              (C_MODEL_BASE + 0x10000000)
#define TCAM_MASK_OFFSET                                             (C_MODEL_BASE + 0x10400000)
#define DS_BFD_OAM_CHAN_TCAM_OFFSET                                  (C_MODEL_BASE + 0x50000000)
#define DS_BFD_OAM_HASH_KEY_OFFSET                                   (C_MODEL_BASE + 0x50000000)
#define DS_ETH_OAM_HASH_KEY_OFFSET                                   (C_MODEL_BASE + 0x50000000)
#define DS_ETH_OAM_RMEP_HASH_KEY_OFFSET                              (C_MODEL_BASE + 0x50000000)
#define DS_ETH_OAM_TCAM_CHAN_OFFSET                                  (C_MODEL_BASE + 0x50000000)
#define DS_ETH_OAM_TCAM_CHAN_CONFLICT_TCAM_OFFSET                    (C_MODEL_BASE + 0x50000000)
#define DS_MPLS_OAM_CHAN_TCAM_OFFSET                                 (C_MODEL_BASE + 0x50000000)
#define DS_MPLS_OAM_LABEL_HASH_KEY_OFFSET                            (C_MODEL_BASE + 0x50000000)
#define DS_MPLS_PBT_BFD_OAM_CHAN_OFFSET                              (C_MODEL_BASE + 0x50000000)
#define DS_PBT_OAM_CHAN_TCAM_OFFSET                                  (C_MODEL_BASE + 0x50000000)
#define DS_PBT_OAM_HASH_KEY_OFFSET                                   (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_OFFSET                                          (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_CAPWAP_HASH_KEY_OFFSET                          (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_CAPWAP_TCAM_OFFSET                              (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_CONFLICT_TCAM_OFFSET                            (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_DEFAULT_OFFSET                                  (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_IPV4_HASH_KEY_OFFSET                            (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_IPV4_RPF_HASH_KEY_OFFSET                        (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_IPV4_TCAM_OFFSET                                (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_IPV6_TCAM_OFFSET                                (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_PBB_HASH_KEY_OFFSET                             (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_PBB_TCAM_OFFSET                                 (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_TRILL_MC_ADJ_CHECK_HASH_KEY_OFFSET              (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_TRILL_MC_DECAP_HASH_KEY_OFFSET                  (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_TRILL_MC_RPF_HASH_KEY_OFFSET                    (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_TRILL_TCAM_OFFSET                               (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_TRILL_UC_DECAP_HASH_KEY_OFFSET                  (C_MODEL_BASE + 0x50000000)
#define DS_TUNNEL_ID_TRILL_UC_RPF_HASH_KEY_OFFSET                    (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_OFFSET                                            (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_CONFLICT_TCAM_OFFSET                              (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_CVLAN_COS_HASH_KEY_OFFSET                         (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_CVLAN_HASH_KEY_OFFSET                             (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_DOUBLE_VLAN_HASH_KEY_OFFSET                       (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_INGRESS_DEFAULT_OFFSET                            (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_IPV4_HASH_KEY_OFFSET                              (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_IPV4_PORT_HASH_KEY_OFFSET                         (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_IPV4_TCAM_OFFSET                                  (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_IPV6_HASH_KEY_OFFSET                              (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_IPV6_TCAM_OFFSET                                  (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_L2_HASH_KEY_OFFSET                                (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_MAC_HASH_KEY_OFFSET                               (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_MAC_PORT_HASH_KEY_OFFSET                          (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_MAC_TCAM_OFFSET                                   (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_PORT_CROSS_HASH_KEY_OFFSET                        (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_PORT_HASH_KEY_OFFSET                              (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_PORT_VLAN_CROSS_HASH_KEY_OFFSET                   (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_SVLAN_COS_HASH_KEY_OFFSET                         (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_SVLAN_HASH_KEY_OFFSET                             (C_MODEL_BASE + 0x50000000)
#define DS_USER_ID_VLAN_TCAM_OFFSET                                  (C_MODEL_BASE + 0x50000000)
#define DS_DEST_PTP_CHANNEL_OFFSET                                   (C_MODEL_BASE + 0x60006100)
#define DS_DLB_CHAN_IDLE_LEVEL_OFFSET                                (C_MODEL_BASE + 0x60006000)
#define DS_OAM_LM_STATS_OFFSET                                       (C_MODEL_BASE + 0x60000000)
#define DS_OAM_LM_STATS0_OFFSET                                      DYNAMIC_DS_LM_STATS_RAM0_OFFSET
#define DS_OAM_LM_STATS1_OFFSET                                      DYNAMIC_DS_LM_STATS_RAM1_OFFSET
#define DS_ACL_OFFSET                                                (C_MODEL_BASE + 0x60006200)
#define DS_ACL_QOS_IPV4_HASH_KEY_OFFSET                              (C_MODEL_BASE + 0x60006200)
#define DS_ACL_QOS_MAC_HASH_KEY_OFFSET                               (C_MODEL_BASE + 0x60006200)
#define DS_BFD_MEP_OFFSET                                            (C_MODEL_BASE + 0x60006200)
#define DS_BFD_RMEP_OFFSET                                           (C_MODEL_BASE + 0x60006200)
#define DS_ETH_MEP_OFFSET                                            (C_MODEL_BASE + 0x60006200)
#define DS_ETH_OAM_CHAN_OFFSET                                       (C_MODEL_BASE + 0x60006200)
#define DS_ETH_RMEP_OFFSET                                           (C_MODEL_BASE + 0x60006200)
#define DS_ETH_RMEP_CHAN_OFFSET                                      (C_MODEL_BASE + 0x60006200)
#define DS_ETH_RMEP_CHAN_CONFLICT_TCAM_OFFSET                        (C_MODEL_BASE + 0x60006200)
#define DS_FCOE_DA_OFFSET                                            (C_MODEL_BASE + 0x60006200)
#define DS_FCOE_DA_TCAM_OFFSET                                       (C_MODEL_BASE + 0x60006200)
#define DS_FCOE_HASH_KEY_OFFSET                                      (C_MODEL_BASE + 0x60006200)
#define DS_FCOE_RPF_HASH_KEY_OFFSET                                  (C_MODEL_BASE + 0x60006200)
#define DS_FCOE_SA_OFFSET                                            (C_MODEL_BASE + 0x60006200)
#define DS_FCOE_SA_TCAM_OFFSET                                       (C_MODEL_BASE + 0x60006200)
#define DS_FWD_OFFSET                                                (C_MODEL_BASE + 0x60006200)
#define DS_IP_DA_OFFSET                                              (C_MODEL_BASE + 0x60006200)
#define DS_IP_SA_NAT_OFFSET                                          (C_MODEL_BASE + 0x60006200)
#define DS_IPV4_ACL0_TCAM_OFFSET                                     (C_MODEL_BASE + 0x60006200)
#define DS_IPV4_ACL1_TCAM_OFFSET                                     (C_MODEL_BASE + 0x60006200)
#define DS_IPV4_ACL2_TCAM_OFFSET                                     (C_MODEL_BASE + 0x60006200)
#define DS_IPV4_ACL3_TCAM_OFFSET                                     (C_MODEL_BASE + 0x60006200)
#define DS_IPV4_MCAST_DA_TCAM_OFFSET                                 (C_MODEL_BASE + 0x60006200)
#define DS_IPV4_SA_NAT_TCAM_OFFSET                                   (C_MODEL_BASE + 0x60006200)
#define DS_IPV4_UCAST_DA_TCAM_OFFSET                                 (C_MODEL_BASE + 0x60006200)
#define DS_IPV4_UCAST_PBR_DUAL_DA_TCAM_OFFSET                        (C_MODEL_BASE + 0x60006200)
#define DS_IPV6_ACL0_TCAM_OFFSET                                     (C_MODEL_BASE + 0x60006200)
#define DS_IPV6_ACL1_TCAM_OFFSET                                     (C_MODEL_BASE + 0x60006200)
#define DS_IPV6_MCAST_DA_TCAM_OFFSET                                 (C_MODEL_BASE + 0x60006200)
#define DS_IPV6_SA_NAT_TCAM_OFFSET                                   (C_MODEL_BASE + 0x60006200)
#define DS_IPV6_UCAST_DA_TCAM_OFFSET                                 (C_MODEL_BASE + 0x60006200)
#define DS_IPV6_UCAST_PBR_DUAL_DA_TCAM_OFFSET                        (C_MODEL_BASE + 0x60006200)
#define DS_L2_EDIT_ETH4_W_OFFSET                                     (C_MODEL_BASE + 0x60006200)
#define DS_L2_EDIT_ETH8_W_OFFSET                                     (C_MODEL_BASE + 0x60006200)
#define DS_L2_EDIT_FLEX8_W_OFFSET                                    (C_MODEL_BASE + 0x60006200)
#define DS_L2_EDIT_LOOPBACK_OFFSET                                   (C_MODEL_BASE + 0x60006200)
#define DS_L2_EDIT_PBB4_W_OFFSET                                     (C_MODEL_BASE + 0x60006200)
#define DS_L2_EDIT_PBB8_W_OFFSET                                     (C_MODEL_BASE + 0x60006200)
#define DS_L2_EDIT_SWAP_OFFSET                                       (C_MODEL_BASE + 0x60006200)
#define DS_L3_EDIT_FLEX_OFFSET                                       (C_MODEL_BASE + 0x60006200)
#define DS_L3_EDIT_MPLS4_W_OFFSET                                    (C_MODEL_BASE + 0x60006200)
#define DS_L3_EDIT_MPLS8_W_OFFSET                                    (C_MODEL_BASE + 0x60006200)
#define DS_L3_EDIT_NAT4_W_OFFSET                                     (C_MODEL_BASE + 0x60006200)
#define DS_L3_EDIT_NAT8_W_OFFSET                                     (C_MODEL_BASE + 0x60006200)
#define DS_L3_EDIT_TUNNEL_V4_OFFSET                                  (C_MODEL_BASE + 0x60006200)
#define DS_L3_EDIT_TUNNEL_V6_OFFSET                                  (C_MODEL_BASE + 0x60006200)
#define DS_MA_OFFSET                                                 (C_MODEL_BASE + 0x60006200)
#define DS_MA_NAME_OFFSET                                            (C_MODEL_BASE + 0x60006200)
#define DS_MAC_OFFSET                                                (C_MODEL_BASE + 0x60006200)
#define DS_MAC_ACL0_TCAM_OFFSET                                      (C_MODEL_BASE + 0x60006200)
#define DS_MAC_ACL1_TCAM_OFFSET                                      (C_MODEL_BASE + 0x60006200)
#define DS_MAC_ACL2_TCAM_OFFSET                                      (C_MODEL_BASE + 0x60006200)
#define DS_MAC_ACL3_TCAM_OFFSET                                      (C_MODEL_BASE + 0x60006200)
#define DS_MAC_HASH_KEY_OFFSET                                       (C_MODEL_BASE + 0x60006200)
#define DS_MAC_IPV4_HASH32_KEY_OFFSET                                (C_MODEL_BASE + 0x60006200)
#define DS_MAC_IPV4_HASH64_KEY_OFFSET                                (C_MODEL_BASE + 0x60006200)
#define DS_MAC_IPV4_TCAM_OFFSET                                      (C_MODEL_BASE + 0x60006200)
#define DS_MAC_IPV6_TCAM_OFFSET                                      (C_MODEL_BASE + 0x60006200)
#define DS_MAC_TCAM_OFFSET                                           (C_MODEL_BASE + 0x60006200)
#define DS_MET_ENTRY_OFFSET                                          (C_MODEL_BASE + 0x60006200)
#define DS_MPLS_OFFSET                                               (C_MODEL_BASE + 0x60006200)
#define DS_NEXT_HOP4_W_OFFSET                                        (C_MODEL_BASE + 0x60006200)
#define DS_NEXT_HOP8_W_OFFSET                                        (C_MODEL_BASE + 0x60006200)
#define DS_TRILL_DA_OFFSET                                           (C_MODEL_BASE + 0x60006200)
#define DS_TRILL_DA_MCAST_TCAM_OFFSET                                (C_MODEL_BASE + 0x60006200)
#define DS_TRILL_DA_UCAST_TCAM_OFFSET                                (C_MODEL_BASE + 0x60006200)
#define DS_TRILL_MCAST_HASH_KEY_OFFSET                               (C_MODEL_BASE + 0x60006200)
#define DS_TRILL_MCAST_VLAN_HASH_KEY_OFFSET                          (C_MODEL_BASE + 0x60006200)
#define DS_TRILL_UCAST_HASH_KEY_OFFSET                               (C_MODEL_BASE + 0x60006200)
#define DS_USER_ID_EGRESS_DEFAULT_OFFSET                             (C_MODEL_BASE + 0x60006200)
#define DS_VLAN_XLATE_OFFSET                                         (C_MODEL_BASE + 0x60006200)
#define DS_VLAN_XLATE_CONFLICT_TCAM_OFFSET                           (C_MODEL_BASE + 0x60006200)
#define DS_ACL_IPV4_KEY0_OFFSET                                      (C_MODEL_BASE + 0x70000000)
#define DS_ACL_IPV4_KEY1_OFFSET                                      (C_MODEL_BASE + 0x70000000)
#define DS_ACL_IPV4_KEY2_OFFSET                                      (C_MODEL_BASE + 0x70000000)
#define DS_ACL_IPV4_KEY3_OFFSET                                      (C_MODEL_BASE + 0x70000000)
#define DS_ACL_IPV6_KEY0_OFFSET                                      (C_MODEL_BASE + 0x70000000)
#define DS_ACL_IPV6_KEY1_OFFSET                                      (C_MODEL_BASE + 0x70000000)
#define DS_ACL_MAC_KEY0_OFFSET                                       (C_MODEL_BASE + 0x70000000)
#define DS_ACL_MAC_KEY1_OFFSET                                       (C_MODEL_BASE + 0x70000000)
#define DS_ACL_MAC_KEY2_OFFSET                                       (C_MODEL_BASE + 0x70000000)
#define DS_ACL_MAC_KEY3_OFFSET                                       (C_MODEL_BASE + 0x70000000)
#define DS_ACL_MPLS_KEY0_OFFSET                                      (C_MODEL_BASE + 0x70000000)
#define DS_ACL_MPLS_KEY1_OFFSET                                      (C_MODEL_BASE + 0x70000000)
#define DS_ACL_MPLS_KEY2_OFFSET                                      (C_MODEL_BASE + 0x70000000)
#define DS_ACL_MPLS_KEY3_OFFSET                                      (C_MODEL_BASE + 0x70000000)
#define DS_ACL_QOS_IPV4_KEY_OFFSET                                   (C_MODEL_BASE + 0x70000000)
#define DS_ACL_QOS_IPV6_KEY_OFFSET                                   (C_MODEL_BASE + 0x70000000)
#define DS_ACL_QOS_MAC_KEY_OFFSET                                    (C_MODEL_BASE + 0x70000000)
#define DS_ACL_QOS_MPLS_KEY_OFFSET                                   (C_MODEL_BASE + 0x70000000)
#define DS_FCOE_ROUTE_KEY_OFFSET                                     (C_MODEL_BASE + 0x70000000)
#define DS_FCOE_RPF_KEY_OFFSET                                       (C_MODEL_BASE + 0x70000000)
#define DS_IPV4_MCAST_ROUTE_KEY_OFFSET                               (C_MODEL_BASE + 0x70000000)
#define DS_IPV4_NAT_KEY_OFFSET                                       (C_MODEL_BASE + 0x70000000)
#define DS_IPV4_PBR_KEY_OFFSET                                       (C_MODEL_BASE + 0x70000000)
#define DS_IPV4_ROUTE_KEY_OFFSET                                     (C_MODEL_BASE + 0x70000000)
#define DS_IPV4_RPF_KEY_OFFSET                                       (C_MODEL_BASE + 0x70000000)
#define DS_IPV4_UCAST_ROUTE_KEY_OFFSET                               (C_MODEL_BASE + 0x70000000)
#define DS_IPV6_MCAST_ROUTE_KEY_OFFSET                               (C_MODEL_BASE + 0x70000000)
#define DS_IPV6_NAT_KEY_OFFSET                                       (C_MODEL_BASE + 0x70000000)
#define DS_IPV6_PBR_KEY_OFFSET                                       (C_MODEL_BASE + 0x70000000)
#define DS_IPV6_ROUTE_KEY_OFFSET                                     (C_MODEL_BASE + 0x70000000)
#define DS_IPV6_RPF_KEY_OFFSET                                       (C_MODEL_BASE + 0x70000000)
#define DS_IPV6_UCAST_ROUTE_KEY_OFFSET                               (C_MODEL_BASE + 0x70000000)
#define DS_MAC_BRIDGE_KEY_OFFSET                                     (C_MODEL_BASE + 0x70000000)
#define DS_MAC_IPV4_KEY_OFFSET                                       (C_MODEL_BASE + 0x70000000)
#define DS_MAC_IPV6_KEY_OFFSET                                       (C_MODEL_BASE + 0x70000000)
#define DS_MAC_LEARNING_KEY_OFFSET                                   (C_MODEL_BASE + 0x70000000)
#define DS_TRILL_MCAST_ROUTE_KEY_OFFSET                              (C_MODEL_BASE + 0x70000000)
#define DS_TRILL_ROUTE_KEY_OFFSET                                    (C_MODEL_BASE + 0x70000000)
#define DS_TRILL_UCAST_ROUTE_KEY_OFFSET                              (C_MODEL_BASE + 0x70000000)
#define DS_TUNNEL_ID_CAPWAP_KEY_OFFSET                               (C_MODEL_BASE + 0x70000000)
#define DS_TUNNEL_ID_IPV4_KEY_OFFSET                                 (C_MODEL_BASE + 0x70000000)
#define DS_TUNNEL_ID_IPV6_KEY_OFFSET                                 (C_MODEL_BASE + 0x70000000)
#define DS_TUNNEL_ID_PBB_KEY_OFFSET                                  (C_MODEL_BASE + 0x70000000)
#define DS_TUNNEL_ID_TRILL_KEY_OFFSET                                (C_MODEL_BASE + 0x70000000)
#define DS_USER_ID_IPV4_KEY_OFFSET                                   (C_MODEL_BASE + 0x70000000)
#define DS_USER_ID_IPV6_KEY_OFFSET                                   (C_MODEL_BASE + 0x70000000)
#define DS_USER_ID_MAC_KEY_OFFSET                                    (C_MODEL_BASE + 0x70000000)
#define DS_USER_ID_VLAN_KEY_OFFSET                                   (C_MODEL_BASE + 0x70000000)
#define DS_FIB_USER_ID160_KEY_OFFSET                                 (C_MODEL_BASE + 0x80000000)
#define DS_FIB_USER_ID80_KEY_OFFSET                                  (C_MODEL_BASE + 0x80000000)
#define DS_IPV4_HASH_KEY_OFFSET                                      (C_MODEL_BASE + 0x80000000)
#define DS_IPV6_HASH_KEY_OFFSET                                      (C_MODEL_BASE + 0x80000000)
#define DS_IPV6_MCAST_HASH_KEY_OFFSET                                (C_MODEL_BASE + 0x80000000)
#define DS_LPM_IPV4_HASH16_KEY_OFFSET                                (C_MODEL_BASE + 0x80000000)
#define DS_LPM_IPV4_HASH8_KEY_OFFSET                                 (C_MODEL_BASE + 0x80000000)
#define DS_LPM_IPV4_MCAST_HASH32_KEY_OFFSET                          (C_MODEL_BASE + 0x80000000)
#define DS_LPM_IPV4_MCAST_HASH64_KEY_OFFSET                          (C_MODEL_BASE + 0x80000000)
#define DS_LPM_IPV4_NAT_DA_PORT_HASH_KEY_OFFSET                      (C_MODEL_BASE + 0x80000000)
#define DS_LPM_IPV4_NAT_SA_HASH_KEY_OFFSET                           (C_MODEL_BASE + 0x80000000)
#define DS_LPM_IPV4_NAT_SA_PORT_HASH_KEY_OFFSET                      (C_MODEL_BASE + 0x80000000)
#define DS_LPM_IPV6_HASH32_HIGH_KEY_OFFSET                           (C_MODEL_BASE + 0x80000000)
#define DS_LPM_IPV6_HASH32_LOW_KEY_OFFSET                            (C_MODEL_BASE + 0x80000000)
#define DS_LPM_IPV6_HASH32_MID_KEY_OFFSET                            (C_MODEL_BASE + 0x80000000)
#define DS_LPM_IPV6_MCAST_HASH0_KEY_OFFSET                           (C_MODEL_BASE + 0x80000000)
#define DS_LPM_IPV6_MCAST_HASH1_KEY_OFFSET                           (C_MODEL_BASE + 0x80000000)
#define DS_LPM_IPV6_NAT_DA_PORT_HASH_KEY_OFFSET                      (C_MODEL_BASE + 0x80000000)
#define DS_LPM_IPV6_NAT_SA_HASH_KEY_OFFSET                           (C_MODEL_BASE + 0x80000000)
#define DS_LPM_IPV6_NAT_SA_PORT_HASH_KEY_OFFSET                      (C_MODEL_BASE + 0x80000000)
#define DS_LPM_LOOKUP_KEY_OFFSET                                     (C_MODEL_BASE + 0x80000000)
#define DS_LPM_LOOKUP_KEY0_OFFSET                                    (C_MODEL_BASE + 0x80000000)
#define DS_LPM_LOOKUP_KEY1_OFFSET                                    (C_MODEL_BASE + 0x80000000)
#define DS_LPM_LOOKUP_KEY2_OFFSET                                    (C_MODEL_BASE + 0x80000000)
#define DS_LPM_LOOKUP_KEY3_OFFSET                                    (C_MODEL_BASE + 0x80000000)
#define DS_LPM_TCAM160_KEY_OFFSET                                    (C_MODEL_BASE + 0x80000000)
#define DS_LPM_TCAM80_KEY_OFFSET                                     (C_MODEL_BASE + 0x80000000)
#define DS_LPM_TCAM_AD_OFFSET                                        (C_MODEL_BASE + 0x80000000)
#define DS_MAC_FIB_KEY_OFFSET                                        (C_MODEL_BASE + 0x80000000)
#define DS_MAC_IP_FIB_KEY_OFFSET                                     (C_MODEL_BASE + 0x80000000)
#define DS_MAC_IPV6_FIB_KEY_OFFSET                                   (C_MODEL_BASE + 0x80000000)
#define FIB_LEARN_FIFO_DATA_OFFSET                                   (C_MODEL_BASE + 0x80000000)
#define MS_DEQUEUE_OFFSET                                            (C_MODEL_BASE + 0x90000000)
#define MS_ENQUEUE_OFFSET                                            (C_MODEL_BASE + 0x90000000)
#define MS_EXCP_INFO_OFFSET                                          (C_MODEL_BASE + 0x90000000)
#define MS_MET_FIFO_OFFSET                                           (C_MODEL_BASE + 0x90000000)
#define MS_PACKET_HEADER_OFFSET                                      (C_MODEL_BASE + 0x90000000)
#define MS_PACKET_RELEASE_OFFSET                                     (C_MODEL_BASE + 0x90000000)
#define MS_RCD_UPDATE_OFFSET                                         (C_MODEL_BASE + 0x90000000)
#define MS_STATS_UPDATE_OFFSET                                       (C_MODEL_BASE + 0x90000000)
#define PACKET_HEADER_OUTER_OFFSET                                   (C_MODEL_BASE + 0x90000000)

#endif /*end of _DRV_CFG_H*/

