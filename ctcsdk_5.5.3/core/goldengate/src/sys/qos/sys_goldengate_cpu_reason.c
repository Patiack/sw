/**
 @file sys_goldengate_cpu_reason.c

 @date 2010-01-13

 @version v2.0

*/

/****************************************************************************
 *
 * Header Files
 *
 ****************************************************************************/

#include "ctc_debug.h"
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_macro.h"

#include "sys_goldengate_chip.h"

#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_queue_drop.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_packet.h"

#include "sys_goldengate_cpu_reason.h"
#include "sys_goldengate_queue_sch.h"
#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"

#define SYS_REASON_NORMAL_EXCP 0
#define SYS_REASON_FATAL_EXCP 1
#define SYS_REASON_OAM_EXCP 2
#define SYS_REASON_FWD_EXCP 3

#define SYS_REASON_ENCAP_EXCP(type, excp, sub_excp)  ((type << 8) | ((excp) + (sub_excp)))
#define SYS_REASON_GET_EXCP_TYPE(exception) (exception >> 8)
#define SYS_REASON_GET_EXCP_INDEX(exception) (exception & 0xFF)


/* IPE module fatal exception meanings definition */
enum sys_fatal_exception_e
{
    SYS_FATAL_EXCP_UC_IP_HDR_ERR_OR_IP_MARTION_ADDR = 0,
    SYS_FATAL_EXCP_UC_IP_OPTIONS                    = 1,
    SYS_FATAL_EXCP_UC_GRE_UNKNOWN_OPTION_OR_PTL     = 2,
    SYS_FATAL_EXCP_UC_ISATAP_SRC_ADD_CHECK_FAIL     = 3,
    SYS_FATAL_EXCP_UC_IP_TTL_CHECK_FAIL             = 4,
    SYS_FATAL_EXCP_UC_RPF_CHECK_FAIL                = 5,
    SYS_FATAL_EXCP_UC_OR_MC_MORE_RPF                = 6,
    SYS_FATAL_EXCP_UC_LINK_ID_CHECK_FAIL            = 7,
    SYS_FATAL_EXCP_MPLS_LABEL_OUT_OF_RANGE          = 8,
    SYS_FATAL_EXCP_MPLS_SBIT_ERROR                  = 9,
    SYS_FATAL_EXCP_MPLS_TTL_CHECK_FAIL              = 10,
    SYS_FATAL_EXCP_FCOE_VIRTUAL_LINK_CHECK_FAIL     = 11,
    SYS_FATAL_EXCP_IGMP_SNOOPED_PACKET              = 12,
    SYS_FATAL_EXCP_TRILL_OPTION                     = 13,
    SYS_FATAL_EXCP_TRILL_TTL_CHECK_FAIL             = 14,
    SYS_FATAL_EXCP_VXLAN_OR_NVGRE_CHK_FAIL          = 15
};
typedef enum sys_fatal_exception_e sys_fatal_exception_t;

enum exception_type_ipe_e
{
    SYS_NORMAL_EXCP_TYPE_USER_ID              = 24,
    SYS_NORMAL_EXCP_TYPE_PROTOCOL_VLAN        = 25,

    SYS_NORMAL_EXCP_TYPE_EGS_PORT_LOG   = 27,
    SYS_NORMAL_EXCP_TYPE_ICMP_REDIRECT        = 28,
    SYS_NORMAL_EXCP_TYPE_LEARNING_CACHE_FULL  = 29,
    SYS_NORMAL_EXCP_TYPE_ROUTE_MCAST_RPF_FAIL = 30,
    SYS_NORMAL_EXCP_TYPE_IGS_PORT_LOG  =  31,
	SYS_NORMAL_EXCP_TYPE_EGRESS_ACL_EXCEPTION  = 44,

	SYS_NORMAL_EXCP_TYPE_EPE_DISCARD_EXP           = 46,
	SYS_NORMAL_EXCP_TYPE_BUFFER_LOG  = 48,
    SYS_NORMAL_EXCP_TYPE_LATENCY_LOG = 52,

    SYS_NORMAL_EXCP_TYPE_EGRESS_VLAN_XLATE  = 56,
    SYS_NORMAL_EXCP_TYPE_PARSER_LEN_ERROR   = 57,
    SYS_NORMAL_EXCP_TYPE_TTL_CHK_FAIL       = 58,
    SYS_NORMAL_EXCP_TYPE_OAM_HASH_CONFLICT  = 59,
    SYS_NORMAL_EXCP_TYPE_OAM_2              = 60,
    SYS_NORMAL_EXCP_TYPE_MTU_CHK_FAIL       = 61,
    SYS_NORMAL_EXCP_TYPE_STRIP_OFFSET_ERROR = 62,
    SYS_NORMAL_EXCP_TYPE_ICMP_ERROR_MSG     = 63,
    SYS_NORMAL_EXCP_TYPE_LAYER2               = 64,
    SYS_NORMAL_EXCP_TYPE_LAYER3               = 128,
    SYS_NORMAL_EXCP_TYPE_OTHER                = 192,

};
typedef enum exception_type_ipe_e exception_type_ipe_t;


enum sys_normal_excp2_sub_type_e
{
    /* 0 -- 31 User define,32~51 reserved */

    SYS_NORMAL_EXCP_SUB_TYPE_LEARNING_CONFLICT      = 52,
    SYS_NORMAL_EXCP_SUB_TYPE_MACDA_SELF_ADRESS      = 53,
    SYS_NORMAL_EXCP_SUB_TYPE_MACSA_SELF_ADRESS      = 54,
	SYS_NORMAL_EXCP_SUB_TYPE_SYS_SECURITY      = 55,

    SYS_NORMAL_EXCP_SUB_TYPE_STORM_CTL         = 58,
    SYS_NORMAL_EXCP_SUB_TYPE_VSI_SECURITY      = 59,
    SYS_NORMAL_EXCP_SUB_TYPE_PROFILE_SECURITY  = 60,
    SYS_NORMAL_EXCP_SUB_TYPE_PORT_SECURITY     = 61,
    SYS_NORMAL_EXCP_SUB_TYPE_SRC_MISMATCH      = 62,
    SYS_NORMAL_EXCP_SUB_TYPE_MAC_SA            = 63
};
typedef enum sys_normal_excp2_sub_type_e sys_normal_excp2_sub_type_t;

enum sys_normal_excp3_sub_type_e
{
    SYS_NORMAL_EXCP_SUB_TYPE_IP_DA            = 63,
};
typedef enum sys_normal_excp3_sub_type_e sys_normal_excp3_sub_type_t;



enum sys_normal_excp_other_sub_type_e
{
    SYS_NORMAL_EXCP_SUB_TYPE_SGMAC_CTL_MSG         = 0,
    SYS_NORMAL_EXCP_SUB_TYPE_PARSER_LEN_ERR        = 1,
    SYS_NORMAL_EXCP_SUB_TYPE_PBB_TCI_NCA           = 2,
    SYS_NORMAL_EXCP_SUB_TYPE_PTP                   = 3,
    SYS_NORMAL_EXCP_SUB_TYPE_PTP_FATAL_EXCP_TO_CPU = 4,
    SYS_NORMAL_EXCP_SUB_TYPE_PLD_OFFSET_ERR        = 5,
    SYS_NORMAL_EXCP_SUB_TYPE_ACL                   = 6,
    SYS_NORMAL_EXCP_SUB_TYPE_ENTROPY_LABEL_ERROR   = 7,
    SYS_NORMAL_EXCP_SUB_TYPE_ETHERNET_OAM_ERR      = 8,
    SYS_NORMAL_EXCP_SUB_TYPE_MPLS_OAM_TTL_CHK_FAIL = 9,
    SYS_NORMAL_EXCP_SUB_TYPE_MPLS_TMPLS_OAM        = 10,
    SYS_NORMAL_EXCP_SUB_TYPE_DCN                   = 11,

    SYS_NORMAL_EXCP_SUB_TYPE_VXLAN_NVGRE_INNER_VTAG_CHK = 12,
    SYS_NORMAL_EXCP_SUB_TYPE_ACH_ERR               = 13,

    SYS_NORMAL_EXCP_SUB_TYPE_SECTION_OAM_EXP       = 14,
    SYS_NORMAL_EXCP_SUB_TYPE_OAM_HASH_CONFLICT     = 15,

    SYS_NORMAL_EXCP_SUB_TYPE_NO_MEP_MIP_DIS        = 16,
    SYS_NORMAL_EXCP_SUB_TYPE_FLOW_DEBUG            = 17,
    SYS_NORMAL_EXCP_SUB_TYPE_IPFIX_HASHCONFLICT    = 18,
    SYS_NORMAL_EXCP_SUB_TYPE_NEW_ELEPHANT_FLOW     = 19,

	SYS_NORMAL_EXCP_SUB_TYPE_VXLAN_FLAG_CHK_ERROR = 20,
    SYS_NORMAL_EXCP_SUB_TYPE_QUE_DROP_SPAN_EXCP    = 21,
    SYS_NORMAL_EXCP_SUB_TYPE_GENEVE_PKT_CPU_PROCESS    = 22,
    SYS_NORMAL_EXCP_SUB_TYPE_IPE_DISCARD    = 23
};
typedef enum sys_normal_excp_other_sub_type_e sys_normal_excp_other_sub_type_t;

#define SYS_CPU_REASON_OAM_DEFECT_MESSAGE_BASE 128
#define SYS_CPU_OAM_EXCP_NUM 33

extern sys_queue_master_t* p_gg_queue_master[CTC_MAX_LOCAL_CHIP_NUM];

