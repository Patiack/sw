/**
 @file sys_goldengate_parser.c

 @date 2009-12-22

 @version v2.0

---file comments----
*/

#include "sal.h"
#include "ctc_error.h"
#include "ctc_vector.h"
#include "sys_goldengate_parser.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_chip.h"
#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"
#include "goldengate/include/drv_common.h"
#include "sys_goldengate_parser_io.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/

#define SYS_PAS_MAX_L4_APP_DATA_CTL_ENTRY_NUM 4

#define SYS_PAS_MAX_L3FLEX_BYTE_SEL 8

#define SYS_PAS_PARSER_LAYER2_PROTOCOL_USER_ENTRY_NUM 4
#define SYS_PAS_PARSER_LAYER3_PROTOCOL_USER_ENTRY_NUM 4

#define SYS_PAS_CAM_INDEX_CHECK(index, min, max) \
    do { \
        if (index < min || index >= max) \
        { \
            return CTC_E_INVALID_PARAM; \
        } \
    } while (0)

#define SYS_PAS_OUTER_ECMP_HASH_TYPE     SYS_PARSER_HASH_TYPE_CRC
#define SYS_PAS_OUTER_LINKAGG_HASH_TYPE  SYS_PARSER_HASH_TYPE_CRC

#define SYS_PAS_INNER_ECMP_HASH_TYPE     SYS_PARSER_HASH_TYPE_CRC
#define SYS_PAS_INNER_LINKAGG_HASH_TYPE  SYS_PARSER_HASH_TYPE_CRC


#define SYS_PAS_CHIP_IPVER_MAP_CTC_IPVER(ipver) ((1 == ipver) ?  CTC_IP_VER_4 : CTC_IP_VER_6)
#define SYS_PAS_CTC_IPVER_MAP_CHIP_IPVER(ipver) ((CTC_IP_VER_4 == ipver) ? 1 : 2)

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

/**
 @brief set tpid
*/
int32
sys_goldengate_parser_set_tpid(uint8 lchip, ctc_parser_l2_tpid_t type, uint16 tpid)
{


    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "layer2 tpid type:%d, tpid:0x%X.\n", type, tpid);

    switch (type)
    {
    case CTC_PARSER_L2_TPID_CVLAN_TPID:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_cvlan_tpid(lchip, tpid));
        break;

    case CTC_PARSER_L2_TPID_ITAG_TPID:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_i_tag_tpid(lchip, tpid));
        break;

    case CTC_PARSER_L2_TPID_BLVAN_TPID:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_bvlan_tpid(lchip, tpid));
        break;

    case CTC_PARSER_L2_TPID_SVLAN_TPID_0:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_svlan_tpid0(lchip, tpid));
        break;

    case CTC_PARSER_L2_TPID_SVLAN_TPID_1:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_svlan_tpid1(lchip, tpid));
        break;

    case CTC_PARSER_L2_TPID_SVLAN_TPID_2:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_svlan_tpid2(lchip, tpid));
        break;

    case CTC_PARSER_L2_TPID_SVLAN_TPID_3:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_svlan_tpid3(lchip, tpid));
        break;

    case CTC_PARSER_L2_TPID_CNTAG_TPID:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_cn_tag_tpid(lchip, tpid));
        break;

    case CTC_PARSER_L2_TPID_EVB_TPID:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_evb_tpid(lchip, tpid));
        break;

    default:
        return CTC_E_INVALID_PARAM;
        break;
    }

    return CTC_E_NONE;
}

/**
 @brief get tpid with some type
*/
int32
sys_goldengate_parser_get_tpid(uint8 lchip, ctc_parser_l2_tpid_t type, uint16* p_tpid)
{

    uint32 value = 0;

    CTC_PTR_VALID_CHECK(p_tpid);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "layer2 tpid type:0x%X.\n", type);

    switch (type)
    {
    case CTC_PARSER_L2_TPID_CVLAN_TPID:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_cvlan_tpid(lchip, &value));
        break;

    case CTC_PARSER_L2_TPID_ITAG_TPID:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_i_tag_tpid(lchip, &value));
        break;

    case CTC_PARSER_L2_TPID_BLVAN_TPID:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_bvlan_tpid(lchip, &value));
        break;

    case CTC_PARSER_L2_TPID_SVLAN_TPID_0:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_svlan_tpid0(lchip, &value));
        break;

    case CTC_PARSER_L2_TPID_SVLAN_TPID_1:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_svlan_tpid1(lchip, &value));
        break;

    case CTC_PARSER_L2_TPID_SVLAN_TPID_2:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_svlan_tpid2(lchip, &value));
        break;

    case CTC_PARSER_L2_TPID_SVLAN_TPID_3:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_svlan_tpid3(lchip, &value));
        break;

    case CTC_PARSER_L2_TPID_CNTAG_TPID:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_cn_tag_tpid(lchip, &value));
        break;

    case CTC_PARSER_L2_TPID_EVB_TPID:
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_evb_tpid(lchip, &value));
        break;

    default:
        return CTC_E_INVALID_PARAM;
        break;
    }

    *p_tpid = value;

    return CTC_E_NONE;
}

/**
 @brief set max_length,based on the value differentiate type or length
*/
int32
sys_goldengate_parser_set_max_length_filed(uint8 lchip, uint16 max_length)
{


    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "max_length:0x%X.\n", max_length);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_max_length(lchip, max_length));

    return CTC_E_NONE;
}

/**
 @brief get max_length value
*/
int32
sys_goldengate_parser_get_max_length_filed(uint8 lchip, uint16* p_max_length)
{

    uint32 tmp = 0;

    CTC_PTR_VALID_CHECK(p_max_length);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_max_length(lchip, &tmp));
    *p_max_length = tmp;

    return CTC_E_NONE;
}

/**
 @brief set vlan parser num
*/
int32
sys_goldengate_parser_set_vlan_parser_num(uint8 lchip, uint8 vlan_num)
{


    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "vlan_num:0x%X.\n", vlan_num);

    if (vlan_num > 2)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_vlan_parsing_num(lchip, vlan_num));

    return CTC_E_NONE;
}

/**
 @brief get vlan parser num
*/
int32
sys_goldengate_parser_get_vlan_parser_num(uint8 lchip, uint8* p_vlan_num)
{

    uint32 tmp = 0;

    CTC_PTR_VALID_CHECK(p_vlan_num);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "vlan_num:0x%X.\n", *p_vlan_num);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_vlan_parsing_num(lchip, &tmp));
    *p_vlan_num = tmp;

    return CTC_E_NONE;
}

/**
 @brief set pbb header info
*/
int32
sys_goldengate_parser_set_pbb_header(uint8 lchip, ctc_parser_pbb_header_t* p_pbb_header)
{

    uint32 value = 0;
    ParserPbbCtl_m ctl;

    CTC_PTR_VALID_CHECK(p_pbb_header);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "nca_value:0x%X.\n", p_pbb_header->nca_value_en);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "outer_vlan_is_cvlan:0x%X.\n", p_pbb_header->outer_vlan_is_cvlan);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "pbb_vlan_parsing_num:0x%X.\n", p_pbb_header->vlan_parsing_num);

    if (p_pbb_header->vlan_parsing_num > 3)
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&ctl, 0, sizeof(ParserPbbCtl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_pbb_ctl(lchip, &ctl));

    value = p_pbb_header->nca_value_en ? 1 : 0;
    SetParserPbbCtl(V, ncaValue_f, &ctl, value);
    value = p_pbb_header->outer_vlan_is_cvlan ? 1 : 0;
    SetParserPbbCtl(V, pbbOuterVlanIsCvlan_f, &ctl, value);
    value = p_pbb_header->vlan_parsing_num;
    SetParserPbbCtl(V, pbbVlanParsingNum_f, &ctl, value);
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_pbb_ctl(lchip, &ctl));

    return CTC_E_NONE;
}

/**
 @brief get pbb header info
*/
int32
sys_goldengate_parser_get_pbb_header(uint8 lchip, ctc_parser_pbb_header_t* p_pbb_header)
{

    uint32 value = 0;
    ParserPbbCtl_m ctl;

    CTC_PTR_VALID_CHECK(p_pbb_header);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&ctl, 0, sizeof(ParserPbbCtl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_pbb_ctl(lchip, &ctl));

    GetParserPbbCtl(A, ncaValue_f, &ctl, &value);
    p_pbb_header->nca_value_en = value;

    GetParserPbbCtl(A, pbbOuterVlanIsCvlan_f, &ctl, &value);
    p_pbb_header->outer_vlan_is_cvlan = value;

    GetParserPbbCtl(A, pbbVlanParsingNum_f, &ctl, &value);
    p_pbb_header->vlan_parsing_num = value;

    return CTC_E_NONE;
}

/**
 @brief mapping layer3 type
*/
int32
sys_goldengate_parser_mapping_l3_type(uint8 lchip, uint8 index, ctc_parser_l2_protocol_entry_t* p_entry)
{

    uint8  isEth = 0;
    uint8  isSAP = 0;
    uint32 value = 0;
    ParserLayer2ProtocolCam_m cam;
    ParserLayer2ProtocolCamValid_m valid;

    CTC_PTR_VALID_CHECK(p_entry);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "index:0x%X.\n", index);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "l3_type:0x%X.\n", p_entry->l3_type);

#if 0
    SYS_PAS_CAM_INDEX_CHECK(entry->l2_type, CTC_PARSER_L2_TYPE_RSV_USER_DEFINE0,
                            MAX_CTC_PARSER_L2_TYPE);
#endif

    /*only l3_type:13,14,15 can be configed by user
      and ethertype 0x8035 can be configed 6 */
    if (!((CTC_PARSER_L3_TYPE_RSV_USER_DEFINE0 == p_entry->l3_type)
        || (CTC_PARSER_L3_TYPE_RSV_USER_DEFINE1 == p_entry->l3_type)
        || (CTC_PARSER_L3_TYPE_RSV_USER_FLEXL3  == p_entry->l3_type)
        || ((CTC_PARSER_L3_TYPE_ARP == p_entry->l3_type) && (0x8035 == p_entry->l2_header_protocol))))
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((p_entry->addition_offset > 0xf) || (p_entry->l2_type > 0xf))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (index > (SYS_PAS_PARSER_LAYER2_PROTOCOL_USER_ENTRY_NUM - 1))
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&cam, 0, sizeof(ParserLayer2ProtocolCam_m));
    sal_memset(&valid, 0, sizeof(ParserLayer2ProtocolCamValid_m));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer2_protocol_cam(lchip, &cam));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer2_protocol_cam_valid(lchip, &valid));

    switch (p_entry->l2_type)
    {
    case CTC_PARSER_L2_TYPE_NONE:
        isEth = 0;
        isSAP = 0;
        break;

    case CTC_PARSER_L2_TYPE_ETH_V2:
    case CTC_PARSER_L2_TYPE_ETH_SNAP:
        isEth = 1;
        isSAP = 0;
        break;

    case CTC_PARSER_L2_TYPE_PPP_2B:
    case CTC_PARSER_L2_TYPE_PPP_1B:
        return CTC_E_FEATURE_NOT_SUPPORT;

    case CTC_PARSER_L2_TYPE_ETH_SAP:
        isEth = 1;
        isSAP = 1;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    switch (index)
    {
    case 0:
        value = p_entry->addition_offset;
        SetParserLayer2ProtocolCam(V, array_0_l2CamAdditionalOffset_f,
                            &cam, value);
        value = p_entry->l3_type;
        SetParserLayer2ProtocolCam(V, array_0_l2CamLayer3Type_f,
                            &cam, value);
        value = p_entry->mask & 0xfffff;
        SetParserLayer2ProtocolCam(V, array_0_l2CamMask_f,
                            &cam, value);

        value = (isEth << 19) | (isSAP << 18)
                | (p_entry->l2_type << 16) | (p_entry->l2_header_protocol);
        SetParserLayer2ProtocolCam(V, array_0_l2CamValue_f,
                            &cam, value);
        break;

    case 1:
        value = p_entry->addition_offset;
        SetParserLayer2ProtocolCam(V, array_1_l2CamAdditionalOffset_f,
                            &cam, value);
        value = p_entry->l3_type;
        SetParserLayer2ProtocolCam(V, array_1_l2CamLayer3Type_f,
                            &cam, value);
        value = p_entry->mask & 0xfffff;
        SetParserLayer2ProtocolCam(V, array_1_l2CamMask_f,
                            &cam, value);
        value = (isEth << 19) | (isSAP << 18)
                | (p_entry->l2_type << 16) | (p_entry->l2_header_protocol);
        SetParserLayer2ProtocolCam(V, array_1_l2CamValue_f,
                            &cam, value);
        break;

    case 2:
        value = p_entry->addition_offset;
        SetParserLayer2ProtocolCam(V, array_2_l2CamAdditionalOffset_f,
                            &cam, value);
        value = p_entry->l3_type;
        SetParserLayer2ProtocolCam(V, array_2_l2CamLayer3Type_f,
                            &cam, value);
        value = p_entry->mask & 0xfffff;
        SetParserLayer2ProtocolCam(V, array_2_l2CamMask_f,
                            &cam, value);
        value = (isEth << 19) | (isSAP << 18)
                | (p_entry->l2_type << 16) | (p_entry->l2_header_protocol);
        SetParserLayer2ProtocolCam(V, array_2_l2CamValue_f,
                            &cam, value);
        break;

    case 3:
        value = p_entry->addition_offset;
        SetParserLayer2ProtocolCam(V, array_3_l2CamAdditionalOffset_f,
                            &cam, value);
        value = p_entry->l3_type;
        SetParserLayer2ProtocolCam(V, array_3_l2CamLayer3Type_f,
                            &cam, value);
        value = p_entry->mask & 0xfffff;
        SetParserLayer2ProtocolCam(V, array_3_l2CamMask_f,
                            &cam, value);
        value = (isEth << 19) | (isSAP << 18)
                | (p_entry->l2_type << 16) | (p_entry->l2_header_protocol);
        SetParserLayer2ProtocolCam(V, array_3_l2CamValue_f,
                            &cam, value);

        break;

    default:
        return CTC_E_INVALID_PARAM;
        break;
    }

    GetParserLayer2ProtocolCamValid(A, layer2CamEntryValid_f,
                  &valid, &value);

    value = ((~((uint32)1 << index)) & value) |  (1 << index);
    SetParserLayer2ProtocolCamValid(V, layer2CamEntryValid_f,\
                        &valid, value);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer2_protocol_cam(lchip, &cam));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer2_protocol_cam_valid(lchip, &valid));

    return CTC_E_NONE;
}

/**
 @brief set the entry invalid based on the index
*/
int32
sys_goldengate_parser_unmapping_l3_type(uint8 lchip, uint8 index)
{

    uint32 value = 0;
    ParserLayer2ProtocolCamValid_m valid;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "index:0x%X.\n", index);

    if (index > (SYS_PAS_PARSER_LAYER2_PROTOCOL_USER_ENTRY_NUM - 1))
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&valid, 0, sizeof(ParserLayer2ProtocolCamValid_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer2_protocol_cam_valid(lchip, &valid));

    GetParserLayer2ProtocolCamValid(A, layer2CamEntryValid_f, &valid, &value);

    if (CTC_IS_BIT_SET(value, index))
    {
        value &= (~(1 << index));
        SetParserLayer2ProtocolCamValid(V, layer2CamEntryValid_f,\
                            &valid, value);
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer2_protocol_cam_valid(lchip, &valid));
    }

    return CTC_E_NONE;
}

