/**
 @file sys_greatbelt_parser.c

 @date 2009-12-22

 @version v2.0

---file comments----
*/

#include "sal.h"
#include "ctc_error.h"
#include "ctc_vector.h"
#include "sys_greatbelt_parser.h"
#include "sys_greatbelt_register.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_parser_io.h"
#include "greatbelt/include/drv_io.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/

#define SYS_PAS_MAX_L4_APP_DATA_CTL_ENTRY_NUM 4

#define SYS_PAS_MAX_L3FLEX_BYTE_SEL 8

#define SYS_PAS_CAM_INDEX_CHECK(index, min, max) \
    do { \
        if (index < min || index >= max) \
        { \
            return CTC_E_INVALID_PARAM; \
        } \
    } while (0)

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

/**
 @brief set tpid
*/
int32
sys_greatbelt_parser_set_tpid(uint8 lchip, ctc_parser_l2_tpid_t type, uint16 tpid)
{
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "type:%d, tpid:%d\n", type, tpid);

    switch (type)
    {
        case CTC_PARSER_L2_TPID_CVLAN_TPID:
            CTC_ERROR_RETURN(sys_greatbelt_parser_io_set_cvlan_tpid(lchip, tpid));
            break;

        case CTC_PARSER_L2_TPID_ITAG_TPID:
            CTC_ERROR_RETURN(sys_greatbelt_parser_io_set_i_tag_tpid(lchip, tpid));
            break;

        case CTC_PARSER_L2_TPID_BLVAN_TPID:
            CTC_ERROR_RETURN(sys_greatbelt_parser_io_set_bvlan_tpid(lchip, tpid));
            break;

        case CTC_PARSER_L2_TPID_SVLAN_TPID_0:
            CTC_ERROR_RETURN(sys_greatbelt_parser_io_set_svlan_tpid0(lchip, tpid));
            break;

        case CTC_PARSER_L2_TPID_SVLAN_TPID_1:
            CTC_ERROR_RETURN(sys_greatbelt_parser_io_set_svlan_tpid1(lchip, tpid));
            break;

        case CTC_PARSER_L2_TPID_SVLAN_TPID_2:
            CTC_ERROR_RETURN(sys_greatbelt_parser_io_set_svlan_tpid2(lchip, tpid));
            break;

        case CTC_PARSER_L2_TPID_SVLAN_TPID_3:
            CTC_ERROR_RETURN(sys_greatbelt_parser_io_set_svlan_tpid3(lchip, tpid));
            break;

        case CTC_PARSER_L2_TPID_CNTAG_TPID:
            CTC_ERROR_RETURN(sys_greatbelt_parser_io_set_cn_tag_tpid(lchip, tpid));
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
sys_greatbelt_parser_get_tpid(uint8 lchip, ctc_parser_l2_tpid_t type, uint16* tpid)
{
    uint32 value = 0;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("type:%d\n", type);

    CTC_PTR_VALID_CHECK(tpid);

    switch (type)
    {
    case CTC_PARSER_L2_TPID_CVLAN_TPID:
        CTC_ERROR_RETURN(sys_greatbelt_parser_io_get_cvlan_tpid(lchip, &value));
        break;

    case CTC_PARSER_L2_TPID_ITAG_TPID:
        CTC_ERROR_RETURN(sys_greatbelt_parser_io_get_i_tag_tpid(lchip, &value));
        break;

    case CTC_PARSER_L2_TPID_BLVAN_TPID:
        CTC_ERROR_RETURN(sys_greatbelt_parser_io_get_bvlan_tpid(lchip, &value));
        break;

    case CTC_PARSER_L2_TPID_SVLAN_TPID_0:
        CTC_ERROR_RETURN(sys_greatbelt_parser_io_get_svlan_tpid0(lchip, &value));
        break;

    case CTC_PARSER_L2_TPID_SVLAN_TPID_1:
        CTC_ERROR_RETURN(sys_greatbelt_parser_io_get_svlan_tpid1(lchip, &value));
        break;

    case CTC_PARSER_L2_TPID_SVLAN_TPID_2:
        CTC_ERROR_RETURN(sys_greatbelt_parser_io_get_svlan_tpid2(lchip, &value));
        break;

    case CTC_PARSER_L2_TPID_SVLAN_TPID_3:
        CTC_ERROR_RETURN(sys_greatbelt_parser_io_get_svlan_tpid3(lchip, &value));
        break;

    case CTC_PARSER_L2_TPID_CNTAG_TPID:
        CTC_ERROR_RETURN(sys_greatbelt_parser_io_get_cn_tag_tpid(lchip, &value));
        break;

    default:
        return CTC_E_INVALID_PARAM;
        break;
    }

    *tpid = value;

    SYS_PARSER_DBG_INFO("tpid:%d\n", *tpid);
    return CTC_E_NONE;
}

/**
 @brief set max_length,based on the value differentiate type or length
*/
int32
sys_greatbelt_parser_set_max_length_filed(uint8 lchip, uint16 max_length)
{

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("max_length:%d\n", max_length);

    CTC_ERROR_RETURN(sys_greatbelt_parser_io_set_max_length_field(lchip, max_length));

    return CTC_E_NONE;
}

/**
 @brief get max_length value
*/
int32
sys_greatbelt_parser_get_max_length_filed(uint8 lchip, uint16* max_length)
{
    uint32 tmp = 0;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(max_length);

    CTC_ERROR_RETURN(sys_greatbelt_parser_io_get_max_length_field(lchip, &tmp));
    *max_length = tmp;

    SYS_PARSER_DBG_INFO("max_length:%d\n", *max_length);

    return CTC_E_NONE;
}

/**
 @brief set vlan parser num
*/
int32
sys_greatbelt_parser_set_vlan_parser_num(uint8 lchip, uint8 vlan_num)
{

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("vlan_num:%d\n", vlan_num);

    if (vlan_num > 2)
    {
        return CTC_E_INVALID_PARAM;
    }


    CTC_ERROR_RETURN(sys_greatbelt_parser_io_set_vlan_parsing_num(lchip, vlan_num));


    return CTC_E_NONE;
}

/**
 @brief get vlan parser num
*/
int32
sys_greatbelt_parser_get_vlan_parser_num(uint8 lchip, uint8* vlan_num)
{
    uint32 tmp = 0;

    CTC_PTR_VALID_CHECK(vlan_num);

    CTC_ERROR_RETURN(sys_greatbelt_parser_io_get_vlan_parsing_num(lchip, &tmp));
    *vlan_num = tmp;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_INFO("vlan_num:%d\n", *vlan_num);

    return CTC_E_NONE;
}

/**
 @brief set pbb header info
*/
int32
sys_greatbelt_parser_set_pbb_header(uint8 lchip, ctc_parser_pbb_header_t* pbb_header)
{
    parser_pbb_ctl_t pbb_ctl;

    CTC_PTR_VALID_CHECK(pbb_header);

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("nca_value:%d\n", pbb_header->nca_value_en);
    SYS_PARSER_DBG_PARAM("outer_vlan_is_cvlan:%d\n", pbb_header->outer_vlan_is_cvlan);
    SYS_PARSER_DBG_PARAM("pbb_vlan_parsing_num:%d\n", pbb_header->vlan_parsing_num);
        sal_memset(&pbb_ctl, 0, sizeof(parser_pbb_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_pbb_ctl_entry(lchip, &pbb_ctl));

        pbb_ctl.nca_value = pbb_header->nca_value_en ? 1 : 0;
        pbb_ctl.pbb_outer_vlan_is_cvlan = pbb_header->outer_vlan_is_cvlan ? 1 : 0;

        if (pbb_header->vlan_parsing_num > 3)
        {
            return CTC_E_INVALID_PARAM;
        }

        pbb_ctl.pbb_vlan_parsing_num = pbb_header->vlan_parsing_num;

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_pbb_ctl_entry(lchip, &pbb_ctl));

    return CTC_E_NONE;
}

/**
 @brief get pbb header info
*/
int32
sys_greatbelt_parser_get_pbb_header(uint8 lchip, ctc_parser_pbb_header_t* pbb_header)
{
    parser_pbb_ctl_t pbb_ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(pbb_header);

    sal_memset(&pbb_ctl, 0, sizeof(parser_pbb_ctl_t));
    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_pbb_ctl_entry(lchip, &pbb_ctl));

    pbb_header->nca_value_en = pbb_ctl.nca_value;
    pbb_header->outer_vlan_is_cvlan = pbb_ctl.pbb_outer_vlan_is_cvlan;
    pbb_header->vlan_parsing_num = pbb_ctl.pbb_vlan_parsing_num;

    return CTC_E_NONE;
}

/**
 @brief set l2 flex header info
*/
int32
sys_greatbelt_parser_set_l2flex_header(uint8 lchip, ctc_parser_l2flex_header_t* l2flex_header)
{
    parser_layer2_flex_ctl_t flex;

    CTC_PTR_VALID_CHECK(l2flex_header);

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("mac_da_basic_offset:%d\n", l2flex_header->mac_da_basic_offset);
    SYS_PARSER_DBG_PARAM("header_protocol_basic_offset:%d\n", l2flex_header->header_protocol_basic_offset);
    SYS_PARSER_DBG_PARAM("l2_basic_offset:%d\n", l2flex_header->l2_basic_offset);

    if (l2flex_header->mac_da_basic_offset > 26)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (l2flex_header->header_protocol_basic_offset > 30)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (l2flex_header->l2_basic_offset > 0x3f)
    {
        return CTC_E_INVALID_PARAM;
    }

        sal_memset(&flex, 0, sizeof(parser_layer2_flex_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_layer2_flex_ctl_entry(lchip, &flex));

        flex.layer2_byte_select0 = l2flex_header->mac_da_basic_offset;
        flex.layer2_byte_select1 = l2flex_header->mac_da_basic_offset + 1;
        flex.layer2_byte_select2 = l2flex_header->mac_da_basic_offset + 2;
        flex.layer2_byte_select3 = l2flex_header->mac_da_basic_offset + 3;
        flex.layer2_byte_select4 = l2flex_header->mac_da_basic_offset + 4;
        flex.layer2_byte_select5 = l2flex_header->mac_da_basic_offset + 5;

        flex.layer2_protocol_byte_select0 = l2flex_header->header_protocol_basic_offset;
        flex.layer2_protocol_byte_select1 = l2flex_header->header_protocol_basic_offset + 1;

        flex.layer2_min_length = l2flex_header->min_length;
        flex.layer2_basic_offset = l2flex_header->l2_basic_offset;

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer2_flex_ctl_entry(lchip, &flex));

    return CTC_E_NONE;
}

/**
 @brief get l2 flex header info
*/
int32
sys_greatbelt_parser_get_l2flex_header(uint8 lchip, ctc_parser_l2flex_header_t* l2flex_header)
{
    parser_layer2_flex_ctl_t flex;

    CTC_PTR_VALID_CHECK(l2flex_header);

    sal_memset(&flex, 0, sizeof(parser_layer2_flex_ctl_t));

    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_layer2_flex_ctl_entry(lchip, &flex));

    l2flex_header->mac_da_basic_offset = flex.layer2_byte_select0;
    l2flex_header->header_protocol_basic_offset = flex.layer2_protocol_byte_select0;
    l2flex_header->l2_basic_offset = flex.layer2_basic_offset;
    l2flex_header->min_length = flex.layer2_min_length;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_INFO("mac_da_basic_offset:%d\n", l2flex_header->mac_da_basic_offset);
    SYS_PARSER_DBG_INFO("header_protocol_basic_offset:%d\n", l2flex_header->header_protocol_basic_offset);
    SYS_PARSER_DBG_INFO("l2_basic_offset:%d\n", l2flex_header->l2_basic_offset);

    return CTC_E_NONE;
}