int32
sys_goldengate_queue_get_excp_idx_by_reason(uint8 lchip, uint16 reason_id,
                                           uint16* excp_idx,
                                           uint8* excp_num)
{
    uint8 idx = 0;
    uint8 i = 0;
    uint8 oam_exception[SYS_CPU_OAM_EXCP_NUM] = /*indicate the value in ctc_oam_exceptions_t*/
    {
       0, /*SYS_OAM_EXCP_NONE             = 0,  for relay all to cpu, use DsOamExcp 0*/
       0, /*SYS_OAM_EXCP_ETH_INVALID      = 1,  CTC_OAM_EXCP_INVALID_OAM_PDU*/
       2, /*SYS_OAM_EXCP_ETH_LB           = 2,  CTC_OAM_EXCP_LBM*/
      24, /*SYS_OAM_EXCP_ETH_LT           = 3,  CTC_OAM_EXCP_EQUAL_LTM_LTR_TO_CPU*/
      27, /*SYS_OAM_EXCP_ETH_LM           = 4,  CTC_OAM_EXCP_LM_TO_CPU*/
      18, /*SYS_OAM_EXCP_ETH_DM           = 5,  CTC_OAM_EXCP_DM_TO_CPU*/
      10, /*SYS_OAM_EXCP_ETH_TST          = 6,  CTC_OAM_EXCP_ETH_TST_TO_CPU*/
      16, /*SYS_OAM_EXCP_ETH_APS          = 7,  CTC_OAM_EXCP_APS_PDU_TO_CPU*/
      31, /*SYS_OAM_EXCP_ETH_SCC          = 8,  CTC_OAM_EXCP_SCC_PDU_TO_CPU*/
      22, /*SYS_OAM_EXCP_ETH_MCC          = 9,  CTC_OAM_EXCP_MCC_PDU_TO_CPU*/
      20, /*SYS_OAM_EXCP_ETH_CSF          = 10, CTC_OAM_EXCP_CSF_TO_CPU*/
      12, /*SYS_OAM_EXCP_ETH_BIG_CCM      = 11, CTC_OAM_EXCP_BIG_INTERVAL_OR_SW_MEP_TO_CPU*/
      19, /*SYS_OAM_EXCP_ETH_LEARN_CCM    = 12, CTC_OAM_EXCP_LEARNING_CCM_TO_CPU*/
       1, /*SYS_OAM_EXCP_ETH_DEFECT       = 13, CTC_OAM_EXCP_ALL_DEFECT*/
      23, /*SYS_OAM_EXCP_PBX_OAM          = 14, CTC_OAM_EXCP_PBT_MM_DEFECT_OAM_PDU*/
       3, /*SYS_OAM_EXCP_ETH_HIGH_VER     = 15, CTC_OAM_EXCP_HIGH_VER_OAM_TO_CPU*/
       8, /*SYS_OAM_EXCP_ETH_TLV          = 16, CTC_OAM_EXCP_OPTION_CCM_TLV*/
      17, /*SYS_OAM_EXCP_ETH_OTHER        = 17, CTC_OAM_EXCP_UNKNOWN_PDU*/
       0, /*SYS_OAM_EXCP_BFD_INVALID      = 18, CTC_OAM_EXCP_INVALID_OAM_PDU*/
      28, /*SYS_OAM_EXCP_BFD_LEARN        = 19, CTC_OAM_EXCP_LEARNING_BFD_TO_CPU*/
      12, /*SYS_OAM_EXCP_BIG_BFD          = 20, CTC_OAM_EXCP_BIG_INTERVAL_OR_SW_MEP_TO_CPU*/
      30, /*SYS_OAM_EXCP_BFD_TIMER_NEG    = 21, CTC_OAM_EXCP_BFD_TIMER_NEGOTIATION*/
       7, /*SYS_OAM_EXCP_TP_SCC           = 22, CTC_OAM_EXCP_TP_SCC*/
       6, /*SYS_OAM_EXCP_TP_MCC           = 23, CTC_OAM_EXCP_TP_MCC*/
       5, /*SYS_OAM_EXCP_TP_CSF           = 24, CTC_OAM_EXCP_TP_CSF*/
       4, /*SYS_OAM_EXCP_TP_LB            = 25, CTC_OAM_EXCP_TP_LBM*/
      14, /*SYS_OAM_EXCP_TP_DLM           = 26, CTC_OAM_EXCP_MPLS_TP_DLM_TO_CPU*/
      15, /*SYS_OAM_EXCP_TP_DM            = 27, CTC_OAM_EXCP_MPLS_TP_DM_DLMDM_TO_CPU*/
       9, /*SYS_OAM_EXCP_TP_FM            = 28, CTC_OAM_EXCP_TP_FM*/
      17, /*SYS_OAM_EXCP_BFD_OTHER        = 29, CTC_OAM_EXCP_UNKNOWN_PDU*/
      21, /*SYS_OAM_EXCP_BFD_DISC_MISMATCH= 30, CTC_OAM_EXCP_BFD_DISC_MISMATCH*/
      11, /*SYS_OAM_EXCP_ETH_SM           = 31, CTC_OAM_EXCP_SM*/
      13 /*SYS_OAM_EXCP_TP_CV            = 32  CTC_OAM_EXCP_TP_BFD_CV*/
    };

    switch (reason_id)
    {
    case CTC_PKT_CPU_REASON_IGMP_SNOOPING:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_IGMP_SNOOPED_PACKET,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_PTP:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_PTP);

		excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_PTP_FATAL_EXCP_TO_CPU);
        break;

    case CTC_PKT_CPU_REASON_MPLS_OAM:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_MPLS_TMPLS_OAM);

        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_MPLS_OAM_TTL_CHK_FAIL);
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                               SYS_NORMAL_EXCP_TYPE_OTHER ,
                                                SYS_NORMAL_EXCP_SUB_TYPE_ACH_ERR);
        break;

    case CTC_PKT_CPU_REASON_DCN:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_DCN);
        break;

    case CTC_PKT_CPU_REASON_SCL_MATCH:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_USER_ID,
                                                0);

        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_EGRESS_VLAN_XLATE,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_ACL_MATCH:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_ACL);

		  excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_EGRESS_ACL_EXCEPTION,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_SFLOW_SOURCE:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_IGS_PORT_LOG,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_SFLOW_DEST:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_EGS_PORT_LOG,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_L2_SYSTEM_LEARN_LIMIT:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                SYS_NORMAL_EXCP_SUB_TYPE_SYS_SECURITY);
        break;

    case CTC_PKT_CPU_REASON_L2_VLAN_LEARN_LIMIT:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                SYS_NORMAL_EXCP_SUB_TYPE_VSI_SECURITY);
        break;

    case CTC_PKT_CPU_REASON_L2_PORT_LEARN_LIMIT:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                SYS_NORMAL_EXCP_SUB_TYPE_PORT_SECURITY);
        break;

    case CTC_PKT_CPU_REASON_L2_MOVE:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                SYS_NORMAL_EXCP_SUB_TYPE_SRC_MISMATCH);
        break;

    case CTC_PKT_CPU_REASON_L2_CPU_LEARN:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LEARNING_CACHE_FULL,
                                                0);

         excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                SYS_NORMAL_EXCP_SUB_TYPE_LEARNING_CONFLICT);


        break;

    case CTC_PKT_CPU_REASON_L2_COPY_CPU:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                SYS_NORMAL_EXCP_SUB_TYPE_MAC_SA);



		excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                SYS_NORMAL_EXCP_SUB_TYPE_MACDA_SELF_ADRESS);

	    excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                SYS_NORMAL_EXCP_SUB_TYPE_MACSA_SELF_ADRESS);


        break;

    case CTC_PKT_CPU_REASON_L3_URPF_FAIL:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_UC_RPF_CHECK_FAIL,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_L3_MTU_FAIL:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_MTU_CHK_FAIL,
                                                0);

        break;

    case CTC_PKT_CPU_REASON_L3_MC_RPF_FAIL:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_ROUTE_MCAST_RPF_FAIL,
                                                0);
        break;

	case CTC_PKT_CPU_REASON_TUNNEL_MORE_RPF:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_UC_OR_MC_MORE_RPF,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_L3_IP_OPTION:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_UC_IP_OPTIONS,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_L3_ICMP_REDIRECT:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_ICMP_REDIRECT,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_L3_COPY_CPU:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER3,
                                                SYS_NORMAL_EXCP_SUB_TYPE_IP_DA);
        break;

    case CTC_PKT_CPU_REASON_L3_MARTIAN_ADDR:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_UC_IP_HDR_ERR_OR_IP_MARTION_ADDR,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_IP_TTL_CHECK_FAIL:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_UC_IP_TTL_CHECK_FAIL,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_MPLS_TTL_CHECK_FAIL:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_MPLS_TTL_CHECK_FAIL,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_IPMC_TTL_CHECK_FAIL:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_TTL_CHK_FAIL,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_GRE_UNKNOWN:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_UC_GRE_UNKNOWN_OPTION_OR_PTL,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_LABEL_MISS:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_MPLS_LABEL_OUT_OF_RANGE,
                                                0);


        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_MPLS_SBIT_ERROR,
                                                0);
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                               SYS_NORMAL_EXCP_TYPE_OTHER ,
                                                SYS_NORMAL_EXCP_SUB_TYPE_ENTROPY_LABEL_ERROR);

        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                               SYS_NORMAL_EXCP_TYPE_OTHER ,
                                                SYS_NORMAL_EXCP_SUB_TYPE_SECTION_OAM_EXP);


        break;

    case CTC_PKT_CPU_REASON_LINK_ID_FAIL:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_UC_LINK_ID_CHECK_FAIL,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_OAM_HASH_CONFLICT:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_OAM_HASH_CONFLICT);

        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OAM_HASH_CONFLICT,
                                                0);
        break;

    case CTC_PKT_CPU_REASON_IPFIX_HASH_CONFLICT :
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_IPFIX_HASHCONFLICT);

         break;
    case CTC_PKT_CPU_REASON_NEW_ELEPHANT_FLOW:

        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_NEW_ELEPHANT_FLOW);

         break;
    case CTC_PKT_CPU_REASON_VXLAN_NVGRE_CHK_FAIL:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_VXLAN_OR_NVGRE_CHK_FAIL,
                                               0 );

		 excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                               SYS_NORMAL_EXCP_SUB_TYPE_VXLAN_NVGRE_INNER_VTAG_CHK );


		 excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                               SYS_NORMAL_EXCP_SUB_TYPE_VXLAN_FLAG_CHK_ERROR );


	    break;

    case CTC_PKT_CPU_REASON_L2_STORM_CTL:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                SYS_NORMAL_EXCP_SUB_TYPE_STORM_CTL);
        break;

    case CTC_PKT_CPU_REASON_MONITOR_BUFFER_LOG:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_BUFFER_LOG,
                                                0);
        break;
    case CTC_PKT_CPU_REASON_MONITOR_LATENCY_LOG:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LATENCY_LOG,
                                                0);
        break;
	case CTC_PKT_CPU_REASON_QUEUE_DROP_PKT:
		 excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_QUE_DROP_SPAN_EXCP);
         break;
    case CTC_PKT_CPU_REASON_GENEVE_PKT:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_GENEVE_PKT_CPU_PROCESS);

         break;
	case CTC_PKT_CPU_REASON_OAM_DEFECT_MESSAGE:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_OAM_EXCP,
                                                reason_id - CTC_PKT_CPU_REASON_OAM,
                                                SYS_CPU_REASON_OAM_DEFECT_MESSAGE_BASE);

		break;
    case CTC_PKT_CPU_REASON_DIAG_DISCARD_PKT:
               excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_IPE_DISCARD);
               excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_EPE_DISCARD_EXP,
                                                0 );

    default:
        break;
    }

    /*IPE l2Pdu (exception2)*/
    if (reason_id >= CTC_PKT_CPU_REASON_L2_PDU &&
        reason_id < (CTC_PKT_CPU_REASON_L2_PDU + CTC_PKT_CPU_REASON_L2_PDU_RESV))
    {
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                reason_id - CTC_PKT_CPU_REASON_L2_PDU);
    }

    /*IPE l3Pdu (exception3)*/
    if (reason_id >= CTC_PKT_CPU_REASON_L3_PDU &&
        reason_id < (CTC_PKT_CPU_REASON_L3_PDU + CTC_PKT_CPU_REASON_L3_PDU_RESV))
    {
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER3,
                                                reason_id - CTC_PKT_CPU_REASON_L3_PDU);
    }

    /* oam exception*/
    if (reason_id >= CTC_PKT_CPU_REASON_OAM &&
        reason_id < (CTC_PKT_CPU_REASON_OAM + CTC_PKT_CPU_REASON_OAM_PDU_RESV))
    {
        for (i = 0; i < SYS_CPU_OAM_EXCP_NUM; i++)
        {
            if ((reason_id - CTC_PKT_CPU_REASON_OAM) == oam_exception[i])
            {
                excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_OAM_EXCP,
                                                        i,
                                                        0);
            }
        }
        if (idx == 0)
        {
            return CTC_E_NONE;
        }
    }

    /* forward to cpu*/
    if (reason_id == CTC_PKT_CPU_REASON_FWD_CPU)
    {
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FWD_EXCP,
                                                0,
                                                0);
    }
    if (reason_id == CTC_PKT_CPU_REASON_DROP)
    {

        /*fatal*/
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_UC_ISATAP_SRC_ADD_CHECK_FAIL,
                                                0);
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_FCOE_VIRTUAL_LINK_CHECK_FAIL,
                                                0);
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_TRILL_OPTION,
                                                0 );

        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FATAL_EXCP,
                                                SYS_FATAL_EXCP_TRILL_TTL_CHECK_FAIL,
                                                0 );
		/*normal*/
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_PROTOCOL_VLAN,
                                                0 );
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_PARSER_LEN_ERROR,
                                                0 );        /*exception =2 */
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                SYS_NORMAL_EXCP_SUB_TYPE_SYS_SECURITY);
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                SYS_NORMAL_EXCP_SUB_TYPE_PROFILE_SECURITY );
        /*other */
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_SGMAC_CTL_MSG);

        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_PARSER_LEN_ERR);
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_PBB_TCI_NCA);
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_PLD_OFFSET_ERR);
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_ETHERNET_OAM_ERR);
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_NO_MEP_MIP_DIS);
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_FLOW_DEBUG);
    }
    *excp_num = idx;

    SYS_QUEUE_DBG_INFO("excp_num = %d, excp_idx[0] = %d \n", *excp_num, excp_idx[0]);
    if ((0 == idx) && (reason_id < CTC_PKT_CPU_REASON_CUSTOM_BASE))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }
    return CTC_E_NONE;
}