/**
 @brief enable or disable parser layer3 type
*/
int32
sys_goldengate_parser_enable_l3_type(uint8 lchip, ctc_parser_l3_type_t l3_type, bool enable)
{

    uint32 type_en = 0;
    uint32 value = 0;
    uint32 l2_cam_layer3_type0 = 0;
    uint32 l2_cam_layer3_type1 = 0;
    uint32 l2_cam_layer3_type2 = 0;
    uint32 l2_cam_layer3_type3 = 0;
    ParserEthernetCtl_m ctl;
    ParserLayer2ProtocolCam_m cam;
    ParserLayer2ProtocolCamValid_m valid;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "l3_type:0x%X.\n", l3_type);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "enable:0x%X.\n", enable);

    if ((l3_type > CTC_PARSER_L3_TYPE_RSV_USER_FLEXL3) || (l3_type < CTC_PARSER_L3_TYPE_IPV4))
    {
        return CTC_E_PARSER_INVALID_L3_TYPE;
    }

    type_en = ((TRUE == enable) ? 1 : 0);

    if (l3_type < CTC_PARSER_L3_TYPE_RSV_USER_FLEXL3)
    {
        sal_memset(&ctl, 0, sizeof(ParserEthernetCtl_m));
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_ethernet_ctl(lchip, &ctl));

        switch (l3_type)
        {
        case CTC_PARSER_L3_TYPE_IPV4:
            SetParserEthernetCtl(V, ipv4TypeEn_f, &ctl, type_en);
            break;

        case CTC_PARSER_L3_TYPE_IPV6:
            SetParserEthernetCtl(V, ipv6TypeEn_f, &ctl, type_en);
            break;

        case CTC_PARSER_L3_TYPE_MPLS:
            SetParserEthernetCtl(V, mplsTypeEn_f, &ctl, type_en);
            break;

        case CTC_PARSER_L3_TYPE_MPLS_MCAST:
            SetParserEthernetCtl(V, mplsMcastTypeEn_f, &ctl, type_en);
            break;

        case CTC_PARSER_L3_TYPE_ARP:
            SetParserEthernetCtl(V, arpTypeEn_f, &ctl, type_en);
            break;

        case CTC_PARSER_L3_TYPE_FCOE:
            SetParserEthernetCtl(V, fcoeTypeEn_f, &ctl, type_en);
            break;

        case CTC_PARSER_L3_TYPE_TRILL:
            SetParserEthernetCtl(V, trillTypeEn_f, &ctl, type_en);
            break;

        case CTC_PARSER_L3_TYPE_ETHER_OAM:
            SetParserEthernetCtl(V, ethOamTypeEn_f, &ctl, type_en);
            break;

        case CTC_PARSER_L3_TYPE_SLOW_PROTO:
            SetParserEthernetCtl(V, slowProtocolTypeEn_f, &ctl, type_en);
            break;

        case CTC_PARSER_L3_TYPE_CMAC:
            SetParserEthernetCtl(V, pbbTypeEn_f, &ctl, type_en);
            break;

        case CTC_PARSER_L3_TYPE_PTP:
            SetParserEthernetCtl(V, ieee1588TypeEn_f, &ctl, type_en);
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }

        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_ethernet_ctl(lchip, &ctl));
    }
    else
    {
        sal_memset(&cam, 0, sizeof(ParserLayer2ProtocolCam_m));
        sal_memset(&valid, 0, sizeof(ParserLayer2ProtocolCamValid_m));

        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer2_protocol_cam(lchip, &cam));
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer2_protocol_cam_valid(lchip, &valid));

        GetParserLayer2ProtocolCamValid(A, layer2CamEntryValid_f,\
                      &valid, &value);

        l2_cam_layer3_type0 = GetParserLayer2ProtocolCam(A, array_0_l2CamLayer3Type_f,\
                                            &cam, &value);
        l2_cam_layer3_type1 = GetParserLayer2ProtocolCam(A, array_1_l2CamLayer3Type_f,\
                                            &cam, &value);
        l2_cam_layer3_type2 = GetParserLayer2ProtocolCam(A, array_2_l2CamLayer3Type_f,\
                                            &cam, &value);
        l2_cam_layer3_type3 = GetParserLayer2ProtocolCam(A, array_3_l2CamLayer3Type_f,\
                                            &cam, &value);

        if (type_en)
        {
            if (l3_type == l2_cam_layer3_type0)
            {
                value |= (1 << 0);
                SetParserLayer2ProtocolCamValid(V, layer2CamEntryValid_f,\
                                    &valid, value);
            }
            else if (l3_type == l2_cam_layer3_type1)
            {
                value |= (1 << 1);
                SetParserLayer2ProtocolCamValid(V, layer2CamEntryValid_f,\
                                    &valid, value);
            }
            else if (l3_type == l2_cam_layer3_type2)
            {
                value |= (1 << 2);
                SetParserLayer2ProtocolCamValid(V, layer2CamEntryValid_f,\
                                    &valid, value);
            }
            else if (l3_type == l2_cam_layer3_type3)
            {
                value |= (1 << 3);
                SetParserLayer2ProtocolCamValid(V, layer2CamEntryValid_f,\
                                    &valid, value);
            }
            else
            {
                return CTC_E_PARSER_INVALID_L3_TYPE;
            }
        }
        else
        {
            if (l3_type == l2_cam_layer3_type0)
            {
                value &= (~(1 << 0));
                SetParserLayer2ProtocolCamValid(V, layer2CamEntryValid_f,\
                                    &valid, value);
            }
            else if (l3_type == l2_cam_layer3_type1)
            {
                value &= (~(1 << 1));
                SetParserLayer2ProtocolCamValid(V, layer2CamEntryValid_f,\
                                    &valid, value);
            }
            else if (l3_type == l2_cam_layer3_type2)
            {
                value &= (~(1 << 2));
                SetParserLayer2ProtocolCamValid(V, layer2CamEntryValid_f,\
                                    &valid, value);
            }
            else if (l3_type == l2_cam_layer3_type3)
            {
                value &= (~(1 << 3));
                SetParserLayer2ProtocolCamValid(V, layer2CamEntryValid_f,\
                                    &valid, value);
            }
            else
            {
                return CTC_E_PARSER_INVALID_L3_TYPE;
            }
        }
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer2_protocol_cam_valid(lchip, &valid));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_parser_hash_table_step(uint32 id, sys_parser_hash_usage_t hash_usage)
{
    if (IpeUserIdHashCtl_t == id)
    {
        return -((IpeUserIdHashCtl_gMask_0_portHashEn_f - IpeUserIdHashCtl_gMask_1_portHashEn_f) * hash_usage);
    }
    else if (IpeAclGenHashKeyCtl_t == id)
    {
        return -((IpeAclGenHashKeyCtl_gMask_0_l2IsEtherEn_f - IpeAclGenHashKeyCtl_gMask_1_l2IsEtherEn_f) * hash_usage);
    }
    else
    {
        return 0;
    }
}

/**
 @brief set layer2 hash info
*/
STATIC int32
_sys_goldengate_parser_set_layer2_hash(uint8 lchip, ctc_parser_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{

    int32  step1 = 0;
    int32  step2 = 0;
    uint32 value1 = 0;
    uint32 value2 = 0;
    IpeUserIdHashCtl_m outer_ctl;
    IpeAclGenHashKeyCtl_m inner_ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "hash_type_bitmap:0x%X.\n", p_hash_ctl->hash_type_bitmap);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "l2_flag:0x%X.\n", p_hash_ctl->l2_flag);

    step1 = _sys_goldengate_parser_hash_table_step(IpeUserIdHashCtl_t, hash_usage);
    step2 = _sys_goldengate_parser_hash_table_step(IpeAclGenHashKeyCtl_t, hash_usage);

    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));
    sal_memset(&inner_ctl, 0, sizeof(IpeAclGenHashKeyCtl_m));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_inner_hash_ctl(lchip, &inner_ctl));

    value1 = CTC_FLAG_ISSET(p_hash_ctl->l2_flag, CTC_PARSER_L2_HASH_FLAGS_STAG_VID) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_sVlanIdHashEn_f + step1, &outer_ctl, value1);

    value2 = CTC_FLAG_ISSET(p_hash_ctl->l2_flag, CTC_PARSER_L2_HASH_FLAGS_CTAG_VID) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_cVlanIdHashEn_f + step1, &outer_ctl, value2);

    DRV_SET_FIELD_V(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_vlanIdHashEn_f + step2,
                        &inner_ctl, (value1 || value2));

    value1 = CTC_FLAG_ISSET(p_hash_ctl->l2_flag, CTC_PARSER_L2_HASH_FLAGS_STAG_CFI) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_sCfiHashEn_f + step1, &outer_ctl, value1);

    value2 = CTC_FLAG_ISSET(p_hash_ctl->l2_flag, CTC_PARSER_L2_HASH_FLAGS_CTAG_CFI) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_cCfiHashEn_f + step1, &outer_ctl, value2);

    DRV_SET_FIELD_V(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_cfiHashEn_f + step2,
                        &inner_ctl, (value1 || value2));

    value1 = CTC_FLAG_ISSET(p_hash_ctl->l2_flag, CTC_PARSER_L2_HASH_FLAGS_STAG_COS) ? 1 : 0;
    value1 |= CTC_FLAG_ISSET(p_hash_ctl->l2_flag, CTC_PARSER_L2_HASH_FLAGS_COS) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_sCosHashEn_f + step1, &outer_ctl, value1);

    value2 = CTC_FLAG_ISSET(p_hash_ctl->l2_flag, CTC_PARSER_L2_HASH_FLAGS_CTAG_COS) ? 1 : 0;
    value2 |= CTC_FLAG_ISSET(p_hash_ctl->l2_flag, CTC_PARSER_L2_HASH_FLAGS_COS) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_cCosHashEn_f + step1, &outer_ctl, value2);

    DRV_SET_FIELD_V(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_cosHashEn_f + step2,
                        &inner_ctl, (value1 || value2));

    value1 = CTC_FLAG_ISSET(p_hash_ctl->l2_flag, CTC_PARSER_L2_HASH_FLAGS_ETHERTYPE) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l2ProtocolHashEn_f + step1, &outer_ctl, value1);
    DRV_SET_FIELD_V(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_etherTypeHashEn_f + step2, &inner_ctl, value1);

    value1 = CTC_FLAG_ISSET(p_hash_ctl->l2_flag, CTC_PARSER_L2_HASH_FLAGS_MACSA) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_macSaHashEn_f + step1, &outer_ctl, value1);
    DRV_SET_FIELD_V(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_macSaHashEn_f + step2, &inner_ctl, value1);

    value1 = CTC_FLAG_ISSET(p_hash_ctl->l2_flag, CTC_PARSER_L2_HASH_FLAGS_MACDA) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_macDaHashEn_f + step1, &outer_ctl, value1);
    DRV_SET_FIELD_V(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_macDaHashEn_f + step2, &inner_ctl, value1);

    value1 = CTC_FLAG_ISSET(p_hash_ctl->l2_flag, CTC_PARSER_L2_HASH_FLAGS_PORT) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_portHashEn_f + step1, &outer_ctl, value1);
    DRV_SET_FIELD_V(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_portHashEn_f + step2, &inner_ctl, value1);

    if (p_hash_ctl->hash_type_bitmap & CTC_PARSER_HASH_TYPE_FLAGS_INNER)
    {
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_inner_hash_ctl(lchip, &inner_ctl));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_outer_hash_ctl(lchip, &outer_ctl));
    }

    return CTC_E_NONE;
}

/**
 @brief get layer2 hash info
*/
STATIC int32
_sys_goldengate_parser_get_layer2_hash(uint8 lchip, ctc_parser_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{

    int32  step = 0;
    uint32 value = 0;
    IpeUserIdHashCtl_m outer_ctl;
    IpeAclGenHashKeyCtl_m inner_ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "hash_type_bitmap:0x%X.\n", p_hash_ctl->hash_type_bitmap);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "ip_flag:0x%X.\n", p_hash_ctl->ip_flag);

    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));
    sal_memset(&inner_ctl, 0, sizeof(IpeAclGenHashKeyCtl_m));

    if (p_hash_ctl->hash_type_bitmap & CTC_PARSER_HASH_TYPE_FLAGS_INNER)
    {
        step = _sys_goldengate_parser_hash_table_step(IpeAclGenHashKeyCtl_t, hash_usage);
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_inner_hash_ctl(lchip, &inner_ctl));

        DRV_GET_FIELD_A(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_vlanIdHashEn_f + step, &inner_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_STAG_VID : 0));

        DRV_GET_FIELD_A(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_cfiHashEn_f + step, &inner_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_STAG_CFI : 0));

        DRV_GET_FIELD_A(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_cosHashEn_f + step, &inner_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_STAG_COS : 0));

        DRV_GET_FIELD_A(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_etherTypeHashEn_f + step, &inner_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_ETHERTYPE : 0));

        DRV_GET_FIELD_A(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_macSaHashEn_f + step, &inner_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_MACSA : 0));

        DRV_GET_FIELD_A(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_macDaHashEn_f + step, &inner_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_MACDA : 0));

        DRV_GET_FIELD_A(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_portHashEn_f + step, &inner_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_PORT : 0));
    }
    else
    {
        step = _sys_goldengate_parser_hash_table_step(IpeUserIdHashCtl_t, hash_usage);
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_sVlanIdHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_STAG_VID : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_cVlanIdHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_CTAG_VID : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_sCfiHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_STAG_CFI : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_cCfiHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_CTAG_CFI : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_sCosHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_STAG_COS : 0));
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_COS : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_cCosHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_CTAG_COS : 0));
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_COS : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l2ProtocolHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_ETHERTYPE : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_macSaHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_MACSA : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_macDaHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_MACDA : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_portHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_PORT : 0));
    }

    return CTC_E_NONE;
}

/**
 @brief set ip hash info
*/
STATIC int32
_sys_goldengate_parser_set_ip_hash(uint8 lchip, ctc_parser_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{

    uint32 value = 0;
    int32  step1 = 0;
    int32  step2 = 0;
    IpeUserIdHashCtl_m outer_ctl;
    IpeAclGenHashKeyCtl_m inner_ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "hash_type_bitmap:0x%X.\n", p_hash_ctl->hash_type_bitmap);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "ip_flag:0x%X.\n", p_hash_ctl->ip_flag);

    step1 = _sys_goldengate_parser_hash_table_step(IpeUserIdHashCtl_t, hash_usage);
    step2 = _sys_goldengate_parser_hash_table_step(IpeAclGenHashKeyCtl_t, hash_usage);

    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));
    sal_memset(&inner_ctl, 0, sizeof(IpeAclGenHashKeyCtl_m));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_inner_hash_ctl(lchip, &inner_ctl));

    value = CTC_FLAG_ISSET(p_hash_ctl->ip_flag, CTC_PARSER_IP_HASH_FLAGS_PROTOCOL) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_ipProtocolHashEn_f + step1, &outer_ctl, value);
    DRV_SET_FIELD_V(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_ipProtocolHashEn_f + step2, &inner_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->ip_flag, CTC_PARSER_IP_HASH_FLAGS_DSCP) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_ipDscpHashEn_f + step1, &outer_ctl, value);
    DRV_SET_FIELD_V(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_ipDscpHashEn_f + step2, &inner_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->ip_flag, CTC_PARSER_IP_HASH_FLAGS_IPV6_FLOW_LABEL) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_ipFlowLabelHashEn_f + step1, &outer_ctl, value);
    DRV_SET_FIELD_V(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_ipFlowLabelHashEn_f + step2, &inner_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->ip_flag, CTC_PARSER_IP_HASH_FLAGS_IPSA) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_ipSaHashEn_f + step1, &outer_ctl, value);
    DRV_SET_FIELD_V(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_ipSaHashEn_f + step2, &inner_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->ip_flag, CTC_PARSER_IP_HASH_FLAGS_IPDA) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_ipDaHashEn_f + step1, &outer_ctl, value);
    DRV_SET_FIELD_V(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_ipDaHashEn_f + step2, &inner_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->ip_flag, CTC_PARSER_IP_HASH_FLAGS_ECN) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_ipEcnHashEn_f + step1, &outer_ctl, value);
    DRV_SET_FIELD_V(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_ipEcnHashEn_f + step2, &inner_ctl, value);


    if (p_hash_ctl->hash_type_bitmap & CTC_PARSER_HASH_TYPE_FLAGS_INNER)
    {
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_inner_hash_ctl(lchip, &inner_ctl));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_outer_hash_ctl(lchip, &outer_ctl));
    }

    return CTC_E_NONE;
}

/**
 @brief set ip ecmp hash info
*/
STATIC int32
_sys_goldengate_parser_get_ip_hash(uint8 lchip, ctc_parser_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{

    int32  step = 0;
    uint32 value = 0;
    IpeUserIdHashCtl_m outer_ctl;
    IpeAclGenHashKeyCtl_m inner_ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "hash_type_bitmap:0x%X.\n", p_hash_ctl->hash_type_bitmap);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "ip_flag:0x%X.\n", p_hash_ctl->ip_flag);

    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));
    sal_memset(&inner_ctl, 0, sizeof(IpeAclGenHashKeyCtl_m));

    if (p_hash_ctl->hash_type_bitmap & CTC_PARSER_HASH_TYPE_FLAGS_INNER)
    {
        step = _sys_goldengate_parser_hash_table_step(IpeAclGenHashKeyCtl_t, hash_usage);
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_inner_hash_ctl(lchip, &inner_ctl));

        DRV_GET_FIELD_A(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_ipProtocolHashEn_f + step, &inner_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->ip_flag, value ? CTC_PARSER_IP_HASH_FLAGS_PROTOCOL : 0);

        DRV_GET_FIELD_A(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_ipDscpHashEn_f + step, &inner_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->ip_flag, value ? CTC_PARSER_IP_HASH_FLAGS_DSCP : 0);

        DRV_GET_FIELD_A(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_ipFlowLabelHashEn_f + step, &inner_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->ip_flag, value ? CTC_PARSER_IP_HASH_FLAGS_IPV6_FLOW_LABEL : 0);

        DRV_GET_FIELD_A(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_ipSaHashEn_f + step, &inner_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->ip_flag, value ? CTC_PARSER_IP_HASH_FLAGS_IPSA : 0);

        DRV_GET_FIELD_A(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_ipDaHashEn_f + step, &inner_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->ip_flag, value ? CTC_PARSER_IP_HASH_FLAGS_IPDA : 0);

        DRV_GET_FIELD_A(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_ipEcnHashEn_f + step, &inner_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->ip_flag, value ? CTC_PARSER_IP_HASH_FLAGS_ECN : 0);

    }
    else
    {
        step = _sys_goldengate_parser_hash_table_step(IpeUserIdHashCtl_t, hash_usage);
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_ipProtocolHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->ip_flag, value ? CTC_PARSER_IP_HASH_FLAGS_PROTOCOL : 0);

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_ipDscpHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->ip_flag, value ? CTC_PARSER_IP_HASH_FLAGS_DSCP : 0);

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_ipFlowLabelHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->ip_flag, value ? CTC_PARSER_IP_HASH_FLAGS_IPV6_FLOW_LABEL : 0);

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_ipSaHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->ip_flag, value ? CTC_PARSER_IP_HASH_FLAGS_IPSA : 0);

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_ipDaHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->ip_flag, value ? CTC_PARSER_IP_HASH_FLAGS_IPDA : 0);

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_ipEcnHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->ip_flag, value ? CTC_PARSER_IP_HASH_FLAGS_ECN : 0);

    }

    return CTC_E_NONE;
}

/**
 @brief set parser mpls hash info
*/
STATIC int32
_sys_goldengate_parser_set_mpls_hash(uint8 lchip, ctc_parser_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{

    int32  step = 0;
    uint32 value = 0;
    uint8  mpls_ecmp_inner_hash_en = 0;
    ParserMplsCtl_m mpls_ctl;
    IpeUserIdHashCtl_m outer_ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "hash_type_bitmap:0x%X.\n", p_hash_ctl->mpls_flag);

    step = _sys_goldengate_parser_hash_table_step(IpeUserIdHashCtl_t, hash_usage);

    sal_memset(&mpls_ctl, 0, sizeof(ParserMplsCtl_m));
    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_mpls_ctl(lchip, &mpls_ctl));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));
    value = CTC_FLAG_ISSET(p_hash_ctl->mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_CANCEL_LABEL) ? 0:0xFF;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_mplsLabelHashEn_f + step, &outer_ctl, value);

    mpls_ecmp_inner_hash_en = (CTC_FLAG_ISSET(p_hash_ctl->mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_PROTOCOL) ? 1 : 0)
                              || (CTC_FLAG_ISSET(p_hash_ctl->mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_DSCP) ? 1 : 0)
                              || (CTC_FLAG_ISSET(p_hash_ctl->mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_IPV6_FLOW_LABEL) ? 1 : 0)
                              || (CTC_FLAG_ISSET(p_hash_ctl->mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_IPSA) ? 1 : 0)
                              || (CTC_FLAG_ISSET(p_hash_ctl->mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_IPDA) ? 1 : 0);

    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_mplsInnerHdrHashValueEn_f + step, &outer_ctl, mpls_ecmp_inner_hash_en);

    value = CTC_FLAG_ISSET(p_hash_ctl->mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_PROTOCOL) ? 1 : 0;
    SetParserMplsCtl(V, mplsIpProtocolHashEn_f, &mpls_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_DSCP) ? 1 : 0;
    SetParserMplsCtl(V, mplsIpDscpHashEn_f, &mpls_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_IPV6_FLOW_LABEL) ? 1 : 0;
    SetParserMplsCtl(V, mplsIpFlowLabelHashEn_f, &mpls_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_IPSA) ? 1 : 0;
    SetParserMplsCtl(V, mplsIpSaHashEn_f, &mpls_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_IPDA) ? 1 : 0;
    SetParserMplsCtl(V, mplsIpDaHashEn_f, &mpls_ctl, value);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_mpls_ctl(lchip, &mpls_ctl));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_outer_hash_ctl(lchip, &outer_ctl));

    return CTC_E_NONE;
}

