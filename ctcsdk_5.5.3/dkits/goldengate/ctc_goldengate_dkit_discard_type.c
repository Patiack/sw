#include "sal.h"
#include "ctc_goldengate_dkit_discard_type.h"

const char *ctc_goldengate_dkit_get_reason_desc(ctc_dkit_discard_reason_id_t reason_id)
{
    static char  reason_desc[256+1] = {0};
    if (reason_id == CTC_DKIT_DISCARD_INVALIED)
    {
        return " ";
    }

    switch(reason_id)
    {
     case CTC_DKIT_IPE_USERID_BINDING_DIS:  
       return "CTC_DKIT_IPE_USERID_BINDING_DIS, the data in packet is not as SCL config, such as MAC DA, port, vlan, IP SA";   
     case CTC_DKIT_IPE_LPBK_HDR_ADJ_RM_ERR0:  
       return "CTC_DKIT_IPE_LPBK_HDR_ADJ_RM_ERR0, length error at loopback header adjust, process offset>pktlen";   
     case CTC_DKIT_IPE_PARSER_LEN_ERR:  
       return "CTC_DKIT_IPE_PARSER_LEN_ERR, length error when do parser";   
     case CTC_DKIT_IPE_LPBK_HDR_ADJ_RM_ERR1:  
       return "CTC_DKIT_IPE_LPBK_HDR_ADJ_RM_ERR1, length error at loopback header adjust, remove bytes>pktlen";   
     case CTC_DKIT_IPE_EXCEP2_DIS:  
       return "CTC_DKIT_IPE_EXCEP2_DIS, l2 PDU discard";   
     case CTC_DKIT_IPE_DS_USERID_DIS:  
       return "CTC_DKIT_IPE_DS_USERID_DIS, next hop index from SCL is invalid";   
     case CTC_DKIT_IPE_RECEIVE_DIS:  
       return "CTC_DKIT_IPE_RECEIVE_DIS, port/vlan receive disable";   
     case CTC_DKIT_IPE_PBB_CHK_DIS:  
       return "CTC_DKIT_IPE_PBB_CHK_DIS, ITag check failure, invalid bmac, or loop MAC SA of PIP port type in PBB";   
     case CTC_DKIT_IPE_PROTOCOL_VLAN_DIS:  
       return "CTC_DKIT_IPE_PROTOCOL_VLAN_DIS, protocol vlan is config as discard";   
     case CTC_DKIT_IPE_AFT_DIS:  
       return "CTC_DKIT_IPE_AFT_DIS, AFT discard";   
     case CTC_DKIT_IPE_ILLEGAL_PKT_DIS:  
       return "CTC_DKIT_IPE_ILLEGAL_PKT_DIS, mcast MAC SA discard /same MAC or IP DA SA discard";   
     case CTC_DKIT_IPE_STP_DIS:  
       return "CTC_DKIT_IPE_STP_DIS, STP discard";   
     case CTC_DKIT_IPE_TRILL_ESADI_LPBK_DIS:  
       return "CTC_DKIT_IPE_TRILL_ESADI_LPBK_DIS, ESADI packet is disable on the port";   
     case CTC_DKIT_IPE_PBB_OAM_DIS:  
       return "CTC_DKIT_IPE_PBB_OAM_DIS, PBB OAM discard";   
     case CTC_DKIT_IPE_ARP_DHCP_DIS:  
       return "CTC_DKIT_IPE_ARP_DHCP_DIS, ARP/DHCP discard";   
     case CTC_DKIT_IPE_DS_PHYPORT_SRCDIS:  
       return "CTC_DKIT_IPE_DS_PHYPORT_SRCDIS, ingress phyport discard";   
     case CTC_DKIT_IPE_VLAN_FILTER_DIS:  
       return "CTC_DKIT_IPE_VLAN_FILTER_DIS, ingress vlan filtering discard";   
     case CTC_DKIT_IPE_DS_ACL_DIS:  
       return "CTC_DKIT_IPE_DS_ACL_DIS, discard by ACL deny or GRE flag is mismatch to config";   
     case CTC_DKIT_IPE_ROUTE_MC_IPADD_DIS:  
       return "CTC_DKIT_IPE_ROUTE_MC_IPADD_DIS, routing multicast IP address check discard or IP Protocal Translation discard";   
     case CTC_DKIT_IPE_SECURITY_CHK_DIS:  
       return "CTC_DKIT_IPE_SECURITY_CHK_DIS, bridge dest port is equal to source port discard or unknown Mcast/Ucast or Bcast discard";   
     case CTC_DKIT_IPE_STORM_CTL_DIS:  
       return "CTC_DKIT_IPE_STORM_CTL_DIS, port or vlan based storm Control discard";   
     case CTC_DKIT_IPE_LEARNING_DIS:  
       return "CTC_DKIT_IPE_LEARNING_DIS, the MACSA config as discard /source port mismatch /port security discard";   
     case CTC_DKIT_IPE_POLICING_DIS:  
       return "CTC_DKIT_IPE_POLICING_DIS, policing discard";   
     case CTC_DKIT_IPE_NO_FORWARDING_PTR:  
       return "CTC_DKIT_IPE_NO_FORWARDING_PTR, not find next hop, no dsFwdPtr";   
     case CTC_DKIT_IPE_IS_DIS_FORWARDING_PTR:  
       return "CTC_DKIT_IPE_IS_DIS_FORWARDING_PTR, next hop index is invalid, dsFwdPtr=0xFFFF";   
     case CTC_DKIT_IPE_FATAL_EXCEP_DIS:  
       return "CTC_DKIT_IPE_FATAL_EXCEP_DIS, some fatal event occured";   
     case CTC_DKIT_IPE_APS_DIS:  
       return "CTC_DKIT_IPE_APS_DIS, APS selector discard";   
     case CTC_DKIT_IPE_DS_FWD_DESTID_DIS:  
       return "CTC_DKIT_IPE_DS_FWD_DESTID_DIS, next hop is invalid, nextHopPtr=0x3FFFF";   
     case CTC_DKIT_IPE_IP_HEADER_ERROR_DIS:  
       return "CTC_DKIT_IPE_IP_HEADER_ERROR_DIS, IP version mismatch /IP header length error /check sum is wrong /ICMP fragment packet /mismatch GRE version";   
     case CTC_DKIT_IPE_VXLAN_FLAG_CHK_DIS:  
       return "CTC_DKIT_IPE_VXLAN_FLAG_CHK_DIS, VXLAN flag error";   
     case CTC_DKIT_IPE_GENEVE_PAKCET_DISCARD:  
       return "CTC_DKIT_IPE_GENEVE_PAKCET_DISCARD, VXLAN packet is OAM frame or has option, redirect to CPU";   
     case CTC_DKIT_IPE_TRILL_OAM_DIS:  
       return "CTC_DKIT_IPE_TRILL_OAM_DIS, TRILL OAM packet discard, redirect to CPU";   
     case CTC_DKIT_IPE_LOOPBACK_DIS:  
       return "CTC_DKIT_IPE_LOOPBACK_DIS, loopback discard when from Fabric is disable";   
     case CTC_DKIT_IPE_EFM_DIS:  
       return "CTC_DKIT_IPE_EFM_DIS, EFM OAM packet discard, redirect to CPU";   
     case CTC_DKIT_IPE_MPLSMC_CHECK_DIS:  
       return "CTC_DKIT_IPE_MPLSMC_CHECK_DIS, MPLS Mcast MAC DA check fail";   
     case CTC_DKIT_IPE_STACKING_HDR_CHK_ERR:  
       return "CTC_DKIT_IPE_STACKING_HDR_CHK_ERR, stacking network header length or MAC DA check error";   
     case CTC_DKIT_IPE_TRILL_FILTER_ERR:  
       return "CTC_DKIT_IPE_TRILL_FILTER_ERR, TRILL filter discard / invalid TRILL MAC DA on edge port";   
     case CTC_DKIT_IPE_ENTROPY_IS_RESERVED_LABEL:  
       return "CTC_DKIT_IPE_ENTROPY_IS_RESERVED_LABEL, entropyLabel is 0";   
     case CTC_DKIT_IPE_L3_EXCEP_DIS:  
       return "CTC_DKIT_IPE_L3_EXCEP_DIS, VXLAN/NVGRE fraginfo should be 0 /ACH type is wrong for OAM or CW is not 0 /the IP is config to CPU";   
     case CTC_DKIT_IPE_L2_EXCEP_DIS:  
       return "CTC_DKIT_IPE_L2_EXCEP_DIS, L2 PDU discard";   
     case CTC_DKIT_IPE_TRILL_MC_ADD_CHK_DIS:  
       return "CTC_DKIT_IPE_TRILL_MC_ADD_CHK_DIS, TRILL Mcast address is not 0x0180C2000040";   
     case CTC_DKIT_IPE_TRILL_VERSION_CHK_DIS:  
       return "CTC_DKIT_IPE_TRILL_VERSION_CHK_DIS, TRILL version check error /inner VLAN ID check discard";   
     case CTC_DKIT_IPE_PTP_EXCEP_DIS:  
       return "CTC_DKIT_IPE_PTP_EXCEP_DIS, PTP packet in MPLS tunnel discard /version check fail /config to discard";   
     case CTC_DKIT_IPE_OAM_NOT_FIND_DIS:  
       return "CTC_DKIT_IPE_OAM_NOT_FIND_DIS, OAM lookup hash conflict or not find MEP/MIP at ether OAM edge port";   
     case CTC_DKIT_IPE_OAM_FILTER_DIS:  
       return "CTC_DKIT_IPE_OAM_FILTER_DIS, OAM packet STP/VLAN filter when no MEP/MIP";   
     case CTC_DKIT_IPE_BFD_OAM_TTL_CHK_DIS:  
       return "CTC_DKIT_IPE_BFD_OAM_TTL_CHK_DIS, BFD IPDA or TTL check error discard";   
     case CTC_DKIT_IPE_ICMP_REDIRECT_DIS:  
       return "CTC_DKIT_IPE_ICMP_REDIRECT_DIS, ICMP discard, redirect to CPU";   
     case CTC_DKIT_IPE_NO_MEP_MIP_DIS:  
       return "CTC_DKIT_IPE_NO_MEP_MIP_DIS, no MEP/MIP found for the OAM packet, redirect to CPU";   
     case CTC_DKIT_IPE_OAM_DISABLE_DIS:  
       return "CTC_DKIT_IPE_OAM_DISABLE_DIS, TP OAM config to disacrd by label";   
     case CTC_DKIT_IPE_PBB_DECAP_DIS:  
       return "CTC_DKIT_IPE_PBB_DECAP_DIS, PBB in MPLS tunnel decap discard";   
     case CTC_DKIT_IPE_MPLS_TMPLS_OAM_DIS:  
       return "CTC_DKIT_IPE_MPLS_TMPLS_OAM_DIS, TMPLS OAM, redirect to CPU";   
     case CTC_DKIT_IPE_MPLSTP_MCC_SCC_DIS:  
       return "CTC_DKIT_IPE_MPLSTP_MCC_SCC_DIS, TP MCC or SCC packet, redirect to CPU";   
     case CTC_DKIT_IPE_ICMP_ERR_MSG_DIS:  
       return "CTC_DKIT_IPE_ICMP_ERR_MSG_DIS, ICMP error message, redirect to CPU";   
     case CTC_DKIT_IPE_ETH_SERV_OAM_DIS:  
       return "CTC_DKIT_IPE_ETH_SERV_OAM_DIS, untagged service OAM, redirect to CPU";   
     case CTC_DKIT_IPE_ETH_LINK_OAM_DIS:  
       return "CTC_DKIT_IPE_ETH_LINK_OAM_DIS, tagged link OAM, redirect to CPU";   
     case CTC_DKIT_IPE_TUNNEL_DECAP_MARTIAN_ADD:  
       return "CTC_DKIT_IPE_TUNNEL_DECAP_MARTIAN_ADD, outer header martian address discard for tunnel packet";   
     case CTC_DKIT_IPE_VXLAN_NVGRE_CHK_FAIL:  
       return "CTC_DKIT_IPE_VXLAN_NVGRE_CHK_FAIL, VXLAN or NVGRE check fail according to the tunnel config";   
     case CTC_DKIT_IPE_USERID_FWD_PTR_DIS:  
       return "CTC_DKIT_IPE_USERID_FWD_PTR_DIS, next hop index from SCL for tunnel is invalid";   
     case CTC_DKIT_IPE_QUE_DROP_SPAN_DISCARD:  
       return "CTC_DKIT_IPE_QUE_DROP_SPAN_DISCARD, dropped packet at queuing do SPAN";   
     case CTC_DKIT_IPE_TRILL_RPF_CHK_DIS:  
       return "CTC_DKIT_IPE_TRILL_RPF_CHK_DIS, TRILL RPF check fail discard";   
     case CTC_DKIT_IPE_MUX_PORT_ERR:  
       return "CTC_DKIT_IPE_MUX_PORT_ERR, mux port error";   
     case CTC_DKIT_IPE_PORT_MAC_CHECK_DIS:  
       return "CTC_DKIT_IPE_PORT_MAC_CHECK_DIS, MAC DA or SA is port MAC";   
     case CTC_DKIT_IPE_VXLAN_NVGRE_INNER_VTAG_CHK_DIS:  
       return "CTC_DKIT_IPE_VXLAN_NVGRE_INNER_VTAG_CHK_DIS, VXLAN or NVGRE packet with inner VTAG discard";   
     case CTC_DKIT_IPE_RESERVED2:  
       return "CTC_DKIT_IPE_RESERVED2, RESERVED";   
     case CTC_DKIT_IPE_HW_HAR_ADJ:  
       return "CTC_DKIT_IPE_HW_HAR_ADJ, some hardware error occurred at header adjust";   
     case CTC_DKIT_IPE_HW_INT_MAP:  
       return "CTC_DKIT_IPE_HW_INT_MAP, some hardware error occurred at interface mapper";   
     case CTC_DKIT_IPE_HW_LKUP_QOS:  
       return "CTC_DKIT_IPE_HW_LKUP_QOS, some hardware error occurred when do ACL&QOS lookup ";   
     case CTC_DKIT_IPE_HW_LKUP_KEY_GEN:  
       return "CTC_DKIT_IPE_HW_LKUP_KEY_GEN, some hardware error occurred when generate lookup key";   
     case CTC_DKIT_IPE_HW_PKT_PROC:  
       return "CTC_DKIT_IPE_HW_PKT_PROC, some hardware error occurred when do packet process";   
     case CTC_DKIT_IPE_HW_PKT_FWD:  
       return "CTC_DKIT_IPE_HW_PKT_FWD, some hardware error occurred at forward";   
     case CTC_DKIT_IPE_MAX:  
       return "";   
     case CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS:  
       return "CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS, discard by drop channel";   
     case CTC_DKIT_EPE_HDR_ADJ_PKT_ERR:  
       return "CTC_DKIT_EPE_HDR_ADJ_PKT_ERR, header adjust error(no use)";   
     case CTC_DKIT_EPE_HDR_ADJ_REMOVE_ERR:  
       return "CTC_DKIT_EPE_HDR_ADJ_REMOVE_ERR, remove bytes error at header adjust, offset>144B";   
     case CTC_DKIT_EPE_DS_DESTPHYPORT_DSTID_DIS:  
       return "CTC_DKIT_EPE_DS_DESTPHYPORT_DSTID_DIS, egress port discard";   
     case CTC_DKIT_EPE_PORT_ISO_DIS:  
       return "CTC_DKIT_EPE_PORT_ISO_DIS, port isolate or Pvlan discard";   
     case CTC_DKIT_EPE_DS_VLAN_TRANSMIT_DIS:  
       return "CTC_DKIT_EPE_DS_VLAN_TRANSMIT_DIS, port/vlan transmit disable";   
     case CTC_DKIT_EPE_BRG_TO_SAME_PORT_DIS:  
       return "CTC_DKIT_EPE_BRG_TO_SAME_PORT_DIS, bridge to same port(no use)";   
     case CTC_DKIT_EPE_VPLS_HORIZON_SPLIT_DIS:  
       return "CTC_DKIT_EPE_VPLS_HORIZON_SPLIT_DIS, VPLS split horizon split or E-tree discard";   
     case CTC_DKIT_EPE_VLAN_FILTER_DIS:  
       return "CTC_DKIT_EPE_VLAN_FILTER_DIS, egress vlan filtering discard";   
     case CTC_DKIT_EPE_STP_DIS:  
       return "CTC_DKIT_EPE_STP_DIS, STP discard";   
     case CTC_DKIT_EPE_PARSER_LEN_ERR:  
       return "CTC_DKIT_EPE_PARSER_LEN_ERR, parser length error";   
     case CTC_DKIT_EPE_PBB_CHK_DIS:  
       return "CTC_DKIT_EPE_PBB_CHK_DIS, PBB check discard";   
     case CTC_DKIT_EPE_UC_MC_FLOODING_DIS:  
       return "CTC_DKIT_EPE_UC_MC_FLOODING_DIS, unkown-unicast/kown-unicast/unkown-mcast/kown-mcast/broadcast discard";   
     case CTC_DKIT_EPE_OAM_802_3_DIS:  
       return "CTC_DKIT_EPE_OAM_802_3_DIS, discard non-EFM OAM packet";   
     case CTC_DKIT_EPE_TTL_FAIL:  
       return "CTC_DKIT_EPE_TTL_FAIL, TTL check failed";   
     case CTC_DKIT_EPE_REMOTE_MIRROR_ESCAP_DIS:  
       return "CTC_DKIT_EPE_REMOTE_MIRROR_ESCAP_DIS, remote mirror filtering discard";   
     case CTC_DKIT_EPE_TUNNEL_MTU_CHK_DIS:  
       return "CTC_DKIT_EPE_TUNNEL_MTU_CHK_DIS, tunnel MTU check discard";   
     case CTC_DKIT_EPE_INTERF_MTU_CHK_DIS:  
       return "CTC_DKIT_EPE_INTERF_MTU_CHK_DIS, interface MTU check discard";   
     case CTC_DKIT_EPE_LOGIC_PORT_CHK_DIS:  
       return "CTC_DKIT_EPE_LOGIC_PORT_CHK_DIS, logic source port is equal to dest port";   
     case CTC_DKIT_EPE_DS_ACL_DIS:  
       return "CTC_DKIT_EPE_DS_ACL_DIS, discard by ACL deny";   
     case CTC_DKIT_EPE_DS_VLAN_XLATE_DIS:  
       return "CTC_DKIT_EPE_DS_VLAN_XLATE_DIS, discard by SCL deny";   
     case CTC_DKIT_EPE_POLICING_DIS:  
       return "CTC_DKIT_EPE_POLICING_DIS, policing discard";   
     case CTC_DKIT_EPE_CRC_ERR_DIS:  
       return "CTC_DKIT_EPE_CRC_ERR_DIS, CRC check error(no use)";   
     case CTC_DKIT_EPE_ROUTE_PLD_OP_DIS:  
       return "CTC_DKIT_EPE_ROUTE_PLD_OP_DIS, route payload operation no IP packet discard";   
     case CTC_DKIT_EPE_BRIDGE_PLD_OP_DIS:  
       return "CTC_DKIT_EPE_BRIDGE_PLD_OP_DIS, bridge payload operation bridge disable";   
     case CTC_DKIT_EPE_PT_L4_OFFSET_LARGE:  
       return "CTC_DKIT_EPE_PT_L4_OFFSET_LARGE, packet edit strip too large";   
     case CTC_DKIT_EPE_BFD_DIS:  
       return "CTC_DKIT_EPE_BFD_DIS, BFD discard(no use)";   
     case CTC_DKIT_EPE_PORT_REFLECTIVE_CHK_DIS:  
       return "CTC_DKIT_EPE_PORT_REFLECTIVE_CHK_DIS, dest port is equal to source port";   
     case CTC_DKIT_EPE_IP_MPLS_TTL_CHK_ERR:  
       return "CTC_DKIT_EPE_IP_MPLS_TTL_CHK_ERR, IP/MPLS TTL check error when do L3 edit";   
     case CTC_DKIT_EPE_OAM_EDGE_PORT_DIS:  
       return "CTC_DKIT_EPE_OAM_EDGE_PORT_DIS, not find MEP/MIP at edge port for OAM packet";   
     case CTC_DKIT_EPE_NAT_PT_ICMP_ERR:  
       return "CTC_DKIT_EPE_NAT_PT_ICMP_ERR, NAT/PT ICMP error";   
     case CTC_DKIT_EPE_LATENCY_DISCARD:  
       return "CTC_DKIT_EPE_LATENCY_DISCARD, latency is too long";   
     case CTC_DKIT_EPE_LOCAL_OAM_DIS:  
       return "CTC_DKIT_EPE_LOCAL_OAM_DIS, local OAM discard(no use)";   
     case CTC_DKIT_EPE_OAM_FILTERING_DIS:  
       return "CTC_DKIT_EPE_OAM_FILTERING_DIS, OAM packet Port/STP/VLAN filter discard or up MEP send OAM packet to edge port or forward link OAM packet";   
     case CTC_DKIT_EPE_OAM_HASH_CONFILICT_DIS:  
       return "CTC_DKIT_EPE_OAM_HASH_CONFILICT_DIS, OAM hash confilict";   
     case CTC_DKIT_EPE_SAME_IPDA_IPSA_DIS:  
       return "CTC_DKIT_EPE_SAME_IPDA_IPSA_DIS, same MAC DA SA or IP DA SA";   
     case CTC_DKIT_EPE_HARD_ERROR_DIS:  
       return "CTC_DKIT_EPE_HARD_ERROR_DIS, hardware error";   
     case CTC_DKIT_EPE_TRILL_PLD_OP_DIS:  
       return "CTC_DKIT_EPE_TRILL_PLD_OP_DIS, TRILL payload operation discard(no use)";   
     case CTC_DKIT_EPE_PBB_CHK_FAIL_DIS:  
       return "CTC_DKIT_EPE_PBB_CHK_FAIL_DIS, PBB check fail";   
     case CTC_DKIT_EPE_DS_NEXTHOP_DATA_VIOLATE:  
       return "CTC_DKIT_EPE_DS_NEXTHOP_DATA_VIOLATE, DsNextHop data violate";   
     case CTC_DKIT_EPE_DEST_VLAN_PTR_DIS:  
       return "CTC_DKIT_EPE_DEST_VLAN_PTR_DIS, dest vlan index is invalid";   
     case CTC_DKIT_EPE_DS_L3EDIT_DATA_VIOLATE1:  
       return "CTC_DKIT_EPE_DS_L3EDIT_DATA_VIOLATE1, discard by L3 edit or inner L2 edit config";   
     case CTC_DKIT_EPE_DS_L3EDIT_DATA_VIOLATE2:  
       return "CTC_DKIT_EPE_DS_L3EDIT_DATA_VIOLATE2, discard by L3 edit or inner L2 edit violate";   
     case CTC_DKIT_EPE_DS_L3EDITNAT_DATA_VIOLATE:  
       return "CTC_DKIT_EPE_DS_L3EDITNAT_DATA_VIOLATE, L3 edit violate(no use)";   
     case CTC_DKIT_EPE_DS_L2EDIT_DATA_VIOLATE1:  
       return "CTC_DKIT_EPE_DS_L2EDIT_DATA_VIOLATE1, discard by L2 edit config";   
     case CTC_DKIT_EPE_DS_L2EDIT_DATA_VIOLATE2:  
       return "CTC_DKIT_EPE_DS_L2EDIT_DATA_VIOLATE2, discard by L2 edit violate";   
     case CTC_DKIT_EPE_PKT_HDR_C2C_TTL_ZERO:  
       return "CTC_DKIT_EPE_PKT_HDR_C2C_TTL_ZERO, packetHeader C2C TTL zero(no use)";   
     case CTC_DKIT_EPE_PT_UDP_CHKSUM_ZERO:  
       return "CTC_DKIT_EPE_PT_UDP_CHKSUM_ZERO, PT/UDP checksum is zero for NAT";   
     case CTC_DKIT_EPE_OAM_TO_LOCAL_DIS:  
       return "CTC_DKIT_EPE_OAM_TO_LOCAL_DIS, send to local up MEP";   
     case CTC_DKIT_EPE_HDR_ADJ_RX_DEST_ID:  
       return "CTC_DKIT_EPE_HDR_ADJ_RX_DEST_ID, BufRetrv packets with DestId discard";   
     case CTC_DKIT_EPE_HDR_ADJ_MIN_PKT_LEN:  
       return "CTC_DKIT_EPE_HDR_ADJ_MIN_PKT_LEN, packets length in BufRetrv is smaller than the minimum required value";   
     case CTC_DKIT_EPE_HDR_ADJ_TX_PI_HARD:  
       return "CTC_DKIT_EPE_HDR_ADJ_TX_PI_HARD, EpeHdrAdj packets with harderror";   
     case CTC_DKIT_EPE_HDR_ADJ_TX_PI_DISCARD:  
       return "CTC_DKIT_EPE_HDR_ADJ_TX_PI_DISCARD, EpeHdrAdj packets with pi-discard";   
     case CTC_DKIT_EPE_HDR_ADJ_RCV_PKT_ERROR:  
       return "CTC_DKIT_EPE_HDR_ADJ_RCV_PKT_ERROR, BufRetrv packets with pktError";   
     case CTC_DKIT_EPE_HDR_EDIT_COMPLETE_DISCARD:  
       return "CTC_DKIT_EPE_HDR_EDIT_COMPLETE_DISCARD, packet discard at EpeHdrEdit";   
     case CTC_DKIT_EPE_HDR_EDIT_TX_PI_DISCARD:  
       return "CTC_DKIT_EPE_HDR_EDIT_TX_PI_DISCARD, EpeHdrEdit packets with pi-discard";   
     case CTC_DKIT_EPE_HDR_EDIT_EPE_TX_DATA:  
       return "CTC_DKIT_EPE_HDR_EDIT_EPE_TX_DATA, EpeHdrEdit packets with data error";   
     case CTC_DKIT_EPE_HDR_PROC_L2_EDIT:  
       return "CTC_DKIT_EPE_HDR_PROC_L2_EDIT, some hardware error occurred when do L2 edit";   
     case CTC_DKIT_EPE_HDR_PROC_L3_EDIT:  
       return "CTC_DKIT_EPE_HDR_PROC_L3_EDIT, some hardware error occurred when do L3 edit";   
     case CTC_DKIT_EPE_HDR_PROC_INNER_L2:  
       return "CTC_DKIT_EPE_HDR_PROC_INNER_L2, some hardware error occurred when do inner L2 edit";   
     case CTC_DKIT_EPE_HDR_PROC_PAYLOAD:  
       return "CTC_DKIT_EPE_HDR_PROC_PAYLOAD, some hardware error occurred when do payload process";   
     case CTC_DKIT_EPE_OTHER_HW_NEXT_HOP:  
       return "CTC_DKIT_EPE_OTHER_HW_NEXT_HOP, some hardware error occurred at nexthop mapper";   
     case CTC_DKIT_EPE_OTHER_HW_ACL_OAM:  
       return "CTC_DKIT_EPE_OTHER_HW_ACL_OAM, some hardware error occurred when do OAM lookup";   
     case CTC_DKIT_EPE_OTHER_HW_CLASS:  
       return "CTC_DKIT_EPE_OTHER_HW_CLASS, some hardware error occurred at classification";   
     case CTC_DKIT_EPE_OTHER_HW_OAM:  
       return "CTC_DKIT_EPE_OTHER_HW_OAM, some hardware error occurred when do OAM process";   
     case CTC_DKIT_EPE_MAX:  
       return "";   
     case CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL:  
       return "CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL, resource check fail discard, read PktErrStatsMem to confirm";   
     case CTC_DKIT_BSR_PKTERR_SILENT_SELF_ORIG:  
       return "CTC_DKIT_BSR_PKTERR_SILENT_SELF_ORIG, fabric source chip discard, read PktErrStatsMem to confirm";   
     case CTC_DKIT_BSR_PKTERR_SILENT_HARD_DISCARD:  
       return "CTC_DKIT_BSR_PKTERR_SILENT_HARD_DISCARD, exception dest id discard from IPE, read PktErrStatsMem to confirm";   
     case CTC_DKIT_BSR_PKTERR_SILENT_CHIP_MISMATCH:  
       return "CTC_DKIT_BSR_PKTERR_SILENT_CHIP_MISMATCH, chip id mismatch discard, read PktErrStatsMem to confirm";   
     case CTC_DKIT_BSR_PKTERR_SILENT_NO_BUF:  
       return "CTC_DKIT_BSR_PKTERR_SILENT_NO_BUF, pkt buffer is full, read PktErrStatsMem to confirm";   
     case CTC_DKIT_BSR_PKTERR_SILENT_DATA_ERROR:  
       return "CTC_DKIT_BSR_PKTERR_SILENT_DATA_ERROR, crc error or ecc error, read PktErrStatsMem to confirm";   
     case CTC_DKIT_BSR_PKTERR_ABORT_UNDER_LEN:  
       return "CTC_DKIT_BSR_PKTERR_ABORT_UNDER_LEN, packet length is smaller than the minimum required value, read PktErrStatsMem to confirm";   
     case CTC_DKIT_BSR_PKTERR_ABORT_MET_FIFO:  
       return "CTC_DKIT_BSR_PKTERR_ABORT_MET_FIFO, packet discard due to the copy ability of MET, read PktErrStatsMem to confirm";   
     case CTC_DKIT_BSR_PKTERR_ABORT_NO_BUF:  
       return "CTC_DKIT_BSR_PKTERR_ABORT_NO_BUF, pkt buffer is full, read PktErrStatsMem to confirm";   
     case CTC_DKIT_BSR_PKTERR_ABORT_FRAME_ERROR:  
       return "CTC_DKIT_BSR_PKTERR_ABORT_FRAME_ERROR, packet discard due to miss EOP, read PktErrStatsMem to confirm";   
     case CTC_DKIT_BSR_PKTERR_ABORT_OVER_LEN:  
       return "CTC_DKIT_BSR_PKTERR_ABORT_OVER_LEN, packet length is larger than the maximum required value, read PktErrStatsMem to confirm";   
     case CTC_DKIT_BSR_PKTERR_ABORT_DATA_ERROR:  
       return "CTC_DKIT_BSR_PKTERR_ABORT_DATA_ERROR, crc error or ecc error, read PktErrStatsMem to confirm";   
     case CTC_DKIT_BSR_BUFSTORE_IPE_HDR_UCAST:  
       return "CTC_DKIT_BSR_BUFSTORE_IPE_HDR_UCAST, ucast discard due to the copy ability of MET in cutthrough model, read BufStoreStallDropDebugStats to confirm";   
     case CTC_DKIT_BSR_BUFSTORE_IPE_HDR_MCAST:  
       return "CTC_DKIT_BSR_BUFSTORE_IPE_HDR_MCAST, mcast discard due to the copy ability of MET in cutthrough model, read BufStoreStallDropDebugStats to confirm";   
     case CTC_DKIT_BSR_BUFSTORE_IPE_UCAST:  
       return "CTC_DKIT_BSR_BUFSTORE_IPE_UCAST, ucast discard in IPE interface due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm";   
     case CTC_DKIT_BSR_BUFSTORE_IPE_MCAST:  
       return "CTC_DKIT_BSR_BUFSTORE_IPE_MCAST, mcast discard in IPE interface due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm";   
     case CTC_DKIT_BSR_BUFSTORE_ELOOP_UCAST:  
       return "CTC_DKIT_BSR_BUFSTORE_ELOOP_UCAST, eloop ucast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm";   
     case CTC_DKIT_BSR_BUFSTORE_ELOOP_MCAST:  
       return "CTC_DKIT_BSR_BUFSTORE_ELOOP_MCAST, eloop mcast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm";   
     case CTC_DKIT_BSR_BUFSTORE_QCN_UCAST:  
       return "CTC_DKIT_BSR_BUFSTORE_QCN_UCAST, QCN ucast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm";   
     case CTC_DKIT_BSR_BUFSTORE_QCN_MCAST:  
       return "CTC_DKIT_BSR_BUFSTORE_QCN_MCAST, QCN mcast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm";   
     case CTC_DKIT_BSR_BUFSTORE_OAM_UCAST:  
       return "CTC_DKIT_BSR_BUFSTORE_OAM_UCAST, OAM ucast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm";   
     case CTC_DKIT_BSR_BUFSTORE_OAM_MCAST:  
       return "CTC_DKIT_BSR_BUFSTORE_OAM_MCAST, OAM mcast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm";   
     case CTC_DKIT_BSR_BUFSTORE_DMA_UCAST:  
       return "CTC_DKIT_BSR_BUFSTORE_DMA_UCAST, DMA ucast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm";   
     case CTC_DKIT_BSR_BUFSTORE_DMA_MCAST:  
       return "CTC_DKIT_BSR_BUFSTORE_DMA_MCAST, DMA mcast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm";   
     case CTC_DKIT_BSR_QMGRENQ_SPAN_ON_DROP:  
       return "CTC_DKIT_BSR_QMGRENQ_SPAN_ON_DROP, packet discard due to Egress Resource Check and spanning to specific queue, read QMgrEnqDebugStats to confirm";   
     case CTC_DKIT_BSR_QMGRENQ_ENQ_DISCARD:  
       return "CTC_DKIT_BSR_QMGRENQ_ENQ_DISCARD, the mcastLinkAggregation discard or stacking discard or noLinkAggMem discard when do LinkAgg, read QMgrEnqDebugStats to confirm";   
     case CTC_DKIT_BSR_QMGRENQ_FREE_USED:  
       return "CTC_DKIT_BSR_QMGRENQ_FREE_USED, packet discard due to the use up of Queue Entry, read QMgrEnqDebugStats to confirm";   
     case CTC_DKIT_BSR_QMGRENQ_WRED_DISCARD:  
       return "CTC_DKIT_BSR_QMGRENQ_WRED_DISCARD, packet discard due to WRED, read QMgrEnqDebugStats to confirm";   
     case CTC_DKIT_BSR_QMGRENQ_EGR_RESRC_MGR:  
       return "CTC_DKIT_BSR_QMGRENQ_EGR_RESRC_MGR, packet discard due to the satisfication of the Egress Resource Check, read QMgrEnqDebugStats to confirm";   
     case CTC_DKIT_BSR_QMGRENQ_RESERVE_CHANNEL:  
       return "CTC_DKIT_BSR_QMGRENQ_RESERVE_CHANNEL, packet discard due to the mapping to reserved channel, read QMgrEnqDebugStats to confirm";   
     case CTC_DKIT_BSR_QMGRENQ_CRITICAL_PKT:  
       return "CTC_DKIT_BSR_QMGRENQ_CRITICAL_PKT, critical packets, read QMgrEnqDebugStats to confirm";   
     case CTC_DKIT_BSR_QMGRENQ_C2C_PKT:  
       return "CTC_DKIT_BSR_QMGRENQ_C2C_PKT, C2C packets, read QMgrEnqDebugStats to confirm";   
     case CTC_DKIT_BSR_QMGRENQ_NO_CHANNEL_LINKAGG:  
       return "CTC_DKIT_BSR_QMGRENQ_NO_CHANNEL_LINKAGG, noLinkAggMem discard due to channel LinkAgg when do LinkAgg, read QMgrEnqDebugStats to confirm";   
     case CTC_DKIT_BSR_QMGRENQ_NO_PORT_LINKAGG:  
       return "CTC_DKIT_BSR_QMGRENQ_NO_PORT_LINKAGG, noLinkAggMem discard due to port LinkAgg when do LinkAgg, read QMgrEnqDebugStats to confirm";   
     case CTC_DKIT_BSR_QMGRENQ_MCAST_LINKAGG:  
       return "CTC_DKIT_BSR_QMGRENQ_MCAST_LINKAGG, mcastLinkAggregation discard when do LinkAgg, read QMgrEnqDebugStats to confirm";   
     case CTC_DKIT_BSR_QMGRENQ_STACKING_DISCARD:  
       return "CTC_DKIT_BSR_QMGRENQ_STACKING_DISCARD, stacking discard when do LinkAgg, read QMgrEnqDebugStats to confirm";   
     case CTC_DKIT_BSR_OTHER_HW_ENQ_DISCARD:  
       return "CTC_DKIT_BSR_OTHER_HW_ENQ_DISCARD, enqueue discard sending to QMgr by MetFifo, read MetFifoDebugStats to confirm";   
     case CTC_DKIT_BSR_OTHER_HW_AGE_DISCARD:  
       return "CTC_DKIT_BSR_OTHER_HW_AGE_DISCARD, dequeue discard due to the aging of DsQueueEntry, read QMgrDeqSliceDebugStats to confirm";   
     case CTC_DKIT_BSR_MAX:  
       return "";   
     case CTC_DKIT_NETRX_LEN_ERROR:  
       return "CTC_DKIT_NETRX_LEN_ERROR, packet discard due to the failure of max or min pkt len check, read NetRxDebugStatsTable to confirm";   
     case CTC_DKIT_NETRX_FRAME_ERROR:  
       return "CTC_DKIT_NETRX_FRAME_ERROR, packet discard due to misssing sop or eop, read NetRxDebugStatsTable to confirm";   
     case CTC_DKIT_NETRX_PKT_ERROR:  
       return "CTC_DKIT_NETRX_PKT_ERROR, packet discard due to error, read NetRxDebugStatsTable to confirm";   
     case CTC_DKIT_NETRX_FULL_DROP:  
       return "CTC_DKIT_NETRX_FULL_DROP, full drop or earlyTerm discard due to the use up of buffer in port oversub model, read NetRxDebugStatsTable to confirm";   
     case CTC_DKIT_NETRX_BPDU0_ERROR:  
       return "CTC_DKIT_NETRX_BPDU_ERROR0, BPDU discard, read NetRxDebugStats to confirm";   
     case CTC_DKIT_NETRX_BPDU1_ERROR:  
       return "CTC_DKIT_NETRX_BPDU_ERROR1, BPDU discard, read NetRxDebugStats to confirm";   
     case CTC_DKIT_NETRX_BPDU2_ERROR:  
       return "CTC_DKIT_NETRX_BPDU_ERROR2, BPDU discard, read NetRxDebugStats to confirm";   
     case CTC_DKIT_NETRX_BPDU3_ERROR:  
       return "CTC_DKIT_NETRX_BPDU_ERROR3, BPDU discard, read NetRxDebugStats to confirm";   
     case CTC_DKIT_NETRX_PAUSE0_ERROR:  
       return "CTC_DKIT_NETRX_PAUSE_ERROR0, PAUSE discard, read NetRxDebugStats to confirm";   
     case CTC_DKIT_NETRX_PAUSE1_ERROR:  
       return "CTC_DKIT_NETRX_PAUSE_ERROR1, PAUSE discard, read NetRxDebugStats to confirm";   
     case CTC_DKIT_NETRX_PAUSE2_ERROR:  
       return "CTC_DKIT_NETRX_PAUSE_ERROR2, PAUSE discard, read NetRxDebugStats to confirm";   
     case CTC_DKIT_NETRX_PAUSE3_ERROR:  
       return "CTC_DKIT_NETRX_PAUSE_ERROR3, PAUSE discard, read NetRxDebugStats to confirm";   
     case CTC_DKIT_NETRX_TX_ERROR:  
       return "CTC_DKIT_NETRX_TX_ERROR, the number of error packets sending to IPE";   
     case CTC_DKIT_NETRX_MAX:  
       return "";   
     case CTC_DKIT_NETTX_EPE_NO_BUF:  
       return "CTC_DKIT_NETTX_EPE_NO_BUF, packet discard due to the use up of PktBuffer in NETTX";   
     case CTC_DKIT_NETTX_EPE_NO_EOP:  
       return "CTC_DKIT_NETTX_EPE_NO_EOP, packet discard due to missing EOP";   
     case CTC_DKIT_NETTX_EPE_NO_SOP:  
       return "CTC_DKIT_NETTX_EPE_NO_SOP, packet discard due to missing SOP";   
     case CTC_DKIT_NETTX_INFO:  
       return "CTC_DKIT_NETTX_INFO, the packet which need to be discarded";   
     case CTC_DKIT_NETTX_EPE_MIN_LEN:  
       return "CTC_DKIT_NETTX_EPE_MIN_LEN, packet discard due to smaller than the cfgMinPktLen";   
     case CTC_DKIT_NETTX_SPAN_MIN_LEN:  
       return "CTC_DKIT_NETTX_SPAN_MIN_LEN, packet discard due to smaller than the cfgMinPktLen";   
     case CTC_DKIT_NETTX_SPAN_NO_BUF:  
       return "CTC_DKIT_NETTX_SPAN_NO_BUF, packet discard due to the use up of PktBuffer in NETTX";   
     case CTC_DKIT_NETTX_SPAN_NO_EOP:  
       return "CTC_DKIT_NETTX_SPAN_NO_EOP, packet discard due to missing EOP";   
     case CTC_DKIT_NETTX_SPAN_NO_SOP:  
       return "CTC_DKIT_NETTX_SPAN_NO_SOP, packet discard due to missing SOP";   
     case CTC_DKIT_NETTX_MAX:  
       return "";   
     case CTC_DKIT_OAM_LINK_AGG_NO_MEMBER:  
       return "CTC_DKIT_OAM_LINK_AGG_NO_MEMBER, linkagg no member when tx CCM or reply LBR/LMR/DMR/SLR";   
     case CTC_DKIT_OAM_ASIC_ERROR:  
       return "CTC_DKIT_OAM_ASIC_ERROR, some hardware error occured";   
     case CTC_DKIT_OAM_EXCEPTION:  
       return "CTC_DKIT_OAM_EXCEPTION, some exception occured when process OAM packet";   
     case CTC_DKIT_OAM_MAX:  
       return "";   
      default:
      sal_sprintf(reason_desc,"Reason id:%d",reason_id);
      return reason_desc;
    }

}