/*****************************************************************************
 Description  : update cpu exception include normal and fatal
*****************************************************************************/
int32
sys_goldengate_cpu_reason_update_exception(uint8 lchip, uint16 reason_id,
                                          uint32 dest_map,
                                          uint32 nexthop_ptr,
                                          uint32 dsnh_8w,
                                          uint8 dest_type)
{
    uint32 cmd         = 0;
    uint8 idx          = 0;
    uint16 exception   = 0;
    uint16 excp_idx    = 0;
    uint8 excp_num     = 0;
    uint8 excp_type    = 0;
    uint16 excp_array[32] = {0};
    sys_cpu_reason_t* p_reason = NULL;
    uint32 dsfwd_offset;
    uint32 offset = 0;
    uint8 critical_packet = 0;

    SYS_QUEUE_DBG_FUNC();

    CTC_ERROR_RETURN(sys_goldengate_queue_get_excp_idx_by_reason(lchip, reason_id,
                                                                excp_array,
                                                                &excp_num));
    p_reason = &p_gg_queue_master[lchip]->cpu_reason[reason_id];

    for (idx = 0; idx < excp_num; idx++)
    {
        exception = excp_array[idx];

        excp_type = SYS_REASON_GET_EXCP_TYPE(exception);
        excp_idx = SYS_REASON_GET_EXCP_INDEX(exception);

        if (SYS_REASON_FWD_EXCP == excp_type)
        {

            CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, CTC_NH_RESERVED_NHID_FOR_TOCPU,
                                                                &dsfwd_offset));
            critical_packet = 1;
            CTC_ERROR_RETURN(sys_goldengate_nh_update_entry_dsfwd(lchip, &dsfwd_offset, dest_map, nexthop_ptr, dsnh_8w, 0, critical_packet, 0));



        }
        else if (SYS_REASON_OAM_EXCP == excp_type)
        {

            if(excp_idx >=SYS_CPU_REASON_OAM_DEFECT_MESSAGE_BASE )
            {
                OamUpdateApsCtl_m aps_ctl;
                cmd = DRV_IOR(OamUpdateApsCtl_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &aps_ctl));
                SetOamUpdateApsCtl(V, destMap_f, &aps_ctl, dest_map);
                SetOamUpdateApsCtl(V, sigFailNextHopPtr_f, &aps_ctl, nexthop_ptr);
                cmd = DRV_IOW(OamUpdateApsCtl_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &aps_ctl));
            }
            else
            {
                DsOamExcp_m oam_excp;
                cmd = DRV_IOR(DsOamExcp_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &oam_excp));
                SetDsOamExcp(V, destMap_f, &oam_excp, dest_map);
                SetDsOamExcp(V, nextHopPtr_f, &oam_excp, nexthop_ptr);
                cmd = DRV_IOW(DsOamExcp_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &oam_excp));
            }

        }
        else if (SYS_REASON_FATAL_EXCP == excp_type)
        {

            /*get fatal exception resver offset*/
            CTC_ERROR_RETURN(sys_goldengate_nh_get_fatal_excp_dsnh_offset(lchip, &offset));
            offset = offset + excp_idx;
            critical_packet = 1;
            CTC_ERROR_RETURN(sys_goldengate_nh_update_entry_dsfwd(lchip, &offset, dest_map, nexthop_ptr, dsnh_8w, 0, critical_packet, 0));
        }
        else
        {
            cmd = DRV_IOW(DsMetFifoExcp_t, DsMetFifoExcp_destMap_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &dest_map));

            cmd = DRV_IOW(DsMetFifoExcp_t, DsMetFifoExcp_nextHopExt_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &dsnh_8w))

            cmd = DRV_IOW(DsBufRetrvExcp_t, DsBufRetrvExcp_nextHopPtr_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &nexthop_ptr));
			/*slice1*/
			CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx +256, cmd, &nexthop_ptr));
        }
    }

    if (excp_num == 0 && (reason_id >= CTC_PKT_CPU_REASON_CUSTOM_BASE))  /*the cpu reason is user-defined reason*/
    {
        if (dest_type != CTC_PKT_CPU_REASON_TO_DROP)
        {
            if ((CTC_PKT_CPU_REASON_TO_LOCAL_CPU == dest_type)
            || (CTC_PKT_CPU_REASON_TO_REMOTE_CPU == dest_type)
            || (CTC_PKT_CPU_REASON_TO_LOCAL_CPU_ETH == dest_type))
            {
                critical_packet = 1;
            }
            else
            {
                critical_packet = 0;
            }
            CTC_ERROR_RETURN(sys_goldengate_nh_update_entry_dsfwd(lchip, &p_reason->dsfwd_offset, dest_map, nexthop_ptr, dsnh_8w, 0, critical_packet, p_reason->lport_en));
            p_reason->is_user_define = 1;
        }
        else if(p_reason->dsfwd_offset)/*free dsfwd if CTC_PKT_CPU_REASON_TO_DROP; can not support drop for user-defined cpu reason*/
        {
            CTC_ERROR_RETURN(sys_goldengate_nh_update_entry_dsfwd(lchip, &p_reason->dsfwd_offset, dest_map, nexthop_ptr, dsnh_8w, 1, critical_packet, 0));
            p_reason->is_user_define = 0;
            p_reason->dsfwd_offset = 0;
        }

    }

    return CTC_E_NONE;
}

int32
sys_goldengate_cpu_reason_get_info(uint8 lchip, uint16 reason_id,
                                   uint32 *destmap)
{
    sys_cpu_reason_t* p_reason = NULL;
    CTC_MAX_VALUE_CHECK(reason_id, CTC_PKT_CPU_REASON_MAX_COUNT - 1);
    SYS_QOS_QUEUE_LOCK(lchip);
    p_reason = &p_gg_queue_master[lchip]->cpu_reason[reason_id];
    *destmap = p_reason->dest_map;
    SYS_QOS_QUEUE_UNLOCK(lchip);

    return CTC_E_NONE;
}


