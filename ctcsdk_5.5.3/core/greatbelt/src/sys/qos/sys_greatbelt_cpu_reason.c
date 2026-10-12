/**
 @file sys_greatbelt_cpu_reason.c

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

#include "sys_greatbelt_chip.h"

#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_queue_drop.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_internal_port.h"

#include  "sys_greatbelt_cpu_reason.h"

#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_lib.h"

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
    SYS_FATAL_EXCP_RESERVED1                        = 15
};
typedef enum sys_fatal_exception_e sys_fatal_exception_t;

enum exception_type_ipe_e
{
    SYS_NORMAL_EXCP_TYPE_USER_ID              = 24,
    SYS_NORMAL_EXCP_TYPE_PROTOCOL_VLAN        = 25,
    SYS_NORMAL_EXCP_TYPE_LAYER2               = 64,
    SYS_NORMAL_EXCP_TYPE_LAYER3               = 128,
    SYS_NORMAL_EXCP_TYPE_ICMP_REDIRECT        = 28,
    SYS_NORMAL_EXCP_TYPE_LEARNING_CACHE_FULL  = 29,
    SYS_NORMAL_EXCP_TYPE_ROUTE_MCAST_RPF_FAIL = 30,
    SYS_NORMAL_EXCP_TYPE_OTHER                = 192
};
typedef enum exception_type_ipe_e exception_type_ipe_t;

#define SYS_NORMAL_EXCP_TYPE_IGS_PORT_LOG   31
#define SYS_NORMAL_EXCP_TYPE_EGS_PORT_LOG   27

enum exception_type_epe_e
{
    SYS_NORMAL_EXCP_TYPE_EGRESS_VLAN_XLATE  = 56,
    SYS_NORMAL_EXCP_TYPE_PARSER_LEN_ERROR   = 57,
    SYS_NORMAL_EXCP_TYPE_TTL_CHK_FAIL       = 58,
    SYS_NORMAL_EXCP_TYPE_OAM_HASH_CONFLICT  = 59,
    SYS_NORMAL_EXCP_TYPE_OAM_2              = 60,
    SYS_NORMAL_EXCP_TYPE_MTU_CHK_FAIL       = 61,
    SYS_NORMAL_EXCP_TYPE_STRIP_OFFSET_ERROR = 62,
    SYS_NORMAL_EXCP_TYPE_ICMP_ERROR_MSG     = 63
};
typedef enum exception_type_epe_e exception_type_epe_t;

enum sys_normal_excp2_sub_type_e
{
    /* 1 -- 53 User define */
    SYS_NORMAL_EXCP_SUB_TYPE_EFM               = 0,
    SYS_NORMAL_EXCP_SUB_TYPE_SYS_SECURITY      = 54,
    SYS_NORMAL_EXCP_SUB_TYPE_PARSER_LEN_ERROR  = 55,
    SYS_NORMAL_EXCP_SUB_TYPE_LAYER2_PTP        = 56,
    SYS_NORMAL_EXCP_SUB_TYPE_STORM_CTL         = 58,
    SYS_NORMAL_EXCP_SUB_TYPE_VSI_SECURITY      = 59,
    SYS_NORMAL_EXCP_SUB_TYPE_VLAN_SECURITY     = 60,
    SYS_NORMAL_EXCP_SUB_TYPE_PORT_SECURITY     = 61,
    SYS_NORMAL_EXCP_SUB_TYPE_SRC_MISMATCH      = 62,
    SYS_NORMAL_EXCP_SUB_TYPE_MAC_SA            = 63
};
typedef enum sys_normal_excp2_sub_type_e sys_normal_excp2_sub_type_t;

enum sys_normal_excp3_sub_type_e
{
    SYS_NORMAL_EXCP_SUB_TYPE_IP_DA            = 63
};
typedef enum sys_normal_excp3_sub_type_e sys_normal_excp3_sub_type_t;

enum sys_normal_excp_other_sub_type_e
{
    SYS_NORMAL_EXCP_SUB_TYPE_RESERVED0             = 0,
    SYS_NORMAL_EXCP_SUB_TYPE_SGMAC_CTL_MSG         = 1,
    SYS_NORMAL_EXCP_SUB_TYPE_PARSER_LEN_ERR        = 2,
    SYS_NORMAL_EXCP_SUB_TYPE_PBB_TCI_NCA           = 3,
    SYS_NORMAL_EXCP_SUB_TYPE_PTP                   = 4,
    SYS_NORMAL_EXCP_SUB_TYPE_PLD_OFFSET_ERR        = 5,
    SYS_NORMAL_EXCP_SUB_TYPE_ACL                   = 6,
    SYS_NORMAL_EXCP_SUB_TYPE_RESERVED1             = 7,
    SYS_NORMAL_EXCP_SUB_TYPE_ETHERNET_OAM_ERR      = 8,
    SYS_NORMAL_EXCP_SUB_TYPE_MPLS_OAM_TTL_CHK_FAIL = 9,
    SYS_NORMAL_EXCP_SUB_TYPE_MPLS_TMPLS_OAM        = 10,
    SYS_NORMAL_EXCP_SUB_TYPE_DCN                   = 11,
    SYS_NORMAL_EXCP_SUB_TYPE_MPLS_ACH_OAM          = 12,
    SYS_NORMAL_EXCP_SUB_TYPE_ACH_ERR               = 13,
    SYS_NORMAL_EXCP_SUB_TYPE_SECTION_OAM_EXP       = 14,
    SYS_NORMAL_EXCP_SUB_TYPE_OAM_HASH_CONFLICT     = 15,
    SYS_NORMAL_EXCP_SUB_TYPE_NO_MEP_MIP_DIS        = 16
};
typedef enum sys_normal_excp_other_sub_type_e sys_normal_excp_other_sub_type_t;