/**
 @brief get parser mpls hash info
*/
STATIC int32
_sys_goldengate_parser_get_mpls_hash(uint8 lchip, ctc_parser_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{

    uint32 value = 0;
    ParserMplsCtl_m mpls_ctl;
    IpeUserIdHashCtl_m outer_ctl;
    int32  step = 0;

    sal_memset(&mpls_ctl, 0, sizeof(ParserMplsCtl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_mpls_ctl(lchip, &mpls_ctl));

    GetParserMplsCtl(A, mplsIpProtocolHashEn_f, &mpls_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->mpls_flag, (value ? CTC_PARSER_MPLS_ECMP_HASH_FLAGS_PROTOCOL : 0));

    GetParserMplsCtl(A, mplsIpDscpHashEn_f, &mpls_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->mpls_flag, (value ? CTC_PARSER_MPLS_ECMP_HASH_FLAGS_DSCP : 0));

    GetParserMplsCtl(A, mplsIpFlowLabelHashEn_f, &mpls_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->mpls_flag, (value ? CTC_PARSER_MPLS_ECMP_HASH_FLAGS_IPV6_FLOW_LABEL : 0));

    GetParserMplsCtl(A, mplsIpSaHashEn_f, &mpls_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->mpls_flag, (value ? CTC_PARSER_MPLS_ECMP_HASH_FLAGS_IPSA : 0));

    GetParserMplsCtl(A, mplsIpDaHashEn_f, &mpls_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->mpls_flag, (value ? CTC_PARSER_MPLS_ECMP_HASH_FLAGS_IPDA : 0));

    step = _sys_goldengate_parser_hash_table_step(IpeUserIdHashCtl_t, hash_usage);
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));
    if(!GetIpeUserIdHashCtl(V,gMask_0_mplsLabelHashEn_f + step, &outer_ctl))
    {
        CTC_SET_FLAG(p_hash_ctl->mpls_flag, CTC_PARSER_MPLS_ECMP_HASH_FLAGS_CANCEL_LABEL);
    }

    return CTC_E_NONE;
}

/**
 @brief set parser pbb hash info
*/
STATIC int32
_sys_goldengate_parser_set_pbb_hash(uint8 lchip, ctc_parser_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{

    int32  step = 0;
    uint32 value = 0;
    IpeUserIdHashCtl_m outer_ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "hash_type_bitmap:0x%X.\n", p_hash_ctl->hash_type_bitmap);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "pbb_flag:0x%X.\n", p_hash_ctl->pbb_flag);

    step = _sys_goldengate_parser_hash_table_step(IpeUserIdHashCtl_t, hash_usage);

    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));

    value = CTC_FLAG_ISSET(p_hash_ctl->pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_CMAC_DA) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbDaHashEn_f + step, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_CMAC_SA) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbSaHashEn_f + step, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_ISID) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbIsidHashEn_f  + step, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_STAG_VID) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbSvlanIdHashEn_f + step, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_CTAG_VID) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbCvlanIdHashEn_f + step, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_STAG_COS) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbScosHashEn_f + step, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_CTAG_COS) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbCcosHashEn_f + step, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_STAG_CFI) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbScfiHashEn_f + step, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_CTAG_CFI) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbCcfiHashEn_f + step, &outer_ctl, value);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_outer_hash_ctl(lchip, &outer_ctl));

    return CTC_E_NONE;
}

/**
 @brief set parser pbb hash info
*/
STATIC int32
_sys_goldengate_parser_get_pbb_hash(uint8 lchip, ctc_parser_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{

    int32  step = 0;
    uint32 value = 0;
    IpeUserIdHashCtl_m outer_ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "hash_type_bitmap:0x%X.\n", p_hash_ctl->hash_type_bitmap);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "pbb_flag:0x%X.\n", p_hash_ctl->pbb_flag);

    step = _sys_goldengate_parser_hash_table_step(IpeUserIdHashCtl_t, hash_usage);

    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));

    DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbDaHashEn_f + step, &outer_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->pbb_flag, value ? CTC_PARSER_PBB_HASH_FLAGS_CMAC_DA : 0);

    DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbSaHashEn_f + step, &outer_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->pbb_flag, value ? CTC_PARSER_PBB_HASH_FLAGS_CMAC_SA : 0);

    DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbIsidHashEn_f + step, &outer_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->pbb_flag, value ? CTC_PARSER_PBB_HASH_FLAGS_ISID : 0);

    DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbSvlanIdHashEn_f + step, &outer_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->pbb_flag, value ? CTC_PARSER_PBB_HASH_FLAGS_STAG_VID : 0);

    DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbCvlanIdHashEn_f + step, &outer_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->pbb_flag, value ? CTC_PARSER_PBB_HASH_FLAGS_CTAG_VID : 0);

    DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbScosHashEn_f + step, &outer_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->pbb_flag, value ? CTC_PARSER_PBB_HASH_FLAGS_STAG_COS : 0);

    DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbCcosHashEn_f + step, &outer_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->pbb_flag, value ? CTC_PARSER_PBB_HASH_FLAGS_CTAG_COS : 0);

    DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbScfiHashEn_f + step, &outer_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->pbb_flag, value ? CTC_PARSER_PBB_HASH_FLAGS_STAG_CFI : 0);

    DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_pbbCcfiHashEn_f + step, &outer_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->pbb_flag, value ? CTC_PARSER_PBB_HASH_FLAGS_CTAG_CFI : 0);

    return CTC_E_NONE;
}

/**
 @brief set parser fcoe hash info
*/
STATIC int32
_sys_goldengate_parser_set_fcoe_hash(uint8 lchip, ctc_parser_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{

    int32  step = 0;
    uint32 value = 0;
    IpeUserIdHashCtl_m outer_ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "hash_type_bitmap:0x%X.\n", p_hash_ctl->hash_type_bitmap);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "fcoe_flag:0x%X.\n", p_hash_ctl->fcoe_flag);

    step = _sys_goldengate_parser_hash_table_step(IpeUserIdHashCtl_t, hash_usage);

    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));

    value = CTC_FLAG_ISSET(p_hash_ctl->fcoe_flag, CTC_PARSER_FCOE_HASH_FLAGS_SID) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_fcoeSidHashEn_f + step, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->fcoe_flag, CTC_PARSER_FCOE_HASH_FLAGS_DID) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_fcoeDidHashEn_f + step, &outer_ctl, value);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_outer_hash_ctl(lchip, &outer_ctl));

    return CTC_E_NONE;
}


/**
 @brief get parser fcoe hash info
*/
STATIC int32
_sys_goldengate_parser_get_fcoe_hash(uint8 lchip, ctc_parser_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{

    int32  step = 0;
    uint32 value = 0;
    IpeUserIdHashCtl_m outer_ctl;

    step = _sys_goldengate_parser_hash_table_step(IpeUserIdHashCtl_t, hash_usage);
    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));

    DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_fcoeSidHashEn_f + step, &outer_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->fcoe_flag, (value ? CTC_PARSER_FCOE_HASH_FLAGS_SID : 0));

    DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_fcoeDidHashEn_f + step, &outer_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->fcoe_flag, (value ? CTC_PARSER_FCOE_HASH_FLAGS_DID : 0));

    return CTC_E_NONE;
}

/**
 @brief set parser trill hash info
*/
STATIC int32
_sys_goldengate_parser_set_trill_hash(uint8 lchip, ctc_parser_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{

    int32  step = 0;
    uint32 value = 0;
    IpeUserIdHashCtl_m outer_ctl;

    step = _sys_goldengate_parser_hash_table_step(IpeUserIdHashCtl_t, hash_usage);

    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));

    value = CTC_FLAG_ISSET(p_hash_ctl->trill_flag, CTC_PARSER_TRILL_HASH_FLAGS_INNER_VLAN_ID) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_trillVlanIdHashEn_f + step, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->trill_flag, CTC_PARSER_TRILL_HASH_FLAGS_INGRESS_NICKNAME) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_trillSaHashEn_f + step, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->trill_flag, CTC_PARSER_TRILL_HASH_FLAGS_EGRESS_NICKNAME) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_trillDaHashEn_f + step, &outer_ctl, value);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_outer_hash_ctl(lchip, &outer_ctl));

    return CTC_E_NONE;
}

/**
 @brief get parser trill hash info
*/
STATIC int32
_sys_goldengate_parser_get_trill_hash(uint8 lchip, ctc_parser_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{

    int32  step = 0;
    uint32 value = 0;
    IpeUserIdHashCtl_m outer_ctl;

    step = _sys_goldengate_parser_hash_table_step(IpeUserIdHashCtl_t, hash_usage);

    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));

    DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_trillVlanIdHashEn_f + step, &outer_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->trill_flag, (value ? CTC_PARSER_TRILL_ECMP_HASH_FLAGS_INNER_VLAN_ID : 0));

    DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_trillSaHashEn_f + step, &outer_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->trill_flag, (value ? CTC_PARSER_TRILL_ECMP_HASH_FLAGS_INGRESS_NICKNAME : 0));

    DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_trillDaHashEn_f + step, &outer_ctl, &value);
    CTC_SET_FLAG(p_hash_ctl->trill_flag, (value ? CTC_PARSER_TRILL_ECMP_HASH_FLAGS_EGRESS_NICKNAME : 0));

    return CTC_E_NONE;
}

/**
 @brief set parser layer4 header hash info
*/
STATIC int32
_sys_goldengate_parser_set_l4_hash(uint8 lchip, ctc_parser_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{

    int32  step1 = 0;
    int32  step2 = 0;
    uint32 value = 0;
    IpeUserIdHashCtl_m outer_ctl;
    IpeAclGenHashKeyCtl_m inner_ctl;

    step1 = _sys_goldengate_parser_hash_table_step(IpeUserIdHashCtl_t, hash_usage);
    step2 = _sys_goldengate_parser_hash_table_step(IpeAclGenHashKeyCtl_t, hash_usage);

    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));
    sal_memset(&inner_ctl, 0, sizeof(IpeAclGenHashKeyCtl_m));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_inner_hash_ctl(lchip, &inner_ctl));

    value = CTC_FLAG_ISSET(p_hash_ctl->l4_flag, CTC_PARSER_L4_HASH_FLAGS_SRC_PORT) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4SourcePortHashEn_f + step1, &outer_ctl, value);
    DRV_SET_FIELD_V(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_l4SourcePortHashEn_f + step2, &inner_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->l4_flag, CTC_PARSER_L4_HASH_FLAGS_DST_PORT) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4DestPortHashEn_f + step1, &outer_ctl, value);
    DRV_SET_FIELD_V(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_l4DestPortHashEn_f + step2, &inner_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->l4_flag, CTC_PARSER_L4_HASH_FLAGS_GRE_KEY) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4GreKeyHashEn_f + step1, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->l4_flag, CTC_PARSER_L4_HASH_FLAGS_L4_TYPE) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_layer4TypeEn_f + step1, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->l4_flag, CTC_PARSER_L4_HASH_FLAGS_L4_USER_TYPE) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_layer4UserTypeEn_f + step1, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->l4_flag, CTC_PARSER_L4_HASH_FLAGS_VXLAN_L4_SRC_PORT) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_vxlanL4SourcePortHashEn_f + step1, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->l4_flag, CTC_PARSER_L4_HASH_FLAGS_L4_TCP_FLAG) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4TcpFlagHashEn_f + step1, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->l4_flag, CTC_PARSER_L4_HASH_FLAGS_L4_TCP_ECN) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4TcpEcnHashEn_f + step1, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->l4_flag, CTC_PARSER_L4_HASH_FLAGS_L4_NVGRE_VSID) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4NvGreVsidHashEn_f + step1, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->l4_flag, CTC_PARSER_L4_HASH_FLAGS_L4_NVGRE_FLOW_ID) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4NvGreFlowIdHashEn_f + step1, &outer_ctl, value);

    value = CTC_FLAG_ISSET(p_hash_ctl->l4_flag, CTC_PARSER_L4_HASH_FLAGS_L4_VXLAN_VNI) ? 1 : 0;
    DRV_SET_FIELD_V(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4VxlanVniHashEn_f + step1, &outer_ctl, value);

    if (p_hash_ctl->hash_type_bitmap & CTC_PARSER_HASH_TYPE_FLAGS_INNER)
    {
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_inner_hash_ctl(lchip, &inner_ctl));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_outer_hash_ctl(lchip, &outer_ctl));
    }

    return CTC_E_NONE;
}

/**
 @brief get parser layer4 header hash info
*/
STATIC int32
_sys_goldengate_parser_get_l4_hash(uint8 lchip, ctc_parser_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{

    int32  step = 0;
    uint32 value = 0;
    IpeUserIdHashCtl_m outer_ctl;
    IpeAclGenHashKeyCtl_m inner_ctl;

    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));
    sal_memset(&inner_ctl, 0, sizeof(IpeAclGenHashKeyCtl_m));

    if (p_hash_ctl->hash_type_bitmap & CTC_PARSER_HASH_TYPE_FLAGS_INNER)
    {
        step = _sys_goldengate_parser_hash_table_step(IpeAclGenHashKeyCtl_t, hash_usage);
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_inner_hash_ctl(lchip, &inner_ctl));

        DRV_GET_FIELD_A(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_l4SourcePortHashEn_f + step, &inner_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_SRC_PORT : 0));

        DRV_GET_FIELD_A(IpeAclGenHashKeyCtl_t, IpeAclGenHashKeyCtl_gMask_0_l4DestPortHashEn_f + step, &inner_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_DST_PORT : 0));
    }
    else
    {
        step = _sys_goldengate_parser_hash_table_step(IpeUserIdHashCtl_t, hash_usage);
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4SourcePortHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_SRC_PORT : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4DestPortHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_DST_PORT : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4GreKeyHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_GRE_KEY : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_layer4TypeEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_L4_TYPE : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_layer4UserTypeEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_L4_USER_TYPE : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_vxlanL4SourcePortHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_VXLAN_L4_SRC_PORT : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4TcpFlagHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_L4_TCP_FLAG : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4TcpEcnHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_L4_TCP_ECN : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4NvGreVsidHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_L4_NVGRE_VSID : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4NvGreFlowIdHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_L4_NVGRE_FLOW_ID : 0));

        DRV_GET_FIELD_A(IpeUserIdHashCtl_t, IpeUserIdHashCtl_gMask_0_l4VxlanVniHashEn_f + step, &outer_ctl, &value);
        CTC_SET_FLAG(p_hash_ctl->l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_L4_VXLAN_VNI : 0));
    }

    return CTC_E_NONE;
}

/**
 @brief set parser trill header info
*/
int32
sys_goldengate_parser_set_trill_header(uint8 lchip, ctc_parser_trill_header_t* p_trill_header)
{

    ParserTrillCtl_m ctl;

    CTC_PTR_VALID_CHECK(p_trill_header);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "inner_tpid:0x%X.\n", p_trill_header->inner_tpid);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "rbridge_channel_ether_type:0x%X.\n",\
                                              p_trill_header->rbridge_channel_ether_type);

    sal_memset(&ctl, 0, sizeof(ParserTrillCtl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_trill_ctl(lchip, &ctl));

    SetParserTrillCtl(V, trillInnerTpid_f, &ctl, p_trill_header->inner_tpid);
    SetParserTrillCtl(V, rBridgeChannelEtherType_f,\
                        &ctl, p_trill_header->rbridge_channel_ether_type);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_trill_ctl(lchip, &ctl));

    return CTC_E_NONE;
}

/**
 @brief get parser trill header info
*/
int32
sys_goldengate_parser_get_trill_header(uint8 lchip, ctc_parser_trill_header_t* p_trill_header)
{

    uint32 value = 0;
    ParserTrillCtl_m ctl;

    CTC_PTR_VALID_CHECK(p_trill_header);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&ctl, 0, sizeof(ParserTrillCtl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_trill_ctl(lchip, &ctl));

    GetParserTrillCtl(A, trillInnerTpid_f, &ctl, &value);
    p_trill_header->inner_tpid = value;
    GetParserTrillCtl(A, rBridgeChannelEtherType_f, &ctl, &value);
    p_trill_header->rbridge_channel_ether_type = value;

    return CTC_E_NONE;
}

/**
 @brief mapping l4type,can add a new l3type,addition offset for the type,can get the layer4 type
*/
int32
sys_goldengate_parser_mapping_l4_type(uint8 lchip, uint8 index, ctc_parser_l3_protocol_entry_t* p_entry)
{

    uint32 value = 0;
    ParserLayer3ProtocolCam_m cam;
    ParserLayer3ProtocolCamValid_m valid;

    CTC_PTR_VALID_CHECK(p_entry);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "index:0x%X.\n", index);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "l3_header_protocol:0x%X.\n", p_entry->l3_header_protocol);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "l3_type:0x%X.\n", p_entry->l3_type);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "l4_type:0x%X.\n", p_entry->l4_type);

#if 0
    SYS_PAS_CAM_INDEX_CHECK(entry->l3_type, CTC_PARSER_L3_TYPE_RSV_USER_DEFINE0,
                            MAX_CTC_PARSER_L3_TYPE);

    SYS_PAS_CAM_INDEX_CHECK(entry->l4_type, CTC_PARSER_L4_TYPE_RSV_USER_DEFINE0,
                            MAX_CTC_PARSER_L4_TYPE);
#endif

    if ((p_entry->addition_offset > 0xF)
       || (p_entry->l3_type > 0xF) || (p_entry->l3_type_mask > 0xF)
       || (p_entry->l4_type > 0xF))
    {
        return CTC_E_INVALID_PARAM;
    }

#if 0
    if ((entry->l4_type <= CTC_PARSER_L4_TYPE_IGMP)
        || (entry->l4_type == CTC_PARSER_L4_TYPE_ANY_PROTO))
    {
        return CTC_E_INVALID_PARAM;
    }
