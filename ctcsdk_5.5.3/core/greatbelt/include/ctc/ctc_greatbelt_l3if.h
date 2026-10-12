/**
 @file ctc_greatbelt_l3if.h

 @author Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2010-1-7

 @version v2.0

 GreatBelt supports 2 types of L3 interfaces: logic IP VLAN interfaces and physical IP interfaces. Total IP interface
 number is up to 1022. For each L3 interface, the corresponding data structures at IPE is DsSrcInterface while at EPE
 is DsDestInterface. All the properties of interface are stored in these two data structures. Both at IPE and EPE,
 interface ID will be used as index for DsSrcInterface/DsDestInterface to retrieve the property. While how to generate
 interface ID depends on the configuration for different L3 interface type.
\p
 Logic IP VLAN interface: Logic IP VLAN interface could be named as VLAN interface for short which is configured over
 VLAN which could include multiple physical ports. This type of interface is used to provide integrated routing and
 bridging on a single device. There is another name for VLAN interface that SVI (switched virtual interface).
 For VLAN interface, the interface ID is retrieved from DsVlan.interfaceId both at IPE and EPE. DsVlan is indexed by
 VLANPTR after VLAN assignment at IPE, and is indexed by DsNextHop.destVlanPtr[12:0] at EPE. Because GreatBelt
 supports 4K VLAN, DsNextHop.destVlanPtr[12] must be zero.
\p
 Physical IP interface: Physical IP interfaces could be named as routed port for short since which is configured
 directly over a physical port (could be a link aggregated port).
 For routed port, VLAN assignment process is skipped. The interface ID is retrieved from DsSrcPort.interfaceId at
 IPE and DsNextHop.destVlanPtr[9:0] at EPE. To distinguish VLAN interface above, DsNextHop.destVlanPtr[12] must be 1
 for routed port. At this time, DsSrcPort.routedPort should be configured as 1 to indicate this port is routed port.
 Also for routed port, DsSrcInterface.routeAllPackets can be set to 1 if the port is point-to-point connected to
 another router, so all IP unicast packets will be routed; otherwise this bit is set to 0, in this case, only IP
 unicast packets with the router's MAC will be routed, other IP unicast packets will be discarded as both route
 lookup and bridge lookup are skipped.
\p
*/
#ifndef _CTC_GREATBELT_L3_IF
#define _CTC_GREATBELT_L3_IF
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_l3if.h"

/**********************************************************************************
                      Define API function interfaces
***********************************************************************************/
/**
 @addtogroup l3if L3if
 @{
*/

/**
 @brief   Init L3 interface module

 @param[in] lchip    local chip id

 @param[in] l3if_global_cfg   The l3if module global configure parameter, use to define the maximum value of VRF id supported in this module.

 @remark  Initialize the l3if module, including global variable, soft table, asic table, etc.
          This function has to be called before any other l3if functions.

 @return  CTC_E_XXX

*/
extern int32
ctc_greatbelt_l3if_init(uint8 lchip, void* l3if_global_cfg);

/**
 @brief De-Initialize l3if module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the l3if configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_l3if_deinit(uint8 lchip);

/**
 @brief     The function is to create the L3 interface.

 @param[in] lchip    local chip id

 @param[in] l3if_id   The id of L3 interface

 @param[in] p_l3_if   L3 interface structure

 @remark    Greatbelt support 2 types of interface : Vlan L3if and Phy L3if. These two types of L3if share 1022
            L3 interface ID , and the range from 1~1022. While create the l3 interface , the following default
            properties will be set :
            \p
            \d CTC_L3IF_PROP_ROUTE_EN (TRUE),
            \d CTC_L3IF_PROP_IPV4_UCAST (TRUE),
            \d CTC_L3IF_PROP_VRF_ID (0),
            \d CTC_L3IF_PROP_VRF_EN (FALSE),

 @return    CTC_E_XXX
*/
extern int32
ctc_greatbelt_l3if_create(uint8 lchip, uint16 l3if_id, ctc_l3if_t* p_l3_if);

/**
  @brief     The function is to destroy the L3 interface.

 @param[in] lchip    local chip id

  @param[in] l3if_id   The id of L3 interface ,If l3if is phy if,the l3if_id range is from 1~1022

  @param[in] p_l3_if   L3 interface structure

  @remark    Delete the given L3 interface , check if the L3if id is present in the system  and the interface type
             is matched, if so, destory the L3 interface and delete all associated information.

  @return    CTC_E_XXX
*/
extern int32
ctc_greatbelt_l3if_destory(uint8 lchip, uint16 l3if_id, ctc_l3if_t* p_l3_if);


/**
  @brief     The function is to get the L3 interface ID.

 @param[in] lchip    local chip id

  @param[in]  p_l3_if   L3 interface structure
  @param[out] l3if_id   L3 interface ID

  @remark    Get L3 interface ID based on L3 interface information.

  @return    CTC_E_XXX
*/
extern int32
ctc_greatbelt_l3if_get_l3if_id(uint8 lchip, ctc_l3if_t* p_l3_if, uint16* l3if_id);

/**
  @brief      Config L3 interface's properties

 @param[in] lchip    local chip id

  @param[in]  l3if_id          The id of L3 interface

  @param[in]  l3if_prop        An L3 interface properties

  @param[in]  value            The value of an L3 interface properties

  @remark     Set the L3 interface's properties ,the L3 interface is identified by l3if_id, more information for
              the properties can be get in the comments of ctc_l3if_property_t .

  @return     CTC_E_XXX
*/
extern int32
ctc_greatbelt_l3if_set_property(uint8 lchip, uint16 l3if_id, ctc_l3if_property_t l3if_prop, uint32 value);

