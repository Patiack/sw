#include "goldengate/include/drv_lib.h"
#include "ctc_cli.h"
#include "ctc_goldengate_dkit.h"
#include "ctc_goldengate_dkit_drv.h"
#include "ctc_goldengate_dkit_discard.h"
#include "ctc_goldengate_dkit_discard_type.h"
#include "ctc_goldengate_dkit_bus_info.h"
#include "ctc_goldengate_dkit_path.h"

struct ctc_dkit_bus_path_info_s
{
      uint8 slice_ipe;
      uint8 slice_epe;
      uint8 is_ipe_path;
      uint8 is_bsr_path;

      uint8 is_epe_path;
      uint8 discard;
      uint8 exception;
      uint8 detail;
      uint32 discard_type;
      uint32 exception_index;
      uint32 exception_sub_index;
      uint8 lchip;

};
typedef struct ctc_dkit_bus_path_info_s ctc_dkit_bus_path_info_t;

#define CTC_DKIT_BUS_HASH_HIT "HASH lookup hit"
#define CTC_DKIT_BUS_TCAM_HIT "TCAM lookup hit"
#define CTC_DKIT_BUS_NOT_HIT "Not hit any entry!!!"
#define CTC_DKIT_BUS_CONFLICT "HASH lookup conflict"
#define CTC_DKIT_BUS_DEFAULT "HASH lookup hit default entry"