/**
 @brief mapping layer3 type
*/
int32
sys_greatbelt_parser_mapping_l3_type(uint8 lchip, uint8 index, ctc_parser_l2_protocol_entry_t* entry)
{
    uint8 isEth = 0;
    uint8 isPPP = 0;
    uint8 isSAP = 0;
    parser_layer2_protocol_cam_t cam;
    parser_layer2_protocol_cam_valid_t cam_valid;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("index:%d\n", index);
    SYS_PARSER_DBG_PARAM("l3_type:%d\n", entry->l3_type);

    CTC_PTR_VALID_CHECK(entry);
#if 0
    SYS_PAS_CAM_INDEX_CHECK(entry->l2_type, CTC_PARSER_L2_TYPE_RSV_USER_DEFINE0,
                            MAX_CTC_PARSER_L2_TYPE);
#endif

    /*only l3_type:13,14,15 can be configed by user
      and ethertype 0x8035 can be configed 6 */
    if (!((CTC_PARSER_L3_TYPE_RSV_USER_DEFINE0 == entry->l3_type)
        || (CTC_PARSER_L3_TYPE_RSV_USER_DEFINE1 == entry->l3_type)
        || (CTC_PARSER_L3_TYPE_RSV_USER_FLEXL3  == entry->l3_type)
        || ((CTC_PARSER_L3_TYPE_ARP == entry->l3_type) && (0x8035 == entry->l2_header_protocol))))
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((entry->addition_offset > 0xf)
        || (entry->l2_type > 0xf))
    {
        return CTC_E_INVALID_PARAM;
    }

    /*only 0,1,2,3 entry index*/
    if (index > 3)
    {
        return CTC_E_INVALID_PARAM;
    }

        sal_memset(&cam, 0, sizeof(parser_layer2_protocol_cam_t));
        sal_memset(&cam_valid, 0, sizeof(parser_layer2_protocol_cam_valid_t));

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_layer2_protocol_cam_entry(lchip, &cam));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_layer2_protocol_cam_valid_entry(lchip, &cam_valid));

        switch (entry->l2_type)
        {
        case CTC_PARSER_L2_TYPE_NONE:
            isEth = 0;
            isPPP = 0;
            isSAP = 0;
            break;

        case CTC_PARSER_L2_TYPE_ETH_V2:
        case CTC_PARSER_L2_TYPE_ETH_SNAP:
            isEth = 1;
            isPPP = 0;
            isSAP = 0;
            break;

        case CTC_PARSER_L2_TYPE_PPP_2B:
        case CTC_PARSER_L2_TYPE_PPP_1B:
            isEth = 0;
            isPPP = 1;
            isSAP = 0;
            break;

        case CTC_PARSER_L2_TYPE_ETH_SAP:
            isEth = 1;
            isPPP = 0;
            isSAP = 1;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }

        switch (index)
        {
        case 0:
            cam.l2_cam_additional_offset0 = entry->addition_offset;
            cam.l2_cam_layer3_type0 = entry->l3_type;
            cam.l2_cam_mask0 = entry->mask & 0x7fffff;
            cam.l2_cam_value0 = (isEth << 22) | (isPPP << 21)
                | (isSAP << 20)
                | (entry->l2_type << 16)
                | (entry->l2_header_protocol);
            break;

        case 1:
            cam.l2_cam_additional_offset1 = entry->addition_offset;
            cam.l2_cam_layer3_type1 = entry->l3_type;
            cam.l2_cam_mask1 = entry->mask & 0x7fffff;
            cam.l2_cam_value1 = (isEth << 22) | (isPPP << 21)
                | (isSAP << 20)
                | (entry->l2_type << 16)
                | (entry->l2_header_protocol);
            break;

        case 2:
            cam.l2_cam_additional_offset2 = entry->addition_offset;
            cam.l2_cam_layer3_type2 = entry->l3_type;
            cam.l2_cam_mask2 = entry->mask & 0x7fffff;
            cam.l2_cam_value2 = (isEth << 22) | (isPPP << 21)
                | (isSAP << 20)
                | (entry->l2_type << 16)
                | (entry->l2_header_protocol);
            break;

        case 3:
            cam.l2_cam_additional_offset3 = entry->addition_offset;
            cam.l2_cam_layer3_type3 = entry->l3_type;
            cam.l2_cam_mask3 = entry->mask & 0x7fffff;
            cam.l2_cam_value3 = (isEth << 22) | (isPPP << 21)
                | (isSAP << 20)
                | (entry->l2_type << 16)
                | (entry->l2_header_protocol);
            break;

        default:
            return CTC_E_INVALID_PARAM;
            break;
        }

        cam_valid.layer2_cam_entry_valid |= (1 << index);

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer2_protocol_cam_entry(lchip, &cam));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer2_protocol_cam_valid_entry(lchip, &cam_valid));

    return CTC_E_NONE;
}

/**
 @brief set the entry invalid based on the index
*/
int32
sys_greatbelt_parser_unmapping_l3_type(uint8 lchip, uint8 index)
{
    parser_layer2_protocol_cam_valid_t cam_valid;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("index:%d\n", index);

    if (index > 3)
    {
        return CTC_E_INVALID_PARAM;
    }

        sal_memset(&cam_valid, 0, sizeof(parser_layer2_protocol_cam_valid_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_layer2_protocol_cam_valid_entry(lchip, &cam_valid));

        if (CTC_IS_BIT_SET(cam_valid.layer2_cam_entry_valid, index))
        {
            cam_valid.layer2_cam_entry_valid &= (~(1 << index));
            CTC_ERROR_RETURN(
                sys_greatbelt_parser_io_set_parser_layer2_protocol_cam_valid_entry(lchip, &cam_valid));
        }

    return CTC_E_NONE;
}

/**
 @brief enable or disable parser layer3 type
*/
int32
sys_greatbelt_parser_enable_l3_type(uint8 lchip, ctc_parser_l3_type_t l3_type, bool enable)
{
    uint8 type_en = 0;
    parser_ethernet_ctl_t  ethernet_ctl;
    parser_layer2_protocol_cam_t cam;
    parser_layer2_protocol_cam_valid_t cam_valid;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    if ((l3_type > CTC_PARSER_L3_TYPE_RSV_USER_FLEXL3) || (l3_type < CTC_PARSER_L3_TYPE_IPV4))
    {
        return CTC_E_PARSER_INVALID_L3_TYPE;
    }

    type_en = ((TRUE == enable) ? 1 : 0);

    if (l3_type < CTC_PARSER_L3_TYPE_RSV_USER_FLEXL3)
    {
        sal_memset(&ethernet_ctl, 0, sizeof(parser_ethernet_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_ethernet_ctl_entry(lchip, &ethernet_ctl));

        switch (l3_type)
        {
        case CTC_PARSER_L3_TYPE_IPV4:
            ethernet_ctl.ipv4_type_en = type_en;
            break;

        case CTC_PARSER_L3_TYPE_IPV6:
            ethernet_ctl.ipv6_type_en = type_en;
            break;

        case CTC_PARSER_L3_TYPE_MPLS:
            ethernet_ctl.mpls_type_en = type_en;
            break;

        case CTC_PARSER_L3_TYPE_MPLS_MCAST:
            ethernet_ctl.mpls_mcast_type_en = type_en;
            break;

        case CTC_PARSER_L3_TYPE_ARP:
            ethernet_ctl.arp_type_en = type_en;
            break;

        case CTC_PARSER_L3_TYPE_FCOE:
            ethernet_ctl.fcoe_type_en = type_en;
            break;

        case CTC_PARSER_L3_TYPE_TRILL:
            ethernet_ctl.trill_type_en = type_en;
            break;

        case CTC_PARSER_L3_TYPE_ETHER_OAM:
            ethernet_ctl.eth_oam_type_en = type_en;
            break;

        case CTC_PARSER_L3_TYPE_SLOW_PROTO:
            ethernet_ctl.slow_protocol_type_en = type_en;
            break;

        case CTC_PARSER_L3_TYPE_CMAC:
            ethernet_ctl.pbb_type_en = type_en;
            break;

        case CTC_PARSER_L3_TYPE_PTP:
            ethernet_ctl.ieee1588_type_en = type_en;
            break;

        default:
            return CTC_E_INVALID_PARAM;
            break;
        }

            CTC_ERROR_RETURN(sys_greatbelt_parser_io_set_parser_ethernet_ctl_entry(lchip, &ethernet_ctl));
    }
    else
    {
        sal_memset(&cam, 0, sizeof(parser_layer2_protocol_cam_t));
        sal_memset(&cam_valid, 0, sizeof(parser_layer2_protocol_cam_valid_t));

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_layer2_protocol_cam_entry(lchip, &cam));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_layer2_protocol_cam_valid_entry(lchip, &cam_valid));

        if (type_en)
        {
            if (l3_type == cam.l2_cam_layer3_type0)
            {
                cam_valid.layer2_cam_entry_valid |= (1 << 0);
            }
            else if (l3_type == cam.l2_cam_layer3_type1)
            {
                cam_valid.layer2_cam_entry_valid |= (1 << 1);
            }
            else if (l3_type == cam.l2_cam_layer3_type2)
            {
                cam_valid.layer2_cam_entry_valid |= (1 << 2);
            }
            else if (l3_type == cam.l2_cam_layer3_type3)
            {
                cam_valid.layer2_cam_entry_valid |= (1 << 3);
            }
            else
            {
                return CTC_E_PARSER_INVALID_L3_TYPE;
            }
        }
        else
        {
            if (l3_type == cam.l2_cam_layer3_type0)
            {
                cam_valid.layer2_cam_entry_valid &= (~(1 << 0));
            }
            else if (l3_type == cam.l2_cam_layer3_type1)
            {
                cam_valid.layer2_cam_entry_valid &= (~(1 << 1));
            }
            else if (l3_type == cam.l2_cam_layer3_type2)
            {
                cam_valid.layer2_cam_entry_valid &= (~(1 << 2));
            }
            else if (l3_type == cam.l2_cam_layer3_type3)
            {
                cam_valid.layer2_cam_entry_valid &= (~(1 << 3));
            }
            else
            {
                return CTC_E_PARSER_INVALID_L3_TYPE;
            }
        }

            CTC_ERROR_RETURN(sys_greatbelt_parser_io_set_parser_layer2_protocol_cam_valid_entry(lchip, &cam_valid));
    }

    return CTC_E_NONE;
}

