/**
 @file ctc_greatbelt_nexthop.h

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2009-11-18

 @version v2.0



Nexthop is a wider concept, which is originated from IP route's nexthop concept. In Centec SDK, Nexthop
contain a set of multiple table, and these table may contain DsFwd,DsMet,DsNexthop,DsL2Edit,DsL3Edit,
and so on. These table determine the packet sent to which a port, and the packet editing before send it
out, and in the different applications, Nexthop contains the different table.
\b
\p
\d  L2UC: DsFwd->DsNexthop
\d  L2MC: DsMet->DsNexthop
\d  IPUC: DsFwd(opt)->DsNexthop
\d  IPMC: DsMet->DsNexthop
\d  MPLS Push Nexthop: DsFwd->DsNexthop->DsL2EditEth ->(DsL3EditMpls, opt)
\d  MPLS PHP Nexthop: DsFwd->DsNexthop->DsL2EditEth
\d  IP tunnel: DsFwd->DsNexthop->DsL2EditEth& DsL3EditTunnel
\d  ECMP:   N * DsFwd  (ECMP member :IPUC/MPLS/IP tunnel)
\d  ILoop : DsFwd
\d  Drop :  DsFwd
\d  BypassAll£º DsFwd->DsNexthop
\d  STP tunnel nexthop£ºDsFwd->DsNexthop->DsL2EditEth
\b
\p
There are two types of Nexthop: one is external and created by the user of SDK. A NHID is assigned by
the user when creating the nexthop,the IPUC/MPLS/IP tunnel Nexthop belong to the type; the other is
internal and created by the SDK itself, and L2UC/L2MC/IPMC Nexthop belong to the type.
\b
\p
Nexthop ID is an important concepts are related to Nexthop,As described above, the Nexthop is a set of
multiple table. The Nexthop ID is used to find the specific set information of the Nexthop. Other than
that, the other an important concepts is dsnh_offset, it indicates the offset of the corresponding
DsNexthop entry. SDK work in pizzbox mode or ingress Edit mode, external Nexthop's dsnh_offset can be
allocated in SDK; else it must be allocated by high-level application. User can config use which kinds
of mode in nextHop module initialization, by default, SDK will allocated the external nexthop's dsnh_offset.


*/

#ifndef _CTC_GREATBELT_NH_H
#define _CTC_GREATBELT_NH_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_stats.h"

/**********************************************************************************
                      Define API function interfaces
***********************************************************************************/
/**
 @addtogroup nexthop NEXTHOP
 @{
*/

