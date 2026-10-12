/**
 @file ctc_greatbelt_port.h

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2011-11-09

 @version v2.0

 The Port module is a resource module, the port related APIs provide control over a significant number of port attributes.
 Each port known to the API has certain abilities. Some normal abilities can be determined during the initialization of
 the port module. For a given port, the overall capabilities for a port are determined by taking the least common
 denominator of all the factors. These attributes can be set by the APIs provided. The gport in this module is global
 physical port, not includes linkagg port.
\b

 The Port module provides many APIs which often used to configure some well known abilities, such as:
\p
\d   ctc_port_set_port_en();
\d   ctc_port_set_vlan_ctl();
\d   ctc_port_set_default_vlan();
\d   ctc_port_set_vlan_filter_en();
\b
 And for extend, the module provides APIs named ctc_port_set_property() and ctc_port_set_property_with_direction() to
 set the other features, refer to the enum ctc_port_property_t.
\p
 Some important structs or enums are described as follows, which defined in ctc_port.h:
\b

\t |  ctc_vlantag_ctl_t                       | Meaning                                  |
\t |-------------------------------------------------------------------------------------|
\t | CTC_VLANCTL_ALLOW_ALL_PACKETS            | allow all packet                         |
\t |-------------------------------------------------------------------------------------|
\t | CTC_VLANCTL_DROP_ALL_UNTAGGED            | drop all untagged packet                 |
\t |-------------------------------------------------------------------------------------|
\t | CTC_VLANCTL_DROP_ALL_TAGGED              | drop all tagged packet                   |
\t |-------------------------------------------------------------------------------------|
\t | CTC_VLANCTL_DROP_ALL                     | drop all packet                          |
\t |-------------------------------------------------------------------------------------|
\t | CTC_VLANCTL_DROP_PACKET_WITHOUT_TWO_TAG  | drop packet without double tagged        |
\t |-------------------------------------------------------------------------------------|
\t | CTC_VLANCTL_DROP_ALL_PACKET_WITH_TWO_TAG | drop packet with double tagged           |
\t |-------------------------------------------------------------------------------------|
\t | CTC_VLANCTL_DROP_ALL_SVLAN_TAG           | drop packet with stag                    |
\t |-------------------------------------------------------------------------------------|
\t | CTC_VLANCTL_DROP_ALL_NON_SVLAN_TAG       | drop packet without stag                 |
\t |-------------------------------------------------------------------------------------|
\t | CTC_VLANCTL_DROP_ONLY_SVLAN_TAG          | drop packet with only stag               |
\t |-------------------------------------------------------------------------------------|
\t | CTC_VLANCTL_PERMIT_ONLY_SVLAN_TAG        | permit packet with only stag             |
\t |-------------------------------------------------------------------------------------|
\t | CTC_VLANCTL_DROP_ALL_CVLAN_TAG           | drop packet with ctag                    |
\t |-------------------------------------------------------------------------------------|
\t | CTC_VLANCTL_DROP_ALL_NON_CVLAN_TAG       | drop packet without ctag                 |
\t |-------------------------------------------------------------------------------------|
\t | CTC_VLANCTL_DROP_ONLY_CVLAN_TAG          | drop packet with only ctag               |
\t |-------------------------------------------------------------------------------------|
\t | CTC_VLANCTL_PERMIT_ONLY_CVLAN_TAG        | permit packet with only ctag             |
\t |-------------------------------------------------------------------------------------|


\b
\t | ctc_port_vlan_domain_type_t              | Meaning                                  |
\t |-------------------------------------------------------------------------------------|
\t | CTC_PORT_VLAN_DOMAIN_SVLAN               | svlan domain                             |
\t |-------------------------------------------------------------------------------------|
\t | CTC_PORT_VLAN_DOMAIN_CVLAN               | cvlan domain                             |
\t |-------------------------------------------------------------------------------------|

\b
\t | ctc_dot1q_type_t                         | Meaning                                  |
\t |-------------------------------------------------------------------------------------|
\t | CTC_DOT1Q_TYPE_NONE                      | packet transmit out with untag           |
\t |-------------------------------------------------------------------------------------|
\t | CTC_DOT1Q_TYPE_CVLAN                     | packet transmit out with ctag            |
\t |-------------------------------------------------------------------------------------|
\t | CTC_DOT1Q_TYPE_SVLAN                     | packet transmit out with stag            |
\t |-------------------------------------------------------------------------------------|
\t | CTC_DOT1Q_TYPE_BOTH                      | packet transmit out with double tag      |
\t |-------------------------------------------------------------------------------------|

\b
\t | ctc_port_raw_packet_t                    | Meaning                                  |
\t |-------------------------------------------------------------------------------------|
\t | CTC_PORT_RAW_PKT_NONE                    | raw packet parser disable                |
\t |-------------------------------------------------------------------------------------|
\t | CTC_PORT_RAW_PKT_ETHERNET                | port parser ethernet raw packet          |
\t |-------------------------------------------------------------------------------------|
\t | CTC_PORT_RAW_PKT_IPV4                    | port only parser ipv4 raw packet         |
\t |-------------------------------------------------------------------------------------|
\t | CTC_PORT_RAW_PKT_IPV6                    | port only parser ipv6 raw packet         |
\t |-------------------------------------------------------------------------------------|

*/

