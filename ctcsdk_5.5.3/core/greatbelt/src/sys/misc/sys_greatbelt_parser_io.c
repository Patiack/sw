#include "ctc_error.h"
#include "sys_greatbelt_parser.h"
#include "sys_greatbelt_parser_io.h"
#include "greatbelt/include/drv_io.h"

extern int32
sys_greatbelt_packet_set_svlan_tpid_index(uint8 lchip, uint16 svlan_tpid, uint8 svlan_tpid_index);
/**
@brief The function is to set PARSER_PBB_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_pbb_ctl_entry(uint8 lchip, parser_pbb_ctl_t* parser_pbb_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserPbbCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_pbb_ctl));
    return CTC_E_NONE;

}

/**
@brief The function is to get PARSER_PBB_CTL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_pbb_ctl_entry(uint8 lchip, parser_pbb_ctl_t* parser_pbb_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserPbbCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_pbb_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_PACKET_TYPE_TABLE whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_packet_type_table_entry(uint8 lchip, parser_packet_type_map_t* parser_packet_type_table)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserPacketTypeMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_packet_type_table));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_PACKET_TYPE_TABLE whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_packet_type_table_entry(uint8 lchip, parser_packet_type_map_t* parser_packet_type_table)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserPacketTypeMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_packet_type_table));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_ETHERNET_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_ethernet_ctl_entry(uint8 lchip, parser_ethernet_ctl_t* parser_ethernet_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserEthernetCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_ethernet_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_ETHERNET_CTL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_ethernet_ctl_entry(uint8 lchip, parser_ethernet_ctl_t* parser_ethernet_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserEthernetCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_ethernet_ctl));

    return CTC_E_NONE;
}

/**
@brief The function is to set cvlan_tpid
*/
int32
sys_greatbelt_parser_io_set_cvlan_tpid(uint8 lchip, uint32 cvlan_tpid)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserEthernetCtl_t, ParserEthernetCtl_CvlanTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cvlan_tpid));

    cmd = DRV_IOW(EpeL2TpidCtl_t, EpeL2TpidCtl_CvlanTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cvlan_tpid));

    cmd = DRV_IOW(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_CvlanTagTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cvlan_tpid));

    cmd = DRV_IOW(OamParserEtherCtl_t, OamParserEtherCtl_CvlanTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cvlan_tpid));

    return CTC_E_NONE;
}

/**
@brief The function is to get cvlan_tpid
*/
int32
sys_greatbelt_parser_io_get_cvlan_tpid(uint8 lchip, uint32* cvlan_tpid)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserEthernetCtl_t, ParserEthernetCtl_CvlanTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, cvlan_tpid));

    return CTC_E_NONE;
}

/**
@brief The function is to set max_length_field
*/
int32
sys_greatbelt_parser_io_set_max_length_field(uint8 lchip, uint32 max_length_field)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserEthernetCtl_t, ParserEthernetCtl_MaxLengthField_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &max_length_field));
    return CTC_E_NONE;
}

/**
@brief The function is to get max_length_field
*/
int32
sys_greatbelt_parser_io_get_max_length_field(uint8 lchip, uint32* max_length_field)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserEthernetCtl_t, ParserEthernetCtl_MaxLengthField_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, max_length_field));
    return CTC_E_NONE;
}

/**
@brief The function is to set i_tag_tpid
*/
int32
sys_greatbelt_parser_io_set_i_tag_tpid(uint8 lchip, uint32 i_tag_tpid)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(EpeL2TpidCtl_t, EpeL2TpidCtl_ITagTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &i_tag_tpid));
    return CTC_E_NONE;
}

/**
@brief The function is to get i_tag_tpid
*/
int32
sys_greatbelt_parser_io_get_i_tag_tpid(uint8 lchip, uint32* i_tag_tpid)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(EpeL2TpidCtl_t, EpeL2TpidCtl_ITagTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, i_tag_tpid));

    return CTC_E_NONE;
}

/**
@brief The function is to set cn_tag_tpid
*/
int32
sys_greatbelt_parser_io_set_cn_tag_tpid(uint8 lchip, uint32 cn_tag_tpid)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserEthernetCtl_t, ParserEthernetCtl_CnTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cn_tag_tpid));
    return CTC_E_NONE;
}

/**
@brief The function is to get cn_tag_tpid
*/
int32
sys_greatbelt_parser_io_get_cn_tag_tpid(uint8 lchip, uint32* cn_tag_tpid)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserEthernetCtl_t, ParserEthernetCtl_CnTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, cn_tag_tpid));

    return CTC_E_NONE;
}