int32
sys_goldengate_cpu_reason_get_dsfwd_offset(uint8 lchip, uint16 reason_id,
                                           uint8 lport_en,
                                           uint32 *dsfwd_offset,
                                           uint32 *dsnh_offset,
                                           uint32 *dest_port)
{
    sys_cpu_reason_t* p_reason = NULL;
	CTC_MIN_VALUE_CHECK(reason_id, CTC_PKT_CPU_REASON_CUSTOM_BASE);
    CTC_MAX_VALUE_CHECK(reason_id, CTC_PKT_CPU_REASON_MAX_COUNT - 1);
    SYS_QOS_QUEUE_LOCK(lchip);
    p_reason = &p_gg_queue_master[lchip]->cpu_reason[reason_id];
    if (p_reason->is_user_define)
    {
        p_reason->lport_en = lport_en;
        *dsfwd_offset = p_reason->dsfwd_offset;
        *dsnh_offset = CTC_PKT_CPU_REASON_BUILD_NHPTR(reason_id, 0);
        if(dest_port)
        {
            *dest_port    = p_reason->dest_port;
        }
    }
    else
    {
        SYS_QOS_QUEUE_UNLOCK(lchip);
		return CTC_E_QUEUE_CPU_REASON_NOT_CREATE;
    }
    SYS_QOS_QUEUE_UNLOCK(lchip);
	return CTC_E_NONE;

}
int32
_sys_goldengate_cpu_reason_set_drop(uint8 lchip, uint16 reason_id,uint8 drop_en)
{
    IpeFwdExceptionCtl_m  igs_ctl;
    EpeHeaderEditCtl_m egs_ctl;
    uint32 cmd = 0;
    uint8 offset = 0;
	uint32 value = 0;

    sal_memset(&igs_ctl, 0, sizeof(IpeFwdExceptionCtl_m));

	if(reason_id == CTC_PKT_CPU_REASON_SFLOW_SOURCE)
	{
        cmd = DRV_IOR(BufferStoreCtl_t, BufferStoreCtl_ingressExceptionMirrorBitmap_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
		if(drop_en)
		{
		   value &= (~(1 <<7));
		}
		else
		{
		   value |= (1 <<7);
		}
        cmd = DRV_IOW(BufferStoreCtl_t, BufferStoreCtl_ingressExceptionMirrorBitmap_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
	}

    cmd = DRV_IOR(IpeFwdExceptionCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igs_ctl));

    sal_memset(&egs_ctl, 0, sizeof(EpeHeaderEditCtl_m));
    cmd = DRV_IOR(EpeHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egs_ctl));

    if (reason_id >= CTC_PKT_CPU_REASON_L2_PDU && reason_id < CTC_PKT_CPU_REASON_L3_PDU)
    {
        offset = reason_id - CTC_PKT_CPU_REASON_L2_PDU;
        DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_0_clearException_f + offset, &igs_ctl, (drop_en?1:0));
    }
    else if (reason_id >= CTC_PKT_CPU_REASON_L3_PDU && reason_id < CTC_PKT_CPU_REASON_IGMP_SNOOPING)
    {
        offset = reason_id - CTC_PKT_CPU_REASON_L3_PDU;
        DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_0_clearException_f + 64 + offset, &igs_ctl, (drop_en?1:0));
    }
    else if (reason_id == CTC_PKT_CPU_REASON_L3_ICMP_REDIRECT)
    {
        DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_0_clearException_f + 162, &igs_ctl, (drop_en?1:0));
    }
    else if (reason_id == CTC_PKT_CPU_REASON_SCL_MATCH)
    {
        DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_0_clearException_f + 160, &igs_ctl, (drop_en?1:0));
    }
    else if (reason_id == CTC_PKT_CPU_REASON_L3_MC_RPF_FAIL)
    {
        DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_0_clearException_f + 164, &igs_ctl, (drop_en?1:0));
    }
    else if (reason_id == CTC_PKT_CPU_REASON_IPMC_TTL_CHECK_FAIL)
    {
        DRV_SET_FIELD_V(EpeHeaderEditCtl_t, EpeHeaderEditCtl_exceptionArray_2_clearException_f, &egs_ctl, (drop_en?1:0));
    }
    else if (reason_id == CTC_PKT_CPU_REASON_OAM_HASH_CONFLICT)
    {
        DRV_SET_FIELD_V(EpeHeaderEditCtl_t, EpeHeaderEditCtl_exceptionArray_3_clearException_f, &egs_ctl, (drop_en?1:0));
    }
    else if (reason_id == CTC_PKT_CPU_REASON_L3_MTU_FAIL)
    {
        DRV_SET_FIELD_V(EpeHeaderEditCtl_t, EpeHeaderEditCtl_exceptionArray_5_clearException_f, &egs_ctl, (drop_en?1:0));
    }
    else
    {
        switch (reason_id)
        {
        case CTC_PKT_CPU_REASON_PTP:
             DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_131_clearException_f, &igs_ctl, (drop_en?1:0));
             DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_132_clearException_f, &igs_ctl, (drop_en?1:0));
             break;
        case CTC_PKT_CPU_REASON_MPLS_OAM:
             DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_137_clearException_f, &igs_ctl, (drop_en?1:0));
             DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_138_clearException_f, &igs_ctl, (drop_en?1:0));
             break;
        case CTC_PKT_CPU_REASON_DCN:
             DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_139_clearException_f, &igs_ctl, (drop_en?1:0));
             break;
        case CTC_PKT_CPU_REASON_SCL_MATCH:
             DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_160_clearException_f, &igs_ctl, (drop_en?1:0));
             DRV_SET_FIELD_V(EpeHeaderEditCtl_t, EpeHeaderEditCtl_exceptionArray_0_clearException_f, &egs_ctl, (drop_en?1:0));
             break;
        case CTC_PKT_CPU_REASON_ACL_MATCH:
              DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_134_clearException_f, &igs_ctl, (drop_en?1:0));
              break;
        case CTC_PKT_CPU_REASON_L2_SYSTEM_LEARN_LIMIT:
        case CTC_PKT_CPU_REASON_L2_VLAN_LEARN_LIMIT:
        case CTC_PKT_CPU_REASON_SFLOW_SOURCE:
               break;
        case CTC_PKT_CPU_REASON_L2_CPU_LEARN:
              DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_163_clearException_f, &igs_ctl, (drop_en?1:0));
              break;
        case CTC_PKT_CPU_REASON_LABEL_MISS:
              DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_135_clearException_f, &igs_ctl, (drop_en?1:0));
              break;
        case CTC_PKT_CPU_REASON_OAM_HASH_CONFLICT:
              DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_143_clearException_f, &igs_ctl, (drop_en?1:0));
              break;
        case CTC_PKT_CPU_REASON_IPFIX_HASH_CONFLICT:
              DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_146_clearException_f, &igs_ctl, (drop_en?1:0));
              break;
        case CTC_PKT_CPU_REASON_NEW_ELEPHANT_FLOW:
              DRV_SET_FIELD_V(IpeFwdExceptionCtl_t, IpeFwdExceptionCtl_array_147_clearException_f, &igs_ctl, (drop_en?1:0));
              break;
        default :
			 break;
        }
    }

    cmd = DRV_IOW(IpeFwdExceptionCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igs_ctl));

    cmd = DRV_IOW(EpeHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egs_ctl));

    return CTC_E_NONE;
}

int32
sys_goldengate_cpu_reason_set_map(uint8 lchip, uint16 reason_id,
                                  uint8 sub_queue_id,
                                  uint8 group_id)
{

    uint8  dest_type;
    uint32 dest_port;

    uint16 exception   = 0;
    uint8 excp_num     = 0;
    uint8 excp_type    = 0;
    uint16 excp_array[32] = {0};
    uint8 b_sub_mode = 0;
    ctc_chip_device_info_t device_info;

    CTC_MAX_VALUE_CHECK(group_id,(SYS_GG_MAX_CPU_REASON_GROUP_NUM-1));
    CTC_MAX_VALUE_CHECK(sub_queue_id,(SYS_GG_MAX_QNUM_PER_GROUP-1));

    dest_type = p_gg_queue_master[lchip]->cpu_reason[reason_id].dest_type;
    dest_port = p_gg_queue_master[lchip]->cpu_reason[reason_id].dest_port;
    sub_queue_id = group_id *SYS_GG_MAX_QNUM_PER_GROUP + sub_queue_id;

    if (dest_type != CTC_PKT_CPU_REASON_TO_LOCAL_CPU_ETH
        && dest_type != CTC_PKT_CPU_REASON_TO_LOCAL_CPU
        && dest_type != CTC_PKT_CPU_REASON_TO_REMOTE_CPU)
    {
        return CTC_E_NONE;
    }
    if(dest_type == CTC_PKT_CPU_REASON_TO_LOCAL_CPU_ETH)
    {
        if(!p_gg_queue_master[lchip]->have_lcpu_by_eth ||
            (sub_queue_id > p_gg_queue_master[lchip]->queue_num_per_chanel))
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    /*for check fatal exception & fwd to cpu not cut through*/
    sys_goldengate_chip_get_device_info(lchip, &device_info);
    if (device_info.version_id <= 1)
    {
        sys_goldengate_queue_get_excp_idx_by_reason(lchip, reason_id, excp_array, &excp_num);
        if (excp_num)
        {
            exception = excp_array[0];
            excp_type = SYS_REASON_GET_EXCP_TYPE(exception);
            if (SYS_REASON_FATAL_EXCP == excp_type)
            {
                b_sub_mode = 1;
            }
        }
    }

    if (((reason_id >= CTC_PKT_CPU_REASON_CUSTOM_BASE) || b_sub_mode || (reason_id == CTC_PKT_CPU_REASON_FWD_CPU))
        && (sub_queue_id < SYS_PHY_PORT_NUM_PER_SLICE) && sys_goldengate_get_cut_through_en(lchip))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_PKT_CPU_REASON_C2C_PKT == reason_id)
    {
        /*c2c pkt need 2 group*/
        CTC_MAX_VALUE_CHECK(group_id,(SYS_GG_MAX_CPU_REASON_GROUP_NUM-2));
        p_gg_queue_master[lchip]->c2c_group_base = group_id;
        sub_queue_id = group_id *SYS_GG_MAX_QNUM_PER_GROUP;
        sys_goldengate_queue_init_c2c_queue(lchip, group_id, 0, SYS_QUEUE_TYPE_EXCP);
        CTC_ERROR_RETURN(sys_goldengate_queue_sch_set_c2c_group_sched(lchip, group_id, 2));
        CTC_ERROR_RETURN(sys_goldengate_queue_sch_set_c2c_group_sched(lchip, group_id + 1, 3));
        sys_goldengate_packet_tx_set_property(lchip, SYS_PKT_TX_TYPE_C2C_SUB_QUEUE_ID, sub_queue_id, 0);
    }

    p_gg_queue_master[lchip]->cpu_reason[reason_id].sub_queue_id = sub_queue_id;

    if (CTC_PKT_CPU_REASON_MIRRORED_TO_CPU == reason_id)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_goldengate_cpu_reason_set_dest(lchip,  reason_id,dest_type,dest_port));
    return CTC_E_NONE;

}