#define DISCARD_____________________________
STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_parser_len_err(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint32 pr_parser_length_error_mode0 = 0;
    uint32 pr_parser_length_error_mode1 = 0;
    uint8 lchip = p_info->lchip;

    IpeUserIdCtl_m ipe_user_id_ctl;
    IpeAclLookupCtl_m ipe_acl_lookup_ctl;

    sal_memset(&ipe_user_id_ctl, 0, sizeof(ipe_user_id_ctl));
    cmd = DRV_IOR(IpeUserIdCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ipe_user_id_ctl);
    sal_memset(&ipe_acl_lookup_ctl, 0, sizeof(ipe_acl_lookup_ctl));
    cmd = DRV_IOR(IpeAclLookupCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ipe_acl_lookup_ctl);

    GetIpeUserIdCtl(A, parserLengthErrorMode_f, &ipe_user_id_ctl,  &pr_parser_length_error_mode0);
    GetIpeUserIdCtl(A, parserLengthErrorMode_f, &ipe_acl_lookup_ctl,  &pr_parser_length_error_mode1);

    if ((pr_parser_length_error_mode0 & 0x2) == 0)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_PARSER_LEN_ERR_USERID;
    }
    else if ((pr_parser_length_error_mode1 & 0x2) == 0)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_PARSER_LEN_ERR_ACL;
    }

    return ret;
}
STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_excep2_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    mac_addr_t pr_mac_da = {0};
    hw_mac_addr_t hw_mac = {0};
    mac_addr_t mac_da_bpdu = {0x01,0x80,0xc2,0x00,0x00,0x00};
    uint16 pr_ether_type = 0;
    uint32 bpdu_exception_en = 0;
    uint32 slow_protocol_exception_en = 0;
    uint32 eapol_exception_en = 0;
    uint32 lldp_exception_en = 0;
    uint32 l2isis_exception_en = 0;

    IpeBpduEscapeCtl_m ipe_bpdu_escape_ctl;

    sal_memset(&ipe_bpdu_escape_ctl, 0, sizeof(ipe_bpdu_escape_ctl));
    cmd = DRV_IOR(IpeBpduEscapeCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ipe_bpdu_escape_ctl);

    GetIpeBpduEscapeCtl(A, bpduExceptionEn_f, &ipe_bpdu_escape_ctl,  &bpdu_exception_en);
    GetIpeBpduEscapeCtl(A, slowProtocolExceptionEn_f, &ipe_bpdu_escape_ctl,  &slow_protocol_exception_en);
    GetIpeBpduEscapeCtl(A, eapolExceptionEn_f, &ipe_bpdu_escape_ctl,  &eapol_exception_en);
    GetIpeBpduEscapeCtl(A, lldpExceptionEn_f, &ipe_bpdu_escape_ctl,  &lldp_exception_en);
    GetIpeBpduEscapeCtl(A, l2isisExceptionEn_f, &ipe_bpdu_escape_ctl,  &l2isis_exception_en);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prMacDa", UserIdImFifo0_t + slice, 0, &hw_mac);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prEtherType", UserIdImFifo0_t + slice, 0, &pr_ether_type);

    CTC_DKIT_SET_USER_MAC(pr_mac_da, hw_mac);
    if (bpdu_exception_en && (0 == sal_memcmp(pr_mac_da, mac_da_bpdu, 6)))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_EXCEP2_DIS_BPDU;
    }
    else if (slow_protocol_exception_en && (pr_ether_type == 0x8809))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_EXCEP2_DIS_SLOW;
    }
    else if (eapol_exception_en && (pr_ether_type == 0x888E))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_EXCEP2_DIS_EAPOL;
    }
    else if (lldp_exception_en && (pr_ether_type == 0x88CC))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_EXCEP2_DIS_LLDP;
    }
    else if (l2isis_exception_en && (pr_ether_type == 0x22F4))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_EXCEP2_DIS_L2ISIS;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_EXCEP2_DIS_CAM;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_receive_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 pi_local_phy_port = 0;
    uint32 pi_from_oam_cpu = 0;
    DsSrcPort_m  ds_src_port;
    uint32 receive_disable = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLocalPhyPort", UserIdImFifo0_t + slice, 0, &pi_local_phy_port);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piFromCpuOrOam", UserIdImFifo0_t + slice, 0, &pi_from_oam_cpu);

    sal_memset(&ds_src_port, 0, sizeof(ds_src_port));
    cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, (pi_local_phy_port + slice*CTC_DKIT_ONE_SLICE_PORT_NUM), cmd, &ds_src_port);
    GetDsSrcPort(A, receiveDisable_f, &ds_src_port,  &receive_disable);

    if (receive_disable && (!pi_from_oam_cpu))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_RECEIVE_DIS_PORT;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_RECEIVE_DIS_VLAN;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_aft(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    DsSrcPort_m  ds_src_port;
    uint32 vlan_tag_ctl = 0;
    uint32 pi_local_phy_port = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLocalPhyPort", UserIdImFifo0_t + slice, 0, &pi_local_phy_port);

    sal_memset(&ds_src_port, 0, sizeof(ds_src_port));
    cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, (pi_local_phy_port + slice*CTC_DKIT_ONE_SLICE_PORT_NUM), cmd, &ds_src_port);
    GetDsSrcPort(A, vlanTagCtl_f, &ds_src_port,  &vlan_tag_ctl);

    p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_AFT_DIS_ALLOW_ALL + vlan_tag_ctl;

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_illegal_pkt_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    mac_addr_t pr_mac_da = {0};
    hw_mac_addr_t hw_mac = {0};
    uint32 allow_mcast_macSa = 0;
    IpeIntfMapperCtl_m  ipe_intf_mapper_ctl;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prMacDa", UserIdImFifo0_t + slice, 0, &hw_mac);
    CTC_DKIT_SET_USER_MAC(pr_mac_da, hw_mac);

    sal_memset(&ipe_intf_mapper_ctl, 0, sizeof(ipe_intf_mapper_ctl));
    cmd = DRV_IOR(IpeIntfMapperCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ipe_intf_mapper_ctl);
    GetIpeIntfMapperCtl(A, allowMcastMacSa_f, &ipe_intf_mapper_ctl,  &allow_mcast_macSa);

    if (IS_BIT_SET(pr_mac_da[0], 0)&& (!allow_mcast_macSa))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_ILLEGAL_PKT_DIS_MCAST;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_ILLEGAL_PKT_DIS_SAME_MAC;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_dscp_arp(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 pr_layer3_type = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer3Type", UserIdImFifo0_t + slice, 0, &pr_layer3_type);

    if (L3TYPE_IPV4 == pr_layer3_type)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_ARP_DHCP_DIS_V4_DSCP;
    }
    else if (L3TYPE_IPV6 == pr_layer3_type)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_ARP_DHCP_DIS_V6_DSCP;
    }
    else if (L3TYPE_ARP == pr_layer3_type)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_ARP_DHCP_DIS_ARP;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_ARP_DHCP_DIS_FIP;
    }

    return ret;

}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_route_mac_ipadd_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 pi_ipv4_mcast = 0;
    uint32 pi_ipv6_mcast = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIpv4McastAddress", IpePktProcPktInfoFifo0_t + slice, 0, &pi_ipv4_mcast);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIpv6McastAddress", IpePktProcPktInfoFifo0_t + slice, 0, &pi_ipv6_mcast);
    if (pi_ipv4_mcast)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_ROUTE_MC_IPADD_DIS_V4;
    }
    else if (pi_ipv6_mcast)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_ROUTE_MC_IPADD_DIS_V6;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_ROUTE_MC_IPADD_DIS_PROTOCOL;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_security_chk_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 pi_mcast = 0;
    uint32 pi_bcast = 0;
    uint32 pi_mac_known = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piVsiMcastMacAddress", IpePktProcPktInfoFifo0_t + slice, 0, &pi_mcast);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piVsiBcastMacAddress", IpePktProcPktInfoFifo0_t + slice, 0, &pi_bcast);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "macKnown", IpeFwdHdrAdjInfoFifo0_t + slice, 0, &pi_mac_known);

    if (pi_mcast && (!pi_mac_known))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_SECURITY_CHK_DIS_MCAST;
    }
    else if ((!pi_mcast) && (!pi_bcast) && (!pi_mac_known))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_SECURITY_CHK_DIS_UCAST;
    }
    else if (pi_bcast)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_SECURITY_CHK_DIS_BCAST;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_SECURITY_CHK_DIS_PORT_MATCH;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_learning_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 index = 0;

    /*bus*/
    uint32 pi_is_inner_mac_sa_lookup = 0;
    uint32 pi_logicSrcPort = 0;
    uint32 pi_outer_learning_logic_src_port = 0;
    uint32 pi_stacking_learning_en = 0;
    uint32 pi_global_src_port = 0;
    uint32 pi_stacking_learning_global_src_port = 0;
    uint32 pi_stacking_learning_logic_src_port = 0;
    uint32 pi_logic_port_security_en = 0;
    uint32 pi_port_security_en = 0;

    /*table or register*/
    uint32 learning_on_stacking = 0;
    uint32 port_security_type  = 0;
    IpeLearningCtl_m ipe_learning_ctl;
    uint32 learn_source = 0;
    uint32 u2_g1_global_src_port = 0;
    uint32 u2_g2_logic_src_port = 0;
    uint32 src_discard = 0;
    uint32 src_mismatch_discard = 0;
    DsMac_m ds_mac;

    uint32 src_port_mismatch = 0;
    uint32 logic_src_port = 0;
    uint32 port_security_en = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "addr", DsMacSaReqFifo0_t + slice, 0, &index);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIsInnerMacSaLookup", IpePktProcPktInfoFifo0_t + slice, 0, &pi_is_inner_mac_sa_lookup);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLogicSrcPort", IpePktProcPktInfoFifo0_t + slice, 0, &pi_logicSrcPort);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piOuterLearningLogicSrcPort", IpePktProcPktInfoFifo0_t + slice, 0, &pi_outer_learning_logic_src_port);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piStackingLearningEn", IpePktProcPktInfoFifo0_t + slice, 0, &pi_stacking_learning_en);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piGlobalSrcPort", IpePktProcPktInfoFifo0_t + slice, 0, &pi_global_src_port);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piStackingLearningGlobalSrcPort", IpePktProcPiFrUiFifo0_t + slice, 0, &pi_stacking_learning_global_src_port);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "pistackingLearningLogicSrcPort", IpePktProcPiFrUiFifo0_t + slice, 0, &pi_stacking_learning_logic_src_port);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLogicPortSecurityEn", IpePktProcPiFrImFifo0_t + slice, 0, &pi_logic_port_security_en);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piPortSecurityEn", IpePktProcPiFrImFifo0_t + slice, 0, &pi_port_security_en);

    sal_memset(&ds_mac, 0, sizeof(ds_mac));
    cmd = DRV_IOR(DsMac_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, (index >> 2),  cmd, &ds_mac);
    GetDsMac(A, srcDiscard_f, &ds_mac,  &src_discard);
    GetDsMac(A, learnSource_f, &ds_mac,  &learn_source);
    GetDsMac(A, u2_g1_globalSrcPort_f, &ds_mac,  &u2_g1_global_src_port);
    GetDsMac(A, u2_g2_logicSrcPort_f, &ds_mac,  &u2_g2_logic_src_port);
    GetDsMac(A, srcMismatchDiscard_f, &ds_mac,  &src_mismatch_discard);

    sal_memset(&ipe_learning_ctl, 0, sizeof(ipe_learning_ctl));
    cmd = DRV_IOR(IpeLearningCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0,  cmd, &ipe_learning_ctl);
    GetIpeLearningCtl(A, learningOnStacking_f, &ipe_learning_ctl,  &learning_on_stacking);
    GetIpeLearningCtl(A, portSecurityType_f, &ipe_learning_ctl,  &port_security_type);

    logic_src_port = pi_is_inner_mac_sa_lookup?pi_logicSrcPort:pi_outer_learning_logic_src_port;
    if (learn_source && pi_stacking_learning_en)
    {
        src_port_mismatch = (!learn_source)?
                                             (pi_stacking_learning_global_src_port != u2_g1_global_src_port)
                                             :(pi_stacking_learning_logic_src_port != u2_g2_logic_src_port);
    }
    else
    {
        src_port_mismatch = (!learn_source)?
                                              (pi_global_src_port != u2_g1_global_src_port)
                                              :(logic_src_port != u2_g2_logic_src_port);
    }
    port_security_en = port_security_type?
                                       (learn_source?pi_logic_port_security_en:pi_port_security_en)
                                       :pi_port_security_en;

    if (src_discard)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_LEARNING_DIS_SRC_MAC;
    }
    else if (src_port_mismatch && src_mismatch_discard && port_security_en)
    {
        if (learn_source)
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_LEARNING_LOGIC_DIS_SRC_PORT;
            p_info->discard_reason_bus.value[0] = (learn_source && pi_stacking_learning_en)?
                                                                                                      pi_stacking_learning_logic_src_port:logic_src_port;
            p_info->discard_reason_bus.value[1] = u2_g2_logic_src_port;
        }
        else
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_LEARNING_DIS_SRC_PORT;
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_LEARNING_LOGIC_DIS_SRC_PORT;
            p_info->discard_reason_bus.value[0] = (learn_source && pi_stacking_learning_en)?
                                                                                                     pi_stacking_learning_global_src_port:pi_global_src_port;
            p_info->discard_reason_bus.value[1] = u2_g1_global_src_port;
        }
        p_info->discard_reason_bus.value_type = CTC_DKIT_DISCARD_VALUE_TYPE_2;
    }

    return ret;
}
STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_forwarding_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint8 is_ecmp = 0;
    /*bus*/
    uint32 pr_layer3_type = 0;
    uint32 pr_layer4_type = 0;
    uint32 pr_layer3_type_2nd = 0;
    uint32 pr_layer4_type_2nd = 0;
    uint32 pi_is_router_mac = 0;
    uint32 pi_is_decap = 0;
    uint32 pi_is_mpls_switched = 0;
    uint32 pi_bridge_packet = 0;
    uint32 pi_deny_bridge = 0;
    uint32 pi_mac_known = 0;
    uint32 pi_ecmp_group_id = 0;
    uint32 pi_inner_packet_lookup = 0;
    uint32 pi_local_phy_port = 0;

    /*register*/
    uint32 bypass_port_cross_connect_disable = 0;
    IpeIntfMapperCtl_m ipe_intf_mapper_ctl;
    uint32 port_cross_connect = 0;
    DsSrcPort_m ds_src_port;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer3Type", UserIdImFifo0_t + slice, 0, &pr_layer3_type);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer4Type", UserIdImFifo0_t + slice, 0, &pr_layer4_type);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLocalPhyPort", UserIdImFifo0_t + slice, 0, &pi_local_phy_port);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer3Type", IpeAclLkupPrFifo0_t + slice, 0, &pr_layer3_type_2nd);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer4Type", IpeAclLkupPrFifo0_t + slice, 0, &pr_layer4_type_2nd);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIsRouterMac", IpeAclLkupInfoFifo0_t + slice, 0, &pi_is_router_mac);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIsDecap", IpeAclLkupInfoFifo0_t + slice, 0, &pi_is_decap);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIsMplsSwitched", IpeAclLkupInfoFifo0_t + slice, 0, &pi_is_mpls_switched);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDenyBridge", IpePktProcPktInfoFifo0_t + slice, 0, &pi_deny_bridge);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piInnerPacketLookup", IpePktProcPktInfoFifo0_t + slice, 0, &pi_inner_packet_lookup);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piBridgePacket", IpeFwdRoutePiThruFifo0_t + slice, 0, &pi_bridge_packet);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piMacKnown", IpeFwdLrnThruFifo0_t + slice, 0, &pi_mac_known);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piEcmpGroupId", IpeFwdLrnThruFifo0_t + slice, 0, &pi_ecmp_group_id);


    sal_memset(&ds_src_port, 0, sizeof(ds_src_port));
    cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, (pi_local_phy_port + slice*CTC_DKIT_ONE_SLICE_PORT_NUM), cmd, &ds_src_port);
    GetDsSrcPort(A, portCrossConnect_f, &ds_src_port,  &port_cross_connect);

    sal_memset(&ipe_intf_mapper_ctl, 0, sizeof(ipe_intf_mapper_ctl));
    cmd = DRV_IOR(IpeIntfMapperCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ipe_intf_mapper_ctl);
    GetIpeIntfMapperCtl(A, bypassPortCrossConnectDisable_f, &ipe_intf_mapper_ctl,  &bypass_port_cross_connect_disable);

    if (pi_ecmp_group_id)
    {
        is_ecmp = 1;
        p_info->discard_reason_bus.value[0] = pi_ecmp_group_id;
        p_info->discard_reason_bus.value_type = CTC_DKIT_DISCARD_VALUE_TYPE_1;
    }

    if (pi_is_router_mac)/*l3 forwarding*/
    {
        if (pi_is_decap)/*tunnel decap*/
        {
            if (pi_inner_packet_lookup)
            {
                if((L3TYPE_IPV4 == pr_layer3_type_2nd) || (L3TYPE_IPV6 == pr_layer3_type_2nd))
                {
                    if (pi_bridge_packet && (!pi_deny_bridge))/*do bridge*/
                    {
                        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_FWD_TUNNEL_BRIDGE + is_ecmp;
                    }
                    else
                    {
                        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_FWD_TUNNEL_IP + is_ecmp;
                    }
                }
            }
            else
            {
                p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_FWD_TUNNEL+ is_ecmp;
            }
        }
        else if (pi_is_mpls_switched)/*mpls decap*/
        {
            if (pi_inner_packet_lookup)
            {
                if((L3TYPE_IPV4 == pr_layer3_type_2nd) || (L3TYPE_IPV6 == pr_layer3_type_2nd))
                {
                    if (pi_bridge_packet && (!pi_deny_bridge))/*do bridge*/
                    {
                        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_FWD_MPLS_BRIDGE + is_ecmp;
                    }
                    else
                    {
                        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_FWD_MPLS_IP + is_ecmp;
                    }
                }
            }
            else
            {
                p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_FWD_MPLS + is_ecmp;
            }
        }
        else/*route*/
        {
            if ((L3TYPE_IPV4 == pr_layer3_type) || (L3TYPE_IPV6 == pr_layer3_type))
            {
                if (pi_bridge_packet && (!pi_deny_bridge))/*do bridge*/
                {
                    p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_FWD_ROUTE_BRIDGE + is_ecmp;
                }
                else
                {
                    p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_FWD_ROUTE + is_ecmp;
                }
            }
            else if (L3TYPE_FCOE == pr_layer3_type)
            {
                p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_FWD_FCOE;
            }
            else if (L3TYPE_TRILL == pr_layer3_type)
            {
                p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_FWD_TRILL;
            }
        }
    }
    else/*l2 forwarding*/
    {
        if (pi_bridge_packet && (!pi_deny_bridge))/*do bridge*/
        {
            if (pi_mac_known)/*FDB hit*/
            {
                p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_FWD_BRIDGE + is_ecmp;
            }
            else/*default entry*/
            {
                p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_FWD_BRIDGE_NOT_HIT + is_ecmp;
            }
        }
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_fatal_excep_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 pi_fatal_exception = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piFatalException", IpeFwdLrnThruFifo0_t + slice, 0, &pi_fatal_exception);
    p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_FATAL_UC_IP_HDR_ERROR_OR_MARTION_ADDR + pi_fatal_exception;

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_ip_header_error_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint8 pr_ip_da[16] = {0};
    uint8 pr_ip_sa[16] = {0};
    uint32 pr_layer3_type = 0;
    uint32 pr_layer4_type = 0;
    uint32 l4_error_bits = 0;
    IpeIntfMapperCtl_m  ipe_intf_mapper_ctl;
    uint32 discard_same_ip_addr = 0;
    uint32 layer4_security_check_en = 0;

    sal_memset(&ipe_intf_mapper_ctl, 0, sizeof(ipe_intf_mapper_ctl));
    cmd = DRV_IOR(IpeIntfMapperCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ipe_intf_mapper_ctl);
    GetIpeIntfMapperCtl(A, discardSameIpAddr_f, &ipe_intf_mapper_ctl,  &discard_same_ip_addr);
    GetIpeIntfMapperCtl(A, layer4SecurityCheckEn_f, &ipe_intf_mapper_ctl,  &layer4_security_check_en);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer3Type", UserIdImFifo0_t + slice, 0, &pr_layer3_type);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer4Type", UserIdImFifo0_t + slice, 0, &pr_layer4_type);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prUL3Source", UserIdImFifo0_t + slice, 0, &pr_ip_sa);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prUL3Dest", UserIdImFifo0_t + slice, 0, &pr_ip_da);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prL4ErrorBits", UserIdImFifo0_t + slice, 0, &l4_error_bits);

    if ((L3TYPE_IPV4 == pr_layer3_type) && ((0 == sal_memcmp(&pr_ip_da[12], &pr_ip_sa[12], 4))) && discard_same_ip_addr)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_IPV4_SAME_IP;
    }
    else if ((L3TYPE_IPV6 == pr_layer3_type) && ((0 == sal_memcmp(pr_ip_da, pr_ip_sa, 16))) && discard_same_ip_addr)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_IPV6_SAME_IP;
    }
    else if ((L4TYPE_TCP == pr_layer4_type) && (l4_error_bits&layer4_security_check_en))
    {
        if ((l4_error_bits&layer4_security_check_en)&0x1)
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_TCP_L4_ERROR0;
        }
        else if ((l4_error_bits&layer4_security_check_en)&0x2)
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_TCP_L4_ERROR1;
        }
        else if ((l4_error_bits&layer4_security_check_en)&0x4)
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_TCP_L4_ERROR2;
        }
        else if ((l4_error_bits&layer4_security_check_en)&0x8)
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_TCP_L4_ERROR3;
        }
        else if ((l4_error_bits&layer4_security_check_en)&0x10)
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_TCP_L4_ERROR4;
        }
    }
    else if ((L4TYPE_UDP == pr_layer4_type) && (l4_error_bits&layer4_security_check_en))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_UDP_L4_ERROR4;
    }
    else if (L4TYPE_ICMP == pr_layer4_type)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_IGMP_FRAG_ERROR;
    }
    else if (L4TYPE_GRE == pr_layer4_type)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_GRE_VERSION_ERROR;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_mplsmc_check_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 pr_layer3_type = 0;
    uint32 pi_context_label_exist = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer3Type", UserIdImFifo0_t + slice, 0, &pr_layer3_type);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piContextLabelExist", Mpls0TrackFifo0_t + slice, 0, &pi_context_label_exist);

    if (pi_context_label_exist && (L3TYPE_MPLSUP == pr_layer3_type))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_MPLSMC_CHECK_DIS_CONTEXT;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_MPLSMC_CHECK_DIS;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_trill_filter_err(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 pr_layer3_type = 0;
    uint32 pi_local_phy_port = 0;
    uint32 discard_trill = 0;
    uint32 discard_non_trill = 0;
    DsSrcPort_m  ds_src_port;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLocalPhyPort", UserIdImFifo0_t + slice, 0, &pi_local_phy_port);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer3Type", UserIdImFifo0_t + slice, 0, &pr_layer3_type);

    sal_memset(&ds_src_port, 0, sizeof(ds_src_port));
    cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, (pi_local_phy_port + slice*CTC_DKIT_ONE_SLICE_PORT_NUM), cmd, &ds_src_port);
    GetDsSrcPort(A, discardTrill_f, &ds_src_port,  &discard_trill);
    GetDsSrcPort(A, discardNonTrill_f, &ds_src_port,  &discard_non_trill);

    if (discard_trill && (L3TYPE_TRILL == pr_layer3_type))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_TRILL_FILTER_ERR_TRILL_DIS;
    }
    else if (discard_non_trill && (L3TYPE_TRILL != pr_layer3_type))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_TRILL_FILTER_ERR_NON_TRILL_DIS;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_TRILL_FILTER_ERR_TRILL_UCAST;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_l3_excep_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 pi_exception_sub_index = 0;
    uint32 pi_exception_index = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piExceptionIndex", IpeFwdOamFifo0_t + slice, 0, &pi_exception_index);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piExceptionSubIndex", IpeFwdOamFifo0_t + slice, 0, &pi_exception_sub_index);

    if ((7 == pi_exception_index) && (0xd == pi_exception_sub_index))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_L3_EXCEP_DIS_ACH;
    }
    else if (3 == pi_exception_index)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_L3_EXCEP_DIS_ROUTE;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_L3_EXCEP_DIS_FRAG;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_trill_version_check_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
     /*register*/
    uint32 trill_version_check_en = 0;
    uint32 trill_version = 0;
    uint32 trill_bfd_check_en = 0;
    uint32 trill_bfd_hop_count = 0;
    uint32 version_check_en = 0;
    uint32 lookup_trill_version = 0;

     /*bus*/
    uint32 pr_layer3_type = 0;
    uint32 parser_trill_version = 0;
    uint32 is_trill_bfd_echo = 0;
    uint32 is_trill_bfd = 0;
    uint32 trill_multicast = 0;
    uint32 trill_multi_hop = 0;
    uint32 pi_packet_ttl = 0;
    uint32 pi_trill_version_match = 0;
    uint32 trill_tnner_vlan_valid = 0;
    uint32 trill_tnner_vlan_id = 0;
    uint8 pr_l3_dest[16] = {0};

    IpeTunnelDecapCtl_m  ipe_tunnel_decap_ctl;
    IpeTrillCtl_m ipe_trill_ctl;
    IpeLookupCtl_m ipe_lookup_ctl;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer3Type", UserIdImFifo0_t + slice, 0, &pr_layer3_type);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prUL3Dest", UserIdImFifo0_t + slice, 0, &pr_l3_dest);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piPacketTtl", Mpls0TrackFifo0_t + slice, 0, &pi_packet_ttl);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piTrillVersionMatch", IpePktProcPktInfoFifo0_t + slice, 0, &pi_trill_version_match);

    parser_trill_version = pr_l3_dest[11]&0x3;
    is_trill_bfd_echo = IS_BIT_SET(pr_l3_dest[9], 1);
    is_trill_bfd = IS_BIT_SET(pr_l3_dest[11], 3);
    trill_multicast = IS_BIT_SET(pr_l3_dest[13], 0);
    trill_multi_hop = IS_BIT_SET(pr_l3_dest[9], 2);

    sal_memset(&ipe_tunnel_decap_ctl, 0, sizeof(ipe_tunnel_decap_ctl));
    cmd = DRV_IOR(IpeTunnelDecapCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0,  cmd, &ipe_tunnel_decap_ctl);
    GetIpeTunnelDecapCtl(A, trillversionCheckEn_f, &ipe_tunnel_decap_ctl,  &trill_version_check_en);
    GetIpeTunnelDecapCtl(A, trillVersion_f, &ipe_tunnel_decap_ctl,  &trill_version);
    GetIpeTunnelDecapCtl(A, trillBfdCheckEn_f, &ipe_tunnel_decap_ctl,  &trill_bfd_check_en);
    GetIpeTunnelDecapCtl(A, trillBfdHopCount_f, &ipe_tunnel_decap_ctl,  &trill_bfd_hop_count);

    sal_memset(&ipe_trill_ctl, 0, sizeof(ipe_trill_ctl));
    cmd = DRV_IOR(IpeTrillCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0,  cmd, &ipe_trill_ctl);
    GetIpeTrillCtl(A, versionCheckEn_f, &ipe_trill_ctl,  &version_check_en);

    sal_memset(&ipe_lookup_ctl, 0, sizeof(ipe_lookup_ctl));
    cmd = DRV_IOR(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0,  cmd, &ipe_lookup_ctl);
    GetIpeLookupCtl(A, trillVersion_f, &ipe_lookup_ctl,  &lookup_trill_version);

    if (trill_version_check_en && (trill_version != parser_trill_version)
     && (L3TYPE_TRILL == pr_layer3_type))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_VERSION;
        p_info->discard_reason_bus.value[0] = trill_version;
        p_info->discard_reason_bus.value_type = CTC_DKIT_DISCARD_VALUE_TYPE_1;
    }
    else if (trill_bfd_check_en && (L3TYPE_TRILL == pr_layer3_type)
                     && (is_trill_bfd_echo || is_trill_bfd) && trill_multicast)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_BFD_MCAST;
    }
    else if (trill_bfd_check_en && (L3TYPE_TRILL == pr_layer3_type)
               && (is_trill_bfd_echo || is_trill_bfd) && trill_multi_hop && (pi_packet_ttl <= trill_bfd_hop_count))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_BFD_MULTIHOP;
        p_info->discard_reason_bus.value[0] = trill_bfd_hop_count;
        p_info->discard_reason_bus.value_type = CTC_DKIT_DISCARD_VALUE_TYPE_1;
    }
    else if (trill_bfd_check_en && (L3TYPE_TRILL == pr_layer3_type) && (is_trill_bfd_echo || is_trill_bfd)
                && (!trill_multi_hop) && (pi_packet_ttl != 0x3F))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_BFD_1HOP;
    }
    else if (version_check_en && (!pi_trill_version_match))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_VERSION;
        p_info->discard_reason_bus.value[0] = lookup_trill_version;
        p_info->discard_reason_bus.value_type = CTC_DKIT_DISCARD_VALUE_TYPE_1;
    }
    else if ((L3TYPE_TRILL == pr_layer3_type) && trill_tnner_vlan_valid
               && ((0xFFF == trill_tnner_vlan_id) || (0 == trill_tnner_vlan_id)))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_INNER_VLAN_INVALID;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_INNER_VLAN_ABSENT;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_ptp_excep_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 pi_ptp_index = 0;
    uint32 pi_ptp_version = 0;
    uint32 pi_ptp_messageType = 0;
    uint32 pi_is_l2_ptp = 0;
    uint32 pi_is_l4_ptp = 0;

    uint32 discard_ptp_packet = 0;
    uint32 v1_ptp_en = 0;
    uint32 v3_ptp_mode = 0;
    DsPtpProfile_m ds_ptp_profile;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piPtpIndex", IpePktProcPktInfoFifo0_t + slice, 0, &pi_ptp_index);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piPtpMessageType", IpePktProcPktInfoFifo0_t + slice, 0, &pi_ptp_messageType);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piPtpVersion", IpePktProcPktInfoFifo0_t + slice, 0, &pi_ptp_version);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIsL2Ptp", IpePktProcPktInfoFifo0_t + slice, 0, &pi_is_l2_ptp);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIsL4Ptp", IpePktProcPktInfoFifo0_t + slice, 0, &pi_is_l4_ptp);

    sal_memset(&ds_ptp_profile, 0, sizeof(ds_ptp_profile));
    cmd = DRV_IOR(DsPtpProfile_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, pi_ptp_index,  cmd, &ds_ptp_profile);
    GetDsPtpProfile(A, v1PtpEn_f, &ds_ptp_profile,  &v1_ptp_en);
    GetDsPtpProfile(A, v3PtpMode_f, &ds_ptp_profile,  &v3_ptp_mode);

    switch(pi_ptp_messageType)
    {
    case 0:
        GetDsPtpProfile(A, array_0_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    case 1:
        GetDsPtpProfile(A, array_1_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    case 2:
        GetDsPtpProfile(A, array_2_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    case 3:
        GetDsPtpProfile(A, array_3_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    case 4:
        GetDsPtpProfile(A, array_4_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    case 5:
        GetDsPtpProfile(A, array_5_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    case 6:
        GetDsPtpProfile(A, array_6_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    case 7:
        GetDsPtpProfile(A, array_7_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    case 8:
        GetDsPtpProfile(A, array_8_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    case 9:
        GetDsPtpProfile(A, array_9_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    case 10:
        GetDsPtpProfile(A, array_10_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    case 11:
        GetDsPtpProfile(A, array_11_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    case 12:
        GetDsPtpProfile(A, array_12_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    case 13:
        GetDsPtpProfile(A, array_13_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    case 14:
        GetDsPtpProfile(A, array_14_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    case 15:
        GetDsPtpProfile(A, array_15_discardPacket_f, &ds_ptp_profile,  &discard_ptp_packet);
        break;
    default:
        break;
    }

    if ((pi_is_l2_ptp || pi_is_l4_ptp) && ((2 == pi_ptp_version) || ((3 == pi_ptp_version) && (2 == v3_ptp_mode))) && discard_ptp_packet)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_PTP_EXCEP_DIS_MESSAGE_DIS;
        p_info->discard_reason_bus.value[0] = pi_ptp_messageType;
        p_info->discard_reason_bus.value_type = CTC_DKIT_DISCARD_VALUE_TYPE_1;
    }
    else if ((pi_is_l2_ptp || pi_is_l4_ptp) && v1_ptp_en && (1 == pi_ptp_version))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_PTP_EXCEP_DIS_V1;
    }
    else if ((pi_is_l2_ptp || pi_is_l4_ptp) && (3 == pi_ptp_version) && (1 == v3_ptp_mode))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_PTP_EXCEP_DIS_HIGH_VERSION;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_PTP_EXCEP_DIS_UNKNOWN;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_oam_filter_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 pi_ether_oam_discard = 0;
    uint32 pi_vlan_ptr = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piEtherOamDiscard", IpeLkupKeyGenPiTrackFifo0_t + slice, 0, &pi_ether_oam_discard);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piVlanPtr", IpeLkupKeyGenPiTrackFifo0_t + slice, 0, &pi_vlan_ptr);

    if (pi_ether_oam_discard)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_OAM_FILTER_DIS_VLAN;
        p_info->discard_reason_bus.value[0] = pi_vlan_ptr;
        p_info->discard_reason_bus.value_type = CTC_DKIT_DISCARD_VALUE_TYPE_1;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_OAM_FILTER_DIS_STP;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_no_mep_mip_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 tempOamVlanId  = 0;
    uint32 oamLkupUseFid = 0;
    uint32 useVlanId = 0;

    /*register*/
    uint32 vpls_oam_use_cvlan_waive = 0;
    uint32 vpls_oam_use_vlan_waive = 0;
    uint32 oam_lookup_user_vlan_id = 0;
    uint32 mpls_section_oam_use_port = 0;
    IpeLookupCtl_m ipe_lookup_ctl;

    /*bus*/
    uint32 pr_cvlan_id_valid = 0;
    uint32 pr_cvlan_id = 0;
    uint32 pr_svlan_id_valid = 0;
    uint32 pr_svlan_id = 0;

    uint32 pi_rx_oam_type = 0;
    uint32 pi_global_src_port = 0;

    uint32 pi_oam_use_fid = 0;
    uint32 pi_fid = 0;
    uint32 pi_vlan_ptr = 0;
    uint32 pi_cvlan_id = 0;
    uint32 pi_svlan_id_valid = 0;
    uint32 pi_svlan_id = 0;
    uint32 pi_oam_hash_conflict = 0;
    uint32 pi_link_or_section_oam = 0;
    uint32 pi_interface_id = 0;
    uint32 pi_local_phy_port = 0;
    uint32 pi_mpls_oam_index = 0;

    uint32 pr_uL4UserData[3] = {0};

    sal_memset(&ipe_lookup_ctl, 0, sizeof(ipe_lookup_ctl));
    cmd = DRV_IOR(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0,  cmd, &ipe_lookup_ctl);
    GetIpeLookupCtl(A, vplsOamUseCvlanWaive_f, &ipe_lookup_ctl,  &vpls_oam_use_cvlan_waive);
    GetIpeLookupCtl(A, vplsOamUseVlanWaive_f, &ipe_lookup_ctl,  &vpls_oam_use_vlan_waive);
    GetIpeLookupCtl(A, oamLookupUserVlanId_f, &ipe_lookup_ctl,  &oam_lookup_user_vlan_id);
    GetIpeLookupCtl(A, mplsSectionOamUsePort_f, &ipe_lookup_ctl,  &mpls_section_oam_use_port);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "oamHashIpeOamConflict", IpeOamHashInfoFifo0_t + slice, 0, &pi_oam_hash_conflict);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prCvlanIdValid", UserIdImFifo0_t + slice, 0, &pr_cvlan_id_valid);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prCvlanId", UserIdImFifo0_t + slice, 0, &pr_cvlan_id);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prSvlanIdValid", UserIdImFifo0_t + slice, 0, &pr_svlan_id_valid);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prSvlanId", UserIdImFifo0_t + slice, 0, &pr_svlan_id);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLocalPhyPort", UserIdImFifo0_t + slice, 0, &pi_local_phy_port);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prUL4UserData", UserIdImFifo0_t + slice, 0, &pr_uL4UserData);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piRxOamType", IpePktProcPktInfoFifo0_t + slice, 0, &pi_rx_oam_type);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piGlobalSrcPort", IpePktProcPktInfoFifo0_t + slice, 0, &pi_global_src_port);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLinkOrSectionOam", IpePktProcPktInfoFifo0_t + slice, 0, &pi_link_or_section_oam);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piInterfaceId", IpePktProcPktInfoFifo0_t + slice, 0, &pi_interface_id);


    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piOamUseFid", IpeLkupKeyGenPiTrackFifo0_t + slice, 0, &pi_oam_use_fid);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piFid", IpeLkupKeyGenPiTrackFifo0_t + slice, 0, &pi_fid);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piVlanPtr", IpeLkupKeyGenPiTrackFifo0_t + slice, 0, &pi_vlan_ptr);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piSvlanIdValid", IpeLkupKeyGenPiTrackFifo0_t + slice, 0, &pi_svlan_id_valid);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piSvlanId", IpeLkupKeyGenPiTrackFifo0_t + slice, 0, &pi_svlan_id);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piCvlanId", IpeLkupKeyGenPiTrackFifo0_t + slice, 0, &pi_cvlan_id);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piMplsOamIndex", IpeLkupKeyGenPiTrackFifo0_t + slice, 0, &pi_mpls_oam_index);

    switch(pi_rx_oam_type)
    {
    case RXOAMTYPE_ETHEROAM:
        {
            if (pi_oam_use_fid && ((pi_fid >> 12) == 0))
            {
                if (vpls_oam_use_cvlan_waive)
                {
                    tempOamVlanId = pr_cvlan_id_valid ?pr_cvlan_id : 0;
                }
                else
                {
                    tempOamVlanId = pr_svlan_id_valid ? pr_svlan_id : 0;
                }
                oamLkupUseFid = (!vpls_oam_use_vlan_waive);
            }
            else
            {
                useVlanId = pi_svlan_id_valid ? pi_svlan_id : pi_cvlan_id;
                tempOamVlanId = ((!oam_lookup_user_vlan_id)) ?pi_vlan_ptr : useVlanId;
                oamLkupUseFid = pi_oam_use_fid;
            }

            if ((CTC_DKIT_IPE_OAM_NOT_FIND_DIS == p_info->discard_reason[0].reason_id) && pi_oam_hash_conflict)
            {
                p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_OAM_NOT_FIND_DIS_CONFLICT_ETHER;
            }
            else if (CTC_DKIT_IPE_OAM_NOT_FIND_DIS == p_info->discard_reason[0].reason_id)
            {
                p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_OAM_NOT_FIND_DIS_EDGE_PORT;
            }
            else
            {
                p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_ETHER;
            }
            p_info->discard_reason_bus.value[0] = pi_global_src_port;
            p_info->discard_reason_bus.value[1] = oamLkupUseFid? pi_fid:tempOamVlanId;
            p_info->discard_reason_bus.value[2] = pi_oam_use_fid;
            p_info->discard_reason_bus.value_type = CTC_DKIT_DISCARD_VALUE_TYPE_3;
        }
        break;
    case RXOAMTYPE_IPBFD:
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_IP_BFD;
        p_info->discard_reason_bus.value[0] = pr_uL4UserData[2]; /*bfdMyDiscriminator*/
        p_info->discard_reason_bus.value_type = CTC_DKIT_DISCARD_VALUE_TYPE_1;
        break;
    case RXOAMTYPE_MPLSBFD:
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_MPLS_BFD;
        p_info->discard_reason_bus.value[0] = pr_uL4UserData[2]; /*bfdMyDiscriminator*/
        p_info->discard_reason_bus.value_type = CTC_DKIT_DISCARD_VALUE_TYPE_1;
        break;
    case RXOAMTYPE_ACHOAM:
        {
            if (pi_link_or_section_oam)
            {
                if (mpls_section_oam_use_port)
                {
                    p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_ACH_SECTION_PORT;
                    p_info->discard_reason_bus.value[0] = pi_local_phy_port;

                }
                else
                {
                    p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_ACH_SECTION_INTF;
                    p_info->discard_reason_bus.value[0] = pi_interface_id;
                }
            }
            else
            {
                p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_ACH;
                p_info->discard_reason_bus.value[0] = pi_mpls_oam_index;
            }
            p_info->discard_reason_bus.value_type = CTC_DKIT_DISCARD_VALUE_TYPE_1;
        }
        break;
    case RXOAMTYPE_TRILLBFD:
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_TRILL_BFD;
        break;
    default:
        break;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_icmp_err_msg_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 pi_is_ipv4_icmp_err_msg = 0;
    uint32 pi_is_ipv6_icmp_err_msg = 0;
    uint32 index = 0;

    uint32 icmp_err_msg_check_en = 0;
    DsIpDa_m ds_ip_da;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIsIpv4IcmpErrMsg", IpePktProcPktInfoFifo0_t + slice, 0, &pi_is_ipv4_icmp_err_msg);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIsIpv6IcmpErrMsg", IpePktProcPktInfoFifo0_t + slice, 0, &pi_is_ipv6_icmp_err_msg);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "addr", DsIpDaReqFifo0_t + slice, 0, &index);

    sal_memset(&ds_ip_da, 0, sizeof(ds_ip_da));
    cmd = DRV_IOR(DsIpDa_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, index,  cmd, &ds_ip_da);
    GetDsIpDa(A, icmpErrMsgCheckEn_f, &ds_ip_da,  &icmp_err_msg_check_en);

    if (icmp_err_msg_check_en && pi_is_ipv4_icmp_err_msg)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_ICMP_ERR_MSG_DIS_IPV4;
    }
    else if (icmp_err_msg_check_en && pi_is_ipv6_icmp_err_msg)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_ICMP_ERR_MSG_DIS_IPV6;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_ICMP_ERR_MSG_DIS_PT;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_vxlan_nvgre_check_fail(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 pi_is_nvgre = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIsNvgre", UserIdImFifo0_t + slice, 0, &pi_is_nvgre);

    if (pi_is_nvgre)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_VXLAN_NVGRE_CHK_FAIL_NVGRE;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_VXLAN_NVGRE_CHK_FAIL_VXLAN;
    }


    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_port_mac_check_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 pi_mac_sa_is_port_mac = 0;
    uint32 pi_mac_da_is_port_mac = 0;


    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piMacDaLookupEn", IpePktProcPktInfoFifo0_t + slice, 0, &pi_mac_da_is_port_mac);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piMacSaIsPortMac", IpePktProcPktInfoFifo0_t + slice, 0, &pi_mac_sa_is_port_mac);

    if (pi_mac_da_is_port_mac)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_PORT_MAC_CHECK_DIS_DA;
    }
    else if (pi_mac_sa_is_port_mac)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_PORT_MAC_CHECK_DIS_SA;
    }


    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe_vxlan_nvgre_inner_vtag_chk_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 pi_is_nvgre = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIsNvgre", UserIdImFifo0_t + slice, 0, &pi_is_nvgre);

    if (pi_is_nvgre)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_VXLAN_NVGRE_INNER_VTAG_CHK_DIS_NVGRE;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_IPE_VXLAN_NVGRE_INNER_VTAG_CHK_DIS_VXLAN;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_ipe(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 pi_local_phy_port = 0;
    uint8 slice = p_info->slice_ipe;
    uint32 global_src_port = 0;
    uint32 cmd = 0;
    uint8 lchip = p_info->lchip;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLocalPhyPort", UserIdImFifo0_t + slice, 0, &pi_local_phy_port);

    cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_globalSrcPort_f);
    DRV_IOCTL(lchip, (pi_local_phy_port + slice*CTC_DKIT_ONE_SLICE_PORT_NUM), cmd, &global_src_port);
    p_info->discard_reason_bus.gport = CTC_DKIT_DRV_GPORT_TO_CTC_GPORT(global_src_port);

    switch (p_info->discard_reason[0].reason_id)
    {
        case CTC_DKIT_DISCARD_INVALIED:
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_DISCARD_INVALIED;
            break;
        case CTC_DKIT_IPE_USERID_BINDING_DIS:
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_DISCARD_AMBIGUOUS;
            break;
             /*case CTC_DKIT_IPE_LPBK_HDR_ADJ_RM_ERR0:*/
             /*  break;*/
        case CTC_DKIT_IPE_PARSER_LEN_ERR:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_parser_len_err(p_info);
            break;
             /*case CTC_DKIT_IPE_LPBK_HDR_ADJ_RM_ERR1:*/
             /*  break;*/
        case CTC_DKIT_IPE_EXCEP2_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_excep2_dis(p_info);
            break;
        case CTC_DKIT_IPE_DS_USERID_DIS:
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_DISCARD_AMBIGUOUS;
            break;
        case CTC_DKIT_IPE_RECEIVE_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_receive_dis(p_info);
            break;
             /*case CTC_DKIT_IPE_PBB_CHK_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_PROTOCOL_VLAN_DIS:*/
             /*  break;*/
        case CTC_DKIT_IPE_AFT_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_aft(p_info);
            break;
        case CTC_DKIT_IPE_ILLEGAL_PKT_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_illegal_pkt_dis(p_info);
            break;
             /*case CTC_DKIT_IPE_STP_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_TRILL_ESADI_LPBK_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_PBB_OAM_DIS:*/
             /*  break;*/
        case CTC_DKIT_IPE_ARP_DHCP_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_dscp_arp(p_info);
            break;
             /*case CTC_DKIT_IPE_DS_PHYPORT_SRCDIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_VLAN_FILTER_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_DS_ACL_DIS:*/
             /*  break;*/
        case CTC_DKIT_IPE_ROUTE_MC_IPADD_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_route_mac_ipadd_dis(p_info);
            break;
        case CTC_DKIT_IPE_SECURITY_CHK_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_security_chk_dis(p_info);
            break;
             /*case CTC_DKIT_IPE_STORM_CTL_DIS:*/
             /*  break;*/
        case CTC_DKIT_IPE_LEARNING_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_learning_dis(p_info);
            break;
             /*case CTC_DKIT_IPE_POLICING_DIS:*/
             /*  break;*/
        case CTC_DKIT_IPE_NO_FORWARDING_PTR:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_forwarding_dis(p_info);
            break;
        case CTC_DKIT_IPE_IS_DIS_FORWARDING_PTR:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_forwarding_dis(p_info);
            break;
        case CTC_DKIT_IPE_FATAL_EXCEP_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_fatal_excep_dis(p_info);
            break;
             /*case CTC_DKIT_IPE_APS_DIS:*/
             /*  break;*/
        case CTC_DKIT_IPE_DS_FWD_DESTID_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_forwarding_dis(p_info);
            break;
        case CTC_DKIT_IPE_IP_HEADER_ERROR_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_ip_header_error_dis(p_info);
            break;
             /*case CTC_DKIT_IPE_VXLAN_FLAG_CHK_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_GENEVE_PAKCET_DISCARD:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_TRILL_OAM_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_LOOPBACK_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_EFM_DIS:*/
             /*  break;*/
        case CTC_DKIT_IPE_MPLSMC_CHECK_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_mplsmc_check_dis(p_info);
            break;
             /*case CTC_DKIT_IPE_STACKING_HDR_CHK_ERR:*/
             /*  break;*/
        case CTC_DKIT_IPE_TRILL_FILTER_ERR:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_trill_filter_err(p_info);
            break;
             /*case CTC_DKIT_IPE_ENTROPY_IS_RESERVED_LABEL:*/
             /*  break;*/
        case CTC_DKIT_IPE_L3_EXCEP_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_l3_excep_dis(p_info);
            break;
             /*case CTC_DKIT_IPE_L2_EXCEP_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_TRILL_MC_ADD_CHK_DIS:*/
             /*  break;*/
        case CTC_DKIT_IPE_TRILL_VERSION_CHK_DIS:
            ret =  _ctc_goldengate_dkit_bus_get_reason_ipe_trill_version_check_dis(p_info);
            break;
        case CTC_DKIT_IPE_PTP_EXCEP_DIS:
            ret =  _ctc_goldengate_dkit_bus_get_reason_ipe_ptp_excep_dis(p_info);
            break;
        case CTC_DKIT_IPE_OAM_NOT_FIND_DIS:
            ret =   _ctc_goldengate_dkit_bus_get_reason_ipe_no_mep_mip_dis(p_info);
            break;
        case CTC_DKIT_IPE_OAM_FILTER_DIS:
            ret =   _ctc_goldengate_dkit_bus_get_reason_ipe_oam_filter_dis(p_info);
            break;
             /*case CTC_DKIT_IPE_BFD_OAM_TTL_CHK_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_ICMP_REDIRECT_DIS:*/
             /*  break;*/
        case CTC_DKIT_IPE_NO_MEP_MIP_DIS:
            ret =   _ctc_goldengate_dkit_bus_get_reason_ipe_no_mep_mip_dis(p_info);
            break;
             /*case CTC_DKIT_IPE_OAM_DISABLE_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_PBB_DECAP_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_MPLS_TMPLS_OAM_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_MPLSTP_MCC_SCC_DIS:*/
             /*  break;*/
        case CTC_DKIT_IPE_ICMP_ERR_MSG_DIS:
            ret =   _ctc_goldengate_dkit_bus_get_reason_ipe_icmp_err_msg_dis(p_info);
            break;
             /*case CTC_DKIT_IPE_ETH_SERV_OAM_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_ETH_LINK_OAM_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_TUNNEL_DECAP_MARTIAN_ADD:*/
             /*  break;*/
        case CTC_DKIT_IPE_VXLAN_NVGRE_CHK_FAIL:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_vxlan_nvgre_check_fail(p_info);
            break;
             /*case CTC_DKIT_IPE_USERID_FWD_PTR_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_QUE_DROP_SPAN_DISCARD:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_TRILL_RPF_CHK_DIS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_MUX_PORT_ERR:*/
             /*  break;*/
        case CTC_DKIT_IPE_PORT_MAC_CHECK_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_port_mac_check_dis(p_info);
            break;
        case CTC_DKIT_IPE_VXLAN_NVGRE_INNER_VTAG_CHK_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe_vxlan_nvgre_inner_vtag_chk_dis(p_info);
            break;
             /*case CTC_DKIT_IPE_RESERVED2:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_HW_HAR_ADJ:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_HW_INT_MAP:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_HW_LKUP_QOS:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_HW_LKUP_KEY_GEN:*/
             /*  break;*/
             /*case CTC_DKIT_IPE_HW_PKT_PROC:*/
             /*  break;*/
        default:
            break;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_epe_port_iso_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;
    uint32 pi_source_port_isolateId = 0;
    uint32 pi_local_phy_port = 0;

    DsDestPort_m ds_dest_port;
    uint32 dest_port_isolateId = 0;
    DsPortIsolation_m  ds_port_isolation;
    uint32 unknown_uc_isolate = 0;
    uint32 known_uc_isolate = 0;
    uint32 unknown_mc_isolate = 0;
    uint32 known_mc_isolate = 0;
    uint32 bc_isolate = 0;

    uint8 src_is_c_port = 0;
    uint8 src_is_p_port = 0;
    uint8 dest_is_c_port = 0;
    uint8 dest_is_p_port = 0;
    uint8 src_community_id = 0;
    uint8 dest_community_id = 0;
    uint8 pi_mac_known = 0;
    uint8 mcast_mac = 0;
    uint8 bcast_mac = 0;
    uint32 pr_mac_da_47_18 = 0;
    uint32 pr_mac_da_17_0 = 0;


    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLocalPhyPort", HdrProcInputInfoFifo0_t + slice, 0, &pi_local_phy_port);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piSourcePortIsolateId", HdrProcInputInfoFifo0_t + slice, 0, &pi_source_port_isolateId);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piMacKnown", HdrProcInputInfoFifo0_t + slice, 0, &pi_mac_known);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prMacDa_47_18_", EpeNextHopPrFifo0_t + slice, 0, &pr_mac_da_47_18);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prMacDa_17_0_", EpeNextHopPrFifo0_t + slice, 0, &pr_mac_da_17_0);

    sal_memset(&ds_dest_port, 0, sizeof(ds_dest_port));
    cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, (pi_local_phy_port + (slice * CTC_DKIT_ONE_SLICE_PORT_NUM)),  cmd, &ds_dest_port);
    GetDsDestPort(A, destPortIsolateId_f, &ds_dest_port,  &dest_port_isolateId);

    sal_memset(&ds_port_isolation, 0, sizeof(ds_port_isolation));
    cmd = DRV_IOR(DsPortIsolation_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, pi_source_port_isolateId,  cmd, &ds_port_isolation);
    GetDsPortIsolation(A, unknownUcIsolate_f, &ds_dest_port,  &unknown_uc_isolate);
    GetDsPortIsolation(A, knownUcIsolate_f, &ds_dest_port,  &known_uc_isolate);
    GetDsPortIsolation(A, unknownMcIsolate_f, &ds_dest_port,  &unknown_mc_isolate);
    GetDsPortIsolation(A, knownMcIsolate_f, &ds_dest_port,  &known_mc_isolate);
    GetDsPortIsolation(A, bcIsolate_f, &ds_dest_port,  &bc_isolate);


    if (IS_BIT_SET(pi_source_port_isolateId, 5) && IS_BIT_SET(dest_port_isolateId, 5)) /*PVlan*/
    {
        if (!IS_BIT_SET(pi_source_port_isolateId, 4))
        {
            src_is_c_port = 1;
            src_community_id = pi_source_port_isolateId&0xF;
        }
        else
        {
            src_is_p_port = !IS_BIT_SET(pi_source_port_isolateId, 3);
        }
        if (!IS_BIT_SET(dest_port_isolateId, 4))
        {
            dest_is_c_port = 1;
            dest_community_id = dest_port_isolateId&0xF;
        }
        else
        {
            dest_is_p_port = !IS_BIT_SET(dest_port_isolateId, 3);
        }

        if (src_is_p_port)
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_PORT_ISO_DIS_PVLAN_P2X;
        }
        else if (dest_is_p_port)
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_PORT_ISO_DIS_PVLAN_X2P;
        }
        else if (src_is_c_port && dest_is_c_port && (src_community_id == dest_community_id))
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_PORT_ISO_DIS_PVLAN_C2C;
            p_info->discard_reason_bus.value[0] = src_community_id;
            p_info->discard_reason_bus.value[1] = dest_community_id;
            p_info->discard_reason_bus.value_type = CTC_DKIT_DISCARD_VALUE_TYPE_2;
        }

    }
    else /*port isolation*/
    {

        if ((0x3FFFFFFF == pr_mac_da_47_18) && (0x3FFFF == pr_mac_da_17_0))
        {
            bcast_mac = 1;
        }
        if (IS_BIT_SET(pr_mac_da_47_18, 22) && (!bcast_mac))
        {
            mcast_mac = 1;
        }


        if ((!pi_mac_known) && (!mcast_mac) && (!bcast_mac) && IS_BIT_SET(unknown_uc_isolate, dest_port_isolateId))
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_PORT_ISO_DIS_UNKNOWN_UC_ISOLATION;
        }
        if (pi_mac_known && (!mcast_mac) && (!bcast_mac) && IS_BIT_SET(known_uc_isolate, dest_port_isolateId))
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_PORT_ISO_DIS_KNOWN_UC_ISOLATION;
        }
        if ((!pi_mac_known) && mcast_mac && IS_BIT_SET(unknown_mc_isolate, dest_port_isolateId))
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_PORT_ISO_DIS_UNKNOWN_MC_ISOLATION;
        }
        if (pi_mac_known && mcast_mac && IS_BIT_SET(known_mc_isolate, dest_port_isolateId))
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_PORT_ISO_DIS_KNOWN_MC_ISOLATION;
        }
        if (bcast_mac  && IS_BIT_SET(bc_isolate, dest_port_isolateId))
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_PORT_ISO_DIS_BC_ISOLATION;
        }
        p_info->discard_reason_bus.value[0] = pi_source_port_isolateId;
        p_info->discard_reason_bus.value[1] = dest_port_isolateId;
        p_info->discard_reason_bus.value_type = CTC_DKIT_DISCARD_VALUE_TYPE_2;

    }

    return ret;

}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_epe_vpls_horizon_split_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;
    uint32 pi_local_phy_port = 0;
    uint32 pi_payload_operation = 0;
    uint32 pi_src_is_logic_tunnel = 0;
    uint32 dest_is_logic_tunnel = 0;
    uint32 pi_ingress_edit_en = 0;
    uint8 bridge_operation = 0;

    uint8 logic_port_type = 0;
    DsDestPort_m ds_dest_port;
    uint8 discard_logic_tunnel_match = 0;
    EpeNextHopCtl_m epe_next_hop_ctl;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLocalPhyPort", HdrProcInputInfoFifo0_t + slice, 0, &pi_local_phy_port);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piPayloadOperation", HdrProcInputInfoFifo0_t + slice, 0, &pi_payload_operation);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIngressEditEn", HdrProcInputInfoFifo0_t + slice, 0, &pi_ingress_edit_en);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLogicPortType", EpeNextHopHdrAdjPiFifo0_t + slice, 0, &pi_src_is_logic_tunnel);

    sal_memset(&ds_dest_port, 0, sizeof(ds_dest_port));
    cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, (pi_local_phy_port + (slice * CTC_DKIT_ONE_SLICE_PORT_NUM)),  cmd, &ds_dest_port);
    GetDsDestPort(A, logicPortType_f, &ds_dest_port,  &logic_port_type);

    sal_memset(&epe_next_hop_ctl, 0, sizeof(epe_next_hop_ctl));
    cmd = DRV_IOR(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0,  cmd, &epe_next_hop_ctl);
    GetEpeNextHopCtl(A, discardLogicTunnelMatch_f, &epe_next_hop_ctl,  &discard_logic_tunnel_match);

    dest_is_logic_tunnel = logic_port_type && (PAYLOADOPERATION_BRIDGE_VPLS == pi_payload_operation);
    bridge_operation = (((pi_payload_operation == PAYLOADOPERATION_BRIDGE) || (pi_payload_operation == PAYLOADOPERATION_BRIDGE_VPLS) || (pi_payload_operation == PAYLOADOPERATION_BRIDGE_INNER)) && (!pi_ingress_edit_en))
                                       || (pi_payload_operation && pi_ingress_edit_en);

    if (discard_logic_tunnel_match && pi_src_is_logic_tunnel && dest_is_logic_tunnel && bridge_operation)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_VPLS_HORIZON_SPLIT_DIS_HORIZON;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_VPLS_HORIZON_SPLIT_DIS_E_TREE;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_epe_uc_mc_flooding_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;
    uint8 pi_mac_known = 0;
    uint8 mcast_mac = 0;
    uint8 bcast_mac = 0;
    uint32 pr_mac_da_47_18 = 0;
    uint32 pr_mac_da_17_0 = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piMacKnown", HdrProcInputInfoFifo0_t + slice, 0, &pi_mac_known);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prMacDa_47_18_", EpeNextHopPrFifo0_t + slice, 0, &pr_mac_da_47_18);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prMacDa_17_0_", EpeNextHopPrFifo0_t + slice, 0, &pr_mac_da_17_0);

    if ((0x3FFFFFFF == pr_mac_da_47_18) && (0x3FFFF == pr_mac_da_17_0))
    {
        bcast_mac = 1;
    }
    if (IS_BIT_SET(pr_mac_da_47_18, 22) && (!bcast_mac))
    {
        mcast_mac = 1;
    }

    if ((!pi_mac_known) && (!mcast_mac) && (!bcast_mac))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_UC_MC_FLOODING_DIS_UNKNOWN_UC;
    }
    if (pi_mac_known && (!mcast_mac) && (!bcast_mac))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_UC_MC_FLOODING_DIS_KNOWN_UC;
    }
    if ((!pi_mac_known) && mcast_mac)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_UC_MC_FLOODING_DIS_UNKNOWN_MC;
    }
    if (pi_mac_known && mcast_mac)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_UC_MC_FLOODING_DIS_KNOWN_MC;
    }
    if (bcast_mac)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_UC_MC_FLOODING_DIS_BC;
    }

    return ret;
}