/**
 @brief set ip ecmp hash info
*/
STATIC int32
_sys_greatbelt_parser_set_ip_ecmp_hash(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{
    parser_ip_hash_ctl_t ip_ctl;

    CTC_PTR_VALID_CHECK(hash_ctl);

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

        sal_memset(&ip_ctl, 0, sizeof(parser_ip_hash_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_ip_hash_ctl_entry(lchip, &ip_ctl));

        ip_ctl.ip_protocol_ecmp_hash_en = ((hash_ctl->ip_flag) & CTC_PARSER_IP_ECMP_HASH_FLAGS_PROTOCOL) ? 1 : 0;
        ip_ctl.ip_dscp_ecmp_hash_en = ((hash_ctl->ip_flag) & CTC_PARSER_IP_ECMP_HASH_FLAGS_DSCP) ? 1 : 0;
        ip_ctl.ip_flow_label_ecmp_hash_en = ((hash_ctl->ip_flag) & CTC_PARSER_IP_ECMP_HASH_FLAGS_IPV6_FLOW_LABEL) ? 1 : 0;

        if (CTC_FLAG_ISSET(hash_ctl->ip_flag, CTC_PARSER_IP_ECMP_HASH_FLAGS_IPSA))
        {
            CTC_BIT_UNSET(ip_ctl.ip_ip_hash_disable, 0);
        }
        else
        {
            CTC_BIT_SET(ip_ctl.ip_ip_hash_disable, 0);
        }

        if (CTC_FLAG_ISSET(hash_ctl->ip_flag, CTC_PARSER_IP_ECMP_HASH_FLAGS_IPDA))
        {
            CTC_BIT_UNSET(ip_ctl.ip_ip_hash_disable, 1);
        }
        else
        {
            CTC_BIT_SET(ip_ctl.ip_ip_hash_disable, 1);
        }

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_ip_hash_ctl_entry(lchip, &ip_ctl));

    return CTC_E_NONE;
}

/**
 @brief get ip ecmp hash info
*/
STATIC int32
_sys_greatbelt_parser_get_ip_ecmp_hash(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{
    parser_ip_hash_ctl_t ip_ctl;

    CTC_PTR_VALID_CHECK(hash_ctl);

    sal_memset(&ip_ctl, 0, sizeof(parser_ip_hash_ctl_t));
    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_ip_hash_ctl_entry(lchip, &ip_ctl));

    hash_ctl->ip_flag = 0;
    if (ip_ctl.ip_protocol_ecmp_hash_en)
    {
        CTC_SET_FLAG(hash_ctl->ip_flag, CTC_PARSER_IP_ECMP_HASH_FLAGS_PROTOCOL);
    }

    if (ip_ctl.ip_dscp_ecmp_hash_en)
    {
        CTC_SET_FLAG(hash_ctl->ip_flag, CTC_PARSER_IP_ECMP_HASH_FLAGS_DSCP);
    }

    if (ip_ctl.ip_flow_label_ecmp_hash_en)
    {
        CTC_SET_FLAG(hash_ctl->ip_flag, CTC_PARSER_IP_ECMP_HASH_FLAGS_IPV6_FLOW_LABEL);
    }

    if (!CTC_IS_BIT_SET(ip_ctl.ip_ip_hash_disable, 0))
    {
        CTC_SET_FLAG(hash_ctl->ip_flag, CTC_PARSER_IP_ECMP_HASH_FLAGS_IPSA);
    }

    if (!CTC_IS_BIT_SET(ip_ctl.ip_ip_hash_disable, 1))
    {
        CTC_SET_FLAG(hash_ctl->ip_flag, CTC_PARSER_IP_ECMP_HASH_FLAGS_IPDA);
    }

    return CTC_E_NONE;
}

/**
 @brief set parser mpls ecmp hash info
*/
STATIC int32
_sys_greatbelt_parser_set_mpls_ecmp_hash(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{
    parser_mpls_ctl_t mpls_ctl;

    CTC_PTR_VALID_CHECK(hash_ctl);

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

        sal_memset(&mpls_ctl, 0, sizeof(parser_mpls_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_mpls_ctl_entry(lchip, &mpls_ctl));

        mpls_ctl.mpls_protocol_ecmp_hash_en = ((hash_ctl->mpls_flag) & CTC_PARSER_MPLS_ECMP_HASH_FLAGS_PROTOCOL) ? 1 : 0;
        mpls_ctl.mpls_dscp_ecmp_hash_en = ((hash_ctl->mpls_flag) & CTC_PARSER_MPLS_ECMP_HASH_FLAGS_DSCP) ? 1 : 0;
        mpls_ctl.mpls_flow_label_ecmp_hash_en = ((hash_ctl->mpls_flag) & CTC_PARSER_MPLS_ECMP_HASH_FLAGS_IPV6_FLOW_LABEL) ? 1 : 0;

        if (CTC_FLAG_ISSET(hash_ctl->mpls_flag, CTC_PARSER_MPLS_ECMP_HASH_FLAGS_IPSA))
        {
            CTC_BIT_UNSET(mpls_ctl.mpls_ip_hash_disable, 0);
        }
        else
        {
            CTC_BIT_SET(mpls_ctl.mpls_ip_hash_disable, 0);
        }

        if (CTC_FLAG_ISSET(hash_ctl->mpls_flag, CTC_PARSER_MPLS_ECMP_HASH_FLAGS_IPDA))
        {
            CTC_BIT_UNSET(mpls_ctl.mpls_ip_hash_disable, 1);
        }
        else
        {
            CTC_BIT_SET(mpls_ctl.mpls_ip_hash_disable, 1);
        }

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_mpls_ctl_entry(lchip, &mpls_ctl));

    return CTC_E_NONE;
}

/**
 @brief get parser mpls ecmp hash info
*/
STATIC int32
_sys_greatbelt_parser_get_mpls_ecmp_hash(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{
    parser_mpls_ctl_t mpls_ctl;

    sal_memset(&mpls_ctl, 0, sizeof(parser_mpls_ctl_t));

    CTC_PTR_VALID_CHECK(hash_ctl);

    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_mpls_ctl_entry(lchip, &mpls_ctl));

    hash_ctl->mpls_flag = 0;

    if (mpls_ctl.mpls_protocol_ecmp_hash_en)
    {
        CTC_SET_FLAG(hash_ctl->mpls_flag, CTC_PARSER_MPLS_ECMP_HASH_FLAGS_PROTOCOL);
    }

    if (mpls_ctl.mpls_dscp_ecmp_hash_en)
    {
        CTC_SET_FLAG(hash_ctl->mpls_flag, CTC_PARSER_MPLS_ECMP_HASH_FLAGS_DSCP);
    }

    if (mpls_ctl.mpls_flow_label_ecmp_hash_en)
    {
        CTC_SET_FLAG(hash_ctl->mpls_flag, CTC_PARSER_MPLS_ECMP_HASH_FLAGS_IPV6_FLOW_LABEL);
    }

    if (!CTC_IS_BIT_SET(mpls_ctl.mpls_ip_hash_disable, 0))
    {
        CTC_SET_FLAG(hash_ctl->mpls_flag, CTC_PARSER_MPLS_ECMP_HASH_FLAGS_IPSA);
    }

    if (!CTC_IS_BIT_SET(mpls_ctl.mpls_ip_hash_disable, 1))
    {
        CTC_SET_FLAG(hash_ctl->mpls_flag, CTC_PARSER_MPLS_ECMP_HASH_FLAGS_IPDA);
    }

    return CTC_E_NONE;
}

/**
 @brief set parser fcoe ecmp hash info
*/
STATIC int32
_sys_greatbelt_parser_set_fcoe_ecmp_hash(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{
    parser_fcoe_ctl_t fcoe_ctl;

    CTC_PTR_VALID_CHECK(hash_ctl);

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

        sal_memset(&fcoe_ctl, 0, sizeof(parser_fcoe_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_fcoe_ctl_entry(lchip, &fcoe_ctl));

        fcoe_ctl.fcoe_ecmp_hash_en = 0;
        if ((hash_ctl->fcoe_flag) & CTC_PARSER_FCOE_ECMP_HASH_FLAGS_SID)
        {
            CTC_BIT_SET(fcoe_ctl.fcoe_ecmp_hash_en, 0);
        }
        else
        {
            CTC_BIT_UNSET(fcoe_ctl.fcoe_ecmp_hash_en, 0);
        }

        if ((hash_ctl->fcoe_flag) & CTC_PARSER_FCOE_ECMP_HASH_FLAGS_DID)
        {
            CTC_BIT_SET(fcoe_ctl.fcoe_ecmp_hash_en, 1);
        }
        else
        {
            CTC_BIT_UNSET(fcoe_ctl.fcoe_ecmp_hash_en, 1);
        }

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_fcoe_ctl_entry(lchip, &fcoe_ctl));

    return CTC_E_NONE;
}

/**
 @brief get parser fcoe ecmp hash info
*/
STATIC int32
_sys_greatbelt_parser_get_fcoe_ecmp_hash(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{
    parser_fcoe_ctl_t fcoe_ctl;

    CTC_PTR_VALID_CHECK(hash_ctl);

    sal_memset(&fcoe_ctl, 0, sizeof(parser_fcoe_ctl_t));

    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_fcoe_ctl_entry(lchip, &fcoe_ctl));

    hash_ctl->fcoe_flag = 0;

    if (CTC_IS_BIT_SET(fcoe_ctl.fcoe_ecmp_hash_en, 0))
    {
        CTC_SET_FLAG(hash_ctl->fcoe_flag, CTC_PARSER_FCOE_ECMP_HASH_FLAGS_SID);
    }

    if (CTC_IS_BIT_SET(fcoe_ctl.fcoe_ecmp_hash_en, 1))
    {
        CTC_SET_FLAG(hash_ctl->fcoe_flag, CTC_PARSER_FCOE_ECMP_HASH_FLAGS_DID);
    }

    return CTC_E_NONE;
}

/**
 @brief set parser trill ecmp hash info
*/
STATIC int32
_sys_greatbelt_parser_set_trill_ecmp_hash(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{
    parser_trill_ctl_t trill_ctl;

    CTC_PTR_VALID_CHECK(hash_ctl);


        sal_memset(&trill_ctl, 0, sizeof(parser_trill_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_trill_ctl_entry(lchip, &trill_ctl));

        trill_ctl.trill_ecmp_hash_en = 0;

        if ((hash_ctl->trill_flag) & CTC_PARSER_TRILL_ECMP_HASH_FLAGS_INNER_VLAN_ID)
        {
            CTC_BIT_SET(trill_ctl.trill_ecmp_hash_en, 0);
        }
        else
        {
            CTC_BIT_UNSET(trill_ctl.trill_ecmp_hash_en, 0);
        }

        if ((hash_ctl->trill_flag) & CTC_PARSER_TRILL_ECMP_HASH_FLAGS_INGRESS_NICKNAME)
        {
            CTC_BIT_SET(trill_ctl.trill_ecmp_hash_en, 1);
        }
        else
        {
            CTC_BIT_UNSET(trill_ctl.trill_ecmp_hash_en, 1);
        }

        if ((hash_ctl->trill_flag) & CTC_PARSER_TRILL_ECMP_HASH_FLAGS_EGRESS_NICKNAME)
        {
            CTC_BIT_SET(trill_ctl.trill_ecmp_hash_en, 2);
        }
        else
        {
            CTC_BIT_UNSET(trill_ctl.trill_ecmp_hash_en, 2);
        }

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_trill_ctl_entry(lchip, &trill_ctl));

    return CTC_E_NONE;
}

/**
 @brief set parser l2 ecmp hash info
*/
STATIC int32
_sys_greatbelt_parser_set_l2_ecmp_hash(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{
    parser_ethernet_ctl_t eth_ctl;

    CTC_PTR_VALID_CHECK(hash_ctl);

        sal_memset(&eth_ctl, 0, sizeof(parser_ethernet_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_ethernet_ctl_entry(lchip, &eth_ctl));

        if ((hash_ctl->l2_flag) & CTC_PARSER_L2_ECMP_HASH_FLAGS_COS)
        {
            eth_ctl.cos_ecmp_hash_en = 1;
        }
        else
        {
            eth_ctl.cos_ecmp_hash_en = 0;
        }

        if ((hash_ctl->l2_flag) & CTC_PARSER_L2_ECMP_HASH_FLAGS_MACSA)
        {
            CTC_BIT_UNSET(eth_ctl.mac_hash_disable, 0);
        }
        else
        {
            CTC_BIT_SET(eth_ctl.mac_hash_disable, 0);
        }

        if ((hash_ctl->l2_flag) & CTC_PARSER_L2_ECMP_HASH_FLAGS_MACDA)
        {
            CTC_BIT_UNSET(eth_ctl.mac_hash_disable, 1);
        }
        else
        {
            CTC_BIT_SET(eth_ctl.mac_hash_disable, 1);
        }

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_ethernet_ctl_entry(lchip, &eth_ctl));

    return CTC_E_NONE;
}


/**
 @brief get parser trill ecmp hash info
*/
STATIC int32
_sys_greatbelt_parser_get_trill_ecmp_hash(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{
    parser_trill_ctl_t trill_ctl;

    CTC_PTR_VALID_CHECK(hash_ctl);

    sal_memset(&trill_ctl, 0, sizeof(parser_trill_ctl_t));

    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_trill_ctl_entry(lchip, &trill_ctl));

    hash_ctl->trill_flag = 0;

    if (CTC_IS_BIT_SET(trill_ctl.trill_ecmp_hash_en, 0))
    {
        CTC_SET_FLAG(hash_ctl->trill_flag, CTC_PARSER_TRILL_ECMP_HASH_FLAGS_INNER_VLAN_ID);
    }

    if (CTC_IS_BIT_SET(trill_ctl.trill_ecmp_hash_en, 1))
    {
        CTC_SET_FLAG(hash_ctl->trill_flag, CTC_PARSER_TRILL_ECMP_HASH_FLAGS_INGRESS_NICKNAME);
    }

    if (CTC_IS_BIT_SET(trill_ctl.trill_ecmp_hash_en, 2))
    {
        CTC_SET_FLAG(hash_ctl->trill_flag, CTC_PARSER_TRILL_ECMP_HASH_FLAGS_EGRESS_NICKNAME);
    }

    return CTC_E_NONE;
}


/**
 @brief get parser l2 ecmp hash info
*/
STATIC int32
_sys_greatbelt_parser_get_l2_ecmp_hash(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{
    parser_ethernet_ctl_t eth_ctl;

    CTC_PTR_VALID_CHECK(hash_ctl);

    sal_memset(&eth_ctl, 0, sizeof(eth_ctl));

    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_ethernet_ctl_entry(lchip, &eth_ctl));

    if (CTC_IS_BIT_SET(eth_ctl.cos_ecmp_hash_en, 0))
    {
        CTC_SET_FLAG(hash_ctl->l2_flag, CTC_PARSER_L2_ECMP_HASH_FLAGS_COS);
    }

    if (!CTC_IS_BIT_SET(eth_ctl.mac_hash_disable, 0))
    {
        CTC_SET_FLAG(hash_ctl->l2_flag, CTC_PARSER_L2_ECMP_HASH_FLAGS_MACSA);
    }

    if (!CTC_IS_BIT_SET(eth_ctl.mac_hash_disable, 1))
    {
        CTC_SET_FLAG(hash_ctl->l2_flag, CTC_PARSER_L2_ECMP_HASH_FLAGS_MACDA);
    }

    return CTC_E_NONE;
}

/**
 @brief set parser trill header info
*/
int32
sys_greatbelt_parser_set_trill_header(uint8 lchip, ctc_parser_trill_header_t* trill_header)
{
    parser_trill_ctl_t trill_ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("inner_tpid:%d\n", trill_header->inner_tpid);
    SYS_PARSER_DBG_PARAM("rbridge_channel_ether_type:%d\n", trill_header->rbridge_channel_ether_type);

    CTC_PTR_VALID_CHECK(trill_header);

        sal_memset(&trill_ctl, 0, sizeof(parser_trill_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_trill_ctl_entry(lchip, &trill_ctl));

        trill_ctl.trill_inner_tpid = trill_header->inner_tpid;
        trill_ctl.r_bridge_channel_ether_type = trill_header->rbridge_channel_ether_type;

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_trill_ctl_entry(lchip, &trill_ctl));

    return CTC_E_NONE;
}

/**
 @brief get parser trill header info
*/
int32
sys_greatbelt_parser_get_trill_header(uint8 lchip, ctc_parser_trill_header_t* trill_header)
{
    parser_trill_ctl_t trill_ctl;

    CTC_PTR_VALID_CHECK(trill_header);
    sal_memset(&trill_ctl, 0, sizeof(parser_trill_ctl_t));
    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_trill_ctl_entry(lchip, &trill_ctl));

    trill_header->inner_tpid = trill_ctl.trill_inner_tpid;
    trill_header->rbridge_channel_ether_type = trill_ctl.r_bridge_channel_ether_type;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_INFO("inner_tpid:%d\n", trill_header->inner_tpid);
    SYS_PARSER_DBG_INFO("rbridge_channel_ether_type:%d\n", trill_header->rbridge_channel_ether_type);

    return CTC_E_NONE;
}

/**
 @brief set parser l3flex header info
*/
int32
sys_greatbelt_parser_set_l3flex_header(uint8 lchip, ctc_parser_l3flex_header_t* l3flex_header)
{
    parser_layer3_flex_ctl_t ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("ipda_basic_offset:%d\n", l3flex_header->ipda_basic_offset);
    SYS_PARSER_DBG_PARAM("protocol_byte_sel:%d\n", l3flex_header->protocol_byte_sel);

    CTC_PTR_VALID_CHECK(l3flex_header);

    CTC_MAX_VALUE_CHECK(l3flex_header->ipda_basic_offset, 24);
    CTC_MAX_VALUE_CHECK(l3flex_header->protocol_byte_sel, 31);

        sal_memset(&ctl, 0, sizeof(parser_layer3_flex_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_layer3_flex_ctl_entry(lchip, &ctl));

        ctl.layer3_min_length = l3flex_header->l3min_length;
        ctl.layer3_basic_offset = l3flex_header->l3_basic_offset;
        ctl.layer3_protocol_byte_select = l3flex_header->protocol_byte_sel;
        ctl.layer3_byte_select0 = l3flex_header->ipda_basic_offset;
        ctl.layer3_byte_select1 = l3flex_header->ipda_basic_offset + 1;
        ctl.layer3_byte_select2 = l3flex_header->ipda_basic_offset + 2;
        ctl.layer3_byte_select3 = l3flex_header->ipda_basic_offset + 3;
        ctl.layer3_byte_select4 = l3flex_header->ipda_basic_offset + 4;
        ctl.layer3_byte_select5 = l3flex_header->ipda_basic_offset + 5;
        ctl.layer3_byte_select6 = l3flex_header->ipda_basic_offset + 6;
        ctl.layer3_byte_select7 = l3flex_header->ipda_basic_offset + 7;

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer3_flex_ctl_entry(lchip, &ctl));

    return CTC_E_NONE;
}

/**
 @brief get parser l3flex header info
*/
int32
sys_greatbelt_parser_get_l3flex_header(uint8 lchip, ctc_parser_l3flex_header_t* l3flex_header)
{
    parser_layer3_flex_ctl_t ctl;

    CTC_PTR_VALID_CHECK(l3flex_header);

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    sal_memset(&ctl, 0, sizeof(parser_layer3_flex_ctl_t));
    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_layer3_flex_ctl_entry(lchip, &ctl));

    l3flex_header->l3min_length = ctl.layer3_min_length;
    l3flex_header->l3_basic_offset = ctl.layer3_basic_offset;
    l3flex_header->protocol_byte_sel = ctl.layer3_protocol_byte_select;
    l3flex_header->ipda_basic_offset = ctl.layer3_byte_select0;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_INFO("ipda_basic_offset:%d\n", l3flex_header->ipda_basic_offset);
    SYS_PARSER_DBG_INFO("protocol_byte_sel:%d\n", l3flex_header->protocol_byte_sel);

    return CTC_E_NONE;
}

/**
 @brief mapping l4type,can add a new l3type,addition offset for the type,can get the layer4 type
*/
int32
sys_greatbelt_parser_mapping_l4_type(uint8 lchip, uint8 index, ctc_parser_l3_protocol_entry_t* entry)
{
    parser_layer3_protocol_cam_t cam;
    parser_layer3_protocol_cam_valid_t cam_valid;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("index:%d\n", index);
    SYS_PARSER_DBG_PARAM("l3_header_protocol:%d\n", entry->l3_header_protocol);
    SYS_PARSER_DBG_PARAM("l3_type:%d\n", entry->l3_type);
    SYS_PARSER_DBG_PARAM("l4_type:%d\n", entry->l4_type);

    CTC_PTR_VALID_CHECK(entry);

#if 0
    SYS_PAS_CAM_INDEX_CHECK(entry->l3_type, CTC_PARSER_L3_TYPE_RSV_USER_DEFINE0,
                            MAX_CTC_PARSER_L3_TYPE);

    SYS_PAS_CAM_INDEX_CHECK(entry->l4_type, CTC_PARSER_L4_TYPE_RSV_USER_DEFINE0,
                            MAX_CTC_PARSER_L4_TYPE);
#endif

    if ((entry->addition_offset > 0xf)
        || (entry->l3_type > 0xf)
        || (entry->l4_type > 0xf))
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

    if (index > 3)
    {
        return CTC_E_INVALID_PARAM;
    }

        sal_memset(&cam, 0, sizeof(parser_layer3_protocol_cam_t));
        sal_memset(&cam_valid, 0, sizeof(parser_layer3_protocol_cam_valid_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_layer3_protocol_cam_entry(lchip, &cam));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_layer3_protocol_cam_valid_entry(lchip, &cam_valid));

        switch (index)
        {
        case 0:
            cam.l3_cam_additional_offset0 = entry->addition_offset;
            cam.l3_cam_layer3_header_protocol0 = entry->l3_header_protocol;
            cam.l3_cam_layer3_header_protocol_mask0 = entry->l3_header_protocol_mask & 0xff;
            cam.l3_cam_layer3_type0 = entry->l3_type;
            cam.l3_cam_layer3_type_mask0 = entry->l3_type_mask & 0xf;
            cam.l3_cam_layer4_type0 = entry->l4_type;
            break;

        case 1:
            cam.l3_cam_additional_offset1 = entry->addition_offset;
            cam.l3_cam_layer3_header_protocol1 = entry->l3_header_protocol;
            cam.l3_cam_layer3_header_protocol_mask1 = entry->l3_header_protocol_mask & 0xff;
            cam.l3_cam_layer3_type1 = entry->l3_type;
            cam.l3_cam_layer3_type_mask1 = entry->l3_type_mask & 0xf;
            cam.l3_cam_layer4_type1 = entry->l4_type;
            break;

        case 2:
            cam.l3_cam_additional_offset2 = entry->addition_offset;
            cam.l3_cam_layer3_header_protocol2 = entry->l3_header_protocol;
            cam.l3_cam_layer3_header_protocol_mask2 = entry->l3_header_protocol_mask & 0xff;
            cam.l3_cam_layer3_type2 = entry->l3_type;
            cam.l3_cam_layer3_type_mask2 = entry->l3_type_mask & 0xf;
            cam.l3_cam_layer4_type2 = entry->l4_type;
            break;

        case 3:
            cam.l3_cam_additional_offset3 = entry->addition_offset;
            cam.l3_cam_layer3_header_protocol3 = entry->l3_header_protocol;
            cam.l3_cam_layer3_header_protocol_mask3 = entry->l3_header_protocol_mask & 0xff;
            cam.l3_cam_layer3_type3 = entry->l3_type;
            cam.l3_cam_layer3_type_mask3 = entry->l3_type_mask & 0xf;
            cam.l3_cam_layer4_type3 = entry->l4_type;
            break;

        default:
            return CTC_E_INVALID_PARAM;
            break;
        }

        cam_valid.layer3_cam_entry_valid |= (1 << index);

        CTC_ERROR_RETURN(sys_greatbelt_parser_io_set_parser_layer3_protocol_cam_entry(lchip, &cam));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer3_protocol_cam_valid_entry(lchip, &cam_valid));

    return CTC_E_NONE;
}

/**
 @brief set the entry invalid based on the index
*/
int32
sys_greatbelt_parser_unmapping_l4_type(uint8 lchip, uint8 index)
{
    parser_layer3_protocol_cam_valid_t cam_valid;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("index:%d\n", index);

    if (index > 3)
    {
        return CTC_E_INVALID_PARAM;
    }
        sal_memset(&cam_valid, 0, sizeof(parser_layer3_protocol_cam_valid_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_layer3_protocol_cam_valid_entry(lchip, &cam_valid));
        if (CTC_IS_BIT_SET(cam_valid.layer3_cam_entry_valid, index))
        {
            cam_valid.layer3_cam_entry_valid &= ~(1 << index);
            CTC_ERROR_RETURN(
                sys_greatbelt_parser_io_set_parser_layer3_protocol_cam_valid_entry(lchip, &cam_valid));
        }

    return CTC_E_NONE;
}

/**
 @brief set parser layer4 header ecmp hash info
*/
STATIC int32
_sys_greatbelt_parser_set_l4_ecmp_hash(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{
    parser_layer4_hash_ctl_t l4_ctl;

    CTC_PTR_VALID_CHECK(hash_ctl);

        sal_memset(&l4_ctl, 0, sizeof(parser_layer4_hash_ctl_t));

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_l4_hash_ctl_entry(lchip, &l4_ctl));

        l4_ctl.source_port_ecmp_hash_en = ((hash_ctl->l4_flag) & CTC_PARSER_L4_ECMP_HASH_FLAGS_SRC_PORT) ? 1 : 0;
        l4_ctl.dest_port_ecmp_hash_en = ((hash_ctl->l4_flag) & CTC_PARSER_L4_ECMP_HASH_FLAGS_DST_PORT) ? 1 : 0;
        l4_ctl.gre_key_ecmp_hash_en = ((hash_ctl->l4_flag) & CTC_PARSER_L4_ECMP_HASH_FLAGS_GRE_KEY) ? 1 : 0;

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_l4_hash_ctl_entry(lchip, &l4_ctl));

    return CTC_E_NONE;
}

/**
 @brief get parser layer4 header ecmp hash info
*/
STATIC int32
_sys_greatbelt_parser_get_l4_ecmp_hash(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{
    parser_layer4_hash_ctl_t l4_ctl;

    CTC_PTR_VALID_CHECK(hash_ctl);
    sal_memset(&l4_ctl, 0, sizeof(parser_layer4_hash_ctl_t));

    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_l4_hash_ctl_entry(lchip, &l4_ctl));

    hash_ctl->l4_flag = 0;

    if (l4_ctl.source_port_ecmp_hash_en)
    {
        CTC_SET_FLAG(hash_ctl->l4_flag, CTC_PARSER_L4_ECMP_HASH_FLAGS_SRC_PORT);
    }

    if (l4_ctl.dest_port_ecmp_hash_en)
    {
        CTC_SET_FLAG(hash_ctl->l4_flag, CTC_PARSER_L4_ECMP_HASH_FLAGS_DST_PORT);
    }

    if (l4_ctl.gre_key_ecmp_hash_en)
    {
        CTC_SET_FLAG(hash_ctl->l4_flag, CTC_PARSER_L4_ECMP_HASH_FLAGS_GRE_KEY);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_parser_set_layer4_flag_op_ctl(uint8 lchip, uint8 index, sys_parser_l4flag_op_ctl_t* l4flag_op_ctl)
{
    parser_layer4_flag_op_ctl_t ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("index:%d\n", index);
    SYS_PARSER_DBG_PARAM("op_and_or:%d\n", l4flag_op_ctl->op_and_or);
    SYS_PARSER_DBG_PARAM("flags_mask:%d\n", l4flag_op_ctl->flags_mask);

    CTC_PTR_VALID_CHECK(l4flag_op_ctl);

    sal_memset(&ctl, 0, sizeof(parser_layer4_flag_op_ctl_t));

    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_layer4_flag_op_ctl_entry(lchip, &ctl));

    switch (index)
    {

    case 0:
        ctl.op_and_or0 = l4flag_op_ctl->op_and_or ? 1 : 0;
        ctl.flags_mask0 = l4flag_op_ctl->flags_mask & 0x3f;
        break;

    case 1:
        ctl.op_and_or1 = l4flag_op_ctl->op_and_or ? 1 : 0;
        ctl.flags_mask1 = l4flag_op_ctl->flags_mask & 0x3f;
        break;

    case 2:
        ctl.op_and_or2 = l4flag_op_ctl->op_and_or ? 1 : 0;
        ctl.flags_mask2 = l4flag_op_ctl->flags_mask & 0x3f;
        break;

    case 3:
        ctl.op_and_or3 = l4flag_op_ctl->op_and_or ? 1 : 0;
        ctl.flags_mask3 = l4flag_op_ctl->flags_mask & 0x3f;
        break;

    default:
        return CTC_E_INVALID_PARAM;
        break;
    }

    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_set_parser_layer4_flag_op_ctl_entry(lchip, &ctl));

    return CTC_E_NONE;
}

int32
sys_greatbelt_parser_set_layer4_port_op_sel(uint8 lchip, sys_parser_l4_port_op_sel_t* l4port_op_sel)
{
    parser_layer4_port_op_sel_t ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("op_dest_port:%d\n", l4port_op_sel->op_dest_port);

    CTC_PTR_VALID_CHECK(l4port_op_sel);

    sal_memset(&ctl, 0, sizeof(parser_layer4_port_op_sel_t));

    ctl.op_dest_port = l4port_op_sel->op_dest_port;

    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_set_parser_layer4_port_op_sel_entry(lchip, &ctl));

    return CTC_E_NONE;

}

int32
sys_greatbelt_parser_set_layer4_port_op_ctl(uint8 lchip, uint8 index, sys_parser_l4_port_op_ctl_t* l4port_op_ctl)
{
    parser_layer4_port_op_ctl_t ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("index:%d\n", index);
    SYS_PARSER_DBG_PARAM("layer4_port_max:%d\n", l4port_op_ctl->layer4_port_max);
    SYS_PARSER_DBG_PARAM("layer4_port_min:%d\n", l4port_op_ctl->layer4_port_min);

    CTC_PTR_VALID_CHECK(l4port_op_ctl);

    sal_memset(&ctl, 0, sizeof(parser_layer4_port_op_ctl_t));

    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_layer4_port_op_ctl_entry(lchip, &ctl));

    switch (index)
    {

    case 0:
        ctl.port_max0 = l4port_op_ctl->layer4_port_max;
        ctl.port_min0 = l4port_op_ctl->layer4_port_min;
        break;

    case 1:
        ctl.port_max1 = l4port_op_ctl->layer4_port_max;
        ctl.port_min1 = l4port_op_ctl->layer4_port_min;
        break;

    case 2:
        ctl.port_max2 = l4port_op_ctl->layer4_port_max;
        ctl.port_min2 = l4port_op_ctl->layer4_port_min;
        break;

    case 3:
        ctl.port_max3 = l4port_op_ctl->layer4_port_max;
        ctl.port_min3 = l4port_op_ctl->layer4_port_min;
        break;

    case 4:
        ctl.port_max4 = l4port_op_ctl->layer4_port_max;
        ctl.port_min4 = l4port_op_ctl->layer4_port_min;
        break;

    case 5:
        ctl.port_max5 = l4port_op_ctl->layer4_port_max;
        ctl.port_min5 = l4port_op_ctl->layer4_port_min;
        break;

    case 6:
        ctl.port_max6 = l4port_op_ctl->layer4_port_max;
        ctl.port_min6 = l4port_op_ctl->layer4_port_min;
        break;

    case 7:
        ctl.port_max7 = l4port_op_ctl->layer4_port_max;
        ctl.port_min7 = l4port_op_ctl->layer4_port_min;
        break;

    default:
        return CTC_E_INVALID_PARAM;
        break;
    }

    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_set_parser_layer4_port_op_ctl_entry(lchip, &ctl));

    return CTC_E_NONE;
}