int32
sys_goldengate_get_sub_queue_id_by_cpu_reason(uint8 lchip, uint16 reason_id, uint8* sub_queue_id)
{
    CTC_MAX_VALUE_CHECK(reason_id, CTC_PKT_CPU_REASON_MAX_COUNT - 1);
    CTC_PTR_VALID_CHECK(sub_queue_id);
    SYS_QOS_QUEUE_LOCK(lchip);
    *sub_queue_id = p_gg_queue_master[lchip]->cpu_reason[reason_id].sub_queue_id;
    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_cpu_reason_update_resrc_pool(uint8 lchip, uint16 reason_id,
                                             uint8 dest_type)
{
    uint8 pool = 0;

    if ((dest_type == CTC_PKT_CPU_REASON_TO_LOCAL_CPU ||
        dest_type == CTC_PKT_CPU_REASON_TO_LOCAL_CPU_ETH ||
        dest_type == CTC_PKT_CPU_REASON_TO_REMOTE_CPU)
        && (CTC_PKT_CPU_REASON_NEW_ELEPHANT_FLOW != reason_id))
    {
        pool = CTC_QOS_EGS_RESRC_CONTROL_POOL;
    }
    else
    {
        pool = CTC_QOS_EGS_RESRC_DEFAULT_POOL;
    }

    if (dest_type != CTC_PKT_CPU_REASON_TO_REMOTE_CPU)
    {
        CTC_ERROR_RETURN(sys_goldengate_queue_set_cpu_queue_egs_pool_classify(lchip, reason_id, pool));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_cpu_reason_set_dest(uint8 lchip,
                                  uint16 reason_id,
                                  uint8 dest_type,
                                  uint32 dest_port)
{
    uint32 dest_map = 0;
    uint8  gchip = 0;
    uint16 lport = 0;
    uint32 nexthop_ptr = 0;
    uint16 sub_queue_id = 0;
    uint8  old_dest_type = 0;
	uint8  dsnh_8w = 0;
    uint8 need_resrc_pool = 1;
    uint16 exception   = 0;
    uint16 excp_idx = 0;
    uint8 excp_num  = 0;
    uint8 excp_type = 0;
    uint16 excp_array[32] = {0};
    uint32 idx = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;

    SYS_QUEUE_DBG_FUNC();
    SYS_QUEUE_DBG_INFO("reason_id = %d, dest_type = %d, dest_port = 0x%x\n",
                       reason_id, dest_type, dest_port);

    CTC_MAX_VALUE_CHECK(reason_id, CTC_PKT_CPU_REASON_MAX_COUNT - 1);
    CTC_MAX_VALUE_CHECK(dest_type, CTC_PKT_CPU_REASON_TO_MAX_COUNT-1);

    sys_goldengate_get_gchip_id(lchip, &gchip);
    old_dest_type = p_gg_queue_master[lchip]->cpu_reason[reason_id].dest_type;
    sub_queue_id = p_gg_queue_master[lchip]->cpu_reason[reason_id].sub_queue_id;

    switch (dest_type)
    {
    case CTC_PKT_CPU_REASON_TO_LOCAL_CPU:

        if (old_dest_type != CTC_PKT_CPU_REASON_TO_LOCAL_CPU
           && sub_queue_id == 0xFF)
        {
            sub_queue_id  = 0;   /*use default sub queue id*/
        }
        dest_map = SYS_ENCODE_EXCP_DESTMAP(gchip,sub_queue_id);
        nexthop_ptr = CTC_PKT_CPU_REASON_BUILD_NHPTR(reason_id, 0);
        dest_port = CTC_GPORT_RCPU(gchip);
        break;
	case CTC_PKT_CPU_REASON_TO_LOCAL_CPU_ETH:
		if (old_dest_type != CTC_PKT_CPU_REASON_TO_LOCAL_CPU_ETH
           && sub_queue_id == 0xFF)
        {
            sub_queue_id  = 0;   /*use default sub queue id*/
        }
        dest_map = SYS_ENCODE_EXCP_BY_ETH_ESTMAP(gchip,sub_queue_id);
        nexthop_ptr = CTC_PKT_CPU_REASON_BUILD_NHPTR(reason_id, 0);
        dest_port = CTC_GPORT_RCPU(gchip);
        break;
    case CTC_PKT_CPU_REASON_TO_LOCAL_PORT:
        {
            gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(dest_port);
            lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(dest_port);
            if (TRUE == sys_goldengate_chip_is_local(lchip, gchip))
            {
                if (CTC_IS_CPU_PORT(dest_port)
                    || SYS_LPORT_IS_CPU_ETH(lchip, lport))
                {
                    return CTC_E_NOT_SUPPORT;
                }
            }
            dest_map = SYS_ENCODE_DESTMAP( gchip, lport);
            CTC_ERROR_RETURN(sys_goldengate_nh_get_resolved_offset(lchip,
                             SYS_NH_RES_OFFSET_TYPE_BYPASS_NH,
                             &nexthop_ptr));

            dsnh_8w = 1;
            sub_queue_id = 0xFF;
        }
        break;
	case CTC_PKT_CPU_REASON_TO_NHID:
		{
            sys_nh_info_dsnh_t nhinfo;
            sal_memset(&nhinfo, 0, sizeof(sys_nh_info_dsnh_t));
            CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, dest_port, &nhinfo));
            if(nhinfo.aps_en ||
				(nhinfo.nh_entry_type != SYS_NH_TYPE_BRGUC
				 && nhinfo.nh_entry_type != SYS_NH_TYPE_IPUC
            	  && nhinfo.nh_entry_type != SYS_NH_TYPE_ILOOP
            	   && nhinfo.nh_entry_type != SYS_NH_TYPE_ELOOP))
            {
                return CTC_E_NH_INVALID_NHID;
            }
            dest_map = SYS_ENCODE_DESTMAP(nhinfo.dest_chipid, nhinfo.dest_id);
            CTC_ERROR_RETURN(sys_goldengate_queue_get_excp_idx_by_reason(lchip, reason_id, excp_array, &excp_num));
            if (0 == excp_num)
            {
                return CTC_E_FEATURE_NOT_SUPPORT;
            }
            for (idx = 0; idx < excp_num; idx++)
            {
                exception = excp_array[idx];
                excp_type = SYS_REASON_GET_EXCP_TYPE(exception);
                excp_idx = SYS_REASON_GET_EXCP_INDEX(exception);
                if (SYS_REASON_NORMAL_EXCP != excp_type)
                {
                    return CTC_E_FEATURE_NOT_SUPPORT;
                }
                field_val = (SYS_NH_TYPE_ILOOP == nhinfo.nh_entry_type) ? 1 : 0;
                cmd = DRV_IOW(DsBufRetrvExcp_t, DsBufRetrvExcp_packetOffsetReset_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &field_val));
            }
            if(SYS_NH_TYPE_ILOOP == nhinfo.nh_entry_type)
            {
                dest_map |= (p_gg_queue_master[lchip]->enq_mode == 2) ? (SYS_QSEL_TYPE_RSV_PORT << 9):(SYS_QSEL_TYPE_ILOOP << 9);
                need_resrc_pool = 0;
            }
            nexthop_ptr = nhinfo.dsnh_offset;
			dsnh_8w = nhinfo.nexthop_ext;
			sub_queue_id = 0xFF;
            break;
		}

    case CTC_PKT_CPU_REASON_TO_REMOTE_CPU:
        gchip = CTC_MAP_GPORT_TO_GCHIP(dest_port);
        dest_map = SYS_ENCODE_EXCP_DESTMAP(gchip,sub_queue_id);
        nexthop_ptr = CTC_PKT_CPU_REASON_BUILD_NHPTR(reason_id, 0);
		break;
    case CTC_PKT_CPU_REASON_TO_DROP:
        sub_queue_id = 0xFF;
        CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));
        dest_port = CTC_MAP_LPORT_TO_GPORT(gchip, SYS_RSV_PORT_DROP_ID);
        dest_map = SYS_ENCODE_DESTMAP(gchip, SYS_RSV_PORT_DROP_ID);
        nexthop_ptr = CTC_PKT_CPU_REASON_BUILD_NHPTR(reason_id, 0);
        break;

    default:
        break;
    }

    /*For CPU Rx drop mirror*/
    if (dest_type == CTC_PKT_CPU_REASON_TO_DROP)
    {
        _sys_goldengate_cpu_reason_set_drop(lchip, reason_id, TRUE);
    }
    else
    {
        _sys_goldengate_cpu_reason_set_drop(lchip, reason_id, FALSE);
    }

    if ((CTC_PKT_CPU_REASON_MIRRORED_TO_CPU != reason_id)
        &&(CTC_PKT_CPU_REASON_ARP_MISS != reason_id)
        &&(CTC_PKT_CPU_REASON_C2C_PKT != reason_id))
    {
        CTC_ERROR_RETURN(sys_goldengate_cpu_reason_update_exception(lchip, reason_id,
                                                                    dest_map,
                                                                    nexthop_ptr,
                                                                    dsnh_8w,
                                                                    dest_type));

        p_gg_queue_master[lchip]->cpu_reason[reason_id].dest_type = dest_type;
        p_gg_queue_master[lchip]->cpu_reason[reason_id].sub_queue_id = sub_queue_id;
        p_gg_queue_master[lchip]->cpu_reason[reason_id].dest_port = dest_port;
        p_gg_queue_master[lchip]->cpu_reason[reason_id].dest_map = dest_map;
    }

    SYS_QUEUE_DBG_INFO("gchip = %d, dest_map = 0x%x\n",  gchip, dest_map);
    if (need_resrc_pool)
    {
        CTC_ERROR_RETURN(_sys_goldengate_cpu_reason_update_resrc_pool(lchip, reason_id, dest_type));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_cpu_reason_set_misc_param(uint8 lchip, uint16 reason_id, uint8 truncation_en)
{
    uint32 cmd         = 0;
    uint8 idx          = 0;
    uint16 exception   = 0;
    uint16 excp_idx    = 0;
    uint8 excp_num     = 0;
    uint8 excp_type    = 0;
    uint16 excp_array[32] = {0};



    SYS_QUEUE_DBG_FUNC();

    CTC_ERROR_RETURN(sys_goldengate_queue_get_excp_idx_by_reason(lchip, reason_id,
                                                                excp_array,
                                                                &excp_num));

    if (0 == excp_num)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    for (idx = 0; idx < excp_num; idx++)
    {
        exception = excp_array[idx];

        excp_type = SYS_REASON_GET_EXCP_TYPE(exception);
        excp_idx = SYS_REASON_GET_EXCP_INDEX(exception);

        if (SYS_REASON_NORMAL_EXCP != excp_type)
        {
          return CTC_E_FEATURE_NOT_SUPPORT;
        }
        else
        {
            BufferRetrieveTruncationCtl_m TruncationCtl;
            uint32 value[8];
            /*read DsMetFifoExcp_t table*/
            cmd = DRV_IOR(BufferRetrieveTruncationCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &TruncationCtl));
            GetBufferRetrieveTruncationCtl(A, truncationEn_f,  &TruncationCtl, &value);


            if (truncation_en)
                CTC_BIT_SET(value[excp_idx / 32], excp_idx % 32);
            else
                CTC_BIT_UNSET(value[excp_idx / 32], excp_idx % 32);
            SetBufferRetrieveTruncationCtl(A, truncationEn_f,  &TruncationCtl, &value);
            cmd = DRV_IOW(BufferRetrieveTruncationCtl_t, DRV_ENTRY_FLAG);
			CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &TruncationCtl));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &TruncationCtl));


        }
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_cpu_reason_set_truncation_length(uint8 lchip, uint32 length)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    SYS_QUEUE_DBG_FUNC();


    if (length < 64 || length > 144 )
    {
        return CTC_E_INVALID_PARAM;
    }

    field_val = length;
    cmd = DRV_IOW(BufferRetrieveCtl_t, BufferRetrieveCtl_truncationLength_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &field_val));
    return CTC_E_NONE;
}
int32
sys_goldengate_queue_cpu_reason_init(uint8 lchip)
{
    uint8 reason = 0;
    uint16 idx = 0;
    uint8 queue_id = 0;
    uint8 group_id = 0;
    uint8 reason_grp8[] =
    {
        CTC_PKT_CPU_REASON_IGMP_SNOOPING,
        CTC_PKT_CPU_REASON_L3_ICMP_REDIRECT,
        CTC_PKT_CPU_REASON_IP_TTL_CHECK_FAIL,
        CTC_PKT_CPU_REASON_L3_MTU_FAIL,
        CTC_PKT_CPU_REASON_L3_MC_RPF_FAIL,
        CTC_PKT_CPU_REASON_L3_IP_OPTION,
        CTC_PKT_CPU_REASON_L3_COPY_CPU,
        CTC_PKT_CPU_REASON_LINK_ID_FAIL
    };
    uint8 reason_grp9[] =
    {
       CTC_PKT_CPU_REASON_MPLS_TTL_CHECK_FAIL,
       CTC_PKT_CPU_REASON_MPLS_OAM,
       CTC_PKT_CPU_REASON_PTP,
       CTC_PKT_CPU_REASON_NEW_ELEPHANT_FLOW,
       CTC_PKT_CPU_REASON_MONITOR_BUFFER_LOG,
       CTC_PKT_CPU_REASON_MONITOR_LATENCY_LOG,
       CTC_PKT_CPU_REASON_GENEVE_PKT
    };

    uint8 reason_grp10[] =
    {
       CTC_PKT_CPU_REASON_SCL_MATCH,
       CTC_PKT_CPU_REASON_ACL_MATCH,
       CTC_PKT_CPU_REASON_SFLOW_SOURCE,
	   CTC_PKT_CPU_REASON_L2_PORT_LEARN_LIMIT,
       CTC_PKT_CPU_REASON_L2_MOVE,
       CTC_PKT_CPU_REASON_L2_COPY_CPU,
       CTC_PKT_CPU_REASON_OAM_DEFECT_MESSAGE,
    };

    uint8 reason_drop_queue[] =
    {
        CTC_PKT_CPU_REASON_DROP,
        CTC_PKT_CPU_REASON_L3_MARTIAN_ADDR,
        CTC_PKT_CPU_REASON_L3_URPF_FAIL,
        CTC_PKT_CPU_REASON_IPMC_TTL_CHECK_FAIL,
        CTC_PKT_CPU_REASON_DCN,
        CTC_PKT_CPU_REASON_L2_SYSTEM_LEARN_LIMIT,
        CTC_PKT_CPU_REASON_L2_CPU_LEARN,
        CTC_PKT_CPU_REASON_GRE_UNKNOWN,
        CTC_PKT_CPU_REASON_LABEL_MISS,
        CTC_PKT_CPU_REASON_OAM_HASH_CONFLICT,
        CTC_PKT_CPU_REASON_IPFIX_HASH_CONFLICT,
        CTC_PKT_CPU_REASON_VXLAN_NVGRE_CHK_FAIL,
        CTC_PKT_CPU_REASON_L2_STORM_CTL,
        CTC_PKT_CPU_REASON_DIAG_DISCARD_PKT,
        CTC_PKT_CPU_REASON_L3_MC_MORE_RPF
    };

    for (idx = 0; idx < CTC_PKT_CPU_REASON_MAX_COUNT; idx++)
    {
        p_gg_queue_master[lchip]->cpu_reason[idx].sub_queue_id = CTC_MAX_UINT16_VALUE;
    }

    /*L2PDU ,reason group :0~3*/
    for (idx = 0; idx < CTC_PKT_CPU_REASON_L2_PDU_RESV; idx++)
    {
        reason = CTC_PKT_CPU_REASON_L2_PDU + idx;
        group_id = idx/8;
        queue_id = idx%8;
        CTC_ERROR_RETURN(sys_goldengate_cpu_reason_set_map(lchip,  reason, queue_id, group_id));
    }
    /*L3PDU ,reason group :4~7*/
    for (idx = 0; idx < CTC_PKT_CPU_REASON_L3_PDU_RESV; idx++)
    {
        reason = CTC_PKT_CPU_REASON_L3_PDU + idx;
        group_id = idx/8 + 4;
        queue_id = idx%8;
        CTC_ERROR_RETURN(sys_goldengate_cpu_reason_set_map(lchip,  reason, queue_id, group_id));
    }
    /*L3 packet ,reason group: 8*/
    for (idx = 0; idx < sizeof(reason_grp8) / sizeof(uint8); idx++)
    {
        reason = reason_grp8[idx];
        group_id = 8;
        queue_id = idx;
        CTC_ERROR_RETURN(sys_goldengate_cpu_reason_set_map(lchip,  reason, queue_id, group_id));
    }
    /*mpls & misc packet, reason group: 9*/
    for (idx = 0; idx < sizeof(reason_grp9) / sizeof(uint8); idx++)
    {
        reason = reason_grp9[idx];
        group_id = 9;
        queue_id = idx;
        CTC_ERROR_RETURN(sys_goldengate_cpu_reason_set_map(lchip,  reason, queue_id, group_id));
    }
    /*l2 packet, reason group: 10*/
    for (idx = 0; idx < sizeof(reason_grp10) / sizeof(uint8); idx++)
    {
        reason = reason_grp10[idx];
        group_id = 10;
        queue_id = idx;
        CTC_ERROR_RETURN(sys_goldengate_cpu_reason_set_map(lchip,  reason, queue_id, group_id));
    }
    reason = CTC_PKT_CPU_REASON_SFLOW_DEST;
    group_id = 10;
    queue_id = 2;
    CTC_ERROR_RETURN(sys_goldengate_cpu_reason_set_map(lchip,  reason, queue_id, group_id));

    reason = CTC_PKT_CPU_REASON_L2_VLAN_LEARN_LIMIT;
    group_id = 10;
    queue_id = 2;
    CTC_ERROR_RETURN(sys_goldengate_cpu_reason_set_map(lchip,  reason, queue_id, group_id));

   /*OAMPDU : reason group :11~12*/
    for (idx = 0; idx < CTC_PKT_CPU_REASON_OAM_PDU_RESV; idx++)
    {
        reason = CTC_PKT_CPU_REASON_OAM + idx ;
        group_id = idx/16 + 11;;
        queue_id = idx%8;
        CTC_ERROR_RETURN(sys_goldengate_cpu_reason_set_map(lchip, reason, queue_id, group_id));
    }

    /*FWD to CPU,reason group :13*/
    reason = CTC_PKT_CPU_REASON_FWD_CPU ;
    group_id = 13;
    queue_id = 0;
    CTC_ERROR_RETURN(sys_goldengate_cpu_reason_set_map(lchip, reason, queue_id, group_id));

    for (idx = 0; idx < sizeof(reason_drop_queue) / sizeof(uint8); idx++)
    {
        reason = reason_drop_queue[idx];
        CTC_ERROR_RETURN(sys_goldengate_cpu_reason_set_dest(lchip, reason,
                                                            CTC_PKT_CPU_REASON_TO_DROP,
                                                            0));
    }

    /*Mirror to CPU,reason group :13*/
    reason = CTC_PKT_CPU_REASON_MIRRORED_TO_CPU;
    group_id = 13;
    queue_id = 0;
    CTC_ERROR_RETURN(sys_goldengate_cpu_reason_set_map(lchip,  reason, queue_id, group_id));

    if (0 == p_gg_queue_master[lchip]->enq_mode)
    {
        /*c2c*/
        reason = CTC_PKT_CPU_REASON_C2C_PKT;
        group_id = p_gg_queue_master[lchip]->c2c_group_base;
        queue_id = 0;
        CTC_ERROR_RETURN(sys_goldengate_cpu_reason_set_map(lchip,  reason, queue_id, group_id));
    }

    return CTC_E_NONE;
}