extern sys_queue_master_t* p_gb_queue_master[CTC_MAX_LOCAL_CHIP_NUM];

/*****************************************************************************
 Prototype    : sys_greatbelt_queue_get_excp_idx_by_reason
 Description  : get exception index by cpu reason id
 Input        : uint8 reason_id
 Output       : uint16 *excp_idx
              : uint8  *excp_num
 Return Value : CTC_E_XXX
 History      :
 Date         : 2012/12/21
 Modification : Created function

*****************************************************************************/
int32
sys_greatbelt_queue_get_excp_idx_by_reason(uint8 lchip, uint16 reason_id,
                                           uint16* excp_idx,
                                           uint8* excp_num)
{
    uint8 idx = 0;

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
        break;

    case CTC_PKT_CPU_REASON_MPLS_OAM:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_MPLS_TMPLS_OAM);

        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_OTHER,
                                                SYS_NORMAL_EXCP_SUB_TYPE_MPLS_ACH_OAM);
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

    case CTC_PKT_CPU_REASON_L2_VSI_LEARN_LIMIT:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                SYS_NORMAL_EXCP_SUB_TYPE_VSI_SECURITY);
        break;

    case CTC_PKT_CPU_REASON_L2_VLAN_LEARN_LIMIT:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                SYS_NORMAL_EXCP_SUB_TYPE_VLAN_SECURITY);
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

        break;

    case CTC_PKT_CPU_REASON_L2_COPY_CPU:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                SYS_NORMAL_EXCP_SUB_TYPE_MAC_SA);
        break;

    case CTC_PKT_CPU_REASON_L2_STORM_CTL:
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_NORMAL_EXCP,
                                                SYS_NORMAL_EXCP_TYPE_LAYER2,
                                                SYS_NORMAL_EXCP_SUB_TYPE_STORM_CTL);
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

    case CTC_PKT_CPU_REASON_L3_MC_MORE_RPF:
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
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_OAM_EXCP,
                                                reason_id - CTC_PKT_CPU_REASON_OAM,
                                                0);
    }

    /* forward to cpu*/
    if (reason_id == CTC_PKT_CPU_REASON_FWD_CPU)
    {
        excp_idx[idx++] = SYS_REASON_ENCAP_EXCP(SYS_REASON_FWD_EXCP,
                                                0,
                                                0);
    }

    *excp_num = idx;
    if ((0 == idx) && (reason_id < CTC_PKT_CPU_REASON_CUSTOM_BASE))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    SYS_QUEUE_DBG_INFO("excp_num = %d, excp_idx[0] = %d \n", *excp_num, excp_idx[0]);

    return CTC_E_NONE;
}

int32
sys_greatbelt_cpu_reason_get_info(uint8 lchip, uint16 reason_id,
                                   uint32 *destmap)
{
    sys_queue_reason_t *p_reason = NULL;
    CTC_MAX_VALUE_CHECK(reason_id, CTC_PKT_CPU_REASON_MAX_COUNT - 1);

    p_reason = ctc_vector_get(p_gb_queue_master[lchip]->reason_vec, reason_id);

    if (NULL == p_reason)
    {
        return CTC_E_QUEUE_CPU_REASON_NOT_CREATE;
    }
    *destmap = p_reason->dest_map;

    return CTC_E_NONE;
}

int32
sys_greatbelt_cpu_reason_get_dsfwd_offset(uint8 lchip, uint16 reason_id, uint8 lport_en,
                                           uint32 *dsfwd_offset,
                                           uint16 *dsnh_offset,
                                           uint32 *dest_port)
{
    sys_queue_reason_t *p_reason = NULL;
    CTC_MIN_VALUE_CHECK(reason_id, CTC_PKT_CPU_REASON_CUSTOM_BASE);
    CTC_MAX_VALUE_CHECK(reason_id, CTC_PKT_CPU_REASON_MAX_COUNT - 1);

    p_reason = ctc_vector_get(p_gb_queue_master[lchip]->reason_vec, reason_id);
    if (NULL == p_reason)
    {
        return CTC_E_QUEUE_CPU_REASON_NOT_CREATE;
    }

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
        return CTC_E_QUEUE_CPU_REASON_NOT_CREATE;
    }
    return CTC_E_NONE;

}