#endif

    if (index > (SYS_PAS_PARSER_LAYER3_PROTOCOL_USER_ENTRY_NUM - 1))
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&cam, 0, sizeof(ParserLayer3ProtocolCam_m));
    sal_memset(&valid, 0, sizeof(ParserLayer3ProtocolCamValid_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer3_protocol_cam(lchip, &cam));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer3_protocol_cam_valid(lchip, &valid));

    switch (index)
    {
    case 0:
        SetParserLayer3ProtocolCam(V, array_0_l3CamAdditionalOffset_f,\
                            &cam, p_entry->addition_offset);
        SetParserLayer3ProtocolCam(V, array_0_l3CamLayer3HeaderProtocol_f,\
                           &cam, p_entry->l3_header_protocol);
        SetParserLayer3ProtocolCam(V, array_0_l3CamLayer3HeaderProtocolMask_f,\
                            &cam, (p_entry->l3_header_protocol_mask & 0xff));
        SetParserLayer3ProtocolCam(V, array_0_l3CamLayer3Type_f,\
                            &cam, p_entry->l3_type);
        SetParserLayer3ProtocolCam(V, array_0_l3CamLayer3TypeMask_f,\
                            &cam, (p_entry->l3_type_mask & 0xf));
        SetParserLayer3ProtocolCam(V, array_0_l3CamLayer4Type_f,\
                            &cam, (p_entry->l4_type));
        break;

    case 1:
        SetParserLayer3ProtocolCam(V, array_1_l3CamAdditionalOffset_f,\
                            &cam, p_entry->addition_offset);
        SetParserLayer3ProtocolCam(V, array_1_l3CamLayer3HeaderProtocol_f,\
                            &cam, p_entry->l3_header_protocol);
        SetParserLayer3ProtocolCam(V, array_1_l3CamLayer3HeaderProtocolMask_f,\
                            &cam, (p_entry->l3_header_protocol_mask & 0xff));
        SetParserLayer3ProtocolCam(V, array_1_l3CamLayer3Type_f,\
                            &cam, p_entry->l3_type);
        SetParserLayer3ProtocolCam(V, array_1_l3CamLayer3TypeMask_f,\
                            &cam, (p_entry->l3_type_mask & 0xf));
        SetParserLayer3ProtocolCam(V, array_1_l3CamLayer4Type_f,\
                            &cam, (p_entry->l4_type));
        break;

    case 2:
        SetParserLayer3ProtocolCam(V, array_2_l3CamAdditionalOffset_f,\
                            &cam, p_entry->addition_offset);
        SetParserLayer3ProtocolCam(V, array_2_l3CamLayer3HeaderProtocol_f,\
                            &cam, p_entry->l3_header_protocol);
        SetParserLayer3ProtocolCam(V, array_2_l3CamLayer3HeaderProtocolMask_f,\
                            &cam, (p_entry->l3_header_protocol_mask & 0xff));
        SetParserLayer3ProtocolCam(V, array_2_l3CamLayer3Type_f,\
                            &cam, p_entry->l3_type);
        SetParserLayer3ProtocolCam(V, array_2_l3CamLayer3TypeMask_f,\
                            &cam, (p_entry->l3_type_mask & 0xf));
        SetParserLayer3ProtocolCam(V, array_2_l3CamLayer4Type_f,\
                            &cam, (p_entry->l4_type));
        break;

    case 3:
        SetParserLayer3ProtocolCam(V, array_3_l3CamAdditionalOffset_f,\
                            &cam, p_entry->addition_offset);
        SetParserLayer3ProtocolCam(V, array_3_l3CamLayer3HeaderProtocol_f,\
                            &cam, p_entry->l3_header_protocol);
        SetParserLayer3ProtocolCam(V, array_3_l3CamLayer3HeaderProtocolMask_f,\
                            &cam, (p_entry->l3_header_protocol_mask & 0xff));
        SetParserLayer3ProtocolCam(V, array_3_l3CamLayer3Type_f,\
                            &cam, p_entry->l3_type);
        SetParserLayer3ProtocolCam(V, array_3_l3CamLayer3TypeMask_f,\
                            &cam, (p_entry->l3_type_mask));
        SetParserLayer3ProtocolCam(V, array_3_l3CamLayer4Type_f,\
                            &cam, (p_entry->l4_type));
        break;

    default:
        return CTC_E_INVALID_PARAM;
        break;
    }

    GetParserLayer3ProtocolCamValid(A, layer3CamEntryValid_f, &valid, &value);
    value |= (1 << index);
    SetParserLayer3ProtocolCamValid(V, layer3CamEntryValid_f,\
                        &valid, value);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer3_protocol_cam(lchip, &cam));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer3_protocol_cam_valid(lchip, &valid));

    return CTC_E_NONE;
}

/**
 @brief set the entry invalid based on the index
*/
int32
sys_goldengate_parser_unmapping_l4_type(uint8 lchip, uint8 index)
{

    uint32 value = 0;
    ParserLayer3ProtocolCamValid_m valid;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "index:0x%X.\n", index);

    if (index > 3)
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&valid, 0, sizeof(ParserLayer3ProtocolCamValid_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer3_protocol_cam_valid(lchip, &valid));

    GetParserLayer3ProtocolCamValid(A, layer3CamEntryValid_f, &valid, &value);

    if (CTC_IS_BIT_SET(value, index))
    {
        value &= ~(1 << index);
        SetParserLayer3ProtocolCamValid(V, layer3CamEntryValid_f,\
                            &valid, value);
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer3_protocol_cam_valid(lchip, &valid));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_parser_set_layer4_flag_op_ctl(uint8 lchip, uint8 index, sys_parser_l4flag_op_ctl_t* p_l4flag_op_ctl)
{
    uint32 value = 0;
    ParserLayer4FlagOpCtl_m ctl;

    CTC_PTR_VALID_CHECK(p_l4flag_op_ctl);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "index:0x%X.\n", index);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "op_and_or:0x%X.\n", p_l4flag_op_ctl->op_and_or);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "flags_mask:0x%X.\n", p_l4flag_op_ctl->flags_mask);

    sal_memset(&ctl, 0, sizeof(ParserLayer4FlagOpCtl_m));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer4_flag_op_ctl(lchip, &ctl));

    switch (index)
    {
    case 0:
        value = p_l4flag_op_ctl->op_and_or ? 1 : 0;
        SetParserLayer4FlagOpCtl(V, opAndOr0_f, &ctl, value);
        value = p_l4flag_op_ctl->flags_mask & 0x3f;
        SetParserLayer4FlagOpCtl(V, flagsMask0_f, &ctl, value);
        break;

    case 1:
        value = p_l4flag_op_ctl->op_and_or ? 1 : 0;
        SetParserLayer4FlagOpCtl(V, opAndOr1_f, &ctl, value);
        value = p_l4flag_op_ctl->flags_mask & 0x3f;
        SetParserLayer4FlagOpCtl(V, flagsMask1_f, &ctl, value);
        break;

    case 2:
        value = p_l4flag_op_ctl->op_and_or ? 1 : 0;
        SetParserLayer4FlagOpCtl(V, opAndOr2_f, &ctl, value);
        value = p_l4flag_op_ctl->flags_mask & 0x3f;
        SetParserLayer4FlagOpCtl(V, flagsMask2_f, &ctl, value);
        break;

    case 3:
        value = p_l4flag_op_ctl->op_and_or ? 1 : 0;
        SetParserLayer4FlagOpCtl(V, opAndOr3_f, &ctl, value);
        value = p_l4flag_op_ctl->flags_mask & 0x3f;
        SetParserLayer4FlagOpCtl(V, flagsMask3_f, &ctl, value);
        break;

    default:
        return CTC_E_INVALID_PARAM;
        break;
    }

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_flag_op_ctl(lchip, &ctl));

    return CTC_E_NONE;
}

int32
sys_goldengate_parser_set_layer4_port_op_sel(uint8 lchip, sys_parser_l4_port_op_sel_t* p_l4port_op_sel)
{
    ParserLayer4PortOpSel_m ctl;

    CTC_PTR_VALID_CHECK(p_l4port_op_sel);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "op_dest_port:0x%X.\n", p_l4port_op_sel->op_dest_port);

    sal_memset(&ctl, 0, sizeof(ParserLayer4PortOpSel_m));
    SetParserLayer4PortOpSel(V, opDestPort_f,\
                        &ctl, p_l4port_op_sel->op_dest_port);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_port_op_sel(lchip, &ctl));

    return CTC_E_NONE;
}

int32
sys_goldengate_parser_set_layer4_port_op_ctl(uint8 lchip, uint8 index, sys_parser_l4_port_op_ctl_t* p_l4port_op_ctl)
{
    ParserLayer4PortOpCtl_m ctl;

    CTC_PTR_VALID_CHECK(p_l4port_op_ctl);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "index:0x%X.\n", index);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "layer4_port_max:0x%X.\n", p_l4port_op_ctl->layer4_port_max);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "layer4_port_min:0x%X.\n", p_l4port_op_ctl->layer4_port_min);

    sal_memset(&ctl, 0, sizeof(ParserLayer4PortOpCtl_m));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer4_port_op_ctl(lchip, &ctl));

    switch (index)
    {
    case 0:
        SetParserLayer4PortOpCtl(V, portMax0_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_max);
        SetParserLayer4PortOpCtl(V, portMin0_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_min);
        break;

    case 1:
        SetParserLayer4PortOpCtl(V, portMax1_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_max);
        SetParserLayer4PortOpCtl(V, portMin1_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_min);
        break;

    case 2:
        SetParserLayer4PortOpCtl(V, portMax2_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_max);
        SetParserLayer4PortOpCtl(V, portMin2_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_min);
        break;

    case 3:
        SetParserLayer4PortOpCtl(V, portMax3_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_max);
        SetParserLayer4PortOpCtl(V, portMin3_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_min);
        break;

    case 4:
        SetParserLayer4PortOpCtl(V, portMax4_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_max);
        SetParserLayer4PortOpCtl(V, portMin4_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_min);
        break;

    case 5:
        SetParserLayer4PortOpCtl(V, portMax5_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_max);
        SetParserLayer4PortOpCtl(V, portMin5_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_min);
        break;

    case 6:
        SetParserLayer4PortOpCtl(V, portMax6_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_max);
        SetParserLayer4PortOpCtl(V, portMin6_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_min);
        break;

    case 7:
        SetParserLayer4PortOpCtl(V, portMax7_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_max);
        SetParserLayer4PortOpCtl(V, portMin7_f,\
                            &ctl, p_l4port_op_ctl->layer4_port_min);
        break;

    default:
        return CTC_E_INVALID_PARAM;
        break;
    }

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_port_op_ctl(lchip, &ctl));

    return CTC_E_NONE;
}

/**
 @brief set parser l4flex header info
*/
int32
sys_goldengate_parser_set_l4flex_header(uint8 lchip, ctc_parser_l4flex_header_t* p_l4flex_header)
{

    ParserLayer4FlexCtl_m ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "dest_port_basic_offset:%d\n", p_l4flex_header->dest_port_basic_offset);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "l4_min_len:%d\n", p_l4flex_header->l4_min_len);

    CTC_PTR_VALID_CHECK(p_l4flex_header);

    if ((p_l4flex_header->dest_port_basic_offset > 30)
       || (p_l4flex_header->l4_min_len > 0x1f))
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&ctl, 0, sizeof(ParserLayer4FlexCtl_m));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer4_flex_ctl(lchip, &ctl));

    SetParserLayer4FlexCtl(V, layer4ByteSelect0_f,\
                        &ctl, p_l4flex_header->dest_port_basic_offset);

    SetParserLayer4FlexCtl(V, layer4ByteSelect1_f,\
                        &ctl, p_l4flex_header->dest_port_basic_offset + 1);

    SetParserLayer4FlexCtl(V, layer4MinLength_f,\
                        &ctl, p_l4flex_header->l4_min_len);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_flex_ctl(lchip, &ctl));

    return CTC_E_NONE;
}

/**
 @brief get parser l4flex header info
*/
int32
sys_goldengate_parser_get_l4flex_header(uint8 lchip, ctc_parser_l4flex_header_t* p_l4flex_header)
{

    uint32 value = 0;
    ParserLayer4FlexCtl_m ctl;

    CTC_PTR_VALID_CHECK(p_l4flex_header);

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "dest_port_basic_offset:0x%X.\n", p_l4flex_header->dest_port_basic_offset);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "l4_min_len:0x%X.\n", p_l4flex_header->l4_min_len);

    sal_memset(&ctl, 0, sizeof(ParserLayer4FlexCtl_m));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer4_flex_ctl(lchip, &ctl));

    GetParserLayer4FlexCtl(A, layer4ByteSelect0_f, &ctl, &value);
    p_l4flex_header->dest_port_basic_offset = value;

    GetParserLayer4FlexCtl(A, layer4MinLength_f, &ctl, &value);
    p_l4flex_header->l4_min_len = value;

    return CTC_E_NONE;
}

/**
 @brief set parser layer4 app control info
*/
int32
sys_goldengate_parser_set_l4app_ctl(uint8 lchip, ctc_parser_l4app_ctl_t* p_l4app_ctl)
{

    ParserLayer4AppCtl_m ctl;

    CTC_PTR_VALID_CHECK(p_l4app_ctl);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "ptp_en:0x%x.\n", p_l4app_ctl->ptp_en);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "ntp_en:0x%X.\n", p_l4app_ctl->ntp_en);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "bfd_en:0x%X.\n", p_l4app_ctl->bfd_en);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "vxlan_en:0x%X.\n", p_l4app_ctl->vxlan_en);

    sal_memset(&ctl, 0, sizeof(ParserLayer4AppCtl_m));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer4_app_ctl(lchip, &ctl));

    SetParserLayer4AppCtl(V, ptpEn_f, &ctl, (p_l4app_ctl->ptp_en ? 1 : 0));
    SetParserLayer4AppCtl(V, ntpEn_f, &ctl, (p_l4app_ctl->ntp_en ? 1 : 0));
    SetParserLayer4AppCtl(V, bfdEn_f, &ctl, (p_l4app_ctl->bfd_en ? 1 : 0));
    SetParserLayer4AppCtl(V, vxlanEn_f, &ctl, (p_l4app_ctl->vxlan_en ? 1 : 0));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_app_ctl(lchip, &ctl));

    return CTC_E_NONE;
}

/**
 @brief get parser layer4 app control info
*/
int32
sys_goldengate_parser_get_l4app_ctl(uint8 lchip, ctc_parser_l4app_ctl_t* p_l4app_ctl)
{
    uint32 value = 0;
    ParserLayer4AppCtl_m ctl;

    CTC_PTR_VALID_CHECK(p_l4app_ctl);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "ptp_en:0x%X.\n", p_l4app_ctl->ptp_en);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "ntp_en:0x%X.\n", p_l4app_ctl->ntp_en);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "bfd_en:0x%X.\n", p_l4app_ctl->bfd_en);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "vxlan_en:0x%X.\n", p_l4app_ctl->vxlan_en);

    sal_memset(&ctl, 0, sizeof(ParserLayer4AppCtl_m));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer4_app_ctl(lchip, &ctl));

    GetParserLayer4AppCtl(A, ptpEn_f, &ctl, &value);
    p_l4app_ctl->ptp_en = value;

    GetParserLayer4AppCtl(A, ntpEn_f, &ctl, &value);
    p_l4app_ctl->ntp_en = value;

    GetParserLayer4AppCtl(A, bfdEn_f, &ctl, &value);
    p_l4app_ctl->bfd_en = value;

    GetParserLayer4AppCtl(A, vxlanEn_f, &ctl, &value);
    p_l4app_ctl->vxlan_en = value;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_sort_udf_offset(uint8 lchip, uint8* p_offset, uint8* p_num)
{
    int32 i = 0, j = 0;
    uint8 tmp = 0;

    for (i = 0; i < *p_num - 1; i++)
    {
        for (j = i + 1; j < *p_num; j++)
        {
            if (p_offset[i] > p_offset[j])
            {
                tmp = p_offset[i];
                p_offset[i] = p_offset[j];
                p_offset[j] = tmp;
            }
        }
    }

    for (i = 1; i < *p_num - 1; i++)
    {
        if (p_offset[i - 1] == p_offset[i])
        {
            sal_memcpy(&p_offset[i], &p_offset[i + 1], (3 - i));
            (*p_num)--;
        }
    }

    return CTC_E_NONE;
}

STATIC uint8
_sys_goldengate_parser_udf_offset_num(uint8 lchip, uint8* p_offset)
{
    uint8 i = 0, sum = 1;
    uint8 offset = p_offset[0];

    for (i = 1; i < 4; i++)
    {
        if (p_offset[i] > offset)
        {
            offset = p_offset[i];
            sum++;
        }
    }

    return sum;
}

STATIC void
_sys_goldengate_parser_mapping_udf_l4field(uint8 lchip, ctc_parser_udf_t* p_udf, uint8* p_sys_field)
{
    CTC_BIT_SET(*p_sys_field, SYS_PARSER_LAYER4_UDF_FIELD_LAYER4_TYPE);
    CTC_BIT_SET(*p_sys_field, SYS_PARSER_LAYER4_UDF_FIELD_IP_VERSION);

    if (p_udf->is_l4_src_port)
    {
        CTC_BIT_SET(*p_sys_field, SYS_PARSER_LAYER4_UDF_FIELD_SRC_PORT);
    }
    else
    {
        CTC_BIT_UNSET(*p_sys_field, SYS_PARSER_LAYER4_UDF_FIELD_SRC_PORT);
    }

    if (p_udf->is_l4_dst_port)
    {
        CTC_BIT_SET(*p_sys_field, SYS_PARSER_LAYER4_UDF_FIELD_DEST_PORT);
    }
    else
    {
        CTC_BIT_UNSET(*p_sys_field, SYS_PARSER_LAYER4_UDF_FIELD_DEST_PORT);
    }

    /*First or no fragment packet.*/
    CTC_BIT_UNSET(*p_sys_field, SYS_PARSER_LAYER4_UDF_FIELD_FRAGINFO_0);
    CTC_BIT_SET(*p_sys_field, SYS_PARSER_LAYER4_UDF_FIELD_FRAGINFO_1);

    return;
}