STATIC int32
_ctc_goldengate_dkit_bus_get_reason_epe_ttl_fail(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;
    uint32 pi_payload_operation = 0;
    uint32 pr_layer3_type = 0;
    uint32 pi_packet_ttl = 0;
    uint32 pi_mcast_ttl_threshold = 0;
    uint32 pi_ttl_no_decrease = 0;

    uint32 discard_route_ttl_0 = 0;
    EpePktProcCtl_m epe_pkt_proc_ctl;
    uint32 new_ttl = 0;


    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piPayloadOperation", HdrProcInputInfoFifo0_t + slice, 0, &pi_payload_operation);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer3Type", HdrProcInputInfoFifo0_t + slice, 0, &pr_layer3_type);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piPacketTtl", HdrProcInputInfoFifo0_t + slice, 0, &pi_packet_ttl);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piMcastTtlThreshold", HdrProcInputInfoFifo0_t + slice, 0, &pi_mcast_ttl_threshold);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piTtlNoDecrease", HdrProcInputInfoFifo0_t + slice, 0, &pi_ttl_no_decrease);

    sal_memset(&epe_pkt_proc_ctl, 0, sizeof(epe_pkt_proc_ctl));
    cmd = DRV_IOR(EpePktProcCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0,  cmd, &epe_pkt_proc_ctl);
    GetEpePktProcCtl(A, discardRouteTtl0_f, &epe_pkt_proc_ctl,  &discard_route_ttl_0);


    if (((pi_payload_operation == PAYLOADOPERATION_ROUTE) || (pi_payload_operation == PAYLOADOPERATION_ROUTE_NOTTL) || (pi_payload_operation == PAYLOADOPERATION_ROUTE_COMPACT))
        && ((pr_layer3_type == L3TYPE_IP) || (pr_layer3_type == L3TYPE_IPV4) || (pr_layer3_type == L3TYPE_IPV6)))
    {
        if ((pi_payload_operation == PAYLOADOPERATION_ROUTE_NOTTL) && (!pi_ttl_no_decrease))
        {
            new_ttl = (pi_packet_ttl > 1) ? (pi_packet_ttl - 1):0;
        }
        else
        {
            new_ttl = pi_packet_ttl;
        }

        if ((0 == new_ttl) && discard_route_ttl_0)
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_TTL_FAIL_ROUTE_TTL_0;
        }
        else
        {
            p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_TTL_FAIL_ROUTE_MCAST;
            p_info->discard_reason_bus.value[0] = new_ttl;
            p_info->discard_reason_bus.value[1] = pi_mcast_ttl_threshold;
            p_info->discard_reason_bus.value_type = CTC_DKIT_DISCARD_VALUE_TYPE_2;
        }
    }
    else if (((pi_payload_operation == PAYLOADOPERATION_ROUTE) || (pi_payload_operation == PAYLOADOPERATION_ROUTE_NOTTL) || (pi_payload_operation == PAYLOADOPERATION_ROUTE_COMPACT))
        && ((pr_layer3_type == L3TYPE_MPLS) || (pr_layer3_type == L3TYPE_MPLSUP)))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_TTL_FAIL_MPLS;
    }
    else if (((pi_payload_operation == PAYLOADOPERATION_ROUTE) || (pi_payload_operation == PAYLOADOPERATION_ROUTE_NOTTL) || (pi_payload_operation == PAYLOADOPERATION_ROUTE_COMPACT))
        && ((pr_layer3_type == L3TYPE_TRILL)))
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_TTL_FAIL_TRILL;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_epe_net_pt_icmp_err(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;
    uint32 pr_layer3_type = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer3Type", HdrProcInputInfoFifo0_t + slice, 0, &pr_layer3_type);

    if (L3TYPE_IPV4 == pr_layer3_type)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_NAT_PT_ICMP_ERR_IPV4;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_NAT_PT_ICMP_ERR_IPV6;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_epe_oam_filtering_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;
    uint32 pi_local_phy_port = 0;
    uint32 pi_is_up = 0;
    uint32 pi_from_cpu_or_oam = 0;
    uint32 pr_layer3_type = 0;
    uint32 pi_link_or_section_oam = 0;

    uint32 ether_oam_edge_port = 0;
    uint32 link_oam_en = 0;
    uint32 link_oam_level = 0;
    uint32 transmit_disable = 0;
    DsDestPort_m ds_dest_port;
    uint32 link_oam_discard_en = 0;
    EpeNextHopCtl_m epe_next_hop_ctl;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLocalPhyPort", HdrProcInputInfoFifo0_t + slice, 0, &pi_local_phy_port);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer3Type", HdrProcInputInfoFifo0_t + slice, 0, &pr_layer3_type);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIsUp", HdrProcInputInfoFifo0_t + slice, 0, &pi_is_up);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piFromCpuOrOam", HdrProcInputInfoFifo0_t + slice, 0, &pi_from_cpu_or_oam);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLinkOrSectionOam", HdrProcInputInfoFifo0_t + slice, 0, &pi_link_or_section_oam);


    sal_memset(&ds_dest_port, 0, sizeof(ds_dest_port));
    cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, (pi_local_phy_port + (slice * CTC_DKIT_ONE_SLICE_PORT_NUM)),  cmd, &ds_dest_port);
    GetDsDestPort(A, etherOamEdgePort_f, &ds_dest_port,  &ether_oam_edge_port);
    GetDsDestPort(A, linkOamEn_f, &ds_dest_port,  &link_oam_en);
    GetDsDestPort(A, linkOamLevel_f, &ds_dest_port,  &link_oam_level);
    GetDsDestPort(A, transmitDisable_f, &ds_dest_port,  &transmit_disable);

    sal_memset(&epe_next_hop_ctl, 0, sizeof(epe_next_hop_ctl));
    cmd = DRV_IOR(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0,  cmd, &epe_next_hop_ctl);
    GetEpeNextHopCtl(A, linkOamDiscardEn_f, &epe_next_hop_ctl,  &link_oam_discard_en);

    if (!((pr_layer3_type == L3TYPE_ETHEROAM) && pi_link_or_section_oam ) && transmit_disable && pi_is_up)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_OAM_FILTERING_DIS_PORT;
    }
    else if (pi_is_up && pi_from_cpu_or_oam && (pr_layer3_type == L3TYPE_ETHEROAM) && ether_oam_edge_port)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_OAM_FILTERING_DIS_EDGE;
    }


    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_epe_same_ipda_ipsa_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;
    uint32 pr_layer2_type = 0;
    uint8 pr_mac_da[8] = {0};
    uint8 pr_mac_sa[8] = {0};

    uint32 discard_same_mac_addr = 0;
    EpeNextHopCtl_m epe_next_hop_ctl;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer2Type", HdrProcInputInfoFifo0_t + slice, 0, &pr_layer2_type);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prMacDa", HdrProcInputInfoFifo0_t + slice, 0, pr_mac_da);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "prMacSa", HdrProcInputInfoFifo0_t + slice, 0, pr_mac_sa);

    sal_memset(&epe_next_hop_ctl, 0, sizeof(epe_next_hop_ctl));
    cmd = DRV_IOR(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0,  cmd, &epe_next_hop_ctl);
    GetEpeNextHopCtl(A, discardSameMacAddr_f, &epe_next_hop_ctl,  &discard_same_mac_addr);

    if (((pr_layer2_type == L2TYPE_ETHV2) || (pr_layer2_type == L2TYPE_ETHSAP) || (pr_layer2_type == L2TYPE_ETHSNAP)) && (0 == sal_memcmp(pr_mac_da, pr_mac_sa, 8)) && discard_same_mac_addr)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_SAME_IPDA_IPSA_DIS_MAC;
    }
    else
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_SAME_IPDA_IPSA_DIS_IP;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_epe_dest_vlan_ptr_dis(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;
    uint32 pi_dest_vlan_ptr = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "destVlanPtr", EpeNextHopDsNextHopFifo0_t + slice, 0, &pi_dest_vlan_ptr);

    if (pi_dest_vlan_ptr == 0x1FFE)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_DEST_VLAN_PTR_DIS_1FFE;
    }
    else if (pi_dest_vlan_ptr == 0x1FFD)
    {
        p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_EPE_DEST_VLAN_PTR_DIS_1FFD;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_epe(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 pi_local_phy_port = 0;
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;
    uint32 global_dest_port = 0;
    uint32 cmd = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLocalPhyPort", HdrProcInputInfoFifo0_t + slice, 0, &pi_local_phy_port);

    cmd = DRV_IOR(DsDestPort_t, DsDestPort_globalDestPort_f);
    DRV_IOCTL(lchip, (pi_local_phy_port + slice*CTC_DKIT_ONE_SLICE_PORT_NUM), cmd, &global_dest_port);
    p_info->discard_reason_bus.gport = CTC_DKIT_DRV_GPORT_TO_CTC_GPORT(global_dest_port);

    switch (p_info->discard_reason[0].reason_id)
    {
             /*case  CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_HDR_ADJ_PKT_ERR:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_HDR_ADJ_REMOVE_ERR:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_DS_DESTPHYPORT_DSTID_DIS:*/
             /*    break;*/
        case CTC_DKIT_EPE_PORT_ISO_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_epe_port_iso_dis(p_info);
            break;
             /*case CTC_DKIT_EPE_DS_VLAN_TRANSMIT_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_BRG_TO_SAME_PORT_DIS:*/
             /*    break;*/
        case CTC_DKIT_EPE_VPLS_HORIZON_SPLIT_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_epe_vpls_horizon_split_dis(p_info);
            break;
             /*case CTC_DKIT_EPE_VLAN_FILTER_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_STP_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_PARSER_LEN_ERR:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_PBB_CHK_DIS:*/
             /*    break;*/
        case CTC_DKIT_EPE_UC_MC_FLOODING_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_epe_uc_mc_flooding_dis(p_info);
            break;
             /*case CTC_DKIT_EPE_OAM_802_3_DIS:*/
             /*    break;*/
        case CTC_DKIT_EPE_TTL_FAIL:
            ret = _ctc_goldengate_dkit_bus_get_reason_epe_ttl_fail(p_info);
            break;
             /*case CTC_DKIT_EPE_REMOTE_MIRROR_ESCAP_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_TUNNEL_MTU_CHK_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_INTERF_MTU_CHK_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_LOGIC_PORT_CHK_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_DS_ACL_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_DS_VLAN_XLATE_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_POLICING_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_CRC_ERR_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_ROUTE_PLD_OP_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_BRIDGE_PLD_OP_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_PT_L4_OFFSET_LARGE:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_BFD_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_PORT_REFLECTIVE_CHK_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_IP_MPLS_TTL_CHK_ERR:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_OAM_EDGE_PORT_DIS:*/
             /*    break;*/
        case CTC_DKIT_EPE_NAT_PT_ICMP_ERR:
            ret = _ctc_goldengate_dkit_bus_get_reason_epe_net_pt_icmp_err(p_info);
            break;
             /*case CTC_DKIT_EPE_LATENCY_DISCARD:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_LOCAL_OAM_DIS:*/
             /*    break;*/
        case CTC_DKIT_EPE_OAM_FILTERING_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_epe_oam_filtering_dis(p_info);
            break;
             /*case CTC_DKIT_EPE_OAM_HASH_CONFILICT_DIS:*/
             /*    break;*/
        case CTC_DKIT_EPE_SAME_IPDA_IPSA_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_epe_same_ipda_ipsa_dis(p_info);
            break;
             /*case CTC_DKIT_EPE_RESERVED2:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_TRILL_PLD_OP_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_PBB_CHK_FAIL_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_DS_NEXTHOP_DATA_VIOLATE:*/
             /*    break;*/
        case CTC_DKIT_EPE_DEST_VLAN_PTR_DIS:
            ret = _ctc_goldengate_dkit_bus_get_reason_epe_dest_vlan_ptr_dis(p_info);
            break;
             /*case CTC_DKIT_EPE_DS_L3EDIT_DATA_VIOLATE1:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_DS_L3EDIT_DATA_VIOLATE2:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_DS_L3EDITNAT_DATA_VIOLATE:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_DS_L2EDIT_DATA_VIOLATE1:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_DS_L2EDIT_DATA_VIOLATE2:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_PKT_HDR_C2C_TTL_ZERO:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_PT_UDP_CHKSUM_ZERO:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_OAM_TO_LOCAL_DIS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_HW_HAR_ADJ:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_HW_NEXT_HOP:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_HW_L2_EDIT:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_HW_L3_EDIT:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_HW_INNER_L2:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_HW_PAYLOAD:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_HW_ACL_OAM:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_HW_CLASS:*/
             /*    break;*/
             /*case CTC_DKIT_EPE_HW_OAM:*/
             /*    break;*/
        default:
            break;
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_oam_exception(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 pi_exception = 0;
    uint8 lchip = p_info->lchip;
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piException", OamHdrEditPIInFifo_t , 0, &pi_exception);
    p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_OAM_EXCE_NON + pi_exception;
    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_reason_oam(ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    switch (p_info->discard_reason[0].reason_id)
    {
         /*case CTC_DKIT_OAM_LINK_AGG_NO_MEMBER:*/
         /*    break;*/
         /*case CTC_DKIT_OAM_HW_ERROR;*/
         /*   break;*/
        case CTC_DKIT_OAM_EXCEPTION:
            ret =_ctc_goldengate_dkit_bus_get_reason_oam_exception(p_info);
            break;
        default:
            break;
    }

    return ret;
}


int32
ctc_goldengate_dkit_bus_get_discard_sub_reason(ctc_dkit_discard_para_t* p_para, ctc_dkit_discard_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;

    DKITS_PTR_VALID_CHECK(p_info);
    DKITS_PTR_VALID_CHECK(p_para);
    CTC_DKIT_LCHIP_CHECK(p_para->lchip);

    p_info->discard_reason_bus.reason_id = CTC_DKIT_SUB_DISCARD_INVALIED;
    p_info->discard_reason_bus.gport = CTC_DKIT_INVALID_PORT;
    p_info->discard_reason_bus.valid = 1;
    p_info->lchip = p_para->lchip;

    switch (p_info->discard_flag)
    {
        case CTC_DKIT_DISCARD_FLAG_IPE:
            ret = _ctc_goldengate_dkit_bus_get_reason_ipe(p_info);
            break;
        case CTC_DKIT_DISCARD_FLAG_EPE:
            ret = _ctc_goldengate_dkit_bus_get_reason_epe(p_info);
            break;
        case CTC_DKIT_DISCARD_FLAG_BSR:
            break;
        case CTC_DKIT_DISCARD_FLAG_OAM:
            ret = _ctc_goldengate_dkit_bus_get_reason_oam(p_info);
            break;
        default:
            break;
    }


    return ret;
}

#define PATH_____________________________
STATIC int32
_ctc_goldengate_dkit_bus_check_discard(uint32 module, ctc_dkit_bus_path_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = 0;
    uint32 piDiscard = 0;
    uint32 piDiscardType = 0;
    uint8 lchip = p_info->lchip;


    return CLI_SUCCESS;

    if (CTC_DKIT_IPE == module)
    {
        slice = p_info->slice_ipe;

        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscard", IpeFwdOamFifo0_t + slice, 0, &piDiscard);
        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscardType", IpeFwdOamFifo0_t + slice, 0, &piDiscardType);
        if (piDiscard)
        {
            p_info->discard = piDiscard;
            p_info->discard_type = piDiscardType;
            return ret;
        }

        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscard", IpeHdrAdjLpbkPiFifo0_t + slice, 0, &piDiscard);
        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscardType", IpeHdrAdjLpbkPiFifo0_t + slice, 0, &piDiscardType);
        if (piDiscard)
        {
            p_info->discard = piDiscard;
            p_info->discard_type = piDiscardType;
            return ret;
        }

        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscard", Mpls0TrackFifo0_t + slice, 0, &piDiscard);
        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscardType", Mpls0TrackFifo0_t + slice, 0, &piDiscardType);
        if (piDiscard)
        {
            p_info->discard = piDiscard;
            p_info->discard_type = piDiscardType;
            return ret;
        }

        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscard", UserIdImFifo0_t + slice, 0, &piDiscard);
        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscardType", UserIdImFifo0_t + slice, 0, &piDiscardType);
        if (piDiscard)
        {
            p_info->discard = piDiscard;
            p_info->discard_type = piDiscardType;
            return ret;
        }

        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscard", UserIdLkupPrepFifo0_t + slice, 0, &piDiscard);
        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscardType", UserIdLkupPrepFifo0_t + slice, 0, &piDiscardType);
        if (piDiscard)
        {
            p_info->discard = piDiscard;
            p_info->discard_type = piDiscardType;
            return ret;
        }

        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscard", IpeAclLkupInfoFifo0_t + slice, 0, &piDiscard);
        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscardType", IpeAclLkupInfoFifo0_t + slice, 0, &piDiscardType);
        if (piDiscard)
        {
            p_info->discard = piDiscard;
            p_info->discard_type = piDiscardType;
            return ret;
        }

        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscard", IpeLkupKeyGenPiTrackFifo0_t + slice, 0, &piDiscard);
        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscardType", IpeLkupKeyGenPiTrackFifo0_t + slice, 0, &piDiscardType);
        if (piDiscard)
        {
            p_info->discard = piDiscard;
            p_info->discard_type = piDiscardType;
            return ret;
        }

        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscard", IpePktProcPktInfoFifo0_t + slice, 0, &piDiscard);
        ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDiscardType", IpePktProcPktInfoFifo0_t + slice, 0, &piDiscardType);
        if (piDiscard)
        {
            p_info->discard = piDiscard;
            p_info->discard_type = piDiscardType;
            return ret;
        }

    }


    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_bus_get_path_info(ctc_dkit_path_stats_t* cur_stats, ctc_dkit_bus_path_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint32 to_hdr_proc_info_cnt = 0;
    uint32 to_lkup_mgr_cnt = 0;
    uint8 lchip = p_info->lchip;


    cmd = DRV_IOR(IpeIntfMapDebugStats0_t, IpeIntfMapDebugStats0_toLkupMgrCnt_f);
    DRV_IOCTL(lchip, 0, cmd, &to_lkup_mgr_cnt);
    if (to_lkup_mgr_cnt != cur_stats->ipe_stats[0])
    {
        cur_stats->ipe_stats[0] = to_lkup_mgr_cnt;
        p_info->slice_ipe = 0;
        p_info->is_ipe_path = 1;
    }
    else
    {
        cmd = DRV_IOR(IpeIntfMapDebugStats1_t, IpeIntfMapDebugStats0_toLkupMgrCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &to_lkup_mgr_cnt);
        if (to_lkup_mgr_cnt != cur_stats->ipe_stats[1])
        {
            cur_stats->ipe_stats[1] = to_lkup_mgr_cnt;
            p_info->slice_ipe = 1;
            p_info->is_ipe_path = 1;
        }
    }

    cmd = DRV_IOR(EpeNextHopDebugStats0_t, EpeNextHopDebugStats0_toHdrProcInfoCnt_f);
    DRV_IOCTL(lchip, 0, cmd, &to_hdr_proc_info_cnt);
    if (to_hdr_proc_info_cnt != cur_stats->epe_stats[0])
    {
        cur_stats->epe_stats[0] = to_hdr_proc_info_cnt;
        p_info->slice_epe = 0;
        p_info->is_epe_path = 1;
    }
    else
    {
        cmd = DRV_IOR(EpeNextHopDebugStats1_t, EpeNextHopDebugStats1_toHdrProcInfoCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &to_hdr_proc_info_cnt);
        if (to_hdr_proc_info_cnt != cur_stats->epe_stats[1])
        {
            cur_stats->epe_stats[1] = to_hdr_proc_info_cnt;
            p_info->slice_epe = 1;
            p_info->is_epe_path = 1;
        }
    }

    p_info->is_bsr_path = 1;

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_path_ipe_parser(ctc_dkit_bus_path_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    char buf[CTC_DKITS_IPV6_ADDR_STR_LEN];
    ipv6_addr_t ipv6_address = {0, 0, 0, 0};
    uint8 mpls_num = 0;
    uint32 mpls_lable[8] = {0};
    uint8 i = 0;

    /*parser result*/
    uint32 prCtagCfi  = 0;
    uint32 prCtagCos  = 0;
    uint32 prCvlanId  = 0;
    uint32 prCvlanIdValid  = 0;
    uint32 prEtherType  = 0;
    uint32 prFragInfo  = 0;
    uint32 prIpHeaderError  = 0;
    uint32 prIpOptions  = 0;
    uint32 prIsIsatapInterface  = 0;
    uint32 prIsTcp  = 0;
    uint32 prIsUdp  = 0;
    uint32 prL4InfoMapped  = 0;
    uint32 prL4ErrorBits  = 0;
    uint32 prLayer2Type  = 0;
    uint32 prLayer3HeaderProtocol = 0;
    uint32 prLayer3Offset  = 0;
    uint32 prLayer3Type  = 0;
    uint32 prLayer4Offset  = 0;
    uint32 prLayer4Type  = 0;
    uint32 prLayer4UserType  = 0;
    uint32 prStagCfi  = 0;
    uint32 prStagCos  = 0;
    uint32 prSvlanId  = 0;
    uint32 prSvlanIdValid  = 0;
    uint32 prTtl  = 0;
    ipv6_addr_t ip_da = {0};
    ipv6_addr_t ip_sa = {0};
    uint32 prUL4Source[4]  = {0};
    uint32 prUL4Dest[4]  = {0};
    uint32 prUL3Tos  = 0;
    uint32 prUL4UserData[4]  = {0};
    mac_addr_t prMacDa = {0};
    mac_addr_t prMacSa = {0};
    hw_mac_addr_t hw_mac = {0};

    CTC_DKIT_PRINT("Parser Result:\n");

    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prCtagCfi", UserIdImFifo0_t + slice, 0, &prCtagCfi);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prCtagCos", UserIdImFifo0_t + slice, 0, &prCtagCos);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prCvlanId", UserIdImFifo0_t + slice, 0, &prCvlanId);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prCvlanIdValid", UserIdImFifo0_t + slice, 0, &prCvlanIdValid);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prEtherType", UserIdImFifo0_t + slice, 0, &prEtherType);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prFragInfo", UserIdImFifo0_t + slice, 0, &prFragInfo);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prIpHeaderError", UserIdImFifo0_t + slice, 0, &prIpHeaderError);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prIpOptions", UserIdImFifo0_t + slice, 0, &prIpOptions);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prIsIsatapInterface", UserIdImFifo0_t + slice, 0, &prIsIsatapInterface);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prIsTcp", UserIdImFifo0_t + slice, 0, &prIsTcp);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prIsUdp", UserIdImFifo0_t + slice, 0, &prIsUdp);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prL4InfoMapped", UserIdImFifo0_t + slice, 0, &prL4InfoMapped);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prL4ErrorBits", UserIdImFifo0_t + slice, 0, &prL4ErrorBits);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer2Type", UserIdImFifo0_t + slice, 0, &prLayer2Type);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer3HeaderProtocol", UserIdImFifo0_t + slice, 0, &prLayer3HeaderProtocol);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer3Offset", UserIdImFifo0_t + slice, 0, &prLayer3Offset);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer3Type", UserIdImFifo0_t + slice, 0, &prLayer3Type);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer4Offset", UserIdImFifo0_t + slice, 0, &prLayer4Offset);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer4Type", UserIdImFifo0_t + slice, 0, &prLayer4Type);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prLayer4UserType", UserIdImFifo0_t + slice, 0, &prLayer4UserType);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prMacDa", UserIdImFifo0_t + slice, 0, &hw_mac);
    CTC_DKIT_SET_USER_MAC(prMacDa, hw_mac);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prMacSa", UserIdImFifo0_t + slice, 0, &hw_mac);
    CTC_DKIT_SET_USER_MAC(prMacSa, hw_mac);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prStagCfi", UserIdImFifo0_t + slice, 0, &prStagCfi);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prStagCos", UserIdImFifo0_t + slice, 0, &prStagCos);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prSvlanId", UserIdImFifo0_t + slice, 0, &prSvlanId);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prSvlanIdValid", UserIdImFifo0_t + slice, 0, &prSvlanIdValid);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prTtl", UserIdImFifo0_t + slice, 0, &prTtl);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prUL3Source", UserIdImFifo0_t + slice, 0, &ip_sa);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prUL3Dest", UserIdImFifo0_t + slice, 0, &ip_da);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prUL4Source", UserIdImFifo0_t + slice, 0, &prUL4Source);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prUL4Dest", UserIdImFifo0_t + slice, 0, &prUL4Dest);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prUL3Tos", UserIdImFifo0_t + slice, 0, &prUL3Tos);
    ret = ctc_goldengate_dkit_drv_read_bus_field(lchip, "prUL4UserData", UserIdImFifo0_t + slice, 0, &prUL4UserData);

    CTC_DKIT_PATH_PRINT("%-15s:%02X%02X.%02X%02X.%02X%02X\n", "MAC DA",
                                                prMacDa[0], prMacDa[1], prMacDa[2], prMacDa[3], prMacDa[4], prMacDa[5]);
    CTC_DKIT_PATH_PRINT("%-15s:%02X%02X.%02X%02X.%02X%02X\n", "MAC SA",
                                                prMacSa[0], prMacSa[1], prMacSa[2], prMacSa[3], prMacSa[4], prMacSa[5]);
    CTC_DKIT_PATH_PRINT("%-15s:0x%04x\n", "ether type", prEtherType);
    if (prSvlanIdValid)
    {
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "svlan COS", prStagCos);
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "svlan CFI", prStagCfi);
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "svlan ID", prSvlanId);
    }
    if (prCvlanIdValid)
    {
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "cvlan COS", prCtagCos);
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "cvlan CFI", prCtagCfi);
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "cvlan ID", prCvlanId);
    }

    switch (prLayer3Type)
    {
    case L3TYPE_IPV4 :
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "l3 type", "IPv4");
        sal_inet_ntop(AF_INET, &ip_da[0], buf, CTC_DKITS_IPV6_ADDR_STR_LEN);
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "IP DA", buf);
        sal_inet_ntop(AF_INET, &ip_sa[0], buf, CTC_DKITS_IPV6_ADDR_STR_LEN);
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "IP SA", buf);
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "protocol", prLayer3HeaderProtocol);
        break;
    case L3TYPE_IPV6 :
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "l3 type", "IPv6");
        ipv6_address[0] = sal_htonl(ip_da[3]);
        ipv6_address[1] = sal_htonl(ip_da[2]);
        ipv6_address[2] = sal_htonl(ip_da[1]);
        ipv6_address[3] = sal_htonl(ip_da[0]);
        sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_DKITS_IPV6_ADDR_STR_LEN);
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "IP DA", buf);
        ipv6_address[0] = sal_htonl(ip_sa[3]);
        ipv6_address[1] = sal_htonl(ip_sa[2]);
        ipv6_address[2] = sal_htonl(ip_sa[1]);
        ipv6_address[3] = sal_htonl(ip_sa[0]);
        sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_DKITS_IPV6_ADDR_STR_LEN);
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "IP SA", buf);
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "protocol", prLayer3HeaderProtocol);
        break;
    case L3TYPE_MPLS :
    case L3TYPE_MPLSUP :
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "l3 type", "MPLS");
        mpls_lable[0] = sal_htonl(ip_da[3]);
        mpls_lable[1] = sal_htonl(ip_da[2]);
        mpls_lable[2] = sal_htonl(ip_da[1]);
        mpls_lable[3] = sal_htonl(ip_da[0]);
        mpls_lable[4] = sal_htonl(ip_sa[3]);
        mpls_lable[5] = sal_htonl(ip_sa[2]);
        mpls_lable[6] = sal_htonl(ip_sa[1]);
        mpls_lable[7] = sal_htonl(ip_sa[0]);
        mpls_num = prUL3Tos & 0xF;
        for (i = 0; i < mpls_num; i++)
        {
            CTC_DKIT_PATH_PRINT("%-15s%d:0x%08x\n", "lable", i, mpls_lable[i]);
        }
        break;
    case L3TYPE_ARP :
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "l3 type", "ARP");
        break;
    case L3TYPE_FCOE :
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "l3 type", "FCOE");
        break;
    case L3TYPE_TRILL :
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "l3 type", "TRILL");
        break;
    case L3TYPE_ETHEROAM :
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "l3 type", "ETHER OAM");
        ipv6_address[0] = sal_htonl(ip_da[3]);
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "level", (ipv6_address[0]>>13)&0x7);
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "version", (ipv6_address[0]>>8)&0x1F);
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "opcode", ipv6_address[0]&0xFF);
        ipv6_address[0] = sal_htonl(ip_sa[1]);
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "rxFcb", ipv6_address[0]);
        ipv6_address[0] = sal_htonl(ip_sa[2]);
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "txFcf", ipv6_address[0]);
        ipv6_address[0] = sal_htonl(ip_sa[3]);
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "txFcb", ipv6_address[0]);
        break;
    case L3TYPE_SLOWPROTO :
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "l3 type", "SLOW PROTOCOL");
        break;
    case L3TYPE_CMAC :
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "l3 type", "CMAC");
        break;
    case L3TYPE_PTP :
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "l3 type", "PTP");
        break;
    default:
        break;
    }


    return ret;
}