/**
@brief The function is to set bvlan_tpid
*/
int32
sys_greatbelt_parser_io_set_bvlan_tpid(uint8 lchip, uint32 bvlan_tpid)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(EpeL2TpidCtl_t, EpeL2TpidCtl_BvlanTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &bvlan_tpid));

    cmd = DRV_IOW(OamParserEtherCtl_t, OamParserEtherCtl_BvlanTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &bvlan_tpid));

    return CTC_E_NONE;
}

/**
@brief The function is to get bvlan_tpid
*/
int32
sys_greatbelt_parser_io_get_bvlan_tpid(uint8 lchip, uint32* bvlan_tpid)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(EpeL2TpidCtl_t, EpeL2TpidCtl_BvlanTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, bvlan_tpid));

    return CTC_E_NONE;
}

/**
@brief The function is to set vlan_parsing_num
*/
int32
sys_greatbelt_parser_io_set_vlan_parsing_num(uint8 lchip, uint32 vlan_parsing_num)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserEthernetCtl_t, ParserEthernetCtl_VlanParsingNum_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &vlan_parsing_num));
    return CTC_E_NONE;
}

/**
@brief The function is to get vlan_parsing_num
*/
int32
sys_greatbelt_parser_io_get_vlan_parsing_num(uint8 lchip, uint32* vlan_parsing_num)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserEthernetCtl_t, ParserEthernetCtl_VlanParsingNum_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, vlan_parsing_num));
    return CTC_E_NONE;
}

/**
@brief The function is to set svlan_tpid0
*/
int32
sys_greatbelt_parser_io_set_svlan_tpid0(uint8 lchip, uint32 svlan_tpid0)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserEthernetCtl_t, ParserEthernetCtl_SvlanTpid0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid0));

    cmd = DRV_IOW(EpeL2TpidCtl_t, EpeL2TpidCtl_SvlanTpid0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid0));

    cmd = DRV_IOW(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_SvlanTagTpid0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid0));

    cmd = DRV_IOW(OamParserEtherCtl_t, OamParserEtherCtl_SvlanTpid0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid0));

    CTC_ERROR_RETURN(sys_greatbelt_packet_set_svlan_tpid_index(lchip, svlan_tpid0, 0));

    return CTC_E_NONE;
}

/**
@brief The function is to get svlan_tpid0
*/
int32
sys_greatbelt_parser_io_get_svlan_tpid0(uint8 lchip, uint32* svlan_tpid0)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserEthernetCtl_t, ParserEthernetCtl_SvlanTpid0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, svlan_tpid0));
    return CTC_E_NONE;
}

/**
@brief The function is to set svlan_tpid1
*/
int32
sys_greatbelt_parser_io_set_svlan_tpid1(uint8 lchip, uint32 svlan_tpid1)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserEthernetCtl_t, ParserEthernetCtl_SvlanTpid1_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid1));

    cmd = DRV_IOW(EpeL2TpidCtl_t, EpeL2TpidCtl_SvlanTpid1_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid1));

    cmd = DRV_IOW(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_SvlanTagTpid1_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid1));

    cmd = DRV_IOW(OamParserEtherCtl_t, OamParserEtherCtl_SvlanTpid1_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid1));

    CTC_ERROR_RETURN(sys_greatbelt_packet_set_svlan_tpid_index(lchip, svlan_tpid1, 1));

    return CTC_E_NONE;
}

/**
@brief The function is to get svlan_tpid1
*/
int32
sys_greatbelt_parser_io_get_svlan_tpid1(uint8 lchip, uint32* svlan_tpid1)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserEthernetCtl_t, ParserEthernetCtl_SvlanTpid1_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, svlan_tpid1));

    return CTC_E_NONE;
}

/**
@brief The function is to set svlan_tpid2
*/
int32
sys_greatbelt_parser_io_set_svlan_tpid2(uint8 lchip, uint32 svlan_tpid2)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserEthernetCtl_t, ParserEthernetCtl_SvlanTpid2_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid2));

    cmd = DRV_IOW(EpeL2TpidCtl_t, EpeL2TpidCtl_SvlanTpid2_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid2));

    cmd = DRV_IOW(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_SvlanTagTpid2_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid2));

    cmd = DRV_IOW(OamParserEtherCtl_t, OamParserEtherCtl_SvlanTpid2_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid2));

    CTC_ERROR_RETURN(sys_greatbelt_packet_set_svlan_tpid_index(lchip, svlan_tpid2, 2));

    return CTC_E_NONE;
}

/**
@brief The function is to get svlan_tpid2
*/
int32
sys_greatbelt_parser_io_get_svlan_tpid2(uint8 lchip, uint32* svlan_tpid2)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserEthernetCtl_t, ParserEthernetCtl_SvlanTpid2_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, svlan_tpid2));

    return CTC_E_NONE;
}