/*****************************************************************************
 Prototype    : _sys_greatbelt_queue_update_exception
 Description  : update cpu exception include normal and fatal
 Input        : uint8 lchip
                uint8 reason_id
                uint32 dest_map
 Output       : None
 Return Value : CTC_E_XXX
 History      :
 Date         : 2012/12/21
 Modification : Created function

*****************************************************************************/
int32
sys_greatbelt_cpu_reason_update_exception(uint8 lchip,
                                          uint16 reason_id,
                                          uint32 dest_map,
                                          uint32 nexthop_ptr,
                                          uint8 dsnh_8w,
                                          uint8 dest_type)
{
    uint32 cmd         = 0;
    uint8 idx          = 0;
    uint16 exception   = 0;
    uint16 excp_idx    = 0;
    uint8 excp_num     = 0;
    uint8 excp_type    = 0;
    uint16 excp_array[10] = {0};
    sys_queue_reason_t* p_reason = NULL;

    SYS_QUEUE_DBG_FUNC();


    CTC_ERROR_RETURN(sys_greatbelt_queue_get_excp_idx_by_reason(lchip, reason_id,
                                                                excp_array,
                                                                &excp_num));

    p_reason = ctc_vector_get(p_gb_queue_master[lchip]->reason_vec, reason_id);

    for (idx = 0; idx < excp_num; idx++)
    {
        exception = excp_array[idx];
        SYS_QUEUE_DBG_INFO("exception = %d, nexthop_ptr = %d \n", exception, reason_id);

        excp_type = SYS_REASON_GET_EXCP_TYPE(exception);
        excp_idx = SYS_REASON_GET_EXCP_INDEX(exception);

        if (SYS_REASON_FWD_EXCP == excp_type)
        {

            uint32 fwd_offset = 0;
            ds_fwd_t dsfwd;

            CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsfwd_offset(lchip,
                                 CTC_NH_RESERVED_NHID_FOR_TOCPU, &fwd_offset));

            sal_memset(&dsfwd, 0, sizeof(ds_fwd_t));
            dsfwd.dest_map = dest_map;
            dsfwd.next_hop_ptr = nexthop_ptr;
            dsfwd.bypass_ingress_edit = 1;
            dsfwd.next_hop_ext = dsnh_8w;
            SYS_QUEUE_DBG_INFO("Fwd reason =%d, dsFwd offset= %d, dest_map = 0x%x nexthopPtr:%d\n",
                               reason_id, fwd_offset, dsfwd.dest_map, dsfwd.next_hop_ptr);
            /*write dsfwd*/
            cmd = DRV_IOW(DsFwd_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, fwd_offset, cmd, &dsfwd));

        }
        else if (SYS_REASON_OAM_EXCP == excp_type)
        {
            uint32 field_val = 0;

            field_val = nexthop_ptr;
            cmd = DRV_IOW(DsOamExcp_t, DsOamExcp_NextHopPtr_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &field_val));

            cmd = DRV_IOR(OamHeaderEditCtl_t, OamHeaderEditCtl_CpuExceptionEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

            if ((dest_map & 0xFF) == SYS_RESERVE_PORT_ID_CPU)
            {
                CTC_BIT_SET(field_val, excp_idx);
            }
            else
            {
                CTC_BIT_UNSET(field_val, excp_idx);
            }

            cmd = DRV_IOW(OamHeaderEditCtl_t, OamHeaderEditCtl_CpuExceptionEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

            if ((dest_map & 0xFF) == SYS_RESERVE_PORT_ID_CPU)
            {
                 /*field_val = dest_map & 0xFF;*/
                field_val = SYS_RESERVE_PORT_ID_CPU_OAM & 0xFF;
                cmd = DRV_IOW(OamHeaderEditCtl_t, OamHeaderEditCtl_CpuPortId_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
            }
        }
        else if (SYS_REASON_FATAL_EXCP == excp_type)
        {
            ds_fwd_t dsfwd;
            uint32 offset = 0;

            /*get fatal exception resver offset*/
            CTC_ERROR_RETURN(sys_greatbelt_nh_get_fatal_excp_dsnh_offset(lchip, &offset));
            offset = offset + excp_idx;

            sal_memset(&dsfwd, 0, sizeof(ds_fwd_t));
            dsfwd.dest_map = dest_map;
            dsfwd.next_hop_ptr = nexthop_ptr;
            dsfwd.bypass_ingress_edit = 1;
            dsfwd.next_hop_ext = dsnh_8w;
            SYS_QUEUE_DBG_INFO("FatalExcp reason =%d, excp = %d,  offset= %d, dest_map = 0x%x \n",
                               reason_id, excp_idx, offset, dsfwd.dest_map);

            /*write dsfwd*/
            cmd = DRV_IOW(DsFwd_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset, cmd, &dsfwd));
        }
        else
        {
            ds_met_fifo_excp_t ds_met_fifo_excp;
            uint32 field_val = 0;

            sal_memset(&ds_met_fifo_excp, 0, sizeof(ds_met_fifo_excp_t));
            ds_met_fifo_excp.next_hop_ext = dsnh_8w;
            ds_met_fifo_excp.length_adjust_type = 0;
            ds_met_fifo_excp.dest_map = dest_map;

            /* write metfifo */
            cmd = DRV_IOW(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &ds_met_fifo_excp));

            cmd = DRV_IOW(DsBufRetrvExcp_t, DsBufRetrvExcp_NextHopPtr_f);
            field_val = nexthop_ptr;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &field_val));

            SYS_QUEUE_DBG_INFO("Excp dest_map = 0x%x \n",  ds_met_fifo_excp.dest_map);

        }
    }

    if (excp_num == 0 && (reason_id >= CTC_PKT_CPU_REASON_CUSTOM_BASE))  /*the cpu reason is user-defined reason*/
    {
        if (dest_type != CTC_PKT_CPU_REASON_TO_DROP)
        {
           CTC_ERROR_RETURN(sys_greatbelt_nh_update_entry_dsfwd(lchip, &p_reason->dsfwd_offset, dest_map,nexthop_ptr, dsnh_8w,0, p_reason->lport_en));
            p_reason->is_user_define = 1;

        }
        else if(p_reason->dsfwd_offset)/*free dsfwd if CTC_PKT_CPU_REASON_TO_DROP; can not support drop for user-defined cpu reason*/
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_update_entry_dsfwd(lchip, &p_reason->dsfwd_offset,dest_map,nexthop_ptr, dsnh_8w,1, 0));
            p_reason->is_user_define = 0;
            p_reason->dsfwd_offset = 0;

        }
    }

    return CTC_E_NONE;

}