STATIC char*
_ctc_goldengate_dkit_bus_get_igr_scl_key_desc(uint32 key_type)
{
    switch (key_type)
    {
        case USERIDHASHTYPE_DISABLE:
            return "";
        case USERIDHASHTYPE_DOUBLEVLANPORT:
            return "DsUserIdDoubleVlanPortHashKey";
        case USERIDHASHTYPE_SVLANPORT:
            return "DsUserIdSvlanPortHashKey";
        case USERIDHASHTYPE_CVLANPORT:
            return "DsUserIdCvlanPortHashKey";
        case USERIDHASHTYPE_SVLANCOSPORT:
            return "DsUserIdSvlanCosPortHashKey";
        case USERIDHASHTYPE_CVLANCOSPORT:
            return "DsUserIdCvlanCosPortHashKey";
        case USERIDHASHTYPE_MACPORT:
            return "DsUserIdMacPortHashKey";
        case USERIDHASHTYPE_IPV4PORT:
            return "DsUserIdIpv4PortHashKey";
        case USERIDHASHTYPE_MAC:
            return "DsUserIdMacHashKey";
        case USERIDHASHTYPE_IPV4SA:
            return "DsUserIdIpv4SaHashKey";
        case USERIDHASHTYPE_PORT:
            return "DsUserIdPortHashKey";
        case USERIDHASHTYPE_SVLANMACSA:
            return "DsUserIdSvlanMacSaHashKey";
        case USERIDHASHTYPE_SVLAN:
            return "DsUserIdSvlanHashKey";
        case USERIDHASHTYPE_IPV6SA:
            return "DsUserIdIpv6SaHashKey";
        case USERIDHASHTYPE_IPV6PORT:
            return "DsUserIdIpv6PortHashKey";
        case USERIDHASHTYPE_TUNNELIPV4:
            return "DsUserIdTunnelIpv4HashKey";
        case USERIDHASHTYPE_TUNNELIPV4GREKEY:
            return "DsUserIdTunnelIpv4GreKeyHashKey";
        case USERIDHASHTYPE_TUNNELIPV4UDP:
            return "DsUserIdTunnelIpv4UdpHashKey";
        case USERIDHASHTYPE_TUNNELPBB:
            return "DsUserIdTunnelPbbHashKey";
        case USERIDHASHTYPE_TUNNELTRILLUCRPF:
            return "DsUserIdTunnelTrillUcRpfHashKey";
        case USERIDHASHTYPE_TUNNELTRILLUCDECAP:
            return "DsUserIdTunnelTrillMcDecapHashKey";
        case USERIDHASHTYPE_TUNNELTRILLMCRPF:
            return "DsUserIdTunnelTrillMcRpfHashKey";
        case USERIDHASHTYPE_TUNNELTRILLMCDECAP:
            return "DsUserIdTunnelTrillMcDecapHashKey";
        case USERIDHASHTYPE_TUNNELTRILLMCADJ:
            return "DsUserIdTunnelTrillMcAdjHashKey";
        case USERIDHASHTYPE_TUNNELIPV4RPF:
            return "DsUserIdTunnelIpv4RpfHashKey";
        case USERIDHASHTYPE_TUNNELIPV4UCVXLANMODE0:
            return "DsUserIdTunnelIpv4UcVxlanMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV4UCVXLANMODE1:
            return "DsUserIdTunnelIpv4UcVxlanMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV6UCVXLANMODE0:
            return "DsUserIdTunnelIpv6UcVxlanMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV6UCVXLANMODE1:
            return "DsUserIdTunnelIpv6UcVxlanMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV4UCNVGREMODE0:
            return "DsUserIdTunnelIpv4UcNvgreMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV4UCNVGREMODE1:
            return "DsUserIdTunnelIpv4UcNvgreMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV6UCNVGREMODE0:
            return "DsUserIdTunnelIpv6UcNvgreMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV6UCNVGREMODE1:
            return "DsUserIdTunnelIpv6UcNvgreMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV4MCVXLANMODE0:
            return "DsUserIdTunnelIpv4McVxlanMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV4VXLANMODE1:
            return "DsUserIdTunnelIpv4VxlanMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV6MCVXLANMODE0:
            return "DsUserIdTunnelIpv6McVxlanMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV6MCVXLANMODE1:
            return "DsUserIdTunnelIpv6McVxlanMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV4MCNVGREMODE0:
            return "DsUserIdTunnelIpv4McNvgreMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV4NVGREMODE1:
            return "DsUserIdTunnelIpv4NvgreMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV6MCNVGREMODE0:
            return "DsUserIdTunnelIpv6McNvgreMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV6MCNVGREMODE1:
            return "DsUserIdTunnelIpv6McNvgreMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV4DA:
            return "DsUserIdTunnelIpv4DaHashKey";
        case USERIDHASHTYPE_TUNNELMPLS:
            return "DsUserIdTunnelMplsHashKey";
        case USERIDHASHTYPE_SCLFLOWL2:
            return "DsUserIdSclFlowL2HashKey";
        default:
            return "";
    }
}

