/**
 @file ctc_greatbelt_ipuc.h

 @author Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2011-11-07

 @version v2.0

 Greatbelt supports wire speed IPv4/IPv6 unicast routing under any situation. According to the description in l3if,
 the source interface information will be retrieved at InterfaceMapper after Parser. For both IPv4/IPv6
 unicast routing, the source interface could be any type among the two L3 interface types.
 DsSrcInterface.v4UcastEn/DsSrcInterface.v6UcastEn must be set as 1 to enable IPv4/IPv6 Unicast routing on source
 interface. And the router MAC should be configured correctly on DsSrcInterface based on the description in l3if.
\p
 At LookupManager stage, only IPDA lookup is necessary, and the lookup key is IPDA + VRF ID. Algorithm based LPM lookup
 is necessary and TCAM is supplement for algorithm based LPM since hash confliction could not be avoid in algorithm
 based LPM. TCAM has two effects, one is to solve conflict, and the other is to expand the lookup table. A specific
 route must be located at one of the lookup table, but not both.
\p
 In both IPv4/IPv6 unicast routing, a route should be located at algorithm based LPM lookup table. If there is conflict,
 it should be located at TCAM. Besides routing table, a default entry is necessary. Greatbelt supports default entry
 for each different lookup type. IPv4/IPv6 unicast, IPv4/IPv6 multicast, etc., all lookup types have a default entry
 base distinguish with each other. After algorithm based LPM lookup, if there is not any hit, a default result will
 be retrieved directly. For IPv4 unicast routing, the default result index is
 FibEngineLookupResultCtl.ipDaLookupResultCtl0.defaultEntryBase + VRF ID. Generally, VRF ID is 0.
\p
 When using TCAM lookup, DsIpv4RouteKey should be used. The DsIpv4RouteKey.vrfId and DsIpv4RouteKey.ipDa should be
 configured correctly. And the mask for ipDa should be configured based on the route's prefix length. When using
 algorithm based LPM, the hash lookup of first stage could use 8 bits or 16 bits. The 8bit hash lookup will be
 performed when FibEngineLookupCtl.lpmHashMode[0] is 0. During this lookup, the high 16 bits of route and vrfId is
 combined as looking up key into DsLpmIpv4Hash16Key. The 16 bits hash lookup will be performed
 when FibEngineLookupCtl.lpmIpv4Pipeline3En is 0. Then the high 8 bits of route and vrfId is combined as
 looking up key into DsLpmIpv4Hash8Key. If 8 bits hash lookup is used, there are three pipelines followed.
 If 16 bits hash lookup is used, there are two pipelines followed. If the scenario in which Greatbelt is used,
 there is lot of different high 16 bits of route and vrfId , to avoid hash conflict 8 bits hash lookup should be used,
 else 16 bits hash lookup should be used. ctc_greatbelt_ipuc_init(void* ipuc_global_cfg) is used to configure the hash
 lookup mode, while initialize the IPv4 unicast routing module.
\p
 There are some entries only TCAM lookup can be used:
\p
\d 1) IPv4 16 bits hash lookup , route's prefix length is between [1,15].
\d 2) IPv4 8 bits hash lookup , route's prefix length is between [1,8].
\d 3) IPv6 mask route's prefix length is between [1,31] or [65,127].
\p
 Greatbelt supports network route and host route by same in TCAM and algorithm based LPM. Therefore, there isn't any
 difference between network route and host route in lookup stage. The only difference is the usage of DsFwd.
 For host route, ECMP will not occur, therefore DsFwd could be skipped. While for network route,
 DsFwd is needed because of ECMP.
\p
 Unicast reverse path forwarding check is an additional security function. It helps to mitigate problems that are
 caused by the introduction of malformed or spoofed source IP addresses into a network. This is done by discarding
 IP packets that lack a verifiable source IP address. If uRPF is enabled, then only those packets whose source
 IP addresses is consistent with the IP routing table are forwarded.
\p
 There are two uRPF modes of verification: 1) strict mode, and 2) loose mode. In strict uRPF check mode,
 checks are performed to ensure that the source IP address is present in the routing table and the incoming L3
 interface matches the source IP's L3 interface in the routing table. In loose uRPF check mode, it merely verifies
 that the source IP address is present in the routing table, and doesn't care the L3 interface match or not.
\p
 Greatbelt supports uRPF without any limitation. Based on general Unicast routing, there are some additional
 process for uRPF. To set the uRPF modes please refer to  the function
 ctc_greatbelt_l3if_set_property(uint16 l3if_id, ctc_l3if_property_t l3if_prop, uint32 value)
\p
 Equal cost multi-path (ECMP) is a technique for routing packets along multiple paths of equal cost. If multiple
 equal-cost routes to the same destination exist, ECMP can be used to provide load balancing among the redundant paths.
 Since the forwarding logic has several next hops for any given destination, it must use some method to choose
 which next hop should be used for a given data packet. Various routing protocols, including
 Open Shortest Path First (OSPF) and Intermediate System to Intermediate System (ISIS), explicitly allow ECMP routing,
 and some routers also allow equal cost multi-path usage with RIP and other routing protocols.
\p
*/