char* sys_goldengate_reason_2Str(uint16 reason_id)
{
    static char  reason_desc[64] = {0};

    if(reason_id >= CTC_PKT_CPU_REASON_L2_PDU && reason_id < CTC_PKT_CPU_REASON_L3_PDU)
    {
    sal_sprintf(reason_desc,"L2_PDU(%d)",reason_id-CTC_PKT_CPU_REASON_L2_PDU);
        return reason_desc;
    }
    else if(reason_id >= CTC_PKT_CPU_REASON_L3_PDU && reason_id < CTC_PKT_CPU_REASON_IGMP_SNOOPING)
    {
     sal_sprintf(reason_desc,"L3_PDU(%d)",reason_id-CTC_PKT_CPU_REASON_L3_PDU);
        return reason_desc;
    }
    else if(reason_id >= CTC_PKT_CPU_REASON_OAM && reason_id < CTC_PKT_CPU_REASON_OAM_DEFECT_MESSAGE)
    {
        char* OAM_CPU_Reason[32] =
        {
            "INDIVIDUAL_OAM_PDU", "All_DEFECT_TO_CPU", "LBM/LBR", "HIGHER_VERSION_OAM", "MPLS-TP_LBM_PDU",
            "MPLS-TP_CSF", "MPLS-TP_MCC_PDU", "MPLS-TP_SCC_PDU", "CCM_HAS_OPTIONAL_TLV",
            "MPLS-TP_FM_PDU ", "Test_PDU", "Ether_SLM/SLR_PDU", "BIGInterval/MEPonSW",
            "TP_BFD_CV_PDU", "MPLS-TP_DLM", "MPLS-TP DM/DLMDM", "APS_PDU",
            "UNKNOWEN_PDU", "DM_PDU", "LEARNING_CCM_TO_CPU", "Y1731_CSF_PDU",
            "BFD_DISC_MISMATCH", "Y1731_MCC_PDU", "PBT_mmDefect_OAM", "LTM/LTR_PDU",
            "OAM_EXCP(25)", "OAM_EXCP(26)", "LM_PDU", "LEARNING_BFD_TO_CPU",
            "OAM_EXCP(29)", "BFD_TIMER_NEGOTIATE", "Y1731_SCC_PDU"
        };

        return OAM_CPU_Reason[reason_id-CTC_PKT_CPU_REASON_OAM];
    }
   switch (reason_id)
    {
    case CTC_PKT_CPU_REASON_IGMP_SNOOPING :/* [GB] IGMP-Snooping*/
		 return "IGMP_SNOOPING";
      case CTC_PKT_CPU_REASON_PTP:                 /* [GB.GG] PTP */
	  	return "PTP";
      case CTC_PKT_CPU_REASON_MPLS_OAM:            /* [GB.GG] TMPLS or MPLS protocol OAM */
	  	 return "MPLS_OAM";
      case CTC_PKT_CPU_REASON_DCN:                /* [GB.GG] DCN */
	  	  return "DCN";
      case CTC_PKT_CPU_REASON_SCL_MATCH:           /* [GB.GG] SCL entry match */
	  	 return "SCL_MATCH";
      case CTC_PKT_CPU_REASON_ACL_MATCH:           /* [GB.GG] ACL entry match */
	  	 return "ACL_MATCH";
      case CTC_PKT_CPU_REASON_SFLOW_SOURCE:        /* [GB.GG] SFlow source */
	  	return "SFLOW_SOURCE";
      case CTC_PKT_CPU_REASON_SFLOW_DEST:         /* [GB.GG] SFlow destination */
          return "SFLOW_DEST";
      case CTC_PKT_CPU_REASON_L2_PORT_LEARN_LIMIT:     /* [GB.GG] MAC learn limit base on port */
	  	  return "PORT_LEARN_LIMIT";
     case CTC_PKT_CPU_REASON_L2_VLAN_LEARN_LIMIT:     /* [GB.GG] MAC learn limit base on vlan/vsi */
	 	return "VLAN_LEARN_LIMIT";
     case CTC_PKT_CPU_REASON_L2_SYSTEM_LEARN_LIMIT:   /* [GB.GG] MAC learn limit base on system */
	 	return "SYSTEM_LEARN_LIMIT";
     case CTC_PKT_CPU_REASON_L2_MOVE:             /* [GB.GG] MAC SA station move */
	 	return "L2_MOVE";
     case CTC_PKT_CPU_REASON_L2_CPU_LEARN:        /* [GB.GG] MAC SA CPU learning */
	 	return "L2_CPU_LEARN";
     case CTC_PKT_CPU_REASON_L2_COPY_CPU:         /* [GB.GG] MAC DA copy to CPU */
	 	return "L2_COPY_CPU";
     case CTC_PKT_CPU_REASON_IP_TTL_CHECK_FAIL:   /* [GB.GG] IP TTL check fail */
	 	return "IP_TTL_CHECK_FAIL";

     case CTC_PKT_CPU_REASON_L3_MTU_FAIL:         /* [GB.GG] IP MTU check fail */
	 	return "L3_MTU_FAIL";
     case CTC_PKT_CPU_REASON_L3_MC_RPF_FAIL:     /* [GB.GG] IP multicast RPF check fail */
	 	return "L3_MC_RPF_FAIL";
     case CTC_PKT_CPU_REASON_L3_MC_MORE_RPF:      /* [GB.GG] IP multicast more RPF */
	 	return "L3_MC_MORE_RPF";
     case CTC_PKT_CPU_REASON_L3_IP_OPTION:        /* [GB.GG] IP option */
	 	return "L3_IP_OPTION";
     case CTC_PKT_CPU_REASON_L3_ICMP_REDIRECT:    /* [GB.GG] IP ICMP redirect */
	 	return "L3_ICMP_REDIRECT";
     case CTC_PKT_CPU_REASON_L3_COPY_CPU:        /* [GB.GG] IP DA copy to CPU */
	 	return "L3_COPY_CPU";


     case CTC_PKT_CPU_REASON_L3_MARTIAN_ADDR:     /* [GB.GG] IP martian address */
	 	return "L3_MARTIAN_ADDR";
     case CTC_PKT_CPU_REASON_L3_URPF_FAIL:        /* [GB.GG] IP URPF fail */
	 	return "L3_URPF_FAIL";
     case CTC_PKT_CPU_REASON_IPMC_TTL_CHECK_FAIL: /* [GB.GG] IP multicast ttl check fail */
	 	return "IPMC_TTL_CHECK_FAIL";
     case CTC_PKT_CPU_REASON_MPLS_TTL_CHECK_FAIL: /* [GB.GG] MPLS ttl check fail */
	 	return "MPLS_TTL_CHECK_FAIL";
     case CTC_PKT_CPU_REASON_GRE_UNKNOWN:         /* [GB.GG] GRE unknown */
	 	return "GRE_UNKNOWN";
     case CTC_PKT_CPU_REASON_LABEL_MISS:          /* [GB.GG] MPLS label out of range */
	 	return "LABEL_MISS";
     case CTC_PKT_CPU_REASON_LINK_ID_FAIL:        /* [GB.GG] IP Link id check fail */
	 	return "LINK_ID_FAIL";
     case CTC_PKT_CPU_REASON_OAM_HASH_CONFLICT:   /* [GB.GG] OAM hash conflict */
	 	return "OAM_HASH_CONFLICT";
     case CTC_PKT_CPU_REASON_IPFIX_HASH_CONFLICT:     /* [GG] IP Fix conflict*/
	 	return "IPFIX_HASHCONFLIC";
     case CTC_PKT_CPU_REASON_VXLAN_NVGRE_CHK_FAIL:   /* [GG] Vxlan or NvGre check fail */
          return "VXLAN_NVGRE_CHK_FAIL";
     case CTC_PKT_CPU_REASON_OAM_DEFECT_MESSAGE: /*OAM Defect message from oam engine */
           return "OAM_DEFECT_MESSAGE";
	 case CTC_PKT_CPU_REASON_FWD_CPU:              /* packet redirect forward to CPU */
	 	   return "FWD_CPU";
	 case CTC_PKT_CPU_REASON_NEW_ELEPHANT_FLOW:     /* [GG] new elephant flow  */
	 	return "NEW_ELEPHANT_FLOW";
     case CTC_PKT_CPU_REASON_L2_STORM_CTL:
        return "L2_STORM_CTL";  /*[GB.GG] storm control log*/
     case CTC_PKT_CPU_REASON_MONITOR_BUFFER_LOG:
        return "MONITOR_BUFFER"; /*[GG] Monitor buffer log*/
     case CTC_PKT_CPU_REASON_MONITOR_LATENCY_LOG:
        return "MONITOR_LATENCY"; /*[GG] Monitor buffer log*/
     case CTC_PKT_CPU_REASON_QUEUE_DROP_PKT:
        return "QUEUE_DROP_PKT"; /*[GG] queue drop packet*/
     case CTC_PKT_CPU_REASON_GENEVE_PKT:
        return "GENEVE_PKT"; /*[GG] Gnveve option packet */
	 case CTC_PKT_CPU_REASON_MIRRORED_TO_CPU:
        return "MIRRORED_TO_CPU"; /*[GG] MIRRORED_TO_CPU */
    case CTC_PKT_CPU_REASON_DIAG_DISCARD_PKT:
        return "DIAG_DISCARD_PKT"; /*[GG] DIAG_DISCARD_PKT */
    case CTC_PKT_CPU_REASON_C2C_PKT:
        return "C2C_PKT"; /* C2C_PKT */
     default:
	 	sal_sprintf(reason_desc,"Reason(%d)",reason_id);
		return reason_desc;
    }
}
char*
sys_goldengate_reason_destType2Str(uint8 dest_type)
{

    switch (dest_type)
    {
    case CTC_PKT_CPU_REASON_TO_LOCAL_CPU:
        return "LCPU";
        break;

    case CTC_PKT_CPU_REASON_TO_LOCAL_PORT:
        return "LPort";
        break;

    case CTC_PKT_CPU_REASON_TO_REMOTE_CPU:
        return "RCPU";
        break;
    case CTC_PKT_CPU_REASON_TO_DROP:
        return "Drop";
        break;
	case CTC_PKT_CPU_REASON_TO_NHID:
      return "NHID";
        break;
	case CTC_PKT_CPU_REASON_TO_LOCAL_CPU_ETH:
        return "LCPU_ETH";
        break;
    default:
        break;
    }

    return "-";
}