/**
 @brief Initialize the nexthop module

 @param[in] lchip    local chip id

 @param[in] nh_cfg  nexthop module global config

 @remark Initialize tables and registers structures for Nexthop management functions.
           when nh_cfg is NULL,nexthop will use default global config to initialize sdk.
           By default:
\p
            1) nexthop support 16k extenal nexthop, use nh_cfg.max_external_nhid to change the number of
                extenal nexthop;
\p
            2) dsl2edit & dsL3edtit  & dsnexthop,dsMet will share ram according to FTM profile,you can
              change the configuration according to the following variable :
              nh_cfg.local_met_num/nh_cfg.local_nh_4w_num/nh_cfg.l2_edit_4w_num/nh_cfg.l3_edit_4w_num;
\p
            3) dsnh_offset will be allocated in SDK .you can change nh_cfg.alloc_nh_offset_in_sdk to 0
               and it will be allocated by high-level application;

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nexthop_init(uint8 lchip, ctc_nh_global_cfg_t* nh_cfg);

/**
 @brief De-Initialize nexthop module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the nexthop configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_nexthop_deinit(uint8 lchip);

/**********************************************************************************
                      Define l2 ucast nexthop functions
***********************************************************************************/
/**
 @brief This function is to create normal ucast bridge nexthop entry

 @param[in] lchip    local chip id

 @param[in] gport   global port id

 @param[in] nh_type   bridge unicast nexthop sub type to create

 @remark Add L2UC nexthop with the given gport and nh_type, by default,
            For regular port,the L2UC nexthop have been created in the Port initialization;
            For Linkagg port,the L2UC nexthop have been created when sdk create the linkagg.
            please refer to ctc_linkagg_create() and ctc_port_init(),ctc_port_set_port_en().
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_add_l2uc(uint8 lchip, uint32 gport,  ctc_nh_param_brguc_sub_type_t nh_type);

/**
 @brief This function is to delete normal ucast bridge nexthop entry

 @param[in] lchip    local chip id

 @param[in] gport   global port id

 @remark Delete L2UC nexthop with the given gport,
          and you need to delete all the forwarding tables use this Nexthop before deleting it.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_remove_l2uc(uint8 lchip, uint32 gport);

/**
 @brief Get ucast nhid by type

 @param[in] lchip    local chip id

 @param[in] gport global port of the system
 @param[in] nh_type   bridge unicast nexthop sub type to create
 @param[out] nhid nexthop ID to get

 @remark Retrieves  L2UC nexthop's NHID of the given gport and subtype.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_get_l2uc(uint8 lchip, uint32 gport, ctc_nh_param_brguc_sub_type_t nh_type, uint32* nhid);

/**********************************************************************************
                      Define nexthop Mac functions
***********************************************************************************/
/**
 @brief Add Next Hop Router Mac

 @param[in] lchip    local chip id

 @param[in] arp_id   ARP id

 @param[in] p_param nexthop MAC information

 @remark  Add Next Hop Router Mac
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_add_nexthop_mac(uint8 lchip, uint16 arp_id, ctc_nh_nexthop_mac_param_t* p_param);

/**
 @brief Remove Next Hop Router Mac

 @param[in] lchip    local chip id

 @param[in] arp_id   ARP id

 @remark  Remove Next Hop Router Mac
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_remove_nexthop_mac(uint8 lchip, uint16 arp_id);

/**
 @brief Update Next Hop Router Mac

 @param[in] lchip    local chip id

 @param[in] arp_id   ARP id

 @param[in] p_new_param   New nexthop MAC information

 @remark  Update Next Hop Router Mac
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_update_nexthop_mac(uint8 lchip, uint16 arp_id, ctc_nh_nexthop_mac_param_t* p_new_param);

/**********************************************************************************
                      Define ipuc nexthop functions
***********************************************************************************/
/**
 @brief Create IPUC nexthop

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID to be created

 @param[in] p_nh_param nexthop parameter used to create this ipuc nexthop

 @remark  Add IPUC nexthop with the given nhid and Next Hop parameter.
          The Next Hop parameter contains output interface(p_nh_param->oif) and Next Hop Router
          Mac(p_nh_param->mac).Use the CTC_IP_NH_FLAG_UNROV flag ,you can create unresolved nexthop.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_add_ipuc(uint8 lchip, uint32 nhid, ctc_ip_nh_param_t* p_nh_param);

/**
 @brief Remove IPUC nexthop

 @param[in] lchip    local chip id

 @param[in] nhid Nexthop ID to be removed

 @remark  Delete IPUC nexthop with the given nhid,
          and you need to delete all the forwarding tables use this Nexthop before deleting it.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_remove_ipuc(uint8 lchip, uint32 nhid);

/**
 @brief Update IPUC nexthop

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID to be updated

 @param[in] p_nh_param nexthop parameter used to update this ipuc nexthop

 @remark  Delete IPUC nexthop with the given nhid and Next Hop parameter,
          the update type :
\p
          1) CTC_NH_UPD_UNRSV_TO_FWD update unresolved(drop) nexthop to forwarding nexthop,you can
              update output interface and Next Hop Router Mac.
\p
          2) CTC_NH_UPD_FWD_TO_UNRSV update forwarding nexthop to unresolved nexthop
\p
          3) CTC_NH_UPD_FWD_ATTR updated forwarding nexthop attribute, you can update
              output interface and Next Hop Router Mac.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_update_ipuc(uint8 lchip, uint32 nhid, ctc_ip_nh_param_t* p_nh_param);

/**********************************************************************************
                      Define ip tunnel nexthop functions
***********************************************************************************/
/**
 @brief Create ip tunnel nexthop use to encapsulate the packet to a ip tunnel packet

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID to be created

 @param[in] p_nh_param nexthop parameter used to create this ip tunnel nexthop

 @remark  Add IP Tunnel nexthop with the given nhid and Next Hop parameter.
          The Next Hop parameter contains output interface(p_nh_param->oif) ,Next Hop Router Mac
          (p_nh_param->mac) and  the tunnel information.
          The tunnel types supported IPV4_IN4,IPV6_IN4,IPV4_IN6,IPV6_IN6,GRE_IN4,GRE_IN6,ISATAP,6TO4 ,
          6RD tunnel, and so on.
\b
\p
          After L3 tunnel editing,Greatbelt supports in two ways:
\p
          1) the encapsulated packet can be forwarded out of the port directly;
\p
          2) the encapsulated packet should be looped back via egress loopback path to the loopback
             interface where the tunnel packet re-enter the forwarding engine and the outgoing interface
             or port will be decided based on the tunnel header (te new Ip header)in the case,the flag
             CTC_IP_NH_TUNNEL_FLAG_REROUTE_WITH_TUNNEL_HDR is set.
\b
\p
         Use the CTC_IP_NH_FLAG_UNROV flag ,you can create unresolved nexthop.

 @return CTC_E_XXX


*/
extern int32
ctc_greatbelt_nh_add_ip_tunnel(uint8 lchip, uint32 nhid, ctc_ip_tunnel_nh_param_t* p_nh_param);