int32
sys_greatbelt_cpu_reason_set_priority(uint8 lchip,
                                          uint16 reason_id,
                                          uint8 cos)
{
    uint32 cmd         = 0;
    uint8 idx          = 0;
    uint16 exception   = 0;
    uint16 excp_idx    = 0;
    uint8 excp_num     = 0;
    uint8 excp_type    = 0;
    uint16 excp_array[10] = {0};

    SYS_QUEUE_DBG_FUNC();

    CTC_ERROR_RETURN(sys_greatbelt_queue_get_excp_idx_by_reason(lchip, reason_id,
                                                                excp_array,
                                                                &excp_num));

    for (idx = 0; idx < excp_num; idx++)
    {
        exception = excp_array[idx];
        SYS_QUEUE_DBG_INFO("exception = %d, nexthop_ptr = %d \n", exception, reason_id);

        excp_type = SYS_REASON_GET_EXCP_TYPE(exception);
        excp_idx = SYS_REASON_GET_EXCP_INDEX(exception);

        if (SYS_REASON_NORMAL_EXCP == excp_type)
        {
            ds_buf_retrv_priority_ctl_t ds_buf_retrv_priority_ctl;

            sal_memset(&ds_buf_retrv_priority_ctl, 0, sizeof(ds_buf_retrv_priority_ctl_t));
            ds_buf_retrv_priority_ctl.priority = cos;
            ds_buf_retrv_priority_ctl.priority_en = 1;

            /* write metfifo */
            cmd = DRV_IOW(DsBufRetrvPriorityCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &ds_buf_retrv_priority_ctl));

            SYS_QUEUE_DBG_INFO("Excp(%d) cos = 0x%x \n",  excp_idx, ds_buf_retrv_priority_ctl.priority);
        }
        else if (SYS_REASON_OAM_EXCP == excp_type)
        {
            ds_oam_excp_t ds_oam_excp;
            cmd = DRV_IOR(DsOamExcp_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &ds_oam_excp));

            ds_oam_excp.priority = ((cos + 1)*((p_gb_queue_master[lchip]->priority_mode)? 2:8)- 1);

            cmd = DRV_IOW(DsOamExcp_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &ds_oam_excp));

        }
    }

    return CTC_E_NONE;

}