char*
sys_goldengate_exception_type_str(uint8 lchip, uint16 exception_index,uint8 *sub_index)
{
    uint8 excp_type = 0;


    excp_type =  SYS_REASON_GET_EXCP_TYPE(exception_index);
    exception_index = SYS_REASON_GET_EXCP_INDEX(exception_index);
	if(excp_type == SYS_REASON_OAM_EXCP)
	{
	    *sub_index =  0;
	    return "OAM exception";
	}
	else if(excp_type == SYS_REASON_FWD_EXCP)
	{
	   *sub_index =  0;
	  return "Fwd exception";
	}
	else if(excp_type == SYS_REASON_FATAL_EXCP)
	{
	   *sub_index =  0;
	  return "Fatal exception";
	}
    else if(exception_index < 24)
    {    /*unexpect*/
       *sub_index = 0;
        return "Ingress mirror";
    }
	else if(exception_index < 32)
	{
       *sub_index = 0;
        return "Normal exception";
    }
	else if(exception_index < 44)  /*32~43*/
	{    /*unexpect*/
       *sub_index = 0;
        return "Egress mirror";
    }
	else if(exception_index < 56)  /*44-63*/
	{
       *sub_index = 0;
        return "Normal exception";
    }
	else if(exception_index < 64)  /*56-63*/
	{
       *sub_index = exception_index - 56;;
        return "Normal exception(epe)";
    }
	else if(exception_index < 128)  /*64~127 */
	{
       *sub_index = exception_index -64;
        return "Normal exception(2)";
    }
	else if(exception_index < 192)  /*128~191 */
	{
       *sub_index = exception_index -128;
        return "Normal exception(3)";
    }
	else if(exception_index < 224)  /*192~223 */
	{
       *sub_index = exception_index -192;
        return "Normal exception(other)";
    }
	else if(exception_index < 255)
	{
       *sub_index = 0;
        return "Normal exception";
    }
   *sub_index = 0;
	return "Other exception";
}