/**
 @brief set parser l4flex header info
*/
int32
sys_greatbelt_parser_set_l4flex_header(uint8 lchip, ctc_parser_l4flex_header_t* l4flex_header)
{
    parser_layer4_flex_ctl_t ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("dest_port_basic_offset:%d\n", l4flex_header->dest_port_basic_offset);
    SYS_PARSER_DBG_PARAM("l4_min_len:%d\n", l4flex_header->l4_min_len);

    CTC_PTR_VALID_CHECK(l4flex_header);
        sal_memset(&ctl, 0, sizeof(parser_layer4_flex_ctl_t));

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_layer4_flex_ctl_entry(lchip, &ctl));

        if (l4flex_header->dest_port_basic_offset > 30)
        {
            return CTC_E_INVALID_PARAM;
        }

        ctl.layer4_byte_select0 = l4flex_header->dest_port_basic_offset;
        ctl.layer4_byte_select1 = l4flex_header->dest_port_basic_offset + 1;

        if (l4flex_header->l4_min_len > 0x1f)
        {
            return CTC_E_INVALID_PARAM;
        }

        ctl.layer4_min_length = l4flex_header->l4_min_len;

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer4_flex_ctl_entry(lchip, &ctl));

    return CTC_E_NONE;
}

/**
 @brief get parser l4flex header info
*/
int32
sys_greatbelt_parser_get_l4flex_header(uint8 lchip, ctc_parser_l4flex_header_t* l4flex_header)
{
    parser_layer4_flex_ctl_t ctl;

    CTC_PTR_VALID_CHECK(l4flex_header);

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    sal_memset(&ctl, 0, sizeof(parser_layer4_flex_ctl_t));

    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_layer4_flex_ctl_entry(lchip, &ctl));

    l4flex_header->dest_port_basic_offset = ctl.layer4_byte_select0;
    l4flex_header->l4_min_len = ctl.layer4_min_length;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_INFO("dest_port_basic_offset:%d\n", l4flex_header->dest_port_basic_offset);
    SYS_PARSER_DBG_INFO("l4_min_len:%d\n", l4flex_header->l4_min_len);

    return CTC_E_NONE;
}