STATIC int32
_sys_goldengate_parser_check_udf(uint8 lchip, ctc_parser_udf_t* p_udf, uint8* p_hit, int8* p_hit_idx, uint8* p_free, int8* p_free_idx)
{
    int8  step = 0;
    uint8 field1 = 0, field2 = 0;
    uint8 i = 0, valid = 0, ip_version = 0;
    uint16 ether_type = 0, layer4_type = 0, l4_src_port = 0, l4_dst_port = 0;
    ParserLayer3UdfCam_m layer3_udf_cam;
    ParserLayer4UdfCam_m layer4_udf_cam;

    if (CTC_PARSER_UDF_TYPE_L3_UDF == p_udf->type)
    {
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer3_udf_cam(lchip, &layer3_udf_cam));
        for (i = 0; i < 4; i++)
        {
            step = ParserLayer3UdfCam_array_1_l3UdfEntryValid_f - ParserLayer3UdfCam_array_0_l3UdfEntryValid_f;
            ether_type = DRV_GET_FIELD_V(ParserLayer3UdfCam_t,\
                                         ParserLayer3UdfCam_array_0_layer3HeaderProtocol_f + step * i, \
                                         &layer3_udf_cam);
            valid = DRV_GET_FIELD_V(ParserLayer3UdfCam_t,\
                                    ParserLayer3UdfCam_array_0_l3UdfEntryValid_f + step * i, \
                                    &layer3_udf_cam);

            if (valid && (ether_type == p_udf->ether_type))
            {
                *p_hit = 1;
                *p_hit_idx = i;
                break;
            }

            if ((0 == *p_free) && (!valid))
            {
                *p_free = 1;
                *p_free_idx = i;
            }
        }
    }
    else if (CTC_PARSER_UDF_TYPE_L4_UDF == p_udf->type)
    {
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer4_udf_cam(lchip, &layer4_udf_cam));

        for (i = 0; i < 4; i++)
        {
            step = ParserLayer4UdfCam_array_1_l4UdfEntryValid_f - ParserLayer4UdfCam_array_0_l4UdfEntryValid_f;

            valid = DRV_GET_FIELD_V(ParserLayer4UdfCam_t,\
                                    ParserLayer4UdfCam_array_0_l4UdfEntryValid_f + step * i, \
                                    &layer4_udf_cam);

            field1 = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, ParserLayer4UdfCam_array_0_fieldSel_f + step * i, \
                                    &layer4_udf_cam);

            _sys_goldengate_parser_mapping_udf_l4field(lchip, p_udf, &field2);

            layer4_type = DRV_GET_FIELD_V(ParserLayer4UdfCam_t,
                                          ParserLayer4UdfCam_array_0_layer4Type_f + step * i, \
                                          &layer4_udf_cam);

            l4_src_port = DRV_GET_FIELD_V(ParserLayer4UdfCam_t,
                                          ParserLayer4UdfCam_array_0_layer4SourcePort_f + step * i, \
                                          &layer4_udf_cam);

            l4_dst_port = DRV_GET_FIELD_V(ParserLayer4UdfCam_t,
                                          ParserLayer4UdfCam_array_0_layer4DestPort_f + step * i, \
                                          &layer4_udf_cam);

            ip_version = DRV_GET_FIELD_V(ParserLayer4UdfCam_t,
                                         ParserLayer4UdfCam_array_0_ipVersion_f + step * i, \
                                         &layer4_udf_cam);
            if (valid)
            {
                if ((field1 == field2)
                   && (!IS_BIT_SET(field1, SYS_PARSER_LAYER4_UDF_FIELD_LAYER4_TYPE)
                   || (layer4_type == p_udf->l3_header_protocol))
                   && (!IS_BIT_SET(field1, SYS_PARSER_LAYER4_UDF_FIELD_SRC_PORT)
                   || (l4_src_port == p_udf->l4_src_port))
                   && (!IS_BIT_SET(field1, SYS_PARSER_LAYER4_UDF_FIELD_DEST_PORT)
                   || (l4_dst_port == p_udf->l4_dst_port))
                   && (!IS_BIT_SET(field1, SYS_PARSER_LAYER4_UDF_FIELD_IP_VERSION)
                   || (ip_version == SYS_PAS_CTC_IPVER_MAP_CHIP_IPVER(p_udf->ip_version))))
                {
                    *p_hit = 1;
                    *p_hit_idx = i;
                    break;
                }
            }

            if ((0 == *p_free) && (!valid))
            {
                *p_free = 1;
                *p_free_idx = i;
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_move_udf(uint8 lchip, ctc_parser_udf_type_t type, uint8 is_upper, int8 start_idx, int8 end_idx)
{
    int8  step = 0;
    /* i = is_upper ? 2 : 1; */
    uint8 field_sel = 0, fragment = 0, ip_version = 0, layer4_type = 0;
    uint8 l3_udf_entry_valid = 0, l4_udf_entry_valid = 0, entry_offset[4] = {0};
    uint16 layer4_dest_port = 0, layer4_src_port = 0, ether_type = 0;
    ParserLayer3UdfCam_m layer3_udf_cam;
    ParserLayer4UdfCam_m layer4_udf_cam;

    ParserLayer3UdfCamResult_m layer3_udf_cam_result;
    ParserLayer4UdfCamResult_m layer4_udf_cam_result;

    sal_memset(&layer3_udf_cam, 0, sizeof(ParserLayer3UdfCam_m));
    sal_memset(&layer4_udf_cam, 0, sizeof(ParserLayer4UdfCam_m));

    sal_memset(&layer3_udf_cam_result, 0, sizeof(ParserLayer3UdfCamResult_m));
    sal_memset(&layer4_udf_cam_result, 0, sizeof(ParserLayer4UdfCamResult_m));

    if (CTC_PARSER_UDF_TYPE_L3_UDF == type)
    {
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer3_udf_cam(lchip, &layer3_udf_cam));
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer3_udf_cam_result(lchip, &layer3_udf_cam_result));

        for (; is_upper ? start_idx >= end_idx : start_idx <= end_idx; is_upper ? start_idx-- : start_idx++)
        {
            step = ParserLayer3UdfCam_array_1_l3UdfEntryValid_f - ParserLayer3UdfCam_array_0_l3UdfEntryValid_f;

            ether_type = DRV_GET_FIELD_V(ParserLayer3UdfCam_t, \
                                         ParserLayer3UdfCam_array_0_layer3HeaderProtocol_f + step * start_idx, \
                                         &layer3_udf_cam);
            l3_udf_entry_valid = DRV_GET_FIELD_V(ParserLayer3UdfCam_t, \
                                                 ParserLayer3UdfCam_array_0_l3UdfEntryValid_f + step * start_idx, \
                                                 &layer3_udf_cam);

            step = ParserLayer3UdfCamResult_array_1_l3UdfEntryOffset0_f - ParserLayer3UdfCamResult_array_0_l3UdfEntryOffset0_f;

            entry_offset[0] = DRV_GET_FIELD_V(ParserLayer3UdfCamResult_t, \
                                              ParserLayer3UdfCamResult_array_0_l3UdfEntryOffset0_f + step * start_idx, \
                                              &layer3_udf_cam_result);
            entry_offset[1] = DRV_GET_FIELD_V(ParserLayer3UdfCamResult_t, \
                                              ParserLayer3UdfCamResult_array_0_l3UdfEntryOffset1_f + step * start_idx, \
                                              &layer3_udf_cam_result);
            entry_offset[2] = DRV_GET_FIELD_V(ParserLayer3UdfCamResult_t, \
                                              ParserLayer3UdfCamResult_array_0_l3UdfEntryOffset2_f + step * start_idx, \
                                              &layer3_udf_cam_result);
            entry_offset[3] = DRV_GET_FIELD_V(ParserLayer3UdfCamResult_t, \
                                              ParserLayer3UdfCamResult_array_0_l3UdfEntryOffset3_f + step * start_idx, \
                                              &layer3_udf_cam_result);

            DRV_SET_FIELD_V(ParserLayer3UdfCamResult_t, \
                            ParserLayer3UdfCamResult_array_0_l3UdfEntryOffset0_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer3_udf_cam_result, entry_offset[0]);
            DRV_SET_FIELD_V(ParserLayer3UdfCamResult_t, \
                            ParserLayer3UdfCamResult_array_0_l3UdfEntryOffset1_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer3_udf_cam_result, entry_offset[1]);
            DRV_SET_FIELD_V(ParserLayer3UdfCamResult_t, \
                            ParserLayer3UdfCamResult_array_0_l3UdfEntryOffset2_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer3_udf_cam_result, entry_offset[2]);
            DRV_SET_FIELD_V(ParserLayer3UdfCamResult_t, \
                            ParserLayer3UdfCamResult_array_0_l3UdfEntryOffset3_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer3_udf_cam_result, entry_offset[3]);

            CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer3_udf_cam_result(lchip, &layer3_udf_cam_result));

            step = ParserLayer3UdfCam_array_1_l3UdfEntryValid_f - ParserLayer3UdfCam_array_0_l3UdfEntryValid_f;

            DRV_SET_FIELD_V(ParserLayer3UdfCam_t, \
                            ParserLayer3UdfCam_array_0_layer3HeaderProtocol_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer3_udf_cam, ether_type);

            DRV_SET_FIELD_V(ParserLayer3UdfCam_t, \
                            ParserLayer3UdfCam_array_0_l3UdfEntryValid_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer3_udf_cam, l3_udf_entry_valid);

            DRV_SET_FIELD_V(ParserLayer3UdfCam_t,
                            ParserLayer3UdfCam_array_0_l3UdfEntryValid_f + step * start_idx,
                            &layer3_udf_cam, 0);

            CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer3_udf_cam(lchip, &layer3_udf_cam));
        }
    }
    else if (CTC_PARSER_UDF_TYPE_L4_UDF == type)
    {
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer4_udf_cam(lchip, &layer4_udf_cam));
        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer4_udf_cam_result(lchip, &layer4_udf_cam_result));

        for (; is_upper ? start_idx >= end_idx : start_idx <= end_idx; is_upper ? start_idx-- : start_idx++)
        {
            step = ParserLayer4UdfCam_array_1_l4UdfEntryValid_f - ParserLayer4UdfCam_array_0_l4UdfEntryValid_f;

            field_sel = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, \
                                        ParserLayer4UdfCam_array_0_fieldSel_f + step * start_idx, \
                                        &layer4_udf_cam);
            fragment = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, \
                                       ParserLayer4UdfCam_array_0_fragment_f + step * start_idx, \
                                       &layer4_udf_cam);
            ip_version = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, \
                                         ParserLayer4UdfCam_array_0_ipVersion_f + step * start_idx, \
                                         &layer4_udf_cam);
            l4_udf_entry_valid = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, \
                                                 ParserLayer4UdfCam_array_0_l4UdfEntryValid_f + step * start_idx, \
                                                 &layer4_udf_cam);
            layer4_dest_port = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, \
                                               ParserLayer4UdfCam_array_0_layer4DestPort_f + step * start_idx, \
                                               &layer4_udf_cam);
            layer4_src_port = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, \
                                               ParserLayer4UdfCam_array_0_layer4SourcePort_f + step * start_idx, \
                                               &layer4_udf_cam);
            layer4_type = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, \
                                          ParserLayer4UdfCam_array_0_layer4Type_f + step * start_idx, \
                                          &layer4_udf_cam);

            step = ParserLayer4UdfCamResult_array_1_l4UdfEntryOffset3_f - ParserLayer4UdfCamResult_array_0_l4UdfEntryOffset3_f;

            entry_offset[0] = DRV_GET_FIELD_V(ParserLayer4UdfCamResult_t, \
                                              ParserLayer4UdfCamResult_array_0_l4UdfEntryOffset0_f + step * start_idx, \
                                              &layer4_udf_cam_result);
            entry_offset[1] = DRV_GET_FIELD_V(ParserLayer4UdfCamResult_t, \
                                              ParserLayer4UdfCamResult_array_0_l4UdfEntryOffset1_f + step * start_idx, \
                                              &layer4_udf_cam_result);
            entry_offset[2] = DRV_GET_FIELD_V(ParserLayer4UdfCamResult_t, \
                                              ParserLayer4UdfCamResult_array_0_l4UdfEntryOffset2_f + step * start_idx, \
                                              &layer4_udf_cam_result);
            entry_offset[3] = DRV_GET_FIELD_V(ParserLayer4UdfCamResult_t, \
                                              ParserLayer4UdfCamResult_array_0_l4UdfEntryOffset3_f + step * start_idx, \
                                              &layer4_udf_cam_result);

            DRV_SET_FIELD_V(ParserLayer4UdfCamResult_t, \
                            ParserLayer4UdfCamResult_array_0_l4UdfEntryOffset0_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer4_udf_cam_result, entry_offset[0]);
            DRV_SET_FIELD_V(ParserLayer4UdfCamResult_t, \
                            ParserLayer4UdfCamResult_array_0_l4UdfEntryOffset1_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer4_udf_cam_result, entry_offset[1]);
            DRV_SET_FIELD_V(ParserLayer4UdfCamResult_t, \
                            ParserLayer4UdfCamResult_array_0_l4UdfEntryOffset2_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer4_udf_cam_result, entry_offset[2]);
            DRV_SET_FIELD_V(ParserLayer4UdfCamResult_t, \
                            ParserLayer4UdfCamResult_array_0_l4UdfEntryOffset3_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer4_udf_cam_result, entry_offset[3]);
            CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_udf_cam_result(lchip, &layer4_udf_cam_result));

            step = ParserLayer4UdfCam_array_1_l4UdfEntryValid_f - ParserLayer4UdfCam_array_0_l4UdfEntryValid_f;

            DRV_SET_FIELD_V(ParserLayer4UdfCam_t, \
                            ParserLayer4UdfCam_array_0_fieldSel_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer4_udf_cam, field_sel);
            DRV_SET_FIELD_V(ParserLayer4UdfCam_t, \
                            ParserLayer4UdfCam_array_0_fragment_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer4_udf_cam, fragment);
            DRV_SET_FIELD_V(ParserLayer4UdfCam_t, \
                            ParserLayer4UdfCam_array_0_ipVersion_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer4_udf_cam, ip_version);
            DRV_SET_FIELD_V(ParserLayer4UdfCam_t, \
                            ParserLayer4UdfCam_array_0_l4UdfEntryValid_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer4_udf_cam, l4_udf_entry_valid);
            DRV_SET_FIELD_V(ParserLayer4UdfCam_t, \
                            ParserLayer4UdfCam_array_0_layer4DestPort_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer4_udf_cam, layer4_dest_port);
            DRV_SET_FIELD_V(ParserLayer4UdfCam_t, \
                            ParserLayer4UdfCam_array_0_layer4SourcePort_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer4_udf_cam, layer4_src_port);
            DRV_SET_FIELD_V(ParserLayer4UdfCam_t, \
                            ParserLayer4UdfCam_array_0_layer4Type_f + step * (is_upper ? start_idx + 1 : start_idx - 1), \
                            &layer4_udf_cam, layer4_type);

            DRV_SET_FIELD_V(ParserLayer4UdfCam_t,
                            ParserLayer4UdfCam_array_0_l4UdfEntryValid_f + step * start_idx,
                            &layer4_udf_cam, 0);
            CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_udf_cam(lchip, &layer4_udf_cam));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_sort_udf(uint8 lchip, ctc_parser_udf_t* p_udf, int8* p_idx)
{
    uint8 src_field = 0, dst_field = 0, valid = 0;
    int8  step = 0, idx = 0;
    ParserLayer4UdfCam_m layer4_udf_cam;
    uint8  match_idx = 0xFF, free_idx = 0xFF;

    if (CTC_PARSER_UDF_TYPE_L4_UDF == p_udf->type)
    {
        step = ParserLayer4UdfCam_array_1_l4UdfEntryValid_f - ParserLayer4UdfCam_array_0_l4UdfEntryValid_f;

        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer4_udf_cam(lchip, &layer4_udf_cam));

        for (idx = 3; idx >= 0; idx--)
        {
            _sys_goldengate_parser_mapping_udf_l4field(lchip, p_udf, &dst_field);

            valid = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, \
                                    ParserLayer4UdfCam_array_0_l4UdfEntryValid_f + step * idx, \
                                    &layer4_udf_cam);

            if (valid)
            {
                src_field = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, \
                                            ParserLayer4UdfCam_array_0_fieldSel_f + step * idx, \
                                            &layer4_udf_cam);

                if (((dst_field | src_field) != dst_field) && ((dst_field | src_field) != src_field))
                {
                    continue;
                }

                if (dst_field == src_field)
                {
                    _sys_goldengate_parser_move_udf(lchip, p_udf->type, 1, 2, idx);
                    break;
                }

                /* dst field is more precision than src */
                if (src_field < dst_field)
                {
                    match_idx = idx;
                    continue;
                }
            }
            else
            {
                free_idx = idx;
            }
        }

        if (-1 != idx)
        {
            *p_idx = idx;
        }
        else if (0xFF != match_idx)
        {
            _sys_goldengate_parser_move_udf(lchip, p_udf->type, 1, 2, match_idx);
            *p_idx = match_idx;
        }
        else
        {
            *p_idx = free_idx;
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_parser_set_udf(uint8 lchip, uint32 index, ctc_parser_udf_t* p_udf)
{
    uint8 offset = 0, hit = 0, free = 0;
    int8  step = 0, idx = 0, hit_idx = 0, free_idx = 0;
    uint8 field = 0;

    ParserLayer3UdfCam_m layer3_udf_cam;
    ParserLayer4UdfCam_m layer4_udf_cam;

    ParserLayer3UdfCamResult_m layer3_udf_cam_result;
    ParserLayer4UdfCamResult_m layer4_udf_cam_result;

    CTC_PTR_VALID_CHECK(p_udf);

    sal_memset(&layer3_udf_cam, 0, sizeof(ParserLayer3UdfCam_m));
    sal_memset(&layer4_udf_cam, 0, sizeof(ParserLayer4UdfCam_m));

    sal_memset(&layer3_udf_cam_result, 0, sizeof(ParserLayer3UdfCamResult_m));
    sal_memset(&layer4_udf_cam_result, 0, sizeof(ParserLayer4UdfCamResult_m));

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "type:0x%X.\n", p_udf->type);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "ether_type:0x%X.\n", p_udf->ether_type);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "l3_header_protocol:0x%X.\n", p_udf->l3_header_protocol);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "ip_version:0x%X.\n", p_udf->ip_version);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "l4_src_port:0x%X.\n", p_udf->l4_src_port);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "l4_dst_port:0x%X.\n", p_udf->l4_dst_port);

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "udf_byte:0x%X.\n", p_udf->udf_num);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "relative_offset0:0x%X.\n", p_udf->udf_offset[0]);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "relative_offset1:0x%X.\n", p_udf->udf_offset[1]);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "relative_offset2:0x%X.\n", p_udf->udf_offset[2]);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "relative_offset3:0x%X.\n", p_udf->udf_offset[3]);

    if (0xFFFFFFFF != index)
    {
        if (index >= 4)
        {
            return CTC_E_INVALID_PARAM;
        }

        idx = index;
        goto SYS_GOLDENGATE_PARSER_UDF_DIRECT_SET;
    }

    CTC_ERROR_RETURN(_sys_goldengate_parser_check_udf(lchip, p_udf, &hit, &hit_idx, &free, &free_idx));

    if ((0 != p_udf->udf_num) && hit)
    {
        return CTC_E_ENTRY_EXIST;
    }

    if ((0 == p_udf->udf_num) && !hit)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if ((1 != free) && (0 != p_udf->udf_num))
    {
        return CTC_E_NO_RESOURCE;
    }

    if (0 == p_udf->udf_num)
    {
        idx = hit_idx;

        if (idx < 3)
        {
            CTC_ERROR_RETURN(_sys_goldengate_parser_move_udf(lchip, p_udf->type, 0, idx + 1, 3));
        }
        else
        {
            if (CTC_PARSER_UDF_TYPE_L3_UDF == p_udf->type)
            {
                CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer3_udf_cam(lchip, &layer3_udf_cam));
                DRV_SET_FIELD_V(ParserLayer3UdfCam_t,
                                ParserLayer3UdfCam_array_3_l3UdfEntryValid_f, &layer3_udf_cam, 0);
                CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer3_udf_cam(lchip, &layer3_udf_cam));
            }
            else
            {
                CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer4_udf_cam(lchip, &layer4_udf_cam));
                DRV_SET_FIELD_V(ParserLayer4UdfCam_t,
                                ParserLayer4UdfCam_array_3_l4UdfEntryValid_f, &layer4_udf_cam, 0);
                CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_udf_cam(lchip, &layer4_udf_cam));
            }
        }
        return CTC_E_NONE;
    }

    for (idx = 0; idx < p_udf->udf_num; idx++)
    {
        CTC_MAX_VALUE_CHECK(p_udf->udf_offset[idx], (SYS_PAS_UDF_MAX_OFFSET - 1));
    }

    if (CTC_PARSER_UDF_TYPE_L4_UDF == p_udf->type)
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_sort_udf(lchip, p_udf, &idx));
    }
    else
    {
        idx = free_idx;
    }