STATIC int32
_ctc_goldengate_dkit_bus_path_ipe_scl(ctc_dkit_bus_path_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;

    /*UserId0AdReqFifo*/
    uint32 hash0_adIndex = 0;

    /*UserId1AdReqFifo*/
    uint32 hash1_adIndex = 0;

    /*UserIdLkupPrepFifo*/
    uint32 scl_userIdHash1Type = 0;
    uint32 scl_userIdHash2Type = 0;
    uint32 scl_sclFlowHashEn = 0;
    uint32 scl_userIdTcam1En = 0;
    uint32 scl_userIdTcam2En = 0;

    /*UserIdHashRes1Fifo*/
    uint32 hash0_lookupResultValid = 0;
    uint32 hash0_defaultEntryValid = 0;

    /*UserIdHashRes2Fifo*/
    uint32 hash1_lookupResultValid = 0;
    uint32 hash1_defaultEntryValid = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "addr", UserId0AdReqFifo0_t + slice, 0, &hash0_adIndex);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "addr", UserId1AdReqFifo0_t + slice, 0, &hash1_adIndex);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "TempInfoIpeUserId_userIdHash1Type", UserIdLkupPrepFifo0_t + slice, 0, &scl_userIdHash1Type);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "TempInfoIpeUserId_userIdHash2Type", UserIdLkupPrepFifo0_t + slice, 0, &scl_userIdHash2Type);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "dsPhyPortExt_sclFlowHashEn", UserIdLkupPrepFifo0_t + slice, 0, &scl_sclFlowHashEn);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "TempInfoIpeUserId_userIdTcam1En", UserIdLkupPrepFifo0_t + slice, 0, &scl_userIdTcam1En);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "TempInfoIpeUserId_userIdTcam2En", UserIdLkupPrepFifo0_t + slice, 0, &scl_userIdTcam2En);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "userIdHash1IpeResultValid", UserIdHashRes1Fifo0_t + slice, 0, &hash0_lookupResultValid);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "userIdHash1IpeDefaultEntryValid", UserIdHashRes1Fifo0_t + slice, 0, &hash0_defaultEntryValid);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "userIdHash2IpeResultValid", UserIdHashRes2Fifo0_t + slice, 0, &hash1_lookupResultValid);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "userIdHash2IpeDefaultEntryValid", UserIdHashRes2Fifo0_t + slice, 0, &hash1_defaultEntryValid);

    if (scl_userIdHash1Type || scl_userIdHash2Type || scl_userIdTcam1En || scl_userIdTcam2En)
    {
        CTC_DKIT_PRINT("SCL Process:\n");
        /*hash0*/
        if (scl_userIdHash1Type)
        {
            CTC_DKIT_PATH_PRINT("%-15s\n", "do HASH0 lookup->");
            if (hash0_defaultEntryValid)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_BUS_DEFAULT);
            }
            else if (hash0_lookupResultValid)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_BUS_HASH_HIT);
            }
            else
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_BUS_NOT_HIT);
            }
            if (hash0_defaultEntryValid || hash0_lookupResultValid)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", _ctc_goldengate_dkit_bus_get_igr_scl_key_desc(scl_userIdHash1Type));
                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", hash0_adIndex);
                if (scl_sclFlowHashEn)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsSclFlow");
                }
                else if (scl_userIdHash1Type && (scl_userIdHash1Type < USERIDHASHTYPE_TUNNELIPV4))
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsUserId");
                }
                else if (scl_userIdHash1Type >= USERIDHASHTYPE_TUNNELIPV4)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsTunnelId");
                }
            }
        }
        /*hash1*/
        if (scl_userIdHash2Type)
        {
            CTC_DKIT_PATH_PRINT("%-15s\n", "do HASH1 lookup->");
            if (hash1_defaultEntryValid)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_BUS_DEFAULT);
            }
            else if (hash1_lookupResultValid)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_BUS_HASH_HIT);
            }
            else
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_BUS_NOT_HIT);
            }
            if (hash1_defaultEntryValid || hash1_lookupResultValid)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", _ctc_goldengate_dkit_bus_get_igr_scl_key_desc(scl_userIdHash2Type));
                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", hash1_adIndex);
                if (scl_sclFlowHashEn)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsSclFlow");
                }
                else if (scl_userIdHash2Type && (scl_userIdHash2Type < USERIDHASHTYPE_TUNNELIPV4))
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsUserId");
                }
                else if (scl_userIdHash2Type >= USERIDHASHTYPE_TUNNELIPV4)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsTunnelId");
                }
            }
        }
        /*tcam0*/
        if (scl_userIdTcam1En)
        {
            CTC_DKIT_PATH_PRINT("%-15s\n", "TCAM0 lookup->");
        }
        /*tcam1*/
        if (scl_userIdTcam2En)
        {
            CTC_DKIT_PATH_PRINT("%-15s\n", "TCAM1 lookup->");
        }

    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_path_ipe_interface(ctc_dkit_bus_path_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint16 gport = 0;
    char* vlan_action[4] = {"None", "Replace", "Add","Delete"};
    MsPacketHeader_m ms_packet_header;

    uint32 routedPort = 0;
    uint32 mplsEn = 0;
    /*UserIdImFifo*/
    uint32 piLocalPhyPort = 0;
    uint32 piGlobalSrcPort = 0;
    uint32 piOuterVlanIsCVlan = 0;
    uint32 piUserVlanPtrValid = 0;
    uint32 piUserVlanPtr = 0;
    /*IpePktProcPktInfoFifo*/
    uint32 piInterfaceId = 0;
    /*IpeAclLkupInfoFifo*/
    uint32 piBridgeEn = 0;
    uint32 piIsRouterMac = 0;

    /*MsPacketHeader*/
    uint32 svlanTagOperationValid = 0;
    uint32 sTagAction = 0;
    uint32 sourceCos = 0;
    uint32 sourceCfi = 0;
    uint32 srcVlanId = 0;
    uint32 cvlanTagOperationValid = 0;
    uint32 cTagAction = 0;
    uint32 srcCtagCos = 0;
    uint32 srcCtagCfi = 0;
    uint32 srcCvlanId = 0;
    uint32 operationType = 0;

    CTC_DKIT_PRINT("Interface Process:\n");

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLocalPhyPort", UserIdImFifo0_t + slice, 0, &piLocalPhyPort);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piGlobalSrcPort", UserIdImFifo0_t + slice, 0, &piGlobalSrcPort);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piOuterVlanIsCVlan", UserIdImFifo0_t + slice, 0, &piOuterVlanIsCVlan);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piUserVlanPtrValid", UserIdImFifo0_t + slice, 0, &piUserVlanPtrValid);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piUserVlanPtrValid", UserIdImFifo0_t + slice, 0, &piUserVlanPtrValid);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piInterfaceId", IpePktProcPktInfoFifo0_t + slice, 0, &piInterfaceId);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piBridgeEn", IpeAclLkupInfoFifo0_t + slice, 0, &piBridgeEn);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIsRouterMac", IpeAclLkupInfoFifo0_t + slice, 0, &piIsRouterMac);

    if (CTC_DKIT_DRV_GPORT_IS_LINKAGG_PORT(piGlobalSrcPort))
    {
        gport = CTC_DKIT_DRV_LPORT_TO_CTC_GPORT(0, (piLocalPhyPort + slice*CTC_DKIT_ONE_SLICE_PORT_NUM));
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "linkagg group", CTC_DKIT_DRV_GPORT_TO_LINKAGG_ID(piGlobalSrcPort));
    }
    else
    {
        gport = CTC_DKIT_DRV_GPORT_TO_CTC_GPORT(piGlobalSrcPort);
    }
    CTC_DKIT_PATH_PRINT("%-15s:0x%04x\n", "source gport", gport);

    if (piUserVlanPtrValid)
    {
        CTC_DKIT_PATH_PRINT("%-15s:%d, from SCL\n", "vlan ptr", piUserVlanPtr);
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "vlan domain", piOuterVlanIsCVlan?"cvlan domain":"svlan domain");
    }

    if (piInterfaceId)
    {
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "l3 intf id", piInterfaceId);
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "route mac", piIsRouterMac?"YES":"NO");
    }

    /*bridge en*/
    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "bridge port", piBridgeEn? "YES" : "NO");
    /*route en*/
    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_routedPort_f);
    DRV_IOCTL(lchip, (piLocalPhyPort + slice*CTC_DKIT_ONE_SLICE_PORT_NUM), cmd, &routedPort);
    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "route port", routedPort? "YES" : "NO");

    /*mpls en*/
    if (piInterfaceId)
    {
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_mplsEn_f);
        DRV_IOCTL(lchip, piInterfaceId, cmd, &mplsEn);
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "MPLS port", mplsEn? "YES" : "NO");
    }


    if (p_info->is_epe_path)
    {
        sal_memset((uint8*)&ms_packet_header, 0, sizeof(ms_packet_header));
        cmd = DRV_IOR(EpeHdrAdjBrgHdrFifo0_t + p_info->slice_epe, 6);
        DRV_IOCTL(lchip, 0, cmd, &ms_packet_header);
        GetMsPacketHeader(A, svlanTagOperationValid_f, &ms_packet_header, &svlanTagOperationValid);
        GetMsPacketHeader(A, stagAction_f, &ms_packet_header, &sTagAction);
        GetMsPacketHeader(A, sourceCos_f, &ms_packet_header, &sourceCos);
        GetMsPacketHeader(A, sourceCfi_f, &ms_packet_header, &sourceCfi);
        GetMsPacketHeader(A, srcVlanId_f, &ms_packet_header, &srcVlanId);

        GetMsPacketHeader(A, u1_normal_cvlanTagOperationValid_f, &ms_packet_header, &cvlanTagOperationValid);
        GetMsPacketHeader(A, u1_normal_cTagAction_f, &ms_packet_header, &cTagAction);
        GetMsPacketHeader(A, u1_normal_srcCtagCos_f, &ms_packet_header, &srcCtagCos);
        GetMsPacketHeader(A, u1_normal_srcCtagCfi_f, &ms_packet_header, &srcCtagCfi);
        GetMsPacketHeader(A, u1_normal_srcCvlanId_f, &ms_packet_header, &srcCvlanId);
        GetMsPacketHeader(A, operationType_f, &ms_packet_header, &operationType);

        if ((svlanTagOperationValid && sTagAction) || (cvlanTagOperationValid && cTagAction && (OPERATIONTYPE_NORMAL == operationType)))
        {
            CTC_DKIT_PATH_PRINT("%-15s\n", "vlan edit->");
        }
        if (svlanTagOperationValid)
        {

            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "svlan edit", vlan_action[sTagAction]);
            if ((VTAGACTIONTYPE_MODIFY == sTagAction) || (VTAGACTIONTYPE_ADD == sTagAction))
            {
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new COS", sourceCos);
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new CFI", sourceCfi);
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new vlan id", srcVlanId);
            }
        }
        if ((cvlanTagOperationValid) && (OPERATIONTYPE_NORMAL == operationType))
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "cvlan edit", vlan_action[cTagAction]);
            if ((VTAGACTIONTYPE_MODIFY == cTagAction) || (VTAGACTIONTYPE_ADD == cTagAction))
            {
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new COS", srcCtagCos);
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new CFI", srcCtagCfi);
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new vlan id", srcCvlanId);
            }
        }
     }

    return CLI_SUCCESS;
}
STATIC int32
_ctc_goldengate_dkit_bus_path_ipe_tunnel(ctc_dkit_bus_path_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;

    /*IpePktProcPktInfoFifo*/
    uint32 piIsMplsSwitched = 0;
    /*Mpls0TrackFifo*/
    uint32 labelCount = 0;
    /*Mpls0ResFifo*/
    uint32 innerPacketLookup = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piIsMplsSwitched", IpePktProcPktInfoFifo0_t + slice, 0, &piIsMplsSwitched);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "MplsInfo_labelCount", Mpls0TrackFifo0_t + slice, 0, &labelCount);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "innerPacketLookup", Mpls0ResFifo0_t + slice, 0, &innerPacketLookup);
    if (piIsMplsSwitched)
    {
        CTC_DKIT_PRINT("MPLS Process:\n");
        if (labelCount )
        {
            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "pop lable", labelCount);
            if (innerPacketLookup)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "forward by", "inner packet");
            }
            else
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "forward by", "MPLS label");
            }
        }
        else
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "pop lable", "not pop any lable!!!");
        }
    }

    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_path_ipe_acl(ctc_dkit_bus_path_info_t* p_info)
{
    return CLI_SUCCESS;
}
STATIC int32
_ctc_goldengate_dkit_bus_path_ipe_forward(ctc_dkit_bus_path_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 addr = 0;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    char buf[CTC_DKITS_IPV6_ADDR_STR_LEN];
    ipv6_addr_t ipv6_address = {0, 0, 0, 0};

    char* l3_da_key_type[8] =
    {
        "IPv4 ucast",
        "IPv6 ucast",
        "IPv4 mcast",
        "IPv6 mcast",
        "FCOE",
        "TRILL ucast",
        "TRILL mcast",
        "Reserve"
    };

    /*FibInKeyFifo*/
    uint32 macDaLookupEn  = 0;
    uint32 macSaLookupEn  = 0;
    uint32 l3DaLookupEn  = 0;
    uint32 l3SaLookupEn  = 0;
    uint32 l3DaKeyType  = 0;
    uint32 l3SaKeyType  = 0;
    uint32 vsiId  = 0;
    uint32 vrfId  = 0;
    ipv6_addr_t ip_da = {0};
    ipv6_addr_t ip_sa = {0};
    mac_addr_t macDa = {0};
    /*mac_addr_t macSa = {0};*/
    hw_mac_addr_t hw_mac = {0};

    /*FibHost0KeyResultFifo*/
    uint32 keyIndex = 0;
    uint32 hashHit = 0;
    uint32  conflict = 0;
    uint32 adIndex = 0;

    /*DynamicDsHashHost0KeyLkupDbg*/
    uint32 host0KeyDbgConflict = 0;
    uint32 host0KeyDbgKeyIndex = 0;
    uint32 host0KeyDbgResultValid = 0;

    /*DynamicDsHashHost1KeyLkupDbg*/
    uint32 host1KeyDbgConflict = 0;
    uint32 host1KeyDbgKeyIndex = 0;
    uint32 host1KeyDbgResultValid = 0;



    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "macDaLookupEn", FibInKeyFifo0_t + slice, 0, &macDaLookupEn);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "macSaLookupEn", FibInKeyFifo0_t + slice, 0, &macSaLookupEn);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "l3DaLookupEn", FibInKeyFifo0_t + slice, 0, &l3DaLookupEn);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "l3SaLookupEn", FibInKeyFifo0_t + slice, 0, &l3SaLookupEn);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "l3DaKeyType", FibInKeyFifo0_t + slice, 0, &l3DaKeyType);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "l3SaKeyType", FibInKeyFifo0_t + slice, 0, &l3SaKeyType);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "macDa", FibInKeyFifo0_t + slice, 0, &hw_mac);
    CTC_DKIT_SET_USER_MAC(macDa, hw_mac);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "macSa", FibInKeyFifo0_t + slice, 0, &hw_mac);
    /*CTC_DKIT_SET_USER_MAC(macSa, hw_mac);*/
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "vsiId", FibInKeyFifo0_t + slice, 0, &vsiId);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "vrfId", FibInKeyFifo0_t + slice, 0, &vrfId);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "uDest", FibInKeyFifo0_t + slice, 0, &ip_da);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "uSource", FibInKeyFifo0_t + slice, 0, &ip_sa);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "keyIndex", FibHost0KeyResultFifo0_t + slice, 0, &keyIndex);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "hashHit", FibHost0KeyResultFifo0_t + slice, 0, &hashHit);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "conflict", FibHost0KeyResultFifo0_t + slice, 0, &conflict);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "adIndex", FibHost0KeyResultFifo0_t + slice, 0, &adIndex);

     ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "addr", DsIpDaReqFifo0_t + slice, 0, &addr);

    if (l3DaLookupEn)
    {
        CTC_DKIT_PRINT("Route Process:\n");
        CTC_DKIT_PATH_PRINT("%-15s\n", "IP DA lookup->");
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key type", l3_da_key_type[l3DaKeyType]);
        if ((FIBDAKEYTYPE_IPV4UCAST == l3DaKeyType) || (FIBDAKEYTYPE_IPV4MCAST == l3DaKeyType))
        {
            sal_inet_ntop(AF_INET, &ip_da[0], buf, CTC_DKITS_IPV6_ADDR_STR_LEN);
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key->IPDA", buf);
            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "key->VRF ID", vrfId);
        }
        else if ((FIBDAKEYTYPE_IPV6UCAST == l3DaKeyType) || (FIBDAKEYTYPE_IPV6MCAST == l3DaKeyType))
        {
            ipv6_address[0] = sal_htonl(ip_da[3]);
            ipv6_address[1] = sal_htonl(ip_da[2]);
            ipv6_address[2] = sal_htonl(ip_da[1]);
            ipv6_address[3] = sal_htonl(ip_da[0]);
            sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_DKITS_IPV6_ADDR_STR_LEN);
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key->IP DA", buf);
            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "key->VRF ID", vrfId);
        }
         /*host0 result*/
        tbl_id = DynamicDsHashHost0KeyLkupDbg0_t + slice;
        cmd = DRV_IOR(tbl_id, DynamicDsHashHost0KeyLkupDbg0_host0KeyDbgConflict_f);
        DRV_IOCTL(lchip, 0, cmd, &host0KeyDbgConflict);
        cmd = DRV_IOR(tbl_id, DynamicDsHashHost0KeyLkupDbg0_host0KeyDbgKeyIndex_f);
        DRV_IOCTL(lchip, 0, cmd, &host0KeyDbgKeyIndex);
        cmd = DRV_IOR(tbl_id, DynamicDsHashHost0KeyLkupDbg0_host0KeyDbgResultValid_f);
        DRV_IOCTL(lchip, 0, cmd, &host0KeyDbgResultValid);
         /*host1 result*/
        tbl_id = DynamicDsHashHost1KeyLkupDbg0_t + slice;
        cmd = DRV_IOR(tbl_id, DynamicDsHashHost1KeyLkupDbg0_host1KeyDbgConflict_f);
        DRV_IOCTL(lchip, 0, cmd, &host1KeyDbgConflict);
        cmd = DRV_IOR(tbl_id, DynamicDsHashHost1KeyLkupDbg0_host1KeyDbgKeyIndex_f);
        DRV_IOCTL(lchip, 0, cmd, &host1KeyDbgKeyIndex);
        cmd = DRV_IOR(tbl_id, DynamicDsHashHost1KeyLkupDbg0_host1KeyDbgResultValid_f);
        DRV_IOCTL(lchip, 0, cmd, &host1KeyDbgResultValid);


        if (host0KeyDbgConflict)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "host0 result", CTC_DKIT_BUS_HASH_HIT);
            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", host0KeyDbgKeyIndex);
        }
        else if (host0KeyDbgResultValid)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "host0 result", CTC_DKIT_BUS_HASH_HIT);
            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", host0KeyDbgKeyIndex);
            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", addr);
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsIpDa");
        }
        else
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "host0 result", CTC_DKIT_BUS_NOT_HIT);
        }
        if (host1KeyDbgConflict)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "host1 result", CTC_DKIT_BUS_HASH_HIT);
            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", host1KeyDbgKeyIndex);
        }
        else if (host1KeyDbgResultValid)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "host1 result", CTC_DKIT_BUS_HASH_HIT);
            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", host1KeyDbgKeyIndex);
            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", adIndex);
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsIpDa");
        }
        else
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "host1 result", CTC_DKIT_BUS_NOT_HIT);
        }

    }
    else if (macDaLookupEn)
    {

        CTC_DKIT_PRINT("Bridge Process:\n");
        CTC_DKIT_PATH_PRINT("%-15s\n", "MAC DA lookup->");
        CTC_DKIT_PATH_PRINT("%-15s:%02X%02X.%02X%02X.%02X%02X\n", "key->MAC DA",
                       macDa[0], macDa[1], macDa[2], macDa[3], macDa[4], macDa[5]);
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "key->FID", vsiId);
        if (conflict)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_BUS_CONFLICT);
            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", keyIndex);
        }
        else if (hashHit)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_BUS_HASH_HIT);
            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", keyIndex);
            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", adIndex);
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsMac");
        }
        else
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_BUS_NOT_HIT);
        }
    }

    return ret;
}
STATIC int32
_ctc_goldengate_dkit_bus_path_ipe_oam(ctc_dkit_bus_path_info_t* p_info)
{
    return CLI_SUCCESS;
}
STATIC int32
_ctc_goldengate_dkit_bus_path_ipe_destination(ctc_dkit_bus_path_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint8 is_mcast = 0;
    uint8 dest_chip= 0;
    uint8 is_link_agg = 0;
    uint32 dest_port = 0;
    uint32 gport = 0;
    uint32 dsNextHopInternalBase = 0;

    /*IpeFwdHdrAdjInfoFifo*/
    uint32 destMap = 0;
    uint32 nextHopPtr = 0;
    uint32 nextHopExt = 0;

    /*IpeFwdLrnThruFifo*/
    uint32 piEcmpGroupId = 0;
    /*IpeFwdApsTrackFifo*/
    uint32 dsFwdApsBridgeEn = 0;
    uint32 piApsSelectValid0 = 0;
    uint32 piApsSelectValid1 = 0;
    uint32 dsFwdDestMap = 0;

    /*IpeFwdOamFifo*/
    uint32 piShareType = 0;

    CTC_DKIT_PRINT("Destination Process:\n");

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "oldDestMap", BsChanInfoSlice0_t + slice, 0, &destMap);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "nextHopPtr", BsChanInfoSlice0_t + slice, 0, &nextHopPtr);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "nextHopExt", BsChanInfoSlice0_t + slice, 0, &nextHopExt);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piEcmpGroupId", IpeFwdLrnThruFifo0_t + slice, 0, &piEcmpGroupId);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "dsFwdApsBridgeEn", IpeFwdApsTrackFifo0_t + slice, 0, &dsFwdApsBridgeEn);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piApsSelectValid0", IpeFwdApsTrackFifo0_t + slice, 0, &piApsSelectValid0);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piApsSelectValid1", IpeFwdApsTrackFifo0_t + slice, 0, &piApsSelectValid1);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "dsFwdDestMap", IpeFwdApsTrackFifo0_t + slice, 0, &dsFwdDestMap);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piShareType", IpeFwdOamFifo0_t + slice, 0, &piShareType);

    if (piEcmpGroupId)
    {
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "do ecmp, group", piEcmpGroupId);
    }
    if (dsFwdApsBridgeEn)
    {
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "APS bridge", "YES");
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "APS group", dsFwdDestMap&0xFFF);
    }
    else
    {
        if (piApsSelectValid0)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "APS selector0", "YES");
        }
        if (piApsSelectValid1)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "APS selector2", "YES");
        }
    }

    _ctc_goldengate_dkit_bus_check_discard(CTC_DKIT_IPE, p_info);
    if (!p_info->discard)
    {
        if (SHARETYPE_OAM == piShareType)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "destination", "OAM engine");
        }
        else
        {
            is_mcast = IS_BIT_SET(destMap, 18);
            dest_port = destMap&0xFFFF;
            if (is_mcast)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "do mcast, group", destMap&0xFFFF);
            }
            else
            {
                dest_chip = (destMap >> 12)&0x1F;
                is_link_agg = (dest_chip == 0x1F);

                if (is_link_agg)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "do linkagg, group", destMap&CTC_DKIT_DRV_LINKAGGID_MASK);
                }
                else
                {
                    gport = CTC_DKIT_DRV_LPORT_TO_CTC_GPORT(dest_chip, dest_port&0x1FF);
                    CTC_DKIT_PATH_PRINT("%-15s:0x%04x\n", "dest gport", gport);
                }

                cmd = DRV_IOR(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_dsNextHopInternalBase_f);
                DRV_IOCTL(lchip, 0, cmd, &dsNextHopInternalBase);
                CTC_DKIT_PATH_PRINT("%-15s\n", "nexthop info->");
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "is 8Wnhp", nextHopExt? "YES" : "NO");
                if (((nextHopPtr >> 4)&0x3FFF) != dsNextHopInternalBase)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "nexthop index", nextHopPtr);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", nextHopExt? "DsNextHop8W" : "DsNextHop4W");
                }
                else
                {
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "nexthop index", (nextHopPtr&0xF));
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "EpeNextHopInternal");
                }

            }
        }
    }
    else
    {
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "Drop!!!", ctc_goldengate_dkit_get_reason_desc(p_info->discard_type + CTC_DKIT_IPE_USERID_BINDING_DIS));
    }


    return CLI_SUCCESS;
}