/**
@brief The function is to set svlan_tpid3
*/
int32
sys_greatbelt_parser_io_set_svlan_tpid3(uint8 lchip, uint32 svlan_tpid3)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserEthernetCtl_t, ParserEthernetCtl_SvlanTpid3_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid3));

    cmd = DRV_IOW(EpeL2TpidCtl_t, EpeL2TpidCtl_SvlanTpid3_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid3));

    cmd = DRV_IOW(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_SvlanTagTpid3_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid3));

    cmd = DRV_IOW(OamParserEtherCtl_t, OamParserEtherCtl_SvlanTpid3_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &svlan_tpid3));

    CTC_ERROR_RETURN(sys_greatbelt_packet_set_svlan_tpid_index(lchip, svlan_tpid3, 3));

    return CTC_E_NONE;
}

/**
@brief The function is to get svlan_tpid3
*/
int32
sys_greatbelt_parser_io_get_svlan_tpid3(uint8 lchip, uint32* svlan_tpid3)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserEthernetCtl_t, ParserEthernetCtl_SvlanTpid3_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, svlan_tpid3));

    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_LAYER2_PROTOCOL_CAM_VALID whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_layer2_protocol_cam_valid_entry(uint8 lchip, parser_layer2_protocol_cam_valid_t* parser_layer2_protocol_cam_valid)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer2ProtocolCamValid_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer2_protocol_cam_valid));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_LAYER2_PROTOCOL_CAM_VALID whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_layer2_protocol_cam_valid_entry(uint8 lchip, parser_layer2_protocol_cam_valid_t* parser_layer2_protocol_cam_valid)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer2ProtocolCamValid_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer2_protocol_cam_valid));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_LAYER2_PROTOCOL_CAM whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_layer2_protocol_cam_entry(uint8 lchip, parser_layer2_protocol_cam_t* parser_layer2_protocol_cam)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer2ProtocolCam_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer2_protocol_cam));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_LAYER2_PROTOCOL_CAM whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_layer2_protocol_cam_entry(uint8 lchip, parser_layer2_protocol_cam_t* parser_layer2_protocol_cam)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer2ProtocolCam_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer2_protocol_cam));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_LAYER2_FLEX_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_layer2_flex_ctl_entry(uint8 lchip, parser_layer2_flex_ctl_t* parser_layer2_flex_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer2FlexCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer2_flex_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_LAYER2_FLEX_CTL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_layer2_flex_ctl_entry(uint8 lchip, parser_layer2_flex_ctl_t* parser_layer2_flex_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer2FlexCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer2_flex_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_MPLS_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_mpls_ctl_entry(uint8 lchip, parser_mpls_ctl_t* parser_mpls_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserMplsCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_mpls_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_MPLS_CTL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_mpls_ctl_entry(uint8 lchip, parser_mpls_ctl_t* parser_mpls_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserMplsCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_mpls_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_FCOE_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_fcoe_ctl_entry(uint8 lchip, parser_fcoe_ctl_t* parser_fcoe_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserFcoeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_fcoe_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_FCOE_CTL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_fcoe_ctl_entry(uint8 lchip, parser_fcoe_ctl_t* parser_fcoe_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserFcoeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_fcoe_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_TRILL_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_trill_ctl_entry(uint8 lchip, parser_trill_ctl_t* parser_trill_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserTrillCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_trill_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_TRILL_CTL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_trill_ctl_entry(uint8 lchip, parser_trill_ctl_t* parser_trill_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserTrillCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_trill_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_IP_HASH_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_ip_hash_ctl_entry(uint8 lchip, parser_ip_hash_ctl_t* parser_ip_hash_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserIpHashCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_ip_hash_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_IP_HASH_CTL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_ip_hash_ctl_entry(uint8 lchip, parser_ip_hash_ctl_t* parser_ip_hash_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserIpHashCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_ip_hash_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_LAYER3_FLEX_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_layer3_flex_ctl_entry(uint8 lchip, parser_layer3_flex_ctl_t* parser_layer3_flex_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer3FlexCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer3_flex_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_LAYER3_FLEX_CTL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_layer3_flex_ctl_entry(uint8 lchip, parser_layer3_flex_ctl_t* parser_layer3_flex_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer3FlexCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer3_flex_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_LAYER3_PROTOCOL_CAM_VALID whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_layer3_protocol_cam_valid_entry(uint8 lchip, parser_layer3_protocol_cam_valid_t* parser_layer3_protocol_cam_valid)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer3ProtocolCamValid_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer3_protocol_cam_valid));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_LAYER3_PROTOCOL_CAM_VALID whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_layer3_protocol_cam_valid_entry(uint8 lchip, parser_layer3_protocol_cam_valid_t* parser_layer3_protocol_cam_valid)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer3ProtocolCamValid_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer3_protocol_cam_valid));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_LAYER3_PROTOCOL_CAM whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_layer3_protocol_cam_entry(uint8 lchip, parser_layer3_protocol_cam_t* parser_layer3_protocol_cam)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer3ProtocolCam_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer3_protocol_cam));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_LAYER3_PROTOCOL_CAM whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_layer3_protocol_cam_entry(uint8 lchip, parser_layer3_protocol_cam_t* parser_layer3_protocol_cam)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer3ProtocolCam_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer3_protocol_cam));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_LAYER3_PROTOCOL_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_layer3_protocol_ctl_entry(uint8 lchip, parser_layer3_protocol_ctl_t* parser_layer3_protocol_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer3ProtocolCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer3_protocol_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_L3_HASH_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_l3_hash_ctl_entry(uint8 lchip, parser_layer3_hash_ctl_t* parser_l3_hash_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer3HashCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_l3_hash_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_L3_HASH_CTL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_l3_hash_ctl_entry(uint8 lchip, parser_layer3_hash_ctl_t* parser_l3_hash_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer3HashCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_l3_hash_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_L4_HASH_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_l4_hash_ctl_entry(uint8 lchip, parser_layer4_hash_ctl_t* parser_l4_hash_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer4HashCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_l4_hash_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_L4_HASH_CTL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_l4_hash_ctl_entry(uint8 lchip, parser_layer4_hash_ctl_t* parser_l4_hash_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer4HashCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_l4_hash_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_LAYER4_FLAG_OP_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_layer4_flag_op_ctl_entry(uint8 lchip, parser_layer4_flag_op_ctl_t* parser_layer4_flag_op_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer4FlagOpCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer4_flag_op_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_LAYER4_FLAG_OP_CTL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_layer4_flag_op_ctl_entry(uint8 lchip, parser_layer4_flag_op_ctl_t* parser_layer4_flag_op_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer4FlagOpCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer4_flag_op_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_LAYER4_PORT_OP_SEL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_layer4_port_op_sel_entry(uint8 lchip, parser_layer4_port_op_sel_t* parser_layer4_port_op_sel)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer4PortOpSel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer4_port_op_sel));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_LAYER4_PORT_OP_SEL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_layer4_port_op_sel_entry(uint8 lchip, parser_layer4_port_op_sel_t* parser_layer4_port_op_sel)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer4PortOpSel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer4_port_op_sel));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_LAYER4_PORT_OP_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_layer4_port_op_ctl_entry(uint8 lchip, parser_layer4_port_op_ctl_t* parser_layer4_port_op_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer4PortOpCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer4_port_op_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_LAYER4_PORT_OP_CTL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_layer4_port_op_ctl_entry(uint8 lchip, parser_layer4_port_op_ctl_t* parser_layer4_port_op_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer4PortOpCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer4_port_op_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_LAYER4_FLEX_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_layer4_flex_ctl_entry(uint8 lchip, parser_layer4_flex_ctl_t* parser_layer4_flex_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer4FlexCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer4_flex_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_LAYER4_FLEX_CTL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_layer4_flex_ctl_entry(uint8 lchip, parser_layer4_flex_ctl_t* parser_layer4_flex_ctl)
{

    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer4FlexCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer4_flex_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_LAYER4_APP_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_layer4_app_ctl_entry(uint8 lchip, parser_layer4_app_ctl_t* parser_layer4_app_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer4AppCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer4_app_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_LAYER4_APP_CTL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_layer4_app_ctl_entry(uint8 lchip, parser_layer4_app_ctl_t* parser_layer4_app_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer4AppCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer4_app_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_LAYER4_APP_DATA_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_layer4_app_data_ctl_entry(uint8 lchip, parser_layer4_app_data_ctl_t* parser_layer4_app_data_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer4AppDataCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer4_app_data_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to get PARSER_LAYER4_APP_DATA_CTL whole entry
*/
int32
sys_greatbelt_parser_io_get_parser_layer4_app_data_ctl_entry(uint8 lchip, parser_layer4_app_data_ctl_t* parser_layer4_app_data_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer4AppDataCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer4_app_data_ctl));
    return CTC_E_NONE;
}

/**
@brief The function is to set PARSER_LAYER4_ACH_CTL whole entry
*/
int32
sys_greatbelt_parser_io_set_parser_layer4_ach_ctl_entry(uint8 lchip, parser_layer4_ach_ctl_t* parser_layer4_ach_ctl)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(ParserLayer4AchCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, parser_layer4_ach_ctl));
    return CTC_E_NONE;
}