SYS_GOLDENGATE_PARSER_UDF_DIRECT_SET:

    if (CTC_PARSER_UDF_TYPE_L3_UDF == p_udf->type)
    {
        step = ParserLayer3UdfCam_array_1_l3UdfEntryValid_f - ParserLayer3UdfCam_array_0_l3UdfEntryValid_f;

        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer3_udf_cam(lchip, &layer3_udf_cam));
        DRV_SET_FIELD_V(ParserLayer3UdfCam_t, ParserLayer3UdfCam_array_0_layer3HeaderProtocol_f + step * idx, \
                        &layer3_udf_cam, p_udf->ether_type);
        DRV_SET_FIELD_V(ParserLayer3UdfCam_t, ParserLayer3UdfCam_array_0_l3UdfEntryValid_f + step * idx, \
                        &layer3_udf_cam, ((0 == p_udf->udf_num) ? 0 : 1));

        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer3_udf_cam(lchip, &layer3_udf_cam));

        CTC_ERROR_RETURN(_sys_goldengate_parser_sort_udf_offset(lchip, p_udf->udf_offset, &p_udf->udf_num));
        p_udf->udf_offset[3] = (p_udf->udf_num > 0) ? p_udf->udf_offset[p_udf->udf_num  - 1] : 0;

        step = ParserLayer3UdfCamResult_array_1_l3UdfEntryOffset0_f - ParserLayer3UdfCamResult_array_0_l3UdfEntryOffset0_f;

        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer3_udf_cam_result(lchip, &layer3_udf_cam_result));
        for (offset = 0; offset < 4; offset++)
        {
            DRV_SET_FIELD_V(ParserLayer3UdfCamResult_t, ParserLayer3UdfCamResult_array_0_l3UdfEntryOffset0_f \
                            + (step * idx) + offset, &layer3_udf_cam_result, p_udf->udf_offset[offset]);
        }
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer3_udf_cam_result(lchip, &layer3_udf_cam_result));
    }
    else if (CTC_PARSER_UDF_TYPE_L4_UDF == p_udf->type)
    {
        step = ParserLayer4UdfCam_array_1_l4UdfEntryValid_f - ParserLayer4UdfCam_array_0_l4UdfEntryValid_f;

        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer4_udf_cam(lchip, &layer4_udf_cam));

        field = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, \
                                ParserLayer4UdfCam_array_0_fieldSel_f + step * idx, \
                                &layer4_udf_cam);

        _sys_goldengate_parser_mapping_udf_l4field(lchip, p_udf, &field);

        DRV_SET_FIELD_V(ParserLayer4UdfCam_t, \
                        ParserLayer4UdfCam_array_0_layer4Type_f + step * idx, \
                        &layer4_udf_cam, p_udf->l3_header_protocol);

        DRV_SET_FIELD_V(ParserLayer4UdfCam_t, ParserLayer4UdfCam_array_0_layer4DestPort_f + step * idx, \
                        &layer4_udf_cam, (p_udf->is_l4_dst_port ? p_udf->l4_dst_port : 0));

        DRV_SET_FIELD_V(ParserLayer4UdfCam_t, ParserLayer4UdfCam_array_0_layer4SourcePort_f + step * idx, \
                        &layer4_udf_cam, (p_udf->is_l4_src_port ? p_udf->l4_src_port : 0));

        DRV_SET_FIELD_V(ParserLayer4UdfCam_t, ParserLayer4UdfCam_array_0_ipVersion_f + step * idx, \
                        &layer4_udf_cam, SYS_PAS_CTC_IPVER_MAP_CHIP_IPVER(p_udf->ip_version));

        /*First or no fragment packet.*/
        DRV_SET_FIELD_V(ParserLayer4UdfCam_t, \
                        ParserLayer4UdfCam_array_0_fragment_f + step * idx, \
                        &layer4_udf_cam, 0);

        DRV_SET_FIELD_V(ParserLayer4UdfCam_t, \
                        ParserLayer4UdfCam_array_0_fieldSel_f + step * idx, \
                        &layer4_udf_cam, field);

        DRV_SET_FIELD_V(ParserLayer4UdfCam_t, \
                        ParserLayer4UdfCam_array_0_l4UdfEntryValid_f + step * idx, \
                        &layer4_udf_cam, ((0 == p_udf->udf_num) ? 0 : 1));

        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_udf_cam(lchip, &layer4_udf_cam));

        CTC_ERROR_RETURN(_sys_goldengate_parser_sort_udf_offset(lchip, p_udf->udf_offset, &p_udf->udf_num));
        p_udf->udf_offset[3] = (p_udf->udf_num > 0) ? p_udf->udf_offset[p_udf->udf_num  - 1] : 0;

        step = ParserLayer4UdfCamResult_array_1_l4UdfEntryOffset0_f - ParserLayer4UdfCamResult_array_0_l4UdfEntryOffset0_f;

        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer4_udf_cam_result(lchip, &layer4_udf_cam_result));
        for (offset = 0; offset < 4; offset++)
        {
            DRV_SET_FIELD_V(ParserLayer4UdfCamResult_t, ParserLayer4UdfCamResult_array_0_l4UdfEntryOffset0_f \
                            + step * idx + offset,
                            &layer4_udf_cam_result, p_udf->udf_offset[offset]);
        }
        CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_udf_cam_result(lchip, &layer4_udf_cam_result));
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_parser_get_udf(uint8 lchip, uint32 index, ctc_parser_udf_t* p_udf)
{
    uint32 ip_version = 0;
    uint8 offset = 0, hit = 0, free = 0;
    int8  hit_idx = 0, free_idx = 0;
    int8  step = 0;

    ParserLayer3UdfCam_m layer3_udf_cam;
    ParserLayer4UdfCam_m layer4_udf_cam;

    ParserLayer3UdfCamResult_m layer3_udf_cam_result;
    ParserLayer4UdfCamResult_m layer4_udf_cam_result;

    CTC_PTR_VALID_CHECK(p_udf);

    sal_memset(&layer3_udf_cam, 0, sizeof(ParserLayer3UdfCam_m));
    sal_memset(&layer4_udf_cam, 0, sizeof(ParserLayer4UdfCam_m));

    sal_memset(&layer3_udf_cam_result, 0, sizeof(ParserLayer3UdfCamResult_m));
    sal_memset(&layer4_udf_cam_result, 0, sizeof(ParserLayer4UdfCamResult_m));

    if (0xFFFFFFFF != index)
    {
        if (((CTC_PARSER_UDF_TYPE_L3_UDF == p_udf->type) && (index >= TABLE_MAX_INDEX(ParserLayer3UdfCam_t)))
           || ((CTC_PARSER_UDF_TYPE_L4_UDF == p_udf->type) && (index >= TABLE_MAX_INDEX(ParserLayer4UdfCam_t))))
        {
            return CTC_E_INVALID_PARAM;
        }

        hit_idx = index;
        goto SYS_GOLDENGATE_PARSER_UDF_DIRECT_GET;
    }

    CTC_ERROR_RETURN(_sys_goldengate_parser_check_udf(lchip, p_udf, &hit, &hit_idx, &free, &free_idx));

    if (!hit)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

SYS_GOLDENGATE_PARSER_UDF_DIRECT_GET:

    if (CTC_PARSER_UDF_TYPE_L3_UDF == p_udf->type)
    {
        step = ParserLayer3UdfCam_array_1_l3UdfEntryValid_f - ParserLayer3UdfCam_array_0_l3UdfEntryValid_f;

        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer3_udf_cam(lchip, &layer3_udf_cam));

        p_udf->ether_type = DRV_GET_FIELD_V(ParserLayer3UdfCam_t, \
                                            ParserLayer3UdfCam_array_0_layer3HeaderProtocol_f + step * hit_idx, \
                                            &layer3_udf_cam);

        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer3_udf_cam_result(lchip, &layer3_udf_cam_result));

        step = ParserLayer3UdfCamResult_array_1_l3UdfEntryOffset0_f - ParserLayer3UdfCamResult_array_0_l3UdfEntryOffset0_f;

        for (offset = 0; offset < 4; offset++)
        {
            p_udf->udf_offset[offset] = DRV_GET_FIELD_V(ParserLayer3UdfCamResult_t, \
                                             ParserLayer3UdfCamResult_array_0_l3UdfEntryOffset0_f \
                                             + (step * hit_idx) + offset, &layer3_udf_cam_result);
        }

        p_udf->udf_num = _sys_goldengate_parser_udf_offset_num(lchip, p_udf->udf_offset);
    }
    else if (CTC_PARSER_UDF_TYPE_L4_UDF == p_udf->type)
    {
        uint32 l4_field_flag = 0;

        step = ParserLayer4UdfCam_array_1_l4UdfEntryValid_f - ParserLayer4UdfCam_array_0_l4UdfEntryValid_f;

        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer4_udf_cam(lchip, &layer4_udf_cam));

        l4_field_flag = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, \
                                        ParserLayer4UdfCam_array_0_fieldSel_f + step * hit_idx, \
                                        &layer4_udf_cam);

        ip_version = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, \
                                     ParserLayer4UdfCam_array_0_ipVersion_f + step * hit_idx, \
                                     &layer4_udf_cam);

        p_udf->ip_version = SYS_PAS_CHIP_IPVER_MAP_CTC_IPVER(ip_version);

        p_udf->l3_header_protocol = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, \
                                 ParserLayer4UdfCam_array_0_layer4Type_f + step * hit_idx, \
                                 &layer4_udf_cam);

        p_udf->is_l4_dst_port = CTC_FLAG_ISSET(l4_field_flag, SYS_PARSER_LAYER4_UDF_FIELD_DEST_PORT);

        p_udf->l4_dst_port = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, \
                                             ParserLayer4UdfCam_array_0_layer4DestPort_f + step * hit_idx, \
                                             &layer4_udf_cam);

        p_udf->is_l4_src_port = CTC_FLAG_ISSET(l4_field_flag, SYS_PARSER_LAYER4_UDF_FIELD_SRC_PORT);

        p_udf->l4_src_port = DRV_GET_FIELD_V(ParserLayer4UdfCam_t, \
                                             ParserLayer4UdfCam_array_0_layer4SourcePort_f + step * hit_idx, \
                                             &layer4_udf_cam);

        CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_layer4_udf_cam_result(lchip, &layer4_udf_cam_result));

        step = ParserLayer4UdfCamResult_array_1_l4UdfEntryOffset0_f - ParserLayer4UdfCamResult_array_0_l4UdfEntryOffset0_f;

        for (offset = 0; offset < 4; offset++)
        {
            p_udf->udf_offset[offset] = DRV_GET_FIELD_V(ParserLayer4UdfCamResult_t, \
                                                        ParserLayer4UdfCamResult_array_0_l4UdfEntryOffset0_f + step * hit_idx + offset, \
                                                        &layer4_udf_cam_result);
        }

        p_udf->udf_num = _sys_goldengate_parser_udf_offset_num(lchip, p_udf->udf_offset);
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/**
 @brief set ecmp hash field
*/
STATIC int32
_sys_goldengate_parser_set_hash_field(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "hash_type_bitmap:%d\n", p_hash_ctl->hash_type_bitmap);

    CTC_PTR_VALID_CHECK(p_hash_ctl);

    if (CTC_FLAG_ISSET(p_hash_ctl->hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_L2))
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_set_layer2_hash(lchip, p_hash_ctl, hash_usage));
    }

    if (CTC_FLAG_ISSET(p_hash_ctl->hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_IP))
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_set_ip_hash(lchip, p_hash_ctl, hash_usage));
    }

    if (CTC_FLAG_ISSET(p_hash_ctl->hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_L4))
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_set_l4_hash(lchip, p_hash_ctl, hash_usage));
    }

    if (CTC_FLAG_ISSET(p_hash_ctl->hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_MPLS))
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_set_mpls_hash(lchip, p_hash_ctl, hash_usage));
    }

    if (CTC_FLAG_ISSET(p_hash_ctl->hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_PBB))
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_set_pbb_hash(lchip, p_hash_ctl, hash_usage));
    }

    if (CTC_FLAG_ISSET(p_hash_ctl->hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_FCOE))
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_set_fcoe_hash(lchip, p_hash_ctl, hash_usage));
    }

    if (CTC_FLAG_ISSET(p_hash_ctl->hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_TRILL))
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_set_trill_hash(lchip, p_hash_ctl, hash_usage));
    }

    return CTC_E_NONE;
}

/**
 @brief set ecmp hash field
*/
int32
sys_goldengate_parser_set_ecmp_hash_field(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* p_hash_ctl)
{
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "hash_type_bitmap:%d\n", p_hash_ctl->hash_type_bitmap);

    CTC_PTR_VALID_CHECK(p_hash_ctl);

    if (p_hash_ctl->hash_type_bitmap & CTC_PARSER_HASH_TYPE_FLAGS_DLB_EFD)
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_set_hash_field(lchip, (ctc_parser_flow_hash_ctl_t*)p_hash_ctl,
                                                          SYS_PARSER_HASH_USAGE_FLOW));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_set_hash_field(lchip, (ctc_parser_ecmp_hash_ctl_t*)p_hash_ctl,
                                                               SYS_PARSER_HASH_USAGE_ECMP));
    }

    return CTC_E_NONE;
}

/**
 @brief set ecmp hash field
*/
int32
sys_goldengate_parser_set_linkagg_hash_field(uint8 lchip, ctc_parser_linkagg_hash_ctl_t* p_hash_ctl)
{
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "hash_type_bitmap:%d\n", p_hash_ctl->hash_type_bitmap);

    CTC_PTR_VALID_CHECK(p_hash_ctl);

    CTC_ERROR_RETURN(_sys_goldengate_parser_set_hash_field(lchip, (ctc_parser_linkagg_hash_ctl_t*)p_hash_ctl,
                                                           SYS_PARSER_HASH_USAGE_LINKAGG));

    return CTC_E_NONE;
}

/**
 @brief get ecmp hash field
*/
STATIC int32
_sys_goldengate_parser_get_hash_field(uint8 lchip, ctc_parser_hash_ctl_t* p_hash_ctl, sys_parser_hash_usage_t hash_usage)
{
    CTC_PTR_VALID_CHECK(p_hash_ctl);

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (CTC_FLAG_ISSET(p_hash_ctl->hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_L2))
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_get_layer2_hash(lchip, p_hash_ctl, hash_usage));
    }

    if (CTC_FLAG_ISSET(p_hash_ctl->hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_IP))
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_get_ip_hash(lchip, p_hash_ctl, hash_usage));
    }

    if (CTC_FLAG_ISSET(p_hash_ctl->hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_L4))
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_get_l4_hash(lchip, p_hash_ctl, hash_usage));
    }

    if (CTC_FLAG_ISSET(p_hash_ctl->hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_MPLS))
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_get_mpls_hash(lchip, p_hash_ctl, hash_usage));
    }

    if (CTC_FLAG_ISSET(p_hash_ctl->hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_PBB))
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_get_pbb_hash(lchip, p_hash_ctl, hash_usage));
    }

    if (CTC_FLAG_ISSET(p_hash_ctl->hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_FCOE))
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_get_fcoe_hash(lchip, p_hash_ctl, hash_usage));
    }

    if (CTC_FLAG_ISSET(p_hash_ctl->hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_TRILL))
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_get_trill_hash(lchip, p_hash_ctl, hash_usage));
    }

    return CTC_E_NONE;
}

/**
 @brief get ecmp hash field
*/
int32
sys_goldengate_parser_get_ecmp_hash_field(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* p_hash_ctl)
{
    CTC_PTR_VALID_CHECK(p_hash_ctl);

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (p_hash_ctl->hash_type_bitmap & CTC_PARSER_HASH_TYPE_FLAGS_DLB_EFD)
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_get_hash_field(lchip, (ctc_parser_ecmp_hash_ctl_t*)p_hash_ctl,
                                                               SYS_PARSER_HASH_USAGE_FLOW));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_parser_get_hash_field(lchip, (ctc_parser_ecmp_hash_ctl_t*)p_hash_ctl,
                                                               SYS_PARSER_HASH_USAGE_ECMP));
    }

    return CTC_E_NONE;
}

/**
 @brief get linkagg hash field
*/
int32
sys_goldengate_parser_get_linkagg_hash_field(uint8 lchip, ctc_parser_linkagg_hash_ctl_t* p_hash_ctl)
{
    CTC_PTR_VALID_CHECK(p_hash_ctl);

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_goldengate_parser_get_hash_field(lchip, (ctc_parser_linkagg_hash_ctl_t*)p_hash_ctl,
                                                            SYS_PARSER_HASH_USAGE_LINKAGG));

    return CTC_E_NONE;
}

