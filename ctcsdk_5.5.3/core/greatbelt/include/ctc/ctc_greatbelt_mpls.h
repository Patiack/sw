/**
 @file ctc_greatbelt_mpls.h

 @author Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2010-03-12

 @version v2.0

 GreatBelt MPLS support can be broken down into two categories, Layer-2 and Layer-3 MPLS.
 L2 MPLS consists of point-to-point Virtual Private Wire Service (VPWS) and point-to-multipoint Virtual Private
 LAN Service (VPLS) Ethernet transport services.
\b
 In VPWS, Ethernet frames are mapped into a MPLS pseudowire based on incoming port plus packet header
 information. At the end of a pseudowire, forwarding of Ethernet frames after MPLS decapsulation is based on
 the MPLS label lookup.
\b
 In VPLS, Ethernet frames of interest are also identified by incoming port plus packet header information.
 Ethernet frames are switched into one or more MPLS pseudowires based on the MAC DA lookup. At the end of
 a pseudowire, frames again are switched based on MAC DA lookup.
\b
 L3 MPLS deals with the establishment of MPLS tunnels. At the MPLS tunnel initiation point, packets entering
 the tunnel are encapsulated with an MPLS header (label PUSH operation). IPv4 and IPv6 packets can be
 directed into an MPLS tunnel based on the DestIP lookup. Along the path of the MPLS tunnel, MPLS label SWAP,
 PUSH, PHP, and POP operations are supported. At the end of an MPLS tunnel, forwarding of IPv4 and IPv6
 packets is based on DestIP lookup. Virtual routing tables are supported for the DestIP lookup.
\b
 In the MPLS APIs, the concept of VPN is introduced. A VPN can be of type VPLS, VPWS, or L3:
\b
 For VPLS, a VPN is similar to a VLAN, which identifies a group of physical ports to be included in a broadcast
 domain. However, instead of physical ports, a VPLS VPN identifies a group of ¡°virtual-ports¡± to be included in
 a broadcast domain.  The VPN ID(VSI ID) is used to qualify MAC DA lookups in VPLS.
\b
 For VPWS, a VPN consists of two MPLS ports. Packets arriving on one MPLS port are sent directly out the other
 MPLS port.
\b
 L3 VPNs are used to identify a virtual routing table. When creating an L3 VPN, a VRF must be specified. At the
 end of a MPLS tunnel, the IPv4 or IPv6 DestIP lookup is qualified by VRF associated with the L3 VPN.
\b
 In GreatBelt, MPLS is treated as a L3 feature, and L3 interface should be retrieved before MPLS process stage.
 The L3 interface and router MAC determination is the same as routing process. Especially,
 DsSrcInterface.routeAllPackets could be used to indicate all MPLS packet should follow MPLS process once
 DsSrcInterface.mplsEn is set.
\b
 DsMpls is the key data structure used at IPE MPLS process, which is accessed based on incoming label value,
 without any lookup. It determines the label's action and following forwarding behavior.

*/

#ifndef _CTC_GREATBELT_MPLS_H
#define _CTC_GREATBELT_MPLS_H
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_const.h"
#include "ctc_mpls.h"
#include "ctc_nexthop.h"
#include "ctc_stats.h"
/****************************************************************
*
* Defines and Macros
*
****************************************************************/

/****************************************************************
 *
* Function
*
****************************************************************/
/**
 @addtogroup mpls MPLS
 @{
*/