/**
 @brief Remove ip tunnel nexthop

 @param[in] lchip    local chip id

 @param[in] nhid Nexthop ID to be removed

 @remark  Delete IP tunnel nexthop with the given nhid,
          and you need to delete all the forwarding tables use this Nexthop before deleting it.


 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_remove_ip_tunnel(uint8 lchip, uint32 nhid);

/**
 @brief Update ip tunnel nexthop

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID to be updated

 @param[in] p_nh_param nexthop parameter used to update this ip tunnel nexthop

 @remark  Update ip tunnel nexthop

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_update_ip_tunnel(uint8 lchip, uint32 nhid, ctc_ip_tunnel_nh_param_t* p_nh_param);

/**********************************************************************************
                      Define mpls nexthop functions
***********************************************************************************/
/**
 @brief Create a mpls tunnel label

 @param[in] lchip    local chip id
 @param[in] tunnel_id    the id of mpls tunnel
 @param[in] p_nh_param   tunnel label parameter

 @remark This API is called to establish an mpls tunnel.
    To Create MPLS nexthop ,firstly you must call the function ctc_nh_add_mpls_tunnel_label() to create
       an MPLS Tunnel, and then call the function ctc_nh_add_mpls() to add an mpls Nexthop.
 @return CTC_E_XXX

*/