/**
 @brief set parser layer4 app control info
*/
int32
sys_greatbelt_parser_set_l4app_ctl(uint8 lchip, ctc_parser_l4app_ctl_t* l4app_ctl)
{
    parser_layer4_app_ctl_t ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("ptp_en:%d\n", l4app_ctl->ptp_en);
    SYS_PARSER_DBG_PARAM("ntp_en:%d\n", l4app_ctl->ntp_en);
    SYS_PARSER_DBG_PARAM("bfd_en:%d\n", l4app_ctl->bfd_en);

    CTC_PTR_VALID_CHECK(l4app_ctl);
        sal_memset(&ctl, 0, sizeof(parser_layer4_app_ctl_t));

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_layer4_app_ctl_entry(lchip, &ctl));

        ctl.ptp_en = l4app_ctl->ptp_en ? 1 : 0;
        ctl.ntp_en = l4app_ctl->ntp_en ? 1 : 0;
        ctl.bfd_en = l4app_ctl->bfd_en ? 1 : 0;

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer4_app_ctl_entry(lchip, &ctl));

    return CTC_E_NONE;
}

/**
 @brief get parser layer4 app control info
*/
int32
sys_greatbelt_parser_get_l4app_ctl(uint8 lchip, ctc_parser_l4app_ctl_t* l4app_ctl)
{
    parser_layer4_app_ctl_t ctl;

    CTC_PTR_VALID_CHECK(l4app_ctl);

    sal_memset(&ctl, 0, sizeof(parser_layer4_app_ctl_t));

    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_layer4_app_ctl_entry(lchip, &ctl));

    l4app_ctl->ptp_en = ctl.ptp_en ? 1 : 0;
    l4app_ctl->ntp_en = ctl.ntp_en ? 1 : 0;
    l4app_ctl->bfd_en = ctl.bfd_en ? 1 : 0;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_INFO("ptp_en:%d\n", l4app_ctl->ptp_en);
    SYS_PARSER_DBG_INFO("ntp_en:%d\n", l4app_ctl->ntp_en);
    SYS_PARSER_DBG_INFO("bfd_en:%d\n", l4app_ctl->bfd_en);

    return CTC_E_NONE;
}