int32
sys_greatbelt_cpu_reason_get_queue_offset(uint8 lchip,
                                          uint16 reason_id,
                                          uint8* offset,
                                          uint16* sub_queue_id)
{
    uint32 cmd         = 0;
    uint8 idx          = 0;
    uint16 exception   = 0;
    uint16 excp_idx    = 0;
    uint8 excp_num     = 0;
    uint8 excp_type    = 0;
    uint16 excp_array[10] = {0};
    sys_queue_reason_t* p_reason = NULL;

    SYS_QUEUE_DBG_FUNC();

    p_reason = ctc_vector_get(p_gb_queue_master[lchip]->reason_vec, reason_id);
    *sub_queue_id = 0;

    if ((CTC_PKT_CPU_REASON_MIRRORED_TO_CPU == reason_id) || (reason_id >= CTC_PKT_CPU_REASON_CUSTOM_BASE)
        || (CTC_PKT_CPU_REASON_C2C_PKT == reason_id))
    {
        if (p_reason)
        {
            *sub_queue_id = ((p_reason->queue_info.dest_queue) & 0x1FFF);
        }
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_greatbelt_queue_get_excp_idx_by_reason(lchip, reason_id,
                                                                excp_array,
                                                                &excp_num));
    for (idx = 0; idx < excp_num; idx++)
    {
        exception = excp_array[idx];
        SYS_QUEUE_DBG_INFO("exception = %d, nexthop_ptr = %d \n", exception, reason_id);

        excp_type = SYS_REASON_GET_EXCP_TYPE(exception);
        excp_idx = SYS_REASON_GET_EXCP_INDEX(exception);

        if (SYS_REASON_NORMAL_EXCP == excp_type || SYS_REASON_FATAL_EXCP == excp_type)
        {
            *offset = 0;
            if (p_reason)
            {
                *sub_queue_id = (p_reason->queue_info.group_id - p_gb_queue_master[lchip]->reason_grp_start)*8 + p_reason->queue_info.offset;
            }
        }
        else if (SYS_REASON_OAM_EXCP == excp_type)
        {
            ds_oam_excp_t ds_oam_excp;
            cmd = DRV_IOR(DsOamExcp_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &ds_oam_excp));

            *offset = (ds_oam_excp.priority)/((p_gb_queue_master[lchip]->priority_mode)? 2:8);
            if (p_reason)
            {
                *sub_queue_id = p_reason->queue_info.queue_base + ((ds_oam_excp.priority) / ((p_gb_queue_master[lchip]->priority_mode)? 2:8));
            }
        }
        else if(SYS_REASON_FWD_EXCP == excp_type)
        {
            if (p_reason)
            {
                *sub_queue_id = p_reason->queue_info.queue_base;
            }
        }

    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_cpu_reason_update_resrc_pool(uint8 lchip, uint16 reason_id,
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
        CTC_ERROR_RETURN(sys_greatbelt_queue_set_cpu_queue_egs_pool_classify(lchip, reason_id, pool));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_cpu_reason_set_dest(uint8 lchip,
                                  uint16 reason_id,
                                  uint8 dest_type,
                                  uint32 dest_port)
{
    uint16 queue_id = 0;
    uint16 sub_queue_id = 0;
    uint32 dest_map = 0;
    uint8 gchip = 0;
    uint8 gchip0 = 0;
    uint16 lport = 0;
    uint32 nexthop_ptr = 0;
    uint8 is_channel_queue = 0;
    uint8 dsnh_8w = 0;
    sys_queue_info_t* p_queue_info = NULL;
    sys_queue_reason_t* p_reason = NULL;
    ctc_qos_queue_id_t queue;


    SYS_QUEUE_DBG_FUNC();
    SYS_QUEUE_DBG_INFO("reason_id = %d, dest_type = %d, dest_port = 0x%x\n",
                       reason_id, dest_type, dest_port);

    CTC_MAX_VALUE_CHECK(reason_id, CTC_PKT_CPU_REASON_MAX_COUNT - 1);
    CTC_MAX_VALUE_CHECK(dest_type, CTC_PKT_CPU_REASON_TO_NHID);


    if ((CTC_PKT_CPU_REASON_ARP_MISS == reason_id)
        &&sys_greatbelt_nh_get_arp_num(lchip))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    p_reason = ctc_vector_get(p_gb_queue_master[lchip]->reason_vec, reason_id);
    if (NULL == p_reason)
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((reason_id >= CTC_PKT_CPU_REASON_OAM
         && reason_id < CTC_PKT_CPU_REASON_OAM + CTC_PKT_CPU_REASON_OAM_PDU_RESV) ||
        reason_id == CTC_PKT_CPU_REASON_FWD_CPU)
    {
        is_channel_queue = 1;
    }

    p_queue_info = &p_reason->queue_info;

    if ((CTC_PKT_CPU_REASON_MIRRORED_TO_CPU != reason_id)
        &&(CTC_PKT_CPU_REASON_ARP_MISS != reason_id)
        && (CTC_PKT_CPU_REASON_C2C_PKT != reason_id))
    {
        sal_memset(&queue, 0, sizeof(queue));
        queue.queue_type = CTC_QUEUE_TYPE_EXCP_CPU;
        queue.queue_id = 0;
        queue.cpu_reason = reason_id;
        CTC_ERROR_RETURN(_sys_greatbelt_queue_get_queue_id(lchip,
                                                           &queue,
                                                           &queue_id));
    }

    sys_greatbelt_get_gchip_id(lchip, &gchip);

    nexthop_ptr = CTC_PKT_CPU_REASON_BUILD_NHPTR(reason_id, 0);

    switch (dest_type)
    {
    case CTC_PKT_CPU_REASON_TO_LOCAL_CPU:
        sys_greatbelt_get_gchip_id(lchip, &gchip0);
        if(!is_channel_queue)
        {
            sub_queue_id = (p_queue_info->group_id - p_gb_queue_master[lchip]->reason_grp_start)
                            * p_gb_queue_master[lchip]->cpu_reason_grp_que_num + p_queue_info->offset;
        }
        dest_map = is_channel_queue ? SYS_NH_ENCODE_DESTMAP(0, gchip0, SYS_RESERVE_PORT_ID_CPU) :
            SYS_REASON_ENCAP_DEST_MAP(gchip0, sub_queue_id);
        dest_port = CTC_MAP_LPORT_TO_GPORT(gchip0, SYS_RESERVE_PORT_ID_CPU);
        break;

    case CTC_PKT_CPU_REASON_TO_LOCAL_PORT:
        gchip = CTC_MAP_GPORT_TO_GCHIP(dest_port);
        lport = CTC_MAP_GPORT_TO_LPORT(dest_port);
        dest_map = SYS_NH_ENCODE_DESTMAP(0, gchip, lport);
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_resolved_offset(lchip,
                             SYS_NH_RES_OFFSET_TYPE_BYPASS_NH,
                             &nexthop_ptr));
        dsnh_8w = 1;
        break;

    case CTC_PKT_CPU_REASON_TO_REMOTE_CPU:

        if (is_channel_queue && (CTC_PKT_CPU_REASON_FWD_CPU != reason_id))
        {
            return CTC_E_NOT_SUPPORT;
        }

        gchip = CTC_MAP_GPORT_TO_GCHIP(dest_port);

        if (CTC_PKT_CPU_REASON_FWD_CPU == reason_id)
        {
            lport = CTC_MAP_GPORT_TO_LPORT(dest_port);
            dest_map = SYS_NH_ENCODE_DESTMAP(0, gchip, lport);
        }
        else
        {
            /*for stacking, channel id is unknown*/
            sub_queue_id = (p_queue_info->group_id - p_gb_queue_master[lchip]->reason_grp_start)
                            * p_gb_queue_master[lchip]->cpu_reason_grp_que_num + p_queue_info->offset;
            dest_map = SYS_REASON_ENCAP_DEST_MAP(gchip, sub_queue_id);
        }

        break;

    case CTC_PKT_CPU_REASON_TO_DROP:
         dest_map = SYS_NH_ENCODE_DESTMAP(0, gchip, SYS_RESERVE_PORT_ID_DROP);

        break;

    case CTC_PKT_CPU_REASON_TO_NHID:
        {
            uint32 nhid = 0;
            sys_nh_info_dsnh_t nhinfo;

            sal_memset(&nhinfo, 0, sizeof(sys_nh_info_dsnh_t));
            nhid = dest_port;
            sys_greatbelt_nh_get_nhinfo(lchip, nhid, &nhinfo);
            dest_map = SYS_NH_ENCODE_DESTMAP(0, nhinfo.dest_chipid, nhinfo.dest_id);
            nexthop_ptr = nhinfo.dsnh_offset;
            dsnh_8w = nhinfo.nexthop_ext;
            break;
        }

    default:
        break;
    }

    if ((CTC_PKT_CPU_REASON_MIRRORED_TO_CPU != reason_id)
        &&(CTC_PKT_CPU_REASON_ARP_MISS != reason_id)
        && (CTC_PKT_CPU_REASON_C2C_PKT != reason_id))
    {
        CTC_ERROR_RETURN(sys_greatbelt_cpu_reason_update_exception(lchip,
                                                                   reason_id,
                                                                   dest_map,
                                                                   nexthop_ptr,
                                                                   dsnh_8w,
                                                                   dest_type));
    }

    p_reason->dest_type = dest_type;
    p_reason->dest_port = dest_port;
    p_reason->dest_map = dest_map;

    SYS_QUEUE_DBG_INFO("gchip = %d, dest_map = 0x%x\n",
                       gchip, dest_map);

    if ((CTC_PKT_CPU_REASON_MIRRORED_TO_CPU != reason_id)
        &&(CTC_PKT_CPU_REASON_ARP_MISS != reason_id))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_cpu_reason_update_resrc_pool(lchip, reason_id, dest_type));
    }

    return CTC_E_NONE;
}