/**
 @brief set parser global config info
*/
int32
sys_goldengate_parser_set_global_cfg(uint8 lchip, ctc_parser_global_cfg_t* p_global_cfg)
{

    uint32 value = 0;
    uint32 merge = 0;
    uint32 inner = 0;
    uint8 i = 0;
    ParserIpCtl_m ip_ctl;
    ParserLayer3HashCtl_m layer3_ctl;
    IpeUserIdHashCtl_m outer_ctl;
    IpeAclGenHashKeyCtl_m inner_ctl;

    CTC_PTR_VALID_CHECK(p_global_cfg);

    /* ipv4 small fragment offset, 0~3,means 0,8,16,24 bytes of small fragment length*/
    if (p_global_cfg->small_frag_offset > 3)
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "ecmp_hash_type:%d\n", p_global_cfg->ecmp_hash_type);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "linkagg_hash_type:%d\n", p_global_cfg->linkagg_hash_type);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "small_frag_offset:%d\n", p_global_cfg->small_frag_offset);

    sal_memset(&layer3_ctl, 0, sizeof(ParserLayer3HashCtl_m));
    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));
    sal_memset(&inner_ctl, 0, sizeof(IpeAclGenHashKeyCtl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_l3_hash_ctl_entry(lchip, &layer3_ctl));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_inner_hash_ctl(lchip, &inner_ctl));

    value = (CTC_PARSER_GEN_HASH_TYPE_XOR == p_global_cfg->ecmp_hash_type)\
            ? SYS_GOLDENGATE_PARSER_GEN_HASH_TYPE_XOR : SYS_GOLDENGATE_PARSER_GEN_HASH_TYPE_CRC;
    SetIpeUserIdHashCtl(V, ecmpHashType_f, &outer_ctl, value);
    SetIpeAclGenHashKeyCtl(V, ecmpHashType_f, &inner_ctl, value);
    SetParserLayer3HashCtl(V, layer3HashType_f, &layer3_ctl, value);

    value = (CTC_PARSER_GEN_HASH_TYPE_XOR == p_global_cfg->linkagg_hash_type)\
            ? SYS_GOLDENGATE_PARSER_GEN_HASH_TYPE_XOR : SYS_GOLDENGATE_PARSER_GEN_HASH_TYPE_CRC;
    SetIpeUserIdHashCtl(V, linkaggHashType_f, &outer_ctl, value);
    SetIpeAclGenHashKeyCtl(V, linkaggHashType_f, &inner_ctl, value);

    /* config tunnel hash key select */
    for (i = 0; i < CTC_PARSER_TUNNEL_TYPE_MAX; i ++)
    {
        if (p_global_cfg->ecmp_tunnel_hash_mode[i] == CTC_PARSER_TUNNEL_HASH_MODE_MERGE)
        {
            merge |= (1 << i);
        }
        else if (p_global_cfg->ecmp_tunnel_hash_mode[i] == CTC_PARSER_TUNNEL_HASH_MODE_INNER)
        {
            inner |= (1 << i);
        }
    }
    SetIpeAclGenHashKeyCtl(V, gMask_0_mergeOuterHdrHashBitmap_f, &inner_ctl, merge);
    SetIpeAclGenHashKeyCtl(V, gMask_0_useInnerHdrHashBitmap_f, &inner_ctl, inner);

    merge = 0;
    inner = 0;
    for (i = 0; i < CTC_PARSER_TUNNEL_TYPE_MAX; i ++)
    {
        if (p_global_cfg->linkagg_tunnel_hash_mode[i] == CTC_PARSER_TUNNEL_HASH_MODE_MERGE)
        {
            merge |= (1 << i);
        }
        else if (p_global_cfg->linkagg_tunnel_hash_mode[i] == CTC_PARSER_TUNNEL_HASH_MODE_INNER)
        {
            inner |= (1 << i);
        }
    }
    SetIpeAclGenHashKeyCtl(V, gMask_1_mergeOuterHdrHashBitmap_f, &inner_ctl, merge);
    SetIpeAclGenHashKeyCtl(V, gMask_1_useInnerHdrHashBitmap_f, &inner_ctl, inner);

    merge = 0;
    inner = 0;
    for (i = 0; i < CTC_PARSER_TUNNEL_TYPE_MAX; i ++)
    {
        if (p_global_cfg->dlb_efd_tunnel_hash_mode[i] == CTC_PARSER_TUNNEL_HASH_MODE_MERGE)
        {
            merge |= (1 << i);
        }
        else if (p_global_cfg->dlb_efd_tunnel_hash_mode[i] == CTC_PARSER_TUNNEL_HASH_MODE_INNER)
        {
            inner |= (1 << i);
        }
    }
    SetIpeAclGenHashKeyCtl(V, gMask_2_mergeOuterHdrHashBitmap_f, &inner_ctl, merge);
    SetIpeAclGenHashKeyCtl(V, gMask_2_useInnerHdrHashBitmap_f, &inner_ctl, inner);


    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_l3_hash_ctl_entry(lchip, &layer3_ctl));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_outer_hash_ctl(lchip, &outer_ctl));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_inner_hash_ctl(lchip, &inner_ctl));

    sal_memset(&ip_ctl, 0, sizeof(ParserIpCtl_m));
    SetParserIpCtl(V, smallFragmentOffset_f, &ip_ctl, p_global_cfg->small_frag_offset);
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_ip_ctl(lchip, &ip_ctl));

    return CTC_E_NONE;
}