/**
 @brief set ecmp hash field
*/
int32
sys_greatbelt_parser_set_ecmp_hash_field(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{
    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("hash_type_bitmap:%d\n", hash_ctl->hash_type_bitmap);

    CTC_PTR_VALID_CHECK(hash_ctl);

    if (hash_ctl->hash_type_bitmap & CTC_PARSER_ECMP_HASH_TYPE_FLAGS_IP)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_parser_set_ip_ecmp_hash(lchip, hash_ctl));
    }

    if (hash_ctl->hash_type_bitmap & CTC_PARSER_ECMP_HASH_TYPE_FLAGS_L4)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_parser_set_l4_ecmp_hash(lchip, hash_ctl));
    }

    if (hash_ctl->hash_type_bitmap & CTC_PARSER_ECMP_HASH_TYPE_FLAGS_MPLS)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_parser_set_mpls_ecmp_hash(lchip, hash_ctl));
    }

    if (hash_ctl->hash_type_bitmap & CTC_PARSER_ECMP_HASH_TYPE_FLAGS_FCOE)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_parser_set_fcoe_ecmp_hash(lchip, hash_ctl));
    }

    if (hash_ctl->hash_type_bitmap & CTC_PARSER_ECMP_HASH_TYPE_FLAGS_TRILL)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_parser_set_trill_ecmp_hash(lchip, hash_ctl));
    }

    if (hash_ctl->hash_type_bitmap & CTC_PARSER_ECMP_HASH_TYPE_FLAGS_L2)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_parser_set_l2_ecmp_hash(lchip, hash_ctl));
    }

    return CTC_E_NONE;
}

/**
 @brief get ecmp hash field
*/
int32
sys_greatbelt_parser_get_ecmp_hash_field(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{

    CTC_PTR_VALID_CHECK(hash_ctl);

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    if (hash_ctl->hash_type_bitmap & CTC_PARSER_ECMP_HASH_TYPE_FLAGS_IP)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_parser_get_ip_ecmp_hash(lchip, hash_ctl));
    }

    if (hash_ctl->hash_type_bitmap & CTC_PARSER_ECMP_HASH_TYPE_FLAGS_L4)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_parser_get_l4_ecmp_hash(lchip, hash_ctl));
    }

    if (hash_ctl->hash_type_bitmap & CTC_PARSER_ECMP_HASH_TYPE_FLAGS_MPLS)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_parser_get_mpls_ecmp_hash(lchip, hash_ctl));
    }

    if (hash_ctl->hash_type_bitmap & CTC_PARSER_ECMP_HASH_TYPE_FLAGS_FCOE)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_parser_get_fcoe_ecmp_hash(lchip, hash_ctl));
    }

    if (hash_ctl->hash_type_bitmap & CTC_PARSER_ECMP_HASH_TYPE_FLAGS_TRILL)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_parser_get_trill_ecmp_hash(lchip, hash_ctl));
    }

    if (hash_ctl->hash_type_bitmap & CTC_PARSER_ECMP_HASH_TYPE_FLAGS_L2)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_parser_get_l2_ecmp_hash(lchip, hash_ctl));
    }

    return CTC_E_NONE;
}

/**
 @brief set parser global config info
*/
int32
sys_greatbelt_parser_set_global_cfg(uint8 lchip, ctc_parser_global_cfg_t* global_cfg)
{
    parser_ethernet_ctl_t eth_ctl;
    parser_layer3_hash_ctl_t l3hash_ctl;
    parser_layer4_hash_ctl_t l4hash_ctl;
    parser_ip_hash_ctl_t ip_ctl;

    CTC_PTR_VALID_CHECK(global_cfg);

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("ecmp_hash_type:%d\n", global_cfg->ecmp_hash_type);
    SYS_PARSER_DBG_PARAM("linkagg_hash_type:%d\n", global_cfg->linkagg_hash_type);
    SYS_PARSER_DBG_PARAM("small_frag_offset:%d\n", global_cfg->small_frag_offset);

        sal_memset(&eth_ctl, 0, sizeof(parser_ethernet_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_ethernet_ctl_entry(lchip, &eth_ctl));

        sal_memset(&l3hash_ctl, 0, sizeof(parser_layer3_hash_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_l3_hash_ctl_entry(lchip, &l3hash_ctl));

        sal_memset(&l4hash_ctl, 0, sizeof(parser_layer4_hash_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_l4_hash_ctl_entry(lchip, &l4hash_ctl));

        sal_memset(&ip_ctl, 0, sizeof(parser_ip_hash_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_ip_hash_ctl_entry(lchip, &ip_ctl));

        /* use xor hash algorithm, if set, use crc */
        eth_ctl.mac_ecmp_hash_type = global_cfg->ecmp_hash_type ? 0 : 1;
        l3hash_ctl.layer3_ecmp_hash_type = global_cfg->ecmp_hash_type ? 0 : 1;
        l4hash_ctl.layer4_ecmp_hash_type = global_cfg->ecmp_hash_type ? 0 : 1;

        eth_ctl.mac_link_agg_hash_type = global_cfg->linkagg_hash_type ? 0 : 1;
        l3hash_ctl.layer3_link_agg_hash_type = global_cfg->linkagg_hash_type ? 0 : 1;
        l4hash_ctl.layer4_link_agg_hash_type = global_cfg->linkagg_hash_type ? 0 : 1;

        /* ipv4 small fragment offset, 0~3,means 0,8,16,24 bytes of small fragment length*/
        if (global_cfg->small_frag_offset > 3)
        {
            return CTC_E_INVALID_PARAM;
        }

        ip_ctl.small_fragment_offset = global_cfg->small_frag_offset;

        /* parser_ip_hash_ctl.ip_ip_hash_disable is enable */

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_ethernet_ctl_entry(lchip, &eth_ctl));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_l3_hash_ctl_entry(lchip, &l3hash_ctl));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_l4_hash_ctl_entry(lchip, &l4hash_ctl));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_ip_hash_ctl_entry(lchip, &ip_ctl));

    return CTC_E_NONE;
}

/**
 @brief get parser global config info
*/
int32
sys_greatbelt_parser_get_global_cfg(uint8 lchip, ctc_parser_global_cfg_t* global_cfg)
{
    parser_ethernet_ctl_t eth_ctl;
    parser_ip_hash_ctl_t ip_ctl;

    CTC_PTR_VALID_CHECK(global_cfg);
        sal_memset(&eth_ctl, 0, sizeof(parser_ethernet_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_ethernet_ctl_entry(lchip, &eth_ctl));

        sal_memset(&ip_ctl, 0, sizeof(parser_ip_hash_ctl_t));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_ip_hash_ctl_entry(lchip, &ip_ctl));

        global_cfg->ecmp_hash_type = eth_ctl.mac_ecmp_hash_type ? 0 : 1;
        global_cfg->linkagg_hash_type = eth_ctl.mac_link_agg_hash_type ? 0 : 1;
        global_cfg->small_frag_offset = ip_ctl.small_fragment_offset;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_INFO("ecmp_hash_type:%d\n", global_cfg->ecmp_hash_type);
    SYS_PARSER_DBG_INFO("linkagg_hash_type:%d\n", global_cfg->linkagg_hash_type);
    SYS_PARSER_DBG_INFO("small_frag_offset:%d\n", global_cfg->small_frag_offset);

    return CTC_E_NONE;
}