char*
sys_greatbelt_reason_destType2Str(uint8 lchip, uint8 dest_type)
{
    switch (dest_type)
    {
    case CTC_PKT_CPU_REASON_TO_LOCAL_CPU:
        return "LocalCPU";
        break;

    case CTC_PKT_CPU_REASON_TO_LOCAL_PORT:
        return "LocalPort";
        break;

    case CTC_PKT_CPU_REASON_TO_REMOTE_CPU:
        return "RemotePort";
        break;

    case CTC_PKT_CPU_REASON_TO_DROP:
        return "Drop";
        break;

    case CTC_PKT_CPU_REASON_TO_NHID:
        return "Nhid";
        break;

    default:
        break;
    }

    return NULL;
}

char*
sys_greatbelt_reason_2Str(uint8 lchip, uint16 reason_id)
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
        sal_sprintf(reason_desc,"OAM_EXCP(%d)",reason_id-CTC_PKT_CPU_REASON_OAM);
        return reason_desc;

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
     case CTC_PKT_CPU_REASON_C2C_PKT:              /* C2C packet */
            return "C2C_PKT";
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
     case CTC_PKT_CPU_REASON_L2_VSI_LEARN_LIMIT:
        return "VSI_LEARN_LIMIT"; /*[GB] MAC learn limit base on VSI */
     case CTC_PKT_CPU_REASON_ARP_MISS:
        return "ARP_MISS"; /*[GB] ARP lookup miss */
     default:
	 	sal_sprintf(reason_desc,"Reason(%d)",reason_id);
		return reason_desc;
    }
}

char*
sys_greatbelt_exception_type_str(uint8 lchip, uint8 exception_type)
{
    switch (exception_type)
    {
    case SYS_REASON_NORMAL_EXCP:
        return "Normal";
        break;

    case SYS_REASON_FATAL_EXCP:
        return "Fatal";
        break;

    case SYS_REASON_OAM_EXCP:
        return "OAM";
        break;

    case SYS_REASON_FWD_EXCP:
        return "Forward";
        break;

    default:
        break;
    }

    return NULL;
}