/**
 @brief Initialize the MPLS module

 @param[in] lchip    local chip id

 @param[in] p_mpls_info Data of the mpls initialization

 @remark Initialize the GreatBelt MPLS subsystem, Including global variable, interface space size, register callback
                function, etc.
                This function has to be called before any other mpls functions.
\p
                Set sizetype from ctc_mpls_label_size_type_e,
                Support 256 mpls interface space, space 0 used as global, space 1-255 for interface.
\p
                User Must set MPLS Profile, at the same time, allocate the interface space by yourself.
                e.g  MPLS_Profile=3K, space0=2K, space1=512,space2=512, allocate as follows:
\b
                p_mpls_info->space_info[0].enable = TRUE;
\p
                p_mpls_info->space_info[0].sizetype = CTC_MPLS_LABEL_NUM_2K_TYPE;
\p
                p_mpls_info->space_info[1].enable = TRUE;
\p
                p_mpls_info->space_info[1].sizetype = CTC_MPLS_LABEL_NUM_512_TYPE;
\p
                p_mpls_info->space_info[2].enable = TRUE;
\p
                p_mpls_info->space_info[2].sizetype = CTC_MPLS_LABEL_NUM_512_TYPE;

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_mpls_init(uint8 lchip, ctc_mpls_init_t* p_mpls_info);

/**
 @brief De-Initialize mpls module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the mpls configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_mpls_deinit(uint8 lchip);

/**
 @brief Add a mpls entry

 @param[in] lchip    local chip id

 @param[in] p_mpls_ilm Data of the mpls entry

 @remark  Add a MPLS ilm for every mpls situation.
\p
                In GreatBelt, every ilm is used with an nexthop,the default action for the label is pop.
\b
                Set pop=TRUE when need to deal with inner label & use inner label's nexthop to edit the packet.
\p
                Set decap=TRUE when the inner packet will decap in pop mode,that means if the label is popped and we use.
                    the inner packet(maybe ipv4) to do lookup and forwarding(lookup ipda if it's an ip packet).
\p
                Set model as mpls tunnel mode, from ctc_mpls_tunnel_mode_e and the default mode is uniform.
\b
                In pop mode, if nh isn't be configured, sdk will use a drop nh for solving the attack of too many label
                in one pkt. But user also can configure a loopback nh,by this way, GreatBelt can manage the packet
                    including more than 4 labels.
\b
                Use CTC_MPLS_LABEL_TYPE_NORMAL to add a normal mpls ilm.
\p
                Use CTC_MPLS_LABEL_TYPE_L3VPN to add a l3vpn mpls ilm.
\p
                Use CTC_MPLS_LABEL_TYPE_VPWS to add a vpws mpls ilm.
\p
                Use CTC_MPLS_LABEL_TYPE_VPLS to add a vpls mpls ilm.

  @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_mpls_add_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm);

/**
 @brief update a mpls entry

 @param[in] lchip    local chip id

 @param[in] p_mpls_ilm Data of the mpls entry

 @remark  Update a MPLS ilm for ttl & exp usage in different tunnel mode.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_mpls_update_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm);

/**
 @brief set a mpls entry property

 @param[in] lchip    local chip id

 @param[in] p_mpls_pro Data of the mpls entry

 @remark  Set a MPLS ilm property according property type.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_mpls_set_ilm_property(uint8 lchip, ctc_mpls_property_t* p_mpls_pro);

/**
 @brief Remove a mpls entry

 @param[in] lchip    local chip id

 @param[in] p_mpls_ilm Data of the mpls entry

 @remark  Delete a MPLS ilm by space id and mpls label.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_mpls_del_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm);

/**
 @brief Get information of a mpls ilm entry, include nexthop IDs

 @param[in] lchip    local chip id

 @param[out] nh_id Nexthop id array of the mpls ilm entry,the entrie info of the mpls ilm entry

 @param[in] p_mpls_ilm Index of the mpls ilm entry

 @remark  Get a MPLS ilm by space id and mpls label, and get the nh_id at the same time.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_mpls_get_ilm(uint8 lchip, uint32* nh_id, ctc_mpls_ilm_t* p_mpls_ilm);

/**
 @brief add mpls stats

 @param[in] lchip    local chip id

 @param[in] stats_index index of the mpls labe stats to be add

 @remark  Add MPLS stats by space id and mpls label.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_mpls_add_stats(uint8 lchip, ctc_mpls_stats_index_t* stats_index);

/**
 @brief delete mpls stats

 @param[in] lchip    local chip id

 @param[in] stats_index index of the mpls labe stats to be delete

 @remark  Remove MPLS stats by space id and mpls label.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_mpls_remove_stats(uint8 lchip, ctc_mpls_stats_index_t* stats_index);

/**
 @brief Add the l2vpn pw entry

 @param[in] lchip    local chip id

 @param[in] p_mpls_pw Information of l2vpn pw entry

 @remark  Add a l2vpn MPLS ilm for vpls or vpws.
\p
                Set fid, logic_port & logic_port_type when add a vpls ilm.
\p
                Set pw_nh_id when add a vpws ilm.
\p
                Use CTC_MPLS_L2VPN_VPLS to add a vpls ilm.
\p
                Use CTC_MPLS_L2VPN_VPWS to add a vpws ilm.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_mpls_add_l2vpn_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw);

/**
 @brief Remove the l2vpn pw entry

 @param[in] lchip    local chip id

 @param[in] p_mpls_pw Information of l2vpn pw entry

 @remark  Delete a l2vpn MPLS ilm for vpls or vpws.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_mpls_del_l2vpn_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw);


/**@} end of @addgroup   */

#ifdef __cplusplus
}
#endif

#endif