const char *ctc_goldengate_dkit_get_sub_reason_desc(ctc_dkit_discard_sub_reason_id_t reason_id)
{
    static char  reason_desc[256+1] = {0};
    if (reason_id == CTC_DKIT_SUB_DISCARD_INVALIED)
    {
        return " ";
    }

    switch(reason_id)
    {
     case CTC_DKIT_SUB_DISCARD_INVALIED:  
       return "no discard";   
     case CTC_DKIT_SUB_DISCARD_AMBIGUOUS:  
       return "the discard reason is ambiguous, try to use captured info";   
     case CTC_DKIT_SUB_DISCARD_NEED_PARAM:  
       return "please input filter param to get the detail reason";   
     case CTC_DKIT_SUB_IPE_PARSER_LEN_ERR_USERID:  
       return "parser length error at SCL";   
     case CTC_DKIT_SUB_IPE_PARSER_LEN_ERR_ACL:  
       return "parser length error at ACL";   
     case CTC_DKIT_SUB_IPE_EXCEP2_DIS_BPDU:  
       return "BPDU, MAC DA is 0x0180c2000000, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_EXCEP2_DIS_SLOW:  
       return "SLOW protocol, ether type is 0x8809, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_EXCEP2_DIS_EAPOL:  
       return "EAPOL, ether type is 0x888E, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_EXCEP2_DIS_LLDP:  
       return "LLDP, ether type is 0x88CC, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_EXCEP2_DIS_L2ISIS:  
       return "L2ISIS, ether type is 0x22F4, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_EXCEP2_DIS_CAM:  
       return "MAC DA match in cam, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_RECEIVE_DIS_PORT:  
       return "port receive is disable";   
     case CTC_DKIT_SUB_IPE_RECEIVE_DIS_VLAN:  
       return "vlan receive is disable";   
     case CTC_DKIT_SUB_IPE_AFT_DIS_ALLOW_ALL:  
       return "AFT is config as allow all packets";   
     case CTC_DKIT_SUB_IPE_AFT_DIS_DROP_UNTAGGED:  
       return "AFT is config as drop all untagged";   
     case CTC_DKIT_SUB_IPE_AFT_DIS_DROP_TAGGED:  
       return "AFT is config as drop all tagged";   
     case CTC_DKIT_SUB_IPE_AFT_DIS_DROP_ALL:  
       return "AFT is config as drop all (same with receiveEn = 0)";   
     case CTC_DKIT_SUB_IPE_AFT_DIS_DROP_WITHOUT_2_TAG:  
       return "AFT is config as drop packet without 2 vlan tag";   
     case CTC_DKIT_SUB_IPE_AFT_DIS_DROP_WITH_2_TAG:  
       return "AFT is config as drop all packet with 2 vlan tag";   
     case CTC_DKIT_SUB_IPE_AFT_DIS_DROP_STAG:  
       return "AFT is config as drop all S-VLAN tag";   
     case CTC_DKIT_SUB_IPE_AFT_DIS_DROP_NO_STAG:  
       return "AFT is config as drop all non S-VLAN tag";   
     case CTC_DKIT_SUB_IPE_AFT_DIS_DROP_ONLY_STAG:  
       return "AFT is config as drop only S-VLAN tag";   
     case CTC_DKIT_SUB_IPE_AFT_DIS_PERMIT_ONLY_STAG:  
       return "AFT is config as permit only S-VLAN tag";   
     case CTC_DKIT_SUB_IPE_AFT_DIS_DROP_CTAG:  
       return "AFT is config as drop all C-VLAN tag";   
     case CTC_DKIT_SUB_IPE_AFT_DIS_DROP_NO_CTAG:  
       return "AFT is config as drop all non C-VLAN tag";   
     case CTC_DKIT_SUB_IPE_AFT_DIS_DROP_ONLY_CTAG:  
       return "AFT is config as drop only C-VLAN tag";   
     case CTC_DKIT_SUB_IPE_AFT_DIS_PERMIT_ONLY_CTAG:  
       return "AFT is config as permit only C-VLAN tag";   
     case CTC_DKIT_SUB_IPE_ILLEGAL_PKT_DIS_MCAST:  
       return "Mcast MAC DA is config as discard";   
     case CTC_DKIT_SUB_IPE_ILLEGAL_PKT_DIS_SAME_MAC:  
       return "the packet has the same MAC DA and SA";   
     case CTC_DKIT_SUB_IPE_ARP_DHCP_DIS_V4_DSCP:  
       return "IPv4 DSCP packet discard";   
     case CTC_DKIT_SUB_IPE_ARP_DHCP_DIS_V6_DSCP:  
       return "IPv6 DSCP packet discard";   
     case CTC_DKIT_SUB_IPE_ARP_DHCP_DIS_ARP:  
       return "ARP packet discard";   
     case CTC_DKIT_SUB_IPE_ARP_DHCP_DIS_FIP:  
       return "FIP packet discard";   
     case CTC_DKIT_SUB_IPE_ROUTE_MC_IPADD_DIS_V4:  
       return "IPv4 mcast MAC DA and IP DA check fail";   
     case CTC_DKIT_SUB_IPE_ROUTE_MC_IPADD_DIS_V6:  
       return "IPv6 mcast MAC DA and IP DA check fail";   
     case CTC_DKIT_SUB_IPE_ROUTE_MC_IPADD_DIS_PROTOCOL:  
       return "IP Protocal Translation discard";   
     case CTC_DKIT_SUB_IPE_SECURITY_CHK_DIS_PORT_MATCH:  
       return "port match discard";   
     case CTC_DKIT_SUB_IPE_SECURITY_CHK_DIS_MCAST:  
       return "unknown mcast discard";   
     case CTC_DKIT_SUB_IPE_SECURITY_CHK_DIS_UCAST:  
       return "unknown ucast discard";   
     case CTC_DKIT_SUB_IPE_SECURITY_CHK_DIS_BCAST:  
       return "bcast discard";   
     case CTC_DKIT_SUB_IPE_LEARNING_DIS_SRC_MAC:  
       return "the MAC SA FDB is config as discard";   
     case CTC_DKIT_SUB_IPE_LEARNING_DIS_SRC_PORT:  
       return "the global source port is not config as it in the MAC SA FDB, they are ";   
     case CTC_DKIT_SUB_IPE_LEARNING_LOGIC_DIS_SRC_PORT:  
       return "the logic source port is not config as it in the MAC SA FDB, they are ";   
     case CTC_DKIT_SUB_IPE_LEARNING_DIS_PORT_SECURITY:  
       return "";   
     case CTC_DKIT_SUB_IPE_LEARNING_DIS_VSI_SECURITY:  
       return "";   
     case CTC_DKIT_SUB_IPE_LEARNING_DIS_SYSTEM_SECURITY:  
       return "";   
     case CTC_DKIT_SUB_IPE_LEARNING_DIS_PROFILE_MAX:  
       return "";   
     case CTC_DKIT_SUB_IPE_FATAL_UC_IP_HDR_ERROR_OR_MARTION_ADDR:  
       return "ucast IP version/header length/checksum error, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_FATAL_UC_IP_OPTIONS:  
       return "ucast IPv4 header length>5/ IPv6 protocol=0/ Trill option!=0, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_FATAL_UC_GRE_UNKNOWN_OPT_PROTOCAL:  
       return "ucast GRE protocol or option is unknown, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_FATAL_UC_ISATAP_SRC_ADDR_CHK_FAIL:  
       return "ucast ISATAP source address check fail, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_FATAL_UC_IPTTL_CHK_FAIL:  
       return "ucast TTL check fail, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_FATAL_UC_RPF_CHK_FAIL:  
       return "ucast RPF check fail, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_FATAL_UC_OR_MC_MORERPF:  
       return "ucast or mcast multi RPF check fail, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_FATAL_UC_LINKID_CHK_FAIL:  
       return "ucast linkid check fail, redirect to CPU(no use)";   
     case CTC_DKIT_SUB_IPE_FATAL_MPLS_LABEL_OUTOFRANGE:  
       return "MPLS label out of range, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_FATAL_MPLS_SBIT_ERROR:  
       return "MPLS sbit check fail, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_FATAL_MPLS_TTL_CHK_FAIL:  
       return "MPLS TTL check fail, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_FATAL_FCOE_VIRTUAL_LINK_CHK_FAIL:  
       return "FCOE RPF or MAC SA check fail, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_FATAL_IGMP_SNOOPING_PKT:  
       return "IGMP snooping, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_FATAL_TRILL_OPTION:  
       return "TRILL header check error, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_FATAL_TRILL_TTL_CHK_FAIL:  
       return "TRILL TTL check fail, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_FATAL_VXLAN_OR_NVGRE_CHK_FAIL:  
       return "VXLAN or NVGRE check fail, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_IPV4_SAME_IP:  
       return "IPv4 packet has the same IP DA and SA";   
     case CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_IPV6_SAME_IP:  
       return "IPv6 packet has the same IP DA and SA";   
     case CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_TCP_L4_ERROR0:  
       return "TCP SYN=0";   
     case CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_TCP_L4_ERROR1:  
       return "TCP flags=0 and sequence number=0 ";   
     case CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_TCP_L4_ERROR2:  
       return "TCP SYN=0, PSH=0, URG=0, and sequence number=0";   
     case CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_TCP_L4_ERROR3:  
       return "TCP FIN=0, SYN=0";   
     case CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_TCP_L4_ERROR4:  
       return "TCP souce port is equal to dest port";   
     case CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_UDP_L4_ERROR4:  
       return "TCP souce port is equal to dest port";   
     case CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_IGMP_FRAG_ERROR:  
       return "IGMP fragment check error";   
     case CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_GRE_VERSION_ERROR:  
       return "GRE version check error";   
     case CTC_DKIT_SUB_IPE_MPLSMC_CHECK_DIS_CONTEXT:  
       return "mcast MPLS with context label MAC DA check fail";   
     case CTC_DKIT_SUB_IPE_MPLSMC_CHECK_DIS:  
       return "mcast MPLS without context label MAC DA check fail";   
     case CTC_DKIT_SUB_IPE_TRILL_FILTER_ERR_TRILL_DIS:  
       return "TRILL packet discard by source port";   
     case CTC_DKIT_SUB_IPE_TRILL_FILTER_ERR_NON_TRILL_DIS:  
       return "non-TRILL packet discard by source port";   
     case CTC_DKIT_SUB_IPE_TRILL_FILTER_ERR_TRILL_UCAST:  
       return "ucast TRILL packet without port MAC DA discard";   
     case CTC_DKIT_SUB_IPE_L3_EXCEP_DIS_FRAG:  
       return "VXLAN or NVGRE packet whose fraginfo should be 0, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_L3_EXCEP_DIS_ACH:  
       return "ACH type is wrong for ACH OAM or control word is not 0, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_L3_EXCEP_DIS_ROUTE:  
       return "the IP is config as redirect to CPU";   
     case CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_VERSION:  
       return "TRILL packet version check fail, the version should be ";   
     case CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_BFD_MCAST:  
       return "mcast TRILL packet discard";   
     case CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_BFD_MULTIHOP:  
       return "multihop TRILL BFD packet discard, the TTL should >";   
     case CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_BFD_1HOP:  
       return "1hop TRILL BFD packet discard, the TTL should be 0x3F";   
     case CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_INNER_VLAN_INVALID:  
       return "TRILL packet discard, the inner vlan id should not be 0xFFF or 0";   
     case CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_INNER_VLAN_ABSENT:  
       return "TRILL packet without inner vlan discard";   
     case CTC_DKIT_SUB_IPE_PTP_EXCEP_DIS_MESSAGE_DIS:  
       return "PTP message config as discard, message type is ";   
     case CTC_DKIT_SUB_IPE_PTP_EXCEP_DIS_V1:  
       return "PTPv1 message discard, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_PTP_EXCEP_DIS_HIGH_VERSION:  
       return "PTP high version message discard, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_PTP_EXCEP_DIS_UNKNOWN:  
       return "unkonwn PTP message in MPLS tunnel discard";   
     case CTC_DKIT_SUB_IPE_OAM_NOT_FIND_DIS_CONFLICT_ETHER:  
       return "ether OAM hash lookup conflict, key: globalSrcPort, vlanId, isFid are ";   
     case CTC_DKIT_SUB_IPE_OAM_NOT_FIND_DIS_EDGE_PORT:  
       return "ether OAM packet discard on edge port, key: globalSrcPort, vlanId, isFid are ";   
     case CTC_DKIT_SUB_IPE_OAM_FILTER_DIS_VLAN:  
       return "ether OAM packet discard by vlan filtering, dsvlan index=";   
     case CTC_DKIT_SUB_IPE_OAM_FILTER_DIS_STP:  
       return "ether OAM packet discard by STP filtering";   
     case CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_ETHER:  
       return "ether OAM not find MEP or MIP, key: globalSrcPort, vlanId, isFid are ";   
     case CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_ACH_SECTION_PORT:  
       return "ACH port based section OAM not find MEP or MIP, key: localPhyPort is ";   
     case CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_ACH_SECTION_INTF:  
       return "ACH interface based section OAM not find MEP or MIP, key: interfaceId is ";   
     case CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_ACH:  
       return "ACH OAM not find MEP or MIP, key: mplsOamIndex is ";   
     case CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_MPLS_BFD:  
       return "MPLS BFD not find MEP, key: bfdMyDiscriminator is ";   
     case CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_IP_BFD:  
       return "IP BFD not find MEP, key: bfdMyDiscriminator is ";   
     case CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_TRILL_BFD:  
       return "TRILL BFD not find MEP";   
     case CTC_DKIT_SUB_IPE_ICMP_ERR_MSG_DIS_IPV4:  
       return "IPv4 ICMP, l4 source port is 0x03XX or 0x04XX or 0x11XX or 0x12XX, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_ICMP_ERR_MSG_DIS_IPV6:  
       return "IPv6 ICMP, l4 source port is 0, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_ICMP_ERR_MSG_DIS_PT:  
       return "ICMP packet discard in PT scenario, redirect to CPU";   
     case CTC_DKIT_SUB_IPE_VXLAN_NVGRE_CHK_FAIL_VXLAN:  
       return "the VxLAN tunnel is not exist on the Gateway device";   
     case CTC_DKIT_SUB_IPE_VXLAN_NVGRE_CHK_FAIL_NVGRE:  
       return "the NvGRE tunnel is not exist on the Gateway device";   
     case CTC_DKIT_SUB_IPE_VXLAN_NVGRE_INNER_VTAG_CHK_DIS_VXLAN:  
       return "VXLAN packet with inner VTAG discard";   
     case CTC_DKIT_SUB_IPE_VXLAN_NVGRE_INNER_VTAG_CHK_DIS_NVGRE:  
       return "VXLAN packet with inner VTAG discard";   
     case CTC_DKIT_SUB_IPE_PORT_MAC_CHECK_DIS_DA:  
       return "MAC DA is in port MAC range";   
     case CTC_DKIT_SUB_IPE_PORT_MAC_CHECK_DIS_SA:  
       return "MAC SA is in port MAC range";   
     case CTC_DKIT_SUB_IPE_FWD_PORT:  
       return "";   
     case CTC_DKIT_SUB_IPE_FWD_USERID:  
       return "";   
     case CTC_DKIT_SUB_IPE_FWD_TUNNEL:  
       return "the packet forward by tunnel";   
     case CTC_DKIT_SUB_IPE_FWD_TUNNEL_ECMP:  
       return "the packet forward by tunnel, and then do ECMP, ECMP group id is ";   
     case CTC_DKIT_SUB_IPE_FWD_TUNNEL_IP:  
       return "the packet forward by IP after tunnel process";   
     case CTC_DKIT_SUB_IPE_FWD_TUNNEL_IP_ECMP:  
       return "the packet forward by IP after tunnel process, and then do ECMP, ECMP group id is ";   
     case CTC_DKIT_SUB_IPE_FWD_TUNNEL_BRIDGE:  
       return "the packet forward by MAC after tunnel process";   
     case CTC_DKIT_SUB_IPE_FWD_TUNNEL_BRIDGE_ECMP:  
       return "the packet forward by MAC after tunnel process, and then do ECMP, ECMP group id is ";   
     case CTC_DKIT_SUB_IPE_FWD_MPLS:  
       return "the packet forward by mpls";   
     case CTC_DKIT_SUB_IPE_FWD_MPLS_ECMP:  
       return "the packet forward by mpls, and the do ECMP, ECMP group id is";   
     case CTC_DKIT_SUB_IPE_FWD_MPLS_IP:  
       return "the packet forward by IP after mpls process";   
     case CTC_DKIT_SUB_IPE_FWD_MPLS_IP_ECMP:  
       return "the packet forward by IP after mpls process, and then do ECMP, ECMP group id is ";   
     case CTC_DKIT_SUB_IPE_FWD_MPLS_BRIDGE:  
       return "the packet forward by MAC after mpls process";   
     case CTC_DKIT_SUB_IPE_FWD_MPLS_BRIDGE_ECMP:  
       return "the packet forward by MAC after mpls process, and then do ECMP, ECMP group id is ";   
     case CTC_DKIT_SUB_IPE_FWD_ACL:  
       return "";   
     case CTC_DKIT_SUB_IPE_FWD_ROUTE:  
       return "the packet forward by IP";   
     case CTC_DKIT_SUB_IPE_FWD_ROUTE_ECMP:  
       return "the packet forward by IP, and then do ECMP, ECMP group id is ";   
     case CTC_DKIT_SUB_IPE_FWD_ROUTE_BRIDGE:  
       return "the packet forward by MAC after IP process";   
     case CTC_DKIT_SUB_IPE_FWD_ROUTE_BRIDGE_ECMP:  
       return "the packet forward by MAC after IP process, and then do ECMP, ECMP group id is ";   
     case CTC_DKIT_SUB_IPE_FWD_FCOE:  
       return "the packet forward by FCOE";   
     case CTC_DKIT_SUB_IPE_FWD_TRILL:  
       return "the packet forward by TRILL";   
     case CTC_DKIT_SUB_IPE_FWD_BRIDGE:  
       return "the packet forward by MAC and hit a FDB entry";   
     case CTC_DKIT_SUB_IPE_FWD_BRIDGE_ECMP:  
       return "the packet forward by MAC and hit a FDB entry, and then do ECMP, ECMP group id is ";   
     case CTC_DKIT_SUB_IPE_FWD_BRIDGE_NOT_HIT:  
       return "the packet forward by MAC but not hit FDB entry";   
     case CTC_DKIT_SUB_IPE_FWD_BRIDGE_NOT_HIT_ECMP:  
       return "the packet forward by MAC but use default FDB entry, and then do ECMP, ECMP group id is ";   
     case CTC_DKIT_SUB_EPE_PORT_ISO_DIS_PVLAN_P2X:  
       return "Pvlan isolation discard, from promiscuous port";   
     case CTC_DKIT_SUB_EPE_PORT_ISO_DIS_PVLAN_C2C:  
       return "Pvlan isolation discard, from community port to community port, source and dest community id are ";   
     case CTC_DKIT_SUB_EPE_PORT_ISO_DIS_PVLAN_X2P:  
       return "Pvlan isolation discard, to promiscuous port";   
     case CTC_DKIT_SUB_EPE_PORT_ISO_DIS_UNKNOWN_UC_ISOLATION:  
       return "port isolation discard, unknown ucast isolation, source and dest isolation id are ";   
     case CTC_DKIT_SUB_EPE_PORT_ISO_DIS_KNOWN_UC_ISOLATION:  
       return "port isolation discard, known ucast isolation, source and dest isolation id are ";   
     case CTC_DKIT_SUB_EPE_PORT_ISO_DIS_UNKNOWN_MC_ISOLATION:  
       return "port isolation discard, unknown mcast isolation, source and dest isolation id are ";   
     case CTC_DKIT_SUB_EPE_PORT_ISO_DIS_KNOWN_MC_ISOLATION:  
       return "port isolation discard, known mcast isolation, source and dest isolation id are ";   
     case CTC_DKIT_SUB_EPE_PORT_ISO_DIS_BC_ISOLATION:  
       return "port isolation discard, bcast isolation, source and dest isolation id are ";   
     case CTC_DKIT_SUB_EPE_VPLS_HORIZON_SPLIT_DIS_HORIZON:  
       return "VPLS horrizon split discard";   
     case CTC_DKIT_SUB_EPE_VPLS_HORIZON_SPLIT_DIS_E_TREE:  
       return "E-tree discard";   
     case CTC_DKIT_SUB_EPE_UC_MC_FLOODING_DIS_UNKNOWN_UC:  
       return "unknown ucast discard";   
     case CTC_DKIT_SUB_EPE_UC_MC_FLOODING_DIS_KNOWN_UC:  
       return "known ucast discard";   
     case CTC_DKIT_SUB_EPE_UC_MC_FLOODING_DIS_UNKNOWN_MC:  
       return "unknown mcast discard";   
     case CTC_DKIT_SUB_EPE_UC_MC_FLOODING_DIS_KNOWN_MC:  
       return "known mcast discard";   
     case CTC_DKIT_SUB_EPE_UC_MC_FLOODING_DIS_BC:  
       return "bcast discard";   
     case CTC_DKIT_SUB_EPE_TTL_FAIL_ROUTE_MCAST:  
       return "new TTL is too little after decrease 1 when do route operation, new TTL and threshold are ";   
     case CTC_DKIT_SUB_EPE_TTL_FAIL_ROUTE_TTL_0:  
       return "new TTL is 0 after decrease 1 when do route operation";   
     case CTC_DKIT_SUB_EPE_TTL_FAIL_MPLS:  
       return "new TTL is 0 after decrease 1 when do MPLS operation";   
     case CTC_DKIT_SUB_EPE_TTL_FAIL_TRILL:  
       return "new TTL is 0 after decrease 1 when do TRILL operation";   
     case CTC_DKIT_SUB_EPE_NAT_PT_ICMP_ERR_IPV4:  
       return "IPv4 ICMP, l4 source port is 0x03XX or 0x04XX or 0x11XX or 0x12XX, redirect to CPU";   
     case CTC_DKIT_SUB_EPE_NAT_PT_ICMP_ERR_IPV6:  
       return "IPv6 ICMP, l4 source port is 0, redirect to CPU";   
     case CTC_DKIT_SUB_EPE_OAM_FILTERING_DIS_EDGE:  
       return "up MEP send ether OAM packet to egde port discard";   
     case CTC_DKIT_SUB_EPE_OAM_FILTERING_DIS_LINK:  
       return "link OAM can not forward";   
     case CTC_DKIT_SUB_EPE_OAM_FILTERING_DIS_VLAN:  
       return "ether OAM packet discard by vlan filtering, dsvlan index=";   
     case CTC_DKIT_SUB_EPE_OAM_FILTERING_DIS_STP:  
       return "ether OAM packet discard by STP filtering";   
     case CTC_DKIT_SUB_EPE_OAM_FILTERING_DIS_PORT:  
       return "ether OAM packet discard by port transmit disable";   
     case CTC_DKIT_SUB_EPE_SAME_IPDA_IPSA_DIS_MAC:  
       return "same MAC discard";   
     case CTC_DKIT_SUB_EPE_SAME_IPDA_IPSA_DIS_IP:  
       return "same IP discard";   
     case CTC_DKIT_SUB_EPE_DEST_VLAN_PTR_DIS_1FFE:  
       return "invalid dest vlan index 0x1FFE";   
     case CTC_DKIT_SUB_EPE_DEST_VLAN_PTR_DIS_1FFD:  
       return "invalid dest vlan index 0x1FFD";   
     case CTC_DKIT_SUB_OAM_EXCE_NON:  
       return "None";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_INVALID:  
       return "ETH OAM (Y1731)PDU invalid /CSF interval mismatch /MD level lower than passive MEP MD level to CPU/MIP received non-process oam PDU/ Configure error";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_LB:  
       return "Send equal LBM/LBR to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_LT:  
       return "Send equal LTM/LTR to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_LM:  
       return "Send equal LMM/LmR to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_DM:  
       return "ETH_1DM/ETH_DMM/ETH_DMR to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_TST:  
       return "ETH TST to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_APS:  
       return "APS PDU to CPU(EthAPS & RAPS) ";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_SCC:  
       return "ETH_SCC to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_MCC:  
       return "ETH_MCC to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_CSF:  
       return "ETH_CSF to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_BIG_CCM:  
       return "Big ccm to CPU/mep cfg in sw ";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_LEARN_CCM:  
       return "Learning CCM to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_DEFECT:  
       return "someRDIdefect/someMACstatusDefect/lowCcm/Maid mismatch/ UnExcepect MEP-Rmep MisMatch (P2P mode)/CCM sequence number error /Source MAC mismatch/Unexp Interval-CCM interval mismatch to CPU/PBT OAM PresentTraffic Mismatch";   
     case CTC_DKIT_SUB_OAM_EXCE_PBX_OAM:  
       return "PBB/PBT OAM ";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_HIGH_VER:  
       return "High version oam send to cpu ";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_TLV:  
       return "Ether CCM TLV Option ";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_OTHER:  
       return "Other ETH OAM PDU to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_BFD_INVALID:  
       return "BFD PDU invalid ";   
     case CTC_DKIT_SUB_OAM_EXCE_BFD_LEARN:  
       return "Learning BFD send to CPU (ParserResult.YourDiscr == 0 ) ";   
     case CTC_DKIT_SUB_OAM_EXCE_BIG_BFD:  
       return "Big bfd to CPU/mep cfg in sw ";   
     case CTC_DKIT_SUB_OAM_EXCE_BFD_TIMER_NEG:  
       return "BFD timer Negotiation packet to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_TP_SCC:  
       return "MPLS TP SCC to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_TP_MCC:  
       return "MPLS TP MCC to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_TP_CSF:  
       return "MPLS TP CSF to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_TP_LB:  
       return "MPLS TP LB to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_TP_DLM:  
       return "MPLS TP DLM to cpu ";   
     case CTC_DKIT_SUB_OAM_EXCE_TP_DM:  
       return "DM/DLMDM to cpu ";   
     case CTC_DKIT_SUB_OAM_EXCE_TP_FM:  
       return "MPLS TP FM to CPU ";   
     case CTC_DKIT_SUB_OAM_EXCE_BFD_OTHER:  
       return "Other BFD OAM PDU to CPU";   
     case CTC_DKIT_SUB_OAM_EXCE_BFD_DISC_MISMATCH:  
       return "BFD discriminator mismatch";   
     case CTC_DKIT_SUB_OAM_EXCE_ETH_SM:  
       return "Send equal SLM/SLR to CPU";   
     case CTC_DKIT_SUB_OAM_EXCE_TP_CV:  
       return "MPLS TP CV to CPU";   
      default:
      sal_sprintf(reason_desc,"Sub reason id:%d",reason_id);
      return reason_desc;
    }

}

