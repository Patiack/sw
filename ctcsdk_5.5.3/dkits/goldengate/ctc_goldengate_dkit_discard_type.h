#ifndef _CTC_DKIT_GOLDENGATE_DISCARD_TYPE_H_
#define _CTC_DKIT_GOLDENGATE_DISCARD_TYPE_H_


#ifdef __cplusplus
extern "C" {
#endif


#include "sal.h"

enum ctc_dkit_discard_reason_id_e
{
    CTC_DKIT_DISCARD_INVALIED                   ,/**< CTC_DKIT_DISCARD_INVALIED, no discard*/
    /*IPE*/
    CTC_DKIT_IPE_USERID_BINDING_DIS             ,/**< CTC_DKIT_IPE_USERID_BINDING_DIS, the data in packet is not as SCL config, such as MAC DA, port, vlan, IP SA*/
    CTC_DKIT_IPE_LPBK_HDR_ADJ_RM_ERR0           ,/**< CTC_DKIT_IPE_LPBK_HDR_ADJ_RM_ERR0, length error at loopback header adjust, process offset>pktlen*/
    CTC_DKIT_IPE_PARSER_LEN_ERR                 ,/**< CTC_DKIT_IPE_PARSER_LEN_ERR, length error when do parser*/
    CTC_DKIT_IPE_LPBK_HDR_ADJ_RM_ERR1           ,/**< CTC_DKIT_IPE_LPBK_HDR_ADJ_RM_ERR1, length error at loopback header adjust, remove bytes>pktlen*/
    CTC_DKIT_IPE_EXCEP2_DIS                     ,/**< CTC_DKIT_IPE_EXCEP2_DIS, l2 PDU discard*/
    CTC_DKIT_IPE_DS_USERID_DIS                  ,/**< CTC_DKIT_IPE_DS_USERID_DIS, next hop index from SCL is invalid*/
    CTC_DKIT_IPE_RECEIVE_DIS                    ,/**< CTC_DKIT_IPE_RECEIVE_DIS, port/vlan receive disable*/
    CTC_DKIT_IPE_PBB_CHK_DIS                    ,/**< CTC_DKIT_IPE_PBB_CHK_DIS, ITag check failure, invalid bmac, or loop MAC SA of PIP port type in PBB*/
    CTC_DKIT_IPE_PROTOCOL_VLAN_DIS              ,/**< CTC_DKIT_IPE_PROTOCOL_VLAN_DIS, protocol vlan is config as discard*/
    CTC_DKIT_IPE_AFT_DIS                        ,/**< CTC_DKIT_IPE_AFT_DIS, AFT discard*/
    CTC_DKIT_IPE_ILLEGAL_PKT_DIS                ,/**< CTC_DKIT_IPE_ILLEGAL_PKT_DIS, mcast MAC SA discard /same MAC or IP DA SA discard*/
    CTC_DKIT_IPE_STP_DIS                        ,/**< CTC_DKIT_IPE_STP_DIS, STP discard*/
    CTC_DKIT_IPE_TRILL_ESADI_LPBK_DIS           ,/**< CTC_DKIT_IPE_TRILL_ESADI_LPBK_DIS, ESADI packet is disable on the port*/
    CTC_DKIT_IPE_PBB_OAM_DIS                    ,/**< CTC_DKIT_IPE_PBB_OAM_DIS, PBB OAM discard*/
    CTC_DKIT_IPE_ARP_DHCP_DIS                   ,/**< CTC_DKIT_IPE_ARP_DHCP_DIS, ARP/DHCP discard*/
    CTC_DKIT_IPE_DS_PHYPORT_SRCDIS              ,/**< CTC_DKIT_IPE_DS_PHYPORT_SRCDIS, ingress phyport discard*/
    CTC_DKIT_IPE_VLAN_FILTER_DIS                ,/**< CTC_DKIT_IPE_VLAN_FILTER_DIS, ingress vlan filtering discard*/
    CTC_DKIT_IPE_DS_ACL_DIS                     ,/**< CTC_DKIT_IPE_DS_ACL_DIS, discard by ACL deny or GRE flag is mismatch to config*/
    CTC_DKIT_IPE_ROUTE_MC_IPADD_DIS             ,/**< CTC_DKIT_IPE_ROUTE_MC_IPADD_DIS, routing multicast IP address check discard or IP Protocal Translation discard*/
    CTC_DKIT_IPE_SECURITY_CHK_DIS               ,/**< CTC_DKIT_IPE_SECURITY_CHK_DIS, bridge dest port is equal to source port discard or unknown Mcast/Ucast or Bcast discard*/
    CTC_DKIT_IPE_STORM_CTL_DIS                  ,/**< CTC_DKIT_IPE_STORM_CTL_DIS, port or vlan based storm Control discard*/
    CTC_DKIT_IPE_LEARNING_DIS                   ,/**< CTC_DKIT_IPE_LEARNING_DIS, the MACSA config as discard /source port mismatch /port security discard*/
    CTC_DKIT_IPE_POLICING_DIS                   ,/**< CTC_DKIT_IPE_POLICING_DIS, policing discard*/
    CTC_DKIT_IPE_NO_FORWARDING_PTR              ,/**< CTC_DKIT_IPE_NO_FORWARDING_PTR, not find next hop, no dsFwdPtr*/
    CTC_DKIT_IPE_IS_DIS_FORWARDING_PTR          ,/**< CTC_DKIT_IPE_IS_DIS_FORWARDING_PTR, next hop index is invalid, dsFwdPtr=0xFFFF*/
    CTC_DKIT_IPE_FATAL_EXCEP_DIS                ,/**< CTC_DKIT_IPE_FATAL_EXCEP_DIS, some fatal event occured*/
    CTC_DKIT_IPE_APS_DIS                        ,/**< CTC_DKIT_IPE_APS_DIS, APS selector discard*/
    CTC_DKIT_IPE_DS_FWD_DESTID_DIS              ,/**< CTC_DKIT_IPE_DS_FWD_DESTID_DIS, next hop is invalid, nextHopPtr=0x3FFFF*/
    CTC_DKIT_IPE_IP_HEADER_ERROR_DIS            ,/**< CTC_DKIT_IPE_IP_HEADER_ERROR_DIS, IP version mismatch /IP header length error /check sum is wrong /ICMP fragment packet /mismatch GRE version*/
    CTC_DKIT_IPE_VXLAN_FLAG_CHK_DIS             ,/**< CTC_DKIT_IPE_VXLAN_FLAG_CHK_DIS, VXLAN flag error*/
    CTC_DKIT_IPE_GENEVE_PAKCET_DISCARD          ,/**< CTC_DKIT_IPE_GENEVE_PAKCET_DISCARD, VXLAN packet is OAM frame or has option, redirect to CPU*/
    CTC_DKIT_IPE_TRILL_OAM_DIS                  ,/**< CTC_DKIT_IPE_TRILL_OAM_DIS, TRILL OAM packet discard, redirect to CPU*/
    CTC_DKIT_IPE_LOOPBACK_DIS                   ,/**< CTC_DKIT_IPE_LOOPBACK_DIS, loopback discard when from Fabric is disable*/
    CTC_DKIT_IPE_EFM_DIS                        ,/**< CTC_DKIT_IPE_EFM_DIS, EFM OAM packet discard, redirect to CPU*/
    CTC_DKIT_IPE_MPLSMC_CHECK_DIS               ,/**< CTC_DKIT_IPE_MPLSMC_CHECK_DIS, MPLS Mcast MAC DA check fail*/
    CTC_DKIT_IPE_STACKING_HDR_CHK_ERR           ,/**< CTC_DKIT_IPE_STACKING_HDR_CHK_ERR, stacking network header length or MAC DA check error*/
    CTC_DKIT_IPE_TRILL_FILTER_ERR               ,/**< CTC_DKIT_IPE_TRILL_FILTER_ERR, TRILL filter discard / invalid TRILL MAC DA on edge port*/
    CTC_DKIT_IPE_ENTROPY_IS_RESERVED_LABEL      ,/**< CTC_DKIT_IPE_ENTROPY_IS_RESERVED_LABEL, entropyLabel is 0*/
    CTC_DKIT_IPE_L3_EXCEP_DIS                   ,/**< CTC_DKIT_IPE_L3_EXCEP_DIS, VXLAN/NVGRE fraginfo should be 0 /ACH type is wrong for OAM or CW is not 0 /the IP is config to CPU*/
    CTC_DKIT_IPE_L2_EXCEP_DIS                   ,/**< CTC_DKIT_IPE_L2_EXCEP_DIS, L2 PDU discard*/
    CTC_DKIT_IPE_TRILL_MC_ADD_CHK_DIS           ,/**< CTC_DKIT_IPE_TRILL_MC_ADD_CHK_DIS, TRILL Mcast address is not 0x0180C2000040*/
    CTC_DKIT_IPE_TRILL_VERSION_CHK_DIS          ,/**< CTC_DKIT_IPE_TRILL_VERSION_CHK_DIS, TRILL version check error /inner VLAN ID check discard*/
    CTC_DKIT_IPE_PTP_EXCEP_DIS                  ,/**< CTC_DKIT_IPE_PTP_EXCEP_DIS, PTP packet in MPLS tunnel discard /version check fail /config to discard*/
    CTC_DKIT_IPE_OAM_NOT_FIND_DIS               ,/**< CTC_DKIT_IPE_OAM_NOT_FIND_DIS, OAM lookup hash conflict or not find MEP/MIP at ether OAM edge port*/
    CTC_DKIT_IPE_OAM_FILTER_DIS                 ,/**< CTC_DKIT_IPE_OAM_FILTER_DIS, OAM packet STP/VLAN filter when no MEP/MIP*/
    CTC_DKIT_IPE_BFD_OAM_TTL_CHK_DIS            ,/**< CTC_DKIT_IPE_BFD_OAM_TTL_CHK_DIS, BFD IPDA or TTL check error discard*/
    CTC_DKIT_IPE_ICMP_REDIRECT_DIS              ,/**< CTC_DKIT_IPE_ICMP_REDIRECT_DIS, ICMP discard, redirect to CPU*/
    CTC_DKIT_IPE_NO_MEP_MIP_DIS                 ,/**< CTC_DKIT_IPE_NO_MEP_MIP_DIS, no MEP/MIP found for the OAM packet, redirect to CPU*/
    CTC_DKIT_IPE_OAM_DISABLE_DIS                ,/**< CTC_DKIT_IPE_OAM_DISABLE_DIS, TP OAM config to disacrd by label*/
    CTC_DKIT_IPE_PBB_DECAP_DIS                  ,/**< CTC_DKIT_IPE_PBB_DECAP_DIS, PBB in MPLS tunnel decap discard*/
    CTC_DKIT_IPE_MPLS_TMPLS_OAM_DIS             ,/**< CTC_DKIT_IPE_MPLS_TMPLS_OAM_DIS, TMPLS OAM, redirect to CPU*/
    CTC_DKIT_IPE_MPLSTP_MCC_SCC_DIS             ,/**< CTC_DKIT_IPE_MPLSTP_MCC_SCC_DIS, TP MCC or SCC packet, redirect to CPU*/
    CTC_DKIT_IPE_ICMP_ERR_MSG_DIS               ,/**< CTC_DKIT_IPE_ICMP_ERR_MSG_DIS, ICMP error message, redirect to CPU*/
    CTC_DKIT_IPE_ETH_SERV_OAM_DIS               ,/**< CTC_DKIT_IPE_ETH_SERV_OAM_DIS, untagged service OAM, redirect to CPU*/
    CTC_DKIT_IPE_ETH_LINK_OAM_DIS               ,/**< CTC_DKIT_IPE_ETH_LINK_OAM_DIS, tagged link OAM, redirect to CPU*/
    CTC_DKIT_IPE_TUNNEL_DECAP_MARTIAN_ADD       ,/**< CTC_DKIT_IPE_TUNNEL_DECAP_MARTIAN_ADD, outer header martian address discard for tunnel packet*/
    CTC_DKIT_IPE_VXLAN_NVGRE_CHK_FAIL           ,/**< CTC_DKIT_IPE_VXLAN_NVGRE_CHK_FAIL, VXLAN or NVGRE check fail according to the tunnel config*/
    CTC_DKIT_IPE_USERID_FWD_PTR_DIS             ,/**< CTC_DKIT_IPE_USERID_FWD_PTR_DIS, next hop index from SCL for tunnel is invalid*/
    CTC_DKIT_IPE_QUE_DROP_SPAN_DISCARD          ,/**< CTC_DKIT_IPE_QUE_DROP_SPAN_DISCARD, dropped packet at queuing do SPAN*/
    CTC_DKIT_IPE_TRILL_RPF_CHK_DIS              ,/**< CTC_DKIT_IPE_TRILL_RPF_CHK_DIS, TRILL RPF check fail discard*/
    CTC_DKIT_IPE_MUX_PORT_ERR                   ,/**< CTC_DKIT_IPE_MUX_PORT_ERR, mux port error*/
    CTC_DKIT_IPE_PORT_MAC_CHECK_DIS             ,/**< CTC_DKIT_IPE_PORT_MAC_CHECK_DIS, MAC DA or SA is port MAC*/
    CTC_DKIT_IPE_VXLAN_NVGRE_INNER_VTAG_CHK_DIS ,/**< CTC_DKIT_IPE_VXLAN_NVGRE_INNER_VTAG_CHK_DIS, VXLAN or NVGRE packet with inner VTAG discard*/
    CTC_DKIT_IPE_RESERVED2                      ,/**< CTC_DKIT_IPE_RESERVED2, RESERVED*/
    CTC_DKIT_IPE_HW_HAR_ADJ                     ,/**< CTC_DKIT_IPE_HW_HAR_ADJ, some hardware error occurred at header adjust*/
    CTC_DKIT_IPE_HW_INT_MAP                     ,/**< CTC_DKIT_IPE_HW_INT_MAP, some hardware error occurred at interface mapper*/
    CTC_DKIT_IPE_HW_LKUP_QOS                    ,/**< CTC_DKIT_IPE_HW_LKUP_QOS, some hardware error occurred when do ACL&QOS lookup */
    CTC_DKIT_IPE_HW_LKUP_KEY_GEN                ,/**< CTC_DKIT_IPE_HW_LKUP_KEY_GEN, some hardware error occurred when generate lookup key*/
    CTC_DKIT_IPE_HW_PKT_PROC                    ,/**< CTC_DKIT_IPE_HW_PKT_PROC, some hardware error occurred when do packet process*/
    CTC_DKIT_IPE_HW_PKT_FWD                     ,/**< CTC_DKIT_IPE_HW_PKT_FWD, some hardware error occurred at forward*/
    CTC_DKIT_IPE_MAX,/**< */
    /*EPE*/
    CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS             ,/**< CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS, discard by drop channel*/
    CTC_DKIT_EPE_HDR_ADJ_PKT_ERR                ,/**< CTC_DKIT_EPE_HDR_ADJ_PKT_ERR, header adjust error(no use)*/
    CTC_DKIT_EPE_HDR_ADJ_REMOVE_ERR             ,/**< CTC_DKIT_EPE_HDR_ADJ_REMOVE_ERR, remove bytes error at header adjust, offset>144B*/
    CTC_DKIT_EPE_DS_DESTPHYPORT_DSTID_DIS       ,/**< CTC_DKIT_EPE_DS_DESTPHYPORT_DSTID_DIS, egress port discard*/
    CTC_DKIT_EPE_PORT_ISO_DIS                   ,/**< CTC_DKIT_EPE_PORT_ISO_DIS, port isolate or Pvlan discard*/
    CTC_DKIT_EPE_DS_VLAN_TRANSMIT_DIS           ,/**< CTC_DKIT_EPE_DS_VLAN_TRANSMIT_DIS, port/vlan transmit disable*/
    CTC_DKIT_EPE_BRG_TO_SAME_PORT_DIS           ,/**< CTC_DKIT_EPE_BRG_TO_SAME_PORT_DIS, bridge to same port(no use)*/
    CTC_DKIT_EPE_VPLS_HORIZON_SPLIT_DIS         ,/**< CTC_DKIT_EPE_VPLS_HORIZON_SPLIT_DIS, VPLS split horizon split or E-tree discard*/
    CTC_DKIT_EPE_VLAN_FILTER_DIS                ,/**< CTC_DKIT_EPE_VLAN_FILTER_DIS, egress vlan filtering discard*/
    CTC_DKIT_EPE_STP_DIS                        ,/**< CTC_DKIT_EPE_STP_DIS, STP discard*/
    CTC_DKIT_EPE_PARSER_LEN_ERR                 ,/**< CTC_DKIT_EPE_PARSER_LEN_ERR, parser length error*/
    CTC_DKIT_EPE_PBB_CHK_DIS                    ,/**< CTC_DKIT_EPE_PBB_CHK_DIS, PBB check discard*/
    CTC_DKIT_EPE_UC_MC_FLOODING_DIS             ,/**< CTC_DKIT_EPE_UC_MC_FLOODING_DIS, unkown-unicast/kown-unicast/unkown-mcast/kown-mcast/broadcast discard*/
    CTC_DKIT_EPE_OAM_802_3_DIS                  ,/**< CTC_DKIT_EPE_OAM_802_3_DIS, discard non-EFM OAM packet*/
    CTC_DKIT_EPE_TTL_FAIL                       ,/**< CTC_DKIT_EPE_TTL_FAIL, TTL check failed*/
    CTC_DKIT_EPE_REMOTE_MIRROR_ESCAP_DIS        ,/**< CTC_DKIT_EPE_REMOTE_MIRROR_ESCAP_DIS, remote mirror filtering discard*/
    CTC_DKIT_EPE_TUNNEL_MTU_CHK_DIS             ,/**< CTC_DKIT_EPE_TUNNEL_MTU_CHK_DIS, tunnel MTU check discard*/
    CTC_DKIT_EPE_INTERF_MTU_CHK_DIS             ,/**< CTC_DKIT_EPE_INTERF_MTU_CHK_DIS, interface MTU check discard*/
    CTC_DKIT_EPE_LOGIC_PORT_CHK_DIS             ,/**< CTC_DKIT_EPE_LOGIC_PORT_CHK_DIS, logic source port is equal to dest port*/
    CTC_DKIT_EPE_DS_ACL_DIS                     ,/**< CTC_DKIT_EPE_DS_ACL_DIS, discard by ACL deny*/
    CTC_DKIT_EPE_DS_VLAN_XLATE_DIS              ,/**< CTC_DKIT_EPE_DS_VLAN_XLATE_DIS, discard by SCL deny*/
    CTC_DKIT_EPE_POLICING_DIS                   ,/**< CTC_DKIT_EPE_POLICING_DIS, policing discard*/
    CTC_DKIT_EPE_CRC_ERR_DIS                    ,/**< CTC_DKIT_EPE_CRC_ERR_DIS, CRC check error(no use)*/
    CTC_DKIT_EPE_ROUTE_PLD_OP_DIS               ,/**< CTC_DKIT_EPE_ROUTE_PLD_OP_DIS, route payload operation no IP packet discard*/
    CTC_DKIT_EPE_BRIDGE_PLD_OP_DIS              ,/**< CTC_DKIT_EPE_BRIDGE_PLD_OP_DIS, bridge payload operation bridge disable*/
    CTC_DKIT_EPE_PT_L4_OFFSET_LARGE             ,/**< CTC_DKIT_EPE_PT_L4_OFFSET_LARGE, packet edit strip too large*/
    CTC_DKIT_EPE_BFD_DIS                        ,/**< CTC_DKIT_EPE_BFD_DIS, BFD discard(no use)*/
    CTC_DKIT_EPE_PORT_REFLECTIVE_CHK_DIS        ,/**< CTC_DKIT_EPE_PORT_REFLECTIVE_CHK_DIS, dest port is equal to source port*/
    CTC_DKIT_EPE_IP_MPLS_TTL_CHK_ERR            ,/**< CTC_DKIT_EPE_IP_MPLS_TTL_CHK_ERR, IP/MPLS TTL check error when do L3 edit*/
    CTC_DKIT_EPE_OAM_EDGE_PORT_DIS              ,/**< CTC_DKIT_EPE_OAM_EDGE_PORT_DIS, not find MEP/MIP at edge port for OAM packet*/
    CTC_DKIT_EPE_NAT_PT_ICMP_ERR                ,/**< CTC_DKIT_EPE_NAT_PT_ICMP_ERR, NAT/PT ICMP error*/
    CTC_DKIT_EPE_LATENCY_DISCARD                ,/**< CTC_DKIT_EPE_LATENCY_DISCARD, latency is too long*/
    CTC_DKIT_EPE_LOCAL_OAM_DIS                  ,/**< CTC_DKIT_EPE_LOCAL_OAM_DIS, local OAM discard(no use)*/
    CTC_DKIT_EPE_OAM_FILTERING_DIS              ,/**< CTC_DKIT_EPE_OAM_FILTERING_DIS, OAM packet Port/STP/VLAN filter discard or up MEP send OAM packet to edge port or forward link OAM packet*/
    CTC_DKIT_EPE_OAM_HASH_CONFILICT_DIS         ,/**< CTC_DKIT_EPE_OAM_HASH_CONFILICT_DIS, OAM hash confilict*/
    CTC_DKIT_EPE_SAME_IPDA_IPSA_DIS             ,/**< CTC_DKIT_EPE_SAME_IPDA_IPSA_DIS, same MAC DA SA or IP DA SA*/
    CTC_DKIT_EPE_HARD_ERROR_DIS                 ,/**< CTC_DKIT_EPE_HARD_ERROR_DIS, hardware error*/
    CTC_DKIT_EPE_TRILL_PLD_OP_DIS               ,/**< CTC_DKIT_EPE_TRILL_PLD_OP_DIS, TRILL payload operation discard(no use)*/
    CTC_DKIT_EPE_PBB_CHK_FAIL_DIS               ,/**< CTC_DKIT_EPE_PBB_CHK_FAIL_DIS, PBB check fail*/
    CTC_DKIT_EPE_DS_NEXTHOP_DATA_VIOLATE        ,/**< CTC_DKIT_EPE_DS_NEXTHOP_DATA_VIOLATE, DsNextHop data violate*/
    CTC_DKIT_EPE_DEST_VLAN_PTR_DIS              ,/**< CTC_DKIT_EPE_DEST_VLAN_PTR_DIS, dest vlan index is invalid*/
    CTC_DKIT_EPE_DS_L3EDIT_DATA_VIOLATE1        ,/**< CTC_DKIT_EPE_DS_L3EDIT_DATA_VIOLATE1, discard by L3 edit or inner L2 edit config*/
    CTC_DKIT_EPE_DS_L3EDIT_DATA_VIOLATE2        ,/**< CTC_DKIT_EPE_DS_L3EDIT_DATA_VIOLATE2, discard by L3 edit or inner L2 edit violate*/
    CTC_DKIT_EPE_DS_L3EDITNAT_DATA_VIOLATE      ,/**< CTC_DKIT_EPE_DS_L3EDITNAT_DATA_VIOLATE, L3 edit violate(no use)*/
    CTC_DKIT_EPE_DS_L2EDIT_DATA_VIOLATE1        ,/**< CTC_DKIT_EPE_DS_L2EDIT_DATA_VIOLATE1, discard by L2 edit config*/
    CTC_DKIT_EPE_DS_L2EDIT_DATA_VIOLATE2        ,/**< CTC_DKIT_EPE_DS_L2EDIT_DATA_VIOLATE2, discard by L2 edit violate*/
    CTC_DKIT_EPE_PKT_HDR_C2C_TTL_ZERO           ,/**< CTC_DKIT_EPE_PKT_HDR_C2C_TTL_ZERO, packetHeader C2C TTL zero(no use)*/
    CTC_DKIT_EPE_PT_UDP_CHKSUM_ZERO             ,/**< CTC_DKIT_EPE_PT_UDP_CHKSUM_ZERO, PT/UDP checksum is zero for NAT*/
    CTC_DKIT_EPE_OAM_TO_LOCAL_DIS               ,/**< CTC_DKIT_EPE_OAM_TO_LOCAL_DIS, send to local up MEP*/

    CTC_DKIT_EPE_HDR_ADJ_RX_DEST_ID,                    /**< CTC_DKIT_EPE_HDR_ADJ_RX_DEST_ID, BufRetrv packets with DestId discard*/
    CTC_DKIT_EPE_HDR_ADJ_MIN_PKT_LEN,                   /**< CTC_DKIT_EPE_HDR_ADJ_MIN_PKT_LEN, packets length in BufRetrv is smaller than the minimum required value*/
    CTC_DKIT_EPE_HDR_ADJ_TX_PI_HARD,                    /**< CTC_DKIT_EPE_HDR_ADJ_TX_PI_HARD, EpeHdrAdj packets with harderror*/
    CTC_DKIT_EPE_HDR_ADJ_TX_PI_DISCARD,                 /**< CTC_DKIT_EPE_HDR_ADJ_TX_PI_DISCARD, EpeHdrAdj packets with pi-discard*/
    CTC_DKIT_EPE_HDR_ADJ_RCV_PKT_ERROR,                 /**< CTC_DKIT_EPE_HDR_ADJ_RCV_PKT_ERROR, BufRetrv packets with pktError*/
    CTC_DKIT_EPE_HDR_EDIT_COMPLETE_DISCARD,             /**< CTC_DKIT_EPE_HDR_EDIT_COMPLETE_DISCARD, packet discard at EpeHdrEdit*/
    CTC_DKIT_EPE_HDR_EDIT_TX_PI_DISCARD,                /**< CTC_DKIT_EPE_HDR_EDIT_TX_PI_DISCARD, EpeHdrEdit packets with pi-discard*/
    CTC_DKIT_EPE_HDR_EDIT_EPE_TX_DATA,                  /**< CTC_DKIT_EPE_HDR_EDIT_EPE_TX_DATA, EpeHdrEdit packets with data error*/
    CTC_DKIT_EPE_HDR_PROC_L2_EDIT,                      /**< CTC_DKIT_EPE_HDR_PROC_L2_EDIT, some hardware error occurred when do L2 edit*/
    CTC_DKIT_EPE_HDR_PROC_L3_EDIT,                      /**< CTC_DKIT_EPE_HDR_PROC_L3_EDIT, some hardware error occurred when do L3 edit*/
    CTC_DKIT_EPE_HDR_PROC_INNER_L2,                     /**< CTC_DKIT_EPE_HDR_PROC_INNER_L2, some hardware error occurred when do inner L2 edit*/
    CTC_DKIT_EPE_HDR_PROC_PAYLOAD,                      /**< CTC_DKIT_EPE_HDR_PROC_PAYLOAD, some hardware error occurred when do payload process*/
    CTC_DKIT_EPE_OTHER_HW_NEXT_HOP,                     /**< CTC_DKIT_EPE_OTHER_HW_NEXT_HOP, some hardware error occurred at nexthop mapper*/
    CTC_DKIT_EPE_OTHER_HW_ACL_OAM,                      /**< CTC_DKIT_EPE_OTHER_HW_ACL_OAM, some hardware error occurred when do OAM lookup*/
    CTC_DKIT_EPE_OTHER_HW_CLASS,                        /**< CTC_DKIT_EPE_OTHER_HW_CLASS, some hardware error occurred at classification*/
    CTC_DKIT_EPE_OTHER_HW_OAM,                          /**< CTC_DKIT_EPE_OTHER_HW_OAM, some hardware error occurred when do OAM process*/
    CTC_DKIT_EPE_MAX,/**< */
    /*BSR*/
    CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL,              /**< CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL, resource check fail discard, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_PKTERR_SILENT_SELF_ORIG,               /**< CTC_DKIT_BSR_PKTERR_SILENT_SELF_ORIG, fabric source chip discard, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_PKTERR_SILENT_HARD_DISCARD,            /**< CTC_DKIT_BSR_PKTERR_SILENT_HARD_DISCARD, exception dest id discard from IPE, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_PKTERR_SILENT_CHIP_MISMATCH,           /**< CTC_DKIT_BSR_PKTERR_SILENT_CHIP_MISMATCH, chip id mismatch discard, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_PKTERR_SILENT_NO_BUF,                  /**< CTC_DKIT_BSR_PKTERR_SILENT_NO_BUF, pkt buffer is full, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_PKTERR_SILENT_DATA_ERROR,              /**< CTC_DKIT_BSR_PKTERR_SILENT_DATA_ERROR, crc error or ecc error, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_PKTERR_ABORT_UNDER_LEN,                /**< CTC_DKIT_BSR_PKTERR_ABORT_UNDER_LEN, packet length is smaller than the minimum required value, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_PKTERR_ABORT_MET_FIFO,                 /**< CTC_DKIT_BSR_PKTERR_ABORT_MET_FIFO, packet discard due to the copy ability of MET, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_PKTERR_ABORT_NO_BUF,                   /**< CTC_DKIT_BSR_PKTERR_ABORT_NO_BUF, pkt buffer is full, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_PKTERR_ABORT_FRAME_ERROR,              /**< CTC_DKIT_BSR_PKTERR_ABORT_FRAME_ERROR, packet discard due to miss EOP, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_PKTERR_ABORT_OVER_LEN,                 /**< CTC_DKIT_BSR_PKTERR_ABORT_OVER_LEN, packet length is larger than the maximum required value, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_PKTERR_ABORT_DATA_ERROR,               /**< CTC_DKIT_BSR_PKTERR_ABORT_DATA_ERROR, crc error or ecc error, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_BUFSTORE_IPE_HDR_UCAST,                /**< CTC_DKIT_BSR_BUFSTORE_IPE_HDR_UCAST, ucast discard due to the copy ability of MET in cutthrough model, read BufStoreStallDropDebugStats to confirm*/
    CTC_DKIT_BSR_BUFSTORE_IPE_HDR_MCAST,                /**< CTC_DKIT_BSR_BUFSTORE_IPE_HDR_MCAST, mcast discard due to the copy ability of MET in cutthrough model, read BufStoreStallDropDebugStats to confirm*/
    CTC_DKIT_BSR_BUFSTORE_IPE_UCAST,                    /**< CTC_DKIT_BSR_BUFSTORE_IPE_UCAST, ucast discard in IPE interface due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm*/
    CTC_DKIT_BSR_BUFSTORE_IPE_MCAST,                    /**< CTC_DKIT_BSR_BUFSTORE_IPE_MCAST, mcast discard in IPE interface due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm*/
    CTC_DKIT_BSR_BUFSTORE_ELOOP_UCAST,                  /**< CTC_DKIT_BSR_BUFSTORE_ELOOP_UCAST, eloop ucast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm*/
    CTC_DKIT_BSR_BUFSTORE_ELOOP_MCAST,                  /**< CTC_DKIT_BSR_BUFSTORE_ELOOP_MCAST, eloop mcast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm*/
    CTC_DKIT_BSR_BUFSTORE_QCN_UCAST,                    /**< CTC_DKIT_BSR_BUFSTORE_QCN_UCAST, QCN ucast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm*/
    CTC_DKIT_BSR_BUFSTORE_QCN_MCAST,                    /**< CTC_DKIT_BSR_BUFSTORE_QCN_MCAST, QCN mcast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm*/
    CTC_DKIT_BSR_BUFSTORE_OAM_UCAST,                    /**< CTC_DKIT_BSR_BUFSTORE_OAM_UCAST, OAM ucast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm*/
    CTC_DKIT_BSR_BUFSTORE_OAM_MCAST,                    /**< CTC_DKIT_BSR_BUFSTORE_OAM_MCAST, OAM mcast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm*/
    CTC_DKIT_BSR_BUFSTORE_DMA_UCAST,                    /**< CTC_DKIT_BSR_BUFSTORE_DMA_UCAST, DMA ucast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm*/
    CTC_DKIT_BSR_BUFSTORE_DMA_MCAST,                    /**< CTC_DKIT_BSR_BUFSTORE_DMA_MCAST, DMA mcast discard due to the copy ability of MET, read BufStoreStallDropDebugStats to confirm*/
    CTC_DKIT_BSR_QMGRENQ_SPAN_ON_DROP,                  /**< CTC_DKIT_BSR_QMGRENQ_SPAN_ON_DROP, packet discard due to Egress Resource Check and spanning to specific queue, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_QMGRENQ_ENQ_DISCARD,                   /**< CTC_DKIT_BSR_QMGRENQ_ENQ_DISCARD, the mcastLinkAggregation discard or stacking discard or noLinkAggMem discard when do LinkAgg, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_QMGRENQ_FREE_USED,                     /**< CTC_DKIT_BSR_QMGRENQ_FREE_USED, packet discard due to the use up of Queue Entry, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_QMGRENQ_WRED_DISCARD,                  /**< CTC_DKIT_BSR_QMGRENQ_WRED_DISCARD, packet discard due to WRED, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_QMGRENQ_EGR_RESRC_MGR,                 /**< CTC_DKIT_BSR_QMGRENQ_EGR_RESRC_MGR, packet discard due to the satisfication of the Egress Resource Check, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_QMGRENQ_RESERVE_CHANNEL,               /**< CTC_DKIT_BSR_QMGRENQ_RESERVE_CHANNEL, packet discard due to the mapping to reserved channel, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_QMGRENQ_CRITICAL_PKT,                  /**< CTC_DKIT_BSR_QMGRENQ_CRITICAL_PKT, critical packets, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_QMGRENQ_C2C_PKT,                       /**< CTC_DKIT_BSR_QMGRENQ_C2C_PKT, C2C packets, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_QMGRENQ_NO_CHANNEL_LINKAGG,            /**< CTC_DKIT_BSR_QMGRENQ_NO_CHANNEL_LINKAGG, noLinkAggMem discard due to channel LinkAgg when do LinkAgg, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_QMGRENQ_NO_PORT_LINKAGG,               /**< CTC_DKIT_BSR_QMGRENQ_NO_PORT_LINKAGG, noLinkAggMem discard due to port LinkAgg when do LinkAgg, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_QMGRENQ_MCAST_LINKAGG,                 /**< CTC_DKIT_BSR_QMGRENQ_MCAST_LINKAGG, mcastLinkAggregation discard when do LinkAgg, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_QMGRENQ_STACKING_DISCARD,              /**< CTC_DKIT_BSR_QMGRENQ_STACKING_DISCARD, stacking discard when do LinkAgg, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_OTHER_HW_ENQ_DISCARD,                  /**< CTC_DKIT_BSR_OTHER_HW_ENQ_DISCARD, enqueue discard sending to QMgr by MetFifo, read MetFifoDebugStats to confirm*/
    CTC_DKIT_BSR_OTHER_HW_AGE_DISCARD,                  /**< CTC_DKIT_BSR_OTHER_HW_AGE_DISCARD, dequeue discard due to the aging of DsQueueEntry, read QMgrDeqSliceDebugStats to confirm*/
    CTC_DKIT_BSR_MAX,/**< */

    /*NETRX*/
    CTC_DKIT_NETRX_LEN_ERROR,                           /**< CTC_DKIT_NETRX_LEN_ERROR, packet discard due to the failure of max or min pkt len check, read NetRxDebugStatsTable to confirm*/
    CTC_DKIT_NETRX_FRAME_ERROR,                         /**< CTC_DKIT_NETRX_FRAME_ERROR, packet discard due to misssing sop or eop, read NetRxDebugStatsTable to confirm*/
    CTC_DKIT_NETRX_PKT_ERROR,                           /**< CTC_DKIT_NETRX_PKT_ERROR, packet discard due to error, read NetRxDebugStatsTable to confirm*/
    CTC_DKIT_NETRX_FULL_DROP,                           /**< CTC_DKIT_NETRX_FULL_DROP, full drop or earlyTerm discard due to the use up of buffer in port oversub model, read NetRxDebugStatsTable to confirm*/
    CTC_DKIT_NETRX_BPDU0_ERROR,                          /**< CTC_DKIT_NETRX_BPDU_ERROR0, BPDU discard, read NetRxDebugStats to confirm*/
    CTC_DKIT_NETRX_BPDU1_ERROR,                          /**< CTC_DKIT_NETRX_BPDU_ERROR1, BPDU discard, read NetRxDebugStats to confirm*/
    CTC_DKIT_NETRX_BPDU2_ERROR,                          /**< CTC_DKIT_NETRX_BPDU_ERROR2, BPDU discard, read NetRxDebugStats to confirm*/
    CTC_DKIT_NETRX_BPDU3_ERROR,                          /**< CTC_DKIT_NETRX_BPDU_ERROR3, BPDU discard, read NetRxDebugStats to confirm*/
    CTC_DKIT_NETRX_PAUSE0_ERROR,                         /**< CTC_DKIT_NETRX_PAUSE_ERROR0, PAUSE discard, read NetRxDebugStats to confirm*/
    CTC_DKIT_NETRX_PAUSE1_ERROR,                         /**< CTC_DKIT_NETRX_PAUSE_ERROR1, PAUSE discard, read NetRxDebugStats to confirm*/
    CTC_DKIT_NETRX_PAUSE2_ERROR,                         /**< CTC_DKIT_NETRX_PAUSE_ERROR2, PAUSE discard, read NetRxDebugStats to confirm*/
    CTC_DKIT_NETRX_PAUSE3_ERROR,                         /**< CTC_DKIT_NETRX_PAUSE_ERROR3, PAUSE discard, read NetRxDebugStats to confirm*/
    CTC_DKIT_NETRX_TX_ERROR,                            /**< CTC_DKIT_NETRX_TX_ERROR, the number of error packets sending to IPE*/
    CTC_DKIT_NETRX_MAX,/**< */

    /*NETTX*/
    CTC_DKIT_NETTX_EPE_NO_BUF,                          /**< CTC_DKIT_NETTX_EPE_NO_BUF, packet discard due to the use up of PktBuffer in NETTX*/
    CTC_DKIT_NETTX_EPE_NO_EOP,                          /**< CTC_DKIT_NETTX_EPE_NO_EOP, packet discard due to missing EOP*/
    CTC_DKIT_NETTX_EPE_NO_SOP,                          /**< CTC_DKIT_NETTX_EPE_NO_SOP, packet discard due to missing SOP*/
    CTC_DKIT_NETTX_INFO,                                /**< CTC_DKIT_NETTX_INFO, the packet which need to be discarded*/
    CTC_DKIT_NETTX_EPE_MIN_LEN,                         /**< CTC_DKIT_NETTX_EPE_MIN_LEN, packet discard due to smaller than the cfgMinPktLen*/
    CTC_DKIT_NETTX_SPAN_MIN_LEN,                        /**< CTC_DKIT_NETTX_SPAN_MIN_LEN, packet discard due to smaller than the cfgMinPktLen*/
    CTC_DKIT_NETTX_SPAN_NO_BUF,                         /**< CTC_DKIT_NETTX_SPAN_NO_BUF, packet discard due to the use up of PktBuffer in NETTX*/
    CTC_DKIT_NETTX_SPAN_NO_EOP,                         /**< CTC_DKIT_NETTX_SPAN_NO_EOP, packet discard due to missing EOP*/
    CTC_DKIT_NETTX_SPAN_NO_SOP,                         /**< CTC_DKIT_NETTX_SPAN_NO_SOP, packet discard due to missing SOP*/
    CTC_DKIT_NETTX_MAX,/**< */

    /*OAM*/
    CTC_DKIT_OAM_LINK_AGG_NO_MEMBER,                    /**< CTC_DKIT_OAM_LINK_AGG_NO_MEMBER, linkagg no member when tx CCM or reply LBR/LMR/DMR/SLR*/
    CTC_DKIT_OAM_ASIC_ERROR,                            /**< CTC_DKIT_OAM_ASIC_ERROR, some hardware error occured*/
    CTC_DKIT_OAM_EXCEPTION,                             /**< CTC_DKIT_OAM_EXCEPTION, some exception occured when process OAM packet*/
    CTC_DKIT_OAM_MAX,/**< */

    CTC_DKIT_DISCARD_MAX/**< */

};
typedef enum ctc_dkit_discard_reason_id_e ctc_dkit_discard_reason_id_t;


enum ctc_dkit_discard_sub_reason_id_e
{
    CTC_DKIT_SUB_DISCARD_INVALIED = 0x0,/**< no discard*/
    CTC_DKIT_SUB_DISCARD_AMBIGUOUS = 0x1,/**< the discard reason is ambiguous, try to use captured info*/
    CTC_DKIT_SUB_DISCARD_NEED_PARAM = 0x2,/**< please input filter param to get the detail reason*/

    /*IPE*/
    CTC_DKIT_SUB_IPE_PARSER_LEN_ERR_USERID,/**< parser length error at SCL*/
    CTC_DKIT_SUB_IPE_PARSER_LEN_ERR_ACL,/**< parser length error at ACL*/

    CTC_DKIT_SUB_IPE_EXCEP2_DIS_BPDU,/**< BPDU, MAC DA is 0x0180c2000000, redirect to CPU*/
    CTC_DKIT_SUB_IPE_EXCEP2_DIS_SLOW,/**< SLOW protocol, ether type is 0x8809, redirect to CPU*/
    CTC_DKIT_SUB_IPE_EXCEP2_DIS_EAPOL,/**< EAPOL, ether type is 0x888E, redirect to CPU*/
    CTC_DKIT_SUB_IPE_EXCEP2_DIS_LLDP,/**< LLDP, ether type is 0x88CC, redirect to CPU*/
    CTC_DKIT_SUB_IPE_EXCEP2_DIS_L2ISIS,/**< L2ISIS, ether type is 0x22F4, redirect to CPU*/
    CTC_DKIT_SUB_IPE_EXCEP2_DIS_CAM,/**< MAC DA match in cam, redirect to CPU*/

    CTC_DKIT_SUB_IPE_RECEIVE_DIS_PORT,/**< port receive is disable*/
    CTC_DKIT_SUB_IPE_RECEIVE_DIS_VLAN,/**< vlan receive is disable*/

    CTC_DKIT_SUB_IPE_AFT_DIS_ALLOW_ALL,/**< AFT is config as allow all packets*/
    CTC_DKIT_SUB_IPE_AFT_DIS_DROP_UNTAGGED,/**< AFT is config as drop all untagged*/
    CTC_DKIT_SUB_IPE_AFT_DIS_DROP_TAGGED,/**< AFT is config as drop all tagged*/
    CTC_DKIT_SUB_IPE_AFT_DIS_DROP_ALL,/**< AFT is config as drop all (same with receiveEn = 0)*/
    CTC_DKIT_SUB_IPE_AFT_DIS_DROP_WITHOUT_2_TAG,/**< AFT is config as drop packet without 2 vlan tag*/
    CTC_DKIT_SUB_IPE_AFT_DIS_DROP_WITH_2_TAG,/**< AFT is config as drop all packet with 2 vlan tag*/
    CTC_DKIT_SUB_IPE_AFT_DIS_DROP_STAG,/**< AFT is config as drop all S-VLAN tag*/
    CTC_DKIT_SUB_IPE_AFT_DIS_DROP_NO_STAG,/**< AFT is config as drop all non S-VLAN tag*/
    CTC_DKIT_SUB_IPE_AFT_DIS_DROP_ONLY_STAG,/**< AFT is config as drop only S-VLAN tag*/
    CTC_DKIT_SUB_IPE_AFT_DIS_PERMIT_ONLY_STAG,/**< AFT is config as permit only S-VLAN tag*/
    CTC_DKIT_SUB_IPE_AFT_DIS_DROP_CTAG,/**< AFT is config as drop all C-VLAN tag*/
    CTC_DKIT_SUB_IPE_AFT_DIS_DROP_NO_CTAG,/**< AFT is config as drop all non C-VLAN tag*/
    CTC_DKIT_SUB_IPE_AFT_DIS_DROP_ONLY_CTAG,/**< AFT is config as drop only C-VLAN tag*/
    CTC_DKIT_SUB_IPE_AFT_DIS_PERMIT_ONLY_CTAG,/**< AFT is config as permit only C-VLAN tag*/

    CTC_DKIT_SUB_IPE_ILLEGAL_PKT_DIS_MCAST,/**< Mcast MAC DA is config as discard*/
    CTC_DKIT_SUB_IPE_ILLEGAL_PKT_DIS_SAME_MAC,/**< the packet has the same MAC DA and SA*/

    CTC_DKIT_SUB_IPE_ARP_DHCP_DIS_V4_DSCP,/**< IPv4 DSCP packet discard*/
    CTC_DKIT_SUB_IPE_ARP_DHCP_DIS_V6_DSCP,/**< IPv6 DSCP packet discard*/
    CTC_DKIT_SUB_IPE_ARP_DHCP_DIS_ARP,/**< ARP packet discard*/
    CTC_DKIT_SUB_IPE_ARP_DHCP_DIS_FIP,/**< FIP packet discard*/

    CTC_DKIT_SUB_IPE_ROUTE_MC_IPADD_DIS_V4,/**< IPv4 mcast MAC DA and IP DA check fail*/
    CTC_DKIT_SUB_IPE_ROUTE_MC_IPADD_DIS_V6,/**< IPv6 mcast MAC DA and IP DA check fail*/
    CTC_DKIT_SUB_IPE_ROUTE_MC_IPADD_DIS_PROTOCOL,/**< IP Protocal Translation discard*/

    CTC_DKIT_SUB_IPE_SECURITY_CHK_DIS_PORT_MATCH,/**< port match discard*/
    CTC_DKIT_SUB_IPE_SECURITY_CHK_DIS_MCAST,/**< unknown mcast discard*/
    CTC_DKIT_SUB_IPE_SECURITY_CHK_DIS_UCAST,/**< unknown ucast discard*/
    CTC_DKIT_SUB_IPE_SECURITY_CHK_DIS_BCAST,/**< bcast discard*/

    CTC_DKIT_SUB_IPE_LEARNING_DIS_SRC_MAC,/**< the MAC SA FDB is config as discard*/
    CTC_DKIT_SUB_IPE_LEARNING_DIS_SRC_PORT,/**< the global source port is not config as it in the MAC SA FDB, they are */
    CTC_DKIT_SUB_IPE_LEARNING_LOGIC_DIS_SRC_PORT,/**< the logic source port is not config as it in the MAC SA FDB, they are */
    CTC_DKIT_SUB_IPE_LEARNING_DIS_PORT_SECURITY,/**< */
    CTC_DKIT_SUB_IPE_LEARNING_DIS_VSI_SECURITY,/**< */
    CTC_DKIT_SUB_IPE_LEARNING_DIS_SYSTEM_SECURITY,/**< */
    CTC_DKIT_SUB_IPE_LEARNING_DIS_PROFILE_MAX,/**< */

    CTC_DKIT_SUB_IPE_FATAL_UC_IP_HDR_ERROR_OR_MARTION_ADDR,/**< ucast IP version/header length/checksum error, redirect to CPU*/
    CTC_DKIT_SUB_IPE_FATAL_UC_IP_OPTIONS,/**< ucast IPv4 header length>5/ IPv6 protocol=0/ Trill option!=0, redirect to CPU*/
    CTC_DKIT_SUB_IPE_FATAL_UC_GRE_UNKNOWN_OPT_PROTOCAL,/**< ucast GRE protocol or option is unknown, redirect to CPU*/
    CTC_DKIT_SUB_IPE_FATAL_UC_ISATAP_SRC_ADDR_CHK_FAIL,/**< ucast ISATAP source address check fail, redirect to CPU*/
    CTC_DKIT_SUB_IPE_FATAL_UC_IPTTL_CHK_FAIL,/**< ucast TTL check fail, redirect to CPU*/
    CTC_DKIT_SUB_IPE_FATAL_UC_RPF_CHK_FAIL,/**< ucast RPF check fail, redirect to CPU*/
    CTC_DKIT_SUB_IPE_FATAL_UC_OR_MC_MORERPF,/**< ucast or mcast multi RPF check fail, redirect to CPU*/
    CTC_DKIT_SUB_IPE_FATAL_UC_LINKID_CHK_FAIL,/**< ucast linkid check fail, redirect to CPU(no use)*/
    CTC_DKIT_SUB_IPE_FATAL_MPLS_LABEL_OUTOFRANGE,/**< MPLS label out of range, redirect to CPU*/
    CTC_DKIT_SUB_IPE_FATAL_MPLS_SBIT_ERROR,/**< MPLS sbit check fail, redirect to CPU*/
    CTC_DKIT_SUB_IPE_FATAL_MPLS_TTL_CHK_FAIL,/**< MPLS TTL check fail, redirect to CPU*/
    CTC_DKIT_SUB_IPE_FATAL_FCOE_VIRTUAL_LINK_CHK_FAIL,/**< FCOE RPF or MAC SA check fail, redirect to CPU*/
    CTC_DKIT_SUB_IPE_FATAL_IGMP_SNOOPING_PKT,/**< IGMP snooping, redirect to CPU*/
    CTC_DKIT_SUB_IPE_FATAL_TRILL_OPTION,/**< TRILL header check error, redirect to CPU*/
    CTC_DKIT_SUB_IPE_FATAL_TRILL_TTL_CHK_FAIL,/**< TRILL TTL check fail, redirect to CPU*/
    CTC_DKIT_SUB_IPE_FATAL_VXLAN_OR_NVGRE_CHK_FAIL,/**< VXLAN or NVGRE check fail, redirect to CPU*/

    CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_IPV4_SAME_IP,/**< IPv4 packet has the same IP DA and SA*/
    CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_IPV6_SAME_IP,/**< IPv6 packet has the same IP DA and SA*/
    CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_TCP_L4_ERROR0,/**< TCP SYN=0*/
    CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_TCP_L4_ERROR1,/**< TCP flags=0 and sequence number=0 */
    CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_TCP_L4_ERROR2,/**< TCP SYN=0, PSH=0, URG=0, and sequence number=0*/
    CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_TCP_L4_ERROR3,/**< TCP FIN=0, SYN=0*/
    CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_TCP_L4_ERROR4,/**< TCP souce port is equal to dest port*/
    CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_UDP_L4_ERROR4,/**< TCP souce port is equal to dest port*/
    CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_IGMP_FRAG_ERROR,/**< IGMP fragment check error*/
    CTC_DKIT_SUB_IPE_IP_HEADER_ERROR_DIS_GRE_VERSION_ERROR,/**< GRE version check error*/

    CTC_DKIT_SUB_IPE_MPLSMC_CHECK_DIS_CONTEXT,/**< mcast MPLS with context label MAC DA check fail*/
    CTC_DKIT_SUB_IPE_MPLSMC_CHECK_DIS,/**< mcast MPLS without context label MAC DA check fail*/

    CTC_DKIT_SUB_IPE_TRILL_FILTER_ERR_TRILL_DIS,/**< TRILL packet discard by source port*/
    CTC_DKIT_SUB_IPE_TRILL_FILTER_ERR_NON_TRILL_DIS,/**< non-TRILL packet discard by source port*/
    CTC_DKIT_SUB_IPE_TRILL_FILTER_ERR_TRILL_UCAST,/**< ucast TRILL packet without port MAC DA discard*/

    CTC_DKIT_SUB_IPE_L3_EXCEP_DIS_FRAG,/**< VXLAN or NVGRE packet whose fraginfo should be 0, redirect to CPU*/
    CTC_DKIT_SUB_IPE_L3_EXCEP_DIS_ACH,/**< ACH type is wrong for ACH OAM or control word is not 0, redirect to CPU*/
    CTC_DKIT_SUB_IPE_L3_EXCEP_DIS_ROUTE,/**< the IP is config as redirect to CPU*/

    CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_VERSION,/**< TRILL packet version check fail, the version should be */
    CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_BFD_MCAST,/**< mcast TRILL packet discard*/
    CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_BFD_MULTIHOP,/**< multihop TRILL BFD packet discard, the TTL should >*/
    CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_BFD_1HOP,/**< 1hop TRILL BFD packet discard, the TTL should be 0x3F*/
    CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_INNER_VLAN_INVALID,/**< TRILL packet discard, the inner vlan id should not be 0xFFF or 0*/
    CTC_DKIT_SUB_IPE_TRILL_VERSION_CHK_DIS_INNER_VLAN_ABSENT,/**< TRILL packet without inner vlan discard*/

    CTC_DKIT_SUB_IPE_PTP_EXCEP_DIS_MESSAGE_DIS,/**< PTP message config as discard, message type is */
    CTC_DKIT_SUB_IPE_PTP_EXCEP_DIS_V1,/**< PTPv1 message discard, redirect to CPU*/
    CTC_DKIT_SUB_IPE_PTP_EXCEP_DIS_HIGH_VERSION,/**< PTP high version message discard, redirect to CPU*/
    CTC_DKIT_SUB_IPE_PTP_EXCEP_DIS_UNKNOWN,/**< unkonwn PTP message in MPLS tunnel discard*/

    CTC_DKIT_SUB_IPE_OAM_NOT_FIND_DIS_CONFLICT_ETHER,/**< ether OAM hash lookup conflict, key: globalSrcPort, vlanId, isFid are */
    CTC_DKIT_SUB_IPE_OAM_NOT_FIND_DIS_EDGE_PORT,/**< ether OAM packet discard on edge port, key: globalSrcPort, vlanId, isFid are */

    CTC_DKIT_SUB_IPE_OAM_FILTER_DIS_VLAN,/**< ether OAM packet discard by vlan filtering, dsvlan index=*/
    CTC_DKIT_SUB_IPE_OAM_FILTER_DIS_STP,/**< ether OAM packet discard by STP filtering*/

    CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_ETHER,/**< ether OAM not find MEP or MIP, key: globalSrcPort, vlanId, isFid are */
    CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_ACH_SECTION_PORT,/**< ACH port based section OAM not find MEP or MIP, key: localPhyPort is */
    CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_ACH_SECTION_INTF,/**< ACH interface based section OAM not find MEP or MIP, key: interfaceId is */
    CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_ACH,/**< ACH OAM not find MEP or MIP, key: mplsOamIndex is */
    CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_MPLS_BFD,/**< MPLS BFD not find MEP, key: bfdMyDiscriminator is */
    CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_IP_BFD,/**< IP BFD not find MEP, key: bfdMyDiscriminator is */
    CTC_DKIT_SUB_IPE_NO_MEP_MIP_DIS_TRILL_BFD,/**< TRILL BFD not find MEP*/

    CTC_DKIT_SUB_IPE_ICMP_ERR_MSG_DIS_IPV4,/**< IPv4 ICMP, l4 source port is 0x03XX or 0x04XX or 0x11XX or 0x12XX, redirect to CPU*/
    CTC_DKIT_SUB_IPE_ICMP_ERR_MSG_DIS_IPV6,/**< IPv6 ICMP, l4 source port is 0, redirect to CPU*/
    CTC_DKIT_SUB_IPE_ICMP_ERR_MSG_DIS_PT,/**< ICMP packet discard in PT scenario, redirect to CPU*/

    CTC_DKIT_SUB_IPE_VXLAN_NVGRE_CHK_FAIL_VXLAN,/**< the VxLAN tunnel is not exist on the Gateway device*/
    CTC_DKIT_SUB_IPE_VXLAN_NVGRE_CHK_FAIL_NVGRE,/**< the NvGRE tunnel is not exist on the Gateway device*/

    CTC_DKIT_SUB_IPE_VXLAN_NVGRE_INNER_VTAG_CHK_DIS_VXLAN,/**< VXLAN packet with inner VTAG discard*/
    CTC_DKIT_SUB_IPE_VXLAN_NVGRE_INNER_VTAG_CHK_DIS_NVGRE,/**< VXLAN packet with inner VTAG discard*/

    CTC_DKIT_SUB_IPE_PORT_MAC_CHECK_DIS_DA,/**< MAC DA is in port MAC range*/
    CTC_DKIT_SUB_IPE_PORT_MAC_CHECK_DIS_SA,/**< MAC SA is in port MAC range*/

    CTC_DKIT_SUB_IPE_FWD_PORT,/**< */
    CTC_DKIT_SUB_IPE_FWD_USERID,/**< */
    CTC_DKIT_SUB_IPE_FWD_TUNNEL,/**< the packet forward by tunnel*/
    CTC_DKIT_SUB_IPE_FWD_TUNNEL_ECMP,/**< the packet forward by tunnel, and then do ECMP, ECMP group id is */
    CTC_DKIT_SUB_IPE_FWD_TUNNEL_IP,/**< the packet forward by IP after tunnel process*/
    CTC_DKIT_SUB_IPE_FWD_TUNNEL_IP_ECMP,/**< the packet forward by IP after tunnel process, and then do ECMP, ECMP group id is */
    CTC_DKIT_SUB_IPE_FWD_TUNNEL_BRIDGE,/**< the packet forward by MAC after tunnel process*/
    CTC_DKIT_SUB_IPE_FWD_TUNNEL_BRIDGE_ECMP,/**< the packet forward by MAC after tunnel process, and then do ECMP, ECMP group id is */
    CTC_DKIT_SUB_IPE_FWD_MPLS,/**< the packet forward by mpls*/
    CTC_DKIT_SUB_IPE_FWD_MPLS_ECMP,/**< the packet forward by mpls, and the do ECMP, ECMP group id is*/
    CTC_DKIT_SUB_IPE_FWD_MPLS_IP,/**< the packet forward by IP after mpls process*/
    CTC_DKIT_SUB_IPE_FWD_MPLS_IP_ECMP,/**< the packet forward by IP after mpls process, and then do ECMP, ECMP group id is */
    CTC_DKIT_SUB_IPE_FWD_MPLS_BRIDGE,/**< the packet forward by MAC after mpls process*/
    CTC_DKIT_SUB_IPE_FWD_MPLS_BRIDGE_ECMP,/**< the packet forward by MAC after mpls process, and then do ECMP, ECMP group id is */
    CTC_DKIT_SUB_IPE_FWD_ACL,/**< */
    CTC_DKIT_SUB_IPE_FWD_ROUTE,/**< the packet forward by IP*/
    CTC_DKIT_SUB_IPE_FWD_ROUTE_ECMP,/**< the packet forward by IP, and then do ECMP, ECMP group id is */
    CTC_DKIT_SUB_IPE_FWD_ROUTE_BRIDGE,/**< the packet forward by MAC after IP process*/
    CTC_DKIT_SUB_IPE_FWD_ROUTE_BRIDGE_ECMP,/**< the packet forward by MAC after IP process, and then do ECMP, ECMP group id is */
    CTC_DKIT_SUB_IPE_FWD_FCOE,/**< the packet forward by FCOE*/
    CTC_DKIT_SUB_IPE_FWD_TRILL,/**< the packet forward by TRILL*/
    CTC_DKIT_SUB_IPE_FWD_BRIDGE,/**< the packet forward by MAC and hit a FDB entry*/
    CTC_DKIT_SUB_IPE_FWD_BRIDGE_ECMP,/**< the packet forward by MAC and hit a FDB entry, and then do ECMP, ECMP group id is */
    CTC_DKIT_SUB_IPE_FWD_BRIDGE_NOT_HIT,/**< the packet forward by MAC but not hit FDB entry*/
    CTC_DKIT_SUB_IPE_FWD_BRIDGE_NOT_HIT_ECMP,/**< the packet forward by MAC but use default FDB entry, and then do ECMP, ECMP group id is */

    /*EPE*/
    CTC_DKIT_SUB_EPE_PORT_ISO_DIS_PVLAN_P2X,/**< Pvlan isolation discard, from promiscuous port*/
    CTC_DKIT_SUB_EPE_PORT_ISO_DIS_PVLAN_C2C,/**< Pvlan isolation discard, from community port to community port, source and dest community id are */
    CTC_DKIT_SUB_EPE_PORT_ISO_DIS_PVLAN_X2P,/**< Pvlan isolation discard, to promiscuous port*/
    CTC_DKIT_SUB_EPE_PORT_ISO_DIS_UNKNOWN_UC_ISOLATION,/**< port isolation discard, unknown ucast isolation, source and dest isolation id are */
    CTC_DKIT_SUB_EPE_PORT_ISO_DIS_KNOWN_UC_ISOLATION,/**< port isolation discard, known ucast isolation, source and dest isolation id are */
    CTC_DKIT_SUB_EPE_PORT_ISO_DIS_UNKNOWN_MC_ISOLATION,/**< port isolation discard, unknown mcast isolation, source and dest isolation id are */
    CTC_DKIT_SUB_EPE_PORT_ISO_DIS_KNOWN_MC_ISOLATION,/**< port isolation discard, known mcast isolation, source and dest isolation id are */
    CTC_DKIT_SUB_EPE_PORT_ISO_DIS_BC_ISOLATION,/**< port isolation discard, bcast isolation, source and dest isolation id are */

    CTC_DKIT_SUB_EPE_VPLS_HORIZON_SPLIT_DIS_HORIZON,/**< VPLS horrizon split discard*/
    CTC_DKIT_SUB_EPE_VPLS_HORIZON_SPLIT_DIS_E_TREE,/**< E-tree discard*/

    CTC_DKIT_SUB_EPE_UC_MC_FLOODING_DIS_UNKNOWN_UC,/**< unknown ucast discard*/
    CTC_DKIT_SUB_EPE_UC_MC_FLOODING_DIS_KNOWN_UC,/**< known ucast discard*/
    CTC_DKIT_SUB_EPE_UC_MC_FLOODING_DIS_UNKNOWN_MC,/**< unknown mcast discard*/
    CTC_DKIT_SUB_EPE_UC_MC_FLOODING_DIS_KNOWN_MC,/**< known mcast discard*/
    CTC_DKIT_SUB_EPE_UC_MC_FLOODING_DIS_BC,/**< bcast discard*/

    CTC_DKIT_SUB_EPE_TTL_FAIL_ROUTE_MCAST,/**< new TTL is too little after decrease 1 when do route operation, new TTL and threshold are */
    CTC_DKIT_SUB_EPE_TTL_FAIL_ROUTE_TTL_0,/**< new TTL is 0 after decrease 1 when do route operation*/
    CTC_DKIT_SUB_EPE_TTL_FAIL_MPLS,/**< new TTL is 0 after decrease 1 when do MPLS operation*/
    CTC_DKIT_SUB_EPE_TTL_FAIL_TRILL,/**< new TTL is 0 after decrease 1 when do TRILL operation*/

    CTC_DKIT_SUB_EPE_NAT_PT_ICMP_ERR_IPV4,/**< IPv4 ICMP, l4 source port is 0x03XX or 0x04XX or 0x11XX or 0x12XX, redirect to CPU*/
    CTC_DKIT_SUB_EPE_NAT_PT_ICMP_ERR_IPV6,/**< IPv6 ICMP, l4 source port is 0, redirect to CPU*/

    CTC_DKIT_SUB_EPE_OAM_FILTERING_DIS_EDGE,/**< up MEP send ether OAM packet to egde port discard*/
    CTC_DKIT_SUB_EPE_OAM_FILTERING_DIS_LINK,/**< link OAM can not forward*/
    CTC_DKIT_SUB_EPE_OAM_FILTERING_DIS_VLAN,/**< ether OAM packet discard by vlan filtering, dsvlan index=*/
    CTC_DKIT_SUB_EPE_OAM_FILTERING_DIS_STP,/**< ether OAM packet discard by STP filtering*/
    CTC_DKIT_SUB_EPE_OAM_FILTERING_DIS_PORT,/**< ether OAM packet discard by port transmit disable*/

    CTC_DKIT_SUB_EPE_SAME_IPDA_IPSA_DIS_MAC,/**< same MAC discard*/
    CTC_DKIT_SUB_EPE_SAME_IPDA_IPSA_DIS_IP,/**< same IP discard*/

    CTC_DKIT_SUB_EPE_DEST_VLAN_PTR_DIS_1FFE,/**< invalid dest vlan index 0x1FFE*/
    CTC_DKIT_SUB_EPE_DEST_VLAN_PTR_DIS_1FFD,/**< invalid dest vlan index 0x1FFD*/

    /*OAM*/
    CTC_DKIT_SUB_OAM_EXCE_NON,/**< None*/
    CTC_DKIT_SUB_OAM_EXCE_ETH_INVALID,    /**<ETH OAM (Y1731)PDU invalid /CSF interval mismatch /MD level lower than passive MEP MD level to CPU/MIP received non-process oam PDU/ Configure error*/
    CTC_DKIT_SUB_OAM_EXCE_ETH_LB,    /**< Send equal LBM/LBR to CPU */
    CTC_DKIT_SUB_OAM_EXCE_ETH_LT,    /**< Send equal LTM/LTR to CPU */
    CTC_DKIT_SUB_OAM_EXCE_ETH_LM,    /**< Send equal LMM/LmR to CPU */
    CTC_DKIT_SUB_OAM_EXCE_ETH_DM,    /**< ETH_1DM/ETH_DMM/ETH_DMR to CPU */
    CTC_DKIT_SUB_OAM_EXCE_ETH_TST,    /**< ETH TST to CPU */
    CTC_DKIT_SUB_OAM_EXCE_ETH_APS,    /**< APS PDU to CPU(EthAPS & RAPS) */
    CTC_DKIT_SUB_OAM_EXCE_ETH_SCC,    /**< ETH_SCC to CPU */
    CTC_DKIT_SUB_OAM_EXCE_ETH_MCC,    /**< ETH_MCC to CPU */
    CTC_DKIT_SUB_OAM_EXCE_ETH_CSF,    /**< ETH_CSF to CPU */
    CTC_DKIT_SUB_OAM_EXCE_ETH_BIG_CCM,    /**< Big ccm to CPU/mep cfg in sw */
    CTC_DKIT_SUB_OAM_EXCE_ETH_LEARN_CCM,    /**< Learning CCM to CPU */
    CTC_DKIT_SUB_OAM_EXCE_ETH_DEFECT,    /**< someRDIdefect/someMACstatusDefect/lowCcm/Maid mismatch/ UnExcepect MEP-Rmep MisMatch (P2P mode)/CCM sequence number error /Source MAC mismatch/Unexp Interval-CCM interval mismatch to CPU/PBT OAM PresentTraffic Mismatch*/
    CTC_DKIT_SUB_OAM_EXCE_PBX_OAM,    /**< PBB/PBT OAM */
    CTC_DKIT_SUB_OAM_EXCE_ETH_HIGH_VER,    /**< High version oam send to cpu */
    CTC_DKIT_SUB_OAM_EXCE_ETH_TLV,    /**< Ether CCM TLV Option */
    CTC_DKIT_SUB_OAM_EXCE_ETH_OTHER,    /**< Other ETH OAM PDU to CPU */
    CTC_DKIT_SUB_OAM_EXCE_BFD_INVALID,    /**< BFD PDU invalid */
    CTC_DKIT_SUB_OAM_EXCE_BFD_LEARN,    /**< Learning BFD send to CPU (ParserResult.YourDiscr == 0 ) */
    CTC_DKIT_SUB_OAM_EXCE_BIG_BFD,    /**< Big bfd to CPU/mep cfg in sw */
    CTC_DKIT_SUB_OAM_EXCE_BFD_TIMER_NEG,    /**< BFD timer Negotiation packet to CPU */
    CTC_DKIT_SUB_OAM_EXCE_TP_SCC,    /**< MPLS TP SCC to CPU */
    CTC_DKIT_SUB_OAM_EXCE_TP_MCC,    /**< MPLS TP MCC to CPU */
    CTC_DKIT_SUB_OAM_EXCE_TP_CSF,    /**< MPLS TP CSF to CPU */
    CTC_DKIT_SUB_OAM_EXCE_TP_LB,    /**< MPLS TP LB to CPU */
    CTC_DKIT_SUB_OAM_EXCE_TP_DLM,    /**< MPLS TP DLM to cpu */
    CTC_DKIT_SUB_OAM_EXCE_TP_DM,    /**< DM/DLMDM to cpu */
    CTC_DKIT_SUB_OAM_EXCE_TP_FM,    /**< MPLS TP FM to CPU */
    CTC_DKIT_SUB_OAM_EXCE_BFD_OTHER,    /**< Other BFD OAM PDU to CPU*/
    CTC_DKIT_SUB_OAM_EXCE_BFD_DISC_MISMATCH,/**< BFD discriminator mismatch*/
    CTC_DKIT_SUB_OAM_EXCE_ETH_SM, /**< Send equal SLM/SLR to CPU*/
    CTC_DKIT_SUB_OAM_EXCE_TP_CV, /**< MPLS TP CV to CPU*/

};
typedef enum ctc_dkit_discard_sub_reason_id_e ctc_dkit_discard_sub_reason_id_t;

extern const char*
ctc_goldengate_dkit_get_reason_desc(ctc_dkit_discard_reason_id_t reason_id);

extern const char*
ctc_goldengate_dkit_get_sub_reason_desc(ctc_dkit_discard_sub_reason_id_t reason_id);



#ifdef __cplusplus
}
#endif

#endif