STATIC int32
_ctc_goldengate_dkit_bus_path_ipe(ctc_dkit_bus_path_info_t* p_info)
{

    CTC_DKIT_PRINT("\nINGRESS PROCESS  \n-----------------------------------\n");

    /*1. Parser*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_bus_path_ipe_parser(p_info);
    }
    /*2. SCL*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_bus_path_ipe_scl(p_info);
    }
    /*3. Interface*/
    if (!p_info->discard)
    {
        _ctc_goldengate_dkit_bus_path_ipe_interface(p_info);
    }
    /*4. Tunnel*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_bus_path_ipe_tunnel(p_info);
    }
    /*5. ACL*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_bus_path_ipe_acl(p_info);
    }
    /*6. Forward*/
    if (!p_info->discard)
    {
        _ctc_goldengate_dkit_bus_path_ipe_forward(p_info);
    }
    /*7. OAM*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_bus_path_ipe_oam(p_info);
    }
    /*8. Destination*/
    if (!p_info->discard)
    {
        _ctc_goldengate_dkit_bus_path_ipe_destination(p_info);
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_bus_path_bsr_mcast(ctc_dkit_bus_path_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 cmd = 0;
    uint32 local_chip = 0;
    uint32 dest_map = 0;
    uint32 groupIdShiftBit = 0;
    uint32 remaining_rcd = 0xFFF;
    uint32 end_replication = 0;
    uint32 dsMetEntryBase = 0;
    uint32 metEntryPtr =0;
    uint32 dsNextHopInternalBase = 0;
    /*DsMetEntry*/
    uint32 currentMetEntryType = 0;
    uint32 isMet = 0;
    uint32 nextMetEntryPtr = 0;
    uint32 endLocalRep = 0;
    uint32  isLinkAggregation = 0;
    uint32  remoteChip = 0;
    uint32  replicationCtl = 0;
    uint32  ucastId = 0;
    uint32   nexthopExt = 0;
    uint32  nexthopPtr = 0;
    uint32 mcastMode = 0;
    uint32 apsBridgeEn = 0;
    uint8 port_bitmap[8] = {0};
    uint8 port_bitmap_high[9] = {0};
    uint8 port_bitmap_low[8] = {0};

    DsMetEntry6W_m ds_met_entry6_w;
    DsMetEntry_m ds_met_entry;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "oldDestMap", BsChanInfoSlice0_t + slice, 0, &dest_map);

    cmd = DRV_IOR(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_dsNextHopInternalBase_f);
    DRV_IOCTL(lchip, 0, cmd, &dsNextHopInternalBase);

    if (IS_BIT_SET(dest_map, 18))
    {
        CTC_DKIT_PRINT("Multicast Process:\n");

        cmd = DRV_IOR(MetFifoCtl_t, MetFifoCtl_chipId_f);
        DRV_IOCTL(lchip, 0, cmd, &local_chip);
        cmd = DRV_IOR(MetFifoCtl_t, MetFifoCtl_dsMetEntryBase_f);
        DRV_IOCTL(lchip, 0, cmd, &dsMetEntryBase);
        cmd = DRV_IOR(MetFifoCtl_t, MetFifoCtl_groupIdShiftBit_f);
        DRV_IOCTL(lchip, 0, cmd, &groupIdShiftBit);
        nextMetEntryPtr = (dest_map&0xFFFF) << groupIdShiftBit;
        for (;;)
        {
            metEntryPtr =  nextMetEntryPtr + dsMetEntryBase;
            CTC_DKIT_PATH_PRINT("%-15s\n", "DsMetEntry--->");
            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "index", metEntryPtr);
            cmd = DRV_IOR(DsMetEntry6W_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, metEntryPtr, cmd, &ds_met_entry6_w);
            GetDsMetEntry6W(A, currentMetEntryType_f, &ds_met_entry6_w, &currentMetEntryType);
            if (currentMetEntryType)
            {
                sal_memcpy((uint8*)&ds_met_entry, (uint8*)&ds_met_entry6_w, DS_MET_ENTRY6_W_BYTES);
            }
            else
            {
                if (IS_BIT_SET(metEntryPtr, 0))
                {
                    sal_memcpy((uint8*)&ds_met_entry, ((uint8 *)&ds_met_entry6_w) + DS_MET_ENTRY3_W_BYTES, DS_MET_ENTRY3_W_BYTES);
                }
                else
                {
                    sal_memcpy((uint8*)&ds_met_entry, ((uint8 *)&ds_met_entry6_w), DS_MET_ENTRY6_W_BYTES);
                }
                sal_memset(((uint8*)&ds_met_entry) + DS_MET_ENTRY3_W_BYTES, 0, DS_MET_ENTRY3_W_BYTES);
            }
            GetDsMetEntry(A, isMet_f, &ds_met_entry, &isMet);
            GetDsMetEntry(A, nextMetEntryPtr_f, &ds_met_entry, &nextMetEntryPtr);
            GetDsMetEntry(A, endLocalRep_f, &ds_met_entry, &endLocalRep);
            GetDsMetEntry(A, isLinkAggregation_f, &ds_met_entry, &isLinkAggregation);
            GetDsMetEntry(A, remoteChip_f, &ds_met_entry, &remoteChip);
            GetDsMetEntry(A, u1_g2_replicationCtl_f, &ds_met_entry, &replicationCtl);
            GetDsMetEntry(A, u1_g2_ucastId_f, &ds_met_entry, &ucastId);
            GetDsMetEntry(A, nextHopExt_f, &ds_met_entry, &nexthopExt);
            GetDsMetEntry(A, nextHopPtr_f, &ds_met_entry, &nexthopPtr);
            GetDsMetEntry(A, mcastMode_f, &ds_met_entry, &mcastMode);
            GetDsMetEntry(A, u1_g2_apsBridgeEn_f, &ds_met_entry, &apsBridgeEn);

            if(remoteChip)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "to remote chip", "YES");
            }
            else
            {
                if (!mcastMode && apsBridgeEn)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "APS bridge", "YES");
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "APS group", ucastId);
                }

                if (!mcastMode)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "met mode", "link list");
                    if (isLinkAggregation)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "do linkagg, group", ucastId&CTC_DKIT_DRV_LINKAGGID_MASK);
                    }
                    else
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:0x%04x\n", "dest gport", CTC_DKIT_DRV_LPORT_TO_CTC_GPORT(local_chip, ucastId&0x1FF));
                    }
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "replication cnt", replicationCtl&0x1F);

                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "is 8Wnhp", nexthopExt? "YES" : "NO");
                    if ((((replicationCtl >> 5) >> 4)&0x3FFF) != dsNextHopInternalBase)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "nexthop index", (replicationCtl >> 5));
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", nexthopExt? "DsNextHop8W" : "DsNextHop4W");
                    }
                    else
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "nexthop index", ((replicationCtl >> 5) >> 4));
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "EpeNextHopInternal");
                    }
                }
                else
                {
                    GetDsMetEntry(A, portBitmapHigh_f, &ds_met_entry, &port_bitmap_high);
                    GetDsMetEntry(A, u1_g1_portBitmap_f, &ds_met_entry, &port_bitmap_low);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "met mode", "port bitmap");
                    if (isLinkAggregation)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "do linkagg", "YES");
                    }
                    else
                    {
                        sal_memcpy(port_bitmap, port_bitmap_low, 8);
                        port_bitmap[7] |= (port_bitmap_high[0] << 3);
                        CTC_DKIT_PATH_PRINT("%-15s:", "dest port63~0");
                        CTC_DKIT_PRINT("0x%02x", port_bitmap[7]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[6]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[5]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[4]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[3]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[2]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[1]);
                        CTC_DKIT_PRINT("%02x\n", port_bitmap[0]);
                        sal_memset(port_bitmap, 0 , 8);
                        port_bitmap[0] = (port_bitmap_high[1] << 3) + (port_bitmap_high[0] >> 5);
                        port_bitmap[1] = (port_bitmap_high[2] << 3) + (port_bitmap_high[1] >> 5);
                        port_bitmap[2] = (port_bitmap_high[3] << 3) + (port_bitmap_high[2] >> 5);
                        port_bitmap[3] = (port_bitmap_high[4] << 3) + (port_bitmap_high[3] >> 5);
                        port_bitmap[4] = (port_bitmap_high[5] << 3) + (port_bitmap_high[4] >> 5);
                        port_bitmap[5] = (port_bitmap_high[6] << 3) + (port_bitmap_high[5] >> 5);
                        port_bitmap[6] = (port_bitmap_high[7] << 3) + (port_bitmap_high[6] >> 5);
                        port_bitmap[7] = (port_bitmap_high[8] << 3) + (port_bitmap_high[7] >> 5);
                        CTC_DKIT_PATH_PRINT("%-15s:", "dest port125~64");
                        CTC_DKIT_PRINT("0x%02x", port_bitmap[7]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[6]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[5]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[4]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[3]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[2]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[1]);
                        CTC_DKIT_PRINT("%02x\n", port_bitmap[0]);

                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "is 8wnhp", nexthopExt? "YES" : "NO");
                        if (((nexthopPtr >> 4)&0x3FFF) != dsNextHopInternalBase)
                        {
                            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "nexthop index", nexthopPtr);
                            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", nexthopExt? "DsNextHop8W" : "DsNextHop4W");
                        }
                        else
                        {
                            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "nexthop index", (nexthopPtr &0xF));
                            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "EpeNextHopInternal");
                        }
                    }
                }
            }
            end_replication = (0xFFFF == nextMetEntryPtr) || (1 == remaining_rcd) || (!isMet);
            remaining_rcd--;

            if (end_replication)
            {
                break;
            }
        }
    }

    return ret;
}
STATIC int32
_ctc_goldengate_dkit_bus_path_bsr_linkagg(ctc_dkit_bus_path_info_t* p_info)
{
    return CLI_SUCCESS;
}
STATIC int32
_ctc_goldengate_dkit_bus_path_bsr_queue(ctc_dkit_bus_path_info_t* p_info)
{
    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_bus_path_bsr(ctc_dkit_bus_path_info_t* p_info)
{
    CTC_DKIT_PRINT("\nRELAY PROCESS    \n-----------------------------------\n");

    /*1. Mcast*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_bus_path_bsr_mcast(p_info);
    }
    /*2. Linkagg*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_bus_path_bsr_linkagg(p_info);
    }
    /*3. Queue*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_bus_path_bsr_queue(p_info);
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_bus_path_epe_parser(ctc_dkit_bus_path_info_t* p_info)
{
    return CLI_SUCCESS;
}
STATIC int32
_ctc_goldengate_dkit_bus_path_epe_scl(ctc_dkit_bus_path_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;
    char* key_name[11] = {"None", "DsEgressXcOamDoubleVlanPortHashKey", "DsEgressXcOamSvlanPortHashKey", "DsEgressXcOamCvlanPortHashKey",
                                   "DsEgressXcOamSvlanCosPortHashKey", "DsEgressXcOamCvlanCosPortHashKey", "DsEgressXcOamPortVlanCrossHashKey",
                                   "DsEgressXcOamPortCrossHashKey", "DsEgressXcOamPortHashKey", "DsEgressXcOamSvlanPortMacHashKey","DsEgressXcOamTunnelPbbHashKey"};


    uint32 vlanHash1Type = 0;
    uint32 vlanHash2Type = 0;
    uint32 piLocalPhyPort = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLocalPhyPort", EpeAclInfoFifo0_t + slice, 0, &piLocalPhyPort);

    cmd = DRV_IOR(DsDestPort_t, DsDestPort_vlanHash1Type_f);
    DRV_IOCTL(lchip, (piLocalPhyPort + slice*CTC_DKIT_ONE_SLICE_PORT_NUM), cmd, &vlanHash1Type);
    cmd = DRV_IOR(DsDestPort_t, DsDestPort_vlanHash2Type_f);
    DRV_IOCTL(lchip, (piLocalPhyPort + slice*CTC_DKIT_ONE_SLICE_PORT_NUM), cmd, &vlanHash2Type);

    if (vlanHash1Type || vlanHash2Type)
    {
        CTC_DKIT_PRINT("SCL Process:\n");

        if (vlanHash1Type)
        {
            CTC_DKIT_PATH_PRINT("%-15s\n", "HASH1 lookup->");
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name[vlanHash1Type]);
        }
        if (vlanHash2Type)
        {
            CTC_DKIT_PATH_PRINT("%-15s\n", "HASH2 lookup->");
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name[vlanHash2Type]);
        }

    }

    return CLI_SUCCESS;
}
STATIC int32
_ctc_goldengate_dkit_bus_path_epe_interface(ctc_dkit_bus_path_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;
    uint16 gport = 0;

    /*EpeAclInfoFifo*/
    uint32 piLocalPhyPort = 0;
    uint32 piGlobalDestPort = 0;
    uint32 piInterfaceId = 0;
    uint32 piDestVlanPtr = 0;

    CTC_DKIT_PRINT("Interface Process:\n");

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLocalPhyPort", EpeAclInfoFifo0_t + slice, 0, &piLocalPhyPort);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piGlobalDestPort", EpeAclInfoFifo0_t + slice, 0, &piGlobalDestPort);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piInterfaceId", EpeAclInfoFifo0_t + slice, 0, &piInterfaceId);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piDestVlanPtr", EpeAclInfoFifo0_t + slice, 0, &piDestVlanPtr);

    if (CTC_DKIT_DRV_GPORT_IS_LINKAGG_PORT(piGlobalDestPort))
    {
        gport = CTC_DKIT_DRV_LPORT_TO_CTC_GPORT(0, (piLocalPhyPort + slice*CTC_DKIT_ONE_SLICE_PORT_NUM));
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "linkagg group", CTC_DKIT_DRV_GPORT_TO_LINKAGG_ID(piGlobalDestPort));
    }
    else
    {
        gport = CTC_DKIT_DRV_GPORT_TO_CTC_GPORT(piGlobalDestPort);
    }
    CTC_DKIT_PATH_PRINT("%-15s:0x%04x\n", "dest gport", gport);

    if (piDestVlanPtr < 4096)
    {
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "vlan ptr", piDestVlanPtr);
    }
    if (piInterfaceId)
    {
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "l3 intf id", piInterfaceId);
    }



    return CLI_SUCCESS;
}
STATIC int32
_ctc_goldengate_dkit_bus_path_epe_acl(ctc_dkit_bus_path_info_t* p_info)
{


    return CLI_SUCCESS;
}
STATIC int32
_ctc_goldengate_dkit_bus_path_epe_oam(ctc_dkit_bus_path_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;

    /*EpeOamTrackFifo*/
    uint32 mepIndex = 0;
    uint32 mipEn = 0;
    uint32 piShareType = 0;
    /*EpeHdrEditPktInfoHdrProcFifo*/
    uint32 piLoopbackEn = 0;

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "mepIndex", EpeOamTrackFifo0_t + slice, 0, &mepIndex);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "mipEn", EpeOamTrackFifo0_t + slice, 0, &mipEn);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piShareType", EpeOamTrackFifo0_t + slice, 0, &piShareType);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piLoopbackEn", EpeHdrEditPktInfoHdrProcFifo0_t + slice, 0, &piLoopbackEn);

    if (SHARETYPE_OAM == piShareType)
    {
        CTC_DKIT_PRINT("OAM Process:\n");
        if (mipEn)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "hit MIP", "YES");
        }
        else
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "hit MEP", "YES");
            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "mep index", mepIndex);
        }
        if (piLoopbackEn)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "destination", "OAM engine");
        }
    }

    return ret;
}
STATIC int32
_ctc_goldengate_dkit_bus_path_epe_edit(ctc_dkit_bus_path_info_t* p_info)
{
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;
    char* l3_edit[12] =
    {
        "NONE edit->",
        "MPLS edit->",
        "RESERVED edit->",
        "NAT edit->",
        "NAT edit->",
        "TUNNELV4 edit->",
        "TUNNELV6 edit->",
        "L3FLEX edit->",
        "OPEN FLOW edit->",
        "OPEN FLOW edit->",
        "LOOPBACK edit->",
        "TRILL edit->"
    };
    char* l3_edit_name[12] =
    {
        " ",
        "DsL3EditMpls3W",
        " ",
        "DsL3EditNat3W",
        "DsL3EditNat6W",
        "DsL3EditTunnelV4",
        "DsL3EditTunnelV6",
        "DsL3EditFlex",
        "DsL3EditOf6W",
        "DsL3EditOf12W",
        "DsL3EditLoopback",
        "DsL3EditTrill"
    };
    char* vlan_action[4] = {"None", "Replace", "Add","Delete"};
    hw_mac_addr_t hw_mac = {0};
    mac_addr_t mac_da = {0};
    DsNextHop8W_m ds_next_hop8_w;
    DsNextHop4W_m ds_next_hop4_w;
    DsL2EditEth3W_m ds_l2_edit_eth3_w;
    DsL3EditMpls3W_m ds_l3_edit_mpls3_w;
    DsL2Edit6WOuter_m ds_l2_edit6_w_outer;
    uint32 dsNextHopInternalBase = 0;
    uint32 nhpShareType = 0;
    uint8 do_l2_edit = 0;
    uint32 l2_edit_ptr = 0;
    uint8 l2_individual_memory = 0;
    uint32 value = 0;
    uint32 next_edit_ptr = 0;
    uint32 next_edit_type = 0;
    uint32 l3RewriteType = 0;
    uint32 l2RewriteType = 0;
    uint32 outerEditPipe3Exist  = 0;
    /*EpeHdrEditPktInfoHdrProcFifo*/
    uint32 piSvlanTagOperation = 0;
    uint32 piL2NewSvlanTag = 0;
    uint32 piCvlanTagOperation = 0;
    uint32 piL2NewCvlanTag = 0;
    uint32 piNextHopPtr = 0;
    /*EpeNextHopDsNextHopFifo*/
    uint32 outerEditLocation = 0;
    uint32 outerEditPtrType = 0;
    uint32 outerEditPtr = 0;
    uint32 innerEditPtrType = 0;
    uint32 innerEditPtr = 0;
    uint32 payloadOperation = 0;
    uint32 isNexthop8W = 0;
    /*L2L3EditPipe1Fifo*/
    uint32 piInnerEditExistSec = 0;
    uint32 piOuterEditPipe2Exist = 0;
    uint32 piOuterEditPipe1ExistSec = 0;
    uint32 piOuterEditPipe1TypeSec = 0;

    sal_memset(&ds_next_hop8_w, 0, sizeof(ds_next_hop8_w));
    sal_memset(&ds_next_hop4_w, 0, sizeof(ds_next_hop4_w));
    sal_memset(&ds_l2_edit_eth3_w, 0, sizeof(ds_l2_edit_eth3_w));
    sal_memset(&ds_l3_edit_mpls3_w, 0, sizeof(ds_l3_edit_mpls3_w));
    sal_memset(&ds_l2_edit6_w_outer, 0, sizeof(ds_l2_edit6_w_outer));

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piSvlanTagOperation", EpeHdrEditPktInfoHdrProcFifo0_t + slice, 0, &piSvlanTagOperation);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piL2NewSvlanTag", EpeHdrEditPktInfoHdrProcFifo0_t + slice, 0, &piL2NewSvlanTag);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piCvlanTagOperation", EpeHdrEditPktInfoHdrProcFifo0_t + slice, 0, &piCvlanTagOperation);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piL2NewCvlanTag", EpeHdrEditPktInfoHdrProcFifo0_t + slice, 0, &piL2NewCvlanTag);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piNextHopPtr", EpeHdrEditPktInfoHdrProcFifo0_t + slice, 0, &piNextHopPtr);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "outerEditLocation", EpeNextHopDsNextHopFifo0_t + slice, 0, &outerEditLocation);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "outerEditPtrType", EpeNextHopDsNextHopFifo0_t + slice, 0, &outerEditPtrType);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "outerEditPtr", EpeNextHopDsNextHopFifo0_t + slice, 0, &outerEditPtr);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "innerEditPtrType", EpeNextHopDsNextHopFifo0_t + slice, 0, &innerEditPtrType);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "innerEditPtr", EpeNextHopDsNextHopFifo0_t + slice, 0, &innerEditPtr);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "payloadOperation", EpeNextHopDsNextHopFifo0_t + slice, 0, &payloadOperation);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "isNexthop8W", EpeNextHopDsNextHopFifo0_t + slice, 0, &isNexthop8W);

    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piInnerEditExistSec", L2L3EditPipe1Fifo0_t + slice, 0, &piInnerEditExistSec);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piOuterEditPipe2Exist", L2L3EditPipe1Fifo0_t + slice, 0, &piOuterEditPipe2Exist);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piOuterEditPipe1ExistSec", L2L3EditPipe1Fifo0_t + slice, 0, &piOuterEditPipe1ExistSec);
    ret |= ctc_goldengate_dkit_drv_read_bus_field(lchip, "piOuterEditPipe1TypeSec", L2L3EditPipe1Fifo0_t + slice, 0, &piOuterEditPipe1TypeSec);

    if (piSvlanTagOperation || piCvlanTagOperation)
    {
        CTC_DKIT_PATH_PRINT("%-15s\n", "vlan edit->");

        if (piSvlanTagOperation)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "svlan edit", vlan_action[piSvlanTagOperation]);
            if ((VTAGACTIONTYPE_MODIFY == piSvlanTagOperation) || (VTAGACTIONTYPE_ADD == piSvlanTagOperation))
            {
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new COS", (piL2NewSvlanTag >> 13)&0x7);
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new CFI", (piL2NewSvlanTag >> 12)&0x1);
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new vlan id", piL2NewSvlanTag&0xFFF);
            }
        }
        if (piCvlanTagOperation)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "cvlan edit", vlan_action[piCvlanTagOperation]);
            if ((VTAGACTIONTYPE_MODIFY == piCvlanTagOperation) || (VTAGACTIONTYPE_ADD == piCvlanTagOperation))
            {
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new COS", (piL2NewCvlanTag >> 13)&0x7);
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new CFI", (piL2NewCvlanTag >> 12)&0x1);
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new vlan id", piL2NewCvlanTag&0xFFF);
            }
        }
    }