/**
 @brief get parser global config info
*/
int32
sys_goldengate_parser_get_global_cfg(uint8 lchip, ctc_parser_global_cfg_t* p_global_cfg)
{

    uint32 value = 0;
    uint32 merge = 0;
    uint32 inner = 0;
    uint8 i = 0;
    ParserIpCtl_m ip_ctl;
    IpeUserIdHashCtl_m outer_ctl;
    IpeAclGenHashKeyCtl_m inner_ctl;

    CTC_PTR_VALID_CHECK(p_global_cfg);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "ecmp_hash_type:%d\n", p_global_cfg->ecmp_hash_type);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "linkagg_hash_type:%d\n", p_global_cfg->linkagg_hash_type);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "small_frag_offset:%d\n", p_global_cfg->small_frag_offset);

    sal_memset(&ip_ctl, 0, sizeof(ParserIpCtl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_ip_ctl(lchip, &ip_ctl));

    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));
    sal_memset(&inner_ctl, 0, sizeof(IpeAclGenHashKeyCtl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_inner_hash_ctl(lchip, &inner_ctl));

    GetParserIpCtl(A, smallFragmentOffset_f, &ip_ctl, &value);
    p_global_cfg->small_frag_offset = value;

    GetIpeUserIdHashCtl(A, ecmpHashType_f, &outer_ctl, &value);
    p_global_cfg->ecmp_hash_type = (SYS_GOLDENGATE_PARSER_GEN_HASH_TYPE_CRC == value) \
                                   ? CTC_PARSER_GEN_HASH_TYPE_CRC : CTC_PARSER_GEN_HASH_TYPE_XOR;

    GetIpeUserIdHashCtl(A, linkaggHashType_f, &outer_ctl, &value);
    p_global_cfg->linkagg_hash_type = (SYS_GOLDENGATE_PARSER_GEN_HASH_TYPE_CRC == value) \
                                      ? CTC_PARSER_GEN_HASH_TYPE_CRC : CTC_PARSER_GEN_HASH_TYPE_XOR;

    /* get tunnel hash key select */
    merge = GetIpeAclGenHashKeyCtl(V, gMask_0_mergeOuterHdrHashBitmap_f, &inner_ctl);
    inner = GetIpeAclGenHashKeyCtl(V, gMask_0_useInnerHdrHashBitmap_f, &inner_ctl);
    for (i = 0; i < CTC_PARSER_TUNNEL_TYPE_MAX; i ++)
    {
        if (merge & (1<<i))
        {
            p_global_cfg->ecmp_tunnel_hash_mode[i] = CTC_PARSER_TUNNEL_HASH_MODE_MERGE;
        }
        else if (inner & (1<<i))
        {
            p_global_cfg->ecmp_tunnel_hash_mode[i] = CTC_PARSER_TUNNEL_HASH_MODE_INNER;
        }
    }

    merge = GetIpeAclGenHashKeyCtl(V, gMask_1_mergeOuterHdrHashBitmap_f, &inner_ctl);
    inner = GetIpeAclGenHashKeyCtl(V, gMask_1_useInnerHdrHashBitmap_f, &inner_ctl);
    for (i = 0; i < CTC_PARSER_TUNNEL_TYPE_MAX; i ++)
    {
        if (merge & (1<<i))
        {
            p_global_cfg->linkagg_tunnel_hash_mode[i] = CTC_PARSER_TUNNEL_HASH_MODE_MERGE;
        }
        else if (inner & (1<<i))
        {
            p_global_cfg->linkagg_tunnel_hash_mode[i] = CTC_PARSER_TUNNEL_HASH_MODE_INNER;
        }
    }

    merge = GetIpeAclGenHashKeyCtl(V, gMask_2_mergeOuterHdrHashBitmap_f, &inner_ctl);
    inner = GetIpeAclGenHashKeyCtl(V, gMask_2_useInnerHdrHashBitmap_f, &inner_ctl);
    for (i = 0; i < CTC_PARSER_TUNNEL_TYPE_MAX; i ++)
    {
        if (merge & (1<<i))
        {
            p_global_cfg->dlb_efd_tunnel_hash_mode[i] = CTC_PARSER_TUNNEL_HASH_MODE_MERGE;
        }
        else if (inner & (1<<i))
        {
            p_global_cfg->dlb_efd_tunnel_hash_mode[i] = CTC_PARSER_TUNNEL_HASH_MODE_INNER;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_ethernet_ctl_init(uint8 lchip)
{

    uint32 cmd = 0;

    EpeL2TpidCtl_m tpid_ctl;
    EpeHdrAdjustCtl_m adjust_ctl;
    IpeUserIdHashCtl_m outer_ctl;
    ParserEthernetCtl_m ethernet_ctl;
    IpeAclGenHashKeyCtl_m inner_ctl;
    hw_mac_addr_t hw_mac;

    sal_memset(&hw_mac, 0, sizeof(hw_mac_addr_t));
    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_outer_hash_ctl(lchip, &outer_ctl));

    SetIpeUserIdHashCtl(V, gMask_0_cVlanIdHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_0_cCosHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_0_cCfiHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_0_sVlanIdHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_0_sCosHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_0_sCfiHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_0_l2ProtocolHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_0_portHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_0_macSaHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_0_macDaHashEn_f, &outer_ctl, 0);

    SetIpeUserIdHashCtl(V, gMask_1_cVlanIdHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_1_cCosHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_1_cCfiHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_1_sVlanIdHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_1_sCosHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_1_sCfiHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_1_l2ProtocolHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_1_portHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_1_macSaHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_1_macDaHashEn_f, &outer_ctl, 0);

    SetIpeUserIdHashCtl(V, gMask_2_cVlanIdHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_2_cCosHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_2_cCfiHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_2_sVlanIdHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_2_sCosHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_2_sCfiHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_2_l2ProtocolHashEn_f, &outer_ctl, 1);
    SetIpeUserIdHashCtl(V, gMask_2_portHashEn_f, &outer_ctl, 1);
    SetIpeUserIdHashCtl(V, gMask_2_macSaHashEn_f, &outer_ctl, 0);
    SetIpeUserIdHashCtl(V, gMask_2_macDaHashEn_f, &outer_ctl, 0);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_outer_hash_ctl(lchip, &outer_ctl));

    sal_memset(&inner_ctl, 0, sizeof(IpeAclGenHashKeyCtl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_get_parser_inner_hash_ctl(lchip, &inner_ctl));

    SetIpeAclGenHashKeyCtl(V, gMask_0_vlanIdHashEn_f, &inner_ctl, 0);
    SetIpeAclGenHashKeyCtl(V, gMask_0_cfiHashEn_f, &inner_ctl, 0);
    SetIpeAclGenHashKeyCtl(V, gMask_0_cosHashEn_f, &inner_ctl, 0);
    SetIpeAclGenHashKeyCtl(V, gMask_0_etherTypeHashEn_f, &inner_ctl, 0);
    SetIpeAclGenHashKeyCtl(V, gMask_0_macDaHashEn_f, &inner_ctl, 0);
    SetIpeAclGenHashKeyCtl(V, gMask_0_macSaHashEn_f, &inner_ctl, 0);

    SetIpeAclGenHashKeyCtl(V, gMask_1_vlanIdHashEn_f, &inner_ctl, 0);
    SetIpeAclGenHashKeyCtl(V, gMask_1_cfiHashEn_f, &inner_ctl, 0);
    SetIpeAclGenHashKeyCtl(V, gMask_1_cosHashEn_f, &inner_ctl, 0);
    SetIpeAclGenHashKeyCtl(V, gMask_1_etherTypeHashEn_f, &inner_ctl, 0);
    SetIpeAclGenHashKeyCtl(V, gMask_1_macDaHashEn_f, &inner_ctl, 0);
    SetIpeAclGenHashKeyCtl(V, gMask_1_macSaHashEn_f, &inner_ctl, 0);

    SetIpeAclGenHashKeyCtl(V, gMask_2_vlanIdHashEn_f, &inner_ctl, 0);
    SetIpeAclGenHashKeyCtl(V, gMask_2_cfiHashEn_f, &inner_ctl, 0);
    SetIpeAclGenHashKeyCtl(V, gMask_2_cosHashEn_f, &inner_ctl, 0);
    SetIpeAclGenHashKeyCtl(V, gMask_2_etherTypeHashEn_f, &inner_ctl, 0);
    SetIpeAclGenHashKeyCtl(V, gMask_2_macDaHashEn_f, &inner_ctl, 0);
    SetIpeAclGenHashKeyCtl(V, gMask_2_macSaHashEn_f, &inner_ctl, 0);

    sal_memset(&ethernet_ctl, 0, sizeof(ParserEthernetCtl_m));
    SetParserEthernetCtl(V, cvlanTpid_f, &ethernet_ctl, 0x8100);
    SetParserEthernetCtl(V, allowNonZeroOui_f, &ethernet_ctl, 1);
    SetParserEthernetCtl(V, ieee1588TypeEn_f, &ethernet_ctl, 1);
    SetParserEthernetCtl(V, slowProtocolTypeEn_f, &ethernet_ctl, 1);
    SetParserEthernetCtl(V, pbbTypeEn_f, &ethernet_ctl, 1);
    SetParserEthernetCtl(V, ethOamTypeEn_f, &ethernet_ctl, 1);
    SetParserEthernetCtl(V, fcoeTypeEn_f, &ethernet_ctl, 1);
    SetParserEthernetCtl(V, trillTypeEn_f, &ethernet_ctl, 1);
    SetParserEthernetCtl(V, arpTypeEn_f, &ethernet_ctl, 1);
    SetParserEthernetCtl(V, mplsMcastTypeEn_f, &ethernet_ctl, 1);
    SetParserEthernetCtl(V, mplsTypeEn_f, &ethernet_ctl, 1);
    SetParserEthernetCtl(V, ipv4TypeEn_f, &ethernet_ctl, 1);
    SetParserEthernetCtl(V, ipv6TypeEn_f, &ethernet_ctl, 1);
    SetParserEthernetCtl(V, maxLengthField_f, &ethernet_ctl, 1536);
    SetParserEthernetCtl(V, parsingQuadVlan_f, &ethernet_ctl, 1);
    SetParserEthernetCtl(V, vlanParsingNum_f, &ethernet_ctl, 2);
    SetParserEthernetCtl(V, array_0_svlanTpid_f, &ethernet_ctl, 0x8100);
    SetParserEthernetCtl(V, array_1_svlanTpid_f, &ethernet_ctl, 0x9100);
    SetParserEthernetCtl(V, array_2_svlanTpid_f, &ethernet_ctl, 0x88A8);
    SetParserEthernetCtl(V, array_3_svlanTpid_f, &ethernet_ctl, 0x88A8);
    SetParserEthernetCtl(V, arpTypeEn_f, &ethernet_ctl, 1);
    SetParserEthernetCtl(A, macSaMaskBits_f, &ethernet_ctl, hw_mac);

    sal_memset(&adjust_ctl, 0, sizeof(EpeHdrAdjustCtl_m));
    cmd = DRV_IOR(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &adjust_ctl));

    SetEpeHdrAdjustCtl(V, vlanEditEn_f, &adjust_ctl, 1);
    SetEpeHdrAdjustCtl(V, cvlanTagTpid_f, &adjust_ctl, 0x8100);
    SetEpeHdrAdjustCtl(V, array_0_svlanTagTpid_f, &adjust_ctl, 0x8100);
    SetEpeHdrAdjustCtl(V, array_1_svlanTagTpid_f, &adjust_ctl, 0x9100);
    SetEpeHdrAdjustCtl(V, array_2_svlanTagTpid_f, &adjust_ctl, 0x88A8);
    SetEpeHdrAdjustCtl(V, array_3_svlanTagTpid_f, &adjust_ctl, 0x88A8);
    SetEpeHdrAdjustCtl(V, portExtenderMcastEn_f, &adjust_ctl, 0);

    sal_memset(&tpid_ctl, 0, sizeof(EpeL2TpidCtl_m));
    SetEpeL2TpidCtl(V, array_0_svlanTpid_f, &tpid_ctl, 0x8100);
    SetEpeL2TpidCtl(V, array_1_svlanTpid_f, &tpid_ctl, 0x9100);
    SetEpeL2TpidCtl(V, array_2_svlanTpid_f, &tpid_ctl, 0x88A8);
    SetEpeL2TpidCtl(V, array_3_svlanTpid_f, &tpid_ctl, 0x88A8);
    SetEpeL2TpidCtl(V, iTagTpid_f, &tpid_ctl, 0x88e7);
    SetEpeL2TpidCtl(V, cvlanTpid_f, &tpid_ctl, 0x8100);
    SetEpeL2TpidCtl(V, bvlanTpid_f, &tpid_ctl, 0x88a8);

    cmd = DRV_IOW(ParserEthernetCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ethernet_ctl));

    cmd = DRV_IOW(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &adjust_ctl));

    cmd = DRV_IOW(EpeL2TpidCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tpid_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_ip_hash_init(uint8 lchip)
{
    ctc_parser_hash_ctl_t hash_ctl;

    sal_memset(&hash_ctl, 0, sizeof(ctc_parser_hash_ctl_t));

    CTC_SET_FLAG(hash_ctl.ip_flag, CTC_PARSER_IP_HASH_FLAGS_IPSA);
    CTC_SET_FLAG(hash_ctl.ip_flag, CTC_PARSER_IP_HASH_FLAGS_IPDA);

    CTC_ERROR_RETURN(_sys_goldengate_parser_set_ip_hash(lchip, &hash_ctl, SYS_PARSER_HASH_USAGE_ECMP));
    CTC_ERROR_RETURN(_sys_goldengate_parser_set_ip_hash(lchip, &hash_ctl, SYS_PARSER_HASH_USAGE_LINKAGG));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_mpls_hash_init(uint8 lchip)
{
    ctc_parser_hash_ctl_t hash_ctl;

    sal_memset(&hash_ctl, 0, sizeof(ctc_parser_hash_ctl_t));

    CTC_SET_FLAG(hash_ctl.mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_IPSA);
    CTC_SET_FLAG(hash_ctl.mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_IPDA);

    CTC_ERROR_RETURN(_sys_goldengate_parser_set_mpls_hash(lchip, &hash_ctl, SYS_PARSER_HASH_USAGE_ECMP));
    CTC_ERROR_RETURN(_sys_goldengate_parser_set_mpls_hash(lchip, &hash_ctl, SYS_PARSER_HASH_USAGE_LINKAGG));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_l2pro_cam_init(uint8 lchip)
{

    ParserLayer2ProtocolCam_m cam;
    ParserLayer2ProtocolCamValid_m valid;

    sal_memset(&cam, 0, sizeof(ParserLayer2ProtocolCam_m));
    sal_memset(&valid, 0, sizeof(ParserLayer2ProtocolCamValid_m));

    /*
       ipv4, ipv6, mpls, mpls_mcast, arp, fcoe, trill, eth_oam, pbb, slow_protocol, 1588(ptp)
       defined in _cm_com_parser_l2_protocol_lookup function
       parser_layer2_protocol_cam only used to mapping 4 user defined layer3 protocol
    */
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer2_protocol_cam_valid(lchip, &valid));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer2_protocol_cam(lchip, &cam));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_pbb_ctl_init(uint8 lchip)
{

    ParserPbbCtl_m ctl;

    sal_memset(&ctl, 0, sizeof(ParserPbbCtl_m));
    SetParserPbbCtl(V, pbbVlanParsingNum_f, &ctl, SYS_PAS_PBB_VLAN_PAS_NUM);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_pbb_ctl(lchip, &ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_l2flex_ctl_init(uint8 lchip)
{
#if 0

    parser_layer2_flex_ctl_m ctl;

    sal_memset(&ctl, 0, sizeof(parser_layer2_flex_ctl_m));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer2_flex_ctl(lchip, &ctl));
#endif

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_packet_type_map_init(uint8 lchip)
{

    ParserPacketTypeMap_m map;

    sal_memset(&map, 0, sizeof(ParserPacketTypeMap_m));
    /*
       ETHERNETV2, IPV4, MPLS, IPV6, MCAST_MPLS, TRILL
       defined in cm_com_parser_packet_parsing function
       parser_packet_type_map used to mapping 2 user defined type
    */
    SetParserPacketTypeMap(V, array_0_layer3Type_f, &map, CTC_PARSER_L3_TYPE_NONE);
    SetParserPacketTypeMap(V, array_1_layer3Type_f, &map, CTC_PARSER_L3_TYPE_NONE);
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_packet_type(lchip, &map));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_l3pro_cam_init(uint8 lchip)
{

    ParserLayer3ProtocolCam_m cam;
    ParserLayer3ProtocolCamValid_m valid;

    sal_memset(&cam, 0, sizeof(ParserLayer3ProtocolCam_m));
    sal_memset(&valid, 0, sizeof(ParserLayer3ProtocolCamValid_m));

    /*
      tcp, udp, gre, pbb_itag_oam, ipinip, v6inip, ipinv6, v6inv6, icmp, igmp, rdp , sctp, dccp
      is predefined in chip parser module.
      parser_layer3_protocol_cam only used to mapping 4 user defined layer4 protocol
   */
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer3_protocol_cam_valid(lchip, &valid));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer3_protocol_cam(lchip, &cam));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_hash_ctl_init(uint8 lchip)
{

    IpeUserIdHashCtl_m outer_ctl;
    IpeAclGenHashKeyCtl_m inner_ctl;

    sal_memset(&outer_ctl, 0, sizeof(IpeUserIdHashCtl_m));
    SetIpeUserIdHashCtl(V, ecmpHashType_f,
                        &outer_ctl, SYS_PAS_OUTER_ECMP_HASH_TYPE);
    SetIpeUserIdHashCtl(V, linkaggHashType_f,
                        &outer_ctl, SYS_PAS_OUTER_LINKAGG_HASH_TYPE);
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_outer_hash_ctl(lchip, &outer_ctl));

    sal_memset(&inner_ctl, 0, sizeof(IpeAclGenHashKeyCtl_m));
    /* flowid hash fixed use crc */
    SetIpeAclGenHashKeyCtl(V, ecmpHashType_f,
                        &inner_ctl, SYS_PAS_INNER_ECMP_HASH_TYPE);
    SetIpeAclGenHashKeyCtl(V, linkaggHashType_f,
                        &inner_ctl, SYS_PAS_INNER_ECMP_HASH_TYPE);

    /* always use merged key */
    SetIpeAclGenHashKeyCtl(V, gMask_0_mergeOuterHdrHashBitmap_f,
                        &inner_ctl, 0x1FF);
    SetIpeAclGenHashKeyCtl(V, gMask_1_mergeOuterHdrHashBitmap_f,
                        &inner_ctl, 0x1FF);
    SetIpeAclGenHashKeyCtl(V, gMask_2_mergeOuterHdrHashBitmap_f,
                        &inner_ctl, 0x1FF);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_inner_hash_ctl(lchip, &inner_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_mpls_ctl_init(uint8 lchip)
{

    IpeUserIdHashCtl_m ctl;

    sal_memset(&ctl, 0, sizeof(IpeUserIdHashCtl_m));
    SetIpeUserIdHashCtl(V, maxReserveLabel_f, &ctl, 0xF);
    SetIpeUserIdHashCtl(V, mplsEcmpUseReserveLabel_f, &ctl, 1);
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_outer_hash_ctl(lchip, &ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_trill_ctl_init(uint8 lchip)
{

    ParserTrillCtl_m ctl;

    sal_memset(&ctl, 0, sizeof(ctl));
    SetParserTrillCtl(V, trillInnerTpid_f, &ctl, 0x8200);
    SetParserTrillCtl(V, innerVlanTpidMode_f, &ctl, 1);
    SetParserTrillCtl(V, rBridgeChannelEtherType_f, &ctl, 0x8946);
    SetParserTrillCtl(V, trillBfdOamChannelProtocol0_f, &ctl, 2);
    SetParserTrillCtl(V, trillBfdOamChannelProtocol1_f, &ctl, 0xFFF);
    SetParserTrillCtl(V, trillBfdEchoChannelProtocol_f, &ctl, 3);
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_trill_ctl(lchip, &ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_l3pro_ctl_init(uint8 lchip)
{

    ParserLayer3ProtocolCtl_m ctl;

    sal_memset(&ctl, 0, sizeof(ParserLayer3ProtocolCtl_m));

    SetParserLayer3ProtocolCtl(V, pbbItagOamTypeEn_f, &ctl, 1);
    SetParserLayer3ProtocolCtl(V, v6InV6TypeEn_f, &ctl, 1);
    SetParserLayer3ProtocolCtl(V, ipInV6TypeEn_f, &ctl, 1);
    SetParserLayer3ProtocolCtl(V, v6InIpTypeEn_f, &ctl, 1);
    SetParserLayer3ProtocolCtl(V, ipInIpTypeEn_f, &ctl, 1);
    SetParserLayer3ProtocolCtl(V, igmpTypeEn_f, &ctl, 1);
    SetParserLayer3ProtocolCtl(V, icmpTypeEn_f, &ctl, 1);
    SetParserLayer3ProtocolCtl(V, greTypeEn_f, &ctl, 1);
    SetParserLayer3ProtocolCtl(V, udpTypeEn_f, &ctl, 1);
    SetParserLayer3ProtocolCtl(V, tcpTypeEn_f, &ctl, 1);
    SetParserLayer3ProtocolCtl(V, rdpTypeEn_f, &ctl, 1);
    SetParserLayer3ProtocolCtl(V, sctpTypeEn_f, &ctl, 1);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer3_protocol_ctl(lchip, &ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_l3type_ctl_init(uint8 lchip)
{

    ParserL3Ctl_m ctl;

    sal_memset(&ctl, 0, sizeof(ParserL3Ctl_m));

    SetParserL3Ctl(V, l3TypePtpEn_f, &ctl, 1);
    SetParserL3Ctl(V, l3TypeTrillEn_f, &ctl, 1);
    SetParserL3Ctl(V, l3TypeFcoeEn_f, &ctl, 1);
    SetParserL3Ctl(V, l3TypeSlowProtocolEn_f, &ctl, 1);
    SetParserL3Ctl(V, l3TypeEtherOamEn_f, &ctl, 1);
    SetParserL3Ctl(V, l3TypeCmacEn_f, &ctl, 1);
    SetParserL3Ctl(V, l3TypeArpEn_f, &ctl, 1);
    SetParserL3Ctl(V, l3TypeMplsEn_f, &ctl, 1);
    SetParserL3Ctl(V, l3TypeIpv6En_f, &ctl, 1);
    SetParserL3Ctl(V, l3TypeIpv4En_f, &ctl, 1);

    /* support parser ipv6 ext header hopByHopLen > 8 byte */
    SetParserL3Ctl(V, hopByHopLenType_f, &ctl, 1);

    /* Not care index */
    SetParserL3Ctl(V, l3UdfCareIndex_f, &ctl, 0);

    /* Igs second udf parser has limit */
    SetParserL3Ctl(V, l3UdfEn_f, &ctl, (SYS_PARSER_TYPE_FLAG_IGS_OUTER | SYS_PARSER_TYPE_FLAG_EGS_OUTER));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer3_type_ctl(lchip, &ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_l4_reg_ctl_init(uint8 lchip)
{

    ParserLayer4PortOpSel_m port_opsel;
    ParserLayer4PortOpCtl_m port_opctl;
    ParserLayer4FlagOpCtl_m l4flag_opctl;
    ParserLayer4FlexCtl_m l4flex_ctl;
    ParserLayer4AppCtl_m l4app_ctl;
    ParserLayer4AchCtl_m l4ach_ctl;

    sal_memset(&port_opsel, 0, sizeof(ParserLayer4PortOpSel_m));
    sal_memset(&port_opctl, 0, sizeof(ParserLayer4PortOpCtl_m));
    sal_memset(&l4flag_opctl, 0, sizeof(ParserLayer4FlagOpCtl_m));
    sal_memset(&l4flex_ctl, 0, sizeof(ParserLayer4FlexCtl_m));
    sal_memset(&l4app_ctl, 0, sizeof(ParserLayer4AppCtl_m));
    sal_memset(&l4ach_ctl, 0, sizeof(ParserLayer4AchCtl_m));

    SetParserLayer4PortOpSel(V, opDestPort_f, &port_opsel, 0xf);
    SetParserLayer4PortOpCtl(V, portMax0_f, &port_opctl, 0x4);
    SetParserLayer4PortOpCtl(V, portMin0_f, &port_opctl, 0x1);
    SetParserLayer4PortOpCtl(V, portMax1_f, &port_opctl, 0x8);
    SetParserLayer4PortOpCtl(V, portMin1_f, &port_opctl, 0x2);
    SetParserLayer4PortOpCtl(V, portMax2_f, &port_opctl, 0x10);
    SetParserLayer4PortOpCtl(V, portMin2_f, &port_opctl, 0x4);
    SetParserLayer4PortOpCtl(V, portMax3_f, &port_opctl, 0x20);
    SetParserLayer4PortOpCtl(V, portMin3_f, &port_opctl, 0x8);
    SetParserLayer4PortOpCtl(V, portMax4_f, &port_opctl, 0x40);
    SetParserLayer4PortOpCtl(V, portMin4_f, &port_opctl, 0x10);
    SetParserLayer4PortOpCtl(V, portMax5_f, &port_opctl, 0x80);
    SetParserLayer4PortOpCtl(V, portMin5_f, &port_opctl, 0x20);
    SetParserLayer4PortOpCtl(V, portMax6_f, &port_opctl, 0x100);
    SetParserLayer4PortOpCtl(V, portMin6_f, &port_opctl, 0x40);
    SetParserLayer4PortOpCtl(V, portMax7_f, &port_opctl, 0x200);
    SetParserLayer4PortOpCtl(V, portMin7_f, &port_opctl, 0x80);

    SetParserLayer4FlagOpCtl(V, opAndOr0_f, &l4flag_opctl, 0);
    SetParserLayer4FlagOpCtl(V, flagsMask0_f, &l4flag_opctl, 0);
    SetParserLayer4FlagOpCtl(V, opAndOr1_f, &l4flag_opctl, 0);
    SetParserLayer4FlagOpCtl(V, flagsMask1_f, &l4flag_opctl, 0x3f);
    SetParserLayer4FlagOpCtl(V, opAndOr2_f, &l4flag_opctl, 1);
    SetParserLayer4FlagOpCtl(V, flagsMask2_f, &l4flag_opctl, 0);
    SetParserLayer4FlagOpCtl(V, opAndOr2_f, &l4flag_opctl, 1);
    SetParserLayer4FlagOpCtl(V, flagsMask2_f, &l4flag_opctl, 0x3f);

    SetParserLayer4FlexCtl(V, layer4ByteSelect0_f, &l4flex_ctl, 0);
    SetParserLayer4FlexCtl(V, layer4ByteSelect1_f, &l4flex_ctl, 0);
    SetParserLayer4FlexCtl(V, layer4MinLength_f, &l4flex_ctl, 0);

    SetParserLayer4AppCtl(V, onlyUsePortRange_f, &l4app_ctl, 1);
    SetParserLayer4AppCtl(V, ptpEn_f, &l4app_ctl, 1);
    SetParserLayer4AppCtl(V, ntpEn_f, &l4app_ctl, 1);
    SetParserLayer4AppCtl(V, bfdEn_f, &l4app_ctl, 1);

    SetParserLayer4AppCtl(V, ntpPort_f, &l4app_ctl, 123);
    SetParserLayer4AppCtl(V, ptpPort0_f, &l4app_ctl, 320);
    SetParserLayer4AppCtl(V, ptpPort1_f, &l4app_ctl, 319);
    SetParserLayer4AppCtl(V, bfdPort0_f, &l4app_ctl, 3784);
    SetParserLayer4AppCtl(V, bfdPort1_f, &l4app_ctl, 4784);
    SetParserLayer4AppCtl(V, bfdPort0_f, &l4app_ctl, 3784);
    SetParserLayer4AppCtl(V, bfdPort1_f, &l4app_ctl, 4784);

    /* Not care index */
    SetParserLayer4AppCtl(V, l4UdfCareIndex_f, &l4app_ctl, 0);
    SetParserLayer4AppCtl(V, l4UdfEn_f, &l4app_ctl, (SYS_PARSER_TYPE_FLAG_IGS_OUTER | SYS_PARSER_TYPE_FLAG_EGS_OUTER));

    SetParserLayer4AchCtl(V, achY1731Type_f, &l4ach_ctl, 0x8902);
    SetParserLayer4AchCtl(V, achDmType_f, &l4ach_ctl, 0x000C);
    SetParserLayer4AchCtl(V, achDlmType_f, &l4ach_ctl, 0x000A);

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_port_op_sel(lchip, &port_opsel));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_port_op_ctl(lchip, &port_opctl));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_flag_op_ctl(lchip, &l4flag_opctl));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_flex_ctl(lchip, &l4flex_ctl));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_app_ctl(lchip, &l4app_ctl));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_ach_ctl(lchip, &l4ach_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_parser_udf_cam_init(uint8 lchip)
{

    ParserLayer2UdfCam_m parser_layer2_udf_cam;
    ParserLayer3UdfCam_m parser_layer3_udf_cam;
    ParserLayer4UdfCam_m parser_layer4_udf_cam;
    ParserLayer2UdfCamResult_m parser_layer2_udf_cam_result;
    ParserLayer3UdfCamResult_m parser_layer3_udf_cam_result;
    ParserLayer4UdfCamResult_m parser_layer4_udf_cam_result;

    sal_memset(&parser_layer2_udf_cam, 0, sizeof(ParserLayer2UdfCam_m));
    sal_memset(&parser_layer3_udf_cam, 0, sizeof(ParserLayer3UdfCam_m));
    sal_memset(&parser_layer4_udf_cam, 0, sizeof(ParserLayer4UdfCam_m));
    sal_memset(&parser_layer2_udf_cam_result, 0, sizeof(parser_layer2_udf_cam_result));
    sal_memset(&parser_layer3_udf_cam_result, 0, sizeof(parser_layer3_udf_cam_result));
    sal_memset(&parser_layer4_udf_cam_result, 0, sizeof(parser_layer4_udf_cam_result));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer2_udf_cam(lchip, &parser_layer2_udf_cam));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer3_udf_cam(lchip, &parser_layer3_udf_cam));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_udf_cam(lchip, &parser_layer4_udf_cam));

    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer2_udf_cam_result(lchip, &parser_layer2_udf_cam_result));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer3_udf_cam_result(lchip, &parser_layer3_udf_cam_result));
    CTC_ERROR_RETURN(sys_goldengate_parser_io_set_parser_layer4_udf_cam_result(lchip, &parser_layer4_udf_cam_result));

    return CTC_E_NONE;
}

/**
 @brief init parser module
*/
int32
sys_goldengate_parser_init(uint8 lchip, void* p_parser_global_cfg)
{
    CTC_PTR_VALID_CHECK(p_parser_global_cfg);

    CTC_ERROR_RETURN(_sys_goldengate_parser_ethernet_ctl_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_parser_l2pro_cam_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_parser_pbb_ctl_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_parser_l2flex_ctl_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_parser_packet_type_map_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_parser_l3pro_cam_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_parser_hash_ctl_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_parser_mpls_ctl_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_parser_l3type_ctl_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_parser_trill_ctl_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_parser_l3pro_ctl_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_parser_l4_reg_ctl_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_parser_udf_cam_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_parser_ip_hash_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_parser_mpls_hash_init(lchip));

    CTC_ERROR_RETURN(sys_goldengate_parser_set_global_cfg(lchip, p_parser_global_cfg));

    return CTC_E_NONE;
}

int32
sys_goldengate_parser_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    return CTC_E_NONE;
}