/**
 @brief set layer4 app data control info
*/
int32
sys_greatbelt_parser_set_l4_app_data_ctl(uint8 lchip, uint8 index, ctc_parser_l4_app_data_entry_t* entry)
{
    parser_layer4_app_data_ctl_t ctl;

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_PARAM("index:%d\n", index);
    SYS_PARSER_DBG_PARAM("l4_dst_port_value:%d\n", entry->l4_dst_port_value);
    SYS_PARSER_DBG_PARAM("is_tcp_value:%d\n", entry->is_tcp_value);
    SYS_PARSER_DBG_PARAM("is_udp_value:%d\n", entry->is_udp_value);

    /*the max entry num is 4*/
    if (index >= SYS_PAS_MAX_L4_APP_DATA_CTL_ENTRY_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_PTR_VALID_CHECK(entry);
    if (entry->is_tcp_mask > 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (entry->is_tcp_value > 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (entry->is_udp_mask > 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (entry->is_udp_value > 1)
    {
        return CTC_E_INVALID_PARAM;
    }
        sal_memset(&ctl, 0, sizeof(parser_layer4_app_data_ctl_t));

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_get_parser_layer4_app_data_ctl_entry(lchip, &ctl));

        switch (index)
        {
        case 0:
            ctl.l4_dest_port_mask0 = entry->l4_dst_port_mask;
            ctl.l4_dest_port_value0 = entry->l4_dst_port_value;
            ctl.is_tcp_mask0 = entry->is_tcp_mask;
            ctl.is_tcp_value0 = entry->is_tcp_value;
            ctl.is_udp_mask0 = entry->is_udp_mask;
            ctl.is_udp_value0 = entry->is_udp_value;
            break;

        case 1:
            ctl.l4_dest_port_mask1 = entry->l4_dst_port_mask;
            ctl.l4_dest_port_value1 = entry->l4_dst_port_value;
            ctl.is_tcp_mask1 = entry->is_tcp_mask;
            ctl.is_tcp_value1 = entry->is_tcp_value;
            ctl.is_udp_mask1 = entry->is_udp_mask;
            ctl.is_udp_value1 = entry->is_udp_value;
            break;

        case 2:
            ctl.l4_dest_port_mask2 = entry->l4_dst_port_mask;
            ctl.l4_dest_port_value2 = entry->l4_dst_port_value;
            ctl.is_tcp_mask2 = entry->is_tcp_mask;
            ctl.is_tcp_value2 = entry->is_tcp_value;
            ctl.is_udp_mask2 = entry->is_udp_mask;
            ctl.is_udp_value2 = entry->is_udp_value;
            break;

        case 3:
            ctl.l4_dest_port_mask3 = entry->l4_dst_port_mask;
            ctl.l4_dest_port_value3 = entry->l4_dst_port_value;
            ctl.is_tcp_mask3 = entry->is_tcp_mask;
            ctl.is_tcp_value3 = entry->is_tcp_value;
            ctl.is_udp_mask3 = entry->is_udp_mask;
            ctl.is_udp_value3 = entry->is_udp_value;
            break;

        default:
            return CTC_E_INVALID_PARAM;
            break;
        }

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer4_app_data_ctl_entry(lchip, &ctl));
    return CTC_E_NONE;
}

/**
 @brief get layer4 app data control info
*/
int32
sys_greatbelt_parser_get_l4_app_data_ctl(uint8 lchip, uint8 index, ctc_parser_l4_app_data_entry_t* entry)
{
    parser_layer4_app_data_ctl_t ctl;

    CTC_PTR_VALID_CHECK(entry);

    sal_memset(&ctl, 0, sizeof(parser_layer4_app_data_ctl_t));

    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_layer4_app_data_ctl_entry(lchip, &ctl));

    switch (index)
    {
    case 0:
        entry->l4_dst_port_mask = ctl.l4_dest_port_mask0;
        entry->l4_dst_port_value = ctl.l4_dest_port_value0;
        entry->is_tcp_mask = ctl.is_tcp_mask0;
        entry->is_tcp_value = ctl.is_tcp_value0;
        entry->is_udp_mask = ctl.is_udp_mask0;
        entry->is_udp_value = ctl.is_udp_value0;
        break;

    case 1:
        entry->l4_dst_port_mask = ctl.l4_dest_port_mask1;
        entry->l4_dst_port_value = ctl.l4_dest_port_value1;
        entry->is_tcp_mask = ctl.is_tcp_mask1;
        entry->is_tcp_value = ctl.is_tcp_value1;
        entry->is_udp_mask = ctl.is_udp_mask1;
        entry->is_udp_value = ctl.is_udp_value1;
        break;

    case 2:
        entry->l4_dst_port_mask = ctl.l4_dest_port_mask2;
        entry->l4_dst_port_value = ctl.l4_dest_port_value2;
        entry->is_tcp_mask = ctl.is_tcp_mask2;
        entry->is_tcp_value = ctl.is_tcp_value2;
        entry->is_udp_mask = ctl.is_udp_mask2;
        entry->is_udp_value = ctl.is_udp_value2;
        break;

    case 3:
        entry->l4_dst_port_mask = ctl.l4_dest_port_mask3;
        entry->l4_dst_port_value = ctl.l4_dest_port_value3;
        entry->is_tcp_mask = ctl.is_tcp_mask3;
        entry->is_tcp_value = ctl.is_tcp_value3;
        entry->is_udp_mask = ctl.is_udp_mask3;
        entry->is_udp_value = ctl.is_udp_value3;
        break;

    default:
        return CTC_E_INVALID_PARAM;
        break;
    }

    SYS_PARSER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PARSER_DBG_INFO("index:%d\n", index);
    SYS_PARSER_DBG_INFO("l4_dst_port_value:%d\n", entry->l4_dst_port_value);
    SYS_PARSER_DBG_INFO("is_tcp_value:%d\n", entry->is_tcp_value);
    SYS_PARSER_DBG_INFO("is_udp_value:%d\n", entry->is_udp_value);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_parser_ethernet_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint8 gchip = 0;

    parser_ethernet_ctl_t parser_ethernet_ctl;
    epe_hdr_adjust_ctl_t epe_hdr_adjust_ctl;
    epe_l2_tpid_ctl_t epe_l2_tpid_ctl;

    sal_memset(&parser_ethernet_ctl, 0, sizeof(parser_ethernet_ctl_t));
    parser_ethernet_ctl.cvlan_tpid                     = 0x8100;
    parser_ethernet_ctl.vlan_hash_en                   = 0;
    parser_ethernet_ctl.cos_hash_en                    = 0;
    parser_ethernet_ctl.layer2_header_protocol_hash_en = 0;
    parser_ethernet_ctl.allow_non_zero_oui             = 1;
    parser_ethernet_ctl.port_hash_en                   = 0;
    parser_ethernet_ctl.vlan_hash_mode                 = 0;
    parser_ethernet_ctl.cos_ecmp_hash_en               = 0;
    parser_ethernet_ctl.mac_hash_disable               = 0xF;
    parser_ethernet_ctl.ieee1588_type_en               = 1;
    parser_ethernet_ctl.slow_protocol_type_en          = 1;
    parser_ethernet_ctl.pbb_type_en                    = 1;
    parser_ethernet_ctl.eth_oam_type_en                = 1;
    parser_ethernet_ctl.fcoe_type_en                   = 1;
    parser_ethernet_ctl.arp_type_en                    = 1;
    parser_ethernet_ctl.mpls_mcast_type_en             = 1;
    parser_ethernet_ctl.mpls_type_en                   = 1;
    parser_ethernet_ctl.ipv4_type_en                   = 1;
    parser_ethernet_ctl.ipv6_type_en                   = 1;
    parser_ethernet_ctl.max_length_field               = 1536;
    parser_ethernet_ctl.parsing_quad_vlan              = 1;
    parser_ethernet_ctl.vlan_parsing_num               = 2;
    parser_ethernet_ctl.svlan_tpid0                    = 0x8100;
    parser_ethernet_ctl.svlan_tpid1                    = 0x9100;
    parser_ethernet_ctl.svlan_tpid2                    = 0x88A8;
    parser_ethernet_ctl.svlan_tpid3                    = 0x88A8;
    parser_ethernet_ctl.trill_type_en                  = 1;

    sal_memset(&epe_hdr_adjust_ctl, 0, sizeof(epe_hdr_adjust_ctl_t));
    epe_hdr_adjust_ctl.vlan_edit_en              = 1;
    epe_hdr_adjust_ctl.cvlan_tag_tpid            = 0x8100;
    epe_hdr_adjust_ctl.svlan_tag_tpid0           = 0x8100;
    epe_hdr_adjust_ctl.svlan_tag_tpid1           = 0x9100;
    epe_hdr_adjust_ctl.svlan_tag_tpid2           = 0x88A8;
    epe_hdr_adjust_ctl.svlan_tag_tpid3           = 0x88A8;
    epe_hdr_adjust_ctl.port_extender_mcast_en    = 0;
    epe_hdr_adjust_ctl.packet_header_bypass_all  = 0;
    epe_hdr_adjust_ctl.packet_header_en31_0      = 0;
    epe_hdr_adjust_ctl.packet_header_en63_32     = 0;
    epe_hdr_adjust_ctl.ds_next_hop_internal_base = SYS_GREATBELT_DSNH_INTERNAL_BASE;

    sal_memset(&epe_l2_tpid_ctl, 0, sizeof(epe_l2_tpid_ctl_t));
    epe_l2_tpid_ctl.svlan_tpid0 = 0x8100;
    epe_l2_tpid_ctl.svlan_tpid1 = 0x9100;
    epe_l2_tpid_ctl.svlan_tpid2 = 0x88A8;
    epe_l2_tpid_ctl.svlan_tpid3 = 0x88a8;
    epe_l2_tpid_ctl.i_tag_tpid  = 0x88e7;
    epe_l2_tpid_ctl.cvlan_tpid  = 0x8100;
    epe_l2_tpid_ctl.bvlan_tpid  = 0x88a8;

        CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
        parser_ethernet_ctl.chip_id = gchip;

        cmd = DRV_IOW(ParserEthernetCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &parser_ethernet_ctl));

        cmd = DRV_IOW(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl));

        cmd = DRV_IOW(EpeL2TpidCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_l2_tpid_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_parser_l2pro_cam_init(uint8 lchip)
{
    parser_layer2_protocol_cam_t cam;
    parser_layer2_protocol_cam_valid_t valid;

    sal_memset(&cam, 0, sizeof(parser_layer2_protocol_cam_t));
    sal_memset(&valid, 0, sizeof(parser_layer2_protocol_cam_valid_t));

    /*
       ipv4, ipv6, mpls, mpls_mcast, arp, fcoe, trill, eth_oam, pbb, slow_protocol, 1588(ptp)
       defined in _cm_com_parser_l2_protocol_lookup function
       parser_layer2_protocol_cam only used to mapping 4 user defined layer3 protocol
    */

    valid.layer2_cam_entry_valid = 0x0;

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer2_protocol_cam_valid_entry(lchip, &valid));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer2_protocol_cam_entry(lchip, &cam));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_parser_pbb_ctl_init(uint8 lchip)
{

    parser_pbb_ctl_t ctl;

    sal_memset(&ctl, 0, sizeof(parser_pbb_ctl_t));

    ctl.pbb_vlan_parsing_num = SYS_PAS_PBB_VLAN_PAS_NUM;
    ctl.c_mac_hash_disable = 0xF;

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_pbb_ctl_entry(lchip, &ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_parser_l2flex_ctl_init(uint8 lchip)
{

    parser_layer2_flex_ctl_t l2flex_ctl;

    sal_memset(&l2flex_ctl, 0, sizeof(parser_layer2_flex_ctl_t));


        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer2_flex_ctl_entry(lchip, &l2flex_ctl));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_parser_packet_type_map_init(uint8 lchip)
{

    parser_packet_type_map_t packet_type;

    sal_memset(&packet_type, 0, sizeof(parser_packet_type_map_t));

    /*
       ETHERNETV2, IPV4, MPLS, IPV6, MCAST_MPLS, TRILL
       defined in cm_com_parser_packet_parsing function
       parser_packet_type_map used to mapping 2 user defined type
    */

    packet_type.layer2_type0 = CTC_PARSER_L2_TYPE_NONE;
    packet_type.layer3_type0 = CTC_PARSER_L3_TYPE_IP;
    packet_type.layer2_type1 = CTC_PARSER_L2_TYPE_NONE;
    packet_type.layer3_type1 = CTC_PARSER_L3_TYPE_NONE;

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_packet_type_table_entry(lchip, &packet_type));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_parser_l3pro_cam_init(uint8 lchip)
{
    parser_layer3_protocol_cam_t cam;
    parser_layer3_protocol_cam_valid_t valid;

    sal_memset(&cam, 0, sizeof(parser_layer3_protocol_cam_t));
    sal_memset(&valid, 0, sizeof(parser_layer3_protocol_cam_valid_t));

    /*
      tcp, udp, gre, pbb_itag_oam, ipinip, v6inip, ipinv6, v6inv6, icmp, igmp, rdp , sctp, dccp
      defined in _cm_com_parser_l3_protocol_lookup function
      parser_layer3_protocol_cam only used to mapping 4 user defined layer4 protocol
   */

    valid.layer3_cam_entry_valid = 0x0;

        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer3_protocol_cam_valid_entry(lchip, &valid));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer3_protocol_cam_entry(lchip, &cam));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_parser_ip_hash_ctl_init(uint8 lchip)
{

    parser_ip_hash_ctl_t ip_hash_ctl;

    sal_memset(&ip_hash_ctl, 0, sizeof(parser_ip_hash_ctl_t));


        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_ip_hash_ctl_entry(lchip, &ip_hash_ctl));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_parser_l3_hash_ctl_init(uint8 lchip)
{

    parser_layer3_hash_ctl_t l3_hash_ctl;

    sal_memset(&l3_hash_ctl, 0, sizeof(parser_layer3_hash_ctl_t));


        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_l3_hash_ctl_entry(lchip, &l3_hash_ctl));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_parser_mpls_ctl_init(uint8 lchip)
{

    parser_mpls_ctl_t mpls_ctl;

    sal_memset(&mpls_ctl, 0, sizeof(parser_mpls_ctl_t));

    mpls_ctl.max_reserve_label = 0xF;
    mpls_ctl.mpls_ecmp_use_reserve_label = 1;


        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_mpls_ctl_entry(lchip, &mpls_ctl));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_parser_l3pro_ctl_init(uint8 lchip)
{
    parser_layer3_protocol_ctl_t l3_protocol_ctl;

    sal_memset(&l3_protocol_ctl, 0, sizeof(parser_layer3_protocol_ctl_t));

    l3_protocol_ctl.pbb_itag_oam_type_en = 1;
    l3_protocol_ctl.v6inv6_type_en       = 1;
    l3_protocol_ctl.ipinv6_type_en       = 1;
    l3_protocol_ctl.v6inip_type_en       = 1;
    l3_protocol_ctl.ipinip_type_en       = 1;
    l3_protocol_ctl.igmp_type_en         = 1;
    l3_protocol_ctl.icmp_type_en         = 1;
    l3_protocol_ctl.gre_type_en          = 1;
    l3_protocol_ctl.udp_type_en          = 1;
    l3_protocol_ctl.tcp_type_en          = 1;
    l3_protocol_ctl.rdp_type_en          = 1;


        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer3_protocol_ctl_entry(lchip, &l3_protocol_ctl));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_parser_l3flex_ctl_init(uint8 lchip)
{

    parser_layer3_flex_ctl_t l3flex_ctl;

    sal_memset(&l3flex_ctl, 0, sizeof(parser_layer3_flex_ctl_t));


        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer3_flex_ctl_entry(lchip, &l3flex_ctl));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_parser_l4_reg_ctl_init(uint8 lchip)
{
    parser_layer4_port_op_sel_t port_opsel;
    parser_layer4_port_op_ctl_t port_opctl;
    parser_layer4_flag_op_ctl_t l4flag_opctl;
    parser_layer4_flex_ctl_t l4flex_ctl;
    parser_layer4_app_ctl_t l4app_ctl;
    parser_layer4_hash_ctl_t l4hash_ctl;
    parser_layer4_app_data_ctl_t app_data_ctl;
    parser_layer4_ach_ctl_t l4ach_ctl;

    sal_memset(&l4hash_ctl, 0, sizeof(parser_layer4_hash_ctl_t));
    sal_memset(&port_opsel, 0, sizeof(parser_layer4_port_op_sel_t));
    sal_memset(&port_opctl, 0, sizeof(parser_layer4_port_op_ctl_t));
    sal_memset(&l4flag_opctl, 0, sizeof(parser_layer4_flag_op_ctl_t));
    sal_memset(&l4flex_ctl, 0, sizeof(parser_layer4_flex_ctl_t));
    sal_memset(&l4app_ctl, 0, sizeof(parser_layer4_app_ctl_t));
    sal_memset(&app_data_ctl, 0, sizeof(parser_layer4_app_data_ctl_t));
    sal_memset(&l4ach_ctl, 0, sizeof(parser_layer4_ach_ctl_t));

    port_opsel.op_dest_port = 0xf;

    port_opctl.port_max0 = 0x4;
    port_opctl.port_min0 = 0x1;
    port_opctl.port_max1 = 0x8;
    port_opctl.port_min1 = 0x2;
    port_opctl.port_max2 = 0x10;
    port_opctl.port_min2 = 0x4;
    port_opctl.port_max3 = 0x20;
    port_opctl.port_min3 = 0x8;
    port_opctl.port_max4 = 0x40;
    port_opctl.port_min4 = 0x10;
    port_opctl.port_max5 = 0x80;
    port_opctl.port_min5 = 0x20;
    port_opctl.port_max6 = 0x100;
    port_opctl.port_min6 = 0x40;
    port_opctl.port_max7 = 0x200;
    port_opctl.port_min7 = 0x80;

    l4flag_opctl.op_and_or0 = 0;
    l4flag_opctl.flags_mask0 = 0;
    l4flag_opctl.op_and_or1 = 0;
    l4flag_opctl.flags_mask1 = 0x3f;
    l4flag_opctl.op_and_or2 = 1;
    l4flag_opctl.flags_mask2 = 0;
    l4flag_opctl.op_and_or3 = 1;
    l4flag_opctl.flags_mask3 = 0x3f;

    l4flex_ctl.layer4_byte_select0 = 2;
    l4flex_ctl.layer4_byte_select1 = 3;

    l4app_ctl.ptp_en = 1;
    l4app_ctl.ntp_en = 1;
    l4app_ctl.bfd_en = 1;
    l4app_ctl.ntp_port = 123;
    l4app_ctl.ptp_port0 = 320;
    l4app_ctl.ptp_port1 = 319;
    l4app_ctl.bfd_port0 = 3784;
    l4app_ctl.bfd_port1 = 4784;
    l4app_ctl.capwap_port0 = 5247;
    l4app_ctl.capwap_port1 = 5247;
    l4app_ctl.state_bits = 0;

    app_data_ctl.is_tcp_mask0       = 0x1;
    app_data_ctl.is_tcp_mask1       = 0x1;
    app_data_ctl.is_tcp_mask2       = 0x1;
    app_data_ctl.is_tcp_mask3       = 0x1;
    app_data_ctl.is_udp_mask0       = 0x1;
    app_data_ctl.is_udp_mask1       = 0x1;
    app_data_ctl.is_udp_mask2       = 0x1;
    app_data_ctl.is_udp_mask3       = 0x1;
    app_data_ctl.l4_dest_port_mask0 = 0xFFFF;
    app_data_ctl.l4_dest_port_mask1 = 0xFFFF;
    app_data_ctl.l4_dest_port_mask2 = 0xFFFF;
    app_data_ctl.l4_dest_port_mask3 = 0xFFFF;

    l4ach_ctl.ach_y1731_type = 0x8902;
    l4ach_ctl.ach_dm_type    = 0x000C;
    l4ach_ctl.ach_dlm_type   = 0x000A;


        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer4_port_op_sel_entry(lchip, &port_opsel));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer4_port_op_ctl_entry(lchip, &port_opctl));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer4_flag_op_ctl_entry(lchip, &l4flag_opctl));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer4_flex_ctl_entry(lchip, &l4flex_ctl));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer4_app_ctl_entry(lchip, &l4app_ctl));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_l4_hash_ctl_entry(lchip, &l4hash_ctl));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer4_app_data_ctl_entry(lchip, &app_data_ctl));
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_layer4_ach_ctl_entry(lchip, &l4ach_ctl));


    return CTC_E_NONE;
}