int32
sys_greatbelt_qos_cpu_reason_dump(uint8 lchip, uint16 start, uint16 end, uint8 detail)
{
    sys_queue_reason_t* p_reason = NULL;
    uint16 queue_id = 0;
    sys_queue_node_t* p_queue_node = NULL;
    uint16 reason_id = 0;

    SYS_QUEUE_INIT_CHECK(lchip);
    SYS_QUEUE_DBG_DUMP("show cpu reason information:\n");
    SYS_QUEUE_DBG_DUMP("============================================\n");
    SYS_QUEUE_DBG_DUMP("%-6s %-10s %-10s", "reason", "dest-type", "dest-port");

    if (detail)
    {
        SYS_QUEUE_DBG_DUMP("%-9s %-7s %-8s %-10s", "exception", "type", "dest-map", "nexthopptr");
        SYS_QUEUE_DBG_DUMP(" %-8s %-7s %-5s",   "queue-id", "channel", "group");
    }

    SYS_QUEUE_DBG_DUMP("\n");

    SYS_QOS_QUEUE_LOCK(lchip);
    for (reason_id = start; reason_id <= end; reason_id++)
    {
        p_reason = ctc_vector_get(p_gb_queue_master[lchip]->reason_vec, reason_id);
        if (NULL == p_reason)
        {
            continue;
        }

        SYS_QUEUE_DBG_DUMP("%-6d %-10s 0x%-8x",
                           reason_id,
                           sys_greatbelt_reason_destType2Str(lchip, p_reason->dest_type),
                           p_reason->dest_port);

        if (detail)
        {
            uint16 excp_arry[10] = {0};
            uint8  excp_num = 0;
            uint32 dest_map = 0;
            uint32 cmd      = 0;
            uint16 excp_idx = 0;
            uint8  excp_type = 0;
            uint16 exception = 0;
            uint16 nexthop_ptr = 0;
            sys_greatbelt_queue_get_excp_idx_by_reason(lchip, reason_id, excp_arry, &excp_num);
            exception = excp_arry[0];

            excp_type = SYS_REASON_GET_EXCP_TYPE(exception);
            excp_idx = SYS_REASON_GET_EXCP_INDEX(exception);

            if (SYS_REASON_FWD_EXCP == excp_type)
            {

                uint32 fwd_offset = 0;
                ds_fwd_t dsfwd;

                CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_nh_get_dsfwd_offset(lchip,
                                     CTC_NH_RESERVED_NHID_FOR_TOCPU, &fwd_offset));

                 sal_memset(&dsfwd, 0, sizeof(ds_fwd_t));

                /*write dsfwd*/
                cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, fwd_offset, cmd, &dsfwd));

                dest_map    = dsfwd.dest_map;
                nexthop_ptr = dsfwd.next_hop_ptr;

                SYS_QUEUE_DBG_INFO("Fwd reason =%d, dsFwd offset= %d, dest_map = 0x%x nexthopPtr:%d\n",
                                   reason_id, fwd_offset, dsfwd.dest_map, dsfwd.next_hop_ptr);

            }
            else if (SYS_REASON_OAM_EXCP == excp_type)
            {
                uint32 field_val = 0;

                cmd = DRV_IOR(DsOamExcp_t, DsOamExcp_NextHopPtr_f);
                CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, excp_idx, cmd, &field_val));
                nexthop_ptr = field_val;

                cmd = DRV_IOR(OamHeaderEditCtl_t, OamHeaderEditCtl_CpuPortId_f);
                CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &field_val));
                dest_map = field_val;

                cmd = DRV_IOR(OamHeaderEditCtl_t, OamHeaderEditCtl_LocalChipId_f);
                CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &field_val));
                dest_map |= (field_val << 16);

            }
            else if (SYS_REASON_FATAL_EXCP == excp_type)
            {
                ds_fwd_t dsfwd;
                uint32 fwd_offset = 0;
                /*get fatal exception resver offset*/
                CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_nh_get_fatal_excp_dsnh_offset(lchip, &fwd_offset));
                fwd_offset = fwd_offset + excp_idx;
                /*read dsfwd*/
                sal_memset(&dsfwd, 0, sizeof(ds_fwd_t));
                cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, fwd_offset, cmd, &dsfwd));
                dest_map = dsfwd.dest_map;
                nexthop_ptr = dsfwd.next_hop_ptr;
            }
            else
            {
                if(CTC_PKT_CPU_REASON_ARP_MISS == reason_id)
                {
                    dest_map    = p_reason->dest_map;
                    nexthop_ptr = CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_ARP_MISS, 0);
                }
                else
                {
                    ds_met_fifo_excp_t ds_met_fifo_excp;
                    uint32 field_val = 0;
                    sal_memset(&ds_met_fifo_excp, 0, sizeof(ds_met_fifo_excp_t));
                    /* read metfifo */
                    cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
                    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, excp_idx, cmd, &ds_met_fifo_excp));
                    dest_map = ds_met_fifo_excp.dest_map;
                    cmd = DRV_IOR(DsBufRetrvExcp_t, DsBufRetrvExcp_NextHopPtr_f);
                    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, excp_idx, cmd, &field_val));
                    nexthop_ptr = field_val;
                }

            }

            SYS_QUEUE_DBG_DUMP("%-9d %-7s 0x%04x   %-10d",
                               excp_idx,
                               sys_greatbelt_exception_type_str(lchip, excp_type),
                               dest_map,
                               nexthop_ptr);

            queue_id = p_reason->queue_info.queue_base;
            p_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);
            if (!p_queue_node)
            {
                SYS_QUEUE_DBG_DUMP("\n");
                continue;
            }

            SYS_QUEUE_DBG_DUMP(" %-8d %-7d %-5d",
                               p_queue_node->queue_id,
                               p_queue_node->channel,
                               p_queue_node->group_id);

        }

        SYS_QUEUE_DBG_DUMP("\n");

    }
    SYS_QOS_QUEUE_UNLOCK(lchip);
    SYS_QUEUE_DBG_DUMP("============================================\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_qos_metfifo_get_destmap_nh(uint8 lchip, uint32 excp_index,uint32 description_index)
{
    uint32 ret = 0;
    uint32 cmd = 0;
    uint8 gchip_id  = 0;
    uint8 lport = 0;
    uint32 dest_map = 0;
    uint32 nh_ptr = 0;
    ds_met_fifo_excp_t met_excp;
    ds_buf_retrv_excp_t bufrev_exp;
    char* description[80]={"Reserve","NULL","Ingress Port Mirror","Ingress Vlan Mirror","Ingress Acl0 Mirror","Ingress Acl1 Mirror", \
                           "Ingress Acl2 Mirror","Ingress Acl3 Mirror","Ipe Scl Entry","Ipe Protocol Vlan","Cpu Log", \
                           "Egress Port Log","Ipe Icmp Redirect","Ipe Learning Cache Full","Ipe Route Mcast Rpf Fail",\
                           "Ingress Port Log","Egress Port Mirror","Egress Vlan Mirror","Egress Acl0 Mirror", \
                           "Egress Acl1 Mirror","Egress Acl2 Mirror","Egress Acl3 Mirror","Epe VlanXlate Exception Entry", \
                           "Epe Parser Length Error","Epe TTL Check Fail","EPE Oam Hash Conflict","NULL","Epe MTU Check Fail",\
                           "EPE StripOffset error","EPE Icmp Error Msg","EFM/BPDU","Slow Protocol","EAPOL","LLDP","ISIS", \
                           "System mac limit to cpu","NULL","NULL","Reserve","Storm Control to cpu","VSI mac limit to cpu", \
                           "Bridge Mac Da PDU/VLAN mac limit", "Bridge Mac Da PDU/PORT mac limit","Bridge Mac Da PDU/SourceMisMatch", \
                           "Bridge Mac Da PDU/MacSa PDU","RIP/RIPng","LDP","OSPF","PIM","BGP","RSVP","ICMPV6/MSDP", \
                           "IGMP","VRRP","BFD","ARP","DHCP","IPDA","Reserve","Sgmac Control Msg", \
                           "Parser Length Error","Pbb Tci Nca Check Fail","PTP packet","NULL","Acl entry0 entry","NULL", \
                           "Ethernet Service OAM Check Fail","NULL","TMPLS Check Fail","Section DCN","Reserve","ACH Check Fail", \
                           "Section TP Oam Sbit Check Fail","Oam Hash Conflict","MPLS-TP OAM no MEP/MIP discard"};

    cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));
    gchip_id = met_excp.dest_map >> 16;
    lport = met_excp.dest_map & CTC_LOCAL_PORT_MASK;
    dest_map = ((gchip_id << 8) | lport);
    cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &bufrev_exp));
    nh_ptr = bufrev_exp.next_hop_ptr;
    SYS_QUEUE_DBG_DUMP("%-6d %-30s 0x%-8x 0x%-10x\n", excp_index, description[description_index], dest_map, nh_ptr);
    return ret;
}