#ifndef _CTC_GREATBELT_IPUC_H
#define _CTC_GREATBELT_IPUC_H
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_const.h"
#include "ctc_ipuc.h"
#include "ctc_nexthop.h"
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
 @addtogroup ipuc IPUC
 @{
*/

/**
 @brief Initialize the IPUC module

 @param[in] lchip    local chip id

 @param[in] ipuc_global_cfg  ipuc module global config

 @remark This function initialize tables and registers structures for ipuc module and set default entry.
         The parameter ipuc_global_cfg could be NULL or a pointer of ctc_ipuc_global_cfg_t type, which defined
         the hash mode IPUC module used. By default NULL should be used which means IPv4 LPM lookup use hash 16 mode.
         If IPUC module IPv4 LPM should use hash 8 mode, ipuc_global_cfg must be set.
 \p
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ipuc_init(uint8 lchip, void* ipuc_global_cfg);

/**
 @brief De-Initialize ipuc module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the ipuc configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_ipuc_deinit(uint8 lchip);

/**
 @brief Add a route entry

 @param[in] lchip    local chip id

 @param[in] p_ipuc_param Data of the ipuc entry

 @remark This function add both IPv4 and IPv6 IP unicast entrys. Different properties can be retrived by set different flags
         in route_flag. If mask length is 0 , a new default entry will be set, it equals to
         ctc_greatbelt_ipuc_add_default_entry(uint8 ip_ver, uint32 nh_id).
\p
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ipuc_add(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param);

/**
 @brief Remove a route entry

 @param[in] lchip    local chip id

 @param[in] p_ipuc_param Data of the ipuc entry

 @remark This function remove both IPv4 and IPv6 IP unicast entrys. Route flag no need to specified

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ipuc_remove(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param);

/**
 @brief get a route entry

 @param[in] lchip    local chip id

 @param[in|out] p_ipuc_param Input data of the ipuc key, and output ipuc ad

 @remark This function get both IPv4 and IPv6 IP unicast entrys ad info, include nhid and route flag.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ipuc_get(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param);

/**
 @brief Add a NAT sa entry

 @param[in] lchip    local chip id

 @param[in] p_ipuc_nat_sa_param Data of the NAT sa entry

 @remark This function add IPv4 NAT sa entrys. For basic NAT, only need ipsa and vrfid as key. For NAPT, the l4_src_port is used as key extra.
         The new_ipsa and new_l4_src_port is the output edit info.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ipuc_add_nat_sa(uint8 lchip, ctc_ipuc_nat_sa_param_t* p_ipuc_nat_sa_param);

/**
 @brief remove a NAT sa entry

 @param[in] lchip    local chip id

 @param[in] p_ipuc_nat_sa_param Data of the NAT sa entry

 @remark This function add IPv4 NAT sa entrys. For basic NAT, only need ipsa and vrfid as key. For NAPT, the l4_src_port is used as key extra.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ipuc_remove_nat_sa(uint8 lchip, ctc_ipuc_nat_sa_param_t* p_ipuc_nat_sa_param);

/**
 @brief Add the default route entrys for both ipv4 and ipv6, default routes should be installed before using ipuc function

 @param[in] lchip    local chip id

 @param[in] ip_ver ip version of the default route

 @param[in] nh_id Nexthop ID of the default route

 @remark This function set a new default behavior for the vrfid 0 and the ip version which defined by the parameter ip_ver.
         The new default behavior is get from the parameter nh_id. Generally speaking the nhid should be 1 (drop) or 2(to cpu).
         This function equals to the function ctc_greatbelt_ipuc_add(ctc_ipuc_param_t* p_ipuc_param) with mask length 0
         and vrfid 0.
 \p
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ipuc_add_default_entry(uint8 lchip, uint8 ip_ver, uint32 nh_id);

/**
 @brief Add an IP-Tunnel route entry use to decapsulate the ip tunnel packet

 @param[in] lchip    local chip id

 @param[in] p_ipuc_tunnel_param Data of the ip tunnel entry

 @remark  IP Tunneling is a mechanism for encapsulating one protocol in another and for transport of a packet across a
          network that does not support the native protocol of the packet. Greatbelt supports diverse IP tunneling
          feature, IPv4 tunneling, IPv6 tunneling and GRE.
\p
          There are two basic function of IP tunneling feature: 1)A packet entering an IP tunnel -- Encapsulation
          or Initiator. 2) A packet leaving an IP tunnel -- Decapsulation or Terminator.
\p
          For encapsulation please refer to the function
          ctc_greatbelt_nh_add_ip_tunnel(uint32 nhid, ctc_ip_tunnel_nh_param_t* p_nh_param).
\p
          Decapsulation is implemented by IP Unicast routing process.
\p
          A packet leaving an IP tunnel, the IP tunnel header and L2 Ethernet header should be removed.
          Then an IP Unicast routing process will be implemented based on the original IP header. And a new L2 Ethernet
          header will be attached for the next hop. It looks like there are two forwarding processes. One is for outer
          IP tunnel header, the destination is an internal port. The other is for inner or original IP tunnel header,
          the destination is the next hop of this packet.
\p
          Greatbelt supports enable tunnel decapsulation on port. The port on which tunnel is terminated should
          enable SCL lookup, more information please refer to function
          ctc_greatbelt_port_set_scl_key_type(uint32 gport, ctc_direction_t dir, uint8 scl_id, ctc_port_scl_key_type_t type).
\p
          This function add IP Tunnel route entry , to terminat the ip tunnel and decapsulate the ip tunnel packet.
\p

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ipuc_add_tunnel(uint8 lchip, ctc_ipuc_tunnel_param_t* p_ipuc_tunnel_param);

/**
 @brief Remove an IP-Tunnel route entry which used to decapsulate the ip tunnel packet

 @param[in] lchip    local chip id

 @param[in] p_ipuc_tunnel_param Data of the ipuc entry, route_flag no need to specified

 @remark This function delete a existing IP-Tunnel route.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ipuc_remove_tunnel(uint8 lchip, ctc_ipuc_tunnel_param_t* p_ipuc_tunnel_param);

/**
 @brief traverse all ipuc entry

 @param[in] lchip    local chip id

 @param[in] ip_ver ip version of the default route

 @param[in] fn callback function to deal with all the ipuc entry

 @param[in] data data used by the callback function

 @remark This function goes through the route table and runs the callback function at each valid route, passing it the route
         information.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ipuc_traverse(uint8 lchip, uint8 ip_ver, void* fn, void* data);

/**
 @brief set global property for ipuc module

 @param[in] lchip    local chip id

 @param[in] p_global_prop the property should be set

 @remark This function set the global chip ipuc configuration.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ipuc_set_global_property(uint8 lchip, ctc_ipuc_global_property_t* p_global_prop);

/**
 @brief Arrange lpm fragment

 @param[in] lchip    local chip id

 @param[in] p_arrange_info not used now

 @remark When add ipuc entry and return CTC_E_TOO_MANY_FRAGMENT error, it means there's too many
         fragments for the lpm sram. Then you can call this function to arrange the fragment.
         After this operation, you can add ipuc entry success.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ipuc_arrange_fragment(uint8 lchip, void* p_arrange_info);


/**@} end of @addgroup   */

#ifdef __cplusplus
}
#endif

#endif