/*
MPLS Nexthop:
  DsFwd--dsnh_offset->DsNexthop-----L2EditPtr--DsL2EditEth  (MacDa & Tunnel Label(up to 2 labels))
                                   |
                                    -----L3EditPtr--(DsL3EditMpls)(VC Label and stats)
 For example:
    (1)If the sent packet is encapsulated with one layer label, the label must be used as tunnel label.
    (2)If the sent packet is encapsulated with two layer label, you can use one layer or two layer
       label as tunnel label.
    (3)If the sent packet is encapsulated with three layer label, you can use one layer or two layer
       label as tunnel label.

 If aps_en is disable,the p_nh_param->label_num indicate the number of tunnel label;and
       p_nh_param->nh_param contains the Next Hop Router mac and the MPLS tunnel information
       (Label/TTL/EXP) to be pushed onto a packet entering the tunnel.

 If aps_en is enbale,there are different cases:
  (1) if Tunnel label have two layer label,taking LSP label & SPME label as a case:
    if CTC_NH_MPLS_APS_EN & CTC_NH_MPLS_APS_2_LAYER_EN are set, aps_bridge_group_id is 2-level(Lsp)
          protection group id
                                    SPME label(w)  (nh_param)
              LSP label(w)-----                  (CTC_NH_MPLS_APS_2_LAYER_EN_PROTECTION isn't set)
                                    SPME label(p)   (nh_p_param)


                                   SPME label(w)   (nh_param)
              LSP label(P)------                    (CTC_NH_MPLS_APS_2_LAYER_EN_PROTECTION is set)
                                   SPME label(p)   (nh_p_param)

    if CTC_NH_MPLS_APS_EN only is set, there are two case:
     1)aps_bridge_group_id is 2-level(Lsp) protection group id.

              LSP label(w)------> SPME label   (-->nh_param)
              LSP label(P)------> SPME label   (-->nh_p_param)

     2)aps_bridge_group_id is 3-level(spme) protection group id.
                                    SPME label(w)  (nh_param)
              LSP label------>
                                    SPME label(p)   (nh_p_param)
  (2) if Tunnel label have one layer label,taking LSP label as a case:
      if CTC_NH_MPLS_APS_EN  is set,
              LSP label(w)   (-->nh_param)
              LSP label(P)  (-->nh_p_param)

*/

extern int32
ctc_greatbelt_nh_add_mpls_tunnel_label(uint8 lchip, uint16 tunnel_id, ctc_mpls_nexthop_tunnel_param_t* p_nh_param);