int32
sys_goldengate_qos_cpu_reason_dump(uint8 lchip, uint16 start, uint16 end, uint8 detail)
{

    sys_cpu_reason_t* p_reason = NULL;
	ctc_qos_queue_id_t queue;
	sys_queue_group_node_t*p_sys_group_node = NULL;
	sys_queue_node_t      *p_queue_node = NULL;
	uint32 token_rate = 0;
    uint32 token_thrd = 0;
    uint16 queue_id ;
	uint16 dest_port = 0;
    uint16 reason_id = 0;
	uint8 is_pps = 0;
    uint8 loop = 0;

    SYS_QOS_QUEUE_INIT_CHECK(lchip);

    SYS_QOS_QUEUE_LOCK(lchip);
    SYS_QUEUE_DBG_DUMP("CPU reason information:\n");
	SYS_QUEUE_DBG_DUMP("Shaping mode %s local CPU\n",p_gg_queue_master[lchip]->shp_pps_en ? "PPS":"BPS");
	SYS_QUEUE_DBG_DUMP("Queue base %d for local CPU\n",p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXCP]);
	if(p_gg_queue_master[lchip]->have_lcpu_by_eth)
	{
	  SYS_QUEUE_DBG_DUMP("Channel Id: %d for local CPU by network port\n",p_gg_queue_master[lchip]->cpu_eth_chan_id);
 	  SYS_QUEUE_DBG_DUMP("Queue base %d for local CPU by network port\n",p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXCP_BY_ETH]);
    }
	SYS_QUEUE_DBG_DUMP("PIR unit:pps(PPS)/kbps(BPS)\n");
	SYS_QUEUE_DBG_DUMP(" \n" );
	SYS_QUEUE_DBG_DUMP("CPU reason Detail information:\n");
    SYS_QUEUE_DBG_DUMP("%-6s %-20s %-8s %-7s %-7s %-9s %-9s\n", "Id", "reason" ,"queue-id", "chanenl", "PIR","dest-type", "dest-id");

	SYS_QUEUE_DBG_DUMP("--------------------------------------------------------------------------\n");
    for (reason_id = start; reason_id <= end && (reason_id < CTC_PKT_CPU_REASON_MAX_COUNT); reason_id++)
    {
        p_reason = &p_gg_queue_master[lchip]->cpu_reason[reason_id];

        if (p_reason && (CTC_PKT_CPU_REASON_TO_REMOTE_CPU == p_reason->dest_type))
        {
            SYS_QUEUE_DBG_DUMP("%-6d %-45s %-8s %-7s %-7s %9s %5d\n",
                               reason_id, sys_goldengate_reason_2Str(reason_id), "-" , "-",
                               "-", sys_goldengate_reason_destType2Str(p_reason->dest_type), p_reason->sub_queue_id);
            continue;
        }

		queue.cpu_reason = reason_id;
        queue.queue_type = CTC_QUEUE_TYPE_EXCP_CPU;
        sys_goldengate_queue_get_queue_id(lchip, &queue, &queue_id);
        p_sys_group_node = ctc_vector_get(p_gg_queue_master[lchip]->group_vec, queue_id/8);
        p_queue_node = ctc_vector_get(p_gg_queue_master[lchip]->queue_vec, queue_id);

        if (!p_reason || !p_sys_group_node || !p_queue_node)
        {
           continue;
        }


		is_pps = sys_goldengate_queue_shp_get_channel_pps_enbale(lchip, p_sys_group_node->chan_id);

		if(p_queue_node->shp_en)
		{

		  sys_goldengate_qos_map_token_rate_hw_to_user(lchip, is_pps,p_queue_node->p_shp_profile->bucket_rate,&token_rate,256,p_gg_queue_master[lchip]->que_shp_update_freq );

          SYS_QUEUE_DBG_DUMP("%-6d %-20s %-8d %-7d %-7u ",
                           reason_id,sys_goldengate_reason_2Str(reason_id),
                           queue_id ,p_sys_group_node->chan_id,token_rate);
		}
		else
		{

		    SYS_QUEUE_DBG_DUMP("%-6d %-20s %-8d %-7d %-7s ",
                           reason_id,sys_goldengate_reason_2Str(reason_id),
                           queue_id ,p_sys_group_node->chan_id, "-");
		}

		if(p_reason->dest_type == CTC_PKT_CPU_REASON_TO_LOCAL_PORT
			|| p_reason->dest_type == CTC_PKT_CPU_REASON_TO_NHID
			|| p_reason->dest_type == CTC_PKT_CPU_REASON_TO_DROP)
		{
			dest_port = p_reason->dest_port;
		}
        else if(p_reason->dest_type == CTC_PKT_CPU_REASON_TO_LOCAL_CPU_ETH)
        {
            if (p_gg_queue_master[lchip]->queue_num_per_chanel == 16)
            {
                dest_port = p_reason->sub_queue_id & 0xF;
            }
            else  if(p_gg_queue_master[lchip]->queue_num_per_chanel == 8)
            {
                dest_port = p_reason->sub_queue_id & 0x7;
            }
        }
        else
        {
            dest_port = p_reason->sub_queue_id;
        }

        SYS_QUEUE_DBG_DUMP("%9s %5d",
                               sys_goldengate_reason_destType2Str(p_reason->dest_type),
                                dest_port);
        SYS_QUEUE_DBG_DUMP("\n");

    }

    if (start != end)
    {
        SYS_QOS_QUEUE_UNLOCK(lchip);
        return CTC_E_NONE;
    }

    if (p_reason && CTC_PKT_CPU_REASON_TO_NHID == p_reason->dest_type)
    {
        p_queue_node = NULL;
    }

    reason_id = start;
	if ((start == end) && p_queue_node && p_sys_group_node)
    {

    	uint8 slice_channel = 0;
        uint32 cmd;
        DsChanShpProfile_m  chan_shp_profile;
    	QMgrChanShapeCtl_m chan_shape_ctl;
    	uint32 shp_en = 0;
    	uint32 chan_shp_en[2];
    	uint32 token_thrd_shift = 0;
    	uint32 token_rate_frac = 0;

        SYS_QUEUE_DBG_DUMP("\nQueue shape:\n");
        SYS_QUEUE_DBG_DUMP("-------------------------------\n");

		if(p_queue_node->shp_en)
		{
            sys_goldengate_qos_map_token_rate_hw_to_user(lchip, is_pps, p_queue_node->p_shp_profile->bucket_rate, &token_rate, 256, p_gg_queue_master[lchip]->que_shp_update_freq );
            sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &token_thrd , p_queue_node->p_shp_profile->bucket_thrd, SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);
            SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd);
		    SYS_QUEUE_DBG_DUMP("%-16s: %u (%s)\n","PIR",token_rate,is_pps ?"pps":"kbps");
			SYS_QUEUE_DBG_DUMP("%-16s: %u (%s)\n","PBS",token_thrd, is_pps ?"pkt":"kb");
		}
		else
		{
		   SYS_QUEUE_DBG_DUMP("No PIR Bucket\n");
		}
		SYS_QUEUE_DBG_DUMP("\nGroup shape:\n");
        SYS_QUEUE_DBG_DUMP("-------------------------------\n");
		if(p_sys_group_node && p_sys_group_node->grp_shp[0].shp_bitmap && p_sys_group_node->grp_shp[0].p_shp_profile)
		{
            sys_goldengate_qos_map_token_rate_hw_to_user(lchip, is_pps, p_sys_group_node->grp_shp[0].p_shp_profile->bucket_rate, &token_rate, 256, p_gg_queue_master[lchip]->que_shp_update_freq );
            sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &token_thrd , p_sys_group_node->grp_shp[0].p_shp_profile->bucket_thrd, SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);
            SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd);
		    SYS_QUEUE_DBG_DUMP("%-16s: %u (%s)\n","PIR",token_rate ,is_pps ?"pps":"kbps");
			SYS_QUEUE_DBG_DUMP("%-16s: %u (%s)\n","PBS",token_thrd, is_pps ?"pkt":"kb");
		}
		else
		{
		   SYS_QUEUE_DBG_DUMP("No PIR Bucket\n");
		}

		cmd = DRV_IOR(DsChanShpProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, p_sys_group_node->chan_id, cmd, &chan_shp_profile));

        token_thrd = GetDsChanShpProfile(V, tokenThrdCfg_f, &chan_shp_profile);
        token_thrd_shift = GetDsChanShpProfile(V, tokenThrdShift_f  , &chan_shp_profile);
        token_rate_frac = GetDsChanShpProfile(V, tokenRateFrac_f, &chan_shp_profile);
        token_rate = GetDsChanShpProfile(V, tokenRate_f  , &chan_shp_profile);


        sys_goldengate_qos_map_token_rate_hw_to_user(lchip, is_pps,(token_rate <<8 |token_rate_frac)  ,&token_rate,256,p_gg_queue_master[lchip]->chan_shp_update_freq );
        sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &token_thrd ,(token_thrd << SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH | token_thrd_shift ),SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);
        SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd);

        cmd = DRV_IOR(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, (p_sys_group_node->chan_id > 64), cmd, &chan_shape_ctl));
        GetQMgrChanShapeCtl(A, chanShpEn_f, &chan_shape_ctl, &chan_shp_en[0]);

        slice_channel = (p_sys_group_node->chan_id > 64 ) ? (p_sys_group_node->chan_id - 64):p_sys_group_node->chan_id;
        shp_en = (slice_channel < 32) ? CTC_IS_BIT_SET(chan_shp_en[0], slice_channel): CTC_IS_BIT_SET(chan_shp_en[1], (slice_channel - 32));
		SYS_QUEUE_DBG_DUMP("\nPort shape:\n");
        SYS_QUEUE_DBG_DUMP("-------------------------------\n");
		if(shp_en)
		{
          SYS_QUEUE_DBG_DUMP("%-16s: %u (%s)\n","PIR",token_rate,is_pps ?"pps":"kbps" );
			SYS_QUEUE_DBG_DUMP("%-16s: %u (%s)\n","PBS",token_thrd, is_pps ?"pkt":"kb");
		}
		else
		{
		   SYS_QUEUE_DBG_DUMP("No PIR Bucket\n");
		}

    }

    SYS_QOS_QUEUE_UNLOCK(lchip);

   if ((start == end) && p_queue_node)
   {
       ctc_qos_sched_queue_t  queue_sched;
	   ctc_qos_sched_group_t  group_sched;

	   sys_goldengate_queue_sch_get_queue_sched(lchip, queue_id ,&queue_sched);

        SYS_QUEUE_DBG_DUMP("\nQueue Scheduler:\n");
        SYS_QUEUE_DBG_DUMP("-------------------------------\n");
        SYS_QUEUE_DBG_DUMP("%-16s: %d \n","Queue Class(Green)",queue_sched.confirm_class);
		SYS_QUEUE_DBG_DUMP("%-16s: %d \n","Queue Class(Yellow)",queue_sched.exceed_class);
        SYS_QUEUE_DBG_DUMP("%-16s: %d \n","Weight", queue_sched.exceed_weight);



        SYS_QUEUE_DBG_DUMP("\nGroup Scheduler:\n");
        SYS_QUEUE_DBG_DUMP("-------------------------------\n");

        SYS_QUEUE_DBG_DUMP("%-12s %-10s %-6s\n", "Queue-Class", "Class-PRI", "Weight");
		SYS_QUEUE_DBG_DUMP("-------------------------------\n");
        for (loop = 0;  loop < 8; loop++)
        {
            group_sched.queue_class = loop;
            sys_goldengate_queue_sch_get_group_sched(lchip, queue_id/4 , &group_sched);
            SYS_QUEUE_DBG_DUMP("%-12d %-10d %-6d\n", loop, group_sched.class_priority, group_sched.weight);
        }

   }


  if (start == end)
    {

        uint16 excp_array[32];
        uint8 excp_num = 0;
        uint32 offset = 0;
        uint8 excp_type = 0;
        uint8 excp_idx = 0;



        sys_goldengate_queue_get_excp_idx_by_reason(lchip, start, excp_array, &excp_num);
        if (excp_num != 0)
        {
            SYS_QUEUE_DBG_DUMP("\nCPU Reason Table:\n");
		    SYS_QUEUE_DBG_DUMP("-------------------------------------------------\n");
            SYS_QUEUE_DBG_DUMP("%-16s %-7s\n",   "Table", "Offset");
            SYS_QUEUE_DBG_DUMP("-------------------------------------------------\n");

            for (loop = 0; loop < excp_num; loop++)
            {
                excp_type = SYS_REASON_GET_EXCP_TYPE(excp_array[loop]);
                excp_idx = SYS_REASON_GET_EXCP_INDEX(excp_array[loop]);
                if (SYS_REASON_FWD_EXCP == excp_type)
                {
                    uint32  dsfwd_offset;

                    CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, CTC_NH_RESERVED_NHID_FOR_TOCPU,
                                                                        &dsfwd_offset));
                    SYS_QUEUE_DBG_DUMP("%-16s %-7d [%s]\n",   "DsFwd", dsfwd_offset, "3w");

                }
                else if (SYS_REASON_OAM_EXCP == excp_type)
                {
                    if ((excp_idx + CTC_PKT_CPU_REASON_OAM) == CTC_PKT_CPU_REASON_OAM_DEFECT_MESSAGE )
                    {
                        SYS_QUEUE_DBG_DUMP("%-16s %-7d\n",   "OamUpdateApsCtl_t", 0);
                    }
                    else
                    {
                        SYS_QUEUE_DBG_DUMP("%-16s %-7d\n",   "DsOamExcp", excp_idx);
                    }
                }
                else if (SYS_REASON_FATAL_EXCP == excp_type)
                {
                    CTC_ERROR_RETURN(sys_goldengate_nh_get_fatal_excp_dsnh_offset(lchip, &offset));
                    offset = offset + excp_idx;
                    SYS_QUEUE_DBG_DUMP("%-16s %-7d [%s]\n",   "DsFwd", offset, "3w");
                }
                else if (SYS_REASON_NORMAL_EXCP == excp_type)
                {
                    SYS_QUEUE_DBG_DUMP("%-16s %-7d\n",   "DsMetFifoExcp", excp_idx);
                }

            }
        }
    }
    return CTC_E_NONE;
}