if ((piNextHopPtr >> 4) != dsNextHopInternalBase)
    {
        cmd = DRV_IOR(DsNextHop8W_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, CLEAR_BIT(piNextHopPtr,0), cmd, &ds_next_hop8_w);
        if (!isNexthop8W)
        {
            if (IS_BIT_SET((piNextHopPtr), 0))
            {
                sal_memcpy((uint8*)&ds_next_hop4_w, (uint8*)&ds_next_hop8_w + DS_NEXT_HOP4_W_BYTES, DS_NEXT_HOP4_W_BYTES);
            }
            else
            {
                sal_memcpy((uint8*)&ds_next_hop4_w, (uint8*)&ds_next_hop8_w , DS_NEXT_HOP4_W_BYTES);
            }
        }
    }
    else
    {
        cmd = DRV_IOR(EpeNextHopInternal_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, ((piNextHopPtr&0xF) >> 1), cmd, &ds_next_hop8_w);
        if (!isNexthop8W)
        {
            if (IS_BIT_SET((piNextHopPtr&0xF), 0))
            {
                sal_memcpy((uint8*)&ds_next_hop4_w, (uint8*)&ds_next_hop8_w + DS_NEXT_HOP4_W_BYTES, DS_NEXT_HOP4_W_BYTES);
            }
            else
            {
                sal_memcpy((uint8*)&ds_next_hop4_w, (uint8*)&ds_next_hop8_w , DS_NEXT_HOP4_W_BYTES);
            }
        }
    }

    if (isNexthop8W)
    {

    }
    else
    {
        GetDsNextHop4W(A, shareType_f, &ds_next_hop4_w, &nhpShareType);
        if (PAYLOADOPERATION_ROUTE_COMPACT == payloadOperation)
        {
            GetDsNextHop4W(A, u1_g3_macDa_f, &ds_next_hop4_w, hw_mac);
            CTC_DKIT_SET_USER_MAC(mac_da, hw_mac);
            if (IS_BIT_SET(mac_da[0], 0)) /*mcast mode*/
            {

            }
            else
            {
                CTC_DKIT_PATH_PRINT("%-15s\n", "MAC edit->");
                CTC_DKIT_PATH_PRINT("%-15s:%02X%02X.%02X%02X.%02X%02X\n", "new MAC DA",
                                         mac_da[0], mac_da[1], mac_da[2], mac_da[3], mac_da[4], mac_da[5]);
            }
        }
        else if (NHPSHARETYPE_L2EDIT_PLUS_L3EDIT_OP == nhpShareType)
        {
            /*inner edit*/
            if (piInnerEditExistSec && (!innerEditPtrType)) /*inner for l2edit*/
            {
                do_l2_edit = 1;
                l2_edit_ptr = innerEditPtr;
                l2_individual_memory = 0;
            }
            else if (piInnerEditExistSec && (innerEditPtrType)) /*inner for l3edit*/
            {
                cmd = DRV_IOR(DsL3EditMpls3W_t, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, innerEditPtr, cmd, &ds_l3_edit_mpls3_w);
                 /*outerEditPtr and outerEditPtrType are in the same position in all l3edit, refer to DsL3Edit3WTemplate*/
                GetDsL3EditMpls3W(A, l3RewriteType_f, &ds_l3_edit_mpls3_w, &l3RewriteType);
                if (L3REWRITETYPE_MPLS4W == l3RewriteType)
                {
                    GetDsL3EditMpls3W(A, label_f, &ds_l3_edit_mpls3_w, &value);
                    CTC_DKIT_PATH_PRINT("%-15s\n", "MPLS edit->");
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l3edit index", innerEditPtr);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "DsL3EditMpls3W");
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new label", value);
                }
                else
                {
                    if (l3RewriteType != L3REWRITETYPE_NONE)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s\n", l3_edit[l3RewriteType]);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l3edit index", innerEditPtr);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", l3_edit_name[l3RewriteType]);
                    }
                }
            }

            /*outer pipeline1*/
            if (piOuterEditPipe1ExistSec)
            {
                if (piOuterEditPipe1TypeSec) /*l3edit*/
                {
                    if (outerEditLocation) /*individual memory*/
                    {
                         /*no use*/
                    }
                    else /*share memory*/
                    {
                        cmd = DRV_IOR(DsL3EditMpls3W_t, DRV_ENTRY_FLAG);
                        DRV_IOCTL(lchip, outerEditPtr, cmd, &ds_l3_edit_mpls3_w);
                         /*outerEditPtr and outerEditPtrType are in the same position in all l3edit, refer to DsL3Edit3WTemplate*/
                        GetDsL3EditMpls3W(A, outerEditPtr_f, &ds_l3_edit_mpls3_w, &next_edit_ptr);
                        GetDsL3EditMpls3W(A, outerEditPtrType_f, &ds_l3_edit_mpls3_w, &next_edit_type);
                        GetDsL3EditMpls3W(A, l3RewriteType_f, &ds_l3_edit_mpls3_w, &l3RewriteType);
                        if (L3REWRITETYPE_MPLS4W == l3RewriteType)
                        {
                            GetDsL3EditMpls3W(A, label_f, &ds_l3_edit_mpls3_w, &value);
                            CTC_DKIT_PATH_PRINT("%-15s\n", "MPLS edit->");
                            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l3edit index", outerEditPtr);
                            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "DsL3EditMpls3W");
                            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new label", value);
                        }
                        else
                        {
                            if (l3RewriteType != L3REWRITETYPE_NONE)
                            {
                                CTC_DKIT_PATH_PRINT("%-15s\n", l3_edit[l3RewriteType]);
                                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l3edit index", innerEditPtr);
                                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", l3_edit_name[l3RewriteType]);
                            }
                        }
                        if ((next_edit_ptr) && (!next_edit_type))
                        {
                            outerEditPipe3Exist = 1;
                        }
                    }
                }
                else  /*l2edit*/
                {
                    do_l2_edit = 1;
                    l2_edit_ptr = outerEditPtr;
                    l2_individual_memory = outerEditLocation?1:0;
                }
            }
            /*outer pipeline2*/
            if (piOuterEditPipe2Exist) /*outer pipeline2, individual memory for SPME*/
            {
                if ( next_edit_type) /*l3edit*/
                {
                    cmd = DRV_IOR(DsL3Edit3W3rd_t, DRV_ENTRY_FLAG);
                    DRV_IOCTL(lchip, next_edit_ptr, cmd, &ds_l3_edit_mpls3_w);
                    GetDsL3EditMpls3W(A, label_f, &ds_l3_edit_mpls3_w, &value);
                    CTC_DKIT_PATH_PRINT("%-15s\n", "MPLS edit->");
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l3edit index", next_edit_ptr);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "DsL3Edit3W3rd");
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new label", value);

                    GetDsL3EditMpls3W(A, outerEditPtr_f, &ds_l3_edit_mpls3_w, &next_edit_ptr);
                    GetDsL3EditMpls3W(A, outerEditPtrType_f, &ds_l3_edit_mpls3_w, &next_edit_type);
                    if ((next_edit_ptr) && (!next_edit_type))
                    {
                        outerEditPipe3Exist = 1;
                    }
                }
                else  /*l2edit*/
                {
                    do_l2_edit = 1;
                    if (!piOuterEditPipe1ExistSec)
                    {
                        l2_edit_ptr = outerEditPtr;
                    }
                    else
                    {
                        l2_edit_ptr = next_edit_ptr;
                    }
                    l2_individual_memory = 1;
                }
            }
            /*outer pipeline3*/
            if (outerEditPipe3Exist) /*outer pipeline3, individual memory for l2edit*/
            {
                do_l2_edit = 1;
                if(!piOuterEditPipe1ExistSec)
                {
                    l2_edit_ptr = outerEditPtr;
                }
                else
                {
                    l2_edit_ptr = next_edit_ptr;
                }
                l2_individual_memory = 1;
            }
           if(do_l2_edit)
           {
               if (l2_individual_memory) /*individual memory*/
               {
                   cmd = DRV_IOR(DsL2Edit6WOuter_t, DRV_ENTRY_FLAG);
                   DRV_IOCTL(lchip, (l2_edit_ptr >> 1), cmd, &ds_l2_edit6_w_outer);

                   if (l2_edit_ptr % 2 == 0)
                   {
                       sal_memcpy((uint8*)&ds_l2_edit_eth3_w, &ds_l2_edit6_w_outer, sizeof(ds_l2_edit_eth3_w));
                   }
                   else
                   {
                       sal_memcpy((uint8*)&ds_l2_edit_eth3_w, ((uint8*)&ds_l2_edit6_w_outer) + sizeof(ds_l2_edit_eth3_w), sizeof(ds_l2_edit_eth3_w));
                   }
                   l2RewriteType = GetDsL2EditEth3W(V, l2RewriteType_f, &ds_l2_edit_eth3_w);
                   if (L2REWRITETYPE_ETH4W == l2RewriteType)
                   {
                       GetDsL2EditEth3W(A, macDa_f, &ds_l2_edit_eth3_w, hw_mac);
                       CTC_DKIT_SET_USER_MAC(mac_da, hw_mac);
                       CTC_DKIT_PATH_PRINT("%-15s\n", "MAC edit->");
                       CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l2edit index", l2_edit_ptr);
                       CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "DsL2Edit6WOuter");
                       CTC_DKIT_PATH_PRINT("%-15s:%02X%02X.%02X%02X.%02X%02X\n", "new MAC DA",
                                      mac_da[0], mac_da[1], mac_da[2], mac_da[3], mac_da[4], mac_da[5]);
                   }
               }
               else /*share memory*/
               {
                   cmd = DRV_IOR(DsL2EditEth3W_t, DRV_ENTRY_FLAG);
                   DRV_IOCTL(lchip, l2_edit_ptr, cmd, &ds_l2_edit_eth3_w);
                   l2RewriteType = GetDsL2EditEth3W(V, l2RewriteType_f, &ds_l2_edit_eth3_w);
                   if (L2REWRITETYPE_ETH4W == l2RewriteType)
                   {
                       GetDsL2EditEth3W(A, macDa_f, &ds_l2_edit_eth3_w, hw_mac);
                       CTC_DKIT_SET_USER_MAC(mac_da, hw_mac);
                       CTC_DKIT_PATH_PRINT("%-15s\n", "MAC edit->");
                       CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l2edit index", l2_edit_ptr);
                       CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "DsL2EditEth3W");
                       CTC_DKIT_PATH_PRINT("%-15s:%02X%02X.%02X%02X.%02X%02X\n", "new MAC DA",
                                                mac_da[0], mac_da[1], mac_da[2], mac_da[3], mac_da[4], mac_da[5]);
                   }
               }

           }
        }
        else if (NHPSHARETYPE_L2EDIT_PLUS_STAG_OP == nhpShareType)
        {

        }

    }


    return ret;
}