#ifndef _CTC_GREATBELT_PORT_H
#define _CTC_GREATBELT_PORT_H
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_const.h"
#include "ctc_vlan.h"
#include "ctc_port.h"
/****************************************************************
*
* Defines and Macros
*
****************************************************************/
/****************************************************************************
 *
* Function
*
*****************************************************************************/
/**
 @addtogroup port PORT
 @{
*/

/**
 @brief Initialize the port module

 @param[in] lchip    local chip id

 @param[in]  p_port_global_cfg

 @remark    Initialize the port module and set default config.
            \p
            Default config:
            \p
             (1)Default VLAN for untagged packets is set to 1, refer to ctc_port_set_default_vlan();
             \p
             (2)Ingress VLAN Filtering is disabled, refer to ctc_port_set_vlan_filter_en();
             \p
             (3)Port tx, rx and bridge is enable, refer to ctc_port_set_port_en() and ctc_port_set_bridge_en();
             \p
             (4)Set svlan domain, refer to ctc_port_set_vlan_domain();
             \p
             (5)Disable route, refer to ctc_port_set_property();
             \p
             (6)Enable STP check;
             \p
             (7)Set dot1_q_en as CTC_DOT1Q_TYPE_BOTH, refer to ctc_port_set_dot1q_type();

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_init(uint8 lchip, void* p_port_global_cfg);

/**
 @brief De-Initialize port module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the port configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_port_deinit(uint8 lchip);

/**
 @brief set port default configure

 @param[in] lchip    local chip id

 @param[in]  gport global physical port

 @remark    Initialize the port module and set default config.
            \p
            Default config:
            \p
             (1)Default VLAN for untagged packets is set to 1, refer to ctc_port_set_default_vlan();
             \p
             (2)Ingress VLAN Filtering is disabled, refer to ctc_port_set_vlan_filter_en();
             \p
             (3)Port tx, rx and bridge is enable, refer to ctc_port_set_port_en() and ctc_port_set_bridge_en();
             \p
             (4)Set svlan domain, refer to ctc_port_set_vlan_domain();
             \p
             (5)Disable route, refer to ctc_port_set_property();
             \p
             (6)Enable STP check;
             \p
             (7)Set dot1_q_en as CTC_DOT1Q_TYPE_BOTH, refer to ctc_port_set_dot1q_type();

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_default_cfg(uint8 lchip, uint32 gport);

/**
 @brief Set port mac enable

 @param[in] lchip    local chip id

 @param[in] gport   global physical port

 @param[in] enable  a boolean value denote port mac enable or not

 @remark  Set mac enable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_mac_en(uint8 lchip, uint32 gport, bool enable);

/**
 @brief Get port mac enable

 @param[in] lchip    local chip id

 @param[in] gport   global physical port

 @param[out] p_enable  a boolean value denote port mac enable or not

 @remark  Get mac enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_mac_en(uint8 lchip, uint32 gport, bool* p_enable);

/**
 @brief Set port enable/disable, include Rx,Tx

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] enable a boolean value wanted to set

 @remark    This API can enable/disable port RX, TX

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_port_en(uint8 lchip, uint32 gport, bool enable);

/**
 @brief Get port enable/disable, include Rx,Tx

 @param[in] lchip    local chip id

 @param[in] gport  global physical port

 @param[out] p_enable  a boolean value to get(it is return TRUE when Rx, Tx are all enable)

 @remark  Get port enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_port_en(uint8 lchip, uint32 gport, bool* p_enable);

/**
 @brief Set port's vlan tag control mode

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] mode  refer to ctc_vlantag_ctl_t

 @remark   There are 14 modes to be selected for vlan control, refer to ctc_vlantag_ctl_t

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_vlan_ctl(uint8 lchip, uint32 gport, ctc_vlantag_ctl_t mode);

/**
 @brief Get port's vlan tag control mode

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[out] p_mode  refer to ctc_vlantag_ctl_t

 @remark  Get vlan control mode, refer to ctc_vlantag_ctl_t

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_vlan_ctl(uint8 lchip, uint32 gport, ctc_vlantag_ctl_t* p_mode);

/**
 @brief Set default vlan id of packet which receive from this port

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] vid default vlan id of the packet

 @remark
    set default vlan ID on certain port,and set the two features at the same time:
    \p
            dest_port.untag_default_svlan = 1,
            \p
            dest_port.untag_default_vlan_id = 1;
            \p
          so the svlan tag will be striped on the egress port.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_default_vlan(uint8 lchip, uint32 gport, uint16 vid);

/**
 @brief Get default vlan id of packet which receive from this port

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[out] p_vid default vlan id of the packet

 @remark  Get default vlan

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_default_vlan(uint8 lchip, uint32 gport, uint16* p_vid);

/**
 @brief Set vlan filtering enable/disable on the port

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] dir  flow direction, refer to ctc_direction_t

 @param[in] enable a boolean value denote whether the function is enable

 @remark    After enable vlan filter,the packet whose vlan ID is not in the port vlan will be discard.For example, the
            vlan ID in a packet is 10, and the port is in vlan 20, 30, 40, the packet will be discard when it arrive at the
            port if enable vlan filter

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_vlan_filter_en(uint8 lchip, uint32 gport, ctc_direction_t dir, bool enable);

/**
 @brief Get vlan filtering enable/disable on the port

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] dir  flow direction, refer to ctc_direction_t

 @param[out] p_enable a boolean value denote whether the function is enable

 @remark  Get vlan filter enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_vlan_filter_en(uint8 lchip, uint32 gport, ctc_direction_t dir, bool* p_enable);

/**
 @brief Set vlan domain when packet single tagged with TPID 0x8100

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] vlan_domain  cvlan or svlan

 @remark    when the cvlan TPID and the svlan TPID are all set to 0x8100, a packet with a single tag 0x8100 will
            be parsed as cvlan if set cvlan domain, and will be parsed as svlan if set svlan domain.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_vlan_domain(uint8 lchip, uint32 gport, ctc_port_vlan_domain_type_t vlan_domain);

/**
 @brief Get vlan domain

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[out] p_vlan_domain  cvlan or svlan

 @remark  Get vlan domain

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_vlan_domain(uint8 lchip, uint32 gport, ctc_port_vlan_domain_type_t* p_vlan_domain);

/**
 @brief Set what tag the packet with transmit from the port

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] type dot1q type of port untagged/ctagged/stagged/double-tagged

 @remark  Set dot1q type on the port, refer to ctc_dot1q_type_t

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_dot1q_type(uint8 lchip, uint32 gport, ctc_dot1q_type_t type);

/**
 @brief  Get what tag the packet with transmit from the port

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[out] p_type dot1q type of port untagged/ctagged/stagged/double-tagged

 @remark  Get dot1q type on the port

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_dot1q_type(uint8 lchip, uint32 gport, ctc_dot1q_type_t* p_type);

/**
 @brief Set port whether the transmit is enable

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] enable a boolean value wanted to set

 @remark  Enable/disable TX on the port

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_transmit_en(uint8 lchip, uint32 gport, bool enable);

/**
 @brief Get port whether the transmit is enable

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[out] p_enable a boolean value wanted to set

 @remark  Get TX enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_transmit_en(uint8 lchip, uint32 gport, bool* p_enable);

/**
 @brief Set port whether the receive is enable

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[out] enable a boolean value to get

 @remark  Enable/disable RX on the port

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_receive_en(uint8 lchip, uint32 gport, bool enable);

/**
 @brief Get port whether the receive is enable

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[out] p_enable a boolean value wanted to set

 @remark   Get RX enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_receive_en(uint8 lchip, uint32 gport, bool* p_enable);

/**
 @brief Set port whether layer2 bridge function is enable

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[out] enable a boolean value to get

 @remark Enable/disable bridge on the port

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_bridge_en(uint8 lchip, uint32 gport, bool enable);

/**
 @brief Get port whether layer2 bridge function is enable

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[out] p_enable a boolean value wanted to set

 @remark  Get bridge enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_bridge_en(uint8 lchip, uint32 gport, bool* p_enable);

/**
 @brief Set the port as physical interface

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] enable   enable/disable the port as physical interface

 @remark  Set the port as a physical interface, so the port will route packets

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_phy_if_en(uint8 lchip, uint32 gport, bool enable);

/**
 @brief Get whether route function on port is enable

 @param[in] lchip    local chip id

 @param[in]  gport   global physical port

 @param[out] p_l3if_id the id of associated l3 interface

 @param[out] p_enable  enable/disable the port as physical interface

 @remark  Get whether the port is a physical interface

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_phy_if_en(uint8 lchip, uint32 gport, uint16* p_l3if_id, bool* p_enable);

/**
 @brief Set the port as sub interface

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] enable   enable/disable the port as sub interface

 @remark  Set the port as a sub interface, defined with port and vlan
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_sub_if_en(uint8 lchip, uint32 gport, bool enable);


/**
 @brief Get whether sub-route function on port is enable

 @param[in] lchip    local chip id

 @param[in]  gport   global physical port

 @param[out] p_enable  enable/disable the port as sub interface

 @remark  Get whether the port is a sub interface

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_sub_if_en(uint8 lchip, uint32 gport, bool* p_enable);

/**
 @brief The function is to set stag tpid index

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] dir  flow direction, refer to ctc_direction_t

 @param[in] index stag tpid index, can be configured in ethernet ctl register

 @remark  There are 4 index to select for stag tpid, when set stag tpid index to 3, the packet will be parser as untagged
          packet

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_stag_tpid_index(uint8 lchip, uint32 gport, ctc_direction_t dir, uint8 index);

/**
 @brief The function is to get stag tpid index

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] dir  flow direction, refer to ctc_direction_t

 @param[out] p_index stag tpid index, the index point to stag tpid value

 @remark  Get stag tpid index

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_stag_tpid_index(uint8 lchip, uint32 gport, ctc_direction_t dir, uint8* p_index);

/**
 @brief Set port cross connect forwarding by port if enable

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] nhid specified for port cross connect dest port

 @remark  Set port cross connect enable/disable, the packet will be forwarding by port ID without looking up mac or routing
          when enable the function

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_cross_connect(uint8 lchip, uint32 gport, uint32 nhid);

/**
 @brief Get port cross connect

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[out] p_value cross connect is enable on the port if enable set

 @remark  Get port cross connect enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_cross_connect(uint8 lchip, uint32 gport, uint32* p_value);

/**
 @brief Set learning enable/disable on this port

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] enable mac learning is enable on the port if enable set

 @remark  Enable/disable learning on this port, so a packet with a unknown source address will be learned when enable
          learning

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_learning_en(uint8 lchip, uint32 gport, bool enable);

/**
 @brief Get learning enable/disable on the port

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[out] p_enable mac learning is enable on the port if enable set

 @remark  Get learning enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_learning_en(uint8 lchip, uint32 gport, bool* p_enable);

/**
 @brief Set use outer ttl in case of tunnel

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] enable a boolen vaule denote the function is enable/disable

 @remark  When a packet is a tunnel application, it will use the outer TTL if enable the property

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_use_outer_ttl(uint8 lchip, uint32 gport, bool enable);

/**
 @brief Get use outer ttl in case of tunnel

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[out] p_enable a boolen vaule denote the function is enable/disable

 @remark  Get use outer TTL enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_use_outer_ttl(uint8 lchip, uint32 gport, bool* p_enable);

/**
 @brief untag transmit packet's vid if packet's vid equal to default vid of port

 @param[in] lchip    local chip id

 @param[in] gport global port of the system

 @param[in] enable a boolen vaule denote the function is enable/disable

 @param[in] untag_svlan untag svlan or cvlan of transmit packet

 @remark  untag transmit packet's vid if packet's vid equal to default vid of port

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_port_set_untag_dft_vid(uint8 lchip, uint32 gport, bool enable, bool untag_svlan);

/**
 @brief get untag default vid enable/disable on svlan or cvlan

 @param[in] lchip    local chip id

 @param[in] gport global port of the system

 @param[out] p_enable a boolen vaule denote the function is enable/disable

 @param[out] p_untag_svlan untag svlan or cvlan of transmit packet

 @remark  get untag default vid enable/disable on svlan or cvlan.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_port_get_untag_dft_vid(uint8 lchip, uint32 gport, bool* p_enable, bool* p_untag_svlan);

/**
 @brief Set protocol based vlan class enable/disable on the port

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] enable a boolen vaule denote the function is enable/disable

 @remark  A untag packet can be classed to a vlan by port, mac, or protocol(refer to ctc_vlan_add_vlan_class()),
          it must enable the property when class by protocol

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_protocol_vlan_en(uint8 lchip, uint32 gport, bool enable);

/**
 @brief Get protocol vlan enable/disable of the port

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[out] p_enable A boolen vaule denote the function is enable/disable

 @remark  Get protocol based vlan class enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_protocol_vlan_en(uint8 lchip, uint32 gport, bool* p_enable);

/**
 @brief Set random log function of port

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] dir   flow direction, refer to ctc_direction_t

 @param[in] enable A boolen vaule denote the function is enable/disable

 @remark  Enable/disable random log function which used for statistics

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_random_log_en(uint8 lchip, uint32 gport, ctc_direction_t dir, bool enable);

/**
 @brief Get random log function of port

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] dir   flow direction, refer to ctc_direction_t

 @param[out] p_enable A boolen vaule denote the function is enable/disable

 @remark  Get random log enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_random_log_en(uint8 lchip, uint32 gport, ctc_direction_t dir, bool* p_enable);

/**
 @brief Set the percentage of received packet for random log

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] dir  flow direction, refer to ctc_direction_t

 @param[in] percent The percentage of received packet for random log,and the range of percent is 0-100

 @remark  Set the percentage of received packet for random log

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_random_log_percent(uint8 lchip, uint32 gport, ctc_direction_t dir, uint8 percent);

/**
 @brief Get the percentage of received packet for random log

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] dir  flow direction, refer to ctc_direction_t

 @param[out] p_percent The percentage of received packet for random log,and the range of percent is 0-100

 @remark  Get the percentage of received packet for random log

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_random_log_percent(uint8 lchip, uint32 gport, ctc_direction_t dir, uint8* p_percent);

/**
 @brief The function is to set scl key type on the port

 @param[in] lchip    local chip id

 @param[in] gport  global physical port

 @param[in] dir  flow direction, refer to ctc_direction_t

 @param[in] scl_id  There are 2 scl lookup<0-1>, and each has its own feature

 @param[in] type   scl key type

 @remark  Set scl key type on the port, refer to ctc_port_scl_key_type_t

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_scl_key_type(uint8 lchip, uint32 gport, ctc_direction_t dir, uint8 scl_id, ctc_port_scl_key_type_t type);

/**
 @brief The function is to get scl key type on the port

 @param[in] lchip    local chip id

 @param[in] gport  global physical port

 @param[in] dir  flow direction, refer to ctc_direction_t

 @param[in] scl_id  There are 2 scl lookup<0-1>, and each has its own feature

 @param[out] p_type   scl key type

 @remark  Get scl key type on the port

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_scl_key_type(uint8 lchip, uint32 gport, ctc_direction_t dir, uint8 scl_id, ctc_port_scl_key_type_t* p_type);

/**
 @brief Set scl property to gport. Provide more feature to config compare to ctc_port_set_scl_key_type.

 @param[in] lchip    local chip id

 @param[in] gport  global physical port

 @param[in] prop   scl property

 @remark  Set scl property on the port.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_port_set_scl_property(uint8 lchip, uint32 gport, ctc_port_scl_property_t* prop);

/**
 @brief Get scl property of gport.

 @param[in] lchip    local chip id

 @param[in] gport  global physical port

 @param[in] prop   scl property

 @remark  Get scl property on the port.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_port_get_scl_property(uint8 lchip, uint32 gport, ctc_port_scl_property_t* prop);


/**
 @brief The function is to set vlan range on the port

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] p_vrange_info vlan range info, refer to ctc_vlan_range_info_t

 @param[in] enable enable or disable vlan range

 @remark  The vlan ID within a range can be mapped into a vlan, but it must enable the vlan range by the API at first

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_vlan_range(uint8 lchip, uint32 gport, ctc_vlan_range_info_t* p_vrange_info, bool enable);

/**
 @brief The function is to get vlan range on the port

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in|out] p_vrange_info vlan range info, refer to ctc_vlan_range_info_t

 @param[out] p_enable enable or disable vlan range

 @remark  Get vlan range enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_vlan_range(uint8 lchip, uint32 gport, ctc_vlan_range_info_t* p_vrange_info, bool* p_enable);

/**
 @brief Set port speed mode

 @param[in] lchip    local chip id

 @param[in] gport       global physical port

 @param[in] speed_mode  Speed at 10M 100M 1G of Gmac

 @remark  When enable mac, set the speed for Gmac, refer to ctc_port_speed_t

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_speed(uint8 lchip, uint32 gport, ctc_port_speed_t speed_mode);

/**
 @brief Set port speed mode

 @param[in] lchip    local chip id

 @param[in] gport       global physical port

 @param[out] p_speed_mode Speed at 10M 100M 1G of Gmac

 @remark  Get the speed of Gmac

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_speed(uint8 lchip, uint32 gport, ctc_port_speed_t* p_speed_mode);

/**
 @brief Set max frame size per port

 @param[in] lchip    local chip id

 @param[in] gport   global physical port

 @param[in] value   max frame size

 @remark  Select a max frame size of the packet on the port by the API, refer to ctc_set_max_frame_size().

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_max_frame(uint8 lchip, uint32 gport, uint32 value);

/**
 @brief Get max frame size per port

 @param[in] lchip    local chip id

 @param[in] gport   global physical port

 @param[out] p_value max frame size

 @remark  Get the mac frame size index on the port

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_max_frame(uint8 lchip, uint32 gport, uint32* p_value);

/**
 @brief Set cpu mac enable

 @param[in] lchip    local chip id

 @param[in] enable   a boolean value denote the function enable or not

 @remark  Enable/disable cpu mac

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_cpu_mac_en(uint8 lchip, bool enable);

/**
 @brief Get cpu mac enable

 @param[in] lchip    local chip id

 @param[out] p_enable    a boolean value denote the function enable or not

 @remark  Get cpu mac enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_cpu_mac_en(uint8 lchip, bool* p_enable);

/**
 @brief Set flow control of port

 @param[in] lchip    local chip id

 @param[in] p_fc_prop Point to flow control property

 @remark  Enable/disable flow control or priority flow control

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_flow_ctl_en(uint8 lchip, ctc_port_fc_prop_t* p_fc_prop);

/**
 @brief Get flow control of port

 @param[in] lchip    local chip id

 @param[in] p_fc_prop point to flow control property

 @remark  Get flow control or priority flow control enable/disable


 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_flow_ctl_en(uint8 lchip, ctc_port_fc_prop_t* p_fc_prop);

/**
 @brief Set port preamble length

 @param[in] lchip    local chip id

 @param[in] gport   global physical port

 @param[in] pre_bytes   preamble length value

 @remark  Set the preamble length on the port, for xgma/sgmac the pre_bytes can only be 4bytes and 8bytes

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_preamble(uint8 lchip, uint32 gport, uint8 pre_bytes);

/**
 @brief Get port preamble length

 @param[in] lchip    local chip id

 @param[in] gport       global physical port

 @param[out] p_pre_bytes   preamble length value

 @remark  Get the preamble length on the port

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_preamble(uint8 lchip, uint32 gport, uint8* p_pre_bytes);

/**
 @brief Set port min frame size

 @param[in] lchip    local chip id

 @param[in] gport       global physical port

 @param[in] size   min frame size

 @remark  Set the minimum frame size on the port, but it not support for xgma/sgmac because they are fixed. The packet
          shorter than min frame size will be dropped on the port.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_min_frame_size(uint8 lchip, uint32 gport, uint8 size);

/**
 @brief Get port min frame size

 @param[in] lchip    local chip id

 @param[in] gport       global physical port

 @param[out] p_size   min frame size

 @remark  Get the minimum frame size on the port

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_min_frame_size(uint8 lchip, uint32 gport, uint8* p_size);

/**
 @brief Set port pading

 @param[in] lchip    local chip id

 @param[in] gport       global physical port

 @param[in] enable      a boolean value denote the function enable or not

 @remark   If a packet length less than 63bytes then the packet length will be paded to be 64bytesk when enable the function

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_pading_en(uint8 lchip, uint32 gport, bool enable);

/**
 @brief Get port pading

 @param[in] lchip    local chip id

 @param[in] gport       global physical port

 @param[out] p_enable      a boolean value denote the function enable or not

 @remark  Get pading enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_pading_en(uint8 lchip, uint32 gport, bool* p_enable);

/**
 @brief Set port whether the srcdiscard is enable

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[in] enable a boolean value wanted to set

 @remark  The packet arrives at the port will be discarded without any other operation

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_srcdiscard_en(uint8 lchip, uint32 gport, bool enable);

/**
 @brief Get port whether the srcdiscard is enable

 @param[in] lchip    local chip id

 @param[in] gport global physical port

 @param[out] p_enable a boolean value denote the function enable or not

 @remark  Get port whether the srcdiscard is enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_srcdiscard_en(uint8 lchip, uint32 gport, bool* p_enable);

/**
 @brief Set port loopback

 @param[in] lchip    local chip id

 @param[in] p_port_lbk point to  ctc_port_lbk_param_t

 @remark Set loopback property on the port, use ctc_port_lbk_param_t->ctc_port_lbk_param_t to set source port for loopback,
         and use ctc_port_lbk_param_t->dst_gport to set destination port for loopback, refer to ctc_port_lbk_param_t

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_loopback(uint8 lchip, ctc_port_lbk_param_t* p_port_lbk);

/**
 @brief Set port port check enable

 @param[in] lchip    local chip id

 @param[in] gport  global physical port

 @param[in] enable  a boolean value wanted to set

 @remark  If the dest port is the source port, the packet will be dropped when bridge if enable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_port_check_en(uint8 lchip, uint32 gport, bool enable);

/**
 @brief Get port port check enable

 @param[in] lchip    local chip id

 @param[in] gport  global physical port

 @param[out] p_enable  a boolean value wanted to get

 @remark  Get port check enable/disable

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_port_check_en(uint8 lchip, uint32 gport, bool* p_enable);

/**
 @brief Set ipg size of system

 @param[in] lchip    local chip id

 @param[in] index   indicate index of ipg to be set

 @param[in] size    ipg size

 @remark  Set the IPG(Interpacket Gap) size, there are four size can be configured, then use ctc_port_set_ipg() to
          select which size to use

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_set_ipg_size(uint8 lchip, ctc_ipg_size_t index, uint8 size);

/**
 @brief Get ipg size of system

 @param[in] lchip    local chip id

 @param[in] index   indicate index of ipg to be set

 @param[out] p_size   ipg size

 @remark  Get the IPG(Interpacket Gap) size

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_get_ipg_size(uint8 lchip, ctc_ipg_size_t index, uint8* p_size);

/**
 @brief Set ipg index per port

 @param[in] lchip    local chip id

 @param[in] gport   global physical port

 @param[in] index   size index

 @remark  Select the IPG size from the four value which configured by ctc_set_ipg_size()

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_ipg(uint8 lchip, uint32 gport, ctc_ipg_size_t index);

/**
 @brief Get ipg index per port

 @param[in] lchip    local chip id

 @param[in] gport   global physical port

 @param[out] p_index  size index

 @remark  Get the selected IPG size index

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_ipg(uint8 lchip, uint32 gport, ctc_ipg_size_t* p_index);

/**
 @brief  Set isolated id with port isolation

 @param[in] lchip    local chip id

 @param[in] gport            global port

 @param[in] p_restriction    port restriction parameter

 @remark
    Configure restriction on port: private vlan, port isolation and blocking based on port.
    \p
    restriction_type will be set as follows:
    \p
       CTC_PORT_RESTRICTION_PVLAN            private vlan is enabled on port;
       \p
       CTC_PORT_RESTRICTION_BLOCKING         blocking based on port is enabled on port;
       \p
       CTC_PORT_RESTRICTION_PORT_ISOLATION   port isolation is enabled on port;
       \p
       CTC_PORT_RESTRICTION_DISABLE          private vlan, blocking and port isolation is disable on port;
       \p

    If pvlan is enabled on port, pvlan_port_type must be set as promiscuous, isolated, community(port type in private vlan function),
    and if the port is community port, isolated_id must be configured.
    \p
    If port isolation is enabled on port, isolated_id and dir must be configured. And if the dir is egress,
    isolation_type can be set(see ctc_port_isolation_pkt_type_t).
    \p
    If blocking is enabled on port, blocking_type must be set(see ctc_port_blocking_pkt_type_t). if blocking_type is set 0,
    unknown ucast, unknown mcast, known mcast, bcast will be not blocked.
    \p
    NOTE: pvlan, port isolation, blocking based on port can not be enabled on port together. If you want to enable
    another feature, you must disable the feature configured on port first.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_restriction(uint8 lchip, uint32 gport, ctc_port_restriction_t* p_restriction);

/**
 @brief  Get isolated id with port isolation

 @param[in] lchip    local chip id

 @param[in] gport            global port

 @param[out] p_restriction    port restriction parameter

 @remark
    Get configuration info about restriction on port.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_restriction(uint8 lchip, uint32 gport, ctc_port_restriction_t* p_restriction);

/**
 @brief Set port property

 @param[in] lchip    local chip id

 @param[in] gport        global physical port

 @param[in] port_prop    port property

 @param[in] value        port property value

 @remark  The API provides a ability to control the various features on the port, refer to ctc_port_property_t

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_property(uint8 lchip, uint32 gport, ctc_port_property_t port_prop, uint32 value);

/**
 @brief Get port property

 @param[in] lchip    local chip id

 @param[in] gport        global physical port

 @param[in] port_prop    port property

 @param[out] p_value        port property value

 @remark  Get the property value on the port

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_property(uint8 lchip, uint32 gport, ctc_port_property_t port_prop, uint32* p_value);

/**
 @brief Set port property with direction

 @param[in] lchip    local chip id

 @param[in] gport        global physical port
 @param[in] port_prop    port property
 @param[in] dir          direction
 @param[in] value        port property value

 @remark  The API provides a ability to control the various features on the port with direction, refer to ctc_port_property_t

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_direction_property(uint8 lchip, uint32 gport, ctc_port_direction_property_t port_prop, ctc_direction_t dir, uint32 value);

/**
 @brief Get port property with direction

 @param[in] lchip    local chip id

 @param[in]  gport        global physical port
 @param[in]  port_prop    port property
 @param[in]  dir          direction
 @param[out] p_value      port property value

 @remark  Get the property value on the port

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_direction_property(uint8 lchip, uint32 gport, ctc_port_direction_property_t port_prop, ctc_direction_t dir, uint32* p_value);

/**
 @brief Get port mac link up status

 @param[in] lchip    local chip id

 @param[in]  gport    global port of the system
 @param[out] p_is_up  0 means not link up, 1 means link up

 @remark  Get port mac link up status

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_mac_link_up(uint8 lchip, uint32 gport, bool* p_is_up);

/**
 @brief Set port mac prefix high 40bits

 @param[in] lchip    local chip id

 @param[in] p_port_mac     port mac high 40bits

 @remark  Set port mac prefix 40bits, up to two prefix mac.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_port_mac_prefix(uint8 lchip, ctc_port_mac_prefix_t* p_port_mac);

/**
 @brief Set port mac low 8bits

 @param[in] lchip    local chip id

 @param[in] gport        global port of the system
 @param[in] p_port_mac     port low 8bits, and which prefix mac to use

 @remark  Set port mac postfix 8bits. Per port to set, and prefix mac should indicated which to use.
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_port_mac_postfix(uint8 lchip, uint32 gport, ctc_port_mac_postfix_t* p_port_mac);

/**
 @brief Get port mac

 @param[in] lchip    local chip id

 @param[in]  gport       global port of the system
 @param[out] p_port_mac    port mac 48bits

 @remark  Get port mac 48bits.
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_port_mac(uint8 lchip, uint32 gport, mac_addr_t* p_port_mac);

/**
 @brief Set port reflective bridge enable

 @param[in] lchip    local chip id

 @param[in] gport  global port of the system
 @param[in] enable enable port reflective bridge

 @remark  Set port reflective bridge enable, l2 bridge source port and dest port are the same.
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_reflective_bridge_en(uint8 lchip, uint32 gport, bool enable);

/**
 @brief Get port reflective bridge enable

 @param[in] lchip    local chip id

 @param[in] gport global port of the system
 @param[out] p_enable port reflective bridge enable status

 @remark  Get port reflective bridge enable status, 0. means reflective bridge disable, 1.means reflective bridge disable.
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_reflective_bridge_en(uint8 lchip, uint32 gport, bool* p_enable);

/**
 @brief Set port Auto-neg mode

 @param[in] lchip    local chip id

 @param[in] gport  global physical port
 @param[in] value  enable or disable this function

 @remark  Only effect for 1G MAC
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_auto_neg(uint8 lchip, uint32 gport, uint32 value);

/**
 @brief Set port link change interrupt

 @param[in] lchip    local chip id

 @param[in] gport   global physical port
 @param[in] value   enable or disable this function

 @remark  when link up status come from phy, should disable port mac link interrupt.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_link_intr(uint8 lchip, uint32 gport, uint32 value);


/**
 @brief Set port based MAC authentication

 @param[in] lchip    local chip id

 @param[in] gport   global port of the system
 @param[in] enable  a boolean value denote port based MAC auth enable or not

 @remark  Set port mac authentication enable, for src port. Used with CTC_L2_DFT_VLAN_FLAG_PORT_MAC_AUTH.
          If enable = 1, set property CTC_PORT_PROP_SRC_MISMATCH_EXCEPTION_EN and port security enable.
          if enable = 0, need to reset them.
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_set_mac_auth(uint8 lchip, uint32 gport, bool enable);

/**
 @brief Get port based MAC authentication

 @param[in] lchip    local chip id

 @param[in] gport       global port of the system
 @param[out] enable     a boolean value denote port based MAC auth enable or not

 @remark  Get port mac authentication enable. Used with CTC_L2_DFT_VLAN_FLAG_PORT_MAC_AUTH.
          If enable = 1, means set property CTC_PORT_PROP_SRC_MISMATCH_EXCEPTION_EN and port security enable.
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_port_get_mac_auth(uint8 lchip, uint32 gport, bool* enable);

/**
 @brief Get gport capability

 @param[in] lchip    local chip id

 @param[in] gport    global physical port

 @param[in] type    port capability type

 @param[out] p_value        port capability value

 @remark  Get the port capability value on the port

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_port_get_capability(uint8 lchip, uint32 gport, ctc_port_capability_type_t type, void* p_value);
/**@} end of @addgroup   */
#ifdef __cplusplus
}
#endif

#endif