int32
sys_greatbelt_qos_metfifo_dump(uint8 lchip)
{
    uint32 ret = 0;
    uint32 excp_index = 0;
    uint32 description_index = 0;

    SYS_LCHIP_CHECK_ACTIVE(lchip);
    SYS_QUEUE_DBG_DUMP("show cpu reason information:\n");
    SYS_QUEUE_DBG_DUMP("============================================\n");
    SYS_QUEUE_DBG_DUMP("%-6s %-30s %-10s %-10s\n", "Index", "Description", "Destmap", "NexthopPtr");
    /*mirror*/
    description_index = 2;
    for (excp_index = 0;excp_index < 24;excp_index = excp_index + 1)
    {
        description_index = description_index + (excp_index / 4);
        CTC_ERROR_RETURN(_sys_greatbelt_qos_metfifo_get_destmap_nh(lchip, excp_index, description_index));
        description_index = description_index - (excp_index / 4);
    }
    description_index = 7;
    for (excp_index = 24;excp_index < 32;excp_index = excp_index+1)
    {
        description_index = description_index + 1;
        CTC_ERROR_RETURN(_sys_greatbelt_qos_metfifo_get_destmap_nh(lchip, excp_index, description_index));
    }
    description_index = description_index + 1;
    for (excp_index = 32;excp_index < 56;excp_index = excp_index + 1)
    {
        description_index = description_index + ((excp_index-32) / 4);
        CTC_ERROR_RETURN(_sys_greatbelt_qos_metfifo_get_destmap_nh(lchip, excp_index, description_index));
        description_index = description_index - ((excp_index-32) / 4);
    }
    description_index = 21;
    for (excp_index = 56;excp_index < 69;excp_index = excp_index + 1)
    {
        description_index = description_index + 1;
        CTC_ERROR_RETURN(_sys_greatbelt_qos_metfifo_get_destmap_nh(lchip, excp_index, description_index));
    }
    for (excp_index = 69;excp_index < 118;excp_index = excp_index + 1)
    {
        description_index = 0;
        CTC_ERROR_RETURN(_sys_greatbelt_qos_metfifo_get_destmap_nh(lchip, excp_index, description_index));
    }
    description_index = 34;
    for (excp_index = 118;excp_index < 138;excp_index = excp_index + 1)
    {
        description_index = description_index + 1;
        CTC_ERROR_RETURN(_sys_greatbelt_qos_metfifo_get_destmap_nh(lchip, excp_index, description_index));
    }
    for (excp_index = 138;excp_index < 141;excp_index = excp_index + 1)
    {
        description_index = 0;
        CTC_ERROR_RETURN(_sys_greatbelt_qos_metfifo_get_destmap_nh(lchip, excp_index, description_index));
    }
    description_index = 54;
    for (excp_index = 141;excp_index < 144;excp_index = excp_index + 1)
    {
        description_index = description_index + 1;
        CTC_ERROR_RETURN(_sys_greatbelt_qos_metfifo_get_destmap_nh(lchip, excp_index, description_index));
    }
    for (excp_index = 144;excp_index < 192;excp_index = excp_index + 1)
    {
        description_index = 1;
        CTC_ERROR_RETURN(_sys_greatbelt_qos_metfifo_get_destmap_nh(lchip, excp_index, description_index));
    }
    description_index = 57;
    for (excp_index = 192;excp_index < 209;excp_index = excp_index + 1)
    {
        description_index = description_index + 1;
        CTC_ERROR_RETURN(_sys_greatbelt_qos_metfifo_get_destmap_nh(lchip, excp_index, description_index));
    }

    SYS_QUEUE_DBG_DUMP("209~255 NULL!\n");
    SYS_QUEUE_DBG_DUMP("============================================\n");

    return ret;
}