STATIC int32
_ctc_goldengate_dkit_bus_path_epe(ctc_dkit_bus_path_info_t* p_info)
{
    CTC_DKIT_PRINT("\nEGRESS PROCESS   \n-----------------------------------\n");

    /*1. Parser*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_bus_path_epe_parser(p_info);
    }
    /*2. SCL*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_bus_path_epe_scl(p_info);
    }
    /*3. Interface*/
    if (!p_info->discard)
    {
        _ctc_goldengate_dkit_bus_path_epe_interface(p_info);
    }
    /*4. ACL*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_bus_path_epe_acl(p_info);
    }
    /*5. OAM*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_bus_path_epe_oam(p_info);
    }
    /*6. Edit*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_bus_path_epe_edit(p_info);
    }

    return CLI_SUCCESS;
}


int32
ctc_goldengate_dkit_bus_path_process(uint8 lchip, void* p_para, uint8 detail)
{
    int32 ret = CLI_SUCCESS;
    ctc_dkit_bus_path_info_t path_info;
    ctc_dkit_path_stats_t* p_stats = (ctc_dkit_path_stats_t*)p_para;

    DKITS_PTR_VALID_CHECK(p_para);
    sal_memset(&path_info, 0, sizeof(path_info));
    path_info.lchip = lchip;
    _ctc_goldengate_dkit_bus_get_path_info(p_stats, &path_info);

    path_info.detail = detail;

    if ((path_info.is_ipe_path) && (!path_info.discard))
    {
        _ctc_goldengate_dkit_bus_path_ipe(&path_info);
    }
    if ((path_info.is_bsr_path) && (!path_info.discard) && path_info.detail)
    {
        _ctc_goldengate_dkit_bus_path_bsr(&path_info);
    }
    if ((path_info.is_epe_path) && (!path_info.discard))
    {
        _ctc_goldengate_dkit_bus_path_epe(&path_info);
    }
    return ret;
}