#define __PARSE_INTERNAL__
int32
sys_greatbelt_parser_packet_type_ip(uint8 lchip, uint8 en)
{
    parser_packet_type_map_t packet_type;
    sal_memset(&packet_type, 0, sizeof(parser_packet_type_map_t));

    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_ERROR_RETURN(
        sys_greatbelt_parser_io_get_parser_packet_type_table_entry(lchip, &packet_type));
    /*
       ETHERNETV2, IPV4, MPLS, IPV6, MCAST_MPLS, TRILL
       defined in cm_com_parser_packet_parsing function
       parser_packet_type_map used to mapping 2 user defined type
    */
    if(en)
    {
        packet_type.layer3_type1 = CTC_PARSER_L3_TYPE_IP;
    }
    else
    {
        packet_type.layer3_type1 = CTC_PARSER_L3_TYPE_NONE;
    }
        CTC_ERROR_RETURN(
            sys_greatbelt_parser_io_set_parser_packet_type_table_entry(lchip, &packet_type));
    return CTC_E_NONE;
}

/**
 @brief init parser module
*/
int32
sys_greatbelt_parser_init(uint8 lchip, void* parser_global_cfg)
{
    parser_ip_hash_ctl_t ip_ctl;
    sal_memset(&ip_ctl, 0, sizeof(parser_ip_hash_ctl_t));

    CTC_PTR_VALID_CHECK(parser_global_cfg);

    CTC_ERROR_RETURN(
        _sys_greatbelt_parser_ethernet_ctl_init(lchip));

    CTC_ERROR_RETURN(
        _sys_greatbelt_parser_l2pro_cam_init(lchip));

    CTC_ERROR_RETURN(
        _sys_greatbelt_parser_pbb_ctl_init(lchip));

    CTC_ERROR_RETURN(
        _sys_greatbelt_parser_l2flex_ctl_init(lchip));

    CTC_ERROR_RETURN(
        _sys_greatbelt_parser_packet_type_map_init(lchip));

    CTC_ERROR_RETURN(
        _sys_greatbelt_parser_l3pro_cam_init(lchip));

    CTC_ERROR_RETURN(
        _sys_greatbelt_parser_ip_hash_ctl_init(lchip));

    CTC_ERROR_RETURN(
        _sys_greatbelt_parser_l3_hash_ctl_init(lchip));

    CTC_ERROR_RETURN(
        _sys_greatbelt_parser_mpls_ctl_init(lchip));

    CTC_ERROR_RETURN(
        _sys_greatbelt_parser_l3pro_ctl_init(lchip));

    CTC_ERROR_RETURN(
        _sys_greatbelt_parser_l3flex_ctl_init(lchip));

    CTC_ERROR_RETURN(
        _sys_greatbelt_parser_l4_reg_ctl_init(lchip));

    CTC_ERROR_RETURN(
        sys_greatbelt_parser_set_global_cfg(lchip, parser_global_cfg));


    CTC_ERROR_RETURN(
    sys_greatbelt_parser_io_get_parser_ip_hash_ctl_entry(lchip, &ip_ctl));
    ip_ctl.use_ip_hash = TRUE;

    CTC_ERROR_RETURN(
    sys_greatbelt_parser_io_set_parser_ip_hash_ctl_entry(lchip, &ip_ctl));



    return CTC_E_NONE;

}

int32
sys_greatbelt_parser_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    return CTC_E_NONE;
}