/**
    @brief      Get l3 interface's properties according to interface id

 @param[in] lchip    local chip id

    @param[in]  l3if_id    The id of L3 interface

    @param[in]  l3if_prop  An L3 interface properties

    @param[out] p_value    The value of an L3 interface properties

    @remark     Get the L3 interface's properties ,the L3 interface is identified by l3if_id, more information for
                the properties can be get in the comments of ctc_l3if_property_t. The return value will be stored
                in p_value.

    @return     CTC_E_XXX
*/

extern int32
ctc_greatbelt_l3if_get_property(uint8 lchip, uint16 l3if_id, ctc_l3if_property_t l3if_prop, uint32* p_value);

/**
     @brief      Config router mac

 @param[in] lchip    local chip id

     @param[in]  mac_addr     Router mac

     @remark     Packets received on a VLAN interface can be either routed to other IP interfaces or bridged to
                 another physical port of the same VLAN interface. And the router MAC of the VLAN interface is the
                 key to determine whether IP unicast packets should be routed or bridged. If the packet's MAC DA
                 matches the router MAC of the ingress interface, it will be routed, Ipv4/6 lookup table will be
                 used to lookup for the next hop; otherwise, it will be bridged.
\p
                 For routed port, if the packet's MAC DA matches the interface's router MAC, it will be routed;
                 otherwise it will be discarded. While DsSrcInterface.routeAllPacket is set, router MAC check
                 will be skipped and all packets will be routed.
\p

     @return     CTC_E_XXX
*/
extern int32
ctc_greatbelt_l3if_set_router_mac(uint8 lchip, mac_addr_t mac_addr);

/**
     @brief       Get router mac

 @param[in] lchip    local chip id

     @param[out]  mac_addr     Router mac

     @remark      Get the router mac value.

     @return      CTC_E_XXX
*/
extern int32
ctc_greatbelt_l3if_get_router_mac(uint8 lchip, mac_addr_t mac_addr);

/**
       @brief       Config 40bits virtual router mac prefix

 @param[in] lchip    local chip id

       @param[in]   prefix_idx  The prefix index of L3 interface router ,mac 0-1

       @param[out]  mac_40bit    40bits  router mac prefix

       @remark      Config 40bits virtual router mac prefix in IpeRouterMacCtl.routerMacType{0/1/2}[47:8]

       @return      CTC_E_XXX
*/
extern int32
ctc_greatbelt_l3if_set_vmac_prefix(uint8 lchip, uint32 prefix_idx, mac_addr_t mac_40bit);

/**
    @brief       Get 40bits virtual router mac prefix

 @param[in] lchip    local chip id

    @param[in]   prefix_idx  The prefix index of L3 interface router mac, 0-1 

    @param[out]  mac_40bit    40bits router mac prefix

    @remark      Get 40bits virtual router mac prefix in IpeRouterMacCtl.routerMacType{0/1/2}[47:8]

    @return      CTC_E_XXX
*/
extern int32
ctc_greatbelt_l3if_get_vmac_prefix(uint8 lchip, uint32 prefix_idx, mac_addr_t mac_40bit);

/**
     @brief      Config a low 8 bits virtual router-mac in the L3 interface, it can config up to 4 VRIDs for a index

 @param[in] lchip    local chip id

     @param[in]  l3if_id          The id of L3 interface

     @param[in]  p_l3if_vmac      The information of a low 8 bits virtual router-mac

     @remark     Config low 8 bits virtual router mac in DsSrcInterface.routerMacLabel[7:0], the DsSrcInterface is
                 indexed by l3if_id.

     @return     CTC_E_XXX
*/
extern int32
ctc_greatbelt_l3if_add_vmac_low_8bit(uint8 lchip, uint16 l3if_id, ctc_l3if_vmac_t* p_l3if_vmac);

/**
     @brief      Remove a low 8 bits virtual router-mac in the L3 interface, it can config up to 4 VRIDs for a index

 @param[in] lchip    local chip id

     @param[in]  l3if_id             The id of L3 interface

     @param[in]  p_l3if_vmac         The information of a low 8 bits virtual router-mac

     @remark     Remove low 8 bits virtual router mac in DsSrcInterface.routerMacLabel[7:0], the DsSrcInterface is
                 indexed by l3if_id.

     @return     CTC_E_XXX
*/
extern int32
ctc_greatbelt_l3if_remove_vmac_low_8bit(uint8 lchip, uint16 l3if_id, ctc_l3if_vmac_t* p_l3if_vmac);

/**
     @brief          Get a low 8 bits router-mac according to interface id

 @param[in] lchip    local chip id

     @param[in]      l3if_id        The id of L3  interface

     @param[in,out]  p_l3if_vmac    The information of  a low 8 bits virtual router-mac

     @remark         Get low 8 bits virtual router mac in DsSrcInterface.routerMacLabel[7:0], the DsSrcInterface is
                     indexed by l3if_id.

     @return         CTC_E_XXX
*/
extern int32
ctc_greatbelt_l3if_get_vmac_low_8bit(uint8 lchip, uint16 l3if_id, ctc_l3if_vmac_t* p_l3if_vmac);

/**
     @brief          Enable vrf stats

 @param[in] lchip    local chip id

     @param[in]      p_vrf_stats        The vrf stats para

     @remark         Only support type CTC_L3IF_VRF_STATS_UCAST.

     @return         CTC_E_XXX
*/
extern int32
ctc_greatbelt_l3if_set_vrf_stats_en(uint8 lchip, ctc_l3if_vrf_stats_t* p_vrf_stats);

/**@} end of @addtogroup l3if L3if*/

#ifdef __cplusplus
}
#endif

#endif