/**
 @brief remove a mpls tunnel label

 @param[in] lchip    local chip id

 @param[in] tunnel_id    the id of mpls tunnel

 @remark This API is called to delete an mpls tunnel,
          and you need delete all use the tunnel's MPLS Nexthop before deleting it.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_remove_mpls_tunnel_label(uint8 lchip, uint16 tunnel_id);

/**
 @brief update a mpls tunnel label

 @param[in] lchip    local chip id

 @param[in] tunnel_id    the id of mpls tunnel
 @param[in] p_new_param  tunnel label parameter

 @remark This API is called to update an mpls tunnel.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_update_mpls_tunnel_label(uint8 lchip, uint16 tunnel_id, ctc_mpls_nexthop_tunnel_param_t* p_new_param);



/**
 @brief swap 2 mpls tunnel label

 @param[in] lchip    local chip id

 @param[in] old_tunnel_id   the id of old mpls tunnel

 @param[in] new_tunnel_id   the id of new mpls tunnel

 @remark swap between old and new tunnel id

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_swap_mpls_tunnel_label(uint8 lchip, uint16 old_tunnel_id,uint16 new_tunnel_id);



/**
 @brief Create a mpls nexthop

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID to be created

 @param[in] p_nh_param   nexthop parameter used to create this nexthop

 @remark This API is called to add an mpls nexthop,the nexthop contains the outgoing interface,
            Next Hop router mac and label information (Tunnel label and VC label).
\b
\p
            The mpls nexthop type maybe 3 type:
\p
\d          CTC_MPLS_NH_PUSH_TYPE    indicate it is a mpls push nexthop
\d          CTC_MPLS_NH_POP_TYPE     indicate it is a mpls pop nexthop
\d          CTC_MPLS_NH_SWITCH_TYPE  indicate it is a mpls pop nexthop[HB]
\b
\p
  When aps_en is set,indicate the nexthop's APS is enable, indicate working and protection path's use different VC label)
\p

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_add_mpls(uint8 lchip, uint32 nhid, ctc_mpls_nexthop_param_t* p_nh_param);

/**
 @brief Remove mpls nexthop

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID to be removed

 @remark  Delete MPLS nexthop with the given nhid,
          and you need to delete all the forwarding tables use this Nexthop before deleting it.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_remove_mpls(uint8 lchip, uint32 nhid);

/**
 @brief Update a mpls unresolved nexthop to forwarded mpls push nexthop

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID to be updated

 @param[in] p_nh_param nexthop parameter used to update this nexthop

 @remark  Update MPLS nexthop with the given nhid.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_update_mpls(uint8 lchip, uint32 nhid, ctc_mpls_nexthop_param_t* p_nh_param);

/**********************************************************************************
                      Define iloop nexthop functions
***********************************************************************************/
/**
 @brief Create a ipe loopback nexthop

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID to be created

 @param[in] p_nh_param loopback nexthop parameters

 @remark  Add ILoop nexthop with the given nhid and parameter.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_add_iloop(uint8 lchip, uint32 nhid, ctc_loopback_nexthop_param_t* p_nh_param);

/**
 @brief Remove ipe loopback nexthop

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID to be removed

 @remark  Delete ILoop nexthop with the given nhid,
          and you need to delete all the forwarding tables use this Nexthop before deleting it.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_remove_iloop(uint8 lchip, uint32 nhid);

/**********************************************************************************
                      Define rspan(remote mirror) nexthop functions
***********************************************************************************/
/**
 @brief Create a rspan(remote mirror) nexthop

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID to be created
 @param[in] p_nh_param remote mirror nexthop parameters

 @remark  Add a RSPSN nexthop with the given nhid and parameters.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_add_rspan(uint8 lchip, uint32 nhid, ctc_rspan_nexthop_param_t* p_nh_param);

/**
 @brief Remove rspan(remote mirror) nexthop

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID to be removed

 @remark  Delete RSPSN nexthop with the given nhid,
          and you need to delete all the forwarding tables use this Nexthop before deleting it.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_remove_rspan(uint8 lchip, uint32 nhid);

/**********************************************************************************
                      Define ECMP nexthop functions
***********************************************************************************/
/**
 @brief Create a ECMP nexthop

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID to be created

 @param[in] p_nh_param ecmp nexthop parameters

 @remark  Add a ECMP nexthop and create a ecmp group.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_add_ecmp(uint8 lchip, uint32 nhid, ctc_nh_ecmp_nh_param_t* p_nh_param);

/**
 @brief Delete a ECMP nexthop

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID of ECMP to be removed

 @remark  Delete ECMP nexthop with the given nhid,
          and you need to delete all the forwarding tables use this Nexthop before deleting it.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_nh_remove_ecmp(uint8 lchip, uint32 nhid);

/**
 @brief Update a ECMP nexthop

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID to be created
 @param[in] p_nh_param ecmp nexthop parameters

 @remark  Add or Delete ECMP member.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_update_ecmp(uint8 lchip, uint32 nhid, ctc_nh_ecmp_nh_param_t* p_nh_param);

/**
 @brief Set the maximum ECMP paths allowed for a route

 @param[in] lchip    local chip id

 @param[out] max_ecmp the maximum ECMP paths allowed for a route

 @remark  Set the maximum ECMP paths allowed for a route ,the default value is CTC_DEFAULT_MAX_ECMP_NUM.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_set_max_ecmp(uint8 lchip, uint16 max_ecmp);

/**
 @brief Get the maximum ECMP paths allowed for a route

 @param[in] lchip    local chip id

 @param[out] max_ecmp the maximum ECMP paths allowed for a route

 @remark  Get the maximum ECMP paths allowed for a route

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_get_max_ecmp(uint8 lchip, uint16* max_ecmp);

/**********************************************************************************
                      Define advanced vlan/APS  nexthop functions
***********************************************************************************/

