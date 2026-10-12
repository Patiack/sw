#ifndef _CTC_DKIT_GOLDENGATE_DISCARD_TYPE_H_
#define _CTC_DKIT_GOLDENGATE_DISCARD_TYPE_H_


#ifdef __cplusplus
extern "C" {
#endif


#include "sal.h"

enum ctc_dkit_discard_reason_id_e
{
    CTC_DKIT_DISCARD_INVALIED                          =0x0000,  /**< CTC_DKIT_DISCARD_INVALIED, no discard*/
    /*IPE*/
    /* 0~9 */
    CTC_DKIT_IPE_USER_ID_BINDING_DISCARD =0x0001,                /**< CTC_DKIT_IPE_USER_ID_BINDING_DISCARD, The data in packet is not as secuirty ip source guard/scl bingding config, such as MacSa, port, vlan, IpSa*/
    CTC_DKIT_IPE_HDR_ADJ_BYTE_RMV_ERR_DISCARD,                   /**< CTC_DKIT_IPE_HDR_ADJ_BYTE_RMV_ERR_DISCARD, Loopback header adjust/remove byte error discard or sgmac discard pdu packet*/
    CTC_DKIT_IPE_PSR_LEN_ERR,                                    /**< CTC_DKIT_IPE_PSR_LEN_ERR, Parser layer2/3/4 packet minimum length error discard*/
    CTC_DKIT_IPE_MAC_ISOLATED_DISCARD,                           /**< CTC_DKIT_IPE_MAC_ISOLATED_DISCARD, Mac Isolated Discard*/
    CTC_DKIT_IPE_EXCEP_2_DISCARD,                                /**< CTC_DKIT_IPE_EXCEP_2_DISCARD, Layer2 pdu exception discard*/
    CTC_DKIT_IPE_USER_ID_DISCARD,                                /**< CTC_DKIT_IPE_USER_ID_DISCARD, SCL config as discard*/
    CTC_DKIT_IPE_PORT_OR_DS_VLAN_RCV_DISCARD,                    /**< CTC_DKIT_IPE_PORT_OR_DS_VLAN_RCV_DISCARD, Port/vlan receive disabled discard*/
    CTC_DKIT_IPE_ITAG_CHK_FAIL,                                  /**< CTC_DKIT_IPE_ITAG_CHK_FAIL, ITag check failure discard, or invalid BmacDa discard, PBB packet source port type is PIP and discard invalid loop MacSa, PBB NCA check failure discard and PBB res2 check failure discard*/
    CTC_DKIT_IPE_PTL_VLAN_DISCARD,                               /**< CTC_DKIT_IPE_PTL_VLAN_DISCARD, Protocol vlan ether type match failure discard*/
    CTC_DKIT_IPE_VLAN_TAG_CTL_DISCARD,                           /**< CTC_DKIT_IPE_VLAN_TAG_CTL_DISCARD, Violate acceptable frame type(AFT) control rule of svlan/cvlan tag discard*/
    /* 10~19 */
    CTC_DKIT_IPE_NOT_ALLOW_MCAST_MAC_SA_DISCARD,                 /**< CTC_DKIT_IPE_NOT_ALLOW_MCAST_MAC_SA_DISCARD, MacSa is mcast address, MacSa equal MacDa, IpSa equal IpDa, tcp Or udp header error, icmp fragment packet or gre version mismatch discard*/
    CTC_DKIT_IPE_STP_DISCARD,                                    /**< CTC_DKIT_IPE_STP_DISCARD, STP discard*/
    CTC_DKIT_IPE_RESERVED0,                                      /**< CTC_DKIT_IPE_RESERVED0, Reserved*/
    CTC_DKIT_IPE_PBB_OAM_DISCARD,                                /**< CTC_DKIT_IPE_PBB_OAM_DISCARD, PBB OAM discard*/
    CTC_DKIT_IPE_ARP_DHCP_DISCARD,                               /**< CTC_DKIT_IPE_ARP_DHCP_DISCARD, Config on port/vlan discard arp/dhcp/fip packet, routed port discard arp bcast MacDa packet, check and discard invalid sendip arp packet by default*/
    CTC_DKIT_IPE_DS_PHYPORT_SRC_DISCARD,                         /**< CTC_DKIT_IPE_DS_PHYPORT_SRC_DISCARD, Ingress phyport discard*/
    CTC_DKIT_IPE_VLAN_STATUS_FILTER_DISCARD,                     /**< CTC_DKIT_IPE_VLAN_STATUS_FILTER_DISCARD, Ingress vlan filtering discard*/
    CTC_DKIT_IPE_ACLQOS_DISCARD_PKT,                             /**< CTC_DKIT_IPE_ACLQOS_DISCARD_PKT, ACL deny discard*/
    CTC_DKIT_IPE_ROUTING_MCAST_IP_ADDR_CHK_DISCARD,              /**< CTC_DKIT_IPE_ROUTING_MCAST_IP_ADDR_CHK_DISCARD, Routing multicast ip address check discard or ip protocal translation discard*/
    CTC_DKIT_IPE_BRG_BPDU_ETC_DISCARD,                           /**< CTC_DKIT_IPE_BRG_BPDU_ETC_DISCARD, Bridge dest port match source port, or unknow mcast/ucast mac address discard*/
    /* 20~29 */
    CTC_DKIT_IPE_STORM_CTL_DISCARD,                              /**< CTC_DKIT_IPE_STORM_CTL_DISCARD, Storm control discard*/
    CTC_DKIT_IPE_LEARNING_DISCARD,                               /**< CTC_DKIT_IPE_LEARNING_DISCARD, In software learning mode, config port/vlan/vsi/system mac limit discard, mac port binding, station move or MacSa discard*/
    CTC_DKIT_IPE_POLICING_DISCARD,                               /**< CTC_DKIT_IPE_POLICING_DISCARD, Policing discard*/
    CTC_DKIT_IPE_NO_FWD_PTR_DISCARD,                             /**< CTC_DKIT_IPE_NO_FWD_PTR_DISCARD, Not find next hop or no dsFwdPtr discard*/
    CTC_DKIT_IPE_FWD_PTR_ALL_F_OR_VALID_DISCARD,                 /**< CTC_DKIT_IPE_FWD_PTR_ALL_F_OR_VALID_DISCARD, Nexthop or forward index is invalid discard*/
    CTC_DKIT_IPE_FATAL_EXCEPTION_DISCARD,                        /**< CTC_DKIT_IPE_FATAL_EXCEPTION_DISCARD, Fatal Exception discard*/
    CTC_DKIT_IPE_APS_DISCARD,                                    /**< CTC_DKIT_IPE_APS_DISCARD, APS bridge or select discard*/
    CTC_DKIT_IPE_FWD_DEST_ID_DISCARD,                            /**< CTC_DKIT_IPE_FWD_DEST_ID_DISCARD, Forward destid discard*/
    CTC_DKIT_IPE_RESERVED1,                                      /**< CTC_DKIT_IPE_RESERVED1, Reserved*/
    CTC_DKIT_IPE_HDR_AJUST_SMALL_PKT_DISCARD,                    /**< CTC_DKIT_IPE_HDR_AJUST_SMALL_PKT_DISCARD, Header adjust small packet discard*/
    /* 30~39 */
    CTC_DKIT_IPE_HDR_ADJUST_PKT_ERR,                             /**< CTC_DKIT_IPE_HDR_ADJUST_PKT_ERR, Header adjust packet error*/
    CTC_DKIT_IPE_TRILL_ESADI_PKT_DISCARD,                        /**< CTC_DKIT_IPE_TRILL_ESADI_PKT_DISCARD, TRILL OAM packet discard, send to CPU*/
    CTC_DKIT_IPE_LOOPBACK_DISCARD,                               /**< CTC_DKIT_IPE_LOOPBACK_DISCARD, Loopback discard*/
    CTC_DKIT_IPE_EFM_DISCARD,                                    /**< CTC_DKIT_IPE_EFM_DISCARD, EFM OAM packet discard, send to CPU*/
    CTC_DKIT_IPE_WLAN_FROM_AC_ERR_OR_UNEXPECTABLE,             /**< CTC_DKIT_IPE_WLAN_FROM_AC_ERR_OR_UNEXPECTABLE, WLAN form AC error discard, WLAN unexpectable discard*/
    CTC_DKIT_IPE_STACKING_NETWORK_HEADER_CHK_ERR,                /**< CTC_DKIT_IPE_STACKING_NETWORK_HEADER_CHK_ERR, Stacking network header check failure discard*/
    CTC_DKIT_IPE_TRILL_FILTER_ERR,                               /**< CTC_DKIT_IPE_TRILL_FILTER_ERR, TRILL filter discard / invalid TRILL MacDa on edge port*/
    CTC_DKIT_IPE_WLAN_CONTROL_EXCEPTION,                       /**< CTC_DKIT_IPE_WLAN_CONTROL_EXCEPTION, WLAN control exception discard, send to CPU*/
    CTC_DKIT_IPE_L3_EXCEPTION,                                   /**< CTC_DKIT_IPE_L3_EXCEPTION, Layer3 exception discard, send to CPU*/
    CTC_DKIT_IPE_L2_EXCEPTION,                                   /**< CTC_DKIT_IPE_L2_EXCEPTION, Layer2 exception discard, send to CPU*/
    /* 40~49 */
    CTC_DKIT_IPE_TRILL_MCAST_ADDR_CHK_ERR,                       /**< CTC_DKIT_IPE_TRILL_MCAST_ADDR_CHK_ERR, TRILL mcast address check error discard*/
    CTC_DKIT_IPE_TRILL_VERSION_CHK_ERR,                          /**< CTC_DKIT_IPE_TRILL_VERSION_CHK_ERR, TRILL version check error / inner VlanId check discard*/
    CTC_DKIT_IPE_PTP_VERSION_CHK_ERR,                            /**< CTC_DKIT_IPE_PTP_VERSION_CHK_ERR, PTP version check error discard*/
    CTC_DKIT_IPE_PTPT_P2P_TRANS_CLOCK_DELAY_DISCARD,             /**< CTC_DKIT_IPE_PTPT_P2P_TRANS_CLOCK_DELAY_DISCARD, PTPT P2P transparent clock discard delay*/
    CTC_DKIT_IPE_OAM_NOT_FOUND_DISCARD,                          /**< CTC_DKIT_IPE_OAM_NOT_FOUND_DISCARD, OAM MEP not found discard*/
    CTC_DKIT_IPE_OAM_STP_VLAN_FILTER_DISCARD,                    /**< CTC_DKIT_IPE_OAM_STP_VLAN_FILTER_DISCARD, OAM STP/VLAN filter discard*/
    CTC_DKIT_IPE_BFD_SINGLE_HOP_OAM_TTL_CHK_ERR,                 /**< CTC_DKIT_IPE_BFD_SINGLE_HOP_OAM_TTL_CHK_ERR, BFD single hop OAM TTL check error discard*/
    CTC_DKIT_IPE_WLAN_DTLS_DISCARD,                            /**< CTC_DKIT_IPE_WLAN_DTLS_DISCARD, WLAN DTLS discard*/
    CTC_DKIT_IPE_NO_MEP_MIP_DISCARD,                             /**< CTC_DKIT_IPE_NO_MEP_MIP_DISCARD, No MEP/MIP discard*/
    CTC_DKIT_IPE_OAM_DISCARD,                                    /**< CTC_DKIT_IPE_OAM_DIS, MPLS OAM disable discard*/
    /* 50~59 */
    CTC_DKIT_IPE_PBB_DECAP_DISCARD,                              /**< CTC_DKIT_IPE_PBB_DECAP_DISCARD, PBB decap discard*/
    CTC_DKIT_IPE_MPLS_TMPLS_OAM_DISCARD,                         /**< CTC_DKIT_IPE_MPLS_TMPLS_OAM_DISCARD, MPLS/T-MPLS OAM router alert label discard*/
    CTC_DKIT_IPE_MPLSTP_MCC_SCC_DISCARD,                         /**< CTC_DKIT_IPE_MPLSTP_MCC_SCC_DISCARD, MPLS-TP MCC/SCC discard, send to CPU*/
    CTC_DKIT_IPE_ICMP_ERR_MSG_DISCARD,                           /**< CTC_DKIT_IPE_ICMP_ERR_MSG_DISCARD, ICMP error message discard, send to CPU*/
    CTC_DKIT_IPE_PTP_ACL_EXCEPTION,                              /**< CTC_DKIT_IPE_PTP_ACL_EXCEPTION, PTP ACL exception discard*/
    CTC_DKIT_IPE_ETHER_SERVICE_OAM_DISCARD,                      /**< CTC_DKIT_IPE_ETHER_SERVICE_OAM_DISCARD, Ethernet service OAM without vlan tag discard*/
    CTC_DKIT_IPE_LINK_OAM_DISCARD,                               /**< CTC_DKIT_IPE_LINK_OAM_DISCARD, Ethernet link OAM with vlan tag discard*/
    CTC_DKIT_IPE_TUNNEL_DECAP_OUTER_MARTIAN_ADDR_DISCARD,        /**< CTC_DKIT_IPE_TUNNEL_DECAP_OUTER_MARTIAN_ADDR_DISCARD, Tunnel decap outer header ipv4 IpSa address is martian address discard, ipv6 IpSa address is 6to4,IPv4MappedIPv6,IPv4 compatible or martian address discard.*/
    CTC_DKIT_IPE_WLAN_BSSID_LKP_OR_LOGIC_PORT_CHK_DISCARD,     /**< CTC_DKIT_IPE_WLAN_BSSID_LKP_OR_LOGIC_PORT_CHK_DISCARD, WLAN BSSID lookup failure discard or WLAN logic port check discard*/
    CTC_DKIT_IPE_USER_ID_FWD_PTR_ALL_F_DISCARD,                  /**< CTC_DKIT_IPE_USER_ID_FWD_PTR_ALL_F_DISCARD, Tunnel discard forwarding pointer discard*/
    /* 60~62 */
    CTC_DKIT_IPE_WLAN_FRAGMEN_DISCARD,                         /**< CTC_DKIT_IPE_WLAN_FRAGMEN_DISCARD, WLAN fragment discard*/
    CTC_DKIT_IPE_TRILL_RPF_CHK_FAIL,                             /**< CTC_DKIT_IPE_TRILL_RPF_CHK_FAIL, TRILL rpf check fail discard*/
    CTC_DKIT_IPE_MUX_PORT_ERR,                                   /**< CTC_DKIT_IPE_MUX_PORT_ERR, Mux Port error discard*/
    CTC_DKIT_IPE_RESERVED2,                                      /**< CTC_DKIT_IPE_RESERVED2, Reserved*/

    CTC_DKIT_IPE_HW_HAR_ADJ,                                     /**< CTC_DKIT_IPE_HW_HAR_ADJ, Some hardware error occurred at header adjust*/
    CTC_DKIT_IPE_HW_INT_MAP,                                     /**< CTC_DKIT_IPE_HW_INT_MAP, Some hardware error occurred at interface mapper*/
    CTC_DKIT_IPE_HW_LKUP,                                        /**< CTC_DKIT_IPE_HW_LKUP, Some hardware error occurred at lookup manager */
    CTC_DKIT_IPE_HW_PKT_PROC,                                    /**< CTC_DKIT_IPE_HW_PKT_PROC, Some hardware error occurred when do packet process*/
    CTC_DKIT_IPE_HW_PKT_FWD,                                     /**< CTC_DKIT_IPE_HW_PKT_FWD, Some hardware error occurred at forward*/
    CTC_DKIT_IPE_MAX,                                            /**< */

    /*EPE*/
    CTC_DKIT_EPE_EPE_HDR_ADJ_DEST_ID_DISCARD=0x0100,             /**< CTC_DKIT_EPE_EPE_HDR_ADJ_DEST_ID_DISCARD, Egress bridge header destid discard*/
    CTC_DKIT_EPE_EPE_HDR_ADJ_PKT_ERR_DISCARD,                    /**< CTC_DKIT_EPE_EPE_HDR_ADJ_PKT_ERR_DISCARD, Egress header adjust packet error discard*/
    CTC_DKIT_EPE_EPE_HDR_ADJ_BYTE_REMOVE_ERR,                    /**< CTC_DKIT_EPE_EPE_HDR_ADJ_BYTE_REMOVE_ERR, Egress header adjust byte remove error discard*/
    CTC_DKIT_EPE_DEST_PHY_PORT_DEST_ID_DISCARD,                  /**< CTC_DKIT_EPE_DEST_PHY_PORT_DEST_ID_DISCARD, DestPort Discard*/
    CTC_DKIT_EPE_PORT_ISOLATE_DISCARD,                           /**< CTC_DKIT_EPE_PORT_ISOLATE_DISCARD, Port isolate or private vlan discard*/
    CTC_DKIT_EPE_DS_VLAN_TRANS_DISCARD,                          /**< CTC_DKIT_EPE_DS_VLAN_TRANS_DISCARD, Port/vlan transmit disabled discard*/
    CTC_DKIT_EPE_BRG_TO_SAME_PORT_DISCARD,                       /**< CTC_DKIT_EPE_BRG_TO_SAME_PORT_DISCARD, Bridge to same port discard*/
    CTC_DKIT_EPE_VPLS_HORIZON_SPLIT_DISCARD,                     /**< CTC_DKIT_EPE_VPLS_HORIZON_SPLIT_DISCARD, VPLS split horizon split or E-tree source and dest are both leaf port discard*/
    CTC_DKIT_EPE_EGRESS_VLAN_FILTER_DISCARD,                     /**< CTC_DKIT_EPE_EGRESS_VLAN_FILTER_DISCARD, Egress VLAN filter discard*/
    CTC_DKIT_EPE_EGRESS_STP_DISCARD,                             /**< CTC_DKIT_EPE_EGRESS_STP_DISCARD, Egress STP discard*/
    /* 10~19 */
    CTC_DKIT_EPE_EGRESS_PARSER_LEN_ERR_DISCARD,                  /**< CTC_DKIT_EPE_EGRESS_PARSER_LEN_ERR_DISCARD, Egress parser layer4 packet length error discard */
    CTC_DKIT_EPE_EGRESS_PBB_CHK_DISCARD,                         /**< CTC_DKIT_EPE_EGRESS_PBB_CHK_DISCARD, Egress PBB check discard */
    CTC_DKIT_EPE_UCAST_MCAST_FLOOD_DISCARD,                      /**< CTC_DKIT_EPE_UCAST_MCAST_FLOOD_DISCARD, Discard unkown Unicast/mcast/broadcast on edge port */
    CTC_DKIT_EPE_802_3_OAM_DISCARD,                              /**< CTC_DKIT_EPE_802_3_OAM_DISCARD, Discard non-EFM OAM packet */
    CTC_DKIT_EPE_EGRESS_TTL_FAIL,                                /**< CTC_DKIT_EPE_EGRESS_TTL_FAIL, Egress TTL failed discard */
    CTC_DKIT_EPE_REMOTE_MIRROR_ESCAPE_DISCARD,                   /**< CTC_DKIT_EPE_REMOTE_MIRROR_ESCAPE_DISCARD, Remote mirror filtering MacDa discard */
    CTC_DKIT_EPE_TUNNEL_MTU_CHK_DISCARD,                         /**< CTC_DKIT_EPE_TUNNEL_MTU_CHK_DISCARD, Tunnel MTU check discard */
    CTC_DKIT_EPE_INTERFACE_MTU_CHK_DISCARD,                      /**< CTC_DKIT_EPE_INTERFACE_MTU_CHK_DISCARD, Interface MTU check discard */
    CTC_DKIT_EPE_LOGIC_PORT_CHK_DISCARD,                         /**< CTC_DKIT_EPE_LOGIC_PORT_CHK_DISCARD, Logic port check discard */
    CTC_DKIT_EPE_EGRESS_ACL_DISCARD,                             /**< CTC_DKIT_EPE_EGRESS_ACL_DISCARD, Egress ACL deny */
    /* 20~29 */
    CTC_DKIT_EPE_EGRESS_QOS_DISCARD,                             /**< CTC_DKIT_EPE_EGRESS_QOS_DISCARD, Egress QoS discard */
    CTC_DKIT_EPE_EGRESS_POLICING_DISCARD,                        /**< CTC_DKIT_EPE_EGRESS_POLICING_DISCARD, Egress policing discard */
    CTC_DKIT_EPE_CRC_ERR,                                        /**< CTC_DKIT_EPE_CRC_ERR, Egress CRC error */
    CTC_DKIT_EPE_ROUTE_PAYLOAD_OPERATION_DISCARD,                /**< CTC_DKIT_EPE_ROUTE_PAYLOAD_OPERATION_DISCARD, Route payload operation no IP packet discard */
    CTC_DKIT_EPE_BRG_PAYLOAD_OPERATION_DISCARD,                  /**< CTC_DKIT_EPE_BRG_PAYLOAD_OPERATION_DISCARD, Bridge payload operation bridge disabled discard */
    CTC_DKIT_EPE_PT_LAYER4_OFFSET_LAGER_DISCARD,                 /**< CTC_DKIT_EPE_PT_LAYER4_OFFSET_LAGER_DISCARD, Packet edit strip too large discard */
    CTC_DKIT_EPE_BFD_DISCARD,                                    /**< CTC_DKIT_EPE_BFD_DISCARD, BFD discard */
    CTC_DKIT_EPE_PORT_REFLECTIVE_CHK_DISCARD,                    /**< CTC_DKIT_EPE_PORT_REFLECTIVE_CHK_DISCARD, Port reflective check discard */
    CTC_DKIT_EPE_IP_MPLS_TTL_CHK_ERR_DISCARD,                    /**< CTC_DKIT_EPE_IP_MPLS_TTL_CHK_ERR_DISCARD, IP/MPLS TTL check error discard */
    CTC_DKIT_EPE_OAM_EGDE_PORT_DISCARD,                          /**< CTC_DKIT_EPE_OAM_EGDE_PORT_DISCARD, OAM edge port discard */
    /* 30~39*/
    CTC_DKIT_EPE_NAT_PT_ICMP_ERR,                                /**< CTC_DKIT_EPE_NAT_PT_ICMP_ERR, NAT/PT ICMP error discard */
    CTC_DKIT_EPE_RESERVED0,                                      /**< CTC_DKIT_EPE_RESERVED0, Reserved*/
    CTC_DKIT_EPE_LOCAL_OAM_DISCARD,                              /**< CTC_DKIT_EPE_LOCAL_OAM_DISCARD, Local OAM discard */
    CTC_DKIT_EPE_OAM_FILTERING_DISCARD,                          /**< CTC_DKIT_EPE_OAM_FILTERING_DISCARD, OAM filtering discard */
    CTC_DKIT_EPE_OAM_HASH_CONFILICT_DISCARD,                     /**< CTC_DKIT_EPE_OAM_HASH_CONFILICT_DISCARD, OAM hash confilict discard */
    CTC_DKIT_EPE_IPDA_EQUALS_TO_IPSA_DISCARD,                    /**< CTC_DKIT_EPE_IPDA_EQUALS_TO_IPSA_DISCARD, Same MacDa/MacSa or IpDa/IpSa discard */
    CTC_DKIT_EPE_RESERVED1,                                      /**< CTC_DKIT_EPE_RESERVED1, Reserved */
    CTC_DKIT_EPE_TRILL_PAYLOAD_OPERATION_DISCARD,                /**< CTC_DKIT_EPE_TRILL_PAYLOAD_OPERATION_DISCARD, TRILL payload operation discard */
    CTC_DKIT_EPE_PBB_CHK_FAIL_DISCARD,                           /**< CTC_DKIT_EPE_PBB_CHK_FAIL_DISCARD, PBB check fail discard */
    CTC_DKIT_EPE_DS_NEXT_HOP_DATA_VIOLATE,                       /**< CTC_DKIT_EPE_DS_NEXT_HOP_DATA_VIOLATE, DsNextHop data violate */
    CTC_DKIT_EPE_DEST_VLAN_PTR_DISCARD,                          /**< CTC_DKIT_EPE_DEST_VLAN_PTR_DISCARD, destVlanPtr discard*/
    CTC_DKIT_EPE_DS_L3_EDIT_DATA_VIOLATE_1,                      /**< CTC_DKIT_EPE_DS_L3_EDIT_DATA_VIOLATE_1, Config nexthop drop packet enable by DsL3EditMpls*/
    CTC_DKIT_EPE_DS_L3_EDIT_DATA_VIOLATE_2,                      /**< CTC_DKIT_EPE_DS_L3_EDIT_DATA_VIOLATE_2, Discard because of DsL3Edit config error*/
    CTC_DKIT_EPE_DS_L3_EDIT_NAT_DATA_VIOLATE,                    /**< CTC_DKIT_EPE_DS_L3_EDIT_NAT_DATA_VIOLATE, Config nexthop drop packet enable by DsL3EditNat*/
    CTC_DKIT_EPE_DS_L2_EDIT_DATA_VIOLATE_1,                      /**< CTC_DKIT_EPE_DS_L2_EDIT_DATA_VIOLATE_1, Config nexthop drop packet enable by DsL2Edit*/
    CTC_DKIT_EPE_DS_L2_EDIT_DATA_VIOLATE_2,                      /**< CTC_DKIT_EPE_DS_L2_EDIT_DATA_VIOLATE_2, Discard because of DsL2EditEth config error*/
    CTC_DKIT_EPE_PACKET_HEADER_C2C_TTL_ZERO,                     /**< CTC_DKIT_EPE_PACKET_HEADER_C2C_TTL_ZERO, Packetheader C2C TTL zero */
    CTC_DKIT_EPE_PT_UDP_CHECKSUM_IS_ZERO,                        /**< CTC_DKIT_EPE_PT_UDP_CHECKSUM_IS_ZERO, Protocol translation(e.g, IVI) UDP checksum is zero */
    CTC_DKIT_EPE_OAM_TO_LOCAL_DISCARD,                           /**< CTC_DKIT_EPE_OAM_TO_LOCAL_DISCARD, OAM to local discard */

    CTC_DKIT_EPE_HW_HAR_ADJ ,                                    /**< CTC_DKIT_EPE_HW_HAR_ADJ, Some hardware error occurred at header adjust*/
    CTC_DKIT_EPE_HW_NEXT_HOP,                                    /**< CTC_DKIT_EPE_HW_NEXT_HOP, Some hardware error occurred at nexthop mapper*/
    CTC_DKIT_EPE_HW_ACL_QOS,                                     /**< CTC_DKIT_EPE_HW_ACL_QOS, Some hardware error occurred when do ACL or QoS*/
    CTC_DKIT_EPE_HW_OAM ,                                        /**< CTC_DKIT_EPE_HW_OAM, Some hardware error occurred when do OAM process*/
    CTC_DKIT_EPE_HW_EDIT ,                                       /**< CTC_DKIT_EPE_HW_EDIT, Some hardware error occurred at header edit*/
    CTC_DKIT_EPE_MAX,                                            /**< */

    /*BSR*/
    CTC_DKIT_BSR_BUFSTORE_HW_ABORT              =0x0200,         /**< CTC_DKIT_BSR_BUFSTORE_HW_ABORT, Ipe discard or hardware error at BUFSTORE, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_BUFSTORE_HW_LEN_ERROR,                          /**< CTC_DKIT_BSR_BUFSTORE_HW_LEN_ERROR, Packet length error at BUFSTORE, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_BUFSTORE_HW_SILENT,                             /**< CTC_DKIT_BSR_BUFSTORE_HW_SILENT, Hardware error at BUFSTORE, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_BUFSTORE_HW_DATA_ERR,                           /**< CTC_DKIT_BSR_BUFSTORE_HW_DATA_ERR, Data error at BUFSTORE, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_BUFSTORE_HW_CHIP_MISMATCH,                      /**< CTC_DKIT_BSR_BUFSTORE_HW_CHIP_MISMATCH, Chip mismatch at BUFSTORE, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_BUFSTORE_HW_NO_BUFF,                            /**< CTC_DKIT_BSR_BUFSTORE_HW_NO_BUFF, No buffer at BUFSTORE, read PktErrStatsMem to confirm*/
    CTC_DKIT_BSR_BUFSTORE_HW_OTHER,                              /**< CTC_DKIT_BSR_BUFSTORE_HW_OTHER, Drop at BUFSTORE, read PktErrStatsMem to confirm*/

    CTC_DKIT_BSR_QMGR_CRITICAL,                                  /**< CTC_DKIT_BSR_QMGR_CRITICAL, Critical packet discard at QMGR, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_QMGR_C2C,                                       /**< CTC_DKIT_BSR_QMGR_C2C, C2C packet discard at QMGR, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_QMGR_EN_Q,                                      /**< CTC_DKIT_BSR_QMGR_EN_Q, Enqueue discard at QMGR, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_QMGR_EGR_RESRC,                                 /**< CTC_DKIT_BSR_QMGR_EGR_RESRC, Egress resource manager discard at QMGR, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_QMGR_OTHER,                                     /**< CTC_DKIT_BSR_QMGR_OTHER, Drop at QMGR, read QMgrEnqDebugStats to confirm*/
    CTC_DKIT_BSR_MAX,/**< */

    /*NETRX*/
    CTC_DKIT_NETRX_NO_BUFFER,                                    /**< CTC_DKIT_NETRX_NO_BUFFER, Buffer is full, read NetRxDebugStats to confirm*/
    CTC_DKIT_NETRX_LEN_ERROR,                                    /**< CTC_DKIT_NETRX_LEN_ERROR, Packet length error, read NetRxDebugStats to confirm*/
    CTC_DKIT_NETRX_PKT_ERROR,                                    /**< CTC_DKIT_NETRX_PKT_ERROR, Packet or frame error, read NetRxDebugStats to confirm*/
    CTC_DKIT_NETRX_SOP_EOP,                                      /**< CTC_DKIT_NETRX_SOP_EOP, SOP/EOP error discard, read NetRxDebugStats to confirm*/
    CTC_DKIT_NETRX_MAX,                                          /**< */

    /*NETTX*/
    CTC_DKIT_NETTX_MIN_LEN,                                      /**< CTC_DKIT_NETTX_MIN_LEN, Packet length is too short, read NetTxDebugStats to confirm*/
    CTC_DKIT_NETTX_NO_BUFFER,                                    /**< CTC_DKIT_NETTX_NO_BUFFER, Buffer is full, read NetTxDebugStats to confirm*/
    CTC_DKIT_NETTX_SOP_EOP,                                      /**< CTC_DKIT_NETTX_SOP_EOP, SOP/EOP error discard, read NetTxDebugStats to confirm*/
    CTC_DKIT_NETTX_PI_ERROR,                                     /**< CTC_DKIT_NETTX_PI_ERROR, Packet info error, read NetTxDebugStats to confirm*/
    CTC_DKIT_NETTX_MAX,                                          /**< */

    /*OAM*/
    CTC_DKIT_OAM_HW_ERROR,                                       /**< CTC_DKIT_OAM_HW_ERROR, Some hardware error occured*/
    CTC_DKIT_OAM_MAX,                                            /**< */

    CTC_DKIT_DISCARD_MAX                                         /**< */
};
typedef enum ctc_dkit_discard_reason_id_e ctc_dkit_discard_reason_id_t;


enum ctc_dkit_discard_sub_reason_id_e
{
    CTC_DKIT_SUB_DISCARD_INVALIED = 0x0,/**< no discard*/
    CTC_DKIT_SUB_DISCARD_AMBIGUOUS = 0x1,/**< the discard reason is ambiguous, try to use captured info*/
    CTC_DKIT_SUB_DISCARD_NEED_PARAM = 0x2/**< please input filter param to get the detail reason*/
};
typedef enum ctc_dkit_discard_sub_reason_id_e ctc_dkit_discard_sub_reason_id_t;

extern const char*
ctc_greatbelt_dkit_get_reason_desc(ctc_dkit_discard_reason_id_t reason_id);

extern const char*
ctc_greatbelt_dkit_get_sub_reason_desc(ctc_dkit_discard_sub_reason_id_t reason_id);



#ifdef __cplusplus
}
#endif

#endif