/**
 @brief The function is to create Egress Vlan Editing nexthop or APS Egress Vlan Editing nexthop

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID to be created

 @param[in] p_nh_param  nexthop parameter used to create this nexthop

 @remark  Add Xlate nexthop with the given nhid and xlate parameter
          it be used to do egress vlan edit for using nexthop.
          generally, it is used to L2 APS and N:1 egress vlan translation.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_add_xlate(uint8 lchip, uint32 nhid, ctc_vlan_edit_nh_param_t* p_nh_param);

/**
 @brief The function is to remove Egress Vlan Editing nexthop or APS Egress Vlan Editing nexthop

 @param[in] lchip    local chip id

 @param[in] nhid      Egress vlan Editing nexthop id or APS Egress vlan Editing nexthop id

 @remark  Delete Xlate nexthop with the given nhid,
          and you need to delete all the forwarding tables use this Nexthop before deleting it.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_remove_xlate(uint8 lchip, uint32 nhid);

/**
 @brief This function is to get mcast nexthop id

 @param[in] lchip    local chip id

 @param[in] group_id   mcast group id

 @param[out] p_nhid   get nhid

 @remark  get mcast nexthop id by mcast group id

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_nh_get_mcast_nh(uint8 lchip, uint32 group_id, uint32* p_nhid);

/**
 @brief This function is to create mcast nexthop

 @param[in] lchip    local chip id

 @param[in] nhid   nexthop ID to be created

 @param[in] p_nh_mcast_group   nexthop parameter used to create this mcast nexthop

 @remark  create mcast nexthop

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_nh_add_mcast(uint8 lchip, uint32 nhid, ctc_mcast_nh_param_group_t* p_nh_mcast_group);

/**
 @brief This function is to delete mcast nexthop

 @param[in] lchip    local chip id

 @param[in] nhid   nexthopid

 @remark  Delete mcast nexthop with the given nhid,
          and you need to delete all the forwarding tables use this Nexthop before deleting it.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_nh_remove_mcast(uint8 lchip, uint32 nhid);

/**
 @brief This function is to add/remove mcast member

 @param[in] lchip    local chip id

 @param[in] nhid              nexthop ID to be created
 @param[in] p_nh_mcast_group  nexthop parameter used to add/remove  mcast member


 @remark  Add/remove mcast member

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_nh_update_mcast(uint8 lchip, uint32 nhid, ctc_mcast_nh_param_group_t* p_nh_mcast_group);

/**
 @brief The function is to create misc nexthop

 @param[in] lchip    local chip id

 @param[in] nhid nexthop ID to be created

 @param[in] p_nh_param  nexthop parameter used to create this nexthop

 @remark  Add some miscellaneous nexthop with the given nhid and miscellaneous parameter
          Contain the following miscellaneous nexthop:
\p
       1) Replace L2 Header, it support:
\p
            (1) Swap MacDa & MacSa;
\p
            (2) Replace MacDa/MacSa/Vlan Id.


 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_add_misc(uint8 lchip, uint32 nhid, ctc_misc_nh_param_t* p_nh_param);

/**
 @brief The function is to remove misc nexthop

 @param[in] lchip    local chip id

 @param[in] nhid     nexthop ID to be created

 @remark  Delete misc nexthop with the given nhid,
          and you need to delete all the forwarding tables use this Nexthop before deleting it.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_nh_remove_misc(uint8 lchip, uint32 nhid);

/**
 @brief The function is to get resolved dsnh_offset  by type

 @param[in] lchip    local chip id

 @param[in] type  the type of reserved dsnh_offset

 @param[out] p_offset  Retrieve reserved dsnh_offset

 @remark Retrieve resolved dsnh_offset  by type

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_get_resolved_dsnh_offset(uint8 lchip, ctc_nh_reserved_dsnh_offset_type_t type, uint32* p_offset);

/**
 @brief The function is to get nexthop information by nhid

 @param[in] lchip    local chip id

 @param[in] nhid  nexthop ID to be created

 @param[out] p_nh_info  Retrieve Nexthop information

 @remark Retrieve Nexthop information by nhid,include dsnh_offset & destination gport

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_get_nh_info(uint8 lchip, uint32 nhid, ctc_nh_info_t* p_nh_info);


/**
 @brief The function is to set nhop drop packet nhid

 @param[in] lchip    local chip id

 @param[in] nhid  nexthop ID to be created

 @param[out] p_nh_drop  Retrieve Nexthop drop information

 @remark Set nexthop to drop

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_nh_set_nh_drop(uint8 lchip, uint32 nhid, ctc_nh_drop_info_t* p_nh_drop);



/**@} end of @addtogroup nexthop NEXTHOP*/

#ifdef __cplusplus
}
#endif

#endif /*_CTC_HUMBER_NH_H*/

